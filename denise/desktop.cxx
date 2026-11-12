// Nirvana Desktop - denise: the desktop, the menu bar and the file manager windows
#include "desktop.h"
#include "jobs.h"
#include "sounds.h"
#include "applets.h"
#include "appimage.h"
#include "gary.h"
#include "tray.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Tooltip.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Multi_Label.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <map>
#include <tuple>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <fcntl.h>

namespace fs = std::filesystem;
using namespace plat;

// ---------------------------------------------------------------------------
// Starting programs (AppImage, xdg-open)
// ---------------------------------------------------------------------------
void rambler_spawn(const std::vector<std::string> &args);
static void spawn(const std::vector<std::string> &args) { rambler_spawn(args); }
void rambler_spawn(const std::vector<std::string> &args) {
  std::vector<const char *> argv;
  for (auto &a : args) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  appimage::spawn(argv.data());    // double fork - no zombie processes
}

static std::string self_exe() {
  std::error_code ec;
  return fs::read_symlink("/proc/self/exe", ec).string();
}
// a Nirvana Desktop program next to nirvana-denise (build/ or prefix), otherwise from PATH
std::string rambler_exe(const char *name) {
  std::string exe = self_exe();
  std::string p = exe.substr(0, exe.rfind('/') + 1) + name;
  return access(p.c_str(), X_OK) == 0 ? p : name;
}

// ---------------------------------------------------------------------------
// Middle of the bar: memory and disk usage, like the screen title bars of old
// home computers that showed the free memory.
// ---------------------------------------------------------------------------
class MemInfo : public Fl_Widget {
public:
  MemInfo(int X, int Y, int W, int H, int screen_w, std::string disk)
      : Fl_Widget(X, Y, W, H), screen_w_(screen_w), disk_(std::move(disk)) {
    sample();
    Fl::add_timeout(3.0, tick, this);
  }
  ~MemInfo() override { Fl::remove_timeout(tick, this); }

  static std::string group(unsigned long long v) {   // 1234567 -> "1 234 567"
    std::string d = std::to_string(v), r;
    for (size_t i = 0; i < d.size(); i++) {
      if (i && (d.size() - i) % 3 == 0) r += plat::language() == "pl" ? ' ' : ',';
      r += d[i];
    }
    return r;
  }
  static std::string gb(unsigned long long mb) {      // 47123 -> "46,0 GB"
    char b[32];
    snprintf(b, sizeof b, "%.1f GB", mb / 1024.0);
    if (plat::language() == "pl") for (char *c = b; *c; c++) if (*c == '.') *c = ',';
    return b;
  }

  // Below 1 GB in MB, above in GB (up to 10 GB with one decimal): "512MB", "3.2GB", "16GB"
  static std::string fmt1(double v) { char b[32]; snprintf(b, sizeof b, "%.1f", v); return b; }
  static std::string size_text(unsigned long long mb) {
    char b[32];
    if (mb < 1024) snprintf(b, sizeof b, "%lluMB", mb);
    else if (mb < 10 * 1024) snprintf(b, sizeof b, "%.1fGB", mb / 1024.0);
    else snprintf(b, sizeof b, "%.0fGB", mb / 1024.0);
    if (plat::language() == "pl") for (char *c = b; *c; c++) if (*c == '.') *c = ',';
    return b;
  }

  // Memory used by Nirvana Desktop itself: all nirvana-* processes of this user (window
  // manager, desktop, terminals, panels, shader). PSS - shared libraries counted in parts,
  // so the sum is the real cost; RSS when PSS cannot be read.
  static unsigned long long nirvana_kb(std::vector<std::pair<std::string, unsigned long long>> &parts) {
    unsigned long long sum = 0;
    std::map<std::string, unsigned long long> by_name;
    uid_t me = getuid();
    DIR *d = opendir("/proc");
    if (!d) return 0;
    while (dirent *e = readdir(d)) {
      if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
      std::string base = std::string("/proc/") + e->d_name;
      struct stat st;
      if (stat(base.c_str(), &st) || st.st_uid != me) continue;
      char comm[64] = "";
      if (FILE *f = fopen((base + "/comm").c_str(), "r")) {
        if (!fgets(comm, sizeof comm, f)) comm[0] = 0;
        fclose(f);
      }
      std::string name = comm;
      while (!name.empty() && name.back() == '\n') name.pop_back();
      if (name.rfind("nirvana-", 0) != 0) continue;
      unsigned long long kb = 0, v;
      if (FILE *f = fopen((base + "/smaps_rollup").c_str(), "r")) {
        char line[128];
        while (fgets(line, sizeof line, f))
          if (sscanf(line, "Pss: %llu", &v) == 1) { kb = v; break; }
        fclose(f);
      }
      if (!kb) {
        if (FILE *f = fopen((base + "/statm").c_str(), "r")) {
          unsigned long long size, res;
          if (fscanf(f, "%llu %llu", &size, &res) == 2) kb = res * (unsigned long long)sysconf(_SC_PAGESIZE) / 1024;
          fclose(f);
        }
      }
      sum += kb;
      by_name[name] += kb;
    }
    closedir(d);
    parts.assign(by_name.begin(), by_name.end());
    std::sort(parts.begin(), parts.end(), [](auto &a, auto &b) { return a.second > b.second; });
    return sum;
  }

  void sample() {
    unsigned long long mem_kb = 0, mem_total = 0;
    if (FILE *f = fopen("/proc/meminfo", "r")) {
      char line[128];
      unsigned long long v;
      while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "MemAvailable: %llu", &v) == 1) mem_kb = v;
        else if (sscanf(line, "MemTotal: %llu", &v) == 1) mem_total = v;
      }
      fclose(f);
    }
    unsigned long long disk_mb = 0, disk_total = 0;
    struct statvfs sv;
    bool disk_ok = statvfs(disk_.c_str(), &sv) == 0;
    if (disk_ok) {
      disk_mb = (unsigned long long)sv.f_bavail * sv.f_frsize / (1024 * 1024);
      disk_total = (unsigned long long)sv.f_blocks * sv.f_frsize / (1024 * 1024);
    }
    // used out of the total, e.g. "512MB used memory of 16GB"
    unsigned long long mem_used = mem_total > mem_kb ? (mem_total - mem_kb) / 1024 : 0;
    unsigned long long disk_used = 0;
    if (disk_ok) disk_used = (unsigned long long)(sv.f_blocks - sv.f_bfree) * sv.f_frsize / (1024 * 1024);
    std::string mu = size_text(mem_used), mt = size_text(mem_total / 1024);
    std::string du = size_text(disk_used), dt = size_text(disk_total);
    std::string full = disk_ok ? plat::fmt(_("%1 used memory of %2     %3 used disk of %4"), mu, mt, du, dt)
                               : plat::fmt(_("%1 used memory of %2"), mu, mt);
    std::string brief = disk_ok ? plat::fmt(_("memory %1/%2   disk %3/%4"), mu, mt, du, dt)
                                : plat::fmt(_("memory %1/%2"), mu, mt);
    std::vector<std::pair<std::string, unsigned long long>> parts;
    unsigned long long nk = nirvana_kb(parts);
    // the shader (OpenGL, often with Mesa's software renderer) can cost more than the rest
    // of Nirvana together - its part is shown separately
    unsigned long long shader_kb = 0;
    for (auto &[n, kb] : parts) if (n == "nirvana-shader") shader_kb = kb;
    if (nk) {
      std::string nm = size_text((nk + 512) / 1024);
      if (shader_kb) {
        std::string sm = size_text((shader_kb + 512) / 1024);
        full += "     " + plat::fmt(_("%1 used by Nirvana (shader %2)"), nm, sm);
        brief += "   " + plat::fmt(_("Nirvana %1 (shader %2)"), nm, sm);
      } else {
        full += "     " + plat::fmt(_("%1 used by Nirvana"), nm);
        brief += "   " + plat::fmt(_("Nirvana %1"), nm);
      }
    }
    std::string tip = plat::fmt(_("Memory: %1 free of %2"), group(mem_kb / 1024) + " MB", group(mem_total / 1024) + " MB");
    if (disk_ok) tip += "\n" + plat::fmt(_("Disk (%1): %2 free of %3"), disk_, gb(disk_mb), gb(disk_total));
    if (nk) {
      tip += "\n" + plat::fmt(_("Nirvana Desktop: %1"), plat::decimal(fmt1((nk) / 1024.0)) + " MB");
      for (auto &[n, kb] : parts) tip += "\n   " + n + "  " + plat::decimal(fmt1(kb / 1024.0)) + " MB";
    }
    if (full != full_ || brief != brief_) { full_ = full; brief_ = brief; redraw(); }
    if (tip != tip_) { tip_ = tip; tooltip(tip_.c_str()); }
  }
  static void tick(void *d) {
    ((MemInfo *)d)->sample();
    Fl::repeat_timeout(3.0, tick, d);
  }
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    draw_box(MENUBAR_BOX, GRAY);
    fl_font(FONT_SMALL, SIZE_SYSTEM);
    const std::string *t = &full_;
    int tw = (int)fl_width(t->c_str());
    if (tw > w() - 24) { t = &brief_; tw = (int)fl_width(t->c_str()); }
    if (tw <= w() - 24) {
      // centered on the screen, and when it does not fit there - centered in the free space
      int tx = (screen_w_ - tw) / 2;
      if (tx < x() + 12 || tx + tw > x() + w() - 12) tx = x() + (w() - tw) / 2;
      fl_color(TEXT);
      fl_draw(t->c_str(), tx, y() + 14);
    }
    fl_pop_clip();
  }
private:
  int screen_w_;
  std::string disk_, full_, brief_, tip_;
};

// ---------------------------------------------------------------------------
// "Rambler" application menu: the list of open folders and window operations.
// Eventually (nirvana-gary) it becomes the application switcher.
// ---------------------------------------------------------------------------
// A menu bar that refreshes the state of its items (active/inactive) before opening.
class NirvanaMenuBar : public Fl_Menu_Bar {
public:
  std::function<void()> before_open;
  NirvanaMenuBar(int X, int Y, int W, int H) : Fl_Menu_Bar(X, Y, W, H) {}
  int handle(int e) override {
    if ((e == FL_PUSH || e == FL_SHORTCUT || e == FL_KEYBOARD) && before_open) before_open();
    return Fl_Menu_Bar::handle(e);
  }
};

// Navigation bar of a folder window: back, forward, up and the location.
class NavBar : public Fl_Group {
public:
  NavBar(int X, int Y, int W, int H) : Fl_Group(X, Y, W, H) { box(FL_FLAT_BOX); color(GRAY); }
  void draw() override {
    Fl_Group::draw();
    fl_color(LIGHT);
    fl_xyline(x(), y(), x() + w() - 1);
    fl_color(SHADOW);
    fl_xyline(x(), y() + h() - 1, x() + w() - 1);
  }
};

static const int APP_W = 34;   // width of the application button (gary) in the right corner

class WindowsMenu : public Fl_Menu_Button {
public:
  std::function<void(WindowsMenu *)> on_open;
  // the active application: name and 16x16 icon (gary)
  std::function<void(std::string &, Fl_Image *&)> current;
  WindowsMenu(int X, int Y, int W, int H) : Fl_Menu_Button(X, Y, W, H) {
    box(FL_FLAT_BOX);   // FL_NO_BOX would turn the button into a right-click menu
    textfont(FONT_SYSTEM);
    textsize(SIZE_SYSTEM);
    down_box(FL_FLAT_BOX);
  }
  int handle(int e) override {
    if (e == FL_PUSH) {
      if (on_open) on_open(this);
      open_ = true; redraw(); Fl::flush();
      int r = Fl_Menu_Button::handle(e);
      open_ = false; redraw();
      return r;
    }
    if (e == FL_ENTER) {   // tooltip: the active application
      std::string name = "Rambler";
      Fl_Image *icon = nullptr;
      if (current) current(name, icon);
      copy_tooltip(plat::fmt(_("Applications - current: %1"), name).c_str());
    }
    return Fl_Menu_Button::handle(e);
  }
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    draw_box(MENUBAR_BOX, GRAY);
    fl_pop_clip();
    if (open_) {
      fl_color(selection_color());
      fl_rectf(x(), y(), w(), h() - 1);
    }
    // Instead of the application name: a depth gadget (two overlapping rectangles). The name is in the tooltip and the menu.
    draw_depth_gadget(x() + (w() - 18) / 2 + 1, y() + 3, open_);
    // separator on the left
    fl_color(SHADOW); fl_yxline(x(), y() + 3, y() + h() - 5);
    fl_color(LIGHT);  fl_yxline(x() + 1, y() + 3, y() + h() - 5);
  }
private:
  bool open_ = false;
};

// ---------------------------------------------------------------------------
// Network in the menu bar: a status icon (Wi-Fi with signal strength, wired,
// no connection), and a menu with the networks on click.
// ---------------------------------------------------------------------------
class NetMenu : public Fl_Menu_Button {
public:
  std::function<void(NetMenu *)> on_open;
  std::function<net::Summary()> summary;
  NetMenu(int X, int Y, int W, int H) : Fl_Menu_Button(X, Y, W, H) {
    box(FL_FLAT_BOX);
    down_box(FL_FLAT_BOX);
    textfont(FONT_SYSTEM);
    textsize(SIZE_SYSTEM);
  }
  int handle(int e) override {
    if (e == FL_PUSH) {
      if (on_open) on_open(this);
      open_ = true; redraw(); Fl::flush();
      int r = Fl_Menu_Button::handle(e);
      open_ = false; redraw();
      return r;
    }
    return Fl_Menu_Button::handle(e);
  }
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    draw_box(MENUBAR_BOX, GRAY);
    fl_pop_clip();
    if (open_) { fl_color(selection_color()); fl_rectf(x(), y(), w(), h() - 1); }
    Fl_Color on = open_ ? SEL_TEXT : TEXT;
    Fl_Color off = fl_color_average(on, open_ ? selection_color() : GRAY, 0.3f);
    int ix = x() + (w() - 16) / 2, iy = y() + 3;
    net::Summary s = summary ? summary() : net::Summary();
    if (!s.vpn.empty()) {   // active VPN: a padlock next to the icon
      ix -= 4;
      draw_lock(ix + 17, iy + 4, on);
    }
    if (s.kind == net::Summary::Wired) draw_wired(ix, iy, on);
    else if (s.kind == net::Summary::Wifi) draw_wifi(ix, iy, 1 + std::min(3, net::bars(s.signal)), on, off);
    else if (s.kind == net::Summary::Other) draw_wired(ix, iy, on);
    else if (s.connecting) draw_wifi(ix, iy, 1 + (int)(time(nullptr) % 4), on, off);
    else draw_wifi(ix, iy, 0, on, off);
  }
private:
  bool open_ = false;
};

// A Wi-Fi network menu item: name, padlock and signal bars on the right.
// Label text: digit 0-4 (signal), '0'/'1' (secured), then the SSID.
static const Fl_Labeltype NET_LABEL = FL_FREE_LABELTYPE;
static void net_label_draw(const Fl_Label *o, int X, int Y, int W, int H, Fl_Align) {
  const char *v = o->value;
  if (!v || strlen(v) < 2) return;
  int level = v[0] - '0';
  bool lock = v[1] == '1';
  fl_font(o->font, o->size);
  Fl_Color c = (Fl_Color)o->color;
  fl_color(c);
  fl_draw(ellipsize(v + 2, W - 44).c_str(), X, Y, W - 44, H, FL_ALIGN_LEFT);
  if (lock) draw_lock(X + W - 36, Y + (H - 9) / 2, c);
  draw_signal(X + W - 22, Y + (H - 10) / 2, level, c, fl_color_average(c, GRAY, 0.3f));
}
static void net_label_measure(const Fl_Label *o, int &W, int &H) {
  fl_font(o->font, o->size);
  W = (int)fl_width(o->value ? o->value + 2 : "") + 52;
  H = fl_height();
}

static std::string alice_exe();
static Entry entry_for(const std::string &path);
// The dock: a strip of tiles at the left and bottom screen edge,
// names in tooltips. A collapse tab on the left, a rounded end on the right.
#define DOCK_TILE (desk_set_.dock_size + 8)   // tile: the icon (Preferences > Desktop > Dock size) + frame
static const int DOCK_TAB = 14;    // collapse tab
static const int DOCK_END = 12;    // rounded end of the strip
static std::vector<net::Network> g_menu_nets;
static std::vector<net::Vpn> g_menu_vpns;
static std::vector<std::string> g_menu_ifaces;

// ---------------------------------------------------------------------------
// Desktop pattern: a two-tone tile (chosen in the Appearance panel)
// ---------------------------------------------------------------------------
static Fl_Tiled_Image *make_pattern() {
  int w, h;
  unsigned char *tile = pattern_tile(current_appearance().pattern, w, h);
  // Repeat the small tile up to 64x64 to draw fewer pieces.
  unsigned char *big = new unsigned char[64 * 64 * 3];
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++) memcpy(big + (y * 64 + x) * 3, tile + ((y % h) * w + x % w) * 3, 3);
  delete[] tile;
  auto *img = new Fl_RGB_Image(big, 64, 64, 3);
  img->alloc_array = 1;   // the image frees the tile
  return new Fl_Tiled_Image(img, 0, 0);
}

static Fl_RGB_Image *make_logo_image();
// First menu: the Nirvana Desktop logo and "Nirvana" (after a scheme change the logo gets a new background).
void Desktop::set_logo() {
  if (!logo_item_) return;
  Fl_RGB_Image *prev = logo_img_;
  logo_img_ = make_logo_image();
  logo_item_->image(logo_img_);       // registers the image label type in FLTK
  static Fl_Multi_Label *ml = new Fl_Multi_Label;   // lives as long as the menu bar
  ml->labela = (const char *)logo_img_;
  ml->typea = _FL_IMAGE_LABEL;
  ml->labelb = " Nirvana";
  ml->typeb = FL_NORMAL_LABEL;
  ml->label(logo_item_);
  delete prev;
}

static Fl_RGB_Image *make_logo_image() {
  // transparent around: the menu bar's own color (any scheme, also when the menu is open) shows
  if (Fl_RGB_Image *img = logo_image(16)) return img;
  Fl_Image_Surface surf(18, 18);
  Fl_Surface_Device::push_current(&surf);
  fl_color(GRAY);
  fl_rectf(0, 0, 18, 18);
  draw_logo(1, 1, 16);
  Fl_Surface_Device::pop_current();
  return surf.image();
}

// ---------------------------------------------------------------------------
// Menu items: Fl_Callback does not take a capturing lambda, so user_data
// carries a pointer to a constant item description (with a Desktop method).
// ---------------------------------------------------------------------------
struct MenuDef { const char *path; int shortcut; void (Desktop::*m)(); int flags; };
static const std::vector<MenuDef> *g_global_items = nullptr;   // the global bar's items (global shortcuts)

static void global_menu_cb(Fl_Widget *w, void *d) {
  auto *self = (Desktop *)w->window();
  auto *def = (const MenuDef *)d;
  if (def->m) (self->*(def->m))();
}

// Window and Icons menus in a file manager window - they act on that window.
static void window_menu_cb(Fl_Widget *w, void *d) {
  auto *nw = (NWin *)w->window();
  auto *def = (const MenuDef *)d;
  Desktop *desk = nw->desk;
  desk->menu_target = nw;
  if (def->m) (desk->*(def->m))();
  desk->menu_target = nullptr;
}

static const std::vector<MenuDef> &window_menu_defs() {
  static const std::vector<MenuDef> defs = {
    // --- Window: this file manager window ---
    {P_("Window/New Window"), FL_COMMAND + FL_SHIFT + 'n', &Desktop::new_window, 0},
    {P_("Window/New Folder"), FL_COMMAND + 'n', &Desktop::new_folder, FL_MENU_DIVIDER},
    {P_("Window/Back"), FL_ALT + FL_Left, &Desktop::go_back, 0},
    {P_("Window/Forward"), FL_ALT + FL_Right, &Desktop::go_forward, 0},
    {P_("Window/Open Parent"), FL_COMMAND + FL_Up, &Desktop::open_parent, FL_MENU_DIVIDER},
    {P_("Window/Connect to a Server…"), 0, &Desktop::connect_server, 0},
    {P_("Window/Find…"), FL_COMMAND + 'f', &Desktop::open_search, 0},
    {P_("Window/Jobs…"), 0, &Desktop::show_jobs, 0},
    {P_("Window/Open Nirvana Shell Here"), 0, &Desktop::open_terminal, 0},
    {P_("Window/Empty Trash…"), 0, &Desktop::empty_trash, FL_MENU_DIVIDER},
    {P_("Window/Close"), FL_COMMAND + 'w', &Desktop::close_active, 0},
    {P_("Window/Update"), FL_COMMAND + 'r', &Desktop::update_window, FL_MENU_DIVIDER},
    {P_("Window/Select Contents"), FL_COMMAND + 'a', &Desktop::select_all, 0},
    {P_("Window/Clean Up"), FL_COMMAND + '.', &Desktop::clean_up, FL_MENU_DIVIDER},
    {P_("Window/Show Hidden Files"), FL_COMMAND + 'h', &Desktop::toggle_hidden, FL_MENU_TOGGLE},
    {P_("Window/View as/Icons"), FL_COMMAND + '1', &Desktop::view_icons, FL_MENU_RADIO},
    {P_("Window/View as/List"), FL_COMMAND + '2', &Desktop::view_list, FL_MENU_RADIO},
    {P_("Window/Sort/by Name"), 0, &Desktop::sort_name, FL_MENU_RADIO},
    {P_("Window/Sort/by Date"), 0, &Desktop::sort_date, FL_MENU_RADIO},
    {P_("Window/Sort/by Size"), 0, &Desktop::sort_size, FL_MENU_RADIO},
    {P_("Window/Sort/by Kind"), 0, &Desktop::sort_kind, FL_MENU_RADIO},
    {P_("Window/Icon Size/Small"), 0, &Desktop::icons_small, FL_MENU_RADIO},
    {P_("Window/Icon Size/Normal"), 0, &Desktop::icons_normal, FL_MENU_RADIO},
    {P_("Window/Icon Size/Large"), 0, &Desktop::icons_large, FL_MENU_RADIO},
    // --- Icons: selected files and folders in this window ---
    {P_("Icons/Open"), FL_COMMAND + 'o', &Desktop::open_selected, 0},
    {P_("Icons/Copy"), FL_COMMAND + 'c', &Desktop::copy_selected, 0},
    {P_("Icons/Paste"), FL_COMMAND + 'v', &Desktop::paste, 0},
    {P_("Icons/Duplicate"), FL_COMMAND + 'd', &Desktop::duplicate, 0},
    {P_("Icons/Pack into an Archive…"), 0, &Desktop::pack_selected, 0},
    {P_("Icons/Rename…"), FL_F + 2, &Desktop::rename_selected, 0},
    {P_("Icons/Delete"), FL_COMMAND + FL_BackSpace, &Desktop::move_to_trash, FL_MENU_DIVIDER},
    {P_("Icons/Information…"), FL_COMMAND + 'i', &Desktop::get_info, 0},
    {P_("Icons/Set as Wallpaper"), 0, &Desktop::set_as_wallpaper, FL_MENU_DIVIDER},
    {P_("Icons/Snapshot"), FL_COMMAND + 's', &Desktop::snapshot, 0},
    {P_("Icons/Unsnapshot"), 0, &Desktop::unsnapshot, FL_MENU_DIVIDER},
    {P_("Icons/Put on Dock"), FL_COMMAND + 'l', &Desktop::dock_add, 0},
    {P_("Icons/Remove from Dock"), FL_COMMAND + 'k', &Desktop::dock_remove, 0},
  };
  return defs;
}

// ---------------------------------------------------------------------------
Desktop::Desktop(int W, int H, const std::string &home)
    : Fl_Double_Window(0, 0, W, H, "Nirvana Desktop"),
      home_(home), root_path_(home), spatial_(home) {
  fl_open_display();
  // Windows of other programs can disappear before we read their properties
  // (gary's application list) - BadWindow is a race then, not an error.
  static XErrorHandler fltk_handler = XSetErrorHandler([](Display *d, XErrorEvent *e) -> int {
    if (e->error_code == BadWindow || e->error_code == BadDrawable) return 0;
    return fltk_handler ? fltk_handler(d, e) : 0;
  });
  box(FL_NO_BOX);
  warm_small_icons();   // outside draw() - see appearance_changed()
  pattern_.reset(make_pattern());
  if (!current_appearance().wallpaper.empty() || current_appearance().background != "pattern") {
    bool ok = false;
    wallpaper_.reset(render_wallpaper(W, H, &ok));
    if (!ok && current_appearance().background == "pattern") wallpaper_.reset();
  }
  detect_wm();

  menu_ = new NirvanaMenuBar(0, 0, W, MENUBAR_H);
  ((NirvanaMenuBar *)menu_)->before_open = [this] { update_menu(); };
  menu_->box(MENUBAR_BOX);
  menu_->color(GRAY);
  menu_->textfont(FONT_SYSTEM);
  menu_->textsize(SIZE_SYSTEM);
  menu_->down_box(FL_FLAT_BOX);
  menu_->selection_color(SEL);

  // Global bar: Nirvana, System, Special. Every file manager window has
  // its own Window and Icons menus (window_menu_defs).
  static const std::vector<MenuDef> items = {
    // --- Nirvana: the whole system ---
    {P_("logo/About This Computer…"), 0, &Desktop::about, FL_MENU_DIVIDER},
    {P_("logo/Preferences…"), 0, &Desktop::open_preferences, 0},
    {P_("logo/Execute Command…"), FL_META + 'e', &Desktop::execute_command, 0},
    {P_("logo/Nirvana Shell"), FL_META + 't', &Desktop::open_terminal, 0},
    {P_("logo/Open System Log"), 0, &Desktop::open_log, FL_MENU_DIVIDER},
    {P_("logo/Reset Nirvana"), 0, &Desktop::reset_nirvana, 0},
    // --- System: session and computer (formerly in the power applet) ---
    {P_("System/Lock Screen"), 0, &Desktop::system_lock, 0},
    {P_("System/Sleep"), 0, &Desktop::system_sleep, 0},
    {P_("System/Hibernate"), 0, &Desktop::system_hibernate, FL_MENU_DIVIDER},
    {P_("System/Log Out…"), FL_META + 'q', &Desktop::system_logout, 0},
    {P_("System/Restart…"), 0, &Desktop::system_restart, 0},
    {P_("System/Shut Down…"), 0, &Desktop::system_shutdown, 0},
    // --- Special ---
    {P_("Special/Format Disk…"), 0, &Desktop::format_disk, 0},
    {P_("Special/Empty Trash…"), 0, &Desktop::empty_trash, 0},
    {P_("Special/File Types…"), 0, &Desktop::edit_filetypes, FL_MENU_DIVIDER},
    {P_("Special/Screenshot"), FL_Print, &Desktop::screenshot, FL_MENU_DIVIDER},
    {P_("Special/Force Quit a Window…"), FL_META + 'k', &Desktop::force_quit, 0},
  };
  for (auto &it : items) {
    menu_->add(plat::tr_path(it.path).c_str(), it.shortcut, global_menu_cb, (void *)&it, it.flags);
  }
  g_global_items = &items;
  update_global_keys();
  logo_item_ = (Fl_Menu_Item *)menu_->find_item("logo");
  set_logo();

  // Right side, from the edge: gary, clock, network, volume, power, tray -
  // tightly, without gaps. The menu bar ends after the last title; between it and
  // the applets there is the memory and disk usage. layout_bar() computes the positions.
  extras_ = applets::make_clock(MENUBAR_H, [this] { layout_bar(); });
  if (net::installed()) {
    auto *nm = new NetMenu(0, 0, 26, MENUBAR_H);
    net_menu_ = nm;
    nm->selection_color(SEL);
    nm->on_open = [this](NetMenu *m) { build_net_menu(m); };
    nm->summary = [this] { return net::summary(net_); };
  }
  bt_w_ = applets::make_bluetooth(0, 0, 22, MENUBAR_H);
  vol_w_ = applets::make_volume(0, 0, 26, MENUBAR_H);
  pow_w_ = applets::make_power(0, 0, 54, MENUBAR_H);
  if (applets::updates_supported()) upd_w_ = applets::make_updates(0, 0, 30, MENUBAR_H);
  prn_w_ = applets::make_printer(0, 0, 24, MENUBAR_H);
  clip_w_ = applets::make_clipboard(0, 0, 22, MENUBAR_H);
  bell_w_ = applets::make_notifications(0, 0, 24, MENUBAR_H);
  shield_w_ = applets::make_shield(0, 0, 22, MENUBAR_H);
  applets::clipboard_hotkey();
  tray_w_ = tray::make(0, 0, MENUBAR_H);
  {
    // width of the menu titles - computed the same way as in Fl_Menu_Bar::draw()
    int mx = 6;
    for (const Fl_Menu_Item *m = menu_->menu(); m && m->label(); m = m->next())
      if (m->visible()) mx += m->measure(nullptr, menu_) + 16;
    mx += 8;
    menu_->size(mx, MENUBAR_H);
    mem_w_ = new MemInfo(mx, 0, 1, MENUBAR_H, W, home_);
  }
  layout_bar();
  // Context menus of the desktop and windows: a hidden menu button as the "owner" (looks
  // like the menu bar). Not the menu bar itself - FLTK treats menu bar menus differently.
  ctx_style_ = new Fl_Menu_Button(0, 0, 1, 1);
  ctx_style_->textfont(FONT_SYSTEM);
  ctx_style_->textsize(SIZE_SYSTEM);
  ctx_style_->color(GRAY);
  ctx_style_->down_box(FL_FLAT_BOX);
  ctx_style_->selection_color(SEL);
  ctx_style_->hide();
  auto *wm = new WindowsMenu(W - APP_W, 0, APP_W, MENUBAR_H);
  app_menu_ = wm;
  wm->selection_color(menu_->selection_color());
  wm->on_open = [this](WindowsMenu *m) { build_windows_menu(m); };
  wm->current = [this](std::string &name, Fl_Image *&icon) { current_app(name, icon); };
  end();

  // Escape must not close the desktop
  callback([](Fl_Widget *w, void *) {
    if (Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape) return;
    ((Desktop *)w)->quit();
  });

  // hidden files: the remembered choice (Window > Show Hidden Files)
  show_hidden_ = false;
  {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::ifstream in((x && *x ? std::string(x) : home_ + "/.config") + "/nirvana/rambler.conf");
    std::string line;
    while (std::getline(in, line)) {
      if (line == "show_hidden=1") show_hidden_ = true;
      if (line == "dock_collapsed=1") dock_collapsed_ = true;
    }
  }
  load_dock();
  watch_desktop_dir();   // (creates ~/Pulpit when there is none)
  layout_icons();
  watch_trash();
  watch_volumes();
  {
    applets::Host h;
    h.dialog = [this](const std::string &t, const std::string &x, applets::Buttons b) {
      std::vector<std::pair<std::string, std::function<void()>>> bb(b.begin(), b.end());
      dialog(t, x, bb);
    };
    h.logout = [this] {
      if (!csd_mode) { quit(); return; }   // Nirvana Desktop session: the end of Rambler ends the session
      // another session (GNOME etc.): ask it to log out
      spawn({"sh", "-c", "gnome-session-quit --logout --no-prompt 2>/dev/null || "
                         "loginctl terminate-session \"$XDG_SESSION_ID\""});
    };
    h.before_shutdown = [this] { for (auto *w : wins_) if (w->pane) remember(w, true); };
    h.open_power_panel = [this] { spawn({alice_exe(), "power"}); };
    h.open_panel = [this](const char *p) { spawn({alice_exe(), p}); };
    h.relayout = [this] { layout_bar(); redraw(); };
    h.nirvana_session = !csd_mode;
    applets::init(h);
  }
  tray_pending_ = true;   // the tray starts after the window is shown (embedding needs an X window)
  if (net_menu_) {
    Fl::set_labeltype(NET_LABEL, net_label_draw, net_label_measure);
    net_ = net::query();
    net::watch([this] { net_refresh(); });
  }
}

// Right side of the menu bar, from the screen edge towards the middle.
void Desktop::layout_bar() {
  int rx = w() - APP_W;
  auto put = [&](Fl_Widget *wd, int ww) {
    if (!wd) return;
    rx -= ww;
    wd->resize(rx, 0, ww, MENUBAR_H);
  };
  put(extras_, extras_->w());
  put(bell_w_, 24);
  put(shield_w_, 22);
  put(net_menu_, 24);
  put(bt_w_, applets::bluetooth_width());
  put(vol_w_, 24);
  put(pow_w_, applets::power_width());
  put(upd_w_, applets::updates_width());
  put(prn_w_, applets::printer_width());
  put(clip_w_, 22);
  int tw = tray::width();
  if (tray_w_) { put(tray_w_, tw); if (tw) tray_w_->show(); else tray_w_->hide(); }
  int mx = menu_->w();
  if (mem_w_) {
    if (rx > mx) { mem_w_->resize(mx, 0, rx - mx, MENUBAR_H); mem_w_->show(); }
    else mem_w_->hide();
  }
  damage(FL_DAMAGE_ALL, 0, 0, w(), MENUBAR_H);
}

// The Trash can change from outside (another program, another window) - full/empty icon.
void Desktop::watch_trash() {
  std::error_code ec;
  fs::create_directories(trash_dir(), ec);
  int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd < 0) return;
  if (inotify_add_watch(fd, trash_dir().c_str(), IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO) < 0) {
    ::close(fd);
    return;
  }
  Fl::add_fd(fd, FL_READ, [](int f, void *data) {
    char buf[4096];
    while (read(f, buf, sizeof buf) > 0) {}
    auto *self = (Desktop *)data;
    bool full = self->trash_full();
    for (auto &ic : self->icons_)
      if (ic.path == self->trash_dir()) {
        Icon want = full ? Icon::TrashFull : Icon::Trash;
        if (ic.icon != want) { ic.icon = want; self->redraw(); }
      }
    self->ex_trash_changed(full);
  }, this);
}

std::string Desktop::trash_dir() const { return home_ + "/.local/share/Trash/files"; }

bool Desktop::trash_full() const {
  std::error_code ec;
  return fs::exists(trash_dir(), ec) && !fs::is_empty(trash_dir(), ec);
}

// Iconified windows (_NIRVANA_ICONS from Insight), in the order they were iconified.
static std::vector<Window> iconic_windows() {
  std::vector<Window> v;
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  if (XGetWindowProperty(fl_display, DefaultRootWindow(fl_display), XInternAtom(fl_display, "_NIRVANA_ICONS", False),
                         0, 1024, False, XA_WINDOW, &type, &fmt, &n, &after, &data) == Success && data)
    for (unsigned long i = 0; i < n; i++) v.push_back((Window)((unsigned long *)data)[i]);
  if (data) XFree(data);
  return v;
}

static bool is_ours(Window w);

// Nirvana's own programs set no window icon - their minimized windows by their class: the
// Preferences panels get the Preferences picture (not the generic program diamond)
static Icon nirvana_icon(Window w) {
  XClassHint ch{};
  std::string cls;
  if (XGetClassHint(fl_display, w, &ch)) {
    if (ch.res_name) { cls = ch.res_name; XFree(ch.res_name); }
    if (ch.res_class) XFree(ch.res_class);
  }
  for (auto &c : cls) c = (char)tolower((unsigned char)c);
  if (cls == "nirvana-shell") return Icon::Terminal;
  static const char *const prefs[] = {"nirvana-alice", "nirvana-preferences", "nirvana-bluetooth", "nirvana-datetime",
                                      "nirvana-network", "nirvana-power", "nirvana-printers", "nirvana-shader-panel",
                                      "nirvana-shield", "nirvana-user", "nirvana-desktop-panel"};
  for (const char *p : prefs) if (cls == p) return Icon::Preferences;
  if (cls == "nirvana-find" || cls == "nirvana-peek") return Icon::Folder;
  return Icon::Application;
}

// A 32x32 window icon image: the icon of an AppImage started from Rambler, _NET_WM_ICON,
// and as a last resort the generic application icon (nullptr - draw_icon).
static Fl_Image *iconic_image(Window w, const std::string &home) {
  for (auto &a : gary::list_apps([](Window) { return false; }))
    for (Window x : a.wins)
      if (x == w && !a.icon_file.empty()) {
        Entry e;
        e.icon_file = a.icon_file;
        e.icon = Icon::Application;
        // only a real image from the file - the fallback icon (16 px) means the file is missing
        Fl_Image *img = entry_icon_image(e, 32, false);
        if (img && img->w() == 32) return img;
      }
  (void)home;
  // _NET_WM_ICON from the program; without it - the icon from the .desktop file and the theme
  if (Fl_Image *img = gary::window_icon(w, 32)) return img;
  return gary::desktop_icon(w, 32);
}

void Desktop::layout_icons() {
  icons_.clear();
  // Desktop icons at the LEFT edge, from the top: Nirvana HD, Applications, Preferences, disks,
  // iconified windows; the Trash at the bottom, above the dock. When a column is full,
  // the next one stands beside it - folder windows and zoom keep all columns free.
  const int COL = 90, ROW = 70, top = MENUBAR_H + 14;
  // The Trash always stays in the same place, above the dock - also when the dock is empty,
  // so adding something to the dock does not move it
  int trash_y = h() - 74 - (DOCK_TILE + 4);
  int ix = 0, iy = top, cols = 1;
  auto place = [&](DeskIcon d) {
    if (iy + ROW > trash_y && iy > top) { ix += COL; iy = top; cols++; }
    d.x = ix; d.y = iy;
    icons_.push_back(d);
    iy += ROW;
  };
  place({"Nirvana HD", home_, Icon::Home, 0, 0});
  // applications (hidden ~/.nirvana-apps) and the Preferences panel
  place({_("Applications"), apps_dir(), Icon::Applications, 0, 0});
  {
    DeskIcon p{_("Preferences"), "", Icon::Preferences, 0, 0};
    p.prefs = true;
    place(p);
  }
  // disks and discs below Nirvana HD
  for (int i = 0; i < (int)vols_.size(); i++) {
    DeskIcon d{vols_[i].name, vols_[i].mountpoint, vols_[i].icon, 0, 0};
    d.vol = i;
    place(d);
  }
  // connected servers: like disks
  for (auto &[label, mp] : servers()) {
    DeskIcon d{label, mp, Icon::Network, 0, 0};
    d.server = true;
    place(d);
  }
  // the files and folders of the Desktop folder (~/Pulpit): between the disks and the
  // minimized windows - folders first, then by name
  {
    std::vector<Entry> files;
    std::error_code ec;
    for (auto &de : fs::directory_iterator(desktop_dir(), fs::directory_options::skip_permission_denied, ec)) {
      std::string n = de.path().filename().string();
      if (n.empty() || n[0] == '.') continue;
      files.push_back(entry_for(de.path().string()));
    }
    std::sort(files.begin(), files.end(), [](const Entry &a, const Entry &b) {
      if (a.dir != b.dir) return a.dir;
      return strcasecmp(a.label.c_str(), b.label.c_str()) < 0;
    });
    for (auto &e : files) {
      DeskIcon d{e.label, e.path, e.icon, 0, 0};
      d.deskfile = true;
      d.entry = e;
      place(d);
    }
  }
  for (Window w : iconic_windows()) {
    DeskIcon d;
    d.win = w;
    d.name = gary::window_title(w);
    bool ours = is_ours(w);
    d.icon = ours ? Icon::Folder : nirvana_icon(w);   // Nirvana's own programs: their own picture
    if (!ours && d.icon == Icon::Application) d.img = iconic_image(w, home_);
    if (d.name.empty()) d.name = "?";
    place(d);
  }
  icons_.push_back({_("Trash"), trash_dir(), trash_full() ? Icon::TrashFull : Icon::Trash, 0, trash_y});
  icon_cols_ = cols;
  // where each minimized window's icon is (_NIRVANA_ICON_RECT on the window, in the same
  // units as the frames): Insight flies the outlines to it and back
  if (shown() && fl_display)
    for (auto &d : icons_)
      if (d.win) {
        long r[4] = {x_root() + d.x + 24, y_root() + d.y, 32, 32};
        XChangeProperty(fl_display, d.win, XInternAtom(fl_display, "_NIRVANA_ICON_RECT", False), XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)r, 4);
      }
  // dock: icons left out on the desktop (Leave Out), as a strip in the bottom
  // left corner. The icon position as for the others: the picture at (x + 24, y).
  dock_shown_ = 0;
  if (!dock_collapsed_) {
    int tx = DOCK_TAB;
    for (auto &p : dock_) {
      std::error_code ec;
      if (!fs::exists(p, ec)) continue;
      if (tx + DOCK_TILE + DOCK_END > w() - 10) break;   // the dock ends at the right edge
      DeskIcon d;
      d.path = p;
      d.dock = true;
      d.entry = entry_for(p);
      d.name = d.entry.label;
      d.icon = d.entry.icon;
      d.x = tx + 4 - 24;
      d.y = h() - DOCK_TILE + 4;
      icons_.push_back(d);
      tx += DOCK_TILE;
      dock_shown_++;
    }
  }
  publish_folder_area();
}

static constexpr int MIN_W = 220, MIN_H = 170;   // smallest folder window
static Atom atom(const char *name) { return XInternAtom(fl_display, name, False); }

plat::Rect Desktop::folder_area() const {
  // Preferences > Desktop: without the margins windows may cover the icon column, and
  // there are no gaps; without the dock room they reach the bottom edge.
  const int GAP = desk_set_.icon_margin ? 6 : 0;
  int L = (desk_set_.icon_margin ? icon_cols_ * 90 + 6 : 0) + FRAME, T = MENUBAR_H + TITLE_H + GAP;
  int R = w() - FRAME - GAP;
  int B = h() - FRAME - GAP - (desk_set_.dock_room ? DOCK_TILE : 0);   // room for the dock (also when empty or collapsed)
  return {L, T, std::max(MIN_W, R - L), std::max(MIN_H, B - T)};
}

// Insight (the window manager) takes the zoom rectangle for Rambler windows from here:
// _NIRVANA_WINDOW_AREA on the root window = x, y, width, height of the frame on screen.
void Desktop::desktop_settings_changed() {
  int dock = desk_set_.dock_size;
  desk_set_ = plat::read_desktop();
  if (dock != desk_set_.dock_size) {   // a new dock size: the tiles and the Trash above them move
    layout_icons();
    redraw();
  }
  publish_folder_area();
}

void Desktop::publish_folder_area() {
  if (!shown() || !fl_display) return;
  // _NIRVANA_DOCK: height of the dock - Insight keeps the windows of all programs above it
  static long last_dock = -1;
  long dock = desk_set_.dock_room ? DOCK_TILE : 0;   // fixed room - windows do not jump when an icon is added
  if (dock != last_dock) {
    last_dock = dock;
    XChangeProperty(fl_display, DefaultRootWindow(fl_display), atom("_NIRVANA_DOCK"), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&dock, 1);
  }
  Rect a = folder_area();
  long v[4] = {x_root() + a.x - FRAME, y_root() + a.y - TITLE_H, a.w + 2 * FRAME, a.h + TITLE_H + FRAME};
  static long last[4] = {-1, -1, -1, -1};
  if (!memcmp(v, last, sizeof v)) return;
  memcpy(last, v, sizeof v);
  XChangeProperty(fl_display, DefaultRootWindow(fl_display), atom("_NIRVANA_WINDOW_AREA"), XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)v, 4);
}

// Rectangle of the whole dock strip (with the tab and the end); empty when the dock is empty.
void Desktop::dock_rect(int &X, int &Y, int &W, int &H) const {
  X = 0; H = DOCK_TILE; Y = h() - H;
  W = dock_.empty() ? 0 : DOCK_TAB + dock_shown_ * DOCK_TILE + DOCK_END;
}

// A dock icon of any size: pictures of programs scaled, Nirvana's vector icons drawn at 32 px
// on the tile color and enlarged (64: by whole pixels, crisp) or made smaller
static Fl_Image *dock_vector_icon(Icon icon, int size) {
  static std::map<std::tuple<Fl_Color, int, int, int>, Fl_Image *> cache;
  auto key = std::make_tuple(Fl::get_color(GRAY), (int)icon, size, (int)icon_style());
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  Fl_Image_Surface surf(32, 32);
  Fl_Surface_Device::push_current(&surf);
  fl_color(GRAY);
  fl_rectf(0, 0, 32, 32);
  draw_icon(icon, 0, 0, false);
  Fl_Surface_Device::pop_current();
  Fl_RGB_Image *src = surf.image();
  Fl_RGB_Scaling was = Fl_Image::RGB_scaling();
  Fl_Image::RGB_scaling(size % 32 == 0 ? FL_RGB_SCALING_NEAREST : FL_RGB_SCALING_BILINEAR);
  Fl_Image *img = src->copy(size, size);
  Fl_Image::RGB_scaling(was);
  delete src;
  cache[key] = img;
  return img;
}

static void draw_dock_icon(const Entry &e, int x, int y, int size) {
  if (size == 32 || !e.icon_file.empty()) draw_entry_icon(e, x, y, false, size);
  else dock_vector_icon(e.icon, size)->draw(x, y);
}

void Desktop::draw_dock() {
  int X, Y, W, H;
  dock_rect(X, Y, W, H);
  if (!W) return;
  // rounded end: background with an outline, an arc on the right
  int ex = X + W - DOCK_END - 6;
  fl_color(DARK);
  fl_rounded_rectf(ex, Y, DOCK_END + 6, H, 7);
  fl_color(GRAY);
  fl_rounded_rectf(ex + 1, Y + 1, DOCK_END + 4, H - 1, 6);
  fl_color(LIGHT);
  fl_xyline(ex + 1, Y + 1, X + W - 6);
  fl_color(SHADOW);
  for (int i = 0; i < 3; i++) fl_yxline(X + W - 9 + i * 2 - 2, Y + 12, Y + H - 12);   // handle
  // collapse tab
  fl_draw_box(UP_BOX, X, Y, DOCK_TAB, H, GRAY);
  fl_color(DARK);
  fl_rect(X, Y, DOCK_TAB + 1, H + 1);
  int cx = X + DOCK_TAB / 2, cy = Y + H / 2;
  fl_color(TEXT);
  if (dock_collapsed_) fl_polygon(cx - 2, cy - 4, cx + 2, cy, cx - 2, cy + 4);
  else fl_polygon(cx + 2, cy - 4, cx - 2, cy, cx + 2, cy + 4);
  // tiles
  for (int i = 0; i < (int)icons_.size(); i++) {
    auto &ic = icons_[i];
    if (!ic.dock) continue;
    bool sel = i == icon_sel_ || i == icon_drop_;
    int bx = ic.x + 20, by = ic.y - 4;
    fl_draw_box(sel ? DOWN_BOX : UP_BOX, bx, by, DOCK_TILE, DOCK_TILE,
                sel ? fl_color_average(GRAY, SHADOW, 0.6f) : GRAY);
    fl_color(DARK);
    fl_rect(bx, by, DOCK_TILE + 1, DOCK_TILE + 1);
    draw_dock_icon(ic.entry, ic.x + 24, ic.y, desk_set_.dock_size);
  }
}

void Desktop::toggle_dock() {
  dock_collapsed_ = !dock_collapsed_;
  if (dock_collapsed_ && icon_sel_ >= 0 && icon_sel_ < (int)icons_.size() && icons_[icon_sel_].dock) icon_sel_ = -1;
  save_rambler_conf();
  layout_icons();
  redraw();
}

int Desktop::icon_at(int ex, int ey) const {
  for (int i = 0; i < (int)icons_.size(); i++) {
    const auto &ic = icons_[i];
    if (ic.dock) {   // the whole tile
      int bx = ic.x + 20, by = ic.y - 4;
      if (ex >= bx && ex < bx + DOCK_TILE && ey >= by && ey < by + DOCK_TILE) return i;
      continue;
    }
    if (ex >= ic.x + 24 && ex < ic.x + 56 && ey >= ic.y && ey < ic.y + 52) return i;
  }
  return -1;
}

bool Desktop::allowed(const std::string &path) const {
  std::error_code ec;
  std::string c = fs::weakly_canonical(path, ec).string();
  std::string h = fs::weakly_canonical(home_, ec).string();
  if (c == h || c.rfind(h + "/", 0) == 0) return true;
  std::string gvfs = "/run/user/" + std::to_string(getuid()) + "/gvfs";   // servers (FTP/SFTP)
  if (c == gvfs || c.rfind(gvfs + "/", 0) == 0) return true;
  return volume_at_path(c) >= 0;   // mounted disks and discs are "ours" too
}

int Desktop::volume_at_path(const std::string &path) const {
  for (int i = 0; i < (int)vols_.size(); i++) {
    const std::string &mp = vols_[i].mountpoint;
    if (!mp.empty() && (path == mp || path.rfind(mp + "/", 0) == 0)) return i;
  }
  return -1;
}

std::string Desktop::title_for(const std::string &path) const {
  std::string arch, inner;
  if (is_archive_cache(path, &arch, &inner)) return fs::path(arch).filename().string() + inner;   // inside an archive
  if (path == home_) return "Nirvana HD";
  if (path == "/") return "System";
  if (path == trash_dir()) return _("Trash");
  if (path == apps_dir()) return _("Applications");
  for (auto &v : vols_) if (!v.mountpoint.empty() && v.mountpoint == path) return v.name;
  return fs::path(path).filename().string();
}

Fl_Window *make_drag_ghost(const Entry &e, int n);   // below, with dragging

// ---------------------------------------------------------------------------
// EWMH: talking to the window manager (Insight or any compliant one)
// ---------------------------------------------------------------------------
static Desktop *g_desk = nullptr;


static Window net_active() {
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  Window w = 0;
  if (XGetWindowProperty(fl_display, DefaultRootWindow(fl_display), atom("_NET_ACTIVE_WINDOW"), 0, 1,
                         False, XA_WINDOW, &type, &fmt, &n, &after, &data) == Success && data) {
    if (n) w = *(Window *)data;
    XFree(data);
  }
  return w;
}

static void net_message(Window w, Atom type, long l0, long l1, long l2) {
  XEvent ev{};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = w;
  ev.xclient.message_type = type;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = l0;
  ev.xclient.data.l[1] = l1;
  ev.xclient.data.l[2] = l2;
  ev.xclient.data.l[3] = 1;   // source: application
  XSendEvent(fl_display, DefaultRootWindow(fl_display), False,
             SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(fl_display);
}

// ---------------------------------------------------------------------------
// Global shortcuts: the items of the Nirvana, System and Special menus are on the Windows
// (Super) key - Win+E, Win+T, Win+Q, Win+K - so they work everywhere, whatever window is
// active, without taking a Ctrl+letter from any program. They are taken from the X server
// (XGrabKey on the root window); the key events of Rambler's own windows come here too.
// ---------------------------------------------------------------------------
struct GlobalKey { KeyCode code; unsigned mods; const MenuDef *def; };
static std::vector<GlobalKey> g_global_keys;
static const unsigned KEY_MODS = ControlMask | Mod1Mask | ShiftMask | Mod4Mask;

static unsigned x_mods(int sc) {
  unsigned m = 0;
  if (sc & FL_CTRL) m |= ControlMask;
  if (sc & FL_ALT) m |= Mod1Mask;
  if (sc & FL_SHIFT) m |= ShiftMask;
  if (sc & FL_META) m |= Mod4Mask;
  return m;
}

static int ignore_x_errors(Display *, XErrorEvent *) { return 0; }

void Desktop::update_global_keys() {
  if (!g_global_items || !fl_display) return;
  std::vector<GlobalKey> want;
  for (auto &d : *g_global_items) {
    if (!d.shortcut) continue;
    KeyCode k = XKeysymToKeycode(fl_display, (KeySym)(d.shortcut & 0xFFFF));
    if (k) want.push_back({k, x_mods(d.shortcut), &d});
  }
  bool same = want.size() == g_global_keys.size();
  for (size_t i = 0; same && i < want.size(); i++)
    same = want[i].code == g_global_keys[i].code && want[i].mods == g_global_keys[i].mods;
  if (same) return;
  Window root = DefaultRootWindow(fl_display);
  XSync(fl_display, False);
  auto old = XSetErrorHandler(ignore_x_errors);   // someone else holds a key: that one stays theirs
  static const unsigned extras[] = {0u, (unsigned)LockMask, (unsigned)Mod2Mask, (unsigned)(LockMask | Mod2Mask)};
  for (auto &g : g_global_keys)
    for (unsigned x : extras) XUngrabKey(fl_display, g.code, g.mods | x, root);
  g_global_keys = want;
  for (auto &g : g_global_keys)
    for (unsigned x : extras) XGrabKey(fl_display, g.code, g.mods | x, root, True, GrabModeAsync, GrabModeAsync);
  XSync(fl_display, False);
  XSetErrorHandler(old);
}

static int global_key(XEvent *e) {
  if (e->type != KeyPress || !g_desk) return 0;
  for (auto &g : g_global_keys)
    if (e->xkey.keycode == g.code && (e->xkey.state & KEY_MODS) == g.mods) {
      const MenuDef *d = g.def;
      Fl::add_timeout(0.0, [](void *p) {   // after this event: the action may open windows
        auto *def = (const MenuDef *)p;
        g_desk->update_menu();
        if (def->m) (g_desk->*(def->m))();
      }, (void *)d);
      return 1;
    }
  return 0;
}

// Change of the active window (a property on the root window).
static int x_watch(void *event, void *) {
  XEvent *e = (XEvent *)event;
  if (global_key(e)) return 1;
  if (e->type == PropertyNotify && e->xproperty.window == DefaultRootWindow(fl_display) && g_desk) {
    if (e->xproperty.atom == atom("_NET_ACTIVE_WINDOW")) g_desk->active_changed();
    else if (e->xproperty.atom == atom("_NIRVANA_ICONS")) g_desk->iconic_changed();
    else if (e->xproperty.atom == atom("_NET_CLIENT_LIST")) g_desk->clients_changed();
    else if (e->xproperty.atom == atom(APPEARANCE_ATOM)) g_desk->appearance_changed();
    else if (e->xproperty.atom == atom(DESKTOP_ATOM)) g_desk->desktop_settings_changed();   // Preferences > Desktop
  }
  return 0;
}

// Who manages the windows? Insight draws the Platinum frames itself; under any other
// manager (GNOME, KDE, Openbox...) or without one, Rambler draws them.
static Window wm_check_window() {
  Window root = DefaultRootWindow(fl_display), w = 0;
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  if (XGetWindowProperty(fl_display, root, atom("_NET_SUPPORTING_WM_CHECK"), 0, 1, False,
                         XA_WINDOW, &type, &fmt, &n, &after, &data) == Success && data) {
    if (n) w = *(Window *)data;
    XFree(data);
  }
  return w;
}

void Desktop::detect_wm() {
  std::string name;
  if (Window chk = wm_check_window()) {
    wm_present = true;
    Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
    if (XGetWindowProperty(fl_display, chk, atom("_NET_WM_NAME"), 0, 64, False, AnyPropertyType,
                           &type, &fmt, &n, &after, &data) == Success && data) {
      name.assign((char *)data, n);
      XFree(data);
    }
    // can the WM move the window for us (_NET_WM_MOVERESIZE)
    data = nullptr;
    if (XGetWindowProperty(fl_display, DefaultRootWindow(fl_display), atom("_NET_SUPPORTED"), 0, 1024,
                           False, XA_ATOM, &type, &fmt, &n, &after, &data) == Success && data) {
      Atom mr = atom("_NET_WM_MOVERESIZE");
      for (unsigned long i = 0; i < n; i++) if (((Atom *)data)[i] == mr) wm_moveresize = true;
      XFree(data);
    }
  }
  csd_mode = name != "insight";
  if (const char *e = getenv("NIRVANA_CSD")) csd_mode = !strcmp(e, "1");   // for testing
}

void Desktop::redraw_frames() {
  for (auto *w : wins_) if (w->csd) w->redraw();
}

// ---------------------------------------------------------------------------
// NWin - a folder or dialog window
// ---------------------------------------------------------------------------
NWin::NWin(Desktop *d, int X, int Y, int W, int H, const char *title, bool dlg)
    : Fl_Double_Window(X, Y, W, H), desk(d), dialog(dlg), csd(d->csd_mode) {
  copy_label(title);
  if (csd) border(0);   // a window manager without decorations - we draw the frame ourselves
  // The "Nirvana-*" class tells Insight that we do the opening animations ourselves.
  xclass(dlg ? "Nirvana-Dialog" : "Nirvana-Rambler");
  callback([](Fl_Widget *w, void *) {
    auto *n = (NWin *)w;
    // Escape closes a dialog, but not a folder window
    if (!n->dialog && Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape) return;
    n->desk->close(n);
  });
}

bool NWin::is_active() {
  if (!shown()) return false;
  if (!desk->wm_present) return desk->manual_active == this;
  return net_active() == fl_xid(this);
}

bool NWin::shaded() {
  if (csd) return csd_shaded_;
  if (!shown()) return false;
  Atom type; int fmt; unsigned long n, after; unsigned char *data = nullptr;
  bool s = false;
  if (XGetWindowProperty(fl_display, fl_xid(this), atom("_NET_WM_STATE"), 0, 32, False, XA_ATOM,
                         &type, &fmt, &n, &after, &data) == Success && data) {
    Atom shaded_atom = atom("_NET_WM_STATE_SHADED");
    for (unsigned long i = 0; i < n; i++) if (((Atom *)data)[i] == shaded_atom) s = true;
    XFree(data);
  }
  return s;
}

void NWin::set_shaded(bool s) {
  if (csd) { if (s != csd_shaded_) csd_toggle_shade(); return; }
  if (shown()) net_message(fl_xid(this), atom("_NET_WM_STATE"), s ? 1 : 0,
                           (long)atom("_NET_WM_STATE_SHADED"), 0);
}

void NWin::activate_window() {
  show();   // FLTK: an already visible window is raised
  if (desk->wm_present) {
    net_message(fl_xid(this), atom("_NET_ACTIVE_WINDOW"), 1, CurrentTime, 0);
  } else {
    XSetInputFocus(fl_display, fl_xid(this), RevertToPointerRoot, CurrentTime);
    desk->manual_active = this;
    desk->redraw_frames();
  }
}

Rect NWin::frame_rect() {
  if (csd) return {x(), y(), w(), h()};
  return {x() - FRAME, y() - TITLE_H, w() + 2 * FRAME, shaded() ? TITLE_H : h() + TITLE_H + FRAME};
}

Rect NWin::content_rect() {
  int H = csd ? (csd_shaded_ ? full_h_ : h()) - extra_h() : h();
  return {x() + ox(), y() + oy(), w() - extra_w(), H};
}

// --- own frame (CSD) ---------------------------------------------------------
void NWin::draw() {
  if (!csd) { Fl_Double_Window::draw(); return; }
  // Change only inside the window (e.g. scrolling, switching the view): redraw
  // only the changed widgets. The frame stays in the window buffer - if we painted
  // it every time, its gray background would wipe the content, and a scrolled area
  // only redraws the strip that came into view.
  if (!(damage() & ~FL_DAMAGE_CHILD)) {
    for (int i = 0; i < children(); i++) update_child(*child(i));
    return;
  }
  bool act = is_active();
  draw_frame(0, 0, w(), h(), act, csd_shaded_);
  draw_titlebar(0, 0, w(), label(), act, pressed_, !dialog);
  if (csd_shaded_) return;
  if (!dialog) {
    fl_color(act ? SHADOW : MID);
    fl_rect(ox() - 1, oy() - 1, w() - extra_w() + 2, h() - extra_h() + 2);
  }
  for (int i = 0; i < children(); i++) draw_child(*child(i));   // full redraw of the content
}

int NWin::hit_button(int ex, int ey) const {
  if (dialog) return 0;
  int by = (TITLE_H - BOX) / 2;
  if (ey < by || ey >= by + BOX) return 0;
  int zoom_x = w() - 8 - BOX, shade_x = zoom_x - BOX - 5;
  if (ex >= 8 && ex < 8 + BOX) return 1;
  if (ex >= zoom_x && ex < zoom_x + BOX) return 2;
  if (ex >= shade_x && ex < shade_x + BOX) return 3;
  return 0;
}

// Move/resize through the window manager (EWMH) - smooth and consistent
// with the desktop's behavior (snapping to edges etc.).
void NWin::start_moveresize(int direction) {
  XUngrabPointer(fl_display, CurrentTime);   // hand the pointer over to the window manager
  XEvent ev{};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = fl_xid(this);
  ev.xclient.message_type = atom("_NET_WM_MOVERESIZE");
  ev.xclient.format = 32;
  // reference point: where the button was pressed (the window "catches up" with the pointer)
  ev.xclient.data.l[0] = px(direction == 8 ? press_x_ : Fl::event_x_root());   // EWMH: pixels
  ev.xclient.data.l[1] = px(direction == 8 ? press_y_ : Fl::event_y_root());
  ev.xclient.data.l[2] = direction;           // 8 = move, 4 = bottom right corner
  ev.xclient.data.l[3] = Button1;
  ev.xclient.data.l[4] = 1;
  XSendEvent(fl_display, DefaultRootWindow(fl_display), False,
             SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(fl_display);
}

// a moment after a change: the window's place and size are remembered
static void remember_later(void *d) {
  auto *w = (NWin *)d;
  w->desk->remember(w, true);
}
static void remember_soon(void *d) { remember_later(d); }

void NWin::csd_toggle_shade() {
  Rect full = {x(), y(), w(), csd_shaded_ ? full_h_ : h()}, bar = {x(), y(), w(), TITLE_H};
  if (csd_shaded_) zoom_rects(bar, full); else zoom_rects(full, bar);
  if (!csd_shaded_) {
    full_h_ = h();
    csd_shaded_ = true;
    resizable(nullptr);          // the content must not be squeezed to zero
    init_sizes();
    size(w(), TITLE_H);
  } else {
    csd_shaded_ = false;
    size(w(), full_h_);
    resizable(pane ? (Fl_Widget *)pane : nullptr);
    init_sizes();
  }
  redraw();
  if (pane) Fl::add_timeout(0.4, remember_soon, this);
}

void NWin::csd_toggle_zoom() {
  if (csd_shaded_) csd_toggle_shade();
  Rect before = {x(), y(), w(), h()};
  if (!csd_zoomed_) {
    unzoom_[0] = x(); unzoom_[1] = y(); unzoom_[2] = w(); unzoom_[3] = h();
    Rect d = desk->desk_rect(), a = desk->folder_area();
    Rect to = {d.x + a.x - FRAME, d.y + a.y - TITLE_H, a.w + 2 * FRAME, a.h + TITLE_H + FRAME};
    zoom_rects(before, to);
    resize(to.x, to.y, to.w, to.h);
    csd_zoomed_ = true;
  } else {
    zoom_rects(before, {unzoom_[0], unzoom_[1], unzoom_[2], unzoom_[3]});
    resize(unzoom_[0], unzoom_[1], unzoom_[2], unzoom_[3]);
    csd_zoomed_ = false;
  }
}

int NWin::csd_handle(int e) {
  int ex = Fl::event_x(), ey = Fl::event_y();
  switch (e) {
  case FL_PUSH: {
    if (!desk->wm_present) { desk->manual_active = this; desk->redraw_frames(); show(); }
    if (Fl::event_button() != FL_LEFT_MOUSE) return 0;
    bool in_title = ey < TITLE_H;
    bool on_edge = !csd_shaded_ && !dialog &&
                   (ex >= w() - FRAME || ey >= h() - FRAME || ex < FRAME);
    if (!in_title && !on_edge) return 0;
    if (in_title) {
      if (int b = hit_button(ex, ey)) { drag_ = BTN; armed_ = pressed_ = b; redraw(); return 1; }
      if (Fl::event_clicks() && !dialog) { Fl::event_clicks(0); csd_toggle_shade(); return 1; }
    }
    bool grow = on_edge && ex >= FRAME;      // right/bottom edge; the left one moves
    if (desk->wm_moveresize && grow) { start_moveresize(4); return 1; }
    // Moving starts only after the mouse moves - otherwise a double click
    // (shading) would never reach us, because the window manager would take the pointer.
    press_x_ = Fl::event_x_root(); press_y_ = Fl::event_y_root();
    drag_ = grow ? GROW : PENDING;
    drag_dx_ = grow ? w() - Fl::event_x_root() : Fl::event_x_root() - x();
    drag_dy_ = grow ? h() - Fl::event_y_root() : Fl::event_y_root() - y();
    return 1;
  }
  case FL_DRAG:
    if (drag_ == PENDING) {
      if (abs(Fl::event_x_root() - press_x_) + abs(Fl::event_y_root() - press_y_) < 4) return 1;
      if (desk->wm_moveresize) { drag_ = NONE; start_moveresize(8); return 1; }
      drag_ = MOVE;
    }
    if (drag_ == MOVE) {
      position(Fl::event_x_root() - drag_dx_,
               std::max(desk->desk_rect().y + MENUBAR_H, Fl::event_y_root() - drag_dy_));
      return 1;
    }
    if (drag_ == GROW) {
      size(std::max(MIN_W + extra_w(), Fl::event_x_root() + drag_dx_),
           std::max(MIN_H + extra_h(), Fl::event_y_root() + drag_dy_));
      return 1;
    }
    if (drag_ == BTN) {
      int p = hit_button(ex, ey) == armed_ ? armed_ : 0;
      if (p != pressed_) { pressed_ = p; redraw(); }
      return 1;
    }
    return 0;
  case FL_RELEASE:
    if (drag_ == BTN) {
      int b = pressed_;
      drag_ = NONE; pressed_ = armed_ = 0;
      redraw();
      if (b == 1) { desk->close(this); return 1; }   // the window is deleted later
      if (b == 2) csd_toggle_zoom();
      if (b == 3) csd_toggle_shade();
      return 1;
    }
    if (drag_ != NONE) { drag_ = NONE; return 1; }
    return 0;
  }
  return 0;
}


void destroy_explorer(Explorer *x, NWin *w);
NWin::~NWin() {
  Fl::remove_timeout(remember_later, this);
  Fl::remove_timeout(remember_soon, this);
  if (ex) destroy_explorer(ex, this);
}

int NWin::handle(int e) {
  if (csd && csd_handle(e)) return 1;
  // FLTK sends unhandled shortcuts to all windows - the Window/Icons menus
  // only work in the window with the keyboard focus.
  if (e == FL_SHORTCUT && menubar && Fl::focus() && Fl::focus()->top_window() != this) return 0;
  // Alt/Ctrl + arrow (back, forward, up) - otherwise Fl_Group would use them
  // to move the focus between the content, the buttons and the location field
  if (e == FL_KEYBOARD && menubar && Fl::focus() != loc && (Fl::event_state() & (FL_ALT | FL_CTRL | FL_META))) {
    int k = Fl::event_key();
    if (k == FL_Left || k == FL_Right || k == FL_Up || k == FL_Down) {
      if (menubar->handle(FL_SHORTCUT)) return 1;
      return desk->shortcut();
    }
  }
  if (ex && e == FL_KEYBOARD && desk->ex_key(this)) return 1;   // Tab, F5-F8 in the explorer
  if (e == FL_KEYBOARD && pane && Fl::event_key() == ' ' && !dynamic_cast<Fl_Input *>(Fl::focus()) &&
      !(Fl::event_state() & (FL_CTRL | FL_ALT | FL_META))) {   // Space: a quick look at the selected file
    desk->menu_target = this;
    desk->preview_selected();
    desk->menu_target = nullptr;
    return 1;
  }
  int r = Fl_Double_Window::handle(e);
  // The menu bar lives in the desktop window while a folder window has the focus - pass the shortcut on.
  if (!r && e == FL_SHORTCUT && !dialog) r = desk->shortcut();
  return r;
}

// Remember the position after a move or resize - with a delay,
// because the window manager sends many changes while dragging.
void NWin::resize(int X, int Y, int W, int H) {
  Fl_Double_Window::resize(X, Y, W, H);
  if (ex) desk->ex_layout(this);
  if (pane && shown()) {
    Fl::remove_timeout(remember_later, this);
    Fl::add_timeout(0.4, remember_later, this);
  }
}

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------
NWin *Desktop::find(const std::string &path) {
  for (auto *w : wins_) if (w->pane && w->path == path) return w;
  return nullptr;
}

bool Desktop::known(NWin *w) const {
  return std::find(wins_.begin(), wins_.end(), w) != wins_.end();
}

NWin *Desktop::active_folder() {
  if (desk_target_) return nullptr;                    // desktop context menu
  if (menu_target && known(menu_target)) return menu_target;   // window menu bar
  Window a = net_active();
  for (auto *w : wins_) if (w->pane && w->shown() && fl_xid(w) == a) return w;
  return nullptr;
}

int Desktop::shortcut() {
  // Note: Fl_Menu_::test_shortcut() calls the action of the found item itself.
  update_menu();
  return menu_->test_shortcut() ? 1 : 0;
}

void Desktop::active_changed() {
  if (g_global_keys.empty()) update_global_keys();   // (the display was not open yet at the start)
  if (app_menu_) app_menu_->redraw();   // name and icon of the active application in the bar
  Window a = net_active();
  for (auto it = wins_.begin(); it != wins_.end(); ++it) {
    if ((*it)->shown() && fl_xid(*it) == a) {
      NWin *w = *it;
      wins_.erase(it);
      wins_.push_back(w);           // most recently active at the end of the list
      if (icon_sel_ >= 0) { icon_sel_ = -1; redraw(); }
      redraw_frames();
          return;
    }
  }
  redraw_frames();
}

void Desktop::remember(NWin *w, bool open) {
  if (!w->pane || !known(w)) return;
  WinState s;
  Rect cr = w->content_rect();   // we store the position of the content - without the frame
  // position relative to the desktop - the desktop can stand in different places (e.g. --center)
  s.x = cr.x - x_root(); s.y = cr.y - y_root(); s.w = cr.w; s.h = cr.h;
  s.shaded = w->shaded();
  s.open = open;
  s.list = w->pane->view()->mode() == FolderView::List;
  s.icons = w->pane->view()->icon_size();
  spatial_.set(w->path, s);
  if (w->ex) {   // the explorer window keeps one size of its own, whatever folder it shows
    WinState e = s;
    e.open = false;
    spatial_.set("@explorer", e);
  }
}

bool Desktop::source_rect(const std::string &path, Rect &out) {
  // Disks and the Trash have icons on the desktop
  for (auto &ic : icons_)
    if (ic.path == path) { out = to_screen({ic.x + 24, ic.y, 32, 32}); return true; }
  // Other folders - the icon in the parent folder's window, if it is open
  NWin *parent_win = find(fs::path(path).parent_path().string());
  if (parent_win && parent_win->shown() && !parent_win->shaded() &&
      parent_win->pane->view()->icon_rect(path, out)) {
    out = {out.x + parent_win->x(), out.y + parent_win->y(), out.w, out.h};
    return true;
  }
  return false;
}

void Desktop::close(NWin *w) {
  if (!known(w)) return;
  bool folder = w->pane != nullptr;
  Rect from = w->frame_rect(), to;
  if (folder && !source_rect(w->path, to)) to = center_of(from);
  if (folder) remember(w, false);
  Fl::remove_timeout(remember_later, w);
  wins_.erase(std::find(wins_.begin(), wins_.end(), w));
  w->hide();
  Fl::delete_widget(w);
  later([this] { release_archives(); ex_save_state(); });
  // folder icons in the other windows lose the "open" hatching
  for (auto *o : wins_) if (o->pane) o->pane->view()->redraw();
  if (folder) {
    Fl::flush();     // first the window disappears, then the outlines
    XSync(fl_display, False);
    zoom_rects(from, to);
  }
}

void Desktop::open_folder(const std::string &path, bool animate, bool new_window) {
  if (!allowed(path)) {
    dialog(GURU_TITLE,
           _("This folder is outside Nirvana HD."),
           {{"OK", nullptr}});
    return;
  }
  std::error_code ec;
  if (path == trash_dir()) fs::create_directories(path, ec);
  if (NWin *w = new_window ? nullptr : find(path)) {
    // already open: outlines show where its window is
    Rect src;
    if (animate && source_rect(path, src)) zoom_rects(src, w->frame_rect());
    if (w->shaded()) w->set_shaded(false);
    w->activate_window();
    return;
  }
  // The file manager works in one window: the folder is shown in the most recently used
  // window (a new window - Window > New Window or Ctrl + double-click).
  // the applications (with their category folders): a plain window also in the explorer mode
  bool apps = path == apps_dir() || in_apps(path + "/");
  if (apps && explorer_mode()) force_simple_ = true;
  if (!new_window) {
    NWin *recent = nullptr;
    // most recently active at the end - in the explorer mode an explorer window, for the
    // applications the plain Applications window
    for (auto *w : wins_)
      if (w->pane && w->shown() && (!explorer_mode() || bool(w->ex) != force_simple_)) recent = w;
    if (recent) {
      Rect src;
      if (recent->shaded()) recent->set_shaded(false);
      if (animate && source_rect(path, src)) zoom_rects(src, recent->frame_rect());
      navigate(recent, path);
      recent->activate_window();
      force_simple_ = false;
      return;
    }
  }
  // The position refers to the window content (without the frame) - that is how FLTK stores it.
  WinState s;
  bool ex_win = explorer_mode() && !force_simple_;   // Applications: always a plain window
  force_simple_ = false;
  bool known_pos = spatial_.get(ex_win ? std::string("@explorer") : path, s);
  if (!known_pos) {
    int n = 0;
    for (auto *w : wins_) if (w->pane) n++;
    s.x = 40 + 26 * (n % 8);
    s.y = MENUBAR_H + TITLE_H + 24 + 26 * (n % 8);
    s.w = ex_win ? 820 : 440; s.h = ex_win ? 520 : 270;
  }
  // the window fits into the folder area: Nirvana HD, the disks, the Trash and the dock stay visible
  Rect area = folder_area();
  s.w = std::clamp(s.w, MIN_W, area.w);
  s.h = std::clamp(s.h, MIN_H, area.h);
  s.x = std::clamp(s.x, area.x, area.x + area.w - s.w);
  s.y = std::clamp(s.y, area.y, area.y + area.h - s.h);

  // remembered positions are relative to the desktop - convert to screen coordinates
  int ax = s.x + x_root(), ay = s.y + y_root();

  if (animate) {
    Rect to = {ax - FRAME, ay - TITLE_H, s.w + 2 * FRAME, s.h + TITLE_H + FRAME};
    if (known_pos && s.shaded) to.h = TITLE_H;
    Rect from;
    if (!source_rect(path, from)) from = center_of(to);
    zoom_rects(from, to);
  }

  // with our own frame the window is larger by the frame, and the content lies inside
  int ox = csd_mode ? FRAME : 0, oy = csd_mode ? TITLE_H : 0;
  int ew = csd_mode ? 2 * FRAME : 0, eh = csd_mode ? TITLE_H + FRAME : 0;
  auto *win = new NWin(this, ax - ox, ay - oy, s.w + ew, s.h + eh, title_for(path).c_str(), false);
  win->path = path;
  if (ex_win) {
    build_explorer(win, ox, oy, s.w, s.h, path);
    win->size_range(560 + ew, csd_mode ? TITLE_H : 320);
    wins_.push_back(win);
    update_nav(win);
    win->show();
    win->pane->view()->take_focus();
    if (known_pos && s.shaded) win->set_shaded(true);
    remember(win, true);
    return;
  }
  const int bars = NWin::MB_H + NWin::NAV_H;
  // the content as the first child: it gets the keyboard focus when the window opens
  win->pane = new FolderPane(ox, oy + bars, s.w, s.h - bars, path, show_hidden_);
  // a folder seen for the first time: a list, the applications - big icons (like Preferences)
  if (!known_pos) { s.list = !(path == apps_dir() || in_apps(path + "/")); s.icons = 32; }
  win->pane->view()->set_icon_size(s.icons);
  build_folder_bars(win, ox, oy, s.w);
  win->resizable(win->pane);
  // with our own frame a shaded window is only as high as the title bar
  win->size_range(MIN_W + ew, csd_mode ? TITLE_H : MIN_H);
  win->end();
  setup_pane(win, win->pane, -1);
  win->pane->set_mode(s.list ? FolderView::List : FolderView::Icons);
  wins_.push_back(win);
  update_nav(win);
  win->show();
  win->pane->view()->take_focus();
  if (known_pos && s.shaded) win->set_shaded(true);
  // the folder icon in the parent window gets the "open" hatching
  for (auto *o : wins_) if (o->pane && o != win) o->pane->view()->redraw();
  remember(win, true);
}

// The callbacks of a file list. In an explorer (idx >= 0) every use of a pane first makes
// it the active one (the source), so all the window's commands work on it.
void Desktop::setup_pane(NWin *win, FolderPane *p, int idx) {
  auto act = [this, win, idx] { if (idx >= 0 && known(win)) ex_activate(win, idx); };
  p->view()->on_open = [this, win, act](const Entry &e) { act(); open_entry(e, win); };
  p->view()->on_context = [this, win, act] {
    act();
    int X = Fl::event_x(), Y = Fl::event_y();
    later([this, win, X, Y] { if (known(win)) folder_context_menu(win, X, Y); });
  };
  p->view()->is_open = [this, win](const std::string &q) { NWin *o = find(q); return o && o != win; };
  p->view()->on_drag_start = [this, win, act](const std::vector<Entry> &v) { act(); drag_start(win, v); };
  p->view()->on_drag_move = [this](int xr, int yr) { drag_move(xr, yr); };
  p->view()->on_drag_end = [this](int xr, int yr, bool copy) { drag_end(xr, yr, copy); };
  p->view()->on_drop_uris = [this, p](const std::string &uris, const std::string &dest) {
    drop_uris(uris, dest.empty() ? p->path() : dest);
  };
  p->on_rename_commit = [this](const std::string &o, const std::string &n) { return rename_path(o, n); };
  p->on_error = [this](const std::string &m) { dialog(GURU_TITLE, m, {{"OK", nullptr}}); };
}

// The window's menu bar (Window, Icons) and the navigation bar above the folder content.
void Desktop::build_folder_bars(NWin *win, int X, int Y, int W) {
  win->begin();
  make_folder_menubar(win, X, Y, W);

  auto *nav = new NavBar(X, Y + NWin::MB_H, W, NWin::NAV_H);
  const int by = Y + NWin::MB_H + 3, bh = NWin::NAV_H - 6, bw = 26;
  auto button = [&](int bx, const char *sym, const char *tip, void (Desktop::*m)()) {
    auto *b = new Fl_Button(bx, by, bw, bh, sym);
    b->box(BUTTON_BOX);
    b->color(GRAY);
    b->labelsize(10);
    b->labelcolor(TEXT);
    b->clear_visible_focus();
    b->tooltip(tip);
    static std::vector<void (Desktop::*)()> ms;
    ms.push_back(m);
    b->callback([](Fl_Widget *w, void *d) {
      auto *nw = (NWin *)w->window();
      nw->desk->menu_target = nw;
      (nw->desk->*ms[(long)d])();
      nw->desk->menu_target = nullptr;
    }, (void *)(long)(ms.size() - 1));
    return b;
  };
  win->back_b = button(X + 6, "@<", _("Back"), &Desktop::go_back);
  win->fwd_b = button(X + 6 + bw + 2, "@>", _("Forward"), &Desktop::go_forward);
  win->up_b = button(X + 6 + 2 * (bw + 2) + 6, "@8>", _("Open Parent"), &Desktop::open_parent);
  int lx = X + 6 + 3 * (bw + 2) + 12;
  auto *in = new Fl_Input(lx, by, std::max(40, X + W - 6 - lx), bh);
  in->box(WELL_BOX);
  in->textfont(FONT_SYSTEM);
  in->textsize(SIZE_SMALL + 1);
  in->when(FL_WHEN_ENTER_KEY_ALWAYS);
  in->callback([](Fl_Widget *w, void *) {
    auto *nw = (NWin *)w->window();
    nw->desk->go_location(nw);
  });
  win->loc = in;
  nav->resizable(in);
  nav->end();
  win->end();
}

Fl_Menu_Bar *Desktop::make_folder_menubar(NWin *win, int X, int Y, int W) {
  auto *mb = new NirvanaMenuBar(X, Y, W, NWin::MB_H);
  mb->box(MENUBAR_BOX);
  mb->color(GRAY);
  mb->textfont(FONT_SYSTEM);
  mb->textsize(SIZE_SYSTEM);
  mb->down_box(FL_FLAT_BOX);
  mb->selection_color(SEL);
  for (auto &d : window_menu_defs())
    mb->add(plat::tr_path(d.path).c_str(), d.shortcut, window_menu_cb, (void *)&d, d.flags);
  mb->before_open = [this, win] { update_menu(win->menubar, win); };
  win->menubar = mb;
  return mb;
}

// The mounted disks by their names (Desktop::rescan_volumes keeps it): "PENDRIVE" for /media/...
static std::vector<std::pair<std::string, std::string>> g_disk_names;   // (mount point, name)

// Paths for people: the home folder is the disk "Nirvana HD" - "Nirvana HD/Muzyka", never "~";
// a mounted disk by its name - "PENDRIVE/Zdjecia"
// the Trash's folder (the freedesktop.org one, as trash_dir())
static std::string trash_files() {
  const char *x = getenv("XDG_DATA_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "") + "/.local/share") + "/Trash/files";
}

std::string disk_path(const std::string &path) {
  const char *h = getenv("HOME");
  std::string home = h ? h : "";
  while (home.size() > 1 && home.back() == '/') home.pop_back();
  std::string trash = trash_files();   // the Trash by its name, not ".local/share/Trash/files"
  if (path == trash) return _("Trash");
  if (path.rfind(trash + "/", 0) == 0) return _("Trash") + path.substr(trash.size());
  for (auto &[mp, name] : g_disk_names) {   // a disk inside the home folder (rare) is still the disk
    if (path == mp) return name;
    if (path.rfind(mp + "/", 0) == 0) return name + path.substr(mp.size());
  }
  if (home.empty() || home == "/") return path;
  std::string apps = home + "/.nirvana-apps";   // the applications by the name of their icon
  if (path == apps) return _("Applications");
  if (path.rfind(apps + "/", 0) == 0) return _("Applications") + path.substr(apps.size());
  if (path == home) return "Nirvana HD";
  if (path.rfind(home + "/", 0) == 0) return "Nirvana HD" + path.substr(home.size());
  return path;
}
// ...and back: "Nirvana HD/Muzyka" (and the old "~/Muzyka"), "PENDRIVE/Zdjecia" -> the real path
std::string real_path(const std::string &text) {
  const char *h = getenv("HOME");
  std::string home = h ? h : "";
  static const std::string disk = "Nirvana HD";
  std::string tn = _("Trash");
  if (text == tn) return trash_files();
  if (text.rfind(tn + "/", 0) == 0) return trash_files() + text.substr(tn.size());
  std::string an = _("Applications");
  if (text == an) return home + "/.nirvana-apps";
  if (text.rfind(an + "/", 0) == 0) return home + "/.nirvana-apps" + text.substr(an.size());
  if (text == disk || text == "~") return home;
  if (text.rfind(disk + "/", 0) == 0) return home + text.substr(disk.size());
  if (text.rfind("~/", 0) == 0) return home + text.substr(1);
  if (!text.empty() && text[0] != '/')
    for (auto &[mp, name] : g_disk_names) {
      if (text == name) return mp;
      if (text.rfind(name + "/", 0) == 0) return mp + text.substr(name.size());
    }
  return text;
}
static std::string location_text(const std::string &path, const std::string &) { return disk_path(path); }

std::string server_url(const std::string &path);   // explorer.cxx

void Desktop::update_nav(NWin *w) {
  if (!w || !w->loc) return;
  std::string arch, inner;
  std::string url = server_url(w->path);   // a server: "sftp://me@host/dir"
  if (is_archive_cache(w->path, &arch, &inner)) w->loc->value((location_text(arch, home_) + inner).c_str());
  else if (!url.empty()) w->loc->value(url.c_str());
  else w->loc->value(location_text(w->path, home_).c_str());
  if (w->ex) ex_after_nav(w);
  auto set = [](Fl_Widget *b, bool on) { if (on) b->activate(); else b->deactivate(); };
  set(w->back_b, !w->back_hist.empty());
  set(w->fwd_b, !w->fwd_hist.empty());
  std::string par = fs::path(w->path).parent_path().string();
  set(w->up_b, par != w->path && allowed(par) && w->path != root_path_);
}

void Desktop::navigate(NWin *w, const std::string &target, Nav how) {
  if (!w || !w->pane || !known(w)) return;
  std::string path = target;
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  if (path == w->path) { update_nav(w); return; }
  // the plain Applications window shows only the applications: another folder (Up from
  // Applications, an address) opens in Rambler Advanced
  if (!w->ex && !(path == apps_dir() || in_apps(path + "/"))) {
    update_nav(w);
    open_folder(path);
    return;
  }
  if (!allowed(path)) {
    update_nav(w);
    dialog(GURU_TITLE,
           _("This folder is outside Nirvana HD."),
           {{"OK", nullptr}});
    return;
  }
  std::error_code ec;
  if (path == trash_dir()) fs::create_directories(path, ec);
  if (!fs::is_directory(path, ec)) {
    update_nav(w);
    dialog(GURU_TITLE, plat::fmt(_("The folder “%1” could not be found."), path), {{"OK", nullptr}});
    return;
  }
  remember(w, false);   // the previous folder is no longer open
  if (how == Nav::Back && !w->back_hist.empty()) {
    w->fwd_hist.push_back(w->path);
    w->back_hist.pop_back();
  } else if (how == Nav::Forward && !w->fwd_hist.empty()) {
    w->back_hist.push_back(w->path);
    w->fwd_hist.pop_back();
  } else {
    w->back_hist.push_back(w->path);
    w->fwd_hist.clear();
  }
  if (w->back_hist.size() > 100) w->back_hist.erase(w->back_hist.begin());
  std::string old = w->path;
  w->path = path;
  // the view remembered for the new folder (list / icons, icon size)
  WinState s;
  bool known_pos = spatial_.get(path, s);
  // the list is the default, the applications are big icons (like Preferences)
  if (!known_pos) { s.list = !(path == apps_dir() || in_apps(path + "/")); s.icons = 32; }
  w->pane->view()->set_icon_size(s.icons);
  w->pane->set_mode(s.list ? FolderView::List : FolderView::Icons);
  w->pane->go(path);
  // going up: select the folder we came from
  if (fs::path(old).parent_path().string() == path) w->pane->view()->select_path(old);
  w->copy_label(title_for(path).c_str());
  update_nav(w);
  remember(w, true);
  for (auto *o : wins_) if (o->pane) o->pane->view()->redraw();
  w->pane->view()->take_focus();
}

void Desktop::go_back() {
  NWin *a = active_folder();
  if (a && !a->back_hist.empty()) navigate(a, a->back_hist.back(), Nav::Back);
}

void Desktop::go_forward() {
  NWin *a = active_folder();
  if (a && !a->fwd_hist.empty()) navigate(a, a->fwd_hist.back(), Nav::Forward);
}

void Desktop::new_window() {
  NWin *a = active_folder();
  open_folder(a ? a->path : root_path_, true, true);
}

// Enter in the location field: an absolute path, "~/..." or relative to the window's folder.
// A file: its folder with the file selected.
void Desktop::go_location(NWin *w) {
  std::string t = w->loc->value();
  while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
  while (!t.empty() && isspace((unsigned char)t.front())) t.erase(0, 1);
  if (t.empty()) { update_nav(w); return; }
  if (t.find("://") != std::string::npos) {   // "sftp://me@host/dir" - a connected server
    std::string p = server_path(t);
    if (p.empty()) { guru(fmt(_("“%1” is not connected. Connect to it first with Server in the toolbar (or Window > Connect to a Server…)."), t)); update_nav(w); return; }
    t = p;
  }
  t = real_path(t);   // "Nirvana HD/Muzyka" (or "~/Muzyka")
  if (t[0] != '/') t = w->path + "/" + t;   // relative to the folder shown
  for (auto &[dir, arch] : archive_dirs_)   // "Nirvana HD/x.zip/sub" - inside an archive that is open
    if (t == arch || t.rfind(arch + "/", 0) == 0) { t = dir + t.substr(arch.size()); break; }
  std::error_code ec;
  fs::path p = fs::weakly_canonical(fs::path(t), ec);
  if (ec) p = fs::path(t).lexically_normal();
  std::string ps = p.string();
  if (fs::exists(p, ec) && !fs::is_directory(p, ec)) {
    navigate(w, p.parent_path().string());
    if (w->path == p.parent_path().string()) w->pane->view()->select_path(ps);
    return;
  }
  navigate(w, ps);
}

void Desktop::open_entry(const Entry &e, NWin *from) {
  if (e.dir) {
    // Ctrl + double-click: the folder in a new window
    bool new_win = (Fl::event_state() & FL_CTRL) != 0;
    if (from && !new_win && known(from)) navigate(from, e.path);
    else open_folder(e.path, true, new_win);
    return;
  }
  if (is_appimage(e.name)) { open_appimage(e.path); return; }
  // Special > File Types: the type's double click (Shift: the second action)
  if (run_event(e, from, (Fl::event_state() & FL_SHIFT) ? "open2" : "open")) return;
  // a file with the "Program" flag (Information): run it in Nirvana Shell with its arguments
  struct stat st;
  if (stat(e.path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR) && allowed(e.path)) {
    std::vector<std::string> a = {rambler_exe("nirvana-shell"), "--working-directory", fs::path(e.path).parent_path().string(),
                                  "-e", "sh", "-c", "m=$1; shift; \"$@\"; s=$?; echo; printf \"$m\" \"$s\"; read x",
                                  // TRANSLATORS: %s = the program's exit code (keep %s)
                                  "sh", _("[exit code %s] Press Enter to close…"), e.path};
    for (auto &x : split_args(tooltypes(e.path))) a.push_back(x);
    spawn(a);
    return;
  }
  spawn({"xdg-open", e.path});
}

std::string Desktop::apps_dir() const { return appimage::apps_dir(home_); }

// The Desktop folder: XDG_DESKTOP_DIR, otherwise ~/Pulpit (the name in the interface language)
std::string Desktop::desktop_dir() const {
  std::string d = plat::xdg_user_dir("DESKTOP");
  if (d.empty() || !allowed(d)) d = home_ + "/" + _("Desktop");
  return d;
}

// A change in the Desktop folder: the icons are laid out again
void Desktop::watch_desktop_dir() {
  std::string d = desktop_dir();
  std::error_code ec;
  fs::create_directories(d, ec);
  if (desk_ino_ >= 0) { Fl::remove_fd(desk_ino_); ::close(desk_ino_); }
  desk_ino_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (desk_ino_ < 0) return;
  if (inotify_add_watch(desk_ino_, d.c_str(), IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB |
                                                   IN_DELETE_SELF | IN_MOVE_SELF) < 0) {
    ::close(desk_ino_);
    desk_ino_ = -1;
    return;
  }
  Fl::add_fd(desk_ino_, FL_READ, [](int fd, void *p) {
    char buf[4096];
    while (read(fd, buf, sizeof buf) > 0) {}
    static auto relayout = [](void *q) {
      auto *dk = (Desktop *)q;
      dk->layout_icons();
      dk->icon_sel_ = -1;
      dk->redraw();
    };
    Fl::remove_timeout(relayout, p);
    Fl::add_timeout(0.2, relayout, p);   // a series of changes: one new layout
  }, this);
}

// Special > Screenshot (Print): the whole screen as a PNG in Pictures, "Screenshot <date>.png"
void Desktop::screenshot() {
  Fl::add_timeout(0.35, [](void *p) {   // after the menu has closed
    auto *dk = (Desktop *)p;
    Display *d = fl_display;
    Window root = DefaultRootWindow(d);
    XWindowAttributes wa;
    XGetWindowAttributes(d, root, &wa);
    XImage *im = XGetImage(d, root, 0, 0, wa.width, wa.height, AllPlanes, ZPixmap);
    if (!im) { dk->guru(_("The screenshot could not be taken.")); return; }
    int W = wa.width, H = wa.height;
    uchar *rgb = new uchar[(size_t)W * H * 3];
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        unsigned long px = XGetPixel(im, x, y);
        uchar *q = rgb + ((size_t)y * W + x) * 3;
        q[0] = (uchar)((px & im->red_mask) >> __builtin_ctzl(im->red_mask ? im->red_mask : 1));
        q[1] = (uchar)((px & im->green_mask) >> __builtin_ctzl(im->green_mask ? im->green_mask : 1));
        q[2] = (uchar)((px & im->blue_mask) >> __builtin_ctzl(im->blue_mask ? im->blue_mask : 1));
      }
    XDestroyImage(im);
    std::string dir = plat::pictures_dir();
    std::error_code ec;
    if (!fs::is_directory(dir, ec) && fs::is_directory(dk->home_ + "/" + _("Pictures"), ec)) dir = dk->home_ + "/" + _("Pictures");
    fs::create_directories(dir, ec);
    char stamp[64];
    time_t t = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M.%S", localtime(&t));
    std::string file = dir + "/" + _("Screenshot") + " " + stamp + ".png";
    Fl_RGB_Image img(rgb, W, H, 3);
    int r = fl_write_png(file.c_str(), &img);
    delete[] rgb;
    if (r != 0) { dk->guru(_("The screenshot could not be saved.")); return; }
    std::string shown = disk_path(file);
    applets::notify_local(_("Screenshot"), _("Screenshot saved"), shown, "", {{"open", _("Show in Folder")}},
                          [dk, file](const std::string &) { dk->open_folder(fs::path(file).parent_path().string()); });
  }, this);
}

// The Applications icon: a plain Rambler window with the folder structure of the programs
// (the category folders) - also when the folder windows are explorers
void Desktop::open_apps() {
  for (auto *w : wins_)
    if (w->pane && w->shown() && !w->ex && in_apps(w->path + "/")) {   // already open: to the front
      if (w->shaded()) w->set_shaded(false);
      w->activate_window();
      return;
    }
  open_folder(apps_dir(), true, true);
}

// Is the file in the applications folder (also in a category subfolder)?
bool Desktop::in_apps(const std::string &path) const {
  std::string a = apps_dir() + "/";
  return path.compare(0, a.size(), a) == 0;
}

// Destination of an AppImage in the applications folder: the category folder (Games, Graphics...).
std::string Desktop::app_destination(const std::string &path) const {
  return (fs::path(apps_dir()) / appimage::category_folder(path, home_) / fs::path(path).filename()).string();
}

// An AppImage from outside the applications folder: offer to install it (like a phone app store,
// just without the store). From the applications folder: run it, extracting it if needed.
static std::string menu_escape(const std::string &s);

void Desktop::open_appimage(const std::string &path) {
  if (std::string a = appimage::arch_problem(path); !a.empty()) { guru(a); return; }
  if (in_apps(path)) { run_app(path); return; }
  std::string name = appimage::label(path, home_);
  std::string fname = fs::path(path).filename().string();
  std::error_code ec;
  // move to the category folder and the first start
  auto install_to = [this, path](const fs::path &dest) {
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (fs::exists(dest, ec)) fs::remove(dest, ec);   // "Replace": the same version
    fs::rename(path, dest, ec);
    if (ec) {   // another file system (USB stick): copy and delete
      ec.clear();
      fs::copy_file(path, dest, ec);
      if (!ec) fs::remove(path, ec);
    }
    if (ec) {
      dialog(GURU_TITLE, _("Could not move the file:") + std::string("\n") + ec.message(), {{_("OK"), nullptr}});
      return;
    }
    chmod(dest.c_str(), 0755);
    path_moved(path, dest.string());
    refresh_all();
    run_app(dest.string());
  };
  // the same version is already in one of the categories - other versions can stand beside it
  fs::path existing;
  for (auto it = fs::recursive_directory_iterator(apps_dir(), fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it.depth() > 1) { it.disable_recursion_pending(); continue; }
    if (it->path().filename() == fname) { existing = it->path(); break; }
  }
  if (!existing.empty()) {
    std::string folder = existing.parent_path().filename().string();
    dialog(_("Installation"),
           plat::fmt(_("“%1” is already installed in %2.\n"
                       "Other versions of this program can be installed alongside it."), name, folder),
           {{_("Cancel"), nullptr},
            {_("Replace"), [install_to, existing] { install_to(existing); }},
            {_("Run"), [this, existing] { run_app(existing.string()); }}});
    return;
  }
  // Installation: the user chooses the category (suggested by the .desktop file in the AppImage)
  int W = 460, H = 196, ox, oy;
  Entry ic;
  ic.name = fname;
  ic.path = path;
  ic.icon = Icon::Application;
  ic.icon_file = appimage::icon(path, home_);
  NWin *win = begin_form(_("Installation"), W, H, &ic, plat::fmt(_("Install the application “%1”?"), name), ox, oy);
  auto *ch = new Fl_Choice(ox + 150, oy + 58, W - 166, 22, _("Category:"));
  ch->labelfont(FONT_SYSTEM);
  ch->labelsize(SIZE_SYSTEM);
  ch->textsize(SIZE_SMALL + 1);
  ch->box(BUTTON_BOX);
  ch->down_box(FL_FLAT_BOX);
  ch->color(GRAY);
  ch->selection_color(SEL);
  const char *detected = appimage::detect_category(path, home_);
  const auto &cats = appimage::categories();
  for (size_t k = 0; k < cats.size(); k++) {
    ch->add(menu_escape(_(cats[k])).c_str());
    if (!strcmp(cats[k], detected)) ch->value((int)k);
  }
  auto *note = new Fl_Box(ox + 64, oy + 92, W - 80, 54);
  note->copy_label((appimage::sandbox_available()
                        ? _("The application will run in a sandbox: it will only see Nirvana HD.")
                        : _("Note: bubblewrap is missing - the application will run without a sandbox.")));
  note->labelsize(SIZE_SMALL + 1);
  note->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  end_form(win, W, H, ox, oy,
           {{_("Cancel"), nullptr},
            {_("Install"), [this, ch, fname, install_to] {
               const auto &cats = appimage::categories();
               int v = std::clamp(ch->value(), 0, (int)cats.size() - 1);
               install_to(fs::path(apps_dir()) / appimage::category_dir_name(home_, cats[v]) / fname);
             }}});
}

void Desktop::run_app(const std::string &path) {
  if (std::string a = appimage::arch_problem(path); !a.empty()) { guru(a); return; }
  if (!appimage::is_installed(path, home_)) {
    for (auto *w : wins_) w->cursor(FL_CURSOR_WAIT);
    cursor(FL_CURSOR_WAIT);
    Fl::flush();
    std::string err = appimage::install(path, home_);
    for (auto *w : wins_) w->cursor(FL_CURSOR_DEFAULT);
    cursor(FL_CURSOR_DEFAULT);
    if (!err.empty()) {
      dialog(GURU_TITLE, err, {{"OK", nullptr}});
      return;
    }
    refresh_all();   // the icon of some AppImages (e.g. DwarFS) is only available after extraction
  }
  // Qt and other programs in AppImages often count on system libraries (xcb-cursor, xkbcommon-x11...)
  if (!libs_ok_.count(path)) {
    auto miss = appimage::missing_libs(path, home_);
    if (!miss.empty()) { ask_missing_libs(path, miss); return; }
    libs_ok_.insert(path);
  }
  // gary: the first new window is this application - it gets the name and icon from the AppImage
  gary::note_launch(appimage::label(path, home_),
                    appimage::icon(path, home_));
  appimage::launch(path, home_, split_args(tooltypes(path)));   // arguments from the Information window
}

// --- system libraries an application needs ---------------------------------------------
static std::string libs_helper() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  std::string e = n > 0 ? std::string(buf, (size_t)n) : "";
  e = e.substr(0, e.rfind('/'));
  for (const std::string &c : {e + "/../libexec/nirvana-libs-helper", e + "/../update/nirvana-libs-helper",
                               std::string("/usr/libexec/nirvana-libs-helper")})
    if (!access(c.c_str(), R_OK)) return c;
  return "";
}

// runs a program, the window stays alive; the output (stdout + stderr) goes to *out
static int run_with_output(const std::vector<std::string> &args, std::string *out) {
  int p[2];
  if (pipe(p)) return 127;
  pid_t pid = fork();
  if (pid == 0) {
    dup2(p[1], 1);
    dup2(p[1], 2);
    close(p[0]);
    close(p[1]);
    std::vector<char *> a;
    for (auto &s : args) a.push_back((char *)s.c_str());
    a.push_back(nullptr);
    execvp(a[0], a.data());
    _exit(127);
  }
  close(p[1]);
  char buf[1024];
  ssize_t n;
  out->clear();
  while ((n = read(p[0], buf, sizeof buf)) != 0) {
    if (n > 0) out->append(buf, (size_t)n);
    else if (errno != EINTR) break;
    Fl::check();
  }
  close(p[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  return WIFEXITED(st) ? WEXITSTATUS(st) : 127;
}

void Desktop::ask_missing_libs(const std::string &path, const std::vector<std::string> &missing) {
  std::string h = libs_helper(), out, list, pkgs;
  for (auto &m : missing) list += (list.empty() ? "" : ", ") + m;
  if (!h.empty()) {
    std::vector<std::string> a = {"sh", h, "packages"};
    a.insert(a.end(), missing.begin(), missing.end());
    run_with_output(a, &out);
    size_t pos = 0;
    while (pos < out.size()) {
      size_t nl = out.find('\n', pos);
      std::string l = out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
      pos = nl == std::string::npos ? out.size() : nl + 1;
      size_t sp = l.find(' ');
      if (sp == std::string::npos) continue;
      std::string pk = l.substr(sp + 1);
      if ((" " + pkgs + " ").find(" " + pk + " ") == std::string::npos) pkgs += (pkgs.empty() ? "" : " ") + pk;
    }
  }
  std::string text = plat::fmt(_("“%1” needs system libraries that are missing:\n%2"), appimage::label(path, home_), list);
  text += "\n\n";
  text += pkgs.empty() ? std::string(_("Nirvana Desktop does not know which packages provide them; without them the application may not start."))
                       : plat::fmt(_("They can be installed now (%1)."), pkgs);
  std::vector<std::pair<std::string, std::function<void()>>> b = {
      {_("Cancel"), nullptr},
      {_("Run Anyway"), [this, path] { libs_ok_.insert(path); run_app(path); }}};
  if (!pkgs.empty()) b.push_back({_("Install"), [this, path, missing] { install_libs(path, missing); }});
  dialog(_("Missing Libraries"), text, b);
}

void Desktop::install_libs(const std::string &path, const std::vector<std::string> &missing) {
  std::string h = libs_helper(), out;
  if (h.empty()) { guru(_("nirvana-libs-helper is missing.")); return; }
  for (auto *w : wins_) w->cursor(FL_CURSOR_WAIT);
  cursor(FL_CURSOR_WAIT);
  Fl::flush();
  std::vector<std::string> a = {plat::pkexec_command(), h, "install"};
  a.insert(a.end(), missing.begin(), missing.end());
  int rc = run_with_output(a, &out);
  for (auto *w : wins_) w->cursor(FL_CURSOR_DEFAULT);
  cursor(FL_CURSOR_DEFAULT);
  appimage::forget_system_libs();
  if (rc != 0) {
    // the last lines of what apt said
    std::string tail = out.size() > 600 ? out.substr(out.size() - 600) : out;
    size_t nl = tail.find('\n');
    if (out.size() > 600 && nl != std::string::npos) tail = tail.substr(nl + 1);
    dialog(GURU_TITLE, rc == 126 || rc == 127 ? std::string(_("The libraries were not installed."))
                                                          : plat::fmt(_("The libraries were not installed:\n%1"), tail),
           {{"OK", nullptr}});
    return;
  }
  run_app(path);   // checked again: what is still missing is shown once more
}

void Desktop::start_tray() {
  if (!tray_pending_) return;
  tray_pending_ = false;
  tray::init(this, [this] { layout_bar(); });
}

void Desktop::restore() {
  spatial_.list_default_once();
  spatial_.apps_icons_once(apps_dir());
  start_tray();
  publish_folder_area();
  later([this] { reconnect_servers(); });   // the remembered servers (Connect at every start)
  g_desk = this;
  // follow changes of the active window (without overwriting the masks FLTK has set)
  XWindowAttributes wa;
  Window rootw = DefaultRootWindow(fl_display);
  XGetWindowAttributes(fl_display, rootw, &wa);
  XSelectInput(fl_display, rootw, wa.your_event_mask | PropertyChangeMask);
  Fl::add_system_handler(x_watch, nullptr);

  int n = 0;
  std::vector<std::string> open = spatial_.open_paths();
  if (explorer_mode()) {   // the explorer windows of the last session, as they were
    auto ex = ex_saved_windows();
    if (!ex.empty()) open = ex;
  }
  for (auto &p : open) {
    std::error_code ec;
    if (fs::is_directory(p, ec) && allowed(p)) { open_folder(p, false, true); n++; }
  }
  if (!n) open_folder(root_path_, false);
}

// ---------------------------------------------------------------------------
// Drawing and events of the desktop
// ---------------------------------------------------------------------------
void Desktop::draw() {
  if (wallpaper_) wallpaper_->draw(0, 0);
  else pattern_->draw(0, 0, w(), h());
  fl_font(FONT_SMALL, SIZE_SMALL);
  for (int i = 0; i < (int)icons_.size(); i++) {
    auto &ic = icons_[i];
    bool sel = i == icon_sel_ || i == icon_drop_;
    Icon shown = ic.icon;
    if (vdragging_ && ic.path == trash_dir()) shown = Icon::Eject;
    if (ic.dock) continue;   // the dock is drawn by draw_dock(): tiles without labels
    if (ic.img) {   // window icon: the program's picture; selected - darkened
      ic.img->draw(ic.x + 24, ic.y);
      if (sel) { fl_color(ICON_SEL); for (int k = 0; k < 32; k += 2) fl_xyline(ic.x + 24, ic.y + k, ic.x + 55); }
    } else if (ic.deskfile) {
      draw_entry_icon(ic.entry, ic.x + 24, ic.y, sel, 32);
    } else {
      draw_icon(shown, ic.x + 24, ic.y, sel);
    }
    // the name in two lines; the selected icon shows all of it
    auto lines = wrap_text(ic.name, 76);
    if (!sel && lines.size() > 2) { lines.resize(2); lines[1] = ellipsize(lines[1] + "…", 76); }
    int ty = ic.y + 36;
    // minimized windows have a yellow label (the color of the minimize button),
    // so they stand out from files, disks and folders whatever the scheme
    bool minimized = ic.win != 0;
    for (auto &nm : lines) {
      int tw = (int)fl_width(nm.c_str());
      int tx = ic.x + 40 - tw / 2;
      fl_color(sel ? ICON_SEL : minimized ? fl_rgb_color(0xF2, 0xB5, 0x26) : CONTENT);
      fl_rectf(tx - 2, ty, tw + 4, fl_height());
      fl_color(sel ? ICON_SEL_TEXT : minimized ? FL_BLACK : CONTENT_TEXT);
      fl_draw(nm.c_str(), tx, ty + fl_height() - fl_descent());
      ty += fl_height();
    }
  }
  draw_dock();
  for (int i = 0; i < children(); i++) draw_child(*child(i));
}

int Desktop::handle(int e) {
  // dock: names in tooltips, the tab collapses and expands the strip
  if (e == FL_MOVE || e == FL_ENTER) {
    int i = icon_at(Fl::event_x(), Fl::event_y());
    static std::string tip;
    if (i >= 0 && icons_[i].dock) {
      if (i != tip_icon_) {
        tip_icon_ = i;
        tip = icons_[i].name;
        Fl_Tooltip::enter_area(this, icons_[i].x + 20, icons_[i].y - 4, DOCK_TILE, DOCK_TILE, tip.c_str());
      }
    } else if (tip_icon_ >= 0) {
      tip_icon_ = -1;
      Fl_Tooltip::enter_area(this, 0, 0, 0, 0, nullptr);
    }
  }
  if (e == FL_LEAVE && tip_icon_ >= 0) {
    tip_icon_ = -1;
    Fl_Tooltip::enter_area(this, 0, 0, 0, 0, nullptr);
  }
  if (e == FL_PUSH && !dock_.empty() && Fl::event_x() < DOCK_TAB && Fl::event_y() >= h() - DOCK_TILE) {
    toggle_dock();
    return 1;
  }
  switch (e) {   // files from other programs dropped on the Trash and disk icons
  case FL_DND_ENTER:
    return 1;
  case FL_DND_DRAG: {
    int i = icon_at(Fl::event_x(), Fl::event_y());
    if (i >= 0 && icons_[i].no_drop()) i = -1;
    if (i != icon_drop_) { icon_drop_ = i; redraw(); }
    return 1;
  }
  case FL_DND_LEAVE:
    if (icon_drop_ >= 0) { icon_drop_ = -1; redraw(); }
    return 1;
  case FL_DND_RELEASE:
    return icon_drop_ >= 0 ? 1 : 0;
  case FL_PASTE:
    if (icon_drop_ >= 0) {
      std::string dest = icons_[icon_drop_].path;
      icon_drop_ = -1;
      redraw();
      if (Fl::event_text()) drop_uris(Fl::event_text(), dest);
      return 1;
    }
    break;
  }
  if (e == FL_SHORTCUT && desk_shortcut()) return 1;   // shortcuts of the selected desktop icon
  if (e == FL_PUSH && Fl::event_y() >= MENUBAR_H && Fl::event_button() == FL_RIGHT_MOUSE) {
    int i = icon_at(Fl::event_x(), Fl::event_y());
    if (tip_icon_ >= 0) { tip_icon_ = -1; Fl_Tooltip::enter_area(this, 0, 0, 0, 0, nullptr); }
    int X = Fl::event_x(), Y = Fl::event_y();
    later([this, i, X, Y] { desk_context_menu(i, X, Y); });
    return 1;
  }
  if (e == FL_PUSH && Fl::event_y() >= MENUBAR_H) {
    int i = icon_at(Fl::event_x(), Fl::event_y());
    icon_sel_ = i;
    vdrag_icon_ = (i >= 0 && icons_[i].vol >= 0) ? i : -1;   // a disk can be dragged to the Trash
    vpress_x_ = Fl::event_x_root(); vpress_y_ = Fl::event_y_root();
    dock_press_ = (i >= 0 && icons_[i].dock) ? i : -1;   // dock: a single click
    if (dock_press_ < 0 && i >= 0 && Fl::event_clicks()) {
      Fl::event_clicks(0);
      vdrag_icon_ = -1;
      if (icons_[i].win) gary::activate(icons_[i].win);   // window icon: restore the window
      else if (icons_[i].prefs) open_preferences();
      else if (icons_[i].deskfile) { Entry en = icons_[i].entry; open_entry(en); }
      else if (icons_[i].vol >= 0) open_volume(icons_[i].vol);
      else if (icons_[i].path == apps_dir() && !icons_[i].dock) open_apps();   // a plain Rambler window
      else open_folder(icons_[i].path);
    } else if (i < 0 && Fl::event_clicks() && Fl::event_button() == FL_LEFT_MOUSE) {
      // double-click on the empty desktop (outside the dock): the file manager - the Nirvana HD window
      int dx, dy, dw, dh;
      dock_rect(dx, dy, dw, dh);
      if (!Fl::event_inside(dx, dy, dw, dh)) {
        Fl::event_clicks(0);
        open_folder(home_);
      }
    }
    if (tip_icon_ >= 0) Fl_Tooltip::enter_area(this, 0, 0, 0, 0, nullptr);
    redraw();
    return 1;
  }
  if (e == FL_RELEASE && dock_press_ >= 0) {
    int i = dock_press_;
    dock_press_ = -1;
    Fl::event_clicks(0);
    if (icon_at(Fl::event_x(), Fl::event_y()) == i) open_entry(icons_[i].entry);
    return 1;
  }
  if (e == FL_DRAG && vdrag_icon_ >= 0) {
    int xr = Fl::event_x_root(), yr = Fl::event_y_root();
    if (!vdragging_ && abs(xr - vpress_x_) + abs(yr - vpress_y_) > 4) {
      vdragging_ = true;
      Entry en;
      en.icon = icons_[vdrag_icon_].icon;
      vghost_ = make_drag_ghost(en, 1);
      vghost_->show();
      redraw();   // the Trash turns into the eject symbol
    }
    if (vdragging_) {
      vghost_->position(xr + 8, yr + 8);
      int over = icon_at(Fl::event_x(), Fl::event_y());
      bool trash = over >= 0 && icons_[over].path == trash_dir();
      int want = trash ? over : -1;
      if (want != icon_drop_) { icon_drop_ = want; redraw(); }
    }
    return 1;
  }
  if (e == FL_RELEASE && vdrag_icon_ >= 0) {
    int src = vdrag_icon_;
    vdrag_icon_ = -1;
    if (vdragging_) {
      vdragging_ = false;
      bool to_trash = icon_drop_ >= 0;
      icon_drop_ = -1;
      vghost_->hide();
      Fl::delete_widget(vghost_);
      vghost_ = nullptr;
      redraw();
      if (to_trash) eject_volume(icons_[src].vol);
    }
    return 1;
  }
  return Fl_Double_Window::handle(e);
}

// ---------------------------------------------------------------------------
// Dialogs - separate modal windows; Insight gives them a frame without buttons
// ---------------------------------------------------------------------------
void Desktop::dialog_cb(Fl_Widget *b, void *idx) {
  auto *p = (NWin *)b->window();
  auto action = p->actions[(long)idx];
  p->desk->close(p);
  if (action) action();
}

void Desktop::guru(const std::string &text) { dialog(GURU_TITLE, text, {{"OK", nullptr}}); }

// The message of an error window: red text in a black box with a blinking red frame,
// and a guru number below - like the error screens of the 1980s home computers.
namespace {
struct GuruBox : Fl_Box {
  bool on = true;
  std::string code;
  GuruBox(int X, int Y, int W, int H, const std::string &text) : Fl_Box(X, Y, W, H) {
    copy_label(text.c_str());
    code = std::string(GURU_TITLE) + " " + guru_number(text);
    Fl::add_timeout(0.8, blink, this);
  }
  ~GuruBox() override { Fl::remove_timeout(blink, this); }
  static void blink(void *d) {
    auto *g = (GuruBox *)d;
    g->on = !g->on;
    g->redraw();
    Fl::repeat_timeout(0.8, blink, d);
  }
  void draw() override {
    Fl_Color red = fl_rgb_color(0xE0, 0x20, 0x10);
    fl_color(FL_BLACK);
    fl_rectf(x(), y(), w(), h());
    fl_color(on ? red : FL_BLACK);
    for (int i = 0; i < 4; i++) fl_rect(x() + 4 + i, y() + 4 + i, w() - 8 - 2 * i, h() - 8 - 2 * i);
    fl_color(red);
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    fl_draw(label(), x() + 18, y() + 16, w() - 36, h() - 52, FL_ALIGN_CENTER | FL_ALIGN_WRAP, nullptr, 0);
    fl_font(FONT_SMALL, SIZE_SMALL + 1);
    fl_draw(code.c_str(), x() + 18, y() + h() - 34, w() - 36, 18, FL_ALIGN_CENTER, nullptr, 0);
  }
};
}  // namespace

void Desktop::dialog(const std::string &title, const std::string &text,
                     std::vector<std::pair<std::string, std::function<void()>>> buttons) {
  bool is_guru = title == GURU_TITLE;
  fl_font(FONT_SYSTEM, SIZE_SYSTEM);
  int tw = 0, th = 0;
  fl_measure(text.c_str(), tw, th, 0);
  if (tw > 440) {   // long messages (e.g. system errors) are wrapped
    tw = 440;
    th = 0;
    fl_measure(text.c_str(), tw, th, 0);
  }
  // each button as wide as its label (Polish "Uruchom ponownie" does not fit in 76 px)
  std::vector<int> bw;
  int all_w = 0;
  for (size_t i = 0; i < buttons.size(); i++) {
    fl_font(i + 1 == buttons.size() ? FONT_SYSTEM : FONT_SMALL, SIZE_SYSTEM);
    int w = std::max(76, (int)fl_width(buttons[i].first.c_str()) + 24);
    bw.push_back(w);
    all_w += w + 8;
  }
  fl_font(FONT_SYSTEM, SIZE_SYSTEM);
  int W = std::max({320, tw + 100, all_w + 24}), H = std::max(130, th + 80);
  if (is_guru) { W = std::max({420, tw + 80, all_w + 24}); H = th + 120; }
  int X = (w() - W) / 2, Y = (h() - H) / 3;

  int ox = csd_mode ? FRAME : 0, oy = csd_mode ? TITLE_H : 0;
  auto *win = new NWin(this, X - ox, Y - oy, W + 2 * ox, H + oy + ox, title.c_str(), true);
  win->color(GRAY);
  // logo icon on the left
  struct Logo : Fl_Widget {
    Logo(int X, int Y) : Fl_Widget(X, Y, 34, 34) {}
    void draw() override { draw_logo(x(), y(), 32); }
  };
  if (is_guru) {
    new GuruBox(ox + 10, oy + 10, W - 20, H - 60, text);
  } else {
    new Logo(ox + 16, oy + 16);
    auto *msg = new Fl_Box(ox + 64, oy + 14, W - 80, th + 8);
    msg->copy_label(text.c_str());
    msg->labelfont(FONT_SYSTEM);
    msg->labelsize(SIZE_SYSTEM);
    msg->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  }

  int bx = ox + W - 16;
  for (int i = (int)buttons.size() - 1; i >= 0; i--) {
    bx -= bw[i] + 8;
    auto *b = new Fl_Button(bx, oy + H - 36, bw[i], 22);
    b->copy_label(buttons[i].first.c_str());
    b->box(BUTTON_BOX);
    b->down_box(BUTTON_DOWN);
    b->color(GRAY);
    b->labelfont(i == (int)buttons.size() - 1 ? FONT_SYSTEM : FONT_SMALL);
    b->labelsize(SIZE_SYSTEM);
    b->callback(dialog_cb, (void *)(long)i);
    if (i == (int)buttons.size() - 1) b->shortcut(FL_Enter);
  }
  for (auto &b : buttons) win->actions.push_back(b.second);
  win->end();
  win->set_modal();
  wins_.push_back(win);
  win->show();
}

// ---------------------------------------------------------------------------
// Menu actions
// ---------------------------------------------------------------------------
// Desktop::about() is in about.cxx

void Desktop::new_folder() {
  NWin *a = active_folder();
  std::string dir = a ? a->path : home_;
  std::string base = dir + "/" + _("untitled folder");
  std::string p = base;
  std::error_code ec;
  for (int i = 2; fs::exists(p, ec); i++) p = base + " " + std::to_string(i);
  fs::create_directory(p, ec);
  refresh_all();
}

void Desktop::open_selected() {
  if (NWin *a = active_folder()) {
    if (const Entry *e = a->pane->view()->selected()) open_entry(*e, a);
  } else if (icon_sel_ >= 0) {
    if (icons_[icon_sel_].win) gary::activate(icons_[icon_sel_].win);
    else if (icons_[icon_sel_].prefs) open_preferences();
    else if (icons_[icon_sel_].vol >= 0) open_volume(icons_[icon_sel_].vol);
    else if (icons_[icon_sel_].dock || icons_[icon_sel_].deskfile) { Entry en = icons_[icon_sel_].entry; open_entry(en); }
    else if (icons_[icon_sel_].path == apps_dir()) open_apps();
    else open_folder(icons_[icon_sel_].path);
  }
}

void Desktop::close_active() {
  if (menu_target && known(menu_target)) { close(menu_target); return; }
  Window a = net_active();
  for (auto *w : wins_)
    if (w->shown() && fl_xid(w) == a) { close(w); return; }
}

// A Trash following the freedesktop.org specification
bool Desktop::trash_path(const std::string &src, std::string &err) {
  std::string files = trash_dir(), info = home_ + "/.local/share/Trash/info";
  std::error_code ec;
  fs::create_directories(files, ec);
  fs::create_directories(info, ec);
  std::string base = fs::path(src).filename().string(), name = base, dest = files + "/" + name;
  for (int i = 2; fs::exists(dest, ec); i++) { name = base + "." + std::to_string(i); dest = files + "/" + name; }
  fs::rename(src, dest, ec);
  if (ec) {   // another file system: copy and delete
    ec.clear();
    fs::copy(src, dest, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (!ec) fs::remove_all(src, ec);
  }
  if (ec) { err = "“" + base + "”: " + ec.message(); return false; }
  char date[32];
  time_t now = time(nullptr);
  strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", localtime(&now));
  std::ofstream(info + "/" + name + ".trashinfo")
      << "[Trash Info]\nPath=" << src << "\nDeletionDate=" << date << "\n";
  path_moved(src, "");
  return true;
}

void Desktop::move_to_trash() {
  NWin *a = active_folder();
  if (!a) return;
  auto sel = a->pane->view()->selection();
  if (sel.empty()) return;
  auto job = std::make_shared<jobs::Job>();   // in the background (another disk = copying)
  job->title = plat::fmt(_("Moving %1 to the Trash"), items_text((int)sel.size()));
  job->files_total = (int)sel.size();
  job->work = [this, sel](jobs::Job &j) {
    for (auto &e : sel) {
      if (!j.check()) break;
      j.set_current(e.name);
      std::string err;
      if (!trash_path(e.path, err)) j.error(err);
      j.files_done++;
    }
  };
  job->on_done = [this](jobs::Job &j) {
    refresh_all();
    if (!j.errors.empty()) dialog(GURU_TITLE, _("Could not move to the Trash:") + std::string("\n") + j.errors, {{"OK", nullptr}});
  };
  jobs::start(job);
}

void Desktop::empty_trash() {
  std::error_code ec;
  int n = 0;
  if (fs::exists(trash_dir(), ec))
    for (auto &x : fs::directory_iterator(trash_dir(), ec)) { (void)x; n++; }
  if (!n) { dialog(_("Trash"), _("The Trash is empty."), {{"OK", nullptr}}); return; }
  dialog(_("Empty Trash"),
         plat::fmt(_("The Trash contains %1.\nDelete them permanently?"), items_text(n)),
         {{_("Cancel"), nullptr},
          {_("Empty"), [this] {
             auto job = std::make_shared<jobs::Job>();
             job->title = _("Emptying the Trash");
             std::vector<std::string> dirs = {trash_dir(), home_ + "/.local/share/Trash/info"};
             job->work = [dirs](jobs::Job &j) {
               std::error_code ec2;
               for (auto &dir : dirs) {
                 if (!fs::exists(dir, ec2)) continue;
                 for (auto &x : fs::directory_iterator(dir, ec2)) {
                   if (!j.check()) return;
                   std::string err;
                   if (!jobs::remove_tree(j, x.path(), err)) j.error(err);
                 }
               }
             };
             job->on_done = [this](jobs::Job &) { refresh_all(); };
             jobs::start(job);
             sounds::play("trash-empty");   // the paper crumpled into the basket
           }}});
}

// The Appearance panel saved new settings: a new palette, desktop pattern and logo.
// Widgets keep palette indexes, so redrawing them is enough.
// The wallpaper at the full screen resolution: at 150% scale the image has 1.5 times
// more pixels than FLTK units, and scale() draws it without blurring.
Fl_RGB_Image *Desktop::render_wallpaper(int W, int H, bool *ok) {
  Fl_RGB_Image *img = render_desktop(current_appearance(), px(W), px(H), ok);
  if (img && ui_scale() != 1.0f) img->scale(W, H, 0, 1);
  return img;
}

void Desktop::appearance_changed() {
  // A new interface scale: the window manager and the desktop have to start again
  // (all Nirvana Desktop programs compute coordinates in the same scale).
  if (std::abs(configured_scale(read_appearance()) - ui_scale()) > 0.01f) {
    reset_nirvana();
    return;
  }
  reload_appearance();
  Fl_Image *old = pattern_ ? pattern_->image() : nullptr;
  pattern_.reset(make_pattern());
  delete old;
  set_logo();
  tray::recolor();
  wallpaper_.reset();
  if (!current_appearance().wallpaper.empty() || current_appearance().background != "pattern") {
    bool ok = false;
    wallpaper_.reset(render_wallpaper(w(), h(), &ok));
    // the wallpaper file is missing or not an image - with the "pattern" background a quick tile is enough
    if (!ok && current_appearance().background == "pattern") wallpaper_.reset();
  }
  // The small 16x16 icons (drawn on the content background) are prepared right away. If they
  // were only made in the menu bar's draw(), the Fl_Image_Surface inside Fl::flush()
  // interrupted refreshing the other windows - they kept the old colors.
  warm_small_icons();
  for (Fl_Window *w = Fl::first_window(); w; w = Fl::next_window(w)) w->redraw();
}

static std::string alice_exe() {
  std::string exe = self_exe();
  std::string alice = exe.substr(0, exe.rfind('/') + 1) + "nirvana-alice";
  if (access(alice.c_str(), X_OK) != 0) alice = "nirvana-alice";   // from PATH
  return alice;
}
void Desktop::open_appearance() { spawn({alice_exe(), "appearance"}); }

static std::string shell_exe() {
  std::string exe = self_exe();
  std::string sh = exe.substr(0, exe.rfind('/') + 1) + "nirvana-shell";
  return access(sh.c_str(), X_OK) == 0 ? sh : "nirvana-shell";
}
// A terminal in the active window's folder (or the selected folder), otherwise in home.
void Desktop::open_terminal() {
  std::string dir = home_;
  if (NWin *w = active_folder()) {
    dir = w->path;
    if (w->pane) {
      auto sel = w->pane->view()->selection();
      if (sel.size() == 1 && sel[0].dir) dir = sel[0].path;
    }
  }
  if (!allowed(dir)) dir = home_;
  spawn({shell_exe(), "--working-directory", dir});
}
void Desktop::open_terminal_home() { spawn({shell_exe(), "--working-directory", home_}); }
void Desktop::open_network() { spawn({alice_exe(), "network"}); }
void Desktop::open_power() { spawn({alice_exe(), "power"}); }

// --- network -------------------------------------------------------------------
// The network menu is built on every opening from the last known state, and we
// ask for a new Wi-Fi scan on the way (the results come through net::watch).
void Desktop::build_net_menu(Fl_Menu_Button *m) {
  m->clear();
  g_menu_nets.clear();
  g_menu_ifaces.clear();
  auto item = [m](const std::string &text, Fl_Callback *cb, long data, int flags) {
    int at = m->add("x", 0, cb, (void *)data, flags);
    m->replace(at, text.c_str());   // replace() does not treat "/" as a submenu
    return at;
  };
  auto self = [](Fl_Widget *w) { return (Desktop *)w->window(); };
  (void)self;
  if (!net_.available) {
    item(net_.problem.empty() ? _("Network unavailable") : net_.problem, nullptr, 0, FL_MENU_INACTIVE | FL_MENU_DIVIDER);
    item(_("Network Settings…"), [](Fl_Widget *w, void *) { ((Desktop *)w->window())->open_network(); }, 0, 0);
    return;
  }
  net::rescan();

  // devices other than Wi-Fi: state and Connect/Disconnect
  for (auto &d : net_.devices) {
    if (d.type == "wifi") continue;
    std::string head = plat::fmt(_("%1 (%2): %3"), net::type_name(d.type), d.iface, net::state_name(d.state));
    item(head, nullptr, 0, FL_MENU_INACTIVE);
    g_menu_ifaces.push_back(d.iface);
    long idx = (long)g_menu_ifaces.size() - 1;
    if (d.state == net::DevState::Connected || d.state == net::DevState::Connecting)
      item(std::string("   ") + _("Disconnect"), [](Fl_Widget *w, void *v) {
        ((Desktop *)w->window())->net_action(false, g_menu_ifaces[(long)v]); }, idx, FL_MENU_DIVIDER);
    else if (d.state == net::DevState::Disconnected)
      item(std::string("   ") + _("Connect"), [](Fl_Widget *w, void *v) {
        ((Desktop *)w->window())->net_action(true, g_menu_ifaces[(long)v]); }, idx, FL_MENU_DIVIDER);
    else
      item(std::string("   ") + _("Cable unplugged"), nullptr, 0, FL_MENU_INACTIVE | FL_MENU_DIVIDER);
  }

  if (net_.wifi_hw) {
    item(!net_.hotspot_ssid.empty() ? plat::fmt(_("Wi-Fi: hotspot “%1”"), net_.hotspot_ssid)
         : net_.wifi_on ? _("Wi-Fi: On") : _("Wi-Fi: Off"), nullptr, 0, FL_MENU_INACTIVE);
    item(net_.wifi_on ? std::string("   ") + _("Turn Wi-Fi Off") : std::string("   ") + _("Turn Wi-Fi On"), [](Fl_Widget *w, void *) {
      Desktop *d = (Desktop *)w->window();
      net::set_wifi(!d->net_state().wifi_on, [d](bool ok, const std::string &msg) {
        d->net_refresh();
        if (!ok) d->dialog_message(_("Network"), msg);
      });
    }, 0, FL_MENU_DIVIDER);
    if (net_.wifi_on) {
      int shown = 0;
      for (auto &n : net_.networks) {
        if (shown++ >= 14) break;
        g_menu_nets.push_back(n);
        std::string text = std::string(1, char('0' + net::bars(n.signal))) + (n.secured() ? "1" : "0") + n.ssid;
        int at = item(text, [](Fl_Widget *w, void *v) {
          ((Desktop *)w->window())->wifi_connect(g_menu_nets[(long)v], "");
        }, (long)g_menu_nets.size() - 1, FL_MENU_RADIO | (n.in_use ? FL_MENU_VALUE : 0));
        ((Fl_Menu_Item *)&m->menu()[at])->labeltype(NET_LABEL);
      }
      if (net_.networks.empty()) item(_("Looking for networks…"), nullptr, 0, FL_MENU_INACTIVE);
      for (auto &d : net_.devices)
        if (d.type == "wifi" && d.state == net::DevState::Connected) {
          g_menu_ifaces.push_back(d.iface);
          item(!net_.hotspot_ssid.empty() ? std::string(_("Turn Hotspot Off")) : plat::fmt(_("Disconnect from “%1”"), d.connection), [](Fl_Widget *w, void *v) {
            ((Desktop *)w->window())->net_action(false, g_menu_ifaces[(long)v]); },
            (long)g_menu_ifaces.size() - 1, 0);
        }
      item(_("Other Network…"), [](Fl_Widget *w, void *) {
        spawn({alice_exe(), "network", "--hidden"}); }, 0, FL_MENU_DIVIDER);
    }
  }
  // VPN
  g_menu_vpns = net_.vpns;
  if (!g_menu_vpns.empty()) {
    item("VPN", nullptr, 0, FL_MENU_INACTIVE);
    for (size_t i = 0; i < g_menu_vpns.size(); i++) {
      auto &v = g_menu_vpns[i];
      std::string t = "   " + v.name + (v.activating ? std::string(" ") + _("(connecting…)") : "");
      item(t, [](Fl_Widget *w, void *d) { ((Desktop *)w->window())->vpn_toggle(g_menu_vpns[(long)d], ""); },
           (long)i, FL_MENU_RADIO | (v.active ? FL_MENU_VALUE : 0) | (i + 1 == g_menu_vpns.size() ? FL_MENU_DIVIDER : 0));
    }
  }
  item(_("Airplane Mode"), [](Fl_Widget *w, void *) {
    Desktop *d = (Desktop *)w->window();
    net::set_airplane(!d->net_state().airplane(), [d](bool ok, const std::string &m) {
      d->net_refresh();
      if (!ok) d->dialog_message(_("Network"), m);
    });
  }, 0, FL_MENU_TOGGLE | (net_.airplane() ? FL_MENU_VALUE : 0) | FL_MENU_DIVIDER);
  item(_("Network Settings…"), [](Fl_Widget *w, void *) { ((Desktop *)w->window())->open_network(); }, 0, 0);
}

// VPN from the network menu: connect or disconnect. The password is asked for only when needed;
// when the profile has no user name, we point to the Network panel.
void Desktop::vpn_toggle(const net::Vpn &v, const std::string &msg) {
  auto done = [this, v](bool ok, const std::string &m) {
    net_refresh();
    if (ok) return;
    if (m == net::NEED_PASSWORD || m == _("Wrong password.")) vpn_toggle(v, m == net::NEED_PASSWORD ? " " : m);
    else dialog_message("VPN", m);
  };
  if (v.active || v.activating) { net::vpn_down(v.uuid, done); return; }
  if (msg.empty()) { net::vpn_up(v.uuid, "", "", done); return; }
  std::string user = net::vpn_username(v.uuid);
  if (user.empty()) { open_network(); return; }   // the user name is needed too
  ask_secret("VPN", Icon::Encrypted, plat::fmt(_("VPN connection “%1” (%2).\nEnter the password."), v.name, user),
             msg == " " ? "" : msg, _("Connect"), [v, done](std::string &p) {
    if (!p.empty()) net::vpn_up(v.uuid, "", p, done);
  });
}

void Desktop::net_refresh() {
  net_ = net::query();
  if (net_menu_) net_menu_->redraw();
}

void Desktop::net_action(bool connect, const std::string &iface) {
  auto done = [this](bool ok, const std::string &msg) { net_refresh(); if (!ok) dialog_message(_("Network"), msg); };
  if (connect) net::connect_device(iface, done);
  else net::disconnect_device(iface, done);
}

void Desktop::dialog_message(const std::string &title, const std::string &text) {
  dialog(title, text, {{"OK", nullptr}});
}

void Desktop::wifi_connect(const net::Network &n, const std::string &msg) {
  if (n.enterprise()) {
    dialog_message(_("Wi-Fi Network"), _("Enterprise networks (WPA Enterprise, 802.1X) are not supported yet.\n"
                                   "Set them up with nmtui or nm-connection-editor."));
    return;
  }
  auto done = [this, n](bool ok, const std::string &m) {
    net_refresh();
    if (ok) return;
    if (m == net::NEED_PASSWORD || m == _("Wrong password.") ||
        m == _("Could not connect. Check the password."))
      wifi_connect(n, m == net::NEED_PASSWORD ? "" : m);   // ask for the password (again)
    else
      dialog_message(_("Wi-Fi Network"), m);
  };
  // a known or open network: no question; on a password error we ask
  if (msg.empty() && (!n.secured() || n.known)) {
    net::connect_wifi(n.ssid, n.security, "", false, done);
    return;
  }
  ask_secret(_("Wi-Fi Network"), Icon::Wifi,
             plat::fmt(_("The network “%1” is secured.\nEnter the password to connect."), n.ssid), msg, _("Connect"),
             [n, done](std::string &p) {
    if (p.empty()) return;
    net::connect_wifi(n.ssid, n.security, p, false, done);
  });
}

void Desktop::refresh_all() {
  for (auto *w : wins_) if (w->pane) w->pane->refresh();
  layout_icons();
  redraw();
}

void Desktop::quit() {
  tray::shutdown();   // XEmbed icons go back to their programs, the tray selection is released
  for (auto *w : wins_) if (w->pane) remember(w, true);
  std::vector<NWin *> all = wins_;
  for (auto *w : all) w->hide();
  hide();
}

// ---------------------------------------------------------------------------
// "Rambler" application menu
// ---------------------------------------------------------------------------
static std::string menu_escape(const std::string &s) {
  std::string r;
  for (char c : s) {
    if (c == '/' || c == '\\' || c == '_') r += '\\';
    if (c == '&') r += '&';
    r += c;
  }
  return r;
}

// ---------------------------------------------------------------------------
// gary: the application menu
// ---------------------------------------------------------------------------
// Rambler windows and the desktop. Not fl_find(): it reorders FLTK's internal
// window list, and this is called from the menu bar's draw() - inside Fl::flush(), which
// walks that list. Some windows were then not redrawn (e.g. after a scheme change).
static bool is_ours(Window w) {
  for (Fl_Window *x = Fl::first_window(); x; x = Fl::next_window(x))
    if (fl_xid(x) == w) return true;
  return false;
}

static Fl_Image *rambler_icon(bool dim) {
  Entry e;
  e.icon = Icon::Folder;
  e.dir = true;
  static std::map<Fl_Image *, Fl_Image *> dimmed;   // separately for every color scheme
  Fl_Image *img = entry_icon_image(e, 16, false);
  if (!dim) return img;
  Fl_Image *&d = dimmed[img];
  if (!d) { d = img->copy(); d->inactive(); }
  return d;
}

static Fl_Image *app_icon(const gary::App &a, bool dim) {
  static std::map<std::string, std::pair<Fl_Image *, Fl_Image *>> cache;
  // the background color in the key: the default icons depend on the color scheme
  std::string key = a.cls + "|" + a.icon_file + "|" + std::to_string(Fl::get_color(CONTENT));
  auto it = cache.find(key);
  if (it == cache.end()) {
    Fl_Image *img = nullptr;
    if (!a.icon_file.empty()) {
      Entry e;
      e.icon_file = a.icon_file;
      e.icon = Icon::Application;
      img = entry_icon_image(e, 16, false);
    }
    if (!img && !a.wins.empty()) img = gary::window_icon(a.wins[0]);
    if (!img) { Entry e; e.icon = Icon::Application; img = entry_icon_image(e, 16, false); }
    Fl_Image *d = img->copy();
    d->inactive();
    it = cache.emplace(key, std::make_pair(img, d)).first;
  }
  return dim ? it->second.second : it->second.first;
}

std::vector<NWin *> Desktop::folder_windows() {
  std::vector<NWin *> v;
  for (auto *w : wins_) if (w->pane && w->shown()) v.push_back(w);
  return v;
}

// The active application: the window from _NET_ACTIVE_WINDOW; the desktop and Rambler windows = Rambler.
int Desktop::active_app_index(const std::vector<gary::App> &apps) {
  Window a = gary::active_window();
  if (!a || is_ours(a)) return -1;
  for (int i = 0; i < (int)apps.size(); i++)
    for (Window w : apps[i].wins) if (w == a) return i;
  return -1;
}

void Desktop::current_app(std::string &name, Fl_Image *&icon) {
  auto apps = gary::list_apps(is_ours);
  int i = active_app_index(apps);
  if (i < 0) { name = "Rambler"; icon = rambler_icon(false); return; }
  name = apps[i].name;
  icon = app_icon(apps[i], false);
}

void Desktop::clients_changed() {
  gary::clients_changed();
  if (app_menu_) app_menu_->redraw();
}

// Menu state at the moment of opening: the actions work on this snapshot.
static std::vector<gary::App> g_menu_apps;
static int g_menu_active = -1;   // -1 = Rambler

static void hide_app(Desktop *d, int i) {
  if (i < 0) { for (NWin *w : d->folder_windows()) gary::hide(fl_xid(w)); return; }
  for (Window w : g_menu_apps[i].wins) gary::hide(w);
}

static void show_app(Desktop *d, int i) {
  if (i < 0) {
    auto f = d->folder_windows();
    for (NWin *w : f) gary::activate(fl_xid(w));
    if (f.empty()) gary::activate(fl_xid(d));
    return;
  }
  for (Window w : g_menu_apps[i].wins) gary::activate(w);   // the last activated one gets the focus
}

void Desktop::build_windows_menu(Fl_Menu_Button *m) {
  m->clear();
  static std::vector<std::unique_ptr<Fl_Multi_Label>> labels;
  static std::vector<std::string> texts;
  labels.clear();
  texts.clear();
  texts.reserve(64);

  g_menu_apps = gary::list_apps(is_ours);
  g_menu_active = active_app_index(g_menu_apps);

  // an item with an icon: icon + text (Fl_Multi_Label)
  auto add_app = [&](const std::string &name, Fl_Image *icon, long idx, bool active, bool last) {
    texts.push_back(" " + name);
    auto ml = std::make_unique<Fl_Multi_Label>();
    ml->labela = (const char *)icon;
    ml->typea = _FL_IMAGE_LABEL;
    ml->labelb = texts.back().c_str();
    ml->typeb = FL_NORMAL_LABEL;
    int flags = FL_MENU_RADIO | (active ? FL_MENU_VALUE : 0) | (last ? FL_MENU_DIVIDER : 0);
    int at = m->add("x", 0,
                    [](Fl_Widget *w, void *d) {
                      auto *self = (Desktop *)w->window();
                      show_app(self, (int)(long)d);
                    },
                    (void *)idx, flags);
    ml->label((Fl_Menu_Item *)&m->menu()[at]);
    labels.push_back(std::move(ml));
  };

  bool rambler_hidden = true;
  for (NWin *w : folder_windows()) if (!gary::is_hidden(fl_xid(w))) rambler_hidden = false;
  if (folder_windows().empty()) rambler_hidden = false;
  add_app("Rambler", rambler_icon(rambler_hidden), -1, g_menu_active < 0, g_menu_apps.empty());
  for (int i = 0; i < (int)g_menu_apps.size(); i++)
    add_app(g_menu_apps[i].name, app_icon(g_menu_apps[i], g_menu_apps[i].hidden), i,
            i == g_menu_active, i + 1 == (int)g_menu_apps.size());

  std::string cur = g_menu_active < 0 ? "Rambler" : g_menu_apps[g_menu_active].name;
  texts.push_back(plat::fmt(_("Hide %1"), menu_escape(cur)));
  m->add(texts.back().c_str(), 0, [](Fl_Widget *w, void *) {
    hide_app((Desktop *)w->window(), g_menu_active);
  }, nullptr, 0);
  m->add(_("Hide Others"), 0, [](Fl_Widget *w, void *) {
    auto *self = (Desktop *)w->window();
    if (g_menu_active >= 0) hide_app(self, -1);
    for (int i = 0; i < (int)g_menu_apps.size(); i++) if (i != g_menu_active) hide_app(self, i);
  }, nullptr, 0);
  m->add(_("Show All"), 0, [](Fl_Widget *w, void *) {
    auto *self = (Desktop *)w->window();
    Window keep = gary::active_window();
    for (NWin *f : self->folder_windows())
      if (gary::is_hidden(fl_xid(f))) gary::activate(fl_xid(f));
    for (auto &a : g_menu_apps)
      for (Window x : a.wins) if (gary::is_hidden(x)) gary::activate(x);
    if (keep) gary::activate(keep);   // the focus goes back where it was
  }, nullptr, FL_MENU_DIVIDER);
  texts.push_back(plat::fmt(_("Quit %1"), menu_escape(cur)));
  m->add(texts.back().c_str(), 0, [](Fl_Widget *, void *) {
    if (g_menu_active >= 0) for (Window x : g_menu_apps[g_menu_active].wins) gary::close(x);
  }, nullptr, (g_menu_active < 0 ? FL_MENU_INACTIVE : 0) | FL_MENU_DIVIDER);

  // Rambler windows in a submenu
  std::vector<NWin *> order;
  for (auto it = wins_.rbegin(); it != wins_.rend(); ++it) if ((*it)->pane) order.push_back(*it);
  if (order.empty())
    m->add(plat::tr_path(P_("Rambler Windows/No open windows")).c_str(), 0, nullptr, nullptr, FL_MENU_INACTIVE | FL_MENU_DIVIDER);
  Window a = net_active();
  for (size_t i = 0; i < order.size(); i++) {
    NWin *w = order[i];
    int flags = FL_MENU_RADIO | (w->shown() && fl_xid(w) == a ? FL_MENU_VALUE : 0);
    if (i + 1 == order.size()) flags |= FL_MENU_DIVIDER;
    texts.push_back(plat::tr_path(P_("Rambler Windows")) + "/" + menu_escape(w->label()));
    m->add(texts.back().c_str(), 0,
           [](Fl_Widget *mw, void *d) {
             auto *self = (Desktop *)mw->window();
             auto *w = (NWin *)d;
             if (!self->known(w)) return;
             if (w->shaded()) w->set_shaded(false);
             w->activate_window();
           },
           w, flags);
  }
  int on = order.empty() ? FL_MENU_INACTIVE : 0;
  m->add(plat::tr_path(P_("Rambler Windows/Collapse All")).c_str(), 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->shade_all(true); }, nullptr, on);
  m->add(plat::tr_path(P_("Rambler Windows/Expand All")).c_str(), 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->shade_all(false); }, nullptr, on | FL_MENU_DIVIDER);
  m->add(plat::tr_path(P_("Rambler Windows/Close All")).c_str(), 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->close_all(); }, nullptr, on);
}

void Desktop::shade_all(bool shade) {
  for (auto *w : wins_)
    if (w->pane && w->shaded() != shade) w->set_shaded(shade);
}

void Desktop::close_all() {
  std::vector<NWin *> v;
  for (auto *w : wins_) if (w->pane) v.push_back(w);
  for (auto *w : v) close(w);
}

// ---------------------------------------------------------------------------
// Renaming, selection, view
// ---------------------------------------------------------------------------
void Desktop::rename_selected() {
  NWin *a = active_folder();
  if (!a) return;
  if (const Entry *e = a->pane->view()->selected())
    a->pane->start_rename(a->pane->view()->index_of(e->path));
}

void Desktop::select_all() {
  if (NWin *a = active_folder()) a->pane->view()->select_all();
}

void Desktop::view_icons() {
  if (NWin *a = active_folder()) { a->pane->set_mode(FolderView::Icons); remember(a, true); }
}

void Desktop::view_list() {
  if (NWin *a = active_folder()) { a->pane->set_mode(FolderView::List); remember(a, true); }
}

std::string Desktop::rename_path(const std::string &old, const std::string &name) {
  if (name.find('/') != std::string::npos) return _("A name cannot contain “/”.");
  if (name == "." || name == "..") return _("That name is reserved.");
  fs::path to = fs::path(old).parent_path() / name;
  std::error_code ec;
  if (fs::exists(to, ec)) return plat::fmt(_("An item named “%1” already exists."), name);
  fs::rename(old, to, ec);
  if (ec) return _("Could not rename:") + std::string("\n") + ec.message();
  path_moved(old, to.string());
  refresh_all();
  if (NWin *w = find(fs::path(old).parent_path().string())) w->pane->view()->select_path(to.string());
  return "";
}

// A folder (or file) moved: fix the open windows and the position memory.
// to == "" means deletion (Trash) - windows of this folder and its subfolders are closed.
void Desktop::path_moved(const std::string &from, const std::string &to) {
  std::vector<NWin *> to_close;
  for (auto *w : wins_) {
    if (!w->pane) continue;
    bool same = w->path == from, inside = w->path.rfind(from + "/", 0) == 0;
    if (!same && !inside) continue;
    if (to.empty()) { to_close.push_back(w); continue; }
    std::string np = to + w->path.substr(from.size());
    WinState s;
    if (spatial_.get(w->path, s)) spatial_.set(np, s);
    w->path = np;
    w->pane->set_path(np);
    w->copy_label(title_for(np).c_str());
  }
  for (auto *w : to_close) close(w);
  // the dock follows the moved file (e.g. an AppImage installed to Applications/Games)
  bool dock_changed = false;
  for (auto &p : dock_) {
    if (p != from && p.rfind(from + "/", 0) != 0) continue;
    if (!to.empty()) { p = to + p.substr(from.size()); dock_changed = true; }
  }
  if (dock_changed) { save_dock(); layout_icons(); redraw(); }
}

// ---------------------------------------------------------------------------
// Moving and copying
// ---------------------------------------------------------------------------
static std::string copy_name(const fs::path &dir, const std::string &name) {
  fs::path p(name);
  std::string stem = p.stem().string(), ext = p.extension().string();
  if (stem.empty()) { stem = name; ext.clear(); }
  std::error_code ec;
  std::string n = stem + " kopia" + ext;
  for (int i = 2; fs::exists(dir / n, ec); i++) n = stem + " kopia " + std::to_string(i) + ext;
  return n;
}

void Desktop::transfer(const std::vector<Entry> &items, const std::string &dest, bool copy, bool replace) {
  if (!allowed(dest)) {
    dialog(GURU_TITLE, _("The destination folder is outside Nirvana HD."), {{"OK", nullptr}});
    return;
  }
  std::error_code ec;
  // a folder into itself or into its own subfolder
  for (auto &e : items) {
    if (e.dir && (dest == e.path || dest.rfind(e.path + "/", 0) == 0)) {
      dialog(GURU_TITLE, plat::fmt(_("The folder “%1” cannot be moved into itself."), e.label),
             {{"OK", nullptr}});
      return;
    }
  }
  // name conflicts (copying within the same folder is skipped - it gets "copy")
  if (!replace) {
    std::vector<std::string> clash;
    for (auto &e : items) {
      bool same_dir = fs::path(e.path).parent_path() == fs::path(dest);
      if (same_dir) continue;
      if (fs::exists(fs::path(dest) / e.name, ec)) clash.push_back(e.name);
    }
    if (!clash.empty()) {
      std::string what = clash.size() == 1 ? plat::fmt(_("an item “%1”"), clash[0])
                                           : items_text((int)clash.size());
      dialog(copy ? _("Copying") : _("Move"),
             plat::fmt(_("The folder “%1” already contains %2.\nReplace?"), title_for(dest), what),
             {{_("Cancel"), nullptr},
              {_("Replace"), [this, items, dest, copy] { transfer(items, dest, copy, true); }}});
      return;
    }
  }
  // the work itself in the background: windows keep working, jobs run side by side
  auto job = std::make_shared<jobs::Job>();
  job->title = plat::fmt(copy ? _("Copying %1 to “%2”") : _("Moving %1 to “%2”"), items_text((int)items.size()), title_for(dest));
  std::string apps = apps_dir();
  job->work = [items, dest, copy, replace](jobs::Job &j) {
    std::error_code ec;
    uint64_t total = 0;
    int files = 0;
    for (auto &e : items) total += jobs::measure(e.path, files);
    j.total = total;
    j.files_total = files;
    for (auto &e : items) {
      if (!j.check()) break;
      fs::path src = e.path, dst = fs::path(dest) / e.name;
      bool same_dir = src.parent_path() == fs::path(dest);
      if (same_dir && !copy) continue;                      // moving to the same place
      if (same_dir && copy) dst = fs::path(dest) / copy_name(dest, e.name);
      ec.clear();
      if (fs::exists(dst, ec) && replace && !same_dir) fs::remove_all(dst, ec);
      std::string err;
      bool ok = copy ? jobs::copy_tree(j, src, dst, err) : jobs::move_tree(j, src, dst, err);
      if (!ok) { if (!err.empty()) j.error("“" + e.name + "”: " + err); continue; }
      std::lock_guard<std::mutex> l(j.m);
      if (!copy) j.moved.push_back({src.string(), dst.string()});
      j.made.push_back(dst.string());
    }
  };
  job->on_done = [this, apps](jobs::Job &j) {
    for (auto &[from, to] : j.moved) path_moved(from, to);
    // An AppImage dropped into Applications is ready to run (installing by dragging)
    // and goes straight to the folder of its category (Games, Graphics...)
    std::error_code ec;
    for (auto &d : j.made) {
      fs::path dst = d;
      if (!is_appimage(dst.filename().string()) || dst.parent_path() != fs::path(apps)) continue;
      chmod(dst.c_str(), 0755);
      fs::path cat = app_destination(dst.string());
      if (!fs::exists(cat, ec)) {
        fs::create_directories(cat.parent_path(), ec);
        fs::rename(dst, cat, ec);
        if (!ec) path_moved(dst.string(), cat.string());
        ec.clear();
      }
    }
    refresh_all();
    if (j.cancel) return;
    if (!j.errors.empty()) dialog(GURU_TITLE, _("There were errors:") + std::string("\n") + j.errors, {{"OK", nullptr}});
  };
  jobs::start(job);
}

// ---------------------------------------------------------------------------
// Dragging
// ---------------------------------------------------------------------------
// The "ghost" of the dragged items: a small frameless window following the cursor.
class DragGhost : public Fl_Double_Window {
public:
  DragGhost(const Entry &e, int n) : Fl_Double_Window(0, 0, 40, 40), entry_(e), n_(n) {
    clear_border();
    set_override();     // outside the window manager, always on top
    end();
  }
  void draw() override {
    fl_color(GRAY);
    fl_rectf(0, 0, w(), h());
    fl_color(SHADOW);
    fl_rect(0, 0, w(), h());
    draw_entry_icon(entry_, 4, 4, true, 32);
    if (n_ > 1) {   // count of the dragged items
      std::string s = std::to_string(n_);
      fl_font(FONT_SYSTEM, 10);
      int bw = (int)fl_width(s.c_str()) + 8;
      fl_color(ALERT);
      fl_rounded_rectf(w() - bw - 1, 1, bw, 13, 6);
      fl_color(FL_WHITE);
      fl_draw(s.c_str(), w() - bw + 3, 11);
    }
  }
private:
  Entry entry_;
  int n_;
};

Fl_Window *make_drag_ghost(const Entry &e, int n) { return new DragGhost(e, n); }

// Is window w (indirectly) inside top? Needed because the window manager
// wraps our windows in frames.
static bool is_under(Window top, Window w) {
  Display *d = fl_display;
  Window root = DefaultRootWindow(d);
  for (int depth = 0; w && w != root && depth < 16; depth++) {
    if (w == top) return true;
    Window r, parent, *kids = nullptr;
    unsigned n = 0;
    if (!XQueryTree(d, w, &r, &parent, &kids, &n)) return false;
    if (kids) XFree(kids);
    w = parent;
  }
  return false;
}

// The pointer over a window of another program - not a folder window, the desktop, the dock or
// a menu of Rambler (every window of this program is "ours")
bool Desktop::pointer_over_foreign() const {
  Window rootret, child = 0;
  int rx, ry, wx, wy;
  unsigned mask;
  XQueryPointer(fl_display, DefaultRootWindow(fl_display), &rootret, &child, &rx, &ry, &wx, &wy, &mask);
  if (!child) return false;
  for (Fl_Window *fw = Fl::first_window(); fw; fw = Fl::next_window(fw))
    if (fw->shown() && is_under(child, fl_xid(fw))) return false;
  return true;
}

// Files dragged out of Rambler into another program (a file manager, a browser, an editor):
// our drag ends, the standard one (XDND, text/uri-list) carries the files on
void Desktop::drag_out() {
  std::vector<Entry> items = drag_.items;
  NWin *src = drag_.src;
  FolderPane *src_pane = drag_.src_pane;
  clear_drop_highlight();
  if (drag_.ghost) { drag_.ghost->hide(); Fl::delete_widget(drag_.ghost); drag_.ghost = nullptr; }
  drag_.active = false;
  if (known(src) && src_pane) src_pane->view()->cancel_drag();
  std::string uris;
  static const char *hex = "0123456789ABCDEF";
  for (auto &e : items) {
    uris += "file://";
    for (unsigned char c : e.path) {
      if (isalnum(c) || strchr("/-_.~!$&'()*+,;=:@", c)) uris += (char)c;
      else { uris += '%'; uris += hex[c >> 4]; uris += hex[c & 15]; }
    }
    uris += "\r\n";
  }
  Fl::copy(uris.c_str(), (int)uris.size(), 0);   // the drag-and-drop buffer
  Fl::dnd();                                       // until the button is let go
}

Desktop::DropTarget Desktop::target_at(int xr, int yr) {
  DropTarget t;
  Window rootret, child = 0;
  int rx, ry, wx, wy;
  unsigned mask;
  XQueryPointer(fl_display, DefaultRootWindow(fl_display), &rootret, &child, &rx, &ry, &wx, &wy, &mask);
  if (!child) return t;
  for (auto *w : wins_) {
    if (!w->pane || !w->shown() || !is_under(child, fl_xid(w))) continue;
    t.win = w;
    int lx = xr - w->x(), ly = yr - w->y();
    t.tree = ex_tree_target(w, lx, ly);   // a folder, disk or the Trash in the explorer's tree
    if (!t.tree.empty()) return t;
    FolderPane *p = w->pane;
    if (w->ex) if (FolderPane *q = ex_pane_at(w, lx, ly)) p = q;   // the explorer pane under the pointer
    t.pane = p;
    Fl_Widget *area = p->view()->parent();   // the visible part of the file list
    bool inside = area && lx >= area->x() && ly >= area->y() &&
                  lx < area->x() + area->w() && ly < area->y() + area->h();
    t.item = (inside && !w->shaded()) ? p->view()->drop_target_at(lx, ly) : -1;
    return t;
  }
  if (is_under(child, fl_xid(this))) t.icon = icon_at(xr - x_root(), yr - y_root());
  if (t.icon >= 0 && icons_[t.icon].no_drop()) t.icon = -1;   // files cannot be dropped on a window icon
  return t;
}

void Desktop::clear_drop_highlight() {
  ex_tree_mark(nullptr, "");
  if (drag_.hl_win && known(drag_.hl_win) && drag_.hl_pane) drag_.hl_pane->view()->set_drop_target(-2);
  drag_.hl_win = nullptr;
  drag_.hl_pane = nullptr;
  if (icon_drop_ >= 0) { icon_drop_ = -1; redraw(); }
}

void Desktop::drag_start(NWin *src, const std::vector<Entry> &items) {
  if (items.empty()) return;
  drag_.active = true;
  drag_.src = src;
  drag_.src_pane = src->pane;
  drag_.items = items;
  drag_.ghost = new DragGhost(items[0], (int)items.size());
  drag_.px = src->x_root() + src->pane->view()->press_x();
  drag_.py = src->y_root() + src->pane->view()->press_y();
  drag_.ghost->position(Fl::event_x_root() + 8, Fl::event_y_root() + 8);
  drag_.ghost->show();
}

void Desktop::drag_move(int xr, int yr) {
  if (!drag_.active) return;
  // the ghost is offset from the cursor, so it does not cover the target under the cursor
  drag_.ghost->position(xr + 8, yr + 8);
  if (pointer_over_foreign()) { drag_out(); return; }   // into another program: XDND
  DropTarget t = target_at(xr, yr);
  clear_drop_highlight();
  if (!t.tree.empty()) {
    ex_tree_mark(t.win, t.tree);
  } else if (t.win) {
    // the same folder as the source (without pointing at a subfolder) is not a target
    bool same = t.pane == drag_.src_pane && t.item < 0;
    if (!same) { t.pane->view()->set_drop_target(t.item); drag_.hl_win = t.win; drag_.hl_pane = t.pane; }
  } else if (t.icon >= 0) {
    icon_drop_ = t.icon;
    redraw();
  }
}

void Desktop::drag_end(int xr, int yr, bool copy) {
  if (!drag_.active) return;
  DropTarget t = target_at(xr, yr);
  clear_drop_highlight();
  Rect ghost = {drag_.ghost->x(), drag_.ghost->y(), drag_.ghost->w(), drag_.ghost->h()};
  drag_.ghost->hide();
  Fl::delete_widget(drag_.ghost);
  drag_.ghost = nullptr;
  drag_.active = false;
  std::vector<Entry> items = drag_.items;
  NWin *src = drag_.src;

  std::string dest;
  bool same_pane = t.pane && t.pane == drag_.src_pane;
  if (!t.tree.empty()) dest = t.tree;
  else if (t.win) dest = t.item >= 0 ? t.pane->view()->entry(t.item)->path : t.pane->path();
  else if (t.icon >= 0) {
    dest = icons_[t.icon].path;
    if (int vi = icons_[t.icon].vol; vi >= 0) {
      copy = true;   // to another disk we copy
      if (dest.empty()) {
        if (vols_[vi].encrypted && !vols_[vi].unlocked) {
          dialog(_("Encrypted Disk"), plat::fmt(_("Open the disk “%1” and enter its password first."), vols_[vi].name),
                 {{"OK", nullptr}});
          return;
        }
        std::string mp, err = vol::mount(vols_[vi], mp);
        if (!err.empty()) { dialog(GURU_TITLE, err, {{"OK", nullptr}}); return; }
        rescan_volumes();
        dest = mp;
      }
      if (fs::path(dest) == fs::path(trash_dir())) dest.clear();
    }
  }

  // Dropping on the background of the same window in icon view: moving the icons
  if (same_pane && t.item < 0 && !copy && known(src) &&
      src->pane->view()->mode() == FolderView::Icons) {
    FolderView *v = src->pane->view();
    std::vector<std::string> paths;
    for (auto &e : items) paths.push_back(e.path);
    v->move_items(paths, xr - drag_.px, yr - drag_.py);   // as far as the mouse moved
    src->pane->resize(src->pane->x(), src->pane->y(), src->pane->w(), src->pane->h());
    src->pane->redraw();
    return;
  }
  if (dest.empty() || (same_pane && t.item < 0 && !copy)) {
    // a missed drop: the icon "flies back" to its place
    Rect back;
    if (known(src) && src->pane->view()->icon_rect(items[0].path, back)) {
      back = {back.x + src->x(), back.y + src->y(), back.w, back.h};
      Fl::flush();
      zoom_rects(ghost, back);
    }
    return;
  }
  if (dest == trash_dir()) {
    std::string errors;
    for (auto &e : items) {
      std::string err;
      if (!trash_path(e.path, err)) errors += err + "\n";
    }
    refresh_all();
    if (!errors.empty()) dialog(GURU_TITLE, _("Could not move to the Trash:") + std::string("\n") + errors, {{"OK", nullptr}});
    return;
  }
  transfer(items, dest, copy, false);
}

// ---------------------------------------------------------------------------
// Copy / Paste / Duplicate
// ---------------------------------------------------------------------------
static Entry entry_for(const std::string &path) {
  Entry e;
  e.path = path;
  e.name = fs::path(path).filename().string();
  std::error_code ec;
  e.dir = fs::is_directory(path, ec);
  e.label = e.name;
  e.icon = icon_for_path(path, e.dir);
  if (!e.dir && is_appimage(e.name)) {   // name and icon from inside the AppImage, as in windows
    const char *h = getenv("HOME");
    e.label = appimage::label(path, h ? h : "/");
    e.icon_file = appimage::icon(path, h ? h : "/");
  }
  return e;
}

static std::string uri_encode(const std::string &path) {
  static const char *hex = "0123456789ABCDEF";
  std::string r = "file://";
  for (unsigned char c : path) {
    if (isalnum(c) || strchr("/-_.~", c)) r += (char)c;
    else { r += '%'; r += hex[c >> 4]; r += hex[c & 15]; }
  }
  return r;
}

void Desktop::duplicate() {
  NWin *a = active_folder();
  if (!a) return;
  auto items = a->pane->view()->selection();
  if (!items.empty()) transfer(items, a->path, true, false);   // in the same folder -> "copy"
}

void Desktop::copy_selected() {
  NWin *a = active_folder();
  if (!a) return;
  auto items = a->pane->view()->selection();
  if (items.empty()) return;
  clipboard_.clear();
  std::string uris;
  for (auto &e : items) {
    clipboard_.push_back(e.path);
    uris += uri_encode(e.path) + "\r\n";
  }
  // also for other programs: the system clipboard as a list of file addresses
  Fl::copy(uris.c_str(), (int)uris.size(), 1, Fl::clipboard_plain_text);
}

void Desktop::paste() {
  std::string dest = active_folder() ? active_folder()->path : home_;
  std::vector<Entry> items;
  std::error_code ec;
  for (auto &p : clipboard_) if (fs::exists(p, ec)) items.push_back(entry_for(p));
  if (items.empty()) return;
  transfer(items, dest, true, false);
}

static std::string uri_decode(const std::string &s) {
  std::string r;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) &&
        isxdigit((unsigned char)s[i + 2])) {
      r += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16);
      i += 2;
    } else r += s[i];
  }
  return r;
}

void Desktop::drop_uris(const std::string &list, const std::string &dest) {
  std::vector<Entry> items;
  std::error_code ec;
  size_t pos = 0;
  while (pos < list.size()) {
    size_t nl = list.find('\n', pos);
    std::string line = list.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? list.size() : nl + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::string path;
    if (line.rfind("file://", 0) == 0) {
      path = uri_decode(line.substr(7));
      if (!path.empty() && path[0] != '/') path = path.substr(path.find('/'));   // file://host/...
    } else if (line[0] == '/') {
      path = line;   // some programs give a plain path
    } else {
      continue;
    }
    if (fs::exists(path, ec)) items.push_back(entry_for(path));
  }
  if (items.empty()) return;
  if (dest == trash_dir()) {
    std::string errors;
    for (auto &e : items) { std::string err; if (!trash_path(e.path, err)) errors += err + "\n"; }
    refresh_all();
    if (!errors.empty()) dialog(GURU_TITLE, _("Could not move to the Trash:") + std::string("\n") + errors, {{"OK", nullptr}});
    return;
  }
  // from another program: we copy (like dragging between disks)
  transfer(items, dest, true, false);
}

// ---------------------------------------------------------------------------
// Disks, discs and encrypted volumes
// ---------------------------------------------------------------------------
// Changes: new/removed devices (/dev), mounts (/proc/self/mounts)
// and every few seconds (an inserted disc creates no new device).
void Desktop::watch_volumes() {
  vols_ = vol::scan();
  g_disk_names.clear();
  for (auto &v : vols_)
    if (!v.mountpoint.empty() && v.name.find('/') == std::string::npos) g_disk_names.push_back({v.mountpoint, v.name});
  layout_icons();
  int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd >= 0 && inotify_add_watch(fd, "/dev", IN_CREATE | IN_DELETE) >= 0) {
    Fl::add_fd(fd, FL_READ, [](int f, void *d) {
      char buf[4096];
      while (read(f, buf, sizeof buf) > 0) {}
      Fl::remove_timeout(rescan_later, d);
      Fl::add_timeout(0.8, rescan_later, d);   // udev needs a moment to recognize the disk
    }, this);
  }
  int mfd = open("/proc/self/mounts", O_RDONLY | O_CLOEXEC);
  if (mfd >= 0) {
    Fl::add_fd(mfd, FL_EXCEPT, [](int, void *d) {
      Fl::remove_timeout(rescan_later, d);
      Fl::add_timeout(0.3, rescan_later, d);
    }, this);
  }
  Fl::add_timeout(4.0, poll_volumes, this);
}

void Desktop::poll_volumes(void *d) {
  ((Desktop *)d)->rescan_volumes();
  Fl::repeat_timeout(4.0, poll_volumes, d);
}

void Desktop::rescan_later(void *d) { ((Desktop *)d)->rescan_volumes(); }

void Desktop::rescan_volumes() {
  auto now = vol::scan();
  auto sig = [](const std::vector<vol::Volume> &v) {
    std::string s;
    for (auto &x : v) s += x.uuid + "|" + x.mountpoint + "|" + x.name + "|" + (x.unlocked ? "1" : "0") + ";";
    return s;
  };
  if (sig(now) == sig(vols_)) return;
  // the disk disappeared or was unmounted: close the windows with its content
  for (auto &old : vols_) {
    if (old.mountpoint.empty()) continue;
    bool still = false;
    for (auto &n : now) if (n.mountpoint == old.mountpoint) still = true;
    if (!still) path_moved(old.mountpoint, "");
  }
  std::string sel_path = icon_sel_ >= 0 && icon_sel_ < (int)icons_.size() ? icons_[icon_sel_].name : "";
  vols_ = now;
  g_disk_names.clear();
  for (auto &v : vols_)
    if (!v.mountpoint.empty() && v.name.find('/') == std::string::npos) g_disk_names.push_back({v.mountpoint, v.name});
  layout_icons();
  icon_sel_ = -1;
  for (int i = 0; i < (int)icons_.size(); i++) if (!sel_path.empty() && icons_[i].name == sel_path) icon_sel_ = i;
  redraw();
  ex_refresh_trees();   // the explorer windows' trees show the disks too
}

void Desktop::open_volume(int vi) {
  if (vi < 0 || vi >= (int)vols_.size()) return;
  vol::Volume v = vols_[vi];
  if (v.encrypted && !v.unlocked) { ask_password(v, ""); return; }
  std::string mp = v.mountpoint;
  if (mp.empty()) {
    cursor(FL_CURSOR_WAIT);
    Fl::flush();
    std::string err = vol::mount(v, mp);
    cursor(FL_CURSOR_DEFAULT);
    if (!err.empty()) { dialog(GURU_TITLE, err, {{"OK", nullptr}}); return; }
    rescan_volumes();
  }
  if (!mp.empty()) open_folder(mp);
}

// Password for a LUKS disk: a separate dialog with a hidden field (Fl_Secret_Input).
// A general window with a password field (encrypted disk, Wi-Fi network).
void Desktop::ask_secret(const std::string &title, Icon icon, const std::string &text,
                         const std::string &msg, const char *ok_label,
                         std::function<void(std::string &)> on_ok) {
  int ox = csd_mode ? FRAME : 0, oy = csd_mode ? TITLE_H : 0;
  int W = 380, H = msg.empty() ? 150 : 170;
  int X = (w() - W) / 2 + x_root(), Y = (h() - H) / 3 + y_root();
  auto *win = new NWin(this, X - ox, Y - oy, W + 2 * ox, H + oy + ox, "", true);
  win->copy_label(title.c_str());
  win->color(GRAY);
  struct Pic : Fl_Widget {
    Icon ic;
    Pic(int X, int Y, Icon i) : Fl_Widget(X, Y, 34, 34), ic(i) {}
    void draw() override { draw_icon(ic, x(), y(), false); }
  };
  new Pic(ox + 16, oy + 16, icon);
  auto *msgbox = new Fl_Box(ox + 64, oy + 12, W - 80, 36);
  msgbox->copy_label(text.c_str());
  msgbox->labelfont(FONT_SYSTEM);
  msgbox->labelsize(SIZE_SYSTEM);
  msgbox->labelcolor(TEXT);
  msgbox->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  int iy = oy + 56;
  if (!msg.empty()) {   // e.g. "Incorrect password."
    auto *err = new Fl_Box(ox + 64, iy, W - 80, 18);
    err->copy_label(msg.c_str());
    err->labelfont(FONT_SYSTEM);
    err->labelsize(SIZE_SMALL);
    err->labelcolor(ALERT);
    err->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    iy += 20;
  }
  auto *pass = new Fl_Secret_Input(ox + 64, iy, W - 80, 24);
  pass->box(WELL_BOX);
  pass->textfont(FONT_SMALL);
  pass->textsize(SIZE_SYSTEM);
  auto button_w = [](const char *label, bool def) {
    fl_font(def ? FONT_SYSTEM : FONT_SMALL, SIZE_SYSTEM);
    return std::max(76, (int)fl_width(label) + 24);
  };
  int okw = button_w(ok_label, true), cw = button_w(_("Cancel"), false);
  int bx = ox + W - 16 - okw;
  auto make_button = [&](int X2, const char *label, bool def, int idx) {
    auto *b = new Fl_Button(X2, oy + H - 36, button_w(label, def), 22, label);
    b->box(BUTTON_BOX);
    b->down_box(BUTTON_DOWN);
    b->color(GRAY);
    b->labelfont(def ? FONT_SYSTEM : FONT_SMALL);
    b->labelsize(SIZE_SYSTEM);
    b->callback(dialog_cb, (void *)(long)idx);
    if (def) b->shortcut(FL_Enter);
  };
  make_button(bx - 8 - cw, _("Cancel"), false, 0);
  make_button(bx, ok_label, true, 1);
  win->actions.push_back(nullptr);
  win->actions.push_back([pass, on_ok] {
    std::string p = pass->value();
    // clear the field right away - the password should not linger in the widget
    pass->value("");
    on_ok(p);
    std::fill(p.begin(), p.end(), '\0');
  });
  win->end();
  win->set_modal();
  wins_.push_back(win);
  win->show();
  pass->take_focus();
}

void Desktop::ask_password(const vol::Volume &v, const std::string &msg) {
  std::string uuid = v.uuid;
  ask_secret(_("Encrypted Disk"), Icon::Encrypted,
             plat::fmt(_("The disk “%1” is encrypted.\nEnter the password to open it."), v.name), msg, _("Open"),
             [this, uuid](std::string &p) {
    int vi = -1;
    for (int i = 0; i < (int)vols_.size(); i++) if (vols_[i].uuid == uuid) vi = i;
    if (vi < 0) return;
    vol::Volume v = vols_[vi];
    cursor(FL_CURSOR_WAIT);
    Fl::flush();
    std::string err = vol::unlock(v, p);
    cursor(FL_CURSOR_DEFAULT);
    if (!err.empty()) {
      if (err == _("Wrong password.")) ask_password(v, err);   // once more
      else dialog(_("Encrypted Disk"), err, {{"OK", nullptr}});
      return;
    }
    rescan_volumes();
    for (int i = 0; i < (int)vols_.size(); i++)
      if (vols_[i].uuid == uuid) { open_volume(i); break; }
  });
}

void Desktop::eject_volume(int vi) {
  if (vi < 0 || vi >= (int)vols_.size()) return;
  vol::Volume v = vols_[vi];
  // first close the windows with the disk's content
  if (!v.mountpoint.empty()) path_moved(v.mountpoint, "");
  cursor(FL_CURSOR_WAIT);
  Fl::flush();
  std::string err = vol::eject(v);
  cursor(FL_CURSOR_DEFAULT);
  rescan_volumes();
  if (!err.empty()) dialog(GURU_TITLE, err, {{"OK", nullptr}});
}

void Desktop::eject_selected() {
  if (icon_sel_ >= 0 && icon_sel_ < (int)icons_.size() && icons_[icon_sel_].vol >= 0) {
    eject_volume(icons_[icon_sel_].vol);
    return;
  }
  if (NWin *a = active_folder()) {   // the active window with the disk's content
    int vi = volume_at_path(a->path);
    if (vi >= 0) eject_volume(vi);
  }
}
