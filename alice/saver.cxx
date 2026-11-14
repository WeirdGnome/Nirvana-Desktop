// Nirvana Desktop - alice: the "Screen Saver" panel
//
// The saver (with a preview in a little monitor), when it starts, whether and when it locks
// the screen, and when the monitor goes to sleep. Saved in ~/.config/nirvana/power.conf
// (shared with the Power panel); Rambler follows at once (_NIRVANA_POWER).
#include "framewin.h"
#include "platinum.h"
#include "power.h"
#include "savers.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <csignal>
#include <string>
#include <unistd.h>
#include <vector>

using namespace plat;

namespace {

NirvanaWindow *g_win;
power::IdleSettings g_s;
Fl_Hold_Browser *g_list;
Fl_Choice *g_start, *g_delay, *g_off;
Fl_Check_Button *g_lock;
std::vector<std::pair<std::string, std::string>> g_savers;

const int START[] = {1, 2, 3, 5, 10, 15, 20, 30, 60, 0};
const int DELAY[] = {0, 1, 2, 5, 10, 15, 30};
const int OFF[] = {5, 10, 15, 30, 60, 0};

// a little monitor with the saver running inside
class Preview : public Fl_Widget {
public:
  savers::Saver *s = nullptr;
  std::string id;
  static constexpr int SX = 14, SY = 12;   // the screen inside the monitor
  Preview(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}
  int sw() const { return w() - 2 * SX; }
  int sh() const { return h() - SY - 30; }
  void set(const std::string &i) {
    if (i == id && s) return;
    id = i;
    delete s;
    s = savers::make(id, sw(), sh());
    redraw();
  }
  void draw() override {
    // the case of the monitor and its foot
    fl_draw_box(UP_BOX, x(), y(), w(), h() - 14, fl_rgb_color(0xD8, 0xD4, 0xC8));
    fl_color(DARK);
    fl_rect(x(), y(), w(), h() - 14);
    fl_draw_box(UP_BOX, x() + w() / 2 - 40, y() + h() - 14, 80, 12, fl_rgb_color(0xD8, 0xD4, 0xC8));
    fl_color(fl_rgb_color(0x3D, 0xB5, 0x4A));
    fl_rectf(x() + w() - 24, y() + h() - 26, 6, 3);
    fl_push_clip(x() + SX, y() + SY, sw(), sh());
    if (s) s->draw(x() + SX, y() + SY);
    fl_pop_clip();
    fl_color(DARK);
    fl_rect(x() + SX - 1, y() + SY - 1, sw() + 2, sh() + 2);
  }
};
Preview *g_prev;

void tick(void *) {
  if (g_prev->s) {
    g_prev->s->step(1.0 / 25);
    g_prev->redraw();
  }
  Fl::repeat_timeout(1.0 / 25, tick);
}

std::string chosen() {
  int v = g_list->value();
  return v > 0 && v <= (int)g_savers.size() ? g_savers[v - 1].first : "stars";
}

Fl_Choice *minutes_choice(int X, int Y, int W, const char *l, const int *vals, int n, int cur, const char *zero) {
  auto *c = new Fl_Choice(X, Y, W, 22, l);
  int sel = n - 1;
  for (int i = 0; i < n; i++) {
    std::string t = vals[i] ? fmt(_("%1 min"), std::to_string(vals[i])) : zero;
    if (vals[i] == 60) t = _("1 hour");
    c->add(t.c_str());
    if (vals[i] == cur) sel = i;
  }
  c->value(sel);
  c->labelsize(SIZE_SMALL + 1);
  c->textsize(SIZE_SMALL + 1);
  return c;
}

void sync_lock() {
  if (g_lock->value()) g_delay->activate();
  else g_delay->deactivate();
}

void save() {
  g_s.saver = chosen();
  g_s.blank_min = START[g_start->value()];
  g_s.lock_on_blank = g_lock->value();
  g_s.lock_delay_min = DELAY[g_delay->value()];
  g_s.monitor_off_min = OFF[g_off->value()];
  power::write_idle(g_s);
  power::notify_settings_changed();
}

void run_lock(bool test) {
  std::string l = power::nirvana_lock();
  if (l.empty()) return;
  std::string id = chosen();
  if (fork() == 0) {
    setsid();
    setenv("NIRVANA_SAVER", id.c_str(), 1);
    if (test) setenv("NIRVANA_SAVER_TEST", "1", 1);
    if (test) execl(l.c_str(), "nirvana-lock", "--saver", (char *)nullptr);
    else execl(l.c_str(), "nirvana-lock", (char *)nullptr);
    _exit(127);
  }
}

Fl_Button *button(int X, int Y, int W, const char *t) {
  auto *b = new Fl_Button(X, Y, W, 24, t);
  b->box(BUTTON_BOX);
  b->down_box(BUTTON_DOWN);
  b->labelsize(SIZE_SMALL + 1);
  return b;
}

}  // namespace

int panel_saver() {
  signal(SIGCHLD, SIG_IGN);   // the test and lock children
  g_s = power::read_idle();
  g_savers = savers::list();
  const int W = 580, H = 456;
  g_win = new NirvanaWindow(W, H, _("Screen Saver"));
  g_win->xclass("nirvana-saver");

  g_list = new Fl_Hold_Browser(15, 14, 180, 214);
  g_list->box(WELL_BOX);
  g_list->textfont(FONT_SMALL);
  g_list->textsize(SIZE_SMALL + 1);
  for (size_t i = 0; i < g_savers.size(); i++) {
    g_list->add(g_savers[i].second.c_str());
    if (g_savers[i].first == g_s.saver) g_list->value((int)i + 1);
  }
  if (!g_list->value()) g_list->value(2);
  g_list->callback([](Fl_Widget *, void *) { g_prev->set(chosen()); });

  g_prev = new Preview(215, 14, 350, 214);
  g_prev->set(chosen());

  int y = 244, lx = 280;
  g_start = minutes_choice(lx, y, 180, _("Start after:"), START, 10, g_s.blank_min, _("Never"));
  y += 32;
  g_lock = new Fl_Check_Button(lx - 4, y, W - lx - 11, 22, _("Lock the screen with the saver"));
  g_lock->labelsize(SIZE_SMALL + 1);
  g_lock->value(g_s.lock_on_blank);
  g_lock->callback([](Fl_Widget *, void *) { sync_lock(); });
  y += 28;
  g_delay = minutes_choice(lx, y, 180, _("Ask for the password after:"), DELAY, 7, g_s.lock_delay_min, _("At once"));
  sync_lock();
  y += 32;
  g_off = minutes_choice(lx, y, 180, _("Monitor to sleep after:"), OFF, 6, g_s.monitor_off_min, _("Never"));
  y += 28;
  auto *n = new Fl_Box(15, y, W - 30, 34);
  n->labelsize(SIZE_SMALL);
  n->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  static std::string note;
  note = power::nirvana_lock().empty()
             ? std::string(_("nirvana-lock is missing (built without PAM) - the screen only goes black."))
             : std::string(_("The monitor time counts from the start of the saver. Presentation mode (battery menu) stops the saver."));
  n->label(note.c_str());

  button(15, H - 36, 90, _("Test"))->callback([](Fl_Widget *, void *) { run_lock(true); });
  button(111, H - 36, 110, _("Lock Now"))->callback([](Fl_Widget *, void *) { run_lock(false); });
  button(W - 290, H - 36, 86, _("Cancel"))->callback([](Fl_Widget *, void *) { g_win->hide(); });
  button(W - 196, H - 36, 86, _("Apply"))->callback([](Fl_Widget *, void *) { save(); });
  auto *ok = button(W - 102, H - 36, 86, _("OK"));
  ok->labelfont(FONT_SYSTEM);
  ok->shortcut(FL_Enter);
  ok->callback([](Fl_Widget *, void *) { save(); g_win->hide(); });
  g_win->end();
  g_win->finish();
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + g_win->oy(), (sh - H) / 4));
  g_win->show();
  Fl::add_timeout(1.0 / 25, tick);
  return Fl::run();
}
