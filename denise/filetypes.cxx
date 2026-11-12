// Nirvana Desktop - Rambler: file types and their actions (Special > File Types…)
//
// A file type: a name, file name patterns (*.zip *.tar.gz ...) and a list of actions. The
// first action is what a double click does; all of them are in the context menu. An action
// is a shell command with placeholders:
//   {f} the file (the first selected)      {F} all the selected files
//   {s} the folder of the files (source)   {d} the destination: the other explorer pane
//   {n} the file name without its ending
// or an action of Rambler: @browse (open an archive like a folder), @wallpaper (set as
// the desktop picture), and "@shell command" runs the command in Nirvana Shell (its output
// stays visible). Saved in ~/.config/nirvana/filetypes.conf; without it, built-in types.
#include "desktop.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multiline_Input.H>

#include <algorithm>
#include <filesystem>
#include <fnmatch.h>
#include <fstream>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;
using namespace plat;

namespace {

struct FileType {
  std::string name;
  std::vector<std::string> globs;
  std::vector<std::pair<std::string, std::string>> actions;   // label, command
};

std::vector<FileType> g_types;
bool g_loaded = false;

std::string conf_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/filetypes.conf";
}

std::vector<FileType> builtin() {
  const char *edit = "@shell ${EDITOR:-nano} {f}";
  return {
      {_("Archive"),
       {"*.zip", "*.tar", "*.tar.gz", "*.tgz", "*.tar.bz2", "*.tbz2", "*.tar.xz", "*.txz", "*.tar.zst", "*.7z",
        "*.rar", "*.cpio", "*.iso", "*.lha", "*.lzh", "*.cab", "*.jar"},
       {{_("Open as Folder"), "@browse"},
        {_("Extract Here"), "bsdtar -xf {f} -C {s}"},
        {_("Extract to the Other Pane"), "bsdtar -xf {f} -C {d}"},
        {_("List Contents"), "@shell bsdtar -tvf {f} | less"}}},
      {_("Shell Script"), {"*.sh"}, {{_("Run in Nirvana Shell"), "@shell sh {f}"}, {_("Edit"), edit}}},
      {_("Python Program"), {"*.py"}, {{_("Run in Nirvana Shell"), "@shell python3 {f}"}, {_("Edit"), edit}}},
      {_("Text"),
       {"*.txt", "*.md", "*.log", "*.conf", "*.cfg", "*.ini", "*.csv", "*.json", "*.xml", "*.yaml", "*.yml", "*.c",
        "*.cxx", "*.cpp", "*.h", "*.hpp", "*.desktop", "README*"},
       {{_("Open"), "xdg-open {f}"}, {_("Edit in Nirvana Shell"), edit}}},
      {_("Picture"),
       {"*.png", "*.jpg", "*.jpeg", "*.gif", "*.bmp", "*.webp", "*.svg", "*.tif", "*.tiff", "*.ico"},
       {{_("Open"), "xdg-open {f}"}, {_("Set as Wallpaper"), "@wallpaper"}}},
      {_("Debian Package"), {"*.deb"},
       {{_("Install"), "@shell pkexec apt-get install -y {f}"}, {_("List Contents"), "@shell dpkg -c {f} | less"}}},
  };
}

void load() {
  if (g_loaded) return;
  g_loaded = true;
  std::ifstream in(conf_file());
  if (!in) { g_types = builtin(); return; }
  std::string l;
  FileType *cur = nullptr;
  while (std::getline(in, l)) {
    if (l.empty() || l[0] == '#') continue;
    if (l[0] == '[' && l.back() == ']') {
      g_types.push_back({l.substr(1, l.size() - 2), {}, {}});
      cur = &g_types.back();
    } else if (cur && l.rfind("patterns=", 0) == 0) {
      std::istringstream s(l.substr(9));
      std::string g;
      while (s >> g) cur->globs.push_back(g);
    } else if (cur && l.rfind("action=", 0) == 0) {
      std::string a = l.substr(7);
      size_t bar = a.find('|');
      if (bar != std::string::npos) cur->actions.push_back({a.substr(0, bar), a.substr(bar + 1)});
    }
  }
}

void save() {
  std::string f = conf_file();
  std::error_code ec;
  fs::create_directories(fs::path(f).parent_path(), ec);
  std::ofstream out(f);
  out << "# Nirvana Desktop - file types (Special > File Types…)\n"
         "# action=label|command   placeholders: {f} file, {F} all selected, {s} its folder,\n"
         "# {d} the other explorer pane, {n} name without ending; @browse @wallpaper \"@shell cmd\"\n";
  for (auto &t : g_types) {
    out << "\n[" << t.name << "]\npatterns=";
    for (size_t i = 0; i < t.globs.size(); i++) out << (i ? " " : "") << t.globs[i];
    out << "\n";
    for (auto &a : t.actions) out << "action=" << a.first << "|" << a.second << "\n";
  }
}

const FileType *type_of(const std::string &name) {
  load();
  for (auto &t : g_types)
    for (auto &g : t.globs)
      if (fnmatch(g.c_str(), name.c_str(), FNM_CASEFOLD) == 0) return &t;
  return nullptr;
}

std::string quote(const std::string &s) {
  std::string o = "'";
  for (char c : s) o += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return o + "'";
}

}  // namespace

std::string ex_other_path(NWin *w);   // explorer.cxx

static void run_action(Desktop *d, const std::string &cmd, const std::vector<Entry> &files, NWin *w) {
  if (files.empty()) return;
  if (cmd == "@browse") { d->browse_archive(w, files[0].path); return; }
  if (cmd == "@wallpaper") { d->menu_target = w; d->set_as_wallpaper(); d->menu_target = nullptr; return; }
  std::string src = fs::path(files[0].path).parent_path().string();
  std::string dst = ex_other_path(w);
  if (dst.empty()) dst = src;
  std::string all;
  for (auto &e : files) all += (all.empty() ? "" : " ") + quote(e.path);
  std::string stem = fs::path(files[0].path).stem().string();
  bool shell = cmd.rfind("@shell ", 0) == 0;
  std::string c = shell ? cmd.substr(7) : cmd, out;
  for (size_t i = 0; i < c.size(); i++) {
    if (c[i] == '{' && i + 2 < c.size() && c[i + 2] == '}') {
      char k = c[i + 1];
      if (k == 'f') { out += quote(files[0].path); i += 2; continue; }
      if (k == 'F') { out += all; i += 2; continue; }
      if (k == 's') { out += quote(src); i += 2; continue; }
      if (k == 'd') { out += quote(dst); i += 2; continue; }
      if (k == 'n') { out += quote(stem); i += 2; continue; }
    }
    out += c[i];
  }
  if (shell)
    rambler_spawn({rambler_exe("nirvana-shell"), "--working-directory", src, "-e", "sh", "-c",
                   out + "; s=$?; echo; printf \"$1\" \"$s\"; read x", "sh", _("[exit code %s] Press Enter to close…")});
  else
    rambler_spawn({"sh", "-c", "cd -- \"$1\" && " + out, "sh", src});
}

bool Desktop::run_filetype(const Entry &e, NWin *from, int action) {
  const FileType *t = type_of(e.name);
  if (!t || action >= (int)t->actions.size()) return false;
  std::vector<Entry> files = {e};
  if (from && from->pane) {
    auto sel = from->pane->view()->selection();
    if (sel.size() > 1) files = sel;
  }
  run_action(this, t->actions[action].second, files, from);
  return true;
}

std::vector<std::pair<std::string, std::function<void()>>> Desktop::filetype_menu(const std::vector<Entry> &sel, NWin *w) {
  std::vector<std::pair<std::string, std::function<void()>>> v;
  if (sel.empty() || sel[0].dir) return v;
  const FileType *t = type_of(sel[0].name);
  if (!t) return v;
  for (size_t i = 0; i < t->actions.size(); i++) {
    std::string cmd = t->actions[i].second;
    std::string label;
    for (char c : t->actions[i].first) { if (c == '&') label += "&&"; else if (c == '/') label += "\\/"; else label += c; }
    v.push_back({label, [this, cmd, sel, w] { run_action(this, cmd, sel, w); }});
  }
  return v;
}

// ---------------------------------------------------------------------------
// The editor: Special > File Types…
// ---------------------------------------------------------------------------
namespace {
struct Editor {
  std::vector<FileType> types;
  int cur = -1;
  Fl_Hold_Browser *list;
  Fl_Input *name, *pats;
  Fl_Multiline_Input *acts;
  void store() {
    if (cur < 0 || cur >= (int)types.size()) return;
    auto &t = types[cur];
    t.name = name->value();
    t.globs.clear();
    std::istringstream s(pats->value());
    std::string g;
    while (s >> g) t.globs.push_back(g);
    t.actions.clear();
    std::istringstream a(acts->value());
    std::string l;
    while (std::getline(a, l)) {
      size_t eq = l.find('=');
      if (eq == std::string::npos) continue;
      std::string label = l.substr(0, eq), cmd = l.substr(eq + 1);
      auto trim = [](std::string x) {
        size_t p = x.find_first_not_of(" \t"), q = x.find_last_not_of(" \t");
        return p == std::string::npos ? std::string() : x.substr(p, q - p + 1);
      };
      label = trim(label);
      cmd = trim(cmd);
      if (!label.empty() && !cmd.empty()) t.actions.push_back({label, cmd});
    }
  }
  void show(int i) {
    cur = i;
    if (i < 0 || i >= (int)types.size()) { name->value(""); pats->value(""); acts->value(""); return; }
    auto &t = types[i];
    name->value(t.name.c_str());
    std::string p, a;
    for (auto &g : t.globs) p += (p.empty() ? "" : " ") + g;
    for (auto &x : t.actions) a += x.first + " = " + x.second + "\n";
    pats->value(p.c_str());
    acts->value(a.c_str());
  }
  void fill() {
    list->clear();
    for (auto &t : types) list->add(t.name.c_str());
    if (cur >= 0) list->value(cur + 1);
  }
};
Editor *g_ed;
}  // namespace

void Desktop::edit_filetypes() {
  load();
  int W = 660, H = 460, ox, oy;
  NWin *f = begin_form(_("File Types"), W, H, nullptr,
                       _("What a double click and the context menu do with each kind of file. The first action is the double click."),
                       ox, oy);
  static Editor ed;
  g_ed = &ed;
  ed.types = g_types;
  ed.cur = -1;
  ed.list = new Fl_Hold_Browser(ox + 16, oy + 60, 170, H - 140);
  ed.list->box(WELL_BOX);
  ed.list->textsize(SIZE_SMALL + 1);
  auto label = [&](int y, const char *t) {
    auto *b = new Fl_Box(ox + 196, oy + y, W - 212, 18, t);
    b->labelsize(SIZE_SMALL + 1);
    b->labelfont(FONT_SYSTEM);
    b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  };
  label(60, _("Name"));
  ed.name = new Fl_Input(ox + 196, oy + 78, W - 212, 22);
  label(104, _("Patterns (separated by spaces)"));
  ed.pats = new Fl_Input(ox + 196, oy + 122, W - 212, 22);
  label(148, _("Actions: one per line, \"label = command\""));
  ed.acts = new Fl_Multiline_Input(ox + 196, oy + 166, W - 212, 150);
  for (Fl_Input *i : {ed.name, ed.pats, (Fl_Input *)ed.acts}) { i->box(WELL_BOX); i->textsize(SIZE_SMALL + 1); }
  ed.acts->textfont(FL_COURIER);
  auto *help = new Fl_Box(ox + 196, oy + 320, W - 212, 76);
  std::string ht;   // "@" starts a symbol in FLTK labels - "@@" is the character itself
  for (char c : std::string(_("{f} the file  {F} all selected  {s} its folder  {d} the other pane  {n} name without ending\n"
                              "@browse open like a folder  @wallpaper  \"@shell command\" in Nirvana Shell")))
    ht += c == '@' ? std::string("@@") : std::string(1, c);
  help->copy_label(ht.c_str());
  help->labelsize(SIZE_SMALL);
  help->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  ed.list->callback([](Fl_Widget *, void *) {
    g_ed->store();
    g_ed->fill();
    int v = g_ed->list->value();
    g_ed->show(v - 1);
    g_ed->list->value(v);
  });
  auto *add = new Fl_Button(ox + 16, oy + H - 76, 82, 22, _("New"));
  auto *del = new Fl_Button(ox + 104, oy + H - 76, 82, 22, _("Remove"));
  for (Fl_Button *b : {add, del}) { b->box(BUTTON_BOX); b->labelsize(SIZE_SMALL + 1); }
  add->callback([](Fl_Widget *, void *) {
    g_ed->store();
    g_ed->types.push_back({_("New Type"), {"*.xyz"}, {{_("Open"), "xdg-open {f}"}}});
    g_ed->cur = (int)g_ed->types.size() - 1;
    g_ed->fill();
    g_ed->show(g_ed->cur);
  });
  del->callback([](Fl_Widget *, void *) {
    if (g_ed->cur < 0) return;
    g_ed->types.erase(g_ed->types.begin() + g_ed->cur);
    g_ed->cur = -1;
    g_ed->fill();
    g_ed->show(-1);
  });
  ed.fill();
  if (!ed.types.empty()) { ed.cur = 0; ed.list->value(1); ed.show(0); }
  end_form(f, W, H, ox, oy, {{_("Cancel"), nullptr}, {_("Built-in"), [] {
    g_types = builtin();
    save();
  }}, {_("Save"), [] {
    g_ed->store();
    g_types = g_ed->types;
    save();
  }}});
}
