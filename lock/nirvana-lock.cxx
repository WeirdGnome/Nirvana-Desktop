// Nirvana Desktop - nirvana-lock: the screen saver and the screen lock
//
//   nirvana-lock            lock the screen now (the saver runs until someone moves the mouse,
//                           then the password window)
//   nirvana-lock --saver    the screen saver (Rambler starts it when the computer is idle); any
//                           key or movement ends it - unless the settings lock it, at once or
//                           after a few minutes (Preferences > Screen Saver)
//
// One window over all screens, the keyboard and the mouse grabbed, so nothing reaches the
// programs under it. The password is checked by PAM (/etc/pam.d/nirvana-lock, otherwise "login")
// in a child process, so the screen keeps running while PAM waits after a wrong password.
// Only one copy runs (the X selection _NIRVANA_LOCK); "nirvana-lock" while the saver is on locks it.
#include "platinum.h"
#include "power.h"
#include "savers.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <security/pam_appl.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <pwd.h>
#include <string>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace plat;

namespace {

double now() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + tv.tv_usec / 1e6;
}

bool have_prog(const char *p) {
  std::string cmd = std::string("command -v ") + p + " >/dev/null 2>&1";
  return system(cmd.c_str()) == 0;
}

// --- PAM (in a child process) ----------------------------------------------------------
const char *g_pam_password = nullptr;

int conv(int n, const struct pam_message **msg, struct pam_response **resp, void *) {
  auto *r = (struct pam_response *)calloc((size_t)n, sizeof(struct pam_response));
  if (!r) return PAM_BUF_ERR;
  for (int i = 0; i < n; i++) {
    if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
      r[i].resp = strdup(g_pam_password ? g_pam_password : "");
  }
  *resp = r;
  return PAM_SUCCESS;
}

// 0 = the password is right
[[noreturn]] void pam_child(const std::string &user, const char *password) {
  g_pam_password = password;
  const char *service = access("/etc/pam.d/nirvana-lock", R_OK) == 0 ? "nirvana-lock" : "login";
  struct pam_conv c = {conv, nullptr};
  pam_handle_t *ph = nullptr;
  int rc = pam_start(service, user.c_str(), &c, &ph);
  if (rc == PAM_SUCCESS) rc = pam_authenticate(ph, 0);
  if (rc == PAM_SUCCESS) {
    int ac = pam_acct_mgmt(ph, 0);
    if (ac != PAM_SUCCESS && ac != PAM_NEW_AUTHTOK_REQD) rc = ac;
  }
  if (rc == PAM_SUCCESS) pam_setcred(ph, PAM_REFRESH_CRED);
  pam_end(ph, rc);
  _exit(rc == PAM_SUCCESS ? 0 : 1);
}

// --- the window ------------------------------------------------------------------------
class LockWin;
LockWin *g_win = nullptr;

struct Settings {
  power::IdleSettings idle;
  bool saver_mode = false;
} g_set;

class Face : public Fl_Widget {
public:
  Fl_Image *img = nullptr;
  Face(int X, int Y, int S) : Fl_Widget(X, Y, S, S) {
    const char *h = getenv("HOME");
    std::string f = std::string(h ? h : "/") + "/.face";
    if (!access(f.c_str(), R_OK))
      if (Fl_Shared_Image *si = Fl_Shared_Image::get(f.c_str())) {
        if (si->w() > 0) img = si->copy(S - 4, S - 4);
        si->release();
      }
  }
  void draw() override {
    fl_draw_box(WELL_BOX, x(), y(), w(), h(), CONTENT);
    if (img) { img->draw(x() + 2, y() + 2); return; }
    int S = w();
    fl_color(fl_color_average(SEL, CONTENT, 0.7f));
    fl_pie(x() + S * 16 / 52, y() + S * 8 / 52, S * 20 / 52, S * 20 / 52, 0, 360);
    fl_push_clip(x() + 2, y() + 2, S - 4, S - 4);
    fl_pie(x() + S * 8 / 52, y() + S * 30 / 52, S * 36 / 52, S * 30 / 52, 0, 180);
    fl_pop_clip();
  }
};

class LockWin : public Fl_Double_Window {
public:
  savers::Saver *saver = nullptr;
  bool locked = false, dialog = false, checking = false;
  double started = now(), last = now(), last_input = 0;
  int mx0 = -1, my0 = -1;
  pid_t pam_pid = 0;
  Fl_RGB_Image *bg = nullptr;
  std::string user, full, since, title_s, msg_s, clock_s;
  int minute = -1;
  // the dialog
  int dx = 0, dy = 0, dw = 440, dh = 220;
  Face *face = nullptr;
  Fl_Secret_Input *pass = nullptr;
  Fl_Button *unlock = nullptr, *switch_user = nullptr;
  Fl_Box *msg = nullptr;

  LockWin(int W, int H) : Fl_Double_Window(0, 0, W, H, "Nirvana Lock") {
    struct passwd *pw = getpwuid(getuid());
    user = pw ? pw->pw_name : "?";
    std::string g = pw && pw->pw_gecos ? pw->pw_gecos : "";
    full = g.substr(0, g.find(','));
    if (full.empty()) full = user;
    int sx, sy, sw, sh;
    Fl::screen_xywh(sx, sy, sw, sh, 0);
    dx = sx + (sw - dw) / 2;
    dy = sy + (sh - dh) / 2;
    face = new Face(dx + 24, dy + TITLE_H + 22, 72);
    pass = new Fl_Secret_Input(dx + 200, dy + TITLE_H + 86, dw - 220, 24, _("Password:"));
    pass->labelfont(FONT_SYSTEM);
    pass->labelsize(SIZE_SYSTEM);
    pass->textsize(SIZE_SYSTEM);
    pass->box(WELL_BOX);
    msg = new Fl_Box(dx + 120, dy + TITLE_H + 116, dw - 140, 34);
    msg->box(FL_FLAT_BOX);
    msg->color(GRAY);
    msg->labelsize(SIZE_SMALL + 1);
    msg->labelfont(FONT_SYSTEM);
    msg->labelcolor(ALERT);
    msg->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    unlock = new Fl_Button(dx + dw - 120, dy + dh - 36, 104, 24, _("Unlock"));
    unlock->box(BUTTON_BOX);
    unlock->down_box(BUTTON_DOWN);
    unlock->labelfont(FONT_SYSTEM);
    unlock->labelsize(SIZE_SYSTEM);
    unlock->shortcut(FL_Enter);
    unlock->callback([](Fl_Widget *, void *) { g_win->check(); });
    // another person may log in next to this session (LightDM)
    if (getenv("XDG_SEAT_PATH") && have_prog("dm-tool")) {
      switch_user = new Fl_Button(dx + 16, dy + dh - 36, 130, 24, _("Switch User…"));
      switch_user->box(BUTTON_BOX);
      switch_user->down_box(BUTTON_DOWN);
      switch_user->labelsize(SIZE_SYSTEM);
      switch_user->callback([](Fl_Widget *, void *) {
        if (fork() == 0) { execlp("dm-tool", "dm-tool", "switch-to-greeter", (char *)nullptr); _exit(127); }
        g_win->hide_dialog();
      });
    }
    end();
    for (int i = 0; i < children(); i++) child(i)->hide();
    char t[16];
    time_t n = time(nullptr);
    strftime(t, sizeof t, "%H:%M", localtime(&n));
    since = t;
  }

  void show_dialog() {
    if (dialog) return;
    dialog = true;
    if (!bg) bg = render_desktop(current_appearance(), w(), h());
    for (int i = 0; i < children(); i++) child(i)->show();
    set_msg("");
    pass->value("");
    pass->take_focus();
    cursor(FL_CURSOR_DEFAULT);
    last_input = now();
    redraw();
  }
  void hide_dialog() {
    if (!dialog || checking) return;
    dialog = false;
    pass->value("");
    for (int i = 0; i < children(); i++) child(i)->hide();
    cursor(FL_CURSOR_NONE);
    mx0 = -1;
    redraw();
  }
  void set_msg(const std::string &m) {
    msg_s = m;
    msg->label(msg_s.c_str());
    msg->redraw();
  }

  void check() {
    if (checking) return;
    std::string p = pass->value();
    if (p.empty()) { pass->take_focus(); return; }
    checking = true;
    pass->deactivate();
    unlock->deactivate();
    set_msg(_("Checking…"));
    Fl::flush();
    pam_pid = fork();
    if (pam_pid == 0) pam_child(user, p.c_str());
    std::fill(p.begin(), p.end(), '\0');
    pass->value("");
    if (pam_pid < 0) { checking = false; pass->activate(); unlock->activate(); set_msg(strerror(errno)); }
  }

  void poll_child() {
    if (!checking || pam_pid <= 0) return;
    int st = 0;
    pid_t r = waitpid(pam_pid, &st, WNOHANG);
    if (r == 0) return;
    checking = false;
    pam_pid = 0;
    if (r > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0) { finish(); return; }
    pass->activate();
    unlock->activate();
    pass->take_focus();
    set_msg(_("That password is not right - try again."));
    last_input = now();
  }

  void finish() {
    XUngrabKeyboard(fl_display, CurrentTime);
    XUngrabPointer(fl_display, CurrentTime);
    XFlush(fl_display);
    exit(0);
  }

  void activity(bool key) {
    if (!locked) { finish(); return; }
    if (!dialog) show_dialog();
    last_input = now();
    (void)key;
  }

  int handle(int e) override {
    switch (e) {
      case FL_KEYDOWN:
      case FL_SHORTCUT:
        if (!dialog) {
          activity(true);
          // the first key that is a letter goes into the password
          if (dialog && Fl::event_length() > 0 && (unsigned char)Fl::event_text()[0] >= 32) pass->insert(Fl::event_text());
          return 1;
        }
        last_input = now();
        if (Fl::event_key() == FL_Escape) { hide_dialog(); return 1; }
        break;
      case FL_PUSH:
        if (!dialog) { activity(false); return 1; }
        last_input = now();
        break;
      case FL_MOVE:
      case FL_DRAG:
        if (!dialog) {
          // a small shake of the table is not a person coming back
          if (mx0 < 0) { mx0 = Fl::event_x_root(); my0 = Fl::event_y_root(); return 1; }
          if (abs(Fl::event_x_root() - mx0) + abs(Fl::event_y_root() - my0) > 12) activity(false);
          return 1;
        }
        last_input = now();
        break;
      case FL_MOUSEWHEEL:
        if (!dialog) activity(false);
        return 1;
    }
    return Fl_Double_Window::handle(e);
  }

  void draw() override {
    if (!dialog) {
      if (saver) saver->draw(0, 0);
      else { fl_color(FL_BLACK); fl_rectf(0, 0, w(), h()); }
      return;
    }
    if (damage() & ~FL_DAMAGE_CHILD) {   // otherwise only a field changed - see Nirvana-DM
      if (bg) bg->draw(0, 0);
      else { fl_color(DESK_A); fl_rectf(0, 0, w(), h()); }
      // the menu bar: the logo and the time
      fl_draw_box(MENUBAR_BOX, 0, 0, w(), MENUBAR_H, GRAY);
      draw_logo(8, 2, 16);
      fl_font(FONT_SYSTEM, SIZE_SYSTEM);
      fl_color(TEXT);
      clock_s = clock_text(read_clock(), time(nullptr), false);
      fl_draw(clock_s.c_str(), 0, 0, w() - 10, MENUBAR_H, FL_ALIGN_RIGHT);
      fl_draw_box(UP_BOX, dx, dy, dw, dh, GRAY);
      fl_color(DARK);
      fl_rect(dx - 1, dy - 1, dw + 2, dh + 2);
      title_s = _("This Computer Is Locked");
      draw_titlebar(dx, dy, dw, title_s.c_str(), true, 0, 0);
      fl_font(FONT_SYSTEM, SIZE_SYSTEM + 2);
      fl_color(TEXT);
      fl_draw(full.c_str(), dx + 120, dy + TITLE_H + 38);
      fl_font(FONT_SMALL, SIZE_SMALL + 1);
      std::string s = fmt(_("Locked at %1. Type your password to come back."), since);
      fl_draw(s.c_str(), dx + 120, dy + TITLE_H + 44, dw - 136, 30, FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP);
    }
    draw_children();
  }

  void tick() {
    double t = now(), dt = std::min(0.2, t - last);
    last = t;
    poll_child();
    if (!locked && g_set.idle.lock_on_blank && (!g_set.saver_mode || t - started >= g_set.idle.lock_delay_min * 60))
      locked = true;
    if (dialog) {
      if (!checking && t - last_input > 30) hide_dialog();   // nobody there: back to the saver
      int m = (int)(time(nullptr) / 60);
      if (m != minute) { minute = m; damage(FL_DAMAGE_ALL, w() - 260, 0, 260, MENUBAR_H); }
    } else if (saver) {
      saver->step(dt);
      redraw();
    }
    // stay on top of everything (a window that maps later must not cover the lock)
    static double raised = 0;
    if (t - raised > 1) { XRaiseWindow(fl_display, fl_xid(this)); raised = t; }
  }
};

Atom g_lock_now;

int x_events(void *event, void *) {
  XEvent *e = (XEvent *)event;
  if (e->type == PropertyNotify && e->xproperty.window == DefaultRootWindow(fl_display) &&
      e->xproperty.atom == g_lock_now && g_win)
    g_win->locked = true;   // "nirvana-lock" while the saver runs: lock it
  if (e->type == SelectionClear) {
    // someone took the selection (should not happen) - keep running
  }
  return 0;
}

void frame(void *) {
  g_win->tick();
  Fl::repeat_timeout(1.0 / 30, frame);
}

bool grab(Window w) {
  for (int i = 0; i < 40; i++) {   // up to 2 s: a menu may hold the keyboard for a moment
    int k = XGrabKeyboard(fl_display, w, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    if (k == GrabSuccess) {
      int p = XGrabPointer(fl_display, w, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                           GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
      if (p == GrabSuccess) return true;
      XUngrabKeyboard(fl_display, CurrentTime);
    }
    usleep(50000);
  }
  return false;
}

}  // namespace

int main(int argc, char **argv) {
  bool saver_mode = argc > 1 && !strcmp(argv[1], "--saver");
  init();
  fl_register_images();
  Fl::visual(FL_RGB);
  fl_open_display();
  Display *d = fl_display;
  Window root = DefaultRootWindow(d);
  Atom sel = XInternAtom(d, "_NIRVANA_LOCK", False);
  g_lock_now = XInternAtom(d, "_NIRVANA_LOCK_NOW", False);

  // already running: a lock request locks it, a second saver is not needed
  if (XGetSelectionOwner(d, sel) != None) {
    if (!saver_mode) {
      long v = (long)time(nullptr);
      XChangeProperty(d, root, g_lock_now, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&v, 1);
      XFlush(d);
    }
    return 0;
  }

  g_set.idle = power::read_idle();
  g_set.saver_mode = saver_mode;
  if (!saver_mode) g_set.idle.lock_on_blank = true, g_set.idle.lock_delay_min = 0;
  if (getenv("NIRVANA_SAVER_TEST")) g_set.idle.lock_on_blank = false;   // Preferences > Test: never locks
  // the session may die, the lock must not: no signals from the terminal
  for (int s : {SIGINT, SIGQUIT, SIGHUP, SIGTSTP, SIGTERM, SIGUSR1, SIGUSR2}) signal(s, SIG_IGN);

  int W = DisplayWidth(d, DefaultScreen(d)), H = DisplayHeight(d, DefaultScreen(d));
  auto *win = new LockWin(W, H);
  g_win = win;
  win->locked = !saver_mode ? true : (g_set.idle.lock_on_blank && g_set.idle.lock_delay_min == 0);
  std::string id = g_set.idle.saver.empty() ? "stars" : g_set.idle.saver;
  if (const char *e = getenv("NIRVANA_SAVER")) id = e;
  win->saver = savers::make(id, W, H);
  win->border(0);
  win->clear_border();
  win->xclass("nirvana-lock");
  win->set_override();
  win->show();
  win->cursor(FL_CURSOR_NONE);
  Fl::flush();
  XSync(d, False);
  if (!grab(fl_xid(win))) {
    fprintf(stderr, "nirvana-lock: could not grab the keyboard and mouse - the screen is not locked\n");
    return 1;
  }
  XSetSelectionOwner(d, sel, fl_xid(win), CurrentTime);
  XWindowAttributes wa;
  XGetWindowAttributes(d, root, &wa);
  XSelectInput(d, root, wa.your_event_mask | PropertyChangeMask);
  Fl::add_system_handler(x_events, nullptr);
  Fl::add_timeout(1.0 / 30, frame);
  for (;;) Fl::wait(1.0);
}
