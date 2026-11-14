/*
    Copyright (C) 2002-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Zune pens (MPEN_*) and the default screen palette. Ported from
    workbench/libs/muimaster (muimaster_init-aros.c, classes/window.c) and
    rom/intuition/intuition_init.c.
*/
#pragma once
#include <FL/Enumerations.H>

namespace zune {

enum MPen {
    MPEN_SHINE,
    MPEN_HALFSHINE,
    MPEN_BACKGROUND,
    MPEN_HALFSHADOW,
    MPEN_SHADOW,
    MPEN_TEXT,
    MPEN_FILL,
    MPEN_MARK,
    MPEN_COUNT
};

// The screen's colours: the four-colour default palette of the screen. HALFSHINE and
// HALFSHADOW are not system pens: the window class computes them between the background
// and SHINE / SHADOW when no pen is set for them.
struct Pens {
    Fl_Color pen[MPEN_COUNT];
    Fl_Color operator[](int i) const { return pen[i]; }
};

const Pens &pens();
void set_palette(Fl_Color background, Fl_Color shadow, Fl_Color shine, Fl_Color fill);

// The default palette (intuition_init.c): grey, black, white, blue
constexpr unsigned PALETTE_GREY = 0xAAAAAA00, PALETTE_BLACK = 0x00000000, PALETTE_WHITE = 0xFFFFFF00,
                   PALETTE_BLUE = 0x6688BB00;

}  // namespace zune
