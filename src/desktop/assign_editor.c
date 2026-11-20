/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the Assigns editor (Nirvana Screen -> Assigns...).
 *
 * Left: the assigns (a multi-assign's paths counted, the desktop icon marked) with New,
 * Clone and Delete. Right: the chosen assign's name, "Show volume icon on Desktop", its
 * paths in search order with Add Path (a file selector), Remove Path, Move Up, Move Down
 * and Open in Lister. Below: Use (the session's assigns changed, the desktop's icons
 * made again), Save (the same, and the assigns written to ~/.nirvana/Startup-Sequence so
 * that they come back at the next session) and Cancel.
 *
 * The editor works on a copy; nothing changes until Use or Save.
 */
#include "desktop.h"

#include <errno.h>
#include <string.h>
#include <strings.h>

typedef struct Ed_Assign {
    char name[NIRVANA_NAME_MAX];
    char orig[NIRVANA_NAME_MAX]; /* its name in the database ("" for a new one) */
    bool desktop;
    Eina_List *paths;            /* char * */
    bool modified;
    Elm_Object_Item *item;
} Ed_Assign;

static struct {
    Nv_Window *w;
    Evas_Object *list, *name, *desktop, *paths, *inwin;
    Eina_List *assigns;  /* Ed_Assign */
    Eina_List *deleted;  /* char *: names in the database to remove */
    Ed_Assign *cur;
    bool loading;        /* the right pane being filled (no "changed" then) */
} ed;

static Elm_Genlist_Item_Class *assign_itc, *path_itc;

/* ---------------------------------------------------------------- the model */

static Ed_Assign *ed_new(const char *name)
{
    Ed_Assign *a = calloc(1, sizeof *a);
    snprintf(a->name, sizeof a->name, "%s", name);
    return a;
}

static void ed_free(Ed_Assign *a)
{
    char *p;
    EINA_LIST_FREE(a->paths, p)
        free(p);
    free(a);
}

static void ed_clear(void)
{
    Ed_Assign *a;
    EINA_LIST_FREE(ed.assigns, a)
        ed_free(a);
    char *n;
    EINA_LIST_FREE(ed.deleted, n)
        free(n);
    ed.cur = NULL;
}

static void ed_load(void)
{
    ed_clear();
    nirvana_assign_list l;
    if (nirvana_assign_list_load(&l) < 0)
        return;
    for (size_t i = 0; i < l.count; i++) {
        Ed_Assign *a = ed_new(l.items[i].name);
        snprintf(a->orig, sizeof a->orig, "%s", l.items[i].name);
        a->desktop = l.items[i].desktop;
        for (size_t j = 0; j < l.items[i].npaths; j++)
            a->paths = eina_list_append(a->paths, strdup(l.items[i].paths[j]));
        ed.assigns = eina_list_append(ed.assigns, a);
    }
    nirvana_assign_list_free(&l);
}

/* ---------------------------------------------------------------- the lists */

static char *assign_text(void *data, Evas_Object *o, const char *part)
{
    (void)o;
    Ed_Assign *a = data;
    char buf[PATH_MAX + 128];
    if (!strcmp(part, "elm.text")) {
        snprintf(buf, sizeof buf, "%s:%s", a->name, a->modified ? "  *" : "");
        return strdup(buf);
    }
    if (!strcmp(part, "elm.text.sub")) {
        unsigned n = eina_list_count(a->paths);
        const char *first = n ? eina_list_data_get(a->paths) : _("(no path)");
        if (n > 1)
            snprintf(buf, sizeof buf, _("%s  (+%u more)"), first, n - 1);
        else
            snprintf(buf, sizeof buf, "%s", first);
        if (a->desktop) {
            size_t k = strlen(buf);
            snprintf(buf + k, sizeof buf - k, "  %s", _("[desktop]"));
        }
        return strdup(buf);
    }
    return NULL;
}

static char *path_text(void *data, Evas_Object *o, const char *part)
{
    (void)o;
    (void)part;
    return strdup(data);
}

static void fill_paths(void)
{
    elm_genlist_clear(ed.paths);
    if (!ed.cur)
        return;
    Eina_List *l;
    char *p;
    EINA_LIST_FOREACH(ed.cur->paths, l, p)
        elm_genlist_item_append(ed.paths, path_itc, p, NULL, ELM_GENLIST_ITEM_NONE, NULL, NULL);
}

static void fill_right(void)
{
    ed.loading = true;
    bool on = ed.cur != NULL;
    elm_object_disabled_set(ed.name, !on);
    elm_object_disabled_set(ed.desktop, !on);
    elm_entry_entry_set(ed.name, on ? ed.cur->name : "");
    elm_check_state_set(ed.desktop, on && ed.cur->desktop);
    fill_paths();
    ed.loading = false;
}

static void touched(Ed_Assign *a)
{
    a->modified = true;
    if (a->item)
        elm_genlist_item_update(a->item);
}

static void on_assign_selected(void *data, Evas_Object *o, void *event)
{
    (void)data;
    (void)o;
    ed.cur = elm_object_item_data_get(event);
    fill_right();
}

static Elm_Object_Item *list_add(Ed_Assign *a)
{
    a->item = elm_genlist_item_append(ed.list, assign_itc, a, NULL, ELM_GENLIST_ITEM_NONE, on_assign_selected, NULL);
    return a->item;
}

static void fill_list(void)
{
    elm_genlist_clear(ed.list);
    Eina_List *l;
    Ed_Assign *a;
    EINA_LIST_FOREACH(ed.assigns, l, a)
        list_add(a);
}

/* ---------------------------------------------------------------- the left pane */

static Ed_Assign *find(const char *name, Ed_Assign *except)
{
    Eina_List *l;
    Ed_Assign *a;
    EINA_LIST_FOREACH(ed.assigns, l, a)
        if (a != except && !strcasecmp(a->name, name))
            return a;
    return NULL;
}

static void unique_name(const char *base, char *out, size_t len)
{
    snprintf(out, len, "%s", base);
    for (int i = 2; find(out, NULL) && i < 1000; i++)
        snprintf(out, len, "%s%d", base, i);
}

static void on_new(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)o;
    (void)ev;
    char name[NIRVANA_NAME_MAX];
    unique_name(_("New"), name, sizeof name);
    Ed_Assign *a = ed_new(name);
    a->modified = true;
    ed.assigns = eina_list_append(ed.assigns, a);
    elm_genlist_item_selected_set(list_add(a), EINA_TRUE);
    elm_object_focus_set(ed.name, EINA_TRUE);
    elm_entry_select_all(ed.name);
}

static void on_clone(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)o;
    (void)ev;
    if (!ed.cur)
        return;
    char name[NIRVANA_NAME_MAX];
    unique_name(ed.cur->name, name, sizeof name);
    Ed_Assign *a = ed_new(name);
    a->desktop = ed.cur->desktop;
    Eina_List *l;
    char *p;
    EINA_LIST_FOREACH(ed.cur->paths, l, p)
        a->paths = eina_list_append(a->paths, strdup(p));
    a->modified = true;
    ed.assigns = eina_list_append(ed.assigns, a);
    elm_genlist_item_selected_set(list_add(a), EINA_TRUE);
}

static void on_delete(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)o;
    (void)ev;
    Ed_Assign *a = ed.cur;
    if (!a)
        return;
    if (*a->orig)
        ed.deleted = eina_list_append(ed.deleted, strdup(a->orig));
    ed.assigns = eina_list_remove(ed.assigns, a);
    Elm_Object_Item *next = elm_genlist_item_next_get(a->item);
    if (!next)
        next = elm_genlist_item_prev_get(a->item);
    elm_object_item_del(a->item);
    ed_free(a);
    ed.cur = NULL;
    if (next)
        elm_genlist_item_selected_set(next, EINA_TRUE);
    else
        fill_right();
}

/* ---------------------------------------------------------------- the right pane */

static void on_name_changed(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)ev;
    if (ed.loading || !ed.cur)
        return;
    char *text = elm_entry_markup_to_utf8(elm_entry_entry_get(o));
    /* a trailing colon is the user's way of writing a name; it is not a part of it */
    size_t n = text ? strlen(text) : 0;
    if (n && text[n - 1] == ':')
        text[--n] = 0;
    if (n && n < NIRVANA_NAME_MAX && !strpbrk(text, ":/;|")) {
        snprintf(ed.cur->name, sizeof ed.cur->name, "%s", text);
        touched(ed.cur);
    }
    free(text);
}

static void on_desktop_changed(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)ev;
    if (ed.loading || !ed.cur)
        return;
    ed.cur->desktop = elm_check_state_get(o);
    touched(ed.cur);
}

static int selected_path(void)
{
    Elm_Object_Item *it = elm_genlist_selected_item_get(ed.paths);
    return it ? elm_genlist_item_index_get(it) - 1 : -1;
}

static void select_path(int i)
{
    Elm_Object_Item *it = elm_genlist_nth_item_get(ed.paths, (unsigned)i);
    if (it) {
        elm_genlist_item_selected_set(it, EINA_TRUE);
        elm_genlist_item_show(it, ELM_GENLIST_ITEM_SCROLLTO_IN);
    }
}

static void fs_done(void *data, Evas_Object *o, void *event)
{
    (void)data;
    (void)o;
    const char *path = event;
    if (path && ed.cur) {
        if (strpbrk(path, ";|")) {
            nv_message(_("Assigns"), _("A path may not hold \";\" or \"|\"."));
        } else {
            ed.cur->paths = eina_list_append(ed.cur->paths, strdup(path));
            touched(ed.cur);
            fill_paths();
            select_path((int)eina_list_count(ed.cur->paths) - 1);
        }
    }
    evas_object_del(ed.inwin);
    ed.inwin = NULL;
}

static void on_add_path(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)o;
    (void)ev;
    if (!ed.cur || ed.inwin)
        return;
    ed.inwin = elm_win_inwin_add(ed.w->win);
    Evas_Object *fs = elm_fileselector_add(ed.w->win);
    elm_fileselector_folder_only_set(fs, EINA_TRUE);
    elm_fileselector_expandable_set(fs, EINA_FALSE);
    elm_fileselector_is_save_set(fs, EINA_FALSE);
    const char *start = ed.cur->paths ? eina_list_last_data_get(ed.cur->paths) : getenv("HOME");
    elm_fileselector_path_set(fs, start ? start : "/");
    evas_object_size_hint_weight_set(fs, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(fs, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_smart_callback_add(fs, "done", fs_done, NULL);
    elm_win_inwin_content_set(ed.inwin, fs);
    evas_object_show(fs);
    elm_win_inwin_activate(ed.inwin);
}

static void on_remove_path(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)o;
    (void)ev;
    int i = selected_path();
    if (!ed.cur || i < 0)
        return;
    Eina_List *node = eina_list_nth_list(ed.cur->paths, (unsigned)i);
    free(eina_list_data_get(node));
    ed.cur->paths = eina_list_remove_list(ed.cur->paths, node);
    touched(ed.cur);
    fill_paths();
    int n = (int)eina_list_count(ed.cur->paths);
    if (n)
        select_path(i < n ? i : n - 1);
}

static void move_path(int dir)
{
    int i = selected_path();
    int n = ed.cur ? (int)eina_list_count(ed.cur->paths) : 0;
    if (i < 0 || i + dir < 0 || i + dir >= n)
        return;
    Eina_List *a = eina_list_nth_list(ed.cur->paths, (unsigned)i);
    Eina_List *b = eina_list_nth_list(ed.cur->paths, (unsigned)(i + dir));
    void *t = eina_list_data_get(a);
    eina_list_data_set(a, eina_list_data_get(b));
    eina_list_data_set(b, t);
    touched(ed.cur);
    fill_paths();
    select_path(i + dir);
}

static void on_up(void *data, Evas_Object *o, void *ev) { (void)data; (void)o; (void)ev; move_path(-1); }
static void on_down(void *data, Evas_Object *o, void *ev) { (void)data; (void)o; (void)ev; move_path(+1); }

static void on_open_in_lister(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)o;
    (void)ev;
    if (!ed.cur || !ed.cur->paths)
        return;
    int i = selected_path();
    const char *p = eina_list_nth(ed.cur->paths, (unsigned)(i < 0 ? 0 : i));
    nv_lister_open(p);
}

/* ---------------------------------------------------------------- Use, Save, Cancel */

static bool check(void)
{
    Eina_List *l;
    Ed_Assign *a;
    char msg[256];
    EINA_LIST_FOREACH(ed.assigns, l, a) {
        if (!a->paths) {
            snprintf(msg, sizeof msg, _("The assign %s: has no path."), a->name);
            nv_message(_("Assigns"), msg);
            elm_genlist_item_selected_set(a->item, EINA_TRUE);
            return false;
        }
        if (find(a->name, a)) {
            snprintf(msg, sizeof msg, _("There are two assigns named %s:."), a->name);
            nv_message(_("Assigns"), msg);
            elm_genlist_item_selected_set(a->item, EINA_TRUE);
            return false;
        }
    }
    return true;
}

/* the changes made to the session's assigns; with save, to the Startup-Sequence too */
static bool apply(bool save)
{
    if (!check())
        return false;
    int r = 0;
    char msg[512];
    Eina_List *l;
    char *n;
    EINA_LIST_FOREACH(ed.deleted, l, n) {
        r = nirvana_assign_delete(n);
        if (r < 0 && r != -ENOENT)
            goto fail;
        if (save)
            nirvana_assign_unpersist(n);
    }
    Ed_Assign *a;
    EINA_LIST_FOREACH(ed.assigns, l, a) {
        bool renamed = *a->orig && strcmp(a->orig, a->name);
        if (renamed) {
            nirvana_assign_delete(a->orig);
            if (save)
                nirvana_assign_unpersist(a->orig);
        }
        if (!a->modified && !renamed)
            continue;
        size_t np = eina_list_count(a->paths), i = 0;
        const char **paths = calloc(np, sizeof *paths);
        Eina_List *pl;
        char *p;
        EINA_LIST_FOREACH(a->paths, pl, p)
            paths[i++] = p;
        r = nirvana_assign_set(a->name, paths, np, a->desktop);
        free(paths);
        if (r < 0)
            goto fail;
        if (save && (r = nirvana_assign_persist(a->name)) < 0)
            goto fail;
    }
    nv_backdrop_reload();
    return true;
fail:
    snprintf(msg, sizeof msg, _("The assigns could not be changed: %s"), strerror(-r));
    nv_message(_("Assigns"), msg);
    nv_backdrop_reload();
    return false;
}

static void close_editor(void)
{
    if (!ed.w)
        return;
    Evas_Object *win = ed.w->win;
    ed.w = NULL;
    elm_genlist_clear(ed.list);
    ed_clear();
    evas_object_del(win);
}

static void on_use(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)o;
    (void)ev;
    if (apply(false))
        close_editor();
}

static void on_save(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)o;
    (void)ev;
    if (apply(true))
        close_editor();
}

static void on_cancel(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)o;
    (void)ev;
    close_editor();
}

static void on_close_gadget(void *data, Nv_Window *w)
{
    (void)data;
    (void)w;
    close_editor();
}

/* ---------------------------------------------------------------- the window */

static Evas_Object *button(Evas_Object *box, const char *label, Evas_Smart_Cb cb)
{
    Evas_Object *b = elm_button_add(ed.w->win);
    elm_object_text_set(b, label);
    evas_object_size_hint_weight_set(b, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(b, EVAS_HINT_FILL, 0.5);
    evas_object_smart_callback_add(b, "clicked", cb, NULL);
    elm_box_pack_end(box, b);
    evas_object_show(b);
    return b;
}

static Evas_Object *hbox(Evas_Object *parent)
{
    Evas_Object *b = elm_box_add(parent);
    elm_box_horizontal_set(b, EINA_TRUE);
    elm_box_homogeneous_set(b, EINA_TRUE);
    elm_box_padding_set(b, 4, 0);
    evas_object_size_hint_weight_set(b, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(b, EVAS_HINT_FILL, 0.5);
    evas_object_show(b);
    return b;
}

static Evas_Object *frame(Evas_Object *parent, const char *title, Evas_Object *content)
{
    Evas_Object *f = elm_frame_add(parent);
    elm_object_text_set(f, title);
    elm_object_content_set(f, content);
    evas_object_size_hint_weight_set(f, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(f, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_show(f);
    return f;
}

void nv_assign_editor_show(void)
{
    if (ed.w) {
        elm_win_raise(ed.w->win);
        elm_win_activate(ed.w->win);
        return;
    }
    if (!assign_itc) {
        assign_itc = elm_genlist_item_class_new();
        assign_itc->item_style = "double_label";
        assign_itc->func.text_get = assign_text;
        path_itc = elm_genlist_item_class_new();
        path_itc->item_style = "default";
        path_itc->func.text_get = path_text;
    }
    ed.w = nv_window_new("nirvana-assigns", _("Assigns"), 760, 460);
    ed.w->on_close = on_close_gadget;
    Evas_Object *win = ed.w->win;

    Evas_Object *outer = elm_box_add(win);
    elm_box_padding_set(outer, 0, 6);
    evas_object_size_hint_weight_set(outer, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

    Evas_Object *panes = elm_panes_add(win);
    elm_panes_content_left_size_set(panes, 0.42);
    evas_object_size_hint_weight_set(panes, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(panes, EVAS_HINT_FILL, EVAS_HINT_FILL);

    /* the left pane: the assigns */
    Evas_Object *left = elm_box_add(win);
    elm_box_padding_set(left, 0, 4);
    ed.list = elm_genlist_add(win);
    elm_genlist_mode_set(ed.list, ELM_LIST_COMPRESS);
    evas_object_size_hint_weight_set(ed.list, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(ed.list, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(left, ed.list);
    evas_object_show(ed.list);
    Evas_Object *lb = hbox(win);
    button(lb, _("New"), on_new);
    button(lb, _("Clone"), on_clone);
    button(lb, _("Delete"), on_delete);
    elm_box_pack_end(left, lb);
    evas_object_show(left);
    elm_object_part_content_set(panes, "left", frame(win, _("Volumes"), left));

    /* the right pane: the chosen assign */
    Evas_Object *right = elm_box_add(win);
    elm_box_padding_set(right, 0, 4);
    Evas_Object *nrow = elm_box_add(win);
    elm_box_horizontal_set(nrow, EINA_TRUE);
    elm_box_padding_set(nrow, 6, 0);
    evas_object_size_hint_weight_set(nrow, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(nrow, EVAS_HINT_FILL, 0.5);
    Evas_Object *lab = elm_label_add(win);
    elm_object_text_set(lab, _("Name:"));
    elm_box_pack_end(nrow, lab);
    evas_object_show(lab);
    ed.name = elm_entry_add(win);
    elm_entry_single_line_set(ed.name, EINA_TRUE);
    elm_entry_scrollable_set(ed.name, EINA_TRUE);
    evas_object_size_hint_weight_set(ed.name, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(ed.name, EVAS_HINT_FILL, 0.5);
    evas_object_smart_callback_add(ed.name, "changed,user", on_name_changed, NULL);
    elm_box_pack_end(nrow, ed.name);
    evas_object_show(ed.name);
    elm_box_pack_end(right, nrow);
    evas_object_show(nrow);

    ed.desktop = elm_check_add(win);
    elm_object_text_set(ed.desktop, _("Show volume icon on Desktop"));
    evas_object_size_hint_align_set(ed.desktop, 0.0, 0.5);
    evas_object_smart_callback_add(ed.desktop, "changed", on_desktop_changed, NULL);
    elm_box_pack_end(right, ed.desktop);
    evas_object_show(ed.desktop);

    Evas_Object *pbox = elm_box_add(win);
    elm_box_padding_set(pbox, 0, 4);
    ed.paths = elm_genlist_add(win);
    elm_genlist_mode_set(ed.paths, ELM_LIST_COMPRESS);
    evas_object_size_hint_weight_set(ed.paths, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(ed.paths, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_pack_end(pbox, ed.paths);
    evas_object_show(ed.paths);
    Evas_Object *pb1 = hbox(win);
    button(pb1, _("Add Path..."), on_add_path);
    button(pb1, _("Remove Path"), on_remove_path);
    elm_box_pack_end(pbox, pb1);
    Evas_Object *pb2 = hbox(win);
    button(pb2, _("Move Up"), on_up);
    button(pb2, _("Move Down"), on_down);
    button(pb2, _("Open in Lister"), on_open_in_lister);
    elm_box_pack_end(pbox, pb2);
    evas_object_show(pbox);
    elm_box_pack_end(right, frame(win, _("Paths (searched in this order)"), pbox));
    evas_object_show(right);
    elm_object_part_content_set(panes, "right", right);

    elm_box_pack_end(outer, panes);
    evas_object_show(panes);

    /* Use, Save, Cancel */
    Evas_Object *bottom = hbox(win);
    button(bottom, _("Save"), on_save);
    button(bottom, _("Use"), on_use);
    button(bottom, _("Cancel"), on_cancel);
    elm_box_pack_end(outer, bottom);

    nv_window_content_set(ed.w, outer);
    evas_object_show(outer);

    ed_load();
    fill_list();
    Elm_Object_Item *first = elm_genlist_first_item_get(ed.list);
    if (first)
        elm_genlist_item_selected_set(first, EINA_TRUE);
    else
        fill_right();
    nv_window_show(ed.w);
}
