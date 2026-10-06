#include "bg.h"
#include "fixed.h"
#include "raster.h"

const Theme THEMES[THEME_COUNT] = {
  // Classic: Sphere-yellow with a warm orange shadow.
  [THEME_CLASSIC] = { { RGB(3, 1, 0), RGB(3, 2, 0), RGB(3, 3, 0), RGB(3, 3, 1), RGB(3, 3, 2) },
                      RGB(3, 1, 1), RGB(3, 2, 1), RGB(1, 0, 0), RGB(3, 1, 1) },
  [THEME_TANGERINE] = { { RGB(2, 1, 0), RGB(3, 1, 0), RGB(3, 2, 0), RGB(3, 2, 1), RGB(3, 3, 2) },
                        RGB(3, 0, 1), RGB(3, 2, 2), RGB(1, 0, 0), RGB(3, 1, 2) },
  [THEME_BUBBLEGUM] = { { RGB(2, 0, 1), RGB(3, 0, 2), RGB(3, 1, 2), RGB(3, 2, 3), RGB(3, 3, 3) },
                        RGB(3, 0, 1), RGB(3, 1, 1), RGB(1, 0, 1), RGB(3, 2, 2) },
  [THEME_MINT] = { { RGB(0, 2, 1), RGB(1, 3, 1), RGB(2, 3, 2), RGB(2, 3, 3), RGB(3, 3, 3) },
                   RGB(3, 1, 1), RGB(3, 2, 2), RGB(1, 0, 0), RGB(3, 1, 1) },
  [THEME_SKY] = { { RGB(0, 0, 2), RGB(0, 1, 3), RGB(1, 2, 3), RGB(2, 3, 3), RGB(3, 3, 3) },
                  RGB(3, 1, 2), RGB(3, 2, 3), RGB(1, 0, 0), RGB(3, 1, 1) },
};

// Light comes from the top left, so the shadowed rim is measured from a point
// below and right of centre: thin at the top left, deep at the bottom right.
#define RIM_IN 96
#define RIM_OUT 150
#define LX (CX + 16)
#define LY (CY + 20)
// Gloss spot near the top-left edge, an ellipse lying along the rim.
#define HX (CX - 70)
#define HY (CY - 70)
#define HRX 30
#define HRY 15
#define HEXT 31   // its bounding half-extent, px

static const Theme *s_theme = &THEMES[THEME_CLASSIC];
static int32_t s_hc, s_hs;   // gloss rotation, Q12

static uint8_t *s_cache;
static uint16_t s_off[SCREEN_H];
static int16_t s_fa[SCREEN_H], s_fb[SCREEN_H];   // flat span (base colour), inclusive
static int16_t s_cx0[SCREEN_H], s_cx1[SCREEN_H]; // cached range, inclusive

// 0..64: ramp index * 16 plus a dither fraction.
static int shade(int x, int y) {
  int dx = x - LX, dy = y - LY;
  int32_t r2 = dx * dx + dy * dy;
  int v = 32;
  if (r2 > RIM_IN * RIM_IN) {
    int32_t t = (r2 - RIM_IN * RIM_IN) * 32 / (RIM_OUT * RIM_OUT - RIM_IN * RIM_IN);
    v -= t > 32 ? 32 : t;
  }
  int hx = x - HX, hy = y - HY;
  if (hx > -HEXT && hx < HEXT && hy > -HEXT && hy < HEXT) {
    int32_t u = (hx * s_hc + hy * s_hs) >> 12;
    int32_t w = (hy * s_hc - hx * s_hs) >> 12;
    int32_t q = u * u * 256 / (HRX * HRX) + w * w * 256 / (HRY * HRY);
    if (q < 256) v += (256 - q) * 34 / 256;
  }
  return v < 0 ? 0 : v > 64 ? 64 : v;
}

static uint8_t shade_color(int x, int y) {
  int v = shade(x, y);
  int idx = v >> 4;
  if (idx >= 4) return s_theme->ramp[4];
  return s_theme->ramp[idx + ((v & 15) > R_BAYER[y & 3][x & 3])];
}

static int circle_half(int y) {
  int dy2 = (2 * y + 1 - SCREEN_H);   // twice the distance from centre
  return (isqrt32(SCREEN_W * SCREEN_W - dy2 * dy2) + 1) / 2 + 1;
}

void bg_free(void) {
  free(s_cache);
  s_cache = NULL;
}

const Theme *bg_theme(void) { return s_theme; }

bool bg_build(int theme) {
  if (theme < 0 || theme >= THEME_COUNT) theme = THEME_CLASSIC;
  s_theme = &THEMES[theme];
  s_hc = cos_lookup(DEG2TRIG(315)) >> 4;
  s_hs = sin_lookup(DEG2TRIG(315)) >> 4;
  bg_free();
  uint32_t total = 0;
  for (int y = 0; y < SCREEN_H; y++) {
    int h = circle_half(y);
    s_cx0[y] = imax(0, CX - h);
    s_cx1[y] = imin(SCREEN_W - 1, CX + h);
    int dy = y - LY;
    int fa = 1, fb = 0;
    if (dy > -RIM_IN && dy < RIM_IN) {
      int w = isqrt32(RIM_IN * RIM_IN - dy * dy) - 1;
      fa = LX - w;
      fb = LX + w;
      if (y > HY - HEXT && y < HY + HEXT && fa < HX + HEXT) fa = HX + HEXT;
      fa = imax(fa, s_cx0[y]);
      fb = imin(fb, s_cx1[y]);
    }
    if (fa > fb) {
      fa = s_cx1[y] + 1;
      fb = s_cx1[y];
    }
    s_fa[y] = fa;
    s_fb[y] = fb;
    s_off[y] = total;
    total += (fa - s_cx0[y]) + (s_cx1[y] - fb);
  }
  s_cache = malloc(total);
  if (!s_cache) return false;
  for (int y = 0; y < SCREEN_H; y++) {
    uint8_t *p = s_cache + s_off[y];
    for (int x = s_cx0[y]; x < s_fa[y]; x++) *p++ = shade_color(x, y);
    for (int x = s_fb[y] + 1; x <= s_cx1[y]; x++) *p++ = shade_color(x, y);
  }
  return true;
}

uint8_t bg_pixel(int x, int y) {
  if (y < 0 || y >= SCREEN_H) return s_theme->ramp[0];
  if (x >= s_fa[y] && x <= s_fb[y]) return s_theme->ramp[2];
  if (!s_cache || x < s_cx0[y] || x > s_cx1[y]) return shade_color(x, y);
  if (x < s_fa[y]) return s_cache[s_off[y] + (x - s_cx0[y])];
  return s_cache[s_off[y] + (s_fa[y] - s_cx0[y]) + (x - s_fb[y] - 1)];
}

void bg_render(uint8_t **rows, const int16_t *minx, const int16_t *maxx) {
  uint8_t base = s_theme->ramp[2];
  for (int y = 0; y < SCREEN_H; y++) {
    uint8_t *row = rows[y];
    int x0 = minx[y], x1 = maxx[y];
    if (x1 < x0) continue;
    int fa = imax(s_fa[y], x0), fb = imin(s_fb[y], x1);
    if (!s_cache) {
      for (int x = x0; x <= x1; x++) row[x] = (x >= fa && x <= fb) ? base : shade_color(x, y);
      continue;
    }
    for (int x = x0; x < fa && x <= x1; x++) row[x] = bg_pixel(x, y);
    if (fb >= fa) memset(row + fa, base, fb - fa + 1);
    for (int x = imax(fb + 1, x0); x <= x1; x++) row[x] = bg_pixel(x, y);
  }
}
