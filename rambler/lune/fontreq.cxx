/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The font requester (asl.library's FontRequest, as Popasl opens it): the families of
    the system on the left, the sizes on the right with the size typed in under them, a
    sample in the chosen font, OK / Cancel.
*/
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <memory>
#include <set>

namespace lune {

std::vector<std::string> font_families() {
    static std::vector<std::string> cache;
    if (!cache.empty()) return cache;
    // Fl::set_fonts fills the font table from the system's fonts (with Xft each family
    // once per style: " Name" plain, "BName" bold, "IName" italic, "PName" both)
    int n = Fl::set_fonts(nullptr);
    std::set<std::string> s;
    for (int i = FL_FREE_FONT; i < n; i++) {
        const char *name = Fl::get_font((Fl_Font)i);
        if (name && name[0] == ' ' && name[1]) s.insert(name + 1);
    }
    cache.assign(s.begin(), s.end());
    return cache;
}

bool font_request(const std::string &title, std::string &family, int &size) {
    static const int SIZES[] = {8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 28, 32, 36};
    auto fams = std::make_shared<std::vector<std::string>>(font_families());
    struct State { std::string family; int size; bool done = false, ok = false; };
    auto st = std::make_shared<State>(State{family, size > 0 ? size : prefs().font_size});

    auto *root = VGroup();
    auto *top = HGroup();
    auto *flist = new List();
    flist->frame(Frame_InputList);
    flist->weight(300);
    int fi = -1;
    for (size_t i = 0; i < fams->size(); i++) {
        flist->add({(*fams)[i]});
        if ((*fams)[i] == st->family) fi = (int)i;
    }
    flist->done();
    auto *right = VGroup();
    right->weight(60);
    auto *slist = new List();
    slist->frame(Frame_InputList);
    int si = -1;
    for (int i = 0; i < (int)(sizeof SIZES / sizeof *SIZES); i++) {
        slist->add({std::to_string(SIZES[i])});
        if (SIZES[i] == st->size) si = i;
    }
    slist->done();
    auto *sizes = new String(std::to_string(st->size), 3);
    sizes->input()->type(FL_INT_INPUT);
    right->end();
    top->end();
    auto *sample = new ImageButton(320, 60, [st](int x, int y, int w, int h, bool) {
        fl_color(pens()[MPEN_SHINE]);
        fl_rectf(x, y, w, h);
        if (st->family.empty()) return;
        fl_push_clip(x, y, w, h);
        fl_font(named_font(st->family), std::clamp(st->size, 4, 96));
        fl_color(pens()[MPEN_TEXT]);
        fl_draw(st->family.c_str(), x + 4, y, w - 8, h, FL_ALIGN_CENTER);
        fl_pop_clip();
    });
    sample->frame(Frame_Text);
    auto *btns = HGroup();
    btns->same_width(true);
    auto *ok = new Button("OK");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();

    flist->on_active = [=](int i) {
        if (i < 0 || i >= (int)fams->size()) return;
        st->family = (*fams)[i];
        sample->redraw();
    };
    flist->on_double = [=](int i) { flist->on_active(i); st->ok = st->done = true; };
    slist->on_active = [=](int i) {
        if (i < 0) return;
        st->size = SIZES[i];
        sizes->value(std::to_string(st->size));
        sizes->redraw();
        sample->redraw();
    };
    sizes->on_acknowledge = [=] {
        int v = atoi(sizes->value());
        if (v >= 4 && v <= 96) { st->size = v; sample->redraw(); }
    };
    ok->on_press = [=] { sizes->on_acknowledge(); st->ok = st->done = true; };
    cancel->on_press = [=] { st->done = true; };

    auto *w = new Window(title, root);
    w->id("FONTREQUESTER");
    w->set_modal();
    w->callback([](Fl_Widget *, void *d) { ((State *)d)->done = true; }, st.get());
    if (fi >= 0) flist->active(fi);
    if (si >= 0) slist->active(si);
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    w->position(sx + (sw - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
    while (!st->done && w->shown()) Fl::wait();
    w->hide();
    Fl::delete_widget(w);
    if (!st->ok || st->family.empty()) return false;
    family = st->family;
    size = st->size;
    return true;
}

}  // namespace lune
