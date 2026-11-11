// Nirvana Desktop - power: implementation
#include "power.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/platform.H>
#include <dbus/dbus.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/scrnsaver.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace power {

// --- small helpers --------------------------------------------------------------------
static std::string read_file(const std::string &p) {
  std::ifstream in(p);
  std::string s;
  std::getline(in, s);
  while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
  return s;
}
static long read_num(const std::string &p, long def = -1) {
  std::string s = read_file(p);
  if (s.empty()) return def;
  char *e;
  long v = strtol(s.c_str(), &e, 10);
  return *e ? def : v;
}
static std::vector<std::string> list_dir(const std::string &d) {
  std::vector<std::string> v;
  if (DIR *dir = opendir(d.c_str())) {
    while (dirent *e = readdir(dir)) if (e->d_name[0] != '.') v.push_back(e->d_name);
    closedir(dir);
  }
  std::sort(v.begin(), v.end());
  return v;
}
static std::string supply_dir() {
  const char *e = getenv("NIRVANA_POWER_SUPPLY");   // for testing: a fake /sys/class/power_supply
  return e && *e ? e : "/sys/class/power_supply";
}
static std::string backlight_dir() {
  const char *e = getenv("NIRVANA_BACKLIGHT");
  return e && *e ? e : "/sys/class/backlight";
}
static int run(const std::vector<std::string> &args, std::string *out = nullptr) {
  int p[2];
  if (pipe(p) != 0) return -1;
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    int devnull = open("/dev/null", O_RDWR);
    dup2(out ? p[1] : devnull, 1);
    dup2(devnull, 2);
    close(p[0]);
    close(p[1]);
    setenv("LC_ALL", "C", 1);
    std::vector<char *> av;
    for (auto &a : args) av.push_back((char *)a.c_str());
    av.push_back(nullptr);
    execvp(av[0], av.data());
    _exit(127);
  }
  close(p[1]);
  char buf[4096];
  ssize_t n;
  while ((n = read(p[0], buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR))
    if (n > 0 && out) out->append(buf, n);
  close(p[0]);
  int st = 0;
  while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
static bool have(const std::string &prog) {
  const char *path = getenv("PATH");
  std::string p = path ? path : "/usr/bin:/bin";
  size_t a = 0;
  while (a <= p.size()) {
    size_t b = p.find(':', a);
    if (b == std::string::npos) b = p.size();
    if (access((p.substr(a, b - a) + "/" + prog).c_str(), X_OK) == 0) return true;
    a = b + 1;
  }
  return false;
}
// A program in the background, without waiting (double fork - no zombie is left).
static void detach(const std::vector<std::string> &args) {
  pid_t pid = fork();
  if (pid == 0) {
    setsid();
    if (fork() == 0) {
      std::vector<char *> av;
      for (auto &a : args) av.push_back((char *)a.c_str());
      av.push_back(nullptr);
      execvp(av[0], av.data());
      _exit(127);
    }
    _exit(0);
  }
  if (pid > 0) waitpid(pid, nullptr, 0);
}

// --- D-Bus ---------------------------------------------------------------------------
static DBusConnection *bus(DBusBusType t) {
  DBusError e;
  dbus_error_init(&e);
  DBusConnection *c = dbus_bus_get(t, &e);
  dbus_error_free(&e);
  if (c) dbus_connection_set_exit_on_disconnect(c, false);
  return c;
}
static DBusMessage *call(DBusConnection *c, DBusMessage *m, std::string *err = nullptr, int timeout = 5000) {
  DBusError e;
  dbus_error_init(&e);
  DBusMessage *r = dbus_connection_send_with_reply_and_block(c, m, timeout, &e);
  dbus_message_unref(m);
  if (!r && err) *err = e.message ? e.message : "D-Bus";
  dbus_error_free(&e);
  return r;
}
static const char *LOGIN = "org.freedesktop.login1";
static const char *LOGIN_PATH = "/org/freedesktop/login1";
static const char *LOGIN_MGR = "org.freedesktop.login1.Manager";

static std::string login_can(DBusConnection *c, const char *what) {
  DBusMessage *m = dbus_message_new_method_call(LOGIN, LOGIN_PATH, LOGIN_MGR, what);
  DBusMessage *r = call(c, m, nullptr, 2000);
  if (!r) return "";
  const char *s = nullptr;
  dbus_message_get_args(r, nullptr, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID);
  std::string out = s ? s : "";
  dbus_message_unref(r);
  return out;
}

static std::string login_action(const char *method) {
  DBusConnection *c = bus(DBUS_BUS_SYSTEM);
  if (!c) return _("No system D-Bus.");
  DBusMessage *m = dbus_message_new_method_call(LOGIN, LOGIN_PATH, LOGIN_MGR, method);
  dbus_bool_t interactive = TRUE;   // polkit may ask for a password
  dbus_message_append_args(m, DBUS_TYPE_BOOLEAN, &interactive, DBUS_TYPE_INVALID);
  std::string err;
  DBusMessage *r = call(c, m, &err, 30000);
  if (!r) {
    if (err.find("inhibit") != std::string::npos || err.find("Inhibit") != std::string::npos)
      return _("Blocked by another program or by presentation mode.");
    if (err.find("ServiceUnknown") != std::string::npos || err.find("was not provided") != std::string::npos)
      return _("The logind/elogind service is missing.");
    return plat::fmt(_("Failed: %1"), err);
  }
  dbus_message_unref(r);
  return "";
}

// --- power profiles without power-profiles-daemon --------------------------------------
// The kernel offers them in one of three ways (nirvana-power-helper sets them the same way):
// the ACPI platform profile, the CPU energy preference (EPP) or the cpufreq governor.
static bool has_word(const std::string &list, const char *w) {
  return (" " + list + " ").find(std::string(" ") + w + " ") != std::string::npos;
}

static void kernel_profiles(State &s) {
  const char *fake = getenv("NIRVANA_SYSFS");   // for testing: a fake /sys
  const std::string sys = fake && *fake ? fake : "/sys";
  const std::string pp = sys + "/firmware/acpi/platform_profile", cpu = sys + "/devices/system/cpu/cpu0/cpufreq";
  std::string cur, saver, perf, bal;
  std::string ch = read_file(pp + "_choices");
  if (!ch.empty()) {
    cur = read_file(pp);
    bool s1 = has_word(ch, "low-power") || has_word(ch, "quiet") || has_word(ch, "cool");
    if (s1) s.profiles.push_back("power-saver");
    if (has_word(ch, "balanced")) s.profiles.push_back("balanced");
    if (has_word(ch, "performance") || has_word(ch, "balanced-performance")) s.profiles.push_back("performance");
    s.profile = cur == "low-power" || cur == "quiet" || cur == "cool" ? "power-saver"
              : cur == "performance" || cur == "balanced-performance" ? "performance" : "balanced";
  } else if (!(ch = read_file(cpu + "/energy_performance_available_preferences")).empty()) {
    cur = read_file(cpu + "/energy_performance_preference");
    if (has_word(ch, "power") || has_word(ch, "balance_power")) s.profiles.push_back("power-saver");
    if (has_word(ch, "balance_performance") || has_word(ch, "default")) s.profiles.push_back("balanced");
    if (has_word(ch, "performance")) s.profiles.push_back("performance");
    s.profile = cur == "power" || cur == "balance_power" ? "power-saver" : cur == "performance" ? "performance" : "balanced";
  } else if (!(ch = read_file(cpu + "/scaling_available_governors")).empty()) {
    cur = read_file(cpu + "/scaling_governor");
    if (has_word(ch, "powersave") || has_word(ch, "conservative")) s.profiles.push_back("power-saver");
    if (has_word(ch, "schedutil") || has_word(ch, "ondemand")) s.profiles.push_back("balanced");
    if (has_word(ch, "performance")) s.profiles.push_back("performance");
    s.profile = cur == "performance" ? "performance" : cur == "powersave" || cur == "conservative" ? "power-saver" : "balanced";
  }
  if (s.profiles.size() < 2) { s.profiles.clear(); s.profile.clear(); return; }   // nothing to choose from
  s.kernel_profiles = true;
}

static std::string exe_dir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return "";
  buf[n] = 0;
  std::string p = buf;
  return p.substr(0, p.rfind('/'));
}

static std::string profile_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/power-profile";
}

// --- stan -----------------------------------------------------------------------------
State query() {
  State s;
  std::string base = supply_dir();
  bool any_mains = false, mains_online = false;
  long e_now_sum = 0, e_full_sum = 0, power_sum = 0;
  for (auto &n : list_dir(base)) {
    std::string d = base + "/" + n;
    std::string type = read_file(d + "/type");
    if (type == "Mains" || type == "USB") {
      if (type == "Mains") any_mains = true;
      if (read_num(d + "/online", 0) == 1) mains_online = true;
    } else if (type == "Battery") {
      if (read_file(d + "/scope") == "Device") continue;   // mouse, wireless keyboard
      if (read_num(d + "/present", 1) == 0) continue;
      Battery b;
      b.name = n;
      b.status = read_file(d + "/status");
      long en = read_num(d + "/energy_now"), ef = read_num(d + "/energy_full"), pw = read_num(d + "/power_now");
      if (en < 0) { en = read_num(d + "/charge_now"); ef = read_num(d + "/charge_full"); pw = read_num(d + "/current_now"); }
      long cap = read_num(d + "/capacity");
      b.percent = cap >= 0 ? (int)cap : (ef > 0 && en >= 0 ? (int)(100 * en / ef) : 0);
      if (pw > 0 && en >= 0) {
        if (b.status == "Discharging") b.minutes = (int)(60.0 * en / pw);
        else if (b.status == "Charging" && ef > en) b.minutes = (int)(60.0 * (ef - en) / pw);
      }
      if (en >= 0 && ef > 0) { e_now_sum += en; e_full_sum += ef; }
      if (pw > 0) power_sum += pw;
      s.batteries.push_back(b);
    }
  }
  if (!s.batteries.empty()) {
    if (e_full_sum > 0) s.percent = (int)std::lround(100.0 * e_now_sum / e_full_sum);
    else s.percent = s.batteries[0].percent;
    for (auto &b : s.batteries) if (b.status == "Charging") s.charging = true;
    bool discharging = false;
    for (auto &b : s.batteries) if (b.status == "Discharging") discharging = true;
    s.on_ac = any_mains ? mains_online : !discharging;
    if (power_sum > 0 && e_full_sum > 0) {
      if (discharging) s.minutes = (int)(60.0 * e_now_sum / power_sum);
      else if (s.charging) s.minutes = (int)(60.0 * (e_full_sum - e_now_sum) / power_sum);
    }
  }
  // brightness: we prefer firmware > platform > raw
  std::string bl = backlight_dir(), best;
  int best_rank = 9;
  for (auto &n : list_dir(bl)) {
    std::string t = read_file(bl + "/" + n + "/type");
    int rank = t == "firmware" ? 0 : t == "platform" ? 1 : 2;
    if (rank < best_rank) { best_rank = rank; best = n; }
  }
  if (!best.empty()) {
    long max = read_num(bl + "/" + best + "/max_brightness"), cur = read_num(bl + "/" + best + "/brightness");
    if (max > 0 && cur >= 0) {
      s.backlight = best;
      s.brightness = (int)std::lround(100.0 * cur / max);
    }
  }
  // power profiles
  if (have("powerprofilesctl")) {
    std::string out;
    if (run({"powerprofilesctl", "get"}, &out) == 0) {
      s.profile = out.substr(0, out.find('\n'));
      out.clear();
      run({"powerprofilesctl", "list"}, &out);
      size_t p = 0;
      while (p < out.size()) {
        size_t e = out.find('\n', p);
        if (e == std::string::npos) e = out.size();
        std::string l = out.substr(p, e - p);
        p = e + 1;
        // "* balanced:" or "  power-saver:"
        if (l.size() > 3 && l.back() == ':' && (l[0] == '*' || (l[0] == ' ' && l[1] == ' ' && l[2] != ' '))) {
          std::string name = l.substr(0, l.size() - 1);
          name.erase(0, name.find_first_not_of(" *"));
          if (!name.empty() && name.find(' ') == std::string::npos) s.profiles.insert(s.profiles.begin(), name);
        }
      }
    }
  }
  if (s.profiles.empty()) kernel_profiles(s);
  // logind
  if (DBusConnection *c = bus(DBUS_BUS_SYSTEM)) {
    std::string susp = login_can(c, "CanSuspend");
    if (!susp.empty()) {
      s.logind = true;
      auto yes = [](const std::string &v) { return v == "yes" || v == "challenge"; };
      s.can_suspend = yes(susp);
      s.can_hibernate = yes(login_can(c, "CanHibernate"));
      s.can_poweroff = yes(login_can(c, "CanPowerOff"));
      s.can_reboot = yes(login_can(c, "CanReboot"));
    }
  }
  return s;
}

std::string battery_text(const State &s) {
  using plat::fmt;
  if (s.percent < 0) return _("AC power");
  std::string pct = std::to_string(s.percent) + "%";
  std::string tm;
  if (s.minutes > 0 && s.minutes < 60 * 48) {
    int h = s.minutes / 60, m = s.minutes % 60;
    tm = h ? fmt(_("%1 h %2 min"), std::to_string(h), std::to_string(m)) : fmt(_("%1 min"), std::to_string(m));
  }
  if (s.charging) return tm.empty() ? fmt(_("%1, charging"), pct) : fmt(_("%1, charging (full in about %2)"), pct, tm);
  if (s.on_ac) return fmt(_("%1, on power adapter"), pct);
  return tm.empty() ? pct : fmt(_("%1, about %2 left"), pct, tm);
}

const char *profile_name(const std::string &p) {
  if (p == "power-saver") return _("Power saver");
  if (p == "balanced") return _("Balanced");
  if (p == "performance") return _("Performance");
  return p.c_str();
}

// --- brightness --------------------------------------------------------------------------
static bool logind_brightness(const std::string &dev, unsigned value) {
  DBusConnection *c = bus(DBUS_BUS_SYSTEM);
  if (!c) return false;
  for (const char *path : {"/org/freedesktop/login1/session/auto", "/org/freedesktop/login1/session/self"}) {
    DBusMessage *m = dbus_message_new_method_call(LOGIN, path, "org.freedesktop.login1.Session", "SetBrightness");
    const char *sub = "backlight", *name = dev.c_str();
    dbus_uint32_t v = value;
    dbus_message_append_args(m, DBUS_TYPE_STRING, &sub, DBUS_TYPE_STRING, &name, DBUS_TYPE_UINT32, &v, DBUS_TYPE_INVALID);
    if (DBusMessage *r = call(c, m, nullptr, 2000)) { dbus_message_unref(r); return true; }
  }
  return false;
}

bool set_brightness(const State &s, int pct) {
  if (s.backlight.empty()) return false;
  std::string d = backlight_dir() + "/" + s.backlight;
  long max = read_num(d + "/max_brightness");
  if (max <= 0) return false;
  pct = std::clamp(pct, 1, 100);   // 0 would turn the backlight off completely
  unsigned v = (unsigned)std::max(1L, std::lround(max * pct / 100.0));
  if (getenv("NIRVANA_BACKLIGHT")) {   // fake: write straight to the file
    std::ofstream(d + "/brightness") << v << "\n";
    return true;
  }
  if (logind_brightness(s.backlight, v)) return true;
  if (FILE *f = fopen((d + "/brightness").c_str(), "w")) {   // writable (e.g. a udev rule)
    fprintf(f, "%u\n", v);
    fclose(f);
    return true;
  }
  if (have("brightnessctl")) return run({"brightnessctl", "-q", "set", std::to_string(pct) + "%"}) == 0;
  return false;
}

void step_brightness(int delta) {
  State s = query();
  if (s.brightness >= 0) set_brightness(s, s.brightness + delta);
}

bool set_profile(const std::string &p) {
  if (have("powerprofilesctl")) return run({"powerprofilesctl", "set", p}) == 0;
  // through the kernel: as root with pkexec (polkit lets the active user do it without a password)
  std::string helper;
  std::string e = exe_dir();
  for (const std::string &c : {e + "/../libexec/nirvana-power-helper", e + "/../update/nirvana-power-helper",
                               std::string("/usr/libexec/nirvana-power-helper")})
    if (!access(c.c_str(), R_OK)) { helper = c; break; }
  if (helper.empty() || !have("pkexec")) return false;
  bool ok = run({"pkexec", helper, "profile", p}) == 0;
  if (ok) std::ofstream(profile_file()) << p << "\n";   // the kernel forgets it at a restart
  return ok;
}

void restore_profile() {
  if (have("powerprofilesctl")) return;   // power-profiles-daemon remembers it itself
  std::string want = read_file(profile_file());
  if (want.empty()) return;
  State s;
  kernel_profiles(s);
  if (s.kernel_profiles && s.profile != want &&
      std::find(s.profiles.begin(), s.profiles.end(), want) != s.profiles.end())
    set_profile(want);
}

// --- actions ----------------------------------------------------------------------------
std::string suspend() { return login_action("Suspend"); }
std::string hibernate() { return login_action("Hibernate"); }
std::string poweroff() { return login_action("PowerOff"); }
std::string reboot() { return login_action("Reboot"); }

std::string locker() {
  if (const char *e = getenv("NIRVANA_LOCKER")) return e;
  if (have("xsecurelock")) return "xsecurelock";
  if (have("i3lock")) return "i3lock -c 5E6FA8";
  if (have("slock")) return "slock";
  if (have("xscreensaver-command")) return "xscreensaver-command -lock";
  if (have("light-locker-command")) return "light-locker-command -l";
  if (have("xlock")) return "xlock -mode blank";
  if (have("loginctl") && getenv("XDG_SESSION_ID")) return "loginctl lock-session";
  return "";
}

bool lock_screen() {
  std::string l = locker();
  if (l.empty()) return false;
  detach({"sh", "-c", l});
  return true;
}

// --- projection mode ---------------------------------------------------------------------
static bool g_proj = false;
static int g_inhibit_fd = -1;
static unsigned g_gnome_cookie = 0, g_fdo_cookie = 0;
static bool g_dpms_was_on = false;
static void keep_awake(void *) {
  // for screensavers with their own idle counter (xscreensaver, xss-lock)
  if (!g_proj) return;
  XResetScreenSaver(fl_display);
  XFlush(fl_display);
  Fl::repeat_timeout(50.0, keep_awake);
}

bool projection() { return g_proj; }

std::vector<std::string> set_projection(bool on) {
  std::vector<std::string> done;
  if (on == g_proj) return done;
  fl_open_display();
  Display *dpy = fl_display;
  int ev, er;
  if (on) {
    // 1. the X screen: screensaver and DPMS
    if (XScreenSaverQueryExtension(dpy, &ev, &er)) { XScreenSaverSuspend(dpy, True); done.push_back(_("screen blanking")); }
    if (DPMSQueryExtension(dpy, &ev, &er) && DPMSCapable(dpy)) {
      CARD16 level; BOOL st;
      DPMSInfo(dpy, &level, &st);
      g_dpms_was_on = st;
      DPMSDisable(dpy);
    }
    XFlush(dpy);
    Fl::add_timeout(50.0, keep_awake);
    // 2. logind/elogind: idleness and sleeping
    if (DBusConnection *c = bus(DBUS_BUS_SYSTEM)) {
      DBusMessage *m = dbus_message_new_method_call(LOGIN, LOGIN_PATH, LOGIN_MGR, "Inhibit");
      const char *what = "idle:sleep", *who = "Nirvana Desktop", *why = "Tryb projekcji", *mode = "block";
      dbus_message_append_args(m, DBUS_TYPE_STRING, &what, DBUS_TYPE_STRING, &who, DBUS_TYPE_STRING, &why,
                               DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);
      if (DBusMessage *r = call(c, m)) {
        int fd = -1;
        if (dbus_message_get_args(r, nullptr, DBUS_TYPE_UNIX_FD, &fd, DBUS_TYPE_INVALID) && fd >= 0) {
          g_inhibit_fd = fd;
          done.push_back(_("sleep (logind)"));
        }
        dbus_message_unref(r);
      }
    }
    // 3. environments with their own idle management (GNOME, KDE, Xfce)
    if (DBusConnection *c = bus(DBUS_BUS_SESSION)) {
      DBusMessage *m = dbus_message_new_method_call("org.gnome.SessionManager", "/org/gnome/SessionManager",
                                                    "org.gnome.SessionManager", "Inhibit");
      const char *app = "nirvana", *why = "Tryb projekcji";
      dbus_uint32_t xid = 0, flags = 4 | 8;   // sleep | idle
      dbus_message_append_args(m, DBUS_TYPE_STRING, &app, DBUS_TYPE_UINT32, &xid, DBUS_TYPE_STRING, &why,
                               DBUS_TYPE_UINT32, &flags, DBUS_TYPE_INVALID);
      if (DBusMessage *r = call(c, m, nullptr, 2000)) {
        dbus_message_get_args(r, nullptr, DBUS_TYPE_UINT32, &g_gnome_cookie, DBUS_TYPE_INVALID);
        dbus_message_unref(r);
        done.push_back("GNOME");
      }
      m = dbus_message_new_method_call("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                                       "org.freedesktop.ScreenSaver", "Inhibit");
      dbus_message_append_args(m, DBUS_TYPE_STRING, &app, DBUS_TYPE_STRING, &why, DBUS_TYPE_INVALID);
      if (DBusMessage *r = call(c, m, nullptr, 2000)) {
        dbus_message_get_args(r, nullptr, DBUS_TYPE_UINT32, &g_fdo_cookie, DBUS_TYPE_INVALID);
        dbus_message_unref(r);
        done.push_back(_("desktop screen saver"));
      }
    }
    g_proj = true;
  } else {
    if (XScreenSaverQueryExtension(dpy, &ev, &er)) XScreenSaverSuspend(dpy, False);
    if (g_dpms_was_on && DPMSQueryExtension(dpy, &ev, &er)) DPMSEnable(dpy);
    XFlush(dpy);
    Fl::remove_timeout(keep_awake);
    if (g_inhibit_fd >= 0) { close(g_inhibit_fd); g_inhibit_fd = -1; }
    if (DBusConnection *c = bus(DBUS_BUS_SESSION)) {
      if (g_gnome_cookie) {
        DBusMessage *m = dbus_message_new_method_call("org.gnome.SessionManager", "/org/gnome/SessionManager",
                                                      "org.gnome.SessionManager", "Uninhibit");
        dbus_message_append_args(m, DBUS_TYPE_UINT32, &g_gnome_cookie, DBUS_TYPE_INVALID);
        if (DBusMessage *r = call(c, m, nullptr, 2000)) dbus_message_unref(r);
        g_gnome_cookie = 0;
      }
      if (g_fdo_cookie) {
        DBusMessage *m = dbus_message_new_method_call("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                                                      "org.freedesktop.ScreenSaver", "UnInhibit");
        dbus_message_append_args(m, DBUS_TYPE_UINT32, &g_fdo_cookie, DBUS_TYPE_INVALID);
        if (DBusMessage *r = call(c, m, nullptr, 2000)) dbus_message_unref(r);
        g_fdo_cookie = 0;
      }
    }
    g_proj = false;
  }
  return done;
}

// --- idleness ---------------------------------------------------------------------------
static std::string idle_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/power.conf";
}

IdleSettings read_idle() {
  IdleSettings s;
  std::ifstream in(idle_file());
  std::string line;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    std::string k = line.substr(0, eq);
    int v = atoi(line.c_str() + eq + 1);
    if (k == "blank_minutes") s.blank_min = std::clamp(v, 0, 240);
    else if (k == "lock_on_blank") s.lock_on_blank = v != 0;
    else if (k == "sleep_battery_minutes") s.sleep_battery_min = std::clamp(v, 0, 600);
    else if (k == "sleep_ac_minutes") s.sleep_ac_min = std::clamp(v, 0, 600);
    else if (k == "critical_action") s.critical_action = std::clamp(v, 0, 2);
  }
  return s;
}

bool write_idle(const IdleSettings &s) {
  std::string f = idle_file();
  std::string dir = f.substr(0, f.rfind('/'));
  mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
  mkdir(dir.c_str(), 0755);
  std::ofstream out(f);
  out << "# Nirvana Desktop - power settings (minutes, 0 = never)\n"
      << "blank_minutes=" << s.blank_min << "\nlock_on_blank=" << (s.lock_on_blank ? 1 : 0)
      << "\nsleep_battery_minutes=" << s.sleep_battery_min << "\nsleep_ac_minutes=" << s.sleep_ac_min
      << "\ncritical_action=" << s.critical_action << "\n";
  return (bool)out;
}

void apply_idle(const IdleSettings &s) {
  fl_open_display();
  Display *dpy = fl_display;
  int t = s.blank_min * 60;
  XSetScreenSaver(dpy, t, 0, PreferBlanking, AllowExposures);
  int ev, er;
  if (DPMSQueryExtension(dpy, &ev, &er) && DPMSCapable(dpy)) {
    if (t > 0) {
      // first blanking (a black screen), after another minute the monitor goes to sleep
      DPMSSetTimeouts(dpy, t + 60, t + 60, t + 60);
      if (!g_proj) DPMSEnable(dpy);
    } else {
      DPMSDisable(dpy);
    }
  }
  XFlush(dpy);
}

int idle_seconds() {
  fl_open_display();
  int ev, er;
  if (!XScreenSaverQueryExtension(fl_display, &ev, &er)) return 0;
  XScreenSaverInfo *info = XScreenSaverAllocInfo();
  if (!info) return 0;
  XScreenSaverQueryInfo(fl_display, DefaultRootWindow(fl_display), info);
  int s = (int)(info->idle / 1000);
  XFree(info);
  return s;
}

void notify_settings_changed() {
  fl_open_display();
  Atom a = XInternAtom(fl_display, SETTINGS_ATOM, False);
  long v = (long)time(nullptr) ^ getpid();
  XChangeProperty(fl_display, DefaultRootWindow(fl_display), a, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&v, 1);
  XFlush(fl_display);
}

}  // namespace power
