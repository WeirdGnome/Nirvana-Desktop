/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: pull-down menus.
 *
 * A menu is a model (Nv_Menu, Nv_Menu_Item) - the desktop's own or a program's, read
 * through D-Bus - shown as panels made of the theme's nirvana/menu/... groups. The panels
 * are put where a host says (menu_host.h): into the screen bar's window, which grows
 * over the screen while a menu is open. The pointer and the keyboard are followed by hand
 * (hit tests in screen coordinates): hover selects, a submenu opens beside its item, a release or a click
 * on an item chooses it, Escape and the arrow keys work as usual.
 */
#include "desktop.h"
#include "menu_host.h"

#include <string.h>

/* ---------------------------------------------------------------- the model */

Nv_Menu *nv_menu_new(const char *title)
{
    Nv_Menu *m = calloc(1, sizeof *m);
    m->title = strdup(title ? title : "");
    return m;
}

static Nv_Menu_Item *item_new(Nv_Menu *m, Nv_Item_Kind kind, const char *label)
{
    Nv_Menu_Item *it = calloc(1, sizeof *it);
    it->kind = kind;
    it->label = label ? strdup(label) : NULL;
    it->enabled = true;
    m->items = eina_list_append(m->items, it);
    return it;
}

Nv_Menu_Item *nv_menu_add(Nv_Menu *m, const char *label, const char *shortcut, Nv_Menu_Cb cb, void *data)
{
    Nv_Menu_Item *it = item_new(m, NV_ITEM_NORMAL, label);
    it->shortcut = shortcut ? strdup(shortcut) : NULL;
    it->cb = cb;
    it->data = data;
    it->enabled = cb != NULL;
    return it;
}

Nv_Menu_Item *nv_menu_add_check(Nv_Menu *m, const char *label, bool checked, Nv_Menu_Cb cb, void *data)
{
    Nv_Menu_Item *it = nv_menu_add(m, label, NULL, cb, data);
    it->kind = NV_ITEM_CHECK;
    it->checked = checked;
    return it;
}

Nv_Menu_Item *nv_menu_add_submenu(Nv_Menu *m, const char *label, Nv_Menu *sub)
{
    Nv_Menu_Item *it = item_new(m, NV_ITEM_NORMAL, label);
    it->submenu = sub;
    return it;
}

void nv_menu_add_separator(Nv_Menu *m)
{
    item_new(m, NV_ITEM_SEPARATOR, NULL);
}

void nv_menu_clear(Nv_Menu *m)
{
    Nv_Menu_Item *it;
    EINA_LIST_FREE(m->items, it) {
        if (it->submenu)
            nv_menu_free(it->submenu);
        free(it->label);
        free(it->shortcut);
        free(it);
    }
}

void nv_menu_free(Nv_Menu *m)
{
    if (!m)
        return;
    nv_menu_clear(m);
    free(m->title);
    free(m);
}

/* "_Open" -> "Open" (the mnemonic's letter in *key); "__" -> "_" */
static void plain_label(const char *in, char *out, size_t len, char *key)
{
    size_t n = 0;
    if (key)
        *key = 0;
    for (const char *p = in ? in : ""; *p && n + 1 < len; p++) {
        if (*p == '_') {
            if (p[1] == '_') {
                out[n++] = '_';
                p++;
                continue;
            }
            if (p[1] && key && !*key)
                *key = p[1];
            continue;
        }
        out[n++] = *p;
    }
    out[n] = 0;
}

/* ---------------------------------------------------------------- panels */

typedef struct Panel {
    Nv_Menu *menu;
    Nv_Menu_Item *parent_item; /* in the panel before */
    Evas_Object *host;         /* where it is (a window) */
    Evas_Object *bg, *box;
    Eina_List *rows;           /* Evas_Object per item, in order */
    int x, y, w, h;            /* screen coordinates */
    int sel;                   /* the selected row, -1: none */
} Panel;

struct Nv_Menu_Session {
    const Nv_Menu_Host *host;
    void *host_data;
    Eina_List *panels; /* Panel, the first the top menu */
    Ecore_Timer *submenu_timer;
    Panel *hover;      /* the panel the pointer was last over */
    bool press_seen;   /* a press inside since opening (a release then chooses) */
    double opened_at;
    void (*done)(void *data);
    void *done_data;
};

static Nv_Menu_Item *row_item(Panel *p, int i)
{
    return eina_list_nth(p->menu->items, (unsigned)i);
}

static void panel_select(Panel *p, int i)
{
    if (p->sel == i)
        return;
    if (p->sel >= 0) {
        Evas_Object *o = eina_list_nth(p->rows, (unsigned)p->sel);
        if (o)
            edje_object_signal_emit(o, "nirvana,state,unselected", "nirvana");
    }
    p->sel = i;
    if (i >= 0) {
        Evas_Object *o = eina_list_nth(p->rows, (unsigned)i);
        if (o)
            edje_object_signal_emit(o, "nirvana,state,selected", "nirvana");
    }
}

/* a row's smallest size: its parts' and the group's own min */
static void row_min(Evas_Object *o, int *w, int *h)
{
    int cw = 0, ch = 0, gw = 0, gh = 0;
    edje_object_size_min_calc(o, &cw, &ch);
    edje_object_size_min_get(o, &gw, &gh);
    *w = cw > gw ? cw : gw;
    *h = ch > gh ? ch : gh;
}

static Panel *panel_open(Nv_Menu_Session *s, Nv_Menu *m, Nv_Menu_Item *parent_item, int x, int y, int alt_x)
{
    if (m->about_to_show)
        m->about_to_show(m->about_data, m);
    Panel *p = calloc(1, sizeof *p);
    p->menu = m;
    p->parent_item = parent_item;
    p->sel = -1;

    /* the rows made first in a scratch place, to know the panel's size */
    Evas_Object *probe = s->host->probe_canvas(s->host_data);
    int w = 120, h = 0;
    Eina_List *l;
    Nv_Menu_Item *it;
    Eina_List *sizes = NULL;
    EINA_LIST_FOREACH(m->items, l, it) {
        Evas_Object *o = nv_edje_add(probe, it->kind == NV_ITEM_SEPARATOR ? "nirvana/menu/separator" : "nirvana/menu/item");
        int mw = 0, mh = 0;
        if (o && it->kind != NV_ITEM_SEPARATOR) {
            char lab[512];
            plain_label(it->label, lab, sizeof lab, NULL);
            edje_object_part_text_set(o, "nirvana.text.label", lab);
            edje_object_part_text_set(o, "nirvana.text.shortcut", it->shortcut ? it->shortcut : "");
            if (it->submenu)
                edje_object_signal_emit(o, "nirvana,state,submenu", "nirvana");
            edje_object_message_signal_process(o);
        }
        if (o) {
            row_min(o, &mw, &mh);
            evas_object_del(o);
        }
        if (it->kind != NV_ITEM_SEPARATOR && it->shortcut)
            mw += 24;
        if (mw + 16 > w)
            w = mw + 16;
        h += mh;
        sizes = eina_list_append(sizes, (void *)(intptr_t)mh);
    }
    eina_list_free(sizes);
    h += 10; /* the background's own border */
    w += 4;

    /* placed on the screen: shifted in from the right edge, or to the other side */
    if (x + w > nv.screen_w)
        x = alt_x >= 0 ? alt_x - w : nv.screen_w - w;
    if (x < 0)
        x = 0;
    if (y + h > nv.screen_h)
        y = nv.screen_h - h > nv.bar_h ? nv.screen_h - h : nv.bar_h;
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;

    int ox = 0, oy = 0;
    p->host = s->host->panel_new(s->host_data, x, y, w, h, &ox, &oy);
    p->bg = nv_edje_add(p->host, "nirvana/menu/bg");
    p->box = elm_box_add(p->host);
    elm_box_homogeneous_set(p->box, EINA_FALSE);
    evas_object_size_hint_weight_set(p->box, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(p->box, EVAS_HINT_FILL, 0.0);
    EINA_LIST_FOREACH(m->items, l, it) {
        Evas_Object *o = nv_edje_add(p->host, it->kind == NV_ITEM_SEPARATOR ? "nirvana/menu/separator" : "nirvana/menu/item");
        if (it->kind != NV_ITEM_SEPARATOR) {
            char lab[512];
            plain_label(it->label, lab, sizeof lab, NULL);
            edje_object_part_text_set(o, "nirvana.text.label", lab);
            edje_object_part_text_set(o, "nirvana.text.shortcut", it->shortcut ? it->shortcut : "");
            if (it->submenu)
                edje_object_signal_emit(o, "nirvana,state,submenu", "nirvana");
            if (!it->enabled && !it->submenu)
                edje_object_signal_emit(o, "nirvana,state,disabled", "nirvana");
            if (it->kind != NV_ITEM_NORMAL && it->checked)
                edje_object_signal_emit(o, "nirvana,state,checked", "nirvana");
        }
        int mw, mh;
        row_min(o, &mw, &mh);
        evas_object_size_hint_min_set(o, 0, mh);
        evas_object_size_hint_weight_set(o, EVAS_HINT_EXPAND, 0);
        evas_object_size_hint_align_set(o, EVAS_HINT_FILL, 0.0);
        evas_object_pass_events_set(o, EINA_TRUE);
        elm_box_pack_end(p->box, o);
        evas_object_show(o);
        p->rows = eina_list_append(p->rows, o);
    }
    edje_object_part_swallow(p->bg, "nirvana.swallow.content", p->box);
    evas_object_move(p->bg, ox, oy);
    evas_object_resize(p->bg, w, h);
    evas_object_layer_set(p->bg, 100);
    evas_object_show(p->bg);
    s->panels = eina_list_append(s->panels, p);
    return p;
}

static void panel_close(Nv_Menu_Session *s, Panel *p)
{
    if (s->hover == p)
        s->hover = NULL;
    s->panels = eina_list_remove(s->panels, p);
    eina_list_free(p->rows);
    /* (an Edje object does not delete what it swallows: the box - and with it the rows -
     * goes first, or its texts stay on a host that outlives the panel) */
    evas_object_del(p->box);
    evas_object_del(p->bg);
    s->host->panel_del(s->host_data, p->host);
    free(p);
}

/* the panels after p closed */
static void close_after(Nv_Menu_Session *s, Panel *p)
{
    while (eina_list_last_data_get(s->panels) != p && s->panels) {
        Panel *last = eina_list_last_data_get(s->panels);
        panel_close(s, last);
    }
}

/* the row of p at screen (x, y), -1 for none */
static int row_at(Panel *p, int x, int y)
{
    if (x < p->x || x >= p->x + p->w || y < p->y || y >= p->y + p->h)
        return -1;
    Eina_List *l;
    Evas_Object *o;
    int i = 0, px, py;
    evas_object_geometry_get(p->bg, &px, &py, NULL, NULL);
    EINA_LIST_FOREACH(p->rows, l, o) {
        int ox, oy, ow, oh;
        evas_object_geometry_get(o, &ox, &oy, &ow, &oh);
        int sy = p->y + (oy - py);
        if (y >= sy && y < sy + oh)
            return i;
        i++;
    }
    return -1;
}

static Panel *panel_at(Nv_Menu_Session *s, int x, int y)
{
    Eina_List *l;
    Panel *p;
    EINA_LIST_REVERSE_FOREACH(s->panels, l, p)
        if (x >= p->x && x < p->x + p->w && y >= p->y && y < p->y + p->h)
            return p;
    return NULL;
}

static void open_submenu_of(Nv_Menu_Session *s, Panel *p)
{
    close_after(s, p);
    Nv_Menu_Item *it = p->sel >= 0 ? row_item(p, p->sel) : NULL;
    if (!it || !it->submenu || !it->enabled)
        return;
    Evas_Object *o = eina_list_nth(p->rows, (unsigned)p->sel);
    int px, py, oy;
    evas_object_geometry_get(p->bg, &px, &py, NULL, NULL);
    evas_object_geometry_get(o, NULL, &oy, NULL, NULL);
    Panel *sub = panel_open(s, it->submenu, it, p->x + p->w - 4, p->y + (oy - py) - 4, p->x + 4);
    (void)sub;
}

static Eina_Bool submenu_timer_cb(void *data)
{
    Nv_Menu_Session *s = data;
    s->submenu_timer = NULL;
    Panel *p = s->hover;
    if (!p || !eina_list_data_find(s->panels, p))
        return ECORE_CALLBACK_CANCEL;
    Nv_Menu_Item *it = p->sel >= 0 ? row_item(p, p->sel) : NULL;
    Eina_List *me = eina_list_data_find_list(s->panels, p);
    Panel *next = me && eina_list_next(me) ? eina_list_data_get(eina_list_next(me)) : NULL;
    if (it && it->submenu && it->enabled) {
        if (!next || next->menu != it->submenu)
            open_submenu_of(s, p);
    } else if (next) {
        close_after(s, p);
    }
    return ECORE_CALLBACK_CANCEL;
}

/* ---------------------------------------------------------------- the session */

Nv_Menu_Session *nv_menu_session_open(const Nv_Menu_Host *host, void *host_data, Nv_Menu *m, int x, int y,
                                      void (*done)(void *data), void *done_data)
{
    Nv_Menu_Session *s = calloc(1, sizeof *s);
    s->host = host;
    s->host_data = host_data;
    s->done = done;
    s->done_data = done_data;
    s->opened_at = ecore_time_get();
    panel_open(s, m, NULL, x, y, -1);
    return s;
}

void nv_menu_session_close(Nv_Menu_Session *s)
{
    if (!s)
        return;
    if (s->submenu_timer)
        ecore_timer_del(s->submenu_timer);
    while (s->panels)
        panel_close(s, eina_list_data_get(s->panels));
    void (*done)(void *) = s->done;
    void *data = s->done_data;
    free(s);
    if (done)
        done(data);
}

Nv_Menu *nv_menu_session_menu(Nv_Menu_Session *s)
{
    Panel *p = s ? eina_list_data_get(s->panels) : NULL;
    return p ? p->menu : NULL;
}

/* the item chosen: the session closed first (the callback may open windows, or menus) */
static void choose(Nv_Menu_Session *s, Nv_Menu_Item *it)
{
    if (!it || !it->enabled || it->submenu || it->kind == NV_ITEM_SEPARATOR)
        return;
    Nv_Menu_Cb cb = it->cb;
    void *data = it->data;
    if (it->kind == NV_ITEM_CHECK)
        it->checked = !it->checked;
    else if (it->kind == NV_ITEM_RADIO)
        it->checked = true;
    nv_menu_session_close(s);
    if (cb)
        cb(data, it);
}

void nv_menu_session_motion(Nv_Menu_Session *s, int x, int y)
{
    Panel *p = panel_at(s, x, y);
    if (!p) {
        /* off the menus: the deepest panel loses its selection unless its submenu is open */
        Panel *last = eina_list_last_data_get(s->panels);
        if (last) {
            Nv_Menu_Item *it = last->sel >= 0 ? row_item(last, last->sel) : NULL;
            if (!it || !it->submenu)
                panel_select(last, -1);
        }
        return;
    }
    int r = row_at(p, x, y);
    Nv_Menu_Item *it = r >= 0 ? row_item(p, r) : NULL;
    if (it && it->kind == NV_ITEM_SEPARATOR)
        r = -1;
    s->hover = p;
    if (r == p->sel)
        return;
    panel_select(p, r);
    if (s->submenu_timer)
        ecore_timer_del(s->submenu_timer);
    s->submenu_timer = ecore_timer_add(0.18, submenu_timer_cb, s);
}

/* a press: true when it was on a menu (else the caller closes the session) */
bool nv_menu_session_press(Nv_Menu_Session *s, int x, int y)
{
    Panel *p = panel_at(s, x, y);
    if (p)
        s->press_seen = true;
    return p != NULL;
}

/* a release: on an item it chooses it; true when the session is still open */
bool nv_menu_session_release(Nv_Menu_Session *s, int x, int y, bool *closed)
{
    *closed = false;
    Panel *p = panel_at(s, x, y);
    if (!p)
        return true;
    int r = row_at(p, x, y);
    if (r < 0)
        return true;
    Nv_Menu_Item *it = row_item(p, r);
    if (it && it->submenu) {
        panel_select(p, r);
        open_submenu_of(s, p);
        return true;
    }
    /* a release just after opening (the click that opened the menu) does nothing */
    if (!s->press_seen && ecore_time_get() - s->opened_at < 0.25)
        return true;
    if (it && it->enabled && it->kind != NV_ITEM_SEPARATOR) {
        *closed = true;
        choose(s, it);
        return false;
    }
    return true;
}

static int next_row(Panel *p, int from, int dir)
{
    int n = (int)eina_list_count(p->menu->items);
    if (!n)
        return -1;
    int i = from;
    for (int k = 0; k < n; k++) {
        i = (i + dir + n) % n;
        if (from < 0 && k == 0)
            i = dir > 0 ? 0 : n - 1;
        Nv_Menu_Item *it = row_item(p, i);
        if (it && it->kind != NV_ITEM_SEPARATOR)
            return i;
    }
    return -1;
}

/* a key; returns: 0 handled, 1 the session closed, -1 Left on the top menu, +1 Right
 * on an item without a submenu (the bar moves to the menu beside) - see menu_host.h */
int nv_menu_session_key(Nv_Menu_Session *s, const char *key, const char *string)
{
    Panel *p = eina_list_last_data_get(s->panels);
    if (!p)
        return NV_MENU_KEY_CLOSED;
    if (!strcmp(key, "Escape")) {
        if (eina_list_count(s->panels) > 1) {
            panel_close(s, p);
            return NV_MENU_KEY_HANDLED;
        }
        nv_menu_session_close(s);
        return NV_MENU_KEY_CLOSED;
    }
    if (!strcmp(key, "Down") || !strcmp(key, "Up")) {
        panel_select(p, next_row(p, p->sel, !strcmp(key, "Down") ? 1 : -1));
        return NV_MENU_KEY_HANDLED;
    }
    if (!strcmp(key, "Home") || !strcmp(key, "End")) {
        panel_select(p, next_row(p, -1, !strcmp(key, "Home") ? 1 : -1));
        return NV_MENU_KEY_HANDLED;
    }
    if (!strcmp(key, "Right")) {
        Nv_Menu_Item *it = p->sel >= 0 ? row_item(p, p->sel) : NULL;
        if (it && it->submenu) {
            open_submenu_of(s, p);
            Panel *sub = eina_list_last_data_get(s->panels);
            if (sub != p)
                panel_select(sub, next_row(sub, -1, 1));
            return NV_MENU_KEY_HANDLED;
        }
        return NV_MENU_KEY_NEXT;
    }
    if (!strcmp(key, "Left")) {
        if (eina_list_count(s->panels) > 1) {
            panel_close(s, p);
            return NV_MENU_KEY_HANDLED;
        }
        return NV_MENU_KEY_PREV;
    }
    if (!strcmp(key, "Return") || !strcmp(key, "KP_Enter") || !strcmp(key, "space")) {
        Nv_Menu_Item *it = p->sel >= 0 ? row_item(p, p->sel) : NULL;
        if (it && it->submenu) {
            open_submenu_of(s, p);
            Panel *sub = eina_list_last_data_get(s->panels);
            if (sub != p)
                panel_select(sub, next_row(sub, -1, 1));
            return NV_MENU_KEY_HANDLED;
        }
        if (it && it->enabled) {
            choose(s, it);
            return NV_MENU_KEY_CLOSED;
        }
        return NV_MENU_KEY_HANDLED;
    }
    /* a mnemonic letter */
    if (string && string[0] && !string[1]) {
        Eina_List *l;
        Nv_Menu_Item *it;
        int i = 0;
        EINA_LIST_FOREACH(p->menu->items, l, it) {
            char lab[512], mk = 0;
            plain_label(it->label, lab, sizeof lab, &mk);
            if (mk && (mk | 0x20) == (string[0] | 0x20) && it->kind != NV_ITEM_SEPARATOR) {
                panel_select(p, i);
                if (it->submenu) {
                    open_submenu_of(s, p);
                    return NV_MENU_KEY_HANDLED;
                }
                choose(s, it);
                return NV_MENU_KEY_CLOSED;
            }
            i++;
        }
    }
    return NV_MENU_KEY_HANDLED;
}

/* the first item selected (a menu opened from the keyboard) */
void nv_menu_session_select_first(Nv_Menu_Session *s)
{
    Panel *p = eina_list_data_get(s->panels);
    if (p)
        panel_select(p, next_row(p, -1, 1));
}
