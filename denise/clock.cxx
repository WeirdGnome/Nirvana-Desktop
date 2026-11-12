// Nirvana Desktop - the clock in the menu bar, with a calendar, a pomodoro,
// a timer and a stopwatch
//
// The clock shows the time (and, as set in Preferences > Date & Time, the seconds,
// the date and the weekday). A click opens a popup with the full date, a month
// calendar and three tools; a running tool shows its time in the menu bar next to
// the clock, so it stays in sight with the popup closed. When a timer or a pomodoro
// phase ends, a sound plays and a window says so.
#include "applets.h"
#include "applet_ui.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/fl_utf8.h>
#include <FL/fl_draw.H>

#include <cmath>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace applets {

namespace {

double mono() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

std::string mmss(double sec, bool tenths = false) {
  if (sec < 0) sec = 0;
  long s = (long)sec;
  char b[32];
  if (s >= 3600) snprintf(b, sizeof b, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
  else if (tenths) snprintf(b, sizeof b, "%02ld:%02ld.%ld", s / 60, s % 60, (long)((sec - s) * 10));
  else snprintf(b, sizeof b, "%02ld:%02ld", s / 60, s % 60);
  return b;
}

// the first letter in upper case ("środa" -> "Środa", also for Polish letters)
std::string capitalize(const std::string &s) {
  if (s.empty()) return s;
  int len = 0;
  unsigned c = fl_utf8decode(s.c_str(), s.c_str() + s.size(), &len);
  char buf[8];
  int n = fl_utf8encode(fl_toupper(c), buf);
  return std::string(buf, n) + s.substr(len);
}

ClockSettings g_cfg;
time_t g_cfg_mtime = 0;

void reload_settings() {
  struct stat st;
  time_t m = stat(clock_file().c_str(), &st) == 0 ? st.st_mtime : 0;
  if (m == g_cfg_mtime && g_cfg_mtime) return;
  g_cfg_mtime = m ? m : 1;
  g_cfg = read_clock();
}

// --- a countdown (the timer, the pomodoro phases) -------------------------------------
struct Countdown {
  double total = 0;         // seconds
  double left = 0;          // when paused
  double end = 0;           // monotonic time of the end, when running
  bool running = false;
  bool active() const { return running || (left > 0 && left < total); }
  double remaining() const { return running ? std::max(0.0, end - mono()) : left; }
  void set(double s) { total = left = s; running = false; }
  void start() { if (left <= 0) left = total; end = mono() + left; running = true; }
  void pause() { if (running) { left = remaining(); running = false; } }
  void reset() { left = total; running = false; }
};

enum Mode { Pomodoro, Timer, Stopwatch };
Mode g_mode = Pomodoro;

// pomodoro
enum Phase { Work, ShortBreak, LongBreak };
Phase g_phase = Work;
int g_round = 1;            // the work round, 1..rounds
Countdown g_pomo;
bool g_pomo_started = false;

// timer
Countdown g_timer;
int g_timer_minutes = 5;

// stopwatch
bool g_sw_running = false;
double g_sw_start = 0, g_sw_acc = 0;
std::vector<double> g_laps;
double sw_elapsed() { return g_sw_acc + (g_sw_running ? mono() - g_sw_start : 0); }

int phase_minutes(Phase p) {
  return p == Work ? g_cfg.work : p == ShortBreak ? g_cfg.short_break : g_cfg.long_break;
}
std::string phase_name(Phase p) {
  return p == Work ? _("Work") : p == ShortBreak ? _("Short break") : _("Long break");
}

void play_sound() {
  if (!g_cfg.sound) return;
  if (fork() == 0) {
    setsid();
    if (fork() == 0) {
      const char *script =
          "for f in /usr/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga "
          "/usr/share/sounds/freedesktop/stereo/complete.oga; do "
          "[ -r \"$f\" ] || continue; "
          "command -v paplay >/dev/null 2>&1 && exec paplay \"$f\"; "
          "command -v pw-play >/dev/null 2>&1 && exec pw-play \"$f\"; "
          "command -v ogg123 >/dev/null 2>&1 && exec ogg123 -q \"$f\"; done; exit 1";
      execl("/bin/sh", "sh", "-c", script, (char *)nullptr);
      _exit(1);
    }
    _exit(0);
  }
  while (waitpid(-1, nullptr, WNOHANG) > 0) {}
  fl_beep(FL_BEEP_DEFAULT);   // at least the bell (also when no sound player is installed)
}

class ClockApplet;
ClockApplet *g_clock = nullptr;
void notify(bool structure);

void start_phase(Phase p) {
  g_phase = p;
  g_pomo.set(phase_minutes(p) * 60.0);
  g_pomo.start();
  g_pomo_started = true;
  notify(true);
}

Phase next_phase() {
  if (g_phase != Work) return Work;
  return g_round >= g_cfg.rounds ? LongBreak : ShortBreak;
}

void close_popup();

void phase_finished() {
  close_popup();   // the popup holds the mouse - the message window must get the clicks
  Phase done = g_phase, next = next_phase();
  if (done != Work) g_round = done == LongBreak ? 1 : g_round + 1;
  play_sound();
  g_phase = next;
  g_pomo.set(phase_minutes(next) * 60.0);
  notify(true);
  if (g_cfg.auto_next) { start_phase(next); return; }
  std::string text = done == Work ? fmt(_("The work session is over. Time for a break (%1 min)."),
                                        std::to_string(phase_minutes(next)))
                                  : fmt(_("The break is over. Ready for work round %1 of %2?"),
                                        std::to_string(g_round), std::to_string(g_cfg.rounds));
  const Host &h = current_host();
  std::string start = next == Work ? _("Start Work") : _("Start Break");
  if (h.dialog) h.dialog(_("Pomodoro"), text, {{_("Later"), nullptr}, {start, [next] { start_phase(next); }}});
}

void timer_finished() {
  close_popup();
  g_timer.reset();
  play_sound();
  notify(true);
  const Host &h = current_host();
  if (h.dialog)
    h.dialog(_("Timer"), fmt(_("The timer has finished (%1)."), mmss(g_timer.total)), {{_("OK"), nullptr}});
}

// --- the month calendar ------------------------------------------------------------------
class Calendar : public Fl_Widget {
public:
  int year, month;   // shown
  Calendar(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) { today(); }
  void today() {
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    year = tm.tm_year + 1900;
    month = tm.tm_mon;
  }
  static bool monday_first() {
    const char *l = getenv("LC_TIME");
    if (!l || !*l) l = getenv("LANG");
    return !(l && (!strncmp(l, "en_US", 5) || !strncmp(l, "en_CA", 5) || !strncmp(l, "ja", 2)));
  }
  void draw() override {
    fl_color(GRAY);
    fl_rectf(x(), y(), w(), h());
    // header: < Month Year >
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    fl_color(TEXT);
    std::string head = capitalize(month_name(month)) + " " + std::to_string(year);
    fl_draw(head.c_str(), x(), y(), w(), 18, FL_ALIGN_CENTER);
    fl_polygon(x() + 8, y() + 9, x() + 14, y() + 4, x() + 14, y() + 14);
    fl_polygon(x() + w() - 8, y() + 9, x() + w() - 14, y() + 4, x() + w() - 14, y() + 14);
    bool mon = monday_first();
    int cw = w() / 7, ch = 16, gx = x() + (w() - 7 * cw) / 2, gy = y() + 22;
    fl_font(FONT_SMALL, SIZE_SMALL);
    for (int i = 0; i < 7; i++) {
      int wd = mon ? (i + 1) % 7 : i;
      fl_color(wd == 0 || wd == 6 ? fl_color_average(TEXT, GRAY, 0.6f) : TEXT);
      fl_draw(weekday_letters(wd).c_str(), gx + i * cw, gy, cw, ch, FL_ALIGN_CENTER);
    }
    fl_color(SHADOW);
    fl_xyline(gx, gy + ch, gx + 7 * cw - 1);
    struct tm first = {};
    first.tm_year = year - 1900;
    first.tm_mon = month;
    first.tm_mday = 1;
    first.tm_hour = 12;
    mktime(&first);
    int start = mon ? (first.tm_wday + 6) % 7 : first.tm_wday;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int n = days[month] + (month == 1 && ((year % 4 == 0 && year % 100) || year % 400 == 0));
    time_t t = time(nullptr);
    struct tm now;
    localtime_r(&t, &now);
    fl_font(FONT_SMALL, SIZE_SMALL + 1);
    for (int d = 1; d <= n; d++) {
      int cell = start + d - 1, cx = gx + (cell % 7) * cw, cy = gy + ch + 3 + (cell / 7) * ch;
      bool is_today = d == now.tm_mday && month == now.tm_mon && year == now.tm_year + 1900;
      int wd = (first.tm_wday + d - 1) % 7;
      if (is_today) {
        fl_color(SEL);
        fl_rectf(cx + 2, cy, cw - 4, ch - 1);
        fl_color(SEL_TEXT);
      } else {
        fl_color(wd == 0 || wd == 6 ? fl_color_average(TEXT, GRAY, 0.6f) : TEXT);
      }
      fl_draw(std::to_string(d).c_str(), cx, cy, cw, ch - 1, FL_ALIGN_CENTER);
    }
  }
  int handle(int e) override {
    if (e == FL_PUSH) {
      int ex = Fl::event_x() - x(), ey = Fl::event_y() - y();
      if (ey < 18) {
        if (ex < 24) { if (--month < 0) { month = 11; year--; } redraw(); return 1; }
        if (ex > w() - 24) { if (++month > 11) { month = 0; year++; } redraw(); return 1; }
        today();   // a click on the title: back to this month
        redraw();
        return 1;
      }
      return 1;
    }
    if (e == FL_MOUSEWHEEL) {
      if (!Fl::event_inside(this) || !Fl::event_dy()) return 0;
      month += Fl::event_dy() > 0 ? 1 : -1;
      if (month < 0) { month = 11; year--; }
      if (month > 11) { month = 0; year++; }
      redraw();
      return 1;
    }
    return Fl_Widget::handle(e);
  }
};

// A small button as in the popups (Platinum rounded)
Fl_Button *small_button(int X, int Y, int W, const char *label) {
  auto *b = new Fl_Button(X, Y, W, 20);
  b->copy_label(label);
  b->box(BUTTON_BOX);
  b->down_box(BUTTON_DOWN);
  b->color(GRAY);
  b->labelsize(SIZE_SMALL + 1);
  return b;
}

class ClockApplet : public Fl_Widget {
public:
  Popup *pop = nullptr;
  Fl_Box *time_box = nullptr, *display = nullptr, *status = nullptr, *laps = nullptr;
  std::function<void()> relayout;
  std::string last_text;

  ClockApplet(int X, int Y, int H) : Fl_Widget(X, Y, 10, H) {
    g_clock = this;
    reload_settings();
    g_timer.set(g_timer_minutes * 60.0);
    g_pomo.set(g_cfg.work * 60.0);
    size(needed_width(), H);
  }

  // what is shown next to the clock: the running (or paused) tool
  std::string tool_text(bool *paused, int *kind) const {
    *paused = false;
    *kind = -1;
    if (g_pomo_started && g_pomo.active()) {
      *kind = g_phase == Work ? 0 : 1;
      *paused = !g_pomo.running;
      return mmss(g_pomo.remaining() + 0.999);
    }
    if (g_timer.active()) {
      *kind = 2;
      *paused = !g_timer.running;
      return mmss(g_timer.remaining() + 0.999);
    }
    if (g_sw_running || g_sw_acc > 0) {
      *kind = 3;
      *paused = !g_sw_running;
      return mmss(sw_elapsed());
    }
    return "";
  }

  std::string text() const { return clock_text(g_cfg, time(nullptr), g_cfg.seconds); }

  int needed_width() const {
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    int wdt = (int)fl_width(text().c_str()) + 10;
    bool p;
    int k;
    std::string t = tool_text(&p, &k);
    if (!t.empty()) wdt += (int)fl_width(t.c_str()) + 22;
    return wdt;
  }

  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_draw_box(MENUBAR_BOX, x(), y(), w(), h(), GRAY);
    bool open = pop && pop->shown();
    if (open) { fl_color(SEL); fl_rectf(x(), y(), w(), h() - 1); }
    Fl_Color c = open ? SEL_TEXT : TEXT;
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    std::string t = text();
    int tx = x() + w() - 6 - (int)fl_width(t.c_str());
    fl_color(c);
    fl_draw(t.c_str(), tx, y() + 14);
    bool paused;
    int kind;
    std::string tool = tool_text(&paused, &kind);
    if (!tool.empty()) {
      int ww = (int)fl_width(tool.c_str());
      int ix = tx - ww - 22;
      Fl_Color ic = kind == 0 ? fl_rgb_color(0xE5, 0x48, 0x3F) : kind == 1 ? fl_rgb_color(0x3D, 0xB5, 0x4A) : c;
      fl_color(ic);
      if (kind <= 1) {   // pomodoro: a tomato with a leaf
        fl_pie(ix, y() + 5, 11, 11, 0, 360);
        fl_color(fl_rgb_color(0x3D, 0xB5, 0x4A));
        fl_rectf(ix + 4, y() + 3, 3, 3);
      } else if (kind == 2) {   // timer: an hourglass
        fl_polygon(ix + 1, y() + 4, ix + 10, y() + 4, ix + 5.5, y() + 10);
        fl_polygon(ix + 1, y() + 16, ix + 10, y() + 16, ix + 5.5, y() + 10);
      } else {   // stopwatch: a dial with a button
        fl_arc(ix, y() + 5, 11, 11, 0, 360);
        fl_rectf(ix + 4, y() + 2, 3, 2);
        fl_line(ix + 5, y() + 10, ix + 5, y() + 7);
      }
      fl_color(paused ? fl_color_average(c, GRAY, 0.5f) : c);
      fl_draw(tool.c_str(), ix + 15, y() + 14);
    }
    fl_pop_clip();
  }

  int handle(int e) override {
    if (e == FL_PUSH) {
      if (pop && pop->shown()) { pop->close(); return 1; }
      open_popup();
      return 1;
    }
    if (e == FL_ENTER || e == FL_LEAVE) return 1;
    return Fl_Widget::handle(e);
  }

  // --- the popup -------------------------------------------------------------------------
  void build() {
    const int W = 290;
    pop->begin();
    pop->clear();
    pop->resizable(nullptr);
    pop->begin();
    int y = 8;
    time_box = new Fl_Box(10, y, W - 20, 30);
    time_box->labelfont(FONT_SYSTEM);
    time_box->labelsize(26);
    time_box->labelcolor(TEXT);
    y += 32;
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    // TRANSLATORS: the full date: %1 weekday, %2 day, %3 month, %4 year
    std::string date = fmt(_("%1, %3 %2, %4"), capitalize(weekday_name(tm.tm_wday)), std::to_string(tm.tm_mday),
                           month_in_date(tm.tm_mon), std::to_string(tm.tm_year + 1900));
    auto *d = new Fl_Box(10, y, W - 20, 18);
    d->copy_label(date.c_str());
    d->labelsize(SIZE_SMALL + 1);
    d->labelcolor(TEXT);
    y += 24;
    new Calendar(14, y, W - 28, 22 + 16 + 3 + 6 * 16);
    y += 22 + 16 + 3 + 6 * 16 + 4;
    new Sep(8, y, W - 16);
    y += 10;

    // the three tools as tabs
    const char *tabs[] = {_("Pomodoro"), _("Timer"), _("Stopwatch")};
    int tw = (W - 24) / 3;
    for (int i = 0; i < 3; i++) {
      auto *b = small_button(12 + i * tw, y, tw, tabs[i]);
      b->type(FL_RADIO_BUTTON);
      b->value(g_mode == i);
      b->labelfont(g_mode == i ? FONT_SYSTEM : FONT_SMALL);
      b->callback([](Fl_Widget *, void *m) { g_mode = (Mode)(long)m; notify(true); }, (void *)(long)i);
    }
    y += 28;
    display = new Fl_Box(10, y, W - 20, 30);
    display->labelfont(FONT_SYSTEM);
    display->labelsize(24);
    display->labelcolor(TEXT);
    y += 30;
    status = new Fl_Box(10, y, W - 20, 16);
    status->labelsize(SIZE_SMALL);
    status->labelcolor(TEXT);
    y += 20;
    laps = nullptr;
    auto row = [&](std::vector<std::pair<std::string, Fl_Callback *>> bs) {
      int bw = (W - 24 - 8 * ((int)bs.size() - 1)) / (int)bs.size();
      for (size_t i = 0; i < bs.size(); i++) small_button(12 + (int)i * (bw + 8), y, bw, bs[i].first.c_str())->callback(bs[i].second);
      y += 28;
    };
    if (g_mode == Pomodoro) {
      bool run = g_pomo.running;
      row({{run ? _("Pause") : g_pomo_started && g_pomo.active() ? _("Continue") : _("Start"),
            [](Fl_Widget *, void *) {
              if (g_pomo.running) g_pomo.pause();
              else if (g_pomo_started && g_pomo.active()) g_pomo.start();
              else start_phase(g_phase);
              notify(true);
            }},
           {_("Skip"), [](Fl_Widget *, void *) { g_pomo.running = false; g_pomo.left = 0; phase_finished(); }},
           {_("Reset"), [](Fl_Widget *, void *) {
              g_phase = Work; g_round = 1; g_pomo_started = false; g_pomo.set(g_cfg.work * 60.0); notify(true);
            }}});
    } else if (g_mode == Timer) {
      static const int presets[] = {1, 3, 5, 10, 15, 25, 45, 60};
      int pw = (W - 24) / 8;
      for (int i = 0; i < 8; i++) {
        auto *b = small_button(12 + i * pw, y, pw - 2, std::to_string(presets[i]).c_str());
        b->labelsize(SIZE_SMALL);
        b->callback([](Fl_Widget *, void *m) {
          g_timer_minutes = (int)(long)m;
          g_timer.set(g_timer_minutes * 60.0);
          notify(true);
        }, (void *)(long)presets[i]);
      }
      y += 26;
      bool run = g_timer.running;
      row({{run ? _("Pause") : g_timer.active() ? _("Continue") : _("Start"),
            [](Fl_Widget *, void *) { if (g_timer.running) g_timer.pause(); else g_timer.start(); notify(true); }},
           {_("+1 min"), [](Fl_Widget *, void *) {
              g_timer.total += 60; g_timer.left += 60; if (g_timer.running) g_timer.end += 60; notify(false);
            }},
           {_("Reset"), [](Fl_Widget *, void *) { g_timer.reset(); notify(true); }}});
    } else {
      row({{g_sw_running ? _("Stop") : g_sw_acc > 0 ? _("Continue") : _("Start"),
            [](Fl_Widget *, void *) {
              if (g_sw_running) { g_sw_acc = sw_elapsed(); g_sw_running = false; }
              else { g_sw_start = mono(); g_sw_running = true; }
              notify(true);
            }},
           {_("Lap"), [](Fl_Widget *, void *) { if (g_sw_running) g_laps.push_back(sw_elapsed()); notify(true); }},
           {_("Reset"), [](Fl_Widget *, void *) { g_sw_running = false; g_sw_acc = 0; g_laps.clear(); notify(true); }}});
      if (!g_laps.empty()) {
        laps = new Fl_Box(12, y, W - 24, 16 * (int)std::min<size_t>(g_laps.size(), 4));
        laps->labelsize(SIZE_SMALL);
        laps->labelcolor(TEXT);
        laps->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE);
        y += laps->h() + 4;
      }
    }
    new Sep(8, y, W - 16);
    y += 8;
    auto *r = new MenuRow(2, y, W - 4, _("Date & Time Settings…"));
    r->callback([](Fl_Widget *, void *) {
      if (g_clock && g_clock->pop) g_clock->pop->close();
      if (current_host().open_panel) current_host().open_panel("datetime");
    });
    y += 22;
    pop->end();
    pop->size(W, y + 4);
    refresh();
  }

  // the texts that change every moment
  void refresh() {
    if (!pop || !pop->shown() || !time_box) return;
    static std::string t, dsp, st, lp;
    ClockSettings c = g_cfg;
    c.weekday = c.date = false;
    t = clock_text(c, time(nullptr), true);
    time_box->label(t.c_str());
    if (g_mode == Pomodoro) {
      dsp = mmss(g_pomo.remaining() + (g_pomo.running ? 0.999 : 0));
      st = fmt(_("%1 · round %2 of %3"), phase_name(g_phase), std::to_string(g_round), std::to_string(g_cfg.rounds));
      if (g_pomo_started && !g_pomo.running && g_pomo.active()) st += " · " + std::string(_("paused"));
    } else if (g_mode == Timer) {
      dsp = mmss(g_timer.remaining() + (g_timer.running ? 0.999 : 0));
      st = g_timer.running ? _("running") : g_timer.active() ? _("paused") : _("Choose minutes, then Start");
    } else {
      dsp = mmss(sw_elapsed(), true);
      st = g_laps.empty() ? "" : fmt(trn((long)g_laps.size(), "%1 lap", "%1 laps"), std::to_string(g_laps.size()));
      if (laps) {
        lp.clear();
        size_t from = g_laps.size() > 4 ? g_laps.size() - 4 : 0;
        for (size_t i = g_laps.size(); i-- > from;) {
          double prev = i ? g_laps[i - 1] : 0;
          lp += fmt(_("Lap %1: %2  (+%3)"), std::to_string(i + 1), mmss(g_laps[i], true), mmss(g_laps[i] - prev, true)) + "\n";
        }
        laps->label(lp.c_str());
      }
    }
    display->label(dsp.c_str());
    status->label(st.c_str());
    pop->redraw();
  }

  void open_popup() {
    if (!pop) {
      pop = new Popup(290, 100);
      pop->on_close = [this] {
        for (int i = 0; i < pop->children(); i++)
          if (auto *r = dynamic_cast<MenuRow *>(pop->child(i))) r->hover(false);
        time_box = nullptr;
        redraw();
      };
      pop->track_hover = [this] { update_hover(pop); };
    }
    build();
    pop->open_at(window()->x_root() + x() + w() - pop->w(), window()->y_root() + MENUBAR_H);
    refresh();
    redraw();
  }

  // a new width (another format, a tool started or stopped): the menu bar is laid out again
  void fit() {
    int nw = needed_width();
    if (nw != w()) {
      size(nw, h());
      if (relayout) relayout();
    }
  }

  void sync(bool structure) {
    fit();
    redraw();
    if (pop && pop->shown()) {
      if (structure) {
        int right = pop->x() + pop->w();
        build();
        pop->position(right - pop->w(), pop->y());
        pop->redraw();
      } else {
        refresh();
      }
    }
  }
};

void close_popup() {
  if (g_clock && g_clock->pop && g_clock->pop->shown()) g_clock->pop->close();
}

void notify(bool structure) {
  if (!g_clock) return;
  if (!structure) { g_clock->sync(false); return; }
  // rebuilding deletes the buttons - not inside the callback of one of them
  static bool pending = false;
  if (pending) return;
  pending = true;
  Fl::add_timeout(0.0, [](void *) {
    pending = false;
    if (g_clock) g_clock->sync(true);
  });
}

void tick(void *) {
  reload_settings();
  if (g_pomo.running && g_pomo.remaining() <= 0) { g_pomo.running = false; g_pomo.left = 0; phase_finished(); }
  if (g_timer.running && g_timer.remaining() <= 0) timer_finished();
  bool busy = g_pomo.running || g_timer.running || g_sw_running;
  bool open = g_clock && g_clock->pop && g_clock->pop->shown();
  if (g_clock) {
    std::string t = g_clock->text();
    bool p;
    int k;
    t += g_clock->tool_text(&p, &k);
    if (t != g_clock->last_text || open) {
      g_clock->last_text = t;
      g_clock->sync(false);
    }
  }
  // often while the stopwatch shows tenths in the open popup, otherwise once a second
  Fl::repeat_timeout(open && g_sw_running && g_mode == Stopwatch ? 0.1 : busy || g_cfg.seconds || open ? 0.25 : 1.0, tick);
}
}  // namespace

Fl_Widget *make_clock(int H, std::function<void()> relayout) {
  auto *c = new ClockApplet(0, 0, H);
  c->relayout = std::move(relayout);
  Fl::add_timeout(0.25, tick);
  return c;
}

}  // namespace applets
