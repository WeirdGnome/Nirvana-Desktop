/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The IControl preferences (workbench/prefs/icontrol): how the window manager (Intuition
    here) behaves - the screens (new windows on the front one, how far a screen is dragged
    down, the key that drags a screen from anywhere), the menus (classic or 3D, sticky) and
    the windows (moved partly off the screen or not). Use writes the settings in use
    (ENV:), Save also ~/.config/rambler/icontrol.prefs (ENVARC:); the window manager takes
    them up at once (the _RAMBLER_ICONTROL property of the root window).
*/
#pragma once

namespace rambler {

void edit_icontrol_prefs(bool standalone);

}  // namespace rambler
