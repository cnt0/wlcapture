#include <unistd.h>

#define _GNU_SOURCE
#include <getopt.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"
#include "ext-foreign-toplevel-list-v1.h"

#include <wayland-client.h>

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

const struct wl_interface *capture_interface =
    &ext_output_image_capture_source_manager_v1_interface;
const struct wl_interface *copy_interface =
    &ext_image_copy_capture_manager_v1_interface;
const struct wl_interface *foreign_interface =
    &ext_foreign_toplevel_image_capture_source_manager_v1_interface;

// this is ridiculous
typedef struct ext_output_image_capture_source_manager_v1 *capture_manager_t;
typedef struct ext_image_copy_capture_manager_v1 *copy_manager_t;
typedef struct ext_image_copy_capture_session_v1 *capture_session_t;
typedef struct ext_image_copy_capture_frame_v1 *capture_frame_t;
typedef struct ext_foreign_toplevel_image_capture_source_manager_v1
    *foreign_manager_t;

#define capture_manager_create_source \
  ext_output_image_capture_source_manager_v1_create_source
#define foreign_manager_create_source \
  ext_foreign_toplevel_image_capture_source_manager_v1_create_source
#define copy_manager_create_session \
  ext_image_copy_capture_manager_v1_create_session
#define copy_session_add_listener ext_image_copy_capture_session_v1_add_listener

struct app_state {
  struct wl_registry *registry;
  struct wl_display *display;
  struct wl_output *output;
  capture_manager_t capture_manager;
  foreign_manager_t foreign_manager;
  struct ext_foreign_toplevel_list_v1 *toplevel_list;
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
  if (!strcmp(interface, foreign_interface->name)) {
    state->foreign_manager =
        wl_registry_bind(registry, name, foreign_interface, version);
  }
  if (!strcmp(interface, ext_foreign_toplevel_list_v1_interface.name)) {
    state->toplevel_list = wl_registry_bind(
        registry, name, &ext_foreign_toplevel_list_v1_interface, version);
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
  uint64_t *cur, *elem;
  wl_array_for_each(cur, modifiers) {
    elem = &state->modifiers[state->modifiers_cnt++];
    memcpy(elem, cur, sizeof(*elem));
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
    memcpy(elem, cur, sizeof(*cur));
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

struct toplevel_handle_info {
  struct ext_foreign_toplevel_handle_v1 *handle;
  char app_id[1024];
  size_t app_id_len;
  char title[1024];
  size_t title_len;
  char uid[1024];
  size_t uid_len;
  uint8_t closed;
  uint8_t done;
};

static void toplevel_handle_closed(
    void *data,
    struct ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel_handle_v1) {
  struct toplevel_handle_info *state = data;
  ext_foreign_toplevel_handle_v1_destroy(state->handle);
  printf("OK destroyed %s\n", state->app_id);
  state->closed = 1;
}
static void toplevel_handle_done(
    void *data,
    struct ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel_handle_v1) {
  struct toplevel_handle_info *state = data;
  state->done = 1;
}
static void toplevel_handle_title(
    void *data,
    struct ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel_handle_v1,
    const char *title) {
  struct toplevel_handle_info *state = data;
  state->title_len = strnlen(title, 1024);
  memcpy(state->title, title, state->title_len);
}
static void toplevel_handle_app_id(
    void *data,
    struct ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel_handle_v1,
    const char *app_id) {
  struct toplevel_handle_info *state = data;
  state->app_id_len = strnlen(app_id, 1024);
  memcpy(state->app_id, app_id, state->app_id_len);
}
static void toplevel_handle_uniq_id(
    void *data,
    struct ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel_handle_v1,
    const char *uniq_id) {
  struct toplevel_handle_info *state = data;
  state->uid_len = strnlen(uniq_id, 1024);
  memcpy(state->uid, uniq_id, state->uid_len);
}

static const struct ext_foreign_toplevel_handle_v1_listener
    toplevel_handle_listener = {
        .closed = toplevel_handle_closed,
        .done = toplevel_handle_done,
        .title = toplevel_handle_title,
        .app_id = toplevel_handle_app_id,
        .identifier = toplevel_handle_uniq_id,
};

struct toplevel_data {
  struct toplevel_handle_info handles[1024];
  size_t n_handles;
  uint8_t finished;
};

static void toplevel_add_handle(
    void *data,
    struct ext_foreign_toplevel_list_v1 *ext_foreign_toplevel_list_v1,
    struct ext_foreign_toplevel_handle_v1 *toplevel) {
  struct toplevel_data *state = data;
  struct toplevel_handle_info *current_info =
      &state->handles[state->n_handles++];
  current_info->handle = toplevel;
  ext_foreign_toplevel_handle_v1_add_listener(
      toplevel, &toplevel_handle_listener, current_info);
}

static void toplevel_finished(
    void *data,
    struct ext_foreign_toplevel_list_v1 *ext_foreign_toplevel_list_v1) {
  struct toplevel_data *state = data;
  state->finished = 1;
}

static const struct ext_foreign_toplevel_list_v1_listener
    toplevel_list_listener = {
        .toplevel = toplevel_add_handle,
        .finished = toplevel_finished,
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
  ext_foreign_toplevel_list_v1_destroy(state->toplevel_list);
  ext_foreign_toplevel_image_capture_source_manager_v1_destroy(
      state->foreign_manager);
  ext_image_copy_capture_manager_v1_destroy(state->copy_manager);
  ext_output_image_capture_source_manager_v1_destroy(state->capture_manager);
  wl_output_destroy(state->output);
  wl_registry_destroy(state->registry);
  wl_display_disconnect(state->display);
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

static void shm_buf_free(struct shm_buf *shm_buf) {
  wl_buffer_destroy(shm_buf->buf);
  munmap(shm_buf->mapping, shm_buf->size);
}

static void frame_free(struct ext_image_copy_capture_frame_v1 **frame) {
  ext_image_copy_capture_frame_v1_destroy(*frame);
}

static void file_close(FILE **file) {
  fclose(*file);
}

static void toplevel_data_free(struct toplevel_data *toplevels) {
  for (size_t i = 0; i < toplevels->n_handles; ++i) {
    struct toplevel_handle_info *entry = toplevels->handles + i;
    if (entry->closed == 1) {
      // already destroyed
      return;
    }
    ext_foreign_toplevel_handle_v1_destroy(entry->handle);
  }
}

enum opts { OPT_CODEC, OPT_CODEC_OPTS, OPT_UID, OPT_HELP, OPT_LAST };

char HELP[] =
    "wlcapture [--codec=CODEC] [--codec-opts=key1=value1,...] [--uid=UID] "
    "[OUTPUT_FILE]\n\n"
    "Default CODEC is libjxl (JPEG XL).\n"
    "You can try any codec from `ffmpeg -encoders` command.\n\n"
    "Check the list of supported options for given codec here:\n"
    "https://ffmpeg.org/ffmpeg-codecs.html\n\n"
    "if --uid is given then the window with UID will be captured\n"
    "UID is foreign toplevel identifier, in sway terminology\n"
    "you can find it in `swaymsg -t get_tree -r`\n\n"
    "OUTPUT_FILE: where to save the encoded image. Possible values:\n"
    "(not given)\tprint binary data to stdout\n"
    "-\t\tprint binary data to stdout\n"
    "some path\tsave image to that path";

int main(int argc, char *argv[]) {
  static struct option opts[] = {
      {"codec", required_argument, NULL, OPT_CODEC},
      {"codec-opts", required_argument, NULL, OPT_CODEC_OPTS},
      {"uid", required_argument, NULL, OPT_UID},
      {"help", no_argument, NULL, OPT_HELP},
      {0, 0, 0, 0},
  };

  const AVCodec *codec = NULL;
  AVDictionary *codec_opts __attribute__((cleanup(av_dict_free))) = NULL;
  FILE *outfile __attribute__((__cleanup__(file_close))) = stdout;
  char uid[1024] = {0};
  size_t uid_len = 0;

  int n_opts;
  int c;
  while ((c = getopt_long(argc, argv, "", opts, &n_opts)) != -1) {
    switch (c) {
      case OPT_CODEC:
        codec = avcodec_find_encoder_by_name(optarg);
        if (!codec) {
          printf("Codec '%s' not found", optarg);
          return EXIT_FAILURE;
        }
        break;
      case OPT_CODEC_OPTS:
        if (av_dict_parse_string(&codec_opts, optarg, "=", ",", 0)) {
          printf("Cannot parse codec opts string '%s'\n", optarg);
        }
        break;
      case OPT_UID:
        uid_len = strnlen(optarg, 1024);
        memcpy(uid, optarg, uid_len);
        break;
      case OPT_HELP:
        puts(HELP);
        return EXIT_SUCCESS;
      default:
        puts("Unknown option");
        return EXIT_FAILURE;
    }
  }
  if (optind < argc && strcmp(argv[optind], "-")) {
    outfile = fopen(argv[optind], "wb");
  }
  if (!outfile) {
    puts("Cannot open output file for writing");
    return EXIT_FAILURE;
  }

  // default value for codec
  if (!codec) {
    codec = avcodec_find_encoder_by_name("libjxl");
    if (!codec) {
      puts("Codec 'libjxl' not found");
      return EXIT_FAILURE;
    }
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

  struct ext_image_capture_source_v1 *src
      __attribute__((cleanup(capture_src_free))) = NULL;

  if (uid_len == 0) {
    src = capture_manager_create_source(wl_state.capture_manager,
                                        wl_state.output);
  } else {
    struct toplevel_data tl_data
        __attribute__((cleanup(toplevel_data_free))) = {0};
    ext_foreign_toplevel_list_v1_add_listener(
        wl_state.toplevel_list, &toplevel_list_listener, &tl_data);
    for (int done = 0; done == 0;) {
      wl_display_roundtrip(wl_state.display);
      for (size_t i = 0; i < tl_data.n_handles; ++i) {
        struct toplevel_handle_info *entry = tl_data.handles + i;
        if (!strncmp(entry->uid, uid, uid_len)) {
          done = 1;
          src = foreign_manager_create_source(wl_state.foreign_manager,
                                              entry->handle);
          break;
        }
      }
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

  struct frame_data frame_data = {0};
  ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener,
                                               &frame_data);
  ext_image_copy_capture_frame_v1_capture(frame);
  while (!frame_data.result) {
    wl_display_dispatch(wl_state.display);
  }

  // lossless JXL
  // av_dict_set(&libjxl_opts, "distance", "0.0", 0);

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

  ret = avcodec_open2(ctx, codec, &codec_opts);
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
    fwrite(pkt->data, 1, pkt->size, outfile);
    av_packet_unref(pkt);
  }
}
