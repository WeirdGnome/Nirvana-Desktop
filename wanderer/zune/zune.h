/*
    Copyright (C) 2002-2026, The AROS Development Team.
    Copyright (C) 1999, David Le Corfec.
    All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Zune classes Wanderer is built on, on FLTK widgets:

      Area        every object: a frame (spec), a background, inner spacing, the minimum /
                  default / maximum size, a weight, the input mode, disabled / selected
      Group       lays out its children in a row or a column (or in columns), with spacing;
                  the free room is shared by the weights within the children's maximum
                  sizes - the layout of muimaster/classes/group.c
      Rectangle   an empty space (HVSpace, HSpace, VSpace)
      Text        a text with Zune's control sequences (\33c centre, \33r right, \33b bold ...)
      Button      a Text with the button frame that reports a release over it
      String      a single line text field in the string frame
      Window      a window with one root object, the window spacing around it
      Application the event loop

    Zune's preferences are the defaults of classes/configdata.c.
*/
#pragma once
#include "frame.h"
#include "pens.h"

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace zune {

constexpr int MAXMAX = 10000;   // MUI_MAXMAX

// configdata.c defaults
struct Prefs {
    int group_hspacing = 6, group_vspacing = 3;
    int window_left = 4, window_right = 4, window_top = 3, window_bottom = 3;
    int font_size = 13;          // the screen font (topaz-like 8 pt is too small on today's screens)
    Fl_Font font = FL_HELVETICA, font_bold = FL_HELVETICA_BOLD, font_fixed = FL_COURIER;
};
Prefs &prefs();

enum InputMode { Input_None, Input_RelVerify, Input_Immediate, Input_Toggle };

// ---------------------------------------------------------------------------------------------
class Area : public Fl_Group {
public:
    Area(StdFrame frame = Frame_None);
    // the frame: a standard one or a spec ("202211")
    void frame(StdFrame f) { frame_ = std_frame(f); }
    void frame(const std::string &spec) { frame_ = FrameSpec::parse(spec); }
    const FrameSpec &frame_spec() const { return frame_; }
    // the background: a pen (MPEN_BACKGROUND by default) or none (the parent's shows through)
    void background(int pen) { bg_pen_ = pen; }
    void inner(int l, int r, int t, int b) { il_ = l; ir_ = r; it_ = t; ib_ = b; inner_set_ = true; }
    void weight(int w) { weight_ = w; }
    int weight() const { return weight_; }
    void fixed_width(int w) { fixw_ = w; }
    void fixed_height(int h) { fixh_ = h; }
    void input_mode(InputMode m) { input_ = m; }
    bool selected() const { return selected_; }
    void selected(bool s);
    void disabled(bool d) { disabled_ = d; d ? deactivate() : activate(); redraw(); }
    std::function<void()> on_press;   // RelVerify: released over it; Toggle: toggled
    // the frame + inner spacing on each side
    int add_left() const, add_right() const, add_top() const, add_bottom() const;
    // AskMinMax: the inside's sizes (without the frame)
    virtual void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh);
    // ... and with it (as the parent group sees them)
    void min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh);
    // the inside rectangle
    int mleft() const { return x() + add_left(); }
    int mtop() const { return y() + add_top(); }
    int mwidth() const { return w() - add_left() - add_right(); }
    int mheight() const { return h() - add_top() - add_bottom(); }
    void draw() override;
    int handle(int e) override;

protected:
    virtual void draw_contents() {}   // MUIM_Draw of the subclass
    void draw_background(int X, int Y, int W, int H);
    FrameSpec frame_;
    int bg_pen_ = MPEN_BACKGROUND;
    int il_ = 0, ir_ = 0, it_ = 0, ib_ = 0;
    bool inner_set_ = false;
    int weight_ = 100, fixw_ = 0, fixh_ = 0;
    InputMode input_ = Input_None;
    bool selected_ = false, disabled_ = false, pressed_ = false;
};

// ---------------------------------------------------------------------------------------------
class Group : public Area {
public:
    enum Dir { Vertical, Horizontal };
    explicit Group(Dir d = Vertical, StdFrame frame = Frame_None);
    void horiz(bool h) { dir_ = h ? Horizontal : Vertical; }
    void spacing(int s) { hs_ = vs_ = s; }
    void hspacing(int s) { hs_ = s; }
    void vspacing(int s) { vs_ = s; }
    void same_size(bool s) { same_w_ = same_h_ = s; }
    void same_width(bool s) { same_w_ = s; }
    void same_height(bool s) { same_h_ = s; }
    void columns(int n) { columns_ = n; }
    void frame_title(const std::string &t) { title_ = t; }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    void resize(int X, int Y, int W, int H) override;
    void layout();
    void draw() override;
    std::vector<Area *> areas() const;   // the children that are Zune objects and shown

protected:
    Dir dir_;
    int hs_, vs_, columns_ = 0;
    bool same_w_ = false, same_h_ = false;
    std::string title_;
    int title_h() const;
    void layout_line(bool horiz);
    void layout_columns();
};

inline Group *VGroup() { return new Group(Group::Vertical); }
inline Group *HGroup() { return new Group(Group::Horizontal); }

// ---------------------------------------------------------------------------------------------
class Rectangle : public Area {
public:
    // 0: both ways, 'h': only horizontally (fixed height 0), 'v': only vertically
    explicit Rectangle(char kind = 0) : kind_(kind) {}
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
private:
    char kind_;
};
inline Rectangle *HVSpace() { return new Rectangle(0); }
inline Rectangle *HSpace(int w = 0) { auto *r = new Rectangle('h'); if (w) r->fixed_width(w); return r; }
inline Rectangle *VSpace(int h = 0) { auto *r = new Rectangle('v'); if (h) r->fixed_height(h); return r; }

// ---------------------------------------------------------------------------------------------
class Text : public Area {
public:
    explicit Text(const std::string &contents = "", StdFrame frame = Frame_None);
    void contents(const std::string &t) { text_ = t; redraw(); }
    const std::string &contents() const { return text_; }
    void preparse(const std::string &p) { preparse_ = p; }
    void set_max(bool m) { setmax_ = m; }   // MUIA_Text_SetMax: the maximum is the default width
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;

protected:
    void draw_contents() override;
    std::string text_, preparse_;
    bool setmax_ = false;
    struct Line { std::string text; int align; bool bold; };
    std::vector<Line> parse() const;
};

class Button : public Text {
public:
    explicit Button(const std::string &label);
};

// ---------------------------------------------------------------------------------------------
class String : public Area {
public:
    explicit String(const std::string &contents = "", int maxlen = 0);
    const char *value() const { return in_->value(); }
    void value(const std::string &v) { in_->value(v.c_str()); }
    Fl_Input *input() { return in_; }
    std::function<void()> on_acknowledge;   // Return pressed
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    void resize(int X, int Y, int W, int H) override;

private:
    Fl_Input *in_;
};

// ---------------------------------------------------------------------------------------------
// Prop: a proportional gadget (a scroller) - a knob in a recessed track
class Prop : public Area {
public:
    explicit Prop(bool horiz);
    void set(int entries, int visible, int first);   // MUIA_Prop_Entries / Visible / First
    int first() const { return first_; }
    std::function<void(int first)> on_change;
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    int handle(int e) override;

protected:
    void draw_contents() override;
    void knob(int &kx, int &ky, int &kw, int &kh) const;
    bool horiz_;
    int entries_ = 1, visible_ = 1, first_ = 0;
    int grab_ = -1, grab_first_ = 0;
};

// Cycle: a button that steps through its entries (the right part shows the cycle mark)
class Cycle : public Area {
public:
    explicit Cycle(std::vector<std::string> entries, int active = 0);
    int active() const { return active_; }
    void active(int a) { active_ = std::clamp(a, 0, (int)entries_.size() - 1); redraw(); }
    std::function<void(int)> on_change;
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    int handle(int e) override;

protected:
    void draw_contents() override;
    std::vector<std::string> entries_;
    int active_;
};

// Checkmark: a box that is ticked or not
class Checkmark : public Area {
public:
    explicit Checkmark(bool on = false);
    bool on() const { return selected_; }
    void on(bool v) { selected(v); }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;

protected:
    void draw_contents() override;
};

// ImageButton: a button showing a picture drawn by a function (the theme's gadget images)
class ImageButton : public Area {
public:
    ImageButton(int w, int h, std::function<void(int x, int y, int w, int h, bool selected)> image);
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;

protected:
    void draw_contents() override;
    int iw_, ih_;
    std::function<void(int, int, int, int, bool)> image_;
};

// ---------------------------------------------------------------------------------------------
class Window : public Fl_Double_Window {
public:
    Window(const std::string &title, Area *root = nullptr);
    void root(Area *r);
    Area *root() const { return root_; }
    // the size from the root object's default size (MUIA_Window_Width/Height default)
    void fit();
    void resize(int X, int Y, int W, int H) override;
    void draw() override;

private:
    Area *root_ = nullptr;
    std::string title_;
};

int run();   // the Application's event loop

}  // namespace zune
