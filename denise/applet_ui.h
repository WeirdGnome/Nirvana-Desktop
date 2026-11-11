// Nirvana Desktop - the widgets of the menu bar applet popups (volume, power, updates):
// a popup window below the icon, menu rows, separators, a Platinum slider.
#pragma once

#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Slider.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <functional>

namespace applets {

using namespace plat;

// ---------------------------------------------------------------------------
// A popup below a menu bar icon (like a menu, but with sliders)
// ---------------------------------------------------------------------------
class Popup : public Fl_Double_Window {
public:
  std::function<void()> on_close;
  Popup(int W, int H) : Fl_Double_Window(W, H) {
    border(0);
    set_override();
    color(GRAY);
    end();
    resizable(nullptr);   // resizing after building the content must not scale it
  }
  void open_at(int X, int Y) {
    int sx, sy, sw, sh;
    Fl::screen_xywh(sx, sy, sw, sh, X, Y);
    position(std::clamp(X, sx, sx + sw - w()), Y);
    show();
    Fl::grab(this);
  }
  void close() {
    if (Fl::grab() == this) Fl::grab(nullptr);
    hide();
    if (on_close) on_close();
  }
  std::function<void()> track_hover;   // set below - highlighting of the menu rows
  int handle(int e) override {
    if (e == FL_PUSH) {
      int ex = Fl::event_x(), ey = Fl::event_y();
      if (ex < 0 || ey < 0 || ex >= w() || ey >= h()) { close(); return 1; }
    }
    if ((e == FL_MOVE || e == FL_DRAG || e == FL_ENTER || e == FL_LEAVE) && track_hover) {
      int r = Fl_Double_Window::handle(e);
      track_hover();
      return r ? r : 1;
    }
    if (e == FL_KEYBOARD && Fl::event_key() == FL_Escape) { close(); return 1; }
    return Fl_Double_Window::handle(e);
  }
  void draw() override {
    // a frame as in the Platinum menu: black outline and shadow
    fl_color(GRAY);
    fl_rectf(0, 0, w(), h());
    fl_color(DARK);
    fl_rect(0, 0, w() - 1, h() - 1);
    fl_color(SHADOW);
    fl_yxline(w() - 1, 1, h() - 1);
    fl_xyline(1, h() - 1, w() - 1);
    fl_color(LIGHT);
    fl_xyline(1, 1, w() - 3);
    fl_yxline(1, 1, h() - 3);
    for (int i = 0; i < children(); i++) {
      draw_child(*child(i));
      draw_outside_label(*child(i));
    }
  }
};

// Fl_Choice closes its own menu with Fl::grab(0) - after it we restore the popup's grab.
class GrabChoice : public Fl_Choice {
public:
  GrabChoice(int X, int Y, int W, int H, const char *l = nullptr) : Fl_Choice(X, Y, W, H, l) {
    textfont(FONT_SMALL);
    textsize(SIZE_SMALL + 1);
    labelsize(SIZE_SMALL + 1);
  }
  int handle(int e) override {
    int r = Fl_Choice::handle(e);
    if ((e == FL_PUSH || e == FL_KEYBOARD) && window() && window()->shown()) Fl::grab(window());
    return r;
  }
};

// Platinum slider: groove, fill in the selection color, knob.
class PSlider : public Fl_Slider {
public:
  PSlider(int X, int Y, int W, int H) : Fl_Slider(X, Y, W, H) {
    type(FL_HOR_SLIDER);
    bounds(0, 100);
    step(1);
    box(FL_NO_BOX);
  }
  void draw() override {
    int tw = 11, th = h() - 2;
    double f = (value() - minimum()) / (maximum() - minimum());
    int tx = x() + (int)((w() - tw) * f);
    int gy = y() + h() / 2 - 3;
    fl_draw_box(WELL_BOX, x(), gy, w(), 6, active_r() ? CONTENT : GRAY);
    if (active_r()) { fl_color(SEL); fl_rectf(x() + 2, gy + 2, std::max(0, tx - x()), 2); }
    fl_draw_box(THUMB_BOX, tx, y() + 1, tw, th, active_r() ? HILITE : GRAY);
    fl_color(SHADOW);
    for (int i = 0; i < 3; i++) fl_yxline(tx + 3 + i * 2, y() + 5, y() + th - 4);
  }
  int handle(int e) override {
    if (e == FL_MOUSEWHEEL) {
      value(std::clamp(value() - Fl::event_dy() * 5, minimum(), maximum()));
      do_callback();
      redraw();
      return 1;
    }
    return Fl_Slider::handle(e);
  }
};

// An item as in a menu: text, highlight under the mouse, a click runs the action.
class MenuRow : public Fl_Button {
public:
  MenuRow(int X, int Y, int W, const char *l) : Fl_Button(X, Y, W, 20, l) {
    box(FL_FLAT_BOX);
    labelfont(FONT_SYSTEM);
    labelsize(SIZE_SYSTEM);
    align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  }
  // The popup sets the highlight from the mouse position: with the mouse
  // grabbed (Fl::grab) FL_LEAVE does not always arrive and rows stayed highlighted.
  void hover(bool h) { if (h != hover_) { hover_ = h; redraw(); } }
  int handle(int e) override {
    if (e == FL_ENTER || e == FL_LEAVE) return 1;
    return Fl_Button::handle(e);
  }
  void draw() override {
    bool hl = (hover_ || value()) && active_r();
    fl_color(hl ? SEL : GRAY);
    fl_rectf(x(), y(), w(), h());
    fl_font(labelfont(), labelsize());
    // inactive: halfway between the text and the background - readable in dark schemes too
    fl_color(!active_r() ? fl_color_average(TEXT, GRAY, 0.45f) : hl ? SEL_TEXT : TEXT);
    fl_draw(label(), x() + 14, y(), w() - 18, h(), FL_ALIGN_LEFT);
  }
private:
  bool hover_ = false;
};

// The row under the mouse highlighted, the others not.
inline void update_hover(Fl_Window *w) {
  int ex = Fl::event_x(), ey = Fl::event_y();
  bool inside = ex >= 0 && ey >= 0 && ex < w->w() && ey < w->h();
  for (int i = 0; i < w->children(); i++)
    if (auto *r = dynamic_cast<MenuRow *>(w->child(i)))
      r->hover(inside && ex >= r->x() && ex < r->x() + r->w() && ey >= r->y() && ey < r->y() + r->h());
}

class Sep : public Fl_Widget {
public:
  Sep(int X, int Y, int W) : Fl_Widget(X, Y, W, 6) {}
  void draw() override {
    fl_color(SHADOW); fl_xyline(x(), y() + 2, x() + w() - 1);
    fl_color(LIGHT); fl_xyline(x(), y() + 3, x() + w() - 1);
  }
};

inline Fl_Box *text_box(int X, int Y, int W, const char *t, bool bold = false, int H = 18) {
  auto *b = new Fl_Box(X, Y, W, H);
  b->copy_label(t);
  b->labelfont(bold ? FONT_SYSTEM : FONT_SMALL);
  b->labelsize(bold ? SIZE_SYSTEM : SIZE_SMALL + 1);
  b->labelcolor(TEXT);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  return b;
}

template <class F>
inline Fl_Check_Button *check(int X, int Y, int W, const char *t, bool v, F cb) {
  auto *c = new Fl_Check_Button(X, Y, W, 20, t);
  c->labelsize(SIZE_SMALL + 1);
  c->value(v);
  c->callback([](Fl_Widget *w, void *d) { (*(F *)d)(((Fl_Check_Button *)w)->value()); }, new F(cb));
  return c;
}

}  // namespace applets
