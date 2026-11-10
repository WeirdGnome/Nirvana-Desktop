// Nirvana Desktop - gary: przełącznik aplikacji
#include "gary.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/Fl_RGB_Image.H>
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
  std::ifstream in(g_file);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string cls, name, icon;
    if (std::getline(ss, cls, '\t') && std::getline(ss, name, '\t')) {
      std::getline(ss, icon, '\t');
      g_known[cls] = {name, icon};
    }
  }
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
  if (!strcasecmp(cls.c_str(), "nirvana-alice")) return _("Wygląd");   // panele Nirvana Desktop (FLTK zmienia "l" na "L")
  if (!strcasecmp(cls.c_str(), "nirvana-siec")) return _("Sieć");
  if (!strcasecmp(cls.c_str(), "nirvana-shell")) return _("Nirvana Shell");
  if (!strcasecmp(cls.c_str(), "nirvana-zasilanie")) return _("Zasilanie");
  if (!strcasecmp(cls.c_str(), "nirvana-preferencje")) return _("Preferencje");
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
  cache[{w, size}] = result;
  return result;
}

} // namespace gary
