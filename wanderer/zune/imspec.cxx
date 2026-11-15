/*
    Copyright (C) 2002-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Image specs: see imspec.h.
*/
#include "imspec.h"
#include "pens.h"

#include <FL/Fl.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/fl_draw.H>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include <unistd.h>

namespace zune {

namespace {
unsigned hex32_to8(const char *&s) {
    char *end;
    unsigned long v = strtoul(s, &end, 16);
    s = end;
    return (unsigned)((v >> 24) & 255);
}

bool parse_rgb(const char *s, unsigned &rgb) {
    const char *p = s;
    unsigned r = hex32_to8(p);
    if (*p != ',') return false;
    p++;
    unsigned g = hex32_to8(p);
    if (*p != ',') return false;
    p++;
    unsigned b = hex32_to8(p);
    rgb = (r << 16) | (g << 8) | b;
    return true;
}

std::string rgb_str(unsigned rgb) {
    char b[40];
    snprintf(b, sizeof b, "%08x,%08x,%08x", ((rgb >> 16) & 255) * 0x01010101u, ((rgb >> 8) & 255) * 0x01010101u,
             (rgb & 255) * 0x01010101u);
    return b;
}

Fl_Color fl_rgb(unsigned rgb) { return fl_rgb_color((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
}  // namespace

ImSpec ImSpec::parse(const std::string &s) {
    ImSpec i;
    if (s.empty()) return i;
    if (s.size() < 2 || s[1] != ':') {   // a bare number: a pattern (older settings)
        i.type = IMS_PATTERN;
        i.pattern = atoi(s.c_str());
        return i;
    }
    const char *v = s.c_str() + 2;
    switch (s[0]) {
    case '0': i.type = IMS_PATTERN; i.pattern = atoi(v); break;
    case '2':
        if (v[0] == 'm') { i.type = IMS_PEN; i.pen = atoi(v + 1); }
        else if (v[0] == 'r' && parse_rgb(v + 1, i.rgb)) i.type = IMS_RGB;
        break;
    case '5': i.type = IMS_BITMAP; i.file = v; break;
    case '7': case '8': {
        const char *p = v;
        if (*p == 'h' || *p == 'H') { i.angle = 90; p++; }
        else if (*p == 'v' || *p == 'V') { i.angle = 0; p++; }
        else { char *e; i.angle = (int)strtol(p, &e, 10); p = e; }
        if (*p != ',') break;
        p++;
        std::string rest(p);
        auto dash = rest.find('-');
        if (dash == std::string::npos) break;
        if (parse_rgb(rest.substr(0, dash).c_str(), i.rgb) && parse_rgb(rest.substr(dash + 1).c_str(), i.rgb2))
            i.type = IMS_GRADIENT;
        break;
    }
    }
    return i;
}

std::string ImSpec::str() const {
    switch (type) {
    case IMS_PATTERN: return "0:" + std::to_string(pattern);
    case IMS_PEN: return "2:m" + std::to_string(pen);
    case IMS_RGB: return "2:r" + rgb_str(rgb);
    case IMS_BITMAP: return "5:" + file;
    case IMS_GRADIENT: return "7:" + std::to_string(angle) + "," + rgb_str(rgb) + "-" + rgb_str(rgb2);
    default: return "";
    }
}

unsigned penspec_rgb(const std::string &pen) {
    if (!pen.empty() && pen[0] == 'r') {
        unsigned rgb;
        if (parse_rgb(pen.c_str() + 1, rgb)) return rgb;
    }
    int n = pen.size() > 1 ? atoi(pen.c_str() + 1) : 2;
    uchar r, g, b;
    Fl::get_color(pens()[std::max(0, std::min(MPEN_COUNT - 1, n))], r, g, b);
    return ((unsigned)r << 16) | ((unsigned)g << 8) | b;
}

void draw_imspec(const std::string &spec, int x, int y, int w, int h) { draw_imspec(ImSpec::parse(spec), x, y, w, h); }

void draw_imspec(const ImSpec &s, int X, int Y, int W, int H) {
    if (W <= 0 || H <= 0) return;
    switch (s.type) {
    case IMS_PATTERN: draw_pattern(s.pattern, X, Y, W, H); return;
    case IMS_PEN: fl_color(pens()[std::max(0, std::min(MPEN_COUNT - 1, s.pen))]); fl_rectf(X, Y, W, H); return;
    case IMS_RGB: fl_color(fl_rgb(s.rgb)); fl_rectf(X, Y, W, H); return;
    case IMS_BITMAP: {
        // the picture, read once, repeated from the window's origin (as the patterns)
        static std::map<std::string, Fl_Image *> cache;
        auto it = cache.find(s.file);
        if (it == cache.end()) {
            static bool registered = false;
            if (!registered) { fl_register_images(); registered = true; }
            Fl_Image *img = nullptr;
            if (access(s.file.c_str(), R_OK) == 0) {
                if (Fl_Shared_Image *si = Fl_Shared_Image::get(s.file.c_str())) {
                    if (si->w() > 0) img = (si->image() ? si->image() : si)->copy();
                    si->release();
                }
            }
            it = cache.emplace(s.file, img).first;
        }
        Fl_Image *img = it->second;
        if (!img || img->w() <= 0 || img->h() <= 0) { draw_pattern(MUII_BACKGROUND, X, Y, W, H); return; }
        fl_push_clip(X, Y, W, H);
        int iw = img->w(), ih = img->h();
        for (int yy = Y - ((Y % ih) + ih) % ih; yy < Y + H; yy += ih)
            for (int xx = X - ((X % iw) + iw) % iw; xx < X + W; xx += iw) img->draw(xx, yy);
        fl_pop_clip();
        return;
    }
    case IMS_GRADIENT: {
        // from the start colour to the end one along the angle (0: downwards, 90: rightwards)
        double a = s.angle * M_PI / 180.0, dx = sin(a), dy = cos(a);
        double len = fabs(dx) * (W - 1) + fabs(dy) * (H - 1);
        if (len < 1) len = 1;
        double ox = dx < 0 ? W - 1 : 0, oy = dy < 0 ? H - 1 : 0;
        int r1 = (s.rgb >> 16) & 255, g1 = (s.rgb >> 8) & 255, b1 = s.rgb & 255;
        int r2 = (s.rgb2 >> 16) & 255, g2 = (s.rgb2 >> 8) & 255, b2 = s.rgb2 & 255;
        // (the last few gradients drawn are kept: the same one is drawn again for every object
        // that shows it through)
        struct Cached { std::string key; Fl_RGB_Image *img; };
        static std::vector<Cached> cache;
        std::string key = s.str() + "@" + std::to_string(W) + "x" + std::to_string(H);
        for (auto &c : cache) if (c.key == key) { c.img->draw(X, Y); return; }
        auto *d = new uchar[W * H * 3];
        for (int yy = 0; yy < H; yy++)
            for (int xx = 0; xx < W; xx++) {
                double t = ((xx - ox) * dx + (yy - oy) * dy) / len;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                uchar *px = d + (yy * W + xx) * 3;
                px[0] = (uchar)(r1 + (r2 - r1) * t);
                px[1] = (uchar)(g1 + (g2 - g1) * t);
                px[2] = (uchar)(b1 + (b2 - b1) * t);
            }
        auto *img = new Fl_RGB_Image(d, W, H, 3);
        img->alloc_array = 1;
        img->draw(X, Y);
        cache.push_back({key, img});
        if (cache.size() > 8) { delete cache.front().img; cache.erase(cache.begin()); }
        return;
    }
    default: return;
    }
}

}  // namespace zune
