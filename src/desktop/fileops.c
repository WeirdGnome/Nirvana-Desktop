/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: what is done to files - copied, moved, deleted, put in the trashcan,
 * restored from it, the trashcan emptied - in a thread, with Ambient's progress window
 * (the file, a gauge, "Files done", the time remaining and the speed, Stop) shown when
 * the work takes more than a moment.
 *
 * When a file is in the way the thread asks and waits: Replace, Replace All, Skip,
 * Skip All, Cancel; when something fails: Retry, Skip, Cancel.
 *
 * The trashcan is the XDG one ($XDG_DATA_HOME/Trash: files/ and info/NAME.trashinfo), the
 * one other desktops share.
 */
#include "desktop.h"

#include <Ecore_File.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { ASK_REPLACE, ASK_REPLACE_ALL, ASK_SKIP, ASK_SKIP_ALL, ASK_CANCEL };
enum { ERR_RETRY, ERR_SKIP, ERR_CANCEL };

typedef struct Job {
    Nv_File_Op op;
    Eina_List *sources; /* char *: POSIX paths */
    char *dest;         /* the drawer copied or moved into */
    Ecore_Thread *th;
    /* counted first */
    unsigned long long total_bytes, total_files;
    /* done so far (written by the thread, read by the window) */
    unsigned long long done_bytes, done_files;
    char current[PATH_MAX];
    double started, last_report;
    /* a question to the user, and its answer */
    Eina_Lock lock;
    Eina_Condition cond;
    int answer;
    bool answered;
    bool replace_all, skip_all;
    bool asking;        /* a question is waiting for its answer */
    /* the progress window */
    Nv_Window *w;
    Evas_Object *lab_file, *bar, *lab_files, *lab_time;
    Ecore_Timer *show_timer;
    bool done;
} Job;

typedef struct Question {
    Job *job;
    bool error;
    bool skip_only; /* (nothing to retry: Skip, Cancel) */
    char text[PATH_MAX * 2];
} Question;

/* ---------------------------------------------------------------- the trashcan */

void nv_trash_dir(char *out, size_t len)
{
    const char *x = getenv("XDG_DATA_HOME");
    if (x && *x)
        snprintf(out, len, "%s/Trash", x);
    else
        snprintf(out, len, "%s/.local/share/Trash", getenv("HOME") ? getenv("HOME") : "");
}

bool nv_trash_full(void)
{
    char t[PATH_MAX], f[PATH_MAX + 8];
    nv_trash_dir(t, sizeof t);
    snprintf(f, sizeof f, "%s/files", t);
    DIR *d = opendir(f);
    if (!d)
        return false;
    struct dirent *de;
    bool full = false;
    while (!full && (de = readdir(d)))
        full = strcmp(de->d_name, ".") && strcmp(de->d_name, "..");
    closedir(d);
    return full;
}

bool nv_in_trash(const char *path)
{
    char t[PATH_MAX], files[PATH_MAX + 8];
    nv_trash_dir(t, sizeof t);
    snprintf(files, sizeof files, "%s/files/", t);
    return !strncmp(path, files, strlen(files));
}

bool nv_is_trash_dir(const char *path)
{
    char t[PATH_MAX], files[PATH_MAX + 8];
    nv_trash_dir(t, sizeof t);
    snprintf(files, sizeof files, "%s/files", t);
    return !strcmp(path, files);
}

/* a path written as a URL's path ("%20" for a space), as .trashinfo wants it */
static void url_encode(const char *in, char *out, size_t len)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 4 < len; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            strchr("/-_.~", *p))
            out[n++] = (char)*p;
        else
            n += (size_t)snprintf(out + n, len - n, "%%%02X", *p);
    }
    out[n] = 0;
}

static void url_decode(const char *in, char *out, size_t len)
{
    size_t n = 0;
    for (const char *p = in; *p && n + 1 < len; p++) {
        unsigned v;
        if (*p == '%' && p[1] && p[2] && sscanf(p + 1, "%2x", &v) == 1) {
            out[n++] = (char)v;
            p += 2;
        } else {
            out[n++] = *p;
        }
    }
    out[n] = 0;
}

/* where a trashed file came from (its .trashinfo), or false */
bool nv_trash_origin(const char *trashed, char *out, size_t len)
{
    char t[PATH_MAX], info[PATH_MAX * 2];
    nv_trash_dir(t, sizeof t);
    const char *base = strrchr(trashed, '/');
    base = base ? base + 1 : trashed;
    snprintf(info, sizeof info, "%s/info/%s.trashinfo", t, base);
    FILE *f = fopen(info, "r");
    if (!f)
        return false;
    char line[PATH_MAX * 3];
    bool found = false;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "Path=", 5)) {
            line[strcspn(line, "\n")] = 0;
            url_decode(line + 5, out, len);
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

/* ---------------------------------------------------------------- asking (the thread waits) */

static void ask_main(void *data, Ecore_Thread *th, void *msg);

/* a question from the thread, answered in the main loop: the answer's index */
static int ask(Job *j, bool error, const char *fmt, const char *name, int err)
{
    Question *q = calloc(1, sizeof *q);
    q->job = j;
    q->error = error;
    q->skip_only = error && err == 0;
    if (error && err == 0)
        snprintf(q->text, sizeof q->text, fmt, name, "");
    else if (error)
        snprintf(q->text, sizeof q->text, fmt, name, strerror(err));
    else
        snprintf(q->text, sizeof q->text, fmt, name);
    eina_lock_take(&j->lock);
    j->answered = false;
    eina_lock_release(&j->lock);
    ecore_thread_feedback(j->th, q);
    eina_lock_take(&j->lock);
    while (!j->answered)
        eina_condition_wait(&j->cond);
    int a = j->answer;
    eina_lock_release(&j->lock);
    return a;
}

static void ask_answer(void *data, int choice)
{
    Job *j = data;
    j->asking = false;
    eina_lock_take(&j->lock);
    j->answer = choice;
    j->answered = true;
    eina_condition_signal(&j->cond);
    eina_lock_release(&j->lock);
}

/* in the way: replace it? (false: leave it, the source is skipped) */
static bool may_replace(Job *j, const char *dst, bool *cancel)
{
    if (j->replace_all)
        return true;
    if (j->skip_all)
        return false;
    int a = ask(j, false, _("File '%s'\nalready exists and will be replaced."), dst, 0);
    switch (a) {
    case ASK_REPLACE_ALL: j->replace_all = true; /* fall through */
    case ASK_REPLACE: return true;
    case ASK_SKIP_ALL: j->skip_all = true; /* fall through */
    case ASK_SKIP: return false;
    default: *cancel = true; return false;
    }
}

/* something failed: try again? (ERR_*) */
static int failed(Job *j, const char *what, const char *name, int err)
{
    if (j->skip_all)
        return ERR_SKIP;
    int a = ask(j, true, what, name, err);
    if (err == 0) /* (Skip, Cancel) */
        return a == 0 ? ERR_SKIP : ERR_CANCEL;
    return a;
}

/* ---------------------------------------------------------------- progress */

static void report(Job *j, const char *name, bool force)
{
    if (name)
        snprintf(j->current, sizeof j->current, "%s", name);
    double now = ecore_time_get();
    if (!force && now - j->last_report < 0.1)
        return;
    j->last_report = now;
    ecore_thread_feedback(j->th, NULL); /* (NULL: "look at the numbers") */
}

/* ---------------------------------------------------------------- counting */

static void count(Job *j, const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return;
    j->total_files++;
    if (S_ISREG(st.st_mode))
        j->total_bytes += (unsigned long long)st.st_size;
    if (!S_ISDIR(st.st_mode) || ecore_thread_check(j->th))
        return;
    DIR *d = opendir(path);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        char sub[PATH_MAX * 2];
        snprintf(sub, sizeof sub, "%s/%s", path, de->d_name);
        count(j, sub);
    }
    closedir(d);
}

/* ---------------------------------------------------------------- the work */

static bool remove_tree(Job *j, const char *path, bool *cancel);

static bool copy_file(Job *j, const char *src, const char *dst, const struct stat *st, bool *cancel)
{
    for (;;) {
        int in = open(src, O_RDONLY | O_CLOEXEC);
        int out = in >= 0 ? open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, st->st_mode & 07777) : -1;
        int err = errno;
        bool ok = in >= 0 && out >= 0;
        static __thread char buf[1 << 20];
        while (ok) {
            if (ecore_thread_check(j->th)) {
                *cancel = true;
                ok = false;
                err = 0;
                break;
            }
            ssize_t n = read(in, buf, sizeof buf);
            if (n == 0)
                break;
            if (n < 0 || write(out, buf, (size_t)n) != n) {
                err = errno;
                ok = false;
                break;
            }
            j->done_bytes += (unsigned long long)n;
            report(j, NULL, false);
        }
        if (ok) {
            struct timespec ts[2] = {st->st_atim, st->st_mtim};
            if (fchmod(out, st->st_mode & 07777) != 0 || futimens(out, ts) != 0) {
            }
        }
        if (in >= 0)
            close(in);
        if (out >= 0 && close(out) != 0 && ok) {
            err = errno;
            ok = false;
        }
        if (ok)
            return true;
        if (out >= 0)
            unlink(dst);
        if (*cancel)
            return false;
        int a = failed(j, _("Couldn't copy '%s'.\n\n%s"), src, err);
        if (a == ERR_RETRY)
            continue;
        if (a == ERR_CANCEL)
            *cancel = true;
        return false;
    }
}

/* src copied to dst (dst's name given): a file, a link, a drawer and all in it */
static bool copy_tree(Job *j, const char *src, const char *dst, bool *cancel)
{
    struct stat st, dt;
    if (lstat(src, &st) != 0) {
        if (failed(j, _("Couldn't read '%s'.\n\n%s"), src, errno) == ERR_CANCEL)
            *cancel = true;
        return false;
    }
    report(j, src, false);
    bool exists = lstat(dst, &dt) == 0;
    if (exists && !(S_ISDIR(st.st_mode) && S_ISDIR(dt.st_mode))) {
        /* in the way (a drawer onto a drawer: what is in them is merged) */
        if (!may_replace(j, dst, cancel))
            return false;
        if (!remove_tree(j, dst, cancel))
            return false;
        exists = false;
    }
    bool ok = true;
    if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX];
        ssize_t n = readlink(src, target, sizeof target - 1);
        if (n < 0 || (target[n] = 0, symlink(target, dst) != 0)) {
            if (failed(j, _("Couldn't copy '%s'.\n\n%s"), src, errno) == ERR_CANCEL)
                *cancel = true;
            ok = false;
        }
    } else if (S_ISDIR(st.st_mode)) {
        if (!exists && mkdir(dst, (st.st_mode & 07777) | 0700) != 0) {
            if (failed(j, _("Couldn't create the drawer '%s'.\n\n%s"), dst, errno) == ERR_CANCEL)
                *cancel = true;
            return false;
        }
        DIR *d = opendir(src);
        struct dirent *de;
        while (d && !*cancel && (de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;
            char s[PATH_MAX * 2], t[PATH_MAX * 2];
            snprintf(s, sizeof s, "%s/%s", src, de->d_name);
            snprintf(t, sizeof t, "%s/%s", dst, de->d_name);
            if (!copy_tree(j, s, t, cancel))
                ok = false;
        }
        if (d)
            closedir(d);
        chmod(dst, st.st_mode & 07777);
        struct timespec ts[2] = {st.st_atim, st.st_mtim};
        utimensat(AT_FDCWD, dst, ts, 0);
    } else if (S_ISREG(st.st_mode)) {
        ok = copy_file(j, src, dst, &st, cancel);
    } else {
        ok = false; /* (devices, pipes: not copied) */
    }
    j->done_files++;
    return ok && !*cancel;
}

static bool remove_tree(Job *j, const char *path, bool *cancel)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return true;
    report(j, path, false);
    bool ok = true;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *de;
        while (d && !*cancel && (de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;
            if (ecore_thread_check(j->th)) {
                *cancel = true;
                break;
            }
            char sub[PATH_MAX * 2];
            snprintf(sub, sizeof sub, "%s/%s", path, de->d_name);
            if (!remove_tree(j, sub, cancel))
                ok = false;
        }
        if (d)
            closedir(d);
    }
    if (*cancel)
        return false;
    for (;;) {
        if ((S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path)) == 0)
            break;
        int a = failed(j, _("Couldn't delete '%s'.\n\n%s"), path, errno);
        if (a == ERR_RETRY)
            continue;
        if (a == ERR_CANCEL)
            *cancel = true;
        ok = false;
        break;
    }
    if (j->op == NV_OP_DELETE || j->op == NV_OP_EMPTY_TRASH)
        j->done_files++;
    return ok;
}

/* moved: renamed when it can be (the same file system), else copied and deleted */
static bool move_tree(Job *j, const char *src, const char *dst, bool *cancel)
{
    struct stat dt;
    if (lstat(dst, &dt) == 0) {
        if (!may_replace(j, dst, cancel))
            return false;
        if (!remove_tree(j, dst, cancel))
            return false;
    }
    report(j, src, false);
    if (rename(src, dst) == 0) {
        j->done_files++;
        return true;
    }
    if (errno != EXDEV) {
        if (failed(j, _("Couldn't move '%s'.\n\n%s"), src, errno) == ERR_CANCEL)
            *cancel = true;
        return false;
    }
    return copy_tree(j, src, dst, cancel) && remove_tree(j, src, cancel);
}

/* a name free in a drawer: the same, or "Copy of NAME", "Copy 2 of NAME" (a copy into the
 * drawer it is in) */
static void dest_name(const char *src, const char *dir, char *out, size_t len)
{
    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;
    snprintf(out, len, "%s/%s", dir, base);
    if (strcmp(out, src))
        return;
    struct stat st;
    for (int i = 1; i < 1000; i++) {
        if (i == 1)
            snprintf(out, len, "%s/%s %s", dir, _("Copy of"), base);
        else
            snprintf(out, len, "%s/%s %d %s %s", dir, _("Copy"), i, _("of"), base);
        if (lstat(out, &st) != 0)
            return;
    }
}

static bool trash_one(Job *j, const char *src, bool *cancel)
{
    char t[PATH_MAX], files[PATH_MAX + 8], info[PATH_MAX + 8];
    nv_trash_dir(t, sizeof t);
    snprintf(files, sizeof files, "%s/files", t);
    snprintf(info, sizeof info, "%s/info", t);
    ecore_file_mkpath(files);
    ecore_file_mkpath(info);
    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;
    /* a free name in the trashcan, claimed by its .trashinfo */
    char name[NAME_MAX + 16], ipath[PATH_MAX * 2];
    int fd = -1;
    for (int i = 1; i < 10000 && fd < 0; i++) {
        if (i == 1)
            snprintf(name, sizeof name, "%s", base);
        else
            snprintf(name, sizeof name, "%s.%d", base, i);
        snprintf(ipath, sizeof ipath, "%s/%s.trashinfo", info, name);
        fd = open(ipath, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    }
    if (fd < 0) {
        if (failed(j, _("Couldn't move '%s' to the trashcan.\n\n%s"), src, errno) == ERR_CANCEL)
            *cancel = true;
        return false;
    }
    char enc[PATH_MAX * 3], when[32];
    url_encode(src, enc, sizeof enc);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%S", &tm);
    dprintf(fd, "[Trash Info]\nPath=%s\nDeletionDate=%s\n", enc, when);
    close(fd);
    char dst[PATH_MAX * 2];
    snprintf(dst, sizeof dst, "%s/%s", files, name);
    if (move_tree(j, src, dst, cancel))
        return true;
    unlink(ipath);
    return false;
}

static bool restore_one(Job *j, const char *src, bool *cancel)
{
    char origin[PATH_MAX];
    if (!nv_trash_origin(src, origin, sizeof origin)) {
        if (failed(j, _("Couldn't restore '%s'.\n\n%s"), src, ENOENT) == ERR_CANCEL)
            *cancel = true;
        return false;
    }
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", origin);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = 0;
        ecore_file_mkpath(dir);
    }
    if (!move_tree(j, src, origin, cancel))
        return false;
    char t[PATH_MAX], ipath[PATH_MAX * 2];
    nv_trash_dir(t, sizeof t);
    const char *base = strrchr(src, '/');
    snprintf(ipath, sizeof ipath, "%s/info/%s.trashinfo", t, base ? base + 1 : src);
    unlink(ipath);
    return true;
}

static void job_run(void *data, Ecore_Thread *th)
{
    Job *j = data;
    j->th = th;
    Eina_List *l;
    char *src;
    /* what there is to do, counted first (for the gauge) */
    if (j->op == NV_OP_EMPTY_TRASH) {
        char t[PATH_MAX], d[PATH_MAX + 8];
        nv_trash_dir(t, sizeof t);
        snprintf(d, sizeof d, "%s/files", t);
        Eina_List *names = ecore_file_ls(d);
        char *n;
        EINA_LIST_FREE(names, n) {
            char full[PATH_MAX * 2];
            snprintf(full, sizeof full, "%s/%s", d, n);
            j->sources = eina_list_append(j->sources, strdup(full));
            free(n);
        }
    }
    bool renames = j->op == NV_OP_MOVE || j->op == NV_OP_TRASH || j->op == NV_OP_RESTORE;
    EINA_LIST_FOREACH(j->sources, l, src) {
        if (renames)
            j->total_files++;
        else
            count(j, src);
    }
    j->started = ecore_time_get();
    bool cancel = false;
    EINA_LIST_FOREACH(j->sources, l, src) {
        if (cancel || ecore_thread_check(th))
            break;
        char dst[PATH_MAX * 2];
        /* a drawer is not copied or moved into itself (it would never end) */
        if ((j->op == NV_OP_COPY || j->op == NV_OP_MOVE) && j->dest) {
            size_t n = strlen(src);
            if (!strncmp(j->dest, src, n) && (j->dest[n] == '/' || j->dest[n] == 0)) {
                if (failed(j, _("A drawer can't be copied into itself:\n'%s'%s"), src, 0) == ERR_CANCEL)
                    cancel = true;
                continue;
            }
        }
        switch (j->op) {
        case NV_OP_COPY:
            dest_name(src, j->dest, dst, sizeof dst);
            copy_tree(j, src, dst, &cancel);
            break;
        case NV_OP_MOVE:
            dest_name(src, j->dest, dst, sizeof dst);
            if (strcmp(dst, src))
                move_tree(j, src, dst, &cancel);
            break;
        case NV_OP_DELETE:
        case NV_OP_EMPTY_TRASH:
            remove_tree(j, src, &cancel);
            if (j->op == NV_OP_EMPTY_TRASH) {
                char t[PATH_MAX], ipath[PATH_MAX * 2];
                nv_trash_dir(t, sizeof t);
                const char *base = strrchr(src, '/');
                snprintf(ipath, sizeof ipath, "%s/info/%s.trashinfo", t, base ? base + 1 : src);
                unlink(ipath);
            }
            break;
        case NV_OP_TRASH:
            trash_one(j, src, &cancel);
            break;
        case NV_OP_RESTORE:
            restore_one(j, src, &cancel);
            break;
        }
    }
    report(j, NULL, true);
}

/* ---------------------------------------------------------------- the progress window */

static void time_text(double secs, char *out, size_t len)
{
    long s = (long)secs;
    if (s >= 3600)
        snprintf(out, len, _("%ld hours"), s / 3600);
    else if (s >= 120)
        snprintf(out, len, _("%ld minutes"), s / 60);
    else
        snprintf(out, len, _("%ld seconds"), s);
}

static void bytes_text(double n, char *out, size_t len)
{
    if (n >= 1e9)
        snprintf(out, len, "%.1f GB", n / 1e9);
    else if (n >= 1e6)
        snprintf(out, len, "%.1f MB", n / 1e6);
    else
        snprintf(out, len, "%.0f kB", n / 1e3);
}

static void window_update(Job *j)
{
    if (!j->w)
        return;
    char buf[PATH_MAX + 64];
    char *m = elm_entry_utf8_to_markup(j->current);
    elm_object_text_set(j->lab_file, m ? m : "");
    free(m);
    double frac = j->total_bytes ? (double)j->done_bytes / (double)j->total_bytes
                  : j->total_files ? (double)j->done_files / (double)j->total_files : 0;
    elm_progressbar_value_set(j->bar, frac > 1 ? 1 : frac);
    snprintf(buf, sizeof buf, "%s %llu / %llu", _("Files done:"), j->done_files, j->total_files);
    elm_object_text_set(j->lab_files, buf);
    double el = ecore_time_get() - j->started;
    if (j->total_bytes && j->done_bytes && el > 0.5) {
        double speed = (double)j->done_bytes / el;
        char t[64], sp[32];
        time_text(((double)j->total_bytes - (double)j->done_bytes) / speed, t, sizeof t);
        bytes_text(speed, sp, sizeof sp);
        snprintf(buf, sizeof buf, _("Time remaining: %s.  Speed: %s/s"), t, sp);
        elm_object_text_set(j->lab_time, buf);
    }
}

static void on_stop(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    Job *j = data;
    if (j->th)
        ecore_thread_cancel(j->th);
}

static void window_closed(void *data, Nv_Window *w)
{
    Job *j = data;
    if (j->th && !j->done)
        ecore_thread_cancel(j->th);
    j->w = NULL;
    evas_object_del(w->win);
}

static const char *op_title(Job *j)
{
    bool many = j->total_files > 1 || eina_list_count(j->sources) > 1;
    switch (j->op) {
    case NV_OP_COPY: return many ? _("Copying files...") : _("Copying file...");
    case NV_OP_MOVE: return many ? _("Moving files...") : _("Moving file...");
    case NV_OP_TRASH: return _("Moving to Trashcan...");
    case NV_OP_RESTORE: return _("Restoring...");
    default: return many ? _("Deleting files...") : _("Deleting file...");
    }
}

static Eina_Bool show_window(void *data)
{
    Job *j = data;
    j->show_timer = NULL;
    if (j->done)
        return ECORE_CALLBACK_CANCEL;
    if (j->asking) {
        j->show_timer = ecore_timer_add(0.3, show_window, j);
        return ECORE_CALLBACK_CANCEL;
    }
    j->w = nv_window_new("nirvana-progress", op_title(j), 440, 150);
    j->w->on_close = window_closed;
    j->w->data = j;
    Evas_Object *win = j->w->win;
    Evas_Object *box = elm_box_add(win);
    elm_box_padding_set(box, 0, 6);
    evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    j->lab_file = elm_label_add(win);
    elm_label_ellipsis_set(j->lab_file, EINA_TRUE);
    evas_object_size_hint_weight_set(j->lab_file, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(j->lab_file, EVAS_HINT_FILL, 0.5);
    j->bar = elm_progressbar_add(win);
    evas_object_size_hint_weight_set(j->bar, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(j->bar, EVAS_HINT_FILL, 0.5);
    j->lab_files = elm_label_add(win);
    evas_object_size_hint_align_set(j->lab_files, 0.0, 0.5);
    j->lab_time = elm_label_add(win);
    evas_object_size_hint_align_set(j->lab_time, 0.0, 0.5);
    elm_object_text_set(j->lab_time, _("Buffering..."));
    Evas_Object *stop = elm_button_add(win);
    elm_object_text_set(stop, _("Stop"));
    evas_object_smart_callback_add(stop, "clicked", on_stop, j);
    Evas_Object *objs[] = {j->lab_file, j->bar, j->lab_files, j->lab_time, stop};
    for (size_t i = 0; i < sizeof objs / sizeof *objs; i++) {
        elm_box_pack_end(box, objs[i]);
        evas_object_show(objs[i]);
    }
    nv_window_content_set(j->w, box);
    evas_object_show(box);
    nv_window_show(j->w);
    window_update(j);
    return ECORE_CALLBACK_CANCEL;
}

/* ---------------------------------------------------------------- the main loop's side */

static void ask_main(void *data, Ecore_Thread *th, void *msg)
{
    (void)th;
    Job *j = data;
    Question *q = msg;
    if (!q) {
        window_update(j);
        return;
    }
    /* (a progress window not shown yet waits: it would come up over the question) */
    j->asking = true;
    const char *title = j->op == NV_OP_COPY ? _("Copy") : j->op == NV_OP_MOVE ? _("Move") : _("Delete");
    nv_request(title, q->text,
               q->skip_only ? _("*_Skip|_Cancel")
               : q->error   ? _("*_Retry|_Skip|_Cancel")
                            : _("*_Replace|Replace All|_Skip|Skip All|_Cancel"),
               ask_answer, j);
    free(q);
}

static void job_free(Job *j)
{
    char *s;
    EINA_LIST_FREE(j->sources, s)
        free(s);
    free(j->dest);
    eina_condition_free(&j->cond);
    eina_lock_free(&j->lock);
    free(j);
}

static void job_end(void *data, Ecore_Thread *th)
{
    (void)th;
    Job *j = data;
    j->done = true;
    j->th = NULL;
    if (j->show_timer)
        ecore_timer_del(j->show_timer);
    if (j->w) {
        Nv_Window *w = j->w;
        w->on_close = NULL;
        j->w = NULL;
        nv_window_close(w);
    }
    /* the listers showing what changed are told (and the desktop's trashcan) */
    nv_listers_refresh();
    nv_backdrop_trash_changed();
    job_free(j);
}

void nv_file_op(Nv_File_Op op, Eina_List *sources, const char *dest)
{
    if (!sources && op != NV_OP_EMPTY_TRASH)
        return;
    Job *j = calloc(1, sizeof *j);
    j->op = op;
    j->sources = sources;
    j->dest = dest ? strdup(dest) : NULL;
    eina_lock_new(&j->lock);
    eina_condition_new(&j->cond, &j->lock);
    /* (shown when the work takes longer than a moment; NIRVANA_PROGRESS_DELAY changes it) */
    const char *delay = getenv("NIRVANA_PROGRESS_DELAY");
    j->show_timer = ecore_timer_add(delay ? atof(delay) : 0.4, show_window, j);
    ecore_thread_feedback_run(job_run, ask_main, job_end, job_end, j, EINA_FALSE);
}
