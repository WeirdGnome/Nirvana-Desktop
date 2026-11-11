// Nirvana Desktop - alice: the "Shader" panel
//
// A full-screen effect over the whole desktop (CRT, amber monitor, VHS...),
// drawn by nirvana-shader. Choosing an effect in the list applies it right away:
// the choice is saved in ~/.config/nirvana/shader.conf (read at the start of the
// session), a running nirvana-shader switches through the _NIRVANA_SHADER property
// on the root window, otherwise the panel starts it.
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/platform.H>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace plat;

namespace {

// The built-in shaders: file name (shaders/NAME.frag), name and description.
struct Known { const char *id, *name, *desc; };
const Known KNOWN[] = {
    {"crt", N_("CRT monitor"), N_("Curved glass, scanlines, a phosphor mask and dark corners.")},
    {"scanlines", N_("Scanlines"), N_("Only the dark lines of a tube screen; the picture stays flat and sharp.")},
    {"amber", N_("Amber monitor"), N_("A monochrome amber screen with a slight glow.")},
    {"green", N_("Green phosphor"), N_("A monochrome green terminal screen.")},
    {"grayscale", N_("Grayscale"), N_("No colors at all.")},
    {"sepia", N_("Sepia"), N_("The brown tint of an old photograph.")},
    {"nightlight", N_("Night light"), N_("Warmer colors with less blue light, for the evening.")},
    {"vhs", N_("VHS tape"), N_("Color bleeding, a shaky picture, noise and a rolling tracking band. Animated.")},
    {"filmgrain", N_("Film grain"), N_("Moving grain and a gentle flicker, like an old projector. Animated.")},
    {"pixelate", N_("Large pixels"), N_("The screen in 4×4 blocks.")},
    {"colors64", N_("64 colors"), N_("Four levels per color channel with ordered dithering, like an old graphics card.")},
    {"halftone", N_("Halftone"), N_("Newspaper print: black dots on paper, larger where the picture is darker.")},
    {"lcd", N_("LCD"), N_("A coarse liquid crystal panel with visible red, green and blue stripes.")},
    {"thermal", N_("Thermal camera"), N_("Brightness shown as heat: dark blue, violet, red, orange, white.")},
    {"dreamy", N_("Dreamy glow"), N_("A soft bloom around bright areas and a little extra color.")},
    {"underwater", N_("Underwater"), N_("A gently waving picture with a blue-green tint and moving light. Animated.")},
    {"invert", N_("Inverted colors"), N_("Light text on a dark background and the other way round.")},
};

struct Entry { std::string id, name, desc; };
std::vector<Entry> g_entries;   // [0] = none
Fl_Hold_Browser *g_list;
Fl_Box *g_desc, *g_status;
std::string g_status_text;

std::string home() { const char *h = getenv("HOME"); return h ? h : "/"; }

std::string config_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  return (x && *x ? std::string(x) : home() + "/.config") + "/nirvana/shader.conf";
}

std::string read_config() {
  std::ifstream in(config_file());
  std::string l;
  while (std::getline(in, l))
    if (!l.compare(0, 7, "shader=")) return l.substr(7);
  return "none";
}

void write_config(const std::string &id) {
  std::string f = config_file();
  std::string dir = f.substr(0, f.rfind('/'));
  mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
  mkdir(dir.c_str(), 0755);
  std::ofstream out(f);
  out << "# Nirvana Desktop - full-screen effect (Preferences > Shader)\nshader=" << id << "\n";
}

std::string exe_dir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return "";
  buf[n] = 0;
  std::string p = buf;
  return p.substr(0, p.rfind('/'));
}

std::string shader_exe() {
  std::string p = exe_dir() + "/nirvana-shader";
  if (!access(p.c_str(), X_OK)) return p;
  for (const char *d : {"/usr/bin/nirvana-shader", "/usr/local/bin/nirvana-shader"})
    if (!access(d, X_OK)) return d;
  return "";
}

std::vector<std::string> shader_dirs() {
  std::vector<std::string> d;
  const char *x = getenv("XDG_DATA_HOME");
  d.push_back((x && *x ? std::string(x) : home() + "/.local/share") + "/nirvana/shaders");
  std::string e = exe_dir();
  d.push_back(e + "/../share/nirvana/shaders");
  d.push_back(e + "/../shaders");
  d.push_back("/usr/local/share/nirvana/shaders");
  d.push_back("/usr/share/nirvana/shaders");
  return d;
}

// The first comment line of a shader file - the description of the user's own shaders.
std::string first_comment(const std::string &path) {
  std::ifstream in(path);
  std::string l;
  while (std::getline(in, l)) {
    if (l.rfind("//", 0) == 0) {
      l = l.substr(2);
      while (!l.empty() && l[0] == ' ') l.erase(0, 1);
      return l;
    }
    if (!l.empty()) break;
  }
  return "";
}

void collect() {
  g_entries.clear();
  g_entries.push_back({"none", _("None (off)"), _("The desktop as it is, without any effect.")});
  std::map<std::string, std::string> files;   // id -> path, the first directory wins
  for (auto &d : shader_dirs())
    if (DIR *dir = opendir(d.c_str())) {
      while (dirent *e = readdir(dir)) {
        std::string n = e->d_name;
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".frag") == 0 && !files.count(n.substr(0, n.size() - 5)))
          files[n.substr(0, n.size() - 5)] = d + "/" + n;
      }
      closedir(dir);
    }
  for (auto &k : KNOWN)
    if (files.count(k.id)) { g_entries.push_back({k.id, _(k.name), _(k.desc)}); files.erase(k.id); }
  for (auto &[id, path] : files) g_entries.push_back({id, id, first_comment(path)});   // the user's own
}

// a compositor is running (nirvana-shader or another one, e.g. picom)
Window cm_owner() {
  Display *d = fl_x11_display();
  char name[32];
  snprintf(name, sizeof name, "_NET_WM_CM_S%d", DefaultScreen(d));
  return XGetSelectionOwner(d, XInternAtom(d, name, False));
}

bool owner_is_ours(Window w) {
  Display *d = fl_x11_display();
  XClassHint ch;
  bool ours = false;
  if (XGetClassHint(d, w, &ch)) {
    ours = ch.res_name && !strcmp(ch.res_name, "nirvana-shader");
    if (ch.res_name) XFree(ch.res_name);
    if (ch.res_class) XFree(ch.res_class);
  }
  return ours;
}

void spawn(const std::string &exe, const std::string &arg) {
  pid_t p = fork();
  if (p == 0) {
    setsid();
    if (fork() == 0) {
      execl(exe.c_str(), "nirvana-shader", arg.c_str(), (char *)nullptr);
      _exit(127);
    }
    _exit(0);
  }
  if (p > 0) waitpid(p, nullptr, 0);
}

void set_status(const std::string &s) {
  g_status_text = s;
  g_status->label(g_status_text.c_str());
  g_status->redraw();
}

void apply(const std::string &id) {
  write_config(id);
  Display *d = fl_x11_display();
  Window owner = cm_owner();
  if (owner && !owner_is_ours(owner)) {
    set_status(_("Another compositor is running (e.g. picom), so the effect cannot be shown. "
                 "The choice is saved for the next session."));
    return;
  }
  if (owner) {   // nirvana-shader is running: switch it (or end it with "none")
    Atom a = XInternAtom(d, "_NIRVANA_SHADER", False);
    XChangeProperty(d, RootWindow(d, DefaultScreen(d)), a, XA_STRING, 8, PropModeReplace,
                    (const unsigned char *)id.data(), (int)id.size());
    XFlush(d);
  } else if (id != "none") {
    std::string exe = shader_exe();
    if (exe.empty()) {
      set_status(_("nirvana-shader is not installed (it needs OpenGL)."));
      return;
    }
    spawn(exe, id);
  }
  set_status(id == "none" ? "" : _("Effects use OpenGL. Without graphics acceleration animated effects can be slow."));
}

void list_cb(Fl_Widget *, void *) {
  int i = g_list->value() - 1;
  if (i < 0 || i >= (int)g_entries.size()) return;
  g_desc->copy_label(g_entries[i].desc.c_str());
  apply(g_entries[i].id);
}

}  // namespace

int panel_shader() {
  const int W = 520, H = 360;
  auto *win = new FrameWindow(W, H, _("Shader"));
  win->xclass("nirvana-shader-panel");
  auto *t1 = new Fl_Box(15, 8, 220, 20, _("Full-screen effect"));
  t1->labelfont(FONT_SYSTEM);
  t1->labelsize(SIZE_SYSTEM);
  t1->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_list = new Fl_Hold_Browser(15, 30, 210, H - 76);
  g_list->box(WELL_BOX);
  g_list->textfont(FONT_SMALL);
  g_list->textsize(SIZE_SMALL + 1);
  g_list->when(FL_WHEN_CHANGED);
  g_list->callback(list_cb);
  collect();
  std::string cur = read_config();
  int sel = 1;
  for (size_t i = 0; i < g_entries.size(); i++) {
    g_list->add(g_entries[i].name.c_str());
    if (g_entries[i].id == cur) sel = (int)i + 1;
  }
  g_list->value(sel);

  auto *t2 = new Fl_Box(240, 8, 260, 20, _("Description"));
  t2->labelfont(FONT_SYSTEM);
  t2->labelsize(SIZE_SYSTEM);
  t2->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_desc = new Fl_Box(240, 30, W - 255, 90);
  g_desc->labelsize(SIZE_SMALL + 1);
  g_desc->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  g_desc->copy_label(g_entries[sel - 1].desc.c_str());
  g_status = new Fl_Box(240, 130, W - 255, 90);
  g_status->labelsize(SIZE_SMALL);
  g_status->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  std::string own = std::string(_("Your own shaders (GLSL fragment shaders, *.frag) go to")) +
                    " ~/.local/share/nirvana/shaders.";
  auto *note = new Fl_Box(240, 230, W - 255, 60);
  note->copy_label(own.c_str());
  note->labelsize(SIZE_SMALL);
  note->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  auto *ok = new Fl_Button(W - 102, H - 36, 86, 24, _("OK"));
  ok->box(BUTTON_BOX);
  ok->down_box(BUTTON_DOWN);
  ok->labelfont(FONT_SYSTEM);
  ok->shortcut(FL_Enter);
  ok->callback([](Fl_Widget *w, void *) { w->window()->hide(); });
  win->end();
  win->finish();
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + win->oy(), (sh - H) / 4));
  win->show();
  return Fl::run();
}
