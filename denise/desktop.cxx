// LisaOS - denise: biurko, pasek menu i zarządzanie oknami menedżera plików
#include "desktop.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

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
static void spawn(std::vector<std::string> args) {
  pid_t pid = fork();
  if (pid != 0) return;            // rodzic (lub błąd) - wracamy
  setsid();
  std::vector<char *> argv;
  for (auto &a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  execvp(argv[0], argv.data());
  _exit(127);
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
  icons_.push_back({"Dom", home_, Icon::Home, ix, MENUBAR_H + 14});
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
  if (path == home_) return "Dom";
  if (path == "/") return "System";
  if (path == trash_dir()) return "Kosz";
  return fs::path(path).filename().string();
}

// ---------------------------------------------------------------------------
// Okna
// ---------------------------------------------------------------------------
Desktop::Win *Desktop::find(PWindow *w) {
  for (auto &x : wins_) if (x.win == w) return &x;
  return nullptr;
}
Desktop::Win *Desktop::find(const std::string &path) {
  for (auto &x : wins_) if (x.pane && x.path == path) return &x;
  return nullptr;
}
Desktop::Win *Desktop::top() {
  // okno najwyżej w kolejności rysowania
  Win *best = nullptr;
  int bi = -1;
  for (auto &x : wins_) {
    int i = Fl_Group::find(x.win);
    if (i > bi) { bi = i; best = &x; }
  }
  return best;
}

void Desktop::raise(PWindow *w) {
  insert(*w, Fl_Group::find(menu_));
  for (auto &x : wins_) x.win->set_active(x.win == w);
  icon_sel_ = -1;
  if (Win *x = find(w); x && x->pane) Fl::focus(x->pane->view());
  redraw();
}

void Desktop::remember(Win &w, bool open) {
  if (!w.pane) return;
  WinState s;
  s.x = w.win->x(); s.y = w.win->y(); s.w = w.win->w();
  s.h = w.win->shaded() ? 0 : w.win->h();
  WinState old;
  if (w.win->shaded() && spatial_.get(w.path, old)) s.h = old.h;   // zachowaj pełną wysokość
  if (s.h == 0) s.h = 300;
  s.shaded = w.win->shaded();
  s.open = open;
  spatial_.set(w.path, s);
}

void Desktop::close(PWindow *w) {
  auto it = std::find_if(wins_.begin(), wins_.end(), [&](const Win &x) { return x.win == w; });
  if (it == wins_.end()) return;
  remember(*it, false);
  wins_.erase(it);
  remove(w);
  Fl::delete_widget(w);
  if (Win *t = top()) t->win->set_active(true);
  redraw();          // ikony folderów tracą kreskowanie "otwarty"
}

void Desktop::wire(PWindow *w) {
  w->on_raise = [this](PWindow *p) { if (!p->is_active() || Fl_Group::find(p) + 1 != Fl_Group::find(menu_)) raise(p); };
  w->on_close = [this](PWindow *p) { close(p); };
  w->on_geometry = [this](PWindow *p) { if (Win *x = find(p)) remember(*x, true); };
}

void Desktop::open_folder(const std::string &path) {
  if (!allowed(path)) {
    dialog("Brak dostępu",
           "Ten folder znajduje się poza Twoim Domem.\nAby go otworzyć, przełącz się w tryb root.",
           {{"OK", nullptr}});
    return;
  }
  std::error_code ec;
  if (path == trash_dir()) fs::create_directories(path, ec);
  if (Win *x = find(path)) {
    if (x->win->shaded()) x->win->toggle_shade();
    raise(x->win);
    return;
  }
  WinState s;
  bool known = spatial_.get(path, s);
  if (!known) {
    int n = (int)wins_.size();
    s.x = 40 + 26 * (n % 8);
    s.y = MENUBAR_H + 30 + 26 * (n % 8);
    s.w = 440; s.h = 290;
  }
  s.x = std::clamp(s.x, 0, std::max(0, w() - 120));
  s.y = std::clamp(s.y, MENUBAR_H, std::max(MENUBAR_H, h() - 60));

  begin();
  auto *pw = new PWindow(s.x, s.y, std::max(s.w, PWindow::MIN_W), std::max(s.h, PWindow::MIN_H),
                         title_for(path).c_str());
  pw->content()->begin();
  auto *c = pw->content();
  auto *pane = new FolderPane(c->x(), c->y(), c->w(), c->h(), path, root_);
  c->resizable(pane);
  pw->content()->end();
  end();

  pane->view()->on_open = [this](const Entry &e) { open_entry(e); };
  pane->view()->is_open = [this](const std::string &p) { return find(p) != nullptr; };
  wire(pw);
  wins_.push_back({pw, pane, path, {}});
  if (known && s.shaded) pw->toggle_shade();
  raise(pw);
  remember(wins_.back(), true);
}

void Desktop::open_entry(const Entry &e) {
  if (e.dir) { open_folder(e.path); return; }
  if (is_appimage(e.name)) {
    // AppImage: nadaj prawo wykonywania i uruchom.
    // TODO: uruchamiać przez bubblewrap z dostępem tylko do $HOME.
    chmod(e.path.c_str(), 0755);
    spawn({e.path});
    return;
  }
  spawn({"xdg-open", e.path});
}

void Desktop::restore() {
  int n = 0;
  for (auto &p : spatial_.open_paths()) {
    std::error_code ec;
    if (fs::is_directory(p, ec) && allowed(p)) { open_folder(p); n++; }
  }
  if (!n) open_folder(root_path_);
}

// ---------------------------------------------------------------------------
// Rysowanie i zdarzenia biurka
// ---------------------------------------------------------------------------
void Desktop::draw() {
  // Zawsze pełne odrysowanie (z podwójnym buforem): okna nachodzą na siebie,
  // a FLTK nie pilnuje kolejności przy częściowych odświeżeniach.
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
    // najpierw okna (od góry), potem ikony biurka
    for (int i = children() - 1; i >= 0; i--) {
      Fl_Widget *c = child(i);
      if (c == menu_ || c == extras_ || !c->visible()) continue;
      if (Fl::event_inside(c)) return Fl_Double_Window::handle(e);
    }
    int i = icon_at(Fl::event_x(), Fl::event_y());
    icon_sel_ = i;
    for (auto &x : wins_) x.win->set_active(false);
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
// Dialogi - to też okna Platinum w obrębie biurka
// ---------------------------------------------------------------------------
void Desktop::dialog_cb(Fl_Widget *b, void *idx) {
  Fl_Widget *p = b->parent();
  while (p && !dynamic_cast<PWindow *>(p)) p = p->parent();
  auto *self = (Desktop *)b->window();
  Win *x = self->find((PWindow *)p);
  if (!x) return;
  auto action = x->actions[(long)idx];
  self->close((PWindow *)p);
  if (action) action();
}

void Desktop::dialog(const std::string &title, const std::string &text,
                     std::vector<std::pair<std::string, std::function<void()>>> buttons) {
  fl_font(FONT_SYSTEM, SIZE_SYSTEM);
  int tw = 0, th = 0;
  fl_measure(text.c_str(), tw, th, 0);
  int W = std::max(320, tw + 100), H = std::max(150, th + 100);
  int X = (w() - W) / 2, Y = (h() - H) / 3;

  begin();
  auto *pw = new PWindow(X, Y, W, H, title.c_str());
  pw->set_dialog(true);
  auto *c = pw->content();
  c->begin();
  auto *bg = new Fl_Box(c->x(), c->y(), c->w(), c->h());
  bg->box(FL_FLAT_BOX);
  bg->color(GRAY);
  // ikona-logo po lewej
  struct Logo : Fl_Widget {
    Logo(int X, int Y) : Fl_Widget(X, Y, 34, 34) {}
    void draw() override { draw_logo(x(), y(), 32); }
  };
  new Logo(c->x() + 16, c->y() + 16);
  auto *msg = new Fl_Box(c->x() + 64, c->y() + 14, c->w() - 80, th + 8);
  msg->copy_label(text.c_str());
  msg->labelfont(FONT_SYSTEM);
  msg->labelsize(SIZE_SYSTEM);
  msg->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  Win rec{pw, nullptr, "", {}};
  int bx = c->x() + c->w() - 16;
  for (int i = (int)buttons.size() - 1; i >= 0; i--) {
    bx -= 84;
    auto *b = new Fl_Button(bx, c->y() + c->h() - 36, 76, 22);
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
  for (auto &b : buttons) rec.actions.push_back(b.second);
  c->end();
  end();
  wire(pw);
  wins_.push_back(std::move(rec));
  raise(pw);
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
  Win *t = top();
  std::string dir = (t && t->pane && t->win->is_active()) ? t->path : home_;
  std::string base = dir + "/bez nazwy";
  std::string p = base;
  std::error_code ec;
  for (int i = 2; fs::exists(p, ec); i++) p = base + " " + std::to_string(i);
  fs::create_directory(p, ec);
  refresh_all();
}

void Desktop::open_selected() {
  Win *t = top();
  if (t && t->pane && t->win->is_active()) {
    if (const Entry *e = t->pane->view()->selected()) open_entry(*e);
  } else if (icon_sel_ >= 0) {
    open_folder(icons_[icon_sel_].path);
  }
}

void Desktop::close_active() {
  Win *t = top();
  if (t && t->win->is_active()) close(t->win);
}

void Desktop::move_to_trash() {
  Win *t = top();
  if (!t || !t->pane || !t->win->is_active()) return;
  const Entry *e = t->pane->view()->selected();
  if (!e) return;
  // Kosz zgodny ze specyfikacją freedesktop.org
  std::string files = trash_dir(), info = home_ + "/.local/share/Trash/info";
  std::error_code ec;
  fs::create_directories(files, ec);
  fs::create_directories(info, ec);
  std::string name = e->name, dest = files + "/" + name;
  for (int i = 2; fs::exists(dest, ec); i++) { name = e->name + "." + std::to_string(i); dest = files + "/" + name; }
  std::string src = e->path;
  fs::rename(src, dest, ec);
  if (ec) {
    dialog("Kosz", "Nie udało się przenieść „" + e->name + "” do kosza:\n" + ec.message(), {{"OK", nullptr}});
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
  for (auto &x : wins_) if (x.pane) x.pane->refresh();
  layout_icons();
  redraw();
}

void Desktop::quit() {
  for (auto &x : wins_) remember(x, true);
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
  // okna w kolejności od najwyższego
  std::vector<Win *> order;
  for (auto &x : wins_) if (x.pane) order.push_back(&x);
  std::sort(order.begin(), order.end(), [this](Win *a, Win *b) {
    return Fl_Group::find(a->win) > Fl_Group::find(b->win);
  });
  if (order.empty()) {
    m->add("Brak otwartych okien", 0, nullptr, nullptr, FL_MENU_INACTIVE | FL_MENU_DIVIDER);
  }
  for (size_t i = 0; i < order.size(); i++) {
    Win *x = order[i];
    int flags = FL_MENU_RADIO | (x->win->is_active() ? FL_MENU_VALUE : 0);
    if (i + 1 == order.size()) flags |= FL_MENU_DIVIDER;
    m->add(menu_escape(x->win->label()).c_str(), 0,
           [](Fl_Widget *w, void *d) {
             auto *self = (Desktop *)w->window();
             auto *pw = (PWindow *)d;
             if (!self->find(pw)) return;
             if (pw->shaded()) pw->toggle_shade();
             self->raise(pw);
           },
           x->win, flags);
  }
  int on = order.empty() ? FL_MENU_INACTIVE : 0;
  m->add("Zwiń wszystkie", 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->shade_all(true); }, nullptr, on);
  m->add("Rozwiń wszystkie", 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->shade_all(false); }, nullptr, on | FL_MENU_DIVIDER);
  m->add("Zamknij wszystkie", 0, [](Fl_Widget *w, void *) { ((Desktop *)w->window())->close_all(); }, nullptr, on);
}

void Desktop::shade_all(bool shade) {
  for (auto &x : wins_)
    if (x.pane && x.win->shaded() != shade) x.win->toggle_shade();
  redraw();
}

void Desktop::close_all() {
  std::vector<PWindow *> v;
  for (auto &x : wins_) if (x.pane) v.push_back(x.win);
  for (auto *w : v) close(w);
}
