#pragma once

#include "state.h"

static void toplevel_handle_closed(
    void *data,
    struct ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel_handle_v1) {
  struct toplevel_data *state = data;
  ext_foreign_toplevel_handle_v1_destroy(state->handle);
  state->closed = 1;
}

static void toplevel_handle_done(
    void *data,
    struct ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel_handle_v1) {
  struct toplevel_data *state = data;
  state->done = 1;
}

static void toplevel_handle_title(
    void *data,
    struct ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel_handle_v1,
    const char *title) {
  struct toplevel_data *state = data;
  state->title_len = strnlen(title, 1024);
  memcpy(state->title, title, state->title_len);
}

static void toplevel_handle_app_id(
    void *data,
    struct ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel_handle_v1,
    const char *app_id) {
  struct toplevel_data *state = data;
  state->app_id_len = strnlen(app_id, 1024);
  memcpy(state->app_id, app_id, state->app_id_len);
}

static void toplevel_handle_uniq_id(
    void *data,
    struct ext_foreign_toplevel_handle_v1 *ext_foreign_toplevel_handle_v1,
    const char *uniq_id) {
  struct toplevel_data *state = data;
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

static void toplevel_add_handle(
    void *data,
    struct ext_foreign_toplevel_list_v1 *ext_foreign_toplevel_list_v1,
    struct ext_foreign_toplevel_handle_v1 *toplevel) {
  struct toplevel_state *state = data;
  struct toplevel_data *current_info =
      &state->handles[state->n_handles++];
  current_info->handle = toplevel;
  ext_foreign_toplevel_handle_v1_add_listener(
      toplevel, &toplevel_handle_listener, current_info);
}

static void toplevel_finished(
    void *data,
    struct ext_foreign_toplevel_list_v1 *ext_foreign_toplevel_list_v1) {
  struct toplevel_state *state = data;
  state->finished = 1;
}

static const struct ext_foreign_toplevel_list_v1_listener
    toplevel_list_listener = {
        .toplevel = toplevel_add_handle,
        .finished = toplevel_finished,
};

