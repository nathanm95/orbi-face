#pragma once
#include <pebble.h>

// A small coverage rasteriser that writes straight into the framebuffer.
//
// Every shape is the intersection of up to R_MAX_C convex constraints (inside
// an ellipse, outside an ellipse, one side of a line), which is enough for
// eyes clipped by curved lids, crescent smiles, mouths with teeth and tongue,
// hearts, bolts and so on. Anything else is a union, drawn as several shapes.
//
// Coordinates are Q4 (1/16 px) and every row is sampled at 4 sub-rows with
// exact horizontal coverage, so edges are antialiased and features can move
// by a fraction of a pixel. Without that, slow easing visibly steps.
//
// There is no FPU: everything per row and per pixel is integer.

#define Q4 16
#define F2Q(v) ((int32_t)((v) * 16.0f))   // float px -> Q4, for per-shape setup only

#define SCREEN_W 260
#define SCREEN_H 260
#define CX 130
#define CY 130

// GColor8: 2 bits per channel, so 64 colours. RGB(r, g, b) takes 0..3 levels.
#define RGB(r, g, b) ((uint8_t)(0xC0 | ((r) << 4) | ((g) << 2) | (b)))
#define C_BLACK RGB(0, 0, 0)
#define C_WHITE RGB(3, 3, 3)
#define C_CLEAR 0x00

#define R_MAX_C 5

typedef struct {
  uint8_t type;
  int32_t cx, cy;   // Q4 centre (ellipse) or point on the line (half-plane)
  int32_t a;        // ellipse: vertical half-extent squared (Q8); line: nx (Q12)
  int32_t k;        // ellipse: half-width scale (Q12);            line: ny (Q12)
  int32_t m;        // ellipse: centre shear per row (Q12)
} RConstraint;

typedef struct {
  RConstraint c[R_MAX_C];
  uint8_t n;
  int16_t x0, y0, x1, y1;   // pixel bounding box, inclusive
} RShape;

// Point the rasteriser at this frame's rows: rows[y][x] is valid for
// minx[y] <= x <= maxx[y] (the round display has a different range per row).
void r_begin(uint8_t **rows, const int16_t *minx, const int16_t *maxx);

void r_shape(RShape *s);
// Inside / outside an ellipse with semi-axes rx, ry (Q4), rotated by `angle`
// (Pebble trig units, positive turns +x towards +y, i.e. clockwise on screen).
void r_ellipse(RShape *s, int32_t cx, int32_t cy, int32_t rx, int32_t ry, int32_t angle);
void r_not_ellipse(RShape *s, int32_t cx, int32_t cy, int32_t rx, int32_t ry, int32_t angle);
// Keeps the side of the line through (px, py) that the normal (nx, ny) points
// into. The normal needn't be unit length, but keep it within +-4096.
void r_half(RShape *s, int32_t px, int32_t py, int32_t nx, int32_t ny);
// The half-plane on the left of the directed edge a -> b (screen coords).
void r_edge(RShape *s, int32_t ax, int32_t ay, int32_t bx, int32_t by);
// Restricts the bounding box (pixels). Shapes made only of half-planes need it.
void r_clip_box(RShape *s, int x0, int y0, int x1, int y1);

// Fills with a solid colour, antialiased.
void r_fill(const RShape *s, uint8_t argb);
// Fills at partial opacity, alpha 0..64.
void r_fill_alpha(const RShape *s, uint8_t argb, int alpha);
// Fills an ordered-dither fraction of the pixels, level 0..16. Reads as a
// soft tint without needing colours the display doesn't have.
void r_fill_dither(const RShape *s, uint8_t argb, int level);
// Repaints the face background (see bg.h), e.g. for eyelids.
void r_fill_bg(const RShape *s);

// Single pixel helpers for particles that are smaller than a pixel or two.
void r_plot(int x, int y, uint8_t argb);

uint8_t r_blend(uint8_t dst, uint8_t src, int alpha);
extern const uint8_t R_BAYER[4][4];
