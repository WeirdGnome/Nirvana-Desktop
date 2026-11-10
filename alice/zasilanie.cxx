// Nirvana Desktop - alice: panel "Zasilanie"
//
// Wygaszanie i blokada ekranu, usypianie po bezczynności (osobno na baterii
// i z zasilacza), reakcja na krytyczny poziom baterii. Ustawienia zapisuje
// w ~/.config/nirvana/zasilanie.conf, a Rambler stosuje je od razu
// (właściwość _NIRVANA_ZASILANIE na oknie głównym X).
#include "framewin.h"
#include "platinum.h"
#include "power.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>

#include <algorithm>
#include <string>

using namespace plat;

namespace {

FrameWindow *g_win;
Fl_Choice *g_blank, *g_bat, *g_ac, *g_crit;
Fl_Check_Button *g_lock;
power::IdleSettings g_s;

const int BLANK[] = {1, 2, 5, 10, 15, 30, 60, 0};
const int SLEEP[] = {5, 10, 15, 20, 30, 45, 60, 120, 0};

Fl_Choice *minutes_choice(int X, int Y, int W, const char *l, const int *vals, int n, int cur) {
  auto *c = new Fl_Choice(X, Y, W, 22, l);
  int sel = n - 1;
  for (int i = 0; i < n; i++) {
    std::string t = vals[i] ? fmt(_("%1 min"), std::to_string(vals[i])) : _("Never");
    if (vals[i] == 60) t = _("1 hour");
    if (vals[i] == 120) t = _("2 hours");
    c->add(t.c_str());
    if (vals[i] == cur) sel = i;
  }
  c->value(sel);
  c->labelsize(SIZE_SMALL + 1);
  c->textsize(SIZE_SMALL + 1);
  return c;
}

Fl_Box *heading(int X, int Y, const char *t) {
  auto *b = new Fl_Box(X, Y, 300, 20, t);
  b->labelfont(FONT_SYSTEM);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  return b;
}

Fl_Box *note(int X, int Y, int W, int H, const char *t) {
  auto *b = new Fl_Box(X, Y, W, H);
  b->copy_label(t);
  b->labelsize(SIZE_SMALL);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  return b;
}

void save() {
  g_s.blank_min = BLANK[g_blank->value()];
  g_s.lock_on_blank = g_lock->value();
  g_s.sleep_battery_min = SLEEP[g_bat->value()];
  g_s.sleep_ac_min = SLEEP[g_ac->value()];
  g_s.critical_action = g_crit->value();
  power::write_idle(g_s);
  power::notify_settings_changed();
}

}  // namespace

int panel_zasilanie() {
  g_s = power::read_idle();
  power::State st = power::query();
  const int W = 500, H = 380;
  g_win = new FrameWindow(W, H, _("Power"));
  g_win->xclass("nirvana-zasilanie");
  int y = 10;
  heading(15, y, _("Screen"));
  y += 26;
  g_blank = minutes_choice(230, y, 150, _("Turn off screen when idle for:"), BLANK, 8, g_s.blank_min);
  y += 30;
  g_lock = new Fl_Check_Button(226, y, 260, 22, _("Lock screen when it turns off"));
  g_lock->labelsize(SIZE_SMALL + 1);
  g_lock->value(g_s.lock_on_blank);
  y += 22;
  std::string l = power::locker();
  std::string lt = l.empty() ? _("No screen locker - install xsecurelock, i3lock or slock.")
                             : fmt(_("Screen locker: %1."), l.substr(0, l.find(' ')));
  note(230, y, W - 245, 30, lt.c_str());
  y += 36;

  heading(15, y, _("Computer Sleep"));
  y += 26;
  g_bat = minutes_choice(230, y, 150, _("On battery, when idle for:"), SLEEP, 9, g_s.sleep_battery_min);
  y += 30;
  g_ac = minutes_choice(230, y, 150, _("On power adapter, when idle for:"), SLEEP, 9, g_s.sleep_ac_min);
  y += 36;

  heading(15, y, _("Battery"));
  y += 26;
  g_crit = new Fl_Choice(230, y, 150, 22, _("At 3% battery:"));
  g_crit->add(_("Do nothing|Hibernate|Sleep"));
  g_crit->value(g_s.critical_action);
  g_crit->labelsize(SIZE_SMALL + 1);
  g_crit->textsize(SIZE_SMALL + 1);
  y += 28;
  std::string bt = st.percent >= 0 ? fmt(_("Now: %1. A warning appears at 10%."), power::battery_text(st))
                                   : _("This computer has no battery.");
  note(230, y, W - 245, 30, bt.c_str());
  y += 34;

  std::string info = _("Presentation mode (battery menu in the menu bar) suspends screen blanking, locking and sleep.");
  if (!FrameWindow::wm_is_insight())
    info += std::string(" ") + _("In a GNOME/KDE session the screen and sleep are managed by that desktop - these settings apply to the Nirvana Desktop session.");
  note(15, y, W - 30, 44, info.c_str());

  auto button = [&](int X, const char *t) {
    auto *b = new Fl_Button(X, H - 36, 86, 24, t);
    b->box(BUTTON_BOX);
    b->down_box(BUTTON_DOWN);
    return b;
  };
  button(W - 290, _("Cancel"))->callback([](Fl_Widget *, void *) { g_win->hide(); });
  button(W - 196, _("Apply"))->callback([](Fl_Widget *, void *) { save(); });
  auto *ok = button(W - 102, _("OK"));
  ok->labelfont(FONT_SYSTEM);
  ok->shortcut(FL_Enter);
  ok->callback([](Fl_Widget *, void *) { save(); g_win->hide(); });
  g_win->end();
  g_win->finish();
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + g_win->oy(), (sh - H) / 3));
  g_win->show();
  return Fl::run();
}
