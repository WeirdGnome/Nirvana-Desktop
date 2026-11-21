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
 *
 * The removable drives (drives.c) have icons too, after the assigns' ("@sdb1" in the
 * places file); opening one mounts it, Icons -> Eject takes it away.
 */
#include "desktop.h"

#include <Ecore_File.h>

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

typedef struct Icon {
    char name[NIRVANA_NAME_MAX];
    char *drive;       /* a removable drive's UDisks2 object, else NULL (an assign) */
    bool trash;        /* the trashcan */
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
    Ecore_File_Monitor *trash_monitor;
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

static void layout(bool all);

static void places_write(void)
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
    Eina_Iterator *it = eina_hash_iterator_tuple_new(bd.places);
    Eina_Hash_Tuple *t;
    EINA_ITERATOR_FOREACH(it, t) {
        const int *xy = t->data;
        fprintf(f, "%s %d %d\n", (const char *)t->key, xy[0], xy[1]);
    }
    eina_iterator_free(it);
    fclose(f);
}

/* the selected icons' places kept (snapshot) or forgotten (unsnapshot) */
static void snapshot_selected(bool keep)
{
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic) {
        if (!ic->selected)
            continue;
        if (keep) {
            int *xy = malloc(2 * sizeof(int));
            xy[0] = ic->x;
            xy[1] = ic->y;
            eina_hash_set(bd.places, ic->name, xy);
            ic->placed = true;
        } else {
            eina_hash_del_by_key(bd.places, ic->name);
            ic->placed = false;
        }
    }
    places_write();
    if (!keep)
        layout(false);
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
    if (ic->trash) {
        char t[PATH_MAX], f[PATH_MAX + 8];
        nv_trash_dir(t, sizeof t);
        snprintf(f, sizeof f, "%s/files", t);
        ecore_file_mkpath(f);
        nv_lister_open(f);
        return;
    }
    if (ic->drive) {
        Nv_Drive *d = nv_drive_find(ic->drive);
        if (d)
            nv_drive_open(d);
        return;
    }
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
        if (ic->selected && !ic->drive)
            out = eina_list_append(out, ic->name);
    return out;
}

void nv_backdrop_invert(void)
{
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic)
        icon_select(ic, !ic->selected);
}

bool nv_backdrop_any_selected(bool drives)
{
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic)
        if (ic->selected && (!drives || ic->drive))
            return true;
    return false;
}

/* the selected icons' volumes described: where they are, how full */
void nv_backdrop_information(void)
{
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic) {
        if (!ic->selected || ic->trash)
            continue;
        char posix[PATH_MAX] = "";
        const char *label = ic->name;
        if (ic->drive) {
            Nv_Drive *d = nv_drive_find(ic->drive);
            if (d) {
                label = d->label;
                snprintf(posix, sizeof posix, "%s", d->mount ? d->mount : "");
            }
        } else {
            char vol[NIRVANA_NAME_MAX + 2];
            snprintf(vol, sizeof vol, "%s:", ic->name);
            if (nirvana_to_posix(vol, posix, sizeof posix) < 0)
                posix[0] = 0;
        }
        nv_volume_information(label, posix);
    }
}

void nv_backdrop_trash_changed(void)
{
    /* the trashcan's icon made again, full or empty */
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic)
        if (ic->trash) {
            const char *group = nv_trash_full() ? "nirvana/icon/trashcan/full" : "nirvana/icon/trashcan";
            edje_object_file_set(ic->obj, nv.theme, group);
            edje_object_part_text_set(ic->obj, "nirvana.text.label", _("Trashcan"));
            if (ic->selected)
                edje_object_signal_emit(ic->obj, "nirvana,state,selected", "nirvana");
        }
}

static Eina_Bool trash_later(void *data)
{
    (void)data;
    nv_backdrop_trash_changed();
    return ECORE_CALLBACK_CANCEL;
}

static void on_trash_monitor(void *data, Ecore_File_Monitor *m, Ecore_File_Event ev, const char *path)
{
    (void)data;
    (void)m;
    (void)ev;
    (void)path;
    ecore_timer_add(0.2, trash_later, NULL);
}

void nv_backdrop_eject_selected(void)
{
    Eina_List *l;
    Icon *ic;
    EINA_LIST_FOREACH(bd.icons, l, ic)
        if (ic->selected && ic->drive) {
            Nv_Drive *d = nv_drive_find(ic->drive);
            if (d)
                nv_drive_eject(d);
        }
}

static void take_focus(void);
static void canvas_move(void *data, Evas *e, Evas_Object *o, void *event);
static void canvas_up(void *data, Evas *e, Evas_Object *o, void *event);

/* ---------------------------------------------------------------- an icon's context menu */

static Nv_Menu *cmenu;   /* the one shown (made again for each) */
static Icon *cmenu_icon; /* the icon it was asked on */

/* where a volume's icon leads (POSIX), or false (a drive not mounted) */
static bool icon_dir(Icon *ic, char *out, size_t len)
{
    if (ic->trash)
        return false;
    if (ic->drive) {
        Nv_Drive *d = nv_drive_find(ic->drive);
        if (!d || !d->mount)
            return false;
        snprintf(out, len, "%s", d->mount);
        return true;
    }
    char vol[NIRVANA_NAME_MAX + 2];
    snprintf(vol, sizeof vol, "%s:", ic->name);
    return nirvana_to_posix(vol, out, len) == 0;
}

static void cm_open(void *d, Nv_Menu_Item *it) { (void)d; (void)it; if (cmenu_icon) open_icon(cmenu_icon); }
static void cm_shell(void *d, Nv_Menu_Item *it)
{
    (void)d;
    (void)it;
    char dir[PATH_MAX];
    if (cmenu_icon && icon_dir(cmenu_icon, dir, sizeof dir))
        nv_new_shell_at(dir);
}
static void cm_info(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_backdrop_information(); }
static void cm_snapshot(void *d, Nv_Menu_Item *it) { (void)d; (void)it; snapshot_selected(true); }
static void cm_unsnapshot(void *d, Nv_Menu_Item *it) { (void)d; (void)it; snapshot_selected(false); }
static void cm_eject(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_backdrop_eject_selected(); }
static void cm_empty(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_cmd_empty_trash(); }

static void icon_context_menu(Icon *ic, int x, int y)
{
    if (cmenu)
        nv_menu_free(cmenu);
    cmenu_icon = ic;
    int n = 0;
    bool any_placed = false;
    Eina_List *l;
    Icon *o;
    EINA_LIST_FOREACH(bd.icons, l, o)
        if (o->selected) {
            n++;
            any_placed |= o->placed;
        }
    if (ic->trash) {
        cmenu = nv_menu_new(_("Trashcan"));
        nv_menu_add(cmenu, _("_Open..."), NULL, cm_open, NULL);
        nv_menu_add_separator(cmenu);
        nv_menu_add(cmenu, _("Snapshot"), NULL, cm_snapshot, NULL);
        nv_menu_add(cmenu, _("UnSnapshot"), NULL, any_placed ? cm_unsnapshot : NULL, NULL);
        nv_menu_add_separator(cmenu);
        nv_menu_add(cmenu, _("E_mpty Trashcan"), NULL, nv_trash_full() ? cm_empty : NULL, NULL);
        nv_screenbar_context(cmenu, x, y);
        return;
    }
    char dir[PATH_MAX];
    bool has_dir = icon_dir(ic, dir, sizeof dir);
    cmenu = nv_menu_new(n > 1 ? _("Icons") : _("Volume"));
    nv_menu_add(cmenu, _("_Open..."), NULL, cm_open, NULL);
    nv_menu_add(cmenu, _("Open _Shell..."), NULL, has_dir ? cm_shell : NULL, NULL);
    nv_menu_add(cmenu, _("_Find..."), NULL, NULL, NULL);
    nv_menu_add_separator(cmenu);
    nv_menu_add(cmenu, _("_Information..."), "Super+I", cm_info, NULL);
    nv_menu_add_separator(cmenu);
    nv_menu_add(cmenu, _("Snapshot"), NULL, cm_snapshot, NULL);
    nv_menu_add(cmenu, _("UnSnapshot"), NULL, any_placed ? cm_unsnapshot : NULL, NULL);
    nv_menu_add_separator(cmenu);
    nv_menu_add(cmenu, _("_Rename..."), "Super+R", NULL, NULL);
    nv_menu_add(cmenu, _("_Put Away"), NULL, NULL, NULL);
    nv_menu_add(cmenu, _("_Format..."), NULL, NULL, NULL);
    if (ic->drive)
        nv_menu_add(cmenu, _("_Eject"), "Super+J", cm_eject, NULL);
    nv_menu_add_separator(cmenu);
    nv_menu_add(cmenu, _("Paste _Into"), NULL, NULL, NULL);
    nv_screenbar_context(cmenu, x, y);
}

static void icon_down(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)e;
    (void)o;
    Icon *ic = data;
    Evas_Event_Mouse_Down *ev = event;
    take_focus();
    if (ev->button == 3) {
        /* the icon's context menu, for it (and the others selected with it) */
        if (!ic->selected) {
            nv_backdrop_select_all(false);
            icon_select(ic, true);
        }
        icon_context_menu(ic, ev->canvas.x, ev->canvas.y);
        return;
    }
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

static void icon_ready(Icon *ic, Eina_List *keep);

void nv_backdrop_reload(void)
{
    Icon *ic;
    /* the selection kept by name */
    Eina_List *keep = NULL, *l;
    EINA_LIST_FREE(bd.icons, ic) {
        if (ic->selected)
            keep = eina_list_append(keep, strdup(ic->name));
        evas_object_del(ic->obj);
        free(ic->drive);
        free(ic);
    }
    bd.press_icon = NULL;
    cmenu_icon = NULL;
    places_load();
    nirvana_assign_list list;
    if (nirvana_assign_list_load(&list) < 0)
        return;
    for (size_t i = 0; i <= list.count; i++) {
        /* (one more round: the drives) */
        if (i == list.count) {
            const Eina_List *dl;
            Nv_Drive *d;
            EINA_LIST_FOREACH(nv_drives_list(), dl, d) {
                ic = calloc(1, sizeof *ic);
                const char *base = strrchr(d->path, '/');
                snprintf(ic->name, sizeof ic->name, "@%s", base ? base + 1 : d->path);
                ic->drive = strdup(d->path);
                ic->obj = nv_edje_add(bd.win, "nirvana/icon/volume/removable");
                if (!ic->obj) {
                    free(ic->drive);
                    free(ic);
                    continue;
                }
                edje_object_part_text_set(ic->obj, "nirvana.text.label", d->label);
                icon_ready(ic, keep);
            }
            /* and the trashcan */
            ic = calloc(1, sizeof *ic);
            snprintf(ic->name, sizeof ic->name, "@trashcan");
            ic->trash = true;
            ic->obj = nv_edje_add(bd.win, nv_trash_full() ? "nirvana/icon/trashcan/full" : "nirvana/icon/trashcan");
            if (ic->obj) {
                edje_object_part_text_set(ic->obj, "nirvana.text.label", _("Trashcan"));
                icon_ready(ic, keep);
            } else {
                free(ic);
            }
            if (!bd.trash_monitor) {
                char t[PATH_MAX], f[PATH_MAX + 8];
                nv_trash_dir(t, sizeof t);
                snprintf(f, sizeof f, "%s/files", t);
                ecore_file_mkpath(f);
                bd.trash_monitor = ecore_file_monitor_add(f, on_trash_monitor, NULL);
            }
            break;
        }
        if (!list.items[i].desktop)
            continue;
        ic = calloc(1, sizeof *ic);
        snprintf(ic->name, sizeof ic->name, "%s", list.items[i].name);
        ic->obj = nv_edje_add(bd.win, icon_group(ic->name));
        if (!ic->obj) {
            free(ic);
            continue;
        }
        /* the system's volume is called "System" on the desktop (its assign is SYS:) */
        edje_object_part_text_set(ic->obj, "nirvana.text.label", !strcasecmp(ic->name, "SYS") ? _("System") : ic->name);
        icon_ready(ic, keep);
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

/* an icon made: its events, its place, kept selected */
static void icon_ready(Icon *ic, Eina_List *keep)
{
    Eina_List *l;
    {
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
    nv.view_window = NULL; /* (the screen's menus act on the desktop now) */
    nv_layer_keyboard_mode(bd.win, 1);
    ecore_timer_add(0.2, keyboard_on_demand, NULL);
}

static void bg_down(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)data;
    (void)e;
    (void)o;
    Evas_Event_Mouse_Down *ev = event;
    take_focus(); /* a click on the desktop: its menus */
    if (ev->button == 3) {
        /* the menu strip, held with the button (the backdrop is at the screen's origin) */
        nv_screenbar_hold_begin(ev->canvas.x, ev->canvas.y);
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
    if (nv_screenbar_holding()) {
        nv_screenbar_hold_move(x, y);
        return;
    }
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
    Evas_Event_Mouse_Up *ev = event;
    if (nv_screenbar_holding()) {
        if (ev->button == 3)
            nv_screenbar_hold_end(ev->canvas.x, ev->canvas.y);
        return;
    }
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
