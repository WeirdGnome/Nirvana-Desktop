/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The tools (tools.h): small Zune windows like those of Wanderer/Tools, run modally.
*/
#include "tools.h"
#include "zune.h"

#include <FL/Fl.H>

#include <cstring>
#include <ctime>
#include <filesystem>
#include <grp.h>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace zune;

namespace wanderer {

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
void information(const std::string &path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return;
    bool done = false;
    std::string type = S_ISDIR(st.st_mode) ? "Drawer" : S_ISLNK(st.st_mode) ? "Link" : (st.st_mode & S_IXUSR) ? "Tool" : "Project";
    char date[64];
    struct tm tm;
    localtime_r(&st.st_mtime, &tm);
    strftime(date, sizeof date, "%d-%b-%y %H:%M:%S", &tm);
    struct passwd *pw = getpwuid(st.st_uid);
    struct group *gr = getgrgid(st.st_gid);
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
        top->end();
        auto *prot = new Group(Group::Horizontal, Frame_Group);
        prot->frame_title("Protection");
        struct Bit { const char *name; mode_t bit; };
        for (auto b : {Bit{"Read", S_IRUSR}, Bit{"Write", S_IWUSR}, Bit{"Execute", S_IXUSR}}) {
            auto *t = new Text(std::string("\033c") + b.name + ((st.st_mode & b.bit) ? " \xe2\x9c\x93" : " -"), Frame_Text);
            t->weight(100);
        }
        prot->end();
        auto *buttons = HGroup();
        HSpace();
        auto *ok = new Button("OK");
        buttons->end();
        ok->on_press = [&done] { done = true; };
    }
    root->end();
    auto *w = new Window(fs::path(path).filename().string() + " - Information", root);
    w->size(std::max(w->w(), 360), w->h());
    centre(w);
    run_modal(w, done);
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

}  // namespace wanderer
