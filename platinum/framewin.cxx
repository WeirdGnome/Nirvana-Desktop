// LisaOS - libplatinum: okno z własną ramką Platinum
#include "framewin.h"
#include "anim.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#if defined(FLTK_USE_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

namespace plat {

#if defined(FLTK_USE_X11)
static Atom atom(const char *n) { return XInternAtom(fl_display, n, False); }

static Window prop_window(Window w, const char *name) {
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  Window r = 0;
  if (XGetWindowProperty(fl_display, w, atom(name), 0, 1, False, XA_WINDOW, &type, &fmt, &n, &after,
                         &data) == Success && data) {
    if (n) r = *(Window *)data;
    XFree(data);
  }
  return r;
}

static bool wm_supports(const char *what) {
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  bool r = false;
  if (XGetWindowProperty(fl_display, DefaultRootWindow(fl_display), atom("_NET_SUPPORTED"), 0, 1024, False,
                         XA_ATOM, &type, &fmt, &n, &after, &data) == Success && data) {
    Atom a = atom(what);
    for (unsigned long i = 0; i < n; i++) if (((Atom *)data)[i] == a) r = true;
    XFree(data);
  }
  return r;
}

// Wszystkie ramki odświeżamy, gdy zmienia się aktywne okno.
static std::set<FrameWindow *> g_frames;
static int watch(void *event, void *) {
  XEvent *e = (XEvent *)event;
  if (e->type == PropertyNotify && e->xproperty.window == DefaultRootWindow(fl_display) &&
      e->xproperty.atom == atom("_NET_ACTIVE_WINDOW"))
    for (auto *f : g_frames) f->redraw();
  return 0;
}
#endif

bool FrameWindow::wm_is_insight() {
#if defined(FLTK_USE_X11)
  fl_open_display();
  Window chk = prop_window(DefaultRootWindow(fl_display), "_NET_SUPPORTING_WM_CHECK");
  if (!chk) return false;
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  std::string name;
  if (XGetWindowProperty(fl_display, chk, atom("_NET_WM_NAME"), 0, 64, False, AnyPropertyType, &type, &fmt, &n,
                         &after, &data) == Success && data) {
    name.assign((char *)data, n);
    XFree(data);
  }
  return name == "insight";
#else
  return false;
#endif
}

FrameWindow::FrameWindow(int W, int H, const char *title, bool dialog)
    : Fl_Double_Window(W, H, title), dialog_(dialog) {
  const char *e = getenv("LISAOS_CSD");
  csd_ = e ? !strcmp(e, "1") : !wm_is_insight();
  if (csd_) border(0);   // bez obcych dekoracji - ramkę rysujemy sami
  full_h_ = H;
#if defined(FLTK_USE_X11)
  static bool watching = false;
  if (csd_ && !watching) {
    watching = true;
    XWindowAttributes wa;
    Window root = DefaultRootWindow(fl_display);
    XGetWindowAttributes(fl_display, root, &wa);
    XSelectInput(fl_display, root, wa.your_event_mask | PropertyChangeMask);
    Fl::add_system_handler(watch, nullptr);
  }
  g_frames.insert(this);
#endif
}

FrameWindow::~FrameWindow() {
#if defined(FLTK_USE_X11)
  g_frames.erase(this);
#endif
}

void FrameWindow::finish() {
  if (!csd_) return;
  for (int i = 0; i < children(); i++) {
    Fl_Widget *c = child(i);
    c->position(c->x() + FRAME_W, c->y() + TITLE_W);
  }
  int W = w() + 2 * FRAME_W, H = h() + TITLE_W + FRAME_W;
  // size() przeskalowałoby dzieci (resizable) - ustawiamy rozmiar bez tego
  Fl_Widget *r = resizable();
  resizable(nullptr);
  size(W, H);
  resizable(r);
  init_sizes();
  full_h_ = H;
}

bool FrameWindow::active() {
  if (!shown()) return false;
#if defined(FLTK_USE_X11)
  if (Window a = prop_window(DefaultRootWindow(fl_display), "_NET_ACTIVE_WINDOW")) return a == fl_xid(this);
#endif
  return Fl::first_window() == this;
}

void FrameWindow::draw() {
  if (!csd_) { Fl_Double_Window::draw(); return; }
  // Zmiana tylko w środku: odrysuj zmienione widgety, ramka zostaje w buforze.
  if (!(damage() & ~FL_DAMAGE_CHILD)) {
    for (int i = 0; i < children(); i++) update_child(*child(i));
    return;
  }
  bool act = active();
  draw_frame(0, 0, w(), h(), act, shaded_);
  draw_titlebar(0, 0, w(), label(), act, pressed_, dialog_ ? 0 : 2);
  if (shaded_) return;
  fl_color(color());
  fl_rectf(FRAME_W, TITLE_W, w() - 2 * FRAME_W, h() - TITLE_W - FRAME_W);
  for (int i = 0; i < children(); i++) {
    draw_child(*child(i));
    draw_outside_label(*child(i));   // np. "Hasło:" po lewej stronie pola
  }
}

// 1 zamknij, 3 zwiń (przycisku "zoom" panele nie mają)
int FrameWindow::hit(int ex, int ey) const {
  if (dialog_ || ey < 0 || ey >= TITLE_W) return 0;
  int by = (TITLE_W - BOX) / 2;
  if (ey < by || ey >= by + BOX) return 0;
  int close_x = 8, shade_x = w() - 8 - BOX;   // jak draw_titlebar(..., boxes = 2)
  if (ex >= close_x && ex < close_x + BOX) return 1;
  if (ex >= shade_x && ex < shade_x + BOX) return 3;
  return 0;
}

void FrameWindow::toggle_shade() {
  if (dialog_) return;
  Rect full = {x(), y(), w(), shaded_ ? full_h_ : h()}, bar = {x(), y(), w(), TITLE_W};
  if (shaded_) zoom_rects(bar, full); else zoom_rects(full, bar);
  Fl_Widget *r = resizable();
  resizable(nullptr);
  if (!shaded_) { full_h_ = h(); shaded_ = true; size(w(), TITLE_W); }
  else { shaded_ = false; size(w(), full_h_); }
  resizable(r);
  init_sizes();
  redraw();
}

void FrameWindow::start_move() {
#if defined(FLTK_USE_X11)
  XUngrabPointer(fl_display, CurrentTime);
  XEvent ev{};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = fl_xid(this);
  ev.xclient.message_type = atom("_NET_WM_MOVERESIZE");
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = press_x_;
  ev.xclient.data.l[1] = press_y_;
  ev.xclient.data.l[2] = 8;   // przesuń
  ev.xclient.data.l[3] = Button1;
  ev.xclient.data.l[4] = 1;
  XSendEvent(fl_display, DefaultRootWindow(fl_display), False,
             SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(fl_display);
#endif
}

int FrameWindow::handle(int e) {
  if (!csd_) return Fl_Double_Window::handle(e);
  int ex = Fl::event_x(), ey = Fl::event_y();
  switch (e) {
  case FL_PUSH:
    if (ey < TITLE_W && Fl::event_button() == FL_LEFT_MOUSE) {
      if (int b = hit(ex, ey)) {
        drag_ = BTN; armed_ = pressed_ = b; redraw();
        return 1;
      }
      if (Fl::event_clicks()) { Fl::event_clicks(0); toggle_shade(); return 1; }
      press_x_ = Fl::event_x_root(); press_y_ = Fl::event_y_root();
      dx_ = press_x_ - x(); dy_ = press_y_ - y();
      drag_ = PENDING;
      return 1;
    }
    break;
  case FL_DRAG:
    if (drag_ == PENDING) {
      if (abs(Fl::event_x_root() - press_x_) + abs(Fl::event_y_root() - press_y_) < 4) return 1;
#if defined(FLTK_USE_X11)
      if (wm_supports("_NET_WM_MOVERESIZE")) { drag_ = NONE; start_move(); return 1; }
#endif
      drag_ = MOVE;
    }
    if (drag_ == MOVE) { position(Fl::event_x_root() - dx_, std::max(0, Fl::event_y_root() - dy_)); return 1; }
    if (drag_ == BTN) {
      int p = hit(ex, ey) == armed_ ? armed_ : 0;
      if (p != pressed_) { pressed_ = p; redraw(); }
      return 1;
    }
    break;
  case FL_RELEASE:
    if (drag_ == BTN) {
      int b = pressed_;
      drag_ = NONE; pressed_ = armed_ = 0;
      redraw();
      if (b == 1) do_callback();   // jak zamknięcie przez menedżer okien
      if (b == 3) toggle_shade();
      return 1;
    }
    if (drag_ != NONE) { drag_ = NONE; return 1; }
    break;
  }
  return Fl_Double_Window::handle(e);
}

}  // namespace plat
