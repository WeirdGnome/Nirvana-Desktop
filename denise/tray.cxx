// Nirvana Desktop - the system tray: XEmbed + StatusNotifierItem (see tray.h)
#include "tray.h"
#include "platinum.h"
#include "anim.h"

#include <FL/Fl.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_Tooltip.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dbus/dbus.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace plat;

namespace tray {

static const int SLOT = 22;     // width of the place for one icon
static const int ICON = 16;     // StatusNotifierItem icons
static const int PAD = 3;       // gap on the left before the first icon

// ---------------------------------------------------------------------------
// Stan
// ---------------------------------------------------------------------------
struct Slot {
  bool xembed = false;
  // XEmbed
  Window icon = 0, box = 0;     // the program's window and our frame window
  bool mapped = false;
  // StatusNotifierItem
  std::string service, owner, path;   // owner: unique name (:1.42)
  std::string id, title, tip, status = "Active", icon_name, att_icon_name, theme_path, menu;
  bool item_is_menu = false;
  std::unique_ptr<Fl_RGB_Image> img;
  bool shown() const { return xembed ? mapped : status != "Passive"; }
};

static std::vector<std::unique_ptr<Slot>> g_slots;
static Fl_Window *g_desk = nullptr;
static Fl_Widget *g_area = nullptr;
static std::function<void()> g_changed;
static Fl_Menu_Button *g_menu = nullptr;   // hidden button for showing the dbusmenu menu

static void changed() {
  if (g_area) g_area->redraw();
  if (g_changed) g_changed();
}

int width() {
  int n = 0;
  for (auto &s : g_slots) if (s->shown()) n++;
  return n ? PAD + n * SLOT : 0;
}

static void place_boxes();

// ---------------------------------------------------------------------------
// XEmbed: the tray manager
// ---------------------------------------------------------------------------
static Display *dpy = nullptr;
static Window g_manager = 0;
static Atom a_selection, a_opcode, a_xembed, a_xembed_info, a_orientation, a_visual, a_manager;
static const long SYSTEM_TRAY_REQUEST_DOCK = 0;
static const long XEMBED_EMBEDDED_NOTIFY = 0;
static const unsigned long XEMBED_MAPPED = 1;

static unsigned long bg_pixel() { return fl_xpixel(GRAY); }

static Slot *slot_for_icon(Window w) {
  for (auto &s : g_slots) if (s->xembed && s->icon == w) return s.get();
  return nullptr;
}

static void update_mapped(Slot *s) {
  Atom type;
  int fmt;
  unsigned long n = 0, after;
  unsigned char *data = nullptr;
  bool map = true;   // without _XEMBED_INFO: show it (most trays do so)
  if (XGetWindowProperty(dpy, s->icon, a_xembed_info, 0, 2, False, a_xembed_info, &type, &fmt, &n, &after,
                         &data) == Success && data) {
    if (n >= 2 && fmt == 32) map = (((unsigned long *)data)[1] & XEMBED_MAPPED) != 0;
    XFree(data);
  }
  if (map == s->mapped) return;
  s->mapped = map;
  if (map) { XMapRaised(dpy, s->icon); XMapWindow(dpy, s->box); }
  else XUnmapWindow(dpy, s->box);
  changed();
}

static void dock(Window icon) {
  if (slot_for_icon(icon)) return;
  XWindowAttributes wa;
  if (!XGetWindowAttributes(dpy, icon, &wa)) return;
  auto s = std::make_unique<Slot>();
  s->xembed = true;
  s->icon = icon;
  // the X windows are in pixels - the interface scale (px)
  int h = px(MENUBAR_H - 3);
  s->box = XCreateSimpleWindow(dpy, fl_xid(g_desk), -px(SLOT), px(1), px(SLOT), h, 0, 0, bg_pixel());
  XSelectInput(dpy, icon, StructureNotifyMask | PropertyChangeMask);
  XAddToSaveSet(dpy, icon);   // when Rambler goes away, the icon returns to the root window instead of dying
  XReparentWindow(dpy, icon, s->box, 0, 0);
  XResizeWindow(dpy, icon, px(SLOT), h);
  // XEMBED_EMBEDDED_NOTIFY: the program knows it has been embedded
  XEvent ev{};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = icon;
  ev.xclient.message_type = a_xembed;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = CurrentTime;
  ev.xclient.data.l[1] = XEMBED_EMBEDDED_NOTIFY;
  ev.xclient.data.l[3] = (long)s->box;
  ev.xclient.data.l[4] = 0;
  XSendEvent(dpy, icon, False, NoEventMask, &ev);
  Slot *raw = s.get();
  g_slots.push_back(std::move(s));
  place_boxes();
  update_mapped(raw);
  XFlush(dpy);
}

static void undock(Slot *s, bool destroyed) {
  if (!destroyed) {   // the program's window is alive: hand it back to the root window
    XSelectInput(dpy, s->icon, NoEventMask);
    XUnmapWindow(dpy, s->icon);
    XReparentWindow(dpy, s->icon, DefaultRootWindow(dpy), 0, 0);
    XRemoveFromSaveSet(dpy, s->icon);
  }
  XDestroyWindow(dpy, s->box);
  for (auto it = g_slots.begin(); it != g_slots.end(); ++it)
    if (it->get() == s) { g_slots.erase(it); break; }
  changed();
}

static int x_events(void *event, void *) {
  XEvent *e = (XEvent *)event;
  switch (e->type) {
  case ClientMessage:
    if (e->xclient.window == g_manager && e->xclient.message_type == a_opcode &&
        e->xclient.data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
      dock((Window)e->xclient.data.l[2]);
      return 1;
    }
    break;
  case DestroyNotify:
    if (Slot *s = slot_for_icon(e->xdestroywindow.window)) { undock(s, true); return 1; }
    break;
  case ReparentNotify:
    if (Slot *s = slot_for_icon(e->xreparent.window))
      if (e->xreparent.parent != s->box) { s->icon = 0; undock(s, true); return 1; }   // the program took its window away
    break;
  case PropertyNotify:
    if (e->xproperty.atom == a_xembed_info)
      if (Slot *s = slot_for_icon(e->xproperty.window)) { update_mapped(s); return 1; }
    break;
  case ConfigureNotify:
    // programs like to resize the icon themselves - we keep it in the frame
    if (Slot *s = slot_for_icon(e->xconfigure.window)) {
      int w = px(SLOT), h = px(MENUBAR_H - 3);
      if (e->xconfigure.width != w || e->xconfigure.height != h || e->xconfigure.x || e->xconfigure.y)
        XMoveResizeWindow(dpy, s->icon, 0, 0, w, h);
      return 1;
    }
    break;
  case SelectionClear:
    if (e->xselectionclear.window == g_manager) {   // another tray took over the selection
      std::vector<Slot *> xe;
      for (auto &s : g_slots) if (s->xembed) xe.push_back(s.get());
      for (auto *s : xe) undock(s, false);
      XDestroyWindow(dpy, g_manager);
      g_manager = 0;
      return 1;
    }
    break;
  }
  return 0;
}

static void init_xembed() {
  dpy = fl_display;
  int scr = DefaultScreen(dpy);
  char name[64];
  snprintf(name, sizeof name, "_NET_SYSTEM_TRAY_S%d", scr);
  a_selection = XInternAtom(dpy, name, False);
  a_opcode = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
  a_xembed = XInternAtom(dpy, "_XEMBED", False);
  a_xembed_info = XInternAtom(dpy, "_XEMBED_INFO", False);
  a_orientation = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
  a_visual = XInternAtom(dpy, "_NET_SYSTEM_TRAY_VISUAL", False);
  a_manager = XInternAtom(dpy, "MANAGER", False);
  if (XGetSelectionOwner(dpy, a_selection) != None) return;   // there is another tray already

  Window root = RootWindow(dpy, scr);
  g_manager = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
  long orient = 0;   // horizontal
  XChangeProperty(dpy, g_manager, a_orientation, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&orient, 1);
  // a plain (opaque) visual: the icons are drawn on the background of our frame
  long vid = (long)XVisualIDFromVisual(DefaultVisual(dpy, scr));
  XChangeProperty(dpy, g_manager, a_visual, XA_VISUALID, 32, PropModeReplace, (unsigned char *)&vid, 1);
  XSelectInput(dpy, g_manager, StructureNotifyMask);
  XSetSelectionOwner(dpy, a_selection, g_manager, CurrentTime);
  if (XGetSelectionOwner(dpy, a_selection) != g_manager) {
    XDestroyWindow(dpy, g_manager);
    g_manager = 0;
    return;
  }
  // MANAGER announcement: programs that waited for a tray dock now
  XEvent ev{};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = root;
  ev.xclient.message_type = a_manager;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = CurrentTime;
  ev.xclient.data.l[1] = (long)a_selection;
  ev.xclient.data.l[2] = (long)g_manager;
  XSendEvent(dpy, root, False, StructureNotifyMask, &ev);
  XFlush(dpy);
}

void recolor() {
  if (!dpy) return;
  for (auto &s : g_slots)
    if (s->xembed) {
      XSetWindowBackground(dpy, s->box, bg_pixel());
      XClearArea(dpy, s->box, 0, 0, 0, 0, False);
      // icons with a "parent relative" background have to redraw
      XClearArea(dpy, s->icon, 0, 0, 0, 0, True);
    }
  XFlush(dpy);
}

void shutdown() {
  if (!dpy) return;
  std::vector<Slot *> xe;
  for (auto &s : g_slots) if (s->xembed) xe.push_back(s.get());
  for (auto *s : xe) undock(s, false);
  if (g_manager) {
    XSetSelectionOwner(dpy, a_selection, None, CurrentTime);
    XDestroyWindow(dpy, g_manager);
    g_manager = 0;
  }
  XFlush(dpy);
}

// ---------------------------------------------------------------------------
// Icons from the theme (StatusNotifierItem usually gives an icon name)
// ---------------------------------------------------------------------------
static std::string icon_theme_name() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  std::string cfg = x && *x ? x : std::string(h ? h : "/") + "/.config";
  for (auto f : {cfg + "/gtk-3.0/settings.ini", cfg + "/gtk-4.0/settings.ini"}) {
    std::ifstream in(f);
    std::string line;
    while (std::getline(in, line))
      if (line.rfind("gtk-icon-theme-name", 0) == 0) {
        auto eq = line.find('=');
        std::string v = line.substr(eq + 1);
        v.erase(0, v.find_first_not_of(" \t\""));
        v.erase(v.find_last_not_of(" \t\"") + 1);
        if (!v.empty()) return v;
      }
  }
  return "";
}

static std::vector<std::string> icon_bases() {
  std::vector<std::string> b;
  const char *h = getenv("HOME");
  std::string home = h ? h : "/";
  const char *xd = getenv("XDG_DATA_HOME");
  b.push_back((xd && *xd ? std::string(xd) : home + "/.local/share") + "/icons");
  b.push_back(home + "/.icons");
  const char *dirs = getenv("XDG_DATA_DIRS");
  std::string ds = dirs && *dirs ? dirs : "/usr/local/share:/usr/share";
  size_t p = 0;
  while (p <= ds.size()) {
    size_t c = ds.find(':', p);
    std::string d = ds.substr(p, c == std::string::npos ? std::string::npos : c - p);
    if (!d.empty()) b.push_back(d + "/icons");
    if (c == std::string::npos) break;
    p = c + 1;
  }
  return b;
}

// Theme index: icon name -> the best file (PNG around 22 px, then SVG).
using Index = std::map<std::string, std::pair<int, std::string>>;
static int score(const std::string &path) {
  bool svg = path.size() > 4 && path.compare(path.size() - 4, 4, ".svg") == 0;
  int size = 0;   // from a "22x22" or "/22/" folder
  for (size_t i = 0; i < path.size(); i++)
    if (isdigit((unsigned char)path[i]) && (i == 0 || path[i - 1] == '/')) {
      size = atoi(path.c_str() + i);
      break;
    }
  if (path.find("/scalable/") != std::string::npos || svg) return 50 - (size ? std::abs(size - 22) : 0);
  if (!size) return 10;
  return 100 - std::abs(size - 22) * (size < 16 ? 4 : 1);
}
static void index_dir(const fs::path &dir, Index &idx) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return;
  for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it.depth() > 4) { it.disable_recursion_pending(); continue; }
    auto ext = it->path().extension().string();
    if (ext != ".png" && ext != ".svg" && ext != ".xpm") continue;
    std::string name = it->path().stem().string(), p = it->path().string();
    int sc = score(p);
    auto f = idx.find(name);
    if (f == idx.end() || f->second.first < sc) idx[name] = {sc, p};
  }
}
static const Index &theme_index(const std::string &theme) {
  static std::map<std::string, Index> cache;
  auto it = cache.find(theme);
  if (it != cache.end()) return it->second;
  Index &idx = cache[theme];
  for (auto &b : icon_bases()) index_dir(fs::path(b) / theme, idx);
  return idx;
}

static std::string find_icon_exact(const std::string &name, const std::string &theme_path);
// As in the icon theme specification: a missing icon is replaced by the "-symbolic" version,
// and then by a shorter and shorter name ("mail-unread" -> "mail").
static std::string find_icon(const std::string &name, const std::string &theme_path) {
  static std::map<std::string, std::string> cache;
  std::string key = name + "|" + theme_path;
  if (auto it = cache.find(key); it != cache.end()) return it->second;
  std::string n = name, found;
  while (!n.empty() && found.empty()) {
    found = find_icon_exact(n, theme_path);
    if (found.empty() && n.find('/') == std::string::npos) found = find_icon_exact(n + "-symbolic", theme_path);
    auto dash = n.rfind('-');
    if (dash == std::string::npos || n[0] == '/') break;
    n.resize(dash);
  }
  return cache[key] = found;
}

static std::string find_icon_exact(const std::string &name, const std::string &theme_path) {
  if (name.empty()) return "";
  std::error_code ec;
  if (name[0] == '/') return fs::exists(name, ec) ? name : "";
  if (!theme_path.empty()) {   // a folder given by the program (e.g. icons in an AppImage)
    static std::map<std::string, Index> extra;
    auto &idx = extra[theme_path];
    if (idx.empty()) index_dir(theme_path, idx);
    auto f = idx.find(name);
    if (f != idx.end()) return f->second.second;
  }
  std::vector<std::string> themes;
  std::string t = icon_theme_name();
  if (!t.empty()) themes.push_back(t);
  for (auto x : {"Adwaita", "breeze", "Papirus", "elementary", "gnome", "Yaru", "hicolor"})
    if (t != x) themes.push_back(x);
  for (auto &th : themes) {
    const Index &idx = theme_index(th);
    auto f = idx.find(name);
    if (f != idx.end()) return f->second.second;
  }
  for (auto ext : {".png", ".svg", ".xpm"})
    for (auto d : {"/usr/share/pixmaps/", "/usr/local/share/pixmaps/"})
      if (fs::exists(d + name + ext, ec)) return d + name + ext;
  return "";
}

// A 16x16 RGBA image from a file; "symbolic" icons take the bar's text color.
static Fl_RGB_Image *load_icon(const std::string &file, bool symbolic) {
  static bool reg = false;
  if (!reg) { fl_register_images(); reg = true; }
  Fl_Shared_Image *sh = Fl_Shared_Image::get(file.c_str());
  if (!sh) return nullptr;
  Fl_RGB_Image *out = nullptr;
  const Fl_Image *src = sh->image() ? sh->image() : sh;
  if (src->w() > 0 && src->h() > 0) {
    Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
    Fl_Image *c = src->copy(ICON, ICON);
    if (auto *r = dynamic_cast<Fl_RGB_Image *>(c)) {
      r->normalize();
      // rewritten to RGBA so the layout is the same whatever the format
      int d = r->d(), ld = r->ld() ? r->ld() : r->data_w() * d;
      auto *px = new uchar[ICON * ICON * 4];
      const uchar *sp = (const uchar *)r->data()[0];
      uchar tr, tg, tb;
      Fl::get_color(TEXT, tr, tg, tb);
      for (int y = 0; y < ICON; y++)
        for (int x = 0; x < ICON; x++) {
          const uchar *s = sp + y * ld + x * d;
          uchar *o = px + (y * ICON + x) * 4;
          if (d >= 3) { o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = d == 4 ? s[3] : 255; }
          else { o[0] = o[1] = o[2] = s[0]; o[3] = d == 2 ? s[1] : 255; }
          if (symbolic) { o[0] = tr; o[1] = tg; o[2] = tb; }
        }
      out = new Fl_RGB_Image(px, ICON, ICON, 4);
      out->alloc_array = 1;
    }
    delete c;
  }
  sh->release();
  return out;
}

// IconPixmap: ARGB32 in network order (A R G B), several sizes to choose from.
struct Pixmap { int w = 0, h = 0; std::vector<uchar> argb; };
static Fl_RGB_Image *from_pixmap(const Pixmap &p) {
  if (p.w <= 0 || p.h <= 0 || (int)p.argb.size() < p.w * p.h * 4) return nullptr;
  auto *px = new uchar[p.w * p.h * 4];
  for (int i = 0; i < p.w * p.h; i++) {
    px[i * 4 + 0] = p.argb[i * 4 + 1];
    px[i * 4 + 1] = p.argb[i * 4 + 2];
    px[i * 4 + 2] = p.argb[i * 4 + 3];
    px[i * 4 + 3] = p.argb[i * 4 + 0];
  }
  Fl_RGB_Image big(px, p.w, p.h, 4);
  Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
  auto *out = (Fl_RGB_Image *)big.copy(ICON, ICON);
  delete[] px;
  return out;
}

// ---------------------------------------------------------------------------
// D-Bus: StatusNotifierWatcher (when needed) and StatusNotifierHost
// ---------------------------------------------------------------------------
static DBusConnection *bus = nullptr;
static bool g_own_watcher = false;
static const char *WATCHER = "org.kde.StatusNotifierWatcher";
static const char *WATCHER_PATH = "/StatusNotifierWatcher";
static const char *ITEM_IFACE = "org.kde.StatusNotifierItem";

static void schedule_dispatch();
static void flush() { if (bus) { dbus_connection_flush(bus); schedule_dispatch(); } }

static std::string key_of(const Slot &s) { return s.service + s.path; }

static Slot *find_item(const std::string &owner, const std::string &path) {
  for (auto &s : g_slots)
    if (!s->xembed && s->owner == owner && (path.empty() || s->path == path)) return s.get();
  return nullptr;
}

static void emit(const char *member, const std::string &arg) {
  if (!g_own_watcher) return;
  DBusMessage *m = dbus_message_new_signal(WATCHER_PATH, WATCHER, member);
  const char *a = arg.c_str();
  if (!arg.empty() || strcmp(member, "StatusNotifierHostRegistered"))
    dbus_message_append_args(m, DBUS_TYPE_STRING, &a, DBUS_TYPE_INVALID);
  dbus_connection_send(bus, m, nullptr);
  dbus_message_unref(m);
}

// --- reading replies ---------------------------------------------------------
static std::string iter_string(DBusMessageIter *it) {
  int t = dbus_message_iter_get_arg_type(it);
  if (t == DBUS_TYPE_STRING || t == DBUS_TYPE_OBJECT_PATH) {
    const char *s = nullptr;
    dbus_message_iter_get_basic(it, &s);
    return s ? s : "";
  }
  return "";
}
static bool iter_bool(DBusMessageIter *it) {
  if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_BOOLEAN) return false;
  dbus_bool_t b = 0;
  dbus_message_iter_get_basic(it, &b);
  return b;
}
static int iter_int(DBusMessageIter *it) {
  int t = dbus_message_iter_get_arg_type(it);
  if (t == DBUS_TYPE_INT32 || t == DBUS_TYPE_UINT32) {
    dbus_int32_t v = 0;
    dbus_message_iter_get_basic(it, &v);
    return v;
  }
  return 0;
}
// a(iiay): choose the size closest to the icon (preferably not smaller)
static Pixmap iter_pixmap(DBusMessageIter *arr) {
  Pixmap best;
  if (dbus_message_iter_get_arg_type(arr) != DBUS_TYPE_ARRAY) return best;
  DBusMessageIter el;
  dbus_message_iter_recurse(arr, &el);
  while (dbus_message_iter_get_arg_type(&el) == DBUS_TYPE_STRUCT) {
    DBusMessageIter st;
    dbus_message_iter_recurse(&el, &st);
    Pixmap p;
    p.w = iter_int(&st);
    dbus_message_iter_next(&st);
    p.h = iter_int(&st);
    dbus_message_iter_next(&st);
    if (dbus_message_iter_get_arg_type(&st) == DBUS_TYPE_ARRAY) {
      DBusMessageIter bytes;
      dbus_message_iter_recurse(&st, &bytes);
      const uchar *data = nullptr;
      int n = 0;
      dbus_message_iter_get_fixed_array(&bytes, &data, &n);
      if (data && n) p.argb.assign(data, data + n);
    }
    auto better = [](const Pixmap &a, const Pixmap &b) {   // is a better than b
      if (!b.w) return true;
      bool ab = a.w >= ICON, bb = b.w >= ICON;
      if (ab != bb) return ab;
      return ab ? a.w < b.w : a.w > b.w;
    };
    if (p.w > 0 && better(p, best)) best = std::move(p);
    dbus_message_iter_next(&el);
  }
  return best;
}

static void refresh_icon(Slot *s, const Pixmap &pix, const Pixmap &att) {
  bool attention = s->status == "NeedsAttention";
  std::string name = attention && !s->att_icon_name.empty() ? s->att_icon_name : s->icon_name;
  const Pixmap &pm = attention && att.w ? att : pix;
  Fl_RGB_Image *img = nullptr;
  std::string file = find_icon(name, s->theme_path);
  if (!file.empty()) img = load_icon(file, file.find("symbolic") != std::string::npos);
  if (!img && pm.w) img = from_pixmap(pm);
  if (!img) img = nullptr;
  s->img.reset(img);
}

static void apply_props(Slot *s, DBusMessage *reply) {
  DBusMessageIter it, dict;
  if (!dbus_message_iter_init(reply, &it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) return;
  bool was_shown = s->shown();
  Pixmap pix, att;
  dbus_message_iter_recurse(&it, &dict);
  while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
    DBusMessageIter e, v;
    dbus_message_iter_recurse(&dict, &e);
    std::string k = iter_string(&e);
    dbus_message_iter_next(&e);
    dbus_message_iter_recurse(&e, &v);
    if (k == "Id") s->id = iter_string(&v);
    else if (k == "Title") s->title = iter_string(&v);
    else if (k == "Status") s->status = iter_string(&v);
    else if (k == "IconName") s->icon_name = iter_string(&v);
    else if (k == "AttentionIconName") s->att_icon_name = iter_string(&v);
    else if (k == "IconThemePath") s->theme_path = iter_string(&v);
    else if (k == "Menu") s->menu = iter_string(&v);
    else if (k == "ItemIsMenu") s->item_is_menu = iter_bool(&v);
    else if (k == "IconPixmap") pix = iter_pixmap(&v);
    else if (k == "AttentionIconPixmap") att = iter_pixmap(&v);
    else if (k == "ToolTip" && dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_STRUCT) {
      DBusMessageIter st;
      dbus_message_iter_recurse(&v, &st);
      dbus_message_iter_next(&st);   // icon name
      dbus_message_iter_next(&st);   // pixmaps
      std::string title = iter_string(&st);
      dbus_message_iter_next(&st);
      std::string desc = iter_string(&st);
      // the description is sometimes HTML (KDE) - keep the first line without tags
      std::string plain;
      bool tag = false;
      for (char c : desc) {
        if (c == '<') tag = true;
        else if (c == '>') tag = false;
        else if (!tag) plain += c;
      }
      if (auto nl = plain.find('\n'); nl != std::string::npos) plain.resize(nl);
      s->tip = title;
      if (!plain.empty() && plain != title) s->tip += (title.empty() ? "" : " - ") + plain;
    }
    dbus_message_iter_next(&dict);
  }
  refresh_icon(s, pix, att);
  if (was_shown != s->shown()) changed();
  else if (g_area) g_area->redraw();
}

struct Pending { std::string owner, path; };

static void props_reply(DBusPendingCall *pc, void *data) {
  auto *p = (Pending *)data;
  DBusMessage *r = dbus_pending_call_steal_reply(pc);
  dbus_pending_call_unref(pc);
  if (!r) return;
  if (dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
    if (Slot *s = find_item(p->owner, p->path)) apply_props(s, r);
  dbus_message_unref(r);
}

static void fetch_props(Slot *s) {
  DBusMessage *m = dbus_message_new_method_call(s->owner.c_str(), s->path.c_str(),
                                                "org.freedesktop.DBus.Properties", "GetAll");
  const char *iface = ITEM_IFACE;
  dbus_message_append_args(m, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
  DBusPendingCall *pc = nullptr;
  if (dbus_connection_send_with_reply(bus, m, &pc, 3000) && pc)
    dbus_pending_call_set_notify(pc, props_reply, new Pending{s->owner, s->path},
                                 [](void *d) { delete (Pending *)d; });
  dbus_message_unref(m);
  flush();
}

static std::string name_owner(const std::string &name) {
  if (name.empty() || name[0] == ':') return name;
  DBusMessage *m = dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                "org.freedesktop.DBus", "GetNameOwner");
  const char *n = name.c_str();
  dbus_message_append_args(m, DBUS_TYPE_STRING, &n, DBUS_TYPE_INVALID);
  DBusError e;
  dbus_error_init(&e);
  DBusMessage *r = dbus_connection_send_with_reply_and_block(bus, m, 1000, &e);
  dbus_message_unref(m);
  std::string owner;
  if (r) {
    const char *o = nullptr;
    if (dbus_message_get_args(r, nullptr, DBUS_TYPE_STRING, &o, DBUS_TYPE_INVALID) && o) owner = o;
    dbus_message_unref(r);
  }
  dbus_error_free(&e);
  schedule_dispatch();
  return owner;
}

// "service" or "service/path" or just "/path" (then service = sender)
static void add_item(std::string service, const std::string &sender) {
  std::string path = "/StatusNotifierItem";
  if (!service.empty() && service[0] == '/') { path = service; service = sender; }
  else if (auto sl = service.find('/'); sl != std::string::npos) { path = service.substr(sl); service.resize(sl); }
  if (service.empty()) return;
  std::string owner = name_owner(service);
  if (owner.empty()) return;
  for (auto &s : g_slots)
    if (!s->xembed && s->owner == owner && s->path == path) return;   // already there
  auto s = std::make_unique<Slot>();
  s->service = service;
  s->owner = owner;
  s->path = path;
  Slot *raw = s.get();
  g_slots.push_back(std::move(s));
  emit("StatusNotifierItemRegistered", key_of(*raw));
  fetch_props(raw);
  changed();
}

static void remove_owner(const std::string &name) {
  bool any = false;
  for (auto it = g_slots.begin(); it != g_slots.end();) {
    Slot &s = **it;
    if (!s.xembed && (s.owner == name || s.service == name)) {
      emit("StatusNotifierItemUnregistered", key_of(s));
      it = g_slots.erase(it);
      any = true;
    } else {
      ++it;
    }
  }
  if (any) changed();
}

// --- Watcher: properties and introspection ------------------------------------
static void append_variant_items(DBusMessageIter *it) {
  DBusMessageIter v, a;
  dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "as", &v);
  dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "s", &a);
  for (auto &s : g_slots)
    if (!s->xembed) {
      std::string k = key_of(*s);
      const char *c = k.c_str();
      dbus_message_iter_append_basic(&a, DBUS_TYPE_STRING, &c);
    }
  dbus_message_iter_close_container(&v, &a);
  dbus_message_iter_close_container(it, &v);
}
static void append_prop(DBusMessageIter *it, const std::string &name) {
  DBusMessageIter v;
  if (name == "RegisteredStatusNotifierItems") { append_variant_items(it); return; }
  if (name == "IsStatusNotifierHostRegistered") {
    dbus_bool_t b = 1;
    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "b", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &b);
  } else {
    dbus_int32_t i = 0;
    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "i", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_INT32, &i);
  }
  dbus_message_iter_close_container(it, &v);
}

static const char *INTROSPECT =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    " <interface name=\"org.kde.StatusNotifierWatcher\">\n"
    "  <method name=\"RegisterStatusNotifierItem\"><arg name=\"service\" type=\"s\" direction=\"in\"/></method>\n"
    "  <method name=\"RegisterStatusNotifierHost\"><arg name=\"service\" type=\"s\" direction=\"in\"/></method>\n"
    "  <property name=\"RegisteredStatusNotifierItems\" type=\"as\" access=\"read\"/>\n"
    "  <property name=\"IsStatusNotifierHostRegistered\" type=\"b\" access=\"read\"/>\n"
    "  <property name=\"ProtocolVersion\" type=\"i\" access=\"read\"/>\n"
    "  <signal name=\"StatusNotifierItemRegistered\"><arg type=\"s\"/></signal>\n"
    "  <signal name=\"StatusNotifierItemUnregistered\"><arg type=\"s\"/></signal>\n"
    "  <signal name=\"StatusNotifierHostRegistered\"/>\n"
    "  <signal name=\"StatusNotifierHostUnregistered\"/>\n"
    " </interface>\n"
    " <interface name=\"org.freedesktop.DBus.Properties\">\n"
    "  <method name=\"Get\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"v\" direction=\"out\"/></method>\n"
    "  <method name=\"GetAll\"><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"out\"/></method>\n"
    " </interface>\n"
    " <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "  <method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method>\n"
    " </interface>\n"
    "</node>\n";

static DBusHandlerResult watcher_call(DBusMessage *m) {
  const char *iface = dbus_message_get_interface(m), *member = dbus_message_get_member(m);
  const char *path = dbus_message_get_path(m);
  if (!member || !path || strcmp(path, WATCHER_PATH)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  DBusMessage *r = nullptr;
  if (!strcmp(member, "RegisterStatusNotifierItem")) {
    const char *svc = nullptr;
    if (dbus_message_get_args(m, nullptr, DBUS_TYPE_STRING, &svc, DBUS_TYPE_INVALID) && svc) {
      r = dbus_message_new_method_return(m);
      dbus_connection_send(bus, r, nullptr);
      dbus_message_unref(r);
      add_item(svc, dbus_message_get_sender(m) ? dbus_message_get_sender(m) : "");
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  } else if (!strcmp(member, "RegisterStatusNotifierHost")) {
    r = dbus_message_new_method_return(m);
    emit("StatusNotifierHostRegistered", "");
  } else if (iface && !strcmp(iface, "org.freedesktop.DBus.Introspectable") && !strcmp(member, "Introspect")) {
    r = dbus_message_new_method_return(m);
    dbus_message_append_args(r, DBUS_TYPE_STRING, &INTROSPECT, DBUS_TYPE_INVALID);
  } else if (iface && !strcmp(iface, "org.freedesktop.DBus.Properties")) {
    const char *pi = nullptr, *pn = nullptr;
    if (!strcmp(member, "Get") &&
        dbus_message_get_args(m, nullptr, DBUS_TYPE_STRING, &pi, DBUS_TYPE_STRING, &pn, DBUS_TYPE_INVALID)) {
      r = dbus_message_new_method_return(m);
      DBusMessageIter it;
      dbus_message_iter_init_append(r, &it);
      append_prop(&it, pn);
    } else if (!strcmp(member, "GetAll")) {
      r = dbus_message_new_method_return(m);
      DBusMessageIter it, a, e;
      dbus_message_iter_init_append(r, &it);
      dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &a);
      for (const char *n : {"RegisteredStatusNotifierItems", "IsStatusNotifierHostRegistered", "ProtocolVersion"}) {
        dbus_message_iter_open_container(&a, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
        dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &n);
        append_prop(&e, n);
        dbus_message_iter_close_container(&a, &e);
      }
      dbus_message_iter_close_container(&it, &a);
    }
  }
  if (!r) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  dbus_connection_send(bus, r, nullptr);
  dbus_message_unref(r);
  flush();
  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult filter(DBusConnection *, DBusMessage *m, void *) {
  int type = dbus_message_get_type(m);
  if (type == DBUS_MESSAGE_TYPE_METHOD_CALL) return g_own_watcher ? watcher_call(m) : DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  if (type != DBUS_MESSAGE_TYPE_SIGNAL) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  const char *iface = dbus_message_get_interface(m), *member = dbus_message_get_member(m);
  if (!iface || !member) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  if (!strcmp(iface, "org.freedesktop.DBus") && !strcmp(member, "NameOwnerChanged")) {
    const char *name = nullptr, *old_o = nullptr, *new_o = nullptr;
    if (dbus_message_get_args(m, nullptr, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old_o, DBUS_TYPE_STRING,
                              &new_o, DBUS_TYPE_INVALID) && new_o && !*new_o)
      remove_owner(name);
  } else if (!strcmp(iface, ITEM_IFACE)) {
    // NewIcon, NewTitle, NewStatus, NewToolTip...: fetch the properties again
    const char *sender = dbus_message_get_sender(m), *path = dbus_message_get_path(m);
    if (sender && path)
      if (Slot *s = find_item(sender, path)) fetch_props(s);
  } else if (!g_own_watcher && !strcmp(iface, WATCHER)) {
    const char *arg = nullptr;
    dbus_message_get_args(m, nullptr, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);
    if (arg && !strcmp(member, "StatusNotifierItemRegistered")) add_item(arg, "");
    else if (arg && !strcmp(member, "StatusNotifierItemUnregistered")) {
      std::string a = arg, svc = a.substr(0, a.find('/'));
      std::string path = a.find('/') != std::string::npos ? a.substr(a.find('/')) : "/StatusNotifierItem";
      for (auto it = g_slots.begin(); it != g_slots.end(); ++it)
        if (!(*it)->xembed && ((*it)->service == svc || (*it)->owner == svc) && (*it)->path == path) {
          g_slots.erase(it);
          changed();
          break;
        }
    }
  }
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;   // others may want this signal too
}

static void dispatch_now() {
  if (!bus) return;
  dbus_connection_read_write(bus, 0);
  while (dbus_connection_dispatch(bus) == DBUS_DISPATCH_DATA_REMAINS) {}
}
static void dispatch_cb(void *) { dispatch_now(); }
static void schedule_dispatch() {
  if (!Fl::has_timeout(dispatch_cb)) Fl::add_timeout(0.0, dispatch_cb);
}
static void on_fd(int, void *) { dispatch_now(); }

static void init_sni() {
  DBusError e;
  dbus_error_init(&e);
  bus = dbus_bus_get_private(DBUS_BUS_SESSION, &e);
  if (!bus) { dbus_error_free(&e); return; }
  dbus_connection_set_exit_on_disconnect(bus, false);
  dbus_connection_add_filter(bus, filter, nullptr, nullptr);
  dbus_bus_add_match(bus, "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
                          "member='NameOwnerChanged'", nullptr);
  dbus_bus_add_match(bus, "type='signal',interface='org.kde.StatusNotifierItem'", nullptr);
  int r = dbus_bus_request_name(bus, WATCHER, DBUS_NAME_FLAG_DO_NOT_QUEUE, &e);
  dbus_error_free(&e);
  g_own_watcher = r == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;
  std::string host = "org.kde.StatusNotifierHost-" + std::to_string(getpid());
  dbus_bus_request_name(bus, host.c_str(), DBUS_NAME_FLAG_DO_NOT_QUEUE, &e);
  dbus_error_free(&e);
  if (g_own_watcher) {
    emit("StatusNotifierHostRegistered", "");
    // After Rambler restarts (Reset Nirvana) the icons are already on the bus - not every
    // program registers again, so we collect them ourselves by service name.
    DBusMessage *m = dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                  "org.freedesktop.DBus", "ListNames");
    DBusMessage *rep = dbus_connection_send_with_reply_and_block(bus, m, 2000, &e);
    dbus_message_unref(m);
    dbus_error_free(&e);
    if (rep) {
      DBusMessageIter it, a;
      if (dbus_message_iter_init(rep, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&it, &a);
        while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_STRING) {
          std::string n = iter_string(&a);
          if (n.rfind("org.kde.StatusNotifierItem-", 0) == 0) add_item(n, "");
          dbus_message_iter_next(&a);
        }
      }
      dbus_message_unref(rep);
    }
  } else {
    // a foreign watcher (e.g. KDE): register as a host and take its list
    dbus_bus_add_match(bus, "type='signal',interface='org.kde.StatusNotifierWatcher'", nullptr);
    DBusMessage *m = dbus_message_new_method_call(WATCHER, WATCHER_PATH, WATCHER, "RegisterStatusNotifierHost");
    const char *h = host.c_str();
    dbus_message_append_args(m, DBUS_TYPE_STRING, &h, DBUS_TYPE_INVALID);
    dbus_connection_send(bus, m, nullptr);
    dbus_message_unref(m);
    m = dbus_message_new_method_call(WATCHER, WATCHER_PATH, "org.freedesktop.DBus.Properties", "Get");
    const char *i = WATCHER, *p = "RegisteredStatusNotifierItems";
    dbus_message_append_args(m, DBUS_TYPE_STRING, &i, DBUS_TYPE_STRING, &p, DBUS_TYPE_INVALID);
    DBusMessage *rep = dbus_connection_send_with_reply_and_block(bus, m, 2000, &e);
    dbus_message_unref(m);
    dbus_error_free(&e);
    if (rep) {
      DBusMessageIter it, v, a;
      if (dbus_message_iter_init(rep, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&it, &v);
        if (dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_ARRAY) {
          dbus_message_iter_recurse(&v, &a);
          while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_STRING) {
            add_item(iter_string(&a), "");
            dbus_message_iter_next(&a);
          }
        }
      }
      dbus_message_unref(rep);
    }
  }
  int fd = -1;
  if (dbus_connection_get_unix_fd(bus, &fd)) Fl::add_fd(fd, FL_READ, on_fd);
  flush();
}

// ---------------------------------------------------------------------------
// dbusmenu: the icon's context menu as a Platinum menu
// ---------------------------------------------------------------------------
struct MenuNode {
  int id = 0;
  std::string label, type, toggle_type;
  bool enabled = true, visible = true, submenu = false;
  int toggle_state = -1;
  std::vector<MenuNode> kids;
};

static void parse_props(DBusMessageIter *arr, MenuNode &n) {
  DBusMessageIter d;
  dbus_message_iter_recurse(arr, &d);
  while (dbus_message_iter_get_arg_type(&d) == DBUS_TYPE_DICT_ENTRY) {
    DBusMessageIter e, v;
    dbus_message_iter_recurse(&d, &e);
    std::string k = iter_string(&e);
    dbus_message_iter_next(&e);
    dbus_message_iter_recurse(&e, &v);
    if (k == "label") n.label = iter_string(&v);
    else if (k == "type") n.type = iter_string(&v);
    else if (k == "enabled") n.enabled = iter_bool(&v);
    else if (k == "visible") n.visible = iter_bool(&v);
    else if (k == "toggle-type") n.toggle_type = iter_string(&v);
    else if (k == "toggle-state") n.toggle_state = iter_int(&v);
    else if (k == "children-display") n.submenu = iter_string(&v) == "submenu";
    dbus_message_iter_next(&d);
  }
}
static void parse_node(DBusMessageIter *st, MenuNode &n) {   // (ia{sv}av)
  DBusMessageIter s;
  dbus_message_iter_recurse(st, &s);
  n.id = iter_int(&s);
  dbus_message_iter_next(&s);
  if (dbus_message_iter_get_arg_type(&s) == DBUS_TYPE_ARRAY) parse_props(&s, n);
  dbus_message_iter_next(&s);
  if (dbus_message_iter_get_arg_type(&s) == DBUS_TYPE_ARRAY) {
    DBusMessageIter kids;
    dbus_message_iter_recurse(&s, &kids);
    while (dbus_message_iter_get_arg_type(&kids) == DBUS_TYPE_VARIANT) {
      DBusMessageIter v;
      dbus_message_iter_recurse(&kids, &v);
      if (dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_STRUCT) {
        MenuNode k;
        parse_node(&v, k);
        n.kids.push_back(std::move(k));
      }
      dbus_message_iter_next(&kids);
    }
  }
  if (!n.kids.empty()) n.submenu = true;
}

static bool get_layout(const Slot &s, MenuNode &root) {
  DBusError e;
  dbus_error_init(&e);
  // AboutToShow gives the program a chance to fill in the menu (e.g. a window list)
  DBusMessage *m = dbus_message_new_method_call(s.owner.c_str(), s.menu.c_str(), "com.canonical.dbusmenu",
                                                "AboutToShow");
  dbus_int32_t zero = 0;
  dbus_message_append_args(m, DBUS_TYPE_INT32, &zero, DBUS_TYPE_INVALID);
  if (DBusMessage *r = dbus_connection_send_with_reply_and_block(bus, m, 500, &e)) dbus_message_unref(r);
  dbus_message_unref(m);
  dbus_error_free(&e);

  m = dbus_message_new_method_call(s.owner.c_str(), s.menu.c_str(), "com.canonical.dbusmenu", "GetLayout");
  dbus_int32_t depth = -1;
  DBusMessageIter it, arr;
  dbus_message_iter_init_append(m, &it);
  dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &zero);
  dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &depth);
  dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);
  dbus_message_iter_close_container(&it, &arr);
  DBusMessage *r = dbus_connection_send_with_reply_and_block(bus, m, 2000, &e);
  dbus_message_unref(m);
  dbus_error_free(&e);
  schedule_dispatch();
  if (!r) return false;
  bool ok = false;
  if (dbus_message_iter_init(r, &it)) {
    dbus_message_iter_next(&it);   // layout revision number
    if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRUCT) { parse_node(&it, root); ok = true; }
  }
  dbus_message_unref(r);
  return ok;
}

// A dbusmenu label ("_Quit") -> FLTK ("&Quit"); '&' has to be doubled.
static std::string fltk_label(const std::string &l) {
  std::string o;
  for (size_t i = 0; i < l.size(); i++) {
    if (l[i] == '&') o += "&&";
    else if (l[i] == '_') {
      if (i + 1 < l.size() && l[i + 1] == '_') { o += '_'; i++; }
      else o += '&';
    } else o += l[i];
  }
  return o;
}

static std::deque<std::string> g_labels;   // menu item texts - they must live as long as the menu is open
static void build_items(const MenuNode &n, std::vector<Fl_Menu_Item> &out) {
  for (size_t i = 0; i < n.kids.size(); i++) {
    const MenuNode &k = n.kids[i];
    if (!k.visible) continue;
    if (k.type == "separator") {
      if (!out.empty() && out.back().text) out.back().flags |= FL_MENU_DIVIDER;
      continue;
    }
    g_labels.push_back(fltk_label(k.label));
    Fl_Menu_Item it{};
    it.text = g_labels.back().c_str();
    it.user_data_ = (void *)(long)k.id;
    if (!k.enabled) it.flags |= FL_MENU_INACTIVE;
    if (k.toggle_type == "checkmark") it.flags |= FL_MENU_TOGGLE;
    else if (k.toggle_type == "radio") it.flags |= FL_MENU_RADIO;
    if (k.toggle_state == 1) it.flags |= FL_MENU_VALUE;
    if (k.submenu && !k.kids.empty()) {
      it.flags |= FL_SUBMENU;
      out.push_back(it);
      build_items(k, out);
      out.push_back(Fl_Menu_Item{});   // end of a submenu
    } else {
      out.push_back(it);
    }
  }
}

static void menu_event(const Slot &s, int id) {
  DBusMessage *m = dbus_message_new_method_call(s.owner.c_str(), s.menu.c_str(), "com.canonical.dbusmenu", "Event");
  DBusMessageIter it, v;
  dbus_message_iter_init_append(m, &it);
  dbus_int32_t i = id, zero = 0;
  const char *ev = "clicked";
  dbus_uint32_t ts = 0;
  dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &i);
  dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &ev);
  dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "i", &v);
  dbus_message_iter_append_basic(&v, DBUS_TYPE_INT32, &zero);
  dbus_message_iter_close_container(&it, &v);
  dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &ts);
  dbus_connection_send(bus, m, nullptr);
  dbus_message_unref(m);
  flush();
}

static bool show_menu(Slot *s, int sx) {
  if (s->menu.empty() || s->menu == "/") return false;
  MenuNode root;
  if (!get_layout(*s, root) || root.kids.empty()) return false;
  g_labels.clear();
  std::vector<Fl_Menu_Item> items;
  build_items(root, items);
  if (items.empty()) return false;
  items.push_back(Fl_Menu_Item{});
  g_menu->menu(items.data());
  std::string owner = s->owner, path = s->path;
  // the menu drops down below the icon, like the other menus of the bar
  const Fl_Menu_Item *picked = g_menu->Fl_Menu_::menu()->pulldown(sx, 0, SLOT, MENUBAR_H - 1, nullptr, g_menu);
  g_menu->menu(nullptr);
  if (picked && !picked->submenu())
    if (Slot *still = find_item(owner, path)) menu_event(*still, (int)(long)picked->user_data());
  return true;
}

// Activate / SecondaryActivate / ContextMenu (x, y in screen pixels)
static bool call_xy(const Slot &s, const char *method, int x, int y) {
  DBusMessage *m = dbus_message_new_method_call(s.owner.c_str(), s.path.c_str(), ITEM_IFACE, method);
  dbus_int32_t xx = x, yy = y;
  dbus_message_append_args(m, DBUS_TYPE_INT32, &xx, DBUS_TYPE_INT32, &yy, DBUS_TYPE_INVALID);
  DBusError e;
  dbus_error_init(&e);
  DBusMessage *r = dbus_connection_send_with_reply_and_block(bus, m, 1000, &e);
  dbus_message_unref(m);
  bool ok = r != nullptr;
  if (r) dbus_message_unref(r);
  dbus_error_free(&e);
  schedule_dispatch();
  return ok;
}

static void scroll(const Slot &s, int delta) {
  DBusMessage *m = dbus_message_new_method_call(s.owner.c_str(), s.path.c_str(), ITEM_IFACE, "Scroll");
  dbus_int32_t d = delta;
  const char *o = "vertical";
  dbus_message_append_args(m, DBUS_TYPE_INT32, &d, DBUS_TYPE_STRING, &o, DBUS_TYPE_INVALID);
  dbus_connection_send(bus, m, nullptr);
  dbus_message_unref(m);
  flush();
}

// ---------------------------------------------------------------------------
// Widget in the menu bar
// ---------------------------------------------------------------------------
class Area : public Fl_Widget {
public:
  Area(int X, int Y, int H) : Fl_Widget(X, Y, 0, H) {}
  int slot_x(int i) const { return x() + PAD + i * SLOT; }

  Slot *slot_at(int ex, int &sx) {
    int i = 0;
    for (auto &s : g_slots) {
      if (!s->shown()) continue;
      if (ex >= slot_x(i) && ex < slot_x(i) + SLOT) { sx = slot_x(i); return s.get(); }
      i++;
    }
    return nullptr;
  }

  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_draw_box(MENUBAR_BOX, x(), y(), w(), h(), GRAY);
    int i = 0;
    for (auto &s : g_slots) {
      if (!s->shown()) continue;
      int sx = slot_x(i++);
      if (s->xembed) continue;   // the program's window lies on top
      bool down = s.get() == pressed_;
      if (down) { fl_color(SEL); fl_rectf(sx, y(), SLOT, h() - 1); }
      if (s->img) s->img->draw(sx + (SLOT - ICON) / 2, y() + (h() - 1 - ICON) / 2);
      else {   // no icon: the first letter of the name in a frame
        std::string t = !s->title.empty() ? s->title : s->id;
        char c[8] = "?";
        if (!t.empty()) snprintf(c, sizeof c, "%c", toupper((unsigned char)t[0]));
        fl_color(down ? SEL_TEXT : TEXT);
        fl_rect(sx + 3, y() + 2, SLOT - 6, h() - 5);
        fl_font(FONT_SYSTEM, SIZE_SMALL);
        fl_draw(c, sx + 3, y() + 2, SLOT - 6, h() - 5, FL_ALIGN_CENTER);
      }
    }
    fl_pop_clip();
  }

  int handle(int e) override {
    switch (e) {
    case FL_ENTER:
    case FL_MOVE: {
      int sx;
      Slot *s = slot_at(Fl::event_x(), sx);
      if (s != tip_) {
        tip_ = s;
        static std::string t;
        if (s && !s->xembed) {
          t = !s->tip.empty() ? s->tip : !s->title.empty() ? s->title : s->id;
          Fl_Tooltip::enter_area(this, sx, y(), SLOT, h(), t.c_str());
        } else {
          Fl_Tooltip::enter_area(this, 0, 0, 0, 0, nullptr);
        }
      }
      return 1;
    }
    case FL_LEAVE:
      tip_ = nullptr;
      Fl_Tooltip::enter_area(this, 0, 0, 0, 0, nullptr);
      return 1;
    case FL_PUSH: {
      int sx;
      Slot *s = slot_at(Fl::event_x(), sx);
      if (!s || s->xembed) return 1;
      Fl_Tooltip::enter_area(this, 0, 0, 0, 0, nullptr);
      tip_ = nullptr;
      pressed_ = s;
      redraw();
      Fl::flush();
      int b = Fl::event_button();
      int rx = g_desk->x_root() + sx, ry = g_desk->y_root() + MENUBAR_H;
      std::string owner = s->owner, path = s->path;
      if (b == FL_LEFT_MOUSE) {
        if (s->item_is_menu || !call_xy(*s, "Activate", rx, ry)) {
          if (Slot *st = find_item(owner, path)) show_menu(st, sx);
        }
      } else if (b == FL_MIDDLE_MOUSE) {
        call_xy(*s, "SecondaryActivate", rx, ry);
      } else {
        if (!show_menu(s, sx))
          if (Slot *st = find_item(owner, path)) call_xy(*st, "ContextMenu", rx, ry);
      }
      pressed_ = nullptr;
      redraw();
      return 1;
    }
    case FL_MOUSEWHEEL: {
      int sx;
      Slot *s = slot_at(Fl::event_x(), sx);
      if (s && !s->xembed) scroll(*s, -Fl::event_dy() * 120);
      return 1;
    }
    }
    return Fl_Widget::handle(e);
  }

  void resize(int X, int Y, int W, int H) override {
    Fl_Widget::resize(X, Y, W, H);
    place_boxes();
  }

private:
  Slot *pressed_ = nullptr, *tip_ = nullptr;
};

// The XEmbed windows stand exactly above their places in the widget.
static void place_boxes() {
  if (!g_area || !dpy) return;
  auto *a = (Area *)g_area;
  int i = 0;
  for (auto &s : g_slots) {
    if (!s->shown()) {
      if (s->xembed) XMoveWindow(dpy, s->box, -px(SLOT), px(1));
      continue;
    }
    if (s->xembed) XMoveWindow(dpy, s->box, px(a->slot_x(i)), px(1));
    i++;
  }
  XFlush(dpy);
}

Fl_Widget *make(int X, int Y, int H) {
  g_area = new Area(X, Y, H);
  g_menu = new Fl_Menu_Button(0, 0, 0, 0);
  g_menu->hide();
  g_menu->textfont(FONT_SYSTEM);
  g_menu->textsize(SIZE_SYSTEM);
  g_menu->selection_color(SEL);
  g_menu->box(FL_NO_BOX);
  return g_area;
}

void init(Fl_Window *desk, std::function<void()> on_changed) {
  g_desk = desk;
  g_changed = std::move(on_changed);
  if (getenv("NIRVANA_NO_TRAY")) return;
  init_xembed();
  if (g_manager) Fl::add_system_handler(x_events, nullptr);
  init_sni();
}

}  // namespace tray
