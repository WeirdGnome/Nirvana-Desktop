/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the backdrop - the screen's background with the volume icons of the
 * assigns that have DESKTOP=1 (SYS:, Home:, RAM: by default).
 *
 * Icons are the theme's two-state nirvana/icon/volume[/kind] groups. A click selects
 * (Shift adds), a double click opens, a drag moves (the selection together), a drag on
 * the background draws a rubber band. Places are kept with Snapshot in
 * ~/.nirvana/Prefs/backdrop.icons ("Name x y"); Clean Up puts the icons in a column at
 * the right edge again. The icons are made again when the assigns change (SIGUSR1).
 */
#include "desktop.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

typedef struct Icon {
    char name[NIRVANA_NAME_MAX];
    Evas_Object *obj;
    int x, y;          /* its place on the backdrop */
    bool selected;
    bool placed;       /* from a snapshot */
} Icon;

static struct {
    Evas_Object *win, *edje, *band;
    Eina_List *icons;
    /* a drag going on */
    Icon *press_icon;
    int press_x, press_y;
    bool dragging, banding;
    Eina_Hash *places; /* name -> "x y" */
} bd;

#define ICON_W 88
#define ICON_H 92

/* ---------------------------------------------------------------- places */

static void places_path(char *out, size_t len)
{
    snprintf(out, len, "%s/.nirvana/Prefs/backdrop.icons", getenv("HOME") ? getenv("HOME") : "");
}

static void places_load(void)
{
    if (bd.places)
        eina_hash_free(bd.places);
    bd.places = eina_hash_string_superfast_new(free);
    char path[PATH_MAX];
    places_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[256], name[NIRVANA_NAME_MAX];
    int x, y;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%63s %d %d", name, &x, &y) == 3) {
            int *xy = malloc(2 * sizeof(int));
            xy[0] = x;
            xy[1] = y;
            eina_hash_set(bd.places, name, xy);
        }
    fclose(f);
}

void nv_backdrop_snapshot(void)
{
    char path[PATH_MAX], dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/.nirvana/Prefs", getenv("HOME") ? getenv("HOME") : "");
    mkdir(dir, 0755);
    places_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) {
        nv_message(_("Snapshot"), strerror(errno));
        return;
    }
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic) {
        fprintf(f, "%s %d %d\n", ic->name, ic->x, ic->y);
        ic->placed = true;
    }
    fclose(f);
    places_load();
}

/* ---------------------------------------------------------------- icons */

static void icon_move(Icon *ic, int x, int y)
{
    if (x < 0)
        x = 0;
    if (y < nv.bar_h)
        y = nv.bar_h;
    if (x > nv.screen_w - ICON_W)
        x = nv.screen_w - ICON_W;
    if (y > nv.screen_h - ICON_H)
        y = nv.screen_h - ICON_H;
    ic->x = x;
    ic->y = y;
    evas_object_move(ic->obj, x, y);
}

static void icon_select(Icon *ic, bool on)
{
    ic->selected = on;
    edje_object_signal_emit(ic->obj, on ? "nirvana,state,selected" : "nirvana,state,unselected", "nirvana");
}

void nv_backdrop_select_all(bool on)
{
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic)
        icon_select(ic, on);
}

/* the icons without a place go in a column at the right edge, from the top */
static void layout(bool all)
{
    int col = 0, row = 0;
    int rows = (nv.screen_h - nv.bar_h - 16) / (ICON_H + 8);
    if (rows < 1)
        rows = 1;
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic) {
        if (ic->placed && !all)
            continue;
        icon_move(ic, nv.screen_w - (col + 1) * (ICON_W + 8) - 8, nv.bar_h + 12 + row * (ICON_H + 8));
        if (++row >= rows) {
            row = 0;
            col++;
        }
    }
}

void nv_backdrop_clean_up(void)
{
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic)
        ic->placed = false;
    layout(true);
}

static void open_icon(Icon *ic)
{
    char path[NIRVANA_NAME_MAX + 2];
    snprintf(path, sizeof path, "%s:", ic->name);
    nv_lister_open(path);
}

void nv_backdrop_open_selected(void)
{
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic)
        if (ic->selected)
            open_icon(ic);
}

Eina_List *nv_backdrop_selected(void)
{
    Eina_List *out = NULL, *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic)
        if (ic->selected)
            out = eina_list_append(out, ic->name);
    return out;
}

static void take_focus(void);
static void canvas_move(void *data, Evas *e, Evas_Object *o, void *event);
static void canvas_up(void *data, Evas *e, Evas_Object *o, void *event);

static void icon_down(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)e;
    (void)o;
    Icon *ic = data;
    Evas_Event_Mouse_Down *ev = event;
    take_focus();
    if (ev->button != 1)
        return;
    if (ev->flags & EVAS_BUTTON_DOUBLE_CLICK) {
        open_icon(ic);
        return;
    }
    bool shift = evas_key_modifier_is_set(ev->modifiers, "Shift");
    if (!ic->selected && !shift)
        nv_backdrop_select_all(false);
    icon_select(ic, shift ? !ic->selected : true);
    evas_object_raise(ic->obj);
    bd.press_icon = ic;
    bd.press_x = ev->canvas.x;
    bd.press_y = ev->canvas.y;
    bd.dragging = false;
}

static const char *icon_group(const char *name)
{
    if (!strcasecmp(name, "SYS"))
        return "nirvana/icon/volume/system";
    if (!strcasecmp(name, "Home"))
        return "nirvana/icon/volume/home";
    if (!strcasecmp(name, "RAM"))
        return "nirvana/icon/volume/ram";
    return "nirvana/icon/volume";
}

void nv_backdrop_reload(void)
{
    Icon *ic;
    /* the selection kept by name */
    Eina_List *keep = NULL, *l;
    EINA_LIST_FREE(bd.icons, ic) {
        if (ic->selected)
            keep = eina_list_append(keep, strdup(ic->name));
        evas_object_del(ic->obj);
        free(ic);
    }
    bd.press_icon = NULL;
    places_load();
    nirvana_assign_list list;
    if (nirvana_assign_list_load(&list) < 0)
        return;
    for (size_t i = 0; i < list.count; i++) {
        if (!list.items[i].desktop)
            continue;
        ic = calloc(1, sizeof *ic);
        snprintf(ic->name, sizeof ic->name, "%s", list.items[i].name);
        ic->obj = nv_edje_add(bd.win, icon_group(ic->name));
        if (!ic->obj) {
            free(ic);
            continue;
        }
        edje_object_part_text_set(ic->obj, "nirvana.text.label", ic->name);
        evas_object_resize(ic->obj, ICON_W, ICON_H);
        evas_object_event_callback_add(ic->obj, EVAS_CALLBACK_MOUSE_DOWN, icon_down, ic);
        evas_object_event_callback_add(ic->obj, EVAS_CALLBACK_MOUSE_MOVE, canvas_move, NULL);
        evas_object_event_callback_add(ic->obj, EVAS_CALLBACK_MOUSE_UP, canvas_up, NULL);
        evas_object_show(ic->obj);
        int *xy = eina_hash_find(bd.places, ic->name);
        if (xy) {
            ic->placed = true;
            ic->x = xy[0];
            ic->y = xy[1];
        }
        bd.icons = eina_list_append(bd.icons, ic);
        char *k;
        EINA_LIST_FOREACH(keep, l, k)
            if (!strcmp(k, ic->name))
                icon_select(ic, true);
    }
    nirvana_assign_list_free(&list);
    char *k;
    EINA_LIST_FREE(keep, k)
        free(k);
    EINA_LIST_FOREACH(bd.icons, l, ic)
        if (ic->placed)
            icon_move(ic, ic->x, ic->y);
    layout(false);
}

/* ---------------------------------------------------------------- the background */

/* a click on the desktop gives it the keyboard (and so takes the focus from the
 * programs' windows, whose menus leave the bar): exclusive for a moment, then on demand
 * again so that a click on a window takes it back */
static Eina_Bool keyboard_on_demand(void *data)
{
    (void)data;
    nv_layer_keyboard_mode(bd.win, 2);
    return ECORE_CALLBACK_CANCEL;
}

static void take_focus(void)
{
    nv_global_menu_desktop_focus(true);
    if (nv.backend == NV_BACKEND_WAYLAND) {
        nv_layer_keyboard_mode(bd.win, 1);
        ecore_timer_add(0.2, keyboard_on_demand, NULL);
    }
}

static void bg_down(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)data;
    (void)e;
    (void)o;
    Evas_Event_Mouse_Down *ev = event;
    take_focus(); /* a click on the desktop: its menus */
    if (ev->button == 3) {
        nv_screenbar_menu_open(2); /* the Icons menu */
        return;
    }
    if (ev->button != 1)
        return;
    if (!evas_key_modifier_is_set(ev->modifiers, "Shift"))
        nv_backdrop_select_all(false);
    bd.press_icon = NULL;
    bd.press_x = ev->canvas.x;
    bd.press_y = ev->canvas.y;
    bd.banding = true;
    evas_object_move(bd.band, bd.press_x, bd.press_y);
    evas_object_resize(bd.band, 0, 0);
}

static void canvas_move(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)data;
    (void)e;
    (void)o;
    Evas_Event_Mouse_Move *ev = event;
    int x = ev->cur.canvas.x, y = ev->cur.canvas.y;
    if (bd.press_icon) {
        if (!bd.dragging && abs(x - bd.press_x) + abs(y - bd.press_y) < 5)
            return;
        bd.dragging = true;
        int dx = x - bd.press_x, dy = y - bd.press_y;
        bd.press_x = x;
        bd.press_y = y;
        Eina_List *l;
        Icon *ic;
        EINA_LIST_FOREACH(bd.icons, l, ic)
            if (ic->selected)
                icon_move(ic, ic->x + dx, ic->y + dy);
    } else if (bd.banding) {
        int x0 = x < bd.press_x ? x : bd.press_x, y0 = y < bd.press_y ? y : bd.press_y;
        int w = abs(x - bd.press_x), h = abs(y - bd.press_y);
        evas_object_move(bd.band, x0, y0);
        evas_object_resize(bd.band, w, h);
        evas_object_show(bd.band);
        evas_object_raise(bd.band);
        Eina_List *l;
        Icon *ic;
        EINA_LIST_FOREACH(bd.icons, l, ic) {
            bool in = ic->x < x0 + w && ic->x + ICON_W > x0 && ic->y < y0 + h && ic->y + ICON_H > y0;
            if (in != ic->selected)
                icon_select(ic, in);
        }
    }
}

static void canvas_up(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)data;
    (void)e;
    (void)o;
    (void)event;
    if (bd.dragging) {
        Eina_List *l;
        Icon *ic;
        EINA_LIST_FOREACH(bd.icons, l, ic)
            if (ic->selected)
                ic->placed = true;
    }
    bd.press_icon = NULL;
    bd.dragging = false;
    bd.banding = false;
    evas_object_hide(bd.band);
}

static void on_key(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)data;
    (void)e;
    (void)o;
    Evas_Event_Key_Down *ev = event;
    if (!strcmp(ev->key, "Return") || !strcmp(ev->key, "KP_Enter"))
        nv_backdrop_open_selected();
    else if (!strcmp(ev->key, "Escape"))
        nv_backdrop_select_all(false);
}

static void configured(void *data, Evas_Object *win, int w, int h)
{
    (void)data;
    (void)win;
    nv.screen_w = w;
    nv.screen_h = h;
    evas_object_resize(bd.edje, w, h);
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic)
        icon_move(ic, ic->x, ic->y);
    layout(false);
}

static void on_focus(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    if (getenv("NIRVANA_DEBUG"))
        fprintf(stderr, "backdrop focus %d\n", data != NULL);
    nv_global_menu_desktop_focus(data != NULL);
}

bool nv_backdrop_create(void)
{
    bd.win = nv_layer_win_add("nirvana-backdrop", NV_LAYER_BACKGROUND);
    bd.edje = nv_edje_add(bd.win, "nirvana/backdrop");
    if (!bd.edje)
        return false;
    edje_object_part_text_set(bd.edje, "nirvana.text.version", "Nirvana Desktop " NIRVANA_VERSION);
    evas_object_move(bd.edje, 0, 0);
    evas_object_show(bd.edje);
    evas_object_event_callback_add(bd.edje, EVAS_CALLBACK_MOUSE_DOWN, bg_down, NULL);

    bd.band = evas_object_rectangle_add(evas_object_evas_get(bd.win));
    evas_object_color_set(bd.band, 11, 19, 36, 70); /* (premultiplied) */
    evas_object_pass_events_set(bd.band, EINA_TRUE);

    /* (a pressed object gets the moves and the release: the icon or the background) */
    evas_object_event_callback_add(bd.edje, EVAS_CALLBACK_MOUSE_MOVE, canvas_move, NULL);
    evas_object_event_callback_add(bd.edje, EVAS_CALLBACK_MOUSE_UP, canvas_up, NULL);
    evas_object_event_callback_add(bd.edje, EVAS_CALLBACK_KEY_DOWN, on_key, NULL);
    evas_object_focus_set(bd.edje, EINA_TRUE);
    evas_object_smart_callback_add(bd.win, "focus,in", on_focus, (void *)1);
    evas_object_smart_callback_add(bd.win, "focus,out", on_focus, NULL);

    if (!nv_layer_setup(bd.win, NV_LAYER_BACKGROUND, NV_ANCHOR_TOP | NV_ANCHOR_BOTTOM | NV_ANCHOR_LEFT | NV_ANCHOR_RIGHT,
                        0, 0, -1, true, configured, NULL))
        return false;
    evas_object_show(bd.win);
    nv.backdrop = bd.win;
    nv_backdrop_reload();
    return true;
}
