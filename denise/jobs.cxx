// Nirvana Desktop - Rambler: background jobs and the Jobs window
#include "jobs.h"
#include "i18n.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Scroll.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace plat;

namespace jobs {

static std::vector<std::shared_ptr<Job>> g_jobs;

static double now() {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}

bool Job::check() {
  while (pause && !cancel) usleep(100000);
  return !cancel;
}

uint64_t measure(const fs::path &p, int &files) {
  std::error_code ec;
  auto st = fs::symlink_status(p, ec);
  if (ec) return 0;
  if (fs::is_symlink(st)) { files++; return 0; }
  if (!fs::is_directory(st)) { files++; return fs::file_size(p, ec); }
  uint64_t sum = 0;
  for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    std::error_code e2;
    auto s = it->symlink_status(e2);
    if (fs::is_regular_file(s)) { files++; sum += it->file_size(e2); }
    else if (fs::is_symlink(s)) files++;
  }
  return sum;
}

static bool copy_file(Job &j, const fs::path &src, const fs::path &dst, std::string &err) {
  int in = open(src.c_str(), O_RDONLY | O_CLOEXEC);
  if (in < 0) { err = src.filename().string() + ": " + strerror(errno); return false; }
  struct stat st;
  fstat(in, &st);
  int out = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, st.st_mode & 07777);
  if (out < 0) { err = dst.filename().string() + ": " + strerror(errno); ::close(in); return false; }
  static thread_local std::vector<char> buf(1 << 20);
  bool ok = true;
  for (;;) {
    if (!j.check()) { ok = false; err.clear(); break; }
    ssize_t n = read(in, buf.data(), buf.size());
    if (n < 0) { if (errno == EINTR) continue; err = src.filename().string() + ": " + strerror(errno); ok = false; break; }
    if (n == 0) break;
    ssize_t off = 0;
    while (off < n) {
      ssize_t w = write(out, buf.data() + off, (size_t)(n - off));
      if (w < 0) { if (errno == EINTR) continue; err = dst.filename().string() + ": " + strerror(errno); ok = false; break; }
      off += w;
    }
    if (!ok) break;
    j.done += (uint64_t)n;
    // tests only: NIRVANA_JOBS_SLOW=ms waits after every megabyte (to see the Jobs window)
    static const int slow = getenv("NIRVANA_JOBS_SLOW") ? atoi(getenv("NIRVANA_JOBS_SLOW")) : 0;
    if (slow > 0) usleep(slow * 1000);
  }
  ::close(in);
  if (::close(out) != 0 && ok) { err = dst.filename().string() + ": " + strerror(errno); ok = false; }
  if (!ok) { unlink(dst.c_str()); return false; }   // no half files
  struct timespec ts[2] = {st.st_atim, st.st_mtim};
  utimensat(AT_FDCWD, dst.c_str(), ts, 0);
  j.files_done++;
  return true;
}

bool copy_tree(Job &j, const fs::path &src, const fs::path &dst, std::string &err) {
  std::error_code ec;
  auto st = fs::symlink_status(src, ec);
  if (ec) { err = src.filename().string() + ": " + ec.message(); return false; }
  j.set_current(src.filename().string());
  if (fs::is_symlink(st)) {
    fs::copy_symlink(src, dst, ec);
    if (ec) { err = src.filename().string() + ": " + ec.message(); return false; }
    j.files_done++;
    return true;
  }
  if (!fs::is_directory(st)) return copy_file(j, src, dst, err);
  fs::create_directory(dst, src, ec);
  if (ec && !fs::is_directory(dst)) { err = dst.filename().string() + ": " + ec.message(); return false; }
  for (auto it = fs::directory_iterator(src, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    if (!j.check()) return false;
    std::string e;
    if (!copy_tree(j, it->path(), dst / it->path().filename(), e)) {
      if (j.cancel) return false;
      j.error(e);
    }
  }
  return true;
}

bool move_tree(Job &j, const fs::path &src, const fs::path &dst, std::string &err) {
  j.set_current(src.filename().string());
  if (rename(src.c_str(), dst.c_str()) == 0) {
    int f = 0;
    j.done += measure(dst, f);
    j.files_done += f;
    return true;
  }
  if (errno != EXDEV) { err = src.filename().string() + ": " + strerror(errno); return false; }
  // another disk: copy, then delete the original (only when everything was copied)
  std::string before;
  { std::lock_guard<std::mutex> l(j.m); before = j.errors; }
  if (!copy_tree(j, src, dst, err)) return false;
  { std::lock_guard<std::mutex> l(j.m); if (j.errors != before) return false; }
  std::error_code ec;
  fs::remove_all(src, ec);
  if (ec) { err = src.filename().string() + ": " + ec.message(); return false; }
  return true;
}

bool remove_tree(Job &j, const fs::path &p, std::string &err) {
  j.set_current(p.filename().string());
  std::error_code ec;
  int f = 0;
  measure(p, f);
  fs::remove_all(p, ec);
  if (ec) { err = p.filename().string() + ": " + ec.message(); return false; }
  j.files_done += f;
  return true;
}

// ---------------------------------------------------------------------------
// The Jobs window
// ---------------------------------------------------------------------------
static std::string human(uint64_t b) {
  char t[32];
  if (b < 1024 * 1024) snprintf(t, sizeof t, "%.0f KB", b / 1024.0);
  else if (b < 1024ull * 1024 * 1024) snprintf(t, sizeof t, "%.1f MB", b / 1048576.0);
  else snprintf(t, sizeof t, "%.2f GB", b / 1073741824.0);
  return t;
}

class Row : public Fl_Group {
public:
  std::shared_ptr<Job> job;
  Fl_Button *pause, *cancel;
  Row(int X, int Y, int W, std::shared_ptr<Job> j) : Fl_Group(X, Y, W, 64), job(std::move(j)) {
    pause = new Fl_Button(X + W - 176, Y + 22, 80, 22);
    cancel = new Fl_Button(X + W - 90, Y + 22, 80, 22, _("Cancel"));
    for (auto *b : {pause, cancel}) { b->box(BUTTON_BOX); b->down_box(BUTTON_DOWN); b->labelsize(SIZE_SMALL + 1); }
    pause->callback([](Fl_Widget *w, void *) { auto *r = (Row *)w->parent(); r->job->pause = !r->job->pause; r->redraw(); });
    cancel->callback([](Fl_Widget *w, void *) { auto *r = (Row *)w->parent(); r->job->cancel = true; r->job->pause = false; });
    end();
  }
  void draw() override {
    pause->label(job->pause ? _("Resume") : _("Pause"));
    fl_color(GRAY);
    fl_rectf(x(), y(), w(), h());
    fl_color(SHADOW);
    fl_xyline(x() + 6, y() + h() - 1, x() + w() - 6);
    fl_font(FONT_SYSTEM, SIZE_SMALL + 1);
    fl_color(TEXT);
    int tw = w() - 196;
    fl_draw(ellipsize(job->title, tw).c_str(), x() + 10, y() + 16, 0, 0, FL_ALIGN_LEFT, nullptr, 0);
    // the bar
    uint64_t t = job->total, d = job->done;
    double frac = t ? std::min(1.0, (double)d / t) : 0;
    fl_draw_box(WELL_BOX, x() + 10, y() + 24, tw, 14, CONTENT);
    fl_color(job->pause ? fl_color_average(SEL, GRAY, 0.4f) : SEL);
    fl_rectf(x() + 12, y() + 26, (int)((tw - 4) * frac), 10);
    std::string cur;
    { std::lock_guard<std::mutex> l(job->m); cur = job->current; }
    double el = now() - job->started;
    std::string info = std::to_string(job->files_done) + "/" + std::to_string(job->files_total) + "   " + human(d) + " / " + human(t);
    if (el > 1 && d > 0) {
      info += "   " + human((uint64_t)(d / el)) + "/s";
      if (t > d && !job->pause) {
        int left = (int)((t - d) / (d / el));
        info += "   " + fmt(_("%1 s left"), std::to_string(left));
      }
    }
    if (job->pause) info += "   " + std::string(_("paused"));
    fl_font(FONT_SMALL, SIZE_SMALL);
    fl_draw((ellipsize(cur, tw / 2) + "   " + info).c_str(), x() + 10, y() + 52, 0, 0, FL_ALIGN_LEFT, nullptr, 0);
    draw_children();
  }
};

class JobsWin : public Fl_Double_Window {
public:
  Fl_Scroll *scroll;
  std::vector<Job *> shown_;
  JobsWin() : Fl_Double_Window(520, 140, _("Jobs")) {
    color(GRAY);
    scroll = new Fl_Scroll(0, 0, 520, 140);
    scroll->type(Fl_Scroll::VERTICAL);
    scroll->box(FL_FLAT_BOX);
    scroll->color(GRAY);
    scroll->end();
    end();
    resizable(scroll);
    xclass("nirvana-progress");   // Insight: shown on top without taking the keyboard
  }
  void sync() {
    std::vector<Job *> now_jobs;
    for (auto &j : g_jobs) now_jobs.push_back(j.get());
    if (now_jobs != shown_) {
      shown_ = now_jobs;
      scroll->clear();
      scroll->begin();
      int y = 0;
      for (auto &j : g_jobs) { new Row(0, y, w() - SCROLL_W, j); y += 64; }
      scroll->end();
      int H = std::clamp((int)g_jobs.size() * 64, 64, 64 * 6);
      size(w(), H);
      scroll->resize(0, 0, w(), H);
    }
    redraw();
  }
};
static JobsWin *g_win = nullptr;

void show_window() {
  if (!g_win) g_win = new JobsWin;
  g_win->sync();
  if (!g_win->shown()) {
    int sx, sy, sw, sh;
    Fl::screen_work_area(sx, sy, sw, sh);
    // the lower right corner - away from the folder windows, which open at the top left
    g_win->position(sx + sw - g_win->w() - 30, sy + sh - g_win->h() - 120);
    g_win->show();
  }
  g_win->show();   // again: an already open window comes to the front
}

static void tick(void *) {
  // finished jobs: their on_done in this (the interface) thread
  for (size_t i = 0; i < g_jobs.size();) {
    auto j = g_jobs[i];
    if (j->finished) {
      g_jobs.erase(g_jobs.begin() + (long)i);
      if (j->on_done) j->on_done(*j);
      continue;
    }
    // a job longer than a moment: the window opens
    if (now() - j->started > 0.7 && (!g_win || !g_win->shown())) show_window();
    i++;
  }
  if (g_win && g_win->shown()) {
    if (g_jobs.empty()) g_win->hide();
    else g_win->sync();
  }
  if (!g_jobs.empty()) Fl::repeat_timeout(0.2, tick);
}

void start(std::shared_ptr<Job> j) {
  j->started = now();
  bool was_idle = g_jobs.empty();
  g_jobs.push_back(j);
  std::thread([j] {
    j->work(*j);
    j->finished = true;
  }).detach();
  if (was_idle) Fl::add_timeout(0.1, tick);
}

int running() { return (int)g_jobs.size(); }

}  // namespace jobs
