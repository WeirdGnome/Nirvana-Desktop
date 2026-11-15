/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Editor: see editor.h.
*/
#include "editor.h"
#include "lune.h"
#include "catalog.h"
#include "tools.h"

#include <FL/Fl.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

namespace {
enum {
    M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_PRINT, M_ABOUT, M_QUIT,
    M_UNDO, M_CUT, M_COPY, M_PASTE, M_SELALL,
    M_FIND, M_FINDNEXT, M_REPLACE, M_GOTO, M_WRAP
};

// the editing field (a TextEditor object): FLTK's text editor in the string frame, in the
// fixed font and the pens; Super+key is left to the menus
class TextEd : public Fl_Text_Editor {
public:
    TextEd(int X, int Y, int W, int H) : Fl_Text_Editor(X, Y, W, H) {}
    int handle(int e) override {
        if ((e == FL_KEYBOARD || e == FL_SHORTCUT) && (Fl::event_state() & FL_META)) return 0;
        return Fl_Text_Editor::handle(e);
    }
};

class EditArea : public Area {
public:
    EditArea() : Area(Frame_String) {
        begin();
        ed_ = new TextEd(x(), y(), w(), h());
        ed_->box(FL_FLAT_BOX);
        end();
        resizable(ed_);
        buf_ = new Fl_Text_Buffer();
        ed_->buffer(buf_);
        style();
    }
    ~EditArea() override { ed_->buffer(nullptr); delete buf_; }
    Fl_Text_Editor *ed() { return ed_; }
    Fl_Text_Buffer *buf() { return buf_; }
    void style() {
        const Prefs &p = prefs();
        int size = p.fixed_font_size > 0 ? p.fixed_font_size : p.font_size;
        ed_->textfont(p.font_fixed);
        ed_->textsize(size);
        ed_->color(pens()[MPEN_SHINE]);
        ed_->textcolor(pens()[MPEN_TEXT]);
        ed_->selection_color(pens()[MPEN_FILL]);
        ed_->cursor_color(pens()[MPEN_TEXT]);
        ed_->scrollbar_width(16);
        ed_->linenumber_width(0);
    }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override {
        minw = 120; minh = 60; defw = 600; defh = 400; maxw = maxh = 10000;
    }
    void resize(int X, int Y, int W, int H) override {
        Fl_Group::resize(X, Y, W, H);
        ed_->resize(mleft(), mtop(), mwidth(), mheight());
    }

private:
    TextEd *ed_;
    Fl_Text_Buffer *buf_;
};

// a small window with a few string fields and buttons; returns the button pressed
// (0: closed), the fields' values in vals
int ask(const std::string &title, const std::vector<std::string> &labels, std::vector<std::string> &vals,
        const std::vector<std::string> &buttons, Checkmark **case_out = nullptr, bool *case_val = nullptr) {
    auto *root = VGroup();
    auto *g = new Group(Group::Vertical);
    g->columns(2);
    std::vector<String *> fields;
    for (size_t i = 0; i < labels.size(); i++) {
        new lune::Text("\033r" + labels[i]);
        fields.push_back(new String(vals[i]));
    }
    Checkmark *cs = nullptr;
    if (case_val) {
        new lune::Text("\033rMatch case:");
        auto *row = HGroup();
        cs = new Checkmark(*case_val);
        HSpace();
        row->end();
    }
    g->end();
    auto *btns = HGroup();
    btns->same_width(true);
    int done = -1;
    for (size_t i = 0; i < buttons.size(); i++) {
        if (i) HSpace();
        auto *b = new Button(buttons[i]);
        int n = (int)i + 1;
        b->on_press = [&done, n] { done = n; };
    }
    btns->end();
    root->end();
    for (auto *f : fields) f->on_acknowledge = [&done] { done = 1; };
    auto *w = new lune::Window(title, root);
    w->callback([](Fl_Widget *, void *d) { *(int *)d = 0; }, &done);
    w->size(std::max(w->w(), 360), w->h());
    w->set_modal();
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
    if (!fields.empty()) fields[0]->input()->take_focus();
    while (done < 0 && w->shown()) Fl::wait();
    for (size_t i = 0; i < fields.size(); i++) vals[i] = fields[i]->value();
    if (cs && case_val) *case_val = cs->on();
    (void)case_out;
    w->hide();
    Fl::delete_widget(w);
    return std::max(done, 0);
}

struct State {
    std::string file;
    bool changed = false;
    std::string find, replace;
    bool match_case = false;
};
}  // namespace

void run_editor(const std::string &start) {
    auto st = std::make_shared<State>();
    auto *root = VGroup();
    auto *area = new EditArea();
    auto *status = new lune::Text("", Frame_Text);
    root->end();
    auto *w = new lune::Window("Editor", root);
    w->id("EDITOR");
    w->resizable(root);
    Fl_Text_Editor *ed = area->ed();
    Fl_Text_Buffer *buf = area->buf();

    auto set_title = [=] {
        std::string t = st->file.empty() ? tr("Untitled") : fs::path(st->file).filename().string();
        if (st->changed) t = "*" + t;
        w->copy_label(t.c_str());
    };
    // the status line: the line and column of the cursor
    auto show_pos = [=] {
        int pos = ed->insert_position();
        int line = buf->count_lines(0, pos) + 1, col = pos - buf->line_start(pos) + 1;
        char b[96];
        snprintf(b, sizeof b, "%s %d   %s %d", tr("Line").c_str(), line, tr("Column").c_str(), col);
        status->contents(b);
    };
    buf->add_modify_callback([](int, int ins, int del, int, const char *, void *d) {
        if (ins || del) (*(std::function<void()> *)d)();
    }, new std::function<void()>([=] { if (!st->changed) { st->changed = true; set_title(); } }));
    // (the cursor's place after each event in the field)
    struct Tick {
        static void cb(void *d) { (*(std::function<void()> *)d)(); Fl::repeat_timeout(0.2, cb, d); }
    };
    Fl::add_timeout(0.2, Tick::cb, new std::function<void()>(show_pos));

    auto save_to = [=](const std::string &f) -> bool {
        if (buf->savefile(f.c_str()) != 0) {
            request("Editor", tr("Cannot save the file:") + "\n" + f + "\n" + strerror(errno), "OK");
            return false;
        }
        st->file = f;
        st->changed = false;
        set_title();
        return true;
    };
    auto save_as = [=]() -> bool {
        std::string p = st->file.empty() ? std::string(getenv("HOME") ? getenv("HOME") : "/") + "/" : st->file;
        if (!file_request(tr("Save As"), p)) return false;
        if (fs::exists(p) && p != st->file &&
            request("Editor", tr("The file exists already:") + "\n" + p + "\n" + tr("Replace it?"), "Replace|Cancel") != 1)
            return false;
        return save_to(p);
    };
    auto save = [=]() -> bool { return st->file.empty() ? save_as() : save_to(st->file); };
    // the changes asked about: false - the user does not want the text to go
    auto may_leave = [=]() -> bool {
        if (!st->changed) return true;
        int r = request("Editor", tr("The text has changed.") + "\n" + tr("Save the changes?"), "Save|Discard|Cancel");
        if (r == 1) return save();
        return r == 2;
    };
    auto load = [=](const std::string &f) {
        if (fs::exists(f)) {
            if (buf->loadfile(f.c_str()) != 0) {
                request("Editor", tr("Cannot open the file:") + "\n" + f, "OK");
                return;
            }
        } else buf->text("");   // (a new file: made when saved)
        st->file = f;
        st->changed = false;
        ed->insert_position(0);
        ed->scroll(0, 0);
        set_title();
    };
    auto find_next = [=](bool wrap) -> bool {
        if (st->find.empty()) return false;
        int start = ed->insert_position(), found = 0;
        bool ok = buf->search_forward(start, st->find.c_str(), &found, st->match_case);
        if (!ok && wrap) ok = buf->search_forward(0, st->find.c_str(), &found, st->match_case);
        if (!ok) { request("Editor", tr("Not found:") + " " + st->find, "OK"); return false; }
        buf->select(found, found + (int)st->find.size());
        ed->insert_position(found + (int)st->find.size());
        ed->show_insert_position();
        return true;
    };

    w->menustrip({
        {"Project", 0, 0, 0, 0, {
            {"New", M_NEW, 0, 'n'},
            {"Open...", M_OPEN, 0, 'o'},
            {"Save", M_SAVE, 0, 's'},
            {"Save As...", M_SAVEAS, 0},
            {"Print", M_PRINT, Menu_Line, 'p'},
            {"About...", M_ABOUT, Menu_Line},
            {"Quit", M_QUIT, 0, 'q'},
        }},
        {"Edit", 0, 0, 0, 0, {
            {"Undo", M_UNDO, Menu_Line, 'z'},
            {"Cut", M_CUT, 0, 'x'},
            {"Copy", M_COPY, 0, 'c'},
            {"Paste", M_PASTE, Menu_Line, 'v'},
            {"Select All", M_SELALL, 0, 'a'},
        }},
        {"Search", 0, 0, 0, 0, {
            {"Find...", M_FIND, 0, 'f'},
            {"Find Next", M_FINDNEXT, 0, 'g'},
            {"Replace...", M_REPLACE, Menu_Line, 'r'},
            {"Go to Line...", M_GOTO, 0, 'l'},
        }},
        {"Settings", 0, 0, 0, 0, {
            {"Word Wrap", M_WRAP, Menu_Toggle, 'w'},
        }},
    });
    w->on_menu = [=](int id) {
        switch (id) {
        case M_NEW: if (may_leave()) { buf->text(""); st->file.clear(); st->changed = false; set_title(); } break;
        case M_OPEN: {
            if (!may_leave()) break;
            std::string p = st->file.empty() ? std::string(getenv("HOME") ? getenv("HOME") : "/") + "/" : st->file;
            if (file_request(tr("Open"), p)) load(p);
            break;
        }
        case M_SAVE: save(); break;
        case M_SAVEAS: save_as(); break;
        case M_PRINT: {
            // (the text as it is now, saved or not)
            std::string tmp = env_dir() + "/editor-print.txt";
            std::error_code ec;
            fs::create_directories(env_dir(), ec);
            buf->savefile(tmp.c_str());
            std::string out;
            int rc = -1;
            std::string title = st->file.empty() ? tr("Untitled") : fs::path(st->file).filename().string();
            std::string cmd = "lp -t '";
            for (char c : title) { if (c == '\'') cmd += "'\\''"; else cmd += c; }
            cmd += "' -- '" + tmp + "' 2>&1";
            if (FILE *p = popen(cmd.c_str(), "r")) {
                char b[512];
                size_t n;
                while ((n = fread(b, 1, sizeof b, p)) > 0) out.append(b, n);
                rc = pclose(p);
            }
            request("Editor", rc == 0 ? tr("Sent to the printer.") + "\n" + out : tr("Cannot print:") + "\n" + out, "OK");
            break;
        }
        case M_ABOUT: request("Editor", "Editor\n" + tr("A plain text editor."), "OK"); break;
        case M_QUIT: if (may_leave()) exit(0); break;
        case M_UNDO: Fl_Text_Editor::kf_undo(0, ed); break;
        case M_CUT: Fl_Text_Editor::kf_cut(0, ed); break;
        case M_COPY: Fl_Text_Editor::kf_copy(0, ed); break;
        case M_PASTE: Fl_Text_Editor::kf_paste(0, ed); break;
        case M_SELALL: Fl_Text_Editor::kf_select_all(0, ed); break;
        case M_FIND: {
            std::vector<std::string> v = {st->find};
            if (char *sel = buf->selection_text()) { if (*sel && !strchr(sel, '\n')) v[0] = sel; free(sel); }
            if (ask(tr("Find"), {"Find:"}, v, {"Find", "Cancel"}, nullptr, &st->match_case) == 1 && !v[0].empty()) {
                st->find = v[0];
                find_next(true);
            }
            break;
        }
        case M_FINDNEXT: if (st->find.empty()) w->menu_pick(M_FIND); else find_next(true); break;
        case M_REPLACE: {
            std::vector<std::string> v = {st->find, st->replace};
            int r = ask(tr("Replace"), {"Find:", "Replace with:"}, v, {"Replace", "Replace All", "Cancel"}, nullptr, &st->match_case);
            if (r != 1 && r != 2) break;
            st->find = v[0];
            st->replace = v[1];
            if (st->find.empty()) break;
            if (r == 1) {
                // the next one replaced (the one marked, when it is the text looked for)
                int a, b;
                bool marked = buf->selection_position(&a, &b) && b - a == (int)st->find.size();
                if (marked) {
                    char *s = buf->text_range(a, b);
                    bool same = st->match_case ? !strcmp(s, st->find.c_str()) : !strcasecmp(s, st->find.c_str());
                    free(s);
                    if (same) { buf->replace(a, b, st->replace.c_str()); ed->insert_position(a + (int)st->replace.size()); }
                }
                find_next(true);
            } else {
                int pos = 0, found, n = 0;
                while (buf->search_forward(pos, st->find.c_str(), &found, st->match_case)) {
                    buf->replace(found, found + (int)st->find.size(), st->replace.c_str());
                    pos = found + (int)st->replace.size();
                    n++;
                }
                request("Editor", std::to_string(n) + " " + tr("replaced."), "OK");
            }
            break;
        }
        case M_GOTO: {
            std::vector<std::string> v = {""};
            if (ask(tr("Go to Line"), {"Line:"}, v, {"OK", "Cancel"}) == 1 && atoi(v[0].c_str()) > 0) {
                int pos = buf->skip_lines(0, atoi(v[0].c_str()) - 1);
                ed->insert_position(pos);
                ed->show_insert_position();
                ed->take_focus();
            }
            break;
        }
        case M_WRAP:
            ed->wrap_mode(w->menu_checked(M_WRAP) ? Fl_Text_Display::WRAP_AT_BOUNDS : Fl_Text_Display::WRAP_NONE, 0);
            break;
        }
    };
    w->callback([](Fl_Widget *, void *d) { ((lune::Window *)d)->menu_pick(M_QUIT); }, w);
    w->size(std::max(w->w(), 640), std::max(w->h(), 480));
    if (!start.empty()) load(fs::absolute(start).string());
    else set_title();
    st->changed = false;
    set_title();
    w->show();
    ed->take_focus();
}

}  // namespace rambler
