#pragma once

#include "state.h"

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
