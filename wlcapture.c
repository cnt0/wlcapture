#include <unistd.h>

#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/types.h>

#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"

#include <wayland-client.h>

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

const struct wl_interface *capture_interface =
    &ext_output_image_capture_source_manager_v1_interface;
const struct wl_interface *copy_interface =
    &ext_image_copy_capture_manager_v1_interface;

// this is ridiculous
typedef struct ext_output_image_capture_source_manager_v1 *capture_manager_t;
typedef struct ext_image_copy_capture_manager_v1 *copy_manager_t;
typedef struct ext_image_copy_capture_session_v1 *capture_session_t;
typedef struct ext_image_copy_capture_frame_v1 *capture_frame_t;
#define capture_manager_create_source \
  ext_output_image_capture_source_manager_v1_create_source
#define copy_manager_create_session \
  ext_image_copy_capture_manager_v1_create_session
#define copy_session_add_listener ext_image_copy_capture_session_v1_add_listener

struct app_state {
  struct wl_registry *registry;
  struct wl_display *display;
  struct wl_output *output;
  capture_manager_t capture_manager;
  copy_manager_t copy_manager;
  struct wl_shm *shm;
};

static void registry_handle_global(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name,
                                   const char *interface,
                                   uint32_t version) {
  struct app_state *state = data;
  if (!strcmp(interface, capture_interface->name)) {
    state->capture_manager =
        wl_registry_bind(registry, name, capture_interface, version);
  }
  if (!strcmp(interface, copy_interface->name)) {
    state->copy_manager =
        wl_registry_bind(registry, name, copy_interface, version);
  }
  if (!strcmp(interface, wl_output_interface.name)) {
    state->output =
        wl_registry_bind(registry, name, &wl_output_interface, version);
  }
  if (!strcmp(interface, wl_shm_interface.name)) {
    state->shm = wl_registry_bind(registry, name, &wl_shm_interface, version);
  }
}
static void registry_handle_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name) {
  // TODO
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

struct capture_state {
  uint32_t width;
  uint32_t height;
  enum wl_shm_format shm_format;
  uint32_t dmabuf_format;
  uint64_t modifiers[128];
  size_t modifiers_cnt;

  dev_t devices[128];
  size_t devices_cnt;
  uint8_t done;
};

static void capture_set_buffer_size(void *data,
                                    capture_session_t session,
                                    uint32_t width,
                                    uint32_t height) {
  struct capture_state *state = data;
  state->width = width;
  state->height = height;
}

static void capture_set_shm_format(void *data,
                                   capture_session_t session,
                                   enum wl_shm_format format) {
  struct capture_state *state = data;
  state->shm_format = format;
}

static void capture_set_dmabuf_format(void *data,
                                      capture_session_t session,
                                      uint32_t format,
                                      struct wl_array *modifiers) {
  struct capture_state *state = data;
  state->dmabuf_format = format;

  state->modifiers_cnt = 0;
  uint64_t *cur;
  wl_array_for_each(cur, modifiers) {
    state->modifiers[state->modifiers_cnt++] = *cur;
  }
}

static void capture_set_dmabuf_device(void *data,
                                      capture_session_t session,
                                      struct wl_array *devices) {
  struct capture_state *state = data;
  state->devices_cnt = 0;
  dev_t *cur, *elem;
  wl_array_for_each(cur, devices) {
    elem = &state->devices[state->devices_cnt++];
    memcpy(elem, cur, sizeof(dev_t));
  }
}

static void capture_set_done(void *data, capture_session_t session) {
  struct capture_state *state = data;
  state->done = 1;
}

static const struct ext_image_copy_capture_session_v1_listener listener = {
    .buffer_size = capture_set_buffer_size,
    .shm_format = capture_set_shm_format,
    .dmabuf_format = capture_set_dmabuf_format,
    .dmabuf_device = capture_set_dmabuf_device,
    .done = capture_set_done,
};

struct frame_data {
  uint32_t tv_sec_hi;
  uint32_t tv_sec_lo;
  uint32_t tv_nsec;
  uint8_t result;
  uint32_t failed_reason;
};

static void frame_transform(void *data,
                            capture_frame_t frame,
                            uint32_t transform) {}

static void frame_damage(void *data,
                         capture_frame_t frame,
                         int32_t x,
                         int32_t y,
                         int32_t width,
                         int32_t height) {}

static void frame_presentation_time(void *data,
                                    capture_frame_t frame,
                                    uint32_t tv_sec_hi,
                                    uint32_t tv_sec_lo,
                                    uint32_t tv_nsec) {
  struct frame_data *state = data;
  state->tv_sec_hi = tv_sec_hi;
  state->tv_sec_lo = tv_sec_lo;
  state->tv_nsec = tv_nsec;
}

static void frame_ready(void *data, capture_frame_t frame) {
  struct frame_data *state = data;
  state->result = 1;
}

static void frame_failed(void *data, capture_frame_t frame, uint32_t reason) {
  struct frame_data *state = data;
  state->result = 2;
  state->failed_reason = reason;
}

static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .transform = frame_transform,
    .damage = frame_damage,
    .presentation_time = frame_presentation_time,
    .ready = frame_ready,
    .failed = frame_failed,
};

static enum AVPixelFormat get_av_format(uint32_t wl_shm_format) {
  switch (wl_shm_format) {
    case WL_SHM_FORMAT_XRGB8888:
      return AV_PIX_FMT_BGR0;
    case WL_SHM_FORMAT_ARGB8888:
      return AV_PIX_FMT_BGRA;
    case WL_SHM_FORMAT_XBGR8888:
      return AV_PIX_FMT_RGB0;
    case WL_SHM_FORMAT_ABGR8888:
      return AV_PIX_FMT_RGBA;
    default:
      return AV_PIX_FMT_NB;
  }
}

struct shm_buf {
  int size;
  int stride;
  enum AVPixelFormat pixel_format;
  int fd;
  struct wl_buffer *buf;
  uint8_t *mapping;
};

typedef enum {
  SHM_BUF_STATE_NOT_DONE,
  SHM_BUF_UNKNOWN_PIXEL_FORMAT,
  // check errno in these 3 cases
  SHM_BUF_FD_ERROR,
  SHM_BUF_TRUNCATE_ERROR,
  SHM_BUF_MMAP_ERROR,
  SHM_BUF_POOL_ERROR,
  SHM_BUF_BUF_CREATE_ERROR,
  SHM_BUF_OK,
} shm_buf_error;

shm_buf_error shm_buf_create(struct wl_shm *shm,
                             const struct capture_state *state,
                             struct shm_buf *buf) {
  if (!state->done) {
    return SHM_BUF_STATE_NOT_DONE;
  }
  buf->pixel_format = get_av_format(state->shm_format);
  if (buf->pixel_format == AV_PIX_FMT_NB) {
    return SHM_BUF_UNKNOWN_PIXEL_FORMAT;
  }

  // alignment 32 seems OK?
  buf->size = av_image_get_buffer_size(buf->pixel_format, state->width,
                                       state->height, 32);
  buf->stride = av_image_get_linesize(buf->pixel_format, state->width, 0);
  buf->fd = memfd_create("membuf", MFD_CLOEXEC);
  if (buf->fd == -1) {
    return SHM_BUF_FD_ERROR;
  }
  int ret = ftruncate(buf->fd, buf->size);
  if (ret == -1) {
    return SHM_BUF_TRUNCATE_ERROR;
  }

  buf->mapping = mmap(NULL, buf->size, PROT_READ, MAP_SHARED, buf->fd, 0);
  if (buf->mapping == MAP_FAILED) {
    return SHM_BUF_MMAP_ERROR;
  }

  struct wl_shm_pool *shm_pool = wl_shm_create_pool(shm, buf->fd, buf->size);
  if (!shm_pool) {
    return SHM_BUF_POOL_ERROR;
  }
  buf->buf = wl_shm_pool_create_buffer(shm_pool, 0, state->width, state->height,
                                       buf->stride, state->shm_format);
  if (!buf->buf) {
    wl_shm_pool_destroy(shm_pool);
    return SHM_BUF_BUF_CREATE_ERROR;
  }
  wl_shm_pool_destroy(shm_pool);
  return SHM_BUF_OK;
}

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

static void app_state_free(struct app_state *state) {
  wl_shm_destroy(state->shm);
  ext_image_copy_capture_manager_v1_destroy(state->copy_manager);
  ext_output_image_capture_source_manager_v1_destroy(state->capture_manager);
  wl_output_destroy(state->output);
  wl_registry_destroy(state->registry);
  wl_display_disconnect(state->display);
}

static void capture_src_free(struct ext_image_capture_source_v1 **src) {
  ext_image_capture_source_v1_destroy(*src);
}

static void capture_session_free(
    struct ext_image_copy_capture_session_v1 **sess) {
  ext_image_copy_capture_session_v1_destroy(*sess);
}

static void shm_buf_free(struct shm_buf *shm_buf) {
  wl_buffer_destroy(shm_buf->buf);
  munmap(shm_buf->mapping, shm_buf->size);
}

static void frame_free(struct ext_image_copy_capture_frame_v1 **frame) {
  ext_image_copy_capture_frame_v1_destroy(*frame);
}

int main(int argc, char *argv[]) {
  struct app_state wl_state __attribute__((cleanup(app_state_free))) = {0};

  wl_state.display = wl_display_connect(NULL);
  if (wl_state.display == NULL) {
    exit(EXIT_FAILURE);
  }
  wl_state.registry = wl_display_get_registry(wl_state.display);
  if (wl_state.registry == NULL) {
    exit(EXIT_FAILURE);
  }
  wl_registry_add_listener(wl_state.registry, &registry_listener, &wl_state);
  wl_display_roundtrip(wl_state.display);

  struct ext_image_capture_source_v1 *src
      __attribute__((cleanup(capture_src_free))) =
          capture_manager_create_source(wl_state.capture_manager,
                                        wl_state.output);

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

  struct frame_data frame_data = {0};
  ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener,
                                               &frame_data);
  ext_image_copy_capture_frame_v1_capture(frame);
  while (!frame_data.result) {
    wl_display_dispatch(wl_state.display);
  }

  const AVCodec *codec = avcodec_find_encoder_by_name("libjxl");
  if (!codec) {
    puts("JXL not found");
    return EXIT_FAILURE;
  }
  AVDictionary *libjxl_opts __attribute__((cleanup(av_dict_free))) = NULL;

  // lossless JXL
  av_dict_set(&libjxl_opts, "distance", "0.0", 0);

  const enum AVPixelFormat av_tgt_fmt =
      av_get_desired_format(codec, shm_buf.pixel_format);

  AVCodecContext *ctx __attribute__((cleanup(avcodec_free_context))) =
      avcodec_alloc_context3(codec);
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

  ret = avcodec_open2(ctx, codec, &libjxl_opts);
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
    FILE *f = fopen("screen.jxl", "wb");
    if (f) {
      fwrite(pkt->data, 1, pkt->size, f);
      fclose(f);
    }
    av_packet_unref(pkt);
  }
}
