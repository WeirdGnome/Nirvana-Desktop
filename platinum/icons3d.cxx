// Nirvana Desktop - the "Nirvana 3D" icon style: glossy 3D pixel icons in two states.
//
// Every icon is drawn here with FLTK's primitives, in one visual language: a slight
// isometric view (the top and the right side of an object visible), a crisp 1 px black
// outline, white specular pixels on the lit edges and saturated colors. Nothing is taken
// from any other system - the drawings are Nirvana's own.
//
// Two states (NirvanaIconAtlas): the normal picture and the selected one, kept as a pair of
// RGBA images. The selected one comes, in this order, from
//   1. a sprite sheet <name>.png, twice as wide as high: the left half normal, the right half
//      selected,
//   2. a pair <name>_normal.png + <name>_selected.png,
//   3. a single picture (one of these files, an AppImage's icon, the drawings below):
//      synthesized - moved 1 px down and right (pressed), its highlights brighter and a warm
//      golden glow around it.
// The files are looked for in ~/.local/share/nirvana/icons and <prefix>/share/nirvana/icons
// (names: icon_name() below - "drawer", "harddisk", "trashcan"...).
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unistd.h>

namespace plat {

namespace {

// --- drawing helpers --------------------------------------------------------------------
void c(unsigned v) { fl_color(fl_rgb_color((uchar)(v >> 16), (uchar)(v >> 8), (uchar)v)); }
Fl_Color mix(unsigned a, unsigned b, double t) {
  auto ch = [&](int s) { return (uchar)std::lround(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t); };
  return fl_rgb_color(ch(16), ch(8), ch(0));
}
const unsigned K = 0x000000, W = 0xFFFFFF;

// a filled polygon with a black outline
void poly(std::initializer_list<std::pair<int, int>> pts, unsigned fill) {
  c(fill);
  fl_begin_polygon();
  for (auto &p : pts) fl_vertex(p.first, p.second);
  fl_end_polygon();
  c(K);
  fl_begin_loop();
  for (auto &p : pts) fl_vertex(p.first, p.second);
  fl_end_loop();
}

// a rectangle shaded from top to bottom
void grad(int x, int y, int w, int h, unsigned top, unsigned bottom) {
  for (int i = 0; i < h; i++) {
    fl_color(mix(top, bottom, h > 1 ? (double)i / (h - 1) : 0));
    fl_xyline(x, y + i, x + w - 1);
  }
}

// An isometric box: the front w x h at (x, y + d), the top and the right side d deep.
// Colors: the front from ftop to fbot, the top lighter, the side darker; a white specular
// line on the top-front edge and the left edge.
void box(int x, int y, int w, int h, int d, unsigned ftop, unsigned fbot, unsigned top, unsigned side) {
  poly({{x, y + d}, {x + d, y}, {x + w + d, y}, {x + w, y + d}}, top);
  poly({{x + w, y + d}, {x + w + d, y}, {x + w + d, y + h}, {x + w, y + h + d}}, side);
  grad(x + 1, y + d + 1, w - 1, h - 1, ftop, fbot);
  c(K);
  fl_rect(x, y + d, w + 1, h + 1);
  c(W);
  fl_xyline(x + 1, y + d + 1, x + w - 1);
  fl_yxline(x + 1, y + d + 1, y + d + h - 2);
  fl_xyline(x + d + 1, y + 1, x + w + d - 2);   // the lit back edge of the top
}

void led(int x, int y, unsigned col) {
  c(col); fl_rectf(x, y, 3, 2);
  c(W); fl_point(x, y);
  c(K); fl_rect(x - 1, y - 1, 5, 4);
}

// --- the drawings (32 x 32) ---------------------------------------------------------------

// The drawer: a classic angled manila folder - a back with a tab, the front leaning out
void drawer(int x, int y, bool open, unsigned body = 0xF0B848, unsigned light = 0xFFD978, unsigned dark = 0xC88A1E) {
  poly({{x + 3, y + 7}, {x + 12, y + 7}, {x + 14, y + 10}, {x + 3, y + 10}}, dark);   // tab
  poly({{x + 3, y + 10}, {x + 27, y + 10}, {x + 27, y + 26}, {x + 3, y + 26}}, dark);  // back
  if (open) {
    poly({{x + 7, y + 6}, {x + 24, y + 4}, {x + 25, y + 20}, {x + 8, y + 21}}, 0xFFFFFF);   // a paper
    c(0x7FA8D8);
    for (int i = 0; i < 4; i++) fl_line(x + 10, y + 9 + i * 3, x + 21, y + 8 + i * 3);
    poly({{x + 1, y + 17}, {x + 25, y + 17}, {x + 30, y + 28}, {x + 5, y + 28}}, body);   // the front, opened
    c(light); fl_xyline(x + 3, y + 18, x + 24);
    c(W); fl_xyline(x + 3, y + 18, x + 8);
  } else {
    poly({{x + 2, y + 13}, {x + 26, y + 13}, {x + 29, y + 28}, {x + 5, y + 28}}, body);   // the front
    // shading: the lower part darker
    for (int i = 0; i < 6; i++) {
      fl_color(mix(body, dark, i / 8.0));
      fl_xyline(x + 5 + (i * 3) / 15 + 3, y + 22 + i, x + 27 + (i * 3) / 15);
    }
    c(light); fl_xyline(x + 3, y + 14, x + 25);
    c(W); fl_xyline(x + 3, y + 14, x + 9); fl_point(x + 3, y + 15);
    c(K);
    fl_begin_loop();
    fl_vertex(x + 2, y + 13); fl_vertex(x + 26, y + 13); fl_vertex(x + 29, y + 28); fl_vertex(x + 5, y + 28);
    fl_end_loop();
  }
}

// A sheet of paper with a folded corner and a little thickness
void page(int x, int y) {
  c(0x8A8A8A); fl_rectf(x + 9, y + 4, 17, 25);   // thickness (right and bottom)
  poly({{x + 6, y + 2}, {x + 19, y + 2}, {x + 24, y + 7}, {x + 24, y + 27}, {x + 6, y + 27}}, 0xFFFFFF);
  poly({{x + 19, y + 2}, {x + 19, y + 7}, {x + 24, y + 7}}, 0xD8D8E8);
  grad(x + 7, y + 20, 17, 7, 0xFFFFFF, 0xE4E8F0);
  c(K);
  fl_begin_loop();
  fl_vertex(x + 6, y + 2); fl_vertex(x + 19, y + 2); fl_vertex(x + 24, y + 7); fl_vertex(x + 24, y + 27); fl_vertex(x + 6, y + 27);
  fl_end_loop();
}

void document(int x, int y) {
  page(x, y);
  c(0x3F6FC0);
  for (int i = 0; i < 5; i++) fl_xyline(x + 9, y + 10 + i * 3, x + (i == 4 ? 16 : 21));
}

void picture(int x, int y) {
  page(x, y);
  grad(x + 8, y + 9, 15, 8, 0x5FA8F0, 0xBFE2FF);
  poly({{x + 8, y + 22}, {x + 12, y + 15}, {x + 15, y + 19}, {x + 18, y + 14}, {x + 23, y + 22}}, 0x2E9E48);
  c(0xFFD020); fl_pie(x + 18, y + 10, 4, 4, 0, 360);
  c(K); fl_rect(x + 8, y + 9, 16, 14);
}

void music(int x, int y) {
  page(x, y);
  c(K);
  fl_rectf(x + 12, y + 10, 2, 11); fl_rectf(x + 19, y + 8, 2, 11);
  fl_rectf(x + 12, y + 9, 9, 2);
  c(0x7A3FD0); fl_pie(x + 8, y + 18, 6, 5, 0, 360); fl_pie(x + 15, y + 16, 6, 5, 0, 360);
  c(K); fl_arc(x + 8, y + 18, 6, 5, 0, 360); fl_arc(x + 15, y + 16, 6, 5, 0, 360);
  c(W); fl_point(x + 10, y + 19); fl_point(x + 17, y + 17);
}

// A system unit / hard disk: a silver box with a drive slot, vents and two LEDs
void hard_disk(int x, int y, bool home) {
  box(x + 2, y + 8, 24, 13, 4, home ? 0xD9DEE8 : 0xE6E6E6, home ? 0x9AA6BC : 0xA8A8A8, 0xF4F4F4, 0x808890);
  c(0x3C3C3C); fl_rectf(x + 5, y + 16, 12, 2);   // the slot
  c(0x9A9A9A); fl_xyline(x + 5, y + 19, x + 16);
  c(0x6A6A6A);
  for (int i = 0; i < 3; i++) fl_xyline(x + 5, y + 22 + i, x + 13);
  led(x + 20, y + 16, 0x30E050);
  led(x + 20, y + 21, home ? 0x4A8CFF : 0xFF3030);
  if (home) {   // Nirvana HD: a blue badge
    c(0x2A56C0); fl_rectf(x + 14, y + 21, 3, 3);
  }
}

// A memory module: a green board with gold contacts, black chips and a glowing check mark
void ram_disk(int x, int y) {
  poly({{x + 2, y + 13}, {x + 6, y + 9}, {x + 30, y + 9}, {x + 26, y + 13}}, 0x5CCB7A);   // top edge
  grad(x + 2, y + 13, 25, 12, 0x2EA052, 0x15652F);
  c(K); fl_rect(x + 2, y + 13, 25, 13);
  poly({{x + 26, y + 13}, {x + 30, y + 9}, {x + 30, y + 21}, {x + 26, y + 25}}, 0x0F4A22);
  for (int i = 0; i < 3; i++) {
    c(0x151515); fl_rectf(x + 4 + i * 7, y + 15, 5, 5);
    c(0x555555); fl_xyline(x + 5 + i * 7, y + 16, x + 7 + i * 7);
  }
  c(0xF2C232);
  for (int i = 0; i < 11; i++) fl_rectf(x + 4 + i * 2, y + 22, 1, 3);
  c(W); fl_xyline(x + 3, y + 14, x + 25);
  // the check mark with a glow
  c(0xFFE680);
  fl_line_style(FL_SOLID, 3);
  fl_line(x + 19, y + 4, x + 22, y + 7); fl_line(x + 22, y + 7, x + 28, y + 1);
  c(0xFFFFFF);
  fl_line_style(FL_SOLID, 1);
  fl_line(x + 19, y + 4, x + 22, y + 7); fl_line(x + 22, y + 7, x + 28, y + 1);
  fl_line_style(0);
}

// A 3.5" diskette, a little turned: a blue body, a silver shutter, a white label
void floppy(int x, int y) {
  poly({{x + 4, y + 4}, {x + 25, y + 4}, {x + 29, y + 8}, {x + 29, y + 28}, {x + 4, y + 28}}, 0x2D5FC8);
  grad(x + 5, y + 5, 23, 22, 0x4A80E8, 0x1E46A0);
  c(K);
  fl_begin_loop();
  fl_vertex(x + 4, y + 4); fl_vertex(x + 25, y + 4); fl_vertex(x + 29, y + 8); fl_vertex(x + 29, y + 28); fl_vertex(x + 4, y + 28);
  fl_end_loop();
  grad(x + 9, y + 5, 13, 8, 0xF0F0F0, 0xA8A8A8);   // shutter
  c(K); fl_rect(x + 9, y + 4, 14, 10);
  c(0x303030); fl_rectf(x + 17, y + 6, 3, 6);
  poly({{x + 7, y + 17}, {x + 26, y + 17}, {x + 26, y + 28}, {x + 7, y + 28}}, 0xFFFFFF);   // label
  c(0xE04040); fl_xyline(x + 9, y + 20, x + 23);
  c(0x7F7F7F); fl_xyline(x + 9, y + 23, x + 21); fl_xyline(x + 9, y + 25, x + 18);
  c(W); fl_xyline(x + 5, y + 5, x + 8); fl_yxline(x + 5, y + 6, y + 12);
}

void optical(int x, int y) {
  c(0xC8CCD8); fl_pie(x + 3, y + 3, 26, 26, 0, 360);
  c(0x9FD0FF); fl_pie(x + 3, y + 3, 26, 26, 30, 75);
  c(0xFFB0E0); fl_pie(x + 3, y + 3, 26, 26, 210, 255);
  c(0xFFF2A0); fl_pie(x + 3, y + 3, 26, 26, 120, 150);
  c(W); fl_pie(x + 8, y + 6, 8, 5, 0, 360);   // the shine
  c(0xE8E8F0); fl_pie(x + 11, y + 11, 10, 10, 0, 360);
  c(K); fl_arc(x + 3, y + 3, 26, 26, 0, 360); fl_arc(x + 13, y + 13, 6, 6, 0, 360);
  c(0x606060); fl_arc(x + 11, y + 11, 10, 10, 0, 360);
}

void padlock(int x, int y) {
  c(K); fl_line_style(FL_SOLID, 2); fl_arc(x + 19, y + 9, 9, 10, 0, 180); fl_line_style(0);
  c(K); fl_yxline(x + 19, y + 13, y + 15); fl_yxline(x + 27, y + 13, y + 15);
  grad(x + 18, y + 15, 11, 9, 0xFFE070, 0xD09010);
  c(K); fl_rect(x + 17, y + 14, 13, 11);
  c(W); fl_xyline(x + 18, y + 15, x + 22);
  c(K); fl_rectf(x + 23, y + 18, 1, 3);
}

void eject(int x, int y) {
  poly({{x + 16, y + 5}, {x + 28, y + 18}, {x + 4, y + 18}}, 0x40B0FF);
  c(W); fl_line(x + 16, y + 7, x + 8, y + 16);
  box(x + 4, y + 20, 22, 4, 2, 0x40B0FF, 0x2070C0, 0x90D8FF, 0x1A5090);
}

// A modem / network box: a dark case with a front of lights and an antenna
void network(int x, int y, bool wifi) {
  box(x + 2, y + 13, 24, 10, 4, 0x5A6070, 0x2E323C, 0x8A92A4, 0x1C2028);
  led(x + 6, y + 22, 0x30F060);
  led(x + 12, y + 22, 0xFF4040);
  led(x + 18, y + 22, 0xFFC030);
  if (wifi) {
    c(K); fl_rectf(x + 23, y + 3, 2, 12);
    c(0xFFC030); fl_pie(x + 22, y + 1, 4, 4, 0, 360);
    c(0x40A0FF);
    fl_arc(x + 16, y - 2, 16, 16, 20, 70); fl_arc(x + 12, y - 6, 24, 24, 25, 65);
  } else {
    c(0x2A6AD0); fl_rectf(x + 8, y + 5, 12, 4);   // a cable plug
    c(K); fl_rect(x + 8, y + 5, 12, 4); fl_yxline(x + 14, y + 9, y + 15);
  }
}

// Preferences: an amber drawer with a gear and a slider sticking out of it
void gear(int cx, int cy, int r, unsigned col) {
  c(col);
  for (int i = 0; i < 8; i++) {
    double a = i * M_PI / 4;
    int tx = cx + (int)std::lround(std::cos(a) * r), ty = cy - (int)std::lround(std::sin(a) * r);
    fl_rectf(tx - 1, ty - 1, 3, 3);
  }
  fl_pie(cx - r + 1, cy - r + 1, 2 * r - 1, 2 * r - 1, 0, 360);
  c(K); fl_arc(cx - r + 1, cy - r + 1, 2 * r - 1, 2 * r - 1, 0, 360);
  c(0x404040); fl_pie(cx - 2, cy - 2, 5, 5, 0, 360);
  c(W); fl_point(cx - r / 2, cy - r / 2);
}

void preferences(int x, int y) {
  drawer(x, y, true, 0xF0B848, 0xFFD978, 0xC88A1E);
  gear(x + 12, y + 10, 6, 0xB8C0CC);
  // a slider
  c(K); fl_rectf(x + 20, y + 3, 2, 14);
  grad(x + 18, y + 7, 6, 4, 0xFF7050, 0xC03018);
  c(K); fl_rect(x + 18, y + 7, 6, 4);
}

// A program: a cartridge with a colorful label (a tool)
void tool(int x, int y) {
  box(x + 3, y + 7, 20, 18, 4, 0x8C96AA, 0x565E70, 0xB4BCCC, 0x3A4050);
  grad(x + 6, y + 13, 14, 9, 0xFF9030, 0xE04020);
  c(K); fl_rect(x + 6, y + 13, 15, 10);
  c(0xFFF4C0); fl_xyline(x + 8, y + 16, x + 18);
  c(W); fl_xyline(x + 8, y + 15, x + 12);
  c(0x2A2E38);
  for (int i = 0; i < 6; i++) fl_rectf(x + 6 + i * 3, y + 26, 2, 2);   // contacts
}

// The applications: a drawer with a cartridge
void applications(int x, int y) {
  drawer(x, y, true);
  box(x + 9, y + 5, 11, 9, 3, 0xFF9030, 0xE04020, 0xFFC080, 0x902010);
}

// The shell: a window with a dark screen, "1>" and a glowing cursor
void terminal(int x, int y) {
  box(x + 2, y + 6, 24, 19, 3, 0xD8D8D8, 0xA0A0A0, 0xF0F0F0, 0x707070);
  c(0x101418); fl_rectf(x + 4, y + 11, 21, 13);
  c(K); fl_rect(x + 4, y + 11, 21, 13);
  c(0xFFB030);
  // "1>"
  fl_yxline(x + 7, y + 14, y + 19); fl_point(x + 6, y + 15);
  fl_line(x + 10, y + 14, x + 13, y + 16); fl_line(x + 13, y + 16, x + 10, y + 18);
  c(W); fl_rectf(x + 16, y + 17, 4, 2);   // the cursor
}

// The Trash: a metallic fluted bin with a lid; full - paper scraps over the edge
void trashcan(int x, int y, bool full) {
  poly({{x + 7, y + 10}, {x + 25, y + 10}, {x + 23, y + 29}, {x + 9, y + 29}}, 0xB8BEC8);
  for (int i = 0; i < 9; i++) {   // the flutes, lit on the left
    int xx = x + 9 + i * 2;
    fl_color(i % 2 ? mix(0x8A92A0, 0x8A92A0, 0) : mix(0xE6EAF0, 0xC8CED8, i / 9.0));
    fl_line(xx, y + 12, xx + (xx < x + 16 ? 1 : 0), y + 27);
  }
  c(K);
  fl_begin_loop();
  fl_vertex(x + 7, y + 10); fl_vertex(x + 25, y + 10); fl_vertex(x + 23, y + 29); fl_vertex(x + 9, y + 29);
  fl_end_loop();
  if (full) {
    poly({{x + 9, y + 10}, {x + 12, y + 3}, {x + 17, y + 7}, {x + 15, y + 11}}, 0xFFFFFF);
    poly({{x + 16, y + 9}, {x + 22, y + 2}, {x + 25, y + 8}, {x + 21, y + 11}}, 0xFFF0B0);
    c(0x7F7F7F); fl_line(x + 12, y + 6, x + 14, y + 8);
  }
  // the lid
  grad(x + 5, y + (full ? 9 : 7), 23, 3, 0xF4F6FA, 0x9AA2B0);
  c(K); fl_rect(x + 5, y + (full ? 9 : 7), 23, 4);
  c(0x6A7280); fl_rectf(x + 14, y + (full ? 7 : 5), 5, 2);
  c(K); fl_rect(x + 13, y + (full ? 6 : 4), 7, 3);
  c(W); fl_xyline(x + 6, y + (full ? 10 : 8), x + 12);
}

}  // namespace

// The drawings - false: none for this icon (the caller falls back)
static bool draw_3d(Icon icon, int x, int y) {
  switch (icon) {
  case Icon::Folder:       drawer(x, y, false); break;
  case Icon::OpenFolder:   drawer(x, y, true); break;
  case Icon::Document:     document(x, y); break;
  case Icon::Image:        picture(x, y); break;
  case Icon::Music:        music(x, y); break;
  case Icon::Application:  tool(x, y); break;
  case Icon::Home:         hard_disk(x, y, true); break;
  case Icon::HardDisk:     hard_disk(x, y, false); break;
  case Icon::Removable:    floppy(x, y); break;
  case Icon::Optical:      optical(x, y); break;
  case Icon::Encrypted:    hard_disk(x, y, false); padlock(x, y); break;
  case Icon::Eject:        eject(x, y); break;
  case Icon::Network:      network(x, y, false); break;
  case Icon::Wifi:         network(x, y, true); break;
  case Icon::Preferences:  preferences(x, y); break;
  case Icon::Applications: applications(x, y); break;
  case Icon::Terminal:     terminal(x, y); break;
  case Icon::RamDisk:      ram_disk(x, y); break;
  case Icon::Trash:        trashcan(x, y, false); break;
  case Icon::TrashFull:    trashcan(x, y, true); break;
  default:
    if (icon >= Icon::CatGames && icon <= Icon::CatOther) {   // a drawer with the category emblem
      drawer(x, y, false);
      draw_category_emblem((int)icon - (int)Icon::CatGames, x + 10, y + 13, false, 1);
      return true;
    }
    return false;
  }
  return true;
}

// The file names of the icons (for pictures that replace the drawings)
const char *icon_name(Icon icon) {
  switch (icon) {
  case Icon::Folder: return "drawer";
  case Icon::OpenFolder: return "drawer-open";
  case Icon::Document: return "document";
  case Icon::Application: return "tool";
  case Icon::Home: return "nirvana-hd";
  case Icon::Trash: return "trashcan";
  case Icon::TrashFull: return "trashcan-full";
  case Icon::Image: return "picture";
  case Icon::Music: return "sound";
  case Icon::HardDisk: return "harddisk";
  case Icon::Removable: return "removable";
  case Icon::Optical: return "optical";
  case Icon::Encrypted: return "encrypted";
  case Icon::Eject: return "eject";
  case Icon::Network: return "network";
  case Icon::Wifi: return "wifi";
  case Icon::Preferences: return "prefs";
  case Icon::Applications: return "applications";
  case Icon::Terminal: return "shell";
  case Icon::RamDisk: return "ramdisk";
  default: break;
  }
  static const char *cats[] = {"cat-games", "cat-internet", "cat-office", "cat-graphics", "cat-multimedia",
                               "cat-development", "cat-education", "cat-science", "cat-system",
                               "cat-accessories", "cat-other"};
  int i = (int)icon - (int)Icon::CatGames;
  return i >= 0 && i < 11 ? cats[i] : "unknown";
}

// --- the two states ----------------------------------------------------------------------

// A drawing as an RGBA picture: drawn once on black and once on white - where both agree the
// pixel is the drawing's, elsewhere transparent (the drawings have no half-transparent pixels)
static Fl_RGB_Image *render(Icon icon) {
  uchar *px[2] = {nullptr, nullptr};
  for (int pass = 0; pass < 2; pass++) {
    Fl_Image_Surface surf(32, 32);
    Fl_Surface_Device::push_current(&surf);
    fl_color(pass ? FL_WHITE : FL_BLACK);
    fl_rectf(0, 0, 32, 32);
    draw_3d(icon, 0, 0);
    Fl_Surface_Device::pop_current();
    Fl_RGB_Image *im = surf.image();
    int d = im->d(), ld = im->ld() ? im->ld() : 32 * d;
    px[pass] = new uchar[32 * 32 * 3];
    const uchar *s = (const uchar *)im->data()[0];
    for (int y = 0; y < 32; y++)
      for (int x = 0; x < 32; x++) memcpy(px[pass] + (y * 32 + x) * 3, s + y * ld + x * d, 3);
    delete im;
  }
  auto *out = new uchar[32 * 32 * 4];
  for (int i = 0; i < 32 * 32; i++) {
    const uchar *b = px[0] + i * 3, *w = px[1] + i * 3;
    bool same = b[0] == w[0] && b[1] == w[1] && b[2] == w[2];
    out[i * 4 + 0] = b[0];
    out[i * 4 + 1] = b[1];
    out[i * 4 + 2] = b[2];
    out[i * 4 + 3] = same ? 255 : 0;
  }
  delete[] px[0];
  delete[] px[1];
  auto *img = new Fl_RGB_Image(out, 32, 32, 4);
  img->alloc_array = 1;
  return img;
}

// RGBA copy of any picture (an RGB one fully opaque)
static Fl_RGB_Image *as_rgba(const Fl_RGB_Image *src) {
  int w = src->w(), h = src->h(), d = src->d(), ld = src->ld() ? src->ld() : w * d;
  if (!w || !h || (d != 3 && d != 4 && d != 1 && d != 2)) return nullptr;
  auto *out = new uchar[w * h * 4];
  const uchar *s = (const uchar *)src->data()[0];
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const uchar *p = s + y * ld + x * d;
      uchar *q = out + (y * w + x) * 4;
      if (d >= 3) { q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = d == 4 ? p[3] : 255; }
      else { q[0] = q[1] = q[2] = p[0]; q[3] = d == 2 ? p[1] : 255; }
    }
  auto *img = new Fl_RGB_Image(out, w, h, 4);
  img->alloc_array = 1;
  return img;
}

Fl_RGB_Image *synthesize_selected(const Fl_RGB_Image *normal) {
  Fl_RGB_Image *n = as_rgba(normal);
  if (!n) return nullptr;
  int w = n->w(), h = n->h();
  const uchar *s = (const uchar *)n->data()[0];
  auto *out = new uchar[w * h * 4]();
  // 1. pressed: one pixel (per 32) down and to the right, the highlights brighter
  int sh = std::max(1, w / 32);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int sx = x - sh, sy = y - sh;
      if (sx < 0 || sy < 0) continue;
      const uchar *p = s + (sy * w + sx) * 4;
      uchar *q = out + (y * w + x) * 4;
      int lum = (p[0] * 3 + p[1] * 6 + p[2]) / 10;
      for (int k = 0; k < 3; k++) {
        double v = p[k] * 1.12;
        if (lum > 170) v = v + (255 - v) * 0.5;   // the specular pixels toward white
        q[k] = (uchar)std::min(255.0, v);
      }
      q[3] = p[3];
    }
  // 2. a warm golden glow around the shape (2 pixels per 32)
  int reach = std::max(2, w / 16);
  std::vector<uchar> a(w * h);
  for (int i = 0; i < w * h; i++) a[i] = out[i * 4 + 3];
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      if (a[y * w + x] > 128) continue;
      int best = reach + 1;
      for (int dy = -reach; dy <= reach && best > 1; dy++)
        for (int dx = -reach; dx <= reach; dx++) {
          int xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= w || yy >= h || a[yy * w + xx] <= 128) continue;
          int dist = std::max(std::abs(dx), std::abs(dy));
          best = std::min(best, dist);
        }
      if (best > reach) continue;
      uchar *q = out + (y * w + x) * 4;
      int glow = best == 1 ? 210 : (int)(150.0 * (reach - best + 1) / reach);
      // the glow under a half-transparent pixel: mixed
      int pa = q[3];
      q[0] = (uchar)((0xFF * glow + q[0] * pa) / std::max(1, glow + pa));
      q[1] = (uchar)((0xB0 * glow + q[1] * pa) / std::max(1, glow + pa));
      q[2] = (uchar)((0x28 * glow + q[2] * pa) / std::max(1, glow + pa));
      q[3] = (uchar)std::max(pa, glow);
    }
  delete n;
  auto *img = new Fl_RGB_Image(out, w, h, 4);
  img->alloc_array = 1;
  return img;
}

// Pictures that replace a drawing: a sprite sheet <name>.png (2W x H) or a pair
static std::vector<std::string> icon_dirs() {
  std::vector<std::string> v;
  const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
  v.push_back((x && *x ? std::string(x) : std::string(h ? h : "") + "/.local/share") + "/nirvana/icons");
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n > 0) {
    std::string e(buf, (size_t)n);
    e = e.substr(0, e.rfind('/'));
    v.push_back(e + "/../share/nirvana/icons");
    v.push_back(e + "/../icons");   // the build folder (tests)
  }
  v.push_back("/usr/local/share/nirvana/icons");
  v.push_back("/usr/share/nirvana/icons");
  return v;
}

static Fl_RGB_Image *load_png(const std::string &f) {
  if (access(f.c_str(), R_OK) != 0) return nullptr;
  Fl_PNG_Image png(f.c_str());
  if (png.fail() || png.w() <= 0) return nullptr;
  return as_rgba(&png);
}

static Fl_RGB_Image *crop(const Fl_RGB_Image *src, int x0, int w) {
  int h = src->h(), sw = src->w();
  auto *out = new uchar[w * h * 4];
  const uchar *s = (const uchar *)src->data()[0];
  for (int y = 0; y < h; y++) memcpy(out + y * w * 4, s + (y * sw + x0) * 4, (size_t)w * 4);
  auto *img = new Fl_RGB_Image(out, w, h, 4);
  img->alloc_array = 1;
  return img;
}

static Fl_RGB_Image *fit(Fl_RGB_Image *img, int size) {
  if (!img || (img->w() == size && img->h() == size)) return img;
  Fl_RGB_Scaling was = Fl_Image::RGB_scaling();
  // whole multiples stay crisp pixels; other sizes smooth
  Fl_Image::RGB_scaling(size % img->w() == 0 || img->w() % size == 0 ? FL_RGB_SCALING_NEAREST : FL_RGB_SCALING_BILINEAR);
  auto *c = (Fl_RGB_Image *)img->copy(size, size);
  Fl_Image::RGB_scaling(was);
  delete img;
  return c;
}

const IconStates &icon_states(Icon icon, int size) {
  static std::map<std::pair<int, int>, IconStates> atlas;
  auto key = std::make_pair((int)icon, size);
  auto it = atlas.find(key);
  if (it != atlas.end()) return it->second;
  IconStates st;
  std::string name = icon_name(icon);
  for (auto &d : icon_dirs()) {
    if (Fl_RGB_Image *sheet = load_png(d + "/" + name + ".png")) {
      if (sheet->w() == 2 * sheet->h()) {   // a sprite sheet: normal | selected
        st.normal = crop(sheet, 0, sheet->h());
        st.selected = crop(sheet, sheet->h(), sheet->h());
      } else {
        st.normal = sheet;
        sheet = nullptr;
      }
      delete sheet;
      break;
    }
    if (Fl_RGB_Image *n = load_png(d + "/" + name + "_normal.png")) {
      st.normal = n;
      st.selected = load_png(d + "/" + name + "_selected.png");
      break;
    }
  }
  if (!st.normal) st.normal = render(icon);   // Nirvana's own drawing (32 px)
  st.normal = fit(st.normal, size);
  if (st.selected) st.selected = fit(st.selected, size);
  else st.selected = synthesize_selected(st.normal);   // the third way: made from the normal one
  return atlas[key] = st;
}

bool draw_icon_3d(Icon icon, int x, int y, bool sel) {
  if (icon_name(icon) == std::string("unknown")) return false;
  const IconStates &st = icon_states(icon, 32);
  Fl_RGB_Image *im = sel ? st.selected : st.normal;
  if (!im) return false;
  im->draw(x, y);
  return true;
}

}  // namespace plat
