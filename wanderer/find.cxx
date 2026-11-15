/*
    Copyright (C) 2016-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Find (workbench/system/find, findgroup_class.c): see find.h.
*/
#include "find.h"
#include "iconlist.h"
#include "tools.h"
#include "zune.h"

#include <FL/Fl.H>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fnmatch.h>
#include <memory>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using namespace zune;

namespace wanderer {

namespace {

struct Found {
    std::string path;
    long long size = 0;
    time_t mtime = 0;
    unsigned mode = 0;
    bool dir = false;
    std::string comment;
};

struct Search {
    Window *win = nullptr;
    String *path = nullptr, *pattern = nullptr, *contents = nullptr;
    List *list = nullptr;
    Text *status = nullptr;
    Button *start = nullptr, *stop = nullptr;
    std::vector<Found> found;
    std::unique_ptr<fs::recursive_directory_iterator> it;
    std::string glob, text;
    long long looked = 0;
    int sort_col = 0;
    bool reverse = false;
    std::function<void(const std::string &)> open, drawer;
};

Search *g_search = nullptr;

// a pattern of the original's (#? any characters, ? one) as a shell pattern
std::string to_glob(std::string p) {
    if (p.empty()) return "*";
    std::string out;
    for (size_t i = 0; i < p.size(); i++) {
        if (p[i] == '#' && i + 1 < p.size() && p[i + 1] == '?') { out += '*'; i++; }
        else out += p[i];
    }
    return out;
}

std::string attributes(const Found &f) {
    // as the original's protection bits (s p a r w e d): script, pure and archive are not
    // kept here; the owner's read, write, execute; d when the drawer it is in can be written
    std::string s = "---";
    s += f.mode & S_IRUSR ? 'r' : '-';
    s += f.mode & S_IWUSR ? 'w' : '-';
    s += f.mode & S_IXUSR ? 'e' : '-';
    s += access(fs::path(f.path).parent_path().c_str(), W_OK) == 0 ? 'd' : '-';
    return s;
}

void fill(Search *s) {
    // the columns: full path, size, attributes, date, time, comment (list_display_func)
    std::vector<Found> v = s->found;
    int c = s->sort_col;
    std::stable_sort(v.begin(), v.end(), [c](const Found &a, const Found &b) {
        switch (c) {
        case 1: return a.size < b.size;
        case 2: return a.mode < b.mode;
        case 3: case 4: return a.mtime < b.mtime;
        case 5: return a.comment < b.comment;
        default: return a.path < b.path;
        }
    });
    if (s->reverse) std::reverse(v.begin(), v.end());
    s->found = v;
    s->list->clear();
    for (auto &f : s->found) {
        char date[32], tim[16];
        struct tm tm;
        localtime_r(&f.mtime, &tm);
        strftime(date, sizeof date, "%d-%b-%y", &tm);
        strftime(tim, sizeof tim, "%H:%M:%S", &tm);
        List::Line l;
        l.cols = {to_volume_path(f.path), f.dir ? tr("Drawer") : std::to_string(f.size), attributes(f), date, tim, f.comment};
        s->list->add(l);
    }
    s->list->done();
}

void step(void *d);

void finish(Search *s, const std::string &why) {
    Fl::remove_idle(step, s);
    s->it.reset();
    fill(s);
    s->start->disabled(false);
    s->stop->disabled(true);
    s->status->contents(why.empty() ? trs("%s found.", std::to_string(s->found.size())) : why);
}

// a piece of the search at a time (the interface stays alive; Stop ends it)
void step(void *d) {
    auto *s = (Search *)d;
    if (!s->it) return;
    std::error_code ec;
    auto end = fs::recursive_directory_iterator();
    for (int n = 0; n < 200 && *s->it != end; n++, s->it->increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const fs::directory_entry &e = **s->it;
        s->looked++;
        std::string name = e.path().filename().string();
        if (fnmatch(s->glob.c_str(), name.c_str(), FNM_CASEFOLD) != 0) continue;
        struct stat st;
        if (lstat(e.path().c_str(), &st) != 0) continue;
        bool dir = S_ISDIR(st.st_mode);
        if (!s->text.empty() && (dir || !file_contains(e.path().string(), s->text))) continue;
        Found f;
        f.path = e.path().string();
        f.size = st.st_size;
        f.mtime = st.st_mtime;
        f.mode = st.st_mode;
        f.dir = dir;
        char buf[256];
        ssize_t cn = lgetxattr(f.path.c_str(), "user.comment", buf, sizeof buf - 1);
        if (cn > 0) f.comment.assign(buf, cn);
        s->found.push_back(f);
    }
    s->status->contents(trs("Searching... %s found", std::to_string(s->found.size())) + " (" +
                        std::to_string(s->looked) + ")");
    if (*s->it == end) finish(s, "");
}

void start(Search *s) {
    std::string dir = from_volume_path(s->path->value());
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) { s->status->contents(tr("The path is not a drawer.")); return; }
    s->found.clear();
    s->list->clear();
    s->list->done();
    s->looked = 0;
    s->glob = to_glob(s->pattern->value());
    s->text = s->contents->value();
    s->it = std::make_unique<fs::recursive_directory_iterator>(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) { s->status->contents(ec.message()); s->it.reset(); return; }
    s->start->disabled(true);
    s->stop->disabled(false);
    Fl::add_idle(step, s);
}

const Found *active(Search *s) {
    int a = s->list->active();
    return a >= 0 && a < (int)s->found.size() ? &s->found[a] : nullptr;
}

}  // namespace

void find_tool(const std::string &startdir, std::function<void(const std::string &)> open,
               std::function<void(const std::string &)> drawer) {
    if (g_search) {
        g_search->path->value(to_volume_path(startdir));
        g_search->win->show();
        return;
    }
    auto *s = new Search;
    g_search = s;
    s->open = std::move(open);
    s->drawer = std::move(drawer);
    auto *root = VGroup();
    {
        auto *g = new Group(Group::Vertical);
        g->columns(2);
        new Text("\033rPath:");
        auto *prow = HGroup();
        prow->spacing(0);
        s->path = new String(to_volume_path(startdir), 1024);
        s->path->weight(300);
        auto *pop = PopButton(Pop_Drawer);
        prow->end();
        new Text("\033rPattern:");
        s->pattern = new String("#?", 256);
        new Text("\033rContents:");
        s->contents = new String("", 256);
        g->end();
        pop->on_press = [s] {
            std::string p = from_volume_path(s->path->value()) + "/";
            if (file_request(tr("Find"), p)) {
                std::error_code ec;
                fs::path fp(p);
                s->path->value(to_volume_path(fs::is_directory(fp, ec) ? fp.string() : fp.parent_path().string()));
            }
        };
        auto *b = HGroup();
        HVSpace();
        s->start = new Button("Start");
        HVSpace();
        s->stop = new Button("Stop");
        HVSpace();
        b->end();
        s->list = new List();
        s->list->frame(Frame_InputList);
        s->list->columns({"Full Path", "Size", "Attributes", "Date", "Time", "Comment"},
                         {false, true, false, false, false, false});
        auto *b2 = HGroup();
        HVSpace();
        auto *bo = new Button("Open");
        HVSpace();
        auto *bv = new Button("View");
        HVSpace();
        auto *bd = new Button("Drawer");
        HVSpace();
        b2->end();
        s->status = new Text("\033l", Frame_Text);
        s->stop->disabled(true);
        s->start->on_press = [s] { start(s); };
        s->stop->on_press = [s] { finish(s, tr("Stopped.")); };
        s->pattern->on_acknowledge = [s] { if (!s->it) start(s); };
        s->contents->on_acknowledge = [s] { if (!s->it) start(s); };
        bo->on_press = [s] { if (auto *f = active(s)) s->open(f->path); };
        s->list->on_double = [s](int) { if (auto *f = active(s)) s->open(f->path); };
        bv->on_press = [s] { if (auto *f = active(s)) launch({"xdg-open", f->path}, fs::path(f->path).parent_path().string()); };
        bd->on_press = [s] { if (auto *f = active(s)) s->drawer(fs::path(f->path).parent_path().string()); };
        s->list->on_title = [s](int c) {
            // a title clicked: sorted by that column, again: the other way round
            if (s->sort_col == c) s->reverse = !s->reverse;
            else { s->sort_col = c; s->reverse = false; }
            fill(s);
        };
    }
    root->end();
    s->win = new Window("Find", root);
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    s->win->size(std::max(s->win->w(), sw / 2), std::max(s->win->h(), sh / 2));
    s->win->position(sx + (sw - s->win->w()) / 2, sy + (sh - s->win->h()) / 4);
    s->win->callback([](Fl_Widget *w, void *d) {
        auto *s = (Search *)d;
        Fl::remove_idle(step, s);
        w->hide();
        Fl::delete_widget(w);
        delete s;
        g_search = nullptr;
    }, s);
    s->win->show();
}

}  // namespace wanderer
