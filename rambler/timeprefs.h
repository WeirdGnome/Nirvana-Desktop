/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Time preferences (workbench/prefs/time): the date in a calendar, the time on a
    clock. Use sets the system's clock, Save also the battery clock (the computer's clock,
    RTC). On Linux this is timedatectl (or date and hwclock through pkexec); the system
    asks for the right to do it. The time can also be set automatically from the network
    (NTP), then the clock only shows it.
*/
#pragma once

namespace rambler {

void edit_time_prefs(bool standalone);

}  // namespace rambler
