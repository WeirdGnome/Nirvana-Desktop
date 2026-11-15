/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Input preferences (prefs/input/ipeditor.c): see inputprefs.h.
*/
#include "inputprefs.h"
#include "tools.h"
#include "zune.h"

#include <FL/Fl.H>

#include <cstdio>
#include <fstream>
#include <memory>
#include <sys/stat.h>
#include <vector>

using namespace zune;

namespace wanderer {

namespace {
std::string file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/wanderer/input.prefs";
}

// the XKB layouts the system knows (evdev.lst: "  pl              Polish")
struct Layout { std::string code, name; };
std::vector<Layout> layouts() {
    std::vector<Layout> v;
    std::ifstream in("/usr/share/X11/xkb/rules/evdev.lst");
    std::string line;
    bool in_layouts = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] == '!') { in_layouts = line.find("layout") != std::string::npos; continue; }
        if (!in_layouts || line.size() < 3) continue;
        size_t a = line.find_first_not_of(' ');
        size_t b = line.find(' ', a);
        if (a == std::string::npos || b == std::string::npos) continue;
        size_t c = line.find_first_not_of(' ', b);
        v.push_back({line.substr(a, b - a), c == std::string::npos ? "" : line.substr(c)});
    }
    return v;
}

const char *const SWITCH_OPTIONS[] = {"grp:alt_shift_toggle", "grp:ctrl_shift_toggle", "grp:caps_toggle",
                                      "grp:win_space_toggle", "grp:toggle"};
}  // namespace

bool InputPrefs::exists() {
    struct stat st;
    return stat(file().c_str(), &st) == 0;
}

InputPrefs InputPrefs::load() {
    InputPrefs p;
    std::ifstream in(file());
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        int n = atoi(v.c_str());
        if (k == "keymap") p.keymap = v;
        else if (k == "alt_keymap") p.alt_keymap = v;
        else if (k == "switch_enable") p.switch_enable = n;
        else if (k == "switch_key") p.switch_key = std::clamp(n, 0, 4);
        else if (k == "repeat_rate") p.repeat_rate = std::clamp(n, 0, 12);
        else if (k == "repeat_delay") p.repeat_delay = std::clamp(n, 0, 74);
        else if (k == "double_click") p.double_click = std::clamp(n, 0, 199);
        else if (k == "mouse_speed") p.mouse_speed = std::clamp(n, 0, 2);
        else if (k == "accelerated") p.accelerated = n;
        else if (k == "left_handed") p.left_handed = n;
    }
    return p;
}

void InputPrefs::save() const {
    std::string f = file();
    std::string dir = f.substr(0, f.rfind('/'));
    mkdir(dir.c_str(), 0755);
    std::ofstream out(f);
    out << "keymap=" << keymap << "\nalt_keymap=" << alt_keymap << "\nswitch_enable=" << switch_enable
        << "\nswitch_key=" << switch_key << "\nrepeat_rate=" << repeat_rate << "\nrepeat_delay=" << repeat_delay
        << "\ndouble_click=" << double_click << "\nmouse_speed=" << mouse_speed << "\naccelerated=" << accelerated
        << "\nleft_handed=" << left_handed << "\n";
}

void InputPrefs::apply() const {
    // the key repeat: xset r rate <delay ms> <repeats a second>
    int delay = 20 + 20 * repeat_delay;
    double interval = 0.02 * (12 - repeat_rate);
    int rate = interval > 0 ? std::clamp((int)(1.0 / interval + 0.5), 1, 100) : 100;
    launch({"xset", "r", "rate", std::to_string(delay), std::to_string(rate)}, "/");
    // the mouse: a pixel for every 1, 2 or 4 counts (ip_PointerTicks), twice that past a few
    // pixels when accelerated
    int ticks = 1 << (2 - mouse_speed);
    std::string accel = accelerated ? std::to_string(2) + "/" + std::to_string(ticks) : "1/" + std::to_string(ticks);
    launch({"xset", "m", accel, accelerated ? "4" : "0"}, "/");
    // the buttons the other way round for a left hand
    launch({"xmodmap", "-e", left_handed ? "pointer = 3 2 1" : "pointer = default"}, "/");
    // the keymap (only when one is chosen: the system's otherwise)
    if (!keymap.empty()) {
        std::vector<std::string> a = {"setxkbmap", "-layout"};
        bool alt = switch_enable && !alt_keymap.empty();
        a.push_back(alt ? keymap + "," + alt_keymap : keymap);
        if (alt) { a.push_back("-option"); a.push_back(""); a.push_back("-option"); a.push_back(SWITCH_OPTIONS[switch_key]); }
        launch(a, "/");
    }
}

static Window *g_input_editor = nullptr;

void edit_input_prefs(bool standalone) {
    if (g_input_editor) { g_input_editor->show(); return; }
    InputPrefs saved = InputPrefs::exists() ? InputPrefs::load() : InputPrefs();
    auto lay = std::make_shared<std::vector<Layout>>(layouts());
    auto keymap = std::make_shared<std::string>(saved.keymap), alt = std::make_shared<std::string>(saved.alt_keymap);
    auto name_of = [lay](const std::string &code) {
        if (code.empty()) return tr("System default");
        for (auto &l : *lay) if (l.code == code) return l.name;
        return code;
    };
    auto *root = VGroup();
    auto *reg = new Register({"Keyboard", "Mouse"});
    // -- Keyboard
    auto *kb = HGroup();
    auto *kt = new Group(Group::Vertical, Frame_Group);
    kt->frame_title("Keyboard Type");
    kt->weight(50);
    auto *list = new List();
    list->frame(Frame_InputList);
    for (auto &l : *lay) list->add({l.name});
    list->done();
    auto *kg = new Group(Group::Vertical);
    kg->columns(3);
    new Text("\033rDefault:");
    auto *def = new Text("\033l" + name_of(saved.keymap), Frame_Text);
    def->weight(255);
    auto *setdef = new Button("Set");
    new Text("\033rAlternative:");
    auto *altt = new Text("\033l" + name_of(saved.alt_keymap), Frame_Text);
    altt->weight(255);
    auto *setalt = new Button("Set");
    kg->end();
    kt->end();
    auto *right = VGroup();
    right->weight(55);
    auto *rr = new Group(Group::Horizontal, Frame_Group);
    rr->frame_title("Key Repeat Rate");
    auto *rate = new Slider(0, 12, saved.repeat_rate);
    rate->format = [](int v) { char b[16]; snprintf(b, sizeof b, "%3.2fs", 0.02 * (12 - v)); return std::string(b); };
    rr->end();
    auto *rd = new Group(Group::Horizontal, Frame_Group);
    rd->frame_title("Key Repeat Delay");
    auto *delay = new Slider(0, 74, saved.repeat_delay);
    delay->format = [](int v) { return std::to_string(20 + 20 * v) + "ms"; };
    rd->end();
    auto *sw = new Group(Group::Vertical, Frame_Group);
    sw->frame_title("Keymap Switching");
    sw->columns(2);
    new Text("\033rEnable:");
    auto *swr = HGroup();
    auto *swon = new Checkmark(saved.switch_enable);
    HSpace();
    swr->end();
    new Text("\033rSwitch Key:");
    auto *swkey = new Cycle({"Alt+Shift", "Ctrl+Shift", "Caps Lock", "Super+Space", "Right Alt"}, saved.switch_key);
    sw->end();
    auto *te = new Group(Group::Vertical, Frame_Group);
    te->frame_title("Test");
    new String("", 256);
    te->end();
    HVSpace();
    right->end();
    kb->end();
    // -- Mouse
    auto *ms = VGroup();
    auto *sp = new Group(Group::Vertical, Frame_Group);
    sp->frame_title("Mouse Speed");
    auto *speed = new Cycle({"Slow", "Normal", "Fast"}, saved.mouse_speed);
    auto *ac = HGroup();
    HSpace();
    new Text("\033rAccelerated:");
    auto *accel = new Checkmark(saved.accelerated);
    ac->end();
    sp->end();
    auto *bs = new Group(Group::Vertical, Frame_Group);
    bs->frame_title("Button Settings");
    bs->columns(2);
    new Text("\033rDouble-Click Delay:");
    auto *dbl = new Slider(0, 199, saved.double_click);
    dbl->format = [](int v) { char b[16]; snprintf(b, sizeof b, "%3.2fs", 0.02 + 0.02 * v); return std::string(b); };
    new Text("\033rLeft-Handed Mouse:");
    auto *lh = HGroup();
    auto *left = new Checkmark(saved.left_handed);
    HSpace();
    lh->end();
    bs->end();
    HVSpace();
    ms->end();
    reg->end();
    auto *btns = HGroup();
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();
    // the keymap set from the list's chosen entry (the "Set" buttons)
    setdef->on_press = [=] {
        int i = list->active();
        if (i < 0) return;
        *keymap = (*lay)[i].code;
        def->contents("\033l" + (*lay)[i].name);
    };
    setalt->on_press = [=] {
        int i = list->active();
        if (i < 0) return;
        *alt = (*lay)[i].code;
        altt->contents("\033l" + (*lay)[i].name);
    };
    auto collect = [=] {
        InputPrefs p;
        p.keymap = *keymap;
        p.alt_keymap = *alt;
        p.switch_enable = swon->on();
        p.switch_key = swkey->active();
        p.repeat_rate = rate->value();
        p.repeat_delay = delay->value();
        p.double_click = dbl->value();
        p.mouse_speed = speed->active();
        p.accelerated = accel->on();
        p.left_handed = left->on();
        return p;
    };
    auto *w = new Window("Input Preferences", root);
    w->id("INPUTPREFS");
    g_input_editor = w;
    auto close = [w, standalone] {
        w->hide();
        Fl::delete_widget(w);
        g_input_editor = nullptr;
        if (standalone) exit(0);
    };
    save->on_press = [=] { auto p = collect(); p.save(); p.apply(); close(); };
    use->on_press = [=] { collect().apply(); close(); };
    cancel->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace wanderer
