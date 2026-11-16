/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Appearance preferences (workbench/prefs/appearance): the decoration theme - on or
    off, which one (the drawers of THEMES:), its preview, its description - and the
    settings a theme brings with it (Env-Archive) for the other preferences: the GUI
    settings (Lune), Rambler's, the palette, the fonts, the pointer. Save writes theme.var
    (ENV: and ENVARC:) and those settings; the window manager takes the theme up at once
    (the original needs a restart). The compositing settings are shown as the original has
    them; screens on X are composed by the window manager, so they are not to be changed.
*/
#pragma once

#include <string>
#include <vector>

namespace rambler {

void edit_appearance_prefs(bool standalone);

// THEMES: the drawers the themes are looked for in (the user's first)
std::vector<std::string> theme_dirs();

}  // namespace rambler
