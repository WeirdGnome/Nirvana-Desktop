/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Calculator: see calculator.h.
*/
#include "calculator.h"
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace lune;

namespace rambler {

namespace {
enum { M_CLEAR = 1, M_QUIT, M_CUT, M_COPY, M_PASTE, M_TAPE };

std::string format(double v) {
    if (!std::isfinite(v)) return tr("Error");
    char b[64];
    snprintf(b, sizeof b, "%.12g", v);
    std::string s = b;
    return s == "-0" ? "0" : s;
}

// the engine: the number being typed (entry), the one kept (acc) and the operation waiting
struct Engine {
    std::string entry = "0";
    double acc = 0;
    char op = 0;
    bool fresh = true;        // the next digit starts a new entry
    bool error = false;
    std::function<void(const std::string &)> tape;

    double value() const { return strtod(entry.c_str(), nullptr); }
    double apply(double a, double b, char o) {
        switch (o) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return b == 0 ? NAN : a / b;
        }
        return b;
    }
    void show(double v) {
        entry = format(v);
        error = !std::isfinite(v);
        if (error) { acc = 0; op = 0; }
    }
    void digit(char c) {
        if (error) clear_all();
        if (fresh) { entry = c == '.' ? "0" : ""; fresh = false; }
        if (c == '.' && entry.find('.') != std::string::npos) return;
        if (entry == "0" && c != '.') entry.clear();
        if (entry.size() < 16) entry += c;
    }
    void operation(char o) {
        if (error) return;
        double v = value();
        if (tape) tape(entry + " " + sign(o));
        if (op && !fresh) acc = apply(acc, v, op);
        else if (!op || !fresh) acc = v;
        op = o;
        fresh = true;
        show(acc);
        if (!error) entry = format(acc);
    }
    void equals() {
        if (error || !op) return;
        double v = value();
        if (tape) tape(entry + " =");
        acc = apply(acc, v, op);
        op = 0;
        fresh = true;
        show(acc);
        if (tape) tape(entry);
        if (tape) tape("");
    }
    void negate() {
        if (error || entry == "0") return;
        entry = entry[0] == '-' ? entry.substr(1) : "-" + entry;
        if (fresh) { acc = value(); }
    }
    void backspace() {
        if (error || fresh) return;
        entry.pop_back();
        if (entry.empty() || entry == "-") entry = "0";
    }
    void clear_entry() { entry = "0"; fresh = false; error = false; }
    void clear_all() {
        entry = "0"; acc = 0; op = 0; fresh = true;
        if (error || !tape) { error = false; return; }
        error = false;
    }
    void set(const std::string &text) {
        char *end = nullptr;
        double v = strtod(text.c_str(), &end);
        if (end == text.c_str()) return;
        entry = format(v);
        fresh = true;
        error = false;
    }
    static std::string sign(char o) { return o == '*' ? "×" : o == '/' ? "÷" : std::string(1, o); }
};

// the display: the number right aligned in a text field, the waiting operation at the left;
// a paste lands here
class Display : public Area {
public:
    explicit Display(std::shared_ptr<Engine> e) : Area(Frame_Text), e_(e) { background(&Prefs::text_back); }
    std::function<void(const std::string &)> on_paste;
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override {
        minw = 120; defw = 220; maxw = 10000;
        minh = defh = maxh = font_px() + 10;
    }
    int handle(int ev) override {
        if (ev == FL_PASTE) { if (on_paste) on_paste(Fl::event_text()); return 1; }
        return Area::handle(ev);
    }

protected:
    int font_px() const { return prefs().font_size * 3 / 2 + 4; }
    void draw_contents() override {
        fl_font(FL_HELVETICA_BOLD, font_px());
        fl_color(pens()[MPEN_TEXT]);
        fl_draw(e_->entry.c_str(), mleft() + 4, mtop(), mwidth() - 8, mheight(), FL_ALIGN_RIGHT);
        if (e_->op) {
            fl_font(FL_HELVETICA, prefs().font_size);
            fl_draw(Engine::sign(e_->op).c_str(), mleft() + 4, mtop(), mwidth() - 8, mheight(), FL_ALIGN_LEFT);
        }
    }
    std::shared_ptr<Engine> e_;
};

// the keyboard first (before the window's own keys: Return, Escape)
std::function<bool(int)> g_keys;
class CalcWindow : public lune::Window {
public:
    using lune::Window::Window;
    int handle(int e) override {
        if (e == FL_KEYBOARD && g_keys && g_keys(e)) return 1;
        return lune::Window::handle(e);
    }
};

lune::Window *g_tape = nullptr;
List *g_tape_list = nullptr;
}  // namespace

void run_calculator() {
    auto eng = std::make_shared<Engine>();
    auto *root = VGroup();
    auto *disp = new Display(eng);
    auto *pad = new Group(Group::Vertical);
    pad->columns(5);
    pad->same_width(true);
    // the keys as the original lays them out: digits on the left, operations on the right
    const char *keys[] = {"7", "8", "9", "CA", "CE",
                          "4", "5", "6", "×", "÷",
                          "1", "2", "3", "+", "-",
                          "0", ".", "«", "+/-", "="};
    std::vector<Button *> btn;
    for (const char *k : keys) btn.push_back(new Button(k));
    pad->end();
    root->end();
    auto *w = new CalcWindow("Calculator", root);
    w->id("CALCULATOR");

    auto refresh = [=] { disp->redraw(); };
    // the tape: a window with the lines calculated
    auto tape_lines = std::make_shared<std::vector<std::string>>();
    eng->tape = [=](const std::string &line) {
        tape_lines->push_back(line);
        if (g_tape_list) {
            g_tape_list->add({line});
            g_tape_list->done();
            g_tape_list->jump_bottom();
        }
    };
    auto press = [=](const std::string &k) {
        if (k.size() == 1 && (isdigit((unsigned char)k[0]) || k == ".")) eng->digit(k[0]);
        else if (k == "+" || k == "-") eng->operation(k[0]);
        else if (k == "×" || k == "*") eng->operation('*');
        else if (k == "÷" || k == "/") eng->operation('/');
        else if (k == "=") eng->equals();
        else if (k == "CA") { eng->clear_all(); eng->tape("---"); }
        else if (k == "CE") eng->clear_entry();
        else if (k == "«") eng->backspace();
        else if (k == "+/-") eng->negate();
        refresh();
    };
    for (size_t i = 0; i < btn.size(); i++) {
        std::string k = keys[i];
        btn[i]->on_press = [=] { press(k); };
    }
    disp->on_paste = [=](const std::string &t) { eng->set(t); refresh(); };

    auto show_tape = [=](bool on) {
        if (!on) {
            if (g_tape) { g_tape->hide(); Fl::delete_widget(g_tape); g_tape = nullptr; g_tape_list = nullptr; }
            return;
        }
        if (g_tape) { g_tape->show(); return; }
        auto *troot = VGroup();
        g_tape_list = new List();
        g_tape_list->frame(Frame_ReadList);
        for (auto &l : *tape_lines) g_tape_list->add({l});
        g_tape_list->done();
        troot->end();
        g_tape = new lune::Window("Tape", troot);
        g_tape->id("CALCULATORTAPE");
        g_tape->callback([](Fl_Widget *, void *d) { ((lune::Window *)d)->menu_pick(M_TAPE); }, w);
        g_tape->size(std::max(g_tape->w(), 200), std::max(g_tape->h(), 240));
        if (w->shown()) g_tape->position(w->x() + w->w() + 12, w->y());
        g_tape->show();
        g_tape_list->jump_bottom();
    };

    w->menustrip({
        {"Project", 0, 0, 0, 0, {
            {"Clear", M_CLEAR, Menu_Line},
            {"Quit", M_QUIT, 0, 'q'},
        }},
        {"Edit", 0, 0, 0, 0, {
            {"Cut", M_CUT, 0, 'x'},
            {"Copy", M_COPY, 0, 'c'},
            {"Paste", M_PASTE, 0, 'v'},
        }},
        {"Windows", 0, 0, 0, 0, {
            {"Show Tape", M_TAPE, Menu_Toggle, 't'},
        }},
    });
    w->on_menu = [=](int id) {
        switch (id) {
        case M_CLEAR: press("CA"); break;
        case M_QUIT: exit(0);
        case M_CUT:
        case M_COPY:
            // (both clipboards: the selection and the one Ctrl+V takes from)
            Fl::copy(eng->entry.c_str(), (int)eng->entry.size(), 0);
            Fl::copy(eng->entry.c_str(), (int)eng->entry.size(), 1);
            if (id == M_CUT) press("CE");
            break;
        case M_PASTE: Fl::paste(*disp, 1); break;
        case M_TAPE: show_tape(w->menu_checked(M_TAPE)); break;
        }
    };
    // the keyboard: digits, the operations, Return or = for equals, Backspace, Delete for
    // CE, Escape for CA, n for +/-
    g_keys = [=](int) -> bool {
        if (Fl::event_state() & (FL_META | FL_CTRL | FL_ALT)) return false;
        int k = Fl::event_key();
        const char *t = Fl::event_text();
        if (k == FL_Enter || k == FL_KP_Enter) { press("="); return true; }
        if (k == FL_BackSpace) { press("«"); return true; }
        if (k == FL_Delete) { press("CE"); return true; }
        if (k == FL_Escape) { press("CA"); return true; }
        if (!t || !t[0] || t[1]) return false;
        char c = t[0];
        if (isdigit((unsigned char)c) || c == '.' || c == ',') { press(c == ',' ? "." : std::string(1, c)); return true; }
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '=') { press(std::string(1, c)); return true; }
        if (c == 'n' || c == 'N') { press("+/-"); return true; }
        return false;
    };
    w->callback([](Fl_Widget *, void *) { exit(0); });
    w->show();
}

}  // namespace rambler
