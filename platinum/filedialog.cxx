// Nirvana Desktop - libplatinum: the Nirvana file dialog
//
// A window in the style of the desktop: the places on the left (home, the user folders, the
// file system, mounted disks), the folder popup above the list (the folders up to the root,
// like the path menu of a 1990s file dialog), the list with icons, size and date, the name
// field when saving, the file type filter and the buttons.
#include "filedialog.h"
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Multi_Browser.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fnmatch.h>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#if defined(FLTK_USE_X11)
#include <X11/Xlib.h>
#endif

namespace plat {

namespace {

std::string home_dir() {
  const char *h = getenv("HOME");
  return h && *h ? h : "/";
}

std::string config_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  return (x && *x ? std::string(x) : home_dir() + "/.config") + "/nirvana/filedialog.conf";
}

std::string last_folder() {
  std::ifstream in(config_file());
  std::string l;
  while (std::getline(in, l))
    if (l.rfind("folder=", 0) == 0) return l.substr(7);
  return "";
}

void save_last_folder(const std::string &f) {
  std::string p = config_file();
  mkdir(p.substr(0, p.rfind('/')).c_str(), 0755);
  std::ofstream out(p);
  out << "# Nirvana Desktop - the file dialog\nfolder=" << f << "\n";
}

bool is_dir(const std::string &p) {
  struct stat st;
  return !stat(p.c_str(), &st) && S_ISDIR(st.st_mode);
}

std::string join(const std::string &dir, const std::string &name) {
  if (!name.empty() && name[0] == '/') return name;
  return dir == "/" ? "/" + name : dir + "/" + name;
}

std::string parent_of(const std::string &p) {
  if (p == "/" || p.empty()) return "/";
  size_t s = p.rfind('/');
  return s == 0 || s == std::string::npos ? "/" : p.substr(0, s);
}

std::string base_name(const std::string &p) {
  if (p == "/") return "/";
  size_t s = p.rfind('/');
  return s == std::string::npos ? p : p.substr(s + 1);
}

// "~/x", "x/../y" -> an absolute, tidy path
std::string normalize(std::string p, const std::string &cwd) {
  if (p.rfind("~", 0) == 0 && (p.size() == 1 || p[1] == '/')) p = home_dir() + p.substr(1);
  if (p.empty() || p[0] != '/') p = join(cwd, p);
  std::vector<std::string> parts;
  std::stringstream ss(p);
  std::string part;
  while (std::getline(ss, part, '/')) {
    if (part.empty() || part == ".") continue;
    if (part == "..") { if (!parts.empty()) parts.pop_back(); continue; }
    parts.push_back(part);
  }
  std::string out;
  for (auto &x : parts) out += "/" + x;
  return out.empty() ? "/" : out;
}

// XDG user folders (~/.config/user-dirs.dirs, localized by xdg-user-dirs)
std::map<std::string, std::string> user_dirs() {
  std::map<std::string, std::string> m;
  const char *x = getenv("XDG_CONFIG_HOME");
  std::ifstream in((x && *x ? std::string(x) : home_dir() + "/.config") + "/user-dirs.dirs");
  std::string l;
  while (std::getline(in, l)) {
    if (l.rfind("XDG_", 0) != 0) continue;
    size_t eq = l.find("_DIR=");
    if (eq == std::string::npos) continue;
    std::string k = l.substr(4, eq - 4), v = l.substr(eq + 5);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    if (v.rfind("$HOME", 0) == 0) v = home_dir() + v.substr(5);
    if (!v.empty() && v[0] == '/' && v != home_dir()) m[k] = v;
  }
  return m;
}

// MIME type -> globs, from the shared MIME database
const std::multimap<std::string, std::string> &mime_globs() {
  static std::multimap<std::string, std::string> m;
  static bool done = false;
  if (done) return m;
  done = true;
  std::vector<std::string> dirs = {home_dir() + "/.local/share"};
  const char *d = getenv("XDG_DATA_DIRS");
  std::stringstream ss(d && *d ? d : "/usr/local/share:/usr/share");
  std::string x;
  while (std::getline(ss, x, ':')) if (!x.empty()) dirs.push_back(x);
  for (auto &dir : dirs) {
    std::ifstream in(dir + "/mime/globs2");
    std::string l;
    while (std::getline(in, l)) {   // weight:type:glob[:flags]
      if (l.empty() || l[0] == '#') continue;
      size_t a = l.find(':'), b = a == std::string::npos ? a : l.find(':', a + 1);
      if (b == std::string::npos) continue;
      size_t c = l.find(':', b + 1);
      m.emplace(l.substr(a + 1, b - a - 1), l.substr(b + 1, c == std::string::npos ? std::string::npos : c - b - 1));
    }
  }
  return m;
}

// the globs of a filter (MIME types turned into globs)
std::vector<std::string> globs_of(const FileFilter &f) {
  std::vector<std::string> out;
  for (auto &p : f.patterns) {
    if (p.find('/') == std::string::npos) { out.push_back(p); continue; }
    if (p.size() > 2 && p.compare(p.size() - 2, 2, "/*") == 0) {   // image/*
      std::string pre = p.substr(0, p.size() - 1);
      for (auto &[t, g] : mime_globs()) if (t.rfind(pre, 0) == 0) out.push_back(g);
      if (pre == "text/") out.push_back("*.txt");
    } else {
      auto r = mime_globs().equal_range(p);
      for (auto i = r.first; i != r.second; ++i) out.push_back(i->second);
    }
  }
  return out;
}

bool is_all(const FileFilter &f) {
  for (auto &p : f.patterns) if (p == "*" || p == "*.*" || p == "application/octet-stream") return true;
  return false;
}

std::string size_text(off_t n) {
  char b[32];
  if (n < 1024) snprintf(b, sizeof b, "%lld B", (long long)n);
  else if (n < 1024 * 1024) snprintf(b, sizeof b, "%.0f KB", n / 1024.0);
  else if (n < 1024LL * 1024 * 1024) snprintf(b, sizeof b, "%.1f MB", n / (1024.0 * 1024));
  else snprintf(b, sizeof b, "%.1f GB", n / (1024.0 * 1024 * 1024));
  return b;
}

Icon icon_for(const std::string &name, bool dir, bool exec) {
  if (dir) return Icon::Folder;
  std::string l = name;
  std::transform(l.begin(), l.end(), l.begin(), ::tolower);
  auto ext = [&](std::initializer_list<const char *> es) {
    for (auto e : es) {
      size_t n = strlen(e);
      if (l.size() > n && l.compare(l.size() - n, n, e) == 0) return true;
    }
    return false;
  };
  if (is_image_file(l)) return Icon::Image;
  if (ext({".mp3", ".ogg", ".oga", ".flac", ".wav", ".opus", ".m4a", ".aac", ".mid", ".mod"})) return Icon::Music;
  if (ext({".appimage"}) || exec) return Icon::Application;
  if (ext({".iso", ".img"})) return Icon::Optical;
  return Icon::Document;
}

Fl_RGB_Image *icon16(Icon i) {
  static std::map<int, Fl_RGB_Image *> cache;
  int key = (int)i * 2 + icon_style();
  auto &p = cache[key];
  if (!p) p = icon_image(i, 16);
  return p;
}

struct Entry {
  std::string name;
  bool dir = false, exec = false;
  off_t size = 0;
  time_t mtime = 0;
};

class FileDialogWin;
std::set<FileDialogWin *> g_open;

class FileDialogWin : public FrameWindow {
public:
  FileDialogOptions o;
  std::function<void(const std::vector<std::string> &, int)> done;
  std::string cur;
  std::vector<Entry> entries;
  std::vector<std::string> place_paths, place_labels;
  std::vector<std::vector<std::string>> filter_globs;
  Fl_Menu_Button *path_btn = nullptr;
  Fl_Button *up_btn = nullptr, *newdir_btn = nullptr, *cancel_btn = nullptr, *accept_btn = nullptr;
  Fl_Hold_Browser *places = nullptr;
  Fl_Browser *list = nullptr;
  Fl_Input *name_in = nullptr;
  Fl_Choice *filter = nullptr;
  Fl_Check_Button *hidden = nullptr;
  Fl_Box *status = nullptr;
  std::string status_text, path_label;
  int col_w[4] = {0, 80, 130, 0};
  bool finished = false;

  static constexpr int W0 = 660, H0 = 440, SIDE = 150;

  FileDialogWin(const FileDialogOptions &opt)
      : FrameWindow(W0, H0, nullptr, false), o(opt) {
    if (o.title.empty())
      o.title = o.mode == FileDialogOptions::Save ? _("Save") : o.mode == FileDialogOptions::Folder ? _("Choose Folder") : _("Open");
    if (o.accept_label.empty())
      o.accept_label = o.mode == FileDialogOptions::Save ? _("Save") : o.mode == FileDialogOptions::Folder ? _("Choose") : _("Open");
    else   // GTK mnemonics: "_Open" -> "Open"
      o.accept_label.erase(std::remove(o.accept_label.begin(), o.accept_label.end(), '_'), o.accept_label.end());
    copy_label(o.title.c_str());
    xclass("nirvana-file-dialog");
    bool has_all = false;
    for (auto &f : o.filters) if (is_all(f)) has_all = true;
    if (!has_all) o.filters.push_back({_("All files"), {"*"}});
    for (auto &f : o.filters) filter_globs.push_back(is_all(f) ? std::vector<std::string>{"*"} : globs_of(f));
    if (o.current_filter < 0 || o.current_filter >= (int)o.filters.size()) o.current_filter = 0;

    const int W = W0, H = H0;
    // --- the places
    auto *pl = new Fl_Box(10, 10, SIDE, 18, _("Places"));
    pl->labelfont(FONT_SYSTEM);
    pl->labelsize(SIZE_SYSTEM);
    pl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    bool saving = o.mode == FileDialogOptions::Save;
    int bottom = saving ? 104 : 72;   // the room for the rows under the list
    places = new Fl_Hold_Browser(10, 32, SIDE, H - 32 - bottom - 6);
    places->box(WELL_BOX);
    places->textfont(FONT_SMALL);
    places->textsize(SIZE_SMALL + 1);
    places->format_char(0);
    places->callback([](Fl_Widget *w, void *d) {
      auto *self = (FileDialogWin *)d;
      int v = self->places->value();
      if (v > 0 && v <= (int)self->place_paths.size()) self->go(self->place_paths[v - 1]);
    }, this);

    // --- the folder popup, Up and New Folder
    int lx = 10 + SIDE + 10, lw = W - lx - 10;
    bool mk = o.mode != FileDialogOptions::Open;   // New Folder only when saving or choosing a folder
    int up_x = mk ? lx + lw - 114 - 8 - 90 : lx + lw - 90;
    path_btn = new Fl_Menu_Button(lx, 6, up_x - 8 - lx, 24);
    path_btn->box(BUTTON_BOX);
    path_btn->down_box(BUTTON_DOWN);
    path_btn->labelsize(SIZE_SMALL + 1);
    path_btn->labelfont(FONT_SYSTEM);
    path_btn->textsize(SIZE_SMALL + 1);
    path_btn->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    path_btn->tooltip(_("The folders above this one"));
    path_btn->callback([](Fl_Widget *w, void *d) {
      auto *self = (FileDialogWin *)d;
      const Fl_Menu_Item *m = ((Fl_Menu_Button *)w)->mvalue();
      if (m && m->user_data()) self->go(*(std::string *)m->user_data());
    }, this);
    up_btn = button(up_x, 6, 90, _("Up"));
    up_btn->tooltip(_("The folder above (Backspace)"));
    up_btn->callback([](Fl_Widget *, void *d) { auto *s = (FileDialogWin *)d; s->go(parent_of(s->cur)); }, this);
    newdir_btn = button(lx + lw - 114, 6, 114, _("New Folder"));
    newdir_btn->callback([](Fl_Widget *, void *d) { ((FileDialogWin *)d)->new_folder(); }, this);
    if (o.mode == FileDialogOptions::Open) newdir_btn->hide();

    // --- the list
    if (o.multiple && o.mode == FileDialogOptions::Open) list = new Fl_Multi_Browser(lx, 34, lw, H - 34 - bottom);
    else list = new Fl_Hold_Browser(lx, 34, lw, H - 34 - bottom);
    list->box(WELL_BOX);
    list->textfont(FONT_SMALL);
    list->textsize(SIZE_SMALL + 1);
    list->format_char(0);
    list->column_char('\t');
    list->column_widths(col_w);
    list->when(FL_WHEN_CHANGED | FL_WHEN_NOT_CHANGED);
    list->callback([](Fl_Widget *, void *d) { ((FileDialogWin *)d)->list_clicked(); }, this);

    int y = H - bottom + 6;
    status = new Fl_Box(lx, y, lw - 150, 18);
    status->labelsize(SIZE_SMALL);
    status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    hidden = new Fl_Check_Button(lx + lw - 146, y, 146, 18, _("Show hidden files"));
    hidden->labelsize(SIZE_SMALL);
    hidden->callback([](Fl_Widget *, void *d) { ((FileDialogWin *)d)->load(); }, this);
    y += 24;
    if (saving) {
      name_in = new Fl_Input(lx + 70, y, lw - 70, 22, _("Name:"));
      name_in->labelsize(SIZE_SMALL + 1);
      name_in->textsize(SIZE_SMALL + 1);
      name_in->value(o.name.c_str());
      y += 30;
    }
    filter = new Fl_Choice(lx + 70, y + 1, lw - 70 - 200, 22, _("Show:"));
    filter->labelsize(SIZE_SMALL + 1);
    filter->textsize(SIZE_SMALL + 1);
    for (auto &f : o.filters) {
      std::string n = f.name.empty() ? _("Files") : f.name;
      std::string esc;
      for (char c : n) { if (c == '/' || c == '\\' || c == '&') esc += '\\'; esc += c; }
      filter->add(esc.c_str());
    }
    filter->value(o.current_filter);
    filter->callback([](Fl_Widget *, void *d) { ((FileDialogWin *)d)->load(); }, this);
    if (o.mode == FileDialogOptions::Folder) filter->deactivate();
    cancel_btn = button(lx + lw - 192, y, 92, _("Cancel"));
    cancel_btn->shortcut(FL_Escape);
    cancel_btn->callback([](Fl_Widget *, void *d) { ((FileDialogWin *)d)->finish_with({}); }, this);
    accept_btn = button(lx + lw - 92, y, 92, nullptr);
    accept_btn->copy_label(o.accept_label.c_str());
    accept_btn->labelfont(FONT_SYSTEM);
    accept_btn->shortcut(FL_Enter);
    accept_btn->callback([](Fl_Widget *, void *d) { ((FileDialogWin *)d)->accept(); }, this);
    end();
    sizable(list);
    size_range(480, 320);
    finish();
    callback([](Fl_Widget *, void *d) { ((FileDialogWin *)d)->finish_with({}); }, this);

    fill_places();
    std::string start = o.folder;
    if (start.empty() || !is_dir(start)) start = last_folder();
    if (start.empty() || !is_dir(start)) start = home_dir();
    go(start);
    if (name_in) {
      name_in->take_focus();
      // select the name without the extension, ready to be typed over
      std::string n = o.name;
      size_t dot = n.rfind('.');
      name_in->insert_position(dot == std::string::npos || dot == 0 ? (int)n.size() : (int)dot, 0);
    } else {
      list->take_focus();
    }
    g_open.insert(this);
  }
  ~FileDialogWin() override { g_open.erase(this); }

  Fl_Button *button(int X, int Y, int W, const char *t) {
    auto *b = new Fl_Button(X, Y, W, 24, t);
    b->box(BUTTON_BOX);
    b->down_box(BUTTON_DOWN);
    b->labelsize(SIZE_SMALL + 1);
    return b;
  }

  void resize(int X, int Y, int W, int H) override {
    FrameWindow::resize(X, Y, W, H);
    if (list) {
      int rest = list->w() - 24 - col_w[1] - col_w[2];
      col_w[0] = std::max(120, rest);
      list->redraw();
    }
  }

  void fill_places() {
    places->clear();
    place_paths.clear();
    place_labels.clear();
    auto add = [&](const std::string &label, const std::string &path, Icon ic) {
      if (!is_dir(path)) return;
      for (auto &p : place_paths) if (p == path) return;
      places->add(label.c_str());
      places->icon(places->size(), icon16(ic));
      place_paths.push_back(path);
      place_labels.push_back(label);
    };
    add(_("Home"), home_dir(), Icon::Home);
    auto ud = user_dirs();
    auto named = [&](const char *key, const char *en, const char *label, Icon ic) {
      std::string p = ud.count(key) ? ud[key] : home_dir() + "/" + en;
      add(ud.count(key) ? base_name(p) : label, p, ic);
    };
    named("DESKTOP", "Desktop", _("Desktop"), Icon::Folder);
    named("DOCUMENTS", "Documents", _("Documents"), Icon::Folder);
    named("DOWNLOAD", "Downloads", _("Downloads"), Icon::Folder);
    named("PICTURES", "Pictures", _("Pictures"), Icon::Image);
    named("MUSIC", "Music", _("Music"), Icon::Music);
    named("VIDEOS", "Videos", _("Videos"), Icon::Folder);
    add(_("Applications"), home_dir() + "/.nirvana-apps", Icon::Applications);
    add(_("File system"), "/", Icon::HardDisk);
    // mounted disks and sticks
    std::ifstream m("/proc/mounts");
    std::string dev, dir, rest;
    while (m >> dev >> dir && std::getline(m, rest)) {
      if (dir.rfind("/media/", 0) != 0 && dir.rfind("/run/media/", 0) != 0 && dir.rfind("/mnt/", 0) != 0) continue;
      std::string d;   // \040 = space
      for (size_t i = 0; i < dir.size(); i++) {
        if (dir[i] == '\\' && i + 3 < dir.size()) { d += (char)strtol(dir.substr(i + 1, 3).c_str(), nullptr, 8); i += 3; }
        else d += dir[i];
      }
      add(base_name(d), d, Icon::Removable);
    }
  }

  void select_place() {
    places->deselect();
    for (size_t i = 0; i < place_paths.size(); i++)
      if (place_paths[i] == cur) places->select((int)i + 1);
  }

  void fill_path_menu() {
    static std::vector<std::unique_ptr<std::string>> keep;   // the paths behind the menu items
    path_btn->clear();
    auto label_of = [&](const std::string &p) {
      for (size_t i = 0; i < place_paths.size(); i++)
        if (place_paths[i] == p && i < place_labels.size() && place_paths[i].rfind("/media", 0) != 0) return place_labels[i];
      return base_name(p);
    };
    path_label = "  " + label_of(cur);
    path_btn->label(path_label.c_str());
    std::string p = cur;
    for (;;) {
      std::string name = label_of(p);
      std::string esc;
      for (char c : name) { if (c == '/' || c == '\\' || c == '&') esc += '\\'; esc += c; }
      if (!esc.empty() && esc[0] == '_') esc = " " + esc;
      keep.push_back(std::make_unique<std::string>(p));
      path_btn->add(esc.c_str(), 0, nullptr, keep.back().get());
      if (p == "/") break;
      p = parent_of(p);
    }
    path_btn->redraw();
  }

  void go(const std::string &dir) {
    std::string d = normalize(dir, cur.empty() ? home_dir() : cur);
    if (!is_dir(d)) return;
    std::string from = cur;
    cur = d;
    load();
    fill_path_menu();
    select_place();
    up_btn->activate();
    if (cur == "/") up_btn->deactivate();
    // coming up from a folder: it is chosen in the list
    if (!from.empty() && parent_of(from) == cur) {
      std::string child = base_name(from);
      for (size_t i = 0; i < entries.size(); i++)
        if (entries[i].name == child) { list->select((int)i + 1); list->middleline((int)i + 1); }
    }
  }

  bool matches(const Entry &e) {
    if (e.dir) return true;
    if (o.mode == FileDialogOptions::Folder) return false;
    int f = filter->value();
    if (f < 0 || f >= (int)filter_globs.size()) return true;
    for (auto &g : filter_globs[f])
      if (fnmatch(g.c_str(), e.name.c_str(), FNM_CASEFOLD) == 0) return true;
    return false;
  }

  void load() {
    entries.clear();
    list->clear();
    DIR *dp = opendir(cur.c_str());
    if (!dp) {
      status_text = fmt(_("This folder cannot be opened: %1"), strerror(errno));
      status->label(status_text.c_str());
      status->redraw();
      return;
    }
    bool show_hidden = hidden->value();
    while (dirent *de = readdir(dp)) {
      std::string n = de->d_name;
      if (n == "." || n == "..") continue;
      if (n[0] == '.' && !show_hidden) continue;
      Entry e;
      e.name = n;
      struct stat st;
      std::string full = join(cur, n);
      if (!stat(full.c_str(), &st)) {
        e.dir = S_ISDIR(st.st_mode);
        e.size = st.st_size;
        e.mtime = st.st_mtime;
        e.exec = !e.dir && (st.st_mode & S_IXUSR);
      }
      if (!matches(e)) continue;
      entries.push_back(e);
    }
    closedir(dp);
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
      if (a.dir != b.dir) return a.dir;
      return strcoll(a.name.c_str(), b.name.c_str()) < 0 ||
             (strcoll(a.name.c_str(), b.name.c_str()) == 0 && a.name < b.name);
    });
    int rest = list->w() - 24 - col_w[1] - col_w[2];
    col_w[0] = std::max(120, rest);
    for (auto &e : entries) {
      char date[32] = "";
      struct tm tm;
      if (e.mtime && localtime_r(&e.mtime, &tm)) strftime(date, sizeof date, "%Y-%m-%d %H:%M", &tm);
      std::string line = e.name + "\t" + (e.dir ? std::string("—") : size_text(e.size)) + "\t" + date;
      list->add(line.c_str());
      list->icon(list->size(), icon16(icon_for(e.name, e.dir, e.exec)));
    }
    status_text = items_text((int)entries.size());
    status->label(status_text.c_str());
    status->redraw();
    list->topline(1);
  }

  void list_clicked() {
    int v = list->value();
    if (v <= 0 || v > (int)entries.size()) return;
    const Entry &e = entries[v - 1];
    if (name_in && !e.dir) {
      name_in->value(e.name.c_str());
    }
    if (Fl::event_clicks() && (Fl::event() == FL_RELEASE || Fl::event() == FL_PUSH) && Fl::event_button() == FL_LEFT_MOUSE) {
      Fl::event_clicks(0);
      if (e.dir) go(join(cur, e.name));
      else if (o.mode != FileDialogOptions::Folder) accept();
    }
  }

  void new_folder() {
    const char *n = fl_input("%s", _("Folder name"), _("New Folder"));
    if (!n || !*n) return;
    std::string p = join(cur, n);
    if (mkdir(p.c_str(), 0755)) {
      guru_alert(fmt(_("The folder could not be created: %1"), strerror(errno)));
      return;
    }
    go(p);
  }

  void accept() {
    std::vector<std::string> out;
    if (o.mode == FileDialogOptions::Save) {
      std::string n = name_in->value();
      if (n.empty()) { name_in->take_focus(); return; }
      std::string p = normalize(n, cur);
      if (is_dir(p)) { go(p); name_in->value(""); return; }
      if (!is_dir(parent_of(p))) {
        guru_alert(fmt(_("The folder %1 does not exist."), parent_of(p)));
        return;
      }
      if (!access(p.c_str(), F_OK)) {
        int r = fl_choice("%s", _("Cancel"), _("Replace"), nullptr,
                          fmt(_("\"%1\" already exists. Replace it?"), base_name(p)).c_str());
        if (r != 1) return;
      }
      out.push_back(p);
      save_last_folder(parent_of(p));
    } else if (o.mode == FileDialogOptions::Folder) {
      int v = list->value();
      std::string p = cur;
      if (v > 0 && v <= (int)entries.size() && entries[v - 1].dir) p = join(cur, entries[v - 1].name);
      out.push_back(p);
      save_last_folder(cur);
    } else {
      std::vector<int> sel;
      for (int i = 1; i <= list->size(); i++) if (list->selected(i)) sel.push_back(i);
      if (sel.size() == 1 && entries[sel[0] - 1].dir) { go(join(cur, entries[sel[0] - 1].name)); return; }
      for (int i : sel) if (!entries[i - 1].dir) out.push_back(join(cur, entries[i - 1].name));
      if (out.empty()) return;
      save_last_folder(cur);
    }
    finish_with(out);
  }

  void finish_with(const std::vector<std::string> &paths) {
    if (finished) return;
    finished = true;
    hide();
    auto cb = done;
    int f = filter->value();
    Fl::delete_widget(this);
    if (cb) cb(paths, f);
  }

  int handle(int e) override {
    if (e == FL_KEYBOARD && Fl::event_key() == FL_BackSpace && Fl::focus() == list) {
      go(parent_of(cur));
      return 1;
    }
    // typing a path: "/" or "~" in the list jumps to the name field (or opens a small prompt)
    if (e == FL_KEYBOARD && Fl::focus() == list && (Fl::event_text()[0] == '/' || Fl::event_text()[0] == '~')) {
      const char *p = fl_input("%s", Fl::event_text(), _("Go to folder:"));
      if (p && *p) {
        std::string full = normalize(p, cur);
        if (is_dir(full)) go(full);
        else if (o.mode == FileDialogOptions::Open && !access(full.c_str(), R_OK)) finish_with({full});
      }
      return 1;
    }
    return FrameWindow::handle(e);
  }
};

}  // namespace

void show_file_dialog(const FileDialogOptions &o,
                      std::function<void(const std::vector<std::string> &, int)> done) {
  auto *w = new FileDialogWin(o);
  w->done = std::move(done);
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  w->place(sx + (sw - w->content_w()) / 2, sy + std::max(MENUBAR_H + 10 + w->oy(), (sh - w->content_h()) / 3));
  w->show();
#if defined(FLTK_USE_X11)
  if (o.parent_xid) {
    XSetTransientForHint(fl_display, fl_xid(w), (Window)o.parent_xid);
    XFlush(fl_display);
  }
#endif
}

std::vector<std::string> run_file_dialog(const FileDialogOptions &o, int *filter) {
  bool done = false;
  std::vector<std::string> result;
  int f = 0;
  show_file_dialog(o, [&](const std::vector<std::string> &paths, int fi) {
    result = paths;
    f = fi;
    done = true;
  });
  while (!done) Fl::wait();
  if (filter) *filter = f;
  return result;
}

std::string choose_file(const std::string &title, const std::vector<FileFilter> &filters, const std::string &folder) {
  FileDialogOptions o;
  o.title = title;
  o.filters = filters;
  o.folder = folder;
  auto r = run_file_dialog(o);
  return r.empty() ? "" : r[0];
}

}  // namespace plat
