// LisaOS - libplatinum: animacje okien w stylu klasycznego Mac OS
//
// Dwa sposoby rysowania konturów:
//
//  XOR    - kontury rysowane prosto na oknie głównym X, z blokadą serwera.
//           Najszybsze i najlżejsze, ale widoczne tylko na "gołym" Xorg/XLibre
//           (bez kompozytora) - czyli w sesji LisaOS z Insightem.
//
//  KSZTAŁT - tymczasowe okno override-redirect wycięte rozszerzeniem SHAPE
//           tak, że zostają z niego tylko linie konturów. Kompozytor pokazuje je
//           jak każde okno, więc działa pod XWaylandem (GNOME na Waylandzie,
//           np. Rocky/RHEL 10) i na pulpitach z kompozytorem (GNOME/KDE na X11).
//
// Wybór jest automatyczny; można go wymusić zmienną LISAOS_ANIM_MODE=xor|ksztalt.
#include "anim.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/platform.H>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#if defined(FLTK_USE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#endif

namespace plat {

static bool g_suspend = false;
void animations_suspend(bool s) { g_suspend = s; }

bool animations_enabled() {
  if (g_suspend) return false;
  // Zmienna środowiskowa ma pierwszeństwo przed ustawieniem z panelu Wygląd.
  if (const char *e = getenv("LISAOS_ANIMATIONS"))
    return !(!strcmp(e, "0") || !strcmp(e, "no") || !strcmp(e, "off"));
  return current_appearance().animations;
}

#if defined(FLTK_USE_X11)

static constexpr int STEPS = 12;   // liczba klatek
static constexpr int TRAIL = 4;    // ile konturów widać naraz ("ogon")
static int frame_ms() {
  // LISAOS_ANIM_MS - czas jednej klatki (do testów: np. 300 zamiast 12)
  const char *e = getenv("LISAOS_ANIM_MS");
  return e ? atoi(e) : 12;
}

// Przyspieszenie: kontury startują szybko i zwalniają przy celu.
static double ease(double t) { return 1.0 - (1.0 - t) * (1.0 - t); }

static Rect lerp(Rect a, Rect b, double t) {
  auto m = [t](int p, int q) { return (int)(p + (q - p) * t + 0.5); };
  return {m(a.x, b.x), m(a.y, b.y), m(a.w, b.w), m(a.h, b.h)};
}

static void wait_frame() {
  std::this_thread::sleep_for(std::chrono::milliseconds(frame_ms()));
}

// ---------------------------------------------------------------------------
// Wybór sposobu rysowania
// ---------------------------------------------------------------------------
enum class Mode { Xor, Ksztalt, Brak };   // (None to makro X11)

static Mode detect_mode(Display *d) {
  if (const char *m = getenv("LISAOS_ANIM_MODE")) {
    if (!strcmp(m, "xor")) return Mode::Xor;
    if (!strcmp(m, "ksztalt") || !strcmp(m, "shape")) return Mode::Ksztalt;
  }
  int ev, err, op;
  bool have_shape = XShapeQueryExtension(d, &ev, &err);
  // XWayland: okno główne X nie jest nigdzie wyświetlane
  bool xwayland = XQueryExtension(d, "XWAYLAND", &op, &ev, &err);
  // Kompozytor (EWMH): ktoś posiada selekcję _NET_WM_CM_Sn
  char name[32];
  snprintf(name, sizeof name, "_NET_WM_CM_S%d", DefaultScreen(d));
  bool composited = XGetSelectionOwner(d, XInternAtom(d, name, False)) != None;
  if (xwayland || composited) return have_shape ? Mode::Ksztalt : Mode::Brak;
  return Mode::Xor;
}

// ---------------------------------------------------------------------------
// XOR na oknie głównym
// ---------------------------------------------------------------------------
static void animate_xor(Display *d, const Rect *r) {
  Window root = DefaultRootWindow(d);
  XGCValues gv;
  gv.function = GXxor;
  gv.foreground = WhitePixel(d, DefaultScreen(d)) ^ BlackPixel(d, DefaultScreen(d));
  gv.subwindow_mode = IncludeInferiors;   // rysuj ponad wszystkimi oknami
  gv.line_width = 1;
  GC gc = XCreateGC(d, root, GCFunction | GCForeground | GCSubwindowMode | GCLineWidth, &gv);

  auto outline = [&](const Rect &q) {
    if (q.w > 1 && q.h > 1) XDrawRectangle(d, root, gc, q.x, q.y, q.w - 1, q.h - 1);
  };

  // Na czas animacji nikt inny nie rysuje - inaczej XOR mógłby zostawić ślady.
  // LISAOS_ANIM_NOGRAB=1 - bez blokady serwera (żeby zrobić zrzut w trakcie animacji)
  bool grab = !getenv("LISAOS_ANIM_NOGRAB");
  if (grab) XGrabServer(d);
  for (int i = 0; i <= STEPS + TRAIL; i++) {
    if (i <= STEPS) outline(r[i]);              // nowy kontur
    if (i >= TRAIL) outline(r[i - TRAIL]);      // zetrzyj najstarszy
    XFlush(d);
    wait_frame();
  }
  if (grab) XUngrabServer(d);
  XFreeGC(d, gc);
  XSync(d, False);
}

// ---------------------------------------------------------------------------
// Okno wycięte w kształt konturów (dla kompozytorów i XWaylanda)
// ---------------------------------------------------------------------------
static void animate_shape(Display *d, const Rect *r) {
  // obszar obejmujący wszystkie klatki
  int x0 = r[0].x, y0 = r[0].y, x1 = r[0].x + r[0].w, y1 = r[0].y + r[0].h;
  for (int i = 1; i <= STEPS; i++) {
    x0 = std::min(x0, r[i].x); y0 = std::min(y0, r[i].y);
    x1 = std::max(x1, r[i].x + r[i].w); y1 = std::max(y1, r[i].y + r[i].h);
  }
  if (x1 - x0 < 2 || y1 - y0 < 2) return;

  XSetWindowAttributes a;
  a.override_redirect = True;               // bez ramki, poza menedżerem okien
  a.background_pixel = BlackPixel(d, DefaultScreen(d));
  a.save_under = True;
  Window w = XCreateWindow(d, DefaultRootWindow(d), x0, y0, x1 - x0, y1 - y0, 0, CopyFromParent,
                           InputOutput, CopyFromParent,
                           CWOverrideRedirect | CWBackPixel | CWSaveUnder, &a);
  XStoreName(d, w, "lisaos-animacja");
  // kliknięcia przechodzą przez okno animacji
  XShapeCombineRectangles(d, w, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);

  std::vector<XRectangle> rects;
  auto add_outline = [&](const Rect &q) {
    if (q.w < 2 || q.h < 2) return;
    short x = (short)(q.x - x0), y = (short)(q.y - y0);
    unsigned short ww = (unsigned short)q.w, hh = (unsigned short)q.h;
    rects.push_back({x, y, ww, 1});                              // góra
    rects.push_back({x, (short)(y + hh - 1), ww, 1});            // dół
    rects.push_back({x, y, 1, hh});                              // lewo
    rects.push_back({(short)(x + ww - 1), y, 1, hh});            // prawo
  };

  bool mapped = false;
  for (int i = 0; i <= STEPS + TRAIL; i++) {
    rects.clear();
    for (int k = std::max(0, i - TRAIL + 1); k <= std::min(i, STEPS); k++) add_outline(r[k]);
    if (rects.empty()) {
      if (mapped) { XUnmapWindow(d, w); mapped = false; }
    } else {
      XShapeCombineRectangles(d, w, ShapeBounding, 0, 0, rects.data(), (int)rects.size(),
                              ShapeSet, Unsorted);
      if (!mapped) { XMapRaised(d, w); mapped = true; }
    }
    XFlush(d);
    wait_frame();
  }
  XDestroyWindow(d, w);
  XSync(d, False);
}

void zoom_rects(Rect from, Rect to) {
  if (!animations_enabled()) return;
  fl_open_display();
  Display *d = fl_x11_display();
  if (!d) return;                  // FLTK z backendem Wayland - animacje jeszcze nieobsługiwane
  static Mode mode = detect_mode(d);
  if (mode == Mode::Brak) return;

  from = px(from);   // rysujemy Xlib-em, w pikselach
  to = px(to);
  Rect r[STEPS + 1];
  for (int i = 0; i <= STEPS; i++) r[i] = lerp(from, to, ease((double)i / STEPS));
  if (mode == Mode::Xor) animate_xor(d, r);
  else animate_shape(d, r);
}

#else
void zoom_rects(Rect, Rect) {}
#endif

} // namespace plat
