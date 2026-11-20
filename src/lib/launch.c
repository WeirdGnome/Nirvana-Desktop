/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * libnirvana: programs started from the desktop - their environment (the global menu's
 * variables), the default program of a MIME type, detached children.
 */
#define _GNU_SOURCE
#include "nirvana.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* "a:b" with word added when missing */
static void add_to_list(const char *var, const char *word)
{
    const char *cur = getenv(var);
    if (!cur || !*cur) {
        setenv(var, word, 1);
        return;
    }
    size_t wl = strlen(word);
    for (const char *p = cur; *p;) {
        const char *end = strchr(p, ':');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == wl && !strncmp(p, word, n))
            return;
        p += n + (end ? 1 : 0);
    }
    char *v;
    if (asprintf(&v, "%s:%s", cur, word) >= 0) {
        setenv(var, v, 1);
        free(v);
    }
}

void nirvana_launch_env(void)
{
    add_to_list("GTK_MODULES", "appmenu-gtk-module");
    setenv("UBUNTU_MENUPROXY", "1", 1);
    setenv("QT_QPA_PLATFORMTHEME", "appmenu-qt5", 0);
    setenv("SAL_USE_VCLPLUGIN", "gtk3", 0);
}

/* ---------------------------------------------------------------- mimeapps.list */

/* in one file: the group's entry for mime, its first desktop id that is installed */
static int scan_mimeapps(const char *file, const char *group, const char *mime, char *out, size_t len)
{
    FILE *f = fopen(file, "re");
    if (!f)
        return -ENOENT;
    char *line = NULL;
    size_t cap = 0;
    bool in_group = false;
    int r = -ENOENT;
    size_t ml = strlen(mime);
    while (r < 0 && getline(&line, &cap, f) >= 0) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') {
            in_group = !strcmp(line, group);
            continue;
        }
        if (!in_group || strncmp(line, mime, ml) || line[ml] != '=')
            continue;
        char *save = NULL;
        for (char *id = strtok_r(line + ml + 1, ";", &save); id; id = strtok_r(NULL, ";", &save)) {
            while (isspace((unsigned char)*id))
                id++;
            if (*id && strlen(id) < len) {
                strcpy(out, id);
                r = 0;
                break;
            }
        }
    }
    free(line);
    fclose(f);
    return r;
}

/* printf at the end of buf (what does not fit is left out) */
__attribute__((format(printf, 3, 4))) static void append(char *buf, size_t size, const char *fmt, ...)
{
    size_t n = strlen(buf);
    if (n + 1 >= size)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, size - n, fmt, ap);
    va_end(ap);
}

int nirvana_mime_default(const char *mime, char *out, size_t len)
{
    if (!mime || !out)
        return -EINVAL;
    /* the files in their order of precedence: the user's configuration, the system's, the
     * data directories' (desktop-specific ones first) */
    char dirs[8192] = "";
    const char *home = getenv("HOME");
    const char *ch = getenv("XDG_CONFIG_HOME"), *cd = getenv("XDG_CONFIG_DIRS");
    const char *dh = getenv("XDG_DATA_HOME"), *dd = getenv("XDG_DATA_DIRS");
    if (ch && *ch)
        append(dirs, sizeof dirs, "%s", ch);
    else
        append(dirs, sizeof dirs, "%s/.config", home ? home : "");
    append(dirs, sizeof dirs, ":%s", cd && *cd ? cd : "/etc/xdg");
    if (dh && *dh)
        append(dirs, sizeof dirs, ":%s/applications", dh);
    else
        append(dirs, sizeof dirs, ":%s/.local/share/applications", home ? home : "");
    char data[2048], tmp[PATH_MAX];
    snprintf(data, sizeof data, "%s", dd && *dd ? dd : "/usr/local/share:/usr/share");
    char *save = NULL;
    for (char *d = strtok_r(data, ":", &save); d; d = strtok_r(NULL, ":", &save))
        append(dirs, sizeof dirs, ":%s/applications", d);

    static const char *const groups[] = {"[Default Applications]", "[Added Associations]"};
    static const char *const names[] = {"nirvana-mimeapps.list", "mimeapps.list", "defaults.list"};
    for (size_t g = 0; g < 2; g++) {
        char copy[sizeof dirs];
        snprintf(copy, sizeof copy, "%s", dirs);
        save = NULL;
        for (char *d = strtok_r(copy, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
            for (size_t n = 0; n < sizeof names / sizeof *names; n++) {
                snprintf(tmp, sizeof tmp, "%s/%s", d, names[n]);
                if (scan_mimeapps(tmp, groups[g], mime, out, len) == 0)
                    return 0;
            }
        }
    }
    return -ENOENT;
}

/* ---------------------------------------------------------------- children */

static int spawn(const char *path, char *const argv[], bool use_path)
{
    /* a grandchild, so that no zombie is left: the child forks and goes */
    int pfd[2];
    if (pipe2(pfd, O_CLOEXEC) < 0)
        return -errno;
    pid_t child = fork();
    if (child < 0) {
        int e = -errno;
        close(pfd[0]);
        close(pfd[1]);
        return e;
    }
    if (child == 0) {
        close(pfd[0]);
        setsid();
        signal(SIGPIPE, SIG_DFL);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        pid_t gc = fork();
        if (gc != 0) {
            ssize_t w = write(pfd[1], &gc, sizeof gc);
            (void)w;
            _exit(gc < 0 ? 1 : 0);
        }
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0) {
            dup2(null, 0);
            if (null > 2)
                close(null);
        }
        nirvana_launch_env();
        if (use_path)
            execvp(path, argv);
        else
            execv(path, argv);
        int e = errno;
        pid_t fail = -e;
        ssize_t w = write(pfd[1], &fail, sizeof fail);
        (void)w;
        _exit(127);
    }
    close(pfd[1]);
    int status;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR)
        ;
    /* the grandchild's pid, then (only if exec failed) its error */
    pid_t gc = 0, err = 0;
    ssize_t n = read(pfd[0], &gc, sizeof gc);
    if (n == sizeof gc && gc > 0) {
        if (read(pfd[0], &err, sizeof err) == sizeof err && err < 0) {
            close(pfd[0]);
            return (int)err;
        }
    }
    close(pfd[0]);
    return n == sizeof gc && gc > 0 ? (int)gc : -ECHILD;
}

int nirvana_spawn(const char *const *argv)
{
    if (!argv || !argv[0])
        return -EINVAL;
    return spawn(argv[0], (char *const *)argv, true);
}

int nirvana_spawn_shell(const char *command)
{
    if (!command)
        return -EINVAL;
    char *const argv[] = {"sh", "-c", (char *)command, NULL};
    return spawn("/bin/sh", argv, false);
}
