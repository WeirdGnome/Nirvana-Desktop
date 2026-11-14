/*
    Copyright (C) 2002-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).
*/
#include "pens.h"

#include <FL/fl_draw.H>

namespace zune {

static Pens g_pens;
static bool g_ready = false;

void set_palette(Fl_Color background, Fl_Color shadow, Fl_Color shine, Fl_Color fill) {
    g_pens.pen[MPEN_SHINE] = shine;
    g_pens.pen[MPEN_BACKGROUND] = background;
    g_pens.pen[MPEN_SHADOW] = shadow;
    // the window class: half way between the background and the shine / shadow pen
    g_pens.pen[MPEN_HALFSHINE] = fl_color_average(shine, background, 0.5f);
    g_pens.pen[MPEN_HALFSHADOW] = fl_color_average(shadow, background, 0.5f);
    g_pens.pen[MPEN_TEXT] = shadow;      // TEXTPEN: the second colour
    g_pens.pen[MPEN_FILL] = fill;        // FILLPEN: the fourth colour
    g_pens.pen[MPEN_MARK] = fill;
    g_ready = true;
}

const Pens &pens() {
    if (!g_ready)
        set_palette((Fl_Color)PALETTE_GREY, (Fl_Color)PALETTE_BLACK, (Fl_Color)PALETTE_WHITE,
                    (Fl_Color)PALETTE_BLUE);
    return g_pens;
}

}  // namespace zune
