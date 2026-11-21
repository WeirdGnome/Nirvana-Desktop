/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the screen bar, "Nirvana Screen".
 *
 * A strip at the top of the screen (a layer surface of the top layer that keeps its
 * height for itself): the screen's title, the menu titles - the desktop's own, or the
 * focused program's (the global menu, stage 3) - the clock and the screen's depth
 * gadget. The menus drop down from it: the bar's surface grows over the screen while a
 * menu is open (the area below the bar transparent, a click there closes the menu).
 *
 * As on the classic desktops the menu titles show while the right mouse button is held
 * (on the bar or on the desktop) - the bar shows the screen's or the program's title
 * otherwise; moving onto a title drops its menu, releasing over an item chooses it.
 * "menus=always" in ~/.nirvana/Prefs/desktop.conf keeps the titles on the bar, to be
 * clicked.
 */
#include "desktop.h"
#include "menu_host.h"

#include <string.h>
#include <strings.h>
#include <time.h>

static struct {
    Evas_Object *win, *edje, *box, *catcher, *probe;
    Eina_List *titles;       /* Evas_Object: the menu titles' edje objects */
    Eina_List *menus;        /* Nv_Menu shown */
    Eina_List *own;          /* the desktop's own menus */
    char *owner;
    int open_index;          /* the open menu, -1: none */
    Nv_Menu_Session *session;
    Ecore_Timer *clock;
    Eina_List *handlers;
    bool closing;
    bool fresh;              /* a popup opened by the press being handled now */
    bool classic;            /* the titles only while the right button is held */
    bool strip;              /* the titles are shown */
    bool holding;            /* the right button is held */
    bool expanded;           /* the bar is over the screen (menus open) */
    char title[256];         /* the title shown when the titles are not */
} bar = {.open_index = -1};

static void titles_rebuild(void);
static void title_show(void);

/* ---------------------------------------------------------------- the menu host */

static Evas_Object *host_probe(void *data)
{
    (void)data;
    return bar.win;
}

static Evas_Object *host_panel_new(void *data, int x, int y, int w, int h, int *ox, int *oy)
{
    (void)data;
    (void)w;
    (void)h;
    /* (the panels are drawn in the bar, grown over the screen) */
    *ox = x;
    *oy = y;
    return bar.win;
}

static void host_panel_del(void *data, Evas_Object *host)
{
    (void)data;
    if (host != bar.win)
        evas_object_del(host);
}

static const Nv_Menu_Host host = {host_probe, host_panel_new, host_panel_del};

/* ---------------------------------------------------------------- menus open and shut */

static void title_select(int index)
{
    Eina_List *l;
    Evas_Object *o;
    int i = 0;
    EINA_LIST_FOREACH(bar.titles, l, o) {
        edje_object_signal_emit(o, i == index ? "nirvana,state,selected" : "nirvana,state,unselected", "nirvana");
        i++;
    }
}

bool nv_screenbar_menu_is_open(void)
{
    return bar.session != NULL;
}

/* the bar over the screen (the menus are drawn in it), or back */
static void expand(bool with_keyboard)
{
    if (bar.expanded)
        return;
    bar.expanded = true;
    nv_layer_resize(bar.win, 0, nv.screen_h);
    nv_layer_keyboard(bar.win, true);
    (void)with_keyboard;
    evas_object_move(bar.catcher, 0, nv.bar_h);
    evas_object_resize(bar.catcher, nv.screen_w, nv.screen_h);
    evas_object_show(bar.catcher);
}

static void shrink(void)
{
    if (!bar.expanded)
        return;
    bar.expanded = false;
    evas_object_hide(bar.catcher);
    nv_layer_keyboard(bar.win, false);
    nv_layer_resize(bar.win, 0, nv.bar_h);
}

/* the titles shown (classic: only while the right button is held) */
static void strip_show(bool on)
{
    if (!bar.classic || bar.strip == on)
        return;
    bar.strip = on;
    titles_rebuild();
    title_show();
}

static void session_done(void *data)
{
    (void)data;
    bar.session = NULL;
    if (bar.closing)
        return; /* another menu is being opened in its place */
    nv_global_menu_closed();
    bar.open_index = -1;
    title_select(-1);
    if (bar.holding)
        return; /* the strip stays while the button is held */
    strip_show(false);
    shrink();
}

void nv_screenbar_menu_close(void)
{
    if (bar.session)
        nv_menu_session_close(bar.session);
}

void nv_screenbar_menu_open(int index)
{
    int n = (int)eina_list_count(bar.menus);
    if (n == 0)
        return;
    index = (index % n + n) % n;
    if (bar.session) {
        if (index == bar.open_index)
            return;
        bar.closing = true;
        nv_menu_session_close(bar.session);
        bar.closing = false;
    } else {
        /* the bar grows over the screen: the panels go into it */
        strip_show(true);
        expand(!bar.holding);
    }
    bar.open_index = index;
    title_select(index);
    Evas_Object *t = eina_list_nth(bar.titles, (unsigned)index);
    int tx = 0;
    evas_object_geometry_get(t, &tx, NULL, NULL, NULL);
    bar.session = nv_menu_session_open(&host, NULL, eina_list_nth(bar.menus, (unsigned)index), tx, nv.bar_h - 1,
                                       session_done, NULL);
}

static void fresh_over(void *data)
{
    (void)data;
    bar.fresh = false;
}

/* a menu at a place of the screen (a dock's or an icon's menu), shown by the bar */
void nv_screenbar_popup(Nv_Menu *m, int x, int y)
{
    if (bar.session) {
        bar.closing = true;
        nv_menu_session_close(bar.session);
        bar.closing = false;
    } else {
        expand(true);
    }
    bar.open_index = -1;
    title_select(-1);
    bar.session = nv_menu_session_open(&host, NULL, m, x, y, session_done, NULL);
    /* the press that opened it (a dock's, an icon's) reaches the bar's handler too: it
     * must not shut the popup */
    bar.fresh = true;
    ecore_job_add(fresh_over, NULL);
}

/* the title under x (bar coordinates), -1 for none */
static int title_at(int x, int y)
{
    if (y < 0 || y >= nv.bar_h)
        return -1;
    Eina_List *l;
    Evas_Object *o;
    int i = 0;
    EINA_LIST_FOREACH(bar.titles, l, o) {
        int ox, ow;
        evas_object_geometry_get(o, &ox, NULL, &ow, NULL);
        if (x >= ox && x < ox + ow)
            return i;
        i++;
    }
    return -1;
}

/* ---------------------------------------------------------------- the held right button */

void nv_screenbar_hold_begin(int x, int y)
{
    if (bar.session)
        nv_screenbar_menu_close();
    bar.holding = true;
    strip_show(true);
    expand(false); /* (the pointer stays with what was pressed; its moves are passed on) */
    int t = title_at(x, y);
    if (t >= 0)
        nv_screenbar_menu_open(t);
}

bool nv_screenbar_holding(void)
{
    return bar.holding;
}

void nv_screenbar_hold_move(int x, int y)
{
    if (!bar.holding)
        return;
    int t = title_at(x, y);
    if (t >= 0 && t != bar.open_index)
        nv_screenbar_menu_open(t);
    else if (bar.session)
        nv_menu_session_motion(bar.session, x, y);
}

void nv_screenbar_hold_end(int x, int y)
{
    if (!bar.holding)
        return;
    bar.holding = false;
    if (bar.session) {
        bool closed = false;
        /* over an item: chosen; elsewhere: the menus go */
        Nv_Menu_Session *s = bar.session;
        if (nv_menu_session_press(s, x, y))
            nv_menu_session_release(s, x, y, &closed);
        if (!closed && bar.session)
            nv_screenbar_menu_close();
    }
    if (!bar.session) {
        strip_show(false);
        shrink();
    }
}

/* ---------------------------------------------------------------- input */

static bool our_event(Ecore_Window w)
{
    return w == elm_win_window_id_get(bar.win);
}

static void event_xy(int x, int y, int rx, int ry, int *ox, int *oy)
{
    (void)rx;
    (void)ry;
    *ox = x;
    *oy = y;
}

static Eina_Bool on_move(void *data, int type, void *event)
{
    (void)data;
    (void)type;
    Ecore_Event_Mouse_Move *ev = event;
    if ((!bar.session && !bar.holding) || !our_event(ev->event_window))
        return ECORE_CALLBACK_PASS_ON;
    int x, y;
    event_xy(ev->x, ev->y, ev->root.x, ev->root.y, &x, &y);
    if (bar.holding) {
        nv_screenbar_hold_move(x, y);
        return ECORE_CALLBACK_PASS_ON;
    }
    int t = title_at(x, y);
    if (t >= 0 && t != bar.open_index && bar.open_index >= 0)
        nv_screenbar_menu_open(t);
    else
        nv_menu_session_motion(bar.session, x, y);
    return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool on_down(void *data, int type, void *event)
{
    (void)data;
    (void)type;
    Ecore_Event_Mouse_Button *ev = event;
    if (!our_event(ev->event_window))
        return ECORE_CALLBACK_PASS_ON;
    int x, y;
    event_xy(ev->x, ev->y, ev->root.x, ev->root.y, &x, &y);
    int t = title_at(x, y);
    if (bar.fresh)
        return ECORE_CALLBACK_PASS_ON;
    if (ev->buttons == 3 && (bar.classic || !bar.session) && y < nv.bar_h) {
        nv_screenbar_hold_begin(x, y);
        return ECORE_CALLBACK_PASS_ON;
    }
    if (!bar.session) {
        if (t >= 0 && ev->buttons == 1)
            nv_screenbar_menu_open(t);
        return ECORE_CALLBACK_PASS_ON;
    }
    if (t >= 0) {
        if (t == bar.open_index && ev->buttons == 1)
            nv_screenbar_menu_close(); /* a second click on the open title shuts it */
        else if (t != bar.open_index)
            nv_screenbar_menu_open(t);
        return ECORE_CALLBACK_PASS_ON;
    }
    if (!nv_menu_session_press(bar.session, x, y))
        nv_screenbar_menu_close();
    return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool on_up(void *data, int type, void *event)
{
    (void)data;
    (void)type;
    Ecore_Event_Mouse_Button *ev = event;
    if ((!bar.session && !bar.holding) || !our_event(ev->event_window))
        return ECORE_CALLBACK_PASS_ON;
    int x, y;
    event_xy(ev->x, ev->y, ev->root.x, ev->root.y, &x, &y);
    if (bar.holding) {
        if (ev->buttons == 3)
            nv_screenbar_hold_end(x, y);
        return ECORE_CALLBACK_PASS_ON;
    }
    bool closed;
    nv_menu_session_release(bar.session, x, y, &closed);
    /* a held right button let go away from the menus shuts them */
    if (!closed && bar.session && ev->buttons == 3 && bar.open_index >= 0 && title_at(x, y) < 0) {
        Nv_Menu_Session *s = bar.session;
        bool inside = nv_menu_session_press(s, x, y);
        if (!inside)
            nv_screenbar_menu_close();
    }
    return ECORE_CALLBACK_PASS_ON;
}

/* a menu item with this shortcut ("Super+Shift+A") in the menus, or NULL */
static Nv_Menu_Item *find_shortcut(Eina_List *menus, const char *sc)
{
    Eina_List *l, *k;
    Nv_Menu *m;
    Nv_Menu_Item *it;
    EINA_LIST_FOREACH(menus, l, m) {
        if (m->about_to_show)
            m->about_to_show(m->about_data, m);
        EINA_LIST_FOREACH(m->items, k, it) {
            if (it->shortcut && !strcasecmp(it->shortcut, sc) && it->enabled && it->cb)
                return it;
            if (it->submenu) {
                Eina_List *one = eina_list_append(NULL, it->submenu);
                Nv_Menu_Item *found = find_shortcut(one, sc);
                eina_list_free(one);
                if (found)
                    return found;
            }
        }
    }
    return NULL;
}

/* Super with a key, in one of the desktop's windows: the menu item with that shortcut */
static bool shortcut(Ecore_Event_Key *ev)
{
    if (!(ev->modifiers & ECORE_EVENT_MODIFIER_WIN))
        return false;
    static const struct { const char *key, *label; } names[] = {
        {"period", "."}, {"comma", ","}, {"question", "?"}, {"slash", "?"}, {"minus", "-"},
    };
    const char *label = ev->key;
    char one[2] = {0, 0};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (!strcmp(ev->key, names[i].key))
            label = names[i].label;
    if (strlen(ev->key) == 1) {
        one[0] = (char)(ev->key[0] >= 'a' && ev->key[0] <= 'z' ? ev->key[0] - 32 : ev->key[0]);
        label = one;
    }
    char sc[64];
    snprintf(sc, sizeof sc, "Super+%s%s", (ev->modifiers & ECORE_EVENT_MODIFIER_SHIFT) && strcmp(label, "?") ? "Shift+" : "",
             label);
    Nv_Menu_Item *it = find_shortcut(bar.menus, sc);
    if (!it && bar.menus != bar.own)
        it = find_shortcut(bar.own, sc);
    if (!it)
        return false;
    it->cb(it->data, it);
    return true;
}

static Eina_Bool on_key(void *data, int type, void *event)
{
    (void)data;
    (void)type;
    Ecore_Event_Key *ev = event;
    if (!bar.session)
        return shortcut(ev) ? ECORE_CALLBACK_DONE : ECORE_CALLBACK_PASS_ON;
    if (!our_event(ev->event_window))
        return ECORE_CALLBACK_PASS_ON;
    int r = nv_menu_session_key(bar.session, ev->key, ev->string);
    if (r == NV_MENU_KEY_PREV || r == NV_MENU_KEY_NEXT) {
        nv_screenbar_menu_open(bar.open_index + (r == NV_MENU_KEY_NEXT ? 1 : -1));
        if (bar.session)
            nv_menu_session_select_first(bar.session);
    }
    return ECORE_CALLBACK_DONE;
}

/* ---------------------------------------------------------------- titles */

static void titles_rebuild(void)
{
    if (!bar.holding)
        nv_screenbar_menu_close();
    Evas_Object *o;
    EINA_LIST_FREE(bar.titles, o)
        evas_object_del(o);
    elm_box_clear(bar.box);
    if (bar.classic && !bar.strip)
        return; /* (no titles until the right button is held) */
    Eina_List *l;
    Nv_Menu *m;
    EINA_LIST_FOREACH(bar.menus, l, m) {
        o = nv_edje_add(bar.win, "nirvana/screenbar/menutitle");
        char lab[256];
        size_t n = 0;
        for (const char *p = m->title; *p && n + 1 < sizeof lab; p++)
            if (*p != '_' || p[1] == '_')
                lab[n++] = *p;
        lab[n] = 0;
        edje_object_part_text_set(o, "nirvana.text", lab);
        int mw, mh;
        edje_object_size_min_calc(o, &mw, &mh);
        evas_object_size_hint_min_set(o, mw, mh);
        evas_object_size_hint_align_set(o, 0.0, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(o, 0, EVAS_HINT_EXPAND);
        elm_box_pack_end(bar.box, o);
        evas_object_show(o);
        bar.titles = eina_list_append(bar.titles, o);
    }
    /* the titles placed now (a menu may be opened under the pointer at once) */
    edje_object_calc_force(bar.edje);
    evas_smart_objects_calculate(evas_object_evas_get(bar.win));
}

void nv_screenbar_menus_set(Eina_List *menus, const char *owner)
{
    if (bar.holding || bar.session)
        return; /* (not under the user's pointer; the global menu sets them again later) */
    bar.menus = menus ? menus : bar.own;
    free(bar.owner);
    bar.owner = owner ? strdup(owner) : NULL;
    titles_rebuild();
    title_show();
}

Eina_List *nv_screenbar_own_menus(void)
{
    return bar.own;
}

/* the title: the program's (or the screen's); not while the titles are there (classic),
 * nor beside the desktop's own menus (whose first is the screen's own) */
static void title_show(void)
{
    const char *t = bar.title[0] ? bar.title : NV_SCREEN_TITLE;
    if (bar.classic)
        t = bar.strip ? "" : t;
    else if (bar.menus == bar.own)
        t = "";
    edje_object_part_text_set(bar.edje, "nirvana.text.title", t);
}

void nv_screenbar_title_set(const char *title)
{
    snprintf(bar.title, sizeof bar.title, "%s", title ? title : "");
    title_show();
}

/* ---------------------------------------------------------------- the clock */

static Eina_Bool clock_tick(void *data)
{
    (void)data;
    char buf[64];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, sizeof buf, "%a %e %b  %H:%M", &tm);
    edje_object_part_text_set(bar.edje, "nirvana.text.clock", buf);
    /* the next tick at the next minute */
    ecore_timer_interval_set(bar.clock, 60 - tm.tm_sec + 0.05);
    return ECORE_CALLBACK_RENEW;
}

/* ---------------------------------------------------------------- the desktop's menus */

static void m_assigns(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_assign_editor_show(); }
static void m_execute(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_execute_command(); }
static void m_shell(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_new_shell(); }
static void m_about(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_about(); }
static void m_quit(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_quit(); }
static void m_update(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_backdrop_reload(); }
static void m_open_drawer(void *d, Nv_Menu_Item *it) { (void)it; nv_lister_open(d); }

static void m_win_close(void *d, Nv_Menu_Item *it) { (void)d; (void)it; if (nv_window_active()) nv_window_close(nv_window_active()); }
static void m_win_iconify(void *d, Nv_Menu_Item *it) { (void)d; (void)it; if (nv_window_active()) nv_window_iconify(nv_window_active()); }
static void m_win_zoom(void *d, Nv_Menu_Item *it) { (void)d; (void)it; if (nv_window_active()) nv_window_zoom(nv_window_active()); }
static void m_win_depth(void *d, Nv_Menu_Item *it) { (void)d; (void)it; if (nv_window_active()) nv_window_depth(nv_window_active()); }

static void m_icons_open(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_backdrop_open_selected(); }
static void m_icons_all(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_backdrop_select_all(true); }
static void m_icons_none(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_backdrop_select_all(false); }
static void m_icons_cleanup(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_backdrop_clean_up(); }
static void m_icons_snapshot(void *d, Nv_Menu_Item *it) { (void)d; (void)it; nv_backdrop_snapshot(); }

/* the Window menu: its items for the active window, and the window's own items */
static void window_menu_show(void *data, Nv_Menu *m)
{
    (void)data;
    nv_menu_clear(m);
    Nv_Window *w = nv_window_active();
    nv_menu_add(m, _("_Close"), "Super+K", w ? m_win_close : NULL, NULL);
    nv_menu_add(m, _("_Iconify"), NULL, w ? m_win_iconify : NULL, NULL);
    nv_menu_add(m, _("_Zoom"), NULL, w ? m_win_zoom : NULL, NULL);
    nv_menu_add(m, _("_Depth"), NULL, w ? m_win_depth : NULL, NULL);
    if (w && w->window_menu && w->window_menu->items) {
        nv_menu_add_separator(m);
        /* the window's items copied (the window keeps its own) */
        Eina_List *l;
        Nv_Menu_Item *src;
        EINA_LIST_FOREACH(w->window_menu->items, l, src) {
            if (src->kind == NV_ITEM_SEPARATOR) {
                nv_menu_add_separator(m);
                continue;
            }
            Nv_Menu_Item *it = nv_menu_add(m, src->label, src->shortcut, src->cb, src->data);
            it->kind = src->kind;
            it->checked = src->checked;
            it->id = src->id;
            it->enabled = src->enabled;
        }
    }
}

/* the Tools menu: the assigns as drawers to open, and the editor */
static void tools_menu_show(void *data, Nv_Menu *m)
{
    (void)data;
    nv_menu_clear(m);
    nv_menu_add(m, _("_Assigns..."), "Super+A", m_assigns, NULL);
    nv_menu_add(m, _("_Execute Command..."), "Super+E", m_execute, NULL);
    nv_menu_add(m, _("New _Shell"), "Super+N", m_shell, NULL);
    nv_menu_add_separator(m);
    Nv_Menu *vols = nv_menu_new(_("Volumes"));
    nirvana_assign_list l;
    if (nirvana_assign_list_load(&l) >= 0) {
        for (size_t i = 0; i < l.count; i++) {
            char label[NIRVANA_NAME_MAX + 4];
            snprintf(label, sizeof label, "%s:", l.items[i].name);
            /* (the label doubles as the path to open: "Name:") */
            nv_menu_add(vols, label, NULL, m_open_drawer, (void *)eina_stringshare_add(label));
        }
        nirvana_assign_list_free(&l);
    }
    nv_menu_add_submenu(m, _("Open _Volume"), vols);
}

static void build_own_menus(void)
{
    Nv_Menu *m = nv_menu_new(NV_SCREEN_TITLE);
    nv_menu_add(m, _("_Assigns..."), "Super+A", m_assigns, NULL);
    nv_menu_add(m, _("_Execute Command..."), "Super+E", m_execute, NULL);
    nv_menu_add(m, _("New _Shell"), "Super+N", m_shell, NULL);
    nv_menu_add_separator(m);
    nv_menu_add(m, _("_Update All"), NULL, m_update, NULL);
    nv_menu_add_separator(m);
    nv_menu_add(m, _("A_bout..."), "Super+?", m_about, NULL);
    nv_menu_add(m, _("_Quit..."), "Super+Q", m_quit, NULL);
    bar.own = eina_list_append(bar.own, m);

    m = nv_menu_new(_("Window"));
    m->about_to_show = window_menu_show;
    window_menu_show(NULL, m);
    bar.own = eina_list_append(bar.own, m);

    m = nv_menu_new(_("Icons"));
    nv_menu_add(m, _("_Open"), "Super+O", m_icons_open, NULL);
    nv_menu_add_separator(m);
    nv_menu_add(m, _("Select _All"), "Super+Shift+A", m_icons_all, NULL);
    nv_menu_add(m, _("Select _None"), NULL, m_icons_none, NULL);
    nv_menu_add_separator(m);
    nv_menu_add(m, _("_Clean Up"), "Super+.", m_icons_cleanup, NULL);
    nv_menu_add(m, _("_Snapshot"), NULL, m_icons_snapshot, NULL);
    bar.own = eina_list_append(bar.own, m);

    m = nv_menu_new(_("Tools"));
    m->about_to_show = tools_menu_show;
    tools_menu_show(NULL, m);
    bar.own = eina_list_append(bar.own, m);
}

/* ---------------------------------------------------------------- the bar */

static void configured(void *data, Evas_Object *win, int w, int h)
{
    (void)data;
    (void)win;
    nv.screen_w = w > nv.screen_w ? w : nv.screen_w;
    evas_object_resize(bar.edje, w, nv.bar_h);
    (void)h;
}

static void on_depth(void *data, Evas_Object *o, const char *em, const char *src)
{
    (void)data;
    (void)o;
    (void)em;
    (void)src;
    /* the screen's depth gadget: Wayland tells no client which window is in front, so
     * the desktop's own active window goes back, or the next of its windows comes forward */
    Nv_Window *w = nv_window_active() ? nv_window_active() : eina_list_last_data_get(nv.windows);
    if (w) {
        Nv_Window *next = NULL;
        Eina_List *l;
        Nv_Window *o;
        EINA_LIST_FOREACH(nv.windows, l, o)
            if (o != w)
                next = o;
        if (next)
            elm_win_activate(next->win);
    }
}

bool nv_screenbar_create(void)
{
    bar.win = nv_layer_win_add("nirvana-screenbar", NV_LAYER_TOP);
    elm_win_alpha_set(bar.win, EINA_TRUE);
    const char *hs = nv_theme_data("nirvana/screenbar/height");
    nv.bar_h = hs ? atoi(hs) : 24;
    if (nv.bar_h < 16)
        nv.bar_h = 24;

    bar.catcher = evas_object_rectangle_add(evas_object_evas_get(bar.win));
    evas_object_color_set(bar.catcher, 0, 0, 0, 0);
    evas_object_repeat_events_set(bar.catcher, EINA_FALSE);

    bar.edje = nv_edje_add(bar.win, "nirvana/screenbar");
    if (!bar.edje) {
        fprintf(stderr, "nirvana-desktop: the theme %s has no screen bar\n", nv.theme);
        return false;
    }
    edje_object_signal_callback_add(bar.edje, "nirvana,gadget,depth", "nirvana", on_depth, NULL);
    bar.box = elm_box_add(bar.win);
    elm_box_horizontal_set(bar.box, EINA_TRUE);
    elm_box_align_set(bar.box, 0.0, 0.5);
    edje_object_part_swallow(bar.edje, "nirvana.swallow.menus", bar.box);
    evas_object_move(bar.edje, 0, 0);
    evas_object_resize(bar.edje, nv.screen_w > 0 ? nv.screen_w : 800, nv.bar_h);
    evas_object_show(bar.edje);

    bar.classic = strcmp(nv_pref("menus", "hold"), "always") != 0;
    build_own_menus();
    bar.menus = bar.own;
    titles_rebuild();
    title_show();

    bar.clock = ecore_timer_add(1.0, clock_tick, NULL);
    clock_tick(NULL);

    bar.handlers = eina_list_append(bar.handlers, ecore_event_handler_add(ECORE_EVENT_MOUSE_MOVE, on_move, NULL));
    bar.handlers = eina_list_append(bar.handlers, ecore_event_handler_add(ECORE_EVENT_MOUSE_BUTTON_DOWN, on_down, NULL));
    bar.handlers = eina_list_append(bar.handlers, ecore_event_handler_add(ECORE_EVENT_MOUSE_BUTTON_UP, on_up, NULL));
    bar.handlers = eina_list_append(bar.handlers, ecore_event_handler_add(ECORE_EVENT_KEY_DOWN, on_key, NULL));

    if (!nv_layer_setup(bar.win, NV_LAYER_TOP, NV_ANCHOR_TOP | NV_ANCHOR_LEFT | NV_ANCHOR_RIGHT, 0, nv.bar_h,
                        nv.bar_h, false, configured, NULL))
        return false;
    evas_object_show(bar.win);
    nv.bar = bar.win;
    return true;
}
