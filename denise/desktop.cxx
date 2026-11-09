// LisaOS - denise: biurko, pasek menu i zarządzanie oknami menedżera plików
#include "desktop.h"
#include "applets.h"
#include "appimage.h"
#include "gary.h"
#include "tray.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Tooltip.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Multi_Label.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <fcntl.h>

namespace fs = std::filesystem;
using namespace plat;

// ---------------------------------------------------------------------------
// Uruchamianie programów (AppImage, xdg-open, pkexec)
// ---------------------------------------------------------------------------
void rambler_spawn(const std::vector<std::string> &args);
static void spawn(const std::vector<std::string> &args) { rambler_spawn(args); }
void rambler_spawn(const std::vector<std::string> &args) {
  std::vector<const char *> argv;
  for (auto &a : args) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  appimage::spawn(argv.data());    // podwójny fork - bez procesów zombie
}

static std::string self_exe() {
  std::error_code ec;
  return fs::read_symlink("/proc/self/exe", ec).string();
}
// program LisaOS obok lisaos-denise (build/ albo prefiks), inaczej z PATH
std::string rambler_exe(const char *name) {
  std::string exe = self_exe();
  std::string p = exe.substr(0, exe.rfind('/') + 1) + name;
  return access(p.c_str(), X_OK) == 0 ? p : name;
}

// ---------------------------------------------------------------------------
// Prawa strona paska menu: plakietka ROOT i zegar
// ---------------------------------------------------------------------------
class MenuExtras : public Fl_Widget {
public:
  MenuExtras(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {
    Fl::add_timeout(1.0, tick, this);
  }
  // szerokość dopasowana do treści - aplety stoją ciasno przy prawej krawędzi
  static int needed_width() {
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    int w = (int)fl_width("00:00") + 18;
    if (root_mode()) w += (int)fl_width("ROOT") + 18;
    return w;
  }
  static void tick(void *d) {
    ((Fl_Widget *)d)->redraw();
    Fl::repeat_timeout(15.0, tick, d);
  }
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    draw_box(MENUBAR_BOX, GRAY);
    fl_pop_clip();
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    char buf[16];
    time_t t = time(nullptr);
    strftime(buf, sizeof buf, "%H:%M", localtime(&t));
    int cx = x() + w() - 9 - (int)fl_width(buf);
    fl_color(TEXT);
    fl_draw(buf, cx, y() + 14);
    if (root_mode()) {
      const char *r = "ROOT";
      int rw = (int)fl_width(r) + 10;
      cx -= rw + 8;
      fl_color(ROOT_TINT);
      fl_rounded_rectf(cx, y() + 3, rw, h() - 7, 3);
      fl_color(FL_WHITE);
      fl_draw(r, cx + 5, y() + 14);
    }
  }
};

// ---------------------------------------------------------------------------
// Środek paska: wolna pamięć i miejsce na dysku, jak na pasku ekranu
// Workbencha 3.1 ("Workbench Screen  1,234,567 graphics mem  ...").
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
    std::string mem = group(mem_kb / 1024) + " MB";
    std::string dsk = group(disk_mb) + " MB";
    std::string full = plat::fmt(_("%1 wolnej pamięci     %2 wolnego dysku"), mem, dsk);
    std::string brief = plat::fmt(_("%1 pamięci   %2 dysku"), mem, dsk);
    std::string tip = plat::fmt(_("Pamięć: wolne %1 z %2"), mem, group(mem_total / 1024) + " MB");
    if (disk_ok) tip += "\n" + plat::fmt(_("Dysk (%1): wolne %2 z %3"), disk_, gb(disk_mb), gb(disk_total));
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
      // na środku ekranu, a gdy tam się nie mieści - na środku wolnego miejsca
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
// Menu aplikacji "Rambler": lista otwartych folderów i operacje na oknach.
// Docelowo (lisaos-gary) stanie się przełącznikiem aplikacji.
// ---------------------------------------------------------------------------
// Pasek menu, który przed otwarciem odświeża stan pozycji (aktywne/nieaktywne).
class LisaMenuBar : public Fl_Menu_Bar {
public:
  std::function<void()> before_open;
  LisaMenuBar(int X, int Y, int W, int H) : Fl_Menu_Bar(X, Y, W, H) {}
  int handle(int e) override {
    if ((e == FL_PUSH || e == FL_SHORTCUT || e == FL_KEYBOARD) && before_open) before_open();
    return Fl_Menu_Bar::handle(e);
  }
};

static const int APP_W = 34;   // szerokość przycisku aplikacji (gary) w prawym rogu

class WindowsMenu : public Fl_Menu_Button {
public:
  std::function<void(WindowsMenu *)> on_open;
  // aktywna aplikacja: nazwa i ikona 16x16 (gary)
  std::function<void(std::string &, Fl_Image *&)> current;
  WindowsMenu(int X, int Y, int W, int H) : Fl_Menu_Button(X, Y, W, H) {
    box(FL_FLAT_BOX);   // FL_NO_BOX zamieniłby przycisk w menu prawego klawisza
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
    if (e == FL_ENTER) {   // podpowiedź: aktywna aplikacja
      std::string name = "Rambler";
      Fl_Image *icon = nullptr;
      if (current) current(name, icon);
      copy_tooltip(plat::fmt(_("Aplikacje - teraz: %1"), name).c_str());
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
    // Ukłon w stronę Amigi: zamiast nazwy aplikacji gadżet głębi z paska ekranu
    // Workbencha (dwa nachodzące prostokąty). Nazwa jest w podpowiedzi i w menu.
    draw_depth_gadget(x() + (w() - 18) / 2 + 1, y() + 3, open_);
    // separator po lewej
    fl_color(SHADOW); fl_yxline(x(), y() + 3, y() + h() - 5);
    fl_color(LIGHT);  fl_yxline(x() + 1, y() + 3, y() + h() - 5);
  }
private:
  bool open_ = false;
};

// ---------------------------------------------------------------------------
// Sieć na pasku menu: ikona stanu (Wi-Fi z siłą sygnału, przewodowe,
// brak połączenia), a po kliknięciu menu z sieciami.
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
    if (!s.vpn.empty()) {   // aktywny VPN: kłódka obok ikony
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

// Pozycja menu z siecią Wi-Fi: nazwa, kłódka i kreski sygnału po prawej.
// Tekst etykiety: cyfra 0-4 (sygnał), '0'/'1' (zabezpieczona), potem SSID.
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
// Dok jak Control Strip z Mac OS: pasek kafelków przy lewej i dolnej krawędzi ekranu,
// nazwy w podpowiedziach. Po lewej zakładka zwijania, po prawej zaokrąglony koniec.
static const int DOCK_TILE = 40;   // kafelek (ikona 32 + ramka)
static const int DOCK_TAB = 14;    // zakładka zwijania
static const int DOCK_END = 12;    // zaokrąglony koniec paska
static std::vector<net::Network> g_menu_nets;
static std::vector<net::Vpn> g_menu_vpns;
static std::vector<std::string> g_menu_ifaces;

// ---------------------------------------------------------------------------
// Wzór biurka: kafelek w dwóch odcieniach (wybierany w panelu Wygląd)
// ---------------------------------------------------------------------------
static Fl_Tiled_Image *make_pattern() {
  int w, h;
  unsigned char *tile = pattern_tile(current_appearance().pattern, w, h);
  // Powielamy mały kafelek do 64x64, żeby rysować mniej kawałków.
  unsigned char *big = new unsigned char[64 * 64 * 3];
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++) memcpy(big + (y * 64 + x) * 3, tile + ((y % h) * w + x % w) * 3, 3);
  delete[] tile;
  auto *img = new Fl_RGB_Image(big, 64, 64, 3);
  img->alloc_array = 1;   // obraz zwalnia kafelek
  return new Fl_Tiled_Image(img, 0, 0);
}

static Fl_RGB_Image *make_logo_image();
// Pierwsze menu: logo LisaOS i napis "Lisa" (po zmianie schematu logo dostaje nowe tło).
void Desktop::set_logo() {
  if (!logo_item_) return;
  Fl_RGB_Image *prev = logo_img_;
  logo_img_ = make_logo_image();
  logo_item_->image(logo_img_);       // rejestruje typ etykiety z obrazkiem w FLTK
  static Fl_Multi_Label *ml = new Fl_Multi_Label;   // żyje tyle co pasek menu
  ml->labela = (const char *)logo_img_;
  ml->typea = _FL_IMAGE_LABEL;
  ml->labelb = " Lisa";
  ml->typeb = FL_NORMAL_LABEL;
  ml->label(logo_item_);
  delete prev;
}

static Fl_RGB_Image *make_logo_image() {
  Fl_Image_Surface surf(18, 18);
  Fl_Surface_Device::push_current(&surf);
  fl_color(GRAY);
  fl_rectf(0, 0, 18, 18);
  draw_logo(1, 1, 16);
  Fl_Surface_Device::pop_current();
  return surf.image();
}

// ---------------------------------------------------------------------------
Desktop::Desktop(int W, int H, const std::string &home, bool root)
    : Fl_Double_Window(0, 0, W, H, "LisaOS"),
      home_(home), root_path_(root ? "/" : home), root_(root), spatial_(home) {
  fl_open_display();
  // Okna innych programów potrafią zniknąć, zanim odczytamy ich właściwości
  // (lista aplikacji gary) - BadWindow to wtedy nie błąd, tylko wyścig.
  static XErrorHandler fltk_handler = XSetErrorHandler([](Display *d, XErrorEvent *e) -> int {
    if (e->error_code == BadWindow || e->error_code == BadDrawable) return 0;
    return fltk_handler ? fltk_handler(d, e) : 0;
  });
  box(FL_NO_BOX);
  warm_small_icons();   // poza draw() - patrz appearance_changed()
  pattern_.reset(make_pattern());
  if (!current_appearance().wallpaper.empty() || current_appearance().background != "wzor") {
    bool ok = false;
    wallpaper_.reset(render_wallpaper(W, H, &ok));
    if (!ok && current_appearance().background == "wzor") wallpaper_.reset();
  }
  detect_wm();

  menu_ = new LisaMenuBar(0, 0, W, MENUBAR_H);
  ((LisaMenuBar *)menu_)->before_open = [this] { update_menu(); };
  menu_->box(MENUBAR_BOX);
  menu_->color(GRAY);
  menu_->textfont(FONT_SYSTEM);
  menu_->textsize(SIZE_SYSTEM);
  menu_->down_box(FL_FLAT_BOX);
  menu_->selection_color(SEL);

  // Fl_Callback nie przyjmie lambdy z przechwyceniem, więc wskaźniki do
  // metod trzymamy w statycznej tablicy, a w user_data podajemy indeks.
  struct Item { const char *path; int shortcut; void (Desktop::*m)(); int flags; };
  std::vector<Item> items = {
    // --- Lisa: cały system ---
    {P_("logo/O systemie…"), 0, &Desktop::about, FL_MENU_DIVIDER},
    {P_("logo/Preferencje…"), 0, &Desktop::open_preferences, 0},
    {P_("logo/Wykonaj polecenie…"), FL_COMMAND + 'e', &Desktop::execute_command, 0},
    {P_("logo/Lisa Shell"), FL_COMMAND + 't', &Desktop::open_terminal, 0},
    {P_("logo/Otwórz dziennik"), 0, &Desktop::open_log, FL_MENU_DIVIDER},
    {P_("logo/Przerysuj wszystko"), 0, &Desktop::redraw_all, 0},
    {P_("logo/Uaktualnij wszystko"), FL_COMMAND + FL_SHIFT + 'r', &Desktop::refresh_all, FL_MENU_DIVIDER},
    {P_("logo/Uśpij"), 0, &Desktop::sleep_now, 0},
    {P_("logo/Uruchom ponownie…"), 0, &Desktop::reboot_ask, 0},
    {P_("logo/Zakończ…"), FL_COMMAND + 'q', &Desktop::quit_ask, 0},
    // --- Okno: aktywne okno folderu ---
    {P_("Okno/Nowy folder"), FL_COMMAND + 'n', &Desktop::new_folder, 0},
    {P_("Okno/Otwórz katalog nadrzędny"), FL_COMMAND + FL_Up, &Desktop::open_parent, 0},
    {P_("Okno/Zamknij"), FL_COMMAND + 'w', &Desktop::close_active, 0},
    {P_("Okno/Uaktualnij"), FL_COMMAND + 'r', &Desktop::update_window, FL_MENU_DIVIDER},
    {P_("Okno/Zaznacz zawartość"), FL_COMMAND + 'a', &Desktop::select_all, 0},
    {P_("Okno/Uporządkuj"), FL_COMMAND + '.', &Desktop::clean_up, FL_MENU_DIVIDER},
    {P_("Okno/Pokaż ukryte pliki"), FL_COMMAND + 'h', &Desktop::toggle_hidden, FL_MENU_TOGGLE},
    {P_("Okno/Pokaż jako/Ikony"), FL_COMMAND + '1', &Desktop::view_icons, FL_MENU_RADIO},
    {P_("Okno/Pokaż jako/Listę"), FL_COMMAND + '2', &Desktop::view_list, FL_MENU_RADIO},
    {P_("Okno/Sortuj/według nazwy"), 0, &Desktop::sort_name, FL_MENU_RADIO},
    {P_("Okno/Sortuj/według daty"), 0, &Desktop::sort_date, FL_MENU_RADIO},
    {P_("Okno/Sortuj/według rozmiaru"), 0, &Desktop::sort_size, FL_MENU_RADIO},
    {P_("Okno/Sortuj/według rodzaju"), 0, &Desktop::sort_kind, FL_MENU_RADIO},
    {P_("Okno/Wielkość ikon/Małe"), 0, &Desktop::icons_small, FL_MENU_RADIO},
    {P_("Okno/Wielkość ikon/Normalne"), 0, &Desktop::icons_normal, FL_MENU_RADIO},
    {P_("Okno/Wielkość ikon/Duże"), 0, &Desktop::icons_large, FL_MENU_RADIO},
    // --- Ikony: zaznaczone pliki, foldery i dyski ---
    {P_("Ikony/Otwórz"), FL_COMMAND + 'o', &Desktop::open_selected, 0},
    {P_("Ikony/Kopiuj"), FL_COMMAND + 'c', &Desktop::copy_selected, 0},
    {P_("Ikony/Wklej"), FL_COMMAND + 'v', &Desktop::paste, 0},
    {P_("Ikony/Duplikuj"), FL_COMMAND + 'd', &Desktop::duplicate, 0},
    {P_("Ikony/Zmień nazwę…"), FL_F + 2, &Desktop::rename_selected, 0},
    {P_("Ikony/Usuń"), FL_COMMAND + FL_BackSpace, &Desktop::move_to_trash, FL_MENU_DIVIDER},
    {P_("Ikony/Informacje…"), FL_COMMAND + 'i', &Desktop::get_info, 0},
    {P_("Ikony/Ustaw jako tapetę"), 0, &Desktop::set_as_wallpaper, FL_MENU_DIVIDER},
    {P_("Ikony/Zamroź"), FL_COMMAND + 's', &Desktop::snapshot, 0},
    {P_("Ikony/Odmroź"), 0, &Desktop::unsnapshot, FL_MENU_DIVIDER},
    {P_("Ikony/Połóż na doku"), FL_COMMAND + 'l', &Desktop::dock_add, 0},
    {P_("Ikony/Zdejmij z doku"), FL_COMMAND + 'k', &Desktop::dock_remove, FL_MENU_DIVIDER},
    {P_("Ikony/Wysuń"), FL_COMMAND + 'y', &Desktop::eject_selected, 0},
    // --- Narzędzia ---
    {P_("Narzędzia/Formatuj dysk…"), 0, &Desktop::format_disk, 0},
    {P_("Narzędzia/Opróżnij kosz…"), 0, &Desktop::empty_trash, FL_MENU_DIVIDER},
    {root ? P_("Narzędzia/Zakończ tryb root") : P_("Narzędzia/Tryb root…"), 0,
     root ? &Desktop::quit : &Desktop::ask_root, FL_MENU_DIVIDER},
    {P_("Narzędzia/Resetuj LISA"), 0, &Desktop::reset_lisa, 0},
  };
  static std::vector<void (Desktop::*)()> methods;
  for (auto &it : items) {
    methods.push_back(it.m);
    long idx = (long)methods.size() - 1;
    menu_->add(plat::tr_path(it.path).c_str(), it.shortcut,
               [](Fl_Widget *w, void *d) {
                 auto *self = (Desktop *)w->window();
                 auto m = methods[(long)d];
                 if (m) (self->*m)();
               },
               (void *)idx, it.flags);
  }
  logo_item_ = (Fl_Menu_Item *)menu_->find_item("logo");
  set_logo();

  // Prawa strona, od krawędzi: gary, zegar, sieć, głośność, zasilanie, zasobnik -
  // ciasno, bez dziur. Pasek menu kończy się za ostatnim tytułem; między nim a
  // apletami jest informacja o wolnej pamięci i dysku. Pozycje liczy layout_bar().
  extras_ = new MenuExtras(0, 0, MenuExtras::needed_width(), MENUBAR_H);
  if (net::installed()) {
    auto *nm = new NetMenu(0, 0, 26, MENUBAR_H);
    net_menu_ = nm;
    nm->selection_color(SEL);
    nm->on_open = [this](NetMenu *m) { build_net_menu(m); };
    nm->summary = [this] { return net::summary(net_); };
  }
  vol_w_ = applets::make_volume(0, 0, 26, MENUBAR_H);
  pow_w_ = applets::make_power(0, 0, 42, MENUBAR_H);
  tray_w_ = tray::make(0, 0, MENUBAR_H);
  {
    // szerokość tytułów menu - tak samo liczy ją Fl_Menu_Bar::draw()
    int mx = 6;
    for (const Fl_Menu_Item *m = menu_->menu(); m && m->label(); m = m->next())
      if (m->visible()) mx += m->measure(nullptr, menu_) + 16;
    mx += 8;
    menu_->size(mx, MENUBAR_H);
    mem_w_ = new MemInfo(mx, 0, 1, MENUBAR_H, W, home_);
  }
  layout_bar();
  auto *wm = new WindowsMenu(W - APP_W, 0, APP_W, MENUBAR_H);
  app_menu_ = wm;
  wm->selection_color(menu_->selection_color());
  wm->on_open = [this](WindowsMenu *m) { build_windows_menu(m); };
  wm->current = [this](std::string &name, Fl_Image *&icon) { current_app(name, icon); };
  end();

  // Escape nie może zamknąć biurka
  callback([](Fl_Widget *w, void *) {
    if (Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape) return;
    ((Desktop *)w)->quit();
  });

  // ukryte pliki: zapamiętany wybór (Okno > Pokaż ukryte pliki), w trybie root zawsze
  show_hidden_ = root_;
  {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::ifstream in((x && *x ? std::string(x) : home_ + "/.config") + "/lisaos/rambler.conf");
    std::string line;
    while (std::getline(in, line)) {
      if (line == "ukryte=1") show_hidden_ = true;
      if (line == "dok_zwiniety=1") dock_collapsed_ = true;
    }
  }
  load_dock();
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
      if (!csd_mode) { quit(); return; }   // sesja LisaOS: koniec Ramblera kończy sesję
      // obca sesja (GNOME itd.): poprośmy ją o wylogowanie
      spawn({"sh", "-c", "gnome-session-quit --logout --no-prompt 2>/dev/null || "
                         "loginctl terminate-session \"$XDG_SESSION_ID\""});
    };
    h.before_shutdown = [this] { for (auto *w : wins_) if (w->pane) remember(w, true); };
    h.open_power_panel = [this] { spawn({alice_exe(), "zasilanie"}); };
    h.lisaos_session = !csd_mode;
    applets::init(h);
  }
  tray_pending_ = true;   // zasobnik startuje po pokazaniu okna (potrzebne okno X do osadzania)
  if (net_menu_) {
    Fl::set_labeltype(NET_LABEL, net_label_draw, net_label_measure);
    net_ = net::query();
    net::watch([this] { net_refresh(); });
  }
}

// Prawa strona paska menu, od krawędzi ekranu do środka.
void Desktop::layout_bar() {
  int rx = w() - APP_W;
  auto put = [&](Fl_Widget *wd, int ww) {
    if (!wd) return;
    rx -= ww;
    wd->resize(rx, 0, ww, MENUBAR_H);
  };
  put(extras_, extras_->w());
  put(net_menu_, 26);
  put(vol_w_, 26);
  put(pow_w_, 42);
  int tw = tray::width();
  if (tray_w_) { put(tray_w_, tw); if (tw) tray_w_->show(); else tray_w_->hide(); }
  int mx = menu_->w();
  if (mem_w_) {
    if (rx > mx) { mem_w_->resize(mx, 0, rx - mx, MENUBAR_H); mem_w_->show(); }
    else mem_w_->hide();
  }
  damage(FL_DAMAGE_ALL, 0, 0, w(), MENUBAR_H);
}

// Kosz może się zmienić z zewnątrz (inny program, inne okno) - ikona pełny/pusty.
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
  }, this);
}

std::string Desktop::trash_dir() const { return home_ + "/.local/share/Trash/files"; }

bool Desktop::trash_full() const {
  std::error_code ec;
  return fs::exists(trash_dir(), ec) && !fs::is_empty(trash_dir(), ec);
}

void Desktop::layout_icons() {
  icons_.clear();
  int ix = w() - 90, iy = MENUBAR_H + 14;
  icons_.push_back({"LisaOS HD", home_, Icon::Home, ix, iy});
  iy += 70;
  if (root_) { icons_.push_back({"System", "/", Icon::Home, ix, iy}); iy += 70; }
  // dyski i płyty pod dyskiem LisaOS HD, jak w Mac OS
  for (int i = 0; i < (int)vols_.size(); i++) {
    if (iy > h() - 150) break;
    DeskIcon d{vols_[i].name, vols_[i].mountpoint, vols_[i].icon, ix, iy};
    d.vol = i;
    icons_.push_back(d);
    iy += 70;
  }
  icons_.push_back({_("Kosz"), trash_dir(), trash_full() ? Icon::TrashFull : Icon::Trash, ix, h() - 74});
  // dok: ikony wyciągnięte na biurko (Leave Out z Workbencha), jako pasek w lewym
  // dolnym rogu. Pozycja ikony jak u innych: rysunek w (x + 24, y).
  dock_shown_ = 0;
  if (!dock_collapsed_) {
    int tx = DOCK_TAB;
    for (auto &p : dock_) {
      std::error_code ec;
      if (!fs::exists(p, ec)) continue;
      if (tx + DOCK_TILE + DOCK_END > ix - 10) break;   // nie wchodzimy pod kosz i dyski
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

static constexpr int MIN_W = 200, MIN_H = 120;   // najmniejsze okno folderu
static Atom atom(const char *name) { return XInternAtom(fl_display, name, False); }

plat::Rect Desktop::folder_area() const {
  const int GAP = 6;
  int L = FRAME + GAP, T = MENUBAR_H + TITLE_H + GAP;
  int R = w() - 96 - FRAME;                                   // kolumna LisaOS HD / dysków / kosza
  int B = h() - FRAME - GAP - (dock_.empty() ? 0 : DOCK_TILE);   // dok (także zwinięty)
  return {L, T, std::max(MIN_W, R - L), std::max(MIN_H, B - T)};
}

// Insight (menedżer okien) bierze stąd prostokąt zoomu dla okien Ramblera:
// _LISAOS_OBSZAR_OKIEN na oknie głównym = x, y, szer., wys. ramki na ekranie.
void Desktop::publish_folder_area() {
  if (!shown() || !fl_display) return;
  // _LISAOS_DOK: wysokość doku - Insight trzyma nad nim okna wszystkich programów
  static long last_dock = -1;
  long dock = dock_.empty() ? 0 : DOCK_TILE;
  if (dock != last_dock) {
    last_dock = dock;
    XChangeProperty(fl_display, DefaultRootWindow(fl_display), atom("_LISAOS_DOK"), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&dock, 1);
  }
  Rect a = folder_area();
  long v[4] = {x_root() + a.x - FRAME, y_root() + a.y - TITLE_H, a.w + 2 * FRAME, a.h + TITLE_H + FRAME};
  static long last[4] = {-1, -1, -1, -1};
  if (!memcmp(v, last, sizeof v)) return;
  memcpy(last, v, sizeof v);
  XChangeProperty(fl_display, DefaultRootWindow(fl_display), atom("_LISAOS_OBSZAR_OKIEN"), XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)v, 4);
}

// Prostokąt całego paska doku (z zakładką i końcem); pusty, gdy dok jest pusty.
void Desktop::dock_rect(int &X, int &Y, int &W, int &H) const {
  X = 0; H = DOCK_TILE; Y = h() - H;
  W = dock_.empty() ? 0 : DOCK_TAB + dock_shown_ * DOCK_TILE + DOCK_END;
}

void Desktop::draw_dock() {
  int X, Y, W, H;
  dock_rect(X, Y, W, H);
  if (!W) return;
  // zaokrąglony koniec: tło z obrysem, z prawej strony łuk
  int ex = X + W - DOCK_END - 6;
  fl_color(DARK);
  fl_rounded_rectf(ex, Y, DOCK_END + 6, H, 7);
  fl_color(GRAY);
  fl_rounded_rectf(ex + 1, Y + 1, DOCK_END + 4, H - 1, 6);
  fl_color(LIGHT);
  fl_xyline(ex + 1, Y + 1, X + W - 6);
  fl_color(SHADOW);
  for (int i = 0; i < 3; i++) fl_yxline(X + W - 9 + i * 2 - 2, Y + 12, Y + H - 12);   // uchwyt
  // zakładka zwijania
  fl_draw_box(UP_BOX, X, Y, DOCK_TAB, H, GRAY);
  fl_color(DARK);
  fl_rect(X, Y, DOCK_TAB + 1, H + 1);
  int cx = X + DOCK_TAB / 2, cy = Y + H / 2;
  fl_color(TEXT);
  if (dock_collapsed_) fl_polygon(cx - 2, cy - 4, cx + 2, cy, cx - 2, cy + 4);
  else fl_polygon(cx + 2, cy - 4, cx - 2, cy, cx + 2, cy + 4);
  // kafelki
  for (int i = 0; i < (int)icons_.size(); i++) {
    auto &ic = icons_[i];
    if (!ic.dock) continue;
    bool sel = i == icon_sel_ || i == icon_drop_;
    int bx = ic.x + 20, by = ic.y - 4;
    fl_draw_box(sel ? DOWN_BOX : UP_BOX, bx, by, DOCK_TILE, DOCK_TILE,
                sel ? fl_color_average(GRAY, SHADOW, 0.6f) : GRAY);
    fl_color(DARK);
    fl_rect(bx, by, DOCK_TILE + 1, DOCK_TILE + 1);
    draw_entry_icon(ic.entry, ic.x + 24, ic.y, false, 32);
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
    if (ic.dock) {   // cały kafelek
      int bx = ic.x + 20, by = ic.y - 4;
      if (ex >= bx && ex < bx + DOCK_TILE && ey >= by && ey < by + DOCK_TILE) return i;
      continue;
    }
    if (ex >= ic.x + 24 && ex < ic.x + 56 && ey >= ic.y && ey < ic.y + 52) return i;
  }
  return -1;
}

bool Desktop::allowed(const std::string &path) const {
  if (root_) return true;
  std::error_code ec;
  std::string c = fs::weakly_canonical(path, ec).string();
  std::string h = fs::weakly_canonical(home_, ec).string();
  if (c == h || c.rfind(h + "/", 0) == 0) return true;
  return volume_at_path(c) >= 0;   // zamontowane dyski i płyty też są "nasze"
}

int Desktop::volume_at_path(const std::string &path) const {
  for (int i = 0; i < (int)vols_.size(); i++) {
    const std::string &mp = vols_[i].mountpoint;
    if (!mp.empty() && (path == mp || path.rfind(mp + "/", 0) == 0)) return i;
  }
  return -1;
}

std::string Desktop::title_for(const std::string &path) const {
  if (path == home_) return "LisaOS HD";
  if (path == "/") return "System";
  if (path == trash_dir()) return _("Kosz");
  for (auto &v : vols_) if (!v.mountpoint.empty() && v.mountpoint == path) return v.name;
  return fs::path(path).filename().string();
}

Fl_Window *make_drag_ghost(const Entry &e, int n);   // niżej, przy przeciąganiu

// ---------------------------------------------------------------------------
// EWMH: rozmowa z menedżerem okien (Insight lub dowolny zgodny)
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
  ev.xclient.data.l[3] = 1;   // źródło: aplikacja
  XSendEvent(fl_display, DefaultRootWindow(fl_display), False,
             SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(fl_display);
}

// Zmiana aktywnego okna (właściwość na oknie głównym X).
static int x_watch(void *event, void *) {
  XEvent *e = (XEvent *)event;
  if (e->type == PropertyNotify && e->xproperty.window == DefaultRootWindow(fl_display) && g_desk) {
    if (e->xproperty.atom == atom("_NET_ACTIVE_WINDOW")) g_desk->active_changed();
    else if (e->xproperty.atom == atom("_NET_CLIENT_LIST")) g_desk->clients_changed();
    else if (e->xproperty.atom == atom(APPEARANCE_ATOM)) g_desk->appearance_changed();
  }
  return 0;
}

// Kto zarządza oknami? Insight rysuje ramki Platinum sam; pod każdym innym
// menedżerem (GNOME, KDE, Openbox...) albo bez menedżera rysujemy je w Ramblerze.
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
    // czy WM umie przesuwać okno za nas (_NET_WM_MOVERESIZE)
    data = nullptr;
    if (XGetWindowProperty(fl_display, DefaultRootWindow(fl_display), atom("_NET_SUPPORTED"), 0, 1024,
                           False, XA_ATOM, &type, &fmt, &n, &after, &data) == Success && data) {
      Atom mr = atom("_NET_WM_MOVERESIZE");
      for (unsigned long i = 0; i < n; i++) if (((Atom *)data)[i] == mr) wm_moveresize = true;
      XFree(data);
    }
  }
  csd_mode = name != "insight";
  if (const char *e = getenv("LISAOS_CSD")) csd_mode = !strcmp(e, "1");   // do testów
}

void Desktop::redraw_frames() {
  for (auto *w : wins_) if (w->csd) w->redraw();
}

// ---------------------------------------------------------------------------
// NWin - okno folderu lub dialogu
// ---------------------------------------------------------------------------
NWin::NWin(Desktop *d, int X, int Y, int W, int H, const char *title, bool dlg)
    : Fl_Double_Window(X, Y, W, H), desk(d), dialog(dlg), csd(d->csd_mode) {
  copy_label(title);
  if (csd) border(0);   // menedżer okien bez dekoracji - ramkę rysujemy sami
  // Klasa "LisaOS-*" mówi Insightowi, że animacje otwierania robimy sami.
  xclass(dlg ? "LisaOS-Dialog" : "LisaOS-Rambler");
  callback([](Fl_Widget *w, void *) {
    auto *n = (NWin *)w;
    // Escape zamyka dialog, ale nie okno folderu (jak w Mac OS)
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
  show();   // FLTK: okno już widoczne zostaje wyciągnięte na wierzch
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

// --- własna ramka (CSD) -------------------------------------------------------
void NWin::draw() {
  if (!csd) { Fl_Double_Window::draw(); return; }
  // Zmiana tylko w środku okna (np. przewijanie, przełączenie widoku): odświeżamy
  // wyłącznie zmienione elementy. Ramka zostaje w buforze okna - gdybyśmy ją
  // malowali za każdym razem, jej szare tło zamazałoby treść, a przewijany obszar
  // dorysowuje tylko pasek, który wjechał na ekran.
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
  for (int i = 0; i < children(); i++) draw_child(*child(i));   // pełne odrysowanie treści
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

// Przesunięcie/zmiana rozmiaru przez menedżer okien (EWMH) - płynne i zgodne
// z zachowaniem pulpitu (przyciąganie do krawędzi itd.).
void NWin::start_moveresize(int direction) {
  XUngrabPointer(fl_display, CurrentTime);   // oddaj wskaźnik menedżerowi okien
  XEvent ev{};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = fl_xid(this);
  ev.xclient.message_type = atom("_NET_WM_MOVERESIZE");
  ev.xclient.format = 32;
  // punkt odniesienia: miejsce wciśnięcia przycisku (okno "dogoni" wskaźnik)
  ev.xclient.data.l[0] = px(direction == 8 ? press_x_ : Fl::event_x_root());   // EWMH: piksele
  ev.xclient.data.l[1] = px(direction == 8 ? press_y_ : Fl::event_y_root());
  ev.xclient.data.l[2] = direction;           // 8 = przesuń, 4 = prawy dolny róg
  ev.xclient.data.l[3] = Button1;
  ev.xclient.data.l[4] = 1;
  XSendEvent(fl_display, DefaultRootWindow(fl_display), False,
             SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(fl_display);
}

void NWin::csd_toggle_shade() {
  Rect full = {x(), y(), w(), csd_shaded_ ? full_h_ : h()}, bar = {x(), y(), w(), TITLE_H};
  if (csd_shaded_) zoom_rects(bar, full); else zoom_rects(full, bar);
  if (!csd_shaded_) {
    full_h_ = h();
    csd_shaded_ = true;
    resizable(nullptr);          // treść nie może się zgnieść do zera
    init_sizes();
    size(w(), TITLE_H);
  } else {
    csd_shaded_ = false;
    size(w(), full_h_);
    resizable(pane ? (Fl_Widget *)pane : nullptr);
    init_sizes();
  }
  redraw();
  if (pane) Fl::add_timeout(0.4, [](void *d) { auto *w = (NWin *)d; w->desk->remember(w, true); }, this);
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
    bool grow = on_edge && ex >= FRAME;      // prawa/dolna krawędź; lewa przesuwa
    if (desk->wm_moveresize && grow) { start_moveresize(4); return 1; }
    // Przesuwanie zaczynamy dopiero po ruchu myszy - inaczej podwójne kliknięcie
    // (zwijanie) nigdy by do nas nie dotarło, bo wskaźnik przejąłby menedżer okien.
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
      if (b == 1) { desk->close(this); return 1; }   // okno usuwane z opóźnieniem
      if (b == 2) csd_toggle_zoom();
      if (b == 3) csd_toggle_shade();
      return 1;
    }
    if (drag_ != NONE) { drag_ = NONE; return 1; }
    return 0;
  }
  return 0;
}

static void remember_later(void *d) {
  auto *w = (NWin *)d;
  w->desk->remember(w, true);
}

NWin::~NWin() { Fl::remove_timeout(remember_later, this); }

int NWin::handle(int e) {
  if (csd && csd_handle(e)) return 1;
  int r = Fl_Double_Window::handle(e);
  // Pasek menu żyje w oknie biurka, a fokus ma okno folderu - przekaż skrót.
  if (!r && e == FL_SHORTCUT && !dialog) r = desk->shortcut();
  return r;
}

// Zapamiętaj położenie po przesunięciu lub zmianie rozmiaru - z opóźnieniem,
// bo podczas przeciągania menedżer okien przysyła wiele zmian.
void NWin::resize(int X, int Y, int W, int H) {
  Fl_Double_Window::resize(X, Y, W, H);
  if (pane && shown()) {
    Fl::remove_timeout(remember_later, this);
    Fl::add_timeout(0.4, remember_later, this);
  }
}

// ---------------------------------------------------------------------------
// Okna
// ---------------------------------------------------------------------------
NWin *Desktop::find(const std::string &path) {
  for (auto *w : wins_) if (w->pane && w->path == path) return w;
  return nullptr;
}

bool Desktop::known(NWin *w) const {
  return std::find(wins_.begin(), wins_.end(), w) != wins_.end();
}

NWin *Desktop::active_folder() {
  Window a = net_active();
  for (auto *w : wins_) if (w->pane && w->shown() && fl_xid(w) == a) return w;
  return nullptr;
}

int Desktop::shortcut() {
  // Uwaga: Fl_Menu_::test_shortcut() sam wywołuje akcję znalezionej pozycji.
  update_menu();
  return menu_->test_shortcut() ? 1 : 0;
}

void Desktop::active_changed() {
  if (app_menu_) app_menu_->redraw();   // nazwa i ikona aktywnej aplikacji na pasku
  Window a = net_active();
  for (auto it = wins_.begin(); it != wins_.end(); ++it) {
    if ((*it)->shown() && fl_xid(*it) == a) {
      NWin *w = *it;
      wins_.erase(it);
      wins_.push_back(w);           // ostatnio aktywne na końcu listy
      if (icon_sel_ >= 0) { icon_sel_ = -1; redraw(); }
      redraw_frames();
      sync_view_menu();
      return;
    }
  }
  redraw_frames();
  sync_view_menu();
}

void Desktop::remember(NWin *w, bool open) {
  if (!w->pane || !known(w)) return;
  WinState s;
  Rect cr = w->content_rect();   // zapisujemy położenie treści - bez ramki
  // pozycja względem biurka - biurko może stać w różnych miejscach (np. --center)
  s.x = cr.x - x_root(); s.y = cr.y - y_root(); s.w = cr.w; s.h = cr.h;
  s.shaded = w->shaded();
  s.open = open;
  s.list = w->pane->view()->mode() == FolderView::List;
  s.icons = w->pane->view()->icon_size();
  spatial_.set(w->path, s);
}

bool Desktop::source_rect(const std::string &path, Rect &out) {
  // Dysk i kosz mają ikony na biurku
  for (auto &ic : icons_)
    if (ic.path == path) { out = to_screen({ic.x + 24, ic.y, 32, 32}); return true; }
  // Pozostałe foldery - ikona w oknie folderu nadrzędnego, jeśli jest otwarte
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
  // ikony folderów w pozostałych oknach tracą kreskowanie "otwarty"
  for (auto *o : wins_) if (o->pane) o->pane->view()->redraw();
  if (folder) {
    Fl::flush();     // najpierw zniknięcie okna, potem kontury
    XSync(fl_display, False);
    zoom_rects(from, to);
  }
}

void Desktop::open_folder(const std::string &path, bool animate) {
  if (!allowed(path)) {
    dialog(_("Brak dostępu"),
           _("Ten folder znajduje się poza LisaOS HD.\nAby go otworzyć, przełącz się w tryb root."),
           {{"OK", nullptr}});
    return;
  }
  std::error_code ec;
  if (path == trash_dir()) fs::create_directories(path, ec);
  if (NWin *w = find(path)) {
    // już otwarty: jak w Mac OS - kontury pokazują, gdzie jest jego okno
    Rect src;
    if (animate && source_rect(path, src)) zoom_rects(src, w->frame_rect());
    if (w->shaded()) w->set_shaded(false);
    w->activate_window();
    return;
  }
  // Pozycja dotyczy treści okna (bez ramki) - tak zapisuje ją FLTK.
  WinState s;
  bool known_pos = spatial_.get(path, s);
  if (!known_pos) {
    int n = 0;
    for (auto *w : wins_) if (w->pane) n++;
    s.x = 40 + 26 * (n % 8);
    s.y = MENUBAR_H + TITLE_H + 24 + 26 * (n % 8);
    s.w = 440; s.h = 270;
  }
  // okno mieści się w obszarze folderów: widać LisaOS HD, dyski, kosz i dok
  Rect area = folder_area();
  s.w = std::clamp(s.w, MIN_W, area.w);
  s.h = std::clamp(s.h, MIN_H, area.h);
  s.x = std::clamp(s.x, area.x, area.x + area.w - s.w);
  s.y = std::clamp(s.y, area.y, area.y + area.h - s.h);

  // zapisane pozycje są względem biurka - zamiana na współrzędne ekranu
  int ax = s.x + x_root(), ay = s.y + y_root();

  if (animate) {
    Rect to = {ax - FRAME, ay - TITLE_H, s.w + 2 * FRAME, s.h + TITLE_H + FRAME};
    if (known_pos && s.shaded) to.h = TITLE_H;
    Rect from;
    if (!source_rect(path, from)) from = center_of(to);
    zoom_rects(from, to);
  }

  // przy własnej ramce okno jest większe o ramkę, a treść leży w środku
  int ox = csd_mode ? FRAME : 0, oy = csd_mode ? TITLE_H : 0;
  int ew = csd_mode ? 2 * FRAME : 0, eh = csd_mode ? TITLE_H + FRAME : 0;
  auto *win = new NWin(this, ax - ox, ay - oy, s.w + ew, s.h + eh, title_for(path).c_str(), false);
  win->path = path;
  win->pane = new FolderPane(ox, oy, s.w, s.h, path, show_hidden_);
  win->pane->view()->set_icon_size(s.icons);
  win->resizable(win->pane);
  // przy własnej ramce zwinięte okno ma wysokość samego paska tytułu
  win->size_range(MIN_W + ew, csd_mode ? TITLE_H : MIN_H);
  win->end();
  win->pane->view()->on_open = [this](const Entry &e) { open_entry(e); };
  win->pane->view()->is_open = [this](const std::string &p) { return find(p) != nullptr; };
  win->pane->view()->on_drag_start = [this, win](const std::vector<Entry> &v) { drag_start(win, v); };
  win->pane->view()->on_drag_move = [this](int xr, int yr) { drag_move(xr, yr); };
  win->pane->view()->on_drag_end = [this](int xr, int yr, bool copy) { drag_end(xr, yr, copy); };
  win->pane->view()->on_drop_uris = [this, win](const std::string &uris, const std::string &dest) {
    drop_uris(uris, dest.empty() ? win->path : dest);
  };
  win->pane->on_rename_commit = [this](const std::string &o, const std::string &n) { return rename_path(o, n); };
  win->pane->on_error = [this](const std::string &m) { dialog(_("Zmiana nazwy"), m, {{"OK", nullptr}}); };
  if (known_pos && s.list) win->pane->set_mode(FolderView::List);
  wins_.push_back(win);
  win->show();
  if (known_pos && s.shaded) win->set_shaded(true);
  // ikona folderu w oknie nadrzędnym dostaje kreskowanie "otwarty"
  for (auto *o : wins_) if (o->pane && o != win) o->pane->view()->redraw();
  remember(win, true);
}

void Desktop::open_entry(const Entry &e) {
  if (e.dir) { open_folder(e.path); return; }
  if (is_appimage(e.name)) { open_appimage(e.path); return; }
  // plik z flagą "Program" (Informacje): uruchom w Lisa Shell z parametrami
  struct stat st;
  if (stat(e.path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR) && allowed(e.path)) {
    std::vector<std::string> a = {rambler_exe("lisaos-shell"), "--katalog", fs::path(e.path).parent_path().string(),
                                  "-e", "sh", "-c", "m=$1; shift; \"$@\"; s=$?; echo; printf \"$m\" \"$s\"; read x",
                                  // TRANSLATORS: %s = kod wyjścia programu (zostaw %s)
                                  "sh", _("[kod wyjścia %s] Enter zamyka okno…"), e.path};
    for (auto &x : split_args(tooltypes(e.path))) a.push_back(x);
    spawn(a);
    return;
  }
  spawn({"xdg-open", e.path});
}

std::string Desktop::apps_dir() const { return appimage::apps_dir(home_); }

// Czy plik leży w ~/Aplikacje (także w podfolderze kategorii).
bool Desktop::in_apps(const std::string &path) const {
  std::string a = apps_dir() + "/";
  return path.compare(0, a.size(), a) == 0;
}

// Docelowe miejsce AppImage w ~/Aplikacje: folder kategorii (Gry, Grafika...).
std::string Desktop::app_destination(const std::string &path) const {
  return (fs::path(apps_dir()) / appimage::category_folder(path, home_) / fs::path(path).filename()).string();
}

// AppImage spoza folderu Aplikacje: zaproponuj instalację (jak sklep w telefonie,
// tylko bez sklepu). Z folderu Aplikacje: uruchom, w razie potrzeby rozpakowując.
void Desktop::open_appimage(const std::string &path) {
  if (in_apps(path)) { run_app(path); return; }
  std::string name = appimage::label(path, home_);
  fs::path dest = app_destination(path);
  std::string folder = dest.parent_path().parent_path().filename().string() + "/" + dest.parent_path().filename().string();
  std::error_code ec;
  auto install = [this, path, dest] {
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (fs::exists(dest, ec)) fs::remove(dest, ec);   // "Zastąp": ta sama wersja
    fs::rename(path, dest, ec);
    if (ec) {   // inny system plików (pendrive): kopia i usunięcie
      ec.clear();
      fs::copy_file(path, dest, ec);
      if (!ec) fs::remove(path, ec);
    }
    if (ec) {
      dialog(_("Instalacja"), _("Nie udało się przenieść pliku:") + std::string("\n") + ec.message(), {{_("OK"), nullptr}});
      return;
    }
    chmod(dest.c_str(), 0755);
    path_moved(path, dest.string());
    refresh_all();
    run_app(dest.string());
  };
  if (fs::exists(dest, ec)) {   // ta sama wersja już jest - inne wersje mogą stać obok
    dialog(_("Instalacja"),
           plat::fmt(_("„%1” jest już zainstalowany w folderze %2.\n"
                       "Inne wersje tego programu mogą być zainstalowane obok."), name, folder),
           {{_("Anuluj"), nullptr},
            {_("Zastąp"), install},
            {_("Uruchom"), [this, dest] { run_app(dest.string()); }}});
    return;
  }
  dialog(_("Instalacja"),
         plat::fmt(_("Zainstalować aplikację „%1”?\nPlik zostanie przeniesiony do folderu %2."), name, folder) +
         "\n" +
         (appimage::sandbox_available()
              ? _("Aplikacja będzie działać w piaskownicy: zobaczy tylko LisaOS HD.")
              : _("Uwaga: brak bubblewrap - aplikacja będzie działać bez piaskownicy.")),
         {{_("Anuluj"), nullptr}, {_("Zainstaluj"), install}});
}

void Desktop::run_app(const std::string &path) {
  if (!appimage::is_installed(path, home_)) {
    for (auto *w : wins_) w->cursor(FL_CURSOR_WAIT);
    cursor(FL_CURSOR_WAIT);
    Fl::flush();
    std::string err = appimage::install(path, home_);
    for (auto *w : wins_) w->cursor(FL_CURSOR_DEFAULT);
    cursor(FL_CURSOR_DEFAULT);
    if (!err.empty()) {
      dialog(_("Nie można uruchomić"), err, {{"OK", nullptr}});
      return;
    }
    refresh_all();   // ikona części AppImage (np. DwarFS) jest dostępna dopiero po rozpakowaniu
  }
  // gary: pierwsze nowe okno to ta aplikacja - dostanie nazwę i ikonę z AppImage
  gary::note_launch(appimage::label(path, home_),
                    appimage::icon(path, home_));
  appimage::launch(path, home_, split_args(tooltypes(path)));   // parametry z okna Informacje
}

void Desktop::start_tray() {
  if (!tray_pending_) return;
  tray_pending_ = false;
  tray::init(this, [this] { layout_bar(); });
}

void Desktop::restore() {
  start_tray();
  publish_folder_area();
  g_desk = this;
  // śledź zmiany aktywnego okna (bez nadpisywania masek, które ustawił FLTK)
  XWindowAttributes wa;
  Window rootw = DefaultRootWindow(fl_display);
  XGetWindowAttributes(fl_display, rootw, &wa);
  XSelectInput(fl_display, rootw, wa.your_event_mask | PropertyChangeMask);
  Fl::add_system_handler(x_watch, nullptr);

  int n = 0;
  for (auto &p : spatial_.open_paths()) {
    std::error_code ec;
    if (fs::is_directory(p, ec) && allowed(p)) { open_folder(p, false); n++; }
  }
  if (!n) open_folder(root_path_, false);
}

// ---------------------------------------------------------------------------
// Rysowanie i zdarzenia biurka
// ---------------------------------------------------------------------------
void Desktop::draw() {
  if (wallpaper_) wallpaper_->draw(0, 0);
  else pattern_->draw(0, 0, w(), h());
  fl_font(FONT_SMALL, SIZE_SMALL);
  for (int i = 0; i < (int)icons_.size(); i++) {
    auto &ic = icons_[i];
    bool sel = i == icon_sel_ || i == icon_drop_;
    Icon shown = ic.icon;
    if (vdragging_ && ic.path == trash_dir()) shown = Icon::Eject;   // jak w Mac OS
    if (ic.dock) continue;   // dok rysuje draw_dock(): kafelki bez podpisów
    draw_icon(shown, ic.x + 24, ic.y, sel);
    std::string nm = ellipsize(ic.name, 76);
    int tw = (int)fl_width(nm.c_str());
    int tx = ic.x + 40 - tw / 2, ty = ic.y + 36;
    fl_color(sel ? ICON_SEL : CONTENT);
    fl_rectf(tx - 2, ty, tw + 4, fl_height());
    fl_color(sel ? ICON_SEL_TEXT : CONTENT_TEXT);
    fl_draw(nm.c_str(), tx, ty + fl_height() - fl_descent());
  }
  draw_dock();
  for (int i = 0; i < children(); i++) draw_child(*child(i));
}

int Desktop::handle(int e) {
  // dok: nazwy w podpowiedziach, zakładka zwija i rozwija pasek
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
  switch (e) {   // pliki z innych programów upuszczane na ikony kosza i dysku
  case FL_DND_ENTER:
    return 1;
  case FL_DND_DRAG: {
    int i = icon_at(Fl::event_x(), Fl::event_y());
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
  if (e == FL_PUSH && Fl::event_y() >= MENUBAR_H) {
    int i = icon_at(Fl::event_x(), Fl::event_y());
    icon_sel_ = i;
    vdrag_icon_ = (i >= 0 && icons_[i].vol >= 0) ? i : -1;   // dysk można przeciągnąć do kosza
    vpress_x_ = Fl::event_x_root(); vpress_y_ = Fl::event_y_root();
    dock_press_ = (i >= 0 && icons_[i].dock) ? i : -1;   // dok: jedno kliknięcie, jak Control Strip
    if (dock_press_ < 0 && i >= 0 && Fl::event_clicks()) {
      Fl::event_clicks(0);
      vdrag_icon_ = -1;
      if (icons_[i].vol >= 0) open_volume(icons_[i].vol);
      else open_folder(icons_[i].path);
    } else if (i < 0 && Fl::event_clicks() && Fl::event_button() == FL_LEFT_MOUSE) {
      // dwuklik na pustym biurku (poza dokiem): menedżer plików - okno LisaOS HD
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
      redraw();   // kosz zmienia się w symbol wysuwania
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
// Dialogi - osobne, modalne okna; Insight rysuje im ramkę bez przycisków
// ---------------------------------------------------------------------------
void Desktop::dialog_cb(Fl_Widget *b, void *idx) {
  auto *p = (NWin *)b->window();
  auto action = p->actions[(long)idx];
  p->desk->close(p);
  if (action) action();
}

void Desktop::dialog(const std::string &title, const std::string &text,
                     std::vector<std::pair<std::string, std::function<void()>>> buttons) {
  fl_font(FONT_SYSTEM, SIZE_SYSTEM);
  int tw = 0, th = 0;
  fl_measure(text.c_str(), tw, th, 0);
  if (tw > 440) {   // długie komunikaty (np. błędy z systemu) zawijamy
    tw = 440;
    th = 0;
    fl_measure(text.c_str(), tw, th, 0);
  }
  int W = std::max(320, tw + 100), H = std::max(130, th + 80);
  int X = (w() - W) / 2, Y = (h() - H) / 3;

  int ox = csd_mode ? FRAME : 0, oy = csd_mode ? TITLE_H : 0;
  auto *win = new NWin(this, X - ox, Y - oy, W + 2 * ox, H + oy + ox, title.c_str(), true);
  win->color(GRAY);
  // ikona-logo po lewej
  struct Logo : Fl_Widget {
    Logo(int X, int Y) : Fl_Widget(X, Y, 34, 34) {}
    void draw() override { draw_logo(x(), y(), 32); }
  };
  new Logo(ox + 16, oy + 16);
  auto *msg = new Fl_Box(ox + 64, oy + 14, W - 80, th + 8);
  msg->copy_label(text.c_str());
  msg->labelfont(FONT_SYSTEM);
  msg->labelsize(SIZE_SYSTEM);
  msg->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  int bx = ox + W - 16;
  for (int i = (int)buttons.size() - 1; i >= 0; i--) {
    bx -= 84;
    auto *b = new Fl_Button(bx, oy + H - 36, 76, 22);
    b->copy_label(buttons[i].first.c_str());
    b->box(BUTTON_BOX);
    b->down_box(BUTTON_DOWN);
    b->color(GRAY);
    b->labelfont(i == (int)buttons.size() - 1 ? FONT_SYSTEM : FONT_SMALL);
    b->labelsize(SIZE_SYSTEM);
    b->callback(dialog_cb, (void *)(long)i);
    if (i == (int)buttons.size() - 1) b->shortcut(FL_Enter);
    bx -= 6;
  }
  for (auto &b : buttons) win->actions.push_back(b.second);
  win->end();
  win->set_modal();
  wins_.push_back(win);
  win->show();
}

// ---------------------------------------------------------------------------
// Akcje menu
// ---------------------------------------------------------------------------
void Desktop::about() {
  long pages = sysconf(_SC_PHYS_PAGES), psize = sysconf(_SC_PAGE_SIZE);
  char mem[64];
  snprintf(mem, sizeof mem, "%.1f", (double)pages * psize / (1024.0 * 1024 * 1024));
  if (plat::language() == "pl") for (char *p = mem; *p; p++) if (*p == '.') *p = ',';
  std::string fltk = std::to_string(Fl::api_version() / 10000) + "." +
                     std::to_string(Fl::api_version() / 100 % 100) + "." + std::to_string(Fl::api_version() % 100);
  std::string text = std::string("LisaOS 0.1 „Tempo”\n") + _("Środowisko w duchu Mac OS 9 i Amigi") + "\n\n" +
                     plat::fmt(_("Pamięć wbudowana: %1 GB"), mem) + "\n" +
                     plat::fmt(_("Menedżer plików: Rambler 0.1 · FLTK %1"), fltk);
  dialog(_("O LisaOS"), text, {{_("OK"), nullptr}});
}

void Desktop::new_folder() {
  NWin *a = active_folder();
  std::string dir = a ? a->path : home_;
  std::string base = dir + "/" + _("bez nazwy");
  std::string p = base;
  std::error_code ec;
  for (int i = 2; fs::exists(p, ec); i++) p = base + " " + std::to_string(i);
  fs::create_directory(p, ec);
  refresh_all();
}

void Desktop::open_selected() {
  if (NWin *a = active_folder()) {
    if (const Entry *e = a->pane->view()->selected()) open_entry(*e);
  } else if (icon_sel_ >= 0) {
    if (icons_[icon_sel_].vol >= 0) open_volume(icons_[icon_sel_].vol);
    else if (icons_[icon_sel_].dock) open_entry(icons_[icon_sel_].entry);
    else open_folder(icons_[icon_sel_].path);
  }
}

void Desktop::close_active() {
  Window a = net_active();
  for (auto *w : wins_)
    if (w->shown() && fl_xid(w) == a) { close(w); return; }
}

// Kosz zgodny ze specyfikacją freedesktop.org
bool Desktop::trash_path(const std::string &src, std::string &err) {
  std::string files = trash_dir(), info = home_ + "/.local/share/Trash/info";
  std::error_code ec;
  fs::create_directories(files, ec);
  fs::create_directories(info, ec);
  std::string base = fs::path(src).filename().string(), name = base, dest = files + "/" + name;
  for (int i = 2; fs::exists(dest, ec); i++) { name = base + "." + std::to_string(i); dest = files + "/" + name; }
  fs::rename(src, dest, ec);
  if (ec) {   // inny system plików: kopia i usunięcie
    ec.clear();
    fs::copy(src, dest, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (!ec) fs::remove_all(src, ec);
  }
  if (ec) { err = "„" + base + "”: " + ec.message(); return false; }
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
  std::string errors;
  for (auto &e : a->pane->view()->selection()) {
    std::string err;
    if (!trash_path(e.path, err)) errors += err + "\n";
  }
  refresh_all();
  if (!errors.empty()) dialog(_("Kosz"), _("Nie udało się przenieść do kosza:") + std::string("\n") + errors, {{"OK", nullptr}});
}

void Desktop::empty_trash() {
  std::error_code ec;
  int n = 0;
  if (fs::exists(trash_dir(), ec))
    for (auto &x : fs::directory_iterator(trash_dir(), ec)) { (void)x; n++; }
  if (!n) { dialog(_("Kosz"), _("Kosz jest pusty."), {{"OK", nullptr}}); return; }
  dialog(_("Opróżnij kosz"),
         plat::fmt(_("Kosz zawiera %1.\nCzy na pewno usunąć je na zawsze?"), polish_items(n)),
         {{_("Anuluj"), nullptr},
          {_("Opróżnij"), [this] {
             std::error_code ec2;
             for (auto dir : {trash_dir(), home_ + "/.local/share/Trash/info"}) {
               if (!fs::exists(dir, ec2)) continue;
               for (auto &x : fs::directory_iterator(dir, ec2)) fs::remove_all(x.path(), ec2);
             }
             refresh_all();
           }}});
}

void Desktop::ask_root() {
  dialog(_("Tryb root"),
         _("Tryb root daje pełny dostęp do systemu plików\n"
           "i ustawień zaawansowanych. Wymaga hasła administratora."),
         {{_("Anuluj"), nullptr},
          {_("Przełącz"), [this] {
             if (access("/usr/bin/pkexec", X_OK) == 0) {
               spawn({"pkexec", "env", std::string("DISPLAY=") + (getenv("DISPLAY") ? getenv("DISPLAY") : ":0"),
                      self_exe(), "--root", "--home", home_});
             } else {
               dialog(_("Tryb root"), _("Nie znaleziono pkexec (pakiet polkit)."), {{"OK", nullptr}});
             }
           }}});
}

// Panel Wygląd zapisał nowe ustawienia: nowa paleta, wzór biurka i logo.
// Widgety trzymają indeksy palety, więc wystarczy je przerysować.
// Tapeta w pełnej rozdzielczości ekranu: przy skali 150% obraz ma 1,5 raza
// więcej pikseli niż jednostek FLTK, a scale() rysuje go bez rozmywania.
Fl_RGB_Image *Desktop::render_wallpaper(int W, int H, bool *ok) {
  Fl_RGB_Image *img = render_desktop(current_appearance(), px(W), px(H), ok);
  if (img && ui_scale() != 1.0f) img->scale(W, H, 0, 1);
  return img;
}

void Desktop::appearance_changed() {
  // Nowa skala interfejsu: menedżer okien i biurko muszą ruszyć od nowa
  // (wszystkie programy LisaOS liczą współrzędne w tej samej skali).
  if (std::abs(configured_scale(read_appearance()) - ui_scale()) > 0.01f) {
    reset_lisa();
    return;
  }
  reload_appearance();
  Fl_Image *old = pattern_ ? pattern_->image() : nullptr;
  pattern_.reset(make_pattern());
  delete old;
  set_logo();
  tray::recolor();
  wallpaper_.reset();
  if (!current_appearance().wallpaper.empty() || current_appearance().background != "wzor") {
    bool ok = false;
    wallpaper_.reset(render_wallpaper(w(), h(), &ok));
    // pliku tapety nie ma albo nie jest obrazem - przy tle "wzór" wystarczy szybki kafelek
    if (!ok && current_appearance().background == "wzor") wallpaper_.reset();
  }
  // Małe ikony 16x16 (rysowane na tle treści) przygotowujemy od razu. Gdyby
  // powstały dopiero w draw() paska menu, Fl_Image_Surface w środku Fl::flush()
  // przerywa odświeżanie pozostałych okien - zostawały w starych kolorach.
  warm_small_icons();
  for (Fl_Window *w = Fl::first_window(); w; w = Fl::next_window(w)) w->redraw();
}

static std::string alice_exe() {
  std::string exe = self_exe();
  std::string alice = exe.substr(0, exe.rfind('/') + 1) + "lisaos-alice";
  if (access(alice.c_str(), X_OK) != 0) alice = "lisaos-alice";   // z PATH
  return alice;
}
void Desktop::open_appearance() { spawn({alice_exe(), "wyglad"}); }

static std::string shell_exe() {
  std::string exe = self_exe();
  std::string sh = exe.substr(0, exe.rfind('/') + 1) + "lisaos-shell";
  return access(sh.c_str(), X_OK) == 0 ? sh : "lisaos-shell";
}
// Terminal w folderze aktywnego okna (albo zaznaczonego folderu), inaczej w domowym.
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
  spawn({shell_exe(), "--katalog", dir});
}
void Desktop::open_terminal_home() { spawn({shell_exe(), "--katalog", home_}); }
void Desktop::open_network() { spawn({alice_exe(), "siec"}); }
void Desktop::open_power() { spawn({alice_exe(), "zasilanie"}); }

// --- sieć ----------------------------------------------------------------------
// Menu sieci budujemy przy każdym otwarciu z ostatniego znanego stanu, a przy
// okazji prosimy o nowe skanowanie Wi-Fi (wyniki przyjdą przez net::watch).
void Desktop::build_net_menu(Fl_Menu_Button *m) {
  m->clear();
  g_menu_nets.clear();
  g_menu_ifaces.clear();
  auto item = [m](const std::string &text, Fl_Callback *cb, long data, int flags) {
    int at = m->add("x", 0, cb, (void *)data, flags);
    m->replace(at, text.c_str());   // replace() nie traktuje "/" jak podmenu
    return at;
  };
  auto self = [](Fl_Widget *w) { return (Desktop *)w->window(); };
  (void)self;
  if (!net_.available) {
    item(net_.problem.empty() ? _("Sieć niedostępna") : net_.problem, nullptr, 0, FL_MENU_INACTIVE | FL_MENU_DIVIDER);
    item(_("Ustawienia sieci…"), [](Fl_Widget *w, void *) { ((Desktop *)w->window())->open_network(); }, 0, 0);
    return;
  }
  net::rescan();

  // urządzenia inne niż Wi-Fi: stan i Połącz/Rozłącz
  for (auto &d : net_.devices) {
    if (d.type == "wifi") continue;
    std::string head = plat::fmt(_("%1 (%2): %3"), net::type_name(d.type), d.iface, net::state_name(d.state));
    item(head, nullptr, 0, FL_MENU_INACTIVE);
    g_menu_ifaces.push_back(d.iface);
    long idx = (long)g_menu_ifaces.size() - 1;
    if (d.state == net::DevState::Connected || d.state == net::DevState::Connecting)
      item(std::string("   ") + _("Rozłącz"), [](Fl_Widget *w, void *v) {
        ((Desktop *)w->window())->net_action(false, g_menu_ifaces[(long)v]); }, idx, FL_MENU_DIVIDER);
    else if (d.state == net::DevState::Disconnected)
      item(std::string("   ") + _("Połącz"), [](Fl_Widget *w, void *v) {
        ((Desktop *)w->window())->net_action(true, g_menu_ifaces[(long)v]); }, idx, FL_MENU_DIVIDER);
    else
      item(std::string("   ") + _("Kabel odłączony"), nullptr, 0, FL_MENU_INACTIVE | FL_MENU_DIVIDER);
  }

  if (net_.wifi_hw) {
    item(!net_.hotspot_ssid.empty() ? plat::fmt(_("Wi-Fi: hotspot „%1”"), net_.hotspot_ssid)
         : net_.wifi_on ? _("Wi-Fi: włączone") : _("Wi-Fi: wyłączone"), nullptr, 0, FL_MENU_INACTIVE);
    item(net_.wifi_on ? std::string("   ") + _("Wyłącz Wi-Fi") : std::string("   ") + _("Włącz Wi-Fi"), [](Fl_Widget *w, void *) {
      Desktop *d = (Desktop *)w->window();
      net::set_wifi(!d->net_state().wifi_on, [d](bool ok, const std::string &msg) {
        d->net_refresh();
        if (!ok) d->dialog_message(_("Sieć"), msg);
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
      if (net_.networks.empty()) item(_("Szukam sieci…"), nullptr, 0, FL_MENU_INACTIVE);
      for (auto &d : net_.devices)
        if (d.type == "wifi" && d.state == net::DevState::Connected) {
          g_menu_ifaces.push_back(d.iface);
          item(!net_.hotspot_ssid.empty() ? std::string(_("Wyłącz hotspot")) : plat::fmt(_("Rozłącz z „%1”"), d.connection), [](Fl_Widget *w, void *v) {
            ((Desktop *)w->window())->net_action(false, g_menu_ifaces[(long)v]); },
            (long)g_menu_ifaces.size() - 1, 0);
        }
      item(_("Inna sieć…"), [](Fl_Widget *w, void *) {
        spawn({alice_exe(), "siec", "--ukryta"}); }, 0, FL_MENU_DIVIDER);
    }
  }
  // VPN
  g_menu_vpns = net_.vpns;
  if (!g_menu_vpns.empty()) {
    item("VPN", nullptr, 0, FL_MENU_INACTIVE);
    for (size_t i = 0; i < g_menu_vpns.size(); i++) {
      auto &v = g_menu_vpns[i];
      std::string t = "   " + v.name + (v.activating ? std::string(" ") + _("(łączenie…)") : "");
      item(t, [](Fl_Widget *w, void *d) { ((Desktop *)w->window())->vpn_toggle(g_menu_vpns[(long)d], ""); },
           (long)i, FL_MENU_RADIO | (v.active ? FL_MENU_VALUE : 0) | (i + 1 == g_menu_vpns.size() ? FL_MENU_DIVIDER : 0));
    }
  }
  item(_("Tryb samolotowy"), [](Fl_Widget *w, void *) {
    Desktop *d = (Desktop *)w->window();
    net::set_airplane(!d->net_state().airplane(), [d](bool ok, const std::string &m) {
      d->net_refresh();
      if (!ok) d->dialog_message(_("Sieć"), m);
    });
  }, 0, FL_MENU_TOGGLE | (net_.airplane() ? FL_MENU_VALUE : 0) | FL_MENU_DIVIDER);
  item(_("Ustawienia sieci…"), [](Fl_Widget *w, void *) { ((Desktop *)w->window())->open_network(); }, 0, 0);
}

// VPN z menu sieci: połącz albo rozłącz. Hasło pytamy dopiero, gdy jest potrzebne;
// gdy profil nie ma nazwy użytkownika, odsyłamy do panelu Sieć.
void Desktop::vpn_toggle(const net::Vpn &v, const std::string &msg) {
  auto done = [this, v](bool ok, const std::string &m) {
    net_refresh();
    if (ok) return;
    if (m == net::NEED_PASSWORD || m == _("Nieprawidłowe hasło.")) vpn_toggle(v, m == net::NEED_PASSWORD ? " " : m);
    else dialog_message("VPN", m);
  };
  if (v.active || v.activating) { net::vpn_down(v.uuid, done); return; }
  if (msg.empty()) { net::vpn_up(v.uuid, "", "", done); return; }
  std::string user = net::vpn_username(v.uuid);
  if (user.empty()) { open_network(); return; }   // potrzebna też nazwa użytkownika
  ask_secret("VPN", Icon::Encrypted, plat::fmt(_("Połączenie VPN „%1” (%2).\nPodaj hasło."), v.name, user),
             msg == " " ? "" : msg, _("Połącz"), [v, done](std::string &p) {
    if (!p.empty()) net::vpn_up(v.uuid, "", p, done);
  });
}

void Desktop::net_refresh() {
  net_ = net::query();
  if (net_menu_) net_menu_->redraw();
}

void Desktop::net_action(bool connect, const std::string &iface) {
  auto done = [this](bool ok, const std::string &msg) { net_refresh(); if (!ok) dialog_message(_("Sieć"), msg); };
  if (connect) net::connect_device(iface, done);
  else net::disconnect_device(iface, done);
}

void Desktop::dialog_message(const std::string &title, const std::string &text) {
  dialog(title, text, {{"OK", nullptr}});
}

void Desktop::wifi_connect(const net::Network &n, const std::string &msg) {
  if (n.enterprise()) {
    dialog_message(_("Sieć Wi-Fi"), _("Sieci firmowe (WPA Enterprise, 802.1X) nie są jeszcze obsługiwane.\n"
                                   "Skonfiguruj je narzędziem nmtui albo nm-connection-editor."));
    return;
  }
  auto done = [this, n](bool ok, const std::string &m) {
    net_refresh();
    if (ok) return;
    if (m == net::NEED_PASSWORD || m == _("Nieprawidłowe hasło.") ||
        m == _("Nie udało się połączyć. Sprawdź hasło."))
      wifi_connect(n, m == net::NEED_PASSWORD ? "" : m);   // zapytaj (jeszcze raz) o hasło
    else
      dialog_message(_("Sieć Wi-Fi"), m);
  };
  // znana albo otwarta sieć: bez pytania; przy błędzie hasła pytamy
  if (msg.empty() && (!n.secured() || n.known)) {
    net::connect_wifi(n.ssid, n.security, "", false, done);
    return;
  }
  ask_secret(_("Sieć Wi-Fi"), Icon::Wifi,
             plat::fmt(_("Sieć „%1” jest zabezpieczona.\nPodaj hasło, aby się połączyć."), n.ssid), msg, _("Połącz"),
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
  tray::shutdown();   // ikony XEmbed wracają do programów, selekcja zasobnika wolna
  for (auto *w : wins_) if (w->pane) remember(w, true);
  std::vector<NWin *> all = wins_;
  for (auto *w : all) w->hide();
  hide();
}

// ---------------------------------------------------------------------------
// Menu aplikacji "Rambler"
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
// gary: menu aplikacji
// ---------------------------------------------------------------------------
// Okna Ramblera i biurko. Nie fl_find(): ono przestawia wewnętrzną listę okien
// FLTK, a wołamy to z draw() paska menu - w trakcie Fl::flush(), które po tej
// liście chodzi. Część okien nie była wtedy przerysowywana (np. po zmianie schematu).
static bool is_ours(Window w) {
  for (Fl_Window *x = Fl::first_window(); x; x = Fl::next_window(x))
    if (fl_xid(x) == w) return true;
  return false;
}

static Fl_Image *rambler_icon(bool dim) {
  Entry e;
  e.icon = Icon::Folder;
  e.dir = true;
  static std::map<Fl_Image *, Fl_Image *> dimmed;   // osobno dla każdego schematu kolorów
  Fl_Image *img = entry_icon_image(e, 16, false);
  if (!dim) return img;
  Fl_Image *&d = dimmed[img];
  if (!d) { d = img->copy(); d->inactive(); }
  return d;
}

static Fl_Image *app_icon(const gary::App &a, bool dim) {
  static std::map<std::string, std::pair<Fl_Image *, Fl_Image *>> cache;
  // kolor tła w kluczu: domyślne ikony zależą od schematu kolorów
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

// Aktywna aplikacja: okno z _NET_ACTIVE_WINDOW; biurko i okna Ramblera = Rambler.
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

// Stan menu w chwili otwarcia: akcje działają na tej migawce.
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
  for (Window w : g_menu_apps[i].wins) gary::activate(w);   // ostatnie aktywowane dostaje fokus
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

  // pozycja z ikoną: ikona + tekst (Fl_Multi_Label)
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
  texts.push_back(plat::fmt(_("Ukryj %1"), menu_escape(cur)));
  m->add(texts.back().c_str(), 0, [](Fl_Widget *w, void *) {
    hide_app((Desktop *)w->window(), g_menu_active);
  }, nullptr, 0);
  m->add(_("Ukryj inne"), 0, [](Fl_Widget *w, void *) {
    auto *self = (Desktop *)w->window();
    if (g_menu_active >= 0) hide_app(self, -1);
    for (int i = 0; i < (int)g_menu_apps.size(); i++) if (i != g_menu_active) hide_app(self, i);
  }, nullptr, 0);
  m->add(_("Pokaż wszystkie"), 0, [](Fl_Widget *w, void *) {
    auto *self = (Desktop *)w->window();
    Window keep = gary::active_window();
    for (NWin *f : self->folder_windows())
      if (gary::is_hidden(fl_xid(f))) gary::activate(fl_xid(f));
    for (auto &a : g_menu_apps)
      for (Window x : a.wins) if (gary::is_hidden(x)) gary::activate(x);
    if (keep) gary::activate(keep);   // fokus wraca tam, gdzie był
  }, nullptr, FL_MENU_DIVIDER);
  texts.push_back(plat::fmt(_("Zakończ %1"), menu_escape(cur)));
  m->add(texts.back().c_str(), 0, [](Fl_Widget *, void *) {
    if (g_menu_active >= 0) for (Window x : g_menu_apps[g_menu_active].wins) gary::close(x);
  }, nullptr, (g_menu_active < 0 ? FL_MENU_INACTIVE : 0) | FL_MENU_DIVIDER);

  // okna Ramblera w podmenu
  std::vector<NWin *> order;
  for (auto it = wins_.rbegin(); it != wins_.rend(); ++it) if ((*it)->pane) order.push_back(*it);
  if (order.empty())
    m->add(plat::tr_path(P_("Okna Ramblera/Brak otwartych okien")).c_str(), 0, nullptr, nullptr, FL_MENU_INACTIVE | FL_MENU_DIVIDER);
  Window a = net_active();
  for (size_t i = 0; i < order.size(); i++) {
    NWin *w = order[i];
    int flags = FL_MENU_RADIO | (w->shown() && fl_xid(w) == a ? FL_MENU_VALUE : 0);
    if (i + 1 == order.size()) flags |= FL_MENU_DIVIDER;
    texts.push_back(plat::tr_path(P_("Okna Ramblera")) + "/" + menu_escape(w->label()));
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
  m->add(plat::tr_path(P_("Okna Ramblera/Zwiń wszystkie")).c_str(), 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->shade_all(true); }, nullptr, on);
  m->add(plat::tr_path(P_("Okna Ramblera/Rozwiń wszystkie")).c_str(), 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->shade_all(false); }, nullptr, on | FL_MENU_DIVIDER);
  m->add(plat::tr_path(P_("Okna Ramblera/Zamknij wszystkie")).c_str(), 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->close_all(); }, nullptr, on);
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
// Zmiana nazwy, zaznaczanie, widok
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
  sync_view_menu();
}

void Desktop::view_list() {
  if (NWin *a = active_folder()) { a->pane->set_mode(FolderView::List); remember(a, true); }
  sync_view_menu();
}

// Radio "jako ikony / jako lista" pokazuje tryb aktywnego okna.
void Desktop::sync_view_menu() {
  NWin *a = active_folder();
  bool list = a && a->pane->view()->mode() == FolderView::List;
  auto *icons = (Fl_Menu_Item *)menu_->find_item(plat::tr_path(P_("Okno/Pokaż jako/Ikony")).c_str());
  auto *lst = (Fl_Menu_Item *)menu_->find_item(plat::tr_path(P_("Okno/Pokaż jako/Listę")).c_str());
  if (icons && lst) {
    if (list) { icons->clear(); lst->set(); } else { icons->set(); lst->clear(); }
  }
}

std::string Desktop::rename_path(const std::string &old, const std::string &name) {
  if (name.find('/') != std::string::npos) return _("Nazwa nie może zawierać znaku „/”.");
  if (name == "." || name == "..") return _("Taka nazwa jest zarezerwowana.");
  fs::path to = fs::path(old).parent_path() / name;
  std::error_code ec;
  if (fs::exists(to, ec)) return plat::fmt(_("Element o nazwie „%1” już istnieje."), name);
  fs::rename(old, to, ec);
  if (ec) return _("Nie udało się zmienić nazwy:") + std::string("\n") + ec.message();
  path_moved(old, to.string());
  refresh_all();
  if (NWin *w = find(fs::path(old).parent_path().string())) w->pane->view()->select_path(to.string());
  return "";
}

// Folder (lub plik) zmienił miejsce: popraw otwarte okna i pamięć pozycji.
// to == "" oznacza usunięcie (kosz) - okna tego folderu i podfolderów są zamykane.
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
  // dok idzie za przeniesionym plikiem (np. AppImage zainstalowany do Aplikacje/Gry)
  bool dock_changed = false;
  for (auto &p : dock_) {
    if (p != from && p.rfind(from + "/", 0) != 0) continue;
    if (!to.empty()) { p = to + p.substr(from.size()); dock_changed = true; }
  }
  if (dock_changed) { save_dock(); layout_icons(); redraw(); }
}

// ---------------------------------------------------------------------------
// Przenoszenie i kopiowanie
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
    dialog(_("Brak dostępu"), _("Folder docelowy jest poza LisaOS HD."), {{"OK", nullptr}});
    return;
  }
  std::error_code ec;
  // folder do samego siebie lub do własnego podfolderu
  for (auto &e : items) {
    if (e.dir && (dest == e.path || dest.rfind(e.path + "/", 0) == 0)) {
      dialog(_("Nie można przenieść"), plat::fmt(_("Folderu „%1” nie można umieścić w nim samym."), e.label),
             {{"OK", nullptr}});
      return;
    }
  }
  // konflikty nazw (pomijamy kopiowanie w obrębie tego samego folderu - dostaje "kopia")
  if (!replace) {
    std::vector<std::string> clash;
    for (auto &e : items) {
      bool same_dir = fs::path(e.path).parent_path() == fs::path(dest);
      if (same_dir) continue;
      if (fs::exists(fs::path(dest) / e.name, ec)) clash.push_back(e.name);
    }
    if (!clash.empty()) {
      std::string what = clash.size() == 1 ? plat::fmt(_("element „%1”"), clash[0])
                                           : polish_items((int)clash.size());
      dialog(copy ? _("Kopiowanie") : _("Przenoszenie"),
             plat::fmt(_("W folderze „%1” jest już %2.\nZastąpić?"), title_for(dest), what),
             {{_("Anuluj"), nullptr},
              {_("Zastąp"), [this, items, dest, copy] { transfer(items, dest, copy, true); }}});
      return;
    }
  }
  std::string errors;
  for (auto &e : items) {
    fs::path src = e.path, dst = fs::path(dest) / e.name;
    bool same_dir = src.parent_path() == fs::path(dest);
    if (same_dir && !copy) continue;                      // przeniesienie w to samo miejsce
    if (same_dir && copy) dst = fs::path(dest) / copy_name(dest, e.name);
    ec.clear();
    if (fs::exists(dst, ec) && replace && !same_dir) fs::remove_all(dst, ec);
    ec.clear();
    if (copy) {
      fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    } else {
      fs::rename(src, dst, ec);
      if (ec) {   // inny system plików: kopia i usunięcie
        ec.clear();
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (!ec) fs::remove_all(src, ec);
      }
      if (!ec) path_moved(src.string(), dst.string());
    }
    if (ec) { errors += "„" + e.name + "”: " + ec.message() + "\n"; continue; }
    // AppImage wrzucony do Aplikacji jest gotowy do uruchomienia (instalacja przeciągnięciem)
    // i od razu trafia do folderu swojej kategorii (Gry, Grafika...)
    if (is_appimage(e.name) && fs::path(dest) == fs::path(apps_dir())) {
      chmod(dst.c_str(), 0755);
      fs::path cat = app_destination(dst.string());
      if (!fs::exists(cat, ec)) {
        fs::create_directories(cat.parent_path(), ec);
        fs::rename(dst, cat, ec);
        if (!ec) path_moved(dst.string(), cat.string());
        ec.clear();
      }
    }
  }
  refresh_all();
  if (!errors.empty())
    dialog(copy ? _("Kopiowanie") : _("Przenoszenie"), _("Wystąpiły błędy:") + std::string("\n") + errors, {{"OK", nullptr}});
}

// ---------------------------------------------------------------------------
// Przeciąganie
// ---------------------------------------------------------------------------
// "Duch" przeciąganych elementów: małe okno bez ramki podążające za kursorem.
class DragGhost : public Fl_Double_Window {
public:
  DragGhost(const Entry &e, int n) : Fl_Double_Window(0, 0, 40, 40), entry_(e), n_(n) {
    clear_border();
    set_override();     // poza menedżerem okien, zawsze na wierzchu
    end();
  }
  void draw() override {
    fl_color(GRAY);
    fl_rectf(0, 0, w(), h());
    fl_color(SHADOW);
    fl_rect(0, 0, w(), h());
    draw_entry_icon(entry_, 4, 4, true, 32);
    if (n_ > 1) {   // licznik przeciąganych elementów
      std::string s = std::to_string(n_);
      fl_font(FONT_SYSTEM, 10);
      int bw = (int)fl_width(s.c_str()) + 8;
      fl_color(ROOT_TINT);
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

// Czy okno w jest (pośrednio) wewnątrz top? Potrzebne, bo menedżer okien
// opakowuje nasze okna w ramki.
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
    Fl_Widget *area = w->pane->view()->parent();   // widoczna część listy plików
    bool inside = area && lx >= area->x() && ly >= area->y() &&
                  lx < area->x() + area->w() && ly < area->y() + area->h();
    t.item = (inside && !w->shaded()) ? w->pane->view()->drop_target_at(lx, ly) : -1;
    return t;
  }
  if (is_under(child, fl_xid(this))) t.icon = icon_at(xr - x_root(), yr - y_root());
  return t;
}

void Desktop::clear_drop_highlight() {
  if (drag_.hl_win && known(drag_.hl_win)) drag_.hl_win->pane->view()->set_drop_target(-2);
  drag_.hl_win = nullptr;
  if (icon_drop_ >= 0) { icon_drop_ = -1; redraw(); }
}

void Desktop::drag_start(NWin *src, const std::vector<Entry> &items) {
  if (items.empty()) return;
  drag_.active = true;
  drag_.src = src;
  drag_.items = items;
  drag_.ghost = new DragGhost(items[0], (int)items.size());
  drag_.px = src->x_root() + src->pane->view()->press_x();
  drag_.py = src->y_root() + src->pane->view()->press_y();
  drag_.ghost->position(Fl::event_x_root() + 8, Fl::event_y_root() + 8);
  drag_.ghost->show();
}

void Desktop::drag_move(int xr, int yr) {
  if (!drag_.active) return;
  // duch jest przesunięty od kursora, więc nie zasłania celu pod kursorem
  drag_.ghost->position(xr + 8, yr + 8);
  DropTarget t = target_at(xr, yr);
  clear_drop_highlight();
  if (t.win) {
    // ten sam folder co źródło (bez wskazania podfolderu) nie jest celem
    bool same = t.win == drag_.src && t.item < 0;
    if (!same) { t.win->pane->view()->set_drop_target(t.item); drag_.hl_win = t.win; }
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
  if (t.win) dest = t.item >= 0 ? t.win->pane->view()->entry(t.item)->path : t.win->path;
  else if (t.icon >= 0) {
    dest = icons_[t.icon].path;
    if (int vi = icons_[t.icon].vol; vi >= 0) {
      copy = true;   // na inny dysk kopiujemy, jak w Mac OS
      if (dest.empty()) {
        if (vols_[vi].encrypted && !vols_[vi].unlocked) {
          dialog(_("Dysk zaszyfrowany"), plat::fmt(_("Najpierw otwórz dysk „%1” i podaj hasło."), vols_[vi].name),
                 {{"OK", nullptr}});
          return;
        }
        std::string mp, err = vol::mount(vols_[vi], mp);
        if (!err.empty()) { dialog(_("Dysk"), err, {{"OK", nullptr}}); return; }
        rescan_volumes();
        dest = mp;
      }
      if (fs::path(dest) == fs::path(trash_dir())) dest.clear();
    }
  }

  // Upuszczenie na tło tego samego okna w widoku ikon: przestawienie ikon (Workbench)
  if (t.win == src && t.item < 0 && !copy && known(src) &&
      src->pane->view()->mode() == FolderView::Icons) {
    FolderView *v = src->pane->view();
    std::vector<std::string> paths;
    for (auto &e : items) paths.push_back(e.path);
    v->move_items(paths, xr - drag_.px, yr - drag_.py);   // o tyle, o ile przesunęła się mysz
    src->pane->resize(src->pane->x(), src->pane->y(), src->pane->w(), src->pane->h());
    src->pane->redraw();
    return;
  }
  if (dest.empty() || (t.win == src && t.item < 0 && !copy)) {
    // nietrafione upuszczenie: ikona "wraca" na miejsce, jak w Mac OS
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
    if (!errors.empty()) dialog(_("Kosz"), _("Nie udało się przenieść do kosza:") + std::string("\n") + errors, {{"OK", nullptr}});
    return;
  }
  transfer(items, dest, copy, false);
}

// ---------------------------------------------------------------------------
// Kopiuj / Wklej / Duplikuj
// ---------------------------------------------------------------------------
static Entry entry_for(const std::string &path) {
  Entry e;
  e.path = path;
  e.name = fs::path(path).filename().string();
  std::error_code ec;
  e.dir = fs::is_directory(path, ec);
  e.label = e.name;
  e.icon = icon_for(e.name, e.dir);
  if (!e.dir && is_appimage(e.name)) {   // nazwa i ikona z wnętrza AppImage, jak w oknach
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
  if (!items.empty()) transfer(items, a->path, true, false);   // w tym samym folderze -> "kopia"
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
  // także dla innych programów: schowek systemowy jako lista adresów plików
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
      path = line;   // niektóre programy podają zwykłą ścieżkę
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
    if (!errors.empty()) dialog(_("Kosz"), _("Nie udało się przenieść do kosza:") + std::string("\n") + errors, {{"OK", nullptr}});
    return;
  }
  // z innego programu: kopiujemy (jak przeciąganie między dyskami w Mac OS)
  transfer(items, dest, true, false);
}

// ---------------------------------------------------------------------------
// Dyski, płyty i zaszyfrowane wolumeny
// ---------------------------------------------------------------------------
// Zmiany: nowe/usunięte urządzenia (/dev), montowania (/proc/self/mounts)
// oraz co kilka sekund (włożona płyta nie tworzy nowego urządzenia).
void Desktop::watch_volumes() {
  vols_ = vol::scan();
  layout_icons();
  int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd >= 0 && inotify_add_watch(fd, "/dev", IN_CREATE | IN_DELETE) >= 0) {
    Fl::add_fd(fd, FL_READ, [](int f, void *d) {
      char buf[4096];
      while (read(f, buf, sizeof buf) > 0) {}
      Fl::remove_timeout(rescan_later, d);
      Fl::add_timeout(0.8, rescan_later, d);   // udev potrzebuje chwili na rozpoznanie dysku
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
  // dysk zniknął lub został odmontowany: zamknij okna z jego zawartością
  for (auto &old : vols_) {
    if (old.mountpoint.empty()) continue;
    bool still = false;
    for (auto &n : now) if (n.mountpoint == old.mountpoint) still = true;
    if (!still) path_moved(old.mountpoint, "");
  }
  std::string sel_path = icon_sel_ >= 0 && icon_sel_ < (int)icons_.size() ? icons_[icon_sel_].name : "";
  vols_ = now;
  layout_icons();
  icon_sel_ = -1;
  for (int i = 0; i < (int)icons_.size(); i++) if (!sel_path.empty() && icons_[i].name == sel_path) icon_sel_ = i;
  redraw();
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
    if (!err.empty()) { dialog(_("Dysk"), err, {{"OK", nullptr}}); return; }
    rescan_volumes();
  }
  if (!mp.empty()) open_folder(mp);
}

// Hasło do dysku LUKS: osobny dialog z ukrytym polem (Fl_Secret_Input).
// Ogólne okno z polem hasła (dysk zaszyfrowany, sieć Wi-Fi).
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
  if (!msg.empty()) {   // np. "Nieprawidłowe hasło."
    auto *err = new Fl_Box(ox + 64, iy, W - 80, 18);
    err->copy_label(msg.c_str());
    err->labelfont(FONT_SYSTEM);
    err->labelsize(SIZE_SMALL);
    err->labelcolor(ROOT_TINT);
    err->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    iy += 20;
  }
  auto *pass = new Fl_Secret_Input(ox + 64, iy, W - 80, 24);
  pass->box(WELL_BOX);
  pass->textfont(FONT_SMALL);
  pass->textsize(SIZE_SYSTEM);
  int bx = ox + W - 16 - 84;
  auto make_button = [&](int X2, const char *label, bool def, int idx) {
    auto *b = new Fl_Button(X2, oy + H - 36, 76, 22, label);
    b->box(BUTTON_BOX);
    b->down_box(BUTTON_DOWN);
    b->color(GRAY);
    b->labelfont(def ? FONT_SYSTEM : FONT_SMALL);
    b->labelsize(SIZE_SYSTEM);
    b->callback(dialog_cb, (void *)(long)idx);
    if (def) b->shortcut(FL_Enter);
  };
  make_button(bx - 90, _("Anuluj"), false, 0);
  make_button(bx, ok_label, true, 1);
  win->actions.push_back(nullptr);
  win->actions.push_back([pass, on_ok] {
    std::string p = pass->value();
    // wyczyść pole od razu - hasło nie powinno wisieć w widżecie
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
  ask_secret(_("Dysk zaszyfrowany"), Icon::Encrypted,
             plat::fmt(_("Dysk „%1” jest zaszyfrowany.\nPodaj hasło, aby go otworzyć."), v.name), msg, _("Otwórz"),
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
      if (err == _("Nieprawidłowe hasło.")) ask_password(v, err);   // jeszcze raz
      else dialog(_("Dysk zaszyfrowany"), err, {{"OK", nullptr}});
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
  // najpierw zamknij okna z zawartością dysku
  if (!v.mountpoint.empty()) path_moved(v.mountpoint, "");
  cursor(FL_CURSOR_WAIT);
  Fl::flush();
  std::string err = vol::eject(v);
  cursor(FL_CURSOR_DEFAULT);
  rescan_volumes();
  if (!err.empty()) dialog(_("Wysuwanie"), err, {{"OK", nullptr}});
}

void Desktop::eject_selected() {
  if (icon_sel_ >= 0 && icon_sel_ < (int)icons_.size() && icons_[icon_sel_].vol >= 0) {
    eject_volume(icons_[icon_sel_].vol);
    return;
  }
  if (NWin *a = active_folder()) {   // aktywne okno z zawartością dysku
    int vi = volume_at_path(a->path);
    if (vi >= 0) eject_volume(vi);
  }
}
