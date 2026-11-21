/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop - the Nirvana Desktop shell: the screen bar ("Nirvana Screen"), the
 * backdrop with the volume icons, the listers and the Assigns editor.
 *
 *     nirvana-desktop [--theme NAME]
 *
 * It runs on Wayland, in a compositor with the layer shell (labwc, as nirvana-session
 * starts it; other wlroots compositors too). X11 programs are seen through XWayland.
 *
 * It answers on the session bus as org.nirvana.Desktop (/org/nirvana/Desktop):
 *   OpenDrawer(s path)      a drawer opened in a lister (wbrun uses it)
 *   ReloadAssigns()         the volume icons made again (as SIGUSR1 does)
 *   ShowAssignsEditor()
 */
#include "desktop.h"

#include <Ecore_File.h>
#include <Eldbus.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
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

/* a setting of ~/.nirvana/Prefs/desktop.conf ("key=value" lines), or def */
const char *nv_pref(const char *key, const char *def)
{
    static Eina_Hash *prefs;
    if (!prefs) {
        prefs = eina_hash_string_superfast_new(free);
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/.nirvana/Prefs/desktop.conf", getenv("HOME") ? getenv("HOME") : "");
        FILE *f = fopen(path, "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof line, f)) {
                line[strcspn(line, "\r\n")] = 0;
                char *eq = strchr(line, '=');
                if (!eq || line[0] == '#')
                    continue;
                *eq = 0;
                eina_hash_set(prefs, line, strdup(eq + 1));
            }
            fclose(f);
        }
    }
    const char *v = eina_hash_find(prefs, key);
    return v && *v ? v : def;
}

/* the theme named in desktop.conf ("theme=classic31"), else Ambient */
static void prefs_theme(char *out, size_t len)
{
    snprintf(out, len, "%.200s", nv_pref("theme", "ambient"));
}

static void prefs_path(char *out, size_t len)
{
    snprintf(out, len, "%s/.nirvana/Prefs/desktop.conf", getenv("HOME") ? getenv("HOME") : "");
}

bool nv_pref_set(const char *key, const char *value)
{
    char path[PATH_MAX], tmp[PATH_MAX + 8];
    prefs_path(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.new", path);
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/.nirvana/Prefs", getenv("HOME") ? getenv("HOME") : "");
    ecore_file_mkpath(dir);
    FILE *out = fopen(tmp, "w");
    if (!out)
        return false;
    /* the other lines kept as they are, the key's line replaced (or added) */
    size_t klen = strlen(key);
    bool done = false;
    FILE *in = fopen(path, "r");
    if (in) {
        char line[512];
        while (fgets(line, sizeof line, in)) {
            if (!strncmp(line, key, klen) && line[klen] == '=') {
                if (!done)
                    fprintf(out, "%s=%s\n", key, value);
                done = true;
            } else {
                fputs(line, out);
            }
        }
        fclose(in);
    }
    if (!done)
        fprintf(out, "%s=%s\n", key, value);
    bool ok = fclose(out) == 0 && rename(tmp, path) == 0;
    return ok;
}

/* ---------------------------------------------------------------- themes: listed, changed */

static char **saved_argv;
static char theme_name[256];

const char *nv_theme_name(void)
{
    return theme_name;
}

static int name_cmp(const void *a, const void *b)
{
    return strcasecmp(a, b);
}

Eina_List *nv_themes_list(void)
{
    Eina_List *names = NULL;
    char dirs[3][PATH_MAX];
    if (nirvana_to_posix("Themes:", dirs[0], sizeof dirs[0]) != 0)
        dirs[0][0] = 0;
    snprintf(dirs[1], sizeof dirs[1], "%s/Themes", NIRVANA_HOME);
    char exe[PATH_MAX];
    exe_dir(exe, sizeof exe);
    snprintf(dirs[2], sizeof dirs[2], "%.4000s/themes", exe);
    for (int i = 0; i < 3; i++) {
        if (!dirs[i][0])
            continue;
        Eina_List *files = ecore_file_ls(dirs[i]);
        char *f;
        EINA_LIST_FREE(files, f) {
            size_t n = strlen(f);
            if (n > 4 && !strcmp(f + n - 4, ".edj")) {
                f[n - 4] = 0;
                if (!eina_list_search_unsorted(names, name_cmp, f))
                    names = eina_list_sorted_insert(names, name_cmp, eina_stringshare_add(f));
            }
            free(f);
        }
    }
    return names;
}

void nv_theme_use(const char *name)
{
    if (!strcmp(name, theme_name))
        return;
    if (!nv_pref_set("theme", name)) {
        nv_message(_("Theme"), _("The choice could not be saved in ~/.nirvana/Prefs/desktop.conf."));
        return;
    }
    /* the desktop started again in the new theme (the same process: the session goes on) */
    Eina_Array *args = eina_array_new(8);
    for (int i = 0; saved_argv[i]; i++) {
        if (!strcmp(saved_argv[i], "--theme") && saved_argv[i + 1]) {
            i++;
            continue;
        }
        eina_array_push(args, saved_argv[i]);
    }
    eina_array_push(args, NULL);
    char **av = (char **)args->data;
    execv("/proc/self/exe", av);
    fprintf(stderr, "nirvana-desktop: could not start again: %s\n", strerror(errno));
    eina_array_free(args);
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

static void size_text(unsigned long long n, char *out, size_t len)
{
    if (n >= 1000ULL * 1000 * 1000)
        snprintf(out, len, "%.1f GB", (double)n / 1e9);
    else if (n >= 1000ULL * 1000)
        snprintf(out, len, "%.1f MB", (double)n / 1e6);
    else
        snprintf(out, len, "%.0f kB", (double)n / 1e3);
}

void nv_volume_information(const char *name, const char *posix)
{
    nv_info_show_volume(name, posix);
}

/* a line's value from a "Key: value" or KEY=value file */
static bool file_value(const char *path, const char *key, char sep, char *out, size_t len)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char line[512];
    size_t k = strlen(key);
    bool found = false;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, k))
            continue;
        char *v = line + k;
        while (*v == ' ' || *v == '\t')
            v++;
        if (*v != sep)
            continue;
        v++;
        while (*v == ' ' || *v == '"')
            v++;
        v[strcspn(v, "\"\n")] = 0;
        snprintf(out, len, "%s", v);
        found = true;
        break;
    }
    fclose(f);
    return found;
}

void nv_about_system(void)
{
    char os[256] = "Linux", cpu[256] = "?", mem[64] = "?", msg[2048];
    file_value("/etc/os-release", "PRETTY_NAME", '=', os, sizeof os);
    file_value("/proc/cpuinfo", "model name", ':', cpu, sizeof cpu);
    char kb[64];
    if (file_value("/proc/meminfo", "MemTotal", ':', kb, sizeof kb))
        size_text(strtoull(kb, NULL, 10) * 1024ULL, mem, sizeof mem);
    struct utsname u;
    uname(&u);
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    double up = 0;
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        if (fscanf(f, "%lf", &up) != 1)
            up = 0;
        fclose(f);
    }
    int days = (int)(up / 86400), hours = (int)(up / 3600) % 24, mins = (int)(up / 60) % 60;
    snprintf(msg, sizeof msg, "%s\n\n%s: %s %s (%s)\n%s: %s × %ld\n%s: %s\n%s: %dd %02d:%02d\n%s: %s", os,
             _("Kernel"), u.sysname, u.release, u.machine, _("Processor"), cpu, cpus, _("Memory"), mem, _("Up"), days,
             hours, mins, _("Computer"), u.nodename);
    nv_message(_("About the System"), msg);
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
    nv_new_shell_at(NULL);
}

void nv_new_shell_at(const char *dir)
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
                /* (where the shell starts: the drawer, else home) */
                if ((!dir || chdir(dir) != 0) && chdir(getenv("HOME") ? getenv("HOME") : "/") != 0) {
                }
                spawn_report(nirvana_spawn(argv), cands[i]);
                if (chdir(getenv("HOME") ? getenv("HOME") : "/") != 0) {
                }
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
    printf(_("Usage: nirvana-desktop [--theme NAME]\n"));
}

EAPI_MAIN int elm_main(int argc, char **argv)
{
    char theme[256];
    prefs_theme(theme, sizeof theme);
    saved_argv = argv;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--theme") && i + 1 < argc)
            snprintf(theme, sizeof theme, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--version")) {
            printf("nirvana-desktop %s\n", NIRVANA_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "--help")) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--wayland")) { /* (accepted: the only one) */
            usage();
            return 1;
        }
    }
    snprintf(theme_name, sizeof theme_name, "%s", theme);
    if (!nv_theme_find(theme, nv.theme, sizeof nv.theme) &&
        !(snprintf(theme_name, sizeof theme_name, "ambient"), nv_theme_find("ambient", nv.theme, sizeof nv.theme))) {
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
    if (!nv_dock_create())
        fprintf(stderr, "nirvana-desktop: no dock (the theme has none, or no layer for it)\n");
    ecore_event_handler_add(ECORE_EVENT_SIGNAL_USER, on_user_signal, NULL);
    dbus_start();
    nv_global_menu_start(bus);
    if (!nv_drives_start())
        fprintf(stderr, "nirvana-desktop: no system bus: no removable drives on the desktop\n");
    elm_run();
    if (bus)
        eldbus_connection_unref(bus);
    return 0;
}

/* elm_main is called by main(); the desktop's own windows on Wayland */
int main(int argc, char **argv)
{
    nirvana_i18n_init();
    if (!getenv("WAYLAND_DISPLAY")) {
        fprintf(stderr, "nirvana-desktop: not on Wayland (it runs in labwc: nirvana-session)\n");
        return 1;
    }
    setenv("ELM_DISPLAY", "wl", 1);
    if (!getenv("ELM_ACCEL"))
        setenv("ELM_ACCEL", "none", 0);
    elm_init(argc, argv);
    int r = elm_main(argc, argv);
    elm_shutdown();
    return r;
}
