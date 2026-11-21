/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: the Information window, laid out as Ambient's - "Info: NAME (Volume)",
 * "(Drawer)", "(Tool)" (a program) or "(Project)" (another file) - its icon at the left,
 * then what there is to know of it:
 *   a volume:  Disk, Size, Used, Free, Filesystem, Type, Blocksize, Status, Features;
 *   a drawer:  Files, Dirs, Softlinks and the Total Size, counted in a thread;
 *   a file:    Type, Blocks, Size, Size on disk, Version (an AppImage's), MD5 (asked);
 * and for each: Modified, Comment (the "user.xdg.comment" attribute other desktops read),
 * the protection - Readable, Writable, Executable - and the Default Tool of a project.
 * Save writes the comment and the protection; Cancel leaves them.
 */
#include "desktop.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

#include "nirvana_appimage.h"

#define COMMENT_ATTR "user.xdg.comment"

typedef struct Info {
    Nv_Window *w;
    char path[PATH_MAX];
    bool volume, drawer;
    mode_t mode;
    Evas_Object *table, *comment, *chk_r, *chk_w, *chk_x;
    int row;
    /* a drawer's counting, a file's MD5 */
    Ecore_Thread *th;
    Evas_Object *lab_files, *lab_dirs, *lab_links, *lab_total, *lab_md5, *bt_md5;
    unsigned long long files, dirs, links, bytes;
    char md5[33];
} Info;

static void size_text(unsigned long long n, char *out, size_t len)
{
    if (n >= 1000ULL * 1000 * 1000)
        snprintf(out, len, "%.1f GB (%llu %s)", (double)n / 1e9, n, _("bytes"));
    else if (n >= 1000ULL * 1000)
        snprintf(out, len, "%.1f MB (%llu %s)", (double)n / 1e6, n, _("bytes"));
    else
        snprintf(out, len, "%llu %s", n, _("bytes"));
}

/* ---------------------------------------------------------------- the rows */

static Evas_Object *label(Info *in, const char *text, bool bold)
{
    Evas_Object *l = elm_label_add(in->w->win);
    char *m = elm_entry_utf8_to_markup(text ? text : "");
    char buf[4096];
    snprintf(buf, sizeof buf, bold ? "<b>%s</b>" : "%s", m ? m : "");
    free(m);
    elm_object_text_set(l, buf);
    evas_object_show(l);
    return l;
}

/* a "Name: value" row; the value's label returned (to be changed later) */
static Evas_Object *row(Info *in, const char *name, const char *value)
{
    Evas_Object *n = label(in, name, true);
    evas_object_size_hint_align_set(n, 1.0, 0.5);
    elm_table_pack(in->table, n, 0, in->row, 1, 1);
    Evas_Object *v = label(in, value, false);
    evas_object_size_hint_weight_set(v, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(v, 0.0, 0.5);
    elm_table_pack(in->table, v, 1, in->row, 1, 1);
    in->row++;
    return v;
}

static void set_text(Evas_Object *l, const char *text)
{
    char *m = elm_entry_utf8_to_markup(text);
    elm_object_text_set(l, m);
    free(m);
}

/* ---------------------------------------------------------------- a volume */

/* the mount holding path: its source, file system type and options (/proc/self/mounts) */
static bool mount_of(const char *path, char *src, size_t slen, char *type, size_t tlen, bool *ro)
{
    FILE *f = fopen("/proc/self/mounts", "r");
    if (!f)
        return false;
    char line[4096], best[PATH_MAX] = "";
    size_t bestlen = 0;
    bool found = false;
    while (fgets(line, sizeof line, f)) {
        char s[1024], m[1024], t[64], o[1024];
        if (sscanf(line, "%1023s %1023s %63s %1023s", s, m, t, o) != 4)
            continue;
        size_t n = strlen(m);
        bool under = !strcmp(m, "/") || (!strncmp(path, m, n) && (path[n] == '/' || path[n] == 0));
        if (under && n >= bestlen) {
            bestlen = n;
            snprintf(best, sizeof best, "%s", m);
            snprintf(src, slen, "%s", s);
            snprintf(type, tlen, "%s", t);
            *ro = !strncmp(o, "ro", 2) && (o[2] == ',' || o[2] == 0);
            found = true;
        }
    }
    fclose(f);
    return found;
}

static void volume_rows(Info *in, const char *name)
{
    char buf[256];
    row(in, _("Disk:"), name);
    struct statvfs sv;
    if (statvfs(in->path, &sv) == 0) {
        unsigned long long total = (unsigned long long)sv.f_blocks * sv.f_frsize;
        unsigned long long used = total - (unsigned long long)sv.f_bfree * sv.f_frsize;
        unsigned long long avail = (unsigned long long)sv.f_bavail * sv.f_frsize;
        size_text(total, buf, sizeof buf);
        row(in, _("Size:"), buf);
        char u[200];
        size_text(used, u, sizeof u);
        snprintf(buf, sizeof buf, "%s, %d%%", u, total ? (int)(used * 100 / total) : 0);
        row(in, _("Used:"), buf);
        size_text(avail, buf, sizeof buf);
        row(in, _("Free:"), buf);
        snprintf(buf, sizeof buf, "%lu %s", (unsigned long)sv.f_bsize, _("bytes"));
        char src[1024] = "?", type[64] = "?";
        bool ro = false;
        mount_of(in->path, src, sizeof src, type, sizeof type, &ro);
        row(in, _("Filesystem:"), type);
        row(in, _("Type:"), src);
        row(in, _("Blocksize:"), buf);
        row(in, _("Status:"), ro || (sv.f_flag & ST_RDONLY) ? _("write protected") : _("validated"));
        bool fat = !strcmp(type, "vfat") || !strcmp(type, "exfat") || !strcmp(type, "msdos");
        snprintf(buf, sizeof buf, "%s%s%s", !strcmp(type, "vfat") ? "" : _("large files"),
                 fat ? "" : !strcmp(type, "vfat") ? "" : ", ", fat ? "" : _("case sensitive"));
        row(in, _("Features:"), *buf ? buf : _("none"));
    } else {
        row(in, _("Status:"), _("No media"));
    }
}

/* ---------------------------------------------------------------- a drawer (counted) */

static void count(Info *in, const char *path, Ecore_Thread *th, int depth)
{
    Eina_Iterator *it = eina_file_direct_ls(path);
    if (!it)
        return;
    const Eina_File_Direct_Info *fi;
    EINA_ITERATOR_FOREACH(it, fi) {
        if (ecore_thread_check(th))
            break;
        struct stat st;
        if (lstat(fi->path, &st) != 0)
            continue;
        if (S_ISLNK(st.st_mode)) {
            in->links++;
        } else if (S_ISDIR(st.st_mode)) {
            in->dirs++;
            if (depth < 64)
                count(in, fi->path, th, depth + 1);
        } else {
            in->files++;
            in->bytes += (unsigned long long)st.st_size;
        }
        if ((in->files + in->dirs) % 500 == 0)
            ecore_thread_feedback(th, NULL);
    }
    eina_iterator_free(it);
}

static void count_run(void *data, Ecore_Thread *th)
{
    Info *in = data;
    count(in, in->path, th, 0);
}

static void count_show(void *data, Ecore_Thread *th, void *msg)
{
    (void)th;
    (void)msg;
    Info *in = data;
    if (!in->w)
        return;
    char buf[200];
    snprintf(buf, sizeof buf, "%llu", in->files);
    set_text(in->lab_files, buf);
    snprintf(buf, sizeof buf, "%llu", in->dirs);
    set_text(in->lab_dirs, buf);
    snprintf(buf, sizeof buf, "%llu", in->links);
    set_text(in->lab_links, buf);
    size_text(in->bytes, buf, sizeof buf);
    set_text(in->lab_total, buf);
}

static void info_free(Info *in)
{
    free(in);
}

static void thread_end(void *data, Ecore_Thread *th)
{
    Info *in = data;
    in->th = NULL;
    if (!in->w) {
        info_free(in);
        return;
    }
    count_show(in, th, NULL);
}

/* ---------------------------------------------------------------- a file's MD5 */

static bool md5_cancelled(void *data)
{
    return ecore_thread_check(data);
}

static void md5_run(void *data, Ecore_Thread *th)
{
    Info *in = data;
    if (!nv_md5_file(in->path, in->md5, md5_cancelled, th))
        in->md5[0] = 0;
}

static void md5_end(void *data, Ecore_Thread *th)
{
    (void)th;
    Info *in = data;
    in->th = NULL;
    if (!in->w) {
        info_free(in);
        return;
    }
    set_text(in->lab_md5, in->md5[0] ? in->md5 : _("unknown"));
}

static void on_md5(void *data, Evas_Object *o, void *ev)
{
    (void)ev;
    Info *in = data;
    if (in->th)
        return;
    elm_object_disabled_set(o, EINA_TRUE);
    set_text(in->lab_md5, _("Computing..."));
    in->th = ecore_thread_run(md5_run, md5_end, md5_end, in);
}

/* ---------------------------------------------------------------- save, cancel */

static void on_save(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    Info *in = data;
    char msg[PATH_MAX + 256];
    if (!in->volume) {
        mode_t m = in->mode & 07777;
        m = elm_check_state_get(in->chk_r) ? m | S_IRUSR : m & ~(mode_t)S_IRUSR;
        m = elm_check_state_get(in->chk_w) ? m | S_IWUSR : m & ~(mode_t)S_IWUSR;
        m = elm_check_state_get(in->chk_x) ? m | S_IXUSR : m & ~(mode_t)S_IXUSR;
        if (m != (in->mode & 07777) && chmod(in->path, m) != 0) {
            snprintf(msg, sizeof msg, _("Couldn't set the protection mode of '%s'.\n\n%s"), in->path, strerror(errno));
            nv_message(_("Information"), msg);
        }
        char *text = elm_entry_markup_to_utf8(elm_entry_entry_get(in->comment));
        char old[1024] = "";
        ssize_t n = getxattr(in->path, COMMENT_ATTR, old, sizeof old - 1);
        old[n > 0 ? n : 0] = 0;
        if (strcmp(text ? text : "", old)) {
            int r = text && *text ? setxattr(in->path, COMMENT_ATTR, text, strlen(text), 0)
                                  : removexattr(in->path, COMMENT_ATTR);
            if (r != 0 && errno != ENODATA) {
                snprintf(msg, sizeof msg, _("Couldn't save the comment of '%s'.\n\n%s"), in->path, strerror(errno));
                nv_message(_("Information"), msg);
            }
        }
        free(text);
    }
    nv_window_close(in->w);
}

static void on_cancel(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    Info *in = data;
    nv_window_close(in->w);
}

static void on_close(void *data, Nv_Window *w)
{
    Info *in = data;
    in->w = NULL;
    evas_object_del(w->win);
    if (in->th)
        ecore_thread_cancel(in->th); /* (freed when it ends) */
    else
        info_free(in);
}

static Evas_Object *check(Info *in, const char *text, bool on)
{
    Evas_Object *c = elm_check_add(in->w->win);
    elm_object_text_set(c, text);
    elm_check_state_set(c, on);
    evas_object_show(c);
    return c;
}

/* ---------------------------------------------------------------- the window */

static void info_open(const char *path, const char *volume_name)
{
    struct stat st;
    if (stat(path, &st) != 0 && !volume_name) {
        char msg[PATH_MAX + 128];
        snprintf(msg, sizeof msg, "%s\n\n%s", path, strerror(errno));
        nv_message(_("Information"), msg);
        return;
    }
    Info *in = calloc(1, sizeof *in);
    snprintf(in->path, sizeof in->path, "%s", path);
    in->volume = volume_name != NULL;
    in->drawer = !in->volume && S_ISDIR(st.st_mode);
    in->mode = st.st_mode;
    bool tool = !in->volume && !in->drawer && (st.st_mode & S_IXUSR);
    const char *base = strrchr(path, '/');
    base = volume_name ? volume_name : base && base[1] ? base + 1 : path;
    char title[PATH_MAX + 64];
    snprintf(title, sizeof title,
             in->volume ? _("Info: %s (Volume)") : in->drawer ? _("Info: %s (Drawer)")
             : tool     ? _("Info: %s (Tool)")
                        : _("Info: %s (Project)"),
             base);
    in->w = nv_window_new("nirvana-info", title, 520, 340);
    in->w->on_close = on_close;
    in->w->data = in;
    Evas_Object *win = in->w->win;

    Evas_Object *box = elm_box_add(win);
    elm_box_padding_set(box, 0, 8);
    evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
    elm_box_align_set(box, 0.5, 0.0);

    /* the icon at the left, the facts at the right */
    Evas_Object *top = elm_box_add(win);
    elm_box_horizontal_set(top, EINA_TRUE);
    elm_box_padding_set(top, 16, 0);
    evas_object_size_hint_weight_set(top, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(top, EVAS_HINT_FILL, 0.0);
    Evas_Object *icon = NULL;
    if (in->volume)
        icon = nv_edje_add(win, "nirvana/lister/icon/drawer");
    if (!icon || in->volume) {
        if (icon)
            evas_object_del(icon);
        icon = nv_icon_for_path(win, path, in->drawer || in->volume, 64);
    }
    evas_object_size_hint_min_set(icon, 64, 64);
    evas_object_size_hint_max_set(icon, 64, 64);
    evas_object_size_hint_align_set(icon, 0.5, 0.0);
    elm_box_pack_end(top, icon);
    evas_object_show(icon);
    in->table = elm_table_add(win);
    elm_table_padding_set(in->table, 10, 2);
    evas_object_size_hint_weight_set(in->table, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(in->table, EVAS_HINT_FILL, 0.0);
    elm_box_pack_end(top, in->table);
    evas_object_show(in->table);
    elm_box_pack_end(box, top);
    evas_object_show(top);

    char buf[PATH_MAX + 64];
    if (in->volume) {
        volume_rows(in, volume_name);
    } else if (in->drawer) {
        in->lab_files = row(in, _("Files:"), "…");
        in->lab_dirs = row(in, _("Dirs:"), "…");
        in->lab_links = row(in, _("Softlinks:"), "…");
        in->lab_total = row(in, _("Total Size:"), _("Searching..."));
        in->th = ecore_thread_feedback_run(count_run, count_show, thread_end, thread_end, in, EINA_FALSE);
    } else {
        const char *mime = efreet_mime_type_get(path);
        snprintf(buf, sizeof buf, "%s (%s)", tool ? _("Tool") : _("Project"), mime ? mime : "?");
        row(in, _("Type:"), buf);
        snprintf(buf, sizeof buf, "%llu", (unsigned long long)st.st_blocks);
        row(in, _("Blocks:"), buf);
        size_text((unsigned long long)st.st_size, buf, sizeof buf);
        row(in, _("Size:"), buf);
        size_text((unsigned long long)st.st_blocks * 512ULL, buf, sizeof buf);
        row(in, _("Size on disk:"), buf);
        if (nv_is_appimage(path)) {
            nirvana_appimage *img;
            nirvana_appimage_meta m;
            if (nirvana_appimage_open(path, &img) == 0) {
                if (nirvana_appimage_metadata(img, &m) == 0) {
                    snprintf(buf, sizeof buf, "%s %s", m.name ? m.name : "", m.version ? m.version : "");
                    row(in, _("Version:"), buf);
                    nirvana_appimage_meta_free(&m);
                }
                nirvana_appimage_close(img);
            }
        }
        /* MD5: computed when asked (a big file takes long) */
        Evas_Object *n = label(in, _("MD5:"), true);
        evas_object_size_hint_align_set(n, 1.0, 0.5);
        elm_table_pack(in->table, n, 0, in->row, 1, 1);
        Evas_Object *rowbox = elm_box_add(win);
        elm_box_horizontal_set(rowbox, EINA_TRUE);
        elm_box_padding_set(rowbox, 6, 0);
        evas_object_size_hint_align_set(rowbox, 0.0, 0.5);
        in->bt_md5 = elm_button_add(win);
        elm_object_text_set(in->bt_md5, _("Compute"));
        evas_object_smart_callback_add(in->bt_md5, "clicked", on_md5, in);
        elm_box_pack_end(rowbox, in->bt_md5);
        evas_object_show(in->bt_md5);
        in->lab_md5 = label(in, "", false);
        elm_box_pack_end(rowbox, in->lab_md5);
        elm_table_pack(in->table, rowbox, 1, in->row++, 1, 1);
        evas_object_show(rowbox);
    }
    if (!in->volume) {
        struct tm tm;
        localtime_r(&st.st_mtime, &tm);
        strftime(buf, sizeof buf, "%A %d-%b-%Y %H:%M:%S", &tm);
        row(in, _("Modified:"), buf);
    }

    if (!in->volume) {
        /* the comment */
        Evas_Object *n = label(in, _("Comment:"), true);
        evas_object_size_hint_align_set(n, 1.0, 0.5);
        elm_table_pack(in->table, n, 0, in->row, 1, 1);
        in->comment = elm_entry_add(win);
        elm_entry_single_line_set(in->comment, EINA_TRUE);
        elm_entry_scrollable_set(in->comment, EINA_TRUE);
        char old[1024] = "";
        ssize_t got = getxattr(path, COMMENT_ATTR, old, sizeof old - 1);
        old[got > 0 ? got : 0] = 0;
        char *m = elm_entry_utf8_to_markup(old);
        elm_entry_entry_set(in->comment, m);
        free(m);
        evas_object_size_hint_weight_set(in->comment, EVAS_HINT_EXPAND, 0);
        evas_object_size_hint_align_set(in->comment, EVAS_HINT_FILL, 0.5);
        elm_table_pack(in->table, in->comment, 1, in->row++, 1, 1);
        evas_object_show(in->comment);

        /* the protection */
        Evas_Object *prot = elm_box_add(win);
        elm_box_horizontal_set(prot, EINA_TRUE);
        elm_box_padding_set(prot, 14, 0);
        evas_object_size_hint_align_set(prot, 0.5, 0.5);
        in->chk_r = check(in, _("Readable"), st.st_mode & S_IRUSR);
        in->chk_w = check(in, _("Writable"), st.st_mode & S_IWUSR);
        in->chk_x = check(in, _("Executable"), st.st_mode & S_IXUSR);
        elm_box_pack_end(prot, in->chk_r);
        elm_box_pack_end(prot, in->chk_w);
        elm_box_pack_end(prot, in->chk_x);
        elm_box_pack_end(box, prot);
        evas_object_show(prot);

        /* a project's default tool: the program its type opens with */
        if (!in->drawer && !tool) {
            const char *mime = efreet_mime_type_get(path);
            Eina_List *apps = mime ? efreet_util_desktop_mime_list(mime) : NULL;
            Efreet_Desktop *d = eina_list_data_get(apps);
            snprintf(buf, sizeof buf, "%s", d && d->name ? d->name : _("none"));
            Efreet_Desktop *e;
            EINA_LIST_FREE(apps, e)
                efreet_desktop_free(e);
            Evas_Object *dt = elm_box_add(win);
            elm_box_horizontal_set(dt, EINA_TRUE);
            elm_box_padding_set(dt, 8, 0);
            elm_box_pack_end(dt, label(in, _("Default Tool:"), true));
            elm_box_pack_end(dt, label(in, buf, false));
            elm_box_pack_end(box, dt);
            evas_object_show(dt);
        }
    }

    /* Save, Cancel */
    Evas_Object *btns = elm_box_add(win);
    elm_box_horizontal_set(btns, EINA_TRUE);
    elm_box_homogeneous_set(btns, EINA_TRUE);
    evas_object_size_hint_weight_set(btns, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(btns, EVAS_HINT_FILL, 1.0);
    Evas_Object *save = elm_button_add(win);
    elm_object_text_set(save, _("Save"));
    evas_object_size_hint_align_set(save, 0.0, 0.5);
    evas_object_size_hint_weight_set(save, EVAS_HINT_EXPAND, 0);
    evas_object_smart_callback_add(save, "clicked", on_save, in);
    elm_object_disabled_set(save, in->volume);
    Evas_Object *cancel = elm_button_add(win);
    elm_object_text_set(cancel, _("Cancel"));
    evas_object_size_hint_align_set(cancel, 1.0, 0.5);
    evas_object_size_hint_weight_set(cancel, EVAS_HINT_EXPAND, 0);
    evas_object_smart_callback_add(cancel, "clicked", on_cancel, in);
    elm_box_pack_end(btns, save);
    elm_box_pack_end(btns, cancel);
    evas_object_show(save);
    evas_object_show(cancel);
    elm_box_pack_end(box, btns);
    evas_object_show(btns);

    nv_window_content_set(in->w, box);
    evas_object_show(box);
    nv_window_show(in->w);
}

void nv_info_show(const char *path)
{
    info_open(path, NULL);
}

void nv_info_show_volume(const char *name, const char *posix)
{
    info_open(posix && *posix ? posix : "/nonexistent", name);
}
