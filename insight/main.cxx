// LisaOS - lisaos-insight: menedżer okien X11 w stylu Platinum
//
// Insight to klasyczny "reparenting" window manager:
//   * każde okno aplikacji dostaje ramkę (okno FLTK rysowane przez libplatinum),
//   * aplikacja zostaje przeniesiona do wnętrza ramki (XReparentWindow),
//   * pasek tytułu obsługuje przesuwanie, zamykanie, zoom i windowshade,
//   * brak kompozytora - aplikacje rysują prosto na ekranie, co oszczędza pamięć.
//
// Okno biurka Ramblera (WM_CLASS "LisaOS-Desktop" lub _NET_WM_WINDOW_TYPE_DESKTOP)
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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "platinum.h"

using namespace plat;

static Display *dpy;
static Window root;
static int screen_w, screen_h;

static Atom A_WM_PROTOCOLS, A_WM_DELETE, A_WM_STATE, A_NET_WM_NAME, A_UTF8,
    A_NET_WM_TYPE, A_NET_WM_TYPE_DESKTOP, A_NET_WM_TYPE_DOCK, A_NET_SUPPORTED,
    A_NET_CHECK, A_NET_ACTIVE;

class Frame;

struct Client {
  Window win = 0;
  Frame *frame = nullptr;
  std::string title;
  bool shaded = false, zoomed = false;
  int full_h = 0;                 // wysokość ramki przed zwinięciem
  int unzoom[4] = {0, 0, 0, 0};
  int ignore_unmap = 0;           // UnmapNotify wywołane przez nas (reparent)
};

static std::map<Window, Client *> clients;   // okno aplikacji -> klient
static Window desktop_win = 0;                // okno biurka (bez ramki)
static Client *g_active = nullptr;

static void focus(Client *c);
static void unmanage(Client *c, bool destroyed);

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
  return "bez tytułu";
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
    if (ch.res_class && !strcmp(ch.res_class, "LisaOS-Desktop")) d = true;
    if (ch.res_name) XFree(ch.res_name);
    if (ch.res_class) XFree(ch.res_class);
  }
  return d || has_atom(w, A_NET_WM_TYPE, A_NET_WM_TYPE_DESKTOP);
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
    XMoveResizeWindow(dpy, c->win, FRAME, TITLE_H, std::max(1, cw()), std::max(1, ch()));
    XConfigureEvent ce{};
    ce.type = ConfigureNotify;
    ce.event = ce.window = c->win;
    ce.x = x() + FRAME; ce.y = y() + TITLE_H;
    ce.width = cw(); ce.height = ch();
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
    draw_titlebar(0, 0, w(), c->title.c_str(), act, pressed_);
    if (!c->shaded) {
      fl_color(act ? SHADOW : MID);
      fl_rect(FRAME - 1, TITLE_H - 1, cw() + 2, ch() + 2);
      // uchwyt zmiany rozmiaru na ramce w prawym dolnym rogu
      fl_color(act ? SHADOW : MID);
      for (int i = 0; i < 3; i++)
        fl_line(w() - 2 - i * 3, h() - FRAME + 1, w() - FRAME + 1 - i * 3 + 3, h() - 2);
    }
  }

  int hit_button(int ex, int ey) const {
    int by = (TITLE_H - BOX) / 2;
    if (ey < by || ey >= by + BOX) return 0;
    int zoom_x = w() - 8 - BOX, shade_x = zoom_x - BOX - 5;
    if (ex >= 8 && ex < 8 + BOX) return 1;
    if (ex >= zoom_x && ex < zoom_x + BOX) return 2;
    if (ex >= shade_x && ex < shade_x + BOX) return 3;
    return 0;
  }

  void toggle_shade() {
    if (!c->shaded) {
      c->full_h = h();
      c->shaded = true;
      size(w(), TITLE_H);          // okno aplikacji zostaje, ale ramka całkowicie je przycina
    } else {
      c->shaded = false;
      size(w(), c->full_h);
    }
    redraw();
  }

  void toggle_zoom() {
    if (c->shaded) toggle_shade();
    if (!c->zoomed) {
      c->unzoom[0] = x(); c->unzoom[1] = y(); c->unzoom[2] = w(); c->unzoom[3] = h();
      resize(4, MENUBAR_H + 4, screen_w - 100, screen_h - MENUBAR_H - 10);
      c->zoomed = true;
    } else {
      resize(c->unzoom[0], c->unzoom[1], c->unzoom[2], c->unzoom[3]);
      c->zoomed = false;
    }
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
        if (Fl::event_clicks()) { Fl::event_clicks(0); toggle_shade(); drag_ = NONE; return 1; }
        drag_ = MOVE;
      } else if (!c->shaded && (ex >= w() - 16 || ey >= h() - FRAME)) {
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
        else if (b == 3) toggle_shade();
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
static void focus(Client *c) {
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

  int x = wa.x, y = wa.y;
  if (!existing && x == 0 && y == 0) {
    static int cascade = 0;
    x = 60 + 26 * (cascade % 8);
    y = MENUBAR_H + 30 + 26 * (cascade % 8);
    cascade++;
  }
  y = std::max(y, MENUBAR_H);
  int fw = wa.width + 2 * FRAME, fh = wa.height + TITLE_H + FRAME;

  c->frame = new Frame(c, x, y, fw, fh);
  c->frame->xclass("LisaOS-Frame");
  c->frame->show();
  Window fx = fl_xid(c->frame);

  XAddToSaveSet(dpy, w);
  XSetWindowBorderWidth(dpy, w, 0);
  XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask);
  if (wa.map_state == IsViewable) c->ignore_unmap++;   // reparent wygeneruje UnmapNotify
  XReparentWindow(dpy, w, fx, FRAME, TITLE_H);
  XMapWindow(dpy, w);
  set_wm_state(w, NormalState);
  // klik w okno aplikacji ma je aktywować - przechwytujemy go synchronicznie
  XGrabButton(dpy, AnyButton, AnyModifier, w, False, ButtonPressMask, GrabModeSync,
              GrabModeAsync, None, None);
  clients[w] = c;
  c->frame->sync_client();
  focus(c);
}

static void unmanage(Client *c, bool destroyed) {
  if (!destroyed) {
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    XReparentWindow(dpy, c->win, root, c->frame->x() + FRAME, c->frame->y() + TITLE_H);
    XRemoveFromSaveSet(dpy, c->win);
    set_wm_state(c->win, WithdrawnState);
  }
  clients.erase(c->win);
  if (g_active == c) g_active = nullptr;
  c->frame->hide();
  Fl::delete_widget(c->frame);
  delete c;
  // fokus na najwyższe pozostałe okno
  if (!g_active) {
    Window r, p, *kids = nullptr;
    unsigned n = 0;
    if (XQueryTree(dpy, root, &r, &p, &kids, &n)) {
      for (int i = (int)n - 1; i >= 0 && !g_active; i--)
        for (auto &[w, cl] : clients)
          if (fl_xid(cl->frame) == kids[i] && !cl->shaded) { focus(cl); break; }
      if (kids) XFree(kids);
    }
  }
}

static Client *client_of(Window w) {
  auto it = clients.find(w);
  return it == clients.end() ? nullptr : it->second;
}

// Surowe zdarzenia X - obsługujemy te, które dotyczą okien aplikacji i korzenia.
static int x_handler(void *event, void *) {
  XEvent *e = (XEvent *)event;
  switch (e->type) {
  case MapRequest:
    if (Client *c = client_of(e->xmaprequest.window)) XMapWindow(dpy, c->win);
    else manage(e->xmaprequest.window, false);
    return 1;

  case ConfigureRequest: {
    XConfigureRequestEvent &r = e->xconfigurerequest;
    if (Client *c = client_of(r.window)) {
      Frame *f = c->frame;
      int nx = f->x(), ny = f->y(), nw = f->w(), nh = f->h();
      if (r.value_mask & CWX) nx = r.x - FRAME;
      if (r.value_mask & CWY) ny = std::max(MENUBAR_H, r.y - TITLE_H);
      if (r.value_mask & CWWidth) nw = r.width + 2 * FRAME;
      if (r.value_mask & CWHeight) nh = r.height + TITLE_H + FRAME;
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
      // klik w biurko: żadne okno nie jest aktywne
      Client *old = g_active;
      g_active = nullptr;
      if (old) old->frame->redraw();
      XSetInputFocus(dpy, desktop_win, RevertToPointerRoot, CurrentTime);
      XAllowEvents(dpy, ReplayPointer, CurrentTime);
      return 1;
    }
    return 0;

  case ClientMessage:
    if (e->xclient.message_type == A_NET_ACTIVE) {
      if (Client *c = client_of(e->xclient.window)) {
        if (c->shaded) c->frame->toggle_shade();
        focus(c);
      }
      return 1;
    }
    return 0;
  }
  return 0;
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--help")) {
      printf("lisaos-insight: menedżer okien LisaOS (X11)\n");
      return 0;
    }

  fl_open_display();
  dpy = fl_display;
  root = DefaultRootWindow(dpy);
  screen_w = DisplayWidth(dpy, DefaultScreen(dpy));
  screen_h = DisplayHeight(dpy, DefaultScreen(dpy));
  plat::init(false);

  // Zostań menedżerem okien: tylko jeden klient może przechwytywać
  // SubstructureRedirect na korzeniu.
  XSetErrorHandler(detect_wm);
  XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | PropertyChangeMask);
  XSync(dpy, False);
  if (wm_running) {
    fprintf(stderr, "insight: inny menedżer okien już działa\n");
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

  // Minimalne EWMH: kto jest menedżerem okien i co obsługuje.
  Window check = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
  XChangeProperty(dpy, root, A_NET_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check, 1);
  XChangeProperty(dpy, check, A_NET_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check, 1);
  XChangeProperty(dpy, check, A_NET_WM_NAME, A_UTF8, 8, PropModeReplace, (unsigned char *)"insight", 7);
  Atom supported[] = {A_NET_WM_NAME, A_NET_WM_TYPE, A_NET_WM_TYPE_DESKTOP, A_NET_WM_TYPE_DOCK,
                      A_NET_ACTIVE, A_NET_CHECK};
  XChangeProperty(dpy, root, A_NET_SUPPORTED, XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)supported, sizeof supported / sizeof supported[0]);

  XDefineCursor(dpy, root, XCreateFontCursor(dpy, XC_left_ptr));
  XSetWindowBackground(dpy, root, 0x5E6FA8);   // kolor biurka zanim wstanie Rambler
  XClearWindow(dpy, root);

  Fl::add_system_handler(x_handler, nullptr);

  // Przejmij okna, które już istnieją.
  Window r, p, *kids = nullptr;
  unsigned n = 0;
  if (XQueryTree(dpy, root, &r, &p, &kids, &n)) {
    for (unsigned i = 0; i < n; i++) {
      XWindowAttributes wa;
      if (XGetWindowAttributes(dpy, kids[i], &wa) && wa.map_state == IsViewable &&
          !wa.override_redirect && kids[i] != check)
        manage(kids[i], true);
    }
    if (kids) XFree(kids);
  }

  for (;;) Fl::wait(1e20);   // pętla działa także wtedy, gdy nie ma żadnego okna
}
