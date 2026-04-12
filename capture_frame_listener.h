#pragma once

#include "state.h"

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
  struct frame_state *state = data;
  state->tv_sec_hi = tv_sec_hi;
  state->tv_sec_lo = tv_sec_lo;
  state->tv_nsec = tv_nsec;
}

static void frame_ready(void *data, capture_frame_t frame) {
  struct frame_state *state = data;
  state->result = 1;
}

static void frame_failed(void *data, capture_frame_t frame, uint32_t reason) {
  struct frame_state *state = data;
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
