// Nirvana Desktop - libplatinum: window animations: flying outlines
//
// "Zoom rects": when a window opens, closes, is iconified or zoomed, a series of
// rectangle outlines flies across the screen. The outlines are drawn with
// XOR straight on the X root window (IncludeInferiors), so no compositor is
// needed, and drawing the same outline a second time erases it exactly.
//
// Turning off: the environment variable NIRVANA_ANIMATIONS=0
// (or the switch in the "Appearance" control panel).
#pragma once

namespace plat {

struct Rect { int x, y, w, h; };

// Interface scale (HiDPI). Nirvana Desktop programs compute in FLTK units, X11
// in pixels: px() converts units to pixels, un() pixels to units.
// Only needed for direct Xlib calls (window manager, XEmbed,
// EWMH messages) - normal FLTK drawing scales by itself.
float ui_scale();
inline int px(int u) { float s = ui_scale(); return (int)(u * s + (u < 0 ? -0.5f : 0.5f)); }
inline int un(int p) { float s = ui_scale(); return (int)(p / s + (p < 0 ? -0.5f : 0.5f)); }
inline Rect px(Rect r) { return {px(r.x), px(r.y), px(r.x + r.w) - px(r.x), px(r.y + r.h) - px(r.y)}; }

bool animations_enabled();
void animations_suspend(bool s);   // e.g. when restoring windows after startup

// Animates the transition from rectangle `from` to `to` (screen coordinates).
// The call blocks for the time of the animation (~150 ms).
void zoom_rects(Rect from, Rect to);

// A small rectangle in the middle of the given one - the start/end point when there is no icon.
inline Rect center_of(Rect r, int size = 16) {
  return {r.x + r.w / 2 - size / 2, r.y + r.h / 2 - size / 2, size, size};
}

} // namespace plat
