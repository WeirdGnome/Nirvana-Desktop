// Nirvana Desktop - alice: the NirvanaSHIELD panel
//
// Made so that a child and a grandfather can use it: big letters, one big switch per
// program ("HAS INTERNET" / "NO INTERNET"), three big buttons for the whole computer,
// and no network words. Programs that just tried to get to the internet come first, with
// an orange note. With the parental lock every change asks for the administrator password.
#include "framewin.h"
#include "platinum.h"
#include "filedialog.h"
#include "shield.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

using namespace plat;

namespace {

const int W = 780, H = 640;
const Fl_Color GREEN = fl_rgb_color(0x2E, 0xA0, 0x44), RED = fl_rgb_color(0xD2, 0x3B, 0x30),
               ORANGE = fl_rgb_color(0xE0, 0x7A, 0x10);

FrameWindow *g_win;
shield::Status g_st;
std::vector<shield::Program> g_progs;
std::set<std::string> g_allowed;
std::map<std::string, shield::Blocked> g_blocked;   // path -> the last attempt
std::string g_filter;

Fl_Box *g_title, *g_text;
Fl_Widget *g_banner_shield;
Fl_Scroll *g_scroll;
Fl_Input *g_search;
Fl_Check_Button *g_lock, *g_lan;
Fl_Group *g_off_group;   // shown when the service is not running
Fl_Group *g_main_group;
std::string g_title_s, g_text_s;
class ModeButton;
ModeButton *g_modes[3];

void rebuild();
void refresh();

std::string home_dir() { const char *h = getenv("HOME"); return h ? h : "/"; }

// A change; with the parental lock (or when the service says "locked") through pkexec.
bool change(const std::vector<std::string> &cmds) {
  std::string err;
  if (!g_st.lock) {
    for (auto &c : cmds) {
      std::string r;
      if (!shield::request(c, r)) { err = "not running"; break; }
      if (r.rfind("ok", 0) != 0) { err = r; break; }
    }
    if (err.empty()) return true;
    if (err.find("locked") == std::string::npos) { fl_alert("%s", err.c_str()); return false; }
  }
  err = shield::admin(cmds);
  if (err == "canceled") return false;
  if (!err.empty()) { fl_alert("%s", err.c_str()); return false; }
  return true;
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
class BigShield : public Fl_Widget {
public:
  BigShield(int X, int Y, int S) : Fl_Widget(X, Y, (int)(S * 0.82) + 2, S + 2) {}
  void draw() override {
    int state = !g_st.running ? 3 : g_st.mode == "protect" ? 1 : g_st.mode == "block" ? 2 : 0;
    fl_color(GRAY);
    fl_rectf(x(), y(), w(), h());
    draw_shield(x(), y(), h() - 2, state);
  }
};

class ModeButton : public Fl_Button {
public:
  std::string mode;
  int state;
  ModeButton(int X, int Y, int W, int H, const char *l, const char *m, int st) : Fl_Button(X, Y, W, H, l), mode(m), state(st) {
    box(BUTTON_BOX);
    down_box(BUTTON_DOWN);
    labelfont(FONT_SYSTEM);
    labelsize(15);
    callback([](Fl_Widget *w, void *) {
      auto *b = (ModeButton *)w;
      if (g_st.mode == b->mode) return;
      if (change({"MODE " + b->mode})) refresh();
    });
  }
  void draw() override {
    bool on = g_st.mode == mode;
    Fl_Color bg = on ? (state == 1 ? GREEN : state == 2 ? RED : fl_rgb_color(0x80, 0x80, 0x80)) : GRAY;
    fl_draw_box(on ? BUTTON_DOWN : BUTTON_BOX, x(), y(), w(), h(), bg);
    if (on) { fl_color(bg); fl_rectf(x() + 3, y() + 3, w() - 6, h() - 6); }   // the chosen one in its color
    draw_shield(x() + 14, y() + (h() - 30) / 2, 30, state);
    fl_font(FONT_SYSTEM, 15);
    fl_color(on ? FL_WHITE : TEXT);
    fl_draw(label(), x() + 52, y(), w() - 58, h(), FL_ALIGN_LEFT | FL_ALIGN_WRAP);
    if (Fl::focus() == this) draw_focus();
  }
};

// The big switch of one program
class Switch : public Fl_Widget {
public:
  bool on = false;
  Switch(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}
  void draw() override {
    Fl_Color c = on ? GREEN : fl_rgb_color(0x9C, 0x9C, 0x9C);
    fl_color(DARK);
    fl_rectf(x(), y(), w(), h());
    fl_color(c);
    fl_rectf(x() + 1, y() + 1, w() - 2, h() - 2);
    // the knob: right when on, left when off
    int kw = h() - 6, kx = on ? x() + w() - kw - 3 : x() + 3;
    fl_draw_box(UP_BOX, kx, y() + 3, kw, h() - 6, LIGHT);
    fl_font(FONT_SYSTEM, 12);
    fl_color(FL_WHITE);
    const char *t = on ? _("HAS INTERNET") : _("NO INTERNET");
    int tx = on ? x() + 6 : x() + kw + 8, tw = w() - kw - 14;
    fl_draw(t, tx, y(), tw, h(), FL_ALIGN_CENTER);
  }
  int handle(int e) override {
    if (e == FL_PUSH) return 1;
    if (e == FL_RELEASE && Fl::event_inside(this)) { do_callback(); return 1; }
    return Fl_Widget::handle(e);
  }
};

// Icons: an absolute file, or a name in the hicolor theme / pixmaps
std::map<std::string, Fl_Image *> g_icons;
Fl_Image *icon_for(const shield::Program &p) {
  std::string key = p.icon;
  auto it = g_icons.find(key);
  if (it != g_icons.end()) return it->second;
  Fl_Image *img = nullptr;
  std::vector<std::string> tries;
  if (!key.empty() && key[0] == '/' && !p.appimage) tries.push_back(key);
  else if (!key.empty() && !p.appimage)
    for (const char *sz : {"48x48", "64x64", "128x128", "256x256", "32x32", "scalable"})
      for (const char *ext : {".png", ".svg"}) tries.push_back(std::string("/usr/share/icons/hicolor/") + sz + "/apps/" + key + ext);
  if (!key.empty() && !p.appimage) for (const char *ext : {".png", ".svg", ".xpm"}) tries.push_back("/usr/share/pixmaps/" + key + ext);
  for (auto &t : tries) {
    if (access(t.c_str(), R_OK) != 0) continue;
    if (Fl_Shared_Image *si = Fl_Shared_Image::get(t.c_str())) {
      if (si->w() > 0 && si->image()) { img = si->image()->copy(40, 40); si->release(); break; }
      si->release();
    }
  }
  g_icons[key] = img;
  return img;
}

class Tile : public Fl_Group {
public:
  shield::Program prog;
  Switch *sw;
  bool tried = false;
  std::string note;
  Tile(int X, int Y, int W, const shield::Program &p) : Fl_Group(X, Y, W, 62), prog(p) {
    box(FL_NO_BOX);
    sw = new Switch(X + W - 176, Y + 12, 164, 38);
    end();
    sw->on = std::any_of(prog.paths.begin(), prog.paths.end(), [](const std::string &f) { return g_allowed.count(f); });
    for (auto &f : prog.paths) {
      auto b = g_blocked.find(f);
      if (b != g_blocked.end()) {
        tried = true;
        char t[16];
        time_t w = (time_t)b->second.when;
        strftime(t, sizeof t, "%H:%M", localtime(&w));
        note = fmt(_("Wanted to use the internet at %1"), t);
      }
    }
    sw->callback([](Fl_Widget *w, void *) {
      auto *t = (Tile *)w->parent();
      bool on = !t->sw->on;
      std::vector<std::string> cmds;
      for (auto &f : t->prog.paths) cmds.push_back(std::string(on ? "ALLOW " : "DENY ") + f);
      if (change(cmds)) refresh();
    });
  }
  void draw() override {
    fl_color(tried && !sw->on ? fl_color_average(ORANGE, CONTENT, 0.18f) : CONTENT);
    fl_rectf(x(), y(), w(), h());
    fl_color(fl_color_average(TEXT, CONTENT, 0.15f));
    fl_xyline(x() + 8, y() + h() - 1, x() + w() - 8);
    if (Fl_Image *img = icon_for(prog)) img->draw(x() + 14 + (40 - img->w()) / 2, y() + 11 + (40 - img->h()) / 2);
    else draw_icon(Icon::Application, x() + 18, y() + 15, false);
    fl_color(CONTENT_TEXT);
    fl_font(FONT_SYSTEM, 16);
    int tw = w() - 76 - 190;
    fl_draw(ellipsize(prog.name, tw).c_str(), x() + 66, y() + 27);
    fl_font(FONT_SMALL, 13);
    if (!note.empty() && !sw->on) {
      fl_color(ORANGE);
      fl_draw(ellipsize(note, tw).c_str(), x() + 66, y() + 47);
    } else {
      fl_color(fl_color_average(CONTENT_TEXT, CONTENT, 0.55f));
      std::string sub = prog.appimage ? std::string(_("Program from the Applications folder")) : prog.comment;
      fl_draw(ellipsize(sub, tw).c_str(), x() + 66, y() + 47);
    }
    draw_children();
  }
};

// ---------------------------------------------------------------------------
void banner() {
  if (!g_st.running) {
    g_title_s = _("NirvanaSHIELD is switched off");
    g_text_s = _("Every program can use the internet. Switch NirvanaSHIELD on, then choose which programs may.");
  } else if (!g_st.error.empty() && g_st.mode == "block") {
    g_title_s = _("All internet is blocked");
    g_text_s = fmt(_("NirvanaSHIELD cannot tell programs apart on this computer (%1), so nothing gets out."), g_st.error);
  } else if (g_st.mode == "protect") {
    g_title_s = _("The internet is protected");
    g_text_s = _("Only the programs with a green switch can use the internet. A new program asks first.");
  } else if (g_st.mode == "block") {
    g_title_s = _("All internet is blocked");
    g_text_s = _("No program can use the internet now - not even the green ones.");
  } else {
    g_title_s = _("Protection is off");
    g_text_s = _("Every program can use the internet. Press \"Protect\" to choose again.");
  }
  g_title->label(g_title_s.c_str());
  g_text->label(g_text_s.c_str());
}

void refresh() {
  g_st = shield::status();
  g_allowed.clear();
  g_blocked.clear();
  if (g_st.running) {
    for (auto &a : shield::allowed()) g_allowed.insert(a);
    for (auto &b : shield::blocked()) if (!g_blocked.count(b.path)) g_blocked[b.path] = b;
  }
  banner();
  if (g_st.running) { g_off_group->hide(); g_main_group->show(); }
  else { g_main_group->hide(); g_off_group->show(); }
  g_lock->value(g_st.lock);
  g_lan->value(g_st.lan);
  rebuild();
  g_win->redraw();
}

std::string lower(std::string s) {
  for (auto &c : s) c = (char)tolower((unsigned char)c);
  return s;
}

void rebuild() {
  // the list: tried to use the internet, then allowed, then the rest; and programs
  // that tried but are not in the menus (a program file of its own)
  std::vector<shield::Program> list = g_progs;
  std::set<std::string> known;
  for (auto &p : list) for (auto &f : p.paths) known.insert(f);
  for (auto &[path, b] : g_blocked)
    if (!known.count(path)) { shield::Program p; p.name = path.substr(path.rfind('/') + 1); p.paths = {path}; p.comment = path; list.push_back(p); known.insert(path); }
  for (auto &path : g_allowed)
    if (!known.count(path)) { shield::Program p; p.name = path.substr(path.rfind('/') + 1); p.paths = {path}; p.comment = path; list.push_back(p); }
  auto rank = [](const shield::Program &p) {
    bool on = false, tried = false;
    for (auto &f : p.paths) { on |= g_allowed.count(f) > 0; tried |= g_blocked.count(f) > 0; }
    return tried && !on ? 0 : on ? 1 : 2;
  };
  std::stable_sort(list.begin(), list.end(), [&](const shield::Program &a, const shield::Program &b) { return rank(a) < rank(b); });
  int sy = g_scroll->yposition();
  g_scroll->clear();
  g_scroll->begin();
  int y = g_scroll->y();
  std::string f = lower(g_filter);
  for (auto &p : list) {
    if (!f.empty() && lower(p.name).find(f) == std::string::npos) continue;
    new Tile(g_scroll->x(), y, g_scroll->w() - SCROLL_W, p);
    y += 62;
  }
  if (y == g_scroll->y()) {
    auto *b = new Fl_Box(g_scroll->x(), y, g_scroll->w() - SCROLL_W, 60, _("No program found."));
    b->labelsize(15);
  }
  g_scroll->end();
  g_scroll->scroll_to(0, std::min(sy, std::max(0, y - g_scroll->y() - g_scroll->h())));
  g_scroll->redraw();
}

void tick(void *) {
  shield::Status s = shield::status();
  bool changed = s.running != g_st.running || s.mode != g_st.mode || s.lock != g_st.lock;
  if (s.running)
    for (auto &x : shield::blocked()) {
      auto it = g_blocked.find(x.path);
      if (it == g_blocked.end() || it->second.when != x.when) changed = true;
    }
  if (changed) refresh();
  Fl::repeat_timeout(3.0, tick);
}

Fl_Button *button(int X, int Y, int W, int H, const char *t, int size = 14) {
  auto *b = new Fl_Button(X, Y, W, H, t);
  b->box(BUTTON_BOX);
  b->down_box(BUTTON_DOWN);
  b->labelsize(size);
  return b;
}

}  // namespace

int panel_shield() {
  static bool reg = false;
  if (!reg) { fl_register_images(); reg = true; }
  g_progs = shield::programs(home_dir() + "/.nirvana-apps");
  g_win = new FrameWindow(W, H, "NirvanaSHIELD");
  g_win->xclass("nirvana-shield");

  g_banner_shield = new BigShield(22, 14, 84);
  g_title = new Fl_Box(126, 16, W - 140, 32);
  g_title->labelfont(FONT_SYSTEM);
  g_title->labelsize(22);
  g_title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_text = new Fl_Box(126, 50, W - 140, 48);
  g_text->labelsize(15);
  g_text->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  // --- the service is running ---
  g_main_group = new Fl_Group(0, 110, W, H - 110);
  int bw = (W - 30 - 2 * 10) / 3;
  g_modes[0] = new ModeButton(15, 116, bw, 52, _("Protect\n(only green ones)"), "protect", 1);
  g_modes[1] = new ModeButton(15 + bw + 10, 116, bw, 52, _("Block all\ninternet"), "block", 2);
  g_modes[2] = new ModeButton(15 + 2 * (bw + 10), 116, bw, 52, _("Switch\nprotection off"), "off", 0);
  auto *sl = new Fl_Box(15, 182, 80, 32, _("Find:"));
  sl->labelfont(FONT_SYSTEM);
  sl->labelsize(15);
  sl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_search = new Fl_Input(85, 182, 300, 32);
  g_search->box(WELL_BOX);
  g_search->textsize(16);
  g_search->when(FL_WHEN_CHANGED);
  g_search->callback([](Fl_Widget *w, void *) { g_filter = ((Fl_Input *)w)->value(); rebuild(); });
  auto *add = button(W - 15 - 220, 182, 220, 32, _("Add another program…"));
  add->callback([](Fl_Widget *, void *) {
    std::string f = choose_file(_("Which program may use the internet?"), {}, "/usr/bin");
    if (f.empty()) return;
    char real[4096];
    if (realpath(f.c_str(), real)) f = real;
    if (change({"ALLOW " + f})) refresh();
  });
  g_scroll = new Fl_Scroll(15, 224, W - 30, H - 224 - 62);
  g_scroll->box(WELL_BOX);
  g_scroll->color(CONTENT);
  g_scroll->type(Fl_Scroll::VERTICAL_ALWAYS);
  g_scroll->end();
  g_lock = new Fl_Check_Button(15, H - 52, 300, 22, _("Changes need the parent's password"));
  g_lock->labelsize(14);
  g_lock->callback([](Fl_Widget *w, void *) {
    bool on = ((Fl_Check_Button *)w)->value();
    // switching the lock always takes the administrator password
    std::string err = shield::admin({std::string("LOCK ") + (on ? "1" : "0")});
    if (!err.empty() && err != "canceled") fl_alert("%s", err.c_str());
    refresh();
  });
  g_lan = new Fl_Check_Button(15, H - 30, 380, 22, _("Home network always allowed (printers, shared folders)"));
  g_lan->labelsize(14);
  g_lan->callback([](Fl_Widget *w, void *) {
    if (!change({std::string("LAN ") + (((Fl_Check_Button *)w)->value() ? "1" : "0")})) {}
    refresh();
  });
  g_main_group->end();

  // --- the service is not running ---
  g_off_group = new Fl_Group(0, 110, W, H - 110);
  auto *info = new Fl_Box(60, 150, W - 120, 120,
                          _("When NirvanaSHIELD is on, no program can use the internet until you allow it. "
                            "Updates of the system and printing always work.\n\n"
                            "Programs asking for the internet show a message with the buttons \"Allow\" and \"No\"."));
  info->labelsize(16);
  info->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  auto *on = button((W - 360) / 2, 300, 360, 64, _("Switch NirvanaSHIELD on"), 20);
  on->labelfont(FONT_SYSTEM);
  on->callback([](Fl_Widget *, void *) {
    std::string err = shield::enable_service(true);
    if (!err.empty() && err != "canceled") fl_alert("%s", err.c_str());
    for (int i = 0; i < 20 && !shield::status().running; i++) usleep(100000);
    refresh();
  });
  g_off_group->end();

  auto *ok = button(W - 102, H - 42, 86, 28, _("OK"));
  ok->labelfont(FONT_SYSTEM);
  ok->shortcut(FL_Enter);
  ok->callback([](Fl_Widget *, void *) { g_win->hide(); });
  g_win->end();
  g_win->finish();
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + g_win->oy(), (sh - H) / 4));
  refresh();
  g_win->show();
  Fl::add_timeout(3.0, tick);   // a program may just be asking: keep the list fresh
  return Fl::run();
}
