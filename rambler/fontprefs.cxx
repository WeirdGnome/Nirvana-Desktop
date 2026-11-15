/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Font preferences (prefs/font/fpeditor.c): see fontprefs.h.
*/
#include "fontprefs.h"
#include "prefs.h"
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <memory>

using namespace lune;

namespace rambler {

namespace {
// a font's line as the original shows it: "DejaVu Sans/13"; the family's when not by name
std::string font_text(const std::string &name, int fallback_family, int size) {
    static const char *const FAMS[] = {"Sans", "Serif", "Fixed"};
    std::string n = !name.empty() ? name : fallback_family >= 0 && fallback_family < 3 ? FAMS[fallback_family] : tr("Screen font");
    return n + "/" + std::to_string(size);
}
}  // namespace

static Window *g_font_editor = nullptr;

void edit_font_prefs(bool standalone) {
    if (g_font_editor) { g_font_editor->show(); return; }
    // what is edited: the icon font (Rambler's), the screen and the system font (Lune's)
    struct Work {
        RamblerPrefs rambler;
        std::string screen, fixed;
        int screen_size, fixed_size, screen_family;
    };
    auto work = std::make_shared<Work>();
    work->rambler = RamblerPrefs::load();
    Prefs &lp = prefs();
    work->screen = lp.font_name;
    work->screen_size = lp.font_size;
    work->screen_family = lp.font_family;
    work->fixed = lp.fixed_font_name;
    work->fixed_size = lp.fixed_font_size > 0 ? lp.fixed_font_size : lp.font_size;
    if (work->rambler.icon_font_size <= 0) work->rambler.icon_font_size = std::max(6, lp.font_size - 1);

    auto *root = VGroup();
    auto *g = new Group(Group::Vertical, Frame_Group);
    g->frame_title("Fonts");
    g->columns(3);
    new Text("\033rIcons:");
    auto *ti = new Text("\033l" + std::string(28, 'n'), Frame_Text);   // (room for a long name)
    ti->weight(255);
    auto *pi = PopButton(Pop_Up);
    new Text("\033rScreen text:");
    auto *ts = new Text("\033l" + std::string(28, 'n'), Frame_Text);   // (room for a long name)
    auto *ps = PopButton(Pop_Up);
    new Text("\033rSystem default text:");
    auto *tf = new Text("\033l" + std::string(28, 'n'), Frame_Text);   // (room for a long name)
    auto *pf = PopButton(Pop_Up);
    g->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();

    auto show = [=] {
        ti->contents("\033l" + font_text(work->rambler.icon_font_name, work->rambler.icon_font, work->rambler.icon_font_size));
        ts->contents("\033l" + font_text(work->screen, work->screen_family, work->screen_size));
        tf->contents("\033l" + font_text(work->fixed.empty() ? "Courier" : work->fixed, -1, work->fixed_size));
    };
    // each popup: the font requester, starting at the font there is
    pi->on_press = [=] {
        std::string f = work->rambler.icon_font_name;
        int s = work->rambler.icon_font_size;
        if (font_request(tr("Icons"), f, s)) { work->rambler.icon_font_name = f; work->rambler.icon_font_size = s; show(); }
    };
    ps->on_press = [=] {
        std::string f = work->screen;
        int s = work->screen_size;
        if (font_request(tr("Screen text"), f, s)) { work->screen = f; work->screen_size = std::clamp(s, 9, 24); show(); }
    };
    pf->on_press = [=] {
        std::string f = work->fixed;
        int s = work->fixed_size;
        if (font_request(tr("System default text"), f, s)) { work->fixed = f; work->fixed_size = std::clamp(s, 6, 24); show(); }
    };

    // Use: the settings in use (ENV:) for Rambler and Lune; Save: also the saved ones
    auto put = [=](bool archive) {
        work->rambler.save(archive);
        Prefs &p = prefs();
        p.font_name = work->screen;
        p.font_size = work->screen_size;
        p.fixed_font_name = work->fixed;
        p.fixed_font_size = work->fixed_size;
        save_prefs(archive);
        apply_prefs();
    };
    auto *w = new Window("Font Preferences", root);
    w->id("FONTPREFS");
    g_font_editor = w;
    show();   // (after the window has measured the fields with room for long names)
    auto close = [w, standalone] {
        w->hide();
        Fl::delete_widget(w);
        g_font_editor = nullptr;
        if (standalone) exit(0);
    };
    save->on_press = [=] { put(true); close(); };
    use->on_press = [=] { put(false); close(); };
    cancel->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace rambler
