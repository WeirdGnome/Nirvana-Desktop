// LisaOS - Rambler: akcje menu Lisa, Okno, Ikony i Narzędzia
//
// Układ menu według specyfikacji (w duchu Workbencha):
//   Lisa       - polecenia całego systemu (preferencje, wykonaj polecenie, dziennik,
//                Lisa Shell, przerysuj/uaktualnij wszystko, uśpij, restart, zakończ)
//   Okno       - aktywne okno folderu (nowy folder, rodzic, widok, sortowanie, ukryte)
//   Ikony      - zaznaczone ikony (otwórz, kopiuj, informacje, zamroź, dok)
//   Narzędzia  - formatowanie dysku, kosz, tryb root, reset środowiska
#include "desktop.h"

#include "appimage.h"
#include "power.h"
#include "volumes.h"
#include "tray.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace plat;

static int g_argc = 0;
static char **g_argv = nullptr;
void Desktop::set_argv(int argc, char **argv) { g_argc = argc; g_argv = argv; }

static std::string config_dir() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/lisaos";
}

// ---------------------------------------------------------------------------
// Formularz w oknie dialogowym Ramblera (ikona, tekst, pola, przyciski)
// ---------------------------------------------------------------------------
struct FormIcon : Fl_Widget {
  Entry e;
  bool logo;
  FormIcon(int X, int Y, const Entry &en, bool lg) : Fl_Widget(X, Y, 34, 34), e(en), logo(lg) {}
  void draw() override {
    if (logo) draw_logo(x(), y(), 32);
    else draw_entry_icon(e, x(), y(), false, 32);
  }
};

NWin *Desktop::begin_form(const std::string &title, int W, int H, const Entry *icon, const std::string &text,
                          int &ox, int &oy) {
  ox = csd_mode ? FRAME : 0;
  oy = csd_mode ? TITLE_H : 0;
  int X = (w() - W) / 2, Y = std::max(MENUBAR_H + 10, (h() - H) / 3);
  auto *win = new NWin(this, X - ox, Y - oy, W + 2 * ox, H + oy + ox, "", true);
  win->copy_label(title.c_str());
  win->color(GRAY);
  new FormIcon(ox + 16, oy + 16, icon ? *icon : Entry(), icon == nullptr);
  if (!text.empty()) {
    auto *msg = new Fl_Box(ox + 64, oy + 14, W - 80, 36);
    msg->copy_label(text.c_str());
    msg->labelfont(FONT_SYSTEM);
    msg->labelsize(SIZE_SYSTEM);
    msg->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  }
  return win;
}

void Desktop::end_form(NWin *win, int W, int H, int ox, int oy,
                       std::vector<std::pair<std::string, std::function<void()>>> buttons) {
  int bx = ox + W - 16;
  for (int i = (int)buttons.size() - 1; i >= 0; i--) {
    bx -= 84;
    auto *b = new Fl_Button(bx, oy + H - 36, 78, 22);
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

static Fl_Box *row_label(int X, int Y, int W, const char *t) {
  auto *b = new Fl_Box(X, Y, W, 18);
  b->copy_label(t);
  b->labelsize(SIZE_SMALL + 1);
  b->labelfont(FONT_SYSTEM);
  b->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
  return b;
}
static Fl_Box *row_value(int X, int Y, int W, const std::string &t) {
  auto *b = new Fl_Box(X, Y, W, 18);
  b->copy_label(t.c_str());
  b->labelsize(SIZE_SMALL + 1);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  return b;
}

// ---------------------------------------------------------------------------
// Stan menu: aktywne/nieaktywne pozycje, znaczniki
// ---------------------------------------------------------------------------
std::vector<std::string> Desktop::targets() {
  std::vector<std::string> v;
  if (NWin *a = active_folder())
    for (auto &e : a->pane->view()->selection()) v.push_back(e.path);
  if (v.empty() && !active_folder() && icon_sel_ >= 0 && icon_sel_ < (int)icons_.size() && !icons_[icon_sel_].win)
    v.push_back(icons_[icon_sel_].path);
  return v;
}

void Desktop::update_menu() {
  NWin *a = active_folder();
  bool win_sel = a && !a->pane->view()->selection().empty();
  bool desk_sel = !a && icon_sel_ >= 0 && icon_sel_ < (int)icons_.size() && !icons_[icon_sel_].win;
  const DeskIcon *di = desk_sel ? &icons_[icon_sel_] : nullptr;
  bool vol_sel = di && di->vol >= 0;
  bool icons_mode = a && a->pane->view()->mode() == FolderView::Icons;
  auto act = [this](const char *path, bool on) {
    if (auto *it = (Fl_Menu_Item *)menu_->find_item(plat::tr_path(path).c_str())) { if (on) it->activate(); else it->deactivate(); }
  };
  auto mark = [this](const char *path, bool on) {
    if (auto *it = (Fl_Menu_Item *)menu_->find_item(plat::tr_path(path).c_str())) { if (on) it->set(); else it->clear(); }
  };
  bool parent_ok = false;
  if (a) {
    std::string par = fs::path(a->path).parent_path().string();
    parent_ok = par != a->path && allowed(par) && a->path != home_;
  }
  act(P_("Okno/Otwórz katalog nadrzędny"), parent_ok);
  for (const char *p : {P_("Okno/Zamknij"), P_("Okno/Uaktualnij"), P_("Okno/Zaznacz zawartość"), P_("Okno/Pokaż jako"),
                        P_("Okno/Sortuj"), P_("Okno/Wielkość ikon"), P_("Okno/Lisa Shell tutaj")})
    act(p, a != nullptr);
  act(P_("Okno/Uporządkuj"), icons_mode);
  mark(P_("Okno/Pokaż ukryte pliki"), show_hidden_);
  if (a) {
    FolderView *v = a->pane->view();
    mark(P_("Okno/Pokaż jako/Ikony"), v->mode() == FolderView::Icons);
    mark(P_("Okno/Pokaż jako/Listę"), v->mode() == FolderView::List);
    mark(P_("Okno/Sortuj/według nazwy"), v->sort_key() == FolderView::ByName);
    mark(P_("Okno/Sortuj/według daty"), v->sort_key() == FolderView::ByDate);
    mark(P_("Okno/Sortuj/według rozmiaru"), v->sort_key() == FolderView::BySize);
    mark(P_("Okno/Sortuj/według rodzaju"), v->sort_key() == FolderView::ByKind);
    mark(P_("Okno/Wielkość ikon/Małe"), v->icon_size() == 16);
    mark(P_("Okno/Wielkość ikon/Normalne"), v->icon_size() == 32);
    mark(P_("Okno/Wielkość ikon/Duże"), v->icon_size() == 64);
  }
  bool file_sel = win_sel;
  act(P_("Ikony/Otwórz"), win_sel || desk_sel);
  act(P_("Ikony/Kopiuj"), file_sel);
  act(P_("Ikony/Wklej"), !clipboard_.empty());
  act(P_("Ikony/Duplikuj"), file_sel);
  act(P_("Ikony/Zmień nazwę…"), file_sel);
  act(P_("Ikony/Usuń"), file_sel);
  act(P_("Ikony/Informacje…"), win_sel || desk_sel);
  {
    std::vector<std::string> t = targets();
    act(P_("Ikony/Ustaw jako tapetę"), t.size() == 1 && is_image_file(t[0]));
  }
  act(P_("Ikony/Zamroź"), icons_mode);
  act(P_("Ikony/Odmroź"), icons_mode);
  bool in_dock = di && di->dock;
  if (a)
    for (auto &e : a->pane->view()->selection())
      if (std::find(dock_.begin(), dock_.end(), e.path) != dock_.end()) in_dock = true;
  act(P_("Ikony/Połóż na doku"), file_sel);
  act(P_("Ikony/Zdejmij z doku"), in_dock);
  act(P_("Ikony/Wysuń"), vol_sel && (vols_[di->vol].removable || !vols_[di->vol].mountpoint.empty()));
  act(P_("Narzędzia/Formatuj dysk…"), vol_sel && !vols_[di->vol].optical);
}

// Ikony > Ustaw jako tapetę: zapis w ustawieniach wyglądu, biurko odświeża się
// przez _LISAOS_WYGLAD (tak samo jak po "Zastosuj" w panelu Wygląd).
void Desktop::set_as_wallpaper() {
  std::vector<std::string> t = targets();
  if (t.size() != 1 || !is_image_file(t[0])) return;
  if (!image_loadable(t[0])) {
    dialog(_("Tapeta"), plat::fmt(_("Nie udało się otworzyć obrazu „%1”.\nPlik może być uszkodzony albo w nieobsługiwanym formacie."),
                               fs::path(t[0]).filename().string()), {{"OK", nullptr}});
    return;
  }
  Appearance a = read_appearance();
  a.wallpaper = t[0];
  if (!write_appearance(a)) {
    dialog(_("Tapeta"), _("Nie udało się zapisać ustawień wyglądu."), {{"OK", nullptr}});
    return;
  }
  notify_appearance_changed();
}

// ---------------------------------------------------------------------------
// Menu Lisa
// ---------------------------------------------------------------------------
void Desktop::open_preferences() { rambler_spawn({rambler_exe("lisaos-alice"), "ustawienia"}); }

void Desktop::execute_command() {
  static std::string last;
  NWin *a = active_folder();
  std::string dir = a ? a->path : home_;
  int W = 440, H = 160, ox, oy;
  NWin *win = begin_form(_("Wykonaj polecenie"), W, H, nullptr,
                         plat::fmt(_("Polecenie do wykonania (w folderze „%1”):"), title_for(dir)), ox, oy);
  auto *in = new Fl_Input(ox + 64, oy + 50, W - 80, 24);
  in->box(WELL_BOX);
  in->textfont(FL_COURIER);
  in->textsize(SIZE_SYSTEM);
  in->value(last.c_str());
  in->insert_position(0, in->size());
  auto *show = new Fl_Check_Button(ox + 60, oy + 80, W - 80, 20, _("Pokaż wynik w Lisa Shell"));
  show->labelsize(SIZE_SMALL + 1);
  static bool show_last = true;
  show->value(show_last);
  end_form(win, W, H, ox, oy, {{_("Anuluj"), nullptr}, {_("Wykonaj"), [in, show, dir] {
    std::string cmd = in->value();
    if (cmd.empty()) return;
    last = cmd;
    show_last = show->value();
    // katalog i polecenie jako argumenty powłoki - bez sklejania cudzysłowów
    const char *script = show_last
        ? "cd -- \"$1\" || exit 1; eval \"$2\"; s=$?; echo; printf \"$3\" \"$s\"; read x"
        : "cd -- \"$1\" || exit 1; eval \"$2\"";
    if (show_last)
      rambler_spawn({rambler_exe("lisaos-shell"), "--katalog", dir, "-e", "sh", "-c", script, "sh", dir, cmd,
                     _("[kod wyjścia %s] Enter zamyka okno…")});
    else
      rambler_spawn({"sh", "-c", script, "sh", dir, cmd});
  }}});
  in->take_focus();
}

void Desktop::open_log() {
  // dziennik systemowy: journalctl (systemd), inaczej plik syslog, inaczej dmesg
  const char *script =
      "if command -v journalctl >/dev/null 2>&1 && journalctl -n 1 >/dev/null 2>&1; then journalctl -b -e; "
      "elif [ -r /var/log/syslog ]; then less +G /var/log/syslog; "
      "elif [ -r /var/log/messages ]; then less +G /var/log/messages; "
      "else dmesg 2>&1 | less +G; fi";
  rambler_spawn({rambler_exe("lisaos-shell"), "--katalog", home_, "-e", "sh", "-c", script});
}

// Przerysuj wszystko: na chwilę przykrywamy ekran przezroczystym oknem - po jego
// zniknięciu każdy program dostaje Expose i rysuje się od nowa (jak xrefresh).
void Desktop::redraw_all() {
  Display *d = fl_display;
  Window root = DefaultRootWindow(d);
  XSetWindowAttributes a{};
  a.override_redirect = True;
  a.background_pixmap = None;
  a.backing_store = NotUseful;
  int scr = DefaultScreen(d);
  Window w = XCreateWindow(d, root, 0, 0, DisplayWidth(d, scr), DisplayHeight(d, scr), 0, CopyFromParent,
                           InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixmap | CWBackingStore, &a);
  XMapWindow(d, w);
  XSync(d, False);
  XDestroyWindow(d, w);
  XSync(d, False);
  Fl::redraw();
}

void Desktop::sleep_now() {
  if (power::projection()) {
    dialog(_("Uśpij"), _("Tryb projekcji jest włączony - wyłącz go w menu baterii, żeby uśpić komputer."), {{"OK", nullptr}});
    return;
  }
  std::string err = power::suspend();
  if (!err.empty()) dialog(_("Uśpij"), err, {{"OK", nullptr}});
}

void Desktop::reboot_ask() {
  dialog(_("Uruchom ponownie"), _("Uruchomić komputer ponownie?"),
         {{_("Anuluj"), nullptr}, {_("Uruchom ponownie"), [this] {
            for (auto *w : wins_) if (w->pane) remember(w, true);
            std::string err = power::reboot();
            if (!err.empty()) dialog(_("Uruchom ponownie"), err, {{"OK", nullptr}});
          }}});
}

void Desktop::quit_ask() {
  dialog(_("Zakończ"), _("Zakończyć pracę z LisaOS?"),
         {{_("Anuluj"), nullptr},
          {_("Wyloguj"), [this] {
             if (!csd_mode) { quit(); return; }
             rambler_spawn({"sh", "-c", "gnome-session-quit --logout --no-prompt 2>/dev/null || "
                                        "loginctl terminate-session \"$XDG_SESSION_ID\""});
           }},
          {_("Wyłącz"), [this] {
             for (auto *w : wins_) if (w->pane) remember(w, true);
             std::string err = power::poweroff();
             if (!err.empty()) dialog(_("Wyłącz"), err, {{"OK", nullptr}});
           }}});
}

// ---------------------------------------------------------------------------
// Menu Okno
// ---------------------------------------------------------------------------
void Desktop::open_parent() {
  NWin *a = active_folder();
  if (!a) return;
  std::string par = fs::path(a->path).parent_path().string();
  if (par == a->path || !allowed(par) || a->path == home_) return;
  open_folder(par);
}

void Desktop::update_window() {
  if (NWin *a = active_folder()) a->pane->refresh();
}

void Desktop::clean_up() {
  if (NWin *a = active_folder()) {
    a->pane->view()->clean_up();
    a->pane->resize(a->pane->x(), a->pane->y(), a->pane->w(), a->pane->h());
    a->pane->redraw();
  }
}

void Desktop::toggle_hidden() {
  show_hidden_ = !show_hidden_;
  for (auto *w : wins_) if (w->pane) w->pane->set_hidden(show_hidden_);
  save_rambler_conf();
}

void Desktop::save_rambler_conf() {
  std::ofstream(config_dir() + "/rambler.conf") << "ukryte=" << (show_hidden_ ? 1 : 0) << "\n"
                                               << "dok_zwiniety=" << (dock_collapsed_ ? 1 : 0) << "\n";
}

void Desktop::set_sort(FolderView::SortKey k) {
  if (NWin *a = active_folder()) {
    a->pane->view()->set_sort(k);
    a->pane->resize(a->pane->x(), a->pane->y(), a->pane->w(), a->pane->h());
    a->pane->redraw();
  }
}
void Desktop::sort_name() { set_sort(FolderView::ByName); }
void Desktop::sort_date() { set_sort(FolderView::ByDate); }
void Desktop::sort_size() { set_sort(FolderView::BySize); }
void Desktop::sort_kind() { set_sort(FolderView::ByKind); }

void Desktop::set_icon_size(int s) {
  if (NWin *a = active_folder()) {
    if (a->pane->view()->mode() != FolderView::Icons) a->pane->set_mode(FolderView::Icons);
    a->pane->set_icon_size(s);
    remember(a, true);
  }
}
void Desktop::icons_small() { set_icon_size(16); }
void Desktop::icons_normal() { set_icon_size(32); }
void Desktop::icons_large() { set_icon_size(64); }

// ---------------------------------------------------------------------------
// Menu Ikony: Informacje (Get Info / Information z Workbencha)
// ---------------------------------------------------------------------------
static std::string fmt_time(time_t t) {
  if (!t) return "—";
  return plat::date_time(t);
}
static std::string fmt_size(uintmax_t b) {
  char s[64];
  if (b < 1024) snprintf(s, sizeof s, "%ju B", b);
  else if (b < 1024 * 1024) snprintf(s, sizeof s, "%.1f KB", b / 1024.0);
  else if (b < 1024ull * 1024 * 1024) snprintf(s, sizeof s, "%.1f MB", b / 1048576.0);
  else snprintf(s, sizeof s, "%.2f GB", b / 1073741824.0);
  std::string r = s;
  r = plat::decimal(r);
  if (b >= 1024) r += " (" + std::to_string(b) + " B)";
  return r;
}
// Rozmiar folderu - z limitem czasu, żeby duży katalog nie zamroził Ramblera.
static std::string folder_size(const std::string &p) {
  uintmax_t total = 0;
  long files = 0;
  auto start = std::chrono::steady_clock::now();
  std::error_code ec;
  fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    std::error_code e2;
    if (it->is_regular_file(e2)) { total += it->file_size(e2); files++; }
    if ((files & 255) == 0 && std::chrono::steady_clock::now() - start > std::chrono::seconds(2))
      return plat::fmt(_("ponad %1 (liczenie przerwane)"), fmt_size(total));
  }
  return plat::fmt(_("%1, plików: %2"), fmt_size(total), std::to_string(files));
}

// Komentarz (xattr user.xdg.comment - standard freedesktop) i parametry
// uruchamiania (ToolTypes: user.lisaos.parametry, zapasowo plik w ~/.config).
static std::string xattr_get(const std::string &p, const char *name, bool *supported = nullptr) {
  char buf[4096];
  ssize_t n = getxattr(p.c_str(), name, buf, sizeof buf - 1);
  if (supported) *supported = n >= 0 || errno == ENODATA;
  return n > 0 ? std::string(buf, n) : "";
}
static bool xattr_set(const std::string &p, const char *name, const std::string &v) {
  if (v.empty()) return removexattr(p.c_str(), name) == 0 || errno == ENODATA;
  return setxattr(p.c_str(), name, v.data(), v.size(), 0) == 0;
}
static std::string tooltypes_file() { return config_dir() + "/parametry"; }
std::string tooltypes(const std::string &path) {
  bool ok = false;
  std::string v = xattr_get(path, "user.lisaos.parametry", &ok);
  if (!v.empty()) return v;
  std::ifstream in(tooltypes_file());
  std::string line;
  while (std::getline(in, line)) {
    auto tab = line.find('\t');
    if (tab != std::string::npos && line.substr(tab + 1) == path) return line.substr(0, tab);
  }
  return "";
}
static void set_tooltypes(const std::string &path, const std::string &v) {
  if (xattr_set(path, "user.lisaos.parametry", v)) return;
  // system plików bez atrybutów rozszerzonych: zapis w pliku
  std::vector<std::string> keep;
  std::ifstream in(tooltypes_file());
  std::string line;
  while (std::getline(in, line)) {
    auto tab = line.find('\t');
    if (tab != std::string::npos && line.substr(tab + 1) != path) keep.push_back(line);
  }
  in.close();
  std::ofstream out(tooltypes_file());
  for (auto &l : keep) out << l << '\n';
  if (!v.empty()) out << v << '\t' << path << '\n';
}
// Podział parametrów jak w powłoce (spacje, cudzysłowy).
std::vector<std::string> split_args(const std::string &s) {
  std::vector<std::string> v;
  std::string cur;
  bool any = false;
  char q = 0;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (q) { if (c == q) q = 0; else cur += c; continue; }
    if (c == '"' || c == '\'') { q = c; any = true; continue; }
    if (c == '\\' && i + 1 < s.size()) { cur += s[++i]; any = true; continue; }
    if (isspace((unsigned char)c)) { if (any || !cur.empty()) v.push_back(cur); cur.clear(); any = false; continue; }
    cur += c;
  }
  if (any || !cur.empty()) v.push_back(cur);
  return v;
}

void Desktop::get_info() {
  auto t = targets();
  if (t.empty()) return;
  std::string path = t[0];
  // dysk z biurka: pojemność i system plików
  int vi = -1;
  if (!active_folder() && icon_sel_ >= 0 && icon_sel_ < (int)icons_.size()) vi = icons_[icon_sel_].vol;
  Entry e;
  e.path = path;
  e.name = fs::path(path).filename().string();
  std::error_code ec;
  e.dir = fs::is_directory(path, ec);
  e.icon = icon_for(e.name, e.dir);
  e.label = e.name;
  if (NWin *a = active_folder())
    for (auto &x : a->pane->view()->selection()) if (x.path == path) e = x;
  if (vi >= 0) { e.icon = vols_[vi].icon; e.label = vols_[vi].name; }
  if (path == home_) { e.icon = Icon::Home; e.label = "LisaOS HD"; }

  struct stat st{};
  bool have_st = !path.empty() && lstat(path.c_str(), &st) == 0;
  bool is_exec = have_st && S_ISREG(st.st_mode);
  int W = 420, ox, oy;
  int rows = 8;
  bool tool = is_exec && (is_appimage(e.name) || (st.st_mode & S_IXUSR));
  int H = 64 + rows * 20 + 90 + (tool ? 30 : 0) + 44;
  NWin *win = begin_form(plat::fmt(_("Informacje: %1"), e.label), W, H, &e, "", ox, oy);
  auto *name = new Fl_Box(ox + 64, oy + 20, W - 80, 24);
  name->copy_label(e.label.c_str());
  name->labelfont(FONT_SYSTEM);
  name->labelsize(SIZE_SYSTEM + 2);
  name->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  int y = oy + 62, lx = ox + 10, lw = 120, vx = ox + 136, vw = W - 150;
  auto row = [&](const char *l, const std::string &v) {
    row_label(lx, y, lw, l);
    row_value(vx, y, vw, v);
    y += 20;
  };
  if (vi >= 0) {
    const vol::Volume &v = vols_[vi];
    row(_("Rodzaj:"), v.optical ? _("płyta") : v.removable ? _("dysk wymienny") : _("dysk"));
    row(_("System plików:"), v.unformatted ? _("brak (niesformatowany)") : v.fstype);
    row(_("Pojemność:"), fmt_size(v.size));
    struct statvfs sv{};
    if (!v.mountpoint.empty() && statvfs(v.mountpoint.c_str(), &sv) == 0)
      row(_("Wolne:"), fmt_size((uintmax_t)sv.f_bavail * sv.f_frsize));
    row(_("Urządzenie:"), v.dev);
    row(_("Zamontowany:"), v.mountpoint.empty() ? std::string(_("nie")) : v.mountpoint);
    row(_("Szyfrowanie:"), v.encrypted ? (v.unlocked ? _("LUKS (otwarty)") : _("LUKS (zamknięty)")) : _("brak"));
    y += 20;
  } else {
    row(_("Rodzaj:"), e.dir ? std::string(_("folder")) : kind_name(e));
    row(_("Rozmiar:"), e.dir ? folder_size(path) : have_st ? fmt_size((uintmax_t)st.st_size) : "—");
    row(_("Położenie:"), fs::path(path).parent_path().string());
    struct statx sx{};
    time_t born = 0;
    if (statx(AT_FDCWD, path.c_str(), 0, STATX_BTIME, &sx) == 0 && (sx.stx_mask & STATX_BTIME)) born = sx.stx_btime.tv_sec;
    row(_("Utworzono:"), born ? fmt_time(born) : "—");
    row(_("Zmieniono:"), have_st ? fmt_time(st.st_mtime) : "—");
    row(_("Otwarto:"), have_st ? fmt_time(st.st_atime) : "—");
    std::string owner = "—";
    if (have_st) {
      passwd *pw = getpwuid(st.st_uid);
      group *gr = getgrgid(st.st_gid);
      owner = std::string(pw ? pw->pw_name : std::to_string(st.st_uid)) + " / " +
              (gr ? gr->gr_name : std::to_string(st.st_gid));
    }
    row(_("Właściciel:"), owner);
    char perm[16];
    snprintf(perm, sizeof perm, "%04o", have_st ? st.st_mode & 07777 : 0);
    row(_("Uprawnienia:"), perm);
  }
  // flagi i komentarz
  Fl_Check_Button *ro = nullptr, *ex = nullptr;
  Fl_Input *comment = nullptr, *args = nullptr;
  if (vi < 0 && have_st) {
    row_label(lx, y, lw, _("Flagi:"));
    ro = new Fl_Check_Button(vx - 4, y - 1, 140, 20, _("Tylko do odczytu"));
    ro->labelsize(SIZE_SMALL + 1);
    ro->value(!(st.st_mode & S_IWUSR));
    if (S_ISREG(st.st_mode)) {
      ex = new Fl_Check_Button(vx + 136, y - 1, 140, 20, _("Program"));
      ex->labelsize(SIZE_SMALL + 1);
      ex->value((st.st_mode & S_IXUSR) != 0);
      ex->tooltip(_("Prawo uruchamiania (jak flaga e w Workbenchu)"));
    }
    y += 26;
    bool xa = false;
    std::string c = xattr_get(path, "user.xdg.comment", &xa);
    row_label(lx, y + 2, lw, _("Komentarz:"));
    comment = new Fl_Input(vx, y, vw, 22);
    comment->box(WELL_BOX);
    comment->textsize(SIZE_SMALL + 1);
    comment->value(c.c_str());
    if (!xa) { comment->deactivate(); comment->tooltip(_("Ten system plików nie obsługuje komentarzy.")); }
    y += 28;
    if (tool) {
      row_label(lx, y + 2, lw, _("Parametry:"));
      args = new Fl_Input(vx, y, vw, 22);
      args->box(WELL_BOX);
      args->textfont(FL_COURIER);
      args->textsize(SIZE_SMALL + 1);
      args->value(tooltypes(path).c_str());
      args->tooltip(_("Parametry uruchamiania programu (ToolTypes), np. --fullscreen"));
      y += 28;
    }
  }
  bool owner_ok = have_st && (st.st_uid == getuid() || root_);
  if (!owner_ok) { if (ro) ro->deactivate(); if (ex) ex->deactivate(); }
  end_form(win, W, H, ox, oy, {{_("Anuluj"), nullptr}, {"OK", [this, path, ro, ex, comment, args, have_st, st] {
    std::string err;
    if (have_st && (ro || ex)) {
      mode_t m = st.st_mode & 07777;
      if (ro) m = ro->value() ? (m & ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH)) : (m | S_IWUSR);
      if (ex) m = ex->value() ? (m | S_IXUSR | ((m & S_IRGRP) ? S_IXGRP : 0) | ((m & S_IROTH) ? S_IXOTH : 0))
                              : (m & ~(mode_t)(S_IXUSR | S_IXGRP | S_IXOTH));
      if (m != (st.st_mode & 07777) && chmod(path.c_str(), m) != 0) err += _("Nie udało się zmienić flag.") + std::string("\n");
    }
    if (comment && comment->active()) xattr_set(path, "user.xdg.comment", comment->value());
    if (args) set_tooltypes(path, args->value());
    refresh_all();
    if (!err.empty()) dialog(_("Informacje"), err, {{"OK", nullptr}});
  }}});
}

// ---------------------------------------------------------------------------
// Zamrożenie (Snapshot) i dok (Leave Out / Put Away)
// ---------------------------------------------------------------------------
void Desktop::snapshot() {
  NWin *a = active_folder();
  if (!a) return;
  FolderView *v = a->pane->view();
  std::vector<std::string> p;
  for (auto &e : v->selection()) p.push_back(e.path);
  if (p.empty()) p = v->all_paths();   // nic nie zaznaczono: całe okno
  v->snapshot(p);
}

void Desktop::unsnapshot() {
  NWin *a = active_folder();
  if (!a) return;
  FolderView *v = a->pane->view();
  std::vector<std::string> p;
  for (auto &e : v->selection()) p.push_back(e.path);
  if (p.empty()) p = v->all_paths();
  v->unsnapshot(p);
  a->pane->resize(a->pane->x(), a->pane->y(), a->pane->w(), a->pane->h());
  a->pane->redraw();
}

void Desktop::load_dock() {
  dock_.clear();
  std::ifstream in(config_dir() + "/dok");
  std::string line;
  std::error_code ec;
  while (std::getline(in, line))
    if (!line.empty() && fs::exists(line, ec)) dock_.push_back(line);
}

void Desktop::save_dock() {
  std::ofstream out(config_dir() + "/dok");
  for (auto &p : dock_) out << p << '\n';
}

void Desktop::dock_add() {
  NWin *a = active_folder();
  if (!a) return;
  for (auto &e : a->pane->view()->selection())
    if (std::find(dock_.begin(), dock_.end(), e.path) == dock_.end()) dock_.push_back(e.path);
  save_dock();
  layout_icons();
  redraw();
}

void Desktop::dock_remove() {
  std::vector<std::string> rm;
  if (NWin *a = active_folder()) for (auto &e : a->pane->view()->selection()) rm.push_back(e.path);
  else if (icon_sel_ >= 0 && icon_sel_ < (int)icons_.size() && icons_[icon_sel_].dock) rm.push_back(icons_[icon_sel_].path);
  for (auto &p : rm) dock_.erase(std::remove(dock_.begin(), dock_.end(), p), dock_.end());
  save_dock();
  icon_sel_ = -1;
  layout_icons();
  redraw();
}

// ---------------------------------------------------------------------------
// Menu Narzędzia
// ---------------------------------------------------------------------------
void Desktop::format_disk() {
  if (active_folder() || icon_sel_ < 0 || icon_sel_ >= (int)icons_.size() || icons_[icon_sel_].vol < 0) {
    dialog(_("Formatuj dysk"), _("Zaznacz na biurku ikonę dysku, który chcesz sformatować."), {{"OK", nullptr}});
    return;
  }
  format_dialog(icons_[icon_sel_].vol);
}

void Desktop::format_dialog(int vi) {
  vol::Volume v = vols_[vi];
  Entry e;
  e.icon = v.icon;
  int W = 440, H = 220, ox, oy;
  NWin *win = begin_form(_("Formatuj dysk"), W, H, &e,
                         plat::fmt(_("Formatowanie „%1” (%2)."), v.name, vol::human_size(v.size)), ox, oy);
  auto *warn = new Fl_Box(ox + 64, oy + 40, W - 80, 18, _("Wszystkie dane na tym dysku zostaną usunięte!"));
  warn->labelcolor(ROOT_TINT);
  warn->labelfont(FONT_SYSTEM);
  warn->labelsize(SIZE_SMALL + 1);
  warn->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  auto *name = new Fl_Input(ox + 180, oy + 70, W - 196, 22, _("Nazwa:"));
  name->box(WELL_BOX);
  name->labelsize(SIZE_SMALL + 1);
  name->value(v.unformatted ? "DYSK" : v.name.substr(0, 11).c_str());
  auto *fsch = new Fl_Choice(ox + 180, oy + 100, W - 196, 22, _("System plików:"));
  fsch->labelsize(SIZE_SMALL + 1);
  fsch->textsize(SIZE_SMALL + 1);
  fsch->add(_("FAT32 (pendrive - działa wszędzie)|exFAT (duże pliki, Windows i Mac)|ext4 (Linux)|NTFS (Windows)"));
  fsch->value(v.size > 32ull * 1024 * 1024 * 1024 ? 1 : 0);
  std::string uuid = v.uuid, vname = v.name;
  end_form(win, W, H, ox, oy, {{_("Anuluj"), nullptr}, {_("Formatuj"), [this, name, fsch, uuid, vname] {
    static const char *types[] = {"vfat", "exfat", "ext4", "ntfs"};
    std::string type = types[std::max(0, fsch->value())], label = name->value();
    if (type == "vfat") { label = label.substr(0, 11); for (auto &c : label) c = (char)toupper((unsigned char)c); }
    dialog(_("Formatuj dysk"), plat::fmt(_("Na pewno sformatować „%1”? Tej operacji nie da się cofnąć."), vname),
           {{_("Anuluj"), nullptr}, {_("Formatuj"), [this, uuid, type, label] {
              int idx = -1;
              for (int i = 0; i < (int)vols_.size(); i++) if (vols_[i].uuid == uuid) idx = i;
              if (idx < 0) { dialog(_("Formatuj dysk"), _("Dysk zniknął."), {{"OK", nullptr}}); return; }
              for (auto *w : wins_) w->cursor(FL_CURSOR_WAIT);
              cursor(FL_CURSOR_WAIT);
              Fl::flush();
              std::string err = vol::format(vols_[idx], type, label);
              for (auto *w : wins_) w->cursor(FL_CURSOR_DEFAULT);
              cursor(FL_CURSOR_DEFAULT);
              rescan_volumes();
              dialog(_("Formatuj dysk"), err.empty() ? plat::fmt(_("Dysk „%1” jest gotowy."), label) : err, {{"OK", nullptr}});
            }}});
  }}});
}

// Reset LISA: Insight i Rambler uruchamiają się od nowa (okna programów zostają).
void Desktop::reset_lisa() {
  for (auto *w : wins_) if (w->pane) remember(w, true);
  spatial_.save();
  Display *d = fl_display;
  XEvent ev{};
  ev.xclient.type = ClientMessage;
  ev.xclient.window = DefaultRootWindow(d);
  ev.xclient.message_type = XInternAtom(d, "_LISAOS_RESTART", False);
  ev.xclient.format = 32;
  XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(d);
  Fl::add_timeout(0.6, [](void *) {
    std::string exe = rambler_exe("lisaos-denise");
    std::vector<char *> av;
    av.push_back((char *)exe.c_str());
    for (int i = 1; i < g_argc; i++) av.push_back(g_argv[i]);
    av.push_back(nullptr);
    tray::shutdown();
    XCloseDisplay(fl_display);
    execv(exe.c_str(), av.data());
    _exit(1);
  });
}
