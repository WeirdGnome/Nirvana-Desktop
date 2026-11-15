/*
    Copyright (C) 2002-2026, The AROS Development Team.
    Copyright (C) 1999, David Le Corfec.
    All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Lune classes on FLTK (lune.h). The layout follows the rules of
    muimaster/classes/group.c: every object says its minimum, default and maximum size;
    a group sums them along its direction (with spacing) and takes the largest minimum and
    the smallest maximum across; the room above the minimum is shared by weight, an object
    never growing above its maximum.
*/
#include "lune.h"
#include <FL/Fl_Menu_Window.H>
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <X11/Xatom.h>

#include <cstring>

#include <algorithm>
#include <sstream>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace lune {

Prefs &prefs() {
    static Prefs p;
    return p;
}

// ENVARC: the saved settings; ENV: the ones in use in this session (what Use writes; gone
// with the session, as ENV: is a RAM disk)
static std::string prefs_path() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/rambler/lune.prefs";
}

std::string env_dir() {
    const char *r = getenv("XDG_RUNTIME_DIR");
    if (r && *r) return std::string(r) + "/rambler";
    return "/tmp/rambler-" + std::to_string(getuid());
}

std::string env_prefs_path() { return env_dir() + "/lune.prefs"; }

// the image and pen specs of the preferences, by their names in the file
static const std::pair<const char *, std::string Prefs::*> SPEC_PREFS[] = {
    {"window_back", &Prefs::window_back}, {"requester_back", &Prefs::requester_back},
    {"group_back", &Prefs::group_back}, {"register_back", &Prefs::register_back}, {"page_back", &Prefs::page_back},
    {"popup_back", &Prefs::popup_back}, {"text_back", &Prefs::text_back}, {"prop_back", &Prefs::prop_back},
    {"slider_back", &Prefs::slider_back}, {"slider_knob", &Prefs::slider_knob},
    {"active_object_color", &Prefs::active_object_color}, {"string_bg", &Prefs::string_bg},
    {"string_text", &Prefs::string_text}, {"string_active_bg", &Prefs::string_active_bg},
    {"string_active_text", &Prefs::string_active_text}, {"string_cursor", &Prefs::string_cursor},
    {"string_marked_bg", &Prefs::string_marked_bg}, {"string_marked_text", &Prefs::string_marked_text},
    {"font_name", &Prefs::font_name}, {"fixed_font_name", &Prefs::fixed_font_name}};

static const std::pair<const char *, int Prefs::*> INT_PREFS[] = {
    {"group_title_position", &Prefs::group_title_position}, {"group_title_color", &Prefs::group_title_color},
    {"title_font_family", &Prefs::title_font_family}, {"title_font_size", &Prefs::title_font_size},
    {"window_positions", &Prefs::window_positions}, {"window_refresh", &Prefs::window_refresh},
    {"window_redraw", &Prefs::window_redraw}, {"window_buttons", &Prefs::window_buttons},
    {"tiny_font_size", &Prefs::tiny_font_size}, {"big_font_size", &Prefs::big_font_size}};

static int Prefs::*int_pref(const std::string &k) {
    for (auto &[name, m] : INT_PREFS) if (k == name) return m;
    return nullptr;
}

static std::string Prefs::*spec_pref(const std::string &k) {
    for (auto &[name, m] : SPEC_PREFS) if (k == name) return m;
    return nullptr;
}

void load_prefs() {
    Prefs &p = prefs();
    FILE *f = fopen(env_prefs_path().c_str(), "r");
    if (!f) f = fopen(prefs_path().c_str(), "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            std::string l(line);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            auto eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string k = l.substr(0, eq), v = l.substr(eq + 1);
            if (k == "background") p.background = (unsigned)strtoul(v.c_str(), nullptr, 16);
            else if (k == "shadow") p.shadow = (unsigned)strtoul(v.c_str(), nullptr, 16);
            else if (k == "shine") p.shine = (unsigned)strtoul(v.c_str(), nullptr, 16);
            else if (k == "fill") p.fill = (unsigned)strtoul(v.c_str(), nullptr, 16);
            else if (k == "button_frame" && v.size() == 6) p.frame[Frame_Button] = p.frame[Frame_ImageButton] = v;
            else if (k == "group_frame" && v.size() == 6) p.frame[Frame_Group] = v;
            else if (k.rfind("frame_", 0) == 0 && v.size() == 6) {
                int i = atoi(k.c_str() + 6);
                if (i > 0 && i < Frame_Count) p.frame[i] = v;
            }
            else if (k == "cycle_menu_position") p.cycle_menu_position = std::clamp(atoi(v.c_str()), 0, 1);
            else if (k == "cycle_menu_min") p.cycle_menu_min = std::clamp(atoi(v.c_str()), 2, 100);
            else if (k == "cycle_menu_recessed") p.cycle_menu_recessed = atoi(v.c_str());
            else if (k == "group_hspacing") p.group_hspacing = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "group_vspacing") p.group_vspacing = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "window_left") p.window_left = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "window_right") p.window_right = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "window_top") p.window_top = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "window_bottom") p.window_bottom = std::clamp(atoi(v.c_str()), 0, 20);
            else if (k == "font_size") p.font_size = std::clamp(atoi(v.c_str()), 9, 24);
            else if (k == "font_family") p.font_family = std::clamp(atoi(v.c_str()), 0, 2);
            else if (k.rfind("key_", 0) == 0) {
                int i = atoi(k.c_str() + 4);
                if (i >= 0 && i < MUIKEY_COUNT && key_spec_valid(v)) p.keys[i] = v;
            }
            else if (k == "drag_left") p.drag_left = atoi(v.c_str());
            else if (k == "drag_middle") p.drag_middle = atoi(v.c_str());
            else if (k == "drag_left_modifier") p.drag_left_modifier = v;
            else if (k == "drag_middle_modifier") p.drag_middle_modifier = v;
            else if (k == "drag_autostart") p.drag_autostart = atoi(v.c_str());
            else if (k == "drag_autostart_length") p.drag_autostart_length = std::clamp(atoi(v.c_str()), 1, 20);
            else if (k == "drag_look") p.drag_look = std::clamp(atoi(v.c_str()), 0, 3);
            else if (k.rfind("custom_frame_", 0) == 0) {
                int i = atoi(k.c_str() + 13) - 1;
                if (i >= 0 && i < 16) p.custom_frame[i] = v;
            }
            else if (k == "scrollbar_arrangement") p.scrollbar_arrangement = std::clamp(atoi(v.c_str()), 0, 2);
            else if (k == "scrollbar_type") p.scrollbar_type = std::clamp(atoi(v.c_str()), 0, 2);
            else if (k == "list_multi") p.list_multi = std::clamp(atoi(v.c_str()), 0, 1);
            else if (k == "list_refresh") p.list_refresh = std::clamp(atoi(v.c_str()), 0, 1);
            else if (k == "list_smoothed") p.list_smoothed = atoi(v.c_str());
            else if (k == "list_smoothval") p.list_smoothval = std::clamp(atoi(v.c_str()), 0, 9);
            else if (k == "list_leading") p.list_leading = std::clamp(atoi(v.c_str()), 0, 9);
            else if (k == "list_font_family") p.list_font_family = std::clamp(atoi(v.c_str()), -1, 2);
            else if (k == "list_font_size") p.list_font_size = std::clamp(atoi(v.c_str()), 0, 24);
            else if (k == "fixed_font_size") p.fixed_font_size = std::clamp(atoi(v.c_str()), 0, 24);
            else if (k == "list_back") p.list_back = v;
            else if (k == "readlist_back") p.readlist_back = v;
            else if (k == "list_cursor") p.list_cursor = v;
            else if (k == "list_select") p.list_select = v;
            else if (k == "list_selcur") p.list_selcur = v;
            else if (std::string Prefs::*m = spec_pref(k)) p.*m = v;
            else if (int Prefs::*m = int_pref(k)) p.*m = atoi(v.c_str());
            else if (k == "register_truncate_titles") p.register_truncate_titles = atoi(v.c_str());
            else if (k == "bubble_first") p.bubble_first = std::clamp(atoi(v.c_str()), 0, 50);
            else if (k == "bubble_next") p.bubble_next = std::clamp(atoi(v.c_str()), 0, 50);
            else if (k == "balancing_look") p.balancing_look = std::clamp(atoi(v.c_str()), 0, 1);
        }
        fclose(f);
    }
    apply_prefs();
}

static void write_prefs(const std::string &path);

void save_prefs(bool archive) {
    write_prefs(env_prefs_path());
    if (archive) write_prefs(prefs_path());
}

static void write_prefs(const std::string &path) {
    Prefs &p = prefs();
    std::string dir = path.substr(0, path.rfind('/'));
    std::string cmd;
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
    FILE *f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "background=%06X\nshadow=%06X\nshine=%06X\nfill=%06X\nfont_size=%d\nfont_family=%d\n"
               "group_hspacing=%d\ngroup_vspacing=%d\nwindow_left=%d\nwindow_right=%d\nwindow_top=%d\nwindow_bottom=%d\n",
            p.background, p.shadow, p.shine, p.fill, p.font_size, p.font_family, p.group_hspacing, p.group_vspacing,
            p.window_left, p.window_right, p.window_top, p.window_bottom);
    fprintf(f, "cycle_menu_position=%d\ncycle_menu_min=%d\ncycle_menu_recessed=%d\n", p.cycle_menu_position,
            p.cycle_menu_min, (int)p.cycle_menu_recessed);
    for (int i = 1; i < Frame_Count; i++)
        if (!p.frame[i].empty()) fprintf(f, "frame_%d=%s\n", i, p.frame[i].c_str());
    for (int i = 0; i < MUIKEY_COUNT; i++)
        if (!p.keys[i].empty() && p.keys[i] != KEY_DEFAULTS[i]) fprintf(f, "key_%d=%s\n", i, p.keys[i].c_str());
    fprintf(f, "drag_left=%d\ndrag_middle=%d\ndrag_autostart=%d\ndrag_autostart_length=%d\ndrag_look=%d\nbalancing_look=%d\n",
            (int)p.drag_left, (int)p.drag_middle, (int)p.drag_autostart, p.drag_autostart_length, p.drag_look,
            p.balancing_look);
    for (int i = 0; i < 16; i++)
        if (!p.custom_frame[i].empty()) fprintf(f, "custom_frame_%d=%s\n", i + 1, p.custom_frame[i].c_str());
    fprintf(f, "list_multi=%d\nlist_refresh=%d\nlist_smoothed=%d\nlist_smoothval=%d\nlist_leading=%d\n"
               "list_font_family=%d\nlist_font_size=%d\nfixed_font_size=%d\nlist_back=%s\nreadlist_back=%s\n"
               "list_cursor=%s\nlist_select=%s\nlist_selcur=%s\n",
            p.list_multi, p.list_refresh, (int)p.list_smoothed, p.list_smoothval, p.list_leading, p.list_font_family,
            p.list_font_size, p.fixed_font_size, p.list_back.c_str(), p.readlist_back.c_str(), p.list_cursor.c_str(),
            p.list_select.c_str(), p.list_selcur.c_str());
    for (auto &[name, m] : SPEC_PREFS) fprintf(f, "%s=%s\n", name, (p.*m).c_str());
    for (auto &[name, m] : INT_PREFS) fprintf(f, "%s=%d\n", name, p.*m);
    fprintf(f, "register_truncate_titles=%d\n", (int)p.register_truncate_titles);
    fprintf(f, "scrollbar_arrangement=%d\nscrollbar_type=%d\n", p.scrollbar_arrangement, p.scrollbar_type);
    fprintf(f, "bubble_first=%d\nbubble_next=%d\n", p.bubble_first, p.bubble_next);
    fprintf(f, "drag_left_modifier=%s\ndrag_middle_modifier=%s\n", p.drag_left_modifier.c_str(), p.drag_middle_modifier.c_str());
    fclose(f);
}

// --------------------------------------------------------------------------------------------
// Keys (MUIKEY_*): configdata.c's defaults; a spec is qualifiers then a key, as the
// commodities' input descriptions ("control up", "-repeat shift tab", "esc")
// --------------------------------------------------------------------------------------------
const char *const KEY_DEFAULTS[MUIKEY_COUNT] = {
    "-upstroke return", "-repeat space", "-repeat up", "-repeat down", "-repeat shift up", "-repeat shift down",
    "control up", "control down", "-repeat left", "-repeat right", "-repeat control left", "-repeat control right",
    "shift left", "shift right", "-repeat tab", "-repeat shift tab", "control tab", "esc", "-repeat alt tab",
    "-repeat alt shift tab", "help", "control p"};
const char *const KEY_LABELS[MUIKEY_COUNT] = {
    "Press", "Toggle", "Up", "Down", "Page up", "Page down", "Top", "Bottom", "Left", "Right", "Word left",
    "Word right", "Line start", "Line end", "Next gadget", "Previous gadget", "Gadget off", "Close window",
    "Next window", "Previous window", "Help", "Popup"};

namespace {
struct KeySpec { int key = 0, state = 0; bool ok = false; };

int key_by_name(const std::string &n) {
    static const std::pair<const char *, int> names[] = {
        {"return", FL_Enter}, {"enter", FL_KP_Enter}, {"space", ' '}, {"spacebar", ' '}, {"tab", FL_Tab},
        {"esc", FL_Escape}, {"escape", FL_Escape}, {"up", FL_Up}, {"cursor_up", FL_Up}, {"down", FL_Down},
        {"cursor_down", FL_Down}, {"left", FL_Left}, {"cursor_left", FL_Left}, {"right", FL_Right},
        {"cursor_right", FL_Right}, {"help", FL_Help}, {"del", FL_Delete}, {"delete", FL_Delete},
        {"backspace", FL_BackSpace}, {"home", FL_Home}, {"end", FL_End}, {"page_up", FL_Page_Up},
        {"page_down", FL_Page_Down}, {"insert", FL_Insert}};
    for (auto &[name, k] : names) if (n == name) return k;
    if (n.size() > 1 && n[0] == 'f' && isdigit((unsigned char)n[1])) {
        int f = atoi(n.c_str() + 1);
        if (f >= 1 && f <= 12) return FL_F + f;
    }
    if (n.size() == 1) return tolower((unsigned char)n[0]);
    return 0;
}

KeySpec parse_key(const std::string &spec) {
    KeySpec k;
    std::stringstream ss(spec);
    std::string t;
    while (ss >> t) {
        for (auto &ch : t) ch = (char)tolower((unsigned char)ch);
        if (t[0] == '-') continue;   // -repeat, -upstroke: taken as they come
        if (t == "shift" || t == "lshift" || t == "rshift" || t == "caps" || t == "capslock") k.state |= FL_SHIFT;
        else if (t == "control" || t == "ctrl") k.state |= FL_CTRL;
        else if (t == "alt" || t == "lalt" || t == "ralt") k.state |= FL_ALT;
        else if (t == "command" || t == "lcommand" || t == "rcommand" || t == "amiga" || t == "super") k.state |= FL_META;
        else { k.key = key_by_name(t); k.ok = k.key != 0; }
    }
    return k;
}
}  // namespace

bool key_spec_valid(const std::string &spec) { return parse_key(spec).ok; }

MuiKey muikey() {
    int key = Fl::event_key();
    if (key == FL_KP_Enter) key = FL_Enter;
    if (key < 128) key = tolower(key);
    int state = Fl::event_state() & (FL_SHIFT | FL_CTRL | FL_ALT | FL_META);
    for (int i = 0; i < MUIKEY_COUNT; i++) {
        const std::string &s = prefs().keys[i].empty() ? std::string(KEY_DEFAULTS[i]) : prefs().keys[i];
        KeySpec k = parse_key(s);
        if (k.ok && k.key == key && k.state == state) return (MuiKey)i;
    }
    return MUIKEY_NONE;
}

Fl_Font named_font(const std::string &family, bool bold) {
    // (numbers far above the ones Fl::set_fonts() fills in for the font requester)
    static std::map<std::string, Fl_Font> made;
    static int next = 6000;
    std::string key = (bold ? "B" : " ") + family;
    auto it = made.find(key);
    if (it != made.end()) return it->second;
    Fl_Font f = (Fl_Font)next++;
    Fl::set_font(f, strdup(key.c_str()));   // (FLTK keeps the name)
    made[key] = f;
    return f;
}

void apply_prefs() {
    Prefs &p = prefs();
    auto col = [](unsigned rgb) { return fl_rgb_color((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); };
    set_palette(col(p.background), col(p.shadow), col(p.shine), col(p.fill));
    static const Fl_Font fams[3][2] = {{FL_HELVETICA, FL_HELVETICA_BOLD}, {FL_TIMES, FL_TIMES_BOLD}, {FL_COURIER, FL_COURIER_BOLD}};
    p.font = p.font_name.empty() ? fams[p.font_family][0] : named_font(p.font_name);
    p.font_bold = p.font_name.empty() ? fams[p.font_family][1] : named_font(p.font_name, true);
    p.font_fixed = p.fixed_font_name.empty() ? FL_COURIER : named_font(p.fixed_font_name);
    p.list_font = p.list_font_family < 0 ? p.font : fams[p.list_font_family][0];
    p.button_font = p.button_font_family < 0 ? p.font : fams[p.button_font_family][0];
    p.title_font = p.title_font_family < 0 ? p.font_bold : fams[std::clamp(p.title_font_family, 0, 2)][1];
    p.button_font_bold = p.button_font_family < 0 ? p.font_bold : fams[p.button_font_family][1];
    for (int i = 0; i < 16; i++) set_custom_frame(i, p.custom_frame[i]);
    for (int i = 1; i < Frame_Count; i++) set_std_frame((StdFrame)i, p.frame[i].empty() ? default_frame((StdFrame)i) : p.frame[i]);
    // bubble help: FLTK's tooltips with the delays set
    Fl_Tooltip::delay(p.bubble_first / 10.0f);
    Fl_Tooltip::hoverdelay(p.bubble_next / 10.0f);
    // the window manager takes the colours too (its frames, the screens' title bars)
    if (fl_display) {
        char v[64];
        snprintf(v, sizeof v, "%06X %06X %06X %06X", p.background & 0xFFFFFF, p.shadow & 0xFFFFFF, p.shine & 0xFFFFFF,
                 p.fill & 0xFFFFFF);
        Atom prop = XInternAtom(fl_display, "_RAMBLER_PALETTE", False);
        Atom utf8 = XInternAtom(fl_display, "UTF8_STRING", False);
        XChangeProperty(fl_display, RootWindow(fl_display, fl_screen), prop, utf8, 8, PropModeReplace, (unsigned char *)v,
                        (int)strlen(v));
        // and the screen font (its title bars and menus): "family TAB pixel size"
        static const char *const FAMS[] = {"Sans", "Serif", "Monospace"};
        std::string f = (p.font_name.empty() ? std::string(FAMS[std::clamp(p.font_family, 0, 2)]) : p.font_name) + "\t" +
                        std::to_string(p.font_size);
        Atom fprop = XInternAtom(fl_display, "_RAMBLER_FONT", False);
        XChangeProperty(fl_display, RootWindow(fl_display, fl_screen), fprop, utf8, 8, PropModeReplace,
                        (const unsigned char *)f.c_str(), (int)f.size());
        XFlush(fl_display);
    }
    // every window: laid out again (sizes depend on the font and frames) and drawn
    for (Fl_Window *w = Fl::first_window(); w; w = Fl::next_window(w)) {
        if (auto *zw = dynamic_cast<Window *>(w)) {
            zw->color(pens()[MPEN_BACKGROUND]);
            zw->resize(zw->x(), zw->y(), zw->w(), zw->h());
        }
        w->redraw();
    }
}

// ============================================================================================
// Area
// ============================================================================================
Area::Area(StdFrame f) : Fl_Group(0, 0, 10, 10) {
    end();
    box(FL_NO_BOX);
    frame(f);
}

void Area::selected(bool s) {
    if (selected_ == s) return;
    selected_ = s;
    redraw();
}

// the inner spacing: set by the object, or the frame spec's
int Area::add_left() const { return frame_borders(frame_spec().type, frame_spec().down).left + (inner_set_ ? il_ : frame_spec().inner_left); }
int Area::add_right() const { return frame_borders(frame_spec().type, frame_spec().down).right + (inner_set_ ? ir_ : frame_spec().inner_right); }
int Area::add_top() const { return frame_borders(frame_spec().type, frame_spec().down).top + (inner_set_ ? it_ : frame_spec().inner_top); }
int Area::add_bottom() const { return frame_borders(frame_spec().type, frame_spec().down).bottom + (inner_set_ ? ib_ : frame_spec().inner_bottom); }

void Area::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = minh = defw = defh = 0;
    maxw = maxh = MAXMAX;
}

void Area::min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    ask_min_max(minw, minh, defw, defh, maxw, maxh);
    if (fixw_) minw = defw = maxw = fixw_;
    if (fixh_) minh = defh = maxh = fixh_;
    int aw = add_left() + add_right(), ah = add_top() + add_bottom();
    minw += aw; defw += aw; maxw = std::min(MAXMAX, maxw + aw);
    minh += ah; defh += ah; maxh = std::min(MAXMAX, maxh + ah);
    defw = std::clamp(defw, minw, std::max(minw, maxw));
    defh = std::clamp(defh, minh, std::max(minh, maxh));
}

void Area::draw_parent_background(int X, int Y, int W, int H) {
    // the background of the first ancestor that has one, laid over that ancestor (a gradient
    // runs over all of it), shown only here
    fl_push_clip(X, Y, W, H);
    bool done = false;
    for (Fl_Widget *p = parent(); p && !done; p = p->parent()) {
        if (auto *a = dynamic_cast<Area *>(p)) {
            auto *g = dynamic_cast<Group *>(a);
            bool own = a->bg_ref_ || a->bg_pen_ >= 0 || (g && g->frame_spec().type != FST_NONE);
            if (own) { a->draw_background(a->x(), a->y(), a->w(), a->h()); done = true; }
        } else if (auto *w = dynamic_cast<Window *>(p)) {
            draw_imspec(w->background_spec(), 0, 0, w->w(), w->h());
            done = true;
        }
    }
    if (!done) draw_imspec(prefs().window_back, X, Y, W, H);
    fl_pop_clip();
}

void Area::draw_background(int X, int Y, int W, int H) {
    if ((std_ == Frame_Button || std_ == Frame_ImageButton) && bg_pen_ == MPEN_BACKGROUND) {
        // a button: MUII_ButtonBack, pressed MUII_SelectedBack (the Buttons page's patterns)
        bool down = selected_ && (input_ == Input_RelVerify || input_ == Input_Immediate);
        draw_imspec(down ? prefs().selected_back : prefs().button_back, X, Y, W, H);
        return;
    }
    if (bg_ref_) { draw_imspec(prefs().*bg_ref_, X, Y, W, H); return; }
    if (bg_pen_ < 0) {
        // a framed group: MUII_GroupBack; otherwise the parent's background
        if (dynamic_cast<Group *>(this) && frame_spec().type != FST_NONE) { draw_imspec(prefs().group_back, X, Y, W, H); return; }
        draw_parent_background(X, Y, W, H);
        return;
    }
    int pen = bg_pen_;
    // a selected button: the "selected" background (configdata: 0:131 - the fill pen)
    if (selected_ && (input_ == Input_RelVerify || input_ == Input_Immediate)) pen = MPEN_FILL;
    fl_color(pens()[pen]);
    fl_rectf(X, Y, W, H);
}

void Area::draw() {
    if (damage() & ~FL_DAMAGE_CHILD) {
        draw_background(x(), y(), w(), h());
        draw_frame(frame_spec(), x(), y(), w(), h(), selected_ && (input_ == Input_RelVerify || input_ == Input_Immediate));
        fl_push_clip(mleft(), mtop(), mwidth(), mheight());
        draw_contents();
        fl_pop_clip();
    }
    draw_children();
    // DoDisabledPattern: a ghost object is covered with dots of the shadow pen (the pattern
    // 0x8888, 0x2222), the frame and the children with it
    if (disabled_) {
        fl_push_clip(x(), y(), w(), h());
        fl_color(pens()[MPEN_SHADOW]);
        for (int Y = y(); Y < y() + h(); Y++)
            for (int X = x() + ((Y - y()) & 1 ? 2 : 0); X < x() + w(); X += 4) fl_point(X, Y);
        fl_pop_clip();
    }
    if (Fl::focus() == this && accepts_focus()) draw_active_outline();
}

// _lune_focus_new: a line pattern 0xCCCC in the shine and shadow pens, one pixel outside
void Area::draw_active_outline() {
    int x1 = x() - 1, y1 = y() - 1, x2 = x() + w(), y2 = y() + h();
    fl_push_no_clip();
    int n = 0;
    auto dot = [&](int X, int Y) {
        fl_color((0xCCCC >> (15 - (n & 15))) & 1 ? pens()[MPEN_SHINE] : pens()[MPEN_SHADOW]);
        fl_point(X, Y);
        n++;
    };
    for (int X = x1; X <= x2; X++) dot(X, y1);
    for (int Y = y1 + 1; Y <= y2; Y++) dot(x2, Y);
    for (int X = x2 - 1; X >= x1; X--) dot(X, y2);
    for (int Y = y2 - 1; Y > y1; Y--) dot(x1, Y);
    fl_pop_clip();
}

// the outline lies outside the object: its parent draws that strip again
void Area::focus_changed() {
    redraw();
    if (parent()) parent()->damage(FL_DAMAGE_ALL, x() - 1, y() - 1, w() + 2, h() + 2);
    if (window()) window()->damage(FL_DAMAGE_ALL, x() - 1, y() - 1, w() + 2, h() + 2);
}

int Area::handle(int e) {
    if ((e == FL_FOCUS || e == FL_UNFOCUS) && accepts_focus() && children() == 0) { focus_changed(); return 1; }
    // (keys a group does not take go on to the window, which moves between the objects with
    // the keys set for it - not FLTK's own Tab navigation)
    if (e == FL_KEYBOARD && children() > 0 && input_ == Input_None) return 0;
    if (input_ == Input_None || disabled_) return Fl_Group::handle(e);
    switch (e) {
    case FL_KEYBOARD: {
        // the active object (area.c MUIKEY_PRESS / MUIKEY_TOGGLE): Press presses it (a toggle
        // object toggles), Toggle toggles a toggle object only
        if (Fl::focus() != this) return 0;
        MuiKey k = muikey();
        if (k == MUIKEY_PRESS || (k == MUIKEY_TOGGLE && input_ == Input_Toggle)) {
            if (input_ == Input_Toggle) { selected(!selected_); if (on_press) on_press(); }
            else if (on_press) on_press();
            return 1;
        }
        return 0;
    }
    case FL_PUSH:
        if (Fl::event_button() != FL_LEFT_MOUSE) return 0;
        pressed_ = true;
        if (input_ == Input_Toggle) {
            selected(!selected_);
            if (on_press) on_press();
        } else selected(true);
        if (input_ == Input_Immediate && on_press) on_press();
        return 1;
    case FL_DRAG:
        if (pressed_ && input_ == Input_RelVerify) selected(Fl::event_inside(this));
        return 1;
    case FL_RELEASE:
        if (!pressed_) return 0;
        pressed_ = false;
        if (input_ == Input_RelVerify) {
            bool over = Fl::event_inside(this);
            selected(false);
            if (over && on_press) on_press();
        } else if (input_ == Input_Immediate) selected(false);
        return 1;
    }
    return Fl_Group::handle(e);
}

// ============================================================================================
// Group
// ============================================================================================
Group::Group(Dir d, StdFrame f) : Area(f), dir_(d), hs_(-1), vs_(-1) {
    background(-1);   // a group shows its parent's background unless framed
    begin();          // the children follow (end() closes it)
}

// the title's band above the frame (mad_TitleHeightAbove): above - the font's baseline,
// centred - half the font
int Group::title_h() const {
    if (title_.empty()) return 0;
    return prefs().title_size() + 2;
}

static int title_above(int th) { return prefs().group_title_position == 0 ? th - 3 : th / 2; }

std::vector<Area *> Group::areas() const {
    std::vector<Area *> v;
    for (int i = 0; i < children(); i++)
        if (auto *a = dynamic_cast<Area *>(child(i)); a && a->visible()) v.push_back(a);
    return v;
}

void Group::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    auto kids = areas();
    minw = minh = defw = defh = 0;
    maxw = maxh = MAXMAX;
    if (kids.empty()) return;
    if (columns_ > 1) {
        // a grid: columns as wide as their widest object, rows as high as their highest
        int cols = columns_, rows = ((int)kids.size() + cols - 1) / cols;
        std::vector<int> cminw(cols), cdefw(cols), cmaxw(cols, MAXMAX), rminh(rows), rdefh(rows), rmaxh(rows, MAXMAX);
        for (size_t i = 0; i < kids.size(); i++) {
            int c = i % cols, r = i / cols, a, b, cc, d, e, f;
            kids[i]->min_max(a, b, cc, d, e, f);
            cminw[c] = std::max(cminw[c], a); cdefw[c] = std::max(cdefw[c], cc); cmaxw[c] = std::min(cmaxw[c], e);
            rminh[r] = std::max(rminh[r], b); rdefh[r] = std::max(rdefh[r], d); rmaxh[r] = std::min(rmaxh[r], f);
        }
        maxw = maxh = 0;
        for (int c = 0; c < cols; c++) { minw += cminw[c]; defw += cdefw[c]; maxw += std::max(cmaxw[c], cminw[c]); }
        for (int r = 0; r < rows; r++) { minh += rminh[r]; defh += rdefh[r]; maxh += std::max(rmaxh[r], rminh[r]); }
        minw += hsp() * (cols - 1); defw += hsp() * (cols - 1); maxw += hsp() * (cols - 1);
        minh += vsp() * (rows - 1); defh += vsp() * (rows - 1); maxh += vsp() * (rows - 1);
    } else {
        bool horiz = dir_ == Horizontal;
        int along_min = 0, along_def = 0, along_max = 0, across_min = 0, across_def = 0, across_max = MAXMAX;
        int same_min = 0, same_def = 0;
        for (auto *k : kids) {
            int a, b, c, d, e, f;
            k->min_max(a, b, c, d, e, f);
            int amin = horiz ? a : b, adef = horiz ? c : d, amax = horiz ? e : f;
            int xmin = horiz ? b : a, xdef = horiz ? d : c, xmax = horiz ? f : e;
            // (w0_maxwidth: an object without weight along the group grows no further)
            if (k->weight(horiz) <= 0) amax = amin;
            along_min += amin; along_def += adef; along_max += amax;
            same_min = std::max(same_min, amin); same_def = std::max(same_def, adef);
            across_min = std::max(across_min, xmin);
            across_def = std::max(across_def, xdef);
            across_max = std::min(across_max, xmax);
        }
        if ((horiz && same_w_) || (!horiz && same_h_)) {
            along_min = same_min * (int)kids.size();
            along_def = same_def * (int)kids.size();
        }
        int sp = (horiz ? hsp() : vsp()) * ((int)kids.size() - 1);
        along_min += sp; along_def += sp; along_max = std::min(MAXMAX, along_max + sp);
        across_max = std::max(across_max, across_min);
        across_def = std::clamp(across_def, across_min, across_max);
        if (horiz) { minw = along_min; defw = along_def; maxw = along_max; minh = across_min; defh = across_def; maxh = across_max; }
        else { minh = along_min; defh = along_def; maxh = along_max; minw = across_min; defw = across_def; maxw = across_max; }
    }
    minh += title_h(); defh += title_h(); maxh = std::min(MAXMAX, maxh + title_h());
}

void Group::resize(int X, int Y, int W, int H) {
    Fl_Widget::resize(X, Y, W, H);   // the children are placed by layout(), not scaled
    layout();
}

// Shares `room` among the objects by weight, never giving one more than its max - min.
static std::vector<int> share(int room, const std::vector<int> &mins, const std::vector<int> &maxs,
                              const std::vector<int> &weights) {
    size_t n = mins.size();
    std::vector<int> size = mins;
    std::vector<bool> full(n, false);
    for (size_t i = 0; i < n; i++) full[i] = maxs[i] <= mins[i] || weights[i] <= 0;
    while (room > 0) {
        long wsum = 0;
        for (size_t i = 0; i < n; i++) if (!full[i]) wsum += weights[i];
        if (wsum == 0) break;
        int given = 0;
        bool capped = false;
        for (size_t i = 0; i < n; i++) {
            if (full[i]) continue;
            int add = (int)((long)room * weights[i] / wsum);
            if (size[i] + add >= maxs[i]) { add = maxs[i] - size[i]; full[i] = true; capped = true; }
            size[i] += add;
            given += add;
        }
        room -= given;
        if (!capped) {
            // rounding: the last pixels to the first objects that still grow
            for (size_t i = 0; i < n && room > 0; i++)
                if (!full[i]) { size[i]++; room--; }
            break;
        }
    }
    return size;
}

void Group::layout_line(bool horiz) {
    auto kids = areas();
    if (kids.empty()) return;
    int X = mleft(), Y = mtop() + title_h(), W = mwidth(), H = mheight() - title_h();
    size_t n = kids.size();
    std::vector<int> mins(n), maxs(n), weights(n), xmin(n), xmax(n);
    for (size_t i = 0; i < n; i++) {
        int a, b, c, d, e, f;
        kids[i]->min_max(a, b, c, d, e, f);
        mins[i] = horiz ? a : b;
        maxs[i] = horiz ? e : f;
        xmin[i] = horiz ? b : a;
        xmax[i] = horiz ? f : e;
        weights[i] = kids[i]->weight(horiz);
    }
    if ((horiz && same_w_) || (!horiz && same_h_)) {
        int m = *std::max_element(mins.begin(), mins.end());
        for (auto &v : mins) v = m;
    }
    int sp = horiz ? hsp() : vsp();
    int total = (horiz ? W : H) - sp * ((int)n - 1);
    int need = 0;
    for (int v : mins) need += v;
    auto sizes = share(std::max(0, total - need), mins, maxs, weights);
    // objects that cannot grow while there is room left: spread it as extra spacing (Lune
    // centres the objects then)
    int used = 0;
    for (int v : sizes) used += v;
    int extra = std::max(0, total - used);
    int pos = (horiz ? X : Y) + extra / 2;
    for (size_t i = 0; i < n; i++) {
        int across = horiz ? H : W;
        int xs = std::clamp(across, xmin[i], std::max(xmin[i], xmax[i]));
        int xoff = (across - xs) / 2;   // a narrower object is centred across
        if (horiz) kids[i]->resize(pos, Y + xoff, sizes[i], xs);
        else kids[i]->resize(X + xoff, pos, xs, sizes[i]);
        pos += sizes[i] + sp;
    }
}

void Group::layout_columns() {
    auto kids = areas();
    int cols = columns_, rows = ((int)kids.size() + cols - 1) / cols;
    std::vector<int> cmin(cols), cmax(cols, MAXMAX), cw(cols, 100), rmin(rows), rmax(rows, MAXMAX), rw(rows, 100);
    for (size_t i = 0; i < kids.size(); i++) {
        int c = i % cols, r = i / cols, a, b, cc, d, e, f;
        kids[i]->min_max(a, b, cc, d, e, f);
        cmin[c] = std::max(cmin[c], a); cmax[c] = std::min(cmax[c], e);
        rmin[r] = std::max(rmin[r], b); rmax[r] = std::min(rmax[r], f);
    }
    for (int c = 0; c < cols; c++) cmax[c] = std::max(cmax[c], cmin[c]);
    for (int r = 0; r < rows; r++) rmax[r] = std::max(rmax[r], rmin[r]);
    int W = mwidth() - hsp() * (cols - 1), H = mheight() - title_h() - vsp() * (rows - 1);
    int needw = 0, needh = 0;
    for (int v : cmin) needw += v;
    for (int v : rmin) needh += v;
    auto ws = share(std::max(0, W - needw), cmin, cmax, cw);
    auto hs = share(std::max(0, H - needh), rmin, rmax, rw);
    int y = mtop() + title_h();
    for (int r = 0; r < rows; r++) {
        int x = mleft();
        for (int c = 0; c < cols; c++) {
            size_t i = r * cols + c;
            if (i < kids.size()) {
                // a child narrower / lower than its cell is centred in it (layout_2d_distribute_space)
                int a, b, cc, d, e, f;
                kids[i]->min_max(a, b, cc, d, e, f);
                int cwid = std::max(std::min(e, ws[c]), a), chei = std::max(std::min(f, hs[r]), b);
                kids[i]->resize(x + (ws[c] - cwid) / 2, y + (hs[r] - chei) / 2, cwid, chei);
            }
            x += ws[c] + hsp();
        }
        y += hs[r] + vsp();
    }
}

void Group::layout() {
    if (columns_ > 1) layout_columns();
    else layout_line(dir_ == Horizontal);
    redraw();
}

void Group::draw() {
    if (damage() & ~FL_DAMAGE_CHILD) {
        // a framed group draws its background (configdata: MUII_GroupBack); the title sits on
        // the top edge of the frame, centred
        int th = title_h(), ta = title_above(th);
        draw_background(x(), y(), w(), h());
        if (!th) draw_frame(frame_spec(), x(), y(), w(), h());
        else {
            // the frame, clipped round the title (an eighth of the font's height either side)
            const Prefs &p = prefs();
            fl_font(p.title_font, p.title_size());
            int addtw = p.group_title_color == 3 ? 2 : p.group_title_color == 2 ? 1 : 0;
            int tw = (int)fl_width(title_.c_str()) + addtw, hspace = std::max(1, p.title_size() / 8);
            int tx = x() + (w() - tw) / 2;
            fl_push_clip(x(), y(), tx - hspace - x(), h());
            draw_frame(frame_spec(), x(), y() + ta, w(), h() - ta);
            fl_pop_clip();
            fl_push_clip(tx + tw + hspace, y(), x() + w() - (tx + tw + hspace), h());
            draw_frame(frame_spec(), x(), y() + ta, w(), h() - ta);
            fl_pop_clip();
            fl_push_clip(tx - hspace, y() + th, tw + 2 * hspace, h() - th);
            draw_frame(frame_spec(), x(), y() + ta, w(), h() - ta);
            fl_pop_clip();
            // the title: standard (shadow), hilite (shine), 3D (shadow under shine), outline
            // (shine round shadow)
            const Pens &P = pens();
            int by = y(), bh = th;
            auto txt = [&](int dx, int dy, Fl_Color c) {
                fl_color(c);
                fl_draw(title_.c_str(), tx + dx, by + dy, tw - addtw, bh, FL_ALIGN_LEFT);
            };
            switch (p.group_title_color) {
            case 2: txt(1, 1, P[MPEN_SHADOW]); txt(0, 0, P[MPEN_SHINE]); break;
            case 3:
                txt(2, 1, P[MPEN_SHINE]); txt(0, 1, P[MPEN_SHINE]); txt(1, 2, P[MPEN_SHINE]); txt(1, 0, P[MPEN_SHINE]);
                txt(1, 1, P[MPEN_SHADOW]);
                break;
            case 1: txt(0, 0, P[MPEN_SHINE]); break;
            default: txt(0, 0, P[MPEN_SHADOW]); break;
            }
        }
    }
    if (sketch_) { draw_sketch(); return; }
    draw_children();
}

// ============================================================================================
// Register
// ============================================================================================
static const int INTERTAB = 4, TEXTSPACING = 4, REGISTERTAB_EXTRA_HEIGHT = 7;

Register::Register(const std::vector<std::string> &titles) : Group(Group::Vertical) {
    for (auto &t : titles) titles_.push_back(tr_text(t));
    // the register's own background inside its frame (MUII_RegisterBack); a page group
    // without titles: MUII_PageBack
    background(titles.empty() ? &Prefs::page_back : &Prefs::register_back);
}

int Register::tab_h() const { return titles_.empty() ? 0 : prefs().font_size + REGISTERTAB_EXTRA_HEIGHT; }

// SetHardCoord: the page sits inside the frame, below the tabs
void Register::hard_coord() {
    if (titles_.empty()) { inner(0, 0, 0, 0); return; }   // a page group: no tabs, no frame
    const FrameSpec &g = std_frame(Frame_Group);
    inner(g.inner_left + 1, g.inner_right + 1, tab_h() + g.inner_top, g.inner_bottom + 1);
}

void Register::end() {
    Group::end();
    hard_coord();
    for (int i = 0; i < children(); i++) if (i != active_) child(i)->hide();
}

void Register::active(int i) {
    if (i < 0 || i >= children() || i == active_) return;
    child(active_)->hide();
    active_ = i;
    child(i)->show();
    resize(x(), y(), w(), h());
    redraw();
    if (on_change) on_change(i);
}

void Register::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    // a page group: as big as the biggest page (all pages, shown or not)
    minw = minh = defw = defh = 0;
    maxw = maxh = MAXMAX;
    for (int i = 0; i < children(); i++) {
        auto *a = dynamic_cast<Area *>(child(i));
        if (!a) continue;
        int mw, mh, dw, dh, xw, xh;
        a->min_max(mw, mh, dw, dh, xw, xh);
        minw = std::max(minw, mw); minh = std::max(minh, mh);
        defw = std::max(defw, dw); defh = std::max(defh, dh);
        maxw = std::min(maxw, xw); maxh = std::min(maxh, xh);
    }
    maxw = std::max(maxw, minw);
    maxh = MAXMAX;
    if (titles_.empty()) return;
    // the tabs: room for every title (register_truncate_titles off)
    int n = std::max(1, (int)titles_.size());
    int total_hspacing = (n + 1) * INTERTAB - 2;
    fl_font(prefs().font, prefs().font_size);
    int textmax = 0;
    for (auto &t : titles_) textmax = std::max(textmax, (int)fl_width(t.c_str()));
    int tabs_def = total_hspacing + (textmax + TEXTSPACING + 1) * n - add_left() - add_right();
    minw = std::max(minw, total_hspacing * 3 - add_left() - add_right());
    defw = std::max(defw, tabs_def);
    maxw = std::max(maxw, defw);
    minw = std::max(minw, tabs_def);   // the titles are not cut
}

void Register::resize(int X, int Y, int W, int H) {
    Fl_Widget::resize(X, Y, W, H);
    for (int i = 0; i < children(); i++) child(i)->resize(mleft(), mtop(), mwidth(), mheight());
}

// LayoutTabItems (one row)
void Register::tab_x(int i, int &x1, int &x2) const {
    int n = std::max(1, (int)titles_.size());
    int total_hspacing = (n + 1) * INTERTAB - 2;
    int item_w = (w() - total_hspacing) / n, extra = (w() - total_hspacing) % n;
    int xx = INTERTAB - 1;
    for (int k = 0; k <= i; k++) {
        x1 = xx;
        x2 = x1 + item_w - 1;
        if (extra > 0) { x2++; extra--; }
        xx += (x2 - x1 + 1) + INTERTAB;
    }
}

// RenderRegisterTabItem
void Register::draw_tab(int i) {
    int x1, x2;
    tab_x(i, x1, x2);
    const Pens &P = pens();
    int top, bottom, left, right;
    if (i == active_) {
        top = y(); bottom = y() + tab_h() - 1 - 2;
        left = x() + x1 - 1; right = x() + x2 + 1;
        int bw = right - left + 1;
        const std::string &rb = prefs().register_back;
        draw_imspec(rb, left, top + 4, bw, tab_h() - 4);
        draw_imspec(rb, left + 2, top + 2, bw - 4, 2);
        draw_imspec(rb, left + 4, top + 1, bw - 8, 1);
    } else {
        top = y() + 2; bottom = y() + tab_h() - 1 - 1;
        left = x() + x1; right = x() + x2;
        const std::string &rb = prefs().register_back;
        draw_imspec(rb, left, top + 4, right - left + 1, bottom - top - 3);
        draw_imspec(rb, left + 2, top + 2, right - left - 3, 2);
        draw_imspec(rb, left + 4, top + 1, right - left - 7, 1);
    }
    auto px = [](int X, int Y) { fl_point(X, Y); };
    fl_color(P[MPEN_SHINE]);
    fl_xyline(left + 4, top, right - 4);                // top
    fl_yxline(left, top + 4, bottom);                   // left
    px(left + 1, top + 3); px(left + 1, top + 2); px(left + 2, top + 1); px(left + 3, top + 1);
    fl_color(P[MPEN_HALFSHINE]);
    px(left + 3, top); px(left + 4, top + 1); px(left + 2, top + 2); px(left + 3, top + 2);
    px(left + 2, top + 3); px(left, top + 3); px(left + 1, top + 4);
    if (i == active_) {
        fl_color(P[MPEN_SHINE]);
        px(left - 1, bottom + 1);
        fl_color(P[MPEN_SHADOW]);
        px(right + 1, bottom + 1);
        draw_imspec(prefs().register_back, left - 1, bottom + 2, right - left + 3, 1);   // the frame's top line opens under the active tab
    }
    fl_color(P[MPEN_SHADOW]);
    px(right - 1, top + 2);
    fl_yxline(right, top + 4, bottom);
    fl_color(P[MPEN_HALFSHADOW]);
    px(right - 2, top + 1); px(right - 1, top + 3); px(right, top + 3);
    fl_color(P[MPEN_BACKGROUND]);
    px(right - 3, top + 1);
    // the title
    int fit = right - left + 1 - TEXTSPACING;
    fl_font(prefs().font, prefs().font_size);
    fl_color(P[MPEN_TEXT]);
    fl_push_clip(left + TEXTSPACING / 2, top, fit, bottom - top + 1);
    fl_draw(titles_[i].c_str(), left + TEXTSPACING / 2, top + TEXTSPACING - 1, fit, prefs().font_size + 2, FL_ALIGN_CENTER);
    fl_pop_clip();
}

void Register::draw() {
    if (damage() & ~FL_DAMAGE_CHILD) {
        // the parent's background behind the tabs, the register's inside the frame
        if (titles_.empty()) {   // a page group
            draw_background(x(), y(), w(), h());
            for (int i = 0; i < children(); i++) if (child(i)->visible()) child(i)->damage(FL_DAMAGE_ALL);
            draw_children();
            return;
        }
        const Pens &P = pens();
        int th = tab_h();
        int fw = w(), fh = h() - th;
        draw_parent_background(x(), y(), w(), th);
        draw_background(x(), y() + th, w(), h() - th);
        fl_color(P[MPEN_SHINE]);
        fl_yxline(x(), y() + th - 1, y() + th + fh - 1);
        fl_xyline(x() + 1, y() + th - 1, x() + fw - 2);
        fl_color(P[MPEN_SHADOW]);
        fl_yxline(x() + fw - 1, y() + th - 1, y() + th + fh - 1);
        fl_xyline(x() + 1, y() + th + fh - 1, x() + fw - 2);
        for (int i = 0; i < (int)titles_.size(); i++) draw_tab(i);
        // the page draws itself entirely
        for (int i = 0; i < children(); i++) if (child(i)->visible()) child(i)->damage(FL_DAMAGE_ALL);
    }
    draw_children();
}

int Register::handle(int e) {
    if (e == FL_PUSH && !titles_.empty() && Fl::event_button() == FL_LEFT_MOUSE && Fl::event_y() < y() + tab_h()) {
        for (int i = 0; i < (int)titles_.size(); i++) {
            int x1, x2;
            tab_x(i, x1, x2);
            if (Fl::event_x() >= x() + x1 && Fl::event_x() <= x() + x2) { active(i); return 1; }
        }
        return 1;
    }
    return Group::handle(e);
}

// ============================================================================================
// Rectangle
// ============================================================================================
void Rectangle::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = minh = defw = defh = 0;
    maxw = maxh = MAXMAX;   // (HSpace / VSpace: free, without weight across)
    if (kind_ == 'H') { minh = defh = maxh = 2; }
}

void Rectangle::draw_contents() {
    if (kind_ != 'H') return;
    int yy = mtop() + mheight() / 2 - 1;
    fl_color(pens()[MPEN_SHADOW]);
    fl_xyline(mleft(), yy, mleft() + mwidth() - 1);
    fl_color(pens()[MPEN_SHINE]);
    fl_xyline(mleft(), yy + 1, mleft() + mwidth() - 1);
}

// ============================================================================================
// Text
// ============================================================================================
Text::Text(const std::string &t, StdFrame f) : Area(f), text_(tr_text(t)) {
    if (f == Frame_Text) background(&Prefs::text_back);   // MUII_TextBack
}

// Lune's control sequences: ESC c / r / l alignment, ESC b bold, ESC n normal, ESC - stop
std::vector<Text::Line> Text::parse() const {
    std::vector<Line> lines;
    std::string all = preparse_ + text_;
    int align = FL_ALIGN_LEFT;
    bool bold = false;
    Line cur{"", align, bold};
    for (size_t i = 0; i < all.size(); i++) {
        char ch = all[i];
        if (ch == '\033' && i + 1 < all.size()) {
            char k = all[++i];
            if (k == 'c') cur.align = align = FL_ALIGN_CENTER;
            else if (k == 'r') cur.align = align = FL_ALIGN_RIGHT;
            else if (k == 'l') cur.align = align = FL_ALIGN_LEFT;
            else if (k == 'b') cur.bold = bold = true;
            else if (k == 'n') cur.bold = bold = false;
            else if (k == '-') { cur.text += all.substr(i + 1); break; }
            continue;
        }
        if (ch == '\n') {
            lines.push_back(cur);
            cur = Line{"", align, bold};
            continue;
        }
        cur.text += ch;
    }
    lines.push_back(cur);
    return lines;
}

Fl_Font Text::font(bool bold) const {
    if (button_font_) return bold ? prefs().button_font_bold : prefs().button_font;
    return bold ? prefs().font_bold : prefs().font;
}

int Text::font_size() const { return button_font_ ? prefs().button_size() : prefs().font_size; }

void Text::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    auto lines = parse();
    int wmax = 0;
    for (auto &l : lines) {
        fl_font(font(l.bold), font_size());
        wmax = std::max(wmax, (int)fl_width(l.text.c_str()));
    }
    int lh = font_size() + 3;
    minw = defw = wmax;
    maxw = setmax_ ? wmax : MAXMAX;
    minh = defh = maxh = lh * (int)lines.size();
}

void Text::draw_contents() {
    auto lines = parse();
    int lh = font_size() + 3;
    int y0 = mtop() + (mheight() - lh * (int)lines.size()) / 2;
    bool sel = selected_ && input_ != Input_None;
    for (size_t i = 0; i < lines.size(); i++) {
        fl_font(font(lines[i].bold), font_size());
        fl_color(active_r() ? pens()[sel ? MPEN_SHINE : MPEN_TEXT] : pens()[MPEN_HALFSHADOW]);
        fl_draw(lines[i].text.c_str(), mleft(), y0 + (int)i * lh, mwidth(), lh, (Fl_Align)(lines[i].align | FL_ALIGN_CLIP));
    }
}

// MUI_MakeObject(MUIO_Button): a centred text in the button frame, RelVerify, background
// "0:128", the selected background "0:131"
Button::Button(const std::string &label) : Text("\033c" + label, Frame_Button) {
    background(MPEN_BACKGROUND);
    input_mode(Input_RelVerify);
    button_font_ = true;
}

// ============================================================================================
// String
// ============================================================================================
String::String(const std::string &contents, int maxlen) : Area(Frame_String) {
    background(MPEN_BACKGROUND);
    begin();
    // (the string's own input: its colours follow whether it is the active one)
    struct Input : Fl_Input {
        Input() : Fl_Input(0, 0, 10, 10) {}
        int handle(int e) override {
            if ((e == FL_FOCUS || e == FL_UNFOCUS) && parent()) parent()->redraw();
            return Fl_Input::handle(e);
        }
    };
    in_ = new Input();
    in_->box(FL_FLAT_BOX);
    in_->textfont(prefs().font);
    in_->textsize(prefs().font_size);
    in_->value(contents.c_str());
    if (maxlen) in_->maximum_size(maxlen);
    in_->when(FL_WHEN_ENTER_KEY_ALWAYS);
    in_->callback([](Fl_Widget *, void *d) {
        auto *s = (String *)d;
        if (s->on_acknowledge) s->on_acknowledge();
    }, this);
    end();
}

// the colours of the Strings page: inactive "2:m2" / "m5", active "2:m1" / "m5", the cursor
// "m7", the marked text's background "m6" (its text: FLTK's contrast to it)
void String::draw_background(int X, int Y, int W, int H) {
    Area::draw_background(X, Y, W, H);
    const Prefs &p = prefs();
    bool active = Fl::focus() == in_;
    auto pen = [](const std::string &s) { unsigned c = penspec_rgb(s); return fl_rgb_color((c >> 16) & 255, (c >> 8) & 255, c & 255); };
    in_->color(pen(active ? p.string_active_bg : p.string_bg));
    in_->textcolor(pen(active ? p.string_active_text : p.string_text));
    in_->cursor_color(pen(p.string_cursor));
    in_->selection_color(pen(p.string_marked_bg));
    in_->textfont(p.font);
    in_->textsize(p.font_size);
    if (center_) {
        // centred while not being edited (Fl_Input has no centred text: the space before it)
        fl_font(p.font, p.font_size);
        int tw = (int)fl_width(in_->value());
        in_->resize(active ? mleft() : mleft() + std::max(0, (mwidth() - tw) / 2 - 3), mtop(),
                    active ? mwidth() : mwidth() - std::max(0, (mwidth() - tw) / 2 - 3), mheight());
        fl_color(in_->color());
        fl_rectf(mleft(), mtop(), mwidth(), mheight());
    }
}

void String::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    int lh = prefs().font_size + 4;
    minw = 30; defw = 200; maxw = MAXMAX;
    minh = defh = maxh = lh;
}

void String::resize(int X, int Y, int W, int H) {
    Fl_Widget::resize(X, Y, W, H);
    in_->resize(mleft(), mtop(), mwidth(), mheight());
}

// ============================================================================================
// Prop
// ============================================================================================
Prop::Prop(bool horiz) : Area(Frame_Prop), horiz_(horiz) {
    background(&Prefs::prop_back);   // the track (MUII_PropBack)
    inner(1, 1, 1, 1);
}

void Prop::set(int entries, int visible, int first) {
    entries_ = std::max(1, entries);
    visible_ = std::clamp(visible, 1, entries_);
    first_ = std::clamp(first, 0, entries_ - visible_);
    redraw();
}

void Prop::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    if (horiz_) { minw = 20; defw = 100; maxw = MAXMAX; minh = defh = maxh = 10; }
    else { minh = 20; defh = 100; maxh = MAXMAX; minw = defw = maxw = 10; }
}

void Prop::knob(int &kx, int &ky, int &kw, int &kh) const {
    int len = horiz_ ? mwidth() : mheight();
    int kl = std::max(8, (int)((long long)len * visible_ / entries_));
    int range = entries_ - visible_;
    int kp = range > 0 ? (int)((long long)(len - kl) * first_ / range) : 0;
    if (horiz_) { kx = mleft() + kp; ky = mtop(); kw = kl; kh = mheight(); }
    else { kx = mleft(); ky = mtop() + kp; kw = mwidth(); kh = kl; }
}

void Prop::draw_contents() {
    int kx, ky, kw, kh;
    knob(kx, ky, kw, kh);
    fl_color(pens()[MPEN_BACKGROUND]);
    fl_rectf(kx, ky, kw, kh);
    draw_frame(FST_BEVEL, grab_ >= 0, kx, ky, kw, kh);
}

void Prop::step(int n) {
    int old = first_;
    set(entries_, visible_, first_ + n);
    if (first_ != old && on_change) on_change(first_);
}

int Prop::handle(int e) {
    int pos = horiz_ ? Fl::event_x() : Fl::event_y();
    switch (e) {
    case FL_PUSH: {
        int kx, ky, kw, kh;
        knob(kx, ky, kw, kh);
        int ks = horiz_ ? kx : ky, kl = horiz_ ? kw : kh;
        if (pos >= ks && pos < ks + kl) { grab_ = pos; grab_first_ = first_; redraw(); }
        else {   // a click in the track: a page towards it
            int nf = first_ + (pos < ks ? -visible_ : visible_);
            set(entries_, visible_, nf);
            if (on_change) on_change(first_);
        }
        return 1;
    }
    case FL_DRAG:
        if (grab_ >= 0) {
            int len = (horiz_ ? mwidth() : mheight());
            int kl = std::max(8, (int)((long long)len * visible_ / entries_));
            int range = entries_ - visible_;
            if (len > kl && range > 0) {
                int nf = grab_first_ + (int)((long long)(pos - grab_) * range / (len - kl));
                int old = first_;
                set(entries_, visible_, nf);
                if (first_ != old && on_change) on_change(first_);
            }
        }
        return 1;
    case FL_RELEASE:
        grab_ = -1;
        redraw();
        return 1;
    }
    return Area::handle(e);
}

// ============================================================================================
// Arrow, Scrollbar
// ============================================================================================
Arrow::Arrow(Dir d, int size) : Area(Frame_ImageButton), dir_(d), size_(size) {
    background(MPEN_BACKGROUND);   // MUII_ButtonBack
    input_mode(Input_RelVerify);
    weight(0);
}

void Arrow::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    // as wide (high) as the bar it belongs to, square
    int iw = std::max(4, size_ - add_left() - add_right()), ih = std::max(4, size_ - add_top() - add_bottom());
    minw = defw = maxw = iw;
    minh = defh = maxh = ih;
}

void Arrow::draw_arrow(Dir d, int X, int Y, int W, int H) {
    int cx = X + W / 2, cy = Y + H / 2, r = std::max(2, std::min(W, H) / 2 - 1);
    switch (d) {
    case Up: fl_polygon(cx - r, cy + r / 2, cx + r, cy + r / 2, cx, cy - r / 2 - 1); break;
    case Down: fl_polygon(cx - r, cy - r / 2, cx + r, cy - r / 2, cx, cy + r / 2 + 1); break;
    case Left: fl_polygon(cx + r / 2, cy - r, cx + r / 2, cy + r, cx - r / 2 - 1, cy); break;
    case Right: fl_polygon(cx - r / 2, cy - r, cx - r / 2, cy + r, cx + r / 2 + 1, cy); break;
    }
}

void Arrow::draw_contents() {
    fl_color(pens()[MPEN_TEXT]);
    draw_arrow(dir_, mleft(), mtop(), mwidth(), mheight());
}

void Arrow::repeat(void *d) {
    auto *a = (Arrow *)d;
    if (!a->pressed_) return;
    if (a->selected_ && a->on_step) a->on_step();
    Fl::repeat_timeout(0.05, repeat, d);
}

int Arrow::handle(int e) {
    switch (e) {
    case FL_PUSH:
        if (Fl::event_button() != FL_LEFT_MOUSE) return 0;
        pressed_ = true;
        selected(true);
        if (on_step) on_step();
        Fl::add_timeout(0.3, repeat, this);   // the first repeat after a pause
        return 1;
    case FL_DRAG:
        if (pressed_) selected(Fl::event_inside(this));
        return 1;
    case FL_RELEASE:
        pressed_ = false;
        Fl::remove_timeout(repeat, this);
        selected(false);
        return 1;
    case FL_HIDE:
        Fl::remove_timeout(repeat, this);
        break;
    }
    return Area::handle(e);
}

Scrollbar::Scrollbar(bool horiz, Type type) : Group(horiz ? Horizontal : Vertical), type_(type) {
    spacing(0);
    prop_ = new Prop(horiz);
    up_ = new Arrow(horiz ? Arrow::Left : Arrow::Up);
    down_ = new Arrow(horiz ? Arrow::Right : Arrow::Down);
    end();
    prop_->on_change = [this](int f) { if (on_change) on_change(f); };
    up_->on_step = [this] { prop_->step(-incdec); };
    down_->on_step = [this] { prop_->step(incdec); };
    arrange();
}

// MUIM_Setup: the children in the order of the arrangement (or of the type set)
void Scrollbar::arrange() {
    int a = type_ == Default ? prefs().scrollbar_arrangement : (int)type_;
    if (a == arranged_) return;
    arranged_ = a;
    Fl_Widget *order[3];
    if (a == 1) { order[0] = up_; order[1] = prop_; order[2] = down_; }
    else if (a == 2) { order[0] = up_; order[1] = down_; order[2] = prop_; }
    else { order[0] = prop_; order[1] = up_; order[2] = down_; }
    for (auto *w : order) remove(w);
    for (auto *w : order) add(w);
}

void Scrollbar::resize(int X, int Y, int W, int H) {
    arrange();   // (the preferences may have changed)
    Group::resize(X, Y, W, H);
}

// ============================================================================================
// Cycle
// ============================================================================================
Cycle::Cycle(std::vector<std::string> entries, int active) : Area(Frame_Button), entries_(std::move(entries)) {
    for (auto &e : entries_) e = tr(e);
    background(MPEN_BACKGROUND);
    active_ = std::clamp(active, 0, std::max(0, (int)entries_.size() - 1));
}

void Cycle::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    fl_font(prefs().font, prefs().font_size);
    int w = 0;
    for (auto &e : entries_) w = std::max(w, (int)fl_width(e.c_str()));
    minw = defw = w + 26;
    maxw = MAXMAX;
    minh = defh = maxh = prefs().font_size + 3;
}

void Cycle::draw_contents() {
    const Pens &p = pens();
    // the cycle mark: a looping arrow at the left, a separator
    int X = mleft(), Y = mtop(), H = mheight();
    fl_color(p[MPEN_TEXT]);
    fl_arc(X + 2, Y + H / 2 - 5, 10, 10, 60, 360);
    fl_polygon(X + 9, Y + H / 2 - 7, X + 13, Y + H / 2 - 4, X + 9, Y + H / 2 - 1);
    fl_color(p[MPEN_SHADOW]);
    fl_yxline(X + 16, Y, Y + H - 1);
    fl_color(p[MPEN_SHINE]);
    fl_yxline(X + 17, Y, Y + H - 1);
    fl_color(selected_ ? p[MPEN_SHINE] : p[MPEN_TEXT]);
    fl_font(prefs().font, prefs().font_size);
    if (!entries_.empty())
        fl_draw(entries_[active_].c_str(), X + 20, Y, mwidth() - 20, H, (Fl_Align)(FL_ALIGN_CENTER | FL_ALIGN_CLIP));
}

// The pop-up (cycle.c MakePopupWin, RenderPopupItem): a borderless window below the cycle
// with every entry; the one under the pointer filled with the fill pen
static const int POPITEM_EXTRAWIDTH = 4, POPITEM_EXTRAHEIGHT = 2, CYCLE_IMAGE_W = 18;

class CyclePopup : public Fl_Menu_Window {
public:
    CyclePopup(int X, int Y, int W, int H, const std::vector<std::string> &entries, int item_w, int item_h,
               int offx, int offy)
        : Fl_Menu_Window(X, Y, W, H), entries_(entries), iw_(item_w), ih_(item_h), ox_(offx), oy_(offy) {
        end();
        clear_border();
        set_override();
    }
    int item_at(int xr, int yr) const {
        int lx = xr - x() - ox_, ly = yr - y() - oy_;
        if (lx < 0 || lx >= iw_ || ly < 0) return -1;
        int i = ly / ih_;
        return i < (int)entries_.size() ? i : -1;
    }
    void highlight(int i) { if (i != active_) { active_ = i; redraw(); } }
    int highlighted() const { return active_; }
    void draw() override {
        const Pens &p = pens();
        draw_imspec(prefs().popup_back, 0, 0, w(), h());   // MUII_PopupBack
        draw_frame(std_frame(Frame_PopUp), 0, 0, w(), h());
        fl_font(prefs().font, prefs().font_size);
        for (int i = 0; i < (int)entries_.size(); i++) {
            int x1 = ox_, y1 = oy_ + i * ih_;
            if (i == active_) {
                int off = 0;
                if (prefs().cycle_menu_recessed) {
                    fl_color(p[MPEN_SHADOW]);
                    fl_yxline(x1, y1, y1 + ih_ - 1);
                    fl_xyline(x1 + 1, y1, x1 + iw_ - 2);
                    fl_color(p[MPEN_SHINE]);
                    fl_yxline(x1 + iw_ - 1, y1, y1 + ih_ - 1);
                    fl_xyline(x1 + 1, y1 + ih_ - 1, x1 + iw_ - 2);
                    off = 1;
                }
                fl_color(p[MPEN_FILL]);
                fl_rectf(x1 + off, y1 + off, iw_ - 2 * off, ih_ - 2 * off);
            }
            fl_color(p[MPEN_TEXT]);
            fl_draw(entries_[i].c_str(), x1, y1, iw_, ih_, FL_ALIGN_CENTER);
        }
    }

private:
    std::vector<std::string> entries_;
    int iw_, ih_, ox_, oy_, active_ = -1;
};

void Cycle::close_popup() {
    if (!popup_) return;
    popup_->hide();
    delete popup_;
    popup_ = nullptr;
}

bool Cycle::open_popup() {
    if ((int)entries_.size() < prefs().cycle_menu_min || !window() || popup_) return false;
    const FrameSpec &pf = std_frame(Frame_PopUp);
    FrameBorders b = frame_borders(pf.type, pf.down);
    int item_w = mwidth() - CYCLE_IMAGE_W + POPITEM_EXTRAWIDTH;
    int item_h = prefs().font_size + 3 + POPITEM_EXTRAHEIGHT + (prefs().cycle_menu_recessed ? 2 : 0);
    int offx = pf.inner_left + b.left, offy = pf.inner_top + b.top;
    int ww = item_w + offx + pf.inner_right + b.right;
    int wh = item_h * (int)entries_.size() + offy + pf.inner_bottom + b.bottom;
    int wx = window()->x_root() + mleft() + CYCLE_IMAGE_W - offx - POPITEM_EXTRAWIDTH / 2;
    int wy = prefs().cycle_menu_position == 0
                 ? window()->y_root() + y() + h()   // below
                 : window()->y_root() + mtop() - offy - POPITEM_EXTRAHEIGHT / 2 - active_ * item_h;   // on the active entry
    int sx, sy, sw, sh;
    Fl::screen_xywh(sx, sy, sw, sh);
    if (wy + wh > sy + sh) wy = std::max(sy, window()->y_root() + y() - wh);   // WA_AutoAdjust
    Fl_Group *saved = Fl_Group::current();
    Fl_Group::current(nullptr);
    popup_ = new CyclePopup(wx, wy, ww, wh, entries_, item_w, item_h, offx, offy);
    Fl_Group::current(saved);
    popup_->show();
    return true;
}

int Cycle::handle(int e) {
    switch (e) {
    case FL_UNFOCUS:
        if (popup_ && key_popup_) close_popup();
        return Area::handle(e);
    case FL_KEYBOARD: {
        // the active object (cycle.c): Press opens the pop-up (with too few entries: the
        // next one), in it picks the highlighted entry; Down / Up the next / the one before
        // (wrapping when closed), Page up / Top the first, Page down / Bottom the last;
        // Close window closes the pop-up
        if (Fl::focus() != this || entries_.empty()) return 0;
        int n = (int)entries_.size();
        MuiKey k = muikey();
        int old = popup_ ? popup_->highlighted() : active_, now = old;
        switch (k) {
        case MUIKEY_WINDOW_CLOSE:
            if (popup_) { close_popup(); return 1; }
            return 0;
        case MUIKEY_PRESS:
            if (!popup_) {
                if (open_popup()) { key_popup_ = true; popup_->highlight(active_); return 1; }
                now = (now + 1) % n;   // too few entries: as Down
                break;
            }
            {
                int i = popup_->highlighted();
                close_popup();
                if (i >= 0 && i != active_) { active_ = i; redraw(); if (on_change) on_change(active_); }
            }
            return 1;
        case MUIKEY_DOWN: now = now < n - 1 ? now + 1 : (popup_ ? now : 0); break;
        case MUIKEY_UP: now = now > 0 ? now - 1 : (popup_ ? now : n - 1); break;
        case MUIKEY_PAGEUP: case MUIKEY_TOP: now = 0; break;
        case MUIKEY_PAGEDOWN: case MUIKEY_BOTTOM: now = n - 1; break;
        default: return 0;
        }
        if (now != old) {
            if (popup_) popup_->highlight(now);
            else { active_ = now; redraw(); if (on_change) on_change(active_); }
        }
        return 1;
    }
    case FL_PUSH: {
        if (Fl::event_button() != FL_LEFT_MOUSE) return 0;
        // a press right of the cycle image opens the pop-up, if there are entries enough
        // (cycle_menu_min_entries 2; the menu below the gadget: CYCLE_MENU_POSITION_BELOW)
        if (Fl::event_x() > mleft() + CYCLE_IMAGE_W && open_popup()) { key_popup_ = false; return 1; }
        selected(true);
        return 1;
    }
    case FL_DRAG:
        if (popup_) { popup_->highlight(popup_->item_at(Fl::event_x_root(), Fl::event_y_root())); return 1; }
        selected(Fl::event_inside(this));
        return 1;
    case FL_RELEASE:
        if (popup_) {
            // released over an entry: that one; elsewhere: nothing changes
            int i = popup_->item_at(Fl::event_x_root(), Fl::event_y_root());
            close_popup();
            if (i >= 0 && i != active_) {
                active_ = i;
                redraw();
                if (on_change) on_change(active_);
            }
            return 1;
        }
        if (selected_ && !entries_.empty()) {
            // Shift steps backwards
            int n = (int)entries_.size();
            active_ = (active_ + (Fl::event_state(FL_SHIFT) ? n - 1 : 1)) % n;
            if (on_change) on_change(active_);
        }
        selected(false);
        return 1;
    }
    return Area::handle(e);
}

// ============================================================================================
// Checkmark
// ============================================================================================
Checkmark::Checkmark(bool on) : Area(Frame_ImageButton) {
    background(MPEN_BACKGROUND);
    input_mode(Input_Toggle);
    selected_ = on;
}

void Checkmark::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = defw = maxw = 18;
    minh = defh = maxh = 13;
}

void Checkmark::draw_contents() {
    // the tick in the text pen (the background stays the button's)
    if (!selected_) return;
    int X = mleft(), Y = mtop(), W = mwidth(), H = mheight();
    fl_color(pens()[MPEN_TEXT]);
    fl_line_style(FL_SOLID, 2);
    fl_line(X + 2, Y + H / 2, X + W / 2 - 1, Y + H - 2, X + W - 2, Y + 1);
    fl_line_style(0);
}

// ============================================================================================
// List
// ============================================================================================
List::List() : Area(Frame_ReadList) {
    background(MPEN_SHINE);   // configdata: the list background ("0:128" on a white screen pen)
    begin();
    prop_ = new Scrollbar(false);
    end();
    prop_->on_change = [this](int f) { top_ = f; redraw(); };
}

void List::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = 80; defw = 300; maxw = MAXMAX;
    minh = line_h() * 3; defh = line_h() * 12; maxh = MAXMAX;
}

void List::resize(int X, int Y, int W, int H) {
    Fl_Widget::resize(X, Y, W, H);
    prop_->resize(x() + w() - add_right() - 14, mtop(), 14, mheight());
    layout_prop();
}

void List::columns(const std::vector<std::string> &titles, const std::vector<bool> &right) {
    titles_.clear();
    for (auto &t : titles) titles_.push_back(tr(t));
    right_ = right;
    right_.resize(titles_.size(), false);
    measure_columns();
}

void List::measure_columns() {
    colw_.assign(titles_.size(), 0);
    fl_font(prefs().list_font, prefs().list_size());
    for (size_t c = 0; c < titles_.size(); c++) colw_[c] = (int)fl_width(titles_[c].c_str()) + 10;
    for (auto &l : lines_)
        for (size_t c = 0; c < l.cols.size() && c < colw_.size(); c++)
            colw_[c] = std::max(colw_[c], (int)fl_width(l.cols[c].c_str()) + 10);
}

void List::layout_prop() {
    int n = (int)lines_.size(), v = visible_lines();
    top_ = std::clamp(top_, 0, std::max(0, n - v));
    prop_->set(std::max(n, v), v, top_);
}

void List::active(int a) {
    active_ = std::clamp(a, -1, (int)lines_.size() - 1);
    if (active_ >= 0) {   // kept in view
        if (active_ < top_) top_ = active_;
        if (active_ >= top_ + visible_lines()) top_ = active_ - visible_lines() + 1;
        layout_prop();
    }
    redraw();
}

// the list's background: an input or a read-only list's pattern (MUII_ListBack / ReadListBack)
void List::draw_background(int X, int Y, int W, int H) {
    draw_imspec(std_ == Frame_InputList ? prefs().list_back : prefs().readlist_back, X, Y, W, H);
}

void List::draw_contents() {
    const Pens &p = pens();
    int lh = line_h(), W = mwidth() - 16;
    fl_font(prefs().list_font, prefs().list_size());
    if (!titles_.empty()) {
        // the title row and the columns, divided by bars
        int th = title_h(), x = mleft();
        fl_color(p[MPEN_BACKGROUND]);
        fl_rectf(mleft(), mtop(), W, th);
        for (size_t c = 0; c < titles_.size(); c++) {
            int cw = c + 1 == titles_.size() ? std::max(colw_[c], mleft() + W - x) : colw_[c];
            fl_color(p[MPEN_TEXT]);
            fl_draw(titles_[c].c_str(), x + 4, mtop(), cw - 8, th, (Fl_Align)((right_[c] ? FL_ALIGN_RIGHT : FL_ALIGN_LEFT) | FL_ALIGN_CLIP));
            x += cw;
        }
        fl_color(p[MPEN_SHADOW]);
        fl_xyline(mleft(), mtop() + th - 1, mleft() + W - 1);
        fl_push_clip(mleft(), mtop() + th, W, mheight() - th);
        for (int i = top_; i < (int)lines_.size() && (i - top_) * lh < mheight() - th; i++) {
            int y = mtop() + th + (i - top_) * lh;
            bool act = i == active_;
            if (act) draw_imspec(prefs().list_cursor, mleft(), y, W, lh);   // MUII_ListCursor
            int cx = mleft();
            auto &l = lines_[i];
            for (size_t c = 0; c < titles_.size(); c++) {
                int cw = colw_[c];
                if (c < l.cols.size()) {
                    fl_color(act ? p[MPEN_SHINE] : p[MPEN_TEXT]);
                    fl_draw(l.cols[c].c_str(), cx + 4, y, cw - 8, lh, (Fl_Align)((right_[c] ? FL_ALIGN_RIGHT : FL_ALIGN_LEFT) | FL_ALIGN_CLIP));
                }
                cx += cw;
            }
        }
        // the bars between the columns
        int bx = mleft();
        for (size_t c = 0; c + 1 < titles_.size(); c++) {
            bx += colw_[c];
            fl_color(p[MPEN_SHADOW]);
            fl_yxline(bx - 1, mtop(), mtop() + mheight() - 1);
        }
        fl_pop_clip();
        int bx2 = mleft();
        for (size_t c = 0; c + 1 < titles_.size(); c++) {
            bx2 += colw_[c];
            fl_color(p[MPEN_SHADOW]);
            fl_yxline(bx2 - 1, mtop(), mtop() + th - 1);
        }
        return;
    }
    for (int i = top_; i < (int)lines_.size() && (i - top_) * lh < mheight(); i++) {
        int y = mtop() + (i - top_) * lh;
        bool act = i == active_;
        if (act) draw_imspec(prefs().list_cursor, mleft(), y, W, lh);   // MUII_ListCursor
        auto &l = lines_[i];
        int infow = l.info.empty() ? 0 : (int)fl_width(l.info.c_str()) + 8;
        fl_color(act ? p[MPEN_SHINE] : p[MPEN_TEXT]);
        fl_draw(l.text.c_str(), mleft() + 3, y, W - infow - 6, lh, (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP));
        if (infow) {
            fl_color(act ? p[MPEN_SHINE] : (l.highlight ? p[MPEN_SHINE] : p[MPEN_TEXT]));
            if (l.highlight && !act) {   // "Drawer": in the highlight colours (white on the fill pen)
                fl_color(p[MPEN_HALFSHADOW]);
                fl_rectf(mleft() + W - infow, y + 1, infow - 2, lh - 2);
                fl_color(p[MPEN_SHINE]);
            }
            fl_draw(l.info.c_str(), mleft() + W - infow, y, infow - 4, lh, FL_ALIGN_RIGHT);
        }
    }
}

int List::handle(int e) {
    switch (e) {
    case FL_PUSH: {
        if (Fl::event_x() >= prop_->x()) return Area::handle(e);
        take_focus();
        if (!titles_.empty() && Fl::event_y() < mtop() + title_h()) {
            // a title clicked: its column (the list sorts by it)
            int x = mleft();
            for (size_t c = 0; c < titles_.size(); c++) {
                x += colw_[c];
                if (Fl::event_x() < x || c + 1 == titles_.size()) { if (on_title) on_title((int)c); break; }
            }
            return 1;
        }
        int i = top_ + (Fl::event_y() - mtop() - title_h()) / line_h();
        if (i < 0 || i >= (int)lines_.size()) return 1;
        bool dbl = Fl::event_clicks() && i == active_;
        active(i);
        if (dbl) { Fl::event_clicks(0); if (on_double) on_double(i); }
        else if (on_active) on_active(i);
        return 1;
    }
    case FL_MOUSEWHEEL:
        top_ += Fl::event_dy() * 3;
        layout_prop();
        redraw();
        return 1;
    case FL_FOCUS: case FL_UNFOCUS: focus_changed(); return 1;
    case FL_KEYBOARD: {
        // list.c: Up / Down a line, Page up / Page down a page, Top / Bottom the ends;
        // Press: as a double click (the listview's)
        if (Fl::focus() != this) return 0;
        int page = std::max(1, visible_lines() - 1), a = active_;
        switch (muikey()) {
        case MUIKEY_UP: a = a < 0 ? 0 : a - 1; break;
        case MUIKEY_DOWN: a = a + 1; break;
        case MUIKEY_PAGEUP: a = a < 0 ? 0 : a - page; break;
        case MUIKEY_PAGEDOWN: a = a + page; break;
        case MUIKEY_TOP: a = 0; break;
        case MUIKEY_BOTTOM: a = (int)lines_.size() - 1; break;
        case MUIKEY_PRESS:
            if (active_ >= 0 && on_double) { on_double(active_); return 1; }
            return 0;
        default: return 0;
        }
        if (lines_.empty()) return 1;
        active(std::clamp(a, 0, (int)lines_.size() - 1));
        if (on_active && active_ >= 0) on_active(active_);
        return 1;
    }    }
    return Area::handle(e);
}

// ============================================================================================
// Gauge
// ============================================================================================
// ============================================================================================
// Slider
// ============================================================================================
Slider::Slider(int min, int max, int value, bool horiz)
    : Area(Frame_Slider), min_(min), max_(std::max(min, max)), horiz_(horiz) {
    value_ = std::clamp(value, min_, max_);
    background(&Prefs::slider_back);   // MUII_SliderBack "0:128"
}

void Slider::value(int v) {
    v = std::clamp(v, min_, max_);
    if (v == value_) return;
    value_ = v;
    redraw();
    if (on_change) on_change(v);
}

// CalcKnobDimensions: as wide as the widest number, with the knob frame around it
int Slider::knob_w() const {
    fl_font(prefs().font, prefs().font_size);
    int wmax = std::max((int)fl_width(text(min_).c_str()), (int)fl_width(text(max_).c_str()));
    const FrameSpec &k = std_frame(Frame_Knob);
    FrameBorders b = frame_borders(k.type, k.down);
    return wmax + b.left + b.right + k.inner_left + k.inner_right;
}

int Slider::knob_h() const {
    const FrameSpec &k = std_frame(Frame_Knob);
    FrameBorders b = frame_borders(k.type, k.down);
    return prefs().font_size + 2 + b.top + b.bottom + k.inner_top + k.inner_bottom;
}

// the room the knob moves in (scale_length): the width or height less the knob's
int Slider::scale_length() const { return horiz_ ? mwidth() - knob_w() : mheight() - knob_h(); }

// MUIM_Numeric_ValueToScale (a vertical slider has its minimum at the top)
int Slider::knob_pos() const {
    int scale = scale_length();
    if (max_ == min_ || scale <= 0) return 0;
    return (int)((long)scale * (value_ - min_) / (max_ - min_));
}

void Slider::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    if (horiz_) {
        minw = knob_w() + 1;
        defw = knob_w() * 4 + 2;
        maxw = MAXMAX;
        minh = defh = maxh = knob_h();
    } else {
        minw = defw = maxw = knob_w();
        minh = knob_h() + 1;
        defh = knob_h() * 4 + 2;
        maxh = MAXMAX;
    }
}

void Slider::draw_contents() {
    int kx = mleft(), ky = mtop(), kw = horiz_ ? knob_w() : mwidth(), kh = horiz_ ? mheight() : knob_h();
    if (horiz_) kx += knob_pos();
    else ky += knob_pos();
    const FrameSpec &k = std_frame(Frame_Knob);
    draw_imspec(prefs().slider_knob, kx, ky, kw, kh);   // MUII_SliderKnob
    draw_frame(k.type, k.down != dragging_, kx, ky, kw, kh);
    fl_font(prefs().font, prefs().font_size);
    fl_color(pens()[MPEN_TEXT]);
    fl_draw(text(value_).c_str(), kx, ky, kw, kh, FL_ALIGN_CENTER);
}

int Slider::handle(int e) {
    if (disabled_) return Fl_Group::handle(e);
    int ev = horiz_ ? Fl::event_x() : Fl::event_y();   // the pointer along the slider
    int start = horiz_ ? mleft() : mtop();
    switch (e) {
    case FL_PUSH: {
        if (Fl::event_button() != FL_LEFT_MOUSE) return 0;
        take_focus();
        int kp = start + knob_pos(), kl = horiz_ ? knob_w() : knob_h();
        if (ev >= kp && ev < kp + kl) {
            // on the knob: dragging starts
            dragging_ = true;
            grab_dx_ = ev - kp;
            redraw();
        } else value(value_ + (ev < kp ? -1 : 1));   // beside it: a step towards the click
        return 1;
    }
    case FL_DRAG:
        if (dragging_) {
            int scale = scale_length();
            if (scale > 0) {
                int pos = std::clamp(ev - grab_dx_ - start, 0, scale);
                value(min_ + (int)(((long)pos * (max_ - min_) + scale / 2) / scale));
            }
        }
        return 1;
    case FL_RELEASE:
        if (dragging_) { dragging_ = false; redraw(); }
        return 1;
    case FL_FOCUS: case FL_UNFOCUS: focus_changed(); return 1;
    case FL_KEYBOARD: {
        // numeric.c: Left / Up one less, Right / Down one more; Page down / Word right ten
        // more (one with fewer than ten steps), Page up / Word left ten less; Top / Line start
        // the minimum, Bottom / Line end the maximum
        if (Fl::focus() != this) return 0;
        int step = max_ - min_ < 10 ? 1 : 10;
        switch (muikey()) {
        case MUIKEY_LEFT: case MUIKEY_UP: value(value_ - 1); return 1;
        case MUIKEY_RIGHT: case MUIKEY_DOWN: value(value_ + 1); return 1;
        case MUIKEY_PAGEDOWN: case MUIKEY_WORDRIGHT: value(value_ + step); return 1;
        case MUIKEY_PAGEUP: case MUIKEY_WORDLEFT: value(value_ - step); return 1;
        case MUIKEY_TOP: case MUIKEY_LINESTART: value(min_); return 1;
        case MUIKEY_BOTTOM: case MUIKEY_LINEEND: value(max_); return 1;
        default: return 0;
        }
    }
    case FL_MOUSEWHEEL:
        value(value_ + (horiz_ ? -Fl::event_dy() : Fl::event_dy()));
        return 1;
    }
    return Area::handle(e);
}

// ============================================================================================
// Balance
// ============================================================================================
Balance::Balance() : Area(Frame_None) { background(-1); }

bool Balance::horiz() const {
    auto *g = dynamic_cast<Group *>(parent());
    return g && g->horizontal();
}

void Balance::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    if (horiz()) { minw = defw = maxw = 3; minh = defh = 0; maxh = MAXMAX; }
    else { minh = defh = maxh = 3; minw = defw = 0; maxw = MAXMAX; }
}

void Balance::draw_contents() {
    // three lines: text and shine pens round the fill pen, the other way round while dragged
    const Pens &p = pens();
    Fl_Color c1 = p[state_ ? MPEN_SHINE : MPEN_TEXT], c2 = p[state_ ? MPEN_TEXT : MPEN_SHINE];
    if (horiz()) {
        fl_color(c1); fl_yxline(mleft(), mtop(), mtop() + mheight() - 1);
        fl_color(p[MPEN_FILL]); fl_yxline(mleft() + 1, mtop(), mtop() + mheight() - 1);
        fl_color(c2); fl_yxline(mleft() + 2, mtop(), mtop() + mheight() - 1);
    } else {
        fl_color(c1); fl_xyline(mleft(), mtop(), mleft() + mwidth() - 1);
        fl_color(p[MPEN_FILL]); fl_xyline(mleft(), mtop() + 1, mleft() + mwidth() - 1);
        fl_color(c2); fl_xyline(mleft(), mtop() + 2, mleft() + mwidth() - 1);
    }
}

void Balance::move(int pos) {
    // set_weight_2 / set_weight_all: the total weight of both sides shared by where the
    // pointer is between the bounds (each side down to its objects' smallest sizes)
    long total = lsum_ + rsum_;
    if (total <= 0) return;
    long wa;
    if (pos >= second_bound_) wa = total;
    else if (pos <= first_bound_) wa = 0;
    else wa = std::min(total, (long)(pos - first_bound_ + 1) * total / (second_bound_ - first_bound_ + 1));
    long wb = total - wa;
    bool h = horiz();
    auto set = [h](Area *a, long w) { if (h) a->horiz_weight((int)w); else a->vert_weight((int)w); };
    for (size_t i = 0; i < before_.size(); i++) set(before_[i], lsum_ ? lw_[i] * wa / lsum_ : wa / (long)before_.size());
    for (size_t i = 0; i < after_.size(); i++) set(after_[i], rsum_ ? rw_[i] * wb / rsum_ : wb / (long)after_.size());
    if (auto *g = dynamic_cast<Group *>(parent())) g->relayout();
}

int Balance::handle(int e) {
    auto *g = dynamic_cast<Group *>(parent());
    bool h = horiz();
    int pos = h ? Fl::event_x() : Fl::event_y();
    switch (e) {
    case FL_ENTER: case FL_LEAVE: case FL_MOVE: return 1;
    case FL_PUSH: {
        if (Fl::event_button() != FL_LEFT_MOUSE || !g) return 0;
        // the objects before and after it: with Shift only the next ones
        before_.clear(); after_.clear(); lw_.clear(); rw_.clear();
        bool shift = Fl::event_state(FL_SHIFT);
        bool past = false;
        for (int i = 0; i < g->children(); i++) {
            auto *a = dynamic_cast<Area *>(g->child(i));
            if (!a || !a->visible()) continue;
            if (a == this) { past = true; continue; }
            if (dynamic_cast<Balance *>(a)) continue;
            if (!past) { if (shift) before_.clear(); before_.push_back(a); }
            else { after_.push_back(a); if (shift) break; }
        }
        if (before_.empty() || after_.empty()) return 1;
        lsum_ = rsum_ = 0;
        for (auto *a : before_) { lw_.push_back(a->weight(h)); lsum_ += a->weight(h); }
        for (auto *a : after_) { rw_.push_back(a->weight(h)); rsum_ += a->weight(h); }
        // the bounds: the first objects' start plus their smallest sizes, and so on the other side
        int sp = h ? g->hsp() : g->vsp();
        int mins_before = 0, mins_after = 0;
        for (auto *a : before_) { int mw, mh, dw, dh, xw, xh; a->min_max(mw, mh, dw, dh, xw, xh); mins_before += (h ? mw : mh) + sp; }
        for (auto *a : after_) { int mw, mh, dw, dh, xw, xh; a->min_max(mw, mh, dw, dh, xw, xh); mins_after += (h ? mw : mh) + sp; }
        Area *first = before_.front(), *last = after_.back();
        first_bound_ = (h ? first->x() : first->y()) + mins_before;
        second_bound_ = (h ? last->x() + last->w() : last->y() + last->h()) - mins_after;
        state_ = shift ? 2 : 1;
        if (prefs().balancing_look == 0) g->sketch(true);
        redraw();
        return 1;
    }
    case FL_DRAG:
        if (state_) move(pos);
        return 1;
    case FL_RELEASE:
        if (state_) {
            state_ = 0;
            if (g) { g->sketch(false); g->relayout(); }
            redraw();
        }
        return 1;
    }
    return Area::handle(e);
}

void Group::draw_sketch() {
    // draw_object_frame: each object's outline in the text pen (with its diagonals when it
    // can change size) over the group's background
    fl_color(pens()[bg_pen_ >= 0 ? bg_pen_ : MPEN_BACKGROUND]);
    fl_rectf(mleft(), mtop() + title_h(), mwidth(), mheight() - title_h());
    bool h = horizontal();
    fl_color(pens()[MPEN_TEXT]);
    for (int i = 0; i < children(); i++) {
        auto *a = dynamic_cast<Area *>(child(i));
        if (!a || !a->visible()) continue;
        if (auto *b = dynamic_cast<Balance *>(a)) { draw_child(*b); continue; }
        int mw, mh, dw, dh, xw, xh;
        a->min_max(mw, mh, dw, dh, xw, xh);
        bool fixed = h ? mw == xw : mh == xh;
        fl_color(pens()[MPEN_TEXT]);
        int x1 = a->x(), y1 = a->y(), x2 = a->x() + a->w() - 1, y2 = a->y() + a->h() - 1;
        fl_rect(x1, y1, a->w(), a->h());
        if (!fixed) { fl_line(x1, y1, x2, y2); fl_line(x2, y1, x1, y2); }
    }
}

// ============================================================================================
// Scale
// ============================================================================================
Scale::Scale() : Area(Frame_None) { background(-1); }

void Scale::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    fl_font(prefs().font, prefs().font_size);
    minw = defw = (int)fl_width("0%100%");   // minlabel
    maxw = MAXMAX;
    minh = defh = maxh = prefs().font_size + 4;
}

void Scale::draw_contents() {
    int L = mleft(), T = mtop(), W = mwidth(), R = L + W - 1;
    fl_font(prefs().font, prefs().font_size);
    fl_color(pens()[MPEN_TEXT]);
    fl_xyline(L, T + 3, R);   // the main bar
    // the layout: 0..100, 0..50..100, ... as many divisions as the labels allow
    int i;
    char buf[16];
    for (i = 2; i < W; i *= 2) {
        int total = 0;
        bool too_big = false;
        for (int j = 0; j <= i; j++) {
            snprintf(buf, sizeof buf, "%d%%", j);
            total += (int)fl_width(buf);
            if (total > 3 * W / 8) { too_big = true; break; }
        }
        if (too_big) break;
    }
    int ty = T + 4 + fl_height() - fl_descent();
    bool drawpct = true;
    for (int k = 0; k <= i; k++) {
        int x = L + W * k / i;
        if (k == 0) {
            fl_draw("0%", L, ty);
            fl_yxline(L, T, T + 3);
            drawpct = false;
        } else if (k == i) {
            fl_draw("100%", R + 1 - (int)fl_width("100%"), ty);
            fl_yxline(R, T, T + 3);
            drawpct = false;
        } else if (drawpct) {
            snprintf(buf, sizeof buf, "%d%%", k * 100 / i);
            fl_draw(buf, x - (int)fl_width(buf) / 2, ty);
            fl_yxline(x, T, T + 3);
            drawpct = false;
        } else {
            fl_yxline(x, T + 1, T + 3);
            drawpct = true;
        }
    }
}

Gauge::Gauge() : Area(Frame_Gauge) {
    background(MPEN_BACKGROUND);
    inner(1, 1, 1, 1);
}

void Gauge::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    if (!horiz_) {   // a vertical gauge: narrow, as high as it is let
        minw = defw = maxw = prefs().font_size + 6;
        minh = 20; defh = 60; maxh = MAXMAX;
        return;
    }
    minw = 60; defw = 260; maxw = MAXMAX;
    minh = defh = maxh = prefs().font_size + 6;
}

void Gauge::draw_contents() {
    const Pens &p = pens();
    if (!horiz_) {
        int fh = (int)((long double)mheight() * std::clamp(cur_, 0LL, max_) / max_);
        fl_color(p[MPEN_FILL]);
        fl_rectf(mleft(), mtop() + mheight() - fh, mwidth(), fh);
        return;
    }
    int fw = (int)((long double)mwidth() * std::clamp(cur_, 0LL, max_) / max_);
    fl_color(p[MPEN_FILL]);
    fl_rectf(mleft(), mtop(), fw, mheight());
    if (!info_.empty()) {
        fl_font(prefs().font, prefs().font_size);
        fl_color(p[MPEN_TEXT]);
        fl_draw(info_.c_str(), mleft(), mtop(), mwidth(), mheight(), FL_ALIGN_CENTER);
    }
}

// ============================================================================================
// ImageButton
// ============================================================================================
namespace {
// Move / Draw of the RastPort
struct Pen { int x = 0, y = 0; void move(int X, int Y) { x = X; y = Y; } void draw(int X, int Y) { fl_line(x, y, X, Y); x = X; y = Y; } };

void popup_draw(int left, int top, int width, int height) {
    Pen r;
    height -= 3;
    fl_color(pens()[MPEN_TEXT]);
    int cx = width / 2;
    r.move(left + 1, top); r.draw(left + width - cx, top + height - 1);
    r.move(left, top); r.draw(left + width - cx - 1, top + height - 1);
    r.move(left + width - 1 - 1, top); r.draw(left + cx - 1, top + height - 1);
    r.move(left + width - 1, top); r.draw(left + cx, top + height - 1);
    int bottom = top + height - 1 + 3, right = left + width - 1;
    r.move(left, bottom - 2); r.draw(right, bottom - 2);
    r.move(left, bottom - 1); r.draw(right, bottom - 1);
}

void popfile_draw(int left, int top, int width, int height) {
    Pen r;
    int right = left + width - 1, bottom = top + height - 1;
    int edgex = left + width * 5 / 8, edgey = top + height * 5 / 8;
    fl_color(pens()[MPEN_TEXT]);
    r.move(left, top); r.draw(left, bottom);
    r.move(left + 1, top); r.draw(left + 1, bottom);
    r.move(left, bottom); r.draw(right, bottom);
    r.move(left, bottom - 1); r.draw(right, bottom - 1);
    r.move(right, bottom - 1); r.draw(right, edgey);
    r.move(right - 1, bottom - 1); r.draw(right - 1, edgey);
    r.move(right, edgey - 1); r.draw(edgex, edgey - 1); r.draw(edgex, top); r.draw(left + 2, top);
    r.move(left + 2, top + 1); r.draw(edgex - 1, top + 1);
    r.move(edgex + 1, top); r.draw(right, edgey - 1);
}

void popdrawer_draw(int left, int top, int width, int height) {
    Pen r;
    int right = left + width - 1, bottom = top + height - 1;
    int halfx = (left + right) / 2, halfy = (top + bottom) / 2, quartery = top + height / 4;
    fl_color(pens()[MPEN_TEXT]);
    r.move(left, quartery); r.draw(left, bottom);
    r.move(left + 1, quartery); r.draw(left + 1, bottom); r.draw(right, bottom); r.draw(right, halfy);
    r.draw(halfx, halfy); r.draw(halfx, quartery); r.draw(left + 1, quartery);
    r.move(halfx, quartery - 1); r.draw(halfx + 2, top); r.draw(right - 2, top); r.draw(right, quartery - 1);
    r.draw(right, halfy);
}
}  // namespace

ImageButton *PopButton(PopImage which) {
    // the vector images are 10 x 11 at the least (vector_table)
    auto *b = new ImageButton(10, 11, [which](int x, int y, int w, int h, bool) {
        switch (which) {
        case Pop_Up: popup_draw(x, y, w, h); break;
        case Pop_File: popfile_draw(x, y, w, h); break;
        case Pop_Drawer: popdrawer_draw(x, y, w, h); break;
        }
    });
    return b;
}
ImageButton::ImageButton(int w, int h, std::function<void(int, int, int, int, bool)> image)
    : Area(Frame_ImageButton), iw_(w), ih_(h), image_(std::move(image)) {
    background(MPEN_BACKGROUND);
    input_mode(Input_RelVerify);
}

void ImageButton::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = defw = maxw = iw_;
    minh = defh = maxh = ih_;
}

void ImageButton::draw_contents() {
    if (image_) image_(mleft(), mtop(), mwidth(), mheight(), selected_);
}

// ============================================================================================
// Window
// ============================================================================================
Window::Window(const std::string &title, Area *r) : Fl_Double_Window(100, 100), title_(tr_text(title)) {
    label(title_.c_str());
    color(pens()[MPEN_BACKGROUND]);
    end();
    if (r) root(r);
}

void Window::root(Area *r) {
    if (root_) remove(root_);
    root_ = r;
    add(r);
    fit();
}

void Window::fit() {
    if (!root_) return;
    int a, b, c, d, e, f;
    root_->min_max(a, b, c, d, e, f);
    auto &p = prefs();
    int ew = p.window_left + p.window_right, eh = p.window_top + p.window_bottom;
    size(c + ew, d + eh);
    size_range(a + ew, b + eh, e >= MAXMAX ? 0 : e + ew, f >= MAXMAX ? 0 : f + eh);
}

void Window::resize(int X, int Y, int W, int H) {
    Fl_Double_Window::resize(X, Y, W, H);   // the X window too (and the asked-for position)
    if (root_) {
        auto &p = prefs();
        root_->resize(p.window_left, p.window_top, W - p.window_left - p.window_right, H - p.window_top - p.window_bottom);
    }
}

// the first text field inside (MUIA_Window_ActiveObject of a window with a string)
static Fl_Widget *first_input(Fl_Group *g) {
    for (int i = 0; i < g->children(); i++) {
        Fl_Widget *c = g->child(i);
        if (!c->visible() || !c->active()) continue;
        if (auto *in = dynamic_cast<Fl_Input *>(c)) { if (!in->readonly()) return c; continue; }
        if (auto *cg = c->as_group())
            if (Fl_Widget *f = first_input(cg)) return f;
    }
    return nullptr;
}

// the objects the keyboard can go to, in order (the cycle chain)
static void chain(Fl_Group *g, std::vector<Fl_Widget *> &out) {
    for (int i = 0; i < g->children(); i++) {
        Fl_Widget *c = g->child(i);
        if (!c->visible() || !c->active()) continue;
        if (auto *a = dynamic_cast<Area *>(c); a && a->accepts_focus() && a->children() == 0) { out.push_back(c); continue; }
        if (dynamic_cast<Fl_Input *>(c)) { out.push_back(c); continue; }
        if (auto *cg = c->as_group()) chain(cg, out);
    }
}

Window::~Window() = default;

int Window::handle(int e) {
    if (Fl::focus() && Fl::focus() != this && Fl::focus()->inside(this) &&
        (!last_focus_ || last_focus_->widget() != Fl::focus()))
        last_focus_ = std::make_unique<Fl_Widget_Tracker>(Fl::focus());
    if (e == FL_FOCUS && (!Fl::focus() || !Fl::focus()->inside(this)) && last_focus_ && last_focus_->exists() &&
        last_focus_->widget()->visible_r()) {
        // back to the window: its active object again
        last_focus_->widget()->take_focus();
        return 1;
    }
    if ((e == FL_KEYBOARD || e == FL_SHORTCUT) && (Fl::event_state() & FL_META) && !menus_.empty() &&
        Fl::event_key() < 128 && menu_key(Fl::event_key()))
        return 1;
    if (e == FL_KEYBOARD) {
        // window.c: Next / Previous gadget move along the cycle chain (wrapping; with no active
        // object the first / the last), Gadget off: no active object; Close window: as the
        // close gadget
        MuiKey k = muikey();
        if (k == MUIKEY_GADGET_NEXT || k == MUIKEY_GADGET_PREV) {
            std::vector<Fl_Widget *> c;
            chain(this, c);
            if (c.empty()) return 1;
            Fl_Widget *f = Fl::focus();
            auto it = std::find_if(c.begin(), c.end(), [f](Fl_Widget *w) { return w == f || (f && f->inside(w)); });
            int n = (int)c.size(), i = it == c.end() ? -1 : (int)(it - c.begin());
            int next = i < 0 ? (k == MUIKEY_GADGET_NEXT ? 0 : n - 1) : (i + (k == MUIKEY_GADGET_NEXT ? 1 : n - 1)) % n;
            c[next]->take_focus();
            return 1;
        }
        if (k == MUIKEY_GADGET_OFF) { Fl::focus(this); redraw(); return 1; }
        if (k == MUIKEY_WINDOW_CLOSE) { do_callback(); return 1; }
        if (Fl::event_key() == FL_Tab) return 1;   // (no FLTK Tab navigation besides)
    }
    if (e == FL_FOCUS && (!Fl::focus() || !Fl::focus()->inside(this))) {
        // a window coming active has no active object, unless it has a text field: the
        // keyboard goes to that one (a requester's buttons are not pressed by a stray key)
        if (Fl_Widget *f = first_input(this)) { f->take_focus(); return 1; }
        Fl::focus(this);
        return 1;
    }
    int r = Fl_Double_Window::handle(e);
    if (getenv("LUNE_DEBUG") && (e == FL_PUSH || e == FL_RELEASE || e == FL_ENTER || e == FL_LEAVE))
        fprintf(stderr, "[%s] ev %d at %d,%d -> %d pushed=%p below=%p grab=%p modal=%p\n", label(), e, Fl::event_x(), Fl::event_y(), r,
                (void *)Fl::pushed(), (void *)Fl::belowmouse(), (void *)Fl::grab(), (void *)Fl::modal());
    return r;
}

void Window::draw() {
    if (damage() & ~FL_DAMAGE_CHILD) {
        // MUII_WindowBack "0:128" (a requester: MUII_RequesterBack "0:137")
        draw_imspec(background_spec(), 0, 0, w(), h());
    }
    draw_children();
}

// --------------------------------------------------------------------------------------------
// Window positions (window.c, MUICFG_Window_Positions): by the windows' IDs, kept while the
// program runs; with "save on exit" in ~/.config/rambler/lune.windows as well
// --------------------------------------------------------------------------------------------
namespace {
struct WinPos { int x, y, w, h; };
std::map<std::string, WinPos> &positions() {
    static std::map<std::string, WinPos> m;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::string path = prefs_path();
        path = path.substr(0, path.rfind('/')) + "/lune.windows";
        if (FILE *f = fopen(path.c_str(), "r")) {
            char id[256];
            WinPos p;
            while (fscanf(f, "%255s %d %d %d %d", id, &p.x, &p.y, &p.w, &p.h) == 5) m[id] = p;
            fclose(f);
        }
        atexit([] {
            if (prefs().window_positions != 2) return;
            std::string path = prefs_path();
            path = path.substr(0, path.rfind('/')) + "/lune.windows";
            if (FILE *f = fopen(path.c_str(), "w")) {
                for (auto &[k, p] : positions()) fprintf(f, "%s %d %d %d %d\n", k.c_str(), p.x, p.y, p.w, p.h);
                fclose(f);
            }
        });
    }
    return m;
}
}  // namespace

void Window::show() {
    if (!shown() && !id_.empty() && prefs().window_positions != 0) {
        auto &m = positions();
        auto it = m.find(id_);
        if (it != m.end()) {
            // within the window's limits now (its contents may have grown since)
            int minw = 20, minh = 20, maxw = 0, maxh = 0;
            get_size_range(&minw, &minh, &maxw, &maxh);
            int W = std::max(it->second.w, std::max(minw, 20)), H = std::max(it->second.h, std::max(minh, 20));
            if (maxw > 0) W = std::min(W, maxw);
            if (maxh > 0) H = std::min(H, maxh);
            resize(it->second.x, it->second.y, W, H);
        }
    }
    bool was = shown();
    Fl_Double_Window::show();
    if (!was && !menus_.empty()) publish_menu();
}

// ---------------------------------------------------------------------------------------------
// The menus (Menustrip): _RAMBLER_MENU on the window, one line per item - depth TAB id TAB
// flags TAB label TAB key; the window manager sends the item picked (_RAMBLER_MENU_PICK)
MenuEntry *Window::menu_entry(int id, std::vector<MenuEntry> *in) {
    for (auto &m : in ? *in : menus_) {
        if (m.id == id && m.children.empty()) return &m;
        if (MenuEntry *c = menu_entry(id, &m.children)) return c;
    }
    return nullptr;
}

void Window::publish_menu() {
    if (!shown() || !fl_display) return;
    std::string out;
    std::function<void(const std::vector<MenuEntry> &, int)> put = [&](const std::vector<MenuEntry> &v, int depth) {
        for (auto &m : v) {
            int f = m.flags | (m.exclude ? Menu_Toggle : 0);
            out += std::to_string(depth) + "\t" + std::to_string(m.id) + "\t" + std::to_string(f) + "\t" + tr(m.label) + "\t" +
                   (m.key ? std::string("Super+") + (char)toupper((unsigned char)m.key) : "") + "\n";
            put(m.children, depth + 1);
        }
    };
    put(menus_, 0);
    Atom prop = XInternAtom(fl_display, "_RAMBLER_MENU", False), utf8 = XInternAtom(fl_display, "UTF8_STRING", False);
    if (menus_.empty()) XDeleteProperty(fl_display, fl_xid(this), prop);
    else XChangeProperty(fl_display, fl_xid(this), prop, utf8, 8, PropModeReplace, (const unsigned char *)out.data(), (int)out.size());
    XFlush(fl_display);
}

namespace {
// the item picked in the screen bar: for a Lune window with menus (other windows' are their
// program's)
int menu_events(void *event, void *) {
    XEvent *e = (XEvent *)event;
    static Atom pick = XInternAtom(fl_display, "_RAMBLER_MENU_PICK", False);
    if (e->type != ClientMessage || e->xclient.message_type != pick) return 0;
    auto *w = dynamic_cast<Window *>(fl_find(e->xclient.window));
    if (!w || !w->on_menu) return 0;
    w->menu_pick((int)e->xclient.data.l[0]);
    return 1;
}
}  // namespace

void Window::menustrip(std::vector<MenuEntry> m) {
    static bool handler = false;
    if (!handler) { Fl::add_system_handler(menu_events, nullptr); handler = true; }
    menus_ = std::move(m);
    publish_menu();
}

bool Window::menu_checked(int id) {
    MenuEntry *m = menu_entry(id);
    return m && (m->flags & Menu_Checked);
}

void Window::menu_checked(int id, bool on) {
    MenuEntry *m = menu_entry(id);
    if (!m) return;
    if (on && m->exclude) {   // (the others of its group off)
        std::function<void(std::vector<MenuEntry> &)> off = [&](std::vector<MenuEntry> &v) {
            for (auto &o : v) { if (o.exclude == m->exclude) o.flags &= ~Menu_Checked; off(o.children); }
        };
        off(menus_);
    }
    m->flags = on ? m->flags | Menu_Checked : m->flags & ~Menu_Checked;
    publish_menu();
}

void Window::menu_disabled(int id, bool d) {
    if (MenuEntry *m = menu_entry(id)) { m->flags = d ? m->flags | Menu_Disabled : m->flags & ~Menu_Disabled; publish_menu(); }
}

void Window::menu_pick(int id) {
    MenuEntry *m = menu_entry(id);
    if (!m || (m->flags & Menu_Disabled)) return;
    if (m->exclude) menu_checked(id, true);
    else if (m->flags & Menu_Toggle) menu_checked(id, !(m->flags & Menu_Checked));
    if (on_menu) on_menu(id);
}

// Super+key: the item with that shortcut
bool Window::menu_key(int key) {
    std::function<MenuEntry *(std::vector<MenuEntry> &)> find = [&](std::vector<MenuEntry> &v) -> MenuEntry * {
        for (auto &m : v) {
            if (m.key && tolower((unsigned char)m.key) == tolower(key) && m.children.empty()) return &m;
            if (MenuEntry *c = find(m.children)) return c;
        }
        return nullptr;
    };
    MenuEntry *m = find(menus_);
    if (!m) return false;
    menu_pick(m->id);
    return true;
}

void Window::hide() {
    if (shown() && !id_.empty()) {
        // the frame's place: the window manager puts the frame where a window asks to be
        // (NorthWest gravity), so the frame's edges (_NET_FRAME_EXTENTS) come off
        long left = 0, top = 0;
        Atom type;
        int format;
        unsigned long n = 0, after;
        unsigned char *d = nullptr;
        if (XGetWindowProperty(fl_display, fl_xid(this), XInternAtom(fl_display, "_NET_FRAME_EXTENTS", False), 0, 4,
                               False, XA_CARDINAL, &type, &format, &n, &after, &d) == Success && d && n == 4) {
            left = ((long *)d)[0];
            top = ((long *)d)[2];
        }
        if (d) XFree(d);
        positions()[id_] = {x() - (int)left, y() - (int)top, w(), h()};
    }
    Fl_Double_Window::hide();
}

const std::string &Window::background_spec() const { return requester_ ? prefs().requester_back : prefs().window_back; }

int run() { return Fl::run(); }

// ============================================================================================
// Popimage, Imageadjust, Penadjust
// ============================================================================================
std::function<bool(const std::string &, std::string &)> file_request_hook;

namespace {
// Imagedisplay: a spec drawn over its whole inside (the previews)
class ImageDisplay : public Area {
public:
    ImageDisplay(std::function<std::string()> spec, int w, int h, StdFrame f = Frame_Text)
        : Area(f), spec_(std::move(spec)), w_(w), h_(h) { background(-1); }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override {
        minw = defw = w_; minh = defh = h_; maxw = maxh = MAXMAX;
    }
    void fixed() { fixed_ = true; }

protected:
    void draw_contents() override { draw_imspec(spec_(), mleft(), mtop(), mwidth(), mheight()); }
    std::function<std::string()> spec_;
    int w_, h_;
    bool fixed_ = false;
};

// a pattern's button in the Pattern page (ButtonFrame, 16 x 16, Immediate)
class PatternButton : public Area {
public:
    PatternButton(int muii) : Area(Frame_Button), muii_(muii) { inner(4, 4, 4, 4); input_mode(Input_Immediate); }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override {
        minw = defw = maxw = minh = defh = maxh = 16;
    }

protected:
    void draw_background(int X, int Y, int W, int H) override {
        fl_color(pens()[selected_ ? MPEN_FILL : MPEN_BACKGROUND]);
        fl_rectf(X, Y, W, H);
    }
    void draw_contents() override { draw_pattern(muii_, mleft(), mtop(), mwidth(), mheight()); }
    int muii_;
};

const char *const PEN_NAMES[MPEN_COUNT] = {"Shine", "Halfshine", "Background", "Halfshadow", "Shadow", "Text", "Fill", "Mark"};

// Penadjust: the MUI pens (a list) or red, green and blue (sliders); the state in a spec
// "m<n>" / "r<r>,<g>,<b>"
struct PenAdjust {
    Register *reg;
    List *list;
    Slider *r, *g, *b;
    std::function<void()> changed;
    std::string spec() const {
        if (reg->active() == 0) return "m" + std::to_string(std::max(0, list->active()));
        char s[40];
        snprintf(s, sizeof s, "r%08x,%08x,%08x", r->value() * 0x01010101u, g->value() * 0x01010101u,
                 b->value() * 0x01010101u);
        return s;
    }
    void set(const std::string &pen) {
        unsigned rgb = penspec_rgb(pen);
        r->value((rgb >> 16) & 255); g->value((rgb >> 8) & 255); b->value(rgb & 255);
        if (!pen.empty() && pen[0] == 'm') { list->active(atoi(pen.c_str() + 1)); reg->active(0); }
        else if (!pen.empty() && pen[0] == 'r') reg->active(1);
    }
};

PenAdjust *make_penadjust() {
    auto *pa = new PenAdjust;
    pa->reg = new Register({"MUI", "RGB"});
    pa->list = new List();
    pa->list->frame(Frame_InputList);
    for (auto *n : PEN_NAMES) pa->list->add({tr(n)});
    pa->list->done();
    auto *rgb = new Group(Group::Vertical);
    rgb->columns(2);
    new Text("\033rRed:"); pa->r = new Slider(0, 255, 0);
    new Text("\033rGreen:"); pa->g = new Slider(0, 255, 0);
    new Text("\033rBlue:"); pa->b = new Slider(0, 255, 0);
    rgb->end();
    pa->reg->end();
    auto ch = [pa] { if (pa->changed) pa->changed(); };
    pa->list->on_active = [ch](int) { ch(); };
    pa->r->on_change = pa->g->on_change = pa->b->on_change = [ch](int) { ch(); };
    pa->reg->on_change = [ch](int) { ch(); };
    return pa;
}
}  // namespace

Popimage::Popimage(const std::string &spec, Kind kind, const std::string &title)
    : Area(Frame_ImageButton), spec_(spec), title_(title), kind_(kind) {
    background(MPEN_BACKGROUND);
    input_mode(Input_RelVerify);
    on_press = [this] { open(); };
}

Popimage::~Popimage() {
    if (win_) { win_->hide(); Fl::delete_widget(win_); }
}

void Popimage::ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) {
    minw = minh = 12;
    defw = defh = 16;
    maxw = 22;   // (MUIA_MaxWidth 28 with the frame)
    maxh = 22;
}

void Popimage::draw_contents() {
    if (kind_ == Pen) {
        unsigned rgb = penspec_rgb(spec_);
        fl_color(fl_rgb_color((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255));
        fl_rectf(mleft(), mtop(), mwidth(), mheight());
    } else draw_imspec(spec_, mleft(), mtop(), mwidth(), mheight());
}

void Popimage::open() {
    if (win_) { win_->show(); return; }
    // the state the window works on (the spec being made)
    struct State {
        ImSpec cur;
        std::string pen;   // (a Poppen: the pen spec)
        std::vector<PatternButton *> patterns;
        PenAdjust *color = nullptr, *gstart = nullptr, *gend = nullptr;
        String *file = nullptr;
        Slider *angle = nullptr;
        Area *preview_bitmap = nullptr, *preview_gradient = nullptr;
        Register *reg = nullptr;
    };
    auto st = std::make_shared<State>();
    Fl_Group *saved = Fl_Group::current();
    Fl_Group::current(nullptr);
    auto *root = VGroup();
    if (kind_ == Pen) {
        st->pen = spec_.empty() ? "m5" : spec_;
        st->color = make_penadjust();
        st->color->set(st->pen);
        st->color->changed = [st] { st->pen = st->color->spec(); };
    } else {
        st->cur = ImSpec::parse(spec_);
        if (st->cur.type == IMS_NONE) { st->cur.type = IMS_PATTERN; st->cur.pattern = MUII_BACKGROUND; }
        st->reg = new Register({"Pattern", "Color", "Bitmap", "Gradient"});
        // Pattern: the 18 patterns in six columns
        auto *pc = HGroup();
        HSpace();
        auto *pg = new Group(Group::Vertical);
        pg->columns(6);
        for (int i = MUII_BACKGROUND; i < MUII_LASTPAT; i++) {
            auto *b = new PatternButton(i);
            b->copy_tooltip(tr(PATTERN_NAMES[i - MUII_BACKGROUND]).c_str());
            st->patterns.push_back(b);
            b->on_press = [st, i] {
                st->cur.type = IMS_PATTERN;
                st->cur.pattern = i;
                for (int k = 0; k < (int)st->patterns.size(); k++) st->patterns[k]->selected(k + MUII_BACKGROUND == i);
            };
        }
        pg->end();
        HSpace();
        pc->end();
        // Color: a Penadjust
        st->color = make_penadjust();
        // Bitmap: the preview, the file
        auto *bg = VGroup();
        st->preview_bitmap = new ImageDisplay([st] { return "5:" + std::string(st->file ? st->file->value() : ""); }, 64, 64);
        auto *fr = HGroup();
        fr->spacing(0);
        st->file = new String(st->cur.type == IMS_BITMAP ? st->cur.file : "", 1024);
        auto *pop = PopButton(Pop_File);
        fr->end();
        bg->end();
        auto use_file = [st] {
            st->cur.type = IMS_BITMAP;
            st->cur.file = st->file->value();
            st->preview_bitmap->redraw();
        };
        st->file->on_acknowledge = use_file;
        pop->on_press = [st, use_file] {
            std::string p = st->file->value();
            if (file_request_hook && file_request_hook(tr("Bitmap"), p)) { st->file->value(p); use_file(); }
        };
        // Gradient: the angle, the two colours, the preview
        auto *gg = new Group(Group::Vertical);
        gg->columns(2);
        new Text("\033rAngle:");
        auto *ang = VGroup();
        auto *hv = HGroup();
        hv->same_width(true);
        auto *bv = new Button("Vertical");
        auto *bh = new Button("Horizontal");
        hv->end();
        st->angle = new Slider(0, 179, st->cur.type == IMS_GRADIENT ? std::min(179, st->cur.angle) : 0);
        ang->end();
        new Text("\033rColors:");
        auto *cols = HGroup();
        auto *startc = new Popimage(st->cur.type == IMS_GRADIENT ? "r" + ImSpec{IMS_RGB, 0, 0, st->cur.rgb}.str().substr(3) : "rbbbbbbbb,bbbbbbbb,bbbbbbbb", Pen, tr("Start pen"));
        auto *swap = new Button("<->");
        swap->weight(0);
        auto *endc = new Popimage(st->cur.type == IMS_GRADIENT ? "r" + ImSpec{IMS_RGB, 0, 0, st->cur.rgb2}.str().substr(3) : "r55555555,55555555,55555555", Pen, tr("End pen"));
        cols->end();
        new Text("\033rPreview:");
        st->preview_gradient = new ImageDisplay([st, startc, endc] {
            ImSpec g;
            g.type = IMS_GRADIENT;
            g.angle = st->angle->value();
            g.rgb = penspec_rgb(startc->spec());
            g.rgb2 = penspec_rgb(endc->spec());
            return g.str();
        }, 64, 48);
        gg->end();
        auto grad = [st, startc, endc] {
            st->cur.type = IMS_GRADIENT;
            st->cur.angle = st->angle->value();
            st->cur.rgb = penspec_rgb(startc->spec());
            st->cur.rgb2 = penspec_rgb(endc->spec());
            st->preview_gradient->redraw();
        };
        st->angle->on_change = [grad](int) { grad(); };
        // (as the original: "Vertical" a gradient from the top down - angle 0, "Horizontal" 90)
        bv->on_press = [st, grad] { st->angle->value(0); grad(); };
        bh->on_press = [st, grad] { st->angle->value(90); grad(); };
        startc->on_change = endc->on_change = [grad](const std::string &) { grad(); };
        swap->on_press = [startc, endc, grad] { std::string a = startc->spec(); startc->spec(endc->spec()); endc->spec(a); grad(); };
        st->reg->end();
        // the spec shown first: its page
        int page = st->cur.type == IMS_PATTERN ? 0 : st->cur.type == IMS_PEN || st->cur.type == IMS_RGB ? 1
                 : st->cur.type == IMS_BITMAP ? 2 : 3;
        if (st->cur.type == IMS_PATTERN && st->cur.pattern >= MUII_BACKGROUND && st->cur.pattern < MUII_LASTPAT)
            st->patterns[st->cur.pattern - MUII_BACKGROUND]->selected(true);
        if (st->cur.type == IMS_PEN) st->color->set("m" + std::to_string(st->cur.pen));
        else if (st->cur.type == IMS_RGB) st->color->set(ImSpec(st->cur).str().substr(2));
        st->color->changed = [st] {
            std::string p = st->color->spec();
            st->cur = ImSpec::parse("2:" + p);
        };
        st->reg->active(page);
    }
    auto *btns = HGroup();
    btns->same_width(true);
    auto *ok = new Button("Ok");
    HVSpace();
    HVSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();
    win_ = new Window(title_.empty() ? tr(kind_ == Pen ? "Adjust Pen" : "Adjust Image") : title_, root);
    Fl_Group::current(saved);
    if (window()) win_->position(window()->x() + x(), window()->y() + y() + h() + 1);   // below the button
    auto close = [this] {
        Window *w = win_;
        win_ = nullptr;
        w->hide();
        Fl::delete_widget(w);
    };
    ok->on_press = [this, st, close] {
        std::string s = kind_ == Pen ? st->pen : st->cur.str();
        close();
        if (s != spec_) { spec(s); if (on_change) on_change(s); }
    };
    cancel->on_press = close;
    win_->callback([](Fl_Widget *, void *d) { ((Popimage *)d)->win_->hide(); Fl::delete_widget(((Popimage *)d)->win_); ((Popimage *)d)->win_ = nullptr; }, this);
    win_->show();
}

}  // namespace lune
