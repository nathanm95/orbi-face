#pragma once
#include <pebble.h>

// Integer helpers. The watch has no FPU, so anything that runs per pixel or
// per row is integer; floats are only used a few dozen times a frame.

static inline int32_t isqrt32(int32_t v) {
  if (v <= 0) return 0;
  uint32_t x = (uint32_t)v, r = 0, bit = 1u << 30;
  while (bit > x) bit >>= 2;
  while (bit) {
    if (x >= r + bit) {
      x -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return (int32_t)r;
}

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline float fclamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float flerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float fabs_(float v) { return v < 0 ? -v : v; }

static inline int rand_range(int lo, int hi) { return lo + rand() % (hi - lo + 1); }
static inline float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rand() % 1000) / 999.0f; }

// Radians -> Pebble trig units (TRIG_MAX_ANGLE per turn), kept positive.
static inline int32_t rad2trig(float rad) {
  int32_t a = (int32_t)(rad * (float)TRIG_MAX_ANGLE / 6.2831853f) % TRIG_MAX_ANGLE;
  return a < 0 ? a + TRIG_MAX_ANGLE : a;
}
#define DEG2TRIG(d) ((int32_t)((((d) % 360 + 360) % 360) * TRIG_MAX_ANGLE / 360))

static inline float fsin(float rad) { return (float)sin_lookup(rad2trig(rad)) / (float)TRIG_MAX_RATIO; }
static inline float fcos(float rad) { return (float)cos_lookup(rad2trig(rad)) / (float)TRIG_MAX_RATIO; }
static inline float fatan2(float y, float x) {
  float m = fabs_(x) > fabs_(y) ? fabs_(x) : fabs_(y);
  if (m == 0) return 0;
  int32_t a = atan2_lookup((int16_t)(y / m * 16000.0f), (int16_t)(x / m * 16000.0f));
  if (a > TRIG_MAX_ANGLE / 2) a -= TRIG_MAX_ANGLE;
  return (float)a * 6.2831853f / (float)TRIG_MAX_ANGLE;
}
// asin for |v| < 1, via the SDK's atan2 table.
static inline float fasin(float v) {
  float c2 = 1.0f - v * v;
  int32_t c = isqrt32((int32_t)(c2 * 1048576.0f));       // sqrt(c2) in Q10
  int32_t a = atan2_lookup((int16_t)(v * 1024.0f), (int16_t)c);
  if (a > TRIG_MAX_ANGLE / 2) a -= TRIG_MAX_ANGLE;
  return (float)a * 6.2831853f / (float)TRIG_MAX_ANGLE;
}
