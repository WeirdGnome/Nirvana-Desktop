/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Clock utility (workbench/utilities/Clock): a window with the time - analog (the
    Clock class) or digital - and the date in its title. Its menus: Project (Analog,
    Digital, Quit), Settings (Seconds, Date, 24 Hour, Set Alarm..., Alarm, Save Settings).
    The alarm beeps (DisplayBeep, as the Sound preferences set it) and says so. The
    settings are saved in ~/.config/rambler/clock.prefs.
*/
#pragma once

namespace rambler {

void run_clock();

}  // namespace rambler
