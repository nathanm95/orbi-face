// A 260x260 round framebuffer for the host harness, laid out like the
// watch's: each row is only valid between minx and maxx.
#pragma once
#include <pebble.h>
#include "../../src/c/raster.h"

static uint8_t g_fb[SCREEN_H][SCREEN_W];
static uint8_t *g_rows[SCREEN_H];
static int16_t g_minx[SCREEN_H], g_maxx[SCREEN_H];

static void fb_init(void) {
  for (int y = 0; y < SCREEN_H; y++) {
    double dy = y + 0.5 - SCREEN_H / 2.0;
    double h = sqrt(fmax(0, 130.0 * 130.0 - dy * dy));
    g_minx[y] = (int16_t)ceil(130 - h - 0.5);
    g_maxx[y] = (int16_t)floor(130 + h - 0.5);
    if (g_minx[y] < 0) g_minx[y] = 0;
    if (g_maxx[y] > SCREEN_W - 1) g_maxx[y] = SCREEN_W - 1;
    g_rows[y] = g_fb[y];
  }
  memset(g_fb, 0xC0, sizeof(g_fb));
  r_begin(g_rows, g_minx, g_maxx);
}

// Writes a binary PPM with the 2-bit channels expanded to 0/85/170/255.
static void fb_write_ppm(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    perror(path);
    exit(1);
  }
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      uint8_t c = (x >= g_minx[y] && x <= g_maxx[y]) ? g_fb[y][x] : 0xC0;
      uint8_t px[3] = { (uint8_t)(((c >> 4) & 3) * 85), (uint8_t)(((c >> 2) & 3) * 85),
                        (uint8_t)((c & 3) * 85) };
      fwrite(px, 1, 3, f);
    }
  }
  fclose(f);
}
