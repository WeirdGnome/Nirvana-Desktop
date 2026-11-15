/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    MultiView (workbench/utilities/MultiView): shows a file as what it is - a text, a
    picture (PNG, JPEG, GIF, BMP, SVG, XPM...), anything else as a hex dump. Its menus:
    Project (Open..., Save As..., Print, About..., Quit), Edit (Mark: a drag marks lines,
    Copy, Select All, Clear Selected), Search (Find..., Find Next), View (Word Wrap for a
    text, Fit to Window for a picture). Without a file it asks for one. The arrow keys,
    Page Up/Down, Home and End and the mouse wheel move through it; a picture is also
    dragged.
*/
#pragma once
#include <string>

namespace rambler {

void run_multiview(const std::string &file);
// can MultiView show this file as a text or a picture? (Rambler opens such a file with it
// when the system has no program for it)
bool multiview_can_show(const std::string &file);

}  // namespace rambler
