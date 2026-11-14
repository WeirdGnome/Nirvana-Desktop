/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    A test of the Zune classes: a requester with a text, a string and buttons.
*/
#include "zune.h"

#include <FL/Fl.H>
#include <cstdio>

using namespace zune;

int main(int argc, char **argv) {
    auto *root = VGroup();
    {
        auto *g = new Group(Group::Vertical, Frame_Group);
        g->frame_title("Execute a file");
        new Text("\033cEnter command and its arguments:");
        auto *row = HGroup();
        new Text("Command:");
        auto *s = new String("", 256);
        s->weight(200);
        row->end();
        g->end();
        auto *buttons = HGroup();
        buttons->same_width(true);
        auto *ok = new Button("Execute");
        HSpace();
        auto *cancel = new Button("Cancel");
        buttons->end();
        ok->on_press = [s] { printf("execute: %s\n", s->value()); };
        cancel->on_press = [] { exit(0); };
    }
    root->end();
    auto *win = new Window("Execute Command", root);
    win->show(argc, argv);
    return run();
}
