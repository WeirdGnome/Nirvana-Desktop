// Nirvana Desktop - nirvana-insight: menedżer okien X11 w stylu Platinum
//
// Insight to klasyczny "reparenting" window manager:
//   * każde okno aplikacji dostaje ramkę (okno FLTK rysowane przez libplatinum),
//   * aplikacja zostaje przeniesiona do wnętrza ramki (XReparentWindow),
//   * pasek tytułu obsługuje przesuwanie, zamykanie, zoom i windowshade,
//   * brak kompozytora - aplikacje rysują prosto na ekranie, co oszczędza pamięć.
//
// Okno biurka Ramblera (WM_CLASS "Nirvana-Desktop" lub _NET_WM_WINDOW_TYPE_DESKTOP)
// nie dostaje ramki i zawsze leży na samym dole.
//
// Działa na Xorg i XLibre (czysty Xlib, bez rozszerzeń).

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include <algorithm>
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

static Atom A_WYGLAD;
static Atom A_WM_PROTOCOLS, A_WM_DELETE, A_WM_STATE, A_NET_WM_NAME, A_UTF8,
    A_NET_WM_TYPE, A_NET_WM_TYPE_DESKTOP, A_NET_WM_TYPE_DOCK, A_NET_SUPPORTED,
    A_NET_CHECK, A_NET_ACTIVE, A_NET_WM_STATE, A_NET_WM_STATE_SHADED, A_NET_WM_TYPE_DIALOG,
    A_NET_CLIENT_LIST, A_NET_CLOSE, A_NET_WM_STATE_HIDDEN, A_WM_CHANGE_STATE,
    A_NET_WM_STATE_MAX_V, A_NET_WM_STATE_MAX_H, A_NET_WORKAREA, A_NET_FRAME_EXTENTS,
    A_NET_REQUEST_FRAME_EXTENTS, A_NIRVANA_DOK;

class Frame;

// Obszar roboczy (jednostki FLTK): ekran bez paska menu u góry i doku Ramblera
// na dole (_NIRVANA_DOK = wysokość doku, 0 gdy pusty). Okna programów go nie
// zasłaniają: przy otwarciu, przy maksymalizacji i przy zoomie.
static int g_dock_h = 0;
static Rect work_area() { return {0, MENUBAR_H, screen_w, screen_h - MENUBAR_H - g_dock_h}; }

static void read_dock_height() {
  Atom type;
  int fmt;
  unsigned long n, after;
  unsigned char *data = nullptr;
  g_dock_h = 0;
  if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), A_NIRVANA_DOK, 0, 1, False, XA_CARDINAL, &type, &fmt, &n,
                         &after, &data) == Success && data && n == 1 && fmt == 32)
    g_dock_h = std::clamp((int)*(long *)data, 0, screen_h / 3);
  if (data) XFree(data);
}

// _NET_WORKAREA (w pikselach) - z niego programy (Chromium, GTK, Qt) biorą
// rozmiar okna "zmaksymalizowanego" i pierwszego otwarcia.
static void publish_workarea() {
  Rect a = px(work_area());
  long wa[4] = {a.x, a.y, a.w, a.h};
  XChangeProperty(dpy, DefaultRootWindow(dpy), A_NET_WORKAREA, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)wa, 4);
  long one = 1, zero = 0, geo[2] = {px(screen_w), px(screen_h)};
  XChangeProperty(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False), XA_CARDINAL,
                  32, PropModeReplace, (unsigned char *)&one, 1);
  XChangeProperty(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False), XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&zero, 1);
  XChangeProperty(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "_NET_DESKTOP_GEOMETRY", False), XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)geo, 2);
}

// _NET_FRAME_EXTENTS: grubość ramki (lewo, prawo, góra, dół) w pikselach
static void set_frame_extents(Window w) {
  long ext[4] = {px(FRAME), px(FRAME), px(TITLE_H), px(FRAME)};
  XChangeProperty(dpy, w, A_NET_FRAME_EXTENTS, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)ext, 4);
}

// _NIRVANA_OBSZAR_OKIEN (x, y, szer., wys.) na oknie głównym - ustawia Rambler.
static bool rambler_area(plat::Rect &r) {
  Atom a = XInternAtom(dpy, "_NIRVANA_OBSZAR_OKIEN", False), type;
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
  int full_h = 0;                 // wysokość ramki przed zwinięciem
  int unzoom[4] = {0, 0, 0, 0};
  int ignore_unmap = 0;           // UnmapNotify wywołane przez nas (reparent)
  bool dialog = false;            // okno dialogowe: bez przycisków i zmiany rozmiaru
  bool own_anim = false;          // programy Nirvana Desktop same animują otwieranie/zamykanie
  bool hidden = false;            // ukryte (Ukryj / Ukryj inne w menu aplikacji)
  bool iconic = false;            // zikonizowane: ukryte, a na biurku jest jego ikona
  unsigned long mru = 0;          // kiedy ostatnio aktywne (do Alt+Tab)
  std::string cls;                // WM_CLASS - okna jednej aplikacji mają tę samą klasę
};

static std::map<Window, Client *> clients;   // okno aplikacji -> klient
static Window desktop_win = 0;                // okno biurka (bez ramki)
static Client *g_active = nullptr;

static void focus(Client *c);
static void unmanage(Client *c, bool destroyed);
static void iconify_client(Client *c);

// ---------------------------------------------------------------------------
// Pomocnicze funkcje Xlib
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

static std::string get_class(Window w) {
  XClassHint ch;
  std::string r;
  if (XGetClassHint(dpy, w, &ch)) {
    if (ch.res_class) { r = ch.res_class; XFree(ch.res_class); }
    if (ch.res_name) XFree(ch.res_name);
  }
  return r;
}

// _NET_WM_STATE: informujemy aplikację (np. Ramblera), czy okno jest zwinięte.
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

// ---------------------------------------------------------------------------
// Ramka okna
// ---------------------------------------------------------------------------
class Frame : public Fl_Double_Window {
public:
  Client *c;
  Frame(Client *cl, int X, int Y, int W, int H)
      : Fl_Double_Window(X, Y, W, H, "insight-ramka"), c(cl) {
    border(0);
    box(FL_NO_BOX);
    end();
  }

  // Obszar klienta wewnątrz ramki
  int cw() const { return w() - 2 * FRAME; }
  int ch() const { return (c->shaded ? c->full_h : h()) - TITLE_H - FRAME; }

  // Po każdej zmianie geometrii ramki dopasowujemy okno aplikacji
  // i wysyłamy mu syntetyczny ConfigureNotify (wymóg ICCCM).
  void sync_client() {
    // ramka jest w jednostkach FLTK, okno aplikacji w pikselach (skala interfejsu)
    int pw = px(w() - FRAME) - px(FRAME), ph = px(TITLE_H + ch()) - px(TITLE_H);
    XMoveResizeWindow(dpy, c->win, px(FRAME), px(TITLE_H), std::max(1, pw), std::max(1, ph));
    XConfigureEvent ce{};
    ce.type = ConfigureNotify;
    ce.event = ce.window = c->win;
    ce.x = px(x()) + px(FRAME); ce.y = px(y()) + px(TITLE_H);
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
    bool act = c == g_active;
    draw_frame(0, 0, w(), h(), act, c->shaded);
    draw_titlebar(0, 0, w(), c->title.c_str(), act, pressed_, !c->dialog);
    if (!c->shaded && !c->dialog) {
      fl_color(act ? SHADOW : MID);
      fl_rect(FRAME - 1, TITLE_H - 1, cw() + 2, ch() + 2);
      // uchwyt zmiany rozmiaru na ramce w prawym dolnym rogu
      fl_color(act ? SHADOW : MID);
      for (int i = 0; i < 3; i++)
        fl_line(w() - 2 - i * 3, h() - FRAME + 1, w() - FRAME + 1 - i * 3 + 3, h() - 2);
    }
  }

  int hit_button(int ex, int ey) const {
    if (c->dialog) return 0;
    int by = (TITLE_H - BOX) / 2;
    if (ey < by || ey >= by + BOX) return 0;
    int zoom_x = w() - 8 - BOX, shade_x = zoom_x - BOX - 5;
    if (ex >= 8 && ex < 8 + BOX) return 1;
    if (ex >= zoom_x && ex < zoom_x + BOX) return 2;
    if (ex >= shade_x && ex < shade_x + BOX) return 3;
    return 0;
  }

  Rect geom(int H = -1) const { return {x(), y(), w(), H < 0 ? h() : H}; }

  void toggle_shade() {
    Rect full = geom(c->shaded ? c->full_h : h()), bar = geom(TITLE_H);
    if (c->shaded) zoom_rects(bar, full); else zoom_rects(full, bar);
    if (!c->shaded) {
      c->full_h = h();
      c->shaded = true;
      size(w(), TITLE_H);          // okno aplikacji zostaje, ale ramka całkowicie je przycina
    } else {
      c->shaded = false;
      size(w(), c->full_h);
    }
    publish_state(c);
    redraw();
  }

  void toggle_zoom() {
    if (c->shaded) toggle_shade();
    if (!c->zoomed) {
      c->unzoom[0] = x(); c->unzoom[1] = y(); c->unzoom[2] = w(); c->unzoom[3] = h();
      // zoom zostawia widoczne ikony biurka (Nirvana HD, dyski, kosz, ikony okien)
      // i dok - obszar podaje Rambler; bez Ramblera: obszar roboczy
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
      ::focus(c);
      if (ey < TITLE_H) {
        if (int b = hit_button(ex, ey)) { drag_ = BTN; armed_ = pressed_ = b; redraw(); return 1; }
        if (Fl::event_clicks() && !c->dialog) { Fl::event_clicks(0); iconify_client(c); drag_ = NONE; return 1; }
        drag_ = MOVE;
      } else if (!c->shaded && !c->dialog && (ex >= w() - 16 || ey >= h() - FRAME)) {
        drag_ = GROW;          // prawa lub dolna krawędź / prawy dolny róg
      } else {
        drag_ = MOVE;          // lewa krawędź ramki
      }
      dx_ = Fl::event_x_root() - x(); dy_ = Fl::event_y_root() - y();
      gw_ = w() - ex; gh_ = h() - ey;
      return 1;
    case FL_DRAG:
      if (drag_ == MOVE) {
        int nx = Fl::event_x_root() - dx_;
        int ny = std::max(MENUBAR_H, Fl::event_y_root() - dy_);   // pasek menu zostaje widoczny
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
      if (drag_ == BTN) {
        int b = pressed_;
        pressed_ = armed_ = 0;
        drag_ = NONE;
        redraw();
        if (b == 1) close_client();
        else if (b == 2) toggle_zoom();
        else if (b == 3) iconify_client(c);   // ikona okna na biurku (jak w Workbenchu)
      }
      drag_ = NONE;
      return 1;
    }
    return Fl_Double_Window::handle(e);
  }

private:
  enum { NONE, MOVE, GROW, BTN } drag_ = NONE;
  int dx_ = 0, dy_ = 0, gw_ = 0, gh_ = 0, armed_ = 0, pressed_ = 0;
};

// ---------------------------------------------------------------------------
// Zarządzanie oknami
// ---------------------------------------------------------------------------
static std::vector<Window> client_order;   // kolejność pojawiania się (EWMH)
static unsigned long mru_clock = 0;

// _NET_CLIENT_LIST: lista okien dla przełącznika aplikacji (i innych programów)
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

// Najświeższe widoczne okno poza wskazanym - dostaje fokus po ukryciu/zamknięciu.
static Client *most_recent_visible(Client *except) {
  Client *best = nullptr;
  for (auto &[w, c] : clients)
    if (c != except && !c->hidden && (!best || c->mru > best->mru)) best = c;
  return best;
}

// _NIRVANA_IKONY na oknie głównym: zikonizowane okna (kolejność ikonizowania).
// Rambler rysuje z niej ikony okien na biurku; dwuklik = _NET_ACTIVE_WINDOW.
static std::vector<Window> g_iconic;
static void publish_iconic() {
  XChangeProperty(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "_NIRVANA_IKONY", False), XA_WINDOW, 32,
                  PropModeReplace, (unsigned char *)g_iconic.data(), (int)g_iconic.size());
}
static void set_iconic(Client *c, bool on) {
  if (c->iconic == on) return;
  c->iconic = on;
  // znacznik na samym oknie: po Resetuj Nirvanę nowy Insight wie, które ukryte
  // okna były ikonami, a które tylko "Ukryj"
  Atom mark = XInternAtom(dpy, "_NIRVANA_IKONA", False);
  long one = 1;
  if (on) XChangeProperty(dpy, c->win, mark, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&one, 1);
  else XDeleteProperty(dpy, c->win, mark);
  if (on) g_iconic.push_back(c->win);
  else g_iconic.erase(std::remove(g_iconic.begin(), g_iconic.end(), c->win), g_iconic.end());
  publish_iconic();
}

static void show_client(Client *c) {
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
  // Ramkę zdejmujemy surowym XUnmapWindow - hide() w FLTK zniszczyłoby okno X
  // ramki razem z oknem aplikacji w środku.
  XUnmapWindow(dpy, fl_xid(c->frame));
  set_wm_state(c->win, IconicState);
  publish_state(c);
  if (g_active == c) {
    g_active = nullptr;
    if (Client *n = most_recent_visible(c)) focus(n);
    else set_active_none();
  }
}

// Przycisk w pasku tytułu, dwuklik w pasek albo "minimalizuj" w programie:
// okno znika, a na biurku pojawia się jego ikona.
static void iconify_client(Client *c) {
  if (c->dialog || c->hidden) return;
  Rect r = c->frame->geom();
  set_iconic(c, true);
  hide_client(c);
  if (!c->own_anim) zoom_rects(r, center_of(r));
}

static void focus(Client *c) {
  if (c && c->hidden) show_client(c);
  if (c) c->mru = ++mru_clock;
  Client *old = g_active;
  g_active = c;
  if (old && old != c && clients.count(old->win)) old->frame->redraw();
  if (!c) return;
  XRaiseWindow(dpy, fl_xid(c->frame));
  XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
  Window w = c->win;
  XChangeProperty(dpy, root, A_NET_ACTIVE, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
  c->frame->redraw();
  if (desktop_win) XLowerWindow(dpy, desktop_win);
}

static void manage(Window w, bool existing) {
  if (clients.count(w) || w == desktop_win) return;
  XWindowAttributes wa;
  if (!XGetWindowAttributes(dpy, w, &wa) || wa.override_redirect) return;
  for (auto &[cw, c] : clients) if (fl_xid(c->frame) == w) return;   // nasze ramki
  if (fl_find(w)) return;                                          // inne okna FLTK insighta

  if (is_desktop(w)) {
    // Biurko: bez ramki, pełny ekran, zawsze na dole.
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
  c->own_anim = cls.rfind("Nirvana-", 0) == 0;
  Window tf = 0;
  c->dialog = (XGetTransientForHint(dpy, w, &tf) && tf) ||
              has_atom(w, A_NET_WM_TYPE, A_NET_WM_TYPE_DIALOG) || cls == "Nirvana-Dialog";

  int x = un(wa.x), y = un(wa.y);
  // StaticGravity (FLTK, wiele innych programów): podana pozycja dotyczy samego
  // okna aplikacji, więc ramkę stawiamy tak, żeby treść wylądowała dokładnie tam.
  XSizeHints sh;
  long supplied;
  if (XGetWMNormalHints(dpy, w, &sh, &supplied) && (sh.flags & PWinGravity) &&
      sh.win_gravity == StaticGravity && (x || y)) {
    x -= FRAME;
    y -= TITLE_H;
  }
  if (!existing && x == 0 && y == 0) {
    static int cascade = 0;
    x = 60 + 26 * (cascade % 8);
    y = MENUBAR_H + 30 + 26 * (cascade % 8);
    cascade++;
  }
  int fw = un(wa.width) + 2 * FRAME, fh = un(wa.height) + TITLE_H + FRAME;
  // okno mieści się w obszarze roboczym: pod paskiem menu i nad dokiem
  // (Chromium/Brave przy pierwszym starcie biorą rozmiar całego ekranu)
  Rect area = work_area();
  if (!c->dialog || fh > area.h) {
    fw = std::min(fw, area.w);
    fh = std::min(fh, area.h);
    x = std::clamp(x, area.x, std::max(area.x, area.x + area.w - fw));
    y = std::clamp(y, area.y, std::max(area.y, area.y + area.h - fh));
  }
  y = std::max(y, MENUBAR_H);

  // nowe okno "wylatuje" ze środka miejsca, w którym się pojawi
  if (!existing && !c->own_anim) zoom_rects(center_of({x, y, fw, fh}), {x, y, fw, fh});
  c->frame = new Frame(c, x, y, fw, fh);
  c->frame->xclass("Nirvana-Frame");
  c->frame->show();
  Window fx = fl_xid(c->frame);
  // FLTK oznacza swoje okna jako cele XDND. Ramka nim nie jest - inaczej pliki
  // przeciągane z innych programów trafiałyby w ramkę, a nie w okno aplikacji.
  XDeleteProperty(dpy, fx, XInternAtom(dpy, "XdndAware", False));

  XAddToSaveSet(dpy, w);
  XSetWindowBorderWidth(dpy, w, 0);
  XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask);
  if (wa.map_state == IsViewable) c->ignore_unmap++;   // reparent wygeneruje UnmapNotify
  XReparentWindow(dpy, w, fx, px(FRAME), px(TITLE_H));
  set_frame_extents(w);
  XMapWindow(dpy, w);
  set_wm_state(w, NormalState);
  // program chce startować zmaksymalizowany (np. Brave zapamiętał taki stan)
  if (!c->dialog && (has_atom(w, A_NET_WM_STATE, A_NET_WM_STATE_MAX_V) ||
                     has_atom(w, A_NET_WM_STATE, A_NET_WM_STATE_MAX_H)))
    c->frame->toggle_zoom();
  // klik w okno aplikacji ma je aktywować - przechwytujemy go synchronicznie
  XGrabButton(dpy, AnyButton, AnyModifier, w, False, ButtonPressMask, GrabModeSync,
              GrabModeAsync, None, None);
  clients[w] = c;
  client_order.push_back(w);
  publish_client_list();
  c->frame->sync_client();
  focus(c);
}

static void unmanage(Client *c, bool destroyed) {
  set_iconic(c, false);
  if (!destroyed) {
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    XReparentWindow(dpy, c->win, root, px(c->frame->x() + FRAME), px(c->frame->y() + TITLE_H));
    XRemoveFromSaveSet(dpy, c->win);
    set_wm_state(c->win, WithdrawnState);
  }
  clients.erase(c->win);
  client_order.erase(std::remove(client_order.begin(), client_order.end(), c->win), client_order.end());
  publish_client_list();
  if (g_active == c) g_active = nullptr;
  Rect gone = c->frame->geom();
  c->frame->hide();
  XSync(dpy, False);
  if (!c->own_anim) zoom_rects(gone, center_of(gone));   // zamykane okno "zapada się" do środka
  Fl::delete_widget(c->frame);
  delete c;
  // fokus na ostatnio używane widoczne okno
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
// Alt+Tab: przełączanie aplikacji (jak Cmd+Tab w Mac OS 9)
// ---------------------------------------------------------------------------
static bool cycling = false;
static std::vector<Client *> cycle_list;
static int cycle_idx = 0;

// Wyciąga na wierzch wszystkie okna aplikacji, a fokus daje wybranemu.
static void bring_app_forward(Client *c) {
  std::vector<Client *> same;
  for (auto &[w, o] : clients)
    if (o != c && o->cls == c->cls && !o->dialog) same.push_back(o);
  std::sort(same.begin(), same.end(), [](Client *a, Client *b) { return a->mru < b->mru; });
  for (Client *o : same) {
    if (o->hidden) show_client(o);
    XRaiseWindow(dpy, fl_xid(o->frame));
  }
  focus(c);
}

static void alt_tab(bool backwards) {
  if (!cycling) {
    // aplikacje od ostatnio używanej; każda reprezentowana przez najświeższe okno
    std::vector<Client *> all;
    for (auto &[w, c] : clients) if (!c->dialog) all.push_back(c);
    std::sort(all.begin(), all.end(), [](Client *a, Client *b) { return a->mru > b->mru; });
    cycle_list.clear();
    for (Client *c : all) {
      bool seen = false;
      for (Client *o : cycle_list) if (o->cls == c->cls) seen = true;
      if (!seen) cycle_list.push_back(c);
    }
    if (cycle_list.size() < 2) return;
    cycling = true;
    cycle_idx = 0;
    // klawiatura do nas, żeby zauważyć puszczenie Alt
    XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime);
  }
  int n = (int)cycle_list.size();
  cycle_idx = (cycle_idx + (backwards ? -1 : 1) + n) % n;
  Client *c = cycle_list[cycle_idx];
  if (clients.count(c->win)) bring_app_forward(c);
}

static void alt_tab_end() {
  if (!cycling) return;
  cycling = false;
  XUngrabKeyboard(dpy, CurrentTime);
}

// Panel Wygląd zmienił schemat: nowa paleta, tło głównego okna, ramki.
static void set_root_background() {
  uchar r, g, b;
  Fl::get_color(plat::DESK_A, r, g, b);
  XSetWindowBackground(dpy, root, ((unsigned long)r << 16) | (g << 8) | b);   // TrueColor
  XClearWindow(dpy, root);
}

static void appearance_changed() {
  plat::reload_appearance();
  set_root_background();
  for (auto &[w, c] : clients) c->frame->redraw();
}

// Surowe zdarzenia X - obsługujemy te, które dotyczą okien aplikacji i korzenia.
static int x_handler(void *event, void *) {
  XEvent *e = (XEvent *)event;
  switch (e->type) {
  case KeyPress: {
    KeySym ks = XLookupKeysym(&e->xkey, 0);
    if (ks == XK_Tab && (e->xkey.state & Mod1Mask)) {
      alt_tab(e->xkey.state & ShiftMask);
      return 1;
    }
    if (cycling && ks == XK_Escape) { alt_tab_end(); return 1; }
    return cycling ? 1 : 0;
  }
  case KeyRelease: {
    KeySym ks = XLookupKeysym(&e->xkey, 0);
    if (cycling && (ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L || ks == XK_Meta_R)) {
      alt_tab_end();
      return 1;
    }
    return cycling ? 1 : 0;
  }
  case MapRequest:
    if (e->xmaprequest.window == desktop_win) {
      // biurko pokazane ponownie (np. Rambler przestawił mu typ na DESKTOP)
      XMapWindow(dpy, desktop_win);
      XLowerWindow(dpy, desktop_win);
    } else if (Client *c = client_of(e->xmaprequest.window)) XMapWindow(dpy, c->win);
    else manage(e->xmaprequest.window, false);
    return 1;

  case ConfigureRequest: {
    XConfigureRequestEvent &r = e->xconfigurerequest;
    if (Client *c = client_of(r.window)) {
      Frame *f = c->frame;
      int nx = f->x(), ny = f->y(), nw = f->w(), nh = f->h();
      // prośba programu jest w pikselach, ramka w jednostkach FLTK
      if (r.value_mask & CWX) nx = un(r.x) - FRAME;
      if (r.value_mask & CWY) ny = std::max(MENUBAR_H, un(r.y) - TITLE_H);
      if (r.value_mask & CWWidth) nw = un(r.width) + 2 * FRAME;
      if (r.value_mask & CWHeight) nh = un(r.height) + TITLE_H + FRAME;
      Rect area = work_area();   // nie większe niż obszar roboczy, nie nad paskiem menu
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
    if (e->xproperty.window == root && e->xproperty.atom == A_NIRVANA_DOK) {
      read_dock_height();
      publish_workarea();
      return 0;
    }
    if (e->xproperty.window == root && e->xproperty.atom == A_WYGLAD) {
      appearance_changed();
      return 0;   // niech zobaczą to też inne obsługi
    }
    if (Client *c = client_of(e->xproperty.window)) {
      if (e->xproperty.atom == XA_WM_NAME || e->xproperty.atom == A_NET_WM_NAME) {
        c->title = get_title(c->win);
        c->frame->redraw();
      }
      return 1;
    }
    return 0;

  case ButtonPress:
    if (Client *c = client_of(e->xbutton.window)) {
      if (c != g_active) focus(c);
      XAllowEvents(dpy, ReplayPointer, CurrentTime);
      return 1;
    }
    if (desktop_win && e->xbutton.window == desktop_win) {
      // Pasek menu działa na aktywne okno - klik w niego niczego nie zmienia.
      if (e->xbutton.y_root < MENUBAR_H) {
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
        return 1;
      }
      // klik w biurko: żadne okno nie jest aktywne
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
      // Narzędzia > Resetuj Nirvanę: uruchom się od nowa. Okna klientów są w "save set",
      // więc po zamknięciu połączenia wracają do okna głównego, a nowy Insight
      // przejmuje je przy starcie (XQueryTree).
      char exe[4096];
      ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
      exe[n > 0 ? n : 0] = 0;   // prawdziwa ścieżka - inaczej proces nazywałby się "exe"
      XCloseDisplay(dpy);
      execv(n > 0 ? exe : g_argv[0], g_argv);
      _exit(1);
    }
    if (e->xclient.message_type == A_NET_WM_STATE) {
      // data.l[0]: 0 usuń, 1 dodaj, 2 przełącz; l[1], l[2]: właściwości
      Client *c = client_of(e->xclient.window);
      if (!c) return 0;
      long act = e->xclient.data.l[0];
      if ((Atom)e->xclient.data.l[1] == A_NET_WM_STATE_SHADED ||
          (Atom)e->xclient.data.l[2] == A_NET_WM_STATE_SHADED) {
        bool want = act == 2 ? !c->shaded : act == 1;
        if (want != c->shaded && !c->dialog) c->frame->toggle_shade();
      }
      // maksymalizacja (przycisk w pasku tytułu Chromium, Firefox...) = zoom
      Atom a1 = (Atom)e->xclient.data.l[1], a2 = (Atom)e->xclient.data.l[2];
      if (a1 == A_NET_WM_STATE_MAX_V || a1 == A_NET_WM_STATE_MAX_H || a2 == A_NET_WM_STATE_MAX_V ||
          a2 == A_NET_WM_STATE_MAX_H) {
        bool want = act == 2 ? !c->zoomed : act == 1;
        if (want != c->zoomed && !c->dialog) c->frame->toggle_zoom();
      }
      return 1;
    }
    if (e->xclient.message_type == A_NET_REQUEST_FRAME_EXTENTS) {
      // program pyta o ramkę jeszcze przed pokazaniem okna (GTK, Chromium)
      set_frame_extents(e->xclient.window);
      return 1;
    }
    if (e->xclient.message_type == A_NET_ACTIVE) {
      if (Client *c = client_of(e->xclient.window)) {
        if (c->shaded) c->frame->toggle_shade();
        focus(c);                 // ukryte okno zostaje też pokazane
      }
      return 1;
    }
    if (e->xclient.message_type == A_WM_CHANGE_STATE) {
      // ICCCM: aplikacja (lub przełącznik) prosi o ukrycie okna
      if (Client *c = client_of(e->xclient.window))
        if (e->xclient.data.l[0] == IconicState) {
          // "Ukryj" z menu aplikacji (gary) oznacza okno _NIRVANA_UKRYJ - bez ikony
          // na biurku; minimalizacja z samego programu tworzy ikonę.
          Atom mark = XInternAtom(dpy, "_NIRVANA_UKRYJ", False);
          Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
          bool plain = XGetWindowProperty(dpy, c->win, mark, 0, 1, True, AnyPropertyType, &type, &fmt, &n, &after,
                                          &data) == Success && type != None;
          if (data) XFree(data);
          if (plain) hide_client(c); else iconify_client(c);
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
  plat::init(false);   // ustawia też skalę interfejsu
  screen_w = un(DisplayWidth(dpy, DefaultScreen(dpy)));
  screen_h = un(DisplayHeight(dpy, DefaultScreen(dpy)));

  // Zostań menedżerem okien: tylko jeden klient może przechwytywać
  // SubstructureRedirect na korzeniu.
  XSetErrorHandler(detect_wm);
  XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | PropertyChangeMask);
  XSync(dpy, False);
  if (wm_running) {
    fprintf(stderr, "insight: another window manager is already running\n");
    return 1;
  }
  XSetErrorHandler(ignore_errors);   // okna znikają w dowolnym momencie - to normalne

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
  A_WYGLAD = XInternAtom(dpy, plat::APPEARANCE_ATOM, False);
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
  A_NIRVANA_DOK = XInternAtom(dpy, "_NIRVANA_DOK", False);
  A_WM_CHANGE_STATE = XInternAtom(dpy, "WM_CHANGE_STATE", False);

  // Minimalne EWMH: kto jest menedżerem okien i co obsługuje.
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

  XDefineCursor(dpy, root, XCreateFontCursor(dpy, XC_left_ptr));
  set_root_background();   // kolor biurka ze schematu, zanim wstanie Rambler

  Fl::add_system_handler(x_handler, nullptr);

  // Alt+Tab i Alt+Shift+Tab - także przy włączonym NumLocku i CapsLocku
  KeyCode tab = XKeysymToKeycode(dpy, XK_Tab);
  for (unsigned extra : {0u, (unsigned)Mod2Mask, (unsigned)LockMask, (unsigned)(Mod2Mask | LockMask)})
    for (unsigned shift : {0u, (unsigned)ShiftMask})
      XGrabKey(dpy, tab, Mod1Mask | shift | extra, root, True, GrabModeAsync, GrabModeAsync);

  // Przejmij okna, które już istnieją.
  // Okna zikonizowane (WM_STATE = Iconic, np. po Resetuj Nirvanę) wracają jako ikony.
  publish_iconic();   // pusta lista - stara mogła zostać po poprzednim Insighcie
  Window r, p, *kids = nullptr;
  unsigned n = 0;
  if (XQueryTree(dpy, root, &r, &p, &kids, &n)) {
    animations_suspend(true);
    for (unsigned i = 0; i < n; i++) {
      XWindowAttributes wa;
      if (!XGetWindowAttributes(dpy, kids[i], &wa) || wa.override_redirect || kids[i] == check) continue;
      // serwer X mapuje okna z "save set" po wyjściu starego Insighta, więc stan
      // ukrycia bierzemy z WM_STATE, a ikonę z naszego znacznika _NIRVANA_IKONA
      bool iconic = false, icon = false;
      Atom type; int fmt; unsigned long cnt, after; unsigned char *data = nullptr;
      if (XGetWindowProperty(dpy, kids[i], A_WM_STATE, 0, 2, False, A_WM_STATE, &type, &fmt, &cnt, &after,
                             &data) == Success && data && cnt >= 1)
        iconic = ((long *)data)[0] == IconicState;
      if (data) { XFree(data); data = nullptr; }
      if (iconic && XGetWindowProperty(dpy, kids[i], XInternAtom(dpy, "_NIRVANA_IKONA", False), 0, 1, False,
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

  for (;;) Fl::wait(1e20);   // pętla działa także wtedy, gdy nie ma żadnego okna
}
