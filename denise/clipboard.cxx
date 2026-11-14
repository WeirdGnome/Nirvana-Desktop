// Nirvana Desktop - the clipboard applet in the menu bar
//
// Remembers the texts copied in any program (the CLIPBOARD selection; FLTK reports each
// change through XFixes) - the last 25, newest first, the same text only once. A click on
// the icon, or Ctrl+Alt+V anywhere, shows the history; choosing an entry puts it back
// into the clipboard, ready for Ctrl+V. "Pause" stops recording (e.g. while copying
// passwords), "Clear History" forgets everything and empties the clipboard.
//
// The history lives only in memory: it is gone after logging out and never lands on disk.
// We do not take over the clipboard after every copy - the program that copied keeps it
// with all its formats (formatted text, pictures); only the text is remembered.
#include "applets.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

using namespace plat;

namespace applets {

namespace {

const size_t MAX_ITEMS = 25;
const size_t MAX_BYTES = 1 << 20;   // a longer text is not remembered (a whole file copied as text)

std::deque<std::string> g_hist;     // newest first
bool g_paused = false;
std::string g_current;              // what is in the clipboard now (as far as we know)
class ClipMenu;
ClipMenu *g_clip;
KeyCode g_key_v = 0;

void draw_clipboard(int x, int y, Fl_Color c, Fl_Color paper, bool full) {
  // a board with a clip at the top and a sheet with lines (12x14)
  if (glossy_glyphs()) {   // a brown board, a white sheet with blue lines, a silver clip
    glyph_color(0xB0743A, c); fl_rectf(x, y + 1, 12, 13);
    glyph_color(0xFFFFFF, c); fl_rectf(x + 2, y + 3, 8, 9);
    if (full) {
      glyph_color(0x3A7CE0, c);
      for (int i = 0; i < 3; i++) fl_xyline(x + 3, y + 5 + i * 2, x + (i == 2 ? 6 : 8));
    }
    glyph_color(0xC8CCD4, c); fl_rectf(x + 3, y, 6, 3);
    glyph_color(0x000000, c);
    fl_rect(x, y + 1, 12, 13);
    fl_rect(x + 3, y, 6, 3);
    return;
  }
  fl_color(c);
  fl_rect(x, y + 1, 12, 13);
  fl_color(paper);
  fl_rectf(x + 2, y + 3, 8, 9);
  fl_color(c);
  fl_rectf(x + 3, y, 6, 3);        // the clip
  if (full)
    for (int i = 0; i < 3; i++) fl_xyline(x + 3, y + 5 + i * 2, x + (i == 2 ? 6 : 8));
}

// One line for the menu: whitespace squeezed, at most ~44 characters (UTF-8 safe)
std::string preview(const std::string &s) {
  std::string o;
  bool sp = false;
  for (unsigned char ch : s) {
    if (ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') { sp = !o.empty(); continue; }
    if (sp) { o += ' '; sp = false; }
    if (ch == '&') o += "&&";       // FLTK menu shortcut character
    else o += (char)ch;
    if (o.size() > 120) break;
  }
  int n = 0;
  size_t i = 0;
  for (; i < o.size(); i++) {
    if (((unsigned char)o[i] & 0xC0) != 0x80) n++;
    if (n > 44) break;
  }
  if (i < o.size()) o = o.substr(0, i) + "…";
  return o.empty() ? std::string(_("(spaces)")) : o;
}

void remember(const std::string &t) {
  if (t.empty() || t.size() > MAX_BYTES) return;
  g_current = t;
  if (g_paused) return;
  auto it = std::find(g_hist.begin(), g_hist.end(), t);
  if (it != g_hist.end()) g_hist.erase(it);
  g_hist.push_front(t);
  while (g_hist.size() > MAX_ITEMS) g_hist.pop_back();
}

class ClipMenu : public Fl_Menu_Button {
public:
  std::vector<std::string> labels_;
  std::vector<Fl_Menu_Item> items_;

  ClipMenu(int X, int Y, int W, int H) : Fl_Menu_Button(X, Y, W, H) {
    box(FL_FLAT_BOX);
    down_box(FL_FLAT_BOX);
    textfont(FONT_SYSTEM);
    textsize(SIZE_SYSTEM);
    selection_color(SEL);
    tooltip(_("Clipboard history (Ctrl+Alt+V)"));
  }

  void build() {
    labels_.clear();
    items_.clear();
    labels_.reserve(MAX_ITEMS + 8);
    struct It { std::string l; Fl_Callback *cb; long v; int flags; };
    std::vector<It> its;
    its.push_back({g_paused ? _("Clipboard History (paused)") : _("Clipboard History"), nullptr, 0, FL_MENU_INACTIVE});
    if (g_hist.empty()) its.push_back({_("Nothing copied yet"), nullptr, 0, FL_MENU_INACTIVE});
    for (size_t i = 0; i < g_hist.size(); i++) {
      int f = FL_MENU_RADIO | (g_hist[i] == g_current ? FL_MENU_VALUE : 0);
      its.push_back({preview(g_hist[i]), pick_cb, (long)i, f});
    }
    its.back().flags |= FL_MENU_DIVIDER;
    its.push_back({_("Pause Recording"), pause_cb, 0, FL_MENU_TOGGLE | (g_paused ? FL_MENU_VALUE : 0)});
    its.push_back({_("Clear History"), clear_cb, 0, g_hist.empty() ? FL_MENU_INACTIVE : 0});
    for (auto &i : its) {
      labels_.push_back(i.l);
      Fl_Menu_Item m{};
      m.text = labels_.back().c_str();
      m.callback_ = i.cb;
      m.user_data_ = (void *)i.v;
      m.flags = i.flags;
      m.labelfont_ = FONT_SYSTEM;
      m.labelsize_ = SIZE_SYSTEM;
      items_.push_back(m);
    }
    items_.push_back(Fl_Menu_Item{});
    menu(items_.data());
  }

  static void pick_cb(Fl_Widget *, void *v) {
    size_t i = (size_t)(long)v;
    if (i >= g_hist.size()) return;
    std::string t = g_hist[i];
    g_hist.erase(g_hist.begin() + (long)i);
    g_hist.push_front(t);
    g_current = t;
    Fl::copy(t.data(), (int)t.size(), 1);   // also to the clipboard of Ctrl+V...
    Fl::copy(t.data(), (int)t.size(), 0);   // ...and to the middle button selection
    if (g_clip) g_clip->redraw();
  }
  static void pause_cb(Fl_Widget *, void *) {
    g_paused = !g_paused;
    if (g_clip) g_clip->redraw();
  }
  static void clear_cb(Fl_Widget *, void *) {
    g_hist.clear();
    g_current.clear();
    Fl::copy("", 0, 1);   // the clipboard empty too: what was copied last does not stay
    if (g_clip) g_clip->redraw();
  }

  // the history at the mouse pointer (Ctrl+Alt+V)
  void popup_here() {
    build();
    int mx, my;
    Fl::get_mouse(mx, my);
    // styled like the desktop context menus: a raised box with a frame (not the flat menu bar look)
    static Fl_Menu_Button *style = nullptr;
    if (!style) {
      Fl_Group *cur = Fl_Group::current();
      Fl_Group::current(window());   // in the desktop window: menu coordinates relative to it
      style = new Fl_Menu_Button(0, 0, 1, 1);
      style->hide();
      style->textfont(FONT_SYSTEM);
      style->textsize(SIZE_SYSTEM);
      style->color(GRAY);
      style->down_box(FL_FLAT_BOX);
      style->selection_color(SEL);
      Fl_Group::current(cur);
    }
    const Fl_Menu_Item *m = items_.data()->popup(mx - window()->x(), my - window()->y(), nullptr, nullptr, style);
    if (m && m->callback_) m->callback_(this, m->user_data_);
  }

  int handle(int e) override {
    if (e == FL_PASTE) {   // the text asked for in changed()
      const char *t = Fl::event_text();
      int n = Fl::event_length();
      if (t && n > 0) remember(std::string(t, (size_t)n));
      redraw();
      return 1;
    }
    if (e == FL_PUSH) {
      build();
      open_ = true; redraw(); Fl::flush();
      int r = Fl_Menu_Button::handle(e);
      open_ = false; redraw();
      return r;
    }
    return Fl_Menu_Button::handle(e);
  }
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    draw_box(MENUBAR_BOX, GRAY);
    if (open_) { fl_color(selection_color()); fl_rectf(x(), y(), w(), h() - 1); }
    Fl_Color on = open_ ? SEL_TEXT : TEXT;
    Fl_Color c = g_paused ? fl_color_average(on, open_ ? selection_color() : GRAY, 0.35f) : on;
    draw_clipboard(x() + (w() - 12) / 2, y() + 3, c, open_ ? selection_color() : GRAY, !g_hist.empty());
    fl_pop_clip();
  }
private:
  bool open_ = false;
};

// Another program changed the clipboard: ask for its text (it comes as FL_PASTE)
void changed(int source, void *) {
  if (source != 1 || !g_clip) return;
  if (!Fl::clipboard_contains(Fl::clipboard_plain_text)) return;
  Fl::paste(*g_clip, 1, Fl::clipboard_plain_text);
}

int ignore_errors(Display *, XErrorEvent *) { return 0; }

// Ctrl+Alt+V on the root window, also with Caps Lock / Num Lock on
void grab_hotkey() {
  Display *d = fl_display;
  g_key_v = XKeysymToKeycode(d, XK_v);
  if (!g_key_v) return;
  XSync(d, False);
  auto old = XSetErrorHandler(ignore_errors);   // someone else holds it: no shortcut
  for (unsigned extra : {0u, (unsigned)LockMask, (unsigned)Mod2Mask, (unsigned)(LockMask | Mod2Mask)})
    XGrabKey(d, g_key_v, ControlMask | Mod1Mask | extra, DefaultRootWindow(d), True, GrabModeAsync, GrabModeAsync);
  XSync(d, False);
  XSetErrorHandler(old);
}

int key_events(void *event, void *) {
  XEvent *e = (XEvent *)event;
  if (e->type == KeyPress && g_key_v && e->xkey.keycode == g_key_v &&
      (e->xkey.state & (ControlMask | Mod1Mask)) == (ControlMask | Mod1Mask) && g_clip) {
    g_clip->popup_here();
    return 1;
  }
  return 0;
}

}  // namespace

Fl_Widget *make_clipboard(int X, int Y, int W, int H) {
  g_clip = new ClipMenu(X, Y, W, H);
  Fl::add_clipboard_notify(changed);
  return g_clip;
}

void clipboard_hotkey() {
  fl_open_display();
  grab_hotkey();
  Fl::add_system_handler(key_events, nullptr);
}

}  // namespace applets
