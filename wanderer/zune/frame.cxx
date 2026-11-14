/*
    Copyright (C) 2002-2026, The AROS Development Team.
    Copyright (C) 1999, David Le Corfec.
    All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Ported from workbench/libs/muimaster/frame.c (the built-in frames) and the
    default frame specs of classes/configdata.c.
*/
#include "frame.h"
#include "pens.h"

#include <FL/fl_draw.H>

#include <cstdlib>

namespace zune {

// ---------------------------------------------------------------------------------------------
// Primitives: Zune draws with RectFill / WritePixel in pens; inclusive coordinates.
// ---------------------------------------------------------------------------------------------
static void pen(MPen p) { fl_color(pens()[p]); }
static void rectfill(int x1, int y1, int x2, int y2) {
    if (x2 < x1 || y2 < y1) return;
    fl_rectf(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
}
static void pixel(int x, int y) { fl_point(x, y); }

static void rect_draw(int left, int top, int width, int height, MPen p) {
    if (width <= 0 || height <= 0) return;
    pen(p);
    if (width == 1 || height == 1) { rectfill(left, top, left + width - 1, top + height - 1); return; }
    rectfill(left, top, left + width - 1, top);
    rectfill(left, top + height - 1, left + width - 1, top + height - 1);
    rectfill(left, top + 1, left, top + height - 2);
    rectfill(left + width - 1, top + 1, left + width - 1, top + height - 2);
}

// A bicolour rectangle: ul pen on the top and left edges, lr pen on the bottom and right;
// the two "odd" corners take the background pen.
static void button_draw(int left, int top, int width, int height, MPen ul, MPen lr) {
    if (width <= 2 || height <= 2) return;
    pen(ul);
    rectfill(left, top, left, top + height - 2);
    rectfill(left, top, left + width - 2, top);
    pen(lr);
    rectfill(left + width - 1, top, left + width - 1, top + height - 1);
    rectfill(left, top + height - 1, left + width - 1, top + height - 1);
    pen(MPEN_BACKGROUND);
    pixel(left, top + height - 1);
    pixel(left + width - 1, top);
}

// 3 : FST_THIN_BORDER (and 9 : FST_GRAY_BORDER with the half pens)
static void thinborder_draw(int left, int top, int width, int height, MPen ul, MPen lr) {
    if (width <= 3 || height <= 3) return;
    pen(ul);
    rectfill(left, top, left, top + height - 1);
    rectfill(left, top, left + width - 1, top);
    rectfill(left + width - 2, top + 2, left + width - 2, top + height - 2);
    rectfill(left + 2, top + height - 2, left + width - 2, top + height - 2);
    rect_draw(left + 1, top + 1, width - 1, height - 1, lr);
}

// 4 : FST_THICK_BORDER
static void thickborder_draw(int left, int top, int width, int height, bool raised) {
    if (width <= 4 || height <= 4) return;
    if (raised) {
        button_draw(left, top, width, height, MPEN_SHINE, MPEN_SHADOW);
        button_draw(left + 2, top + 2, width - 4, height - 4, MPEN_SHADOW, MPEN_SHINE);
    } else {
        button_draw(left, top, width, height, MPEN_SHADOW, MPEN_SHINE);
        button_draw(left + 2, top + 2, width - 4, height - 4, MPEN_SHINE, MPEN_SHADOW);
    }
    rect_draw(left + 1, top + 1, width - 2, height - 2, MPEN_BACKGROUND);
}

// 5 : FST_ROUND_BEVEL
static void round_bevel_draw(int left, int top, int width, int height, MPen ul, MPen lr) {
    if (width <= 8 || height <= 4) return;
    pen(MPEN_BACKGROUND);
    rectfill(left, top, left + 3, top + height - 1);
    rectfill(left + width - 4, top, left + width - 1, top + height - 1);
    pen(ul);
    rectfill(left, top + 2, left + 1, top + height - 3);
    rectfill(left + 1, top + 1, left + 2, top + 1);
    rectfill(left + 1, top + height - 2, left + 2, top + height - 2);
    rectfill(left + 2, top + height - 1, left + 2, top + height - 1);
    rectfill(left + 2, top, left + width - 6, top);
    pen(lr);
    rectfill(left + width - 2, top + 2, left + width - 1, top + height - 3);
    rectfill(left + width - 3, top + 1, left + width - 2, top + 1);
    rectfill(left + width - 3, top + height - 2, left + width - 2, top + height - 2);
    rectfill(left + 3, top + height - 1, left + width - 6, top + height - 1);
    rectfill(left + width - 3, top, left + width - 3, top);
}

// 6 : FST_WIN_BEVEL
static void win_bevel_draw(int left, int top, int width, int height, bool down) {
    if (!down) {
        if (width <= 2 || height <= 2) return;
        rect_draw(left, top, width, height, MPEN_SHADOW);
        button_draw(left + 1, top + 1, width - 2, height - 2, MPEN_SHINE, MPEN_HALFSHADOW);
    } else {
        if (width <= 4 || height <= 4) return;
        rect_draw(left, top, width, height, MPEN_SHADOW);
        button_draw(left + 1, top + 1, width - 1, height - 1, MPEN_HALFSHADOW, MPEN_SHADOW);
        button_draw(left + 2, top + 2, width - 2, height - 2, MPEN_BACKGROUND, MPEN_SHADOW);
    }
}

// 7 : FST_ROUND_THICK_BORDER
static void round_thick_border_draw(int left, int top, int width, int height, MPen p1, MPen p2, MPen p3,
                                    MPen p4, MPen p5) {
    rect_draw(left, top, width - 2, height - 2, p1);
    rect_draw(left + 2, top + 2, width - 2, height - 2, p5);
    rect_draw(left, top, 2, 2, p2);
    rect_draw(left, top + height - 3, 2, 2, p2);
    rect_draw(left + width - 3, top, 2, 2, p2);
    rect_draw(left + width - 2, top + height - 2, 2, 2, p4);
    rect_draw(left + 1, top + height - 2, 2, 2, p4);
    rect_draw(left + width - 2, top + 1, 2, 2, p4);
    rect_draw(left + 1, top + 1, width - 2, height - 2, p3);
    rect_draw(left + 2, top + 2, 2, 2, p3);
    rect_draw(left + 2, top + height - 4, 2, 2, p3);
    rect_draw(left + width - 4, top + height - 4, 2, 2, p3);
    rect_draw(left + width - 4, top + 2, 2, 2, p3);
    pen(p5);
    pixel(left + 3, top + 3);
    pen(p1);
    pixel(left + width - 4, top + height - 4);
}

// 8 : FST_ROUND_THIN_BORDER
static void round_thin_border_draw(int left, int top, int width, int height, MPen p1, MPen p2, MPen p3,
                                   MPen p4, MPen p5) {
    if (width <= 6 || height <= 6) return;
    rect_draw(left, top, width - 1, height - 1, p1);
    rect_draw(left + 1, top + 1, width - 1, height - 1, p5);
    pen(p2);
    rectfill(left, top, left + 1, top + 1);
    rectfill(left + width - 4, top + height - 4, left + width - 3, top + height - 3);
    pixel(left, top + height - 2);
    pixel(left + 2, top + height - 2);
    pixel(left + width - 2, top);
    pixel(left + width - 2, top + 2);
    pen(p3);
    rectfill(left + 1, top + 1, left + 2, top + 2);
    rectfill(left + width - 3, top + height - 3, left + width - 2, top + height - 2);
    pen(p4);
    rectfill(left + 2, top + 2, left + 3, top + 3);
    rectfill(left + width - 2, top + height - 2, left + width - 1, top + height - 1);
    rectfill(left + 1, top + height - 3, left + 1, top + height - 1);
    rectfill(left + width - 3, top + 1, left + width - 1, top + 1);
    pixel(left + 2, top + height - 3);
    pixel(left + width - 3, top + 2);
}

// A : FST_SEMIROUND_BEVEL
static void semiround_bevel_draw(int left, int top, int width, int height, MPen p1, MPen p2, MPen p3, MPen p4,
                                 MPen p5) {
    button_draw(left, top, width, height, p1, p5);
    button_draw(left + 1, top + 1, width - 2, height - 2, p2, p4);
    pen(p2); pixel(left, top);
    pen(p1); pixel(left + 1, top + 1);
    pen(p5); pixel(left + width - 2, top + height - 2);
    pen(p4); pixel(left + width - 1, top + height - 1);
    pen(p3);
    pixel(left, top + height - 2);
    pixel(left + 1, top + height - 1);
    pixel(left + width - 2, top);
    pixel(left + width - 1, top + 1);
}

// B : FST_ROUNDED - a one pixel line with round corners of radius 8 in the half shadow pen
static void corner(int cx, int cy, int r, int quadrant) {
    int x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        if (quadrant & 1) { pixel(cx + x, cy - y); pixel(cx + y, cy - x); }
        if (quadrant & 2) { pixel(cx - x, cy - y); pixel(cx - y, cy - x); }
        if (quadrant & 4) { pixel(cx - x, cy + y); pixel(cx - y, cy + x); }
        if (quadrant & 8) { pixel(cx + x, cy + y); pixel(cx + y, cy + x); }
        if (d > 0) { y--; d += 4 * (x - y) + 10; } else d += 4 * x + 6;
        x++;
    }
}
static void rounded_draw(int left, int top, int width, int height) {
    const int r = 8;
    if (width <= r * 2 + 4 || height <= r * 2 + 4) return;
    pen(MPEN_HALFSHADOW);
    rectfill(left + r, top, left + width - r - 1, top);
    rectfill(left, top + r, left, top + height - r - 1);
    rectfill(left + r, top + height - 1, left + width - r - 1, top + height - 1);
    rectfill(left + width - 1, top + r, left + width - 1, top + height - r - 1);
    corner(left + r, top + r, r, 2);
    corner(left + width - r - 1, top + r, r, 1);
    corner(left + r, top + height - r - 1, r, 4);
    corner(left + width - r - 1, top + height - r - 1, r, 8);
}

// ---------------------------------------------------------------------------------------------
// The table: the frame's border on each side (left, right, top, bottom), up and down state
// ---------------------------------------------------------------------------------------------
static const FrameBorders BORDERS[FST_COUNT][2] = {
    {{0, 0, 0, 0}, {0, 0, 0, 0}},   // NONE
    {{1, 1, 1, 1}, {1, 1, 1, 1}},   // RECT
    {{1, 1, 1, 1}, {1, 1, 1, 1}},   // BEVEL
    {{2, 2, 2, 2}, {2, 2, 2, 2}},   // THIN_BORDER
    {{3, 3, 3, 3}, {3, 3, 3, 3}},   // THICK_BORDER
    {{4, 4, 1, 1}, {4, 4, 1, 1}},   // ROUND_BEVEL
    {{2, 2, 2, 2}, {3, 1, 3, 1}},   // WIN_BEVEL
    {{4, 4, 4, 4}, {4, 4, 4, 4}},   // ROUND_THICK_BORDER
    {{4, 4, 4, 4}, {4, 4, 4, 4}},   // ROUND_THIN_BORDER
    {{2, 2, 2, 2}, {2, 2, 2, 2}},   // GRAY_BORDER
    {{2, 2, 2, 2}, {2, 2, 2, 2}},   // SEMIROUND_BEVEL
    {{2, 2, 2, 2}, {2, 2, 2, 2}},   // ROUNDED
};

FrameBorders frame_borders(int type, bool down) {
    if (type < 0 || type >= FST_COUNT) return {0, 0, 0, 0};
    return BORDERS[type][down ? 1 : 0];
}

void draw_frame(int type, bool down, int x, int y, int w, int h) {
    switch (type) {
    case FST_RECT: rect_draw(x, y, w, h, down ? MPEN_SHADOW : MPEN_SHINE); break;
    case FST_BEVEL:
        if (down) button_draw(x, y, w, h, MPEN_SHADOW, MPEN_SHINE);
        else button_draw(x, y, w, h, MPEN_SHINE, MPEN_SHADOW);
        break;
    case FST_THIN_BORDER:
        if (down) thinborder_draw(x, y, w, h, MPEN_SHADOW, MPEN_SHINE);
        else thinborder_draw(x, y, w, h, MPEN_SHINE, MPEN_SHADOW);
        break;
    case FST_THICK_BORDER: thickborder_draw(x, y, w, h, !down); break;
    case FST_ROUND_BEVEL:
        if (down) round_bevel_draw(x, y, w, h, MPEN_SHADOW, MPEN_SHINE);
        else round_bevel_draw(x, y, w, h, MPEN_SHINE, MPEN_SHADOW);
        break;
    case FST_WIN_BEVEL: win_bevel_draw(x, y, w, h, down); break;
    case FST_ROUND_THICK_BORDER:
        if (down) round_thick_border_draw(x, y, w, h, MPEN_SHADOW, MPEN_HALFSHADOW, MPEN_BACKGROUND, MPEN_HALFSHINE, MPEN_SHINE);
        else round_thick_border_draw(x, y, w, h, MPEN_SHINE, MPEN_HALFSHINE, MPEN_BACKGROUND, MPEN_HALFSHADOW, MPEN_SHADOW);
        break;
    case FST_ROUND_THIN_BORDER:
        if (down) round_thin_border_draw(x, y, w, h, MPEN_SHADOW, MPEN_HALFSHADOW, MPEN_BACKGROUND, MPEN_HALFSHINE, MPEN_SHINE);
        else round_thin_border_draw(x, y, w, h, MPEN_SHINE, MPEN_HALFSHINE, MPEN_BACKGROUND, MPEN_HALFSHADOW, MPEN_SHADOW);
        break;
    case FST_GRAY_BORDER:
        if (down) thinborder_draw(x, y, w, h, MPEN_HALFSHADOW, MPEN_HALFSHINE);
        else thinborder_draw(x, y, w, h, MPEN_HALFSHINE, MPEN_HALFSHADOW);
        break;
    case FST_SEMIROUND_BEVEL:
        if (down) semiround_bevel_draw(x, y, w, h, MPEN_SHADOW, MPEN_HALFSHADOW, MPEN_BACKGROUND, MPEN_HALFSHINE, MPEN_SHINE);
        else semiround_bevel_draw(x, y, w, h, MPEN_SHINE, MPEN_HALFSHINE, MPEN_BACKGROUND, MPEN_HALFSHADOW, MPEN_SHADOW);
        break;
    case FST_ROUNDED: rounded_draw(x, y, w, h); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------------------------
// Specs
// ---------------------------------------------------------------------------------------------
static int hexdigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

FrameSpec FrameSpec::parse(const std::string &s) {
    FrameSpec f;
    if (s.size() >= 1) f.type = hexdigit(s[0]);
    if (f.type >= FST_COUNT) f.type = FST_NONE;
    if (s.size() >= 2) f.down = s[1] == '1';
    if (s.size() >= 6) {
        f.inner_left = hexdigit(s[2]);
        f.inner_right = hexdigit(s[3]);
        f.inner_top = hexdigit(s[4]);
        f.inner_bottom = hexdigit(s[5]);
    }
    return f;
}

// configdata.c: the default frame of every standard frame kind
static FrameSpec table[Frame_Count];
static bool ready = false;

void set_std_frame(StdFrame which, const std::string &spec) {
    std_frame(which);   // the defaults first
    if (which >= 0 && which < Frame_Count) table[which] = FrameSpec::parse(spec);
}

const FrameSpec &std_frame(StdFrame which) {
    if (!ready) {
        static const char *specs[Frame_Count] = {
            "000000",   // None
            "202211",   // Button
            "202211",   // ImageButton
            "212211",   // Text
            "302211",   // String
            "212211",   // ReadList
            "202211",   // InputList
            "202211",   // Prop
            "210000",   // Gauge
            "314444",   // Group
            "112211",   // PopUp
            "212211",   // Virtual
            "400000",   // Slider
            "202211",   // Knob
            "300000",   // Drag
        };
        for (int i = 0; i < Frame_Count; i++) table[i] = FrameSpec::parse(specs[i]);
        ready = true;
    }
    return table[which < 0 || which >= Frame_Count ? 0 : which];
}

}  // namespace zune
