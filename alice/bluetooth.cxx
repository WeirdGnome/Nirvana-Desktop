// Nirvana Desktop - alice: the "Bluetooth" panel
//
// Turning Bluetooth on and off, visibility for other devices, the list of devices
// (paired and found nearby), searching, pairing (with the code check in a dialog),
// connecting, disconnecting and forgetting. Works through bluetoothctl (libnirvana-bt):
// an interactive session with its own agent is open while the panel is.
// "nirvana-alice bluetooth-add" starts searching at once (the applet's "Add a Device…").
#include "bt.h"
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace plat;

namespace {

FrameWindow *g_win;
bt::State g_st;
std::unique_ptr<bt::Session> g_ses;
Fl_Check_Button *g_power, *g_visible;
Fl_Hold_Browser *g_list;
Fl_Button *g_search, *g_pair, *g_conn, *g_forget;
Fl_Box *g_status, *g_name;
std::vector<std::string> g_rows;   // MAC of each list row
std::string g_status_text, g_name_text;
std::string g_pairing;             // the device being paired
bool g_searching = false, g_busy = false;
std::string g_note;                // the result of the last pairing - stays until the next action

void refresh(void * = nullptr);

void status(const std::string &t) {
  g_status_text = t;
  g_status->label(g_status_text.c_str());
  g_status->redraw();
}

const bt::Device *selected() {
  int v = g_list->value();
  if (v < 1 || v > (int)g_rows.size()) return nullptr;
  for (auto &d : g_st.devices) if (d.mac == g_rows[v - 1]) return &d;
  return nullptr;
}

std::string clean(const std::string &s) {   // browser format characters
  std::string o;
  for (char c : s) o += c == '\t' ? ' ' : c;
  if (!o.empty() && o[0] == '@') o = " " + o;
  return o;
}

void sync_buttons() {
  const bt::Device *d = selected();
  bool on = g_st.available && g_st.powered && !g_busy;
  g_search->label(g_searching ? _("Stop") : _("Search"));
  if (on) g_search->activate(); else g_search->deactivate();
  if (on && d && !d->paired) g_pair->activate(); else g_pair->deactivate();
  if (on && d && d->paired) g_conn->activate(); else g_conn->deactivate();
  g_conn->label(d && d->connected ? _("Disconnect") : _("Connect"));
  if (g_st.available && d && !g_busy) g_forget->activate(); else g_forget->deactivate();
  if (g_st.available && !g_busy) { g_power->activate(); } else g_power->deactivate();
  if (g_st.available && g_st.powered && !g_busy) g_visible->activate(); else g_visible->deactivate();
}

void fill() {
  std::string keep;
  if (const bt::Device *d = selected()) keep = d->mac;
  g_list->clear();
  g_rows.clear();
  for (auto &d : g_st.devices) {
    std::string state;
    if (d.connected) state = d.battery >= 0 ? fmt(_("Connected, battery %1%"), std::to_string(d.battery)) : _("Connected");
    else if (d.paired) state = _("Paired");
    else if (d.rssi) state = d.rssi > -60 ? _("Nearby, strong signal") : d.rssi > -80 ? _("Nearby") : _("Nearby, weak signal");
    else state = _("Found");
    std::string row = (d.connected ? "@b" : "") + clean(d.name) + "\t" + bt::kind_name(d.icon) + "\t" + state;
    g_list->add(row.c_str());
    g_rows.push_back(d.mac);
    if (d.mac == keep) g_list->value((int)g_rows.size());
  }
  if (g_st.devices.empty())
    g_list->add(g_st.powered ? _("@iNo devices yet - press Search and make the device visible (pairing mode).")
                             : _("@iBluetooth is off."));
}

void refresh(void *) {
  g_st = bt::query();
  if (!g_st.available) {
    g_power->value(0);
    status(g_st.problem);
  } else {
    g_power->value(g_st.powered);
    g_visible->value(g_st.discoverable);
    if (!g_busy && g_pairing.empty() && !g_note.empty()) status(g_note);
    else if (!g_busy && g_pairing.empty())
      status(g_searching ? _("Searching for devices… Put the device into pairing mode.")
                         : g_st.powered ? "" : _("Bluetooth is off."));
  }
  g_name_text = g_st.adapter.empty() ? "" : fmt(_("Other devices see this computer as “%1”."), g_st.adapter);
  g_name->label(g_name_text.c_str());
  fill();
  sync_buttons();
}

void later() { Fl::remove_timeout(refresh); Fl::add_timeout(0.5, refresh); }

void done(bool ok, const std::string &msg) {
  g_busy = false;
  if (!ok && !msg.empty()) fl_alert("%s", msg.c_str());
  refresh();
}

void set_search(bool on) {
  if (!g_ses) return;
  g_note.clear();
  g_searching = on;
  g_ses->scan(on);
  later();
  sync_buttons();
}

void ask(bt::Session::Ask a, const std::string &text) {
  using A = bt::Session::Ask;
  std::string dev = g_pairing;
  for (auto &d : g_st.devices) if (d.mac == g_pairing) dev = d.name;
  switch (a) {
  case A::Confirm: {
    int r = fl_choice(_("Does “%s” show the code %s?\n\nConfirm only when the codes are the same."),
                      _("No"), _("Yes, Pair"), nullptr, dev.c_str(), text.c_str());
    g_ses->answer(r == 1 ? "yes" : "no");
    break;
  }
  case A::Pin:
  case A::Passkey: {
    const char *v = fl_input(a == A::Pin ? _("Type the PIN for “%s” (often 0000 or 1234 - see its manual):")
                                         : _("Type the code shown on “%s”:"), "", dev.c_str());
    g_ses->answer(v ? v : "");
    break;
  }
  case A::Show:
    status(fmt(_("Type %1 on “%2” and press Enter there."), text, dev));
    break;
  case A::Authorize: {
    int r = fl_choice(_("“%s” asks for permission to connect. Allow it?"), _("Deny"), _("Allow"), nullptr, dev.c_str());
    g_ses->answer(r == 1 ? "yes" : "no");
    break;
  }
  }
}

Fl_Button *button(int X, int Y, int W, const char *t) {
  auto *b = new Fl_Button(X, Y, W, 24, t);
  b->box(BUTTON_BOX);
  b->down_box(BUTTON_DOWN);
  b->labelsize(SIZE_SMALL + 1);
  return b;
}

}  // namespace

int panel_bluetooth(bool add) {
  const int W = 600, H = 440;
  g_win = new FrameWindow(W, H, _("Bluetooth"));
  g_win->xclass("nirvana-bluetooth");

  g_power = new Fl_Check_Button(15, 12, 250, 22, _("Bluetooth on"));
  g_power->labelfont(FONT_SYSTEM);
  g_power->callback([](Fl_Widget *w, void *) {
    bool on = ((Fl_Check_Button *)w)->value();
    g_busy = true;
    status(on ? _("Turning Bluetooth on…") : _("Turning Bluetooth off…"));
    sync_buttons();
    if (!on) g_searching = false;
    bt::set_power(on, done);
  });
  g_visible = new Fl_Check_Button(280, 12, W - 295, 22, _("Visible to other devices"));
  g_visible->labelsize(SIZE_SMALL + 1);
  g_visible->callback([](Fl_Widget *w, void *) {
    g_busy = true;
    bt::set_discoverable(((Fl_Check_Button *)w)->value(), done);
  });
  g_name = new Fl_Box(15, 36, W - 30, 18);
  g_name->labelsize(SIZE_SMALL);
  g_name->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  g_list = new Fl_Hold_Browser(15, 60, W - 30, 250);
  g_list->box(WELL_BOX);
  g_list->textfont(FONT_SMALL);
  g_list->textsize(SIZE_SMALL + 1);
  static int cols[] = {225, 165, 0};
  g_list->column_widths(cols);
  g_list->column_char('\t');
  g_list->callback([](Fl_Widget *, void *) {
    sync_buttons();
    // a double click: pair a new device, connect/disconnect a paired one
    if (Fl::event_clicks()) {
      Fl::event_clicks(0);
      if (g_pair->active()) g_pair->do_callback();
      else if (g_conn->active()) g_conn->do_callback();
    }
  });

  int by = 318;
  g_search = button(15, by, 100, _("Search"));
  g_search->callback([](Fl_Widget *, void *) { set_search(!g_searching); });
  g_pair = button(121, by, 100, _("Pair"));
  g_pair->callback([](Fl_Widget *, void *) {
    const bt::Device *d = selected();
    if (!d || !g_ses) return;
    g_pairing = d->mac;
    g_note.clear();
    g_busy = true;
    status(fmt(_("Pairing with “%1”… Follow the instructions on the device."), d->name));
    sync_buttons();
    g_ses->pair(d->mac);
  });
  g_conn = button(227, by, 110, _("Connect"));
  g_conn->callback([](Fl_Widget *, void *) {
    const bt::Device *d = selected();
    if (!d) return;
    g_note.clear();
    g_busy = true;
    status(fmt(d->connected ? _("Disconnecting “%1”…") : _("Connecting “%1”…"), d->name));
    sync_buttons();
    if (d->connected) bt::disconnect(d->mac, done);
    else bt::connect(d->mac, done);
  });
  g_forget = button(W - 115, by, 100, _("Forget"));
  g_forget->callback([](Fl_Widget *, void *) {
    const bt::Device *d = selected();
    if (!d) return;
    if (d->paired && fl_choice(_("Forget “%s”?\nTo use it again it has to be paired anew."), _("Cancel"), _("Forget"),
                               nullptr, d->name.c_str()) != 1)
      return;
    g_busy = true;
    bt::remove(d->mac, done);
  });

  g_status = new Fl_Box(15, 350, W - 30, 40);
  g_status->labelsize(SIZE_SMALL + 1);
  g_status->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  auto *ok = button(W - 102, H - 36, 86, _("OK"));
  ok->labelfont(FONT_SYSTEM);
  ok->shortcut(FL_Enter);
  ok->callback([](Fl_Widget *, void *) { g_win->hide(); });
  g_win->end();
  g_win->finish();
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + g_win->oy(), (sh - H) / 4));
  g_win->show();

  if (bt::installed()) {
    g_ses.reset(new bt::Session);
    g_ses->on_change = [] { later(); };
    g_ses->on_ask = ask;
    g_ses->on_paired = [](bool ok, const std::string &msg) {
      std::string name = g_pairing;
      for (auto &d : g_st.devices) if (d.mac == g_pairing) name = d.name;
      g_pairing.clear();
      g_busy = false;
      refresh();
      if (ok) { g_note = msg.empty() ? fmt(_("“%1” is paired and connected."), name) : msg; status(g_note); }
      else { status(""); fl_alert("%s", msg.c_str()); }
    };
  }
  refresh();
  if (add && g_st.available) {
    if (!g_st.powered) { bt::set_power(true, [](bool ok, const std::string &m) { done(ok, m); set_search(true); }); g_busy = true; }
    else set_search(true);
  }
  int r = Fl::run();
  g_ses.reset();   // quits bluetoothctl (and the search)
  return r;
}
