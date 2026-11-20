/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: GTK programs' menus (org.gtk.Menus and org.gtk.Actions).
 *
 * GTK programs - through appmenu-gtk-module, or GtkApplication's own menubar - do not
 * use the registrar: they put their bus name and the menu's object path on their X11
 * window (_GTK_UNIQUE_BUS_NAME, _GTK_MENUBAR_OBJECT_PATH, and where the actions live:
 * _UNITY_OBJECT_PATH, _GTK_APPLICATION_OBJECT_PATH, _GTK_WINDOW_OBJECT_PATH) and export
 * a GMenuModel over D-Bus. The model comes in groups of menus (Start returns a group's
 * menus as lists of items; ":section" and ":submenu" refer to other menus); the items
 * name actions ("unity.-New", "app.quit", "win.save"), whose state and whether they are
 * enabled come from DescribeAll, and which are run with Activate.
 */
#include "desktop.h"
#include "gtk_menu.h"

#include <string.h>

typedef struct Item {
    char *label, *action, *accel;
    char target_type; /* 0: none, 's', 'i', 'b' */
    char *target_s;
    int target_i;
    bool has_section, has_submenu;
    unsigned sec_group, sec_menu, sub_group, sub_menu;
} Item;

typedef struct Action {
    bool enabled;
    char state_type; /* 0, 'b', 's' */
    bool state_b;
    char *state_s;
} Action;

typedef struct Action_Group {
    char *prefix; /* "unity", "app", "win" */
    char *path;
    Eldbus_Proxy *proxy;
    Eldbus_Object *obj;
    Eina_Hash *actions; /* name -> Action */
} Action_Group;

struct Gtk_App {
    Eldbus_Connection *bus;
    char *name, *menu_path;
    Eldbus_Object *obj;
    Eldbus_Proxy *menus;
    Eina_Hash *data;      /* "group/menu" -> Eina_List of Item */
    Eina_List *groups;    /* the groups subscribed to (uintptr) */
    Eina_List *agroups;   /* Action_Group */
    int pending;          /* replies awaited */
    Eina_List *nv_menus;  /* the menus built */
    Ecore_Timer *again;
    void (*changed)(void *data);
    void *changed_data;
    Eina_List *signals;
};

static void item_free(Item *it)
{
    free(it->label);
    free(it->action);
    free(it->accel);
    free(it->target_s);
    free(it);
}

static void list_free_items(void *l)
{
    Item *it;
    Eina_List *list = l;
    EINA_LIST_FREE(list, it)
        item_free(it);
}

static void action_free(void *p)
{
    Action *a = p;
    free(a->state_s);
    free(a);
}

/* ---------------------------------------------------------------- building the menus */

static Action *action_find(Gtk_App *app, const char *full)
{
    const char *dot = full ? strchr(full, '.') : NULL;
    if (!dot)
        return NULL;
    Eina_List *l;
    Action_Group *ag;
    EINA_LIST_FOREACH(app->agroups, l, ag)
        if (strlen(ag->prefix) == (size_t)(dot - full) && !strncmp(ag->prefix, full, (size_t)(dot - full)))
            return ag->actions ? eina_hash_find(ag->actions, dot + 1) : NULL;
    return NULL;
}

static Action_Group *group_of(Gtk_App *app, const char *full)
{
    const char *dot = full ? strchr(full, '.') : NULL;
    if (!dot)
        return NULL;
    Eina_List *l;
    Action_Group *ag;
    EINA_LIST_FOREACH(app->agroups, l, ag)
        if (strlen(ag->prefix) == (size_t)(dot - full) && !strncmp(ag->prefix, full, (size_t)(dot - full)))
            return ag;
    return NULL;
}

/* "<Control><Shift>s" -> "Ctrl+Shift+S" */
static void accel_text(const char *a, char *out, size_t len)
{
    out[0] = 0;
    if (!a)
        return;
    size_t n = 0;
    while (*a == '<') {
        const char *e = strchr(a, '>');
        if (!e)
            break;
        char mod[32];
        snprintf(mod, sizeof mod, "%.*s", (int)(e - a - 1), a + 1);
        const char *name = !strcasecmp(mod, "Control") || !strcasecmp(mod, "Primary") ? "Ctrl"
                           : !strcasecmp(mod, "Shift")                                  ? "Shift"
                           : !strcasecmp(mod, "Alt") || !strcasecmp(mod, "Mod1")      ? "Alt"
                           : !strcasecmp(mod, "Super")                                  ? "Super"
                                                                                        : mod;
        n += (size_t)snprintf(out + n, n < len ? len - n : 0, "%s+", name);
        a = e + 1;
    }
    if (n < len) {
        snprintf(out + n, len - n, "%s", a);
        if (strlen(a) == 1 && out[n] >= 'a' && out[n] <= 'z')
            out[n] -= 32;
    }
}

static Eina_List *menu_items(Gtk_App *app, unsigned group, unsigned menu)
{
    char key[32];
    snprintf(key, sizeof key, "%u/%u", group, menu);
    return eina_hash_find(app->data, key);
}

static void item_activate(void *data, Nv_Menu_Item *mi);

static void fill(Gtk_App *app, Nv_Menu *m, unsigned group, unsigned menu, int depth);

static void add_item(Gtk_App *app, Nv_Menu *m, Item *it, int depth)
{
    if (it->has_section) {
        /* a section: its items in place, with a separator between it and what is before */
        if (m->items && ((Nv_Menu_Item *)eina_list_last_data_get(m->items))->kind != NV_ITEM_SEPARATOR)
            nv_menu_add_separator(m);
        fill(app, m, it->sec_group, it->sec_menu, depth + 1);
        return;
    }
    if (it->has_submenu) {
        Nv_Menu *sub = nv_menu_new(it->label ? it->label : "");
        fill(app, sub, it->sub_group, it->sub_menu, depth + 1);
        nv_menu_add_submenu(m, it->label ? it->label : "", sub);
        return;
    }
    char acc[64];
    accel_text(it->accel, acc, sizeof acc);
    Nv_Menu_Item *mi = nv_menu_add(m, it->label ? it->label : "", acc[0] ? acc : NULL, item_activate, it);
    Action *a = action_find(app, it->action);
    mi->enabled = a ? a->enabled : false;
    if (a && a->state_type == 'b' && !it->target_type) {
        mi->kind = NV_ITEM_CHECK;
        mi->checked = a->state_b;
    } else if (a && a->state_type == 's' && it->target_type == 's') {
        mi->kind = NV_ITEM_RADIO;
        mi->checked = a->state_s && it->target_s && !strcmp(a->state_s, it->target_s);
    }
    /* (the item's App is found through the menu's about_data) */
}

static void fill(Gtk_App *app, Nv_Menu *m, unsigned group, unsigned menu, int depth)
{
    if (depth > 16)
        return;
    Eina_List *l, *items = menu_items(app, group, menu);
    Item *it;
    EINA_LIST_FOREACH(items, l, it)
        add_item(app, m, it, depth);
    /* no separator at the end */
    Nv_Menu_Item *last = eina_list_last_data_get(m->items);
    if (last && last->kind == NV_ITEM_SEPARATOR) {
        m->items = eina_list_remove_list(m->items, eina_list_last(m->items));
        free(last);
    }
}

static Gtk_App *activating; /* (the app whose menus are being built: items find it) */

static void build(Gtk_App *app)
{
    Eina_List *menus = NULL;
    /* the menubar: group 0, menu 0; its items (and its sections' items) are the menus */
    Nv_Menu *root = nv_menu_new("");
    fill(app, root, 0, 0, 0);
    Nv_Menu_Item *mi;
    EINA_LIST_FREE(root->items, mi) {
        if (mi->submenu) {
            free(mi->submenu->title);
            mi->submenu->title = strdup(mi->label ? mi->label : "");
            menus = eina_list_append(menus, mi->submenu);
            mi->submenu = NULL;
        }
        free(mi->label);
        free(mi->shortcut);
        free(mi);
    }
    nv_menu_free(root);
    /* the items' data points at their Item; the activation needs the app too */
    activating = app;
    Eina_List *old = app->nv_menus;
    app->nv_menus = menus;
    if (app->changed)
        app->changed(app->changed_data);
    Nv_Menu *m;
    EINA_LIST_FREE(old, m)
        nv_menu_free(m);
}

static void item_activate(void *data, Nv_Menu_Item *mi)
{
    (void)mi;
    Item *it = data;
    Gtk_App *app = activating;
    if (!app || !it->action)
        return;
    Action_Group *ag = group_of(app, it->action);
    if (!ag || !ag->proxy)
        return;
    const char *name = strchr(it->action, '.') + 1;
    Eldbus_Message *msg = eldbus_proxy_method_call_new(ag->proxy, "Activate");
    Eldbus_Message_Iter *iter = eldbus_message_iter_get(msg);
    eldbus_message_iter_basic_append(iter, 's', name);
    Eldbus_Message_Iter *av = eldbus_message_iter_container_new(iter, 'a', "v");
    if (it->target_type) {
        char sig[2] = {it->target_type, 0};
        Eldbus_Message_Iter *v = eldbus_message_iter_container_new(av, 'v', sig);
        if (it->target_type == 's')
            eldbus_message_iter_basic_append(v, 's', it->target_s ? it->target_s : "");
        else if (it->target_type == 'i')
            eldbus_message_iter_basic_append(v, 'i', it->target_i);
        else
            eldbus_message_iter_basic_append(v, 'b', (Eina_Bool)it->target_i);
        eldbus_message_iter_container_close(av, v);
    }
    eldbus_message_iter_container_close(iter, av);
    Eldbus_Message_Iter *pd = eldbus_message_iter_container_new(iter, 'a', "{sv}");
    eldbus_message_iter_container_close(iter, pd);
    eldbus_proxy_send(ag->proxy, msg, NULL, NULL, -1);
}

/* ---------------------------------------------------------------- reading */

static bool have_group(Gtk_App *app, unsigned g)
{
    return eina_list_data_find(app->groups, (void *)(uintptr_t)(g + 1)) != NULL;
}

static void start_groups(Gtk_App *app, Eina_List *groups);

static void parse_item(Eldbus_Message_Iter *dict, Item *it)
{
    Eldbus_Message_Iter *entry;
    while (eldbus_message_iter_get_and_next(dict, 'e', &entry)) {
        const char *key;
        Eldbus_Message_Iter *var;
        if (!eldbus_message_iter_arguments_get(entry, "sv", &key, &var))
            continue;
        char *sig = eldbus_message_iter_signature_get(var);
        if (!sig)
            continue;
        if (!strcmp(sig, "s")) {
            const char *s;
            eldbus_message_iter_arguments_get(var, "s", &s);
            if (!strcmp(key, "label"))
                it->label = strdup(s);
            else if (!strcmp(key, "action"))
                it->action = strdup(s);
            else if (!strcmp(key, "accel"))
                it->accel = strdup(s);
            else if (!strcmp(key, "target")) {
                it->target_type = 's';
                it->target_s = strdup(s);
            }
        } else if (!strcmp(sig, "(uu)")) {
            Eldbus_Message_Iter *st;
            unsigned a, b;
            if (eldbus_message_iter_arguments_get(var, "(uu)", &st) &&
                eldbus_message_iter_arguments_get(st, "uu", &a, &b)) {
                if (!strcmp(key, ":section")) {
                    it->has_section = true;
                    it->sec_group = a;
                    it->sec_menu = b;
                } else if (!strcmp(key, ":submenu")) {
                    it->has_submenu = true;
                    it->sub_group = a;
                    it->sub_menu = b;
                }
            }
        } else if (!strcmp(key, "target") && (!strcmp(sig, "i") || !strcmp(sig, "b"))) {
            it->target_type = sig[0];
            if (sig[0] == 'i')
                eldbus_message_iter_arguments_get(var, "i", &it->target_i);
            else {
                Eina_Bool b;
                eldbus_message_iter_arguments_get(var, "b", &b);
                it->target_i = b;
            }
        }
        free(sig);
    }
}

static void maybe_build(Gtk_App *app)
{
    if (app->pending > 0)
        return;
    /* the groups referred to but not read yet */
    Eina_List *missing = NULL;
    Eina_Iterator *iter = eina_hash_iterator_data_new(app->data);
    Eina_List *items;
    EINA_ITERATOR_FOREACH(iter, items) {
        Eina_List *l;
        Item *it;
        EINA_LIST_FOREACH(items, l, it) {
            unsigned g = it->has_section ? it->sec_group : it->has_submenu ? it->sub_group : 0;
            if ((it->has_section || it->has_submenu) && !have_group(app, g) &&
                !eina_list_data_find(missing, (void *)(uintptr_t)(g + 1)))
                missing = eina_list_append(missing, (void *)(uintptr_t)(g + 1));
        }
    }
    eina_iterator_free(iter);
    if (missing) {
        start_groups(app, missing);
        eina_list_free(missing);
        return;
    }
    build(app);
}

static void start_reply(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
    (void)pending;
    Gtk_App *app = data;
    app->pending--;
    const char *en, *et;
    if (!eldbus_message_error_get(msg, &en, &et)) {
        Eldbus_Message_Iter *arr, *st;
        if (eldbus_message_arguments_get(msg, "a(uuaa{sv})", &arr)) {
            while (eldbus_message_iter_get_and_next(arr, 'r', &st)) {
                unsigned g, m;
                Eldbus_Message_Iter *items, *dict;
                if (!eldbus_message_iter_arguments_get(st, "uuaa{sv}", &g, &m, &items))
                    continue;
                Eina_List *list = NULL;
                while (eldbus_message_iter_get_and_next(items, 'a', &dict)) {
                    Item *it = calloc(1, sizeof *it);
                    parse_item(dict, it);
                    list = eina_list_append(list, it);
                }
                char key[32];
                snprintf(key, sizeof key, "%u/%u", g, m);
                Eina_List *old = eina_hash_find(app->data, key);
                if (old)
                    eina_hash_modify(app->data, key, list), list_free_items(old);
                else
                    eina_hash_add(app->data, key, list);
            }
        }
    }
    maybe_build(app);
}

static void start_groups(Gtk_App *app, Eina_List *groups)
{
    Eldbus_Message *msg = eldbus_proxy_method_call_new(app->menus, "Start");
    Eldbus_Message_Iter *iter = eldbus_message_iter_get(msg);
    Eldbus_Message_Iter *arr = eldbus_message_iter_container_new(iter, 'a', "u");
    Eina_List *l;
    void *g;
    EINA_LIST_FOREACH(groups, l, g) {
        eldbus_message_iter_basic_append(arr, 'u', (unsigned)((uintptr_t)g - 1));
        if (!eina_list_data_find(app->groups, g))
            app->groups = eina_list_append(app->groups, g);
    }
    eldbus_message_iter_container_close(iter, arr);
    app->pending++;
    eldbus_proxy_send(app->menus, msg, start_reply, app, 5.0);
}

static void describe_reply(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
    (void)pending;
    Action_Group *ag = data;
    const char *en, *et;
    if (eldbus_message_error_get(msg, &en, &et))
        return;
    Eldbus_Message_Iter *dict, *entry;
    if (!eldbus_message_arguments_get(msg, "a{s(bgav)}", &dict))
        return;
    Eina_Hash *h = eina_hash_string_superfast_new(action_free);
    while (eldbus_message_iter_get_and_next(dict, 'e', &entry)) {
        const char *name, *psig;
        Eldbus_Message_Iter *st, *states, *v;
        Eina_Bool enabled;
        if (!eldbus_message_iter_arguments_get(entry, "s(bgav)", &name, &st) ||
            !eldbus_message_iter_arguments_get(st, "bgav", &enabled, &psig, &states))
            continue;
        Action *a = calloc(1, sizeof *a);
        a->enabled = enabled;
        if (eldbus_message_iter_get_and_next(states, 'v', &v)) {
            char *sig = eldbus_message_iter_signature_get(v);
            if (sig && !strcmp(sig, "b")) {
                Eina_Bool b;
                eldbus_message_iter_arguments_get(v, "b", &b);
                a->state_type = 'b';
                a->state_b = b;
            } else if (sig && !strcmp(sig, "s")) {
                const char *s;
                eldbus_message_iter_arguments_get(v, "s", &s);
                a->state_type = 's';
                a->state_s = strdup(s);
            }
            free(sig);
        }
        eina_hash_add(h, name, a);
    }
    if (ag->actions)
        eina_hash_free(ag->actions);
    ag->actions = h;
}

/* ---------------------------------------------------------------- changes */

static void describe(Gtk_App *app, Action_Group *ag);

static Eina_Bool again_cb(void *data)
{
    Gtk_App *app = data;
    app->again = NULL;
    if (nv_screenbar_menu_is_open()) {
        app->again = ecore_timer_add(0.5, again_cb, app);
        return ECORE_CALLBACK_CANCEL;
    }
    Eina_List *l;
    Action_Group *ag;
    EINA_LIST_FOREACH(app->agroups, l, ag)
        if (ag->proxy)
            describe(app, ag);
    /* everything read again (the groups' contents come whole) */
    if (app->groups)
        start_groups(app, app->groups);
    return ECORE_CALLBACK_CANCEL;
}

static void schedule_again(Gtk_App *app)
{
    if (!app->again)
        app->again = ecore_timer_add(0.1, again_cb, app);
}

static void on_menus_changed(void *data, const Eldbus_Message *msg)
{
    (void)msg;
    schedule_again(data);
}

typedef struct Ag_Ctx {
    Gtk_App *app;
    Action_Group *ag;
} Ag_Ctx;

static void describe_reply2(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
    Ag_Ctx *c = data;
    describe_reply(c->ag, msg, pending);
    c->app->pending--;
    maybe_build(c->app);
    free(c);
}

static void describe(Gtk_App *app, Action_Group *ag)
{
    Ag_Ctx *c = malloc(sizeof *c);
    c->app = app;
    c->ag = ag;
    app->pending++;
    eldbus_proxy_call(ag->proxy, "DescribeAll", describe_reply2, c, 5.0, "");
}

static void on_actions_changed(void *data, const Eldbus_Message *msg)
{
    (void)msg;
    Ag_Ctx *c = data;
    if (nv_screenbar_menu_is_open()) {
        schedule_again(c->app);
        return;
    }
    describe(c->app, c->ag);
}

/* ---------------------------------------------------------------- the interface */

Gtk_App *nv_gtk_app_new(Eldbus_Connection *bus, const char *name, const char *menu_path, const char *const paths[3],
                        void (*changed)(void *data), void *data)
{
    if (!bus || !name || !menu_path)
        return NULL;
    Gtk_App *app = calloc(1, sizeof *app);
    app->bus = bus;
    app->name = strdup(name);
    app->menu_path = strdup(menu_path);
    app->changed = changed;
    app->changed_data = data;
    app->data = eina_hash_string_superfast_new(list_free_items);
    app->obj = eldbus_object_get(bus, name, menu_path);
    app->menus = eldbus_proxy_get(app->obj, "org.gtk.Menus");
    app->signals = eina_list_append(app->signals,
                                    eldbus_proxy_signal_handler_add(app->menus, "Changed", on_menus_changed, app));
    static const char *const prefixes[3] = {"unity", "app", "win"};
    for (int i = 0; i < 3; i++) {
        if (!paths[i] || !*paths[i])
            continue;
        Action_Group *ag = calloc(1, sizeof *ag);
        ag->prefix = strdup(prefixes[i]);
        ag->path = strdup(paths[i]);
        ag->obj = eldbus_object_get(bus, name, paths[i]);
        ag->proxy = eldbus_proxy_get(ag->obj, "org.gtk.Actions");
        app->agroups = eina_list_append(app->agroups, ag);
        Ag_Ctx *c = malloc(sizeof *c);
        c->app = app;
        c->ag = ag;
        app->signals = eina_list_append(app->signals,
                                        eldbus_proxy_signal_handler_add(ag->proxy, "Changed", on_actions_changed, c));
        describe(app, ag);
    }
    Eina_List *g = eina_list_append(NULL, (void *)(uintptr_t)1); /* group 0 */
    start_groups(app, g);
    eina_list_free(g);
    return app;
}

Eina_List *nv_gtk_app_menus(Gtk_App *app)
{
    if (app)
        activating = app;
    return app ? app->nv_menus : NULL;
}

void nv_gtk_app_free(Gtk_App *app)
{
    if (!app)
        return;
    if (activating == app)
        activating = NULL;
    if (app->again)
        ecore_timer_del(app->again);
    Eldbus_Signal_Handler *sh;
    EINA_LIST_FREE(app->signals, sh)
        eldbus_signal_handler_del(sh);
    Action_Group *ag;
    EINA_LIST_FREE(app->agroups, ag) {
        if (ag->actions)
            eina_hash_free(ag->actions);
        eldbus_proxy_unref(ag->proxy);
        eldbus_object_unref(ag->obj);
        free(ag->prefix);
        free(ag->path);
        free(ag);
    }
    Nv_Menu *m;
    EINA_LIST_FREE(app->nv_menus, m)
        nv_menu_free(m);
    eina_hash_free(app->data);
    eina_list_free(app->groups);
    eldbus_proxy_unref(app->menus);
    eldbus_object_unref(app->obj);
    free(app->name);
    free(app->menu_path);
    free(app);
}

const char *nv_gtk_app_bus_name(const Gtk_App *app)
{
    return app ? app->name : "";
}
