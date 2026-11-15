/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The ScreenMode preferences (workbench/prefs/screenmode): the display mode of the screen
    (an output's mode and refresh rate here), its size - larger than the visible part with
    autoscroll (xrandr panning) - kept in ~/.config/rambler/screenmode.prefs and put into
    effect with xrandr (at the start of a session, and by the editor's Test, Use and Save).
*/
#pragma once
#include <string>
#include <vector>

namespace rambler {

struct ScreenModePrefs {
    std::string output;      // "" - the first connected output
    std::string mode;        // "1920x1080"; "" - as the system has it
    std::string rate;        // "60.00"; "" - the mode's first rate
    int width = 0, height = 0;   // the screen's size; 0 - the mode's (Default)
    bool autoscroll = false;     // a larger screen scrolls with the pointer (panning)
    static bool exists();
    static ScreenModePrefs load();
    void save() const;
    void apply() const;
};

// a display mode as xrandr lists it
struct DisplayMode {
    std::string output, name, rate;
    int w = 0, h = 0;
    bool current = false, preferred = false;
};
std::vector<DisplayMode> display_modes();

// the editor (Test / Save / Use / Cancel); standalone: the program ends when it closes
void edit_screenmode_prefs(bool standalone);

}  // namespace rambler
