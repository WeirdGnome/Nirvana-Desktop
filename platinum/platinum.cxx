// LisaOS - libplatinum: implementacja wyglądu Platinum
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_utf8.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace plat {

Fl_Boxtype UP_BOX, DOWN_BOX, BUTTON_BOX, BUTTON_DOWN, WELL_BOX, MENUBAR_BOX,
           THUMB_BOX, TRACK_BOX;
Fl_Font FONT_SYSTEM = FL_HELVETICA_BOLD;
Fl_Font FONT_SMALL  = FL_HELVETICA;

static bool g_root = false;
bool root_mode() { return g_root; }

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

// Przycisk Platinum: czarny zaokrąglony obrys, jasny górny-lewy brzeg.
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

// "Studnia": białe wnętrze, ciemny obrys wewnętrzny, jasny zewnętrzny.
static void well_box(int x, int y, int w, int h, Fl_Color c) {
  fl_color(c);
  fl_rectf(x + 2, y + 2, w - 4, h - 4);
  bevel(x, y, w, h, MID, LIGHT);
  fl_color(DARK);
  fl_rect(x + 1, y + 1, w - 2, h - 2);
}

static void menubar_box(int x, int y, int w, int h, Fl_Color c) {
  fl_color(c);
  fl_rectf(x, y, w, h);
  fl_color(LIGHT);
  fl_xyline(x, y, x + w - 1);
  fl_color(MID);
  fl_xyline(x, y + h - 2, x + w - 1);
  fl_color(DARK);
  fl_xyline(x, y + h - 1, x + w - 1);
}

// Suwak przewijania: lawendowy z rowkami pośrodku, jak w Mac OS 9.
static void thumb_box(int x, int y, int w, int h, Fl_Color c) {
  fl_color(c);
  fl_rectf(x, y, w, h);
  bevel(x + 1, y + 1, w - 2, h - 2, fl_lighter(c), fl_darker(c));
  fl_color(DARK);
  fl_rect(x, y, w, h);
  if (w > h + 6) {          // poziomy
    int cx = x + w / 2 - 4;
    for (int i = 0; i < 4; i++) {
      fl_color(fl_lighter(c)); fl_yxline(cx + i * 2, y + 4, y + h - 5);
      fl_color(fl_darker(c));  fl_yxline(cx + i * 2 + 1, y + 4, y + h - 5);
    }
  } else if (h > w + 6) {   // pionowy
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

void init(bool root) {
  g_root = root;
  Fl::scheme("none");
  reload_appearance();   // schemat kolorów z ~/.config/lisaos/wyglad.conf

  int f = FL_FREE_BOXTYPE;
  UP_BOX      = Fl_Boxtype(f++); Fl::set_boxtype(UP_BOX, up_box, 1, 1, 2, 2);
  DOWN_BOX    = Fl_Boxtype(f++); Fl::set_boxtype(DOWN_BOX, down_box, 1, 1, 2, 2);
  BUTTON_BOX  = Fl_Boxtype(f++); Fl::set_boxtype(BUTTON_BOX, button_box, 2, 2, 4, 4);
  BUTTON_DOWN = Fl_Boxtype(f++); Fl::set_boxtype(BUTTON_DOWN, button_down, 2, 2, 4, 4);
  WELL_BOX    = Fl_Boxtype(f++); Fl::set_boxtype(WELL_BOX, well_box, 2, 2, 4, 4);
  MENUBAR_BOX = Fl_Boxtype(f++); Fl::set_boxtype(MENUBAR_BOX, menubar_box, 0, 0, 0, 2);
  THUMB_BOX   = Fl_Boxtype(f++); Fl::set_boxtype(THUMB_BOX, thumb_box, 1, 1, 2, 2);
  TRACK_BOX   = Fl_Boxtype(f++); Fl::set_boxtype(TRACK_BOX, track_box, 1, 1, 2, 2);

  // Standardowe widgety FLTK też dostają wygląd Platinum.
  Fl::set_boxtype(FL_UP_BOX, up_box, 1, 1, 2, 2);
  Fl::set_boxtype(FL_THIN_UP_BOX, up_box, 1, 1, 2, 2);
  Fl::set_boxtype(FL_DOWN_BOX, down_box, 1, 1, 2, 2);
  Fl::set_boxtype(FL_THIN_DOWN_BOX, down_box, 1, 1, 2, 2);
  Fl::set_boxtype(FL_UP_FRAME, up_frame, 1, 1, 2, 2);
  Fl::set_boxtype(FL_DOWN_FRAME, down_frame, 1, 1, 2, 2);

  // Jeśli w systemie jest wolna czcionka w stylu Chicago, użyj jej.
  if (const char *ff = getenv("LISAOS_SYSTEM_FONT")) {
    Fl::set_font(FL_FREE_FONT, ff);
    FONT_SYSTEM = FL_FREE_FONT;
  }
  // podpowiedzi (dok, aplety, gary): dymek pomocy jak w Mac OS
  Fl_Tooltip::font(FONT_SMALL);
  Fl_Tooltip::size(SIZE_SMALL + 1);
  Fl_Tooltip::color(fl_rgb_color(255, 255, 204));
  Fl_Tooltip::textcolor(FL_BLACK);
  Fl_Tooltip::margin_width(5);
  Fl_Tooltip::margin_height(3);
}

// ---------------------------------------------------------------------------
// Ikony
// ---------------------------------------------------------------------------
// Zaznaczona ikona w Mac OS 9 jest przyciemniona - robimy to samo.
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
  if (open) {  // folder z otwartym oknem: wzór kreskowany
    col(0x55559A, sel);
    for (int yy = y + 11; yy < y + 26; yy += 2)
      for (int xx = x + 4 + (yy & 2); xx < x + 28; xx += 4) fl_point(xx, yy);
  }
  fl_color(DARK);
  fl_loop(x + 2, y + 9, x + 4, y + 6, x + 13, y + 6, x + 15, y + 9);
  fl_rect(x + 2, y + 9, 28, 19);
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
  fl_line(cx - 4, cy + 4, cx + 5, cy - 5);   // "ołówek" na rombie
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
  // mały domek na obudowie
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
  // pokrywa (przy pełnym koszu uchylona)
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

// Dysk wewnętrzny: szara obudowa z diodą (jak LisaOS HD, bez domku)
static void hard_disk(int x, int y, bool sel) {
  col(0xD8D8D8, sel); fl_rectf(x + 2, y + 10, 28, 15);
  col(0xFFFFFF, sel); fl_xyline(x + 3, y + 11, x + 28);
  col(0xA0A0A0, sel); fl_rectf(x + 3, y + 19, 26, 5);
  col(0x22CC44, sel); fl_rectf(x + 24, y + 20, 3, 2);
  fl_color(DARK); fl_rect(x + 2, y + 10, 28, 15);
}

// Pendrive: korpus z wtykiem USB
static void removable(int x, int y, bool sel) {
  col(0xC0C0C0, sel); fl_rectf(x + 20, y + 12, 8, 9);              // wtyk
  col(0x707070, sel); fl_rectf(x + 23, y + 14, 2, 2); fl_rectf(x + 23, y + 18, 2, 2);
  col(0x5B3FA0, sel); fl_rectf(x + 3, y + 9, 18, 15);              // korpus
  col(0x8C74C8, sel); fl_xyline(x + 4, y + 10, x + 19);
  col(0xF0C040, sel); fl_rectf(x + 6, y + 14, 6, 5);               // naklejka
  fl_color(DARK);
  fl_rect(x + 3, y + 9, 18, 15);
  fl_rect(x + 20, y + 12, 8, 9);
}

// Płyta CD/DVD: srebrny krążek z otworem i tęczowym połyskiem
static void optical(int x, int y, bool sel) {
  col(0xE4E4EC, sel); fl_pie(x + 3, y + 3, 26, 26, 0, 360);
  col(0xB8C8F0, sel); fl_pie(x + 3, y + 3, 26, 26, 20, 70);
  col(0xF0D0E8, sel); fl_pie(x + 3, y + 3, 26, 26, 200, 250);
  col(0xFFFFFF, sel); fl_pie(x + 11, y + 11, 10, 10, 0, 360);
  fl_color(DARK);
  fl_arc(x + 3, y + 3, 26, 26, 0, 360);
  fl_arc(x + 13, y + 13, 6, 6, 0, 360);
}

// Dysk zaszyfrowany: dysk z kłódką
static void encrypted(int x, int y, bool sel) {
  hard_disk(x, y + 3, sel);
  col(0xF0C040, sel); fl_rectf(x + 18, y + 13, 11, 9);             // korpus kłódki
  fl_color(DARK); fl_rect(x + 18, y + 13, 11, 9);
  fl_arc(x + 19, y + 6, 9, 12, 0, 180);                            // pałąk
  fl_line(x + 19, y + 12, x + 19, y + 13);
  fl_line(x + 27, y + 12, x + 27, y + 13);
  fl_rectf(x + 23, y + 16, 1, 3);                                  // dziurka
}

// Symbol wysuwania (kosz podczas przeciągania dysku, jak w Mac OS)
static void eject_icon(int x, int y, bool sel) {
  col(0x404040, sel);
  fl_polygon(x + 16, y + 5, x + 28, y + 19, x + 4, y + 19);
  fl_rectf(x + 4, y + 23, 25, 5);
}

// Sieć: kula ziemska z wtyczką (panel Sieć, okna dialogowe)
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
  // kabel z wtyczką
  col(0x404040, sel);
  fl_line_style(FL_SOLID, 2);
  fl_line(x + 16, y + 26, x + 16, y + 29);
  fl_line_style(0);
  fl_rectf(x + 12, y + 28, 9, 4);
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
  // kropka i trzy łuki rozchodzące się w górę (16x13)
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
  if (level < 0) {   // wyłączone: ukośna kreska
    fl_color(on);
    fl_line_style(FL_SOLID, 2);
    fl_line(x + 2, y + 1, x + 14, y + 12);
    fl_line_style(0);
  }
}

void draw_wired(int x, int y, Fl_Color c) {
  // trzy połączone kwadraty - klasyczny symbol sieci
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
  // korpus i lejek
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
  if (charging) {   // błyskawica w kolorze tła paska na wypełnieniu
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
  // tylny: lewy górny, sam obrys
  fl_color(back);
  fl_rectf(x, y, 12, 9);
  fl_color(line);
  fl_rect(x, y, 12, 9);
  // przedni: prawy dolny, wypełniony, przykrywa róg tylnego
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

void draw_icon(Icon icon, int x, int y, bool sel) {
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
  }
}

// Logo LisaOS: chip DIP (ukłon w stronę układów Amigi) z literą "L".
void draw_logo(int x, int y, int s) {
  Fl_Color body = g_root ? ROOT_TINT : rgb(0x4B3590);
  fl_color(rgb(0x777777));   // logo ma stałe kolory, niezależne od schematu
  for (int i = 0; i < 4; i++) {
    int px = x + 3 + i * (s - 6) / 3;
    fl_rectf(px, y, 2, 2);
    fl_rectf(px, y + s - 2, 2, 2);
  }
  fl_color(body);
  fl_rectf(x + 1, y + 2, s - 2, s - 4);
  fl_color(FL_BLACK);
  fl_rect(x + 1, y + 2, s - 2, s - 4);
  fl_color(FL_WHITE);
  int lx = x + s / 2 - 3, ly = y + 4;
  fl_rectf(lx, ly, 2, s - 8);
  fl_rectf(lx, ly + s - 10, 6, 2);
  fl_color(rgb(0xF0C040));
  fl_pie(x + 2, y + 3, 3, 3, 0, 360);   // znacznik pinu 1
}

// ---------------------------------------------------------------------------
// Chrome okna
// ---------------------------------------------------------------------------
static void title_button(int x, int y, int kind, bool pressed) {
  fl_color(SHADOW);
  fl_rect(x, y, BOX, BOX);
  fl_color(pressed ? SHADOW : GRAY);
  fl_rectf(x + 1, y + 1, BOX - 2, BOX - 2);
  if (!pressed) bevel(x + 1, y + 1, BOX - 2, BOX - 2, LIGHT, MID);
  fl_color(pressed ? LIGHT : DARK);
  if (kind == 2) {                        // zoom: mały kwadrat w rogu
    fl_rect(x + 2, y + 2, 5, 5);
  } else if (kind == 3) {                 // zwiń (windowshade): pozioma kreska
    fl_xyline(x + 2, y + 4, x + BOX - 3);
    fl_xyline(x + 2, y + 6, x + BOX - 3);
  }
}

void draw_titlebar(int x, int y, int w, const char *title, bool active, int pressed, int boxes) {
  int by = y + (TITLE_H - BOX) / 2;
  int close_x = x + 8;
  int zoom_x = x + w - 8 - BOX;
  int shade_x = boxes == 2 ? zoom_x : zoom_x - BOX - 5;   // bez "zoom": zwijanie w rogu

  fl_font(FONT_SYSTEM, SIZE_SYSTEM);
  std::string t = ellipsize(title ? title : "", w - 2 * (BOX * 2 + 30));
  int tw = (int)fl_width(t.c_str());
  int tx = x + (w - tw) / 2;

  TitleStyle st = title_style();
  if (st != TitleStyle::Stripes) {
    // Kolorowy pasek (schematy w stylu Redmond97): jednolity albo gradient.
    const Scheme &sc = current_scheme();
    unsigned c1 = active ? sc.title : sc.title_i, c2 = active ? sc.title2 : sc.title_i2;
    int fx = x + 3, fw = w - 6, fy = y + 3, fh = TITLE_H - 5;
    if (st == TitleStyle::Solid || c1 == c2) {
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
    if (boxes && active) {
      title_button(close_x, by, 1, pressed == 1);
      title_button(shade_x, by, 3, pressed == 3);
      if (boxes != 2) title_button(zoom_x, by, 2, pressed == 2);
    }
  } else if (active) {
    // Prążki: ciemna linia nad jasną, przerwane przy tytule i przyciskach.
    auto stripe = [&](int x0, int x1) {
      if (x1 <= x0) return;
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
      title_button(close_x, by, 1, pressed == 1);
      title_button(shade_x, by, 3, pressed == 3);
      if (boxes != 2) {
        stripe(zoom_x + BOX + 4, x + w - 4);
        title_button(zoom_x, by, 2, pressed == 2);
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
  // cień okna (prawa i dolna krawędź), jak w Platinum
  if (!shaded) {
    fl_color(SHADOW);
    fl_xyline(x + 2, y + h, x + w);
    fl_yxline(x + w, y + 2, y + h);
  }
}

void draw_grow_box(int x, int y) {
  // 15x15, dwie zachodzące na siebie ramki - uchwyt zmiany rozmiaru
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
// Pomocnicze
// ---------------------------------------------------------------------------
std::string ellipsize(const std::string &s, int max_w) {
  if (max_w <= 0) return "";
  if (fl_width(s.c_str()) <= max_w) return s;
  std::string t = s;
  const char *ell = "\xE2\x80\xA6";  // "…"
  while (!t.empty()) {
    // usuń ostatni znak UTF-8
    const char *b = t.c_str();
    const char *e = b + t.size();
    const char *p = fl_utf8back(e - 1, b, e);
    t.resize(p - b);
    if (fl_width((t + ell).c_str()) <= max_w) return t + ell;
  }
  return ell;
}

std::string polish_items(int n) {
  const char *word;
  int d = n % 10, dd = n % 100;
  if (n == 1) word = "element";
  else if (d >= 2 && d <= 4 && !(dd >= 12 && dd <= 14)) word = "elementy";
  else word = "elementów";
  return std::to_string(n) + " " + word;
}

} // namespace plat
