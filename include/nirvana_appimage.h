/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * libnirvana: an AppImage's icon and metadata read without running it (no execve, no
 * FUSE, no mounting). The file is mapped read-only; the payload after the ELF runtime
 * (at e_shoff + e_shnum * e_shentsize) is read as the file system it is:
 *
 *     SquashFS ("hsqs")     - libnirvana's own reader (gzip, lzma, xz, lz4, zstd)
 *     ISO 9660 ("CD001")    - through libarchive
 *     DwarFS   ("DWARFS")   - through libdwarfs, when libnirvana was built with it
 *
 * The functions return 0 on success and a negative errno value on failure; -ENOTSUP when
 * the payload is of a kind (or a compressor) this build cannot read.
 */
#ifndef NIRVANA_APPIMAGE_H
#define NIRVANA_APPIMAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nirvana_appimage_kind {
    NIRVANA_APPIMAGE_UNKNOWN = 0,
    NIRVANA_APPIMAGE_SQUASHFS,
    NIRVANA_APPIMAGE_ISO9660,
    NIRVANA_APPIMAGE_DWARFS,
} nirvana_appimage_kind;

typedef struct nirvana_appimage nirvana_appimage;

/* the keys of the image's .desktop file ([Desktop Entry], unlocalised); NULL when missing */
typedef struct nirvana_appimage_meta {
    char *name;
    char *generic_name;
    char *comment;
    char *exec;
    char *icon;
    char *categories;
    char *version;     /* X-AppImage-Version */
    char *desktop_file; /* the .desktop file's name in the image */
} nirvana_appimage_meta;

/* The payload's offset in an ELF file in memory: the end of its section headers (ELF64 or
 * ELF32, either byte order). 0 when the data is not ELF. */
uint64_t nirvana_elf_payload_offset(const void *data, size_t size);

int nirvana_appimage_open(const char *file, nirvana_appimage **out);
void nirvana_appimage_close(nirvana_appimage *img);

nirvana_appimage_kind nirvana_appimage_get_kind(const nirvana_appimage *img);
uint64_t nirvana_appimage_get_offset(const nirvana_appimage *img);
/* "SquashFS", "ISO 9660", "DwarFS" */
const char *nirvana_appimage_kind_name(nirvana_appimage_kind kind);

/* A file of the image (its links followed) into memory; free() it. The data is
 * NUL-terminated past *len. */
int nirvana_appimage_read(nirvana_appimage *img, const char *path, unsigned char **data, size_t *len);

/* The image's icon: .DirIcon, else the .desktop file's Icon (at the top, or in
 * usr/share/icons/hicolor, or usr/share/pixmaps). *format is "png", "svg", "svgz", "xpm"
 * or "" when not known. free() the data. */
int nirvana_appimage_icon(nirvana_appimage *img, unsigned char **data, size_t *len, const char **format);

/* The .desktop file at the top of the image, as text; free() it. */
int nirvana_appimage_desktop(nirvana_appimage *img, char **text, size_t *len);

/* The .desktop file's keys; free with nirvana_appimage_meta_free(). */
int nirvana_appimage_metadata(nirvana_appimage *img, nirvana_appimage_meta *out);
void nirvana_appimage_meta_free(nirvana_appimage_meta *meta);

/* What kind of image data is: "png", "svg", "svgz", "xpm" or "". */
const char *nirvana_image_format(const unsigned char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* NIRVANA_APPIMAGE_H */
