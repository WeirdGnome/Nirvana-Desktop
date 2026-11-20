/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the lister - a drawer's contents in a window.
 *
 * Spatial by default: each drawer has its own window, and opened again it comes to the
 * front; in browser mode (Window menu) a lister goes into the drawers it opens itself.
 * The contents are shown as icons or as a list (name, size, date), sorted by name, date,
 * size or type. A directory is read in a thread (Ecore_Thread) and comes in batches; an
 * AppImage's icon is read in the same thread (libnirvana: nothing is run). Archives
 * (.zip, .tar.*, .7z, .lha, .iso, ...) open as drawers too, read through libarchive; a
 * file opened from one is unpacked into RAM: first.
 *
 * The filter field shows only the names that match its pattern: "#?" stands for any
 * characters, "?" for one, "(a|b)" for either, case not minded; a word without
 * wildcards matches anywhere in a name. Icons can be dragged out (as file:// URIs - to
 * the dock, say). A right click gives the item's menu: Open, Information, and for an
 * AppImage Extract Icon.
 */
#define _GNU_SOURCE
#include "desktop.h"

#include <Efreet.h>
#include <Efreet_Mime.h>
#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "nirvana_appimage.h"

typedef enum { VIEW_ICONS, VIEW_LIST } View;
typedef enum { SORT_NAME, SORT_DATE, SORT_SIZE, SORT_TYPE } Sort;

typedef struct Lister Lister;

typedef struct Entry {
    Lister *ls;
    char *name;
    bool dir, exec, link;
    off_t size;
    time_t mtime;
    unsigned char *icon; /* an AppImage's icon, read in the thread */
    size_t icon_len;
    const char *icon_fmt;
} Entry;

typedef struct Scan {
    Lister *ls;
    char path[PATH_MAX];
    char inner[PATH_MAX];
    bool archive;
} Scan;

struct Lister {
    Nv_Window *w;
    char path[PATH_MAX];  /* the directory, or the archive */
    char inner[PATH_MAX]; /* in an archive: the drawer in it ("" for its top) */
    bool archive;
    View view;
    Sort sort;
    bool browser, show_hidden;
    Evas_Object *box, *row, *view_obj, *filter, *location, *status, *inwin;
    Ecore_Thread *scan;
    Scan *scan_ctx;
    Eina_List *entries; /* Entry: all of them (the filter hides some) */
    char *pattern;
    int shown;
    Entry *menu_entry;  /* the item a context menu is for */
};

static Eina_List *listers;
static Elm_Gengrid_Item_Class *gitc;
static Elm_Genlist_Item_Class *litc;
static bool default_browser;

static void rescan(Lister *ls);
static void rebuild_view(Lister *ls);

/* ---------------------------------------------------------------- archives */

static const char *const archive_exts[] = {
    ".zip", ".tar", ".tar.gz", ".tgz", ".tar.bz2", ".tbz2", ".tar.xz", ".txz", ".tar.zst", ".tzst",
    ".tar.lz4", ".7z", ".lha", ".lzh", ".iso", ".cpio", ".rar", ".cab", ".ar", ".xar", ".jar",
};

static bool is_archive(const char *path)
{
    size_t n = strlen(path);
    for (size_t i = 0; i < sizeof archive_exts / sizeof *archive_exts; i++) {
        size_t e = strlen(archive_exts[i]);
        if (n > e && !strcasecmp(path + n - e, archive_exts[i]))
            return true;
    }
    return false;
}

static struct archive *archive_begin(const char *file)
{
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, file, 65536) != ARCHIVE_OK) {
        archive_read_free(a);
        return NULL;
    }
    return a;
}

/* an entry's name without "./" in front or a slash behind */
static void clean_entry_name(const char *in, char *out, size_t len)
{
    while (in[0] == '.' && in[1] == '/')
        in += 2;
    while (*in == '/')
        in++;
    snprintf(out, len, "%s", in);
    size_t n = strlen(out);
    while (n && out[n - 1] == '/')
        out[--n] = 0;
}

/* a file of an archive unpacked into RAM: (its path in *out) */
static int archive_extract(const char *file, const char *entry, char *out, size_t len)
{
    char ram[PATH_MAX];
    if (nirvana_to_posix("RAM:", ram, sizeof ram) < 0)
        snprintf(ram, sizeof ram, "/tmp");
    char dir[PATH_MAX + 32];
    snprintf(dir, sizeof dir, "%s/Archives", ram);
    mkdir(dir, 0700);
    const char *base = strrchr(entry, '/');
    base = base ? base + 1 : entry;
    if (snprintf(out, len, "%s/%s", dir, base) >= (int)len)
        return -ENAMETOOLONG;
    struct archive *a = archive_begin(file);
    if (!a)
        return -EIO;
    struct archive_entry *e;
    int r = -ENOENT;
    char name[PATH_MAX];
    while (archive_read_next_header(a, &e) == ARCHIVE_OK) {
        clean_entry_name(archive_entry_pathname(e) ? archive_entry_pathname(e) : "", name, sizeof name);
        if (strcmp(name, entry))
            continue;
        int fd = open(out, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) {
            r = -errno;
            break;
        }
        r = archive_read_data_into_fd(a, fd) == ARCHIVE_OK ? 0 : -EIO;
        close(fd);
        break;
    }
    archive_read_free(a);
    return r;
}

/* ---------------------------------------------------------------- the pattern */

/* "#?.(png|jpg)" -> "*.@(png|jpg)"; a pattern without wildcards matches anywhere */
static char *to_fnmatch(const char *in)
{
    if (!in || !*in)
        return NULL;
    bool wild = strpbrk(in, "#?*([|") != NULL;
    size_t n = strlen(in);
    char *out = malloc(n * 2 + 4), *o = out;
    if (!wild)
        *o++ = '*';
    for (const char *p = in; *p; p++) {
        if (p[0] == '#' && p[1] == '?') {
            *o++ = '*';
            p++;
        } else if (*p == '(') {
            *o++ = '@';
            *o++ = '(';
        } else {
            *o++ = *p;
        }
    }
    if (!wild)
        *o++ = '*';
    *o = 0;
    return out;
}

static bool visible(Lister *ls, Entry *e)
{
    if (!ls->show_hidden && e->name[0] == '.')
        return false;
    if (ls->pattern && fnmatch(ls->pattern, e->name, FNM_CASEFOLD | FNM_EXTMATCH) != 0)
        return false;
    return true;
}

/* ---------------------------------------------------------------- items */

static void entry_path(Entry *e, char *out, size_t len)
{
    Lister *ls = e->ls;
    if (ls->archive)
        snprintf(out, len, "%s%s%s", ls->inner, *ls->inner ? "/" : "", e->name);
    else
        snprintf(out, len, "%s%s%s", ls->path, strcmp(ls->path, "/") ? "/" : "", e->name);
}

static char *item_text(void *data, Evas_Object *o, const char *part)
{
    (void)o;
    Entry *e = data;
    if (!strcmp(part, "elm.text.sub")) {
        char buf[128], when[64];
        struct tm tm;
        localtime_r(&e->mtime, &tm);
        strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm);
        if (e->dir)
            snprintf(buf, sizeof buf, "%s    %s", _("Drawer"), e->mtime ? when : "");
        else if (e->size >= 1 << 20)
            snprintf(buf, sizeof buf, "%.1f MB    %s", (double)e->size / (1 << 20), e->mtime ? when : "");
        else if (e->size >= 1024)
            snprintf(buf, sizeof buf, "%.1f KB    %s", (double)e->size / 1024, e->mtime ? when : "");
        else
            snprintf(buf, sizeof buf, "%lld B    %s", (long long)e->size, e->mtime ? when : "");
        return strdup(buf);
    }
    return strdup(e->name);
}

static Evas_Object *item_content(void *data, Evas_Object *o, const char *part)
{
    Entry *e = data;
    if (strcmp(part, "elm.swallow.icon"))
        return NULL;
    int size = e->ls->view == VIEW_ICONS ? 48 : 24;
    if (e->icon) {
        Evas_Object *img = elm_image_add(o);
        if (elm_image_memfile_set(img, e->icon, e->icon_len, e->icon_fmt && *e->icon_fmt ? e->icon_fmt : NULL, NULL)) {
            evas_object_size_hint_min_set(img, size, size);
            return img;
        }
        evas_object_del(img);
    }
    if (e->ls->archive)
        return nv_icon_for_path(o, e->name, e->dir, size); /* (by its name only) */
    char full[PATH_MAX * 2];
    entry_path(e, full, sizeof full);
    if (!e->dir && nv_is_appimage(full)) /* (its icon could not be read) */
        return nv_icon_for_path(o, "x", false, size);
    return nv_icon_for_path(o, full, e->dir, size);
}

static const char *type_of(const Entry *e)
{
    const char *dot = strrchr(e->name, '.');
    return dot ? dot + 1 : "";
}

static int entry_cmp(const void *a, const void *b)
{
    const Entry *x = elm_object_item_data_get(a), *y = elm_object_item_data_get(b);
    if (x->dir != y->dir)
        return x->dir ? -1 : 1;
    int r = 0;
    switch (x->ls->sort) {
    case SORT_DATE: r = x->mtime < y->mtime ? 1 : x->mtime > y->mtime ? -1 : 0; break;
    case SORT_SIZE: r = x->size < y->size ? 1 : x->size > y->size ? -1 : 0; break;
    case SORT_TYPE: r = strcasecmp(type_of(x), type_of(y)); break;
    default: break;
    }
    return r ? r : strcasecmp(x->name, y->name);
}

static void status_update(Lister *ls)
{
    char buf[128];
    snprintf(buf, sizeof buf, ls->scan ? _("%d items (reading...)") : _("%d items"), ls->shown);
    elm_object_text_set(ls->status, buf);
}

static void show_entry(Lister *ls, Entry *e)
{
    if (!visible(ls, e))
        return;
    if (ls->view == VIEW_ICONS)
        elm_gengrid_item_sorted_insert(ls->view_obj, gitc, e, entry_cmp, NULL, NULL);
    else
        elm_genlist_item_sorted_insert(ls->view_obj, litc, e, NULL, ELM_GENLIST_ITEM_NONE, entry_cmp, NULL, NULL);
    ls->shown++;
}

static void refilter(Lister *ls)
{
    if (ls->view == VIEW_ICONS)
        elm_gengrid_clear(ls->view_obj);
    else
        elm_genlist_clear(ls->view_obj);
    ls->shown = 0;
    Eina_List *l;
    Entry *e;
    EINA_LIST_FOREACH(ls->entries, l, e)
        show_entry(ls, e);
    status_update(ls);
}

/* ---------------------------------------------------------------- reading in a thread */

static Entry *entry_new(const char *name)
{
    Entry *e = calloc(1, sizeof *e);
    e->name = strdup(name);
    return e;
}

static void entry_free(Entry *e)
{
    free(e->name);
    free(e->icon);
    free(e);
}

static void scan_dir(Scan *sc, Ecore_Thread *th)
{
    DIR *d = opendir(sc->path);
    if (!d)
        return;
    Eina_List *batch = NULL;
    struct dirent *de;
    int n = 0;
    while ((de = readdir(d)) && !ecore_thread_check(th)) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        Entry *e = entry_new(de->d_name);
        char full[PATH_MAX * 2];
        snprintf(full, sizeof full, "%s/%s", sc->path, de->d_name);
        struct stat st, lst;
        if (lstat(full, &lst) == 0)
            e->link = S_ISLNK(lst.st_mode);
        if (stat(full, &st) == 0) {
            e->dir = S_ISDIR(st.st_mode);
            e->exec = !e->dir && (st.st_mode & 0111);
            e->size = st.st_size;
            e->mtime = st.st_mtime;
        }
        if (!e->dir && nv_is_appimage(e->name)) {
            nirvana_appimage *img;
            if (nirvana_appimage_open(full, &img) == 0) {
                const char *fmt = NULL;
                if (nirvana_appimage_icon(img, &e->icon, &e->icon_len, &fmt) == 0)
                    e->icon_fmt = fmt;
                nirvana_appimage_close(img);
            }
        }
        batch = eina_list_append(batch, e);
        if (++n % 64 == 0) {
            ecore_thread_feedback(th, batch);
            batch = NULL;
        }
    }
    closedir(d);
    if (batch)
        ecore_thread_feedback(th, batch);
}

/* an archive's entries one level below the drawer looked at; drawers that only show in
 * their files' paths are made up */
static void scan_archive(Scan *sc, Ecore_Thread *th)
{
    struct archive *a = archive_begin(sc->path);
    if (!a)
        return;
    Eina_Hash *seen = eina_hash_string_superfast_new(NULL);
    Eina_List *batch = NULL;
    size_t pl = strlen(sc->inner);
    struct archive_entry *ae;
    char name[PATH_MAX];
    while (archive_read_next_header(a, &ae) == ARCHIVE_OK && !ecore_thread_check(th)) {
        clean_entry_name(archive_entry_pathname(ae) ? archive_entry_pathname(ae) : "", name, sizeof name);
        const char *rest = name;
        if (pl) {
            if (strncmp(name, sc->inner, pl) || name[pl] != '/')
                continue;
            rest = name + pl + 1;
        }
        if (!*rest)
            continue;
        const char *slash = strchr(rest, '/');
        char first[PATH_MAX];
        snprintf(first, sizeof first, "%.*s", slash ? (int)(slash - rest) : (int)strlen(rest), rest);
        Entry *e = eina_hash_find(seen, first);
        bool dir = slash || archive_entry_filetype(ae) == AE_IFDIR;
        if (!e) {
            e = entry_new(first);
            eina_hash_add(seen, first, e);
            batch = eina_list_append(batch, e);
        }
        if (dir) {
            e->dir = true;
        } else {
            e->size = archive_entry_size(ae);
            e->exec = (archive_entry_perm(ae) & 0111) != 0;
        }
        if (!slash)
            e->mtime = archive_entry_mtime(ae);
        archive_read_data_skip(a);
    }
    archive_read_free(a);
    eina_hash_free(seen);
    if (batch)
        ecore_thread_feedback(th, batch);
}

static void scan_run(void *data, Ecore_Thread *th)
{
    Scan *sc = data;
    if (sc->archive)
        scan_archive(sc, th);
    else
        scan_dir(sc, th);
}

static void scan_feedback(void *data, Ecore_Thread *th, void *msg)
{
    (void)th;
    Scan *sc = data;
    Eina_List *batch = msg;
    Entry *e;
    if (!sc->ls) {
        EINA_LIST_FREE(batch, e)
            entry_free(e);
        return;
    }
    EINA_LIST_FREE(batch, e) {
        e->ls = sc->ls;
        sc->ls->entries = eina_list_append(sc->ls->entries, e);
        show_entry(sc->ls, e);
    }
    status_update(sc->ls);
}

static void scan_end(void *data, Ecore_Thread *th)
{
    (void)th;
    Scan *sc = data;
    if (sc->ls) {
        sc->ls->scan = NULL;
        sc->ls->scan_ctx = NULL;
        status_update(sc->ls);
    }
    free(sc);
}

static void entries_clear(Lister *ls)
{
    if (ls->view == VIEW_ICONS)
        elm_gengrid_clear(ls->view_obj);
    else
        elm_genlist_clear(ls->view_obj);
    Entry *e;
    EINA_LIST_FREE(ls->entries, e)
        entry_free(e);
    ls->shown = 0;
}

static void scan_stop(Lister *ls)
{
    if (ls->scan) {
        if (ls->scan_ctx)
            ls->scan_ctx->ls = NULL; /* the old reading's results go nowhere */
        ecore_thread_cancel(ls->scan);
        ls->scan = NULL;
        ls->scan_ctx = NULL;
    }
}

static void rescan(Lister *ls)
{
    scan_stop(ls);
    entries_clear(ls);
    Scan *sc = calloc(1, sizeof *sc);
    sc->ls = ls;
    sc->archive = ls->archive;
    snprintf(sc->path, sizeof sc->path, "%s", ls->path);
    snprintf(sc->inner, sizeof sc->inner, "%s", ls->inner);
    ls->scan_ctx = sc;
    ls->scan = ecore_thread_feedback_run(scan_run, scan_feedback, scan_end, scan_end, sc, EINA_FALSE);
    status_update(ls);
}

/* ---------------------------------------------------------------- where the lister is */

static void location_text(Lister *ls, char *out, size_t len)
{
    char np[PATH_MAX];
    if (posix_to_nirvana(ls->path, np, sizeof np) < 0)
        snprintf(np, sizeof np, "%s", ls->path);
    if (ls->archive && *ls->inner)
        snprintf(out, len, "%s/%s", np, ls->inner);
    else
        snprintf(out, len, "%s", np);
}

static void set_place(Lister *ls, const char *path, const char *inner, bool archive)
{
    snprintf(ls->path, sizeof ls->path, "%s", path);
    snprintf(ls->inner, sizeof ls->inner, "%s", inner ? inner : "");
    ls->archive = archive;
    char title[PATH_MAX * 2];
    location_text(ls, title, sizeof title);
    nv_window_title_set(ls->w, title);
    elm_entry_entry_set(ls->location, title);
    rescan(ls);
}

static Lister *find_lister(const char *path, const char *inner)
{
    Eina_List *l;
    Lister *ls;
    EINA_LIST_FOREACH(listers, l, ls)
        if (!strcmp(ls->path, path) && !strcmp(ls->inner, inner ? inner : ""))
            return ls;
    return NULL;
}

static Lister *lister_new(const char *path, const char *inner, bool archive);

/* a drawer opened from a lister: in it (browser mode) or in its own window */
static void go(Lister *from, const char *path, const char *inner, bool archive)
{
    Lister *other = find_lister(path, inner);
    if (other && other != from) {
        elm_win_raise(other->w->win);
        elm_win_activate(other->w->win);
        return;
    }
    if (from && from->browser) {
        set_place(from, path, inner, archive);
        return;
    }
    if (!other)
        lister_new(path, inner, archive);
}

static void open_entry(Lister *ls, Entry *e)
{
    char full[PATH_MAX * 2];
    entry_path(e, full, sizeof full);
    if (ls->archive) {
        if (e->dir) {
            go(ls, ls->path, full, true);
            return;
        }
        char out[PATH_MAX];
        int r = archive_extract(ls->path, full, out, sizeof out);
        if (r < 0) {
            char msg[PATH_MAX + 64];
            snprintf(msg, sizeof msg, _("%s could not be unpacked: %s"), e->name, strerror(-r));
            nv_message(_("Open"), msg);
            return;
        }
        nv_open_path(out);
        return;
    }
    if (e->dir)
        go(ls, full, "", false);
    else if (is_archive(full))
        go(ls, full, "", true);
    else
        nv_open_path(full);
}

static void open_parent(Lister *ls)
{
    if (ls->archive) {
        if (*ls->inner) {
            char in[PATH_MAX];
            snprintf(in, sizeof in, "%s", ls->inner);
            char *slash = strrchr(in, '/');
            if (slash)
                *slash = 0;
            else
                *in = 0;
            go(ls, ls->path, in, true);
            return;
        }
        /* the archive's own drawer */
        char dir[PATH_MAX];
        snprintf(dir, sizeof dir, "%s", ls->path);
        char *slash = strrchr(dir, '/');
        if (slash == dir)
            slash[1] = 0;
        else if (slash)
            *slash = 0;
        go(ls, dir, "", false);
        return;
    }
    char parent[PATH_MAX];
    snprintf(parent, sizeof parent, "%s", ls->path);
    char *slash = strrchr(parent, '/');
    if (!slash || !strcmp(parent, "/"))
        return;
    if (slash == parent)
        slash[1] = 0;
    else
        *slash = 0;
    go(ls, parent, "", false);
}

/* ---------------------------------------------------------------- the item's menu */

static void information(Lister *ls, Entry *e)
{
    char full[PATH_MAX * 2], np[PATH_MAX * 2], msg[16384], when[64];
    entry_path(e, full, sizeof full);
    if (ls->archive || posix_to_nirvana(full, np, sizeof np) < 0)
        snprintf(np, sizeof np, "%s", full);
    struct tm tm;
    localtime_r(&e->mtime, &tm);
    strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm);
    const char *mime = !ls->archive && !e->dir ? efreet_mime_type_get(full) : NULL;
    int n = snprintf(msg, sizeof msg, "%s\n\n%s: %s\n%s: %lld\n%s: %s\n%s: %s", e->name, _("Path"), np, _("Size"),
                     (long long)e->size, _("Changed"), when, _("Type"),
                     e->dir ? _("Drawer") : mime ? mime : "?");
    if (!ls->archive && !e->dir && nv_is_appimage(full)) {
        nirvana_appimage *img;
        if (nirvana_appimage_open(full, &img) == 0) {
            nirvana_appimage_meta m;
            n += snprintf(msg + n, sizeof msg - (size_t)n, "\n\nAppImage (%s)",
                          nirvana_appimage_kind_name(nirvana_appimage_get_kind(img)));
            if (nirvana_appimage_metadata(img, &m) == 0) {
                n += snprintf(msg + n, sizeof msg - (size_t)n, "\n%s: %s", _("Name"), m.name ? m.name : "");
                if (m.version)
                    n += snprintf(msg + n, sizeof msg - (size_t)n, "\n%s: %s", _("Version"), m.version);
                if (m.comment)
                    n += snprintf(msg + n, sizeof msg - (size_t)n, "\n%s", m.comment);
            }
            nirvana_appimage_meta_free(&m);
            nirvana_appimage_close(img);
        }
    }
    nv_message(_("Information"), msg);
}

typedef struct Extract {
    Lister *ls;
    char appimage[PATH_MAX];
} Extract;

static void extract_done(void *data, Evas_Object *o, void *event)
{
    (void)o;
    Extract *x = data;
    const char *dest = event;
    Lister *ls = x->ls;
    if (dest && *dest) {
        nirvana_appimage *img;
        unsigned char *icon = NULL;
        size_t len = 0;
        const char *fmt = "";
        int r = nirvana_appimage_open(x->appimage, &img);
        if (r == 0) {
            r = nirvana_appimage_icon(img, &icon, &len, &fmt);
            nirvana_appimage_close(img);
        }
        if (r == 0) {
            FILE *f = fopen(dest, "wb");
            if (!f || fwrite(icon, 1, len, f) != len)
                r = -errno;
            if (f)
                fclose(f);
        }
        free(icon);
        char msg[PATH_MAX + 128];
        if (r < 0)
            snprintf(msg, sizeof msg, _("The icon could not be saved: %s"), strerror(-r));
        else
            snprintf(msg, sizeof msg, _("The icon (%s) was saved as %s."), *fmt ? fmt : "?", dest);
        nv_message(_("Extract Icon"), msg);
        if (r == 0 && !ls->archive)
            rescan(ls);
    }
    evas_object_del(ls->inwin);
    ls->inwin = NULL;
    free(x);
}

static void extract_icon(Lister *ls, Entry *e)
{
    if (ls->inwin)
        return;
    Extract *x = calloc(1, sizeof *x);
    x->ls = ls;
    entry_path(e, x->appimage, sizeof x->appimage);
    /* the icon's format decides the file's ending */
    const char *ext = "png";
    nirvana_appimage *img;
    if (nirvana_appimage_open(x->appimage, &img) == 0) {
        unsigned char *d;
        size_t len;
        const char *fmt;
        if (nirvana_appimage_icon(img, &d, &len, &fmt) == 0) {
            if (*fmt)
                ext = fmt;
            free(d);
        }
        nirvana_appimage_close(img);
    }
    char name[NAME_MAX + 8];
    snprintf(name, sizeof name, "%.*s.%s", (int)(strlen(e->name) - 9), e->name, ext);

    ls->inwin = elm_win_inwin_add(ls->w->win);
    Evas_Object *fs = elm_fileselector_add(ls->w->win);
    elm_fileselector_is_save_set(fs, EINA_TRUE);
    elm_fileselector_expandable_set(fs, EINA_FALSE);
    elm_fileselector_path_set(fs, access(ls->path, W_OK) == 0 ? ls->path : getenv("HOME"));
    elm_fileselector_current_name_set(fs, name);
    evas_object_size_hint_weight_set(fs, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(fs, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_smart_callback_add(fs, "done", extract_done, x);
    elm_win_inwin_content_set(ls->inwin, fs);
    evas_object_show(fs);
    elm_win_inwin_activate(ls->inwin);
}

static void cm_open(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    Lister *ls = data;
    if (ls->menu_entry)
        open_entry(ls, ls->menu_entry);
}

static void cm_info(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    Lister *ls = data;
    if (ls->menu_entry)
        information(ls, ls->menu_entry);
}

static void cm_extract(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    Lister *ls = data;
    if (ls->menu_entry)
        extract_icon(ls, ls->menu_entry);
}

static void cm_copy_path(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    Lister *ls = data;
    if (!ls->menu_entry)
        return;
    char full[PATH_MAX * 2];
    entry_path(ls->menu_entry, full, sizeof full);
    elm_cnp_selection_set(ls->w->win, ELM_SEL_TYPE_CLIPBOARD, ELM_SEL_FORMAT_TEXT, full, strlen(full));
}

static Elm_Object_Item *item_at(Lister *ls, int x, int y)
{
    int pos;
    if (ls->view == VIEW_ICONS)
        return elm_gengrid_at_xy_item_get(ls->view_obj, x, y, NULL, NULL);
    return elm_genlist_at_xy_item_get(ls->view_obj, x, y, &pos);
}

static void on_view_down(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)e;
    (void)o;
    Lister *ls = data;
    Evas_Event_Mouse_Down *ev = event;
    if (ev->button != 3)
        return;
    Elm_Object_Item *it = item_at(ls, ev->canvas.x, ev->canvas.y);
    if (!it)
        return;
    Entry *en = elm_object_item_data_get(it);
    ls->menu_entry = en;
    Evas_Object *menu = elm_menu_add(ls->w->win);
    elm_menu_item_add(menu, NULL, NULL, _("Open"), cm_open, ls);
    elm_menu_item_add(menu, NULL, NULL, _("Information..."), cm_info, ls);
    if (!ls->archive) {
        elm_menu_item_add(menu, NULL, NULL, _("Copy Path"), cm_copy_path, ls);
        char full[PATH_MAX * 2];
        entry_path(en, full, sizeof full);
        if (!en->dir && nv_is_appimage(full)) {
            elm_menu_item_separator_add(menu, NULL);
            elm_menu_item_add(menu, NULL, NULL, _("Extract Icon..."), cm_extract, ls);
        }
    }
    elm_menu_move(menu, ev->canvas.x, ev->canvas.y);
    evas_object_show(menu);
}

/* ---------------------------------------------------------------- dragging out */

static Elm_Object_Item *drag_item_get(Evas_Object *obj, Evas_Coord x, Evas_Coord y, int *xpos, int *ypos)
{
    Lister *ls = evas_object_data_get(obj, "lister");
    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 0;
    return ls ? item_at(ls, x, y) : NULL;
}

static Evas_Object *drag_icon(void *data, Evas_Object *win, Evas_Coord *xoff, Evas_Coord *yoff)
{
    Entry *e = data;
    char full[PATH_MAX * 2];
    entry_path(e, full, sizeof full);
    Evas_Object *icon = nv_icon_for_path(win, full, e->dir, 48);
    evas_object_resize(icon, 48, 48);
    if (xoff)
        *xoff = 24;
    if (yoff)
        *yoff = 24;
    return icon;
}

static Eina_Bool drag_data_get(Evas_Object *obj, Elm_Object_Item *it, Elm_Drag_User_Info *info)
{
    Lister *ls = evas_object_data_get(obj, "lister");
    Entry *e = elm_object_item_data_get(it);
    if (!ls || !e || ls->archive)
        return EINA_FALSE;
    static char uri[PATH_MAX * 2 + 16];
    char full[PATH_MAX * 2];
    entry_path(e, full, sizeof full);
    snprintf(uri, sizeof uri, "file://%s\n", full);
    info->format = ELM_SEL_FORMAT_TARGETS;
    info->data = uri;
    info->action = ELM_XDND_ACTION_COPY;
    info->createicon = drag_icon;
    info->createdata = e;
    return EINA_TRUE;
}

/* ---------------------------------------------------------------- the view */

static void on_double(void *data, Evas_Object *o, void *event)
{
    (void)o;
    open_entry(data, elm_object_item_data_get(event));
}

static void rebuild_view(Lister *ls)
{
    if (ls->view_obj) {
        elm_box_unpack(ls->box, ls->view_obj);
        evas_object_del(ls->view_obj);
    }
    Evas_Object *win = ls->w->win;
    if (ls->view == VIEW_ICONS) {
        ls->view_obj = elm_gengrid_add(win);
        elm_gengrid_item_size_set(ls->view_obj, ELM_SCALE_SIZE(92), ELM_SCALE_SIZE(84));
        elm_gengrid_multi_select_set(ls->view_obj, EINA_TRUE);
        elm_gengrid_align_set(ls->view_obj, 0.0, 0.0);
    } else {
        ls->view_obj = elm_genlist_add(win);
        elm_genlist_multi_select_set(ls->view_obj, EINA_TRUE);
        elm_genlist_homogeneous_set(ls->view_obj, EINA_TRUE);
    }
    evas_object_data_set(ls->view_obj, "lister", ls);
    evas_object_size_hint_weight_set(ls->view_obj, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(ls->view_obj, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_smart_callback_add(ls->view_obj, "clicked,double", on_double, ls);
    evas_object_smart_callback_add(ls->view_obj, "activated", on_double, ls);
    evas_object_event_callback_add(ls->view_obj, EVAS_CALLBACK_MOUSE_DOWN, on_view_down, ls);
    elm_drag_item_container_add(ls->view_obj, 0.3, 0.25, drag_item_get, drag_data_get);
    elm_box_pack_after(ls->box, ls->view_obj, ls->row);
    evas_object_show(ls->view_obj);
    ls->shown = 0;
    Eina_List *l;
    Entry *e;
    EINA_LIST_FOREACH(ls->entries, l, e)
        show_entry(ls, e);
    status_update(ls);
}

/* ---------------------------------------------------------------- the Window menu */

static void m_parent(void *data, Nv_Menu_Item *it) { (void)it; open_parent(data); }
static void m_update(void *data, Nv_Menu_Item *it) { (void)it; rescan(data); }
static void m_hidden(void *data, Nv_Menu_Item *it)
{
    Lister *ls = data;
    ls->show_hidden = it->checked;
    refilter(ls);
}
static void m_browser(void *data, Nv_Menu_Item *it)
{
    Lister *ls = data;
    ls->browser = it->checked;
    default_browser = ls->browser; /* (new listers follow the last choice) */
}
static void m_select_all(void *data, Nv_Menu_Item *it)
{
    (void)it;
    Lister *ls = data;
    if (ls->view == VIEW_ICONS) {
        for (Elm_Object_Item *gi = elm_gengrid_first_item_get(ls->view_obj); gi; gi = elm_gengrid_item_next_get(gi))
            elm_gengrid_item_selected_set(gi, EINA_TRUE);
    } else {
        for (Elm_Object_Item *gi = elm_genlist_first_item_get(ls->view_obj); gi; gi = elm_genlist_item_next_get(gi))
            elm_genlist_item_selected_set(gi, EINA_TRUE);
    }
}

static void window_menu_refresh(Lister *ls);

static void m_view(void *data, Nv_Menu_Item *it)
{
    Lister *ls = data;
    View v = it->id == 1 ? VIEW_ICONS : VIEW_LIST;
    if (v != ls->view) {
        ls->view = v;
        rebuild_view(ls);
    }
    window_menu_refresh(ls);
}

static void m_sort(void *data, Nv_Menu_Item *it)
{
    Lister *ls = data;
    ls->sort = (Sort)it->id;
    refilter(ls);
    window_menu_refresh(ls);
}

static void window_menu_refresh(Lister *ls)
{
    Nv_Menu *wm = ls->w->window_menu;
    nv_menu_clear(wm);
    nv_menu_add(wm, _("Open _Parent"), "Super+P", m_parent, ls);
    nv_menu_add(wm, _("_Update"), NULL, m_update, ls);
    nv_menu_add(wm, _("Select _Contents"), NULL, m_select_all, ls);
    nv_menu_add_separator(wm);
    Nv_Menu_Item *it;
    it = nv_menu_add_check(wm, _("View as _Icons"), ls->view == VIEW_ICONS, m_view, ls);
    it->kind = NV_ITEM_RADIO;
    it->id = 1;
    it = nv_menu_add_check(wm, _("View as _List"), ls->view == VIEW_LIST, m_view, ls);
    it->kind = NV_ITEM_RADIO;
    it->id = 2;
    nv_menu_add_separator(wm);
    static const char *const sorts[] = {N_("Sort by _Name"), N_("Sort by _Date"), N_("Sort by _Size"),
                                        N_("Sort by _Type")};
    for (int i = 0; i < 4; i++) {
        it = nv_menu_add_check(wm, _(sorts[i]), (int)ls->sort == i, m_sort, ls);
        it->kind = NV_ITEM_RADIO;
        it->id = i;
    }
    nv_menu_add_separator(wm);
    nv_menu_add_check(wm, _("Show _Hidden Files"), ls->show_hidden, m_hidden, ls);
    nv_menu_add_check(wm, _("_Browser Mode"), ls->browser, m_browser, ls);
}

/* ---------------------------------------------------------------- the window */

static void on_parent_clicked(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    open_parent(data);
}

static void on_filter_changed(void *data, Evas_Object *o, void *ev)
{
    (void)ev;
    Lister *ls = data;
    free(ls->pattern);
    char *text = elm_entry_markup_to_utf8(elm_entry_entry_get(o));
    ls->pattern = to_fnmatch(text);
    free(text);
    refilter(ls);
}

static void on_location(void *data, Evas_Object *o, void *ev)
{
    (void)ev;
    Lister *ls = data;
    char *text = elm_entry_markup_to_utf8(elm_entry_entry_get(o));
    char posix[PATH_MAX];
    struct stat st;
    if (text && nirvana_to_posix(text, posix, sizeof posix) == 0 && stat(posix, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            go(ls, posix, "", false);
        else if (is_archive(posix))
            go(ls, posix, "", true);
        else
            nv_open_path(posix);
    } else {
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof msg, _("There is no drawer \"%s\"."), text ? text : "");
        nv_message(_("Open"), msg);
    }
    free(text);
}

static void lister_close(void *data, Nv_Window *w)
{
    Lister *ls = data;
    scan_stop(ls);
    listers = eina_list_remove(listers, ls);
    entries_clear(ls);
    free(ls->pattern);
    evas_object_del(w->win);
    free(ls);
}

static Lister *lister_new(const char *path, const char *inner, bool archive)
{
    if (!gitc) {
        gitc = elm_gengrid_item_class_new();
        gitc->item_style = "default";
        gitc->func.text_get = item_text;
        gitc->func.content_get = item_content;
        litc = elm_genlist_item_class_new();
        litc->item_style = "double_label";
        litc->func.text_get = item_text;
        litc->func.content_get = item_content;
    }
    Lister *ls = calloc(1, sizeof *ls);
    ls->browser = default_browser;
    ls->w = nv_window_new("nirvana-lister", "", 600, 400);
    ls->w->on_close = lister_close;
    ls->w->data = ls;
    Evas_Object *win = ls->w->win;

    ls->box = elm_box_add(win);
    evas_object_size_hint_weight_set(ls->box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    Evas_Object *row = ls->row = elm_box_add(win);
    elm_box_horizontal_set(row, EINA_TRUE);
    elm_box_padding_set(row, 4, 0);
    evas_object_size_hint_weight_set(row, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(row, EVAS_HINT_FILL, 0.5);
    Evas_Object *b = elm_button_add(win);
    elm_object_text_set(b, _("Parent"));
    evas_object_smart_callback_add(b, "clicked", on_parent_clicked, ls);
    elm_box_pack_end(row, b);
    evas_object_show(b);
    ls->location = elm_entry_add(win);
    elm_entry_single_line_set(ls->location, EINA_TRUE);
    elm_entry_scrollable_set(ls->location, EINA_TRUE);
    evas_object_size_hint_weight_set(ls->location, 3.0, 0);
    evas_object_size_hint_align_set(ls->location, EVAS_HINT_FILL, 0.5);
    evas_object_smart_callback_add(ls->location, "activated", on_location, ls);
    elm_box_pack_end(row, ls->location);
    evas_object_show(ls->location);
    ls->filter = elm_entry_add(win);
    elm_entry_single_line_set(ls->filter, EINA_TRUE);
    elm_entry_scrollable_set(ls->filter, EINA_TRUE);
    elm_object_part_text_set(ls->filter, "guide", _("Filter (#?.txt)"));
    evas_object_size_hint_weight_set(ls->filter, 1.0, 0);
    evas_object_size_hint_align_set(ls->filter, EVAS_HINT_FILL, 0.5);
    evas_object_smart_callback_add(ls->filter, "changed,user", on_filter_changed, ls);
    elm_box_pack_end(row, ls->filter);
    evas_object_show(ls->filter);
    elm_box_pack_end(ls->box, row);
    evas_object_show(row);

    ls->status = elm_label_add(win);
    evas_object_size_hint_align_set(ls->status, 0.0, 0.5);
    elm_box_pack_end(ls->box, ls->status);
    evas_object_show(ls->status);

    ls->view = VIEW_ICONS;
    rebuild_view(ls);

    nv_window_content_set(ls->w, ls->box);
    evas_object_show(ls->box);
    ls->w->window_menu = nv_menu_new("lister");
    window_menu_refresh(ls);

    listers = eina_list_append(listers, ls);
    nv_window_show(ls->w);
    set_place(ls, path, inner, archive);
    return ls;
}

void nv_lister_open(const char *path)
{
    char posix[PATH_MAX], real[PATH_MAX];
    if (nirvana_to_posix(path, posix, sizeof posix) < 0) {
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof msg, _("There is no volume \"%s\"."), path);
        nv_message(_("Open"), msg);
        return;
    }
    if (realpath(posix, real))
        snprintf(posix, sizeof posix, "%s", real);
    struct stat st;
    if (stat(posix, &st) < 0) {
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof msg, _("%s: %s"), path, strerror(errno));
        nv_message(_("Open"), msg);
        return;
    }
    if (S_ISDIR(st.st_mode))
        go(NULL, posix, "", false);
    else if (is_archive(posix))
        go(NULL, posix, "", true);
    else
        nv_open_path(posix);
}
