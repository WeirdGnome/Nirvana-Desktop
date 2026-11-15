/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    KeyShow: see keyshow.h.
*/
#include "keyshow.h"
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include <algorithm>
#include <clocale>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace lune;

namespace rambler {

namespace {
// a key: its place and width in key units (a row is one unit high), its X key code (the
// evdev numbering), a fixed label for the keys that type nothing
struct Key { float x, y, w, h; int code; const char *label; };

// a full-size keyboard (ISO: the key beside the left Shift and the tall Return)
std::vector<Key> layout() {
    std::vector<Key> k;
    auto row = [&](float y, float x, std::initializer_list<std::pair<int, float>> keys) {
        for (auto &[code, w] : keys) { if (code) k.push_back({x, y, w, 1, code, nullptr}); x += w; }
    };
    // the function keys
    k.push_back({0, 0, 1, 1, 9, "Esc"});
    for (int i = 0; i < 12; i++) {
        float x = 2 + i + (i / 4) * 0.5f;
        static const char *F[] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
        k.push_back({x, 0, 1, 1, i < 10 ? 67 + i : 95 + (i - 10), F[i]});
    }
    // the main block
    row(1.5f, 0, {{49, 1}, {10, 1}, {11, 1}, {12, 1}, {13, 1}, {14, 1}, {15, 1}, {16, 1}, {17, 1}, {18, 1}, {19, 1}, {20, 1}, {21, 1}});
    k.push_back({13, 1.5f, 2, 1, 22, "⟵"});
    k.push_back({0, 2.5f, 1.5f, 1, 23, "Tab"});
    row(2.5f, 1.5f, {{24, 1}, {25, 1}, {26, 1}, {27, 1}, {28, 1}, {29, 1}, {30, 1}, {31, 1}, {32, 1}, {33, 1}, {34, 1}, {35, 1}});
    k.push_back({13.5f, 2.5f, 1.5f, 2, 36, "↵"});
    k.push_back({0, 3.5f, 1.75f, 1, 66, "Caps"});
    row(3.5f, 1.75f, {{38, 1}, {39, 1}, {40, 1}, {41, 1}, {42, 1}, {43, 1}, {44, 1}, {45, 1}, {46, 1}, {47, 1}, {48, 1}, {51, 1}});
    k.push_back({0, 4.5f, 1.25f, 1, 50, "Shift"});
    row(4.5f, 1.25f, {{94, 1}, {52, 1}, {53, 1}, {54, 1}, {55, 1}, {56, 1}, {57, 1}, {58, 1}, {59, 1}, {60, 1}, {61, 1}});
    k.push_back({12.25f, 4.5f, 2.75f, 1, 62, "Shift"});
    k.push_back({0, 5.5f, 1.25f, 1, 37, "Ctrl"});
    k.push_back({1.25f, 5.5f, 1.25f, 1, 133, "Super"});
    k.push_back({2.5f, 5.5f, 1.25f, 1, 64, "Alt"});
    k.push_back({3.75f, 5.5f, 6.25f, 1, 65, ""});
    k.push_back({10, 5.5f, 1.25f, 1, 108, "AltGr"});
    k.push_back({11.25f, 5.5f, 1.25f, 1, 134, "Super"});
    k.push_back({12.5f, 5.5f, 1.25f, 1, 135, "Menu"});
    k.push_back({13.75f, 5.5f, 1.25f, 1, 105, "Ctrl"});
    // the editing keys and the cursor keys
    float e = 15.5f;
    k.push_back({e, 0, 1, 1, 107, "PrtSc"});
    k.push_back({e + 1, 0, 1, 1, 78, "Scroll"});
    k.push_back({e + 2, 0, 1, 1, 127, "Pause"});
    k.push_back({e, 1.5f, 1, 1, 118, "Ins"});
    k.push_back({e + 1, 1.5f, 1, 1, 110, "Home"});
    k.push_back({e + 2, 1.5f, 1, 1, 112, "PgUp"});
    k.push_back({e, 2.5f, 1, 1, 119, "Del"});
    k.push_back({e + 1, 2.5f, 1, 1, 115, "End"});
    k.push_back({e + 2, 2.5f, 1, 1, 117, "PgDn"});
    k.push_back({e + 1, 4.5f, 1, 1, 111, "↑"});
    k.push_back({e, 5.5f, 1, 1, 113, "←"});
    k.push_back({e + 1, 5.5f, 1, 1, 116, "↓"});
    k.push_back({e + 2, 5.5f, 1, 1, 114, "→"});
    // the numeric keypad
    float n = 19;
    k.push_back({n, 1.5f, 1, 1, 77, "Num"});
    k.push_back({n + 1, 1.5f, 1, 1, 106, nullptr});
    k.push_back({n + 2, 1.5f, 1, 1, 63, nullptr});
    k.push_back({n + 3, 1.5f, 1, 1, 82, nullptr});
    row(2.5f, n, {{79, 1}, {80, 1}, {81, 1}});
    k.push_back({n + 3, 2.5f, 1, 2, 86, nullptr});
    row(3.5f, n, {{83, 1}, {84, 1}, {85, 1}});
    row(4.5f, n, {{87, 1}, {88, 1}, {89, 1}});
    k.push_back({n + 3, 4.5f, 1, 2, 104, "↵"});
    k.push_back({n, 5.5f, 2, 1, 90, nullptr});
    k.push_back({n + 2, 5.5f, 1, 1, 91, nullptr});
    return k;
}

// what a key types (in the keymap in use, with the given modifiers): through a local input
// method, which knows every character (not only Latin-1)
class Typer {
public:
    Typer() {
        std::setlocale(LC_CTYPE, "");
        char *old = XSetLocaleModifiers("@im=none");
        (void)old;
        im_ = XOpenIM(fl_display, nullptr, nullptr, nullptr);
        if (im_) ic_ = XCreateIC(im_, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, nullptr);
    }
    ~Typer() { if (ic_) XDestroyIC(ic_); if (im_) XCloseIM(im_); }
    std::string type(int code, unsigned state) {
        XKeyEvent ev = {};
        ev.type = KeyPress;
        ev.display = fl_display;
        ev.root = ev.window = RootWindow(fl_display, fl_screen);
        ev.keycode = (unsigned)code;
        ev.state = state;
        char buf[32] = "";
        KeySym ks = NoSymbol;
        int n = 0;
        if (ic_) {
            Status st;
            n = Xutf8LookupString(ic_, &ev, buf, sizeof buf - 1, &ks, &st);
            if (st != XLookupChars && st != XLookupBoth) n = 0;
        } else n = XLookupString(&ev, buf, sizeof buf - 1, &ks, nullptr);
        buf[std::max(0, n)] = 0;
        std::string s = buf;
        // (control characters are no label)
        if (!s.empty() && (unsigned char)s[0] < 32) s.clear();
        if (s == "\x7f") s.clear();
        if (s.empty()) {
            // a dead key: its accent shown; others by the key symbol's name
            if (ks == NoSymbol) ks = XkbKeycodeToKeysym(fl_display, (KeyCode)code, 0, (state & ShiftMask) ? 1 : 0);
            static const std::pair<KeySym, const char *> DEAD[] = {
                {XK_dead_grave, "`"}, {XK_dead_acute, "´"}, {XK_dead_circumflex, "^"}, {XK_dead_tilde, "~"},
                {XK_dead_diaeresis, "¨"}, {XK_dead_cedilla, "¸"}, {XK_dead_ogonek, "˛"}, {XK_dead_caron, "ˇ"},
                {XK_dead_abovedot, "˙"}, {XK_dead_doubleacute, "˝"}, {XK_dead_breve, "˘"}, {XK_dead_abovering, "°"},
                {XK_dead_macron, "¯"}};
            for (auto &[d, t] : DEAD) if (ks == d) return t;
            // the keypad without Num Lock: the moving keys
            static const std::pair<KeySym, const char *> PAD[] = {
                {XK_KP_Home, "Home"}, {XK_KP_End, "End"}, {XK_KP_Prior, "PgUp"}, {XK_KP_Next, "PgDn"},
                {XK_KP_Up, "↑"}, {XK_KP_Down, "↓"}, {XK_KP_Left, "←"}, {XK_KP_Right, "→"},
                {XK_KP_Insert, "Ins"}, {XK_KP_Delete, "Del"}, {XK_KP_Begin, "·"}};
            for (auto &[d, t] : PAD) if (ks == d) return t;
        }
        return s;
    }

private:
    XIM im_ = nullptr;
    XIC ic_ = nullptr;
};

class Keyboard : public Area {
public:
    Keyboard() : Area(Frame_None), keys_(layout()) {
        background(MPEN_BACKGROUND);
        Fl::add_timeout(0.05, poll, this);
    }
    ~Keyboard() override { Fl::remove_timeout(poll, this); }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override {
        minw = 460; minh = 140; defw = 920; defh = 280; maxw = maxh = 10000;
    }
    int handle(int e) override {
        if (e == FL_PUSH) {
            // a modifier clicked: latched, or let go
            if (const Key *k = key_at(Fl::event_x(), Fl::event_y())) {
                static const int LATCH[] = {50, 62, 37, 105, 64, 108};
                for (int c : LATCH) if (k->code == c) {
                    if (latched_.count(c)) latched_.erase(c); else latched_.insert(c);
                    redraw();
                    if (on_change) on_change();
                }
            }
            return 1;
        }
        return Area::handle(e);
    }
    unsigned state() const {
        auto down = [&](int c) { return pressed_.count(c) || latched_.count(c); };
        unsigned s = 0;
        if (down(50) || down(62)) s |= ShiftMask;
        if (down(37) || down(105)) s |= ControlMask;
        if (down(64)) s |= Mod1Mask;
        if (down(108)) s |= Mod5Mask;   // (AltGr: the third level)
        // the locks as they are
        unsigned locks = 0;
        XkbGetIndicatorState(fl_display, XkbUseCoreKbd, &locks);
        if (locks & 1) s |= LockMask;
        if (locks & 2) s |= Mod2Mask;   // (Num Lock)
        return s;
    }
    std::function<void()> on_change;

protected:
    static void poll(void *d) {
        auto *k = (Keyboard *)d;
        char map[32];
        XQueryKeymap(fl_display, map);
        std::set<int> now;
        for (int i = 8; i < 256; i++) if (map[i / 8] & (1 << (i % 8))) now.insert(i);
        if (now != k->pressed_) {
            k->pressed_ = now;
            k->redraw();
            if (k->on_change) k->on_change();
        }
        Fl::repeat_timeout(0.05, poll, d);
    }
    const Key *key_at(int X, int Y) const {
        for (auto &k : keys_) {
            int kx, ky, kw, kh;
            place(k, kx, ky, kw, kh);
            if (X >= kx && X < kx + kw && Y >= ky && Y < ky + kh) return &k;
        }
        return nullptr;
    }
    void place(const Key &k, int &X, int &Y, int &W, int &H) const {
        float u = std::min(mwidth() / 23.0f, mheight() / 6.5f);
        int ox = mleft() + (int)((mwidth() - 23 * u) / 2), oy = mtop() + (int)((mheight() - 6.5f * u) / 2);
        X = ox + (int)(k.x * u) + 1;
        Y = oy + (int)(k.y * u) + 1;
        W = (int)(k.w * u) - 2;
        H = (int)(k.h * u) - 2;
    }
    void draw_contents() override {
        unsigned st = state();
        const Pens &P = pens();
        for (auto &k : keys_) {
            int X, Y, W, H;
            place(k, X, Y, W, H);
            bool down = pressed_.count(k.code) || latched_.count(k.code);
            // the key: raised, or pressed in (the fill pen)
            fl_color(down ? P[MPEN_FILL] : P[MPEN_HALFSHINE]);
            fl_rectf(X, Y, W, H);
            fl_color(down ? P[MPEN_SHADOW] : P[MPEN_SHINE]);
            fl_xyline(X, Y, X + W - 1);
            fl_yxline(X, Y, Y + H - 1);
            fl_color(down ? P[MPEN_SHINE] : P[MPEN_SHADOW]);
            fl_xyline(X, Y + H - 1, X + W - 1);
            fl_yxline(X + W - 1, Y, Y + H - 1);
            std::string label = k.label ? tr(k.label) : typer_.type(k.code, st);
            if (label.empty()) continue;
            bool fixed = k.label != nullptr;
            int size = std::max(7, std::min(H * (fixed ? 1 : 2) / 3, (int)(prefs().font_size * (fixed ? 0.9 : 1.4))));
            fl_font(fixed ? prefs().font : FL_HELVETICA_BOLD, size);
            while (size > 7 && fl_width(label.c_str()) > W - 4) fl_font(fixed ? prefs().font : FL_HELVETICA_BOLD, --size);
            fl_color(down ? P[MPEN_SHINE] : P[MPEN_TEXT]);
            fl_draw(label.c_str(), X + 2, Y + 1, W - 4, H - 2, FL_ALIGN_CENTER | FL_ALIGN_CLIP);
        }
    }
    std::vector<Key> keys_;
    std::set<int> pressed_, latched_;
    mutable Typer typer_;
};

std::string layout_name() {
    std::string out;
    if (FILE *p = popen("setxkbmap -query 2>/dev/null", "r")) {
        char b[256];
        while (fgets(b, sizeof b, p)) {
            std::string l = b;
            if (l.rfind("layout:", 0) == 0) {
                out = l.substr(7);
                out.erase(0, out.find_first_not_of(" \t"));
                while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
            }
        }
        pclose(p);
    }
    return out;
}
}  // namespace

// (the keys pressed only show: none of them works the window, Escape no more than others)
class KSWindow : public lune::Window {
public:
    using lune::Window::Window;
    int handle(int e) override {
        if ((e == FL_KEYBOARD || e == FL_KEYUP) && !(Fl::event_state() & FL_META)) return 1;
        return lune::Window::handle(e);
    }
};

void run_keyshow() {
    auto *root = VGroup();
    auto *kb = new Keyboard();
    auto *info = new lune::Text("", Frame_Text);
    root->end();
    auto *w = new KSWindow("KeyShow", root);
    w->id("KEYSHOW");
    w->resizable(root);
    std::string lay = layout_name();
    auto show = [=] {
        unsigned s = kb->state();
        std::string q;
        if (s & ShiftMask) q += " Shift";
        if (s & ControlMask) q += " Ctrl";
        if (s & Mod1Mask) q += " Alt";
        if (s & Mod5Mask) q += " AltGr";
        if (s & LockMask) q += " Caps Lock";
        info->contents(tr("Keymap:") + " " + (lay.empty() ? "?" : lay) + "    " + tr("Qualifiers:") + (q.empty() ? " -" : q));
    };
    kb->on_change = show;
    show();
    w->menustrip({{"Project", 0, 0, 0, 0, {{"Quit", 1, 0, 'q'}}}});
    w->on_menu = [](int) { exit(0); };
    w->callback([](Fl_Widget *, void *) { exit(0); });
    w->size(std::max(w->w(), 920), std::max(w->h(), 300));
    w->show();
}

}  // namespace rambler
