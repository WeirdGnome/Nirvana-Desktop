/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Rambler: the root window with the volumes (a backdrop or a window), the drawer
    windows opened from it, the menus (Rambler, Window, Icon, Tools) and their
    functions. Ported from Wanderer/wanderer.c and main.c.

    Until the window manager draws the screen's title bar, the menus open with the right
    mouse button over a Rambler window (as Intuition's menus open with it over the
    screen).
*/
#include "iconwindow.h"
#include "tools.h"
#include "prefs.h"
#include "fileops.h"
#include "guisettings.h"
#include "find.h"
#include "inputprefs.h"
#include "screenmodeprefs.h"
#include "localeprefs.h"
#include "pointerprefs.h"
#include "fontprefs.h"
#include "networkprefs.h"

#include <FL/Fl.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <X11/Xatom.h>

#include <algorithm>
#include <clocale>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

void make_prefs_drawer();   // (main.cxx part below)

namespace fs = std::filesystem;
using namespace lune;
using namespace rambler;

namespace {

// --------------------------------------------------------------------------------------------
// Snapshots: a drawer's window place and its icons' positions (on AROS kept in the .info
// files; here in ~/.config/rambler/drawers)
// --------------------------------------------------------------------------------------------
std::string read_file(const std::string &path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string config_dir() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/rambler";
}

std::string snapshot_file(const std::string &location) {
    std::string key = location.empty() ? std::string("ROOT") : location;
    for (auto &c : key) if (c == '/') c = '|';
    return config_dir() + "/drawers/" + key;
}

struct Snapshot {
    int x = -1, y = -1, w = 0, h = 0;
    int display = -1, sort = -1;   // the drawer's view (icons / list, all files) and order
    std::map<std::string, std::pair<int, int>> icons;
};

Snapshot load_snapshot(const std::string &location) {
    Snapshot s;
    std::ifstream in(snapshot_file(location));
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("window ", 0) == 0) sscanf(line.c_str() + 7, "%d %d %d %d", &s.x, &s.y, &s.w, &s.h);
        else if (line.rfind("view ", 0) == 0) sscanf(line.c_str() + 5, "%d %d", &s.display, &s.sort);
        else if (line.rfind("icon ", 0) == 0) {
            int ix, iy, n = 0;
            if (sscanf(line.c_str() + 5, "%d %d %n", &ix, &iy, &n) >= 2 && n) s.icons[line.substr(5 + n)] = {ix, iy};
        }
    }
    return s;
}

void save_snapshot(const std::string &location, const Snapshot &s) {
    std::error_code ec;
    fs::create_directories(config_dir() + "/drawers", ec);
    std::ofstream out(snapshot_file(location));
    if (s.w) out << "window " << s.x << ' ' << s.y << ' ' << s.w << ' ' << s.h << '\n';
    if (s.display >= 0) out << "view " << s.display << ' ' << s.sort << '\n';
    for (auto &[name, p] : s.icons) out << "icon " << p.first << ' ' << p.second << ' ' << name << '\n';
}

// --------------------------------------------------------------------------------------------
class Rambler {
public:
    void start(bool backdrop);
    IconWindow *open_drawer(const std::string &path);
    void navigate(IconWindow *w, const std::string &path);   // another drawer in the same window
    void open_network();   // the Network Access window
    void find(IconWindow *w);   // the Find tool, in the window's drawer
    void open_entry(IconWindow *w, IconEntry &e);
    void show_menu(IconWindow *w, int x_root, int y_root);
    std::vector<Fl_Menu_Item> menu_items(IconWindow *w);
    void publish_menu(IconWindow *w);
    std::vector<ToolItem> tools_;   // the Tools menu
    void publish_title(IconWindow *w);
    static void tick(void *d);
    static void watch_volumes(void *d);
    static void watch_prefs(void *d);
    std::string lune_env_, rambler_env_, locale_env_, pointer_env_;
    bool prefs_seen_ = false;   // the settings in use (ENV:), as last read
    void use_prefs(const RamblerPrefs &p);
    void mount_and_open(const std::string &device);
    void eject(const std::string &device);
    std::string volumes_state_;
    std::string appmenu_state_;     // the programs' menu items, as last read
    void pick(IconWindow *w, int id);
    bool shortcut(IconWindow *w);

private:
    IconWindow *root_ = nullptr;
    RamblerPrefs prefs_ = RamblerPrefs::load();
    void apply_prefs(IconWindow *w);
    std::vector<IconWindow *> windows_;
    IconWindow *active_ = nullptr;
    bool backdrop_ = false;

    void setup(IconWindow *w);
    void apply_snapshot(IconWindow *w, bool place = true);   // place: the window's size and view too
    void close(IconWindow *w);
    void update_all(const std::string &dir);
    IconWindow *window_at(int x_root, int y_root, IconWindow *except);
    void drop(IconWindow *from, std::vector<IconEntry *> &entries, IconEntry *target);
    void leave_out_at(IconWindow *from, std::vector<IconEntry *> &entries);
    void drop_paths(const std::vector<std::string> &sources, const std::string &dest);
    void menu_action(IconWindow *w, int id);
};

// the settings of an earlier version (named Wanderer then) copied once into Rambler's
// directory (as the session does; for Rambler started some other way)
void migrate_settings() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    std::error_code ec;
    if (fs::exists(base + "/rambler", ec) || !fs::is_directory(base + "/wanderer", ec)) return;
    fs::copy(base + "/wanderer", base + "/rambler", fs::copy_options::recursive, ec);
    fs::rename(base + "/rambler/zune.prefs", base + "/rambler/lune.prefs", ec);
    fs::rename(base + "/rambler/zune.windows", base + "/rambler/lune.windows", ec);
    fs::rename(base + "/rambler/wanderer.prefs", base + "/rambler/rambler.prefs", ec);
}

// (before g_rambler, whose settings are read when it is made)
const bool g_migrated = (migrate_settings(), true);

Rambler g_rambler;

// The menu items (wanderer.c: MEN_*)
enum MenuId {
    MEN_RAMBLER_BACKDROP = 1, MEN_RAMBLER_EXECUTE, MEN_RAMBLER_SHELL, MEN_RAMBLER_ABOUT, MEN_RAMBLER_QUIT,
    MEN_WINDOW_NEW_DRAWER, MEN_WINDOW_OPEN_PARENT, MEN_WINDOW_CLOSE, MEN_WINDOW_UPDATE, MEN_WINDOW_SELECT,
    MEN_WINDOW_CLEAR, MEN_WINDOW_SNAP_WIN, MEN_WINDOW_SNAP_ALL, MEN_WINDOW_VIEW_ICON, MEN_WINDOW_VIEW_DETAIL,
    MEN_WINDOW_VIEW_ALL, MEN_WINDOW_VIEW_HIDDEN, MEN_WINDOW_SORT_NOW, MEN_WINDOW_SORT_ENABLE, MEN_WINDOW_SORT_NAME, MEN_WINDOW_SORT_DATE,
    MEN_WINDOW_SORT_SIZE, MEN_WINDOW_SORT_TYPE, MEN_WINDOW_SORT_REVERSE, MEN_WINDOW_SORT_TOPDRAWERS,
    MEN_WINDOW_FIND, MEN_ICON_OPEN, MEN_ICON_RENAME, MEN_ICON_INFORMATION, MEN_ICON_SNAPSHOT,
    MEN_ICON_UNSNAPSHOT, MEN_ICON_DELETE, MEN_ICON_DISKINFO, MEN_RAMBLER_PREFS,
    MEN_ICON_LEAVEOUT, MEN_ICON_PUTAWAY, MEN_ICON_EMPTYTRASH, MEN_RAMBLER_GUISETTINGS, MEN_ICON_EJECT, MEN_ICON_COPY, MEN_TOOLS_DRAWER, MEN_TOOLS_PREFS, MEN_RAMBLER_SHUTDOWN, MEN_RAMBLER_ASSIGNS, MEN_ICON_FORMAT,
    MEN_TOOL_FIRST = 1000
};

// The preferences on a window: the labels, the layout, the bars, the filter, the order
void Rambler::apply_prefs(IconWindow *w) {
    IconList *l = w->iconlist();
    l->text_mode = (TextMode)prefs_.text_mode;
    l->label_multiline = prefs_.label_lines;
    l->label_multiline_on_focus = prefs_.lines_on_focus;
    l->icon_font_family = prefs_.icon_font;
    l->icon_font_name = prefs_.icon_font_name;
    l->icon_font_size = prefs_.icon_font_size;
    l->label_maxlen = prefs_.label_maxlen;
    l->hspacing = prefs_.hspacing;
    l->image_spacing = prefs_.image_spacing;
    l->label_hpad = prefs_.label_hpad;
    l->label_vpad = prefs_.label_vpad;
    l->label_border_w = prefs_.label_border_w;
    l->label_border_h = prefs_.label_border_h;
    l->vspacing = prefs_.vspacing;
    if (auto *vl = dynamic_cast<IconVolumeList *>(l)) vl->show_network = prefs_.show_network;
    l->tile_x_offset = w->is_root() ? prefs_.backdrop_ox : prefs_.drawer_ox;
    l->tile_y_offset = w->is_root() ? prefs_.backdrop_oy : prefs_.drawer_oy;
    if (w->is_root()) l->backfill(prefs_.backdrop_image, prefs_.backdrop_mode);
    else l->backfill(prefs_.drawer_image, prefs_.drawer_mode);
    if (!w->is_root()) {
        l->list_mode = (ListMode)prefs_.list_mode;
        if (prefs_.show_hidden) l->display_flags |= Disp_ShowHidden; else l->display_flags &= ~Disp_ShowHidden;
        if (prefs_.only_icons) l->display_flags |= Disp_ShowInfo; else l->display_flags &= ~Disp_ShowInfo;
        static const int orders[3] = {Sort_ByName, Sort_ByDate, Sort_BySize};
        l->sort_flags = (l->sort_flags & ~Sort_Orders) | orders[std::clamp(prefs_.default_sort, 0, 2)];
        if (prefs_.auto_sort) l->sort_flags |= Sort_AutoSort; else l->sort_flags &= ~Sort_AutoSort;
        if (prefs_.drawers_first) l->sort_flags &= ~(Sort_DrawersMixed | Sort_DrawersLast);
        else l->sort_flags |= Sort_DrawersMixed;
        w->show_toolbar(prefs_.toolbar);
        w->show_statusbar(prefs_.statusbar);
    }
}

// the version string (the Info tool's Version: shows it)
[[gnu::used]] static const char version_tag[] = "$VER: Rambler " RAMBLER_VERSION " (X11)";

// --------------------------------------------------------------------------------------------
void Rambler::start(bool backdrop) {
    backdrop_ = backdrop;
    open_drawer_hook = [this](const std::string &p) { open_drawer(p); };
    root_ = new IconWindow("", backdrop);
    setup(root_);
    root_->update();
    if (backdrop) {
        // the backdrop: the whole screen below the screen's title bar, behind everything
        int sx, sy, sw, sh;
        Fl::screen_xywh(sx, sy, sw, sh);
        root_->border(0);
        root_->resize(sx, sy, sw, sh);
    } else {
        root_->label("Rambler");
        apply_snapshot(root_);
    }
    root_->show();
    if (backdrop) {
        // the window manager puts a "desktop" window behind everything, below the screen's bar
        Atom type = XInternAtom(fl_display, "_NET_WM_WINDOW_TYPE", False);
        Atom desk = XInternAtom(fl_display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
        ::Window xw = fl_xid(root_);
        root_->hide();
        root_->border(0);
        root_->show();
        xw = fl_xid(root_);
        XChangeProperty(fl_display, xw, type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&desk, 1);
        (void)xw;
    }
}

void Rambler::setup(IconWindow *w) {
    apply_prefs(w);
    w->on_open = [this](IconWindow *win, IconEntry &e) { open_entry(win, e); };
    w->on_activate = [this](IconWindow *win) { active_ = win; };
    w->on_parent = [this](IconWindow *win) {
        if (win->is_root()) return;
        // the parent of a volume is the root window with the volumes
        const std::string &loc = win->location();
        fs::path p = fs::path(loc).parent_path();
        struct stat a, b;
        const char *home = getenv("HOME");
        bool volume = loc == "/" || (home && loc == home) || loc == ramdisk_dir() ||
                      (stat(loc.c_str(), &a) == 0 && stat(p.c_str(), &b) == 0 && a.st_dev != b.st_dev);
        if (volume) { if (root_) root_->show(); return; }
        if (p.string() != loc) open_drawer(p.string());
    };
    w->on_location = [this](IconWindow *win, const std::string &loc) {
        // the tool bar's location: this window shows the drawer (panel_toolbar.c)
        std::error_code ec;
        if (!fs::is_directory(loc, ec)) return;
        if (dynamic_cast<IconDrawerList *>(win->iconlist())) navigate(win, loc);
        else open_drawer(loc);
    };
    w->on_search = [this](IconWindow *win) { find(win); };   // the tool bar's search button
    w->on_dir_up = [this](IconWindow *win) {
        // the tool bar's "parent": up in this window, not above the volume (ICONWINDOW_ACTION_DIRUP)
        if (!dynamic_cast<IconDrawerList *>(win->iconlist())) { if (win->on_parent) win->on_parent(win); return; }
        const std::string &loc = win->location();
        fs::path p = fs::path(loc).parent_path();
        struct stat a, b;
        const char *home = getenv("HOME");
        bool volume = loc == "/" || (home && loc == home) || loc == ramdisk_dir() ||
                      (stat(loc.c_str(), &a) == 0 && stat(p.c_str(), &b) == 0 && a.st_dev != b.st_dev);
        if (!volume && p.string() != loc) navigate(win, p.string());
    };
    w->iconlist()->on_context_menu = [this, w](int x, int y) { show_menu(w, x, y); };
    w->iconlist()->on_selection_changed = [this, w] { w->update_status(); publish_menu(w); };
    w->on_shortcut = [this](IconWindow *win) { return shortcut(win); };
    w->on_shown = [this](IconWindow *win) { publish_menu(win); publish_title(win); };
    w->on_changed = [this](IconWindow *win) { apply_snapshot(win, false); publish_menu(win); };
    w->iconlist()->on_drop = [this, w](std::vector<IconEntry *> &entries, IconEntry *target) { drop(w, entries, target); };
    w->iconlist()->on_external_drop = [this, w](const std::vector<std::string> &paths, IconEntry *target) {
        std::string dest = target ? target->path : w->location();
        if (dest.empty()) return;   // the root window: no drawer of its own
        drop_paths(paths, dest);
    };
    w->callback([](Fl_Widget *win, void *d) { ((Rambler *)d)->close((IconWindow *)win); }, this);
}

void Rambler::apply_snapshot(IconWindow *w, bool place) {
    Snapshot s = load_snapshot(w->location());
    if (place && s.w > 0) w->resize(s.x, s.y, s.w, s.h);
    if (place && s.display >= 0 && !w->is_root()) {
        // the drawer's own view: icons or list, all files or not, its order
        IconList *l = w->iconlist();
        bool hidden_was = l->display_flags & Disp_ShowHidden;
        l->display_flags = s.display;
        l->sort_flags = s.sort;
        if (hidden_was != bool(l->display_flags & Disp_ShowHidden)) l->update();
        else { l->sort(); l->rethink(); }
        w->update_status();
    }
    if (!s.icons.empty()) {
        for (auto &e : w->iconlist()->entries()) {
            auto it = s.icons.find(fs::path(e.path).filename().string());
            if (it == s.icons.end() && w->is_root()) it = s.icons.find(e.path);
            if (it != s.icons.end()) { e.provided_x = it->second.first; e.provided_y = it->second.second; }
        }
        w->iconlist()->rethink();
    }
}

void Rambler::find(IconWindow *w) {
    // the Find tool (SYS:System/Find), started in this window's drawer
    std::string start = w && !w->location().empty() ? w->location() : (getenv("HOME") ? getenv("HOME") : "/");
    find_tool(start, [this](const std::string &p) {
        IconEntry e;
        e.path = p;
        e.label = fs::path(p).filename().string();
        std::error_code ec;
        e.type = fs::is_directory(p, ec) ? ST_USERDIR : ST_FILE;
        open_entry(nullptr, e);
    }, [this](const std::string &p) { open_drawer(p); });
}

IconWindow *Rambler::open_drawer(const std::string &path) {
    // a drawer has one window: opening it again brings that one to the front
    for (auto *w : windows_)
        if (w->location() == path) { w->show(); return w; }
    auto *w = new IconWindow(path);
    setup(w);
    w->update();
    int n = (int)windows_.size();
    w->position(60 + (n % 8) * 24, 60 + (n % 8) * 24);
    apply_snapshot(w);
    windows_.push_back(w);
    w->show();
    return w;
}

// Network Access: one window with the network file systems that are mounted
void Rambler::open_network() {
    for (auto *o : windows_)
        if (dynamic_cast<IconNetworkList *>(o->iconlist())) { o->show(); return; }
    auto *list = new IconNetworkList();
    auto *nw = new IconWindow(tr("Network Access.."), false, list);
    setup(nw);
    nw->show_toolbar(false);
    windows_.push_back(nw);
    if (root_) nw->position(root_->x() + 120, root_->y() + 80);
    nw->show();
    nw->update();
}

void Rambler::navigate(IconWindow *w, const std::string &path) {
    if (w->location() == path) return;
    apply_prefs(w);   // the defaults, then the drawer's own view
    IconList *l = w->iconlist();
    Snapshot s = load_snapshot(path);
    if (s.display >= 0) { l->display_flags = s.display; l->sort_flags = s.sort; }
    w->set_location(path);
    apply_snapshot(w, false);
    publish_menu(w);
}

void Rambler::close(IconWindow *w) {
    if (w == root_) {
        if (request("Rambler", file_operations_running() ? "Files are still being copied, moved or deleted.\nQuit Rambler anyway?"
                                                         : "Do you really want to quit Rambler?", "Quit|Cancel") == 1) exit(0);
        return;
    }
    windows_.erase(std::remove(windows_.begin(), windows_.end(), w), windows_.end());
    if (active_ == w) active_ = nullptr;
    w->hide();
    Fl::delete_widget(w);
}

void Rambler::update_all(const std::string &dir) {
    for (auto *w : windows_) if (w->location() == dir) w->update();
}

// A volume not mounted yet: mounted (udisks, as the user), then opened
void Rambler::mount_and_open(const std::string &device) {
    std::string out;
    FILE *p = popen(("udisksctl mount --no-user-interaction -b '" + device + "' 2>&1").c_str(), "r");
    if (!p) return;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
    int rc = pclose(p);
    if (rc != 0) {
        request("Rambler", trs("Could not mount %s:", device) + "\n" + out, "OK");
        return;
    }
    // "Mounted /dev/sdb1 at /media/user/LABEL"
    auto at = out.find(" at ");
    if (root_) root_->update();
    if (at != std::string::npos) {
        std::string mnt = out.substr(at + 4);
        while (!mnt.empty() && (mnt.back() == '\n' || mnt.back() == '.' || mnt.back() == ' ')) mnt.pop_back();
        open_drawer(mnt);
    }
}

// Icon > Eject: the volume unmounted, the drive powered off if it can be
void Rambler::eject(const std::string &device) {
    for (auto *w : std::vector<IconWindow *>(windows_)) {
        // the volume's windows close first (their files would keep it busy)
        struct stat a, b;
        if (stat(w->location().c_str(), &a) == 0) {
            FILE *p = popen(("findmnt -n -o SOURCE --target '" + w->location() + "' 2>/dev/null").c_str(), "r");
            char src[256] = {};
            if (p) { if (!fgets(src, sizeof src, p)) src[0] = 0; pclose(p); }
            std::string s(src);
            while (!s.empty() && s.back() == '\n') s.pop_back();
            if (s == device) close(w);
            (void)b;
        }
    }
    std::string cmd = "udisksctl unmount --no-user-interaction -b '" + device + "' 2>&1";
    std::string out;
    FILE *p = popen(cmd.c_str(), "r");
    char buf[512];
    while (p && fgets(buf, sizeof buf, p)) out += buf;
    int rc = p ? pclose(p) : 1;
    if (rc != 0) { request("Eject", trs("Could not unmount %s:", device) + "\n" + out, "OK"); return; }
    // the whole drive off (a memory stick can then be pulled out); not possible for some
    std::string disk = device;
    while (!disk.empty() && isdigit((unsigned char)disk.back())) disk.pop_back();
    if (disk.size() > 2 && disk.compare(disk.size() - 1, 1, "p") == 0 && disk.find("nvme") != std::string::npos) disk.pop_back();
    if (system(("udisksctl power-off --no-user-interaction -b '" + disk + "' >/dev/null 2>&1").c_str()) != 0) {}
    if (root_) root_->update();
}

void Rambler::open_entry(IconWindow *w, IconEntry &e) {
    if (e.unmounted) { mount_and_open(e.device); return; }
    if (e.network) { open_network(); return; }
    if (e.app_window) {
        // an AppIcon opened: its program gets a message without paths
        app_message({e.app_window, e.app_id, e.label}, "_RAMBLER_APPICON_MESSAGE", "_RAMBLER_APPICON_ARGS", {});
        return;
    }
    if (e.is_drawer()) {
        // a new window from the root window or with classic navigation, else the same window
        if (w && !w->is_root() && prefs_.navigation == 1 && dynamic_cast<IconDrawerList *>(w->iconlist())) navigate(w, e.path);
        else open_drawer(e.path);
        return;
    }
    struct stat st;
    std::string dir = fs::path(e.path).parent_path().string();
    // a tool runs; a project opens with its default tool, else the program for its type
    // the other icons selected (in every window: Shift held while the last one was
    // double-clicked) are its arguments, as the original passes them with WBOPENA_ArgName
    std::vector<std::string> args;
    auto collect = [&](IconWindow *o) {
        if (!o || !Fl::event_state(FL_SHIFT)) return;
        for (auto *x : o->iconlist()->selection())
            if (x != &e && !x->app_window && !x->network && !x->path.empty() && x->path[0] == '/') args.push_back(x->path);
    };
    collect(root_);
    for (auto *o : windows_) collect(o);
    // a .desktop file: the program it describes (its Exec), the other icons its arguments
    if (e.path.size() > 8 && e.path.compare(e.path.size() - 8, 8, ".desktop") == 0) {
        DesktopEntry d;
        if (read_desktop_entry(e.path, d) && !d.exec.empty()) {
            std::vector<std::string> argv = {"/bin/sh", "-c", d.exec + " \"$@\"", "sh"};
            if (d.terminal) argv = {"x-terminal-emulator", "-e", "/bin/sh", "-c", d.exec + " \"$@\"", "sh"};
            argv.insert(argv.end(), args.begin(), args.end());
            launch(argv, getenv("HOME") ? getenv("HOME") : dir);
            return;
        }
    }
    if (stat(e.path.c_str(), &st) == 0 && (st.st_mode & S_IXUSR) && S_ISREG(st.st_mode)) {
        std::vector<std::string> argv = {e.path};
        argv.insert(argv.end(), args.begin(), args.end());
        launch(argv, dir);
        return;
    }
    std::string tool = default_tool(e.path);
    if (!tool.empty()) {
        std::vector<std::string> argv = {"/bin/sh", "-c", tool + " \"$@\"", "sh", e.path};
        argv.insert(argv.end(), args.begin(), args.end());
        launch(argv, dir);
    } else if (has_opener(e.path)) launch({"xdg-open", e.path}, dir);
    else {
        // nothing opens this kind of file: Execute Command with its name, to be given the
        // program to open it with (execute_open_with_command)
        std::string name = fs::path(e.path).filename().string();
        bool plain = name.find_first_of(" '\"\\$`!*?&;|<>()") == std::string::npos;
        execute_command(dir, plain ? name : "'" + name + "'");
    }
}

IconWindow *Rambler::window_at(int xr, int yr, IconWindow *except) {
    // the front-most first (the newest windows are on top - no stacking order from X here)
    for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
        IconWindow *w = *it;
        if (w == except || !w->shown()) continue;
        if (xr >= w->x() && xr < w->x() + w->w() && yr >= w->y() && yr < w->y() + w->h()) return w;
    }
    if (root_ != except && xr >= root_->x() && xr < root_->x() + root_->w() && yr >= root_->y() &&
        yr < root_->y() + root_->h())
        return root_;
    return nullptr;
}

// Icons dropped: onto a drawer icon (into it), into another window (into its drawer), or onto
// the root window's background (left out there, where they were dropped, as Leave Out
// does). Within one volume they move, across volumes they are copied (Rambler's rule).
// Icons dragged out of a drawer onto the root window's background: left out (they stay in
// their drawer on the disk), each where it was dropped - for this session, until snapshot
void Rambler::leave_out_at(IconWindow *from, std::vector<IconEntry *> &entries) {
    IconList *fl = from->iconlist();
    std::vector<std::pair<std::string, std::pair<int, int>>> placed;   // path, screen position
    auto v = left_out_paths();
    for (auto *e : entries) {
        // not volumes, the trash or programs' icons; only files and drawers
        if (e->trash || e->type == ST_ROOT || e->app_window || e->path.empty() || e->path[0] != '/') continue;
        placed.push_back({e->path, {from->x() + fl->mleft() - fl->view_left() + e->x,
                                    from->y() + fl->mtop() - fl->view_top() + e->y}});
        if (std::find(v.begin(), v.end(), e->path) == v.end()) v.push_back(e->path);
    }
    fl->rethink();   // (back to their places in the drawer)
    if (placed.empty() || !root_) return;
    set_left_out(v);
    root_->update();
    apply_snapshot(root_);
    IconList *rl = root_->iconlist();
    for (auto &[path, pos] : placed)
        for (auto &e : rl->entries())
            if (e.left_out && e.path == path) {
                e.provided_x = std::max(0, pos.first - root_->x() - rl->mleft() + rl->view_left());
                e.provided_y = std::max(0, pos.second - root_->y() - rl->mtop() + rl->view_top());
            }
    rl->rethink();
    rl->redraw();
}

void Rambler::drop(IconWindow *from, std::vector<IconEntry *> &entries, IconEntry *target) {
    std::string dest;
    if (target && target->app_window) {
        // icons dropped onto an AppIcon: its program gets their paths (an AppMessage)
        std::vector<std::string> paths;
        for (auto *e : entries) if (!e->app_window && !e->path.empty() && e->path[0] == '/') paths.push_back(e->path);
        from->iconlist()->rethink();
        app_message({target->app_window, target->app_id, target->label}, "_RAMBLER_APPICON_MESSAGE",
                    "_RAMBLER_APPICON_ARGS", paths);
        return;
    }
    if (target) dest = target->path;
    else {
        IconWindow *to = window_at(Fl::event_x_root(), Fl::event_y_root(), from);
        if (!to || to->is_root()) {
            if (to && to->is_root()) {
                IconEntry *vol = to->iconlist()->entry_at(Fl::event_x_root() - to->x(), Fl::event_y_root() - to->y());
                if (vol && vol->app_window) { drop(from, entries, vol); return; }
                if (vol) dest = vol->path;
                else { leave_out_at(from, entries); return; }
            }
            if (dest.empty()) { from->iconlist()->rethink(); return; }
        } else {
            IconEntry *onto = to->iconlist()->entry_at(Fl::event_x_root() - to->x(), Fl::event_y_root() - to->y());
            dest = onto && onto->is_drawer() ? onto->path : to->location();
        }
    }
    std::vector<std::string> sources;
    for (auto *e : entries) if (!e->trash && e->type != ST_ROOT && !e->app_window) sources.push_back(e->path);
    from->iconlist()->rethink();
    drop_paths(sources, dest);
}

// Files into a drawer: to the trash; within one volume moved, onto another copied
void Rambler::drop_paths(const std::vector<std::string> &sources, const std::string &dest) {
    auto refresh = [this, dest] { update_all(dest); for (auto *w : windows_) w->update(); if (root_) root_->update(); };
    if (dest == trash_dir()) { file_operation(FileOpKind::Trash, sources, dest, refresh); return; }
    struct stat sd;
    if (stat(dest.c_str(), &sd) != 0) return;
    std::vector<std::string> move, copy;
    for (auto &src : sources) {
        struct stat ss;
        if (stat(src.c_str(), &ss) != 0) continue;
        // drop mode Move: moved within the volume; Copy: always copied (DROP_MODE_*)
        (ss.st_dev == sd.st_dev && prefs_.drop_mode == 0 ? move : copy).push_back(src);
    }
    if (!move.empty()) file_operation(FileOpKind::Move, move, dest, refresh);
    if (!copy.empty()) file_operation(FileOpKind::Copy, copy, dest, refresh);
}

// --------------------------------------------------------------------------------------------
// The menus
// --------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------
// AppMenuItems (appobjects.c, AddAppMenuItem): a program puts items into the Tools menu with
// the property _RAMBLER_APPMENU on its top-level window (UTF8_STRING, the item names
// separated by NUL). Picked, an item is a ClientMessage _RAMBLER_APPMENU_PICK to that window
// (l[0] the item's number, l[1] how many paths); the selected icons' paths are in the
// window's property _RAMBLER_APPMENU_ARGS (NUL-separated), as an AppMessage carries them.
// --------------------------------------------------------------------------------------------
static std::vector<ToolItem> app_menu_items() {
    std::vector<ToolItem> items;
    for (auto &a : app_items("_RAMBLER_APPMENU")) {
        ToolItem t;
        t.label = a.label;
        t.app_window = a.window;
        t.app_id = a.id;
        items.push_back(t);
    }
    return items;
}

// both kinds: a change shows in the menus and on the root window
static std::string app_state() {
    std::string s;
    for (auto &a : app_items("_RAMBLER_APPMENU")) s += "m" + std::to_string(a.window) + ":" + a.label + "\n";
    for (auto &a : app_items("_RAMBLER_APPICON")) s += "i" + std::to_string(a.window) + ":" + a.label + "\n";
    return s;
}

std::vector<Fl_Menu_Item> Rambler::menu_items(IconWindow *w) {
    IconList *l = w->iconlist();
    bool sel = !l->selection().empty();
    bool drawer = !w->is_root();
    bool left_out_sel = false, trash_sel = w->location() == trash_dir(), volume_sel = false, eject_sel = false, format_sel = false;
    bool app_sel = false, only_apps = true;
    unsigned long app_mask = ~0ul;
    for (auto *e : l->selection()) {
        if (e->left_out) left_out_sel = true;
        if (e->trash) trash_sel = true;
        if (e->type == ST_ROOT) volume_sel = true;
        if (e->type == ST_ROOT && !e->device.empty() && !e->unmounted) eject_sel = true;
        if (e->type == ST_ROOT && !e->device.empty()) format_sel = true;
        if (e->app_window) { app_sel = true; app_mask &= e->app_supports; }
        else only_apps = false;
    }
    // a program's icon selected: only what it takes (and Rambler passes on)
    app_mask &= 1ul << AMCLASSICON_Open | 1ul << AMCLASSICON_Snapshot | 1ul << AMCLASSICON_UnSnapshot |
                1ul << AMCLASSICON_Delete | 1ul << AMCLASSICON_EmptyTrash;
    auto app_ok = [&](int cls) { return !app_sel || (app_mask >> cls & 1); };
    int sf = l->sort_flags, df = l->display_flags;
    auto chk = [](bool on) { return FL_MENU_RADIO | (on ? FL_MENU_VALUE : 0); };
    auto tog = [](bool on) { return FL_MENU_TOGGLE | (on ? FL_MENU_VALUE : 0); };
    int dis = FL_MENU_INACTIVE;
    Fl_Menu_Item menu[] = {
        {"Rambler", 0, nullptr, nullptr, FL_SUBMENU},
            {"Backdrop", FL_META + 'b', nullptr, (void *)MEN_RAMBLER_BACKDROP, tog(backdrop_)},
            {"Execute Command...", FL_META + 'e', nullptr, (void *)MEN_RAMBLER_EXECUTE},
            {"Shell", FL_META + 'w', nullptr, (void *)MEN_RAMBLER_SHELL},
            {"Preferences...", 0, nullptr, (void *)MEN_RAMBLER_PREFS},
            {"GUI Settings...", 0, nullptr, (void *)MEN_RAMBLER_GUISETTINGS},
            {"Assigns...", 0, nullptr, (void *)MEN_RAMBLER_ASSIGNS},
            {"About...", 0, nullptr, (void *)MEN_RAMBLER_ABOUT},
            {"Quit...", FL_META + 'q', nullptr, (void *)MEN_RAMBLER_QUIT},
            {"Shutdown...", 0, nullptr, (void *)MEN_RAMBLER_SHUTDOWN},
            {nullptr},
        {"Window", 0, nullptr, nullptr, FL_SUBMENU},
            {"New Drawer...", FL_META + 'n', nullptr, (void *)MEN_WINDOW_NEW_DRAWER, drawer ? 0 : dis},
            {"Open Parent", 0, nullptr, (void *)MEN_WINDOW_OPEN_PARENT, drawer ? 0 : dis},
            {"Close", FL_META + 'k', nullptr, (void *)MEN_WINDOW_CLOSE, drawer ? 0 : dis},
            {"Update", 0, nullptr, (void *)MEN_WINDOW_UPDATE, FL_MENU_DIVIDER},
            {"Select Contents", FL_META + 'a', nullptr, (void *)MEN_WINDOW_SELECT},
            {"Clear Selection", FL_META + 'z', nullptr, (void *)MEN_WINDOW_CLEAR, FL_MENU_DIVIDER},
            {"Snapshot", 0, nullptr, nullptr, FL_SUBMENU | FL_MENU_DIVIDER},
                {"Window", 0, nullptr, (void *)MEN_WINDOW_SNAP_WIN},
                {"All", 0, nullptr, (void *)MEN_WINDOW_SNAP_ALL},
                {nullptr},
            {"View", 0, nullptr, nullptr, FL_SUBMENU},
                {"as Icons", 0, nullptr, (void *)MEN_WINDOW_VIEW_ICON, chk(!(df & Disp_ModeList))},
                {"as List", 0, nullptr, (void *)MEN_WINDOW_VIEW_DETAIL, chk(df & Disp_ModeList) | FL_MENU_DIVIDER},
                // All Files: not ticked, only what has an icon of its own (ICONLIST_DISP_SHOWINFO)
                {"All Files", 0, nullptr, (void *)MEN_WINDOW_VIEW_ALL, tog(!(df & Disp_ShowInfo))},
                {"Hidden Files", 0, nullptr, (void *)MEN_WINDOW_VIEW_HIDDEN, tog(df & Disp_ShowHidden)},
                {nullptr},
            {"Sort Icons", 0, nullptr, nullptr, FL_SUBMENU | FL_MENU_DIVIDER},
                {"Clean Up", FL_META + '.', nullptr, (void *)MEN_WINDOW_SORT_NOW},
                {"Enable Sorting", 0, nullptr, (void *)MEN_WINDOW_SORT_ENABLE, tog(sf & Sort_AutoSort) | FL_MENU_DIVIDER},
                {"by Name", 0, nullptr, (void *)MEN_WINDOW_SORT_NAME, chk(!(sf & (Sort_ByDate | Sort_BySize | Sort_ByType)))},
                {"by Date", 0, nullptr, (void *)MEN_WINDOW_SORT_DATE, chk(sf & Sort_ByDate)},
                {"by Size", 0, nullptr, (void *)MEN_WINDOW_SORT_SIZE, chk(sf & Sort_BySize)},
                {"by Type", 0, nullptr, (void *)MEN_WINDOW_SORT_TYPE, chk(sf & Sort_ByType) | FL_MENU_DIVIDER},
                {"Reverse", 0, nullptr, (void *)MEN_WINDOW_SORT_REVERSE, tog(sf & Sort_Reverse)},
                {"Drawers First", 0, nullptr, (void *)MEN_WINDOW_SORT_TOPDRAWERS, tog(!(sf & (Sort_DrawersMixed | Sort_DrawersLast)))},
                {nullptr},
            {"Find...", FL_META + 'f', nullptr, (void *)MEN_WINDOW_FIND, drawer ? 0 : dis},
            {nullptr},
        {"Icon", 0, nullptr, nullptr, FL_SUBMENU},
            {"Open", FL_META + 'o', nullptr, (void *)MEN_ICON_OPEN, sel && app_ok(AMCLASSICON_Open) ? 0 : dis},
            {"Copy", FL_META + 'c', nullptr, (void *)MEN_ICON_COPY, sel && drawer && !app_sel ? 0 : dis},
            {"Rename...", FL_META + 'r', nullptr, (void *)MEN_ICON_RENAME, sel && drawer && !app_sel ? 0 : dis},
            {"Information...", FL_META + 'i', nullptr, (void *)MEN_ICON_INFORMATION, sel && !app_sel ? 0 : dis},
            {"Snapshot", FL_META + 's', nullptr, (void *)MEN_ICON_SNAPSHOT, sel && app_ok(AMCLASSICON_Snapshot) ? 0 : dis},
            {"Unsnapshot", FL_META + 'u', nullptr, (void *)MEN_ICON_UNSNAPSHOT, sel && app_ok(AMCLASSICON_UnSnapshot) ? 0 : dis},
            {"Leave Out", FL_META + 'l', nullptr, (void *)MEN_ICON_LEAVEOUT, sel && drawer && !app_sel ? 0 : dis},
            {"Put Away", FL_META + 'p', nullptr, (void *)MEN_ICON_PUTAWAY, (left_out_sel ? 0 : dis) | FL_MENU_DIVIDER},
            {"Delete...", 0, nullptr, (void *)MEN_ICON_DELETE, sel && (drawer || only_apps) && app_ok(AMCLASSICON_Delete) ? 0 : dis},
            {"Disk Info...", 0, nullptr, (void *)MEN_ICON_DISKINFO, volume_sel ? 0 : dis},
            {"Eject", FL_META + 'j', nullptr, (void *)MEN_ICON_EJECT, eject_sel ? 0 : dis},
            {"Format...", 0, nullptr, (void *)MEN_ICON_FORMAT, format_sel ? 0 : dis},
            {"Empty Trash", 0, nullptr, (void *)MEN_ICON_EMPTYTRASH, (trash_sel || app_sel) && app_ok(AMCLASSICON_EmptyTrash) ? 0 : dis},
            {nullptr},
        {"Tools", 0, nullptr, nullptr, FL_SUBMENU},
            {"Tools Drawer", 0, nullptr, (void *)MEN_TOOLS_DRAWER},
            {"Prefs Drawer", 0, nullptr, (void *)MEN_TOOLS_PREFS},
            {nullptr},
        {nullptr}};
    // the menus look like the screen's: the window's pens, the screen font
    for (auto &m : menu) {
        if (!m.text) continue;
        m.labelfont(prefs().font);
        m.labelsize(prefs().font_size);
    }
    std::vector<Fl_Menu_Item> v(menu, menu + sizeof menu / sizeof menu[0]);
    // the Tools menu: the user's tools (~/.config/rambler/tools), then its drawer
    tools_ = tool_items();
    for (auto &t : app_menu_items()) tools_.push_back(t);
    size_t at = 0;
    for (size_t i = 0; i < v.size(); i++)
        if (v[i].text && !strcmp(v[i].text, "Tools Drawer")) { at = i; break; }
    if (at) {
        if (!tools_.empty()) v[at].flags |= 0;
        std::vector<Fl_Menu_Item> items;
        for (size_t i = 0; i < tools_.size(); i++) {
            Fl_Menu_Item m = {};
            m.text = tools_[i].label.c_str();
            m.user_data_ = (void *)(intptr_t)(MEN_TOOL_FIRST + (int)i);
            m.labelfont_ = prefs().font;
            m.labelsize_ = prefs().font_size;
            if (i + 1 == tools_.size()) m.flags = FL_MENU_DIVIDER;
            items.push_back(m);
        }
        v.insert(v.begin() + at, items.begin(), items.end());
    }
    for (auto &m : v)
        if (m.text && (intptr_t)m.user_data_ < MEN_TOOL_FIRST) m.text = tr(m.text).c_str();   // tools keep their names
    return v;
}

// Without the window manager's screen bar: the menus as a pop-up under the pointer
void Rambler::show_menu(IconWindow *w, int xr, int yr) {
    active_ = w;
    auto items = menu_items(w);
    const Fl_Menu_Item *picked = items.data()->popup(xr - w->x(), yr - w->y(), nullptr, nullptr, nullptr);
    if (picked && picked->user_data()) menu_action(w, (int)(intptr_t)picked->user_data());
    publish_menu(w);
}

// The screen's title while a Rambler window is active: the version and the free memory
// (as Rambler shows on its screen); _RAMBLER_SCREEN_TITLE, read by the window manager
static std::string mem_text(long kb) {
    char b[64];
    if (kb < 0) return "?";
    if (kb >= 10 * 1024 * 1024) snprintf(b, sizeof b, "%.1f GB", kb / 1048576.0);
    else snprintf(b, sizeof b, "%.1f MB", kb / 1024.0);
    return b;
}

static std::string os_release() {
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            std::string v = line.substr(12);
            if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'')) v = v.substr(1, v.size() - 2);
            return v;
        }
    return "Linux";
}

// ProcessUserScreenTitle (wandererprefs.c): the tokens of the user's screen title
static std::string screen_title(const std::string &tmpl) {
    std::ifstream m("/proc/meminfo");
    std::string k, unit;
    long v = 0, avail = -1, mfree = -1, total = -1;
    while (m >> k >> v >> unit) {
        if (k == "MemAvailable:") avail = v;
        else if (k == "MemFree:") mfree = v;
        else if (k == "MemTotal:") total = v;
    }
    if (tmpl.empty()) {
        std::string t = "Rambler " RAMBLER_VERSION;
        if (avail >= 0) t += "    " + mem_text(avail) + " " + tr("free");
        return t;
    }
    struct utsname u {};
    uname(&u);
    std::string out;
    for (size_t i = 0; i < tmpl.size(); i++) {
        if (tmpl[i] == '%' && i + 2 < tmpl.size()) {
            std::string tok = tmpl.substr(i + 1, 2);
            std::string r;
            bool hit = true;
            if (tok == "wv" || tok == "wb") r = RAMBLER_VERSION;
            else if (tok == "ov") r = u.release;
            else if (tok == "os") r = os_release();
            else if (tok == "pc") r = mem_text(avail);
            else if (tok == "pf") r = mem_text(mfree);
            else if (tok == "pt") r = mem_text(total);
            else hit = false;
            if (hit) { out += r; i += 2; continue; }
        }
        out += tmpl[i];
    }
    return out;
}

void Rambler::publish_title(IconWindow *w) {
    if (!w || !w->shown()) return;
    std::string t = screen_title(prefs_.screen_title);
    Atom prop = XInternAtom(fl_display, "_RAMBLER_SCREEN_TITLE", False);
    Atom utf8 = XInternAtom(fl_display, "UTF8_STRING", False);
    XChangeProperty(fl_display, fl_xid(w), prop, utf8, 8, PropModeReplace, (const unsigned char *)t.data(), (int)t.size());
}

// Disks put in or taken out, mounted or unmounted: the root window follows (every 2 seconds
// the mounts and the removable devices are compared with what it shows)
void Rambler::watch_volumes(void *d) {
    auto *self = (Rambler *)d;
    std::string state;
    {
        std::ifstream m("/proc/self/mounts");
        std::string line;
        while (std::getline(m, line)) {
            std::istringstream ls(line);
            std::string dev, mnt, type;
            ls >> dev >> mnt >> type;
            if (line.rfind("/dev/", 0) == 0 || is_network_fs(type)) state += line + "\n";
        }
    }
    for (auto &v : removable_volumes()) state += v.device + "|" + v.mountpoint + "\n";
    if (state != self->volumes_state_) {
        bool first = self->volumes_state_.empty();
        self->volumes_state_ = state;
        if (!first && self->root_) { self->root_->update(); self->apply_snapshot(self->root_); }
        if (!first)
            for (auto *o : self->windows_)
                if (dynamic_cast<IconNetworkList *>(o->iconlist())) o->update();
    }
    // the programs' menu items: the menus again when they change
    std::string apps = app_state();
    if (apps != self->appmenu_state_) {
        self->appmenu_state_ = apps;
        if (self->root_) { self->root_->update(); self->apply_snapshot(self->root_); }
        for (auto *o : self->windows_) self->publish_menu(o);
        if (self->root_) self->publish_menu(self->root_);
    }
    Fl::repeat_timeout(2.0, watch_volumes, d);
}

void Rambler::use_prefs(const RamblerPrefs &p) {
    prefs_ = p;
    rambler_env_ = read_file(RamblerPrefs::in_use_file());   // (not taken up again by watch_prefs)
    for (auto *o : windows_) { apply_prefs(o); o->update(); apply_snapshot(o); }
    if (root_) { apply_prefs(root_); root_->update(); apply_snapshot(root_); }
    if (root_) publish_title(root_);
}

// As IPrefs, which is notified when a preferences program writes ENV:Sys: the settings in
// use are looked at every second, and a change (by a preferences program running on its
// own, Use or Save) is put into effect at once
void Rambler::watch_prefs(void *d) {
    auto *self = (Rambler *)d;
    bool first = !self->prefs_seen_;
    self->prefs_seen_ = true;
    std::string l = read_file(lune::env_prefs_path());
    if (l != self->lune_env_) {
        self->lune_env_ = l;
        if (!first && !l.empty()) { lune::load_prefs(); lune::apply_prefs(); }
    }
    // the Locale preferences: the programs started from now on get them
    std::string lo = read_file(LocalePrefs::in_use_file());
    if (lo != self->locale_env_) {
        self->locale_env_ = lo;
        if (!first && !lo.empty()) LocalePrefs::load().apply();
    }
    // the Pointer preferences changed by the program on its own: the environment too
    std::string po = read_file(PointerPrefs::in_use_file());
    if (po != self->pointer_env_) {
        self->pointer_env_ = po;
        if (!first && !po.empty()) PointerPrefs::load().apply();
    }
    std::string r = read_file(RamblerPrefs::in_use_file());
    if (r != self->rambler_env_) {
        self->rambler_env_ = r;
        if (!first && !r.empty()) {
            RamblerPrefs p = RamblerPrefs::load();
            if (p.text() != self->prefs_.text()) self->use_prefs(p);
        }
    }
    Fl::repeat_timeout(1.0, watch_prefs, d);
}

void Rambler::tick(void *d) {
    auto *self = (Rambler *)d;
    for (auto *w : self->windows_) self->publish_title(w);
    self->publish_title(self->root_);
    Fl::repeat_timeout(5.0, tick, d);
}

// The menus for the window manager: it shows them in the screen's title bar while the right
// mouse button is held over the window (as Intuition does) and sends back the item picked.
// _RAMBLER_MENU on the window, one line per item: depth TAB id TAB flags TAB label TAB key;
// flags: 1 disabled, 2 check mark item, 4 checked, 8 a line after it
void Rambler::publish_menu(IconWindow *w) {
    if (!w->shown()) return;
    auto items = menu_items(w);
    std::string out;
    int depth = 0;
    for (auto &m : items) {
        if (!m.text) { if (depth > 0) depth--; continue; }
        int f = 0;
        if (m.flags & FL_MENU_INACTIVE) f |= 1;
        if (m.flags & (FL_MENU_TOGGLE | FL_MENU_RADIO)) f |= 2;
        if (m.flags & FL_MENU_VALUE) f |= 4;
        if (m.flags & FL_MENU_DIVIDER) f |= 8;
        std::string key;
        if (m.shortcut_ & FL_META) key = std::string("Super+") + (char)toupper(m.shortcut_ & 0xff);
        out += std::to_string(depth) + "\t" + std::to_string((intptr_t)m.user_data_) + "\t" + std::to_string(f) + "\t" +
               m.text + "\t" + key + "\n";
        if (m.flags & FL_SUBMENU) depth++;
    }
    Atom prop = XInternAtom(fl_display, "_RAMBLER_MENU", False);
    Atom utf8 = XInternAtom(fl_display, "UTF8_STRING", False);
    XChangeProperty(fl_display, fl_xid(w), prop, utf8, 8, PropModeReplace, (const unsigned char *)out.data(), (int)out.size());
}

// The item picked in the screen bar (a client message from the window manager)
static int x_events(void *event, void *) {
    XEvent *e = (XEvent *)event;
    static Atom pick = XInternAtom(fl_display, "_RAMBLER_MENU_PICK", False);
    if (e->type != ClientMessage || e->xclient.message_type != pick) return 0;
    Fl_Window *fw = fl_find(e->xclient.window);
    if (auto *w = dynamic_cast<IconWindow *>(fw)) g_rambler.pick(w, (int)e->xclient.data.l[0]);
    return 1;
}

void Rambler::pick(IconWindow *w, int id) {
    active_ = w;
    menu_action(w, id);
    for (auto *o : windows_) publish_menu(o);
    if (root_) publish_menu(root_);
}

// The keyboard shortcuts of the menus (Super+key)
bool Rambler::shortcut(IconWindow *w) {
    auto items = menu_items(w);
    const Fl_Menu_Item *m = items.data()->test_shortcut();
    if (!m || !m->user_data() || !m->active()) return false;
    pick(w, (int)(intptr_t)m->user_data());
    return true;
}

void Rambler::menu_action(IconWindow *w, int id) {
    if (id >= MEN_TOOL_FIRST) {
        // a tool: started in the window's drawer, with the selected icons' paths as arguments
        size_t i = id - MEN_TOOL_FIRST;
        if (i >= tools_.size()) return;
        if (tools_[i].app_window) {
            std::vector<std::string> paths;
            for (auto *e : w->iconlist()->selection()) paths.push_back(e->path);
            app_message({tools_[i].app_window, tools_[i].app_id, tools_[i].label}, "_RAMBLER_APPMENU_PICK",
                        "_RAMBLER_APPMENU_ARGS", paths);
            return;
        }
        std::vector<std::string> argv = {"/bin/sh", "-c", tools_[i].command + " \"$@\"", "sh"};
        for (auto *e : w->iconlist()->selection()) argv.push_back(e->path);
        launch(argv, w->is_root() ? (getenv("HOME") ? getenv("HOME") : "/") : w->location());
        return;
    }
    IconList *l = w->iconlist();
    auto resort = [l, w] { l->sort(); for (auto &e : l->entries()) e.provided_x = e.provided_y = NO_ICON_POSITION; l->rethink(); w->update_status(); };

    // a program's icons (AppIcons) among the selected: the program is told what was picked
    // (SendAppIconMenuMessage: snapshot, unsnapshot, delete; empty trash for the first one)
    if (id == MEN_ICON_SNAPSHOT || id == MEN_ICON_UNSNAPSHOT || id == MEN_ICON_DELETE || id == MEN_ICON_EMPTYTRASH) {
        long cls = id == MEN_ICON_SNAPSHOT ? AMCLASSICON_Snapshot : id == MEN_ICON_UNSNAPSHOT ? AMCLASSICON_UnSnapshot
                 : id == MEN_ICON_DELETE   ? AMCLASSICON_Delete   : AMCLASSICON_EmptyTrash;
        auto sel = l->selection();
        bool files = false;
        for (auto *e : sel) {
            if (!e->app_window) { files = true; continue; }
            app_message({e->app_window, e->app_id, e->label}, "_RAMBLER_APPICON_MESSAGE", "_RAMBLER_APPICON_ARGS", {}, cls);
            if (id == MEN_ICON_EMPTYTRASH) return;
        }
        if (!files && !sel.empty()) return;
    }

    switch (id) {
    case MEN_RAMBLER_BACKDROP: {
        // the root window between a backdrop and a window: made again
        bool b = !backdrop_;
        root_->hide();
        Fl::delete_widget(root_);
        start(b);
        break;
    }
    case MEN_RAMBLER_EXECUTE: execute_command(w->is_root() ? getenv("HOME") : w->location()); break;
    case MEN_RAMBLER_SHELL: launch({"x-terminal-emulator"}, w->is_root() ? getenv("HOME") : w->location()); break;
    case MEN_RAMBLER_PREFS:
        edit_prefs(prefs_, [this](const RamblerPrefs &p) { use_prefs(p); });
        break;
    case MEN_RAMBLER_GUISETTINGS: edit_gui_settings(); break;
    case MEN_RAMBLER_ASSIGNS: edit_assigns(); break;
    case MEN_RAMBLER_SHUTDOWN: {
        int r = request("Shutdown", "Turn the computer off, or restart it?\nPrograms still running are ended.",
                        "Turn Off|Restart|Cancel");
        if (r == 0) break;
        if (file_operations_running() &&
            request("Shutdown", "Files are still being copied, moved or deleted.\nTurn off anyway?", "Yes|No") != 1)
            break;
        // elogind / logind (as the user, through the login manager), else the init system's tools
        std::string what = r == 1 ? "poweroff" : "reboot";
        std::string cmd = "loginctl " + what + " 2>/dev/null || systemctl " + what + " 2>/dev/null || " +
                          (r == 1 ? "shutdown -h now" : "shutdown -r now") + " 2>&1";
        FILE *p = popen(cmd.c_str(), "r");
        std::string out;
        char buf[256];
        while (p && fgets(buf, sizeof buf, p)) out += buf;
        if (!p || pclose(p) != 0)
            request("Shutdown", tr("The computer could not be turned off from here:") + "\n" + out +
                                    "\n" + tr("(the user may need the right to do it - elogind or polkit)"), "OK");
        break;
    }
    case MEN_RAMBLER_ABOUT: about_window(); break;
    case MEN_RAMBLER_QUIT:
        if (request("Rambler", file_operations_running() ? "Files are still being copied, moved or deleted.\nQuit Rambler anyway?"
                                                         : "Do you really want to quit Rambler?", "Quit|Cancel") == 1) exit(0);
        break;
    case MEN_WINDOW_NEW_DRAWER: {
        std::string made;
        if (new_drawer(w->location(), made)) w->update();
        break;
    }
    case MEN_WINDOW_OPEN_PARENT: if (w->on_parent) w->on_parent(w); break;
    case MEN_WINDOW_CLOSE: close(w); break;
    case MEN_WINDOW_UPDATE: w->update(); apply_snapshot(w); break;
    case MEN_WINDOW_SELECT: l->select_all(); break;
    case MEN_WINDOW_CLEAR: l->unselect_all(); break;
    case MEN_WINDOW_SNAP_WIN:
    case MEN_WINDOW_SNAP_ALL: {
        Snapshot s = load_snapshot(w->location());
        s.x = w->x(); s.y = w->y(); s.w = w->w(); s.h = w->h();
        s.display = l->display_flags;
        s.sort = l->sort_flags;
        if (id == MEN_WINDOW_SNAP_ALL)
            for (auto &e : l->entries())
                s.icons[w->is_root() ? e.path : fs::path(e.path).filename().string()] = {e.x, e.y};
        save_snapshot(w->location(), s);
        break;
    }
    case MEN_WINDOW_VIEW_ICON: l->display_flags = (l->display_flags & ~Disp_ModeMask) | Disp_ModeDefault; l->rethink(); break;
    case MEN_WINDOW_VIEW_DETAIL: l->display_flags = (l->display_flags & ~Disp_ModeMask) | Disp_ModeList; l->rethink(); break;
    case MEN_WINDOW_VIEW_ALL: l->display_flags ^= Disp_ShowInfo; w->update(); break;
    case MEN_WINDOW_VIEW_HIDDEN: l->display_flags ^= Disp_ShowHidden; w->update(); break;
    case MEN_WINDOW_SORT_NOW: resort(); break;
    case MEN_WINDOW_SORT_ENABLE: l->sort_flags ^= Sort_AutoSort; resort(); break;
    case MEN_WINDOW_SORT_NAME: l->sort_flags = (l->sort_flags & ~Sort_Orders) | Sort_ByName; resort(); break;
    case MEN_WINDOW_SORT_DATE: l->sort_flags = (l->sort_flags & ~Sort_Orders) | Sort_ByDate; resort(); break;
    case MEN_WINDOW_SORT_SIZE: l->sort_flags = (l->sort_flags & ~Sort_Orders) | Sort_BySize; resort(); break;
    case MEN_WINDOW_SORT_TYPE: l->sort_flags = (l->sort_flags & ~Sort_Orders) | Sort_ByType; resort(); break;
    case MEN_WINDOW_SORT_REVERSE: l->sort_flags ^= Sort_Reverse; resort(); break;
    case MEN_WINDOW_SORT_TOPDRAWERS:
        if (l->sort_flags & (Sort_DrawersMixed | Sort_DrawersLast)) l->sort_flags &= ~(Sort_DrawersMixed | Sort_DrawersLast);
        else l->sort_flags |= Sort_DrawersMixed;
        resort();
        break;
    case MEN_ICON_OPEN: for (auto *e : l->selection()) open_entry(w, *e); break;
    case MEN_ICON_RENAME:
        for (auto *e : l->selection()) {
            std::string renamed;
            if (!rename_entry(e->path, renamed)) break;
        }
        w->update();
        break;
    case MEN_ICON_INFORMATION: for (auto *e : l->selection()) information(e->path); break;
    case MEN_ICON_SNAPSHOT:
    case MEN_ICON_UNSNAPSHOT: {
        Snapshot s = load_snapshot(w->location());
        for (auto *e : l->selection()) {
            if (e->app_window) continue;
            std::string key = w->is_root() ? e->path : fs::path(e->path).filename().string();
            if (id == MEN_ICON_SNAPSHOT) s.icons[key] = {e->x, e->y};
            else { s.icons.erase(key); e->provided_x = e->provided_y = NO_ICON_POSITION; }
            std::error_code ec;
            std::string info = e->path + ".info";
            if (id == MEN_ICON_SNAPSHOT && e->icon && !e->is_drawer() && !fs::exists(info, ec)) {
                // an AppImage's icon (no .info yet) is written out as its own dual icon, as
                // Snapshot gives a file shown with a default icon an icon of its own; its
                // second image (selected) can then be painted over
                DiskObject o = *e->icon;
                o.dual = true;   // (its selected image written as a second one, to be painted over)
                o.current_x = e->x;
                o.current_y = e->y;
                if (put_png_icon(info, o)) e->icon = get_disk_object(e->path);
            } else if (e->type != ST_ROOT && fs::is_regular_file(info, ec)) {
                // an icon file of its own keeps the place too (do_CurrentX/Y), as on the
                // original system: it goes with the file; Unsnapshot takes it away
                if (id == MEN_ICON_SNAPSHOT) put_icon_position(info, e->x, e->y);
                else put_icon_position(info, NO_ICON_POSITION, NO_ICON_POSITION);
                e->icon = get_disk_object(e->path);
            }
        }
        save_snapshot(w->location(), s);
        if (id == MEN_ICON_UNSNAPSHOT) l->rethink();
        break;
    }
    case MEN_ICON_DELETE: {
        auto sel = l->selection();
        sel.erase(std::remove_if(sel.begin(), sel.end(), [](IconEntry *e) { return e->app_window != 0; }), sel.end());
        std::string what = sel.size() == 1 ? "'" + sel[0]->label + "'" : trs("%s icons", std::to_string(sel.size()));
        int r = request("Delete", trs("Delete %s?\nMove to Trash keeps them until the trash is emptied;\nDelete removes them for good.", what),
                        "Move to Trash|Delete|Cancel");
        if (r == 1 || r == 2) {
            std::vector<std::string> paths;
            for (auto *e : sel) paths.push_back(e->path);
            bool trash = r == 1 && w->location() != trash_dir();
            file_operation(trash ? FileOpKind::Trash : FileOpKind::Delete, paths, "", [this, w] {
                for (auto *o : windows_) o->update();
                if (root_) root_->update();
            });
        }
        break;
    }
    case MEN_TOOLS_PREFS: {
        const char *x = getenv("XDG_CONFIG_HOME");
        std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
        make_prefs_drawer();
        open_drawer(base + "/rambler/prefs");
        break;
    }
    case MEN_TOOLS_DRAWER: {
        const char *x = getenv("XDG_CONFIG_HOME");
        std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
        open_drawer(base + "/rambler/tools");
        break;
    }
    case MEN_ICON_COPY: {
        std::vector<std::string> paths;
        for (auto *e : l->selection()) paths.push_back(e->path);
        file_operation(FileOpKind::Duplicate, paths, "", [this, w] { w->update(); apply_snapshot(w); });
        break;
    }
    case MEN_ICON_FORMAT:
        for (auto *e : l->selection())
            if (e->type == ST_ROOT && !e->device.empty()) {
                std::string label = e->label;
                if (!label.empty() && label.back() == ':') label.pop_back();
                // the volume's windows close first (they would keep it busy)
                for (auto *o : std::vector<IconWindow *>(windows_))
                    if (!e->unmounted && (o->location() == e->path || o->location().rfind(e->path + "/", 0) == 0)) close(o);
                format_disk(e->device, label);
                break;
            }
        break;
    case MEN_ICON_EJECT:
        for (auto *e : l->selection())
            if (e->type == ST_ROOT && !e->device.empty() && !e->unmounted) { eject(e->device); break; }
        break;
    case MEN_ICON_DISKINFO: for (auto *e : l->selection()) if (e->type == ST_ROOT && !e->unmounted) disk_info(e->path); break;
    case MEN_ICON_LEAVEOUT: {
        // the icons go onto the root window (they stay where they are on the disk)
        auto v = left_out_paths();
        for (auto *e : l->selection())
            if (std::find(v.begin(), v.end(), e->path) == v.end()) v.push_back(e->path);
        set_left_out(v);
        if (root_) root_->update();
        break;
    }
    case MEN_ICON_PUTAWAY: {
        auto v = left_out_paths();
        for (auto *e : l->selection())
            if (e->left_out) v.erase(std::remove(v.begin(), v.end(), e->path), v.end());
        set_left_out(v);
        if (root_) root_->update();
        break;
    }
    case MEN_ICON_EMPTYTRASH:
        if (request("Empty Trash", "Do you really want to delete\neverything in the trash?\nThis cannot be undone.",
                    "Empty|Cancel") == 1) {
            empty_trash();
            update_all(trash_dir());
        }
        break;
    case MEN_WINDOW_FIND: find(w); break;
    }
}

}  // namespace

// The first Rambler of this X session? (the window manager's check window names the session;
// a marker in the runtime directory remembers it)
static bool first_in_session() {
    Atom check = XInternAtom(fl_display, "_NET_SUPPORTING_WM_CHECK", False), type;
    int fmt;
    unsigned long n = 0, after;
    unsigned char *data = nullptr;
    unsigned long id = 0;
    if (XGetWindowProperty(fl_display, RootWindow(fl_display, fl_screen), check, 0, 1, False, XA_WINDOW, &type, &fmt, &n,
                           &after, &data) == Success && data && n == 1)
        id = ((::Window *)data)[0];
    if (data) XFree(data);
    const char *rt = getenv("XDG_RUNTIME_DIR");
    const char *disp = getenv("DISPLAY");
    std::string marker = std::string(rt && *rt ? rt : "/tmp") + "/rambler-startup-" + std::to_string(getuid()) + "-" +
                         (disp ? disp : "") ;
    std::string want = std::to_string(id);
    std::ifstream in(marker);
    std::string have;
    std::getline(in, have);
    if (id && have == want) return false;
    std::ofstream(marker) << want << "\n";
    return true;
}

// the Prefs drawer (SYS:Prefs): ~/.config/rambler/prefs with a starter for each preferences
// program (made when missing; the user may add others)
void make_prefs_drawer() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    std::string dir = base + "/rambler/prefs";
    std::error_code ec;
    fs::create_directories(dir, ec);
    struct { const char *file, *name, *arg; } progs[] = {
        {"Font.desktop", "Font", "font"}, {"Input.desktop", "Input", "input"}, {"Locale.desktop", "Locale", "locale"}, {"Network.desktop", "Network", "network"},
        {"Pointer.desktop", "Pointer", "pointer"}, {"Lune.desktop", "Lune", "lune"}, {"Rambler.desktop", "Rambler", "rambler"},
        {"ScreenMode.desktop", "ScreenMode", "screenmode"}};
    for (auto &pr : progs) {
        std::string f = dir + "/" + pr.file;
        // made when missing, and made again when it is one of ours from an earlier version
        // (without its icon); the user's own files stay as they are
        std::string had = read_file(f), want_exec = std::string("Exec=rambler --prefs ") + pr.arg;
        if (!had.empty() && (had.find(want_exec) == std::string::npos || had.find("Icon=rambler-prefs-") != std::string::npos))
            continue;
        std::ofstream out(f);
        out << "[Desktop Entry]\nType=Application\nName=" << pr.name << "\n" << want_exec
            << "\nIcon=rambler-prefs-" << pr.arg << "\nCategories=Settings;\n";
    }
}

int main(int argc, char **argv) {
    // dates and times in the user's language (the locale's FORMAT_DEF); numbers stay as they are
    setlocale(LC_TIME, "");
    // Lune's image adjust window asks for a picture with Rambler's file requester
    lune::file_request_hook = [](const std::string &title, std::string &path) { return file_request(title, path); };
    bool backdrop = false;
    std::string prefs_program;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--backdrop")) backdrop = true;
        else if (!strcmp(argv[i], "--prefs") && i + 1 < argc) prefs_program = argv[++i];
        else if (!strcmp(argv[i], "--appimage-icon") && i + 1 < argc) {
            // (what becomes of an AppImage's icon, step by step - to find why one has none)
            Fl::visual(FL_DOUBLE | FL_INDEX);
            lune::load_prefs();
            return appimage_icon_trace(argv[++i]);
        }
    }
    if (!prefs_program.empty()) {
        // a preferences program on its own (SYS:Prefs/...): its editor, the program ends with it
        Fl::visual(FL_DOUBLE | FL_INDEX);
        lune::load_prefs();
        fl_open_display();
        if (prefs_program == "input") edit_input_prefs(true);
        else if (prefs_program == "lune") edit_gui_settings(true);
        else if (prefs_program == "screenmode") edit_screenmode_prefs(true);
        else if (prefs_program == "locale") edit_locale_prefs(true);
        else if (prefs_program == "pointer") edit_pointer_prefs(true);
        else if (prefs_program == "font") edit_font_prefs(true);
        else if (prefs_program == "network") edit_network_prefs(true);
        else if (prefs_program == "rambler") edit_prefs(RamblerPrefs::load(), [](const RamblerPrefs &) {}, true);
        else {
            fprintf(stderr, "rambler: no preferences program '%s' (font, input, locale, lune, network, pointer, rambler, screenmode)\n", prefs_program.c_str());
            return 1;
        }
        return run();
    }
    Fl::visual(FL_DOUBLE | FL_INDEX);
    Fl::get_system_colors();
    Fl::add_system_handler(x_events, nullptr);
    lune::load_prefs();
    fl_open_display();
    g_rambler.start(backdrop);
    // as IPrefs: the input preferences put into effect (when there are any saved)
    if (backdrop && ScreenModePrefs::exists()) ScreenModePrefs::load().apply();
    if (backdrop) LocalePrefs::load().apply();
    if (backdrop) PointerPrefs::load().apply();
    if (backdrop && InputPrefs::exists()) InputPrefs::load().apply();
    if (backdrop) make_prefs_drawer();
    if (backdrop && first_in_session()) run_startup();   // once per session (not again after a restart)
    Fl::add_timeout(5.0, Rambler::tick, &g_rambler);
    Fl::add_timeout(0.5, Rambler::watch_volumes, &g_rambler);
    Rambler::watch_prefs(&g_rambler);   // (the first look only notes what is there)
    return run();
}
