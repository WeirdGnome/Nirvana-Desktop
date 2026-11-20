/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the Nirvana Dock - a panel of launchers at the bottom of the screen.
 *
 * Its contents are in ~/.nirvana/Prefs/dock.conf, one item a line:
 *
 *     launcher /usr/share/applications/org.example.Editor.desktop
 *     launcher Home:Apps/Some.AppImage
 *     separator
 *     sub Internet            (a sub-dock: the launchers up to "end" in a column
 *       launcher ...           that opens above it)
 *     end
 *     docky clock | cpu | ram (small live displays)
 *
 * A click starts a launcher (a .desktop file's program, an AppImage or a program, else
 * the file opened as wbrun does) or opens a sub-dock. Files dropped on the dock (.desktop
 * files, AppImages, programs) are pinned; a right click gives an item's menu (Open,
 * Move Left/Right, Remove from Dock). The dock is a layer surface on Wayland that keeps
 * its strip of the screen, a dock window on X11; a sub-dock makes it taller on Wayland
 * and is a popup window on X11.
 */
#define _GNU_SOURCE
#include "desktop.h"

#include <Efreet.h>
#include <Ecore_X.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ITEM 56
#define PAD 10

typedef enum { IT_LAUNCHER, IT_SEPARATOR, IT_DOCKY, IT_SUB } Item_Kind;
typedef enum { DOCKY_CLOCK, DOCKY_CPU, DOCKY_RAM } Docky_Kind;

typedef struct Dock_Item {
    Item_Kind kind;
    char *path;          /* a launcher: what it starts (as written: a Nirvana path is kept) */
    char *name;          /* a sub-dock's, or the program's (the tooltip) */
    Docky_Kind docky;
    Eina_List *children; /* a sub-dock's launchers */
    Evas_Object *obj;
    struct Dock_Item *parent;
} Dock_Item;

static struct {
    Evas_Object *win, *bg, *box;
    Evas_Object *sub_win, *sub_bg, *sub_box; /* the open sub-dock */
    Dock_Item *sub_open;
    Eina_List *items;
    int panel_w, panel_h, surf_w, surf_h;
    Ecore_Timer *tick, *sub_close;
    unsigned long long cpu_total, cpu_idle;
    Nv_Menu *menu;
    Dock_Item *menu_item;
} dk;

static void layout(void);
static void save(void);

/* ---------------------------------------------------------------- the configuration */

static void conf_path(char *out, size_t len)
{
    snprintf(out, len, "%s/.nirvana/Prefs/dock.conf", getenv("HOME") ? getenv("HOME") : "");
}

static Dock_Item *item_new(Item_Kind k, const char *path)
{
    Dock_Item *it = calloc(1, sizeof *it);
    it->kind = k;
    it->path = path ? strdup(path) : NULL;
    return it;
}

static void item_free(Dock_Item *it)
{
    Dock_Item *c;
    EINA_LIST_FREE(it->children, c)
        item_free(c);
    if (it->obj)
        evas_object_del(it->obj);
    free(it->path);
    free(it->name);
    free(it);
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    return s;
}

static Dock_Item *docky_item(const char *what)
{
    Dock_Item *it = item_new(IT_DOCKY, NULL);
    it->docky = !strcasecmp(what, "cpu") ? DOCKY_CPU : !strcasecmp(what, "ram") ? DOCKY_RAM : DOCKY_CLOCK;
    return it;
}

/* the first of these desktop files that is installed */
static void add_default(Eina_List **items, const char *const *ids)
{
    for (; *ids; ids++) {
        Efreet_Desktop *d = efreet_util_desktop_file_id_find(*ids);
        if (d) {
            *items = eina_list_append(*items, item_new(IT_LAUNCHER, d->orig_path));
            efreet_desktop_free(d);
            return;
        }
    }
}

static void load(void)
{
    char path[PATH_MAX];
    conf_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) {
        /* a first dock: a terminal, a browser, a text editor, the dockies */
        static const char *const term[] = {"foot.desktop", "org.gnome.Terminal.desktop", "xfce4-terminal.desktop",
                                           "lxterminal.desktop", "terminology.desktop", "debian-xterm.desktop",
                                           "xterm.desktop", NULL};
        static const char *const web[] = {"firefox-esr.desktop", "firefox.desktop", "chromium.desktop", NULL};
        static const char *const edit[] = {"org.gnome.TextEditor.desktop", "mousepad.desktop", "org.xfce.mousepad.desktop",
                                           "pluma.desktop", "gedit.desktop", "kate.desktop", NULL};
        static const char *const office[] = {"libreoffice-startcenter.desktop", NULL};
        add_default(&dk.items, term);
        add_default(&dk.items, web);
        add_default(&dk.items, edit);
        add_default(&dk.items, office);
        dk.items = eina_list_append(dk.items, item_new(IT_SEPARATOR, NULL));
        dk.items = eina_list_append(dk.items, docky_item("cpu"));
        dk.items = eina_list_append(dk.items, docky_item("ram"));
        dk.items = eina_list_append(dk.items, docky_item("clock"));
        return;
    }
    char line[PATH_MAX + 32];
    Dock_Item *sub = NULL;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (!*s || *s == '#')
            continue;
        char *arg = s;
        while (*arg && !isspace((unsigned char)*arg))
            arg++;
        if (*arg)
            *arg++ = 0;
        arg = trim(arg);
        Dock_Item *it = NULL;
        if (!strcmp(s, "launcher") && *arg)
            it = item_new(IT_LAUNCHER, arg);
        else if (!strcmp(s, "separator") && !sub)
            it = item_new(IT_SEPARATOR, NULL);
        else if (!strcmp(s, "docky") && !sub)
            it = docky_item(arg);
        else if (!strcmp(s, "sub") && !sub) {
            sub = item_new(IT_SUB, NULL);
            sub->name = strdup(*arg ? arg : "Sub-dock");
            dk.items = eina_list_append(dk.items, sub);
            continue;
        } else if (!strcmp(s, "end")) {
            sub = NULL;
            continue;
        }
        if (!it)
            continue;
        if (sub && it->kind == IT_LAUNCHER) {
            it->parent = sub;
            sub->children = eina_list_append(sub->children, it);
        } else {
            dk.items = eina_list_append(dk.items, it);
        }
    }
    fclose(f);
}

static void save(void)
{
    char path[PATH_MAX], dir[PATH_MAX], tmp[PATH_MAX + 8];
    snprintf(dir, sizeof dir, "%s/.nirvana/Prefs", getenv("HOME") ? getenv("HOME") : "");
    mkdir(dir, 0755);
    conf_path(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    fputs("# Nirvana Dock: launcher PATH | separator | docky clock|cpu|ram | sub NAME ... end\n", f);
    Eina_List *l, *k;
    Dock_Item *it, *c;
    EINA_LIST_FOREACH(dk.items, l, it) {
        switch (it->kind) {
        case IT_LAUNCHER: fprintf(f, "launcher %s\n", it->path); break;
        case IT_SEPARATOR: fputs("separator\n", f); break;
        case IT_DOCKY:
            fprintf(f, "docky %s\n", it->docky == DOCKY_CPU ? "cpu" : it->docky == DOCKY_RAM ? "ram" : "clock");
            break;
        case IT_SUB:
            fprintf(f, "sub %s\n", it->name);
            EINA_LIST_FOREACH(it->children, k, c)
                fprintf(f, "  launcher %s\n", c->path);
            fputs("end\n", f);
            break;
        }
    }
    fclose(f);
    rename(tmp, path);
}

/* ---------------------------------------------------------------- launching */

static void *dock_command_cb(void *data, Efreet_Desktop *desktop, char *command, int remaining)
{
    (void)data;
    (void)desktop;
    (void)remaining;
    int r = nirvana_spawn_shell(command);
    if (r < 0) {
        char msg[1024];
        snprintf(msg, sizeof msg, _("%s could not be started: %s"), command, strerror(-r));
        nv_message(_("Dock"), msg);
    }
    return NULL;
}

static void launch(Dock_Item *it)
{
    char posix[PATH_MAX];
    if (nirvana_to_posix(it->path, posix, sizeof posix) < 0)
        snprintf(posix, sizeof posix, "%s", it->path);
    if (nv_is_desktop_file(posix)) {
        Efreet_Desktop *d = efreet_desktop_get(posix);
        if (d) {
            if (d->exec)
                efreet_desktop_command_get(d, NULL, dock_command_cb, NULL);
            efreet_desktop_free(d);
            return;
        }
    }
    struct stat st;
    if (stat(posix, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0111)) {
        const char *argv[] = {posix, NULL};
        int r = nirvana_spawn(argv);
        if (r < 0) {
            char msg[PATH_MAX + 64];
            snprintf(msg, sizeof msg, _("%s could not be started: %s"), posix, strerror(-r));
            nv_message(_("Dock"), msg);
        }
        return;
    }
    nv_open_path(posix);
}

/* a launcher's name (its .desktop Name, else its file's name) */
static void launcher_name(Dock_Item *it, char *out, size_t len)
{
    char posix[PATH_MAX];
    if (nirvana_to_posix(it->path, posix, sizeof posix) < 0)
        snprintf(posix, sizeof posix, "%s", it->path);
    if (nv_is_desktop_file(posix)) {
        Efreet_Desktop *d = efreet_desktop_get(posix);
        if (d && d->name) {
            snprintf(out, len, "%s", d->name);
            efreet_desktop_free(d);
            return;
        }
        if (d)
            efreet_desktop_free(d);
    }
    const char *slash = strrchr(posix, '/');
    snprintf(out, len, "%s", slash ? slash + 1 : posix);
}

/* ---------------------------------------------------------------- dockies */

static void docky_update(Dock_Item *it)
{
    Evas_Object *e = elm_layout_edje_get(it->obj);
    char val[32];
    double frac = -1;
    if (it->docky == DOCKY_CLOCK) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(val, sizeof val, "%H:%M", &tm);
        edje_object_part_text_set(e, "nirvana.text.label", "");
    } else if (it->docky == DOCKY_CPU) {
        FILE *f = fopen("/proc/stat", "r");
        unsigned long long u = 0, n = 0, s = 0, id = 0, io = 0, irq = 0, sirq = 0, st = 0;
        if (f) {
            if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &id, &io, &irq, &sirq, &st) < 4)
                u = n = s = id = 0;
            fclose(f);
        }
        unsigned long long total = u + n + s + id + io + irq + sirq + st, idle = id + io;
        if (dk.cpu_total && total > dk.cpu_total)
            frac = 1.0 - (double)(idle - dk.cpu_idle) / (double)(total - dk.cpu_total);
        else
            frac = 0;
        dk.cpu_total = total;
        dk.cpu_idle = idle;
        snprintf(val, sizeof val, "%d%%", (int)(frac * 100 + 0.5));
        edje_object_part_text_set(e, "nirvana.text.label", _("CPU"));
    } else {
        FILE *f = fopen("/proc/meminfo", "r");
        unsigned long long total = 0, avail = 0, v;
        char key[64];
        if (f) {
            while (fscanf(f, "%63s %llu kB\n", key, &v) == 2) {
                if (!strcmp(key, "MemTotal:"))
                    total = v;
                else if (!strcmp(key, "MemAvailable:"))
                    avail = v;
            }
            fclose(f);
        }
        frac = total ? 1.0 - (double)avail / (double)total : 0;
        snprintf(val, sizeof val, "%d%%", (int)(frac * 100 + 0.5));
        edje_object_part_text_set(e, "nirvana.text.label", _("RAM"));
    }
    edje_object_part_text_set(e, "nirvana.text.value", val);
    if (frac >= 0)
        edje_object_part_drag_value_set(e, "level", 0.0, 1.0 - frac);
}

static Eina_Bool tick(void *data)
{
    (void)data;
    Eina_List *l;
    Dock_Item *it;
    EINA_LIST_FOREACH(dk.items, l, it)
        if (it->kind == IT_DOCKY && it->obj)
            docky_update(it);
    return ECORE_CALLBACK_RENEW;
}

/* ---------------------------------------------------------------- the item's menu */

static void sub_close(void);

static void m_open(void *data, Nv_Menu_Item *mi)
{
    (void)data;
    (void)mi;
    if (dk.menu_item && dk.menu_item->kind == IT_LAUNCHER)
        launch(dk.menu_item);
}

static Eina_List **owner_list(Dock_Item *it)
{
    return it->parent ? &it->parent->children : &dk.items;
}

static void m_move(void *data, Nv_Menu_Item *mi)
{
    (void)data;
    Dock_Item *it = dk.menu_item;
    if (!it)
        return;
    Eina_List **list = owner_list(it);
    Eina_List *node = eina_list_data_find_list(*list, it);
    Eina_List *other = mi->id < 0 ? eina_list_prev(node) : eina_list_next(node);
    if (!other)
        return;
    void *t = eina_list_data_get(other);
    eina_list_data_set(other, it);
    eina_list_data_set(node, t);
    sub_close();
    save();
    layout();
}

static void m_remove(void *data, Nv_Menu_Item *mi)
{
    (void)data;
    (void)mi;
    Dock_Item *it = dk.menu_item;
    if (!it)
        return;
    sub_close();
    Eina_List **list = owner_list(it);
    *list = eina_list_remove(*list, it);
    dk.menu_item = NULL;
    item_free(it);
    save();
    layout();
}

static void item_menu(Dock_Item *it, int x, int y)
{
    if (dk.menu && !nv_screenbar_menu_is_open()) {
        nv_menu_free(dk.menu);
        dk.menu = NULL;
    }
    if (dk.menu)
        return;
    dk.menu_item = it;
    dk.menu = nv_menu_new("dock");
    if (it->kind == IT_LAUNCHER)
        nv_menu_add(dk.menu, _("_Open"), NULL, m_open, NULL);
    Nv_Menu_Item *mi = nv_menu_add(dk.menu, _("Move _Left"), NULL, m_move, NULL);
    mi->id = -1;
    mi = nv_menu_add(dk.menu, _("Move _Right"), NULL, m_move, NULL);
    mi->id = 1;
    nv_menu_add_separator(dk.menu);
    nv_menu_add(dk.menu, _("Remove from Dock"), NULL, m_remove, NULL);
    nv_screenbar_popup(dk.menu, x, y);
}

/* ---------------------------------------------------------------- items' objects */

/* the dock's place on the screen (for screen coordinates of its items) */
static void dock_origin(int *x, int *y)
{
    /* (centred at the bottom of the screen, on Wayland and on X11 alike) */
    *x = (nv.screen_w - dk.surf_w) / 2;
    *y = nv.screen_h - dk.surf_h;
}

static void sub_open(Dock_Item *it);

static void on_item_activate(void *data, Evas_Object *o, const char *em, const char *src)
{
    (void)o;
    (void)em;
    (void)src;
    Dock_Item *it = data;
    if (getenv("NIRVANA_DEBUG"))
        fprintf(stderr, "dock activate %d\n", it->kind);
    if (it->kind == IT_SUB) {
        if (dk.sub_open == it)
            sub_close();
        else
            sub_open(it);
        return;
    }
    if (it->kind == IT_LAUNCHER) {
        launch(it);
            if (it->parent)
            sub_close();
    }
}

static void on_item_down(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)e;
    Dock_Item *it = data;
    Evas_Event_Mouse_Down *ev = event;
    if (getenv("NIRVANA_DEBUG"))
        fprintf(stderr, "dock down %d\n", ev->button);
    if (ev->button != 3)
        return;
    int ox, oy, iy;
    dock_origin(&ox, &oy);
    evas_object_geometry_get(o, NULL, &iy, NULL, NULL);
    if (it->parent && nv.backend == NV_BACKEND_X11) {
        /* (in the sub-dock's popup window, above the dock) */
        int sx, sy;
        evas_object_geometry_get(dk.sub_win, &sx, &sy, NULL, NULL);
        ox = sx;
        oy = sy;
    }
    item_menu(it, ox + ev->canvas.x, oy + iy - 150);
}

static Evas_Object *item_obj(Evas_Object *win, Dock_Item *it)
{
    Evas_Object *o = elm_layout_add(win);
    switch (it->kind) {
    case IT_SEPARATOR:
        elm_layout_file_set(o, nv.theme, "nirvana/dock/separator");
        break;
    case IT_DOCKY:
        elm_layout_file_set(o, nv.theme, "nirvana/dock/docky");
        if (it->docky == DOCKY_CLOCK)
            elm_layout_signal_emit(o, "nirvana,state,nogauge", "nirvana");
        break;
    case IT_SUB: {
        elm_layout_file_set(o, nv.theme, "nirvana/dock/item");
        elm_layout_signal_emit(o, "nirvana,state,subdock", "nirvana");
        /* a sub-dock's picture: its first launcher's, else a drawer */
        Dock_Item *first = eina_list_data_get(it->children);
        char posix[PATH_MAX] = "";
        if (first && nirvana_to_posix(first->path, posix, sizeof posix) < 0)
            snprintf(posix, sizeof posix, "%s", first->path);
        elm_layout_content_set(o, "nirvana.swallow.icon", nv_icon_for_path(win, first ? posix : "/", !first, 48));
        elm_object_tooltip_text_set(o, it->name);
        break;
    }
    case IT_LAUNCHER: {
        elm_layout_file_set(o, nv.theme, "nirvana/dock/item");
        char posix[PATH_MAX], name[PATH_MAX];
        if (nirvana_to_posix(it->path, posix, sizeof posix) < 0)
            snprintf(posix, sizeof posix, "%s", it->path);
        struct stat st;
        bool dir = stat(posix, &st) == 0 && S_ISDIR(st.st_mode);
        elm_layout_content_set(o, "nirvana.swallow.icon", nv_icon_for_path(win, posix, dir, 48));
        launcher_name(it, name, sizeof name);
        elm_object_tooltip_text_set(o, name);
        break;
    }
    }
    elm_layout_signal_callback_add(o, "nirvana,dock,activate", "nirvana", on_item_activate, it);
    evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_DOWN, on_item_down, it);
    int w = it->kind == IT_SEPARATOR ? 10 : ITEM;
    evas_object_size_hint_min_set(o, w, ITEM);
    evas_object_size_hint_max_set(o, w, ITEM);
    evas_object_show(o);
    if (it->kind == IT_DOCKY) {
        it->obj = o;
        docky_update(it);
    }
    return o;
}

/* ---------------------------------------------------------------- sub-docks */

static void surface_size(int w, int h)
{
    dk.surf_w = w;
    dk.surf_h = h;
    nv_layer_resize(dk.win, w, h);
}

static void sub_close(void)
{
    if (!dk.sub_open)
        return;
    Eina_List *l;
    Dock_Item *c;
    EINA_LIST_FOREACH(dk.sub_open->children, l, c)
        c->obj = NULL; /* (deleted with the box) */
    dk.sub_open = NULL;
    if (dk.sub_close) {
        ecore_timer_del(dk.sub_close);
        dk.sub_close = NULL;
    }
    if (nv.backend == NV_BACKEND_WAYLAND) {
        evas_object_del(dk.sub_bg);
        dk.sub_bg = dk.sub_box = NULL;
        surface_size(dk.panel_w, dk.panel_h);
        evas_object_move(dk.bg, 0, 0);
    } else {
        evas_object_del(dk.sub_win);
        dk.sub_win = dk.sub_bg = dk.sub_box = NULL;
    }
}

static void sub_open(Dock_Item *it)
{
    sub_close();
    if (!it->children)
        return;
    dk.sub_open = it;
    int n = (int)eina_list_count(it->children);
    int cw = ITEM + 2 * PAD, ch = n * ITEM + 2 * PAD;
    int ix, iw;
    evas_object_geometry_get(it->obj, &ix, NULL, &iw, NULL);
    Evas_Object *win;
    int cx, cy;
    if (nv.backend == NV_BACKEND_WAYLAND) {
        /* the surface grows upwards; the panel stays at its bottom */
        win = dk.win;
        surface_size(dk.panel_w, dk.panel_h + ch + 4);
        evas_object_move(dk.bg, 0, ch + 4);
        cx = ix + iw / 2 - cw / 2;
        cy = 0;
    } else {
        int ox, oy;
        dock_origin(&ox, &oy);
        dk.sub_win = win = elm_win_add(dk.win, "nirvana-subdock", ELM_WIN_POPUP_MENU);
        elm_win_borderless_set(win, EINA_TRUE);
        elm_win_override_set(win, EINA_TRUE);
        evas_object_move(win, ox + ix + iw / 2 - cw / 2, oy - ch - 4);
        evas_object_resize(win, cw, ch);
        evas_object_show(win);
        cx = 0;
        cy = 0;
    }
    dk.sub_bg = elm_layout_add(win);
    elm_layout_file_set(dk.sub_bg, nv.theme, "nirvana/dock/bg");
    dk.sub_box = elm_box_add(win);
    elm_layout_content_set(dk.sub_bg, "nirvana.swallow.content", dk.sub_box);
    Eina_List *l;
    Dock_Item *c;
    EINA_LIST_FOREACH(it->children, l, c) {
        c->obj = item_obj(win, c);
        elm_box_pack_end(dk.sub_box, c->obj);
    }
    evas_object_move(dk.sub_bg, cx, cy);
    evas_object_resize(dk.sub_bg, cw, ch);
    evas_object_show(dk.sub_bg);
}

/* ---------------------------------------------------------------- the panel */

static void layout(void)
{
    elm_box_clear(dk.box);
    Eina_List *l;
    Dock_Item *it;
    int w = 2 * PAD;
    EINA_LIST_FOREACH(dk.items, l, it) {
        it->obj = NULL;
        it->obj = item_obj(dk.win, it);
        elm_box_pack_end(dk.box, it->obj);
        w += it->kind == IT_SEPARATOR ? 10 : ITEM;
    }
    if (w < 3 * ITEM)
        w = 3 * ITEM;
    dk.panel_w = w;
    dk.panel_h = ITEM + 2 * PAD - 6;
    evas_object_resize(dk.bg, dk.panel_w, dk.panel_h);
    evas_object_move(dk.bg, 0, 0);
    surface_size(dk.panel_w, dk.panel_h);
}

/* ---------------------------------------------------------------- dropping files */

static void uri_unescape(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (s[0] == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(hex, NULL, 16);
            s += 2;
        } else {
            *o++ = *s;
        }
    }
    *o = 0;
}

static Eina_Bool on_drop(void *data, Evas_Object *obj, Elm_Selection_Data *ev)
{
    (void)data;
    (void)obj;
    if (!ev->data || !ev->len)
        return EINA_FALSE;
    char *text = strndup(ev->data, ev->len);
    char *save_ptr = NULL;
    int added = 0;
    /* the new launchers go before the first separator or docky (after the programs) */
    Eina_List *before = NULL, *l;
    Dock_Item *it;
    EINA_LIST_FOREACH(dk.items, l, it)
        if (it->kind == IT_SEPARATOR || it->kind == IT_DOCKY) {
            before = l;
            break;
        }
    for (char *line = strtok_r(text, "\r\n", &save_ptr); line; line = strtok_r(NULL, "\r\n", &save_ptr)) {
        char *p = trim(line);
        if (!strncmp(p, "file://", 7))
            p += 7;
        uri_unescape(p);
        if (*p != '/')
            continue;
        struct stat st;
        if (stat(p, &st) < 0)
            continue;
        if (!nv_is_desktop_file(p) && !nv_is_appimage(p) && !(S_ISREG(st.st_mode) && (st.st_mode & 0111)) &&
            !S_ISDIR(st.st_mode))
            continue;
        char np[PATH_MAX];
        if (posix_to_nirvana(p, np, sizeof np) < 0 || !strncmp(np, "SYS:", 4))
            snprintf(np, sizeof np, "%s", p);
        Dock_Item *n = item_new(IT_LAUNCHER, np);
        if (before)
            dk.items = eina_list_prepend_relative_list(dk.items, n, before);
        else
            dk.items = eina_list_append(dk.items, n);
        added++;
    }
    free(text);
    if (added) {
        sub_close();
        save();
        layout();
    }
    return added > 0;
}

/* ---------------------------------------------------------------- the dock */

static void configured(void *data, Evas_Object *win, int w, int h)
{
    (void)data;
    (void)win;
    (void)w;
    (void)h;
}

bool nv_dock_create(void)
{
    if (!nv_theme_data("nirvana/theme/name") || !edje_file_group_exists(nv.theme, "nirvana/dock/bg"))
        return false;
    dk.win = nv_layer_win_add("nirvana-dock", NV_LAYER_TOP);
    elm_win_alpha_set(dk.win, EINA_TRUE);
    dk.bg = elm_layout_add(dk.win);
    elm_layout_file_set(dk.bg, nv.theme, "nirvana/dock/bg");
    dk.box = elm_box_add(dk.win);
    elm_box_horizontal_set(dk.box, EINA_TRUE);
    elm_layout_content_set(dk.bg, "nirvana.swallow.content", dk.box);
    evas_object_show(dk.bg);
    load();
    layout();
    elm_drop_target_add(dk.win, ELM_SEL_FORMAT_TARGETS, NULL, NULL, NULL, NULL, NULL, NULL, on_drop, NULL);
    if (!nv_layer_setup(dk.win, NV_LAYER_TOP, NV_ANCHOR_BOTTOM, dk.panel_w, dk.panel_h, dk.panel_h, false,
                        configured, NULL))
        return false;
    evas_object_show(dk.win);
    dk.tick = ecore_timer_add(2.0, tick, NULL);
    return true;
}
