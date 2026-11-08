// LisaOS - denise: biurko, pasek menu i zarządzanie oknami menedżera plików
#include "desktop.h"
#include "appimage.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace plat;

// ---------------------------------------------------------------------------
// Uruchamianie programów (AppImage, xdg-open, pkexec)
// ---------------------------------------------------------------------------
static void spawn(const std::vector<std::string> &args) {
  std::vector<const char *> argv;
  for (auto &a : args) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  appimage::spawn(argv.data());    // podwójny fork - bez procesów zombie
}

static std::string self_exe() {
  std::error_code ec;
  return fs::read_symlink("/proc/self/exe", ec).string();
}

// ---------------------------------------------------------------------------
// Prawa strona paska menu: plakietka ROOT i zegar
// ---------------------------------------------------------------------------
class MenuExtras : public Fl_Widget {
public:
  MenuExtras(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {
    Fl::add_timeout(1.0, tick, this);
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
    int cx = x() + w() - 10 - (int)fl_width(buf);
    fl_color(FL_BLACK);
    fl_draw(buf, cx, y() + 14);
    if (root_mode()) {
      const char *r = "ROOT";
      int rw = (int)fl_width(r) + 10;
      cx -= rw + 10;
      fl_color(ROOT_TINT);
      fl_rounded_rectf(cx, y() + 3, rw, h() - 7, 3);
      fl_color(FL_WHITE);
      fl_draw(r, cx + 5, y() + 14);
    }
  }
};

// ---------------------------------------------------------------------------
// Menu aplikacji "Rambler": lista otwartych folderów i operacje na oknach.
// Docelowo (lisaos-gary) stanie się przełącznikiem aplikacji.
// ---------------------------------------------------------------------------
class WindowsMenu : public Fl_Menu_Button {
public:
  std::function<void(WindowsMenu *)> on_open;
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
    // ikona: dwa nachodzące na siebie okienka
    int ix = x() + 8, iy = y() + 4;
    for (int i = 0; i < 2; i++) {
      int ox = ix + i * 4, oy = iy + i * 3;
      fl_color(FL_WHITE);
      fl_rectf(ox, oy, 11, 9);
      fl_color(open_ ? fl_lighter(selection_color()) : MID);
      fl_rectf(ox, oy, 11, 3);
      fl_color(FL_BLACK);
      fl_rect(ox, oy, 11, 9);
    }
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    fl_color(open_ ? FL_WHITE : FL_BLACK);
    fl_draw("Rambler", x() + 28, y() + 14);
    // separator po lewej
    fl_color(SHADOW); fl_yxline(x(), y() + 3, y() + h() - 5);
    fl_color(LIGHT);  fl_yxline(x() + 1, y() + 3, y() + h() - 5);
  }
private:
  bool open_ = false;
};

// ---------------------------------------------------------------------------
// Wzór biurka: kafelek 64x64 w dwóch odcieniach (do zmiany w panelu Wygląd)
// ---------------------------------------------------------------------------
static Fl_Tiled_Image *make_pattern() {
  static unsigned char data[64 * 64 * 3];
  uchar ar, ag, ab, br, bg, bb;
  Fl::get_color(DESK_A, ar, ag, ab);
  Fl::get_color(DESK_B, br, bg, bb);
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++) {
      bool dot = (x % 4 == 0 && y % 4 == 0) || (x % 4 == 2 && y % 4 == 2);
      unsigned char *p = data + (y * 64 + x) * 3;
      p[0] = dot ? br : ar; p[1] = dot ? bg : ag; p[2] = dot ? bb : ab;
    }
  return new Fl_Tiled_Image(new Fl_RGB_Image(data, 64, 64, 3));
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
  box(FL_NO_BOX);
  pattern_.reset(make_pattern());

  menu_ = new Fl_Menu_Bar(0, 0, W, MENUBAR_H);
  menu_->box(MENUBAR_BOX);
  menu_->color(GRAY);
  menu_->textfont(FONT_SYSTEM);
  menu_->textsize(SIZE_SYSTEM);
  menu_->down_box(FL_FLAT_BOX);
  menu_->selection_color(root ? fl_rgb_color(0xA0, 0x30, 0x28) : fl_rgb_color(0x44, 0x44, 0xAA));

  // Fl_Callback nie przyjmie lambdy z przechwyceniem, więc wskaźniki do
  // metod trzymamy w statycznej tablicy, a w user_data podajemy indeks.
  struct Item { const char *path; int shortcut; void (Desktop::*m)(); int flags; };
  std::vector<Item> items = {
    {"logo/O LisaOS…", 0, &Desktop::about, FL_MENU_DIVIDER},
    {"logo/Panele sterowania", 0, nullptr, FL_MENU_INACTIVE},
    {"logo/Ostatnie aplikacje", 0, nullptr, FL_MENU_INACTIVE},
    {"Plik/Nowy folder", FL_COMMAND + 'n', &Desktop::new_folder, 0},
    {"Plik/Otwórz", FL_COMMAND + 'o', &Desktop::open_selected, 0},
    {"Plik/Zamknij okno", FL_COMMAND + 'w', &Desktop::close_active, FL_MENU_DIVIDER},
    {"Plik/Przenieś do kosza", FL_COMMAND + FL_BackSpace, &Desktop::move_to_trash, 0},
    {"Edycja/Cofnij", FL_COMMAND + 'z', nullptr, FL_MENU_INACTIVE | FL_MENU_DIVIDER},
    {"Edycja/Wytnij", FL_COMMAND + 'x', nullptr, FL_MENU_INACTIVE},
    {"Edycja/Kopiuj", FL_COMMAND + 'c', nullptr, FL_MENU_INACTIVE},
    {"Edycja/Wklej", FL_COMMAND + 'v', nullptr, FL_MENU_INACTIVE},
    {"Widok/jako ikony", 0, nullptr, FL_MENU_RADIO | FL_MENU_VALUE},
    {"Widok/jako lista", 0, nullptr, FL_MENU_RADIO | FL_MENU_INACTIVE | FL_MENU_DIVIDER},
    {"Widok/Odśwież", FL_COMMAND + 'r', &Desktop::refresh_all, 0},
    {"Specjalne/Opróżnij kosz…", 0, &Desktop::empty_trash, FL_MENU_DIVIDER},
    {root ? "Specjalne/Zakończ tryb root" : "Specjalne/Tryb root…", 0,
     root ? &Desktop::quit : &Desktop::ask_root, FL_MENU_DIVIDER},
    {"Specjalne/Uśpij", 0, nullptr, FL_MENU_INACTIVE},
    {"Specjalne/Uruchom ponownie", 0, nullptr, FL_MENU_INACTIVE},
    {"Specjalne/Wyłącz", FL_COMMAND + 'q', &Desktop::quit, 0},
  };
  static std::vector<void (Desktop::*)()> methods;
  for (auto &it : items) {
    methods.push_back(it.m);
    long idx = (long)methods.size() - 1;
    menu_->add(it.path, it.shortcut,
               [](Fl_Widget *w, void *d) {
                 auto *self = (Desktop *)w->window();
                 auto m = methods[(long)d];
                 if (m) (self->*m)();
               },
               (void *)idx, it.flags);
  }
  // pierwsze menu ma zamiast tekstu logo LisaOS
  if (Fl_Menu_Item *logo = (Fl_Menu_Item *)menu_->find_item("logo")) {
    static Fl_RGB_Image *img = make_logo_image();
    logo->image(img);
  }

  extras_ = new MenuExtras(W - 92 - 150, 0, 150, MENUBAR_H);
  auto *wm = new WindowsMenu(W - 92, 0, 92, MENUBAR_H);
  wm->selection_color(menu_->selection_color());
  wm->on_open = [this](WindowsMenu *m) { build_windows_menu(m); };
  end();

  // Escape nie może zamknąć biurka
  callback([](Fl_Widget *w, void *) {
    if (Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape) return;
    ((Desktop *)w)->quit();
  });

  layout_icons();
}

std::string Desktop::trash_dir() const { return home_ + "/.local/share/Trash/files"; }

bool Desktop::trash_full() const {
  std::error_code ec;
  return fs::exists(trash_dir(), ec) && !fs::is_empty(trash_dir(), ec);
}

void Desktop::layout_icons() {
  icons_.clear();
  int ix = w() - 90;
  icons_.push_back({"LisaOS HD", home_, Icon::Home, ix, MENUBAR_H + 14});
  if (root_) icons_.push_back({"System", "/", Icon::Home, ix, MENUBAR_H + 84});
  icons_.push_back({"Kosz", trash_dir(), trash_full() ? Icon::TrashFull : Icon::Trash, ix, h() - 74});
}

int Desktop::icon_at(int ex, int ey) const {
  for (int i = 0; i < (int)icons_.size(); i++) {
    const auto &ic = icons_[i];
    if (ex >= ic.x + 24 && ex < ic.x + 56 && ey >= ic.y && ey < ic.y + 52) return i;
  }
  return -1;
}

bool Desktop::allowed(const std::string &path) const {
  if (root_) return true;
  std::error_code ec;
  std::string c = fs::weakly_canonical(path, ec).string();
  std::string h = fs::weakly_canonical(home_, ec).string();
  return c == h || c.rfind(h + "/", 0) == 0;
}

std::string Desktop::title_for(const std::string &path) const {
  if (path == home_) return "LisaOS HD";
  if (path == "/") return "System";
  if (path == trash_dir()) return "Kosz";
  return fs::path(path).filename().string();
}

// ---------------------------------------------------------------------------
// EWMH: rozmowa z menedżerem okien (Insight lub dowolny zgodny)
// ---------------------------------------------------------------------------
static Desktop *g_desk = nullptr;
static constexpr int MIN_W = 200, MIN_H = 120;

static Atom atom(const char *name) { return XInternAtom(fl_display, name, False); }

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
  if (e->type == PropertyNotify && e->xproperty.window == DefaultRootWindow(fl_display) &&
      e->xproperty.atom == atom("_NET_ACTIVE_WINDOW") && g_desk)
    g_desk->active_changed();
  return 0;
}

// ---------------------------------------------------------------------------
// NWin - okno folderu lub dialogu
// ---------------------------------------------------------------------------
NWin::NWin(Desktop *d, int X, int Y, int W, int H, const char *title, bool dlg)
    : Fl_Double_Window(X, Y, W, H), desk(d), dialog(dlg) {
  copy_label(title);
  // Klasa "LisaOS-*" mówi Insightowi, że animacje otwierania robimy sami.
  xclass(dlg ? "LisaOS-Dialog" : "LisaOS-Rambler");
  callback([](Fl_Widget *w, void *) {
    auto *n = (NWin *)w;
    // Escape zamyka dialog, ale nie okno folderu (jak w Mac OS)
    if (!n->dialog && Fl::event() == FL_SHORTCUT && Fl::event_key() == FL_Escape) return;
    n->desk->close(n);
  });
}

bool NWin::is_active() { return shown() && net_active() == fl_xid(this); }

bool NWin::shaded() {
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
  if (shown()) net_message(fl_xid(this), atom("_NET_WM_STATE"), s ? 1 : 0,
                           (long)atom("_NET_WM_STATE_SHADED"), 0);
}

void NWin::activate_window() {
  show();   // FLTK: okno już widoczne zostaje wyciągnięte na wierzch
  net_message(fl_xid(this), atom("_NET_ACTIVE_WINDOW"), 1, CurrentTime, 0);
}

Rect NWin::frame_rect() {
  return {x() - FRAME, y() - TITLE_H, w() + 2 * FRAME, shaded() ? TITLE_H : h() + TITLE_H + FRAME};
}

static void remember_later(void *d) {
  auto *w = (NWin *)d;
  w->desk->remember(w, true);
}

NWin::~NWin() { Fl::remove_timeout(remember_later, this); }

int NWin::handle(int e) {
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
  return menu_->test_shortcut() ? 1 : 0;
}

void Desktop::active_changed() {
  Window a = net_active();
  for (auto it = wins_.begin(); it != wins_.end(); ++it) {
    if ((*it)->shown() && fl_xid(*it) == a) {
      NWin *w = *it;
      wins_.erase(it);
      wins_.push_back(w);           // ostatnio aktywne na końcu listy
      if (icon_sel_ >= 0) { icon_sel_ = -1; redraw(); }
      return;
    }
  }
}

void Desktop::remember(NWin *w, bool open) {
  if (!w->pane || !known(w)) return;
  WinState s;
  s.x = w->x(); s.y = w->y(); s.w = w->w(); s.h = w->h();
  s.shaded = w->shaded();
  s.open = open;
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
    dialog("Brak dostępu",
           "Ten folder znajduje się poza LisaOS HD.\nAby go otworzyć, przełącz się w tryb root.",
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
  s.w = std::max(s.w, MIN_W);
  s.h = std::max(s.h, MIN_H);
  s.x = std::clamp(s.x, FRAME, std::max(FRAME, w() - 120));
  s.y = std::clamp(s.y, MENUBAR_H + TITLE_H, std::max(MENUBAR_H + TITLE_H, h() - 60));

  if (animate) {
    Rect to = {s.x - FRAME, s.y - TITLE_H, s.w + 2 * FRAME, s.h + TITLE_H + FRAME};
    if (known_pos && s.shaded) to.h = TITLE_H;
    Rect from;
    if (!source_rect(path, from)) from = center_of(to);
    zoom_rects(from, to);
  }

  auto *win = new NWin(this, s.x, s.y, s.w, s.h, title_for(path).c_str(), false);
  win->path = path;
  win->pane = new FolderPane(0, 0, s.w, s.h, path, root_);
  win->resizable(win->pane);
  win->size_range(MIN_W, MIN_H);
  win->end();
  win->pane->view()->on_open = [this](const Entry &e) { open_entry(e); };
  win->pane->view()->is_open = [this](const std::string &p) { return find(p) != nullptr; };
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
  spawn({"xdg-open", e.path});
}

std::string Desktop::apps_dir() const { return home_ + "/Aplikacje"; }

// AppImage spoza folderu Aplikacje: zaproponuj instalację (jak sklep w telefonie,
// tylko bez sklepu). Z folderu Aplikacje: uruchom, w razie potrzeby rozpakowując.
void Desktop::open_appimage(const std::string &path) {
  std::string name = appimage::display_name(fs::path(path).filename().string());
  if (fs::path(path).parent_path() != fs::path(apps_dir())) {
    dialog("Instalacja",
           "Zainstalować aplikację „" + name + "”?\n"
           "Plik zostanie przeniesiony do folderu Aplikacje.\n" +
           (appimage::sandbox_available()
                ? "Aplikacja będzie działać w piaskownicy: zobaczy tylko LisaOS HD."
                : "Uwaga: brak bubblewrap - aplikacja będzie działać bez piaskownicy."),
           {{"Anuluj", nullptr},
            {"Zainstaluj", [this, path] {
               std::error_code ec;
               fs::create_directories(apps_dir(), ec);
               fs::path dest = fs::path(apps_dir()) / fs::path(path).filename();
               for (int i = 2; fs::exists(dest, ec); i++)
                 dest = fs::path(apps_dir()) / (fs::path(path).stem().string() + " " +
                                                std::to_string(i) + ".AppImage");
               fs::rename(path, dest, ec);
               if (ec) {
                 dialog("Instalacja", "Nie udało się przenieść pliku:\n" + ec.message(), {{"OK", nullptr}});
                 return;
               }
               chmod(dest.c_str(), 0755);
               refresh_all();
               run_app(dest.string());
             }}});
    return;
  }
  run_app(path);
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
      dialog("Nie można uruchomić", err, {{"OK", nullptr}});
      return;
    }
  }
  appimage::launch(path, home_);
}

void Desktop::restore() {
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
  pattern_->draw(0, 0, w(), h());
  fl_font(FONT_SMALL, SIZE_SMALL);
  for (int i = 0; i < (int)icons_.size(); i++) {
    auto &ic = icons_[i];
    bool sel = i == icon_sel_;
    draw_icon(ic.icon, ic.x + 24, ic.y, sel);
    int tw = (int)fl_width(ic.name.c_str());
    int tx = ic.x + 40 - tw / 2, ty = ic.y + 36;
    fl_color(sel ? FL_BLACK : FL_WHITE);
    fl_rectf(tx - 2, ty, tw + 4, fl_height());
    fl_color(sel ? FL_WHITE : FL_BLACK);
    fl_draw(ic.name.c_str(), tx, ty + fl_height() - fl_descent());
  }
  for (int i = 0; i < children(); i++) draw_child(*child(i));
}

int Desktop::handle(int e) {
  if (e == FL_PUSH && Fl::event_y() >= MENUBAR_H) {
    int i = icon_at(Fl::event_x(), Fl::event_y());
    icon_sel_ = i;
    if (i >= 0 && Fl::event_clicks()) {
      Fl::event_clicks(0);
      open_folder(icons_[i].path);
    }
    redraw();
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
  int W = std::max(320, tw + 100), H = std::max(130, th + 80);
  int X = (w() - W) / 2, Y = (h() - H) / 3;

  auto *win = new NWin(this, X, Y, W, H, title.c_str(), true);
  win->color(GRAY);
  // ikona-logo po lewej
  struct Logo : Fl_Widget {
    Logo(int X, int Y) : Fl_Widget(X, Y, 34, 34) {}
    void draw() override { draw_logo(x(), y(), 32); }
  };
  new Logo(16, 16);
  auto *msg = new Fl_Box(64, 14, W - 80, th + 8);
  msg->copy_label(text.c_str());
  msg->labelfont(FONT_SYSTEM);
  msg->labelsize(SIZE_SYSTEM);
  msg->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  int bx = W - 16;
  for (int i = (int)buttons.size() - 1; i >= 0; i--) {
    bx -= 84;
    auto *b = new Fl_Button(bx, H - 36, 76, 22);
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
  for (char *p = mem; *p; p++) if (*p == '.') *p = ',';
  std::string text = std::string("LisaOS 0.1 „Tempo”\n") +
                     "Środowisko w duchu Mac OS 9 i Amigi\n\n" +
                     "Pamięć wbudowana: " + mem + " GB\n" +
                     "Menedżer plików: Rambler 0.1 · FLTK " + std::to_string(Fl::api_version() / 10000) +
                     "." + std::to_string(Fl::api_version() / 100 % 100) + "." +
                     std::to_string(Fl::api_version() % 100);
  dialog("O LisaOS", text, {{"OK", nullptr}});
}

void Desktop::new_folder() {
  NWin *a = active_folder();
  std::string dir = a ? a->path : home_;
  std::string base = dir + "/bez nazwy";
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
    open_folder(icons_[icon_sel_].path);
  }
}

void Desktop::close_active() {
  Window a = net_active();
  for (auto *w : wins_)
    if (w->shown() && fl_xid(w) == a) { close(w); return; }
}

void Desktop::move_to_trash() {
  NWin *a = active_folder();
  if (!a) return;
  const Entry *e = a->pane->view()->selected();
  if (!e) return;
  // Kosz zgodny ze specyfikacją freedesktop.org
  std::string files = trash_dir(), info = home_ + "/.local/share/Trash/info";
  std::error_code ec;
  fs::create_directories(files, ec);
  fs::create_directories(info, ec);
  std::string name = e->name, dest = files + "/" + name;
  for (int i = 2; fs::exists(dest, ec); i++) { name = e->name + "." + std::to_string(i); dest = files + "/" + name; }
  std::string src = e->path, shown_name = e->name;
  fs::rename(src, dest, ec);
  if (ec) {
    dialog("Kosz", "Nie udało się przenieść „" + shown_name + "” do kosza:\n" + ec.message(), {{"OK", nullptr}});
    return;
  }
  char date[32];
  time_t now = time(nullptr);
  strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", localtime(&now));
  std::ofstream(info + "/" + name + ".trashinfo")
      << "[Trash Info]\nPath=" << src << "\nDeletionDate=" << date << "\n";
  refresh_all();
}

void Desktop::empty_trash() {
  std::error_code ec;
  int n = 0;
  if (fs::exists(trash_dir(), ec))
    for (auto &x : fs::directory_iterator(trash_dir(), ec)) { (void)x; n++; }
  if (!n) { dialog("Kosz", "Kosz jest pusty.", {{"OK", nullptr}}); return; }
  dialog("Opróżnij kosz",
         "Kosz zawiera " + polish_items(n) + ".\nCzy na pewno usunąć je na zawsze?",
         {{"Anuluj", nullptr},
          {"Opróżnij", [this] {
             std::error_code ec2;
             for (auto dir : {trash_dir(), home_ + "/.local/share/Trash/info"}) {
               if (!fs::exists(dir, ec2)) continue;
               for (auto &x : fs::directory_iterator(dir, ec2)) fs::remove_all(x.path(), ec2);
             }
             refresh_all();
           }}});
}

void Desktop::ask_root() {
  dialog("Tryb root",
         "Tryb root daje pełny dostęp do systemu plików\n"
         "i ustawień zaawansowanych. Wymaga hasła administratora.",
         {{"Anuluj", nullptr},
          {"Przełącz", [this] {
             if (access("/usr/bin/pkexec", X_OK) == 0) {
               spawn({"pkexec", "env", std::string("DISPLAY=") + (getenv("DISPLAY") ? getenv("DISPLAY") : ":0"),
                      self_exe(), "--root", "--home", home_});
             } else {
               dialog("Tryb root", "Nie znaleziono pkexec (pakiet polkit).", {{"OK", nullptr}});
             }
           }}});
}

void Desktop::refresh_all() {
  for (auto *w : wins_) if (w->pane) w->pane->refresh();
  layout_icons();
  redraw();
}

void Desktop::quit() {
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

void Desktop::build_windows_menu(Fl_Menu_Button *m) {
  m->clear();
  // okna od ostatnio aktywnego
  std::vector<NWin *> order;
  for (auto it = wins_.rbegin(); it != wins_.rend(); ++it) if ((*it)->pane) order.push_back(*it);
  if (order.empty()) {
    m->add("Brak otwartych okien", 0, nullptr, nullptr, FL_MENU_INACTIVE | FL_MENU_DIVIDER);
  }
  Window a = net_active();
  for (size_t i = 0; i < order.size(); i++) {
    NWin *w = order[i];
    int flags = FL_MENU_RADIO | (w->shown() && fl_xid(w) == a ? FL_MENU_VALUE : 0);
    if (i + 1 == order.size()) flags |= FL_MENU_DIVIDER;
    m->add(menu_escape(w->label()).c_str(), 0,
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
  m->add("Zwiń wszystkie", 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->shade_all(true); }, nullptr, on);
  m->add("Rozwiń wszystkie", 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->shade_all(false); }, nullptr, on | FL_MENU_DIVIDER);
  m->add("Zamknij wszystkie", 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->close_all(); }, nullptr, on);
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
