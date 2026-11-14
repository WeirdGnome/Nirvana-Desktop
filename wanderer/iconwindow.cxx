/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    IconListview and IconWindow (iconwindow.h). Ported from Wanderer/Classes/iconlistview.c,
    Wanderer/iconwindow.c, panel_toolbar.c and panel_statusbar.c. The tool bar's gadget
    pictures are drawn here (the theme's images are not used).
*/
#include "iconwindow.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using namespace zune;

namespace wanderer {

// ============================================================================================
// IconListview: the list with a vertical scroller on the right and a horizontal one below
// ============================================================================================
IconListview::IconListview(IconList *list) : Group(Vertical), list_(list) {
    spacing(0);
    auto *row = HGroup();
    row->spacing(0);
    row->add(list_);
    vprop_ = new Prop(false);
    row->end();
    hprop_ = new Prop(true);
    end();
    vprop_->on_change = [this](int f) { list_->scroll_to(list_->view_left(), f); };
    hprop_->on_change = [this](int f) { list_->scroll_to(f, list_->view_top()); };
    list_->on_view_changed = [this] { update_props(); };
}

void IconListview::update_props() {
    vprop_->set(std::max(list_->content_h(), list_->mheight()), std::max(1, list_->mheight()), list_->view_top());
    hprop_->set(std::max(list_->content_w(), list_->mwidth()), std::max(1, list_->mwidth()), list_->view_left());
}

// ============================================================================================
// The tool bar's pictures (DirUp, Search)
// ============================================================================================
static void draw_dirup(int x, int y, int w, int h, bool sel) {
    const Pens &p = pens();
    fl_color(sel ? p[MPEN_SHINE] : p[MPEN_TEXT]);
    int cx = x + w / 2, top = y + 3, bot = y + h - 4;
    fl_polygon(cx, top, cx + 6, top + 6, cx - 6, top + 6);   // an arrow up
    fl_rectf(cx - 2, top + 6, 4, bot - top - 6);
}

static void draw_search(int x, int y, int w, int h, bool sel) {
    const Pens &p = pens();
    fl_color(sel ? p[MPEN_SHINE] : p[MPEN_TEXT]);
    int r = std::min(w, h) / 2 - 3, cx = x + w / 2 - 2, cy = y + h / 2 - 2;
    fl_line_style(FL_SOLID, 2);
    fl_arc(cx - r, cy - r, 2 * r, 2 * r, 0, 360);   // a magnifier
    fl_line(cx + r - 1, cy + r - 1, x + w - 3, y + h - 3);
    fl_line_style(0);
}

// panel_statusbar.c FmtSizeToString: one decimal, rounded; "Bytes" without one
static std::string fmt_size(unsigned long long num) {
    static const char *units[] = {"GB", "MB", "KB", "Bytes"};
    unsigned long long div[] = {1ULL << 30, 1ULL << 20, 1ULL << 10, 1};
    for (int i = 0; i < 4; i++) {
        if (num >= div[i] || i == 3) {
            unsigned long long val = num / div[i], d = i == 3 ? 0 : (num * 10 + div[i] / 2) / div[i];
            unsigned dec = (unsigned)(d % 10);
            if (!dec && d > val * 10) val++;
            char b[64];
            if (dec) snprintf(b, sizeof b, "%llu.%u %s", val, dec, units[i]);
            else snprintf(b, sizeof b, "%llu %s", val, units[i]);
            return b;
        }
    }
    return "";
}

// ============================================================================================
// IconWindow
// ============================================================================================
IconWindow::IconWindow(const std::string &location, bool backdrop)
    : Window(location.empty() ? "Wanderer" : location), location_(location), backdrop_(backdrop) {
    // the window contents (OM_NEW): the top panel, the view, the bottom panel
    auto *root = new Group(Group::Vertical);
    root->spacing(0);
    {
        // the tool bar (panel_toolbar.c): the location string, "parent", "search"
        toolbar_ = HGroup();
        toolbar_->spacing(3);
        toolbar_->inner(2, 2, 2, 2);
        loc_string_ = new String(location_, 1024);
        loc_string_->weight(100);
        auto *up = new ImageButton(20, 18, draw_dirup);
        auto *search = new ImageButton(20, 18, draw_search);
        toolbar_->end();
        loc_string_->on_acknowledge = [this] { if (on_location) on_location(this, loc_string_->value()); };
        up->on_press = [this] { if (on_parent) on_parent(this); };
        search->on_press = [this] { if (on_search) on_search(this); };

        if (location_.empty()) list_ = new IconVolumeList();
        else list_ = new IconDrawerList(location_);
        Fl_Group::current(nullptr);
        root->begin();
        view_ = new IconListview(list_);
        view_->weight(100);

        // the status bar (panel_statusbar.c)
        statusbar_ = HGroup();
        statusbar_->inner(2, 2, 1, 1);
        status_text_ = new Text("", Frame_Text);
        status_text_->weight(100);
        statusbar_->end();
    }
    root->end();
    this->root(root);
    if (backdrop_ || is_root()) {
        show_toolbar(false);
        show_statusbar(false);
    }
    list_->on_open = [this](IconEntry &e) { if (on_open) on_open(this, e); };
    list_->on_selection_changed = [this] { update_status(); };
    size(520, 340);
}

void IconWindow::show_toolbar(bool on) {
    if (on) toolbar_->show(); else toolbar_->hide();
    resize(x(), y(), w(), h());
}

void IconWindow::show_statusbar(bool on) {
    if (on) statusbar_->show(); else statusbar_->hide();
    resize(x(), y(), w(), h());
}

void IconWindow::update() {
    list_->update();
    view_->update_props();
    update_status();
}

// " 12.3 KB in 4 files, 2 drawers (3 hidden)"
void IconWindow::update_status() {
    int files = 0, dirs = 0, hidden = 0;
    unsigned long long size = 0;
    for (auto &e : list_->entries()) {
        if (!e.visible) { hidden++; continue; }
        if (e.type == ST_USERDIR) dirs++;
        else { files++; size += (unsigned long long)e.size; }
    }
    std::string s;
    if (files > 0) s = " " + fmt_size(size) + " in " + std::to_string(files) + " files";
    if (dirs > 0) s += (s.empty() ? " " : ", ") + std::to_string(dirs) + " drawers";
    if (hidden > 0) s += " (" + std::to_string(hidden) + " hidden)";
    status_text_->contents(s);
}

void IconWindow::show() {
    Window::show();
    if (on_shown) on_shown(this);
}

int IconWindow::handle(int e) {
    if (e == FL_SHORTCUT && on_shortcut && on_shortcut(this)) return 1;
    if (e == FL_KEYBOARD && (Fl::event_state() & FL_META) && on_shortcut && on_shortcut(this)) return 1;
    if (e == FL_FOCUS || (e == FL_PUSH && on_activate)) {
        if (on_activate) on_activate(this);
    }
    if (e == FL_HIDE && on_close) {
        int r = Window::handle(e);
        return r;
    }
    return Window::handle(e);
}

}  // namespace wanderer
