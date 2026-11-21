/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the drives that come and go - USB sticks, memory cards, discs - read
 * from UDisks2 on the system bus.
 *
 * A filesystem is shown when UDisks2 does not call it a system one (HintSystem) or one to
 * ignore (HintIgnore): what a desktop shows for removable media. Its name is its label,
 * else its drive's vendor and model, else its size. Opening one mounts it first (through
 * UDisks2, so the user's session may do it without a password); Eject unmounts it and,
 * when the drive can, ejects the medium or powers the drive off.
 *
 * The list is read again (a moment after) whenever UDisks2 says an object or a property
 * changed; the backdrop is told (nv_backdrop_reload).
 *
 * NIRVANA_UDISKS_BUS=session looks for UDisks2 on the session bus instead (for tests).
 */
#include "desktop.h"

#include <string.h>

#define UD_BUS "org.freedesktop.UDisks2"
#define UD_PATH "/org/freedesktop/UDisks2"
#define UD_BLOCK UD_BUS ".Block"
#define UD_FS UD_BUS ".Filesystem"
#define UD_DRIVE UD_BUS ".Drive"

/* what is read of one object */
typedef struct Obj {
    char *path;
    bool block, fs, drive;
    /* Block */
    char *label, *device, *drive_path, *hint_name;
    bool ignore, system;
    unsigned long long size;
    /* Filesystem */
    char *mount;
    /* Drive */
    char *vendor, *model;
    bool ejectable, can_power_off, removable;
} Obj;

static struct {
    Eldbus_Connection *conn;
    Eldbus_Object *manager;
    Eina_List *drives; /* Nv_Drive */
    Ecore_Timer *again;
    Eldbus_Pending *pending;
} dr;

/* ---------------------------------------------------------------- reading */

static void obj_free(Obj *o)
{
    free(o->path);
    free(o->label);
    free(o->device);
    free(o->drive_path);
    free(o->hint_name);
    free(o->mount);
    free(o->vendor);
    free(o->model);
    free(o);
}

static char *bytes_string(Eldbus_Message_Iter *it)
{
    const char *bytes = NULL;
    int n = 0;
    if (!eldbus_message_iter_fixed_array_get(it, 'y', &bytes, &n) || n <= 0)
        return NULL;
    return strndup(bytes, (size_t)n);
}

/* one property of a{sv}: the value's iterator handed to the reader */
static void read_props(Eldbus_Message_Iter *dict, Obj *o, const char *iface)
{
    Eldbus_Message_Iter *entry;
    while (eldbus_message_iter_get_and_next(dict, 'e', &entry)) {
        const char *key;
        Eldbus_Message_Iter *var;
        if (!eldbus_message_iter_arguments_get(entry, "sv", &key, &var))
            continue;
        char *sig = eldbus_message_iter_signature_get(var);
        const char *s;
        Eina_Bool b;
        uint64_t t;
        if (!strcmp(iface, UD_BLOCK)) {
            if (!strcmp(key, "IdLabel") && !strcmp(sig, "s") && eldbus_message_iter_arguments_get(var, "s", &s))
                o->label = *s ? strdup(s) : NULL;
            else if (!strcmp(key, "HintName") && !strcmp(sig, "s") && eldbus_message_iter_arguments_get(var, "s", &s))
                o->hint_name = *s ? strdup(s) : NULL;
            else if (!strcmp(key, "Drive") && !strcmp(sig, "o") && eldbus_message_iter_arguments_get(var, "o", &s))
                o->drive_path = strcmp(s, "/") ? strdup(s) : NULL;
            else if (!strcmp(key, "HintIgnore") && !strcmp(sig, "b") && eldbus_message_iter_arguments_get(var, "b", &b))
                o->ignore = b;
            else if (!strcmp(key, "HintSystem") && !strcmp(sig, "b") && eldbus_message_iter_arguments_get(var, "b", &b))
                o->system = b;
            else if (!strcmp(key, "Size") && !strcmp(sig, "t") && eldbus_message_iter_arguments_get(var, "t", &t))
                o->size = t;
            else if (!strcmp(key, "PreferredDevice") && !strcmp(sig, "ay")) {
                Eldbus_Message_Iter *a;
                if (eldbus_message_iter_arguments_get(var, "ay", &a)) {
                    free(o->device);
                    o->device = bytes_string(a);
                }
            }
        } else if (!strcmp(iface, UD_FS)) {
            if (!strcmp(key, "MountPoints") && !strcmp(sig, "aay")) {
                Eldbus_Message_Iter *outer, *a;
                if (eldbus_message_iter_arguments_get(var, "aay", &outer) &&
                    eldbus_message_iter_get_and_next(outer, 'a', &a))
                    o->mount = bytes_string(a); /* (the first mount point) */
            }
        } else if (!strcmp(iface, UD_DRIVE)) {
            if (!strcmp(key, "Vendor") && !strcmp(sig, "s") && eldbus_message_iter_arguments_get(var, "s", &s))
                o->vendor = *s ? strdup(s) : NULL;
            else if (!strcmp(key, "Model") && !strcmp(sig, "s") && eldbus_message_iter_arguments_get(var, "s", &s))
                o->model = *s ? strdup(s) : NULL;
            else if (!strcmp(key, "Ejectable") && !strcmp(sig, "b") && eldbus_message_iter_arguments_get(var, "b", &b))
                o->ejectable = b;
            else if (!strcmp(key, "CanPowerOff") && !strcmp(sig, "b") && eldbus_message_iter_arguments_get(var, "b", &b))
                o->can_power_off = b;
            else if (!strcmp(key, "Removable") && !strcmp(sig, "b") && eldbus_message_iter_arguments_get(var, "b", &b))
                o->removable = b;
        }
        free(sig);
    }
}

static Obj *find_obj(Eina_List *objs, const char *path)
{
    Eina_List *l;
    Obj *o;
    EINA_LIST_FOREACH(objs, l, o)
        if (path && !strcmp(o->path, path))
            return o;
    return NULL;
}

static void drive_free(Nv_Drive *d)
{
    free(d->path);
    free(d->label);
    free(d->mount);
    free(d->drive);
    free(d);
}

static char *size_name(unsigned long long size)
{
    char buf[64];
    if (size >= 1000ULL * 1000 * 1000)
        snprintf(buf, sizeof buf, _("%.1f GB Volume"), (double)size / 1e9);
    else
        snprintf(buf, sizeof buf, _("%.0f MB Volume"), (double)size / 1e6);
    return strdup(buf);
}

static int drive_cmp(const void *a, const void *b)
{
    const Nv_Drive *x = a, *y = b;
    return strcmp(x->path, y->path);
}

static void on_objects(void *data, const Eldbus_Message *msg, Eldbus_Pending *p)
{
    (void)data;
    (void)p;
    dr.pending = NULL;
    const char *err, *errmsg;
    if (eldbus_message_error_get(msg, &err, &errmsg)) {
        if (getenv("NIRVANA_DEBUG"))
            fprintf(stderr, "drives: %s: %s\n", err, errmsg);
        return;
    }
    Eina_List *objs = NULL;
    Eldbus_Message_Iter *top, *entry;
    if (!eldbus_message_arguments_get(msg, "a{oa{sa{sv}}}", &top))
        return;
    while (eldbus_message_iter_get_and_next(top, 'e', &entry)) {
        const char *path;
        Eldbus_Message_Iter *ifaces, *ie;
        if (!eldbus_message_iter_arguments_get(entry, "oa{sa{sv}}", &path, &ifaces))
            continue;
        Obj *o = calloc(1, sizeof *o);
        o->path = strdup(path);
        while (eldbus_message_iter_get_and_next(ifaces, 'e', &ie)) {
            const char *iface;
            Eldbus_Message_Iter *props;
            if (!eldbus_message_iter_arguments_get(ie, "sa{sv}", &iface, &props))
                continue;
            if (!strcmp(iface, UD_BLOCK))
                o->block = true;
            else if (!strcmp(iface, UD_FS))
                o->fs = true;
            else if (!strcmp(iface, UD_DRIVE))
                o->drive = true;
            else
                continue;
            read_props(props, o, iface);
        }
        objs = eina_list_append(objs, o);
    }

    /* the filesystems to show, as drives */
    Nv_Drive *d;
    EINA_LIST_FREE(dr.drives, d)
        drive_free(d);
    Eina_List *l;
    Obj *o;
    EINA_LIST_FOREACH(objs, l, o) {
        if (!o->block || !o->fs || o->ignore || o->system)
            continue;
        Obj *drv = find_obj(objs, o->drive_path);
        d = calloc(1, sizeof *d);
        d->path = strdup(o->path);
        d->mount = o->mount ? strdup(o->mount) : NULL;
        d->drive = o->drive_path ? strdup(o->drive_path) : NULL;
        d->can_eject = drv && (drv->ejectable || drv->can_power_off);
        /* a medium is ejected (a disc, a card reader's card); a stick's drive powered off */
        d->power_off = drv && !drv->ejectable && drv->can_power_off;
        if (o->label)
            d->label = strdup(o->label);
        else if (o->hint_name)
            d->label = strdup(o->hint_name);
        else if (drv && (drv->vendor || drv->model)) {
            char buf[256];
            snprintf(buf, sizeof buf, "%s%s%s", drv->vendor ? drv->vendor : "",
                     drv->vendor && drv->model ? " " : "", drv->model ? drv->model : "");
            d->label = strdup(buf);
        } else {
            d->label = size_name(o->size);
        }
        dr.drives = eina_list_sorted_insert(dr.drives, drive_cmp, d);
    }
    EINA_LIST_FREE(objs, o)
        obj_free(o);
    if (getenv("NIRVANA_DEBUG"))
        fprintf(stderr, "drives: %u shown\n", eina_list_count(dr.drives));
    nv_backdrop_reload();
}

static void read_all(void)
{
    if (dr.pending)
        eldbus_pending_cancel(dr.pending);
    Eldbus_Message *m = eldbus_object_method_call_new(dr.manager, "org.freedesktop.DBus.ObjectManager",
                                                      "GetManagedObjects");
    dr.pending = eldbus_object_send(dr.manager, m, on_objects, NULL, -1);
}

static Eina_Bool read_again(void *data)
{
    (void)data;
    dr.again = NULL;
    read_all();
    return ECORE_CALLBACK_CANCEL;
}

/* something changed: read a moment after (a stick brings several signals at once) */
static void changed(void *data, const Eldbus_Message *msg)
{
    (void)data;
    (void)msg;
    if (dr.again)
        ecore_timer_reset(dr.again);
    else
        dr.again = ecore_timer_add(0.3, read_again, NULL);
}

bool nv_drives_start(void)
{
    const char *bus = getenv("NIRVANA_UDISKS_BUS");
    dr.conn = eldbus_connection_get(bus && !strcmp(bus, "session") ? ELDBUS_CONNECTION_TYPE_SESSION
                                                                   : ELDBUS_CONNECTION_TYPE_SYSTEM);
    if (!dr.conn)
        return false;
    dr.manager = eldbus_object_get(dr.conn, UD_BUS, UD_PATH);
    eldbus_signal_handler_add(dr.conn, UD_BUS, UD_PATH, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded",
                              changed, NULL);
    eldbus_signal_handler_add(dr.conn, UD_BUS, UD_PATH, "org.freedesktop.DBus.ObjectManager",
                              "InterfacesRemoved", changed, NULL);
    eldbus_signal_handler_add(dr.conn, UD_BUS, NULL, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                              changed, NULL);
    /* (UDisks2 is started on demand: the call starts it) */
    read_all();
    return true;
}

const Eina_List *nv_drives_list(void)
{
    return dr.drives;
}

Nv_Drive *nv_drive_find(const char *path)
{
    Eina_List *l;
    Nv_Drive *d;
    EINA_LIST_FOREACH(dr.drives, l, d)
        if (!strcmp(d->path, path))
            return d;
    return NULL;
}

/* ---------------------------------------------------------------- acting */

static bool failed(const Eldbus_Message *msg, const char *what)
{
    const char *err, *errmsg;
    if (!eldbus_message_error_get(msg, &err, &errmsg))
        return false;
    char text[1024];
    snprintf(text, sizeof text, "%s\n\n%s", what, errmsg && *errmsg ? errmsg : err);
    nv_message(_("Drives"), text);
    return true;
}

/* an empty a{sv} of options */
static Eldbus_Message *call_new(const char *path, const char *iface, const char *method)
{
    Eldbus_Message *m = eldbus_message_method_call_new(UD_BUS, path, iface, method);
    Eldbus_Message_Iter *it = eldbus_message_iter_get(m), *dict;
    eldbus_message_iter_arguments_append(it, "a{sv}", &dict);
    eldbus_message_iter_container_close(it, dict);
    return m;
}

static void on_mounted(void *data, const Eldbus_Message *msg, Eldbus_Pending *p)
{
    (void)p;
    char *label = data;
    const char *where;
    if (!failed(msg, _("The volume could not be mounted.")) && eldbus_message_arguments_get(msg, "s", &where))
        nv_lister_open(where);
    free(label);
}

void nv_drive_open(Nv_Drive *d)
{
    if (d->mount) {
        nv_lister_open(d->mount);
        return;
    }
    eldbus_connection_send(dr.conn, call_new(d->path, UD_FS, "Mount"), on_mounted, strdup(d->label), -1);
}

static void on_ejected(void *data, const Eldbus_Message *msg, Eldbus_Pending *p)
{
    (void)data;
    (void)p;
    failed(msg, _("The drive could not be ejected (the volume is unmounted: it may be taken out)."));
}

typedef struct Eject {
    char *drive;
    bool power_off;
} Eject;

static void eject_send(Eject *e)
{
    eldbus_connection_send(dr.conn, call_new(e->drive, UD_DRIVE, e->power_off ? "PowerOff" : "Eject"), on_ejected,
                           NULL, -1);
}

static void on_unmounted(void *data, const Eldbus_Message *msg, Eldbus_Pending *p)
{
    (void)p;
    Eject *e = data;
    if (!failed(msg, _("The volume could not be unmounted (is a program still using it?).")) && e->drive)
        eject_send(e);
    free(e->drive);
    free(e);
}

void nv_drive_eject(Nv_Drive *d)
{
    Eject *e = calloc(1, sizeof *e);
    e->drive = d->can_eject && d->drive ? strdup(d->drive) : NULL;
    e->power_off = d->power_off;
    if (d->mount) {
        eldbus_connection_send(dr.conn, call_new(d->path, UD_FS, "Unmount"), on_unmounted, e, -1);
        return;
    }
    if (e->drive)
        eject_send(e);
    free(e->drive);
    free(e);
}
