/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Sound preferences: see soundprefs.h and beep.h.
*/
#include "soundprefs.h"
#include "beep.h"
#include "tools.h"
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/platform.H>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

namespace {
struct Result { std::string out; bool ok = false; };

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
        r.ok = pclose(p) == 0;
    }
    while (!r.out.empty() && (r.out.back() == '\n' || r.out.back() == ' ')) r.out.pop_back();
    return r;
}

std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

// the sound output (PulseAudio or PipeWire through pactl)
struct Sink { std::string name, description; };
struct Output {
    bool ok = false;
    std::vector<Sink> sinks;
    int current = 0, volume = 100;
    bool mute = false;
};

Output read_output() {
    Output o;
    Result l = run({"pactl", "list", "sinks"});
    if (!l.ok) return o;
    std::istringstream in(l.out);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.rfind("Name:", 0) == 0) o.sinks.push_back({trim(t.substr(5)), ""});
        else if (t.rfind("Description:", 0) == 0 && !o.sinks.empty()) o.sinks.back().description = trim(t.substr(12));
    }
    if (o.sinks.empty()) return o;
    o.ok = true;
    std::string def = run({"pactl", "get-default-sink"}).out;
    for (size_t i = 0; i < o.sinks.size(); i++) if (o.sinks[i].name == def) o.current = (int)i;
    // "Volume: front-left: 65536 / 100% / 0,00 dB, ...": the first per cent
    std::string v = run({"pactl", "get-sink-volume", "@DEFAULT_SINK@"}).out;
    auto pc = v.find('%');
    if (pc != std::string::npos) {
        size_t a = pc;
        while (a > 0 && isdigit((unsigned char)v[a - 1])) a--;
        o.volume = std::clamp(atoi(v.c_str() + a), 0, 150);
    }
    o.mute = run({"pactl", "get-sink-mute", "@DEFAULT_SINK@"}).out.find("yes") != std::string::npos;
    return o;
}

void put_beep(const BeepPrefs &p, bool archive) {
    std::string t = p.text();
    for (auto &f : archive ? std::vector<std::string>{env_dir() + "/sound.prefs", beep_saved_file()}
                           : std::vector<std::string>{env_dir() + "/sound.prefs"}) {
        std::error_code ec;
        fs::create_directories(fs::path(f).parent_path(), ec);
        std::ofstream(f) << t;
    }
    // the window manager: at once
    if (fl_display) {
        Atom prop = XInternAtom(fl_display, "_RAMBLER_SOUND", False), utf8 = XInternAtom(fl_display, "UTF8_STRING", False);
        XChangeProperty(fl_display, RootWindow(fl_display, fl_screen), prop, utf8, 8, PropModeReplace,
                        (const unsigned char *)t.c_str(), (int)t.size());
        XFlush(fl_display);
    }
}
}  // namespace

static lune::Window *g_sound_editor = nullptr;

void edit_sound_prefs(bool standalone) {
    if (g_sound_editor) { g_sound_editor->show(); return; }
    BeepPrefs p = load_beep_prefs();
    auto out = std::make_shared<Output>(read_output());

    auto *root = VGroup();
    // -- the beep
    auto *bg = new Group(Group::Vertical, Frame_Group);
    bg->frame_title("Beep");
    auto *ck = new Group(Group::Vertical);
    ck->columns(2);
    new Text("\033rFlash display:");
    auto *flash = new Checkmark(p.flash);
    new Text("\033rMake sound:");
    auto *sound = new Checkmark(p.sound);
    new Text("\033rSound type:");
    auto *type = new Cycle({"Beep", "Sampled sound"}, p.type);
    new Text("\033rVolume:");
    auto *volume = new Slider(0, 100, p.volume);
    volume->format = [](int v) { return std::to_string(v) + "%"; };
    new Text("\033rPitch:");
    auto *pitch = new Slider(110, 3520, p.pitch);
    pitch->format = [](int v) { return std::to_string(v) + " Hz"; };
    new Text("\033rDuration:");
    auto *duration = new Slider(1, 200, p.duration);
    duration->format = [](int v) { char b[16]; snprintf(b, sizeof b, "%d.%02d s", v / 100, v % 100); return std::string(b); };
    new Text("\033rSample:");
    auto *srow = HGroup();
    auto *sample = new String(p.sample);
    auto *pop = PopButton(Pop_File);
    srow->end();
    ck->end();
    auto *trow = HGroup();
    HSpace();
    auto *test = new Button("Test");
    HSpace();
    trow->end();
    bg->end();
    // -- the output (AHI's part)
    Cycle *device = nullptr;
    Slider *master = nullptr;
    Checkmark *mute = nullptr;
    if (out->ok) {
        auto *og = new Group(Group::Vertical, Frame_Group);
        og->frame_title("Output");
        og->columns(2);
        std::vector<std::string> names;
        for (auto &s : out->sinks) names.push_back(s.description.empty() ? s.name : s.description);
        new Text("\033rDevice:");
        device = new Cycle(names, out->current);
        new Text("\033rMaster volume:");
        master = new Slider(0, 150, out->volume);
        master->format = [](int v) { return std::to_string(v) + "%"; };
        new Text("\033rMute:");
        mute = new Checkmark(out->mute);
        og->end();
    }
    auto *btns = HGroup();
    btns->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();

    auto collect = [=] {
        BeepPrefs n;
        n.flash = flash->on();
        n.sound = sound->on();
        n.type = type->active();
        n.sample = sample->value();
        n.volume = volume->value();
        n.pitch = pitch->value();
        n.duration = duration->value();
        return n;
    };
    // the beep's own settings only with a beep, the file only with a sampled sound
    auto ghost = [=] {
        bool s = sound->on(), beep = type->active() == 0;
        type->disabled(!s);
        volume->disabled(!s);
        pitch->disabled(!s || !beep);
        duration->disabled(!s || !beep);
        sample->disabled(!s || beep);
        pop->disabled(!s || beep);
        root->redraw();
    };
    ghost();
    sound->on_press = ghost;
    type->on_change = [=](int) { ghost(); };
    pop->on_press = [=] {
        std::string path = sample->value();
        if (path.empty()) path = std::string(getenv("HOME") ? getenv("HOME") : "/") + "/";
        if (file_request(tr("Select a sound"), path)) sample->value(path);
    };
    // Test: the beep as it is set up here (the screen does not flash for it)
    test->on_press = [=] {
        BeepPrefs n = collect();
        if (n.type == 1 && n.sample.empty()) n.type = 0;
        play_beep_sound(n);
    };
    auto put_output = [=](bool archive) {
        if (!device) return;
        // (PulseAudio and PipeWire keep the device and the volume themselves: Use and Save
        // set them alike)
        (void)archive;
        const std::string &name = out->sinks[device->active()].name;
        if (device->active() != out->current) run({"pactl", "set-default-sink", name});
        if (master->value() != out->volume) run({"pactl", "set-sink-volume", name, std::to_string(master->value()) + "%"});
        if (mute->on() != out->mute) run({"pactl", "set-sink-mute", name, mute->on() ? "1" : "0"});
    };

    auto *w = new lune::Window("Sound Preferences", root);
    w->id("SOUNDPREFS");
    g_sound_editor = w;
    auto close = [w, standalone] {
        w->hide();
        Fl::delete_widget(w);
        g_sound_editor = nullptr;
        if (standalone) exit(0);
    };
    save->on_press = [=] { put_beep(collect(), true); put_output(true); close(); };
    use->on_press = [=] { put_beep(collect(), false); put_output(false); close(); };
    cancel->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    {   // (opened wide enough for the sliders)
        int minw = 0, minh = 0, maxw = 0, maxh = 0;
        w->get_size_range(&minw, &minh, &maxw, &maxh);
        if (minw > 0) w->size(std::max(minw, 420), std::max(minh, w->h()));
    }
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace rambler
