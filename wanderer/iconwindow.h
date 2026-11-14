/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    IconListview (Wanderer/Classes/iconlistview.c): an IconList with its scrollers.
    IconWindow (Wanderer/iconwindow.c, panel_toolbar.c, panel_statusbar.c): a window
    showing a drawer or the volumes - its title is the location; a tool bar with the
    location, "parent" and "search" at the top, the view, a status bar at the bottom.
    The root window (the volumes) can be a backdrop: borderless, without a title, behind
    every other window.
*/
#pragma once
#include "iconlist.h"

#include <functional>
#include <string>

namespace wanderer {

class IconListview : public zune::Group {
public:
    explicit IconListview(IconList *list);
    IconList *list() const { return list_; }
    void update_props();
    void show_props(bool on);   // the backdrop has no scrollers

private:
    IconList *list_;
    zune::Prop *vprop_, *hprop_;
};

class IconWindow : public zune::Window {
public:
    // location: a directory; "" - the root window (the volumes)
    IconWindow(const std::string &location, bool backdrop = false, IconList *list = nullptr);
    const std::string &location() const { return location_; }
    IconList *iconlist() const { return list_; }
    bool is_root() const { return location_.empty(); }
    bool is_backdrop() const { return backdrop_; }
    void show_toolbar(bool on);
    void show_statusbar(bool on);
    void update();                 // re-read the contents
    void update_status();

    // what happens (Wanderer connects these)
    std::function<void(IconWindow *, IconEntry &)> on_open;
    std::function<void(IconWindow *, const std::string &)> on_location;   // the tool bar's location
    std::function<void(IconWindow *)> on_parent;
    std::function<void(IconWindow *)> on_search;
    std::function<void(IconWindow *)> on_activate;
    std::function<void(IconWindow *)> on_close;
    std::function<bool(IconWindow *)> on_shortcut;   // a menu shortcut pressed
    std::function<void(IconWindow *)> on_shown;      // mapped (the window has its X id)
    void show() override;
    int handle(int e) override;

private:
    std::string location_;
    bool backdrop_;
    IconList *list_ = nullptr;
    IconListview *view_ = nullptr;
    zune::Group *toolbar_ = nullptr, *statusbar_ = nullptr;
    zune::String *loc_string_ = nullptr;
    zune::Text *status_text_ = nullptr;
};

}  // namespace wanderer
