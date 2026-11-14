/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    A test: a drawer (IconDrawerList) of the directory given, and the volumes.
*/
#include "iconlist.h"

#include <cstdio>

using namespace zune;
using namespace wanderer;

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : getenv("HOME");
    auto *root = VGroup();
    auto *list = new IconDrawerList(dir);
    root->end();
    auto *win = new Window(dir, root);
    win->size(560, 360);
    list->update();
    list->on_open = [](IconEntry &e) { printf("open %s\n", e.path.c_str()); };
    win->show();

    auto *vroot = VGroup();
    auto *vols = new IconVolumeList();
    vroot->end();
    auto *vwin = new Window("Volumes", vroot);
    vwin->size(160, 360);
    vols->update();
    vwin->position(600, 20);
    vwin->show();
    return run();
}
