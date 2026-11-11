// Nirvana Desktop - libplatinum: a Platinum style window
#include "pwindow.h"
#include "platinum.h"
#include "anim.h"

#include <FL/Fl_Window.H>

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <algorithm>

namespace plat {

PWindow::PWindow(int X, int Y, int W, int H, const char *title)
    : Fl_Group(X, Y, W, H), full_h_(H) {
  copy_label(title);
  align(FL_ALIGN_INSIDE);           // we draw the label ourselves in the title bar
  labeltype(FL_NO_LABEL);
  box(FL_NO_BOX);
  content_ = new Fl_Group(X + FRAME, Y + TITLE_H, W - 2 * FRAME, H - TITLE_H - FRAME);
  content_->end();
  end();
  resizable(content_);
}

void PWindow::layout_content() {
  int ch = (shaded_ ? full_h_ : h()) - TITLE_H - FRAME;
  content_->resize(x() + FRAME, y() + TITLE_H, w() - 2 * FRAME, ch);
}

// Our own layout: the content always fills the area inside the frame.
void PWindow::resize(int X, int Y, int W, int H) {
  Fl_Widget::resize(X, Y, W, H);
  layout_content();
}

int PWindow::hit_button(int ex, int ey) const {
  if (dialog_) return 0;
  int by = y() + (TITLE_H - BOX) / 2;
  if (ey < by || ey >= by + BOX) return 0;
  int close_x = x() + 8;
  int zoom_x = x() + w() - 8 - BOX;
  int shade_x = zoom_x - BOX - 5;
  if (ex >= close_x && ex < close_x + BOX) return 1;
  if (ex >= zoom_x && ex < zoom_x + BOX) return 2;
  if (ex >= shade_x && ex < shade_x + BOX) return 3;
  return 0;
}

bool PWindow::in_grow(int ex, int ey) const {
  if (shaded_ || dialog_) return false;
  int gx = x() + w() - FRAME - SCROLL_W, gy = y() + h() - FRAME - SCROLL_W;
  return ex >= gx && ex < gx + SCROLL_W && ey >= gy && ey < gy + SCROLL_W;
}

// Widget geometry in screen coordinates (for animations)
static Rect screen_rect(Fl_Widget *w, int X, int Y, int W, int H) {
  Fl_Window *win = w->window();
  int ox = win ? win->x_root() : 0, oy = win ? win->y_root() : 0;
  return {ox + X, oy + Y, W, H};
}

void PWindow::toggle_shade() {
  Rect full = screen_rect(this, x(), y(), w(), shaded_ ? full_h_ : h());
  Rect bar = screen_rect(this, x(), y(), w(), TITLE_H + 1);
  if (shaded_) zoom_rects(bar, full); else zoom_rects(full, bar);
  if (!shaded_) {
    full_h_ = h();
    shaded_ = true;
    content_->hide();
    size(w(), TITLE_H + 1);
  } else {
    shaded_ = false;
    size(w(), full_h_);
    content_->show();
  }
  if (parent()) parent()->redraw();
  if (on_geometry) on_geometry(this);
}

void PWindow::toggle_zoom() {
  if (shaded_) toggle_shade();
  Rect before = screen_rect(this, x(), y(), w(), h());
  if (!zoomed_) {
    unzoom_[0] = x(); unzoom_[1] = y(); unzoom_[2] = w(); unzoom_[3] = h();
    Fl_Widget *p = parent();
    int px = p ? p->x() : 0, pw = p ? p->w() : 800, ph = p ? p->h() : 600;
    zoom_rects(before, screen_rect(this, px + 12, MENUBAR_H + 8, pw - 120, ph - MENUBAR_H - 20));
    resize(px + 12, MENUBAR_H + 8, pw - 120, ph - MENUBAR_H - 20);
    zoomed_ = true;
  } else {
    zoom_rects(before, screen_rect(this, unzoom_[0], unzoom_[1], unzoom_[2], unzoom_[3]));
    resize(unzoom_[0], unzoom_[1], unzoom_[2], unzoom_[3]);
    zoomed_ = false;
  }
  if (parent()) parent()->redraw();
  if (on_geometry) on_geometry(this);
}

int PWindow::handle(int e) {
  int ex = Fl::event_x(), ey = Fl::event_y();
  switch (e) {
  case FL_PUSH: {
    if (on_raise) on_raise(this);
    if (Fl::event_button() != FL_LEFT_MOUSE) break;
    bool in_title = ey >= y() && ey < y() + TITLE_H;
    if (in_title) {
      if (int b = active_ ? hit_button(ex, ey) : 0) {
        drag_ = BUTTON; armed_ = pressed_ = b;
        redraw();
        return 1;
      }
      // a double click on the title bar shades the window
      if (Fl::event_clicks() && !dialog_) { Fl::event_clicks(0); toggle_shade(); drag_ = NONE; return 1; }
      drag_ = MOVE; drag_dx_ = ex - x(); drag_dy_ = ey - y();
      return 1;
    }
    if (in_grow(ex, ey)) {
      drag_ = GROW; drag_dx_ = x() + w() - ex; drag_dy_ = y() + h() - ey;
      return 1;
    }
    // the window frame can be used for moving too
    bool on_frame = ex < x() + FRAME || ex >= x() + w() - FRAME || ey >= y() + h() - FRAME;
    if (on_frame) { drag_ = MOVE; drag_dx_ = ex - x(); drag_dy_ = ey - y(); return 1; }
    break;
  }
  case FL_DRAG:
    if (drag_ == MOVE) {
      Fl_Widget *p = parent();
      int nx = ex - drag_dx_, ny = std::max(ey - drag_dy_, (p ? p->y() : 0) + MENUBAR_H);
      position(nx, ny);
      if (p) p->redraw();
      return 1;
    }
    if (drag_ == GROW) {
      int nw = std::max(MIN_W, ex + drag_dx_ - x());
      int nh = std::max(MIN_H, ey + drag_dy_ - y());
      size(nw, nh);
      zoomed_ = false;
      if (parent()) parent()->redraw();
      return 1;
    }
    if (drag_ == BUTTON) {
      int p = hit_button(ex, ey) == armed_ ? armed_ : 0;
      if (p != pressed_) { pressed_ = p; redraw(); }
      return 1;
    }
    break;
  case FL_RELEASE:
    if (drag_ == BUTTON) {
      int b = pressed_;
      drag_ = NONE; pressed_ = armed_ = 0;
      redraw();
      if (b == 1 && on_close) { on_close(this); return 1; }   // note: the window may no longer exist
      if (b == 2) toggle_zoom();
      if (b == 3) toggle_shade();
      return 1;
    }
    if (drag_ == MOVE || drag_ == GROW) {
      drag_ = NONE;
      if (on_geometry) on_geometry(this);
      return 1;
    }
    break;
  }
  return Fl_Group::handle(e);
}

void PWindow::draw() {
  draw_frame(x(), y(), w(), h(), active_, shaded_);
  draw_titlebar(x(), y(), w(), label(), active_, pressed_, !dialog_);
  if (shaded_) return;
  // a narrow sunken edge around the content
  fl_color(active_ ? SHADOW : MID);
  fl_rect(content_->x() - 1, content_->y() - 1, content_->w() + 2, content_->h() + 2);
  draw_children();
  if (!dialog_) draw_grow_box(x() + w() - FRAME - SCROLL_W, y() + h() - FRAME - SCROLL_W);
}

} // namespace plat
