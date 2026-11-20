/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the bar and the backdrop as layers of the screen.
 *
 * Wayland: the window's wl_surface is given the role of a wlr-layer-shell surface before
 * it is first shown; Ecore_Wl2 is told the window has no shell role of its own
 * (ECORE_WL2_WINDOW_TYPE_NONE), so it only draws into the surface. The first configure
 * is waited for (a round trip) before the first buffer is attached, as the protocol
 * asks.
 *
 * X11: the bar is a dock window with a strut, the backdrop a desktop window, both as big
 * as the screen asks.
 */
#include "desktop.h"

#include <Ecore_Wl2.h>
#include <Ecore_X.h>
#include <string.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

typedef struct Layer {
    Evas_Object *win;
    struct zwlr_layer_surface_v1 *ls;
    Nv_Layer_Configure_Cb cb;
    void *data;
    bool configured;
    int x, y, w, h;
    unsigned anchors;
} Layer;

static struct zwlr_layer_shell_v1 *shell;
static unsigned shell_version;
static Ecore_Wl2_Display *wl_disp;
static Eina_List *layers;

static Layer *layer_of(Evas_Object *win)
{
    Eina_List *l;
    Layer *ly;
    EINA_LIST_FOREACH(layers, l, ly)
        if (ly->win == win)
            return ly;
    return NULL;
}

/* ---------------------------------------------------------------- Wayland */

static void ls_configure(void *data, struct zwlr_layer_surface_v1 *s, uint32_t serial, uint32_t w, uint32_t h)
{
    Layer *ly = data;
    if (getenv("NIRVANA_DEBUG"))
        fprintf(stderr, "layer %p configure %u x %u\n", (void *)ly->win, w, h);
    zwlr_layer_surface_v1_ack_configure(s, serial);
    ly->configured = true;
    if ((int)w != ly->w || (int)h != ly->h || !ly->w) {
        ly->w = (int)w;
        ly->h = (int)h;
        evas_object_resize(ly->win, ly->w, ly->h);
        if (ly->cb)
            ly->cb(ly->data, ly->win, ly->w, ly->h);
    }
}

static void ls_closed(void *data, struct zwlr_layer_surface_v1 *s)
{
    (void)data;
    (void)s;
    /* the output went away: the desktop ends (its session restarts it) */
    elm_exit();
}

static const struct zwlr_layer_surface_v1_listener ls_listener = {ls_configure, ls_closed};

static bool bind_shell(void)
{
    if (shell)
        return true;
    wl_disp = ecore_wl2_connected_display_get(NULL);
    if (!wl_disp)
        return false;
    Eina_Iterator *it = ecore_wl2_display_globals_get(wl_disp);
    Ecore_Wl2_Global *g;
    unsigned id = 0, version = 0;
    EINA_ITERATOR_FOREACH(it, g) {
        if (!strcmp(g->interface, zwlr_layer_shell_v1_interface.name)) {
            id = g->id;
            version = g->version;
        }
    }
    eina_iterator_free(it);
    if (!id)
        return false;
    shell_version = version < 4 ? version : 4;
    if (getenv("NIRVANA_DEBUG"))
        fprintf(stderr, "layer shell version %u\n", version);
    shell = wl_registry_bind(ecore_wl2_display_registry_get(wl_disp), id, &zwlr_layer_shell_v1_interface,
                             shell_version);
    return shell != NULL;
}

static uint32_t wl_layer(Nv_Layer l)
{
    switch (l) {
    case NV_LAYER_BACKGROUND: return ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    case NV_LAYER_BOTTOM: return ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
    case NV_LAYER_TOP: return ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    default: return ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    }
}

static uint32_t wl_anchors(unsigned a)
{
    uint32_t r = 0;
    if (a & NV_ANCHOR_TOP)
        r |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    if (a & NV_ANCHOR_BOTTOM)
        r |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    if (a & NV_ANCHOR_LEFT)
        r |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    if (a & NV_ANCHOR_RIGHT)
        r |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    return r;
}

static bool setup_wayland(Layer *ly, Ecore_Wl2_Window *ww, Nv_Layer layer, int w, int h, int exclusive,
                          bool keyboard)
{
    if (!bind_shell()) {
        fprintf(stderr, "nirvana-desktop: the compositor has no layer shell (zwlr_layer_shell_v1)\n");
        return false;
    }
    ecore_wl2_window_type_set(ww, ECORE_WL2_WINDOW_TYPE_NONE);
    struct wl_surface *surf = ecore_wl2_window_surface_get(ww);
    if (!surf)
        return false;
    ly->ls = zwlr_layer_shell_v1_get_layer_surface(shell, surf, NULL, wl_layer(layer), "nirvana");
    zwlr_layer_surface_v1_add_listener(ly->ls, &ls_listener, ly);
    zwlr_layer_surface_v1_set_size(ly->ls, (uint32_t)w, (uint32_t)h);
    zwlr_layer_surface_v1_set_anchor(ly->ls, wl_anchors(ly->anchors));
    zwlr_layer_surface_v1_set_exclusive_zone(ly->ls, exclusive);
    if (keyboard)
        zwlr_layer_surface_v1_set_keyboard_interactivity(ly->ls, shell_version >= 4 ? 2 /* on demand */ : 1);
    wl_surface_commit(surf);
    struct wl_display *d = ecore_wl2_display_get(wl_disp);
    for (int i = 0; i < 20 && !ly->configured; i++)
        if (wl_display_roundtrip(d) < 0)
            break;
    return ly->configured;
}

/* ---------------------------------------------------------------- X11 */

/* a window manager may place a new window where it likes; once mapped, the window is
 * asked to its place again (and keeps it: docks and desktops are not moved by users) */
static void x11_place(void *data)
{
    Layer *ly = data;
    Ecore_X_Window xw = elm_win_xwindow_get(ly->win);
    if (xw)
        ecore_x_window_move_resize(xw, ly->x, ly->y, ly->w, ly->h);
}

static Eina_Bool x11_place_later(void *data)
{
    x11_place(data);
    return ECORE_CALLBACK_CANCEL;
}

static void x11_shown(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)e;
    (void)o;
    (void)event;
    ecore_job_add(x11_place, data);
    ecore_timer_add(0.3, x11_place_later, data); /* (some managers move it after mapping) */
}

static bool setup_x11(Layer *ly, Nv_Layer layer, int w, int h, int exclusive)
{
    Ecore_X_Window xw = elm_win_xwindow_get(ly->win);
    if (!xw)
        return false;
    int sw = 0, sh = 0;
    ecore_x_screen_size_get(ecore_x_default_screen_get(), &sw, &sh);
    int x = 0, y = 0;
    if ((ly->anchors & (NV_ANCHOR_LEFT | NV_ANCHOR_RIGHT)) == (NV_ANCHOR_LEFT | NV_ANCHOR_RIGHT) || !w)
        w = sw;
    if ((ly->anchors & (NV_ANCHOR_TOP | NV_ANCHOR_BOTTOM)) == (NV_ANCHOR_TOP | NV_ANCHOR_BOTTOM) || !h)
        h = sh;
    if ((ly->anchors & NV_ANCHOR_BOTTOM) && !(ly->anchors & NV_ANCHOR_TOP))
        y = sh - h;
    if ((ly->anchors & NV_ANCHOR_RIGHT) && !(ly->anchors & NV_ANCHOR_LEFT))
        x = sw - w;
    evas_object_move(ly->win, x, y);
    evas_object_resize(ly->win, w, h);
    if (exclusive > 0 && (ly->anchors & NV_ANCHOR_TOP) && !(ly->anchors & NV_ANCHOR_BOTTOM))
        ecore_x_netwm_strut_partial_set(xw, 0, 0, exclusive, 0, 0, 0, 0, 0, x, x + w - 1, 0, 0);
    if (layer == NV_LAYER_BACKGROUND || layer == NV_LAYER_BOTTOM)
        ecore_x_window_lower(xw);
    else
        ecore_x_window_raise(xw);
    ly->configured = true;
    ly->x = x;
    ly->y = y;
    ly->w = w;
    ly->h = h;
    evas_object_event_callback_add(ly->win, EVAS_CALLBACK_SHOW, x11_shown, ly);
    if (ly->cb)
        ly->cb(ly->data, ly->win, w, h);
    return true;
}

/* ---------------------------------------------------------------- the interface */

bool nv_layer_init(void)
{
    return true; /* the backend is known from the first window (nv_layer_setup) */
}

bool nv_layer_setup(Evas_Object *win, Nv_Layer layer, unsigned anchors, int w, int h, int exclusive,
                    bool keyboard, Nv_Layer_Configure_Cb cb, void *data)
{
    Layer *ly = calloc(1, sizeof *ly);
    ly->win = win;
    ly->cb = cb;
    ly->data = data;
    ly->anchors = anchors;
    layers = eina_list_append(layers, ly);
    Ecore_Wl2_Window *ww = elm_win_wl_window_get(win);
    if (ww) {
        nv.backend = NV_BACKEND_WAYLAND;
        return setup_wayland(ly, ww, layer, w, h, exclusive, keyboard);
    }
    nv.backend = NV_BACKEND_X11;
    return setup_x11(ly, layer, w, h, exclusive);
}

void nv_layer_resize(Evas_Object *win, int w, int h)
{
    Layer *ly = layer_of(win);
    if (!ly)
        return;
    if (ly->ls) {
        zwlr_layer_surface_v1_set_size(ly->ls, (uint32_t)w, (uint32_t)h);
        Ecore_Wl2_Window *ww = elm_win_wl_window_get(win);
        if (ww)
            wl_surface_commit(ecore_wl2_window_surface_get(ww));
        wl_display_flush(ecore_wl2_display_get(wl_disp));
    } else {
        evas_object_resize(win, w ? w : ly->w, h);
        ly->h = h;
        x11_place(ly);
        if (ly->cb)
            ly->cb(ly->data, win, w ? w : ly->w, h);
    }
}

void nv_layer_keyboard(Evas_Object *win, bool on)
{
    nv_layer_keyboard_mode(win, on ? 1 : 0);
}

void nv_layer_keyboard_mode(Evas_Object *win, int mode)
{
    Layer *ly = layer_of(win);
    if (!ly || !ly->ls)
        return;
    if (mode == 2 && shell_version < 4)
        mode = 0;
    zwlr_layer_surface_v1_set_keyboard_interactivity(ly->ls, (uint32_t)mode);
    Ecore_Wl2_Window *ww = elm_win_wl_window_get(win);
    if (ww)
        wl_surface_commit(ecore_wl2_window_surface_get(ww));
}

Evas_Object *nv_layer_win_add(const char *name, Nv_Layer layer)
{
    Elm_Win_Type t = layer == NV_LAYER_BACKGROUND || layer == NV_LAYER_BOTTOM ? ELM_WIN_DESKTOP : ELM_WIN_DOCK;
    Evas_Object *win = elm_win_add(NULL, name, t);
    elm_win_borderless_set(win, EINA_TRUE);
    elm_win_title_set(win, NV_SCREEN_TITLE);
    return win;
}
