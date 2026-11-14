/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The GUI settings (the Zune preferences): the screen's palette, the font and the frames of
    buttons and groups. Use and Save put them into effect in every window at once; the window
    manager takes the palette when it starts.
*/
#include "guisettings.h"
#include "zune.h"

#include <FL/Fl.H>

#include <cstdio>

using namespace zune;

namespace wanderer {

namespace {

struct Palette { const char *name; unsigned bg, shadow, shine, fill; };
const Palette PALETTES[] = {
    {"Default", 0xAAAAAA, 0x000000, 0xFFFFFF, 0x6688BB},
    {"Warm", 0xB8AE9C, 0x2A2118, 0xFFF8EC, 0xB0704A},
    {"Cool", 0x9FB0BF, 0x101822, 0xF4FAFF, 0x4A78A8},
    {"Green", 0xA2B3A0, 0x101C10, 0xF4FFF2, 0x4F8A55},
    {"Contrast", 0xC0C0C0, 0x000000, 0xFFFFFF, 0x000080},
};
const int NPAL = sizeof PALETTES / sizeof PALETTES[0];

struct FrameChoice { const char *name; const char *button, *group; };
const FrameChoice BUTTONS[] = {{"Bevel", "202211", nullptr}, {"Window bevel", "602211", nullptr},
                               {"Semi-round", "A02211", nullptr}, {"Round", "502211", nullptr},
                               {"Thick border", "402211", nullptr}};
const FrameChoice GROUPS[] = {{"Thin border", nullptr, "314444"}, {"Bevel", nullptr, "214444"},
                              {"Grey border", nullptr, "914444"}, {"Rounded", nullptr, "B14444"},
                              {"Round thin border", nullptr, "814444"}};
const int SIZES[] = {11, 12, 13, 14, 15, 16, 18};

std::string hex(unsigned v) { char b[8]; snprintf(b, sizeof b, "%06X", v); return b; }

Window *g_win = nullptr;

}  // namespace

void edit_gui_settings() {
    if (g_win) { g_win->show(); return; }
    Prefs saved = prefs();   // Cancel puts these back
    auto *root = VGroup();
    auto *cols = HGroup();
    // -- the palette
    auto *pal = new Group(Group::Vertical, Frame_Group);
    pal->frame_title("Screen Colours");
    pal->columns(2);
    int pi = 0;
    for (int i = 0; i < NPAL; i++)
        if (PALETTES[i].bg == saved.background && PALETTES[i].fill == saved.fill) pi = i;
    new Text("\033rScheme:");
    auto *scheme = new Cycle({PALETTES[0].name, PALETTES[1].name, PALETTES[2].name, PALETTES[3].name, PALETTES[4].name}, pi);
    new Text("\033rBackground:");
    auto *cbg = new String(hex(saved.background), 6);
    new Text("\033rShine:");
    auto *csh = new String(hex(saved.shine), 6);
    new Text("\033rShadow / text:");
    auto *csd = new String(hex(saved.shadow), 6);
    new Text("\033rFill:");
    auto *cfl = new String(hex(saved.fill), 6);
    pal->end();
    // -- the look
    auto *look = new Group(Group::Vertical, Frame_Group);
    look->frame_title("Look");
    look->columns(2);
    int bi = 0, gi = 0, si = 2;
    for (int i = 0; i < 5; i++) {
        if (saved.button_frame == BUTTONS[i].button) bi = i;
        if (saved.group_frame == GROUPS[i].group) gi = i;
    }
    for (int i = 0; i < 7; i++) if (SIZES[i] == saved.font_size) si = i;
    new Text("\033rFont:");
    auto *fam = new Cycle({"Sans", "Serif", "Fixed"}, saved.font_family);
    new Text("\033rFont size:");
    auto *size = new Cycle({"11", "12", "13", "14", "15", "16", "18"}, si);
    new Text("\033rButtons:");
    auto *bfr = new Cycle({BUTTONS[0].name, BUTTONS[1].name, BUTTONS[2].name, BUTTONS[3].name, BUTTONS[4].name}, bi);
    new Text("\033rGroups:");
    auto *gfr = new Cycle({GROUPS[0].name, GROUPS[1].name, GROUPS[2].name, GROUPS[3].name, GROUPS[4].name}, gi);
    VSpace();
    VSpace();
    look->end();
    cols->end();
    new Text("\033cThe window manager takes the colours when the session starts again.");
    auto *buttons = HGroup();
    buttons->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    buttons->end();
    root->end();

    auto collect = [=] {
        Prefs &p = prefs();
        auto parse = [](String *s, unsigned def) {
            char *end = nullptr;
            unsigned long v = strtoul(s->value(), &end, 16);
            return end && *end == 0 && *s->value() ? (unsigned)v : def;
        };
        p.background = parse(cbg, p.background);
        p.shine = parse(csh, p.shine);
        p.shadow = parse(csd, p.shadow);
        p.fill = parse(cfl, p.fill);
        p.font_family = fam->active();
        p.font_size = SIZES[size->active()];
        p.button_frame = BUTTONS[bfr->active()].button;
        p.group_frame = GROUPS[gfr->active()].group;
    };
    // a scheme fills the four colours in
    scheme->on_change = [=](int i) {
        cbg->value(hex(PALETTES[i].bg));
        csh->value(hex(PALETTES[i].shine));
        csd->value(hex(PALETTES[i].shadow));
        cfl->value(hex(PALETTES[i].fill));
    };
    auto *w = new Window("GUI Settings", root);
    g_win = w;
    auto close = [w] { w->hide(); Fl::delete_widget(w); g_win = nullptr; };
    save->on_press = [=] { collect(); save_prefs(); apply_prefs(); close(); };
    use->on_press = [=] { collect(); apply_prefs(); close(); };
    cancel->on_press = [=] { prefs() = saved; apply_prefs(); close(); };
    w->callback([](Fl_Widget *win, void *) { win->hide(); Fl::delete_widget(win); g_win = nullptr; });
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    w->position(sx + (sw - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace wanderer
