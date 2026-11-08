// LisaOS - sieć: Wi-Fi, Ethernet i inne połączenia przez NetworkManager
//
// Odczyty idą przez nmcli w trybie "terse" (LC_ALL=C, żeby stany nie były
// tłumaczone), długie operacje (łączenie, skanowanie) w tle przez Fl::add_fd,
// a połączenie z hasłem przez D-Bus (AddAndActivateConnection) - hasło nie
// trafia do linii poleceń, którą widać w `ps`.
//
// Zmiany na bieżąco: proces `nmcli monitor` w tle; każda jego linia to sygnał
// "coś się zmieniło", po którym odświeżamy stan.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace net {

enum class DevState { Unavailable, Disconnected, Connecting, Connected, Unmanaged };

struct Device {
  std::string iface, type;       // type: ethernet, wifi, gsm, bridge, ...
  std::string connection;        // nazwa aktywnego profilu
  std::string raw_state;
  DevState state = DevState::Disconnected;
};

struct Network {                 // sieć Wi-Fi widoczna w okolicy
  std::string ssid, security, bssid, device;
  int signal = 0;                // 0-100
  bool in_use = false;
  bool known = false;            // jest zapisany profil
  bool secured() const { return !security.empty() && security != "--"; }
  bool enterprise() const { return security.find("802.1X") != std::string::npos; }
};

struct Details {
  std::string ip4, gateway, dns, hwaddr, speed, ip6;
  std::string method;            // auto | manual (z profilu)
};

struct State {
  bool available = false;        // NetworkManager działa
  std::string problem;           // dlaczego niedostępny
  bool wifi_hw = false, wifi_on = false;
  std::vector<Device> devices;   // bez lo i urządzeń niezarządzanych
  std::vector<Network> networks; // jedna pozycja na SSID (najsilniejsza)
  std::vector<std::string> saved_wifi;   // nazwy zapisanych profili Wi-Fi
};

bool installed();                // czy jest nmcli
State query();                   // szybki odczyt (bez skanowania)
Details details(const std::string &iface);
const char *type_name(const std::string &type);   // "Wi-Fi", "Ethernet", ...
const char *state_name(DevState s);
int bars(int signal);            // 0-4 kreski

// Najważniejsze połączenie do pokazania na pasku menu.
// (nie "None" - to makro X11)
struct Summary {
  enum Kind { Brak, Wifi, Wired, Other } kind = Brak;
  bool connecting = false;
  int signal = 0;
  std::string name;
};
Summary summary(const State &s);

extern const char *const NEED_PASSWORD;   // komunikat: trzeba zapytać o hasło

// Operacje w tle; po zakończeniu done(ok, komunikat po polsku).
using Done = std::function<void(bool ok, const std::string &msg)>;
void rescan();
void set_wifi(bool on, Done done);
void connect_device(const std::string &iface, Done done);
void disconnect_device(const std::string &iface, Done done);
// Łączenie z siecią Wi-Fi. password puste: sieć otwarta albo zapisana.
// hidden: sieć ukryta (bez rozgłaszanego SSID).
void connect_wifi(const std::string &ssid, const std::string &security,
                  const std::string &password, bool hidden, Done done);
void forget(const std::string &connection, Done done);
// IPv4 profilu: manual=false -> DHCP; address w postaci 192.168.1.10/24.
void set_ipv4(const std::string &connection, bool manual, const std::string &address,
              const std::string &gateway, const std::string &dns, Done done);

// Obserwowanie zmian (nmcli monitor). changed wołane z opóźnieniem ~0,3 s.
void watch(std::function<void()> changed);

}  // namespace net
