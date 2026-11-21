/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: what the menus do to files - the file clipboard (Cut, Copy, Paste),
 * Delete and Move to Trashcan (asked first, as Ambient asks), Rename, New Drawer, Copy To
 * and Move To (a drawer chosen), Restore, Empty Trashcan. The work itself is fileops.c's.
 */
#include "desktop.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

static Eina_List *clip; /* char *: POSIX paths */
static bool clip_cut;

static Eina_List *paths_dup(const Eina_List *paths)
{
    Eina_List *out = NULL;
    const Eina_List *l;
    const char *p;
    EINA_LIST_FOREACH(paths, l, p)
        out = eina_list_append(out, strdup(p));
    return out;
}

void nv_paths_free(Eina_List *paths)
{
    char *p;
    EINA_LIST_FREE(paths, p)
        free(p);
}

static const char *base_name(const char *path)
{
    const char *b = strrchr(path, '/');
    return b && b[1] ? b + 1 : path;
}

/* ---------------------------------------------------------------- the clipboard */

void nv_clip_set(const Eina_List *paths, bool cut)
{
    nv_paths_free(clip);
    clip = paths_dup(paths);
    clip_cut = cut;
    /* the paths also as text, for other programs */
    Eina_Strbuf *sb = eina_strbuf_new();
    const Eina_List *l;
    const char *p;
    EINA_LIST_FOREACH(paths, l, p) {
        eina_strbuf_append_printf(sb, "file://%s\n", p);
    }
    if (nv.backdrop && eina_strbuf_length_get(sb))
        elm_cnp_selection_set(nv.backdrop, ELM_SEL_TYPE_CLIPBOARD, ELM_SEL_FORMAT_TEXT, eina_strbuf_string_get(sb),
                              eina_strbuf_length_get(sb));
    eina_strbuf_free(sb);
}

bool nv_clip_has(void)
{
    return clip != NULL;
}

void nv_clip_paste(const char *dir)
{
    if (!clip || !dir)
        return;
    nv_file_op(clip_cut ? NV_OP_MOVE : NV_OP_COPY, paths_dup(clip), dir);
    if (clip_cut) {
        /* (moved: they are not there to be pasted again) */
        nv_paths_free(clip);
        clip = NULL;
    }
}

/* ---------------------------------------------------------------- delete, trash */

typedef struct Confirm {
    Eina_List *paths;
    Nv_File_Op op;
} Confirm;

static void confirmed(void *data, int choice)
{
    Confirm *c = data;
    if (choice == 0)
        nv_file_op(c->op, c->paths, NULL);
    else
        nv_paths_free(c->paths);
    free(c);
}

static void ask_first(Nv_File_Op op, const Eina_List *paths, const char *one, const char *many, const char *title,
                      const char *buttons)
{
    unsigned n = eina_list_count(paths);
    if (!n)
        return;
    char text[PATH_MAX + 256];
    if (n == 1)
        snprintf(text, sizeof text, one, base_name(eina_list_data_get(paths)));
    else
        snprintf(text, sizeof text, many, n);
    Confirm *c = calloc(1, sizeof *c);
    c->paths = paths_dup(paths);
    c->op = op;
    nv_request(title, text, buttons, confirmed, c);
}

void nv_cmd_delete(const Eina_List *paths)
{
    ask_first(NV_OP_DELETE, paths, _("Do you really want to delete\n%s ?"),
              _("Do you really want to delete\nselected items (%u) ?"), _("Deleting..."), _("*_Delete|_Cancel"));
}

void nv_cmd_trash(const Eina_List *paths)
{
    ask_first(NV_OP_TRASH, paths, _("Do you really want to trash\n%s ?"),
              _("Do you really want to trash\nselected items (%u) ?"), _("Moving to Trashcan..."),
              _("*_Trash|_Cancel"));
}

void nv_cmd_restore(const Eina_List *paths)
{
    nv_file_op(NV_OP_RESTORE, paths_dup(paths), NULL);
}

static void empty_confirmed(void *data, int choice)
{
    (void)data;
    if (choice == 0)
        nv_file_op(NV_OP_EMPTY_TRASH, NULL, NULL);
}

void nv_cmd_empty_trash(void)
{
    nv_request(_("Empty Trashcan"), _("Do you really want to delete\neverything in the trashcan?"),
               _("*_Empty|_Cancel"), empty_confirmed, NULL);
}

/* ---------------------------------------------------------------- rename, new drawer */

static void renamed(void *data, int choice, const char *text)
{
    char *path = data;
    if (choice >= 0 && text && *text && strcmp(text, base_name(path))) {
        char to[PATH_MAX * 2], dir[PATH_MAX];
        snprintf(dir, sizeof dir, "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash)
            *slash = 0;
        snprintf(to, sizeof to, "%s/%s", dir, text);
        struct stat st;
        char msg[PATH_MAX * 2 + 128];
        if (strchr(text, '/')) {
            snprintf(msg, sizeof msg, _("'%s' can't be renamed.\n\n%s"), base_name(path), _("A name has no \"/\"."));
            nv_message(_("Rename"), msg);
        } else if (lstat(to, &st) == 0) {
            snprintf(msg, sizeof msg, _("'%s' can't be renamed.\n\n%s"), base_name(path), strerror(EEXIST));
            nv_message(_("Rename"), msg);
        } else if (rename(path, to) != 0) {
            snprintf(msg, sizeof msg, _("'%s' can't be renamed.\n\n%s"), base_name(path), strerror(errno));
            nv_message(_("Rename"), msg);
        } else {
            nv_listers_refresh();
        }
    }
    free(path);
}

void nv_cmd_rename(const char *path)
{
    if (!path)
        return;
    nv_request_string(_("Rename"), _("New name:"), base_name(path), _("*_Rename|_Cancel"), true, renamed,
                      strdup(path));
}

static void drawer_made(void *data, int choice, const char *text)
{
    char *dir = data;
    if (choice >= 0 && text && *text) {
        char path[PATH_MAX * 2];
        snprintf(path, sizeof path, "%s/%s", dir, text);
        if (strchr(text, '/') || mkdir(path, 0777) != 0) {
            char msg[PATH_MAX * 2 + 128];
            snprintf(msg, sizeof msg, _("Couldn't create directory '%s'.\n\n%s"), text,
                     strchr(text, '/') ? _("A name has no \"/\".") : strerror(errno));
            nv_message(_("New Drawer"), msg);
        } else {
            nv_listers_refresh();
        }
    }
    free(dir);
}

void nv_cmd_new_drawer(const char *dir)
{
    if (!dir)
        return;
    nv_request_string(_("New Drawer"), _("Directory Name:"), _("Unnamed"), _("*_Create|_Cancel"), false, drawer_made,
                      strdup(dir));
}

/* ---------------------------------------------------------------- copy to, move to */

typedef struct Dest {
    Nv_Window *w;
    Evas_Object *fs;
    Eina_List *paths;
    Nv_File_Op op;
} Dest;

static void dest_closed(void *data, Nv_Window *w)
{
    Dest *d = data;
    nv_paths_free(d->paths);
    evas_object_del(w->win);
    free(d);
}

static void dest_done(void *data, Evas_Object *o, void *event)
{
    (void)o;
    Dest *d = data;
    const char *chosen = event;
    if (chosen && *chosen) {
        nv_file_op(d->op, d->paths, chosen);
        d->paths = NULL;
    }
    nv_window_close(d->w);
}

static void choose_dest(const Eina_List *paths, Nv_File_Op op)
{
    if (!paths)
        return;
    Dest *d = calloc(1, sizeof *d);
    d->paths = paths_dup(paths);
    d->op = op;
    d->w = nv_window_new("nirvana-destination", op == NV_OP_COPY ? _("Copy To...") : _("Move To..."), 520, 420);
    d->w->on_close = dest_closed;
    d->w->data = d;
    d->fs = elm_fileselector_add(d->w->win);
    elm_fileselector_folder_only_set(d->fs, EINA_TRUE);
    elm_fileselector_is_save_set(d->fs, EINA_FALSE);
    elm_fileselector_expandable_set(d->fs, EINA_FALSE);
    char start[PATH_MAX];
    snprintf(start, sizeof start, "%s", (const char *)eina_list_data_get(paths));
    char *slash = strrchr(start, '/');
    if (slash && slash != start)
        *slash = 0;
    elm_fileselector_path_set(d->fs, start);
    evas_object_size_hint_weight_set(d->fs, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(d->fs, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_smart_callback_add(d->fs, "done", dest_done, d);
    nv_window_content_set(d->w, d->fs);
    evas_object_show(d->fs);
    nv_window_show(d->w);
}

void nv_cmd_copy_to(const Eina_List *paths)
{
    choose_dest(paths, NV_OP_COPY);
}

void nv_cmd_move_to(const Eina_List *paths)
{
    choose_dest(paths, NV_OP_MOVE);
}
