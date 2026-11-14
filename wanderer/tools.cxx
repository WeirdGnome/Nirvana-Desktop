/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The tools (tools.h): small Zune windows like those of Wanderer/Tools, run modally.
*/
#include "tools.h"
#include "zune.h"
#include "iconlist.h"

#include <FL/Fl.H>

#include <algorithm>
#include <cstring>
#include <functional>
#include <ctime>
#include <filesystem>
#include <grp.h>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <cerrno>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace zune;

namespace wanderer {

static std::string trash_files() { return trash_dir(); }

// Runs a window until `done` is set (or the window is closed)
static void run_modal(Window *w, bool &done) {
    w->set_modal();
    w->show();
    while (!done && w->shown()) Fl::wait();
    w->hide();
    Fl::delete_widget(w);
}

static void centre(Window *w) {
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    w->position(sx + (sw - w->w()) / 2, sy + (sh - w->h()) / 3);
}

// --------------------------------------------------------------------------------------------
int request(const std::string &title, const std::string &text, const std::string &buttons) {
    std::vector<std::string> labels;
    std::stringstream ss(buttons);
    std::string b;
    while (std::getline(ss, b, '|')) labels.push_back(b);
    int result = 0;
    bool done = false;
    auto *root = VGroup();
    {
        auto *g = new Group(Group::Vertical, Frame_Text);
        g->background(MPEN_BACKGROUND);
        new Text("\033c" + text);
        g->end();
        auto *row = HGroup();
        for (size_t i = 0; i < labels.size(); i++) {
            if (i) HSpace();
            auto *btn = new Button(labels[i]);
            // EasyRequest: the buttons count 1, 2, ... and the rightmost is 0
            int value = i + 1 == labels.size() ? 0 : (int)i + 1;
            btn->on_press = [&result, &done, value] { result = value; done = true; };
        }
        row->end();
    }
    root->end();
    auto *w = new Window(title, root);
    centre(w);
    run_modal(w, done);
    return result;
}

void launch(const std::vector<std::string> &argv, const std::string &dir) {
    if (argv.empty()) return;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() != 0) _exit(0);   // detached: the program is nobody's child
        if (!dir.empty() && chdir(dir.c_str()) != 0) {}
        std::vector<char *> a;
        for (auto &s : argv) a.push_back(const_cast<char *>(s.c_str()));
        a.push_back(nullptr);
        execvp(a[0], a.data());
        _exit(127);
    }
    if (pid > 0) {
        int st;
        waitpid(pid, &st, 0);
    }
}

// --------------------------------------------------------------------------------------------
// Execute Command: a command line, run in the drawer's directory
void execute_command(const std::string &dir) {
    bool done = false, run = false;
    auto *root = VGroup();
    auto *row = HGroup();
    new Text("Command:");
    auto *cmd = new String("", 1024);
    cmd->weight(300);
    row->end();
    auto *buttons = HGroup();
    auto *ok = new Button("Execute");
    HSpace();
    auto *cancel = new Button("Cancel");
    buttons->end();
    root->end();
    auto *w = new Window("Execute a file", root);
    w->size(std::max(w->w(), 420), w->h());
    ok->on_press = [&] { run = done = true; };
    cmd->on_acknowledge = [&] { run = done = true; };
    cancel->on_press = [&] { done = true; };
    centre(w);
    std::string command;
    w->set_modal();
    w->show();
    Fl::focus(cmd->input());
    while (!done && w->shown()) Fl::wait();
    command = cmd->value();
    w->hide();
    Fl::delete_widget(w);
    if (run && !command.empty()) launch({"/bin/sh", "-c", command}, dir);
}

// --------------------------------------------------------------------------------------------
// One line of text asked for: New Drawer, Rename
static bool ask_name(const std::string &title, const std::string &prompt, std::string &name, const std::string &okl) {
    bool done = false, ok = false;
    auto *root = VGroup();
    new Text("\033c" + prompt);
    auto *row = HGroup();
    new Text("Name:");
    auto *s = new String(name, 255);
    s->weight(300);
    row->end();
    auto *buttons = HGroup();
    auto *bok = new Button(okl);
    HSpace();
    auto *bc = new Button("Cancel");
    buttons->end();
    root->end();
    auto *w = new Window(title, root);
    w->size(std::max(w->w(), 380), w->h());
    bok->on_press = [&] { ok = done = true; };
    s->on_acknowledge = [&] { ok = done = true; };
    bc->on_press = [&] { done = true; };
    centre(w);
    w->set_modal();
    w->show();
    Fl::focus(s->input());
    s->input()->insert_position(0, (int)name.size());
    while (!done && w->shown()) Fl::wait();
    name = s->value();
    w->hide();
    Fl::delete_widget(w);
    return ok && !name.empty();
}

bool new_drawer(const std::string &dir, std::string &made) {
    std::string name = "Unnamed1";
    for (int i = 2; fs::exists(fs::path(dir) / name); i++) name = "Unnamed" + std::to_string(i);
    while (ask_name("New Drawer", "Enter the name of the new drawer:", name, "OK")) {
        std::error_code ec;
        fs::path p = fs::path(dir) / name;
        if (fs::exists(p, ec)) { request("New Drawer", "'" + name + "' already exists.", "OK"); continue; }
        if (fs::create_directory(p, ec)) { made = p.string(); return true; }
        request("New Drawer", "Could not create '" + name + "':\n" + ec.message(), "OK");
        return false;
    }
    return false;
}

bool rename_entry(const std::string &path, std::string &renamed) {
    fs::path p(path);
    std::string name = p.filename().string();
    while (ask_name("Rename", "Enter a new name for '" + name + "':", name, "Rename")) {
        if (name == p.filename().string()) return false;
        std::error_code ec;
        fs::path to = p.parent_path() / name;
        if (fs::exists(to, ec)) { request("Rename", "'" + name + "' already exists.", "OK"); continue; }
        fs::rename(p, to, ec);
        if (!ec) { renamed = to.string(); return true; }
        request("Rename", "Could not rename:\n" + ec.message(), "OK");
        return false;
    }
    return false;
}

// --------------------------------------------------------------------------------------------
// Information: the name, type, size, date, owner and the protection bits (as check marks)
// The default tools of projects (~/.config/wanderer/defaulttools: path TAB command), as a
// project's icon names the program that opens it
static std::string tools_file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/wanderer/defaulttools";
}

std::string default_tool(const std::string &path) {
    FILE *f = fopen(tools_file().c_str(), "r");
    if (!f) return "";
    char line[4096];
    std::string r;
    while (fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && l.back() == '\n') l.pop_back();
        auto t = l.find('\t');
        if (t != std::string::npos && l.substr(0, t) == path) r = l.substr(t + 1);
    }
    fclose(f);
    return r;
}

void set_default_tool(const std::string &path, const std::string &tool) {
    std::vector<std::string> keep;
    if (FILE *f = fopen(tools_file().c_str(), "r")) {
        char line[4096];
        while (fgets(line, sizeof line, f)) {
            std::string l(line);
            while (!l.empty() && l.back() == '\n') l.pop_back();
            auto t = l.find('\t');
            if (t != std::string::npos && l.substr(0, t) != path) keep.push_back(l);
        }
        fclose(f);
    }
    if (!tool.empty()) keep.push_back(path + "\t" + tool);
    std::error_code ec;
    fs::create_directories(fs::path(tools_file()).parent_path(), ec);
    if (FILE *f = fopen(tools_file().c_str(), "w")) {
        for (auto &l : keep) fprintf(f, "%s\n", l.c_str());
        fclose(f);
    }
}

// The comment of a file (the extended attribute user.comment, as the file systems of the
// system keep a comment with every file)
std::string file_comment(const std::string &path) {
    char buf[1024];
    ssize_t n = lgetxattr(path.c_str(), "user.comment", buf, sizeof buf - 1);
    return n > 0 ? std::string(buf, n) : "";
}

static bool set_file_comment(const std::string &path, const std::string &c, std::string &err) {
    int r = c.empty() ? lremovexattr(path.c_str(), "user.comment")
                      : lsetxattr(path.c_str(), "user.comment", c.data(), c.size(), 0);
    if (r != 0 && !(c.empty() && errno == ENODATA)) { err = strerror(errno); return false; }
    return true;
}

// Information: the name, type, size, date, owner; the protection (read, write, execute -
// for the owner, the group and the others), the comment, the default tool of a project;
// Save puts the changes into effect
void information(const std::string &path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return;
    bool done = false, save = false;
    bool is_dir = S_ISDIR(st.st_mode), is_link = S_ISLNK(st.st_mode);
    bool project = S_ISREG(st.st_mode) && !(st.st_mode & S_IXUSR);
    std::string type = is_dir ? "Drawer" : is_link ? "Link" : (st.st_mode & S_IXUSR) ? "Tool" : "Project";
    char date[64];
    struct tm tm;
    localtime_r(&st.st_mtime, &tm);
    strftime(date, sizeof date, "%d-%b-%y %H:%M:%S", &tm);
    struct passwd *pw = getpwuid(st.st_uid);
    struct group *gr = getgrgid(st.st_gid);
    Checkmark *bits[9];
    String *comment, *tool = nullptr;
    auto *root = VGroup();
    {
        auto *top = new Group(Group::Vertical, Frame_Group);
        top->columns(2);
        new Text("\033rName:");
        new Text(fs::path(path).filename().string(), Frame_Text);
        new Text("\033rType:");
        new Text(type, Frame_Text);
        new Text("\033rSize:");
        new Text(std::to_string((long long)st.st_size) + " bytes", Frame_Text);
        new Text("\033rLast changed:");
        new Text(date, Frame_Text);
        new Text("\033rOwner:");
        new Text(std::string(pw ? pw->pw_name : "?") + " / " + (gr ? gr->gr_name : "?"), Frame_Text);
        new Text("\033rComment:");
        comment = new String(file_comment(path), 512);
        if (project) {
            new Text("\033rDefault Tool:");
            tool = new String(default_tool(path), 1024);
        }
        top->end();
        auto *prot = new Group(Group::Vertical, Frame_Group);
        prot->frame_title("Protection");
        prot->columns(4);
        new Text("");
        new Text("\033cRead");
        new Text("\033cWrite");
        new Text("\033cExecute");
        static const mode_t m[9] = {S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP, S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH};
        const char *who[3] = {"\033rOwner", "\033rGroup", "\033rOthers"};
        for (int r = 0; r < 3; r++) {
            new Text(who[r]);
            for (int c = 0; c < 3; c++) {
                auto *row = HGroup();
                HSpace();
                bits[r * 3 + c] = new Checkmark(st.st_mode & m[r * 3 + c]);
                HSpace();
                row->end();
            }
        }
        prot->end();
        if (is_link) for (auto *b : bits) b->disabled(true);   // links have no protection of their own
        auto *buttons = HGroup();
        buttons->same_width(true);
        auto *bs = new Button("Save");
        HSpace();
        auto *bc = new Button("Cancel");
        buttons->end();
        bs->on_press = [&] { save = done = true; };
        bc->on_press = [&] { done = true; };
    }
    root->end();
    auto *w = new Window(fs::path(path).filename().string() + " - Information", root);
    w->size(std::max(w->w(), 380), w->h());
    centre(w);
    w->set_modal();
    w->show();
    while (!done && w->shown()) Fl::wait();
    if (save) {
        std::string errors, err;
        if (!is_link) {
            static const mode_t m[9] = {S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP, S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH};
            mode_t mode = st.st_mode & ~(mode_t)0777;
            for (int i = 0; i < 9; i++) if (bits[i]->on()) mode |= m[i];
            if ((mode & 07777) != (st.st_mode & 07777) && chmod(path.c_str(), mode & 07777) != 0)
                errors += std::string("Protection: ") + strerror(errno) + "\n";
        }
        if (comment->value() != file_comment(path) && !set_file_comment(path, comment->value(), err))
            errors += "Comment: " + err + "\n";
        if (tool) set_default_tool(path, tool->value());
        if (!errors.empty()) request("Information", "Not everything could be changed:\n" + errors, "OK");
    }
    w->hide();
    Fl::delete_widget(w);
}

// Disk Info: the volume's name, size, used, free
void disk_info(const std::string &volume) {
    struct statvfs sv;
    if (statvfs(volume.c_str(), &sv) != 0) return;
    unsigned long long total = (unsigned long long)sv.f_blocks * sv.f_frsize,
                       freeb = (unsigned long long)sv.f_bavail * sv.f_frsize, used = total - (unsigned long long)sv.f_bfree * sv.f_frsize;
    auto mb = [](unsigned long long b) { return std::to_string(b / (1024 * 1024)) + " MB"; };
    bool done = false;
    auto *root = VGroup();
    auto *g = new Group(Group::Vertical, Frame_Group);
    g->columns(2);
    new Text("\033rVolume:");
    new Text(volume, Frame_Text);
    new Text("\033rSize:");
    new Text(mb(total), Frame_Text);
    new Text("\033rUsed:");
    new Text(mb(used) + " (" + std::to_string(total ? used * 100 / total : 0) + "%)", Frame_Text);
    new Text("\033rFree:");
    new Text(mb(freeb), Frame_Text);
    new Text("\033rBlock size:");
    new Text(std::to_string(sv.f_frsize) + " bytes", Frame_Text);
    g->end();
    auto *buttons = HGroup();
    HSpace();
    auto *ok = new Button("OK");
    buttons->end();
    root->end();
    ok->on_press = [&done] { done = true; };
    auto *w = new Window("Disk Info", root);
    centre(w);
    run_modal(w, done);
}

// --------------------------------------------------------------------------------------------
bool move_to_trash(const std::string &path, std::string &error) {
    fs::path files = fs::path(trash_files());
    fs::path info = files.parent_path() / "info";
    std::error_code ec;
    fs::create_directories(files, ec);
    fs::create_directories(info, ec);
    std::string name = fs::path(path).filename().string(), base = name;
    for (int i = 2; fs::exists(files / name, ec) || fs::exists(info / (name + ".trashinfo"), ec); i++)
        name = base + "." + std::to_string(i);
    char date[32];
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &tm);
    {
        FILE *f = fopen((info / (name + ".trashinfo")).c_str(), "w");
        if (f) {
            // the path as a URI path: unsafe characters escaped
            std::string esc;
            for (unsigned char c : fs::absolute(path).string()) {
                if (isalnum(c) || strchr("/-_.~", c)) esc += (char)c;
                else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); esc += b; }
            }
            fprintf(f, "[Trash Info]\nPath=%s\nDeletionDate=%s\n", esc.c_str(), date);
            fclose(f);
        }
    }
    fs::rename(path, files / name, ec);
    if (ec) {
        // another file system: copied, then removed
        fs::copy(path, files / name, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (!ec) fs::remove_all(path, ec);
    }
    if (ec) {
        fs::remove(info / (name + ".trashinfo"));
        error = ec.message();
        return false;
    }
    return true;
}

void empty_trash() {
    fs::path files = fs::path(trash_files());
    std::error_code ec;
    for (auto &dir : {files, files.parent_path() / "info"})
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            std::error_code e2;
            fs::remove_all(it->path(), e2);
        }
}

// --------------------------------------------------------------------------------------------
bool file_request(const std::string &title, std::string &path) {
    std::error_code ec;
    std::string dir = path.empty() ? std::string(getenv("HOME") ? getenv("HOME") : "/") : path;
    std::string file;
    if (!fs::is_directory(dir, ec)) {
        file = fs::path(dir).filename().string();
        dir = fs::path(dir).parent_path().string();
        if (!fs::is_directory(dir, ec)) dir = getenv("HOME") ? getenv("HOME") : "/";
    }
    bool done = false, ok = false, volumes = false;
    auto *root = VGroup();
    auto *list = new List();
    list->weight(300);
    auto *g = new Group(Group::Vertical);
    g->columns(2);
    new Text("\033rDrawer");
    auto *sdir = new String(dir, 1024);
    new Text("\033rFile");
    auto *sfile = new String(file, 255);
    g->end();
    auto *buttons = HGroup();
    buttons->same_width(true);
    auto *bok = new Button("OK");
    auto *bvol = new Button("Volumes");
    auto *bpar = new Button("Parent");
    auto *bc = new Button("Cancel");
    buttons->end();
    root->end();

    std::vector<std::string> paths;   // the path of every line
    std::function<void(const std::string &)> read_dir = [&](const std::string &d) {
        volumes = false;
        list->clear();
        paths.clear();
        std::vector<std::pair<std::string, bool>> items;
        std::error_code e2;
        for (auto it = fs::directory_iterator(d, e2); !e2 && it != fs::directory_iterator(); it.increment(e2)) {
            std::string n = it->path().filename().string();
            if (!n.empty() && n[0] == '.') continue;
            items.push_back({n, it->is_directory(e2)});
        }
        std::sort(items.begin(), items.end(), [](auto &a, auto &b) {
            if (a.second != b.second) return a.second;
            return strcasecmp(a.first.c_str(), b.first.c_str()) < 0;
        });
        for (auto &[n, isdir] : items) {
            std::string full = (fs::path(d) / n).string();
            std::string info = "Drawer";
            if (!isdir) {
                std::error_code e3;
                auto sz = fs::file_size(full, e3);
                info = e3 ? "" : std::to_string(sz);
            }
            list->add({n, info, isdir});
            paths.push_back(full);
        }
        list->done();
        sdir->value(d);
    };
    auto show_volumes = [&] {
        volumes = true;
        list->clear();
        paths.clear();
        auto add = [&](const std::string &p, const std::string &n) { list->add({n + ":", "Volume", true}); paths.push_back(p); };
        add("/", "System");
        if (getenv("HOME")) add(getenv("HOME"), "Home");
        for (const char *base : {"/media", "/mnt", "/run/media"}) {
            std::error_code e2;
            for (auto it = fs::recursive_directory_iterator(base, e2); !e2 && it != fs::recursive_directory_iterator(); it.increment(e2)) {
                if (it.depth() > 1) { it.disable_recursion_pending(); continue; }
                struct stat a, b;
                if (stat(it->path().c_str(), &a) == 0 && stat(it->path().parent_path().c_str(), &b) == 0 && a.st_dev != b.st_dev)
                    add(it->path().string(), it->path().filename().string());
            }
        }
        list->done();
    };
    list->on_active = [&](int i) {
        if (i < 0 || i >= (int)paths.size()) return;
        if (!list->lines()[i].highlight) sfile->value(list->lines()[i].text);
    };
    list->on_double = [&](int i) {
        if (i < 0 || i >= (int)paths.size()) return;
        if (list->lines()[i].highlight) { read_dir(paths[i]); return; }   // a drawer or a volume: into it
        sfile->value(list->lines()[i].text);
        ok = done = true;
    };
    sdir->on_acknowledge = [&] { std::error_code e2; if (fs::is_directory(sdir->value(), e2)) read_dir(sdir->value()); };
    sfile->on_acknowledge = [&] { ok = done = true; };
    bok->on_press = [&] { ok = done = true; };
    bc->on_press = [&] { done = true; };
    bvol->on_press = [&] { show_volumes(); };
    bpar->on_press = [&] {
        fs::path p = fs::path(sdir->value());
        if (volumes) return;
        if (p.has_parent_path() && p.parent_path() != p) read_dir(p.parent_path().string());
        else show_volumes();
    };
    auto *w = new Window(title, root);
    w->size(std::max(w->w(), 380), std::max(w->h(), 420));
    centre(w);
    read_dir(dir);
    w->set_modal();
    w->show();
    while (!done && w->shown()) Fl::wait();
    std::string chosen = (fs::path(sdir->value()) / sfile->value()).string();
    if (std::string(sfile->value()).empty()) chosen = sdir->value();
    w->hide();
    Fl::delete_widget(w);
    if (ok) path = chosen;
    return ok;
}

bool ask_find(std::string &pattern) {
    bool done = false, ok = false;
    auto *root = VGroup();
    new Text("\033cFind the files and drawers whose names match\n(wildcards: * and ?)");
    auto *row = HGroup();
    new Text("Name:");
    auto *s = new String(pattern, 255);
    s->weight(300);
    row->end();
    auto *buttons = HGroup();
    auto *bok = new Button("Find");
    HSpace();
    auto *bc = new Button("Cancel");
    buttons->end();
    root->end();
    auto *w = new Window("Find", root);
    w->size(std::max(w->w(), 360), w->h());
    bok->on_press = [&] { ok = done = true; };
    s->on_acknowledge = [&] { ok = done = true; };
    bc->on_press = [&] { done = true; };
    centre(w);
    w->set_modal();
    w->show();
    Fl::focus(s->input());
    while (!done && w->shown()) Fl::wait();
    pattern = s->value();
    w->hide();
    Fl::delete_widget(w);
    if (ok && !pattern.empty() && pattern.find_first_of("*?") == std::string::npos) pattern = "*" + pattern + "*";
    return ok && !pattern.empty();
}

}  // namespace wanderer
