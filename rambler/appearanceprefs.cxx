/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Appearance preferences (prefs/appearance/appearanceeditor.c, themepreview.c): see
    appearanceprefs.h.
*/
#include "appearanceprefs.h"
#include "prefs.h"
#include "tools.h"
#include "lune.h"
#include "catalog.h"
#include "../wm/decor.h"

#include <FL/Fl.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <strings.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

std::vector<std::string> theme_dirs() {
    std::vector<std::string> dirs;
    const char *xd = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    dirs.push_back(xd && *xd ? std::string(xd) + "/rambler/themes" : std::string(home ? home : "/tmp") + "/.local/share/rambler/themes");
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        std::string d(exe);
        d = d.substr(0, d.rfind('/'));
        dirs.push_back(d + "/themes");
        dirs.push_back(d + "/../share/rambler/themes");
    }
    dirs.push_back("/usr/share/rambler/themes");
    dirs.push_back("/usr/local/share/rambler/themes");
    return dirs;
}

namespace {
const char *THEMES_DEFAULT = "Ice";

struct ThemeEntry { std::string name, dir; };

// the drawers of THEMES: that hold a theme (by name, the user's before the installation's)
std::vector<ThemeEntry> list_themes() {
    std::vector<ThemeEntry> out;
    for (auto &d : theme_dirs()) {
        DIR *dp = opendir(d.c_str());
        if (!dp) continue;
        while (dirent *e = readdir(dp)) {
            if (e->d_name[0] == '.') continue;
            std::string p = d + "/" + e->d_name;
            if (decor::find_path(p, "System/Config").empty()) continue;
            bool have = false;
            for (auto &t : out) if (!strcasecmp(t.name.c_str(), e->d_name)) have = true;
            if (!have) out.push_back({e->d_name, p});
        }
        closedir(dp);
    }
    std::sort(out.begin(), out.end(), [](const ThemeEntry &a, const ThemeEntry &b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
    return out;
}

std::string config_base() {
    const char *x = getenv("XDG_CONFIG_HOME");
    return x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
}

std::string read_line(const std::string &f) {
    std::ifstream in(f);
    std::string l;
    std::getline(in, l);
    while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
    return l;
}

// ---- the theme's settings for the other preferences (Env-Archive) ---------------------------
uint16_t be16(const std::string &d, size_t i) { return i + 1 < d.size() ? (uint16_t)(((uint8_t)d[i] << 8) | (uint8_t)d[i + 1]) : 0; }
uint32_t be32(const std::string &d, size_t i) { return ((uint32_t)be16(d, i) << 16) | be16(d, i + 2); }

std::string read_file(const std::string &f) {
    std::ifstream in(f, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// the chunks of an IFF PREF file of this type
std::vector<std::string> iff_chunks(const std::string &d, const char *id) {
    std::vector<std::string> out;
    if (d.size() < 12 || d.compare(0, 4, "FORM") || d.compare(8, 4, "PREF")) return out;
    size_t i = 12;
    while (i + 8 <= d.size()) {
        uint32_t n = be32(d, i + 4);
        if (!d.compare(i, 4, id)) out.push_back(d.substr(i + 8, n));
        i += 8 + n + (n & 1);
    }
    return out;
}

// palette.prefs (prefs/palette.h): the colours and the pens of screens with more than four
// colours; Lune's palette is the background, shadow, shine and fill pens' colours
bool import_palette(const std::string &file, Prefs &p) {
    auto ch = iff_chunks(read_file(file), "PALT");
    if (ch.empty() || ch[0].size() < 144 + 8) return false;
    const std::string &c = ch[0];
    uint32_t colors[32];
    for (auto &x : colors) x = 0;
    for (int k = 0; k < 32 && 144 + k * 8 + 8 <= (int)c.size(); k++) {
        int16_t idx = (int16_t)be16(c, 144 + k * 8);
        if (idx < 0) break;
        if (idx < 32) colors[idx] = ((be16(c, 146 + k * 8) >> 8) << 16) | ((be16(c, 148 + k * 8) >> 8) << 8) | (be16(c, 150 + k * 8) >> 8);
    }
    auto pen = [&](int i) { int16_t v = (int16_t)be16(c, 80 + i * 2); return colors[std::clamp((int)v, 0, 31)]; };
    // (DrawInfo: 2 TEXTPEN, 3 SHINEPEN, 4 SHADOWPEN, 5 FILLPEN, 6 FILLTEXTPEN, 7 BACKGROUNDPEN,
    // 9 BARDETAILPEN, 10 BARBLOCKPEN)
    p.background = pen(7);
    p.shadow = pen(4);
    p.shine = pen(3);
    p.fill = pen(5);
    p.text_pen = pen(2);
    p.filltext_pen = pen(6);
    p.bardetail_pen = pen(9);
    p.barblock_pen = pen(10);
    return true;
}

// font.prefs (prefs/font.h): the icons' (FP_WBFONT), the system's (FP_SYSFONT, the fixed
// one) and the screens' font (FP_SCREENFONT); "Name Style.font" is the family "Name"
bool import_fonts(const std::string &file, Prefs &p, RamblerPrefs &rp) {
    auto ch = iff_chunks(read_file(file), "FONT");
    if (ch.empty()) return false;
    for (auto &c : ch) {
        if (c.size() < 28 + 2) continue;
        int type = be16(c, 14), size = be16(c, 24);
        std::string name(c.c_str() + 28, strnlen(c.c_str() + 28, std::min<size_t>(128, c.size() - 28)));
        if (name.size() > 5 && !strcasecmp(name.c_str() + name.size() - 5, ".font")) name.resize(name.size() - 5);
        for (const char *style : {" Regular", " Normal", " Book", " Roman"})
            if (name.size() > strlen(style) && !strcasecmp(name.c_str() + name.size() - strlen(style), style)) name.resize(name.size() - strlen(style));
        if (name.empty() || size <= 0) continue;
        if (type == 0) { rp.icon_font_name = name; rp.icon_font_size = size; }
        else if (type == 1) { p.fixed_font_name = name; p.fixed_font_size = size; }
        else if (type == 2) { p.font_name = name; p.font_size = size; }
    }
    return true;
}

// ---- ThemePreview: a mock scene drawn with the theme - a screen bar, a window with its title
// bar and gadgets and a scroller --------------------------------------------------------------
const int PREVIEW_WIDTH = 256, PREVIEW_HEIGHT = 150;

class ThemePreview : public Area {
public:
    ThemePreview() : Area(Frame_None) {}
    ~ThemePreview() override { delete img_; }
    void theme(const std::string &dir) {
        if (dir == dir_) return;
        dir_ = dir;
        render();
        redraw();
    }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override {
        minw = defw = PREVIEW_WIDTH;
        minh = defh = PREVIEW_HEIGHT;
        maxw = 10000;
        maxh = PREVIEW_HEIGHT;
    }

protected:
    void render() {
        delete img_;
        img_ = nullptr;
        theme_.reset();
        if (dir_.empty()) return;
        theme_ = decor::load_theme(dir_);
        if (!theme_) return;
        decor::Theme &t = *theme_;
        const decor::Config &dc = t.dc;
        const decor::Images &di = t.di;
        decor::RastPort rp;
        rp.size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        int gcols = dc.GadgetsThreeState ? 3 : 4;
        sbarh_ = std::clamp(dc.SBarHeight, 10, 28);
        barh_ = std::clamp(dc.BarHeight, 10, 30);
        auto ok = [](const decor::ImagePtr &i) { return i && i->ok; };
        // the desktop
        rp.fill(0, 0, PREVIEW_WIDTH, PREVIEW_HEIGHT, 0xAAAAAA);
        // the screen bar
        decor::render_element(t.el[decor::ScrBarFill], rp, 0, 0, 0, PREVIEW_WIDTH, sbarh_, 0);
        if (ok(di.sbarlogo))
            decor::render_element(t.el[decor::ScrBarLogo], rp, 0, dc.SLogoOffset, (sbarh_ - di.sbarlogo->h) / 2, di.sbarlogo->w, di.sbarlogo->h, 0);
        if (ok(di.sdepth)) {
            int dw = di.sdepth->w >> 1;
            decor::render_element(t.el[decor::ScrDepth], rp, decor::IDS_NORMAL, PREVIEW_WIDTH - dw - 2, (sbarh_ - di.sdepth->h) / 2, -1, -1, 0);
        }
        scrtitle_rgb_ = dc.STitleColorText != 0xFFFFFFFF ? dc.STitleColorText & 0xFFFFFF : 0xCCCCCC;
        stitle_x_ = dc.STitleOffset + 2;
        // the window
        int wx0 = 14, wy0 = sbarh_ + 10, ww = PREVIEW_WIDTH - 28, wh = PREVIEW_HEIGHT - wy0 - 10, wx1 = wx0 + ww;
        if (ok(di.border_normal)) decor::render_element(t.el[decor::WinBorderNormal], rp, 0, wx0, wy0, ww, wh, 0);
        else decor::fill_gradient(rp, 0, 0, ww, wh, wx0, wy0, ww, wh, dc.ActivatedGradientColor_s, dc.ActivatedGradientColor_e,
                                  dc.ActivatedGradientColor_a, 0);
        rp.fill(wx0 + dc.LeftBorder, wy0 + barh_, wx1 - dc.RightBorder - (wx0 + dc.LeftBorder), wy0 + wh - dc.BottomBorder - (wy0 + barh_), 0xF4F4F4);
        title_x_ = -1;
        if (ok(di.winbar_normal)) {
            int closew = ok(di.close) ? di.close->w / gcols : 0, zoomw = ok(di.zoom) ? di.zoom->w / gcols : 0,
                depthw = ok(di.depth) ? di.depth->w / gcols : 0;
            int x = wx0;
            auto el = [&](int id, int dw) { return decor::render_element(t.el[id], rp, 0, x, wy0, dw, barh_, wx1); };
            x = el(decor::WinBarPreGadget, dc.BarPreGadget_s);
            if (closew) {
                decor::render_element(t.el[decor::WinClose], rp, decor::IDS_NORMAL, x, wy0 + (barh_ - di.close->h) / 2, -1, -1, 0);
                x += closew;
            }
            x = el(decor::WinBarJoinGB, dc.BarJoinGB_s);
            x = el(decor::WinBarJoinBT, dc.BarJoinBT_s);
            title_x_ = x + 3;
            int rgadx = wx1 - dc.BarPostGadget_s - zoomw - depthw;
            int titlew = rgadx - x - dc.BarJoinTB_s - dc.BarJoinBG_s;
            if (titlew > 0) x = el(decor::WinBarTitleFill, titlew);
            x = el(decor::WinBarJoinTB, dc.BarJoinTB_s);
            x = el(decor::WinBarJoinBG, dc.BarJoinBG_s);
            if (zoomw) {
                decor::render_element(t.el[decor::WinZoom], rp, decor::IDS_NORMAL, x, wy0 + (barh_ - di.zoom->h) / 2, -1, -1, 0);
                x += zoomw;
            }
            if (depthw) {
                decor::render_element(t.el[decor::WinDepth], rp, decor::IDS_NORMAL, x, wy0 + (barh_ - di.depth->h) / 2, -1, -1, 0);
                x += depthw;
            }
            decor::render_element(t.el[decor::WinBarPostGadget], rp, 0, x, wy0, dc.BarPostGadget_s, barh_, wx1);
            wintitle_rgb_ = dc.TitleColorText != 0xFFFFFFFF ? dc.TitleColorText & 0xFFFFFF : 0xFFFFFF;
            wtitle_y_ = wy0;
        }
        // a scroller: its container and a knob
        if (ok(di.verticalcontainer)) {
            int contw = di.verticalcontainer->w >> 1, vx = wx1 - contw - 1, vy0 = wy0 + barh_ + 1, vy1 = wy0 + wh - 1 - dc.BottomBorder;
            int mid = (vy1 - vy0) - dc.ContainerTop_s - dc.ContainerBottom_s;
            int y = vy0;
            y = decor::render_element(t.el[decor::VContainerTop], rp, 0, vx, y, 0, dc.ContainerTop_s, 0);
            if (mid > 0) y = decor::render_element(t.el[decor::VContainerTile], rp, 0, vx, y, 0, mid, 0);
            decor::render_element(t.el[decor::VContainerBottom], rp, 0, vx, y, 0, dc.ContainerBottom_s, 0);
            if (ok(di.verticalknob)) {
                int knobw = di.verticalknob->w / 3, kx = vx + (contw - knobw) / 2, kh = ((vy1 - vy0) * 2) / 5;
                int ktile = kh - dc.KnobTop_s - dc.KnobBottom_s;
                y = vy0 + 2;
                y = decor::render_element(t.el[decor::VKnobTop], rp, 0, kx, y, 0, dc.KnobTop_s, 0);
                if (ktile > 0) y = decor::render_element(t.el[decor::VKnobTileTop], rp, 0, kx, y, 0, ktile, 0);
                decor::render_element(t.el[decor::VKnobBottom], rp, 0, kx, y, 0, dc.KnobBottom_s, 0);
            }
        }
        rgb_.resize((size_t)PREVIEW_WIDTH * PREVIEW_HEIGHT * 3);
        for (size_t i = 0; i < rp.px.size(); i++) {
            rgb_[i * 3] = (rp.px[i] >> 16) & 0xff;
            rgb_[i * 3 + 1] = (rp.px[i] >> 8) & 0xff;
            rgb_[i * 3 + 2] = rp.px[i] & 0xff;
        }
        img_ = new Fl_RGB_Image(rgb_.data(), PREVIEW_WIDTH, PREVIEW_HEIGHT, 3);
    }
    void draw_contents() override {
        int px = mleft() + (mwidth() - PREVIEW_WIDTH) / 2, py = mtop() + (mheight() - PREVIEW_HEIGHT) / 2;
        if (!img_) {
            fl_color(pens()[MPEN_TEXT]);
            fl_font(prefs().font, prefs().font_size);
            fl_draw(tr("-Preview Unavailable-").c_str(), mleft(), mtop(), mwidth(), mheight(), FL_ALIGN_CENTER);
            return;
        }
        img_->draw(px, py);
        fl_font(FL_HELVETICA, 11);
        auto rgb = [](uint32_t c) { return fl_rgb_color((uchar)(c >> 16), (uchar)(c >> 8), (uchar)c); };
        fl_color(rgb(scrtitle_rgb_));
        fl_draw("Rambler", px + stitle_x_, py + (sbarh_ >> 1) + 3 + 1);
        if (title_x_ >= 0) {
            fl_color(rgb(wintitle_rgb_));
            fl_draw(tr("Preview").c_str(), px + title_x_, py + wtitle_y_ + (barh_ >> 1) + 3 + 1);
        }
    }

private:
    std::string dir_;
    std::unique_ptr<decor::Theme> theme_;
    std::vector<uchar> rgb_;
    Fl_RGB_Image *img_ = nullptr;
    int sbarh_ = 0, barh_ = 0, stitle_x_ = 0, title_x_ = -1, wtitle_y_ = 0;
    uint32_t scrtitle_rgb_ = 0xCCCCCC, wintitle_rgb_ = 0xFFFFFF;
};

Checkmark *check_row(const char *label, bool on) {
    auto *row = HGroup();
    auto *c = new Checkmark(on);
    new lune::Text(label);
    HVSpace();
    row->end();
    return c;
}
}  // namespace

static lune::Window *g_appearance = nullptr;

void edit_appearance_prefs(bool standalone) {
    if (g_appearance) { g_appearance->show(); return; }
    auto themes = std::make_shared<std::vector<ThemeEntry>>(list_themes());
    // the theme in use: theme.var in ENV:, else in ENVARC:
    std::string cur = read_line(env_dir() + "/theme.var");
    if (cur.empty()) cur = read_line(config_base() + "/rambler/theme.var");
    bool enabled = strcasecmp(cur.c_str(), "none") != 0;
    std::string curname = cur.rfind("THEMES:", 0) == 0 || !strncasecmp(cur.c_str(), "THEMES:", 7) ? cur.substr(7) : fs::path(cur).filename().string();
    if (cur.empty() || !enabled) curname = THEMES_DEFAULT;
    int active = 0;
    std::vector<std::string> names;
    for (size_t i = 0; i < themes->size(); i++) {
        names.push_back((*themes)[i].name);
        if (!strcasecmp((*themes)[i].name.c_str(), curname.c_str())) active = (int)i;
    }
    if (names.empty()) names.push_back("-");

    auto *root = VGroup();
    auto *preview = new ThemePreview();
    auto *cols = HGroup();
    // -- Theming
    auto *tg = new Group(Group::Vertical, Frame_Group);
    tg->frame_title("Theming");
    auto *enable = check_row("Enable theming", enabled);
    auto *srow = HGroup();
    new lune::Text("Theme:");
    auto *choice = new Cycle(names, active);
    srow->end();
    auto *descgrp = new Group(Group::Vertical, Frame_Text);
    auto *desca = new lune::Text("\033c");
    auto *descb = new lune::Text("\033c");
    descgrp->end();
    HBar();
    new lune::Text("Use theme configuration for..");
    auto *zune = check_row("Lune", true);
    auto *wand = check_row("Rambler", true);
    auto *pal = check_row("Palette", true);
    auto *font = check_row("Fonts", true);
    auto *ptr = check_row("Pointers", true);
    HVSpace();
    tg->end();
    // -- Compositing (the window manager composes the screens: shown, not to be changed)
    auto *cg = new Group(Group::Vertical, Frame_Group);
    cg->frame_title("Compositing");
    std::vector<Checkmark *> comp = {check_row("Enable screen composition", true), check_row("Composite below", true),
                                     check_row("Composite left", false), check_row("Composite right", false),
                                     check_row("Enable compositing with alpha", false),
                                     check_row("Scale smaller screens up to the largest screen's mode", false),
                                     check_row("Use the display's highest mode instead", false)};
    for (auto *c : comp) c->disabled(true);
    HVSpace();
    cg->end();
    cols->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();
    use->disabled(true);   // (MUIA_PrefsEditor_CanUse FALSE: the theme is saved, as on the original)

    struct Option { Checkmark *box; std::string file; };
    auto options = std::make_shared<std::vector<Option>>(std::vector<Option>{
        {zune, "Env-Archive/Zune/global.prefs"}, {wand, "Env-Archive/SYS/Wanderer/global.prefs"},
        {pal, "Env-Archive/SYS/palette.prefs"}, {font, "Env-Archive/SYS/font.prefs"}, {ptr, "Env-Archive/SYS/pointer.prefs"}});
    // AppearanceEditor__PreviewHookFunc: the gadgets following the choice, the preview
    auto update = [=](bool reset_options) {
        bool on = enable->on();
        choice->disabled(!on);
        int a = choice->active();
        bool any = a >= 0 && a < (int)themes->size();
        for (auto &o : *options) {
            bool have = any && !decor::find_path((*themes)[a].dir, o.file).empty();
            o.box->disabled(!on || !have);
            if (reset_options) o.box->on(have);
        }
        // the description: its first line, and its second in italics
        bool shown = false;
        if (any) {
            std::string d = decor::find_path((*themes)[a].dir, "Description");
            if (!d.empty()) {
                std::string t = read_file(d);
                auto nl = t.find('\n');
                std::string l1 = t.substr(0, nl), l2 = nl == std::string::npos ? "" : t.substr(nl + 1);
                while (!l2.empty() && (l2.back() == '\n' || l2.back() == '\r' || l2.back() == ' ')) l2.pop_back();
                l2 = l2.substr(0, l2.find('\n'));
                desca->contents("\033c" + l1);
                descb->contents("\033c" + l2);
                shown = true;
            }
        }
        if (shown) descgrp->show(); else descgrp->hide();
        preview->theme(on && any ? (*themes)[a].dir : "");
        if (auto *win = dynamic_cast<lune::Window *>(root->window())) win->recalc();
        else root->relayout();
    };
    update(true);
    enable->on_press = [=] { update(false); };
    choice->on_change = [=](int) { update(true); };

    // Save: theme.var (ENV: and ENVARC:) and the settings the theme brings
    auto put = [=] {
        int a = choice->active();
        bool any = a >= 0 && a < (int)themes->size();
        std::string v = enable->on() && any ? "THEMES:" + (*themes)[a].name : "none";
        for (const std::string &f : {env_dir() + "/theme.var", config_base() + "/rambler/theme.var"}) {
            std::error_code ec;
            fs::create_directories(fs::path(f).parent_path(), ec);
            std::ofstream(f) << v << "\n";
        }
        if (enable->on() && any) {
            const std::string &dir = (*themes)[a].dir;
            Prefs &p = prefs();
            RamblerPrefs rp = RamblerPrefs::load();
            bool lune_changed = false, rambler_changed = false;
            if (!zune->disabled() && zune->on()) lune_changed |= import_zune_prefs(decor::find_path(dir, (*options)[0].file), p);
            if (!pal->disabled() && pal->on()) lune_changed |= import_palette(decor::find_path(dir, (*options)[2].file), p);
            if (!font->disabled() && font->on()) {
                bool f = import_fonts(decor::find_path(dir, (*options)[3].file), p, rp);
                lune_changed |= f;
                rambler_changed |= f;
            }
            if (lune_changed) { save_prefs(true); apply_prefs(); }
            if (rambler_changed) rp.save(true);
        }
        // the window manager: the theme at once
        if (fl_display) {
            Atom prop = XInternAtom(fl_display, "_RAMBLER_THEME", False), utf8 = XInternAtom(fl_display, "UTF8_STRING", False);
            XChangeProperty(fl_display, RootWindow(fl_display, fl_screen), prop, utf8, 8, PropModeReplace,
                            (const unsigned char *)v.c_str(), (int)v.size());
            XFlush(fl_display);
        }
    };

    auto *w = new lune::Window("Appearance Preferences", root);
    w->id("APPEARANCEPREFS");
    g_appearance = w;
    auto close = [w, standalone] {
        w->hide();
        Fl::delete_widget(w);
        g_appearance = nullptr;
        if (standalone) exit(0);
    };
    save->on_press = [=] { put(); close(); };
    cancel->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 4);
    w->show();
}

}  // namespace rambler
