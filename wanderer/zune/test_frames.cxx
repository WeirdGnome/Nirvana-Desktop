/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    A test: every built-in Zune frame, up and down.
*/
#include "frame.h"
#include "pens.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/fl_draw.H>

class Frames : public Fl_Widget {
public:
    Frames(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}
    void draw() override {
        fl_color(zune::pens()[zune::MPEN_BACKGROUND]);
        fl_rectf(x(), y(), w(), h());
        fl_font(FL_HELVETICA, 11);
        for (int t = 1; t < zune::FST_COUNT; t++)
            for (int d = 0; d < 2; d++) {
                int fx = x() + 10 + d * 110, fy = y() + 10 + (t - 1) * 34;
                zune::draw_frame(t, d, fx, fy, 100, 28);
                fl_color(zune::pens()[zune::MPEN_TEXT]);
                char b[16];
                snprintf(b, sizeof b, "%X %s", t, d ? "down" : "up");
                fl_draw(b, fx, fy, 100, 28, FL_ALIGN_CENTER);
            }
    }
};

int main(int argc, char **argv) {
    auto *w = new Fl_Double_Window(240, 400, "Zune frames");
    new Frames(0, 0, 240, 400);
    w->end();
    w->show(argc, argv);
    return Fl::run();
}
