#include "platinum.h"
#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <cstdio>
using namespace plat;
int main(int, char **argv) {
  init(false);
  const int N = 16, CW = 44;
  Fl_Image_Surface surf(N * CW + 8, 3 * 44 + 8);
  Fl_Surface_Device::push_current(&surf);
  fl_color(fl_rgb_color(0x5E, 0x6F, 0xA8)); fl_rectf(0, 0, N * CW + 8, 3 * 44 + 8);
  for (int i = 0; i < N; i++) {
    draw_icon_workbench((Icon)i, 8 + i * CW, 6, false);
    draw_icon_workbench((Icon)i, 8 + i * CW, 50, true);
  }
  Appearance a = current_appearance(); a.icons = "platinum";
  for (int i = 0; i < N; i++) {   // dla porównania: Platinum (draw_icon w stylu platinum)
    apply(current_scheme(), a);
    draw_icon((Icon)i, 8 + i * CW, 94, false);
  }
  Fl_Surface_Device::pop_current();
  Fl_RGB_Image *im = surf.image();
  FILE *o = fopen(argv[1], "wb");
  fprintf(o, "P6 %d %d 255\n", im->w(), im->h());
  int ld = im->ld() ? im->ld() : im->w() * im->d();
  for (int y = 0; y < im->h(); y++)
    for (int x = 0; x < im->w(); x++) fwrite((const char *)im->data()[0] + y * ld + x * im->d(), 1, 3, o);
  fclose(o);
}
