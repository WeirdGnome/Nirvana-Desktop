// Nirvana Desktop - notifications: the org.freedesktop.Notifications server and its applet
//
// Rambler takes the standard notification service on the session bus, so notifications
// of any program (web browsers, messengers, notify-send, mail) come here. Each one shows
// as a small window at the top right below the menu bar, drawn in the current appearance
// (the scheme's title bar, or the classic 16-bit gadgets), with the program's picture,
// the summary, the text and the action buttons. A short chime plays (the notification's
// own sound, a sound of the theme, or Nirvana's built-in chime).
//
// The applet (a bell): a dot for unread notifications, a menu with the recent ones (only in
// memory), "Do Not Disturb" (no windows and no sound, urgent ones still show), "Sound"
// and "Clear". Settings in ~/.config/nirvana/notify.conf.
//
// If another notification program already owns the service (dunst, xfce4-notifyd...),
// it stays in charge and the applet says so.
#include "applets.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <dbus/dbus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace plat;

namespace applets {

namespace {

const char *PATH = "/org/freedesktop/Notifications";
const char *IFACE = "org.freedesktop.Notifications";
const int POP_W = 340, GAP = 8, MAX_VISIBLE = 5, HISTORY = 30;

DBusConnection *g_conn;
bool g_server = false;          // we own the service
bool g_dnd = false, g_sound = true;
int g_unread = 0;
unsigned g_next_id = 1;
class BellMenu;
BellMenu *g_bell;

struct Note {
  unsigned id = 0;
  std::string app, summary, body, icon;
  std::vector<std::pair<std::string, std::string>> actions;   // key, label
  std::shared_ptr<Fl_RGB_Image> image;
  int timeout = -1;              // ms, -1 default, 0 never
  int urgency = 1;               // 0 low, 1 normal, 2 critical
  bool resident = false, transient = false, suppress_sound = false;
  std::string sound_file, sound_name;
  time_t when = 0;
  bool alive = true;             // the program can still get its actions
  std::function<void(const std::string &)> local;   // Nirvana's own notification: its actions
};

std::deque<std::shared_ptr<Note>> g_hist;   // newest first

double clock_now() {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
std::string conf_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/notify.conf";
}
void load_conf() {
  std::ifstream in(conf_file());
  std::string l;
  while (std::getline(in, l)) {
    if (l.rfind("dnd=", 0) == 0) g_dnd = l.substr(4) == "1";
    if (l.rfind("sound=", 0) == 0) g_sound = l.substr(6) != "0";
  }
}
void save_conf() {
  std::string f = conf_file();
  std::string dir = f.substr(0, f.rfind('/'));
  mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
  mkdir(dir.c_str(), 0755);
  std::ofstream out(f);
  out << "# Nirvana Desktop - notifications (the bell in the menu bar)\ndnd=" << g_dnd << "\nsound=" << g_sound << "\n";
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------
// Nirvana's own chime: two short bell-like notes, made once into a WAV file
std::string chime_file() {
  const char *rt = getenv("XDG_RUNTIME_DIR");
  std::string f = std::string(rt && *rt ? rt : "/tmp") + "/nirvana-chime-" + std::to_string(getuid()) + ".wav";
  struct stat st;
  if (stat(f.c_str(), &st) == 0 && st.st_size > 1000) return f;
  const int RATE = 22050;
  std::vector<int16_t> pcm;
  auto note = [&](double freq, double len, double start_gap) {
    int gap = (int)(start_gap * RATE);
    for (int i = 0; i < gap; i++) pcm.push_back(0);
    int n = (int)(len * RATE);
    for (int i = 0; i < n; i++) {
      double t = (double)i / RATE;
      double env = std::min(1.0, t / 0.004) * std::exp(-t * 9.0);
      double v = std::sin(2 * M_PI * freq * t) + 0.35 * std::sin(2 * M_PI * freq * 2.01 * t) +
                 0.12 * std::sin(2 * M_PI * freq * 3.02 * t);
      pcm.push_back((int16_t)(v * env * 0.32 * 32767));
    }
  };
  note(987.77, 0.16, 0.0);    // B5
  note(1318.51, 0.42, 0.0);   // E6
  FILE *fp = fopen(f.c_str(), "wb");
  if (!fp) return "";
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, fp); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, fp); };
  uint32_t data = (uint32_t)(pcm.size() * 2);
  fwrite("RIFF", 1, 4, fp); u32(36 + data); fwrite("WAVE", 1, 4, fp);
  fwrite("fmt ", 1, 4, fp); u32(16); u16(1); u16(1); u32(RATE); u32(RATE * 2); u16(2); u16(16);
  fwrite("data", 1, 4, fp); u32(data);
  fwrite(pcm.data(), 2, pcm.size(), fp);
  fclose(fp);
  return f;
}

bool readable(const std::string &f) { return !f.empty() && access(f.c_str(), R_OK) == 0; }

void play(const Note &n) {
  if (!g_sound || n.suppress_sound) return;
  static double last = 0;
  double now = clock_now();
  if (now - last < 0.6) return;   // a burst of notifications: one sound
  last = now;
  std::string f;
  if (readable(n.sound_file)) f = n.sound_file;
  else if (!n.sound_name.empty()) {
    for (const char *ext : {".oga", ".ogg", ".wav"}) {
      std::string c = "/usr/share/sounds/freedesktop/stereo/" + n.sound_name + ext;
      if (readable(c)) { f = c; break; }
    }
  }
  if (f.empty()) f = chime_file();
  if (f.empty()) return;
  if (fork() == 0) {
    setsid();
    if (fork() == 0) {
      execl("/bin/sh", "sh", "-c",
            "command -v paplay >/dev/null 2>&1 && exec paplay \"$0\"; "
            "command -v pw-play >/dev/null 2>&1 && exec pw-play \"$0\"; "
            "command -v aplay >/dev/null 2>&1 && exec aplay -q \"$0\"; exit 1",
            f.c_str(), (char *)nullptr);
      _exit(1);
    }
    _exit(0);
  }
  while (waitpid(-1, nullptr, WNOHANG) > 0) {}
}

// ---------------------------------------------------------------------------
// Text and pictures
// ---------------------------------------------------------------------------
// body-markup: the tags are dropped (<b>, <i>, <a href>, <img>), entities decoded
std::string plain(const std::string &s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '<') {
      size_t e = s.find('>', i);
      if (e != std::string::npos) {
        std::string tag = s.substr(i + 1, e - i - 1);
        if (tag == "br" || tag == "br/" || tag == "br /") o += '\n';
        i = e;
        continue;
      }
    }
    if (s[i] == '&') {
      static const std::pair<const char *, const char *> ent[] = {
          {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&#39;", "'"}, {"&nbsp;", " "}};
      bool done = false;
      for (auto &[k, v] : ent)
        if (s.compare(i, strlen(k), k) == 0) { o += v; i += strlen(k) - 1; done = true; break; }
      if (done) continue;
    }
    o += s[i];
  }
  // at most 2 empty lines in a row, no spaces at the ends
  size_t a = o.find_first_not_of(" \n\t"), b = o.find_last_not_of(" \n\t");
  return a == std::string::npos ? "" : o.substr(a, b - a + 1);
}

std::shared_ptr<Fl_RGB_Image> picture_file(const std::string &spec) {
  static bool reg = false;
  if (!reg) { fl_register_images(); reg = true; }
  std::string p = spec;
  if (p.rfind("file://", 0) == 0) p = p.substr(7);
  std::vector<std::string> tries;
  if (!p.empty() && p[0] == '/') tries.push_back(p);
  else if (!p.empty()) {
    for (const char *sz : {"48x48", "64x64", "128x128", "256x256", "32x32", "scalable"})
      for (const char *ext : {".png", ".svg"})
        tries.push_back(std::string("/usr/share/icons/hicolor/") + sz + "/apps/" + p + ext);
    for (const char *ext : {".png", ".svg", ".xpm"}) tries.push_back("/usr/share/pixmaps/" + p + ext);
  }
  for (auto &t : tries) {
    if (!readable(t)) continue;
    Fl_Shared_Image *si = Fl_Shared_Image::get(t.c_str());
    if (!si) continue;
    if (si->w() <= 0 || si->h() <= 0 || !si->image()) { si->release(); continue; }
    Fl_Image *c = si->image()->copy(40, 40);
    si->release();
    if (auto *rgb = dynamic_cast<Fl_RGB_Image *>(c)) return std::shared_ptr<Fl_RGB_Image>(rgb);
    delete c;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// The notification window
// ---------------------------------------------------------------------------
class Popup;
std::vector<Popup *> g_pops;
void restack();
void emit_closed(unsigned id, unsigned reason);
void emit_action(unsigned id, const std::string &key);

class Popup : public Fl_Double_Window {
public:
  std::shared_ptr<Note> n;
  int text_x = 0, sum_h = 0, body_h = 0;
  bool close_down_ = false, from_history = false;
  std::deque<std::string> keys_;   // the action keys of the buttons (stable addresses)

  Popup(std::shared_ptr<Note> note, bool history) : Fl_Double_Window(POP_W, 80), n(std::move(note)), from_history(history) {
    border(0);
    set_override();
    box(FL_NO_BOX);
    end();
    resizable(nullptr);   // layout() sets the height after placing the buttons - no scaling
    layout();
  }

  bool has_default() const {
    for (auto &a : n->actions) if (a.first == "default") return true;
    return false;
  }

  void layout() {
    clear();
    keys_.clear();
    begin();
    text_x = FRAME + 10 + (n->image ? 48 : 0);
    int tw = POP_W - text_x - FRAME - 10;
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    int ww = tw, hh = 0;
    fl_measure(n->summary.c_str(), ww, hh, 0);
    sum_h = n->summary.empty() ? 0 : std::min(hh, 3 * fl_height());
    fl_font(FONT_SMALL, SIZE_SMALL + 1);
    ww = tw; hh = 0;
    if (!n->body.empty()) fl_measure(n->body.c_str(), ww, hh, 0);
    body_h = std::min(hh, 6 * fl_height());
    int content = std::max(n->image ? 40 : 0, sum_h + (body_h ? body_h + 4 : 0));
    int H = TITLE_H + 10 + content + 10;
    // action buttons (not "default" - that is a click on the window)
    std::vector<std::pair<std::string, std::string>> btns;
    if (n->alive && !from_history)
      for (auto &a : n->actions) if (a.first != "default" && !a.second.empty()) btns.push_back(a);
    if (btns.size() > 3) btns.resize(3);
    if (!btns.empty()) H += 32;
    end();
    size(POP_W, H + FRAME);   // the final size first, then the buttons at the bottom edge
    begin();
    int by = h() - FRAME - 30;
    int bx = POP_W - FRAME - 10;
    fl_font(FONT_SYSTEM, SIZE_SMALL + 1);
    for (size_t i = btns.size(); i-- > 0;) {
      int bw = std::max(70, (int)fl_width(btns[i].second.c_str()) + 24);
      bx -= bw;
      auto *b = new Fl_Button(bx, by, bw, 22);
      b->copy_label(btns[i].second.c_str());
      b->box(BUTTON_BOX);
      b->down_box(BUTTON_DOWN);
      b->labelsize(SIZE_SMALL + 1);
      b->callback([](Fl_Widget *w, void *d) {
        auto *p = (Popup *)w->window();
        std::string key = *(std::string *)d;
        if (p->n->local) p->n->local(key);
        else emit_action(p->n->id, key);
        if (!p->n->resident) p->dismiss(2);
      }, &keys_.emplace_back(btns[i].first));
      bx -= 6;
    }
    end();
  }

  void draw() override {
    bool classic = read_desktop_cached();
    if (classic) {
      draw_classic_frame(0, 0, w(), h(), true, false, false);
      draw_classic_titlebar(0, 0, w(), n->app.c_str(), true, close_down_ ? 1 : 0, 4);
    } else {
      draw_frame(0, 0, w(), h(), true, false);
      draw_titlebar(0, 0, w(), n->app.c_str(), true, 0, 0);
      draw_close_box(8, (TITLE_H - BOX) / 2, close_down_, true);
    }
    fl_color(GRAY);
    fl_rectf(FRAME, TITLE_H, w() - 2 * FRAME, h() - TITLE_H - FRAME);
    if (n->urgency == 2) {   // urgent: a red stripe at the left edge
      fl_color(ALERT);
      fl_rectf(FRAME, TITLE_H, 4, h() - TITLE_H - FRAME);
    }
    int y = TITLE_H + 10;
    if (n->image) {
      int ix = FRAME + 10 + (40 - n->image->w()) / 2, iy = y + (40 - n->image->h()) / 2;
      n->image->draw(ix, iy);
    }
    int tw = w() - text_x - FRAME - 10;
    fl_color(TEXT);
    if (sum_h) {
      fl_font(FONT_SYSTEM, SIZE_SYSTEM);
      fl_push_clip(text_x, y, tw, sum_h);
      fl_draw(n->summary.c_str(), text_x, y, tw, sum_h, FL_ALIGN_TOP_LEFT | FL_ALIGN_WRAP, nullptr, 0);
      fl_pop_clip();
      y += sum_h + 4;
    }
    if (body_h) {
      fl_font(FONT_SMALL, SIZE_SMALL + 1);
      fl_push_clip(text_x, y, tw, body_h);
      fl_draw(n->body.c_str(), text_x, y, tw, body_h, FL_ALIGN_TOP_LEFT | FL_ALIGN_WRAP, nullptr, 0);
      fl_pop_clip();
    }
    if (from_history) {
      char t[16];
      strftime(t, sizeof t, "%H:%M", localtime(&n->when));
      fl_font(FONT_SMALL, SIZE_SMALL);
      fl_color(fl_color_average(TEXT, GRAY, 0.6f));
      fl_draw(t, w() - FRAME - 10 - (int)fl_width(t), h() - FRAME - 6);
    }
    draw_children();
  }

  bool on_close(int ex, int ey) const {
    if (read_desktop_cached()) return ex < CLASSIC_GADGET && ey < TITLE_H;
    int by = (TITLE_H - BOX) / 2;
    return ex >= 6 && ex < 10 + BOX && ey >= by - 2 && ey < by + BOX + 2;
  }

  int handle(int e) override {
    if (Fl_Double_Window::handle(e) && e != FL_ENTER && e != FL_LEAVE && e != FL_MOVE) return 1;
    switch (e) {
    case FL_ENTER:   // the pointer over it: it stays
      Fl::remove_timeout(expire_cb, this);
      return 1;
    case FL_LEAVE:
      arm(2500);
      return 1;
    case FL_PUSH:
      if (on_close(Fl::event_x(), Fl::event_y())) { close_down_ = true; redraw(); return 1; }
      return 1;
    case FL_DRAG: {
      bool d = on_close(Fl::event_x(), Fl::event_y());
      if (d != close_down_) { close_down_ = d; redraw(); }
      return 1;
    }
    case FL_RELEASE:
      if (close_down_) { close_down_ = false; dismiss(2); return 1; }
      if (on_close(Fl::event_x(), Fl::event_y())) return 1;
      // a click on the notification: its default action (e.g. open the chat), then away
      if (n->alive && !from_history && has_default()) {
        if (n->local) n->local("default");
        else emit_action(n->id, "default");
      }
      dismiss(2);
      return 1;
    }
    return 0;
  }

  void arm(int ms) {
    Fl::remove_timeout(expire_cb, this);
    if (from_history) { Fl::add_timeout(8.0, expire_cb, this); return; }
    if (n->urgency == 2 || n->timeout == 0) return;   // urgent / "never": until clicked
    Fl::add_timeout(ms / 1000.0, expire_cb, this);
  }
  void start() {
    int ms = n->timeout > 0 ? n->timeout : 5000 + (int)std::min<size_t>(n->body.size(), 300) * 20;
    arm(ms);
  }
  static void expire_cb(void *d) { ((Popup *)d)->dismiss(1); }

  // reason: 1 expired, 2 dismissed by the user, 3 closed by the program
  void dismiss(int reason) {
    Fl::remove_timeout(expire_cb, this);
    g_pops.erase(std::remove(g_pops.begin(), g_pops.end(), this), g_pops.end());
    if (!from_history) {
      if (!n->local) emit_closed(n->id, (unsigned)reason);
      n->alive = false;   // closed: the program no longer waits for its actions
    }
    hide();
    Fl::delete_widget(this);
    restack();
  }

  static bool read_desktop_cached() {
    static double t = -10;
    static bool c = false;
    double now = clock_now();
    if (now - t > 2) { c = read_desktop().classic; t = now; }
    return classic_look(c);
  }
};

void restack() {
  int sx, sy, sw, sh;
  Fl::screen_xywh(sx, sy, sw, sh);
  int y = sy + MENUBAR_H + GAP;
  int shown = 0;
  for (auto *p : g_pops) {
    if (shown >= MAX_VISIBLE || y + p->h() > sy + sh - 40) { p->hide(); continue; }
    p->position(sx + sw - p->w() - GAP, y);
    if (!p->shown()) p->show();
    y += p->h() + GAP;
    shown++;
  }
}

Popup *popup_of(unsigned id) {
  for (auto *p : g_pops) if (p->n->id == id && !p->from_history) return p;
  return nullptr;
}

void show_note(std::shared_ptr<Note> n, bool history) {
  auto *p = new Popup(n, history);
  g_pops.insert(g_pops.begin(), p);   // the newest at the top
  restack();
  p->start();
}

// ---------------------------------------------------------------------------
// D-Bus
// ---------------------------------------------------------------------------
void flush() { if (g_conn) dbus_connection_flush(g_conn); }

void emit_closed(unsigned id, unsigned reason) {
  if (!g_conn || !g_server) return;
  DBusMessage *s = dbus_message_new_signal(PATH, IFACE, "NotificationClosed");
  dbus_message_append_args(s, DBUS_TYPE_UINT32, &id, DBUS_TYPE_UINT32, &reason, DBUS_TYPE_INVALID);
  dbus_connection_send(g_conn, s, nullptr);
  dbus_message_unref(s);
  flush();
}

void emit_action(unsigned id, const std::string &key) {
  if (!g_conn || !g_server) return;
  DBusMessage *s = dbus_message_new_signal(PATH, IFACE, "ActionInvoked");
  const char *k = key.c_str();
  dbus_message_append_args(s, DBUS_TYPE_UINT32, &id, DBUS_TYPE_STRING, &k, DBUS_TYPE_INVALID);
  dbus_connection_send(g_conn, s, nullptr);
  dbus_message_unref(s);
  flush();
}

// image-data: (iiibiiay) width, height, rowstride, has_alpha, bits, channels, data
std::shared_ptr<Fl_RGB_Image> image_data(DBusMessageIter *v) {
  if (dbus_message_iter_get_arg_type(v) != DBUS_TYPE_STRUCT) return nullptr;
  DBusMessageIter s;
  dbus_message_iter_recurse(v, &s);
  int vals[6] = {0};
  for (int i = 0; i < 6; i++) {
    int t = dbus_message_iter_get_arg_type(&s);
    if (t == DBUS_TYPE_INT32) dbus_message_iter_get_basic(&s, &vals[i]);
    else if (t == DBUS_TYPE_BOOLEAN) { dbus_bool_t b; dbus_message_iter_get_basic(&s, &b); vals[i] = b; }
    else return nullptr;
    dbus_message_iter_next(&s);
  }
  if (dbus_message_iter_get_arg_type(&s) != DBUS_TYPE_ARRAY) return nullptr;
  DBusMessageIter a;
  dbus_message_iter_recurse(&s, &a);
  const unsigned char *data = nullptr;
  int len = 0;
  dbus_message_iter_get_fixed_array(&a, &data, &len);
  int w = vals[0], h = vals[1], stride = vals[2], ch = vals[5];
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || (ch != 3 && ch != 4) || vals[4] != 8 || !data ||
      len < stride * (h - 1) + w * ch)
    return nullptr;
  auto *buf = new uchar[(size_t)w * h * ch];
  for (int y = 0; y < h; y++) memcpy(buf + (size_t)y * w * ch, data + (size_t)y * stride, (size_t)w * ch);
  auto *img = new Fl_RGB_Image(buf, w, h, ch);
  img->alloc_array = 1;
  Fl_Image *c = img->copy(40, 40);
  delete img;
  return std::shared_ptr<Fl_RGB_Image>((Fl_RGB_Image *)c);
}

unsigned on_notify(DBusMessage *m) {
  DBusMessageIter it;
  if (!dbus_message_iter_init(m, &it)) return 0;
  auto n = std::make_shared<Note>();
  auto str = [&]() {
    const char *s = "";
    if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING) dbus_message_iter_get_basic(&it, &s);
    dbus_message_iter_next(&it);
    return std::string(s ? s : "");
  };
  n->app = str();
  unsigned replaces = 0;
  if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_UINT32) dbus_message_iter_get_basic(&it, &replaces);
  dbus_message_iter_next(&it);
  n->icon = str();
  n->summary = plain(str());
  n->body = plain(str());
  if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {   // actions: key, label, key, label...
    DBusMessageIter a;
    dbus_message_iter_recurse(&it, &a);
    std::vector<std::string> v;
    while (dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_STRING) {
      const char *s;
      dbus_message_iter_get_basic(&a, &s);
      v.push_back(s);
      dbus_message_iter_next(&a);
    }
    for (size_t i = 0; i + 1 < v.size(); i += 2) n->actions.push_back({v[i], v[i + 1]});
  }
  dbus_message_iter_next(&it);
  std::string image_path;
  if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {   // hints a{sv}
    DBusMessageIter d;
    dbus_message_iter_recurse(&it, &d);
    while (dbus_message_iter_get_arg_type(&d) == DBUS_TYPE_DICT_ENTRY) {
      DBusMessageIter e, v;
      dbus_message_iter_recurse(&d, &e);
      const char *key = "";
      dbus_message_iter_get_basic(&e, &key);
      dbus_message_iter_next(&e);
      dbus_message_iter_recurse(&e, &v);
      int t = dbus_message_iter_get_arg_type(&v);
      std::string k = key;
      auto sval = [&]() { const char *s = ""; if (t == DBUS_TYPE_STRING) dbus_message_iter_get_basic(&v, &s); return std::string(s); };
      auto bval = [&]() { dbus_bool_t b = 0; if (t == DBUS_TYPE_BOOLEAN) dbus_message_iter_get_basic(&v, &b); return (bool)b; };
      if (k == "urgency" && t == DBUS_TYPE_BYTE) { unsigned char u; dbus_message_iter_get_basic(&v, &u); n->urgency = u; }
      else if (k == "image-data" || k == "image_data" || (k == "icon_data" && !n->image)) n->image = image_data(&v);
      else if (k == "image-path" || k == "image_path") image_path = sval();
      else if (k == "sound-file") n->sound_file = sval();
      else if (k == "sound-name") n->sound_name = sval();
      else if (k == "suppress-sound") n->suppress_sound = bval();
      else if (k == "resident") n->resident = bval();
      else if (k == "transient") n->transient = bval();
      dbus_message_iter_next(&d);
    }
  }
  dbus_message_iter_next(&it);
  if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_INT32) dbus_message_iter_get_basic(&it, &n->timeout);
  if (!n->image && !image_path.empty()) n->image = picture_file(image_path);
  if (!n->image && !n->icon.empty()) n->image = picture_file(n->icon);
  if (n->app.empty()) n->app = _("Notification");
  n->when = time(nullptr);

  // an update of a notification already shown
  if (replaces) {
    for (auto &h : g_hist)
      if (h->id == replaces) {
        n->id = replaces;
        h = n;
        if (Popup *p = popup_of(replaces)) {
          p->n = n;
          p->layout();
          p->redraw();
          restack();
          p->start();
        } else if (!g_dnd || n->urgency == 2) {
          show_note(n, false);
        }
        return replaces;
      }
  }
  n->id = g_next_id++;
  if (!g_next_id) g_next_id = 1;
  if (!n->transient) {
    g_hist.push_front(n);
    while ((int)g_hist.size() > HISTORY) g_hist.pop_back();
    g_unread++;
  }
  if (!g_dnd || n->urgency == 2) {
    show_note(n, false);
    play(*n);
  }
  if (g_bell) ((Fl_Widget *)g_bell)->redraw();
  return n->id;
}

const char *INTROSPECT =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node><interface name=\"org.freedesktop.Notifications\">"
    "<method name=\"GetCapabilities\"><arg type=\"as\" direction=\"out\"/></method>"
    "<method name=\"Notify\"><arg type=\"s\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/>"
    "<arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"as\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/>"
    "<arg type=\"u\" direction=\"out\"/></method>"
    "<method name=\"CloseNotification\"><arg type=\"u\" direction=\"in\"/></method>"
    "<method name=\"GetServerInformation\"><arg type=\"s\" direction=\"out\"/><arg type=\"s\" direction=\"out\"/>"
    "<arg type=\"s\" direction=\"out\"/><arg type=\"s\" direction=\"out\"/></method>"
    "<signal name=\"NotificationClosed\"><arg type=\"u\"/><arg type=\"u\"/></signal>"
    "<signal name=\"ActionInvoked\"><arg type=\"u\"/><arg type=\"s\"/></signal>"
    "</interface><interface name=\"org.freedesktop.DBus.Introspectable\">"
    "<method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method></interface></node>";

DBusHandlerResult on_message(DBusConnection *c, DBusMessage *m, void *) {
  DBusMessage *r = nullptr;
  if (dbus_message_is_method_call(m, IFACE, "Notify")) {
    dbus_uint32_t id = on_notify(m);
    r = dbus_message_new_method_return(m);
    dbus_message_append_args(r, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);
  } else if (dbus_message_is_method_call(m, IFACE, "CloseNotification")) {
    dbus_uint32_t id = 0;
    dbus_message_get_args(m, nullptr, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);
    r = dbus_message_new_method_return(m);
    dbus_connection_send(c, r, nullptr);
    dbus_message_unref(r);
    r = nullptr;
    if (Popup *p = popup_of(id)) p->dismiss(3);
    else
      for (auto &h : g_hist)
        if (h->id == id && h->alive) { h->alive = false; emit_closed(id, 3); }   // once only
  } else if (dbus_message_is_method_call(m, IFACE, "GetCapabilities")) {
    r = dbus_message_new_method_return(m);
    const char *caps[] = {"actions", "body", "body-markup", "icon-static", "persistence", "sound"};
    const char **p = caps;
    dbus_message_append_args(r, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &p, 6, DBUS_TYPE_INVALID);
  } else if (dbus_message_is_method_call(m, IFACE, "GetServerInformation")) {
    r = dbus_message_new_method_return(m);
    const char *name = "Nirvana", *vendor = "Nirvana Desktop", *ver = "1", *spec = "1.2";
    dbus_message_append_args(r, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &vendor, DBUS_TYPE_STRING, &ver,
                             DBUS_TYPE_STRING, &spec, DBUS_TYPE_INVALID);
  } else if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
    r = dbus_message_new_method_return(m);
    dbus_message_append_args(r, DBUS_TYPE_STRING, &INTROSPECT, DBUS_TYPE_INVALID);
  } else {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  if (r) {
    dbus_connection_send(c, r, nullptr);
    dbus_message_unref(r);
  }
  flush();
  return DBUS_HANDLER_RESULT_HANDLED;
}

void pump() {
  if (!g_conn) return;
  while (dbus_connection_dispatch(g_conn) == DBUS_DISPATCH_DATA_REMAINS) {}
}
void fd_cb(int fd, void *) {
  if (!dbus_connection_read_write(g_conn, 0)) {   // the bus is gone (end of the session)
    Fl::remove_fd(fd);
    return;
  }
  pump();
}

void start_server() {
  if (const char *e = getenv("NIRVANA_NOTIFICATIONS"); e && !strcmp(e, "0")) return;   // another program's job
  DBusError err;
  dbus_error_init(&err);
  g_conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
  if (!g_conn) { dbus_error_free(&err); return; }
  dbus_connection_set_exit_on_disconnect(g_conn, FALSE);
  int r = dbus_bus_request_name(g_conn, IFACE, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
  if (dbus_error_is_set(&err)) dbus_error_free(&err);
  g_server = r == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER || r == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER;
  if (!g_server) return;   // dunst, xfce4-notifyd... stays in charge
  static DBusObjectPathVTable vt = {nullptr, on_message, nullptr, nullptr, nullptr, nullptr};
  dbus_connection_register_object_path(g_conn, PATH, &vt, nullptr);
  int fd = -1;
  if (dbus_connection_get_unix_fd(g_conn, &fd)) Fl::add_fd(fd, FL_READ, fd_cb);
  pump();
}

// ---------------------------------------------------------------------------
// The applet: a bell
// ---------------------------------------------------------------------------
void draw_bell(int x, int y, Fl_Color c, bool dnd) {
  if (glossy_glyphs()) {   // a gold bell with a shine; do not disturb - a red slash
    glyph_color(0xF2C232, c);
    fl_pie(x + 2, y + 1, 10, 10, 0, 180);
    fl_rectf(x + 2, y + 5, 10, 4);
    fl_polygon(x + 2, y + 9, x + 12, y + 9, x + 14, y + 11, x + 0, y + 11);
    glyph_color(0x000000, c);
    fl_arc(x + 2, y + 1, 10, 10, 0, 180);
    fl_yxline(x + 2, y + 6, y + 9); fl_yxline(x + 11, y + 6, y + 9);
    fl_loop(x + 2, y + 9, x + 12, y + 9, x + 14, y + 11, x + 0, y + 11);
    fl_rectf(x + 6, y + 12, 3, 2);
    fl_rectf(x + 6, y, 3, 1);
    glyph_color(0xFFFFFF, c);
    fl_xyline(x + 4, y + 4, x + 5); fl_point(x + 4, y + 5);
    if (dnd) {
      glyph_color(0xE02828, FL_BLACK);
      fl_line_style(FL_SOLID, 2);
      fl_line(x, y + 1, x + 14, y + 13);
      fl_line_style(0);
    }
    return;
  }
  fl_color(c);
  fl_pie(x + 2, y + 1, 10, 10, 0, 180);                 // the dome
  fl_rectf(x + 2, y + 5, 10, 4);
  fl_polygon(x + 2, y + 9, x + 12, y + 9, x + 14, y + 11, x + 0, y + 11);   // the rim
  fl_rectf(x + 6, y + 12, 3, 2);                         // the clapper
  fl_rectf(x + 6, y, 3, 1);
  if (dnd) {                                             // do not disturb: crossed out
    fl_line_style(FL_SOLID, 2);
    fl_line(x, y + 1, x + 14, y + 13);
    fl_line_style(0);
  }
}

std::string menu_text(const std::string &s, size_t max) {
  std::string o;
  for (char ch : s) {
    if (ch == '\n' || ch == '\t') { o += ' '; continue; }
    if (ch == '&') o += "&&";
    else o += ch;
  }
  int n = 0;
  size_t i = 0;
  for (; i < o.size(); i++) {
    if (((unsigned char)o[i] & 0xC0) != 0x80) n++;
    if ((size_t)n > max) break;
  }
  return i < o.size() ? o.substr(0, i) + "…" : o;
}

class BellMenu : public Fl_Menu_Button {
public:
  std::vector<std::string> labels_;
  std::vector<Fl_Menu_Item> items_;
  BellMenu(int X, int Y, int W, int H) : Fl_Menu_Button(X, Y, W, H) {
    box(FL_FLAT_BOX);
    down_box(FL_FLAT_BOX);
    textfont(FONT_SYSTEM);
    textsize(SIZE_SYSTEM);
    selection_color(SEL);
  }
  void build() {
    labels_.clear();
    items_.clear();
    labels_.reserve(HISTORY + 10);
    struct It { std::string l; Fl_Callback *cb; long v; int flags; };
    std::vector<It> its;
    if (!g_server)
      its.push_back({_("Another program shows the notifications"), nullptr, 0, FL_MENU_INACTIVE | FL_MENU_DIVIDER});
    its.push_back({_("Notifications"), nullptr, 0, FL_MENU_INACTIVE});
    if (g_hist.empty()) its.push_back({_("No notifications"), nullptr, 0, FL_MENU_INACTIVE});
    for (size_t i = 0; i < g_hist.size(); i++) {
      auto &n = g_hist[i];
      char t[16];
      strftime(t, sizeof t, "%H:%M", localtime(&n->when));
      std::string l = std::string(t) + "   " + menu_text(n->app, 18) + ": " + menu_text(n->summary.empty() ? n->body : n->summary, 40);
      its.push_back({l, show_cb, (long)i, 0});
    }
    its.back().flags |= FL_MENU_DIVIDER;
    its.push_back({_("Do Not Disturb"), dnd_cb, 0, FL_MENU_TOGGLE | (g_dnd ? FL_MENU_VALUE : 0)});
    its.push_back({_("Notification Sound"), sound_cb, 0, FL_MENU_TOGGLE | (g_sound ? FL_MENU_VALUE : 0)});
    its.push_back({_("Clear Notifications"), clear_cb, 0, g_hist.empty() ? FL_MENU_INACTIVE : 0});
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
  static void show_cb(Fl_Widget *, void *v) {
    size_t i = (size_t)(long)v;
    if (i < g_hist.size()) show_note(g_hist[i], true);   // again, without sound and actions
  }
  static void dnd_cb(Fl_Widget *w, void *) { g_dnd = !g_dnd; save_conf(); w->redraw(); }
  static void sound_cb(Fl_Widget *, void *) {
    g_sound = !g_sound;
    save_conf();
    if (g_sound) { Note n; play(n); }   // a sample
  }
  static void clear_cb(Fl_Widget *w, void *) {
    g_hist.clear();
    g_unread = 0;
    while (!g_pops.empty()) g_pops.front()->dismiss(2);
    w->redraw();
  }
  int handle(int e) override {
    if (e == FL_PUSH) {
      build();
      g_unread = 0;
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
    Fl_Color on = open_ ? SEL_TEXT : TEXT;
    Fl_Color c = g_dnd ? fl_color_average(on, open_ ? selection_color() : GRAY, 0.4f) : on;
    int ix = x() + (w() - 14) / 2, iy = y() + 3;
    draw_bell(ix, iy, c, g_dnd);
    if (g_unread > 0 && !open_) {   // unread: a red dot at the top right
      fl_color(ALERT);
      fl_pie(ix + 10, iy - 2, 6, 6, 0, 360);
    }
    fl_pop_clip();
  }
private:
  bool open_ = false;
};

}  // namespace

// Nirvana's own notification (e.g. NirvanaSHIELD): shown and sounded like the others,
// its buttons call back here instead of over D-Bus
void notify_local(const std::string &app, const std::string &summary, const std::string &body,
                  const std::string &icon, const std::vector<std::pair<std::string, std::string>> &actions,
                  std::function<void(const std::string &)> on_action, bool urgent) {
  auto n = std::make_shared<Note>();
  n->app = app;
  n->summary = summary;
  n->body = body;
  n->actions = actions;
  n->local = std::move(on_action);
  n->urgency = urgent ? 2 : 1;
  n->timeout = urgent ? 0 : 15000;
  if (!icon.empty()) n->image = picture_file(icon);
  n->when = time(nullptr);
  n->id = g_next_id++;
  g_hist.push_front(n);
  while ((int)g_hist.size() > HISTORY) g_hist.pop_back();
  g_unread++;
  if (!g_dnd || urgent) {
    show_note(n, false);
    play(*n);
  }
  if (g_bell) ((Fl_Widget *)g_bell)->redraw();
}

Fl_Widget *make_notifications(int X, int Y, int W, int H) {
  load_conf();
  start_server();
  g_bell = new BellMenu(X, Y, W, H);
  return g_bell;
}

}  // namespace applets
