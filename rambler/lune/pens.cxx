/*
    Copyright (C) 2002-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).
*/
#include "pens.h"
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl.H>

#include <FL/fl_draw.H>

namespace lune {

static Pens g_pens;
static bool g_ready = false;

void set_palette(Fl_Color background, Fl_Color shadow, Fl_Color shine, Fl_Color fill, long text) {
    g_pens.pen[MPEN_SHINE] = shine;
    g_pens.pen[MPEN_BACKGROUND] = background;
    g_pens.pen[MPEN_SHADOW] = shadow;
    // the window class: half way between the background and the shine / shadow pen
    g_pens.pen[MPEN_HALFSHINE] = fl_color_average(shine, background, 0.5f);
    g_pens.pen[MPEN_HALFSHADOW] = fl_color_average(shadow, background, 0.5f);
    g_pens.pen[MPEN_TEXT] = text >= 0 ? (Fl_Color)text : shadow;   // TEXTPEN: the second colour, unless set
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


// --------------------------------------------------------------------------------------------
// Patterns (imspec.c patternPens: fg, bg and the grid)
// --------------------------------------------------------------------------------------------
const char *const PATTERN_NAMES[MUII_LASTPAT - MUII_BACKGROUND] = {
    "Background", "Shadow", "Shine", "Fill", "Shadow-Background", "Shadow-Fill", "Shadow-Shine",
    "Fill-Background", "Fill-Shine", "Shine-Background", "Fill-Background 2", "Halfshine-Background",
    "Halfshadow-Background", "Halfshine-Shine", "Halfshadow-Shadow", "Mark-Shine", "Mark-Halfshine",
    "Mark-Background"};

namespace {
struct MPattern { int fg, bg; const unsigned short *grid; };
const unsigned short grid1[] = {0x5555, 0xaaaa};
const unsigned short grid2[] = {0x4444, 0x1111};
const MPattern PATTERNS[] = {
    {MPEN_SHADOW, MPEN_BACKGROUND, grid1}, {MPEN_SHADOW, MPEN_FILL, grid1}, {MPEN_SHADOW, MPEN_SHINE, grid1},
    {MPEN_FILL, MPEN_BACKGROUND, grid1}, {MPEN_FILL, MPEN_SHINE, grid1}, {MPEN_SHINE, MPEN_BACKGROUND, grid1},
    {MPEN_FILL, MPEN_BACKGROUND, grid2}, {MPEN_HALFSHINE, MPEN_BACKGROUND, grid1},
    {MPEN_HALFSHADOW, MPEN_BACKGROUND, grid1}, {MPEN_HALFSHINE, MPEN_SHINE, grid1},
    {MPEN_HALFSHADOW, MPEN_SHADOW, grid1}, {MPEN_MARK, MPEN_SHINE, grid1}, {MPEN_MARK, MPEN_HALFSHINE, grid1},
    {MPEN_MARK, MPEN_BACKGROUND, grid1}};
}  // namespace

void draw_pattern(int muii, int X, int Y, int W, int H) {
    if (W <= 0 || H <= 0) return;
    const Pens &p = pens();
    if (muii < MUII_BACKGROUND || muii >= MUII_LASTPAT) muii = MUII_BACKGROUND;
    if (muii <= MUII_FILL) {
        static const int solid[] = {MPEN_BACKGROUND, MPEN_SHADOW, MPEN_SHINE, MPEN_FILL};
        fl_color(p[solid[muii - MUII_BACKGROUND]]);
        fl_rectf(X, Y, W, H);
        return;
    }
    // a 64 x 64 tile of the pattern for these pens (made again when the palette changes),
    // laid from the window's origin so that neighbouring areas meet
    const MPattern &m = PATTERNS[muii - MUII_SHADOWBACK];
    struct Tile { Fl_Color fg = 0, bg = 0; Fl_RGB_Image *img = nullptr; };
    static Tile tiles[MUII_LASTPAT - MUII_SHADOWBACK];
    Tile &t = tiles[muii - MUII_SHADOWBACK];
    if (!t.img || t.fg != p[m.fg] || t.bg != p[m.bg]) {
        delete t.img;
        t.fg = p[m.fg];
        t.bg = p[m.bg];
        auto *d = new uchar[64 * 64 * 3];
        uchar fr, fg_, fb, br, bg_, bb;
        Fl::get_color(t.fg, fr, fg_, fb);
        Fl::get_color(t.bg, br, bg_, bb);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                bool on = (m.grid[y & 1] >> (15 - (x & 15))) & 1;
                uchar *px = d + (y * 64 + x) * 3;
                px[0] = on ? fr : br; px[1] = on ? fg_ : bg_; px[2] = on ? fb : bb;
            }
        t.img = new Fl_RGB_Image(d, 64, 64, 3);
        t.img->alloc_array = 1;
    }
    fl_push_clip(X, Y, W, H);
    for (int y = Y - (Y & 63); y < Y + H; y += 64)
        for (int x = X - (X & 63); x < X + W; x += 64) t.img->draw(x, y);
    fl_pop_clip();
}

}  // namespace lune
