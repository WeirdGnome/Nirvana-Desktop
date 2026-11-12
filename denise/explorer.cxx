// Nirvana Desktop - Rambler: the explorer window (Preferences > Desktop > Folder windows)
//
// The look of the classic explorer windows of the late 1990s - a bar of big flat buttons
// with labels, an address field, the folder tree on the left, the files on the right, a
// status bar - with the working style of the two-pane file managers of the 16-bit
// desktops: a second pane at the press of a button, one pane is the SOURCE (the active
// one, everything works on it), the other the DESTINATION (copy F5, move F6 go there; Tab
// swaps), file types with their own actions, archives opened like folders and servers
// (SFTP, FTP) shown like folders.
//
// The window is an ordinary folder window (NWin) whose "pane" is always the active pane -
// so every command of Rambler (menus, keys, dragging, renaming) works on the source.
#include "desktop.h"
#include "appimage.h"
#include "i18n.h"
#include "jobs.h"
#include "metadata.h"
#include "sniff.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Tree.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <fnmatch.h>
#include <set>
#include <sstream>
#include <thread>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace plat;

static std::string run_with_input(const std::vector<std::string> &args, const std::string &input);
static std::set<std::string> g_archive_mounts;   // archives opened through archivemount (written back)
struct ServerSpec { std::string kind, host, port, user, folder; };   // kind: sftp ftp smb
static std::string servers_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/servers.conf";
}

static std::string human_size(uintmax_t b) {
  char t[32];
  if (b < 1024) snprintf(t, sizeof t, "%ju B", b);
  else if (b < 1024 * 1024) snprintf(t, sizeof t, "%.1f KB", b / 1024.0);
  else if (b < 1024ull * 1024 * 1024) snprintf(t, sizeof t, "%.1f MB", b / 1048576.0);
  else snprintf(t, sizeof t, "%.2f GB", b / 1073741824.0);
  return t;
}


static const int TOOL_H = 46, ADDR_H = 26, STATUS_H = 20, HEAD_H = 18, SPLIT_W = 5, LOGO_W = 52;

class ExTree;
struct Explorer {
  FolderPane *panes[2] = {nullptr, nullptr};
  Fl_Group *slots[2] = {nullptr, nullptr};
  struct Hist { std::string path; std::vector<std::string> back, fwd; } hist[2];
  int active = 0;
  bool dual = false, tree_on = true;
  int tree_w = 220;
  ExTree *tree = nullptr;
  Fl_Group *toolbar = nullptr, *addr = nullptr;
  Fl_Widget *status = nullptr, *splitter = nullptr;
  Fl_Button *b_tree = nullptr, *b_dual = nullptr, *b_copy = nullptr, *b_move = nullptr, *go = nullptr;
  Fl_Box *addr_label = nullptr;
  Fl_Widget *logo = nullptr;         // the logo at the right end of the button bar
  int tools_end = 0;                 // where the buttons end (relative to the bar)
  std::string status_key;
  bool info_on = false;              // the information panel (under the tree)
  Fl_Widget *info = nullptr;
  Fl_Button *b_info = nullptr;
  Fl_Input *filter = nullptr;        // the source pane's filter
  Fl_Box *filter_label = nullptr;
  std::string filters[2];
  std::string sel_key;               // the selection last saved (session state)
};

// the destination pane's folder ("" when the window has one pane)
std::string ex_other_path(NWin *w) {
  if (!w || !w->ex || !w->ex->dual) return "";
  return w->ex->panes[1 - w->ex->active]->path();
}

static void status_tick(void *d);
// The window is going away: its timer stops first (it ran with the window as its data -
// after the window was freed it crashed Rambler, and the session started it again: the
// desktop "reset" when a folder window was closed)
void destroy_explorer(Explorer *x, NWin *w);
void destroy_explorer(Explorer *x, NWin *w) {
  Fl::remove_timeout(status_tick, w);
  delete x;
}

// ---------------------------------------------------------------------------
// The big flat buttons with a picture and a label
// ---------------------------------------------------------------------------
enum class ExIcon { Back, Forward, Up, Tree, Dual, CopyTo, MoveTo, Delete, Views, Server, Swap, Search, Info };

static void ex_icon(ExIcon k, int x, int y, bool on) {
  Fl_Color ink = on ? TEXT : fl_color_average(TEXT, GRAY, 0.4f);
  Fl_Color acc = on ? SEL : fl_color_average(SEL, GRAY, 0.4f);
  Fl_Color green = on ? fl_rgb_color(0x2E, 0xA0, 0x44) : fl_color_average(fl_rgb_color(0x2E, 0xA0, 0x44), GRAY, 0.5f);
  Fl_Color paper = on ? fl_rgb_color(0xFF, 0xFF, 0xF4) : LIGHT;
  Fl_Color folder = on ? fl_rgb_color(0xF2, 0xC2, 0x30) : fl_color_average(fl_rgb_color(0xF2, 0xC2, 0x30), GRAY, 0.5f);
  auto arrow_r = [&](int ax, int ay, Fl_Color c) {   // a fat arrow to the right, 12x10
    fl_color(c);
    fl_rectf(ax, ay + 3, 7, 5);
    fl_polygon(ax + 6, ay, ax + 12, ay + 5, ax + 6, ay + 10);
  };
  auto doc = [&](int dx, int dy) {
    fl_color(paper); fl_rectf(dx, dy, 10, 13);
    fl_color(ink); fl_rect(dx, dy, 10, 13);
    for (int i = 0; i < 3; i++) fl_xyline(dx + 2, dy + 4 + i * 3, dx + 7);
  };
  auto fold = [&](int fx, int fy, int w, int h) {
    fl_color(folder); fl_rectf(fx, fy + 2, w, h - 2); fl_rectf(fx, fy, w / 2, 3);
    fl_color(ink); fl_rect(fx, fy + 2, w, h - 2);
  };
  switch (k) {
  case ExIcon::Back:
  case ExIcon::Forward: {   // a round green button with a white arrow
    fl_color(green); fl_pie(x + 2, y + 2, 20, 20, 0, 360);
    fl_color(ink); fl_arc(x + 2, y + 2, 20, 20, 0, 360);
    fl_color(FL_WHITE);
    if (k == ExIcon::Back) fl_polygon(x + 6, y + 12, x + 12, y + 6, x + 12, y + 18);
    else fl_polygon(x + 18, y + 12, x + 12, y + 6, x + 12, y + 18);
    fl_rectf(k == ExIcon::Back ? x + 11 : x + 7, y + 10, 6, 5);
    break;
  }
  case ExIcon::Up:
    fold(x + 2, y + 8, 20, 14);
    fl_color(ink);
    fl_polygon(x + 12, y + 1, x + 6, y + 8, x + 18, y + 8);
    fl_rectf(x + 10, y + 7, 4, 7);
    break;
  case ExIcon::Tree:
    fl_color(ink);
    fl_yxline(x + 5, y + 6, y + 19);
    fl_xyline(x + 5, y + 12, x + 9);
    fl_xyline(x + 5, y + 19, x + 9);
    fold(x + 1, y + 1, 10, 7);
    fold(x + 10, y + 9, 10, 7);
    fold(x + 10, y + 16, 10, 7);
    break;
  case ExIcon::Dual: {   // one window split in two: the source (a blue header) and the destination
    Fl_Color frame = fl_color_average(GRAY, ink, 0.55f);
    fl_color(frame); fl_rectf(x + 1, y + 2, 22, 4);                  // the title bar
    fl_color(paper); fl_rectf(x + 2, y + 6, 9, 15); fl_rectf(x + 13, y + 6, 9, 15);
    fl_color(acc); fl_rectf(x + 2, y + 6, 9, 3);                     // the source header
    fl_color(fl_color_average(GRAY, ink, 0.3f)); fl_rectf(x + 13, y + 6, 9, 3);   // the destination header
    fl_color(fl_color_average(paper, ink, 0.6f));                   // lines of files
    for (int ly = y + 11; ly <= y + 18; ly += 3) { fl_xyline(x + 3, ly, x + 9); fl_xyline(x + 14, ly, x + 20); }
    fl_color(ink);
    fl_rect(x + 1, y + 2, 22, 20);
    fl_yxline(x + 11, y + 6, y + 20); fl_yxline(x + 12, y + 6, y + 20);   // the divider
    fl_xyline(x + 1, y + 5, x + 22);
    break;
  }
  case ExIcon::CopyTo:
    doc(x + 1, y + 2); doc(x + 4, y + 6);
    arrow_r(x + 12, y + 9, acc);
    break;
  case ExIcon::MoveTo:
    doc(x + 1, y + 4);
    arrow_r(x + 12, y + 7, acc);
    break;
  case ExIcon::Delete:
    fl_color(on ? fl_rgb_color(0xD2, 0x3B, 0x30) : fl_color_average(ALERT, GRAY, 0.5f));
    fl_line_style(FL_SOLID | FL_CAP_ROUND, 4);
    fl_line(x + 5, y + 5, x + 19, y + 19);
    fl_line(x + 19, y + 5, x + 5, y + 19);
    fl_line_style(0);
    break;
  case ExIcon::Views:
    for (int r = 0; r < 2; r++) for (int c = 0; c < 2; c++) {
      fl_color(paper); fl_rectf(x + 2 + c * 11, y + 2 + r * 11, 9, 9);
      fl_color(acc); fl_rectf(x + 4 + c * 11, y + 4 + r * 11, 5, 4);
      fl_color(ink); fl_rect(x + 2 + c * 11, y + 2 + r * 11, 9, 9);
    }
    break;
  case ExIcon::Server:   // a globe
    fl_color(on ? fl_rgb_color(0x3A, 0x7B, 0xD5) : fl_color_average(fl_rgb_color(0x3A, 0x7B, 0xD5), GRAY, 0.5f));
    fl_pie(x + 2, y + 2, 20, 20, 0, 360);
    fl_color(green);
    fl_pie(x + 6, y + 5, 7, 6, 0, 360); fl_pie(x + 12, y + 12, 7, 7, 0, 360);
    fl_color(ink); fl_arc(x + 2, y + 2, 20, 20, 0, 360);
    fl_arc(x + 7, y + 2, 10, 20, 0, 360);
    fl_xyline(x + 2, y + 12, x + 22);
    break;
  case ExIcon::Search:   // a magnifying glass over a page
    doc(x + 1, y + 1);
    fl_color(on ? fl_rgb_color(0xCF, 0xE6, 0xFA) : LIGHT);
    fl_pie(x + 9, y + 7, 11, 11, 0, 360);
    fl_color(ink);
    fl_line_style(FL_SOLID, 2);
    fl_arc(x + 9, y + 7, 11, 11, 0, 360);
    fl_line(x + 18, y + 16, x + 22, y + 21);
    fl_line_style(0);
    break;
  case ExIcon::Info:   // an "i" in a round blue badge
    fl_color(on ? fl_rgb_color(0x3A, 0x7B, 0xD5) : fl_color_average(fl_rgb_color(0x3A, 0x7B, 0xD5), GRAY, 0.5f));
    fl_pie(x + 2, y + 2, 20, 20, 0, 360);
    fl_color(ink); fl_arc(x + 2, y + 2, 20, 20, 0, 360);
    fl_color(FL_WHITE);
    fl_rectf(x + 11, y + 6, 3, 3);
    fl_rectf(x + 11, y + 10, 3, 8);
    fl_rectf(x + 9, y + 10, 3, 2);
    fl_rectf(x + 9, y + 16, 7, 2);
    break;
  case ExIcon::Swap:
    arrow_r(x + 6, y + 3, acc);
    fl_color(acc);   // and one to the left below
    fl_rectf(x + 11, y + 14, 7, 5);
    fl_polygon(x + 12, y + 11, x + 6, y + 16, x + 12, y + 21);
    break;
  }
}

// The logo at the right end of the button bar, in its own field - where the explorers of the
// late 1990s had theirs
class ExLogo : public Fl_Widget {
public:
  Fl_RGB_Image *img = nullptr;
  ExLogo(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}
  ~ExLogo() override { delete img; }
  void draw() override {
    fl_color(GRAY);
    fl_rectf(x(), y(), w(), h());
    fl_color(SHADOW); fl_yxline(x() + 1, y() + 3, y() + h() - 4);
    fl_color(LIGHT); fl_yxline(x() + 2, y() + 3, y() + h() - 4);
    int s = std::min(w() - 10, h() - 4);
    if (!img || img->w() != s) { delete img; img = logo_image(s); }
    if (img) img->draw(x() + 6 + (w() - 6 - s) / 2, y() + (h() - s) / 2);
  }
};

class ExButton : public Fl_Button {
public:
  ExIcon kind;
  bool toggle_on = false;
  ExButton(int X, int Y, int W, int H, ExIcon k, const char *l) : Fl_Button(X, Y, W, H), kind(k) {
    copy_label(l);
    box(FL_NO_BOX);
    clear_visible_focus();
    labelsize(SIZE_SMALL);
  }
  void draw() override {
    bool hover = Fl::belowmouse() == this && active_r();
    bool down = value() || toggle_on;
    fl_color(GRAY);
    fl_rectf(x(), y(), w(), h());
    if (down) {
      fl_color(fl_color_average(GRAY, LIGHT, 0.5f));
      fl_rectf(x() + 1, y() + 1, w() - 2, h() - 2);
    }
    if (down || hover) {   // flat until the pointer is over it (or it is pressed / switched on)
      fl_color(down ? SHADOW : LIGHT); fl_xyline(x(), y(), x() + w() - 1); fl_yxline(x(), y(), y() + h() - 1);
      fl_color(down ? LIGHT : SHADOW); fl_xyline(x(), y() + h() - 1, x() + w() - 1); fl_yxline(x() + w() - 1, y(), y() + h() - 1);
    }
    int o = down ? 1 : 0;
    ex_icon(kind, x() + (w() - 24) / 2 + o, y() + 3 + o, active_r());
    fl_font(FONT_SMALL, SIZE_SMALL);
    fl_color(active_r() ? TEXT : fl_color_average(TEXT, GRAY, 0.45f));
    fl_draw(label(), x() + o, y() + 28 + o, w(), 14, FL_ALIGN_CENTER | FL_ALIGN_CLIP);
  }
  int handle(int e) override {
    if (e == FL_ENTER || e == FL_LEAVE) { redraw(); return 1; }
    return Fl_Button::handle(e);
  }
};

class ExBar : public Fl_Group {   // the button bar and the address bar
public:
  ExBar(int X, int Y, int W, int H) : Fl_Group(X, Y, W, H) { box(FL_FLAT_BOX); color(GRAY); }
  void draw() override {
    Fl_Group::draw();
    fl_color(LIGHT); fl_xyline(x(), y(), x() + w() - 1);
    fl_color(SHADOW); fl_xyline(x(), y() + h() - 1, x() + w() - 1);
  }
};

class ExSep : public Fl_Widget {   // an engraved line between groups of buttons
public:
  ExSep(int X, int Y, int H) : Fl_Widget(X, Y, 4, H) {}
  void draw() override {
    fl_color(SHADOW); fl_yxline(x() + 1, y() + 4, y() + h() - 5);
    fl_color(LIGHT); fl_yxline(x() + 2, y() + 4, y() + h() - 5);
  }
};

// ---------------------------------------------------------------------------
// The folder tree (loaded as it is opened)
// ---------------------------------------------------------------------------
struct TreeRoot { std::string label, path; Icon icon; };   // path "mount:sdb1": not mounted yet

class ExTree : public Fl_Tree {
public:
  Desktop *desk;
  NWin *win;
  std::deque<std::string> paths;   // user data of the items (stable addresses)
  bool quiet = false;              // selecting from the program: no navigation
  ExTree(int X, int Y, int W, int H, Desktop *d, NWin *w) : Fl_Tree(X, Y, W, H), desk(d), win(w) {
    showroot(0);
    selectmode(FL_TREE_SELECT_SINGLE);
    connectorstyle(FL_TREE_CONNECTOR_DOTTED);
    item_labelfont(FONT_SMALL);
    item_labelsize(SIZE_SMALL + 1);
    item_labelfgcolor(CONTENT_TEXT);
    color(CONTENT);
    selection_color(SEL);
    box(WELL_BOX);
    marginleft(2);
    linespacing(1);
    item_reselect_mode(FL_TREE_SELECTABLE_ALWAYS);
    callback([](Fl_Widget *w, void *) { ((ExTree *)w)->changed(); });
  }
  Fl_Tree_Item *add_dir(Fl_Tree_Item *parent, const std::string &label, const std::string &path, bool lazy = true,
                        int icon = -1) {
    // add(parent, name) - not add(path): a "/" in the name would make sub-items
    Fl_Tree_Item *it = add(parent ? parent : root(), label.c_str());
    if (!it) return nullptr;
    paths.push_back(path);
    it->user_data((void *)&paths.back());
    Entry e;
    e.dir = true;
    e.path = path;
    e.name = fs::path(path).filename().string();
    e.icon = icon >= 0 ? (Icon)icon : icon_for_path(path, true);
    if (Fl_Image *img = entry_icon_image(e, 16, false)) it->usericon(img);
    if (lazy && path.rfind("mount:", 0) != 0 && has_subdirs(path)) { add(it, "…"); it->close(); }
    return it;
  }
  static std::string path_of(Fl_Tree_Item *it) { return it && it->user_data() ? *(std::string *)it->user_data() : ""; }
  bool has_subdirs(const std::string &p) {
    std::error_code ec;
    for (auto it = fs::directory_iterator(p, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      std::string n = it->path().filename().string();
      if (!n.empty() && n[0] == '.') continue;
      if (it->is_directory(ec)) return true;
    }
    return false;
  }
  void populate(Fl_Tree_Item *it) {
    if (!it || it->children() != 1 || it->child(0)->user_data()) return;   // only the placeholder
    std::string p = path_of(it);
    clear_children(it);
    std::vector<std::string> dirs;
    std::error_code ec;
    for (auto d = fs::directory_iterator(p, fs::directory_options::skip_permission_denied, ec); !ec && d != fs::directory_iterator(); d.increment(ec)) {
      std::string n = d->path().filename().string();
      if (n.empty() || n[0] == '.') continue;
      if (d->is_directory(ec)) dirs.push_back(n);
    }
    std::sort(dirs.begin(), dirs.end(), [](const std::string &a, const std::string &b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
    for (auto &n : dirs) add_dir(it, n, p + "/" + n);
  }
  void changed() {
    Fl_Tree_Item *it = callback_item();
    if (!it) return;
    if (callback_reason() == FL_TREE_REASON_OPENED) { populate(it); redraw(); return; }
    if (callback_reason() == FL_TREE_REASON_SELECTED && !quiet) {
      std::string p = path_of(it);
      if (p.rfind("mount:", 0) == 0) {   // a disk that is not mounted: mount it, then show it
        std::string kname = p.substr(6);
        NWin *w = win;
        Desktop *d = desk;
        Desktop::later([d, w, kname] { d->ex_open_volume(w, kname); });
      } else if (!p.empty()) {
        desk->navigate(win, p);
      }
    }
  }
  // Nirvana HD, the disks (mounted or not), the servers, the Trash - each with its own icon
  void rebuild(const std::string &home, const std::string &trash, const std::vector<TreeRoot> &roots) {
    if (root()) clear_children(root());   // clear() would remove the root item too
    paths.clear();
    Fl_Tree_Item *h = add_dir(nullptr, "Nirvana HD", home, true, (int)Icon::Home);
    if (h) { populate(h); h->open(); }
    for (auto &r : roots) add_dir(nullptr, r.label, r.path, true, (int)r.icon);
    trash_item = add_dir(nullptr, _("Trash"), trash, false, (int)(desk->trash_is_full() ? Icon::TrashFull : Icon::Trash));
    redraw();
  }
  Fl_Tree_Item *trash_item = nullptr;
  void set_trash(bool full) {   // the Trash filled or emptied: its picture
    if (!trash_item) return;
    Entry e;
    e.dir = true;
    e.path = path_of(trash_item);
    e.icon = full ? Icon::TrashFull : Icon::Trash;
    if (Fl_Image *img = entry_icon_image(e, 16, false)) trash_item->usericon(img);
    redraw();
  }
  // show the folder: open the items down to it and select it (without navigating)
  void reveal(const std::string &path) {
    Fl_Tree_Item *best = nullptr;
    size_t best_len = 0;
    for (Fl_Tree_Item *it = first(); it; it = next(it)) {
      if (it->parent() != root()) continue;
      std::string p = path_of(it);
      if (!p.empty() && (path == p || path.rfind(p + "/", 0) == 0) && p.size() > best_len) { best = it; best_len = p.size(); }
    }
    quiet = true;
    deselect_all(nullptr, 0);
    Fl_Tree_Item *cur = best;
    while (cur && path_of(cur) != path) {
      populate(cur);
      cur->open();
      Fl_Tree_Item *next_it = nullptr;
      for (int i = 0; i < cur->children(); i++) {
        std::string c = path_of(cur->child(i));
        if (!c.empty() && (path == c || path.rfind(c + "/", 0) == 0)) { next_it = cur->child(i); break; }
      }
      if (!next_it) break;
      cur = next_it;
    }
    if (cur) {
      select(cur, 0);
      show_item(cur);
    }
    quiet = false;
    redraw();
  }
};

// ---------------------------------------------------------------------------
// A pane with its header (SOURCE / DESTINATION) - a click anywhere makes it the source
// ---------------------------------------------------------------------------
// a path for people: "~/..." for the home folder, "x.zip/..." inside an archive
std::string server_url(const std::string &path);
static std::string short_path(Desktop *d, const std::string &p) {
  std::string arch, inner, s = p;
  if (d->is_archive_cache(p, &arch, &inner)) s = arch + inner;
  std::string url = server_url(s);
  if (!url.empty()) return url;
  return disk_path(s);
}

class ExSlot : public Fl_Group {
public:
  Desktop *desk;
  NWin *win;
  int idx;
  ExSlot(int X, int Y, int W, int H, Desktop *d, NWin *w, int i) : Fl_Group(X, Y, W, H), desk(d), win(w), idx(i) {}
  bool header() const { return win->ex && win->ex->dual; }
  void draw() override {
    if (header()) {
      bool src = win->ex->active == idx;
      fl_color(GRAY);
      fl_rectf(x(), y(), w(), HEAD_H);
      const char *t = src ? _("SOURCE") : _("DESTINATION");
      fl_font(FONT_SYSTEM, SIZE_SMALL);
      int tw = (int)fl_width(t) + 12;
      fl_color(src ? SEL : fl_color_average(GRAY, SHADOW, 0.5f));
      fl_rectf(x() + 2, y() + 2, tw, HEAD_H - 4);
      fl_color(src ? SEL_TEXT : TEXT);
      fl_draw(t, x() + 2, y() + 2, tw, HEAD_H - 4, FL_ALIGN_CENTER);
      fl_font(FONT_SMALL, SIZE_SMALL);
      fl_color(TEXT);
      std::string p = win->ex->panes[idx] ? short_path(desk, win->ex->panes[idx]->path()) : "";
      fl_draw(ellipsize(p, w() - tw - 14).c_str(), x() + tw + 8, y(), w() - tw - 12, HEAD_H, FL_ALIGN_LEFT, nullptr, 0);   // no "@" symbols
    }
    draw_children();
  }
  int handle(int e) override {
    if (e == FL_PUSH && win->ex && win->ex->active != idx) desk->ex_activate(win, idx);
    return Fl_Group::handle(e);
  }
};

// ---------------------------------------------------------------------------
// The information panel: the selected file with its picture and its metadata
// ---------------------------------------------------------------------------
struct SlowMeta {
  std::string path;
  std::atomic<bool> ready{false};
  meta::Fields f;
};

class ExInfo : public Fl_Widget {
public:
  NWin *win;
  std::string key, name, kind, size, date, path;
  int count = 0;
  meta::Fields fields;
  std::shared_ptr<SlowMeta> slow;
  bool slow_added = false;
  Fl_Image *thumb = nullptr;
  Entry entry;
  ExInfo(int X, int Y, int W, int H, NWin *w) : Fl_Widget(X, Y, W, H), win(w) {}
  ~ExInfo() override { delete thumb; }
  void set(const std::vector<Entry> &sel, const std::string &folder, int items, uintmax_t total) {
    delete thumb;
    thumb = nullptr;
    fields.clear();
    slow.reset();
    slow_added = false;
    count = (int)sel.size();
    if (sel.size() != 1) {   // the folder, or several items
      entry = Entry();
      entry.dir = true;
      entry.path = folder;
      entry.icon = Icon::Folder;
      name = sel.empty() ? win->desk->folder_title(folder) : fmt(_("%1 items selected"), std::to_string(sel.size()));
      if (name.empty()) name = folder;
      kind = sel.empty() ? fmt(_("%1 items"), std::to_string(items)) : "";
      size = sel.empty() ? "" : human_size(total);
      date.clear();
      redraw();
      return;
    }
    entry = sel[0];
    name = entry.label.empty() ? entry.name : entry.label;
    sniff::Kind k = entry.dir ? sniff::Kind{} : sniff::of(entry.path);
    kind = entry.dir ? std::string(_("folder")) : (k.label.empty() ? kind_name(entry) : k.label);
    size = entry.dir ? "" : human_size(entry.size);
    date = entry.mtime ? plat::date_time(entry.mtime) : "";
    path = entry.path;
    if (!entry.dir) {
      fields = meta::read(entry.path, k);
      if (k.cat == "image" && sniff::allowed(entry.path) && entry.size < 40u * 1024 * 1024) {
        if (Fl_Shared_Image *si = Fl_Shared_Image::get(entry.path.c_str())) {   // the picture itself
          if (si->w() > 0 && si->h() > 0) {
            int mw = w() - 20, mh = 150;
            double sc = std::min({(double)mw / si->w(), (double)mh / si->h(), 1.0});
            thumb = si->copy(std::max(1, (int)(si->w() * sc)), std::max(1, (int)(si->h() * sc)));
          }
          si->release();
        }
      }
      if (k.cat == "video" || k.cat == "archive" || k.mime == "audio/mp4") {   // ffprobe / bsdtar: in a thread
        auto sm = std::make_shared<SlowMeta>();
        sm->path = entry.path;
        slow = sm;
        std::thread([sm, k] { sm->f = meta::read_slow(sm->path, k); sm->ready = true; }).detach();
      }
    }
    redraw();
  }
  void poll() {
    if (slow && slow->ready && !slow_added) {
      slow_added = true;
      fields.insert(fields.end(), slow->f.begin(), slow->f.end());
      redraw();
    }
  }
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_draw_box(WELL_BOX, x(), y(), w(), h(), CONTENT);
    int X = x() + 10, Y = y() + 10, W = w() - 20;
    if (thumb) {
      thumb->draw(x() + (w() - thumb->w()) / 2, Y);
      Y += thumb->h() + 8;
    } else {
      draw_entry_icon(entry, x() + (w() - 32) / 2, Y, false, 32);
      Y += 40;
    }
    fl_color(CONTENT_TEXT);
    fl_font(FONT_SYSTEM, SIZE_SYSTEM + 1);
    int ww = W, hh = 0;
    fl_measure(name.c_str(), ww, hh, 0);
    fl_draw(name.c_str(), X, Y, W, hh, FL_ALIGN_TOP_LEFT | FL_ALIGN_WRAP, nullptr, 0);
    Y += hh + 4;
    fl_color(SEL);
    fl_xyline(X, Y, X + W);
    Y += 6;
    auto row = [&](const std::string &label, const std::string &value) {
      if (value.empty() || Y > y() + h() - 14) return;
      fl_font(FONT_SMALL, SIZE_SMALL);
      fl_color(fl_color_average(CONTENT_TEXT, CONTENT, 0.55f));
      fl_draw(label.c_str(), X, Y + fl_height() - fl_descent(), 0, 0, FL_ALIGN_LEFT, nullptr, 0);
      Y += fl_height();
      fl_color(CONTENT_TEXT);
      fl_font(FONT_SMALL, SIZE_SMALL + 1);
      int vw = W, vh = 0;
      fl_measure(value.c_str(), vw, vh, 0);
      fl_draw(value.c_str(), X, Y, W, vh, FL_ALIGN_TOP_LEFT | FL_ALIGN_WRAP, nullptr, 0);
      Y += vh + 4;
    };
    row(_("Kind"), kind);
    row(_("Size"), size);
    row(_("Modified"), date);
    for (auto &[l, v] : fields) row(l, v);
    if (slow && !slow->ready) row("", _("Reading…"));
    fl_pop_clip();
  }
};

class ExStatus : public Fl_Widget {
public:
  NWin *win;
  ExStatus(int X, int Y, int W, int H, NWin *w) : Fl_Widget(X, Y, W, H), win(w) {}
  std::string text_left, text_right;
  void draw() override {
    fl_draw_box(FL_FLAT_BOX, x(), y(), w(), h(), GRAY);
    fl_color(SHADOW); fl_xyline(x(), y(), x() + w() - 1);
    fl_color(LIGHT); fl_xyline(x(), y() + 1, x() + w() - 1);
    int rw = text_right.empty() ? 0 : std::min(w() / 2, (int)(fl_width(text_right.c_str()) + 20));
    fl_draw_box(WELL_BOX, x() + 2, y() + 3, w() - rw - 6, h() - 5, GRAY);
    if (rw) fl_draw_box(WELL_BOX, x() + w() - rw - 2, y() + 3, rw, h() - 5, GRAY);
    fl_font(FONT_SMALL, SIZE_SMALL);
    fl_color(TEXT);
    fl_draw(ellipsize(text_left, w() - rw - 16).c_str(), x() + 8, y() + 3, w() - rw - 16, h() - 5, FL_ALIGN_LEFT, nullptr, 0);
    if (rw) fl_draw(ellipsize(text_right, rw - 10).c_str(), x() + w() - rw + 3, y() + 3, rw - 8, h() - 5, FL_ALIGN_LEFT, nullptr, 0);
  }
};

class ExSplitter : public Fl_Widget {   // between the tree and the files
public:
  Desktop *desk;
  NWin *win;
  int start_x = 0, start_w = 0;
  ExSplitter(int X, int Y, int W, int H, Desktop *d, NWin *w) : Fl_Widget(X, Y, W, H), desk(d), win(w) {}
  void draw() override { fl_color(GRAY); fl_rectf(x(), y(), w(), h()); }
  int handle(int e) override {
    switch (e) {
    case FL_ENTER: window()->cursor(FL_CURSOR_WE); return 1;
    case FL_LEAVE: window()->cursor(FL_CURSOR_DEFAULT); return 1;
    case FL_PUSH: start_x = Fl::event_x_root(); start_w = win->ex->tree_w; return 1;
    case FL_DRAG:
      win->ex->tree_w = std::clamp(start_w + Fl::event_x_root() - start_x, 120, win->w() / 2);
      desk->ex_layout(win);
      win->redraw();
      return 1;
    case FL_RELEASE: return 1;
    }
    return Fl_Widget::handle(e);
  }
};


static void status_tick(void *d);
static bool archive_mounted(const std::string &path);
class Desktop;
static void ex_restore_state(Desktop *d, NWin *w, const std::string &path);

// ---------------------------------------------------------------------------
// Building the window
// ---------------------------------------------------------------------------
std::vector<TreeRoot> ex_tree_roots(Desktop *d);

void Desktop::build_explorer(NWin *win, int ox, int oy, int W, int H, const std::string &path) {
  auto *x = new Explorer;
  win->ex = x;
  win->begin();
  // the panes first: the active one gets the keyboard focus when the window opens
  for (int i = 0; i < 2; i++) {
    x->slots[i] = new ExSlot(ox, oy, W, H, this, win, i);
    x->panes[i] = new FolderPane(ox, oy, W, H, path, show_hidden_);
    x->panes[i]->set_mode(FolderView::List);
    x->slots[i]->end();
    x->hist[i].path = path;
    setup_pane(win, x->panes[i], i);
  }
  x->slots[1]->hide();
  win->pane = x->panes[0];
  make_folder_menubar(win, ox, oy, W);

  // the button bar
  x->toolbar = new ExBar(ox, oy + NWin::MB_H, W, TOOL_H);
  int bx = ox + 4, by = oy + NWin::MB_H + 1, bh = TOOL_H - 2;
  auto button = [&](ExIcon k, const char *l, int bw, std::function<void(NWin *)> f) {
    fl_font(FONT_SMALL, SIZE_SMALL);   // wide enough for its label (Polish ones are longer)
    bw = std::max(bw, (int)fl_width(l) + 12);
    auto *b = new ExButton(bx, by, bw, bh, k, l);
    bx += bw + 1;
    auto *fp = new std::function<void(NWin *)>(std::move(f));   // lives as long as Rambler (a few per window)
    b->callback([](Fl_Widget *w, void *d) {
      auto *nw = (NWin *)w->window();
      nw->desk->menu_target = nw;
      (*(std::function<void(NWin *)> *)d)(nw);
      nw->desk->menu_target = nullptr;
    }, fp);
    return b;
  };
  auto sep = [&] { new ExSep(bx + 2, by, bh); bx += 8; };
  win->back_b = button(ExIcon::Back, _("Back"), 54, [this](NWin *) { go_back(); });
  win->fwd_b = button(ExIcon::Forward, _("Forward"), 54, [this](NWin *) { go_forward(); });
  win->up_b = button(ExIcon::Up, _("Up"), 46, [this](NWin *) { open_parent(); });
  sep();
  x->b_tree = button(ExIcon::Tree, _("Folders"), 54, [this](NWin *) { ex_toggle_tree(); });
  x->b_dual = button(ExIcon::Dual, _("Two Panes"), 66, [this](NWin *) { ex_toggle_dual(); });
  button(ExIcon::Swap, _("Swap"), 50, [this](NWin *) { ex_swap_panes(); });
  sep();
  x->b_copy = button(ExIcon::CopyTo, _("Copy To"), 62, [this](NWin *) { ex_copy_to_other(); });
  x->b_move = button(ExIcon::MoveTo, _("Move To"), 62, [this](NWin *) { ex_move_to_other(); });
  button(ExIcon::Delete, _("Delete"), 50, [this](NWin *) { move_to_trash(); });
  sep();
  button(ExIcon::Views, _("Views"), 50, [this](NWin *nw) {
    if (nw->pane->view()->mode() == FolderView::List) view_icons(); else view_list();
  });
  button(ExIcon::Server, _("Server"), 54, [this](NWin *) { connect_server(); });
  sep();
  button(ExIcon::Search, _("Search"), 50, [this](NWin *) { open_search(); });
  x->b_info = button(ExIcon::Info, _("Information"), 70, [this](NWin *) { ex_toggle_info(); });
  x->tools_end = bx - ox;
  x->logo = new ExLogo(ox + W - LOGO_W, oy + NWin::MB_H, LOGO_W, TOOL_H);
  x->toolbar->end();
  x->toolbar->resizable(nullptr);

  // the address bar
  x->addr = new ExBar(ox, oy + NWin::MB_H + TOOL_H, W, ADDR_H);
  x->addr_label = new Fl_Box(ox + 4, oy + NWin::MB_H + TOOL_H + 3, 52, ADDR_H - 6, _("Address"));
  x->addr_label->labelsize(SIZE_SMALL + 1);
  x->addr_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  auto *in = new Fl_Input(ox + 58, oy + NWin::MB_H + TOOL_H + 3, W - 58 - 74, ADDR_H - 6);
  in->box(WELL_BOX);
  in->textfont(FONT_SYSTEM);
  in->textsize(SIZE_SMALL + 1);
  in->when(FL_WHEN_ENTER_KEY_ALWAYS);
  in->callback([](Fl_Widget *w, void *) { auto *nw = (NWin *)w->window(); nw->desk->go_location(nw); });
  win->loc = in;
  x->go = new Fl_Button(ox + W - 70, oy + NWin::MB_H + TOOL_H + 3, 66, ADDR_H - 6, _("Go"));
  x->go->box(BUTTON_BOX);
  x->go->labelsize(SIZE_SMALL + 1);
  x->go->callback([](Fl_Widget *w, void *) { auto *nw = (NWin *)w->window(); nw->desk->go_location(nw); });
  x->filter_label = new Fl_Box(ox + W - 260, oy + NWin::MB_H + TOOL_H + 3, 44, ADDR_H - 6, _("Filter"));
  x->filter_label->labelsize(SIZE_SMALL + 1);
  x->filter_label->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
  x->filter = new Fl_Input(ox + W - 212, oy + NWin::MB_H + TOOL_H + 3, 140, ADDR_H - 6);
  x->filter->box(WELL_BOX);
  x->filter->textsize(SIZE_SMALL + 1);
  x->filter->tooltip(_("Show only matching files: a part of the name, *.jpg, or a kind - kind:picture, kind:sound, kind:film, kind:archive, kind:document, kind:text, kind:program"));
  x->filter->when(FL_WHEN_CHANGED);
  x->filter->callback([](Fl_Widget *w, void *) {
    auto *nw = (NWin *)w->window();
    nw->desk->ex_set_filter(nw, ((Fl_Input *)w)->value());
  });
  x->addr->end();
  x->addr->resizable(in);

  // the tree, the splitter, the status bar
  x->tree = new ExTree(ox, oy, 100, 100, this, win);
  x->info = new ExInfo(ox, oy, 100, 100, win);
  x->info->hide();
  x->splitter = new ExSplitter(ox, oy, SPLIT_W, 100, this, win);
  x->status = new ExStatus(ox, oy, W, STATUS_H, win);
  auto *rz = new Fl_Box(ox + W / 2, oy + H / 2, 1, 1);   // resizing: ex_layout places everything
  win->resizable(rz);
  win->end();

  x->tree->rebuild(home_, trash_dir(), ex_tree_roots(this));
  ex_restore_state(this, win, path);
  win->copy_label(title_for(win->path).c_str());   // the source pane's folder (it may be another after the restore)
  update_nav(win);
  ex_layout(win);
  Fl::add_timeout(0.3, status_tick, win);
}

void Desktop::ex_layout(NWin *w) {
  Explorer *x = w->ex;
  if (!x || !x->toolbar) return;
  int ox = w->ox(), oy = w->oy();
  int W = w->w() - w->extra_w(), H = w->h() - w->extra_h();
  if (w->menubar) w->menubar->resize(ox, oy, W, NWin::MB_H);
  x->toolbar->resize(ox, oy + NWin::MB_H, W, TOOL_H);
  if (x->logo) {   // at the right end - hidden when the window is too narrow for it
    x->logo->resize(ox + W - LOGO_W, oy + NWin::MB_H, LOGO_W, TOOL_H);
    if (W - LOGO_W >= x->tools_end + 4) x->logo->show(); else x->logo->hide();
  }
  x->addr->resize(ox, oy + NWin::MB_H + TOOL_H, W, ADDR_H);
  x->addr_label->resize(ox + 4, oy + NWin::MB_H + TOOL_H + 3, 52, ADDR_H - 6);
  int fy = oy + NWin::MB_H + TOOL_H + 3;
  x->filter->resize(ox + W - 150, fy, 146, ADDR_H - 6);
  x->filter_label->resize(ox + W - 198, fy, 44, ADDR_H - 6);
  x->go->resize(ox + W - 268, fy, 66, ADDR_H - 6);
  w->loc->resize(ox + 58, fy, std::max(40, W - 58 - 274), ADDR_H - 6);
  int top = oy + NWin::MB_H + TOOL_H + ADDR_H + 2;
  int bodyh = std::max(40, H - (top - oy) - STATUS_H);
  int px = ox;
  if (x->tree_on || x->info_on) {   // the left column: the tree, the information panel, or both
    int tw = std::clamp(x->tree_w, 120, std::max(120, W / 2));
    int th = x->tree_on && x->info_on ? bodyh * 45 / 100 : bodyh;
    if (x->tree_on) { x->tree->resize(ox + 2, top, tw, th); x->tree->show(); } else x->tree->hide();
    if (x->info_on) {
      int iy = x->tree_on ? top + th + 3 : top;
      x->info->resize(ox + 2, iy, tw, top + bodyh - iy);
      x->info->show();
    } else x->info->hide();
    x->splitter->resize(ox + 2 + tw, top, SPLIT_W, bodyh);
    x->splitter->show();
    px = ox + 2 + tw + SPLIT_W;
  } else {
    x->tree->hide();
    x->info->hide();
    x->splitter->hide();
    px = ox + 2;
  }
  int pw = ox + W - 2 - px;
  int hh = x->dual ? HEAD_H : 0;
  auto place = [&](int i, int X, int PW) {
    x->slots[i]->resize(X, top, PW, bodyh);
    x->panes[i]->resize(X, top + hh, PW, bodyh - hh);
  };
  if (x->dual) {
    int a = (pw - 4) / 2;
    place(0, px, a);
    place(1, px + a + 4, pw - a - 4);
    x->slots[1]->show();
  } else {
    place(0, px, pw);
    x->slots[1]->hide();
  }
  x->status->resize(ox, oy + H - STATUS_H, W, STATUS_H);
  ((ExButton *)x->b_tree)->toggle_on = x->tree_on;
  ((ExButton *)x->b_info)->toggle_on = x->info_on;
  ((ExButton *)x->b_dual)->toggle_on = x->dual;
  if (x->dual) { x->b_copy->activate(); x->b_move->activate(); }
  else { x->b_copy->deactivate(); x->b_move->deactivate(); }
  w->redraw();
}

FolderPane *Desktop::ex_pane_at(NWin *w, int lx, int ly) {
  Explorer *x = w->ex;
  if (!x) return nullptr;
  for (int i = 0; i < 2; i++) {
    Fl_Group *s = x->slots[i];
    if (s->visible() && lx >= s->x() && ly >= s->y() && lx < s->x() + s->w() && ly < s->y() + s->h()) return x->panes[i];
  }
  return nullptr;
}

void Desktop::ex_activate(NWin *w, int idx) {
  Explorer *x = w->ex;
  if (!x || idx < 0 || idx > 1 || (idx == 1 && !x->dual)) return;
  if (x->active == idx && w->pane == x->panes[idx]) return;
  x->hist[x->active] = {w->path, w->back_hist, w->fwd_hist};
  x->active = idx;
  w->pane = x->panes[idx];
  w->path = x->hist[idx].path;
  w->back_hist = x->hist[idx].back;
  w->fwd_hist = x->hist[idx].fwd;
  w->copy_label(title_for(w->path).c_str());
  if (x->filter) x->filter->value(x->filters[idx].c_str());
  update_nav(w);
  x->slots[0]->redraw();
  x->slots[1]->redraw();
}

void Desktop::ex_after_nav(NWin *w) {
  Explorer *x = w->ex;
  if (!x) return;
  x->hist[x->active].path = w->path;
  if (x->tree && x->tree_on) x->tree->reveal(w->path);
  x->slots[0]->redraw();
  x->slots[1]->redraw();
  x->status_key.clear();   // the status bar follows at once
  later([this] { release_archives(); ex_save_state(); });
}

bool Desktop::ex_key(NWin *w) {
  Explorer *x = w->ex;
  if (!x || Fl::focus() == w->loc) return false;
  int k = Fl::event_key();
  int mods = Fl::event_state() & (FL_SHIFT | FL_CTRL | FL_ALT | FL_META);
  if (mods) return false;
  menu_target = w;
  bool done = true;
  if (k == FL_Tab && x->dual) { ex_activate(w, 1 - x->active); w->pane->view()->take_focus(); }
  else if (k == FL_F + 5) ex_copy_to_other();
  else if (k == FL_F + 6) ex_move_to_other();
  else if (k == FL_F + 7) new_folder();
  else if (k == FL_F + 8) move_to_trash();
  else done = false;
  menu_target = nullptr;
  return done;
}

void Desktop::ex_toggle_tree() {
  NWin *w = active_folder();
  if (!w || !w->ex) return;
  w->ex->tree_on = !w->ex->tree_on;
  ex_layout(w);
  if (w->ex->tree_on) w->ex->tree->reveal(w->path);
}

void Desktop::ex_toggle_dual() {
  NWin *w = active_folder();
  if (!w || !w->ex) return;
  Explorer *x = w->ex;
  if (x->dual && x->active == 1) ex_activate(w, 0);
  x->dual = !x->dual;
  if (x->dual && x->hist[1].path.empty()) x->hist[1].path = w->path;
  ex_layout(w);
}

void Desktop::ex_swap_panes() {   // the other pane becomes the source
  NWin *w = active_folder();
  if (!w || !w->ex) return;
  if (!w->ex->dual) { ex_toggle_dual(); return; }
  ex_activate(w, 1 - w->ex->active);
  w->pane->view()->take_focus();
}

void Desktop::ex_copy_to_other() {
  NWin *w = active_folder();
  if (!w || !w->ex || !w->ex->dual) return;
  auto sel = w->pane->view()->selection();
  if (sel.empty()) return;
  transfer(sel, w->ex->panes[1 - w->ex->active]->path(), true, false);
}
void Desktop::ex_move_to_other() {
  NWin *w = active_folder();
  if (!w || !w->ex || !w->ex->dual) return;
  auto sel = w->pane->view()->selection();
  if (sel.empty()) return;
  transfer(sel, w->ex->panes[1 - w->ex->active]->path(), false, false);
}

// the status bar: the items and the selection of the source; source -> destination
static void status_tick(void *d) {
  auto *w = (NWin *)d;
  Explorer *x = w->ex;
  if (!x || !x->status) return;
  FolderView *v = w->pane->view();
  auto sel = v->selection();
  uintmax_t total = 0;
  for (auto &e : sel) total += e.size;
  std::string key = w->path + "|" + std::to_string(v->count()) + "|" + std::to_string(sel.size()) + "|" +
                    std::to_string(total) + "|" + std::to_string(x->dual) + std::to_string(x->active) +
                    std::to_string(x->tree_on) + std::to_string(x->info_on) + "|" + std::to_string(x->tree_w) + "|" +
                    x->panes[1 - x->active]->path() + "|" + std::to_string(jobs::running());
  if (key != x->status_key) {
    x->status_key = key;
    auto *st = (ExStatus *)x->status;
    if (sel.empty()) st->text_left = fmt(_("%1 items"), std::to_string(v->count()));
    else st->text_left = fmt(_("%1 of %2 selected (%3)"), std::to_string(sel.size()), std::to_string(v->count()), human_size(total));
    std::string arch;
    if (w->desk->is_archive_cache(w->path, &arch))
      st->text_left += "   " + fmt(archive_mounted(w->path) ? _("Inside the archive “%1” - changes are saved into it when you leave it")
                                                         : _("Inside the archive “%1” - a copy: changes do not go back into it"),
                                  fs::path(arch).filename().string());
    st->text_right = x->dual ? fmt(_("Copy/Move to: %1"), short_path(w->desk, x->panes[1 - x->active]->path())) : std::string();
    if (jobs::running()) st->text_right = fmt(_("Jobs: %1"), std::to_string(jobs::running())) + (st->text_right.empty() ? "" : "   " + st->text_right);
    if (!x->filters[x->active].empty()) st->text_left += "   " + fmt(_("(filter: %1)"), x->filters[x->active]);
    st->redraw();
    if (x->info_on) ((ExInfo *)x->info)->set(sel, w->path, v->count(), total);
    std::string sk = key;
    if (sk != x->sel_key) { x->sel_key = sk; w->desk->ex_save_state(); }
  }
  if (x->info_on) ((ExInfo *)x->info)->poll();
  Fl::repeat_timeout(0.3, status_tick, d);
}

// ---------------------------------------------------------------------------
// Archives: opened like folders (unpacked into a cache with bsdtar)
// ---------------------------------------------------------------------------
static std::string cache_root() {
  const char *x = getenv("XDG_CACHE_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/tmp") + "/.cache") + "/nirvana/archives";
}

bool Desktop::is_archive_cache(const std::string &path, std::string *archive, std::string *inner) const {
  static bool scanned = false;
  if (!scanned) {   // after a restart: the unpacked archives are known by their marker files
    scanned = true;
    std::ifstream mounts("/proc/mounts");   // and archives still mounted (archivemount)
    std::string dev, dir, type, rest;
    while (mounts >> dev >> dir >> type) {
      std::getline(mounts, rest);
      if (type == "fuse.archivemount" && dir.rfind(cache_root() + "/", 0) == 0) {
        archive_dirs_[dir] = dev;
        g_archive_mounts.insert(dir);
      }
    }
    std::error_code ec;
    for (auto it = fs::directory_iterator(cache_root(), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      std::ifstream m(it->path().string() + "/.nirvana-unpacked");
      std::string arch;
      if (std::getline(m, arch) && !arch.empty()) archive_dirs_[it->path().string()] = arch;
    }
  }
  for (auto &[dir, arch] : archive_dirs_)
    if (path == dir || path.rfind(dir + "/", 0) == 0) {
      if (archive) *archive = arch;
      if (inner) *inner = path.substr(dir.size());
      return true;
    }
  return false;
}

struct Unpack { Desktop *d; NWin *w; std::string dir, archive; pid_t pid; int fd; std::string err; };

static bool have_prog(const char *p);

// Formats that can be written back (libarchive writes them); the others open as a copy
// What is inside an opened archive (names, sizes, times): taken when it opens and compared
// when it is left - the archive is written again only when something changed
static std::map<std::string, std::string> g_archive_snap;
static std::string archive_snapshot(const std::string &mp) {
  std::string o;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(mp, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    struct stat st;
    if (lstat(it->path().c_str(), &st) != 0) continue;
    o += it->path().string().substr(mp.size()) + "\t" + std::to_string(st.st_mode) + "\t" + std::to_string(st.st_size) + "\t" +
         std::to_string(st.st_mtime) + "\n";
  }
  return o;
}

static bool writable_archive(const std::string &name) {
  std::string l = name;
  for (auto &c : l) c = (char)tolower((unsigned char)c);
  for (const char *e : {".zip", ".tar", ".tar.gz", ".tgz", ".tar.bz2", ".tbz2", ".tar.xz", ".txz", ".7z", ".cpio"})
    if (l.size() > strlen(e) && l.compare(l.size() - strlen(e), strlen(e), e) == 0) return true;
  return false;
}

static bool archive_mounted(const std::string &path) {
  for (auto &mp : g_archive_mounts)
    if (path == mp || path.rfind(mp + "/", 0) == 0) return true;
  return false;
}

void Desktop::browse_archive(NWin *w, const std::string &archive) {
  struct stat st;
  if (stat(archive.c_str(), &st) != 0) return;
  std::string name = fs::path(archive).filename().string();
  // Transparent: archivemount shows the archive as a folder that can be changed - when the
  // last window leaves it, the changes are written into the archive (release_archives).
  if (writable_archive(name) && access(archive.c_str(), W_OK) == 0 && have_prog("archivemount")) {
    std::string mp = cache_root() + "/" + name + "-rw-" + std::to_string((unsigned long)std::hash<std::string>()(archive) % 100000000);
    std::error_code ec;
    if (!g_archive_mounts.count(mp)) {
      fs::create_directories(mp, ec);
      // nosave: archivemount itself would write every archive back as TAR - Rambler packs it
      // again itself, in the format of its name (bsdtar -a), when it is left
      std::string err = run_with_input({"archivemount", "-o", "nosave", archive, mp}, "");
      if (!err.empty()) { guru(fmt(_("The archive “%1” could not be opened.\n%2"), name, err)); return; }
      g_archive_mounts.insert(mp);
      g_archive_snap[mp] = archive_snapshot(mp);
    }
    archive_dirs_[mp] = archive;
    if (w && known(w)) navigate(w, mp); else open_folder(mp);
    return;
  }
  std::string dir = cache_root() + "/" + name + "-" + std::to_string((unsigned long)std::hash<std::string>()(archive) % 100000000) +
                    "-" + std::to_string((long)st.st_mtime);
  archive_dirs_[dir] = archive;
  std::error_code ec;
  if (fs::is_directory(dir + "/.done", ec) || fs::exists(dir + "/.nirvana-unpacked", ec)) {
    if (w && known(w)) navigate(w, dir); else open_folder(dir);
    return;
  }
  fs::create_directories(dir, ec);
  int p[2];
  if (pipe(p) != 0) return;
  pid_t pid = fork();
  if (pid == 0) {
    dup2(p[1], 2);
    dup2(p[1], 1);
    execlp("bsdtar", "bsdtar", "-xf", archive.c_str(), "-C", dir.c_str(), (char *)nullptr);
    _exit(127);
  }
  ::close(p[1]);
  auto *u = new Unpack{this, w, dir, archive, pid, p[0], ""};
  Fl::add_fd(p[0], FL_READ, [](int fd, void *data) {
    auto *u = (Unpack *)data;
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof buf);
    if (n > 0) { u->err.append(buf, n); return; }
    Fl::remove_fd(fd);
    ::close(fd);
    int st = 0;
    waitpid(u->pid, &st, 0);
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    if (code == 0) {
      std::ofstream(u->dir + "/.nirvana-unpacked") << u->archive << "\n";
      if (u->w && u->d->is_known(u->w)) u->d->navigate(u->w, u->dir); else u->d->open_folder(u->dir);
    } else {
      std::string msg = code == 127 ? std::string(_("bsdtar is not installed (package libarchive-tools).")) : u->err;
      u->d->error_box(fmt(_("The archive “%1” could not be opened.\n%2"), fs::path(u->archive).filename().string(), msg));
    }
    delete u;
  }, u);
}

// Icons > Pack: the selection into an archive next to it (zip, tar.gz, tar.xz, 7z by the name)
void Desktop::pack_selected() {
  NWin *w = active_folder();
  if (!w) return;
  auto sel = w->pane->view()->selection();
  if (sel.empty()) return;
  std::string base = sel.size() == 1 ? sel[0].name : fs::path(w->path).filename().string();
  if (base.empty()) base = "archive";
  int W = 440, H = 170, ox, oy;
  NWin *f = begin_form(_("Pack into an Archive"), W, H, nullptr,
                       fmt(_("Pack %1 items into the archive (the ending chooses the kind: .zip, .tar.gz, .tar.xz, .7z):"),
                           std::to_string(sel.size())), ox, oy);
  auto *in = new Fl_Input(ox + 64, oy + 62, W - 80, 24);
  in->box(WELL_BOX);
  in->textsize(SIZE_SYSTEM);
  in->value((base + ".zip").c_str());
  in->insert_position(0, (int)base.size());
  std::string dir = w->path;
  end_form(f, W, H, ox, oy, {{_("Cancel"), nullptr}, {_("Pack"), [this, in, sel, dir] {
    std::string name = in->value();
    if (name.empty()) return;
    std::string out = name[0] == '/' ? name : dir + "/" + name;
    std::vector<std::string> args = {"bsdtar", "-a", "-cf", out, "-C", dir};
    for (auto &e : sel) args.push_back(fs::path(e.path).filename().string());
    std::vector<char *> av;
    for (auto &a : args) av.push_back((char *)a.c_str());
    av.push_back(nullptr);
    pid_t pid = fork();
    if (pid == 0) { execvp(av[0], av.data()); _exit(127); }
    int st = 0;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
      guru(WIFEXITED(st) && WEXITSTATUS(st) == 127 ? _("bsdtar is not installed (package libarchive-tools).")
                                                     : fmt(_("The archive “%1” could not be made."), name));
  }}});
  in->take_focus();
}

// ---------------------------------------------------------------------------
// Servers: SFTP through sshfs (or gio), FTP through gio (gvfs) - shown like folders
// ---------------------------------------------------------------------------
static std::string remote_root() {
  const char *x = getenv("XDG_CACHE_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/tmp") + "/.cache") + "/nirvana/servers";
}

// A folder on a server as an address people know: "sftp://me@host/dir", "ftp://host/dir",
// "smb://host/share/dir" ("" when the path is not on a server)
static std::string gvfs_param(const std::string &n, const std::string &key) {
  size_t p = n.find(key + "=");
  if (p == std::string::npos) return "";
  p += key.size() + 1;
  return n.substr(p, n.find(',', p) == std::string::npos ? std::string::npos : n.find(',', p) - p);
}
std::string server_url(const std::string &path) {
  std::string root = remote_root();
  if (path.rfind(root + "/", 0) == 0) {   // sshfs: <root>/user@host[:port]
    std::string rest = path.substr(root.size() + 1);
    size_t sl = rest.find('/');
    return "sftp://" + rest.substr(0, sl) + "/" + (sl == std::string::npos ? "" : rest.substr(sl + 1));
  }
  std::string gvfs = "/run/user/" + std::to_string(getuid()) + "/gvfs/";
  if (path.rfind(gvfs, 0) != 0) return "";
  std::string rest = path.substr(gvfs.size());
  size_t sl = rest.find('/');
  std::string n = rest.substr(0, sl), inner = sl == std::string::npos ? "" : rest.substr(sl + 1);
  std::string user = gvfs_param(n, "user");
  std::string at = user.empty() ? "" : user + "@";
  if (n.rfind("smb-share:", 0) == 0)
    return "smb://" + at + gvfs_param(n, "server") + "/" + gvfs_param(n, "share") + "/" + inner;
  size_t c = n.find(':');
  if (c == std::string::npos) return "";
  std::string port = gvfs_param(n, "port");
  return n.substr(0, c) + "://" + at + gvfs_param(n, "host") + (port.empty() ? "" : ":" + port) + "/" + inner;
}

// The other way: an address typed in the location field -> the folder, if that server is connected
std::string Desktop::server_path(const std::string &url) const {
  if (url.find("://") == std::string::npos) return "";
  std::string u = url;
  while (u.size() > 1 && u.back() == '/') u.pop_back();
  for (auto &[label, dir] : servers()) {
    std::string base = server_url(dir);
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.empty()) continue;
    if (u == base) return dir;
    if (u.rfind(base + "/", 0) == 0) return dir + u.substr(base.size());
    // without the user: "ftp://host/x" for "ftp://me@host/x"
    size_t at = base.find('@'), sch = base.find("://");
    if (at != std::string::npos && sch != std::string::npos) {
      std::string b2 = base.substr(0, sch + 3) + base.substr(at + 1);
      if (u == b2) return dir;
      if (u.rfind(b2 + "/", 0) == 0) return dir + u.substr(b2.size());
    }
  }
  return "";
}

std::vector<std::pair<std::string, std::string>> Desktop::servers() const {
  std::vector<std::pair<std::string, std::string>> v;
  std::ifstream m("/proc/mounts");
  std::string dev, dir, type, rest;
  std::string root = remote_root();
  while (m >> dev >> dir >> type) {
    std::getline(m, rest);
    if (dir.rfind(root + "/", 0) == 0) {   // sshfs: user@host:/path
      std::string host = dev.substr(dev.find('@') == std::string::npos ? 0 : dev.find('@') + 1);
      host = host.substr(0, host.find(':'));
      v.push_back({host + " (SFTP)", dir});
    }
  }
  std::string gvfs = "/run/user/" + std::to_string(getuid()) + "/gvfs";
  std::error_code ec;
  for (auto it = fs::directory_iterator(gvfs, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    std::string n = it->path().filename().string();   // ftp:host=example.org,user=me
    if (n.rfind("ftp:", 0) == 0 || n.rfind("sftp:", 0) == 0 || n.rfind("smb-share:", 0) == 0) {
      size_t hp = n.find("host=") != std::string::npos ? n.find("host=") + 5 : n.find("server=") != std::string::npos ? n.find("server=") + 7 : 0;
      std::string host = n.substr(hp);
      host = host.substr(0, host.find(','));
      if (n.rfind("smb-share:", 0) == 0) {   // "smb-share:server=h,share=s" -> "h/s (SMB)"
        size_t sh = n.find("share=");
        if (sh != std::string::npos) host += "/" + n.substr(sh + 6, n.find(',', sh) - sh - 6);
        v.push_back({host + " (SMB)", it->path().string()});
        continue;
      }
      std::string proto = n.substr(0, n.find(':'));
      for (auto &c : proto) c = (char)toupper((unsigned char)c);
      v.push_back({host + " (" + proto + ")", it->path().string()});
    }
  }
  return v;
}

void Desktop::disconnect_server(const std::string &mp) {
  pid_t pid = fork();
  if (pid == 0) {
    int dn = open("/dev/null", O_WRONLY);
    if (dn >= 0) { dup2(dn, 1); dup2(dn, 2); }
    if (mp.find("/gvfs/") != std::string::npos) {
      std::string n = fs::path(mp).filename().string();   // ftp:host=h,user=u -> ftp://u@h/
      std::string proto = n.substr(0, n.find(':'));
      auto val = [&](const char *k) {
        size_t a = n.find(std::string(k) + "=");
        if (a == std::string::npos) return std::string();
        a += strlen(k) + 1;
        return n.substr(a, n.find(',', a) - a);
      };
      std::string u = val("user"), uri = proto + "://" + (u.empty() ? "" : u + "@") + val("host") + "/";
      execlp("gio", "gio", "mount", "-u", uri.c_str(), (char *)nullptr);
    } else {
      execlp("fusermount3", "fusermount3", "-u", mp.c_str(), (char *)nullptr);
      execlp("fusermount", "fusermount", "-u", mp.c_str(), (char *)nullptr);
    }
    _exit(127);
  }
  int st;
  waitpid(pid, &st, 0);
  for (auto *w : wins_)
    if (w->pane && (w->path == mp || w->path.rfind(mp + "/", 0) == 0)) navigate(w, home_);
}

static bool have_prog(const char *p) {
  std::string c = std::string("command -v ") + p + " >/dev/null 2>&1";
  return system(c.c_str()) == 0;
}

// run a mounting program, the password on its standard input; "" or the error output.
// The output goes to a file, not a pipe: sshfs leaves a process in the background that
// would keep a pipe open - we wait only for the program we started.
static std::string run_with_input(const std::vector<std::string> &args, const std::string &input) {
  int in[2];
  if (pipe(in) != 0) return strerror(errno);
  char tmpl[] = "/tmp/nirvana-mount-XXXXXX";
  int out = mkstemp(tmpl);
  if (out < 0) return strerror(errno);
  unlink(tmpl);
  pid_t pid = fork();
  if (pid == 0) {
    dup2(in[0], 0);
    dup2(out, 1);
    dup2(out, 2);
    ::close(in[1]);
    setsid();   // no terminal: ssh and gio read the password from stdin
    std::vector<char *> av;
    for (auto &a : args) av.push_back((char *)a.c_str());
    av.push_back(nullptr);
    execvp(av[0], av.data());
    _exit(127);
  }
  ::close(in[0]);
  if (!input.empty()) { ssize_t r = write(in[1], input.data(), input.size()); (void)r; }
  ::close(in[1]);
  int st = 0;
  waitpid(pid, &st, 0);
  std::string o;
  lseek(out, 0, SEEK_SET);
  char buf[1024];
  ssize_t n;
  while ((n = read(out, buf, sizeof buf)) > 0) o.append(buf, n);
  ::close(out);
  int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
  if (code == 0) return "";
  if (code == 127) return "127";
  return o.empty() ? "error " + std::to_string(code) : o;
}

// gio mount, answering its questions (user, domain, password) as they come
static std::string gio_mount(const std::string &uri, const std::string &user, const std::string &domain,
                             const std::string &pw, bool anonymous) {
  int in[2], out[2];
  if (pipe(in) != 0 || pipe(out) != 0) return strerror(errno);
  pid_t pid = fork();
  if (pid == 0) {
    dup2(in[0], 0);
    dup2(out[1], 1);
    dup2(out[1], 2);
    ::close(in[1]);
    ::close(out[0]);
    setsid();
    if (anonymous) execlp("gio", "gio", "mount", "-a", uri.c_str(), (char *)nullptr);
    else execlp("gio", "gio", "mount", uri.c_str(), (char *)nullptr);
    _exit(127);
  }
  ::close(in[0]);
  ::close(out[1]);
  std::string o, pending;
  char buf[512];
  int answered = 0;
  for (;;) {
    struct pollfd pf = {out[0], POLLIN, 0};
    if (poll(&pf, 1, 20000) <= 0) { kill(pid, SIGTERM); o += "timeout"; break; }
    ssize_t n = read(out[0], buf, sizeof buf);
    if (n <= 0) break;
    o.append(buf, n);
    pending.append(buf, n);
    auto reply = [&](const std::string &v) { std::string l = v + "\n"; ssize_t r = write(in[1], l.data(), l.size()); (void)r; pending.clear(); answered++; };
    if (answered > 6) { kill(pid, SIGTERM); break; }   // asked again and again: the password is wrong
    if (pending.find("User") != std::string::npos) reply(user);
    else if (pending.find("Domain") != std::string::npos) reply(domain);
    else if (pending.find("Password") != std::string::npos) reply(pw);
  }
  ::close(in[1]);
  ::close(out[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
  if (code == 0 || o.find("already mounted") != std::string::npos) return "";
  if (code == 127) return "127";
  return o.empty() ? "error " + std::to_string(code) : o;
}

// Connecting (also from a job's thread): the folder it appears in comes back in mp
static std::string mount_server(const ServerSpec &sp, const std::string &pw, std::string &mp) {
  bool sftp = sp.kind == "sftp";
  std::string err;
  if (sftp && have_prog("sshfs")) {
    mp = remote_root() + "/" + (sp.user.empty() ? "" : sp.user + "@") + sp.host;
    std::error_code ec;
    fs::create_directories(mp, ec);
    std::vector<std::string> a = {"sshfs", (sp.user.empty() ? "" : sp.user + "@") + sp.host + ":" + sp.folder, mp, "-o",
                                  "StrictHostKeyChecking=accept-new,reconnect,ServerAliveInterval=15"};
    if (!sp.port.empty()) { a.push_back("-p"); a.push_back(sp.port); }
    if (!pw.empty()) { a.push_back("-o"); a.push_back("password_stdin"); }
    else { a.push_back("-o"); a.push_back("BatchMode=yes"); }
    err = run_with_input(a, pw.empty() ? "" : pw + "\n");
    if (err == "127") return _("sshfs is not installed (package sshfs).");
    // sshfs goes into the background before the folder is ready: wait until it answers
    struct stat a1, a2;
    for (int i = 0; err.empty() && i < 50; i++) {
      bool mounted = stat(mp.c_str(), &a1) == 0 && stat(fs::path(mp).parent_path().c_str(), &a2) == 0 && a1.st_dev != a2.st_dev;
      int entries = 0;
      if (mounted)
        if (DIR *dd = opendir(mp.c_str())) { while (readdir(dd)) entries++; closedir(dd); }
      if (entries > 2) break;
      usleep(100000);
    }
    return err;
  }
  if (!have_prog("gio")) return sftp ? _("sshfs is not installed (package sshfs).") : _("gio is not installed (packages gvfs-backends and gvfs-fuse).");
  // FTP, SMB (and SFTP without sshfs) through gvfs
  std::string user = sp.user, domain = "WORKGROUP";
  if (size_t bs = user.find('\\'); bs != std::string::npos) { domain = user.substr(0, bs); user = user.substr(bs + 1); }
  std::string folder = sp.folder;
  while (!folder.empty() && folder[0] == '/') folder.erase(0, 1);
  std::string share = sp.kind == "smb" ? folder.substr(0, folder.find('/')) : "";
  std::string uri = sp.kind + "://" + (user.empty() ? "" : user + "@") + sp.host + (sp.port.empty() ? "" : ":" + sp.port) + "/" + share;
  err = gio_mount(uri, user, domain, pw, user.empty() && pw.empty() && sp.kind != "smb");
  if (err == "127") return _("gio is not installed (packages gvfs-backends and gvfs-fuse).");
  if (!err.empty()) return err;
  // the folder under /run/user/UID/gvfs: "smb-share:server=h,share=s", "ftp:host=h,user=u"
  std::string gvfs = "/run/user/" + std::to_string(getuid()) + "/gvfs";
  std::error_code ec;
  for (int i = 0; i < 30 && mp.empty(); i++) {
    for (auto it = fs::directory_iterator(gvfs, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      std::string n = it->path().filename().string();
      if (n.find(sp.host) != std::string::npos && n.rfind(sp.kind == "smb" ? "smb-share:" : sp.kind + ":", 0) == 0) {
        mp = it->path().string();
        std::string rest = sp.kind == "smb" ? (folder.find('/') == std::string::npos ? "" : folder.substr(folder.find('/'))) : (folder.empty() ? "" : "/" + folder);
        mp += rest;
      }
    }
    if (mp.empty()) usleep(100000);
  }
  if (mp.empty()) return _("The server is connected, but its folder cannot be seen - is gvfs-fuse installed?");
  return "";
}

void Desktop::connect_server_to(const std::vector<std::string> &spec, const std::string &note) {
  int W = 500, H = 360, ox, oy;
  NWin *f = begin_form(_("Connect to a Server"), W, H, nullptr,
                       note.empty() ? _("The server's folders open like folders on this computer, and the server appears on the desktop like a disk.")
                                    : note.c_str(), ox, oy);
  auto row = [&](int y, const char *l) {
    auto *b = new Fl_Box(ox + 64, oy + y, 110, 24, l);
    b->labelsize(SIZE_SMALL + 1);
    b->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
  };
  row(56, _("Kind:"));
  auto *kind = new Fl_Choice(ox + 178, oy + 56, 240, 24);
  kind->add("SFTP (SSH)");
  kind->add("FTP");
  kind->add(_("SMB (shared folders)"));
  kind->value(0);
  kind->textsize(SIZE_SMALL + 1);
  row(86, _("Server:"));
  auto *host = new Fl_Input(ox + 178, oy + 86, 230, 24);
  auto *port = new Fl_Input(ox + 412, oy + 86, 60, 24);
  port->tooltip(_("Port (empty: the usual one)"));
  row(116, _("User:"));
  auto *user = new Fl_Input(ox + 178, oy + 116, 294, 24);
  user->tooltip(_("For SMB also DOMAIN\\user"));
  const char *u = getenv("USER");
  user->value(u ? u : "");
  row(146, _("Password:"));
  auto *pass = new Fl_Secret_Input(ox + 178, oy + 146, 294, 24);
  pass->tooltip(_("Empty: an SSH key or an anonymous FTP login"));
  row(176, _("Folder:"));
  auto *folder = new Fl_Input(ox + 178, oy + 176, 294, 24);
  folder->tooltip(_("For SMB: the share name, e.g. Documents"));
  for (Fl_Input *i : {host, port, user, (Fl_Input *)pass, folder}) { i->box(WELL_BOX); i->textsize(SIZE_SMALL + 1); }
  bool prefilled = spec.size() >= 5;
  if (prefilled) {   // a remembered server: everything but the password
    kind->value(spec[0] == "ftp" ? 1 : spec[0] == "smb" ? 2 : 0);
    host->value(spec[1].c_str());
    port->value(spec[2].c_str());
    user->value(spec[3].c_str());
    folder->value(spec[4].c_str());
  }
  auto *keep = new Fl_Check_Button(ox + 174, oy + 206, 300, 22, _("Connect at every start"));
  if (prefilled) keep->value(1);
  keep->labelsize(SIZE_SMALL + 1);
  keep->tooltip(_("Remembered without the password: works with an SSH key, anonymous FTP or a guest share"));
  auto conns = servers();
  std::string list;
  for (auto &[l, mp] : conns) list += (list.empty() ? "" : ", ") + l;
  auto *cl = new Fl_Box(ox + 64, oy + 236, W - 80, 36);
  static std::string cl_text;
  cl_text = conns.empty() ? std::string(_("No server connected.")) : fmt(_("Connected: %1"), list);
  cl->label(cl_text.c_str());
  cl->labelsize(SIZE_SMALL);
  cl->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  std::vector<std::pair<std::string, std::function<void()>>> buttons = {{_("Cancel"), nullptr}};
  if (!conns.empty())
    buttons.push_back({_("Disconnect All"), [this, conns] { for (auto &c : conns) disconnect_server(c.second); servers_changed(); }});
  buttons.push_back({_("Connect"), [this, kind, host, port, user, pass, folder, keep] {
    static const char *kinds[] = {"sftp", "ftp", "smb"};
    ServerSpec sp{kinds[std::clamp(kind->value(), 0, 2)], host->value(), port->value(), user->value(), folder->value()};
    if (sp.host.empty()) return;
    std::string mp, err = mount_server(sp, pass->value(), mp);
    if (!err.empty()) { guru(fmt(_("Could not connect to “%1”.\n%2"), sp.host, err)); return; }
    std::string line = sp.kind + "|" + sp.host + "|" + sp.port + "|" + sp.user + "|" + sp.folder;
    std::vector<std::string> lines;   // servers.conf without this server
    {
      std::ifstream in(servers_file());
      std::string l;
      while (std::getline(in, l)) if (l != line && !l.empty()) lines.push_back(l);
    }
    if (keep->value()) lines.push_back(line);
    std::error_code ec;
    fs::create_directories(fs::path(servers_file()).parent_path(), ec);
    std::ofstream out(servers_file());
    for (auto &l : lines) out << l << "\n";
    servers_changed();
    NWin *w = nullptr;
    for (auto *o : wins_) if (o->pane && o->ex) w = o;
    if (w) {
      navigate(w, mp);
      Fl::add_timeout(1.0, [](void *d) {   // a slow server: look once more
        auto *nw = (NWin *)d;
        if (nw->desk->is_known(nw) && nw->pane) nw->pane->refresh();
      }, w);
    } else {
      open_folder(mp);
    }
  }});
  end_form(f, W, H, ox, oy, buttons);
  if (prefilled) pass->take_focus(); else host->take_focus();
}

// ---------------------------------------------------------------------------
// Archives written back: an archive no window shows any more is closed (unmounted) in the
// background - archivemount writes the changes into the archive file then
// ---------------------------------------------------------------------------
void Desktop::release_archives() {
  for (auto it = g_archive_mounts.begin(); it != g_archive_mounts.end();) {
    std::string mp = *it;
    bool shown = false;
    for (auto *w : wins_) {
      if (!w->pane) continue;
      std::vector<std::string> paths = {w->path};
      if (w->ex) for (auto *p : w->ex->panes) if (p) paths.push_back(p->path());
      for (auto &p : paths) if (p == mp || p.rfind(mp + "/", 0) == 0) shown = true;
    }
    if (shown) { ++it; continue; }
    it = g_archive_mounts.erase(it);
    std::string arch = archive_dirs_.count(mp) ? archive_dirs_[mp] : mp;
    auto job = std::make_shared<jobs::Job>();
    job->title = fmt(_("Saving the archive “%1”"), fs::path(arch).filename().string());
    std::string snap;
    bool known_snap = g_archive_snap.count(mp);
    if (known_snap) { snap = g_archive_snap[mp]; g_archive_snap.erase(mp); }
    job->work = [mp, arch, snap, known_snap](jobs::Job &j) {
      j.set_current(fs::path(arch).filename().string());
      // changed inside? then pack it again: a new file next to it, in the format of its name
      // ("zip", "tar.gz", "7z"... - bsdtar -a), which then replaces the old one
      if (arch != mp && (!known_snap || archive_snapshot(mp) != snap)) {
        fs::path a(arch);
        std::string tmp = (a.parent_path() / (".nirvana-saving-" + a.filename().string())).string();
        std::vector<std::string> names;
        std::error_code ec;
        for (auto it = fs::directory_iterator(mp, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
          names.push_back(it->path().filename().string());
        std::vector<std::string> cmd = {"bsdtar", "-a", "-cf", tmp, "-C", mp, "-s", ",^\\./,,"};
        if (names.empty()) { cmd.push_back("-T"); cmd.push_back("/dev/null"); }
        for (auto &n : names) cmd.push_back("./" + n);   // "./": a name may start with "-"
        std::string err = run_with_input(cmd, "");
        struct stat st;
        if (err.empty() && stat(tmp.c_str(), &st) == 0) {
          struct stat old;
          if (stat(arch.c_str(), &old) == 0) chmod(tmp.c_str(), old.st_mode & 07777);
          if (rename(tmp.c_str(), arch.c_str()) != 0) { j.error(arch + ": " + strerror(errno)); unlink(tmp.c_str()); }
        } else {
          unlink(tmp.c_str());
          j.error(err.empty() ? arch : err);
        }
      }
      for (int i = 0; i < 20; i++) {   // a program may still have a file open there
        std::string err = run_with_input({"fusermount3", "-u", mp}, "");
        if (err == "127") err = run_with_input({"fusermount", "-u", mp}, "");
        if (err.empty()) { std::error_code ec; fs::remove(mp, ec); return; }
        if (i == 19) j.error(err);
        usleep(500000);
      }
    };
    job->on_done = [this, mp](jobs::Job &j) {
      archive_dirs_.erase(mp);
      if (!j.errors.empty()) guru(fmt(_("The archive could not be saved:\n%1"), j.errors));
      refresh_all();
    };
    jobs::start(job);
  }
}

void Desktop::show_jobs() { jobs::show_window(); }

void Desktop::ex_toggle_info() {
  NWin *w = active_folder();
  if (!w || !w->ex) return;
  w->ex->info_on = !w->ex->info_on;
  w->ex->status_key.clear();
  ex_layout(w);
  ex_save_state();
}

// "kind:picture" and the like, in English and Polish
static std::string kind_word(std::string w) {
  for (auto &c : w) c = (char)tolower((unsigned char)c);
  static const std::pair<const char *, const char *> words[] = {
      {"picture", "image"}, {"pictures", "image"}, {"image", "image"}, {"photo", "image"}, {"obraz", "image"},
      {"obrazy", "image"}, {"zdjęcie", "image"}, {"zdjęcia", "image"}, {"sound", "audio"}, {"sounds", "audio"},
      {"music", "audio"}, {"audio", "audio"}, {"dźwięk", "audio"}, {"dźwięki", "audio"}, {"muzyka", "audio"},
      {"film", "video"}, {"films", "video"}, {"video", "video"}, {"filmy", "video"}, {"wideo", "video"},
      {"archive", "archive"}, {"archives", "archive"}, {"archiwum", "archive"}, {"archiwa", "archive"},
      {"document", "document"}, {"documents", "document"}, {"dokument", "document"}, {"dokumenty", "document"},
      {"text", "text"}, {"texts", "text"}, {"tekst", "text"}, {"teksty", "text"}, {"program", "program"},
      {"programs", "program"}, {"programy", "program"}};
  for (auto &[a, b] : words) if (w == a) return b;
  return w;
}

void Desktop::ex_set_filter(NWin *w, const std::string &f) {
  if (!w || !w->ex) return;
  Explorer *x = w->ex;
  x->filters[x->active] = f;
  std::string pat, cat;
  std::istringstream in(f);
  std::string tok;
  while (in >> tok) {
    size_t c = tok.find(':');
    std::string k = c == std::string::npos ? "" : tok.substr(0, c);
    if (k == "kind" || k == "typ" || k == "rodzaj" || k == "type") cat = kind_word(tok.substr(c + 1));
    else pat += (pat.empty() ? "" : " ") + tok;
  }
  for (auto &ch : pat) ch = (char)tolower((unsigned char)ch);
  FolderView *v = w->pane->view();
  if (pat.empty() && cat.empty()) v->filter = nullptr;
  else v->filter = [pat, cat](const Entry &e) {
    if (!cat.empty() && e.cat != cat) return false;
    if (pat.empty()) return true;
    std::string n = e.name;
    for (auto &ch : n) ch = (char)tolower((unsigned char)ch);
    bool wild = pat.find_first_of("*?[") != std::string::npos;
    return wild ? fnmatch(pat.c_str(), n.c_str(), 0) == 0 : n.find(pat) != std::string::npos;
  };
  w->pane->refresh();
  x->status_key.clear();
  ex_save_state();
}

// ---------------------------------------------------------------------------
// Space: a quick look - the file type's preview action, or Rambler's own preview:
// the picture, the start of a text, or what is known about the file
// ---------------------------------------------------------------------------
namespace {
class Peek : public Fl_Double_Window {
public:
  Fl_Text_Display *text = nullptr;
  Fl_Text_Buffer *buf = nullptr;
  Fl_Image *img = nullptr;
  meta::Fields fields;
  Entry e;
  std::string kind, caption;
  Peek() : Fl_Double_Window(720, 540) {
    color(CONTENT);
    buf = new Fl_Text_Buffer;
    text = new Fl_Text_Display(10, 10, 700, 520);
    text->buffer(buf);
    text->textfont(FL_COURIER);
    text->textsize(SIZE_SMALL + 1);
    text->box(WELL_BOX);
    text->hide();
    end();
    xclass("nirvana-peek");
  }
  ~Peek() override { delete img; }
  void show_entry(const Entry &en) {
    e = en;
    delete img;
    img = nullptr;
    fields.clear();
    text->hide();
    sniff::Kind k = sniff::of(e.path);
    kind = k.label;
    copy_label((e.label.empty() ? e.name : e.label).c_str());
    if (k.cat == "image" && sniff::allowed(e.path)) {
      if (Fl_Shared_Image *si = Fl_Shared_Image::get(e.path.c_str())) {
        if (si->w() > 0) {
          int sx, sy, sw, sh;
          Fl::screen_work_area(sx, sy, sw, sh);
          double sc = std::min({(sw - 120.0) / si->w(), (sh - 120.0) / si->h(), 1.0});
          img = si->copy(std::max(1, (int)(si->w() * sc)), std::max(1, (int)(si->h() * sc)));
        }
        si->release();
      }
    }
    if (img) {
      // the caption under the picture: kind and details; the window is at least that wide
      std::string d;
      for (auto &[l, v] : meta::read(e.path, k)) d += (d.empty() ? "" : "   ") + v;
      caption = kind + (d.empty() ? "" : "   " + d);
      fl_font(FONT_SMALL, SIZE_SMALL + 1);
      int cw = (int)fl_width(caption.c_str()) + 20;
      size(std::max({img->w() + 20, cw, 300}), img->h() + 46);
    } else if (k.cat == "text" || k.mime.rfind("text/", 0) == 0) {
      std::ifstream in(e.path, std::ios::binary);
      std::string t(256 * 1024, '\0');
      in.read(&t[0], (std::streamsize)t.size());
      t.resize((size_t)in.gcount());
      buf->text(t.c_str());
      size(720, 540);
      text->resize(10, 10, 700, 520);
      text->show();
    } else {
      fields = meta::read(e.path, k);
      auto slow = meta::read_slow(e.path, k);
      fields.insert(fields.end(), slow.begin(), slow.end());
      size(460, std::max(200, 110 + (int)fields.size() * 22));
    }
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    position(sx + (sw - w()) / 2, sy + (sh - h()) / 2);
    show();
    redraw();
  }
  void draw() override {
    Fl_Double_Window::draw();
    if (text->visible()) return;
    fl_color(CONTENT);
    fl_rectf(0, 0, w(), h());
    if (img) {
      img->draw((w() - img->w()) / 2, 10);
      fl_color(CONTENT_TEXT);
      fl_font(FONT_SMALL, SIZE_SMALL + 1);
      fl_draw(caption.c_str(), 10, h() - 26, w() - 20, 20, FL_ALIGN_CENTER | FL_ALIGN_CLIP, nullptr, 0);
      return;
    }
    draw_entry_icon(e, 20, 20, false, 64);
    fl_color(CONTENT_TEXT);
    fl_font(FONT_SYSTEM, SIZE_SYSTEM + 2);
    fl_draw((e.label.empty() ? e.name : e.label).c_str(), 100, 20, w() - 110, 24, FL_ALIGN_LEFT | FL_ALIGN_CLIP, nullptr, 0);
    fl_font(FONT_SMALL, SIZE_SMALL + 1);
    fl_draw(kind.c_str(), 100, 46, w() - 110, 20, FL_ALIGN_LEFT, nullptr, 0);
    int y = 100;
    for (auto &[l, v] : fields) {
      fl_color(fl_color_average(CONTENT_TEXT, CONTENT, 0.55f));
      fl_draw(l.c_str(), 20, y, 140, 20, FL_ALIGN_RIGHT, nullptr, 0);
      fl_color(CONTENT_TEXT);
      fl_draw(v.c_str(), 170, y, w() - 180, 20, FL_ALIGN_LEFT | FL_ALIGN_CLIP, nullptr, 0);
      y += 22;
    }
  }
  int handle(int ev) override {
    if (ev == FL_KEYBOARD && (Fl::event_key() == ' ' || Fl::event_key() == FL_Escape)) { hide(); return 1; }
    return Fl_Double_Window::handle(ev);
  }
};
Peek *g_peek;
}  // namespace

void Desktop::preview_selected() {
  NWin *w = active_folder();
  if (!w || !w->pane) return;
  auto sel = w->pane->view()->selection();
  if (sel.empty() || sel[0].dir) return;
  if (g_peek && g_peek->shown()) { g_peek->hide(); return; }
  if (run_event(sel[0], w, "preview")) return;   // the file type's own preview
  if (!g_peek) g_peek = new Peek;
  g_peek->show_entry(sel[0]);
}

// ---------------------------------------------------------------------------
// The session: what every explorer window shows (both panes, their histories, the
// selections, the views, filters, the tree and the information panel)
// ---------------------------------------------------------------------------
static std::string state_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/explorer.conf";
}

static std::string join(const std::vector<std::string> &v) {
  std::string s;
  for (size_t i = 0; i < v.size(); i++) s += (i ? "\t" : "") + v[i];
  return s;
}
static std::vector<std::string> split_tabs(const std::string &s) {
  std::vector<std::string> v;
  size_t a = 0;
  while (a < s.size()) {
    size_t b = s.find('\t', a);
    if (b == std::string::npos) b = s.size();
    if (b > a) v.push_back(s.substr(a, b - a));
    a = b + 1;
  }
  return v;
}

void Desktop::ex_save_state() {
  std::ostringstream o;
  o << "# Nirvana Desktop - the explorer windows of the last session\n";
  for (auto *w : wins_) {
    if (!w->ex || !w->pane || !w->shown()) continue;
    Explorer *x = w->ex;
    x->hist[x->active] = {w->path, w->back_hist, w->fwd_hist};
    o << "[window]\nactive=" << x->active << "\ndual=" << x->dual << "\ntree=" << x->tree_on << "\ninfo=" << x->info_on
      << "\ntree_w=" << x->tree_w << "\n";
    for (int i = 0; i < 2; i++) {
      auto &h = x->hist[i];
      std::vector<std::string> back(h.back.end() - (long)std::min<size_t>(h.back.size(), 30), h.back.end());
      std::vector<std::string> fwd(h.fwd.end() - (long)std::min<size_t>(h.fwd.size(), 30), h.fwd.end());
      std::vector<std::string> sel;
      for (auto &e : x->panes[i]->view()->selection()) sel.push_back(e.path);
      o << "pane" << i << "=" << x->panes[i]->path() << "\n"
        << "pane" << i << "_list=" << (x->panes[i]->view()->mode() == FolderView::List) << "\n"
        << "pane" << i << "_back=" << join(back) << "\n"
        << "pane" << i << "_fwd=" << join(fwd) << "\n"
        << "pane" << i << "_sel=" << join(sel) << "\n"
        << "pane" << i << "_filter=" << x->filters[i] << "\n";
    }
  }
  std::string f = state_file();
  std::error_code ec;
  fs::create_directories(fs::path(f).parent_path(), ec);
  std::ofstream(f) << o.str();
}

static std::vector<std::map<std::string, std::string>> g_saved;

// The folders to open at the start in explorer mode: the source pane of every saved window
// (the list of open folders of the spatial mode would open one window per pane ever shown)
std::vector<std::string> Desktop::ex_saved_windows() const {
  std::vector<std::string> v;
  std::ifstream in(state_file());
  std::string l, p0, p1;
  int active = 0;
  bool any = false;
  auto flush = [&] {
    if (any) v.push_back(active == 1 && !p1.empty() ? p1 : p0);
    any = false; p0.clear(); p1.clear(); active = 0;
  };
  while (std::getline(in, l)) {
    if (l == "[window]") { flush(); any = true; }
    else if (l.rfind("active=", 0) == 0) active = atoi(l.c_str() + 7);
    else if (l.rfind("pane0=", 0) == 0) p0 = l.substr(6);
    else if (l.rfind("pane1=", 0) == 0) p1 = l.substr(6);
  }
  flush();
  return v;
}

static void ex_restore_state(Desktop *d, NWin *w, const std::string &path) {
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    std::ifstream in(state_file());
    std::string l;
    while (std::getline(in, l)) {
      if (l == "[window]") { g_saved.push_back({}); continue; }
      size_t e = l.find('=');
      if (e == std::string::npos || g_saved.empty() || l[0] == '#') continue;
      g_saved.back()[l.substr(0, e)] = l.substr(e + 1);
    }
  }
  // the saved window whose source pane showed this folder - or, failing that, either pane
  // (the desktop may reopen the window on the other one)
  auto found = g_saved.end();
  for (auto it = g_saved.begin(); it != g_saved.end() && found == g_saved.end(); ++it)
    if ((*it)["pane" + std::string(atoi((*it)["active"].c_str()) == 1 ? "1" : "0")] == path) found = it;
  for (auto it = g_saved.begin(); it != g_saved.end() && found == g_saved.end(); ++it)
    if ((*it)["pane0"] == path || ((*it)["dual"] == "1" && (*it)["pane1"] == path)) found = it;
  for (auto it = found; it != g_saved.end(); it = g_saved.end()) {
    auto &m = *it;
    int active = atoi(m["active"].c_str()) == 1 ? 1 : 0;
    Explorer *x = w->ex;
    x->dual = m["dual"] == "1";
    x->tree_on = m["tree"] != "0";
    x->info_on = m["info"] == "1";
    if (int tw = atoi(m["tree_w"].c_str())) x->tree_w = tw;
    std::error_code ec;
    for (int i = 0; i < 2; i++) {
      std::string k = "pane" + std::to_string(i), p = m[k];
      if (!p.empty() && fs::is_directory(p, ec) && p != x->panes[i]->path()) x->panes[i]->go(p);
      x->hist[i].path = x->panes[i]->path();
      x->hist[i].back = split_tabs(m[k + "_back"]);
      x->hist[i].fwd = split_tabs(m[k + "_fwd"]);
      x->panes[i]->set_mode(m[k + "_list"] == "0" ? FolderView::Icons : FolderView::List);
      x->filters[i] = m[k + "_filter"];
      x->panes[i]->view()->select_paths(split_tabs(m[k + "_sel"]));
    }
    w->back_hist = x->hist[0].back;
    w->fwd_hist = x->hist[0].fwd;
    w->path = x->hist[0].path;
    if (x->dual && active == 1) d->ex_activate(w, 1);
    for (int i = 0; i < 2; i++)   // the filters: applied with each pane active in turn
      if (!x->filters[i].empty()) {
        int was = x->active;
        if (i == 1 && !x->dual) continue;
        d->ex_activate(w, i);
        d->ex_set_filter(w, x->filters[i]);
        d->ex_activate(w, was);
      }
    if (x->filter) x->filter->value(x->filters[x->active].c_str());
    g_saved.erase(it);
    return;
  }
}

// ---------------------------------------------------------------------------
// Servers like disks: on the desktop and in the trees; remembered ones connect at the start
// ---------------------------------------------------------------------------
void Desktop::servers_changed() {
  layout_icons();
  redraw();
  ex_refresh_trees();
}

void Desktop::ex_trash_changed(bool full) {
  for (auto *w : wins_)
    if (w->ex && w->ex->tree) w->ex->tree->set_trash(full);
}

void Desktop::ex_refresh_trees() {
  auto roots = ex_tree_roots(this);
  for (auto *w : wins_)
    if (w->ex && w->ex->tree) {
      w->ex->tree->rebuild(home_, trash_dir(), roots);
      w->ex->tree->reveal(w->path);
    }
}

// The disks for the trees: every volume - a mounted one by its folder, the others to be
// mounted with a click - then the connected servers
std::vector<TreeRoot> ex_tree_roots(Desktop *d) {
  std::vector<TreeRoot> v;
  for (auto &vo : d->volumes())
    v.push_back({vo.name, vo.mountpoint.empty() ? "mount:" + vo.kname : vo.mountpoint, vo.icon});
  for (auto &[label, mp] : d->servers()) v.push_back({label, mp, Icon::Network});
  return v;
}

// A disk clicked in the tree: mounted (or unlocked first - the password window), then shown
void Desktop::ex_open_volume(NWin *w, const std::string &kname) {
  for (int i = 0; i < (int)vols_.size(); i++) {
    if (vols_[i].kname != kname) continue;
    vol::Volume v = vols_[i];
    if (v.encrypted && !v.unlocked) { ask_password(v, ""); return; }
    std::string mp = v.mountpoint;
    if (mp.empty()) {
      for (auto *o : wins_) o->cursor(FL_CURSOR_WAIT);
      Fl::flush();
      std::string err = vol::mount(v, mp);
      for (auto *o : wins_) o->cursor(FL_CURSOR_DEFAULT);
      if (!err.empty()) { dialog(GURU_TITLE, err, {{"OK", nullptr}}); ex_refresh_trees(); return; }
      rescan_volumes();
    }
    if (!mp.empty() && known(w)) navigate(w, mp);
    ex_refresh_trees();
    return;
  }
}



void Desktop::reconnect_servers() {
  std::ifstream in(servers_file());
  std::string l;
  auto connected = servers();
  while (std::getline(in, l)) {
    if (l.empty() || l[0] == '#') continue;
    std::vector<std::string> f;
    std::stringstream ss(l);
    std::string part;
    while (std::getline(ss, part, '|')) f.push_back(part);
    if (f.size() < 2) continue;
    f.resize(5);   // empty fields at the end (no folder: "sftp|host|port|user|") are not read
    ServerSpec sp{f[0], f[1], f[2], f[3], f[4]};
    bool already = false;
    for (auto &[label, mp] : connected) if (label.find(sp.host) != std::string::npos) already = true;
    if (already) continue;
    auto job = std::make_shared<jobs::Job>();
    job->title = fmt(_("Connecting to “%1”"), sp.host);
    job->work = [sp](jobs::Job &j) {   // without a password: an SSH key, anonymous FTP, a guest share
      std::string mp, err = mount_server(sp, "", mp);
      if (!err.empty()) j.error(sp.host + ": " + err);
    };
    // no key / no guest access: the dialog, filled in, asks for the password
    job->on_done = [this, sp](jobs::Job &j) {
      servers_changed();
      if (!j.errors.empty())
        connect_server_to({sp.kind, sp.host, sp.port, sp.user, sp.folder},
                          fmt(_("“%1” connects at every start, but it needs a password this time."), sp.host));
    };
    jobs::start(job);
  }
}
