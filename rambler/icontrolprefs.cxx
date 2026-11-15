/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The IControl preferences (prefs/icontrol/icontroleditor.c, prefs.c): see icontrolprefs.h.
    The settings, one a line ("name=value"):
      frontmost_default  1: new windows open on the front screen (else on the first one)
      drag_bottom        1: a screen can be dragged down to the bottom edge (else its bar
                         stays in sight)
      meta_drag          the key held to drag a screen from anywhere: 0 Super, 1 Alt, 2 Ctrl
      menu_look          0 classic, 1 3D
      menu_sticky        1: a click of the menu button leaves the menus open
      offscreen          1: windows can be moved partly off the screen
*/
#include "icontrolprefs.h"
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/platform.H>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

namespace {
struct IControl {
    bool frontmost_default = true, drag_bottom = true, menu_sticky = false, offscreen = true;
    int meta_drag = 0, menu_look = 0;
    std::string text() const {
        std::ostringstream o;
        o << "frontmost_default=" << frontmost_default << "\ndrag_bottom=" << drag_bottom << "\nmeta_drag=" << meta_drag
          << "\nmenu_look=" << menu_look << "\nmenu_sticky=" << menu_sticky << "\noffscreen=" << offscreen << "\n";
        return o.str();
    }
};

std::string saved_file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/rambler/icontrol.prefs";
}

IControl load() {
    IControl p;
    std::ifstream in(env_dir() + "/icontrol.prefs");
    if (!in) in.open(saved_file());
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        int v = atoi(line.c_str() + eq + 1);
        if (k == "frontmost_default") p.frontmost_default = v;
        else if (k == "drag_bottom") p.drag_bottom = v;
        else if (k == "meta_drag") p.meta_drag = std::clamp(v, 0, 2);
        else if (k == "menu_look") p.menu_look = std::clamp(v, 0, 1);
        else if (k == "menu_sticky") p.menu_sticky = v;
        else if (k == "offscreen") p.offscreen = v;
    }
    return p;
}

void put(const IControl &p, bool archive) {
    std::string t = p.text();
    for (auto &f : archive ? std::vector<std::string>{env_dir() + "/icontrol.prefs", saved_file()}
                           : std::vector<std::string>{env_dir() + "/icontrol.prefs"}) {
        std::error_code ec;
        fs::create_directories(fs::path(f).parent_path(), ec);
        std::ofstream(f) << t;
    }
    // the window manager: at once
    if (fl_display) {
        Atom prop = XInternAtom(fl_display, "_RAMBLER_ICONTROL", False), utf8 = XInternAtom(fl_display, "UTF8_STRING", False);
        XChangeProperty(fl_display, RootWindow(fl_display, fl_screen), prop, utf8, 8, PropModeReplace,
                        (const unsigned char *)t.c_str(), (int)t.size());
        XFlush(fl_display);
    }
}
}  // namespace

static lune::Window *g_ic_editor = nullptr;

void edit_icontrol_prefs(bool standalone) {
    if (g_ic_editor) { g_ic_editor->show(); return; }
    IControl p = load();
    auto *root = VGroup();
    auto *reg = new Register({"Screens", "Menus", "Windows"});
    // -- Screens
    auto *sp = VGroup();
    auto *pg = new Group(Group::Vertical, Frame_Group);
    pg->frame_title("Public Screens");
    pg->columns(2);
    new Text("\033rFrontmost screen is default:");
    auto *front = new Checkmark(p.frontmost_default);
    pg->end();
    auto *dg = new Group(Group::Vertical, Frame_Group);
    dg->frame_title("Drag");
    new Text("\033cScreens can be dragged beyond the:");
    auto *bg = new Group(Group::Vertical);
    bg->columns(2);
    // (screens move only down here: the other edges are shown as the original has them)
    new Text("\033rLeft edge:");
    auto *bl = new Checkmark(false);
    bl->disabled(true);
    new Text("\033rRight edge:");
    auto *br = new Checkmark(false);
    br->disabled(true);
    new Text("\033rTop edge:");
    auto *bt = new Checkmark(false);
    bt->disabled(true);
    new Text("\033rBottom edge:");
    auto *bb = new Checkmark(p.drag_bottom);
    bg->end();
    auto *mg = new Group(Group::Vertical);
    mg->columns(2);
    new Text("\033rScreen drag key:");
    auto *meta = new Cycle({"Super", "Alt", "Ctrl"}, p.meta_drag);
    mg->end();
    dg->end();
    HVSpace();
    sp->end();
    // -- Menus
    auto *mp = VGroup();
    auto *mm = new Group(Group::Vertical, Frame_Group);
    mm->frame_title("Menus");
    mm->columns(2);
    new Text("\033rLook:");
    auto *look = new Cycle({"Classic", "3D"}, p.menu_look);
    new Text("\033rSticky:");
    auto *sticky = new Checkmark(p.menu_sticky);
    mm->end();
    HVSpace();
    mp->end();
    // -- Windows
    auto *wp = VGroup();
    auto *wg = new Group(Group::Vertical, Frame_Group);
    wg->frame_title("Windows");
    wg->columns(2);
    new Text("\033rOffscreen window moves:");
    auto *off = new Checkmark(p.offscreen);
    wg->end();
    HVSpace();
    wp->end();
    reg->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();
    (void)bl; (void)br; (void)bt;

    auto collect = [=] {
        IControl n;
        n.frontmost_default = front->on();
        n.drag_bottom = bb->on();
        n.meta_drag = meta->active();
        n.menu_look = look->active();
        n.menu_sticky = sticky->on();
        n.offscreen = off->on();
        return n;
    };
    auto *w = new lune::Window("IControl Preferences", root);
    w->id("ICONTROLPREFS");
    g_ic_editor = w;
    auto close = [w, standalone] {
        w->hide();
        Fl::delete_widget(w);
        g_ic_editor = nullptr;
        if (standalone) exit(0);
    };
    save->on_press = [=] { put(collect(), true); close(); };
    use->on_press = [=] { put(collect(), false); close(); };
    cancel->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace rambler
