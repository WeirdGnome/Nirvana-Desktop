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

typedef enum { NV_BACKEND_X11, NV_BACKEND_WAYLAND } Nv_Backend;

typedef struct Nv_Desktop {
    Nv_Backend backend;
    char theme[4096];       /* the theme's .edj */
    int screen_w, screen_h; /* the output's size */
    int bar_h;
    Evas_Object *bar;       /* the screen bar's window */
    Evas_Object *backdrop;  /* the backdrop's window */
    Eina_List *windows;     /* Nv_Window: the desktop's own framed windows */
    struct Nv_Window *active;
} Nv_Desktop;

extern Nv_Desktop nv;

/* the theme's file, found in Themes: (else beside the program) */
bool nv_theme_find(const char *name, char *out, size_t len);
/* an Edje object of a theme group; NULL when the theme lacks it */
Evas_Object *nv_edje_add(Evas_Object *parent, const char *group);
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

/* the display server's kind found; on Wayland the layer shell bound (false when the
 * compositor has none) */
bool nv_layer_init(void);
/* a window for a layer: a desktop window (background, bottom) or a dock (top, overlay) */
Evas_Object *nv_layer_win_add(const char *name, Nv_Layer layer);
/* A window made a layer surface (Wayland) or a dock/desktop window (X11), before it is
 * shown. A size of 0 along an anchored pair of edges means "as wide as the output".
 * exclusive: the strip the window keeps for itself (the bar's height), 0 for none, -1 to
 * cover others' strips too. */
bool nv_layer_setup(Evas_Object *win, Nv_Layer layer, unsigned anchors, int w, int h, int exclusive,
                    bool keyboard, Nv_Layer_Configure_Cb cb, void *data);
/* a layer window's size changed (the bar grows while a menu is open) */
void nv_layer_resize(Evas_Object *win, int w, int h);
/* keyboard focus for a layer window wanted or not (a menu open: wanted) */
void nv_layer_keyboard(Evas_Object *win, bool on);

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

/* ---------------------------------------------------------------- the global menu */

/* the menu registrar on the bus (NULL: none), the focus followed */
bool nv_global_menu_start(Eldbus_Connection *bus);
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

/* ---------------------------------------------------------------- listers, editor */

/* a drawer opened in a lister window (a POSIX or Nirvana path) */
void nv_lister_open(const char *path);
void nv_assign_editor_show(void);

/* ---------------------------------------------------------------- actions */

/* a path opened as wbrun does it (a drawer in a lister, other things started) */
void nv_open_path(const char *posix_path);
void nv_new_shell(void);
void nv_execute_command(void);
void nv_about(void);
void nv_quit(void);
/* a short message in a small window */
void nv_message(const char *title, const char *text);

#endif
