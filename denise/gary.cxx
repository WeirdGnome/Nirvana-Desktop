// Nirvana Desktop - gary: przełącznik aplikacji
#include "gary.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_Image.H>
#include <FL/platform.H>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <strings.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace gary {

static Atom atom(const char *n) { return XInternAtom(fl_display, n, False); }
static Window root() { return DefaultRootWindow(fl_display); }

// ---------------------------------------------------------------------------
// Klasa okna -> nazwa i ikona aplikacji uruchomionej z Ramblera (AppImage)
// ---------------------------------------------------------------------------
struct Known { std::string name, icon; };
static std::map<std::string, Known> g_known;     // zapamiętane na stałe
static std::string g_file;
struct Launch { std::string name, icon; double when; };
static std::vector<Launch> g_pending;            // świeżo uruchomione, jeszcze bez okna
static std::set<std::string> g_seen;             // klasy widziane w bieżącej sesji

static double now() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static void save_known() {
  std::ofstream out(g_file);
  for (auto &[cls, k] : g_known) out << cls << '\t' << k.name << '\t' << k.icon << '\n';
}

void init(const std::string &home) {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  fs::path dir = xdg && *xdg ? fs::path(xdg) : fs::path(home) / ".config";
  dir /= "nirvana";
  std::error_code ec;
  fs::create_directories(dir, ec);
  g_file = (dir / "aplikacje.klasy").string();
  bool stale = false;
  {
  std::ifstream in(g_file);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string cls, name, icon;
    if (std::getline(ss, cls, '\t') && std::getline(ss, name, '\t')) {
      std::getline(ss, icon, '\t');
      // po zmianie nazwy LisaOS -> Nirvana pamięć podręczna ikon jest w innym miejscu
      if (auto p = icon.find("/lisaos/"); p != std::string::npos) { icon.replace(p, 8, "/nirvana/"); stale = true; }
      if (!icon.empty() && !fs::exists(icon, ec)) { icon.clear(); stale = true; }   // ikona zniknęła
      g_known[cls] = {name, icon};
    }
  }
  }
  if (stale) save_known();
}

void note_launch(const std::string &name, const std::string &icon_file) {
  g_pending.push_back({name, icon_file, now()});
}

// ---------------------------------------------------------------------------
// Właściwości okien
// ---------------------------------------------------------------------------
static std::vector<Window> client_list() {
  std::vector<Window> v;
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  if (XGetWindowProperty(fl_display, root(), atom("_NET_CLIENT_LIST"), 0, 4096, False, XA_WINDOW,
                         &type, &fmt, &n, &after, &data) == Success && data) {
    v.assign((Window *)data, (Window *)data + n);
    XFree(data);
  }
  return v;
}

static std::string wm_class(Window w) {
  XClassHint ch;
  std::string r;
  if (XGetClassHint(fl_display, w, &ch)) {
    if (ch.res_class) { r = ch.res_class; XFree(ch.res_class); }
    if (ch.res_name) XFree(ch.res_name);
  }
  return r;
}

static bool has_atom(Window w, const char *prop, const char *value) {
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  bool found = false;
  Atom v = atom(value);
  if (XGetWindowProperty(fl_display, w, atom(prop), 0, 64, False, XA_ATOM, &type, &fmt, &n, &after,
                         &data) == Success && data) {
    for (unsigned long i = 0; i < n; i++) if (((Atom *)data)[i] == v) found = true;
    XFree(data);
  }
  return found;
}

// "org.gnome.Nautilus" -> "Nautilus", "firefox" -> "Firefox"
static std::string pretty(const std::string &cls) {
  if (!strcasecmp(cls.c_str(), "nirvana-alice")) return _("Appearance");   // panele Nirvana Desktop (FLTK zmienia "l" na "L")
  if (!strcasecmp(cls.c_str(), "nirvana-siec")) return _("Network");
  if (!strcasecmp(cls.c_str(), "nirvana-shell")) return _("Nirvana Shell");
  if (!strcasecmp(cls.c_str(), "nirvana-zasilanie")) return _("Power");
  if (!strcasecmp(cls.c_str(), "nirvana-preferencje")) return _("Preferences");
  std::string s = cls;
  if (auto dot = s.rfind('.'); dot != std::string::npos && dot + 1 < s.size()) s = s.substr(dot + 1);
  if (!s.empty() && islower((unsigned char)s[0])) s[0] = (char)toupper((unsigned char)s[0]);
  return s;
}

void clients_changed() {
  // Nowe klasy okien zestawiamy ze świeżo uruchomionymi AppImage.
  double t = now();
  g_pending.erase(std::remove_if(g_pending.begin(), g_pending.end(),
                                 [t](const Launch &l) { return t - l.when > 20; }),
                  g_pending.end());
  bool changed = false;
  for (Window w : client_list()) {
    std::string cls = wm_class(w);
    if (cls.empty() || cls.rfind("Nirvana-", 0) == 0 || g_seen.count(cls)) continue;
    g_seen.insert(cls);
    if (!g_pending.empty() && !g_known.count(cls)) {
      g_known[cls] = {g_pending.front().name, g_pending.front().icon};
      g_pending.erase(g_pending.begin());
      changed = true;
    }
  }
  if (changed) save_known();
}

std::vector<App> list_apps(bool (*skip)(Window)) {
  std::vector<App> apps;
  for (Window w : client_list()) {
    if (skip && skip(w)) continue;
    if (has_atom(w, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK") ||
        has_atom(w, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DESKTOP"))
      continue;
    std::string cls = wm_class(w);
    if (cls.empty()) cls = "?";
    auto it = std::find_if(apps.begin(), apps.end(), [&](const App &a) { return a.cls == cls; });
    if (it == apps.end()) {
      App a;
      a.cls = cls;
      if (auto k = g_known.find(cls); k != g_known.end()) {
        a.name = k->second.name;
        a.icon_file = k->second.icon;
      } else {
        a.name = pretty(cls);
      }
      apps.push_back(a);
      it = apps.end() - 1;
    }
    it->wins.push_back(w);
  }
  for (auto &a : apps) {
    a.hidden = true;
    for (Window w : a.wins) if (!is_hidden(w)) a.hidden = false;
  }
  return apps;
}

Window active_window() {
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  Window w = 0;
  if (XGetWindowProperty(fl_display, root(), atom("_NET_ACTIVE_WINDOW"), 0, 1, False, XA_WINDOW,
                         &type, &fmt, &n, &after, &data) == Success && data) {
    if (n) w = *(Window *)data;
    XFree(data);
  }
  return w;
}

bool is_hidden(Window w) {
  if (has_atom(w, "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN")) return true;
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  bool iconic = false;
  Atom ws = atom("WM_STATE");
  if (XGetWindowProperty(fl_display, w, ws, 0, 2, False, ws, &type, &fmt, &n, &after, &data) == Success &&
      data) {
    iconic = n && ((long *)data)[0] == IconicState;
    XFree(data);
  }
  return iconic;
}

static void message(Window w, const char *type, long l0, long l1, long l2) {
  XEvent ev{};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = w;
  ev.xclient.message_type = atom(type);
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = l0;
  ev.xclient.data.l[1] = l1;
  ev.xclient.data.l[2] = l2;
  XSendEvent(fl_display, root(), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(fl_display);
}

void activate(Window w) { message(w, "_NET_ACTIVE_WINDOW", 2, CurrentTime, 0); }   // 2 = przełącznik
void hide(Window w) {
  // znacznik dla Insighta: to "Ukryj" z menu aplikacji, nie ikonizacja - bez ikony na biurku
  long one = 1;
  XChangeProperty(fl_display, w, atom("_NIRVANA_UKRYJ"), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&one, 1);
  XIconifyWindow(fl_display, w, DefaultScreen(fl_display));
  XFlush(fl_display);
}

std::string window_title(Window w) {
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  std::string t;
  if (XGetWindowProperty(fl_display, w, atom("_NET_WM_NAME"), 0, 1024, False, atom("UTF8_STRING"), &type, &fmt,
                         &n, &after, &data) == Success && data && n)
    t.assign((char *)data, n);
  if (data) { XFree(data); data = nullptr; }
  if (t.empty() && XGetWindowProperty(fl_display, w, XA_WM_NAME, 0, 1024, False, AnyPropertyType, &type, &fmt,
                                      &n, &after, &data) == Success && data && n)
    t.assign((char *)data, n);
  if (data) XFree(data);
  return t;
}
void close(Window w) { message(w, "_NET_CLOSE_WINDOW", CurrentTime, 2, 0); }

// ---------------------------------------------------------------------------
// Ikona okna z _NET_WM_ICON (tablica: szer, wys, piksele ARGB, ... kolejne rozmiary)
// ---------------------------------------------------------------------------
Fl_Image *window_icon(Window w, int size) {
  static std::map<std::pair<Window, int>, Fl_Image *> cache;
  if (auto it = cache.find({w, size}); it != cache.end()) return it->second;
  Fl_Image *result = nullptr;
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  if (XGetWindowProperty(fl_display, w, atom("_NET_WM_ICON"), 0, 1 << 20, False, XA_CARDINAL, &type,
                         &fmt, &n, &after, &data) == Success && data && n > 2) {
    unsigned long *p = (unsigned long *)data;   // Xlib: 32-bitowe wartości w long
    unsigned long best = 0, best_w = 0, i = 0;
    while (i + 2 < n) {
      unsigned long iw = p[i], ih = p[i + 1];
      if (!iw || !ih || i + 2 + iw * ih > n) break;
      // najmniejsza ikona nie mniejsza niż size, a jeśli takiej nie ma - największa
      unsigned long sz = (unsigned long)size;
      bool better = !best_w || (iw >= sz && (best_w < sz || iw < best_w)) || (best_w < sz && iw > best_w);
      if (better) { best = i; best_w = iw; }
      i += 2 + iw * ih;
    }
    if (best_w) {
      unsigned long iw = p[best], ih = p[best + 1];
      auto *rgba = new unsigned char[iw * ih * 4];
      for (unsigned long k = 0; k < iw * ih; k++) {
        unsigned long argb = p[best + 2 + k];
        rgba[k * 4 + 0] = (argb >> 16) & 0xFF;
        rgba[k * 4 + 1] = (argb >> 8) & 0xFF;
        rgba[k * 4 + 2] = argb & 0xFF;
        rgba[k * 4 + 3] = (argb >> 24) & 0xFF;
      }
      auto *img = new Fl_RGB_Image(rgba, (int)iw, (int)ih, 4);
      img->alloc_array = 1;   // obraz zwolni dane sam
      Fl_RGB_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
      result = img->copy(size, size);
      delete img;
    }
  }
  if (data) XFree(data);
  if (result) cache[{w, size}] = result;   // brak ikony nie jest zapamiętywany - program może ją ustawić później
  return result;
}

// --- ikona z pliku .desktop i motywu ikon -------------------------------------
static std::vector<std::string> data_dirs() {
  std::vector<std::string> v;
  const char *h = getenv("HOME");
  std::string home = h ? h : "/";
  const char *xd = getenv("XDG_DATA_HOME");
  v.push_back(xd && *xd ? xd : home + "/.local/share");
  v.push_back(home + "/.local/share/flatpak/exports/share");
  v.push_back("/var/lib/flatpak/exports/share");
  const char *dirs = getenv("XDG_DATA_DIRS");
  std::stringstream ss(dirs && *dirs ? dirs : "/usr/local/share:/usr/share");
  std::string d;
  while (std::getline(ss, d, ':'))
    if (!d.empty() && std::find(v.begin(), v.end(), d) == v.end()) v.push_back(d);
  for (const char *x : {"/usr/local/share", "/usr/share"})
    if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x);
  return v;
}

static std::string lower(std::string s) {
  for (auto &c : s) c = (char)tolower((unsigned char)c);
  return s;
}

// Wartość klucza z sekcji [Desktop Entry]
static std::string desktop_key(const std::string &file, const char *key) {
  std::ifstream in(file);
  std::string line, k = std::string(key) + "=";
  bool entry = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line[0] == '[') { entry = line == "[Desktop Entry]"; continue; }
    if (entry && line.rfind(k, 0) == 0) return line.substr(k.size());
  }
  return "";
}

// Plik .desktop programu o klasie okna cls / nazwie name.
static std::string find_desktop_file(const std::string &cls, const std::string &name) {
  std::string lc = lower(cls), ln = lower(name);
  std::string by_name;
  for (auto &dd : data_dirs()) {
    std::error_code ec;
    fs::path apps = fs::path(dd) / "applications";
    if (!fs::is_directory(apps, ec)) continue;
    for (auto &de : fs::directory_iterator(apps, fs::directory_options::skip_permission_denied, ec)) {
      if (de.path().extension() != ".desktop") continue;
      std::string f = de.path().string(), id = lower(de.path().stem().string());
      // dokładne dopasowanie po StartupWMClass jest najpewniejsze
      std::string wmc = lower(desktop_key(f, "StartupWMClass"));
      if (!wmc.empty() && (wmc == lc || wmc == ln)) return f;
      // "librewolf", "abiword", "io.gitlab.librewolf-community" (Flatpak)
      if (by_name.empty() && (id == lc || id == ln)) by_name = f;
      if (by_name.empty()) {
        auto dot = id.rfind('.');
        std::string last = dot == std::string::npos ? id : id.substr(dot + 1);
        if (dot != std::string::npos && (last == lc || last == ln || last.rfind(lc + "-", 0) == 0)) by_name = f;
      }
    }
  }
  return by_name;
}

static std::string gtk_icon_theme() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  std::string cfg = x && *x ? x : std::string(h ? h : "/") + "/.config";
  for (auto f : {cfg + "/gtk-3.0/settings.ini", cfg + "/gtk-4.0/settings.ini"}) {
    std::ifstream in(f);
    std::string line;
    while (std::getline(in, line))
      if (line.rfind("gtk-icon-theme-name", 0) == 0) {
        std::string v = line.substr(line.find('=') + 1);
        v.erase(0, v.find_first_not_of(" \t\""));
        v.erase(v.find_last_not_of(" \t\"") + 1);
        if (!v.empty()) return v;
      }
  }
  return "";
}

// Plik ikony o nazwie name z motywu (najbliżej rozmiaru size), hicolor albo pixmaps.
static std::string theme_icon_file(const std::string &name, int size) {
  std::error_code ec;
  if (name.empty()) return "";
  if (name[0] == '/') return fs::exists(name, ec) ? name : "";
  std::vector<std::string> themes;
  std::string t = gtk_icon_theme();
  if (!t.empty()) themes.push_back(t);
  for (auto x : {"hicolor", "Adwaita", "breeze", "Papirus"}) if (t != x) themes.push_back(x);
  std::vector<std::string> bases;
  const char *h = getenv("HOME");
  bases.push_back(std::string(h ? h : "/") + "/.icons");
  for (auto &d : data_dirs()) bases.push_back(d + "/icons");
  std::vector<int> sizes = {size, 48, 64, 128, 256, 512, 24, 22};
  for (auto &th : themes)
    for (auto &b : bases) {
      fs::path root = fs::path(b) / th;
      if (!fs::is_directory(root, ec)) continue;
      for (int sz : sizes) {
        std::string dim = std::to_string(sz) + "x" + std::to_string(sz);
        for (auto p : {root / dim / "apps" / (name + ".png"), root / "apps" / std::to_string(sz) / (name + ".png"),
                       root / "apps" / std::to_string(sz) / (name + ".svg")})
          if (fs::exists(p, ec)) return p.string();
      }
      for (auto p : {root / "scalable" / "apps" / (name + ".svg"), root / "apps" / "scalable" / (name + ".svg")})
        if (fs::exists(p, ec)) return p.string();
    }
  for (auto &d : data_dirs())
    for (auto ext : {".png", ".svg", ".xpm"}) {
      fs::path p = fs::path(d) / "pixmaps" / (name + ext);
      if (fs::exists(p, ec)) return p.string();
    }
  return "";
}

Fl_Image *desktop_icon(Window w, int size) {
  static std::map<std::string, Fl_Image *> cache;   // klasa okna -> obrazek
  XClassHint ch;
  std::string cls, name;
  if (XGetClassHint(fl_display, w, &ch)) {
    if (ch.res_class) { cls = ch.res_class; XFree(ch.res_class); }
    if (ch.res_name) { name = ch.res_name; XFree(ch.res_name); }
  }
  if (cls.empty() && name.empty()) return nullptr;
  std::string key = cls + "|" + name + "|" + std::to_string(size);
  if (auto it = cache.find(key); it != cache.end()) return it->second;
  std::string icon;
  std::string df = find_desktop_file(cls, name);
  if (!df.empty()) icon = desktop_key(df, "Icon");
  std::string file = theme_icon_file(icon, size);
  if (file.empty()) file = theme_icon_file(lower(cls), size);
  if (file.empty()) file = theme_icon_file(lower(name), size);
  Fl_Image *result = nullptr;
  if (!file.empty()) {
    static bool reg = false;
    if (!reg) { fl_register_images(); reg = true; }
    if (Fl_Shared_Image *sh = Fl_Shared_Image::get(file.c_str())) {
      if (sh->w() > 0 && sh->h() > 0) {
        Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
        const Fl_Image *src = sh->image() ? sh->image() : sh;
        result = src->copy(size, size);
      }
      sh->release();
    }
  }
  cache[key] = result;
  return result;
}

} // namespace gary
