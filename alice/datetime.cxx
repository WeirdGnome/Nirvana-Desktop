// Nirvana Desktop - alice: the "Date & Time" panel
//
// The clock in the menu bar (24-hour, seconds, date, weekday), the pomodoro and
// timer settings (~/.config/nirvana/clock.conf - the clock follows them at once),
// and the system settings: the time zone, the date and time, network time. Those
// need root: nirvana-time-helper through pkexec (administrators at the computer are
// not asked for a password - the polkit rule of Nirvana Desktop).
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Spinner.H>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace plat;

namespace {

FrameWindow *g_win;
ClockSettings g_c;
Fl_Check_Button *g_h24, *g_sec, *g_date, *g_wday, *g_sound, *g_auto;
Fl_Spinner *g_work, *g_short, *g_long, *g_rounds;
Fl_Box *g_preview, *g_zone_now, *g_sync, *g_msg;
Fl_Choice *g_region;
Fl_Hold_Browser *g_cities;
Fl_Input *g_date_in, *g_time_in;
std::map<std::string, std::vector<std::string>> g_zones;   // region -> cities (Area/City without the region)
std::vector<std::string> g_regions;

std::string exe_dir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return "";
  buf[n] = 0;
  std::string p = buf;
  return p.substr(0, p.rfind('/'));
}

std::string helper() {
  std::string e = exe_dir();
  for (const std::string &c : {e + "/../libexec/nirvana-time-helper", e + "/../update/nirvana-time-helper",
                               std::string("/usr/libexec/nirvana-time-helper")})
    if (!access(c.c_str(), R_OK)) return c;
  return "";
}

// runs the helper as root and returns its exit code; the message goes to *out
int run_helper(const std::vector<std::string> &args, std::string *out) {
  std::string h = helper();
  if (h.empty()) { *out = _("nirvana-time-helper is missing."); return 127; }
  int p[2];
  if (pipe(p)) return 127;
  pid_t pid = fork();
  if (pid == 0) {
    dup2(p[1], 1);
    dup2(p[1], 2);
    close(p[0]);
    close(p[1]);
    static std::string pk = plat::pkexec_command();
    std::vector<char *> a = {(char *)pk.c_str(), (char *)h.c_str()};
    for (auto &s : args) a.push_back((char *)s.c_str());
    a.push_back(nullptr);
    execvp(a[0], a.data());
    _exit(127);
  }
  close(p[1]);
  char buf[512];
  ssize_t n;
  out->clear();
  // the window stays alive while polkit asks for a password
  while ((n = read(p[0], buf, sizeof buf)) != 0) {
    if (n > 0) out->append(buf, (size_t)n);
    Fl::check();
  }
  close(p[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  while (!out->empty() && (out->back() == '\n' || out->back() == ' ')) out->pop_back();
  return WIFEXITED(st) ? WEXITSTATUS(st) : 127;
}

std::string current_zone() {
  char buf[512];
  ssize_t n = readlink("/etc/localtime", buf, sizeof buf - 1);
  if (n > 0) {
    buf[n] = 0;
    std::string p = buf;
    size_t z = p.find("zoneinfo/");
    if (z != std::string::npos) return p.substr(z + 9);
  }
  std::ifstream in("/etc/timezone");
  std::string s;
  std::getline(in, s);
  if (!s.empty()) return s;
  const char *tz = getenv("TZ");
  return tz && *tz ? tz : "UTC";
}

void load_zones() {
  std::set<std::string> all;
  for (const char *f : {"/usr/share/zoneinfo/zone1970.tab", "/usr/share/zoneinfo/zone.tab"}) {
    std::ifstream in(f);
    std::string l;
    while (std::getline(in, l)) {
      if (l.empty() || l[0] == '#') continue;
      std::istringstream ss(l);
      std::string cc, coord, tz;
      ss >> cc >> coord >> tz;
      if (!tz.empty()) all.insert(tz);
    }
  }
  all.insert("UTC");
  for (auto &z : all) {
    size_t s = z.find('/');
    std::string region = s == std::string::npos ? "UTC" : z.substr(0, s);
    g_zones[region].push_back(s == std::string::npos ? z : z.substr(s + 1));
  }
  for (auto &[r, v] : g_zones) g_regions.push_back(r);
}

std::string pretty(std::string city) {
  std::replace(city.begin(), city.end(), '_', ' ');
  size_t s;
  while ((s = city.find('/')) != std::string::npos) city.replace(s, 1, " – ");
  return city;
}

void fill_cities(const std::string &select) {
  g_cities->clear();
  int r = g_region->value();
  if (r < 0 || r >= (int)g_regions.size()) return;
  auto &v = g_zones[g_regions[r]];
  for (size_t i = 0; i < v.size(); i++) {
    g_cities->add(pretty(v[i]).c_str());
    if (v[i] == select) { g_cities->value((int)i + 1); g_cities->middleline((int)i + 1); }
  }
}

std::string selected_zone() {
  int r = g_region->value(), c = g_cities->value();
  if (r < 0 || c <= 0) return "";
  const std::string &region = g_regions[r];
  const std::string &city = g_zones[region][c - 1];
  return region == "UTC" && city == "UTC" ? "UTC" : region + "/" + city;
}

void show_zone() {
  static std::string t;
  time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);
  char off[48];
  long o = tm.tm_gmtoff;
  snprintf(off, sizeof off, "UTC%c%02ld:%02ld", o < 0 ? '-' : '+', labs(o) / 3600, labs(o) % 3600 / 60);
  t = fmt(_("Current time zone: %1 (%2)"), current_zone(), off);
  g_zone_now->label(t.c_str());
}

void message(const std::string &m) {
  static std::string t;
  t = m;
  g_msg->label(t.c_str());
  g_msg->redraw();
}

// network time: systemd says it directly, otherwise a running time service is enough
std::string sync_state() {
  if (!access("/run/systemd/system", F_OK)) {
    FILE *p = popen("timedatectl show -p NTP -p NTPSynchronized 2>/dev/null", "r");
    std::string out;
    if (p) {
      char b[256];
      while (fgets(b, sizeof b, p)) out += b;
      pclose(p);
    }
    if (out.find("NTP=yes") != std::string::npos)
      return out.find("NTPSynchronized=yes") != std::string::npos ? _("Set automatically from the network (synchronized).")
                                                                   : _("Set automatically from the network (not synchronized yet).");
    if (!out.empty()) return _("Network time is off.");
  }
  for (const char *svc : {"chronyd", "ntpd", "openntpd", "ntpsec", "systemd-timesyncd", "busybox-ntpd"}) {
    std::string cmd = std::string("pidof ") + svc + " >/dev/null 2>&1";
    if (system(cmd.c_str()) == 0) return fmt(_("Set automatically from the network by %1."), svc);
  }
  return _("No network time service is running - install chrony or ntpsec to keep the clock exact.");
}

void preview(void * = nullptr) {
  static std::string t;
  ClockSettings c = g_c;
  c.h24 = g_h24->value();
  c.seconds = g_sec->value();
  c.date = g_date->value();
  c.weekday = g_wday->value();
  t = fmt(_("In the menu bar: %1"), clock_text(c, time(nullptr), c.seconds));
  g_preview->label(t.c_str());
  g_preview->redraw();
  show_zone();
  g_zone_now->redraw();
}

void tick(void *) {
  preview();
  Fl::repeat_timeout(1.0, tick);
}

void save() {
  g_c.h24 = g_h24->value();
  g_c.seconds = g_sec->value();
  g_c.date = g_date->value();
  g_c.weekday = g_wday->value();
  g_c.work = (int)g_work->value();
  g_c.short_break = (int)g_short->value();
  g_c.long_break = (int)g_long->value();
  g_c.rounds = (int)g_rounds->value();
  g_c.sound = g_sound->value();
  g_c.auto_next = g_auto->value();
  write_clock(g_c);   // the clock notices the file and follows it
}

Fl_Box *heading(int X, int Y, const char *t) {
  auto *b = new Fl_Box(X, Y, 260, 20, t);
  b->labelfont(FONT_SYSTEM);
  b->labelsize(SIZE_SYSTEM);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  return b;
}

Fl_Check_Button *checkbox(int X, int Y, const char *t, bool v) {
  auto *c = new Fl_Check_Button(X, Y, 240, 20, t);
  c->labelsize(SIZE_SMALL + 1);
  c->value(v);
  c->callback([](Fl_Widget *, void *) { preview(); });
  return c;
}

Fl_Spinner *spinner(int X, int Y, const char *label, int lo, int hi, int v) {
  auto *s = new Fl_Spinner(X, Y, 64, 22, label);
  s->range(lo, hi);
  s->step(1);
  s->value(v);
  s->labelsize(SIZE_SMALL + 1);
  s->textsize(SIZE_SMALL + 1);
  return s;
}

Fl_Button *button(int X, int Y, int W, const char *t) {
  auto *b = new Fl_Button(X, Y, W, 24, t);
  b->box(BUTTON_BOX);
  b->down_box(BUTTON_DOWN);
  b->labelsize(SIZE_SMALL + 1);
  return b;
}

}  // namespace

int panel_datetime() {
  g_c = read_clock();
  load_zones();
  const int W = 620, H = 480;
  g_win = new FrameWindow(W, H, _("Date & Time"));
  g_win->xclass("nirvana-datetime");

  // --- left: the clock and the timers
  int x = 15, y = 10;
  heading(x, y, _("Clock in the Menu Bar"));
  y += 24;
  g_h24 = checkbox(x + 4, y, _("24-hour clock"), g_c.h24); y += 22;
  g_sec = checkbox(x + 4, y, _("Show seconds"), g_c.seconds); y += 22;
  g_date = checkbox(x + 4, y, _("Show the date"), g_c.date); y += 22;
  g_wday = checkbox(x + 4, y, _("Show the day of the week"), g_c.weekday); y += 26;
  g_preview = new Fl_Box(x + 4, y, 270, 20);
  g_preview->labelsize(SIZE_SMALL + 1);
  g_preview->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  y += 34;
  heading(x, y, _("Pomodoro and Timer"));
  y += 26;
  g_work = spinner(x + 196, y, _("Work (minutes):"), 1, 180, g_c.work); y += 28;
  g_short = spinner(x + 196, y, _("Short break (minutes):"), 1, 60, g_c.short_break); y += 28;
  g_long = spinner(x + 196, y, _("Long break (minutes):"), 1, 120, g_c.long_break); y += 28;
  g_rounds = spinner(x + 196, y, _("Long break after rounds:"), 1, 12, g_c.rounds); y += 30;
  g_auto = checkbox(x + 4, y, _("Start the next phase by itself"), g_c.auto_next); y += 22;
  g_sound = checkbox(x + 4, y, _("Play a sound when time is up"), g_c.sound); y += 22;

  // --- right: the system time
  x = 320;
  y = 10;
  heading(x, y, _("Time Zone"));
  y += 22;
  g_zone_now = new Fl_Box(x, y, W - x - 15, 18);
  g_zone_now->labelsize(SIZE_SMALL + 1);
  g_zone_now->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  y += 22;
  g_region = new Fl_Choice(x + 60, y, W - x - 75, 22, _("Region:"));
  g_region->labelsize(SIZE_SMALL + 1);
  g_region->textsize(SIZE_SMALL + 1);
  for (auto &r : g_regions) g_region->add(r.c_str());
  std::string cur = current_zone();
  if (cur == "Etc/UTC" || cur == "Etc/UCT" || cur == "Etc/Universal" || cur == "UCT" || cur == "Universal") cur = "UTC";
  std::string cur_region = cur.find('/') == std::string::npos ? "UTC" : cur.substr(0, cur.find('/'));
  std::string cur_city = cur.find('/') == std::string::npos ? cur : cur.substr(cur.find('/') + 1);
  for (size_t i = 0; i < g_regions.size(); i++) if (g_regions[i] == cur_region) g_region->value((int)i);
  if (g_region->value() < 0 && !g_regions.empty()) g_region->value(0);
  y += 28;
  g_cities = new Fl_Hold_Browser(x, y, W - x - 15, 150);
  g_cities->box(WELL_BOX);
  g_cities->textfont(FONT_SMALL);
  g_cities->textsize(SIZE_SMALL + 1);
  fill_cities(cur_city);
  g_region->callback([](Fl_Widget *, void *) { fill_cities(""); });
  y += 156;
  button(W - 15 - 130, y, 130, _("Set Time Zone"))->callback([](Fl_Widget *, void *) {
    std::string z = selected_zone();
    if (z.empty()) { message(_("Choose a city first.")); return; }
    std::string out;
    message(_("Setting the time zone…"));
    if (run_helper({"timezone", z}, &out) == 0) {
      setenv("TZ", (":" + std::string("/usr/share/zoneinfo/") + z).c_str(), 1);   // this panel sees it at once
      tzset();
      message(fmt(_("The time zone is now %1."), z));
    } else {
      message(out.empty() ? _("The time zone was not changed.") : out);
    }
    preview();
  });
  y += 36;
  heading(x, y, _("Date and Time"));
  y += 24;
  time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);
  char d[32], t[32];
  strftime(d, sizeof d, "%Y-%m-%d", &tm);
  strftime(t, sizeof t, "%H:%M:%S", &tm);
  g_date_in = new Fl_Input(x + 40, y, 96, 22, _("Date:"));
  g_date_in->value(d);
  g_date_in->labelsize(SIZE_SMALL + 1);
  g_date_in->textsize(SIZE_SMALL + 1);
  g_time_in = new Fl_Input(x + 182, y, 76, 22, _("Time:"));
  g_time_in->value(t);
  g_time_in->labelsize(SIZE_SMALL + 1);
  g_time_in->textsize(SIZE_SMALL + 1);
  button(W - 15 - 40, y - 1, 40, _("Set"))->callback([](Fl_Widget *, void *) {
    std::string v = std::string(g_date_in->value()) + " " + g_time_in->value();
    std::string out;
    message(_("Setting the date and time…"));
    message(run_helper({"time", v}, &out) == 0 ? std::string(_("The date and time were set."))
                                               : out.empty() ? std::string(_("The date and time were not changed.")) : out);
  });
  y += 30;
  g_sync = new Fl_Box(x, y, W - x - 15, 34);
  g_sync->labelsize(SIZE_SMALL);
  g_sync->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  static std::string sync;
  sync = sync_state();
  g_sync->label(sync.c_str());
  y += 38;
  g_msg = new Fl_Box(x, y, W - x - 15, 34);
  g_msg->labelsize(SIZE_SMALL);
  g_msg->labelfont(FONT_SYSTEM);
  g_msg->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  auto *note = new Fl_Box(15, H - 74, 290, 36);
  note->label(_("The time zone and the date need administrator rights; administrators at the computer are not asked for a password."));
  note->labelsize(SIZE_SMALL);
  note->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  button(W - 290, H - 36, 86, _("Cancel"))->callback([](Fl_Widget *, void *) { g_win->hide(); });
  button(W - 196, H - 36, 86, _("Apply"))->callback([](Fl_Widget *, void *) { save(); });
  auto *ok = button(W - 102, H - 36, 86, _("OK"));
  ok->labelfont(FONT_SYSTEM);
  ok->shortcut(FL_Enter);
  ok->callback([](Fl_Widget *, void *) { save(); g_win->hide(); });
  g_win->end();
  g_win->finish();
  preview();
  Fl::add_timeout(1.0, tick);
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + g_win->oy(), (sh - H) / 4));
  g_win->show();
  return Fl::run();
}
