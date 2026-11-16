/*
    Port to FLTK/X11: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    rambler-wm: the window manager of the Rambler port - what Intuition does on AROS.

      Screens   every screen is a window as large as the display holding its own title
                bar (the screen's title at the left, the depth gadget at the right) and the
                windows opened on it. A screen is dragged down by its title bar, uncovering
                the screens behind it; Esc or the right mouse button while dragging puts it
                back where it was. The depth gadget sends the front screen to the back
                (or brings a screen in the back to the front); Super+M brings the next
                screen to the front.
      Windows   the frame of the default look: a title bar with the close gadget on the
                left, the zoom and depth gadgets on the right, a size gadget in the bottom
                right corner. A click activates a window without bringing it to the front;
                the depth gadget brings it to the front, or sends it back if it is already
                in front. Zoom switches between two sizes.
      Backdrop  a window of the type _NET_WM_WINDOW_TYPE_DESKTOP (Rambler's root window as a
                backdrop) fills its screen below the title bar, behind every other window,
                without a frame.

    This is new code written for the port (no code of other window managers is used).
    The pens are the default four colour palette and the default pens of the system:
    BACKGROUND grey, SHADOW/TEXT black, SHINE white, FILL blue; the screen bar in
    BARBLOCK (white) with BARDETAIL (black) text and a BARTRIM (black) line under it.
*/
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/shape.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <poll.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>
#include "../rambler/beep.h"
#include "decor.h"

#include <memory>
#include <dirent.h>
#include <strings.h>

namespace {

Display *dpy;
int scr;
Window root;
int SW, SH;

// --------------------------------------------------------------------------------------------
// Pens and fonts
// --------------------------------------------------------------------------------------------
unsigned long pen_grey, pen_black, pen_white, pen_blue, pen_halfshadow;
XftFont *font;
XftColor xft_black, xft_white;
bool xft_ready = false;
XftDraw *xft_for(Drawable d) {
    return XftDrawCreate(dpy, d, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr));
}

unsigned long alloc(const char *spec) {
    XColor c, e;
    XAllocNamedColor(dpy, DefaultColormap(dpy, scr), spec, &c, &e);
    return c.pixel;
}

int text_width(const std::string &s) {
    XGlyphInfo gi;
    XftTextExtentsUtf8(dpy, font, (const FcChar8 *)s.data(), (int)s.size(), &gi);
    return gi.xOff;
}

// --------------------------------------------------------------------------------------------
// Geometry of the frames
// --------------------------------------------------------------------------------------------
// (the default look's; a decoration theme sets them from its pictures and settings)
int BAR_H = 20;                // the screen's title bar
int TITLE_H = 20;              // a window's title bar
int BORDER_L = 4, BORDER_R = 4, BORDER_B = 4;
int GADGET_W = 22;             // close, zoom, depth (and the screen's depth gadget)
int SIZE_W = 16, SIZE_H = 16;

// The decoration theme (THEME:): the frames, the screen bars and the gadgets drawn from its
// pictures, as decoration.library and its decorator classes draw them; none: the default look
std::unique_ptr<decor::Theme> theme;
decor::ScreenBar theme_bar;

Atom WM_PROTOCOLS, WM_DELETE_WINDOW, WM_STATE, WM_TAKE_FOCUS, NET_WM_NAME, UTF8_STRING, NET_WM_WINDOW_TYPE,
    NET_WM_WINDOW_TYPE_DESKTOP, NET_WM_WINDOW_TYPE_DOCK, NET_SUPPORTED, NET_ACTIVE_WINDOW, NET_CLIENT_LIST,
    NET_SUPPORTING_WM_CHECK, NET_WM_STATE, NET_WM_STATE_FULLSCREEN, RAMBLER_SCREEN_TITLE, RAMBLER_PALETTE, RAMBLER_FONT, RAMBLER_ICONTROL, RAMBLER_SOUND, RAMBLER_COMMODITIES,
    NET_WM_STATE_MAXIMIZED_VERT, NET_WM_STATE_MAXIMIZED_HORZ, NET_CLOSE_WINDOW, NET_FRAME_EXTENTS, NET_WORKAREA,
    NET_NUMBER_OF_DESKTOPS, NET_CURRENT_DESKTOP, NET_DESKTOP_GEOMETRY;

struct WScreen;

struct Client {
    Window win = 0, frame = 0;
    WScreen *screen = nullptr;
    std::string title;
    std::string screen_title;                 // _RAMBLER_SCREEN_TITLE: the screen's title while active
    int x = 0, y = 0, w = 100, h = 100;       // the frame, on its screen
    int alt_x = 0, alt_y = 0, alt_w = 0, alt_h = 0;   // the other size (zoom)
    bool backdrop = false, active = false, zoomed = false;
    bool undecorated = false;                 // no frame: docks, notifications, splash screens, tooltips,
                                              // and windows asking for none (_MOTIF_WM_HINTS)
    bool fullscreen = false;                  // _NET_WM_STATE_FULLSCREEN: the whole display, no frame
    bool on_top = false;                      // notifications, docks: above the other windows
    bool no_focus = false;                    // notifications, tooltips: never active
    int fs_x = 0, fs_y = 0, fs_w = 0, fs_h = 0;   // the size before full screen
    bool bare() const { return backdrop || undecorated || fullscreen; }
    bool protocols_delete = false, takes_focus = false, accepts_input = true;
    int min_w = 40, min_h = 20;
    int max_w = 0, max_h = 0;                 // 0: no limit (frame sizes)
    int inc_w = 1, inc_h = 1, base_w = 0, base_h = 0;   // resize increments (terminals: characters)
    int pressed = 0;                          // a gadget held down
    int shape_w = -1, shape_h = -1, shape_active = -1;   // the frame's shape made for (a theme's)
    int ignore_unmap = 0;                     // unmaps caused by reparenting a mapped window
};

struct WScreen {
    Window win = 0;
    std::string title;
    int y = 0;                                // dragged down by
    std::vector<Client *> clients;            // bottom to top
    void draw_bar();
};

std::vector<WScreen *> screens;               // back to front
WScreen *main_screen = nullptr;               // the Rambler Screen (the default public screen)

// AppIcons: the icons programs put out (the system tray, XEmbed) sit in the main screen's
// title bar, left of its depth gadget
std::vector<Window> tray_icons;
Window tray_owner = 0;
Atom NET_SYSTEM_TRAY_S, NET_SYSTEM_TRAY_OPCODE, XEMBED, MANAGER, XEMBED_INFO;
const int TRAY_SIZE = 18, TRAY_GAP = 4;
int tray_width() { return tray_icons.empty() ? 0 : (int)tray_icons.size() * (TRAY_SIZE + TRAY_GAP) + TRAY_GAP; }
bool is_tray_icon(Window w) { return std::find(tray_icons.begin(), tray_icons.end(), w) != tray_icons.end(); }
std::map<Window, Client *> by_window, by_frame;
Client *active = nullptr;

// The IControl preferences (Intuition's settings): from icontrol.prefs, then from the
// _RAMBLER_ICONTROL property of the root window when the preferences program changes them
struct IControlPrefs {
    bool frontmost_default = true;   // new windows on the front screen (else on the first)
    bool drag_bottom = true;         // a screen dragged down to the bottom edge (else its bar stays)
    unsigned meta_drag = Mod4Mask;   // the key that drags a screen from anywhere
    bool menu_3d = false;            // the menus' look: classic or 3D
    bool menu_popup = false;         // pop-up menus: the titles in a box at the pointer
    bool title_pulldown = true;      // (pop-up) the screen bar still drops its menus down
    bool menu_sticky = false;        // a click of the menu button leaves the menus open
    bool offscreen = true;           // windows moved partly off the screen
} ic;

void read_icontrol(const std::string &text) {
    IControlPrefs p;
    size_t a = 0;
    while (a < text.size()) {
        size_t b = text.find('\n', a);
        if (b == std::string::npos) b = text.size();
        std::string line = text.substr(a, b - a);
        a = b + 1;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        int v = atoi(line.c_str() + eq + 1);
        if (k == "frontmost_default") p.frontmost_default = v;
        else if (k == "drag_bottom") p.drag_bottom = v;
        else if (k == "meta_drag") p.meta_drag = v == 1 ? Mod1Mask : v == 2 ? ControlMask : Mod4Mask;
        else if (k == "menu_look") p.menu_3d = v == 1;
        else if (k == "menu_type") p.menu_popup = v == 1;
        else if (k == "menu_title_pulldown") p.title_pulldown = v;
        else if (k == "menu_sticky") p.menu_sticky = v;
        else if (k == "offscreen") p.offscreen = v;
    }
    ic = p;
}
// The commodities the window manager does (Exchange): ClickToFront and AutoPoint
struct CommodityPrefs {
    bool click_to_front = false;
    unsigned ctf_mask = 0;               // 0: a double click; else the key held with a click
    bool autopoint = false;
} cx;

void read_commodities(const std::string &text) {
    CommodityPrefs p;
    size_t a = 0;
    while (a < text.size()) {
        size_t b = text.find('\n', a);
        if (b == std::string::npos) b = text.size();
        std::string line = text.substr(a, b - a);
        a = b + 1;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        int v = atoi(line.c_str() + eq + 1);
        if (k == "click_to_front") p.click_to_front = v;
        else if (k == "ctf_key") p.ctf_mask = v == 1 ? Mod1Mask : v == 2 ? ControlMask : v == 3 ? ShiftMask : 0;
        else if (k == "autopoint") p.autopoint = v;
    }
    cx = p;
}
Window check_win;

enum Gadget { G_NONE, G_CLOSE, G_ZOOM, G_DEPTH, G_SIZE, G_TITLE };

// --------------------------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------------------------
void fill(Drawable d, GC gc, unsigned long pen, int x, int y, int w, int h) {
    XSetForeground(dpy, gc, pen);
    XFillRectangle(dpy, d, gc, x, y, std::max(0, w), std::max(0, h));
}

// a bevel: shine on the top and left, shadow on the bottom and right (swapped when recessed)
void bevel(Drawable d, GC gc, int x, int y, int w, int h, bool recessed) {
    XSetForeground(dpy, gc, recessed ? pen_black : pen_white);
    XDrawLine(dpy, d, gc, x, y, x + w - 1, y);
    XDrawLine(dpy, d, gc, x, y, x, y + h - 1);
    XSetForeground(dpy, gc, recessed ? pen_white : pen_black);
    XDrawLine(dpy, d, gc, x, y + h - 1, x + w - 1, y + h - 1);
    XDrawLine(dpy, d, gc, x + w - 1, y, x + w - 1, y + h - 1);
}

void draw_text(Drawable d, int x, int y, int maxw, const std::string &s, XftColor *c) {
    XftDraw *xd = xft_for(d);
    XRectangle clip = {(short)x, (short)(y - font->ascent), (unsigned short)std::max(0, maxw), (unsigned short)(font->height + 2)};
    XftDrawSetClipRectangles(xd, 0, 0, &clip, 1);
    XftDrawStringUtf8(xd, c, font, x, y, (const FcChar8 *)s.data(), (int)s.size());
    XftDrawDestroy(xd);
}

// The gadget pictures (original drawings in the default pens)
void draw_close(Drawable d, GC gc, int x, int y, int w, int h, bool pressed) {
    fill(d, gc, pressed ? pen_halfshadow : pen_grey, x, y, w, h);
    bevel(d, gc, x, y, w, h, pressed);
    int cw = 6, cx = x + (w - cw) / 2, cy = y + (h - cw) / 2;
    fill(d, gc, pen_white, cx, cy, cw, cw);
    XSetForeground(dpy, gc, pen_black);
    XDrawRectangle(dpy, d, gc, cx, cy, cw, cw);
}

void draw_zoom(Drawable d, GC gc, int x, int y, int w, int h, bool pressed) {
    fill(d, gc, pressed ? pen_halfshadow : pen_grey, x, y, w, h);
    bevel(d, gc, x, y, w, h, pressed);
    XSetForeground(dpy, gc, pen_black);
    XDrawRectangle(dpy, d, gc, x + 5, y + 4, w - 11, h - 9);
    fill(d, gc, pen_white, x + 6, y + 5, (w - 12) / 2, (h - 10) / 2);
    XDrawRectangle(dpy, d, gc, x + 5, y + 4, (w - 11) / 2 + 1, (h - 9) / 2 + 1);
}

void draw_depth(Drawable d, GC gc, int x, int y, int w, int h, bool pressed) {
    fill(d, gc, pressed ? pen_halfshadow : pen_grey, x, y, w, h);
    bevel(d, gc, x, y, w, h, pressed);
    // two overlapping rectangles: the one behind outlined, the front one filled
    int bw = w - 12, bh = h - 10;
    XSetForeground(dpy, gc, pen_black);
    XDrawRectangle(dpy, d, gc, x + 4, y + 3, bw - 2, bh - 2);
    fill(d, gc, pen_white, x + 8, y + 7, bw - 2, bh - 2);
    XSetForeground(dpy, gc, pen_black);
    XDrawRectangle(dpy, d, gc, x + 8, y + 7, bw - 2, bh - 2);
}

void draw_size(Drawable d, GC gc, int x, int y, int w, int h, bool pressed) {
    fill(d, gc, pressed ? pen_halfshadow : pen_grey, x, y, w, h);
    bevel(d, gc, x, y, w, h, pressed);
    XSetForeground(dpy, gc, pen_white);
    XPoint tri[3] = {{(short)(x + w - 4), (short)(y + 4)}, {(short)(x + w - 4), (short)(y + h - 4)}, {(short)(x + 4), (short)(y + h - 4)}};
    XFillPolygon(dpy, d, gc, tri, 3, Convex, CoordModeOrigin);
    XSetForeground(dpy, gc, pen_black);
    XDrawLines(dpy, d, gc, tri, 3, CoordModeOrigin);
    XDrawLine(dpy, d, gc, tri[2].x, tri[2].y, tri[0].x, tri[0].y);
}

GC gc_for(Drawable d) { return XCreateGC(dpy, d, 0, nullptr); }

// ---- the themed look -------------------------------------------------------------------------
int font_height() { return font->ascent + font->descent; }

decor::WindowFrame frame_of(const Client *c) {
    return decor::window_frame(*theme, font_height(), c->w, c->h, true, true, true, true);
}

// a drawing of the decorator onto a drawable (its pixels as 32-bit words: 24 and 32 bit
// TrueColor displays)
bool themed_ok() {
    return theme && (DefaultDepth(dpy, scr) == 24 || DefaultDepth(dpy, scr) == 32) && DefaultVisual(dpy, scr)->c_class == TrueColor;
}
void put_rastport(Drawable d, GC gc, decor::RastPort &rp) {
    XImage *im = XCreateImage(dpy, DefaultVisual(dpy, scr), DefaultDepth(dpy, scr), ZPixmap, 0, (char *)rp.px.data(),
                              (unsigned)rp.w, (unsigned)rp.h, 32, rp.w * 4);
    if (!im) return;
    XPutImage(dpy, d, gc, im, 0, 0, 0, 0, (unsigned)rp.w, (unsigned)rp.h);
    im->data = nullptr;
    XDestroyImage(im);
}

// TextFit: as much of a text as fits in maxw pixels
std::string fit_text(const std::string &s, int maxw) {
    if (text_width(s) <= maxw) return s;
    std::string t = s;
    while (!t.empty() && text_width(t) > maxw) {
        do t.pop_back(); while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80);
        if (!t.empty() && ((unsigned char)t.back() & 0x80)) t.pop_back();   // (a sequence's first byte)
    }
    return t;
}

// a title in a theme's way: plain, with a shadow (+1,+1) or an outline (eight times round)
void themed_title(Drawable d, int x, int y, int maxw, const std::string &s, bool outline, bool shadow, uint32_t text_rgb,
                  uint32_t shadow_rgb, XftColor *plain) {
    auto color = [](uint32_t rgb) {
        XRenderColor rc = {(unsigned short)(((rgb >> 16) & 0xff) * 257), (unsigned short)(((rgb >> 8) & 0xff) * 257),
                           (unsigned short)((rgb & 0xff) * 257), 0xffff};
        XftColor c;
        XftColorAllocValue(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), &rc, &c);
        return c;
    };
    // (plain: in the pen given, or in the colour given when there is no pen)
    if (!outline && !shadow) {
        if (plain) { draw_text(d, x, y, maxw, s, plain); return; }
        XftColor tc = color(text_rgb);
        draw_text(d, x, y, maxw, s, &tc);
        XftColorFree(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), &tc);
        return;
    }
    XftColor tc = color(text_rgb), sc = color(shadow_rgb);
    if (outline) {
        for (auto [dx, dy] : {std::pair{1, 0}, {2, 0}, {0, 0}, {0, 1}, {0, 2}, {1, 2}, {2, 1}, {2, 2}})
            draw_text(d, x + dx, y + dy, maxw + 2, s, &sc);
        draw_text(d, x + 1, y + 1, maxw + 2, s, &tc);
    } else {
        draw_text(d, x + 1, y + 1, maxw + 1, s, &sc);
        draw_text(d, x, y, maxw, s, &tc);
    }
    XftColorFree(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), &tc);
    XftColorFree(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), &sc);
}

void draw_frame_themed(Client *c) {
    Window f = c->frame;
    int w = c->w, h = c->h;
    decor::WindowFrame fr = frame_of(c);
    decor::RastPort rp;
    rp.size(w, h);
    // the gadgets' states: held down, or the window's (in)active look
    decor::GadgetStates gs;
    int idle = c->active ? decor::IDS_NORMAL : decor::IDS_INACTIVENORMAL;
    gs.close = c->pressed == G_CLOSE ? decor::IDS_SELECTED : idle;
    gs.depth = c->pressed == G_DEPTH ? decor::IDS_SELECTED : idle;
    gs.zoom = c->pressed == G_ZOOM ? decor::IDS_SELECTED : idle;
    gs.size = c->pressed == G_SIZE ? decor::IDS_SELECTED : idle;
    struct Fit { std::string text, shown; } fit{c->title, ""};
    int tx, ty, tw;
    decor::draw_window(*theme, rp, fr, c->active, gs, !c->title.empty(), font->ascent, font_height(),
                       [](void *d, int maxw) {
                           auto *ft = (Fit *)d;
                           ft->shown = fit_text(ft->text, maxw);
                           return ft->shown.empty() ? 0 : text_width(ft->shown);
                       }, &fit, tx, ty, tw);
    Pixmap pm = XCreatePixmap(dpy, f, (unsigned)w, (unsigned)h, (unsigned)DefaultDepth(dpy, scr));
    GC gc = gc_for(pm);
    put_rastport(pm, gc, rp);
    if (tx >= 0) {
        // FILLTEXTPEN (active) and TEXTPEN: the text pen, black
        const decor::Config &dc = theme->dc;
        themed_title(pm, tx, ty, tw + 1, fit.shown, dc.TitleOutline, dc.TitleShadow, dc.TitleColorText, dc.TitleColorShadow,
                     &xft_black);
    }
    XCopyArea(dpy, pm, f, gc, 0, 0, (unsigned)w, (unsigned)h, 0, 0);
    XFreeGC(dpy, gc);
    XFreePixmap(dpy, pm);
    // the frame's shape (rounded title bars), made again when the size or the activity changed
    if (c->shape_w != w || c->shape_h != h || c->shape_active != (int)c->active) {
        c->shape_w = w;
        c->shape_h = h;
        c->shape_active = c->active;
        std::vector<decor::Rect> rs = decor::window_shape(*theme, fr, w, h, c->active, !c->title.empty(),
            [](void *d, int maxw) {
                std::string s = fit_text(*(std::string *)d, maxw);
                return s.empty() ? 0 : text_width(s);
            }, &c->title);
        if (!rs.empty()) {
            std::vector<XRectangle> xr;
            for (auto &r : rs) if (r.w > 0 && r.h > 0) xr.push_back({(short)r.x, (short)r.y, (unsigned short)r.w, (unsigned short)r.h});
            XShapeCombineRectangles(dpy, f, ShapeBounding, 0, 0, xr.data(), (int)xr.size(), ShapeSet, Unsorted);
        }
    }
}

void draw_frame(Client *c) {
    if (c->bare()) return;
    if (themed_ok()) { draw_frame_themed(c); return; }
    Window f = c->frame;
    GC gc = gc_for(f);
    int w = c->w, h = c->h;
    // the border and the title bar: the active window's in the fill pen
    unsigned long bar = c->active ? pen_blue : pen_grey;
    fill(f, gc, bar, 0, 0, w, TITLE_H);
    fill(f, gc, bar, 0, TITLE_H, BORDER_L, h - TITLE_H);
    fill(f, gc, bar, w - BORDER_R, TITLE_H, BORDER_R, h - TITLE_H);
    fill(f, gc, bar, 0, h - BORDER_B, w, BORDER_B);
    bevel(f, gc, 0, 0, w, h, false);
    // the inner edge around the contents
    bevel(f, gc, BORDER_L - 1, TITLE_H - 1, w - BORDER_L - BORDER_R + 2, h - TITLE_H - BORDER_B + 2, true);
    // the gadgets
    draw_close(f, gc, 0, 0, GADGET_W, TITLE_H, c->pressed == G_CLOSE);
    draw_depth(f, gc, w - GADGET_W, 0, GADGET_W, TITLE_H, c->pressed == G_DEPTH);
    draw_zoom(f, gc, w - 2 * GADGET_W, 0, GADGET_W, TITLE_H, c->pressed == G_ZOOM);
    draw_size(f, gc, w - SIZE_W, h - SIZE_H, SIZE_W, SIZE_H, c->pressed == G_SIZE);
    // the title
    int tx = GADGET_W + 6, tw = w - 3 * GADGET_W - 12;
    draw_text(f, tx, (TITLE_H + font->ascent - font->descent) / 2, tw, c->title, &xft_black);
    XFreeGC(dpy, gc);
}

void WScreen::draw_bar() {
    if (themed_ok()) {
        // screendecorclass: the bar from the theme's picture, the title, the depth gadget
        decor::RastPort rp;
        rp.size(SW, BAR_H);
        int tx, ty;
        decor::draw_screen_bar(*theme, rp, theme_bar, decor::IDS_NORMAL, font->ascent, font_height(), tx, ty);
        Pixmap pm = XCreatePixmap(dpy, win, (unsigned)SW, (unsigned)BAR_H, (unsigned)DefaultDepth(dpy, scr));
        GC gc = gc_for(pm);
        put_rastport(pm, gc, rp);
        std::string t = title;
        if (active && active->screen == this && !active->screen_title.empty()) t = active->screen_title;
        else for (auto *c : clients) if (c->backdrop && !c->screen_title.empty()) t = c->screen_title;
        int room = theme_bar.title_right - tx - (this == main_screen ? tray_width() : 0);
        const decor::Config &dc = theme->dc;
        // the title's colours: the theme's (TITLE_DEFCOLORTEXT/SHADOW when it gives none for
        // an outline), plain without them in BARDETAILPEN
        bool own = dc.STitleColorText != 0xFFFFFFFF;
        uint32_t text_rgb = dc.STitleColorText & 0xFFFFFF, shadow_rgb = dc.STitleColorShadow & 0xFFFFFF;
        if (dc.STitleOutline) {
            if (!own) text_rgb = 0xCCCCCC;
            if (dc.STitleColorShadow == 0xFFFFFFFF) shadow_rgb = 0x444444;
        }
        themed_title(pm, tx, ty, room, fit_text(t, room), dc.STitleOutline, !dc.STitleOutline && dc.STitleShadow,
                     text_rgb, shadow_rgb, own || dc.STitleOutline || dc.STitleShadow ? nullptr : &xft_black);
        XCopyArea(dpy, pm, win, gc, 0, 0, (unsigned)SW, (unsigned)BAR_H, 0, 0);
        XFreeGC(dpy, gc);
        XFreePixmap(dpy, pm);
        return;
    }
    GC gc = gc_for(win);
    fill(win, gc, pen_white, 0, 0, SW, BAR_H - 1);
    fill(win, gc, pen_black, 0, BAR_H - 1, SW, 1);
    // the active window's screen title, else the screen's own
    std::string t = title;
    if (active && active->screen == this && !active->screen_title.empty()) t = active->screen_title;
    else for (auto *c : clients) if (c->backdrop && !c->screen_title.empty()) t = c->screen_title;
    int room = SW - GADGET_W - 12 - (this == main_screen ? tray_width() : 0);
    draw_text(win, 6, (BAR_H - 1 + font->ascent - font->descent) / 2, room, t, &xft_black);
    // the depth gadget
    int gx = SW - GADGET_W;
    fill(win, gc, pen_white, gx, 0, GADGET_W, BAR_H - 1);
    XSetForeground(dpy, gc, pen_black);
    XDrawLine(dpy, win, gc, gx, 0, gx, BAR_H - 2);
    int bw = GADGET_W - 12, bh = BAR_H - 10;
    XDrawRectangle(dpy, win, gc, gx + 4, 3, bw - 2, bh - 2);
    fill(win, gc, pen_grey, gx + 8, 7, bw - 2, bh - 2);
    XSetForeground(dpy, gc, pen_black);
    XDrawRectangle(dpy, win, gc, gx + 8, 7, bw - 2, bh - 2);
    XFreeGC(dpy, gc);
}

// --------------------------------------------------------------------------------------------
// Properties
// --------------------------------------------------------------------------------------------
std::string window_title(Window w) {
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = nullptr;
    std::string t;
    if (XGetWindowProperty(dpy, w, NET_WM_NAME, 0, 1024, False, UTF8_STRING, &type, &fmt, &n, &after, &data) == Success && data) {
        t.assign((char *)data, n);
        XFree(data);
        if (!t.empty()) return t;
    }
    char *name = nullptr;
    if (XFetchName(dpy, w, &name) && name) { t = name; XFree(name); }
    return t;
}

std::string utf8_prop(Window w, Atom a) {
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = nullptr;
    std::string t;
    if (XGetWindowProperty(dpy, w, a, 0, 1024, False, UTF8_STRING, &type, &fmt, &n, &after, &data) == Success && data) {
        t.assign((char *)data, n);
        XFree(data);
    }
    return t;
}

bool has_type(Window w, Atom type_atom) {
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = nullptr;
    bool r = false;
    if (XGetWindowProperty(dpy, w, NET_WM_WINDOW_TYPE, 0, 16, False, XA_ATOM, &type, &fmt, &n, &after, &data) == Success && data) {
        for (unsigned long i = 0; i < n; i++) if (((Atom *)data)[i] == type_atom) r = true;
        XFree(data);
    }
    return r;
}

void read_protocols(Client *c) {
    Atom *p;
    int n;
    c->protocols_delete = c->takes_focus = false;
    if (XGetWMProtocols(dpy, c->win, &p, &n)) {
        for (int i = 0; i < n; i++) {
            if (p[i] == WM_DELETE_WINDOW) c->protocols_delete = true;
            if (p[i] == WM_TAKE_FOCUS) c->takes_focus = true;
        }
        XFree(p);
    }
    XWMHints *hints = XGetWMHints(dpy, c->win);
    c->accepts_input = !hints || !(hints->flags & InputHint) || hints->input;
    if (hints) XFree(hints);
    XSizeHints sh;
    long supplied;
    if (XGetWMNormalHints(dpy, c->win, &sh, &supplied)) {
        if (sh.flags & PMinSize) {
            c->min_w = std::max(40, sh.min_width + BORDER_L + BORDER_R);
            c->min_h = std::max(TITLE_H + BORDER_B + 10, sh.min_height + TITLE_H + BORDER_B);
        }
        if ((sh.flags & PMaxSize) && sh.max_width > 0 && sh.max_height > 0) {
            c->max_w = sh.max_width + BORDER_L + BORDER_R;
            c->max_h = sh.max_height + TITLE_H + BORDER_B;
        }
        if ((sh.flags & PResizeInc) && sh.width_inc > 0 && sh.height_inc > 0) {
            c->inc_w = sh.width_inc;
            c->inc_h = sh.height_inc;
            c->base_w = (sh.flags & PBaseSize) ? sh.base_width : (sh.flags & PMinSize) ? sh.min_width : 0;
            c->base_h = (sh.flags & PBaseSize) ? sh.base_height : (sh.flags & PMinSize) ? sh.min_height : 0;
        }
    }
}

void set_wm_state(Window w, long state) {
    long data[2] = {state, None};
    XChangeProperty(dpy, w, WM_STATE, WM_STATE, 32, PropModeReplace, (unsigned char *)data, 2);
}

void publish_client_list() {
    std::vector<Window> list;
    for (auto *s : screens) for (auto *c : s->clients) list.push_back(c->win);
    XChangeProperty(dpy, root, NET_CLIENT_LIST, XA_WINDOW, 32, PropModeReplace, (unsigned char *)list.data(), (int)list.size());
    Window a = active ? active->win : None;
    XChangeProperty(dpy, root, NET_ACTIVE_WINDOW, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&a, 1);
}

// --------------------------------------------------------------------------------------------
// Windows
// --------------------------------------------------------------------------------------------
void send_configure(Client *c) {
    XConfigureEvent ce = {};
    ce.type = ConfigureNotify;
    ce.event = c->win;
    ce.window = c->win;
    int sy = c->screen ? c->screen->y : 0;
    if (c->bare()) { ce.x = c->x; ce.y = c->y + sy; ce.width = c->w; ce.height = c->h; }
    else {
        ce.x = c->x + BORDER_L;
        ce.y = c->y + TITLE_H + sy;
        ce.width = c->w - BORDER_L - BORDER_R;
        ce.height = c->h - TITLE_H - BORDER_B;
    }
    ce.override_redirect = False;
    XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void place(Client *c) {
    c->w = std::max(c->w, c->min_w);
    c->h = std::max(c->h, c->min_h);
    XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w, c->h);
    if (c->bare()) XMoveResizeWindow(dpy, c->win, 0, 0, c->w, c->h);
    else XMoveResizeWindow(dpy, c->win, BORDER_L, TITLE_H, c->w - BORDER_L - BORDER_R, c->h - TITLE_H - BORDER_B);
    send_configure(c);
    long ext[4] = {c->bare() ? 0 : BORDER_L, c->bare() ? 0 : BORDER_R, c->bare() ? 0 : TITLE_H, c->bare() ? 0 : BORDER_B};
    XChangeProperty(dpy, c->win, NET_FRAME_EXTENTS, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)ext, 4);
    draw_frame(c);
}

// the stacking within a screen: backdrops at the bottom, then the clients in order
void restack(WScreen *s) {
    std::vector<Window> order;   // top first (XRestackWindows)
    for (auto it = s->clients.rbegin(); it != s->clients.rend(); ++it) if ((*it)->on_top) order.push_back((*it)->frame);
    for (auto it = s->clients.rbegin(); it != s->clients.rend(); ++it) if ((*it)->fullscreen && !(*it)->on_top) order.push_back((*it)->frame);
    for (auto it = s->clients.rbegin(); it != s->clients.rend(); ++it)
        if (!(*it)->backdrop && !(*it)->on_top && !(*it)->fullscreen) order.push_back((*it)->frame);
    for (auto it = s->clients.rbegin(); it != s->clients.rend(); ++it) if ((*it)->backdrop) order.push_back((*it)->frame);
    if (!order.empty()) XRestackWindows(dpy, order.data(), (int)order.size());
}

void activate(Client *c) {
    if (active == c) return;
    Client *old = active;
    active = c;
    if (old) { old->active = false; draw_frame(old); }
    if (c) {
        c->active = true;
        draw_frame(c);
        if (c->accepts_input) XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
        if (c->takes_focus) {
            XEvent e = {};
            e.xclient.type = ClientMessage;
            e.xclient.window = c->win;
            e.xclient.message_type = WM_PROTOCOLS;
            e.xclient.format = 32;
            e.xclient.data.l[0] = (long)WM_TAKE_FOCUS;
            e.xclient.data.l[1] = CurrentTime;
            XSendEvent(dpy, c->win, False, NoEventMask, &e);
        }
    } else XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    if (old && old->screen && (!c || old->screen != c->screen)) old->screen->draw_bar();
    if (c && c->screen) c->screen->draw_bar();
    publish_client_list();
}

void to_front(Client *c) {
    auto &v = c->screen->clients;
    v.erase(std::remove(v.begin(), v.end(), c), v.end());
    v.push_back(c);
    restack(c->screen);
}

void to_back(Client *c) {
    auto &v = c->screen->clients;
    v.erase(std::remove(v.begin(), v.end(), c), v.end());
    v.insert(v.begin(), c);
    restack(c->screen);
}

bool in_front(Client *c) {
    for (auto it = c->screen->clients.rbegin(); it != c->screen->clients.rend(); ++it) {
        if ((*it)->backdrop) continue;
        return *it == c;
    }
    return false;
}

void close_window(Client *c) {
    if (c->protocols_delete) {
        XEvent e = {};
        e.xclient.type = ClientMessage;
        e.xclient.window = c->win;
        e.xclient.message_type = WM_PROTOCOLS;
        e.xclient.format = 32;
        e.xclient.data.l[0] = (long)WM_DELETE_WINDOW;
        e.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, c->win, False, NoEventMask, &e);
    } else XKillClient(dpy, c->win);
}

void publish_state(Client *c);

void zoom(Client *c) {
    // the other size: at first the largest the screen allows
    if (!c->alt_w) {
        c->alt_x = 0;
        c->alt_y = BAR_H;
        c->alt_w = SW;
        c->alt_h = SH - BAR_H;
    }
    std::swap(c->x, c->alt_x);
    std::swap(c->y, c->alt_y);
    std::swap(c->w, c->alt_w);
    std::swap(c->h, c->alt_h);
    c->zoomed = !c->zoomed;
    place(c);
    publish_state(c);
}

WScreen *front_screen() { return screens.back(); }

bool motif_undecorated(Window w) {
    Atom a = XInternAtom(dpy, "_MOTIF_WM_HINTS", False), type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = nullptr;
    bool r = false;
    if (XGetWindowProperty(dpy, w, a, 0, 5, False, AnyPropertyType, &type, &fmt, &n, &after, &data) == Success && data) {
        long *h = (long *)data;   // flags, functions, decorations, ...
        if (n >= 3 && (h[0] & 2) && h[2] == 0) r = true;
        XFree(data);
    }
    return r;
}

bool has_state(Window w, Atom state) {
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = nullptr;
    bool r = false;
    if (XGetWindowProperty(dpy, w, NET_WM_STATE, 0, 32, False, XA_ATOM, &type, &fmt, &n, &after, &data) == Success && data) {
        for (unsigned long i = 0; i < n; i++) if (((Atom *)data)[i] == state) r = true;
        XFree(data);
    }
    return r;
}

void publish_state(Client *c) {
    std::vector<Atom> st;
    if (c->fullscreen) st.push_back(NET_WM_STATE_FULLSCREEN);
    if (c->zoomed) { st.push_back(NET_WM_STATE_MAXIMIZED_VERT); st.push_back(NET_WM_STATE_MAXIMIZED_HORZ); }
    XChangeProperty(dpy, c->win, NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char *)st.data(), (int)st.size());
}

// The whole display for the window (a film, a game, a presentation): no frame, over the
// screen's title bar; back to its size and frame after
void set_fullscreen(Client *c, bool on) {
    if (c->backdrop || c->fullscreen == on) return;
    if (on) {
        c->fs_x = c->x; c->fs_y = c->y; c->fs_w = c->w; c->fs_h = c->h;
        c->fullscreen = true;
        c->x = 0; c->y = 0; c->w = SW; c->h = SH;   // (place() puts the window at 0, 0 of its frame)
    } else {
        c->fullscreen = false;
        c->x = c->fs_x; c->y = c->fs_y; c->w = c->fs_w; c->h = c->fs_h;
    }
    place(c);
    restack(c->screen);
    publish_state(c);
}

// A window that has become a "desktop" window: its screen's backdrop
void make_backdrop(Client *c) {
    c->backdrop = true;
    c->zoomed = false;
    c->x = 0; c->y = BAR_H; c->w = SW; c->h = SH - BAR_H;
    auto &v = c->screen->clients;
    v.erase(std::remove(v.begin(), v.end(), c), v.end());
    v.insert(v.begin(), c);
    place(c);
    restack(c->screen);
}

void manage(Window w, bool existing) {
    if (by_window.count(w) || by_frame.count(w)) return;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, w, &wa) || wa.override_redirect) return;
    if (existing && wa.map_state != IsViewable) return;
    for (auto *s : screens) if (s->win == w) return;
    auto *c = new Client;
    c->win = w;
    c->screen = ic.frontmost_default || screens.empty() ? front_screen() : screens.front();
    c->title = window_title(w);
    c->screen_title = utf8_prop(w, RAMBLER_SCREEN_TITLE);
    c->backdrop = has_type(w, NET_WM_WINDOW_TYPE_DESKTOP);
    read_protocols(c);
    // the kinds of windows that have no frame
    for (const char *t : {"_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
                          "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_POPUP_MENU",
                          "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_DND"})
        if (has_type(w, XInternAtom(dpy, t, False))) c->undecorated = true;
    c->no_focus = has_type(w, XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False)) ||
                  has_type(w, XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", False));
    c->on_top = c->no_focus || has_type(w, XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False));
    if (motif_undecorated(w)) c->undecorated = true;
    if (c->backdrop) {
        c->x = 0; c->y = BAR_H; c->w = SW; c->h = SH - BAR_H;
    } else {
        c->w = wa.width + (c->undecorated ? 0 : BORDER_L + BORDER_R);
        c->h = wa.height + (c->undecorated ? 0 : TITLE_H + BORDER_B);
        // the position the program asked for, else a little down and right of the last one
        XSizeHints sh;
        long supplied;
        bool asked = XGetWMNormalHints(dpy, w, &sh, &supplied) && (sh.flags & (USPosition | PPosition));
        if (asked || existing) { c->x = wa.x; c->y = std::max(BAR_H, wa.y); }
        else {
            static int n = 0;
            c->x = 40 + (n % 10) * 24;
            c->y = BAR_H + 20 + (n % 10) * 24;
            n++;
        }
        Window transient_for;
        if (XGetTransientForHint(dpy, w, &transient_for) && by_window.count(transient_for)) {
            Client *p = by_window[transient_for];
            c->screen = p->screen;
            c->x = p->x + (p->w - c->w) / 2;
            c->y = std::max(BAR_H, p->y + (p->h - c->h) / 3);
            // (over its parent, but within the screen when it fits)
            if (c->w <= SW) c->x = std::clamp(c->x, 0, SW - c->w);
        }
        c->x = std::clamp(c->x, -c->w + 40, SW - 40);
        if (!c->undecorated) c->y = std::clamp(c->y, BAR_H, SH - TITLE_H);
    }
    XSetWindowAttributes a = {};
    a.background_pixel = pen_grey;
    a.event_mask = SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
                   PointerMotionMask | ExposureMask | EnterWindowMask;
    c->frame = XCreateWindow(dpy, c->screen->win, c->x, c->y, c->w, c->h, 0, CopyFromParent, InputOutput,
                             CopyFromParent, CWBackPixel | CWEventMask, &a);
    XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask);
    // (the title again: one changed between the first look and the selection went unseen)
    c->title = window_title(w);
    XSetWindowBorderWidth(dpy, w, 0);
    XAddToSaveSet(dpy, w);
    if (wa.map_state == IsViewable) c->ignore_unmap++;   // reparenting unmaps it once
    XReparentWindow(dpy, w, c->frame, c->bare() ? 0 : BORDER_L, c->bare() ? 0 : TITLE_H);
    // clicks in the window: activate it first (the click goes on to the program)
    XGrabButton(dpy, AnyButton, AnyModifier, w, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    by_window[w] = c;
    by_frame[c->frame] = c;
    if (c->backdrop) c->screen->clients.insert(c->screen->clients.begin(), c);
    else c->screen->clients.push_back(c);
    place(c);
    XMapWindow(dpy, w);
    XMapWindow(dpy, c->frame);
    set_wm_state(w, NormalState);
    restack(c->screen);
    if (has_state(w, NET_WM_STATE_FULLSCREEN)) set_fullscreen(c, true);
    // the type may have come between reading it and watching the window's properties
    if (!c->backdrop && !c->fullscreen && has_type(w, NET_WM_WINDOW_TYPE_DESKTOP)) make_backdrop(c);
    if (!c->backdrop && !c->no_focus) activate(c);
    publish_client_list();
}

void unmanage(Client *c, bool destroyed) {
    auto &v = c->screen->clients;
    v.erase(std::remove(v.begin(), v.end(), c), v.end());
    by_window.erase(c->win);
    by_frame.erase(c->frame);
    if (!destroyed) {
        XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
        set_wm_state(c->win, WithdrawnState);
        XReparentWindow(dpy, c->win, root, c->x, c->y + c->screen->y);
        XRemoveFromSaveSet(dpy, c->win);
    }
    XDestroyWindow(dpy, c->frame);
    if (active == c) {
        active = nullptr;
        // the next window in front on the same screen becomes active
        for (auto it = v.rbegin(); it != v.rend(); ++it) if (!(*it)->backdrop) { activate(*it); break; }
    }
    delete c;
    publish_client_list();
}

Gadget gadget_at(Client *c, int x, int y) {
    if (c->bare()) return G_NONE;
    if (themed_ok()) {
        decor::WindowFrame f = frame_of(c);
        if (f.close.contains(x, y)) return G_CLOSE;
        if (f.depth.contains(x, y)) return G_DEPTH;
        if (f.zoom.contains(x, y)) return G_ZOOM;
        if (y < f.border_top) return G_TITLE;
        if (f.size.contains(x, y)) return G_SIZE;
        return G_NONE;
    }
    if (y < TITLE_H) {
        if (x < GADGET_W) return G_CLOSE;
        if (x >= c->w - GADGET_W) return G_DEPTH;
        if (x >= c->w - 2 * GADGET_W) return G_ZOOM;
        return G_TITLE;
    }
    if (x >= c->w - SIZE_W && y >= c->h - SIZE_H) return G_SIZE;
    return G_NONE;
}

// --------------------------------------------------------------------------------------------
// Screens
// --------------------------------------------------------------------------------------------
WScreen *open_screen(const std::string &title) {
    auto *s = new WScreen;
    s->title = title;
    XSetWindowAttributes a = {};
    a.background_pixel = pen_grey;
    a.event_mask = SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
                   PointerMotionMask | ExposureMask;
    s->win = XCreateWindow(dpy, root, 0, 0, SW, SH, 0, CopyFromParent, InputOutput, CopyFromParent,
                           CWBackPixel | CWEventMask, &a);
    XMapWindow(dpy, s->win);
    screens.push_back(s);
    return s;
}

void restack_screens() {
    std::vector<Window> order;
    for (auto it = screens.rbegin(); it != screens.rend(); ++it) order.push_back((*it)->win);
    XRestackWindows(dpy, order.data(), (int)order.size());
}

void screen_to_front(WScreen *s) {
    screens.erase(std::remove(screens.begin(), screens.end(), s), screens.end());
    screens.push_back(s);
    restack_screens();
    // the front screen's window in front gets active
    Client *top = nullptr;
    for (auto *c : s->clients) if (!c->backdrop) top = c;
    activate(top);
}

void screen_to_back(WScreen *s) {
    screens.erase(std::remove(screens.begin(), screens.end(), s), screens.end());
    screens.insert(screens.begin(), s);
    restack_screens();
    Client *top = nullptr;
    for (auto *c : front_screen()->clients) if (!c->backdrop) top = c;
    activate(top);
}

void move_screen(WScreen *s, int y) {
    // (to the bottom edge, or as far as leaves its bar in sight)
    s->y = std::clamp(y, 0, ic.drag_bottom ? SH - 1 : SH - BAR_H);
    XMoveWindow(dpy, s->win, 0, s->y);
}

void screen_moved(WScreen *s) {
    for (auto *c : s->clients) send_configure(c);
}

// --------------------------------------------------------------------------------------------
// Dragging: a window (move / size) or a screen
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
// Menus: while the right mouse button is held over a window that has menus
// (_RAMBLER_MENU), the screen's title bar shows the menu titles of that window; the menu
// under the pointer drops down, a sub-menu opens beside its item; releasing the button over
// an item picks it (_RAMBLER_MENU_PICK is sent to the window).
// --------------------------------------------------------------------------------------------
Atom RAMBLER_MENU, RAMBLER_MENU_PICK;

struct MenuItem {
    int id = 0, flags = 0;               // 1 disabled, 2 check mark item, 4 checked, 8 line after
    std::string label, key;
    std::vector<MenuItem> children;
    bool disabled() const { return flags & 1; }
};

struct MenuState {
    bool open = false;
    Client *c = nullptr;
    WScreen *s = nullptr;
    std::vector<MenuItem> titles;
    std::vector<int> title_x, title_w;
    int title = -1;                      // the menu dropped down
    Window box = 0, sub = 0;             // the drop-down box and a sub-menu's box
    int box_x = 0, box_y = 0, box_w = 0, box_h = 0;
    int sub_x = 0, sub_y = 0, sub_w = 0, sub_h = 0;
    int item = -1, sub_item = -1, sub_of = -1;
    Time opened = 0;                     // when the menu button went down
    bool stuck = false;                  // sticky menus (IControl): left open by a click
    bool popup = false;                  // pop-up menus (IControl): the titles in tbox
    Window tbox = 0;
    int tbox_x = 0, tbox_y = 0, tbox_w = 0, tbox_h = 0;
} menu;
Time last_press = 0;                     // (the time of the last button press)

const int ITEM_H = 18, MENU_PAD = 4;

bool read_menu(Client *c, std::vector<MenuItem> &out) {
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = nullptr;
    out.clear();
    if (XGetWindowProperty(dpy, c->win, RAMBLER_MENU, 0, 1 << 16, False, UTF8_STRING, &type, &fmt, &n, &after, &data) != Success || !data)
        return false;
    std::string all((char *)data, n);
    XFree(data);
    std::vector<std::vector<MenuItem> *> stack = {&out};
    size_t pos = 0;
    while (pos < all.size()) {
        size_t nl = all.find('\n', pos);
        std::string line = all.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? all.size() : nl + 1;
        std::vector<std::string> f;
        size_t a = 0, t;
        while ((t = line.find('\t', a)) != std::string::npos) { f.push_back(line.substr(a, t - a)); a = t + 1; }
        f.push_back(line.substr(a));
        if (f.size() < 5) continue;
        int depth = atoi(f[0].c_str());
        if (depth < 0 || depth >= (int)stack.size()) continue;
        stack.resize(depth + 1);
        MenuItem m;
        m.id = atoi(f[1].c_str());
        m.flags = atoi(f[2].c_str());
        m.label = f[3];
        m.key = f[4];
        stack[depth]->push_back(m);
        stack.push_back(&stack[depth]->back().children);
    }
    return !out.empty();
}

void menu_box_size(const std::vector<MenuItem> &items, int &w, int &h) {
    int lw = 0, kw = 0;
    h = 2 * MENU_PAD;
    for (auto &m : items) {
        lw = std::max(lw, text_width(m.label));
        if (!m.key.empty()) kw = std::max(kw, text_width(m.key));
        h += ITEM_H + ((m.flags & 8) ? 6 : 0);
    }
    w = 2 * MENU_PAD + 18 + lw + (kw ? kw + 24 : 0) + 18;
}

int item_y(const std::vector<MenuItem> &items, int i) {
    int y = MENU_PAD;
    for (int k = 0; k < i; k++) y += ITEM_H + ((items[k].flags & 8) ? 6 : 0);
    return y;
}

int item_at(const std::vector<MenuItem> &items, int y) {
    for (int i = 0; i < (int)items.size(); i++) {
        int iy = item_y(items, i);
        if (y >= iy && y < iy + ITEM_H) return i;
    }
    return -1;
}

void draw_menu_box(Window w, const std::vector<MenuItem> &items, int bw, int bh, int hi) {
    GC gc = gc_for(w);
    // BARBLOCK (white) with a BARDETAIL (black) outline, items in BARDETAIL; the item under
    // the pointer in the fill pen (FILLTEXT on it)
    // (IControl's 3D look: the background pen in a raised bevel, the item under the pointer
    // pressed in)
    unsigned long block = ic.menu_3d ? pen_grey : pen_white;
    fill(w, gc, block, 0, 0, bw, bh);
    if (ic.menu_3d) bevel(w, gc, 0, 0, bw, bh, false);
    else {
        XSetForeground(dpy, gc, pen_black);
        XDrawRectangle(dpy, w, gc, 0, 0, bw - 1, bh - 1);
    }
    for (int i = 0; i < (int)items.size(); i++) {
        auto &m = items[i];
        int y = item_y(items, i);
        bool lit = i == hi && !m.disabled();
        if (lit && ic.menu_3d) bevel(w, gc, 2, y, bw - 4, ITEM_H, true);
        else if (lit) fill(w, gc, pen_blue, 2, y, bw - 4, ITEM_H);
        XftColor *col = &xft_black;
        int ty = y + (ITEM_H + font->ascent - font->descent) / 2;
        if ((m.flags & 2) && (m.flags & 4)) {   // the check mark
            XSetForeground(dpy, gc, pen_black);
            XSegment seg[2] = {{(short)(MENU_PAD + 2), (short)(y + 9), (short)(MENU_PAD + 6), (short)(y + 13)},
                               {(short)(MENU_PAD + 6), (short)(y + 13), (short)(MENU_PAD + 13), (short)(y + 4)}};
            XSetLineAttributes(dpy, gc, 2, LineSolid, CapRound, JoinRound);
            XDrawSegments(dpy, w, gc, seg, 2);
            XSetLineAttributes(dpy, gc, 0, LineSolid, CapButt, JoinMiter);
        }
        draw_text(w, MENU_PAD + 18, ty, bw - MENU_PAD * 2 - 18, m.label, col);
        if (!m.key.empty()) draw_text(w, bw - MENU_PAD - 12 - text_width(m.key), ty, text_width(m.key) + 2, m.key, col);
        if (!m.children.empty()) {   // a sub-menu: an arrow
            XSetForeground(dpy, gc, pen_black);
            XPoint tri[3] = {{(short)(bw - 12), (short)(y + 5)}, {(short)(bw - 7), (short)(y + 9)}, {(short)(bw - 12), (short)(y + 13)}};
            XFillPolygon(dpy, w, gc, tri, 3, Convex, CoordModeOrigin);
        }
        if (m.disabled()) {
            // ghosted: a pattern of the block pen over it
            static const char stip[] = {0x05, 0x0a};
            Pixmap pm = XCreateBitmapFromData(dpy, w, stip, 4, 2);
            XSetStipple(dpy, gc, pm);
            XSetFillStyle(dpy, gc, FillStippled);
            XSetForeground(dpy, gc, block);
            XFillRectangle(dpy, w, gc, 2, y, bw - 4, ITEM_H);
            XSetFillStyle(dpy, gc, FillSolid);
            XFreePixmap(dpy, pm);
        }
        if (m.flags & 8) {
            XSetForeground(dpy, gc, pen_black);
            XDrawLine(dpy, w, gc, 4, y + ITEM_H + 2, bw - 5, y + ITEM_H + 2);
        }
    }
    XFreeGC(dpy, gc);
}

Window make_box(int x, int y, int w, int h) {
    XSetWindowAttributes a = {};
    a.override_redirect = True;
    a.background_pixel = ic.menu_3d ? pen_grey : pen_white;
    a.save_under = True;
    a.event_mask = ExposureMask;
    Window b = XCreateWindow(dpy, root, x, y, w, h, 0, CopyFromParent, InputOutput, CopyFromParent,
                             CWOverrideRedirect | CWBackPixel | CWSaveUnder | CWEventMask, &a);
    XMapRaised(dpy, b);
    return b;
}

void draw_menu_bar() {
    WScreen *s = menu.s;
    GC gc = gc_for(s->win);
    fill(s->win, gc, pen_white, 0, 0, SW, BAR_H - 1);
    fill(s->win, gc, pen_black, 0, BAR_H - 1, SW, 1);
    menu.title_x.clear();
    menu.title_w.clear();
    int x = 8;
    for (int i = 0; i < (int)menu.titles.size(); i++) {
        int w = text_width(menu.titles[i].label) + 16;
        menu.title_x.push_back(x);
        menu.title_w.push_back(w);
        bool lit = i == menu.title;
        if (lit) fill(s->win, gc, pen_blue, x, 0, w, BAR_H - 1);
        draw_text(s->win, x + 8, (BAR_H - 1 + font->ascent - font->descent) / 2, w, menu.titles[i].label, &xft_black);
        if (menu.titles[i].disabled()) {
            static const char stip[] = {0x05, 0x0a};
            Pixmap pm = XCreateBitmapFromData(dpy, s->win, stip, 4, 2);
            XSetStipple(dpy, gc, pm);
            XSetFillStyle(dpy, gc, FillStippled);
            XSetForeground(dpy, gc, pen_white);
            XFillRectangle(dpy, s->win, gc, x, 0, w, BAR_H - 1);
            XSetFillStyle(dpy, gc, FillSolid);
            XFreePixmap(dpy, pm);
        }
        x += w;
    }
    XFreeGC(dpy, gc);
}

void close_sub() {
    if (menu.sub) XDestroyWindow(dpy, menu.sub);
    menu.sub = 0;
    menu.sub_of = menu.sub_item = -1;
}

void close_box() {
    close_sub();
    if (menu.box) XDestroyWindow(dpy, menu.box);
    menu.box = 0;
    menu.item = -1;
}

void open_title(int t) {
    if (t == menu.title) return;
    close_box();
    menu.title = t;
    if (menu.popup) draw_menu_box(menu.tbox, menu.titles, menu.tbox_w, menu.tbox_h, t);
    else draw_menu_bar();
    if (t < 0 || menu.titles[t].disabled() || menu.titles[t].children.empty()) return;
    menu_box_size(menu.titles[t].children, menu.box_w, menu.box_h);
    if (menu.popup) {
        // beside the title in the pop-up box (on its left when there is no room on the right)
        menu.box_x = menu.tbox_x + menu.tbox_w - 8;
        if (menu.box_x + menu.box_w > SW) menu.box_x = std::max(0, menu.tbox_x - menu.box_w + 8);
        menu.box_y = std::clamp(menu.tbox_y + item_y(menu.titles, t) - MENU_PAD, 0, std::max(0, SH - menu.box_h));
    } else {
        menu.box_x = std::min(menu.title_x[t], SW - menu.box_w);
        menu.box_y = menu.s->y + BAR_H - 1;
    }
    menu.box = make_box(menu.box_x, menu.box_y, menu.box_w, menu.box_h);
    draw_menu_box(menu.box, menu.titles[t].children, menu.box_w, menu.box_h, -1);
}

void open_sub(int item) {
    if (menu.sub_of == item) return;
    close_sub();
    auto &items = menu.titles[menu.title].children;
    if (item < 0 || items[item].children.empty() || items[item].disabled()) return;
    menu.sub_of = item;
    menu_box_size(items[item].children, menu.sub_w, menu.sub_h);
    menu.sub_x = menu.box_x + menu.box_w - 8;
    if (menu.sub_x + menu.sub_w > SW) menu.sub_x = std::max(0, menu.box_x - menu.sub_w + 8);
    menu.sub_y = menu.box_y + item_y(items, item) - MENU_PAD;
    menu.sub = make_box(menu.sub_x, menu.sub_y, menu.sub_w, menu.sub_h);
    draw_menu_box(menu.sub, items[item].children, menu.sub_w, menu.sub_h, -1);
}

bool menu_begin(Client *c, bool from_bar = false) {
    if (!c || !read_menu(c, menu.titles)) return false;
    menu.open = true;
    menu.c = c;
    menu.s = c->screen;
    menu.title = -1;
    menu.opened = last_press;
    menu.stuck = false;
    // pop-up menus (IControl): the menu titles in a box at the pointer - but a click on the
    // screen bar drops them down from it, when the titles are to stay there
    menu.popup = ic.menu_popup && !(from_bar && ic.title_pulldown);
    if (menu.popup) {
        Window r, ch;
        int px = 0, py = 0, wx, wy;
        unsigned mask;
        XQueryPointer(dpy, root, &r, &ch, &px, &py, &wx, &wy, &mask);
        menu_box_size(menu.titles, menu.tbox_w, menu.tbox_h);
        menu.tbox_x = std::clamp(px - 8, 0, std::max(0, SW - menu.tbox_w));
        menu.tbox_y = std::clamp(py - MENU_PAD - ITEM_H / 2, 0, std::max(0, SH - menu.tbox_h));
        menu.tbox = make_box(menu.tbox_x, menu.tbox_y, menu.tbox_w, menu.tbox_h);
        draw_menu_box(menu.tbox, menu.titles, menu.tbox_w, menu.tbox_h, -1);
    } else draw_menu_bar();
    XGrabPointer(dpy, root, False, ButtonReleaseMask | PointerMotionMask | ButtonPressMask, GrabModeAsync,
                 GrabModeAsync, None, None, CurrentTime);
    return true;
}

void menu_motion(int xr, int yr) {
    // pop-up: over the titles' box, the title under the pointer opens beside it
    if (menu.popup && xr >= menu.tbox_x && xr < menu.tbox_x + menu.tbox_w && yr >= menu.tbox_y &&
        yr < menu.tbox_y + menu.tbox_h) {
        int t = item_at(menu.titles, yr - menu.tbox_y);
        if (t >= 0) open_title(t);
        return;
    }
    // over the bar: the title under the pointer drops down
    if (!menu.popup && yr >= menu.s->y && yr < menu.s->y + BAR_H) {
        for (int i = 0; i < (int)menu.titles.size(); i++)
            if (xr >= menu.title_x[i] && xr < menu.title_x[i] + menu.title_w[i]) { open_title(i); break; }
        if (menu.box && menu.item != -1) { menu.item = -1; draw_menu_box(menu.box, menu.titles[menu.title].children, menu.box_w, menu.box_h, -1); }
        return;
    }
    // over a sub-menu
    if (menu.sub && xr >= menu.sub_x && xr < menu.sub_x + menu.sub_w && yr >= menu.sub_y && yr < menu.sub_y + menu.sub_h) {
        auto &items = menu.titles[menu.title].children[menu.sub_of].children;
        int it = item_at(items, yr - menu.sub_y);
        if (it != menu.sub_item) { menu.sub_item = it; draw_menu_box(menu.sub, items, menu.sub_w, menu.sub_h, it); }
        return;
    }
    if (menu.sub && menu.sub_item != -1) {
        menu.sub_item = -1;
        draw_menu_box(menu.sub, menu.titles[menu.title].children[menu.sub_of].children, menu.sub_w, menu.sub_h, -1);
    }
    // over the drop-down box
    if (menu.box && xr >= menu.box_x && xr < menu.box_x + menu.box_w && yr >= menu.box_y && yr < menu.box_y + menu.box_h) {
        auto &items = menu.titles[menu.title].children;
        int it = item_at(items, yr - menu.box_y);
        if (it != menu.item) {
            menu.item = it;
            draw_menu_box(menu.box, items, menu.box_w, menu.box_h, it);
            open_sub(it);
        }
        return;
    }
    if (menu.box && menu.item != -1 && !menu.sub) {
        menu.item = -1;
        draw_menu_box(menu.box, menu.titles[menu.title].children, menu.box_w, menu.box_h, -1);
    }
}

// The screen drag key (IControl) + the left button anywhere: dragging screens (also with Caps
// Lock or Num Lock on); grabbed again when the key changes
void grab_meta_drag() {
    static unsigned grabbed = 0;
    unsigned extras[] = {0u, (unsigned)LockMask, (unsigned)Mod2Mask, (unsigned)(LockMask | Mod2Mask)};
    if (grabbed)
        for (unsigned extra : extras) XUngrabButton(dpy, Button1, grabbed | extra, root);
    for (unsigned extra : extras)
        XGrabButton(dpy, Button1, ic.meta_drag | extra, root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, None);
    grabbed = ic.meta_drag;
}

// the item under the pointer, if it can be picked (0: none)
int menu_current_id() {
    if (menu.title < 0) return 0;
    auto &items = menu.titles[menu.title].children;
    if (menu.sub && menu.sub_item >= 0) {
        auto &m = items[menu.sub_of].children[menu.sub_item];
        return m.disabled() ? 0 : m.id;
    }
    if (menu.item >= 0 && items[menu.item].children.empty() && !items[menu.item].disabled()) return items[menu.item].id;
    return 0;
}

void send_menu_pick(Client *c, int id) {
    if (!id || !c || !by_window.count(c->win)) return;
    XEvent e = {};
    e.xclient.type = ClientMessage;
    e.xclient.window = c->win;
    e.xclient.message_type = RAMBLER_MENU_PICK;
    e.xclient.format = 32;
    e.xclient.data.l[0] = id;
    XSendEvent(dpy, c->win, False, NoEventMask, &e);
    XFlush(dpy);
}

void menu_end(bool pick) {
    int id = pick ? menu_current_id() : 0;
    close_box();
    if (menu.tbox) XDestroyWindow(dpy, menu.tbox);
    XUngrabPointer(dpy, CurrentTime);
    WScreen *s = menu.s;
    Client *c = menu.c;
    menu = MenuState{};
    s->draw_bar();
    send_menu_pick(c, id);
}

enum DragKind { D_NONE, D_MOVE, D_SIZE, D_SCREEN, D_GADGET };
struct Drag {
    DragKind kind = D_NONE;
    Client *c = nullptr;
    WScreen *s = nullptr;
    int px = 0, py = 0;            // where the pointer was pressed (root)
    int ox = 0, oy = 0, ow = 0, oh = 0;
    Gadget gadget = G_NONE;
} drag;

void begin_grab() {
    XGrabPointer(dpy, root, False, ButtonReleaseMask | PointerMotionMask | ButtonPressMask, GrabModeAsync,
                 GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
}

void end_grab() {
    XUngrabPointer(dpy, CurrentTime);
    XUngrabKeyboard(dpy, CurrentTime);
    drag = Drag{};
}

void cancel_drag() {
    if (drag.kind == D_SCREEN) { move_screen(drag.s, drag.oy); screen_moved(drag.s); }
    else if ((drag.kind == D_MOVE || drag.kind == D_SIZE) && drag.c) {
        drag.c->x = drag.ox; drag.c->y = drag.oy; drag.c->w = drag.ow; drag.c->h = drag.oh;
        place(drag.c);
    } else if (drag.kind == D_GADGET && drag.c) { drag.c->pressed = G_NONE; draw_frame(drag.c); }
    end_grab();
}

WScreen *screen_of(Window w) {
    for (auto *s : screens) if (s->win == w) return s;
    return nullptr;
}

void button_press(XButtonEvent &e) {
    // a press in a program's window comes through the synchronous grab (click to activate):
    // whatever happens to it, the pointer is let go again (else it stays frozen for the
    // program's window - every later click there would be lost)
    struct Release {
        bool sync;
        ~Release() { if (sync) XAllowEvents(dpy, AsyncPointer, CurrentTime); }
    } release{by_window.count(e.window) > 0};
    last_press = e.time;
    // the left button while the menus are held open: the item is picked and the menus stay
    // (the original's multiple selection: one menu pass, several items)
    if (menu.open && e.button == Button1) {
        int id = menu_current_id();
        if (menu.stuck) {
            // sticky menus: a click picks the item and closes them; a click beside them closes
            // them
            menu_end(id != 0);
            return;
        }
        if (id) send_menu_pick(menu.c, id);
        return;
    }
    if (getenv("WM_DEBUG"))
        fprintf(stderr, "press win=%lx sub=%lx b=%u st=%x menu=%d drag=%d client=%d frame=%d\n", e.window, e.subwindow,
                e.button, e.state, menu.open, (int)drag.kind, (int)by_window.count(e.window), (int)by_frame.count(e.window));
    if (menu.open) return;
    if (drag.kind != D_NONE) {
        if (e.button == Button3) cancel_drag();   // the right button cancels a drag
        return;
    }
    // Super + the left button anywhere: the screen under the pointer is dragged (as the
    // left system key with the mouse button drags a screen on the original system)
    if (e.window == root && e.button == Button1 && (e.state & ic.meta_drag)) {
        WScreen *s = nullptr;
        for (auto it = screens.rbegin(); it != screens.rend(); ++it)
            if (e.y_root >= (*it)->y) { s = *it; break; }   // the front-most screen there
        if (!s) return;
        if (s != front_screen()) screen_to_front(s);
        drag.kind = D_SCREEN;
        drag.s = s;
        drag.py = e.y_root;
        drag.oy = s->y;
        begin_grab();
        return;
    }
    // a click inside a program's window: activate it, let the click through
    if (by_window.count(e.window)) {
        Client *c = by_window[e.window];
        if (c->screen != front_screen() && c->screen->y == 0) screen_to_front(c->screen);
        // (the backdrop too: as the system's backdrop window, it takes the keys and its menus
        // show; it is not raised - activating raises nothing)
        activate(c);
        // ClickToFront: a double click (or a click with its key held) brings it to the front
        if (cx.click_to_front && e.button == Button1 && !c->backdrop) {
            static Window last_win = 0;
            static Time last_time = 0;
            bool to = cx.ctf_mask ? (e.state & cx.ctf_mask) != 0 : (e.window == last_win && e.time - last_time < 400);
            last_win = e.window;
            last_time = to ? 0 : e.time;
            if (to) to_front(c);
        }
        // the right button over a window with menus: the screen bar shows them
        release.sync = false;
        if (e.button == Button3 && menu_begin(c)) {
            XAllowEvents(dpy, AsyncPointer, CurrentTime);
            return;
        }
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
        return;
    }
    if (WScreen *s = screen_of(e.window)) {
        if (e.y < BAR_H && e.button == Button1) {
            if (e.x >= SW - GADGET_W) {
                // the depth gadget: the front screen goes back, another comes to the front
                if (s == front_screen()) screen_to_back(s);
                else screen_to_front(s);
                return;
            }
            if (s != front_screen()) screen_to_front(s);
            drag.kind = D_SCREEN;
            drag.s = s;
            drag.py = e.y_root;
            drag.oy = s->y;
            begin_grab();
        } else if (e.button == Button3) {
            // the right button over the screen: the active window's menus (or the backdrop's)
            Client *c = active && active->screen == s ? active : nullptr;
            if (!c) for (auto *k : s->clients) if (k->backdrop) c = k;
            menu_begin(c, true);
        } else if (s != front_screen()) screen_to_front(s);
        return;
    }
    if (!by_frame.count(e.window)) return;
    Client *c = by_frame[e.window];
    if (c->screen != front_screen()) screen_to_front(c->screen);
    activate(c);
    if (e.button == Button3) { menu_begin(c); return; }
    if (e.button != Button1) return;
    Gadget g = gadget_at(c, e.x, e.y);
    drag.c = c;
    drag.px = e.x_root;
    drag.py = e.y_root;
    drag.ox = c->x; drag.oy = c->y; drag.ow = c->w; drag.oh = c->h;
    if (g == G_TITLE || g == G_NONE) drag.kind = D_MOVE;
    else if (g == G_SIZE) { drag.kind = D_SIZE; c->pressed = G_SIZE; draw_frame(c); }
    else { drag.kind = D_GADGET; drag.gadget = g; c->pressed = g; draw_frame(c); }
    begin_grab();
}

void motion(XMotionEvent &e) {
    if (menu.open) {
        while (XCheckTypedEvent(dpy, MotionNotify, (XEvent *)&e)) {}
        menu_motion(e.x_root, e.y_root);
        return;
    }
    while (XCheckTypedEvent(dpy, MotionNotify, (XEvent *)&e)) {}
    int dx = e.x_root - drag.px, dy = e.y_root - drag.py;
    switch (drag.kind) {
    case D_SCREEN: move_screen(drag.s, drag.oy + dy); break;
    case D_MOVE:
        drag.c->x = drag.ox + dx;
        drag.c->y = std::max(BAR_H, drag.oy + dy);
        if (!ic.offscreen) {   // (the whole window stays on the screen)
            drag.c->x = std::clamp(drag.c->x, 0, std::max(0, SW - drag.c->w));
            drag.c->y = std::clamp(drag.c->y, BAR_H, std::max(BAR_H, SH - drag.c->h));
        }
        XMoveWindow(dpy, drag.c->frame, drag.c->x, drag.c->y);
        break;
    case D_SIZE: {
        Client *c = drag.c;
        int w = std::max(c->min_w, drag.ow + dx), h = std::max(c->min_h, drag.oh + dy);
        if (c->max_w) w = std::min(w, c->max_w);
        if (c->max_h) h = std::min(h, c->max_h);
        // whole steps of the program's increment (a terminal: whole characters)
        int cw = w - BORDER_L - BORDER_R - c->base_w, ch = h - TITLE_H - BORDER_B - c->base_h;
        if (c->inc_w > 1 && cw > 0) w -= cw % c->inc_w;
        if (c->inc_h > 1 && ch > 0) h -= ch % c->inc_h;
        if (w != c->w || h != c->h) { c->w = w; c->h = h; place(c); }
        break;
    }
    case D_GADGET: {
        // the gadget stays pressed while the pointer is over it
        Window child;
        int fx, fy;
        XTranslateCoordinates(dpy, root, drag.c->frame, e.x_root, e.y_root, &fx, &fy, &child);
        Gadget over = gadget_at(drag.c, fx, fy) == drag.gadget ? drag.gadget : G_NONE;
        if (over != drag.c->pressed) { drag.c->pressed = over; draw_frame(drag.c); }
        break;
    }
    default: break;
    }
}

void button_release(XButtonEvent &e) {
    if (menu.open) {
        if (e.button != Button3) return;
        // sticky menus (IControl): a short click of the menu button, over no item, leaves the
        // menus open; the next click picks
        if (ic.menu_sticky && !menu.stuck && e.time - menu.opened < 400 && !menu_current_id()) {
            menu.stuck = true;
            return;
        }
        menu_end(true);
        return;
    }
    Drag d = drag;
    if (d.kind == D_NONE || e.button != Button1) return;
    end_grab();
    switch (d.kind) {
    case D_SCREEN: screen_moved(d.s); break;
    case D_MOVE: send_configure(d.c); break;
    case D_SIZE: d.c->pressed = G_NONE; place(d.c); break;
    case D_GADGET:
        if (d.c->pressed == d.gadget) {
            d.c->pressed = G_NONE;
            draw_frame(d.c);
            if (d.gadget == G_CLOSE) close_window(d.c);
            else if (d.gadget == G_ZOOM) zoom(d.c);
            else if (d.gadget == G_DEPTH) {
                if (in_front(d.c)) to_back(d.c);
                else to_front(d.c);
            }
        } else { d.c->pressed = G_NONE; draw_frame(d.c); }
        break;
    default: break;
    }
}

// Is the right Super key (and not the left one) held? The left one is the system's, the right
// one the programs' (as the left and the right Amiga keys)
bool right_super_only() {
    char keys[32];
    XQueryKeymap(dpy, keys);
    auto down = [&](KeySym s) {
        KeyCode c = XKeysymToKeycode(dpy, s);
        return c && (keys[c / 8] & (1 << (c % 8)));
    };
    return down(XK_Super_R) && !down(XK_Super_L);
}

void key_press(XKeyEvent &e) {
    KeySym k = XLookupKeysym(&e, 0);
    if ((e.state & Mod4Mask) && (k == XK_m || k == XK_n)) {
        // the screen keys belong to the left Super key; with the right one the key goes on to
        // the active window (a program's Super+N, Super+M)
        if (getenv("WM_DEBUG")) {
            char keys[32];
            XQueryKeymap(dpy, keys);
            for (int c = 0; c < 256; c++) if (keys[c / 8] & (1 << (c % 8))) fprintf(stderr, "down %d ", c);
            fprintf(stderr, "L=%d R=%d right only=%d\n", XKeysymToKeycode(dpy, XK_Super_L), XKeysymToKeycode(dpy, XK_Super_R), (int)right_super_only());
        }
        if (drag.kind == D_NONE && right_super_only()) { XAllowEvents(dpy, ReplayKeyboard, e.time); return; }
        XAllowEvents(dpy, AsyncKeyboard, e.time);
    }
    if (drag.kind != D_NONE) {
        if (k == XK_Escape) cancel_drag();
        return;
    }
    if ((e.state & Mod4Mask) && (k == XK_m || k == XK_M)) {
        // Super+M: the screen in the back comes to the front
        if (screens.size() > 1) screen_to_front(screens.front());
    } else if ((e.state & Mod4Mask) && (k == XK_n || k == XK_N)) {
        // Super+N: the front screen goes to the back
        if (screens.size() > 1) screen_to_back(front_screen());
    }
}

int on_error(Display *, XErrorEvent *e) {
    if (e->error_code == BadAccess && e->request_code == 2) {   // ChangeWindowAttributes on root
        fprintf(stderr, "rambler-wm: another window manager is running\n");
        exit(1);
    }
    if (getenv("RAMBLER_WM_DEBUG")) fprintf(stderr, "rambler-wm: X error %d (request %d)\n", e->error_code, e->request_code);
    return 0;   // windows that went away meanwhile
}

Atom atom(const char *n) { return XInternAtom(dpy, n, False); }

void tray_layout() {
    if (!main_screen) return;
    int x = SW - GADGET_W - TRAY_GAP - TRAY_SIZE;
    for (Window w : tray_icons) {
        XMoveResizeWindow(dpy, w, x, (BAR_H - 1 - TRAY_SIZE) / 2, TRAY_SIZE, TRAY_SIZE);
        x -= TRAY_SIZE + TRAY_GAP;
    }
    main_screen->draw_bar();
}

void tray_dock(Window icon) {
    if (!main_screen || is_tray_icon(icon)) return;
    XSelectInput(dpy, icon, StructureNotifyMask | PropertyChangeMask);
    XSetWindowBackgroundPixmap(dpy, icon, ParentRelative);
    XReparentWindow(dpy, icon, main_screen->win, 0, 0);
    XAddToSaveSet(dpy, icon);
    tray_icons.push_back(icon);
    // XEMBED_EMBEDDED_NOTIFY: the icon knows it is in its place
    XEvent e = {};
    e.xclient.type = ClientMessage;
    e.xclient.window = icon;
    e.xclient.message_type = XEMBED;
    e.xclient.format = 32;
    e.xclient.data.l[0] = CurrentTime;
    e.xclient.data.l[1] = 0;
    e.xclient.data.l[3] = main_screen->win;
    e.xclient.data.l[4] = 0;
    XSendEvent(dpy, icon, False, NoEventMask, &e);
    tray_layout();
    XMapRaised(dpy, icon);
}

void tray_remove(Window icon) {
    auto it = std::find(tray_icons.begin(), tray_icons.end(), icon);
    if (it == tray_icons.end()) return;
    tray_icons.erase(it);
    tray_layout();
}

// The tray's owner: the selection _NET_SYSTEM_TRAY_Sn and the MANAGER announcement
void tray_start() {
    char name[32];
    snprintf(name, sizeof name, "_NET_SYSTEM_TRAY_S%d", scr);
    NET_SYSTEM_TRAY_S = atom(name);
    NET_SYSTEM_TRAY_OPCODE = atom("_NET_SYSTEM_TRAY_OPCODE");
    XEMBED = atom("_XEMBED");
    XEMBED_INFO = atom("_XEMBED_INFO");
    MANAGER = atom("MANAGER");
    if (XGetSelectionOwner(dpy, NET_SYSTEM_TRAY_S) != None) return;   // another tray runs
    tray_owner = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
    long orient = 0;   // horizontal
    XChangeProperty(dpy, tray_owner, atom("_NET_SYSTEM_TRAY_ORIENTATION"), XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&orient, 1);
    XSetSelectionOwner(dpy, NET_SYSTEM_TRAY_S, tray_owner, CurrentTime);
    if (XGetSelectionOwner(dpy, NET_SYSTEM_TRAY_S) != tray_owner) return;
    XEvent e = {};
    e.xclient.type = ClientMessage;
    e.xclient.window = root;
    e.xclient.message_type = MANAGER;
    e.xclient.format = 32;
    e.xclient.data.l[0] = CurrentTime;
    e.xclient.data.l[1] = NET_SYSTEM_TRAY_S;
    e.xclient.data.l[2] = tray_owner;
    XSendEvent(dpy, root, False, StructureNotifyMask, &e);
}

// The bell (a program's XBell, a mistake typed): the front screen flashes in the fill pen for
// a moment, as the system flashes the screen when it beeps
int xkb_event = -1;
// the Sound preferences: flash, sound or both (from sound.prefs, then the _RAMBLER_SOUND
// property when the preferences program changes them)
rambler::BeepPrefs beep_prefs;
void display_beep() {
    if (beep_prefs.sound) rambler::play_beep_sound(beep_prefs);
    if (!beep_prefs.flash) return;
    WScreen *s = screens.empty() ? nullptr : screens.back();
    int y = s ? s->y : 0;
    XSetWindowAttributes a = {};
    a.override_redirect = True;
    a.background_pixel = pen_blue;
    Window f = XCreateWindow(dpy, root, 0, y, SW, SH - y, 0, CopyFromParent, InputOutput, CopyFromParent,
                             CWOverrideRedirect | CWBackPixel, &a);
    // no input of its own: a click during the flash goes to the window under it (the
    // original's DisplayBeep only flashes the screen's colours)
    XShapeCombineRectangles(dpy, f, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);
    XMapRaised(dpy, f);
    XFlush(dpy);
    struct pollfd none = {-1, 0, 0};
    poll(&none, 0, 120);
    XDestroyWindow(dpy, f);
    XFlush(dpy);
}

// The pens from four colours (hex RRGGBB): the background, the shadow (and text), the shine,
// the fill; HALFSHADOW half way between the background and the shadow
void apply_palette(const std::string &bg, const std::string &sd, const std::string &sh, const std::string &fl) {
    pen_grey = alloc(("#" + bg).c_str());
    pen_black = alloc(("#" + sd).c_str());
    pen_white = alloc(("#" + sh).c_str());
    pen_blue = alloc(("#" + fl).c_str());
    unsigned b = (unsigned)strtoul(bg.c_str(), nullptr, 16), d = (unsigned)strtoul(sd.c_str(), nullptr, 16);
    char hs[8];
    snprintf(hs, sizeof hs, "#%02X%02X%02X", (((b >> 16) & 255) + ((d >> 16) & 255)) / 2,
             (((b >> 8) & 255) + ((d >> 8) & 255)) / 2, ((b & 255) + (d & 255)) / 2);
    pen_halfshadow = alloc(hs);
    if (xft_ready) {
        XftColorFree(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), &xft_black);
        XftColorFree(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), &xft_white);
    }
    XftColorAllocName(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), ("#" + sd).c_str(), &xft_black);
    XftColorAllocName(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), ("#" + sh).c_str(), &xft_white);
    xft_ready = true;
}

// The GUI settings changed the colours (Rambler sets _RAMBLER_PALETTE on the root window):
// the pens again, every frame and screen bar drawn again
void palette_changed() {
    std::string v = utf8_prop(root, RAMBLER_PALETTE);
    char a[8] = {}, b[8] = {}, c[8] = {}, d[8] = {};
    if (sscanf(v.c_str(), "%6s %6s %6s %6s", a, b, c, d) != 4) return;
    apply_palette(a, b, c, d);   // background, shadow, shine, fill
    for (auto *s : screens) {
        XSetWindowBackground(dpy, s->win, pen_grey);
        XClearWindow(dpy, s->win);
        s->draw_bar();
        for (auto *cl : s->clients) {
            XSetWindowBackground(dpy, cl->frame, pen_grey);
            draw_frame(cl);
        }
    }
}

// The Font preferences changed the screen font (Lune sets _RAMBLER_FONT on the root window:
// "family TAB size"): the bars' and menus' text in it, as large as the bars allow
// ---- THEME: --------------------------------------------------------------------------------
// the theme in use: theme.var (ENV:, else ENVARC:) holds its drawer, "THEMES:Name" or a path;
// none set: THEMES:Ice, the system's default; "none": the default look without a theme
std::string theme_drawer() {
    const char *x = getenv("XDG_CONFIG_HOME"), *rt = getenv("XDG_RUNTIME_DIR"), *home = getenv("HOME");
    std::string base = x && *x ? x : std::string(home ? home : "/tmp") + "/.config";
    std::string env = rt && *rt ? std::string(rt) + "/rambler/theme.var" : "/tmp/rambler-" + std::to_string(getuid()) + "/theme.var";
    std::string v = "THEMES:Ice";
    for (const std::string &f : {env, base + "/rambler/theme.var"}) {
        if (FILE *fp = fopen(f.c_str(), "r")) {
            char buf[1024] = "";
            if (fgets(buf, sizeof buf, fp)) v = buf;
            fclose(fp);
            break;
        }
    }
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
    if (v.empty() || !strcasecmp(v.c_str(), "none")) return "";
    if (strncasecmp(v.c_str(), "THEMES:", 7)) return v;
    // THEMES: the user's themes, then the installation's
    std::string name = v.substr(7);
    std::vector<std::string> dirs;
    const char *xd = getenv("XDG_DATA_HOME");
    dirs.push_back(xd && *xd ? std::string(xd) + "/rambler/themes" : std::string(home ? home : "/tmp") + "/.local/share/rambler/themes");
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        std::string d(exe);
        d = d.substr(0, d.rfind('/'));
        dirs.push_back(d + "/themes");
        dirs.push_back(d + "/../share/rambler/themes");
    }
    dirs.push_back("/usr/share/rambler/themes");
    dirs.push_back("/usr/local/share/rambler/themes");
    for (auto &d : dirs) {
        std::string p = decor::find_path(d, name);
        if (!p.empty()) return p;
    }
    return "";
}

// the frames' and the bars' sizes: the theme's (they grow with the font, as the original's do)
void theme_geometry() {
    if (!theme) return;
    decor::WindowFrame f = decor::window_frame(*theme, font_height(), 400, 300, true, true, true, true);
    TITLE_H = f.border_top;
    BORDER_L = f.border_left;
    BORDER_R = f.border_right;
    BORDER_B = f.border_bottom;
    SIZE_W = f.size.w;
    SIZE_H = f.size.h;
    theme_bar = decor::screen_bar(*theme, font_height(), SW);
    BAR_H = theme_bar.bar_height + 1;
    GADGET_W = SW - theme_bar.depth.x;
}

void load_theme() {
    std::string d = theme_drawer();
    theme = d.empty() ? nullptr : decor::load_theme(d);
    if (!d.empty() && !theme) fprintf(stderr, "rambler-wm: no decoration theme in %s\n", d.c_str());
    theme_geometry();
}

void font_changed() {
    static std::string had;
    std::string v = utf8_prop(root, RAMBLER_FONT);
    if (v.empty() || v == had) return;
    auto tab = v.find('\t');
    if (tab == std::string::npos) return;
    std::string family = v.substr(0, tab);
    // (the default look's bars are of a fixed height; a theme's grow with the font)
    int size = std::clamp(atoi(v.c_str() + tab + 1), 8, theme ? 64 : std::min(BAR_H, TITLE_H) - 5);
    for (char &ch : family) if (ch == ':' || ch == '-') ch = ' ';   // (no fontconfig syntax in a name)
    XftFont *f = XftFontOpenName(dpy, scr, (family + ":pixelsize=" + std::to_string(size)).c_str());
    if (!f) return;
    had = v;
    if (font) XftFontClose(dpy, font);
    font = f;
    theme_geometry();
    for (auto *s : screens) {
        s->draw_bar();
        for (auto *cl : s->clients) draw_frame(cl);
    }
}

}  // namespace

int main(int argc, char **argv) {
    dpy = XOpenDisplay(nullptr);
    if (!dpy) { fprintf(stderr, "rambler-wm: cannot open the display\n"); return 1; }
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    SW = DisplayWidth(dpy, scr);
    SH = DisplayHeight(dpy, scr);
    XSetErrorHandler(on_error);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | KeyPressMask | PropertyChangeMask);
    XSync(dpy, False);

    WM_PROTOCOLS = atom("WM_PROTOCOLS");
    WM_DELETE_WINDOW = atom("WM_DELETE_WINDOW");
    WM_TAKE_FOCUS = atom("WM_TAKE_FOCUS");
    WM_STATE = atom("WM_STATE");
    NET_WM_NAME = atom("_NET_WM_NAME");
    UTF8_STRING = atom("UTF8_STRING");
    NET_WM_WINDOW_TYPE = atom("_NET_WM_WINDOW_TYPE");
    NET_WM_WINDOW_TYPE_DESKTOP = atom("_NET_WM_WINDOW_TYPE_DESKTOP");
    NET_WM_WINDOW_TYPE_DOCK = atom("_NET_WM_WINDOW_TYPE_DOCK");
    NET_SUPPORTED = atom("_NET_SUPPORTED");
    NET_ACTIVE_WINDOW = atom("_NET_ACTIVE_WINDOW");
    NET_CLIENT_LIST = atom("_NET_CLIENT_LIST");
    NET_SUPPORTING_WM_CHECK = atom("_NET_SUPPORTING_WM_CHECK");
    NET_WM_STATE = atom("_NET_WM_STATE");
    NET_WM_STATE_FULLSCREEN = atom("_NET_WM_STATE_FULLSCREEN");
    NET_WM_STATE_MAXIMIZED_VERT = atom("_NET_WM_STATE_MAXIMIZED_VERT");
    NET_WM_STATE_MAXIMIZED_HORZ = atom("_NET_WM_STATE_MAXIMIZED_HORZ");
    NET_CLOSE_WINDOW = atom("_NET_CLOSE_WINDOW");
    NET_FRAME_EXTENTS = atom("_NET_FRAME_EXTENTS");
    NET_WORKAREA = atom("_NET_WORKAREA");
    NET_NUMBER_OF_DESKTOPS = atom("_NET_NUMBER_OF_DESKTOPS");
    NET_CURRENT_DESKTOP = atom("_NET_CURRENT_DESKTOP");
    NET_DESKTOP_GEOMETRY = atom("_NET_DESKTOP_GEOMETRY");
    RAMBLER_MENU = atom("_RAMBLER_MENU");
    RAMBLER_SCREEN_TITLE = atom("_RAMBLER_SCREEN_TITLE");
    RAMBLER_PALETTE = atom("_RAMBLER_PALETTE");
    RAMBLER_FONT = atom("_RAMBLER_FONT");
    RAMBLER_ICONTROL = atom("_RAMBLER_ICONTROL");
    RAMBLER_SOUND = atom("_RAMBLER_SOUND");
    RAMBLER_COMMODITIES = atom("_RAMBLER_COMMODITIES");
    RAMBLER_MENU_PICK = atom("_RAMBLER_MENU_PICK");

    // the IControl preferences: the ones in use (the property, ENV:), else the saved ones
    {
        std::string v = utf8_prop(root, RAMBLER_ICONTROL);
        const char *x = getenv("XDG_CONFIG_HOME"), *rt = getenv("XDG_RUNTIME_DIR");
        std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
        std::string env = rt && *rt ? std::string(rt) + "/rambler/icontrol.prefs"
                                    : "/tmp/rambler-" + std::to_string(getuid()) + "/icontrol.prefs";
        for (const std::string &f : {env, base + "/rambler/icontrol.prefs"}) {
            if (!v.empty()) break;
            if (FILE *fp = fopen(f.c_str(), "r")) {
                char buf[1024];
                size_t n = fread(buf, 1, sizeof buf - 1, fp);
                fclose(fp);
                v.assign(buf, n);
            }
        }
        read_icontrol(v);
    }
    {   // the commodities: the property, else ENV:, else ENVARC:
        std::string v = utf8_prop(root, RAMBLER_COMMODITIES);
        for (const std::string &f : {rambler::beep_env_dir() + "/commodities.prefs",
                                     rambler::beep_saved_file().substr(0, rambler::beep_saved_file().size() - 11) + "commodities.prefs"}) {
            if (!v.empty()) break;
            if (FILE *fp = fopen(f.c_str(), "r")) {
                char buf[1024];
                size_t n = fread(buf, 1, sizeof buf - 1, fp);
                fclose(fp);
                v.assign(buf, n);
            }
        }
        read_commodities(v);
    }
    {   // the Sound preferences: the property, else ENV:, else ENVARC:
        std::string v = utf8_prop(root, RAMBLER_SOUND);
        beep_prefs = v.empty() ? rambler::load_beep_prefs() : rambler::BeepPrefs::parse(v);
    }

    // the palette: the GUI settings' (~/.config/rambler/lune.prefs), else the default
    std::string bg = "AAAAAA", sd = "000000", sh = "FFFFFF", fl = "6688BB";
    {
        const char *x = getenv("XDG_CONFIG_HOME");
        std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
        // the session's settings in use (ENV:) first, else the saved ones
        const char *rt = getenv("XDG_RUNTIME_DIR");
        std::string env = rt && *rt ? std::string(rt) + "/rambler/lune.prefs"
                                    : "/tmp/rambler-" + std::to_string(getuid()) + "/lune.prefs";
        FILE *f = fopen(env.c_str(), "r");
        if (!f) f = fopen((base + "/rambler/lune.prefs").c_str(), "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof line, f)) {
                std::string l(line);
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
                auto eq = l.find('=');
                if (eq == std::string::npos || l.size() - eq - 1 != 6) continue;
                std::string k = l.substr(0, eq), v = l.substr(eq + 1);
                if (k == "background") bg = v; else if (k == "shadow") sd = v;
                else if (k == "shine") sh = v; else if (k == "fill") fl = v;
            }
            fclose(f);
        }
    }
    apply_palette(bg, sd, sh, fl);
    font = XftFontOpenName(dpy, scr, "sans-10");
    load_theme();
    font_changed();   // (Rambler running already: its screen font)
    XDefineCursor(dpy, root, XCreateFontCursor(dpy, XC_left_ptr));

    // EWMH: who manages the windows, what is supported
    check_win = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, check_win, NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
    XChangeProperty(dpy, check_win, NET_WM_NAME, UTF8_STRING, 8, PropModeReplace, (const unsigned char *)"rambler-wm", 11);
    XChangeProperty(dpy, root, NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
    Atom supported[] = {NET_WM_NAME, NET_WM_WINDOW_TYPE, NET_WM_WINDOW_TYPE_DESKTOP, NET_ACTIVE_WINDOW, NET_CLIENT_LIST,
                        NET_SUPPORTING_WM_CHECK, NET_WM_STATE, NET_WM_STATE_FULLSCREEN, NET_WM_STATE_MAXIMIZED_VERT,
                        NET_WM_STATE_MAXIMIZED_HORZ, NET_CLOSE_WINDOW, NET_FRAME_EXTENTS, NET_WORKAREA,
                        NET_NUMBER_OF_DESKTOPS, NET_CURRENT_DESKTOP, NET_DESKTOP_GEOMETRY};
    XChangeProperty(dpy, root, NET_SUPPORTED, XA_ATOM, 32, PropModeReplace, (unsigned char *)supported,
                    sizeof supported / sizeof supported[0]);
    {
        // one desktop as large as the display; its work area below the screen's title bar
        long one = 1, zero = 0, geo[2] = {SW, SH}, area[4] = {0, BAR_H, SW, SH - BAR_H};
        XChangeProperty(dpy, root, NET_NUMBER_OF_DESKTOPS, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&one, 1);
        XChangeProperty(dpy, root, NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&zero, 1);
        XChangeProperty(dpy, root, NET_DESKTOP_GEOMETRY, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)geo, 2);
        XChangeProperty(dpy, root, NET_WORKAREA, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)area, 4);
    }

    // the screens: the number from the command line (--screens N), at least one
    int nscreens = 2;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--screens")) nscreens = std::clamp(atoi(argv[i + 1]), 1, 8);
    for (int i = nscreens - 1; i >= 0; i--)
        open_screen(i == 0 ? "Rambler Screen" : "Screen " + std::to_string(i + 1));
    main_screen = screens.back();
    restack_screens();
    tray_start();

    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_m), Mod4Mask, root, True, GrabModeAsync, GrabModeSync);
    grab_meta_drag();
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_n), Mod4Mask, root, True, GrabModeAsync, GrabModeSync);

    // the windows already there
    Window r, p, *kids;
    unsigned n;
    if (XQueryTree(dpy, root, &r, &p, &kids, &n)) {
        for (unsigned i = 0; i < n; i++) manage(kids[i], true);
        if (kids) XFree(kids);
    }

    // the bell: told by the keyboard extension; the sound itself is switched off (the flash is it)
    {
        int opcode, error, major = XkbMajorVersion, minor = XkbMinorVersion;
        if (XkbQueryExtension(dpy, &opcode, &xkb_event, &error, &major, &minor)) {
            XkbSelectEvents(dpy, XkbUseCoreKbd, XkbBellNotifyMask, XkbBellNotifyMask);
            unsigned int aud = XkbAudibleBellMask;
            XkbChangeEnabledControls(dpy, XkbUseCoreKbd, aud, 0);
        }
    }

    XEvent ev;
    for (;;) {
        XNextEvent(dpy, &ev);
        if (xkb_event >= 0 && ev.type == xkb_event && ((XkbAnyEvent *)&ev)->xkb_type == XkbBellNotify) {
            display_beep();
            continue;
        }
        switch (ev.type) {
        case MapRequest:
            if (is_tray_icon(ev.xmaprequest.window)) XMapRaised(dpy, ev.xmaprequest.window);
            else manage(ev.xmaprequest.window, false);
            break;
        case ConfigureRequest: {
            if (is_tray_icon(ev.xconfigurerequest.window)) { tray_layout(); break; }   // its place is ours
            auto &cr = ev.xconfigurerequest;
            if (by_window.count(cr.window)) {
                Client *c = by_window[cr.window];
                if (c->undecorated && !c->fullscreen && !c->backdrop) {
                    if (cr.value_mask & CWWidth) c->w = cr.width;
                    if (cr.value_mask & CWHeight) c->h = cr.height;
                    if (cr.value_mask & CWX) c->x = cr.x;
                    if (cr.value_mask & CWY) c->y = cr.y - c->screen->y;
                } else if (!c->bare()) {
                    if (cr.value_mask & CWWidth) c->w = cr.width + BORDER_L + BORDER_R;
                    if (cr.value_mask & CWHeight) c->h = cr.height + TITLE_H + BORDER_B;
                    if (cr.value_mask & CWX) c->x = cr.x - BORDER_L;
                    if (cr.value_mask & CWY) c->y = std::max(BAR_H, cr.y - TITLE_H - c->screen->y);
                }
                place(c);
            } else {
                XWindowChanges wc = {cr.x, cr.y, cr.width, cr.height, cr.border_width, cr.above, cr.detail};
                XConfigureWindow(dpy, cr.window, cr.value_mask, &wc);
            }
            break;
        }
        case ReparentNotify:
            // an icon taken away by its program (or by another tray)
            if (is_tray_icon(ev.xreparent.window) && main_screen && ev.xreparent.parent != main_screen->win)
                tray_remove(ev.xreparent.window);
            break;
        case UnmapNotify:
            if (by_window.count(ev.xunmap.window) && ev.xunmap.event != root) {
                Client *c = by_window[ev.xunmap.window];
                if (c->ignore_unmap > 0 && !ev.xunmap.send_event) { c->ignore_unmap--; break; }
                if (ev.xunmap.event == c->frame || ev.xunmap.send_event) unmanage(c, false);
            }
            break;
        case DestroyNotify:
            if (is_tray_icon(ev.xdestroywindow.window)) { tray_remove(ev.xdestroywindow.window); break; }
            if (by_window.count(ev.xdestroywindow.window)) unmanage(by_window[ev.xdestroywindow.window], true);
            break;
        case PropertyNotify:
            if (ev.xproperty.window == root && ev.xproperty.atom == RAMBLER_PALETTE) palette_changed();
            if (ev.xproperty.window == root && ev.xproperty.atom == RAMBLER_FONT) font_changed();
            if (ev.xproperty.window == root && ev.xproperty.atom == RAMBLER_COMMODITIES)
                read_commodities(utf8_prop(root, RAMBLER_COMMODITIES));
            if (ev.xproperty.window == root && ev.xproperty.atom == RAMBLER_SOUND)
                beep_prefs = rambler::BeepPrefs::parse(utf8_prop(root, RAMBLER_SOUND));
            if (ev.xproperty.window == root && ev.xproperty.atom == RAMBLER_ICONTROL) {
                read_icontrol(utf8_prop(root, RAMBLER_ICONTROL));
                grab_meta_drag();
            }
            else if (by_window.count(ev.xproperty.window)) {
                Client *c = by_window[ev.xproperty.window];
                if (ev.xproperty.atom == XA_WM_NAME || ev.xproperty.atom == NET_WM_NAME) {
                    c->title = window_title(c->win);
                    draw_frame(c);
                } else if (ev.xproperty.atom == RAMBLER_MENU && menu.open && menu.c == c) {
                    // the program changed its menus while they are open (an item picked with the
                    // left button): the ticks and the ghosting show at once, if the shape is the same
                    std::vector<MenuItem> fresh;
                    auto same_shape = [](const std::vector<MenuItem> &a, const std::vector<MenuItem> &b, auto &&self) -> bool {
                        if (a.size() != b.size()) return false;
                        for (size_t i = 0; i < a.size(); i++)
                            if (!self(a[i].children, b[i].children, self)) return false;
                        return true;
                    };
                    if (read_menu(c, fresh) && same_shape(fresh, menu.titles, same_shape)) {
                        menu.titles = fresh;
                        if (menu.box) draw_menu_box(menu.box, menu.titles[menu.title].children, menu.box_w, menu.box_h, menu.item);
                        if (menu.sub) draw_menu_box(menu.sub, menu.titles[menu.title].children[menu.sub_of].children, menu.sub_w, menu.sub_h, menu.sub_item);
                    }
                } else if (ev.xproperty.atom == RAMBLER_SCREEN_TITLE) {
                    c->screen_title = utf8_prop(c->win, RAMBLER_SCREEN_TITLE);
                    if (!menu.open) c->screen->draw_bar();
                } else if (ev.xproperty.atom == NET_WM_WINDOW_TYPE && !c->backdrop && !c->fullscreen &&
                           has_type(c->win, NET_WM_WINDOW_TYPE_DESKTOP)) {
                    make_backdrop(c);   // the type set after mapping
                } else if (ev.xproperty.atom == XA_WM_HINTS || ev.xproperty.atom == WM_PROTOCOLS ||
                           ev.xproperty.atom == XA_WM_NORMAL_HINTS)
                    read_protocols(c);
            }
            break;
        case ClientMessage:
            if (ev.xclient.message_type == NET_SYSTEM_TRAY_OPCODE && ev.xclient.data.l[1] == 0) {   // REQUEST_DOCK
                tray_dock((Window)ev.xclient.data.l[2]);
                break;
            }
            if (ev.xclient.message_type == NET_WM_STATE && by_window.count(ev.xclient.window)) {
                // data: action (0 remove, 1 add, 2 toggle), the first and the second state
                Client *c = by_window[ev.xclient.window];
                long action = ev.xclient.data.l[0];
                for (int i = 1; i <= 2; i++) {
                    Atom a = (Atom)ev.xclient.data.l[i];
                    if (a == NET_WM_STATE_FULLSCREEN)
                        set_fullscreen(c, action == 2 ? !c->fullscreen : action == 1);
                    else if ((a == NET_WM_STATE_MAXIMIZED_VERT || a == NET_WM_STATE_MAXIMIZED_HORZ) && i == 1 && !c->bare()) {
                        bool want = action == 2 ? !c->zoomed : action == 1;
                        if (want != c->zoomed) zoom(c);
                        publish_state(c);
                    }
                }
                break;
            }
            if (ev.xclient.message_type == NET_CLOSE_WINDOW && by_window.count(ev.xclient.window)) {
                close_window(by_window[ev.xclient.window]);
                break;
            }
            if (ev.xclient.message_type == NET_ACTIVE_WINDOW && by_window.count(ev.xclient.window)) {
                Client *c = by_window[ev.xclient.window];
                if (c->screen != front_screen()) screen_to_front(c->screen);
                to_front(c);
                activate(c);
            }
            break;
        case Expose:
            if (ev.xexpose.count == 0) {
                if (by_frame.count(ev.xexpose.window)) draw_frame(by_frame[ev.xexpose.window]);
                else if (WScreen *s = screen_of(ev.xexpose.window)) { if (menu.open && menu.s == s) draw_menu_bar(); else s->draw_bar(); }
                else if (menu.box && ev.xexpose.window == menu.box) draw_menu_box(menu.box, menu.titles[menu.title].children, menu.box_w, menu.box_h, menu.item);
                else if (menu.sub && ev.xexpose.window == menu.sub) draw_menu_box(menu.sub, menu.titles[menu.title].children[menu.sub_of].children, menu.sub_w, menu.sub_h, menu.sub_item);
            }
            break;
        case ButtonPress: button_press(ev.xbutton); break;
        case EnterNotify:
            // AutoPoint: the window the pointer comes into becomes the active one (not while
            // something is dragged or a menu is open; the backdrop stays as it is)
            if (cx.autopoint && ev.xcrossing.mode == NotifyNormal && ev.xcrossing.detail != NotifyInferior &&
                drag.kind == D_NONE && !menu.open && by_frame.count(ev.xcrossing.window)) {
                Client *c = by_frame[ev.xcrossing.window];
                if (!c->backdrop && c != active) activate(c);
            }
            break;
        case MotionNotify: if (drag.kind != D_NONE || menu.open) motion(ev.xmotion); break;
        case ButtonRelease: button_release(ev.xbutton); break;
        case KeyPress: key_press(ev.xkey); break;
        }
    }
}
