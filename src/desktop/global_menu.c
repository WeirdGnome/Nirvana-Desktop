/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the global menu - the focused program's menus in the screen bar.
 *
 * Programs export their menus over D-Bus (com.canonical.dbusmenu) and register them with
 * the menu registrar (com.canonical.AppMenu.Registrar) under their X11 window id; GTK
 * programs do so through appmenu-gtk-module, Qt ones through their platform theme, and
 * LibreOffice through its GTK plugin (the Startup-Sequence sets the variables for all
 * of them). The desktop is the registrar. When a registered window has the focus, its
 * menus are read (GetLayout), shown in the bar after the "Nirvana Screen" menu, and a
 * chosen item is sent back (Event "clicked"). Menus that change (LayoutUpdated,
 * ItemsPropertiesUpdated) are read again; submenus filled on demand get AboutToShow.
 *
 * The focus: on X11 the root window's _NET_ACTIVE_WINDOW. On Wayland the X11 programs run
 * in XWayland, whose root window the compositor keeps _NET_ACTIVE_WINDOW on; the desktop
 * watches it there. Native Wayland programs have no window ids to register menus under -
 * the protocol has none - so for them the bar shows the program's name (from
 * wlr-foreign-toplevel-management) and the desktop's own menus, with a Window menu that
 * can close, iconify and zoom the program's window.
 */
#include "desktop.h"

#include <Ecore_Wl2.h>
#include <Ecore_X.h>
#include <Eldbus.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wayland-client.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#define REGISTRAR_BUS "com.canonical.AppMenu.Registrar"
#define REGISTRAR_PATH "/com/canonical/AppMenu/Registrar"
#define DBUSMENU_IFACE "com.canonical.dbusmenu"

typedef struct App_Menu {
    unsigned xid;
    char *service, *path;
    Eldbus_Object *obj;
    Eldbus_Proxy *proxy;
    Eina_List *signals;   /* Eldbus_Signal_Handler */
    Eina_List *menus;     /* Nv_Menu: the program's top menus */
    Eldbus_Pending *pending;
    bool stale;           /* read again once the open menu is shut */
} App_Menu;

static struct {
    Eldbus_Connection *bus;
    Eldbus_Service_Interface *iface;
    Eina_Hash *apps;      /* xid -> App_Menu */
    App_Menu *shown;      /* whose menus are in the bar */
    Eina_List *bar_list;  /* the list given to the bar */
    unsigned active_xid;
    bool x_ok;
    Ecore_X_Window x_root;
    /* native Wayland programs */
    struct zwlr_foreign_toplevel_manager_v1 *ftm;
    Eina_List *toplevels; /* Toplevel */
    Ecore_Timer *refresh;
} gm;

typedef struct Toplevel {
    struct zwlr_foreign_toplevel_handle_v1 *h;
    char *title, *app_id;
    bool activated, pending_activated;
} Toplevel;

static void refresh(void);

/* ---------------------------------------------------------------- reading dbusmenu */

typedef struct Props {
    const char *label, *type, *toggle_type, *children_display;
    bool enabled, visible;
    int toggle_state;
    char shortcut[64];
} Props;

static void props_read(Eldbus_Message_Iter *dict, Props *p)
{
    memset(p, 0, sizeof *p);
    p->enabled = p->visible = true;
    p->toggle_state = -1;
    Eldbus_Message_Iter *entry;
    while (eldbus_message_iter_get_and_next(dict, 'e', &entry)) {
        const char *key;
        Eldbus_Message_Iter *var;
        if (!eldbus_message_iter_arguments_get(entry, "sv", &key, &var))
            continue;
        char *sig = eldbus_message_iter_signature_get(var);
        if (!sig)
            continue;
        if (sig[0] == 's') {
            const char *s = NULL;
            eldbus_message_iter_arguments_get(var, "s", &s);
            if (!strcmp(key, "label"))
                p->label = s;
            else if (!strcmp(key, "type"))
                p->type = s;
            else if (!strcmp(key, "toggle-type"))
                p->toggle_type = s;
            else if (!strcmp(key, "children-display"))
                p->children_display = s;
        } else if (sig[0] == 'b') {
            Eina_Bool b = EINA_TRUE;
            eldbus_message_iter_arguments_get(var, "b", &b);
            if (!strcmp(key, "enabled"))
                p->enabled = b;
            else if (!strcmp(key, "visible"))
                p->visible = b;
        } else if (sig[0] == 'i') {
            int i = 0;
            eldbus_message_iter_arguments_get(var, "i", &i);
            if (!strcmp(key, "toggle-state"))
                p->toggle_state = i;
        } else if (!strcmp(sig, "aas") && !strcmp(key, "shortcut")) {
            /* [["Control", "q"]]: the first combination, as "Ctrl+Q" */
            Eldbus_Message_Iter *outer, *inner;
            if (eldbus_message_iter_arguments_get(var, "aas", &outer) &&
                eldbus_message_iter_get_and_next(outer, 'a', &inner)) {
                const char *k;
                while (eldbus_message_iter_get_and_next(inner, 's', &k)) {
                    const char *name = !strcmp(k, "Control") ? "Ctrl" : !strcmp(k, "Super") ? "Super" : k;
                    size_t n = strlen(p->shortcut);
                    snprintf(p->shortcut + n, sizeof p->shortcut - n, "%s%s", n ? "+" : "", name);
                    if (strlen(k) == 1 && p->shortcut[strlen(p->shortcut) - 1] >= 'a' &&
                        p->shortcut[strlen(p->shortcut) - 1] <= 'z')
                        p->shortcut[strlen(p->shortcut) - 1] -= 32;
                }
            }
        }
        free(sig);
    }
}

static void item_clicked(void *data, Nv_Menu_Item *it)
{
    App_Menu *am = data;
    if (!am->proxy)
        return;
    Eldbus_Message *msg = eldbus_proxy_method_call_new(am->proxy, "Event");
    Eldbus_Message_Iter *iter = eldbus_message_iter_get(msg);
    eldbus_message_iter_arguments_append(iter, "is", it->id, "clicked");
    Eldbus_Message_Iter *var = eldbus_message_iter_container_new(iter, 'v', "i");
    eldbus_message_iter_basic_append(var, 'i', 0);
    eldbus_message_iter_container_close(iter, var);
    eldbus_message_iter_basic_append(iter, 'u', (unsigned)time(NULL));
    eldbus_proxy_send(am->proxy, msg, NULL, NULL, -1);
}

static void submenu_about_to_show(void *data, Nv_Menu *m)
{
    App_Menu *am = data;
    if (am->proxy && m->id > 0)
        eldbus_proxy_call(am->proxy, "AboutToShow", NULL, NULL, -1, "i", m->id);
}

/* the children (an "av" of "(ia{sv}av)") put into m */
static void fill_menu(App_Menu *am, Nv_Menu *m, Eldbus_Message_Iter *children, int depth)
{
    Eldbus_Message_Iter *var;
    while (eldbus_message_iter_get_and_next(children, 'v', &var)) {
        Eldbus_Message_Iter *st, *props, *kids;
        int id;
        if (!eldbus_message_iter_arguments_get(var, "(ia{sv}av)", &st) ||
            !eldbus_message_iter_arguments_get(st, "ia{sv}av", &id, &props, &kids))
            continue;
        Props p;
        props_read(props, &p);
        if (!p.visible)
            continue;
        if (p.type && !strcmp(p.type, "separator")) {
            nv_menu_add_separator(m);
            continue;
        }
        bool sub = (p.children_display && !strcmp(p.children_display, "submenu"));
        Nv_Menu_Item *it;
        if (sub && depth < 16) {
            Nv_Menu *sm = nv_menu_new(p.label ? p.label : "");
            sm->id = id;
            sm->about_to_show = submenu_about_to_show;
            sm->about_data = am;
            fill_menu(am, sm, kids, depth + 1);
            it = nv_menu_add_submenu(m, p.label ? p.label : "", sm);
            it->enabled = p.enabled;
        } else {
            it = nv_menu_add(m, p.label ? p.label : "", p.shortcut[0] ? p.shortcut : NULL, item_clicked, am);
            it->enabled = p.enabled;
            if (p.toggle_type && (!strcmp(p.toggle_type, "checkmark") || !strcmp(p.toggle_type, "radio"))) {
                it->kind = !strcmp(p.toggle_type, "radio") ? NV_ITEM_RADIO : NV_ITEM_CHECK;
                it->checked = p.toggle_state == 1;
            }
        }
        it->id = id;
    }
}

static void menus_free(Eina_List *menus)
{
    Nv_Menu *m;
    EINA_LIST_FREE(menus, m)
        nv_menu_free(m);
}

static void layout_reply(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
    (void)pending;
    App_Menu *am = data;
    am->pending = NULL;
    const char *ename, *etext;
    if (eldbus_message_error_get(msg, &ename, &etext))
        return;
    unsigned rev;
    Eldbus_Message_Iter *root, *props, *kids;
    int id;
    if (!eldbus_message_arguments_get(msg, "u(ia{sv}av)", &rev, &root) ||
        !eldbus_message_iter_arguments_get(root, "ia{sv}av", &id, &props, &kids))
        return;
    /* the top level: one Nv_Menu per child of the root */
    Eina_List *menus = NULL;
    Eldbus_Message_Iter *var;
    while (eldbus_message_iter_get_and_next(kids, 'v', &var)) {
        Eldbus_Message_Iter *st, *tprops, *tkids;
        int tid;
        if (!eldbus_message_iter_arguments_get(var, "(ia{sv}av)", &st) ||
            !eldbus_message_iter_arguments_get(st, "ia{sv}av", &tid, &tprops, &tkids))
            continue;
        Props p;
        props_read(tprops, &p);
        if (!p.visible || !p.label || (p.type && !strcmp(p.type, "separator")))
            continue;
        Nv_Menu *m = nv_menu_new(p.label);
        m->id = tid;
        m->about_to_show = submenu_about_to_show;
        m->about_data = am;
        fill_menu(am, m, tkids, 0);
        menus = eina_list_append(menus, m);
    }
    Eina_List *old = am->menus;
    am->menus = menus;
    if (gm.shown == am)
        refresh();
    menus_free(old);
}

static void read_layout(App_Menu *am)
{
    if (!am->proxy)
        return;
    if (nv_screenbar_menu_is_open() && gm.shown == am) {
        am->stale = true; /* not under the user's pointer: when the menu is shut */
        return;
    }
    am->stale = false;
    if (am->pending)
        eldbus_pending_cancel(am->pending);
    Eldbus_Message *msg = eldbus_proxy_method_call_new(am->proxy, "GetLayout");
    Eldbus_Message_Iter *iter = eldbus_message_iter_get(msg);
    eldbus_message_iter_arguments_append(iter, "ii", 0, -1);
    Eldbus_Message_Iter *arr = eldbus_message_iter_container_new(iter, 'a', "s");
    eldbus_message_iter_container_close(iter, arr);
    am->pending = eldbus_proxy_send(am->proxy, msg, layout_reply, am, 5.0);
}

static void on_layout_updated(void *data, const Eldbus_Message *msg)
{
    (void)msg;
    read_layout(data);
}

static void app_connect(App_Menu *am)
{
    if (am->proxy || !gm.bus)
        return;
    am->obj = eldbus_object_get(gm.bus, am->service, am->path);
    am->proxy = eldbus_proxy_get(am->obj, DBUSMENU_IFACE);
    am->signals = eina_list_append(am->signals,
                                   eldbus_proxy_signal_handler_add(am->proxy, "LayoutUpdated", on_layout_updated, am));
    am->signals = eina_list_append(
        am->signals, eldbus_proxy_signal_handler_add(am->proxy, "ItemsPropertiesUpdated", on_layout_updated, am));
    /* the top menus told they are about to be shown (some programs fill them then) */
    eldbus_proxy_call(am->proxy, "AboutToShow", NULL, NULL, -1, "i", 0);
    read_layout(am);
}

static void app_free(void *data)
{
    App_Menu *am = data;
    if (gm.shown == am) {
        gm.shown = NULL;
        refresh();
    }
    if (am->pending)
        eldbus_pending_cancel(am->pending);
    Eldbus_Signal_Handler *sh;
    EINA_LIST_FREE(am->signals, sh)
        eldbus_signal_handler_del(sh);
    if (am->proxy)
        eldbus_proxy_unref(am->proxy);
    if (am->obj)
        eldbus_object_unref(am->obj);
    menus_free(am->menus);
    free(am->service);
    free(am->path);
    free(am);
}

/* ---------------------------------------------------------------- the registrar */

static void emit_registered(App_Menu *am)
{
    eldbus_service_signal_emit(gm.iface, 0, am->xid, am->service, am->path);
}

static void on_owner_gone(void *data, const char *bus, const char *old_id, const char *new_id)
{
    (void)data;
    (void)old_id;
    if (new_id && *new_id)
        return;
    /* the program left the bus: its windows' menus go */
    Eina_Iterator *it = eina_hash_iterator_data_new(gm.apps);
    App_Menu *am;
    Eina_List *gone = NULL;
    EINA_ITERATOR_FOREACH(it, am)
        if (!strcmp(am->service, bus))
            gone = eina_list_append(gone, am);
    eina_iterator_free(it);
    EINA_LIST_FREE(gone, am) {
        unsigned xid = am->xid;
        eldbus_service_signal_emit(gm.iface, 1, xid);
        eina_hash_del_by_key(gm.apps, &xid);
    }
}

static Eldbus_Message *m_register(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
    (void)iface;
    unsigned xid;
    const char *path;
    if (!eldbus_message_arguments_get(msg, "uo", &xid, &path))
        return eldbus_message_error_new(msg, "org.freedesktop.DBus.Error.InvalidArgs", "uo expected");
    const char *sender = eldbus_message_sender_get(msg);
    App_Menu *old = eina_hash_find(gm.apps, &xid);
    if (old && !strcmp(old->service, sender) && !strcmp(old->path, path))
        return eldbus_message_method_return_new(msg);
    if (old)
        eina_hash_del_by_key(gm.apps, &xid);
    App_Menu *am = calloc(1, sizeof *am);
    am->xid = xid;
    am->service = strdup(sender);
    am->path = strdup(path);
    eina_hash_add(gm.apps, &xid, am);
    eldbus_name_owner_changed_callback_add(gm.bus, sender, on_owner_gone, NULL, EINA_FALSE);
    emit_registered(am);
    if (xid == gm.active_xid)
        refresh();
    return eldbus_message_method_return_new(msg);
}

static Eldbus_Message *m_unregister(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
    (void)iface;
    unsigned xid;
    if (!eldbus_message_arguments_get(msg, "u", &xid))
        return eldbus_message_error_new(msg, "org.freedesktop.DBus.Error.InvalidArgs", "u expected");
    if (eina_hash_find(gm.apps, &xid)) {
        eina_hash_del_by_key(gm.apps, &xid);
        eldbus_service_signal_emit(gm.iface, 1, xid);
    }
    return eldbus_message_method_return_new(msg);
}

static Eldbus_Message *m_get_menu(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
    (void)iface;
    unsigned xid;
    if (!eldbus_message_arguments_get(msg, "u", &xid))
        return eldbus_message_error_new(msg, "org.freedesktop.DBus.Error.InvalidArgs", "u expected");
    App_Menu *am = eina_hash_find(gm.apps, &xid);
    Eldbus_Message *reply = eldbus_message_method_return_new(msg);
    eldbus_message_arguments_append(reply, "so", am ? am->service : "", am ? am->path : "/");
    return reply;
}

static Eldbus_Message *m_get_menus(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
    (void)iface;
    Eldbus_Message *reply = eldbus_message_method_return_new(msg);
    Eldbus_Message_Iter *iter = eldbus_message_iter_get(reply);
    Eldbus_Message_Iter *arr = eldbus_message_iter_container_new(iter, 'a', "(uso)");
    Eina_Iterator *it = eina_hash_iterator_data_new(gm.apps);
    App_Menu *am;
    EINA_ITERATOR_FOREACH(it, am) {
        Eldbus_Message_Iter *st = eldbus_message_iter_container_new(arr, 'r', NULL);
        eldbus_message_iter_arguments_append(st, "uso", am->xid, am->service, am->path);
        eldbus_message_iter_container_close(arr, st);
    }
    eina_iterator_free(it);
    eldbus_message_iter_container_close(iter, arr);
    return reply;
}

static const Eldbus_Method reg_methods[] = {
    {"RegisterWindow", ELDBUS_ARGS({"u", "windowId"}, {"o", "menuObjectPath"}), NULL, m_register, 0},
    {"UnregisterWindow", ELDBUS_ARGS({"u", "windowId"}), NULL, m_unregister, 0},
    {"GetMenuForWindow", ELDBUS_ARGS({"u", "windowId"}), ELDBUS_ARGS({"s", "service"}, {"o", "menuObjectPath"}),
     m_get_menu, 0},
    {"GetMenus", NULL, ELDBUS_ARGS({"a(uso)", "menus"}), m_get_menus, 0},
    {NULL, NULL, NULL, NULL, 0},
};

static const Eldbus_Signal reg_signals[] = {
    {"WindowRegistered", ELDBUS_ARGS({"u", "windowId"}, {"s", "service"}, {"o", "menuObjectPath"}), 0},
    {"WindowUnregistered", ELDBUS_ARGS({"u", "windowId"}), 0},
    {NULL, NULL, 0},
};

static const Eldbus_Service_Interface_Desc reg_desc = {REGISTRAR_BUS, reg_methods, reg_signals, NULL, NULL, NULL};

/* ---------------------------------------------------------------- native Wayland programs */

static Toplevel *active_toplevel(void)
{
    Eina_List *l;
    Toplevel *t;
    EINA_LIST_FOREACH(gm.toplevels, l, t)
        if (t->activated)
            return t;
    return NULL;
}

static void tl_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const char *title)
{
    (void)h;
    Toplevel *t = data;
    free(t->title);
    t->title = strdup(title);
}

static void tl_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const char *app_id)
{
    (void)h;
    Toplevel *t = data;
    free(t->app_id);
    t->app_id = strdup(app_id);
}

static void tl_output(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *o)
{
    (void)data;
    (void)h;
    (void)o;
}

static void tl_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_array *state)
{
    (void)h;
    Toplevel *t = data;
    t->pending_activated = false;
    uint32_t *s;
    wl_array_for_each(s, state)
        if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
            t->pending_activated = true;
}

static Eina_Bool refresh_later(void *data)
{
    (void)data;
    gm.refresh = NULL;
    refresh();
    return ECORE_CALLBACK_CANCEL;
}

static void schedule_refresh(void)
{
    if (!gm.refresh)
        gm.refresh = ecore_timer_add(0.05, refresh_later, NULL);
}

static void tl_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *h)
{
    (void)h;
    Toplevel *t = data;
    t->activated = t->pending_activated;
    schedule_refresh();
}

static void tl_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *h)
{
    Toplevel *t = data;
    gm.toplevels = eina_list_remove(gm.toplevels, t);
    zwlr_foreign_toplevel_handle_v1_destroy(h);
    free(t->title);
    free(t->app_id);
    free(t);
    schedule_refresh();
}

static void tl_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
                      struct zwlr_foreign_toplevel_handle_v1 *parent)
{
    (void)data;
    (void)h;
    (void)parent;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener tl_listener = {
    tl_title, tl_app_id, tl_output, tl_output, tl_state, tl_done, tl_closed, tl_parent,
};

static void ftm_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *m,
                         struct zwlr_foreign_toplevel_handle_v1 *h)
{
    (void)data;
    (void)m;
    Toplevel *t = calloc(1, sizeof *t);
    t->h = h;
    zwlr_foreign_toplevel_handle_v1_add_listener(h, &tl_listener, t);
    gm.toplevels = eina_list_append(gm.toplevels, t);
}

static void ftm_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *m)
{
    (void)data;
    zwlr_foreign_toplevel_manager_v1_destroy(m);
    gm.ftm = NULL;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener ftm_listener = {ftm_toplevel, ftm_finished};

static void foreign_start(void)
{
    Ecore_Wl2_Display *d = ecore_wl2_connected_display_get(NULL);
    if (!d)
        return;
    Eina_Iterator *it = ecore_wl2_display_globals_get(d);
    Ecore_Wl2_Global *g;
    unsigned id = 0, ver = 0;
    EINA_ITERATOR_FOREACH(it, g)
        if (!strcmp(g->interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
            id = g->id;
            ver = g->version;
        }
    eina_iterator_free(it);
    if (!id)
        return;
    gm.ftm = wl_registry_bind(ecore_wl2_display_registry_get(d), id, &zwlr_foreign_toplevel_manager_v1_interface,
                              ver < 3 ? ver : 3);
    zwlr_foreign_toplevel_manager_v1_add_listener(gm.ftm, &ftm_listener, NULL);
}

/* the Window menu's actions on a native Wayland program's window */
static void tl_act(void *data, Nv_Menu_Item *it)
{
    (void)data;
    Toplevel *t = active_toplevel();
    if (!t)
        return;
    switch (it->id) {
    case 1: zwlr_foreign_toplevel_handle_v1_close(t->h); break;
    case 2: zwlr_foreign_toplevel_handle_v1_set_minimized(t->h); break;
    case 3: zwlr_foreign_toplevel_handle_v1_set_maximized(t->h); break;
    case 4: zwlr_foreign_toplevel_handle_v1_unset_maximized(t->h); break;
    }
}

/* ---------------------------------------------------------------- X11: the active window */

static bool is_ours_x(Ecore_X_Window w)
{
    if (nv.backend != NV_BACKEND_X11)
        return false;
    if (w == elm_win_xwindow_get(nv.bar) || w == elm_win_xwindow_get(nv.backdrop))
        return true;
    Eina_List *l;
    Nv_Window *nw;
    EINA_LIST_FOREACH(nv.windows, l, nw)
        if (w == elm_win_xwindow_get(nw->win))
            return true;
    return false;
}

static void read_active(void)
{
    Ecore_X_Window w = 0;
    if (ecore_x_window_prop_window_get(gm.x_root, ECORE_X_ATOM_NET_ACTIVE_WINDOW, &w, 1) != 1)
        w = 0;
    gm.active_xid = is_ours_x(w) ? 0 : (unsigned)w;
}

static Eina_Bool on_x_property(void *data, int type, void *event)
{
    (void)data;
    (void)type;
    Ecore_X_Event_Window_Property *ev = event;
    if (ev->win == gm.x_root && ev->atom == ECORE_X_ATOM_NET_ACTIVE_WINDOW) {
        read_active();
        schedule_refresh();
    }
    return ECORE_CALLBACK_PASS_ON;
}

static void x_start(void)
{
    if (nv.backend == NV_BACKEND_WAYLAND) {
        /* XWayland's root window, when there is an X server for X11 programs */
        if (!getenv("DISPLAY") || !ecore_x_init(NULL))
            return;
    }
    gm.x_root = ecore_x_window_root_first_get();
    if (!gm.x_root)
        return;
    ecore_x_event_mask_set(gm.x_root, ECORE_X_EVENT_MASK_WINDOW_PROPERTY);
    ecore_event_handler_add(ECORE_X_EVENT_WINDOW_PROPERTY, on_x_property, NULL);
    gm.x_ok = true;
    read_active();
}

/* the X window's program name: WM_CLASS's class ("Gimp"), else its title */
static char *x_name(Ecore_X_Window w)
{
    char *name = NULL, *klass = NULL;
    ecore_x_icccm_name_class_get(w, &name, &klass);
    free(name);
    if (klass && *klass)
        return klass;
    free(klass);
    char *t = NULL;
    if (ecore_x_netwm_name_get(w, &t) && t)
        return t;
    return ecore_x_icccm_title_get(w);
}

/* ---------------------------------------------------------------- the bar */

/* "org.example.TextEditor" -> "TextEditor"; "gimp-2.10" stays */
static void pretty(const char *app_id, char *out, size_t len)
{
    const char *dot = strrchr(app_id, '.');
    snprintf(out, len, "%s", dot && dot[1] ? dot + 1 : app_id);
    if (out[0] >= 'a' && out[0] <= 'z')
        out[0] -= 32;
}

static Nv_Menu *foreign_window_menu;

static void refresh(void)
{
    if (nv_screenbar_menu_is_open()) {
        schedule_refresh(); /* not while a menu is open */
        return;
    }
    Eina_List *own = nv_screenbar_own_menus();
    App_Menu *am = gm.active_xid ? eina_hash_find(gm.apps, &gm.active_xid) : NULL;
    Eina_List *list = NULL;
    char title[256] = "";

    if (am) {
        app_connect(am);
        if (am->stale)
            read_layout(am);
        list = eina_list_append(NULL, eina_list_data_get(own)); /* the Nirvana Screen menu first */
        Eina_List *l;
        Nv_Menu *m;
        EINA_LIST_FOREACH(am->menus, l, m)
            list = eina_list_append(list, m);
        char *n = x_name(gm.active_xid);
        snprintf(title, sizeof title, "%s", n ? n : "");
        free(n);
    } else {
        Toplevel *t = active_toplevel();
        bool ours = !t || (t->app_id && !strncmp(t->app_id, "nirvana", 7));
        if (!ours) {
            /* a native Wayland program: its name, and a Window menu for its window */
            pretty(t->app_id && *t->app_id ? t->app_id : (t->title ? t->title : ""), title, sizeof title);
            if (!foreign_window_menu) {
                foreign_window_menu = nv_menu_new(_("Window"));
                Nv_Menu_Item *it;
                it = nv_menu_add(foreign_window_menu, _("_Close"), NULL, tl_act, NULL);
                it->id = 1;
                it = nv_menu_add(foreign_window_menu, _("_Iconify"), NULL, tl_act, NULL);
                it->id = 2;
                it = nv_menu_add(foreign_window_menu, _("_Zoom"), NULL, tl_act, NULL);
                it->id = 3;
                it = nv_menu_add(foreign_window_menu, _("_Unzoom"), NULL, tl_act, NULL);
                it->id = 4;
            }
            list = eina_list_append(NULL, eina_list_data_get(own));
            list = eina_list_append(list, foreign_window_menu);
            Eina_List *l = eina_list_next(eina_list_next(own)); /* Icons, Tools */
            for (; l; l = eina_list_next(l))
                list = eina_list_append(list, eina_list_data_get(l));
        }
    }
    gm.shown = am;
    Eina_List *old = gm.bar_list;
    gm.bar_list = list;
    nv_screenbar_menus_set(list, title[0] ? title : NULL);
    nv_screenbar_title_set(title[0] ? title : NULL);
    eina_list_free(old);
}

void nv_global_menu_closed(void)
{
    /* a menu was shut: what changed meanwhile is shown now */
    if (gm.shown && gm.shown->stale)
        read_layout(gm.shown);
}

bool nv_global_menu_start(Eldbus_Connection *bus)
{
    gm.apps = eina_hash_int32_new(app_free);
    gm.bus = bus;
    if (bus) {
        gm.iface = eldbus_service_interface_register(bus, REGISTRAR_PATH, &reg_desc);
        eldbus_name_request(bus, REGISTRAR_BUS, ELDBUS_NAME_REQUEST_FLAG_REPLACE_EXISTING, NULL, NULL);
    }
    x_start();
    if (nv.backend == NV_BACKEND_WAYLAND)
        foreign_start();
    refresh();
    return bus != NULL;
}
