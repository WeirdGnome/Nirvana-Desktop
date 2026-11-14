/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    File operations in the background (fileops.h).
*/
#include "fileops.h"
#include "tools.h"
#include "zune.h"

#include <FL/Fl.H>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <condition_variable>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace zune;

namespace wanderer {

namespace {

std::atomic<int> g_running{0};

struct Job {
    FileOpKind kind;
    std::vector<std::string> sources;
    std::string dest;
    std::function<void()> done;

    // progress (written by the worker, read by the interface)
    std::mutex m;
    std::string current;               // the item being worked on
    std::atomic<long long> bytes_done{0}, bytes_total{0};
    std::atomic<int> items_done{0}, items_total{0};
    std::atomic<bool> abort{false}, finished{false}, counted{false};
    std::vector<std::string> errors;

    // a question to the user: the worker waits for the answer
    std::condition_variable cv;
    std::string question;              // non-empty: waiting
    int answer = -1;                   // 1 replace, 2 replace all, 3 skip, 0 abort
    bool replace_all = false;

    // the interface
    Window *win = nullptr;
    Text *what = nullptr, *where = nullptr;
    Gauge *gauge = nullptr;
    double started = 0;
    std::thread thread;
};

const char *verb(FileOpKind k) {
    switch (k) {
    case FileOpKind::Copy: return "Copying";
    case FileOpKind::Move: return "Moving";
    case FileOpKind::Delete: return "Deleting";
    default: return "Moving to the trash";
    }
}

// --------------------------------------------------------------------------------------------
// The worker
// --------------------------------------------------------------------------------------------
void count(Job &j, const fs::path &p) {
    std::error_code ec;
    struct stat st;
    if (lstat(p.c_str(), &st) != 0) return;
    j.items_total++;
    if (S_ISREG(st.st_mode)) j.bytes_total += st.st_size;
    else if (S_ISDIR(st.st_mode))
        for (auto it = fs::directory_iterator(p, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (j.abort) return;
            count(j, it->path());
        }
}

void set_current(Job &j, const std::string &s) {
    std::lock_guard<std::mutex> l(j.m);
    j.current = s;
}

void error(Job &j, const std::string &s) {
    std::lock_guard<std::mutex> l(j.m);
    j.errors.push_back(s);
}

// asks the interface; returns the answer (1 replace, 3 skip, 0 abort)
int ask(Job &j, const std::string &q) {
    if (j.replace_all) return 1;
    std::unique_lock<std::mutex> l(j.m);
    j.question = q;
    j.answer = -1;
    j.cv.wait(l, [&] { return j.answer >= 0 || j.abort; });
    j.question.clear();
    if (j.abort) return 0;
    if (j.answer == 2) { j.replace_all = true; return 1; }
    return j.answer;
}

bool copy_file(Job &j, const fs::path &from, const fs::path &to) {
    int in = open(from.c_str(), O_RDONLY);
    if (in < 0) { error(j, from.string() + ": " + strerror(errno)); return false; }
    struct stat st;
    fstat(in, &st);
    int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
    if (out < 0) { error(j, to.string() + ": " + strerror(errno)); close(in); return false; }
    std::vector<char> buf(1 << 20);
    bool ok = true;
    for (;;) {
        if (j.abort) { ok = false; break; }
        ssize_t n = read(in, buf.data(), buf.size());
        if (n == 0) break;
        if (n < 0) { error(j, from.string() + ": " + strerror(errno)); ok = false; break; }
        for (ssize_t off = 0; off < n;) {
            ssize_t w = write(out, buf.data() + off, n - off);
            if (w < 0) { error(j, to.string() + ": " + strerror(errno)); ok = false; break; }
            off += w;
        }
        if (!ok) break;
        j.bytes_done += n;
    }
    close(in);
    if (close(out) != 0) ok = false;
    if (!ok) unlink(to.c_str());   // no half files left behind
    else {
        struct timespec t[2] = {st.st_atim, st.st_mtim};
        utimensat(AT_FDCWD, to.c_str(), t, 0);
    }
    return ok;
}

// copies a file, a link or a drawer (with everything in it)
bool copy_any(Job &j, const fs::path &from, const fs::path &to) {
    if (j.abort) return false;
    std::error_code ec;
    struct stat st;
    if (lstat(from.c_str(), &st) != 0) { error(j, from.string() + ": " + strerror(errno)); return false; }
    set_current(j, from.filename().string());
    j.items_done++;
    if (S_ISLNK(st.st_mode)) {
        fs::copy_symlink(from, to, ec);
        if (ec) { error(j, from.string() + ": " + ec.message()); return false; }
        return true;
    }
    if (S_ISDIR(st.st_mode)) {
        fs::create_directory(to, ec);
        if (ec) { error(j, to.string() + ": " + ec.message()); return false; }
        bool ok = true;
        for (auto it = fs::directory_iterator(from, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (!copy_any(j, it->path(), to / it->path().filename())) ok = false;
        chmod(to.c_str(), st.st_mode & 07777);
        return ok;
    }
    if (S_ISREG(st.st_mode)) return copy_file(j, from, to);
    error(j, from.string() + ": not a file or drawer - not copied");   // pipes, devices, sockets
    return false;
}

bool remove_any(Job &j, const fs::path &p) {
    if (j.abort) return false;
    std::error_code ec;
    struct stat st;
    if (lstat(p.c_str(), &st) != 0) return true;
    if (S_ISDIR(st.st_mode))
        for (auto it = fs::directory_iterator(p, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (!remove_any(j, it->path())) return false;
    set_current(j, p.filename().string());
    j.items_done++;
    if (S_ISREG(st.st_mode)) j.bytes_done += st.st_size;
    fs::remove(p, ec);
    if (ec) { error(j, p.string() + ": " + ec.message()); return false; }
    return true;
}

void work(Job &j) {
    for (auto &s : j.sources) count(j, s);
    j.counted = true;
    for (auto &src : j.sources) {
        if (j.abort) break;
        fs::path from(src);
        std::error_code ec;
        if (j.kind == FileOpKind::Delete) { remove_any(j, from); continue; }
        if (j.kind == FileOpKind::Trash) {
            set_current(j, from.filename().string());
            std::string err;
            if (!move_to_trash(src, err)) error(j, src + ": " + err);
            j.items_done++;
            continue;
        }
        fs::path to = fs::path(j.dest) / from.filename();
        if (from.parent_path() == fs::path(j.dest) || to == from) continue;
        // a drawer into itself
        std::string fs_ = fs::weakly_canonical(from, ec).string(), ts = fs::weakly_canonical(j.dest, ec).string();
        if (ts == fs_ || ts.rfind(fs_ + "/", 0) == 0) { error(j, from.filename().string() + ": cannot go into itself"); continue; }
        if (fs::exists(fs::symlink_status(to, ec))) {
            int a = ask(j, "'" + to.filename().string() + "' already exists in\n" + j.dest + ".");
            if (a == 0) { j.abort = true; break; }
            if (a == 3) continue;
            remove_any(j, to);
        }
        if (j.kind == FileOpKind::Move) {
            fs::rename(from, to, ec);   // the same volume: at once
            if (!ec) { set_current(j, from.filename().string()); j.items_done++; continue; }
            if (ec != std::errc::cross_device_link) { error(j, src + ": " + ec.message()); continue; }
            // another volume: copied, then removed
            if (copy_any(j, from, to)) remove_any(j, from);
        } else copy_any(j, from, to);
    }
    j.finished = true;
    Fl::awake();
}

// --------------------------------------------------------------------------------------------
// The interface: the progress window (shown when the work takes longer than a moment)
// --------------------------------------------------------------------------------------------
std::string human(long long b) {
    char s[32];
    if (b < 1024) snprintf(s, sizeof s, "%lld bytes", b);
    else if (b < 1024LL * 1024) snprintf(s, sizeof s, "%.1f KB", b / 1024.0);
    else if (b < 1024LL * 1024 * 1024) snprintf(s, sizeof s, "%.1f MB", b / 1048576.0);
    else snprintf(s, sizeof s, "%.2f GB", b / 1073741824.0);
    return s;
}

void make_window(Job *j) {
    auto *root = VGroup();
    auto *g = new Group(Group::Vertical, Frame_Group);
    g->columns(2);
    new Text("\033rItem:");
    j->what = new Text("", Frame_Text);
    j->what->weight(300);
    new Text("\033rTo:");
    j->where = new Text(j->kind == FileOpKind::Delete ? "-" : j->kind == FileOpKind::Trash ? "Trash" : j->dest, Frame_Text);
    g->end();
    j->gauge = new Gauge();
    auto *row = HGroup();
    HSpace();
    auto *abort = new Button("Abort");
    HSpace();
    row->end();
    root->end();
    abort->on_press = [j] { j->abort = true; j->cv.notify_all(); };
    j->win = new Window(verb(j->kind), root);
    j->win->size(std::max(j->win->w(), 420), j->win->h());
    j->win->callback([](Fl_Widget *, void *d) { auto *jj = (Job *)d; jj->abort = true; jj->cv.notify_all(); }, j);
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    j->win->position(sx + (sw - j->win->w()) / 2, sy + sh / 3);
    j->win->show();
}

void poll(void *d) {
    auto *j = (Job *)d;
    // a question from the worker
    std::string q;
    {
        std::lock_guard<std::mutex> l(j->m);
        q = j->question;
    }
    if (!q.empty() && j->answer < 0) {
        int r = request(verb(j->kind), q + "\nReplace it?", "Replace|Replace All|Skip|Abort");
        {
            std::lock_guard<std::mutex> l(j->m);
            j->answer = r == 1 ? 1 : r == 2 ? 2 : r == 3 ? 3 : 0;
        }
        j->cv.notify_all();
    }
    if (j->finished) {
        j->thread.join();
        if (j->win) { j->win->hide(); Fl::delete_widget(j->win); }
        std::vector<std::string> errs;
        {
            std::lock_guard<std::mutex> l(j->m);
            errs = j->errors;
        }
        if (!errs.empty()) {
            std::string t;
            for (size_t i = 0; i < errs.size() && i < 8; i++) t += errs[i] + "\n";
            if (errs.size() > 8) t += "... and " + std::to_string(errs.size() - 8) + " more\n";
            request(verb(j->kind), "Some items could not be handled:\n" + t, "OK");
        }
        if (j->done) j->done();
        g_running--;
        delete j;
        return;
    }
    // the window after a moment (quick operations show none)
    j->started += 0.1;
    if (!j->win && j->started >= 0.4) make_window(j);
    if (j->win) {
        std::string cur;
        {
            std::lock_guard<std::mutex> l(j->m);
            cur = j->current;
        }
        j->what->contents(cur);
        long long tot = j->bytes_total, done = j->bytes_done;
        if (j->kind == FileOpKind::Copy || j->kind == FileOpKind::Move) {
            if (tot > 0) {
                j->gauge->set(done, tot);
                j->gauge->info(human(done) + " of " + human(tot));
            } else j->gauge->set(j->items_done, std::max(1, (int)j->items_total));
        } else {
            j->gauge->set(j->items_done, std::max(1, (int)j->items_total));
            j->gauge->info(std::to_string((int)j->items_done) + " of " + std::to_string((int)j->items_total));
        }
    }
    Fl::repeat_timeout(0.1, poll, d);
}

}  // namespace

void file_operation(FileOpKind kind, std::vector<std::string> sources, std::string dest, std::function<void()> done) {
    if (sources.empty()) { if (done) done(); return; }
    auto *j = new Job;
    j->kind = kind;
    j->sources = std::move(sources);
    j->dest = std::move(dest);
    j->done = std::move(done);
    g_running++;
    j->thread = std::thread([j] { work(*j); });
    Fl::add_timeout(0.1, poll, j);
}

int file_operations_running() { return g_running; }

}  // namespace wanderer
