// LisaOS - alice: panel "Wygląd".
// Schemat kolorów, styl paska tytułu, kolor
// i wzór biurka, tapeta, animacje okien. Zmiany widać od razu w samym panelu
// (podgląd na żywo); "Zastosuj" zapisuje ~/.config/lisaos/wyglad.conf i
// ustawia właściwość _LISAOS_WYGLAD na oknie głównym X - Rambler i Insight
// przeładowują wtedy paletę bez restartu.
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Color_Chooser.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include <FL/Fl_RGB_Image.H>
#include "framewin.h"
#include <FL/Fl_Hold_Browser.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace plat;

static std::vector<Scheme> g_schemes;
static Appearance g_cur;            // ustawienia widoczne w panelu
static int g_scheme = 0;            // indeks w g_schemes
static FrameWindow *g_win;
static Fl_Hold_Browser *g_list;
static Fl_Box *g_source;
static Fl_Choice *g_style, *g_pattern, *g_wall, *g_wall_mode;
static std::vector<std::string> g_walls;   // pozycje listy tapet (0 = brak, ostatnia = "Inny plik…")
static Fl_Check_Button *g_anim;
static Fl_Button *g_desk_default;

// ---------------------------------------------------------------------------
// Podgląd: miniatura biurka z dwoma oknami, paskiem menu i zaznaczeniem.
// ---------------------------------------------------------------------------
class Preview : public Fl_Widget {
public:
  Preview(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    if (!g_cur.wallpaper.empty()) {
      // tapeta: biurko w wielkości ekranu, zmniejszone do podglądu (układ jak naprawdę)
      uchar r, g, b;
      Fl::get_color(DESK_A, r, g, b);
      std::string key = g_cur.wallpaper + "|" + g_cur.wallpaper_mode + "|" + g_cur.pattern + "|" +
                        std::to_string((r << 16) | (g << 8) | b);
      if (key != key_) {
        key_ = key;
        int sx, sy, sw, sh;
        Fl::screen_xywh(sx, sy, sw, sh);
        std::unique_ptr<Fl_RGB_Image> full(render_desktop(g_cur, sw, sh));
        Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
        img_.reset((Fl_RGB_Image *)full->copy(w(), h()));
      }
      img_->draw(x(), y());
    } else {
      // wzór biurka
      int tw, th;
      unsigned char *tile = pattern_tile(g_cur.pattern, tw, th);
      std::vector<unsigned char> buf((size_t)w() * h() * 3);
      for (int yy = 0; yy < h(); yy++)
        for (int xx = 0; xx < w(); xx++)
          memcpy(&buf[((size_t)yy * w() + xx) * 3], tile + ((yy % th) * tw + xx % tw) * 3, 3);
      delete[] tile;
      fl_draw_image(buf.data(), x(), y(), w(), h(), 3);
    }

    // pasek menu
    fl_push_clip(x(), y(), w(), MENUBAR_H);
    fl_draw_box(MENUBAR_BOX, x(), y(), w(), MENUBAR_H, GRAY);
    fl_pop_clip();
    draw_logo(x() + 8, y() + 2, 16);
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    fl_color(TEXT);
    fl_draw("Lisa", x() + 27, y() + 14);
    int mx = x() + 34 + (int)fl_width("Lisa");
    const char *menus[] = {"Okno", "Ikony", "Narzędzia"};
    for (int i = 0; i < 3; i++) {
      int mw = (int)fl_width(menus[i]) + 14;
      if (i == 1) { fl_color(SEL); fl_rectf(mx, y(), mw, MENUBAR_H - 1); }
      fl_color(i == 1 ? SEL_TEXT : TEXT);
      fl_draw(menus[i], mx + 7, y() + 14);
      mx += mw;
    }

    // okno w tle (nieaktywne)
    int bx = x() + 14, by = y() + 34, bw = 170, bh = 104;
    draw_window(bx, by, bw, bh, "Dokumenty", false);
    // okno na wierzchu (aktywne)
    int fx = x() + 104, fy = y() + 70, fw = w() - 118, fh = h() - 82;
    draw_window(fx, fy, fw, fh, "LisaOS HD", true);

    // ikony dysku i kosza na biurku
    fl_font(FONT_SMALL, SIZE_SMALL);
    label_icon(Icon::Home, x() + 30, y() + h() - 58, "LisaOS HD", false);
    fl_pop_clip();
  }

private:
  std::string key_;
  std::unique_ptr<Fl_RGB_Image> img_;
  static void label_icon(Icon ic, int cx, int iy, const char *txt, bool sel) {
    draw_icon(ic, cx - 16, iy, sel);
    int tw = (int)fl_width(txt);
    int tx = cx - tw / 2, ty = iy + 35;
    fl_color(sel ? ICON_SEL : CONTENT);
    fl_rectf(tx - 2, ty, tw + 4, fl_height());
    fl_color(sel ? ICON_SEL_TEXT : CONTENT_TEXT);
    fl_draw(txt, tx, ty + fl_height() - fl_descent());
  }

  static void draw_window(int X, int Y, int W, int H, const char *title, bool active) {
    draw_frame(X, Y, W, H, active, false);
    draw_titlebar(X, Y, W, title, active, 0, true);
    int cx = X + FRAME, cy = Y + TITLE_H, cw = W - 2 * FRAME, ch = H - TITLE_H - FRAME;
    // pasek informacji
    const int INFO = 16;
    fl_color(GRAY); fl_rectf(cx, cy, cw, INFO);
    fl_color(SHADOW); fl_xyline(cx, cy + INFO - 1, cx + cw - 1);
    fl_font(FONT_SMALL, 10);
    fl_color(TEXT);
    fl_draw(active ? "3 elementy, 12,4 GB wolne" : "2 elementy", cx, cy, cw, INFO - 1, FL_ALIGN_CENTER);
    // treść
    int ty = cy + INFO, th = ch - INFO;
    fl_color(CONTENT); fl_rectf(cx, ty, cw - SCROLL_W, th);
    fl_draw_box(TRACK_BOX, cx + cw - SCROLL_W, ty, SCROLL_W, th, MID);
    if (active) {
      fl_draw_box(THUMB_BOX, cx + cw - SCROLL_W, ty + 8, SCROLL_W, th / 2, HILITE);
      fl_push_clip(cx, ty, cw - SCROLL_W, th);
      fl_font(FONT_SMALL, SIZE_SMALL);
      label_icon(Icon::Folder, cx + 34, ty + 8, "Muzyka", false);
      label_icon(Icon::Document, cx + 98, ty + 8, "List.txt", true);
      label_icon(Icon::Application, cx + 162, ty + 8, "Gra", false);
      fl_pop_clip();
    } else {
      fl_push_clip(cx, ty, cw - SCROLL_W, th);
      fl_font(FONT_SMALL, SIZE_SMALL);
      label_icon(Icon::Folder, cx + 34, ty + 8, "Zdjęcia", false);
      fl_pop_clip();
    }
  }
};

// ---------------------------------------------------------------------------
// Próbka koloru biurka: przycisk otwierający wybór koloru
// ---------------------------------------------------------------------------
class Swatch : public Fl_Button {
public:
  Swatch(int X, int Y, int W, int H) : Fl_Button(X, Y, W, H) { box(BUTTON_BOX); down_box(BUTTON_DOWN); }
  void draw() override {
    Fl_Button::draw();
    fl_draw_box(WELL_BOX, x() + 5, y() + 4, w() - 10, h() - 8, DESK_A);
  }
};

static void preview() {
  if (g_scheme >= 0 && g_scheme < (int)g_schemes.size()) apply(g_schemes[g_scheme], g_cur);
  const Scheme &s = g_schemes[g_scheme];
  static std::string src;
  src = "Źródło: " + (s.source.empty() ? std::string("własny") : s.source);
  if (s.source.find("Redmond97") != std::string::npos) src += " (GPL-3.0)";
  g_source->label(src.c_str());
  g_desk_default->deactivate();
  if (g_cur.desk >= 0) g_desk_default->activate();
  if (g_cur.wallpaper.empty()) g_wall_mode->deactivate();
  else g_wall_mode->activate();
  g_win->redraw();
}

static void fill_list() {
  g_list->clear();
  std::string group;
  for (int i = 0; i < (int)g_schemes.size(); i++) {
    const Scheme &s = g_schemes[i];
    std::string gname = s.id.find('/') == std::string::npos ? "LisaOS" : s.source.empty() ? s.id.substr(0, s.id.find('/')) : s.source;
    if (gname != group) {
      group = gname;
      g_list->add(("@b@." + gname).c_str(), (void *)(long)-1);
    }
    g_list->add(("   " + s.name).c_str(), (void *)(long)i);
    if (s.id == g_cur.scheme) { g_scheme = i; g_list->value(g_list->size()); }
  }
}

static void list_cb(Fl_Widget *, void *) {
  int line = g_list->value();
  if (line <= 0) return;
  long i = (long)g_list->data(line);
  if (i < 0) {   // nagłówek grupy - przejdź do pierwszego schematu pod nim
    if (line < g_list->size()) { g_list->value(line + 1); i = (long)g_list->data(line + 1); }
    else return;
  }
  g_scheme = (int)i;
  g_cur.scheme = g_schemes[i].id;
  preview();
}

static void save() {
  write_appearance(g_cur);
  notify_appearance_changed();
}

int panel_wyglad() {
  g_cur = current_appearance();
  g_schemes = list_schemes();

  const int W = 600, H = 470;
  g_win = new FrameWindow(W, H, "Wygląd");
  g_win->xclass("lisaos-alice");

  auto *t1 = new Fl_Box(15, 8, 220, 20, "Schemat kolorów");
  t1->labelfont(FONT_SYSTEM); t1->labelsize(SIZE_SYSTEM);
  t1->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_list = new Fl_Hold_Browser(15, 30, 220, 372);
  g_list->box(WELL_BOX);
  g_list->textfont(FONT_SMALL);
  g_list->textsize(SIZE_SMALL + 1);
  g_list->callback(list_cb);
  g_list->when(FL_WHEN_CHANGED);
  g_source = new Fl_Box(15, 404, 220, 20);
  g_source->labelsize(10);
  g_source->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  auto *t2 = new Fl_Box(250, 8, 220, 20, "Podgląd");
  t2->labelfont(FONT_SYSTEM); t2->labelsize(SIZE_SYSTEM);
  t2->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  auto *pw = new Fl_Box(250, 30, 335, 215);
  pw->box(WELL_BOX);
  new Preview(252, 32, 331, 211);

  g_style = new Fl_Choice(400, 256, 185, 22, "Pasek tytułu:");
  g_style->add("Jak w schemacie|Prążki (Platinum)|Jednolity kolor|Gradient");
  g_style->value(g_cur.style + 1);
  g_style->callback([](Fl_Widget *, void *) { g_cur.style = g_style->value() - 1; preview(); });

  auto *desk = new Swatch(400, 284, 60, 24);
  desk->label("Kolor biurka:");
  desk->align(FL_ALIGN_LEFT);
  desk->callback([](Fl_Widget *, void *) {
    uchar r, g, b;
    Fl::get_color(DESK_A, r, g, b);
    if (fl_color_chooser("Kolor biurka", r, g, b, 1)) {
      g_cur.desk = (r << 16) | (g << 8) | b;
      preview();
    }
  });
  g_desk_default = new Fl_Button(466, 284, 119, 24, "Ze schematu");
  g_desk_default->box(BUTTON_BOX); g_desk_default->down_box(BUTTON_DOWN);
  g_desk_default->callback([](Fl_Widget *, void *) { g_cur.desk = -1; preview(); });

  g_pattern = new Fl_Choice(400, 314, 185, 22, "Wzór biurka:");
  for (int i = 0; PATTERNS[i]; i++) {
    g_pattern->add(PATTERN_NAMES[i]);
    if (g_cur.pattern == PATTERNS[i]) g_pattern->value(i);
  }
  g_pattern->callback([](Fl_Widget *, void *) { g_cur.pattern = PATTERNS[g_pattern->value()]; preview(); });

  // tapeta: gotowe obrazy (LisaOS, systemowe, ~/Obrazy) albo dowolny plik
  g_wall = new Fl_Choice(400, 344, 185, 22, "Tapeta:");
  g_walls.clear();
  g_walls.push_back("");
  for (auto &p : wallpaper_candidates()) g_walls.push_back(p);
  if (!g_cur.wallpaper.empty() &&
      std::find(g_walls.begin(), g_walls.end(), g_cur.wallpaper) == g_walls.end())
    g_walls.insert(g_walls.begin() + 1, g_cur.wallpaper);
  g_walls.push_back("*");
  auto add_item = [](const std::string &label) {
    int i = g_wall->add("x");             // add() traktuje '/' jako podmenu - tekst wstawiamy osobno
    g_wall->replace(i, label.c_str());
  };
  for (size_t i = 0; i < g_walls.size(); i++) {
    if (i == 0) add_item("Brak (sam wzór)");
    else if (g_walls[i] == "*") add_item("Inny plik…");
    else add_item(wallpaper_title(g_walls[i]));
    if (g_walls[i] == g_cur.wallpaper) g_wall->value((int)i);
  }
  if (g_walls.size() > 2) g_wall->mode(0, FL_MENU_DIVIDER);
  g_wall->mode((int)g_walls.size() - 2, FL_MENU_DIVIDER);
  g_wall->callback([](Fl_Widget *, void *) {
    int i = g_wall->value();
    if (i < 0 || i >= (int)g_walls.size()) return;
    if (g_walls[i] == "*") {
      const char *h = getenv("HOME");
      std::string start = std::string(h ? h : "/") + "/Obrazy/";
      polish_file_chooser();
      const char *f = fl_file_chooser("Wybierz tapetę", "Obrazy (*.{png,jpg,jpeg,gif,bmp,svg,svgz,xpm})",
                                      start.c_str(), 0);
      if (!f) {   // anulowano - wracamy do poprzedniej pozycji
        auto it = std::find(g_walls.begin(), g_walls.end(), g_cur.wallpaper);
        g_wall->value(it == g_walls.end() ? 0 : (int)(it - g_walls.begin()));
        return;
      }
      if (!image_loadable(f)) {
        fl_message_title("Tapeta");
        fl_alert("Nie udało się otworzyć tego obrazu.\nPlik może być uszkodzony albo w nieobsługiwanym formacie.");
        auto it = std::find(g_walls.begin(), g_walls.end(), g_cur.wallpaper);
        g_wall->value(it == g_walls.end() ? 0 : (int)(it - g_walls.begin()));
        return;
      }
      g_cur.wallpaper = f;
      // nowy plik trafia na listę (przed "Inny plik…")
      if (std::find(g_walls.begin(), g_walls.end(), g_cur.wallpaper) == g_walls.end()) {
        g_walls.insert(g_walls.end() - 1, g_cur.wallpaper);
        int pos = (int)g_walls.size() - 2;
        g_wall->insert(pos, "x", 0, nullptr);
        g_wall->replace(pos, wallpaper_title(g_cur.wallpaper).c_str());
      }
      auto it = std::find(g_walls.begin(), g_walls.end(), g_cur.wallpaper);
      g_wall->value((int)(it - g_walls.begin()));
    } else {
      g_cur.wallpaper = g_walls[i];
    }
    preview();
  });
  g_wall_mode = new Fl_Choice(400, 372, 185, 22, "Układ tapety:");
  for (int i = 0; WALLPAPER_MODES[i]; i++) {
    g_wall_mode->add(WALLPAPER_MODE_NAMES[i]);
    if (g_cur.wallpaper_mode == WALLPAPER_MODES[i]) g_wall_mode->value(i);
  }
  g_wall_mode->callback([](Fl_Widget *, void *) {
    g_cur.wallpaper_mode = WALLPAPER_MODES[g_wall_mode->value()];
    preview();
  });

  g_anim = new Fl_Check_Button(396, 402, 190, 22, "Animacje okien");
  g_anim->value(g_cur.animations);
  g_anim->callback([](Fl_Widget *, void *) { g_cur.animations = g_anim->value(); });

  auto button = [](int X, const char *l) {
    auto *b = new Fl_Button(X, H - 36, 86, 24, l);
    b->box(BUTTON_BOX); b->down_box(BUTTON_DOWN);
    return b;
  };
  button(311, "Anuluj")->callback([](Fl_Widget *, void *) { g_win->hide(); });
  button(405, "Zastosuj")->callback([](Fl_Widget *, void *) { save(); });
  auto *ok = button(499, "OK");
  ok->shortcut(FL_Enter);
  ok->callback([](Fl_Widget *, void *) { save(); g_win->hide(); });

  for (Fl_Choice *c : {g_style, g_pattern, g_wall, g_wall_mode}) {
    c->labelsize(SIZE_SMALL + 1);
    c->textsize(SIZE_SMALL + 1);
  }
  desk->labelsize(SIZE_SMALL + 1);
  g_anim->labelsize(SIZE_SMALL + 1);
  g_win->end();
  g_win->finish();   // pod GNOME/KDE: własna ramka Platinum

  fill_list();
  preview();
  // Na środku obszaru roboczego (pod paskiem menu LisaOS albo GNOME).
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + g_win->oy(), (sh - H) / 3));
  g_win->show();
  return Fl::run();
}
