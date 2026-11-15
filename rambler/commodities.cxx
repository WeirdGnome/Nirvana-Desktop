/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The commodities and Exchange: see commodities.h.
*/
#include "commodities.h"
#include "lune.h"
#include "catalog.h"
#include "tools.h"

#include <FL/Fl.H>
#include <FL/platform.H>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

namespace {
std::string saved_file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/rambler/commodities.prefs";
}

// a program run and waited for (its output thrown away)
void run(const std::vector<std::string> &argv) {
    pid_t pid = fork();
    if (pid == 0) {
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        std::vector<char *> a;
        for (auto &s : argv) a.push_back((char *)s.c_str());
        a.push_back(nullptr);
        execvp(a[0], a.data());
        _exit(127);
    }
    if (pid > 0) { int st; waitpid(pid, &st, 0); }
}
}  // namespace

Commodities Commodities::load() {
    Commodities c;
    std::ifstream in(env_dir() + "/commodities.prefs");
    if (!in) in.open(saved_file());
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        int v = atoi(line.c_str() + eq + 1);
        if (k == "click_to_front") c.click_to_front = v;
        else if (k == "ctf_key") c.ctf_key = std::clamp(v, 0, 3);
        else if (k == "autopoint") c.autopoint = v;
        else if (k == "blanker") c.blanker = v;
        else if (k == "blank_minutes") c.blank_minutes = std::clamp(v, 1, 120);
        else if (k == "no_caps_lock") c.no_caps_lock = v;
    }
    return c;
}

std::string Commodities::text() const {
    std::ostringstream o;
    o << "click_to_front=" << click_to_front << "\nctf_key=" << ctf_key << "\nautopoint=" << autopoint
      << "\nblanker=" << blanker << "\nblank_minutes=" << blank_minutes << "\nno_caps_lock=" << no_caps_lock << "\n";
    return o.str();
}

void Commodities::save() const {
    std::string t = text();
    for (const std::string &f : {env_dir() + "/commodities.prefs", saved_file()}) {
        std::error_code ec;
        fs::create_directories(fs::path(f).parent_path(), ec);
        std::ofstream(f) << t;
    }
    if (fl_display) {
        Atom prop = XInternAtom(fl_display, "_RAMBLER_COMMODITIES", False), utf8 = XInternAtom(fl_display, "UTF8_STRING", False);
        XChangeProperty(fl_display, RootWindow(fl_display, fl_screen), prop, utf8, 8, PropModeReplace,
                        (const unsigned char *)t.c_str(), (int)t.size());
        XFlush(fl_display);
    }
}

void Commodities::apply() const {
    // Blanker: X's own screen saver, blanking (not a pattern)
    if (blanker) {
        run({"xset", "s", std::to_string(blank_minutes * 60), "0"});
        run({"xset", "s", "blank"});
    } else run({"xset", "s", "off"});
    // NoCapsLock: the key no longer locks (the lock modifier without it); back again
    if (no_caps_lock) run({"xmodmap", "-e", "clear lock"});
    else run({"xmodmap", "-e", "clear lock", "-e", "add lock = Caps_Lock"});
}

// ---------------------------------------------------------------------------------------------
// Exchange: the list of the commodities, the one chosen with its title and description,
// Active / Inactive, Show / Hide Interface
namespace {
struct Info { const char *name, *title, *description; bool has_interface; };
const Info INFO[] = {
    {"AutoPoint", "AutoPoint", "The window under the pointer becomes active.", false},
    {"Blanker", "Blanker", "Blanks the screen after some minutes without input.", true},
    {"ClickToFront", "ClickToFront", "A double click brings a window to the front.", true},
    {"NoCapsLock", "NoCapsLock", "The Caps Lock key does not lock.", false},
};
const int N = sizeof INFO / sizeof INFO[0];

bool &active_of(Commodities &c, int i) {
    switch (i) {
    case 0: return c.autopoint;
    case 1: return c.blanker;
    case 2: return c.click_to_front;
    default: return c.no_caps_lock;
    }
}

lune::Window *g_iface[N] = {};

// a commodity's own window (its settings, taking effect at once)
void show_interface(int i, std::shared_ptr<Commodities> c) {
    if (g_iface[i]) { g_iface[i]->show(); return; }
    auto *root = VGroup();
    auto *g = new Group(Group::Vertical, Frame_Group);
    g->columns(2);
    std::function<void()> collect;
    if (i == 1) {
        new lune::Text("\033rTimeout:");
        auto *s = new Slider(1, 120, c->blank_minutes);
        s->format = [](int v) { return std::to_string(v) + " min"; };
        s->on_change = [c](int v) { c->blank_minutes = v; };
        collect = [c, s] { c->blank_minutes = s->value(); };
    } else {
        new lune::Text("\033rBring to front with:");
        auto *k = new Cycle({"Double click", "Alt + click", "Ctrl + click", "Shift + click"}, c->ctf_key);
        collect = [c, k] { c->ctf_key = k->active(); };
    }
    g->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *use = new Button("Use");
    HSpace();
    auto *hide = new Button("Hide");
    btns->end();
    root->end();
    auto *w = new lune::Window(INFO[i].title, root);
    w->id(std::string("CX_") + INFO[i].name);
    g_iface[i] = w;
    auto close = [w, i] { w->hide(); Fl::delete_widget(w); g_iface[i] = nullptr; };
    use->on_press = [=] { collect(); c->save(); c->apply(); };
    hide->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    w->size(std::max(w->w(), 340), w->h());
    w->show();
}
}  // namespace

void run_exchange() {
    auto c = std::make_shared<Commodities>(Commodities::load());
    auto *root = VGroup();
    auto *top = HGroup();
    auto *lg = new Group(Group::Vertical, Frame_Group);
    lg->frame_title("Commodities");
    auto *list = new List();
    list->frame(Frame_InputList);
    for (auto &in : INFO) list->add({in.name});
    list->done();
    lg->end();
    auto *ig = new Group(Group::Vertical, Frame_Group);
    ig->frame_title("Information");
    auto *title = new lune::Text("", Frame_Text);
    auto *desc = new lune::Text("", Frame_Text);
    desc->set_max(false);
    auto *state = new Cycle({"Active", "Inactive"}, 0);
    auto *brow = HGroup();
    brow->same_width(true);
    auto *showi = new Button("Show Interface");
    auto *hidei = new Button("Hide Interface");
    brow->end();
    HVSpace();
    ig->end();
    top->end();
    root->end();
    auto *w = new lune::Window("Exchange", root);
    w->id("EXCHANGE");

    auto show = [=] {
        int i = list->active();
        bool any = i >= 0 && i < N;
        title->contents(any ? INFO[i].title : "");
        desc->contents(any ? tr(INFO[i].description) : "");
        if (any) state->active(active_of(*c, i) ? 0 : 1);
        state->disabled(!any);
        showi->disabled(!any || !INFO[i].has_interface);
        hidei->disabled(!any || !INFO[i].has_interface);
        root->redraw();
    };
    list->on_active = [=](int) { show(); };
    state->on_change = [=](int v) {
        int i = list->active();
        if (i < 0) return;
        active_of(*c, i) = v == 0;
        c->save();
        c->apply();
    };
    showi->on_press = [=] { if (list->active() >= 0) show_interface(list->active(), c); };
    hidei->on_press = [=] {
        int i = list->active();
        if (i >= 0 && g_iface[i]) { g_iface[i]->hide(); Fl::delete_widget(g_iface[i]); g_iface[i] = nullptr; }
    };
    list->active(0);
    show();

    w->menustrip({
        {"Project", 0, 0, 0, 0, {
            {"Hide", 1, 0, 'h'},
            {"Quit", 2, 0, 'q'},
        }},
    });
    w->on_menu = [](int id) { if (id == 1 || id == 2) exit(0); };
    w->callback([](Fl_Widget *, void *) { exit(0); });
    w->size(std::max(w->w(), 520), std::max(w->h(), 240));
    w->show();
}

}  // namespace rambler
