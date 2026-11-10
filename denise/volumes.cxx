// Nirvana Desktop - denise: dyski, płyty i zaszyfrowane wolumeny (LUKS)
#include "volumes.h"
#include "i18n.h"

#include <dbus/dbus.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>

namespace vol {

// ---------------------------------------------------------------------------
// lsblk -P: linie KLUCZ="wartość" (znaki specjalne jako \xNN)
// ---------------------------------------------------------------------------
using Row = std::map<std::string, std::string>;

static std::string unescape(const std::string &s) {
  std::string r;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 3 < s.size() && s[i + 1] == 'x') {
      r += (char)std::stoi(s.substr(i + 2, 2), nullptr, 16);
      i += 3;
    } else {
      r += s[i];
    }
  }
  return r;
}

static Row parse_line(const std::string &line) {
  Row row;
  size_t i = 0;
  while (i < line.size()) {
    size_t eq = line.find("=\"", i);
    if (eq == std::string::npos) break;
    std::string key = line.substr(i, eq - i);
    size_t end = line.find('"', eq + 2);
    if (end == std::string::npos) break;
    row[key] = unescape(line.substr(eq + 2, end - eq - 2));
    i = end + 1;
    while (i < line.size() && line[i] == ' ') i++;
  }
  return row;
}

static std::vector<Row> lsblk() {
  std::vector<Row> rows;
  FILE *p = popen("lsblk -P -b -o NAME,KNAME,PATH,TYPE,FSTYPE,LABEL,SIZE,MOUNTPOINT,RM,HOTPLUG,TRAN,"
                  "PKNAME,UUID 2>/dev/null", "r");
  if (!p) return rows;
  char buf[4096];
  while (fgets(buf, sizeof buf, p)) {
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (!line.empty()) rows.push_back(parse_line(line));
  }
  pclose(p);
  return rows;
}

std::string human_size(uint64_t b) {
  const char *u[] = {"B", "KB", "MB", "GB", "TB"};
  double v = (double)b;
  int i = 0;
  while (v >= 1000 && i < 4) { v /= 1000; i++; }   // jak producenci dysków: 16 GB
  char buf[32];
  snprintf(buf, sizeof buf, v < 10 && i > 0 ? "%.1f %s" : "%.0f %s", v, u[i]);
  return plat::decimal(buf);
}

static bool user_mount(const std::string &mp) {
  return mp.rfind("/media/", 0) == 0 || mp.rfind("/run/media/", 0) == 0 || mp.rfind("/mnt", 0) == 0;
}

std::vector<Volume> scan() {
  std::vector<Row> rows = lsblk();
  std::map<std::string, Row *> by_kname;
  for (auto &r : rows) by_kname[r["KNAME"]] = &r;

  auto children = [&](const std::string &kname) {
    std::vector<Row *> v;
    for (auto &r : rows) if (r["PKNAME"] == kname) v.push_back(&r);
    return v;
  };
  // Czy urządzenie (albo coś "pod nim", np. odszyfrowany LUKS) należy do systemu?
  std::function<bool(const std::string &, int)> system_part = [&](const std::string &kname, int depth) {
    auto it = by_kname.find(kname);
    if (it == by_kname.end() || depth > 8) return false;
    const std::string &mp = (*it->second)["MOUNTPOINT"];
    if (!mp.empty() && !user_mount(mp)) return true;            // /, /boot, [SWAP], /snap/...
    for (Row *c : children(kname)) if (system_part((*c)["KNAME"], depth + 1)) return true;
    return false;
  };
  auto top_disk = [&](const std::string &kname) -> Row * {
    Row *r = by_kname.count(kname) ? by_kname[kname] : nullptr;
    for (int d = 0; r && d < 8; d++) {
      const std::string &t = (*r)["TYPE"];
      if (t == "disk" || t == "rom" || t == "loop") return r;
      if (!by_kname.count((*r)["PKNAME"])) break;
      r = by_kname[(*r)["PKNAME"]];
    }
    return r;
  };

  std::vector<Volume> out;
  for (auto &r : rows) {
    const std::string &type = r["TYPE"], &fs = r["FSTYPE"];
    if (type != "part" && type != "disk" && type != "rom" && type != "loop") continue;
    if (fs == "swap" || fs == "LVM2_member" || fs == "linux_raid_member") continue;
    if (type == "disk" && !children(r["KNAME"]).empty() && fs != "crypto_LUKS") continue;
    if (fs.empty()) {
      // bez systemu plików: pokazujemy tylko nośniki wymienne (do sformatowania)
      Row *d = top_disk(r["KNAME"]);
      bool rem = d && ((*d)["RM"] == "1" || (*d)["HOTPLUG"] == "1" || (*d)["TRAN"] == "usb" ||
                       (*d)["TRAN"] == "mmc" || (*d)["TRAN"] == "sdio");
      if (!rem || (*d)["TYPE"] == "rom" || strtoull(r["SIZE"].c_str(), nullptr, 10) == 0) continue;
    }
    if (system_part(r["KNAME"], 0)) continue;

    Volume v;
    v.dev = r["PATH"];
    v.kname = r["KNAME"];
    v.uuid = r["UUID"].empty() ? r["KNAME"] : r["UUID"];
    v.size = strtoull(r["SIZE"].c_str(), nullptr, 10);
    v.fstype = fs;
    v.mountpoint = r["MOUNTPOINT"];
    std::string label = r["LABEL"];
    Row *disk = top_disk(v.kname);
    if (disk) {
      v.disk_kname = (*disk)["KNAME"];
      v.optical = (*disk)["TYPE"] == "rom";
      const std::string &tran = (*disk)["TRAN"];
      v.removable = v.optical || (*disk)["RM"] == "1" || (*disk)["HOTPLUG"] == "1" || tran == "usb" ||
                    tran == "mmc" || tran == "sdio";
    }
    if (fs == "crypto_LUKS") {
      v.encrypted = true;
      for (Row *c : children(v.kname)) {
        if ((*c)["TYPE"] != "crypt") continue;
        v.unlocked = true;
        v.clear_kname = (*c)["KNAME"];
        v.fstype = (*c)["FSTYPE"];
        v.mountpoint = (*c)["MOUNTPOINT"];
        if (!(*c)["LABEL"].empty()) label = (*c)["LABEL"];
      }
    }
    // nazwa: etykieta albo opis z rozmiarem
    if (!label.empty()) v.name = label;
    else if (v.optical) v.name = _("Disc");
    else if (v.encrypted && !v.unlocked) v.name = plat::fmt(_("Encrypted disk %1"), human_size(v.size));
    else if (fs.empty()) { v.unformatted = true; v.name = plat::fmt(_("Unformatted %1"), human_size(v.size)); }
    else if (v.removable) v.name = plat::fmt(_("USB disk %1"), human_size(v.size));
    else v.name = plat::fmt(_("Disk %1"), human_size(v.size));
    if (v.encrypted && !v.unlocked) v.icon = plat::Icon::Encrypted;
    else if (v.optical) v.icon = plat::Icon::Optical;
    else if (v.removable) v.icon = plat::Icon::Removable;
    else v.icon = plat::Icon::HardDisk;
    out.push_back(v);
  }
  return out;
}

// ---------------------------------------------------------------------------
// UDisks2 przez D-Bus
// ---------------------------------------------------------------------------
static const char *UD = "org.freedesktop.UDisks2";

static DBusConnection *bus() {
  static DBusConnection *c = nullptr;
  if (!c) {
    DBusError err;
    dbus_error_init(&err);
    c = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    dbus_error_free(&err);
  }
  return c;
}

bool udisks_available() {
  DBusConnection *c = bus();
  if (!c) return false;
  DBusError err;
  dbus_error_init(&err);
  // uruchomiona albo do uruchomienia na żądanie (aktywacja D-Bus)
  bool ok = dbus_bus_name_has_owner(c, UD, &err);
  dbus_error_free(&err);
  if (!ok) {
    dbus_error_init(&err);
    dbus_uint32_t res = 0;
    ok = dbus_bus_start_service_by_name(c, UD, 0, &res, &err);
    dbus_error_free(&err);
  }
  return ok;
}

// Ścieżka obiektu: znaki inne niż [A-Za-z0-9] jako _xx (np. dm-0 -> dm_2d0).
static std::string block_path(const std::string &kname) {
  std::string p = "/org/freedesktop/UDisks2/block_devices/";
  for (unsigned char c : kname) {
    if (isalnum(c)) p += (char)c;
    else { char b[4]; snprintf(b, sizeof b, "_%02x", c); p += b; }
  }
  return p;
}

static void append_empty_options(DBusMessageIter *it) {
  DBusMessageIter dict;
  dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "{sv}", &dict);
  dbus_message_iter_close_container(it, &dict);
}

static std::string polish_error(const std::string &name, const std::string &msg) {
  if (name.find("NotAuthorized") != std::string::npos)
    return _("You are not allowed to do this.\n(A polkit agent or administrator rights are needed.)");
  if (name.find("DeviceBusy") != std::string::npos || msg.find("busy") != std::string::npos)
    return _("The disk is in use - close the files and programs that use it.");
  if (name.find("NotMounted") != std::string::npos) return "";   // już odmontowany - to nie błąd
  return msg;
}

// Wywołanie metody UDisks2; reply (opcjonalnie) dostaje odpowiedź do odczytu.
static std::string call(const std::string &path, const char *iface, const char *method,
                        const std::function<void(DBusMessageIter *)> &args, DBusMessage **reply,
                        int timeout_ms = 120000) {
  DBusConnection *c = bus();
  if (!c) return _("No connection to the system D-Bus.");
  DBusMessage *m = dbus_message_new_method_call(UD, path.c_str(), iface, method);
  DBusMessageIter it;
  dbus_message_iter_init_append(m, &it);
  if (args) args(&it);
  DBusError err;
  dbus_error_init(&err);
  DBusMessage *r = dbus_connection_send_with_reply_and_block(c, m, timeout_ms, &err);
  dbus_message_unref(m);
  if (!r) {
    std::string e = polish_error(err.name ? err.name : "", err.message ? err.message : _("unknown error"));
    dbus_error_free(&err);
    return e.empty() ? "" : e;
  }
  if (reply) *reply = r; else dbus_message_unref(r);
  return "";
}

static bool get_property(const std::string &path, const char *iface, const char *prop, DBusMessage **reply,
                         DBusMessageIter *value) {
  std::string e = call(path, "org.freedesktop.DBus.Properties", "Get", [&](DBusMessageIter *it) {
    dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &prop);
  }, reply, 10000);
  if (!e.empty() || !*reply) return false;
  DBusMessageIter top;
  dbus_message_iter_init(*reply, &top);
  if (dbus_message_iter_get_arg_type(&top) != DBUS_TYPE_VARIANT) return false;
  dbus_message_iter_recurse(&top, value);
  return true;
}

static std::string drive_of(const std::string &kname) {
  DBusMessage *r = nullptr;
  DBusMessageIter v;
  std::string drive;
  if (get_property(block_path(kname), "org.freedesktop.UDisks2.Block", "Drive", &r, &v) &&
      dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_OBJECT_PATH) {
    const char *s = nullptr;
    dbus_message_iter_get_basic(&v, &s);
    if (s && strcmp(s, "/") != 0) drive = s;
  }
  if (r) dbus_message_unref(r);
  return drive;
}

static bool drive_bool(const std::string &drive, const char *prop) {
  DBusMessage *r = nullptr;
  DBusMessageIter v;
  dbus_bool_t b = FALSE;
  if (get_property(drive, "org.freedesktop.UDisks2.Drive", prop, &r, &v) &&
      dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_BOOLEAN)
    dbus_message_iter_get_basic(&v, &b);
  if (r) dbus_message_unref(r);
  return b;
}

std::string mount(const Volume &v, std::string &mountpoint) {
  if (v.encrypted && !v.unlocked) return _("The disk is encrypted - enter its password first.");
  if (!v.mountpoint.empty()) { mountpoint = v.mountpoint; return ""; }
  DBusMessage *r = nullptr;
  std::string e = call(block_path(v.encrypted ? v.clear_kname : v.kname), "org.freedesktop.UDisks2.Filesystem",
                       "Mount", append_empty_options, &r);
  if (!e.empty()) return plat::fmt(_("Could not mount “%1”:"), v.name) + "\n" + e;
  const char *mp = nullptr;
  if (r && dbus_message_get_args(r, nullptr, DBUS_TYPE_STRING, &mp, DBUS_TYPE_INVALID) && mp) mountpoint = mp;
  if (r) dbus_message_unref(r);
  return "";
}

std::string unmount(const Volume &v) {
  if (v.mountpoint.empty()) return "";
  std::string e = call(block_path(v.encrypted ? v.clear_kname : v.kname), "org.freedesktop.UDisks2.Filesystem",
                       "Unmount", append_empty_options, nullptr);
  return e.empty() ? "" : plat::fmt(_("Could not unmount “%1”:"), v.name) + "\n" + e;
}

std::string unlock(const Volume &v, const std::string &pass) {
  if (!v.encrypted || v.unlocked) return "";
  std::string e = call(block_path(v.kname), "org.freedesktop.UDisks2.Encrypted", "Unlock",
                       [&](DBusMessageIter *it) {
                         const char *p = pass.c_str();
                         dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &p);
                         append_empty_options(it);
                       }, nullptr);
  if (e.empty()) return "";
  if (e.find("passphrase") != std::string::npos || e.find("Operation not permitted") != std::string::npos ||
      e.find("No key available") != std::string::npos)
    return _("Wrong password.");
  return _("Could not unlock the disk:") + std::string("\n") + e;
}

std::string lock(const Volume &v) {
  if (!v.encrypted || !v.unlocked) return "";
  std::string e = call(block_path(v.kname), "org.freedesktop.UDisks2.Encrypted", "Lock",
                       append_empty_options, nullptr);
  return e.empty() ? "" : _("Could not lock the encrypted disk:") + std::string("\n") + e;
}

std::string format(const Volume &v, const std::string &type, const std::string &label) {
  if (v.optical) return _("Discs cannot be formatted here.");
  std::string e;
  if (!v.mountpoint.empty() && !(e = unmount(v)).empty()) return e;
  if (v.encrypted && v.unlocked && !(e = lock(v)).empty()) return e;
  e = call(block_path(v.kname), "org.freedesktop.UDisks2.Block", "Format", [&](DBusMessageIter *it) {
    const char *t = type.c_str();
    dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &t);
    DBusMessageIter dict;
    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    auto put_s = [&](const char *k, const std::string &val) {
      DBusMessageIter en, var;
      dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &en);
      dbus_message_iter_append_basic(&en, DBUS_TYPE_STRING, &k);
      dbus_message_iter_open_container(&en, DBUS_TYPE_VARIANT, "s", &var);
      const char *c = val.c_str();
      dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &c);
      dbus_message_iter_close_container(&en, &var);
      dbus_message_iter_close_container(&dict, &en);
    };
    auto put_b = [&](const char *k, bool val) {
      DBusMessageIter en, var;
      dbus_bool_t b = val;
      dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &en);
      dbus_message_iter_append_basic(&en, DBUS_TYPE_STRING, &k);
      dbus_message_iter_open_container(&en, DBUS_TYPE_VARIANT, "b", &var);
      dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &b);
      dbus_message_iter_close_container(&en, &var);
      dbus_message_iter_close_container(&dict, &en);
    };
    if (!label.empty()) put_s("label", label);
    if (type == "ext4") put_b("take-ownership", true);   // właścicielem nowego systemu plików jest użytkownik
    dbus_message_iter_close_container(it, &dict);
  }, nullptr, 600000);
  return e.empty() ? "" : plat::fmt(_("Could not format “%1”:"), v.name) + "\n" + e;
}

std::string eject(const Volume &v) {
  if (std::string e = unmount(v); !e.empty()) return e;
  if (std::string e = lock(v); !e.empty()) return e;
  std::string drive = drive_of(v.disk_kname.empty() ? v.kname : v.disk_kname);
  if (drive.empty()) return "";   // np. obraz dysku (loop) - wystarczy odmontowanie
  std::string e;
  if (v.optical || drive_bool(drive, "Ejectable"))
    e = call(drive, "org.freedesktop.UDisks2.Drive", "Eject", append_empty_options, nullptr);
  if (e.empty() && v.removable && !v.optical && drive_bool(drive, "CanPowerOff"))
    e = call(drive, "org.freedesktop.UDisks2.Drive", "PowerOff", append_empty_options, nullptr);
  // Odmontowanie się udało, więc dysk można bezpiecznie wyjąć nawet bez wyłączenia.
  return "";
}

} // namespace vol
