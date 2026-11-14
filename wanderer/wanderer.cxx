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

private:
    IconWindow *root_ = nullptr;
    std::vector<IconWindow *> windows_;
    IconWindow *active_ = nullptr;
    bool backdrop_ = false;

    void setup(IconWindow *w);
    void apply_snapshot(IconWindow *w);
    void close(IconWindow *w);
    void update_all(const std::string &dir);
    IconWindow *window_at(int x_root, int y_root, IconWindow *except);
    void drop(IconWindow *from, std::vector<IconEntry *> &entries, IconEntry *target);
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
    MEN_ICON_UNSNAPSHOT, MEN_ICON_DELETE, MEN_ICON_DISKINFO
};

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
    w->iconlist()->on_drop = [this, w](std::vector<IconEntry *> &entries, IconEntry *target) { drop(w, entries, target); };
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
        if (request("Wanderer", "Do you really want to quit Wanderer?", "Quit|Cancel") == 1) exit(0);
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

void Wanderer::open_entry(IconWindow *, IconEntry &e) {
    if (e.is_drawer()) { open_drawer(e.path); return; }
    struct stat st;
    std::string dir = fs::path(e.path).parent_path().string();
    // a tool runs; a project opens with the program for its type
    if (stat(e.path.c_str(), &st) == 0 && (st.st_mode & S_IXUSR) && S_ISREG(st.st_mode)) launch({e.path}, dir);
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
    struct stat sd, ss;
    if (stat(dest.c_str(), &sd) != 0) return;
    std::vector<std::string> sources;
    for (auto *e : entries) sources.push_back(e->path);
    for (auto &src : sources) {
        if (stat(src.c_str(), &ss) != 0) continue;
        fs::path to = fs::path(dest) / fs::path(src).filename();
        if (fs::path(src).parent_path() == fs::path(dest) || to == fs::path(src)) continue;
        std::error_code ec;
        if (fs::exists(to, ec)) {
            if (request("Wanderer", "'" + to.filename().string() + "' already exists in the destination.\nReplace it?",
                        "Replace|Skip") != 1)
                continue;
            fs::remove_all(to, ec);
        }
        if (ss.st_dev == sd.st_dev) fs::rename(src, to, ec);
        else fs::copy(src, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec) request("Wanderer", "Could not " + std::string(ss.st_dev == sd.st_dev ? "move" : "copy") + " '" +
                                        fs::path(src).filename().string() + "':\n" + ec.message(), "OK");
    }
    from->update();
    update_all(dest);
}

// --------------------------------------------------------------------------------------------
// The menus
// --------------------------------------------------------------------------------------------
void Wanderer::show_menu(IconWindow *w, int xr, int yr) {
    active_ = w;
    IconList *l = w->iconlist();
    bool sel = !l->selection().empty();
    bool drawer = !w->is_root();
    int sf = l->sort_flags, df = l->display_flags;
    auto chk = [](bool on) { return FL_MENU_RADIO | (on ? FL_MENU_VALUE : 0); };
    auto tog = [](bool on) { return FL_MENU_TOGGLE | (on ? FL_MENU_VALUE : 0); };
    int dis = FL_MENU_INACTIVE;
    Fl_Menu_Item menu[] = {
        {"Wanderer", 0, nullptr, nullptr, FL_SUBMENU},
            {"Backdrop", FL_META + 'b', nullptr, (void *)MEN_WANDERER_BACKDROP, tog(backdrop_)},
            {"Execute Command...", FL_META + 'e', nullptr, (void *)MEN_WANDERER_EXECUTE},
            {"Shell", FL_META + 'w', nullptr, (void *)MEN_WANDERER_SHELL},
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
            {"Find...", FL_META + 'f', nullptr, (void *)MEN_WINDOW_FIND, dis},
            {nullptr},
        {"Icon", 0, nullptr, nullptr, FL_SUBMENU},
            {"Open", FL_META + 'o', nullptr, (void *)MEN_ICON_OPEN, sel ? 0 : dis},
            {"Rename...", FL_META + 'r', nullptr, (void *)MEN_ICON_RENAME, sel && drawer ? 0 : dis},
            {"Information...", FL_META + 'i', nullptr, (void *)MEN_ICON_INFORMATION, sel ? 0 : dis},
            {"Snapshot", FL_META + 's', nullptr, (void *)MEN_ICON_SNAPSHOT, sel ? 0 : dis},
            {"Unsnapshot", FL_META + 'u', nullptr, (void *)MEN_ICON_UNSNAPSHOT, (sel ? 0 : dis) | FL_MENU_DIVIDER},
            {"Delete...", 0, nullptr, (void *)MEN_ICON_DELETE, sel && drawer ? 0 : dis},
            {"Disk Info...", 0, nullptr, (void *)MEN_ICON_DISKINFO, sel && !drawer ? 0 : dis},
            {nullptr},
        {"Tools", 0, nullptr, nullptr, FL_SUBMENU | FL_MENU_INACTIVE},
            {nullptr},
        {nullptr}};
    // the menus look like the screen's: the window's pens, the screen font
    for (auto &m : menu) {
        if (!m.text) continue;
        m.labelfont(prefs().font);
        m.labelsize(prefs().font_size);
    }
    Fl_Menu_Item proto;
    const Fl_Menu_Item *picked = menu->popup(xr - w->x(), yr - w->y(), nullptr, nullptr, nullptr);
    (void)proto;
    if (picked && picked->user_data()) menu_action(w, (int)(intptr_t)picked->user_data());
}

void Wanderer::menu_action(IconWindow *w, int id) {
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
    case MEN_WANDERER_ABOUT:
        request("About Wanderer",
                "Wanderer for X11\n\nA port of the AROS desktop and file manager.\n"
                "AROS Public License 1.1 - The AROS Development Team.",
                "OK");
        break;
    case MEN_WANDERER_QUIT:
        if (request("Wanderer", "Do you really want to quit Wanderer?", "Quit|Cancel") == 1) exit(0);
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
        if (request("Delete", "Do you really want to delete\n" + what + "?\nThis cannot be undone.", "Delete|Cancel") == 1) {
            for (auto *e : sel) {
                std::error_code ec;
                fs::remove_all(e->path, ec);
                if (ec) request("Delete", "Could not delete '" + e->label + "':\n" + ec.message(), "OK");
            }
            w->update();
        }
        break;
    }
    case MEN_ICON_DISKINFO: for (auto *e : l->selection()) disk_info(e->path); break;
    }
}

}  // namespace

int main(int argc, char **argv) {
    bool backdrop = false;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--backdrop")) backdrop = true;
    Fl::visual(FL_DOUBLE | FL_INDEX);
    Fl::get_system_colors();
    g_wanderer.start(backdrop);
    return run();
}
