/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Wanderer: the root window with the volumes (a backdrop or a window), the drawer
    windows opened from it, the menus (Wanderer, Window, Icon, Tools) and their
    functions. Ported from Wanderer/wanderer.c and main.c.

    Until the window manager draws the screen's title bar, the menus open with the right
    mouse button over a Wanderer window (as Intuition's menus open with it over the
    screen).
*/
#include "iconwindow.h"
#include "tools.h"
#include "prefs.h"
#include "fileops.h"
#include "guisettings.h"

#include <FL/Fl.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace zune;
using namespace wanderer;

namespace {

// --------------------------------------------------------------------------------------------
// Snapshots: a drawer's window place and its icons' positions (on AROS kept in the .info
// files; here in ~/.config/wanderer/drawers)
// --------------------------------------------------------------------------------------------
std::string config_dir() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/wanderer";
}

std::string snapshot_file(const std::string &location) {
    std::string key = location.empty() ? std::string("ROOT") : location;
    for (auto &c : key) if (c == '/') c = '|';
    return config_dir() + "/drawers/" + key;
}

struct Snapshot {
    int x = -1, y = -1, w = 0, h = 0;
    std::map<std::string, std::pair<int, int>> icons;
};

Snapshot load_snapshot(const std::string &location) {
    Snapshot s;
    std::ifstream in(snapshot_file(location));
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("window ", 0) == 0) sscanf(line.c_str() + 7, "%d %d %d %d", &s.x, &s.y, &s.w, &s.h);
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
    for (auto &[name, p] : s.icons) out << "icon " << p.first << ' ' << p.second << ' ' << name << '\n';
}

// --------------------------------------------------------------------------------------------
class Wanderer {
public:
    void start(bool backdrop);
    IconWindow *open_drawer(const std::string &path);
    void open_entry(IconWindow *w, IconEntry &e);
    void show_menu(IconWindow *w, int x_root, int y_root);
    std::vector<Fl_Menu_Item> menu_items(IconWindow *w);
    void publish_menu(IconWindow *w);
    std::vector<ToolItem> tools_;   // the Tools menu
    void publish_title(IconWindow *w);
    static void tick(void *d);
    static void watch_volumes(void *d);
    void mount_and_open(const std::string &device);
    void eject(const std::string &device);
    std::string volumes_state_;
    void pick(IconWindow *w, int id);
    bool shortcut(IconWindow *w);

private:
    IconWindow *root_ = nullptr;
    WandererPrefs prefs_ = WandererPrefs::load();
    void apply_prefs(IconWindow *w);
    std::vector<IconWindow *> windows_;
    IconWindow *active_ = nullptr;
    bool backdrop_ = false;

    void setup(IconWindow *w);
    void apply_snapshot(IconWindow *w);
    void close(IconWindow *w);
    void update_all(const std::string &dir);
    IconWindow *window_at(int x_root, int y_root, IconWindow *except);
    void drop(IconWindow *from, std::vector<IconEntry *> &entries, IconEntry *target);
    void drop_paths(const std::vector<std::string> &sources, const std::string &dest);
    void menu_action(IconWindow *w, int id);
};

Wanderer g_wanderer;

// The menu items (wanderer.c: MEN_*)
enum MenuId {
    MEN_WANDERER_BACKDROP = 1, MEN_WANDERER_EXECUTE, MEN_WANDERER_SHELL, MEN_WANDERER_ABOUT, MEN_WANDERER_QUIT,
    MEN_WINDOW_NEW_DRAWER, MEN_WINDOW_OPEN_PARENT, MEN_WINDOW_CLOSE, MEN_WINDOW_UPDATE, MEN_WINDOW_SELECT,
    MEN_WINDOW_CLEAR, MEN_WINDOW_SNAP_WIN, MEN_WINDOW_SNAP_ALL, MEN_WINDOW_VIEW_ICON, MEN_WINDOW_VIEW_DETAIL,
    MEN_WINDOW_VIEW_ALL, MEN_WINDOW_SORT_NOW, MEN_WINDOW_SORT_ENABLE, MEN_WINDOW_SORT_NAME, MEN_WINDOW_SORT_DATE,
    MEN_WINDOW_SORT_SIZE, MEN_WINDOW_SORT_TYPE, MEN_WINDOW_SORT_REVERSE, MEN_WINDOW_SORT_TOPDRAWERS,
    MEN_WINDOW_FIND, MEN_ICON_OPEN, MEN_ICON_RENAME, MEN_ICON_INFORMATION, MEN_ICON_SNAPSHOT,
    MEN_ICON_UNSNAPSHOT, MEN_ICON_DELETE, MEN_ICON_DISKINFO, MEN_WANDERER_PREFS,
    MEN_ICON_LEAVEOUT, MEN_ICON_PUTAWAY, MEN_ICON_EMPTYTRASH, MEN_WANDERER_GUISETTINGS, MEN_ICON_EJECT, MEN_ICON_COPY, MEN_TOOLS_DRAWER,
    MEN_TOOL_FIRST = 1000
};

// The preferences on a window: the labels, the layout, the bars, the filter, the order
void Wanderer::apply_prefs(IconWindow *w) {
    IconList *l = w->iconlist();
    l->text_mode = (TextMode)prefs_.text_mode;
    l->label_multiline = prefs_.label_lines;
    l->label_maxlen = prefs_.label_maxlen;
    l->hspacing = prefs_.hspacing;
    l->vspacing = prefs_.vspacing;
    if (w->is_root()) l->backfill(prefs_.backdrop_image, prefs_.backdrop_mode);
    else l->backfill(prefs_.drawer_image, prefs_.drawer_mode);
    if (!w->is_root()) {
        l->list_mode = (ListMode)prefs_.list_mode;
        if (prefs_.show_hidden) l->display_flags |= Disp_ShowHidden; else l->display_flags &= ~Disp_ShowHidden;
        if (prefs_.drawers_first) l->sort_flags &= ~(Sort_DrawersMixed | Sort_DrawersLast);
        else l->sort_flags |= Sort_DrawersMixed;
        w->show_toolbar(prefs_.toolbar);
        w->show_statusbar(prefs_.statusbar);
    }
}

// --------------------------------------------------------------------------------------------
void Wanderer::start(bool backdrop) {
    backdrop_ = backdrop;
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
        root_->label("Wanderer");
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

void Wanderer::setup(IconWindow *w) {
    apply_prefs(w);
    w->on_open = [this](IconWindow *win, IconEntry &e) { open_entry(win, e); };
    w->on_activate = [this](IconWindow *win) { active_ = win; };
    w->on_parent = [this](IconWindow *win) {
        if (win->is_root()) return;
        fs::path p = fs::path(win->location()).parent_path();
        if (p.string() != win->location()) open_drawer(p.string());
    };
    w->on_location = [this](IconWindow *, const std::string &loc) {
        std::error_code ec;
        if (fs::is_directory(loc, ec)) open_drawer(loc);
    };
    w->iconlist()->on_context_menu = [this, w](int x, int y) { show_menu(w, x, y); };
    w->iconlist()->on_selection_changed = [this, w] { w->update_status(); publish_menu(w); };
    w->on_shortcut = [this](IconWindow *win) { return shortcut(win); };
    w->on_shown = [this](IconWindow *win) { publish_menu(win); publish_title(win); };
    w->iconlist()->on_drop = [this, w](std::vector<IconEntry *> &entries, IconEntry *target) { drop(w, entries, target); };
    w->iconlist()->on_external_drop = [this, w](const std::vector<std::string> &paths, IconEntry *target) {
        std::string dest = target ? target->path : w->location();
        if (dest.empty()) return;   // the root window: no drawer of its own
        drop_paths(paths, dest);
    };
    w->callback([](Fl_Widget *win, void *d) { ((Wanderer *)d)->close((IconWindow *)win); }, this);
}

void Wanderer::apply_snapshot(IconWindow *w) {
    Snapshot s = load_snapshot(w->location());
    if (s.w > 0) w->resize(s.x, s.y, s.w, s.h);
    if (!s.icons.empty()) {
        for (auto &e : w->iconlist()->entries()) {
            auto it = s.icons.find(fs::path(e.path).filename().string());
            if (it == s.icons.end() && w->is_root()) it = s.icons.find(e.path);
            if (it != s.icons.end()) { e.provided_x = it->second.first; e.provided_y = it->second.second; }
        }
        w->iconlist()->rethink();
    }
}

IconWindow *Wanderer::open_drawer(const std::string &path) {
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

void Wanderer::close(IconWindow *w) {
    if (w == root_) {
        if (request("Wanderer", file_operations_running() ? "Files are still being copied, moved or deleted.\nQuit Wanderer anyway?"
                                                         : "Do you really want to quit Wanderer?", "Quit|Cancel") == 1) exit(0);
        return;
    }
    windows_.erase(std::remove(windows_.begin(), windows_.end(), w), windows_.end());
    if (active_ == w) active_ = nullptr;
    w->hide();
    Fl::delete_widget(w);
}

void Wanderer::update_all(const std::string &dir) {
    for (auto *w : windows_) if (w->location() == dir) w->update();
}

// A volume not mounted yet: mounted (udisks, as the user), then opened
void Wanderer::mount_and_open(const std::string &device) {
    std::string out;
    FILE *p = popen(("udisksctl mount --no-user-interaction -b '" + device + "' 2>&1").c_str(), "r");
    if (!p) return;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
    int rc = pclose(p);
    if (rc != 0) {
        request("Wanderer", "Could not mount " + device + ":\n" + out, "OK");
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
void Wanderer::eject(const std::string &device) {
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
    if (rc != 0) { request("Eject", "Could not unmount " + device + ":\n" + out, "OK"); return; }
    // the whole drive off (a memory stick can then be pulled out); not possible for some
    std::string disk = device;
    while (!disk.empty() && isdigit((unsigned char)disk.back())) disk.pop_back();
    if (disk.size() > 2 && disk.compare(disk.size() - 1, 1, "p") == 0 && disk.find("nvme") != std::string::npos) disk.pop_back();
    if (system(("udisksctl power-off --no-user-interaction -b '" + disk + "' >/dev/null 2>&1").c_str()) != 0) {}
    if (root_) root_->update();
}

void Wanderer::open_entry(IconWindow *, IconEntry &e) {
    if (e.unmounted) { mount_and_open(e.device); return; }
    if (e.is_drawer()) { open_drawer(e.path); return; }
    struct stat st;
    std::string dir = fs::path(e.path).parent_path().string();
    // a tool runs; a project opens with its default tool, else the program for its type
    if (stat(e.path.c_str(), &st) == 0 && (st.st_mode & S_IXUSR) && S_ISREG(st.st_mode)) { launch({e.path}, dir); return; }
    std::string tool = default_tool(e.path);
    if (!tool.empty()) launch({"/bin/sh", "-c", tool + " \"$1\"", "sh", e.path}, dir);
    else launch({"xdg-open", e.path}, dir);
}

IconWindow *Wanderer::window_at(int xr, int yr, IconWindow *except) {
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
// the root window (left out - not yet). Within one volume they move, across volumes they
// are copied (Wanderer's rule).
void Wanderer::drop(IconWindow *from, std::vector<IconEntry *> &entries, IconEntry *target) {
    std::string dest;
    if (target) dest = target->path;
    else {
        IconWindow *to = window_at(Fl::event_x_root(), Fl::event_y_root(), from);
        if (!to || to->is_root()) {
            if (to && to->is_root()) {
                IconEntry *vol = to->iconlist()->entry_at(Fl::event_x_root() - to->x(), Fl::event_y_root() - to->y());
                if (vol) dest = vol->path;
            }
            if (dest.empty()) { from->iconlist()->rethink(); return; }
        } else {
            IconEntry *onto = to->iconlist()->entry_at(Fl::event_x_root() - to->x(), Fl::event_y_root() - to->y());
            dest = onto && onto->is_drawer() ? onto->path : to->location();
        }
    }
    std::vector<std::string> sources;
    for (auto *e : entries) if (!e->trash && e->type != ST_ROOT) sources.push_back(e->path);
    from->iconlist()->rethink();
    drop_paths(sources, dest);
}

// Files into a drawer: to the trash; within one volume moved, onto another copied
void Wanderer::drop_paths(const std::vector<std::string> &sources, const std::string &dest) {
    auto refresh = [this, dest] { update_all(dest); for (auto *w : windows_) w->update(); if (root_) root_->update(); };
    if (dest == trash_dir()) { file_operation(FileOpKind::Trash, sources, dest, refresh); return; }
    struct stat sd;
    if (stat(dest.c_str(), &sd) != 0) return;
    std::vector<std::string> move, copy;
    for (auto &src : sources) {
        struct stat ss;
        if (stat(src.c_str(), &ss) != 0) continue;
        (ss.st_dev == sd.st_dev ? move : copy).push_back(src);
    }
    if (!move.empty()) file_operation(FileOpKind::Move, move, dest, refresh);
    if (!copy.empty()) file_operation(FileOpKind::Copy, copy, dest, refresh);
}

// --------------------------------------------------------------------------------------------
// The menus
// --------------------------------------------------------------------------------------------
std::vector<Fl_Menu_Item> Wanderer::menu_items(IconWindow *w) {
    IconList *l = w->iconlist();
    bool sel = !l->selection().empty();
    bool drawer = !w->is_root();
    bool left_out_sel = false, trash_sel = w->location() == trash_dir(), volume_sel = false, eject_sel = false;
    for (auto *e : l->selection()) {
        if (e->left_out) left_out_sel = true;
        if (e->trash) trash_sel = true;
        if (e->type == ST_ROOT) volume_sel = true;
        if (e->type == ST_ROOT && !e->device.empty() && !e->unmounted) eject_sel = true;
    }
    int sf = l->sort_flags, df = l->display_flags;
    auto chk = [](bool on) { return FL_MENU_RADIO | (on ? FL_MENU_VALUE : 0); };
    auto tog = [](bool on) { return FL_MENU_TOGGLE | (on ? FL_MENU_VALUE : 0); };
    int dis = FL_MENU_INACTIVE;
    Fl_Menu_Item menu[] = {
        {"Wanderer", 0, nullptr, nullptr, FL_SUBMENU},
            {"Backdrop", FL_META + 'b', nullptr, (void *)MEN_WANDERER_BACKDROP, tog(backdrop_)},
            {"Execute Command...", FL_META + 'e', nullptr, (void *)MEN_WANDERER_EXECUTE},
            {"Shell", FL_META + 'w', nullptr, (void *)MEN_WANDERER_SHELL},
            {"Preferences...", 0, nullptr, (void *)MEN_WANDERER_PREFS},
            {"GUI Settings...", 0, nullptr, (void *)MEN_WANDERER_GUISETTINGS},
            {"About...", 0, nullptr, (void *)MEN_WANDERER_ABOUT},
            {"Quit...", FL_META + 'q', nullptr, (void *)MEN_WANDERER_QUIT},
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
                {"All Files", 0, nullptr, (void *)MEN_WINDOW_VIEW_ALL, tog(df & Disp_ShowHidden)},
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
            {"Open", FL_META + 'o', nullptr, (void *)MEN_ICON_OPEN, sel ? 0 : dis},
            {"Copy", FL_META + 'c', nullptr, (void *)MEN_ICON_COPY, sel && drawer ? 0 : dis},
            {"Rename...", FL_META + 'r', nullptr, (void *)MEN_ICON_RENAME, sel && drawer ? 0 : dis},
            {"Information...", FL_META + 'i', nullptr, (void *)MEN_ICON_INFORMATION, sel ? 0 : dis},
            {"Snapshot", FL_META + 's', nullptr, (void *)MEN_ICON_SNAPSHOT, sel ? 0 : dis},
            {"Unsnapshot", FL_META + 'u', nullptr, (void *)MEN_ICON_UNSNAPSHOT, sel ? 0 : dis},
            {"Leave Out", FL_META + 'l', nullptr, (void *)MEN_ICON_LEAVEOUT, sel && drawer ? 0 : dis},
            {"Put Away", FL_META + 'p', nullptr, (void *)MEN_ICON_PUTAWAY, (left_out_sel ? 0 : dis) | FL_MENU_DIVIDER},
            {"Delete...", 0, nullptr, (void *)MEN_ICON_DELETE, sel && drawer ? 0 : dis},
            {"Disk Info...", 0, nullptr, (void *)MEN_ICON_DISKINFO, volume_sel ? 0 : dis},
            {"Eject", FL_META + 'j', nullptr, (void *)MEN_ICON_EJECT, eject_sel ? 0 : dis},
            {"Empty Trash", 0, nullptr, (void *)MEN_ICON_EMPTYTRASH, trash_sel ? 0 : dis},
            {nullptr},
        {"Tools", 0, nullptr, nullptr, FL_SUBMENU},
            {"Tools Drawer", 0, nullptr, (void *)MEN_TOOLS_DRAWER},
            {nullptr},
        {nullptr}};
    // the menus look like the screen's: the window's pens, the screen font
    for (auto &m : menu) {
        if (!m.text) continue;
        m.labelfont(prefs().font);
        m.labelsize(prefs().font_size);
    }
    std::vector<Fl_Menu_Item> v(menu, menu + sizeof menu / sizeof menu[0]);
    // the Tools menu: the user's tools (~/.config/wanderer/tools), then its drawer
    tools_ = tool_items();
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
    return v;
}

// Without the window manager's screen bar: the menus as a pop-up under the pointer
void Wanderer::show_menu(IconWindow *w, int xr, int yr) {
    active_ = w;
    auto items = menu_items(w);
    const Fl_Menu_Item *picked = items.data()->popup(xr - w->x(), yr - w->y(), nullptr, nullptr, nullptr);
    if (picked && picked->user_data()) menu_action(w, (int)(intptr_t)picked->user_data());
    publish_menu(w);
}

// The screen's title while a Wanderer window is active: the version and the free memory
// (as Wanderer shows on its screen); _WANDERER_SCREEN_TITLE, read by the window manager
static std::string screen_title() {
    std::ifstream m("/proc/meminfo");
    std::string k;
    long v = 0, avail = -1;
    std::string unit;
    while (m >> k >> v >> unit) if (k == "MemAvailable:") { avail = v; break; }
    std::string t = "Wanderer " WANDERER_VERSION;
    if (avail >= 0) {
        char b[64];
        snprintf(b, sizeof b, "    %.1f MB free", avail / 1024.0);
        t += b;
    }
    return t;
}

void Wanderer::publish_title(IconWindow *w) {
    if (!w || !w->shown()) return;
    std::string t = screen_title();
    Atom prop = XInternAtom(fl_display, "_WANDERER_SCREEN_TITLE", False);
    Atom utf8 = XInternAtom(fl_display, "UTF8_STRING", False);
    XChangeProperty(fl_display, fl_xid(w), prop, utf8, 8, PropModeReplace, (const unsigned char *)t.data(), (int)t.size());
}

// Disks put in or taken out, mounted or unmounted: the root window follows (every 2 seconds
// the mounts and the removable devices are compared with what it shows)
void Wanderer::watch_volumes(void *d) {
    auto *self = (Wanderer *)d;
    std::string state;
    {
        std::ifstream m("/proc/self/mounts");
        std::string line;
        while (std::getline(m, line)) if (line.rfind("/dev/", 0) == 0) state += line + "\n";
    }
    for (auto &v : removable_volumes()) state += v.device + "|" + v.mountpoint + "\n";
    if (state != self->volumes_state_) {
        bool first = self->volumes_state_.empty();
        self->volumes_state_ = state;
        if (!first && self->root_) { self->root_->update(); self->apply_snapshot(self->root_); }
    }
    Fl::repeat_timeout(2.0, watch_volumes, d);
}

void Wanderer::tick(void *d) {
    auto *self = (Wanderer *)d;
    for (auto *w : self->windows_) self->publish_title(w);
    self->publish_title(self->root_);
    Fl::repeat_timeout(5.0, tick, d);
}

// The menus for the window manager: it shows them in the screen's title bar while the right
// mouse button is held over the window (as Intuition does) and sends back the item picked.
// _WANDERER_MENU on the window, one line per item: depth TAB id TAB flags TAB label TAB key;
// flags: 1 disabled, 2 check mark item, 4 checked, 8 a line after it
void Wanderer::publish_menu(IconWindow *w) {
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
    Atom prop = XInternAtom(fl_display, "_WANDERER_MENU", False);
    Atom utf8 = XInternAtom(fl_display, "UTF8_STRING", False);
    XChangeProperty(fl_display, fl_xid(w), prop, utf8, 8, PropModeReplace, (const unsigned char *)out.data(), (int)out.size());
}

// The item picked in the screen bar (a client message from the window manager)
static int x_events(void *event, void *) {
    XEvent *e = (XEvent *)event;
    static Atom pick = XInternAtom(fl_display, "_WANDERER_MENU_PICK", False);
    if (e->type != ClientMessage || e->xclient.message_type != pick) return 0;
    Fl_Window *fw = fl_find(e->xclient.window);
    if (auto *w = dynamic_cast<IconWindow *>(fw)) g_wanderer.pick(w, (int)e->xclient.data.l[0]);
    return 1;
}

void Wanderer::pick(IconWindow *w, int id) {
    active_ = w;
    menu_action(w, id);
    for (auto *o : windows_) publish_menu(o);
    if (root_) publish_menu(root_);
}

// The keyboard shortcuts of the menus (Super+key)
bool Wanderer::shortcut(IconWindow *w) {
    auto items = menu_items(w);
    const Fl_Menu_Item *m = items.data()->test_shortcut();
    if (!m || !m->user_data() || !m->active()) return false;
    pick(w, (int)(intptr_t)m->user_data());
    return true;
}

void Wanderer::menu_action(IconWindow *w, int id) {
    if (id >= MEN_TOOL_FIRST) {
        // a tool: started in the window's drawer, with the selected icons' paths as arguments
        size_t i = id - MEN_TOOL_FIRST;
        if (i >= tools_.size()) return;
        std::vector<std::string> argv = {"/bin/sh", "-c", tools_[i].command + " \"$@\"", "sh"};
        for (auto *e : w->iconlist()->selection()) argv.push_back(e->path);
        launch(argv, w->is_root() ? (getenv("HOME") ? getenv("HOME") : "/") : w->location());
        return;
    }
    IconList *l = w->iconlist();
    auto resort = [l, w] { l->sort(); for (auto &e : l->entries()) e.provided_x = e.provided_y = NO_ICON_POSITION; l->rethink(); w->update_status(); };
    switch (id) {
    case MEN_WANDERER_BACKDROP: {
        // the root window between a backdrop and a window: made again
        bool b = !backdrop_;
        root_->hide();
        Fl::delete_widget(root_);
        start(b);
        break;
    }
    case MEN_WANDERER_EXECUTE: execute_command(w->is_root() ? getenv("HOME") : w->location()); break;
    case MEN_WANDERER_SHELL: launch({"x-terminal-emulator"}, w->is_root() ? getenv("HOME") : w->location()); break;
    case MEN_WANDERER_PREFS:
        edit_prefs(prefs_, [this](const WandererPrefs &p) {
            prefs_ = p;
            for (auto *o : windows_) { apply_prefs(o); o->update(); apply_snapshot(o); }
            if (root_) { apply_prefs(root_); root_->update(); apply_snapshot(root_); }
        });
        break;
    case MEN_WANDERER_GUISETTINGS: edit_gui_settings(); break;
    case MEN_WANDERER_ABOUT:
        request("About Wanderer",
                "Wanderer for X11 " WANDERER_VERSION "\n\nA port of the AROS desktop and file manager.\n"
                "AROS Public License 1.1 - The AROS Development Team.",
                "OK");
        break;
    case MEN_WANDERER_QUIT:
        if (request("Wanderer", file_operations_running() ? "Files are still being copied, moved or deleted.\nQuit Wanderer anyway?"
                                                         : "Do you really want to quit Wanderer?", "Quit|Cancel") == 1) exit(0);
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
        if (id == MEN_WINDOW_SNAP_ALL)
            for (auto &e : l->entries())
                s.icons[w->is_root() ? e.path : fs::path(e.path).filename().string()] = {e.x, e.y};
        save_snapshot(w->location(), s);
        break;
    }
    case MEN_WINDOW_VIEW_ICON: l->display_flags = (l->display_flags & ~Disp_ModeMask) | Disp_ModeDefault; l->rethink(); break;
    case MEN_WINDOW_VIEW_DETAIL: l->display_flags = (l->display_flags & ~Disp_ModeMask) | Disp_ModeList; l->rethink(); break;
    case MEN_WINDOW_VIEW_ALL: l->display_flags ^= Disp_ShowHidden; w->update(); break;
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
            std::string key = w->is_root() ? e->path : fs::path(e->path).filename().string();
            if (id == MEN_ICON_SNAPSHOT) s.icons[key] = {e->x, e->y};
            else { s.icons.erase(key); e->provided_x = e->provided_y = NO_ICON_POSITION; }
        }
        save_snapshot(w->location(), s);
        if (id == MEN_ICON_UNSNAPSHOT) l->rethink();
        break;
    }
    case MEN_ICON_DELETE: {
        auto sel = l->selection();
        std::string what = sel.size() == 1 ? "'" + sel[0]->label + "'" : std::to_string(sel.size()) + " icons";
        int r = request("Delete", "Delete " + what + "?\nMove to Trash keeps them until the trash is emptied;\n"
                        "Delete removes them for good.", "Move to Trash|Delete|Cancel");
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
    case MEN_TOOLS_DRAWER: {
        const char *x = getenv("XDG_CONFIG_HOME");
        std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
        open_drawer(base + "/wanderer/tools");
        break;
    }
    case MEN_ICON_COPY: {
        std::vector<std::string> paths;
        for (auto *e : l->selection()) paths.push_back(e->path);
        file_operation(FileOpKind::Duplicate, paths, "", [this, w] { w->update(); apply_snapshot(w); });
        break;
    }
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
    case MEN_WINDOW_FIND: {
        std::string pattern;
        if (!ask_find(pattern)) break;
        auto *list = new IconSearchList(w->location(), pattern);
        auto *fw = new IconWindow("Find: " + pattern + " in " + w->location(), false, list);
        setup(fw);
        fw->show_toolbar(false);
        windows_.push_back(fw);
        fw->position(w->x() + 30, w->y() + 30);
        fw->show();
        fw->update();
        break;
    }
    }
}

}  // namespace

// The first Wanderer of this X session? (the window manager's check window names the session;
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
    std::string marker = std::string(rt && *rt ? rt : "/tmp") + "/wanderer-startup-" + std::to_string(getuid()) + "-" +
                         (disp ? disp : "") ;
    std::string want = std::to_string(id);
    std::ifstream in(marker);
    std::string have;
    std::getline(in, have);
    if (id && have == want) return false;
    std::ofstream(marker) << want << "\n";
    return true;
}

int main(int argc, char **argv) {
    bool backdrop = false;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--backdrop")) backdrop = true;
    Fl::visual(FL_DOUBLE | FL_INDEX);
    Fl::get_system_colors();
    Fl::add_system_handler(x_events, nullptr);
    zune::load_prefs();
    fl_open_display();
    g_wanderer.start(backdrop);
    if (backdrop && first_in_session()) run_startup();   // once per session (not again after a restart)
    Fl::add_timeout(5.0, Wanderer::tick, &g_wanderer);
    Fl::add_timeout(0.5, Wanderer::watch_volumes, &g_wanderer);
    return run();
}
