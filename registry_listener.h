#pragma once

#include "state.h"

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
