// Nirvana Desktop - alice: Preferences (the set of control panels)
//
// A window with the panel icons, like a drawer of preference programs. A double click (or Enter)
// opens a panel in its own window.
//
// Extensible: more panels come from .desktop files (Name, Name[pl], Exec, Icon - a PNG file or a
// Nirvana icon name such as "harddisk", Replaces - a built-in panel it takes the place of) in
// ~/.local/share/nirvana/preferences and <prefix>/share/nirvana/preferences. The right button
// hides a panel, adds a panel (a name and a command) or removes one the user added; hidden
// panels are remembered in ~/.config/nirvana/preferences.conf.
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Group.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <map>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

using namespace plat;

namespace {

void open_panel(const char *name, Fl_Widget *from = nullptr) {
  // the same program with another argument, in the background (double fork - no zombie)
  pid_t p = fork();
  if (p == 0) {
    if (fork() == 0) {
      if (from && from->window()) {   // Insight: the panel flies out of its icon
        Fl_Window *w = from->window();
        char z[64];
        snprintf(z, sizeof z, "%d %d %d %d", w->x_root() + from->x() + (from->w() - 32) / 2, w->y_root() + from->y() + 4, 32, 32);
        setenv("NIRVANA_ZOOM_FROM", z, 1);
      }
      char exe[4096];
      ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
      exe[n > 0 ? n : 0] = 0;
      execl(exe, "nirvana-alice", name, (char *)nullptr);
      _exit(127);
    }
    _exit(0);
  }
  if (p > 0) waitpid(p, nullptr, 0);
}

class PanelIcon;
void context_menu(PanelIcon *on);

class PanelIcon : public Fl_Button {
public:
  enum Kind { Appearance, Desktop, Network, Power, Shader, DateTime, Printers, User, Saver, Bluetooth, Shield,
              External } kind;
  std::string id;          // "appearance", or the .desktop file of an added panel
  std::string exec;        // External: the command
  std::string icon;        // External: a PNG file or a Nirvana icon name
  std::string file;        // External: its .desktop file
  std::string label_;
  PanelIcon(int X, int Y, Kind k, const std::string &l) : Fl_Button(X, Y, 96, 74), kind(k), label_(l) {
    label(label_.c_str());
    box(FL_NO_BOX);
    labelsize(SIZE_SMALL + 1);
    align(FL_ALIGN_BOTTOM | FL_ALIGN_INSIDE);
  }
  // Like icons everywhere in Nirvana: a click selects, a double click (or Enter) opens
  int handle(int e) override {
    switch (e) {
    case FL_PUSH:
      take_focus();
      if (Fl::event_button() == FL_RIGHT_MOUSE) { context_menu(this); return 1; }
      if (Fl::event_clicks()) { Fl::event_clicks(0); do_callback(); }
      return 1;
    case FL_RELEASE:
    case FL_DRAG:
      return 1;
    case FL_FOCUS:
    case FL_UNFOCUS:
      redraw();
      return 1;
    case FL_KEYBOARD: {
      int k = Fl::event_key();
      if (k == FL_Enter || k == FL_KP_Enter || k == ' ') { do_callback(); return 1; }
      return 0;   // the arrows: the next icon (the group moves the focus)
    }
    case FL_SHORTCUT:
      return 0;
    }
    return Fl_Button::handle(e);
  }
  void draw() override {
    bool sel = Fl::focus() == this;
    int ix = x() + (w() - 32) / 2, iy = y() + 6;
    switch (kind) {
      case External: {   // a PNG file, or one of Nirvana's icons by its name
        static std::map<std::string, Fl_Image *> cache;
        Fl_Image *img = nullptr;
        if (!icon.empty() && icon[0] == '/') {
          auto it = cache.find(icon);
          if (it != cache.end()) img = it->second;
          else {
            fl_register_images();
            if (Fl_Shared_Image *si = Fl_Shared_Image::get(icon.c_str())) {
              if (si->w() > 0) img = si->copy(32, 32);
              si->release();
            }
            cache[icon] = img;
          }
        }
        if (img) { img->draw(ix, iy); break; }
        Icon ic = Icon::Preferences;
        for (int i = 0; i <= (int)Icon::Last; i++)
          if (icon == icon_name((Icon)i)) ic = (Icon)i;
        draw_icon(ic, ix, iy, sel);
        break;
      }
      case Appearance: {   // a monitor with a color palette
        fl_draw_box(UP_BOX, ix, iy, 32, 26, GRAY);
        fl_color(DESK_A); fl_rectf(ix + 3, iy + 3, 26, 18);
        const Fl_Color c[] = {SEL, HILITE, ALERT, LIGHT};
        for (int i = 0; i < 4; i++) { fl_color(c[i]); fl_rectf(ix + 5 + i * 6, iy + 12, 5, 6); }
        fl_color(DARK); fl_rect(ix, iy, 32, 26);
        fl_rectf(ix + 10, iy + 27, 12, 3);
        break;
      }
      case Network: draw_icon(Icon::Network, ix, iy, sel); break;
      case Shield: draw_shield(ix + 3, iy, 32, 1); break;
      case Bluetooth: {   // a round blue badge with the rune, and a little headset beside it
        fl_color(fl_rgb_color(0x2E, 0x5F, 0xC8)); fl_pie(ix + 3, iy, 26, 30, 0, 360);
        fl_color(DARK); fl_arc(ix + 3, iy, 26, 30, 0, 360);
        fl_color(FL_WHITE);
        fl_line_style(FL_SOLID, 2);
        fl_line(ix + 16, iy + 5, ix + 16, iy + 25);
        fl_line(ix + 16, iy + 5, ix + 21, iy + 10);
        fl_line(ix + 21, iy + 10, ix + 11, iy + 20);
        fl_line(ix + 16, iy + 25, ix + 21, iy + 20);
        fl_line(ix + 21, iy + 20, ix + 11, iy + 10);
        fl_line_style(0);
        break;
      }
      case Power:
        draw_battery(ix - 2, iy + 6, 70, true, TEXT, 1.6);
        break;
      case Desktop: {   // a little screen: icon column and a window
        fl_color(DESK_A); fl_rectf(ix, iy + 2, 32, 24);
        fl_color(LIGHT);
        for (int i = 0; i < 3; i++) fl_rectf(ix + 2, iy + 5 + i * 7, 5, 4);
        fl_color(GRAY); fl_rectf(ix + 10, iy + 6, 19, 16);
        fl_color(SEL); fl_rectf(ix + 10, iy + 6, 19, 3);
        fl_color(DARK); fl_rect(ix + 10, iy + 6, 19, 16); fl_rect(ix, iy + 2, 32, 24);
        break;
      }
      case Printers: {   // a printer with a sheet coming out
        fl_color(fl_rgb_color(0xF4, 0xF4, 0xF0)); fl_rectf(ix + 8, iy, 16, 9);
        fl_color(DARK); fl_rect(ix + 8, iy, 16, 9);
        fl_color(fl_rgb_color(0xC8, 0xC8, 0xC0)); fl_rectf(ix, iy + 8, 32, 12);
        fl_color(DARK); fl_rect(ix, iy + 8, 32, 12);
        fl_color(fl_rgb_color(0x3D, 0xB5, 0x4A)); fl_rectf(ix + 26, iy + 11, 3, 2);
        fl_color(fl_rgb_color(0xFF, 0xFF, 0xFF)); fl_rectf(ix + 7, iy + 17, 18, 10);
        fl_color(DARK); fl_rect(ix + 7, iy + 17, 18, 10);
        for (int i = 0; i < 3; i++) fl_xyline(ix + 10, iy + 20 + i * 2, ix + 21);
        break;
      }
      case Saver: {   // a monitor with stars on a night screen
        fl_draw_box(UP_BOX, ix, iy, 32, 26, GRAY);
        fl_color(fl_rgb_color(0x10, 0x14, 0x30)); fl_rectf(ix + 3, iy + 3, 26, 18);
        fl_color(fl_rgb_color(0xFF, 0xFF, 0xFF));
        const int st[][2] = {{6, 6}, {12, 14}, {20, 8}, {25, 16}, {16, 5}, {8, 17}, {22, 12}};
        for (auto &p : st) fl_point(ix + p[0], iy + p[1]);
        fl_color(fl_rgb_color(0xF2, 0xC2, 0x30)); fl_pie(ix + 17, iy + 5, 7, 7, 0, 360);
        fl_color(DARK); fl_rect(ix + 3, iy + 3, 26, 18); fl_rect(ix, iy, 32, 26);
        fl_rectf(ix + 10, iy + 27, 12, 3);
        break;
      }
      case User: {   // a portrait in a frame
        fl_draw_box(UP_BOX, ix + 2, iy, 28, 30, GRAY);
        fl_color(fl_rgb_color(0x8C, 0xC8, 0xF0)); fl_rectf(ix + 5, iy + 3, 22, 24);
        fl_color(SEL);
        fl_pie(ix + 11, iy + 6, 10, 10, 0, 360);
        fl_pie(ix + 7, iy + 16, 18, 20, 0, 180);
        fl_color(DARK); fl_rect(ix + 5, iy + 3, 22, 24); fl_rect(ix + 2, iy, 28, 30);
        break;
      }
      case DateTime: {   // a clock face with a calendar page
        fl_color(fl_rgb_color(0xF4, 0xF4, 0xF0));
        fl_rectf(ix + 2, iy + 4, 20, 22);
        fl_color(fl_rgb_color(0xE5, 0x48, 0x3F));
        fl_rectf(ix + 2, iy + 4, 20, 6);
        fl_color(DARK);
        fl_rect(ix + 2, iy + 4, 20, 22);
        for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) fl_rectf(ix + 5 + c * 4, iy + 13 + r * 4, 2, 2);
        fl_color(GRAY);
        fl_pie(ix + 13, iy + 9, 19, 19, 0, 360);
        fl_color(fl_rgb_color(0xFF, 0xFF, 0xFF));
        fl_pie(ix + 14, iy + 10, 17, 17, 0, 360);
        fl_color(DARK);
        fl_arc(ix + 13, iy + 9, 19, 19, 0, 360);
        fl_line(ix + 22, iy + 18, ix + 22, iy + 12);
        fl_line(ix + 22, iy + 18, ix + 27, iy + 20);
        break;
      }
      case Shader: {   // a tube monitor with scanlines over a rainbow
        fl_draw_box(UP_BOX, ix, iy, 32, 26, GRAY);
        const unsigned band[] = {0xE5483F, 0xF29A26, 0xF2C230, 0x3DB54A, 0x3A7BD5, 0x8A4FC8};
        for (int i = 0; i < 6; i++) {
          fl_color(fl_rgb_color(band[i] >> 16, band[i] >> 8 & 255, band[i] & 255));
          fl_rectf(ix + 3, iy + 3 + i * 3, 26, 3);
        }
        fl_color(fl_rgb_color(20, 20, 30));
        for (int yy = iy + 4; yy < iy + 21; yy += 2) fl_xyline(ix + 3, yy, ix + 28);
        fl_color(DARK);
        fl_rect(ix + 3, iy + 3, 26, 18);
        fl_rect(ix, iy, 32, 26);
        fl_rectf(ix + 10, iy + 27, 12, 3);
        break;
      }
    }
    fl_font(FONT_SMALL, SIZE_SMALL + 1);
    int tw = (int)fl_width(label());
    int tx = x() + (w() - tw) / 2, ty = y() + 46;
    fl_color(sel ? ICON_SEL : GRAY);
    fl_rectf(tx - 2, ty, tw + 4, fl_height());
    fl_color(sel ? ICON_SEL_TEXT : TEXT);
    fl_draw(label(), tx, ty + fl_height() - fl_descent());
  }
};

}  // namespace

namespace {

namespace fs = std::filesystem;

std::string config_file() {
  const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "") + "/.config") + "/nirvana/preferences.conf";
}
std::string user_panels_dir() {
  const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "") + "/.local/share") + "/nirvana/preferences";
}

std::set<std::string> hidden_panels() {
  std::set<std::string> v;
  std::ifstream in(config_file());
  std::string line;
  while (std::getline(in, line))
    if (line.rfind("hide=", 0) == 0) v.insert(line.substr(5));
  return v;
}
void save_hidden(const std::set<std::string> &v) {
  std::error_code ec;
  fs::create_directories(fs::path(config_file()).parent_path(), ec);
  std::ofstream out(config_file());
  for (auto &h : v) out << "hide=" << h << "\n";
}

struct Panel {
  PanelIcon::Kind kind;
  std::string label, id, exec, icon, file;
  bool user = false;   // added by the user (can be removed)
};

std::vector<Panel> external_panels(std::set<std::string> &replaced) {
  std::vector<Panel> v;
  std::vector<std::string> dirs = {user_panels_dir()};
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n > 0) {
    std::string e(buf, (size_t)n);
    dirs.push_back(e.substr(0, e.rfind('/')) + "/../share/nirvana/preferences");
  }
  dirs.push_back("/usr/local/share/nirvana/preferences");
  dirs.push_back("/usr/share/nirvana/preferences");
  std::set<std::string> seen;
  std::string lang = language();
  for (size_t di = 0; di < dirs.size(); di++) {
    std::error_code ec;
    for (auto &de : fs::directory_iterator(dirs[di], ec)) {
      if (de.path().extension() != ".desktop") continue;
      std::string fid = de.path().filename().string();
      if (!seen.insert(fid).second) continue;   // the user's own wins
      std::ifstream in(de.path());
      std::string line, name, lname, exec, icon, repl;
      bool hidden = false;
      while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), val = line.substr(eq + 1);
        if (k == "Name") name = val;
        else if (k == "Name[" + lang + "]") lname = val;
        else if (k == "Exec") exec = val;
        else if (k == "Icon") icon = val;
        else if (k == "Replaces") repl = val;
        else if ((k == "NoDisplay" || k == "Hidden") && val == "true") hidden = true;
      }
      if (hidden || exec.empty() || (name.empty() && lname.empty())) continue;
      if (!repl.empty()) replaced.insert(repl);
      v.push_back({PanelIcon::External, lname.empty() ? name : lname, fid, exec, icon, de.path().string(), di == 0});
    }
  }
  std::sort(v.begin(), v.end(), [](const Panel &a, const Panel &b) { return a.label < b.label; });
  return v;
}

Fl_Group *g_icons = nullptr;
FrameWindow *g_win = nullptr;
Fl_Choice *g_lang = nullptr;
Fl_Box *g_note = nullptr;
std::vector<Panel> g_panels;
std::set<std::string> g_hidden;
const int PER_ROW = 5, CELL_W = 106, CELL_H = 84;

void run_external(const std::string &cmd, Fl_Widget *from) {
  pid_t p = fork();
  if (p == 0) {
    if (fork() == 0) {
      setsid();
      if (from && from->window()) {
        Fl_Window *w = from->window();
        char z[64];
        snprintf(z, sizeof z, "%d %d %d %d", w->x_root() + from->x() + (from->w() - 32) / 2, w->y_root() + from->y() + 4, 32, 32);
        setenv("NIRVANA_ZOOM_FROM", z, 1);
      }
      execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)nullptr);
      _exit(127);
    }
    _exit(0);
  }
  if (p > 0) waitpid(p, nullptr, 0);
}

void build();

// The right button: hide a panel, show the hidden ones, add a panel, remove an added one
void context_menu(PanelIcon *on) {
  std::vector<std::string> labels;
  std::vector<int> acts;
  std::vector<Fl_Menu_Item> items;
  auto add = [&](const std::string &l, int act, int flags = 0) {
    labels.push_back(l);
    acts.push_back(act);
    Fl_Menu_Item it{};
    it.flags = flags;
    it.user_data_ = (void *)(long)(acts.size() - 1);
    items.push_back(it);
  };
  bool removable = false;
  if (on) {
    add(_("Open"), 1);
    add(_("Hide This Panel"), 2, on->kind == PanelIcon::External ? 0 : FL_MENU_DIVIDER);
    for (auto &p : g_panels) if (p.id == on->id && p.user) removable = true;
    if (removable) add(_("Remove This Panel…"), 3, FL_MENU_DIVIDER);
    else if (on->kind == PanelIcon::External) items.back().flags |= FL_MENU_DIVIDER;
  }
  add(_("Add a Panel…"), 4);
  add(g_hidden.empty() ? std::string(_("Show Hidden Panels")) : fmt(_("Show Hidden Panels (%1)"), std::to_string(g_hidden.size())),
      5, g_hidden.empty() ? FL_MENU_INACTIVE : 0);
  for (size_t i = 0; i < items.size(); i++) items[i].text = labels[i].c_str();
  items.push_back(Fl_Menu_Item{});
  Fl_Menu_Button style(0, 0, 1, 1);
  style.textfont(FONT_SYSTEM);
  style.textsize(SIZE_SYSTEM);
  style.color(GRAY);
  style.selection_color(SEL);
  style.down_box(FL_FLAT_BOX);
  const Fl_Menu_Item *m = items.data()->popup(Fl::event_x(), Fl::event_y(), nullptr, nullptr, &style);
  if (!m) return;
  int act = acts[(long)m->user_data()];
  std::string id = on ? on->id : "";
  if (act == 1 && on) on->do_callback();
  else if (act == 2) { g_hidden.insert(id); save_hidden(g_hidden); Fl::add_timeout(0, [](void *) { build(); }); }
  else if (act == 3) {
    std::string f;
    for (auto &p : g_panels) if (p.id == id) f = p.file;
    if (fl_choice(_("Remove the panel “%s” from Preferences?"), _("Cancel"), _("Remove"), nullptr, on->label()) == 1) {
      std::error_code ec;
      fs::remove(f, ec);
      Fl::add_timeout(0, [](void *) { build(); });
    }
  } else if (act == 4) {
    // a name and a command: a new .desktop file in the user's panels folder
    Fl_Double_Window dlg(380, 130, _("Add a Panel"));
    dlg.color(GRAY);
    Fl_Input name(100, 14, 266, 24, _("Name:"));
    Fl_Input cmd(100, 46, 266, 24, _("Command:"));
    for (auto *i : {&name, &cmd}) { i->box(WELL_BOX); i->textsize(SIZE_SMALL + 1); i->labelsize(SIZE_SMALL + 1); }
    Fl_Button cancel(190, 92, 84, 24, _("Cancel")), ok(282, 92, 84, 24, _("Add"));
    for (auto *b : {&cancel, &ok}) { b->box(BUTTON_BOX); b->down_box(BUTTON_DOWN); b->labelsize(SIZE_SMALL + 1); }
    ok.labelfont(FONT_SYSTEM);
    ok.shortcut(FL_Enter);
    int result = 0;
    cancel.callback([](Fl_Widget *w, void *r) { *(int *)r = 1; w->window()->hide(); }, &result);
    ok.callback([](Fl_Widget *w, void *r) { *(int *)r = 2; w->window()->hide(); }, &result);
    dlg.end();
    dlg.set_modal();
    dlg.show();
    while (dlg.shown()) Fl::wait();
    std::string n = name.value(), c = cmd.value();
    if (result != 2 || n.empty() || c.empty()) return;
    std::error_code ec;
    fs::create_directories(user_panels_dir(), ec);
    std::string base;
    for (char ch : n) base += isalnum((unsigned char)ch) ? (char)tolower((unsigned char)ch) : '-';
    std::string f = user_panels_dir() + "/" + base + ".desktop";
    for (int i = 2; fs::exists(f, ec); i++) f = user_panels_dir() + "/" + base + "-" + std::to_string(i) + ".desktop";
    std::ofstream(f) << "[Desktop Entry]\nType=Application\nName=" << n << "\nExec=" << c << "\nIcon=prefs\n";
    Fl::add_timeout(0, [](void *) { build(); });
  } else if (act == 5) {
    g_hidden.clear();
    save_hidden(g_hidden);
    Fl::add_timeout(0, [](void *) { build(); });
  }
}

// The background of the icons: the right button adds a panel or shows the hidden ones
class IconArea : public Fl_Group {
public:
  IconArea(int X, int Y, int W, int H) : Fl_Group(X, Y, W, H) {}
  int handle(int e) override {
    if (e == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE) {
      for (int i = 0; i < children(); i++)
        if (Fl::event_inside(child(i))) return Fl_Group::handle(e);
      context_menu(nullptr);
      return 1;
    }
    return Fl_Group::handle(e);
  }
};

void build() {
  g_hidden = hidden_panels();
  std::set<std::string> replaced;
  std::vector<Panel> ext = external_panels(replaced);
  g_panels.clear();
  const Panel builtin[] = {
      {PanelIcon::User, _("User"), "user"},
      {PanelIcon::Appearance, _("Appearance"), "appearance"},
      {PanelIcon::Desktop, _("Desktop"), "desktop"},
      {PanelIcon::Network, _("Network"), "network"},
      {PanelIcon::Bluetooth, _("Bluetooth"), "bluetooth"},
      {PanelIcon::Shield, "NirvanaSHIELD", "shield"},
      {PanelIcon::Power, _("Power"), "power"},
      {PanelIcon::Saver, _("Screen Saver"), "saver"},
      {PanelIcon::DateTime, _("Date & Time"), "datetime"},
      {PanelIcon::Printers, _("Printers"), "printers"},
      {PanelIcon::Shader, _("Shader"), "shader"},
  };
  for (auto &p : builtin)
    if (!replaced.count(p.id)) g_panels.push_back(p);
  for (auto &p : ext) g_panels.push_back(p);
  std::vector<Panel *> shown;
  for (auto &p : g_panels) if (!g_hidden.count(p.id)) shown.push_back(&p);

  int N = std::max(1, (int)shown.size());
  const int rows = (N + PER_ROW - 1) / PER_ROW;
  const int W = 14 * 2 + PER_ROW * CELL_W - 10, icons_h = 20 + rows * CELL_H;
  const int H = icons_h + 90;
  // after finish() (a rebuild in the open window): the content offset of our own frame
  int ox = g_win->shown() ? g_win->ox() : 0, oy = g_win->shown() ? g_win->oy() : 0;
  g_icons->clear();
  g_icons->resize(ox, oy, W, icons_h);
  g_icons->begin();
  for (int i = 0; i < (int)shown.size(); i++) {
    Panel *p = shown[i];
    auto *b = new PanelIcon(ox + 14 + (i % PER_ROW) * CELL_W, oy + 20 + (i / PER_ROW) * CELL_H, p->kind, p->label);
    b->id = p->id;
    b->exec = p->exec;
    b->icon = p->icon;
    b->file = p->file;
    if (p->kind == PanelIcon::External)
      b->callback([](Fl_Widget *w, void *) { run_external(((PanelIcon *)w)->exec, w); });
    else
      b->callback([](Fl_Widget *w, void *) { open_panel(((PanelIcon *)w)->id.c_str(), w); });
  }
  g_icons->end();
  g_lang->resize(ox + 110, oy + icons_h + 8, W - 124, 22);
  g_note->resize(ox + 14, oy + icons_h + 36, W - 28, 44);
  if (g_win->shown()) g_win->size(W + 2 * ox, H + oy + ox);
  g_win->redraw();
}

}  // namespace

int panel_preferences() {
  const int W = 14 * 2 + PER_ROW * CELL_W - 10;
  auto *win = new FrameWindow(W, 300, _("Preferences"));
  g_win = win;
  win->xclass("nirvana-preferences");
  g_icons = new IconArea(0, 0, W, 200);
  g_icons->end();

  // Interface language - saved in ~/.config/nirvana/language, applies after a restart
  static std::vector<std::string> codes;
  codes = {""};
  auto *lang = new Fl_Choice(110, 208, W - 124, 22, _("Language:"));
  g_lang = lang;
  lang->labelfont(FONT_SYSTEM);
  lang->labelsize(SIZE_SYSTEM);
  lang->textsize(SIZE_SMALL + 1);
  lang->add(_("Automatic (system language)"));
  for (auto &[code, name] : available_languages()) {
    codes.push_back(code);
    lang->add(name.c_str());
  }
  std::string cur = language_setting();
  lang->value(0);
  for (size_t i = 0; i < codes.size(); i++) if (codes[i] == cur) lang->value((int)i);
  auto *note = new Fl_Box(14, 236, W - 28, 44);
  g_note = note;
  note->labelsize(SIZE_SMALL);
  note->align(FL_ALIGN_INSIDE | FL_ALIGN_TOP_LEFT | FL_ALIGN_WRAP);
  note->label(_("The new language takes effect after restarting Nirvana Desktop or after Special > Reset Nirvana."));
  lang->callback([](Fl_Widget *w, void *) {
    int v = ((Fl_Choice *)w)->value();
    if (v >= 0 && v < (int)codes.size()) set_language_setting(codes[v]);
  });
  win->end();
  build();
  int H = g_lang->y() + 82;
  win->size(W, H);
  win->finish();
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + win->oy(), (sh - H) / 4));
  win->show();
  return Fl::run();
}
