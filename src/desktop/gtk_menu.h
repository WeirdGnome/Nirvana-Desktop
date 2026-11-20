/* SPDX-License-Identifier: GPL-3.0-or-later */
/* nirvana-desktop: GTK programs' menus (org.gtk.Menus, org.gtk.Actions) - gtk_menu.c */
#ifndef NIRVANA_GTK_MENU_H
#define NIRVANA_GTK_MENU_H

#include "desktop.h"

typedef struct Gtk_App Gtk_App;

/* A program's menubar read from its bus name and object path; paths: where its "unity",
 * "app" and "win" actions are (NULL for none). changed is called whenever the menus have
 * been (re)built. */
Gtk_App *nv_gtk_app_new(Eldbus_Connection *bus, const char *name, const char *menu_path, const char *const paths[3],
                        void (*changed)(void *data), void *data);
/* the menus built (owned by the app; valid until the next change) */
Eina_List *nv_gtk_app_menus(Gtk_App *app);
void nv_gtk_app_free(Gtk_App *app);
/* the program's bus name */
const char *nv_gtk_app_bus_name(const Gtk_App *app);

#endif
