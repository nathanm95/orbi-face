#include "face.h"
#include "bg.h"
#include "fixed.h"
#include "raster.h"

// Feature layout on the sphere, in px from the centre of the face when it
// looks straight ahead. The sphere is a little bigger than the screen so
// features near the edge foreshorten without vanishing.
#define SPHERE_R 150.0f
#define EYE_U 36.0f
#define EYE_V -28.0f
#define EYE_RX 24.0f
#define EYE_RY 29.0f
#define BROW_GAP 11.0f
#define MOUTH_V 26.0f
#define MOUTH_HW 26.0f
#define CHEEK_U 64.0f
#define CHEEK_V 10.0f
#define HEAD_TOP_V -124.0f
#define OUTLINE 2.8f

#define C_RAIN RGB(1, 2, 3)
#define C_ICE RGB(2, 3, 3)
#define C_DEEP RGB(0, 1, 3)
#define C_RED RGB(3, 0, 0)

static bool s_pill = true;
static float s_roll, s_cr = 1, s_sr = 0;

void face_set_pill(bool on) { s_pill = on; }

void face_neutral(FaceState *f) {
  f->yaw = f->pitch = f->roll = 0;
  f->bob = f->shake_x = f->shake_y = 0;
  f->scale = 1;
  f->gaze_x = f->gaze_y = 0;
  f->cross = 0;
  f->pupil = 1;
  f->eye_scale = 1;
  f->eye_l = f->eye_r = 1;
  f->lid_top_l = f->lid_top_r = 0;
  f->lid_bot_l = f->lid_bot_r = 0;
  f->lid_tilt = 0;
  f->happy = 0;
  f->eye_mode = EYES_NORMAL;
  f->brow_y = f->brow_tilt = f->brow_l = f->brow_r = 0;
  f->brow_arch = 0.3f;
  f->mouth_open = 0;
  f->mouth_w = 1;
  f->mouth_curve = 0.45f;
  f->mouth_round = 0;
  f->mouth_skew = 0;
  f->mouth_x = 0;
  f->teeth = 0;
  f->tongue = 0;
  f->cheek = 0;
  f->flush = 0;
  f->sweat = 0;
  f->flash = 0;
}

// ------------------------------------------------------------- geometry ----

// A feature's anchor on screen, with how much it is foreshortened.
typedef struct { float x, y, sx, sy; } Pt;

static Pt proj(const FaceState *f, float u, float v) {
  u *= f->scale;
  v *= f->scale;
  float t0 = fasin(fclamp(u / SPHERE_R, -0.95f, 0.95f));
  float p0 = fasin(fclamp(v / SPHERE_R, -0.95f, 0.95f));
  float t = fclamp(t0 + f->yaw, -1.3f, 1.3f), p = fclamp(p0 + f->pitch, -1.3f, 1.3f);
  float x = SPHERE_R * fsin(t), y = SPHERE_R * fsin(p);
  float sx = fcos(t) / fcos(t0), sy = fcos(p) / fcos(p0);
  Pt out = { CX + x * s_cr - y * s_sr + f->shake_x, CY + x * s_sr + y * s_cr + f->bob + f->shake_y,
             fclamp(sx, 0.2f, 1.2f) * f->scale, fclamp(sy, 0.2f, 1.2f) * f->scale };
  return out;
}

// Local offsets (lx, ly) are in the feature's own frame: foreshortened, then
// turned with the head's roll. `rot` additionally turns them about the anchor
// (a tilted brow or mouth).
typedef struct {
  Pt c;
  float cr, sr;   // cos/sin of roll + rot
  float rot;
} Frame;

static Frame frame(Pt c, float rot) {
  Frame fr = { c, fcos(s_roll + rot), fsin(s_roll + rot), rot };
  return fr;
}

static void at(const Frame *fr, float lx, float ly, int32_t *X, int32_t *Y) {
  float x = lx * fr->c.sx, y = ly * fr->c.sy;
  *X = F2Q(fr->c.x + x * fr->cr - y * fr->sr);
  *Y = F2Q(fr->c.y + x * fr->sr + y * fr->cr);
}

static void ell(RShape *s, const Frame *fr, float lx, float ly, float rx, float ry, bool inside) {
  int32_t X, Y;
  at(fr, lx, ly, &X, &Y);
  int32_t a = rad2trig(s_roll + fr->rot);
  if (rx < 0.3f) rx = 0.3f;
  if (ry < 0.3f) ry = 0.3f;
  if (inside) {
    r_ellipse(s, X, Y, F2Q(rx * fr->c.sx), F2Q(ry * fr->c.sy), a);
  } else {
    r_not_ellipse(s, X, Y, F2Q(rx * fr->c.sx), F2Q(ry * fr->c.sy), a);
  }
}

// Keeps the side of the line through local (lx, ly) that local normal (nx, ny)
// points into.
static void half(RShape *s, const Frame *fr, float lx, float ly, float nx, float ny) {
  int32_t X, Y;
  at(fr, lx, ly, &X, &Y);
  float wx = nx * fr->cr - ny * fr->sr, wy = nx * fr->sr + ny * fr->cr;
  r_half(s, X, Y, (int32_t)(wx * 4096), (int32_t)(wy * 4096));
}

// A convex polygon from local points, listed clockwise as seen on screen.
static void poly(RShape *s, const Frame *fr, const float *pts, int n, float scale, float grow) {
  int32_t X[6], Y[6];
  float cx = 0, cy = 0;
  for (int i = 0; i < n; i++) {
    cx += pts[2 * i];
    cy += pts[2 * i + 1];
  }
  cx /= n;
  cy /= n;
  int32_t minx = INT32_MAX, miny = INT32_MAX, maxx = INT32_MIN, maxy = INT32_MIN;
  for (int i = 0; i < n; i++) {
    float px = pts[2 * i], py = pts[2 * i + 1];
    // Grow by pushing each point away from the centroid: a cheap outline.
    float dx = px - cx, dy = py - cy;
    float d = fabs_(dx) + fabs_(dy);
    if (d > 0.001f) {
      px += dx / d * grow * 1.3f;
      py += dy / d * grow * 1.3f;
    }
    at(fr, px * scale, py * scale, &X[i], &Y[i]);
    if (X[i] < minx) minx = X[i];
    if (Y[i] < miny) miny = Y[i];
    if (X[i] > maxx) maxx = X[i];
    if (Y[i] > maxy) maxy = Y[i];
  }
  for (int i = 0; i < n; i++) r_edge(s, X[i], Y[i], X[(i + 1) % n], Y[(i + 1) % n]);
  r_clip_box(s, (minx >> 4) - 1, (miny >> 4) - 1, (maxx >> 4) + 1, (maxy >> 4) + 1);
}

// ------------------------------------------------------------- pieces ----

static void heart(const Frame *fr, float lx, float ly, float w, uint8_t col, float grow) {
  RShape s;
  float r = 0.54f * w + grow;
  for (int k = -1; k <= 1; k += 2) {
    r_shape(&s);
    ell(&s, fr, lx + k * 0.46f * w, ly - 0.2f * w, r, r, true);
    r_fill(&s, col);
  }
  float tri[6] = { lx - 0.97f * w - grow, ly + 0.0f * w, lx + 0.97f * w + grow, ly + 0.0f * w,
                   lx, ly + 1.0f * w + grow * 1.5f };
  r_shape(&s);
  poly(&s, fr, tri, 3, 1, 0);
  r_fill(&s, col);
}

// Teardrop with the point up, centred on the round part.
static void drop(const Frame *fr, float lx, float ly, float r, uint8_t fill, uint8_t line) {
  RShape s;
  for (int pass = 0; pass < 2; pass++) {
    float g = pass ? 0 : 1.3f;
    uint8_t col = pass ? fill : line;
    r_shape(&s);
    ell(&s, fr, lx, ly, r + g, r + g, true);
    r_fill(&s, col);
    float tri[6] = { lx - r * 0.93f - g, ly - r * 0.25f, lx, ly - r * 2.1f - g * 1.6f,
                     lx + r * 0.93f + g, ly - r * 0.25f };
    r_shape(&s);
    poly(&s, fr, tri, 3, 1, 0);
    r_fill(&s, col);
  }
  r_shape(&s);
  ell(&s, fr, lx - r * 0.35f, ly - r * 0.1f, r * 0.28f, r * 0.34f, true);
  r_fill(&s, C_WHITE);
}

static void sparkle(const Frame *fr, float lx, float ly, float len, uint8_t col, uint8_t line) {
  RShape s;
  for (int pass = 0; pass < 2; pass++) {
    float g = pass ? 0 : 1.1f;
    for (int k = 0; k < 2; k++) {
      Frame f2 = *fr;
      f2.rot += k ? 1.5708f : 0;
      f2.cr = fcos(s_roll + f2.rot);
      f2.sr = fsin(s_roll + f2.rot);
      // Rotate the anchor offset back so both bars share the centre.
      float ox = lx * fcos(-f2.rot + fr->rot) - ly * fsin(-f2.rot + fr->rot);
      float oy = lx * fsin(-f2.rot + fr->rot) + ly * fcos(-f2.rot + fr->rot);
      r_shape(&s);
      ell(&s, &f2, ox, oy, len + g, len * 0.2f + g, true);
      r_fill(&s, pass ? col : line);
    }
  }
}

static void zglyph(const Frame *fr, float lx, float ly, float sz, uint8_t col) {
  float top[8] = { -1, -1, 1, -1, 1, -0.6f, -1, -0.6f };
  float bot[8] = { -1, 0.6f, 1, 0.6f, 1, 1, -1, 1 };
  float dia[8] = { 0.42f, -0.6f, 1, -0.6f, -0.42f, 0.6f, -1, 0.6f };
  const float *parts[3] = { top, dia, bot };
  for (int i = 0; i < 3; i++) {
    float p[8];
    for (int k = 0; k < 4; k++) {
      p[2 * k] = lx + parts[i][2 * k] * sz;
      p[2 * k + 1] = ly + parts[i][2 * k + 1] * sz;
    }
    RShape s;
    r_shape(&s);
    poly(&s, fr, p, 4, 1, 0);
    r_fill(&s, col);
  }
}

static void note(const Frame *fr, float lx, float ly, float sz, uint8_t col) {
  RShape s;
  Frame h = *fr;
  h.rot -= 0.45f;
  h.cr = fcos(s_roll + h.rot);
  h.sr = fsin(s_roll + h.rot);
  float hx = lx * fcos(0.45f) - ly * fsin(0.45f), hy = lx * fsin(0.45f) + ly * fcos(0.45f);
  r_shape(&s);
  ell(&s, &h, hx, hy, 4.3f * sz, 3.1f * sz, true);
  r_fill(&s, col);
  float stem[8] = { lx + 2.6f * sz, ly - 13 * sz, lx + 4.2f * sz, ly - 13 * sz, lx + 4.2f * sz, ly - 0.5f * sz,
                    lx + 2.6f * sz, ly - 0.5f * sz };
  r_shape(&s);
  poly(&s, fr, stem, 4, 1, 0);
  r_fill(&s, col);
  float flag[8] = { lx + 2.6f * sz, ly - 13 * sz, lx + 8.5f * sz, ly - 9 * sz, lx + 8.5f * sz, ly - 6.5f * sz,
                    lx + 2.6f * sz, ly - 10 * sz };
  r_shape(&s);
  poly(&s, fr, flag, 4, 1, 0);
  r_fill(&s, col);
}

static void bolt(const Frame *fr, float lx, float ly, float sz) {
  static const float up[8] = { -0.1f, -1, 0.5f, -1, 0.15f, -0.08f, -0.42f, -0.08f };
  static const float lo[6] = { -0.18f, -0.2f, 0.58f, -0.2f, -0.32f, 1.15f };
  for (int pass = 0; pass < 2; pass++) {
    float g = pass ? 0 : 2.2f;
    uint8_t col = pass ? RGB(3, 3, 0) : C_BLACK;
    float p[8];
    for (int k = 0; k < 4; k++) {
      p[2 * k] = lx + up[2 * k] * sz;
      p[2 * k + 1] = ly + up[2 * k + 1] * sz;
    }
    RShape s;
    r_shape(&s);
    poly(&s, fr, p, 4, 1, g);
    r_fill(&s, col);
    for (int k = 0; k < 3; k++) {
      p[2 * k] = lx + lo[2 * k] * sz;
      p[2 * k + 1] = ly + lo[2 * k + 1] * sz;
    }
    r_shape(&s);
    poly(&s, fr, p, 3, 1, g);
    r_fill(&s, col);
  }
}

// ---------------------------------------------------------------- eyes ----

static void closed_eye(const Frame *fr, float rx, float ry, float yc, float happy, float t) {
  RShape s;
  r_shape(&s);
  float h = happy * 0.42f * ry;
  if (h > t * 0.9f) {
    // ^ ^ : the top half of an elliptical ring.
    float cy = yc + h * 0.5f;
    ell(&s, fr, 0, cy, rx * 0.98f, h + t * 0.5f, true);
    ell(&s, fr, 0, cy, rx * 0.98f, h - t * 0.5f, false);
    half(&s, fr, 0, cy, 0, -1);
  } else if (h < -t * 0.9f) {
    // Peaceful, sleeping: the bottom half, curving down.
    float cy = yc + h * 0.5f;
    ell(&s, fr, 0, cy, rx * 0.98f, -h + t * 0.5f, true);
    ell(&s, fr, 0, cy, rx * 0.98f, -h - t * 0.5f, false);
    half(&s, fr, 0, cy, 0, 1);
  } else {
    ell(&s, fr, 0, yc + h * 0.5f, rx * 1.0f, t * 0.6f + h * 0.4f, true);
  }
  r_fill(&s, C_BLACK);
}

static void draw_eye(const FaceState *f, int side) {
  float es = f->eye_scale * (side < 0 ? f->eye_l : f->eye_r);
  Pt c = proj(f, side * EYE_U, EYE_V);
  float rx = EYE_RX * es, ry = EYE_RY * es;
  float inner = (float)-side;   // +1 when the inner corner is towards +x
  float t = OUTLINE * (0.6f + 0.4f * es);
  RShape s;

  if (f->eye_mode == EYES_HEART) {
    Frame fr = frame(c, 0);
    float w = rx * (1.02f + 0.1f * fsin(f->eye_phase * 2));
    heart(&fr, 0, -w * 0.15f, w, C_BLACK, t);
    heart(&fr, 0, -w * 0.15f, w, C_RED, 0);
    r_shape(&s);
    ell(&s, &fr, -w * 0.48f, -w * 0.42f, w * 0.16f, w * 0.12f, true);
    r_fill(&s, C_WHITE);
    return;
  }
  if (f->eye_mode == EYES_X) {
    for (int k = -1; k <= 1; k += 2) {
      Frame fr = frame(c, k * 0.785f);
      r_shape(&s);
      ell(&s, &fr, 0, 0, rx * 0.8f, t * 1.2f, true);
      r_fill(&s, C_BLACK);
    }
    return;
  }

  float lt = fclamp(side < 0 ? f->lid_top_l : f->lid_top_r, 0, 1);
  float lb = fclamp(side < 0 ? f->lid_bot_l : f->lid_bot_r, 0, 1);
  // The bottom lid rises towards a point just below centre; the top lid
  // closes onto wherever the bottom lid is, so a lone top lid can shut the eye.
  float yc = 0.18f * ry;
  float yb = flerp(ry + 6, yc, lb);
  float yt = flerp(-ry - 6, yb, lt);
  float tilt = fclamp(f->lid_tilt, -1.2f, 1.2f) * 0.5f * inner;
  Frame fr = frame(c, 0);
  // A smiling bottom lid is curved, so while closing it leaves the eye's
  // corners showing as two white points. Switch to the ^ arc early instead.
  bool squeezed = f->happy > 0.5f && lt > 0.5f && lb > 0.5f;
  if (yt >= yb - 0.8f || squeezed) {
    Frame fc = frame(c, tilt * 0.6f);
    closed_eye(&fc, rx, ry, yb, f->happy, t);
    return;
  }

  // Lids are big circles: the top one hangs above the eye and tilts, the
  // bottom one rises from below and tightens into a curve when smiling.
  float rlt = 2.6f * rx, rlb = flerp(2.6f * rx, 0.8f * rx, fclamp(f->happy, 0, 1));
  float tcx = rlt * fsin(tilt), tcy = yt - rlt * fcos(tilt);
  float bcx = 0, bcy = yb + rlb;

  r_shape(&s);
  ell(&s, &fr, 0, 0, rx + t, ry + t, true);
  ell(&s, &fr, tcx, tcy, rlt - t, rlt - t, false);
  ell(&s, &fr, bcx, bcy, rlb - t, rlb - t, false);
  r_fill(&s, C_BLACK);

#define WHITE_CLIP(sh)                                \
  do {                                                \
    ell(sh, &fr, 0, 0, rx, ry, true);                 \
    ell(sh, &fr, tcx, tcy, rlt, rlt, false);          \
    ell(sh, &fr, bcx, bcy, rlb, rlb, false);          \
  } while (0)

  r_shape(&s);
  WHITE_CLIP(&s);
  r_fill(&s, C_WHITE);

  // A soft shadow under the top lid gives the eye some depth.
  r_shape(&s);
  WHITE_CLIP(&s);
  ell(&s, &fr, tcx, tcy, rlt + 5.5f, rlt + 5.5f, true);
  r_fill_dither(&s, RGB(2, 2, 2), 7);

  float pr = 0.56f * (rx < ry ? rx : ry) * f->pupil;
  float gx = f->gaze_x - side * f->cross * 0.8f, gy = f->gaze_y;
  if (f->eye_mode == EYES_SPIN) {
    gx = 0.6f * fcos(f->eye_phase * side);
    gy = 0.6f * fsin(f->eye_phase * side);
  }
  float px = fclamp(gx, -1.2f, 1.2f) * (rx - pr * 0.72f);
  float py = fclamp(gy, -1.2f, 1.2f) * (ry - pr * 0.72f);

  r_shape(&s);
  WHITE_CLIP(&s);
  ell(&s, &fr, px, py, pr, pr, true);
  r_fill(&s, C_BLACK);

  r_shape(&s);
  WHITE_CLIP(&s);
  ell(&s, &fr, px + pr * 0.36f, py - pr * 0.38f, pr * 0.3f, pr * 0.3f, true);
  r_fill(&s, C_WHITE);
  r_shape(&s);
  WHITE_CLIP(&s);
  ell(&s, &fr, px - pr * 0.34f, py + pr * 0.36f, pr * 0.13f, pr * 0.13f, true);
  r_fill(&s, C_WHITE);
#undef WHITE_CLIP
}

// --------------------------------------------------------------- brows ----

static void draw_brow(const FaceState *f, int side) {
  float inner = (float)-side;
  float es = f->eye_scale * (side < 0 ? f->eye_l : f->eye_r);
  float raise = f->brow_y + (side < 0 ? f->brow_l : f->brow_r);
  Pt c = proj(f, side * (EYE_U + 1), EYE_V - EYE_RY * es - BROW_GAP - raise);
  float ang = fclamp(f->brow_tilt, -1.3f, 1.3f) * 0.42f * inner;
  Frame fr = frame(c, ang);
  float a = 2 + fclamp(f->brow_arch, -0.5f, 1.5f) * 6, th = 4.8f, bw = 17;
  RShape s;
  r_shape(&s);
  ell(&s, &fr, 0, a, bw, a + th * 0.5f, true);
  if (a - th * 0.5f > 0.4f) ell(&s, &fr, 0, a, bw, a - th * 0.5f, false);
  half(&s, &fr, 0, a, 0, -1);
  r_fill(&s, C_BLACK);
}

// --------------------------------------------------------------- mouth ----

static void draw_mouth(const FaceState *f) {
  const Theme *th = bg_theme();
  Pt c = proj(f, f->mouth_x, MOUTH_V);
  Frame fr = frame(c, fclamp(f->mouth_skew, -1.2f, 1.2f) * 0.32f);
  float w = MOUTH_HW * f->mouth_w;
  float o = fclamp(f->mouth_open, 0, 1.2f);
  float cv = fclamp(f->mouth_curve, -1.2f, 1.2f);
  float t = 3.0f;
  RShape s;

  if (o < 0.06f) {
    // A tapered crescent: half of a ring whose inner edge only differs in
    // height, so the line thins out towards the corners like a brush stroke.
    float d = cv * 13.0f;
    float ad = d < 0 ? -d : d;
    float cy = d >= 0 ? -1.5f : 3.0f;
    r_shape(&s);
    ell(&s, &fr, 0, cy, w, ad + t, true);
    if (ad > 0.6f) ell(&s, &fr, 0, cy, w, ad, false);
    half(&s, &fr, 0, cy, 0, d >= 0 ? 1 : -1);
    r_fill(&s, C_BLACK);
    return;
  }

  float h = flerp(7.0f, 27.0f, fclamp(o, 0, 1));
  float rd = fclamp(f->mouth_round, 0, 1);
  float erx = flerp(w, h * 0.62f > 7 ? h * 0.62f : 7, rd);
  float ery = flerp(h, h * 0.55f, rd);
  float ecy = flerp(0, h * 0.5f, rd) - 2.0f;
  // The upper lip is the bottom of a circle overhead: a smaller circle makes
  // the corners turn up more.
  float rup = flerp(flerp(150.0f, 36.0f, fclamp(cv, 0, 1)), 320.0f, rd);
  float lip = flerp(-1.0f, -h - 16.0f, rd);
  float ucy = lip - rup;

  r_shape(&s);
  ell(&s, &fr, 0, ecy, erx + t, ery + t, true);
  ell(&s, &fr, 0, ucy, rup - t, rup - t, false);
  r_fill(&s, C_BLACK);

  r_shape(&s);
  ell(&s, &fr, 0, ecy, erx, ery, true);
  ell(&s, &fr, 0, ucy, rup, rup, false);
  r_fill(&s, th->mouth);

  if (f->teeth > 0.05f) {
    r_shape(&s);
    ell(&s, &fr, 0, ecy, erx, ery, true);
    ell(&s, &fr, 0, ucy, rup, rup, false);
    ell(&s, &fr, 0, ucy, rup + 5.0f * f->teeth, rup + 5.0f * f->teeth, true);
    r_fill(&s, C_WHITE);
  }
  if (o > 0.25f && f->tongue < 0.05f) {
    r_shape(&s);
    ell(&s, &fr, 0, ecy, erx, ery, true);
    ell(&s, &fr, 0, ucy, rup, rup, false);
    ell(&s, &fr, erx * 0.1f, ecy + ery * 0.72f, erx * 0.56f, ery * 0.5f, true);
    r_fill(&s, th->tongue);
  }
  if (f->tongue >= 0.05f) {
    // Sticking out over the lower lip.
    float tg = fclamp(f->tongue, 0, 1);
    float tx = w * 0.2f, ty = ecy + ery * 0.55f + 6 * tg;
    float trx = 9.5f, try_ = 6 + 10 * tg;
    r_shape(&s);
    ell(&s, &fr, tx, ty, trx + t, try_ + t, true);
    ell(&s, &fr, 0, ucy, rup - t, rup - t, false);
    r_fill(&s, C_BLACK);
    r_shape(&s);
    ell(&s, &fr, tx, ty, trx, try_, true);
    ell(&s, &fr, 0, ucy, rup, rup, false);
    r_fill(&s, th->tongue);
    r_shape(&s);
    ell(&s, &fr, tx, ty + try_ * 0.15f, 1.0f, try_ * 0.55f, true);
    ell(&s, &fr, 0, ucy, rup + 2, rup + 2, false);
    r_fill(&s, RGB(2, 0, 0));
  }
}

// ------------------------------------------------------------ the face ----

static void draw_pill(void) {
  RShape s;
  int r = PILL_H / 2;
  r_shape(&s);
  r_clip_box(&s, PILL_X + r, PILL_Y, PILL_X + PILL_W - r - 1, PILL_Y + PILL_H - 1);
  r_fill(&s, C_BLACK);
  for (int k = 0; k < 2; k++) {
    int cx = k ? PILL_X + PILL_W - r : PILL_X + r;
    r_shape(&s);
    r_ellipse(&s, cx * Q4, (PILL_Y + r) * Q4, r * Q4, r * Q4, 0);
    r_fill(&s, C_BLACK);
  }
}

void face_render_back(const FaceState *f, uint8_t **rows, const int16_t *minx,
                      const int16_t *maxx) {
  s_roll = f->roll;
  s_cr = fcos(s_roll);
  s_sr = fsin(s_roll);
  r_begin(rows, minx, maxx);
  bg_render(rows, minx, maxx);
  const Theme *th = bg_theme();
  RShape s;

  if (f->flush > 0.02f) {
    // Red rising from the chin, densest at the bottom, with a gently curved
    // top edge so it reads as the face colouring rather than a block.
    float top = SCREEN_H - fclamp(f->flush, 0, 1.2f) * 175;
    static const int8_t LEVELS[4] = { 2, 4, 6, 8 };
    for (int band = 0; band < 4; band++) {
      float y = top + band * 16;
      r_shape(&s);
      r_ellipse(&s, F2Q(CX), F2Q(y + 260), F2Q(300), F2Q(260), 0);
      r_clip_box(&s, 0, (int)y - 1, SCREEN_W - 1, SCREEN_H - 1);
      r_fill_dither(&s, C_RED, LEVELS[band]);   // Bayer levels nest, so bands accumulate
    }
  }
  if (f->cheek > 0.03f) {
    for (int side = -1; side <= 1; side += 2) {
      Frame fr = frame(proj(f, side * CHEEK_U, CHEEK_V), 0);
      float c = fclamp(f->cheek, 0, 1);
      r_shape(&s);
      ell(&s, &fr, 0, 0, 16, 9, true);
      r_fill_dither(&s, th->cheek_soft, (int)(c * 12 + 0.5f));
      r_shape(&s);
      ell(&s, &fr, 0, 0, 10, 5.5f, true);
      r_fill_dither(&s, th->cheek, (int)(c * 9 + 0.5f));
    }
  }
  for (int side = -1; side <= 1; side += 2) draw_brow(f, side);
  for (int side = -1; side <= 1; side += 2) draw_eye(f, side);
  draw_mouth(f);
  if (f->sweat > 0.05f) {
    Frame fr = frame(proj(f, 80, -62 + 10 * f->sweat), 0);
    drop(&fr, 0, 0, 5.5f * fclamp(f->sweat, 0, 1.2f), C_ICE, C_DEEP);
  }
  if (s_pill) draw_pill();
}

static void draw_particle(const Particle *p) {
  Frame fr = frame((Pt){ p->x, p->y, 1, 1 }, p->angle);
  fr.cr = fcos(p->angle);
  fr.sr = fsin(p->angle);
  RShape s;
  float k = p->life > 0 ? fclamp(p->age / p->life, 0, 1) : 0;
  switch (p->kind) {
    case P_RAIN: {
      Frame fd = fr;
      fd.rot = fatan2(p->vy, p->vx);
      fd.cr = fcos(fd.rot);
      fd.sr = fsin(fd.rot);
      r_shape(&s);
      ell(&s, &fd, 0, 0, 5.5f * p->size, 1.15f, true);
      r_fill(&s, p->color ? p->color : C_RAIN);
      break;
    }
    case P_SPLASH:
    case P_SPIT:
      r_shape(&s);
      ell(&s, &fr, 0, 0, 1.4f * p->size, 1.4f * p->size, true);
      r_fill(&s, C_RAIN);
      break;
    case P_SNOW:
      r_shape(&s);
      ell(&s, &fr, 0, 0, 2.2f * p->size + 1, 2.2f * p->size + 1, true);
      r_fill(&s, RGB(2, 2, 3));
      r_shape(&s);
      ell(&s, &fr, 0, 0, 2.2f * p->size, 2.2f * p->size, true);
      r_fill(&s, C_WHITE);
      break;
    case P_HEART: {
      float w = 6.5f * p->size * (k < 0.15f ? k / 0.15f : 1);
      heart(&fr, 0, 0, w, RGB(2, 0, 0), 1.2f);
      heart(&fr, 0, 0, w, p->color ? p->color : C_RED, 0);
      break;
    }
    case P_SPARKLE:
    case P_STAR: {
      float pulse = fsin(k * 3.1416f);
      sparkle(&fr, 0, 0, 7.5f * p->size * pulse + 1, C_WHITE, RGB(3, 2, 0));
      break;
    }
    case P_CONFETTI: {
      r_shape(&s);
      ell(&s, &fr, 0, 0, 3.6f * p->size, 0.4f + 1.6f * p->size * fabs_(fcos(p->spin * p->age)), true);
      r_fill(&s, p->color);
      break;
    }
    case P_ZZZ:
      zglyph(&fr, 0, 0, 6.5f * p->size * (0.7f + 0.6f * k), RGB(0, 0, 2));
      break;
    case P_TEAR:
    case P_SWEAT:
      drop(&fr, 0, 0, 3.6f * p->size, C_ICE, C_DEEP);
      break;
    case P_STEAM: {
      // A three-lobed puff: solid while fresh, thinning out as it drifts.
      float r = (6 + 10 * k) * p->size;
      int level = k < 0.5f ? 16 : (int)(16 * (1 - (k - 0.5f) * 2) + 2);
      for (int pass = 0; pass < 2; pass++) {
        for (int k2 = 0; k2 < 3; k2++) {
          float g = pass ? 0 : 1.2f;
          r_shape(&s);
          ell(&s, &fr, (k2 - 1) * r * 0.55f, (k2 == 1 ? -0.35f : 0.1f) * r, r * 0.6f + g, r * 0.55f + g,
              true);
          r_fill_dither(&s, pass ? C_WHITE : RGB(2, 2, 2), pass ? level : level - 2);
        }
      }
      break;
    }
    case P_NOTE:
      note(&fr, 0, 0, 1.5f * p->size, C_BLACK);
      break;
    case P_LEAF:
      r_shape(&s);
      ell(&s, &fr, 0, 0, 7 * p->size + 1.2f, 3 * p->size + 1.2f, true);
      r_fill(&s, RGB(0, 1, 0));
      r_shape(&s);
      ell(&s, &fr, 0, 0, 7 * p->size, 3 * p->size, true);
      r_fill(&s, RGB(1, 2, 0));
      break;
    case P_FOG: {
      r_shape(&s);
      ell(&s, &fr, 0, 0, 90 * p->size, 15 * p->size, true);
      r_fill_dither(&s, RGB(2, 2, 2), (int)(10 * fsin(k * 3.1416f)));
      break;
    }
    case P_BOLT:
      bolt(&fr, 0, 0, 22 * p->size);
      break;
  }
}

void face_render_front(const FaceState *f, uint8_t **rows, const int16_t *minx,
                       const int16_t *maxx) {
  r_begin(rows, minx, maxx);
  s_roll = 0;   // particles live in screen space, not on the face
  s_cr = 1;
  s_sr = 0;
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (f->particles[i].kind != P_NONE) draw_particle(&f->particles[i]);
  }
  if (f->flash > 0.02f) {
    RShape s;
    r_shape(&s);
    r_fill_alpha(&s, C_WHITE, (int)(fclamp(f->flash, 0, 1) * 56));
  }
}

// --------------------------------------------------------------- props ----

bool face_prop_pos(const FaceState *f, int slot, int w, int h, int *x, int *y) {
  const PropState *p = &f->props[slot];
  if (p->id == PROP_NONE || p->show <= 0.01f) return false;
  float hide = 1 - fclamp(p->show, 0, 1);
  Pt c;
  float bx, by;
  switch (slot) {
    case SLOT_EYES:
      // Centred between the eyes, dropping in from above.
      c = proj(f, 0, EYE_V + 2);
      bx = c.x - w * 0.5f;
      by = c.y - h * 0.5f - hide * 170;
      break;
    case SLOT_HEAD:
      // Sitting on top of the head, its bottom edge just over the brows.
      c = proj(f, 0, HEAD_TOP_V + (p->id == PROP_PARTYHAT ? 6 : 0));
      bx = c.x - w * 0.5f;
      by = c.y + (p->id == PROP_PARTYHAT ? 52 : 60) - h - hide * 130;
      break;
    default:
      // Overhead: the umbrella or a passing cloud.
      bx = CX - w * 0.5f + (p->id == PROP_CLOUD ? 0 : f->yaw * 40);
      by = (p->id == PROP_UMBRELLA ? 50 : 58) - h - hide * 110;
      break;
  }
  *x = (int)(bx + p->dx + (bx + p->dx < 0 ? -0.5f : 0.5f));
  *y = (int)(by + p->dy + (by + p->dy < 0 ? -0.5f : 0.5f));
  return true;
}
