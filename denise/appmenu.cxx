// Nirvana Desktop - Rambler: the menus of other programs in the menu bar (global menu)
//
// Rambler owns com.canonical.AppMenu.Registrar on the session bus. A program that finds it
// (Qt 5/6 by itself, GTK with appmenu-gtk-module, Electron, LibreOffice...) does not draw a
// menu bar in its window; it registers "window X id -> its com.canonical.dbusmenu object".
// When such a window is active, Rambler reads the menu tree (GetLayout), shows its top-level
// menus in the menu bar after Nirvana's own ones, and sends Event "clicked" back for an item.
// The program announces changes (LayoutUpdated, ItemsPropertiesUpdated) - the bar follows.
#include "appmenu.h"

#include <FL/Fl.H>
#include <dbus/dbus.h>

#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace appmenu {

namespace {

const char *REG_NAME = "com.canonical.AppMenu.Registrar";
const char *REG_PATH = "/com/canonical/AppMenu/Registrar";
const char *MENU_IFACE = "com.canonical.dbusmenu";

DBusConnection *g_conn = nullptr;
std::function<void()> g_changed;
struct Reg { std::string service, path; };
std::map<unsigned long, Reg> g_windows;   // X window -> its menu
unsigned long g_active = 0;
std::vector<Item> g_menus;
bool g_dirty = false;

const char *INTROSPECT =
    "<node><interface name='com.canonical.AppMenu.Registrar'>"
    "<method name='RegisterWindow'><arg name='windowId' type='u' direction='in'/>"
    "<arg name='menuObjectPath' type='o' direction='in'/></method>"
    "<method name='UnregisterWindow'><arg name='windowId' type='u' direction='in'/></method>"
    "<method name='GetMenuForWindow'><arg name='windowId' type='u' direction='in'/>"
    "<arg name='service' type='s' direction='out'/><arg name='menuObjectPath' type='o' direction='out'/></method>"
    "<method name='GetMenus'><arg name='menus' type='a(uso)' direction='out'/></method>"
    "<signal name='WindowRegistered'><arg type='u'/><arg type='s'/><arg type='o'/></signal>"
    "<signal name='WindowUnregistered'><arg type='u'/></signal>"
    "</interface></node>";

void flush() { if (g_conn) dbus_connection_flush(g_conn); }

// "_File" -> "File", "__" -> "_"
std::string strip_mnemonic(const std::string &s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '_') {
      if (i + 1 < s.size() && s[i + 1] == '_') { o += '_'; i++; }
      continue;
    }
    o += s[i];
  }
  return o;
}

// a variant's string / bool / int
bool variant_basic(DBusMessageIter *v, int type, void *out) {
  if (dbus_message_iter_get_arg_type(v) != DBUS_TYPE_VARIANT) return false;
  DBusMessageIter in;
  dbus_message_iter_recurse(v, &in);
  if (dbus_message_iter_get_arg_type(&in) != type) return false;
  dbus_message_iter_get_basic(&in, out);
  return true;
}

// "shortcut": aas - [["Control", "S"]]
std::string read_shortcut(DBusMessageIter *v) {
  if (dbus_message_iter_get_arg_type(v) != DBUS_TYPE_VARIANT) return "";
  DBusMessageIter arr;
  dbus_message_iter_recurse(v, &arr);
  if (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_ARRAY) return "";
  DBusMessageIter combo;
  dbus_message_iter_recurse(&arr, &combo);
  if (dbus_message_iter_get_arg_type(&combo) != DBUS_TYPE_ARRAY) return "";
  DBusMessageIter keys;
  dbus_message_iter_recurse(&combo, &keys);
  std::string out;
  while (dbus_message_iter_get_arg_type(&keys) == DBUS_TYPE_STRING) {
    const char *k;
    dbus_message_iter_get_basic(&keys, &k);
    std::string key = k;
    if (key == "Control") key = "Ctrl";
    else if (key == "Super") key = "Win";
    if (!out.empty()) out += "+";
    out += key;
    dbus_message_iter_next(&keys);
  }
  return out;
}

// (ia{sv}av) -> Item
void read_item(DBusMessageIter *st, Item &it) {
  DBusMessageIter f;
  dbus_message_iter_recurse(st, &f);
  if (dbus_message_iter_get_arg_type(&f) != DBUS_TYPE_INT32) return;
  dbus_int32_t id;
  dbus_message_iter_get_basic(&f, &id);
  it.id = id;
  dbus_message_iter_next(&f);
  if (dbus_message_iter_get_arg_type(&f) == DBUS_TYPE_ARRAY) {
    DBusMessageIter props;
    dbus_message_iter_recurse(&f, &props);
    while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
      DBusMessageIter kv;
      dbus_message_iter_recurse(&props, &kv);
      const char *key;
      dbus_message_iter_get_basic(&kv, &key);
      dbus_message_iter_next(&kv);
      std::string k = key;
      const char *s = nullptr;
      dbus_bool_t b;
      dbus_int32_t n;
      if (k == "label" && variant_basic(&kv, DBUS_TYPE_STRING, &s)) it.label = strip_mnemonic(s);
      else if (k == "enabled" && variant_basic(&kv, DBUS_TYPE_BOOLEAN, &b)) it.enabled = b;
      else if (k == "visible" && variant_basic(&kv, DBUS_TYPE_BOOLEAN, &b)) it.visible = b;
      else if (k == "type" && variant_basic(&kv, DBUS_TYPE_STRING, &s)) it.separator = !strcmp(s, "separator");
      else if (k == "toggle-type" && variant_basic(&kv, DBUS_TYPE_STRING, &s))
        it.toggle = !strcmp(s, "checkmark") ? 1 : !strcmp(s, "radio") ? 2 : 0;
      else if (k == "toggle-state" && variant_basic(&kv, DBUS_TYPE_INT32, &n)) it.checked = n == 1;
      else if (k == "shortcut") it.shortcut = read_shortcut(&kv);
      dbus_message_iter_next(&props);
    }
  }
  dbus_message_iter_next(&f);
  if (dbus_message_iter_get_arg_type(&f) == DBUS_TYPE_ARRAY) {
    DBusMessageIter kids;
    dbus_message_iter_recurse(&f, &kids);
    while (dbus_message_iter_get_arg_type(&kids) == DBUS_TYPE_VARIANT) {
      DBusMessageIter v;
      dbus_message_iter_recurse(&kids, &v);
      if (dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_STRUCT) {
        Item c;
        read_item(&v, c);
        if (c.visible) it.children.push_back(std::move(c));
      }
      dbus_message_iter_next(&kids);
    }
  }
}

DBusMessage *call(const Reg &r, const char *method, int timeout_ms) {
  return dbus_message_new_method_call(r.service.c_str(), r.path.c_str(), MENU_IFACE, method);
  (void)timeout_ms;
}

DBusMessage *send_block(DBusMessage *m, int timeout_ms) {
  DBusError e;
  dbus_error_init(&e);
  DBusMessage *rep = dbus_connection_send_with_reply_and_block(g_conn, m, timeout_ms, &e);
  dbus_message_unref(m);
  if (dbus_error_is_set(&e)) dbus_error_free(&e);
  return rep;
}

// GetLayout(0, -1, []) -> the whole tree
bool fetch(const Reg &r, Item &root) {
  DBusMessage *m = call(r, "GetLayout", 0);
  dbus_int32_t parent = 0, depth = -1;
  DBusMessageIter it, arr;
  dbus_message_iter_init_append(m, &it);
  dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &parent);
  dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &depth);
  dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);
  dbus_message_iter_close_container(&it, &arr);
  // a short wait: a program that does not answer must not stop the desktop
  DBusMessage *rep = send_block(m, 300);
  if (!rep) return false;
  bool ok = false;
  DBusMessageIter ri;
  if (dbus_message_iter_init(rep, &ri) && dbus_message_iter_get_arg_type(&ri) == DBUS_TYPE_UINT32) {
    dbus_message_iter_next(&ri);
    if (dbus_message_iter_get_arg_type(&ri) == DBUS_TYPE_STRUCT) {
      read_item(&ri, root);
      ok = true;
    }
  }
  dbus_message_unref(rep);
  return ok;
}

// Programs fill their menus when they are about to be shown: the top-level ones are asked first
void about_to_show(const Reg &r, const std::vector<int> &ids) {
  DBusMessage *m = call(r, "AboutToShowGroup", 0);
  DBusMessageIter it, arr;
  dbus_message_iter_init_append(m, &it);
  dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "i", &arr);
  for (int id : ids) { dbus_int32_t v = id; dbus_message_iter_append_basic(&arr, DBUS_TYPE_INT32, &v); }
  dbus_message_iter_close_container(&it, &arr);
  DBusMessage *rep = send_block(m, 300);
  if (rep && dbus_message_get_type(rep) != DBUS_MESSAGE_TYPE_ERROR) { dbus_message_unref(rep); return; }
  if (rep) dbus_message_unref(rep);
  for (int id : ids) {   // the older single call
    DBusMessage *s = call(r, "AboutToShow", 0);
    dbus_int32_t v = id;
    dbus_message_append_args(s, DBUS_TYPE_INT32, &v, DBUS_TYPE_INVALID);
    if (DBusMessage *rp = send_block(s, 200)) dbus_message_unref(rp);
  }
}

void reload() {
  g_dirty = false;
  std::vector<Item> menus;
  auto it = g_windows.find(g_active);
  if (it != g_windows.end()) {
    Item root;
    if (fetch(it->second, root)) {
      std::vector<int> ids;
      for (auto &c : root.children) ids.push_back(c.id);
      if (!ids.empty()) {
        about_to_show(it->second, ids);
        Item again;
        if (fetch(it->second, again)) root = std::move(again);
      }
      for (auto &c : root.children)
        if (!c.separator && !c.label.empty()) menus.push_back(std::move(c));
    }
  }
  g_menus = std::move(menus);
  if (g_changed) g_changed();
}

void reload_later(void *) {
  if (Fl::grab()) { Fl::repeat_timeout(0.3, reload_later); return; }   // a menu is open: after it
  reload();
}

void emit(const char *name, unsigned long xid, const Reg *r) {
  DBusMessage *s = dbus_message_new_signal(REG_PATH, REG_NAME, name);
  dbus_uint32_t w = (dbus_uint32_t)xid;
  if (r) {
    const char *svc = r->service.c_str(), *p = r->path.c_str();
    dbus_message_append_args(s, DBUS_TYPE_UINT32, &w, DBUS_TYPE_STRING, &svc, DBUS_TYPE_OBJECT_PATH, &p,
                             DBUS_TYPE_INVALID);
  } else {
    dbus_message_append_args(s, DBUS_TYPE_UINT32, &w, DBUS_TYPE_INVALID);
  }
  dbus_connection_send(g_conn, s, nullptr);
  dbus_message_unref(s);
}

DBusHandlerResult on_message(DBusConnection *c, DBusMessage *m, void *) {
  DBusMessage *r = nullptr;
  if (dbus_message_is_method_call(m, REG_NAME, "RegisterWindow")) {
    dbus_uint32_t w = 0;
    const char *path = nullptr;
    if (dbus_message_get_args(m, nullptr, DBUS_TYPE_UINT32, &w, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID)) {
      Reg reg{dbus_message_get_sender(m) ? dbus_message_get_sender(m) : "", path};
      g_windows[w] = reg;
      emit("WindowRegistered", w, &reg);
      if (w == g_active) Fl::add_timeout(0.05, reload_later);
    }
    r = dbus_message_new_method_return(m);
  } else if (dbus_message_is_method_call(m, REG_NAME, "UnregisterWindow")) {
    dbus_uint32_t w = 0;
    if (dbus_message_get_args(m, nullptr, DBUS_TYPE_UINT32, &w, DBUS_TYPE_INVALID)) {
      g_windows.erase(w);
      emit("WindowUnregistered", w, nullptr);
      if (w == g_active) Fl::add_timeout(0.05, reload_later);
    }
    r = dbus_message_new_method_return(m);
  } else if (dbus_message_is_method_call(m, REG_NAME, "GetMenuForWindow")) {
    dbus_uint32_t w = 0;
    dbus_message_get_args(m, nullptr, DBUS_TYPE_UINT32, &w, DBUS_TYPE_INVALID);
    auto it = g_windows.find(w);
    if (it == g_windows.end()) {
      r = dbus_message_new_error(m, "com.canonical.AppMenu.Registrar.Error", "no menu for this window");
    } else {
      r = dbus_message_new_method_return(m);
      const char *svc = it->second.service.c_str(), *p = it->second.path.c_str();
      dbus_message_append_args(r, DBUS_TYPE_STRING, &svc, DBUS_TYPE_OBJECT_PATH, &p, DBUS_TYPE_INVALID);
    }
  } else if (dbus_message_is_method_call(m, REG_NAME, "GetMenus")) {
    r = dbus_message_new_method_return(m);
    DBusMessageIter it, arr;
    dbus_message_iter_init_append(r, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(uso)", &arr);
    for (auto &[w, reg] : g_windows) {
      DBusMessageIter st;
      dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, nullptr, &st);
      dbus_uint32_t id = (dbus_uint32_t)w;
      const char *svc = reg.service.c_str(), *p = reg.path.c_str();
      dbus_message_iter_append_basic(&st, DBUS_TYPE_UINT32, &id);
      dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &svc);
      dbus_message_iter_append_basic(&st, DBUS_TYPE_OBJECT_PATH, &p);
      dbus_message_iter_close_container(&arr, &st);
    }
    dbus_message_iter_close_container(&it, &arr);
  } else if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
    r = dbus_message_new_method_return(m);
    dbus_message_append_args(r, DBUS_TYPE_STRING, &INTROSPECT, DBUS_TYPE_INVALID);
  } else {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  if (r) {
    dbus_connection_send(c, r, nullptr);
    dbus_message_unref(r);
  }
  flush();
  return DBUS_HANDLER_RESULT_HANDLED;
}

// signals: a program's menu changed; a program left the bus
DBusHandlerResult on_signal(DBusConnection *, DBusMessage *m, void *) {
  if (dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_SIGNAL) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  if (dbus_message_is_signal(m, "org.freedesktop.DBus", "NameOwnerChanged")) {
    const char *name, *old_owner, *new_owner;
    if (dbus_message_get_args(m, nullptr, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old_owner, DBUS_TYPE_STRING,
                              &new_owner, DBUS_TYPE_INVALID) &&
        !*new_owner && name[0] == ':') {
      bool active_gone = false;
      for (auto it = g_windows.begin(); it != g_windows.end();) {
        if (it->second.service == name) {
          if (it->first == g_active) active_gone = true;
          it = g_windows.erase(it);
        } else {
          ++it;
        }
      }
      if (active_gone) Fl::add_timeout(0.05, reload_later);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  const char *iface = dbus_message_get_interface(m);
  if (iface && !strcmp(iface, MENU_IFACE)) {
    auto it = g_windows.find(g_active);
    const char *sender = dbus_message_get_sender(m), *path = dbus_message_get_path(m);
    if (it != g_windows.end() && sender && path && it->second.service == sender && it->second.path == path &&
        !g_dirty) {
      g_dirty = true;
      Fl::add_timeout(0.15, reload_later);   // a series of changes: one reload
    }
  }
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void pump() {
  if (!g_conn) return;
  while (dbus_connection_dispatch(g_conn) == DBUS_DISPATCH_DATA_REMAINS) {}
}
void fd_cb(int fd, void *) {
  if (!dbus_connection_read_write(g_conn, 0)) { Fl::remove_fd(fd); return; }
  pump();
}

}  // namespace

void start(std::function<void()> changed) {
  g_changed = std::move(changed);
  if (const char *e = getenv("NIRVANA_GLOBAL_MENU"); e && !strcmp(e, "0")) return;
  DBusError err;
  dbus_error_init(&err);
  g_conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
  if (!g_conn) { dbus_error_free(&err); return; }
  dbus_connection_set_exit_on_disconnect(g_conn, FALSE);
  int r = dbus_bus_request_name(g_conn, REG_NAME, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
  if (dbus_error_is_set(&err)) dbus_error_free(&err);
  if (r != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER && r != DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER) return;   // another registrar
  static DBusObjectPathVTable vt = {nullptr, on_message, nullptr, nullptr, nullptr, nullptr};
  dbus_connection_register_object_path(g_conn, REG_PATH, &vt, nullptr);
  dbus_bus_add_match(g_conn, "type='signal',interface='com.canonical.dbusmenu'", nullptr);
  dbus_bus_add_match(g_conn, "type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged'", nullptr);
  dbus_connection_add_filter(g_conn, on_signal, nullptr, nullptr);
  int fd = -1;
  if (dbus_connection_get_unix_fd(g_conn, &fd)) Fl::add_fd(fd, FL_READ, fd_cb);
  flush();
  pump();
}

void set_active(unsigned long xid) {
  if (xid == g_active) return;
  g_active = xid;
  if (!g_conn) return;
  bool had = !g_menus.empty();
  if (!g_windows.count(xid)) {   // no menu: the bar at once without the old program's menus
    g_menus.clear();
    if (had && g_changed) g_changed();
    return;
  }
  reload();
}

const std::vector<Item> &menus() { return g_menus; }

void refresh() {
  if (g_conn && g_windows.count(g_active)) reload();
}

void activate(int id) {
  auto it = g_windows.find(g_active);
  if (it == g_windows.end() || !g_conn) return;
  DBusMessage *m = dbus_message_new_method_call(it->second.service.c_str(), it->second.path.c_str(), MENU_IFACE,
                                                "Event");
  dbus_int32_t i = id;
  const char *ev = "clicked";
  dbus_uint32_t ts = (dbus_uint32_t)time(nullptr);
  DBusMessageIter a, v;
  dbus_message_iter_init_append(m, &a);
  dbus_message_iter_append_basic(&a, DBUS_TYPE_INT32, &i);
  dbus_message_iter_append_basic(&a, DBUS_TYPE_STRING, &ev);
  dbus_message_iter_open_container(&a, DBUS_TYPE_VARIANT, "i", &v);
  dbus_int32_t zero = 0;
  dbus_message_iter_append_basic(&v, DBUS_TYPE_INT32, &zero);
  dbus_message_iter_close_container(&a, &v);
  dbus_message_iter_append_basic(&a, DBUS_TYPE_UINT32, &ts);
  dbus_message_set_no_reply(m, TRUE);
  dbus_connection_send(g_conn, m, nullptr);
  dbus_message_unref(m);
  flush();
}

}  // namespace appmenu
