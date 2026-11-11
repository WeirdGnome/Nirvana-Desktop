// Nirvana Desktop - libplatinum: clock settings (~/.config/nirvana/clock.conf) and date names
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/platform.H>
#if defined(FLTK_USE_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sys/stat.h>

namespace plat {

std::string clock_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/clock.conf";
}

ClockSettings read_clock() {
  ClockSettings c;
  std::ifstream in(clock_file());
  std::string line;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    int n = atoi(v.c_str());
    if (k == "h24") c.h24 = n;
    else if (k == "seconds") c.seconds = n;
    else if (k == "date") c.date = n;
    else if (k == "weekday") c.weekday = n;
    else if (k == "work" && n > 0) c.work = std::min(n, 180);
    else if (k == "short_break" && n > 0) c.short_break = std::min(n, 60);
    else if (k == "long_break" && n > 0) c.long_break = std::min(n, 120);
    else if (k == "rounds" && n > 0) c.rounds = std::min(n, 12);
    else if (k == "sound") c.sound = n;
    else if (k == "auto_next") c.auto_next = n;
  }
  return c;
}

bool write_clock(const ClockSettings &c) {
  std::string f = clock_file();
  std::string dir = f.substr(0, f.rfind('/'));
  mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
  mkdir(dir.c_str(), 0755);
  std::ofstream out(f);
  out << "# Nirvana Desktop - the clock in the menu bar (Preferences > Date & Time)\n"
      << "h24=" << c.h24 << "\nseconds=" << c.seconds << "\ndate=" << c.date << "\nweekday=" << c.weekday
      << "\nwork=" << c.work << "\nshort_break=" << c.short_break << "\nlong_break=" << c.long_break
      << "\nrounds=" << c.rounds << "\nsound=" << c.sound << "\nauto_next=" << c.auto_next << "\n";
  return (bool)out;
}

std::string month_name(int m) {
  static const char *n[] = {N_("January"), N_("February"), N_("March"), N_("April"), N_("May (month)"),
                            N_("June"), N_("July"), N_("August"), N_("September"), N_("October"),
                            N_("November"), N_("December")};
  std::string s = _(n[((m % 12) + 12) % 12]);
  if (s == "May (month)") s = "May";   // a different text from the short "May"
  return s;
}
// in a date ("11 listopada 2026" - Polish uses another case than in "listopad 2026")
std::string month_in_date(int m) {
  static const char *n[] = {N_("January (in a date)"), N_("February (in a date)"), N_("March (in a date)"),
                            N_("April (in a date)"), N_("May (in a date)"), N_("June (in a date)"),
                            N_("July (in a date)"), N_("August (in a date)"), N_("September (in a date)"),
                            N_("October (in a date)"), N_("November (in a date)"), N_("December (in a date)")};
  std::string s = _(n[((m % 12) + 12) % 12]);
  size_t p = s.find(" (in a date)");   // not translated: just the name
  return p == std::string::npos ? s : s.substr(0, p);
}

std::string month_short(int m) {
  static const char *n[] = {N_("Jan"), N_("Feb"), N_("Mar"), N_("Apr"), N_("May"), N_("Jun"),
                            N_("Jul"), N_("Aug"), N_("Sep"), N_("Oct"), N_("Nov"), N_("Dec")};
  return _(n[((m % 12) + 12) % 12]);
}
std::string weekday_name(int d) {
  static const char *n[] = {N_("Sunday"), N_("Monday"), N_("Tuesday"), N_("Wednesday"), N_("Thursday"),
                            N_("Friday"), N_("Saturday")};
  return _(n[((d % 7) + 7) % 7]);
}
std::string weekday_short(int d) {
  static const char *n[] = {N_("Sun"), N_("Mon"), N_("Tue"), N_("Wed"), N_("Thu"), N_("Fri"), N_("Sat")};
  return _(n[((d % 7) + 7) % 7]);
}
std::string weekday_letters(int d) {
  static const char *n[] = {N_("Su"), N_("Mo"), N_("Tu"), N_("We"), N_("Th"), N_("Fr"), N_("Sa")};
  return _(n[((d % 7) + 7) % 7]);
}

std::string clock_text(const ClockSettings &c, time_t t, bool with_seconds) {
  struct tm tm;
  localtime_r(&t, &tm);
  char hm[32];
  int h = tm.tm_hour;
  if (c.h24) {
    if (with_seconds) snprintf(hm, sizeof hm, "%02d:%02d:%02d", h, tm.tm_min, tm.tm_sec);
    else snprintf(hm, sizeof hm, "%02d:%02d", h, tm.tm_min);
  } else {
    int h12 = h % 12 ? h % 12 : 12;
    if (with_seconds) snprintf(hm, sizeof hm, "%d:%02d:%02d %s", h12, tm.tm_min, tm.tm_sec, h < 12 ? "AM" : "PM");
    else snprintf(hm, sizeof hm, "%d:%02d %s", h12, tm.tm_min, h < 12 ? "AM" : "PM");
  }
  std::string r;
  if (c.weekday) r += weekday_short(tm.tm_wday) + " ";
  // TRANSLATORS: a short date in the menu bar: %1 day, %2 month (abbreviated)
  if (c.date) r += fmt(_("%2 %1"), std::to_string(tm.tm_mday), month_short(tm.tm_mon)) + " ";
  return r + hm;
}

// ---------------------------------------------------------------------------
// Desktop settings
// ---------------------------------------------------------------------------
static std::string desktop_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/desktop.conf";
}

DesktopSettings read_desktop() {
  DesktopSettings d;
  std::ifstream in(desktop_file());
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("icon_margin=", 0) == 0) d.icon_margin = line.substr(12) != "0";
    else if (line.rfind("dock_room=", 0) == 0) d.dock_room = line.substr(10) != "0";
  }
  return d;
}

bool write_desktop(const DesktopSettings &d) {
  std::string f = desktop_file();
  std::string dir = f.substr(0, f.rfind('/'));
  mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
  mkdir(dir.c_str(), 0755);
  std::ofstream out(f);
  out << "# Nirvana Desktop - the desktop (Preferences > Desktop)\n"
      << "icon_margin=" << d.icon_margin << "\ndock_room=" << d.dock_room << "\n";
  return (bool)out;
}

void notify_desktop_changed() {
#if defined(FLTK_USE_X11)
  fl_open_display();
  Display *dpy = fl_x11_display();
  if (!dpy) return;
  Atom at = XInternAtom(dpy, DESKTOP_ATOM, False);
  long v = (long)time(nullptr) ^ (long)clock();
  XChangeProperty(dpy, RootWindow(dpy, DefaultScreen(dpy)), at, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&v, 1);
  XFlush(dpy);
#endif
}

}  // namespace plat
