/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Printer preferences (workbench/prefs/printer): which printer prints and how - the
    paper (size, type, source), the orientation, both sides, colour, resolution and
    quality. On Linux the printers are CUPS's: the list and the settings come from
    lpstat and lpoptions, and they are kept as the user's own (lpoptions, ~/.cups) - no
    rights asked. The printers themselves are set up with the system's printer tool (or
    CUPS's web pages). A test page shows that the printer works.
*/
#pragma once

namespace rambler {

void edit_printer_prefs(bool standalone);

}  // namespace rambler
