/*
    Copyright (C) 2002-2026, The AROS Development Team.
    Copyright (C) 1999, David Le Corfec.
    All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Lune classes Rambler is built on, on FLTK widgets:

      Area        every object: a frame (spec), a background, inner spacing, the minimum /
                  default / maximum size, a weight, the input mode, disabled / selected
      Group       lays out its children in a row or a column (or in columns), with spacing;
                  the free room is shared by the weights within the children's maximum
                  sizes - the layout of muimaster/classes/group.c
      Rectangle   an empty space (HVSpace, HSpace, VSpace)
      Text        a text with Lune's control sequences (\33c centre, \33r right, \33b bold ...)
      Button      a Text with the button frame that reports a release over it
      String      a single line text field in the string frame
      Window      a window with one root object, the window spacing around it
      Application the event loop

    Lune's preferences are the defaults of classes/configdata.c.
*/
#pragma once
#include "frame.h"
#include "imspec.h"
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

namespace lune {

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
    // the Scrollbars page: the arrows' place (0 after the bar, 1 around it, 2 before it;
    // SCROLLBAR_ARRANGEMENT_TOP / MIDDLE / BOTTOM) and the gadget type (kept, as the original)
    int scrollbar_arrangement = 0, scrollbar_type = 0;
    // the Listviews page: multi selection (0 with Shift, 1 always), refresh (0 linear,
    // 1 mixed), smooth scrolling and its speed, the lists' fonts (-1 / 0: the normal one),
    // the leading, the backgrounds (MUII patterns) of input and read-only lists and of the
    // cursor, the selected lines and the selected cursor line
    int list_multi = 0, list_refresh = 1, list_smoothval = 0, list_leading = 1;
    bool list_smoothed = false;
    int list_font_family = -1, list_font_size = 0, fixed_font_size = 0;
    // (image specs, imspec.h; configdata "0:128" for the lists - white here)
    std::string list_back = "0:130", readlist_back = "0:130";
    std::string list_cursor = "0:131", list_select = "0:135", list_selcur = "0:138";
    Fl_Font list_font = FL_HELVETICA;
    // the Strings page (pens, MPEN_*): the background and text of an inactive and of the
    // active string, the cursor, the marked text's background and text (configdata.c
    // "2:m2", "m5", "2:m1", "m5", "m7", "m6", "m0")
    // the Buttons page: the background and the pressed background of buttons (MUII
    // patterns), the button font (-1 / 0: the normal one), the radio buttons' spacing
    std::string button_back = "0:128", selected_back = "0:131";   // (image specs)
    // the other backgrounds (image specs, configdata.c): windows, requesters, framed groups,
    // registers, page groups, pop-ups, text fields, scrollers (the half shadow here), sliders
    // and their knobs; the colour of the active object (Navigation page)
    // the Groups page: the title's position (0 above the frame, 1 centred on its top line),
    // colour (0 standard, 1 hilite, 2 3D, 3 outline) and font (-1 / 0: the normal bold one);
    // registers truncating their titles (kept)
    int group_title_position = 1, group_title_color = 1, title_font_family = -1, title_font_size = 0;
    bool register_truncate_titles = false;
    Fl_Font title_font = FL_HELVETICA_BOLD;
    int title_size() const { return title_font_size > 0 ? title_font_size : font_size; }
    // the Windows page: window positions (0 forgotten on exit, 1 remembered on exit, 2 saved
    // on exit), refresh and redraw (kept), the extra buttons (kept), the tiny and big fonts
    int window_positions = 0, window_refresh = 1, window_redraw = 0, window_buttons = 0;
    int tiny_font_size = 0, big_font_size = 0;
    std::string window_back = "0:128", requester_back = "0:137", group_back = "0:128", register_back = "0:128",
                page_back = "0:128", popup_back = "0:128", text_back = "0:128", prop_back = "2:m3",
                slider_back = "0:128", slider_knob = "0:128", active_object_color = "m0";
    int button_font_family = -1, button_font_size = 0;
    int radio_hspacing = 4, radio_vspacing = 1;
    Fl_Font button_font = FL_HELVETICA, button_font_bold = FL_HELVETICA_BOLD;
    int button_size() const { return button_font_size > 0 ? button_font_size : font_size; }
    std::string string_bg = "m2", string_text = "m5", string_active_bg = "m1", string_active_text = "m5",
                string_cursor = "m7", string_marked_bg = "m6", string_marked_text = "m0";   // (pen specs)
    int list_size() const { return list_font_size > 0 ? list_font_size : font_size; }
    std::string custom_frame[16];   // the Frames page: the custom frames' frame.config files
    int font_size = 13;          // the screen font (topaz-like 8 pt is too small on today's screens)
    // the Font preferences: the screen font and the fixed-width (system) font by name - a
    // font of the system ("DejaVu Sans"); "" - font_family / Courier
    std::string font_name, fixed_font_name;
    Fl_Font font = FL_HELVETICA, font_bold = FL_HELVETICA_BOLD, font_fixed = FL_COURIER;
};
// a font of the system by its family name ("DejaVu Sans"), plain or bold: an FLTK font
// number of its own (made the first time it is asked for)
Fl_Font named_font(const std::string &family, bool bold = false);
// the font families of the system (for the font requester), sorted
std::vector<std::string> font_families();
// the font requester (ASL's FontRequest, as Popasl shows it): a family and a size; false -
// cancelled
bool font_request(const std::string &title, std::string &family, int &size);
Prefs &prefs();
// ~/.config/rambler/lune.prefs (ENVARC:) and $XDG_RUNTIME_DIR/rambler/lune.prefs (ENV:): read
// at the start (ENV: first), written by the settings editor (Use: ENV:, Save: both);
// apply() puts them into effect in every window at once
void load_prefs();
void save_prefs(bool archive = true);
std::string env_dir();          // the session's settings in use (ENV:)
std::string env_prefs_path();
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
    void background(int pen) { bg_pen_ = pen; bg_ref_ = nullptr; }
    // a background of the preferences (an image spec, e.g. &Prefs::text_back)
    void background(std::string Prefs::*ref) { bg_ref_ = ref; }
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
    // object has the dashed outline of _lune_focus_new around it
    virtual bool accepts_focus() const { return input_ != Input_None && !disabled_; }

protected:
    virtual void draw_contents() {}   // MUIM_Draw of the subclass
    void draw_active_outline();
    void focus_changed();
    virtual void draw_background(int X, int Y, int W, int H);
    // no background of its own: the parent's shows through (MUIM_DrawParentBackground)
    void draw_parent_background(int X, int Y, int W, int H);
    std::string Prefs::*bg_ref_ = nullptr;
    StdFrame std_ = Frame_None;
    FrameSpec own_;
    int bg_pen_ = -1;   // (-1: none of its own - the parent's)
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
    std::vector<Area *> areas() const;   // the children that are Lune objects and shown

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
    bool button_font_ = false;   // MUIV_Font_Button (a button's text)
    Fl_Font font(bool bold) const;
    int font_size() const;
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
    void center(bool c) { center_ = c; }   // MUIV_String_Format_Center (shown so while inactive)

protected:
    void draw_background(int X, int Y, int W, int H) override;

private:
    Fl_Input *in_;
    bool center_ = false;
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
    void step(int n);   // MUIM_Prop_Increase / Decrease (n < 0)

protected:
    void draw_contents() override;
    void knob(int &kx, int &ky, int &kw, int &kh) const;
    bool horiz_;
    int entries_ = 1, visible_ = 1, first_ = 0;
    int grab_ = -1, grab_first_ = 0;
};

// Arrow (MUII_ArrowUp / Down / Left / Right in an image button frame): pressed it acts at
// once, then again and again while held (MUIA_Timer)
class Arrow : public Area {
public:
    enum Dir { Up, Down, Left, Right };
    explicit Arrow(Dir d, int size = 14);
    std::function<void()> on_step;
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    int handle(int e) override;
    static void draw_arrow(Dir d, int x, int y, int w, int h);   // the image itself

protected:
    void draw_contents() override;
    static void repeat(void *);
    Dir dir_;
    int size_;
};

// Scrollbar (classes/scrollbar.c): a prop gadget with its two arrows, arranged as the
// preferences say (Scrollbars page: arrows after it, around it or before it), or as set
class Scrollbar : public Group {
public:
    enum Type { Default = -1, Top, Sym, Bottom };   // MUIV_Scrollbar_Type_Top / Sym / Bottom
    explicit Scrollbar(bool horiz, Type type = Default);
    void set(int entries, int visible, int first) { prop_->set(entries, visible, first); }
    int first() const { return prop_->first(); }
    std::function<void(int first)> on_change;
    int incdec = 1;   // MUIA_Scrollbar_IncDecSize
    void resize(int X, int Y, int W, int H) override;

private:
    void arrange();
    Prop *prop_;
    Arrow *up_, *down_;
    Type type_;
    int arranged_ = -1;
};

// Cycle: a button that steps through its entries (the right part shows the cycle mark)
class Cycle : public Area {
public:
    bool accepts_focus() const override { return !disabled_; }
    explicit Cycle(std::vector<std::string> entries, int active = 0);
    int active() const { return active_; }
    void active(int a) { active_ = std::clamp(a, 0, (int)entries_.size() - 1); redraw(); }
    // MUIA_Cycle_Entries set again (the entries as they are: not looked up in the catalog)
    void entries(std::vector<std::string> e, int active = 0) {
        entries_ = std::move(e);
        if (entries_.empty()) entries_.push_back("");
        active_ = std::clamp(active, 0, (int)entries_.size() - 1);
        redraw();
    }
    int count() const { return (int)entries_.size(); }
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
    // MUIA_List_First at the end (MUIV_List_Jump_Bottom): the last lines in sight
    void jump_bottom() { top_ = std::max(0, (int)lines_.size() - visible_lines()); layout_prop(); redraw(); }
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
    int line_h() const { return prefs().list_size() + 2 + prefs().list_leading; }
    void draw_background(int X, int Y, int W, int H) override;
    int title_h() const { return titles_.empty() ? 0 : line_h() + 2; }
    int visible_lines() const { return std::max(1, (mheight() - title_h()) / line_h()); }
    std::vector<std::string> titles_;
    std::vector<bool> right_;
    std::vector<int> colw_;
    void measure_columns();
    void layout_prop();
    std::vector<Line> lines_;
    int active_ = -1, top_ = 0;
    class Scrollbar *prop_ = nullptr;
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
// Calendar (classes/zune/calendar): a month as a grid of days under the names of the week's
// days (the week starting with the locale's first day); a click or the cursor keys pick a
// day
class Calendar : public Area {
public:
    Calendar();
    void date(int year, int month, int day);   // month 1 - 12
    int year() const { return year_; }
    int month() const { return month_; }
    int day() const { return day_; }
    std::function<void(int day)> on_change;
    bool accepts_focus() const override { return true; }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    int handle(int e) override;

protected:
    void draw_contents() override;
    int year_ = 2000, month_ = 1, day_ = 1;
    int first_wday_ = 1;   // the locale's first day of the week (0 Sunday, 1 Monday)
    int cell(int &cw, int &ch, int &x0, int &y0) const;   // the first day's column
};

// Clock (classes/zune/clock): an analog clock; it goes with the system's time unless frozen
// (the time being edited), then it shows the time set
class Clock : public Area {
public:
    Clock();
    ~Clock() override;
    void time(int h, int m, int s);
    int hour() const { return h_; }
    int minute() const { return m_; }
    int second() const { return s_; }
    void frozen(bool f);
    bool frozen() const { return frozen_; }
    std::function<void()> on_tick;   // (each second, when it goes)
    void seconds(bool s) { seconds_ = s; redraw(); }   // the seconds hand shown
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;

protected:
    void draw_contents() override;
    static void tick(void *d);
    int h_ = 0, m_ = 0, s_ = 0;
    bool frozen_ = false, seconds_ = true;
};

// ---------------------------------------------------------------------------------------------
// Popimage (classes/popimage.c, imageadjust.c, penadjust.c): an image spec shown in a button;
// pressed, a window to change it - the pattern, a colour (a MUI pen or red, green and blue),
// a picture or a gradient for a background; only a colour for a pen (a Poppen). OK takes
// the new spec (on_change), Cancel leaves it
class Popimage : public Area {
public:
    enum Kind { Background, Pen };
    explicit Popimage(const std::string &spec, Kind kind = Background, const std::string &title = "");
    const std::string &spec() const { return spec_; }
    void spec(const std::string &s) { spec_ = s; redraw(); }
    std::function<void(const std::string &)> on_change;
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    ~Popimage() override;

protected:
    void draw_contents() override;
    void open();
    std::string spec_, title_;
    Kind kind_;
    class Window *win_ = nullptr;
};
// the file requester the image adjust window uses for a picture (set by the program)
extern std::function<bool(const std::string &title, std::string &path)> file_request_hook;

// Menustrip, Menu, Menuitem (classes/menustrip.c, menu.c, menuitem.c): the window's menus,
// which the window manager shows in the screen's bar while the menu button is held over
// the window (Intuition's menus). An item with children is a menu (or a sub-menu); a
// check mark item (Menu_Toggle) flips by itself when picked, the items of one exclude
// group (MUIA_Menuitem_Exclude) are checked one at a time; key: the Super+key shortcut
enum MenuFlags { Menu_Disabled = 1, Menu_Toggle = 2, Menu_Checked = 4, Menu_Line = 8 };
struct MenuEntry {
    std::string label;
    int id = 0, flags = 0;
    char key = 0;
    int exclude = 0;
    std::vector<MenuEntry> children;
};

class Window : public Fl_Double_Window {
public:
    // MUIA_Window_Menustrip; on_menu: an item picked (MUIA_Menuitem_Trigger), by its id
    void menustrip(std::vector<MenuEntry> m);
    std::function<void(int)> on_menu;
    bool menu_checked(int id);
    void menu_checked(int id, bool on);
    void menu_disabled(int id, bool off);
    void menu_pick(int id);   // (as if the item were picked)
    Window(const std::string &title, Area *root = nullptr);
    ~Window() override;
    void root(Area *r);
    Area *root() const { return root_; }
    // MUIA_Window_ID: a window with an ID opens where it was (the Windows page's positions)
    void id(const std::string &i) { id_ = i; }
    void show() override;
    using Fl_Double_Window::show;
    void hide() override;
    // a requester's window has the requester background (MUII_RequesterBack)
    void requester(bool r) { requester_ = r; }
    const std::string &background_spec() const;
    // the size from the root object's default size (MUIA_Window_Width/Height default)
    void fit();
    void resize(int X, int Y, int W, int H) override;
    void draw() override;
    int handle(int e) override;

private:
    std::vector<MenuEntry> menus_;
    MenuEntry *menu_entry(int id, std::vector<MenuEntry> *in = nullptr);
    void publish_menu();
    bool menu_key(int key);
    bool requester_ = false;
    std::string id_;
    Area *root_ = nullptr;
    std::string title_;
    std::unique_ptr<class Fl_Widget_Tracker> last_focus_;   // the active object when the window was left
};

int run();   // the Application's event loop

}  // namespace lune
