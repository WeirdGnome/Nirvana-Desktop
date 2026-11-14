/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Wanderer's preferences (Wanderer/wandererprefs.c and the Wanderer preferences program):
    how the icon labels look, how icons are laid out, which bars a drawer window shows.
    Kept in ~/.config/wanderer/wanderer.prefs; the editor has Save, Use and Cancel like
    every preferences program (Use: for this session only).
*/
#pragma once
#include <functional>
#include <string>

namespace wanderer {

struct WandererPrefs {
    int text_mode = 0;           // 0 outline, 1 plain, 2 drop shadow
    int label_lines = 1;         // lines of a label (1..5)
    int label_maxlen = 10;       // characters of a line
    int list_mode = 0;           // 0 grid, 1 rough
    bool toolbar = true;         // drawer windows: the tool bar
    bool statusbar = true;       //                 the status bar
    bool show_hidden = false;    // all files
    bool drawers_first = true;
    int hspacing = 5, vspacing = 5;
    std::string backdrop_image, drawer_image;   // background pictures ("" - none)
    int backdrop_mode = 1, drawer_mode = 0;     // 0 tiled, 1 scaled, 2 centred

    static WandererPrefs load();
    void save() const;
};

// The editor; apply is called with the settings to use (Use or Save)
void edit_prefs(const WandererPrefs &current, std::function<void(const WandererPrefs &)> apply);

}  // namespace wanderer
