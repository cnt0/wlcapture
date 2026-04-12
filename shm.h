#pragma once

#include "state.h"

#include <unistd.h>
#include <sys/mman.h>

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>

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

static shm_buf_error shm_buf_create(struct wl_shm *shm,
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

static void shm_buf_free(struct shm_buf *shm_buf) {
  wl_buffer_destroy(shm_buf->buf);
  munmap(shm_buf->mapping, shm_buf->size);
}

