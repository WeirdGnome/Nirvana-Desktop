// Nirvana Desktop - libplatinum: window animations: flying outlines
//
// Two ways of drawing the outlines:
//
// XOR    - outlines drawn straight on the X root window, with the server grabbed.
// The fastest and lightest, but only visible on a "bare" Xorg/XLibre
// (without a compositor) - that is, in a Nirvana Desktop session with Insight.
//
// SHAPE  - a temporary override-redirect window cut with the SHAPE extension
// so that only the outline lines are left of it. The compositor shows it
// like any window, so it works under XWayland (GNOME on Wayland,
// e.g. Rocky/RHEL 10) and on desktops with a compositor (GNOME/KDE on X11).
//
// The choice is automatic; it can be forced with NIRVANA_ANIM_MODE=xor|shape.
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
  // The environment variable takes precedence over the Appearance panel setting.
  if (const char *e = getenv("NIRVANA_ANIMATIONS"))
    return !(!strcmp(e, "0") || !strcmp(e, "no") || !strcmp(e, "off"));
  return current_appearance().animations;
}

#if defined(FLTK_USE_X11)

static constexpr int STEPS = 12;   // number of frames
static constexpr int TRAIL = 4;    // how many outlines are visible at once (the "tail")
static int frame_ms() {
  // NIRVANA_ANIM_MS - duration of one frame (for testing: e.g. 300 instead of 12)
  const char *e = getenv("NIRVANA_ANIM_MS");
  return e ? atoi(e) : 12;
}

// Easing: the outlines start fast and slow down near the target.
static double ease(double t) { return 1.0 - (1.0 - t) * (1.0 - t); }

static Rect lerp(Rect a, Rect b, double t) {
  auto m = [t](int p, int q) { return (int)(p + (q - p) * t + 0.5); };
  return {m(a.x, b.x), m(a.y, b.y), m(a.w, b.w), m(a.h, b.h)};
}

static void wait_frame() {
  std::this_thread::sleep_for(std::chrono::milliseconds(frame_ms()));
}

// ---------------------------------------------------------------------------
// Choosing the drawing method
// ---------------------------------------------------------------------------
enum class Mode { Xor, Shape, Off };   // (None is an X11 macro)

static Mode detect_mode(Display *d) {
  if (const char *m = getenv("NIRVANA_ANIM_MODE")) {
    if (!strcmp(m, "xor")) return Mode::Xor;
    if (!strcmp(m, "shape")) return Mode::Shape;
  }
  int ev, err, op;
  bool have_shape = XShapeQueryExtension(d, &ev, &err);
  // XWayland: the X root window is not shown anywhere
  bool xwayland = XQueryExtension(d, "XWAYLAND", &op, &ev, &err);
  // Compositor (EWMH): someone owns the _NET_WM_CM_Sn selection
  char name[32];
  snprintf(name, sizeof name, "_NET_WM_CM_S%d", DefaultScreen(d));
  bool composited = XGetSelectionOwner(d, XInternAtom(d, name, False)) != None;
  if (xwayland || composited) return have_shape ? Mode::Shape : Mode::Off;
  return Mode::Xor;
}

// ---------------------------------------------------------------------------
// XOR on the root window
// ---------------------------------------------------------------------------
static void animate_xor(Display *d, const Rect *r) {
  Window root = DefaultRootWindow(d);
  XGCValues gv;
  gv.function = GXxor;
  gv.foreground = WhitePixel(d, DefaultScreen(d)) ^ BlackPixel(d, DefaultScreen(d));
  gv.subwindow_mode = IncludeInferiors;   // draw above all windows
  gv.line_width = 1;
  GC gc = XCreateGC(d, root, GCFunction | GCForeground | GCSubwindowMode | GCLineWidth, &gv);

  auto outline = [&](const Rect &q) {
    if (q.w > 1 && q.h > 1) XDrawRectangle(d, root, gc, q.x, q.y, q.w - 1, q.h - 1);
  };

  // Nobody else draws during the animation - otherwise XOR could leave traces.
  // NIRVANA_ANIM_NOGRAB=1 - without the server grab (to take a screenshot during the animation)
  bool grab = !getenv("NIRVANA_ANIM_NOGRAB");
  if (grab) XGrabServer(d);
  for (int i = 0; i <= STEPS + TRAIL; i++) {
    if (i <= STEPS) outline(r[i]);              // new outline
    if (i >= TRAIL) outline(r[i - TRAIL]);      // erase the oldest
    XFlush(d);
    wait_frame();
  }
  if (grab) XUngrabServer(d);
  XFreeGC(d, gc);
  XSync(d, False);
}

// ---------------------------------------------------------------------------
// A window cut to the shape of the outlines (for compositors and XWayland)
// ---------------------------------------------------------------------------
static void animate_shape(Display *d, const Rect *r) {
  // the area covering all frames
  int x0 = r[0].x, y0 = r[0].y, x1 = r[0].x + r[0].w, y1 = r[0].y + r[0].h;
  for (int i = 1; i <= STEPS; i++) {
    x0 = std::min(x0, r[i].x); y0 = std::min(y0, r[i].y);
    x1 = std::max(x1, r[i].x + r[i].w); y1 = std::max(y1, r[i].y + r[i].h);
  }
  if (x1 - x0 < 2 || y1 - y0 < 2) return;

  XSetWindowAttributes a;
  a.override_redirect = True;               // no frame, outside the window manager
  a.background_pixel = BlackPixel(d, DefaultScreen(d));
  a.save_under = True;
  Window w = XCreateWindow(d, DefaultRootWindow(d), x0, y0, x1 - x0, y1 - y0, 0, CopyFromParent,
                           InputOutput, CopyFromParent,
                           CWOverrideRedirect | CWBackPixel | CWSaveUnder, &a);
  XStoreName(d, w, "nirvana-animation");
  // clicks pass through the animation window
  XShapeCombineRectangles(d, w, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);

  std::vector<XRectangle> rects;
  auto add_outline = [&](const Rect &q) {
    if (q.w < 2 || q.h < 2) return;
    short x = (short)(q.x - x0), y = (short)(q.y - y0);
    unsigned short ww = (unsigned short)q.w, hh = (unsigned short)q.h;
    rects.push_back({x, y, ww, 1});                              // top
    rects.push_back({x, (short)(y + hh - 1), ww, 1});            // bottom
    rects.push_back({x, y, 1, hh});                              // left
    rects.push_back({(short)(x + ww - 1), y, 1, hh});            // right
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
  if (!d) return;                  // FLTK with the Wayland backend - animations not supported yet
  // checked every time: a compositor (nirvana-shader) can start or end during the session
  Mode mode = detect_mode(d);
  if (mode == Mode::Off) return;

  from = px(from);   // we draw with Xlib, in pixels
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
