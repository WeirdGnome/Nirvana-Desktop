/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the lister - a drawer's contents in a window, as icons.
 *
 * Spatial: each drawer has its own window, opened again it comes to the front. The
 * directory is read in a thread (Ecore_Thread) and the icons come in batches; an
 * AppImage's icon is read (libnirvana, nothing run) in the same thread. A pattern in the
 * filter field shows only the names that match it: "#?" stands for any characters,
 * "?" for one, "(a|b)" for either, case not minded; plain words match anywhere in a name.
 */
#define _GNU_SOURCE
#include "desktop.h"

#include <Efreet.h>
#include <Efreet_Mime.h>
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "nirvana_appimage.h"

typedef struct Entry {
    char *name;
    bool dir, exec, link;
    off_t size;
    time_t mtime;
    unsigned char *icon; /* an AppImage's icon, read in the thread */
    size_t icon_len;
    const char *icon_fmt;
} Entry;

typedef struct Lister {
    Nv_Window *w;
    char path[PATH_MAX];
    Evas_Object *grid, *filter, *status;
    Ecore_Thread *scan;
    Eina_List *entries;  /* Entry: all of them (the filter hides some) */
    bool show_hidden;
    char *pattern;
    int count, shown;
} Lister;

static Eina_List *listers;
static Elm_Gengrid_Item_Class *itc;

/* ---------------------------------------------------------------- the pattern */

/* "#?.(png|jpg)" -> "*.@(png|jpg)"; a pattern without wildcards matches anywhere */
static char *to_fnmatch(const char *in)
{
    if (!in || !*in)
        return NULL;
    bool wild = strpbrk(in, "#?*([|") != NULL;
    size_t n = strlen(in);
    char *out = malloc(n * 2 + 4), *o = out;
    if (!wild)
        *o++ = '*';
    for (const char *p = in; *p; p++) {
        if (p[0] == '#' && p[1] == '?') {
            *o++ = '*';
            p++;
        } else if (*p == '(') {
            *o++ = '@';
            *o++ = '(';
        } else {
            *o++ = *p;
        }
    }
    if (!wild)
        *o++ = '*';
    *o = 0;
    return out;
}

static bool visible(Lister *ls, Entry *e)
{
    if (!ls->show_hidden && e->name[0] == '.')
        return false;
    if (ls->pattern && fnmatch(ls->pattern, e->name, FNM_CASEFOLD | FNM_EXTMATCH) != 0)
        return false;
    return true;
}

/* ---------------------------------------------------------------- grid items */

static char *item_text(void *data, Evas_Object *o, const char *part)
{
    (void)o;
    (void)part;
    Entry *e = data;
    return strdup(e->name);
}

static Evas_Object *item_content(void *data, Evas_Object *o, const char *part)
{
    Entry *e = data;
    if (strcmp(part, "elm.swallow.icon"))
        return NULL;
    Evas_Object *img = elm_image_add(o);
    bool ok = false;
    if (e->icon)
        ok = elm_image_memfile_set(img, e->icon, e->icon_len, e->icon_fmt && *e->icon_fmt ? e->icon_fmt : NULL, NULL);
    if (!ok && !e->dir) {
        Lister *ls = evas_object_data_get(o, "lister");
        char full[PATH_MAX * 2];
        snprintf(full, sizeof full, "%s/%s", ls ? ls->path : "", e->name);
        const char *mime = efreet_mime_type_get(full);
        const char *icon = mime ? efreet_mime_type_icon_get(mime, elm_config_icon_theme_get(), 48) : NULL;
        if (icon)
            ok = elm_image_file_set(img, icon, NULL);
        eina_stringshare_del(icon);
    }
    if (!ok) {
        /* the theme's own drawer, program or file picture */
        const char *kind = e->dir ? "drawer" : e->exec ? "program" : "file";
        char group[64];
        snprintf(group, sizeof group, "nirvana/lister/icon/%s", kind);
        evas_object_del(img);
        img = elm_layout_add(o);
        elm_layout_file_set(img, nv.theme, group);
    }
    evas_object_size_hint_min_set(img, 48, 48);
    return img;
}

static int entry_cmp(const void *a, const void *b)
{
    const Entry *x = elm_object_item_data_get(a), *y = elm_object_item_data_get(b);
    if (x->dir != y->dir)
        return x->dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static void status_update(Lister *ls)
{
    char buf[128];
    snprintf(buf, sizeof buf, ls->scan ? _("%d items (reading...)") : _("%d items"), ls->shown);
    elm_object_text_set(ls->status, buf);
}

static void show_entry(Lister *ls, Entry *e)
{
    if (!visible(ls, e))
        return;
    elm_gengrid_item_sorted_insert(ls->grid, itc, e, entry_cmp, NULL, NULL);
    ls->shown++;
}

static void refilter(Lister *ls)
{
    elm_gengrid_clear(ls->grid);
    ls->shown = 0;
    Eina_List *l;
    Entry *e;
    EINA_LIST_FOREACH(ls->entries, l, e)
        show_entry(ls, e);
    status_update(ls);
}

/* ---------------------------------------------------------------- reading in a thread */

typedef struct Scan {
    Lister *ls;
    char path[PATH_MAX];
} Scan;

static void scan_run(void *data, Ecore_Thread *th)
{
    Scan *sc = data;
    DIR *d = opendir(sc->path);
    if (!d)
        return;
    Eina_List *batch = NULL;
    struct dirent *de;
    int n = 0;
    while ((de = readdir(d)) && !ecore_thread_check(th)) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        Entry *e = calloc(1, sizeof *e);
        e->name = strdup(de->d_name);
        char full[PATH_MAX * 2];
        snprintf(full, sizeof full, "%s/%s", sc->path, de->d_name);
        struct stat st, lst;
        if (lstat(full, &lst) == 0)
            e->link = S_ISLNK(lst.st_mode);
        if (stat(full, &st) == 0) {
            e->dir = S_ISDIR(st.st_mode);
            e->exec = !e->dir && (st.st_mode & 0111);
            e->size = st.st_size;
            e->mtime = st.st_mtime;
        }
        size_t nl = strlen(e->name);
        if (!e->dir && nl > 9 && !strcasecmp(e->name + nl - 9, ".AppImage")) {
            nirvana_appimage *img;
            if (nirvana_appimage_open(full, &img) == 0) {
                const char *fmt = NULL;
                if (nirvana_appimage_icon(img, &e->icon, &e->icon_len, &fmt) == 0)
                    e->icon_fmt = fmt;
                nirvana_appimage_close(img);
            }
        }
        batch = eina_list_append(batch, e);
        if (++n % 64 == 0) {
            ecore_thread_feedback(th, batch);
            batch = NULL;
        }
    }
    closedir(d);
    if (batch)
        ecore_thread_feedback(th, batch);
}

static void entry_free(Entry *e)
{
    free(e->name);
    free(e->icon);
    free(e);
}

static void scan_feedback(void *data, Ecore_Thread *th, void *msg)
{
    (void)th;
    Scan *sc = data;
    Eina_List *batch = msg;
    Entry *e;
    if (!sc->ls) {
        EINA_LIST_FREE(batch, e)
            entry_free(e);
        return;
    }
    EINA_LIST_FREE(batch, e) {
        sc->ls->entries = eina_list_append(sc->ls->entries, e);
        show_entry(sc->ls, e);
    }
    status_update(sc->ls);
}

static void scan_end(void *data, Ecore_Thread *th)
{
    (void)th;
    Scan *sc = data;
    if (sc->ls) {
        sc->ls->scan = NULL;
        if (evas_object_data_get(sc->ls->grid, "scan") == sc)
            evas_object_data_del(sc->ls->grid, "scan");
        status_update(sc->ls);
    }
    free(sc);
}

static void rescan(Lister *ls)
{
    if (ls->scan) {
        /* the old reading's results go nowhere */
        Scan *old = evas_object_data_get(ls->grid, "scan");
        if (old)
            old->ls = NULL;
        ecore_thread_cancel(ls->scan);
        ls->scan = NULL;
    }
    elm_gengrid_clear(ls->grid);
    Entry *e;
    EINA_LIST_FREE(ls->entries, e)
        entry_free(e);
    ls->shown = 0;
    Scan *sc = calloc(1, sizeof *sc);
    sc->ls = ls;
    snprintf(sc->path, sizeof sc->path, "%s", ls->path);
    ls->scan = ecore_thread_feedback_run(scan_run, scan_feedback, scan_end, scan_end, sc, EINA_FALSE);
    evas_object_data_set(ls->grid, "scan", sc);
    status_update(ls);
}

/* ---------------------------------------------------------------- actions */

static void open_entry(Lister *ls, Entry *e)
{
    char full[PATH_MAX * 2];
    snprintf(full, sizeof full, "%s%s%s", ls->path, strcmp(ls->path, "/") ? "/" : "", e->name);
    if (e->dir)
        nv_lister_open(full);
    else
        nv_open_path(full);
}

static void on_double(void *data, Evas_Object *o, void *event)
{
    (void)o;
    open_entry(data, elm_object_item_data_get(event));
}

static void on_activated(void *data, Evas_Object *o, void *event)
{
    (void)o;
    open_entry(data, elm_object_item_data_get(event));
}

static void open_parent(Lister *ls)
{
    char parent[PATH_MAX];
    snprintf(parent, sizeof parent, "%s", ls->path);
    char *slash = strrchr(parent, '/');
    if (!slash)
        return;
    if (slash == parent)
        slash[1] = 0;
    else
        *slash = 0;
    nv_lister_open(parent);
}

static void m_parent(void *data, Nv_Menu_Item *it) { (void)it; open_parent(data); }
static void m_update(void *data, Nv_Menu_Item *it) { (void)it; rescan(data); }
static void m_hidden(void *data, Nv_Menu_Item *it)
{
    Lister *ls = data;
    ls->show_hidden = it->checked;
    refilter(ls);
}
static void m_select_all(void *data, Nv_Menu_Item *it)
{
    (void)it;
    Lister *ls = data;
    for (Elm_Object_Item *gi = elm_gengrid_first_item_get(ls->grid); gi; gi = elm_gengrid_item_next_get(gi))
        elm_gengrid_item_selected_set(gi, EINA_TRUE);
}

static void on_parent_clicked(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    open_parent(data);
}

static void on_filter_changed(void *data, Evas_Object *o, void *ev)
{
    (void)ev;
    Lister *ls = data;
    free(ls->pattern);
    ls->pattern = to_fnmatch(elm_entry_entry_get(o));
    refilter(ls);
}

static void lister_close(void *data, Nv_Window *w)
{
    Lister *ls = data;
    Scan *sc = evas_object_data_get(ls->grid, "scan");
    if (ls->scan && sc)
        sc->ls = NULL; /* the thread's results go nowhere */
    if (ls->scan)
        ecore_thread_cancel(ls->scan);
    listers = eina_list_remove(listers, ls);
    Entry *e;
    elm_gengrid_clear(ls->grid);
    EINA_LIST_FREE(ls->entries, e)
        entry_free(e);
    free(ls->pattern);
    evas_object_del(w->win);
    free(ls);
}

void nv_lister_open(const char *path)
{
    char posix[PATH_MAX], real[PATH_MAX];
    if (nirvana_to_posix(path, posix, sizeof posix) < 0) {
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof msg, _("There is no volume \"%s\"."), path);
        nv_message(_("Open"), msg);
        return;
    }
    if (realpath(posix, real))
        snprintf(posix, sizeof posix, "%s", real);
    struct stat st;
    if (stat(posix, &st) < 0 || !S_ISDIR(st.st_mode)) {
        nv_open_path(posix);
        return;
    }
    /* spatial: a drawer has one window */
    Eina_List *l;
    Lister *ls;
    EINA_LIST_FOREACH(listers, l, ls)
        if (!strcmp(ls->path, posix)) {
            elm_win_raise(ls->w->win);
            elm_win_activate(ls->w->win);
            return;
        }
    if (!itc) {
        itc = elm_gengrid_item_class_new();
        itc->item_style = "default";
        itc->func.text_get = item_text;
        itc->func.content_get = item_content;
    }
    ls = calloc(1, sizeof *ls);
    snprintf(ls->path, sizeof ls->path, "%s", posix);
    char title[PATH_MAX];
    if (posix_to_nirvana(posix, title, sizeof title) < 0)
        snprintf(title, sizeof title, "%s", posix);
    ls->w = nv_window_new("nirvana-lister", title, 560, 380);
    ls->w->on_close = lister_close;
    ls->w->data = ls;
    Evas_Object *win = ls->w->win;

    Evas_Object *box = elm_box_add(win);
    evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    Evas_Object *row = elm_box_add(win);
    elm_box_horizontal_set(row, EINA_TRUE);
    evas_object_size_hint_weight_set(row, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(row, EVAS_HINT_FILL, 0.5);
    Evas_Object *b = elm_button_add(win);
    elm_object_text_set(b, _("Parent"));
    evas_object_smart_callback_add(b, "clicked", on_parent_clicked, ls);
    elm_box_pack_end(row, b);
    evas_object_show(b);
    ls->filter = elm_entry_add(win);
    elm_entry_single_line_set(ls->filter, EINA_TRUE);
    elm_entry_scrollable_set(ls->filter, EINA_TRUE);
    elm_object_part_text_set(ls->filter, "guide", _("Filter (#?.txt)"));
    evas_object_size_hint_weight_set(ls->filter, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(ls->filter, EVAS_HINT_FILL, 0.5);
    evas_object_smart_callback_add(ls->filter, "changed,user", on_filter_changed, ls);
    elm_box_pack_end(row, ls->filter);
    evas_object_show(ls->filter);
    elm_box_pack_end(box, row);
    evas_object_show(row);

    ls->grid = elm_gengrid_add(win);
    evas_object_data_set(ls->grid, "lister", ls);
    elm_gengrid_item_size_set(ls->grid, ELM_SCALE_SIZE(92), ELM_SCALE_SIZE(84));
    elm_gengrid_multi_select_set(ls->grid, EINA_TRUE);
    elm_gengrid_align_set(ls->grid, 0.0, 0.0);
    evas_object_size_hint_weight_set(ls->grid, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(ls->grid, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_smart_callback_add(ls->grid, "clicked,double", on_double, ls);
    evas_object_smart_callback_add(ls->grid, "activated", on_activated, ls);
    elm_box_pack_end(box, ls->grid);
    evas_object_show(ls->grid);

    ls->status = elm_label_add(win);
    evas_object_size_hint_align_set(ls->status, 0.0, 0.5);
    elm_box_pack_end(box, ls->status);
    evas_object_show(ls->status);

    nv_window_content_set(ls->w, box);
    evas_object_show(box);

    Nv_Menu *wm = nv_menu_new("lister");
    nv_menu_add(wm, _("Open _Parent"), "Super+P", m_parent, ls);
    nv_menu_add(wm, _("_Update"), NULL, m_update, ls);
    nv_menu_add(wm, _("Select _Contents"), NULL, m_select_all, ls);
    nv_menu_add_check(wm, _("Show _Hidden Files"), false, m_hidden, ls);
    ls->w->window_menu = wm;

    listers = eina_list_append(listers, ls);
    nv_window_show(ls->w);
    rescan(ls);
}
