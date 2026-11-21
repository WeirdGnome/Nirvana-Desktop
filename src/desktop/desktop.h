/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: what its parts share.
 */
#ifndef NIRVANA_DESKTOP_H
#define NIRVANA_DESKTOP_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define EFL_BETA_API_SUPPORT 1
#include <Eldbus.h>
#include <Elementary.h>
#include <stdbool.h>

#include "../common/i18n.h"
#include "nirvana.h"

#define NV_SCREEN_TITLE "Nirvana Screen"

/* ---------------------------------------------------------------- the session */

typedef struct Nv_Desktop {
    char theme[4096];       /* the theme's .edj */
    int screen_w, screen_h; /* the output's size */
    int bar_h;
    Evas_Object *bar;       /* the screen bar's window */
    Evas_Object *backdrop;  /* the backdrop's window */
    Eina_List *windows;     /* Nv_Window: the desktop's own framed windows */
    struct Nv_Window *active;
    /* the window the screen's View, Edit and Icons menus act on: the last of the
     * desktop's windows to be active, NULL when the desktop itself was clicked last */
    struct Nv_Window *view_window;
} Nv_Desktop;

extern Nv_Desktop nv;

/* the theme's file, found in Themes: (else beside the program) */
bool nv_theme_find(const char *name, char *out, size_t len);
/* an Edje object of a theme group; NULL when the theme lacks it */
Evas_Object *nv_edje_add(Evas_Object *parent, const char *group);
/* a setting of ~/.nirvana/Prefs/desktop.conf ("menus=hold"), or def */
const char *nv_pref(const char *key, const char *def);
/* a setting written to desktop.conf (the other lines kept) */
bool nv_pref_set(const char *key, const char *value);
/* the themes in Themes: (names, stringshares, sorted), the one in use, another chosen
 * (saved, and the desktop started again in it) */
Eina_List *nv_themes_list(void);
const char *nv_theme_name(void);
void nv_theme_use(const char *name);
/* a data item of the theme ("nirvana/screenbar/height") */
const char *nv_theme_data(const char *key);

/* ---------------------------------------------------------------- layers (layer.c) */

typedef enum { NV_LAYER_BACKGROUND, NV_LAYER_BOTTOM, NV_LAYER_TOP, NV_LAYER_OVERLAY } Nv_Layer;

enum {
    NV_ANCHOR_TOP = 1,
    NV_ANCHOR_BOTTOM = 2,
    NV_ANCHOR_LEFT = 4,
    NV_ANCHOR_RIGHT = 8,
};

typedef void (*Nv_Layer_Configure_Cb)(void *data, Evas_Object *win, int w, int h);

/* (the layer shell is bound with the first layer window) */
bool nv_layer_init(void);
/* a window for a layer (background, bottom, top, overlay) */
Evas_Object *nv_layer_win_add(const char *name, Nv_Layer layer);
/* A window made a layer surface, before it is shown. A size of 0 along an anchored pair of edges means "as wide as the output".
 * exclusive: the strip the window keeps for itself (the bar's height), 0 for none, -1 to
 * cover others' strips too. */
bool nv_layer_setup(Evas_Object *win, Nv_Layer layer, unsigned anchors, int w, int h, int exclusive,
                    bool keyboard, Nv_Layer_Configure_Cb cb, void *data);
/* a layer window's size changed (the bar grows while a menu is open) */
void nv_layer_resize(Evas_Object *win, int w, int h);
/* keyboard focus for a layer window wanted or not (a menu open: wanted) */
void nv_layer_keyboard(Evas_Object *win, bool on);
/* the keyboard's mode exactly: 0 none, 1 exclusive, 2 on demand */
void nv_layer_keyboard_mode(Evas_Object *win, int mode);

/* ---------------------------------------------------------------- the compositor */

/* the window under the pointer put to the back by the compositor (Wayland: labwc, with
 * Nirvana's settings); false when the compositor cannot be asked */
bool nv_compositor_lower_under_pointer(void);

/* ---------------------------------------------------------------- menus (menu.c) */

typedef struct Nv_Menu Nv_Menu;
typedef struct Nv_Menu_Item Nv_Menu_Item;
typedef void (*Nv_Menu_Cb)(void *data, Nv_Menu_Item *item);

typedef enum { NV_ITEM_NORMAL, NV_ITEM_CHECK, NV_ITEM_RADIO, NV_ITEM_SEPARATOR } Nv_Item_Kind;

struct Nv_Menu_Item {
    Nv_Item_Kind kind;
    char *label;     /* "_" before a letter: its key; "__": an underscore */
    char *shortcut;  /* shown at the right ("Super+O") */
    bool enabled;
    bool checked;
    Nv_Menu *submenu;
    Nv_Menu_Cb cb;
    void *data;
    int id;          /* the owner's (a D-Bus menu's item id) */
};

struct Nv_Menu {
    char *title;
    Eina_List *items; /* Nv_Menu_Item */
    void (*about_to_show)(void *data, Nv_Menu *menu);
    void *about_data;
    int id;
};

Nv_Menu *nv_menu_new(const char *title);
Nv_Menu_Item *nv_menu_add(Nv_Menu *m, const char *label, const char *shortcut, Nv_Menu_Cb cb, void *data);
Nv_Menu_Item *nv_menu_add_check(Nv_Menu *m, const char *label, bool checked, Nv_Menu_Cb cb, void *data);
Nv_Menu_Item *nv_menu_add_submenu(Nv_Menu *m, const char *label, Nv_Menu *sub);
void nv_menu_add_separator(Nv_Menu *m);
void nv_menu_clear(Nv_Menu *m);
void nv_menu_free(Nv_Menu *m);

/* ---------------------------------------------------------------- the screen bar */

bool nv_screenbar_create(void);
/* the menus shown: the desktop's own, or a program's (the global menu) with its name;
 * NULL: the desktop's own again */
void nv_screenbar_menus_set(Eina_List *menus, const char *owner);
Eina_List *nv_screenbar_own_menus(void);
void nv_screenbar_title_set(const char *title);
bool nv_screenbar_menu_is_open(void);
/* the right button pressed on the desktop (at screen x, y): the menu strip held open;
 * the moves and the release that follow are passed on while it is held */
void nv_screenbar_hold_begin(int x, int y);
bool nv_screenbar_holding(void);
void nv_screenbar_hold_move(int x, int y);
void nv_screenbar_hold_end(int x, int y);
/* a menu at a place of the screen (the dock's, an icon's), shown by the bar */
void nv_screenbar_popup(Nv_Menu *m, int x, int y);
/* a context menu at the pointer while the right button pressed at x, y is held (the
 * moves and the release passed on with nv_screenbar_hold_move/_end) */
void nv_screenbar_context(Nv_Menu *m, int x, int y);

/* a context menu in one of the desktop's windows, at (x, y) of its canvas, while the
 * right button pressed on `pressed` is held (NULL: opened by a key); done is called when
 * it shuts (to free m) */
void nv_win_context(Evas_Object *win, Evas_Object *pressed, Nv_Menu *m, int x, int y, void (*done)(void *data),
                    void *data);
void nv_win_context_close(void);

/* ---------------------------------------------------------------- the global menu */

/* the menu registrar on the bus (NULL: none), the focus followed */
bool nv_global_menu_start(Eldbus_Connection *bus);
/* the backdrop got or lost the focus (its own menus while it has it) */
void nv_global_menu_desktop_focus(bool on);
/* (the bar's menu was shut) */
void nv_global_menu_closed(void);
/* the bar opens a menu: it grows over the screen while the menu is open */
void nv_screenbar_menu_open(int index);
void nv_screenbar_menu_close(void);

/* ---------------------------------------------------------------- the backdrop */

bool nv_backdrop_create(void);
/* the volume icons made again from the assigns (SIGUSR1, the editor's Use) */
void nv_backdrop_reload(void);
/* the selected icons' assign names (for the Icons menu) */
Eina_List *nv_backdrop_selected(void);
void nv_backdrop_open_selected(void);
void nv_backdrop_select_all(bool on);
void nv_backdrop_clean_up(void);
void nv_backdrop_snapshot(void);

/* ---------------------------------------------------------------- framed windows */

typedef struct Nv_Window Nv_Window;
typedef void (*Nv_Window_Cb)(void *data, Nv_Window *w);

/* how a view sorts (the order of Ambient's Sort By menu) */
typedef enum { NV_SORT_NAME, NV_SORT_TYPE, NV_SORT_SIZE, NV_SORT_DATE } Nv_Sort;

/* what a window showing files (a lister) lets the screen's menus do with it */
typedef struct Nv_View_Ops {
    bool (*list_mode)(void *v);
    void (*set_list_mode)(void *v, bool on);
    Nv_Sort (*sort)(void *v);
    void (*set_sort)(void *v, Nv_Sort s);
    bool (*show_all)(void *v);
    void (*set_show_all)(void *v, bool on);
    void (*select_all)(void *v);
    void (*invert)(void *v);
    /* the selected items' POSIX paths (malloc'd strings) */
    Eina_List *(*selected)(void *v);
    /* the directory shown (POSIX), or NULL (an archive) */
    const char *(*directory)(void *v);
    void (*information)(void *v);
    void (*update)(void *v);
} Nv_View_Ops;

struct Nv_Window {
    Evas_Object *win;
    Evas_Object *layout;   /* the theme's nirvana/frame (an elm_layout) */
    Evas_Object *frame;    /* its Edje object */
    Evas_Object *content;  /* swallowed */
    char *title;
    bool zoomed;
    int normal_w, normal_h, zoom_w, zoom_h;
    Nv_Window_Cb on_close; /* else the window is deleted */
    Nv_Window_Cb on_prefs; /* NULL: no prefs gadget action */
    void *data;
    Nv_Menu *window_menu;  /* the window's own items in the Window menu, or NULL */
    const Nv_View_Ops *view; /* a window showing files: its operations, else NULL */
    void *view_data;
};

Nv_Window *nv_window_new(const char *name, const char *title, int w, int h);
void nv_window_content_set(Nv_Window *w, Evas_Object *content);
void nv_window_title_set(Nv_Window *w, const char *title);
void nv_window_show(Nv_Window *w);
void nv_window_close(Nv_Window *w);
void nv_window_iconify(Nv_Window *w);
void nv_window_zoom(Nv_Window *w);
void nv_window_depth(Nv_Window *w);
void nv_window_jump(Nv_Window *w);
Nv_Window *nv_window_active(void);

/* ---------------------------------------------------------------- pictures (icons.c) */

bool nv_is_appimage(const char *path);
bool nv_is_desktop_file(const char *path);
/* an icon theme's name ("org.gimp.GIMP") or a path as a file (a stringshare), or NULL */
const char *nv_icon_name_path(const char *icon, int size);
/* a picture for a file: a program's or AppImage's icon, its type's, else the theme's */
Evas_Object *nv_icon_for_path(Evas_Object *parent, const char *path, bool is_dir, int size);

/* ---------------------------------------------------------------- drives (drives.c) */

/* a removable volume, as UDisks2 tells it */
typedef struct Nv_Drive {
    char *path;      /* its UDisks2 object (a stable key) */
    char *label;     /* what the icon says */
    char *mount;     /* where it is mounted, or NULL */
    char *drive;     /* its drive's object, or NULL */
    bool can_eject, power_off;
} Nv_Drive;

/* UDisks2 followed (false when there is no system bus); the backdrop told of changes */
bool nv_drives_start(void);
const Eina_List *nv_drives_list(void);
Nv_Drive *nv_drive_find(const char *path);
/* mounted first when it is not, then opened in a lister */
void nv_drive_open(Nv_Drive *d);
/* unmounted, then ejected or powered off when the drive can be */
void nv_drive_eject(Nv_Drive *d);
/* the selected drive icons ejected (the Icons menu) */
void nv_backdrop_eject_selected(void);
void nv_backdrop_invert(void);
/* an icon is selected on the desktop (drives: a drive's icon) */
bool nv_backdrop_any_selected(bool drives);
/* the selected volumes' Information */
void nv_backdrop_information(void);
/* a volume's Information: its name, where it is, its size and free space */
void nv_volume_information(const char *name, const char *posix);

/* ---------------------------------------------------------------- the dock */

bool nv_dock_create(void);

/* ---------------------------------------------------------------- listers, editor */

/* a drawer opened in a lister window (a POSIX or Nirvana path) */
void nv_lister_open(const char *path);
void nv_assign_editor_show(void);

/* ---------------------------------------------------------------- actions */

/* a path opened as wbrun does it (a drawer in a lister, other things started) */
void nv_open_path(const char *posix_path);
void nv_new_shell(void);
/* a shell in a drawer (POSIX; NULL: home) */
void nv_new_shell_at(const char *dir);
void nv_execute_command(void);
void nv_about(void);
/* the system's name, kernel, processor, memory */
void nv_about_system(void);
void nv_quit(void);
/* a short message in a small window */
void nv_message(const char *title, const char *text);

#endif
