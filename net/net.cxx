// LisaOS - sieć: implementacja (nmcli + D-Bus NetworkManagera)
#include "net.h"

#include <FL/Fl.H>
#include <dbus/dbus.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace net {

static const char *nmcli() {
  const char *e = getenv("LISAOS_NMCLI");   // do testów: atrapa nmcli
  return e && *e ? e : "nmcli";
}

bool installed() {
  std::string n = nmcli();
  if (n.find('/') != std::string::npos) return access(n.c_str(), X_OK) == 0;
  const char *path = getenv("PATH");
  std::string p = path ? path : "/usr/bin:/bin";
  size_t a = 0;
  while (a <= p.size()) {
    size_t b = p.find(':', a);
    if (b == std::string::npos) b = p.size();
    if (access((p.substr(a, b - a) + "/" + n).c_str(), X_OK) == 0) return true;
    a = b + 1;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Uruchamianie nmcli
// ---------------------------------------------------------------------------
static pid_t start(const std::vector<std::string> &args, int &out_fd) {
  int p[2];
  if (pipe2(p, O_CLOEXEC) != 0) return -1;
  pid_t pid = fork();
  if (pid < 0) { close(p[0]); close(p[1]); return -1; }
  if (pid == 0) {
    prctl(PR_SET_PDEATHSIG, SIGTERM);   // np. "nmcli monitor" nie przeżyje Ramblera
    dup2(p[1], 1);
    dup2(p[1], 2);
    int devnull = open("/dev/null", O_RDONLY);
    if (devnull >= 0) dup2(devnull, 0);
    setenv("LC_ALL", "C", 1);   // stany i komunikaty po angielsku - parsujemy je
    std::vector<char *> argv;
    argv.push_back((char *)nmcli());
    for (auto &a : args) argv.push_back((char *)a.c_str());
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  close(p[1]);
  out_fd = p[0];
  return pid;
}

static int run(const std::vector<std::string> &args, std::string &out) {
  int fd;
  pid_t pid = start(args, fd);
  if (pid < 0) return -1;
  char buf[4096];
  ssize_t n;
  while ((n = read(fd, buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR))
    if (n > 0) out.append(buf, n);
  close(fd);
  int st = 0;
  while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

// Polskie komunikaty dla typowych błędów nmcli / NetworkManagera.
extern const char *const NEED_PASSWORD;
static std::string pl_error(const std::string &raw, bool had_password = false) {
  auto has = [&](const char *s) { return strcasestr(raw.c_str(), s) != nullptr; };
  if (has("Secrets were required") || has("no secrets") || has("802-1x"))
    return had_password ? "Nieprawidłowe hasło." : "Ta sieć wymaga hasła.";
  if (has("No network with SSID")) return "Nie znaleziono tej sieci w zasięgu.";
  if (has("not authorized") || has("Insufficient privileges") || has("PermissionDenied"))
    return "Brak uprawnień do zmiany ustawień sieci.";
  if (has("NetworkManager is not running") || has("ServiceUnknown"))
    return "NetworkManager nie działa.";
  if (has("timed out") || has("Timeout")) return "Przekroczono czas oczekiwania na połączenie.";
  if (has("IP configuration could not be reserved") || has("dhcp"))
    return "Nie udało się uzyskać adresu IP.";
  if (has("invalid IP") || has("invalid address") || has("ipv4.addresses") || has("ipv4.gateway"))
    return "Nieprawidłowy adres IP.";
  if (has("unknown connection")) return "Nie ma takiego połączenia.";
  if (has("No such file") && has("nmcli")) return "Brak programu nmcli (NetworkManager).";
  std::string m = raw.substr(0, raw.find('\n'));
  if (m.rfind("Error: ", 0) == 0) m = m.substr(7);
  return m.empty() ? "Operacja się nie udała." : "Nie udało się: " + m;
}
const char *const NEED_PASSWORD = "Ta sieć wymaga hasła.";

// Pola w trybie -t: dwukropek rozdziela, "\:" i "\\" to znaki dosłowne.
static std::vector<std::string> fields(const std::string &line) {
  std::vector<std::string> f(1);
  for (size_t i = 0; i < line.size(); i++) {
    char c = line[i];
    if (c == '\\' && i + 1 < line.size()) { f.back() += line[++i]; continue; }
    if (c == ':') { f.emplace_back(); continue; }
    f.back() += c;
  }
  return f;
}
static std::vector<std::string> lines(const std::string &s) {
  std::vector<std::string> v;
  size_t a = 0;
  while (a < s.size()) {
    size_t b = s.find('\n', a);
    if (b == std::string::npos) b = s.size();
    if (b > a) v.push_back(s.substr(a, b - a));
    a = b + 1;
  }
  return v;
}

// Zadanie w tle: wyjście zbieramy przez Fl::add_fd, kończymy po EOF.
struct Job {
  pid_t pid;
  int fd;
  std::string out;
  std::function<void(int, const std::string &)> done;
};
static void job_cb(int fd, void *d) {
  auto *j = (Job *)d;
  char buf[4096];
  ssize_t n = read(fd, buf, sizeof buf);
  if (n > 0) { j->out.append(buf, n); return; }
  if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
  Fl::remove_fd(fd);
  close(fd);
  int st = 0;
  while (waitpid(j->pid, &st, 0) < 0 && errno == EINTR) {}
  int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
  auto done = std::move(j->done);
  std::string out = std::move(j->out);
  delete j;
  if (done) done(code, out);
}
static void run_async(const std::vector<std::string> &args,
                      std::function<void(int, const std::string &)> done) {
  int fd;
  pid_t pid = start(args, fd);
  if (pid < 0) { if (done) done(-1, "fork"); return; }
  auto *j = new Job{pid, fd, "", std::move(done)};
  Fl::add_fd(fd, FL_READ, job_cb, j);
}
static void simple(const std::vector<std::string> &args, Done done, const char *ok_msg = "") {
  run_async(args, [done, ok_msg](int code, const std::string &out) {
    if (done) done(code == 0, code == 0 ? std::string(ok_msg) : pl_error(out));
  });
}

// ---------------------------------------------------------------------------
// Odczyt stanu
// ---------------------------------------------------------------------------
static DevState parse_state(const std::string &s) {
  if (s.rfind("connected", 0) == 0) return DevState::Connected;
  if (s.rfind("connecting", 0) == 0 || s == "deactivating") return DevState::Connecting;
  if (s == "unavailable") return DevState::Unavailable;
  if (s == "unmanaged") return DevState::Unmanaged;
  return DevState::Disconnected;
}

State query() {
  State st;
  std::string out;
  int code = run({"-t", "-f", "RUNNING", "general"}, out);
  if (code == 127 || code < 0) { st.problem = "Brak programu nmcli (pakiet network-manager)."; return st; }
  if (code != 0 || out.find("running") == std::string::npos) {
    st.problem = "NetworkManager nie działa.";
    return st;
  }
  st.available = true;
  out.clear();
  if (run({"-t", "-f", "WIFI", "radio"}, out) == 0) st.wifi_on = out.find("enabled") != std::string::npos;

  out.clear();
  run({"-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device"}, out);
  static const char *keep[] = {"ethernet", "wifi", "gsm", "bt", "wireguard", "vpn", "infiniband", nullptr};
  for (auto &l : lines(out)) {
    auto f = fields(l);
    if (f.size() < 4) continue;
    bool ok = false;
    for (int i = 0; keep[i]; i++) if (f[1] == keep[i]) ok = true;
    if (!ok) continue;
    Device d;
    d.iface = f[0]; d.type = f[1]; d.raw_state = f[2]; d.connection = f[3];
    d.state = parse_state(f[2]);
    if (d.state == DevState::Unmanaged) continue;
    if (d.type == "wifi") st.wifi_hw = true;
    st.devices.push_back(d);
  }
  // przewodowe na początku, potem Wi-Fi, potem reszta
  auto rank = [](const Device &d) { return d.type == "ethernet" ? 0 : d.type == "wifi" ? 1 : 2; };
  std::stable_sort(st.devices.begin(), st.devices.end(),
                   [&](const Device &a, const Device &b) { return rank(a) < rank(b); });

  out.clear();
  run({"-t", "-f", "NAME,TYPE", "connection", "show"}, out);
  for (auto &l : lines(out)) {
    auto f = fields(l);
    if (f.size() >= 2 && f[1] == "802-11-wireless") st.saved_wifi.push_back(f[0]);
  }

  if (st.wifi_hw && st.wifi_on) {
    out.clear();
    run({"-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY,BSSID,DEVICE", "device", "wifi", "list", "--rescan", "no"}, out);
    for (auto &l : lines(out)) {
      auto f = fields(l);
      if (f.size() < 6 || f[1].empty()) continue;
      Network n;
      n.in_use = f[0] == "*";
      n.ssid = f[1];
      n.signal = atoi(f[2].c_str());
      n.security = f[3];
      n.bssid = f[4];
      n.device = f[5];
      n.known = std::find(st.saved_wifi.begin(), st.saved_wifi.end(), n.ssid) != st.saved_wifi.end();
      auto it = std::find_if(st.networks.begin(), st.networks.end(),
                             [&](const Network &x) { return x.ssid == n.ssid; });
      if (it == st.networks.end()) st.networks.push_back(n);
      else {
        bool in_use = it->in_use || n.in_use;
        if (n.signal > it->signal) *it = n;
        it->in_use = in_use;
      }
    }
    std::sort(st.networks.begin(), st.networks.end(), [](const Network &a, const Network &b) {
      if (a.in_use != b.in_use) return a.in_use;
      if (a.known != b.known) return a.known;
      return a.signal > b.signal;
    });
  }
  return st;
}

Details details(const std::string &iface) {
  Details d;
  std::string out;
  run({"-t", "-f", "GENERAL.HWADDR,GENERAL.CONNECTION,CAPABILITIES.SPEED,IP4.ADDRESS,IP4.GATEWAY,IP4.DNS,IP6.ADDRESS",
       "device", "show", iface}, out);
  std::string conn;
  auto add = [](std::string &dst, const std::string &v) { if (!v.empty()) dst += (dst.empty() ? "" : ", ") + v; };
  for (auto &l : lines(out)) {
    auto c = l.find(':');
    if (c == std::string::npos) continue;
    std::string k = l.substr(0, c), v;
    for (size_t i = c + 1; i < l.size(); i++) {   // wartość z "\:" -> ":"
      if (l[i] == '\\' && i + 1 < l.size()) { v += l[++i]; continue; }
      v += l[i];
    }
    if (v == "--") v.clear();
    if (k == "GENERAL.HWADDR") d.hwaddr = v;
    else if (k == "GENERAL.CONNECTION") conn = v;
    else if (k == "CAPABILITIES.SPEED") d.speed = v == "unknown" ? "" : v;
    else if (k.rfind("IP4.ADDRESS", 0) == 0) add(d.ip4, v);
    else if (k == "IP4.GATEWAY") d.gateway = v;
    else if (k.rfind("IP4.DNS", 0) == 0) add(d.dns, v);
    else if (k.rfind("IP6.ADDRESS", 0) == 0 && v.rfind("fe80", 0) != 0) add(d.ip6, v);
  }
  if (!conn.empty()) {
    out.clear();
    if (run({"-g", "ipv4.method", "connection", "show", "id", conn}, out) == 0)
      d.method = out.substr(0, out.find('\n'));
  }
  return d;
}

const char *type_name(const std::string &t) {
  if (t == "ethernet") return "Ethernet";
  if (t == "wifi") return "Wi-Fi";
  if (t == "gsm") return "Modem komórkowy";
  if (t == "bt") return "Bluetooth";
  if (t == "wireguard") return "WireGuard";
  if (t == "vpn") return "VPN";
  return "Sieć";
}

const char *state_name(DevState s) {
  switch (s) {
    case DevState::Connected: return "połączono";
    case DevState::Connecting: return "łączenie…";
    case DevState::Unavailable: return "niedostępne";
    case DevState::Unmanaged: return "poza NetworkManagerem";
    default: return "rozłączono";
  }
}

int bars(int signal) { return signal >= 75 ? 4 : signal >= 50 ? 3 : signal >= 30 ? 2 : signal > 5 ? 1 : 0; }

Summary summary(const State &s) {
  Summary r;
  if (!s.available) return r;
  for (auto &d : s.devices) {
    if (d.state == DevState::Connecting && r.kind == Summary::Brak) { r.connecting = true; r.name = d.connection; }
    if (d.state != DevState::Connected) continue;
    Summary::Kind k = d.type == "ethernet" ? Summary::Wired : d.type == "wifi" ? Summary::Wifi : Summary::Other;
    // przewodowe ma pierwszeństwo, jak w systemie: zwykle to ono jest domyślną trasą
    if (r.kind == Summary::Brak || (k == Summary::Wired && r.kind != Summary::Wired)) {
      r.kind = k;
      r.connecting = false;
      r.name = d.connection;
      if (k == Summary::Wifi)
        for (auto &n : s.networks) if (n.in_use) r.signal = n.signal;
    }
  }
  return r;
}

// ---------------------------------------------------------------------------
// Operacje
// ---------------------------------------------------------------------------
void rescan() { run_async({"device", "wifi", "rescan"}, nullptr); }

void set_wifi(bool on, Done done) { simple({"radio", "wifi", on ? "on" : "off"}, done); }
void connect_device(const std::string &iface, Done done) {
  simple({"--wait", "45", "device", "connect", iface}, done, "Połączono.");
}
void disconnect_device(const std::string &iface, Done done) {
  simple({"device", "disconnect", iface}, done, "Rozłączono.");
}
void forget(const std::string &connection, Done done) {
  simple({"connection", "delete", "id", connection}, done, "Zapomniano sieć.");
}

void set_ipv4(const std::string &conn, bool manual, const std::string &address,
              const std::string &gateway, const std::string &dns, Done done) {
  std::vector<std::string> a = {"connection", "modify", "id", conn};
  if (manual) {
    std::string addr = address;
    if (addr.find('/') == std::string::npos) addr += "/24";
    a.insert(a.end(), {"ipv4.method", "manual", "ipv4.addresses", addr, "ipv4.gateway", gateway,
                       "ipv4.dns", dns});
  } else {
    a.insert(a.end(), {"ipv4.method", "auto", "ipv4.addresses", "", "ipv4.gateway", "", "ipv4.dns", ""});
  }
  run_async(a, [conn, done](int code, const std::string &out) {
    if (code != 0) { if (done) done(false, pl_error(out)); return; }
    simple({"--wait", "45", "connection", "up", "id", conn}, done, "Zapisano ustawienia IPv4.");
  });
}

// --- D-Bus: połączenie z hasłem ----------------------------------------------
static const char *NM = "org.freedesktop.NetworkManager";

static void put_var_str(DBusMessageIter *dict, const char *key, const std::string &val) {
  DBusMessageIter e, v;
  dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
  dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "s", &v);
  const char *s = val.c_str();
  dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &s);
  dbus_message_iter_close_container(&e, &v);
  dbus_message_iter_close_container(dict, &e);
}
static void put_var_bool(DBusMessageIter *dict, const char *key, bool val) {
  DBusMessageIter e, v;
  dbus_bool_t b = val;
  dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
  dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "b", &v);
  dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &b);
  dbus_message_iter_close_container(&e, &v);
  dbus_message_iter_close_container(dict, &e);
}
static void put_var_u32(DBusMessageIter *dict, const char *key, unsigned val) {
  DBusMessageIter e, v;
  dbus_uint32_t u = val;
  dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
  dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "u", &v);
  dbus_message_iter_append_basic(&v, DBUS_TYPE_UINT32, &u);
  dbus_message_iter_close_container(&e, &v);
  dbus_message_iter_close_container(dict, &e);
}
static void put_var_bytes(DBusMessageIter *dict, const char *key, const std::string &val) {
  DBusMessageIter e, v, a;
  dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
  dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "ay", &v);
  dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "y", &a);
  const unsigned char *p = (const unsigned char *)val.data();
  dbus_message_iter_append_fixed_array(&a, DBUS_TYPE_BYTE, &p, (int)val.size());
  dbus_message_iter_close_container(&v, &a);
  dbus_message_iter_close_container(&e, &v);
  dbus_message_iter_close_container(dict, &e);
}
// Sekcja ustawień: klucz -> słownik a{sv}, wypełniany przez fill.
static void section(DBusMessageIter *top, const char *name, const std::function<void(DBusMessageIter *)> &fill) {
  DBusMessageIter e, d;
  dbus_message_iter_open_container(top, DBUS_TYPE_DICT_ENTRY, nullptr, &e);
  dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &name);
  dbus_message_iter_open_container(&e, DBUS_TYPE_ARRAY, "{sv}", &d);
  fill(&d);
  dbus_message_iter_close_container(&e, &d);
  dbus_message_iter_close_container(top, &e);
}

static DBusConnection *bus(std::string &err) {
  DBusError e;
  dbus_error_init(&e);
  DBusConnection *c = dbus_bus_get(DBUS_BUS_SYSTEM, &e);
  if (!c) { err = e.message ? e.message : "D-Bus"; dbus_error_free(&e); }
  else dbus_connection_set_exit_on_disconnect(c, false);
  return c;
}

static DBusMessage *call(DBusConnection *c, DBusMessage *m, std::string &err) {
  DBusError e;
  dbus_error_init(&e);
  DBusMessage *r = dbus_connection_send_with_reply_and_block(c, m, 15000, &e);
  dbus_message_unref(m);
  if (!r) {
    err = std::string(e.name ? e.name : "") + ": " + (e.message ? e.message : "");
    dbus_error_free(&e);
  }
  return r;
}

static std::string device_path(DBusConnection *c, const std::string &iface, std::string &err) {
  DBusMessage *m = dbus_message_new_method_call(NM, "/org/freedesktop/NetworkManager", NM, "GetDeviceByIpIface");
  const char *s = iface.c_str();
  dbus_message_append_args(m, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID);
  DBusMessage *r = call(c, m, err);
  if (!r) return "";
  const char *p = nullptr;
  dbus_message_get_args(r, nullptr, DBUS_TYPE_OBJECT_PATH, &p, DBUS_TYPE_INVALID);
  std::string out = p ? p : "";
  dbus_message_unref(r);
  return out;
}

// Stan aktywnego połączenia: 1 łączy, 2 aktywne, 3 rozłącza, 4 rozłączone; -1 zniknęło.
static int active_state(DBusConnection *c, const std::string &path) {
  DBusMessage *m = dbus_message_new_method_call(NM, path.c_str(), "org.freedesktop.DBus.Properties", "Get");
  const char *iface = "org.freedesktop.NetworkManager.Connection.Active", *prop = "State";
  dbus_message_append_args(m, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
  std::string err;
  DBusMessage *r = call(c, m, err);
  if (!r) return -1;
  DBusMessageIter it, v;
  int st = -1;
  if (dbus_message_iter_init(r, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT) {
    dbus_message_iter_recurse(&it, &v);
    if (dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_UINT32) {
      dbus_uint32_t u;
      dbus_message_iter_get_basic(&v, &u);
      st = (int)u;
    }
  }
  dbus_message_unref(r);
  return st;
}

static void delete_settings(DBusConnection *c, const std::string &path) {
  DBusMessage *m = dbus_message_new_method_call(NM, path.c_str(), "org.freedesktop.NetworkManager.Settings.Connection", "Delete");
  std::string err;
  if (DBusMessage *r = call(c, m, err)) dbus_message_unref(r);
}

struct Pending {
  std::string active, settings;
  Done done;
  int ticks = 0;
};
static void watch_activation(void *d) {
  auto *p = (Pending *)d;
  std::string err;
  DBusConnection *c = bus(err);
  int st = c ? active_state(c, p->active) : -1;
  if (st == 2) { auto done = p->done; delete p; if (done) done(true, "Połączono."); return; }
  if (st == 1 && ++p->ticks < 60) { Fl::repeat_timeout(0.75, watch_activation, d); return; }
  // Nie wyszło (zwykle złe hasło): usuń świeżo dodany profil, żeby nie zapamiętać błędnego hasła.
  if (c && !p->settings.empty()) delete_settings(c, p->settings);
  auto done = p->done;
  delete p;
  if (done) done(false, st == 1 ? "Przekroczono czas oczekiwania na połączenie."
                                 : "Nie udało się połączyć. Sprawdź hasło.");
}

// AddAndActivateConnection: profil Wi-Fi z hasłem przekazanym przez D-Bus.
static std::string add_and_activate(DBusConnection *c, const std::string &dev, const std::string &ssid,
                                    const std::string &security, const std::string &password, bool hidden,
                                    std::string &conn_path, std::string &active_path) {
  std::string err;
  std::string key_mgmt = security.find("WEP") != std::string::npos ? "none"
                       : security.find("WPA3") != std::string::npos && security.find("WPA2") == std::string::npos &&
                         security.find("WPA1") == std::string::npos ? "sae" : "wpa-psk";
  DBusMessage *m = dbus_message_new_method_call(NM, "/org/freedesktop/NetworkManager", NM, "AddAndActivateConnection");
  DBusMessageIter it, top;
  dbus_message_iter_init_append(m, &it);
  dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sa{sv}}", &top);
  section(&top, "connection", [&](DBusMessageIter *d) {
    put_var_str(d, "id", ssid);
    put_var_str(d, "type", "802-11-wireless");
  });
  section(&top, "802-11-wireless", [&](DBusMessageIter *d) {
    put_var_bytes(d, "ssid", ssid);
    if (hidden) put_var_bool(d, "hidden", true);
  });
  if (!password.empty()) {
    section(&top, "802-11-wireless-security", [&](DBusMessageIter *d) {
      put_var_str(d, "key-mgmt", key_mgmt);
      if (key_mgmt == "none") {   // WEP: 5/13 znaków albo 10/26 cyfr szesnastkowych
        put_var_str(d, "wep-key0", password);
        size_t n = password.size();
        put_var_u32(d, "wep-key-type", (n == 5 || n == 13 || n == 10 || n == 26) ? 1 : 2);
      } else {
        put_var_str(d, "psk", password);
      }
    });
  }
  dbus_message_iter_close_container(&it, &top);
  const char *devp = dev.c_str(), *spec = "/";
  dbus_message_append_args(m, DBUS_TYPE_OBJECT_PATH, &devp, DBUS_TYPE_OBJECT_PATH, &spec, DBUS_TYPE_INVALID);
  DBusMessage *r = call(c, m, err);
  if (!r) return err.empty() ? "D-Bus" : err;
  const char *conn = nullptr, *act = nullptr;
  dbus_message_get_args(r, nullptr, DBUS_TYPE_OBJECT_PATH, &conn, DBUS_TYPE_OBJECT_PATH, &act, DBUS_TYPE_INVALID);
  conn_path = conn ? conn : "";
  active_path = act ? act : "";
  dbus_message_unref(r);
  return "";
}

static void connect_with_password(const std::string &ssid, const std::string &security,
                                  const std::string &password, bool hidden, Done done) {
  // urządzenie Wi-Fi: to, które widzi sieć, albo pierwsze z brzegu
  State st = query();
  std::string iface;
  for (auto &n : st.networks) if (n.ssid == ssid) iface = n.device;
  if (iface.empty()) for (auto &d : st.devices) if (d.type == "wifi") { iface = d.iface; break; }
  if (iface.empty()) { done(false, "Brak karty Wi-Fi."); return; }
  // stary profil (np. z nieaktualnym hasłem) zastępujemy nowym
  for (auto &s : st.saved_wifi)
    if (s == ssid) { std::string o; run({"connection", "delete", "id", ssid}, o); }

  std::string err;
  DBusConnection *c = bus(err);
  if (!c) { done(false, "Brak połączenia z magistralą systemową D-Bus."); return; }
  std::string dev = device_path(c, iface, err);
  if (dev.empty()) { done(false, pl_error(err)); return; }

  std::string conn, act;
  err = add_and_activate(c, dev, ssid, security, password, hidden, conn, act);
  if (!err.empty()) { done(false, pl_error(err, true)); return; }
  auto *p = new Pending{act, conn, done};
  Fl::add_timeout(0.75, watch_activation, p);
}

void connect_wifi(const std::string &ssid, const std::string &security,
                  const std::string &password, bool hidden, Done done) {
  if (getenv("LISAOS_NMCLI") && !password.empty()) {
    // Tryb testowy z atrapą nmcli (bez prawdziwej karty Wi-Fi nie ma czego wołać przez
    // D-Bus). Tylko tu hasło idzie w argumentach - atrapa i tak jest lokalnym skryptem.
    run_async({"--wait", "45", "device", "wifi", "connect", ssid, "password", password},
              [done](int code, const std::string &out) { if (done) done(code == 0, code == 0 ? "Połączono." : pl_error(out, true)); });
    return;
  }
  if (!password.empty() || (hidden && !security.empty() && security != "--")) {
    connect_with_password(ssid, security, password, hidden, done);
    return;
  }
  std::vector<std::string> a = {"--wait", "45", "device", "wifi", "connect", ssid};
  if (hidden) a.insert(a.end(), {"hidden", "yes"});
  run_async(a, [done, ssid](int code, const std::string &out) {
    if (code == 0) { if (done) done(true, "Połączono."); return; }
    // nmcli zostawia profil po nieudanej próbie - sprzątamy, żeby kolejna zapytała o hasło
    if (strcasestr(out.c_str(), "Secrets were required")) {
      std::string o;
      run({"connection", "delete", "id", ssid}, o);
    }
    if (done) done(false, pl_error(out));
  });
}

// ---------------------------------------------------------------------------
// Obserwowanie zmian
// ---------------------------------------------------------------------------
static std::function<void()> g_changed;
static pid_t g_mon_pid = -1;
static void fire(void *) { if (g_changed) g_changed(); }
static void poke() { Fl::remove_timeout(fire); Fl::add_timeout(0.3, fire); }
static void start_monitor(void *);
static void mon_cb(int fd, void *) {
  char buf[1024];
  ssize_t n = read(fd, buf, sizeof buf);
  if (n > 0) { poke(); return; }
  if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
  Fl::remove_fd(fd);
  close(fd);
  if (g_mon_pid > 0) waitpid(g_mon_pid, nullptr, WNOHANG);
  g_mon_pid = -1;
  poke();
  Fl::add_timeout(5.0, start_monitor);   // nmcli monitor padł (np. restart NM) - spróbuj znów
}
static void start_monitor(void *) {
  int fd;
  g_mon_pid = start({"monitor"}, fd);
  if (g_mon_pid < 0) return;
  Fl::add_fd(fd, FL_READ, mon_cb);
}
static void periodic(void *) {   // siła sygnału nie wywołuje zdarzeń monitora
  poke();
  Fl::repeat_timeout(30.0, periodic);
}

void watch(std::function<void()> changed) {
  g_changed = std::move(changed);
  signal(SIGPIPE, SIG_IGN);
  start_monitor(nullptr);
  Fl::add_timeout(30.0, periodic);
}

}  // namespace net
