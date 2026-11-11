// About This Computer: the system at a glance in the manner of neofetch (a big
// logo on the left, "key: value" lines and color swatches on the right), plus
// the licence (GPL-3.0) and the acknowledgements.
#include "desktop.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <X11/Xlib.h>

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <pwd.h>
#include <sstream>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

using namespace plat;

namespace {

// ---------------------------------------------------------------------------
// Gathering the information
// ---------------------------------------------------------------------------
std::string trim(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n\"");
  size_t b = s.find_last_not_of(" \t\r\n\"");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string read_line(const std::string &file) {
  std::ifstream in(file);
  std::string l;
  std::getline(in, l);
  return trim(l);
}

std::string run(const char *cmd) {   // the first line of a command's output
  std::string out;
  if (FILE *p = popen(cmd, "r")) {
    char buf[512];
    if (fgets(buf, sizeof buf, p)) out = buf;
    pclose(p);
  }
  return trim(out);
}

std::string os_name() {
  std::ifstream in("/etc/os-release");
  std::string l, name;
  while (std::getline(in, l))
    if (!l.compare(0, 12, "PRETTY_NAME=")) name = trim(l.substr(12));
  struct utsname u;
  if (!uname(&u)) name += std::string(name.empty() ? "" : " ") + u.machine;
  return name.empty() ? "Linux" : name;
}

std::string host_name() {
  // values that firmware leaves as placeholders mean nothing to the user
  auto useful = [](const std::string &s) {
    static const char *junk[] = {"To be filled", "To Be Filled", "System Product", "Default string", "Not Applicable",
                                 "None", "O.E.M.", "System Version", "Type1Product"};
    if (s.empty()) return false;
    for (auto j : junk) if (s.find(j) != std::string::npos) return false;
    return true;
  };
  std::string d = "/sys/devices/virtual/dmi/id/";
  std::string name = read_line(d + "product_name"), ver = read_line(d + "product_version");
  std::string vendor = read_line(d + "sys_vendor");
  std::string r;
  if (useful(vendor) && name.find(vendor) == std::string::npos) r = vendor;
  if (useful(name)) r += (r.empty() ? "" : " ") + name;
  if (useful(ver)) r += (r.empty() ? "" : " ") + ver;
  if (r.empty()) r = read_line("/sys/firmware/devicetree/base/model");
  return r.empty() ? "—" : r;
}

std::string kernel() {
  struct utsname u;
  return uname(&u) ? "—" : u.release;
}

std::string uptime() {
  double up = 0;
  if (FILE *f = fopen("/proc/uptime", "r")) { if (fscanf(f, "%lf", &up) != 1) up = 0; fclose(f); }
  long s = (long)up, d = s / 86400, h = s / 3600 % 24, m = s / 60 % 60;
  std::vector<std::string> parts;
  if (d) parts.push_back(fmt(trn(d, "%1 day", "%1 days"), std::to_string(d)));
  if (h) parts.push_back(fmt(trn(h, "%1 hour", "%1 hours"), std::to_string(h)));
  if (m || parts.empty()) parts.push_back(fmt(trn(m, "%1 minute", "%1 minutes"), std::to_string(m)));
  std::string r;
  for (auto &p : parts) r += (r.empty() ? "" : ", ") + p;
  return r;
}

int count_dirs(const std::string &dir) {
  int n = 0;
  if (DIR *d = opendir(dir.c_str())) {
    while (dirent *e = readdir(d)) if (e->d_name[0] != '.') n++;
    closedir(d);
  }
  return n;
}

std::string packages(const std::string &home) {
  std::vector<std::string> parts;
  int dpkg = 0;
  {
    std::ifstream in("/var/lib/dpkg/status");
    std::string l;
    while (std::getline(in, l))
      if (!l.compare(0, 8, "Status: ") && l.find(" installed") != std::string::npos) dpkg++;
  }
  if (dpkg) parts.push_back(std::to_string(dpkg) + " (dpkg)");
  if (!access("/var/lib/rpm", F_OK) && !dpkg) {
    std::string n = run("rpm -qa 2>/dev/null | wc -l");
    if (!n.empty() && n != "0") parts.push_back(n + " (rpm)");
  }
  int flat = count_dirs("/var/lib/flatpak/app") + count_dirs(home + "/.local/share/flatpak/app");
  if (flat) parts.push_back(std::to_string(flat) + " (flatpak)");
  int apps = 0;   // AppImages installed with Nirvana Desktop: one per program folder
  if (DIR *d = opendir((home + "/.nirvana-apps").c_str())) {
    while (dirent *e = readdir(d)) {
      if (e->d_name[0] == '.') continue;
      std::string sub = home + "/.nirvana-apps/" + e->d_name;
      if (DIR *c = opendir(sub.c_str())) {   // a category folder
        while (dirent *f = readdir(c)) if (f->d_name[0] != '.') apps++;
        closedir(c);
      } else apps++;
    }
    closedir(d);
  }
  if (apps) parts.push_back(std::to_string(apps) + " (AppImage)");
  std::string r;
  for (auto &p : parts) r += (r.empty() ? "" : ", ") + p;
  return r.empty() ? "—" : r;
}

std::string shell_name() {
  const char *sh = getenv("SHELL");
  std::string path = sh && *sh ? sh : "";
  if (path.empty())
    if (passwd *pw = getpwuid(getuid())) path = pw->pw_shell ? pw->pw_shell : "";
  if (path.empty()) return "—";
  std::string name = path.substr(path.rfind('/') + 1);
  std::string ver;
  if (name == "bash") ver = run((path + " -c 'echo $BASH_VERSION' 2>/dev/null").c_str());
  else if (name == "zsh") ver = run((path + " -c 'echo $ZSH_VERSION' 2>/dev/null").c_str());
  else if (name == "fish") ver = run((path + " -c 'echo $version' 2>/dev/null").c_str());
  if (auto p = ver.find('('); p != std::string::npos) ver = ver.substr(0, p);
  return ver.empty() ? name : name + " " + ver;
}

std::string resolution() {
  std::string r;
  for (int i = 0; i < Fl::screen_count(); i++) {
    int x, y, w, h;
    Fl::screen_xywh(x, y, w, h, i);
    float s = Fl::screen_scale(i);
    r += (r.empty() ? "" : ", ") + std::to_string((int)(w * s + 0.5)) + "×" + std::to_string((int)(h * s + 0.5));
  }
  return r.empty() ? "—" : r;
}

std::string cpu() {
  std::ifstream in("/proc/cpuinfo");
  std::string l, model;
  int n = 0;
  while (std::getline(in, l)) {
    if (!l.compare(0, 9, "processor")) n++;
    else if (model.empty() && (!l.compare(0, 10, "model name") || !l.compare(0, 8, "Hardware")))
      model = trim(l.substr(l.find(':') + 1));
  }
  for (const char *junk : {"(R)", "(TM)", "(tm)", " CPU", " Processor", " with Radeon Graphics"}) {
    size_t p;
    while ((p = model.find(junk)) != std::string::npos) model.erase(p, strlen(junk));
  }
  if (auto p = model.find(" @ "); p != std::string::npos) model = model.substr(0, p);
  while (model.find("  ") != std::string::npos) model.erase(model.find("  "), 1);
  if (model.empty()) model = "—";
  long n2 = sysconf(_SC_NPROCESSORS_ONLN);
  if (n2 > 0) n = (int)n2;
  std::string r = model + " (" + std::to_string(n) + ")";
  std::string mhz = read_line("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
  if (!mhz.empty()) {
    char b[32];
    snprintf(b, sizeof b, " @ %.2f GHz", atof(mhz.c_str()) / 1e6);
    r += b;
    if (language() == "pl") for (auto &c : r) if (c == '.') c = ',';
  }
  return r;
}

std::string gpu() {
  std::string r;
  if (FILE *p = popen("lspci -mm 2>/dev/null", "r")) {
    char buf[512];
    while (fgets(buf, sizeof buf, p)) {
      // 00:02.0 "VGA compatible controller" "Intel Corporation" "UHD Graphics 620" ...
      std::vector<std::string> f;
      for (char *q = buf; (q = strchr(q, '"'));) {
        char *e = strchr(q + 1, '"');
        if (!e) break;
        f.emplace_back(q + 1, e);
        q = e + 1;
      }
      if (f.size() < 3) continue;
      if (f[0].find("VGA") == std::string::npos && f[0].find("3D") == std::string::npos &&
          f[0].find("Display") == std::string::npos) continue;
      std::string vendor = f[1];
      for (const char *junk : {" Corporation", " Technologies Inc", ", Inc.", " Inc.", " [AMD/ATI]"}) {
        size_t q;
        while ((q = vendor.find(junk)) != std::string::npos) vendor.erase(q, strlen(junk));
      }
      std::string dev = f[2];
      if (auto a = dev.find('['), b = dev.find(']'); a != std::string::npos && b > a) dev = dev.substr(a + 1, b - a - 1);
      r += (r.empty() ? "" : ", ") + vendor + " " + dev;
    }
    pclose(p);
  }
  if (r.empty()) {   // no lspci: at least the kernel driver
    std::string link = "/sys/class/drm/card0/device/driver";
    char buf[512];
    ssize_t n = readlink(link.c_str(), buf, sizeof buf - 1);
    if (n > 0) { buf[n] = 0; r = strrchr(buf, '/') + 1; }
  }
  return r.empty() ? "—" : r;
}

std::string mib(unsigned long long mb) {   // as in the menu bar: MB below 1 GB, then GB
  char b[32];
  if (mb < 1024) snprintf(b, sizeof b, "%lluMB", mb);
  else snprintf(b, sizeof b, "%.1fGB", mb / 1024.0);
  std::string s = b;
  if (language() == "pl") for (auto &c : s) if (c == '.') c = ',';
  return s;
}

std::string memory() {
  unsigned long long avail = 0, total = 0, v;
  if (FILE *f = fopen("/proc/meminfo", "r")) {
    char line[128];
    while (fgets(line, sizeof line, f)) {
      if (sscanf(line, "MemAvailable: %llu", &v) == 1) avail = v;
      else if (sscanf(line, "MemTotal: %llu", &v) == 1) total = v;
    }
    fclose(f);
  }
  if (!total) return "—";
  unsigned long long used = total > avail ? total - avail : 0;
  return fmt(_("%1 of %2 (%3%)"), mib(used / 1024), mib(total / 1024), std::to_string(used * 100 / total));
}

std::string disk(const char *path) {
  struct statvfs sv;
  if (statvfs(path, &sv)) return "—";
  unsigned long long total = (unsigned long long)sv.f_blocks * sv.f_frsize / (1024 * 1024);
  unsigned long long used = (unsigned long long)(sv.f_blocks - sv.f_bfree) * sv.f_frsize / (1024 * 1024);
  if (!total) return "—";
  return fmt(_("%1 of %2 (%3%)"), mib(used), mib(total), std::to_string(used * 100 / total));
}

std::string battery() {
  std::string r;
  if (DIR *d = opendir("/sys/class/power_supply")) {
    while (dirent *e = readdir(d)) {
      std::string base = std::string("/sys/class/power_supply/") + e->d_name;
      if (e->d_name[0] == '.' || read_line(base + "/type") != "Battery") continue;
      std::string cap = read_line(base + "/capacity"), st = read_line(base + "/status");
      if (cap.empty()) continue;
      std::string state = st == "Charging" ? _("charging") : st == "Full" ? _("full")
                        : st == "Discharging" ? _("discharging") : "";
      r += (r.empty() ? "" : ", ") + cap + "%" + (state.empty() ? "" : " [" + state + "]");
    }
    closedir(d);
  }
  return r;
}

std::string local_ip() {
  std::string r;
  ifaddrs *list = nullptr;
  if (getifaddrs(&list)) return "—";
  for (ifaddrs *a = list; a; a = a->ifa_next) {
    if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK)) continue;
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((sockaddr_in *)a->ifa_addr)->sin_addr, buf, sizeof buf);
    r += (r.empty() ? "" : ", ") + std::string(buf) + " (" + a->ifa_name + ")";
  }
  freeifaddrs(list);
  return r.empty() ? _("offline") : r;
}

std::string locale_name() {
  for (const char *v : {"LC_ALL", "LC_MESSAGES", "LANG"})
    if (const char *e = getenv(v); e && *e) return e;
  return "C";
}

std::string cursor_theme() {
  if (const char *e = getenv("XCURSOR_THEME"); e && *e) return e;
  if (fl_display)
    if (const char *t = XGetDefault(fl_display, "Xcursor", "theme")) return t;
  return _("X11 default");
}

std::string terminal_font() {
  std::string font = "DejaVu Sans Mono", size = "12";
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  std::ifstream in((x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/terminal.conf");
  std::string l;
  while (std::getline(in, l)) {
    if (!l.compare(0, 5, "font=") && l.size() > 5) font = l.substr(5);
    else if (!l.compare(0, 5, "size=") && l.size() > 5) size = l.substr(5);
  }
  return font + " " + size;
}

std::string user_host() {
  std::string user;
  if (passwd *pw = getpwuid(getuid())) user = pw->pw_name;
  char host[256] = "";
  gethostname(host, sizeof host - 1);
  return user + "@" + host;
}

// ---------------------------------------------------------------------------
// The "neofetch" view
// ---------------------------------------------------------------------------
struct Fact { std::string key, value; };

class SystemView : public Fl_Widget {
public:
  std::string title;
  std::vector<Fact> facts;
  SystemView(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}

  // The key color: the logo violet, lightened on dark schemes so it stays readable.
  static Fl_Color key_color() {
    uchar r, g, b;
    Fl::get_color(GRAY, r, g, b);
    bool dark = 0.299 * r + 0.587 * g + 0.114 * b < 110;
    return dark ? fl_rgb_color(0xB0, 0xA0, 0xF0) : fl_rgb_color(0x4B, 0x35, 0x90);
  }

  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_color(GRAY);
    fl_rectf(x(), y(), w(), h());
    const int LOGO = 120;
    int lx = x() + 18, ly = y() + 14;
    draw_logo(lx, ly, LOGO);
    fl_font(FONT_SYSTEM, SIZE_SYSTEM + 2);
    fl_color(TEXT);
    int cx = lx + LOGO / 2;
    auto centered = [&](const char *s, int yy) { fl_draw(s, cx - (int)fl_width(s) / 2, yy); };
    centered("Nirvana Desktop", ly + LOGO + 24);
    fl_font(FONT_SMALL, SIZE_SYSTEM);
    centered(fmt(_("version %1"), NIRVANA_VERSION).c_str(), ly + LOGO + 40);

    int tx = lx + LOGO + 30, ty = y() + 18, lh = 16, maxw = x() + w() - tx - 12;
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    fl_color(key_color());
    fl_draw(title.c_str(), tx, ty + 10);
    fl_color(TEXT);
    std::string dashes(title.size(), '-');
    fl_font(FONT_SMALL, SIZE_SYSTEM);
    fl_draw(dashes.c_str(), tx, ty + 10 + lh);
    int yy = ty + 10 + 2 * lh;
    for (auto &f : facts) {
      fl_font(FONT_SYSTEM, SIZE_SYSTEM);
      fl_color(key_color());
      std::string k = f.key + ": ";
      fl_draw(k.c_str(), tx, yy);
      int kw = (int)fl_width(k.c_str());
      fl_font(FONT_SMALL, SIZE_SYSTEM);
      fl_color(TEXT);
      std::string v = ellipsize(f.value, maxw - kw);
      fl_draw(v.c_str(), tx + kw, yy);
      yy += lh;
    }
    // swatches under the logo, like neofetch's terminal colors: the scheme and the classic 16
    yy = ly + LOGO + 60;
    tx = cx - 4 * 15;
    const int SW = 15, SH = 12;
    const Scheme &sc = current_scheme();
    unsigned row1[8] = {sc.window, sc.content, sc.text, sc.sel, sc.title, sc.title2, sc.border, sc.title_text};
    if (sc.accent >= 0) row1[7] = (unsigned)sc.accent;
    static const unsigned row2[8] = {0x000000, 0xCC0000, 0x4E9A06, 0xC4A000, 0x3465A4, 0x75507B, 0x06989A, 0xD3D7CF};
    static const unsigned row3[8] = {0x555753, 0xEF2929, 0x8AE234, 0xFCE94F, 0x729FCF, 0xAD7FA8, 0x34E2E2, 0xEEEEEC};
    for (auto *row : {row1, (unsigned *)row2, (unsigned *)row3}) {
      for (int i = 0; i < 8; i++) {
        unsigned c = row[i];
        fl_color(fl_rgb_color(c >> 16, (c >> 8) & 0xFF, c & 0xFF));
        fl_rectf(tx + i * SW, yy, SW, SH);
      }
      fl_color(DARK);
      fl_rect(tx - 1, yy - 1, 8 * SW + 2, SH + 2);
      yy += SH + 4;
    }
    fl_pop_clip();
  }
};

std::vector<Fact> gather(const std::string &home) {
  const Scheme &sc = current_scheme();
  std::string scheme = sc.name.empty() ? "Platinum" : _(sc.name.c_str());
  const char *style = sc.style == TitleStyle::Stripes ? _("stripes") : sc.style == TitleStyle::Solid ? _("solid")
                    : sc.style == TitleStyle::Glossy ? _("glass") : _("gradient");
  std::string font = std::string(Fl::get_font_name(FONT_SYSTEM)) + " " + std::to_string(SIZE_SYSTEM);
  if (font[0] == ' ' || font[0] == 'B') font.erase(0, 1);   // Xft names start with a style letter
  std::vector<Fact> f = {
      {_("OS"), os_name()},
      {_("Host"), host_name()},
      {_("Kernel"), kernel()},
      {_("Uptime"), uptime()},
      {_("Packages"), packages(home)},
      {_("Shell"), shell_name()},
      {_("Resolution"), resolution()},
      {_("DE"), "Nirvana Desktop " NIRVANA_VERSION},
      {_("WM"), "Insight"},
      {_("WM Theme"), fmt(_("%1 (title bar: %2)"), scheme, style)},
      {_("Theme"), scheme},
      {_("Icons"), icon_style() == 0 ? _("Retro") : _("Platinum")},
      {_("Font"), font},
      {_("Cursor"), cursor_theme()},
      {_("Terminal"), "Nirvana Shell"},
      {_("Terminal Font"), terminal_font()},
      {_("CPU"), cpu()},
      {_("GPU"), gpu()},
      {_("Memory"), memory()},
      {_("Disk (/)"), disk("/")},
  };
  std::string bat = battery();
  if (!bat.empty()) f.push_back({_("Battery"), bat});
  f.push_back({_("Local IP"), local_ip()});
  f.push_back({_("Locale"), locale_name()});
  return f;
}

// ---------------------------------------------------------------------------
// Licence and acknowledgements
// ---------------------------------------------------------------------------
std::string licence_text() {
  std::string t = std::string("Nirvana Desktop ") + NIRVANA_VERSION + "\n" +
                  _("Copyright © 2026 the Nirvana Desktop authors") + "\n\n" +
                  _("This program is free software: you can redistribute it and/or modify it under the terms of "
                    "the GNU General Public License as published by the Free Software Foundation, either version 3 "
                    "of the License, or (at your option) any later version.") + "\n\n" +
                  _("This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; "
                    "without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. "
                    "See the GNU General Public License for more details.") + "\n\n" +
                  _("The full text of the licence follows (only the English original is legally binding).") +
                  "\n\n* * *\n\n";
  std::string file = data_file("LICENSE");
  if (file.empty() && !access("/usr/share/common-licenses/GPL-3", R_OK)) file = "/usr/share/common-licenses/GPL-3";
  std::ifstream in(file);
  if (in) {
    std::stringstream ss;
    ss << in.rdbuf();
    t += ss.str();
  } else {
    t += _("The licence file was not found. See https://www.gnu.org/licenses/gpl-3.0.html");
  }
  return t;
}

std::string credits_text() {
  std::string t;
  auto head = [&](const std::string &h) { t += (t.empty() ? "" : "\n") + h + "\n"; };
  auto item = [&](const std::string &s) { t += "  • " + s + "\n"; };
  t += _("Nirvana Desktop stands on the work of these people and projects. Thank you!") + std::string("\n");
  head(_("Libraries in the code"));
  item("FLTK 1.4 — Bill Spitzak, Michael Sweet, Matthias Melcher, Albrecht Schlosser, Greg Ercolano, Ian MacArthur "
       + std::string(_("and the whole FLTK team")) + " (LGPL-2.0 " + _("with the FLTK exceptions") + ")");
  item(std::string("libvterm — Paul Evans (MIT): ") + _("the terminal emulation in Nirvana Shell"));
  item(std::string("Xlib, libXext, libXss — ") + _("the X.Org Foundation and the X Window System authors") + " (MIT/X11)");
  item(std::string("libdbus — freedesktop.org (AFL-2.1 / GPL-2.0)"));
  head(_("Color schemes"));
  item("Redmond97 SE — Sliver X (GPL-3.0)");
  item("Redmond97 — matthewmx86 (GPL-3.0)");
  head(_("Programs Nirvana Desktop works with"));
  item("NetworkManager, UDisks2, polkit, elogind, D-Bus");
  item("PipeWire, PulseAudio, ALSA");
  item("squashfs-tools (unsquashfs), bubblewrap, xdg-desktop-portal");
  head(_("Standards"));
  item(std::string("freedesktop.org: XDG Base Directory, Desktop Entry, Icon Theme, ") + _("Trash, autostart"));
  item("ICCCM, EWMH, XEmbed");
  item("StatusNotifierItem (KDE), DBusMenu (Canonical)");
  item(std::string("AppImage — Simon Peter ") + _("and the AppImage team"));
  head(_("Inspirations"));
  item(_("the desktops of the late 1980s and the 1990s: bevelled gray windows, a menu bar at the top, icons on the desktop"));
  item(std::string("neofetch — Dylan Araps: ") + _("the layout of “About This Computer”"));
  item(std::string("Devuan, Debian — ") + _("the system underneath"));
  head(_("Made with"));
  item(std::string("Claude (Anthropic) — ") + _("co-author of the code"));
  return t;
}

// A read-only text pane. Lines for which bold(line number, line) is true are set in bold.
Fl_Text_Display *text_pane(int X, int Y, int W, int H, const std::string &text, Fl_Text_Buffer *&buf,
                           bool (*bold)(int, const std::string &)) {
  buf = new Fl_Text_Buffer();
  buf->text(text.c_str());
  std::string style;
  std::istringstream in(text);
  std::string line;
  for (int n = 0; std::getline(in, line); n++)
    style += std::string(line.size(), bold(n, line) ? 'B' : 'A') + "A";
  style.resize(text.size(), 'A');
  auto *sbuf = new Fl_Text_Buffer();
  sbuf->text(style.c_str());
  static Fl_Text_Display::Style_Table_Entry table[2];
  table[0] = {CONTENT_TEXT, FONT_SMALL, SIZE_SYSTEM, 0, 0};
  table[1] = {CONTENT_TEXT, FONT_SYSTEM, SIZE_SYSTEM, 0, 0};
  auto *d = new Fl_Text_Display(X, Y, W, H);
  d->buffer(buf);
  d->highlight_data(sbuf, table, 2, 'A', nullptr, nullptr);
  d->box(WELL_BOX);
  d->color(CONTENT);
  d->textcolor(CONTENT_TEXT);
  d->selection_color(SEL);
  d->textfont(FONT_SMALL);
  d->textsize(SIZE_SYSTEM);
  d->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
  d->scrollbar_width(16);
  return d;
}

}  // namespace

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------
void Desktop::about() {
  const int W = 700, H = 540;
  int X = (w() - W) / 2, Y = std::max(30, (h() - H) / 3);
  int ox = csd_mode ? FRAME : 0, oy = csd_mode ? TITLE_H : 0;
  auto *win = new NWin(this, X - ox, Y - oy, W + 2 * ox, H + oy + ox, _("About This Computer"), true);
  win->color(GRAY);

  // three "tabs" as a row of buttons, the chosen one held down
  const char *names[3] = {_("System Info"), _("Licence"), _("Acknowledgements")};
  fl_font(FONT_SYSTEM, SIZE_SYSTEM);
  int tw[3], total = 0;
  for (int i = 0; i < 3; i++) { tw[i] = (int)fl_width(names[i]) + 32; total += tw[i]; }
  int bx = ox + (W - total) / 2;
  int py = oy + 44, ph = H - 44 - 48;
  struct Tabs { Fl_Button *b[3]; Fl_Widget *pane[3]; };
  auto *tabs = new Tabs;
  for (int i = 0; i < 3; i++) {
    auto *b = new Fl_Button(bx, oy + 12, tw[i], 22, names[i]);
    b->box(BUTTON_BOX);
    b->down_box(BUTTON_DOWN);
    b->color(GRAY);
    b->labelfont(FONT_SYSTEM);
    b->labelsize(SIZE_SYSTEM);
    b->type(FL_RADIO_BUTTON);
    tabs->b[i] = b;
    bx += tw[i];
  }

  auto *sys = new SystemView(ox + 12, py, W - 24, ph);
  sys->title = user_host();
  sys->facts = gather(home_);
  Fl_Text_Buffer *b1 = nullptr, *b2 = nullptr;
  tabs->pane[0] = sys;
  tabs->pane[1] = text_pane(ox + 16, py + 4, W - 32, ph - 8, licence_text(), b1,
                            [](int n, const std::string &) { return n == 0; });
  tabs->pane[2] = text_pane(ox + 16, py + 4, W - 32, ph - 8, credits_text(), b2,
                            [](int n, const std::string &l) { return n > 0 && !l.empty() && l[0] != ' '; });

  auto pick = [](Tabs *t, int i) {
    for (int k = 0; k < 3; k++) {
      t->b[k]->value(k == i);
      if (k == i) t->pane[k]->show(); else t->pane[k]->hide();
    }
  };
  for (int i = 0; i < 3; i++) {
    tabs->b[i]->callback([](Fl_Widget *w, void *d) {
      auto *t = (Tabs *)d;
      for (int k = 0; k < 3; k++)
        if (t->b[k] == w) {
          for (int j = 0; j < 3; j++) {
            t->b[j]->value(j == k);
            if (j == k) t->pane[j]->show(); else t->pane[j]->hide();
          }
          w->window()->redraw();
        }
    }, tabs);
  }
  pick(tabs, 0);

  auto *ok = new Fl_Button(ox + W - 16 - 76, oy + H - 36, 76, 22, _("OK"));
  ok->box(BUTTON_BOX);
  ok->down_box(BUTTON_DOWN);
  ok->color(GRAY);
  ok->labelfont(FONT_SYSTEM);
  ok->labelsize(SIZE_SYSTEM);
  ok->shortcut(FL_Enter);
  ok->callback(dialog_cb, (void *)0L);
  win->actions.push_back(nullptr);
  win->end();
  win->set_modal();
  wins_.push_back(win);
  // The tab state lives as long as the window. The two small text buffers are
  // left alone: a display still refers to its buffer while it is being destroyed.
  struct Owner : Fl_Widget {
    Tabs *t;
    explicit Owner(Tabs *t) : Fl_Widget(0, 0, 0, 0), t(t) {}
    ~Owner() { delete t; }
    void draw() override {}
  };
  win->add(new Owner(tabs));
  win->show();
}
