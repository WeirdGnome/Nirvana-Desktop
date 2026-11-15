/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Font preferences (workbench/prefs/font): the icons' font (Rambler's settings), the
    screen's text font and the system's fixed-width text font (Lune's settings), each a font
    of the system and a size chosen in the font requester. Use writes the settings in use
    (ENV:), Save also the saved ones (ENVARC:); a running Rambler takes them up at once.
*/
#pragma once

namespace rambler {

void edit_font_prefs(bool standalone);

}  // namespace rambler
