/* SPDX-License-Identifier: LGPL-3.0-or-later */
#define _GNU_SOURCE
/* libnirvana: paths inside images, the ELF payload offset, image formats. */
#include "container.h"
#include "nirvana_appimage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failures;

static void join(const char *dir, const char *rel, int want_r, const char *want)
{
    char out[256];
    int r = nv_path_join(dir, rel, out, sizeof out);
    if (r != want_r || (r == 0 && strcmp(out, want))) {
        fprintf(stderr, "join(\"%s\", \"%s\") = %d \"%s\", not %d \"%s\"\n", dir, rel, r, r ? "" : out, want_r,
                want ? want : "");
        failures++;
    }
}

int main(void)
{
    join("", ".DirIcon", 0, ".DirIcon");
    join("usr/share", "../lib/x", 0, "usr/lib/x");
    join("", "./a//b/./c/", 0, "a/b/c");
    join("a", "..", 0, "");
    join("", "..", -22, NULL);
    join("a/b", "../../../x", -22, NULL);
    join("", "usr/share/icons/hicolor/256x256/apps/app.png", 0, "usr/share/icons/hicolor/256x256/apps/app.png");

    /* an ELF file: its payload starts where its section headers end - for a plain program
     * at its end */
    FILE *f = fopen("/bin/true", "rb");
    if (f) {
        struct stat st;
        fstat(fileno(f), &st);
        unsigned char *buf = malloc((size_t)st.st_size);
        if (fread(buf, 1, (size_t)st.st_size, f) == (size_t)st.st_size) {
            uint64_t off = nirvana_elf_payload_offset(buf, (size_t)st.st_size);
            if (off != (uint64_t)st.st_size) {
                fprintf(stderr, "ELF offset %llu, size %lld\n", (unsigned long long)off, (long long)st.st_size);
                failures++;
            }
            /* not ELF, cut short */
            if (nirvana_elf_payload_offset("hello", 5) != 0 || nirvana_elf_payload_offset(buf, 40) != 0) {
                fprintf(stderr, "non-ELF data gave an offset\n");
                failures++;
            }
        }
        free(buf);
        fclose(f);
    }

    const unsigned char png[] = "\x89PNG\r\n\x1a\n....";
    const char svg[] = "<?xml version=\"1.0\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\"/>";
    if (strcmp(nirvana_image_format(png, sizeof png), "png") ||
        strcmp(nirvana_image_format((const unsigned char *)svg, sizeof svg), "svg") ||
        strcmp(nirvana_image_format((const unsigned char *)"/* XPM */", 9), "xpm") ||
        strcmp(nirvana_image_format((const unsigned char *)"text", 4), "")) {
        fprintf(stderr, "image formats\n");
        failures++;
    }
    return failures ? 1 : 0;
}
