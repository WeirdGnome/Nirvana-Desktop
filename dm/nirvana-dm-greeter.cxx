// Nirvana Desktop - Nirvana-DM: the login screen (a LightDM greeter)
//
// LightDM does the hard and security-sensitive part - the X server, PAM, the
// session, logind/elogind; this program is only the screen in front of it, in the
// spirit of the 1990s desktops: the desktop background, a menu bar at the top
// (the theme, the background, the session, the language, sleep/restart/shut down,
// the clock) and a window in the middle with the users and the password.
//
// The theme, background and language chosen here are saved in the home folder of the
// greeter user (LightDM: /var/lib/lightdm/.config/nirvana) - they stay after a restart.
// The last user is remembered there too (dm.conf).
//
//   nirvana-dm-greeter          started by LightDM (greeter-session=nirvana-dm)
//   nirvana-dm-greeter --test   a preview in a window - nothing is logged in
#include "platinum.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/fl_draw.H>

#include <lightdm.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace plat;

namespace {

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
LightDMGreeter *g_greeter = nullptr;
bool g_test = false;                 // a preview without LightDM

struct User { std::string name, display, image, session; bool logged_in = false; };
std::vector<User> g_users;
struct Session { std::string key, name; };
std::vector<Session> g_sessions;
int g_user = -1;                     // the chosen user (index), -1: typed by hand
bool g_manual = false;               // the "Other…" user: the name is typed
bool g_prompted = false;             // LightDM is waiting for the password
std::string g_message;
bool g_message_error = false;

std::string config_dir() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/tmp") + "/.config") + "/nirvana";
}

std::string read_key(const char *key) {
  std::ifstream in(config_dir() + "/dm.conf");
  std::string l, k = std::string(key) + "=";
  while (std::getline(in, l))
    if (!l.compare(0, k.size(), k)) return l.substr(k.size());
  return "";
}

void write_last_user(const std::string &user) {
  mkdir(config_dir().c_str(), 0755);
  std::ofstream out(config_dir() + "/dm.conf");
  out << "# Nirvana-DM\nlast_user=" << user << "\n";
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------
class Screen;
Screen *g_screen = nullptr;
void build_ui();
void start_auth();

// A user: the picture (~/.face) or a figure, the name below; the chosen one is highlighted.
class UserIcon : public Fl_Widget {
public:
  int idx;   // -2: "Other…"
  Fl_Image *face = nullptr;
  UserIcon(int X, int Y, int i) : Fl_Widget(X, Y, 84, 86), idx(i) {
    if (i >= 0 && !g_users[i].image.empty() && !access(g_users[i].image.c_str(), R_OK)) {
      if (Fl_Shared_Image *img = Fl_Shared_Image::get(g_users[i].image.c_str())) {
        face = img->copy(48, 48);
        img->release();
      }
    }
  }
  ~UserIcon() override { delete face; }
  bool chosen() const { return idx == -2 ? g_manual : (!g_manual && g_user == idx); }
  void draw() override {
    int fx = x() + (w() - 52) / 2, fy = y() + 2;
    if (chosen()) {
      fl_color(SEL);
      fl_rectf(fx - 4, fy - 2, 60, 58);
    }
    fl_draw_box(WELL_BOX, fx, fy, 52, 52, CONTENT);
    if (face) {
      face->draw(fx + 2, fy + 2);
    } else {   // a figure: head and shoulders
      Fl_Color c = idx == -2 ? MID : fl_color_average(SEL, CONTENT, 0.7f);
      fl_color(c);
      fl_pie(fx + 16, fy + 8, 20, 20, 0, 360);
      fl_pie(fx + 8, fy + 30, 36, 30, 0, 180);
      fl_color(DARK);
      fl_arc(fx + 16, fy + 8, 20, 20, 0, 360);
      if (idx == -2) {
        fl_font(FONT_SYSTEM, 16);
        fl_color(TEXT);
        fl_draw("?", fx, fy, 52, 52, FL_ALIGN_CENTER);
      }
    }
    std::string label = idx == -2 ? _("Other…") : (g_users[idx].display.empty() ? g_users[idx].name : g_users[idx].display);
    fl_font(chosen() ? FONT_SYSTEM : FONT_SMALL, SIZE_SMALL + 1);
    label = ellipsize(label, w() - 4);
    int tw = (int)fl_width(label.c_str());
    if (chosen()) {
      fl_color(SEL);
      fl_rectf(x() + (w() - tw) / 2 - 3, y() + 62, tw + 6, 16);
      fl_color(SEL_TEXT);
    } else {
      fl_color(TEXT);
    }
    fl_draw(label.c_str(), x() + (w() - tw) / 2, y() + 74);
    if (idx >= 0 && g_users[idx].logged_in) {   // already logged in: a small green dot
      fl_color(fl_rgb_color(0x3D, 0xB5, 0x4A));
      fl_pie(fx + 44, fy + 44, 9, 9, 0, 360);
    }
  }
  int handle(int e) override;
};

// The desktop background, the menu bar and the login window drawn as one screen.
class Screen : public Fl_Double_Window {
public:
  Fl_RGB_Image *bg = nullptr;
  int lx, ly, lw, lh;              // the login window (frame included)
  Fl_Menu_Bar *menu = nullptr;
  Fl_Box *clock = nullptr, *msg = nullptr, *name_label = nullptr;
  Fl_Input *name = nullptr;
  Fl_Secret_Input *pass = nullptr;
  Fl_Choice *session = nullptr;
  Fl_Button *login = nullptr;
  std::vector<UserIcon *> icons;
  std::string clock_text, msg_text;

  Screen(int X, int Y, int W, int H) : Fl_Double_Window(X, Y, W, H, "Nirvana-DM") {}
  ~Screen() override { delete bg; }

  void make_background() {
    delete bg;
    bg = render_desktop(current_appearance(), w(), h());
  }

  void draw() override {
    if (bg) bg->draw(0, 0);
    else { fl_color(DESK_A); fl_rectf(0, 0, w(), h()); }
    fl_draw_box(MENUBAR_BOX, 0, 0, 30, MENUBAR_H, GRAY);   // the logo before the menus
    draw_logo(8, 2, 16);
    // the login window: a Platinum frame with a title bar, without buttons
    fl_draw_box(UP_BOX, lx, ly, lw, lh, GRAY);
    fl_color(DARK);
    fl_rect(lx - 1, ly - 1, lw + 2, lh + 2);
    std::string title = fmt(_("Welcome to %1"), std::string(lightdm_get_hostname() ? lightdm_get_hostname() : "Nirvana"));
    draw_titlebar(lx, ly, lw, title.c_str(), true, 0, 0);
    draw_logo(lx + 22, ly + TITLE_H + 22, 64);
    fl_font(FONT_SYSTEM, SIZE_SYSTEM + 2);
    fl_color(TEXT);
    fl_draw("Nirvana", lx + 22, ly + TITLE_H + 108);
    fl_font(FONT_SMALL, SIZE_SMALL);
    fl_draw(NIRVANA_VERSION, lx + 22, ly + TITLE_H + 124);
    // a 1-pixel groove between the logo and the form
    fl_color(SHADOW);
    fl_yxline(lx + 112, ly + TITLE_H + 16, ly + lh - 56);
    fl_color(LIGHT);
    fl_yxline(lx + 113, ly + TITLE_H + 16, ly + lh - 56);
    draw_children();
  }

  int handle(int e) override {
    if (e == FL_KEYBOARD && Fl::event_key() == FL_Escape) {   // Escape: back to the choice of users
      if (g_manual && !g_users.empty()) {
        g_manual = false;
        if (g_user < 0) g_user = 0;
        Fl::add_timeout(0.0, [](void *) { build_ui(); });
      }
      return 1;
    }
    return Fl_Double_Window::handle(e);
  }
};

int UserIcon::handle(int e) {
  if (e == FL_PUSH) {
    if (idx == -2) { g_manual = true; g_user = -1; }
    else { g_manual = false; g_user = idx; }
    build_ui();
    return 1;
  }
  return Fl_Widget::handle(e);
}

// ---------------------------------------------------------------------------
// Messages and the clock
// ---------------------------------------------------------------------------
void show_message(const std::string &m, bool error) {
  g_message = m;
  g_message_error = error;
  if (!g_screen || !g_screen->msg) return;
  g_screen->msg_text = m;
  g_screen->msg->label(g_screen->msg_text.c_str());
  g_screen->msg->labelcolor(error ? fl_rgb_color(0xC0, 0x20, 0x20) : TEXT);
  g_screen->redraw();
}

void clock_tick(void *) {
  if (g_screen && g_screen->clock) {
    ClockSettings c = read_clock();   // the greeter's own clock.conf (24 h by default)
    c.weekday = c.date = true;
    g_screen->clock_text = clock_text(c, time(nullptr), false);
    g_screen->clock->label(g_screen->clock_text.c_str());
    g_screen->clock->redraw();
  }
  Fl::repeat_timeout(10.0, clock_tick);
}

// ---------------------------------------------------------------------------
// Authentication (LightDM)
// ---------------------------------------------------------------------------
std::string chosen_user() {
  if (g_manual) return g_screen && g_screen->name ? g_screen->name->value() : "";
  return g_user >= 0 && g_user < (int)g_users.size() ? g_users[g_user].name : "";
}

std::string chosen_session() {
  if (!g_screen || !g_screen->session) return "";
  int i = g_screen->session->value();
  return i >= 0 && i < (int)g_sessions.size() ? g_sessions[i].key : "";
}

// a new conversation with PAM for the chosen user
void start_auth() {
  g_prompted = false;
  if (g_test || !g_greeter) return;
  GError *err = nullptr;
  if (lightdm_greeter_get_in_authentication(g_greeter)) lightdm_greeter_cancel_authentication(g_greeter, nullptr);
  std::string u = chosen_user();
  if (u.empty()) return;
  if (!lightdm_greeter_authenticate(g_greeter, u.c_str(), &err)) {
    show_message(err ? err->message : _("Logging in is not possible now."), true);
    if (err) g_error_free(err);
  }
}

void do_login() {
  if (g_test) {
    show_message(_("This is a preview - nobody is logged in."), false);
    return;
  }
  std::string u = chosen_user();
  if (u.empty()) { show_message(_("Choose a user or type a name."), true); return; }
  if (!g_prompted) {   // PAM has not asked yet (e.g. the name was just typed): wait for it, up to 6 s
    static int tries = 0;
    if (!lightdm_greeter_get_in_authentication(g_greeter)) { start_auth(); tries = 0; }
    if (++tries > 20) { tries = 0; show_message(_("Logging in is not possible now."), true); return; }
    Fl::add_timeout(0.3, [](void *) { do_login(); });
    return;
  }
  GError *err = nullptr;
  g_prompted = false;
  show_message(_("Logging in…"), false);
  if (!lightdm_greeter_respond(g_greeter, g_screen->pass->value(), &err)) {
    show_message(err ? err->message : _("Logging in is not possible now."), true);
    if (err) g_error_free(err);
  }
}

void on_prompt(LightDMGreeter *, const gchar *text, LightDMPromptType type, gpointer) {
  g_prompted = true;
  // "Password:" is the usual question; other ones (a new password, a code) are shown as they are
  std::string t = text ? text : "";
  if (type != LIGHTDM_PROMPT_TYPE_SECRET || (t.find("assword") == std::string::npos && !t.empty()))
    show_message(t, false);
  if (g_screen && g_screen->pass) {
    g_screen->pass->value("");
    Fl::focus(g_screen->pass);
  }
}

void on_message(LightDMGreeter *, const gchar *text, LightDMMessageType type, gpointer) {
  show_message(text ? text : "", type == LIGHTDM_MESSAGE_TYPE_ERROR);
}

void on_complete(LightDMGreeter *greeter, gpointer) {
  if (lightdm_greeter_get_is_authenticated(greeter)) {
    std::string user = lightdm_greeter_get_authentication_user(greeter) ? lightdm_greeter_get_authentication_user(greeter) : "";
    write_last_user(user);
    std::string s = chosen_session();
    GError *err = nullptr;
    show_message(_("Starting the session…"), false);
    if (!lightdm_greeter_start_session_sync(greeter, s.empty() ? nullptr : s.c_str(), &err)) {
      show_message(fmt(_("The session could not start: %1"), err ? err->message : "?"), true);
      if (err) g_error_free(err);
      start_auth();
    }
    return;
  }
  show_message(_("The password is not right. Try again."), true);
  if (g_screen && g_screen->pass) g_screen->pass->value("");
  start_auth();
}

void glib_pump(void *) {   // GLib (LightDM) events inside the FLTK loop
  while (g_main_context_iteration(nullptr, FALSE)) {}
  Fl::repeat_timeout(0.02, glib_pump);
}

// ---------------------------------------------------------------------------
// The menu: theme, background, session, language, power
// ---------------------------------------------------------------------------
std::vector<Scheme> g_schemes;
std::vector<std::string> g_wallpapers;

void apply_look(const Appearance &a) {
  write_appearance(a);   // in the greeter's home: it stays after a restart
  reload_appearance();
  build_ui();
}

void theme_cb(Fl_Widget *, void *d) {
  Appearance a = read_appearance();
  a.scheme = g_schemes[(long)d].id;
  a.style = -1;   // the title bars as the scheme wants
  apply_look(a);
}

void pattern_cb(Fl_Widget *, void *d) {
  Appearance a = read_appearance();
  a.background = "pattern";
  a.pattern = PATTERNS[(long)d];
  a.wallpaper.clear();
  apply_look(a);
}

void wallpaper_cb(Fl_Widget *, void *d) {
  Appearance a = read_appearance();
  a.wallpaper = g_wallpapers[(long)d];
  a.wallpaper_mode = "fill";
  apply_look(a);
}

void language_cb(Fl_Widget *, void *d) {
  set_language_setting((const char *)d);
  reload_i18n();
  build_ui();
}

void power_cb(Fl_Widget *, void *d) {
  if (g_test) { show_message(_("This is a preview - nothing happens."), false); return; }
  GError *err = nullptr;
  bool ok = (long)d == 1 ? lightdm_suspend(&err) : (long)d == 2 ? lightdm_restart(&err) : lightdm_shutdown(&err);
  if (!ok) {
    show_message(err ? err->message : _("Not possible now."), true);
    if (err) g_error_free(err);
  }
}

// menu labels may contain "/" and "&" - FLTK would read them as submenus and shortcuts
std::string menu_label(std::string s) {
  std::string r;
  for (char c : s) {
    if (c == '/' || c == '\\' || c == '&' || c == '_') r += '\\';
    r += c;
  }
  return r;
}

void build_menu(Fl_Menu_Bar *m) {
  const Appearance &cur = current_appearance();
  g_schemes = list_schemes();
  for (size_t i = 0; i < g_schemes.size(); i++) {
    std::string label = std::string(_("Theme")) + "/" + menu_label(_(g_schemes[i].name.c_str()));
    int flags = FL_MENU_RADIO | (g_schemes[i].id == cur.scheme ? FL_MENU_VALUE : 0);
    // the schemes of Nirvana Desktop first, a line before the imported ones
    if (i + 1 < g_schemes.size() && g_schemes[i].source != g_schemes[i + 1].source) flags |= FL_MENU_DIVIDER;
    m->add(label.c_str(), 0, theme_cb, (void *)(long)i, flags);
  }
  for (int i = 0; PATTERNS[i]; i++) {
    std::string label = std::string(_("Background")) + "/" + menu_label(_(PATTERN_NAMES[i]));
    bool on = cur.wallpaper.empty() && cur.background == "pattern" && cur.pattern == PATTERNS[i];
    m->add(label.c_str(), 0, pattern_cb, (void *)(long)i, FL_MENU_RADIO | (on ? FL_MENU_VALUE : 0) |
                                                              (PATTERNS[i + 1] ? 0 : FL_MENU_DIVIDER));
  }
  g_wallpapers.clear();
  for (auto &d : wallpaper_dirs())
    for (auto &p : wallpaper_candidates())
      if (p.rfind(d, 0) == 0 && std::find(g_wallpapers.begin(), g_wallpapers.end(), p) == g_wallpapers.end())
        g_wallpapers.push_back(p);
  for (size_t i = 0; i < g_wallpapers.size(); i++) {
    std::string label = std::string(_("Background")) + "/" + menu_label(wallpaper_title(g_wallpapers[i]));
    m->add(label.c_str(), 0, wallpaper_cb, (void *)(long)i,
           FL_MENU_RADIO | (cur.wallpaper == g_wallpapers[i] ? FL_MENU_VALUE : 0));
  }
  static std::vector<std::string> codes;
  codes.clear();
  for (auto &[code, native] : available_languages()) codes.push_back(code);
  size_t li = 0;
  for (auto &[code, native] : available_languages()) {
    std::string label = std::string(_("Language")) + "/" + menu_label(native);
    m->add(label.c_str(), 0, language_cb, (void *)codes[li++].c_str(),
           FL_MENU_RADIO | (language() == code ? FL_MENU_VALUE : 0));
  }
  bool test = g_test;
  if (test || lightdm_get_can_suspend()) m->add((std::string(_("Special")) + "/" + _("Sleep")).c_str(), 0, power_cb, (void *)1L, FL_MENU_DIVIDER);
  if (test || lightdm_get_can_restart()) m->add((std::string(_("Special")) + "/" + _("Restart…")).c_str(), 0, power_cb, (void *)2L);
  if (test || lightdm_get_can_shutdown()) m->add((std::string(_("Special")) + "/" + _("Shut Down…")).c_str(), 0, power_cb, (void *)3L);
}

// ---------------------------------------------------------------------------
// Building the screen (again after a change of the theme or the language)
// ---------------------------------------------------------------------------
void build_ui() {
  std::string typed_name = g_screen && g_screen->name ? g_screen->name->value() : "";
  std::string keep_session = chosen_session();
  int X = 0, Y = 0, W = Fl::w(), H = Fl::h();
  if (g_test) { W = std::min(W, 1280); H = std::min(H, 800); }
  bool first = !g_screen;
  if (first) {
    g_screen = new Screen(X, Y, W, H);
    if (!g_test) g_screen->border(0);
  }
  Screen *s = g_screen;
  s->clear();
  s->icons.clear();
  s->begin();
  s->color(DESK_A);
  s->make_background();

  // the menu bar: a logo, the menus, the clock on the right
  s->menu = new Fl_Menu_Bar(30, 0, W - 30, MENUBAR_H);
  s->menu->box(MENUBAR_BOX);
  s->menu->textfont(FONT_SYSTEM);
  s->menu->textsize(SIZE_SYSTEM);
  s->menu->color(GRAY);
  s->menu->selection_color(SEL);
  build_menu(s->menu);
  s->clock = new Fl_Box(W - 220, 0, 210, MENUBAR_H);
  s->clock->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
  s->clock->labelfont(FONT_SYSTEM);
  s->clock->labelsize(SIZE_SYSTEM);
  s->clock->labelcolor(TEXT);

  // the login window in the middle of the first screen
  int sx, sy, sw, sh;
  Fl::screen_xywh(sx, sy, sw, sh, 0);
  bool show_users = !g_users.empty() && g_users.size() <= 6 && !(g_greeter && lightdm_greeter_get_hide_users_hint(g_greeter));
  bool allow_manual = g_test || !g_greeter || lightdm_greeter_get_show_manual_login_hint(g_greeter) || !show_users;
  int n_icons = show_users ? (int)g_users.size() + (allow_manual ? 1 : 0) : 0;
  int form_w = std::max(300, n_icons * 86);
  s->lw = 112 + 20 + form_w + 24;
  s->lh = TITLE_H + 250 + (show_users ? 0 : -40);
  s->lx = sx + (sw - s->lw) / 2;
  s->ly = sy + std::max(MENUBAR_H + 20, (sh - s->lh) / 2 - 40);
  int fx = s->lx + 132, fy = s->ly + TITLE_H + 16;

  if (!show_users) g_manual = true;
  if (show_users) {
    for (int i = 0; i < (int)g_users.size(); i++) s->icons.push_back(new UserIcon(fx + i * 86, fy, i));
    if (allow_manual) s->icons.push_back(new UserIcon(fx + (int)g_users.size() * 86, fy, -2));
    fy += 92;
  }
  s->name = nullptr;
  if (g_manual) {
    s->name = new Fl_Input(fx + 76, fy, form_w - 76, 24, _("Name:"));
    s->name->value(typed_name.c_str());
    s->name->when(FL_WHEN_ENTER_KEY_ALWAYS);
    s->name->callback([](Fl_Widget *, void *) { start_auth(); Fl::focus(g_screen->pass); });
    fy += 32;
  } else if (!show_users) {
    fy += 0;
  }
  s->pass = new Fl_Secret_Input(fx + 76, fy, form_w - 76, 24, _("Password:"));
  s->pass->when(FL_WHEN_ENTER_KEY_ALWAYS);
  s->pass->callback([](Fl_Widget *, void *) { do_login(); });
  fy += 32;
  s->session = new Fl_Choice(fx + 76, fy, form_w - 76, 24, _("Session:"));
  int sel = 0;
  std::string want = keep_session;
  if (want.empty() && g_user >= 0 && g_user < (int)g_users.size()) want = g_users[g_user].session;
  if (want.empty() && g_greeter && lightdm_greeter_get_default_session_hint(g_greeter))
    want = lightdm_greeter_get_default_session_hint(g_greeter);
  if (want.empty()) want = "nirvana";
  for (size_t i = 0; i < g_sessions.size(); i++) {
    s->session->add(menu_label(g_sessions[i].name).c_str());
    if (g_sessions[i].key == want) sel = (int)i;
  }
  if (!g_sessions.empty()) s->session->value(sel);
  fy += 34;
  s->msg = new Fl_Box(fx, fy, form_w - 110, 34);
  s->msg->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  s->msg->labelsize(SIZE_SMALL);
  s->login = new Fl_Button(fx + form_w - 100, fy, 100, 26, _("Log In"));
  s->login->labelfont(FONT_SYSTEM);
  s->login->callback([](Fl_Widget *, void *) { do_login(); });
  for (Fl_Widget *w : std::initializer_list<Fl_Widget *>{s->name, s->pass, s->session}) {
    if (!w) continue;
    w->labelfont(FONT_SYSTEM);
    w->labelsize(SIZE_SYSTEM);
  }
  for (Fl_Input *i : {s->name, (Fl_Input *)s->pass}) {
    if (!i) continue;
    i->box(WELL_BOX);
    i->color(CONTENT);
    i->textcolor(CONTENT_TEXT);
    i->textsize(SIZE_SYSTEM);
  }
  s->lh = fy + 34 + 16 - s->ly;   // as tall as the content
  s->login->box(BUTTON_BOX);
  s->login->down_box(BUTTON_DOWN);
  s->login->color(GRAY);
  s->end();
  show_message(g_message, g_message_error);
  clock_tick(nullptr);
  Fl::remove_timeout(clock_tick);
  Fl::add_timeout(10.0, clock_tick);
  if (first) s->show();
  s->redraw();
  if (s->name && chosen_user().empty()) Fl::focus(s->name);
  else Fl::focus(s->pass);
  start_auth();
}

void load_users_and_sessions() {
  if (g_test) {
    const char *u = getenv("USER");
    const char *home = getenv("HOME");
    std::string face = home ? std::string(home) + "/.face" : "";
    g_users.push_back({u ? u : "user", "", access(face.c_str(), R_OK) ? "" : face, "nirvana", false});
    g_sessions = {{"nirvana", "Nirvana Desktop"}, {"other", "Other Desktop"}};
    g_user = 0;
    return;
  }
  for (GList *l = lightdm_user_list_get_users(lightdm_user_list_get_instance()); l; l = l->next) {
    auto *u = (LightDMUser *)l->data;
    User x;
    x.name = lightdm_user_get_name(u) ? lightdm_user_get_name(u) : "";
    x.display = lightdm_user_get_display_name(u) ? lightdm_user_get_display_name(u) : "";
    x.image = lightdm_user_get_image(u) ? lightdm_user_get_image(u) : "";
    // a home folder is usually closed to the login screen: the copy that Preferences > User
    // keeps in AccountsService (also without the accountsservice daemon)
    if (x.image.empty() || access(x.image.c_str(), R_OK)) {
      std::string as = "/var/lib/AccountsService/icons/" + x.name;
      x.image = access(as.c_str(), R_OK) ? "" : as;
    }
    x.session = lightdm_user_get_session(u) ? lightdm_user_get_session(u) : "";
    x.logged_in = lightdm_user_get_logged_in(u);
    g_users.push_back(x);
  }
  for (GList *l = lightdm_get_sessions(); l; l = l->next) {
    auto *ss = (LightDMSession *)l->data;
    g_sessions.push_back({lightdm_session_get_key(ss) ? lightdm_session_get_key(ss) : "",
                          lightdm_session_get_name(ss) ? lightdm_session_get_name(ss) : ""});
  }
  // the user to start with: LightDM's hint, else the last one from here, else the first
  std::string hint = lightdm_greeter_get_select_user_hint(g_greeter) ? lightdm_greeter_get_select_user_hint(g_greeter) : "";
  if (hint.empty()) hint = read_key("last_user");
  g_user = g_users.empty() ? -1 : 0;
  for (size_t i = 0; i < g_users.size(); i++) if (g_users[i].name == hint) g_user = (int)i;
}

}  // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--test")) g_test = true;
  fl_register_images();
  init();
  if (!g_test) {
    g_greeter = lightdm_greeter_new();
    g_signal_connect(g_greeter, LIGHTDM_GREETER_SIGNAL_SHOW_PROMPT, G_CALLBACK(on_prompt), nullptr);
    g_signal_connect(g_greeter, LIGHTDM_GREETER_SIGNAL_SHOW_MESSAGE, G_CALLBACK(on_message), nullptr);
    g_signal_connect(g_greeter, LIGHTDM_GREETER_SIGNAL_AUTHENTICATION_COMPLETE, G_CALLBACK(on_complete), nullptr);
    GError *err = nullptr;
    if (!lightdm_greeter_connect_to_daemon_sync(g_greeter, &err)) {
      fprintf(stderr, "nirvana-dm-greeter: cannot connect to LightDM (%s) - showing a preview\n", err ? err->message : "?");
      if (err) g_error_free(err);
      g_test = true;
      g_object_unref(g_greeter);
      g_greeter = nullptr;
    }
  }
  load_users_and_sessions();
  if (g_test) show_message(_("Preview: nobody is logged in from here."), false);
  build_ui();
  if (!g_test) {
    Fl::add_timeout(0.02, glib_pump);
    fl_cursor(FL_CURSOR_DEFAULT);
  }
  return Fl::run();
}
