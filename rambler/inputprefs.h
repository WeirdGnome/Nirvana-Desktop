/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Input preferences (workbench/prefs/input): the keyboard - its keymap (an XKB layout
    here), an alternative one and the key that switches to it, the key repeat rate and
    delay - and the mouse: its speed, acceleration, the double-click delay and a left-handed
    mouse. Kept in ~/.config/rambler/input.prefs and put into effect with xset, xmodmap and
    setxkbmap (as IPrefs does at the start of a session, and the editor's Use and Save).
*/
#pragma once
#include <string>

namespace rambler {

struct InputPrefs {
    std::string keymap, alt_keymap;   // XKB layouts ("" - as the system has it)
    bool switch_enable = false;       // the alternative keymap and its switch
    int switch_key = 0;               // 0 Alt+Shift, 1 Ctrl+Shift, 2 Caps Lock, 3 Super+Space, 4 Right Alt
    int repeat_rate = 10;             // 0..12: 0.02 * (12 - n) seconds between repeats (Prefs_Default 40 ms)
    int repeat_delay = 24;            // 0..74: 20 + 20 * n milliseconds (500 ms)
    int double_click = 24;            // 0..199: 0.02 + 0.02 * n seconds (0.5 s)
    int mouse_speed = 2;              // 0 slow, 1 normal, 2 fast (ip_PointerTicks 4, 2, 1)
    bool accelerated = true;
    bool left_handed = false;
    static bool exists();
    static InputPrefs load();
    void save() const;
    void apply() const;
};

// the editor (Save / Use / Cancel); standalone: the program ends when it closes
void edit_input_prefs(bool standalone);

}  // namespace rambler
