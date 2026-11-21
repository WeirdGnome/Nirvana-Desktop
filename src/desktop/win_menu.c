/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: a context menu inside one of the desktop's windows (a lister's), drawn
 * in that window's canvas with the theme's menu groups - as the screen bar draws its
 * menus - since a Wayland window does not know where it is on the screen.
 *
 * It opens where the right button was pressed and follows the pointer while the button
 * is held: letting go over an item chooses it, letting go elsewhere shuts the menu, and
 * a click without moving leaves it open to be clicked (as context menus have always
 * done). Escape shuts it. One such menu is open at a time.
 */
#include "desktop.h"
#include "menu_host.h"

#include <stdlib.h>

static struct {
    Evas_Object *win, *catcher, *pressed;
    Nv_Menu_Session *s;
    bool holding;
    int px, py;
    void (*done)(void *data);
    void *data;
} wm;

static Evas_Object *h_probe(void *data)
{
    (void)data;
    return wm.win;
}

static Evas_Object *h_panel_new(void *data, int x, int y, int w, int h, int *ox, int *oy)
{
    (void)data;
    (void)w;
    (void)h;
    *ox = x;
    *oy = y;
    return wm.win;
}

static void h_panel_del(void *data, Evas_Object *host)
{
    (void)data;
    (void)host;
}

static void h_bounds(void *data, int *x, int *y, int *w, int *h)
{
    (void)data;
    *x = 0;
    *y = 0;
    evas_object_geometry_get(wm.win, NULL, NULL, w, h);
}

static const Nv_Menu_Host win_host = {h_probe, h_panel_new, h_panel_del, h_bounds};

static void pressed_move(void *data, Evas *e, Evas_Object *o, void *event);
static void pressed_up(void *data, Evas *e, Evas_Object *o, void *event);

static void hold_over(void)
{
    if (!wm.holding)
        return;
    wm.holding = false;
    if (wm.pressed) {
        evas_object_event_callback_del(wm.pressed, EVAS_CALLBACK_MOUSE_MOVE, pressed_move);
        evas_object_event_callback_del(wm.pressed, EVAS_CALLBACK_MOUSE_UP, pressed_up);
    }
}

static void session_done(void *data)
{
    (void)data;
    hold_over();
    wm.s = NULL;
    if (wm.catcher) {
        evas_object_del(wm.catcher);
        wm.catcher = NULL;
    }
    void (*done)(void *) = wm.done;
    wm.done = NULL;
    if (done)
        done(wm.data);
}

void nv_win_context_close(void)
{
    if (wm.s)
        nv_menu_session_close(wm.s);
}

/* ---------------------------------------------------------------- while held */

static void pressed_move(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)data;
    (void)e;
    (void)o;
    Evas_Event_Mouse_Move *ev = event;
    if (wm.s)
        nv_menu_session_motion(wm.s, ev->cur.canvas.x, ev->cur.canvas.y);
}

static void pressed_up(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)data;
    (void)e;
    (void)o;
    Evas_Event_Mouse_Up *ev = event;
    if (ev->button != 3)
        return;
    int x = ev->canvas.x, y = ev->canvas.y;
    hold_over();
    if (!wm.s)
        return;
    bool closed = false;
    if (nv_menu_session_press(wm.s, x, y))
        nv_menu_session_release(wm.s, x, y, &closed);
    else if (abs(x - wm.px) > 4 || abs(y - wm.py) > 4)
        nv_win_context_close();
}

/* ---------------------------------------------------------------- once let go */

static void catcher_move(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)data;
    (void)e;
    (void)o;
    Evas_Event_Mouse_Move *ev = event;
    if (wm.s && !wm.holding)
        nv_menu_session_motion(wm.s, ev->cur.canvas.x, ev->cur.canvas.y);
}

static void catcher_down(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)data;
    (void)e;
    (void)o;
    Evas_Event_Mouse_Down *ev = event;
    if (wm.s && !wm.holding && !nv_menu_session_press(wm.s, ev->canvas.x, ev->canvas.y))
        nv_win_context_close();
}

static void catcher_up(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)data;
    (void)e;
    (void)o;
    Evas_Event_Mouse_Up *ev = event;
    bool closed;
    if (wm.s && !wm.holding)
        nv_menu_session_release(wm.s, ev->canvas.x, ev->canvas.y, &closed);
}

static void catcher_key(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)data;
    (void)e;
    (void)o;
    Evas_Event_Key_Down *ev = event;
    if (wm.s && nv_menu_session_key(wm.s, ev->key, ev->string) != NV_MENU_KEY_HANDLED)
        nv_win_context_close();
}

void nv_win_context(Evas_Object *win, Evas_Object *pressed, Nv_Menu *m, int x, int y, void (*done)(void *data),
                    void *data)
{
    nv_win_context_close();
    wm.win = win;
    wm.pressed = pressed;
    wm.done = done;
    wm.data = data;
    wm.px = x;
    wm.py = y;
    /* a cover over the window: the pointer and the keys go to the menu while it is open */
    Evas *e = evas_object_evas_get(win);
    wm.catcher = evas_object_rectangle_add(e);
    evas_object_color_set(wm.catcher, 0, 0, 0, 0);
    int w, h;
    evas_object_geometry_get(win, NULL, NULL, &w, &h);
    evas_object_move(wm.catcher, 0, 0);
    evas_object_resize(wm.catcher, w, h);
    evas_object_layer_set(wm.catcher, EVAS_LAYER_MAX - 10);
    evas_object_repeat_events_set(wm.catcher, EINA_FALSE);
    evas_object_event_callback_add(wm.catcher, EVAS_CALLBACK_MOUSE_MOVE, catcher_move, NULL);
    evas_object_event_callback_add(wm.catcher, EVAS_CALLBACK_MOUSE_DOWN, catcher_down, NULL);
    evas_object_event_callback_add(wm.catcher, EVAS_CALLBACK_MOUSE_UP, catcher_up, NULL);
    evas_object_event_callback_add(wm.catcher, EVAS_CALLBACK_KEY_DOWN, catcher_key, NULL);
    evas_object_show(wm.catcher);
    evas_object_focus_set(wm.catcher, EINA_TRUE);
    wm.s = nv_menu_session_open(&win_host, NULL, m, x, y, session_done, NULL);
    if (!wm.s) {
        session_done(NULL);
        return;
    }
    /* the panels above the cover */
    nv_menu_session_raise(wm.s, EVAS_LAYER_MAX - 5);
    if (pressed) {
        wm.holding = true;
        evas_object_event_callback_add(pressed, EVAS_CALLBACK_MOUSE_MOVE, pressed_move, NULL);
        evas_object_event_callback_add(pressed, EVAS_CALLBACK_MOUSE_UP, pressed_up, NULL);
    }
}
