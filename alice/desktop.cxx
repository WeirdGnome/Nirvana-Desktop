// Nirvana Desktop - alice: the "Desktop" panel
//
// How much room windows leave on the desktop: the column of desktop icons with
// small gaps at the edges (the margins) and the dock at the bottom. Saved in
// ~/.config/nirvana/desktop.conf; Rambler and Insight follow at once.
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/fl_draw.H>

#include <algorithm>

using namespace plat;

namespace {

DesktopSettings g_d;
Fl_Check_Button *g_margin, *g_dock;

// A small screen: menu bar, icon column, dock and the zoomed window over them.
class Preview : public Fl_Widget {
public:
  Preview(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}
  void draw() override {
    int X = x(), Y = y(), W = w(), H = h();
    fl_color(DESK_A);
    fl_rectf(X, Y, W, H);
    fl_color(GRAY);
    fl_rectf(X, Y, W, 8);                      // menu bar
    fl_color(DARK);
    fl_xyline(X, Y + 8, X + W - 1);
    for (int i = 0; i < 3; i++) {              // desktop icons in the left column
      fl_color(LIGHT);
      fl_rectf(X + 6, Y + 16 + i * 22, 12, 10);
      fl_color(DARK);
      fl_rect(X + 6, Y + 16 + i * 22, 12, 10);
    }
    int dh = 14;                                // the dock
    fl_color(GRAY);
    fl_rectf(X + 2, Y + H - dh, 70, dh - 2);
    fl_color(DARK);
    fl_rect(X + 2, Y + H - dh, 70, dh - 2);
    int gap = g_d.icon_margin ? 4 : 0;
    int l = g_d.icon_margin ? 26 : 0, t = 9 + gap, r = W - gap, b = H - gap - (g_d.dock_room ? dh : 0);
    fl_color(GRAY);                             // the zoomed window
    fl_rectf(X + l, Y + t, r - l, b - t);
    fl_color(SEL);
    fl_rectf(X + l + 1, Y + t + 1, r - l - 2, 6);
    fl_color(CONTENT);
    fl_rectf(X + l + 2, Y + t + 8, r - l - 4, b - t - 10);
    fl_color(DARK);
    fl_rect(X + l, Y + t, r - l, b - t);
    fl_color(DARK);
    fl_rect(X, Y, W, H);
  }
};
Preview *g_preview;

void changed(Fl_Widget *, void *) {
  g_d.icon_margin = g_margin->value();
  g_d.dock_room = g_dock->value();
  write_desktop(g_d);
  notify_desktop_changed();
  g_preview->redraw();
}

Fl_Box *note(int X, int Y, int W, int H, const char *t) {
  auto *b = new Fl_Box(X, Y, W, H, t);
  b->labelsize(SIZE_SMALL);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  return b;
}

}  // namespace

int panel_desktop() {
  g_d = read_desktop();
  const int W = 500, H = 300;
  auto *win = new FrameWindow(W, H, _("Desktop"));
  win->xclass("nirvana-desktop-panel");
  auto *h = new Fl_Box(15, 10, 300, 20, _("Room for Windows"));
  h->labelfont(FONT_SYSTEM);
  h->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_margin = new Fl_Check_Button(15, 38, 290, 20, _("Keep the desktop icons free"));
  g_margin->labelsize(SIZE_SMALL + 1);
  g_margin->value(g_d.icon_margin);
  g_margin->callback(changed);
  note(35, 60, 270, 60,
       _("Zoomed and new windows leave the column of icons at the left edge free and keep small gaps at the edges. "
         "Off: windows use the whole screen below the menu bar."));
  g_dock = new Fl_Check_Button(15, 126, 290, 20, _("Keep windows above the dock"));
  g_dock->labelsize(SIZE_SMALL + 1);
  g_dock->value(g_d.dock_room);
  g_dock->callback(changed);
  note(35, 148, 270, 46, _("Off: zoomed windows reach the bottom edge and may cover the dock."));
  g_preview = new Preview(320, 38, 165, 110);
  note(320, 152, 165, 40, _("Preview of a zoomed window."));
  note(15, H - 72, W - 30, 30, _("The change applies at once, also to windows zoomed from now on."));
  auto *ok = new Fl_Button(W - 102, H - 36, 86, 24, _("OK"));
  ok->box(BUTTON_BOX);
  ok->down_box(BUTTON_DOWN);
  ok->labelfont(FONT_SYSTEM);
  ok->shortcut(FL_Enter);
  ok->callback([](Fl_Widget *w, void *) { w->window()->hide(); });
  win->end();
  win->finish();
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + win->oy(), (sh - H) / 4));
  win->show();
  return Fl::run();
}
