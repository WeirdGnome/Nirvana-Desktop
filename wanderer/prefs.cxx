/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Wanderer's preferences and their editor (prefs.h).
*/
#include "prefs.h"
#include "zune.h"
#include "tools.h"

#include <FL/Fl.H>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace zune;

namespace wanderer {

static std::string prefs_file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/wanderer/wanderer.prefs";
}

WandererPrefs WandererPrefs::load() {
    WandererPrefs p;
    std::ifstream in(prefs_file());
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        if (k == "backdrop_image") { p.backdrop_image = line.substr(eq + 1); continue; }
        if (k == "drawer_image") { p.drawer_image = line.substr(eq + 1); continue; }
        int v = atoi(line.c_str() + eq + 1);
        if (k == "text_mode") p.text_mode = std::clamp(v, 0, 2);
        else if (k == "label_lines") p.label_lines = std::clamp(v, 1, 5);
        else if (k == "label_maxlen") p.label_maxlen = std::clamp(v, 4, 64);
        else if (k == "list_mode") p.list_mode = std::clamp(v, 0, 1);
        else if (k == "toolbar") p.toolbar = v;
        else if (k == "statusbar") p.statusbar = v;
        else if (k == "show_hidden") p.show_hidden = v;
        else if (k == "drawers_first") p.drawers_first = v;
        else if (k == "hspacing") p.hspacing = std::clamp(v, 0, 40);
        else if (k == "vspacing") p.vspacing = std::clamp(v, 0, 40);
        else if (k == "backdrop_mode") p.backdrop_mode = std::clamp(v, 0, 2);
        else if (k == "drawer_mode") p.drawer_mode = std::clamp(v, 0, 2);
    }
    return p;
}

void WandererPrefs::save() const {
    std::error_code ec;
    fs::create_directories(fs::path(prefs_file()).parent_path(), ec);
    std::ofstream out(prefs_file());
    out << "text_mode=" << text_mode << "\nlabel_lines=" << label_lines << "\nlabel_maxlen=" << label_maxlen
        << "\nlist_mode=" << list_mode << "\ntoolbar=" << toolbar << "\nstatusbar=" << statusbar
        << "\nshow_hidden=" << show_hidden << "\ndrawers_first=" << drawers_first << "\nhspacing=" << hspacing
        << "\nvspacing=" << vspacing << "\nbackdrop_image=" << backdrop_image << "\nbackdrop_mode=" << backdrop_mode
        << "\ndrawer_image=" << drawer_image << "\ndrawer_mode=" << drawer_mode << "\n";
}

// --------------------------------------------------------------------------------------------
// The editor: one window, groups with titles, Save / Use / Cancel
// --------------------------------------------------------------------------------------------
static Window *g_editor = nullptr;

static Text *label(const char *t) { return new Text(std::string("\033r") + t); }

void edit_prefs(const WandererPrefs &current, std::function<void(const WandererPrefs &)> apply) {
    if (g_editor) { g_editor->show(); return; }
    auto *root = VGroup();
    auto *columns = HGroup();
    // -- Icons
    auto *icons = new Group(Group::Vertical, Frame_Group);
    icons->frame_title("Icons");
    icons->columns(2);
    label("Label text:");
    auto *mode = new Cycle({"Outline", "Plain", "Shadow"}, current.text_mode);
    label("Label lines:");
    auto *lines = new Cycle({"1", "2", "3", "4", "5"}, current.label_lines - 1);
    label("Line length:");
    auto *maxlen = new String(std::to_string(current.label_maxlen), 3);
    label("Layout:");
    auto *layout = new Cycle({"Grid", "Free"}, current.list_mode);
    label("Spacing:");
    auto *spacing = new Cycle({"Small", "Normal", "Large"}, current.hspacing <= 3 ? 0 : current.hspacing >= 10 ? 2 : 1);
    icons->end();
    // -- Windows
    auto *wins = new Group(Group::Vertical, Frame_Group);
    wins->frame_title("Drawer Windows");
    wins->columns(2);
    label("Tool bar:");
    auto *tb = new Checkmark(current.toolbar);
    label("Status bar:");
    auto *sb = new Checkmark(current.statusbar);
    label("All files:");
    auto *hidden = new Checkmark(current.show_hidden);
    label("Drawers first:");
    auto *first = new Checkmark(current.drawers_first);
    VSpace();
    VSpace();
    wins->end();
    columns->end();
    // -- Background pictures
    auto *pics = new Group(Group::Vertical, Frame_Group);
    pics->frame_title("Background Pictures");
    pics->columns(4);
    label("Backdrop:");
    auto *bimg = new String(current.backdrop_image, 1024);
    bimg->weight(300);
    auto *bpick = new Button("?");
    bpick->fixed_width(16);
    auto *bmode = new Cycle({"Tiled", "Scaled", "Centred"}, current.backdrop_mode);
    label("Drawers:");
    auto *dimg = new String(current.drawer_image, 1024);
    dimg->weight(300);
    auto *dpick = new Button("?");
    dpick->fixed_width(16);
    auto *dmode = new Cycle({"Tiled", "Scaled", "Centred"}, current.drawer_mode);
    bpick->on_press = [bimg] { std::string p = bimg->value(); if (file_request("Choose the backdrop picture", p)) bimg->value(p); };
    dpick->on_press = [dimg] { std::string p = dimg->value(); if (file_request("Choose the drawers' picture", p)) dimg->value(p); };
    pics->end();
    // -- Save / Use / Cancel
    auto *buttons = HGroup();
    buttons->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    buttons->end();
    root->end();

    auto collect = [=] {
        WandererPrefs p;
        p.text_mode = mode->active();
        p.label_lines = lines->active() + 1;
        p.label_maxlen = std::clamp(atoi(maxlen->value()), 4, 64);
        p.list_mode = layout->active();
        int sp[] = {2, 5, 12};
        p.hspacing = p.vspacing = sp[spacing->active()];
        p.toolbar = tb->on();
        p.statusbar = sb->on();
        p.show_hidden = hidden->on();
        p.drawers_first = first->on();
        p.backdrop_image = bimg->value();
        p.backdrop_mode = bmode->active();
        p.drawer_image = dimg->value();
        p.drawer_mode = dmode->active();
        return p;
    };
    auto *w = new Window("Wanderer Preferences", root);
    g_editor = w;
    auto close = [w] { w->hide(); Fl::delete_widget(w); g_editor = nullptr; };
    save->on_press = [=] { auto p = collect(); p.save(); apply(p); close(); };
    use->on_press = [=] { apply(collect()); close(); };
    cancel->on_press = [=] { close(); };
    w->callback([](Fl_Widget *win, void *) { win->hide(); Fl::delete_widget(win); g_editor = nullptr; });
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    w->position(sx + (sw - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace wanderer
