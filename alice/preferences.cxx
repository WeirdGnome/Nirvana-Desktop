// Nirvana Desktop - alice: Preferences (the set of control panels)
//
// A window with the panel icons, like the Control Panels folder in Mac OS or the
// Prefs drawer in Workbench. A double click (or a click) opens a panel in its own window.
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
  enum Kind { Appearance, Network, Power } kind;
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
  const int W = 340, H = 196;
  auto *win = new FrameWindow(W, H, _("Preferences"));
  win->xclass("nirvana-preferences");
  struct P { PanelIcon::Kind k; const char *label, *arg; } panels[] = {
      {PanelIcon::Appearance, _("Appearance"), "appearance"},
      {PanelIcon::Network, _("Network"), "network"},
      {PanelIcon::Power, _("Power"), "power"},
  };
  int x = 14;
  for (auto &p : panels) {
    auto *b = new PanelIcon(x, 20, p.k, p.label);
    b->callback([](Fl_Widget *, void *a) { open_panel((const char *)a); }, (void *)p.arg);
    x += 106;
  }

  // Interface language - saved in ~/.config/nirvana/language, applies after a restart
  static std::vector<std::string> codes;
  codes = {""};
  auto *lang = new Fl_Choice(110, 112, W - 124, 22, _("Language:"));
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
  auto *note = new Fl_Box(14, 140, W - 28, 44);
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
