#pragma once

#define _GNU_SOURCE
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <wayland-client.h>

#include "ext-foreign-toplevel-list-v1.h"
#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"

static const struct wl_interface *capture_interface =
    &ext_output_image_capture_source_manager_v1_interface;
static const struct wl_interface *copy_interface =
    &ext_image_copy_capture_manager_v1_interface;
static const struct wl_interface *foreign_interface =
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

struct toplevel_data {
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

struct app_state {
  struct wl_registry *registry;
  struct wl_display *display;
  struct wl_output *output;
  capture_manager_t capture_manager;
  foreign_manager_t foreign_manager;
  struct ext_foreign_toplevel_list_v1 *toplevel_list;
  struct toplevel_state {
    struct toplevel_data handles[1024];
    size_t n_handles;
    uint8_t finished;
  } toplevel_state;
  copy_manager_t copy_manager;
  struct wl_shm *shm;
};

static void app_state_free(struct app_state *state) {
  ext_foreign_toplevel_list_v1_stop(state->toplevel_list);
  while (!state->toplevel_state.finished) {
    wl_display_roundtrip(state->display);
  }
  for (size_t i = 0; i < state->toplevel_state.n_handles; ++i) {
    struct toplevel_data *handle_info = state->toplevel_state.handles + i;
    if (!handle_info->closed) {
      // if closed then already destroyed
      ext_foreign_toplevel_handle_v1_destroy(handle_info->handle);
    }
  }
  ext_foreign_toplevel_list_v1_destroy(state->toplevel_list);
  wl_shm_destroy(state->shm);
  ext_foreign_toplevel_image_capture_source_manager_v1_destroy(
      state->foreign_manager);
  ext_image_copy_capture_manager_v1_destroy(state->copy_manager);
  ext_output_image_capture_source_manager_v1_destroy(state->capture_manager);
  wl_output_destroy(state->output);
  wl_registry_destroy(state->registry);
  wl_display_disconnect(state->display);
}

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

struct frame_state {
  uint32_t tv_sec_hi;
  uint32_t tv_sec_lo;
  uint32_t tv_nsec;
  uint8_t result;
  uint32_t failed_reason;
};

