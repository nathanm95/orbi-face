// Runs Orbi's animation and renderer on the Mac and writes frames, so faces,
// acts and timing can be checked without the emulator.
//
//   harness <out_dir> <act> <seconds> [stride] [theme] [weather] [temp_c]
//
// <act> is an ActId number (0 = just idle life). Every <stride>th frame at
// 30fps is written as out_dir/fNNNN.ppm. Props come from build/host_props/,
// made by tools/make_props.py, and are blitted between the two render passes
// exactly as main.c does on the watch.
#include <time.h>
#include "fb.h"
#include "../../src/c/anim.h"
#include "../../src/c/bg.h"
#include "../../src/c/face.h"

typedef struct {
  int w, h;
  uint8_t *rgba;
} Sprite;

static Sprite s_sprites[PROP_COUNT];

static void load_sprites(const char *dir) {
  for (int i = 1; i < PROP_COUNT; i++) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%d.rgba", dir, i);
    FILE *f = fopen(path, "rb");
    if (!f) continue;
    uint16_t wh[2];
    if (fread(wh, 2, 2, f) == 2) {
      s_sprites[i].w = wh[0];
      s_sprites[i].h = wh[1];
      s_sprites[i].rgba = malloc(wh[0] * wh[1] * 4);
      if (fread(s_sprites[i].rgba, 4, wh[0] * wh[1], f) != (size_t)(wh[0] * wh[1])) {
        s_sprites[i].w = 0;
      }
    }
    fclose(f);
  }
}

static void draw_props(const FaceState *f) {
  for (int slot = 0; slot < SLOT_COUNT; slot++) {
    const Sprite *sp = &s_sprites[f->props[slot].id];
    int x0, y0;
    if (!sp->w || !face_prop_pos(f, slot, sp->w, sp->h, &x0, &y0)) continue;
    for (int y = 0; y < sp->h; y++) {
      int sy = y0 + y;
      if (sy < 0 || sy >= SCREEN_H) continue;
      for (int x = 0; x < sp->w; x++) {
        int sx = x0 + x;
        if (sx < g_minx[sy] || sx > g_maxx[sy]) continue;
        const uint8_t *p = &sp->rgba[(y * sp->w + x) * 4];
        if (p[3] < 128) continue;
        g_fb[sy][sx] = RGB(p[0] / 85, p[1] / 85, p[2] / 85);
      }
    }
  }
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s out_dir act seconds [stride] [theme] [weather] [temp]\n", argv[0]);
    return 2;
  }
  const char *out = argv[1];
  int act = atoi(argv[2]);
  float seconds = atof(argv[3]);
  int stride = argc > 4 ? atoi(argv[4]) : 1;
  int theme = argc > 5 ? atoi(argv[5]) : 0;
  Weather wx = { argc > 6 ? atoi(argv[6]) : WX_UNKNOWN, argc > 7 ? atoi(argv[7]) : 12, true, 10 };

  srand(7);
  fb_init();
  bg_build(theme);
  if (getenv("ORBI_NOPILL")) face_set_pill(false);   // icons: just Orbi
  load_sprites("build/host_props");
  anim_init();
  anim_set_weather(&wx, wx.kind != WX_UNKNOWN);
  anim_set_ambient(true);
  if (act > 0) anim_play((ActId)act);

  int frames = (int)(seconds * 30 + 0.5f);
  if (frames < 1) frames = 1;
  clock_t render = 0;
  int written = 0;
  for (int i = 0; i < frames; i++) {
    anim_step(1.0f / 30);
    if (i % stride) continue;
    clock_t c0 = clock();
    const FaceState *f = anim_face();
    face_render_back(f, g_rows, g_minx, g_maxx);
    draw_props(f);
    face_render_front(f, g_rows, g_minx, g_maxx);
    render += clock() - c0;
    char path[512];
    snprintf(path, sizeof(path), "%s/f%04d.ppm", out, written++);
    fb_write_ppm(path);
  }
  fprintf(stderr, "%d frames, %.2f ms/frame on this Mac, busy at end: %d\n", written,
          written ? 1000.0 * render / CLOCKS_PER_SEC / written : 0, anim_busy());
  return 0;
}
