/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * libnirvana's assigns tested in a made-up home: the defaults, changes, multi-assigns,
 * both translations, the Startup-Sequence.
 */
#define _GNU_SOURCE
#include "nirvana.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

#define CHECK_STR(a, b)                                                                    \
    do {                                                                                   \
        const char *a_ = (a), *b_ = (b);                                                   \
        if (strcmp(a_, b_)) {                                                              \
            fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, a_, b_);       \
            failures++;                                                                    \
        }                                                                                  \
    } while (0)

static char root[PATH_MAX], home[PATH_MAX + 8], run[PATH_MAX + 8];

static void mk(const char *fmt, const char *arg)
{
    char p[PATH_MAX * 2];
    snprintf(p, sizeof p, fmt, arg);
    mkdir(p, 0755);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    fputs(text, f);
    fclose(f);
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return strdup("");
    static char buf[65536];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return strdup(buf);
}

static void setup(void)
{
    snprintf(root, sizeof root, "/tmp/nirvana-test-XXXXXX");
    const char *tmp = getenv("TMPDIR");
    if (tmp && *tmp)
        snprintf(root, sizeof root, "%s/nirvana-test-XXXXXX", tmp);
    if (!mkdtemp(root)) {
        perror("mkdtemp");
        exit(99);
    }
    snprintf(home, sizeof home, "%s/home", root);
    snprintf(run, sizeof run, "%s/run", root);
    mkdir(home, 0755);
    mkdir(run, 0700);
    mk("%s/Downloads", home);
    mk("%s/Documents", home);
    mk("%s/Projects", home);
    mk("%s/.hidden", home);
    mk("%s/Bilder", home); /* a localised pictures directory */
    mk("%s/.config", home);
    char p[PATH_MAX * 2];
    snprintf(p, sizeof p, "%s/.config/user-dirs.dirs", home);
    write_file(p, "# made by the test\n"
                  "XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n"
                  "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n"
                  "XDG_PICTURES_DIR=\"$HOME/Bilder\"\n"
                  "XDG_MUSIC_DIR=\"$HOME/\"\n" /* = $HOME: not set */
                  "XDG_VIDEOS_DIR=\"$HOME/Videos\"\n");
    snprintf(p, sizeof p, "%s/notadir", home);
    write_file(p, "x");
    setenv("HOME", home, 1);
    setenv("XDG_RUNTIME_DIR", run, 1);
    unsetenv("XDG_CONFIG_HOME");
}

static void test_defaults(void)
{
    CHECK(nirvana_assign_init() == 0);
    nirvana_assign a;
    CHECK(nirvana_assign_get("SYS:", &a) == 0);
    CHECK(a.npaths == 1 && a.desktop);
    CHECK_STR(a.paths[0], "/");
    nirvana_assign_free(&a);

    CHECK(nirvana_assign_get("home", &a) == 0); /* names without regard to case */
    CHECK_STR(a.name, "Home");
    CHECK_STR(a.paths[0], home);
    CHECK(a.desktop);
    nirvana_assign_free(&a);

    char want[PATH_MAX * 2];
    CHECK(nirvana_assign_get("RAM", &a) == 0);
    snprintf(want, sizeof want, "%s/nirvana-ram", run);
    CHECK_STR(a.paths[0], want);
    nirvana_assign_free(&a);

    CHECK(nirvana_assign_get("Nirvana", &a) == 0);
    CHECK(a.npaths == 2 && !a.desktop);
    snprintf(want, sizeof want, "%s/.nirvana", home);
    CHECK_STR(a.paths[0], want);
    CHECK_STR(a.paths[1], "/opt/nirvana");
    nirvana_assign_free(&a);

    CHECK(nirvana_assign_get("Tools", &a) == 0);
    CHECK(a.npaths == 2);
    nirvana_assign_free(&a);

    CHECK(nirvana_assign_get("Downloads", &a) == 0);
    snprintf(want, sizeof want, "%s/Downloads", home);
    CHECK_STR(a.paths[0], want);
    CHECK(!a.desktop);
    nirvana_assign_free(&a);

    CHECK(nirvana_assign_get("Docs", &a) == 0);
    nirvana_assign_free(&a);
    CHECK(nirvana_assign_get("Pics", &a) == 0);
    snprintf(want, sizeof want, "%s/Bilder", home);
    CHECK_STR(a.paths[0], want);
    nirvana_assign_free(&a);
    CHECK(nirvana_assign_get("Music", &a) == -ENOENT); /* set to $HOME */
    CHECK(nirvana_assign_get("Videos", &a) == 0);      /* set, even if missing */
    nirvana_assign_free(&a);

    /* the folders of $HOME: their own assign unless an XDG one has them */
    CHECK(nirvana_assign_get("Projects", &a) == 0);
    nirvana_assign_free(&a);
    CHECK(nirvana_assign_get("Documents", &a) == -ENOENT);
    CHECK(nirvana_assign_get("Bilder", &a) == -ENOENT);
    CHECK(nirvana_assign_get(".hidden", &a) == -ENOENT);
    CHECK(nirvana_assign_get("notadir", &a) == -ENOENT);

    /* the database's text */
    char db[PATH_MAX];
    CHECK(nirvana_assign_db_path(db, sizeof db) == 0);
    char *text = read_file(db);
    CHECK(strstr(text, "SYS:|1|/\n") != NULL);
    char line[PATH_MAX * 2];
    snprintf(line, sizeof line, "Nirvana:|0|%s/.nirvana;/opt/nirvana\n", home);
    CHECK(strstr(text, line) != NULL);
    free(text);

    /* init again: nothing doubled, a user's assign kept */
    const char *w[] = {"/srv/work"};
    CHECK(nirvana_assign_set("Work:", w, 1, false) == 0);
    nirvana_assign_list l;
    CHECK(nirvana_assign_list_load(&l) > 0);
    size_t before = l.count;
    nirvana_assign_list_free(&l);
    CHECK(nirvana_assign_init() == 0);
    CHECK(nirvana_assign_list_load(&l) == (int)before);
    nirvana_assign_list_free(&l);
    CHECK(nirvana_assign_get("Work", &a) == 0);
    nirvana_assign_free(&a);
}

static void test_changes(void)
{
    char d1[PATH_MAX * 2], d2[PATH_MAX * 2], d3[PATH_MAX * 2];
    snprintf(d1, sizeof d1, "%s/m1", root);
    snprintf(d2, sizeof d2, "%s/m2", root);
    snprintf(d3, sizeof d3, "%s/m3/", root); /* the slash goes */
    mkdir(d1, 0755);
    mkdir(d2, 0755);
    const char *paths[] = {d1, d2};
    CHECK(nirvana_assign_set("Multi:", paths, 2, true) == 0);
    CHECK(nirvana_assign_add_path("Multi", d3, -1) == 0);
    nirvana_assign a;
    CHECK(nirvana_assign_get("MULTI", &a) == 0);
    CHECK(a.npaths == 3 && a.desktop);
    CHECK_STR(a.paths[1], d2);
    d3[strlen(d3) - 1] = 0;
    CHECK_STR(a.paths[2], d3);
    nirvana_assign_free(&a);

    CHECK(nirvana_assign_move_path("Multi", 2, 0) == 0);
    CHECK(nirvana_assign_get("Multi", &a) == 0);
    CHECK_STR(a.paths[0], d3);
    CHECK_STR(a.paths[1], d1);
    CHECK_STR(a.paths[2], d2);
    nirvana_assign_free(&a);
    CHECK(nirvana_assign_move_path("Multi", 0, 2) == 0);
    CHECK(nirvana_assign_get("Multi", &a) == 0);
    CHECK_STR(a.paths[0], d1);
    CHECK_STR(a.paths[2], d3);
    nirvana_assign_free(&a);
    CHECK(nirvana_assign_move_path("Multi", 0, 5) == -ERANGE);

    CHECK(nirvana_assign_set_desktop("Multi", false) == 0);
    CHECK(nirvana_assign_get("Multi", &a) == 0);
    CHECK(!a.desktop);
    nirvana_assign_free(&a);

    CHECK(nirvana_assign_remove_path("Multi", 2) == 0);
    CHECK(nirvana_assign_get("Multi", &a) == 0);
    CHECK(a.npaths == 2);
    nirvana_assign_free(&a);

    /* bad names and paths */
    const char *bad[] = {"/a;b"};
    CHECK(nirvana_assign_set("Bad:", bad, 1, false) == -EINVAL);
    const char *good[] = {"/tmp"};
    CHECK(nirvana_assign_set("Ba/d:", good, 1, false) == -EINVAL);
    CHECK(nirvana_assign_set(":", good, 1, false) == -EINVAL);

    /* rename and delete */
    CHECK(nirvana_assign_set("Tmp:", good, 1, false) == 0);
    CHECK(nirvana_assign_rename("Tmp", "Multi") == -EEXIST);
    CHECK(nirvana_assign_rename("Tmp", "Scratch") == 0);
    CHECK(nirvana_assign_get("Tmp", &a) == -ENOENT);
    CHECK(nirvana_assign_delete("Scratch:") == 0);
    CHECK(nirvana_assign_delete("Scratch:") == -ENOENT);

    /* the last path removed: the assign goes */
    CHECK(nirvana_assign_set("One:", good, 1, false) == 0);
    CHECK(nirvana_assign_remove_path("One", 0) == 0);
    CHECK(nirvana_assign_get("One", &a) == -ENOENT);
}

static void test_translation(void)
{
    char out[PATH_MAX], want[PATH_MAX * 2];

    CHECK(nirvana_to_posix("SYS:etc/passwd", out, sizeof out) == 0);
    CHECK_STR(out, "/etc/passwd");
    CHECK(nirvana_to_posix("SYS:", out, sizeof out) == 0);
    CHECK_STR(out, "/");
    CHECK(nirvana_to_posix("Home:", out, sizeof out) == 0);
    CHECK_STR(out, home);
    CHECK(nirvana_to_posix("home:Projects/x.c", out, sizeof out) == 0);
    snprintf(want, sizeof want, "%s/Projects/x.c", home);
    CHECK_STR(out, want);
    CHECK(nirvana_to_posix("Nope:x", out, sizeof out) == -ENOENT);
    CHECK(nirvana_to_posix("/usr/bin", out, sizeof out) == 0); /* POSIX already */
    CHECK_STR(out, "/usr/bin");
    CHECK(nirvana_to_posix("dir/with:colon", out, sizeof out) == 0);
    CHECK_STR(out, "dir/with:colon");
    char tiny[4];
    CHECK(nirvana_to_posix("SYS:etc/passwd", tiny, sizeof tiny) == -ENAMETOOLONG);

    /* a multi-assign: the first target where the file is, else the first */
    char f[PATH_MAX * 2];
    snprintf(f, sizeof f, "%s/m2/only-here.txt", root);
    write_file(f, "x");
    CHECK(nirvana_to_posix("Multi:only-here.txt", out, sizeof out) == 0);
    CHECK_STR(out, f);
    CHECK(nirvana_to_posix("Multi:nowhere.txt", out, sizeof out) == 0);
    snprintf(want, sizeof want, "%s/m1/nowhere.txt", root);
    CHECK_STR(out, want);

    /* the other way: the longest prefix wins */
    snprintf(f, sizeof f, "%s/Downloads/test.png", home);
    CHECK(posix_to_nirvana(f, out, sizeof out) == 0);
    CHECK_STR(out, "Downloads:test.png");
    snprintf(f, sizeof f, "%s/Downloads", home);
    CHECK(posix_to_nirvana(f, out, sizeof out) == 0);
    CHECK_STR(out, "Downloads:");
    snprintf(f, sizeof f, "%s/Downloadsx/a", home); /* not a prefix at a slash */
    CHECK(posix_to_nirvana(f, out, sizeof out) == 0);
    CHECK_STR(out, "Home:Downloadsx/a");
    snprintf(f, sizeof f, "%s/.nirvana/Themes/x.edj", home);
    CHECK(posix_to_nirvana(f, out, sizeof out) == 0);
    CHECK_STR(out, "Themes:x.edj");
    CHECK(posix_to_nirvana("/usr/share/applications/a.desktop", out, sizeof out) == 0);
    CHECK_STR(out, "Tools:a.desktop");
    CHECK(posix_to_nirvana("/usr/bin/", out, sizeof out) == 0);
    CHECK_STR(out, "SYS:usr/bin");
    CHECK(posix_to_nirvana("/", out, sizeof out) == 0);
    CHECK_STR(out, "SYS:");
    snprintf(f, sizeof f, "%s/m2/x", root);
    CHECK(posix_to_nirvana(f, out, sizeof out) == 0);
    CHECK_STR(out, "Multi:x");
}

static void test_persist(void)
{
    char ss[PATH_MAX * 2];
    snprintf(ss, sizeof ss, "%s/.nirvana/Startup-Sequence", home);
    mk("%s/.nirvana", home);
    write_file(ss, "#!/bin/sh\nexport UBUNTU_MENUPROXY=1\nassign multi: '/old' --desktop\necho done\n");
    CHECK(nirvana_assign_persist("Multi") == 0);
    char *t = read_file(ss);
    char want[PATH_MAX * 4];
    snprintf(want, sizeof want, "#!/bin/sh\nexport UBUNTU_MENUPROXY=1\nassign Multi: '%s/m1;%s/m2'\necho done\n", root, root);
    CHECK_STR(t, want);
    free(t);

    const char *q[] = {"/it's here"};
    CHECK(nirvana_assign_set("Quote:", q, 1, true) == 0);
    CHECK(nirvana_assign_persist("Quote:") == 0);
    t = read_file(ss);
    CHECK(strstr(t, "echo done\nassign Quote: '/it'\\''s here' --desktop\n") != NULL);
    free(t);

    CHECK(nirvana_assign_unpersist("multi") == 0);
    t = read_file(ss);
    CHECK(strstr(t, "Multi:") == NULL);
    CHECK(strstr(t, "Quote:") != NULL);
    free(t);
    CHECK(nirvana_assign_persist("Missing") == -ENOENT);
}

static void test_mime_default(void)
{
    char p[PATH_MAX * 2];
    snprintf(p, sizeof p, "%s/.config/mimeapps.list", home);
    write_file(p, "[Added Associations]\ntext/plain=added.desktop;\n"
                  "[Default Applications]\ntext/plain=editor.desktop;other.desktop\n"
                  "image/png=viewer.desktop\n");
    setenv("XDG_CONFIG_DIRS", root, 1);
    setenv("XDG_DATA_DIRS", root, 1);
    char out[256];
    CHECK(nirvana_mime_default("text/plain", out, sizeof out) == 0);
    CHECK_STR(out, "editor.desktop");
    CHECK(nirvana_mime_default("image/png", out, sizeof out) == 0);
    CHECK_STR(out, "viewer.desktop");
    CHECK(nirvana_mime_default("image/gif", out, sizeof out) == -ENOENT);
}

int main(void)
{
    setup();
    test_defaults();
    test_changes();
    test_translation();
    test_persist();
    test_mime_default();
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (!failures && system(cmd) != 0)
        fprintf(stderr, "could not remove %s\n", root);
    if (failures)
        fprintf(stderr, "%d failure(s); the test's files are in %s\n", failures, root);
    return failures ? 1 : 0;
}
