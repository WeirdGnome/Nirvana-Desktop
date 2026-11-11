// Nirvana Desktop - the logo at several sizes into a PPM file (tools only, not installed)
//   logo-sheet OUT.ppm [SIZE]   one size, or the sheet 16, 24, 32, 48, 64, 128, 256
#include "platinum.h"
#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace plat;
int main(int argc, char **argv) {
  init();
  std::vector<int> sizes = {16, 24, 32, 48, 64, 128, 256};
  if (argc > 2) sizes = {atoi(argv[2])};
  int W = 8, H = 0;
  for (int s : sizes) { W += s + 12; H = std::max(H, s); }
  if (argc > 2) { W = sizes[0]; H = sizes[0]; }
  else H += 16;
  Fl_Image_Surface surf(W, H);
  Fl_Surface_Device::push_current(&surf);
  fl_color(fl_rgb_color(0xC8, 0xC4, 0xBC)); fl_rectf(0, 0, W, H);
  int x = argc > 2 ? 0 : 8;
  for (int s : sizes) { draw_logo(x, argc > 2 ? 0 : 8, s); x += s + 12; }
  Fl_Surface_Device::pop_current();
  Fl_RGB_Image *im = surf.image();
  FILE *o = fopen(argv[1], "wb");
  fprintf(o, "P6 %d %d 255\n", im->w(), im->h());
  int ld = im->ld() ? im->ld() : im->w() * im->d();
  for (int y = 0; y < im->h(); y++)
    for (int xx = 0; xx < im->w(); xx++) fwrite((const char *)im->data()[0] + y * ld + xx * im->d(), 1, 3, o);
  fclose(o);
}
