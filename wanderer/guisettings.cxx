/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The GUI settings (the Zune preferences, prefs/Zune): pages for the windows (fonts,
    spacing), groups, buttons, strings, listviews, scrollbars and the screen's palette. Use and Save put them into effect in every window at once; the window
    manager takes the palette when it starts.
*/
#include "guisettings.h"
#include "zune.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

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

// the frame types (frame.c), by their names in the frame adjust window
const char *const FRAME_NAMES[FST_COUNT] = {"None", "Rectangle", "Bevel", "Thin border", "Thick border",
                                            "Round bevel", "Window bevel", "Round thick border",
                                            "Round thin border", "Grey border", "Semi-round bevel", "Rounded"};
const int SIZES[] = {11, 12, 13, 14, 15, 16, 18};

// a frame setting: a cycle of the types; the state and the inner spacing stay the kind's own
struct FrameSetting {
    StdFrame kind;
    Cycle *cycle;
    std::string spec() const {
        std::string cur = prefs().frame[kind].empty() ? default_frame(kind) : prefs().frame[kind];
        const char hexd[] = "0123456789ABCDEF";
        cur[0] = hexd[cycle->active()];
        return cur;
    }
};

Cycle *frame_cycle(StdFrame kind) {
    std::vector<std::string> names(FRAME_NAMES, FRAME_NAMES + FST_COUNT);
    return new Cycle(names, std_frame(kind).type);
}

Slider *number(int v) { return new Slider(0, 20, v); }   // the spacing: sliders as in the prefs program

std::string hex(unsigned v) { char b[8]; snprintf(b, sizeof b, "%06X", v); return b; }

Window *g_win = nullptr;

}  // namespace

// The Zune preferences (prefs/Zune/zune.c): the list of pages on the left, the page on the
// right in a text frame; Save / Use / Cancel (the prefs window)
void edit_gui_settings() {
    if (g_win) { g_win->show(); return; }
    Prefs saved = prefs();   // Cancel puts these back
    std::vector<FrameSetting> frames;
    auto *root = VGroup();
    root->vspacing(10);
    auto *main = HGroup();
    const char *pages[] = {"Windows", "Groups", "Buttons", "Cycles", "Strings", "Listviews", "Scrollbars", "Palette"};
    auto *list = new List();
    list->frame(Frame_InputList);
    {
        fl_font(prefs().font, prefs().font_size);
        int wmax = 0;
        for (auto *p : pages) wmax = std::max(wmax, (int)fl_width(tr(p).c_str()));
        list->fixed_width(wmax + 24);
    }
    for (auto *p : pages) list->add({tr(p)});
    list->done();
    auto *pageframe = new Group(Group::Vertical, Frame_Text);
    pageframe->inner(6, 6, 6, 6);
    auto *book = new Register({});
    // -- Windows: the fonts, the spacing (windowp.c)
    Slider *wl, *wr, *wt, *wb;
    Cycle *fam, *size;
    {
        auto *pg = VGroup();
        auto *fonts = new Group(Group::Vertical, Frame_Group);
        fonts->frame_title("Fonts");
        fonts->columns(2);
        int si = 2;
        for (int i = 0; i < 7; i++) if (SIZES[i] == saved.font_size) si = i;
        new Text("\033rNormal:");
        fam = new Cycle({"Sans", "Serif", "Fixed"}, saved.font_family);
        new Text("\033rSize:");
        size = new Cycle({"11", "12", "13", "14", "15", "16", "18"}, si);
        fonts->end();
        auto *sp = new Group(Group::Vertical, Frame_Group);
        sp->frame_title("Spacing");
        sp->columns(4);
        new Text("\033rL"); wl = number(saved.window_left);
        new Text("\033rT"); wt = number(saved.window_top);
        new Text("\033rR"); wr = number(saved.window_right);
        new Text("\033rB"); wb = number(saved.window_bottom);
        sp->end();
        VSpace();
        pg->end();
    }
    // -- Groups: the frame, the spacing (groupsp.c)
    Slider *gh, *gv;
    {
        auto *pg = VGroup();
        auto *fr = new Group(Group::Vertical, Frame_Group);
        fr->frame_title("Frame");
        fr->columns(2);
        new Text("\033rNormal:");
        frames.push_back({Frame_Group, frame_cycle(Frame_Group)});
        new Text("\033rVirtual:");
        frames.push_back({Frame_Virtual, frame_cycle(Frame_Virtual)});
        fr->end();
        auto *sp = new Group(Group::Vertical, Frame_Group);
        sp->frame_title("Spacing");
        sp->columns(2);
        new Text("\033rHorizontal:"); gh = number(saved.group_hspacing);
        new Text("\033rVertical:"); gv = number(saved.group_vspacing);
        sp->end();
        VSpace();
        pg->end();
    }
    // -- Buttons (buttonsp.c)
    {
        auto *pg = VGroup();
        auto *tb = new Group(Group::Vertical, Frame_Group);
        tb->frame_title("Text Buttons");
        tb->columns(2);
        new Text("\033rFrame:");
        frames.push_back({Frame_Button, frame_cycle(Frame_Button)});
        tb->end();
        auto *ib = new Group(Group::Vertical, Frame_Group);
        ib->frame_title("Image Buttons");
        ib->columns(2);
        new Text("\033rFrame:");
        frames.push_back({Frame_ImageButton, frame_cycle(Frame_ImageButton)});
        ib->end();
        auto *tx = new Group(Group::Vertical, Frame_Group);
        tx->frame_title("Texts");
        tx->columns(2);
        new Text("\033rFrame:");
        frames.push_back({Frame_Text, frame_cycle(Frame_Text)});
        tx->end();
        VSpace();
        pg->end();
    }
    // -- Cycles (cyclesp.c): the pop-up menu
    Cycle *cpos;
    Slider *cmin;
    Checkmark *crec;
    {
        auto *pg = VGroup();
        auto *g = new Group(Group::Vertical, Frame_Group);
        g->frame_title("Menu");
        g->columns(2);
        new Text("\033rPosition:");
        cpos = new Cycle({"Below", "On Active"}, saved.cycle_menu_position);
        new Text("\033rLevel:");
        cmin = new Slider(2, 20, saved.cycle_menu_min);
        new Text("\033rRecessed entries:");
        crec = new Checkmark(saved.cycle_menu_recessed);
        g->end();
        VSpace();
        pg->end();
    }
    // -- Strings (stringsp.c)
    {
        auto *pg = VGroup();
        auto *g = new Group(Group::Vertical, Frame_Group);
        g->frame_title("Frame");
        g->columns(2);
        new Text("\033rStrings:");
        frames.push_back({Frame_String, frame_cycle(Frame_String)});
        new Text("\033rPop-ups:");
        frames.push_back({Frame_PopUp, frame_cycle(Frame_PopUp)});
        g->end();
        VSpace();
        pg->end();
    }
    // -- Listviews (listviewsp.c)
    {
        auto *pg = VGroup();
        auto *g = new Group(Group::Vertical, Frame_Group);
        g->frame_title("Frames");
        g->columns(2);
        new Text("\033rInput list:");
        frames.push_back({Frame_InputList, frame_cycle(Frame_InputList)});
        new Text("\033rRead list:");
        frames.push_back({Frame_ReadList, frame_cycle(Frame_ReadList)});
        g->end();
        VSpace();
        pg->end();
    }
    // -- Scrollbars and gauges (scrollbarsp.c)
    {
        auto *pg = VGroup();
        auto *g = new Group(Group::Vertical, Frame_Group);
        g->frame_title("Frames");
        g->columns(2);
        new Text("\033rScrollbar:");
        frames.push_back({Frame_Prop, frame_cycle(Frame_Prop)});
        new Text("\033rGauge:");
        frames.push_back({Frame_Gauge, frame_cycle(Frame_Gauge)});
        g->end();
        VSpace();
        pg->end();
    }
    // -- Palette (the screen's four colours: the palette preferences)
    Cycle *scheme;
    String *cbg, *csh, *csd, *cfl;
    {
        auto *pg = VGroup();
        auto *pal = new Group(Group::Vertical, Frame_Group);
        pal->frame_title("Screen Colours");
        pal->columns(2);
        int pi = 0;
        for (int i = 0; i < NPAL; i++)
            if (PALETTES[i].bg == saved.background && PALETTES[i].fill == saved.fill) pi = i;
        new Text("\033rScheme:");
        scheme = new Cycle({PALETTES[0].name, PALETTES[1].name, PALETTES[2].name, PALETTES[3].name, PALETTES[4].name}, pi);
        new Text("\033rBackground:");
        cbg = new String(hex(saved.background), 6);
        new Text("\033rShine:");
        csh = new String(hex(saved.shine), 6);
        new Text("\033rShadow / text:");
        csd = new String(hex(saved.shadow), 6);
        new Text("\033rFill:");
        cfl = new String(hex(saved.fill), 6);
        pal->end();
        new Text("\033cThe colours apply at once, also to the window frames and the screens.");
        VSpace();
        pg->end();
    }
    book->end();
    pageframe->end();
    main->end();
    auto *buttons = HGroup();
    buttons->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    buttons->end();
    root->end();

    list->on_active = [book](int i) { book->active(i); };
    list->active(0);
    auto collect = [=] {
        Prefs &p = prefs();
        auto parse = [](String *s, unsigned def) {
            char *end = nullptr;
            unsigned long v = strtoul(s->value(), &end, 16);
            return end && *end == 0 && *s->value() ? (unsigned)v : def;
        };
        std::vector<std::pair<StdFrame, std::string>> specs;
        for (auto &f : frames) specs.push_back({f.kind, f.spec()});
        for (auto &sp : specs) p.frame[sp.first] = sp.second == default_frame(sp.first) ? "" : sp.second;
        p.background = parse(cbg, p.background);
        p.shine = parse(csh, p.shine);
        p.shadow = parse(csd, p.shadow);
        p.fill = parse(cfl, p.fill);
        p.font_family = fam->active();
        p.font_size = SIZES[size->active()];
        p.window_left = wl->value();
        p.window_right = wr->value();
        p.window_top = wt->value();
        p.window_bottom = wb->value();
        p.group_hspacing = gh->value();
        p.cycle_menu_position = cpos->active();
        p.cycle_menu_min = cmin->value();
        p.cycle_menu_recessed = crec->on();
        p.group_vspacing = gv->value();
    };
    // a scheme fills the four colours in
    scheme->on_change = [=](int i) {
        cbg->value(hex(PALETTES[i].bg));
        csh->value(hex(PALETTES[i].shine));
        csd->value(hex(PALETTES[i].shadow));
        cfl->value(hex(PALETTES[i].fill));
    };
    auto *w = new Window("Zune Preferences", root);
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
