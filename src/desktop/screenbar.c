/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the screen bar, "Nirvana Screen".
 *
 * A strip at the top of the screen (a layer surface of the top layer that keeps its
 * height for itself): the screen's title, the menu titles - the desktop's own, or the
 * focused program's (the global menu, stage 3) - the clock and the screen's depth
 * gadget. The menus drop down from it: on Wayland the bar's surface grows over the
 * screen while a menu is open (the area below the bar transparent, a click there
 * closes the menu); on X11 the panels are popup windows and the pointer is grabbed.
 * A press of the right button on the bar opens the menus too, held: releasing over an
 * item chooses it.
 */
#include "desktop.h"
#include "menu_host.h"

#include <Ecore_X.h>
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
    bool grabbed;
    bool closing;
} bar = {.open_index = -1};

/* ---------------------------------------------------------------- the menu host */

static Evas_Object *host_probe(void *data)
{
    (void)data;
    return bar.win;
}

static Evas_Object *host_panel_new(void *data, int x, int y, int w, int h, int *ox, int *oy)
{
    (void)data;
    if (nv.backend == NV_BACKEND_WAYLAND) {
        *ox = x;
        *oy = y;
        return bar.win;
    }
    Evas_Object *pw = elm_win_add(bar.win, "nirvana-menu", ELM_WIN_POPUP_MENU);
    elm_win_borderless_set(pw, EINA_TRUE);
    elm_win_override_set(pw, EINA_TRUE);
    evas_object_move(pw, x, y);
    evas_object_resize(pw, w, h);
    evas_object_show(pw);
    *ox = 0;
    *oy = 0;
    return pw;
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

static void grab(bool on)
{
    if (nv.backend != NV_BACKEND_X11)
        return;
    Ecore_X_Window xw = elm_win_xwindow_get(bar.win);
    if (on && !bar.grabbed) {
        bar.grabbed = ecore_x_pointer_grab(xw) && ecore_x_keyboard_grab(xw);
    } else if (!on && bar.grabbed) {
        ecore_x_pointer_ungrab();
        ecore_x_keyboard_ungrab();
        bar.grabbed = false;
    }
}

static void session_done(void *data)
{
    (void)data;
    bar.session = NULL;
    if (bar.closing)
        return; /* another menu is being opened in its place */
    bar.open_index = -1;
    title_select(-1);
    evas_object_hide(bar.catcher);
    grab(false);
    if (nv.backend == NV_BACKEND_WAYLAND) {
        nv_layer_keyboard(bar.win, false);
        nv_layer_resize(bar.win, 0, nv.bar_h);
    }
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
        if (nv.backend == NV_BACKEND_WAYLAND) {
            nv_layer_resize(bar.win, 0, nv.screen_h);
            nv_layer_keyboard(bar.win, true);
        }
        evas_object_move(bar.catcher, 0, nv.bar_h);
        evas_object_resize(bar.catcher, nv.screen_w, nv.screen_h);
        evas_object_show(bar.catcher);
        grab(true);
    }
    bar.open_index = index;
    title_select(index);
    Evas_Object *t = eina_list_nth(bar.titles, (unsigned)index);
    int tx = 0;
    evas_object_geometry_get(t, &tx, NULL, NULL, NULL);
    bar.session = nv_menu_session_open(&host, NULL, eina_list_nth(bar.menus, (unsigned)index), tx, nv.bar_h - 1,
                                       session_done, NULL);
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

/* ---------------------------------------------------------------- input */

static bool our_event(Ecore_Window w)
{
    if (w == elm_win_window_id_get(bar.win))
        return true;
    /* (X11: the popup panels are the bar's children; with the grab, events come to the
     * bar anyway) */
    return nv.backend == NV_BACKEND_X11 && bar.grabbed;
}

static void event_xy(int x, int y, int rx, int ry, int *ox, int *oy)
{
    if (nv.backend == NV_BACKEND_X11) {
        *ox = rx;
        *oy = ry;
    } else {
        *ox = x;
        *oy = y;
    }
}

static Eina_Bool on_move(void *data, int type, void *event)
{
    (void)data;
    (void)type;
    Ecore_Event_Mouse_Move *ev = event;
    if (!bar.session || !our_event(ev->event_window))
        return ECORE_CALLBACK_PASS_ON;
    int x, y;
    event_xy(ev->x, ev->y, ev->root.x, ev->root.y, &x, &y);
    int t = title_at(x, y);
    if (t >= 0 && t != bar.open_index)
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
    if (!bar.session) {
        if (t >= 0 && ev->buttons == 1)
            nv_screenbar_menu_open(t);
        else if (ev->buttons == 3 && y < nv.bar_h)
            nv_screenbar_menu_open(t >= 0 ? t : 0);
        return ECORE_CALLBACK_PASS_ON;
    }
    if (t >= 0) {
        if (t == bar.open_index && ev->buttons == 1)
            nv_screenbar_menu_close(); /* a second click on the open title shuts it */
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
    if (!bar.session || !our_event(ev->event_window))
        return ECORE_CALLBACK_PASS_ON;
    int x, y;
    event_xy(ev->x, ev->y, ev->root.x, ev->root.y, &x, &y);
    bool closed;
    nv_menu_session_release(bar.session, x, y, &closed);
    /* a held right button let go away from the menus shuts them */
    if (!closed && bar.session && ev->buttons == 3 && title_at(x, y) < 0) {
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
    if (!our_event(ev->event_window) && nv.backend == NV_BACKEND_WAYLAND)
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
    nv_screenbar_menu_close();
    Evas_Object *o;
    EINA_LIST_FREE(bar.titles, o)
        evas_object_del(o);
    elm_box_clear(bar.box);
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
}

void nv_screenbar_menus_set(Eina_List *menus, const char *owner)
{
    bar.menus = menus ? menus : bar.own;
    free(bar.owner);
    bar.owner = owner ? strdup(owner) : NULL;
    titles_rebuild();
}

Eina_List *nv_screenbar_own_menus(void)
{
    return bar.own;
}

void nv_screenbar_title_set(const char *title)
{
    edje_object_part_text_set(bar.edje, "nirvana.text.title", title ? title : NV_SCREEN_TITLE);
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
            Nv_Menu_Item *it = src->kind == NV_ITEM_CHECK
                                   ? nv_menu_add_check(m, src->label, src->checked, src->cb, src->data)
                                   : nv_menu_add(m, src->label, src->shortcut, src->cb, src->data);
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
    Nv_Window *w = nv_window_active();
    if (w)
        nv_window_depth(w);
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
    edje_object_part_text_set(bar.edje, "nirvana.text.title", NV_SCREEN_TITLE);
    edje_object_signal_callback_add(bar.edje, "nirvana,gadget,depth", "nirvana", on_depth, NULL);
    bar.box = elm_box_add(bar.win);
    elm_box_horizontal_set(bar.box, EINA_TRUE);
    elm_box_align_set(bar.box, 0.0, 0.5);
    edje_object_part_swallow(bar.edje, "nirvana.swallow.menus", bar.box);
    evas_object_move(bar.edje, 0, 0);
    evas_object_resize(bar.edje, nv.screen_w > 0 ? nv.screen_w : 800, nv.bar_h);
    evas_object_show(bar.edje);

    build_own_menus();
    bar.menus = bar.own;
    titles_rebuild();

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
