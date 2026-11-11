// Nirvana Desktop - alice: the "Printers" panel
//
// The printers of CUPS with their state, adding (found devices, driverless IPP or a
// driver from CUPS), removing, the default printer, a test page, pausing the queue -
// and installing the printing system or drivers when they are missing. Everything that
// needs root goes through nirvana-print-helper with pkexec.
#include "framewin.h"
#include "platinum.h"

#ifdef NIRVANA_PRINT
#include "print.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>

#include <algorithm>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace plat;

namespace {

FrameWindow *g_win;
Fl_Hold_Browser *g_list;
Fl_Box *g_details, *g_status;
Fl_Button *g_remove, *g_default, *g_test, *g_pause;
printing::State g_st;
std::string g_details_text, g_status_text;

void status(const std::string &s) {
  g_status_text = s;
  g_status->label(g_status_text.c_str());
  g_status->redraw();
  Fl::check();
}

// The helper as root; the window stays alive and the last line of the output is shown.
int run_helper(const std::vector<std::string> &args, std::string *out) {
  std::string h = printing::helper_path();
  if (h.empty()) { *out = _("nirvana-print-helper is missing."); return 127; }
  int p[2];
  if (pipe(p)) return 127;
  pid_t pid = fork();
  if (pid == 0) {
    dup2(p[1], 1);
    dup2(p[1], 2);
    close(p[0]);
    close(p[1]);
    static std::string pk = plat::pkexec_command();
    std::vector<char *> a = {(char *)pk.c_str(), (char *)h.c_str()};
    for (auto &s : args) a.push_back((char *)s.c_str());
    a.push_back(nullptr);
    execvp(a[0], a.data());
    _exit(127);
  }
  close(p[1]);
  out->clear();
  char buf[1024];
  ssize_t n;
  while ((n = read(p[0], buf, sizeof buf)) != 0) {
    if (n > 0) {
      out->append(buf, (size_t)n);
      size_t e = out->find_last_not_of("\n");
      size_t b = e == std::string::npos ? std::string::npos : out->rfind('\n', e);
      if (e != std::string::npos) {
        std::string last = out->substr(b == std::string::npos ? 0 : b + 1, e - (b == std::string::npos ? 0 : b + 1) + 1);
        if (!last.compare(0, 5, "STEP ")) status(fmt(_("Installing %1…"), last.substr(16)));
      }
    }
    Fl::check();
  }
  close(p[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  while (!out->empty() && (out->back() == '\n' || out->back() == ' ')) out->pop_back();
  return WIFEXITED(st) ? WEXITSTATUS(st) : 127;
}

std::string last_line(const std::string &s) {
  size_t b = s.rfind('\n');
  return b == std::string::npos ? s : s.substr(b + 1);
}

const printing::Printer *selected() {
  int i = g_list->value();
  if (i <= 0 || i > (int)g_st.printers.size()) return nullptr;
  return &g_st.printers[i - 1];
}

void show_details() {
  const printing::Printer *p = selected();
  if (!p) {
    g_details_text = g_st.printers.empty() ? _("No printer is set up yet. Click Add Printer…") : "";
  } else {
    g_details_text = fmt(_("Name: %1"), p->name) + "\n" +
                     fmt(_("Description: %1"), p->info.empty() ? "—" : p->info) + "\n" +
                     fmt(_("Driver: %1"), p->make_model.empty() ? "—" : p->make_model) + "\n" +
                     fmt(_("Connection: %1"), p->uri.empty() ? "—" : p->uri) + "\n" +
                     fmt(_("State: %1"), printing::state_text(*p)) + "\n" +
                     fmt(_("Jobs waiting: %1"), std::to_string(p->queued)) +
                     (p->is_default ? "\n\n" + std::string(_("This is your default printer.")) : "");
  }
  g_details->label(g_details_text.c_str());
  bool s = p != nullptr;
  for (auto *b : {g_remove, g_default, g_test, g_pause}) s ? b->activate() : b->deactivate();
  if (p) g_pause->label(p->state == 5 ? _("Resume") : _("Pause"));
  g_win->redraw();
}

void reload(const std::string &select = "") {
  std::string keep = select;
  if (keep.empty() && selected()) keep = selected()->name;
  g_st = printing::query();
  g_list->clear();
  int sel = 0;
  for (size_t i = 0; i < g_st.printers.size(); i++) {
    auto &p = g_st.printers[i];
    std::string row = (p.is_default ? "@b" : "") + (p.info.empty() ? p.name : p.info);
    g_list->add(row.c_str());
    if (p.name == keep) sel = (int)i + 1;
  }
  if (!sel && !g_st.printers.empty()) sel = 1;
  g_list->value(sel);
  show_details();
}

// --- installing drivers -------------------------------------------------------------------
void install_drivers(const std::vector<std::string> &ids) {
  if (ids.empty()) return;
  std::vector<std::string> args = {"install-drivers"};
  args.insert(args.end(), ids.begin(), ids.end());
  status(_("Installing drivers…"));
  std::string out;
  int rc = run_helper(args, &out);
  status(rc == 0 ? std::string(_("The drivers are installed.")) : fmt(_("Installing failed: %1"), last_line(out)));
}

void drivers_dialog(const std::vector<std::string> &preselect) {
  const int W = 460, H = 330;
  auto *d = new FrameWindow(W, H, _("Install Printer Drivers"));
  d->set_modal();
  auto *t = new Fl_Box(15, 10, W - 30, 36, _("Choose the drivers to install. Most new printers print without a driver; "
                                             "older ones need the driver of their manufacturer."));
  t->labelsize(SIZE_SMALL + 1);
  t->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  std::vector<std::pair<std::string, Fl_Check_Button *>> boxes;
  int y = 52;
  for (auto &dr : printing::all_drivers()) {
    auto *c = new Fl_Check_Button(20, y, W - 40, 22);
    c->copy_label(_(dr.label));
    c->labelsize(SIZE_SMALL + 1);
    c->value(std::find(preselect.begin(), preselect.end(), dr.id) != preselect.end());
    boxes.push_back({dr.id, c});
    y += 24;
  }
  bool go = false, done = false;
  auto *cancel = new Fl_Button(W - 196, H - 36, 86, 24, _("Cancel"));
  auto *ok = new Fl_Button(W - 102, H - 36, 86, 24, _("Install"));
  for (auto *b : {cancel, ok}) { b->box(BUTTON_BOX); b->down_box(BUTTON_DOWN); }
  ok->labelfont(FONT_SYSTEM);
  struct Ctx { bool *go, *done; } ctx{&go, &done};
  cancel->callback([](Fl_Widget *, void *c) { *((Ctx *)c)->done = true; }, &ctx);
  ok->callback([](Fl_Widget *, void *c) { *((Ctx *)c)->go = *((Ctx *)c)->done = true; }, &ctx);
  d->end();
  d->finish();
  d->place(g_win->x() + 40, g_win->y() + 40);
  d->show();
  while (!done && d->shown()) Fl::wait();
  std::vector<std::string> ids;
  for (auto &[id, c] : boxes) if (c->value()) ids.push_back(id);
  d->hide();
  Fl::delete_widget(d);
  if (go) install_drivers(ids);
}

// --- adding a printer ------------------------------------------------------------------------
struct AddState {
  FrameWindow *win = nullptr;
  Fl_Hold_Browser *devices = nullptr, *models = nullptr;
  Fl_Input *uri = nullptr, *name = nullptr;
  Fl_Box *hint = nullptr;
  Fl_Button *drivers = nullptr;
  std::vector<printing::Device> found;
  std::vector<printing::Model> all_models, shown_models;   // shown[0] may be "everywhere"
  std::string hint_text;
  std::vector<std::string> suggested;
  bool done = false;
};
AddState *g_add;

std::string safe_name(std::string s) {
  std::string r;
  for (char c : s) r += isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' ? c : '_';
  while (r.find("__") != std::string::npos) r.erase(r.find("__"), 1);
  while (!r.empty() && r.back() == '_') r.pop_back();
  return r.substr(0, 60).empty() ? "Printer" : r.substr(0, 60);
}

void fill_models() {
  auto &a = *g_add;
  a.models->clear();
  a.shown_models.clear();
  std::string uri = a.uri->value();
  printing::Device dev;
  dev.uri = uri;
  for (auto &d : a.found) if (d.uri == uri) dev = d;
  if (dev.driverless()) a.shown_models.push_back({"everywhere", _("Driverless (IPP Everywhere / AirPrint) – recommended")});
  std::string mfg = dev.manufacturer();
  std::string low = mfg;
  std::transform(low.begin(), low.end(), low.begin(), ::tolower);
  if (low == "hewlett-packard") low = "hp";
  size_t matched = 0;
  for (auto &m : a.all_models) {
    std::string d = m.desc;
    std::transform(d.begin(), d.end(), d.begin(), ::tolower);
    if (!low.empty() && d.find(low) != std::string::npos) { a.shown_models.push_back(m); matched++; }
  }
  if (!matched)   // nothing for this manufacturer: the generic drivers
    for (auto &m : a.all_models) {
      std::string d = m.desc;
      std::transform(d.begin(), d.end(), d.begin(), ::tolower);
      if (d.find("generic") != std::string::npos) a.shown_models.push_back(m);
    }
  for (auto &m : a.shown_models) a.models->add(m.desc.c_str());
  if (!a.shown_models.empty()) a.models->value(1);
  a.suggested = printing::suggested_drivers(mfg);
  if (!dev.driverless() && !matched && !uri.empty())
    a.hint_text = fmt(_("No driver for %1 is installed. Install the drivers for this manufacturer and the list fills in."),
                      mfg.empty() ? std::string(_("this printer")) : mfg);
  else if (dev.driverless())
    a.hint_text = _("This printer works without a driver.");
  else
    a.hint_text = "";
  a.hint->label(a.hint_text.c_str());
  a.drivers->label(matched || dev.driverless() ? _("More Drivers…") : _("Install Drivers…"));
  a.win->redraw();
}

void search_devices() {
  auto &a = *g_add;
  status(_("Looking for printers…"));
  std::string out;
  int rc = run_helper({"devices"}, &out);
  a.found = rc == 0 ? printing::parse_devices(out) : std::vector<printing::Device>();
  a.devices->clear();
  for (auto &d : a.found) {
    std::string kind = d.uri.rfind("usb:", 0) == 0 ? _("USB")
                     : d.driverless() || d.cls == "network" ? _("network") : d.cls;
    std::string label = (d.info.empty() ? d.uri : d.info) + "  (" + kind + ")";
    a.devices->add(label.c_str());
  }
  status(rc == 0 ? fmt(trn((long)a.found.size(), "%1 device found.", "%1 devices found."), std::to_string(a.found.size()))
                 : fmt(_("Searching failed: %1"), last_line(out)));
}

void add_dialog() {
  AddState a;
  g_add = &a;
  a.all_models = printing::models();
  const int W = 600, H = 470;
  a.win = new FrameWindow(W, H, _("Add Printer"));
  a.win->set_modal();
  auto *l1 = new Fl_Box(15, 8, 300, 20, _("1. The printer"));
  l1->labelfont(FONT_SYSTEM);
  l1->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  a.devices = new Fl_Hold_Browser(15, 30, W - 30 - 110, 130);
  a.devices->box(WELL_BOX);
  a.devices->textsize(SIZE_SMALL + 1);
  auto *search = new Fl_Button(W - 15 - 100, 30, 100, 24, _("Search"));
  search->callback([](Fl_Widget *, void *) { search_devices(); });
  a.uri = new Fl_Input(110, 168, W - 125, 22, _("Address (URI):"));
  a.uri->labelsize(SIZE_SMALL + 1);
  a.uri->textsize(SIZE_SMALL + 1);
  a.uri->tooltip(_("For a network printer that was not found: ipp://192.168.1.20/ipp/print or socket://192.168.1.20"));
  a.uri->when(FL_WHEN_CHANGED);
  a.uri->callback([](Fl_Widget *, void *) {
    // a typed address: the name from its host ("Printer_192.168.1.50")
    std::string u = g_add->uri->value();
    size_t h = u.find("://");
    if (h != std::string::npos) {
      std::string host = u.substr(h + 3, u.find_first_of("/:?", h + 3) - (h + 3));
      if (!host.empty()) g_add->name->value(safe_name("Printer_" + host).c_str());
    }
    fill_models();
  });
  a.devices->callback([](Fl_Widget *, void *) {
    auto &a = *g_add;
    int i = a.devices->value();
    if (i <= 0) return;
    auto &d = a.found[i - 1];
    a.uri->value(d.uri.c_str());
    a.name->value(safe_name(d.make_model.empty() ? d.info : d.make_model).c_str());
    fill_models();
  });
  auto *l2 = new Fl_Box(15, 200, 300, 20, _("2. The driver"));
  l2->labelfont(FONT_SYSTEM);
  l2->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  a.models = new Fl_Hold_Browser(15, 222, W - 30, 130);
  a.models->box(WELL_BOX);
  a.models->textsize(SIZE_SMALL + 1);
  a.hint = new Fl_Box(15, 356, W - 30 - 140, 34);
  a.hint->labelsize(SIZE_SMALL);
  a.hint->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  a.drivers = new Fl_Button(W - 15 - 130, 358, 130, 24, _("Install Drivers…"));
  a.drivers->callback([](Fl_Widget *, void *) {
    drivers_dialog(g_add->suggested);
    g_add->all_models = printing::models();   // CUPS sees new drivers at once
    fill_models();
  });
  a.name = new Fl_Input(110, 398, 260, 22, _("Name:"));
  a.name->labelsize(SIZE_SMALL + 1);
  a.name->textsize(SIZE_SMALL + 1);
  auto *cancel = new Fl_Button(W - 196, H - 36, 86, 24, _("Cancel"));
  cancel->callback([](Fl_Widget *, void *) { g_add->done = true; });
  auto *add = new Fl_Button(W - 102, H - 36, 86, 24, _("Add"));
  add->labelfont(FONT_SYSTEM);
  add->callback([](Fl_Widget *, void *) {
    auto &a = *g_add;
    int m = a.models->value();
    std::string uri = a.uri->value(), name = safe_name(a.name->value());
    if (uri.empty()) { status(_("Choose a printer or type its address.")); return; }
    if (m <= 0 || m > (int)a.shown_models.size()) { status(_("Choose a driver.")); return; }
    status(_("Adding the printer…"));
    std::string out;
    int rc = run_helper({"add", name, uri, a.shown_models[m - 1].name}, &out);
    if (rc == 0) {
      status(fmt(_("The printer %1 was added."), name));
      a.done = true;
      reload(name);
    } else {
      status(fmt(_("Adding failed: %1"), last_line(out)));
    }
  });
  for (auto *b : {search, a.drivers, cancel, add}) { b->box(BUTTON_BOX); b->down_box(BUTTON_DOWN); b->labelsize(SIZE_SMALL + 1); }
  a.win->end();
  a.win->finish();
  a.win->place(g_win->x() + 30, g_win->y() + 20);
  a.win->show();
  search_devices();
  while (!a.done && a.win->shown()) Fl::wait();
  a.win->hide();
  Fl::delete_widget(a.win);
  g_add = nullptr;
}

void refresh_tick(void *) {
  if (g_list && !g_add) {
    int v = g_list->value();
    g_st = printing::query();
    if (v > 0 && v <= (int)g_st.printers.size()) show_details();
  }
  Fl::repeat_timeout(5.0, refresh_tick);
}

Fl_Button *button(int X, int Y, int W, const char *t, Fl_Callback *cb) {
  auto *b = new Fl_Button(X, Y, W, 24, t);
  b->box(BUTTON_BOX);
  b->down_box(BUTTON_DOWN);
  b->labelsize(SIZE_SMALL + 1);
  b->callback(cb);
  return b;
}

}  // namespace

int panel_printers() {
  const int W = 620, H = 400;
  g_win = new FrameWindow(W, H, _("Printers"));
  g_win->xclass("nirvana-printers");
  g_st = printing::query();
  g_status = new Fl_Box(15, H - 62, W - 30, 20);
  g_status->labelsize(SIZE_SMALL);
  g_status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);

  if (!g_st.running) {
    // no printing system (or it does not run): offer to install / start it
    bool installed = !access("/usr/sbin/cupsd", X_OK) || !access("/usr/bin/lpstat", X_OK);
    auto *t = new Fl_Box(15, 20, W - 30, 120);
    t->label(installed ? _("The printing system (CUPS) is installed but not running.")
                       : _("The printing system (CUPS) is not installed. Without it nothing can be printed."));
    t->labelsize(SIZE_SYSTEM);
    t->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    button(15, 150, 230, installed ? _("Start the Printing System") : _("Install the Printing System"),
           [](Fl_Widget *, void *) {
             status(_("Installing the printing system…"));
             std::string out;
             int rc = run_helper({"install-cups"}, &out);
             status(rc == 0 ? std::string(_("Done. Open this panel again to set up a printer."))
                            : fmt(_("Failed: %1"), last_line(out)));
           });
    g_list = nullptr;
  } else {
    auto *h = new Fl_Box(15, 8, 220, 20, _("Printers"));
    h->labelfont(FONT_SYSTEM);
    h->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    g_list = new Fl_Hold_Browser(15, 30, 230, H - 140);
    g_list->box(WELL_BOX);
    g_list->textsize(SIZE_SMALL + 1);
    g_list->callback([](Fl_Widget *, void *) { show_details(); });
    g_details = new Fl_Box(260, 30, W - 275, 150);
    g_details->labelsize(SIZE_SMALL + 1);
    g_details->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
    int bx = 260, by = 190;
    g_default = button(bx, by, 170, _("Set as Default"), [](Fl_Widget *, void *) {
      if (auto *p = selected()) {
        std::string n = p->name;
        status(printing::set_user_default(n) ? fmt(_("%1 is now your default printer."), n) : _("Could not change it."));
        reload(n);
      }
    });
    g_test = button(bx + 180, by, 170, _("Print Test Page"), [](Fl_Widget *, void *) {
      if (auto *p = selected()) {
        std::string err;
        status(printing::print_test_page(p->name, &err) ? fmt(_("A test page was sent to %1."), p->name) : err);
      }
    });
    g_pause = button(bx, by + 30, 170, _("Pause"), [](Fl_Widget *, void *) {
      if (auto *p = selected()) {
        std::string n = p->name, out;
        int rc = run_helper({p->state == 5 ? "enable" : "disable", n}, &out);
        if (rc) status(last_line(out));
        reload(n);
      }
    });
    g_remove = button(bx + 180, by + 30, 170, _("Remove"), [](Fl_Widget *, void *) {
      if (auto *p = selected()) {
        std::string n = p->name, out;
        int rc = run_helper({"delete", n}, &out);
        status(rc == 0 ? fmt(_("The printer %1 was removed."), n) : last_line(out));
        reload();
      }
    });
    button(15, H - 100, 150, _("Add Printer…"), [](Fl_Widget *, void *) { add_dialog(); });
  }
  button(175, H - 100, 150, _("Install Drivers…"), [](Fl_Widget *, void *) { drivers_dialog({}); });
  auto *ok = button(W - 102, H - 36, 86, _("OK"), [](Fl_Widget *w, void *) { w->window()->hide(); });
  ok->labelfont(FONT_SYSTEM);
  ok->shortcut(FL_Enter);
  g_win->end();
  g_win->finish();
  if (g_list) reload();
  Fl::add_timeout(5.0, refresh_tick);   // the state changes while printing
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + g_win->oy(), (sh - H) / 4));
  g_win->show();
  return Fl::run();
}

#else   // no libcups

#include <cstdio>
int panel_printers() {
  fprintf(stderr, "nirvana-alice: built without CUPS (libcups) - no Printers panel\n");
  return 1;
}

#endif
