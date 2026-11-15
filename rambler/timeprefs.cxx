/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Time preferences (prefs/time/main.c): see timeprefs.h.
*/
#include "timeprefs.h"
#include "tools.h"
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/Fl_Input.H>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <sys/wait.h>
#include <vector>

using namespace lune;

namespace rambler {

namespace {
struct Result { std::string out; bool ok = false; int rc = 127; };

std::string quote(const std::string &s) {
    std::string q = "'";
    for (char c : s) { if (c == '\'') q += "'\\''"; else q += c; }
    return q + "'";
}

Result run(const std::vector<std::string> &argv) {
    std::string cmd;
    for (auto &a : argv) cmd += (cmd.empty() ? "" : " ") + quote(a);
    cmd += " 2>&1";
    Result r;
    if (FILE *p = popen(cmd.c_str(), "r")) {
        char buf[1024];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) r.out.append(buf, n);
        int st = pclose(p);
        r.rc = WIFEXITED(st) ? WEXITSTATUS(st) : 127;
        r.ok = r.rc == 0;
    }
    while (!r.out.empty() && (r.out.back() == '\n' || r.out.back() == ' ')) r.out.pop_back();
    return r;
}

bool have(const char *prog) {
    return run({"sh", "-c", std::string("command -v ") + prog}).ok;
}
}  // namespace

static lune::Window *g_time_editor = nullptr;

void edit_time_prefs(bool standalone) {
    if (g_time_editor) { g_time_editor->show(); return; }
    bool timedatectl = have("timedatectl");
    bool ntp = timedatectl && run({"timedatectl", "show", "-p", "NTP", "--value"}).out == "yes";

    auto *root = VGroup();
    auto *top = HGroup();
    // -- the calendar box
    auto *cb = new Group(Group::Vertical, Frame_Group);
    auto *my = HGroup();
    std::vector<std::string> months;
    for (int m = 0; m < 12; m++) {
        struct tm t = {};
        t.tm_mon = m;
        char b[64];
        strftime(b, sizeof b, "%B", &t);
        months.push_back(b);
    }
    auto *month = new Cycle(months, 0);
    HSpace();
    auto *ysub = new Button("-");
    auto *year = new String("", 4);
    year->input()->type(FL_INT_INPUT);
    auto *yadd = new Button("+");
    my->end();
    auto *cal = new Calendar();
    cb->end();
    // -- the clock box
    auto *kb = new Group(Group::Vertical, Frame_Group);
    auto *clock = new Clock();
    auto *trow = HGroup();
    HSpace();
    auto *hf = new String("", 2);
    new Text(":");
    auto *mf = new String("", 2);
    new Text(":");
    auto *sf = new String("", 2);
    for (String *f : {hf, mf, sf}) f->input()->type(FL_INT_INPUT);
    auto *tsub = new Button("-");
    auto *tadd = new Button("+");
    HSpace();
    trow->end();
    Checkmark *auto_time = nullptr;
    if (timedatectl) {
        auto *ar = HGroup();
        HSpace();
        auto_time = new Checkmark(ntp);
        new Text("Set automatically (network time)");
        HSpace();
        ar->end();
    }
    kb->end();
    top->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();

    auto two = [](int v) { char b[8]; snprintf(b, sizeof b, "%02d", v); return std::string(b); };
    // the fields from the clock (while it goes)
    auto show_time = [=] {
        hf->value(two(clock->hour()));
        mf->value(two(clock->minute()));
        sf->value(two(clock->second()));
        for (String *f : {hf, mf, sf}) f->redraw();
    };
    auto show_date = [=] {
        month->active(cal->month() - 1);
        year->value(std::to_string(cal->year()));
        year->redraw();
    };
    show_date();
    show_time();
    clock->on_tick = show_time;
    // the time being set by hand: the clock stops at it (the time set, not the time now)
    auto active_field = std::make_shared<int>(0);   // 0 hours, 1 minutes, 2 seconds
    auto take_time = [=] {
        clock->frozen(true);
        clock->time(atoi(hf->value()), atoi(mf->value()), atoi(sf->value()));
        show_time();
    };
    int i = 0;
    for (String *f : {hf, mf, sf}) {
        int which = i++;
        f->on_acknowledge = [=] { *active_field = which; take_time(); };
    }
    auto step = [=](int by) {
        // (the field being typed in, else the one last used)
        if (Fl::focus() == hf->input()) *active_field = 0;
        else if (Fl::focus() == mf->input()) *active_field = 1;
        else if (Fl::focus() == sf->input()) *active_field = 2;
        if (!clock->frozen()) take_time();
        clock->frozen(true);
        int h = clock->hour(), m = clock->minute(), s = clock->second();
        if (*active_field == 0) h += by;
        else if (*active_field == 1) m += by;
        else s += by;
        clock->time(h, m, s);
        show_time();
    };
    tsub->on_press = [=] { step(-1); };
    tadd->on_press = [=] { step(1); };
    month->on_change = [=](int m) { cal->date(cal->year(), m + 1, cal->day()); };
    auto set_year = [=](int y) { cal->date(y, cal->month(), cal->day()); show_date(); };
    year->on_acknowledge = [=] { set_year(atoi(year->value())); };
    ysub->on_press = [=] { set_year(cal->year() - 1); };
    yadd->on_press = [=] { set_year(cal->year() + 1); };
    // automatic time: the clock goes and nothing is set by hand
    auto manual = [=] {
        bool by_hand = !auto_time || !auto_time->on();
        for (Area *a : std::initializer_list<Area *>{month, ysub, year, yadd, cal, hf, mf, sf, tsub, tadd}) a->disabled(!by_hand);
        if (!by_hand) { clock->frozen(false); show_time(); }
        root->redraw();
    };
    if (auto_time) auto_time->on_press = manual;
    manual();

    // Use: the system's clock; Save: also the computer's own clock (RTC)
    auto put = [=](bool save_rtc) -> bool {
        std::string err;
        if (auto_time && auto_time->on()) {
            if (!ntp) {
                Result r = run({"timedatectl", "set-ntp", "true"});
                if (!r.ok) err = r.out;
            }
        } else {
            char when[64];
            time_t now = ::time(nullptr);
            struct tm t;
            localtime_r(&now, &t);
            int h = clock->frozen() ? clock->hour() : t.tm_hour, mi = clock->frozen() ? clock->minute() : t.tm_min,
                s = clock->frozen() ? clock->second() : t.tm_sec;
            snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d:%02d", cal->year(), cal->month(), cal->day(), h, mi, s);
            if (timedatectl) {
                // (set by hand: the network's time off first, or the system refuses)
                if (ntp) { Result r = run({"timedatectl", "set-ntp", "false"}); if (!r.ok) err = r.out; }
                if (err.empty()) {
                    Result r = run({"timedatectl", "set-time", when});
                    if (!r.ok) err = r.out;
                }
                // (timedatectl keeps the computer's clock along with the system's)
            } else {
                Result r = run({"pkexec", "date", "-s", when});
                if (!r.ok) err = r.rc == 127 ? tr("Setting the time needs timedatectl or pkexec.") : r.out;
                else if (save_rtc) {
                    Result h2 = run({"pkexec", "hwclock", "--systohc"});
                    if (!h2.ok) err = h2.out;
                }
            }
        }
        if (!err.empty()) { request("Time Preferences", tr("The time could not be set:") + "\n" + err, "OK"); return false; }
        return true;
    };
    auto *w = new lune::Window("Time Preferences", root);
    w->id("TIMEPREFS");
    g_time_editor = w;
    auto close = [w, standalone] {
        w->hide();
        Fl::delete_widget(w);
        g_time_editor = nullptr;
        if (standalone) exit(0);
    };
    save->on_press = [=] { if (put(true)) close(); };
    use->on_press = [=] { if (put(false)) close(); };
    cancel->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    {   // (opened at its least width: the calendar and the clock side by side)
        int minw = 0, minh = 0, maxw = 0, maxh = 0;
        w->get_size_range(&minw, &minh, &maxw, &maxh);
        if (minw > 0) w->size(std::max(minw, 560), std::max(minh, w->h()));
    }
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace rambler
