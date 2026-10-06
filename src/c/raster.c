#include "raster.h"
#include "fixed.h"
#include "bg.h"

const uint8_t R_BAYER[4][4] = {
  {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5},
};

enum { C_IN = 0, C_OUT = 1, C_HALF = 2 };
enum { M_FILL, M_ALPHA, M_DITHER, M_BG };

#define IV_INF (1 << 24)
#define MAX_IV 4

typedef struct { int32_t a, b; } Iv;   // [a, b) in Q4

static uint8_t **s_rows;
static const int16_t *s_minx, *s_maxx;
static uint8_t s_acc[SCREEN_W + 1];     // per-pixel coverage, 0..64

void r_begin(uint8_t **rows, const int16_t *minx, const int16_t *maxx) {
  s_rows = rows;
  s_minx = minx;
  s_maxx = maxx;
}

void r_shape(RShape *s) {
  s->n = 0;
  s->x0 = 0;
  s->y0 = 0;
  s->x1 = SCREEN_W - 1;
  s->y1 = SCREEN_H - 1;
}

void r_clip_box(RShape *s, int x0, int y0, int x1, int y1) {
  if (x0 > s->x0) s->x0 = x0;
  if (y0 > s->y0) s->y0 = y0;
  if (x1 < s->x1) s->x1 = x1;
  if (y1 < s->y1) s->y1 = y1;
}

// A rotated ellipse, solved per row. With u = c*x + s*y and v = c*y - s*x the
// inside test is (u/rx)^2 + (v/ry)^2 <= 1. As a quadratic in x it gives, for a
// row at height y from the centre:
//   centre   = y * c*s*(rx^2 - ry^2) / A
//   halfwidth = rx*ry * sqrt(A - y^2) / A,   A = ry^2*c^2 + rx^2*s^2
// so A is also the vertical half-extent squared. Only an isqrt per row.
static void add_ellipse(RShape *s, uint8_t type, int32_t cx, int32_t cy, int32_t rx, int32_t ry,
                        int32_t angle) {
  if (s->n >= R_MAX_C) return;
  if (rx < 1) rx = 1;
  if (ry < 1) ry = 1;
  RConstraint *c = &s->c[s->n++];
  int64_t co = cos_lookup(angle) >> 4, si = sin_lookup(angle) >> 4;   // Q12
  int64_t rx2 = (int64_t)rx * rx, ry2 = (int64_t)ry * ry;              // Q8
  int64_t a = (ry2 * co * co + rx2 * si * si) >> 24;
  if (a < 1) a = 1;
  c->type = type;
  c->cx = cx;
  c->cy = cy;
  c->a = (int32_t)a;
  c->k = (int32_t)(((int64_t)rx * ry << 12) / a);
  c->m = (int32_t)((co * si * (rx2 - ry2) / a) >> 12);
  if (type == C_IN) {
    int32_t ey = isqrt32((int32_t)a) + Q4;
    int32_t ex = isqrt32((int32_t)((rx2 * co * co + ry2 * si * si) >> 24)) + Q4;
    r_clip_box(s, (cx - ex) >> 4, (cy - ey) >> 4, (cx + ex) >> 4, (cy + ey) >> 4);
  }
}

void r_ellipse(RShape *s, int32_t cx, int32_t cy, int32_t rx, int32_t ry, int32_t angle) {
  add_ellipse(s, C_IN, cx, cy, rx, ry, angle);
}

void r_not_ellipse(RShape *s, int32_t cx, int32_t cy, int32_t rx, int32_t ry, int32_t angle) {
  add_ellipse(s, C_OUT, cx, cy, rx, ry, angle);
}

void r_half(RShape *s, int32_t px, int32_t py, int32_t nx, int32_t ny) {
  if (s->n >= R_MAX_C) return;
  while (nx > 4096 || nx < -4096 || ny > 4096 || ny < -4096) {
    nx /= 2;
    ny /= 2;
  }
  RConstraint *c = &s->c[s->n++];
  c->type = C_HALF;
  c->cx = px;
  c->cy = py;
  c->a = nx;
  c->k = ny;
  c->m = 0;
}

void r_edge(RShape *s, int32_t ax, int32_t ay, int32_t bx, int32_t by) {
  // The normal (-dy, dx) points to the right of a -> b as seen on screen
  // (y down), which is the inside of a clockwise polygon.
  r_half(s, ax, ay, -(by - ay), bx - ax);
}

// The constraint's allowed x-intervals on sub-row y (Q4). Returns the count.
static int c_span(const RConstraint *c, int32_t y, Iv *o) {
  if (c->type == C_HALF) {
    int32_t nx = c->a, ny = c->k;
    int32_t rhs = -ny * (y - c->cy);
    if (nx == 0) {
      if (rhs > 0) return 0;
      o[0] = (Iv){ -IV_INF, IV_INF };
      return 1;
    }
    int32_t xb = c->cx + rhs / nx;
    o[0] = nx > 0 ? (Iv){ xb, IV_INF } : (Iv){ -IV_INF, xb };
    return 1;
  }
  int32_t dy = y - c->cy;
  int32_t rem = c->a - dy * dy;
  if (rem <= 0) {
    if (c->type == C_IN) return 0;
    o[0] = (Iv){ -IV_INF, IV_INF };
    return 1;
  }
  int32_t hw = (int32_t)(((int64_t)c->k * isqrt32(rem)) >> 12);
  int32_t xc = c->cx + (int32_t)(((int64_t)c->m * dy) >> 12);
  if (c->type == C_IN) {
    o[0] = (Iv){ xc - hw, xc + hw };
    return 1;
  }
  o[0] = (Iv){ -IV_INF, xc - hw };
  o[1] = (Iv){ xc + hw, IV_INF };
  return 2;
}

static int iv_and(Iv *out, const Iv *x, int nx, const Iv *y, int ny) {
  int n = 0;
  for (int i = 0; i < nx; i++) {
    for (int j = 0; j < ny; j++) {
      int32_t a = x[i].a > y[j].a ? x[i].a : y[j].a;
      int32_t b = x[i].b < y[j].b ? x[i].b : y[j].b;
      if (a < b && n < MAX_IV) out[n++] = (Iv){ a, b };
    }
  }
  return n;
}

uint8_t r_blend(uint8_t dst, uint8_t src, int a) {
  if (a >= 64) return src;
  if (a <= 0) return dst;
  int ia = 64 - a;
  int r = (((dst >> 4) & 3) * ia + ((src >> 4) & 3) * a + 32) >> 6;
  int g = (((dst >> 2) & 3) * ia + ((src >> 2) & 3) * a + 32) >> 6;
  int b = ((dst & 3) * ia + (src & 3) * a + 32) >> 6;
  return (uint8_t)(0xC0 | (r << 4) | (g << 2) | b);
}

static void fill(const RShape *s, uint8_t src, int mode, int param) {
  if (!s_rows) return;
  int y0 = imax(s->y0, 0), y1 = imin(s->y1, SCREEN_H - 1);
  for (int y = y0; y <= y1; y++) {
    int rx0 = imax(s->x0, s_minx[y]), rx1 = imin(s->x1, s_maxx[y]);
    if (rx0 > rx1) continue;
    int tlo = SCREEN_W, thi = -1;
    for (int sub = 0; sub < 4; sub++) {
      int32_t yq = y * Q4 + 2 + sub * 4;
      Iv cur[MAX_IV], tmp[MAX_IV], cs[2];
      int n = 1;
      cur[0] = (Iv){ rx0 * Q4, (rx1 + 1) * Q4 };
      for (int i = 0; i < s->n && n; i++) {
        int m = c_span(&s->c[i], yq, cs);
        n = iv_and(tmp, cur, n, cs, m);
        for (int k = 0; k < n; k++) cur[k] = tmp[k];
      }
      for (int i = 0; i < n; i++) {
        int32_t a = cur[i].a, b = cur[i].b;
        int pa = a >> 4, pb = (b - 1) >> 4;
        if (pa < tlo) tlo = pa;
        if (pb > thi) thi = pb;
        if (pa == pb) {
          s_acc[pa] += b - a;
        } else {
          s_acc[pa] += Q4 - (a & 15);
          for (int p = pa + 1; p < pb; p++) s_acc[p] += Q4;
          s_acc[pb] += ((b - 1) & 15) + 1;
        }
      }
    }
    uint8_t *row = s_rows[y];
    for (int p = tlo; p <= thi; p++) {
      int cov = s_acc[p];
      if (!cov) continue;
      s_acc[p] = 0;
      switch (mode) {
        case M_FILL:
          row[p] = cov >= 64 ? src : r_blend(row[p], src, cov);
          break;
        case M_ALPHA:
          row[p] = r_blend(row[p], src, cov * param >> 6);
          break;
        case M_DITHER:
          if (R_BAYER[y & 3][p & 3] < param) row[p] = r_blend(row[p], src, cov);
          break;
        case M_BG: {
          uint8_t c = bg_pixel(p, y);
          row[p] = cov >= 64 ? c : r_blend(row[p], c, cov);
          break;
        }
      }
    }
  }
}

void r_fill(const RShape *s, uint8_t argb) { fill(s, argb, M_FILL, 0); }
void r_fill_alpha(const RShape *s, uint8_t argb, int alpha) {
  if (alpha > 0) fill(s, argb, M_ALPHA, alpha > 64 ? 64 : alpha);
}
void r_fill_dither(const RShape *s, uint8_t argb, int level) {
  if (level > 0) fill(s, argb, M_DITHER, level > 16 ? 16 : level);
}
void r_fill_bg(const RShape *s) { fill(s, 0, M_BG, 0); }

void r_plot(int x, int y, uint8_t argb) {
  if (!s_rows || y < 0 || y >= SCREEN_H || x < s_minx[y] || x > s_maxx[y]) return;
  s_rows[y][x] = argb;
}
