/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * libnirvana: the assigns database (see include/nirvana.h).
 *
 * Every change is made under an exclusive lock (flock on "assigns.db.lock"): the database
 * is read, changed in memory and written to a temporary file that is renamed over the
 * old one, so a reader never sees half a file.
 */
#define _GNU_SOURCE
#include "nirvana.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ---------------------------------------------------------------- small helpers */

static char *xstrdup(const char *s)
{
    char *d = strdup(s);
    return d;
}

static int copy_out(char *out, size_t len, const char *s)
{
    size_t n = strlen(s);
    if (n + 1 > len)
        return -ENAMETOOLONG;
    memcpy(out, s, n + 1);
    return 0;
}

static const char *home_dir(void)
{
    const char *h = getenv("HOME");
    return h && *h ? h : "/";
}

/* "Home:" or "Home" -> "Home"; -EINVAL for an empty or bad name */
static int clean_name(const char *in, char out[NIRVANA_NAME_MAX])
{
    if (!in)
        return -EINVAL;
    size_t n = strlen(in);
    if (n && in[n - 1] == ':')
        n--;
    if (n == 0 || n >= NIRVANA_NAME_MAX)
        return -EINVAL;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == ':' || c == '/' || c == '|' || c == ';' || c < 0x20)
            return -EINVAL;
    }
    memcpy(out, in, n);
    out[n] = 0;
    return 0;
}

static bool valid_path(const char *p)
{
    if (!p || !*p)
        return false;
    for (; *p; p++)
        if (*p == ';' || *p == '|' || *p == '\n' || *p == '\r')
            return false;
    return true;
}

/* a path without its trailing slashes (but "/" stays) */
static void strip_slashes(char *p)
{
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/')
        p[--n] = 0;
}

void nirvana_assign_free(nirvana_assign *a)
{
    if (!a)
        return;
    for (size_t i = 0; i < a->npaths; i++)
        free(a->paths[i]);
    free(a->paths);
    a->paths = NULL;
    a->npaths = 0;
}

void nirvana_assign_list_free(nirvana_assign_list *list)
{
    if (!list)
        return;
    for (size_t i = 0; i < list->count; i++)
        nirvana_assign_free(&list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static int assign_add_path_raw(nirvana_assign *a, const char *path, size_t at)
{
    char **p = realloc(a->paths, (a->npaths + 1) * sizeof *p);
    if (!p)
        return -ENOMEM;
    a->paths = p;
    char *dup = xstrdup(path);
    if (!dup)
        return -ENOMEM;
    strip_slashes(dup);
    if (at > a->npaths)
        at = a->npaths;
    memmove(&a->paths[at + 1], &a->paths[at], (a->npaths - at) * sizeof *p);
    a->paths[at] = dup;
    a->npaths++;
    return 0;
}

static int assign_copy(nirvana_assign *dst, const nirvana_assign *src)
{
    memset(dst, 0, sizeof *dst);
    memcpy(dst->name, src->name, sizeof dst->name);
    dst->desktop = src->desktop;
    for (size_t i = 0; i < src->npaths; i++) {
        int r = assign_add_path_raw(dst, src->paths[i], dst->npaths);
        if (r < 0) {
            nirvana_assign_free(dst);
            return r;
        }
    }
    return 0;
}

static nirvana_assign *list_find(nirvana_assign_list *l, const char *name)
{
    for (size_t i = 0; i < l->count; i++)
        if (!strcasecmp(l->items[i].name, name))
            return &l->items[i];
    return NULL;
}

static nirvana_assign *list_append(nirvana_assign_list *l, const char *name)
{
    nirvana_assign *items = realloc(l->items, (l->count + 1) * sizeof *items);
    if (!items)
        return NULL;
    l->items = items;
    nirvana_assign *a = &items[l->count++];
    memset(a, 0, sizeof *a);
    snprintf(a->name, sizeof a->name, "%s", name);
    return a;
}

static void list_remove(nirvana_assign_list *l, nirvana_assign *a)
{
    size_t i = (size_t)(a - l->items);
    nirvana_assign_free(a);
    memmove(&l->items[i], &l->items[i + 1], (l->count - i - 1) * sizeof *a);
    l->count--;
}

/* ---------------------------------------------------------------- the database's file */

static int ram_dir(char *out, size_t len)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    char base[PATH_MAX];
    if (rt && *rt)
        snprintf(base, sizeof base, "%s", rt);
    else
        snprintf(base, sizeof base, "/run/user/%u", (unsigned)getuid());
    int n = snprintf(out, len, "%s/nirvana-ram", base);
    if (n < 0 || (size_t)n >= len)
        return -ENAMETOOLONG;
    if (mkdir(out, 0700) < 0 && errno != EEXIST)
        return -errno;
    return 0;
}

int nirvana_assign_db_path(char *out, size_t len)
{
    char dir[PATH_MAX];
    int r = ram_dir(dir, sizeof dir);
    if (r < 0)
        return r;
    int n = snprintf(out, len, "%s/assigns.db", dir);
    if (n < 0 || (size_t)n >= len)
        return -ENAMETOOLONG;
    return 0;
}

/* the lock: an fd to close (and so unlock) with db_unlock() */
static int db_lock(int how)
{
    char path[PATH_MAX];
    int r = nirvana_assign_db_path(path, sizeof path - 8);
    if (r < 0)
        return r;
    strcat(path, ".lock");
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return -errno;
    while (flock(fd, how) < 0) {
        if (errno != EINTR) {
            int e = -errno;
            close(fd);
            return e;
        }
    }
    return fd;
}

static void db_unlock(int fd)
{
    if (fd >= 0)
        close(fd);
}

/* one line "NAME:|D|p1;p2" parsed into a new assign of the list */
static int parse_line(nirvana_assign_list *l, char *line)
{
    char *bar1 = strchr(line, '|');
    if (!bar1)
        return 0; /* not an assign: passed over */
    *bar1 = 0;
    char *bar2 = strchr(bar1 + 1, '|');
    if (!bar2)
        return 0;
    *bar2 = 0;
    char name[NIRVANA_NAME_MAX];
    if (clean_name(line, name) < 0)
        return 0;
    if (list_find(l, name))
        return 0; /* the first of the same name wins */
    nirvana_assign *a = list_append(l, name);
    if (!a)
        return -ENOMEM;
    a->desktop = atoi(bar1 + 1) != 0;
    char *save = NULL;
    for (char *p = strtok_r(bar2 + 1, ";", &save); p; p = strtok_r(NULL, ";", &save)) {
        if (!*p)
            continue;
        int r = assign_add_path_raw(a, p, a->npaths);
        if (r < 0)
            return r;
    }
    return 0;
}

static int db_read(nirvana_assign_list *out)
{
    memset(out, 0, sizeof *out);
    char path[PATH_MAX];
    int r = nirvana_assign_db_path(path, sizeof path);
    if (r < 0)
        return r;
    FILE *f = fopen(path, "re");
    if (!f)
        return errno == ENOENT ? 0 : -errno;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    r = 0;
    while ((n = getline(&line, &cap, f)) >= 0) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (!*line || *line == '#')
            continue;
        if ((r = parse_line(out, line)) < 0)
            break;
    }
    free(line);
    fclose(f);
    if (r < 0)
        nirvana_assign_list_free(out);
    return r;
}

static int db_write(const nirvana_assign_list *l)
{
    char path[PATH_MAX], tmp[PATH_MAX + 16];
    int r = nirvana_assign_db_path(path, sizeof path);
    if (r < 0)
        return r;
    snprintf(tmp, sizeof tmp, "%s.%ld.tmp", path, (long)getpid());
    FILE *f = fopen(tmp, "we");
    if (!f)
        return -errno;
    for (size_t i = 0; i < l->count; i++) {
        const nirvana_assign *a = &l->items[i];
        fprintf(f, "%s:|%d|", a->name, a->desktop ? 1 : 0);
        for (size_t j = 0; j < a->npaths; j++)
            fprintf(f, "%s%s", j ? ";" : "", a->paths[j]);
        fputc('\n', f);
    }
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
        r = -errno;
        fclose(f);
        unlink(tmp);
        return r;
    }
    fclose(f);
    if (rename(tmp, path) < 0) {
        r = -errno;
        unlink(tmp);
        return r;
    }
    return 0;
}

/* a change: the database read, changed by fn and written, all under the lock */
typedef int (*change_fn)(nirvana_assign_list *l, void *data);

static int db_change(change_fn fn, void *data)
{
    int lock = db_lock(LOCK_EX);
    if (lock < 0)
        return lock;
    nirvana_assign_list l;
    int r = db_read(&l);
    if (r >= 0) {
        r = fn(&l, data);
        if (r >= 0) {
            int w = db_write(&l);
            if (w < 0)
                r = w;
        }
        nirvana_assign_list_free(&l);
    }
    db_unlock(lock);
    return r;
}

/* ---------------------------------------------------------------- reading */

int nirvana_assign_list_load(nirvana_assign_list *out)
{
    if (!out)
        return -EINVAL;
    int lock = db_lock(LOCK_SH);
    if (lock < 0)
        return lock;
    int r = db_read(out);
    db_unlock(lock);
    return r < 0 ? r : (int)out->count;
}

int nirvana_assign_get(const char *name, nirvana_assign *out)
{
    char n[NIRVANA_NAME_MAX];
    int r = clean_name(name, n);
    if (r < 0)
        return r;
    nirvana_assign_list l;
    if ((r = nirvana_assign_list_load(&l)) < 0)
        return r;
    nirvana_assign *a = list_find(&l, n);
    r = a ? assign_copy(out, a) : -ENOENT;
    nirvana_assign_list_free(&l);
    return r;
}

/* ---------------------------------------------------------------- changing */

struct set_args {
    const char *name;
    const char *const *paths;
    size_t npaths;
    bool desktop;
};

static int do_set(nirvana_assign_list *l, void *data)
{
    struct set_args *s = data;
    nirvana_assign *a = list_find(l, s->name);
    if (!a && !(a = list_append(l, s->name)))
        return -ENOMEM;
    nirvana_assign fresh = {0};
    memcpy(fresh.name, a->name, sizeof fresh.name);
    /* a new spelling of the name ("home" -> "HOME") is taken */
    snprintf(fresh.name, sizeof fresh.name, "%s", s->name);
    fresh.desktop = s->desktop;
    for (size_t i = 0; i < s->npaths; i++) {
        int r = assign_add_path_raw(&fresh, s->paths[i], fresh.npaths);
        if (r < 0) {
            nirvana_assign_free(&fresh);
            return r;
        }
    }
    nirvana_assign_free(a);
    *a = fresh;
    return 0;
}

int nirvana_assign_set(const char *name, const char *const *paths, size_t npaths, bool desktop)
{
    char n[NIRVANA_NAME_MAX];
    int r = clean_name(name, n);
    if (r < 0)
        return r;
    if (npaths == 0 || !paths)
        return -EINVAL;
    for (size_t i = 0; i < npaths; i++)
        if (!valid_path(paths[i]))
            return -EINVAL;
    struct set_args s = {n, paths, npaths, desktop};
    return db_change(do_set, &s);
}

struct edit_args {
    const char *name;
    const char *new_name;
    const char *path;
    long a, b;
    bool flag;
};

static int do_set_desktop(nirvana_assign_list *l, void *data)
{
    struct edit_args *e = data;
    nirvana_assign *a = list_find(l, e->name);
    if (!a)
        return -ENOENT;
    a->desktop = e->flag;
    return 0;
}

int nirvana_assign_set_desktop(const char *name, bool desktop)
{
    char n[NIRVANA_NAME_MAX];
    int r = clean_name(name, n);
    if (r < 0)
        return r;
    struct edit_args e = {.name = n, .flag = desktop};
    return db_change(do_set_desktop, &e);
}

static int do_add_path(nirvana_assign_list *l, void *data)
{
    struct edit_args *e = data;
    nirvana_assign *a = list_find(l, e->name);
    if (!a && !(a = list_append(l, e->name)))
        return -ENOMEM;
    size_t at = e->a < 0 ? a->npaths : (size_t)e->a;
    return assign_add_path_raw(a, e->path, at);
}

int nirvana_assign_add_path(const char *name, const char *path, int position)
{
    char n[NIRVANA_NAME_MAX];
    int r = clean_name(name, n);
    if (r < 0)
        return r;
    if (!valid_path(path))
        return -EINVAL;
    struct edit_args e = {.name = n, .path = path, .a = position};
    return db_change(do_add_path, &e);
}

static int do_remove_path(nirvana_assign_list *l, void *data)
{
    struct edit_args *e = data;
    nirvana_assign *a = list_find(l, e->name);
    if (!a)
        return -ENOENT;
    size_t i = (size_t)e->a;
    if (i >= a->npaths)
        return -ERANGE;
    free(a->paths[i]);
    memmove(&a->paths[i], &a->paths[i + 1], (a->npaths - i - 1) * sizeof *a->paths);
    a->npaths--;
    if (a->npaths == 0)
        list_remove(l, a);
    return 0;
}

int nirvana_assign_remove_path(const char *name, size_t index)
{
    char n[NIRVANA_NAME_MAX];
    int r = clean_name(name, n);
    if (r < 0)
        return r;
    struct edit_args e = {.name = n, .a = (long)index};
    return db_change(do_remove_path, &e);
}

static int do_move_path(nirvana_assign_list *l, void *data)
{
    struct edit_args *e = data;
    nirvana_assign *a = list_find(l, e->name);
    if (!a)
        return -ENOENT;
    size_t from = (size_t)e->a, to = (size_t)e->b;
    if (from >= a->npaths || to >= a->npaths)
        return -ERANGE;
    char *p = a->paths[from];
    if (from < to)
        memmove(&a->paths[from], &a->paths[from + 1], (to - from) * sizeof p);
    else
        memmove(&a->paths[to + 1], &a->paths[to], (from - to) * sizeof p);
    a->paths[to] = p;
    return 0;
}

int nirvana_assign_move_path(const char *name, size_t from, size_t to)
{
    char n[NIRVANA_NAME_MAX];
    int r = clean_name(name, n);
    if (r < 0)
        return r;
    struct edit_args e = {.name = n, .a = (long)from, .b = (long)to};
    return db_change(do_move_path, &e);
}

static int do_delete(nirvana_assign_list *l, void *data)
{
    struct edit_args *e = data;
    nirvana_assign *a = list_find(l, e->name);
    if (!a)
        return -ENOENT;
    list_remove(l, a);
    return 0;
}

int nirvana_assign_delete(const char *name)
{
    char n[NIRVANA_NAME_MAX];
    int r = clean_name(name, n);
    if (r < 0)
        return r;
    struct edit_args e = {.name = n};
    return db_change(do_delete, &e);
}

static int do_rename(nirvana_assign_list *l, void *data)
{
    struct edit_args *e = data;
    nirvana_assign *a = list_find(l, e->name);
    if (!a)
        return -ENOENT;
    nirvana_assign *other = list_find(l, e->new_name);
    if (other && other != a)
        return -EEXIST;
    snprintf(a->name, sizeof a->name, "%s", e->new_name);
    return 0;
}

int nirvana_assign_rename(const char *name, const char *new_name)
{
    char n[NIRVANA_NAME_MAX], nn[NIRVANA_NAME_MAX];
    int r = clean_name(name, n);
    if (r < 0 || (r = clean_name(new_name, nn)) < 0)
        return r;
    struct edit_args e = {.name = n, .new_name = nn};
    return db_change(do_rename, &e);
}

/* ---------------------------------------------------------------- the defaults */

/* one default: made when missing, its paths given anew when it is there */
static int put_default(nirvana_assign_list *l, const char *name, bool desktop, const char *paths)
{
    nirvana_assign *a = list_find(l, name);
    if (a) {
        nirvana_assign_free(a);
    } else if (!(a = list_append(l, name))) {
        return -ENOMEM;
    }
    a->desktop = desktop;
    char *copy = xstrdup(paths);
    if (!copy)
        return -ENOMEM;
    char *save = NULL;
    int r = 0;
    for (char *p = strtok_r(copy, ";", &save); p && r >= 0; p = strtok_r(NULL, ";", &save))
        if (*p)
            r = assign_add_path_raw(a, p, a->npaths);
    free(copy);
    return r;
}

/* ~/.config/user-dirs.dirs: XDG_DOWNLOAD_DIR="$HOME/Downloads" -> the path, or NULL */
static char *user_dir(const char *key)
{
    const char *cfg = getenv("XDG_CONFIG_HOME");
    char file[PATH_MAX];
    if (cfg && *cfg)
        snprintf(file, sizeof file, "%s/user-dirs.dirs", cfg);
    else
        snprintf(file, sizeof file, "%s/.config/user-dirs.dirs", home_dir());
    FILE *f = fopen(file, "re");
    if (!f)
        return NULL;
    char want[64];
    snprintf(want, sizeof want, "XDG_%s_DIR=", key);
    char *line = NULL, *result = NULL;
    size_t cap = 0;
    while (!result && getline(&line, &cap, f) >= 0) {
        char *s = line;
        while (isspace((unsigned char)*s))
            s++;
        if (strncmp(s, want, strlen(want)))
            continue;
        s += strlen(want);
        size_t n = strcspn(s, "\r\n");
        s[n] = 0;
        if (*s == '"') {
            s++;
            char *q = strrchr(s, '"');
            if (q)
                *q = 0;
        }
        char buf[PATH_MAX];
        if (!strncmp(s, "$HOME", 5))
            snprintf(buf, sizeof buf, "%s%s", home_dir(), s + 5);
        else
            snprintf(buf, sizeof buf, "%s", s);
        strip_slashes(buf);
        /* a user directory set to $HOME itself means "not set" */
        if (*buf == '/' && strcmp(buf, home_dir()) != 0)
            result = xstrdup(buf);
    }
    free(line);
    fclose(f);
    return result;
}

static bool path_used(const nirvana_assign_list *l, const char *path)
{
    for (size_t i = 0; i < l->count; i++)
        for (size_t j = 0; j < l->items[i].npaths; j++)
            if (!strcmp(l->items[i].paths[j], path))
                return true;
    return false;
}

static int name_cmp(const void *a, const void *b)
{
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

static int do_init(nirvana_assign_list *l, void *data)
{
    (void)data;
    const char *home = home_dir();
    char ram[PATH_MAX], buf[2 * PATH_MAX + 64];
    int r = ram_dir(ram, sizeof ram);
    if (r < 0)
        return r;

    if ((r = put_default(l, "SYS", true, "/")) < 0)
        return r;
    if ((r = put_default(l, "Home", true, home)) < 0)
        return r;
    if ((r = put_default(l, "RAM", true, ram)) < 0)
        return r;
    snprintf(buf, sizeof buf, "%s/.nirvana;/opt/nirvana", home);
    if ((r = put_default(l, "Nirvana", false, buf)) < 0)
        return r;
    snprintf(buf, sizeof buf, "%s/.nirvana/Themes;/opt/nirvana/Themes", home);
    if ((r = put_default(l, "Themes", false, buf)) < 0)
        return r;
    snprintf(buf, sizeof buf, "/usr/share/applications;%s/.local/share/applications", home);
    if ((r = put_default(l, "Tools", false, buf)) < 0)
        return r;

    static const struct { const char *key, *name; } xdg[] = {
        {"DOWNLOAD", "Downloads"}, {"DOCUMENTS", "Docs"}, {"PICTURES", "Pics"},
        {"MUSIC", "Music"},        {"VIDEOS", "Videos"},
    };
    for (size_t i = 0; i < sizeof xdg / sizeof *xdg; i++) {
        char *p = user_dir(xdg[i].key);
        if (p && valid_path(p))
            r = put_default(l, xdg[i].name, false, p);
        free(p);
        if (r < 0)
            return r;
    }

    /* one assign for each visible directory at the top of $HOME, in the order of their
     * names; none for a directory an assign has already, nor for a name that is taken */
    DIR *d = opendir(home);
    if (!d)
        return 0;
    char **names = NULL;
    size_t count = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char full[PATH_MAX];
        if (snprintf(full, sizeof full, "%s/%s", strcmp(home, "/") ? home : "", e->d_name) >= (int)sizeof full)
            continue;
        struct stat st;
        if (stat(full, &st) < 0 || !S_ISDIR(st.st_mode))
            continue;
        char n[NIRVANA_NAME_MAX];
        if (clean_name(e->d_name, n) < 0 || !valid_path(full))
            continue;
        char **nn = realloc(names, (count + 1) * sizeof *nn);
        if (!nn)
            break;
        names = nn;
        if (!(names[count] = xstrdup(e->d_name)))
            break;
        count++;
    }
    closedir(d);
    qsort(names, count, sizeof *names, name_cmp);
    for (size_t i = 0; i < count && r >= 0; i++) {
        snprintf(buf, sizeof buf, "%s/%s", strcmp(home, "/") ? home : "", names[i]);
        nirvana_assign *a = list_find(l, names[i]);
        if (!path_used(l, buf) && (!a || (a->npaths == 1 && !strcmp(a->paths[0], buf))))
            r = put_default(l, names[i], false, buf);
    }
    for (size_t i = 0; i < count; i++)
        free(names[i]);
    free(names);
    return r;
}

int nirvana_assign_init(void)
{
    return db_change(do_init, NULL);
}

/* ---------------------------------------------------------------- translation */

/* "Name:rest" -> name and rest; false when the path has no volume name */
static bool split_volume(const char *path, char name[NIRVANA_NAME_MAX], const char **rest)
{
    const char *colon = strchr(path, ':');
    if (!colon)
        return false;
    size_t n = (size_t)(colon - path);
    if (n == 0 || n >= NIRVANA_NAME_MAX || memchr(path, '/', n))
        return false;
    memcpy(name, path, n);
    name[n] = 0;
    *rest = colon + 1;
    while (**rest == '/')
        (*rest)++;
    return true;
}

static int join(char *out, size_t len, const char *dir, const char *rest)
{
    int n;
    if (!*rest)
        n = snprintf(out, len, "%s", dir);
    else if (!strcmp(dir, "/"))
        n = snprintf(out, len, "/%s", rest);
    else
        n = snprintf(out, len, "%s/%s", dir, rest);
    return n < 0 || (size_t)n >= len ? -ENAMETOOLONG : 0;
}

int nirvana_to_posix(const char *nirvana_path, char *out, size_t len)
{
    if (!nirvana_path || !out)
        return -EINVAL;
    char name[NIRVANA_NAME_MAX];
    const char *rest;
    if (!split_volume(nirvana_path, name, &rest))
        return copy_out(out, len, nirvana_path);
    nirvana_assign a;
    int r = nirvana_assign_get(name, &a);
    if (r < 0)
        return r;
    if (a.npaths == 0) {
        nirvana_assign_free(&a);
        return -ENOENT;
    }
    char cand[PATH_MAX];
    size_t pick = 0;
    for (size_t i = 0; i < a.npaths; i++) {
        if (join(cand, sizeof cand, a.paths[i], rest) == 0 && access(cand, F_OK) == 0) {
            pick = i;
            break;
        }
    }
    r = join(out, len, a.paths[pick], rest);
    nirvana_assign_free(&a);
    return r;
}

int posix_to_nirvana(const char *posix_path, char *out, size_t len)
{
    if (!posix_path || !out)
        return -EINVAL;
    if (*posix_path != '/')
        return copy_out(out, len, posix_path);
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s", posix_path) >= (int)sizeof path)
        return -ENAMETOOLONG;
    strip_slashes(path);

    nirvana_assign_list l;
    int r = nirvana_assign_list_load(&l);
    if (r < 0)
        return r;
    const nirvana_assign *best = NULL;
    size_t best_len = 0;
    for (size_t i = 0; i < l.count; i++) {
        for (size_t j = 0; j < l.items[i].npaths; j++) {
            const char *t = l.items[i].paths[j];
            size_t n = strlen(t);
            bool match;
            if (!strcmp(t, "/"))
                match = true, n = 1;
            else
                match = !strncmp(path, t, n) && (path[n] == 0 || path[n] == '/');
            if (match && (!best || n > best_len)) {
                best = &l.items[i];
                best_len = n;
            }
        }
    }
    if (!best) {
        nirvana_assign_list_free(&l);
        return copy_out(out, len, path);
    }
    const char *rest = path + best_len;
    while (*rest == '/')
        rest++;
    int n = snprintf(out, len, "%s:%s", best->name, rest);
    nirvana_assign_list_free(&l);
    return n < 0 || (size_t)n >= len ? -ENAMETOOLONG : 0;
}

/* ---------------------------------------------------------------- Startup-Sequence */

static int startup_sequence(char *out, size_t len)
{
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/.nirvana", home_dir());
    if (mkdir(dir, 0755) < 0 && errno != EEXIST)
        return -errno;
    int n = snprintf(out, len, "%s/Startup-Sequence", dir);
    return n < 0 || (size_t)n >= len ? -ENAMETOOLONG : 0;
}

/* "assign Name: '/p1;/p2' --desktop" */
static void write_assign_line(FILE *f, const nirvana_assign *a)
{
    fprintf(f, "assign %s: '", a->name);
    for (size_t i = 0; i < a->npaths; i++) {
        if (i)
            fputc(';', f);
        for (const char *s = a->paths[i]; *s; s++) {
            if (*s == '\'')
                fputs("'\\''", f);
            else
                fputc(*s, f);
        }
    }
    fputs(a->desktop ? "' --desktop\n" : "'\n", f);
}

/* is this line the "assign NAME: ..." line of an assign? */
static bool is_assign_line(const char *line, const char *name)
{
    while (*line == ' ' || *line == '\t')
        line++;
    if (strncmp(line, "assign", 6) || (line[6] != ' ' && line[6] != '\t'))
        return false;
    line += 6;
    while (*line == ' ' || *line == '\t')
        line++;
    if (*line == '\'' || *line == '"')
        line++;
    size_t n = strlen(name);
    return !strncasecmp(line, name, n) && line[n] == ':';
}

/* the Startup-Sequence rewritten: the assign's line replaced by `a` (or dropped when a is
 * NULL); a new line goes to the end */
static int rewrite_startup(const char *name, const nirvana_assign *a)
{
    char path[PATH_MAX], tmp[PATH_MAX + 16];
    int r = startup_sequence(path, sizeof path);
    if (r < 0)
        return r;
    snprintf(tmp, sizeof tmp, "%s.%ld.tmp", path, (long)getpid());
    FILE *in = fopen(path, "re");
    FILE *out = fopen(tmp, "we");
    if (!out) {
        r = -errno;
        if (in)
            fclose(in);
        return r;
    }
    struct stat st;
    mode_t mode = 0755;
    if (in && fstat(fileno(in), &st) == 0)
        mode = st.st_mode & 07777;
    if (!in)
        fputs("#!/bin/sh\n# Nirvana Desktop: the session's start.\n", out);

    bool written = false;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    bool last_nl = true;
    while (in && (n = getline(&line, &cap, in)) >= 0) {
        if (is_assign_line(line, name)) {
            if (a && !written) {
                write_assign_line(out, a);
                written = true;
            }
            continue;
        }
        fputs(line, out);
        last_nl = n > 0 && line[n - 1] == '\n';
    }
    free(line);
    if (in)
        fclose(in);
    if (a && !written) {
        if (!last_nl)
            fputc('\n', out);
        write_assign_line(out, a);
    }
    if (fflush(out) != 0) {
        r = -errno;
        fclose(out);
        unlink(tmp);
        return r;
    }
    fchmod(fileno(out), mode);
    fclose(out);
    if (rename(tmp, path) < 0) {
        r = -errno;
        unlink(tmp);
        return r;
    }
    return 0;
}

int nirvana_assign_persist(const char *name)
{
    nirvana_assign a;
    int r = nirvana_assign_get(name, &a);
    if (r < 0)
        return r;
    r = rewrite_startup(a.name, &a);
    nirvana_assign_free(&a);
    return r;
}

int nirvana_assign_unpersist(const char *name)
{
    char n[NIRVANA_NAME_MAX];
    int r = clean_name(name, n);
    if (r < 0)
        return r;
    return rewrite_startup(n, NULL);
}

/* ---------------------------------------------------------------- the desktop told */

int nirvana_notify_desktop(void)
{
    DIR *d = opendir("/proc");
    if (!d)
        return -errno;
    int told = 0;
    uid_t me = getuid();
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!isdigit((unsigned char)e->d_name[0]))
            continue;
        char file[300], comm[64] = "";
        snprintf(file, sizeof file, "/proc/%s/comm", e->d_name);
        FILE *f = fopen(file, "re");
        if (!f)
            continue;
        if (fgets(comm, sizeof comm, f))
            comm[strcspn(comm, "\n")] = 0;
        fclose(f);
        if (strcmp(comm, "nirvana-desktop"))
            continue;
        snprintf(file, sizeof file, "/proc/%s", e->d_name);
        struct stat st;
        if (stat(file, &st) < 0 || st.st_uid != me)
            continue;
        if (kill((pid_t)atol(e->d_name), SIGUSR1) == 0)
            told++;
    }
    closedir(d);
    return told;
}
