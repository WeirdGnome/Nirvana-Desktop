/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * libnirvana, internal: the file systems an AppImage's payload can be. Each reader works
 * on the payload in memory (a read-only mapping of the AppImage), never runs anything,
 * and follows the symbolic links inside the image (".DirIcon" -> "app.png").
 *
 * read: the whole of a regular file (malloc'ed; *data is NUL-terminated past *len), the
 *       path its links led to in *real when real is not NULL.
 * list_root: the names of the regular files and links at the top of the image.
 */
#ifndef NIRVANA_CONTAINER_H
#define NIRVANA_CONTAINER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the most a reader gives of one file */
#define NV_MAX_FILE (64u << 20)
/* the most links followed for one path */
#define NV_MAX_LINKS 16

typedef struct nv_fs nv_fs;

struct nv_fs_ops {
    int (*read)(nv_fs *fs, const char *path, unsigned char **data, size_t *len, char *real, size_t rlen);
    int (*list_root)(nv_fs *fs, char ***names, size_t *count);
    void (*close)(nv_fs *fs);
};

struct nv_fs {
    const struct nv_fs_ops *ops;
};

int nv_squashfs_open(const unsigned char *base, size_t size, nv_fs **out);
int nv_iso9660_open(const unsigned char *base, size_t size, nv_fs **out);
/* DwarFS through libdwarfs, which reads the image from its file (mapped by libdwarfs) at
 * the payload's offset; -ENOTSUP when libnirvana was built without DwarFS */
int nv_dwarfs_open(const char *file, uint64_t offset, nv_fs **out);

/* "a/b/../c/./d" relative to the directory "dir" (no leading slash) -> "a/c/d"; links in a
 * readers' paths are made with this. -EINVAL when the path climbs above the root. */
int nv_path_join(const char *dir, const char *rel, char *out, size_t len);

void nv_names_free(char **names, size_t count);

#ifdef __cplusplus
}
#endif

#endif
