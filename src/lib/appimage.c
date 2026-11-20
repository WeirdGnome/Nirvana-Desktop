/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * libnirvana: AppImages read without running them (see include/nirvana_appimage.h).
 */
#define _GNU_SOURCE
#include "nirvana_appimage.h"
#include "container.h"

#include <ctype.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct nirvana_appimage {
    unsigned char *map;
    size_t size;
    nirvana_appimage_kind kind;
    uint64_t offset;
    nv_fs *fs;
};

/* ---------------------------------------------------------------- shared helpers */

void nv_names_free(char **names, size_t count)
{
    for (size_t i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

int nv_path_join(const char *dir, const char *rel, char *out, size_t len)
{
    /* the components kept as offsets into out */
    size_t marks[512], depth = 0, n = 0;
    if (!len)
        return -ENAMETOOLONG;
    out[0] = 0;
    const char *parts[2] = {dir ? dir : "", rel ? rel : ""};
    for (int k = 0; k < 2; k++) {
        const char *p = parts[k];
        while (*p) {
            while (*p == '/')
                p++;
            if (!*p)
                break;
            const char *end = strchr(p, '/');
            size_t cl = end ? (size_t)(end - p) : strlen(p);
            if (cl == 1 && p[0] == '.') {
                /* nothing */
            } else if (cl == 2 && p[0] == '.' && p[1] == '.') {
                if (depth == 0)
                    return -EINVAL;
                n = marks[--depth];
                out[n] = 0;
            } else {
                if (depth == sizeof marks / sizeof *marks)
                    return -ENAMETOOLONG;
                size_t need = n + (n ? 1 : 0) + cl + 1;
                if (need > len)
                    return -ENAMETOOLONG;
                marks[depth++] = n;
                if (n)
                    out[n++] = '/';
                memcpy(out + n, p, cl);
                n += cl;
                out[n] = 0;
            }
            p += cl;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- ELF */

static uint16_t rd16(const unsigned char *p, bool be)
{
    return be ? (uint16_t)(p[0] << 8 | p[1]) : (uint16_t)(p[1] << 8 | p[0]);
}

static uint32_t rd32(const unsigned char *p, bool be)
{
    return be ? (uint32_t)rd16(p, true) << 16 | rd16(p + 2, true) : (uint32_t)rd16(p + 2, false) << 16 | rd16(p, false);
}

static uint64_t rd64(const unsigned char *p, bool be)
{
    return be ? (uint64_t)rd32(p, true) << 32 | rd32(p + 4, true) : (uint64_t)rd32(p + 4, false) << 32 | rd32(p, false);
}

uint64_t nirvana_elf_payload_offset(const void *data, size_t size)
{
    const unsigned char *p = data;
    if (size < EI_NIDENT || memcmp(p, ELFMAG, SELFMAG))
        return 0;
    bool be = p[EI_DATA] == ELFDATA2MSB;
    if (p[EI_DATA] != ELFDATA2LSB && !be)
        return 0;
    uint64_t shoff, entsize, num;
    if (p[EI_CLASS] == ELFCLASS64) {
        if (size < sizeof(Elf64_Ehdr))
            return 0;
        shoff = rd64(p + offsetof(Elf64_Ehdr, e_shoff), be);
        entsize = rd16(p + offsetof(Elf64_Ehdr, e_shentsize), be);
        num = rd16(p + offsetof(Elf64_Ehdr, e_shnum), be);
    } else if (p[EI_CLASS] == ELFCLASS32) {
        if (size < sizeof(Elf32_Ehdr))
            return 0;
        shoff = rd32(p + offsetof(Elf32_Ehdr, e_shoff), be);
        entsize = rd16(p + offsetof(Elf32_Ehdr, e_shentsize), be);
        num = rd16(p + offsetof(Elf32_Ehdr, e_shnum), be);
    } else {
        return 0;
    }
    uint64_t end = shoff + entsize * num;
    return end < shoff || end > size ? 0 : end;
}

/* ---------------------------------------------------------------- the payload */

static bool magic_at(const nirvana_appimage *img, uint64_t off, nirvana_appimage_kind *kind)
{
    const unsigned char *p = img->map + off;
    size_t left = img->size - off;
    if (off >= img->size)
        return false;
    if (left >= 4 && !memcmp(p, "hsqs", 4)) {
        *kind = NIRVANA_APPIMAGE_SQUASHFS;
        return true;
    }
    if (left >= 6 && !memcmp(p, "DWARFS", 6)) {
        *kind = NIRVANA_APPIMAGE_DWARFS;
        return true;
    }
    if (left >= 0x8006 && !memcmp(p + 0x8001, "CD001", 5)) {
        *kind = NIRVANA_APPIMAGE_ISO9660;
        return true;
    }
    return false;
}

/* the payload found: at the end of the ELF part, else (an odd runtime) looked for in the
 * first megabytes; a type-1 image is an ISO with the ELF in its system area */
static int find_payload(nirvana_appimage *img, const char *file)
{
    uint64_t off = nirvana_elf_payload_offset(img->map, img->size);
    nirvana_appimage_kind kind;
    if (off && magic_at(img, off, &kind))
        goto found;
    if (magic_at(img, 0, &kind) && kind == NIRVANA_APPIMAGE_ISO9660) {
        off = 0;
        goto found;
    }
    size_t limit = img->size < (16u << 20) ? img->size : (16u << 20);
    for (off = 4; off + 6 <= limit; off++) {
        unsigned char c = img->map[off];
        if ((c == 'h' || c == 'D') && magic_at(img, off, &kind) && kind != NIRVANA_APPIMAGE_ISO9660)
            goto found;
    }
    return -EINVAL;
found:
    img->offset = off;
    img->kind = kind;
    const unsigned char *base = img->map + off;
    size_t size = img->size - off;
    switch (kind) {
    case NIRVANA_APPIMAGE_SQUASHFS:
        return nv_squashfs_open(base, size, &img->fs);
    case NIRVANA_APPIMAGE_ISO9660:
        return nv_iso9660_open(base, size, &img->fs);
    case NIRVANA_APPIMAGE_DWARFS:
        return nv_dwarfs_open(file, off, &img->fs);
    default:
        return -EINVAL;
    }
}

int nirvana_appimage_open(const char *file, nirvana_appimage **out)
{
    if (!file || !out)
        return -EINVAL;
    int fd = open(file, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    struct stat st;
    if (fstat(fd, &st) < 0) {
        int e = -errno;
        close(fd);
        return e;
    }
    if (!S_ISREG(st.st_mode) || st.st_size < 64) {
        close(fd);
        return -EINVAL;
    }
    nirvana_appimage *img = calloc(1, sizeof *img);
    if (!img) {
        close(fd);
        return -ENOMEM;
    }
    img->size = (size_t)st.st_size;
    img->map = mmap(NULL, img->size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (img->map == MAP_FAILED) {
        int e = -errno;
        free(img);
        return e;
    }
    madvise(img->map, img->size, MADV_RANDOM);
    int r = find_payload(img, file);
    if (r < 0) {
        nirvana_appimage_close(img);
        return r;
    }
    *out = img;
    return 0;
}

void nirvana_appimage_close(nirvana_appimage *img)
{
    if (!img)
        return;
    if (img->fs)
        img->fs->ops->close(img->fs);
    if (img->map && img->map != MAP_FAILED)
        munmap(img->map, img->size);
    free(img);
}

nirvana_appimage_kind nirvana_appimage_get_kind(const nirvana_appimage *img)
{
    return img ? img->kind : NIRVANA_APPIMAGE_UNKNOWN;
}

uint64_t nirvana_appimage_get_offset(const nirvana_appimage *img)
{
    return img ? img->offset : 0;
}

const char *nirvana_appimage_kind_name(nirvana_appimage_kind kind)
{
    switch (kind) {
    case NIRVANA_APPIMAGE_SQUASHFS: return "SquashFS";
    case NIRVANA_APPIMAGE_ISO9660: return "ISO 9660";
    case NIRVANA_APPIMAGE_DWARFS: return "DwarFS";
    default: return "unknown";
    }
}

int nirvana_appimage_read(nirvana_appimage *img, const char *path, unsigned char **data, size_t *len)
{
    if (!img || !img->fs || !path || !data || !len)
        return -EINVAL;
    return img->fs->ops->read(img->fs, path, data, len, NULL, 0);
}

/* ---------------------------------------------------------------- the .desktop file */

static int find_desktop(nirvana_appimage *img, char *name, size_t len)
{
    char **names;
    size_t count;
    int r = img->fs->ops->list_root(img->fs, &names, &count);
    if (r < 0)
        return r;
    r = -ENOENT;
    for (size_t i = 0; i < count; i++) {
        size_t n = strlen(names[i]);
        if (n > 8 && !strcmp(names[i] + n - 8, ".desktop")) {
            snprintf(name, len, "%s", names[i]);
            r = 0;
            break;
        }
    }
    nv_names_free(names, count);
    return r;
}

int nirvana_appimage_desktop(nirvana_appimage *img, char **text, size_t *len)
{
    if (!img || !img->fs || !text || !len)
        return -EINVAL;
    char name[512];
    int r = find_desktop(img, name, sizeof name);
    if (r < 0)
        return r;
    return img->fs->ops->read(img->fs, name, (unsigned char **)text, len, NULL, 0);
}

static char *trimmed_dup(const char *s, size_t n)
{
    while (n && isspace((unsigned char)*s))
        s++, n--;
    while (n && isspace((unsigned char)s[n - 1]))
        n--;
    return strndup(s, n);
}

int nirvana_appimage_metadata(nirvana_appimage *img, nirvana_appimage_meta *out)
{
    if (!out)
        return -EINVAL;
    memset(out, 0, sizeof *out);
    char name[512];
    int r = img && img->fs ? find_desktop(img, name, sizeof name) : -EINVAL;
    if (r < 0)
        return r;
    char *text;
    size_t len;
    if ((r = img->fs->ops->read(img->fs, name, (unsigned char **)&text, &len, NULL, 0)) < 0)
        return r;
    out->desktop_file = strdup(name);
    static const struct { const char *key; size_t off; } keys[] = {
        {"Name", offsetof(nirvana_appimage_meta, name)},
        {"GenericName", offsetof(nirvana_appimage_meta, generic_name)},
        {"Comment", offsetof(nirvana_appimage_meta, comment)},
        {"Exec", offsetof(nirvana_appimage_meta, exec)},
        {"Icon", offsetof(nirvana_appimage_meta, icon)},
        {"Categories", offsetof(nirvana_appimage_meta, categories)},
        {"X-AppImage-Version", offsetof(nirvana_appimage_meta, version)},
    };
    bool in_entry = false;
    for (char *line = text; line && *line;) {
        char *nl = strchr(line, '\n');
        size_t n = nl ? (size_t)(nl - line) : strlen(line);
        if (n && line[n - 1] == '\r')
            n--;
        if (n && line[0] == '[') {
            in_entry = n == 15 && !strncmp(line, "[Desktop Entry]", 15);
        } else if (in_entry && n && line[0] != '#') {
            char *eq = memchr(line, '=', n);
            if (eq) {
                size_t kl = (size_t)(eq - line);
                while (kl && isspace((unsigned char)line[kl - 1]))
                    kl--;
                for (size_t k = 0; k < sizeof keys / sizeof *keys; k++) {
                    char **field = (char **)((char *)out + keys[k].off);
                    if (!*field && kl == strlen(keys[k].key) && !strncmp(line, keys[k].key, kl))
                        *field = trimmed_dup(eq + 1, n - (size_t)(eq - line) - 1);
                }
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    free(text);
    return out->name || out->exec ? 0 : -ENOENT;
}

void nirvana_appimage_meta_free(nirvana_appimage_meta *m)
{
    if (!m)
        return;
    free(m->name);
    free(m->generic_name);
    free(m->comment);
    free(m->exec);
    free(m->icon);
    free(m->categories);
    free(m->version);
    free(m->desktop_file);
    memset(m, 0, sizeof *m);
}

/* ---------------------------------------------------------------- the icon */

const char *nirvana_image_format(const unsigned char *d, size_t len)
{
    if (len >= 8 && !memcmp(d, "\x89PNG\r\n\x1a\n", 8))
        return "png";
    if (len >= 2 && d[0] == 0x1f && d[1] == 0x8b)
        return "svgz";
    if (len >= 9 && !memcmp(d, "/* XPM */", 9))
        return "xpm";
    size_t scan = len < 4096 ? len : 4096;
    for (size_t i = 0; i + 4 <= scan; i++)
        if (d[i] == '<' && !strncasecmp((const char *)d + i, "<svg", 4))
            return "svg";
    return "";
}

int nirvana_appimage_icon(nirvana_appimage *img, unsigned char **data, size_t *len, const char **format)
{
    if (!img || !img->fs || !data || !len)
        return -EINVAL;
    int r = img->fs->ops->read(img->fs, ".DirIcon", data, len, NULL, 0);
    if (r == 0)
        goto done;
    /* no .DirIcon: the .desktop file's Icon looked for where images keep it */
    nirvana_appimage_meta m;
    if (nirvana_appimage_metadata(img, &m) < 0 || !m.icon) {
        nirvana_appimage_meta_free(&m);
        return -ENOENT;
    }
    static const char *const where[] = {
        "%s.png", "%s.svg", "%s.xpm", "%s",
        "usr/share/icons/hicolor/256x256/apps/%s.png",
        "usr/share/icons/hicolor/512x512/apps/%s.png",
        "usr/share/icons/hicolor/128x128/apps/%s.png",
        "usr/share/icons/hicolor/scalable/apps/%s.svg",
        "usr/share/icons/hicolor/64x64/apps/%s.png",
        "usr/share/icons/hicolor/48x48/apps/%s.png",
        "usr/share/pixmaps/%s.png", "usr/share/pixmaps/%s.svg", "usr/share/pixmaps/%s.xpm",
    };
    r = -ENOENT;
    char path[1024];
    for (size_t i = 0; i < sizeof where / sizeof *where && r < 0; i++) {
        if (strchr(m.icon, '/') && i)
            break; /* an Icon with a path is looked for as it is (the first try) */
        snprintf(path, sizeof path, where[i], m.icon[0] == '/' ? m.icon + 1 : m.icon);
        r = img->fs->ops->read(img->fs, path, data, len, NULL, 0);
        if (r == 0 && !*nirvana_image_format(*data, *len)) {
            free(*data);
            r = -ENOENT;
        }
    }
    nirvana_appimage_meta_free(&m);
    if (r < 0)
        return r;
done:
    if (format)
        *format = nirvana_image_format(*data, *len);
    return 0;
}

#ifndef NIRVANA_WITH_DWARFS
int nv_dwarfs_open(const char *file, uint64_t offset, nv_fs **out)
{
    (void)file;
    (void)offset;
    (void)out;
    return -ENOTSUP;
}
#endif
