// Nirvana Desktop - alice: the "Desktop" panel
//
// How much room windows leave on the desktop: the column of desktop icons with
// small gaps at the edges (the margins) and the dock at the bottom. Saved in
// ~/.config/nirvana/desktop.conf; Rambler and Insight follow at once.
// Also the window behavior: the "Classic 16-bit" windows (gadgets, click without raising,
// depth gadget) and moving with an outline.
#include "framewin.h"
#include "nirvana_ui.h"
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

// A small wrapped explanation under a setting
Fl_Box *note(const char *t) {
  auto *b = new Fl_Box(0, 0, 300, 18, t);
  b->labelsize(SIZE_SMALL);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  return b;
}

Fl_Check_Button *check(const char *t, bool v) {
  auto *b = new Fl_Check_Button(0, 0, 290, 20, t);
  b->labelsize(SIZE_SMALL + 1);
  b->value(v);
  b->callback(changed);
  return b;
}

// "Label: [choice]" in a row
Fl_Choice *choice_row(const char *t, std::initializer_list<const char *> items, int value) {
  auto *row = new NirvanaHGroup(0, 0, 300, 22);
  auto *l = new Fl_Box(0, 0, 10, 22, t);
  l->labelsize(SIZE_SMALL + 1);
  l->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  auto *c = new Fl_Choice(0, 0, 100, 22);
  c->textsize(SIZE_SMALL + 1);
  for (const char *n : items) {
    int k = c->add("x");   // add() treats '/' as a submenu - the text is set separately
    c->replace(k, n);
  }
  c->value(value);
  c->callback(changed);
  row->weight(new NirvanaSpace, 1);
  row->end();
  return c;
}

}  // namespace

// The panel: a preferences window with tabs; every page is laid out by the Nirvana groups
int panel_desktop() {
  g_d = read_desktop();
  auto *win = new NirvanaPreferencesWindow(_("Desktop"), 520);
  win->xclass("nirvana-desktop-panel");

  // -- Windows -------------------------------------------------------------------------------
  win->add_tab(_("Windows"));
  {
    auto *room = new NirvanaHGroup(0, 0, 480, 100, _("Room for Windows"));
    room->spacing(12);
    {
      auto *col = new NirvanaVGroup(0, 0, 300, 100);
      col->spacing(3);
      g_margin = check(_("Keep the desktop icons free"), g_d.icon_margin);
      note(_("Zoomed and new windows leave the column of icons at the left edge free and keep small gaps at the edges. "
             "Off: windows use the whole screen below the menu bar."));
      g_dock = check(_("Keep windows above the dock"), g_d.dock_room);
      note(_("Off: zoomed windows reach the bottom edge and may cover the dock."));
      col->end();
      room->weight(col, 1);
      g_preview = new Preview(0, 0, 150, 110);
      room->fixed(g_preview, 150);
    }
    room->end();
    auto *beh = new NirvanaHGroup(0, 0, 480, 100, _("Window Behavior"));
    beh->spacing(12);
    {
      auto *col = new NirvanaVGroup(0, 0, 300, 100);
      col->spacing(3);
      g_classic = check(_("Classic 16-bit windows"), g_d.classic);
      note(_("Close gadget at the left, zoom and depth gadgets at the right, size gadget in the bottom right corner. "
             "A click activates a window without bringing it to the front - the depth gadget sends the top window "
             "to the back and brings any other to the front."));
      g_outline = check(_("Move and resize with an outline"), g_d.outline);
      note(_("The window jumps to its new place when the mouse button is released."));
      col->end();
      beh->weight(col, 1);
      g_sample = new WindowSample(0, 0, 150, 150);
      beh->fixed(g_sample, 150);
    }
    beh->end();
    g_tdclick = choice_row(_("Double click on a title bar:"),
                           {_("Iconify to the desktop"), _("Zoom"), _("Shade (roll up)"), _("Depth (front / back)")},
                           std::clamp(g_d.title_dclick, 0, 3));
  }
  win->end_tab();

  // -- Dock and Backdrop ----------------------------------------------------------------------
  win->add_tab(_("Dock and Backdrop"));
  {
    auto *dock = new NirvanaVGroup(0, 0, 480, 100, _("Dock"));
    dock->spacing(3);
    g_dsize = choice_row(_("Dock size:"),
                         {_("Small (24 px)"), _("Normal (32 px)"), _("Large (48 px)"), _("Very large (64 px)")},
                         g_d.dock_size == 24 ? 0 : g_d.dock_size == 48 ? 2 : g_d.dock_size == 64 ? 3 : 1);
    g_float = check(_("Floating dock in the middle"), g_d.dock_floating);
    note(_("On: the dock floats centered above the bottom edge, with a grip and dividers between groups. "
           "Off: a strip in the bottom left corner."));
    dock->end();
    auto *bd = new NirvanaVGroup(0, 0, 480, 100, _("Backdrop"));
    bd->spacing(3);
    g_backdrop = check(_("Desktop as the backdrop"), g_d.backdrop);
    note(_("Off: the desktop icons are in the window \"Nirvana Desktop\", moved and stacked like any "
           "other (also Nirvana > Backdrop, Win+B)."));
    bd->end();
  }
  win->end_tab();

  // -- Screens --------------------------------------------------------------------------------
  win->add_tab(_("Screens"));
  {
    auto *sc = new NirvanaVGroup(0, 0, 480, 100, _("Virtual Screens"));
    sc->spacing(3);
    g_screens = choice_row(_("Screens:"), {"1", "2", "3", "4", "5", "6", "7", "8", "9"}, std::clamp(g_d.screens, 1, 9) - 1);
    note(_("Virtual screens: Win+M shows the next one, Win+1..9 a chosen one, Win+Shift+number moves "
           "the active window there; pulling the menu bar down reveals the next screen."));
    sc->end();
  }
  win->end_tab();

  // -- Folder Windows -------------------------------------------------------------------------
  win->add_tab(_("Folder Windows"));
  {
    auto *fw = new NirvanaVGroup(0, 0, 480, 100, _("Folder Windows"));
    fw->spacing(3);
    g_style_r = new Fl_Round_Button(0, 0, 400, 20, _("Drawers: every folder in its own window, which remembers its place"));
    g_style_e = new Fl_Round_Button(0, 0, 400, 20, _("Rambler Advanced: folder tree, two panes, jobs, search"));
    for (auto *b : {g_style_r, g_style_e}) {
      b->type(FL_RADIO_BUTTON);
      b->labelsize(SIZE_SMALL + 1);
      b->callback(changed);
    }
    (g_d.folder_style == "explorer" ? g_style_e : g_style_r)->value(1);
    note(_("The changes apply at once; the folder window style to the windows opened from now on."));
    fw->end();
  }
  win->end_tab();

  win->prepare();   // sizes itself to the largest page
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  win->place(sx + (sw - win->content_w()) / 2, sy + std::max(MENUBAR_H + 10 + win->oy(), (sh - win->content_h()) / 4));
  win->show();
  return Fl::run();
}
