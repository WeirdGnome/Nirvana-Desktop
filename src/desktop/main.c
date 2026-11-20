/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop - the Nirvana Desktop shell: the screen bar ("Nirvana Screen"), the
 * backdrop with the volume icons, the listers and the Assigns editor.
 *
 *     nirvana-desktop [--theme NAME] [--wayland | --x11]
 *
 * On Wayland it needs a compositor with the layer shell (wlroots based: labwc, sway,
 * river, wayfire...); on X11 any window manager that honours dock and desktop windows.
 *
 * It answers on the session bus as org.nirvana.Desktop (/org/nirvana/Desktop):
 *   OpenDrawer(s path)      a drawer opened in a lister (wbrun uses it)
 *   ReloadAssigns()         the volume icons made again (as SIGUSR1 does)
 *   ShowAssignsEditor()
 */
#include "desktop.h"

#include <Eldbus.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

Nv_Desktop nv;

/* ---------------------------------------------------------------- the theme */

static void exe_dir(char *out, size_t len)
{
    ssize_t n = readlink("/proc/self/exe", out, len - 1);
    if (n <= 0) {
        snprintf(out, len, ".");
        return;
    }
    out[n] = 0;
    char *slash = strrchr(out, '/');
    if (slash)
        *slash = 0;
}

static bool exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

bool nv_theme_find(const char *name, char *out, size_t len)
{
    if (strchr(name, '/'))
        return snprintf(out, len, "%s", name), exists(out);
    char file[PATH_MAX], tmp[PATH_MAX * 2];
    snprintf(file, sizeof file, "Themes:%s.edj", name);
    if (nirvana_to_posix(file, out, len) == 0 && exists(out))
        return true;
    snprintf(tmp, sizeof tmp, "%s/Themes/%s.edj", NIRVANA_HOME, name);
    if (exists(tmp))
        return snprintf(out, len, "%s", tmp), true;
    /* run from the build tree: build/themes/NAME.edj beside the program */
    char dir[PATH_MAX];
    exe_dir(dir, sizeof dir);
    snprintf(tmp, sizeof tmp, "%s/themes/%s.edj", dir, name);
    if (exists(tmp))
        return snprintf(out, len, "%s", tmp), true;
    return false;
}

Evas_Object *nv_edje_add(Evas_Object *parent, const char *group)
{
    Evas_Object *o = edje_object_add(evas_object_evas_get(parent));
    if (!edje_object_file_set(o, nv.theme, group)) {
        evas_object_del(o);
        return NULL;
    }
    return o;
}

const char *nv_theme_data(const char *key)
{
    /* the values kept for the program's life (a theme is not changed while it runs) */
    static Eina_Hash *cache;
    if (!cache)
        cache = eina_hash_string_superfast_new(free);
    char *v = eina_hash_find(cache, key);
    if (v)
        return v;
    v = edje_file_data_get(nv.theme, key);
    if (v)
        eina_hash_add(cache, key, v);
    return v;
}

/* the theme named in ~/.nirvana/Prefs/desktop.conf ("theme=classic31"), else Ambient */
static void prefs_theme(char *out, size_t len)
{
    snprintf(out, len, "ambient");
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/.nirvana/Prefs/desktop.conf", getenv("HOME") ? getenv("HOME") : "");
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[200];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "theme=", 6) && line[6])
            snprintf(out, len, "%.180s", line + 6);
    }
    fclose(f);
}

/* ---------------------------------------------------------------- small windows */

static void msg_ok(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    Nv_Window *w = data;
    evas_object_del(w->win);
}

void nv_message(const char *title, const char *text)
{
    Nv_Window *w = nv_window_new("nirvana-requester", title, 380, 140);
    Evas_Object *box = elm_box_add(w->win);
    elm_box_padding_set(box, 0, 10);
    evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    Evas_Object *lab = elm_label_add(w->win);
    elm_label_line_wrap_set(lab, ELM_WRAP_WORD);
    char *markup = elm_entry_utf8_to_markup(text);
    elm_object_text_set(lab, markup);
    free(markup);
    evas_object_size_hint_weight_set(lab, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(lab, EVAS_HINT_FILL, 0.5);
    elm_box_pack_end(box, lab);
    evas_object_show(lab);
    Evas_Object *b = elm_button_add(w->win);
    elm_object_text_set(b, _("OK"));
    evas_object_smart_callback_add(b, "clicked", msg_ok, w);
    elm_box_pack_end(box, b);
    evas_object_show(b);
    nv_window_content_set(w, box);
    evas_object_show(box);
    nv_window_show(w);
    elm_object_focus_set(b, EINA_TRUE);
}

static void quit_yes(void *data, Evas_Object *o, void *ev)
{
    (void)data;
    (void)o;
    (void)ev;
    elm_exit();
}

void nv_quit(void)
{
    Nv_Window *w = nv_window_new("nirvana-requester", _("Quit"), 400, 140);
    Evas_Object *box = elm_box_add(w->win);
    elm_box_padding_set(box, 0, 10);
    evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    Evas_Object *lab = elm_label_add(w->win);
    elm_object_text_set(lab, _("Do you really want to quit Nirvana Desktop?"));
    elm_box_pack_end(box, lab);
    evas_object_show(lab);
    Evas_Object *row = elm_box_add(w->win);
    elm_box_horizontal_set(row, EINA_TRUE);
    elm_box_homogeneous_set(row, EINA_TRUE);
    elm_box_padding_set(row, 10, 0);
    Evas_Object *b = elm_button_add(w->win);
    elm_object_text_set(b, _("Quit"));
    evas_object_smart_callback_add(b, "clicked", quit_yes, NULL);
    elm_box_pack_end(row, b);
    evas_object_show(b);
    b = elm_button_add(w->win);
    elm_object_text_set(b, _("Cancel"));
    evas_object_smart_callback_add(b, "clicked", msg_ok, w);
    elm_box_pack_end(row, b);
    evas_object_show(b);
    elm_box_pack_end(box, row);
    evas_object_show(row);
    nv_window_content_set(w, box);
    evas_object_show(box);
    nv_window_show(w);
}

void nv_about(void)
{
    nv_message(_("About"), "Nirvana Desktop " NIRVANA_VERSION "\n\n"
                           "Copyright © Nirvana Desktop contributors.\n"
                           "This program is free software: GNU GPL version 3 or later;\n"
                           "libnirvana: GNU LGPL version 3 or later.");
}

/* ---------------------------------------------------------------- starting things */

static void spawn_report(int r, const char *what)
{
    if (r >= 0)
        return;
    char msg[PATH_MAX + 128];
    snprintf(msg, sizeof msg, _("%s could not be started: %s"), what, strerror(-r));
    nv_message(_("Execute"), msg);
}

void nv_open_path(const char *posix_path)
{
    struct stat st;
    if (stat(posix_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        nv_lister_open(posix_path);
        return;
    }
    char wbrun[PATH_MAX + 8];
    exe_dir(wbrun, sizeof wbrun - 8);
    strcat(wbrun, "/wbrun");
    const char *argv[] = {access(wbrun, X_OK) == 0 ? wbrun : "wbrun", "--", posix_path, NULL};
    spawn_report(nirvana_spawn(argv), posix_path);
}

void nv_new_shell(void)
{
    const char *t = getenv("TERMINAL");
    const char *const cands[] = {t, "x-terminal-emulator", "foot", "alacritty", "kitty", "terminology",
                                 "xfce4-terminal", "konsole", "gnome-terminal", "lxterminal", "xterm", NULL};
    char path[PATH_MAX];
    const char *dirs = getenv("PATH");
    for (size_t i = 0; i < sizeof cands / sizeof *cands - 1; i++) {
        if (!cands[i] || !*cands[i])
            continue;
        char copy[4096];
        snprintf(copy, sizeof copy, "%s", dirs ? dirs : "/usr/bin:/bin");
        char *save = NULL;
        for (char *d = strtok_r(copy, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
            snprintf(path, sizeof path, "%s/%s", d, cands[i]);
            if (access(path, X_OK) == 0) {
                const char *argv[] = {path, NULL};
                if (chdir(getenv("HOME") ? getenv("HOME") : "/") != 0) { /* (where the shell starts) */
                }
                spawn_report(nirvana_spawn(argv), cands[i]);
                return;
            }
        }
    }
    nv_message(_("New Shell"), _("No terminal program was found. Set TERMINAL to one."));
}

static void exec_activated(void *data, Evas_Object *o, void *ev)
{
    (void)ev;
    Nv_Window *w = data;
    char *cmd = elm_entry_markup_to_utf8(elm_entry_entry_get(o));
    if (cmd && *cmd)
        spawn_report(nirvana_spawn_shell(cmd), cmd);
    free(cmd);
    evas_object_del(w->win);
}

void nv_execute_command(void)
{
    Nv_Window *w = nv_window_new("nirvana-requester", _("Execute Command"), 460, 110);
    Evas_Object *box = elm_box_add(w->win);
    elm_box_padding_set(box, 0, 6);
    evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    Evas_Object *lab = elm_label_add(w->win);
    elm_object_text_set(lab, _("Enter command and its arguments:"));
    evas_object_size_hint_align_set(lab, 0.0, 0.5);
    elm_box_pack_end(box, lab);
    evas_object_show(lab);
    Evas_Object *en = elm_entry_add(w->win);
    elm_entry_single_line_set(en, EINA_TRUE);
    elm_entry_scrollable_set(en, EINA_TRUE);
    evas_object_size_hint_weight_set(en, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(en, EVAS_HINT_FILL, 0.5);
    evas_object_smart_callback_add(en, "activated", exec_activated, w);
    elm_box_pack_end(box, en);
    evas_object_show(en);
    nv_window_content_set(w, box);
    evas_object_show(box);
    nv_window_show(w);
    elm_object_focus_set(en, EINA_TRUE);
}

/* ---------------------------------------------------------------- D-Bus */

static Eldbus_Connection *bus;

static Eldbus_Message *dbus_open_drawer(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
    (void)iface;
    const char *path;
    if (!eldbus_message_arguments_get(msg, "s", &path))
        return eldbus_message_error_new(msg, "org.freedesktop.DBus.Error.InvalidArgs", "a path expected");
    nv_lister_open(path);
    return eldbus_message_method_return_new(msg);
}

static Eldbus_Message *dbus_reload(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
    (void)iface;
    nv_backdrop_reload();
    return eldbus_message_method_return_new(msg);
}

static Eldbus_Message *dbus_editor(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
    (void)iface;
    nv_assign_editor_show();
    return eldbus_message_method_return_new(msg);
}

static const Eldbus_Method methods[] = {
    {"OpenDrawer", ELDBUS_ARGS({"s", "path"}), NULL, dbus_open_drawer, 0},
    {"ReloadAssigns", NULL, NULL, dbus_reload, 0},
    {"ShowAssignsEditor", NULL, NULL, dbus_editor, 0},
    {NULL, NULL, NULL, NULL, 0},
};

static const Eldbus_Service_Interface_Desc desc = {"org.nirvana.Desktop", methods, NULL, NULL, NULL, NULL};

static void dbus_start(void)
{
    if (!getenv("DBUS_SESSION_BUS_ADDRESS") && !getenv("XDG_RUNTIME_DIR"))
        return;
    eldbus_init();
    bus = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SESSION);
    if (!bus)
        return;
    eldbus_service_interface_register(bus, "/org/nirvana/Desktop", &desc);
    eldbus_name_request(bus, "org.nirvana.Desktop", ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE, NULL, NULL);
}

/* ---------------------------------------------------------------- signals */

static Eina_Bool on_user_signal(void *data, int type, void *event)
{
    (void)data;
    (void)type;
    Ecore_Event_Signal_User *ev = event;
    if (ev->number == 1)
        nv_backdrop_reload();
    return ECORE_CALLBACK_PASS_ON;
}

/* ---------------------------------------------------------------- main */

static void usage(void)
{
    printf(_("Usage: nirvana-desktop [--theme NAME] [--wayland | --x11]\n"));
}

EAPI_MAIN int elm_main(int argc, char **argv)
{
    char theme[256];
    prefs_theme(theme, sizeof theme);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--theme") && i + 1 < argc)
            snprintf(theme, sizeof theme, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--version")) {
            printf("nirvana-desktop %s\n", NIRVANA_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "--help")) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--wayland") && strcmp(argv[i], "--x11")) {
            usage();
            return 1;
        }
    }
    if (!nv_theme_find(theme, nv.theme, sizeof nv.theme) && !nv_theme_find("ambient", nv.theme, sizeof nv.theme)) {
        fprintf(stderr, "nirvana-desktop: no theme %s (looked in Themes:, %s/Themes and beside the program)\n", theme,
                NIRVANA_HOME);
        return 1;
    }
    elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_NONE);
    /* the widgets' colours: the palette the theme asks for (Ambient: "light") */
    const char *pal = nv_theme_data("nirvana/widgets/palette");
    if (pal && elm_config_palette_system_has(pal))
        elm_config_palette_set(pal);
    nirvana_launch_env(); /* the programs started from here get the global menu */

    /* the assigns: the defaults when the session's start has not made them */
    nirvana_assign_list l;
    if (nirvana_assign_list_load(&l) == 0)
        nirvana_assign_init();
    nirvana_assign_list_free(&l);

    if (!nv_backdrop_create()) {
        fprintf(stderr, "nirvana-desktop: the backdrop could not be made\n");
        return 1;
    }
    if (!nv_screenbar_create()) {
        fprintf(stderr, "nirvana-desktop: the screen bar could not be made\n");
        return 1;
    }
    ecore_event_handler_add(ECORE_EVENT_SIGNAL_USER, on_user_signal, NULL);
    dbus_start();
    elm_run();
    if (bus)
        eldbus_connection_unref(bus);
    return 0;
}

/* elm_main is called by main(); the display server chosen first */
int main(int argc, char **argv)
{
    nirvana_i18n_init();
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--wayland"))
            setenv("ELM_DISPLAY", "wl", 1);
        else if (!strcmp(argv[i], "--x11"))
            setenv("ELM_DISPLAY", "x11", 1);
    }
    if (!getenv("ELM_DISPLAY") && getenv("WAYLAND_DISPLAY"))
        setenv("ELM_DISPLAY", "wl", 1);
    if (!getenv("ELM_ACCEL"))
        setenv("ELM_ACCEL", "none", 0);
    elm_init(argc, argv);
    int r = elm_main(argc, argv);
    elm_shutdown();
    return r;
}
