/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Pointer preferences (workbench/prefs/pointer): the mouse pointer - here an X cursor
    theme and its size, shown as the Normal and the Busy pointer. Use writes the settings in
    use for the session (ENV:), Save also ~/.config/rambler/pointer.prefs (ENVARC:); they go
    into the X resources (Xcursor.theme, Xcursor.size), the root window's pointer and the
    environment of the programs started from then on (the session exports the saved ones).
*/
#pragma once
#include <string>

namespace rambler {

struct PointerPrefs {
    std::string theme;   // "" - the system's
    int size = 0;        // 0 - the system's
    static PointerPrefs load();      // the settings in use (ENV:), else the saved ones
    void save(bool archive) const;   // Use: false, Save: true
    void apply() const;
    static std::string in_use_file();
};

void edit_pointer_prefs(bool standalone);

}  // namespace rambler
