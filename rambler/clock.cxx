/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Clock utility: see clock.h.
*/
#include "clock.h"
#include "lune.h"
#include "catalog.h"
#include "tools.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Input.H>
#include <FL/fl_ask.H>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

namespace {
enum { M_ANALOG = 1, M_DIGITAL, M_QUIT, M_SECONDS, M_DATE, M_24H, M_SETALARM, M_ALARM, M_SAVE };

struct ClockPrefs {
    bool digital = false, seconds = true, date = true, h24 = true, alarm = false;
    int alarm_h = 7, alarm_m = 0;
};

std::string prefs_file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/rambler/clock.prefs";
}

ClockPrefs load() {
    ClockPrefs p;
    std::ifstream in(prefs_file());
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        int v = atoi(line.c_str() + eq + 1);
        if (k == "digital") p.digital = v;
        else if (k == "seconds") p.seconds = v;
        else if (k == "date") p.date = v;
        else if (k == "h24") p.h24 = v;
        else if (k == "alarm") p.alarm = v;
        else if (k == "alarm_h") p.alarm_h = std::clamp(v, 0, 23);
        else if (k == "alarm_m") p.alarm_m = std::clamp(v, 0, 59);
    }
    return p;
}

void save(const ClockPrefs &p) {
    std::error_code ec;
    fs::create_directories(fs::path(prefs_file()).parent_path(), ec);
    std::ofstream(prefs_file()) << "digital=" << p.digital << "\nseconds=" << p.seconds << "\ndate=" << p.date << "\nh24=" << p.h24
                                << "\nalarm=" << p.alarm << "\nalarm_h=" << p.alarm_h << "\nalarm_m=" << p.alarm_m << "\n";
}

// the digital face: the time as large as the window lets it be
class Digital : public Area {
public:
    Digital() : Area(Frame_None) { background(MPEN_BACKGROUND); }
    void text(const std::string &t) { if (t != text_) { text_ = t; redraw(); } }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override {
        minw = 80; minh = 24; defw = 180; defh = 60; maxw = maxh = 10000;
    }

protected:
    void draw_contents() override {
        int size = std::max(8, mheight() * 2 / 3);
        fl_font(FL_HELVETICA_BOLD, size);
        while (size > 8 && fl_width(text_.c_str()) > mwidth() - 8) fl_font(FL_HELVETICA_BOLD, --size);
        fl_color(pens()[MPEN_TEXT]);
        fl_draw(text_.c_str(), mleft(), mtop(), mwidth(), mheight(), FL_ALIGN_CENTER);
    }
    std::string text_;
};

// Set Alarm...: the hour and the minute
void alarm_request(std::shared_ptr<ClockPrefs> p, std::function<void()> done) {
    auto *root = VGroup();
    auto *row = HGroup();
    HSpace();
    new Text("\033rAlarm time:");
    auto *hf = new String(std::to_string(p->alarm_h), 2);
    new Text(":");
    char mm[4];
    snprintf(mm, sizeof mm, "%02d", p->alarm_m);
    auto *mf = new String(mm, 2);
    HSpace();
    for (String *f : {hf, mf}) { f->input()->type(FL_INT_INPUT); f->fixed_width(36); }
    row->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *ok = new Button("OK");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();
    auto *w = new lune::Window("Set Alarm", root);
    auto close = [w] { w->hide(); Fl::delete_widget(w); };
    ok->on_press = [=] {
        p->alarm_h = std::clamp(atoi(hf->value()), 0, 23);
        p->alarm_m = std::clamp(atoi(mf->value()), 0, 59);
        p->alarm = true;
        close();
        done();
    };
    cancel->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    w->set_modal();
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}
}  // namespace

void run_clock() {
    auto p = std::make_shared<ClockPrefs>(load());
    auto *root = VGroup();
    root->spacing(0);
    auto *analog = new Clock();
    auto *digital = new Digital();
    root->end();
    auto *w = new lune::Window("Clock", root);
    w->id("CLOCK");
    w->resizable(root);

    auto title = std::make_shared<std::string>();
    auto last_alarm = std::make_shared<int>(-1);   // (the minute it last rang)
    auto update = [=] {
        time_t now = ::time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        char b[64];
        // the time in the digital face; the date (as the locale writes it) in the title
        strftime(b, sizeof b, p->h24 ? (p->seconds ? "%H:%M:%S" : "%H:%M") : (p->seconds ? "%I:%M:%S %p" : "%I:%M %p"), &t);
        digital->text(b);
        strftime(b, sizeof b, "%x", &t);
        std::string tt = p->date ? std::string(b) : tr("Clock");
        if (tt != *title) { *title = tt; w->copy_label(tt.c_str()); }
        // the alarm: once, on its minute
        int minute = t.tm_hour * 60 + t.tm_min;
        if (p->alarm && t.tm_hour == p->alarm_h && t.tm_min == p->alarm_m && *last_alarm != minute) {
            *last_alarm = minute;
            for (int i = 0; i < 3; i++) fl_beep(FL_BEEP_DEFAULT);
            char at[16];
            snprintf(at, sizeof at, "%02d:%02d", p->alarm_h, p->alarm_m);
            request("Clock", tr("Alarm") + " " + at, "OK");
        }
    };
    auto show_face = [=] {
        if (p->digital) { analog->hide(); digital->show(); }
        else { digital->hide(); analog->show(); }
        analog->seconds(p->seconds);
        root->relayout();
        w->redraw();
        update();
    };
    analog->on_tick = update;   // (each second)

    w->menustrip({
        {"Project", 0, 0, 0, 0, {
            {"Analog", M_ANALOG, p->digital ? 0 : Menu_Checked, 0, 1},
            {"Digital", M_DIGITAL, (p->digital ? Menu_Checked : 0) | Menu_Line, 0, 1},
            {"Quit", M_QUIT, 0, 'q'},
        }},
        {"Settings", 0, 0, 0, 0, {
            {"Seconds", M_SECONDS, Menu_Toggle | (p->seconds ? Menu_Checked : 0)},
            {"Date", M_DATE, Menu_Toggle | (p->date ? Menu_Checked : 0)},
            {"24 Hour", M_24H, Menu_Toggle | (p->h24 ? Menu_Checked : 0) | Menu_Line},
            {"Set Alarm...", M_SETALARM},
            {"Alarm", M_ALARM, Menu_Toggle | (p->alarm ? Menu_Checked : 0) | Menu_Line},
            {"Save Settings", M_SAVE, 0, 's'},
        }},
    });
    auto quit = [] { exit(0); };
    w->on_menu = [=](int id) {
        switch (id) {
        case M_ANALOG: p->digital = false; show_face(); break;
        case M_DIGITAL: p->digital = true; show_face(); break;
        case M_QUIT: quit(); break;
        case M_SECONDS: p->seconds = w->menu_checked(M_SECONDS); show_face(); break;
        case M_DATE: p->date = w->menu_checked(M_DATE); update(); break;
        case M_24H: p->h24 = w->menu_checked(M_24H); update(); break;
        case M_SETALARM:
            alarm_request(p, [=] { w->menu_checked(M_ALARM, true); *last_alarm = -1; });
            break;
        case M_ALARM: p->alarm = w->menu_checked(M_ALARM); *last_alarm = -1; break;
        case M_SAVE: save(*p); break;
        }
    };
    w->callback([](Fl_Widget *, void *) { exit(0); });
    w->size(std::max(w->w(), 160), std::max(w->h(), 160));
    w->show();
    show_face();
}

}  // namespace rambler
