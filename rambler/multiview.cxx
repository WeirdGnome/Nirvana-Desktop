/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    MultiView: see multiview.h.
*/
#include "multiview.h"
#include "lune.h"
#include "catalog.h"
#include "tools.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_SVG_Image.H>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

namespace {
enum { M_OPEN = 1, M_SAVEAS, M_PRINT, M_ABOUT, M_QUIT, M_COPY, M_SELALL, M_CLEARSEL, M_FIND, M_FINDNEXT, M_WRAP, M_FIT };

std::string read_all(const std::string &f, size_t max = 64u << 20) {
    std::ifstream in(f, std::ios::binary);
    std::string s;
    char buf[65536];
    while (in && s.size() < max) {
        in.read(buf, sizeof buf);
        s.append(buf, (size_t)in.gcount());
    }
    return s;
}

// a text: no NUL bytes and few control characters in its start
bool looks_text(const std::string &head) {
    if (head.empty()) return true;
    size_t odd = 0;
    for (unsigned char c : head) {
        if (c == 0) return false;
        if (c < 32 && c != '\n' && c != '\r' && c != '\t' && c != '\f' && c != 27) odd++;
    }
    return odd * 50 < head.size();
}

Fl_Shared_Image *load_picture(const std::string &f) {
    static bool registered = false;
    if (!registered) { fl_register_images(); registered = true; }
    Fl_Shared_Image *si = Fl_Shared_Image::get(f.c_str());
    if (si && (si->w() <= 0 || si->h() <= 0 || si->fail())) { si->release(); si = nullptr; }
    return si;
}

std::string hex_dump(const std::string &d) {
    std::string out;
    char b[96];
    for (size_t i = 0; i < d.size(); i += 16) {
        int n = snprintf(b, sizeof b, "%08zx ", i);
        std::string asc;
        for (size_t j = 0; j < 16; j++) {
            if (i + j < d.size()) {
                unsigned char c = (unsigned char)d[i + j];
                n += snprintf(b + n, sizeof b - n, "%s%02x", j == 8 ? "  " : " ", c);
                asc += c >= 32 && c < 127 ? (char)c : '.';
            } else n += snprintf(b + n, sizeof b - n, "%s  ", j == 8 ? "    " : " ");
        }
        out += std::string(b) + "  " + asc + "\n";
        if (out.size() > (32u << 20)) break;
    }
    return out;
}

// The view (the datatype object in the window): a text's lines or a picture
class View : public Area {
public:
    enum Mode { None, Text, Picture };
    View() : Area(Frame_ReadList) { background(&Prefs::readlist_back); }
    ~View() override { drop_picture(); }

    Mode mode = None;
    std::vector<std::string> lines;        // the text's lines (tabs expanded)
    bool wrap = false, fit = true;
    int top = 0, left = 0;                 // the first display line, the left pixel
    int sel_a = -1, sel_b = -1;            // the lines marked (logical lines)
    std::function<void()> on_scroll;       // the scrollers set again

    void set_text(const std::string &t) {
        drop_picture();
        mode = Text;
        lines.clear();
        std::string line;
        for (size_t i = 0; i <= t.size(); i++) {
            char c = i < t.size() ? t[i] : '\n';
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
                line.clear();
            } else if (c == '\t') {
                line.append(8 - line.size() % 8, ' ');
            } else line += c;
        }
        if (lines.size() > 1 && lines.back().empty() && !t.empty() && t.back() == '\n') lines.pop_back();
        top = left = 0;
        sel_a = sel_b = -1;
        relayout_text();
    }
    void set_picture(Fl_Shared_Image *img) {
        drop_picture();
        mode = Picture;
        image_ = img;
        lines.clear();
        top = left = 0;
        redraw();
        if (on_scroll) on_scroll();
    }

    // the display lines: the logical lines cut at the window's width when wrapping
    struct DLine { int line; size_t from, len; };
    std::vector<DLine> dlines;
    int char_w() const { fl_font(prefs().font_fixed, fsize()); return std::max(1, (int)fl_width("M")); }
    int line_h() const { return fsize() + 2; }
    int fsize() const { return prefs().fixed_font_size > 0 ? prefs().fixed_font_size : prefs().font_size; }
    int rows() const { return std::max(1, mheight() / line_h()); }
    int cols() const { return std::max(1, (mwidth() - 4) / char_w()); }
    int longest() const {
        size_t m = 0;
        for (auto &l : lines) m = std::max(m, l.size());
        return (int)m;
    }
    void relayout_text() {
        dlines.clear();
        int c = cols();
        for (int i = 0; i < (int)lines.size(); i++) {
            const std::string &l = lines[i];
            if (!wrap || (int)l.size() <= c) { dlines.push_back({i, 0, l.size()}); continue; }
            // (cut at a space when there is one in the line's last third)
            size_t at = 0;
            while (at < l.size()) {
                size_t len = std::min((size_t)c, l.size() - at);
                if (at + len < l.size()) {
                    size_t sp = l.rfind(' ', at + len);
                    if (sp != std::string::npos && sp > at + len * 2 / 3) len = sp - at + 1;
                }
                dlines.push_back({i, at, len});
                at += len;
            }
        }
        if (wrap) left = 0;
        clamp();
        redraw();
        if (on_scroll) on_scroll();
    }
    void clamp() {
        if (mode == Text) {
            top = std::clamp(top, 0, std::max(0, (int)dlines.size() - rows()));
            int w = longest() * char_w() + 8;
            left = std::clamp(left, 0, std::max(0, w - mwidth()));
        } else if (mode == Picture && image_ && !fit) {
            top = std::clamp(top, 0, std::max(0, image_->h() - mheight()));
            left = std::clamp(left, 0, std::max(0, image_->w() - mwidth()));
        } else top = left = 0;
    }
    void scroll_to(int t, int l) { top = t; left = l; clamp(); redraw(); if (on_scroll) on_scroll(); }
    // the scrollers' extents: entries, visible, first (vertical, then horizontal)
    void extents(int &ve, int &vv, int &vf, int &he, int &hv, int &hf) {
        ve = vv = he = hv = 1; vf = hf = 0;
        if (mode == Text) {
            ve = std::max(1, (int)dlines.size()); vv = rows(); vf = top;
            he = std::max(1, longest() * char_w() + 8); hv = std::max(1, mwidth()); hf = left;
        } else if (mode == Picture && image_ && !fit) {
            ve = image_->h(); vv = std::max(1, mheight()); vf = top;
            he = image_->w(); hv = std::max(1, mwidth()); hf = left;
        }
    }
    // the display line of a logical line (for Find)
    int dline_of(int line) const {
        for (size_t i = 0; i < dlines.size(); i++) if (dlines[i].line == line) return (int)i;
        return 0;
    }
    std::string selected_text() const {
        if (mode != Text || sel_a < 0) return "";
        int a = std::min(sel_a, sel_b), b = std::max(sel_a, sel_b);
        std::string s;
        for (int i = a; i <= b && i < (int)lines.size(); i++) s += lines[i] + "\n";
        return s;
    }

    void resize(int X, int Y, int W, int H) override {
        Area::resize(X, Y, W, H);
        delete scaled_;
        scaled_ = nullptr;
        if (mode == Text) relayout_text();
        else { clamp(); if (on_scroll) on_scroll(); }
    }
    int handle(int e) override {
        switch (e) {
        case FL_PUSH:
            take_focus();
            drag_x_ = Fl::event_x(); drag_y_ = Fl::event_y();
            drag_top_ = top; drag_left_ = left;
            if (mode == Text && Fl::event_button() == FL_LEFT_MOUSE) {
                int l = line_at(Fl::event_y());
                if (Fl::event_state(FL_SHIFT) && sel_a >= 0) sel_b = l;
                else sel_a = sel_b = l;
                redraw();
            }
            return 1;
        case FL_DRAG:
            if (mode == Picture) scroll_to(drag_top_ - (Fl::event_y() - drag_y_), drag_left_ - (Fl::event_x() - drag_x_));
            else if (mode == Text && sel_a >= 0) {
                // (beyond the edges the text moves along)
                if (Fl::event_y() < mtop()) scroll_to(top - 1, left);
                else if (Fl::event_y() > mtop() + mheight()) scroll_to(top + 1, left);
                sel_b = line_at(Fl::event_y());
                redraw();
            }
            return 1;
        case FL_RELEASE:
            if (mode == Text && sel_a >= 0) {
                std::string s = selected_text();
                Fl::copy(s.c_str(), (int)s.size(), 0);   // (the primary selection, as X has it)
            }
            return 1;
        case FL_MOUSEWHEEL:
            if (mode == Text) scroll_to(top + Fl::event_dy() * 3, left + Fl::event_dx() * char_w() * 3);
            else scroll_to(top + Fl::event_dy() * 40, left + Fl::event_dx() * 40);
            return 1;
        case FL_FOCUS: case FL_UNFOCUS: return 1;
        }
        return Area::handle(e);
    }
    bool key(int k) {
        int page = mode == Text ? std::max(1, rows() - 1) : std::max(1, mheight() - 20);
        int step = mode == Text ? 1 : 32, hstep = mode == Text ? char_w() * 4 : 32;
        switch (k) {
        case FL_Up: scroll_to(top - step, left); return true;
        case FL_Down: scroll_to(top + step, left); return true;
        case FL_Left: scroll_to(top, left - hstep); return true;
        case FL_Right: scroll_to(top, left + hstep); return true;
        case FL_Page_Up: scroll_to(top - page, left); return true;
        case FL_Page_Down: case ' ': scroll_to(top + page, left); return true;
        case FL_Home: scroll_to(0, 0); return true;
        case FL_End: scroll_to(1 << 30, 0); return true;
        }
        return false;
    }

protected:
    void draw_contents() override {
        if (mode == Text) {
            fl_font(prefs().font_fixed, fsize());
            int lh = line_h(), y = mtop(), x = mleft() + 2 - left;
            int a = std::min(sel_a, sel_b), b = std::max(sel_a, sel_b);
            for (int i = top; i < (int)dlines.size() && y < mtop() + mheight(); i++, y += lh) {
                const DLine &d = dlines[i];
                bool sel = sel_a >= 0 && d.line >= a && d.line <= b;
                if (sel) { fl_color(pens()[MPEN_FILL]); fl_rectf(mleft(), y, mwidth(), lh); }
                fl_color(pens()[sel ? MPEN_SHINE : MPEN_TEXT]);
                std::string part = lines[d.line].substr(d.from, d.len);
                fl_draw(part.c_str(), (int)part.size(), x, y + lh - fl_descent() - 1);
            }
        } else if (mode == Picture && image_) {
            Fl_Image *img = image_;
            int W = image_->w(), H = image_->h();
            if (fit && (W > mwidth() || H > mheight())) {
                // (fitted, its proportions kept)
                double s = std::min((double)mwidth() / W, (double)mheight() / H);
                int sw = std::max(1, (int)(W * s)), sh = std::max(1, (int)(H * s));
                if (!scaled_ || scaled_->w() != sw || scaled_->h() != sh) {
                    delete scaled_;
                    scaled_ = image_->copy(sw, sh);
                    if (auto *svg = dynamic_cast<Fl_SVG_Image *>(scaled_)) svg->normalize();
                }
                img = scaled_;
                W = sw; H = sh;
            }
            int x = W < mwidth() ? mleft() + (mwidth() - W) / 2 : mleft() - (fit ? 0 : left);
            int y = H < mheight() ? mtop() + (mheight() - H) / 2 : mtop() - (fit ? 0 : top);
            img->draw(x, y);
        }
    }
    int line_at(int Y) const {
        int d = std::clamp(top + (Y - mtop()) / line_h(), 0, std::max(0, (int)dlines.size() - 1));
        return dlines.empty() ? 0 : dlines[d].line;
    }
    void drop_picture() {
        delete scaled_;
        scaled_ = nullptr;
        if (image_) { image_->release(); image_ = nullptr; }
    }
    Fl_Shared_Image *image_ = nullptr;
    Fl_Image *scaled_ = nullptr;
    int drag_x_ = 0, drag_y_ = 0, drag_top_ = 0, drag_left_ = 0;
};

// the window: its keys go to the view first
std::function<bool(int)> g_keys;
class MVWindow : public lune::Window {
public:
    using lune::Window::Window;
    int handle(int e) override {
        if (e == FL_KEYBOARD && !(Fl::event_state() & (FL_META | FL_CTRL | FL_ALT)) && g_keys && g_keys(Fl::event_key())) return 1;
        return lune::Window::handle(e);
    }
};

// Find...: the text asked for
bool ask_text(const std::string &title, std::string &text) {
    auto *root = VGroup();
    auto *row = HGroup();
    new lune::Text("\033rFind:");
    auto *s = new String(text);
    row->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *ok = new Button("Find");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();
    auto *w = new lune::Window(title, root);
    int done = 0;
    ok->on_press = [&] { done = 1; };
    s->on_acknowledge = [&] { done = 1; };
    cancel->on_press = [&] { done = 2; };
    w->callback([](Fl_Widget *, void *d) { *(int *)d = 2; }, &done);
    w->size(std::max(w->w(), 320), w->h());
    w->set_modal();
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
    s->input()->take_focus();
    while (!done && w->shown()) Fl::wait();
    if (done == 1) text = s->value();
    w->hide();
    Fl::delete_widget(w);
    return done == 1 && !text.empty();
}

std::string lower(std::string s) {
    for (auto &c : s) c = (char)tolower((unsigned char)c);
    return s;
}
}  // namespace

bool multiview_can_show(const std::string &file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return false;
    std::string head = read_all(file, 4096);
    if (looks_text(head)) return true;
    Fl_Shared_Image *img = load_picture(file);
    if (img) { img->release(); return true; }
    return false;
}

void run_multiview(const std::string &start) {
    auto *root = VGroup();
    root->spacing(0);
    auto *top = HGroup();
    top->spacing(0);
    auto *view = new View();
    auto *vs = new Scrollbar(false);
    top->end();
    auto *bottom = HGroup();
    bottom->spacing(0);
    auto *hs = new Scrollbar(true);
    auto *corner = new Rectangle();
    corner->fixed_width(16);
    corner->fixed_height(16);
    bottom->end();
    root->end();
    auto *w = new MVWindow("MultiView", root);
    w->id("MULTIVIEW");
    w->resizable(root);

    auto file = std::make_shared<std::string>();
    auto find_text = std::make_shared<std::string>();
    auto find_at = std::make_shared<int>(-1);

    view->on_scroll = [=] {
        int ve, vv, vf, he, hv, hf;
        view->extents(ve, vv, vf, he, hv, hf);
        vs->set(ve, vv, vf);
        hs->set(he, hv, hf);
    };
    vs->on_change = [=](int f) { view->top = f; view->clamp(); view->redraw(); };
    hs->on_change = [=](int f) { view->left = f; view->clamp(); view->redraw(); };
    g_keys = [=](int k) { return view->key(k); };

    auto set_menus = [=] {
        bool text = view->mode == View::Text;
        w->menu_disabled(M_WRAP, !text);
        w->menu_disabled(M_FIND, !text);
        w->menu_disabled(M_FINDNEXT, !text);
        w->menu_disabled(M_COPY, view->mode == View::None);
        w->menu_disabled(M_SELALL, !text);
        w->menu_disabled(M_CLEARSEL, !text);
        w->menu_disabled(M_FIT, view->mode != View::Picture);
        w->menu_disabled(M_SAVEAS, file->empty());
        w->menu_disabled(M_PRINT, file->empty());
    };
    // a file shown: a picture when it is one, else a text, else its bytes
    auto load = [=](const std::string &f) -> bool {
        std::error_code ec;
        if (!fs::is_regular_file(f, ec)) {
            request("MultiView", tr("Cannot open the file:") + "\n" + f, "OK");
            return false;
        }
        std::string head = read_all(f, 4096);
        Fl_Shared_Image *img = looks_text(head) ? nullptr : load_picture(f);
        // (an SVG is text, and a picture)
        if (!img && f.size() > 4 && lower(f.substr(f.size() - 4)) == ".svg") img = load_picture(f);
        if (img) view->set_picture(img);
        else {
            std::string all = read_all(f);
            view->set_text(looks_text(all.substr(0, 65536)) ? all : hex_dump(all));
        }
        *file = f;
        *find_at = -1;
        w->copy_label(fs::path(f).filename().string().c_str());
        set_menus();
        return true;
    };

    w->menustrip({
        {"Project", 0, 0, 0, 0, {
            {"Open...", M_OPEN, 0, 'o'},
            {"Save As...", M_SAVEAS, 0, 's'},
            {"Print", M_PRINT, Menu_Line, 'p'},
            {"About...", M_ABOUT, Menu_Line},
            {"Quit", M_QUIT, 0, 'q'},
        }},
        {"Edit", 0, 0, 0, 0, {
            {"Copy", M_COPY, 0, 'c'},
            {"Select All", M_SELALL, 0, 'a'},
            {"Clear Selected", M_CLEARSEL},
        }},
        {"Search", 0, 0, 0, 0, {
            {"Find...", M_FIND, 0, 'f'},
            {"Find Next", M_FINDNEXT, 0, 'n'},
        }},
        {"View", 0, 0, 0, 0, {
            {"Word Wrap", M_WRAP, Menu_Toggle, 'w'},
            {"Fit to Window", M_FIT, Menu_Toggle | Menu_Checked},
        }},
    });
    auto find_next = [=] {
        if (find_text->empty() || view->mode != View::Text) return;
        std::string needle = lower(*find_text);
        int n = (int)view->lines.size();
        for (int k = 1; k <= n; k++) {
            int i = (*find_at + k) % n;
            if (lower(view->lines[i]).find(needle) != std::string::npos) {
                *find_at = i;
                view->sel_a = view->sel_b = i;
                view->scroll_to(view->dline_of(i) - view->rows() / 3, view->left);
                return;
            }
        }
        request("MultiView", tr("Not found:") + " " + *find_text, "OK");
    };
    w->on_menu = [=](int id) {
        switch (id) {
        case M_OPEN: {
            std::string p = file->empty() ? std::string(getenv("HOME") ? getenv("HOME") : "/") + "/" : *file;
            if (file_request(tr("Open"), p)) load(p);
            break;
        }
        case M_SAVEAS: {
            std::string p = *file;
            if (!file_request(tr("Save As"), p) || p == *file) break;
            std::error_code ec;
            fs::copy_file(*file, p, fs::copy_options::overwrite_existing, ec);
            if (ec) request("MultiView", tr("Cannot save the file:") + "\n" + ec.message(), "OK");
            break;
        }
        case M_PRINT: {
            // (CUPS prints texts and pictures itself)
            std::string cmd = "lp -- '";
            for (char c : *file) { if (c == '\'') cmd += "'\\''"; else cmd += c; }
            cmd += "' 2>&1";
            std::string out;
            int rc = -1;
            if (FILE *p = popen(cmd.c_str(), "r")) {
                char buf[512];
                size_t n;
                while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
                rc = pclose(p);
            }
            request("MultiView", rc == 0 ? tr("Sent to the printer.") + "\n" + out : tr("Cannot print:") + "\n" + out, "OK");
            break;
        }
        case M_ABOUT:
            request("MultiView", "MultiView\n" + tr("Shows texts and pictures.") + "\n\n" + *file, "OK");
            break;
        case M_QUIT: exit(0);
        case M_COPY: {
            std::string s = view->mode == View::Text ? view->selected_text() : std::string();
            if (view->mode == View::Text && s.empty()) {
                for (auto &l : view->lines) s += l + "\n";
            }
            if (view->mode == View::Picture) s = *file;   // (a picture: its name)
            Fl::copy(s.c_str(), (int)s.size(), 1);
            break;
        }
        case M_SELALL:
            view->sel_a = 0;
            view->sel_b = (int)view->lines.size() - 1;
            view->redraw();
            break;
        case M_CLEARSEL: view->sel_a = view->sel_b = -1; view->redraw(); break;
        case M_FIND:
            if (ask_text(tr("Find"), *find_text)) { *find_at = view->sel_a >= 0 ? view->sel_a : view->top > 0 ? view->dlines[view->top].line - 1 : -1; find_next(); }
            break;
        case M_FINDNEXT: if (find_text->empty()) w->menu_pick(M_FIND); else find_next(); break;
        case M_WRAP: view->wrap = w->menu_checked(M_WRAP); view->relayout_text(); break;
        case M_FIT:
            view->fit = w->menu_checked(M_FIT);
            view->clamp();
            view->redraw();
            view->on_scroll();
            break;
        }
    };
    w->callback([](Fl_Widget *, void *) { exit(0); });
    w->size(std::max(w->w(), 640), std::max(w->h(), 480));
    set_menus();

    std::string f = start;
    if (f.empty()) {
        // without a file: the file requester, as the original asks
        f = std::string(getenv("HOME") ? getenv("HOME") : "/") + "/";
        if (!file_request(tr("Open"), f)) exit(0);
    }
    w->show();
    if (!load(f)) { if (start.empty()) exit(0); }
}

}  // namespace rambler
