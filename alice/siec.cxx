// Nirvana Desktop - alice: panel "Sieć"
//
// Lista połączeń (Ethernet, Wi-Fi, modem...) po lewej, szczegóły wybranego po
// prawej: adresy, Połącz/Rozłącz, ustawienia IPv4, a dla Wi-Fi lista sieci
// w zasięgu z łączeniem, ukrytymi sieciami i zapominaniem. Wszystko przez
// NetworkManagera (nirvana-net); zmiany z zewnątrz widać na bieżąco.
#include "net.h"
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/fl_draw.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace plat;

namespace {

net::State g_st;
std::string g_iface;                 // wybrane urządzenie
std::string g_conn;                  // jego aktywny profil
FrameWindow *g_win;
Fl_Hold_Browser *g_devs, *g_nets;
Fl_Box *g_title, *g_status, *g_msg, *g_hint;
Fl_Button *g_connect, *g_ipv4;
Fl_Group *g_info, *g_wifi;
Fl_Output *g_out[6];
Fl_Check_Button *g_wifi_on;
std::vector<net::Network> g_list_nets;   // sieci w kolejności listy
std::vector<net::Vpn> g_list_vpns;
Fl_Group *g_vpn;
Fl_Hold_Browser *g_vpns;
Fl_Button *g_vpn_toggle;
Fl_Box *g_vpn_note;
Fl_Check_Button *g_air;
const char *const VPN_ID = "#vpn";       // pseudo-urządzenie "VPN" na liście połączeń

void message(const std::string &m, bool error = false) {
  g_msg->copy_label(m.c_str());
  g_msg->labelcolor(error ? ROOT_TINT : TEXT);
  g_msg->redraw_label();
  g_win->redraw();
}

// --- lista urządzeń: ikona, nazwa, stan w drugiej linii -------------------------
class DevList : public Fl_Hold_Browser {
public:
  DevList(int X, int Y, int W, int H) : Fl_Hold_Browser(X, Y, W, H) {}
protected:
  int item_height(void *) const override { return 36; }
  int item_width(void *) const override { return w() - 20; }
  void item_draw(void *item, int X, int Y, int W, int H) const override {
    // tekst pozycji: typ \t interfejs \t stan \t profil \t 1=połączone
    std::string t = item_text(item);
    std::vector<std::string> f;
    size_t a = 0;
    while (true) {
      size_t b = t.find('\t', a);
      f.push_back(t.substr(a, b == std::string::npos ? std::string::npos : b - a));
      if (b == std::string::npos) break;
      a = b + 1;
    }
    while (f.size() < 5) f.emplace_back();
    bool sel = item_selected(item);
    Fl_Color fg = sel ? SEL_TEXT : CONTENT_TEXT;
    if (sel) { fl_color(SEL); fl_rectf(X - 2, Y, W + 4, H); }
    Fl_Color off = fl_color_average(fg, sel ? SEL : CONTENT, 0.35f);
    bool up = f[4] == "1";
    if (f[0] == "wifi") draw_wifi(X + 6, Y + 11, up ? 4 : 0, fg, off);
    else if (f[0] == "vpn") { draw_lock(X + 10, Y + 9, up ? fg : off); draw_lock(X + 10, Y + 9, up ? fg : off); }
    else draw_wired(X + 6, Y + 11, up ? fg : off);
    fl_color(fg);
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    std::string name = f[0] == "vpn" ? std::string("VPN") : std::string(net::type_name(f[0])) + " (" + f[1] + ")";
    fl_draw(ellipsize(name, W - 34).c_str(), X + 28, Y + 15);
    fl_font(FONT_SMALL, SIZE_SMALL);
    std::string st = f[2];
    if (up && !f[3].empty()) st += ": " + f[3];
    fl_color(sel ? fg : off == fg ? fg : fl_color_average(fg, CONTENT, 0.7f));
    fl_draw(ellipsize(st, W - 34).c_str(), X + 28, Y + 30);
  }
};

// --- lista sieci Wi-Fi: znacznik, nazwa, "zapisana", kłódka, sygnał -------------
class NetList : public Fl_Hold_Browser {
public:
  NetList(int X, int Y, int W, int H) : Fl_Hold_Browser(X, Y, W, H) {}
protected:
  int item_height(void *) const override { return 20; }
  int item_width(void *) const override { return w() - 20; }
  void item_draw(void *item, int X, int Y, int W, int H) const override {
    const char *v = item_text(item);   // [w użyciu][kreski][kłódka][zapisana]SSID
    if (!v || strlen(v) < 4) return;
    bool sel = item_selected(item);
    Fl_Color fg = sel ? SEL_TEXT : CONTENT_TEXT;
    if (sel) { fl_color(SEL); fl_rectf(X - 2, Y, W + 4, H); }
    fl_color(fg);
    if (v[0] == '1') {   // ✓
      fl_line_style(FL_SOLID, 2);
      fl_line(X + 3, Y + 10, X + 6, Y + 14, X + 12, Y + 5);
      fl_line_style(0);
    }
    fl_font(v[0] == '1' ? FONT_SYSTEM : FONT_SMALL, SIZE_SYSTEM);
    int tx = X + 18;
    std::string name = ellipsize(v + 4, W - 130);
    fl_draw(name.c_str(), tx, Y + H - 5);
    if (v[3] == '1') {
      int nx = tx + (int)fl_width(name.c_str()) + 8;
      fl_font(FONT_SMALL, SIZE_SMALL - 1);
      fl_color(sel ? fg : fl_color_average(fg, CONTENT, 0.6f));
      fl_draw("zapisana", nx, Y + H - 6);
    }
    if (v[2] == '1') draw_lock(X + W - 40, Y + 5, fg);
    if (v[1] != 'x')   // 'x': wiersz informacyjny, bez kresek sygnału
      draw_signal(X + W - 24, Y + 5, v[1] - '0', fg, fl_color_average(fg, sel ? SEL : CONTENT, 0.3f));
  }
};

// --- lista profili VPN: nazwa, rodzaj, stan ------------------------------------
class VpnList : public Fl_Hold_Browser {
public:
  VpnList(int X, int Y, int W, int H) : Fl_Hold_Browser(X, Y, W, H) {}
protected:
  int item_height(void *) const override { return 34; }
  int item_width(void *) const override { return w() - 20; }
  void item_draw(void *item, int X, int Y, int W, int H) const override {
    std::string t = item_text(item);   // stan(A/C/-) rodzaj \t nazwa
    if (t.size() < 2) return;
    char st = t[0];
    auto tab = t.find('\t');
    std::string kind = t.substr(1, tab - 1), name = tab == std::string::npos ? "" : t.substr(tab + 1);
    bool sel = item_selected(item);
    Fl_Color fg = sel ? SEL_TEXT : CONTENT_TEXT, dim = sel ? fg : fl_color_average(fg, CONTENT, 0.65f);
    if (sel) { fl_color(SEL); fl_rectf(X - 2, Y, W + 4, H); }
    draw_lock(X + 6, Y + 8, st == 'A' ? fg : dim);
    fl_color(fg);
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    fl_draw(ellipsize(name, W - 130).c_str(), X + 22, Y + 15);
    fl_font(FONT_SMALL, SIZE_SMALL);
    fl_color(dim);
    fl_draw(net::vpn_kind_name(kind), X + 22, Y + 29);
    const char *state = st == 'A' ? _("connected") : st == 'C' ? _("connecting…") : "";
    fl_color(st == 'A' ? fg : dim);
    fl_draw(state, X + W - 8 - (int)fl_width(state), Y + 15);
  }
};

Fl_Button *button(int X, int Y, int W, const char *l) {
  auto *b = new Fl_Button(X, Y, W, 24, l);
  b->box(BUTTON_BOX);
  b->down_box(BUTTON_DOWN);
  b->labelsize(SIZE_SMALL + 1);
  return b;
}

// --- okienka modalne -------------------------------------------------------------
// Pętla modalna: zwraca true po "Połącz"/"Zapisz", false po "Anuluj".
bool run_modal(FrameWindow *w, Fl_Button *ok, Fl_Button *cancel) {
  int result = -1;
  ok->callback([](Fl_Widget *, void *r) { *(int *)r = 1; }, &result);
  cancel->callback([](Fl_Widget *, void *r) { *(int *)r = 0; }, &result);
  w->callback([](Fl_Widget *, void *r) { *(int *)r = 0; }, &result);
  ok->shortcut(FL_Enter);
  w->set_modal();
  w->position(g_win->x() + (g_win->w() - w->w()) / 2, g_win->y() + 60);   // całe okna, z ramkami
  w->show();
  while (result < 0 && w->shown()) Fl::wait();
  w->hide();
  return result == 1;
}

struct WifiAnswer { std::string ssid, security, password; };

// Hasło do sieci; hidden=true dodaje pola nazwy i rodzaju zabezpieczeń.
bool ask_wifi(bool hidden, const std::string &ssid, const std::string &err, WifiAnswer &out) {
  int W = 420, H = hidden ? 250 : 180;
  auto *w = new FrameWindow(W, H, hidden ? _("Other Wi-Fi Network") : _("Wi-Fi Network"), true);
  struct Pic : Fl_Widget {
    Pic(int X, int Y) : Fl_Widget(X, Y, 34, 34) {}
    void draw() override { draw_icon(Icon::Wifi, x(), y(), false); }
  };
  new Pic(16, 16);
  std::string text = hidden ? std::string(_("Connect to a network that does not broadcast its name."))
                            : fmt(_("The network “%1” is secured.\nEnter the password to connect."), ssid);
  auto *t = new Fl_Box(64, 12, W - 80, 36);
  t->copy_label(text.c_str());
  t->labelfont(FONT_SYSTEM);
  t->labelsize(SIZE_SYSTEM);
  t->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  int y = 54;
  if (!err.empty()) {
    auto *e = new Fl_Box(64, y, W - 80, 18);
    e->copy_label(err.c_str());
    e->labelcolor(ROOT_TINT);
    e->labelsize(SIZE_SMALL);
    e->labelfont(FONT_SYSTEM);
    e->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 20;
  }
  Fl_Input *name = nullptr;
  Fl_Choice *sec = nullptr;
  if (hidden) {
    name = new Fl_Input(170, y, W - 186, 24, _("Network name:"));
    name->box(WELL_BOX);
    name->value(ssid.c_str());
    y += 32;
    sec = new Fl_Choice(170, y, W - 186, 24, _("Security:"));
    sec->add(_("WPA\\/WPA2 Personal|WPA3 Personal|None"));
    sec->value(0);
    y += 32;
  }
  auto *pass = new Fl_Secret_Input(170, y, W - 186, 24, _("Password:"));
  pass->box(WELL_BOX);
  y += 28;
  auto *show = new Fl_Check_Button(166, y, 160, 20, _("Show password"));
  show->labelsize(SIZE_SMALL);
  show->callback([](Fl_Widget *b, void *p) {
    auto *in = (Fl_Input *)p;
    in->input_type(((Fl_Check_Button *)b)->value() ? FL_NORMAL_INPUT : FL_SECRET_INPUT);
    in->redraw();
  }, pass);
  if (sec)
    sec->callback([](Fl_Widget *c, void *p) {
      auto *in = (Fl_Input *)p;
      if (((Fl_Choice *)c)->value() == 2) in->deactivate(); else in->activate();
    }, pass);
  auto *cancel = button(W - 196, H - 38, 86, _("Cancel"));
  auto *ok = button(W - 102, H - 38, 86, _("Connect"));
  ok->labelfont(FONT_SYSTEM);
  w->end();
  w->finish();
  (name ? (Fl_Widget *)name : pass)->take_focus();
  bool r = run_modal(w, ok, cancel);
  if (r) {
    out.ssid = name ? name->value() : ssid;
    out.password = pass->value();
    if (sec) out.security = sec->value() == 0 ? "WPA2" : sec->value() == 1 ? "WPA3" : "";
    if (sec && sec->value() == 2) out.password.clear();
  }
  pass->value("");   // hasło nie wisi w widżecie
  Fl::delete_widget(w);
  return r && !out.ssid.empty();
}

// --- akcje ------------------------------------------------------------------------
void refresh();

net::Done report(const std::string &what) {
  return [what](bool ok, const std::string &m) {
    message(ok ? (m.empty() ? what : m) : m, !ok);
    refresh();
  };
}

void connect_network(const net::Network &n, const std::string &err) {
  if (n.enterprise()) {
    message(_("Enterprise networks (802.1X) are not supported yet - use nmtui."), true);
    return;
  }
  auto done = [n](bool ok, const std::string &m) {
    if (!ok && (m == net::NEED_PASSWORD || m == _("Wrong password.") ||
                m == _("Could not connect. Check the password."))) {
      connect_network(n, m == net::NEED_PASSWORD ? "" : m);
      return;
    }
    message(ok ? fmt(_("Connected to “%1”."), n.ssid) : m, !ok);
    refresh();
  };
  if (err.empty() && (!n.secured() || n.known)) {
    message(fmt(_("Connecting to “%1”…"), n.ssid));
    net::connect_wifi(n.ssid, n.security, "", false, done);
    return;
  }
  WifiAnswer a;
  if (!ask_wifi(false, n.ssid, err, a)) return;
  message(fmt(_("Connecting to “%1”…"), n.ssid));
  net::connect_wifi(n.ssid, n.security, a.password, false, done);
  std::fill(a.password.begin(), a.password.end(), '\0');
}

void connect_hidden() {
  WifiAnswer a;
  if (!ask_wifi(true, "", "", a)) return;
  message(fmt(_("Connecting to “%1”…"), a.ssid));
  std::string ssid = a.ssid;
  net::connect_wifi(a.ssid, a.security, a.password, true, [ssid](bool ok, const std::string &m) {
    message(ok ? fmt(_("Connected to “%1”."), ssid) : m, !ok);
    refresh();
  });
  std::fill(a.password.begin(), a.password.end(), '\0');
}

void ipv4_dialog() {
  if (g_conn.empty()) return;
  net::Details d = net::details(g_iface);
  int W = 400, H = 220;
  auto *w = new FrameWindow(W, H, _("IPv4 Settings"), true);
  std::string head = fmt(_("Connection “%1”"), g_conn);
  auto *t = new Fl_Box(16, 10, W - 32, 22);
  t->copy_label(head.c_str());
  t->labelfont(FONT_SYSTEM);
  t->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  auto *how = new Fl_Choice(150, 40, W - 166, 24, _("Configure:"));
  how->add(_("Automatically (DHCP)|Manually"));
  how->value(d.method == "manual" ? 1 : 0);
  auto *addr = new Fl_Input(150, 72, W - 166, 24, _("Address/prefix:"));
  auto *gw = new Fl_Input(150, 102, W - 166, 24, _("Gateway:"));
  auto *dns = new Fl_Input(150, 132, W - 166, 24, _("DNS servers:"));
  std::string a0 = d.ip4.substr(0, d.ip4.find(','));
  addr->value(a0.c_str());
  gw->value(d.gateway.c_str());
  dns->value(d.dns.c_str());
  for (Fl_Input *i : {addr, gw, dns}) i->box(WELL_BOX);
  auto sync = [](Fl_Widget *c, void *v) {
    auto **ins = (Fl_Input **)v;
    for (int i = 0; i < 3; i++) {
      if (((Fl_Choice *)c)->value() == 1) ins[i]->activate(); else ins[i]->deactivate();
    }
  };
  static Fl_Input *ins[3];
  ins[0] = addr; ins[1] = gw; ins[2] = dns;
  how->callback(sync, ins);
  sync(how, ins);
  auto *cancel = button(W - 196, H - 38, 86, _("Cancel"));
  auto *ok = button(W - 102, H - 38, 86, _("Save"));
  ok->labelfont(FONT_SYSTEM);
  w->end();
  w->finish();
  if (run_modal(w, ok, cancel)) {
    bool manual = how->value() == 1;
    std::string dn = dns->value();
    std::replace(dn.begin(), dn.end(), ' ', ',');
    if (manual && !*addr->value()) message(_("Enter an IPv4 address, e.g. 192.168.1.20/24."), true);
    else {
      message(_("Saving IPv4 settings…"));
      net::set_ipv4(g_conn, manual, addr->value(), gw->value(), dn, report(_("IPv4 settings saved.")));
    }
  }
  Fl::delete_widget(w);
}

// --- VPN ------------------------------------------------------------------------
// Nazwa użytkownika i hasło do VPN (OpenVPN itp.). Hasło nie jest zapisywane.
bool ask_vpn(const net::Vpn &v, const std::string &err, std::string &user, std::string &pass) {
  int W = 420, H = err.empty() ? 196 : 216;
  auto *w = new FrameWindow(W, H, "VPN", true);
  struct Pic : Fl_Widget {
    Pic(int X, int Y) : Fl_Widget(X, Y, 34, 34) {}
    void draw() override { draw_icon(Icon::Encrypted, x(), y(), false); }
  };
  new Pic(16, 16);
  std::string text = fmt(_("The VPN connection “%1” requires a login."), v.name);
  auto *t = new Fl_Box(64, 12, W - 80, 36);
  t->copy_label(text.c_str());
  t->labelfont(FONT_SYSTEM);
  t->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  int y = 54;
  if (!err.empty()) {
    auto *e = new Fl_Box(64, y, W - 80, 18);
    e->copy_label(err.c_str());
    e->labelcolor(ROOT_TINT);
    e->labelsize(SIZE_SMALL);
    e->labelfont(FONT_SYSTEM);
    e->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y += 20;
  }
  auto *u = new Fl_Input(170, y, W - 186, 24, _("User name:"));
  u->box(WELL_BOX);
  u->value(user.c_str());
  auto *p = new Fl_Secret_Input(170, y + 32, W - 186, 24, _("Password:"));
  p->box(WELL_BOX);
  auto *cancel = button(W - 196, H - 38, 86, _("Cancel"));
  auto *ok = button(W - 102, H - 38, 86, _("Connect"));
  ok->labelfont(FONT_SYSTEM);
  w->end();
  w->finish();
  (user.empty() ? (Fl_Widget *)u : p)->take_focus();
  bool r = run_modal(w, ok, cancel);
  if (r) { user = u->value(); pass = p->value(); }
  p->value("");
  Fl::delete_widget(w);
  return r;
}

void vpn_connect(const net::Vpn &v, const std::string &err) {
  std::string user, pass;
  if (!err.empty()) {
    user = net::vpn_username(v.uuid);
    if (!ask_vpn(v, err == net::NEED_PASSWORD ? "" : err, user, pass)) return;
  }
  message(fmt(_("Connecting to VPN “%1”…"), v.name));
  net::vpn_up(v.uuid, user, pass, [v](bool ok, const std::string &m) {
    if (!ok && (m == net::NEED_PASSWORD || m == _("Wrong password."))) {
      vpn_connect(v, m);
      return;
    }
    message(ok ? fmt(_("Connected to VPN “%1”."), v.name) : m, !ok);
    refresh();
  });
  std::fill(pass.begin(), pass.end(), '\0');
}

const net::Vpn *selected_vpn() {
  int i = g_vpns->value();
  return i > 0 && i <= (int)g_list_vpns.size() ? &g_list_vpns[i - 1] : nullptr;
}

void vpn_buttons() {
  const net::Vpn *v = selected_vpn();
  g_vpn_toggle->label(v && (v->active || v->activating) ? _("Disconnect") : _("Connect"));
  g_vpn_toggle->redraw();
}

void show_vpn() {
  static std::string title, status, note;
  g_title->label("VPN");
  const net::Vpn *act = nullptr;
  for (auto &v : g_st.vpns) if (v.active) act = &v;
  status = act ? fmt(_("Connected to “%1” (%2)."), act->name, net::vpn_kind_name(act->kind))
               : g_st.vpns.empty() ? _("You have no VPN connections yet. Import a file from your administrator or VPN provider.")
                                   : _("No active VPN connection.");
  g_status->label(status.c_str());
  std::string keep;
  if (const net::Vpn *v = selected_vpn()) keep = v->uuid;
  g_vpns->clear();
  g_list_vpns = g_st.vpns;
  for (auto &v : g_list_vpns) {
    std::string line(1, v.active ? 'A' : v.activating ? 'C' : '-');
    line += v.kind + "\t" + v.name;
    g_vpns->add(line.c_str());
    if (v.uuid == keep) g_vpns->value(g_vpns->size());
  }
  if (!g_vpns->value() && !g_list_vpns.empty()) g_vpns->value(1);
  std::string kinds = "WireGuard";
  for (auto &k : net::vpn_plugins()) kinds += std::string(", ") + net::vpn_kind_name(k);
  note = fmt(_("Supported: %1. Other types need a NetworkManager plugin (network-manager-… packages)."), kinds);
  g_vpn_note->label(note.c_str());
  vpn_buttons();
  g_vpn->show();
  g_win->redraw();
}

void vpn_import_dialog() {
  polish_file_chooser();
  const char *f = fl_file_chooser(_("Import VPN Configuration"), "Konfiguracje VPN (*.{conf,ovpn,pcf})\tWszystkie pliki (*)",
                                  getenv("HOME"), 0);
  if (!f) return;
  std::string file = f;
  message(_("Importing…"));
  net::vpn_import(file, [](bool ok, const std::string &m) {
    message(ok ? fmt(_("Added VPN connection “%1”."), m) : m, !ok);
    refresh();
  });
}

// Hotspot: udostępnianie połączenia przez Wi-Fi.
void hotspot_dialog() {
  int W = 420, H = 200;
  auto *w = new FrameWindow(W, H, _("Hotspot"), true);
  struct Pic : Fl_Widget {
    Pic(int X, int Y) : Fl_Widget(X, Y, 34, 34) {}
    void draw() override { draw_icon(Icon::Wifi, x(), y(), false); }
  };
  new Pic(16, 16);
  auto *t = new Fl_Box(64, 12, W - 80, 36, _("Share your internet connection with other devices over Wi-Fi."));
  t->labelfont(FONT_SYSTEM);
  t->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  auto *n = new Fl_Input(170, 60, W - 186, 24, _("Network name:"));
  n->box(WELL_BOX);
  char host[64] = "nirvana";
  gethostname(host, sizeof host - 1);
  std::string def = std::string("Nirvana-") + host;
  n->value(def.c_str());
  auto *p = new Fl_Input(170, 92, W - 186, 24, _("Password (min. 8):"));
  p->box(WELL_BOX);
  auto *cancel = button(W - 196, H - 38, 86, _("Cancel"));
  auto *ok = button(W - 102, H - 38, 86, _("Turn On"));
  ok->labelfont(FONT_SYSTEM);
  w->end();
  w->finish();
  p->take_focus();
  if (run_modal(w, ok, cancel)) {
    message(_("Turning on hotspot…"));
    std::string ssid = n->value(), pw = p->value();
    net::hotspot(ssid, pw, report(_("Hotspot is on.")));
    std::fill(pw.begin(), pw.end(), '\0');
  }
  p->value("");
  Fl::delete_widget(w);
}

// --- odświeżanie ----------------------------------------------------------------
const net::Device *selected_device() {
  for (auto &d : g_st.devices) if (d.iface == g_iface) return &d;
  return nullptr;
}

void show_device() {
  const net::Device *d = selected_device();
  g_vpn->hide();
  g_air->value(g_st.airplane());
  g_wifi->hide();
  g_info->hide();
  g_connect->hide();
  g_ipv4->hide();
  g_hint->hide();
  if (!g_st.available) {
    g_title->label(_("Network unavailable"));
    static std::string p;
    p = g_st.problem + "\n" + _("Nirvana Desktop manages networking through NetworkManager: install the network-manager package and start the NetworkManager service.");
    g_status->label(p.c_str());
    g_win->redraw();
    return;
  }
  if (g_iface == VPN_ID) { show_vpn(); return; }
  if (!d) {
    g_title->label(_("No connections"));
    g_status->label(_("No network adapters managed by NetworkManager were found."));
    g_win->redraw();
    return;
  }
  static std::string title, status;
  title = std::string(net::type_name(d->type)) + " (" + d->iface + ")";
  g_title->label(title.c_str());
  g_conn = d->state == net::DevState::Connected || d->state == net::DevState::Connecting ? d->connection : "";
  switch (d->state) {
    case net::DevState::Connected:
      status = d->type == "wifi" ? (!g_st.hotspot_ssid.empty()
                                       ? fmt(_("Hotspot: sharing your connection as “%1”."), g_st.hotspot_ssid)
                                       : fmt(_("Connected to “%1”."), d->connection))
                                 : fmt(_("Connected (“%1”)."), d->connection);
      break;
    case net::DevState::Connecting: status = _("Connecting…"); break;
    case net::DevState::Unavailable:
      status = d->type == "wifi" ? (g_st.wifi_on ? _("Wi-Fi adapter unavailable.") : _("Wi-Fi is off."))
                                 : _("The network cable is unplugged.");
      break;
    default: status = _("Disconnected."); break;
  }
  g_status->label(status.c_str());

  bool up = d->state == net::DevState::Connected || d->state == net::DevState::Connecting;
  if (d->state != net::DevState::Unavailable) {
    g_connect->label(up ? _("Disconnect") : _("Connect"));
    g_connect->show();
  }
  if (!g_conn.empty()) g_ipv4->show();
  if (up) {
    net::Details det = net::details(d->iface);
    const std::string *vals[] = {&det.ip4, &det.gateway, &det.dns, &det.ip6, &det.hwaddr, &det.speed};
    for (int i = 0; i < 6; i++) g_out[i]->value(vals[i]->empty() ? "—" : vals[i]->c_str());
    g_info->show();
  } else if (d->type != "wifi") {
    g_hint->show();
  }

  if (d->type == "wifi") {
    g_wifi_on->value(g_st.wifi_on);
    // zachowaj zaznaczenie na liście sieci po odświeżeniu
    std::string keep;
    if (int v = g_nets->value(); v > 0 && v <= (int)g_list_nets.size()) keep = g_list_nets[v - 1].ssid;
    g_nets->clear();
    g_list_nets.clear();
    for (auto &n : g_st.networks) {
      if (n.device != d->iface && !n.device.empty()) continue;
      g_list_nets.push_back(n);
      std::string line;
      line += n.in_use ? '1' : '0';
      line += char('0' + net::bars(n.signal));
      line += n.secured() ? '1' : '0';
      line += n.known ? '1' : '0';
      line += n.ssid;
      g_nets->add(line.c_str());
      if (n.ssid == keep) g_nets->value(g_nets->size());
    }
    if (g_list_nets.empty()) g_nets->add((std::string("0x00") + (g_st.wifi_on ? _("Looking for networks…") : _("(Wi-Fi off)"))).c_str());
    g_wifi->show();
  }
  g_win->redraw();
}

void refresh() {
  g_st = net::query();
  g_devs->clear();
  int sel = 0;
  for (auto &d : g_st.devices) {
    bool up = d.state == net::DevState::Connected;
    std::string line = d.type + "\t" + d.iface + "\t" + net::state_name(d.state) + "\t" + d.connection + "\t" +
                       (up ? "1" : "0");
    g_devs->add(line.c_str());
    if (d.iface == g_iface) sel = g_devs->size();
  }
  if (g_st.available) {
    const net::Vpn *act = nullptr;
    for (auto &v : g_st.vpns) if (v.active) act = &v;
    std::string st = act ? _("connected") : g_st.vpns.empty() ? _("no connections") : _("disconnected");
    g_devs->add((std::string("vpn\t\t") + st + "\t" + (act ? act->name : "") + "\t" + (act ? "1" : "0")).c_str());
    if (g_iface == VPN_ID) sel = g_devs->size();
  }
  if (!sel && !g_st.devices.empty()) { sel = 1; g_iface = g_st.devices[0].iface; }
  if (sel) g_devs->value(sel);
  show_device();
}

}  // namespace

int panel_siec(bool hidden_dialog) {
  const int W = 640, H = 470;
  g_win = new FrameWindow(W, H, _("Network"));
  g_win->xclass("nirvana-siec");

  auto *t1 = new Fl_Box(15, 8, 200, 20, _("Connections"));
  t1->labelfont(FONT_SYSTEM);
  t1->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_devs = new DevList(15, 30, 200, H - 80);
  g_devs->box(WELL_BOX);
  g_devs->callback([](Fl_Widget *, void *) {
    int v = g_devs->value();
    if (v > 0 && v <= (int)g_st.devices.size()) { g_iface = g_st.devices[v - 1].iface; show_device(); }
    else if (v == (int)g_st.devices.size() + 1) { g_iface = VPN_ID; show_device(); }
  });

  const int RX = 232, RW = W - RX - 15;
  g_title = new Fl_Box(RX, 8, RW - 200, 24);
  g_title->labelfont(FONT_SYSTEM);
  g_title->labelsize(16);
  g_title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_status = new Fl_Box(RX, 34, RW, 34);
  g_status->labelsize(SIZE_SYSTEM);
  g_status->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  g_ipv4 = button(RX + RW - 190, 8, 90, _("IPv4…"));
  g_ipv4->callback([](Fl_Widget *, void *) { ipv4_dialog(); });
  g_connect = button(RX + RW - 94, 8, 94, _("Connect"));
  g_connect->labelfont(FONT_SYSTEM);
  g_connect->callback([](Fl_Widget *, void *) {
    const net::Device *d = selected_device();
    if (!d) return;
    bool up = d->state == net::DevState::Connected || d->state == net::DevState::Connecting;
    std::string iface = d->iface;
    if (up) { message(_("Disconnecting…")); net::disconnect_device(iface, report(_("Disconnected."))); }
    else if (d->type == "wifi" && g_nets->value() > 0 && g_nets->value() <= (int)g_list_nets.size())
      connect_network(g_list_nets[g_nets->value() - 1], "");
    else { message(_("Connecting…")); net::connect_device(iface, report(_("Connected."))); }
  });

  // szczegóły połączenia
  g_info = new Fl_Group(RX, 70, RW, 130);
  const char *labels[] = {_("IPv4 address:"), _("Gateway:"), _("DNS servers:"), _("IPv6 address:"), _("MAC address:"), _("Speed:")};
  for (int i = 0; i < 6; i++) {
    g_out[i] = new Fl_Output(RX + 110, 72 + i * 21, RW - 110, 20, labels[i]);
    g_out[i]->box(FL_FLAT_BOX);
    g_out[i]->color(GRAY);
    g_out[i]->labelsize(SIZE_SMALL + 1);
    g_out[i]->textsize(SIZE_SMALL + 1);
  }
  g_info->end();
  g_hint = new Fl_Box(RX, 76, RW, 60,
                      _("Plug in the cable or click “Connect”. The address is obtained automatically (DHCP) unless you set it manually under “IPv4…”."));
  g_hint->labelsize(SIZE_SMALL);
  g_hint->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  // Wi-Fi
  g_wifi = new Fl_Group(RX, 206, RW, H - 206 - 50);
  g_wifi_on = new Fl_Check_Button(RX - 4, 208, 200, 22, _("Wi-Fi on"));
  g_wifi_on->labelfont(FONT_SYSTEM);
  g_wifi_on->callback([](Fl_Widget *, void *) {
    bool on = g_wifi_on->value();
    message(on ? _("Turning Wi-Fi on…") : _("Turning Wi-Fi off…"));
    net::set_wifi(on, report(on ? _("Wi-Fi is on.") : _("Wi-Fi is off.")));
  });
  auto *lab = new Fl_Box(RX, 232, RW, 18, _("Networks in range:"));
  lab->labelsize(SIZE_SMALL + 1);
  lab->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_nets = new NetList(RX, 252, RW, H - 252 - 86);
  g_nets->box(WELL_BOX);
  g_nets->callback([](Fl_Widget *, void *) {
    int v = g_nets->value();
    if (Fl::event_clicks() && v > 0 && v <= (int)g_list_nets.size()) {   // dwuklik = połącz
      Fl::event_clicks(0);
      connect_network(g_list_nets[v - 1], "");
    }
  });
  int by = H - 80;
  button(RX, by, 90, _("Connect"))->callback([](Fl_Widget *, void *) {
    int v = g_nets->value();
    if (v > 0 && v <= (int)g_list_nets.size()) connect_network(g_list_nets[v - 1], "");
    else message(_("Choose a network from the list."), true);
  });
  button(RX + 96, by, 100, _("Other Network…"))->callback([](Fl_Widget *, void *) { connect_hidden(); });
  button(RX + 202, by, 90, _("Forget"))->callback([](Fl_Widget *, void *) {
    int v = g_nets->value();
    if (v <= 0 || v > (int)g_list_nets.size() || !g_list_nets[v - 1].known) {
      message(_("Choose a saved network."), true);
      return;
    }
    net::forget(g_list_nets[v - 1].ssid, report(_("Network forgotten.")));
  });
  button(RX + RW - 90, by, 90, _("Hotspot…"))->callback([](Fl_Widget *, void *) { hotspot_dialog(); });
  g_wifi->end();

  // VPN
  g_vpn = new Fl_Group(RX, 76, RW, H - 76 - 50);
  g_vpns = new VpnList(RX, 78, RW, H - 78 - 140);
  g_vpns->box(WELL_BOX);
  g_vpns->callback([](Fl_Widget *, void *) {
    vpn_buttons();
    if (Fl::event_clicks()) { Fl::event_clicks(0); g_vpn_toggle->do_callback(); }
  });
  int vy = H - 56 - 60;
  g_vpn_toggle = button(RX, vy, 90, _("Connect"));
  g_vpn_toggle->labelfont(FONT_SYSTEM);
  g_vpn_toggle->callback([](Fl_Widget *, void *) {
    const net::Vpn *v = selected_vpn();
    if (!v) { message(_("Choose a VPN connection."), true); return; }
    if (v->active || v->activating) {
      message(_("Disconnecting VPN…"));
      net::vpn_down(v->uuid, report(_("VPN disconnected.")));
    } else {
      vpn_connect(*v, "");
    }
  });
  button(RX + 96, vy, 100, _("Import…"))->callback([](Fl_Widget *, void *) { vpn_import_dialog(); });
  button(RX + 202, vy, 90, _("Delete"))->callback([](Fl_Widget *, void *) {
    const net::Vpn *v = selected_vpn();
    if (!v) { message(_("Choose a VPN connection."), true); return; }
    if (fl_choice(_("Delete the VPN connection “%s”?"), _("Cancel"), _("Delete"), nullptr, v->name.c_str()) != 1) return;
    net::remove_connection(v->uuid, report(_("VPN connection deleted.")));
  });
  g_vpn_note = new Fl_Box(RX, vy + 30, RW, 34);
  g_vpn_note->labelsize(SIZE_SMALL);
  g_vpn_note->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  g_vpn->end();
  g_vpn->hide();

  g_air = new Fl_Check_Button(11, H - 38, 160, 24, _("Airplane Mode"));
  g_air->labelsize(SIZE_SMALL + 1);
  g_air->callback([](Fl_Widget *, void *) {
    bool on = g_air->value();
    message(on ? _("Turning off all radios…") : _("Turning radios on…"));
    net::set_airplane(on, report(on ? _("Airplane mode is on.") : _("Airplane mode is off.")));
  });
  g_msg = new Fl_Box(175, H - 38, W - 300, 24);
  g_msg->labelsize(SIZE_SMALL + 1);
  g_msg->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  auto *close = button(W - 110, H - 38, 95, _("Close"));
  close->labelfont(FONT_SYSTEM);
  close->callback([](Fl_Widget *, void *) { g_win->hide(); });
  g_win->end();
  g_win->finish();

  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + g_win->oy(), (sh - H) / 3));

  refresh();
  if (g_st.available) {
    net::rescan();
    net::watch([] { refresh(); });
    static Fl_Timeout_Handler rescan_tick = [](void *) { net::rescan(); Fl::repeat_timeout(30.0, rescan_tick); };
    Fl::add_timeout(30.0, rescan_tick);
  }
  g_win->show();
  if (hidden_dialog) {
    // od razu "Inna sieć" (z menu sieci na pasku): wybierz kartę Wi-Fi
    for (auto &d : g_st.devices) if (d.type == "wifi") { g_iface = d.iface; refresh(); break; }
    Fl::add_timeout(0.2, [](void *) { connect_hidden(); });
  }
  return Fl::run();
}
