/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    A test: a drawer window (IconWindow) of the directory given, and the volumes.
*/
#include "iconwindow.h"

#include <cstdio>

using namespace zune;
using namespace wanderer;

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : getenv("HOME");
    auto *win = new IconWindow(dir);
    win->update();
    win->on_open = [](IconWindow *, IconEntry &e) { printf("open %s\n", e.path.c_str()); };
    win->resize(20, 20, 480, 300);
    win->show();
    auto *vols = new IconWindow("");
    vols->update();
    vols->resize(560, 20, 160, 360);
    vols->show();
    return run();
}
