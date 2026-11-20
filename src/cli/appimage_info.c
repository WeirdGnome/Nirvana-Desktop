/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * appimage-info - what an AppImage is, read without running it.
 *
 *     appimage-info FILE                    its kind, payload offset and .desktop keys
 *     appimage-info FILE --icon OUT         its icon written to OUT
 *     appimage-info FILE --desktop          its .desktop file printed
 *     appimage-info FILE --extract PATH OUT one file of the image written to OUT ("-": stdout)
 */
#define _GNU_SOURCE
#include "../common/i18n.h"
#include "nirvana_appimage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_out(const char *out, const unsigned char *data, size_t len)
{
    FILE *f = strcmp(out, "-") ? fopen(out, "wb") : stdout;
    if (!f) {
        perror(out);
        return 10;
    }
    size_t w = fwrite(data, 1, len, f);
    if (f != stdout)
        fclose(f);
    return w == len ? 0 : 10;
}

int main(int argc, char **argv)
{
    nirvana_i18n_init();
    if (argc < 2 || !strcmp(argv[1], "--help")) {
        fprintf(argc < 2 ? stderr : stdout,
                _("Usage: appimage-info FILE [--icon OUT | --desktop | --extract PATH OUT]\n"));
        return argc < 2 ? 1 : 0;
    }
    nirvana_appimage *img;
    int r = nirvana_appimage_open(argv[1], &img);
    if (r < 0) {
        fprintf(stderr, "appimage-info: %s: %s\n", argv[1], strerror(-r));
        return r == -ENOTSUP ? 20 : 10;
    }
    int rc = 0;
    unsigned char *data = NULL;
    size_t len = 0;
    if (argc >= 4 && !strcmp(argv[2], "--icon")) {
        const char *fmt;
        if ((r = nirvana_appimage_icon(img, &data, &len, &fmt)) < 0) {
            fprintf(stderr, _("appimage-info: no icon: %s\n"), strerror(-r));
            rc = 5;
        } else {
            rc = write_out(argv[3], data, len);
            if (!rc && strcmp(argv[3], "-"))
                printf("%s %zu\n", *fmt ? fmt : "?", len);
        }
    } else if (argc >= 3 && !strcmp(argv[2], "--desktop")) {
        char *text;
        if ((r = nirvana_appimage_desktop(img, &text, &len)) < 0) {
            fprintf(stderr, _("appimage-info: no .desktop file: %s\n"), strerror(-r));
            rc = 5;
        } else {
            fwrite(text, 1, len, stdout);
            free(text);
        }
    } else if (argc >= 5 && !strcmp(argv[2], "--extract")) {
        if ((r = nirvana_appimage_read(img, argv[3], &data, &len)) < 0) {
            fprintf(stderr, "appimage-info: %s: %s\n", argv[3], strerror(-r));
            rc = 5;
        } else {
            rc = write_out(argv[4], data, len);
        }
    } else if (argc == 2) {
        printf("Kind: %s\n", nirvana_appimage_kind_name(nirvana_appimage_get_kind(img)));
        printf("Offset: %llu\n", (unsigned long long)nirvana_appimage_get_offset(img));
        nirvana_appimage_meta m;
        if (nirvana_appimage_metadata(img, &m) == 0) {
            printf("Desktop-File: %s\n", m.desktop_file ? m.desktop_file : "");
            printf("Name: %s\n", m.name ? m.name : "");
            if (m.generic_name)
                printf("GenericName: %s\n", m.generic_name);
            if (m.comment)
                printf("Comment: %s\n", m.comment);
            if (m.exec)
                printf("Exec: %s\n", m.exec);
            if (m.icon)
                printf("Icon: %s\n", m.icon);
            if (m.categories)
                printf("Categories: %s\n", m.categories);
            if (m.version)
                printf("Version: %s\n", m.version);
        }
        nirvana_appimage_meta_free(&m);
    } else {
        fprintf(stderr, _("appimage-info: bad arguments\n"));
        rc = 1;
    }
    free(data);
    nirvana_appimage_close(img);
    return rc;
}
