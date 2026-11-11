#include "platinum.h"
#include <FL/Fl.H>
#include <FL/Fl_RGB_Image.H>
#include <cstdio>
using namespace plat;
int main(int argc, char **argv) {
  init(false);
  Appearance a = current_appearance();
  a.wallpaper.clear();
  for (int i = 0; PATTERNS[i]; i++) {
    a.pattern = PATTERNS[i]; a.background = "pattern";
    Fl_RGB_Image *im = render_desktop(a, 64, 64);
    char f[256]; snprintf(f, sizeof f, "%s/%02d_%s.ppm", argv[1], i, PATTERNS[i]);
    FILE *o = fopen(f, "wb"); fprintf(o, "P6 64 64 255\n"); fwrite(im->data()[0], 1, 64 * 64 * 3, o); fclose(o);
  }
  const char *g[] = {"vertical", "horizontal", "diagonal", "radial"};
  for (int i = 0; i < 4; i++) {
    a.background = "gradient"; a.gradient = g[i];
    Fl_RGB_Image *im = render_desktop(a, 320, 200);
    char f[256]; snprintf(f, sizeof f, "%s/g_%s.ppm", argv[1], g[i]);
    FILE *o = fopen(f, "wb"); fprintf(o, "P6 320 200 255\n"); fwrite(im->data()[0], 1, 320 * 200 * 3, o); fclose(o);
  }
}
