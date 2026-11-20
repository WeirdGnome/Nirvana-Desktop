/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * libnirvana: ISO 9660 payloads (the older, "type 1" AppImages) read in memory through
 * libarchive, its Rock Ridge links followed.
 */
#define _GNU_SOURCE
#include "container.h"

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct iso {
    struct nv_fs fs;
    const unsigned char *base;
    size_t size;
} iso;

static struct archive *iso_begin(const iso *s)
{
    struct archive *a = archive_read_new();
    if (!a)
        return NULL;
    archive_read_support_format_iso9660(a);
    if (archive_read_open_memory(a, s->base, s->size) != ARCHIVE_OK) {
        archive_read_free(a);
        return NULL;
    }
    return a;
}

/* an entry's name without "./" or "/" in front and without a slash behind */
static void entry_name(struct archive_entry *e, char *out, size_t len)
{
    const char *p = archive_entry_pathname(e);
    if (!p)
        p = "";
    while (*p == '/' || (p[0] == '.' && p[1] == '/'))
        p += *p == '/' ? 1 : 2;
    snprintf(out, len, "%s", p);
    size_t n = strlen(out);
    while (n && out[n - 1] == '/')
        out[--n] = 0;
}

static int iso_read(nv_fs *fs, const char *path, unsigned char **data, size_t *len, char *real, size_t rlen)
{
    iso *s = (iso *)fs;
    char cur[4096];
    if (nv_path_join("", path, cur, sizeof cur) < 0)
        return -ENOENT;
    for (int hops = 0; hops <= NV_MAX_LINKS; hops++) {
        struct archive *a = iso_begin(s);
        if (!a)
            return -EIO;
        struct archive_entry *e;
        bool again = false;
        int r = -ENOENT;
        char name[4096];
        while (archive_read_next_header(a, &e) == ARCHIVE_OK) {
            entry_name(e, name, sizeof name);
            size_t n = strlen(name);
            bool exact = !strcmp(name, cur);
            bool prefix = n && !strncmp(cur, name, n) && cur[n] == '/';
            if (!exact && !prefix)
                continue;
            const char *link = archive_entry_symlink(e);
            if (link) {
                /* a link: its target (from its directory) and the rest of the path */
                char dir[4096], target[4096], joined[8192];
                snprintf(dir, sizeof dir, "%s", name);
                char *slash = strrchr(dir, '/');
                if (slash)
                    *slash = 0;
                else
                    *dir = 0;
                int k = link[0] == '/' ? nv_path_join("", link + 1, target, sizeof target)
                                       : nv_path_join(dir, link, target, sizeof target);
                if (k < 0)
                    break;
                snprintf(joined, sizeof joined, "%s%s", target, exact ? "" : cur + n);
                if (nv_path_join("", joined, cur, sizeof cur) < 0)
                    break;
                again = true;
                break;
            }
            if (!exact)
                continue; /* a directory on the way */
            if (archive_entry_filetype(e) != AE_IFREG) {
                r = archive_entry_filetype(e) == AE_IFDIR ? -EISDIR : -EINVAL;
                break;
            }
            int64_t size = archive_entry_size(e);
            if (size < 0 || (uint64_t)size > NV_MAX_FILE) {
                r = -EFBIG;
                break;
            }
            unsigned char *buf = malloc((size_t)size + 1);
            if (!buf) {
                r = -ENOMEM;
                break;
            }
            la_ssize_t got = archive_read_data(a, buf, (size_t)size);
            if (got != (la_ssize_t)size) {
                free(buf);
                r = -EIO;
                break;
            }
            buf[size] = 0;
            if (real && rlen)
                snprintf(real, rlen, "%s", name);
            *data = buf;
            *len = (size_t)size;
            r = 0;
            break;
        }
        archive_read_free(a);
        if (!again)
            return r;
    }
    return -ELOOP;
}

static int iso_list_root(nv_fs *fs, char ***names, size_t *count)
{
    iso *s = (iso *)fs;
    struct archive *a = iso_begin(s);
    if (!a)
        return -EIO;
    char **list = NULL;
    size_t n = 0;
    int r = 0;
    struct archive_entry *e;
    char name[4096];
    while (archive_read_next_header(a, &e) == ARCHIVE_OK) {
        entry_name(e, name, sizeof name);
        if (!*name || strchr(name, '/'))
            continue;
        if (archive_entry_filetype(e) != AE_IFREG && !archive_entry_symlink(e))
            continue;
        char **l = realloc(list, (n + 1) * sizeof *l);
        if (!l || !(l[n] = strdup(name))) {
            if (l)
                list = l;
            r = -ENOMEM;
            break;
        }
        list = l;
        n++;
    }
    archive_read_free(a);
    if (r < 0) {
        nv_names_free(list, n);
        return r;
    }
    *names = list;
    *count = n;
    return 0;
}

static void iso_close(nv_fs *fs)
{
    free(fs);
}

static const struct nv_fs_ops iso_ops = {iso_read, iso_list_root, iso_close};

int nv_iso9660_open(const unsigned char *base, size_t size, nv_fs **out)
{
    /* the primary volume descriptor: "CD001" at 32 KiB + 1 */
    if (size < 0x8006 || memcmp(base + 0x8001, "CD001", 5))
        return -EINVAL;
    iso *s = calloc(1, sizeof *s);
    if (!s)
        return -ENOMEM;
    s->fs.ops = &iso_ops;
    s->base = base;
    s->size = size;
    *out = &s->fs;
    return 0;
}
