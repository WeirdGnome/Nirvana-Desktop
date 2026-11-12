// Nirvana Desktop - Rambler: actions of the Nirvana, Window, Icons and Special menus
//
// Menu layout (in the spirit of the classic desktops):
// Nirvana    - whole-system commands (about, preferences, execute command, Nirvana Shell,
//              system log, reset)
// System     - lock, sleep, hibernate, log out, restart, shut down
// Window     - in each folder window: navigation, new folder, view, sorting, hidden files
// Icons      - in each folder window: open, copy, information, snapshot, dock
// Special    - format disk, empty Trash
#include "desktop.h"

#include "appimage.h"
#include "power.h"
#include "volumes.h"
#include "tray.h"
#include "i18n.h"
#include "applets.h"

std::string ex_other_path(NWin *w);   // explorer.cxx: the destination pane

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace plat;

static int g_argc = 0;
static char **g_argv = nullptr;
void Desktop::set_argv(int argc, char **argv) { g_argc = argc; g_argv = argv; }

static std::string config_dir() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana";
}

// ---------------------------------------------------------------------------
// A form in a Rambler dialog window (icon, text, fields, buttons)
// ---------------------------------------------------------------------------
struct FormIcon : Fl_Widget {
  Entry e;
  bool logo;
  FormIcon(int X, int Y, const Entry &en, bool lg) : Fl_Widget(X, Y, 34, 34), e(en), logo(lg) {}
  void draw() override {
    if (logo) draw_logo(x(), y(), 32);
    else draw_entry_icon(e, x(), y(), false, 32);
  }
};

NWin *Desktop::begin_form(const std::string &title, int W, int H, const Entry *icon, const std::string &text,
                          int &ox, int &oy) {
  ox = csd_mode ? FRAME : 0;
  oy = csd_mode ? TITLE_H : 0;
  int X = (w() - W) / 2, Y = std::max(MENUBAR_H + 10, (h() - H) / 3);
  auto *win = new NWin(this, X - ox, Y - oy, W + 2 * ox, H + oy + ox, "", true);
  win->copy_label(title.c_str());
  win->color(GRAY);
  new FormIcon(ox + 16, oy + 16, icon ? *icon : Entry(), icon == nullptr);
  if (!text.empty()) {
    auto *msg = new Fl_Box(ox + 64, oy + 14, W - 80, 36);
    msg->copy_label(text.c_str());
    msg->labelfont(FONT_SYSTEM);
    msg->labelsize(SIZE_SYSTEM);
    msg->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  }
  return win;
}

void Desktop::end_form(NWin *win, int W, int H, int ox, int oy,
                       std::vector<std::pair<std::string, std::function<void()>>> buttons) {
  int bx = ox + W - 16;
  for (int i = (int)buttons.size() - 1; i >= 0; i--) {
    bx -= 84;
    auto *b = new Fl_Button(bx, oy + H - 36, 78, 22);
    b->copy_label(buttons[i].first.c_str());
    b->box(BUTTON_BOX);
    b->down_box(BUTTON_DOWN);
    b->color(GRAY);
    b->labelfont(i == (int)buttons.size() - 1 ? FONT_SYSTEM : FONT_SMALL);
    b->labelsize(SIZE_SYSTEM);
    b->callback(dialog_cb, (void *)(long)i);
    if (i == (int)buttons.size() - 1) b->shortcut(FL_Enter);
    bx -= 6;
  }
  for (auto &b : buttons) win->actions.push_back(b.second);
  win->end();
  win->set_modal();
  wins_.push_back(win);
  win->show();
}

static Fl_Box *row_label(int X, int Y, int W, const char *t) {
  auto *b = new Fl_Box(X, Y, W, 18);
  b->copy_label(t);
  b->labelsize(SIZE_SMALL + 1);
  b->labelfont(FONT_SYSTEM);
  b->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
  return b;
}
static Fl_Box *row_value(int X, int Y, int W, const std::string &t) {
  auto *b = new Fl_Box(X, Y, W, 18);
  b->copy_label(t.c_str());
  b->labelsize(SIZE_SMALL + 1);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  return b;
}

// ---------------------------------------------------------------------------
// Menu state: active/inactive items, check marks
// ---------------------------------------------------------------------------
std::vector<std::string> Desktop::targets() {
  std::vector<std::string> v;
  if (NWin *a = active_folder())
    for (auto &e : a->pane->view()->selection()) v.push_back(e.path);
  if (v.empty() && !active_folder() && icon_sel_ >= 0 && icon_sel_ < (int)icons_.size() && !icons_[icon_sel_].no_drop())
    v.push_back(icons_[icon_sel_].path);
  return v;
}

void Desktop::update_menu() { update_menu(menu_, nullptr); }

// State of the items of menu bar m. a - the folder window the bar belongs to (nullptr:
// the global desktop bar - then the active window or the selected desktop icon).
void Desktop::update_menu(Fl_Menu_ *m, NWin *a) {
  if (!a) a = active_folder();
  bool win_sel = a && !a->pane->view()->selection().empty();
  bool desk_sel = !a && icon_sel_ >= 0 && icon_sel_ < (int)icons_.size() && !icons_[icon_sel_].no_drop();
  const DeskIcon *di = desk_sel ? &icons_[icon_sel_] : nullptr;
  bool vol_sel = di && di->vol >= 0;
  bool icons_mode = a && a->pane->view()->mode() == FolderView::Icons;
  auto act = [m](const char *path, bool on) {
    if (auto *it = (Fl_Menu_Item *)m->find_item(plat::tr_path(path).c_str())) { if (on) it->activate(); else it->deactivate(); }
  };
  auto mark = [m](const char *path, bool on) {
    if (auto *it = (Fl_Menu_Item *)m->find_item(plat::tr_path(path).c_str())) { if (on) it->set(); else it->clear(); }
  };
  using applets::PowerAction;
  act(P_("System/Lock Screen"), applets::power_action_available(PowerAction::Lock));
  act(P_("System/Sleep"), applets::power_action_available(PowerAction::Sleep));
  act(P_("System/Hibernate"), applets::power_action_available(PowerAction::Hibernate));
  act(P_("System/Restart…"), applets::power_action_available(PowerAction::Restart));
  act(P_("System/Shut Down…"), applets::power_action_available(PowerAction::ShutDown));
  act(P_("Special/Format Disk…"), vol_sel && !vols_[di->vol].optical);
  if (m == menu_) return;   // Window and Icons exist only in folder windows

  bool parent_ok = false;
  if (a) {
    std::string par = fs::path(a->path).parent_path().string();
    parent_ok = par != a->path && allowed(par) && a->path != root_path_;
  }
  act(P_("Window/Open Parent"), parent_ok);
  act(P_("Window/Back"), a && !a->back_hist.empty());
  act(P_("Window/Forward"), a && !a->fwd_hist.empty());
  for (const char *p : {P_("Window/Close"), P_("Window/Update"), P_("Window/Select Contents"), P_("Window/View as"),
                        P_("Window/Sort"), P_("Window/Icon Size")})
    act(p, a != nullptr);
  act(P_("Window/Clean Up"), icons_mode);
  mark(P_("Window/Show Hidden Files"), show_hidden_);
  if (a) {
    FolderView *v = a->pane->view();
    mark(P_("Window/View as/Icons"), v->mode() == FolderView::Icons);
    mark(P_("Window/View as/List"), v->mode() == FolderView::List);
    mark(P_("Window/Sort/by Name"), v->sort_key() == FolderView::ByName);
    mark(P_("Window/Sort/by Date"), v->sort_key() == FolderView::ByDate);
    mark(P_("Window/Sort/by Size"), v->sort_key() == FolderView::BySize);
    mark(P_("Window/Sort/by Kind"), v->sort_key() == FolderView::ByKind);
    mark(P_("Window/Icon Size/Small"), v->icon_size() == 16);
    mark(P_("Window/Icon Size/Normal"), v->icon_size() == 32);
    mark(P_("Window/Icon Size/Large"), v->icon_size() == 64);
  }
  act(P_("Icons/Open"), win_sel);
  act(P_("Icons/Copy"), win_sel);
  act(P_("Icons/Paste"), !clipboard_.empty());
  act(P_("Icons/Duplicate"), win_sel);
  act(P_("Icons/Rename…"), win_sel);
  act(P_("Icons/Delete"), win_sel);
  act(P_("Icons/Information…"), win_sel);
  bool one_image = false, in_dock = false;
  if (a) {
    auto sel = a->pane->view()->selection();
    one_image = sel.size() == 1 && is_image_file(sel[0].path);
    for (auto &e : sel)
      if (std::find(dock_.begin(), dock_.end(), e.path) != dock_.end()) in_dock = true;
  }
  act(P_("Icons/Set as Wallpaper"), one_image);
  act(P_("Icons/Snapshot"), icons_mode);
  act(P_("Icons/Unsnapshot"), icons_mode);
  act(P_("Icons/Put on Dock"), win_sel);
  act(P_("Icons/Remove from Dock"), in_dock);
}

// ---------------------------------------------------------------------------
// Context menu of the desktop icons (right button) - the commands that used to
// be in the Icons menu of the global bar. The same shortcuts work when the desktop has the focus.
// ---------------------------------------------------------------------------
namespace {
struct DeskMenu {
  std::vector<std::string> labels;
  std::vector<std::function<void()>> acts;
  std::vector<Fl_Menu_Item> items;
  void add(const std::string &label, int shortcut, std::function<void()> f, int flags = 0) {
    labels.push_back(label);
    acts.push_back(std::move(f));
    Fl_Menu_Item it{};
    it.shortcut_ = shortcut;
    it.user_data_ = (void *)(long)(acts.size() - 1);
    it.flags = flags;
    items.push_back(it);
  }
  void divider() { if (!items.empty()) items.back().flags |= FL_MENU_DIVIDER; }
  Fl_Menu_Item *finish() {
    for (size_t i = 0; i < items.size(); i++) items[i].text = labels[i].c_str();
    items.push_back(Fl_Menu_Item{});   // end of the array
    return items.data();
  }
  void run(const Fl_Menu_Item *m) {
    if (m) { auto f = acts[(long)m->user_data()]; if (f) f(); }
  }
};
}   // namespace

static void build_desk_menu(Desktop *d, DeskMenu &dm, int i, bool has_win, bool has_vol, bool is_dock,
                            bool is_trash, bool can_eject, bool can_format, std::function<void()> restore,
                            const std::string &home, bool prefs = false) {
  if (i < 0) {   // empty desktop
    dm.add(_("Open Nirvana HD"), 0, [d, home] { d->open_folder(home); });
    dm.add(_("Desktop Appearance…"), 0, [d] { d->open_appearance(); });
    return;
  }
  if (has_win) { dm.add(_("Restore"), FL_COMMAND + 'o', restore); return; }
  if (prefs) { dm.add(_("Open"), FL_COMMAND + 'o', [d] { d->open_preferences(); }); return; }
  if (is_dock) {   // dock tile: open or unpin
    dm.add(_("Open"), FL_COMMAND + 'o', [d] { d->open_selected(); });
    dm.add(_("Unpin from Dock"), FL_COMMAND + 'k', [d] { d->dock_remove(); });
    return;
  }
  dm.add(_("Open"), FL_COMMAND + 'o', [d] { d->open_selected(); });
  dm.add(_("Information…"), FL_COMMAND + 'i', [d] { d->get_info(); });
  if (is_trash) { dm.divider(); dm.add(_("Empty Trash…"), 0, [d] { d->empty_trash(); }); }
  if (has_vol) {
    dm.divider();
    dm.add(_("Eject"), FL_COMMAND + 'y', [d] { d->eject_selected(); }, can_eject ? 0 : FL_MENU_INACTIVE);
    dm.add(_("Format Disk…"), 0, [d] { d->format_disk(); }, can_format ? 0 : FL_MENU_INACTIVE);
  }
}

// FLTK scrolls a menu that sticks out of the work area (here: into the dock), and does so
// in the middle of event handling - a click at that moment crashes the program (an FLTK
// 1.4 bug). So context menus are placed to fit entirely in the work area.
// X, Y - relative to the window at position (ox, oy) on screen.
static void fit_popup(const Fl_Menu_Item *m, int &X, int &Y, int ox, int oy) {
  int n = m->size() - 1, wide = 0;
  fl_font(FONT_SYSTEM, SIZE_SYSTEM);
  for (const Fl_Menu_Item *it = m; it && it->text; it = it->next())
    wide = std::max(wide, (int)fl_width(it->text) + 90);
  int ih = fl_height() + Fl::menu_linespacing() + 2;
  int H = n * ih + 16;
  int wx, wy, ww, wh;
  Fl::screen_work_area(wx, wy, ww, wh, ox + X, oy + Y);
  if (oy + Y + H > wy + wh) Y = wy + wh - H - oy;
  if (oy + Y < wy) Y = wy - oy;
  if (ox + X + wide > wx + ww) X = wx + ww - wide - ox;
}

void Desktop::desk_context_menu(int i, int X, int Y) {
  if (i >= (int)icons_.size()) i = -1;   // the icons may have changed in the meantime
  icon_sel_ = i;
  redraw();
  DeskMenu dm;
  if (i >= 0 && icons_[i].server) {   // a server: open or disconnect
    std::string mp = icons_[i].path;
    dm.add(_("Open"), 0, [this, mp] { open_folder(mp); });
    dm.add(_("Disconnect"), 0, [this, mp] { disconnect_server(mp); servers_changed(); });
  } else if (i >= 0) {
    const DeskIcon &ic = icons_[i];
    unsigned long win = ic.win;
    bool vol = ic.vol >= 0;
    build_desk_menu(this, dm, i, win != 0, vol, ic.dock, ic.path == trash_dir() && !ic.dock,
                    vol && (vols_[ic.vol].removable || !vols_[ic.vol].mountpoint.empty()),
                    vol && !vols_[ic.vol].optical, [win] { gary::activate(win); }, home_, ic.prefs);
  } else {
    build_desk_menu(this, dm, -1, false, false, false, false, false, false, nullptr, root_path_);
  }
  Fl_Menu_Item *items = dm.finish();
  fit_popup(items, X, Y, x_root(), y_root());
  const Fl_Menu_Item *m = items->popup(X, Y, nullptr, nullptr, ctx_style_);
  desk_target_ = true;
  dm.run(m);
  desk_target_ = false;
}

// Right button in a folder window: commands for the selected icons or for the window.
void Desktop::later(std::function<void()> f) {
  static std::vector<std::function<void()>> queue;
  queue.push_back(std::move(f));
  Fl::add_timeout(0.0, [](void *) {
    auto q = std::move(queue);
    queue.clear();
    for (auto &x : q) x();
  });
}

void Desktop::folder_context_menu(NWin *w, int X, int Y) {
  if (!known(w)) return;
  auto sel = w->pane->view()->selection();
  bool in_dock = false;
  for (auto &e : sel)
    if (std::find(dock_.begin(), dock_.end(), e.path) != dock_.end()) in_dock = true;
  DeskMenu dm;
  // Nirvana Shell in this folder - or in the selected folder
  std::string shell_dir = sel.size() == 1 && sel[0].dir ? sel[0].path : w->path;
  auto add_shell = [&] {
    dm.add(_("Open Nirvana Shell Here"), 0, [this, shell_dir] {
      rambler_spawn({rambler_exe("nirvana-shell"), "--working-directory", shell_dir});
    });
  };
  bool in_trash = w->path == trash_dir();
  if (!sel.empty()) {
    dm.add(_("Open"), FL_COMMAND + 'o', [this] { open_selected(); });
    // the file type's actions (Special > File Types…), the first one is the double click
    // ("Open" itself is there already - it runs the type's first action, as the double click)
    for (auto &[label, f] : filetype_menu(sel, w))
      if (strcasecmp(label.c_str(), _("Open")) != 0 && strcasecmp(label.c_str(), "Open") != 0) dm.add(label, 0, f);
    if (sel.size() == 1 && sel[0].dir) add_shell();
    dm.divider();
    if (w->ex && ex_other_path(w).size()) {   // the explorer with two panes
      dm.add(_("Copy to the Other Pane"), FL_F + 5, [this] { ex_copy_to_other(); });
      dm.add(_("Move to the Other Pane"), FL_F + 6, [this] { ex_move_to_other(); });
      dm.divider();
    }
    if (in_dock) dm.add(_("Unpin from Dock"), FL_COMMAND + 'k', [this] { dock_remove(); });
    else dm.add(_("Put on Dock"), FL_COMMAND + 'l', [this] { dock_add(); });
    dm.divider();
    dm.add(_("Information…"), FL_COMMAND + 'i', [this] { get_info(); });
    dm.add(_("Rename…"), FL_F + 2, [this] { rename_selected(); }, sel.size() == 1 ? 0 : FL_MENU_INACTIVE);
    dm.add(_("Duplicate"), FL_COMMAND + 'd', [this] { duplicate(); });
    dm.add(_("Copy"), FL_COMMAND + 'c', [this] { copy_selected(); });
    dm.add(_("Pack into an Archive…"), 0, [this] { pack_selected(); });
    dm.divider();
    dm.add(_("Delete"), FL_COMMAND + FL_BackSpace, [this] { move_to_trash(); });
    if (in_trash) { dm.divider(); dm.add(_("Empty Trash…"), 0, [this] { empty_trash(); }); }
  } else {
    dm.add(_("New Folder"), FL_COMMAND + 'n', [this] { new_folder(); }, in_trash ? FL_MENU_INACTIVE : 0);
    dm.add(_("Paste"), FL_COMMAND + 'v', [this] { paste(); }, clipboard_.empty() ? FL_MENU_INACTIVE : 0);
    dm.divider();
    add_shell();
    dm.divider();
    dm.add(_("Update"), FL_COMMAND + 'r', [this] { update_window(); });
    dm.add(_("Select Contents"), FL_COMMAND + 'a', [this] { select_all(); });
    if (in_trash) { dm.divider(); dm.add(_("Empty Trash…"), 0, [this] { empty_trash(); }, trash_full() ? 0 : FL_MENU_INACTIVE); }
  }
  // folder window coordinates -> desktop ones (the menu's owner is a button on the desktop)
  Fl_Menu_Item *items = dm.finish();
  X += w->x() - x();
  Y += w->y() - y();
  fit_popup(items, X, Y, x_root(), y_root());
  const Fl_Menu_Item *m = items->popup(X, Y, nullptr, nullptr, ctx_style_);
  menu_target = w;
  dm.run(m);
  if (menu_target == w) menu_target = nullptr;
}

bool Desktop::desk_shortcut() {
  if (icon_sel_ < 0 || icon_sel_ >= (int)icons_.size()) return false;
  const DeskIcon &ic = icons_[icon_sel_];
  DeskMenu dm;
  unsigned long win = ic.win;
  bool vol = ic.vol >= 0;
  build_desk_menu(this, dm, icon_sel_, win != 0, vol, ic.dock, ic.path == trash_dir() && !ic.dock,
                  vol && (vols_[ic.vol].removable || !vols_[ic.vol].mountpoint.empty()),
                  vol && !vols_[ic.vol].optical, [win] { gary::activate(win); }, home_, ic.prefs);
  const Fl_Menu_Item *m = dm.finish()->test_shortcut();
  if (!m || !m->active()) return false;
  desk_target_ = true;
  dm.run(m);
  desk_target_ = false;
  return true;
}

// Icons > Set as Wallpaper: saved in the appearance settings, the desktop refreshes
// through _NIRVANA_APPEARANCE (the same as after "Apply" in the Appearance panel).
void Desktop::set_as_wallpaper() {
  std::vector<std::string> t = targets();
  if (t.size() != 1 || !is_image_file(t[0])) return;
  if (!image_loadable(t[0])) {
    dialog(GURU_TITLE, plat::fmt(_("Could not open the image “%1”.\nThe file may be damaged or in an unsupported format."),
                               fs::path(t[0]).filename().string()), {{"OK", nullptr}});
    return;
  }
  Appearance a = read_appearance();
  a.wallpaper = t[0];
  if (!write_appearance(a)) {
    dialog(GURU_TITLE, _("Could not save the appearance settings."), {{"OK", nullptr}});
    return;
  }
  notify_appearance_changed();
}

// ---------------------------------------------------------------------------
// Menu Nirvana
// ---------------------------------------------------------------------------
// The window flies out of the Preferences icon on the desktop (Insight reads NIRVANA_ZOOM_FROM)
void Desktop::open_preferences() {
  for (auto &ic : icons_)
    if (ic.prefs) {
      Rect r = to_screen({ic.x + 24, ic.y, 32, 32});
      setenv("NIRVANA_ZOOM_FROM", (std::to_string(r.x) + " " + std::to_string(r.y) + " " + std::to_string(r.w) + " " +
                                   std::to_string(r.h)).c_str(), 1);
      break;
    }
  rambler_spawn({rambler_exe("nirvana-alice"), "preferences"});
  unsetenv("NIRVANA_ZOOM_FROM");
}

// --- Force Quit (like xkill) ------------------------------------------------------------
// The program of a window that does not respond is cut off from the X server (XKillClient):
// with the skull cursor click its window; the right button or Escape cancels.
static Window client_of_toplevel(Display *d, Window w, int depth = 0) {
  static Atom wm_state = XInternAtom(d, "WM_STATE", False);
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  if (XGetWindowProperty(d, w, wm_state, 0, 0, False, AnyPropertyType, &type, &fmt, &n, &after, &data) == Success) {
    if (data) XFree(data);
    if (type != None) return w;
  }
  if (depth > 4) return None;
  Window r, p, *kids = nullptr;
  unsigned nk = 0;
  Window found = None;
  if (XQueryTree(d, w, &r, &p, &kids, &nk)) {
    for (int i = (int)nk - 1; i >= 0 && !found; i--) found = client_of_toplevel(d, kids[i], depth + 1);
    if (kids) XFree(kids);
  }
  return found;
}

void Desktop::force_quit() {
  // after the menu has closed
  Fl::add_timeout(0.15, [](void *d) { ((Desktop *)d)->force_quit_pick(); }, this);
}

void Desktop::force_quit_pick() {
  Display *d = fl_display;
  Window root = DefaultRootWindow(d);
  Cursor skull = XCreateFontCursor(d, XC_pirate);
  if (XGrabPointer(d, root, False, ButtonPressMask | ButtonReleaseMask, GrabModeSync, GrabModeAsync, root, skull,
                   CurrentTime) != GrabSuccess) {
    XFreeCursor(d, skull);
    guru(_("The mouse is held by another program - try again in a moment."));
    return;
  }
  XGrabKeyboard(d, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
  Window target = None;
  bool cancel = false, pressed = false;
  for (;;) {
    XAllowEvents(d, SyncPointer, CurrentTime);
    XEvent ev;
    XMaskEvent(d, ButtonPressMask | ButtonReleaseMask | KeyPressMask, &ev);
    if (ev.type == KeyPress) {
      if (XLookupKeysym(&ev.xkey, 0) == XK_Escape) { cancel = true; break; }
      continue;
    }
    if (ev.type == ButtonPress && !pressed) {
      pressed = true;
      if (ev.xbutton.button != Button1) cancel = true;
      else target = ev.xbutton.subwindow;
    } else if (ev.type == ButtonRelease && pressed) break;
  }
  XUngrabPointer(d, CurrentTime);
  XUngrabKeyboard(d, CurrentTime);
  XFreeCursor(d, skull);
  XFlush(d);
  if (cancel || target == None) return;
  Window client = client_of_toplevel(d, target);
  if (client == None) { guru(_("There is no program window there.")); return; }
  // Nirvana's own windows (the desktop, the folders, the menu bar) are not closed this way
  for (Fl_Window *w = Fl::first_window(); w; w = Fl::next_window(w))
    if (w->shown() && (fl_xid(w) == client || fl_xid(w) == target)) {
      guru(_("This is part of Nirvana Desktop - it cannot be force quit."));
      return;
    }
  if (client == fl_xid(this)) return;
  std::string title;
  {
    Atom utf8 = XInternAtom(d, "UTF8_STRING", False), name = XInternAtom(d, "_NET_WM_NAME", False);
    Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
    if (XGetWindowProperty(d, client, name, 0, 256, False, utf8, &type, &fmt, &n, &after, &data) == Success && data) {
      title.assign((char *)data, n);
      XFree(data);
    }
    if (title.empty()) {
      char *t = nullptr;
      if (XFetchName(d, client, &t) && t) { title = t; XFree(t); }
    }
  }
  if (title.empty()) title = _("this window");
  dialog(_("Force Quit"),
         plat::fmt(_("Force quit “%1”?\nThe program is closed at once, and what was not saved in it is lost."), title),
         {{_("Cancel"), nullptr},
          {_("Force Quit"), [client] {
             XKillClient(fl_display, client);
             XFlush(fl_display);
           }}});
}

// ---------------------------------------------------------------------------
// Nirvana > Execute Command: history and suggestions
// ---------------------------------------------------------------------------
// The history (~/.config/nirvana/run-history, the last 50 commands, newest first) and, while
// the first word is typed, the programs from $PATH and the user's AppImages
// (~/.nirvana-apps). Up/Down choose in the list, Tab or a click puts the suggestion into the
// field, a double click or Enter runs it.
namespace {

struct Suggestion {
  std::string text;      // what goes into the field
  std::string kind;      // "history", "program", "AppImage"
  std::string shown;     // the list row
};

std::string run_history_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/run-history";
}

std::vector<std::string> load_run_history() {
  std::vector<std::string> v;
  std::ifstream in(run_history_file());
  std::string l;
  while (std::getline(in, l) && v.size() < 50)
    if (!l.empty()) v.push_back(l);
  return v;
}

void save_run_history(const std::string &cmd) {
  std::vector<std::string> v = load_run_history();
  v.erase(std::remove(v.begin(), v.end(), cmd), v.end());
  v.insert(v.begin(), cmd);
  if (v.size() > 50) v.resize(50);
  std::string f = run_history_file();
  std::error_code ec;
  fs::create_directories(fs::path(f).parent_path(), ec);
  std::ofstream out(f);
  for (auto &c : v) out << c << "\n";
}

// executable files in the $PATH folders (names only, sorted, without duplicates)
std::vector<std::string> path_programs() {
  std::vector<std::string> v;
  const char *p = getenv("PATH");
  std::string path = p && *p ? p : "/usr/local/bin:/usr/bin:/bin";
  size_t a = 0;
  while (a <= path.size()) {
    size_t b = path.find(':', a);
    if (b == std::string::npos) b = path.size();
    std::string dir = path.substr(a, b - a);
    a = b + 1;
    if (dir.empty()) continue;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      std::string n = it->path().filename().string();
      if (n.empty() || n[0] == '.') continue;
      if (access(it->path().c_str(), X_OK) != 0 || fs::is_directory(it->path(), ec)) continue;
      v.push_back(n);
    }
  }
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}

std::string lower(std::string s) {
  for (auto &c : s) c = (char)tolower((unsigned char)c);
  return s;
}

std::string shell_quote(const std::string &s) {
  std::string o = "'";
  for (char c : s) o += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return o + "'";
}

// the user's AppImages: (name without version and extension, full path)
std::vector<std::pair<std::string, std::string>> user_appimages(const std::string &dir) {
  std::vector<std::pair<std::string, std::string>> v;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
    std::string n = it->path().filename().string();
    std::string ln = lower(n);
    if (ln.size() < 9 || ln.compare(ln.size() - 9, 9, ".appimage") != 0) continue;
    std::string name = n.substr(0, n.size() - 9);
    // "Kalkulator-1.0-x86_64" -> "Kalkulator"
    size_t cut = name.find_first_of("-_ ");
    auto ver_at = [&](size_t i) {   // "1.0" or "v1.0"
      if (i >= name.size()) return false;
      if (isdigit((unsigned char)name[i])) return true;
      return (name[i] == 'v' || name[i] == 'V') && i + 1 < name.size() && isdigit((unsigned char)name[i + 1]);
    };
    while (cut != std::string::npos && !ver_at(cut + 1)) cut = name.find_first_of("-_ ", cut + 1);
    std::string version;
    if (cut != std::string::npos && cut > 0) { version = name.substr(cut + 1); name = name.substr(0, cut); }
    for (const char *arch : {"-x86_64", "_x86_64", "-amd64", "-i686", "-aarch64", "-arm64"})   // no architecture
      if (size_t p = version.find(arch); p != std::string::npos) version.erase(p);
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) version.erase(0, 1);
    v.push_back({version.empty() ? name : name + " " + version, it->path().string()});
  }
  std::sort(v.begin(), v.end());
  return v;
}

class RunInput;
struct RunState {
  std::vector<std::string> history, programs;
  std::vector<std::pair<std::string, std::string>> apps;
  std::vector<Suggestion> shown;
  Fl_Hold_Browser *list = nullptr;
  RunInput *in = nullptr;
  bool quiet = false;   // while we fill the field ourselves
};
RunState *g_run;

void refill();

class RunInput : public Fl_Input {
public:
  std::function<void()> run;
  RunInput(int X, int Y, int W, int H) : Fl_Input(X, Y, W, H) { when(FL_WHEN_CHANGED); }
  void take(int row) {   // the suggestion into the field
    if (!g_run || row < 1 || row > (int)g_run->shown.size()) return;
    g_run->quiet = true;
    value(g_run->shown[row - 1].text.c_str());
    insert_position(size());
    g_run->quiet = false;
    refill();
  }
  int handle(int e) override {
    if (e == FL_KEYBOARD && g_run && g_run->list) {
      int k = Fl::event_key(), n = g_run->list->size(), v = g_run->list->value();
      if ((k == FL_Down || k == FL_Up) && n > 0) {
        v = k == FL_Down ? std::min(n, v + 1) : std::max(1, v - 1);
        g_run->list->value(v);
        g_run->list->middleline(v);
        return 1;
      }
      if (k == FL_Tab && n > 0 && !Fl::event_shift()) { take(v > 0 ? v : 1); return 1; }
      if ((k == FL_Enter || k == FL_KP_Enter) && v > 0) {   // a chosen suggestion: run it
        take(v);
        if (run) run();
        return 1;
      }
      if (k == FL_Escape && v > 0) { g_run->list->value(0); return 1; }
    }
    return Fl_Input::handle(e);
  }
};

void refill() {
  if (!g_run || !g_run->list) return;
  std::string t = g_run->in->value();
  std::string lt = lower(t);
  bool first_word = t.find(' ') == std::string::npos;
  auto &v = g_run->shown;
  v.clear();
  // history: starting with the text first, then containing it
  for (int pass = 0; pass < 2; pass++)
    for (auto &h : g_run->history) {
      std::string lh = lower(h);
      bool starts = lh.rfind(lt, 0) == 0;
      if (pass == 0 ? !starts : (starts || lh.find(lt) == std::string::npos)) continue;
      if (h == t) continue;
      v.push_back({h, "history", h});
    }
  if (first_word && !t.empty()) {
    for (auto &[name, path] : g_run->apps) {   // a word of the name starts with the text
      std::string ln = lower(name);
      bool hit = false;
      for (size_t i = 0; i < ln.size() && !hit; i++)
        if ((i == 0 || ln[i - 1] == ' ') && ln.compare(i, lt.size(), lt) == 0) hit = true;
      if (hit) v.push_back({shell_quote(path), "AppImage", name});
    }
    int n = 0;
    for (auto &p : g_run->programs) {
      if (p.rfind(t, 0) != 0 || p == t) continue;
      bool dup = false;
      for (auto &s : v) if (s.text == p) dup = true;
      if (dup) continue;
      v.push_back({p, "program", p});
      if (++n >= 200) break;
    }
  }
  auto *l = g_run->list;
  l->clear();
  for (auto &s : v) {
    std::string kind = s.kind == "history" ? _("history") : s.kind == "AppImage" ? std::string("AppImage") : _("program");
    std::string row;
    for (char c : s.shown) row += c == '\t' ? ' ' : c;
    if (!row.empty() && row[0] == '@') row = "@." + row;   // a literal '@' at the start
    l->add((row + "\t@i" + kind).c_str());
  }
}

}  // namespace

void Desktop::execute_command() {
  NWin *a = active_folder();
  std::string dir = a ? a->path : home_;
  int W = 500, H = 330, ox, oy;
  NWin *win = begin_form(_("Execute Command"), W, H, nullptr,
                         plat::fmt(_("Command to execute (in folder “%1”):"), title_for(dir)), ox, oy);
  static RunState st;
  g_run = &st;
  st.history = load_run_history();
  st.programs = path_programs();
  st.apps = user_appimages(apps_dir());
  auto *in = new RunInput(ox + 64, oy + 50, W - 80, 24);
  st.in = in;
  in->box(WELL_BOX);
  in->textfont(FL_COURIER);
  in->textsize(SIZE_SYSTEM);
  if (!st.history.empty()) {
    in->value(st.history.front().c_str());
    in->insert_position(0, in->size());
  }
  auto *list = new Fl_Hold_Browser(ox + 64, oy + 80, W - 80, 160);
  st.list = list;
  list->box(WELL_BOX);
  list->textfont(FL_COURIER);
  list->textsize(SIZE_SMALL + 1);
  static int cols[] = {W - 80 - 110, 0};
  list->column_widths(cols);
  list->column_char('\t');
  list->callback([](Fl_Widget *w, void *) {
    auto *l = (Fl_Hold_Browser *)w;
    if (!g_run || !l->value()) return;
    g_run->in->take(l->value());
    g_run->in->take_focus();
    if (Fl::event_clicks() && g_run->in->run) { Fl::event_clicks(0); g_run->in->run(); }
  });
  auto *hint = new Fl_Box(ox + 64, oy + 242, W - 80, 18,
                          _("↑↓ choose · Tab completes · Enter runs"));
  hint->labelsize(SIZE_SMALL);
  hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  in->callback([](Fl_Widget *, void *) { if (g_run && !g_run->quiet) refill(); });
  refill();
  auto *show = new Fl_Check_Button(ox + 60, oy + 264, W - 80, 20, _("Show output in Nirvana Shell"));
  show->labelsize(SIZE_SMALL + 1);
  static bool show_last = true;
  show->value(show_last);
  auto go = [in, show, dir, this] {
    std::string cmd = in->value();
    if (cmd.empty()) return;
    save_run_history(cmd);
    show_last = show->value();
    // the folder and the command as shell arguments - no quote juggling
    const char *script = show_last
        ? "cd -- \"$1\" || exit 1; eval \"$2\"; s=$?; echo; printf \"$3\" \"$s\"; read x"
        : "cd -- \"$1\" || exit 1; eval \"$2\"";
    if (show_last)
      rambler_spawn({rambler_exe("nirvana-shell"), "--working-directory", dir, "-e", "sh", "-c", script, "sh", dir, cmd,
                     _("[exit code %s] Press Enter to close…")});
    else
      rambler_spawn({"sh", "-c", script, "sh", dir, cmd});
  };
  end_form(win, W, H, ox, oy, {{_("Cancel"), nullptr}, {_("Execute"), go}});
  // Enter on a chosen suggestion, or a double click: run and close the window
  in->run = [go, win, this] {
    go();
    g_run->list = nullptr;
    later([this, win] { close(win); });   // not from inside the field's own event
  };
  in->take_focus();
}

void Desktop::open_log() {
  // The system log, the first one that can be read:
  //  1. journalctl (systemd),
  //  2. /var/log/syslog or /var/log/messages (rsyslog; readable for the "adm" group),
  //  3. dmesg (blocked for ordinary users when kernel.dmesg_restrict = 1),
  //  4. the same files through pkexec (asks for the administrator password; only "cat"
  //     runs as root, the viewer runs as the user),
  //  5. at least the log of this session (~/.xsession-errors).
  std::string script =
      "if command -v journalctl >/dev/null 2>&1 && journalctl -n 1 >/dev/null 2>&1; then exec journalctl -b -e; fi\n"
      "for f in /var/log/syslog /var/log/messages; do [ -r \"$f\" ] && exec less +G \"$f\"; done\n"
      "if dmesg >/dev/null 2>&1; then dmesg | less +G; exit; fi\n"
      "f=/var/log/syslog; [ -e $f ] || f=/var/log/messages\n"
      "if [ -e $f ] && command -v pkexec >/dev/null 2>&1; then\n"
      "  printf '%s\\n\\n' \"$1\"\n"
      "  if pkexec cat \"$f\" > \"$HOME/.cache/nirvana-syslog.$$\" 2>/dev/null; then\n"
      "    less +G \"$HOME/.cache/nirvana-syslog.$$\"; rm -f \"$HOME/.cache/nirvana-syslog.$$\"; exit; fi\n"
      "  rm -f \"$HOME/.cache/nirvana-syslog.$$\"\n"
      "fi\n"
      "if [ -r \"$HOME/.xsession-errors\" ]; then\n"
      "  { printf '%s\\n\\n' \"$2\"; cat \"$HOME/.xsession-errors\"; } | less +G\n"
      "else printf '%s\\n' \"$3\"; read x; fi\n";
  mkdir((home_ + "/.cache").c_str(), 0755);
  rambler_spawn({rambler_exe("nirvana-shell"), "--working-directory", home_, "-e", "sh", "-c", script, "sh",
                 _("The system log is readable only by the administrator. Enter the password to view it."),
                 _("The system log is not available without administrator rights (tip: add yourself to the "
                   "\"adm\" group). Below is the log of this session."),
                 _("No log is available. Press Enter to close…")});
}

// System menu - the same commands as formerly in the power applet
void Desktop::system_lock() { applets::power_action(applets::PowerAction::Lock); }
void Desktop::system_sleep() { applets::power_action(applets::PowerAction::Sleep); }
void Desktop::system_hibernate() { applets::power_action(applets::PowerAction::Hibernate); }
void Desktop::system_logout() { applets::power_action(applets::PowerAction::LogOut); }
void Desktop::system_restart() { applets::power_action(applets::PowerAction::Restart); }
void Desktop::system_shutdown() { applets::power_action(applets::PowerAction::ShutDown); }

// ---------------------------------------------------------------------------
// Window menu
// ---------------------------------------------------------------------------
void Desktop::open_parent() {
  NWin *a = active_folder();
  if (!a) return;
  std::string par = fs::path(a->path).parent_path().string();
  if (par == a->path || !allowed(par) || a->path == root_path_) return;
  navigate(a, par);
}

void Desktop::update_window() {
  if (NWin *a = active_folder()) a->pane->refresh();
}

void Desktop::clean_up() {
  if (NWin *a = active_folder()) {
    a->pane->view()->clean_up();
    a->pane->resize(a->pane->x(), a->pane->y(), a->pane->w(), a->pane->h());
    a->pane->redraw();
  }
}

void Desktop::toggle_hidden() {
  show_hidden_ = !show_hidden_;
  for (auto *w : wins_) if (w->pane) w->pane->set_hidden(show_hidden_);
  save_rambler_conf();
}

void Desktop::save_rambler_conf() {
  std::ofstream(config_dir() + "/rambler.conf") << "show_hidden=" << (show_hidden_ ? 1 : 0) << "\n"
                                               << "dock_collapsed=" << (dock_collapsed_ ? 1 : 0) << "\n";
}

void Desktop::set_sort(FolderView::SortKey k) {
  if (NWin *a = active_folder()) {
    a->pane->view()->set_sort(k);
    a->pane->resize(a->pane->x(), a->pane->y(), a->pane->w(), a->pane->h());
    a->pane->redraw();
  }
}
void Desktop::sort_name() { set_sort(FolderView::ByName); }
void Desktop::sort_date() { set_sort(FolderView::ByDate); }
void Desktop::sort_size() { set_sort(FolderView::BySize); }
void Desktop::sort_kind() { set_sort(FolderView::ByKind); }

void Desktop::set_icon_size(int s) {
  if (NWin *a = active_folder()) {
    if (a->pane->view()->mode() != FolderView::Icons) a->pane->set_mode(FolderView::Icons);
    a->pane->set_icon_size(s);
    remember(a, true);
  }
}
void Desktop::icons_small() { set_icon_size(16); }
void Desktop::icons_normal() { set_icon_size(32); }
void Desktop::icons_large() { set_icon_size(64); }

// ---------------------------------------------------------------------------
// Icons menu: Information (properties of a file)
// ---------------------------------------------------------------------------
static std::string fmt_time(time_t t) {
  if (!t) return "—";
  return plat::date_time(t);
}
static std::string fmt_size(uintmax_t b) {
  char s[64];
  if (b < 1024) snprintf(s, sizeof s, "%ju B", b);
  else if (b < 1024 * 1024) snprintf(s, sizeof s, "%.1f KB", b / 1024.0);
  else if (b < 1024ull * 1024 * 1024) snprintf(s, sizeof s, "%.1f MB", b / 1048576.0);
  else snprintf(s, sizeof s, "%.2f GB", b / 1073741824.0);
  std::string r = s;
  r = plat::decimal(r);
  if (b >= 1024) r += " (" + std::to_string(b) + " B)";
  return r;
}
// Folder size - with a time limit, so a large folder does not freeze Rambler.
static std::string folder_size(const std::string &p) {
  uintmax_t total = 0;
  long files = 0;
  auto start = std::chrono::steady_clock::now();
  std::error_code ec;
  fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    std::error_code e2;
    if (it->is_regular_file(e2)) { total += it->file_size(e2); files++; }
    if ((files & 255) == 0 && std::chrono::steady_clock::now() - start > std::chrono::seconds(2))
      return plat::fmt(_("over %1 (counting stopped)"), fmt_size(total));
  }
  return plat::fmt(_("%1, files: %2"), fmt_size(total), std::to_string(files));
}

// Comment (xattr user.xdg.comment - the freedesktop standard) and launch
// arguments (ToolTypes: user.nirvana.tooltypes, a file in ~/.config as a fallback).
static std::string xattr_get(const std::string &p, const char *name, bool *supported = nullptr) {
  char buf[4096];
  ssize_t n = getxattr(p.c_str(), name, buf, sizeof buf - 1);
  if (supported) *supported = n >= 0 || errno == ENODATA;
  return n > 0 ? std::string(buf, n) : "";
}
static bool xattr_set(const std::string &p, const char *name, const std::string &v) {
  if (v.empty()) return removexattr(p.c_str(), name) == 0 || errno == ENODATA;
  return setxattr(p.c_str(), name, v.data(), v.size(), 0) == 0;
}
static std::string tooltypes_file() { return config_dir() + "/tooltypes"; }
std::string tooltypes(const std::string &path) {
  bool ok = false;
  std::string v = xattr_get(path, "user.nirvana.tooltypes", &ok);
  if (v.empty()) v = xattr_get(path, "user.nirvana.parametry", &ok);   // written before 0.0.7
  if (!v.empty()) return v;
  std::ifstream in(tooltypes_file());
  std::string line;
  while (std::getline(in, line)) {
    auto tab = line.find('\t');
    if (tab != std::string::npos && line.substr(tab + 1) == path) return line.substr(0, tab);
  }
  return "";
}
static void set_tooltypes(const std::string &path, const std::string &v) {
  if (xattr_set(path, "user.nirvana.tooltypes", v)) {
    removexattr(path.c_str(), "user.nirvana.parametry");   // name used before 0.0.7
    return;
  }
  // a file system without extended attributes: stored in a file
  std::vector<std::string> keep;
  std::ifstream in(tooltypes_file());
  std::string line;
  while (std::getline(in, line)) {
    auto tab = line.find('\t');
    if (tab != std::string::npos && line.substr(tab + 1) != path) keep.push_back(line);
  }
  in.close();
  std::ofstream out(tooltypes_file());
  for (auto &l : keep) out << l << '\n';
  if (!v.empty()) out << v << '\t' << path << '\n';
}
// Splitting arguments as in the shell (spaces, quotes).
std::vector<std::string> split_args(const std::string &s) {
  std::vector<std::string> v;
  std::string cur;
  bool any = false;
  char q = 0;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (q) { if (c == q) q = 0; else cur += c; continue; }
    if (c == '"' || c == '\'') { q = c; any = true; continue; }
    if (c == '\\' && i + 1 < s.size()) { cur += s[++i]; any = true; continue; }
    if (isspace((unsigned char)c)) { if (any || !cur.empty()) v.push_back(cur); cur.clear(); any = false; continue; }
    cur += c;
  }
  if (any || !cur.empty()) v.push_back(cur);
  return v;
}

void Desktop::get_info() {
  auto t = targets();
  if (t.empty()) return;
  std::string path = t[0];
  // a disk from the desktop: capacity and file system
  int vi = -1;
  if (!active_folder() && icon_sel_ >= 0 && icon_sel_ < (int)icons_.size()) vi = icons_[icon_sel_].vol;
  Entry e;
  e.path = path;
  e.name = fs::path(path).filename().string();
  std::error_code ec;
  e.dir = fs::is_directory(path, ec);
  e.icon = icon_for_path(path, e.dir);
  e.label = e.name;
  if (NWin *a = active_folder())
    for (auto &x : a->pane->view()->selection()) if (x.path == path) e = x;
  if (vi >= 0) { e.icon = vols_[vi].icon; e.label = vols_[vi].name; }
  if (path == home_) { e.icon = Icon::Home; e.label = "Nirvana HD"; }

  struct stat st{};
  bool have_st = !path.empty() && lstat(path.c_str(), &st) == 0;
  bool is_exec = have_st && S_ISREG(st.st_mode);
  int W = 420, ox, oy;
  int rows = 8;
  bool tool = is_exec && (is_appimage(e.name) || (st.st_mode & S_IXUSR));
  int H = 64 + rows * 20 + 90 + (tool ? 30 : 0) + 44;
  NWin *win = begin_form(plat::fmt(_("Info: %1"), e.label), W, H, &e, "", ox, oy);
  auto *name = new Fl_Box(ox + 64, oy + 20, W - 80, 24);
  name->copy_label(e.label.c_str());
  name->labelfont(FONT_SYSTEM);
  name->labelsize(SIZE_SYSTEM + 2);
  name->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  int y = oy + 62, lx = ox + 10, lw = 120, vx = ox + 136, vw = W - 150;
  auto row = [&](const char *l, const std::string &v) {
    row_label(lx, y, lw, l);
    row_value(vx, y, vw, v);
    y += 20;
  };
  if (vi >= 0) {
    const vol::Volume &v = vols_[vi];
    row(_("Kind:"), v.optical ? _("disc") : v.removable ? _("removable disk") : _("disk"));
    row(_("File system:"), v.unformatted ? _("none (unformatted)") : v.fstype);
    row(_("Capacity:"), fmt_size(v.size));
    struct statvfs sv{};
    if (!v.mountpoint.empty() && statvfs(v.mountpoint.c_str(), &sv) == 0)
      row(_("Free:"), fmt_size((uintmax_t)sv.f_bavail * sv.f_frsize));
    row(_("Device:"), v.dev);
    row(_("Mounted at:"), v.mountpoint.empty() ? std::string(_("no")) : v.mountpoint);
    row(_("Encryption:"), v.encrypted ? (v.unlocked ? _("LUKS (unlocked)") : _("LUKS (locked)")) : _("none"));
    y += 20;
  } else {
    row(_("Kind:"), e.dir ? std::string(_("folder")) : kind_name(e));
    row(_("Size:"), e.dir ? folder_size(path) : have_st ? fmt_size((uintmax_t)st.st_size) : "—");
    row(_("Where:"), fs::path(path).parent_path().string());
    struct statx sx{};
    time_t born = 0;
    if (statx(AT_FDCWD, path.c_str(), 0, STATX_BTIME, &sx) == 0 && (sx.stx_mask & STATX_BTIME)) born = sx.stx_btime.tv_sec;
    row(_("Created:"), born ? fmt_time(born) : "—");
    row(_("Modified:"), have_st ? fmt_time(st.st_mtime) : "—");
    row(_("Opened:"), have_st ? fmt_time(st.st_atime) : "—");
    std::string owner = "—";
    if (have_st) {
      passwd *pw = getpwuid(st.st_uid);
      group *gr = getgrgid(st.st_gid);
      owner = std::string(pw ? pw->pw_name : std::to_string(st.st_uid)) + " / " +
              (gr ? gr->gr_name : std::to_string(st.st_gid));
    }
    row(_("Owner:"), owner);
    char perm[16];
    snprintf(perm, sizeof perm, "%04o", have_st ? st.st_mode & 07777 : 0);
    row(_("Permissions:"), perm);
  }
  // flags and comment
  Fl_Check_Button *ro = nullptr, *ex = nullptr;
  Fl_Input *comment = nullptr, *args = nullptr;
  if (vi < 0 && have_st) {
    row_label(lx, y, lw, _("Flags:"));
    ro = new Fl_Check_Button(vx - 4, y - 1, 140, 20, _("Read only"));
    ro->labelsize(SIZE_SMALL + 1);
    ro->value(!(st.st_mode & S_IWUSR));
    if (S_ISREG(st.st_mode)) {
      ex = new Fl_Check_Button(vx + 136, y - 1, 140, 20, _("Program"));
      ex->labelsize(SIZE_SMALL + 1);
      ex->value((st.st_mode & S_IXUSR) != 0);
      ex->tooltip(_("Execute permission (the e flag)"));
    }
    y += 26;
    bool xa = false;
    std::string c = xattr_get(path, "user.xdg.comment", &xa);
    row_label(lx, y + 2, lw, _("Comment:"));
    comment = new Fl_Input(vx, y, vw, 22);
    comment->box(WELL_BOX);
    comment->textsize(SIZE_SMALL + 1);
    comment->value(c.c_str());
    if (!xa) { comment->deactivate(); comment->tooltip(_("This file system does not support comments.")); }
    y += 28;
    if (tool) {
      row_label(lx, y + 2, lw, _("Arguments:"));
      args = new Fl_Input(vx, y, vw, 22);
      args->box(WELL_BOX);
      args->textfont(FL_COURIER);
      args->textsize(SIZE_SMALL + 1);
      args->value(tooltypes(path).c_str());
      args->tooltip(_("Program start arguments (ToolTypes), e.g. --fullscreen"));
      y += 28;
    }
  }
  bool owner_ok = have_st && st.st_uid == getuid();
  if (!owner_ok) { if (ro) ro->deactivate(); if (ex) ex->deactivate(); }
  end_form(win, W, H, ox, oy, {{_("Cancel"), nullptr}, {"OK", [this, path, ro, ex, comment, args, have_st, st] {
    std::string err;
    if (have_st && (ro || ex)) {
      mode_t m = st.st_mode & 07777;
      if (ro) m = ro->value() ? (m & ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH)) : (m | S_IWUSR);
      if (ex) m = ex->value() ? (m | S_IXUSR | ((m & S_IRGRP) ? S_IXGRP : 0) | ((m & S_IROTH) ? S_IXOTH : 0))
                              : (m & ~(mode_t)(S_IXUSR | S_IXGRP | S_IXOTH));
      if (m != (st.st_mode & 07777) && chmod(path.c_str(), m) != 0) err += _("Could not change the flags.") + std::string("\n");
    }
    if (comment && comment->active()) xattr_set(path, "user.xdg.comment", comment->value());
    if (args) set_tooltypes(path, args->value());
    refresh_all();
    if (!err.empty()) dialog(GURU_TITLE, err, {{"OK", nullptr}});
  }}});
}

// ---------------------------------------------------------------------------
// Snapshot and the dock (Leave Out / Put Away)
// ---------------------------------------------------------------------------
void Desktop::snapshot() {
  NWin *a = active_folder();
  if (!a) return;
  FolderView *v = a->pane->view();
  std::vector<std::string> p;
  for (auto &e : v->selection()) p.push_back(e.path);
  if (p.empty()) p = v->all_paths();   // nothing selected: the whole window
  v->snapshot(p);
}

void Desktop::unsnapshot() {
  NWin *a = active_folder();
  if (!a) return;
  FolderView *v = a->pane->view();
  std::vector<std::string> p;
  for (auto &e : v->selection()) p.push_back(e.path);
  if (p.empty()) p = v->all_paths();
  v->unsnapshot(p);
  a->pane->resize(a->pane->x(), a->pane->y(), a->pane->w(), a->pane->h());
  a->pane->redraw();
}

void Desktop::load_dock() {
  dock_.clear();
  std::ifstream in(config_dir() + "/dock");
  std::string line;
  std::error_code ec;
  while (std::getline(in, line))
    if (!line.empty() && fs::exists(line, ec)) dock_.push_back(line);
}

void Desktop::save_dock() {
  std::ofstream out(config_dir() + "/dock");
  for (auto &p : dock_) out << p << '\n';
}

void Desktop::dock_add() {
  NWin *a = active_folder();
  if (!a) return;
  for (auto &e : a->pane->view()->selection())
    if (std::find(dock_.begin(), dock_.end(), e.path) == dock_.end()) dock_.push_back(e.path);
  save_dock();
  layout_icons();
  redraw();
}

void Desktop::dock_remove() {
  std::vector<std::string> rm;
  if (NWin *a = active_folder()) for (auto &e : a->pane->view()->selection()) rm.push_back(e.path);
  else if (icon_sel_ >= 0 && icon_sel_ < (int)icons_.size() && icons_[icon_sel_].dock) rm.push_back(icons_[icon_sel_].path);
  for (auto &p : rm) dock_.erase(std::remove(dock_.begin(), dock_.end(), p), dock_.end());
  save_dock();
  icon_sel_ = -1;
  layout_icons();
  redraw();
}

// ---------------------------------------------------------------------------
// Special menu
// ---------------------------------------------------------------------------
void Desktop::format_disk() {
  if (active_folder() || icon_sel_ < 0 || icon_sel_ >= (int)icons_.size() || icons_[icon_sel_].vol < 0) {
    dialog(_("Format Disk"), _("Select the icon of the disk you want to format on the desktop."), {{"OK", nullptr}});
    return;
  }
  format_dialog(icons_[icon_sel_].vol);
}

void Desktop::format_dialog(int vi) {
  vol::Volume v = vols_[vi];
  Entry e;
  e.icon = v.icon;
  int W = 440, H = 220, ox, oy;
  NWin *win = begin_form(_("Format Disk"), W, H, &e,
                         plat::fmt(_("Formatting “%1” (%2)."), v.name, vol::human_size(v.size)), ox, oy);
  auto *warn = new Fl_Box(ox + 64, oy + 40, W - 80, 18, _("All data on this disk will be erased!"));
  warn->labelcolor(ALERT);
  warn->labelfont(FONT_SYSTEM);
  warn->labelsize(SIZE_SMALL + 1);
  warn->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  auto *name = new Fl_Input(ox + 180, oy + 70, W - 196, 22, _("Name:"));
  name->box(WELL_BOX);
  name->labelsize(SIZE_SMALL + 1);
  name->value(v.unformatted ? "DISK" : v.name.substr(0, 11).c_str());
  auto *fsch = new Fl_Choice(ox + 180, oy + 100, W - 196, 22, _("File system:"));
  fsch->labelsize(SIZE_SMALL + 1);
  fsch->textsize(SIZE_SMALL + 1);
  fsch->add(_("FAT32 (USB stick - works everywhere)|exFAT (large files, Windows and Mac)|ext4 (Linux)|NTFS (Windows)"));
  fsch->value(v.size > 32ull * 1024 * 1024 * 1024 ? 1 : 0);
  std::string uuid = v.uuid, vname = v.name;
  end_form(win, W, H, ox, oy, {{_("Cancel"), nullptr}, {_("Format"), [this, name, fsch, uuid, vname] {
    static const char *types[] = {"vfat", "exfat", "ext4", "ntfs"};
    std::string type = types[std::max(0, fsch->value())], label = name->value();
    if (type == "vfat") { label = label.substr(0, 11); for (auto &c : label) c = (char)toupper((unsigned char)c); }
    dialog(_("Format Disk"), plat::fmt(_("Really format “%1”? This cannot be undone."), vname),
           {{_("Cancel"), nullptr}, {_("Format"), [this, uuid, type, label] {
              int idx = -1;
              for (int i = 0; i < (int)vols_.size(); i++) if (vols_[i].uuid == uuid) idx = i;
              if (idx < 0) { dialog(_("Format Disk"), _("The disk is gone."), {{"OK", nullptr}}); return; }
              for (auto *w : wins_) w->cursor(FL_CURSOR_WAIT);
              cursor(FL_CURSOR_WAIT);
              Fl::flush();
              std::string err = vol::format(vols_[idx], type, label);
              for (auto *w : wins_) w->cursor(FL_CURSOR_DEFAULT);
              cursor(FL_CURSOR_DEFAULT);
              rescan_volumes();
              dialog(_("Format Disk"), err.empty() ? plat::fmt(_("The disk “%1” is ready."), label) : err, {{"OK", nullptr}});
            }}});
  }}});
}

// Reset Nirvana: Insight and Rambler start again (program windows stay).
void Desktop::reset_nirvana() {
  for (auto *w : wins_) if (w->pane) remember(w, true);
  spatial_.save();
  Display *d = fl_display;
  XEvent ev{};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = DefaultRootWindow(d);
  ev.xclient.message_type = XInternAtom(d, "_NIRVANA_RESTART", False);
  ev.xclient.format = 32;
  XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(d);
  Fl::add_timeout(0.6, [](void *) {
    std::string exe = rambler_exe("nirvana-denise");
    std::vector<char *> av;
    av.push_back((char *)exe.c_str());
    for (int i = 1; i < g_argc; i++) av.push_back(g_argv[i]);
    av.push_back(nullptr);
    tray::shutdown();
    XCloseDisplay(fl_display);
    execv(exe.c_str(), av.data());
    _exit(1);
  });
}
