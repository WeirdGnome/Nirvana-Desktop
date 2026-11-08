// LisaOS - libplatinum: animacje okien w stylu klasycznego Mac OS
#include "anim.h"

#include <FL/Fl.H>
#include <FL/platform.H>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined(FLTK_USE_X11)
#include <X11/Xlib.h>
#endif

namespace plat {

static bool g_suspend = false;
void animations_suspend(bool s) { g_suspend = s; }

bool animations_enabled() {
  if (g_suspend) return false;
  const char *e = getenv("LISAOS_ANIMATIONS");
  return !(e && (!strcmp(e, "0") || !strcmp(e, "no") || !strcmp(e, "off")));
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

void zoom_rects(Rect from, Rect to) {
  if (!animations_enabled()) return;
  fl_open_display();
  Display *d = fl_x11_display();
  if (!d) return;                  // FLTK działa na Waylandzie - na razie bez animacji
  Window root = DefaultRootWindow(d);

  XGCValues gv;
  gv.function = GXxor;
  gv.foreground = WhitePixel(d, DefaultScreen(d)) ^ BlackPixel(d, DefaultScreen(d));
  gv.subwindow_mode = IncludeInferiors;   // rysuj ponad wszystkimi oknami
  gv.line_width = 1;
  GC gc = XCreateGC(d, root, GCFunction | GCForeground | GCSubwindowMode | GCLineWidth, &gv);

  Rect r[STEPS + 1];
  for (int i = 0; i <= STEPS; i++) r[i] = lerp(from, to, ease((double)i / STEPS));

  auto outline = [&](const Rect &q) {
    if (q.w > 1 && q.h > 1) XDrawRectangle(d, root, gc, q.x, q.y, q.w - 1, q.h - 1);
  };

  // Na czas animacji nikt inny nie rysuje - inaczej XOR mógłby zostawić ślady.
  // LISAOS_ANIM_NOGRAB=1 - bez blokady serwera (żeby zrobić zrzut w trakcie animacji)
  bool grab = !getenv("LISAOS_ANIM_NOGRAB");
  if (grab) XGrabServer(d);
  int ms = frame_ms();
  for (int i = 0; i <= STEPS + TRAIL; i++) {
    if (i <= STEPS) outline(r[i]);              // nowy kontur
    if (i >= TRAIL) outline(r[i - TRAIL]);      // zetrzyj najstarszy
    XFlush(d);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }
  if (grab) XUngrabServer(d);
  XFreeGC(d, gc);
  XSync(d, False);
}

#else
void zoom_rects(Rect, Rect) {}
#endif

} // namespace plat
