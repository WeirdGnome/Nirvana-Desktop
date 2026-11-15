/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Wanderer's preferences and their editor (prefs.h).
*/
#include "prefs.h"
#include "zune.h"
#include "tools.h"
#include "iconlist.h"

#include <FL/Fl.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/fl_draw.H>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

namespace fs = std::filesystem;
using namespace zune;

namespace wanderer {

static std::string prefs_file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/wanderer/wanderer.prefs";
}

WandererPrefs WandererPrefs::load() {
    WandererPrefs p;
    std::ifstream in(prefs_file());
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        if (k == "backdrop_image") { p.backdrop_image = line.substr(eq + 1); continue; }
        if (k == "drawer_image") { p.drawer_image = line.substr(eq + 1); continue; }
        if (k == "screen_title") { p.screen_title = line.substr(eq + 1); continue; }
        int v = atoi(line.c_str() + eq + 1);
        if (k == "text_mode") p.text_mode = std::clamp(v, 0, 2);
        else if (k == "label_lines") p.label_lines = std::clamp(v, 1, 5);
        else if (k == "label_maxlen") p.label_maxlen = std::clamp(v, 4, 64);
        else if (k == "list_mode") p.list_mode = std::clamp(v, 0, 1);
        else if (k == "toolbar") p.toolbar = v;
        else if (k == "statusbar") p.statusbar = v;
        else if (k == "show_hidden") p.show_hidden = v;
        else if (k == "drawers_first") p.drawers_first = v;
        else if (k == "hspacing") p.hspacing = std::clamp(v, 0, 40);
        else if (k == "vspacing") p.vspacing = std::clamp(v, 0, 40);
        else if (k == "backdrop_mode") p.backdrop_mode = std::clamp(v, 0, 3);
        else if (k == "icon_font") p.icon_font = std::clamp(v, -1, 2);
        else if (k == "icon_font_size") p.icon_font_size = std::clamp(v, 0, 48);
        else if (k == "lines_on_focus") p.lines_on_focus = v;
        else if (k == "image_spacing") p.image_spacing = std::clamp(v, 0, 20);
        else if (k == "label_hpad") p.label_hpad = std::clamp(v, 0, 20);
        else if (k == "label_vpad") p.label_vpad = std::clamp(v, 0, 20);
        else if (k == "label_border_w") p.label_border_w = std::clamp(v, 0, 10);
        else if (k == "label_border_h") p.label_border_h = std::clamp(v, 0, 10);
        else if (k == "backdrop_ox") p.backdrop_ox = std::clamp(v, 0, 1024);
        else if (k == "backdrop_oy") p.backdrop_oy = std::clamp(v, 0, 1024);
        else if (k == "drawer_ox") p.drawer_ox = std::clamp(v, 0, 1024);
        else if (k == "drawer_oy") p.drawer_oy = std::clamp(v, 0, 1024);
        else if (k == "auto_sort") p.auto_sort = v;
        else if (k == "default_sort") p.default_sort = std::clamp(v, 0, 2);
        else if (k == "drop_mode") p.drop_mode = std::clamp(v, 0, 1);
        else if (k == "show_network") p.show_network = v;
        else if (k == "navigation") p.navigation = std::clamp(v, 0, 1);
        else if (k == "drawer_mode") p.drawer_mode = std::clamp(v, 0, 3);
    }
    return p;
}

void WandererPrefs::save() const {
    std::error_code ec;
    fs::create_directories(fs::path(prefs_file()).parent_path(), ec);
    std::ofstream out(prefs_file());
    out << "text_mode=" << text_mode << "\nlabel_lines=" << label_lines << "\nlabel_maxlen=" << label_maxlen
        << "\nlist_mode=" << list_mode << "\ntoolbar=" << toolbar << "\nstatusbar=" << statusbar
        << "\nshow_hidden=" << show_hidden << "\ndrawers_first=" << drawers_first << "\nhspacing=" << hspacing
        << "\nvspacing=" << vspacing << "\nbackdrop_image=" << backdrop_image << "\nbackdrop_mode=" << backdrop_mode
        << "\ndrawer_image=" << drawer_image << "\ndrawer_mode=" << drawer_mode << "\nnavigation=" << navigation << "\nshow_network=" << show_network << "\ndrop_mode=" << drop_mode << "\ndefault_sort=" << default_sort << "\nauto_sort=" << auto_sort << "\nbackdrop_ox=" << backdrop_ox << "\nbackdrop_oy=" << backdrop_oy << "\ndrawer_ox=" << drawer_ox << "\ndrawer_oy=" << drawer_oy << "\nimage_spacing=" << image_spacing << "\nlabel_hpad=" << label_hpad << "\nlabel_vpad=" << label_vpad << "\nlabel_border_w=" << label_border_w << "\nlabel_border_h=" << label_border_h << "\nlines_on_focus=" << lines_on_focus << "\nicon_font=" << icon_font << "\nicon_font_size=" << icon_font_size << "\nscreen_title=" << screen_title << "\n";
}

// --------------------------------------------------------------------------------------------
// The editor: one window, groups with titles, Save / Use / Cancel
// --------------------------------------------------------------------------------------------
static Window *g_editor = nullptr, *g_advanced = nullptr;

static Text *label(const char *t) { return new Text(std::string("\033r") + t); }

// MUIC_Popimage of the view settings: the picture, small; a click chooses another
static ImageButton *picture_button(std::shared_ptr<std::string> path) {
    // each button keeps its picture (a shared image, scaled to the button when drawn)
    struct Cache { std::string path; Fl_Shared_Image *img = nullptr; };
    auto cache = std::make_shared<Cache>();
    auto *b = new ImageButton(50, 50, [path, cache](int x, int y, int w, int h, bool) {
        fl_color(pens()[MPEN_HALFSHADOW]);
        fl_rectf(x, y, w, h);
        if (cache->path != *path) {
            if (cache->img) { cache->img->release(); cache->img = nullptr; }
            cache->path = *path;
            if (!path->empty()) cache->img = Fl_Shared_Image::get(path->c_str());
        }
        if (cache->img && cache->img->w() > 0 && cache->img->h() > 0) {
            cache->img->scale(w, h, 0, 1);
            cache->img->draw(x, y);
        }
    });
    b->input_mode(Input_RelVerify);
    return b;
}

// The advanced options of a view (MSG_ADVANCEDOPTIONS): Icons, Labels, Background
static void advanced_options(const std::string &view, std::shared_ptr<WandererPrefs> work, bool backdrop) {
    if (g_advanced) { g_advanced->show(); return; }
    WandererPrefs &c = *work;
    auto *root = VGroup();
    auto *reg = new Register({"Icons", "Labels", "Background"});
    // -- Icons
    auto *ip = VGroup();
    auto *ig = new Group(Group::Vertical, Frame_Group);
    ig->frame_title("Icon Settings");
    ig->columns(2);
    label("Icon listing mode:");
    auto *layout = new Cycle({"Grid", "Plain"}, c.list_mode);
    label("Horizontal spacing:");
    auto *hsp = new Slider(0, 40, c.hspacing);
    label("Vertical spacing:");
    auto *vsp = new Slider(0, 40, c.vspacing);
    label("Image-label spacing:");
    auto *isp = new Slider(0, 20, c.image_spacing);
    label("Drawers first:");
    auto *first = new Checkmark(c.drawers_first);
    label("Icon auto-sort enabled:");
    auto *asort = new Checkmark(c.auto_sort);
    label("Default sort mode:");
    auto *sortm = new Cycle({"By Name", "By Date", "By Size"}, c.default_sort);
    label("Default view mode:");
    auto *viewm = new Cycle({"Only Icons", "All Files"}, c.show_hidden ? 1 : 0);
    ig->end();
    VSpace();
    ip->end();
    // -- Labels
    auto *lp = VGroup();
    auto *lg = new Group(Group::Vertical, Frame_Group);
    lg->frame_title("Label Settings");
    lg->columns(2);
    label("Label text mode:");
    auto *mode = new Cycle({"Outline", "Plain", "Shadow"}, c.text_mode);
    label("Icon label font:");
    auto *ifont = new Cycle({"Screen", "Sans", "Serif", "Fixed"}, c.icon_font + 1);
    static const int ISIZES[] = {0, 9, 10, 11, 12, 13, 14, 16, 18, 20};
    int isi = 0;
    for (int i = 0; i < 10; i++) if (ISIZES[i] == c.icon_font_size) isi = i;
    label("Font size:");
    auto *ifsize = new Cycle({"Screen", "9", "10", "11", "12", "13", "14", "16", "18", "20"}, isi);
    label("Label line length:");
    auto *maxlen = new Slider(4, 64, c.label_maxlen);
    label("Use multi-line labels:");
    auto *multi = new Checkmark(c.label_lines > 1);
    label("Only show focused icon's lines:");
    auto *onfocus = new Checkmark(c.lines_on_focus);
    label("No. of lines to display:");
    auto *lines = new Slider(2, 5, std::max(2, c.label_lines));
    label("Horizontal text padding:");
    auto *lhp = new Slider(0, 20, c.label_hpad);
    label("Vertical text padding:");
    auto *lvp = new Slider(0, 20, c.label_vpad);
    label("Label frame width:");
    auto *lbw = new Slider(0, 10, c.label_border_w);
    label("Label frame height:");
    auto *lbh = new Slider(0, 10, c.label_border_h);
    lg->end();
    VSpace();
    lp->end();
    // -- Background
    auto *bp = VGroup();
    auto *bg = new Group(Group::Vertical, Frame_Group);
    bg->frame_title("Background Options");
    bg->columns(2);
    int &bmode = backdrop ? c.backdrop_mode : c.drawer_mode;
    label("Draw mode:");
    auto *draw = new Cycle({"Tiled", "Scaled", "Centred"}, bmode == 3 ? 0 : bmode);
    label("Tile mode:");
    auto *tile = new Cycle({"Floating", "Fixed"}, bmode == 3 ? 1 : 0);
    int &box = backdrop ? c.backdrop_ox : c.drawer_ox, &boy = backdrop ? c.backdrop_oy : c.drawer_oy;
    label("X offset:");
    auto *tox = new Slider(0, 256, box);
    label("Y offset:");
    auto *toy = new Slider(0, 256, boy);
    bg->end();
    VSpace();
    bp->end();
    reg->end();
    auto *buttons = HGroup();
    buttons->same_width(true);
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    buttons->end();
    root->end();
    auto *w = new Window(tr("Advanced Options") + " - " + view, root);
    g_advanced = w;
    auto close = [w] { w->hide(); Fl::delete_widget(w); g_advanced = nullptr; };
    use->on_press = [=, &bmode, &box, &boy] {
        WandererPrefs &p = *work;
        p.list_mode = layout->active();
        p.hspacing = hsp->value();
        p.vspacing = vsp->value();
        p.drawers_first = first->on();
        p.default_sort = sortm->active();
        p.auto_sort = asort->on();
        p.show_hidden = viewm->active() == 1;
        p.text_mode = mode->active();
        p.icon_font = ifont->active() - 1;
        p.icon_font_size = ISIZES[ifsize->active()];
        p.label_maxlen = maxlen->value();
        p.label_lines = multi->on() ? lines->value() : 1;
        p.lines_on_focus = onfocus->on();
        p.image_spacing = isp->value();
        p.label_hpad = lhp->value();
        p.label_vpad = lvp->value();
        p.label_border_w = lbw->value();
        p.label_border_h = lbh->value();
        bmode = draw->active() == 0 && tile->active() == 1 ? 3 : draw->active();
        box = tox->value();
        boy = toy->value();
        close();
    };
    cancel->on_press = [=] { close(); };
    w->callback([](Fl_Widget *win, void *) { win->hide(); Fl::delete_widget(win); g_advanced = nullptr; });
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    w->position(sx + (sw - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

// The Wanderer preferences program (prefs/wanderer/wpeditor.c): a register with General,
// Appearance and Toolbar; Save / Use / Cancel
void edit_prefs(const WandererPrefs &current, std::function<void(const WandererPrefs &)> apply) {
    if (g_editor) { g_editor->show(); return; }
    auto work = std::make_shared<WandererPrefs>(current);
    auto bpath = std::make_shared<std::string>(current.backdrop_image);
    auto dpath = std::make_shared<std::string>(current.drawer_image);
    auto *root = VGroup();
    auto *reg = new Register({"General", "Appearance", "Toolbar"});
    Cycle *m, *d;
    Checkmark *n, *tb, *sb;
    String *hidden, *title;
    // -- General: navigation and the root window
    auto *gen = VGroup();
    {
        auto *nav = new Group(Group::Vertical, Frame_Group);
        nav->frame_title("Navigation");
        nav->columns(2);
        label("Method:");
        m = new Cycle({"Classic", "Enhanced"}, current.navigation);
        label("Icon drop mode:");
        d = new Cycle({"Move", "Copy"}, current.drop_mode);
        nav->end();
        auto *rootw = new Group(Group::Vertical, Frame_Group);
        rootw->frame_title("Root Window");
        auto *r1 = HGroup();
        label("Network Access:");
        n = new Checkmark(current.show_network);
        HSpace();
        r1->end();
        auto *r2 = HGroup();
        label("Hidden volumes:");
        std::string hv;
        for (auto &v : hidden_volumes()) hv += (hv.empty() ? "" : ", ") + v;
        hidden = new String(hv, 1024);
        hidden->weight(300);
        r2->end();
        rootw->end();
        auto *st = new Group(Group::Vertical, Frame_Group);
        st->frame_title("Wanderer Screen Title");
        st->columns(2);
        label("Title:");
        title = new String(current.screen_title, 256);
        label("");
        new Text("%wv version  %os system  %ov kernel  %pc %pf %pt memory");
        st->end();
        VSpace();
    }
    gen->end();
    // -- Appearance: the view settings - each view's picture and its advanced options
    auto *app = VGroup();
    {
        auto *vs = new Group(Group::Vertical, Frame_Group);
        vs->frame_title("View Settings");
        vs->columns(3);
        struct View { const char *name; std::shared_ptr<std::string> path; bool backdrop; };
        for (View v : {View{"Root window", bpath, true}, View{"Drawers", dpath, false}}) {
            label((std::string(v.name) + ":").c_str());
            auto *pic = picture_button(v.path);
            auto *adv = new Button("Advanced");
            auto path = v.path;
            std::string name = tr(v.name);
            pic->on_press = [path, pic] {
                std::string p = *path;
                if (file_request(tr("Select the background"), p)) { *path = p; pic->redraw(); }
            };
            bool bd = v.backdrop;
            adv->on_press = [work, name, bd] { advanced_options(name, work, bd); };
        }
        vs->end();
        new Text("\033cA click on a picture chooses another; Advanced has the icons, labels and background.");
        VSpace();
    }
    app->end();
    // -- Toolbar: the objects of a drawer window, with a preview
    auto *tbp = VGroup();
    {
        auto *obj = new Group(Group::Vertical, Frame_Group);
        obj->frame_title("Objects");
        obj->columns(2);
        label("Toolbar enabled:");
        tb = new Checkmark(current.toolbar);
        label("Status bar enabled:");
        sb = new Checkmark(current.statusbar);
        obj->end();
        auto *pv = new Group(Group::Horizontal, Frame_Group);
        pv->frame_title("Preview");
        auto *loc = new String("Home:", 64);
        loc->weight(100);
        new ImageButton(20, 18, [](int x, int y, int w, int h, bool) {
            fl_color(pens()[MPEN_TEXT]);
            int cx = x + w / 2;
            fl_polygon(cx, y + 3, cx - 6, y + 9, cx + 6, y + 9);
            fl_rectf(cx - 2, y + 9, 5, h - 12);
        });
        new ImageButton(20, 18, [](int x, int y, int w, int h, bool) {
            fl_color(pens()[MPEN_TEXT]);
            fl_line_style(FL_SOLID, 2);
            fl_arc(x + 4, y + 3, 9, 9, 0, 360);
            fl_line(x + 12, y + 11, x + w - 4, y + h - 3);
            fl_line_style(0);
        });
        pv->end();
        VSpace();
        (void)loc;
    }
    tbp->end();
    reg->end();
    // -- Save / Use / Cancel
    auto *buttons = HGroup();
    buttons->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    buttons->end();
    root->end();

    auto store_hidden = [hidden] {
        std::vector<std::string> v;
        std::string all = std::string(hidden->value()) + ",", cur;
        for (char ch : all) {
            if (ch == ',') {
                size_t b = cur.find_first_not_of(' '), e = cur.find_last_not_of(' ');
                if (b != std::string::npos) v.push_back(cur.substr(b, e - b + 1));
                cur.clear();
            } else cur += ch;
        }
        set_hidden_volumes(v);
    };
    auto collect = [=] {
        WandererPrefs p = *work;
        p.navigation = m->active();
        p.drop_mode = d->active();
        p.show_network = n->on();
        p.screen_title = title->value();
        p.toolbar = tb->on();
        p.statusbar = sb->on();
        p.backdrop_image = *bpath;
        p.drawer_image = *dpath;
        return p;
    };
    auto *w = new Window("Wanderer Preferences", root);
    w->id("WANDERERPREFS");
    g_editor = w;
    auto close = [w] {
        if (g_advanced) { g_advanced->hide(); Fl::delete_widget(g_advanced); g_advanced = nullptr; }
        w->hide(); Fl::delete_widget(w); g_editor = nullptr;
    };
    save->on_press = [=] { auto p = collect(); p.save(); store_hidden(); apply(p); close(); };
    use->on_press = [=] { store_hidden(); apply(collect()); close(); };
    cancel->on_press = [=] { close(); };
    w->callback([](Fl_Widget *win, void *) { win->hide(); Fl::delete_widget(win); g_editor = nullptr; });
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    w->position(sx + (sw - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace wanderer
