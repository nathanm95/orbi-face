#pragma once
#include <pebble.h>

// The orb itself: a glossy sphere lit from the top left, filling the screen.
// Shading is an ordered dither between neighbouring colours of a 5-step ramp,
// because the display only has 64 colours. It is computed once per theme and
// cached; a frame is then mostly memset and memcpy.

typedef struct {
  uint8_t ramp[5];   // rim shadow .. base .. highlight
  uint8_t cheek;     // blush, the core
  uint8_t cheek_soft;   // blush, the edge: close to the base so it reads soft
  uint8_t mouth;     // inside of an open mouth
  uint8_t tongue;
} Theme;

enum { THEME_CLASSIC, THEME_TANGERINE, THEME_BUBBLEGUM, THEME_MINT, THEME_SKY, THEME_COUNT };

extern const Theme THEMES[THEME_COUNT];

// Builds the cache. Returns false if there wasn't memory for it, in which case
// drawing still works, just more slowly.
bool bg_build(int theme);
void bg_free(void);
const Theme *bg_theme(void);
// Paints the whole orb.
void bg_render(uint8_t **rows, const int16_t *minx, const int16_t *maxx);
// The orb's colour at one pixel, e.g. to paint an eyelid over an eye.
uint8_t bg_pixel(int x, int y);
