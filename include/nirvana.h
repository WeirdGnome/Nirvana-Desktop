/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * libnirvana - the core of Nirvana Desktop.
 *
 * Assigns: logical volume names ("Home:", "Downloads:") that stand for one or more POSIX
 * directories. The assigns of the running session live in a small text database in a
 * tmpfs directory of the user's:
 *
 *     $XDG_RUNTIME_DIR/nirvana-ram/assigns.db      (else /run/user/$UID/nirvana-ram)
 *
 * one assign per line:
 *
 *     NAME:|DESKTOP|PATH_1;PATH_2;...
 *
 * DESKTOP is 1 when the assign has a volume icon on the desktop. An assign with more than
 * one path is a multi-assign: its paths are searched from left to right. Names are
 * matched without regard to case.
 *
 * Every function returns 0 (or a non-negative count) on success and a negative errno value
 * on failure, unless its comment says otherwise.
 */
#ifndef NIRVANA_H
#define NIRVANA_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NIRVANA_NAME_MAX 64

typedef struct nirvana_assign {
    char name[NIRVANA_NAME_MAX];  /* without the colon */
    bool desktop;                 /* a volume icon on the desktop */
    char **paths;                 /* the targets, searched in this order */
    size_t npaths;
} nirvana_assign;

typedef struct nirvana_assign_list {
    nirvana_assign *items;
    size_t count;
} nirvana_assign_list;

/* The database's path (the directory is created when missing). */
int nirvana_assign_db_path(char *out, size_t len);

/* The session's start: the default assigns are written into the database (SYS:, Home:,
 * RAM:, Nirvana:, Themes:, Tools:, the XDG user directories as Downloads:, Docs:, Pics:,
 * Music:, Videos:, and one assign for each visible directory at the top of $HOME).
 * Assigns already in the database that are not defaults are kept. */
int nirvana_assign_init(void);

/* All the assigns, in the database's order; free with nirvana_assign_list_free(). */
int nirvana_assign_list_load(nirvana_assign_list *out);
void nirvana_assign_list_free(nirvana_assign_list *list);

/* One assign by name ("Home" or "Home:"); -ENOENT when there is none. Free with
 * nirvana_assign_free(). */
int nirvana_assign_get(const char *name, nirvana_assign *out);
void nirvana_assign_free(nirvana_assign *a);

/* An assign made or replaced with these paths (a new one goes to the end). */
int nirvana_assign_set(const char *name, const char *const *paths, size_t npaths, bool desktop);
/* The desktop flag of an existing assign. */
int nirvana_assign_set_desktop(const char *name, bool desktop);
/* A path added to an assign (made when missing) at a position (-1: at the end). */
int nirvana_assign_add_path(const char *name, const char *path, int position);
/* A path of an assign removed by its index; the assign goes when it has no paths left. */
int nirvana_assign_remove_path(const char *name, size_t index);
/* A path of an assign moved from one index to another (its search order). */
int nirvana_assign_move_path(const char *name, size_t from, size_t to);
/* An assign removed. */
int nirvana_assign_delete(const char *name);
/* An assign renamed. */
int nirvana_assign_rename(const char *name, const char *new_name);

/* A Nirvana path ("Home:Docs/letter.txt", "SYS:etc") as a POSIX path. A multi-assign
 * gives its first target where the rest of the path exists, else its first target. A
 * path without a volume name is a POSIX path already and is copied. -ENOENT: no such
 * assign; -ENAMETOOLONG: out is too small. */
int nirvana_to_posix(const char *nirvana_path, char *out, size_t len);

/* A POSIX path as a Nirvana path: the assign whose target is the longest prefix of the
 * path ("/home/ann/Downloads/a.png" -> "Downloads:a.png"). */
int posix_to_nirvana(const char *posix_path, char *out, size_t len);

/* The user's Startup-Sequence ($HOME/.nirvana/Startup-Sequence): the assign's line
 * ("assign Name: /p1;/p2 --desktop") written or replaced, or removed. */
int nirvana_assign_persist(const char *name);
int nirvana_assign_unpersist(const char *name);

/* The running desktop told to read the assigns again (SIGUSR1 to nirvana-desktop);
 * returns how many processes were told. */
int nirvana_notify_desktop(void);

/* ---------------------------------------------------------------- launching */

/* The environment a program started from the desktop gets so that it shows its menus in
 * the screen bar: appmenu-gtk-module added to GTK_MODULES, UBUNTU_MENUPROXY=1,
 * QT_QPA_PLATFORMTHEME=appmenu-qt5 unless set, SAL_USE_VCLPLUGIN=gtk3 unless set. Changes
 * this process's environment (for its children). */
void nirvana_launch_env(void);

/* The desktop file id ("org.example.Viewer.desktop") of the default program for a MIME
 * type, from the mimeapps.list files of the XDG configuration and data directories
 * ([Default Applications], then [Added Associations]). -ENOENT when none is set. */
int nirvana_mime_default(const char *mime, char *out, size_t len);

/* A program started detached (its own session, stdin from /dev/null) with nirvana_launch_env()
 * applied: argv[0] looked up in PATH. Returns the child's pid. */
int nirvana_spawn(const char *const *argv);
/* the same with a shell command line ("/bin/sh -c") */
int nirvana_spawn_shell(const char *command);

#ifdef __cplusplus
}
#endif

#endif /* NIRVANA_H */
