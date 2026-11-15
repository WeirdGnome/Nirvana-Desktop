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
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <unistd.h>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Image.H>

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

static FrameBorders custom_borders(int n);
static bool draw_custom(int n, bool down, int x, int y, int w, int h);

FrameBorders frame_borders(int type, bool down) {
    if (type >= FST_CUSTOM1 && type < FST_ALL) {
        if (custom_frame_loaded(type - FST_CUSTOM1)) return custom_borders(type - FST_CUSTOM1);
        type = FST_RECT;
    }
    if (type < 0 || type >= FST_COUNT) return {0, 0, 0, 0};
    return BORDERS[type][down ? 1 : 0];
}

void draw_frame(int type, bool down, int x, int y, int w, int h) {
    if (type >= FST_CUSTOM1 && type < FST_ALL) {
        if (draw_custom(type - FST_CUSTOM1, down, x, y, w, h)) return;
        type = FST_RECT;
    }
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
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;   // (a type past 'f': the custom frames)
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 0;
}

FrameSpec FrameSpec::parse(const std::string &s) {
    FrameSpec f;
    if (s.size() >= 1) f.type = hexdigit(s[0]);
    if (f.type >= FST_ALL) f.type = FST_NONE;
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

static const char *const DEFAULT_SPECS[Frame_Count] = {
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

const char *default_frame(StdFrame which) { return DEFAULT_SPECS[which < 0 || which >= Frame_Count ? 0 : which]; }

void set_std_frame(StdFrame which, const std::string &spec) {
    std_frame(which);   // the defaults first
    if (which >= 0 && which < Frame_Count) table[which] = FrameSpec::parse(spec);
}

const FrameSpec &std_frame(StdFrame which) {
    if (!ready) {
        for (int i = 0; i < Frame_Count; i++) table[i] = FrameSpec::parse(DEFAULT_SPECS[i]);
        ready = true;
    }
    return table[which < 0 || which >= Frame_Count ? 0 : which];
}



// ---------------------------------------------------------------------------------------------
// Custom frames (datatypescache.c ReadFrameConfig, frame.c draw_tile_frame)
// ---------------------------------------------------------------------------------------------
namespace {
struct CustomFrame {
    std::string config;
    int tile_left = 0, tile_top = 0, tile_right = 0, tile_bottom = 0;
    int inner_left = 0, inner_top = 0, inner_right = 0, inner_bottom = 0;
    Fl_RGB_Image *up = nullptr, *down = nullptr;
    bool pictures_pending = false;
};
CustomFrame g_custom[16];

Fl_RGB_Image *load_picture(const std::string &base) {
    static bool registered = false;
    if (!registered) { fl_register_images(); registered = true; }
    for (const char *ext : {"", ".png", ".ilbm", ".iff", ".bmp", ".gif", ".jpg"}) {
        std::string p = base + ext;
        if (access(p.c_str(), R_OK) != 0) continue;
        Fl_Shared_Image *si = Fl_Shared_Image::get(p.c_str());
        if (!si || si->w() <= 0) continue;
        // a copy of its own (as an RGB image the parts of it can be drawn)
        const Fl_Image *base = si->image() ? si->image() : si;
        Fl_Image *c = const_cast<Fl_Image *>(base)->copy();
        si->release();
        if (auto *rgb = dynamic_cast<Fl_RGB_Image *>(c)) return rgb;
        delete c;
    }
    return nullptr;
}

// DrawTileToImage: the source part repeated over the destination part
void tile(Fl_RGB_Image *img, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    fl_push_clip(dx, dy, dw, dh);
    for (int y = dy; y < dy + dh; y += sh)
        for (int x = dx; x < dx + dw; x += sw) img->draw(x, y, std::min(sw, dx + dw - x), std::min(sh, dy + dh - y), sx, sy);
    fl_pop_clip();
}
}  // namespace

void set_custom_frame(int n, const std::string &config) {
    if (n < 0 || n >= 16) return;
    CustomFrame &f = g_custom[n];
    if (f.config == config) return;
    delete f.up; delete f.down;
    f = CustomFrame();
    f.config = config;
    if (config.empty()) return;
    FILE *in = fopen(config.c_str(), "r");
    if (in) {
        char line[256];
        while (fgets(line, sizeof line, in)) {
            static const std::pair<const char *, int CustomFrame::*> keys[] = {
                {"TileLeft ", &CustomFrame::tile_left}, {"TileTop ", &CustomFrame::tile_top},
                {"TileRight ", &CustomFrame::tile_right}, {"TileBottom ", &CustomFrame::tile_bottom},
                {"InnerLeft ", &CustomFrame::inner_left}, {"InnerTop ", &CustomFrame::inner_top},
                {"InnerRight ", &CustomFrame::inner_right}, {"InnerBottom ", &CustomFrame::inner_bottom}};
            for (auto &[k, m] : keys)
                if (strncmp(line, k, strlen(k)) == 0) f.*m = atoi(line + strlen(k));
        }
        fclose(in);
    }
    f.pictures_pending = true;   // (read when first drawn: the display is open then)
}

bool custom_frame_loaded(int n) {
    if (n < 0 || n >= 16) return false;
    CustomFrame &f = g_custom[n];
    if (f.pictures_pending) {
        f.pictures_pending = false;
        std::string dir = f.config.substr(0, f.config.find_last_of('/') + 1);
        f.up = load_picture(dir + "up/default");
        f.down = load_picture(dir + "down/default");
        if (!f.up || !f.down) { delete f.up; delete f.down; f.up = f.down = nullptr; }
    }
    return f.up != nullptr;
}

static FrameBorders custom_borders(int n) {
    const CustomFrame &f = g_custom[n];
    return {f.inner_left, f.inner_right, f.inner_top, f.inner_bottom};
}

// draw_tile_frame: the corners as they are, the edges and the middle repeated
static bool draw_custom(int n, bool down, int x, int y, int w, int h) {
    if (!custom_frame_loaded(n) || w <= 0 || h <= 0) return false;
    const CustomFrame &f = g_custom[n];
    Fl_RGB_Image *src = down ? f.down : f.up;
    int lw = std::min(f.tile_left, w), rw = std::min(f.tile_right, w - lw);
    int th = std::min(f.tile_top, h), bh = std::min(f.tile_bottom, h - th);
    int mw = w - lw - rw, mh = h - th - bh;
    int smw = src->w() - f.tile_left - f.tile_right, smh = src->h() - f.tile_top - f.tile_bottom;
    int rp = src->w() - f.tile_right, bp = src->h() - f.tile_bottom;
    tile(src, 0, 0, lw, th, x, y, lw, th);
    tile(src, 0, bp, lw, bh, x, y + h - bh, lw, bh);
    tile(src, rp, 0, rw, th, x + w - rw, y, rw, th);
    tile(src, rp, bp, rw, bh, x + w - rw, y + h - bh, rw, bh);
    tile(src, f.tile_left, 0, smw, th, x + lw, y, mw, th);
    tile(src, f.tile_left, bp, smw, bh, x + lw, y + h - bh, mw, bh);
    tile(src, 0, f.tile_top, lw, smh, x, y + th, lw, mh);
    tile(src, rp, f.tile_top, rw, smh, x + w - rw, y + th, rw, mh);
    tile(src, f.tile_left, f.tile_top, smw, smh, x + lw, y + th, mw, mh);
    return true;
}

}  // namespace zune
