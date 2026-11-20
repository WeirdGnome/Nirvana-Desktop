/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: pictures for files - a program's icon (its .desktop file's Icon, or an
 * AppImage's own, read without running it), a file type's (the icon theme through
 * Efreet), else the Nirvana theme's drawer, program or file.
 */
#include "desktop.h"

#include <Efreet.h>
#include <Efreet_Mime.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nirvana_appimage.h"

bool nv_is_appimage(const char *path)
{
    size_t n = path ? strlen(path) : 0;
    return n > 9 && !strcasecmp(path + n - 9, ".AppImage");
}

bool nv_is_desktop_file(const char *path)
{
    size_t n = path ? strlen(path) : 0;
    return n > 8 && !strcasecmp(path + n - 8, ".desktop");
}

/* an icon name or path ("org.gimp.GIMP", "/usr/share/pixmaps/x.png") as a file */
const char *nv_icon_name_path(const char *icon, int size)
{
    if (!icon || !*icon)
        return NULL;
    if (icon[0] == '/')
        return access(icon, R_OK) == 0 ? eina_stringshare_add(icon) : NULL;
    const char *p = efreet_icon_path_find(elm_config_icon_theme_get(), icon, size);
    if (!p)
        p = efreet_icon_path_find("hicolor", icon, size);
    return p; /* (a stringshare from Efreet) */
}

static bool image_from_file(Evas_Object *img, const char *file)
{
    return file && elm_image_file_set(img, file, NULL);
}

Evas_Object *nv_icon_for_path(Evas_Object *parent, const char *path, bool is_dir, int size)
{
    Evas_Object *img = elm_image_add(parent);
    bool ok = false;
    struct stat st;
    bool exec = !is_dir && stat(path, &st) == 0 && (st.st_mode & 0111);

    if (!is_dir && nv_is_appimage(path)) {
        nirvana_appimage *ai;
        if (nirvana_appimage_open(path, &ai) == 0) {
            unsigned char *data;
            size_t len;
            const char *fmt;
            if (nirvana_appimage_icon(ai, &data, &len, &fmt) == 0) {
                ok = elm_image_memfile_set(img, data, len, *fmt ? fmt : NULL, NULL);
                free(data);
            }
            nirvana_appimage_close(ai);
        }
    } else if (!is_dir && nv_is_desktop_file(path)) {
        Efreet_Desktop *d = efreet_desktop_get(path);
        if (d) {
            const char *f = nv_icon_name_path(d->icon, size);
            ok = image_from_file(img, f);
            eina_stringshare_del(f);
            efreet_desktop_free(d);
        }
    } else if (!is_dir) {
        const char *mime = efreet_mime_type_get(path);
        const char *f = mime ? efreet_mime_type_icon_get(mime, elm_config_icon_theme_get(), (unsigned)size) : NULL;
        ok = image_from_file(img, f);
        eina_stringshare_del(f);
    }
    if (!ok) {
        /* the theme's own drawer, program or file picture */
        evas_object_del(img);
        img = elm_layout_add(parent);
        char group[64];
        snprintf(group, sizeof group, "nirvana/lister/icon/%s", is_dir ? "drawer" : exec ? "program" : "file");
        elm_layout_file_set(img, nv.theme, group);
    }
    evas_object_size_hint_min_set(img, size, size);
    evas_object_size_hint_max_set(img, size, size);
    return img;
}
