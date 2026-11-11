// Nirvana Desktop - the system updates applet in the menu bar
//
// Looks for updates without root rights (nirvana-update-check: apt, dnf, pacman,
// zypper, apk and xbps; Flatpak applications are not touched), shows their number on the icon and installs them
// with a progress bar (pkexec nirvana-update-helper - administrators at the computer
// need no password thanks to the polkit rule installed with Nirvana Desktop).
// Checks a minute after the start and then every 6 hours, or on demand.
#include "applets.h"
#include "applet_ui.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <cerrno>
#include <cmath>
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace applets {

namespace {

std::string exe_dir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return "";
  buf[n] = 0;
  std::string p = buf;
  return p.substr(0, p.rfind('/'));
}

// The helper scripts: installed in libexec, or next to the program, or in the source tree.
std::string find_script(const char *name) {
  std::string e = exe_dir();
  for (const std::string &p : {e + "/../libexec/" + name, e + "/" + name, e + "/../update/" + name,
                               std::string("/usr/libexec/") + name, std::string("/usr/local/libexec/") + name})
    if (!access(p.c_str(), R_OK)) return p;
  return "";
}

std::string log_file() {
  const char *x = getenv("XDG_CACHE_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/tmp") + "/.cache") + "/nirvana/update.log";
}

// A program in the background whose output arrives line by line in the FLTK loop.
struct Job {
  pid_t pid = -1;
  int fd = -1;
  std::string buf;
  std::function<void(const std::string &)> on_line;
  std::function<void(int)> on_exit;

  bool running() const { return pid > 0; }

  bool start(const std::vector<std::string> &argv) {
    int p[2];
    if (pipe(p)) return false;
    pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); pid = -1; return false; }
    if (pid == 0) {
      dup2(p[1], 1);
      dup2(p[1], 2);
      close(p[0]);
      close(p[1]);
      int devnull = open("/dev/null", O_RDONLY);
      if (devnull >= 0) dup2(devnull, 0);
      std::vector<char *> a;
      for (auto &s : argv) a.push_back((char *)s.c_str());
      a.push_back(nullptr);
      execvp(a[0], a.data());
      _exit(127);
    }
    close(p[1]);
    fd = p[0];
    fcntl(fd, F_SETFL, O_NONBLOCK);
    buf.clear();
    Fl::add_fd(fd, FL_READ, readable, this);
    return true;
  }

  static void readable(int, void *d) {
    auto *j = (Job *)d;
    char tmp[4096];
    for (;;) {
      ssize_t n = read(j->fd, tmp, sizeof tmp);
      if (n > 0) {
        j->buf.append(tmp, (size_t)n);
        size_t nl;
        while ((nl = j->buf.find_first_of("\n\r")) != std::string::npos) {
          std::string line = j->buf.substr(0, nl);
          j->buf.erase(0, nl + 1);
          if (!line.empty() && j->on_line) j->on_line(line);
        }
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
      // end of the output: the program has finished
      Fl::remove_fd(j->fd);
      close(j->fd);
      j->fd = -1;
      if (!j->buf.empty() && j->on_line) j->on_line(j->buf);
      int st = 0;
      waitpid(j->pid, &st, 0);
      j->pid = -1;
      int code = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
      if (j->on_exit) j->on_exit(code);
      return;
    }
  }
};

enum class State { Unknown, Checking, UpToDate, Available, Installing, Done, Failed };

struct Update { std::string name, version; };

State g_state = State::Unknown;
std::string g_backend;
std::vector<Update> g_updates;
std::string g_warning, g_error, g_action;
double g_progress = -1;         // 0..100, -1: unknown
std::string g_step;
time_t g_last_check = 0;
bool g_reboot = false;
Job g_job;
std::ofstream g_log;

class UpdatesApplet;
UpdatesApplet *g_upd = nullptr;
void changed();

const double CHECK_EVERY = 6 * 3600;

void check() {
  if (g_job.running()) return;
  std::string script = find_script("nirvana-update-check");
  if (script.empty()) { g_state = State::Failed; g_error = _("nirvana-update-check is missing."); changed(); return; }
  g_state = State::Checking;
  g_warning.clear();
  g_error.clear();
  auto *found = new std::vector<Update>;
  g_job.on_line = [found](const std::string &l) {
    size_t t1 = l.find('\t');
    std::string kind = l.substr(0, t1);
    std::string rest = t1 == std::string::npos ? "" : l.substr(t1 + 1);
    if (kind == "BACKEND") g_backend = rest;
    else if (kind == "WARN") g_warning = rest;
    else if (kind == "PKG") {
      size_t t2 = rest.find('\t');
      found->push_back({rest.substr(0, t2), t2 == std::string::npos ? "" : rest.substr(t2 + 1)});
    }
  };
  g_job.on_exit = [found](int) {
    g_updates = *found;
    delete found;
    g_last_check = time(nullptr);
    g_state = g_updates.empty() ? State::UpToDate : State::Available;
    changed();
  };
  if (!g_job.start({"sh", script})) { delete found; g_state = State::Failed; g_error = _("Cannot start the check."); }
  changed();
}

void periodic(void *) {
  if (g_state != State::Installing) check();
  Fl::repeat_timeout(CHECK_EVERY, periodic);
}

// Progress from the output of the helper: the stages are weighted - lists 0-10%,
// downloading 10-40%, installing 40-100% (other managers: 10-100% by "(n/N)").
void progress_line(const std::string &l) {
  if (g_log.is_open()) g_log << l << "\n" << std::flush;
  if (!l.compare(0, 5, "STEP ")) {
    g_step = l.substr(5);
    g_action = g_step == "refresh" ? _("Refreshing the package lists…")
             : g_step == "upgrade" ? _("Installing updates…") : g_step;
    g_progress = g_step == "refresh" ? 0 : g_step == "upgrade" ? 10 : g_progress;
    changed();
    return;
  }
  bool dl = !l.compare(0, 9, "dlstatus:"), pm = !l.compare(0, 9, "pmstatus:");
  if (dl || pm) {   // apt: "pmstatus:package:percent:description"
    // the package may contain a colon itself (libc6:amd64) - the percentage is the first number
    size_t b = l.find(':'), c = std::string::npos;
    while (b != std::string::npos) {
      size_t e = l.find(':', b + 1);
      if (e == std::string::npos) break;
      std::string f = l.substr(b + 1, e - b - 1);
      if (!f.empty() && f.find_first_not_of("0123456789.") == std::string::npos) { c = e; break; }
      b = e;
    }
    if (c == std::string::npos) return;
    double v = atof(l.substr(b + 1, c - b - 1).c_str());
    if (g_step == "refresh") g_progress = v * 0.1;
    else g_progress = dl ? 10 + v * 0.3 : 40 + v * 0.6;
    g_action = l.substr(c + 1);
    changed();
    return;
  }
  // dnf, pacman, zypper, apk: "(3/120) upgrading foo" or "Upgrading : foo   3/120"
  size_t slash = l.rfind('/');
  while (slash != std::string::npos && slash > 0) {
    size_t s = slash, e = slash + 1;
    while (s > 0 && isdigit((unsigned char)l[s - 1])) s--;
    while (e < l.size() && isdigit((unsigned char)l[e])) e++;
    if (s < slash && e > slash + 1) {
      double n = atof(l.substr(s, slash - s).c_str()), total = atof(l.substr(slash + 1, e - slash - 1).c_str());
      if (total > 0 && n <= total) {
        g_progress = 10 + 90 * n / total;
        std::string a = l;
        while (!a.empty() && a[0] == ' ') a.erase(0, 1);
        g_action = a.substr(0, 80);
        changed();
        return;
      }
    }
    slash = slash ? l.rfind('/', slash - 1) : std::string::npos;
  }
  if (!l.compare(0, 2, "E:") || !l.compare(0, 6, "error:") || !l.compare(0, 6, "Error:")) g_error = l.substr(0, 160);
}

void install() {
  if (g_job.running()) return;
  std::string helper = find_script("nirvana-update-helper");
  if (helper.empty()) { g_state = State::Failed; g_error = _("nirvana-update-helper is missing."); changed(); return; }
  std::string lf = log_file();
  g_log.close();
  g_log.open(lf, std::ios::trunc);
  g_state = State::Installing;
  g_progress = -1;
  g_action = _("Waiting for permission…");
  g_error.clear();
  g_step.clear();
  g_job.on_line = progress_line;
  g_job.on_exit = [](int code) {
    g_log.close();
    if (code == 0) {
      g_state = State::Done;
      g_reboot = !access("/run/reboot-required", F_OK) || !access("/var/run/reboot-required", F_OK);
      g_updates.clear();
      changed();
      Fl::add_timeout(2.0, [](void *) { check(); });   // what is left (e.g. held back packages)
      return;
    }
    g_state = State::Failed;
    if (code == 126 || code == 127) g_error = _("Installing was cancelled or not permitted.");
    else if (g_error.empty()) g_error = fmt(_("The package manager ended with error %1."), std::to_string(code));
    changed();
  };
  // as root through polkit; installed: /usr/libexec/nirvana-update-helper (see the policy file)
  if (!g_job.start({plat::pkexec_command(), helper, "upgrade"})) {
    g_state = State::Failed;
    g_error = _("pkexec is missing - install polkit.");
  }
  changed();
}

void confirm_install() {
  if (g_job.running() || g_updates.empty()) return;
  std::string text = fmt(trn((long)g_updates.size(), "Install %1 update?", "Install %1 updates?"),
                         std::to_string(g_updates.size())) + "\n\n" +
                     _("You can keep working meanwhile. Programs that are running may need a restart afterwards.");
  const Host &h = current_host();
  if (h.dialog) h.dialog(_("Software Update"), text, {{_("Cancel"), nullptr}, {_("Install"), install}});
  else install();
}

void show_log() {
  std::string shell = exe_dir() + "/nirvana-shell";
  if (access(shell.c_str(), X_OK)) shell = "nirvana-shell";
  std::string lf = log_file();
  if (fork() == 0) {
    setsid();
    if (fork() == 0) {
      execlp(shell.c_str(), "nirvana-shell", "-e", "less", "+G", lf.c_str(), (char *)nullptr);
      _exit(127);
    }
    _exit(0);
  }
  while (waitpid(-1, nullptr, WNOHANG) > 0) {}
}

std::string clock_text(time_t t) {
  char b[16];
  strftime(b, sizeof b, "%H:%M", localtime(&t));
  return b;
}

// A progress bar: a well with the selection color; without a value a moving stripe.
class Progress : public Fl_Widget {
public:
  Progress(int X, int Y, int W) : Fl_Widget(X, Y, W, 12) {}
  void draw() override {
    fl_draw_box(WELL_BOX, x(), y(), w(), h(), CONTENT);
    int iw = w() - 4, ih = h() - 4;
    fl_color(SEL);
    if (g_progress >= 0) {
      fl_rectf(x() + 2, y() + 2, (int)(iw * std::min(100.0, g_progress) / 100), ih);
    } else {   // unknown: a block moving back and forth
      double t = fmod(time_now(), 2.0);
      int bw = iw / 4, pos = (int)((t < 1 ? t : 2 - t) * (iw - bw));
      fl_rectf(x() + 2 + pos, y() + 2, bw, ih);
    }
  }
  static double time_now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
  }
};


class UpdatesApplet : public Fl_Widget {
public:
  Popup *pop = nullptr;
  UpdatesApplet(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {
    g_upd = this;
    tooltip_text();
  }

  bool busy() const { return g_state == State::Checking || g_state == State::Installing; }

  void tooltip_text() {
    static std::string t;
    t = headline();
    tooltip(t.c_str());
  }

  static std::string headline() {
    switch (g_state) {
    case State::Checking: return _("Checking for updates…");
    case State::UpToDate: return _("The system is up to date");
    case State::Available:
      return fmt(trn((long)g_updates.size(), "%1 update available", "%1 updates available"),
                 std::to_string(g_updates.size()));
    case State::Installing:
      return g_progress >= 0 ? fmt(_("Installing updates… %1%"), std::to_string((int)g_progress))
                             : std::string(_("Installing updates…"));
    case State::Done: return _("Updates installed");
    case State::Failed: return _("Updating failed");
    default: return _("Software Update");
    }
  }

  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_draw_box(MENUBAR_BOX, x(), y(), w(), h(), GRAY);
    bool open = pop && pop->shown();
    if (open) { fl_color(SEL); fl_rectf(x(), y(), w(), h() - 1); }
    Fl_Color c = open ? SEL_TEXT : TEXT;
    // a parcel (a box with a lid) with an arrow pointing into it
    int ix = x() + 4, iy = y() + 3;
    fl_color(open ? SEL_TEXT : fl_color_average(TEXT, GRAY, 0.25f));
    fl_rectf(ix + 1, iy + 7, 13, 7);
    fl_color(open ? SEL : GRAY);
    fl_rectf(ix + 2, iy + 8, 11, 5);
    fl_color(c);
    fl_rect(ix, iy + 6, 15, 9);
    fl_xyline(ix, iy + 9, ix + 14);
    fl_rectf(ix + 6, iy + 9, 3, 2);                   // tape
    fl_rectf(ix + 6, iy, 3, 5);                       // arrow shaft
    fl_polygon(ix + 3, iy + 4, ix + 12, iy + 4, ix + 7, iy + 8);
    if (g_state == State::Checking || g_state == State::Installing) {   // a moving dot / progress
      double t = Progress::time_now();
      if (g_state == State::Installing && g_progress >= 0) {
        fl_color(SEL);
        fl_rectf(ix, y() + h() - 3, (int)(14 * g_progress / 100), 2);
      } else {
        int d = (int)(fmod(t * 4, 4));
        fl_color(open ? SEL_TEXT : SEL);
        fl_rectf(ix + 2 + d * 3, y() + h() - 3, 2, 2);
      }
    }
    if (g_state == State::Available && !g_updates.empty()) {   // a red badge with the number
      std::string n = g_updates.size() > 99 ? "99+" : std::to_string(g_updates.size());
      fl_font(FONT_SYSTEM, 9);
      int bw = std::max(11, (int)fl_width(n.c_str()) + 5), bh = 11;
      int bx = x() + 4 + 15 - 5, by = y() + 1;   // over the top right corner of the parcel
      fl_color(fl_rgb_color(0xE5, 0x48, 0x3F));
      fl_pie(bx, by, bh, bh, 90, 270);
      fl_pie(bx + bw - bh, by, bh, bh, 270, 450);
      fl_rectf(bx + bh / 2, by, bw - bh, bh);
      fl_color(FL_WHITE);
      fl_draw(n.c_str(), bx, by, bw, bh, FL_ALIGN_CENTER);
    } else if (g_state == State::Failed) {
      fl_color(fl_rgb_color(0xE5, 0x48, 0x3F));
      fl_font(FONT_SYSTEM, 10);
      fl_draw("!", x() + 4 + 15 + 1, y() + 11);
    }
    fl_pop_clip();
  }

  int handle(int e) override {
    if (e == FL_PUSH) {
      if (pop && pop->shown()) { pop->close(); return 1; }
      open_popup();
      return 1;
    }
    if (e == FL_ENTER || e == FL_LEAVE) return 1;
    return Fl_Widget::handle(e);
  }

  Progress *bar = nullptr;
  Fl_Box *head = nullptr, *sub = nullptr;

  void build() {
    const int W = 320;
    pop->begin();
    pop->clear();
    pop->resizable(nullptr);
    pop->begin();
    int y = 8;
    head = text_box(12, y, W - 24, headline().c_str(), true);
    y += 22;
    std::string s;
    if (g_state == State::Installing) s = g_action;
    else if (g_state == State::Failed) s = g_error;
    else if (g_state == State::Done) s = g_reboot ? _("Restart the computer to finish the update.") : "";
    else if (g_last_check) s = fmt(_("Last checked at %1 (%2)"), clock_text(g_last_check), g_backend);
    if (!s.empty()) {
      sub = text_box(12, y, W - 24, s.c_str(), false, 30);
      sub->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
      y += 32;
    } else {
      sub = nullptr;
    }
    if (!g_warning.empty() && g_state != State::Installing) {
      auto *w = text_box(12, y, W - 24, (std::string(_("Note: ")) + g_warning).c_str(), false, 30);
      w->labelsize(SIZE_SMALL);
      w->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
      y += 32;
    }
    bar = nullptr;
    if (g_state == State::Installing) {
      bar = new Progress(12, y, W - 24);
      y += 20;
    }
    if (g_state == State::Available) {
      const size_t SHOW = 10;
      for (size_t i = 0; i < g_updates.size() && i < SHOW; i++) {
        auto *b = text_box(20, y, W - 32, "", false, 16);
        std::string row = g_updates[i].name + (g_updates[i].version.empty() ? "" : "  " + g_updates[i].version);
        b->copy_label(row.c_str());
        b->labelsize(SIZE_SMALL);
        y += 16;
      }
      if (g_updates.size() > SHOW) {
        std::string more = fmt(trn((long)(g_updates.size() - SHOW), "…and %1 more", "…and %1 more"),
                               std::to_string(g_updates.size() - SHOW));
        auto *b = text_box(20, y, W - 32, more.c_str(), false, 16);
        b->labelsize(SIZE_SMALL);
        y += 16;
      }
      y += 4;
    }
    new Sep(8, y, W - 16);
    y += 8;
    auto row = [&](const char *label, long id, bool active) {
      auto *r = new MenuRow(2, y, W - 4, label);
      r->callback(act, (void *)id);
      if (!active) r->deactivate();
      y += 22;
    };
    row(_("Install Updates…"), 1, g_state == State::Available);
    row(_("Check for Updates"), 2, !busy());
    row(_("Show Log"), 3, !access(log_file().c_str(), R_OK));
    pop->end();
    pop->size(W, y + 4);
  }

  static void act(Fl_Widget *, void *d) {
    if (g_upd && g_upd->pop) g_upd->pop->close();
    switch ((long)d) {
    case 1: confirm_install(); break;
    case 2: check(); break;
    case 3: show_log(); break;
    }
  }

  void open_popup() {
    if (!pop) {
      pop = new Popup(320, 100);
      pop->on_close = [this] {
        for (int i = 0; i < pop->children(); i++)
          if (auto *r = dynamic_cast<MenuRow *>(pop->child(i))) r->hover(false);
        redraw();
      };
      pop->track_hover = [this] { update_hover(pop); };
    }
    build();
    pop->open_at(window()->x_root() + x() + w() - pop->w(), window()->y_root() + MENUBAR_H);
    redraw();
  }

  void sync() {
    tooltip_text();
    redraw();
    if (w() != updates_width() && current_host().relayout) current_host().relayout();
    if (pop && pop->shown()) {
      int oldw = pop->w();
      build();
      pop->position(window()->x_root() + x() + w() - oldw, pop->y());
      pop->redraw();
    }
  }
};

void changed() {
  if (g_upd) g_upd->sync();
}
}  // namespace

// the animation of the icon (and of a bar without a known value) while busy
static void tick(void *) {
  if (g_state != State::Checking && g_state != State::Installing) { Fl::repeat_timeout(1.0, tick); return; }
  if (g_upd) {
    g_upd->redraw();
    if (g_upd->bar) g_upd->bar->redraw();
  }
  Fl::repeat_timeout(0.15, tick);
}

int updates_width() {
  int wdt = 4 + 15 + 4;
  if (g_state == State::Available && !g_updates.empty()) {   // the badge sticks out on the right
    std::string n = g_updates.size() > 99 ? "99+" : std::to_string(g_updates.size());
    fl_font(FONT_SYSTEM, 9);
    int bw = std::max(11, (int)fl_width(n.c_str()) + 5);
    wdt = std::max(wdt, 4 + 15 - 5 + bw + 1);
  } else if (g_state == State::Failed) {
    wdt += 4;
  }
  return wdt;
}

bool updates_supported() {
  const char *path = getenv("PATH");
  std::string dirs = path ? path : "/usr/bin:/bin";
  for (const char *p : {"apt-get", "dnf", "pacman", "zypper", "apk", "xbps-install"}) {
    size_t a = 0;
    while (a <= dirs.size()) {
      size_t b = dirs.find(':', a);
      if (b == std::string::npos) b = dirs.size();
      if (!access((dirs.substr(a, b - a) + "/" + p).c_str(), X_OK)) return true;
      a = b + 1;
    }
  }
  return false;
}

Fl_Widget *make_updates(int X, int Y, int W, int H) {
  auto *a = new UpdatesApplet(X, Y, W, H);
  Fl::add_timeout(60.0, periodic);      // the first check a minute after the start
  Fl::add_timeout(1.0, tick);
  return a;
}

}  // namespace applets
