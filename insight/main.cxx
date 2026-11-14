// Nirvana Desktop - nirvana-insight: an X11 window manager in the Platinum style
//
// Insight is a classic "reparenting" window manager:
// * every application window gets a frame (an FLTK window drawn by libplatinum),
// * the application is moved inside the frame (XReparentWindow),
// * the title bar handles moving, closing, zoom and iconifying,
// * no compositor - applications draw straight to the screen, which saves memory.
//
// The Rambler desktop window (WM_CLASS "Nirvana-Desktop" or _NET_WM_WINDOW_TYPE_DESKTOP)
// gets no frame and always lies at the very bottom.
//
// Works on Xorg and XLibre (plain Xlib, no extensions).

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xfixes.h>
#include <X11/keysym.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <map>
#include <vector>
#include <string>

#include "anim.h"
#include "platinum.h"

using namespace plat;

static Display *dpy;
static Window root;
static char **g_argv;
static int screen_w, screen_h;

static Atom A_APPEARANCE;
static Atom A_WM_PROTOCOLS, A_WM_DELETE, A_WM_STATE, A_NET_WM_NAME, A_UTF8,
    A_NET_WM_TYPE, A_NET_WM_TYPE_DESKTOP, A_NET_WM_TYPE_DOCK, A_NET_SUPPORTED,
    A_NET_CHECK, A_NET_ACTIVE, A_NET_WM_STATE, A_NET_WM_STATE_SHADED, A_NET_WM_TYPE_DIALOG,
    A_NET_CLIENT_LIST, A_NET_CLOSE, A_NET_WM_STATE_HIDDEN, A_WM_CHANGE_STATE,
    A_NET_WM_STATE_MAX_V, A_NET_WM_STATE_MAX_H, A_NET_WORKAREA, A_NET_FRAME_EXTENTS,
    A_NET_REQUEST_FRAME_EXTENTS, A_NIRVANA_DOCK, A_DESKTOP;

class Frame;

// Work area (FLTK units): the screen without the menu bar at the top and Rambler's dock
// at the bottom (_NIRVANA_DOCK = dock height, 0 when empty). Program windows do not
// cover it: when opened, maximized or zoomed.
static int g_dock_h = 0;
static Rect work_area() { return {0, MENUBAR_H, screen_w, screen_h - MENUBAR_H - g_dock_h}; }

static void read_dock_height() {
  Atom type;
  int fmt;
  unsigned long n, after;
  unsigned char *data = nullptr;
  g_dock_h = 0;
  if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), A_NIRVANA_DOCK, 0, 1, False, XA_CARDINAL, &type, &fmt, &n,
                         &after, &data) == Success && data && n == 1 && fmt == 32)
    g_dock_h = std::clamp((int)*(long *)data, 0, screen_h / 3);
  if (data) XFree(data);
}

// _NET_WORKAREA (in pixels) - programs (Chromium, GTK, Qt) take the size of a
// "maximized" window and of the first opening from it.
// The top of the area is 0, not the bottom of the menu bar: FLTK (1.4.x, Menu_Window::autoscroll)
// moves a menu bar lying above the work area down when moving between menu
// titles - Rambler's menus fell apart when the mouse moved. Insight protects the
// menu bar itself anyway (windows do not go under it, zoom neither).
static void publish_workarea() {
  Rect area = work_area();
  area.h += area.y;
  area.y = 0;
  Rect a = px(area);
  long wa[4] = {a.x, a.y, a.w, a.h};
  XChangeProperty(dpy, DefaultRootWindow(dpy), A_NET_WORKAREA, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)wa, 4);
  long geo[2] = {px(screen_w), px(screen_h)};
  XChangeProperty(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "_NET_DESKTOP_GEOMETRY", False), XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)geo, 2);
}

// _NET_FRAME_EXTENTS: frame thickness (left, right, top, bottom) in pixels
static void set_frame_extents(Window w, bool bare) {
  long ext[4] = {px(FRAME), px(FRAME), px(TITLE_H), px(FRAME)};
  if (bare) ext[0] = ext[1] = ext[2] = ext[3] = 0;
  XChangeProperty(dpy, w, A_NET_FRAME_EXTENTS, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)ext, 4);
}

// _NIRVANA_WINDOW_AREA (x, y, width, height) on the root window - set by Rambler.
static bool rambler_area(plat::Rect &r) {
  Atom a = XInternAtom(dpy, "_NIRVANA_WINDOW_AREA", False), type;
  int fmt;
  unsigned long n, after;
  unsigned char *data = nullptr;
  bool ok = false;
  if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), a, 0, 4, False, XA_CARDINAL, &type, &fmt, &n, &after,
                         &data) == Success && data && n == 4 && fmt == 32) {
    long *v = (long *)data;
    if (v[2] > 100 && v[3] > 60) { r = {(int)v[0], (int)v[1], (int)v[2], (int)v[3]}; ok = true; }
  }
  if (data) XFree(data);
  return ok;
}

struct Client {
  Window win = 0;
  Frame *frame = nullptr;
  std::string title;
  bool shaded = false, zoomed = false;
  int full_h = 0;                 // frame height before shading
  int unzoom[4] = {0, 0, 0, 0};
  int ignore_unmap = 0;           // UnmapNotify caused by us (reparent)
  bool dialog = false;            // dialog window: no buttons and no resizing
  bool fixed = false;             // a fixed size (WM_NORMAL_HINTS min = max): no zoom, no resizing
  bool own_anim = false;          // Nirvana Desktop programs animate opening/closing themselves
  bool hidden = false;            // hidden (Hide / Hide Others in the application menu)
  bool iconic = false;            // iconified: hidden, and its icon is on the desktop
  unsigned long mru = 0;          // when it was last active (for Alt+Tab)
  std::string cls;                // WM_CLASS - the windows of one application share the class
  // Undecorated (_MOTIF_WM_HINTS without decorations: skinned players such as qmmp or
  // Audacious, client-side decorations): the frame has no border and no title bar -
  // it only holds the window, which draws its own.
  bool bare = false;
  // virtual screens: the screen the window lives on (EWMH desktop), on all of them (sticky -
  // the Jobs window, windows that ask for it); off - its frame taken down because another
  // screen is shown
  int screen = 0;
  bool sticky = false, off = false;
  int fw() const { return bare ? 0 : FRAME; }     // frame width (left, right, bottom)
  int th() const { return bare ? 0 : TITLE_H; }   // title bar height
};

static std::map<Window, Client *> clients;   // application window -> client
static Window desktop_win = 0;                // desktop window (no frame)
static Client *g_active = nullptr;

// Virtual screens (Preferences > Desktop > Screens, 1-9): each window lives on one of them;
// only the windows of the shown screen have their frames on the display. EWMH:
// _NET_NUMBER_OF_DESKTOPS, _NET_CURRENT_DESKTOP, _NET_WM_DESKTOP; Rambler's screen gadget,
// Win+M / Win+1..9 and pulling the menu bar down switch them.
static int g_screens = 4, g_screen = 0;
static bool here(const Client *c) { return c->sticky || c->screen == g_screen; }

// Preferences > Desktop: "Classic 16-bit" windows and outline dragging (desktop.conf)
static bool g_classic = false, g_outline = false;
static void read_window_mode() {
  DesktopSettings d = read_desktop();
  g_classic = d.classic;
  g_outline = d.outline;
}

static void focus(Client *c, bool raise = true);
static bool on_top(Client *c);
static void depth(Client *c);
static void unmanage(Client *c, bool destroyed);
static void iconify_client(Client *c);
static void preview_soon(void *);
static void forget_preview(Window w);

// ---------------------------------------------------------------------------
// Xlib helpers
// ---------------------------------------------------------------------------
static int ignore_errors(Display *, XErrorEvent *) { return 0; }

static bool wm_running = false;
static int detect_wm(Display *, XErrorEvent *e) {
  if (e->error_code == BadAccess) wm_running = true;
  return 0;
}

static std::string get_title(Window w) {
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  if (XGetWindowProperty(dpy, w, A_NET_WM_NAME, 0, 1024, False, A_UTF8, &type, &fmt, &n,
                         &after, &data) == Success && data) {
    std::string s((char *)data, n);
    XFree(data);
    if (!s.empty()) return s;
  }
  char *name = nullptr;
  if (XFetchName(dpy, w, &name) && name) {
    std::string s(name);
    XFree(name);
    return s;
  }
  return _("untitled");
}

static bool has_atom(Window w, Atom prop, Atom value) {
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  bool found = false;
  if (XGetWindowProperty(dpy, w, prop, 0, 64, False, XA_ATOM, &type, &fmt, &n, &after,
                         &data) == Success && data) {
    Atom *a = (Atom *)data;
    for (unsigned long i = 0; i < n; i++) if (a[i] == value) found = true;
    XFree(data);
  }
  return found;
}

static bool is_desktop(Window w) {
  XClassHint ch;
  bool d = false;
  if (XGetClassHint(dpy, w, &ch)) {
    if (ch.res_class && !strcmp(ch.res_class, "Nirvana-Desktop")) d = true;
    if (ch.res_name) XFree(ch.res_name);
    if (ch.res_class) XFree(ch.res_class);
  }
  return d || has_atom(w, A_NET_WM_TYPE, A_NET_WM_TYPE_DESKTOP);
}

// Where a new window "flies out" from: the icon it was opened with. The program that opened it
// (Rambler for the Preferences icon, Preferences for a panel icon) gives the rectangle in the
// environment of the new program: NIRVANA_ZOOM_FROM="x y w h" - Insight reads it through
// _NET_WM_PID. Only the first window of the program (its later dialogs open from their middle).
static bool zoom_source(Window w, Rect &out) {
  static std::vector<long> used;
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  long pid = 0;
  if (XGetWindowProperty(dpy, w, XInternAtom(dpy, "_NET_WM_PID", False), 0, 1, False, XA_CARDINAL, &type, &fmt, &n,
                         &after, &data) == Success && data) {
    if (n) pid = *(long *)data;
    XFree(data);
  }
  if (pid <= 0 || std::find(used.begin(), used.end(), pid) != used.end()) return false;
  FILE *f = fopen(("/proc/" + std::to_string(pid) + "/environ").c_str(), "r");
  if (!f) return false;
  std::string env, var;
  int ch;
  bool found = false;
  while ((ch = fgetc(f)) != EOF) {
    if (ch) { var += (char)ch; continue; }
    if (var.rfind("NIRVANA_ZOOM_FROM=", 0) == 0) { env = var.substr(18); found = true; break; }
    var.clear();
  }
  fclose(f);
  if (!found) return false;
  used.push_back(pid);
  if (used.size() > 64) used.erase(used.begin());
  Rect r{};
  if (sscanf(env.c_str(), "%d %d %d %d", &r.x, &r.y, &r.w, &r.h) != 4 || r.w <= 0 || r.h <= 0) return false;
  out = r;
  return true;
}

static std::string get_class(Window w) {
  XClassHint ch;
  std::string r;
  if (XGetClassHint(dpy, w, &ch)) {
    if (ch.res_class) { r = ch.res_class; XFree(ch.res_class); }
    if (ch.res_name) XFree(ch.res_name);
  }
  return r;
}

// _NET_WM_STATE: tell the application (e.g. Rambler) whether the window is shaded.
static void publish_state(Client *c) {
  Atom st[4];
  int n = 0;
  if (c->zoomed) { st[n++] = A_NET_WM_STATE_MAX_V; st[n++] = A_NET_WM_STATE_MAX_H; }
  if (c->shaded) st[n++] = A_NET_WM_STATE_SHADED;
  if (c->hidden) st[n++] = A_NET_WM_STATE_HIDDEN;
  XChangeProperty(dpy, c->win, A_NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char *)st, n);
}

static bool supports_delete(Window w) {
  Atom *protos; int n;
  bool ok = false;
  if (XGetWMProtocols(dpy, w, &protos, &n)) {
    for (int i = 0; i < n; i++) if (protos[i] == A_WM_DELETE) ok = true;
    XFree(protos);
  }
  return ok;
}

static void set_wm_state(Window w, long state) {
  long data[2] = {state, None};
  XChangeProperty(dpy, w, A_WM_STATE, A_WM_STATE, 32, PropModeReplace, (unsigned char *)data, 2);
}

// _MOTIF_WM_HINTS: flags, functions, decorations, input mode, status. A window that
// asks for no decorations (flag 2 set, decorations 0) gets a frame without chrome.
static bool undecorated(Window w) {
  static Atom A_MOTIF = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
  Atom type; int fmt; unsigned long n = 0, after; unsigned char *data = nullptr;
  bool bare = false;
  if (XGetWindowProperty(dpy, w, A_MOTIF, 0, 5, False, AnyPropertyType, &type, &fmt, &n, &after, &data) == Success &&
      data && fmt == 32 && n >= 3) {
    long *h = (long *)data;
    bare = (h[0] & 2) && h[2] == 0;
  }
  if (data) XFree(data);
  return bare;
}

// ---------------------------------------------------------------------------
// Window frame
// ---------------------------------------------------------------------------
class Frame : public Fl_Double_Window {
public:
  Client *c;
  Frame(Client *cl, int X, int Y, int W, int H)
      : Fl_Double_Window(X, Y, W, H, "insight-frame"), c(cl) {
    border(0);
    box(FL_NO_BOX);
    end();
  }

  // Client area inside the frame
  int cw() const { return w() - 2 * c->fw(); }
  int ch() const { return (c->shaded ? c->full_h : h()) - c->th() - c->fw(); }

  // After every change of the frame geometry we fit the application window
  // and send it a synthetic ConfigureNotify (required by ICCCM).
  void sync_client() {
    // the frame is in FLTK units, the application window in pixels (interface scale)
    int F = c->fw(), T = c->th();
    int pw = px(w() - F) - px(F), ph = px(T + ch()) - px(T);
    XMoveResizeWindow(dpy, c->win, px(F), px(T), std::max(1, pw), std::max(1, ph));
    XConfigureEvent ce{};
    ce.type = ConfigureNotify;
    ce.event = ce.window = c->win;
    ce.x = px(x()) + px(F); ce.y = px(y()) + px(T);
    ce.width = pw; ce.height = ph;
    ce.border_width = 0;
    ce.above = None;
    XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
  }

  void resize(int X, int Y, int W, int H) override {
    bool sized = W != w() || H != h();
    Fl_Double_Window::resize(X, Y, W, H);
    (void)sized;
    sync_client();
  }

  void draw() override {
    if (c->bare) return;   // the window covers the whole frame and draws itself
    bool act = c == g_active;
    if (classic_look(g_classic)) {
      draw_classic_frame(0, 0, w(), h(), act, c->shaded, !c->dialog && !c->fixed);
      draw_classic_titlebar(0, 0, w(), c->title.c_str(), act, pressed_, gadgets());
      if (!c->shaded) {
        fl_color(DARK);
        fl_rect(FRAME - 1, TITLE_H - 1, cw() + 2, ch() + 2);
      }
      return;
    }
    draw_frame(0, 0, w(), h(), act, c->shaded);
    draw_titlebar(0, 0, w(), c->title.c_str(), act, pressed_, c->dialog ? 0 : c->fixed ? 2 : 1);
    if (!c->shaded && !c->dialog && !c->fixed) {
      fl_color(act ? SHADOW : MID);
      fl_rect(FRAME - 1, TITLE_H - 1, cw() + 2, ch() + 2);
      // resize handle on the frame in the bottom right corner
      fl_color(act ? SHADOW : MID);
      for (int i = 0; i < 3; i++)
        fl_line(w() - 2 - i * 3, h() - FRAME + 1, w() - FRAME + 1 - i * 3 + 3, h() - 2);
    }
  }

  // classic: dialogs only get the depth gadget, fixed-size windows no zoom
  int gadgets() const { return c->dialog ? 2 : c->fixed ? 3 : 1; }

  int hit_button(int ex, int ey) const {
    if (classic_look(g_classic)) return classic_gadget_hit(w(), ex, ey, gadgets());
    if (c->dialog) return 0;
    int by = (TITLE_H - BOX) / 2;
    if (ey < by || ey >= by + BOX) return 0;
    // as draw_titlebar(): without "zoom" the iconify button takes its place in the corner
    int zoom_x = w() - 8 - BOX, shade_x = c->fixed ? zoom_x : zoom_x - BOX - 5;
    if (ex >= 8 && ex < 8 + BOX) return 1;
    if (ex >= shade_x && ex < shade_x + BOX) return 3;
    if (!c->fixed && ex >= zoom_x && ex < zoom_x + BOX) return 2;
    if (ex >= shade_x && ex < shade_x + BOX) return 3;
    return 0;
  }

  Rect geom(int H = -1) const { return {x(), y(), w(), H < 0 ? h() : H}; }

  void toggle_shade() {
    if (c->bare) return;   // no title bar to shade to
    Rect full = geom(c->shaded ? c->full_h : h()), bar = geom(TITLE_H);
    if (c->shaded) zoom_rects(bar, full); else zoom_rects(full, bar);
    if (!c->shaded) {
      c->full_h = h();
      c->shaded = true;
      size(w(), TITLE_H);          // the application window stays, but the frame clips it completely
    } else {
      c->shaded = false;
      size(w(), c->full_h);
    }
    publish_state(c);
    redraw();
  }

  void toggle_zoom() {
    if (c->fixed) return;   // a window with a fixed size is not zoomed (nor maximized)
    if (c->shaded) toggle_shade();
    if (!c->zoomed) {
      c->unzoom[0] = x(); c->unzoom[1] = y(); c->unzoom[2] = w(); c->unzoom[3] = h();
      // zoom keeps the desktop icons visible (Nirvana HD, disks, Trash, window icons)
      // and the dock - Rambler gives the area; without Rambler: the work area
      Rect wa = work_area();
      Rect to = {wa.x + 4, wa.y + 4, wa.w - 8, wa.h - 8};
      rambler_area(to);
      zoom_rects(geom(), to);
      resize(to.x, to.y, to.w, to.h);
      c->zoomed = true;
    } else {
      zoom_rects(geom(), {c->unzoom[0], c->unzoom[1], c->unzoom[2], c->unzoom[3]});
      resize(c->unzoom[0], c->unzoom[1], c->unzoom[2], c->unzoom[3]);
      c->zoomed = false;
    }
    publish_state(c);
    redraw();
  }

  void close_client() {
    if (supports_delete(c->win)) {
      XEvent ev{};
      ev.xclient.type = ClientMessage;
      ev.xclient.window = c->win;
      ev.xclient.message_type = A_WM_PROTOCOLS;
      ev.xclient.format = 32;
      ev.xclient.data.l[0] = A_WM_DELETE;
      ev.xclient.data.l[1] = CurrentTime;
      XSendEvent(dpy, c->win, False, NoEventMask, &ev);
    } else {
      XKillClient(dpy, c->win);
    }
  }

  int handle(int e) override {
    int ex = Fl::event_x(), ey = Fl::event_y();
    switch (e) {
    case FL_PUSH:
      ::focus(c, !g_classic);   // classic windows: a click activates, only the depth gadget raises
      if (c->bare) return 1;
      if (ey < TITLE_H) {
        if (int b = hit_button(ex, ey)) { drag_ = BTN; armed_ = pressed_ = b; redraw(); return 1; }
        if (Fl::event_clicks() && !c->dialog && !classic_look(g_classic)) {
          Fl::event_clicks(0); iconify_client(c); drag_ = NONE; return 1;
        }
        drag_ = MOVE;
      } else if (classic_look(g_classic) && !c->shaded && !c->dialog && !c->fixed) {
        // classic: only the size gadget in the corner resizes, the rest of the frame moves
        bool corner = (ex >= w() - CLASSIC_SIZE && ey >= h() - FRAME) || (ex >= w() - FRAME && ey >= h() - CLASSIC_SIZE);
        drag_ = corner ? GROW : MOVE;
      } else if (!c->shaded && !c->dialog && !c->fixed && (ex >= w() - 16 || ey >= h() - FRAME)) {
        drag_ = GROW;          // right or bottom edge / bottom right corner
      } else {
        drag_ = MOVE;          // left edge of the frame
      }
      dx_ = Fl::event_x_root() - x(); dy_ = Fl::event_y_root() - y();
      gw_ = w() - ex; gh_ = h() - ey;
      px_ = Fl::event_x_root(); py_ = Fl::event_y_root();
      return 1;
    case FL_DRAG:
      if ((drag_ == MOVE || drag_ == GROW) && g_outline) {
        // outline: an XOR rectangle on the screen, the window follows on release
        if (!outlining_) {
          if (abs(Fl::event_x_root() - px_) + abs(Fl::event_y_root() - py_) < 3) return 1;
          outlining_ = true;
          out_ = geom();
          outline_begin(out_);
        }
        Rect n = out_;
        if (drag_ == MOVE) {
          n.x = Fl::event_x_root() - dx_;
          n.y = std::max(MENUBAR_H, Fl::event_y_root() - dy_);
        } else {
          n.w = std::max(160, Fl::event_x_root() - x() + gw_);
          n.h = std::max(TITLE_H + 60, Fl::event_y_root() - y() + gh_);
        }
        outline_move(out_, n);
        out_ = n;
        return 1;
      }
      if (drag_ == MOVE) {
        int nx = Fl::event_x_root() - dx_;
        int ny = std::max(MENUBAR_H, Fl::event_y_root() - dy_);   // the menu bar stays visible
        position(nx, ny);
        c->zoomed = false;
      } else if (drag_ == GROW) {
        size(std::max(160, ex + gw_), std::max(TITLE_H + 60, ey + gh_));
        c->zoomed = false;
      } else if (drag_ == BTN) {
        int p = hit_button(ex, ey) == armed_ ? armed_ : 0;
        if (p != pressed_) { pressed_ = p; redraw(); }
      }
      return 1;
    case FL_RELEASE:
      if (outlining_) { finish_outline(); drag_ = NONE; return 1; }
      if (drag_ == BTN) {
        int b = pressed_;
        pressed_ = armed_ = 0;
        drag_ = NONE;
        redraw();
        if (b == 1) close_client();
        else if (b == 2) toggle_zoom();
        else if (b == 3) iconify_client(c);   // window icon on the desktop
        else if (b == 4) depth(c);            // classic: to the front or to the back
      }
      drag_ = NONE;
      return 1;
    }
    return Fl_Double_Window::handle(e);
  }

  // ends an outline drag: the outline disappears and the window jumps to its place
  void finish_outline() {
    if (!outlining_) return;
    outlining_ = false;
    outline_end(out_);
    if (c->shaded) position(out_.x, out_.y);
    else resize(out_.x, out_.y, out_.w, out_.h);
    c->zoomed = false;
  }

  // XOR outlines on the root window (IncludeInferiors), with the server grabbed so that
  // nothing draws under them and leaves traces
  static GC xor_gc() {
    static GC gc = nullptr;
    if (!gc) {
      XGCValues gv;
      gv.function = GXxor;
      gv.foreground = 0xFFFFFF;
      gv.subwindow_mode = IncludeInferiors;
      gv.line_width = 1;
      gc = XCreateGC(dpy, root, GCFunction | GCForeground | GCSubwindowMode | GCLineWidth, &gv);
    }
    return gc;
  }
  static void outline_draw(Rect r) {
    Rect p = px(r);
    XDrawRectangle(dpy, root, xor_gc(), p.x, p.y, std::max(1, p.w - 1), std::max(1, p.h - 1));
    if (r.h > TITLE_H + 2) XDrawLine(dpy, root, xor_gc(), p.x, p.y + px(TITLE_H), p.x + p.w - 1, p.y + px(TITLE_H));
  }
  static bool grab_ok() {   // NIRVANA_OUTLINE_GRAB=0: no server grab (testing with xdotool)
    const char *e = getenv("NIRVANA_OUTLINE_GRAB");
    return !e || strcmp(e, "0");
  }
  static void outline_begin(Rect r) {
    XSync(dpy, False);
    if (grab_ok()) XGrabServer(dpy);
    outline_draw(r);
    XFlush(dpy);
    Fl::add_timeout(30, outline_safety);
  }
  static void outline_move(Rect a, Rect b) {
    outline_draw(a);
    outline_draw(b);
    XFlush(dpy);
  }
  static void outline_end(Rect r) {
    Fl::remove_timeout(outline_safety);
    outline_draw(r);
    if (grab_ok()) XUngrabServer(dpy);
    XFlush(dpy);
  }
  static void outline_safety(void *);   // never keep the server grabbed for long

private:
  enum { NONE, MOVE, GROW, BTN } drag_ = NONE;
  int dx_ = 0, dy_ = 0, gw_ = 0, gh_ = 0, armed_ = 0, pressed_ = 0, px_ = 0, py_ = 0;
  bool outlining_ = false;
  Rect out_{0, 0, 0, 0};
};

// ---------------------------------------------------------------------------
// Window management
// ---------------------------------------------------------------------------
static std::vector<Window> client_order;   // order of appearance (EWMH)
static unsigned long mru_clock = 0;

// _NET_CLIENT_LIST: the window list for the application switcher (and other programs)
static void publish_client_list() {
  XChangeProperty(dpy, root, A_NET_CLIENT_LIST, XA_WINDOW, 32, PropModeReplace,
                  (unsigned char *)client_order.data(), (int)client_order.size());
}

static void set_active_none() {
  g_active = nullptr;
  Window none = None;
  XChangeProperty(dpy, root, A_NET_ACTIVE, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&none, 1);
  if (desktop_win) XSetInputFocus(dpy, desktop_win, RevertToPointerRoot, CurrentTime);
}

// The most recent visible window other than the given one - gets the focus after hiding/closing.
static Client *most_recent_visible(Client *except) {
  Client *best = nullptr;
  for (auto &[w, c] : clients)
    if (c != except && !c->hidden && here(c) && (!best || c->mru > best->mru)) best = c;
  return best;
}

static void publish_screens() {
  long n = g_screens, cur = g_screen;
  XChangeProperty(dpy, root, XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False), XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&n, 1);
  XChangeProperty(dpy, root, XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&cur, 1);
}

static void set_client_screen(Client *c, int s) {
  c->screen = std::clamp(s, 0, g_screens - 1);
  long v = c->sticky ? 0xFFFFFFFFL : c->screen;
  XChangeProperty(dpy, c->win, XInternAtom(dpy, "_NET_WM_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&v, 1);
}

// The frames of the windows follow the shown screen (a hidden or iconified window stays down)
static void sync_screen_frames() {
  for (auto &[w, c] : clients) {
    bool want = here(c);
    if (!want && !c->off) {
      c->off = true;
      if (!c->hidden) XUnmapWindow(dpy, fl_xid(c->frame));
    } else if (want && c->off) {
      c->off = false;
      if (!c->hidden) XMapWindow(dpy, fl_xid(c->frame));
    }
  }
}

static void focus(Client *c, bool raise);
static void switch_screen(int s) {
  s = ((s % g_screens) + g_screens) % g_screens;
  if (s == g_screen) return;
  g_screen = s;
  sync_screen_frames();
  publish_screens();
  if (g_active && !here(g_active)) g_active = nullptr;
  if (!g_active) {
    if (Client *n = most_recent_visible(nullptr)) focus(n, true);
    else set_active_none();
  }
  XFlush(dpy);
}

// A window to another screen (Win+Shift+number, _NET_WM_DESKTOP from a program)
static void move_to_screen(Client *c, int s) {
  if (!c || c->sticky) return;
  set_client_screen(c, s);
  sync_screen_frames();
  if (g_active == c && !here(c)) {
    g_active = nullptr;
    if (Client *n = most_recent_visible(c)) focus(n, true);
    else set_active_none();
  }
}

// Preferences > Desktop: how many screens (windows of a screen that is gone come to the last one)
static void read_screens() {
  int n = std::clamp(read_desktop().screens, 1, 9);
  if (n == g_screens) return;
  g_screens = n;
  for (auto &[w, c] : clients) if (c->screen >= n) set_client_screen(c, n - 1);
  if (g_screen >= n) g_screen = n - 1;
  sync_screen_frames();
  publish_screens();
}

// _NIRVANA_ICONS on the root window: the iconified windows (in iconify order).
// Rambler draws the window icons on the desktop from it; double-click = _NET_ACTIVE_WINDOW.
static std::vector<Window> g_iconic;
static void publish_iconic() {
  XChangeProperty(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "_NIRVANA_ICONS", False), XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)g_iconic.data(), (int)g_iconic.size());
}
static void set_iconic(Client *c, bool on) {
  if (c->iconic == on) return;
  c->iconic = on;
  // a mark on the window itself: after Reset Nirvana the new Insight knows which hidden
  // windows were icons and which were only "Hide"
  Atom mark = XInternAtom(dpy, "_NIRVANA_ICONIFIED", False);
  long one = 1;
  if (on) XChangeProperty(dpy, c->win, mark, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&one, 1);
  else XDeleteProperty(dpy, c->win, mark);
  if (on) g_iconic.push_back(c->win);
  else g_iconic.erase(std::remove(g_iconic.begin(), g_iconic.end(), c->win), g_iconic.end());
  publish_iconic();
}

static bool icon_rect(Window w, Rect &r);

static void show_client(Client *c) {
  // a window from its desktop icon comes to the screen being shown (the icons are on every one)
  if (c->iconic && !here(c)) { set_client_screen(c, g_screen); c->off = false; }
  // back from the desktop icon: the outlines fly from the icon to the window
  Rect icon;
  if (c->iconic && c->hidden && icon_rect(c->win, icon)) zoom_rects(icon, c->frame->geom());
  set_iconic(c, false);
  if (!c->hidden) return;
  c->hidden = false;
  XMapWindow(dpy, fl_xid(c->frame));
  set_wm_state(c->win, NormalState);
  publish_state(c);
}

static void hide_client(Client *c) {
  if (c->hidden) return;
  c->hidden = true;
  // The frame is taken down with a raw XUnmapWindow - FLTK's hide() would destroy the X
  // window of the frame together with the application window inside.
  XUnmapWindow(dpy, fl_xid(c->frame));
  set_wm_state(c->win, IconicState);
  publish_state(c);
  if (g_active == c) {
    g_active = nullptr;
    if (Client *n = most_recent_visible(c)) focus(n);
    else set_active_none();
  }
}

// The icon of a minimized window on the desktop: Rambler puts its place in
// _NIRVANA_ICON_RECT on the window (x, y, w, h in the same units as the frames).
static bool icon_rect(Window w, Rect &r) {
  static Atom a = XInternAtom(dpy, "_NIRVANA_ICON_RECT", False);
  Atom type; int fmt; unsigned long n = 0, after; unsigned char *data = nullptr;
  bool ok = false;
  if (XGetWindowProperty(dpy, w, a, 0, 4, False, XA_CARDINAL, &type, &fmt, &n, &after, &data) == Success && data &&
      fmt == 32 && n == 4) {
    long *v = (long *)data;
    r = {(int)v[0], (int)v[1], (int)v[2], (int)v[3]};
    ok = r.w > 0 && r.h > 0;
  }
  if (data) XFree(data);
  return ok;
}

// Minimizing: the outlines fly from the window to its new icon. The icon appears only after
// Rambler has laid out the desktop again - we wait for its _NIRVANA_ICON_RECT (at most
// 0.4 s; without Rambler the window just shrinks into its middle).
struct PendingIconify { Window win = 0; Rect from; };
static PendingIconify g_pending;

static Atom A_ICON_RECT;

static void iconify_fallback(void *) {
  if (!g_pending.win) return;
  Rect r = g_pending.from;
  g_pending.win = 0;
  zoom_rects(r, center_of(r));
}

static void iconify_animate(Window w) {
  if (g_pending.win != w) return;
  Fl::remove_timeout(iconify_fallback);
  Rect to;
  Rect from = g_pending.from;
  g_pending.win = 0;
  if (icon_rect(w, to)) zoom_rects(from, to);
  else zoom_rects(from, center_of(from));
}

// The title bar button, a double click on the bar or "minimize" in the program:
// the window disappears and its icon appears on the desktop.
static void iconify_client(Client *c) {
  if (c->dialog || c->hidden) return;
  Rect r = c->frame->geom();
  set_iconic(c, true);
  hide_client(c);
  if (g_pending.win) iconify_fallback(nullptr);   // the previous one still waiting
  g_pending.win = c->win;
  g_pending.from = r;
  Fl::add_timeout(0.4, iconify_fallback);
}

static void focus(Client *c, bool raise) {
  if (c && !here(c)) switch_screen(c->screen);   // a window of another screen: to its screen
  if (c && c->hidden) { show_client(c); raise = true; }
  if (c) c->mru = ++mru_clock;
  Client *old = g_active;
  g_active = c;
  if (old && old != c && clients.count(old->win)) old->frame->redraw();
  if (!c) return;
  if (raise) XRaiseWindow(dpy, fl_xid(c->frame));
  XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
  Window w = c->win;
  XChangeProperty(dpy, root, A_NET_ACTIVE, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
  c->frame->redraw();
  if (desktop_win) XLowerWindow(dpy, desktop_win);
  Fl::remove_timeout(preview_soon);
  Fl::add_timeout(0.6, preview_soon);
}

// The client whose frame is this X window
static Client *client_of_frame(Window f) {
  for (auto &[w, c] : clients)
    if (c->frame && c->frame->shown() && fl_xid(c->frame) == f) return c;
  return nullptr;
}

// Is the window the top one of all visible program windows?
static bool on_top(Client *c) {
  Window r, p, *kids = nullptr;
  unsigned n = 0;
  if (!XQueryTree(dpy, root, &r, &p, &kids, &n)) return true;
  bool top = false;
  for (int i = (int)n - 1; i >= 0; i--) {
    Client *k = client_of_frame(kids[i]);
    if (!k || k->hidden) continue;
    top = k == c;
    break;
  }
  if (kids) XFree(kids);
  return top;
}

// The depth gadget: a window that is on top goes to the very back (still above the
// desktop), any other comes to the front. The keyboard focus stays where it was.
static void depth(Client *c) {
  if (!c || c->hidden) return;
  if (on_top(c)) {
    XLowerWindow(dpy, fl_xid(c->frame));
    if (desktop_win) XLowerWindow(dpy, desktop_win);
  } else {
    XRaiseWindow(dpy, fl_xid(c->frame));
  }
  if (c == g_active) {
    Fl::remove_timeout(preview_soon);
    Fl::add_timeout(0.6, preview_soon);
  }
}

void Frame::outline_safety(void *) {
  for (auto &[w, c] : clients)
    if (c->frame) c->frame->finish_outline();
}

// WM_NORMAL_HINTS with the same minimum and maximum size: the program does not want
// to be resized (FLTK windows without resizable(), e.g. the Preferences panels)
static bool fixed_size(Window w) {
  XSizeHints h;
  long supplied;
  if (!XGetWMNormalHints(dpy, w, &h, &supplied)) return false;
  return (h.flags & PMinSize) && (h.flags & PMaxSize) && h.min_width == h.max_width &&
         h.min_height == h.max_height && h.min_width > 0;
}

static void manage(Window w, bool existing) {
  if (clients.count(w) || w == desktop_win) return;
  XWindowAttributes wa;
  if (!XGetWindowAttributes(dpy, w, &wa) || wa.override_redirect) return;
  for (auto &[cw, c] : clients) if (fl_xid(c->frame) == w) return;   // our frames
  if (fl_find(w)) return;                                          // other FLTK windows of insight

  if (is_desktop(w)) {
    // Desktop: no frame, full screen, always at the bottom.
    desktop_win = w;
    XMapWindow(dpy, w);
    XLowerWindow(dpy, w);
    XGrabButton(dpy, AnyButton, AnyModifier, w, False, ButtonPressMask, GrabModeSync,
                GrabModeAsync, None, None);
    return;
  }
  if (has_atom(w, A_NET_WM_TYPE, A_NET_WM_TYPE_DOCK)) { XMapWindow(dpy, w); return; }

  auto *c = new Client;
  c->win = w;
  c->title = get_title(w);
  std::string cls = get_class(w);
  c->cls = cls;
  // only Rambler animates its folder windows and dialogs itself; FLTK writes every class with
  // a capital letter ("nirvana-preferences" -> "Nirvana-preferences"), so the Preferences,
  // Nirvana Shell... were taken for Rambler and opened without any animation
  c->own_anim = cls == "Nirvana-Rambler" || cls == "Nirvana-Dialog" || cls == "Nirvana-Desktop";
  Window tf = 0;
  c->dialog = (XGetTransientForHint(dpy, w, &tf) && tf) ||
              has_atom(w, A_NET_WM_TYPE, A_NET_WM_TYPE_DIALOG) || cls == "Nirvana-Dialog";
  c->bare = undecorated(w);
  c->fixed = fixed_size(w);

  int x = un(wa.x), y = un(wa.y);
  // StaticGravity (FLTK, many other programs): the given position refers to the
  // application window itself, so the frame is placed so that the content lands exactly there.
  XSizeHints sh;
  long supplied;
  if (XGetWMNormalHints(dpy, w, &sh, &supplied) && (sh.flags & PWinGravity) &&
      sh.win_gravity == StaticGravity && (x || y)) {
    x -= c->fw();
    y -= c->th();
  }
  if (!existing && x == 0 && y == 0) {
    static int cascade = 0;
    x = 60 + 26 * (cascade % 8);
    y = MENUBAR_H + 30 + 26 * (cascade % 8);
    cascade++;
  }
  int fw = un(wa.width) + 2 * c->fw(), fh = un(wa.height) + c->th() + c->fw();
  // the window fits into the work area: below the menu bar and above the dock
  // (Chromium/Brave take the size of the whole screen on the first start)
  Rect area = work_area();
  if ((!c->dialog && !c->bare) || fh > area.h) {
    fw = std::min(fw, area.w);
    fh = std::min(fh, area.h);
    x = std::clamp(x, area.x, std::max(area.x, area.x + area.w - fw));
    y = std::clamp(y, area.y, std::max(area.y, area.y + area.h - fh));
  }
  y = std::max(y, MENUBAR_H);

  // a new window "flies out" of the middle of the place where it will appear
  if (!existing && !c->own_anim) {
    Rect from;
    if (!zoom_source(w, from)) from = center_of({x, y, fw, fh});
    zoom_rects(from, {x, y, fw, fh});
  }
  c->frame = new Frame(c, x, y, fw, fh);
  c->frame->xclass("Nirvana-Frame");
  c->frame->show();
  Window fx = fl_xid(c->frame);
  // FLTK marks its windows as XDND targets. The frame is not one - otherwise files
  // dragged from other programs would land in the frame instead of the application window.
  XDeleteProperty(dpy, fx, XInternAtom(dpy, "XdndAware", False));

  XAddToSaveSet(dpy, w);
  XSetWindowBorderWidth(dpy, w, 0);
  XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask);
  if (wa.map_state == IsViewable) c->ignore_unmap++;   // reparenting will cause an UnmapNotify
  XReparentWindow(dpy, w, fx, px(c->fw()), px(c->th()));
  // Resize requests of a window (e.g. the grow box of a Rambler window) must reach
  // us also after reparenting into the frame - otherwise the window grows or shrinks
  // while the frame keeps its old size.
  {
    XWindowAttributes fa;
    XGetWindowAttributes(dpy, fx, &fa);
    XSelectInput(dpy, fx, fa.your_event_mask | SubstructureRedirectMask);
  }
  set_frame_extents(w, c->bare);
  XMapWindow(dpy, w);
  set_wm_state(w, NormalState);
  // the program wants to start maximized (e.g. Brave remembered that state)
  if (!c->dialog && (has_atom(w, A_NET_WM_STATE, A_NET_WM_STATE_MAX_V) ||
                     has_atom(w, A_NET_WM_STATE, A_NET_WM_STATE_MAX_H)))
    c->frame->toggle_zoom();
  // a click in an application window should activate it - we catch it synchronously
  XGrabButton(dpy, AnyButton, AnyModifier, w, False, ButtonPressMask, GrabModeSync,
              GrabModeAsync, None, None);
  clients[w] = c;
  client_order.push_back(w);
  publish_client_list();
  // its screen: the one it had (Reset Nirvana keeps _NET_WM_DESKTOP), otherwise the shown one;
  // the Jobs window and windows asking to be on all screens are sticky
  {
    Atom type; int fmt; unsigned long n = 0, after; unsigned char *data = nullptr;
    long d = -2;
    if (XGetWindowProperty(dpy, w, XInternAtom(dpy, "_NET_WM_DESKTOP", False), 0, 1, False, XA_CARDINAL, &type,
                           &fmt, &n, &after, &data) == Success && data && n == 1)
      d = (long)(unsigned)((long *)data)[0] == 0xFFFFFFFFL ? -1 : ((long *)data)[0];
    if (data) XFree(data);
    c->sticky = d == -1 || strcasecmp(cls.c_str(), "nirvana-progress") == 0 ||
                has_atom(w, A_NET_WM_STATE, XInternAtom(dpy, "_NET_WM_STATE_STICKY", False));
    set_client_screen(c, existing && d >= 0 ? (int)d : g_screen);
    if (!here(c)) { c->off = true; XUnmapWindow(dpy, fx); }
  }
  c->frame->sync_client();
  // a window that does not want the keyboard (WM_HINTS input = False, or the class
  // "Nirvana-Progress" - the progress of Rambler's file jobs) comes up on top but leaves the
  // focus where it was: typing and F5 go on undisturbed in the window the person works in
  bool no_input = strcasecmp(get_class(w).c_str(), "nirvana-progress") == 0;
  if (XWMHints *h = XGetWMHints(dpy, w)) {
    if ((h->flags & InputHint) && !h->input) no_input = true;
    XFree(h);
  }
  if (c->off) return;   // (Reset Nirvana: a window of another screen)
  if (no_input && g_active) XRaiseWindow(dpy, fl_xid(c->frame));
  else focus(c);
}

static void unmanage(Client *c, bool destroyed) {
  set_iconic(c, false);
  if (!destroyed) {
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    XReparentWindow(dpy, c->win, root, px(c->frame->x() + c->fw()), px(c->frame->y() + c->th()));
    XRemoveFromSaveSet(dpy, c->win);
    set_wm_state(c->win, WithdrawnState);
  }
  forget_preview(c->win);
  clients.erase(c->win);
  client_order.erase(std::remove(client_order.begin(), client_order.end(), c->win), client_order.end());
  publish_client_list();
  if (g_active == c) g_active = nullptr;
  Rect gone = c->frame->geom();
  c->frame->hide();
  XSync(dpy, False);
  if (!c->own_anim) zoom_rects(gone, center_of(gone));   // a closing window "collapses" into its center
  Fl::delete_widget(c->frame);
  delete c;
  // focus on the most recently used visible window
  if (!g_active) {
    if (Client *n = most_recent_visible(nullptr)) focus(n);
    else set_active_none();
  }
}

static Client *client_of(Window w) {
  auto it = clients.find(w);
  return it == clients.end() ? nullptr : it->second;
}

// ---------------------------------------------------------------------------
// Alt+Tab: the window switcher with previews
// ---------------------------------------------------------------------------
// Without a compositor a window that is covered cannot be photographed, so each window's
// preview is taken while it is the active one (on top): shortly after it gets the focus and
// then every few seconds. Minimized windows keep their last preview.
static const int TW = 200, TH = 130;   // the preview size (FLTK units)

static Fl_RGB_Image *grab_preview(Client *c) {
  if (!c || c->hidden || !c->frame->shown()) return nullptr;
  Window xw = fl_xid(c->frame);
  XWindowAttributes wa;
  if (!XGetWindowAttributes(dpy, xw, &wa) || wa.map_state != IsViewable || wa.width < 8 || wa.height < 8) return nullptr;
  auto old = XSetErrorHandler(ignore_errors);
  XImage *im = XGetImage(dpy, xw, 0, 0, wa.width, wa.height, AllPlanes, ZPixmap);
  XSync(dpy, False);
  XSetErrorHandler(old);
  if (!im) return nullptr;
  double k = std::min((double)px(TW) / wa.width, (double)px(TH) / wa.height);
  int w = std::max(1, (int)(wa.width * k)), h = std::max(1, (int)(wa.height * k));
  auto *buf = new uchar[w * h * 3];
  bool fast = im->bits_per_pixel == 32 && im->red_mask == 0xff0000 && im->green_mask == 0xff00 && im->blue_mask == 0xff;
  // each preview pixel: the average of a few source pixels (smoother than nearest)
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int x0 = (int)(x / k), y0 = (int)(y / k), x1 = std::min(wa.width - 1, (int)((x + 1) / k)),
          y1 = std::min(wa.height - 1, (int)((y + 1) / k));
      int sx = std::max(1, (x1 - x0) / 3), sy = std::max(1, (y1 - y0) / 3);
      unsigned r = 0, g = 0, b = 0, n = 0;
      for (int yy = y0; yy <= y1; yy += sy)
        for (int xx = x0; xx <= x1; xx += sx) {
          unsigned long p = fast ? *(uint32_t *)(im->data + yy * im->bytes_per_line + xx * 4) : XGetPixel(im, xx, yy);
          r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++;
        }
      uchar *o = buf + (y * w + x) * 3;
      o[0] = (uchar)(r / n); o[1] = (uchar)(g / n); o[2] = (uchar)(b / n);
    }
  XDestroyImage(im);
  auto *img = new Fl_RGB_Image(buf, w, h, 3);
  img->alloc_array = 1;
  img->scale(un(w), un(h), 0, 1);   // drawn in FLTK units, the pixels kept
  return img;
}

static std::map<Window, Fl_RGB_Image *> g_previews;

static void take_preview(Client *c) {
  if (g_classic && !on_top(c)) return;   // an active window may lie under others here
  if (Fl_RGB_Image *img = grab_preview(c)) {
    delete g_previews[c->win];
    g_previews[c->win] = img;
  }
}

static void forget_preview(Window w) {
  auto it = g_previews.find(w);
  if (it != g_previews.end()) { delete it->second; g_previews.erase(it); }
}

static bool cycling = false;
static std::vector<Window> cycle_list;   // windows, the most recently used first
static int cycle_idx = 0;

static void preview_tick(void *) {
  if (g_active && clients.count(g_active->win) && !cycling) take_preview(g_active);
  Fl::repeat_timeout(3.0, preview_tick);
}

// a moment after the focus changed - the window has drawn itself on top by then
static void preview_soon(void *) {
  if (g_active && clients.count(g_active->win)) take_preview(g_active);
}

class Switcher : public Fl_Double_Window {
public:
  static const int CW = TW + 24, CH = TH + 44;
  int cols = 1;
  Switcher() : Fl_Double_Window(0, 0, 10, 10, "insight-switcher") {
    border(0);
    end();
    set_override();
  }
  void layout() {
    int n = (int)cycle_list.size();
    cols = std::max(1, std::min(n, (screen_w - 60) / CW));
    int rows = (n + cols - 1) / cols;
    int W = cols * CW + 24, H = rows * CH + 24;
    resize((screen_w - W) / 2, std::max(MENUBAR_H + 10, (screen_h - H) / 2), W, H);
  }
  int cell_at(int ex, int ey) const {
    int c = (ex - 12) / CW, r = (ey - 12) / CH;
    if (ex < 12 || ey < 12 || c >= cols) return -1;
    int i = r * cols + c;
    return i < (int)cycle_list.size() ? i : -1;
  }
  void draw() override {
    fl_draw_box(UP_BOX, 0, 0, w(), h(), GRAY);
    fl_color(DARK);
    fl_rect(0, 0, w(), h());
    for (int i = 0; i < (int)cycle_list.size(); i++) {
      Client *c = client_of(cycle_list[i]);
      if (!c) continue;
      int cx = 12 + (i % cols) * CW, cy = 12 + (i / cols) * CH;
      bool sel = i == cycle_idx;
      if (sel) {
        fl_color(SEL);
        fl_rectf(cx + 2, cy + 2, CW - 4, CH - 4);
      }
      int px0 = cx + 12, py0 = cy + 10;
      auto it = g_previews.find(c->win);
      if (it != g_previews.end()) {
        Fl_RGB_Image *img = it->second;
        int ix = px0 + (TW - img->w()) / 2, iy = py0 + (TH - img->h()) / 2;
        img->draw(ix, iy);
        if (c->iconic || c->hidden) {   // minimized: a light veil over the preview
          fl_color(fl_color_average(GRAY, FL_WHITE, 0.5f));
          for (int yy = iy; yy < iy + img->h(); yy += 2) fl_xyline(ix, yy, ix + img->w() - 1);
        }
        fl_color(DARK);
        fl_rect(ix - 1, iy - 1, img->w() + 2, img->h() + 2);
      } else {   // no preview yet: an empty little window with the program's initial
        fl_draw_box(WELL_BOX, px0 + 30, py0 + 14, TW - 60, TH - 28, CONTENT);
        draw_titlebar(px0 + 30, py0 + 14, TW - 60, "", true, 0, 0);
        std::string ini = c->cls.empty() ? (c->title.empty() ? "?" : c->title.substr(0, 1)) : c->cls.substr(0, 1);
        fl_font(FONT_SYSTEM, 36);
        fl_color(MID);
        fl_draw(ini.c_str(), px0 + 30, py0 + 14 + TITLE_H, TW - 60, TH - 28 - TITLE_H, FL_ALIGN_CENTER);
      }
      std::string t = c->title.empty() ? c->cls : c->title;
      fl_font(sel ? FONT_SYSTEM : FONT_SMALL, SIZE_SMALL + 1);
      t = ellipsize(t, CW - 16);
      fl_color(sel ? SEL_TEXT : TEXT);
      fl_draw(t.c_str(), cx + 8, cy + TH + 16, CW - 16, 20, FL_ALIGN_CENTER, nullptr, 0);
    }
  }
  int handle(int e) override;
};
static Switcher *g_switcher = nullptr;

static void alt_tab_end(bool choose);

int Switcher::handle(int e) {
  if (e == FL_PUSH) {
    int i = cell_at(Fl::event_x(), Fl::event_y());
    if (i >= 0) { cycle_idx = i; alt_tab_end(true); }
    return 1;
  }
  if (e == FL_MOVE) {
    int i = cell_at(Fl::event_x(), Fl::event_y());
    if (i >= 0 && i != cycle_idx) { cycle_idx = i; redraw(); }
    return 1;
  }
  return Fl_Double_Window::handle(e);
}

static void alt_tab(int step) {
  if (!cycling) {
    std::vector<Client *> all;
    for (auto &[w, c] : clients) if (!c->dialog && here(c)) all.push_back(c);   // this screen
    if (all.size() < 2) return;
    std::sort(all.begin(), all.end(), [](Client *a, Client *b) { return a->mru > b->mru; });
    cycle_list.clear();
    for (Client *c : all) cycle_list.push_back(c->win);
    if (g_active) take_preview(g_active);   // the window on top now: a fresh picture
    cycling = true;
    cycle_idx = 0;
    // keyboard to us, to notice Alt being released
    XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    if (!g_switcher) g_switcher = new Switcher;
    g_switcher->layout();
    g_switcher->show();
    XRaiseWindow(dpy, fl_xid(g_switcher));
  }
  int n = (int)cycle_list.size();
  cycle_idx = ((cycle_idx + step) % n + n) % n;
  g_switcher->redraw();
}

static void alt_tab_end(bool choose) {
  if (!cycling) return;
  cycling = false;
  XUngrabKeyboard(dpy, CurrentTime);
  if (g_switcher) g_switcher->hide();
  if (choose && cycle_idx >= 0 && cycle_idx < (int)cycle_list.size())
    if (Client *c = client_of(cycle_list[cycle_idx])) focus(c);   // also brings back a minimized one
}

// The Appearance panel changed the scheme: new palette, root window background, frames.
static void set_root_background() {
  uchar r, g, b;
  Fl::get_color(plat::DESK_A, r, g, b);
  XSetWindowBackground(dpy, root, ((unsigned long)r << 16) | (g << 8) | b);   // TrueColor
  XClearWindow(dpy, root);
}

// The pointer over the desktop - and over every window that does not set its own (they all
// inherit it): from the theme chosen in Appearance (reload_appearance sets it for the display)
static void set_root_cursor() {
  static Cursor current = None;
  Cursor c = XcursorLibraryLoadCursor(dpy, "left_ptr");
  if (!c) c = XCreateFontCursor(dpy, XC_left_ptr);
  XDefineCursor(dpy, root, c);
  if (current != None) XFreeCursor(dpy, current);
  current = c;
  XFlush(dpy);
}

// ---------------------------------------------------------------------------
// XSETTINGS (only when no other settings manager runs): the pointer for GTK programs -
// Gtk/CursorThemeName and Gtk/CursorThemeSize. GTK follows a change at once, so Firefox,
// the editors... take the new pointer without being opened again.
// ---------------------------------------------------------------------------
static Window xs_win = 0;
static Atom A_XS_SEL, A_XS_SETTINGS;
static unsigned xs_serial = 0;

static void xsettings_publish() {
  if (!xs_win) return;
  const char *t = getenv("XCURSOR_THEME");
  const char *z = getenv("XCURSOR_SIZE");
  std::string theme = t && *t ? t : "";
  int size = z ? atoi(z) : 0;
  std::string b;
  auto u8 = [&](unsigned v) { b += (char)v; };
  auto u16 = [&](unsigned v) { b.append((const char *)&v, 2); };   // native byte order (said in byte 0)
  auto u32 = [&](uint32_t v) { b.append((const char *)&v, 4); };
  auto pad = [&] { while (b.size() % 4) b += '\0'; };
  uint32_t one = 1;
  u8(*(char *)&one == 1 ? 0 : 1);   // LSBFirst = 0, MSBFirst = 1
  u8(0); u8(0); u8(0);
  u32(++xs_serial);
  int n = (theme.empty() ? 0 : 1) + (size > 0 ? 1 : 0);
  u32((uint32_t)n);
  auto name = [&](int type, const char *nm) {
    u8((unsigned)type); u8(0); u16((unsigned)strlen(nm));
    b += nm; pad();
    u32(xs_serial);
  };
  if (!theme.empty()) {
    name(1, "Gtk/CursorThemeName");
    u32((uint32_t)theme.size());
    b += theme; pad();
  }
  if (size > 0) {
    name(0, "Gtk/CursorThemeSize");
    u32((uint32_t)size);
  }
  XChangeProperty(dpy, xs_win, A_XS_SETTINGS, A_XS_SETTINGS, 8, PropModeReplace, (const unsigned char *)b.data(), (int)b.size());
  XFlush(dpy);
}

static void xsettings_start() {
  A_XS_SEL = XInternAtom(dpy, "_XSETTINGS_S0", False);
  A_XS_SETTINGS = XInternAtom(dpy, "_XSETTINGS_SETTINGS", False);
  if (XGetSelectionOwner(dpy, A_XS_SEL) != None) return;   // another manager (xsettingsd...): leave it
  xs_win = XCreateSimpleWindow(dpy, root, -10, -10, 1, 1, 0, 0, 0);
  xsettings_publish();
  XSetSelectionOwner(dpy, A_XS_SEL, xs_win, CurrentTime);
  if (XGetSelectionOwner(dpy, A_XS_SEL) != xs_win) { XDestroyWindow(dpy, xs_win); xs_win = 0; return; }
  XClientMessageEvent m{};
  m.type = ClientMessage;
  m.window = root;
  m.message_type = XInternAtom(dpy, "MANAGER", False);
  m.format = 32;
  m.data.l[0] = CurrentTime;
  m.data.l[1] = (long)A_XS_SEL;
  m.data.l[2] = (long)xs_win;
  XSendEvent(dpy, root, False, StructureNotifyMask, (XEvent *)&m);
  XFlush(dpy);
}

// The pointer changed in Appearance: every pointer already made - in every program (FLTK keeps
// its pointers for good, GTK and Qt theirs) - takes its new picture at once. Xcursor names the
// pointers it makes ("left_ptr", "xterm"...); XFixes changes all pointers of a name together.
static void replace_cursors() {
  static std::string last;
  const char *t = getenv("XCURSOR_THEME");
  const char *z = getenv("XCURSOR_SIZE");
  std::string now = std::string(t ? t : "") + "/" + (z ? z : "");
  if (now == last) return;
  bool first = last.empty();
  last = now;
  if (first) return;   // the start: nothing made yet with another theme
  int ev, er;
  if (!XFixesQueryExtension(dpy, &ev, &er)) return;
  static const char *const names[] = {
      "left_ptr", "default", "arrow", "top_left_arrow", "xterm", "text", "ibeam", "watch", "wait", "left_ptr_watch",
      "progress", "hand1", "hand2", "pointer", "pointing_hand", "crosshair", "cross", "tcross", "fleur", "move",
      "all-scroll", "grab", "grabbing", "openhand", "closedhand", "sb_h_double_arrow", "sb_v_double_arrow",
      "h_double_arrow", "v_double_arrow", "ew-resize", "ns-resize", "col-resize", "row-resize", "nwse-resize",
      "nesw-resize", "size_hor", "size_ver", "size_fdiag", "size_bdiag", "bd_double_arrow", "fd_double_arrow",
      "top_left_corner", "top_right_corner", "bottom_left_corner", "bottom_right_corner", "top_side",
      "bottom_side", "left_side", "right_side", "n-resize", "s-resize", "e-resize", "w-resize", "ne-resize",
      "nw-resize", "se-resize", "sw-resize", "question_arrow", "help", "whats_this", "not-allowed",
      "crossed_circle", "no-drop", "forbidden", "copy", "dnd-copy", "alias", "link", "dnd-link", "dnd-move",
      "dnd-none", "context-menu", "cell", "X_cursor", "pirate", "vertical-text"};
  for (const char *n : names) {
    Cursor c = XcursorLibraryLoadCursor(dpy, n);
    if (!c) continue;
    XFixesChangeCursorByName(dpy, c, n);
    XFreeCursor(dpy, c);
  }
  XFlush(dpy);
}

static void appearance_changed() {
  plat::reload_appearance();
  set_root_cursor();
  replace_cursors();
  xsettings_publish();
  set_root_background();
  for (auto &[w, c] : clients) c->frame->redraw();
}

// Raw X events - we handle the ones about application windows and the root.
static int x_handler(void *event, void *) {
  XEvent *e = (XEvent *)event;
  switch (e->type) {
  case KeyPress: {
    KeySym ks = XLookupKeysym(&e->xkey, 0);
    if (ks == XK_Tab && (e->xkey.state & Mod1Mask)) {
      alt_tab(e->xkey.state & ShiftMask ? -1 : 1);
      return 1;
    }
    // screens: Win+M the next one (Win+Shift+M the previous), Win+1..9 that one,
    // Win+Shift+1..9 the active window to it
    if ((e->xkey.state & Mod4Mask) && !cycling) {
      bool sh = e->xkey.state & ShiftMask;
      if (ks == XK_m) { switch_screen(g_screen + (sh ? -1 : 1)); return 1; }
      if (ks >= XK_1 && ks <= XK_9) {
        int n = (int)(ks - XK_1);
        if (n < g_screens) {
          if (sh) move_to_screen(g_active, n);
          else switch_screen(n);
        }
        return 1;
      }
    }
    if (cycling) {
      if (ks == XK_Escape) alt_tab_end(false);
      else if (ks == XK_Right) alt_tab(1);
      else if (ks == XK_Left) alt_tab(-1);
      else if (ks == XK_Down) alt_tab(g_switcher ? g_switcher->cols : 1);
      else if (ks == XK_Up) alt_tab(g_switcher ? -g_switcher->cols : -1);
      else if (ks == XK_Return) alt_tab_end(true);
      return 1;
    }
    return 0;
  }
  case KeyRelease: {
    KeySym ks = XLookupKeysym(&e->xkey, 0);
    if (cycling && (ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L || ks == XK_Meta_R)) {
      alt_tab_end(true);
      return 1;
    }
    return cycling ? 1 : 0;
  }
  case MapRequest:
    if (e->xmaprequest.window == desktop_win) {
      // the desktop shown again (e.g. Rambler switched its type to DESKTOP)
      XMapWindow(dpy, desktop_win);
      XLowerWindow(dpy, desktop_win);
    } else if (Client *c = client_of(e->xmaprequest.window)) XMapWindow(dpy, c->win);
    else manage(e->xmaprequest.window, false);
    return 1;

  case ConfigureRequest: {
    XConfigureRequestEvent &r = e->xconfigurerequest;
    if (Client *c = client_of(r.window)) {
      Frame *f = c->frame;
      int nx = f->x(), ny = f->y(), nw = f->w(), nh = f->h(), nh_shrink = 0;
      // the program's request is in pixels, the frame in FLTK units
      if (r.value_mask & CWX) nx = un(r.x) - c->fw();
      if (r.value_mask & CWY) {
        ny = un(r.y) - c->th();
        // the window wants to stand under the menu bar: move it lower and shorten it by as much
        if (ny < MENUBAR_H && (r.value_mask & CWHeight)) nh_shrink = MENUBAR_H - ny;
        ny = std::max(MENUBAR_H, ny);
      }
      if (r.value_mask & CWWidth) nw = un(r.width) + 2 * c->fw();
      if (r.value_mask & CWHeight) nh = un(r.height) + c->th() + c->fw() - nh_shrink;
      Rect area = work_area();   // no larger than the work area, not above the menu bar
      nw = std::min(nw, area.w);
      if (!c->shaded) nh = std::min(nh, area.h);
      if (c->shaded) { c->full_h = nh; nh = f->h(); }
      f->resize(nx, ny, nw, nh);
      f->sync_client();
    } else {
      XWindowChanges wc;
      wc.x = r.x; wc.y = r.y; wc.width = r.width; wc.height = r.height;
      wc.border_width = r.border_width; wc.sibling = r.above; wc.stack_mode = r.detail;
      XConfigureWindow(dpy, r.window, r.value_mask, &wc);
      if (r.window == desktop_win) XLowerWindow(dpy, desktop_win);
    }
    return 1;
  }

  case UnmapNotify: {
    Client *c = client_of(e->xunmap.window);
    if (!c || e->xunmap.event != e->xunmap.window) return c ? 1 : 0;
    if (c->ignore_unmap > 0) { c->ignore_unmap--; return 1; }
    unmanage(c, false);
    return 1;
  }

  case DestroyNotify:
    if (Client *c = client_of(e->xdestroywindow.window)) { unmanage(c, true); return 1; }
    if (e->xdestroywindow.window == desktop_win) desktop_win = 0;
    return 0;

  case PropertyNotify:
    if (e->xproperty.window == root && e->xproperty.atom == A_NIRVANA_DOCK) {
      read_dock_height();
      publish_workarea();
      return 0;
    }
    if (e->xproperty.window == root && e->xproperty.atom == A_DESKTOP) {
      read_window_mode();   // Preferences > Desktop: classic windows / outline dragging
      read_screens();       // ... and the number of screens
      for (auto &[w, c] : clients) c->frame->redraw();
      return 0;
    }
    if (e->xproperty.window == root && e->xproperty.atom == A_APPEARANCE) {
      appearance_changed();
      return 0;   // let the other handlers see it too
    }
    if (Client *c = client_of(e->xproperty.window)) {
      if (e->xproperty.atom == XA_WM_NAME || e->xproperty.atom == A_NET_WM_NAME) {
        c->title = get_title(c->win);
        c->frame->redraw();
      } else if (e->xproperty.atom == XA_WM_NORMAL_HINTS) {
        c->fixed = fixed_size(c->win);
        c->frame->redraw();
      } else if (g_pending.win == c->win && e->xproperty.atom == A_ICON_RECT) {
        iconify_animate(c->win);   // Rambler has placed the icon of the window just minimized
      }
      return 1;
    }
    return 0;

  case ButtonPress:
    if (Client *c = client_of(e->xbutton.window)) {
      if (c != g_active) focus(c, !g_classic);
      XAllowEvents(dpy, ReplayPointer, CurrentTime);
      return 1;
    }
    if (desktop_win && e->xbutton.window == desktop_win) {
      // The menu bar acts on the active window - clicking it changes nothing.
      if (e->xbutton.y_root < MENUBAR_H) {
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
        return 1;
      }
      // click on the desktop: no window is active
      Client *old = g_active;
      g_active = nullptr;
      if (old) old->frame->redraw();
      XSetInputFocus(dpy, desktop_win, RevertToPointerRoot, CurrentTime);
      Window none = None;
      XChangeProperty(dpy, root, A_NET_ACTIVE, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&none, 1);
      XAllowEvents(dpy, ReplayPointer, CurrentTime);
      return 1;
    }
    return 0;

  case ClientMessage:
    if (e->xclient.window == root && e->xclient.message_type == XInternAtom(dpy, "_NIRVANA_RESTART", False)) {
      // Special > Reset Nirvana: restart ourselves. Client windows are in the "save set",
      // so after the connection closes they go back to the root window, and the new Insight
      // takes them over at startup (XQueryTree).
      char exe[4096];
      ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
      exe[n > 0 ? n : 0] = 0;   // the real path - otherwise the process would be called "exe"
      // after a package upgrade the running file is deleted: "/usr/bin/x (deleted)"
      if (char *d = n > 0 ? strstr(exe, " (deleted)") : nullptr) *d = 0;
      if (n > 0 && access(exe, X_OK) != 0) n = 0;
      XCloseDisplay(dpy);
      if (n > 0) execv(exe, g_argv);
      execvp("nirvana-insight", g_argv);   // PATH (e.g. after an upgrade)
      execv(g_argv[0], g_argv);
      _exit(1);
    }
    if (e->xclient.message_type == A_NET_WM_STATE) {
      // data.l[0]: 0 remove, 1 add, 2 toggle; l[1], l[2]: properties
      Client *c = client_of(e->xclient.window);
      if (!c) return 0;
      long act = e->xclient.data.l[0];
      if ((Atom)e->xclient.data.l[1] == A_NET_WM_STATE_SHADED ||
          (Atom)e->xclient.data.l[2] == A_NET_WM_STATE_SHADED) {
        bool want = act == 2 ? !c->shaded : act == 1;
        if (want != c->shaded && !c->dialog) c->frame->toggle_shade();
      }
      // maximizing (the title bar button of Chromium, Firefox...) = zoom
      Atom a1 = (Atom)e->xclient.data.l[1], a2 = (Atom)e->xclient.data.l[2];
      if (a1 == A_NET_WM_STATE_MAX_V || a1 == A_NET_WM_STATE_MAX_H || a2 == A_NET_WM_STATE_MAX_V ||
          a2 == A_NET_WM_STATE_MAX_H) {
        bool want = act == 2 ? !c->zoomed : act == 1;
        if (want != c->zoomed && !c->dialog) c->frame->toggle_zoom();
      }
      // _NET_WM_STATE_HIDDEN: some programs minimize this way
      if (a1 == A_NET_WM_STATE_HIDDEN || a2 == A_NET_WM_STATE_HIDDEN) {
        bool want = act == 2 ? !c->hidden : act == 1;
        if (want && !c->hidden) iconify_client(c);
        else if (!want && c->hidden) focus(c);
      }
      return 1;
    }
    if (e->xclient.message_type == A_NET_REQUEST_FRAME_EXTENTS) {
      // the program asks for the frame even before showing the window (GTK, Chromium)
      set_frame_extents(e->xclient.window, undecorated(e->xclient.window));
      return 1;
    }
    if (e->xclient.message_type == A_NET_ACTIVE) {
      if (Client *c = client_of(e->xclient.window)) {
        if (c->shaded) c->frame->toggle_shade();
        focus(c);                 // a hidden window is shown as well
      }
      return 1;
    }
    if (e->xclient.message_type == A_WM_CHANGE_STATE) {
      // ICCCM: the application (or the switcher) asks to hide the window
      if (Client *c = client_of(e->xclient.window))
        if (e->xclient.data.l[0] == IconicState) {
          // "Hide" from the application menu (gary) marks the window with _NIRVANA_HIDE - no icon
          // on the desktop; minimizing from the program itself creates an icon.
          Atom mark = XInternAtom(dpy, "_NIRVANA_HIDE", False);
          Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
          bool plain = XGetWindowProperty(dpy, c->win, mark, 0, 1, True, AnyPropertyType, &type, &fmt, &n, &after,
                                          &data) == Success && type != None;
          if (data) XFree(data);
          if (plain) hide_client(c); else iconify_client(c);
        }
      return 1;
    }
    if (e->xclient.message_type == XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False)) {
      switch_screen((int)e->xclient.data.l[0]);
      return 1;
    }
    if (e->xclient.message_type == XInternAtom(dpy, "_NET_WM_DESKTOP", False)) {
      if (Client *c = client_of(e->xclient.window)) {
        long d = e->xclient.data.l[0];
        if ((unsigned long)d == 0xFFFFFFFFUL) { c->sticky = true; set_client_screen(c, g_screen); sync_screen_frames(); }
        else { c->sticky = false; move_to_screen(c, (int)d); }
      }
      return 1;
    }
    if (e->xclient.message_type == A_NET_CLOSE) {
      if (Client *c = client_of(e->xclient.window)) c->frame->close_client();
      return 1;
    }
    return 0;
  }
  return 0;
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
  g_argv = argv;
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--help")) {
      printf("nirvana-insight: Nirvana Desktop window manager (X11)\n");
      return 0;
    }

  fl_open_display();
  dpy = fl_display;
  root = DefaultRootWindow(dpy);
  plat::init();   // also sets the interface scale
  screen_w = un(DisplayWidth(dpy, DefaultScreen(dpy)));
  screen_h = un(DisplayHeight(dpy, DefaultScreen(dpy)));

  // Become the window manager: only one client can select
  // SubstructureRedirect on the root.
  XSetErrorHandler(detect_wm);
  XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | PropertyChangeMask);
  XSync(dpy, False);
  if (wm_running) {
    fprintf(stderr, "insight: another window manager is already running\n");
    return 1;
  }
  XSetErrorHandler(ignore_errors);   // windows disappear at any moment - that is normal

  A_WM_PROTOCOLS = XInternAtom(dpy, "WM_PROTOCOLS", False);
  A_WM_DELETE = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  A_WM_STATE = XInternAtom(dpy, "WM_STATE", False);
  A_NET_WM_NAME = XInternAtom(dpy, "_NET_WM_NAME", False);
  A_UTF8 = XInternAtom(dpy, "UTF8_STRING", False);
  A_NET_WM_TYPE = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  A_NET_WM_TYPE_DESKTOP = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
  A_NET_WM_TYPE_DOCK = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
  A_NET_SUPPORTED = XInternAtom(dpy, "_NET_SUPPORTED", False);
  A_NET_CHECK = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
  A_NET_ACTIVE = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
  A_APPEARANCE = XInternAtom(dpy, plat::APPEARANCE_ATOM, False);
  A_NET_WM_STATE = XInternAtom(dpy, "_NET_WM_STATE", False);
  A_NET_WM_STATE_SHADED = XInternAtom(dpy, "_NET_WM_STATE_SHADED", False);
  A_NET_WM_TYPE_DIALOG = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  A_NET_CLIENT_LIST = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
  A_NET_CLOSE = XInternAtom(dpy, "_NET_CLOSE_WINDOW", False);
  A_NET_WM_STATE_HIDDEN = XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", False);
  A_NET_WM_STATE_MAX_V = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
  A_NET_WM_STATE_MAX_H = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
  A_NET_WORKAREA = XInternAtom(dpy, "_NET_WORKAREA", False);
  A_NET_FRAME_EXTENTS = XInternAtom(dpy, "_NET_FRAME_EXTENTS", False);
  A_NET_REQUEST_FRAME_EXTENTS = XInternAtom(dpy, "_NET_REQUEST_FRAME_EXTENTS", False);
  A_NIRVANA_DOCK = XInternAtom(dpy, "_NIRVANA_DOCK", False);
  A_DESKTOP = XInternAtom(dpy, plat::DESKTOP_ATOM, False);
  read_window_mode();
  A_ICON_RECT = XInternAtom(dpy, "_NIRVANA_ICON_RECT", False);
  A_WM_CHANGE_STATE = XInternAtom(dpy, "WM_CHANGE_STATE", False);

  // Minimal EWMH: who the window manager is and what it supports.
  Window check = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
  XChangeProperty(dpy, root, A_NET_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check, 1);
  XChangeProperty(dpy, check, A_NET_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check, 1);
  XChangeProperty(dpy, check, A_NET_WM_NAME, A_UTF8, 8, PropModeReplace, (unsigned char *)"insight", 7);
  Atom supported[] = {A_NET_WM_NAME, A_NET_WM_TYPE, A_NET_WM_TYPE_DESKTOP, A_NET_WM_TYPE_DOCK,
                      A_NET_ACTIVE, A_NET_CHECK, A_NET_WM_STATE, A_NET_WM_STATE_SHADED,
                      A_NET_WM_TYPE_DIALOG, A_NET_CLIENT_LIST, A_NET_CLOSE,
                      A_NET_WM_STATE_HIDDEN, A_NET_WM_STATE_MAX_V, A_NET_WM_STATE_MAX_H,
                      A_NET_WORKAREA, A_NET_FRAME_EXTENTS, A_NET_REQUEST_FRAME_EXTENTS};
  XChangeProperty(dpy, root, A_NET_SUPPORTED, XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)supported, sizeof supported / sizeof supported[0]);

  read_dock_height();
  publish_workarea();

  set_root_cursor();
  replace_cursors();   // remembers the starting pointer
  xsettings_start();   // the pointer for GTK programs (when no other settings manager runs)
  set_root_background();   // the scheme's desktop color before Rambler comes up

  Fl::add_system_handler(x_handler, nullptr);
  Fl::add_timeout(3.0, preview_tick);

  // Alt+Tab and Alt+Shift+Tab - also with NumLock and CapsLock on
  KeyCode tab = XKeysymToKeycode(dpy, XK_Tab);
  for (unsigned extra : {0u, (unsigned)Mod2Mask, (unsigned)LockMask, (unsigned)(Mod2Mask | LockMask)})
    for (unsigned shift : {0u, (unsigned)ShiftMask})
      XGrabKey(dpy, tab, Mod1Mask | shift | extra, root, True, GrabModeAsync, GrabModeAsync);

  // screens: Win+M, Win+Shift+M, Win+(Shift+)1..9
  for (unsigned extra : {0u, (unsigned)Mod2Mask, (unsigned)LockMask, (unsigned)(Mod2Mask | LockMask)})
    for (unsigned shift : {0u, (unsigned)ShiftMask}) {
      XGrabKey(dpy, XKeysymToKeycode(dpy, XK_m), Mod4Mask | shift | extra, root, True, GrabModeAsync, GrabModeAsync);
      for (KeySym k = XK_1; k <= XK_9; k++)
        XGrabKey(dpy, XKeysymToKeycode(dpy, k), Mod4Mask | shift | extra, root, True, GrabModeAsync, GrabModeAsync);
    }
  {   // the screen shown before Reset Nirvana
    Atom type; int fmt; unsigned long n = 0, after; unsigned char *data = nullptr;
    if (XGetWindowProperty(dpy, root, XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False), 0, 1, False, XA_CARDINAL,
                           &type, &fmt, &n, &after, &data) == Success && data && n == 1)
      g_screen = (int)((long *)data)[0];
    if (data) XFree(data);
    g_screens = std::clamp(read_desktop().screens, 1, 9);
    g_screen = std::clamp(g_screen, 0, g_screens - 1);
    publish_screens();
  }

  // Take over the windows that already exist.
  // Iconified windows (WM_STATE = Iconic, e.g. after Reset Nirvana) come back as icons.
  publish_iconic();   // empty list - an old one could be left by the previous Insight
  Window r, p, *kids = nullptr;
  unsigned n = 0;
  if (XQueryTree(dpy, root, &r, &p, &kids, &n)) {
    animations_suspend(true);
    for (unsigned i = 0; i < n; i++) {
      XWindowAttributes wa;
      if (!XGetWindowAttributes(dpy, kids[i], &wa) || wa.override_redirect || kids[i] == check) continue;
      // the X server maps "save set" windows after the old Insight exits, so the hidden
      // state comes from WM_STATE, and the icon from our _NIRVANA_ICONIFIED mark
      bool iconic = false, icon = false;
      Atom type; int fmt; unsigned long cnt, after; unsigned char *data = nullptr;
      if (XGetWindowProperty(dpy, kids[i], A_WM_STATE, 0, 2, False, A_WM_STATE, &type, &fmt, &cnt, &after,
                             &data) == Success && data && cnt >= 1)
        iconic = ((long *)data)[0] == IconicState;
      if (data) { XFree(data); data = nullptr; }
      if (iconic && XGetWindowProperty(dpy, kids[i], XInternAtom(dpy, "_NIRVANA_ICONIFIED", False), 0, 1, False,
                                       AnyPropertyType, &type, &fmt, &cnt, &after, &data) == Success)
        icon = type != None;
      if (data) XFree(data);
      if (wa.map_state != IsViewable && !iconic) continue;
      manage(kids[i], true);
      if (Client *c = client_of(kids[i])) {
        if (icon) iconify_client(c);
        else if (iconic) hide_client(c);
      }
    }
    animations_suspend(false);
    if (kids) XFree(kids);
  }

  for (;;) Fl::wait(1e20);   // the loop also runs when there is no window at all
}
