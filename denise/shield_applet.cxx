// Nirvana Desktop - the NirvanaSHIELD applet in the menu bar
//
// A shield: green with a check mark (protecting), red (all internet blocked), gray
// (protection off), an outline (the service is not running). The menu switches between the
// three, lists the programs that just asked for the internet ("Allow ...") and opens the
// panel. When a program that is not allowed tries to get out, a notification asks:
// "May Firefox use the internet?" [Allow] [No].
#include "applets.h"
#include "platinum.h"
#include "shield.h"

#include <FL/Fl.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cerrno>
#include <ctime>
#include <map>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace plat;

namespace applets {

namespace {

class ShieldMenu;
ShieldMenu *g_shield;
shield::Status g_st;
std::vector<shield::Blocked> g_recent;
std::vector<shield::Program> g_progs;
time_t g_progs_time = 0;
int g_watch = -1;
std::string g_buf;

std::string home() { const char *h = getenv("HOME"); return h ? h : "/"; }

const std::vector<shield::Program> &progs() {
  if (time(nullptr) - g_progs_time > 60) {   // the menus change rarely
    g_progs = shield::programs(home() + "/.nirvana-apps");
    g_progs_time = time(nullptr);
  }
  return g_progs;
}

// all the program files of the program a file belongs to (a launcher script and its program)
std::vector<std::string> group_of(const std::string &path) {
  for (auto &p : progs())
    for (auto &f : p.paths)
      if (f == path) return p.paths;
  return {path};
}

void refresh();

// A change; with the parental lock through pkexec - in a child process, so that Rambler
// keeps working while the password window is open.
void change(const std::vector<std::string> &cmds) {
  if (!g_st.lock) {
    bool ok = true;
    for (auto &c : cmds) {
      std::string r;
      if (!shield::request(c, r) || r.rfind("ok", 0) != 0) { ok = false; break; }
    }
    if (ok) { refresh(); return; }
  }
  pid_t pid = fork();
  if (pid == 0) {
    shield::admin(cmds);
    _exit(0);
  }
  Fl::add_timeout(1.0, [](void *) {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
    refresh();
  });
}

void allow_path(const std::string &path) {
  std::vector<std::string> cmds;
  for (auto &f : group_of(path)) cmds.push_back("ALLOW " + f);
  change(cmds);
}

int state() { return !g_st.running ? 3 : g_st.mode == "protect" ? 1 : g_st.mode == "block" ? 2 : 0; }

class ShieldMenu : public Fl_Menu_Button {
public:
  std::vector<std::string> labels_, paths_;
  std::vector<Fl_Menu_Item> items_;
  ShieldMenu(int X, int Y, int W, int H) : Fl_Menu_Button(X, Y, W, H) {
    box(FL_FLAT_BOX);
    down_box(FL_FLAT_BOX);
    textfont(FONT_SYSTEM);
    textsize(SIZE_SYSTEM);
    selection_color(SEL);
    tooltip("NirvanaSHIELD");
  }
  void build() {
    labels_.clear();
    paths_.clear();
    items_.clear();
    labels_.reserve(24);
    struct It { std::string l; Fl_Callback *cb; long v; int flags; };
    std::vector<It> its;
    if (!g_st.running) {
      its.push_back({_("NirvanaSHIELD is switched off"), nullptr, 0, FL_MENU_INACTIVE});
      its.push_back({_("Switch NirvanaSHIELD on…"), panel_cb, 0, 0});
    } else {
      its.push_back({"NirvanaSHIELD", nullptr, 0, FL_MENU_INACTIVE});
      auto radio = [&](const char *l, const char *m) {
        its.push_back({l, mode_cb, (long)(m[0]), FL_MENU_RADIO | (g_st.mode == m ? FL_MENU_VALUE : 0)});
      };
      radio(_("Protect (only allowed programs)"), "protect");
      radio(_("Block All Internet"), "block");
      radio(_("Protection Off"), "off");
      its.back().flags |= FL_MENU_DIVIDER;
      int n = 0;
      for (auto &b : g_recent) {
        if (n++ >= 6) break;
        std::string name = shield::name_of(b.path, progs());
        std::string l;
        for (char c : fmt(_("Allow “%1”"), name)) { if (c == '&') l += "&&"; else if (c == '/') l += "\\/"; else l += c; }
        paths_.push_back(b.path);
        its.push_back({l, allow_cb, (long)paths_.size() - 1, 0});
      }
      if (n) its.back().flags |= FL_MENU_DIVIDER;
      its.push_back({_("Programs and the Internet…"), panel_cb, 0, 0});
    }
    for (auto &i : its) {
      labels_.push_back(i.l);
      Fl_Menu_Item m{};
      m.text = labels_.back().c_str();
      m.callback_ = i.cb;
      m.user_data_ = (void *)i.v;
      m.flags = i.flags;
      m.labelfont_ = FONT_SYSTEM;
      m.labelsize_ = SIZE_SYSTEM;
      items_.push_back(m);
    }
    items_.push_back(Fl_Menu_Item{});
    menu(items_.data());
  }
  static void mode_cb(Fl_Widget *, void *v) {
    char c = (char)(long)v;
    change({std::string("MODE ") + (c == 'p' ? "protect" : c == 'b' ? "block" : "off")});
  }
  static void allow_cb(Fl_Widget *w, void *v) {
    auto *m = (ShieldMenu *)w;
    size_t i = (size_t)(long)v;
    if (i < m->paths_.size()) allow_path(m->paths_[i]);
  }
  static void panel_cb(Fl_Widget *, void *) {
    if (current_host().open_panel) current_host().open_panel("shield");
  }
  int handle(int e) override {
    if (e == FL_PUSH) {
      refresh();
      build();
      open_ = true; redraw(); Fl::flush();
      int r = Fl_Menu_Button::handle(e);
      open_ = false; redraw();
      return r;
    }
    return Fl_Menu_Button::handle(e);
  }
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    draw_box(MENUBAR_BOX, GRAY);
    if (open_) { fl_color(selection_color()); fl_rectf(x(), y(), w(), h() - 1); }
    draw_shield(x() + (w() - 12) / 2, y() + 3, 14, state());
    fl_pop_clip();
  }
private:
  bool open_ = false;
};

void refresh() {
  g_st = shield::status();
  g_recent = g_st.running ? shield::blocked() : std::vector<shield::Blocked>{};
  if (g_shield) g_shield->redraw();
}

// A program asked for the internet
void asked(const std::string &path, const std::string &dest) {
  std::string icon;
  std::string name = shield::name_of(path, progs(), &icon);
  (void)dest;
  notify_local("NirvanaSHIELD", fmt(_("May “%1” use the internet?"), name),
               _("NirvanaSHIELD stopped it. Allow it only if you know this program and trust it."),
               icon, {{"allow", _("Allow")}, {"no", _("No")}},
               [path](const std::string &key) {
                 if (key == "allow") allow_path(path);
               });
  refresh();
}

void connect_watch(void *);
void watch_cb(int fd, void *) {
  char buf[2048];
  ssize_t n = recv(fd, buf, sizeof buf, MSG_DONTWAIT);
  if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
  if (n <= 0) {   // the service stopped
    Fl::remove_fd(fd);
    close(fd);
    g_watch = -1;
    refresh();
    Fl::add_timeout(10.0, connect_watch);
    return;
  }
  g_buf.append(buf, n);
  size_t nl;
  while ((nl = g_buf.find('\n')) != std::string::npos) {
    std::string l = g_buf.substr(0, nl);
    g_buf.erase(0, nl + 1);
    if (l.rfind("blocked ", 0) == 0) {
      std::string rest = l.substr(8);
      size_t t = rest.find('\t');
      asked(rest.substr(0, t), t == std::string::npos ? "" : rest.substr(t + 1));
    }
  }
}

void connect_watch(void *) {
  if (g_watch >= 0) return;
  g_watch = shield::watch_fd();
  if (g_watch < 0) { Fl::repeat_timeout(10.0, connect_watch); return; }
  g_buf.clear();
  Fl::add_fd(g_watch, FL_READ, watch_cb);
  refresh();
}

void tick(void *) {   // the mode may be changed in the panel
  int s = state();
  bool lock = g_st.lock;
  g_st = shield::status();
  if (state() != s || g_st.lock != lock) refresh();
  if (g_st.running && g_watch < 0) connect_watch(nullptr);
  Fl::repeat_timeout(5.0, tick);
}

}  // namespace

Fl_Widget *make_shield(int X, int Y, int W, int H) {
  g_shield = new ShieldMenu(X, Y, W, H);
  refresh();
  connect_watch(nullptr);
  Fl::add_timeout(5.0, tick);
  return g_shield;
}

}  // namespace applets
