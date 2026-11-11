// Nirvana Desktop - nirvana-portal: the file dialogs of other programs
//
// A backend of xdg-desktop-portal (D-Bus name org.freedesktop.impl.portal.desktop.nirvana,
// interface org.freedesktop.impl.portal.FileChooser). Programs that ask the portal for a file
// dialog - Firefox, Chromium, GTK with GTK_USE_PORTAL=1, Qt with the xdgdesktopportal theme,
// all Flatpaks - get the Nirvana file dialog instead of the GTK one.
//
// D-Bus starts it when needed (org.freedesktop.impl.portal.desktop.nirvana.service); it ends
// after a few idle minutes. The choice of backend: share/xdg-desktop-portal/nirvana-portals.conf.
#include "filedialog.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <gio/gio.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

using namespace plat;

namespace {

const char *IFACE_XML = R"(
<node>
  <interface name="org.freedesktop.impl.portal.FileChooser">
    <method name="OpenFile">
      <arg type="o" name="handle" direction="in"/>
      <arg type="s" name="app_id" direction="in"/>
      <arg type="s" name="parent_window" direction="in"/>
      <arg type="s" name="title" direction="in"/>
      <arg type="a{sv}" name="options" direction="in"/>
      <arg type="u" name="response" direction="out"/>
      <arg type="a{sv}" name="results" direction="out"/>
    </method>
    <method name="SaveFile">
      <arg type="o" name="handle" direction="in"/>
      <arg type="s" name="app_id" direction="in"/>
      <arg type="s" name="parent_window" direction="in"/>
      <arg type="s" name="title" direction="in"/>
      <arg type="a{sv}" name="options" direction="in"/>
      <arg type="u" name="response" direction="out"/>
      <arg type="a{sv}" name="results" direction="out"/>
    </method>
    <method name="SaveFiles">
      <arg type="o" name="handle" direction="in"/>
      <arg type="s" name="app_id" direction="in"/>
      <arg type="s" name="parent_window" direction="in"/>
      <arg type="s" name="title" direction="in"/>
      <arg type="a{sv}" name="options" direction="in"/>
      <arg type="u" name="response" direction="out"/>
      <arg type="a{sv}" name="results" direction="out"/>
    </method>
  </interface>
</node>)";

const char *REQUEST_XML = R"(
<node>
  <interface name="org.freedesktop.impl.portal.Request">
    <method name="Close"/>
  </interface>
</node>)";

GDBusConnection *g_bus;
GDBusNodeInfo *g_request_info;
int g_open = 0;
time_t g_idle_since = time(nullptr);
const int IDLE_EXIT = 300;   // seconds without a dialog

struct Pending {
  GDBusMethodInvocation *inv;
  guint request_id = 0;
  bool closed = false;
  std::string method;
  std::vector<std::string> save_files;   // SaveFiles: the names to put into the chosen folder
  GVariant *filters = nullptr;           // the filters as given (for current_filter in the answer)
};

std::string bytes_string(GVariant *v) {   // "ay", null-terminated
  gsize n = 0;
  const char *p = (const char *)g_variant_get_fixed_array(v, &n, 1);
  std::string s(p ? p : "", n);
  while (!s.empty() && s.back() == 0) s.pop_back();
  return s;
}

FileFilter read_filter(GVariant *f) {   // (sa(us))
  FileFilter out;
  const char *name;
  GVariantIter *it;
  g_variant_get(f, "(&sa(us))", &name, &it);
  out.name = name;
  guint32 kind;
  const char *pat;
  while (g_variant_iter_next(it, "(u&s)", &kind, &pat)) out.patterns.push_back(pat);
  g_variant_iter_free(it);
  return out;
}

void request_call(GDBusConnection *, const char *, const char *, const char *, const char *method, GVariant *,
                  GDBusMethodInvocation *inv, gpointer data) {
  auto *p = (Pending *)data;
  if (!strcmp(method, "Close")) {
    // the program gave up waiting: answer "cancelled"; the dialog stays until it is closed
    if (!p->closed) {
      p->closed = true;
      g_dbus_method_invocation_return_value(p->inv, g_variant_new("(u@a{sv})", 2u, g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)));
    }
    g_dbus_method_invocation_return_value(inv, nullptr);
  }
}

const GDBusInterfaceVTable REQUEST_VTABLE = {request_call, nullptr, nullptr, {}};

void answer(Pending *p, const std::vector<std::string> &paths, int filter) {
  g_open--;
  g_idle_since = time(nullptr);
  if (p->request_id) g_dbus_connection_unregister_object(g_bus, p->request_id);
  if (!p->closed) {
    GVariantBuilder res;
    g_variant_builder_init(&res, G_VARIANT_TYPE("a{sv}"));
    guint32 code = paths.empty() ? 1 : 0;   // 0 = done, 1 = cancelled
    if (!paths.empty()) {
      GVariantBuilder uris;
      g_variant_builder_init(&uris, G_VARIANT_TYPE("as"));
      std::vector<std::string> files = paths;
      if (p->method == "SaveFiles") {   // the chosen folder + each name
        files.clear();
        for (auto &n : p->save_files) files.push_back(paths[0] + (paths[0] == "/" ? "" : "/") + n);
      }
      for (auto &f : files) {
        gchar *uri = g_filename_to_uri(f.c_str(), nullptr, nullptr);
        if (uri) { g_variant_builder_add(&uris, "s", uri); g_free(uri); }
      }
      g_variant_builder_add(&res, "{sv}", "uris", g_variant_builder_end(&uris));
      if (p->filters && filter >= 0 && filter < (int)g_variant_n_children(p->filters))
        g_variant_builder_add(&res, "{sv}", "current_filter", g_variant_get_child_value(p->filters, filter));
    }
    g_dbus_method_invocation_return_value(p->inv, g_variant_new("(ua{sv})", code, &res));
  }
  if (p->filters) g_variant_unref(p->filters);
  delete p;
}

void portal_call(GDBusConnection *, const char *, const char *, const char *, const char *method, GVariant *params,
                 GDBusMethodInvocation *inv, gpointer) {
  const char *handle, *app_id, *parent, *title;
  GVariant *opts;
  g_variant_get(params, "(&o&s&s&s@a{sv})", &handle, &app_id, &parent, &title, &opts);

  FileDialogOptions o;
  o.title = title;
  if (!strncmp(parent, "x11:", 4)) o.parent_xid = strtoul(parent + 4, nullptr, 16);
  auto *p = new Pending;
  p->inv = inv;
  p->method = method;

  const char *s;
  gboolean b;
  if (g_variant_lookup(opts, "accept_label", "&s", &s)) o.accept_label = s;
  if (g_variant_lookup(opts, "multiple", "b", &b)) o.multiple = b;
  if (g_variant_lookup(opts, "directory", "b", &b) && b) o.mode = FileDialogOptions::Folder;
  if (!strcmp(method, "SaveFile")) o.mode = FileDialogOptions::Save;
  if (!strcmp(method, "SaveFiles")) o.mode = FileDialogOptions::Folder;
  if (g_variant_lookup(opts, "current_name", "&s", &s)) o.name = s;
  if (GVariant *v = g_variant_lookup_value(opts, "current_folder", G_VARIANT_TYPE_BYTESTRING)) {
    o.folder = bytes_string(v);
    g_variant_unref(v);
  }
  if (GVariant *v = g_variant_lookup_value(opts, "current_file", G_VARIANT_TYPE_BYTESTRING)) {
    std::string f = bytes_string(v);
    g_variant_unref(v);
    size_t sl = f.rfind('/');
    if (sl != std::string::npos) { o.folder = sl == 0 ? "/" : f.substr(0, sl); o.name = f.substr(sl + 1); }
  }
  if (GVariant *v = g_variant_lookup_value(opts, "files", G_VARIANT_TYPE("aay"))) {
    for (gsize i = 0; i < g_variant_n_children(v); i++) {
      GVariant *c = g_variant_get_child_value(v, i);
      std::string f = bytes_string(c);
      g_variant_unref(c);
      size_t sl = f.rfind('/');
      p->save_files.push_back(sl == std::string::npos ? f : f.substr(sl + 1));
    }
    g_variant_unref(v);
  }
  if (GVariant *v = g_variant_lookup_value(opts, "filters", G_VARIANT_TYPE("a(sa(us))"))) {
    p->filters = v;   // kept for the answer
    for (gsize i = 0; i < g_variant_n_children(v); i++) {
      GVariant *c = g_variant_get_child_value(v, i);
      o.filters.push_back(read_filter(c));
      g_variant_unref(c);
    }
  }
  if (GVariant *v = g_variant_lookup_value(opts, "current_filter", G_VARIANT_TYPE("(sa(us))"))) {
    FileFilter cf = read_filter(v);
    g_variant_unref(v);
    int found = -1;
    for (size_t i = 0; i < o.filters.size(); i++)
      if (o.filters[i].name == cf.name && o.filters[i].patterns == cf.patterns) found = (int)i;
    if (found < 0) {   // a filter that is not in the list: it goes first
      o.filters.insert(o.filters.begin(), cf);
      found = 0;
      if (p->filters) {   // keep the indexes of the answer in step
        GVariantBuilder bld;
        g_variant_builder_init(&bld, G_VARIANT_TYPE("a(sa(us))"));
        GVariant *cv = g_variant_lookup_value(opts, "current_filter", G_VARIANT_TYPE("(sa(us))"));
        g_variant_builder_add_value(&bld, cv);
        for (gsize i = 0; i < g_variant_n_children(p->filters); i++)
          g_variant_builder_add_value(&bld, g_variant_get_child_value(p->filters, i));
        g_variant_unref(p->filters);
        p->filters = g_variant_ref_sink(g_variant_builder_end(&bld));
        g_variant_unref(cv);
      }
    }
    o.current_filter = found;
  }
  g_variant_unref(opts);

  // the Request object: the program may close the dialog
  GError *err = nullptr;
  p->request_id = g_dbus_connection_register_object(g_bus, handle, g_request_info->interfaces[0], &REQUEST_VTABLE, p,
                                                    nullptr, &err);
  if (err) { g_error_free(err); p->request_id = 0; }

  g_open++;
  show_file_dialog(o, [p](const std::vector<std::string> &paths, int filter) { answer(p, paths, filter); });
}

const GDBusInterfaceVTable PORTAL_VTABLE = {portal_call, nullptr, nullptr, {}};

void glib_pump(void *) {
  while (g_main_context_iteration(nullptr, FALSE)) {}
  if (g_open == 0 && time(nullptr) - g_idle_since > IDLE_EXIT) exit(0);
  Fl::repeat_timeout(0.02, glib_pump);
}

bool g_lost = false;

}  // namespace

int main(int argc, char **argv) {
  init();
  Fl::visual(FL_RGB);
  bool replace = argc > 1 && !strcmp(argv[1], "--replace");

  GError *err = nullptr;
  g_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
  if (!g_bus) {
    fprintf(stderr, "nirvana-portal: no session bus: %s\n", err ? err->message : "?");
    return 1;
  }
  GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(IFACE_XML, nullptr);
  g_request_info = g_dbus_node_info_new_for_xml(REQUEST_XML, nullptr);
  g_dbus_connection_register_object(g_bus, "/org/freedesktop/portal/desktop", info->interfaces[0], &PORTAL_VTABLE,
                                    nullptr, nullptr, &err);
  if (err) {
    fprintf(stderr, "nirvana-portal: %s\n", err->message);
    return 1;
  }
  g_bus_own_name_on_connection(
      g_bus, "org.freedesktop.impl.portal.desktop.nirvana",
      replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT, nullptr,
      [](GDBusConnection *, const char *, gpointer) { g_lost = true; }, nullptr, nullptr);
  Fl::add_timeout(0.02, glib_pump);
  while (!g_lost) Fl::wait(1.0);
  return 0;
}
