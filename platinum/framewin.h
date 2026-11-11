// Nirvana Desktop - libplatinum: a window with a Platinum frame drawn by the program (CSD)
//
// Under Insight windows get their frame from the window manager. Under any other
// (GNOME, KDE, Openbox...) FrameWindow turns off the foreign decorations and draws the
// Platinum frame itself: a title bar with close and shade buttons, moving by the bar
// (through _NET_WM_MOVERESIZE, when the manager can do it), shading with a double click.
//
// Usage: build the content as in an ordinary window, in coordinates from (0,0),
// and after end() call finish() - with our own frame it moves the content into the
// frame and enlarges the window.
#pragma once

#include <FL/Fl_Double_Window.H>

namespace plat {

class FrameWindow : public Fl_Double_Window {
public:
  // dialog=true: a dialog window - no buttons in the bar, cannot be shaded
  FrameWindow(int W, int H, const char *title, bool dialog = false);
  ~FrameWindow() override;
  void finish();                       // after end(): the frame and the content offset
  // A resizable window: the "zoom" button in the bar and a corner to drag
  // (bottom right, the last 15 px of the content - the program draws draw_grow_box there).
  void sizable(Fl_Widget *content) { sizable_ = true; resizable(content); }
  bool csd() const { return csd_; }
  int ox() const { return csd_ ? FRAME_W : 0; }
  int oy() const { return csd_ ? TITLE_W : 0; }
  // position of the content on screen (for centering dialog windows)
  void place(int X, int Y) { position(X - ox(), Y - oy()); }
  int content_w() const { return w() - 2 * ox(); }
  int content_h() const { return full_h_ - oy() - ox(); }

  int handle(int e) override;
  void draw() override;

  static bool wm_is_insight();         // are the frames provided by Insight

private:
  static constexpr int FRAME_W = 5, TITLE_W = 20;   // = plat::FRAME, plat::TITLE_H
  bool active();
  int hit(int ex, int ey) const;
  void toggle_shade();
  void start_move();
  void start_moveresize(int direction);
  void toggle_zoom();
  bool in_grow(int ex, int ey) const;
  bool csd_, dialog_, shaded_ = false, sizable_ = false, zoomed_ = false;
  int full_h_ = 0, unzoom_[4] = {0, 0, 0, 0};
  enum { NONE, PENDING, MOVE, BTN, GROW } drag_ = NONE;
  int press_x_ = 0, press_y_ = 0, dx_ = 0, dy_ = 0, armed_ = 0, pressed_ = 0;
};

}  // namespace plat
