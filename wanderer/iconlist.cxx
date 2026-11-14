/*
    Copyright (C) 2002-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    IconList, IconDrawerList, IconVolumeList (iconlist.h). Ported from
    workbench/system/Wanderer/Classes/iconlist.c, icondrawerlist.c and iconvolumelist.c:
    the label splitting, the label / area rectangles, the grid and rough positioning, the
    sorting and the selection follow those sources; drawing and input are FLTK's.
*/
#include "iconlist.h"
#include "icons.h"
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/platform.H>
#include <X11/Xatom.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string_view>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/xattr.h>
#include <fnmatch.h>
#include <strings.h>

namespace fs = std::filesystem;
using namespace zune;

namespace wanderer {

static const int ILC_ICONLABEL_SHORTEST = 3;

IconList::IconList() {
    frame(Frame_None);
    background(MPEN_BACKGROUND);
    label_pen = pens()[MPEN_SHINE];          // Setup: the label in the shine pen ...
    label_shadow_pen = pens()[MPEN_SHADOW];  // ... with the shadow pen around
    info_pen = pens()[MPEN_SHINE];
}

IconList::~IconList() {
    if (backfill_scaled_) backfill_scaled_->release();
    if (backfill_) backfill_->release();
}


// Is the pointer over a window of another program? (the deepest window under it and its
// parents: none of them one of ours - the backdrop counts as ours)
static bool pointer_over_foreign_window() {
    Display *d = fl_display;
    ::Window root = RootWindow(d, fl_screen), w = root, child = 0;
    int rx, ry, wx, wy;
    unsigned mask;
    ::Window r, c;
    if (!XQueryPointer(d, root, &r, &c, &rx, &ry, &wx, &wy, &mask)) return false;
    bool any = false;
    for (int depth = 0; depth < 32; depth++) {
        int x, y;
        if (!XTranslateCoordinates(d, root, w, rx, ry, &x, &y, &child) || !child) break;
        w = child;
        any = true;
        if (fl_find(w)) return false;   // one of ours
    }
    return any;
}

// The selected icons as a list of file URIs (text/uri-list, lines ending in CR LF)
static std::string uri_list(const std::vector<IconEntry *> &sel) {
    std::string out;
    for (auto *e : sel) {
        if (e->unmounted || e->path.empty() || e->path[0] != '/') continue;
        out += "file://";
        for (unsigned char c : e->path) {
            if (isalnum(c) || strchr("/-_.~", c)) out += (char)c;
            else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); out += b; }
        }
        out += "\r\n";
    }
    return out;
}

void IconList::backfill(const std::string &path, int mode) {
    if (path == backfill_path_ && mode == backfill_mode_) return;
    static bool registered = false;
    if (!registered) { fl_register_images(); registered = true; }
    if (backfill_scaled_) { backfill_scaled_->release(); backfill_scaled_ = nullptr; }
    if (backfill_) { backfill_->release(); backfill_ = nullptr; }
    backfill_path_ = path;
    backfill_mode_ = mode;
    if (!path.empty()) {
        backfill_ = Fl_Shared_Image::get(path.c_str());
        if (backfill_ && (backfill_->w() <= 0 || backfill_->h() <= 0)) { backfill_->release(); backfill_ = nullptr; }
    }
    redraw();
}

// The background: the picture tiled (moving with the view, as Wanderer's backfill does),
// scaled to the view or centred on the background pen
void IconList::draw_background(int X, int Y, int W, int H) {
    if (!backfill_) { Area::draw_background(X, Y, W, H); return; }
    fl_push_clip(X, Y, W, H);
    if (backfill_mode_ == 1) {
        if (!backfill_scaled_ || backfill_scaled_->w() != W || backfill_scaled_->h() != H) {
            if (backfill_scaled_) backfill_scaled_->release();
            backfill_scaled_ = (Fl_Shared_Image *)backfill_->copy(W, H);
        }
        backfill_scaled_->draw(X, Y);
    } else if (backfill_mode_ == 2) {
        fl_color(pens()[MPEN_BACKGROUND]);
        fl_rectf(X, Y, W, H);
        backfill_->draw(X + (W - backfill_->w()) / 2, Y + (H - backfill_->h()) / 2);
    } else {
        int tw = backfill_->w(), th = backfill_->h();
        // TileMode_Float: the tiles move with the view; TileMode_Fixed (mode 3): they stay
        int sx = X - (view_x_ % tw), sy = Y - (view_y_ % th);
        if (backfill_mode_ == 3) { sx = X; sy = Y; }
        for (int ty = sy; ty < Y + H; ty += th)
            for (int tx = sx; tx < X + W; tx += tw) backfill_->draw(tx, ty);
    }
    fl_pop_clip();
}

void IconList::clear() {
    entries_.clear();
    pressed_ = nullptr;
    view_x_ = view_y_ = 0;
}

IconEntry &IconList::add(IconEntry e) {
    entries_.push_back(std::move(e));
    return entries_.back();
}

std::vector<IconEntry *> IconList::selection() {
    std::vector<IconEntry *> v;
    for (auto &e : entries_) if (e.selected && e.visible) v.push_back(&e);
    return v;
}

void IconList::select_all() {
    for (auto &e : entries_) if (e.visible) e.selected = true;
    redraw();
    if (on_selection_changed) on_selection_changed();
}

void IconList::unselect_all() {
    for (auto &e : entries_) e.selected = e.focus = false;
    redraw();
    if (on_selection_changed) on_selection_changed();
}

// ---------------------------------------------------------------------------------------------
// Labels (IconList__LabelFunc_SplitLabel / CreateLabel)
// ---------------------------------------------------------------------------------------------
static int text_len(const std::string &s, size_t start, size_t n) {
    return (int)fl_width(s.c_str() + start, (int)std::min(n, s.size() - start));
}

void IconList::split_label(IconEntry &e) {
    std::string label = e.label;
    size_t len = label.size();
    if (trim_volume_names && e.type == ST_ROOT && len && label[len - 1] == ':') len--;
    if ((size_t)label_maxlen >= len) return;
    // the width of label_maxlen characters is the width of a line; a line breaks at a
    // space, a dot or a dash near that width, keeping at least ILC_ICONLABEL_SHORTEST
    // characters on each side
    int txwidth = text_len(label, 0, label_maxlen);
    size_t done = 0;
    std::vector<std::string> parts;
    while (done < len) {
        while (done < len && label[done] == ' ') done++;
        if (done >= len) break;
        long cur = (long)(len - done);
        while (cur > 1 && text_len(label, done, cur) > txwidth) cur--;
        long remaining = (long)len - (long)(done + cur);
        if (remaining > 0) {
            int off = 0;
            bool found = false;
            while (off < cur - ILC_ICONLABEL_SHORTEST) {
                char back = label[done + cur - off], fwd = done + cur + off < len ? label[done + cur + off] : 0;
                if (cur - off > ILC_ICONLABEL_SHORTEST && (back == ' ' || back == '.' || back == '-')) {
                    cur -= off; found = true; break;
                }
                if (remaining - off < 0) break;
                if (remaining - off >= ILC_ICONLABEL_SHORTEST && (fwd == ' ' || fwd == '.' || fwd == '-')) {
                    cur += off; found = true; break;
                }
                off++;
            }
            remaining = (long)len - (long)(done + cur);
            if (!found && remaining > 0 && remaining <= ILC_ICONLABEL_SHORTEST)
                cur += remaining - ILC_ICONLABEL_SHORTEST;
        }
        if (cur < 1) cur = 1;
        if (done + cur > len) cur = (long)(len - done);
        parts.push_back(label.substr(done, cur));
        done += cur;
    }
    if (parts.size() > 1) e.lines = parts;
}

void IconList::create_label(IconEntry &e) {
    fl_font(label_font(), label_font_size());
    e.lines.clear();
    if (label_multiline > 1) split_label(e);
    if (e.lines.empty()) {
        std::string l = e.label;
        if (trim_volume_names && e.type == ST_ROOT && !l.empty() && l.back() == ':') l.pop_back();
        if ((int)l.size() > label_maxlen && label_maxlen > 3) l = l.substr(0, label_maxlen - 3) + " ..";
        e.lines.push_back(l);
    }
    // more parts than lines: the last shown line ends with " .."
    if ((int)e.lines.size() > label_multiline) {
        e.lines.resize(label_multiline);
        std::string &last = e.lines.back();
        if (last.size() > 3) last = last.substr(0, last.size() - 3);
        last += " ..";
    }
    e.label_w = 0;
    for (auto &l : e.lines) e.label_w = std::max(e.label_w, (int)fl_width(l.c_str()));
    // sorted by size or date: the size or date under the label (not for drawers)
    e.info.clear();
    if (e.type != ST_USERDIR && (sort_flags & (Sort_BySize | Sort_ByDate))) {
        char b[64];
        if (sort_flags & Sort_BySize) {
            double s = (double)e.size;
            if (e.size < 1024) snprintf(b, sizeof b, "%lld", e.size);
            else if (s < 1024.0 * 1024) snprintf(b, sizeof b, "%.1fK", s / 1024);
            else if (s < 1024.0 * 1024 * 1024) snprintf(b, sizeof b, "%.1fM", s / 1024 / 1024);
            else snprintf(b, sizeof b, "%.1fG", s / 1024 / 1024 / 1024);
        } else {
            struct tm tm, now;
            time_t t = time(nullptr);
            localtime_r(&e.mtime, &tm);
            localtime_r(&t, &now);
            bool today = tm.tm_year == now.tm_year && tm.tm_yday == now.tm_yday;
            strftime(b, sizeof b, today ? "%H:%M:%S" : "%d-%m-%y", &tm);
        }
        e.info = b;
    }
}

// ---------------------------------------------------------------------------------------------
// Rectangles (IconList_GetIconLabelRectangle / GetIconAreaRectangle)
// ---------------------------------------------------------------------------------------------
void IconList::icon_size(IconEntry &e, int &w, int &h) {
    if (display_flags & Disp_NoIcons) { w = h = 0; return; }
    w = ICON_W;
    h = ICON_H;
}

void IconList::label_rect(IconEntry &e, int &w, int &h) {
    int outline = text_mode == TextMode_DropShadow ? 1 : text_mode == TextMode_Plain ? 0 : 2;
    int fh = label_font_size() + 2;
    w = (label_hpad + label_border_w) * 2 + e.label_w + outline;
    int lines = std::max(1, std::min((int)e.lines.size(), label_multiline));
    h = (label_border_h + label_vpad) * 2 + (fh + outline) * lines;
    if (!e.info.empty()) {
        fl_font(label_font(), label_font_size() - 1);
        int tw = (int)fl_width(e.info.c_str());
        h += label_font_size() + 1 + outline;
        w = std::max(w, tw + outline + (label_vpad + label_border_w) * 2);
    }
    label_largest_h_ = std::max(label_largest_h_, h);
}

void IconList::area_rect(IconEntry &e) {
    icon_size(e, e.icon_w, e.icon_h);
    icon_largest_h_ = std::max(icon_largest_h_, e.icon_h);
    e.area_w = e.icon_w;
    e.area_h = list_mode == ListMode_Grid ? icon_largest_h_ : e.icon_h;
    int lw, lh;
    label_rect(e, lw, lh);
    e.area_w = std::max(e.area_w, lw);
    e.area_h += image_spacing + lh;
    area_largest_w_ = std::max(area_largest_w_, e.area_w);
    area_largest_h_ = std::max(area_largest_h_, e.area_h);
}

// ---------------------------------------------------------------------------------------------
// Sorting (MUIM_IconList_Sort)
// ---------------------------------------------------------------------------------------------
static std::string ext_of(const std::string &n) {
    auto d = n.rfind('.');
    return d == std::string::npos || d == 0 ? "" : n.substr(d + 1);
}

void IconList::sort() {
    int f = sort_flags;
    std::stable_sort(entries_.begin(), entries_.end(), [f](const IconEntry &a, const IconEntry &b) {
        // volumes always first, then drawers before (or after) files unless mixed
        if ((a.type == ST_ROOT) != (b.type == ST_ROOT)) return a.type == ST_ROOT;
        if (!(f & Sort_DrawersMixed) && a.is_drawer() != b.is_drawer())
            return (f & Sort_DrawersLast) ? !a.is_drawer() : a.is_drawer();
        int c = 0;
        if (f & Sort_ByDate) c = a.mtime < b.mtime ? -1 : a.mtime > b.mtime ? 1 : 0;
        else if (f & Sort_BySize) c = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
        else if (f & Sort_ByType) c = strcasecmp(ext_of(a.label).c_str(), ext_of(b.label).c_str());
        if (c == 0) c = strcasecmp(a.label.c_str(), b.label.c_str());
        if (f & Sort_Reverse) c = -c;
        return c < 0;
    });
}

// ---------------------------------------------------------------------------------------------
// Positioning (MUIM_IconList_RethinkDimensions, IconList_Layout_FullAutoLayout /
// PartialAutoLayout)
// ---------------------------------------------------------------------------------------------
void IconList::rethink() {
    icon_largest_h_ = area_largest_w_ = area_largest_h_ = label_largest_h_ = 0;
    for (auto &e : entries_) if (e.visible) create_label(e);
    for (auto &e : entries_) if (e.visible) area_rect(e);
    if (list_mode == ListMode_Grid)   // the grid: every area as high as the highest icon
        for (auto &e : entries_) if (e.visible) area_rect(e);
    position_icons();
    redraw();
    if (on_view_changed) on_view_changed();
}

void IconList::position_icons() {
    if ((display_flags & Disp_ModeMask) == Disp_ModeList) {
        int fh = label_font_size() + 6, y = fh + 2;
        content_w_ = 0;
        for (auto &e : entries_) if (e.visible) { e.x = 0; e.y = y; y += fh; }
        content_h_ = y;
        content_w_ = mwidth();
        return;
    }
    const bool vertical = display_flags & Disp_Vertical;
    const bool autosort = sort_flags & Sort_AutoSort;
    const int left = hspacing, top = vspacing;
    const int view_w = mwidth(), view_h = mheight();
    // icons with a snapshot position keep it (unless sorting automatically); the others
    // go to free places in reading order
    std::vector<std::array<int, 4>> occupied;
    if (!autosort)
        for (auto &e : entries_)
            if (e.visible && e.provided_x != NO_ICON_POSITION && e.provided_y != NO_ICON_POSITION)
                occupied.push_back({e.provided_x, e.provided_y, e.area_w + (vertical ? 0 : hspacing),
                                    e.area_h + (vertical ? vspacing : 0)});
    auto free_at = [&](int x, int y, int w, int h) {
        for (auto &r : occupied)
            if (x < r[0] + r[2] && r[0] < x + w && y < r[1] + r[3] && r[1] < y + h) return false;
        return true;
    };
    int cur_x = left, cur_y = top;
    int maxw = 0, maxh = 0;
    size_t line_first = 0;
    std::vector<IconEntry *> vis;
    for (auto &e : entries_) if (e.visible) vis.push_back(&e);
    for (size_t i = 0; i < vis.size(); i++) {
        IconEntry &e = *vis[i];
        if (!autosort && e.provided_x != NO_ICON_POSITION && e.provided_y != NO_ICON_POSITION) {
            e.x = e.provided_x;
            e.y = e.provided_y;
            continue;
        }
        int gridx, gridy;
        if (list_mode == ListMode_Grid) {
            gridx = area_largest_w_ + hspacing;
            gridy = icon_largest_h_ + image_spacing + label_largest_h_ + vspacing;
        } else if (vertical) {
            gridx = area_largest_w_;
            gridy = e.area_h + vspacing;
        } else {
            gridx = e.area_w + hspacing;
            gridy = area_largest_h_ + vspacing;
        }
        // find a free place from the current one on
        for (int guard = 0; guard < 10000; guard++) {
            if (free_at(cur_x, cur_y, e.area_w, e.area_h)) break;
            if (vertical) {
                cur_y += gridy;
                if (cur_y + e.area_h - border_overlap >= view_h) { cur_y = top; cur_x += gridx; }
            } else {
                cur_x += gridx;
                if (cur_x + e.area_w - border_overlap >= view_w) { cur_x = left; cur_y += gridy; }
            }
        }
        e.x = cur_x;
        e.y = cur_y;
        // rough mode, vertical: icons of a column centred on the widest
        if (list_mode == ListMode_Rough && vertical) e.x += (area_largest_w_ - e.area_w) / 2;
        maxw = std::max(maxw, gridx);
        maxh = std::max(maxh, gridy);
        (void)line_first;
        // the next place (wrap at the edge of the view)
        if (vertical) {
            cur_y += gridy;
            int next_h = i + 1 < vis.size() ? vis[i + 1]->area_h : 0;
            if (cur_y >= view_h || cur_y + (list_mode == ListMode_Grid ? area_largest_h_ : next_h) - border_overlap >= view_h) {
                cur_x += maxw;
                cur_y = top;
                maxw = 0;
            }
        } else {
            cur_x += gridx;
            int next_w = i + 1 < vis.size() ? vis[i + 1]->area_w : 0;
            if (cur_x >= view_w || cur_x + (list_mode == ListMode_Grid ? area_largest_w_ : next_w) - border_overlap >= view_w) {
                cur_x = left;
                cur_y += maxh;
                maxh = 0;
            }
        }
    }
    content_w_ = content_h_ = 0;
    for (auto *e : vis) {
        content_w_ = std::max(content_w_, e->x + e->area_w + hspacing);
        content_h_ = std::max(content_h_, e->y + e->area_h + vspacing);
    }
}

void IconList::scroll_to(int x, int y) {
    x = std::clamp(x, 0, std::max(0, content_w_ - mwidth()));
    y = std::clamp(y, 0, std::max(0, content_h_ - mheight()));
    if (x == view_x_ && y == view_y_) return;
    view_x_ = x;
    view_y_ = y;
    redraw();
    if (on_view_changed) on_view_changed();
}

void IconList::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = 40; minh = 40;
    defw = 300; defh = 200;
    maxw = maxh = MAXMAX;
}

void IconList::resize(int X, int Y, int W, int H) {
    bool sized = W != w() || H != h();
    Area::resize(X, Y, W, H);
    if (sized) {
        position_icons();
        scroll_to(view_x_, view_y_);
        if (on_view_changed) on_view_changed();
    }
}

// ---------------------------------------------------------------------------------------------
// Drawing (MUIM_Draw, MUIM_IconList_DrawEntry, MUIM_IconList_DrawEntryLabel)
// ---------------------------------------------------------------------------------------------
// The image of an entry (icons.cxx): by its type and name
void IconList::draw_icon_image(IconEntry &e, int x, int y, bool sel) {
    if (display_flags & Disp_NoIcons) return;
    IconKind k = icon_kind(e.path, e.is_drawer() && e.type != ST_ROOT, e.type == ST_ROOT, e.executable);
    if (e.type == ST_LINKFILE) k = IconKind::Link;
    if (e.trash) k = IconKind::Trash;
    if (e.unmounted) k = IconKind::Removable;
    if (e.network) k = IconKind::Network;
    if (e.network_volume) k = IconKind::NetworkDisk;
    draw_icon(k, x, y, sel);
    if (e.unmounted) {
        // ghosted: every other pixel in the background pen (as a disabled gadget)
        fl_color(pens()[MPEN_BACKGROUND]);
        for (int gy = y; gy < y + ICON_H; gy++)
            for (int gx = x + (gy & 1); gx < x + ICON_W; gx += 2) fl_point(gx, gy);
    }
}

static void outlined_text(const char *s, int tx, int ty, TextMode mode, Fl_Color pen, Fl_Color shadow) {
    switch (mode) {
    case TextMode_DropShadow:
        fl_color(shadow); fl_draw(s, tx + 1, ty + 1);
        [[fallthrough]];
    case TextMode_Plain:
        fl_color(pen); fl_draw(s, tx, ty);
        break;
    default:   // outline: the shadow pen one pixel around, then the text
        fl_color(shadow);
        fl_draw(s, tx + 1, ty); fl_draw(s, tx - 1, ty); fl_draw(s, tx, ty - 1); fl_draw(s, tx, ty + 1);
        fl_color(pen); fl_draw(s, tx, ty);
    }
}

void IconList::draw_entry_label(IconEntry &e, int lx, int ly) {
    int lw, lh;
    int save_largest = label_largest_h_;
    label_rect(e, lw, lh);
    label_largest_h_ = save_largest;
    // the label area as wide as the entry's area, the text centred in it
    int ax = lx + (e.area_w - lw) / 2;
    if (e.focus && label_border_w > 0) {
        // the focused entry: a dotted frame around its label (InvertPixelArray in the sources)
        fl_color(pens()[MPEN_SHADOW]);
        fl_line_style(FL_DOT);
        fl_rect(ax, ly, lw, lh);
        fl_line_style(0);
    }
    int outline = text_mode == TextMode_DropShadow ? 1 : text_mode == TextMode_Plain ? 0 : 2;
    int fh = label_font_size() + 2;
    int tyy = ly + label_border_h + label_vpad;
    fl_font(label_font(text_mode == TextMode_Outline), label_font_size());
    int txt_area = lw - (label_border_w + label_hpad) * 2;
    size_t shown = label_multiline_on_focus && !e.focus ? 1 : e.lines.size();
    for (size_t i = 0; i < shown && i < e.lines.size(); i++) {
        std::string line = e.lines[i];
        if (shown < e.lines.size() && line.size() > 3) line = line.substr(0, line.size() - 3) + " ..";
        int tw = (int)fl_width(line.c_str());
        int tx = ax + label_border_w + label_hpad + std::max(0, (txt_area - tw) / 2) + outline / 2;
        outlined_text(line.c_str(), tx, tyy + fh - fl_descent() + outline / 2, text_mode, label_pen, label_shadow_pen);
        tyy += fh + outline;
    }
    if (!e.info.empty()) {
        fl_font(label_font(), label_font_size() - 1);
        int tw = (int)fl_width(e.info.c_str());
        int tx = ax + label_border_w + label_hpad + std::max(0, (txt_area - tw) / 2);
        outlined_text(e.info.c_str(), tx, tyy + label_font_size() - fl_descent(), text_mode, info_pen, label_shadow_pen);
    }
}

void IconList::draw_entry(IconEntry &e) {
    int ox = mleft() - view_x_, oy = mtop() - view_y_;
    int ex = ox + e.x, ey = oy + e.y;
    if (!fl_not_clipped(ex - 2, ey - 2, e.area_w + 4, e.area_h + 4)) return;
    // the image: centred in the entry's area; in the grid also in the grid cell
    int offx = (e.area_w - e.icon_w) / 2, offy = 0;
    if (list_mode == ListMode_Grid) offy = icon_largest_h_ - e.icon_h;
    draw_icon_image(e, ex + offx, ey + offy, e.selected);
    int ly = ey + (list_mode == ListMode_Grid ? icon_largest_h_ : e.icon_h) + image_spacing;
    draw_entry_label(e, ex, ly);
}

// the protection as ls shows it: d / l / -, then rwx for the owner, the group, the others
static std::string protection(unsigned m) {
    std::string r = S_ISDIR(m) ? "d" : S_ISLNK(m) ? "l" : "-";
    const char *c = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) r += (m & (0400 >> i)) ? c[i] : '-';
    return r;
}

void IconList::draw_list_mode() {
    // the list view: a header and one line per entry - Name, Size, Protection, Date, Comment
    const Pens &p = pens();
    int fh = label_font_size() + 6;
    int X = mleft(), Y = mtop(), W = mwidth();
    fl_font(label_font(), label_font_size());
    int wsize = (int)fl_width("0000000000") + 8, wprot = (int)fl_width("drwxrwxrwx") + 12,
        wdate = (int)fl_width("00-00-00 00:00") + 12;
    int wname = std::max(80, (W - wsize - wprot - wdate) * 55 / 100);
    int cname = X, csize = cname + wname, cprot = csize + wsize, cdate = cprot + wprot, ccom = cdate + wdate;
    int cols[6] = {cname, csize, cprot, cdate, ccom, X + W};
    std::copy(cols, cols + 6, col_x_);
    for (auto &e : entries_) {
        if (!e.visible) continue;
        int y = Y + e.y - view_y_;
        if (y + fh < Y || y > Y + mheight()) continue;
        if (e.selected) { fl_color(p[MPEN_FILL]); fl_rectf(X, y, W, fh); }
        fl_color(e.selected ? p[MPEN_SHINE] : p[MPEN_TEXT]);
        fl_draw(e.label.c_str(), cname + 4, y, wname - 8, fh, (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP));
        std::string s = e.is_drawer() ? "Drawer" : std::to_string(e.size);
        fl_draw(s.c_str(), csize, y, wsize - 6, fh, (Fl_Align)(FL_ALIGN_RIGHT | FL_ALIGN_CLIP));
        if (e.mode) fl_draw(protection(e.mode).c_str(), cprot + 6, y, wprot - 8, fh, (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP));
        char b[32];
        struct tm tm;
        localtime_r(&e.mtime, &tm);
        strftime(b, sizeof b, "%d-%m-%y %H:%M", &tm);
        fl_draw(b, cdate + 6, y, wdate - 8, fh, (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP));
        if (!e.comment.empty())
            fl_draw(e.comment.c_str(), ccom + 6, y, X + W - ccom - 8, fh, (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP));
    }
    // the header on top
    fl_draw_box(FL_FLAT_BOX, X, Y, W, fh, p[MPEN_BACKGROUND]);
    for (auto [cx, cw, t] : {std::tuple{cname, wname, "Name"}, {csize, wsize, "Size"}, {cprot, wprot, "Protection"},
                             {cdate, wdate, "Date"}, {ccom, std::max(0, X + W - ccom), "Comment"}}) {
        if (cw <= 0) continue;
        draw_frame(FST_BEVEL, false, cx, Y, cw, fh);
        fl_color(p[MPEN_TEXT]);
        fl_draw(t, cx + 4, Y, cw - 8, fh, (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP));
        // the order: a small arrow on the column sorted by (down: reversed)
        bool by = (!strcmp(t, "Name") && !(sort_flags & (Sort_ByDate | Sort_BySize | Sort_ByType))) ||
                  (!strcmp(t, "Size") && (sort_flags & Sort_BySize)) || (!strcmp(t, "Date") && (sort_flags & Sort_ByDate));
        if (by && cw > 30) {
            int ax = cx + cw - 12, ay = Y + fh / 2;
            if (sort_flags & Sort_Reverse) fl_polygon(ax, ay - 2, ax + 7, ay - 2, ax + 3, ay + 3);
            else fl_polygon(ax, ay + 2, ax + 7, ay + 2, ax + 3, ay - 3);
        }
    }
}

void IconList::draw_contents() {
    if ((display_flags & Disp_ModeMask) == Disp_ModeList) { draw_list_mode(); return; }
    for (auto &e : entries_) if (e.visible) draw_entry(e);
    if (lasso_) {
        int ox = mleft() - view_x_, oy = mtop() - view_y_;
        int x1 = std::min(lasso_x_, press_x_), y1 = std::min(lasso_y_, press_y_);
        int x2 = std::max(lasso_x_, press_x_), y2 = std::max(lasso_y_, press_y_);
        fl_color(pens()[MPEN_SHADOW]);
        fl_line_style(FL_DASH);
        fl_rect(ox + x1, oy + y1, x2 - x1 + 1, y2 - y1 + 1);
        fl_line_style(0);
    }
}

// ---------------------------------------------------------------------------------------------
// Input (MUIM_HandleEvent): click, Shift-click, double click, lasso, moving icons, keys
// ---------------------------------------------------------------------------------------------
// The cursor keys: the focus goes to the nearest icon in that direction (selected alone, or
// added to the selection with Shift); kept in view
void IconList::move_focus(int key, bool extend) {
    IconEntry *cur = nullptr;
    for (auto &e : entries_) if (e.focus && e.visible) cur = &e;
    IconEntry *best = nullptr;
    if (!cur) {
        for (auto &e : entries_) if (e.visible) { best = &e; break; }
    } else {
        long bestd = 0;
        int cx = cur->x + cur->area_w / 2, cy = cur->y + cur->area_h / 2;
        for (auto &e : entries_) {
            if (!e.visible || &e == cur) continue;
            int ex = e.x + e.area_w / 2, ey = e.y + e.area_h / 2, dx = ex - cx, dy = ey - cy;
            bool ok = (key == FL_Left && dx < 0 && std::abs(dy) <= std::abs(dx) * 2) ||
                      (key == FL_Right && dx > 0 && std::abs(dy) <= std::abs(dx) * 2) ||
                      (key == FL_Up && dy < 0 && std::abs(dx) <= std::abs(dy) * 2) ||
                      (key == FL_Down && dy > 0 && std::abs(dx) <= std::abs(dy) * 2);
            if (!ok) continue;
            // along the direction counts once, across it four times
            long along = (key == FL_Left || key == FL_Right) ? std::abs(dx) : std::abs(dy);
            long across = (key == FL_Left || key == FL_Right) ? std::abs(dy) : std::abs(dx);
            long d = along + across * 4;
            if (!best || d < bestd) { best = &e; bestd = d; }
        }
    }
    if (!best) return;
    for (auto &e : entries_) {
        e.focus = false;
        if (!extend) e.selected = false;
    }
    best->focus = true;
    best->selected = true;
    // in view
    int vx = view_x_, vy = view_y_;
    if (best->x < vx) vx = best->x - hspacing;
    if (best->x + best->area_w > vx + mwidth()) vx = best->x + best->area_w - mwidth() + hspacing;
    if (best->y < vy) vy = best->y - vspacing;
    if (best->y + best->area_h > vy + mheight()) vy = best->y + best->area_h - mheight() + vspacing;
    scroll_to(vx, vy);
    redraw();
    if (on_selection_changed) on_selection_changed();
}

IconEntry *IconList::entry_at(int ex, int ey) {
    int vx = ex - mleft() + view_x_, vy = ey - mtop() + view_y_;
    if ((display_flags & Disp_ModeMask) == Disp_ModeList) {
        int fh = label_font_size() + 6;
        for (auto &e : entries_) if (e.visible && vy >= e.y && vy < e.y + fh) return &e;
        return nullptr;
    }
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        auto &e = *it;
        if (!e.visible) continue;
        // the image or the label - not the empty corners of the area
        int offx = (e.area_w - e.icon_w) / 2, offy = list_mode == ListMode_Grid ? icon_largest_h_ - e.icon_h : 0;
        bool in_img = vx >= e.x + offx && vx < e.x + offx + e.icon_w && vy >= e.y + offy && vy < e.y + offy + e.icon_h;
        int lw, lh, save = label_largest_h_;
        label_rect(e, lw, lh);
        label_largest_h_ = save;
        int lx = e.x + (e.area_w - lw) / 2, ly = e.y + (list_mode == ListMode_Grid ? icon_largest_h_ : e.icon_h) + image_spacing;
        bool in_lbl = vx >= lx && vx < lx + lw && vy >= ly && vy < ly + lh;
        if (in_img || in_lbl) return &e;
    }
    return nullptr;
}

int IconList::handle(int ev) {
    switch (ev) {
    // files dragged here from another program
    case FL_DND_ENTER: case FL_DND_DRAG: case FL_DND_LEAVE: return on_external_drop ? 1 : 0;
    case FL_DND_RELEASE: return on_external_drop ? 1 : 0;
    case FL_PASTE: {
        if (!on_external_drop || !Fl::event_text()) return 0;
        std::vector<std::string> paths;
        std::istringstream in(Fl::event_text());
        std::string line;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line.rfind("file://", 0) == 0) {
                line = line.substr(7);
                if (line.rfind("localhost", 0) == 0) line = line.substr(9);
                std::string dec;   // %20 and friends
                for (size_t i = 0; i < line.size(); i++) {
                    if (line[i] == '%' && i + 2 < line.size()) { dec += (char)std::stoi(line.substr(i + 1, 2), nullptr, 16); i += 2; }
                    else dec += line[i];
                }
                line = dec;
            }
            if (!line.empty() && line[0] == '/') paths.push_back(line);
        }
        if (paths.empty()) return 0;
        IconEntry *target = entry_at(Fl::event_x(), Fl::event_y());
        if (target && !target->is_drawer()) target = nullptr;
        on_external_drop(paths, target);
        return 1;
    }
    case FL_FOCUS: case FL_UNFOCUS: return 1;
    case FL_PUSH: {
        take_focus();
        // the list's header: a click sorts by that column (again: the other way round)
        if ((display_flags & Disp_ModeMask) == Disp_ModeList && Fl::event_button() == FL_LEFT_MOUSE &&
            Fl::event_y() >= mtop() && Fl::event_y() < mtop() + label_font_size() + 6) {
            int col = -1;
            for (int i = 0; i < 5; i++) if (Fl::event_x() >= col_x_[i] && Fl::event_x() < col_x_[i + 1]) col = i;
            int order = col == 1 ? Sort_BySize : col == 3 ? Sort_ByDate : Sort_ByName;
            if ((sort_flags & Sort_Orders) == order || (order == Sort_ByName && !(sort_flags & (Sort_ByDate | Sort_BySize | Sort_ByType))))
                sort_flags ^= Sort_Reverse;
            else sort_flags = (sort_flags & ~(Sort_Orders | Sort_Reverse)) | order;
            sort();
            rethink();
            if (on_selection_changed) on_selection_changed();   // (the menus show the order)
            return 1;
        }
        if (Fl::event_button() == FL_RIGHT_MOUSE) {
            // the menus are the screen's (the right button over a window shows them)
            if (on_context_menu) on_context_menu(Fl::event_x_root(), Fl::event_y_root());
            return 1;
        }
        if (Fl::event_button() != FL_LEFT_MOUSE) return 0;
        IconEntry *e = entry_at(Fl::event_x(), Fl::event_y());
        press_x_ = Fl::event_x() - mleft() + view_x_;
        press_y_ = Fl::event_y() - mtop() + view_y_;
        moved_ = false;
        bool shift = Fl::event_state(FL_SHIFT);
        if (e) {
            if (Fl::event_clicks() && (e->selected || shift)) {
                // a double click opens it; without Shift the others are unselected first, with
                // Shift they stay selected (the arguments of a tool)
                Fl::event_clicks(0);
                if (!shift) for (auto &o : entries_) if (&o != e) o.selected = false;
                e->selected = true;
                redraw();
                if (on_selection_changed) on_selection_changed();
                if (on_open) on_open(*e);
                return 1;
            }
            if (!shift && !e->selected)
                for (auto &o : entries_) o.selected = false;
            if (shift && e->selected) e->selected = false;
            else e->selected = true;
            for (auto &o : entries_) o.focus = false;
            e->focus = true;
            pressed_ = e;
            dragging_ = e->selected;
            redraw();
            if (on_selection_changed) on_selection_changed();
        } else {
            // the empty space: a lasso (Shift keeps the selection)
            if (!shift) for (auto &o : entries_) o.selected = o.focus = false;
            lasso_before_.clear();
            for (auto &o : entries_) lasso_before_.push_back(o.selected);
            lasso_ = true;
            lasso_x_ = press_x_;
            lasso_y_ = press_y_;
            pressed_ = nullptr;
            redraw();
            if (on_selection_changed) on_selection_changed();
        }
        return 1;
    }
    case FL_DRAG: {
        int vx = Fl::event_x() - mleft() + view_x_, vy = Fl::event_y() - mtop() + view_y_;
        if (lasso_) {
            lasso_x_ = vx;
            lasso_y_ = vy;
            int x1 = std::min(lasso_x_, press_x_), y1 = std::min(lasso_y_, press_y_);
            int x2 = std::max(lasso_x_, press_x_), y2 = std::max(lasso_y_, press_y_);
            size_t i = 0;
            for (auto &o : entries_) {
                bool in = o.visible && o.x < x2 && o.x + o.area_w > x1 && o.y < y2 && o.y + o.area_h > y1;
                o.selected = (i < lasso_before_.size() && lasso_before_[i]) != in;   // Shift toggles
                i++;
            }
            redraw();
            // scroll while the lasso reaches past an edge
            if (Fl::event_y() > mtop() + mheight()) scroll_to(view_x_, view_y_ + 10);
            else if (Fl::event_y() < mtop()) scroll_to(view_x_, view_y_ - 10);
            return 1;
        }
        if (dragging_ && (std::abs(vx - press_x_) > 3 || std::abs(vy - press_y_) > 3)) moved_ = true;
        if (moved_ && pointer_over_foreign_window()) {
            // out over another program's window: the files go on as a drag and drop of
            // file URIs (XDND), the icons go back to their places
            std::string uris = uri_list(selection());
            moved_ = dragging_ = false;
            position_icons();
            redraw();
            if (!uris.empty()) {
                Fl::copy(uris.c_str(), (int)uris.size(), 0);
                Fl::dnd();
            }
            return 1;
        }
        if (moved_) {
            // the selected icons follow the pointer (MUIM_CreateDragImage in the sources)
            int dx = vx - press_x_, dy = vy - press_y_;
            press_x_ = vx;
            press_y_ = vy;
            for (auto &o : entries_)
                if (o.selected && o.visible) { o.x += dx; o.y += dy; }
            redraw();
        }
        return 1;
    }
    case FL_RELEASE: {
        if (lasso_) {
            lasso_ = false;
            redraw();
            if (on_selection_changed) on_selection_changed();
            return 1;
        }
        if (moved_) {
            moved_ = false;
            dragging_ = false;
            auto sel = selection();
            // dropped on a drawer of this list: into it; otherwise the icons stay where
            // they were put (their positions become their snapshot positions)
            IconEntry *target = nullptr;
            for (auto &o : entries_) {
                if (o.selected || !o.visible || !(o.is_drawer() || o.app_window)) continue;   // AppIcons take drops
                int vx = Fl::event_x() - mleft() + view_x_, vy = Fl::event_y() - mtop() + view_y_;
                if (vx >= o.x && vx < o.x + o.area_w && vy >= o.y && vy < o.y + o.area_h) { target = &o; break; }
            }
            if (!Fl::event_inside(this) || target) {
                if (on_drop) on_drop(sel, target);
                if (target) position_icons();
            } else {
                for (auto *o : sel) { o->provided_x = o->x; o->provided_y = o->y; }
                position_icons();
            }
            redraw();
            if (on_view_changed) on_view_changed();
            return 1;
        }
        dragging_ = false;
        return 1;
    }
    case FL_MOUSEWHEEL: {
        // DoWheelMove: the vertical wheel scrolls sideways when there is nothing to scroll
        // down or with Alt; Ctrl goes to the edge, Shift a page, else 30 pixels a step
        int wx = Fl::event_dx(), wy = Fl::event_dy();
        if (wy && !wx && (content_h_ <= mheight() || Fl::event_state(FL_ALT))) { wx = wy; wy = 0; }
        int nx = view_x_, ny = view_y_;
        if (Fl::event_state(FL_CTRL)) {
            if (wx < 0) nx = 0;
            if (wx > 0) nx = content_w_;
            if (wy < 0) ny = 0;
            if (wy > 0) ny = content_h_;
        } else if (Fl::event_state(FL_SHIFT)) {
            nx += wx * mwidth();
            ny += wy * mheight();
        } else {
            nx += wx * 30;
            ny += wy * 30;
        }
        nx = std::max(0, std::min(nx, content_w_ - mwidth()));
        ny = std::max(0, std::min(ny, content_h_ - mheight()));
        scroll_to(nx, ny);
        return 1;
    }
    case FL_KEYBOARD: {
        int k = Fl::event_key();
        if (k == FL_Enter || k == FL_KP_Enter) {
            for (auto *e : selection()) if (on_open) on_open(*e);
            return 1;
        }
        if (k == 'a' && Fl::event_state(FL_META | FL_ALT)) { select_all(); return 1; }
        if (k == FL_Left || k == FL_Right || k == FL_Up || k == FL_Down) { move_focus(k, Fl::event_state(FL_SHIFT)); return 1; }
        if (k == FL_Page_Up || k == FL_Page_Down) {
            // a view's height up or down (RAWKEY_PAGEUP / PAGEDOWN)
            if (content_h_ > mheight()) {
                int ny = view_y_ + (k == FL_Page_Up ? -mheight() : mheight());
                scroll_to(view_x_, std::clamp(ny, 0, content_h_ - mheight()));
            }
            return 1;
        }
        if (k == FL_Home || k == FL_End) {
            // the focus to the first / last visible entry (RAWKEY_HOME / END)
            IconEntry *t = nullptr;
            for (auto &e : entries_) {
                e.focus = false;
                if (e.visible && (k == FL_End || !t)) t = &e;
            }
            if (t) t->focus = true;
            redraw();
            return 1;
        }
        if (k == ' ') {
            for (auto &e : entries_) if (e.focus) { e.selected = !e.selected; break; }
            redraw();
            if (on_selection_changed) on_selection_changed();
            return 1;
        }
        return 0;
    }
    }
    return Area::handle(ev);
}

// ============================================================================================
// IconDrawerList (icondrawerlist.c): the entries of a directory
// ============================================================================================
IconDrawerList::IconDrawerList(const std::string &dir) : dir_(dir) {}

void IconDrawerList::directory(const std::string &d) {
    dir_ = d;   // read by update()
}

void IconDrawerList::update() {
    clear();
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir_, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::string name = it->path().filename().string();
        bool hidden = !name.empty() && name[0] == '.';
        if (hidden && !(display_flags & Disp_ShowHidden)) continue;
        IconEntry e;
        e.path = it->path().string();
        e.label = name;
        struct stat st;
        if (lstat(e.path.c_str(), &st) == 0) {
            e.mtime = st.st_mtime;
            e.size = st.st_size;
            e.mode = st.st_mode;
            char cbuf[512];
            ssize_t cn = lgetxattr(e.path.c_str(), "user.comment", cbuf, sizeof cbuf);
            if (cn > 0) e.comment.assign(cbuf, cn);
            if (S_ISLNK(st.st_mode)) {
                struct stat t;
                bool dir = stat(e.path.c_str(), &t) == 0 && S_ISDIR(t.st_mode);
                e.type = dir ? ST_LINKDIR : ST_LINKFILE;
            } else if (S_ISDIR(st.st_mode)) e.type = ST_USERDIR;
            else if (S_ISFIFO(st.st_mode)) e.type = ST_PIPEFILE;
            else e.type = ST_FILE;
            e.executable = S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR);
        }
        add(std::move(e));
    }
    sort();
    rethink();
}

// ============================================================================================
// IconVolumeList (iconvolumelist.c): the mounted volumes
// ============================================================================================
IconVolumeList::IconVolumeList() {
    display_flags = Disp_ModeDefault | Disp_Vertical;   // the desktop: icons in columns
    list_mode = ListMode_Rough;
}

static std::string unescape_mount(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 3 < s.size()) {
            o += (char)std::stoi(s.substr(i + 1, 3), nullptr, 8);
            i += 3;
        } else o += s[i];
    }
    return o;
}

void IconVolumeList::update() {
    clear();
    auto hidden = hidden_volumes();
    auto is_hidden = [&](const std::string &path, const std::string &name, const std::string &dev) {
        for (auto &h : hidden)
            if (!strcasecmp(h.c_str(), name.c_str()) || h == path || (!dev.empty() && h == dev)) return true;
        return false;
    };
    auto add_vol = [&](const std::string &path, const std::string &name, const std::string &dev = "") {
        if (is_hidden(path, name, dev)) return false;
        IconEntry e;
        e.path = path;
        e.label = name + ":";   // volumes end with a colon (trimmed on the label)
        e.type = ST_ROOT;
        e.device = dev;
        add(std::move(e));
        return true;
    };
    // the system volume and the home volume, then the mounted devices
    add_vol("/", "System");
    // the Ram Disk: always there, in memory, empty after a restart of the computer
    {
        std::string ram = ramdisk_dir();
        if (!ram.empty()) add_vol(ram, "Ram Disk");
    }
    if (const char *h = getenv("HOME")) add_vol(h, "Home");
    std::ifstream m("/proc/self/mounts");
    std::string line;
    std::vector<std::string> mounted_devs;
    while (std::getline(m, line)) {
        std::istringstream ls(line);
        std::string dev, mnt, type;
        ls >> dev >> mnt >> type;
        mnt = unescape_mount(mnt);
        if (dev.rfind("/dev/", 0) == 0) mounted_devs.push_back(dev);
        if (dev.rfind("/dev/", 0) != 0 || mnt == "/" || mnt.rfind("/boot", 0) == 0) continue;
        if (mnt.rfind("/media/", 0) != 0 && mnt.rfind("/mnt/", 0) != 0 && mnt.rfind("/run/media/", 0) != 0) continue;
        add_vol(mnt, fs::path(mnt).filename().string(), dev);
    }
    // removable devices not mounted yet: ghosted volumes; opening one mounts it (as a disk
    // put into a drive shows its icon)
    for (auto &v : removable_volumes()) {
        if (std::find(mounted_devs.begin(), mounted_devs.end(), v.device) != mounted_devs.end()) continue;
        if (is_hidden(v.device, v.label.empty() ? fs::path(v.device).filename().string() : v.label, v.device)) continue;
        IconEntry e;
        e.path = v.device;
        e.device = v.device;
        e.label = (v.label.empty() ? fs::path(v.device).filename().string() : v.label) + ":";
        e.type = ST_ROOT;
        e.unmounted = true;
        add(std::move(e));
    }
    // the trash can
    {
        std::error_code ec;
        fs::create_directories(trash_dir(), ec);
        IconEntry e;
        e.path = trash_dir();
        e.label = "Trash";
        e.type = ST_USERDIR;
        e.trash = true;
        add(std::move(e));
    }
    // the icons left out
    for (auto &p : left_out_paths()) {
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        IconEntry e;
        e.path = p;
        e.label = fs::path(p).filename().string();
        e.type = S_ISDIR(st.st_mode) ? ST_USERDIR : ST_FILE;
        e.size = st.st_size;
        e.mtime = st.st_mtime;
        e.executable = S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR);
        e.left_out = true;
        add(std::move(e));
    }
    // Network Access (the network browser icon, after the special drawers)
    if (show_network) {
        IconEntry e;
        e.path = NETWORK_BROWSER;
        e.label = zune::tr("Network Access..");
        e.type = ST_USERDIR;
        e.network = true;
        add(std::move(e));
    }
    // the programs' AppIcons (AddAppIcon): _WANDERER_APPICON on their windows
    for (auto &a : app_items("_WANDERER_APPICON")) {
        IconEntry e;
        e.path = "appicon:" + a.label;   // stable, for the snapshot's positions
        e.label = a.label;
        e.type = ST_FILE;
        e.executable = true;
        e.app_window = a.window;
        e.app_id = a.id;
        add(std::move(e));
    }
    rethink();
}

const char *const NETWORK_BROWSER = "?wanderer.networkbrowse?";

bool is_network_fs(const std::string &type) {
    static const char *net[] = {"nfs", "nfs4", "cifs", "smb3", "smbfs", "ncpfs", "afs", "9p", "ceph", "glusterfs",
                                "davfs", "fuse.sshfs", "sshfs", "fuse.rclone", "fuse.davfs2", "fuse.curlftpfs",
                                "fuse.s3fs", "fuse.smbnetfs", "fuse.gvfsd-fuse"};
    for (auto *n : net) if (type == n) return true;
    return false;
}

void IconNetworkList::update() {
    clear();
    std::ifstream m("/proc/self/mounts");
    std::string line;
    while (std::getline(m, line)) {
        std::istringstream ls(line);
        std::string dev, mnt, type;
        ls >> dev >> mnt >> type;
        if (!is_network_fs(type)) continue;
        mnt = unescape_mount(mnt);
        IconEntry e;
        e.path = mnt;
        e.label = fs::path(mnt).filename().string() + ":";
        e.type = ST_ROOT;
        e.device = dev;          // server:/share, //server/share, user@host:
        e.network_volume = true;
        add(std::move(e));
    }
    rethink();
}

std::vector<AppItem> app_items(const char *property) {
    std::vector<AppItem> items;
    Display *d = fl_display;
    if (!d) return items;
    Atom clients = XInternAtom(d, "_NET_CLIENT_LIST", False), prop = XInternAtom(d, property, False);
    Atom type;
    int format;
    unsigned long n = 0, after;
    unsigned char *data = nullptr;
    if (XGetWindowProperty(d, RootWindow(d, fl_screen), clients, 0, 4096, False, XA_WINDOW, &type, &format, &n,
                           &after, &data) != Success || !data)
        return items;
    std::vector<::Window> wins((::Window *)data, (::Window *)data + n);
    XFree(data);
    for (::Window w : wins) {
        unsigned char *p = nullptr;
        unsigned long len = 0;
        if (XGetWindowProperty(d, w, prop, 0, 16384, False, AnyPropertyType, &type, &format, &len, &after, &p) !=
                Success || !p)
            continue;
        if (format == 8) {
            std::string all((char *)p, len);
            long id = 0;
            size_t at = 0;
            while (at < all.size()) {
                size_t z = all.find('\0', at);
                if (z == std::string::npos) z = all.size();
                std::string label = all.substr(at, z - at);
                if (!label.empty()) items.push_back({w, id, label});
                id++;
                at = z + 1;
            }
        }
        XFree(p);
    }
    return items;
}

void app_message(const AppItem &item, const char *message, const char *args_property,
                 const std::vector<std::string> &paths) {
    Display *d = fl_display;
    std::string args;
    for (auto &p : paths) { args += p; args += '\0'; }
    XChangeProperty(d, item.window, XInternAtom(d, args_property, False), XInternAtom(d, "UTF8_STRING", False), 8,
                    PropModeReplace, (const unsigned char *)args.data(), (int)args.size());
    XEvent e = {};
    e.xclient.type = ClientMessage;
    e.xclient.window = item.window;
    e.xclient.message_type = XInternAtom(d, message, False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = item.id;
    e.xclient.data.l[1] = (long)paths.size();
    XSendEvent(d, item.window, False, NoEventMask, &e);
    XFlush(d);
}

// ============================================================================================
// Removable devices (lsblk): partitions with a file system on a removable or hot-plugged disk
// ============================================================================================
std::vector<BlockVolume> removable_volumes() {
    std::vector<BlockVolume> v;
    FILE *p = popen("lsblk -P -p -o NAME,LABEL,FSTYPE,MOUNTPOINT,RM,HOTPLUG,TYPE 2>/dev/null", "r");
    if (!p) return v;
    char buf[1024];
    while (fgets(buf, sizeof buf, p)) {
        std::map<std::string, std::string> f;
        std::string l(buf);
        size_t i = 0;
        while (i < l.size()) {
            size_t eq = l.find("=\"", i);
            if (eq == std::string::npos) break;
            std::string key = l.substr(i, eq - i);
            while (!key.empty() && key[0] == ' ') key.erase(0, 1);
            size_t end = l.find('"', eq + 2);
            if (end == std::string::npos) break;
            f[key] = l.substr(eq + 2, end - eq - 2);
            i = end + 1;
        }
        if (f["FSTYPE"].empty() || f["FSTYPE"] == "swap" || (f["TYPE"] != "part" && f["TYPE"] != "disk")) continue;
        if (f["RM"] != "1" && f["HOTPLUG"] != "1") continue;
        v.push_back({f["NAME"], f["LABEL"], f["MOUNTPOINT"]});
    }
    pclose(p);
    return v;
}

// ============================================================================================
// The trash can and the icons left out
// ============================================================================================
static std::string config_path(const char *name) {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/wanderer/" + name;
}

// The Ram Disk: a drawer of the user's own on the memory file system (/dev/shm, tmpfs) - it
// takes only the memory its files need and is emptied when the computer restarts, as the
// system's RAM: volume; made at the first use, readable only by its owner
std::string ramdisk_dir() {
    static std::string dir;
    if (!dir.empty()) return dir;
    const char *base = "/dev/shm";
    struct stat st;
    if (stat(base, &st) != 0 || !S_ISDIR(st.st_mode)) {
        const char *rt = getenv("XDG_RUNTIME_DIR");   // elsewhere: the user's runtime directory
        if (!rt || !*rt) return "";
        base = rt;
    }
    const char *user = getenv("USER");
    std::string d = std::string(base) + "/ramdisk-" + (user && *user ? user : std::to_string(getuid()));
    mkdir(d.c_str(), 0700);
    if (stat(d.c_str(), &st) != 0 || st.st_uid != getuid()) return "";   // someone else's: not used
    dir = d;
    return dir;
}

// --------------------------------------------------------------------------------------------
// Volume paths: "Home:Dokumenty/list.txt", "Ram Disk:", "System:etc" - a volume's name, a
// colon, the path inside it (as the system names its files); both ways
// --------------------------------------------------------------------------------------------
static std::vector<std::pair<std::string, std::string>> volume_names() {
    std::vector<std::pair<std::string, std::string>> v;   // name, mount point (most specific first)
    std::ifstream m("/proc/self/mounts");
    std::string line;
    while (std::getline(m, line)) {
        std::istringstream ls(line);
        std::string dev, mnt, type;
        ls >> dev >> mnt >> type;
        mnt = unescape_mount(mnt);
        if (is_network_fs(type)) { v.push_back({fs::path(mnt).filename().string(), mnt}); continue; }   // Network Access
        if (dev.rfind("/dev/", 0) != 0) continue;
        if (mnt.rfind("/media/", 0) != 0 && mnt.rfind("/mnt/", 0) != 0 && mnt.rfind("/run/media/", 0) != 0) continue;
        v.push_back({fs::path(mnt).filename().string(), mnt});
    }
    if (!ramdisk_dir().empty()) v.push_back({"Ram Disk", ramdisk_dir()});
    v.push_back({"Trash", trash_dir()});
    if (const char *h = getenv("HOME")) v.push_back({"Home", h});
    v.push_back({"System", "/"});
    return v;
}

std::string to_volume_path(const std::string &path) {
    std::string best_name, best_mnt;
    for (auto &[name, mnt] : volume_names()) {
        bool inside = path == mnt || mnt == "/" || path.rfind(mnt + "/", 0) == 0;
        if (inside && mnt.size() > best_mnt.size()) { best_name = name; best_mnt = mnt; }
        if (inside && best_mnt.empty()) { best_name = name; best_mnt = mnt; }
    }
    if (best_name.empty()) return path;
    std::string rest = path.size() > best_mnt.size() ? path.substr(best_mnt == "/" ? 1 : best_mnt.size() + 1) : "";
    return best_name + ":" + rest;
}

// Assigns: names of the user's own for drawers (~/.config/wanderer/assigns, one per line:
// "Docs = ~/Dokumenty"), as the system's logical device names - usable in every volume
// path ("Docs:Listy")
std::vector<std::pair<std::string, std::string>> assigns() {
    std::vector<std::pair<std::string, std::string>> v;
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    std::ifstream in(base + "/wanderer/assigns");
    std::string line;
    auto trim = [](std::string t) {
        while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
        while (!t.empty() && isspace((unsigned char)t[0])) t.erase(0, 1);
        return t;
    };
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = trim(line.substr(0, eq)), path = trim(line.substr(eq + 1));
        if (!name.empty() && name.back() == ':') name.pop_back();
        if (!path.empty() && path[0] == '~' && getenv("HOME")) path = getenv("HOME") + path.substr(1);
        while (path.size() > 1 && path.back() == '/') path.pop_back();
        if (!name.empty() && !path.empty()) v.push_back({name, path});
    }
    return v;
}

std::string from_volume_path(const std::string &p) {
    if (p.empty() || p[0] == '/' || p[0] == '~') {
        if (!p.empty() && p[0] == '~' && getenv("HOME")) return getenv("HOME") + p.substr(1);
        return p;
    }
    auto colon = p.find(':');
    if (colon == std::string::npos || p.find('/') < colon) return p;
    std::string vol = p.substr(0, colon), rest = p.substr(colon + 1);
    auto eq = [](std::string a, std::string b) { return strcasecmp(a.c_str(), b.c_str()) == 0; };
    std::string mnt;
    if (eq(vol, "RAM")) mnt = ramdisk_dir();
    else if (eq(vol, "SYS")) mnt = "/";
    else for (auto &[name, m] : volume_names()) if (eq(vol, name)) { mnt = m; break; }
    if (mnt.empty()) for (auto &[name, m] : assigns()) if (eq(vol, name)) { mnt = m; break; }
    if (mnt.empty()) return p;
    if (rest.empty()) return mnt;
    return (mnt == "/" ? "" : mnt) + "/" + rest;
}

std::string trash_dir() {
    const char *x = getenv("XDG_DATA_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.local/share";
    return base + "/Trash/files";
}

std::vector<std::string> left_out_paths() {
    std::vector<std::string> v;
    std::ifstream in(config_path("leftout"));
    std::string line;
    while (std::getline(in, line)) if (!line.empty()) v.push_back(line);
    return v;
}

void set_left_out(const std::vector<std::string> &paths) {
    std::error_code ec;
    fs::create_directories(fs::path(config_path("leftout")).parent_path(), ec);
    std::ofstream out(config_path("leftout"));
    for (auto &p : paths) out << p << "\n";
}

std::vector<std::string> hidden_volumes() {
    std::vector<std::string> v;
    std::ifstream in(config_path("hiddenvolumes"));
    std::string line;
    while (std::getline(in, line)) {
        // surrounding blanks and a trailing colon tolerated (VolumeIsHidden)
        while (!line.empty() && (line.back() == ':' || isspace((unsigned char)line.back()))) line.pop_back();
        size_t b = line.find_first_not_of(" \t");
        if (b != std::string::npos) v.push_back(line.substr(b));
    }
    return v;
}

void set_hidden_volumes(const std::vector<std::string> &names) {
    std::error_code ec;
    fs::create_directories(fs::path(config_path("hiddenvolumes")).parent_path(), ec);
    std::ofstream out(config_path("hiddenvolumes"));
    for (auto &n : names) out << n << "\n";
}

// ============================================================================================
// IconSearchList: Find - the names matching a pattern (wildcards * ?, no case) under a
// directory; the interface stays alive while it searches
// ============================================================================================
// Does a regular file (up to 64 MB) contain the text? (no case; read in pieces)
static bool file_contains(const std::string &path, const std::string &text) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > (64LL << 20)) return false;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string want;
    for (char c : text) want += (char)tolower((unsigned char)c);
    std::string buf(65536 + want.size(), '\0');
    size_t keep = 0;
    bool found = false;
    for (;;) {
        size_t n = fread(&buf[keep], 1, 65536, f);
        if (n == 0) break;
        size_t len = keep + n;
        for (size_t i = keep; i < len; i++) buf[i] = (char)tolower((unsigned char)buf[i]);
        if (std::string_view(buf.data(), len).find(want) != std::string_view::npos) { found = true; break; }
        keep = std::min(len, want.size() - 1);
        memmove(&buf[0], &buf[len - keep], keep);
    }
    fclose(f);
    return found;
}

void IconSearchList::update() {
    clear();
    std::error_code ec;
    int seen = 0;
    auto it = fs::recursive_directory_iterator(dir_, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (++seen % 300 == 0) Fl::check();
        std::string name = it->path().filename().string();
        if (!(display_flags & Disp_ShowHidden) && !name.empty() && name[0] == '.') {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (fnmatch(pattern_.c_str(), name.c_str(), FNM_CASEFOLD) != 0) continue;
        if (!contents_.empty() && !file_contains(it->path().string(), contents_)) continue;
        IconEntry e;
        e.path = it->path().string();
        e.label = name;
        struct stat st;
        if (lstat(e.path.c_str(), &st) == 0) {
            e.mtime = st.st_mtime;
            e.size = st.st_size;
            e.type = S_ISDIR(st.st_mode) ? ST_USERDIR : ST_FILE;
            e.executable = S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR);
        }
        add(std::move(e));
        if (entries_.size() >= 2000) break;   // enough to show
    }
    sort();
    rethink();
}

}  // namespace wanderer
