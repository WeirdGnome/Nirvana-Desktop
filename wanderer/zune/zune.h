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
#include "catalog.h"

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace zune {

constexpr int MAXMAX = 10000;   // MUI_MAXMAX

// The keys of the user interface (MUIKEY_*: the Navigation page's keyboard control), each set
// by a key spec as the original's ("-repeat shift tab": qualifiers, then the key)
enum MuiKey {
    MUIKEY_NONE = -1, MUIKEY_PRESS, MUIKEY_TOGGLE, MUIKEY_UP, MUIKEY_DOWN, MUIKEY_PAGEUP, MUIKEY_PAGEDOWN,
    MUIKEY_TOP, MUIKEY_BOTTOM, MUIKEY_LEFT, MUIKEY_RIGHT, MUIKEY_WORDLEFT, MUIKEY_WORDRIGHT, MUIKEY_LINESTART,
    MUIKEY_LINEEND, MUIKEY_GADGET_NEXT, MUIKEY_GADGET_PREV, MUIKEY_GADGET_OFF, MUIKEY_WINDOW_CLOSE,
    MUIKEY_WINDOW_NEXT, MUIKEY_WINDOW_PREV, MUIKEY_HELP, MUIKEY_POPUP, MUIKEY_COUNT
};
extern const char *const KEY_DEFAULTS[MUIKEY_COUNT];   // configdata.c
extern const char *const KEY_LABELS[MUIKEY_COUNT];     // the Navigation page's labels
// the keyboard event being handled as a MUIKEY (MUIKEY_NONE: none of them)
MuiKey muikey();
bool key_spec_valid(const std::string &spec);

// configdata.c defaults
struct Prefs {
    // the screen's palette (MUI pens come from it) and the frames of buttons and groups
    unsigned background = 0xAAAAAA, shadow = 0x000000, shine = 0xFFFFFF, fill = 0x6688BB;
    std::string frame[Frame_Count];   // the frames of the standard kinds ("" - the built-in one)
    int font_family = 0;         // 0 sans, 1 serif, 2 fixed
    int group_hspacing = 6, group_vspacing = 3;
    int window_left = 4, window_right = 4, window_top = 3, window_bottom = 3;
    // the cycles' pop-up menu (Cycles page): below the gadget or on the active entry, from how
    // many entries, the active entry recessed
    int cycle_menu_position = 0, cycle_menu_min = 2;
    bool cycle_menu_recessed = false;
    std::string keys[MUIKEY_COUNT];   // "" - the default (KEY_DEFAULTS)
    // drag & drop (Navigation page): which buttons drag, after how many pixels it starts
    bool drag_left = true, drag_middle = false, drag_autostart = true;
    std::string drag_left_modifier, drag_middle_modifier;   // (kept, as the original does)
    int drag_autostart_length = 3;
    int drag_look = 0;      // DNDLook: 0 always solid, 1 ghosted on box, 2 ghosted outside, 3 always ghosted
    int balancing_look = 0; // 0 show frames, 1 show objects
    // bubble help (System page): the delays before the first bubble and between bubbles, in
    // tenths of a second (configdata.c 30 and 10)
    int bubble_first = 30, bubble_next = 10;
    std::string custom_frame[16];   // the Frames page: the custom frames' frame.config files
    int font_size = 13;          // the screen font (topaz-like 8 pt is too small on today's screens)
    Fl_Font font = FL_HELVETICA, font_bold = FL_HELVETICA_BOLD, font_fixed = FL_COURIER;
};
Prefs &prefs();
// ~/.config/wanderer/zune.prefs: read at the start, written by the settings editor;
// apply() puts them into effect in every window at once
void load_prefs();
void save_prefs();
void apply_prefs();

enum InputMode { Input_None, Input_RelVerify, Input_Immediate, Input_Toggle };

// ---------------------------------------------------------------------------------------------
class Area : public Fl_Group {
public:
    Area(StdFrame frame = Frame_None);
    // the frame: a standard one or a spec ("202211")
    // (a standard frame follows the preferences: changed there, it changes here)
    void frame(StdFrame f) { std_ = f; }
    void frame(const std::string &spec) { own_ = FrameSpec::parse(spec); std_ = Frame_Count; }
    const FrameSpec &frame_spec() const { return std_ < Frame_Count ? std_frame(std_) : own_; }
    // the background: a pen (MPEN_BACKGROUND by default) or none (the parent's shows through)
    void background(int pen) { bg_pen_ = pen; }
    void inner(int l, int r, int t, int b) { il_ = l; ir_ = r; it_ = t; ib_ = b; inner_set_ = true; }
    void weight(int w) { weight_ = w; hweight_ = vweight_ = -1; }
    int weight() const { return weight_; }
    // MUIA_HorizWeight / MUIA_VertWeight: the weight along one direction only
    void horiz_weight(int w) { hweight_ = w; }
    void vert_weight(int w) { vweight_ = w; }
    int weight(bool horiz) const { int w = horiz ? hweight_ : vweight_; return w >= 0 ? w : weight_; }
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
    // the keyboard's active object (MUIA_CycleChain): Tab moves it, Space presses it; the
    // object has the dashed outline of _zune_focus_new around it
    virtual bool accepts_focus() const { return input_ != Input_None && !disabled_; }

protected:
    virtual void draw_contents() {}   // MUIM_Draw of the subclass
    void draw_active_outline();
    void focus_changed();
    virtual void draw_background(int X, int Y, int W, int H);
    StdFrame std_ = Frame_None;
    FrameSpec own_;
    int bg_pen_ = MPEN_BACKGROUND;
    int il_ = 0, ir_ = 0, it_ = 0, ib_ = 0;
    bool inner_set_ = false;
    int weight_ = 100, hweight_ = -1, vweight_ = -1, fixw_ = 0, fixh_ = 0;
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
    void frame_title(const std::string &t) { title_ = tr_text(t); }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    void resize(int X, int Y, int W, int H) override;
    bool horizontal() const { return dir_ == Horizontal; }
    // a Balance being dragged with "Show frames": the children as outlines only (with crosses
    // when they can change size), not drawn themselves
    void sketch(bool on) { sketch_ = on; redraw(); }
    void relayout() { layout(); redraw(); }
    void layout();
    void draw() override;
    std::vector<Area *> areas() const;   // the children that are Zune objects and shown

protected:
    Dir dir_;
    int hs_, vs_, columns_ = 0;   // the spacing (-1: the preferences')
public:
    int hsp() const { return hs_ < 0 ? prefs().group_hspacing : hs_; }
    int vsp() const { return vs_ < 0 ? prefs().group_vspacing : vs_; }
protected:
    void draw_sketch();
    bool sketch_ = false;
    bool same_w_ = false, same_h_ = false;
    std::string title_;
    int title_h() const;
    void layout_line(bool horiz);
    void layout_columns();
};

// Register (classes/register.c): a page group with a row of tabs on top; only the active
// page shows. The pages are the children, one for each title. Without titles: a page group
// (MUIA_Group_PageMode) - the pages without tabs or frame.
class Register : public Group {
public:
    explicit Register(const std::vector<std::string> &titles);
    int active() const { return active_; }
    void active(int i);
    std::function<void(int)> on_change;
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    void resize(int X, int Y, int W, int H) override;
    void draw() override;
    int handle(int e) override;
    void end();

private:
    std::vector<std::string> titles_;
    int active_ = 0;
    int tab_h() const;
    void hard_coord();
    void tab_x(int i, int &x1, int &x2) const;
    void draw_tab(int i);
};

inline Group *VGroup() { return new Group(Group::Vertical); }
inline Group *HGroup() { return new Group(Group::Horizontal); }

// ---------------------------------------------------------------------------------------------
class Rectangle : public Area {
public:
    // 0: both ways, 'h': only horizontally (fixed height 0), 'v': only vertically,
    // 'H': a horizontal bar (MUIA_Rectangle_HBar: a groove across, 2 pixels high)
    explicit Rectangle(char kind = 0) : kind_(kind) {}
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
protected:
    void draw_contents() override;
private:
    char kind_;
};
inline Rectangle *HVSpace() { return new Rectangle(0); }
inline Rectangle *HBar() { return new Rectangle('H'); }
// MUIO_HSpace: no vertical weight, a fixed width (0: free); MUIO_VSpace the other way round
inline Rectangle *HSpace(int w = 0) { auto *r = new Rectangle('h'); r->vert_weight(0); if (w) r->fixed_width(w); return r; }
inline Rectangle *VSpace(int h = 0) { auto *r = new Rectangle('v'); r->horiz_weight(0); if (h) r->fixed_height(h); return r; }

// Balance (classes/balance.c): a thin bar between objects of a group; dragging it shares
// the room between them anew (their weights) - the two next to it with Shift held, else all
// on each side. While dragging the group shows its objects or only their frames (the
// balancing look of the preferences)
class Balance : public Area {
public:
    Balance();
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    int handle(int e) override;

protected:
    void draw_contents() override;

private:
    bool horiz() const;
    void move(int pos);
    int state_ = 0;   // NOT_CLICKED, CLICKED, SHIFT_CLICKED
    std::vector<Area *> before_, after_;
    long lsum_ = 0, rsum_ = 0;
    std::vector<int> lw_, rw_;   // the weights when the drag started
    int first_bound_ = 0, second_bound_ = 0;
};

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
    bool accepts_focus() const override { return !disabled_; }
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
    // the pop-up of the entries (MakePopupWin): open while the button is held
    class CyclePopup *popup_ = nullptr;
    bool open_popup();     // MakePopupWin (false: too few entries)
    void close_popup();
    bool key_popup_ = false;   // opened by the keyboard (MUIKEY_PRESS): the keys move in it

public:
    ~Cycle() override { close_popup(); }
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

// List: lines of text in the read list frame - a name on the left, an optional second
// column on the right (drawers: "Drawer" in the highlight pen); one line active; a
// scroller at the right
class List : public Area {
public:
    bool accepts_focus() const override { return !disabled_; }
    struct Line { std::string text, info; bool highlight = false; std::vector<std::string> cols; };
    // columns (MUIA_List_Format "BAR,P=\33r BAR,..." with MUIA_List_Title): the titles, which
    // are right aligned; a line's cells are its cols; a title clicked: on_title
    void columns(const std::vector<std::string> &titles, const std::vector<bool> &right = {});
    std::function<void(int)> on_title;
    List();
    void clear() { lines_.clear(); active_ = -1; top_ = 0; redraw(); }
    void add(Line l) { lines_.push_back(std::move(l)); }
    void done() { top_ = 0; measure_columns(); layout_prop(); redraw(); }   // after adding
    const std::vector<Line> &lines() const { return lines_; }
    int active() const { return active_; }
    void active(int a);
    std::function<void(int)> on_active;      // a line clicked
    std::function<void(int)> on_double;      // a line double clicked
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    void resize(int X, int Y, int W, int H) override;
    int handle(int e) override;

protected:
    void draw_contents() override;
    int line_h() const { return prefs().font_size + 3; }
    int title_h() const { return titles_.empty() ? 0 : line_h() + 2; }
    int visible_lines() const { return std::max(1, (mheight() - title_h()) / line_h()); }
    std::vector<std::string> titles_;
    std::vector<bool> right_;
    std::vector<int> colw_;
    void measure_columns();
    void layout_prop();
    std::vector<Line> lines_;
    int active_ = -1, top_ = 0;
    class Prop *prop_ = nullptr;
};

// Gauge: a bar filled in the fill pen to the current value, a text in its middle
// Slider (classes/slider.c, numeric.c): a value between min and max, a knob with the number
// in it inside a recessed frame; dragging the knob, clicks beside it and the cursor keys
// change it
class Slider : public Area {
public:
    bool accepts_focus() const override { return !disabled_; }
    Slider(int min, int max, int value, bool horiz = true);   // horiz false: MUIA_Slider_Horiz FALSE
    int value() const { return value_; }
    // MUIM_Numeric_Stringify / MUIA_Numeric_Format: the number as the knob shows it
    std::function<std::string(int)> format;
    std::string text(int v) const { return format ? format(v) : std::to_string(v); }
    void value(int v);
    std::function<void(int)> on_change;
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    int handle(int e) override;

protected:
    void draw_contents() override;
    int knob_w() const, knob_h() const;
    int knob_pos() const;   // the knob's offset along the slider (knob_offset)
    int scale_length() const;
    int min_, max_, value_;
    bool horiz_ = true;
    bool dragging_ = false;
    int grab_dx_ = 0;
};

// Scale (classes/scale.c): the percent marks under a gauge
class Scale : public Area {
public:
    Scale();
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;

protected:
    void draw_contents() override;
};

class Gauge : public Area {
public:
    Gauge();
    void set(long long current, long long max) { cur_ = current; max_ = max > 0 ? max : 1; redraw(); }
    void info(const std::string &t) { info_ = t; redraw(); }
    void vertical(bool v) { horiz_ = !v; }   // MUIA_Gauge_Horiz FALSE: it fills from the bottom
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;

protected:
    void draw_contents() override;
    bool horiz_ = true;
    long long cur_ = 0, max_ = 1;
    std::string info_;
};

// ImageButton: a button showing a picture drawn by a function (the theme's gadget images)
// The pop-up images (imspec_vector.c: MUII_PopUp, MUII_PopFile, MUII_PopDrawer) on an image
// button: the button beside a string that opens a requester (PopButton())
enum PopImage { Pop_Up, Pop_File, Pop_Drawer };
class ImageButton;
ImageButton *PopButton(PopImage which);

class ImageButton : public Area {
public:
    // a frameless image (an icon shown in a window) is no stop of the keyboard
    bool accepts_focus() const override { return Area::accepts_focus() && frame_spec().type != FST_NONE; }
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
    ~Window() override;
    void root(Area *r);
    Area *root() const { return root_; }
    // the size from the root object's default size (MUIA_Window_Width/Height default)
    void fit();
    void resize(int X, int Y, int W, int H) override;
    void draw() override;
    int handle(int e) override;

private:
    Area *root_ = nullptr;
    std::string title_;
    std::unique_ptr<class Fl_Widget_Tracker> last_focus_;   // the active object when the window was left
};

int run();   // the Application's event loop

}  // namespace zune
