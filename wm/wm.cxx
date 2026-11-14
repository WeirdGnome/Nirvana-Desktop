/*
    Port to FLTK/X11: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    wanderer-wm: the window manager of the Wanderer port - what Intuition does on AROS.

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
      Backdrop  a window of the type _NET_WM_WINDOW_TYPE_DESKTOP (Wanderer's root window as a
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
#include <X11/keysym.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

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
const int BAR_H = 20;          // the screen's title bar
const int TITLE_H = 20;        // a window's title bar
const int BORDER_L = 4, BORDER_R = 4, BORDER_B = 4;
const int GADGET_W = 22;       // close, zoom, depth
const int SIZE_W = 16, SIZE_H = 16;

Atom WM_PROTOCOLS, WM_DELETE_WINDOW, WM_STATE, WM_TAKE_FOCUS, NET_WM_NAME, UTF8_STRING, NET_WM_WINDOW_TYPE,
    NET_WM_WINDOW_TYPE_DESKTOP, NET_WM_WINDOW_TYPE_DOCK, NET_SUPPORTED, NET_ACTIVE_WINDOW, NET_CLIENT_LIST,
    NET_SUPPORTING_WM_CHECK, NET_WM_STATE, NET_WM_STATE_FULLSCREEN;

struct WScreen;

struct Client {
    Window win = 0, frame = 0;
    WScreen *screen = nullptr;
    std::string title;
    int x = 0, y = 0, w = 100, h = 100;       // the frame, on its screen
    int alt_x = 0, alt_y = 0, alt_w = 0, alt_h = 0;   // the other size (zoom)
    bool backdrop = false, active = false, zoomed = false;
    bool protocols_delete = false, takes_focus = false, accepts_input = true;
    int min_w = 40, min_h = 20;
    int pressed = 0;                          // a gadget held down
};

struct WScreen {
    Window win = 0;
    std::string title;
    int y = 0;                                // dragged down by
    std::vector<Client *> clients;            // bottom to top
    void draw_bar();
};

std::vector<WScreen *> screens;               // back to front
std::map<Window, Client *> by_window, by_frame;
Client *active = nullptr;
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

void draw_frame(Client *c) {
    if (c->backdrop) return;
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
    GC gc = gc_for(win);
    fill(win, gc, pen_white, 0, 0, SW, BAR_H - 1);
    fill(win, gc, pen_black, 0, BAR_H - 1, SW, 1);
    // the active window's screen title, else the screen's own
    std::string t = title;
    draw_text(win, 6, (BAR_H - 1 + font->ascent - font->descent) / 2, SW - GADGET_W - 12, t, &xft_black);
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
    if (XGetWMNormalHints(dpy, c->win, &sh, &supplied) && (sh.flags & PMinSize)) {
        c->min_w = std::max(40, sh.min_width + BORDER_L + BORDER_R);
        c->min_h = std::max(TITLE_H + BORDER_B + 10, sh.min_height + TITLE_H + BORDER_B);
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
    if (c->backdrop) { ce.x = c->x; ce.y = c->y + sy; ce.width = c->w; ce.height = c->h; }
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
    if (c->backdrop) XMoveResizeWindow(dpy, c->win, 0, 0, c->w, c->h);
    else XMoveResizeWindow(dpy, c->win, BORDER_L, TITLE_H, c->w - BORDER_L - BORDER_R, c->h - TITLE_H - BORDER_B);
    send_configure(c);
    draw_frame(c);
}

// the stacking within a screen: backdrops at the bottom, then the clients in order
void restack(WScreen *s) {
    std::vector<Window> order;   // top first (XRestackWindows)
    for (auto it = s->clients.rbegin(); it != s->clients.rend(); ++it) if (!(*it)->backdrop) order.push_back((*it)->frame);
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
}

WScreen *front_screen() { return screens.back(); }

void manage(Window w, bool existing) {
    if (by_window.count(w) || by_frame.count(w)) return;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, w, &wa) || wa.override_redirect) return;
    if (existing && wa.map_state != IsViewable) return;
    for (auto *s : screens) if (s->win == w) return;
    auto *c = new Client;
    c->win = w;
    c->screen = front_screen();
    c->title = window_title(w);
    c->backdrop = has_type(w, NET_WM_WINDOW_TYPE_DESKTOP);
    read_protocols(c);
    if (c->backdrop) {
        c->x = 0; c->y = BAR_H; c->w = SW; c->h = SH - BAR_H;
    } else {
        c->w = wa.width + BORDER_L + BORDER_R;
        c->h = wa.height + TITLE_H + BORDER_B;
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
        }
        c->x = std::clamp(c->x, -c->w + 40, SW - 40);
        c->y = std::clamp(c->y, BAR_H, SH - TITLE_H);
    }
    XSetWindowAttributes a = {};
    a.background_pixel = pen_grey;
    a.event_mask = SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
                   PointerMotionMask | ExposureMask | EnterWindowMask;
    c->frame = XCreateWindow(dpy, c->screen->win, c->x, c->y, c->w, c->h, 0, CopyFromParent, InputOutput,
                             CopyFromParent, CWBackPixel | CWEventMask, &a);
    XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask);
    XSetWindowBorderWidth(dpy, w, 0);
    XAddToSaveSet(dpy, w);
    XReparentWindow(dpy, w, c->frame, c->backdrop ? 0 : BORDER_L, c->backdrop ? 0 : TITLE_H);
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
    if (!c->backdrop) activate(c);
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
    if (c->backdrop) return G_NONE;
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
    s->y = std::clamp(y, 0, SH - 1);
    XMoveWindow(dpy, s->win, 0, s->y);
}

void screen_moved(WScreen *s) {
    for (auto *c : s->clients) send_configure(c);
}

// --------------------------------------------------------------------------------------------
// Dragging: a window (move / size) or a screen
// --------------------------------------------------------------------------------------------
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
    if (drag.kind != D_NONE) {
        if (e.button == Button3) cancel_drag();   // the right button cancels a drag
        return;
    }
    // a click inside a program's window: activate it, let the click through
    if (by_window.count(e.window)) {
        Client *c = by_window[e.window];
        if (c->screen != front_screen() && c->screen->y == 0) screen_to_front(c->screen);
        if (!c->backdrop) activate(c);
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
        } else if (s != front_screen()) screen_to_front(s);
        return;
    }
    if (!by_frame.count(e.window)) return;
    Client *c = by_frame[e.window];
    if (c->screen != front_screen()) screen_to_front(c->screen);
    activate(c);
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
    while (XCheckTypedEvent(dpy, MotionNotify, (XEvent *)&e)) {}
    int dx = e.x_root - drag.px, dy = e.y_root - drag.py;
    switch (drag.kind) {
    case D_SCREEN: move_screen(drag.s, drag.oy + dy); break;
    case D_MOVE:
        drag.c->x = drag.ox + dx;
        drag.c->y = std::max(BAR_H, drag.oy + dy);
        XMoveWindow(dpy, drag.c->frame, drag.c->x, drag.c->y);
        break;
    case D_SIZE:
        drag.c->w = std::max(drag.c->min_w, drag.ow + dx);
        drag.c->h = std::max(drag.c->min_h, drag.oh + dy);
        place(drag.c);
        break;
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

void key_press(XKeyEvent &e) {
    KeySym k = XLookupKeysym(&e, 0);
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
        fprintf(stderr, "wanderer-wm: another window manager is running\n");
        exit(1);
    }
    return 0;   // windows that went away meanwhile
}

Atom atom(const char *n) { return XInternAtom(dpy, n, False); }

}  // namespace

int main(int argc, char **argv) {
    dpy = XOpenDisplay(nullptr);
    if (!dpy) { fprintf(stderr, "wanderer-wm: cannot open the display\n"); return 1; }
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    SW = DisplayWidth(dpy, scr);
    SH = DisplayHeight(dpy, scr);
    XSetErrorHandler(on_error);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | KeyPressMask);
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

    pen_grey = alloc("#AAAAAA");
    pen_black = alloc("#000000");
    pen_white = alloc("#FFFFFF");
    pen_blue = alloc("#6688BB");
    pen_halfshadow = alloc("#555555");
    font = XftFontOpenName(dpy, scr, "sans-10");
    XftColorAllocName(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), "#000000", &xft_black);
    XftColorAllocName(dpy, DefaultVisual(dpy, scr), DefaultColormap(dpy, scr), "#FFFFFF", &xft_white);
    XDefineCursor(dpy, root, XCreateFontCursor(dpy, XC_left_ptr));

    // EWMH: who manages the windows, what is supported
    check_win = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, check_win, NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
    XChangeProperty(dpy, check_win, NET_WM_NAME, UTF8_STRING, 8, PropModeReplace, (const unsigned char *)"wanderer-wm", 11);
    XChangeProperty(dpy, root, NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
    Atom supported[] = {NET_WM_NAME, NET_WM_WINDOW_TYPE, NET_WM_WINDOW_TYPE_DESKTOP, NET_ACTIVE_WINDOW, NET_CLIENT_LIST,
                        NET_SUPPORTING_WM_CHECK};
    XChangeProperty(dpy, root, NET_SUPPORTED, XA_ATOM, 32, PropModeReplace, (unsigned char *)supported,
                    sizeof supported / sizeof supported[0]);

    // the screens: the number from the command line (--screens N), at least one
    int nscreens = 2;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--screens")) nscreens = std::clamp(atoi(argv[i + 1]), 1, 8);
    for (int i = nscreens - 1; i >= 0; i--)
        open_screen(i == 0 ? "Wanderer Screen" : "Screen " + std::to_string(i + 1));
    restack_screens();

    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_m), Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_n), Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);

    // the windows already there
    Window r, p, *kids;
    unsigned n;
    if (XQueryTree(dpy, root, &r, &p, &kids, &n)) {
        for (unsigned i = 0; i < n; i++) manage(kids[i], true);
        if (kids) XFree(kids);
    }

    XEvent ev;
    for (;;) {
        XNextEvent(dpy, &ev);
        switch (ev.type) {
        case MapRequest: manage(ev.xmaprequest.window, false); break;
        case ConfigureRequest: {
            auto &cr = ev.xconfigurerequest;
            if (by_window.count(cr.window)) {
                Client *c = by_window[cr.window];
                if (!c->backdrop) {
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
        case UnmapNotify:
            if (by_window.count(ev.xunmap.window) && ev.xunmap.event != root) {
                Client *c = by_window[ev.xunmap.window];
                if (ev.xunmap.event == c->frame || ev.xunmap.send_event) unmanage(c, false);
            }
            break;
        case DestroyNotify:
            if (by_window.count(ev.xdestroywindow.window)) unmanage(by_window[ev.xdestroywindow.window], true);
            break;
        case PropertyNotify:
            if (by_window.count(ev.xproperty.window)) {
                Client *c = by_window[ev.xproperty.window];
                if (ev.xproperty.atom == XA_WM_NAME || ev.xproperty.atom == NET_WM_NAME) {
                    c->title = window_title(c->win);
                    draw_frame(c);
                } else if (ev.xproperty.atom == XA_WM_HINTS || ev.xproperty.atom == WM_PROTOCOLS ||
                           ev.xproperty.atom == XA_WM_NORMAL_HINTS)
                    read_protocols(c);
            }
            break;
        case ClientMessage:
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
                else if (WScreen *s = screen_of(ev.xexpose.window)) s->draw_bar();
            }
            break;
        case ButtonPress: button_press(ev.xbutton); break;
        case MotionNotify: if (drag.kind != D_NONE) motion(ev.xmotion); break;
        case ButtonRelease: button_release(ev.xbutton); break;
        case KeyPress: key_press(ev.xkey); break;
        }
    }
}
