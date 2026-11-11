// Nirvana Desktop - libplatinum: a Platinum style window
//
// PWindow is an FLTK group drawing the full window "chrome":
// a striped title bar, the close button, zoom, windowshade
// and the resize handle. In the prototype stage the windows lived inside the desktop
// (one process). nirvana-insight uses the same drawing functions to draw
// frames around real X11 windows.
#pragma once

#include <FL/Fl_Group.H>
#include <functional>

namespace plat {

class PWindow : public Fl_Group {
public:
  PWindow(int x, int y, int w, int h, const char *title);

  Fl_Group *content() { return content_; }

  void set_active(bool a) { if (active_ != a) { active_ = a; redraw(); } }
  bool is_active() const { return active_; }
  bool shaded() const { return shaded_; }
  void set_dialog(bool d) { dialog_ = d; redraw(); }   // no buttons and no resizing
  void toggle_shade();
  void toggle_zoom();

  // Signals to the window manager (desktop).
  std::function<void(PWindow *)> on_close;   // the close button was clicked
  std::function<void(PWindow *)> on_raise;   // the window was clicked
  std::function<void(PWindow *)> on_geometry;// moved / resized

  int handle(int e) override;
  void draw() override;
  void resize(int X, int Y, int W, int H) override;

  static constexpr int MIN_W = 200, MIN_H = 120;

private:
  enum Drag { NONE, MOVE, GROW, BUTTON };
  int hit_button(int ex, int ey) const;   // 0 none, 1 close, 2 zoom, 3 shade
  bool in_grow(int ex, int ey) const;
  void layout_content();

  Fl_Group *content_;
  bool active_ = true, shaded_ = false, zoomed_ = false, dialog_ = false;
  int full_h_;                        // height before shading
  int unzoom_[4] = {0, 0, 0, 0};       // geometry before zoom
  Drag drag_ = NONE;
  int drag_dx_ = 0, drag_dy_ = 0, armed_ = 0, pressed_ = 0;
};

} // namespace plat
