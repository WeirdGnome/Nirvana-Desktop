/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Rambler's preferences (Wanderer/wandererprefs.c and the Rambler preferences program):
    how the icon labels look, how icons are laid out, which bars a drawer window shows.
    Kept in ~/.config/rambler/rambler.prefs; the editor has Save, Use and Cancel like
    every preferences program (Use: for this session only).
*/
#pragma once
#include <functional>
#include <string>

namespace rambler {

struct RamblerPrefs {
    int text_mode = 0;           // 0 outline, 1 plain, 2 drop shadow
    int label_lines = 1;         // lines of a label (1..5)
    int label_maxlen = 10;       // characters of a line
    int icon_font = -1;          // -1 the screen font, 0 sans, 1 serif, 2 fixed
    int icon_font_size = 0;      // 0 - the screen font's
    bool lines_on_focus = false; // all the lines only on the focused icon
    int list_mode = 0;           // 0 grid, 1 rough
    bool toolbar = true;         // drawer windows: the tool bar
    bool statusbar = true;       //                 the status bar
    bool show_hidden = false;    // all files
    bool drawers_first = true;
    bool auto_sort = false;      // new drawer windows sort their icons (no free positions)
    int default_sort = 0;        // a drawer's order without a snapshot: 0 name, 1 date, 2 size
    int hspacing = 5, vspacing = 5;
    // the label's padding and frame (ILC_ICONLABEL_*_DEFAULT) and the image-label space
    int image_spacing = 1, label_hpad = 4, label_vpad = 4, label_border_w = 1, label_border_h = 1;
    int drop_mode = 0;           // icons dropped: 0 moved within a volume (else copied), 1 always copied
    bool show_network = true;    // the Network Access icon on the root window
    int navigation = 0;          // a drawer opens in: 0 a new window (classic), 1 the same window (enhanced)
    std::string backdrop_image, drawer_image;   // background pictures ("" - none)
    int backdrop_mode = 1, drawer_mode = 0;
    int backdrop_ox = 0, backdrop_oy = 0, drawer_ox = 0, drawer_oy = 0;   // the tiles' offsets     // 0 tiled, 1 scaled, 2 centred, 3 fixed tiles
    // The user screen title: %wv / %wb Rambler version, %ov OS version, %os OS release,
    // %pc available, %pf free, %pt total memory ("" - the default title)
    std::string screen_title;
    std::string icon_font_name;   // the Font preferences' icon font ("" - icon_font's family)

    static RamblerPrefs load();
    void save(bool archive = true) const;   // Use: false (ENV: only), Save: true (and ENVARC:)
    std::string text() const;               // as written to the file
    static std::string in_use_file();       // ENV:'s copy (watched by a running Rambler)
};

// The editor; apply is called with the settings to use (Use or Save)
void edit_prefs(const RamblerPrefs &current, std::function<void(const RamblerPrefs &)> apply,
                bool standalone = false);

}  // namespace rambler
