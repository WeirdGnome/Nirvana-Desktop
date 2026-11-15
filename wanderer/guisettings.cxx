/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The GUI settings (the Zune preferences, prefs/Zune): pages for the windows (fonts,
    spacing), groups, buttons, strings, listviews, scrollbars and the screen's palette. Use and Save put them into effect in every window at once; the window
    manager takes the palette when it starts.
*/
#include "guisettings.h"
#include "zune.h"
#include "tools.h"
#include <map>
#include <FL/platform.H>
#include <FL/Fl_RGB_Image.H>
#include "icons.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <cstdio>
#include <memory>
#include <FL/fl_ask.H>

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
        // the type as zune_frame_intern_to_spec writes it: 0-9, then a-z (the custom frames)
        int t = cycle->active();
        cur[0] = t < 10 ? char('0' + t) : char('a' + t - 10);
        return cur;
    }
};

Cycle *frame_cycle(StdFrame kind) {
    std::vector<std::string> names(FRAME_NAMES, FRAME_NAMES + FST_COUNT);
    for (int i = 1; i <= 16; i++) names.push_back(trs("Custom #%s", std::to_string(i)));
    return new Cycle(names, std_frame(kind).type);
}

Slider *number(int v) { return new Slider(0, 20, v); }   // the spacing: sliders as in the prefs program

std::string hex(unsigned v) { char b[8]; snprintf(b, sizeof b, "%06X", v); return b; }

zune::Window *g_win = nullptr;

// an image of the Special page (a popimage: 28 pixels, in a button frame)
class SpecialImage : public Area {
public:
    explicit SpecialImage(std::function<void(int, int, int, int)> f) : Area(Frame_ImageButton), f_(std::move(f)) {}
    void painter(std::function<void(int, int, int, int)> f) { f_ = std::move(f); redraw(); }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override {
        minw = defw = maxw = minh = defh = maxh = 20;   // (MUIA_MaxWidth / MaxHeight 28 with the frame)
    }

protected:
    void draw_contents() override { f_(mleft(), mtop(), mwidth(), mheight()); }
    std::function<void(int, int, int, int)> f_;
};

// MakeSingleRadio: a radio button image without a frame (MUII_RadioButton)
class RadioMark : public Checkmark {
public:
    explicit RadioMark(bool on) : Checkmark(on) { frame(Frame_None); }

protected:
    void draw_contents() override {
        int d = std::min(mwidth(), mheight()) - 1, X = mleft() + (mwidth() - d) / 2, Y = mtop() + (mheight() - d) / 2;
        fl_color(pens()[MPEN_SHADOW]);
        fl_arc(X, Y, d, d, 45, 225);
        fl_color(pens()[MPEN_SHINE]);
        fl_arc(X, Y, d, d, 225, 405);
        if (selected_) { fl_color(pens()[MPEN_TEXT]); fl_pie(X + d / 4, Y + d / 4, d / 2 + 1, d / 2 + 1, 0, 360); }
    }
};

// a picture of the icons, made small (as the device images)
void draw_small_icon(IconKind k, int X, int Y, int W, int H) {
    static std::map<int, Fl_RGB_Image *> cache;
    Fl_RGB_Image *&img = cache[(int)k];
    if (!img) {
        Fl_Offscreen off = fl_create_offscreen(ICON_W, ICON_H);
        fl_begin_offscreen(off);
        fl_color(fl_rgb_color(255, 0, 255));
        fl_rectf(0, 0, ICON_W, ICON_H);
        draw_icon(k, 0, 0, false);
        uchar *rgb = fl_read_image(nullptr, 0, 0, ICON_W, ICON_H);
        fl_end_offscreen();
        fl_delete_offscreen(off);
        auto *rgba = new uchar[ICON_W * ICON_H * 4];
        for (int i = 0; i < ICON_W * ICON_H; i++) {
            bool key = rgb[i * 3] == 255 && rgb[i * 3 + 1] == 0 && rgb[i * 3 + 2] == 255;
            for (int c = 0; c < 3; c++) rgba[i * 4 + c] = key ? 0 : rgb[i * 3 + c];
            rgba[i * 4 + 3] = key ? 0 : 255;
        }
        delete[] rgb;
        Fl_RGB_Image full(rgba, ICON_W, ICON_H, 4);
        full.alloc_array = 1;
        Fl_RGB_Scaling old = Fl_Image::RGB_scaling();
        Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
        img = (Fl_RGB_Image *)full.copy(18, 18 * ICON_H / ICON_W);
        Fl_Image::RGB_scaling(old);
    }
    img->draw(X + (W - img->w()) / 2, Y + (H - img->h()) / 2);
}

// the tape deck's images: play, play back, pause, stop, record, up, down (in the text pen;
// record in red)
void draw_tape(int which, int X, int Y, int W, int H) {
    int cx = X + W / 2, cy = Y + H / 2, r = std::min(W, H) / 2 - 3;
    fl_color(pens()[MPEN_TEXT]);
    switch (which) {
    case 0: fl_polygon(cx - r / 2, cy - r, cx - r / 2, cy + r, cx + r, cy); break;
    case 1: fl_polygon(cx + r / 2, cy - r, cx + r / 2, cy + r, cx - r, cy); break;
    case 2: fl_rectf(cx - r + 1, cy - r, r * 2 / 3, 2 * r); fl_rectf(cx + r / 3, cy - r, r * 2 / 3, 2 * r); break;
    case 3: fl_rectf(cx - r + 1, cy - r + 1, 2 * r - 1, 2 * r - 1); break;
    case 4: fl_color(fl_rgb_color(0xCC, 0x22, 0x22)); fl_pie(cx - r, cy - r, 2 * r, 2 * r, 0, 360); break;
    case 5: fl_polygon(cx - r, cy + r / 2, cx + r, cy + r / 2, cx, cy - r); break;
    case 6: fl_polygon(cx - r, cy - r / 2, cx + r, cy - r / 2, cx, cy + r); break;
    }
}

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
    const char *pages[] = {"System", "Windows", "Groups", "Buttons", "Cycles", "Sliders", "Scrollbars", "Listviews", "Strings", "Navigation", "Special", "Frames", "Palette"};
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
    // -- System (systemp.c): the bubble help's delays (the public screen, iconification and
    // ARexx groups have nothing to set on X11)
    Slider *bfirst, *bnext;
    {
        auto *pg = VGroup();
        auto *g = new Group(Group::Vertical, Frame_Group);
        g->frame_title("Bubble Help");
        g->columns(2);
        auto secs = [](int v) { char b[16]; snprintf(b, sizeof b, "%.1f s", 0.1 * v); return std::string(b); };
        new Text("\033rFirst bubble:");
        bfirst = new Slider(0, 50, saved.bubble_first);
        bfirst->format = secs;
        new Text("\033rNext bubble:");
        bnext = new Slider(0, 50, saved.bubble_next);
        bnext->format = secs;
        g->end();
        VSpace();
        pg->end();
    }
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
    // -- Sliders (slidersp.c): the container's frame, the knob's frame; example sliders
    {
        auto *pg = VGroup();
        auto *top = HGroup();
        top->weight(800);
        auto *cont = new Group(Group::Vertical, Frame_Group);
        cont->frame_title("Container Design");
        cont->columns(2);
        new Text("\033rFrame:");
        frames.push_back({Frame_Slider, frame_cycle(Frame_Slider)});
        cont->end();
        auto *knob = new Group(Group::Vertical, Frame_Group);
        knob->frame_title("Knob Design");
        knob->columns(2);
        new Text("\033rFrame:");
        frames.push_back({Frame_Knob, frame_cycle(Frame_Knob)});
        knob->end();
        top->end();
        auto *ex = new Group(Group::Vertical, Frame_Group);
        ex->frame_title("Example Sliders");
        VSpace();
        auto *row = HGroup();
        auto *col = VGroup();
        new Slider(0, 9, 5);     // MakeSmallHorizSlider
        new Slider(0, 99, 42);   // MakeBigHorizSlider
        col->end();
        new Slider(0, 9, 5, false);
        new Slider(0, 99, 42, false);
        row->end();
        HVSpace();
        ex->end();
        pg->end();
    }
    // -- Scrollbars (scrollbarsp.c): the arrows, the bar, the frame, the arrangement
    Cycle *sbtype;
    auto sbarr = std::make_shared<int>(saved.scrollbar_arrangement);
    {
        auto *pg = HGroup();
        auto *left = VGroup();
        auto *ar = new Group(Group::Vertical, Frame_Group);
        ar->frame_title("Arrows");
        auto *ac = new Group(Group::Vertical);
        ac->columns(4);
        ac->same_height(true);
        auto arrow_image = [](Arrow::Dir d, const char *title) {
            auto *im = new SpecialImage([d](int X, int Y, int W, int H) {
                fl_color(pens()[MPEN_TEXT]);
                Arrow::draw_arrow(d, X, Y, W, H);
            });
            im->copy_tooltip(tr(title).c_str());
        };
        new Text("\033rUp"); arrow_image(Arrow::Up, "Arrow up"); arrow_image(Arrow::Down, "Arrow down"); new Text("\033lDown");
        new Text("\033rLeft"); arrow_image(Arrow::Left, "Arrow left"); arrow_image(Arrow::Right, "Arrow right"); new Text("\033lRight");
        ac->end();
        ar->end();
        auto *bar = new Group(Group::Vertical, Frame_Group);
        bar->frame_title("Bar");
        auto *tr_ = HGroup();
        new Text("\033rGadget Type:");
        sbtype = new Cycle({"Standard", "NewLook", "Custom"}, saved.scrollbar_type);
        tr_->end();
        bar->end();
        left->end();
        auto *right = VGroup();
        auto *fr = new Group(Group::Vertical, Frame_Group);
        fr->frame_title("Frame");
        frames.push_back({Frame_Prop, frame_cycle(Frame_Prop)});
        fr->end();
        auto *arr = new Group(Group::Vertical, Frame_Group);
        arr->frame_title("Arrangement");
        arr->columns(3);
        RadioMark *radios[3];
        for (int i = 0; i < 3; i++) radios[i] = new RadioMark(*sbarr == i);
        for (int t : {Scrollbar::Top, Scrollbar::Sym, Scrollbar::Bottom}) {
            auto *sb = new Scrollbar(false, (Scrollbar::Type)t);
            sb->set(10, 4, 3);   // MUIA_Prop_Entries 10, Visible 4, First 3
        }
        arr->end();
        for (int i = 0; i < 3; i++) {
            RadioMark *r0 = radios[0], *r1 = radios[1], *r2 = radios[2];
            radios[i]->on_press = [i, sbarr, r0, r1, r2] {
                *sbarr = i;
                RadioMark *all[3] = {r0, r1, r2};
                for (int k = 0; k < 3; k++) all[k]->on(k == i);
            };
        }
        right->end();
        pg->end();
    }
    // -- Listviews (listviewsp.c): control, fonts, design (frames and backgrounds), cursor
    Cycle *lmulti, *lrefresh, *lfam, *lsize, *fsize;
    Checkmark *lsmooth;
    Slider *lsmoothval, *llead;
    Cycle *lback, *rback, *lcur, *lsel, *lselcur;
    {
        auto *pg = new Group(Group::Vertical);
        pg->columns(2);
        pg->same_size(true);
        auto *ctl = new Group(Group::Vertical, Frame_Group);
        ctl->frame_title("Control");
        VSpace();
        auto *cc = new Group(Group::Vertical);
        cc->columns(2);
        new Text("\033rMulti:");
        lmulti = new Cycle({"Shifted", "Always"}, saved.list_multi);
        new Text("\033rRefresh:");
        lrefresh = new Cycle({"Linear", "Mixed"}, saved.list_refresh);
        new Text("\033rSmooth:");
        auto *sm = HGroup();
        lsmooth = new Checkmark(saved.list_smoothed);
        lsmoothval = new Slider(0, 9, saved.list_smoothval);
        sm->end();
        cc->end();
        VSpace();
        ctl->end();
        lsmoothval->disabled(!saved.list_smoothed);
        lsmooth->on_press = [=] { lsmoothval->disabled(!lsmooth->on()); };
        auto *fo = new Group(Group::Vertical, Frame_Group);
        fo->frame_title("Fonts");
        VSpace();
        auto *fc = new Group(Group::Vertical);
        fc->columns(2);
        auto size_index = [](int v) { for (int i = 0; i < 7; i++) if (SIZES[i] == v) return i + 1; return 0; };
        std::vector<std::string> sizes = {"Normal"};
        for (int sz : SIZES) sizes.push_back(std::to_string(sz));
        new Text("\033rNormal:");
        auto *nr = HGroup();
        lfam = new Cycle({"Normal", "Sans", "Serif", "Fixed"}, saved.list_font_family + 1);
        lsize = new Cycle(sizes, size_index(saved.list_font_size));
        nr->end();
        new Text("\033rFixed:");
        fsize = new Cycle(sizes, size_index(saved.fixed_font_size));
        new Text("\033rLeading:");
        llead = new Slider(0, 9, saved.list_leading);
        fc->end();
        VSpace();
        fo->end();
        // a background: the pattern's sample and its name (the popimage of the original)
        auto pattern = [](int muii) {
            auto *row = HGroup();
            row->spacing(2);
            auto *sample = new SpecialImage([](int, int, int, int) {});
            std::vector<std::string> names(PATTERN_NAMES, PATTERN_NAMES + (MUII_LASTPAT - MUII_BACKGROUND));
            auto *c = new Cycle(names, muii - MUII_BACKGROUND);
            row->end();
            sample->painter([c](int X, int Y, int W, int H) { draw_pattern(MUII_BACKGROUND + c->active(), X, Y, W, H); });
            c->on_change = [sample](int) { sample->redraw(); };
            return c;
        };
        auto *de = new Group(Group::Vertical, Frame_Group);
        de->frame_title("Design");
        de->columns(3);
        new Text("\033rInput Lists:");
        frames.push_back({Frame_InputList, frame_cycle(Frame_InputList)});
        lback = pattern(saved.list_back);
        new Text("\033rRead-only Lists:");
        frames.push_back({Frame_ReadList, frame_cycle(Frame_ReadList)});
        rback = pattern(saved.readlist_back);
        VSpace();
        new Text("\033cFrame");
        new Text("\033cBackground");
        de->end();
        auto *cu = new Group(Group::Vertical, Frame_Group);
        cu->frame_title("Cursor");
        cu->columns(3);
        new Text("\033rActive:");
        lcur = pattern(saved.list_cursor);
        auto *asl = VGroup();
        HVSpace();
        new Text("\033cActive+Selected");
        asl->end();
        new Text("\033rSelected:");
        lsel = pattern(saved.list_select);
        lselcur = pattern(saved.list_selcur);
        cu->end();
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
    // -- Navigation (navigationp.c): drag & drop, balancing groups, the keys
    Checkmark *dleft, *dmid, *dauto;
    String *dleft_mod, *dmid_mod;
    Slider *dlen;
    Cycle *dlook, *blook;
    auto keys = std::make_shared<std::vector<std::string>>();
    for (int i = 0; i < MUIKEY_COUNT; i++) keys->push_back(saved.keys[i].empty() ? KEY_DEFAULTS[i] : saved.keys[i]);
    {
        auto *pg = HGroup();
        auto *left = VGroup();
        auto *dd = new Group(Group::Vertical, Frame_Group);
        dd->frame_title("Drag & Drop");
        auto *c3 = new Group(Group::Vertical);
        c3->columns(3);
        new Text("\033rLeft Button:");
        dleft = new Checkmark(saved.drag_left);
        dleft_mod = new String(saved.drag_left_modifier, 64);
        new Text("\033rMiddle Button:");
        dmid = new Checkmark(saved.drag_middle);
        dmid_mod = new String(saved.drag_middle_modifier, 64);
        new Text("\033rAutostart:");
        dauto = new Checkmark(saved.drag_autostart);
        dlen = new Slider(1, 20, saved.drag_autostart_length);
        dlen->format = [](int v) { return trs("%s pixel", std::to_string(v)); };   // MSG_PIXEL
        c3->end();
        auto *c2 = new Group(Group::Vertical);
        c2->columns(2);
        new Text("\033rFrame:");
        frames.push_back({Frame_Drag, frame_cycle(Frame_Drag)});
        new Text("\033rLook:");
        dlook = new Cycle({"Always solid", "Ghosted on box", "Ghosted outside box", "Always ghosted"}, saved.drag_look);
        c2->end();
        dd->end();
        auto *bg = new Group(Group::Vertical, Frame_Group);
        bg->frame_title("Balancing Groups");
        HVSpace();
        auto *bc = new Group(Group::Vertical);
        bc->columns(2);
        new Text("\033rLook:");
        blook = new Cycle({"Show frames", "Show objects"}, saved.balancing_look);
        new Text("\033rExample:");
        auto *ex = HGroup();
        new Text("\033cTry with", Frame_Text);
        new Balance();
        new Text("\033cshift", Frame_Text);
        new Balance();
        new Text("\033ctoo", Frame_Text);
        ex->end();
        bc->end();
        HVSpace();
        bg->end();
        left->end();
        // the keys (MUICFG_Keyboard_*): a list of the functions with their keys; the string
        // below sets the chosen one's (the original's scroll group of strings)
        auto *kc = new Group(Group::Vertical, Frame_Group);
        kc->frame_title("Keyboard Control");
        auto *kl = new List();
        kl->frame(Frame_ReadList);
        kl->columns({"Function", "Key"});
        auto fill_keys = [kl, keys] {
            int a = kl->active();
            kl->clear();
            for (int i = 0; i < MUIKEY_COUNT; i++) {
                List::Line l;
                l.cols = {tr(KEY_LABELS[i]), (*keys)[i]};
                kl->add(l);
            }
            kl->done();
            if (a >= 0) kl->active(a);
        };
        fill_keys();
        auto *ks = new String("", 128);
        ks->disabled(true);
        kc->end();
        kl->on_active = [kl, ks, keys](int i) {
            ks->disabled(false);
            ks->value((*keys)[i]);
        };
        ks->on_acknowledge = [kl, ks, keys, fill_keys] {
            int i = kl->active();
            if (i < 0) return;
            std::string v = ks->value();
            if (!key_spec_valid(v)) { fl_beep(); return; }
            (*keys)[i] = v;
            fill_keys();
        };
        pg->end();
    }
    // -- Special (specialp.c): text fields, the progress indicator, the device and tape images
    {
        auto *pg = VGroup();
        auto *top = new Group(Group::Vertical);
        top->columns(2);
        auto *tf = new Group(Group::Vertical, Frame_Group);
        tf->frame_title("Text Fields");
        auto *tfr = new Group(Group::Vertical);
        tfr->columns(2);
        new Text("\033rFrame:");
        frames.push_back({Frame_Text, frame_cycle(Frame_Text)});
        tfr->end();
        new Text("\033cExample Textfield", Frame_Text);
        tf->end();
        auto *pi = new Group(Group::Vertical, Frame_Group);
        pi->frame_title("Progress Indicator");
        pi->columns(2);
        new Text("\033rFrame:");
        frames.push_back({Frame_Gauge, frame_cycle(Frame_Gauge)});
        new Text("\033rExample:");
        auto *ga = new Gauge();
        ga->set(50, 100);
        ga->info("50 %");
        VSpace();
        new Scale();
        pi->end();
        top->end();
        // the images (MUII_Drawer ... MUII_TapeDown): shown, not chosen here
        auto images = [](const char *title, std::vector<std::pair<const char *, std::function<void(int, int, int, int)>>> v) {
            auto *g = new Group(Group::Horizontal, Frame_Group);
            g->frame_title(title);
            HSpace();
            for (auto &[name, fn] : v) {
                auto *im = new SpecialImage(fn);
                im->copy_tooltip(tr(name).c_str());
            }
            HSpace();
            g->end();
        };
        auto icon = [](IconKind k) {
            return [k](int X, int Y, int W, int H) { draw_small_icon(k, X, Y, W, H); };
        };
        images("Device Images", {{"Drawer", icon(IconKind::Drawer)}, {"Hard disk", icon(IconKind::Harddisk)},
                                 {"Disk", icon(IconKind::Removable)}, {"RAM", icon(IconKind::RamDisk)},
                                 {"Volume", icon(IconKind::Optical)}, {"Network", icon(IconKind::Network)},
                                 {"Assign", icon(IconKind::Link)}});
        auto tape = [](int which) { return [which](int X, int Y, int W, int H) { draw_tape(which, X, Y, W, H); }; };
        images("Tape Images", {{"Play", tape(0)}, {"Play back", tape(1)}, {"Pause", tape(2)}, {"Stop", tape(3)},
                               {"Record", tape(4)}, {"Up", tape(5)}, {"Down", tape(6)}});
        pg->end();
    }
    // -- Frames (framesp.c): the custom frames #1 to #16 - each a frame.config file with the
    // pictures up/default and down/default beside it
    String *cframe[16];
    {
        auto *pg = VGroup();
        VSpace();
        auto *g = new Group(Group::Vertical, Frame_Group);
        g->frame_title("Custom Frames");
        g->columns(4);
        for (int i = 0; i < 16; i++) {
            new Text("\033r#" + std::to_string(i + 1));
            auto *row = HGroup();
            row->spacing(0);
            cframe[i] = new String(saved.custom_frame[i], 1024);
            auto *pop = PopButton(Pop_File);
            row->end();
            String *str = cframe[i];
            pop->on_press = [str] {
                std::string path = str->value();
                if (path.empty()) path = std::string(getenv("HOME") ? getenv("HOME") : "/") + "/";
                if (file_request(tr("Custom Frames"), path)) str->value(path);
            };
        }
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
        p.drag_left = dleft->on();
        p.drag_middle = dmid->on();
        p.drag_left_modifier = dleft_mod->value();
        p.drag_middle_modifier = dmid_mod->value();
        p.drag_autostart = dauto->on();
        p.drag_autostart_length = dlen->value();
        p.drag_look = dlook->active();
        p.balancing_look = blook->active();
        p.bubble_first = bfirst->value();
        p.bubble_next = bnext->value();
        p.scrollbar_arrangement = *sbarr;
        p.list_multi = lmulti->active();
        p.list_refresh = lrefresh->active();
        p.list_smoothed = lsmooth->on();
        p.list_smoothval = lsmoothval->value();
        p.list_font_family = lfam->active() - 1;
        p.list_font_size = lsize->active() ? SIZES[lsize->active() - 1] : 0;
        p.fixed_font_size = fsize->active() ? SIZES[fsize->active() - 1] : 0;
        p.list_leading = llead->value();
        p.list_back = MUII_BACKGROUND + lback->active();
        p.readlist_back = MUII_BACKGROUND + rback->active();
        p.list_cursor = MUII_BACKGROUND + lcur->active();
        p.list_select = MUII_BACKGROUND + lsel->active();
        p.list_selcur = MUII_BACKGROUND + lselcur->active();
        p.scrollbar_type = sbtype->active();
        for (int i = 0; i < 16; i++) p.custom_frame[i] = cframe[i]->value();
        for (int i = 0; i < MUIKEY_COUNT; i++) p.keys[i] = (*keys)[i] == KEY_DEFAULTS[i] ? "" : (*keys)[i];
    };
    // a scheme fills the four colours in
    scheme->on_change = [=](int i) {
        cbg->value(hex(PALETTES[i].bg));
        csh->value(hex(PALETTES[i].shine));
        csd->value(hex(PALETTES[i].shadow));
        cfl->value(hex(PALETTES[i].fill));
    };
    auto *w = new zune::Window("Zune Preferences", root);
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
