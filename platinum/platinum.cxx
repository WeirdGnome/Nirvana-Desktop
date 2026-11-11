// Nirvana Desktop - libplatinum: the Platinum look implementation
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Tooltip.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include <FL/fl_utf8.h>
#include <FL/platform.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace plat {

Fl_Boxtype UP_BOX, DOWN_BOX, BUTTON_BOX, BUTTON_DOWN, WELL_BOX, MENUBAR_BOX,
           THUMB_BOX, TRACK_BOX;
Fl_Font FONT_SYSTEM = FL_HELVETICA_BOLD;
Fl_Font FONT_SMALL  = FL_HELVETICA;


static inline Fl_Color rgb(unsigned v) {
  return fl_rgb_color((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// ---------------------------------------------------------------------------
// Box types
// ---------------------------------------------------------------------------
static void bevel(int x, int y, int w, int h, Fl_Color tl, Fl_Color br) {
  fl_color(tl);
  fl_xyline(x, y, x + w - 1);
  fl_yxline(x, y, y + h - 1);
  fl_color(br);
  fl_xyline(x, y + h - 1, x + w - 1);
  fl_yxline(x + w - 1, y, y + h - 1);
}

static void up_box(int x, int y, int w, int h, Fl_Color c) {
  fl_color(c);
  fl_rectf(x, y, w, h);
  bevel(x, y, w, h, LIGHT, MID);
}

static void down_box(int x, int y, int w, int h, Fl_Color c) {
  fl_color(c);
  fl_rectf(x, y, w, h);
  bevel(x, y, w, h, MID, LIGHT);
}

static void up_frame(int x, int y, int w, int h, Fl_Color) { bevel(x, y, w, h, LIGHT, MID); }
static void down_frame(int x, int y, int w, int h, Fl_Color) { bevel(x, y, w, h, MID, LIGHT); }

// Platinum button: a black rounded outline, a light top-left edge.
static void button_box(int x, int y, int w, int h, Fl_Color c) {
  fl_color(c);
  fl_rounded_rectf(x, y, w, h, 3);
  fl_color(LIGHT);
  fl_xyline(x + 2, y + 1, x + w - 3);
  fl_yxline(x + 1, y + 2, y + h - 3);
  fl_color(MID);
  fl_xyline(x + 2, y + h - 2, x + w - 3);
  fl_yxline(x + w - 2, y + 2, y + h - 3);
  fl_color(DARK);
  fl_rounded_rect(x, y, w, h, 3);
}

static void button_down(int x, int y, int w, int h, Fl_Color) {
  fl_color(SHADOW);
  fl_rounded_rectf(x, y, w, h, 3);
  fl_color(fl_darker(SHADOW));
  fl_xyline(x + 2, y + 1, x + w - 3);
  fl_yxline(x + 1, y + 2, y + h - 3);
  fl_color(DARK);
  fl_rounded_rect(x, y, w, h, 3);
}

// "Well": a white inside, a dark inner outline, a light outer one.
static void well_box(int x, int y, int w, int h, Fl_Color c) {
  fl_color(c);
  fl_rectf(x + 2, y + 2, w - 4, h - 4);
  bevel(x, y, w, h, MID, LIGHT);
  fl_color(DARK);
  fl_rect(x + 1, y + 1, w - 2, h - 2);
}

static void menubar_box(int x, int y, int w, int h, Fl_Color c) {
  if (title_style() == TitleStyle::Glossy && h > 4 && h <= 32) {   // (not the tall pull-down menus, they use this box too)
    // glass, like the title bars: a shine on the upper half, then the bar color with a little
    // light coming back at the bottom (depends only on y, so all the pieces of the bar match)
    uchar r, g, b;
    Fl::get_color(c, r, g, b);
    for (int i = 0; i < h - 2; i++) {
      double t = (double)i / (h - 3);
      // + toward white, - toward black: bright shine on top, a darker band below the middle
      double k = t < 0.5 ? 0.85 - 0.9 * t : -0.14 + 0.2 * (t - 0.5) * 2;
      auto ch = [&](uchar v) { return (uchar)(k >= 0 ? v + (255 - v) * k : v * (1 + k)); };
      fl_color(fl_rgb_color(ch(r), ch(g), ch(b)));
      fl_xyline(x, y + i, x + w - 1);
    }
    fl_color(MID);
    fl_xyline(x, y + h - 2, x + w - 1);
    fl_color(DARK);
    fl_xyline(x, y + h - 1, x + w - 1);
    return;
  }
  fl_color(c);
  fl_rectf(x, y, w, h);
  fl_color(LIGHT);
  fl_xyline(x, y, x + w - 1);
  fl_color(MID);
  fl_xyline(x, y + h - 2, x + w - 1);
  fl_color(DARK);
  fl_xyline(x, y + h - 1, x + w - 1);
}

// Scroll thumb: lavender with grooves in the middle.
static void thumb_box(int x, int y, int w, int h, Fl_Color c) {
  fl_color(c);
  fl_rectf(x, y, w, h);
  bevel(x + 1, y + 1, w - 2, h - 2, fl_lighter(c), fl_darker(c));
  fl_color(DARK);
  fl_rect(x, y, w, h);
  if (w > h + 6) {          // horizontal
    int cx = x + w / 2 - 4;
    for (int i = 0; i < 4; i++) {
      fl_color(fl_lighter(c)); fl_yxline(cx + i * 2, y + 4, y + h - 5);
      fl_color(fl_darker(c));  fl_yxline(cx + i * 2 + 1, y + 4, y + h - 5);
    }
  } else if (h > w + 6) {   // vertical
    int cy = y + h / 2 - 4;
    for (int i = 0; i < 4; i++) {
      fl_color(fl_lighter(c)); fl_xyline(x + 4, cy + i * 2, x + w - 5);
      fl_color(fl_darker(c));  fl_xyline(x + 4, cy + i * 2 + 1, x + w - 5);
    }
  }
}

static void track_box(int x, int y, int w, int h, Fl_Color) {
  fl_color(MID);
  fl_rectf(x, y, w, h);
  fl_color(SHADOW);
  fl_rect(x, y, w, h);
  fl_color(fl_darker(MID));
  fl_xyline(x + 1, y + 1, x + w - 2);
  fl_yxline(x + 1, y + 1, y + h - 2);
}

// ---------------------------------------------------------------------------
// Interface scale
// ---------------------------------------------------------------------------
const int SCALES[] = {0, 100, 125, 150, 175, 200, 250, 300, -1};
static float g_scale = 1.0f;

float ui_scale() { return g_scale; }

// Xft.dpi from the X resources (GNOME/KDE/Xfce read the same): 96 dpi = 100%, 192 = 200%
static float xft_scale() {
#if defined(FLTK_USE_X11)
  if (!fl_x11_display()) return 1.0f;
  const char *res = XResourceManagerString(fl_x11_display());
  if (!res) return 1.0f;
  const char *p = strstr(res, "Xft.dpi:");
  if (!p) return 1.0f;
  double dpi = atof(p + 8);
  if (dpi < 48 || dpi > 480) return 1.0f;
  return (float)(std::round(dpi / 96.0 * 4) / 4);   // co 25%
#else
  return 1.0f;
#endif
}

float configured_scale(const Appearance &a) {
  if (const char *e = getenv("NIRVANA_SCALE")) {   // e.g. NIRVANA_SCALE=150 or 1.5
    double v = atof(e);
    if (v > 4) v /= 100;
    if (v >= 0.5 && v <= 4) return (float)v;
  }
  if (a.scale > 0) return a.scale / 100.0f;
  return xft_scale();
}

static void apply_scale() {
  fl_open_display();
  // All Nirvana Desktop programs (window manager, desktop, panels) must use the
  // same scale - they exchange coordinates. Our own scale instead of FLTK's
  // automatic one; no scale changes with Ctrl +/-.
  g_scale = std::clamp(configured_scale(read_appearance()), 0.5f, 4.0f);
  Fl::keyboard_screen_scaling(0);
  Fl::option(Fl::OPTION_SHOW_SCALING, false);
  for (int i = 0; i < Fl::screen_count(); i++) Fl::screen_scale(i, g_scale);
}

void init() {
  init_i18n();   // before the first interface text
  Fl::scheme("none");
  reload_appearance();   // color scheme from ~/.config/nirvana/appearance.conf
  apply_scale();

  int f = FL_FREE_BOXTYPE;
  UP_BOX      = Fl_Boxtype(f++); Fl::set_boxtype(UP_BOX, up_box, 1, 1, 2, 2);
  DOWN_BOX    = Fl_Boxtype(f++); Fl::set_boxtype(DOWN_BOX, down_box, 1, 1, 2, 2);
  BUTTON_BOX  = Fl_Boxtype(f++); Fl::set_boxtype(BUTTON_BOX, button_box, 2, 2, 4, 4);
  BUTTON_DOWN = Fl_Boxtype(f++); Fl::set_boxtype(BUTTON_DOWN, button_down, 2, 2, 4, 4);
  WELL_BOX    = Fl_Boxtype(f++); Fl::set_boxtype(WELL_BOX, well_box, 2, 2, 4, 4);
  MENUBAR_BOX = Fl_Boxtype(f++); Fl::set_boxtype(MENUBAR_BOX, menubar_box, 0, 0, 0, 2);
  THUMB_BOX   = Fl_Boxtype(f++); Fl::set_boxtype(THUMB_BOX, thumb_box, 1, 1, 2, 2);
  TRACK_BOX   = Fl_Boxtype(f++); Fl::set_boxtype(TRACK_BOX, track_box, 1, 1, 2, 2);

  // Standard FLTK widgets get the Platinum look too.
  Fl::set_boxtype(FL_UP_BOX, up_box, 1, 1, 2, 2);
  Fl::set_boxtype(FL_THIN_UP_BOX, up_box, 1, 1, 2, 2);
  Fl::set_boxtype(FL_DOWN_BOX, down_box, 1, 1, 2, 2);
  Fl::set_boxtype(FL_THIN_DOWN_BOX, down_box, 1, 1, 2, 2);
  Fl::set_boxtype(FL_UP_FRAME, up_frame, 1, 1, 2, 2);
  Fl::set_boxtype(FL_DOWN_FRAME, down_frame, 1, 1, 2, 2);

  // If the user has chosen a system font (e.g. a free bitmap-style one), use it.
  if (const char *ff = getenv("NIRVANA_SYSTEM_FONT")) {
    Fl::set_font(FL_FREE_FONT, ff);
    FONT_SYSTEM = FL_FREE_FONT;
  }
  // buttons of FLTK dialogs (fl_alert, color chooser...) in the interface language
  fl_ok = _("OK");
  fl_cancel = _("Cancel");
  fl_yes = _("Yes");
  fl_no = _("No");
  fl_close = _("Close");
  // tooltips (dock, applets, gary): a yellow help balloon
  Fl_Tooltip::font(FONT_SMALL);
  Fl_Tooltip::size(SIZE_SMALL + 1);
  Fl_Tooltip::color(fl_rgb_color(255, 255, 204));
  Fl_Tooltip::textcolor(FL_BLACK);
  Fl_Tooltip::margin_width(5);
  Fl_Tooltip::margin_height(3);
}

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------
// A selected icon is darkened.
static inline void col(unsigned v, bool sel) {
  Fl_Color c = rgb(v);
  fl_color(sel ? fl_color_average(c, FL_BLACK, 0.5f) : c);
}

static void folder(int x, int y, bool sel, bool open) {
  col(0xA8A8F0, sel);
  fl_polygon(x + 2, y + 9, x + 4, y + 6, x + 13, y + 6, x + 15, y + 9);
  fl_rectf(x + 2, y + 9, 28, 19);
  col(0xD4D4FF, sel);
  fl_xyline(x + 3, y + 10, x + 28);
  fl_xyline(x + 4, y + 7, x + 12);
  col(0x7272C8, sel);
  fl_xyline(x + 3, y + 26, x + 28);
  fl_yxline(x + 28, y + 10, y + 26);
  if (open) {  // folder with an open window: a hatched pattern
    col(0x55559A, sel);
    for (int yy = y + 11; yy < y + 26; yy += 2)
      for (int xx = x + 4 + (yy & 2); xx < x + 28; xx += 4) fl_point(xx, yy);
  }
  fl_color(DARK);
  fl_loop(x + 2, y + 9, x + 4, y + 6, x + 13, y + 6, x + 15, y + 9);
  fl_rect(x + 2, y + 9, 28, 19);
}

// Terminal (Nirvana Shell): a small monitor with a dark screen and a green ">_" prompt
static void terminal_icon(int x, int y, bool sel) {
  col(0xD6D6D6, sel);                       // case
  fl_rectf(x + 2, y + 3, 28, 22);
  col(0xF4F4F4, sel);
  fl_xyline(x + 3, y + 4, x + 28);
  fl_yxline(x + 3, y + 5, y + 23);
  col(0x9A9A9A, sel);
  fl_xyline(x + 3, y + 24, x + 29);
  fl_yxline(x + 29, y + 4, y + 24);
  col(0x0E1A12, sel);                       // screen
  fl_rectf(x + 5, y + 6, 22, 16);
  col(0x3CE06A, sel);                       // prompt
  fl_line_style(FL_SOLID, 2);
  fl_line(x + 8, y + 9, x + 12, y + 13);
  fl_line(x + 12, y + 13, x + 8, y + 17);
  fl_line_style(0);
  fl_rectf(x + 14, y + 17, 7, 2);
  col(0xB8B8B8, sel);                       // stand
  fl_polygon(x + 11, y + 25, x + 21, y + 25, x + 23, y + 29, x + 9, y + 29);
  fl_color(DARK);
  fl_rect(x + 2, y + 3, 28, 22);
  fl_rect(x + 5, y + 6, 22, 16);
  fl_loop(x + 11, y + 25, x + 21, y + 25, x + 23, y + 29, x + 9, y + 29);
}

// Applications: a folder with four colorful application tiles (2 x 2) on its front
static void applications_icon(int x, int y, bool sel) {
  folder(x, y, sel, false);
  static const unsigned tiles[4][2] = {   // face, highlight
    {0xD8433B, 0xF59A92}, {0x3E9A4A, 0x95D99C}, {0x3A6FC8, 0x9CC0F2}, {0xE8A21C, 0xFAD786}};
  for (int i = 0; i < 4; i++) {
    int tx = x + 9 + (i % 2) * 8, ty = y + 11 + (i / 2) * 8;
    col(tiles[i][0], sel);
    fl_rectf(tx, ty, 7, 7);
    col(tiles[i][1], sel);
    fl_xyline(tx + 1, ty + 1, tx + 4);     // gloss on the upper-left edge
    fl_yxline(tx + 1, ty + 2, ty + 3);
    fl_color(DARK);
    fl_rect(tx, ty, 7, 7);
  }
}

static void page(int x, int y, bool sel) {
  col(0xFFFFFF, sel);
  fl_polygon(x + 6, y + 2, x + 21, y + 2, x + 27, y + 8, x + 27, y + 30);
  fl_polygon(x + 6, y + 2, x + 27, y + 30, x + 6, y + 30);
  col(0xCCCCCC, sel);
  fl_polygon(x + 21, y + 2, x + 21, y + 8, x + 27, y + 8);
  fl_color(DARK);
  fl_begin_loop();
  fl_vertex(x + 6, y + 2); fl_vertex(x + 21, y + 2); fl_vertex(x + 27, y + 8);
  fl_vertex(x + 27, y + 30); fl_vertex(x + 6, y + 30);
  fl_end_loop();
  fl_line(x + 21, y + 2, x + 21, y + 8, x + 27, y + 8);
}

static void application(int x, int y, bool sel) {
  int cx = x + 16, cy = y + 16;
  col(0xB89CF0, sel);
  fl_polygon(cx, cy - 14, cx + 14, cy, cx, cy + 14, cx - 14, cy);
  col(0xE6DAFF, sel);
  fl_polygon(cx, cy - 8, cx + 8, cy, cx, cy + 8, cx - 8, cy);
  col(0x5B3FA0, sel);
  fl_line(cx - 4, cy + 4, cx + 5, cy - 5);   // a "pencil" on the diamond
  fl_line(cx - 3, cy + 4, cx + 5, cy - 4);
  fl_color(DARK);
  fl_loop(cx, cy - 14, cx + 14, cy, cx, cy + 14, cx - 14, cy);
}

static void home_disk(int x, int y, bool sel) {
  col(0xD8D8D8, sel);
  fl_rectf(x + 2, y + 10, 28, 15);
  col(0xFFFFFF, sel);
  fl_xyline(x + 3, y + 11, x + 28);
  col(0xA0A0A0, sel);
  fl_rectf(x + 3, y + 19, 26, 5);
  col(0x22CC44, sel);
  fl_rectf(x + 24, y + 20, 3, 2);
  // a small house on the case
  col(0x5B3FA0, sel);
  fl_polygon(x + 6, y + 15, x + 9, y + 12, x + 12, y + 15);
  fl_rectf(x + 7, y + 15, 4, 3);
  fl_color(DARK);
  fl_rect(x + 2, y + 10, 28, 15);
}

static void trash(int x, int y, bool sel, bool full) {
  if (full) {
    col(0xFFFFFF, sel);
    fl_polygon(x + 10, y + 9, x + 14, y + 1, x + 19, y + 3, x + 17, y + 9);
    fl_color(DARK);
    fl_loop(x + 10, y + 9, x + 14, y + 1, x + 19, y + 3, x + 17, y + 9);
  }
  col(0xCFCFCF, sel);
  fl_polygon(x + 8, y + 10, x + 24, y + 10, x + 23, y + 30, x + 9, y + 30);
  col(0x999999, sel);
  for (int i = 0; i < 4; i++) fl_line(x + 11 + i * 3, y + 13, x + 11 + i * 3, y + 27);
  fl_color(DARK);
  fl_loop(x + 8, y + 10, x + 24, y + 10, x + 23, y + 30, x + 9, y + 30);
  // the lid (ajar when the Trash is full)
  col(0xBDBDBD, sel);
  if (full) {
    fl_polygon(x + 6, y + 8, x + 25, y + 5, x + 26, y + 8, x + 7, y + 10);
    fl_color(DARK);
    fl_loop(x + 6, y + 8, x + 25, y + 5, x + 26, y + 8, x + 7, y + 10);
  } else {
    fl_rectf(x + 6, y + 6, 20, 4);
    fl_rectf(x + 13, y + 3, 6, 3);
    fl_color(DARK);
    fl_rect(x + 6, y + 6, 20, 4);
    fl_rect(x + 13, y + 3, 6, 3);
  }
}

// Internal disk: a gray case with an LED (like Nirvana HD, without the house)
static void hard_disk(int x, int y, bool sel) {
  col(0xD8D8D8, sel); fl_rectf(x + 2, y + 10, 28, 15);
  col(0xFFFFFF, sel); fl_xyline(x + 3, y + 11, x + 28);
  col(0xA0A0A0, sel); fl_rectf(x + 3, y + 19, 26, 5);
  col(0x22CC44, sel); fl_rectf(x + 24, y + 20, 3, 2);
  fl_color(DARK); fl_rect(x + 2, y + 10, 28, 15);
}

// USB stick: a body with a USB plug
static void removable(int x, int y, bool sel) {
  col(0xC0C0C0, sel); fl_rectf(x + 20, y + 12, 8, 9);              // plug
  col(0x707070, sel); fl_rectf(x + 23, y + 14, 2, 2); fl_rectf(x + 23, y + 18, 2, 2);
  col(0x5B3FA0, sel); fl_rectf(x + 3, y + 9, 18, 15);              // body
  col(0x8C74C8, sel); fl_xyline(x + 4, y + 10, x + 19);
  col(0xF0C040, sel); fl_rectf(x + 6, y + 14, 6, 5);               // label
  fl_color(DARK);
  fl_rect(x + 3, y + 9, 18, 15);
  fl_rect(x + 20, y + 12, 8, 9);
}

// CD/DVD: a silver disc with a hole and a rainbow sheen
static void optical(int x, int y, bool sel) {
  col(0xE4E4EC, sel); fl_pie(x + 3, y + 3, 26, 26, 0, 360);
  col(0xB8C8F0, sel); fl_pie(x + 3, y + 3, 26, 26, 20, 70);
  col(0xF0D0E8, sel); fl_pie(x + 3, y + 3, 26, 26, 200, 250);
  col(0xFFFFFF, sel); fl_pie(x + 11, y + 11, 10, 10, 0, 360);
  fl_color(DARK);
  fl_arc(x + 3, y + 3, 26, 26, 0, 360);
  fl_arc(x + 13, y + 13, 6, 6, 0, 360);
}

// Encrypted disk: a disk with a padlock
static void encrypted(int x, int y, bool sel) {
  hard_disk(x, y + 3, sel);
  col(0xF0C040, sel); fl_rectf(x + 18, y + 13, 11, 9);             // padlock body
  fl_color(DARK); fl_rect(x + 18, y + 13, 11, 9);
  fl_arc(x + 19, y + 6, 9, 12, 0, 180);                            // shackle
  fl_line(x + 19, y + 12, x + 19, y + 13);
  fl_line(x + 27, y + 12, x + 27, y + 13);
  fl_rectf(x + 23, y + 16, 1, 3);                                  // keyhole
}

// Eject symbol (the Trash while dragging a disk)
static void eject_icon(int x, int y, bool sel) {
  col(0x404040, sel);
  fl_polygon(x + 16, y + 5, x + 28, y + 19, x + 4, y + 19);
  fl_rectf(x + 4, y + 23, 25, 5);
}

// Network: a globe with a plug (Network panel, dialogs)
static void network_icon(int x, int y, bool sel) {
  col(0x3A78C8, sel);
  fl_pie(x + 4, y + 2, 24, 24, 0, 360);
  col(0x55A84A, sel);
  fl_begin_complex_polygon();
  for (auto [px, py] : {std::pair{9, 7}, {15, 5}, {16, 11}, {11, 15}, {8, 12}}) fl_vertex(x + px, y + py);
  fl_end_complex_polygon();
  fl_polygon(x + 17, y + 15, x + 24, y + 13, x + 23, y + 21, x + 18, y + 23);
  col(0x1E3E6A, sel);
  fl_arc(x + 4, y + 2, 24, 24, 0, 360);
  fl_arc(x + 10, y + 2, 12, 24, 0, 360);
  fl_xyline(x + 5, y + 14, x + 27);
  // cable with a plug
  col(0x404040, sel);
  fl_line_style(FL_SOLID, 2);
  fl_line(x + 16, y + 26, x + 16, y + 29);
  fl_line_style(0);
  fl_rectf(x + 12, y + 28, 9, 4);
}

// Preferences: a control panel window with three sliders
static void preferences_icon(int x, int y, bool sel) {
  col(0xD8D8D8, sel);
  fl_rectf(x + 3, y + 4, 26, 24);
  col(0x6A5AB8, sel);
  fl_rectf(x + 3, y + 4, 26, 5);            // title bar
  col(0xFFFFFF, sel);
  fl_rectf(x + 5, y + 6, 2, 1);
  for (int i = 0; i < 3; i++) {
    int ly = y + 13 + i * 5, kx = x + 8 + (i == 0 ? 12 : i == 1 ? 4 : 9);
    col(0x606060, sel);
    fl_xyline(x + 7, ly, x + 25);           // slider groove
    col(0x3A78C8 + i * 0x301000, sel);
    fl_rectf(kx, ly - 2, 5, 5);             // knob
    fl_color(DARK);
    fl_rect(kx, ly - 2, 5, 5);
  }
  fl_color(DARK);
  fl_rect(x + 3, y + 4, 26, 24);
  fl_xyline(x + 3, y + 9, x + 28);
}

static void wifi_icon(int x, int y, bool sel) {
  Fl_Color c = sel ? fl_color_average(fl_rgb_color(0x30, 0x30, 0x30), FL_BLACK, 0.5f) : fl_rgb_color(0x30, 0x30, 0x30);
  fl_color(c);
  fl_line_style(FL_SOLID | FL_CAP_ROUND, 3);
  for (int i = 1; i <= 3; i++) {
    int r = i * 8;
    fl_arc(x + 16 - r, y + 26 - r, 2 * r, 2 * r, 45, 135);
  }
  fl_line_style(0);
  fl_pie(x + 13, y + 23, 6, 6, 0, 360);
}

void draw_wifi(int x, int y, int level, Fl_Color on, Fl_Color off) {
  // a dot and three arcs spreading upwards (16x13)
  int cx = x + 8, by = y + 12;
  fl_color(level >= 1 ? on : off);
  fl_pie(cx - 1, by - 2, 3, 3, 0, 360);
  fl_line_style(FL_SOLID, 2);
  for (int i = 1; i <= 3; i++) {
    fl_color(level >= i + 1 ? on : off);
    int r = i * 4;
    fl_arc(cx - r, by - r, 2 * r + 1, 2 * r + 1, 45, 135);
  }
  fl_line_style(0);
  if (level < 0) {   // off: a diagonal line
    fl_color(on);
    fl_line_style(FL_SOLID, 2);
    fl_line(x + 2, y + 1, x + 14, y + 12);
    fl_line_style(0);
  }
}

void draw_wired(int x, int y, Fl_Color c) {
  // three connected squares - the classic network symbol
  fl_color(c);
  fl_rectf(x + 5, y, 6, 5);
  fl_rectf(x, y + 8, 6, 5);
  fl_rectf(x + 10, y + 8, 6, 5);
  fl_xyline(x + 3, y + 6, x + 13);
  fl_yxline(x + 8, y + 5, y + 6);
  fl_yxline(x + 3, y + 6, y + 8);
  fl_yxline(x + 13, y + 6, y + 8);
}

void draw_signal(int x, int y, int level, Fl_Color on, Fl_Color off) {
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 2 + (i == 3);
    fl_color(level > i ? on : off);
    fl_rectf(x + i * 3, y + 10 - h, 2, h);
  }
}

void draw_speaker(int x, int y, int level, bool muted, Fl_Color c, double sc) {
  fl_push_matrix();
  fl_translate(x, y);
  fl_scale(sc);
  fl_color(c);
  // body and cone
  fl_begin_polygon();
  fl_vertex(0, 4); fl_vertex(3, 4); fl_vertex(7, 0.5); fl_vertex(7, 12.5); fl_vertex(3, 9); fl_vertex(0, 9);
  fl_end_polygon();
  fl_line_style(FL_SOLID, std::max(1, (int)(1.5 * sc + 0.5)));
  if (muted) {
    fl_begin_line(); fl_vertex(9.5, 4); fl_vertex(14.5, 9); fl_end_line();
    fl_begin_line(); fl_vertex(14.5, 4); fl_vertex(9.5, 9); fl_end_line();
  } else {
    for (int i = 1; i <= level && i <= 3; i++) {
      double r = 2.5 * i;
      fl_begin_line();
      fl_arc(7.5, 6.5, r, -50, 50);
      fl_end_line();
    }
  }
  fl_line_style(0);
  fl_pop_matrix();
}

void draw_battery(int x, int y, int pct, bool charging, Fl_Color c, double sc) {
  fl_push_matrix();
  fl_translate(x, y);
  fl_scale(sc);
  fl_color(c);
  fl_begin_loop(); fl_vertex(0.5, 0.5); fl_vertex(19.5, 0.5); fl_vertex(19.5, 10.5); fl_vertex(0.5, 10.5); fl_end_loop();
  fl_begin_polygon(); fl_vertex(20, 3); fl_vertex(22, 3); fl_vertex(22, 8); fl_vertex(20, 8); fl_end_polygon();
  double fw = 17.0 * std::clamp(pct, 0, 100) / 100.0;
  if (fw > 0) {
    fl_color(pct <= 10 && !charging ? fl_rgb_color(0xC0, 0x20, 0x20) : c);
    fl_begin_polygon(); fl_vertex(2, 2); fl_vertex(2 + fw, 2); fl_vertex(2 + fw, 9); fl_vertex(2, 9); fl_end_polygon();
  }
  if (charging) {   // a lightning bolt in the bar background color on the fill
    fl_color(GRAY);
    fl_begin_polygon(); fl_vertex(11, 1); fl_vertex(6.5, 6); fl_vertex(9.5, 6); fl_vertex(8, 10); fl_vertex(13, 4.5); fl_vertex(10, 4.5); fl_end_polygon();
    fl_color(c);
    fl_begin_loop(); fl_vertex(11, 1); fl_vertex(6.5, 6); fl_vertex(9.5, 6); fl_vertex(8, 10); fl_vertex(13, 4.5); fl_vertex(10, 4.5); fl_end_loop();
  }
  fl_pop_matrix();
}

void draw_plug(int x, int y, Fl_Color c) {
  fl_color(c);
  fl_rectf(x + 3, y, 2, 4);
  fl_rectf(x + 9, y, 2, 4);
  fl_rectf(x + 1, y + 4, 12, 4);
  fl_rectf(x + 3, y + 8, 8, 2);
  fl_rectf(x + 6, y + 10, 2, 3);
}

void draw_sun(int x, int y, Fl_Color c, double sc) {
  fl_push_matrix();
  fl_translate(x, y);
  fl_scale(sc);
  fl_color(c);
  fl_begin_polygon(); fl_circle(6.5, 6.5, 3); fl_end_polygon();
  fl_line_style(FL_SOLID, std::max(1, (int)(sc + 0.5)));
  for (int i = 0; i < 8; i++) {
    double a = i * M_PI / 4;
    fl_begin_line(); fl_vertex(6.5 + 4.5 * cos(a), 6.5 + 4.5 * sin(a)); fl_vertex(6.5 + 6.3 * cos(a), 6.5 + 6.3 * sin(a)); fl_end_line();
  }
  fl_line_style(0);
  fl_pop_matrix();
}

void draw_display(int x, int y, Fl_Color c) {
  fl_color(c);
  fl_rect(x, y, 11, 8);
  fl_rectf(x + 2, y + 2, 7, 4);
  fl_xyline(x + 3, y + 9, x + 7);
  fl_yxline(x + 5, y + 8, y + 9);
}

void draw_depth_gadget(int x, int y, bool pressed) {
  Fl_Color line = pressed ? SEL_TEXT : TEXT;
  Fl_Color back = pressed ? SEL : GRAY;
  Fl_Color front = pressed ? fl_color_average(SEL_TEXT, SEL, 0.35f) : LIGHT;
  // back: top left, outline only
  fl_color(back);
  fl_rectf(x, y, 12, 9);
  fl_color(line);
  fl_rect(x, y, 12, 9);
  // front: bottom right, filled, covers the corner of the back one
  fl_color(front);
  fl_rectf(x + 6, y + 5, 12, 9);
  fl_color(line);
  fl_rect(x + 6, y + 5, 12, 9);
}

void draw_lock(int x, int y, Fl_Color c) {
  fl_color(c);
  fl_rectf(x, y + 4, 7, 5);
  fl_xyline(x + 2, y, x + 4);
  fl_yxline(x + 1, y + 1, y + 3);
  fl_yxline(x + 5, y + 1, y + 3);
}

// ---------------------------------------------------------------------------
// Category emblems: small 14x14 pictures with a black outline, drawn on a folder.
// ---------------------------------------------------------------------------
void draw_category_emblem(int cat, int x, int y, bool sel, int style) {
  auto c = [&](unsigned v) {
    Fl_Color k = rgb(v);
    if (sel) k = style == 0 ? rgb(0xFFFFFF ^ v) : fl_color_average(k, FL_BLACK, 0.5f);
    fl_color(k);
  };
  const unsigned K = 0x000000, W = 0xFFFFFF;
  auto box = [&](int bx, int by, int bw, int bh, unsigned fill) {   // a filled box with an outline
    c(K); fl_rectf(bx, by, bw, bh);
    c(fill); fl_rectf(bx + 1, by + 1, bw - 2, bh - 2);
  };
  auto disc = [&](int dx, int dy, int d, unsigned fill) {
    c(K); fl_pie(dx, dy, d, d, 0, 360);
    c(fill); fl_pie(dx + 1, dy + 1, d - 2, d - 2, 0, 360);
  };
  switch (cat) {
  case 0: {   // Games: a gamepad with a cross and two buttons
    c(K);
    fl_pie(x, y + 4, 7, 9, 0, 360); fl_pie(x + 7, y + 4, 7, 9, 0, 360); fl_rectf(x + 3, y + 3, 8, 7);
    c(0x4A4A60);
    fl_pie(x + 1, y + 5, 5, 7, 0, 360); fl_pie(x + 8, y + 5, 5, 7, 0, 360); fl_rectf(x + 3, y + 4, 8, 5);
    c(W); fl_rectf(x + 2, y + 6, 5, 1); fl_rectf(x + 4, y + 4, 1, 5);
    c(0xE5483F); fl_rectf(x + 10, y + 4, 2, 2);
    c(0x3DB54A); fl_rectf(x + 8, y + 6, 2, 2);
    break;
  }
  case 1: {   // Internet: a globe
    disc(x, y, 14, 0x3A7BD5);
    c(0x58C05A);
    fl_polygon(x + 3, y + 3, x + 7, y + 2, x + 7, y + 6, x + 3, y + 7);
    fl_polygon(x + 8, y + 7, x + 11, y + 6, x + 12, y + 10, x + 8, y + 12);
    c(0xA9CCF5); fl_arc(x + 2, y + 2, 10, 10, 100, 170);
    break;
  }
  case 2: {   // Office: a page with lines and a pencil
    box(x + 1, y, 10, 14, W);
    c(0x5A7FC8); for (int i = 0; i < 4; i++) fl_xyline(x + 3, y + 3 + i * 2, x + 8);
    c(K); fl_line_style(FL_SOLID, 4); fl_line(x + 13, y + 5, x + 7, y + 11); fl_line_style(0);
    c(0xF2C230); fl_line_style(FL_SOLID, 2); fl_line(x + 12, y + 6, x + 8, y + 10); fl_line_style(0);
    c(K); fl_rectf(x + 6, y + 11, 2, 2);
    break;
  }
  case 3: {   // Graphics: a palette with paint blobs
    c(K); fl_pie(x, y + 1, 14, 12, 0, 360);
    c(0xE6C690); fl_pie(x + 1, y + 2, 12, 10, 0, 360);
    c(K); fl_pie(x + 3, y + 7, 4, 4, 0, 360);
    c(0xE5483F); fl_rectf(x + 4, y + 3, 2, 2);
    c(0xF2C230); fl_rectf(x + 7, y + 3, 2, 2);
    c(0x3A7BD5); fl_rectf(x + 10, y + 5, 2, 2);
    c(0x3DB54A); fl_rectf(x + 8, y + 8, 2, 2);
    break;
  }
  case 4: {   // Multimedia: a film frame with a play button
    box(x, y + 1, 14, 12, 0x2A2A30);
    c(W);
    for (int i = 0; i < 6; i++) { fl_point(x + 2 + i * 2, y + 2); fl_point(x + 2 + i * 2, y + 11); }
    c(0xFFB020); fl_polygon(x + 5, y + 4, x + 5, y + 10, x + 10, y + 7);
    break;
  }
  case 5: {   // Development: a window with </>
    box(x, y + 1, 14, 12, 0x1E2A3A);
    c(0x7E9DD0); fl_rectf(x + 1, y + 2, 12, 2);
    c(0x5CE07A);
    fl_line(x + 5, y + 6, x + 3, y + 8); fl_line(x + 3, y + 8, x + 5, y + 10);
    fl_line(x + 9, y + 6, x + 11, y + 8); fl_line(x + 11, y + 8, x + 9, y + 10);
    c(W); fl_line(x + 8, y + 5, x + 6, y + 11);
    break;
  }
  case 6: {   // Education: a graduation cap with a tassel
    c(K); fl_polygon(x - 1, y + 5, x + 7, y + 1, x + 15, y + 5, x + 7, y + 9);
    c(0x34344A); fl_polygon(x + 1, y + 5, x + 7, y + 2, x + 13, y + 5, x + 7, y + 8);
    box(x + 3, y + 7, 8, 5, 0x34344A);
    c(0xF2C230); fl_line(x + 7, y + 5, x + 12, y + 6); fl_yxline(x + 12, y + 6, y + 11);
    fl_rectf(x + 11, y + 11, 3, 2);
    break;
  }
  case 7: {   // Science: a flask with a green liquid
    auto poly = [](std::initializer_list<int> v) {
      fl_begin_polygon();
      for (auto p = v.begin(); p != v.end(); p += 2) fl_vertex(p[0], p[1]);
      fl_end_polygon();
    };
    c(K); poly({x + 4, y, x + 10, y, x + 10, y + 5, x + 14, y + 14, x, y + 14, x + 4, y + 5});
    c(0xE6F2FF); poly({x + 5, y + 1, x + 9, y + 1, x + 9, y + 6, x + 12, y + 13, x + 2, y + 13, x + 5, y + 6});
    c(0x5CC85C); fl_polygon(x + 3, y + 9, x + 11, y + 9, x + 12, y + 13, x + 2, y + 13);
    c(W); fl_point(x + 6, y + 11); fl_point(x + 9, y + 10);
    break;
  }
  case 8: {   // System: a gear
    c(K); fl_line_style(FL_SOLID, 4);
    fl_line(x + 7, y, x + 7, y + 14); fl_line(x, y + 7, x + 14, y + 7);
    fl_line(x + 2, y + 2, x + 12, y + 12); fl_line(x + 12, y + 2, x + 2, y + 12);
    fl_line_style(0);
    disc(x + 2, y + 2, 10, 0xA6A6B0);
    c(0xA6A6B0); fl_line_style(FL_SOLID, 2);
    fl_line(x + 7, y + 1, x + 7, y + 13); fl_line(x + 1, y + 7, x + 13, y + 7);
    fl_line(x + 3, y + 3, x + 11, y + 11); fl_line(x + 11, y + 3, x + 3, y + 11);
    fl_line_style(0);
    disc(x + 5, y + 5, 4, 0x4A4A55);
    break;
  }
  case 9: {   // Accessories: a calculator
    box(x + 2, y, 10, 14, 0xC8C8C0);
    box(x + 3, y + 1, 8, 4, 0x9CC09C);
    for (int j = 0; j < 3; j++)
      for (int i = 0; i < 3; i++) {
        c(i == 2 && j == 2 ? 0xF29A26 : 0x55555F);
        fl_rectf(x + 4 + i * 2 + i, y + 6 + j * 2 + j / 2, 2, 2);
      }
    break;
  }
  default: {   // Other: a cardboard box with tape
    box(x + 1, y + 4, 12, 10, 0xC69858);
    box(x, y + 2, 14, 4, 0xDDB070);
    c(0xF0E2B6); fl_rectf(x + 6, y + 3, 2, 10);
    break;
  }
  }
}

int icon_style() { return current_appearance().icons == "platinum" ? 1 : 0; }

void draw_icon(Icon icon, int x, int y, bool sel) {
  if (icon_style() == 0 && draw_icon_retro(icon, x, y, sel)) return;
  switch (icon) {
  case Icon::Folder:      folder(x, y, sel, false); break;
  case Icon::OpenFolder:  folder(x, y, sel, true); break;
  case Icon::Document:
    page(x, y, sel);
    col(0x9A9A9A, sel);
    for (int yy = y + 12; yy < y + 27; yy += 3) fl_xyline(x + 10, yy, x + 23);
    break;
  case Icon::Image:
    page(x, y, sel);
    col(0x88BBEE, sel); fl_rectf(x + 9, y + 12, 15, 13);
    col(0x44AA55, sel);
    fl_begin_polygon();
    fl_vertex(x + 9, y + 25); fl_vertex(x + 14, y + 17); fl_vertex(x + 18, y + 21);
    fl_vertex(x + 21, y + 18); fl_vertex(x + 24, y + 25);
    fl_end_polygon();
    col(0xFFDD33, sel); fl_pie(x + 18, y + 13, 4, 4, 0, 360);
    fl_color(DARK); fl_rect(x + 9, y + 12, 15, 13);
    break;
  case Icon::Music:
    page(x, y, sel);
    fl_color(DARK);
    fl_pie(x + 10, y + 21, 5, 4, 0, 360);
    fl_pie(x + 18, y + 19, 5, 4, 0, 360);
    fl_yxline(x + 14, y + 12, y + 22);
    fl_yxline(x + 22, y + 10, y + 20);
    fl_line(x + 14, y + 12, x + 22, y + 10);
    fl_line(x + 14, y + 13, x + 22, y + 11);
    break;
  case Icon::Application: application(x, y, sel); break;
  case Icon::Home:        home_disk(x, y, sel); break;
  case Icon::Trash:       trash(x, y, sel, false); break;
  case Icon::TrashFull:   trash(x, y, sel, true); break;
  case Icon::HardDisk:    hard_disk(x, y, sel); break;
  case Icon::Removable:   removable(x, y, sel); break;
  case Icon::Optical:     optical(x, y, sel); break;
  case Icon::Encrypted:   encrypted(x, y, sel); break;
  case Icon::Eject:       eject_icon(x, y, sel); break;
  case Icon::Network:     network_icon(x, y, sel); break;
  case Icon::Wifi:        wifi_icon(x, y, sel); break;
  case Icon::Preferences: preferences_icon(x, y, sel); break;
  case Icon::Applications: applications_icon(x, y, sel); break;
  case Icon::Terminal:    terminal_icon(x, y, sel); break;
  default:
    if (icon >= Icon::CatGames && icon <= Icon::CatOther) {   // a folder with the category emblem
      folder(x, y, sel, false);
      draw_category_emblem((int)icon - (int)Icon::CatGames, x + 9, y + 12, sel, 1);
    }
    break;
  }
}

// The Nirvana Desktop logo: a DIP chip (a nod to the custom chips of old home computers) with the letter "N".
// Drawn for 32 px; larger sizes scale the strokes and get more pins.
std::string guru_number(const std::string &m) {
  unsigned h = 2166136261u;   // FNV-1a
  for (unsigned char c : m) h = (h ^ c) * 16777619u;
  char b[32];
  snprintf(b, sizeof b, "#%08X.%08X", (unsigned)(m.size() % 16 + 1), h);
  return b;
}

void guru_alert(const std::string &m) {
  fl_message_title(GURU_TITLE);
  fl_alert("%s\n\n%s %s", m.c_str(), GURU_TITLE, guru_number(m).c_str());
}

// The Nirvana Desktop logo: a chip seen from the side (a golden rim, a green board with
// traces, pins), the silver N on it, rays of light and sparkles on a night-blue screen in a
// light frame. Drawn at any size; small sizes leave out the details. Fixed colors (not the scheme).
void draw_logo(int x, int y, int s) {
  const double u = s / 64.0;
  auto X = [&](double v) { return x + v * u; };
  auto Y = [&](double v) { return y + v * u; };
  auto quad = [&](double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4) {
    fl_begin_polygon();
    fl_vertex(X(x1), Y(y1)); fl_vertex(X(x2), Y(y2)); fl_vertex(X(x3), Y(y3)); fl_vertex(X(x4), Y(y4));
    fl_end_polygon();
  };
  auto tri = [&](double x1, double y1, double x2, double y2, double x3, double y3) {
    fl_begin_polygon();
    fl_vertex(X(x1), Y(y1)); fl_vertex(X(x2), Y(y2)); fl_vertex(X(x3), Y(y3));
    fl_end_polygon();
  };
  bool big = s >= 32, huge = s >= 96;
  // 1. the frame and the night-blue screen
  double m = big ? 4 : 0;   // the frame width
  if (big) {
    fl_color(rgb(0xB4AE9E)); fl_rectf(x, y, s, s);
    fl_color(rgb(0xE6E1D3)); fl_xyline(x, y, x + s - 1); fl_yxline(x, y, y + s - 1);
    fl_color(rgb(0x5C574D)); fl_xyline(x, y + s - 1, x + s - 1); fl_yxline(x + s - 1, y, y + s - 1);
  }
  int sx = (int)X(m), sy = (int)Y(m), sw = s - 2 * (int)(m * u), sh = sw;
  for (int i = 0; i < sh; i++) {
    fl_color(fl_color_average(rgb(0x2A1D58), rgb(0x16285E), sh > 1 ? (float)i / (sh - 1) : 0));
    fl_xyline(sx, sy + i, sx + sw - 1);
  }
  fl_push_clip(sx, sy, sw, sh);
  // 2. rays of light from behind the chip
  if (big) {
    const unsigned ray[] = {0x2E56A8, 0x1F8C9C, 0x4B3C9E, 0x2B6CC0, 0x21A5A8, 0x5A3AA0};
    const double ang[] = {200, 228, 250, 285, 310, 330, 20, 48, 75, 110, 135, 160};
    for (int i = 0; i < 12; i++) {
      double a = ang[i] * M_PI / 180, w = (i % 3 == 0 ? 7 : 4) * M_PI / 180;
      fl_color(fl_color_average(rgb(ray[i % 6]), rgb(0x1E2258), 0.6f));
      tri(32, 32, 32 + 60 * cos(a - w), 32 + 60 * sin(a - w), 32 + 60 * cos(a + w), 32 + 60 * sin(a + w));
    }
    if (huge) {   // a bright streak across the top right
      fl_color(rgb(0x58D2D8));
      quad(40, 4, 46, 4, 30, 34, 27, 34);
    }
  }
  // 3. the pins under the two lower edges
  if (s >= 20) {
    int n = s >= 64 ? 9 : s >= 40 ? 6 : 4;
    for (int side = 0; side < 2; side++)
      for (int i = 0; i < n; i++) {
        double t = (i + 0.7) / (n + 0.4);
        double px = side ? 32 + 26 * t : 6 + 26 * t, py = side ? 48 - 16 * t : 32 + 16 * t;
        fl_color(rgb(0x5F594C));
        fl_rectf((int)X(px), (int)Y(py), std::max(1, (int)(1.6 * u)), std::max(2, (int)(6 * u)));
        fl_color(rgb(0xD8D1BC));
        fl_rectf((int)X(px), (int)Y(py), std::max(1, (int)(1.0 * u)), std::max(2, (int)(5 * u)));
      }
  }
  // 4. the chip: the golden rim, a darker edge and the green board
  fl_color(rgb(0x7A5A12));
  quad(4, 32, 32, 15, 60, 32, 32, 50);
  fl_color(rgb(0xE8B93A));
  quad(5, 31, 32, 14, 59, 31, 32, 48);
  fl_color(rgb(0xFFE38A));
  if (big) { fl_line(X(6), Y(31), X(32), Y(15)); fl_line(X(32), Y(15), X(58), Y(31)); }
  fl_color(rgb(0x2E8A4E));
  quad(10, 31, 32, 17.5, 54, 31, 32, 44.5);
  if (s >= 48) {   // golden traces on the board
    fl_color(rgb(0xD6B24A));
    fl_line_style(FL_SOLID, std::max(1, (int)(0.6 * u)));
    const double tr[][4] = {{32, 19, 32, 24}, {24, 24, 28, 27}, {40, 24, 36, 27}, {16, 31, 22, 31},
                            {42, 31, 48, 31}, {24, 38, 28, 35}, {40, 38, 36, 35}, {32, 43, 32, 39}};
    for (auto &t : tr) fl_line(X(t[0]), Y(t[1]), X(t[2]), Y(t[3]));
    fl_line_style(0);
    for (auto &t : tr) fl_rectf((int)X(t[0]) - (int)u / 2, (int)Y(t[1]) - (int)u / 2, std::max(1, (int)u), std::max(1, (int)u));
  }
  // 5. the letters: silver with a violet shadow
  auto text = [&](const char *t, double cx, double cy, double size, Fl_Color face) {
    fl_font(FL_HELVETICA_BOLD, std::max(6, (int)(size * u)));
    int tw = (int)fl_width(t), th = fl_height(), d = fl_descent();
    int tx = (int)X(cx) - tw / 2, ty = (int)Y(cy) + th / 2 - d;
    int sh2 = std::max(1, (int)(1.2 * u));
    fl_color(rgb(0x5A2A88));
    fl_draw(t, tx + sh2, ty + sh2);
    fl_color(rgb(0x2A1840));
    fl_draw(t, tx + 1, ty);
    fl_color(face);
    fl_draw(t, tx, ty);
  };
  if (huge) {
    text("NIRVANA", 32, 29.5, 9.6, rgb(0xEEE8D8));
    // the ribbon with DESKTOP
    fl_color(rgb(0xBDB6A2));
    quad(18, 36.5, 46, 36.5, 46, 41.5, 18, 41.5);
    tri(18, 36.5, 15, 39, 18, 41.5);
    tri(46, 36.5, 49, 39, 46, 41.5);
    fl_color(rgb(0x3A3446));
    fl_font(FL_HELVETICA_BOLD, std::max(6, (int)(3.6 * u)));
    fl_draw("DESKTOP", (int)X(18), (int)Y(36.5), (int)(28 * u), (int)(5 * u), FL_ALIGN_CENTER, nullptr, 0);
  } else {
    text("N", 32, 31.5, s >= 32 ? 21 : 26, rgb(0xF2EEE2));
  }
  // 6. sparkles
  if (big) {
    auto spark = [&](double cx, double cy, double r, unsigned c) {
      fl_color(rgb(c));
      quad(cx - r, cy, cx, cy - r * 0.22, cx + r, cy, cx, cy + r * 0.22);
      quad(cx, cy - r, cx + r * 0.22, cy, cx, cy + r, cx - r * 0.22, cy);
    };
    spark(50, 11, 5, 0xFFD45A);
    spark(13, 13, 3, 0x7FF0E0);
    spark(56, 22, 2.6, 0xFFD45A);
    spark(41, 7, 2, 0xD98CFF);
    if (huge) { spark(20, 8, 2.2, 0xFFD45A); spark(54, 44, 2.4, 0x7FF0E0); spark(9, 50, 2, 0xD98CFF); }
  }
  fl_pop_clip();
  if (big) {   // the inner edge of the frame
    fl_color(rgb(0x2A2833));
    fl_rect(sx - 1, sy - 1, sw + 2, sh + 2);
  }
}

// ---------------------------------------------------------------------------
// Window chrome
// ---------------------------------------------------------------------------
// Title bar buttons in the scheme colors, drawn boldly so they stand out on any
// bar (an echo of the old home-computer window gadgets): a contrasting
// rim, a deep bevel and solid glyphs filled with the accent color. The colors
// follow the scheme, but are pushed apart when it is too low-contrast (dark
// schemes). Inactive windows keep the buttons, only the glyph fill turns gray.
static double luma(Fl_Color c) {
  uchar r, g, b;
  Fl::get_color(c, r, g, b);
  return (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
}

static void title_button(int x, int y, int kind, bool pressed, bool active) {
  const int B = BOX;
  Fl_Color face = GRAY;
  bool dark = luma(face) < 0.4;
  // the rim and the ink of the glyphs must stand out from the face
  Fl_Color rim = dark ? fl_color_average(TEXT, face, 0.8) : DARK;
  Fl_Color ink = dark ? fl_color_average(FL_WHITE, TEXT, 0.4) : DARK;
  Fl_Color hi = dark ? fl_color_average(FL_WHITE, face, 0.35) : LIGHT;
  Fl_Color lo = dark ? fl_color_average(FL_BLACK, face, 0.6) : SHADOW;
  Fl_Color fill = active ? HILITE : fl_color_average(face, ink, 0.5);
  if (active && std::fabs(luma(fill) - luma(face)) < (dark ? 0.25 : 0.1))
    fill = fl_color_average(dark ? SEL : HILITE, dark ? FL_WHITE : FL_BLACK, dark ? 0.5 : 0.65);
  if (active && dark && std::fabs(luma(fill) - luma(face)) < 0.25)
    fill = fl_color_average(fill, FL_WHITE, 0.4);

  fl_color(rim);
  fl_rect(x, y, B, B);
  fl_color(pressed ? fl_color_average(face, lo, 0.5) : face);
  fl_rectf(x + 1, y + 1, B - 2, B - 2);
  bevel(x + 1, y + 1, B - 2, B - 2, pressed ? lo : hi, pressed ? hi : lo);
  int o = pressed ? 1 : 0;   // a pressed glyph sinks by a pixel
  auto glyph = [&](int gx, int gy, int gw, int gh) {
    fl_color(fill);
    fl_rectf(gx + o, gy + o, gw, gh);
    fl_color(ink);
    fl_rect(gx + o, gy + o, gw, gh);
  };
  if (kind == 1) {                        // close: a solid block in the middle
    glyph(x + 3, y + 3, B - 6, B - 6);
  } else if (kind == 2) {                 // zoom: a large frame with a solid corner
    fl_color(ink);
    fl_rect(x + 3 + o, y + 3 + o, B - 6, B - 6);
    glyph(x + 3, y + 3, B / 2 - 1, B / 2 - 1);
  } else if (kind == 3) {                 // iconify: the window dropped to a bar at the bottom
    fl_color(ink);
    fl_xyline(x + 3 + o, y + 3 + o, x + B - 4 + o);   // the trace of the window above
    glyph(x + 3, y + B - 7, B - 6, 4);
  }
}

static unsigned mix_hex(unsigned a, unsigned b, double t) {
  t = std::clamp(t, 0.0, 1.0);
  auto ch = [&](int sh) { return (unsigned)(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t + 0.5) << sh; };
  return ch(16) | ch(8) | ch(0);
}

void draw_titlebar(int x, int y, int w, const char *title, bool active, int pressed, int boxes) {
  int by = y + (TITLE_H - BOX) / 2;
  int close_x = x + 8;
  int zoom_x = x + w - 8 - BOX;
  int shade_x = boxes == 2 ? zoom_x : zoom_x - BOX - 5;   // without "zoom": iconify in the corner

  fl_font(FONT_SYSTEM, SIZE_SYSTEM);
  std::string t = ellipsize(title ? title : "", w - 2 * (BOX * 2 + 30));
  int tw = (int)fl_width(t.c_str());
  int tx = x + (w - tw) / 2;

  TitleStyle st = title_style();
  if (st != TitleStyle::Stripes) {
    // A colored bar (Redmond97 style schemes): solid or gradient.
    const Scheme &sc = current_scheme();
    unsigned c1 = active ? sc.title : sc.title_i, c2 = active ? sc.title2 : sc.title_i2;
    int fx = x + 3, fw = w - 6, fy = y + 3, fh = TITLE_H - 5;
    if (st == TitleStyle::Glossy) {
      // glass: a light upper half fading down, the darker middle, a little light at the bottom
      for (int i = 0; i < fh; i++) {
        double t = fh > 1 ? (double)i / (fh - 1) : 0;
        unsigned c = t < 0.5 ? mix_hex(c1, 0xFFFFFF, 0.42 - 0.6 * t) : mix_hex(c2, c1, (t - 0.5) * 1.6);
        fl_color(fl_rgb_color(c >> 16, (c >> 8) & 0xFF, c & 0xFF));
        fl_xyline(fx, fy + i, fx + fw - 1);
      }
      unsigned hl = mix_hex(c1, 0xFFFFFF, 0.65);   // the shine on the top edge
      fl_color(fl_rgb_color(hl >> 16, (hl >> 8) & 0xFF, hl & 0xFF));
      fl_xyline(fx + 1, fy, fx + fw - 2);
    } else if (st == TitleStyle::Solid || c1 == c2) {
      fl_color(fl_rgb_color(c1 >> 16, (c1 >> 8) & 0xFF, c1 & 0xFF));
      fl_rectf(fx, fy, fw, fh);
    } else {
      for (int i = 0; i < fw; i++) {
        double t = fw > 1 ? (double)i / (fw - 1) : 0;
        auto ch = [&](int sh) { return (uchar)(((c1 >> sh) & 0xFF) * (1 - t) + ((c2 >> sh) & 0xFF) * t + 0.5); };
        fl_color(fl_rgb_color(ch(16), ch(8), ch(0)));
        fl_yxline(fx + i, fy, fy + fh - 1);
      }
    }
    if (boxes) {   // the buttons also in inactive windows
      title_button(close_x, by, 1, pressed == 1, active);
      title_button(shade_x, by, 3, pressed == 3, active);
      if (boxes != 2) title_button(zoom_x, by, 2, pressed == 2, active);
    }
  } else {
    // Stripes: a dark line above a light one, interrupted at the title and the buttons.
    auto stripe = [&](int x0, int x1) {
      if (x1 <= x0 || !active) return;   // an inactive window has a plain bar, but keeps its buttons
      for (int yy = y + 4; yy <= y + TITLE_H - 6; yy += 2) {
        fl_color(STRIPE); fl_xyline(x0, yy, x1);
        fl_color(LIGHT); fl_xyline(x0, yy + 1, x1);
      }
    };
    if (!boxes) {
      stripe(x + 3, tx - 8);
      stripe(tx + tw + 8, x + w - 4);
    } else {
      stripe(close_x + BOX + 5, tx - 8);
      stripe(tx + tw + 8, shade_x - 6);
      stripe(x + 3, close_x - 4);
      title_button(close_x, by, 1, pressed == 1, active);
      title_button(shade_x, by, 3, pressed == 3, active);
      if (boxes != 2) {
        stripe(zoom_x + BOX + 4, x + w - 4);
        title_button(zoom_x, by, 2, pressed == 2, active);
      } else {
        stripe(shade_x + BOX + 4, x + w - 4);
      }
    }
  }
  fl_color(active ? TITLE_TEXT : TITLE_I_TEXT);
  fl_draw(t.c_str(), tx, y + TITLE_H / 2 + fl_height() / 2 - fl_descent());
}

void draw_frame(int x, int y, int w, int h, bool active, bool shaded) {
  fl_color(GRAY);
  fl_rectf(x, y, w, h);
  if (active) bevel(x + 1, y + 1, w - 2, h - 2, LIGHT, MID);
  fl_color(active ? DARK : SHADOW);
  fl_rect(x, y, w, h);
  // window shadow (right and bottom edge), as in Platinum
  if (!shaded) {
    fl_color(SHADOW);
    fl_xyline(x + 2, y + h, x + w);
    fl_yxline(x + w, y + 2, y + h);
  }
}

void draw_grow_box(int x, int y) {
  // 15x15, two overlapping frames - the resize handle
  fl_color(GRAY);
  fl_rectf(x, y, 15, 15);
  fl_color(SHADOW);
  fl_rect(x + 3, y + 3, 7, 7);
  fl_rect(x + 5, y + 5, 7, 7);
  fl_color(LIGHT);
  fl_xyline(x + 6, y + 6, x + 10);
  fl_yxline(x + 6, y + 6, y + 10);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
std::string ellipsize(const std::string &s, int max_w) {
  if (max_w <= 0) return "";
  if (fl_width(s.c_str()) <= max_w) return s;
  std::string t = s;
  const char *ell = "\xE2\x80\xA6";  // "…"
  while (!t.empty()) {
    // remove the last UTF-8 character
    const char *b = t.c_str();
    const char *e = b + t.size();
    const char *p = fl_utf8back(e - 1, b, e);
    t.resize(p - b);
    if (fl_width((t + ell).c_str()) <= max_w) return t + ell;
  }
  return ell;
}

std::string items_text(int n) {
  return fmt(trn(n, "%1 item", "%1 items"), std::to_string(n));
}

// The FLTK file chooser in the interface language (Network: VPN import, Appearance: wallpaper).
void localize_file_chooser() {
  fl_cancel = _("Cancel");
  fl_ok = _("Choose");
  Fl_File_Chooser::add_favorites_label = _("Add to Favorites");
  Fl_File_Chooser::all_files_label = _("All Files (*)");
  Fl_File_Chooser::custom_filter_label = _("Custom Filter");
  Fl_File_Chooser::existing_file_label = _("The file already exists. Replace it?");
  Fl_File_Chooser::favorites_label = _("Favorites");
  Fl_File_Chooser::filename_label = _("File:");
  Fl_File_Chooser::filesystems_label = _("Disks");
  Fl_File_Chooser::hidden_label = _("Hidden files");
  Fl_File_Chooser::manage_favorites_label = _("Manage Favorites");
  Fl_File_Chooser::new_directory_label = _("New folder name:");
  Fl_File_Chooser::new_directory_tooltip = _("New Folder");
  Fl_File_Chooser::preview_label = _("Preview");
  Fl_File_Chooser::save_label = _("Save");
  Fl_File_Chooser::show_label = _("Show:");
}

// An icon as an RGBA image: drawn at 32x32 on a transparent background (an
// offscreen with alpha), scaled to `size`.
Fl_RGB_Image *icon_image(Icon icon, int size) {
  // two renderings, on black and on white: the alpha comes from their difference
  unsigned char *px[2] = {nullptr, nullptr};
  for (int pass = 0; pass < 2; pass++) {
    Fl_Image_Surface surf(32, 32);
    Fl_Surface_Device::push_current(&surf);
    fl_color(pass ? FL_WHITE : FL_BLACK);
    fl_rectf(0, 0, 32, 32);
    draw_icon(icon, 0, 0, false);
    Fl_Surface_Device::pop_current();
    Fl_RGB_Image *img = surf.image();
    px[pass] = new unsigned char[32 * 32 * 3];
    const unsigned char *src = (const unsigned char *)img->data()[0];
    int d = img->d(), ld = img->ld() ? img->ld() : img->data_w() * d;
    for (int yy = 0; yy < 32; yy++)
      for (int xx = 0; xx < 32; xx++)
        for (int c = 0; c < 3; c++) px[pass][(yy * 32 + xx) * 3 + c] = src[yy * ld + xx * d + c];
    delete img;
  }
  auto *rgba = new unsigned char[32 * 32 * 4];
  for (int i = 0; i < 32 * 32; i++) {
    int a = 255 - (px[1][i * 3 + 1] - px[0][i * 3 + 1]);   // green channel difference
    a = std::clamp(a, 0, 255);
    for (int c = 0; c < 3; c++)
      rgba[i * 4 + c] = a ? (unsigned char)std::min(255, px[0][i * 3 + c] * 255 / a) : 0;
    rgba[i * 4 + 3] = (unsigned char)a;
  }
  delete[] px[0];
  delete[] px[1];
  auto *full = new Fl_RGB_Image(rgba, 32, 32, 4);
  full->alloc_array = 1;
  if (size == 32) return full;
  Fl_RGB_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
  auto *out = (Fl_RGB_Image *)full->copy(size, size);
  delete full;
  return out;
}

}  // namespace plat
