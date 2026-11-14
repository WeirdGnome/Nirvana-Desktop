// Nirvana Desktop - Rambler: the virtual screens in the menu bar (screens.cxx)
#pragma once
class Fl_Widget;

namespace screens {
int count();
int current();
void go(int screen);   // asks Insight to show that screen (wraps around)
Fl_Widget *make_gadget(int X, int Y, int W, int H);
// pulling the shown screen down (the gadget, the empty middle of the menu bar)
void pull_begin(int y_root);
void pull_move(int y_root);
void pull_end();
bool pulling();
}  // namespace screens
