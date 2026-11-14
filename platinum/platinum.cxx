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
#include <FL/Fl_PNG_Image.H>
#include <iterator>
#include <map>
#include "logo_data.h"
#include <algorithm>
#include <unistd.h>
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

// Every Nirvana program follows Appearance at once - the colors, the window style, the
// pointer: the panel tells everybody through a property of the root window (APPEARANCE_ATOM).
// (Before, only the desktop and the window manager did; the other programs kept the old
// scheme until they were opened again.) The interface scale still needs them started again.
static void appearance_later(void *) {
  reload_appearance();
  for (Fl_Window *w = Fl::first_window(); w; w = Fl::next_window(w)) w->redraw();
}

static void watch_pointer() {
#if defined(FLTK_USE_X11)
  Display *d = fl_x11_display();
  if (!d) return;
  Window root = DefaultRootWindow(d);
  XWindowAttributes wa;
  XGetWindowAttributes(d, root, &wa);
  XSelectInput(d, root, wa.your_event_mask | PropertyChangeMask);
  static Atom at = XInternAtom(d, APPEARANCE_ATOM, False);
  Fl::add_system_handler([](void *ev, void *) {
    auto *e = (XEvent *)ev;
    if (e->type == PropertyNotify && e->xproperty.atom == at && e->xproperty.window == DefaultRootWindow(fl_x11_display()))
      Fl::add_timeout(0.0, appearance_later);   // after this event, not in the middle of it
    return 0;   // the others may need it too (the desktop, the window manager reload more)
  }, nullptr);
#endif
}

void init() {
  // where the window manager zooms this program's window from (it reads our starting
  // environment) - not for the programs we start
  unsetenv("NIRVANA_ZOOM_FROM");
  fl_local_meta = "Win";   // menus show the global shortcuts as "Win+E" (the Windows/Super key)
  init_i18n();   // before the first interface text
  Fl::scheme("none");
  reload_appearance();   // color scheme from ~/.config/nirvana/appearance.conf
  apply_scale();
  apply_cursor(read_appearance());   // now with the display open: its pointer theme
  watch_pointer();

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

// Ram Disk: a green memory module with gold contacts, black chips and a light check mark
static void ram_disk(int x, int y, bool sel) {
  col(0x1E7A3C, sel); fl_rectf(x + 1, y + 9, 30, 14);                // board
  col(0x3FA85E, sel); fl_xyline(x + 2, y + 10, x + 29);              // its lit edge
  col(0x111111, sel);                                                // chips
  for (int i = 0; i < 3; i++) fl_rectf(x + 4 + i * 9, y + 12, 6, 6);
  col(0x3A3A3A, sel);
  for (int i = 0; i < 3; i++) fl_xyline(x + 5 + i * 9, y + 13, x + 8 + i * 9);
  col(0xE8B830, sel);                                                // gold contacts
  for (int i = 0; i < 13; i++) fl_rectf(x + 3 + i * 2, y + 20, 1, 3);
  fl_color(DARK);
  fl_rect(x + 1, y + 9, 30, 14);
  fl_rectf(x + 15, y + 21, 2, 2);                                    // the notch
  col(0xFFFFFF, sel);                                                // check mark (ready)
  fl_line_style(FL_SOLID, 2);
  fl_line(x + 22, y + 5, x + 25, y + 8);
  fl_line(x + 25, y + 8, x + 30, y + 2);
  fl_line_style(0);
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

void draw_bluetooth(int x, int y, Fl_Color c) {
  // the bind rune drawn with lines: a stem and two arrow heads on the right
  fl_color(c);
  fl_line_style(FL_SOLID, 1);
  fl_yxline(x + 4, y, y + 12);
  fl_yxline(x + 5, y, y + 12);
  fl_line(x + 5, y, x + 8, y + 3);
  fl_line(x + 8, y + 3, x + 1, y + 9);
  fl_line(x + 5, y + 12, x + 8, y + 9);
  fl_line(x + 8, y + 9, x + 1, y + 3);
}

// The shield outline (as in draw_shield) - a path to fill or stroke
static void shield_path(double x, double y, double W, double H) {
  double l = x, r = x + W, t = y, b = y + H, m = x + W / 2;
  fl_vertex(m, t);
  fl_curve(m, t, m + (r - m) * 0.45, t + (b - t) * 0.10, r, t + (b - t) * 0.08, r, t + (b - t) * 0.12);
  fl_curve(r, t + (b - t) * 0.12, r, t + (b - t) * 0.55, r - (r - m) * 0.35, t + (b - t) * 0.82, m, b);
  fl_curve(m, b, l + (m - l) * 0.35, t + (b - t) * 0.82, l, t + (b - t) * 0.55, l, t + (b - t) * 0.12);
  fl_curve(l, t + (b - t) * 0.12, l, t + (b - t) * 0.08, m - (m - l) * 0.45, t + (b - t) * 0.10, m, t);
}

// a small shield (12 x 14) drawn point by point: shoulders, straight sides, the point below
static void small_shield(int x, int y, bool fill) {
  static const int P[][2] = {{0, 2}, {3, 1}, {6, 0}, {9, 1}, {12, 2}, {12, 7}, {11, 10}, {9, 12}, {6, 14},
                             {3, 12}, {1, 10}, {0, 7}};
  if (fill) fl_begin_polygon(); else fl_begin_loop();
  for (auto &p : P) fl_vertex(x + p[0] + (fill ? 0 : 0.5), y + p[1] + (fill ? 0 : 0.5));
  if (fill) fl_end_polygon(); else fl_end_loop();
}

void draw_shield_mono(int x, int y, int size, int state, Fl_Color c, Fl_Color bg) {
  if (size <= 16) {
    fl_color(c);
    if (state == 1 || state == 2) {
      small_shield(x, y, true);
      fl_color(bg);
      fl_line_style(FL_SOLID, 2);
      if (state == 1) fl_line(x + 3, y + 6, x + 5, y + 9, x + 9, y + 4);
      else fl_line(x + 3, y + 6, x + 9, y + 6);
      fl_line_style(0);
    } else {
      fl_line_style(state == 3 ? FL_DOT : FL_SOLID, 1);
      small_shield(x, y, false);
      fl_line_style(0);
    }
    return;
  }
  double W = size * 0.82, H = size;
  fl_color(c);
  if (state == 1 || state == 2) {   // filled, the sign cut out in the background color
    fl_begin_complex_polygon(); shield_path(x, y, W, H); fl_end_complex_polygon();
    fl_color(bg);
    fl_line_style(FL_SOLID | FL_CAP_ROUND, std::max(1, size / 8));
    double cx = x + W / 2, cy = y + H * 0.45;
    if (state == 1)
      fl_line((int)(cx - W * 0.24), (int)cy, (int)(cx - W * 0.05), (int)(cy + H * 0.17), (int)(cx + W * 0.26), (int)(cy - H * 0.17));
    else
      fl_line((int)(cx - W * 0.24), (int)cy, (int)(cx + W * 0.24), (int)cy);
    fl_line_style(0);
  } else {                          // an outline: off; dotted look when the service is not there
    fl_line_style(state == 3 ? FL_DOT : FL_SOLID, 1);
    fl_begin_loop(); shield_path(x + 0.5, y + 0.5, W - 1, H - 1); fl_end_loop();
    fl_line_style(0);
  }
}

void draw_shield(int x, int y, int size, int state) {
  double W = size * 0.82, H = size;
  auto shape = [&](double inset) {
    double l = x + inset, r = x + W - inset, t = y + inset, b = y + H - inset, m = x + W / 2;
    fl_vertex(m, t);
    fl_curve(m, t, m + (r - m) * 0.45, t + (b - t) * 0.10, r, t + (b - t) * 0.08, r, t + (b - t) * 0.12);
    fl_curve(r, t + (b - t) * 0.12, r, t + (b - t) * 0.55, r - (r - m) * 0.35, t + (b - t) * 0.82, m, b);
    fl_curve(m, b, l + (m - l) * 0.35, t + (b - t) * 0.82, l, t + (b - t) * 0.55, l, t + (b - t) * 0.12);
    fl_curve(l, t + (b - t) * 0.12, l, t + (b - t) * 0.08, m - (m - l) * 0.45, t + (b - t) * 0.10, m, t);
  };
  Fl_Color fill = state == 1 ? fl_rgb_color(0x2E, 0xA0, 0x44) : state == 2 ? fl_rgb_color(0xD2, 0x3B, 0x30)
                : state == 3 ? GRAY : fl_rgb_color(0x9A, 0x9A, 0x9A);
  Fl_Color hi = fl_color_average(fill, FL_WHITE, 0.6f);
  fl_color(DARK);
  fl_begin_complex_polygon(); shape(0); fl_end_complex_polygon();
  double bw = std::max(1.0, size / 14.0);
  fl_color(fill);
  fl_begin_complex_polygon(); shape(bw); fl_end_complex_polygon();
  if (size >= 24) {   // a light left half, as if lit from the left
    fl_color(hi);
    fl_begin_complex_polygon();
    double l = x + bw * 2, m = x + W / 2, t = y + bw * 2, b = y + H - bw * 2;
    fl_vertex(m, t + size * 0.02);
    fl_curve(m, t, l + (m - l) * 0.5, t + (b - t) * 0.1, l, t + (b - t) * 0.09, l, t + (b - t) * 0.13);
    fl_curve(l, t + (b - t) * 0.13, l, t + (b - t) * 0.5, l + (m - l) * 0.5, t + (b - t) * 0.78, m, b - size * 0.04);
    fl_end_complex_polygon();
    fl_color(fill);
    fl_begin_complex_polygon(); shape(bw + size * 0.08); fl_end_complex_polygon();
  }
  // the sign
  int lw = std::max(1, size / 9);
  fl_color(state == 3 ? DARK : FL_WHITE);
  fl_line_style(FL_SOLID | FL_CAP_ROUND | FL_JOIN_ROUND, lw);
  double cx = x + W / 2, cy = y + H * 0.46;
  if (state == 1) {
    fl_line((int)(cx - W * 0.22), (int)(cy), (int)(cx - W * 0.05), (int)(cy + H * 0.16), (int)(cx + W * 0.24), (int)(cy - H * 0.16));
  } else if (state == 2) {
    fl_line((int)(cx - W * 0.22), (int)cy, (int)(cx + W * 0.22), (int)cy);
  } else if (state == 3 && size >= 24) {
    fl_line((int)(cx - W * 0.15), (int)(cy - H * 0.15), (int)(cx + W * 0.15), (int)(cy + H * 0.15));
    fl_line((int)(cx + W * 0.15), (int)(cy - H * 0.15), (int)(cx - W * 0.15), (int)(cy + H * 0.15));
  }
  fl_line_style(0);
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
  case Icon::RamDisk:     ram_disk(x, y, sel); break;
  default:
    if (icon >= Icon::CatGames && icon <= Icon::CatOther) {   // a folder with the category emblem
      folder(x, y, sel, false);
      draw_category_emblem((int)icon - (int)Icon::CatGames, x + 9, y + 12, sel, 1);
    }
    break;
  }
}

std::string guru_number(const std::string &m) {
  unsigned h = 2166136261u;   // FNV-1a
  for (unsigned char c : m) h = (h ^ c) * 16777619u;
  char b[32];
  snprintf(b, sizeof b, "#%08X.%08X", (unsigned)(m.size() % 16 + 1), h);
  return b;
}

std::string pkexec_command() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  std::string e = n > 0 ? std::string(buf, (size_t)n) : "";
  e = e.substr(0, e.rfind('/'));
  for (const std::string &c : {e + "/../libexec/nirvana-pkexec", e + "/../update/nirvana-pkexec",
                               std::string("/usr/libexec/nirvana-pkexec"), std::string("/usr/local/libexec/nirvana-pkexec")})
    if (!access(c.c_str(), X_OK)) return c;
  return "pkexec";
}

void guru_alert(const std::string &m) {
  fl_message_title(GURU_TITLE);
  fl_alert("%s\n\n%s %s", m.c_str(), GURU_TITLE, guru_number(m).c_str());
}

// The Nirvana Desktop logo: an enso - one stroke of a brush in a circle, in one color: black
// on light schemes, light on dark ones (on the screen saver's black too); transparent around
// (it sits on the menu bar, the About window, the login screen as it is). Built into the
// program as PNG files (platinum/logo_data.h, made by tools/logo/make_logo.py) - only their
// transparency is used; other sizes are scaled from the nearest larger one (smoothly) and kept.
static std::map<std::pair<int, unsigned>, Fl_RGB_Image *> g_logo_cache;

Fl_Color logo_ink(Fl_Color bg) {
  uchar r, g, b;
  Fl::get_color(bg, r, g, b);
  bool dark = 0.299 * r + 0.587 * g + 0.114 * b < 128;
  return dark ? fl_rgb_color(0xF4, 0xF4, 0xF4) : fl_rgb_color(0x00, 0x00, 0x00);
}

static Fl_RGB_Image *logo_for(int s, Fl_Color ink) {
  uchar ir, ig, ib;
  Fl::get_color(ink, ir, ig, ib);
  unsigned key = (unsigned)ir << 16 | (unsigned)ig << 8 | ib;
  auto &cache = g_logo_cache;
  auto it = cache.find({s, key});
  if (it != cache.end()) return it->second;
  const logo_data::Png *best = nullptr;
  for (auto &p : logo_data::all)
    if (p.size >= s && (!best || p.size < best->size)) best = &p;
  if (!best) best = &logo_data::all[std::size(logo_data::all) - 1];
  Fl_PNG_Image png(nullptr, best->data, best->len);
  Fl_RGB_Image *img = nullptr;
  if (png.w() > 0 && png.d() == 4) {
    Fl_RGB_Image *src = (Fl_RGB_Image *)png.copy();
    if (png.w() != s) {
      Fl_RGB_Scaling was = Fl_Image::RGB_scaling();
      Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
      delete src;
      src = (Fl_RGB_Image *)png.copy(s, s);
      Fl_Image::RGB_scaling(was);
    }
    // one color, the shape (the transparency) of the picture
    int w = src->w(), h = src->h(), ld = src->ld() ? src->ld() : w * 4;
    const uchar *in = (const uchar *)src->data()[0];
    uchar *out = new uchar[w * h * 4];
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        const uchar *p = in + y * ld + x * 4;
        uchar *q = out + (y * w + x) * 4;
        q[0] = ir; q[1] = ig; q[2] = ib; q[3] = p[3];
      }
    delete src;
    img = new Fl_RGB_Image(out, w, h, 4);
    img->alloc_array = 1;
  }
  if (cache.size() > 64) {   // the screen saver draws many sizes: do not keep them all
    for (auto &[k, v] : cache) delete v;
    cache.clear();
  }
  cache[{s, key}] = img;
  return img;
}

void draw_logo(int x, int y, int s, Fl_Color bg) {
  if (s <= 0) return;
  Fl_Color ink = logo_ink(bg);
  if (Fl_RGB_Image *img = logo_for(s, ink)) {
    img->draw(x, y);
  } else {   // no PNG support: a ring
    fl_color(ink);
    fl_line_style(FL_SOLID, std::max(2, s / 8));
    fl_arc(x + s / 6, y + s / 6, s * 2 / 3, s * 2 / 3, 0, 330);
    fl_line_style(0);
  }
}

// The logo as a picture with transparency (for menu items, buttons): a copy, the caller owns it
Fl_RGB_Image *logo_image(int s, Fl_Color bg) {
  if (s <= 0) return nullptr;
  Fl_RGB_Image *img = logo_for(s, logo_ink(bg));
  return img ? (Fl_RGB_Image *)img->copy() : nullptr;
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

// The colored title bar (schemes with solid, gradient or glass bars) in the given box
static void title_fill(int fx, int fy, int fw, int fh, bool active, TitleStyle st) {
  const Scheme &sc = current_scheme();
  unsigned c1 = active ? sc.title : sc.title_i, c2 = active ? sc.title2 : sc.title_i2;
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
    title_fill(x + 3, y + 3, w - 6, TITLE_H - 5, active, st);
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

void draw_close_box(int x, int y, bool pressed, bool active) { title_button(x, y, 1, pressed, active); }

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
// "Classic 16-bit" windows - our own drawing of the gadget style of the 16-bit
// desktops: a flat bar of beveled cells, colored when the window is active.
// ---------------------------------------------------------------------------
static void classic_cell(int x, int y, int w, int h, bool pressed, Fl_Color face, bool fill = true) {
  if (fill) {
    fl_color(face);
    fl_rectf(x, y, w, h);
  }
  fl_color(pressed ? DARK : LIGHT);
  fl_xyline(x, y, x + w - 2);
  fl_yxline(x, y, y + h - 2);
  fl_color(pressed ? LIGHT : DARK);
  fl_xyline(x, y + h - 1, x + w - 1);
  fl_yxline(x + w - 1, y, y + h - 1);
}

// Does the scheme color the classic bar itself (gradient, glass, or a solid title color)?
static bool classic_own_bar() {
  const Scheme &sc = current_scheme();
  TitleStyle st = title_style();
  return st == TitleStyle::Gradient || st == TitleStyle::Glossy || (st == TitleStyle::Solid && sc.title != sc.window);
}

static bool has_close(int b) { return b == 1 || b == 3 || b == 4; }
static bool has_zoom(int b) { return b == 1; }
static bool has_depth(int b) { return b >= 1 && b <= 3; }

int classic_gadget_hit(int w, int ex, int ey, int boxes) {
  if (!boxes || ey < 0 || ey >= TITLE_H || ex < 0 || ex >= w) return 0;
  const int G = CLASSIC_GADGET;
  if (has_close(boxes) && ex < G) return 1;
  if (has_depth(boxes) && ex >= w - G) return 4;
  if (has_zoom(boxes) && ex >= w - 2 * G && ex < w - G) return 2;
  return 0;
}

void draw_classic_titlebar(int x, int y, int w, const char *title, bool active, int pressed, int boxes) {
  const int G = CLASSIC_GADGET, H = TITLE_H;
  Fl_Color face = active ? SEL : GRAY;
  Fl_Color ink = DARK;                       // the gadget pictures: dark lines,
  Fl_Color fill = active ? LIGHT : CONTENT;  // light sheets - readable on any bar color
  // a scheme with its own bar colors (gradient, glass, or a solid title color) paints the bar
  // with them, the gadget cells then only carry their bevels
  const Scheme &sc = current_scheme();
  TitleStyle st = title_style();
  bool own = classic_own_bar();
  Fl_Color tcol = active ? SEL_TEXT : TEXT;
  if (own) {
    title_fill(x, y, w, H, active, st);
    unsigned tc = active ? sc.title_text : sc.title_i_text;
    tcol = fl_rgb_color(tc >> 16, (tc >> 8) & 0xFF, tc & 0xFF);
  }
  classic_cell(x, y, w, H, false, face, !own);
  int left = x + 6, right = x + w - 6;
  if (has_close(boxes)) {
    bool p = pressed == 1;
    classic_cell(x, y, G, H, p, face, !own);
    int cx = x + (G - 9) / 2 + p, cy = y + (H - 9) / 2 + p;   // a square with a solid core
    fl_color(fill); fl_rectf(cx + 1, cy + 1, 7, 7);
    fl_color(ink);  fl_rect(cx, cy, 9, 9);
    fl_rectf(cx + 3, cy + 3, 3, 3);
    left = x + G + 6;
  }
  if (has_depth(boxes)) {
    bool p = pressed == 4;
    int gx = x + w - G;
    classic_cell(gx, y, G, H, p, face, !own);
    int cx = gx + (G - 14) / 2 + p, cy = y + (H - 11) / 2 + p;   // two sheets, one behind the other
    fl_color(ink);  fl_rectf(cx, cy, 9, 7);
    fl_color(fill); fl_rectf(cx + 5, cy + 4, 9, 7);
    fl_color(ink);  fl_rect(cx + 5, cy + 4, 9, 7);
    right = gx - 6;
  }
  if (has_zoom(boxes)) {
    bool p = pressed == 2;
    int gx = x + w - 2 * G;
    classic_cell(gx, y, G, H, p, face, !own);
    int cx = gx + (G - 12) / 2 + p, cy = y + (H - 10) / 2 + p;   // a big frame with a small one in its corner
    fl_color(fill); fl_rectf(cx + 1, cy + 1, 10, 8);
    fl_color(ink);  fl_rect(cx, cy, 12, 10);
    fl_rectf(cx + 2, cy + 2, 5, 4);
    right = gx - 6;
  }
  fl_font(FONT_SYSTEM, SIZE_SYSTEM);
  std::string t = ellipsize(title ? title : "", right - left);
  fl_color(tcol);
  fl_push_clip(left, y, std::max(0, right - left), H);
  fl_draw(t.c_str(), left, y + H / 2 + fl_height() / 2 - fl_descent());
  fl_pop_clip();
}

void draw_classic_frame(int x, int y, int w, int h, bool active, bool shaded, bool size_gadget) {
  Fl_Color face = active ? SEL : GRAY;
  if (classic_own_bar()) {   // the frame in the bar's color
    unsigned c = active ? current_scheme().title : current_scheme().title_i;
    face = fl_rgb_color(c >> 16, (c >> 8) & 0xFF, c & 0xFF);
  }
  fl_color(face);
  fl_rectf(x, y, w, h);
  fl_color(LIGHT);
  fl_xyline(x + 1, y + 1, x + w - 2);
  fl_yxline(x + 1, y + 1, y + h - 2);
  fl_color(SHADOW);
  fl_xyline(x + 1, y + h - 2, x + w - 2);
  fl_yxline(x + w - 2, y + 1, y + h - 2);
  fl_color(DARK);
  fl_rect(x, y, w, h);
  if (!size_gadget || shaded) return;
  // the size gadget: an L of the frame in the bottom right corner, gray, with a little
  // arrow pointing out of the corner
  const int S = CLASSIC_SIZE, F = FRAME;
  int rx = x + w - F, by = y + h - F;
  fl_color(GRAY);
  fl_rectf(x + w - S, by, S, F);
  fl_rectf(rx, y + h - S, F, S);
  fl_color(LIGHT);
  fl_xyline(x + w - S, by, rx);
  fl_yxline(rx, y + h - S, by);
  fl_color(DARK);
  fl_xyline(x + w - S - 1, by, x + w - S - 1);
  fl_yxline(x + w - S - 1, by, y + h - 1);
  fl_xyline(rx, y + h - S - 1, x + w - 1);
  fl_rect(x, y, w, h);
  fl_color(TEXT);
  for (int i = 0; i < 3; i++) fl_point(x + w - 3 - i, y + h - 3);
  for (int i = 0; i < 3; i++) fl_point(x + w - 3, y + h - 3 - i);
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

// A name in lines no wider than max_w (the current font): broken after a space, "-", "_" or
// "." when there is one in the line, otherwise between two characters - nothing left out
std::vector<std::string> wrap_text(const std::string &s, int max_w) {
  std::vector<std::string> lines;
  if (max_w <= 0 || fl_width(s.c_str()) <= max_w) { lines.push_back(s); return lines; }
  std::string cur;
  size_t brk = std::string::npos;   // where the current line may be broken (after this byte)
  const char *p = s.c_str(), *end = p + s.size();
  while (p < end) {
    int len = fl_utf8len1(*p);
    if (len < 1) len = 1;
    std::string ch(p, (size_t)std::min<long>(len, end - p));
    p += ch.size();
    if (!cur.empty() && fl_width((cur + ch).c_str()) > max_w) {
      if (brk != std::string::npos && brk + 1 < cur.size()) {
        lines.push_back(cur.substr(0, brk + 1));
        cur = cur.substr(brk + 1);
      } else {
        lines.push_back(cur);
        cur.clear();
      }
      brk = std::string::npos;
      for (size_t i = 0; i < cur.size(); i++)
        if (cur[i] == ' ' || cur[i] == '-' || cur[i] == '_' || cur[i] == '.') brk = i;
    }
    cur += ch;
    if (ch == " " || ch == "-" || ch == "_" || ch == ".") brk = cur.size() - 1;
  }
  if (!cur.empty()) lines.push_back(cur);
  for (auto &l : lines)   // a space at the end of a line is not shown
    while (l.size() > 1 && l.back() == ' ') l.pop_back();
  return lines;
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
