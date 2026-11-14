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

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>

namespace zune {

Prefs &prefs() {
    static Prefs p;
    return p;
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
int Area::add_left() const { return frame_borders(frame_.type, frame_.down).left + (inner_set_ ? il_ : frame_.inner_left); }
int Area::add_right() const { return frame_borders(frame_.type, frame_.down).right + (inner_set_ ? ir_ : frame_.inner_right); }
int Area::add_top() const { return frame_borders(frame_.type, frame_.down).top + (inner_set_ ? it_ : frame_.inner_top); }
int Area::add_bottom() const { return frame_borders(frame_.type, frame_.down).bottom + (inner_set_ ? ib_ : frame_.inner_bottom); }

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
        draw_frame(frame_, x(), y(), w(), h(), selected_ && (input_ == Input_RelVerify || input_ == Input_Immediate));
        fl_push_clip(mleft(), mtop(), mwidth(), mheight());
        draw_contents();
        fl_pop_clip();
    }
    draw_children();
}

int Area::handle(int e) {
    if (input_ == Input_None || disabled_) return Fl_Group::handle(e);
    switch (e) {
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
Group::Group(Dir d, StdFrame f) : Area(f), dir_(d), hs_(prefs().group_hspacing), vs_(prefs().group_vspacing) {
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
        minw += hs_ * (cols - 1); defw += hs_ * (cols - 1); maxw += hs_ * (cols - 1);
        minh += vs_ * (rows - 1); defh += vs_ * (rows - 1); maxh += vs_ * (rows - 1);
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
        int sp = (horiz ? hs_ : vs_) * ((int)kids.size() - 1);
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
    int sp = horiz ? hs_ : vs_;
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
    int W = mwidth() - hs_ * (cols - 1), H = mheight() - title_h() - vs_ * (rows - 1);
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
            if (i < kids.size()) kids[i]->resize(x, y, ws[c], hs[r]);
            x += ws[c] + hs_;
        }
        y += hs[r] + vs_;
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
        if (bg_pen_ >= 0 || frame_.type != FST_NONE) {
            fl_color(pens()[bg_pen_ >= 0 ? bg_pen_ : MPEN_BACKGROUND]);
            fl_rectf(x(), y(), w(), h());
        }
        draw_frame(frame_, x(), y() + th / 2, w(), h() - th / 2);
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
Text::Text(const std::string &t, StdFrame f) : Area(f), text_(t) {
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

int Cycle::handle(int e) {
    switch (e) {
    case FL_PUSH: selected(true); return 1;
    case FL_DRAG: selected(Fl::event_inside(this)); return 1;
    case FL_RELEASE:
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
// ImageButton
// ============================================================================================
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
Window::Window(const std::string &title, Area *r) : Fl_Double_Window(100, 100), title_(title) {
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
    Fl_Widget::resize(X, Y, W, H);
    if (root_) {
        auto &p = prefs();
        root_->resize(p.window_left, p.window_top, W - p.window_left - p.window_right, H - p.window_top - p.window_bottom);
    }
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
