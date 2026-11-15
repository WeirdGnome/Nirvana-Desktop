/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    ScreenGrabber: see screengrabber.h.
*/
#include "screengrabber.h"
#include "lune.h"
#include "catalog.h"
#include "tools.h"

#include <FL/Fl.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <memory>
#include <vector>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

namespace {
struct Shot { int w = 0, h = 0; std::vector<unsigned char> rgb; };

// the frame round a program's window: its ancestor that is a child of the root window
::Window top_level(::Window w) {
    ::Window root_ret, parent, *kids = nullptr;
    unsigned n;
    ::Window root = RootWindow(fl_display, fl_screen);
    while (w && w != root) {
        if (!XQueryTree(fl_display, w, &root_ret, &parent, &kids, &n)) return 0;
        if (kids) XFree(kids);
        if (parent == root) return w;
        w = parent;
    }
    return 0;
}

::Window active_window() {
    Atom prop = XInternAtom(fl_display, "_NET_ACTIVE_WINDOW", False), type;
    int fmt;
    unsigned long n, after;
    unsigned char *d = nullptr;
    ::Window w = 0;
    if (XGetWindowProperty(fl_display, RootWindow(fl_display, fl_screen), prop, 0, 1, False, XA_WINDOW, &type, &fmt, &n, &after, &d) == Success && d) {
        if (n) w = *(::Window *)d;
        XFree(d);
    }
    return w;
}

// the screen's pixels in a rectangle (of the root window, so what is on top shows)
bool grab(int x, int y, int w, int h, Shot &s) {
    ::Window root = RootWindow(fl_display, fl_screen);
    int SW = DisplayWidth(fl_display, fl_screen), SH = DisplayHeight(fl_display, fl_screen);
    int x2 = std::min(SW, x + w), y2 = std::min(SH, y + h);
    x = std::max(0, x); y = std::max(0, y);
    w = x2 - x; h = y2 - y;
    if (w <= 0 || h <= 0) return false;
    XImage *im = XGetImage(fl_display, root, x, y, (unsigned)w, (unsigned)h, AllPlanes, ZPixmap);
    if (!im) return false;
    s.w = w; s.h = h;
    s.rgb.resize((size_t)w * h * 3);
    unsigned long rm = im->red_mask, gm = im->green_mask, bm = im->blue_mask;
    auto shift = [](unsigned long m) { int n = 0; while (m && !(m & 1)) { m >>= 1; n++; } return n; };
    auto bits = [](unsigned long m) { int n = 0; while (m) { n += m & 1; m >>= 1; } return n; };
    int rs = shift(rm), gs = shift(gm), bs = shift(bm), rb = bits(rm), gb = bits(gm), bb = bits(bm);
    auto scale = [](unsigned long v, int nbits) { return (unsigned char)(nbits >= 8 ? v >> (nbits - 8) : v * 255 / ((1u << nbits) - 1)); };
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            unsigned long p = XGetPixel(im, i, j);
            unsigned char *o = &s.rgb[((size_t)j * w + i) * 3];
            o[0] = scale((p & rm) >> rs, rb);
            o[1] = scale((p & gm) >> gs, gb);
            o[2] = scale((p & bm) >> bs, bb);
        }
    XDestroyImage(im);
    return true;
}

// the picture, small (its proportions kept)
class Preview : public Area {
public:
    Preview() : Area(Frame_ReadList) { background(&Prefs::readlist_back); }
    ~Preview() override { delete img_; delete small_; }
    void shot(const Shot &s) {
        delete img_; delete small_;
        small_ = nullptr;
        data_ = s.rgb;
        img_ = new Fl_RGB_Image(data_.data(), s.w, s.h, 3);
        redraw();
    }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override {
        minw = 160; minh = 100; defw = 320; defh = 200; maxw = maxh = 10000;
    }

protected:
    void draw_contents() override {
        if (!img_) {
            fl_color(pens()[MPEN_HALFSHADOW]);
            fl_font(prefs().font, prefs().font_size);
            fl_draw(tr("No picture yet").c_str(), mleft(), mtop(), mwidth(), mheight(), FL_ALIGN_CENTER);
            return;
        }
        double sc = std::min(1.0, std::min((double)mwidth() / img_->w(), (double)mheight() / img_->h()));
        int W = std::max(1, (int)(img_->w() * sc)), H = std::max(1, (int)(img_->h() * sc));
        if (!small_ || small_->w() != W || small_->h() != H) { delete small_; small_ = img_->copy(W, H); }
        small_->draw(mleft() + (mwidth() - W) / 2, mtop() + (mheight() - H) / 2);
    }
    std::vector<unsigned char> data_;
    Fl_RGB_Image *img_ = nullptr;
    Fl_Image *small_ = nullptr;
};

std::string default_name() {
    std::string home = getenv("HOME") ? getenv("HOME") : "/tmp";
    std::string dir = home;
    // the pictures' drawer (xdg-user-dirs), else Pictures, else home
    if (FILE *p = popen("xdg-user-dir PICTURES 2>/dev/null", "r")) {
        char b[1024] = "";
        if (fgets(b, sizeof b, p)) {
            std::string d = b;
            while (!d.empty() && d.back() == '\n') d.pop_back();
            std::error_code ec;
            if (!d.empty() && d != home && fs::is_directory(d, ec)) dir = d;
        }
        pclose(p);
    }
    if (dir == home && fs::is_directory(home + "/Pictures")) dir = home + "/Pictures";
    char t[64];
    time_t now = time(nullptr);
    strftime(t, sizeof t, "Screenshot-%Y%m%d-%H%M%S.png", localtime(&now));
    return dir + "/" + t;
}
}  // namespace

void run_screengrabber() {
    auto shot = std::make_shared<Shot>();
    auto *root = VGroup();
    auto *g = new Group(Group::Vertical, Frame_Group);
    g->columns(2);
    new lune::Text("\033rGrab:");
    auto *what = new Cycle({"Whole screen", "Active window"}, 0);
    new lune::Text("\033rDelay:");
    auto *delay = new Slider(0, 30, 2);
    delay->format = [](int v) { return std::to_string(v) + " s"; };
    new lune::Text("\033rHide this window:");
    auto *hide = new Checkmark(true);
    g->end();
    auto *preview = new Preview();
    auto *frow = HGroup();
    new lune::Text("\033rFile:");
    auto *file = new String(default_name());
    auto *pop = PopButton(Pop_File);
    frow->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *grabb = new Button("Grab");
    HSpace();
    auto *save = new Button("Save");
    HSpace();
    auto *quit = new Button("Quit");
    btns->end();
    root->end();
    auto *w = new lune::Window("ScreenGrabber", root);
    w->id("SCREENGRABBER");
    save->disabled(true);

    // the grab, once the delay has passed (and the window has gone, when it is hidden)
    struct Pending { std::function<void()> fn; };
    auto pending = std::make_shared<Pending>();
    pending->fn = [=] {
        bool ok = false;
        if (what->active() == 1) {
            ::Window a = active_window(), f = a ? top_level(a) : 0;
            XWindowAttributes wa;
            if (f && XGetWindowAttributes(fl_display, f, &wa))
                ok = grab(wa.x, wa.y, wa.width + 2 * wa.border_width, wa.height + 2 * wa.border_width, *shot);
        }
        if (!ok) ok = grab(0, 0, DisplayWidth(fl_display, fl_screen), DisplayHeight(fl_display, fl_screen), *shot);
        if (!w->shown()) w->show();
        if (ok) {
            preview->shot(*shot);
            save->disabled(false);
            file->value(default_name());
            root->redraw();
        } else request("ScreenGrabber", tr("The screen could not be read."), "OK");
    };
    grabb->on_press = [=] {
        if (hide->on()) w->hide();
        XSync(fl_display, False);
        // (a moment at least, for the window to be gone from the screen; waited for here, as
        // the program would end with no window shown)
        double left = std::max(0.3, (double)delay->value());
        Fl_Timestamp t0 = Fl::now();
        while (Fl::seconds_since(t0) < left) Fl::wait(std::max(0.01, left - Fl::seconds_since(t0)));
        pending->fn();
    };
    pop->on_press = [=] {
        std::string p = file->value();
        if (file_request(tr("Save As"), p)) file->value(p);
    };
    save->on_press = [=] {
        std::string p = file->value();
        if (p.empty() || shot->rgb.empty()) return;
        if (p.size() < 4 || (p.substr(p.size() - 4) != ".png" && p.substr(p.size() - 4) != ".PNG")) p += ".png";
        if (fs::exists(p) && request("ScreenGrabber", tr("The file exists already:") + "\n" + p + "\n" + tr("Replace it?"), "Replace|Cancel") != 1)
            return;
        std::error_code ec;
        fs::create_directories(fs::path(p).parent_path(), ec);
        if (fl_write_png(p.c_str(), shot->rgb.data(), shot->w, shot->h, 3) != 0)
            request("ScreenGrabber", tr("Cannot save the file:") + "\n" + p, "OK");
        else {
            file->value(p);
            save->disabled(true);   // (saved: the next grab makes another)
            root->redraw();
        }
    };
    quit->on_press = [] { exit(0); };
    w->menustrip({
        {"Project", 0, 0, 0, 0, {
            {"Grab", 1, 0, 'g'},
            {"Save", 2, Menu_Line, 's'},
            {"Quit", 3, 0, 'q'},
        }},
    });
    w->on_menu = [=](int id) {
        if (id == 1) grabb->on_press();
        else if (id == 2 && !save->disabled()) save->on_press();
        else if (id == 3) exit(0);
    };
    w->callback([](Fl_Widget *, void *) { exit(0); });
    w->size(std::max(w->w(), 420), std::max(w->h(), 380));
    w->show();
}

}  // namespace rambler
