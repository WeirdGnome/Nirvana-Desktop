/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The ScreenMode preferences (prefs/screenmode/smeditor.c, smselector.c, smproperties.c):
    see screenmodeprefs.h.
*/
#include "screenmodeprefs.h"
#include "tools.h"
#include "lune.h"

#include <FL/Fl.H>
#include <FL/Fl_Input.H>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <sys/stat.h>

using namespace lune;

namespace rambler {

namespace {
std::string file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/rambler/screenmode.prefs";
}

std::vector<std::string> xrandr_args(const ScreenModePrefs &p, const std::vector<DisplayMode> &modes) {
    std::string out = p.output;
    if (out.empty() && !modes.empty()) out = modes.front().output;
    if (out.empty() || p.mode.empty()) return {};
    std::vector<std::string> a = {"xrandr", "--output", out, "--mode", p.mode};
    if (!p.rate.empty()) { a.push_back("--rate"); a.push_back(p.rate); }
    // the screen larger than the mode: it scrolls with the pointer (autoscroll); else none
    int mw = 0, mh = 0;
    sscanf(p.mode.c_str(), "%dx%d", &mw, &mh);
    int w = p.width > 0 ? p.width : mw, h = p.height > 0 ? p.height : mh;
    a.push_back("--panning");
    a.push_back(p.autoscroll && (w > mw || h > mh) ? std::to_string(std::max(w, mw)) + "x" + std::to_string(std::max(h, mh))
                                                     : "0x0");
    return a;
}
}  // namespace

// xrandr -q: an output's line ("HDMI-1 connected primary 1920x1080+0+0 ..."), then its modes
// ("   1920x1080     60.00*+  50.00    59.94"); disconnected outputs have none listed
std::vector<DisplayMode> display_modes() {
    std::vector<DisplayMode> v;
    FILE *p = popen("xrandr -q 2>/dev/null", "r");
    if (!p) return v;
    char buf[1024];
    std::string output;
    while (fgets(buf, sizeof buf, p)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (line.empty() || line.rfind("Screen ", 0) == 0) continue;
        if (line[0] != ' ') {
            std::istringstream ls(line);
            std::string name, state;
            ls >> name >> state;
            output = state == "connected" ? name : "";
            continue;
        }
        if (output.empty()) continue;
        std::istringstream ls(line);
        std::string name, r;
        ls >> name;
        DisplayMode m;
        m.output = output;
        m.name = name;
        if (sscanf(name.c_str(), "%dx%d", &m.w, &m.h) != 2) continue;
        while (ls >> r) {
            if (r == "+") { if (!v.empty()) v.back().preferred = true; continue; }
            DisplayMode e = m;
            e.current = r.find('*') != std::string::npos;
            e.preferred = r.find('+') != std::string::npos;
            r.erase(std::remove_if(r.begin(), r.end(), [](char c) { return c == '*' || c == '+'; }), r.end());
            e.rate = r;
            v.push_back(e);
        }
    }
    pclose(p);
    return v;
}

bool ScreenModePrefs::exists() {
    struct stat st;
    return stat(file().c_str(), &st) == 0;
}

ScreenModePrefs ScreenModePrefs::load() {
    ScreenModePrefs p;
    std::ifstream in(file());
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "output") p.output = v;
        else if (k == "mode") p.mode = v;
        else if (k == "rate") p.rate = v;
        else if (k == "width") p.width = std::clamp(atoi(v.c_str()), 0, 32767);
        else if (k == "height") p.height = std::clamp(atoi(v.c_str()), 0, 32767);
        else if (k == "autoscroll") p.autoscroll = atoi(v.c_str());
    }
    return p;
}

void ScreenModePrefs::save() const {
    std::string f = file();
    mkdir(f.substr(0, f.rfind('/')).c_str(), 0755);
    std::ofstream(f) << "output=" << output << "\nmode=" << mode << "\nrate=" << rate << "\nwidth=" << width
                     << "\nheight=" << height << "\nautoscroll=" << autoscroll << "\n";
}

void ScreenModePrefs::apply() const {
    auto modes = display_modes();
    // only a mode the output still has (a monitor changed since would get a blank screen)
    bool known = false;
    for (auto &m : modes)
        if ((output.empty() ? m.output == modes.front().output : m.output == output) && m.name == mode &&
            (rate.empty() || m.rate == rate))
            known = true;
    if (!known) return;
    auto a = xrandr_args(*this, modes);
    if (!a.empty()) launch(a, "/");
}

static Window *g_sm_editor = nullptr;

// Test: the requester counting down, and the mode the screen goes back to
struct TestState { Window *w; Text *msg; int left; std::shared_ptr<ScreenModePrefs> before; };

static void test_tick(void *d);

// "The screen goes back in %d seconds." with the number
static std::string back_in(int n) {
    std::string s = tr("The screen goes back in %d seconds.");
    auto p = s.find("%d");
    if (p != std::string::npos) s.replace(p, 2, std::to_string(n));
    return s;
}

static void end_test(TestState *t) {
    Fl::remove_timeout(test_tick, t);
    t->before->apply();
    t->w->hide();
    Fl::delete_widget(t->w);
    delete t;
}

static void test_tick(void *d) {
    auto *t = (TestState *)d;
    if (--t->left <= 0) { end_test(t); return; }
    t->msg->contents("\033c" + tr("Testing the screen mode.") + "\n" + back_in(t->left));
    Fl::repeat_timeout(1.0, test_tick, d);
}

void edit_screenmode_prefs(bool standalone) {
    if (g_sm_editor) { g_sm_editor->show(); return; }
    auto modes = std::make_shared<std::vector<DisplayMode>>(display_modes());
    ScreenModePrefs saved = ScreenModePrefs::exists() ? ScreenModePrefs::load() : ScreenModePrefs();
    // what the screen has now: the mode a test goes back to
    ScreenModePrefs now;
    for (auto &m : *modes)
        if (m.current) { now.output = m.output; now.mode = m.name; now.rate = m.rate; break; }
    if (!saved.width && !saved.height) { saved.autoscroll = false; }
    if (saved.mode.empty()) { ScreenModePrefs s = now; s.width = saved.width; s.height = saved.height; saved = s; }

    auto *root = VGroup();
    auto *top = HGroup();
    // -- the selector (smselector.c): the display modes
    auto *sel = new Group(Group::Vertical, Frame_Group);
    sel->frame_title("Display Mode");
    auto *list = new List();
    list->frame(Frame_InputList);
    int active = -1;
    for (size_t i = 0; i < modes->size(); i++) {
        auto &m = (*modes)[i];
        list->add({m.output + ": " + m.name + ", " + m.rate + " Hz"});
        if (m.output == saved.output && m.name == saved.mode && m.rate == saved.rate) active = (int)i;
    }
    list->done();
    sel->end();
    // -- the properties (smproperties.c)
    auto *right = VGroup();
    auto *pr = new Group(Group::Vertical, Frame_Group);
    pr->frame_title("Properties");
    pr->columns(3);
    new Text("\033rWidth:");
    auto *wid = new String("", 6);
    wid->input()->type(FL_INT_INPUT);
    auto *wd = HGroup();
    auto *wdef = new Checkmark(saved.width == 0);
    new Text("Default");
    wd->end();
    new Text("\033rHeight:");
    auto *hei = new String("", 6);
    hei->input()->type(FL_INT_INPUT);
    auto *hd = HGroup();
    auto *hdef = new Checkmark(saved.height == 0);
    new Text("Default");
    hd->end();
    new Text("\033rColors:");
    auto *col = new Text("\033l16777216", Frame_Text);
    new Text("");
    new Text("\033rAutoscroll:");
    auto *ac = HGroup();
    auto *autos = new Checkmark(saved.autoscroll);
    HSpace();
    ac->end();
    new Text("");
    pr->end();
    auto *inf = new Group(Group::Vertical, Frame_Group);
    inf->frame_title("Mode Information");
    auto *info = new Text("\033l\n\n\n\n");   // (room for its five lines)
    inf->end();
    HVSpace();
    right->end();
    top->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *test = new Button("Test");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();
    (void)col;

    // the mode chosen: its size in the properties (Default: the mode's), its information
    auto show_mode = [=](int i) {
        if (i < 0 || i >= (int)modes->size()) {
            info->contents("\033l" + tr("No display mode chosen."));
            return;
        }
        auto &m = (*modes)[i];
        if (wdef->on()) wid->value(std::to_string(m.w));
        if (hdef->on()) hei->value(std::to_string(m.h));
        std::string t = "\033l" + tr("Output") + ": " + m.output + "\n" + tr("Visible size") + ": " +
                        std::to_string(m.w) + " x " + std::to_string(m.h) + "\n" + tr("Refresh rate") + ": " + m.rate + " Hz";
        if (m.preferred) t += "\n" + tr("The monitor's preferred mode");
        if (m.current) t += "\n" + tr("In use now");
        while (std::count(t.begin(), t.end(), '\n') < 4) t += "\n";   // (the lines at the top)
        info->contents(t);
        wid->redraw();
        hei->redraw();
    };
    if (saved.width) wid->value(std::to_string(saved.width));
    if (saved.height) hei->value(std::to_string(saved.height));
    list->on_active = show_mode;
    // Default: the size of the mode, the field not to be typed in
    auto defaults = [=] {
        wid->disabled(wdef->on());
        hei->disabled(hdef->on());
        show_mode(list->active());
    };
    wdef->on_press = defaults;
    hdef->on_press = defaults;
    auto collect = [=] {
        ScreenModePrefs p;
        int i = list->active();
        if (i < 0 || i >= (int)modes->size()) return p;
        auto &m = (*modes)[i];
        p.output = m.output;
        p.mode = m.name;
        p.rate = m.rate;
        p.width = wdef->on() ? 0 : std::max(m.w, atoi(wid->value()));
        p.height = hdef->on() ? 0 : std::max(m.h, atoi(hei->value()));
        p.autoscroll = autos->on();
        return p;
    };

    auto *w = new Window("Screen Mode Preferences", root);
    w->id("SCREENMODEPREFS");
    g_sm_editor = w;
    auto close = [w, standalone] {
        w->hide();
        Fl::delete_widget(w);
        g_sm_editor = nullptr;
        if (standalone) exit(0);
    };
    save->on_press = [=] { auto p = collect(); if (p.mode.empty()) return; p.save(); p.apply(); close(); };
    use->on_press = [=] { auto p = collect(); if (p.mode.empty()) return; p.apply(); close(); };
    cancel->on_press = close;
    // Test: the mode for a few seconds, then the screen goes back to what it had
    test->on_press = [=] {
        auto p = collect();
        if (p.mode.empty()) return;
        auto before = std::make_shared<ScreenModePrefs>(now);
        for (auto &m : display_modes())
            if (m.current) { before->output = m.output; before->mode = m.name; before->rate = m.rate; break; }
        p.apply();
        auto *tr_root = VGroup();
        auto *msg = new Text("\033c" + tr("Testing the screen mode.") + "\n" + back_in(10));
        auto *back = new Button("Back Now");
        tr_root->end();
        auto *tw = new Window("Test", tr_root);
        tw->set_modal();
        auto *t = new TestState{tw, msg, 10, before};
        back->on_press = [t] { end_test(t); };
        tw->callback([](Fl_Widget *, void *d) { end_test((TestState *)d); }, t);
        Fl::add_timeout(1.0, test_tick, t);
        // in the middle of the screen as the mode under test has it
        int mw = 0, mh = 0;
        sscanf(p.mode.c_str(), "%dx%d", &mw, &mh);
        tw->position(std::max(0, (mw - tw->w()) / 2), std::max(0, (mh - tw->h()) / 3));
        tw->show();
    };
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    if (active >= 0) list->active(active);
    defaults();
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace rambler
