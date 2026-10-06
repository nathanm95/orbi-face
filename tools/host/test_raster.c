// Renders a few primitives over the background, to check the rasteriser.
#include "fb.h"
#include "../../src/c/bg.h"
#include "../../src/c/fixed.h"

int main(int argc, char **argv) {
  fb_init();
  bg_build(argc > 2 ? atoi(argv[2]) : 0);
  bg_render(g_rows, g_minx, g_maxx);
  RShape s;
  // A lidded eye: outline, white, pupil, all clipped by a tilted lid circle.
  int32_t ex = F2Q(94), ey = F2Q(102), rx = F2Q(24), ry = F2Q(29), t = F2Q(3);
  int32_t lx = ex + F2Q(20), ly = ey - F2Q(60);   // lid circle centre, tilted
  int32_t lr = F2Q(66);
  r_shape(&s);
  r_ellipse(&s, ex, ey, rx + t, ry + t, 0);
  r_not_ellipse(&s, lx, ly, lr - t, lr - t, 0);
  r_fill(&s, C_BLACK);
  r_shape(&s);
  r_ellipse(&s, ex, ey, rx, ry, 0);
  r_not_ellipse(&s, lx, ly, lr, lr, 0);
  r_fill(&s, C_WHITE);
  r_shape(&s);
  r_ellipse(&s, ex + F2Q(3), ey + F2Q(4), F2Q(13), F2Q(13), 0);
  r_ellipse(&s, ex, ey, rx, ry, 0);
  r_not_ellipse(&s, lx, ly, lr, lr, 0);
  r_fill(&s, C_BLACK);
  // A rotated brow and a crescent smile.
  r_shape(&s);
  r_ellipse(&s, F2Q(166), F2Q(64), F2Q(16), F2Q(4.5f), DEG2TRIG(345));
  r_fill(&s, C_BLACK);
  r_shape(&s);
  r_ellipse(&s, F2Q(130), F2Q(158), F2Q(30), F2Q(16), 0);
  r_not_ellipse(&s, F2Q(130), F2Q(158), F2Q(30), F2Q(12), 0);
  r_half(&s, F2Q(130), F2Q(158), 0, 4096);
  r_fill(&s, C_BLACK);
  // A triangle from edges, clockwise on screen.
  r_shape(&s);
  r_edge(&s, F2Q(150), F2Q(200), F2Q(190), F2Q(230));
  r_edge(&s, F2Q(190), F2Q(230), F2Q(140), F2Q(240));
  r_edge(&s, F2Q(140), F2Q(240), F2Q(150), F2Q(200));
  r_clip_box(&s, 140, 200, 190, 240);
  r_fill(&s, RGB(3, 0, 0));
  fb_write_ppm(argc > 1 ? argv[1] : "/tmp/raster.ppm");
  return 0;
}
