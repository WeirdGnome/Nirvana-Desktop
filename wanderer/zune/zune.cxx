/*
    Copyright (C) 2002-2026, The AROS Development Team.
    Copyright (C) 1999, David Le Corfec.
    All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Zune classes on FLTK (zune.h). The layout follows the rules of
    muimaster/classes/group.c: every object says its minimum, default and maximum size;
    a group sums them along its direction (with spacing) and takes the largest minimum and
    the smallest maximum across; the room above the minimum is shared by weight, an object
    never growing above its maximum.
*/
#include "zune.h"
#include <FL/Fl_Menu_Window.H>
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <cstring>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

namespace zune {

Prefs &prefs() {
    static Prefs p;
    return p;
}

static std::string prefs_path() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/wanderer/zune.prefs";
}

void load_prefs() {
    Prefs &p = prefs();
    FILE *f = fopen(prefs_path().c_str(), "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            std::string l(line);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            auto eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string k = l.substr(0, eq), v = l.substr(eq + 1);
            if (k == "background") p.background = (unsigned)strtoul(v.c_str(), nullptr, 16);
            else if (k == "shadow") p.shadow = (unsigned)strtoul(v.c_str(), nullptr, 16);
            else if (k == "shine") p.shine = (unsigned)strtoul(v.c_str(), nullptr, 16);
            else if (k == "fill") p.fill = (unsigned)strtoul(v.c_str(), nullptr, 16);
            else if (k == "button_frame" && v.size() == 6) p.frame[Frame_Button] = p.frame[Frame_ImageButton] = v;
            else if (k == "group_frame" && v.size() == 6) p.frame[Frame_Group] = v;
            else if (k.rfind("frame_", 0) == 0 && v.size() == 6) {
                int i = atoi(k.c_str() + 6);
                if (i > 0 && i < Frame_Count) p.frame[i] = v;
            }
            else if (k == "cycle_menu_position") p.cycle_menu_position = std::clamp(atoi(v.c_str()), 0, 1);
            else if (k == "cycle_menu_min") p.cycle_menu_min = std::clamp(atoi(v.c_str()), 2, 100);
            else if (k == "cycle_menu_recessed") p.cycle_menu_recessed = atoi(v.c_str());
            else if (k == "group_hspacing") p.group_hspacing = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "group_vspacing") p.group_vspacing = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "window_left") p.window_left = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "window_right") p.window_right = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "window_top") p.window_top = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "window_bottom") p.window_bottom = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "font_size") p.font_size = std::clamp(atoi(v.c_str()), 9, 24);
            else if (k == "font_family") p.font_family = std::clamp(atoi(v.c_str()), 0, 2);
        }
        fclose(f);
    }
    apply_prefs();
}

void save_prefs() {
    Prefs &p = prefs();
    std::string path = prefs_path();
    std::string dir = path.substr(0, path.rfind('/'));
    std::string cmd;
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
    FILE *f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "background=%06X\nshadow=%06X\nshine=%06X\nfill=%06X\nfont_size=%d\nfont_family=%d\n"
               "group_hspacing=%d\ngroup_vspacing=%d\nwindow_left=%d\nwindow_right=%d\nwindow_top=%d\nwindow_bottom=%d\n",
            p.background, p.shadow, p.shine, p.fill, p.font_size, p.font_family, p.group_hspacing, p.group_vspacing,
            p.window_left, p.window_right, p.window_top, p.window_bottom);
    fprintf(f, "cycle_menu_position=%d\ncycle_menu_min=%d\ncycle_menu_recessed=%d\n", p.cycle_menu_position,
            p.cycle_menu_min, (int)p.cycle_menu_recessed);
    for (int i = 1; i < Frame_Count; i++)
        if (!p.frame[i].empty()) fprintf(f, "frame_%d=%s\n", i, p.frame[i].c_str());
    fclose(f);
}

void apply_prefs() {
    Prefs &p = prefs();
    auto col = [](unsigned rgb) { return fl_rgb_color((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); };
    set_palette(col(p.background), col(p.shadow), col(p.shine), col(p.fill));
    static const Fl_Font fams[3][2] = {{FL_HELVETICA, FL_HELVETICA_BOLD}, {FL_TIMES, FL_TIMES_BOLD}, {FL_COURIER, FL_COURIER_BOLD}};
    p.font = fams[p.font_family][0];
    p.font_bold = fams[p.font_family][1];
    for (int i = 1; i < Frame_Count; i++) set_std_frame((StdFrame)i, p.frame[i].empty() ? default_frame((StdFrame)i) : p.frame[i]);
    // the window manager takes the colours too (its frames, the screens' title bars)
    if (fl_display) {
        char v[64];
        snprintf(v, sizeof v, "%06X %06X %06X %06X", p.background & 0xFFFFFF, p.shadow & 0xFFFFFF, p.shine & 0xFFFFFF,
                 p.fill & 0xFFFFFF);
        Atom prop = XInternAtom(fl_display, "_WANDERER_PALETTE", False);
        Atom utf8 = XInternAtom(fl_display, "UTF8_STRING", False);
        XChangeProperty(fl_display, RootWindow(fl_display, fl_screen), prop, utf8, 8, PropModeReplace, (unsigned char *)v,
                        (int)strlen(v));
        XFlush(fl_display);
    }
    // every window: laid out again (sizes depend on the font and frames) and drawn
    for (Fl_Window *w = Fl::first_window(); w; w = Fl::next_window(w)) {
        if (auto *zw = dynamic_cast<Window *>(w)) {
            zw->color(pens()[MPEN_BACKGROUND]);
            zw->resize(zw->x(), zw->y(), zw->w(), zw->h());
        }
        w->redraw();
    }
}

// ============================================================================================
// Area
// ============================================================================================
Area::Area(StdFrame f) : Fl_Group(0, 0, 10, 10) {
    end();
    box(FL_NO_BOX);
    frame(f);
}

void Area::selected(bool s) {
    if (selected_ == s) return;
    selected_ = s;
    redraw();
}

// the inner spacing: set by the object, or the frame spec's
int Area::add_left() const { return frame_borders(frame_spec().type, frame_spec().down).left + (inner_set_ ? il_ : frame_spec().inner_left); }
int Area::add_right() const { return frame_borders(frame_spec().type, frame_spec().down).right + (inner_set_ ? ir_ : frame_spec().inner_right); }
int Area::add_top() const { return frame_borders(frame_spec().type, frame_spec().down).top + (inner_set_ ? it_ : frame_spec().inner_top); }
int Area::add_bottom() const { return frame_borders(frame_spec().type, frame_spec().down).bottom + (inner_set_ ? ib_ : frame_spec().inner_bottom); }

void Area::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = minh = defw = defh = 0;
    maxw = maxh = MAXMAX;
}

void Area::min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    ask_min_max(minw, minh, defw, defh, maxw, maxh);
    if (fixw_) minw = defw = maxw = fixw_;
    if (fixh_) minh = defh = maxh = fixh_;
    int aw = add_left() + add_right(), ah = add_top() + add_bottom();
    minw += aw; defw += aw; maxw = std::min(MAXMAX, maxw + aw);
    minh += ah; defh += ah; maxh = std::min(MAXMAX, maxh + ah);
    defw = std::clamp(defw, minw, std::max(minw, maxw));
    defh = std::clamp(defh, minh, std::max(minh, maxh));
}

void Area::draw_background(int X, int Y, int W, int H) {
    if (bg_pen_ < 0) return;
    int pen = bg_pen_;
    // a selected button: the "selected" background (configdata: 0:131 - the fill pen)
    if (selected_ && (input_ == Input_RelVerify || input_ == Input_Immediate)) pen = MPEN_FILL;
    fl_color(pens()[pen]);
    fl_rectf(X, Y, W, H);
}

void Area::draw() {
    if (damage() & ~FL_DAMAGE_CHILD) {
        draw_background(x(), y(), w(), h());
        draw_frame(frame_spec(), x(), y(), w(), h(), selected_ && (input_ == Input_RelVerify || input_ == Input_Immediate));
        fl_push_clip(mleft(), mtop(), mwidth(), mheight());
        draw_contents();
        fl_pop_clip();
    }
    draw_children();
    if (Fl::focus() == this && accepts_focus()) draw_active_outline();
}

// _zune_focus_new: a line pattern 0xCCCC in the shine and shadow pens, one pixel outside
void Area::draw_active_outline() {
    int x1 = x() - 1, y1 = y() - 1, x2 = x() + w(), y2 = y() + h();
    fl_push_no_clip();
    int n = 0;
    auto dot = [&](int X, int Y) {
        fl_color((0xCCCC >> (15 - (n & 15))) & 1 ? pens()[MPEN_SHINE] : pens()[MPEN_SHADOW]);
        fl_point(X, Y);
        n++;
    };
    for (int X = x1; X <= x2; X++) dot(X, y1);
    for (int Y = y1 + 1; Y <= y2; Y++) dot(x2, Y);
    for (int X = x2 - 1; X >= x1; X--) dot(X, y2);
    for (int Y = y2 - 1; Y > y1; Y--) dot(x1, Y);
    fl_pop_clip();
}

// the outline lies outside the object: its parent draws that strip again
void Area::focus_changed() {
    redraw();
    if (parent()) parent()->damage(FL_DAMAGE_ALL, x() - 1, y() - 1, w() + 2, h() + 2);
    if (window()) window()->damage(FL_DAMAGE_ALL, x() - 1, y() - 1, w() + 2, h() + 2);
}

int Area::handle(int e) {
    if ((e == FL_FOCUS || e == FL_UNFOCUS) && accepts_focus() && children() == 0) { focus_changed(); return 1; }
    if (input_ == Input_None || disabled_) return Fl_Group::handle(e);
    switch (e) {
    case FL_KEYBOARD:
        // the active object: Space (and Return) press it
        if (Fl::focus() == this && (Fl::event_key() == ' ' || Fl::event_key() == FL_Enter)) {
            if (input_ == Input_Toggle) { selected(!selected_); if (on_press) on_press(); }
            else if (on_press) on_press();
            return 1;
        }
        return 0;
    case FL_PUSH:
        if (Fl::event_button() != FL_LEFT_MOUSE) return 0;
        pressed_ = true;
        if (input_ == Input_Toggle) {
            selected(!selected_);
            if (on_press) on_press();
        } else selected(true);
        if (input_ == Input_Immediate && on_press) on_press();
        return 1;
    case FL_DRAG:
        if (pressed_ && input_ == Input_RelVerify) selected(Fl::event_inside(this));
        return 1;
    case FL_RELEASE:
        if (!pressed_) return 0;
        pressed_ = false;
        if (input_ == Input_RelVerify) {
            bool over = Fl::event_inside(this);
            selected(false);
            if (over && on_press) on_press();
        } else if (input_ == Input_Immediate) selected(false);
        return 1;
    }
    return Fl_Group::handle(e);
}

// ============================================================================================
// Group
// ============================================================================================
Group::Group(Dir d, StdFrame f) : Area(f), dir_(d), hs_(-1), vs_(-1) {
    background(-1);   // a group shows its parent's background unless framed
    begin();          // the children follow (end() closes it)
}

int Group::title_h() const {
    if (title_.empty()) return 0;
    return prefs().font_size + 2;
}

std::vector<Area *> Group::areas() const {
    std::vector<Area *> v;
    for (int i = 0; i < children(); i++)
        if (auto *a = dynamic_cast<Area *>(child(i)); a && a->visible()) v.push_back(a);
    return v;
}

void Group::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    auto kids = areas();
    minw = minh = defw = defh = 0;
    maxw = maxh = MAXMAX;
    if (kids.empty()) return;
    if (columns_ > 1) {
        // a grid: columns as wide as their widest object, rows as high as their highest
        int cols = columns_, rows = ((int)kids.size() + cols - 1) / cols;
        std::vector<int> cminw(cols), cdefw(cols), cmaxw(cols, MAXMAX), rminh(rows), rdefh(rows), rmaxh(rows, MAXMAX);
        for (size_t i = 0; i < kids.size(); i++) {
            int c = i % cols, r = i / cols, a, b, cc, d, e, f;
            kids[i]->min_max(a, b, cc, d, e, f);
            cminw[c] = std::max(cminw[c], a); cdefw[c] = std::max(cdefw[c], cc); cmaxw[c] = std::min(cmaxw[c], e);
            rminh[r] = std::max(rminh[r], b); rdefh[r] = std::max(rdefh[r], d); rmaxh[r] = std::min(rmaxh[r], f);
        }
        maxw = maxh = 0;
        for (int c = 0; c < cols; c++) { minw += cminw[c]; defw += cdefw[c]; maxw += std::max(cmaxw[c], cminw[c]); }
        for (int r = 0; r < rows; r++) { minh += rminh[r]; defh += rdefh[r]; maxh += std::max(rmaxh[r], rminh[r]); }
        minw += hsp() * (cols - 1); defw += hsp() * (cols - 1); maxw += hsp() * (cols - 1);
        minh += vsp() * (rows - 1); defh += vsp() * (rows - 1); maxh += vsp() * (rows - 1);
    } else {
        bool horiz = dir_ == Horizontal;
        int along_min = 0, along_def = 0, along_max = 0, across_min = 0, across_def = 0, across_max = MAXMAX;
        int same_min = 0, same_def = 0;
        for (auto *k : kids) {
            int a, b, c, d, e, f;
            k->min_max(a, b, c, d, e, f);
            int amin = horiz ? a : b, adef = horiz ? c : d, amax = horiz ? e : f;
            int xmin = horiz ? b : a, xdef = horiz ? d : c, xmax = horiz ? f : e;
            along_min += amin; along_def += adef; along_max += amax;
            same_min = std::max(same_min, amin); same_def = std::max(same_def, adef);
            across_min = std::max(across_min, xmin);
            across_def = std::max(across_def, xdef);
            across_max = std::min(across_max, xmax);
        }
        if ((horiz && same_w_) || (!horiz && same_h_)) {
            along_min = same_min * (int)kids.size();
            along_def = same_def * (int)kids.size();
        }
        int sp = (horiz ? hsp() : vsp()) * ((int)kids.size() - 1);
        along_min += sp; along_def += sp; along_max = std::min(MAXMAX, along_max + sp);
        across_max = std::max(across_max, across_min);
        across_def = std::clamp(across_def, across_min, across_max);
        if (horiz) { minw = along_min; defw = along_def; maxw = along_max; minh = across_min; defh = across_def; maxh = across_max; }
        else { minh = along_min; defh = along_def; maxh = along_max; minw = across_min; defw = across_def; maxw = across_max; }
    }
    minh += title_h(); defh += title_h(); maxh = std::min(MAXMAX, maxh + title_h());
}

void Group::resize(int X, int Y, int W, int H) {
    Fl_Widget::resize(X, Y, W, H);   // the children are placed by layout(), not scaled
    layout();
}

// Shares `room` among the objects by weight, never giving one more than its max - min.
static std::vector<int> share(int room, const std::vector<int> &mins, const std::vector<int> &maxs,
                              const std::vector<int> &weights) {
    size_t n = mins.size();
    std::vector<int> size = mins;
    std::vector<bool> full(n, false);
    for (size_t i = 0; i < n; i++) full[i] = maxs[i] <= mins[i] || weights[i] <= 0;
    while (room > 0) {
        long wsum = 0;
        for (size_t i = 0; i < n; i++) if (!full[i]) wsum += weights[i];
        if (wsum == 0) break;
        int given = 0;
        bool capped = false;
        for (size_t i = 0; i < n; i++) {
            if (full[i]) continue;
            int add = (int)((long)room * weights[i] / wsum);
            if (size[i] + add >= maxs[i]) { add = maxs[i] - size[i]; full[i] = true; capped = true; }
            size[i] += add;
            given += add;
        }
        room -= given;
        if (!capped) {
            // rounding: the last pixels to the first objects that still grow
            for (size_t i = 0; i < n && room > 0; i++)
                if (!full[i]) { size[i]++; room--; }
            break;
        }
    }
    return size;
}

void Group::layout_line(bool horiz) {
    auto kids = areas();
    if (kids.empty()) return;
    int X = mleft(), Y = mtop() + title_h(), W = mwidth(), H = mheight() - title_h();
    size_t n = kids.size();
    std::vector<int> mins(n), maxs(n), weights(n), xmin(n), xmax(n);
    for (size_t i = 0; i < n; i++) {
        int a, b, c, d, e, f;
        kids[i]->min_max(a, b, c, d, e, f);
        mins[i] = horiz ? a : b;
        maxs[i] = horiz ? e : f;
        xmin[i] = horiz ? b : a;
        xmax[i] = horiz ? f : e;
        weights[i] = kids[i]->weight();
    }
    if ((horiz && same_w_) || (!horiz && same_h_)) {
        int m = *std::max_element(mins.begin(), mins.end());
        for (auto &v : mins) v = m;
    }
    int sp = horiz ? hsp() : vsp();
    int total = (horiz ? W : H) - sp * ((int)n - 1);
    int need = 0;
    for (int v : mins) need += v;
    auto sizes = share(std::max(0, total - need), mins, maxs, weights);
    // objects that cannot grow while there is room left: spread it as extra spacing (Zune
    // centres the objects then)
    int used = 0;
    for (int v : sizes) used += v;
    int extra = std::max(0, total - used);
    int pos = (horiz ? X : Y) + extra / 2;
    for (size_t i = 0; i < n; i++) {
        int across = horiz ? H : W;
        int xs = std::clamp(across, xmin[i], std::max(xmin[i], xmax[i]));
        int xoff = (across - xs) / 2;   // a narrower object is centred across
        if (horiz) kids[i]->resize(pos, Y + xoff, sizes[i], xs);
        else kids[i]->resize(X + xoff, pos, xs, sizes[i]);
        pos += sizes[i] + sp;
    }
}

void Group::layout_columns() {
    auto kids = areas();
    int cols = columns_, rows = ((int)kids.size() + cols - 1) / cols;
    std::vector<int> cmin(cols), cmax(cols, MAXMAX), cw(cols, 100), rmin(rows), rmax(rows, MAXMAX), rw(rows, 100);
    for (size_t i = 0; i < kids.size(); i++) {
        int c = i % cols, r = i / cols, a, b, cc, d, e, f;
        kids[i]->min_max(a, b, cc, d, e, f);
        cmin[c] = std::max(cmin[c], a); cmax[c] = std::min(cmax[c], e);
        rmin[r] = std::max(rmin[r], b); rmax[r] = std::min(rmax[r], f);
    }
    for (int c = 0; c < cols; c++) cmax[c] = std::max(cmax[c], cmin[c]);
    for (int r = 0; r < rows; r++) rmax[r] = std::max(rmax[r], rmin[r]);
    int W = mwidth() - hsp() * (cols - 1), H = mheight() - title_h() - vsp() * (rows - 1);
    int needw = 0, needh = 0;
    for (int v : cmin) needw += v;
    for (int v : rmin) needh += v;
    auto ws = share(std::max(0, W - needw), cmin, cmax, cw);
    auto hs = share(std::max(0, H - needh), rmin, rmax, rw);
    int y = mtop() + title_h();
    for (int r = 0; r < rows; r++) {
        int x = mleft();
        for (int c = 0; c < cols; c++) {
            size_t i = r * cols + c;
            if (i < kids.size()) {
                // a child narrower / lower than its cell is centred in it (layout_2d_distribute_space)
                int a, b, cc, d, e, f;
                kids[i]->min_max(a, b, cc, d, e, f);
                int cwid = std::max(std::min(e, ws[c]), a), chei = std::max(std::min(f, hs[r]), b);
                kids[i]->resize(x + (ws[c] - cwid) / 2, y + (hs[r] - chei) / 2, cwid, chei);
            }
            x += ws[c] + hsp();
        }
        y += hs[r] + vsp();
    }
}

void Group::layout() {
    if (columns_ > 1) layout_columns();
    else layout_line(dir_ == Horizontal);
    redraw();
}

void Group::draw() {
    if (damage() & ~FL_DAMAGE_CHILD) {
        // a framed group draws its background (configdata: MUII_GroupBack); the title sits on
        // the top edge of the frame, centred
        int th = title_h();
        if (bg_pen_ >= 0 || frame_spec().type != FST_NONE) {
            fl_color(pens()[bg_pen_ >= 0 ? bg_pen_ : MPEN_BACKGROUND]);
            fl_rectf(x(), y(), w(), h());
        }
        draw_frame(frame_spec(), x(), y() + th / 2, w(), h() - th / 2);
        if (th) {
            fl_font(prefs().font_bold, prefs().font_size);
            int tw = (int)fl_width(title_.c_str()) + 8;
            int tx = x() + (w() - tw) / 2;
            fl_color(pens()[MPEN_BACKGROUND]);
            fl_rectf(tx, y(), tw, th);
            fl_color(pens()[MPEN_TEXT]);
            fl_draw(title_.c_str(), tx, y(), tw, th, FL_ALIGN_CENTER);
        }
    }
    draw_children();
}

// ============================================================================================
// Register
// ============================================================================================
static const int INTERTAB = 4, TEXTSPACING = 4, REGISTERTAB_EXTRA_HEIGHT = 7;

Register::Register(const std::vector<std::string> &titles) : Group(Group::Vertical) {
    for (auto &t : titles) titles_.push_back(tr_text(t));
    background(MPEN_BACKGROUND);   // the register's own background inside its frame
}

int Register::tab_h() const { return titles_.empty() ? 0 : prefs().font_size + REGISTERTAB_EXTRA_HEIGHT; }

// SetHardCoord: the page sits inside the frame, below the tabs
void Register::hard_coord() {
    if (titles_.empty()) { inner(0, 0, 0, 0); return; }   // a page group: no tabs, no frame
    const FrameSpec &g = std_frame(Frame_Group);
    inner(g.inner_left + 1, g.inner_right + 1, tab_h() + g.inner_top, g.inner_bottom + 1);
}

void Register::end() {
    Group::end();
    hard_coord();
    for (int i = 0; i < children(); i++) if (i != active_) child(i)->hide();
}

void Register::active(int i) {
    if (i < 0 || i >= children() || i == active_) return;
    child(active_)->hide();
    active_ = i;
    child(i)->show();
    resize(x(), y(), w(), h());
    redraw();
    if (on_change) on_change(i);
}

void Register::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    // a page group: as big as the biggest page (all pages, shown or not)
    minw = minh = defw = defh = 0;
    maxw = maxh = MAXMAX;
    for (int i = 0; i < children(); i++) {
        auto *a = dynamic_cast<Area *>(child(i));
        if (!a) continue;
        int mw, mh, dw, dh, xw, xh;
        a->min_max(mw, mh, dw, dh, xw, xh);
        minw = std::max(minw, mw); minh = std::max(minh, mh);
        defw = std::max(defw, dw); defh = std::max(defh, dh);
        maxw = std::min(maxw, xw); maxh = std::min(maxh, xh);
    }
    maxw = std::max(maxw, minw);
    maxh = MAXMAX;
    if (titles_.empty()) return;
    // the tabs: room for every title (register_truncate_titles off)
    int n = std::max(1, (int)titles_.size());
    int total_hspacing = (n + 1) * INTERTAB - 2;
    fl_font(prefs().font, prefs().font_size);
    int textmax = 0;
    for (auto &t : titles_) textmax = std::max(textmax, (int)fl_width(t.c_str()));
    int tabs_def = total_hspacing + (textmax + TEXTSPACING + 1) * n - add_left() - add_right();
    minw = std::max(minw, total_hspacing * 3 - add_left() - add_right());
    defw = std::max(defw, tabs_def);
    maxw = std::max(maxw, defw);
    minw = std::max(minw, tabs_def);   // the titles are not cut
}

void Register::resize(int X, int Y, int W, int H) {
    Fl_Widget::resize(X, Y, W, H);
    for (int i = 0; i < children(); i++) child(i)->resize(mleft(), mtop(), mwidth(), mheight());
}

// LayoutTabItems (one row)
void Register::tab_x(int i, int &x1, int &x2) const {
    int n = std::max(1, (int)titles_.size());
    int total_hspacing = (n + 1) * INTERTAB - 2;
    int item_w = (w() - total_hspacing) / n, extra = (w() - total_hspacing) % n;
    int xx = INTERTAB - 1;
    for (int k = 0; k <= i; k++) {
        x1 = xx;
        x2 = x1 + item_w - 1;
        if (extra > 0) { x2++; extra--; }
        xx += (x2 - x1 + 1) + INTERTAB;
    }
}

// RenderRegisterTabItem
void Register::draw_tab(int i) {
    int x1, x2;
    tab_x(i, x1, x2);
    const Pens &P = pens();
    int top, bottom, left, right;
    if (i == active_) {
        top = y(); bottom = y() + tab_h() - 1 - 2;
        left = x() + x1 - 1; right = x() + x2 + 1;
        fl_color(P[MPEN_BACKGROUND]);
        int bw = right - left + 1;
        fl_rectf(left, top + 4, bw, tab_h() - 4);
        fl_rectf(left + 2, top + 2, bw - 4, 2);
        fl_rectf(left + 4, top + 1, bw - 8, 1);
    } else {
        top = y() + 2; bottom = y() + tab_h() - 1 - 1;
        left = x() + x1; right = x() + x2;
        fl_color(P[MPEN_BACKGROUND]);
        fl_rectf(left, top + 4, right - left + 1, bottom - top - 3);
        fl_rectf(left + 2, top + 2, right - left - 3, 2);
        fl_rectf(left + 4, top + 1, right - left - 7, 1);
    }
    auto px = [](int X, int Y) { fl_point(X, Y); };
    fl_color(P[MPEN_SHINE]);
    fl_xyline(left + 4, top, right - 4);                // top
    fl_yxline(left, top + 4, bottom);                   // left
    px(left + 1, top + 3); px(left + 1, top + 2); px(left + 2, top + 1); px(left + 3, top + 1);
    fl_color(P[MPEN_HALFSHINE]);
    px(left + 3, top); px(left + 4, top + 1); px(left + 2, top + 2); px(left + 3, top + 2);
    px(left + 2, top + 3); px(left, top + 3); px(left + 1, top + 4);
    if (i == active_) {
        fl_color(P[MPEN_SHINE]);
        px(left - 1, bottom + 1);
        fl_color(P[MPEN_SHADOW]);
        px(right + 1, bottom + 1);
        fl_color(P[MPEN_BACKGROUND]);   // the frame's top line opens under the active tab
        fl_xyline(left - 1, bottom + 2, right + 1);
    }
    fl_color(P[MPEN_SHADOW]);
    px(right - 1, top + 2);
    fl_yxline(right, top + 4, bottom);
    fl_color(P[MPEN_HALFSHADOW]);
    px(right - 2, top + 1); px(right - 1, top + 3); px(right, top + 3);
    fl_color(P[MPEN_BACKGROUND]);
    px(right - 3, top + 1);
    // the title
    int fit = right - left + 1 - TEXTSPACING;
    fl_font(prefs().font, prefs().font_size);
    fl_color(P[MPEN_TEXT]);
    fl_push_clip(left + TEXTSPACING / 2, top, fit, bottom - top + 1);
    fl_draw(titles_[i].c_str(), left + TEXTSPACING / 2, top + TEXTSPACING - 1, fit, prefs().font_size + 2, FL_ALIGN_CENTER);
    fl_pop_clip();
}

void Register::draw() {
    if (damage() & ~FL_DAMAGE_CHILD) {
        // the parent's background behind the tabs, the register's inside the frame
        Fl_Widget *p = parent();
        auto *pa = dynamic_cast<Area *>(p);
        (void)pa;
        fl_color(pens()[MPEN_BACKGROUND]);
        fl_rectf(x(), y(), w(), h());
        if (titles_.empty()) {   // a page group
            for (int i = 0; i < children(); i++) if (child(i)->visible()) child(i)->damage(FL_DAMAGE_ALL);
            draw_children();
            return;
        }
        const Pens &P = pens();
        int th = tab_h();
        int fw = w(), fh = h() - th;
        fl_color(P[MPEN_SHINE]);
        fl_yxline(x(), y() + th - 1, y() + th + fh - 1);
        fl_xyline(x() + 1, y() + th - 1, x() + fw - 2);
        fl_color(P[MPEN_SHADOW]);
        fl_yxline(x() + fw - 1, y() + th - 1, y() + th + fh - 1);
        fl_xyline(x() + 1, y() + th + fh - 1, x() + fw - 2);
        for (int i = 0; i < (int)titles_.size(); i++) draw_tab(i);
        // the page draws itself entirely
        for (int i = 0; i < children(); i++) if (child(i)->visible()) child(i)->damage(FL_DAMAGE_ALL);
    }
    draw_children();
}

int Register::handle(int e) {
    if (e == FL_PUSH && !titles_.empty() && Fl::event_button() == FL_LEFT_MOUSE && Fl::event_y() < y() + tab_h()) {
        for (int i = 0; i < (int)titles_.size(); i++) {
            int x1, x2;
            tab_x(i, x1, x2);
            if (Fl::event_x() >= x() + x1 && Fl::event_x() <= x() + x2) { active(i); return 1; }
        }
        return 1;
    }
    return Group::handle(e);
}

// ============================================================================================
// Rectangle
// ============================================================================================
void Rectangle::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = minh = defw = defh = 0;
    maxw = kind_ == 'v' ? 0 : MAXMAX;
    maxh = kind_ == 'h' ? 0 : MAXMAX;
}

// ============================================================================================
// Text
// ============================================================================================
Text::Text(const std::string &t, StdFrame f) : Area(f), text_(tr_text(t)) {
    if (f == Frame_Text) background(MPEN_BACKGROUND);
}

// Zune's control sequences: ESC c / r / l alignment, ESC b bold, ESC n normal, ESC - stop
std::vector<Text::Line> Text::parse() const {
    std::vector<Line> lines;
    std::string all = preparse_ + text_;
    int align = FL_ALIGN_LEFT;
    bool bold = false;
    Line cur{"", align, bold};
    for (size_t i = 0; i < all.size(); i++) {
        char ch = all[i];
        if (ch == '\033' && i + 1 < all.size()) {
            char k = all[++i];
            if (k == 'c') cur.align = align = FL_ALIGN_CENTER;
            else if (k == 'r') cur.align = align = FL_ALIGN_RIGHT;
            else if (k == 'l') cur.align = align = FL_ALIGN_LEFT;
            else if (k == 'b') cur.bold = bold = true;
            else if (k == 'n') cur.bold = bold = false;
            else if (k == '-') { cur.text += all.substr(i + 1); break; }
            continue;
        }
        if (ch == '\n') {
            lines.push_back(cur);
            cur = Line{"", align, bold};
            continue;
        }
        cur.text += ch;
    }
    lines.push_back(cur);
    return lines;
}

void Text::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    auto lines = parse();
    int wmax = 0;
    for (auto &l : lines) {
        fl_font(l.bold ? prefs().font_bold : prefs().font, prefs().font_size);
        wmax = std::max(wmax, (int)fl_width(l.text.c_str()));
    }
    int lh = prefs().font_size + 3;
    minw = defw = wmax;
    maxw = setmax_ ? wmax : MAXMAX;
    minh = defh = maxh = lh * (int)lines.size();
}

void Text::draw_contents() {
    auto lines = parse();
    int lh = prefs().font_size + 3;
    int y0 = mtop() + (mheight() - lh * (int)lines.size()) / 2;
    bool sel = selected_ && input_ != Input_None;
    for (size_t i = 0; i < lines.size(); i++) {
        fl_font(lines[i].bold ? prefs().font_bold : prefs().font, prefs().font_size);
        fl_color(active_r() ? pens()[sel ? MPEN_SHINE : MPEN_TEXT] : pens()[MPEN_HALFSHADOW]);
        fl_draw(lines[i].text.c_str(), mleft(), y0 + (int)i * lh, mwidth(), lh, (Fl_Align)(lines[i].align | FL_ALIGN_CLIP));
    }
}

// MUI_MakeObject(MUIO_Button): a centred text in the button frame, RelVerify, background
// "0:128", the selected background "0:131"
Button::Button(const std::string &label) : Text("\033c" + label, Frame_Button) {
    background(MPEN_BACKGROUND);
    input_mode(Input_RelVerify);
}

// ============================================================================================
// String
// ============================================================================================
String::String(const std::string &contents, int maxlen) : Area(Frame_String) {
    background(MPEN_BACKGROUND);
    begin();
    in_ = new Fl_Input(0, 0, 10, 10);
    in_->box(FL_FLAT_BOX);
    // configdata: the background "2:m2" (the half shine pen), active "2:m1"
    in_->color(pens()[MPEN_HALFSHINE]);
    in_->selection_color(pens()[MPEN_FILL]);
    in_->textfont(prefs().font);
    in_->textsize(prefs().font_size);
    in_->value(contents.c_str());
    if (maxlen) in_->maximum_size(maxlen);
    in_->when(FL_WHEN_ENTER_KEY_ALWAYS);
    in_->callback([](Fl_Widget *, void *d) {
        auto *s = (String *)d;
        if (s->on_acknowledge) s->on_acknowledge();
    }, this);
    end();
}

void String::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    int lh = prefs().font_size + 4;
    minw = 30; defw = 200; maxw = MAXMAX;
    minh = defh = maxh = lh;
}

void String::resize(int X, int Y, int W, int H) {
    Fl_Widget::resize(X, Y, W, H);
    in_->resize(mleft(), mtop(), mwidth(), mheight());
}

// ============================================================================================
// Prop
// ============================================================================================
Prop::Prop(bool horiz) : Area(Frame_Prop), horiz_(horiz) {
    background(MPEN_HALFSHADOW);   // the track
    inner(1, 1, 1, 1);
}

void Prop::set(int entries, int visible, int first) {
    entries_ = std::max(1, entries);
    visible_ = std::clamp(visible, 1, entries_);
    first_ = std::clamp(first, 0, entries_ - visible_);
    redraw();
}

void Prop::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    if (horiz_) { minw = 20; defw = 100; maxw = MAXMAX; minh = defh = maxh = 10; }
    else { minh = 20; defh = 100; maxh = MAXMAX; minw = defw = maxw = 10; }
}

void Prop::knob(int &kx, int &ky, int &kw, int &kh) const {
    int len = horiz_ ? mwidth() : mheight();
    int kl = std::max(8, (int)((long long)len * visible_ / entries_));
    int range = entries_ - visible_;
    int kp = range > 0 ? (int)((long long)(len - kl) * first_ / range) : 0;
    if (horiz_) { kx = mleft() + kp; ky = mtop(); kw = kl; kh = mheight(); }
    else { kx = mleft(); ky = mtop() + kp; kw = mwidth(); kh = kl; }
}

void Prop::draw_contents() {
    int kx, ky, kw, kh;
    knob(kx, ky, kw, kh);
    fl_color(pens()[MPEN_BACKGROUND]);
    fl_rectf(kx, ky, kw, kh);
    draw_frame(FST_BEVEL, grab_ >= 0, kx, ky, kw, kh);
}

int Prop::handle(int e) {
    int pos = horiz_ ? Fl::event_x() : Fl::event_y();
    switch (e) {
    case FL_PUSH: {
        int kx, ky, kw, kh;
        knob(kx, ky, kw, kh);
        int ks = horiz_ ? kx : ky, kl = horiz_ ? kw : kh;
        if (pos >= ks && pos < ks + kl) { grab_ = pos; grab_first_ = first_; redraw(); }
        else {   // a click in the track: a page towards it
            int nf = first_ + (pos < ks ? -visible_ : visible_);
            set(entries_, visible_, nf);
            if (on_change) on_change(first_);
        }
        return 1;
    }
    case FL_DRAG:
        if (grab_ >= 0) {
            int len = (horiz_ ? mwidth() : mheight());
            int kl = std::max(8, (int)((long long)len * visible_ / entries_));
            int range = entries_ - visible_;
            if (len > kl && range > 0) {
                int nf = grab_first_ + (int)((long long)(pos - grab_) * range / (len - kl));
                int old = first_;
                set(entries_, visible_, nf);
                if (first_ != old && on_change) on_change(first_);
            }
        }
        return 1;
    case FL_RELEASE:
        grab_ = -1;
        redraw();
        return 1;
    }
    return Area::handle(e);
}

// ============================================================================================
// Cycle
// ============================================================================================
Cycle::Cycle(std::vector<std::string> entries, int active) : Area(Frame_Button), entries_(std::move(entries)) {
    for (auto &e : entries_) e = tr(e);
    background(MPEN_BACKGROUND);
    active_ = std::clamp(active, 0, std::max(0, (int)entries_.size() - 1));
}

void Cycle::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    fl_font(prefs().font, prefs().font_size);
    int w = 0;
    for (auto &e : entries_) w = std::max(w, (int)fl_width(e.c_str()));
    minw = defw = w + 26;
    maxw = MAXMAX;
    minh = defh = maxh = prefs().font_size + 3;
}

void Cycle::draw_contents() {
    const Pens &p = pens();
    // the cycle mark: a looping arrow at the left, a separator
    int X = mleft(), Y = mtop(), H = mheight();
    fl_color(p[MPEN_TEXT]);
    fl_arc(X + 2, Y + H / 2 - 5, 10, 10, 60, 360);
    fl_polygon(X + 9, Y + H / 2 - 7, X + 13, Y + H / 2 - 4, X + 9, Y + H / 2 - 1);
    fl_color(p[MPEN_SHADOW]);
    fl_yxline(X + 16, Y, Y + H - 1);
    fl_color(p[MPEN_SHINE]);
    fl_yxline(X + 17, Y, Y + H - 1);
    fl_color(selected_ ? p[MPEN_SHINE] : p[MPEN_TEXT]);
    fl_font(prefs().font, prefs().font_size);
    if (!entries_.empty())
        fl_draw(entries_[active_].c_str(), X + 20, Y, mwidth() - 20, H, (Fl_Align)(FL_ALIGN_CENTER | FL_ALIGN_CLIP));
}

// The pop-up (cycle.c MakePopupWin, RenderPopupItem): a borderless window below the cycle
// with every entry; the one under the pointer filled with the fill pen
static const int POPITEM_EXTRAWIDTH = 4, POPITEM_EXTRAHEIGHT = 2, CYCLE_IMAGE_W = 18;

class CyclePopup : public Fl_Menu_Window {
public:
    CyclePopup(int X, int Y, int W, int H, const std::vector<std::string> &entries, int item_w, int item_h,
               int offx, int offy)
        : Fl_Menu_Window(X, Y, W, H), entries_(entries), iw_(item_w), ih_(item_h), ox_(offx), oy_(offy) {
        end();
        clear_border();
        set_override();
    }
    int item_at(int xr, int yr) const {
        int lx = xr - x() - ox_, ly = yr - y() - oy_;
        if (lx < 0 || lx >= iw_ || ly < 0) return -1;
        int i = ly / ih_;
        return i < (int)entries_.size() ? i : -1;
    }
    void highlight(int i) { if (i != active_) { active_ = i; redraw(); } }
    int highlighted() const { return active_; }
    void draw() override {
        const Pens &p = pens();
        fl_color(p[MPEN_BACKGROUND]);   // MUII_PopupBack
        fl_rectf(0, 0, w(), h());
        draw_frame(std_frame(Frame_PopUp), 0, 0, w(), h());
        fl_font(prefs().font, prefs().font_size);
        for (int i = 0; i < (int)entries_.size(); i++) {
            int x1 = ox_, y1 = oy_ + i * ih_;
            if (i == active_) {
                int off = 0;
                if (prefs().cycle_menu_recessed) {
                    fl_color(p[MPEN_SHADOW]);
                    fl_yxline(x1, y1, y1 + ih_ - 1);
                    fl_xyline(x1 + 1, y1, x1 + iw_ - 2);
                    fl_color(p[MPEN_SHINE]);
                    fl_yxline(x1 + iw_ - 1, y1, y1 + ih_ - 1);
                    fl_xyline(x1 + 1, y1 + ih_ - 1, x1 + iw_ - 2);
                    off = 1;
                }
                fl_color(p[MPEN_FILL]);
                fl_rectf(x1 + off, y1 + off, iw_ - 2 * off, ih_ - 2 * off);
            }
            fl_color(p[MPEN_TEXT]);
            fl_draw(entries_[i].c_str(), x1, y1, iw_, ih_, FL_ALIGN_CENTER);
        }
    }

private:
    std::vector<std::string> entries_;
    int iw_, ih_, ox_, oy_, active_ = -1;
};

void Cycle::close_popup() {
    if (!popup_) return;
    popup_->hide();
    delete popup_;
    popup_ = nullptr;
}

int Cycle::handle(int e) {
    switch (e) {
    case FL_KEYBOARD:
        // the active object: Space / Down the next entry, Up the one before (wrapping)
        if (Fl::focus() == this && !entries_.empty()) {
            int n = (int)entries_.size(), k = Fl::event_key();
            if (k == ' ' || k == FL_Down || k == FL_Up) {
                bool back = k == FL_Up || Fl::event_state(FL_SHIFT);
                active_ = (active_ + (back ? n - 1 : 1)) % n;
                redraw();
                if (on_change) on_change(active_);
                return 1;
            }
        }
        return 0;
    case FL_PUSH: {
        if (Fl::event_button() != FL_LEFT_MOUSE) return 0;
        // a press right of the cycle image opens the pop-up, if there are entries enough
        // (cycle_menu_min_entries 2; the menu below the gadget: CYCLE_MENU_POSITION_BELOW)
        if (Fl::event_x() > mleft() + CYCLE_IMAGE_W && (int)entries_.size() >= prefs().cycle_menu_min && window()) {
            const FrameSpec &pf = std_frame(Frame_PopUp);
            FrameBorders b = frame_borders(pf.type, pf.down);
            int item_w = mwidth() - CYCLE_IMAGE_W + POPITEM_EXTRAWIDTH;
            int item_h = prefs().font_size + 3 + POPITEM_EXTRAHEIGHT + (prefs().cycle_menu_recessed ? 2 : 0);
            int offx = pf.inner_left + b.left, offy = pf.inner_top + b.top;
            int ww = item_w + offx + pf.inner_right + b.right;
            int wh = item_h * (int)entries_.size() + offy + pf.inner_bottom + b.bottom;
            int wx = window()->x_root() + mleft() + CYCLE_IMAGE_W - offx - POPITEM_EXTRAWIDTH / 2;
            int wy = prefs().cycle_menu_position == 0
                         ? window()->y_root() + y() + h()   // below
                         : window()->y_root() + mtop() - offy - POPITEM_EXTRAHEIGHT / 2 - active_ * item_h;   // on the active entry
            int sx, sy, sw, sh;
            Fl::screen_xywh(sx, sy, sw, sh);
            if (wy + wh > sy + sh) wy = std::max(sy, window()->y_root() + y() - wh);   // WA_AutoAdjust
            Fl_Group *saved = Fl_Group::current();
            Fl_Group::current(nullptr);
            popup_ = new CyclePopup(wx, wy, ww, wh, entries_, item_w, item_h, offx, offy);
            Fl_Group::current(saved);
            popup_->show();
            return 1;
        }
        selected(true);
        return 1;
    }
    case FL_DRAG:
        if (popup_) { popup_->highlight(popup_->item_at(Fl::event_x_root(), Fl::event_y_root())); return 1; }
        selected(Fl::event_inside(this));
        return 1;
    case FL_RELEASE:
        if (popup_) {
            // released over an entry: that one; elsewhere: nothing changes
            int i = popup_->item_at(Fl::event_x_root(), Fl::event_y_root());
            close_popup();
            if (i >= 0 && i != active_) {
                active_ = i;
                redraw();
                if (on_change) on_change(active_);
            }
            return 1;
        }
        if (selected_ && !entries_.empty()) {
            // Shift steps backwards
            int n = (int)entries_.size();
            active_ = (active_ + (Fl::event_state(FL_SHIFT) ? n - 1 : 1)) % n;
            if (on_change) on_change(active_);
        }
        selected(false);
        return 1;
    }
    return Area::handle(e);
}

// ============================================================================================
// Checkmark
// ============================================================================================
Checkmark::Checkmark(bool on) : Area(Frame_ImageButton) {
    background(MPEN_BACKGROUND);
    input_mode(Input_Toggle);
    selected_ = on;
}

void Checkmark::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = defw = maxw = 18;
    minh = defh = maxh = 13;
}

void Checkmark::draw_contents() {
    // the tick in the text pen (the background stays the button's)
    if (!selected_) return;
    int X = mleft(), Y = mtop(), W = mwidth(), H = mheight();
    fl_color(pens()[MPEN_TEXT]);
    fl_line_style(FL_SOLID, 2);
    fl_line(X + 2, Y + H / 2, X + W / 2 - 1, Y + H - 2, X + W - 2, Y + 1);
    fl_line_style(0);
}

// ============================================================================================
// List
// ============================================================================================
List::List() : Area(Frame_ReadList) {
    background(MPEN_SHINE);   // configdata: the list background ("0:128" on a white screen pen)
    begin();
    prop_ = new Prop(false);
    end();
    prop_->on_change = [this](int f) { top_ = f; redraw(); };
}

void List::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = 80; defw = 300; maxw = MAXMAX;
    minh = line_h() * 3; defh = line_h() * 12; maxh = MAXMAX;
}

void List::resize(int X, int Y, int W, int H) {
    Fl_Widget::resize(X, Y, W, H);
    prop_->resize(x() + w() - add_right() - 14, mtop(), 14, mheight());
    layout_prop();
}

void List::layout_prop() {
    int n = (int)lines_.size(), v = visible_lines();
    top_ = std::clamp(top_, 0, std::max(0, n - v));
    prop_->set(std::max(n, v), v, top_);
}

void List::active(int a) {
    active_ = std::clamp(a, -1, (int)lines_.size() - 1);
    if (active_ >= 0) {   // kept in view
        if (active_ < top_) top_ = active_;
        if (active_ >= top_ + visible_lines()) top_ = active_ - visible_lines() + 1;
        layout_prop();
    }
    redraw();
}

void List::draw_contents() {
    const Pens &p = pens();
    int lh = line_h(), W = mwidth() - 16;
    fl_font(prefs().font, prefs().font_size);
    for (int i = top_; i < (int)lines_.size() && (i - top_) * lh < mheight(); i++) {
        int y = mtop() + (i - top_) * lh;
        bool act = i == active_;
        if (act) { fl_color(p[MPEN_FILL]); fl_rectf(mleft(), y, W, lh); }
        auto &l = lines_[i];
        int infow = l.info.empty() ? 0 : (int)fl_width(l.info.c_str()) + 8;
        fl_color(act ? p[MPEN_SHINE] : p[MPEN_TEXT]);
        fl_draw(l.text.c_str(), mleft() + 3, y, W - infow - 6, lh, (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP));
        if (infow) {
            fl_color(act ? p[MPEN_SHINE] : (l.highlight ? p[MPEN_SHINE] : p[MPEN_TEXT]));
            if (l.highlight && !act) {   // "Drawer": in the highlight colours (white on the fill pen)
                fl_color(p[MPEN_HALFSHADOW]);
                fl_rectf(mleft() + W - infow, y + 1, infow - 2, lh - 2);
                fl_color(p[MPEN_SHINE]);
            }
            fl_draw(l.info.c_str(), mleft() + W - infow, y, infow - 4, lh, FL_ALIGN_RIGHT);
        }
    }
}

int List::handle(int e) {
    switch (e) {
    case FL_PUSH: {
        if (Fl::event_x() >= prop_->x()) return Area::handle(e);
        take_focus();
        int i = top_ + (Fl::event_y() - mtop()) / line_h();
        if (i < 0 || i >= (int)lines_.size()) return 1;
        bool dbl = Fl::event_clicks() && i == active_;
        active(i);
        if (dbl) { Fl::event_clicks(0); if (on_double) on_double(i); }
        else if (on_active) on_active(i);
        return 1;
    }
    case FL_MOUSEWHEEL:
        top_ += Fl::event_dy() * 3;
        layout_prop();
        redraw();
        return 1;
    case FL_FOCUS: case FL_UNFOCUS: focus_changed(); return 1;
    case FL_KEYBOARD: {
        int k = Fl::event_key();
        if (k == FL_Down || k == FL_Up) {
            active(active_ + (k == FL_Down ? 1 : -1));
            if (on_active && active_ >= 0) on_active(active_);
            return 1;
        }
        if ((k == FL_Enter || k == FL_KP_Enter) && active_ >= 0 && on_double) { on_double(active_); return 1; }
        return 0;
    }
    }
    return Area::handle(e);
}

// ============================================================================================
// Gauge
// ============================================================================================
// ============================================================================================
// Slider
// ============================================================================================
Slider::Slider(int min, int max, int value) : Area(Frame_Slider), min_(min), max_(std::max(min, max)) {
    value_ = std::clamp(value, min_, max_);
    background(MPEN_BACKGROUND);   // MUII_SliderBack "0:128"
}

void Slider::value(int v) {
    v = std::clamp(v, min_, max_);
    if (v == value_) return;
    value_ = v;
    redraw();
    if (on_change) on_change(v);
}

// CalcKnobDimensions: as wide as the widest number, with the knob frame around it
int Slider::knob_w() const {
    fl_font(prefs().font, prefs().font_size);
    int wmax = std::max((int)fl_width(std::to_string(min_).c_str()), (int)fl_width(std::to_string(max_).c_str()));
    const FrameSpec &k = std_frame(Frame_Knob);
    FrameBorders b = frame_borders(k.type, k.down);
    return wmax + b.left + b.right + k.inner_left + k.inner_right;
}

int Slider::knob_h() const {
    const FrameSpec &k = std_frame(Frame_Knob);
    FrameBorders b = frame_borders(k.type, k.down);
    return prefs().font_size + 2 + b.top + b.bottom + k.inner_top + k.inner_bottom;
}

// MUIM_Numeric_ValueToScale
int Slider::knob_x() const {
    int scale = mwidth() - knob_w();
    if (max_ == min_ || scale <= 0) return mleft();
    return mleft() + (int)((long)scale * (value_ - min_) / (max_ - min_));
}

void Slider::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = knob_w() + 1;
    defw = knob_w() * 4 + 2;
    maxw = MAXMAX;
    minh = defh = maxh = knob_h();
}

void Slider::draw_contents() {
    int kx = knob_x(), ky = mtop(), kw = knob_w(), kh = mheight();
    const FrameSpec &k = std_frame(Frame_Knob);
    fl_color(pens()[MPEN_BACKGROUND]);   // MUII_SliderKnob
    fl_rectf(kx, ky, kw, kh);
    draw_frame(k.type, k.down != dragging_, kx, ky, kw, kh);
    fl_font(prefs().font, prefs().font_size);
    fl_color(pens()[MPEN_TEXT]);
    fl_draw(std::to_string(value_).c_str(), kx, ky, kw, kh, FL_ALIGN_CENTER);
}

int Slider::handle(int e) {
    if (disabled_) return Fl_Group::handle(e);
    switch (e) {
    case FL_PUSH: {
        if (Fl::event_button() != FL_LEFT_MOUSE) return 0;
        take_focus();
        int kx = knob_x();
        if (Fl::event_x() >= kx && Fl::event_x() < kx + knob_w()) {
            // on the knob: dragging starts
            dragging_ = true;
            grab_dx_ = Fl::event_x() - kx;
            redraw();
        } else value(value_ + (Fl::event_x() < kx ? -1 : 1));   // beside it: a step towards the click
        return 1;
    }
    case FL_DRAG:
        if (dragging_) {
            int scale = mwidth() - knob_w();
            if (scale > 0) {
                int pos = std::clamp(Fl::event_x() - grab_dx_ - mleft(), 0, scale);
                value(min_ + (int)(((long)pos * (max_ - min_) + scale / 2) / scale));
            }
        }
        return 1;
    case FL_RELEASE:
        if (dragging_) { dragging_ = false; redraw(); }
        return 1;
    case FL_FOCUS: case FL_UNFOCUS: focus_changed(); return 1;
    case FL_KEYBOARD:
        if (Fl::event_key() == FL_Left || Fl::event_key() == FL_Down) { value(value_ - 1); return 1; }
        if (Fl::event_key() == FL_Right || Fl::event_key() == FL_Up) { value(value_ + 1); return 1; }
        if (Fl::event_key() == FL_Home) { value(min_); return 1; }
        if (Fl::event_key() == FL_End) { value(max_); return 1; }
        return 0;
    case FL_MOUSEWHEEL:
        value(value_ - Fl::event_dy());
        return 1;
    }
    return Area::handle(e);
}

// ============================================================================================
// Scale
// ============================================================================================
Scale::Scale() : Area(Frame_None) { background(-1); }

void Scale::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    fl_font(prefs().font, prefs().font_size);
    minw = defw = (int)fl_width("0%100%");   // minlabel
    maxw = MAXMAX;
    minh = defh = maxh = prefs().font_size + 4;
}

void Scale::draw_contents() {
    int L = mleft(), T = mtop(), W = mwidth(), R = L + W - 1;
    fl_font(prefs().font, prefs().font_size);
    fl_color(pens()[MPEN_TEXT]);
    fl_xyline(L, T + 3, R);   // the main bar
    // the layout: 0..100, 0..50..100, ... as many divisions as the labels allow
    int i;
    char buf[16];
    for (i = 2; i < W; i *= 2) {
        int total = 0;
        bool too_big = false;
        for (int j = 0; j <= i; j++) {
            snprintf(buf, sizeof buf, "%d%%", j);
            total += (int)fl_width(buf);
            if (total > 3 * W / 8) { too_big = true; break; }
        }
        if (too_big) break;
    }
    int ty = T + 4 + fl_height() - fl_descent();
    bool drawpct = true;
    for (int k = 0; k <= i; k++) {
        int x = L + W * k / i;
        if (k == 0) {
            fl_draw("0%", L, ty);
            fl_yxline(L, T, T + 3);
            drawpct = false;
        } else if (k == i) {
            fl_draw("100%", R + 1 - (int)fl_width("100%"), ty);
            fl_yxline(R, T, T + 3);
            drawpct = false;
        } else if (drawpct) {
            snprintf(buf, sizeof buf, "%d%%", k * 100 / i);
            fl_draw(buf, x - (int)fl_width(buf) / 2, ty);
            fl_yxline(x, T, T + 3);
            drawpct = false;
        } else {
            fl_yxline(x, T + 1, T + 3);
            drawpct = true;
        }
    }
}

Gauge::Gauge() : Area(Frame_Gauge) {
    background(MPEN_BACKGROUND);
    inner(1, 1, 1, 1);
}

void Gauge::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = 60; defw = 260; maxw = MAXMAX;
    minh = defh = maxh = prefs().font_size + 6;
}

void Gauge::draw_contents() {
    const Pens &p = pens();
    int fw = (int)((long double)mwidth() * std::clamp(cur_, 0LL, max_) / max_);
    fl_color(p[MPEN_FILL]);
    fl_rectf(mleft(), mtop(), fw, mheight());
    if (!info_.empty()) {
        fl_font(prefs().font, prefs().font_size);
        fl_color(p[MPEN_TEXT]);
        fl_draw(info_.c_str(), mleft(), mtop(), mwidth(), mheight(), FL_ALIGN_CENTER);
    }
}

// ============================================================================================
// ImageButton
// ============================================================================================
namespace {
// Move / Draw of the RastPort
struct Pen { int x = 0, y = 0; void move(int X, int Y) { x = X; y = Y; } void draw(int X, int Y) { fl_line(x, y, X, Y); x = X; y = Y; } };

void popup_draw(int left, int top, int width, int height) {
    Pen r;
    height -= 3;
    fl_color(pens()[MPEN_TEXT]);
    int cx = width / 2;
    r.move(left + 1, top); r.draw(left + width - cx, top + height - 1);
    r.move(left, top); r.draw(left + width - cx - 1, top + height - 1);
    r.move(left + width - 1 - 1, top); r.draw(left + cx - 1, top + height - 1);
    r.move(left + width - 1, top); r.draw(left + cx, top + height - 1);
    int bottom = top + height - 1 + 3, right = left + width - 1;
    r.move(left, bottom - 2); r.draw(right, bottom - 2);
    r.move(left, bottom - 1); r.draw(right, bottom - 1);
}

void popfile_draw(int left, int top, int width, int height) {
    Pen r;
    int right = left + width - 1, bottom = top + height - 1;
    int edgex = left + width * 5 / 8, edgey = top + height * 5 / 8;
    fl_color(pens()[MPEN_TEXT]);
    r.move(left, top); r.draw(left, bottom);
    r.move(left + 1, top); r.draw(left + 1, bottom);
    r.move(left, bottom); r.draw(right, bottom);
    r.move(left, bottom - 1); r.draw(right, bottom - 1);
    r.move(right, bottom - 1); r.draw(right, edgey);
    r.move(right - 1, bottom - 1); r.draw(right - 1, edgey);
    r.move(right, edgey - 1); r.draw(edgex, edgey - 1); r.draw(edgex, top); r.draw(left + 2, top);
    r.move(left + 2, top + 1); r.draw(edgex - 1, top + 1);
    r.move(edgex + 1, top); r.draw(right, edgey - 1);
}

void popdrawer_draw(int left, int top, int width, int height) {
    Pen r;
    int right = left + width - 1, bottom = top + height - 1;
    int halfx = (left + right) / 2, halfy = (top + bottom) / 2, quartery = top + height / 4;
    fl_color(pens()[MPEN_TEXT]);
    r.move(left, quartery); r.draw(left, bottom);
    r.move(left + 1, quartery); r.draw(left + 1, bottom); r.draw(right, bottom); r.draw(right, halfy);
    r.draw(halfx, halfy); r.draw(halfx, quartery); r.draw(left + 1, quartery);
    r.move(halfx, quartery - 1); r.draw(halfx + 2, top); r.draw(right - 2, top); r.draw(right, quartery - 1);
    r.draw(right, halfy);
}
}  // namespace

ImageButton *PopButton(PopImage which) {
    // the vector images are 10 x 11 at the least (vector_table)
    auto *b = new ImageButton(10, 11, [which](int x, int y, int w, int h, bool) {
        switch (which) {
        case Pop_Up: popup_draw(x, y, w, h); break;
        case Pop_File: popfile_draw(x, y, w, h); break;
        case Pop_Drawer: popdrawer_draw(x, y, w, h); break;
        }
    });
    return b;
}
ImageButton::ImageButton(int w, int h, std::function<void(int, int, int, int, bool)> image)
    : Area(Frame_ImageButton), iw_(w), ih_(h), image_(std::move(image)) {
    background(MPEN_BACKGROUND);
    input_mode(Input_RelVerify);
}

void ImageButton::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = defw = maxw = iw_;
    minh = defh = maxh = ih_;
}

void ImageButton::draw_contents() {
    if (image_) image_(mleft(), mtop(), mwidth(), mheight(), selected_);
}

// ============================================================================================
// Window
// ============================================================================================
Window::Window(const std::string &title, Area *r) : Fl_Double_Window(100, 100), title_(tr_text(title)) {
    label(title_.c_str());
    color(pens()[MPEN_BACKGROUND]);
    end();
    if (r) root(r);
}

void Window::root(Area *r) {
    if (root_) remove(root_);
    root_ = r;
    add(r);
    fit();
}

void Window::fit() {
    if (!root_) return;
    int a, b, c, d, e, f;
    root_->min_max(a, b, c, d, e, f);
    auto &p = prefs();
    int ew = p.window_left + p.window_right, eh = p.window_top + p.window_bottom;
    size(c + ew, d + eh);
    size_range(a + ew, b + eh, e >= MAXMAX ? 0 : e + ew, f >= MAXMAX ? 0 : f + eh);
}

void Window::resize(int X, int Y, int W, int H) {
    Fl_Double_Window::resize(X, Y, W, H);   // the X window too (and the asked-for position)
    if (root_) {
        auto &p = prefs();
        root_->resize(p.window_left, p.window_top, W - p.window_left - p.window_right, H - p.window_top - p.window_bottom);
    }
}

// the first text field inside (MUIA_Window_ActiveObject of a window with a string)
static Fl_Widget *first_input(Fl_Group *g) {
    for (int i = 0; i < g->children(); i++) {
        Fl_Widget *c = g->child(i);
        if (!c->visible() || !c->active()) continue;
        if (auto *in = dynamic_cast<Fl_Input *>(c)) { if (!in->readonly()) return c; continue; }
        if (auto *cg = c->as_group())
            if (Fl_Widget *f = first_input(cg)) return f;
    }
    return nullptr;
}

// the objects the keyboard can go to, in order (the cycle chain)
static void chain(Fl_Group *g, std::vector<Fl_Widget *> &out) {
    for (int i = 0; i < g->children(); i++) {
        Fl_Widget *c = g->child(i);
        if (!c->visible() || !c->active()) continue;
        if (auto *a = dynamic_cast<Area *>(c); a && a->accepts_focus() && a->children() == 0) { out.push_back(c); continue; }
        if (dynamic_cast<Fl_Input *>(c)) { out.push_back(c); continue; }
        if (auto *cg = c->as_group()) chain(cg, out);
    }
}

Window::~Window() = default;

int Window::handle(int e) {
    if (Fl::focus() && Fl::focus() != this && Fl::focus()->inside(this) &&
        (!last_focus_ || last_focus_->widget() != Fl::focus()))
        last_focus_ = std::make_unique<Fl_Widget_Tracker>(Fl::focus());
    if (e == FL_FOCUS && (!Fl::focus() || !Fl::focus()->inside(this)) && last_focus_ && last_focus_->exists() &&
        last_focus_->widget()->visible_r()) {
        // back to the window: its active object again
        last_focus_->widget()->take_focus();
        return 1;
    }
    if (e == FL_KEYBOARD && Fl::event_key() == FL_Tab && Fl::focus() == this) {
        // no active object yet: Tab takes the first, Shift+Tab the last
        std::vector<Fl_Widget *> c;
        chain(this, c);
        if (!c.empty()) { (Fl::event_state(FL_SHIFT) ? c.back() : c.front())->take_focus(); return 1; }
    }
    if (e == FL_FOCUS && (!Fl::focus() || !Fl::focus()->inside(this))) {
        // a window coming active has no active object, unless it has a text field: the
        // keyboard goes to that one (a requester's buttons are not pressed by a stray key)
        if (Fl_Widget *f = first_input(this)) { f->take_focus(); return 1; }
        Fl::focus(this);
        return 1;
    }
    int r = Fl_Double_Window::handle(e);
    if (getenv("ZUNE_DEBUG") && (e == FL_PUSH || e == FL_RELEASE || e == FL_ENTER || e == FL_LEAVE))
        fprintf(stderr, "[%s] ev %d at %d,%d -> %d pushed=%p below=%p grab=%p modal=%p\n", label(), e, Fl::event_x(), Fl::event_y(), r,
                (void *)Fl::pushed(), (void *)Fl::belowmouse(), (void *)Fl::grab(), (void *)Fl::modal());
    return r;
}

void Window::draw() {
    if (damage() & ~FL_DAMAGE_CHILD) {
        // configdata: the window background "0:128"
        fl_color(pens()[MPEN_BACKGROUND]);
        fl_rectf(0, 0, w(), h());
    }
    draw_children();
}

int run() { return Fl::run(); }

}  // namespace zune
