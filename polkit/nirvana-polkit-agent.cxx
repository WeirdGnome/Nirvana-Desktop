// Nirvana Desktop - nirvana-polkit-agent: the password window of the system
//
// A polkit authentication agent: when a program asks for administrator rights (pkexec, disks,
// network, updates, printers...), polkit asks the agent of the session, and this window asks
// for the password. The password goes to polkit's own helper (polkit-agent-helper-1, PAM);
// this program only passes it on and never keeps it.
//
//   nirvana-polkit-agent                 the agent of this session (nirvana-autostart starts it)
//   nirvana-polkit-agent --process PID   only for one process and its children (tests)
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE 1
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/fl_draw.H>
#include <polkitagent/polkitagent.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <pwd.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace plat;

// --- the listener (a GObject subclass) --------------------------------------------------
struct NirvanaListener { PolkitAgentListener parent; };
struct NirvanaListenerClass { PolkitAgentListenerClass parent_class; };
G_DEFINE_TYPE(NirvanaListener, nirvana_listener, POLKIT_AGENT_TYPE_LISTENER)

namespace {

struct Request {
  std::string action, message, cookie, program;
  std::vector<PolkitIdentity *> identities;
  GTask *task = nullptr;
  GCancellable *cancellable = nullptr;
  gulong cancel_id = 0;
};

std::deque<Request *> g_queue;   // one window at a time, the others wait
Request *g_cur = nullptr;
PolkitAgentSession *g_session = nullptr;
std::string g_password;
bool g_answered = false;

class AuthWindow;
AuthWindow *g_win = nullptr;
void next_request();

std::string user_name(PolkitIdentity *id, bool full) {
  if (!POLKIT_IS_UNIX_USER(id)) {
    gchar *s = polkit_identity_to_string(id);
    std::string r = s ? s : "?";
    g_free(s);
    return r;
  }
  uid_t uid = polkit_unix_user_get_uid(POLKIT_UNIX_USER(id));
  struct passwd *pw = getpwuid(uid);
  if (!pw) return std::to_string(uid);
  std::string name = pw->pw_name, gecos = pw->pw_gecos ? pw->pw_gecos : "";
  gecos = gecos.substr(0, gecos.find(','));
  if (full && !gecos.empty() && gecos != name) return gecos + " (" + name + ")";
  return name;
}

// A padlock with a key - the picture of the window.
class LockIcon : public Fl_Widget {
public:
  LockIcon(int X, int Y) : Fl_Widget(X, Y, 56, 64) {}
  void draw() override {
    int X = x() + 6, Y = y() + 2;
    fl_color(fl_rgb_color(0x8A, 0x8A, 0x96));
    fl_line_style(FL_SOLID, 5);
    fl_arc(X + 8, Y + 2, 28, 34, 0, 180);
    fl_line(X + 10, Y + 19, X + 10, Y + 28);
    fl_line(X + 34, Y + 19, X + 34, Y + 28);
    fl_line_style(0);
    fl_color(fl_rgb_color(0xE8, 0xB8, 0x30));
    fl_rectf(X + 2, Y + 26, 40, 30);
    fl_color(fl_rgb_color(0xFF, 0xE0, 0x80));
    fl_rectf(X + 4, Y + 28, 36, 4);
    fl_color(fl_rgb_color(0xB0, 0x80, 0x10));
    fl_rectf(X + 4, Y + 50, 36, 4);
    fl_color(DARK);
    fl_rect(X + 2, Y + 26, 40, 30);
    fl_pie(X + 18, Y + 34, 8, 8, 0, 360);
    fl_rectf(X + 20, Y + 40, 4, 8);
  }
};

class AuthWindow : public FrameWindow {
public:
  Fl_Box *title, *text, *prog, *status;
  Fl_Choice *who = nullptr;
  Fl_Box *who_label = nullptr;
  Fl_Secret_Input *pass;
  Fl_Button *cancel, *ok;
  std::string title_s, text_s, prog_s, status_s, who_s;

  AuthWindow(Request *r) : FrameWindow(470, 238, _("Authentication"), true) {
    xclass("nirvana-polkit-agent");
    new LockIcon(14, 14);
    bool self = r->identities.size() == 1 && POLKIT_IS_UNIX_USER(r->identities[0]) &&
                polkit_unix_user_get_uid(POLKIT_UNIX_USER(r->identities[0])) == getuid();
    title = new Fl_Box(84, 12, 372, 20, self ? _("Confirm with your password") : _("Administrator rights are needed"));
    title->labelfont(FONT_SYSTEM);
    title->labelsize(SIZE_SYSTEM + 1);
    title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    text_s = r->message.empty() ? std::string(_("A program wants to do something that needs your permission.")) : r->message;
    text = new Fl_Box(84, 34, 372, 36, text_s.c_str());
    text->labelsize(SIZE_SMALL + 1);
    text->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    prog_s = r->program.empty() ? r->action : fmt(_("Program: %1"), r->program);
    prog = new Fl_Box(84, 72, 372, 30, prog_s.c_str());
    prog->labelsize(SIZE_SMALL);
    prog->labelcolor(fl_color_average(TEXT, GRAY, 0.6f));
    prog->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
    prog->tooltip(r->action.c_str());
    int y = 108;
    if (r->identities.size() > 1) {
      who = new Fl_Choice(200, y, 256, 22, _("Authenticate as:"));
      who->labelsize(SIZE_SMALL + 1);
      who->textsize(SIZE_SMALL + 1);
      int pick = 0;
      for (size_t i = 0; i < r->identities.size(); i++) {
        std::string n = user_name(r->identities[i], true);
        std::string esc;
        for (char c : n) { if (c == '/' || c == '\\' || c == '&') esc += '\\'; esc += c; }
        who->add(esc.c_str());
        // the person at the computer first, when they may authenticate
        if (POLKIT_IS_UNIX_USER(r->identities[i]) && polkit_unix_user_get_uid(POLKIT_UNIX_USER(r->identities[i])) == getuid())
          pick = (int)i;
      }
      who->value(pick);
    } else {
      who_s = r->identities.empty() ? "" : fmt(_("Authenticate as %1."), user_name(r->identities[0], true));
      who_label = new Fl_Box(84, y, 372, 22, who_s.c_str());
      who_label->labelsize(SIZE_SMALL + 1);
      who_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    }
    y += 30;
    pass = new Fl_Secret_Input(200, y, 256, 22, _("Password:"));
    pass->labelsize(SIZE_SMALL + 1);
    pass->textsize(SIZE_SMALL + 1);
    y += 28;
    status = new Fl_Box(84, y, 372, 30);
    status->labelsize(SIZE_SMALL);
    status->labelfont(FONT_SYSTEM);
    status->labelcolor(ALERT);
    status->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    cancel = new Fl_Button(470 - 222, 238 - 36, 100, 24, _("Cancel"));
    ok = new Fl_Button(470 - 114, 238 - 36, 100, 24, _("Authenticate"));
    for (auto *b : {cancel, ok}) {
      b->box(BUTTON_BOX);
      b->down_box(BUTTON_DOWN);
      b->labelsize(SIZE_SMALL + 1);
    }
    ok->labelfont(FONT_SYSTEM);
    ok->shortcut(FL_Enter);
    cancel->shortcut(FL_Escape);
    cancel->callback([](Fl_Widget *, void *) { done(false, true); });
    ok->callback([](Fl_Widget *, void *) { start_session(); });
    callback([](Fl_Widget *, void *) { done(false, true); });
    end();
    finish();
  }

  void message(const std::string &m) {
    status_s = m;
    status->label(status_s.c_str());
    status->redraw();
  }
  void busy(bool b) {
    if (b) { pass->deactivate(); ok->deactivate(); if (who) who->deactivate(); }
    else { pass->activate(); ok->activate(); if (who) who->activate(); }
  }

  static void start_session();
  static void done(bool gained, bool cancelled);
};

void on_request(PolkitAgentSession *s, const gchar *, gboolean, gpointer) {
  if (s != g_session) return;
  if (g_answered) {   // asked again (e.g. a PAM module wants more): back to the window
    if (g_win) { g_win->busy(false); g_win->pass->value(""); g_win->pass->take_focus(); }
    return;
  }
  g_answered = true;
  polkit_agent_session_response(s, g_password.c_str());
  std::fill(g_password.begin(), g_password.end(), '\0');
  g_password.clear();
}

void on_show_error(PolkitAgentSession *s, const gchar *t, gpointer) {
  if (s == g_session && g_win && t) g_win->message(t);
}

void on_show_info(PolkitAgentSession *s, const gchar *t, gpointer) {
  if (s == g_session && g_win && t) g_win->message(t);
}

void drop_session() {
  if (!g_session) return;
  g_signal_handlers_disconnect_matched(g_session, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, nullptr);
  g_object_unref(g_session);
  g_session = nullptr;
}

void on_completed(PolkitAgentSession *s, gboolean gained, gpointer) {
  if (s != g_session) return;
  drop_session();
  if (gained) { AuthWindow::done(true, false); return; }
  if (!g_win) return;
  g_win->busy(false);
  g_win->message(_("That password is not right - try again."));
  g_win->pass->value("");
  g_win->pass->take_focus();
}

void AuthWindow::start_session() {
  if (!g_cur || !g_win || g_session) return;
  int i = g_win->who ? g_win->who->value() : 0;
  if (i < 0 || i >= (int)g_cur->identities.size()) return;
  g_password = g_win->pass->value();
  g_answered = false;
  g_win->busy(true);
  g_win->message(_("Checking…"));
  g_session = polkit_agent_session_new(g_cur->identities[i], g_cur->cookie.c_str());
  g_signal_connect(g_session, "request", G_CALLBACK(on_request), nullptr);
  g_signal_connect(g_session, "show-error", G_CALLBACK(on_show_error), nullptr);
  g_signal_connect(g_session, "show-info", G_CALLBACK(on_show_info), nullptr);
  g_signal_connect(g_session, "completed", G_CALLBACK(on_completed), nullptr);
  polkit_agent_session_initiate(g_session);
}

void free_request(Request *r) {
  for (auto *id : r->identities) g_object_unref(id);
  if (r->cancellable) {
    if (r->cancel_id) g_cancellable_disconnect(r->cancellable, r->cancel_id);
    g_object_unref(r->cancellable);
  }
  delete r;
}

// ends the current request: the answer goes back to polkit
void AuthWindow::done(bool gained, bool cancelled) {
  if (!g_cur) return;
  if (g_session) { polkit_agent_session_cancel(g_session); drop_session(); }
  g_password.clear();
  Request *r = g_cur;
  g_cur = nullptr;
  if (gained) g_task_return_boolean(r->task, TRUE);
  else if (cancelled) g_task_return_new_error(r->task, POLKIT_ERROR, POLKIT_ERROR_CANCELLED, "%s", "The user cancelled");
  else g_task_return_new_error(r->task, POLKIT_ERROR, POLKIT_ERROR_FAILED, "%s", "Not authorized");
  g_object_unref(r->task);
  free_request(r);
  if (g_win) {
    g_win->hide();
    Fl::delete_widget(g_win);
    g_win = nullptr;
  }
  Fl::add_timeout(0.0, [](void *) { next_request(); });
}

void show_request(Request *r) {
  g_cur = r;
  g_win = new AuthWindow(r);
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->place(sx + (sw - g_win->content_w()) / 2, sy + (sh - g_win->content_h()) / 3);
  g_win->show();
  g_win->pass->take_focus();
}

void next_request() {
  if (g_cur || g_queue.empty()) return;
  Request *r = g_queue.front();
  g_queue.pop_front();
  show_request(r);
}

void on_cancelled(GCancellable *, gpointer data) {
  auto *r = (Request *)data;
  // polkit (or the program) gave up: the window goes away
  Fl::add_timeout(0.0, [](void *d) {
    auto *r = (Request *)d;
    if (r == g_cur) { AuthWindow::done(false, true); return; }
    for (auto it = g_queue.begin(); it != g_queue.end(); ++it)
      if (*it == r) {
        g_queue.erase(it);
        g_task_return_new_error(r->task, POLKIT_ERROR, POLKIT_ERROR_CANCELLED, "%s", "Cancelled");
        g_object_unref(r->task);
        free_request(r);
        break;
      }
  }, r);
}

void initiate(PolkitAgentListener *listener, const gchar *action_id, const gchar *message, const gchar *,
              PolkitDetails *details, const gchar *cookie, GList *identities, GCancellable *cancellable,
              GAsyncReadyCallback callback, gpointer user_data) {
  auto *r = new Request;
  r->action = action_id ? action_id : "";
  r->message = message ? message : "";
  r->cookie = cookie ? cookie : "";
  if (details) {
    const gchar *p = polkit_details_lookup(details, "command_line");
    if (!p) p = polkit_details_lookup(details, "program");
    if (p) r->program = p;
  }
  for (GList *l = identities; l; l = l->next) r->identities.push_back((PolkitIdentity *)g_object_ref(l->data));
  r->task = g_task_new(listener, cancellable, callback, user_data);
  if (cancellable) {
    r->cancellable = (GCancellable *)g_object_ref(cancellable);
    r->cancel_id = g_cancellable_connect(cancellable, G_CALLBACK(on_cancelled), r, nullptr);
  }
  g_queue.push_back(r);
  next_request();
}

gboolean initiate_finish(PolkitAgentListener *, GAsyncResult *res, GError **error) {
  return g_task_propagate_boolean(G_TASK(res), error);
}

void glib_pump(void *) {
  while (g_main_context_iteration(nullptr, FALSE)) {}
  Fl::repeat_timeout(0.02, glib_pump);
}

}  // namespace

static void nirvana_listener_init(NirvanaListener *) {}
static void nirvana_listener_class_init(NirvanaListenerClass *k) {
  auto *lc = POLKIT_AGENT_LISTENER_CLASS(k);
  lc->initiate_authentication = initiate;
  lc->initiate_authentication_finish = initiate_finish;
}

int main(int argc, char **argv) {
  init();
  Fl::visual(FL_RGB);
  GError *err = nullptr;
  PolkitSubject *subject = nullptr;
  if (argc > 2 && !strcmp(argv[1], "--process")) {
    subject = polkit_unix_process_new_for_owner(atoi(argv[2]), 0, getuid());
  } else {
    subject = polkit_unix_session_new_for_process_sync(getpid(), nullptr, &err);
    if (!subject) {
      fprintf(stderr, "nirvana-polkit-agent: no login session (elogind/logind): %s\n", err ? err->message : "?");
      return 1;
    }
  }
  auto *listener = (PolkitAgentListener *)g_object_new(nirvana_listener_get_type(), nullptr);
  gpointer handle = polkit_agent_listener_register(listener, POLKIT_AGENT_REGISTER_FLAGS_NONE, subject,
                                                   "/org/nirvana_desktop/PolicyKit1/AuthenticationAgent", nullptr, &err);
  if (!handle) {
    // usually: another agent is already running for this session
    fprintf(stderr, "nirvana-polkit-agent: %s\n", err ? err->message : "could not register");
    return 1;
  }
  Fl::add_timeout(0.02, glib_pump);
  for (;;) Fl::wait(1.0);
}
