// Nirvana Desktop - denise: the desktop, the menu bar and the file manager windows (Rambler)
//
// The desktop (pattern, disk and Trash icons, the global menu bar) is a single X window
// of type DESKTOP. Every folder and every dialog is a separate X window - frames,
// moving, shading and focus are provided by the window manager (Insight).
// The menu bar acts on the active window, which we read from _NET_ACTIVE_WINDOW.
#pragma once
#include "dock.h"
#include "openwith.h"
namespace jobs { struct Job; }

#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Tiled_Image.H>
#include <functional>
#include <memory>
#include <set>
#include <map>
#include <string>
#include <vector>

#include "anim.h"
#include "folder_view.h"
#include "gary.h"
#include "net.h"
#include "volumes.h"
#include "spatial.h"

// The home folder is the disk "Nirvana HD": "Nirvana HD/Muzyka" for people, and back
std::string disk_path(const std::string &path);
std::string real_path(const std::string &text);

class NirvanaDesktopCanvas;
void rambler_spawn(const std::vector<std::string> &args);   // double fork
std::string rambler_exe(const char *name);                  // np. "nirvana-shell"
std::string tooltypes(const std::string &path);             // launch arguments (Information)
std::vector<std::string> split_args(const std::string &s);

// A top-level Rambler window: a folder or a dialog.
struct Explorer;   // explorer.cxx: the tree, two panes, the button bar

class NirvanaDrawerWindow : public Fl_Double_Window {
public:
  NirvanaDrawerWindow(NirvanaDesktopCanvas *d, int X, int Y, int W, int H, const char *title, bool dialog);

  NirvanaDesktopCanvas *desk;
  FolderPane *pane = nullptr;                  // nullptr for dialogs; in an explorer: the active pane
  Explorer *ex = nullptr;                      // an explorer window (Preferences > Desktop)
  std::string path;
  // Folder window: its own menu bar (Window, Icons) and a navigation bar
  // (back, forward, parent folder, location) above the content.
  Fl_Menu_Bar *menubar = nullptr;
  Fl_Button *back_b = nullptr, *fwd_b = nullptr, *up_b = nullptr;
  Fl_Input *loc = nullptr;
  std::vector<std::string> back_hist, fwd_hist;   // browsing history
  static constexpr int MB_H = 20, NAV_H = 26;     // bar heights
  std::vector<std::function<void()>> actions;  // actions of the dialog buttons
  bool dialog;
  // Our own Platinum frame (CSD) when the window manager is not Insight -
  // e.g. under GNOME. Then the window has no decorations and the content lies inside the frame.
  bool csd;
  int ox() const { return csd ? plat::FRAME : 0; }          // content offset
  int oy() const { return csd ? plat::TITLE_H : 0; }
  int extra_w() const { return csd ? 2 * plat::FRAME : 0; } // the whole frame
  int extra_h() const { return csd ? plat::TITLE_H + plat::FRAME : 0; }
  plat::Rect content_rect();  // position of the content on screen (full height)

  bool is_active();     // does this window have the focus (according to the window manager)
  bool shaded();       // has the window manager shaded it
  void set_shaded(bool s);    // ask the window manager to shade/unshade
  void activate_window();     // raise and give the focus
  plat::Rect frame_rect();   // approximate outline with the frame (for animations)

  ~NirvanaDrawerWindow() override;
  int handle(int e) override;   // keyboard shortcuts go to the global menu bar
  void resize(int X, int Y, int W, int H) override;
  void draw() override;

private:
  // state of our own frame
  int csd_handle(int e);
  int hit_button(int ex, int ey) const;
  void csd_toggle_shade();
  void csd_toggle_zoom();
  void start_moveresize(int direction);
  bool csd_shaded_ = false, csd_zoomed_ = false;
  int full_h_ = 0, unzoom_[4] = {0, 0, 0, 0};
  enum { NONE, PENDING, MOVE, GROW, BTN } drag_ = NONE;   // PENDING: pressed, no movement yet
  int press_x_ = 0, press_y_ = 0;
  int drag_dx_ = 0, drag_dy_ = 0, armed_ = 0, pressed_ = 0;
};

class NirvanaDesktopCanvas : public Fl_Double_Window {
public:
  NirvanaDesktopCanvas(int W, int H, const std::string &home);

  // A folder in a file manager window: by default in the most recently used window
  // (one window that navigates), new_window - always in a new window.
  void open_folder(const std::string &path, bool animate = true, bool new_window = false);
  bool force_simple_ = false;   // the next window: a plain one, also in the explorer mode (Applications)
  enum class Nav { Go, Back, Forward };
  void navigate(NirvanaDrawerWindow *w, const std::string &path, Nav how = Nav::Go);
  void update_nav(NirvanaDrawerWindow *w);       // buttons and the location field
  void go_back();
  void go_forward();
  void new_window();
  void iconify_active();
  void leave_out();
  void put_away();
  void put_away_path(const std::string &path);
  std::vector<std::string> left_out() const;
  void save_left_out(const std::vector<std::string> &v);
  void go_location(NirvanaDrawerWindow *w);      // Enter in the location field
  NirvanaDrawerWindow *menu_target = nullptr;    // the window whose menu bar the command was chosen from
  void restore();                 // open the windows remembered from the previous session
  void close(NirvanaDrawerWindow *w);            // closing a window (frame button, Ctrl+W, dialog)
  void remember(NirvanaDrawerWindow *w, bool open);
  void update_global_keys();    // the global shortcuts (Win+letter, XGrabKey)
  void active_changed();
  void screen_changed();
  void rebuild_global_menu();   // Nirvana's menus + those of the active program (global menu)
  void fit_menu_width();          // the window manager changed the active window
  void clients_changed();         // the window list changed (_NET_CLIENT_LIST)
  void current_app(std::string &name, Fl_Image *&icon);   // the active application for the bar
  std::vector<NirvanaDrawerWindow *> folder_windows();                    // visible folder windows
  int shortcut();                 // a shortcut from another window -> the menu bar

  // Window environment (detected at startup)
  bool csd_mode = false;          // the window manager is not Insight: we draw the frames ourselves
  plat::DesktopSettings desk_set_ = plat::read_desktop();   // margins for windows (Preferences > Desktop)
  void desktop_settings_changed();
  bool wm_present = false;        // any EWMH-compliant window manager
  bool wm_moveresize = false;     // the WM supports _NET_WM_MOVERESIZE (moves for us)
  NirvanaDrawerWindow *manual_active = nullptr;  // the active window when there is no window manager
  void redraw_frames();           // refresh the frames (active window change)
  void detect_wm();
  plat::Rect desk_rect() { return {x_root(), y_root(), w(), h()}; }

  int handle(int e) override;
  void draw() override;

  // menu actions
  void about();
  void new_folder();
  void open_selected();
  void close_active();
  void move_to_trash();
  void empty_trash();
  void force_quit();          // like xkill: a skull cursor, click a window, its program is closed
  void force_quit_pick();
  void refresh_all();
  void appearance_changed();      // the Appearance panel changed the scheme (_NIRVANA_APPEARANCE)
  void open_appearance();         // start the Appearance panel (nirvana-alice)
  void open_network();            // Network panel
  void open_power();              // Power panel
  void open_terminal();           // Nirvana Shell in the active window's folder
  void open_terminal_home();
  // network (menu bar)
  void build_net_menu(Fl_Menu_Button *m);
  void net_action(bool connect, const std::string &iface);
  void net_refresh();
  void vpn_toggle(const net::Vpn &v, const std::string &msg);
  void wifi_connect(const net::Network &n, const std::string &msg);
  const net::State &net_state() const { return net_; }
  void dialog_message(const std::string &title, const std::string &text);
  void quit();
  void build_windows_menu(Fl_Menu_Button *m);
  void rename_selected();
  void eject_selected();
  void duplicate();
  void copy_selected();
  void paste();
  // files dragged from other programs (file:// list) - copied into dest
  void drop_uris(const std::string &uri_list, const std::string &dest);
  void select_all();
  void view_icons();
  void view_list();
  void shade_all(bool shade);
  void close_all();
  // --- Nirvana / Window / Icons / Special menus (menus.cxx) ---
  void update_menu();            // active/inactive items before a menu opens
  void update_menu(Fl_Menu_ *m, NirvanaDrawerWindow *a);   // the same for the given menu bar (folder window)
  // Context menus (right button) open after the click has been handled, not during it -
  // an FLTK menu opened straight from FL_PUSH could crash when quickly
  // reopened. X, Y - position in the window that was clicked.
  void desk_context_menu(int icon, int X, int Y);
  void folder_context_menu(NirvanaDrawerWindow *w, int X, int Y);
  static void later(std::function<void()> f);   // run after the current event
  Fl_Menu_Button *ctx_style_ = nullptr;           // hidden button: the look of the context menus
  bool desk_shortcut();                     // shortcuts of desktop icons (no active window)
  void open_preferences();
  void open_apps();
  std::string desktop_dir() const;
  std::string library_dir() const;
  std::string ramdisk_label() const;   // ~/Pulpit (XDG_DESKTOP_DIR): its files and folders are on the desktop
  void watch_desktop_dir();
  int desk_ino_ = -1;
  void screenshot();                 // Special > Screenshot (Print): the screen into Pictures   // the Applications icon: always a plain (simple) Rambler window
  void execute_command();
  void open_log();
  // menu System (applets::power_action)
  void system_lock();
  void system_sleep();
  void system_hibernate();
  void system_logout();
  void system_restart();
  void system_shutdown();
  void open_parent();
  void update_window();
  void clean_up();
  void toggle_hidden();
  void sort_name();
  void sort_date();
  void sort_size();
  void sort_kind();
  void icons_small();
  void icons_normal();
  void icons_large();
  void get_info();
  void set_as_wallpaper();   // the selected image onto the desktop
  void snapshot();
  void unsnapshot();
  void dock_add();
  void dock_remove();
  void format_disk();
  void reset_nirvana();
  // --- the explorer (explorer.cxx): folder tree, one or two panes (source/destination) ---
  // folder windows: drawers (each folder its own window, remembering its place - the default)
  // or Rambler Advanced (tree, two panes); Preferences > Desktop > Folder Windows
  bool explorer_mode() const { return desk_set_.folder_style == "explorer"; }
  std::string folder_title(const std::string &path) const { return title_for(path); }   // "wakacje.zip", not the cache folder
  void build_explorer(NirvanaDrawerWindow *win, int ox, int oy, int W, int H, const std::string &path);
  void ex_layout(NirvanaDrawerWindow *w);
  void ex_activate(NirvanaDrawerWindow *w, int idx);
  void ex_after_nav(NirvanaDrawerWindow *w);
  bool ex_key(NirvanaDrawerWindow *w);
  FolderPane *ex_pane_at(NirvanaDrawerWindow *w, int lx, int ly);
  void ex_toggle_tree();
  void ex_toggle_dual();
  void ex_copy_to_other();
  void ex_move_to_other();
  void ex_swap_panes();
  Fl_Menu_Bar *make_folder_menubar(NirvanaDrawerWindow *win, int X, int Y, int W);
  void setup_pane(NirvanaDrawerWindow *win, FolderPane *p, int idx);   // callbacks of a file list (idx: explorer pane, -1)
  // --- file types (filetypes.cxx): actions for double click and the context menu ---
  bool run_filetype(const Entry &e, NirvanaDrawerWindow *from, int action);   // false: no type / no action
  // the type's actions for the context menu of the selection (label, what it does)
  std::vector<std::pair<std::string, std::function<void()>>> filetype_menu(const std::vector<Entry> &sel, NirvanaDrawerWindow *w);
  void edit_filetypes();
  void show_jobs();   // the background jobs window (jobs.cxx)
  // --- search (search.cxx), preview and the information panel (explorer.cxx) ---
  void open_search();
  void open_found(const std::string &path, bool in_folder);
  void copy_found(const std::vector<Entry> &items);
  void preview_selected();   // Space: the type's preview action, or the built-in preview
  void ex_toggle_info();
  void ex_set_filter(NirvanaDrawerWindow *w, const std::string &f);
  void ex_save_state();
  void ex_refresh_trees();
  void ex_trash_changed(bool full);
  std::string ex_tree_target(NirvanaDrawerWindow *w, int lx, int ly);   // a drop on the explorer's tree
  void ex_tree_mark(NirvanaDrawerWindow *w, const std::string &path);
  bool trash_is_full() const { return trash_full(); }                                  // the disks changed: the explorer trees again
  void ex_open_volume(NirvanaDrawerWindow *w, const std::string &kname);   // a disk clicked in the tree
  const std::vector<vol::Volume> &volumes() const { return vols_; }
  std::vector<std::string> ex_saved_windows() const;   // explorer mode: the windows to open at the start      // the explorer windows' state (~/.config/nirvana/explorer.conf)
  void release_archives();   // archives no window shows any more: written back and closed
  void reconnect_servers();  // the remembered servers, at the start
  void servers_changed();    // the tree and the desktop icons follow
  bool run_event(const Entry &e, NirvanaDrawerWindow *from, const std::string &event);   // file type events
  // --- archives as folders and servers (explorer.cxx) ---
  void browse_archive(NirvanaDrawerWindow *w, const std::string &archive);
  bool is_archive_cache(const std::string &path, std::string *archive = nullptr, std::string *inner = nullptr) const;
  void pack_selected();
  void connect_server() { connect_server_to({}, ""); }
  // the dialog filled in: kind, host, port, user, folder (a remembered server that needs a password)
  void connect_server_to(const std::vector<std::string> &spec, const std::string &note);
  void disconnect_server(const std::string &mountpoint);
  std::vector<std::pair<std::string, std::string>> servers() const;
  std::string server_path(const std::string &url) const;   // "sftp://me@host/dir" -> the folder ("" if not connected)   // (label, mount point)
  mutable std::map<std::string, std::string> archive_dirs_;   // cache folder -> archive file
  bool is_known(NirvanaDrawerWindow *w) const { return known(w); }
  void error_box(const std::string &t) { guru(t); }
  static void set_argv(int argc, char **argv);
  NirvanaDrawerWindow *begin_form(const std::string &title, int W, int H, const Entry *icon, const std::string &text,
                   int &ox, int &oy);
  void end_form(NirvanaDrawerWindow *win, int W, int H, int ox, int oy,
                std::vector<std::pair<std::string, std::function<void()>>> buttons);

private:
  struct DeskIcon {
    std::string name, path;
    plat::Icon icon;
    int x, y;
    int vol = -1;   // index into vols_ for disk icons
    bool dock = false;   // an icon left out on the desktop (the dock in the bottom left corner)
    Entry entry;         // for the dock: file/folder/application
    unsigned long win = 0;      // icon of an iconified window (Window); a double click restores it
    bool prefs = false;         // the Preferences icon (opens the panel, not a folder)
    bool server = false;        // a connected server (SFTP, FTP, SMB) - like a disk
    bool deskfile = false;      // a file or folder of the Desktop folder (~/Pulpit), in .entry
    bool leftout = false;       // left out from a folder (Icons > Leave Out), Put Away takes it back
    bool ramdisk = false;       // the Ram Disk (~/.ramdisk), its label shows how full it is
    // files cannot be dropped here (a program on the desktop takes them: it starts with them)
    bool no_drop() const { return win || prefs || (deskfile && !entry.dir && entry.icon != plat::Icon::Application); }
    Fl_Image *img = nullptr;    // its picture (from _NET_WM_ICON or the AppImage)
  };
  int icon_cols_ = 1;   // icon columns at the left edge (disks, windows, Trash)
  // the dock (dock.h): its items, geometry and drawing; its tiles are icons of the canvas
  friend class NirvanaDock;
  NirvanaDock dock_{this};
  std::string backdrop_dir() const;
  void sync_backdrop_folder(const std::vector<std::pair<std::string, std::string>> &items);
  void toggle_backdrop();
  int tip_icon_ = -1;             // the tile with a visible tooltip
  void save_rambler_conf();
  void format_dialog(int vi);
  std::vector<std::string> targets();   // selected icons (window or desktop)
  void set_icon_size(int s);
  void set_sort(FolderView::SortKey k);
  bool show_hidden_ = false;
  // --- disks and discs ---
  std::vector<vol::Volume> vols_;
  void watch_volumes();
  void rescan_volumes();
  static void rescan_later(void *d);
  static void poll_volumes(void *d);
  void open_volume(int vi);
  void eject_volume(int vi);
  void ask_password(const vol::Volume &v, const std::string &msg);
  void ask_secret(const std::string &title, plat::Icon icon, const std::string &text,
                  const std::string &msg, const char *ok_label,
                  std::function<void(std::string &)> on_ok);
  int volume_at_path(const std::string &path) const;   // the volume containing the path
  // dragging a disk icon over the desktop (to the Trash = eject)
  int vdrag_icon_ = -1, vpress_x_ = 0, vpress_y_ = 0;
  bool vdragging_ = false;
  Fl_Window *vghost_ = nullptr;

  NirvanaDrawerWindow *find(const std::string &path);
  bool known(NirvanaDrawerWindow *w) const;
  NirvanaDrawerWindow *active_folder();          // the active folder window (or nullptr)
  void layout_icons();
public:
  void iconic_changed() { icon_sel_ = -1; layout_icons(); redraw(); }
  // The area for folder windows (window content, desktop coordinates): without the
  // icon column on the left and without the dock at the bottom - always visible, also after zoom.
  plat::Rect folder_area() const;
private:
  void publish_folder_area();
  Fl_RGB_Image *render_wallpaper(int W, int H, bool *ok);
  void watch_trash();
  int icon_at(int ex, int ey) const;
  bool allowed(const std::string &path) const;
  bool source_rect(const std::string &path, plat::Rect &out);  // the icon the window "flies out" of
  plat::Rect to_screen(plat::Rect r) const { return {r.x + x_root(), r.y + y_root(), r.w, r.h}; }
  std::string title_for(const std::string &path) const;
  std::string trash_dir() const;
  bool trash_full() const;
  void open_entry(const Entry &e, NirvanaDrawerWindow *from = nullptr);   // folder: navigation in the window from
  void build_folder_bars(NirvanaDrawerWindow *win, int X, int Y, int W);
  bool desk_target_ = false;      // a command from the desktop context menu
  void open_appimage(const std::string &path);
  void run_app(const std::string &path, const std::vector<std::string> &files = {});   // files: Open With
  // Open With (the context menu of files)
  std::vector<std::string> open_with_files(const std::vector<Entry> &sel);
  void open_with_app(const openwith::App &a, const std::vector<std::string> &files);
  void open_with_program(const std::string &prog, const std::vector<std::string> &files);
  bool is_program(const std::string &path) const;   // an AppImage or an executable file (a drop starts it)
  void ask_missing_libs(const std::string &path, const std::vector<std::string> &missing);
  void install_libs(const std::string &path, const std::vector<std::string> &missing);
  std::set<std::string> libs_ok_;   // applications started even though libraries are missing
  std::string apps_dir() const;
  bool in_apps(const std::string &path) const;
  std::string app_destination(const std::string &path) const;
  // file operations
  std::string rename_path(const std::string &old, const std::string &name);
  bool trash_move(jobs::Job &j, const std::string &path, std::string &err);   // in a job's thread
  void trash_paths(const std::vector<std::string> &paths);                     // a job in the background
  void transfer(const std::vector<Entry> &items, const std::string &dest, bool copy, bool replace);
  void path_moved(const std::string &from, const std::string &to);   // windows and position memory
  // dragging
  struct DropTarget { NirvanaDrawerWindow *win = nullptr; FolderPane *pane = nullptr; int item = -2; int icon = -1; std::string tree; };
  bool pointer_over_foreign() const;   // over a window of another program (not Nirvana's)
  void drag_out();                    // the drag goes on as XDND into that program
  DropTarget target_at(int xr, int yr);
  void drag_start(NirvanaDrawerWindow *src, const std::vector<Entry> &items);
  void drag_move(int xr, int yr);
  void drag_end(int xr, int yr, bool copy);
  void clear_drop_highlight();
  struct {
    bool active = false;
    NirvanaDrawerWindow *src = nullptr;
    FolderPane *src_pane = nullptr;
    std::vector<Entry> items;
    Fl_Window *ghost = nullptr;
    NirvanaDrawerWindow *hl_win = nullptr;
    FolderPane *hl_pane = nullptr;
    int px = 0, py = 0;   // where the drag started (screen)
  } drag_;
  int icon_drop_ = -1;            // the desktop icon highlighted as the drop target
  std::vector<std::string> clipboard_;   // paths copied with Icons > Copy
  void guru(const std::string &text);   // an error window: "Guru Meditation"
  void dialog(const std::string &title, const std::string &text,
              std::vector<std::pair<std::string, std::function<void()>>> buttons);
  static void dialog_cb(Fl_Widget *b, void *idx);

  std::string home_, root_path_;
  Spatial spatial_;
  std::vector<NirvanaDrawerWindow *> wins_;      // order: most recently active at the end
  std::vector<DeskIcon> icons_;
  int icon_sel_ = -1;
  Fl_Menu_Bar *menu_;
  Fl_Widget *app_menu_ = nullptr;   // gary: the application menu in the top right corner
  int active_app_index(const std::vector<gary::App> &apps);
  Fl_Widget *extras_;
  Fl_Widget *screens_w_ = nullptr;   // the virtual screens gadget
  Fl_Widget *vol_w_ = nullptr, *pow_w_ = nullptr, *upd_w_ = nullptr, *prn_w_ = nullptr, *bt_w_ = nullptr, *clip_w_ = nullptr, *bell_w_ = nullptr, *shield_w_ = nullptr, *tray_w_ = nullptr, *mem_w_ = nullptr;
  bool tray_pending_ = false;
public:
  void layout_bar();        // placing the applets and the tray in the menu bar
  void start_tray();        // after show(): the system tray (XEmbed + StatusNotifierItem)
private:
  std::unique_ptr<Fl_Tiled_Image> pattern_;
  std::unique_ptr<Fl_RGB_Image> wallpaper_;   // the desktop with the wallpaper (pattern + image), when there is a wallpaper
  Fl_Menu_Item *logo_item_ = nullptr;
  void set_logo();
  Fl_RGB_Image *logo_img_ = nullptr;
  net::State net_;
  Fl_Widget *net_menu_ = nullptr;
};
