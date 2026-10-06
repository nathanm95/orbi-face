// Host-side stand-in for <pebble.h>: just enough for the portable files
// (raster, bg, face, anim, acts) to compile on the Mac, so frames can be
// rendered and checked there instead of in the emulator. See harness.c.
#pragma once
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRIG_MAX_ANGLE 0x10000
#define TRIG_MAX_RATIO 0xffff

static inline int32_t sin_lookup(int32_t angle) {
  return (int32_t)lround(sin((double)angle * 2 * M_PI / TRIG_MAX_ANGLE) * TRIG_MAX_RATIO);
}
static inline int32_t cos_lookup(int32_t angle) {
  return (int32_t)lround(cos((double)angle * 2 * M_PI / TRIG_MAX_ANGLE) * TRIG_MAX_RATIO);
}
// Same contract as the SDK: 0..TRIG_MAX_ANGLE, counter-clockwise from +x.
static inline int32_t atan2_lookup(int16_t y, int16_t x) {
  double a = atan2((double)y, (double)x);
  if (a < 0) a += 2 * M_PI;
  return (int32_t)lround(a * TRIG_MAX_ANGLE / (2 * M_PI)) % TRIG_MAX_ANGLE;
}

#define APP_LOG_LEVEL_DEBUG 0
#define APP_LOG(level, fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
