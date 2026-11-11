// Nirvana Desktop - network: Wi-Fi, Ethernet and other connections through NetworkManager
//
// Reads go through nmcli in "terse" mode (LC_ALL=C, so the states are not
// translated), long operations (connecting, scanning) in the background through Fl::add_fd,
// and a connection with a password through D-Bus (AddAndActivateConnection) - the password
// never lands on a command line visible in `ps`.
//
// Live changes: an `nmcli monitor` process in the background; each of its lines is a signal
// "something changed", after which we refresh the state.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace net {

enum class DevState { Unavailable, Disconnected, Connecting, Connected, Unmanaged };

struct Device {
  std::string iface, type;       // type: ethernet, wifi, gsm, bridge, ...
  std::string connection;        // name of the active profile
  std::string raw_state;
  DevState state = DevState::Disconnected;
};

struct Network {                 // a Wi-Fi network visible nearby
  std::string ssid, security, bssid, device;
  int signal = 0;                // 0-100
  bool in_use = false;
  bool known = false;            // there is a saved profile
  bool secured() const { return !security.empty() && security != "--"; }
  bool enterprise() const { return security.find("802.1X") != std::string::npos; }
};

struct Details {
  std::string ip4, gateway, dns, hwaddr, speed, ip6;
  std::string method;            // auto | manual (from the profile)
};

struct Vpn {                     // a VPN profile (NetworkManager plugin or WireGuard)
  std::string name, uuid;
  std::string kind;              // openvpn, wireguard, openconnect, vpnc, pptp, l2tp, ...
  bool active = false, activating = false;
};

struct State {
  bool available = false;        // NetworkManager is running
  std::string problem;           // why it is unavailable
  bool wifi_hw = false, wifi_on = false;
  bool wwan_on = true;
  bool airplane() const { return !wifi_on && !wwan_on; }   // all radios off
  std::vector<Vpn> vpns;
  std::string hotspot_ssid;      // non-empty: this machine shares a Wi-Fi network
  std::vector<Device> devices;   // without lo and unmanaged devices
  std::vector<Network> networks; // one entry per SSID (the strongest)
  std::vector<std::string> saved_wifi;   // names of the saved Wi-Fi profiles
};

bool installed();                // is nmcli there
State query();                   // a quick read (no scanning)
Details details(const std::string &iface);
const char *type_name(const std::string &type);   // "Wi-Fi", "Ethernet", ...
const char *state_name(DevState s);
int bars(int signal);            // 0-4 bars

// The most important connection to show in the menu bar.
// (not "None" - that is an X11 macro)
struct Summary {
  enum Kind { NoNetwork, Wifi, Wired, Other } kind = NoNetwork;
  bool connecting = false;
  int signal = 0;
  std::string name;
  std::string vpn;                // name of the active VPN (padlock on the icon)
};
Summary summary(const State &s);

extern const char *const NEED_PASSWORD;   // message: the password has to be asked for

// Background operations; when finished done(ok, message in the interface language).
using Done = std::function<void(bool ok, const std::string &msg)>;
void rescan();
void set_wifi(bool on, Done done);
void connect_device(const std::string &iface, Done done);
void disconnect_device(const std::string &iface, Done done);
// Connecting to a Wi-Fi network. Empty password: an open or saved network.
// hidden: a hidden network (no broadcast SSID).
void connect_wifi(const std::string &ssid, const std::string &security,
                  const std::string &password, bool hidden, Done done);
void forget(const std::string &connection, Done done);
// The profile's IPv4: manual=false -> DHCP; address as 192.168.1.10/24.
void set_ipv4(const std::string &connection, bool manual, const std::string &address,
              const std::string &gateway, const std::string &dns, Done done);

// Airplane mode: turns all radios off (or on) - Wi-Fi and modem.
void set_airplane(bool on, Done done);

// --- VPN ---
const char *vpn_kind_name(const std::string &kind);      // "OpenVPN", "WireGuard", ...
std::vector<std::string> vpn_plugins();                   // installed plugins (without WireGuard)
// Importing a configuration file: .conf with [Interface] -> WireGuard, .ovpn -> OpenVPN,
// .pcf -> Cisco VPNC. After the import done(ok, profile name or error).
void vpn_import(const std::string &file, Done done);
std::string vpn_username(const std::string &uuid);       // from vpn.data (OpenVPN etc.)
// Connect; a non-empty password is passed through a password file (passwd-file) with mode 0600.
void vpn_up(const std::string &uuid, const std::string &user, const std::string &password, Done done);
void vpn_down(const std::string &uuid, Done done);
void remove_connection(const std::string &uuid, Done done);

// --- Hotspot: sharing the connection over Wi-Fi (WPA2, IPv4 "shared") ---
void hotspot(const std::string &ssid, const std::string &password, Done done);

// Watching for changes (nmcli monitor). changed is called with a ~0.3 s delay.
void watch(std::function<void()> changed);

}  // namespace net
