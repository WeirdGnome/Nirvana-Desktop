// Nirvana Desktop - alice: the "Desktop" panel
//
// How much room windows leave on the desktop: the column of desktop icons with
// small gaps at the edges (the margins) and the dock at the bottom. Saved in
// ~/.config/nirvana/desktop.conf; Rambler and Insight follow at once.
// Also the window behavior: the "Classic 16-bit" windows (gadgets, click without raising,
// depth gadget) and moving with an outline.
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Round_Button.H>
#include <FL/fl_draw.H>

#include <algorithm>

using namespace plat;

namespace {

DesktopSettings g_d;
Fl_Check_Button *g_margin, *g_dock, *g_classic, *g_outline;
Fl_Choice *g_dsize, *g_screens, *g_tdclick;
Fl_Check_Button *g_float, *g_backdrop;
Fl_Round_Button *g_style_r, *g_style_e;

// Two overlapping windows in the chosen style: the active one in front.
class WindowSample : public Fl_Widget {
public:
  WindowSample(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}
  static void one(int X, int Y, int W, int H, const char *t, bool act) {
    if (classic_look(g_d.classic)) {
      draw_classic_frame(X, Y, W, H, act, false, true);
      draw_classic_titlebar(X, Y, W, t, act, 0, 1);
    } else {
      draw_frame(X, Y, W, H, act, false);
      draw_titlebar(X, Y, W, "", act, 0, 1);   // too narrow for a centered title
    }
    fl_color(CONTENT);
    fl_rectf(X + FRAME, Y + TITLE_H, W - 2 * FRAME, H - TITLE_H - FRAME);
    fl_color(DARK);
    fl_rect(X + FRAME - 1, Y + TITLE_H - 1, W - 2 * FRAME + 2, H - TITLE_H - FRAME + 2);
  }
  void draw() override {
    fl_color(DESK_A);
    fl_rectf(x(), y(), w(), h());
    fl_push_clip(x(), y(), w(), h());
    one(x() + 8, y() + 8, w() - 50, h() - 40, _("Documents"), false);
    one(x() + 40, y() + 32, w() - 50, h() - 40, _("Notes"), true);
    fl_pop_clip();
    fl_color(DARK);
    fl_rect(x(), y(), w(), h());
  }
};
WindowSample *g_sample;

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
    int dh = g_d.dock_size / 4 + 6;             // the dock (its size from the setting)
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
  g_d.screens = g_screens->value() + 1;
  g_d.dock_floating = g_float->value();
  g_d.backdrop = g_backdrop->value();
  g_d.title_dclick = g_tdclick->value();
  { static const int sizes[] = {24, 32, 48, 64}; int i = g_dsize->value(); g_d.dock_size = sizes[i >= 0 && i < 4 ? i : 1]; }
  g_d.classic = g_classic->value();
  g_d.outline = g_outline->value();
  g_d.folder_style = g_style_e->value() ? "explorer" : "rambler";
  write_desktop(g_d);
  notify_desktop_changed();
  g_preview->redraw();
  g_sample->redraw();
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
  const int W = 500, H = 694;
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
  auto *dsl = new Fl_Box(15, 200, 120, 22, _("Dock size:"));
  dsl->labelsize(SIZE_SMALL + 1);
  dsl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_dsize = new Fl_Choice(120, 200, 185, 22);
  g_dsize->labelsize(SIZE_SMALL + 1);
  g_dsize->textsize(SIZE_SMALL + 1);
  for (const char *n : {_("Small (24 px)"), _("Normal (32 px)"), _("Large (48 px)"), _("Very large (64 px)")}) {
    int k = g_dsize->add("x");   // add() treats '/' as a submenu - the text is set separately
    g_dsize->replace(k, n);
  }
  g_dsize->value(g_d.dock_size == 24 ? 0 : g_d.dock_size == 48 ? 2 : g_d.dock_size == 64 ? 3 : 1);
  g_dsize->callback(changed);
  auto *scl = new Fl_Box(320, 200, 60, 22, _("Screens:"));
  scl->labelsize(SIZE_SMALL + 1);
  scl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_screens = new Fl_Choice(385, 200, 60, 22);
  g_screens->textsize(SIZE_SMALL + 1);
  g_screens->add("1|2|3|4|5|6|7|8|9");
  g_screens->value(std::clamp(g_d.screens, 1, 9) - 1);
  g_screens->callback(changed);
  g_screens->tooltip(_("Virtual screens: Win+M shows the next one, Win+1..9 a chosen one, Win+Shift+number moves "
                       "the active window there; pulling the menu bar down reveals the next screen."));
  g_preview = new Preview(320, 38, 165, 110);
  note(320, 152, 165, 40, _("Preview of a zoomed window."));
  auto *sep = new Fl_Box(15, 234, W - 30, 2);
  sep->box(FL_ENGRAVED_BOX);
  auto *h2 = new Fl_Box(15, 244, 300, 20, _("Window Behavior"));
  h2->labelfont(FONT_SYSTEM);
  h2->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_classic = new Fl_Check_Button(15, 272, 290, 20, _("Classic 16-bit windows"));
  g_classic->labelsize(SIZE_SMALL + 1);
  g_classic->value(g_d.classic);
  g_classic->callback(changed);
  note(35, 294, 270, 112,
       _("Close gadget at the left, zoom and depth gadgets at the right, size gadget in the bottom right corner. "
         "A click activates a window without bringing it to the front - the depth gadget sends the top window "
         "to the back and brings any other to the front."));
  g_outline = new Fl_Check_Button(15, 410, 290, 20, _("Move and resize with an outline"));
  g_outline->labelsize(SIZE_SMALL + 1);
  g_outline->value(g_d.outline);
  g_outline->callback(changed);
  note(35, 432, 270, 30, _("The window jumps to its new place when the mouse button is released."));
  g_sample = new WindowSample(320, 272, 165, 150);
  auto *sep3 = new Fl_Box(15, 470, W - 30, 2);
  sep3->box(FL_ENGRAVED_BOX);
  auto *h4 = new Fl_Box(15, 478, 300, 20, _("Dock, Backdrop and Title Bars"));
  h4->labelfont(FONT_SYSTEM);
  h4->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_float = new Fl_Check_Button(15, 500, 230, 20, _("Floating dock in the middle"));
  g_float->labelsize(SIZE_SMALL + 1);
  g_float->value(g_d.dock_floating);
  g_float->callback(changed);
  g_float->tooltip(_("On: the dock floats centered above the bottom edge, with a grip and dividers between groups. "
                     "Off: a strip in the bottom left corner."));
  g_backdrop = new Fl_Check_Button(250, 500, 235, 20, _("Desktop as the backdrop"));
  g_backdrop->labelsize(SIZE_SMALL + 1);
  g_backdrop->value(g_d.backdrop);
  g_backdrop->callback(changed);
  g_backdrop->tooltip(_("Off: the desktop icons are in the window \"Nirvana Desktop\", moved and stacked like any "
                        "other (also Nirvana > Backdrop, Win+B)."));
  auto *tdl = new Fl_Box(15, 524, 200, 22, _("Double click on a title bar:"));
  tdl->labelsize(SIZE_SMALL + 1);
  tdl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_tdclick = new Fl_Choice(220, 524, 200, 22);
  g_tdclick->textsize(SIZE_SMALL + 1);
  for (const char *n : {_("Iconify to the desktop"), _("Zoom"), _("Shade (roll up)"), _("Depth (front / back)")}) {
    int k = g_tdclick->add("x");
    g_tdclick->replace(k, n);
  }
  g_tdclick->value(std::clamp(g_d.title_dclick, 0, 3));
  g_tdclick->callback(changed);
  auto *sep2 = new Fl_Box(15, 550, W - 30, 2);
  sep2->box(FL_ENGRAVED_BOX);
  auto *h3 = new Fl_Box(15, 558, 300, 20, _("Folder Windows"));
  h3->labelfont(FONT_SYSTEM);
  h3->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_style_r = new Fl_Round_Button(15, 582, W - 30, 20, _("Drawers: every folder in its own window, which remembers its place"));
  g_style_e = new Fl_Round_Button(15, 602, W - 30, 20, _("Rambler Advanced: folder tree, two panes, jobs, search"));
  for (auto *b : {g_style_r, g_style_e}) {
    b->type(FL_RADIO_BUTTON);
    b->labelsize(SIZE_SMALL + 1);
    b->callback(changed);
  }
  (g_d.folder_style == "explorer" ? g_style_e : g_style_r)->value(1);
  note(15, H - 66, W - 120, 30, _("The changes apply at once; the folder window style to the windows opened from now on."));
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
