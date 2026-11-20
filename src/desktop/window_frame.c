/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the frames of the desktop's own windows (listers, the Assigns
 * editor, requesters) and their gadgets.
 *
 * The window is borderless; the theme's nirvana/frame group draws the title bar and the
 * border, and the gadgets are the theme's nirvana/frame/gadget/<name> groups, laid out
 * as the theme's data says ("nirvana/frame/gadgets/left": "close", ".../right": "prefs
 * jump iconify zoom depth"). Their actions:
 *
 *   close    the window's on_close (or the window goes)
 *   prefs    the window's settings (on_prefs; the gadget is left out without one)
 *   jump     the next of the desktop's windows to the front
 *   iconify  the window iconified
 *   zoom     between its size and the screen's (also a double click on the title)
 *   depth    to the back (X11), or the next window to the front (Wayland has no
 *            request to lower a window)
 *
 * Dragging the title moves the window, the corner gadget resizes it - both through the
 * display server (elm_win_move_resize_start), so they work on Wayland too.
 */
#include "desktop.h"

#include <Ecore_X.h>
#include <string.h>

static const char *const gadget_names[] = {"close", "prefs", "jump", "iconify", "zoom", "depth"};

static void set_active(Nv_Window *w, bool on)
{
    const char *sig = on ? "nirvana,state,active" : "nirvana,state,inactive";
    elm_layout_signal_emit(w->layout, sig, "nirvana");
    for (int side = 0; side < 2; side++) {
        Evas_Object *box = elm_layout_content_get(w->layout, side ? "nirvana.swallow.gadgets.right"
                                                                  : "nirvana.swallow.gadgets.left");
        if (!box)
            continue;
        Eina_List *kids = elm_box_children_get(box), *l;
        Evas_Object *g;
        EINA_LIST_FOREACH(kids, l, g)
            edje_object_signal_emit(g, sig, "nirvana");
        eina_list_free(kids);
    }
    if (on)
        nv.active = w;
    else if (nv.active == w)
        nv.active = NULL;
}

Nv_Window *nv_window_active(void)
{
    return nv.active;
}

/* ---------------------------------------------------------------- gadget actions */

static Nv_Window *next_window(Nv_Window *w)
{
    Eina_List *me = eina_list_data_find_list(nv.windows, w);
    Eina_List *n = me ? eina_list_next(me) : NULL;
    if (!n)
        n = nv.windows;
    Nv_Window *o = n ? eina_list_data_get(n) : NULL;
    return o == w ? NULL : o;
}

void nv_window_close(Nv_Window *w)
{
    if (w->on_close)
        w->on_close(w->data, w);
    else
        evas_object_del(w->win);
}

void nv_window_iconify(Nv_Window *w)
{
    elm_win_iconified_set(w->win, EINA_TRUE);
}

void nv_window_zoom(Nv_Window *w)
{
    w->zoomed = !w->zoomed;
    elm_win_maximized_set(w->win, w->zoomed);
}

void nv_window_depth(Nv_Window *w)
{
    if (nv.backend == NV_BACKEND_X11 && elm_win_xwindow_get(w->win)) {
        ecore_x_window_lower(elm_win_xwindow_get(w->win));
        ecore_x_window_lower(elm_win_xwindow_get(nv.backdrop)); /* (the backdrop stays below) */
        return;
    }
    Nv_Window *o = next_window(w);
    if (o)
        elm_win_activate(o->win);
}

void nv_window_jump(Nv_Window *w)
{
    Nv_Window *o = next_window(w);
    if (o) {
        elm_win_raise(o->win);
        elm_win_activate(o->win);
    }
}

static void on_signal(void *data, Evas_Object *o, const char *emission, const char *source)
{
    (void)o;
    (void)source;
    Nv_Window *w = data;
    if (!strcmp(emission, "nirvana,frame,move"))
        elm_win_move_resize_start(w->win, ELM_WIN_MOVE_RESIZE_MOVE);
    else if (!strcmp(emission, "nirvana,frame,resize"))
        elm_win_move_resize_start(w->win, ELM_WIN_MOVE_RESIZE_BOTTOM | ELM_WIN_MOVE_RESIZE_RIGHT);
    else if (!strcmp(emission, "nirvana,gadget,close"))
        nv_window_close(w);
    else if (!strcmp(emission, "nirvana,gadget,prefs")) {
        if (w->on_prefs)
            w->on_prefs(w->data, w);
    } else if (!strcmp(emission, "nirvana,gadget,jump"))
        nv_window_jump(w);
    else if (!strcmp(emission, "nirvana,gadget,iconify"))
        nv_window_iconify(w);
    else if (!strcmp(emission, "nirvana,gadget,zoom"))
        nv_window_zoom(w);
    else if (!strcmp(emission, "nirvana,gadget,depth"))
        nv_window_depth(w);
}

/* ---------------------------------------------------------------- the frame */

static Evas_Object *gadget_box(Nv_Window *w, const char *list)
{
    Evas_Object *box = elm_box_add(w->win);
    elm_box_horizontal_set(box, EINA_TRUE);
    elm_box_padding_set(box, 3, 0);
    char *copy = strdup(list ? list : ""), *save = NULL;
    for (char *n = strtok_r(copy, " ", &save); n; n = strtok_r(NULL, " ", &save)) {
        bool known = false;
        for (size_t i = 0; i < sizeof gadget_names / sizeof *gadget_names; i++)
            known |= !strcmp(n, gadget_names[i]);
        if (!known || (!strcmp(n, "prefs") && !w->on_prefs))
            continue;
        char group[64];
        snprintf(group, sizeof group, "nirvana/frame/gadget/%s", n);
        Evas_Object *g = nv_edje_add(w->win, group);
        if (!g)
            continue;
        edje_object_signal_callback_add(g, "nirvana,gadget,*", "nirvana", on_signal, w);
        evas_object_size_hint_min_set(g, 18, 18);
        elm_box_pack_end(box, g);
        evas_object_show(g);
    }
    free(copy);
    evas_object_size_hint_min_set(box, 0, 18);
    return box;
}

static void build_gadgets(Nv_Window *w)
{
    Evas_Object *old;
    if ((old = elm_layout_content_unset(w->layout, "nirvana.swallow.gadgets.left")))
        evas_object_del(old);
    if ((old = elm_layout_content_unset(w->layout, "nirvana.swallow.gadgets.right")))
        evas_object_del(old);
    const char *left = nv_theme_data("nirvana/frame/gadgets/left");
    const char *right = nv_theme_data("nirvana/frame/gadgets/right");
    Evas_Object *lb = gadget_box(w, left ? left : "close");
    Evas_Object *rb = gadget_box(w, right ? right : "prefs jump iconify zoom depth");
    elm_layout_content_set(w->layout, "nirvana.swallow.gadgets.left", lb);
    elm_layout_content_set(w->layout, "nirvana.swallow.gadgets.right", rb);
}

static void on_focus_in(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    set_active(data, true);
}

static void on_focus_out(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    set_active(data, false);
}

static void on_delete_request(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    nv_window_close(data);
}

static void on_del(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)e;
    (void)o;
    (void)event;
    Nv_Window *w = data;
    nv.windows = eina_list_remove(nv.windows, w);
    if (nv.active == w)
        nv.active = NULL;
    if (w->window_menu)
        nv_menu_free(w->window_menu);
    free(w->title);
    free(w);
}

Nv_Window *nv_window_new(const char *name, const char *title, int width, int height)
{
    Nv_Window *w = calloc(1, sizeof *w);
    w->win = elm_win_add(NULL, name, ELM_WIN_BASIC);
    elm_win_borderless_set(w->win, EINA_TRUE);
    elm_win_autodel_set(w->win, EINA_FALSE);
    w->title = strdup(title ? title : "");
    elm_win_title_set(w->win, w->title);
    w->layout = elm_layout_add(w->win);
    elm_layout_file_set(w->layout, nv.theme, "nirvana/frame");
    evas_object_size_hint_weight_set(w->layout, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    elm_win_resize_object_add(w->win, w->layout);
    evas_object_show(w->layout);
    w->frame = elm_layout_edje_get(w->layout);
    edje_object_part_text_set(w->frame, "nirvana.text.title", w->title);
    elm_layout_signal_callback_add(w->layout, "nirvana,frame,*", "nirvana", on_signal, w);
    elm_layout_signal_callback_add(w->layout, "nirvana,gadget,*", "nirvana", on_signal, w);
    evas_object_event_callback_add(w->win, EVAS_CALLBACK_DEL, on_del, w);
    evas_object_smart_callback_add(w->win, "focus,in", on_focus_in, w);
    evas_object_smart_callback_add(w->win, "focus,out", on_focus_out, w);
    evas_object_smart_callback_add(w->win, "delete,request", on_delete_request, w);
    evas_object_resize(w->win, width, height);
    w->normal_w = width;
    w->normal_h = height;
    nv.windows = eina_list_append(nv.windows, w);
    return w;
}

void nv_window_content_set(Nv_Window *w, Evas_Object *content)
{
    w->content = content;
    elm_layout_content_set(w->layout, "nirvana.swallow.content", content);
}

void nv_window_title_set(Nv_Window *w, const char *title)
{
    free(w->title);
    w->title = strdup(title ? title : "");
    elm_win_title_set(w->win, w->title);
    elm_layout_text_set(w->layout, "nirvana.text.title", w->title);
}

void nv_window_show(Nv_Window *w)
{
    build_gadgets(w);
    set_active(w, false);
    evas_object_show(w->win);
    elm_win_activate(w->win);
}
