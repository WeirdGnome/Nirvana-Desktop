/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The tools (tools.h): small Lune windows like those of Wanderer/Tools, run modally.
*/
#include "tools.h"
#include "lune.h"
#include "iconlist.h"
#include "icons.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_RGB_Image.H>

#include <algorithm>
#include <cstring>
#include <functional>
#include <ctime>
#include <filesystem>
#include <map>
#include <grp.h>
#include <pwd.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/xattr.h>
#include <cerrno>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

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
// The requester's window: the keys of a system requester - Super+V the leftmost button
// (positive), Super+B the rightmost (negative); Escape too (negative). Return does not
// answer: a requester asking to delete or format is not answered by a stray key
namespace {
class RequesterWindow : public Window {
public:
    RequesterWindow(const std::string &title, Area *root, std::function<void(bool)> key)
        : Window(title, root), key_(std::move(key)) { requester(true); }   // MUII_RequesterBack
    int handle(int e) override {
        if (e == FL_KEYBOARD || e == FL_SHORTCUT) {
            int k = Fl::event_key();
            bool super = Fl::event_state() & FL_META;
            if (super && k == 'v') { key_(true); return 1; }
            if ((super && k == 'b') || k == FL_Escape) { key_(false); return 1; }
        }
        return Window::handle(e);
    }

private:
    std::function<void(bool)> key_;
};
}  // namespace

int request(const std::string &title, const std::string &text, const std::string &buttons) {
    std::vector<std::string> labels;
    std::stringstream ss(buttons);
    std::string b;
    while (std::getline(ss, b, '|')) labels.push_back(tr(b));
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
    int n = (int)labels.size();
    auto *w = new RequesterWindow(title, root, [&result, &done, n](bool positive) {
        result = positive && n > 1 ? 1 : 0;
        done = true;
    });
    // (EasyRequest: the requester is wide enough for its title and the window's gadgets)
    fl_font(prefs().font, prefs().font_size);
    int tw = (int)fl_width(tr(title).c_str()) + 3 * 24 + 16;
    if (w->w() < tw) w->size(tw, w->h());
    centre(w);
    run_modal(w, done);
    return result;
}

bool launch(const std::vector<std::string> &argv, const std::string &dir) {
    if (argv.empty()) return false;
    // a pipe closed on exec: a byte through it - the program could not be started
    int fd[2];
    if (pipe2(fd, O_CLOEXEC) != 0) fd[0] = fd[1] = -1;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() != 0) _exit(0);   // detached: the program is nobody's child
        if (fd[0] >= 0) close(fd[0]);
        if (!dir.empty() && chdir(dir.c_str()) != 0) {}
        std::vector<char *> a;
        for (auto &s : argv) a.push_back(const_cast<char *>(s.c_str()));
        a.push_back(nullptr);
        execvp(a[0], a.data());
        if (fd[1] >= 0 && write(fd[1], "x", 1) != 1) {}
        _exit(127);
    }
    if (fd[1] >= 0) close(fd[1]);
    bool ok = pid > 0;
    if (pid > 0) {
        int st;
        waitpid(pid, &st, 0);
    }
    if (fd[0] >= 0) {
        char c;
        if (ok && read(fd[0], &c, 1) == 1) ok = false;
        close(fd[0]);
    }
    return ok;
}

// --------------------------------------------------------------------------------------------
// The output window: what a command started by Execute Command prints opens a window of its
// own (as the system's console opened for a command's output) - only when there is output
namespace {

class TextView : public Area {
public:
    TextView() : Area(Frame_ReadList) {
        background(MPEN_SHINE);
        begin();
        buf_ = new Fl_Text_Buffer;
        view_ = new Fl_Text_Display(0, 0, 10, 10);
        view_->buffer(buf_);
        view_->box(FL_FLAT_BOX);
        view_->color(pens()[MPEN_SHINE]);
        view_->textcolor(pens()[MPEN_TEXT]);
        view_->textfont(prefs().font_fixed);
        view_->textsize(prefs().font_size);
        view_->scrollbar_width(14);
        end();
    }
    void append(const char *t, int n) {
        buf_->append(std::string(t, n).c_str());
        view_->insert_position(buf_->length());
        view_->show_insert_position();
    }
    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override {
        minw = 200; defw = 560; maxw = MAXMAX;
        minh = 80; defh = 260; maxh = MAXMAX;
    }
    void resize(int X, int Y, int W, int H) override {
        Fl_Widget::resize(X, Y, W, H);
        view_->resize(mleft(), mtop(), mwidth(), mheight());
    }

private:
    Fl_Text_Buffer *buf_;
    Fl_Text_Display *view_;
};

struct OutputRun {
    int fd = -1;
    std::string title, dir;
    Window *win = nullptr;
    TextView *text = nullptr;
};

void output_window(OutputRun *r) {
    auto *root = VGroup();
    r->text = new TextView();
    r->text->weight(300);
    auto *row = HGroup();
    HSpace();
    auto *close = new Button("Close");
    row->end();
    root->end();
    r->win = new Window(tr("Output:") + " " + r->title, root);
    Window *w = r->win;
    close->on_press = [w] { w->hide(); };
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    w->position(sx + 40, sy + sh - w->h() - 60);
    w->show();
}

void output_ready(int fd, void *d) {
    auto *r = (OutputRun *)d;
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof buf);
    if (n > 0) {
        if (!r->win || !r->win->shown()) {
            if (r->win) Fl::delete_widget(r->win);
            output_window(r);
        }
        r->text->append(buf, (int)n);
        return;
    }
    // the end of the output (the command and whatever it left running have closed it)
    Fl::remove_fd(fd);
    close(fd);
    if (!r->win) delete r;   // nothing printed: no window
    // (with a window: it stays until closed; the run is kept with it)
}

}  // namespace

void run_with_output(const std::string &command, const std::string &dir, const std::string &title) {
    int p[2];
    if (pipe(p) != 0) { launch({"/bin/sh", "-c", command}, dir); return; }
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() != 0) _exit(0);   // detached (nobody's child), its output through the pipe
        close(p[0]);
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) { dup2(nul, 0); close(nul); }
        dup2(p[1], 1);
        dup2(p[1], 2);
        close(p[1]);
        if (!dir.empty() && chdir(dir.c_str()) != 0) {}
        execl("/bin/sh", "sh", "-c", command.c_str(), (char *)nullptr);
        _exit(127);
    }
    close(p[1]);
    if (pid > 0) { int st; waitpid(pid, &st, 0); }
    auto *r = new OutputRun;
    r->fd = p[0];
    r->title = title.empty() ? command : title;
    r->dir = dir;
    fcntl(r->fd, F_SETFD, FD_CLOEXEC);
    Fl::add_fd(r->fd, FL_READ, output_ready, r);
}

// Words of a command line that are volume paths or assigns ("Docs:list.txt", "RAM:") become
// the drawers they name (quoted for the shell); everything else stays as it was typed
std::string volume_paths_in_command(const std::string &cmd) {
    std::string out, word;
    char quote = 0;
    auto flush = [&] {
        if (word.empty()) return;
        auto colon = word.find(':');
        std::string real = colon != std::string::npos && colon > 0 && word.find('/') > colon ? from_volume_path(word) : word;
        if (real != word) {
            std::string q = "'";
            for (char c : real) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
            out += q + "'";
        } else out += word;
        word.clear();
    };
    for (char c : cmd) {
        if (quote) { out += c; if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { flush(); quote = c; out += c; continue; }
        if (isspace((unsigned char)c) || strchr(";|&<>()", c)) { flush(); out += c; continue; }
        word += c;
    }
    flush();
    return out;
}

// --------------------------------------------------------------------------------------------
// Execute Command: a command line, run in the drawer's directory
bool has_opener(const std::string &path) {
    auto out = [](const std::vector<std::string> &argv) {
        std::string cmd;
        for (auto &a : argv) {
            cmd += "'";
            for (char c : a) cmd += c == '\'' ? std::string("'\\''") : std::string(1, c);
            cmd += "' ";
        }
        cmd += "2>/dev/null";
        std::string r;
        if (FILE *p = popen(cmd.c_str(), "r")) {
            char b[512];
            while (fgets(b, sizeof b, p)) r += b;
            pclose(p);
        }
        while (!r.empty() && isspace((unsigned char)r.back())) r.pop_back();
        return r;
    };
    if (system("command -v xdg-mime >/dev/null 2>&1") != 0) return true;   // no database to ask: xdg-open tries
    std::string type = out({"xdg-mime", "query", "filetype", path});
    if (type.empty()) return true;
    return !out({"xdg-mime", "query", "default", type}).empty();
}

void execute_command(const std::string &dir, const std::string &initial) {
    static std::string last;   // the command given last time is offered again
    bool done = false, run = false;
    // executecommand.c: the label above the string with its Popasl; the buttons together at
    // the right, of one size
    auto *root = VGroup();
    auto *top = VGroup();
    new Text("\033lEnter Command and its Arguments:");
    auto *row = HGroup();
    row->spacing(0);
    auto *cmd = new String(initial.empty() ? last : initial, 1024);
    if (!initial.empty()) cmd->input()->insert_position(0);   // the program's name goes before the file's
    cmd->weight(300);
    auto *pop = PopButton(Pop_File);
    row->end();
    top->end();
    pop->on_press = [cmd, dir] {
        std::string p = cmd->value();
        if (p.empty()) p = dir + "/";
        if (file_request(tr("Execute a File"), p)) cmd->value(p);
    };
    auto *brow = HGroup();
    brow->spacing(0);
    brow->inner(0, 0, 0, 0);
    HVSpace();
    auto *buttons = HGroup();
    buttons->same_width(true);
    buttons->weight(0);
    auto *ok = new Button("Execute");
    auto *cancel = new Button("Cancel");
    buttons->end();
    brow->end();
    root->end();
    auto *w = new Window("Execute Command", root);
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
    if (run && !command.empty()) { last = command; run_with_output(volume_paths_in_command(command), dir, command); }
}

// --------------------------------------------------------------------------------------------
// One line of text asked for: New Drawer, Rename
// a name the drawers can hold: not empty, no "/" and no ":" (the volume paths use it), as
// WBRename's illegal_chars
static bool name_ok(const std::string &title, const std::string &name) {
    if (!name.empty() && name.find_first_of("/:") == std::string::npos && name != "." && name != "..") return true;
    request(title, trs("'%s' contains illegal characters (/ or :).", name), "OK");
    return false;
}

static bool ask_name(const std::string &title, const std::string &prompt, std::string &name, const std::string &okl) {
    // WBRename / WBNewDrawer: the line, Name: and the string in a group frame, a bar, the buttons
    bool done = false, ok = false;
    auto *root = new Group(Group::Vertical, Frame_Group);
    auto *top = HGroup();
    HVSpace();
    new Text("\033r" + prompt);
    top->end();
    auto *row = HGroup();
    new Text("Name:");
    auto *s = new String(name, 255);
    s->weight(300);
    row->end();
    HBar();
    auto *buttons = HGroup();
    buttons->same_width(true);
    auto *bok = new Button(okl);
    auto *bc = new Button("Cancel");
    buttons->end();
    root->end();
    auto *w = new Window(title, root);
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    w->size(std::max(w->w(), sw / 3), w->h());   // MUIV_Window_Width_Visible(33)
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
        if (!name_ok("New Drawer", name)) continue;
        std::error_code ec;
        fs::path p = fs::path(dir) / name;
        if (fs::exists(p, ec)) { request("New Drawer", trs("'%s' already exists.", name), "OK"); continue; }
        if (fs::create_directory(p, ec)) { made = p.string(); return true; }
        request("New Drawer", trs("Could not create '%s':", name) + "\n" + ec.message(), "OK");
        return false;
    }
    return false;
}

bool rename_entry(const std::string &path, std::string &renamed) {
    fs::path p(path);
    std::string name = p.filename().string();
    while (ask_name("Rename", trs("Enter a new name for '%s':", name), name, "Rename")) {
        if (name == p.filename().string()) return false;
        if (!name_ok("Rename", name)) continue;
        std::error_code ec;
        fs::path to = p.parent_path() / name;
        if (fs::exists(to, ec)) { request("Rename", trs("'%s' already exists.", name), "OK"); continue; }
        fs::rename(p, to, ec);
        if (!ec) {
            // its icon takes the new name too
            std::error_code ie;
            if (fs::is_regular_file(path + ".info", ie)) fs::rename(path + ".info", to.string() + ".info", ie);
            renamed = to.string();
            return true;
        }
        request("Rename", tr("Could not rename:") + "\n" + ec.message(), "OK");
        return false;
    }
    return false;
}

// --------------------------------------------------------------------------------------------
// Information: the name, type, size, date, owner and the protection bits (as check marks)
// The default tools of projects (~/.config/rambler/defaulttools: path TAB command), as a
// project's icon names the program that opens it
static std::string tools_file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/rambler/defaulttools";
}

std::string icon_default_tool(const std::string &path);

std::string default_tool(const std::string &path) {
    FILE *f = fopen(tools_file().c_str(), "r");
    if (!f) return icon_default_tool(path);
    char line[4096];
    std::string r;
    while (fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && l.back() == '\n') l.pop_back();
        auto t = l.find('\t');
        if (t != std::string::npos && l.substr(0, t) == path) r = l.substr(t + 1);
    }
    fclose(f);
    return r.empty() ? icon_default_tool(path) : r;
}

// the default tool of the project's icon file (do_DefaultTool), when it names a program
// there is here (an absolute path, or a name found in PATH; the original system's paths
// such as "SYS:Utilities/..." are not)
std::string icon_default_tool(const std::string &path) {
    auto o = get_disk_object(path);
    if (!o || o->default_tool.empty()) return "";
    std::string t = o->default_tool, prog = t.substr(0, t.find(' '));
    std::error_code ec;
    if (prog.find(':') != std::string::npos) return "";
    if (prog[0] == '/') return access(prog.c_str(), X_OK) == 0 ? t : "";
    const char *pathenv = getenv("PATH");
    std::string dirs = pathenv ? pathenv : "/usr/bin:/bin";
    for (size_t a = 0; a < dirs.size();) {
        size_t b = dirs.find(':', a);
        if (b == std::string::npos) b = dirs.size();
        std::string cand = dirs.substr(a, b - a) + "/" + prog;
        if (b > a && access(cand.c_str(), X_OK) == 0) return t;
        a = b + 1;
    }
    return "";
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
// The tool types of a file (the extended attribute user.tooltypes, one a line): the
// settings a tool reads from its icon
std::vector<std::string> tool_types(const std::string &path) {
    std::vector<std::string> v;
    char buf[8192];
    ssize_t n = lgetxattr(path.c_str(), "user.tooltypes", buf, sizeof buf);
    if (n <= 0) {
        // none set here: the icon file's (do_ToolTypes)
        if (auto o = get_disk_object(path)) return o->tool_types;
        return v;
    }
    std::string all(buf, n), line;
    std::istringstream in(all);
    while (std::getline(in, line)) if (!line.empty()) v.push_back(line);
    return v;
}

static bool set_tool_types(const std::string &path, const std::vector<std::string> &v, std::string &err) {
    std::string all;
    for (auto &t : v) if (!t.empty()) all += t + "\n";
    int r = all.empty() ? lremovexattr(path.c_str(), "user.tooltypes")
                        : lsetxattr(path.c_str(), "user.tooltypes", all.data(), all.size(), 0);
    if (r != 0 && !(all.empty() && errno == ENODATA)) { err = strerror(errno); return false; }
    return true;
}

// The version string of a file: "$VER: name 1.2 (1.1.2026)" in its first 32 MB, read in
// pieces (as the Version command looks for it)
static std::string file_version(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return "";
    const size_t piece = 1 << 20, keep = 160;
    std::string buf, v;
    std::vector<char> b(piece);
    for (size_t total = 0; total < (32u << 20);) {
        size_t n = fread(b.data(), 1, piece, f);
        if (n == 0) break;
        total += n;
        buf.append(b.data(), n);
        size_t at = buf.find("$VER: ");
        if (at != std::string::npos && buf.size() - at > keep - 1) {
            for (size_t i = at + 6; i < buf.size() && v.size() < 120; i++) {
                unsigned char c = buf[i];
                if (c == 0 || c == '\n' || c == '\r') break;
                v += (char)c;
            }
            break;
        }
        if (at == std::string::npos && buf.size() > keep) buf.erase(0, buf.size() - keep);
    }
    fclose(f);
    // "Rambler 0.8.0 (X11)": blanks after the tag tolerated
    size_t s = v.find_first_not_of(' ');
    return s == std::string::npos ? "" : v.substr(s);
}

std::function<void(const std::string &)> open_drawer_hook;

// Tools/Info: the icon and the name above a register - Information (type, date, version,
// size, path, default tool, comment), Protection, Tool Types; Save puts the changes into effect
void information(const std::string &path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return;
    bool done = false, save = false;
    bool is_dir = S_ISDIR(st.st_mode), is_link = S_ISLNK(st.st_mode);
    bool project = S_ISREG(st.st_mode) && !(st.st_mode & S_IXUSR);
    bool tool_file = S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR);
    std::string type = is_dir ? "Drawer" : is_link ? "Link" : tool_file ? "Tool" : "Project";
    char date[64];
    struct tm tm;
    localtime_r(&st.st_mtime, &tm);
    strftime(date, sizeof date, "%d-%b-%y %H:%M:%S", &tm);
    struct passwd *pw = getpwuid(st.st_uid);
    struct group *gr = getgrgid(st.st_gid);
    Checkmark *bits[9];
    String *comment, *tool = nullptr, *tt_string;
    std::string tool_shown;   // the default tool the field showed
    List *tt_list;
    std::vector<std::string> types = tool_types(path);
    int tt_active = -1;
    std::string parent = fs::path(path).parent_path().string();
    IconKind kind = icon_kind(path, is_dir, false, tool_file);
    auto *root = VGroup();
    {
        auto *head = HGroup();
        // the file's own icon (its .info, an AppImage's), else the default one; clicking it
        // shows the selected image (a dual icon's second one)
        auto dobj = get_disk_object(path);
        int iw = dobj ? std::max(ICON_W, dobj->width) : ICON_W, ih = dobj ? std::max(ICON_H, dobj->height) : ICON_H;
        auto *img = new ImageButton(iw, ih, [kind, dobj](int x, int y, int w, int h, bool sel) {
            if (dobj) {
                if (Fl_RGB_Image *i = dobj->image(sel)) i->draw(x + (w - dobj->width) / 2, y + (h - dobj->height) / 2);
            } else draw_icon(kind, x, y, sel);
        });
        img->frame(Frame_None);
        img->input_mode(Input_Toggle);
        auto *name = new Text("\033l" + fs::path(path).filename().string(), Frame_Text);
        name->weight(300);
        head->end();

        auto *reg = new Register({"Information", "Protection", "Tool Types"});
        // -- Information
        auto *info = VGroup();
        {
            auto *cols = new Group(Group::Vertical);
            cols->columns(2);
            new Text("\033rType:");
            new Text("\033r" + tr(type), Frame_Text);
            new Text("\033rDate:");
            new Text(std::string("\033r") + date, Frame_Text);
            new Text("\033rVersion:");
            std::string ver = S_ISREG(st.st_mode) ? file_version(path) : "";
            new Text("\033r" + (ver.empty() ? tr("N/A") : ver), Frame_Text);
            new Text("\033rSize:");
            new Text("\033r" + (is_dir ? tr("N/A") : std::to_string((long long)st.st_size) + " " + tr("bytes")), Frame_Text);
            new Text("\033rPath:");
            auto *prow = HGroup();
            auto *ps = new String(to_volume_path(parent), 1024);
            ps->weight(150);
            ps->input()->readonly(1);
            auto *open = new Button("Open");
            prow->end();
            open->on_press = [parent] { if (open_drawer_hook) open_drawer_hook(parent); };
            cols->end();
            HVSpace();
            if (project) {
                auto *trow = HGroup();
                new Text("Default Tool:");
                tool_shown = default_tool(path);
                tool = new String(tool_shown, 1024);
                tool->weight(300);
                auto *pick = PopButton(Pop_File);   // a Popasl
                trow->end();
                String *t = tool;
                pick->on_press = [t] { std::string p = t->value(); if (file_request(tr("Default Tool"), p)) t->value(p); };
            }
            auto *crow = HGroup();
            new Text("Comment:");
            comment = new String(file_comment(path), 512);
            comment->weight(300);
            crow->end();
        }
        info->end();
        // -- Protection: read, write, execute for the owner, the group, the others
        auto *protp = HGroup();
        {
            HVSpace();
            auto *pv = VGroup();
            HVSpace();
            auto *prot = new Group(Group::Vertical);
            prot->columns(4);
            new Text("");
            new Text("\033cRead");
            new Text("\033cWrite");
            new Text("\033cExecute");
            static const mode_t m[9] = {S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP, S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH};
            const char *who[3] = {"\033rOwner", "\033rGroup", "\033rOthers"};
            for (int r = 0; r < 3; r++) {
                new Text(who[r]);
                for (int c = 0; c < 3; c++) bits[r * 3 + c] = new Checkmark(st.st_mode & m[r * 3 + c]);
            }
            prot->end();
            new Text("\033c" + std::string(pw ? pw->pw_name : "?") + " / " + (gr ? gr->gr_name : "?"));
            HVSpace();
            pv->end();
            HVSpace();
        }
        protp->end();
        if (is_link) for (auto *b : bits) b->disabled(true);   // links have no protection of their own
        // -- Tool Types: the list, the string to edit the active one, New / Delete
        auto *ttp = VGroup();
        {
            auto *lg = VGroup();
            lg->spacing(0);
            tt_list = new List();
            tt_string = new String("", 1024);
            tt_string->disabled(true);
            lg->end();
            auto *bg = HGroup();
            bg->same_width(true);
            auto *newk = new Button("New");
            auto *delk = new Button("Delete");
            bg->end();
            auto refill = [tt_list, &types] {
                tt_list->clear();
                for (auto &t : types) tt_list->add({t});
                tt_list->done();
            };
            refill();
            tt_list->on_active = [&, tt_string](int i) {
                tt_active = i;
                tt_string->disabled(false);
                tt_string->value(i >= 0 && i < (int)types.size() ? types[i] : "");
                Fl::focus(tt_string->input());
            };
            tt_string->on_acknowledge = [&, tt_list, tt_string, refill] {
                if (tt_active < 0 || tt_active >= (int)types.size()) return;
                types[tt_active] = tt_string->value();
                int a = tt_active;
                refill();
                tt_list->active(a);
            };
            newk->on_press = [&, tt_list, tt_string, refill] {
                types.push_back("");
                refill();
                tt_active = (int)types.size() - 1;
                tt_list->active(tt_active);
                tt_string->disabled(false);
                tt_string->value("");
                Fl::focus(tt_string->input());
            };
            delk->on_press = [&, tt_list, tt_string, refill] {
                if (tt_active < 0 || tt_active >= (int)types.size()) return;
                types.erase(types.begin() + tt_active);
                tt_active = -1;
                refill();
                tt_string->value("");
                tt_string->disabled(true);
            };
        }
        ttp->end();
        reg->end();

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
    auto *w = new Window(fs::path(path).filename().string() + " - " + tr("Information"), root);
    w->size(std::max(w->w(), 380), w->h());
    centre(w);
    w->set_modal();
    w->show();
    while (!done && w->shown()) Fl::wait();
    if (save) {
        // an edit not yet acknowledged counts too
        if (tt_active >= 0 && tt_active < (int)types.size()) types[tt_active] = tt_string->value();
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
        // a PNG icon file of its own: the tool types and the default tool go into it
        // (PutDiskObject); the other kinds Rambler cannot write - the file's attribute and
        // Rambler's list keep them, as before
        bool in_icon = false;
        {
            std::string info = path + ".info";
            char magic[4] = {0};
            if (FILE *f = fopen(info.c_str(), "rb")) { (void)!fread(magic, 1, 4, f); fclose(f); }
            if (!memcmp(magic, "\x89PNG", 4))
                if (auto o = read_icon_file(info)) {
                    DiskObject n = *o;
                    n.tool_types = types;
                    // (the default tool as shown: one of the original system's paths, not shown
                    // in the field, stays unless the field was changed)
                    if (tool && tool->value() != tool_shown) n.default_tool = tool->value();
                    if (put_png_icon(info, n)) {
                        in_icon = true;
                        std::string e2;
                        set_tool_types(path, {}, e2);   // (none kept beside it any more)
                        set_default_tool(path, "");
                    } else errors += tr("The icon file could not be written.") + "\n";
                }
        }
        if (!in_icon && types != tool_types(path) && !set_tool_types(path, types, err))
            errors += "Tool Types: " + err + "\n";
        if (!in_icon && tool) set_default_tool(path, tool->value());
        if (!errors.empty()) request("Information", tr("Not everything could be changed:") + "\n" + errors, "OK");
    }
    w->hide();
    Fl::delete_widget(w);
}

// About Rambler (wanderer_menufunc_wanderer_about): the icon, the name and version, the
// copyright; the classes Rambler is made of. One window, closed by its close gadget.
static Window *g_about = nullptr;

void about_window() {
    if (g_about) { g_about->show(); return; }
    auto *root = VGroup();
    {
        auto *head = HGroup();
        auto *img = new ImageButton(ICON_W, ICON_H, [](int x, int y, int, int, bool sel) { draw_icon(IconKind::Harddisk, x, y, sel); });
        img->frame(Frame_None);
        img->input_mode(Input_Toggle);
        auto *tg = VGroup();
        new Text("\033c\033bRambler " RAMBLER_VERSION);
        new Text("\033c\302\251 The AROS Dev Team");
        new Text("\033cX11 port: Rambler X11 contributors");
        tg->end();
        head->end();
        auto *cl = new Group(Group::Vertical, Frame_Group);
        new Text("\033bInternal Classes:");
        HVSpace();
        auto *c1 = new Group(Group::Vertical);
        c1->columns(2);
        for (const char *n : {"IconWindow", "IconWindowVolumeList", "IconWindowDrawerList"}) { new Text(n); new Text(""); }
        c1->end();
        HVSpace();
        new Text("\033bExternal Classes:");
        HVSpace();
        auto *c2 = new Group(Group::Vertical);
        c2->columns(2);
        for (const char *n : {"IconList", "IconListview", "IconVolumeList", "IconDrawerList"}) { new Text(n); new Text(""); }
        c2->end();
        HVSpace();
        cl->end();
    }
    root->end();
    auto *w = new Window("About Rambler...", root);
    g_about = w;
    w->callback([](Fl_Widget *win, void *) { win->hide(); });
    centre(w);
    w->show();
}

// Disk Info: the volume's name, size, used, free
// Tools/DiskInfo: the volume's icon with its device and file system on the left; on the
// right its name, size, used and free beside a gauge, the block size and the state
void disk_info(const std::string &volume) {
    struct statvfs sv;
    if (statvfs(volume.c_str(), &sv) != 0) return;
    unsigned long long total = (unsigned long long)sv.f_blocks * sv.f_frsize,
                       freeb = (unsigned long long)sv.f_bavail * sv.f_frsize,
                       used = total - (unsigned long long)sv.f_bfree * sv.f_frsize;
    auto human = [](unsigned long long b) {
        char t[64];
        if (b >= 1ULL << 30) snprintf(t, sizeof t, "%.1fG", b / 1073741824.0);
        else if (b >= 1ULL << 20) snprintf(t, sizeof t, "%.1fM", b / 1048576.0);
        else snprintf(t, sizeof t, "%.1fK", b / 1024.0);
        return std::string(t);
    };
    auto with_bytes = [&](unsigned long long b) { return human(b) + " (" + std::to_string(b) + " " + tr("bytes") + ")"; };
    // the device and the file system (the longest mount point holding the volume)
    std::string device, fstype;
    {
        std::ifstream m("/proc/self/mounts");
        std::string line;
        size_t best = 0;
        std::string v = fs::weakly_canonical(volume).string();
        while (std::getline(m, line)) {
            std::istringstream ls(line);
            std::string dev, mnt, type;
            ls >> dev >> mnt >> type;
            if ((v == mnt || v.rfind(mnt == "/" ? "/" : mnt + "/", 0) == 0) && mnt.size() >= best) {
                best = mnt.size(); device = dev; fstype = type;
            }
        }
    }
    std::string name = to_volume_path(volume);
    if (!name.empty() && name.back() == ':') name.pop_back();
    if (name.find(':') != std::string::npos || name.empty()) name = fs::path(volume).filename().string();
    int percent = total ? (int)(used * 100 / total) : 0;
    bool done = false;
    auto *root = HGroup();
    {
        auto *left = VGroup();
        HVSpace();
        auto *ir = HGroup();
        HVSpace();
        IconKind kind = icon_kind(volume, true, true, false);
        auto *img = new ImageButton(ICON_W, ICON_H, [kind](int x, int y, int, int, bool sel) { draw_icon(kind, x, y, sel); });
        img->frame(Frame_None);
        img->input_mode(Input_Toggle);
        HVSpace();
        ir->end();
        HVSpace();
        auto *info = new Group(Group::Vertical);
        info->columns(2);
        if (!device.empty()) { new Text("\033rDevice:"); new Text("\033l" + device); }
        new Text("\033rFile system:");
        new Text("\033l" + (fstype.empty() ? tr("Unknown") : fstype));
        info->end();
        HVSpace();
        left->end();

        auto *right = VGroup();
        auto *g = new Group(Group::Horizontal, Frame_Group);
        HVSpace();
        auto *cols = new Group(Group::Vertical);
        cols->columns(2);
        new Text("\033rName:");
        new Text("\033b\033l" + name, Frame_Text);
        auto *labels = VGroup();
        // MUIA_FramePhantomHoriz: as high as the framed values beside them, no frame drawn
        const FrameSpec &tf = std_frame(Frame_Text);
        FrameBorders tb = frame_borders(tf.type, tf.down);
        for (const char *l : {"\033rSize:", "\033rUsed:", "\033rFree:"})
            (new Text(l))->inner(0, 0, tb.top + tf.inner_top, tb.bottom + tf.inner_bottom);
        labels->end();
        auto *vals = HGroup();
        auto *vv = VGroup();
        new Text("\033l" + with_bytes(total), Frame_Text);
        new Text("\033l" + with_bytes(used) + " " + std::to_string(percent) + "%", Frame_Text);
        new Text("\033l" + with_bytes(freeb), Frame_Text);
        vv->end();
        auto *gauge = new Gauge();
        gauge->vertical(true);
        gauge->set(percent, 100);
        vals->end();
        new Text("\033rBlock size:");
        new Text("\033l" + std::to_string(sv.f_frsize) + " " + tr("bytes"), Frame_Text);
        new Text("\033rStatus:");
        new Text(std::string("\033l") + tr(sv.f_flag & ST_RDONLY ? "Read only" : "Read/Write"), Frame_Text);
        cols->end();
        HVSpace();
        g->end();
        HVSpace();
        right->end();
    }
    root->end();
    auto *w = new Window(tr("Disk Info") + " - " + name, root);
    w->callback([](Fl_Widget *win, void *d) { *(bool *)d = true; win->hide(); }, &done);
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
// .desktop files (the programs' descriptions on Linux): Name, Exec without its field codes
bool read_desktop_entry(const std::string &file, DesktopEntry &e) {
    FILE *f = fopen(file.c_str(), "r");
    if (!f) return false;
    char line[4096];
    bool in_entry = false;
    e = DesktopEntry{};
    while (fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (!l.empty() && l[0] == '[') { in_entry = l == "[Desktop Entry]"; continue; }
        if (!in_entry) continue;
        auto eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (k == "Name") e.name = v;
        else if (k == "Exec") e.exec = v;
        else if (k == "TryExec") e.try_exec = v;
        else if (k == "Icon") e.icon = v;
        else if (k == "Hidden") e.hidden = v == "true";
        else if (k == "NoDisplay") e.no_display = v == "true";
        else if (k == "OnlyShowIn") e.only_show_in = v;
        else if (k == "NotShowIn") e.not_show_in = v;
        else if (k == "Terminal") e.terminal = v == "true";
    }
    fclose(f);
    // field codes (%f %F %u %U %i %c %k ...) are left out; %% is a percent sign
    std::string ex;
    for (size_t i = 0; i < e.exec.size(); i++) {
        if (e.exec[i] == '%' && i + 1 < e.exec.size()) {
            if (e.exec[i + 1] == '%') ex += '%';
            i++;
            continue;
        }
        ex += e.exec[i];
    }
    e.exec = ex;
    return !e.exec.empty();
}

static bool in_path(const std::string &prog) {
    if (prog.empty()) return true;
    if (prog[0] == '/') return access(prog.c_str(), X_OK) == 0;
    const char *path = getenv("PATH");
    std::string p = path ? path : "/usr/bin:/bin";
    size_t a = 0;
    while (a <= p.size()) {
        size_t b = p.find(':', a);
        std::string d = p.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!d.empty() && access((d + "/" + prog).c_str(), X_OK) == 0) return true;
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return false;
}

static bool listed(const std::string &list, const char *name) {
    size_t a = 0;
    while (a < list.size()) {
        size_t b = list.find(';', a);
        if (list.substr(a, b == std::string::npos ? std::string::npos : b - a) == name) return true;
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return false;
}

// The programs started with Rambler (Tools/ExecuteStartup): everything in the Startup
// drawer (~/.config/rambler/startup: programs, scripts, .desktop files), in the order of
// their STARTPRI tool type (+127 first, -128 last, 0 without one; the same: by name), each
// followed by a pause of its WAIT tool type (seconds, at most 60); then the autostart entries
// of the system and the user (XDG autostart) meant for this desktop. (DONOTWAIT is taken for
// every program, as the original does.)
namespace {
struct StartupItem {
    std::string name;
    std::vector<std::string> argv;
    int pri = 0, wait = 0;
};
std::vector<StartupItem> g_startup;
size_t g_startup_next = 0;
std::string g_conf;
void run_autostart(const std::string &conf);

std::string home_dir() { return getenv("HOME") ? getenv("HOME") : "/"; }

void start_next(void *) {
    while (g_startup_next < g_startup.size()) {
        StartupItem &it = g_startup[g_startup_next++];
        if (!launch(it.argv, home_dir()))
            request(tr("Warning"), trs("Startup:\nCould not start\n%s", it.name), tr("OK"));
        if (it.wait > 0) { Fl::add_timeout(it.wait, start_next); return; }
    }
    if (!g_conf.empty()) { run_autostart(g_conf); g_conf.clear(); }   // the Startup drawer done
}

void run_autostart(const std::string &conf) {
    std::error_code ec;
    // XDG autostart: the user's entries hide the system's of the same name
    std::map<std::string, std::string> entries;
    for (std::string dir : {std::string("/etc/xdg/autostart"), conf + "/autostart"})
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (it->path().extension() == ".desktop") entries[it->path().filename().string()] = it->path().string();
    for (auto &[name, file] : entries) {
        DesktopEntry e;
        if (!read_desktop_entry(file, e) || e.hidden) continue;
        if (!e.only_show_in.empty() && !listed(e.only_show_in, "Rambler")) continue;
        if (listed(e.not_show_in, "Rambler")) continue;
        if (!in_path(e.try_exec)) continue;
        launch({"/bin/sh", "-c", e.exec}, home_dir());
    }
}
}  // namespace

void run_startup() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string conf = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    std::string startup = conf + "/rambler/startup";
    std::error_code ec;
    fs::create_directories(startup, ec);
    std::vector<fs::path> paths;
    for (auto it = fs::directory_iterator(startup, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        paths.push_back(it->path());
    std::sort(paths.begin(), paths.end());
    g_startup.clear();
    g_startup_next = 0;
    for (auto &p : paths) {
        StartupItem it;
        it.name = p.filename().string();
        if (p.extension() == ".desktop") {
            DesktopEntry e;
            if (!read_desktop_entry(p.string(), e) || e.hidden) continue;
            it.argv = {"/bin/sh", "-c", e.exec};
        } else if (fs::is_regular_file(p, ec) && access(p.c_str(), X_OK) == 0) {
            it.argv = {p.string()};
        } else continue;
        for (auto &t : tool_types(p.string())) {
            if (t.rfind("STARTPRI=", 0) == 0) it.pri = std::clamp(atoi(t.c_str() + 9), -128, 127);
            else if (t.rfind("WAIT=", 0) == 0) it.wait = std::clamp(atoi(t.c_str() + 5), 0, 60);
        }
        g_startup.push_back(it);
    }
    // Enqueue: the highest priority first, the same in the order found
    std::stable_sort(g_startup.begin(), g_startup.end(), [](auto &a, auto &b) { return a.pri > b.pri; });
    g_conf = conf;
    start_next(nullptr);
}

// The Tools menu: the programs, scripts and .desktop files in ~/.config/rambler/tools
std::vector<ToolItem> tool_items() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string dir = (x && *x ? std::string(x) : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config") +
                      "/rambler/tools";
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::vector<ToolItem> v;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        auto &p = it->path();
        DesktopEntry e;
        if (p.extension() == ".desktop") {
            if (read_desktop_entry(p.string(), e) && !e.hidden && !e.no_display)
                v.push_back({e.name.empty() ? p.stem().string() : e.name, e.exec});
        } else if (fs::is_regular_file(p, ec) && access(p.c_str(), X_OK) == 0)
            v.push_back({p.filename().string(), "'" + p.string() + "'"});
    }
    std::sort(v.begin(), v.end(), [](auto &a, auto &b) { return strcasecmp(a.label.c_str(), b.label.c_str()) < 0; });
    return v;
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
    auto *sdir = new String(to_volume_path(dir), 1024);
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
        sdir->value(to_volume_path(d));
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
    sdir->on_acknowledge = [&] {
        std::error_code e2;
        std::string d = from_volume_path(sdir->value());
        if (fs::is_directory(d, e2)) read_dir(d);
    };
    sfile->on_acknowledge = [&] { ok = done = true; };
    bok->on_press = [&] { ok = done = true; };
    bc->on_press = [&] { done = true; };
    bvol->on_press = [&] { show_volumes(); };
    bpar->on_press = [&] {
        fs::path p = fs::path(from_volume_path(sdir->value()));
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
    std::string chosen = (fs::path(from_volume_path(sdir->value())) / sfile->value()).string();
    if (std::string(sfile->value()).empty()) chosen = from_volume_path(sdir->value());
    w->hide();
    Fl::delete_widget(w);
    if (ok) path = chosen;
    return ok;
}

// --------------------------------------------------------------------------------------------
// Format (as the system's Format tool): a removable disk gets a new, empty file system - the
// volume's name, the file system, Put Trashcan (its trash drawer made at once), Format (the
// disk checked for bad blocks while it is made) / Quick Format / Cancel. The work is done as
// root through polkit (pkexec); what it prints shows in an output window.
void format_disk(const std::string &device, const std::string &label) {
    // only removable disks: never the system's
    bool removable = false;
    for (auto &v : removable_volumes()) if (v.device == device) removable = true;
    if (!removable) {
        request("Format", trs("%s is not a removable disk - it is not formatted from here.", device), "OK");
        return;
    }
    bool done = false;
    int how = 0;   // 1 Format, 2 Quick Format
    auto *root = VGroup();
    auto *g = new Group(Group::Vertical, Frame_Group);
    g->columns(2);
    new Text("\033rDevice:");
    new Text(device, Frame_Text);
    new Text("\033rName:");
    auto *name = new String(label.empty() ? "Empty" : label, 32);
    new Text("\033rFile system:");
    auto *fsys = new Cycle({"FAT32 (every system)", "exFAT (large files)", "ext4 (Linux)"}, 0);
    new Text("\033rPut Trashcan:");
    auto *trashcan = new Checkmark(true);
    g->end();
    auto *buttons = HGroup();
    buttons->same_width(true);
    auto *bf = new Button("Format");
    auto *bq = new Button("Quick Format");
    auto *bc = new Button("Cancel");
    buttons->end();
    root->end();
    bf->on_press = [&] { how = 1; done = true; };
    bq->on_press = [&] { how = 2; done = true; };
    bc->on_press = [&] { done = true; };
    auto *w = new Window(trs("Format %s", device), root);
    w->size(std::max(w->w(), 380), w->h());
    centre(w);
    w->set_modal();
    w->show();
    while (!done && w->shown()) Fl::wait();
    std::string vol = name->value();
    int kind = fsys->active();
    bool put_trash = trashcan->on();
    w->hide();
    Fl::delete_widget(w);
    if (!how) return;
    if (request("Format", trs("Everything on %s (%s) will be lost.\nFormat it?", device, label.empty() ? "?" : label),
                "Format|Cancel") != 1)
        return;
    // the name as the file system allows it
    std::string clean;
    for (char c : vol) if (isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') clean += c;
    size_t maxlen = kind == 0 ? 11 : kind == 1 ? 15 : 16;
    if (clean.size() > maxlen) clean.resize(maxlen);
    if (kind == 0) for (auto &c : clean) c = (char)toupper((unsigned char)c);
    if (clean.empty()) clean = "EMPTY";
    std::string check = how == 1 ? (kind == 2 ? " -c" : kind == 0 ? " -c" : "") : "";
    std::string mkfs = kind == 0 ? "mkfs.vfat -F 32" + check + " -n '" + clean + "'"
                     : kind == 1 ? "mkfs.exfat -L '" + clean + "'"
                                 : "mkfs.ext4 -F" + check + " -L '" + clean + "'";
    std::string cmd =
        "udisksctl unmount --no-user-interaction -b '" + device + "' >/dev/null 2>&1; "
        "echo 'Formatting " + device + "...'; "
        "pkexec sh -c \"wipefs -a '" + device + "' >/dev/null && " + mkfs + " '" + device + "'\" && "
        "{ out=$(udisksctl mount --no-user-interaction -b '" + device + "' 2>&1); echo \"$out\"; " +
        (put_trash ? "mnt=$(echo \"$out\" | sed -n 's/.* at \\(.*\\)\\.*$/\\1/p' | sed 's/\\.$//'); "
                     "[ -n \"$mnt\" ] && mkdir -p \"$mnt/.Trash-$(id -u)\" 2>/dev/null; "
                   : "") +
        "echo 'Done.'; } || echo 'Formatting failed.'";
    run_with_output(cmd, "/", trs("Format %s", device));
}

// --------------------------------------------------------------------------------------------
// The assigns' editor: the list of names and their drawers; Add (a name and a drawer from the
// file requester), Remove; Save writes ~/.config/rambler/assigns
void edit_assigns() {
    auto list_v = assigns();
    bool done = false, save = false;
    auto *root = VGroup();
    new Text("\033cNames of your own for drawers, usable in every volume path (\"Docs:Listy\")");
    auto *list = new List();
    list->weight(300);
    auto fill = [&] {
        list->clear();
        for (auto &[n, p] : list_v) list->add({n + ":", to_volume_path(p), false});
        list->done();
    };
    auto *row = HGroup();
    new Text("Name:");
    auto *name = new String("", 64);
    name->weight(100);
    auto *add = new Button("Add...");
    auto *rem = new Button("Remove");
    row->end();
    auto *buttons = HGroup();
    buttons->same_width(true);
    auto *bs = new Button("Save");
    HSpace();
    auto *bc = new Button("Cancel");
    buttons->end();
    root->end();
    fill();
    add->on_press = [&] {
        std::string n = name->value();
        while (!n.empty() && (n.back() == ':' || n.back() == ' ')) n.pop_back();
        if (n.empty() || n.find_first_of("/:") != std::string::npos) {
            request("Assigns", "Give a name first (without '/' or ':').", "OK");
            return;
        }
        std::string dir = getenv("HOME") ? getenv("HOME") : "/";
        if (!file_request(trs("The drawer for %s:", n), dir)) return;
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) dir = fs::path(dir).parent_path().string();
        for (auto &a : list_v) if (strcasecmp(a.first.c_str(), n.c_str()) == 0) { a.second = dir; fill(); return; }
        list_v.push_back({n, dir});
        fill();
        name->value("");
    };
    rem->on_press = [&] {
        int i = list->active();
        if (i >= 0 && i < (int)list_v.size()) { list_v.erase(list_v.begin() + i); fill(); }
    };
    bs->on_press = [&] { save = done = true; };
    bc->on_press = [&] { done = true; };
    auto *w = new Window("Assigns", root);
    w->size(std::max(w->w(), 420), std::max(w->h(), 300));
    centre(w);
    w->set_modal();
    w->show();
    while (!done && w->shown()) Fl::wait();
    w->hide();
    Fl::delete_widget(w);
    if (!save) return;
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    std::error_code ec;
    fs::create_directories(base + "/rambler", ec);
    if (FILE *f = fopen((base + "/rambler/assigns").c_str(), "w")) {
        fprintf(f, "# Rambler's assigns: NAME = drawer (usable as NAME: in volume paths)\n");
        for (auto &[n, p] : list_v) fprintf(f, "%s = %s\n", n.c_str(), p.c_str());
        fclose(f);
    }
}

bool ask_find(std::string &pattern, std::string &contents) {
    bool done = false, ok = false;
    auto *root = VGroup();
    new Text("\033cFind the files and drawers whose names match\n(wildcards: * and ?) and, if given, that contain a text");
    auto *g = new Group(Group::Vertical);
    g->columns(2);
    new Text("\033rName:");
    auto *s = new String(pattern, 255);
    s->weight(300);
    new Text("\033rContents:");
    auto *c = new String(contents, 255);
    c->weight(300);
    g->end();
    auto *buttons = HGroup();
    auto *bok = new Button("Find");
    HSpace();
    auto *bc = new Button("Cancel");
    buttons->end();
    root->end();
    auto *w = new Window("Find", root);
    w->size(std::max(w->w(), 380), w->h());
    bok->on_press = [&] { ok = done = true; };
    s->on_acknowledge = [&] { ok = done = true; };
    c->on_acknowledge = [&] { ok = done = true; };
    bc->on_press = [&] { done = true; };
    centre(w);
    w->set_modal();
    w->show();
    Fl::focus(s->input());
    while (!done && w->shown()) Fl::wait();
    pattern = s->value();
    contents = c->value();
    w->hide();
    Fl::delete_widget(w);
    if (pattern.empty() && !contents.empty()) pattern = "*";
    if (ok && !pattern.empty() && pattern.find_first_of("*?") == std::string::npos) pattern = "*" + pattern + "*";
    return ok && !pattern.empty();
}

}  // namespace rambler
