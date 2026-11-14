// Nirvana Desktop - Rambler: the virtual screens in the menu bar
//
// Insight keeps the screens (EWMH _NET_NUMBER_OF_DESKTOPS / _NET_CURRENT_DESKTOP). Here:
//  - the screen gadget at the right end of the menu bar: two screens one behind the other,
//    the number of the shown one. A click shows the next screen, the right button lists
//    them all;
//  - pulling the screen down: dragging the gadget or the empty middle of the menu bar
//    downwards pulls the shown screen down like a sheet - the next screen is revealed above
//    it. Let go below a quarter of the height and the next screen stays; higher, the screen
//    springs back. (Without a compositor the pulled screen is its picture, taken when the
//    drag begins.)
#include "screens.h"
#include "i18n.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <unistd.h>
#include <vector>

using namespace plat;

namespace screens {

static long root_card(const char *name, long def) {
  Atom type; int fmt; unsigned long n = 0, after; unsigned char *data = nullptr;
  long v = def;
  if (XGetWindowProperty(fl_display, DefaultRootWindow(fl_display), XInternAtom(fl_display, name, False), 0, 1,
                         False, XA_CARDINAL, &type, &fmt, &n, &after, &data) == Success && data && n == 1)
    v = ((long *)data)[0];
  if (data) XFree(data);
  return v;
}

int count() { return (int)std::max(1L, root_card("_NET_NUMBER_OF_DESKTOPS", 1)); }
int current() { return (int)std::clamp(root_card("_NET_CURRENT_DESKTOP", 0), 0L, (long)count() - 1); }

void go(int s) {
  int n = count();
  s = ((s % n) + n) % n;
  XEvent ev{};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = DefaultRootWindow(fl_display);
  ev.xclient.message_type = XInternAtom(fl_display, "_NET_CURRENT_DESKTOP", False);
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = s;
  ev.xclient.data.l[1] = CurrentTime;
  XSendEvent(fl_display, DefaultRootWindow(fl_display), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(fl_display);
}

// --- pulling the screen down --------------------------------------------------------------
class Curtain : public Fl_Double_Window {
public:
  Fl_RGB_Image *img = nullptr;
  Curtain(int W, int H) : Fl_Double_Window(0, 0, W, H) { border(0); set_override(); }
  ~Curtain() override { delete img; }
  void draw() override {
    if (img) img->draw(0, 0);
    // the lower edge of the pulled screen: a dark line, like the edge of a sheet
    fl_color(FL_BLACK);
    fl_xyline(0, 0, w());
  }
};

static Curtain *g_curtain = nullptr;
static int g_from = 0, g_start_y = 0, g_dy = 0;
static bool g_pulling = false;

// the whole screen as a picture (the pulled sheet)
static Fl_RGB_Image *grab_screen(int &W, int &H) {
  Display *d = fl_display;
  Window root = DefaultRootWindow(d);
  XWindowAttributes wa;
  XGetWindowAttributes(d, root, &wa);
  W = wa.width;
  H = wa.height;
  XImage *im = XGetImage(d, root, 0, 0, W, H, AllPlanes, ZPixmap);
  if (!im) return nullptr;
  auto *rgb = new uchar[(size_t)W * H * 3];
  int rs = __builtin_ctzl(im->red_mask ? im->red_mask : 1), gs = __builtin_ctzl(im->green_mask ? im->green_mask : 1),
      bs = __builtin_ctzl(im->blue_mask ? im->blue_mask : 1);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      unsigned long p = XGetPixel(im, x, y);
      uchar *q = rgb + ((size_t)y * W + x) * 3;
      q[0] = (uchar)((p & im->red_mask) >> rs);
      q[1] = (uchar)((p & im->green_mask) >> gs);
      q[2] = (uchar)((p & im->blue_mask) >> bs);
    }
  XDestroyImage(im);
  auto *img = new Fl_RGB_Image(rgb, W, H, 3);
  img->alloc_array = 1;
  return img;
}

bool pulling() { return g_pulling; }

void pull_begin(int y_root) {
  g_pulling = false;
  g_start_y = y_root;
  g_dy = 0;
}

void pull_move(int y_root) {
  int dy = y_root - g_start_y;
  if (!g_pulling) {
    if (dy < 8 || count() < 2) return;   // a click, not a pull
    int W, H;
    Fl_RGB_Image *img = grab_screen(W, H);
    if (!img) return;
    g_pulling = true;
    g_from = current();
    // the scale of the interface: the window in FLTK units, the picture in pixels
    float sc = Fl::screen_scale(0);
    g_curtain = new Curtain((int)std::lround(W / sc), (int)std::lround(H / sc));
    if (sc != 1) {
      img->scale(g_curtain->w(), g_curtain->h(), 0, 1);
    }
    g_curtain->img = img;
    g_curtain->show();
    Fl::flush();
    XSync(fl_display, False);
    go(g_from + 1);   // the next screen comes up behind the sheet
  }
  g_dy = std::max(0, dy);
  g_curtain->position(0, g_dy);
  Fl::flush();
}

static void slide(int from, int to) {
  const int steps = 10;
  for (int i = 1; i <= steps; i++) {
    double t = (double)i / steps;
    t = 1 - (1 - t) * (1 - t);   // easing out
    g_curtain->position(0, (int)std::lround(from + (to - from) * t));
    Fl::flush();
    XSync(fl_display, False);
    usleep(12000);
  }
}

void pull_end() {
  if (!g_pulling) return;
  g_pulling = false;
  int H = g_curtain->h();
  if (g_dy > H / 4) {
    slide(g_dy, H);   // the pulled screen goes away below: the next one stays
  } else {
    slide(g_dy, 0);   // back: the screen springs up again
    go(g_from);
    XSync(fl_display, False);
    usleep(30000);
  }
  g_curtain->hide();
  Fl::delete_widget(g_curtain);
  g_curtain = nullptr;
}

// --- the gadget ---------------------------------------------------------------------------
class Gadget : public Fl_Widget {
public:
  Gadget(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {
    tooltip(_("Screens - a click shows the next one, the right button lists them; pull the menu bar down to reveal the next screen"));
  }
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_draw_box(MENUBAR_BOX, x(), y(), w(), h(), GRAY);
    if (pressed_) { fl_color(SEL); fl_rectf(x(), y(), w(), h() - 1); }
    // two screens, one behind the other; the front one with the number of the shown one
    int bx = x() + (w() - 18) / 2, by = y() + 3;
    Fl_Color ink = pressed_ ? SEL_TEXT : TEXT;
    fl_color(pressed_ ? SEL : GRAY); fl_rectf(bx + 5, by, 13, 10);
    fl_color(ink); fl_rect(bx + 5, by, 13, 10);
    fl_color(glossy_glyphs() ? fl_rgb_color(0x48, 0x68, 0xB0) : (pressed_ ? SEL : CONTENT));
    fl_rectf(bx, by + 4, 13, 10);
    fl_color(ink); fl_rect(bx, by + 4, 13, 10);
    fl_font(FONT_SYSTEM, 9);
    fl_color(glossy_glyphs() ? FL_WHITE : ink);
    std::string n = std::to_string(current() + 1);
    fl_draw(n.c_str(), bx, by + 4, 13, 10, FL_ALIGN_CENTER);
    fl_color(SHADOW); fl_yxline(x(), y() + 3, y() + h() - 5);
    fl_color(LIGHT);  fl_yxline(x() + 1, y() + 3, y() + h() - 5);
    fl_pop_clip();
  }
  int handle(int e) override {
    switch (e) {
    case FL_PUSH:
      if (Fl::event_button() == FL_RIGHT_MOUSE) { list(); return 1; }
      pressed_ = true;
      redraw();
      pull_begin(Fl::event_y_root());
      return 1;
    case FL_DRAG:
      pull_move(Fl::event_y_root());
      return 1;
    case FL_RELEASE:
      pressed_ = false;
      redraw();
      if (pulling()) pull_end();
      else if (Fl::event_inside(this)) go(current() + 1);
      return 1;
    case FL_ENTER: case FL_LEAVE: return 1;
    }
    return Fl_Widget::handle(e);
  }

private:
  bool pressed_ = false;
  void list() {
    int n = count(), cur = current();
    std::vector<std::string> labels;
    for (int i = 0; i < n; i++) labels.push_back(fmt(_("Screen %1"), std::to_string(i + 1)));
    std::vector<Fl_Menu_Item> items;
    for (int i = 0; i < n; i++) {
      Fl_Menu_Item it{};
      it.text = labels[i].c_str();
      it.flags = FL_MENU_RADIO | (i == cur ? FL_MENU_VALUE : 0);
      it.user_data_ = (void *)(long)i;
      items.push_back(it);
    }
    items.push_back(Fl_Menu_Item{});
    Fl_Menu_Button style(0, 0, 1, 1);
    style.textfont(FONT_SYSTEM);
    style.textsize(SIZE_SYSTEM);
    style.color(GRAY);
    style.selection_color(SEL);
    style.down_box(FL_FLAT_BOX);
    const Fl_Menu_Item *m = items.data()->popup(x(), y() + h(), nullptr, nullptr, &style);
    if (m) go((int)(long)m->user_data());
  }
};

Fl_Widget *make_gadget(int X, int Y, int W, int H) { return new Gadget(X, Y, W, H); }

}  // namespace screens
