/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The commodities (workbench/tools/commodities) and Exchange, which lists and controls
    them. Here they are built into the session rather than programs of their own:
      ClickToFront  a double click (or a click with a key held) brings a window to the front
      AutoPoint     the window under the pointer becomes the active one
      Blanker       the screen goes blank after some minutes without input (X's screen saver)
      NoCapsLock    the Caps Lock key does not lock
    Exchange switches each on and off (Active / Inactive) and shows the interface of those
    that have settings; a change takes effect at once and is kept (ENV: and ENVARC:
    commodities.prefs); the window manager takes ClickToFront and AutoPoint from the
    _RAMBLER_COMMODITIES property of the root window.
*/
#pragma once
#include <string>

namespace rambler {

struct Commodities {
    bool click_to_front = false;
    int ctf_key = 0;               // 0 a double click, 1 Alt, 2 Ctrl, 3 Shift held with a click
    bool autopoint = false;
    bool blanker = false;
    int blank_minutes = 10;
    bool no_caps_lock = false;

    static Commodities load();
    std::string text() const;
    void save() const;             // ENV: and ENVARC:, and the window manager's property
    void apply() const;            // the X settings (the blanker, Caps Lock)
};

void run_exchange();

}  // namespace rambler
