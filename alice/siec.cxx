// LisaOS - alice: panel "Sieć"
//
// Lista połączeń (Ethernet, Wi-Fi, modem...) po lewej, szczegóły wybranego po
// prawej: adresy, Połącz/Rozłącz, ustawienia IPv4, a dla Wi-Fi lista sieci
// w zasięgu z łączeniem, ukrytymi sieciami i zapominaniem. Wszystko przez
// NetworkManagera (lisaos-net); zmiany z zewnątrz widać na bieżąco.
#include "net.h"
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

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace plat;

namespace {

net::State g_st;
std::string g_iface;                 // wybrane urządzenie
std::string g_conn;                  // jego aktywny profil
Fl_Double_Window *g_win;
Fl_Hold_Browser *g_devs, *g_nets;
Fl_Box *g_title, *g_status, *g_msg, *g_hint;
Fl_Button *g_connect, *g_ipv4;
Fl_Group *g_info, *g_wifi;
Fl_Output *g_out[6];
Fl_Check_Button *g_wifi_on;
std::vector<net::Network> g_list_nets;   // sieci w kolejności listy

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
    else draw_wired(X + 6, Y + 11, up ? fg : off);
    fl_color(fg);
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    std::string name = std::string(net::type_name(f[0])) + " (" + f[1] + ")";
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
    draw_signal(X + W - 24, Y + 5, v[1] - '0', fg, fl_color_average(fg, sel ? SEL : CONTENT, 0.3f));
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
bool run_modal(Fl_Double_Window *w, Fl_Button *ok, Fl_Button *cancel) {
  int result = -1;
  ok->callback([](Fl_Widget *, void *r) { *(int *)r = 1; }, &result);
  cancel->callback([](Fl_Widget *, void *r) { *(int *)r = 0; }, &result);
  w->callback([](Fl_Widget *, void *r) { *(int *)r = 0; }, &result);
  ok->shortcut(FL_Enter);
  w->set_modal();
  w->position(g_win->x() + (g_win->w() - w->w()) / 2, g_win->y() + 60);
  w->show();
  while (result < 0 && w->shown()) Fl::wait();
  w->hide();
  return result == 1;
}

struct WifiAnswer { std::string ssid, security, password; };

// Hasło do sieci; hidden=true dodaje pola nazwy i rodzaju zabezpieczeń.
bool ask_wifi(bool hidden, const std::string &ssid, const std::string &err, WifiAnswer &out) {
  int W = 420, H = hidden ? 250 : 180;
  auto *w = new Fl_Double_Window(W, H, hidden ? "Inna sieć Wi-Fi" : "Sieć Wi-Fi");
  struct Pic : Fl_Widget {
    Pic(int X, int Y) : Fl_Widget(X, Y, 34, 34) {}
    void draw() override { draw_icon(Icon::Wifi, x(), y(), false); }
  };
  new Pic(16, 16);
  std::string text = hidden ? std::string("Połącz z siecią, która nie rozgłasza nazwy.")
                            : "Sieć „" + ssid + "” jest zabezpieczona.\nPodaj hasło, aby się połączyć.";
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
    name = new Fl_Input(170, y, W - 186, 24, "Nazwa sieci:");
    name->box(WELL_BOX);
    name->value(ssid.c_str());
    y += 32;
    sec = new Fl_Choice(170, y, W - 186, 24, "Zabezpieczenia:");
    sec->add("WPA\\/WPA2 Personal|WPA3 Personal|Brak");
    sec->value(0);
    y += 32;
  }
  auto *pass = new Fl_Secret_Input(170, y, W - 186, 24, "Hasło:");
  pass->box(WELL_BOX);
  y += 28;
  auto *show = new Fl_Check_Button(166, y, 160, 20, "Pokaż hasło");
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
  auto *cancel = button(W - 196, H - 38, 86, "Anuluj");
  auto *ok = button(W - 102, H - 38, 86, "Połącz");
  ok->labelfont(FONT_SYSTEM);
  w->end();
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
    message("Sieci firmowe (802.1X) nie są jeszcze obsługiwane - użyj nmtui.", true);
    return;
  }
  auto done = [n](bool ok, const std::string &m) {
    if (!ok && (m == net::NEED_PASSWORD || m == "Nieprawidłowe hasło." ||
                m == "Nie udało się połączyć. Sprawdź hasło.")) {
      connect_network(n, m == net::NEED_PASSWORD ? "" : m);
      return;
    }
    message(ok ? "Połączono z siecią „" + n.ssid + "”." : m, !ok);
    refresh();
  };
  if (err.empty() && (!n.secured() || n.known)) {
    message("Łączenie z „" + n.ssid + "”…");
    net::connect_wifi(n.ssid, n.security, "", false, done);
    return;
  }
  WifiAnswer a;
  if (!ask_wifi(false, n.ssid, err, a)) return;
  message("Łączenie z „" + n.ssid + "”…");
  net::connect_wifi(n.ssid, n.security, a.password, false, done);
  std::fill(a.password.begin(), a.password.end(), '\0');
}

void connect_hidden() {
  WifiAnswer a;
  if (!ask_wifi(true, "", "", a)) return;
  message("Łączenie z „" + a.ssid + "”…");
  std::string ssid = a.ssid;
  net::connect_wifi(a.ssid, a.security, a.password, true, [ssid](bool ok, const std::string &m) {
    message(ok ? "Połączono z siecią „" + ssid + "”." : m, !ok);
    refresh();
  });
  std::fill(a.password.begin(), a.password.end(), '\0');
}

void ipv4_dialog() {
  if (g_conn.empty()) return;
  net::Details d = net::details(g_iface);
  int W = 400, H = 220;
  auto *w = new Fl_Double_Window(W, H, "Ustawienia IPv4");
  std::string head = "Połączenie „" + g_conn + "”";
  auto *t = new Fl_Box(16, 10, W - 32, 22);
  t->copy_label(head.c_str());
  t->labelfont(FONT_SYSTEM);
  t->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  auto *how = new Fl_Choice(150, 40, W - 166, 24, "Konfiguracja:");
  how->add("Automatycznie (DHCP)|Ręcznie");
  how->value(d.method == "manual" ? 1 : 0);
  auto *addr = new Fl_Input(150, 72, W - 166, 24, "Adres/prefiks:");
  auto *gw = new Fl_Input(150, 102, W - 166, 24, "Brama:");
  auto *dns = new Fl_Input(150, 132, W - 166, 24, "Serwery DNS:");
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
  auto *cancel = button(W - 196, H - 38, 86, "Anuluj");
  auto *ok = button(W - 102, H - 38, 86, "Zapisz");
  ok->labelfont(FONT_SYSTEM);
  w->end();
  if (run_modal(w, ok, cancel)) {
    bool manual = how->value() == 1;
    std::string dn = dns->value();
    std::replace(dn.begin(), dn.end(), ' ', ',');
    if (manual && !*addr->value()) message("Podaj adres IPv4, np. 192.168.1.20/24.", true);
    else {
      message("Zapisywanie ustawień IPv4…");
      net::set_ipv4(g_conn, manual, addr->value(), gw->value(), dn, report("Zapisano ustawienia IPv4."));
    }
  }
  Fl::delete_widget(w);
}

// --- odświeżanie ----------------------------------------------------------------
const net::Device *selected_device() {
  for (auto &d : g_st.devices) if (d.iface == g_iface) return &d;
  return nullptr;
}

void show_device() {
  const net::Device *d = selected_device();
  g_wifi->hide();
  g_info->hide();
  g_connect->hide();
  g_ipv4->hide();
  g_hint->hide();
  if (!g_st.available) {
    g_title->label("Sieć niedostępna");
    static std::string p;
    p = g_st.problem + "\nLisaOS zarządza siecią przez NetworkManagera: zainstaluj pakiet "
        "network-manager i uruchom usługę NetworkManager.";
    g_status->label(p.c_str());
    g_win->redraw();
    return;
  }
  if (!d) {
    g_title->label("Brak połączeń");
    g_status->label("Nie znaleziono kart sieciowych zarządzanych przez NetworkManagera.");
    g_win->redraw();
    return;
  }
  static std::string title, status;
  title = std::string(net::type_name(d->type)) + " (" + d->iface + ")";
  g_title->label(title.c_str());
  g_conn = d->state == net::DevState::Connected || d->state == net::DevState::Connecting ? d->connection : "";
  switch (d->state) {
    case net::DevState::Connected:
      status = d->type == "wifi" ? "Połączono z siecią „" + d->connection + "”."
                                 : "Połączono („" + d->connection + "”).";
      break;
    case net::DevState::Connecting: status = "Łączenie…"; break;
    case net::DevState::Unavailable:
      status = d->type == "wifi" ? (g_st.wifi_on ? "Karta Wi-Fi niedostępna." : "Wi-Fi jest wyłączone.")
                                 : "Kabel sieciowy jest odłączony.";
      break;
    default: status = "Rozłączono."; break;
  }
  g_status->label(status.c_str());

  bool up = d->state == net::DevState::Connected || d->state == net::DevState::Connecting;
  if (d->state != net::DevState::Unavailable) {
    g_connect->label(up ? "Rozłącz" : "Połącz");
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
    if (g_list_nets.empty()) g_nets->add(g_st.wifi_on ? "0000Szukam sieci…" : "0000(Wi-Fi wyłączone)");
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
  if (!sel && !g_st.devices.empty()) { sel = 1; g_iface = g_st.devices[0].iface; }
  if (sel) g_devs->value(sel);
  show_device();
}

}  // namespace

int panel_siec(bool hidden_dialog) {
  const int W = 640, H = 470;
  g_win = new Fl_Double_Window(W, H, "Sieć");
  g_win->xclass("lisaos-siec");

  auto *t1 = new Fl_Box(15, 8, 200, 20, "Połączenia");
  t1->labelfont(FONT_SYSTEM);
  t1->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_devs = new DevList(15, 30, 200, H - 80);
  g_devs->box(WELL_BOX);
  g_devs->callback([](Fl_Widget *, void *) {
    int v = g_devs->value();
    if (v > 0 && v <= (int)g_st.devices.size()) { g_iface = g_st.devices[v - 1].iface; show_device(); }
  });

  const int RX = 232, RW = W - RX - 15;
  g_title = new Fl_Box(RX, 8, RW - 200, 24);
  g_title->labelfont(FONT_SYSTEM);
  g_title->labelsize(16);
  g_title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_status = new Fl_Box(RX, 34, RW, 34);
  g_status->labelsize(SIZE_SYSTEM);
  g_status->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  g_ipv4 = button(RX + RW - 190, 8, 90, "IPv4…");
  g_ipv4->callback([](Fl_Widget *, void *) { ipv4_dialog(); });
  g_connect = button(RX + RW - 94, 8, 94, "Połącz");
  g_connect->labelfont(FONT_SYSTEM);
  g_connect->callback([](Fl_Widget *, void *) {
    const net::Device *d = selected_device();
    if (!d) return;
    bool up = d->state == net::DevState::Connected || d->state == net::DevState::Connecting;
    std::string iface = d->iface;
    if (up) { message("Rozłączanie…"); net::disconnect_device(iface, report("Rozłączono.")); }
    else if (d->type == "wifi" && g_nets->value() > 0 && g_nets->value() <= (int)g_list_nets.size())
      connect_network(g_list_nets[g_nets->value() - 1], "");
    else { message("Łączenie…"); net::connect_device(iface, report("Połączono.")); }
  });

  // szczegóły połączenia
  g_info = new Fl_Group(RX, 70, RW, 130);
  const char *labels[] = {"Adres IPv4:", "Brama:", "Serwery DNS:", "Adres IPv6:", "Adres MAC:", "Szybkość:"};
  for (int i = 0; i < 6; i++) {
    g_out[i] = new Fl_Output(RX + 110, 72 + i * 21, RW - 110, 20, labels[i]);
    g_out[i]->box(FL_FLAT_BOX);
    g_out[i]->color(GRAY);
    g_out[i]->labelsize(SIZE_SMALL + 1);
    g_out[i]->textsize(SIZE_SMALL + 1);
  }
  g_info->end();
  g_hint = new Fl_Box(RX, 76, RW, 60,
                      "Podłącz kabel albo kliknij „Połącz”. Adres zostanie pobrany automatycznie (DHCP), "
                      "chyba że w „IPv4…” ustawisz go ręcznie.");
  g_hint->labelsize(SIZE_SMALL);
  g_hint->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  // Wi-Fi
  g_wifi = new Fl_Group(RX, 206, RW, H - 206 - 50);
  g_wifi_on = new Fl_Check_Button(RX - 4, 208, 200, 22, "Wi-Fi włączone");
  g_wifi_on->labelfont(FONT_SYSTEM);
  g_wifi_on->callback([](Fl_Widget *, void *) {
    bool on = g_wifi_on->value();
    message(on ? "Włączanie Wi-Fi…" : "Wyłączanie Wi-Fi…");
    net::set_wifi(on, report(on ? "Wi-Fi włączone." : "Wi-Fi wyłączone."));
  });
  auto *lab = new Fl_Box(RX, 232, RW, 18, "Sieci w zasięgu:");
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
  button(RX, by, 90, "Połącz")->callback([](Fl_Widget *, void *) {
    int v = g_nets->value();
    if (v > 0 && v <= (int)g_list_nets.size()) connect_network(g_list_nets[v - 1], "");
    else message("Wybierz sieć z listy.", true);
  });
  button(RX + 96, by, 100, "Inna sieć…")->callback([](Fl_Widget *, void *) { connect_hidden(); });
  button(RX + 202, by, 90, "Zapomnij")->callback([](Fl_Widget *, void *) {
    int v = g_nets->value();
    if (v <= 0 || v > (int)g_list_nets.size() || !g_list_nets[v - 1].known) {
      message("Wybierz zapisaną sieć.", true);
      return;
    }
    net::forget(g_list_nets[v - 1].ssid, report("Zapomniano sieć."));
  });
  button(RX + RW - 90, by, 90, "Odśwież")->callback([](Fl_Widget *, void *) {
    net::rescan();
    message("Szukam sieci…");
    Fl::add_timeout(3.0, [](void *) { refresh(); message(""); });
  });
  g_wifi->end();

  g_msg = new Fl_Box(15, H - 38, W - 140, 24);
  g_msg->labelsize(SIZE_SMALL + 1);
  g_msg->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  auto *close = button(W - 110, H - 38, 95, "Zamknij");
  close->labelfont(FONT_SYSTEM);
  close->callback([](Fl_Widget *, void *) { g_win->hide(); });
  g_win->end();

  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->position(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10, (sh - H) / 3));

  refresh();
  if (g_st.available) {
    net::rescan();
    net::watch([] { refresh(); });
  }
  g_win->show();
  if (hidden_dialog) {
    // od razu "Inna sieć" (z menu sieci na pasku): wybierz kartę Wi-Fi
    for (auto &d : g_st.devices) if (d.type == "wifi") { g_iface = d.iface; refresh(); break; }
    Fl::add_timeout(0.2, [](void *) { connect_hidden(); });
  }
  return Fl::run();
}
