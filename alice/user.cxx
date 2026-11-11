// Nirvana Desktop - alice: the "User" panel
//
// The account of the person logged in: the picture (shown by the login screen, Nirvana-DM),
// the full name, the password, automatic login and a few facts about the account.
//
// The picture is saved as ~/.face (other programs read it too) and, through
// nirvana-user-helper (pkexec, polkit action org.nirvana-desktop.user), in
// /var/lib/AccountsService/icons - the login screen runs as another user and usually cannot
// look into a home folder. The full name and automatic login go through the same helper; the
// password is changed by the system program passwd, as when typing it in a terminal.
#include "filedialog.h"
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <grp.h>
#include <poll.h>
#include <pty.h>
#include <pwd.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace plat;

namespace {

const int FACE = 96;            // the size of the saved picture
const int N_AVATARS = 8;
const char *AUTOLOGIN_CONF = "/etc/lightdm/lightdm.conf.d/60-nirvana-autologin.conf";

FrameWindow *g_win;
Fl_Input *g_name;
Fl_Check_Button *g_autologin;
Fl_Box *g_msg;
Fl_Widget *g_face_w;
std::string g_user, g_home, g_shell, g_full, g_groups;
bool g_admin = false, g_autologin_now = false;
uid_t g_uid;

// the picture: what is saved now, and what Apply will save
Fl_RGB_Image *g_face = nullptr;      // the current one (96x96) or null: a figure
Fl_RGB_Image *g_pending = nullptr;   // a new one
bool g_remove = false;               // Apply removes the picture

void message(const std::string &m) {
  static std::string t;
  t = m;
  g_msg->label(t.c_str());
  g_msg->redraw();
}

std::string exe_dir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return "";
  buf[n] = 0;
  std::string p = buf;
  return p.substr(0, p.rfind('/'));
}

std::string helper() {
  std::string e = exe_dir();
  for (const std::string &c : {e + "/../libexec/nirvana-user-helper", e + "/../update/nirvana-user-helper",
                               std::string("/usr/libexec/nirvana-user-helper")})
    if (!access(c.c_str(), R_OK)) return c;
  return "";
}

// runs the helper as root (stdin from a file when given); the message goes to *out
int run_helper(const std::vector<std::string> &args, std::string *out, const std::string &input = "") {
  std::string h = helper();
  if (h.empty()) { *out = _("nirvana-user-helper is missing."); return 127; }
  int p[2];
  if (pipe(p)) return 127;
  pid_t pid = fork();
  if (pid == 0) {
    if (!input.empty()) {
      int fd = open(input.c_str(), O_RDONLY);
      if (fd >= 0) { dup2(fd, 0); close(fd); }
    }
    dup2(p[1], 1);
    dup2(p[1], 2);
    close(p[0]);
    close(p[1]);
    std::vector<char *> a = {(char *)"pkexec", (char *)h.c_str()};
    for (auto &s : args) a.push_back((char *)s.c_str());
    a.push_back(nullptr);
    execvp("pkexec", a.data());
    _exit(127);
  }
  close(p[1]);
  char buf[512];
  ssize_t n;
  out->clear();
  while ((n = read(p[0], buf, sizeof buf)) != 0) {   // the window lives while polkit asks
    if (n > 0) out->append(buf, (size_t)n);
    else if (errno != EINTR) break;
    Fl::check();
  }
  close(p[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  while (!out->empty() && (out->back() == '\n' || out->back() == ' ' || out->back() == '.')) out->pop_back();
  return WIFEXITED(st) ? WEXITSTATUS(st) : 127;
}

// --- pictures -------------------------------------------------------------------------

// draws an image so that it covers the square (S x S) - the middle of a wider or taller one
void draw_cover(Fl_Image *src, int X, int Y, int S) {
  if (!src || src->w() <= 0 || src->h() <= 0) return;
  double k = std::max((double)S / src->w(), (double)S / src->h());
  int w = std::max(S, (int)std::lround(src->w() * k)), h = std::max(S, (int)std::lround(src->h() * k));
  // a shared image's copy belongs to its cache (released, not deleted): scale the image inside
  const Fl_Image *base = src;
  if (auto *sh = dynamic_cast<Fl_Shared_Image *>(src)) if (sh->image()) base = sh->image();
  Fl_Image *c = base->copy(w, h);
  fl_push_clip(X, Y, S, S);
  c->draw(X - (w - S) / 2, Y - (h - S) / 2);
  fl_pop_clip();
  delete c;
}

void vgrad(int X, int Y, int W, int H, Fl_Color a, Fl_Color b) {
  for (int i = 0; i < H; i++) {
    fl_color(fl_color_average(b, a, H > 1 ? (float)i / (H - 1) : 0));
    fl_xyline(X, Y + i, X + W - 1);
  }
}

Fl_Color rgb(unsigned v) { return fl_rgb_color(v >> 16, v >> 8 & 255, v & 255); }

// Built-in pictures, drawn here (no image files): in a square of size S.
void draw_avatar(int i, int X, int Y, int S) {
  auto P = [&](double v) { return (int)std::lround(v * S / 96.0); };   // 96-unit grid
  fl_push_clip(X, Y, S, S);
  switch (i) {
    case 0: {   // a sunset over the sea
      vgrad(X, Y, S, P(60), rgb(0xF6B24A), rgb(0xE0566B));
      fl_color(rgb(0xFFE38A));
      fl_pie(X + P(30), Y + P(36), P(36), P(36), 0, 180);
      vgrad(X, Y + P(60), S, S - P(60), rgb(0x2C5F9E), rgb(0x173764));
      fl_color(rgb(0xFFE38A));
      for (int k = 0; k < 4; k++) fl_xyline(X + P(36 + k * 2), Y + P(66 + k * 7), X + P(60 - k * 2));
      break;
    }
    case 1: {   // mountains
      vgrad(X, Y, S, S, rgb(0x8CC8F0), rgb(0xD8EEFA));
      fl_color(rgb(0x7A8496));
      fl_polygon(X + P(4), Y + P(80), X + P(40), Y + P(24), X + P(76), Y + P(80));
      fl_color(rgb(0x5D6678));
      fl_polygon(X + P(40), Y + P(80), X + P(68), Y + P(40), X + P(96), Y + P(80));
      fl_color(rgb(0xFFFFFF));
      fl_polygon(X + P(31), Y + P(38), X + P(40), Y + P(24), X + P(49), Y + P(38));
      fl_color(rgb(0x4E9A4A));
      fl_rectf(X, Y + P(78), S, S - P(78));
      break;
    }
    case 2: {   // a flower
      vgrad(X, Y, S, S, rgb(0x9ED38A), rgb(0x5FA35A));
      fl_color(rgb(0x3D7A36));
      fl_rectf(X + P(46), Y + P(50), P(4), P(46));
      for (int k = 0; k < 6; k++) {
        double a = k * M_PI / 3;
        fl_color(rgb(0xF4F0FF));
        int cx = X + P(48 + 16 * cos(a)), cy = Y + P(40 + 16 * sin(a));
        fl_pie(cx - P(11), cy - P(11), P(22), P(22), 0, 360);
      }
      fl_color(rgb(0xF2C230));
      fl_pie(X + P(38), Y + P(30), P(20), P(20), 0, 360);
      break;
    }
    case 3: {   // a ringed planet among the stars
      vgrad(X, Y, S, S, rgb(0x2A1F4F), rgb(0x0C0B1E));
      fl_color(rgb(0xFFFFFF));
      const int st[][2] = {{10, 14}, {80, 10}, {70, 80}, {16, 74}, {50, 8}, {88, 50}, {6, 44}};
      for (auto &s : st) fl_rectf(X + P(s[0]), Y + P(s[1]), std::max(1, P(2)), std::max(1, P(2)));
      fl_color(rgb(0xE59A4A));
      fl_pie(X + P(28), Y + P(28), P(40), P(40), 0, 360);
      fl_color(rgb(0xF5D7A0));
      fl_line_style(FL_SOLID, std::max(1, P(3)));
      fl_arc(X + P(8), Y + P(40), P(80), P(16), 180, 360);
      fl_line_style(0);
      break;
    }
    case 4: {   // a fish
      vgrad(X, Y, S, S, rgb(0x5EC8C8), rgb(0x1E7E9A));
      fl_color(rgb(0xF07A2E));
      fl_polygon(X + P(62), Y + P(48), X + P(86), Y + P(30), X + P(86), Y + P(66));
      fl_pie(X + P(14), Y + P(28), P(56), P(40), 0, 360);
      fl_color(rgb(0xFFFFFF));
      fl_pie(X + P(24), Y + P(38), P(10), P(10), 0, 360);
      fl_color(rgb(0x1A1A1A));
      fl_pie(X + P(27), Y + P(41), P(5), P(5), 0, 360);
      fl_color(rgb(0xD8F4F4));
      fl_arc(X + P(70), Y + P(10), P(8), P(8), 0, 360);
      fl_arc(X + P(76), Y + P(20), P(6), P(6), 0, 360);
      break;
    }
    case 5: {   // a rocket
      vgrad(X, Y, S, S, rgb(0x6A4FC8), rgb(0x2E2270));
      fl_color(rgb(0xE5483F));
      fl_polygon(X + P(36), Y + P(60), X + P(26), Y + P(78), X + P(38), Y + P(74));
      fl_polygon(X + P(60), Y + P(60), X + P(70), Y + P(78), X + P(58), Y + P(74));
      fl_color(rgb(0xF2C230));
      fl_polygon(X + P(40), Y + P(76), X + P(48), Y + P(94), X + P(56), Y + P(76));
      fl_color(rgb(0xF4F4F0));
      fl_pie(X + P(36), Y + P(10), P(24), P(40), 0, 180);
      fl_rectf(X + P(36), Y + P(30), P(24), P(46));
      fl_color(rgb(0x3A7BD5));
      fl_pie(X + P(41), Y + P(36), P(14), P(14), 0, 360);
      break;
    }
    case 6: {   // a cup of tea
      vgrad(X, Y, S, S, rgb(0xF3E2C4), rgb(0xD9B98A));
      fl_color(rgb(0xFFFFFF));
      fl_line_style(FL_SOLID, std::max(1, P(5)));
      fl_arc(X + P(56), Y + P(46), P(22), P(22), -90, 90);
      fl_line_style(0);
      fl_rectf(X + P(22), Y + P(40), P(42), P(40));
      fl_color(rgb(0x8A5A2E));
      fl_rectf(X + P(26), Y + P(44), P(34), P(6));
      fl_color(rgb(0xFFFFFF));
      fl_line_style(FL_SOLID, std::max(1, P(2)));
      for (int k = 0; k < 3; k++) fl_arc(X + P(28 + k * 11), Y + P(14), P(8), P(20), 100, 260);
      fl_line_style(0);
      break;
    }
    default: {   // a tree
      vgrad(X, Y, S, S, rgb(0xFFF4C8), rgb(0xF2D27A));
      fl_color(rgb(0x7A4E2A));
      fl_rectf(X + P(43), Y + P(52), P(10), P(36));
      fl_color(rgb(0x3E8E41));
      fl_pie(X + P(22), Y + P(16), P(34), P(34), 0, 360);
      fl_pie(X + P(40), Y + P(12), P(34), P(34), 0, 360);
      fl_pie(X + P(30), Y + P(32), P(38), P(30), 0, 360);
      fl_color(rgb(0x5DA83A));
      fl_pie(X + P(32), Y + P(18), P(16), P(14), 0, 360);
      fl_color(rgb(0x8BB86A));
      fl_rectf(X, Y + P(86), S, S - P(86));
      break;
    }
  }
  fl_pop_clip();
}

// a 96x96 picture made by a drawing function
template <class F> Fl_RGB_Image *render(F f) {
  Fl_Image_Surface surf(FACE, FACE);
  Fl_Surface_Device::push_current(&surf);
  fl_color(FL_WHITE);
  fl_rectf(0, 0, FACE, FACE);
  f();
  Fl_RGB_Image *img = surf.image();
  Fl_Surface_Device::pop_current();
  return img;
}

void draw_figure(int X, int Y, int S, Fl_Color c) {
  auto P = [&](double v) { return (int)std::lround(v * S / 52.0); };
  fl_color(c);
  fl_pie(X + P(16), Y + P(8), P(20), P(20), 0, 360);
  fl_pie(X + P(8), Y + P(30), P(36), P(30), 0, 180);
  fl_color(DARK);
  fl_arc(X + P(16), Y + P(8), P(20), P(20), 0, 360);
}

// the big picture in the panel
class FaceBox : public Fl_Widget {
public:
  FaceBox(int X, int Y) : Fl_Widget(X, Y, FACE + 8, FACE + 8) {}
  void draw() override {
    fl_draw_box(WELL_BOX, x(), y(), w(), h(), CONTENT);
    int X = x() + 4, Y = y() + 4;
    Fl_RGB_Image *img = g_pending ? g_pending : (g_remove ? nullptr : g_face);
    if (img) {
      img->draw(X, Y);
    } else {
      fl_push_clip(X, Y, FACE, FACE);
      draw_figure(X, Y, FACE, fl_color_average(SEL, CONTENT, 0.7f));
      fl_pop_clip();
    }
  }
};

// a built-in picture to click
class AvatarButton : public Fl_Button {
public:
  int idx;
  AvatarButton(int X, int Y, int i) : Fl_Button(X, Y, 44, 44), idx(i) { box(FL_NO_BOX); tooltip(_("Use this picture")); }
  void draw() override {
    fl_draw_box(Fl::belowmouse() == this ? UP_BOX : WELL_BOX, x(), y(), w(), h(), CONTENT);
    draw_avatar(idx, x() + 3, y() + 3, w() - 6);
  }
  int handle(int e) override {
    if (e == FL_ENTER || e == FL_LEAVE) redraw();
    return Fl_Button::handle(e);
  }
};

void set_pending(Fl_RGB_Image *img) {
  delete g_pending;
  g_pending = img;
  g_remove = false;
  g_face_w->redraw();
  message(_("Click Apply or OK to save the picture."));
}

void choose_file() {
  std::string chosen = plat::choose_file(_("Choose Picture"), {{_("Images"), {"*.png", "*.jpg", "*.jpeg", "*.gif", "*.bmp", "*.svg", "*.svgz", "*.xpm"}}}, pictures_dir());
  if (chosen.empty()) return;
  const char *f = chosen.c_str();
  Fl_Shared_Image *img = Fl_Shared_Image::get(f);
  if (!img || img->w() <= 0) {
    if (img) img->release();
    message(_("This file is not a picture that can be opened."));
    return;
  }
  set_pending(render([&] { draw_cover(img, 0, 0, FACE); }));
  img->release();
}

// --- the account ---------------------------------------------------------------------

void read_account() {
  g_uid = getuid();
  struct passwd *pw = getpwuid(g_uid);
  if (pw) {
    g_user = pw->pw_name;
    g_home = pw->pw_dir;
    g_shell = pw->pw_shell ? pw->pw_shell : "";
    std::string gecos = pw->pw_gecos ? pw->pw_gecos : "";
    g_full = gecos.substr(0, gecos.find(','));
    int n = 64;
    std::vector<gid_t> gs(n);
    if (getgrouplist(pw->pw_name, pw->pw_gid, gs.data(), &n) < 0) {
      gs.resize(n);
      getgrouplist(pw->pw_name, pw->pw_gid, gs.data(), &n);
    }
    gs.resize(std::max(0, n));
    for (gid_t g : gs) {
      struct group *gr = getgrgid(g);
      if (!gr) continue;
      std::string name = gr->gr_name;
      if (name == "sudo" || name == "wheel" || name == "admin") g_admin = true;
      if (!g_groups.empty()) g_groups += ", ";
      g_groups += name;
    }
  } else {
    const char *u = getenv("USER");
    g_user = u ? u : "?";
    const char *h = getenv("HOME");
    g_home = h ? h : "/";
  }
  std::ifstream in(AUTOLOGIN_CONF);
  std::string l;
  while (std::getline(in, l)) if (l == "autologin-user=" + g_user) g_autologin_now = true;

}

// the current picture - drawn off screen, so only once the window is shown
void read_face(void *) {
  std::string face = g_home + "/.face";
  if (access(face.c_str(), R_OK)) return;
  if (Fl_Shared_Image *img = Fl_Shared_Image::get(face.c_str())) {
    if (img->w() > 0) g_face = render([&] { draw_cover(img, 0, 0, FACE); });
    img->release();
  }
  g_face_w->redraw();
}

bool apply() {   // true: everything was saved
  std::vector<std::string> done, failed;
  std::string out;
  // the picture
  std::string face = g_home + "/.face";
  if (g_pending) {
    std::string tmp = face + ".new";
    if (fl_write_png(tmp.c_str(), g_pending) == 0 && rename(tmp.c_str(), face.c_str()) == 0) {
      chmod(face.c_str(), 0644);
      delete g_face;
      g_face = (Fl_RGB_Image *)g_pending->copy();
      if (run_helper({"picture"}, &out, face) == 0) done.push_back(_("the picture"));
      else failed.push_back(fmt(_("the login screen picture (%1)"), out.empty() ? _("not allowed") : out));
    } else {
      unlink(tmp.c_str());
      failed.push_back(fmt(_("the picture (%1)"), strerror(errno)));
    }
    delete g_pending;
    g_pending = nullptr;
  } else if (g_remove) {
    unlink(face.c_str());
    delete g_face;
    g_face = nullptr;
    g_remove = false;
    if (run_helper({"picture-remove"}, &out) == 0) done.push_back(_("the picture"));
    else failed.push_back(fmt(_("the login screen picture (%1)"), out.empty() ? _("not allowed") : out));
  }
  // the full name
  std::string name = g_name->value();
  if (name != g_full) {
    if (run_helper({"name", name}, &out) == 0) {
      g_full = name;
      done.push_back(_("the full name"));
    } else {
      failed.push_back(fmt(_("the full name (%1)"), out.empty() ? _("not allowed") : out));
    }
  }
  // automatic login
  if (g_autologin->active() && (bool)g_autologin->value() != g_autologin_now) {
    bool on = g_autologin->value();
    if (run_helper({"autologin", on ? "on" : "off"}, &out) == 0) {
      g_autologin_now = on;
      done.push_back(_("automatic login"));
    } else {
      g_autologin->value(g_autologin_now);
      failed.push_back(fmt(_("automatic login (%1)"), out.empty() ? _("not allowed") : out));
    }
  }
  g_face_w->redraw();
  if (!failed.empty()) {
    std::string f;
    for (auto &s : failed) f += (f.empty() ? "" : "; ") + s;
    message(fmt(_("Not changed: %1."), f));
    return false;
  } else if (!done.empty()) {
    std::string d;
    for (auto &s : done) d += (d.empty() ? "" : ", ") + s;
    message(fmt(_("Saved: %1."), d));
  } else {
    message("");
  }
  return true;
}

// --- the password --------------------------------------------------------------------

// Runs passwd in a pseudo-terminal and answers its questions. Returns "" when the password
// was changed, otherwise what went wrong.
std::string change_password(const std::string &cur, const std::string &nw) {
  int master;
  pid_t pid = forkpty(&master, nullptr, nullptr, nullptr);
  if (pid < 0) return strerror(errno);
  if (pid == 0) {
    setenv("LC_ALL", "C", 1);   // the questions are recognized in English
    execlp("passwd", "passwd", (char *)nullptr);
    _exit(127);
  }
  std::string buf, all;
  bool sent_cur = false, sent_new = false, sent_again = false, rejected = false;
  time_t start = time(nullptr);
  for (;;) {
    struct pollfd p = {master, POLLIN, 0};
    int r = poll(&p, 1, 50);
    Fl::check();
    if (time(nullptr) - start > 30) { kill(pid, SIGTERM); all += "\ntimeout"; break; }
    if (r <= 0) continue;
    char b[512];
    ssize_t n = read(master, b, sizeof b);
    if (n <= 0) break;   // EIO: passwd is gone
    buf.append(b, (size_t)n);
    all.append(b, (size_t)n);
    std::string low = buf;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    size_t e = low.find_last_not_of(" \t");
    if (e == std::string::npos || low[e] != ':' || low.find("password") == std::string::npos) continue;
    std::string answer;
    if (low.find("retype") != std::string::npos || low.find("again") != std::string::npos || low.find("repeat") != std::string::npos) {
      if (sent_again) { rejected = true; break; }
      answer = nw;
      sent_again = true;
    } else if (low.find("new") != std::string::npos) {
      if (sent_new) { rejected = true; break; }   // asked again: the new password was not accepted
      answer = nw;
      sent_new = true;
    } else {
      if (sent_cur) { rejected = true; break; }
      answer = cur;
      sent_cur = true;
    }
    answer += "\n";
    if (write(master, answer.data(), answer.size()) < 0) break;
    buf.clear();
  }
  if (rejected) kill(pid, SIGTERM);
  close(master);
  int st = 0;
  waitpid(pid, &st, 0);
  if (!rejected && WIFEXITED(st) && WEXITSTATUS(st) == 0) return "";
  // the last meaningful line of what passwd said
  std::string reason;
  size_t pos = 0;
  while (pos < all.size()) {
    size_t nl = all.find_first_of("\r\n", pos);
    std::string l = all.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? all.size() : nl + 1;
    while (!l.empty() && l.back() == ' ') l.pop_back();
    if (l.empty() || (l.back() == ':' && l.find("assword") != std::string::npos)) continue;
    if (l.find("Changing password") != std::string::npos) continue;
    reason = l;
  }
  if (sent_cur && !sent_new) reason = _("the current password is not right");
  return reason.empty() ? _("passwd did not accept it") : reason;
}

void password_dialog() {
  const int W = 380, H = 200;
  auto *d = new FrameWindow(W, H, _("Change Password"), true);
  auto field = [&](int Y, const char *l) {
    auto *i = new Fl_Secret_Input(170, Y, W - 185, 22, l);
    i->labelsize(SIZE_SMALL + 1);
    i->textsize(SIZE_SMALL + 1);
    return i;
  };
  auto *cur = field(14, _("Current password:"));
  auto *nw = field(44, _("New password:"));
  auto *again = field(74, _("Repeat new password:"));
  auto *msg = new Fl_Box(15, 104, W - 30, 50);
  msg->labelsize(SIZE_SMALL);
  msg->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  msg->label(_("Use at least 8 characters; a few unrelated words are easy to remember and hard to guess."));
  auto *cancel = new Fl_Button(W - 204, H - 36, 90, 24, _("Cancel"));
  auto *ok = new Fl_Button(W - 104, H - 36, 90, 24, _("Change"));
  for (auto *b : {cancel, ok}) {
    b->box(BUTTON_BOX);
    b->down_box(BUTTON_DOWN);
    b->labelsize(SIZE_SMALL + 1);
  }
  ok->labelfont(FONT_SYSTEM);
  ok->shortcut(FL_Enter);
  d->end();
  d->finish();
  d->set_modal();
  d->place(g_win->x() + g_win->ox() + (g_win->content_w() - W) / 2, g_win->y() + g_win->oy() + 40);
  d->show();
  cur->take_focus();
  static std::string text;
  bool done = false;
  while (d->shown() && !done) {
    Fl_Widget *o = Fl::readqueue();
    if (!o) { Fl::wait(); continue; }
    if (o == cancel || o == d) break;
    if (o != ok) continue;
    if (strcmp(nw->value(), again->value())) {
      text = _("The new passwords are not the same.");
    } else if (!*nw->value()) {
      text = _("Type the new password.");
    } else {
      msg->label(_("Changing the password…"));
      msg->redraw();
      ok->deactivate();
      std::string r = change_password(cur->value(), nw->value());
      ok->activate();
      if (r.empty()) {
        message(_("The password was changed."));
        done = true;
        break;
      }
      text = fmt(_("The password was not changed: %1"), r);
    }
    msg->label(text.c_str());
    msg->redraw();
  }
  d->hide();
  Fl::delete_widget(d);
}

// --- the window ------------------------------------------------------------------------

Fl_Box *heading(int X, int Y, const char *t) {
  auto *b = new Fl_Box(X, Y, 260, 20, t);
  b->labelfont(FONT_SYSTEM);
  b->labelsize(SIZE_SYSTEM);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  return b;
}

Fl_Button *button(int X, int Y, int W, const char *t) {
  auto *b = new Fl_Button(X, Y, W, 24, t);
  b->box(BUTTON_BOX);
  b->down_box(BUTTON_DOWN);
  b->labelsize(SIZE_SMALL + 1);
  return b;
}

Fl_Box *fact(int X, int Y, int W, const char *label, const std::string &value) {
  auto *l = new Fl_Box(X, Y, 110, 18, label);
  l->labelsize(SIZE_SMALL + 1);
  l->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
  auto *v = new Fl_Box(X + 116, Y, W - 116, 18);
  v->copy_label(value.c_str());
  v->labelsize(SIZE_SMALL + 1);
  v->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  return v;
}

}  // namespace

int panel_user() {
  fl_register_images();
  read_account();
  const int W = 580, H = 440;
  g_win = new FrameWindow(W, H, _("User"));
  g_win->xclass("nirvana-user");

  // --- left: the picture
  int x = 15, y = 10;
  heading(x, y, _("Picture"));
  y += 26;
  g_face_w = new FaceBox(x + 20, y);
  y += FACE + 16;
  button(x, y, 150, _("Choose Picture…"))->callback([](Fl_Widget *, void *) { choose_file(); });
  y += 30;
  button(x, y, 150, _("No Picture"))->callback([](Fl_Widget *, void *) {
    delete g_pending;
    g_pending = nullptr;
    g_remove = g_face != nullptr;
    g_face_w->redraw();
    if (g_remove) message(_("Click Apply or OK to remove the picture."));
  });
  y += 34;
  auto *pn = new Fl_Box(x, y, 160, 46, _("The login screen shows this picture next to your name."));
  pn->labelsize(SIZE_SMALL);
  pn->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  // --- right: the account
  x = 200;
  y = 10;
  const int RW = W - x - 15;
  heading(x, y, _("Account"));
  y += 26;
  fact(x, y, RW, _("Login name:"), g_user); y += 20;
  fact(x, y, RW, _("Account type:"), g_admin ? _("Administrator") : _("Standard user")); y += 20;
  fact(x, y, RW, _("Home folder:"), g_home); y += 20;
  fact(x, y, RW, _("Shell:"), g_shell); y += 20;
  auto *gv = fact(x, y, RW, _("Groups:"), g_groups);
  gv->tooltip(g_groups.c_str());
  y += 30;
  g_name = new Fl_Input(x + 116, y, RW - 116, 22, _("Full name:"));
  g_name->labelsize(SIZE_SMALL + 1);
  g_name->textsize(SIZE_SMALL + 1);
  g_name->value(g_full.c_str());
  y += 36;
  heading(x, y, _("Logging In"));
  y += 26;
  button(x + 4, y, 170, _("Change Password…"))->callback([](Fl_Widget *, void *) { password_dialog(); });
  y += 34;
  g_autologin = new Fl_Check_Button(x + 4, y, RW - 4, 20, _("Log in automatically, without the login screen"));
  g_autologin->labelsize(SIZE_SMALL + 1);
  g_autologin->value(g_autologin_now);
  y += 22;
  auto *an = new Fl_Box(x + 26, y, RW - 26, 34);
  an->labelsize(SIZE_SMALL);
  an->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  if (access("/usr/sbin/lightdm", X_OK)) {
    g_autologin->deactivate();
    an->label(_("Needs the Nirvana-DM login screen (LightDM)."));
  } else if (!g_admin) {
    g_autologin->deactivate();
    an->label(_("Only an administrator can turn it on."));
  } else {
    an->label(_("Anyone who switches the computer on gets your desktop."));
  }

  // --- below: the built-in pictures
  y = H - 136;
  heading(15, y, _("Or pick one of these:"));
  y += 24;
  for (int i = 0; i < N_AVATARS; i++)
    (new AvatarButton(15 + i * 52, y, i))->callback([](Fl_Widget *w, void *) {
      int i = ((AvatarButton *)w)->idx;
      set_pending(render([i] { draw_avatar(i, 0, 0, FACE); }));
    });

  g_msg = new Fl_Box(15, H - 64, W - 30, 22);
  g_msg->labelsize(SIZE_SMALL);
  g_msg->labelfont(FONT_SYSTEM);
  g_msg->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  button(W - 290, H - 36, 86, _("Cancel"))->callback([](Fl_Widget *, void *) { g_win->hide(); });
  button(W - 196, H - 36, 86, _("Apply"))->callback([](Fl_Widget *, void *) { apply(); });
  auto *ok = button(W - 102, H - 36, 86, _("OK"));
  ok->labelfont(FONT_SYSTEM);
  ok->shortcut(FL_Enter);
  ok->callback([](Fl_Widget *, void *) {
    if (apply()) g_win->hide();
  });
  g_win->end();
  g_win->finish();
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + g_win->oy(), (sh - H) / 4));
  g_win->show();
  Fl::add_timeout(0.05, read_face);
  return Fl::run();
}
