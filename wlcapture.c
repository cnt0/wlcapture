#include "capture_frame_listener.h"
#include "capture_session_listener.h"
#include "opts.h"
#include "registry_listener.h"
#include "shm.h"
#include "state.h"
#include "toplevel_listener.h"

#include <libswscale/swscale.h>

static enum AVPixelFormat av_get_desired_format(const AVCodec *codec,
                                                enum AVPixelFormat src_format) {
  const enum AVPixelFormat *codec_formats = NULL;
  int codec_formats_len;
  int ret = avcodec_get_supported_config(
      NULL, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, (const void **)&codec_formats,
      &codec_formats_len);
  int losses;
  return avcodec_find_best_pix_fmt_of_list(codec_formats, src_format, 1,
                                           &losses);
}

static void capture_src_free(struct ext_image_capture_source_v1 **src) {
  if (*src) {
    ext_image_capture_source_v1_destroy(*src);
  }
}

static void capture_session_free(
    struct ext_image_copy_capture_session_v1 **sess) {
  ext_image_copy_capture_session_v1_destroy(*sess);
}

static void frame_free(struct ext_image_copy_capture_frame_v1 **frame) {
  ext_image_copy_capture_frame_v1_destroy(*frame);
}

static void file_close(FILE **file) {
  fclose(*file);
}

static int get_desired_toplevel(struct ext_image_capture_source_v1 **src,
                                const struct toplevel_state *state,
                                foreign_manager_t manager,
                                const char *uid,
                                size_t uid_len) {
  for (size_t i = 0; i < state->n_handles; ++i) {
    const struct toplevel_data *entry = state->handles + i;
    if (!strncmp(entry->uid, uid, uid_len)) {
      *src = foreign_manager_create_source(manager, entry->handle);
      return 1;
    }
  }
  return 0;
}

int main(int argc, char *argv[]) {
  struct app_opts opts __attribute__((cleanup(app_opts_free))) = {0};
  int opts_ret = app_opts_parse(argc, argv, &opts);
  if (opts_ret == EXIT_FAILURE) {
    return EXIT_FAILURE;
  }

  struct app_state wl_state __attribute__((cleanup(app_state_free))) = {0};

  wl_state.display = wl_display_connect(NULL);
  if (wl_state.display == NULL) {
    return EXIT_FAILURE;
  }
  wl_state.registry = wl_display_get_registry(wl_state.display);
  if (wl_state.registry == NULL) {
    return EXIT_FAILURE;
  }
  wl_registry_add_listener(wl_state.registry, &registry_listener, &wl_state);
  wl_display_roundtrip(wl_state.display);

  struct toplevel_state *tl_data = &wl_state.toplevel_state;
  ext_foreign_toplevel_list_v1_add_listener(wl_state.toplevel_list,
                                            &toplevel_list_listener, tl_data);

  struct ext_image_capture_source_v1 *src
      __attribute__((cleanup(capture_src_free))) = NULL;

  if (opts.uid_len == 0) {
    src = capture_manager_create_source(wl_state.capture_manager,
                                        wl_state.output);
  } else {
    while (!get_desired_toplevel(&src, tl_data, wl_state.foreign_manager,
                                 opts.uid, opts.uid_len)) {
      wl_display_roundtrip(wl_state.display);
    }
  }

  struct ext_image_copy_capture_session_v1 *session
      __attribute__((cleanup(capture_session_free))) =
          copy_manager_create_session(wl_state.copy_manager, src, 0);

  struct capture_state cp_state = {0};
  copy_session_add_listener(session, &listener, &cp_state);

  while (!cp_state.done) {
    wl_display_dispatch(wl_state.display);
  }
  struct shm_buf shm_buf __attribute__((cleanup(shm_buf_free))) = {0};
  shm_buf_error ret = shm_buf_create(wl_state.shm, &cp_state, &shm_buf);
  if (ret != SHM_BUF_OK) {
    return ret;
  }

  // according to protocol, we must create frame, attach buffer, then damage
  // this buffer
  struct ext_image_copy_capture_frame_v1 *frame
      __attribute__((cleanup(frame_free))) =
          ext_image_copy_capture_session_v1_create_frame(session);

  ext_image_copy_capture_frame_v1_attach_buffer(frame, shm_buf.buf);

  ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, cp_state.width,
                                                cp_state.height);

  struct frame_state frame_data = {0};
  ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener,
                                               &frame_data);
  ext_image_copy_capture_frame_v1_capture(frame);
  while (!frame_data.result) {
    wl_display_dispatch(wl_state.display);
  }

  // lossless JXL
  // av_dict_set(&libjxl_opts, "distance", "0.0", 0);

  const enum AVPixelFormat av_tgt_fmt =
      av_get_desired_format(opts.codec, shm_buf.pixel_format);

  AVCodecContext *ctx __attribute__((cleanup(avcodec_free_context))) =
      avcodec_alloc_context3(opts.codec);
  ctx->width = cp_state.width;
  ctx->height = cp_state.height;
  ctx->pix_fmt = av_tgt_fmt;
  AVRational time_base = {.num = 1, .den = 1};
  ctx->time_base = time_base;

  // seem to be sane defaults? IDK what to define here
  ctx->color_range = AVCOL_RANGE_JPEG;
  ctx->color_primaries = AVCOL_PRI_BT709;
  ctx->color_trc = AVCOL_TRC_BT709;
  ctx->colorspace = AVCOL_SPC_RGB;

  ret = avcodec_open2(ctx, opts.codec, &opts.codec_opts);
  if (ret < 0) {
    printf("Can't open codec: %d\n", ret);
    return EXIT_FAILURE;
  }

  AVFrame *src_fr __attribute__((cleanup(av_frame_free))) = av_frame_alloc();
  src_fr->width = cp_state.width;
  src_fr->height = cp_state.height;
  src_fr->format = shm_buf.pixel_format;
  av_image_fill_arrays(src_fr->data, src_fr->linesize, shm_buf.mapping,
                       shm_buf.pixel_format, cp_state.width, cp_state.height,
                       4);

  AVFrame *dst_fr __attribute__((cleanup(av_frame_free))) = av_frame_alloc();

  SwsContext *sw_scale_ctx = NULL;
  if (shm_buf.pixel_format != av_tgt_fmt) {
    dst_fr->width = cp_state.width;
    dst_fr->height = cp_state.height;
    dst_fr->format = av_tgt_fmt;
    av_frame_get_buffer(dst_fr, 0);

    sw_scale_ctx = sws_getContext(
        cp_state.width, cp_state.height, shm_buf.pixel_format, cp_state.width,
        cp_state.height, av_tgt_fmt, SWS_BILINEAR, NULL, NULL, NULL);

    sws_scale(sw_scale_ctx, (const uint8_t *const *)src_fr->data,
              src_fr->linesize, 0, cp_state.height, dst_fr->data,
              dst_fr->linesize);
    sws_freeContext(sw_scale_ctx);
  } else {
    av_frame_move_ref(dst_fr, src_fr);
  }

  dst_fr->color_range = ctx->color_range;
  dst_fr->color_primaries = ctx->color_primaries;
  dst_fr->color_trc = ctx->color_trc;
  dst_fr->colorspace = ctx->colorspace;

  AVPacket *pkt __attribute__((cleanup(av_packet_free))) = av_packet_alloc();
  if (!pkt) {
    return EXIT_FAILURE;
  }
  ret = avcodec_send_frame(ctx, dst_fr);
  if (ret) {
    return EXIT_FAILURE;
  }
  if (avcodec_receive_packet(ctx, pkt) == 0) {
    fwrite(pkt->data, 1, pkt->size, opts.outfile);
    av_packet_unref(pkt);
  }
}
