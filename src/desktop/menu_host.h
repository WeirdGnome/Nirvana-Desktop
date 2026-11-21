/* SPDX-License-Identifier: GPL-3.0-or-later */
/* nirvana-desktop: where menu panels are put, and an open menu's session (menu.c). */
#ifndef NIRVANA_MENU_HOST_H
#define NIRVANA_MENU_HOST_H

#include "desktop.h"

typedef struct Nv_Menu_Host {
    /* a canvas to measure rows in */
    Evas_Object *(*probe_canvas)(void *data);
    /* a place for a panel at screen (x, y), w x h: the window to add it to, and where in
     * that window's canvas it goes */
    Evas_Object *(*panel_new)(void *data, int x, int y, int w, int h, int *ox, int *oy);
    void (*panel_del)(void *data, Evas_Object *host);
    /* the area panels must fit in (NULL: the screen below the bar) */
    void (*bounds)(void *data, int *x, int *y, int *w, int *h);
} Nv_Menu_Host;

typedef struct Nv_Menu_Session Nv_Menu_Session;

enum {
    NV_MENU_KEY_HANDLED = 0,
    NV_MENU_KEY_CLOSED = 1,
    NV_MENU_KEY_PREV = -1, /* Left on the top menu: the menu to the left */
    NV_MENU_KEY_NEXT = 2,  /* Right on an item without a submenu: the menu to the right */
};

Nv_Menu_Session *nv_menu_session_open(const Nv_Menu_Host *host, void *host_data, Nv_Menu *m, int x, int y,
                                      void (*done)(void *data), void *done_data);
void nv_menu_session_close(Nv_Menu_Session *s);
Nv_Menu *nv_menu_session_menu(Nv_Menu_Session *s);
void nv_menu_session_motion(Nv_Menu_Session *s, int x, int y);
bool nv_menu_session_press(Nv_Menu_Session *s, int x, int y);
bool nv_menu_session_release(Nv_Menu_Session *s, int x, int y, bool *closed);
int nv_menu_session_key(Nv_Menu_Session *s, const char *key, const char *string);
void nv_menu_session_select_first(Nv_Menu_Session *s);
/* the panels put in a canvas layer (above a window's own objects) */
void nv_menu_session_raise(Nv_Menu_Session *s, int layer);

#endif
