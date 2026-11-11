// Nirvana Desktop - alice: Preferences (the set of control panels)
//
// A window with the panel icons, like a drawer of preference programs. A double click (or a click) opens a panel in its own window.
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace plat;

namespace {

void open_panel(const char *name) {
  // the same program with another argument, in the background (double fork - no zombie)
  pid_t p = fork();
  if (p == 0) {
    if (fork() == 0) {
      char exe[4096];
      ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
      exe[n > 0 ? n : 0] = 0;
      execl(exe, "nirvana-alice", name, (char *)nullptr);
      _exit(127);
    }
    _exit(0);
  }
  if (p > 0) waitpid(p, nullptr, 0);
}

class PanelIcon : public Fl_Button {
public:
  enum Kind { Appearance, Desktop, Network, Power, Shader, DateTime, Printers, User, Saver } kind;
  PanelIcon(int X, int Y, Kind k, const char *l) : Fl_Button(X, Y, 96, 74, l), kind(k) {
    box(FL_NO_BOX);
    labelsize(SIZE_SMALL + 1);
    align(FL_ALIGN_BOTTOM | FL_ALIGN_INSIDE);
  }
  void draw() override {
    bool sel = value() || Fl::focus() == this;
    int ix = x() + (w() - 32) / 2, iy = y() + 6;
    switch (kind) {
      case Appearance: {   // a monitor with a color palette
        fl_draw_box(UP_BOX, ix, iy, 32, 26, GRAY);
        fl_color(DESK_A); fl_rectf(ix + 3, iy + 3, 26, 18);
        const Fl_Color c[] = {SEL, HILITE, ALERT, LIGHT};
        for (int i = 0; i < 4; i++) { fl_color(c[i]); fl_rectf(ix + 5 + i * 6, iy + 12, 5, 6); }
        fl_color(DARK); fl_rect(ix, iy, 32, 26);
        fl_rectf(ix + 10, iy + 27, 12, 3);
        break;
      }
      case Network: draw_icon(Icon::Network, ix, iy, sel); break;
      case Power:
        draw_battery(ix - 2, iy + 6, 70, true, TEXT, 1.6);
        break;
      case Desktop: {   // a little screen: icon column and a window
        fl_color(DESK_A); fl_rectf(ix, iy + 2, 32, 24);
        fl_color(LIGHT);
        for (int i = 0; i < 3; i++) fl_rectf(ix + 2, iy + 5 + i * 7, 5, 4);
        fl_color(GRAY); fl_rectf(ix + 10, iy + 6, 19, 16);
        fl_color(SEL); fl_rectf(ix + 10, iy + 6, 19, 3);
        fl_color(DARK); fl_rect(ix + 10, iy + 6, 19, 16); fl_rect(ix, iy + 2, 32, 24);
        break;
      }
      case Printers: {   // a printer with a sheet coming out
        fl_color(fl_rgb_color(0xF4, 0xF4, 0xF0)); fl_rectf(ix + 8, iy, 16, 9);
        fl_color(DARK); fl_rect(ix + 8, iy, 16, 9);
        fl_color(fl_rgb_color(0xC8, 0xC8, 0xC0)); fl_rectf(ix, iy + 8, 32, 12);
        fl_color(DARK); fl_rect(ix, iy + 8, 32, 12);
        fl_color(fl_rgb_color(0x3D, 0xB5, 0x4A)); fl_rectf(ix + 26, iy + 11, 3, 2);
        fl_color(fl_rgb_color(0xFF, 0xFF, 0xFF)); fl_rectf(ix + 7, iy + 17, 18, 10);
        fl_color(DARK); fl_rect(ix + 7, iy + 17, 18, 10);
        for (int i = 0; i < 3; i++) fl_xyline(ix + 10, iy + 20 + i * 2, ix + 21);
        break;
      }
      case Saver: {   // a monitor with stars on a night screen
        fl_draw_box(UP_BOX, ix, iy, 32, 26, GRAY);
        fl_color(fl_rgb_color(0x10, 0x14, 0x30)); fl_rectf(ix + 3, iy + 3, 26, 18);
        fl_color(fl_rgb_color(0xFF, 0xFF, 0xFF));
        const int st[][2] = {{6, 6}, {12, 14}, {20, 8}, {25, 16}, {16, 5}, {8, 17}, {22, 12}};
        for (auto &p : st) fl_point(ix + p[0], iy + p[1]);
        fl_color(fl_rgb_color(0xF2, 0xC2, 0x30)); fl_pie(ix + 17, iy + 5, 7, 7, 0, 360);
        fl_color(DARK); fl_rect(ix + 3, iy + 3, 26, 18); fl_rect(ix, iy, 32, 26);
        fl_rectf(ix + 10, iy + 27, 12, 3);
        break;
      }
      case User: {   // a portrait in a frame
        fl_draw_box(UP_BOX, ix + 2, iy, 28, 30, GRAY);
        fl_color(fl_rgb_color(0x8C, 0xC8, 0xF0)); fl_rectf(ix + 5, iy + 3, 22, 24);
        fl_color(SEL);
        fl_pie(ix + 11, iy + 6, 10, 10, 0, 360);
        fl_pie(ix + 7, iy + 16, 18, 20, 0, 180);
        fl_color(DARK); fl_rect(ix + 5, iy + 3, 22, 24); fl_rect(ix + 2, iy, 28, 30);
        break;
      }
      case DateTime: {   // a clock face with a calendar page
        fl_color(fl_rgb_color(0xF4, 0xF4, 0xF0));
        fl_rectf(ix + 2, iy + 4, 20, 22);
        fl_color(fl_rgb_color(0xE5, 0x48, 0x3F));
        fl_rectf(ix + 2, iy + 4, 20, 6);
        fl_color(DARK);
        fl_rect(ix + 2, iy + 4, 20, 22);
        for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) fl_rectf(ix + 5 + c * 4, iy + 13 + r * 4, 2, 2);
        fl_color(GRAY);
        fl_pie(ix + 13, iy + 9, 19, 19, 0, 360);
        fl_color(fl_rgb_color(0xFF, 0xFF, 0xFF));
        fl_pie(ix + 14, iy + 10, 17, 17, 0, 360);
        fl_color(DARK);
        fl_arc(ix + 13, iy + 9, 19, 19, 0, 360);
        fl_line(ix + 22, iy + 18, ix + 22, iy + 12);
        fl_line(ix + 22, iy + 18, ix + 27, iy + 20);
        break;
      }
      case Shader: {   // a tube monitor with scanlines over a rainbow
        fl_draw_box(UP_BOX, ix, iy, 32, 26, GRAY);
        const unsigned band[] = {0xE5483F, 0xF29A26, 0xF2C230, 0x3DB54A, 0x3A7BD5, 0x8A4FC8};
        for (int i = 0; i < 6; i++) {
          fl_color(fl_rgb_color(band[i] >> 16, band[i] >> 8 & 255, band[i] & 255));
          fl_rectf(ix + 3, iy + 3 + i * 3, 26, 3);
        }
        fl_color(fl_rgb_color(20, 20, 30));
        for (int yy = iy + 4; yy < iy + 21; yy += 2) fl_xyline(ix + 3, yy, ix + 28);
        fl_color(DARK);
        fl_rect(ix + 3, iy + 3, 26, 18);
        fl_rect(ix, iy, 32, 26);
        fl_rectf(ix + 10, iy + 27, 12, 3);
        break;
      }
    }
    fl_font(FONT_SMALL, SIZE_SMALL + 1);
    int tw = (int)fl_width(label());
    int tx = x() + (w() - tw) / 2, ty = y() + 46;
    fl_color(sel ? ICON_SEL : GRAY);
    fl_rectf(tx - 2, ty, tw + 4, fl_height());
    fl_color(sel ? ICON_SEL_TEXT : TEXT);
    fl_draw(label(), tx, ty + fl_height() - fl_descent());
  }
};

}  // namespace

int panel_preferences() {
  // at most 5 icons in a row, then a new row
  const int PER_ROW = 5, CELL_W = 106, CELL_H = 84;
  const int N = 9;   // the number of panels below
  const int rows = (N + PER_ROW - 1) / PER_ROW;
  const int W = 14 * 2 + PER_ROW * CELL_W - 10, icons_h = 20 + rows * CELL_H;
  const int H = icons_h + 90;
  auto *win = new FrameWindow(W, H, _("Preferences"));
  win->xclass("nirvana-preferences");
  struct P { PanelIcon::Kind k; const char *label, *arg; } panels[] = {
      {PanelIcon::User, _("User"), "user"},
      {PanelIcon::Appearance, _("Appearance"), "appearance"},
      {PanelIcon::Desktop, _("Desktop"), "desktop"},
      {PanelIcon::Network, _("Network"), "network"},
      {PanelIcon::Power, _("Power"), "power"},
      {PanelIcon::Saver, _("Screen Saver"), "saver"},
      {PanelIcon::DateTime, _("Date & Time"), "datetime"},
      {PanelIcon::Printers, _("Printers"), "printers"},
      {PanelIcon::Shader, _("Shader"), "shader"},
  };
  static_assert(sizeof(panels) / sizeof(panels[0]) == N, "N must match the panels");
  for (int i = 0; i < N; i++) {
    auto &p = panels[i];
    auto *b = new PanelIcon(14 + (i % PER_ROW) * CELL_W, 20 + (i / PER_ROW) * CELL_H, p.k, p.label);
    b->callback([](Fl_Widget *, void *a) { open_panel((const char *)a); }, (void *)p.arg);
  }

  // Interface language - saved in ~/.config/nirvana/language, applies after a restart
  static std::vector<std::string> codes;
  codes = {""};
  auto *lang = new Fl_Choice(110, icons_h + 8, W - 124, 22, _("Language:"));
  lang->labelfont(FONT_SYSTEM);
  lang->labelsize(SIZE_SYSTEM);
  lang->textsize(SIZE_SMALL + 1);
  lang->add(_("Automatic (system language)"));
  for (auto &[code, name] : available_languages()) {
    codes.push_back(code);
    lang->add(name.c_str());
  }
  std::string cur = language_setting();
  lang->value(0);
  for (size_t i = 0; i < codes.size(); i++) if (codes[i] == cur) lang->value((int)i);
  auto *note = new Fl_Box(14, icons_h + 36, W - 28, 44);
  note->labelsize(SIZE_SMALL);
  note->align(FL_ALIGN_INSIDE | FL_ALIGN_TOP_LEFT | FL_ALIGN_WRAP);
  note->label(_("The new language takes effect after restarting Nirvana Desktop or after Special > Reset Nirvana."));
  lang->callback([](Fl_Widget *w, void *) {
    int v = ((Fl_Choice *)w)->value();
    if (v >= 0 && v < (int)codes.size()) set_language_setting(codes[v]);
  });
  win->end();
  win->finish();
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + win->oy(), (sh - H) / 4));
  win->show();
  return Fl::run();
}
