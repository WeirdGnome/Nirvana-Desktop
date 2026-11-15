/*
    Copyright (C) 2002-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Calendar and Clock classes (workbench/classes/zune/calendar, clock): see lune.h.
*/
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <langinfo.h>

namespace lune {

namespace {
int days_in(int y, int m) {
    static const int D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return D[(m - 1) % 12];
}

// the week day of a date (0 Sunday)
int weekday(int y, int m, int d) {
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    mktime(&t);
    return t.tm_wday;
}
}  // namespace

// ---------------------------------------------------------------------------------------------
Calendar::Calendar() : Area(Frame_Group) {
    background(MPEN_BACKGROUND);
#ifdef _NL_TIME_FIRST_WEEKDAY
    // the locale's first day of the week (glibc: 1 Sunday, 2 Monday)
    first_wday_ = (*nl_langinfo(_NL_TIME_FIRST_WEEKDAY) + 6) % 7;
#endif
    time_t now = ::time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    date(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

void Calendar::date(int y, int m, int d) {
    year_ = std::clamp(y, 1970, 2099);
    month_ = std::clamp(m, 1, 12);
    day_ = std::clamp(d, 1, days_in(year_, month_));
    redraw();
}

void Calendar::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    fl_font(prefs().font, prefs().font_size);
    int cw = (int)fl_width("88") + 8, ch = fl_height() + 4;
    minw = defw = cw * 7;
    minh = defh = ch * 7;
    maxw = maxh = 10000;
}

int Calendar::cell(int &cw, int &ch, int &x0, int &y0) const {
    cw = mwidth() / 7;
    ch = mheight() / 7;
    x0 = mleft() + (mwidth() - cw * 7) / 2;
    y0 = mtop() + (mheight() - ch * 7) / 2;
    return (weekday(year_, month_, 1) - first_wday_ + 7) % 7;
}

void Calendar::draw_contents() {
    int cw, ch, x0, y0, col0 = cell(cw, ch, x0, y0);
    fl_font(prefs().font, prefs().font_size);
    // the names of the days, the week's first first
    for (int i = 0; i < 7; i++) {
        struct tm t = {};
        t.tm_wday = (first_wday_ + i) % 7;
        char name[32];
        strftime(name, sizeof name, "%a", &t);
        std::string n(name);
        if (n.size() > 2 && (unsigned char)n[0] < 0x80) n = n.substr(0, 2);   // (two letters, as the original)
        fl_color(pens()[MPEN_SHADOW]);
        fl_draw(n.c_str(), x0 + i * cw, y0, cw, ch, FL_ALIGN_CENTER);
    }
    fl_color(pens()[MPEN_SHADOW]);
    fl_xyline(x0, y0 + ch - 1, x0 + cw * 7 - 1);
    int days = days_in(year_, month_);
    for (int d = 1; d <= days; d++) {
        int pos = col0 + d - 1, cx = x0 + (pos % 7) * cw, cy = y0 + (1 + pos / 7) * ch;
        bool sel = d == day_;
        if (sel) {
            // the chosen day: a pressed button in the fill pen
            fl_color(pens()[MPEN_FILL]);
            fl_rectf(cx + 1, cy + 1, cw - 2, ch - 2);
            fl_color(pens()[MPEN_SHADOW]);
            fl_xyline(cx + 1, cy + 1, cx + cw - 2);
            fl_yxline(cx + 1, cy + 1, cy + ch - 2);
            fl_color(pens()[MPEN_SHINE]);
            fl_xyline(cx + 1, cy + ch - 2, cx + cw - 2);
            fl_yxline(cx + cw - 2, cy + 1, cy + ch - 2);
        }
        fl_color(sel ? pens()[MPEN_SHINE] : pens()[MPEN_TEXT]);
        fl_draw(std::to_string(d).c_str(), cx, cy, cw, ch, FL_ALIGN_CENTER);
    }
    if (Fl::focus() == this) {   // (the keyboard's stop)
        fl_color(pens()[MPEN_SHADOW]);
        fl_line_style(FL_DOT);
        fl_rect(mleft(), mtop(), mwidth(), mheight());
        fl_line_style(0);
    }
}

int Calendar::handle(int e) {
    switch (e) {
    case FL_PUSH: {
        if (Fl::visible_focus()) Fl::focus(this);
        int cw, ch, x0, y0, col0 = cell(cw, ch, x0, y0);
        if (cw <= 0 || ch <= 0) return 1;
        int col = (Fl::event_x() - x0) / cw, row = (Fl::event_y() - y0) / ch - 1;
        if (col < 0 || col > 6 || row < 0) return 1;
        int d = row * 7 + col - col0 + 1;
        if (d >= 1 && d <= days_in(year_, month_) && d != day_) {
            day_ = d;
            redraw();
            if (on_change) on_change(d);
        }
        return 1;
    }
    case FL_FOCUS: case FL_UNFOCUS: redraw(); return 1;
    case FL_KEYBOARD: {
        int k = Fl::event_key(), d = day_;
        if (k == FL_Left) d--;
        else if (k == FL_Right) d++;
        else if (k == FL_Up) d -= 7;
        else if (k == FL_Down) d += 7;
        else return Area::handle(e);
        d = std::clamp(d, 1, days_in(year_, month_));
        if (d != day_) {
            day_ = d;
            redraw();
            if (on_change) on_change(d);
        }
        return 1;
    }
    default: return Area::handle(e);
    }
}

// ---------------------------------------------------------------------------------------------
Clock::Clock() : Area(Frame_None) {
    background(MPEN_BACKGROUND);
    time_t now = ::time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    h_ = t.tm_hour; m_ = t.tm_min; s_ = t.tm_sec;
    Fl::add_timeout(1.0 - fmod((double)std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch()).count() / 1000.0, 1.0),
                    tick, this);
}

Clock::~Clock() { Fl::remove_timeout(tick, this); }

void Clock::time(int h, int m, int s) {
    h_ = ((h % 24) + 24) % 24;
    m_ = ((m % 60) + 60) % 60;
    s_ = ((s % 60) + 60) % 60;
    redraw();
}

void Clock::frozen(bool f) {
    frozen_ = f;
    if (!f) {   // (going again: the system's time)
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        time(t.tm_hour, t.tm_min, t.tm_sec);
    }
}

void Clock::tick(void *d) {
    auto *c = (Clock *)d;
    if (!c->frozen_) {
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        c->time(t.tm_hour, t.tm_min, t.tm_sec);
        if (c->on_tick) c->on_tick();
    }
    Fl::repeat_timeout(1.0, tick, d);
}

void Clock::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = minh = 80;
    defw = defh = 140;
    maxw = maxh = 10000;
}

void Clock::draw_contents() {
    int sz = std::min(mwidth(), mheight()) - 4;
    if (sz < 20) return;
    double cx = mleft() + mwidth() / 2.0, cy = mtop() + mheight() / 2.0, r = sz / 2.0;
    // the face: the shine pen, a shadow ring, the hours' marks
    fl_color(pens()[MPEN_SHINE]);
    fl_pie((int)(cx - r), (int)(cy - r), sz, sz, 0, 360);
    fl_color(pens()[MPEN_SHADOW]);
    fl_line_style(FL_SOLID, 2);
    fl_arc((int)(cx - r), (int)(cy - r), sz, sz, 0, 360);
    for (int i = 0; i < 60; i++) {
        double a = i * M_PI / 30, in = i % 5 ? r * 0.92 : r * 0.82;
        fl_line_style(FL_SOLID, i % 5 ? 1 : 2);
        fl_line((int)(cx + sin(a) * in), (int)(cy - cos(a) * in), (int)(cx + sin(a) * r * 0.97), (int)(cy - cos(a) * r * 0.97));
    }
    // the hands: hours and minutes in the shadow pen, seconds in the fill pen
    auto hand = [&](double turn, double len, int width, Fl_Color c) {
        double a = turn * 2 * M_PI;
        fl_color(c);
        fl_line_style(FL_SOLID | FL_CAP_ROUND, width);
        fl_line((int)cx, (int)cy, (int)(cx + sin(a) * len), (int)(cy - cos(a) * len));
    };
    hand(((h_ % 12) + m_ / 60.0) / 12.0, r * 0.5, std::max(3, sz / 30), pens()[MPEN_SHADOW]);
    hand((m_ + s_ / 60.0) / 60.0, r * 0.75, std::max(2, sz / 45), pens()[MPEN_SHADOW]);
    hand(s_ / 60.0, r * 0.85, 1, pens()[MPEN_FILL]);
    fl_line_style(0);
    fl_color(pens()[MPEN_SHADOW]);
    fl_pie((int)cx - 3, (int)cy - 3, 6, 6, 0, 360);
}

}  // namespace lune
