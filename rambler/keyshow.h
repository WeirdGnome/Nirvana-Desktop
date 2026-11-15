/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    KeyShow (workbench/tools/KeyShow): the keyboard drawn with what each key types in the
    keymap in use. The keys held down show pressed, and with Shift or AltGr held the keys
    show that level; a click on Shift, AltGr, Ctrl or Alt latches it (a click again lets it
    go), to look at a level without holding the key.
*/
#pragma once

namespace rambler {

void run_keyshow();

}  // namespace rambler
