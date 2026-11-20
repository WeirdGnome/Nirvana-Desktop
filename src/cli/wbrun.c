/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * wbrun - something opened as the desktop opens it.
 *
 *     wbrun PATH [ARGUMENTS...]
 *
 * PATH may be a Nirvana path ("Downloads:", "Tools:org.example.App.desktop") or a POSIX
 * path. A drawer (directory) opens in the desktop's lister (D-Bus: org.nirvana.Desktop,
 * OpenDrawer); a .desktop file is started; a program (an AppImage too) is run with the
 * arguments; any other file is opened with the default program of its MIME type
 * (mimeapps.list, else the first program that says it can). Programs get the global
 * menu's environment.
 *
 *     --with ID        open with this program (a desktop file id or path)
 *     --print          print the command instead of running it
 *
 * Exit status: 0, 1 for a bad call, 5 when the path or a program is not found, 10 on
 * another failure.
 */
#define _GNU_SOURCE
#include "../common/i18n.h"
#include "nirvana.h"

#include <Ecore.h>
#include <Efreet.h>
#include <Efreet_Mime.h>
#include <Eldbus.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef NIRVANA_VERSION
#define NIRVANA_VERSION "0"
#endif

#define DESKTOP_BUS "org.nirvana.Desktop"
#define DESKTOP_PATH "/org/nirvana/Desktop"
#define DESKTOP_IFACE "org.nirvana.Desktop"

static bool print_only;
static int status = 0;

static void usage(FILE *f)
{
    fprintf(f, _("Usage: wbrun [--with PROGRAM] [--print] PATH [ARGUMENTS...]\n"
                 "Opens PATH (a Nirvana or POSIX path) as the desktop opens it.\n"));
}

/* ---------------------------------------------------------------- a drawer */

static void drawer_reply(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending)
{
    (void)pending;
    bool *ok = data;
    const char *name, *text;
    *ok = !eldbus_message_error_get(msg, &name, &text);
    ecore_main_loop_quit();
}

static Eina_Bool drawer_timeout(void *data)
{
    (void)data;
    ecore_main_loop_quit();
    return ECORE_CALLBACK_CANCEL;
}

/* the desktop asked to open a drawer; false when it is not running */
static bool open_drawer(const char *path)
{
    if (print_only) {
        printf("dbus %s %s %s.OpenDrawer %s\n", DESKTOP_BUS, DESKTOP_PATH, DESKTOP_IFACE, path);
        return true;
    }
    /* no session bus: no desktop to ask */
    const char *addr = getenv("DBUS_SESSION_BUS_ADDRESS");
    if (!addr || !*addr) {
        const char *rt = getenv("XDG_RUNTIME_DIR");
        char sock[PATH_MAX];
        snprintf(sock, sizeof sock, "%s/bus", rt ? rt : "");
        if (!rt || access(sock, F_OK) != 0)
            return false;
    }
    eldbus_init();
    eina_log_domain_level_set("eldbus", EINA_LOG_LEVEL_CRITICAL);
    Eldbus_Connection *c = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SESSION);
    bool ok = false;
    if (c) {
        Eldbus_Message *m = eldbus_message_method_call_new(DESKTOP_BUS, DESKTOP_PATH, DESKTOP_IFACE, "OpenDrawer");
        eldbus_message_arguments_append(m, "s", path);
        if (eldbus_connection_send(c, m, drawer_reply, &ok, 3.0)) {
            Ecore_Timer *t = ecore_timer_add(3.5, drawer_timeout, NULL);
            ecore_main_loop_begin();
            ecore_timer_del(t);
        }
        eldbus_connection_unref(c);
    }
    eldbus_shutdown();
    return ok;
}

/* ---------------------------------------------------------------- programs */

static void *command_cb(void *data, Efreet_Desktop *desktop, char *command, int remaining)
{
    (void)data;
    (void)desktop;
    (void)remaining;
    if (print_only) {
        printf("%s\n", command);
        return NULL;
    }
    int r = nirvana_spawn_shell(command);
    if (r < 0) {
        fprintf(stderr, "wbrun: %s: %s\n", command, strerror(-r));
        status = 10;
    }
    return NULL;
}

/* a desktop entry started with files (a list of paths) */
static int start_desktop(Efreet_Desktop *d, Eina_List *files)
{
    if (!d->exec) {
        fprintf(stderr, _("wbrun: %s starts no program\n"), d->orig_path ? d->orig_path : "?");
        return 10;
    }
    efreet_desktop_command_get(d, files, command_cb, NULL);
    return status;
}

/* a desktop file id or path */
static Efreet_Desktop *find_desktop(const char *id)
{
    if (strchr(id, '/'))
        return efreet_desktop_get(id);
    return efreet_util_desktop_file_id_find(id);
}

static int open_with(const char *mime_or_null, const char *with, const char *file)
{
    Efreet_Desktop *d = NULL;
    char id[512];
    if (with)
        d = find_desktop(with);
    else if (mime_or_null && nirvana_mime_default(mime_or_null, id, sizeof id) == 0)
        d = find_desktop(id);
    if (!d && !with && mime_or_null) {
        Eina_List *l = efreet_util_desktop_mime_list(mime_or_null);
        Efreet_Desktop *cand;
        EINA_LIST_FREE(l, cand) {
            if (!d)
                d = cand;
            else
                efreet_desktop_free(cand);
        }
    }
    if (!d) {
        fprintf(stderr, with ? _("wbrun: no program %s\n") : _("wbrun: no program opens %s\n"),
                with ? with : (mime_or_null ? mime_or_null : file));
        return 5;
    }
    Eina_List *files = eina_list_append(NULL, file);
    int r = start_desktop(d, files);
    eina_list_free(files);
    efreet_desktop_free(d);
    return r;
}

static bool ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), t = strlen(tail);
    return n >= t && !strcasecmp(s + n - t, tail);
}

int main(int argc, char **argv)
{
    nirvana_i18n_init();
    const char *with = NULL;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++) {
        if (!strcmp(argv[i], "--")) {
            i++;
            break;
        } else if (!strcmp(argv[i], "--with") && i + 1 < argc) {
            with = argv[++i];
        } else if (!strcmp(argv[i], "--print")) {
            print_only = true;
        } else if (!strcmp(argv[i], "--help")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(argv[i], "--version")) {
            printf("wbrun (Nirvana Desktop) %s\n", NIRVANA_VERSION);
            return 0;
        } else {
            fprintf(stderr, _("wbrun: unknown option %s\n"), argv[i]);
            usage(stderr);
            return 1;
        }
    }
    if (i >= argc) {
        usage(stderr);
        return 1;
    }

    char path[PATH_MAX];
    int r = nirvana_to_posix(argv[i], path, sizeof path);
    if (r < 0) {
        fprintf(stderr, "wbrun: %s: %s\n", argv[i], strerror(-r));
        return r == -ENOENT ? 5 : 10;
    }
    struct stat st;
    if (stat(path, &st) < 0) {
        fprintf(stderr, "wbrun: %s: %s\n", argv[i], strerror(errno));
        return 5;
    }
    char abs[PATH_MAX];
    if (realpath(path, abs))
        snprintf(path, sizeof path, "%s", abs);
    char **args = argv + i + 1;
    int nargs = argc - i - 1;

    /* no battery or network modules for a command that is gone in a moment */
    ecore_app_no_system_modules();
    ecore_init();
    efreet_init();
    efreet_mime_init();
    int rc = 0;

    if (S_ISDIR(st.st_mode) && !with) {
        if (!open_drawer(path))
            rc = open_with("inode/directory", NULL, path);
    } else if (ends_with(path, ".desktop") && !with) {
        Efreet_Desktop *d = efreet_desktop_get(path);
        if (!d) {
            fprintf(stderr, _("wbrun: %s is not a desktop entry\n"), path);
            rc = 10;
        } else if (d->type == EFREET_DESKTOP_TYPE_LINK && d->url) {
            rc = open_with("x-scheme-handler/http", NULL, d->url);
            efreet_desktop_free(d);
        } else {
            Eina_List *files = NULL;
            for (int k = 0; k < nargs; k++)
                files = eina_list_append(files, args[k]);
            rc = start_desktop(d, files);
            eina_list_free(files);
            efreet_desktop_free(d);
        }
    } else if (S_ISREG(st.st_mode) && access(path, X_OK) == 0 && !with) {
        /* a program: run as it is, with the arguments */
        const char **av = calloc((size_t)nargs + 2, sizeof *av);
        av[0] = path;
        for (int k = 0; k < nargs; k++)
            av[k + 1] = args[k];
        if (print_only) {
            for (int k = 0; av[k]; k++)
                printf("%s%s", k ? " " : "", av[k]);
            putchar('\n');
        } else if ((r = nirvana_spawn(av)) < 0) {
            fprintf(stderr, "wbrun: %s: %s\n", path, strerror(-r));
            rc = 10;
        }
        free(av);
    } else {
        const char *mime = with ? NULL : efreet_mime_type_get(path);
        rc = open_with(mime, with, path);
    }

    efreet_mime_shutdown();
    efreet_shutdown();
    ecore_shutdown();
    return rc;
}
