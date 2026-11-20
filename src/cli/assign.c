/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * assign - the session's assigns listed, made, changed and removed.
 *
 *     assign                              all the assigns
 *     assign Name:                        one assign
 *     assign Name: /path1;/path2 [--desktop]
 *                                         made or replaced (a multi-assign: several paths)
 *     assign Name: /path --add            a path added to the end (made when missing)
 *     assign Name: REMOVE                 removed
 *     assign --init                       the defaults written (the session's start)
 *
 *     --desktop / --no-desktop   with a volume icon on the desktop or without one
 *     --save                     the change also written to ~/.nirvana/Startup-Sequence
 *     --quiet                    the desktop not told to show the change
 *
 * Paths may be Nirvana paths ("Home:Projects") or relative ones; they are stored as
 * absolute POSIX paths. Exit status: 0, 1 for a bad call, 5 when the assign is missing,
 * 10 on another failure.
 */
#define _GNU_SOURCE
#include "../common/i18n.h"
#include "nirvana.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifndef NIRVANA_VERSION
#define NIRVANA_VERSION "0"
#endif

static void usage(FILE *f)
{
    fprintf(f, _("Usage: assign [NAME: [PATH[;PATH...] | REMOVE]] [OPTIONS]\n"
                 "\n"
                 "  assign                         list the assigns\n"
                 "  assign NAME:                   show one assign\n"
                 "  assign NAME: PATH;PATH ...     make or replace an assign\n"
                 "  assign NAME: PATH --add        add a path to an assign\n"
                 "  assign NAME: REMOVE            remove an assign\n"
                 "  assign --init                  write the default assigns\n"
                 "\n"
                 "Options:\n"
                 "  --desktop, --no-desktop        show a volume icon on the desktop or not\n"
                 "  --add                          add the paths instead of replacing them\n"
                 "  --save                         keep the change in ~/.nirvana/Startup-Sequence\n"
                 "  --quiet                        do not tell the desktop\n"
                 "  --help, --version\n"));
}

static int fail(int err, const char *what)
{
    fprintf(stderr, "assign: %s: %s\n", what, strerror(-err));
    return err == -ENOENT ? 5 : 10;
}

static void print_assign(const nirvana_assign *a, int width)
{
    char head[NIRVANA_NAME_MAX + 2];
    snprintf(head, sizeof head, "%s:", a->name);
    for (size_t i = 0; i < a->npaths; i++) {
        if (i == 0)
            printf("%-*s %s", width, head, a->paths[i]);
        else
            printf("%-*s + %s", width, "", a->paths[i]);
        if (i == 0 && a->desktop)
            printf("  %s", _("[desktop]"));
        putchar('\n');
    }
}

static int list_all(void)
{
    nirvana_assign_list l;
    int r = nirvana_assign_list_load(&l);
    if (r < 0)
        return fail(r, _("cannot read the assigns"));
    int width = 8;
    for (size_t i = 0; i < l.count; i++) {
        int n = (int)strlen(l.items[i].name) + 1;
        if (n > width)
            width = n;
    }
    for (size_t i = 0; i < l.count; i++)
        print_assign(&l.items[i], width);
    nirvana_assign_list_free(&l);
    return 0;
}

/* one path as an absolute POSIX path: a Nirvana path resolved, a relative one made
 * absolute (not resolved further: the path need not exist yet) */
static int absolute(const char *in, char *out, size_t len)
{
    char tmp[PATH_MAX];
    int r = nirvana_to_posix(in, tmp, sizeof tmp);
    if (r < 0)
        return r;
    if (tmp[0] == '/')
        return snprintf(out, len, "%s", tmp) >= (int)len ? -ENAMETOOLONG : 0;
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd))
        return -errno;
    return snprintf(out, len, "%s/%s", cwd, tmp) >= (int)len ? -ENAMETOOLONG : 0;
}

int main(int argc, char **argv)
{
    nirvana_i18n_init();
    const char *name = NULL;
    char *targets[256];
    size_t ntargets = 0;
    int desktop = -1; /* -1: as it is (or off for a new assign) */
    bool add = false, save = false, quiet = false, init = false, remove = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--desktop"))
            desktop = 1;
        else if (!strcmp(a, "--no-desktop"))
            desktop = 0;
        else if (!strcmp(a, "--add"))
            add = true;
        else if (!strcmp(a, "--save"))
            save = true;
        else if (!strcmp(a, "--quiet"))
            quiet = true;
        else if (!strcmp(a, "--init"))
            init = true;
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(a, "--version")) {
            printf("assign (Nirvana Desktop) %s\n", NIRVANA_VERSION);
            return 0;
        } else if (a[0] == '-' && a[1] == '-') {
            fprintf(stderr, _("assign: unknown option %s\n"), a);
            usage(stderr);
            return 1;
        } else if (!name) {
            name = a;
        } else if (!strcasecmp(a, "REMOVE") && ntargets == 0) {
            remove = true;
        } else {
            /* "p1;p2" and separate words both make paths */
            char *copy = strdup(a), *save_ptr = NULL;
            for (char *p = strtok_r(copy, ";", &save_ptr); p; p = strtok_r(NULL, ";", &save_ptr)) {
                if (ntargets == sizeof targets / sizeof *targets) {
                    fprintf(stderr, _("assign: too many paths\n"));
                    return 1;
                }
                targets[ntargets++] = strdup(p);
            }
            free(copy);
        }
    }

    int r;
    if (init) {
        if ((r = nirvana_assign_init()) < 0)
            return fail(r, _("cannot write the default assigns"));
        if (!name) {
            if (!quiet)
                nirvana_notify_desktop();
            return 0;
        }
    }
    if (!name)
        return list_all();

    size_t nl = strlen(name);
    if (nl < 2 || name[nl - 1] != ':') {
        fprintf(stderr, _("assign: \"%s\" is not an assign's name (it ends in a colon: \"Work:\")\n"), name);
        return 1;
    }

    if (remove) {
        if ((r = nirvana_assign_delete(name)) < 0)
            return fail(r, name);
        if (save && (r = nirvana_assign_unpersist(name)) < 0)
            return fail(r, _("cannot write the Startup-Sequence"));
    } else if (ntargets == 0) {
        if (desktop < 0 && !save) {
            nirvana_assign a;
            if ((r = nirvana_assign_get(name, &a)) < 0)
                return fail(r, name);
            print_assign(&a, (int)strlen(a.name) + 1);
            nirvana_assign_free(&a);
            return 0;
        }
        if (desktop >= 0 && (r = nirvana_assign_set_desktop(name, desktop)) < 0)
            return fail(r, name);
        if (save && (r = nirvana_assign_persist(name)) < 0)
            return fail(r, _("cannot write the Startup-Sequence"));
    } else {
        char *abs[256];
        for (size_t i = 0; i < ntargets; i++) {
            char buf[PATH_MAX];
            if ((r = absolute(targets[i], buf, sizeof buf)) < 0)
                return fail(r, targets[i]);
            abs[i] = strdup(buf);
        }
        if (add) {
            for (size_t i = 0; i < ntargets; i++)
                if ((r = nirvana_assign_add_path(name, abs[i], -1)) < 0)
                    return fail(r, name);
            if (desktop >= 0 && (r = nirvana_assign_set_desktop(name, desktop)) < 0)
                return fail(r, name);
        } else {
            bool d = desktop > 0;
            if (desktop < 0) {
                nirvana_assign old;
                if (nirvana_assign_get(name, &old) == 0) {
                    d = old.desktop; /* a replaced assign keeps its icon */
                    nirvana_assign_free(&old);
                }
            }
            if ((r = nirvana_assign_set(name, (const char *const *)abs, ntargets, d)) < 0)
                return fail(r, name);
        }
        if (save && (r = nirvana_assign_persist(name)) < 0)
            return fail(r, _("cannot write the Startup-Sequence"));
        for (size_t i = 0; i < ntargets; i++)
            free(abs[i]), free(targets[i]);
    }
    if (!quiet)
        nirvana_notify_desktop();
    return 0;
}
