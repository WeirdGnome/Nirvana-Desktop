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

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Tree.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace plat;

static const int TOOL_H = 46, ADDR_H = 26, STATUS_H = 20, HEAD_H = 18, SPLIT_W = 5;

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
  std::string status_key;
};

// the destination pane's folder ("" when the window has one pane)
std::string ex_other_path(NWin *w) {
  if (!w || !w->ex || !w->ex->dual) return "";
  return w->ex->panes[1 - w->ex->active]->path();
}

void destroy_explorer(Explorer *x);
void destroy_explorer(Explorer *x) {
  Fl::remove_timeout(nullptr, x);
  delete x;
}

// ---------------------------------------------------------------------------
// The big flat buttons with a picture and a label
// ---------------------------------------------------------------------------
enum class ExIcon { Back, Forward, Up, Tree, Dual, CopyTo, MoveTo, Delete, Views, Server, Swap };

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
  case ExIcon::Dual:
    fl_color(paper); fl_rectf(x + 1, y + 3, 10, 18); fl_rectf(x + 13, y + 3, 10, 18);
    fl_color(acc); fl_rectf(x + 1, y + 3, 10, 4);
    fl_color(ink); fl_rect(x + 1, y + 3, 10, 18); fl_rect(x + 13, y + 3, 10, 18);
    break;
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
  case ExIcon::Swap:
    arrow_r(x + 6, y + 3, acc);
    fl_color(acc);   // and one to the left below
    fl_rectf(x + 11, y + 14, 7, 5);
    fl_polygon(x + 12, y + 11, x + 6, y + 16, x + 12, y + 21);
    break;
  }
}

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
  Fl_Tree_Item *add_dir(Fl_Tree_Item *parent, const std::string &label, const std::string &path, bool lazy = true) {
    // add(parent, name) - not add(path): a "/" in the name would make sub-items
    Fl_Tree_Item *it = add(parent ? parent : root(), label.c_str());
    if (!it) return nullptr;
    paths.push_back(path);
    it->user_data((void *)&paths.back());
    Entry e;
    e.dir = true;
    e.path = path;
    e.name = fs::path(path).filename().string();
    e.icon = icon_for_path(path, true);
    if (Fl_Image *img = entry_icon_image(e, 16, false)) it->usericon(img);
    if (lazy && has_subdirs(path)) { add(it, "…"); it->close(); }
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
      if (!p.empty()) desk->navigate(win, p);
    }
  }
  void rebuild(const std::string &home, const std::string &trash, const std::vector<std::pair<std::string, std::string>> &roots) {
    if (root()) clear_children(root());   // clear() would remove the root item too
    paths.clear();
    Fl_Tree_Item *h = add_dir(nullptr, "Nirvana HD", home);
    if (h) { populate(h); h->open(); }
    for (auto &[label, path] : roots) add_dir(nullptr, label, path);
    add_dir(nullptr, _("Trash"), trash, false);
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
static std::string short_path(Desktop *d, const std::string &p) {
  std::string arch, inner, s = p;
  if (d->is_archive_cache(p, &arch, &inner)) s = arch + inner;
  const char *h = getenv("HOME");
  std::string home = h ? h : "";
  if (!home.empty() && s == home) return "~";
  if (!home.empty() && s.rfind(home + "/", 0) == 0) return "~" + s.substr(home.size());
  return s;
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

static std::string human_size(uintmax_t b) {
  char t[32];
  if (b < 1024) snprintf(t, sizeof t, "%ju B", b);
  else if (b < 1024 * 1024) snprintf(t, sizeof t, "%.1f KB", b / 1024.0);
  else if (b < 1024ull * 1024 * 1024) snprintf(t, sizeof t, "%.1f MB", b / 1048576.0);
  else snprintf(t, sizeof t, "%.2f GB", b / 1073741824.0);
  return t;
}

static void status_tick(void *d);

// ---------------------------------------------------------------------------
// Building the window
// ---------------------------------------------------------------------------
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
  x->addr->end();
  x->addr->resizable(in);

  // the tree, the splitter, the status bar
  x->tree = new ExTree(ox, oy, 100, 100, this, win);
  x->splitter = new ExSplitter(ox, oy, SPLIT_W, 100, this, win);
  x->status = new ExStatus(ox, oy, W, STATUS_H, win);
  auto *rz = new Fl_Box(ox + W / 2, oy + H / 2, 1, 1);   // resizing: ex_layout places everything
  win->resizable(rz);
  win->end();

  std::vector<std::pair<std::string, std::string>> roots;
  for (auto &v : vols_) if (!v.mountpoint.empty()) roots.push_back({v.name, v.mountpoint});
  for (auto &s : servers()) roots.push_back(s);
  x->tree->rebuild(home_, trash_dir(), roots);
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
  x->addr->resize(ox, oy + NWin::MB_H + TOOL_H, W, ADDR_H);
  x->addr_label->resize(ox + 4, oy + NWin::MB_H + TOOL_H + 3, 52, ADDR_H - 6);
  w->loc->resize(ox + 58, oy + NWin::MB_H + TOOL_H + 3, std::max(40, W - 58 - 74), ADDR_H - 6);
  x->go->resize(ox + W - 70, oy + NWin::MB_H + TOOL_H + 3, 66, ADDR_H - 6);
  int top = oy + NWin::MB_H + TOOL_H + ADDR_H + 2;
  int bodyh = std::max(40, H - (top - oy) - STATUS_H);
  int px = ox;
  if (x->tree_on) {
    int tw = std::clamp(x->tree_w, 120, std::max(120, W / 2));
    x->tree->resize(ox + 2, top, tw, bodyh);
    x->tree->show();
    x->splitter->resize(ox + 2 + tw, top, SPLIT_W, bodyh);
    x->splitter->show();
    px = ox + 2 + tw + SPLIT_W;
  } else {
    x->tree->hide();
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
                    std::to_string(total) + "|" + std::to_string(x->dual) + std::to_string(x->active);
  if (key != x->status_key) {
    x->status_key = key;
    auto *st = (ExStatus *)x->status;
    if (sel.empty()) st->text_left = fmt(_("%1 items"), std::to_string(v->count()));
    else st->text_left = fmt(_("%1 of %2 selected (%3)"), std::to_string(sel.size()), std::to_string(v->count()), human_size(total));
    std::string arch;
    if (w->desk->is_archive_cache(w->path, &arch)) st->text_left += "   " + fmt(_("Inside the archive “%1” - a copy: changes do not go back into it"), fs::path(arch).filename().string());
    st->text_right = x->dual ? fmt(_("Copy/Move to: %1"), short_path(w->desk, x->panes[1 - x->active]->path())) : std::string();
    st->redraw();
  }
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

void Desktop::browse_archive(NWin *w, const std::string &archive) {
  struct stat st;
  if (stat(archive.c_str(), &st) != 0) return;
  std::string name = fs::path(archive).filename().string();
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
    if (n.rfind("ftp:", 0) == 0 || n.rfind("sftp:", 0) == 0) {
      std::string host = n.substr(n.find("host=") == std::string::npos ? 0 : n.find("host=") + 5);
      host = host.substr(0, host.find(','));
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

void Desktop::connect_server() {
  int W = 480, H = 330, ox, oy;
  NWin *f = begin_form(_("Connect to a Server"), W, H, nullptr,
                       _("The server's folders open like folders on this computer."), ox, oy);
  auto row = [&](int y, const char *l) {
    auto *b = new Fl_Box(ox + 64, oy + y, 110, 24, l);
    b->labelsize(SIZE_SMALL + 1);
    b->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
  };
  row(56, _("Kind:"));
  auto *kind = new Fl_Choice(ox + 178, oy + 56, 160, 24);
  kind->add("SFTP (SSH)");
  kind->add("FTP");
  kind->value(0);
  kind->textsize(SIZE_SMALL + 1);
  row(86, _("Server:"));
  auto *host = new Fl_Input(ox + 178, oy + 86, 210, 24);
  auto *port = new Fl_Input(ox + 392, oy + 86, 60, 24);
  port->tooltip(_("Port (empty: the usual one)"));
  row(116, _("User:"));
  auto *user = new Fl_Input(ox + 178, oy + 116, 274, 24);
  const char *u = getenv("USER");
  user->value(u ? u : "");
  row(146, _("Password:"));
  auto *pass = new Fl_Secret_Input(ox + 178, oy + 146, 274, 24);
  pass->tooltip(_("Empty: an SSH key or an anonymous FTP login"));
  row(176, _("Folder:"));
  auto *folder = new Fl_Input(ox + 178, oy + 176, 274, 24);
  for (Fl_Input *i : {host, port, user, (Fl_Input *)pass, folder}) { i->box(WELL_BOX); i->textsize(SIZE_SMALL + 1); }
  // the connected servers
  auto conns = servers();
  std::string list;
  for (auto &[l, mp] : conns) list += (list.empty() ? "" : ", ") + l;
  auto *cl = new Fl_Box(ox + 64, oy + 208, W - 80, 36);
  static std::string cl_text;
  cl_text = conns.empty() ? std::string(_("No server connected.")) : fmt(_("Connected: %1"), list);
  cl->label(cl_text.c_str());
  cl->labelsize(SIZE_SMALL);
  cl->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  std::vector<std::pair<std::string, std::function<void()>>> buttons = {{_("Cancel"), nullptr}};
  if (!conns.empty())
    buttons.push_back({_("Disconnect All"), [this, conns] { for (auto &c : conns) disconnect_server(c.second); }});
  buttons.push_back({_("Connect"), [this, kind, host, port, user, pass, folder] {
    std::string h = host->value(), pt = port->value(), us = user->value(), pw = pass->value(), dir = folder->value();
    if (h.empty()) return;
    bool sftp = kind->value() == 0;
    std::string err, mp;
    if (sftp && have_prog("sshfs")) {
      mp = remote_root() + "/" + (us.empty() ? "" : us + "@") + h;
      std::error_code ec;
      fs::create_directories(mp, ec);
      std::vector<std::string> a = {"sshfs", (us.empty() ? "" : us + "@") + h + ":" + dir, mp, "-o",
                                    "StrictHostKeyChecking=accept-new,reconnect,ServerAliveInterval=15"};
      if (!pt.empty()) { a.push_back("-p"); a.push_back(pt); }
      if (!pw.empty()) { a.push_back("-o"); a.push_back("password_stdin"); }
      else { a.push_back("-o"); a.push_back("BatchMode=yes"); }
      err = run_with_input(a, pw.empty() ? "" : pw + "\n");
      if (err == "127") err = _("sshfs is not installed (package sshfs).");
      // sshfs goes into the background before the folder is ready: wait for it (3 s at most)
      struct stat a1, a2;
      for (int i = 0; err.empty() && i < 50; i++) {
        bool mounted = stat(mp.c_str(), &a1) == 0 && stat(fs::path(mp).parent_path().c_str(), &a2) == 0 && a1.st_dev != a2.st_dev;
        int entries = 0;
        if (mounted)
          if (DIR *dd = opendir(mp.c_str())) {
            while (readdir(dd)) entries++;
            closedir(dd);
          }
        if (entries > 2) break;   // more than "." and "..": the server answers
        usleep(100000);
      }
    } else if (have_prog("gio")) {
      std::string uri = std::string(sftp ? "sftp" : "ftp") + "://" + (us.empty() ? "" : us + "@") + h + (pt.empty() ? "" : ":" + pt) + "/";
      std::vector<std::string> a = {"gio", "mount", uri};
      if (us.empty() && !sftp) a.insert(a.begin() + 2, "-a");   // anonymous FTP
      err = run_with_input(a, pw + "\n");
      if (err.find("already mounted") != std::string::npos) err.clear();
      for (auto &[l, m] : servers()) if (l.find(h) != std::string::npos) mp = m + (dir.empty() ? "" : (dir[0] == '/' ? dir : "/" + dir));
      if (err.empty() && mp.empty()) err = _("The server is connected, but its folder cannot be seen - is gvfs-fuse installed?");
    } else {
      err = sftp ? _("sshfs is not installed (package sshfs).") : _("gio is not installed (packages gvfs-backends and gvfs-fuse).");
    }
    if (!err.empty()) { guru(fmt(_("Could not connect to “%1”.\n%2"), h, err)); return; }
    NWin *w = nullptr;
    for (auto *o : wins_) if (o->pane && o->ex) w = o;
    if (w) {
      std::vector<std::pair<std::string, std::string>> roots;
      for (auto &v : vols_) if (!v.mountpoint.empty()) roots.push_back({v.name, v.mountpoint});
      for (auto &s : servers()) roots.push_back(s);
      w->ex->tree->rebuild(home_, trash_dir(), roots);
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
  host->take_focus();
}
