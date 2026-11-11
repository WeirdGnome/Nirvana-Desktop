// Nirvana Desktop - libplatinum: schematy kolorów, ustawienia wyglądu, wzory biurka
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/platform.H>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

#if defined(FLTK_USE_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

namespace plat {

// Indeksy palety. Cztery pierwsze to standardowe kolory FLTK - ich zmiana
// przestawia też wszystkie zwykłe widgety (przyciski, pola, menu).
Fl_Color GRAY = FL_BACKGROUND_COLOR, TEXT = FL_FOREGROUND_COLOR,
         CONTENT = FL_BACKGROUND2_COLOR, SEL = FL_SELECTION_COLOR;
// Pozostałe w wolnym obszarze palety FL_FREE_COLOR..FL_FREE_COLOR+15.
Fl_Color CONTENT_TEXT  = FL_FREE_COLOR + 0, SEL_TEXT     = FL_FREE_COLOR + 1,
         ICON_SEL      = FL_FREE_COLOR + 2, ICON_SEL_TEXT = FL_FREE_COLOR + 3,
         LIGHT         = FL_FREE_COLOR + 4, MID          = FL_FREE_COLOR + 5,
         SHADOW        = FL_FREE_COLOR + 6, DARK         = FL_FREE_COLOR + 7,
         HILITE        = FL_FREE_COLOR + 8, STRIPE       = FL_FREE_COLOR + 9,
         TITLE_TEXT    = FL_FREE_COLOR + 10, TITLE_I_TEXT = FL_FREE_COLOR + 11,
         DESK_A        = FL_FREE_COLOR + 12, DESK_B       = FL_FREE_COLOR + 13,
         ALERT         = FL_FREE_COLOR + 14;

const char *const PATTERNS[] = {"gladki", "kropki", "groszki", "krata", "kratka", "szachownica",
                                "ukosne", "paski_poziome", "paski_pionowe", "cegly", "raster", "romby",
                                "krzyzyki", "zygzak", "jodelka", "tkanina", "fale", "luski", "gwiazdki",
                                "piasek", nullptr};
const char *const PATTERN_NAMES[] = {N_("Plain"), N_("Dots"), N_("Polka dots"), N_("Grid"), N_("Fine grid"), N_("Checkerboard"),
                                     N_("Diagonal stripes"), N_("Horizontal stripes"), N_("Vertical stripes"), N_("Bricks"),
                                     N_("Raster (Amiga)"), N_("Diamonds"), N_("Crosses"), N_("Zigzag"), N_("Herringbone"), N_("Weave"),
                                     N_("Waves"), N_("Scales"), N_("Stars"), N_("Sand"), nullptr};
const char *const BACKGROUNDS[] = {"wzor", "kolor", "gradient", nullptr};
const char *const BACKGROUND_NAMES[] = {N_("Pattern"), N_("Solid color"), N_("Gradient"), nullptr};
const char *const GRADIENTS[] = {"pionowy", "poziomy", "ukosny", "promienisty", nullptr};
const char *const GRADIENT_NAMES[] = {N_("Vertical"), N_("Horizontal"), N_("Diagonal"), N_("Radial"), nullptr};

static Scheme g_scheme;
static Appearance g_app;
static TitleStyle g_style = TitleStyle::Stripes;

const Scheme &current_scheme() { return g_scheme; }
const Appearance &current_appearance() { return g_app; }
TitleStyle title_style() { return g_style; }

// ---------------------------------------------------------------------------
// Kolory RGB (0xRRGGBB)
// ---------------------------------------------------------------------------
static unsigned mix(unsigned a, unsigned b, double t) {
  auto ch = [&](int sh) {
    double x = ((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t;
    return (unsigned)std::lround(std::clamp(x, 0.0, 255.0)) << sh;
  };
  return ch(16) | ch(8) | ch(0);
}
static double lum(unsigned c) {
  return (0.299 * ((c >> 16) & 0xFF) + 0.587 * ((c >> 8) & 0xFF) + 0.114 * (c & 0xFF)) / 255.0;
}
static void set(Fl_Color i, unsigned c) {
  Fl::set_color(i, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static bool parse_color(std::string v, unsigned &out) {
  while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();
  if (v.size() == 4 && v[0] == '#') v = std::string("#") + v[1] + v[1] + v[2] + v[2] + v[3] + v[3];
  if (v.size() != 7 || v[0] != '#') return false;
  char *end;
  unsigned long x = strtoul(v.c_str() + 1, &end, 16);
  if (*end) return false;
  out = (unsigned)x;
  return true;
}
static std::string color_str(unsigned c) {
  char b[8];
  snprintf(b, sizeof b, "#%06X", c & 0xFFFFFF);
  return b;
}

// ---------------------------------------------------------------------------
// Pliki
// ---------------------------------------------------------------------------
static std::string home_dir() { const char *h = getenv("HOME"); return h ? h : "/"; }

static std::string exe_dir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return "";
  buf[n] = 0;
  std::string p = buf;
  return p.substr(0, p.rfind('/'));
}

static bool is_dir(const std::string &p) { struct stat st; return !stat(p.c_str(), &st) && S_ISDIR(st.st_mode); }

std::vector<std::string> scheme_dirs() {
  std::vector<std::string> d;
  const char *xdg = getenv("XDG_DATA_HOME");
  d.push_back((xdg && *xdg ? std::string(xdg) : home_dir() + "/.local/share") + "/nirvana/schematy");
  std::string e = exe_dir();
  if (!e.empty()) {
    d.push_back(e + "/../share/nirvana/schematy");   // instalacja w prefiksie
    d.push_back(e + "/../schematy");                // uruchomienie z katalogu build/
  }
  d.push_back("/usr/local/share/nirvana/schematy");
  d.push_back("/usr/share/nirvana/schematy");
  std::vector<std::string> out;
  for (auto &x : d) if (is_dir(x)) out.push_back(x);
  return out;
}

// nazwy schematów z plików w schematy/ - do tłumaczenia (panel Wygląd woła _(nazwa))
[[maybe_unused]] static const char *const SCHEME_NAMES[] = {N_("Graphite")};

Scheme builtin_platinum() {
  Scheme s;
  s.id = "platinum"; s.name = "Platinum"; s.source = "Nirvana Desktop";
  s.style = TitleStyle::Stripes;
  s.icon_sel = 0x000000; s.accent = 0xAAAAE6; s.desk = 0x5E6FA8;
  s.light = 0xFFFFFF; s.mid = 0xAAAAAA; s.shadow = 0x777777;
  return s;
}

bool parse_scheme(const std::string &file, Scheme &s) {
  std::ifstream in(file);
  if (!in) return false;
  s = Scheme();
  // Domyślnie bez osobnego paska nieaktywnego: wyliczymy go niżej.
  bool have_i = false, have_i2 = false, have_t2 = false, have_title = false, have_okno = false;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    while (!k.empty() && isspace((unsigned char)k.back())) k.pop_back();
    while (!v.empty() && isspace((unsigned char)v.front())) v.erase(0, 1);
    while (!v.empty() && (v.back() == '\r' || isspace((unsigned char)v.back()))) v.pop_back();
    unsigned c = 0;
    bool col = parse_color(v, c);
    if (k == "nazwa") s.name = v;
    else if (k == "zrodlo") s.source = v;
    else if (k == "styl_paska")
      s.style = v == "prazki" ? TitleStyle::Stripes : v == "gradient" ? TitleStyle::Gradient : TitleStyle::Solid;
    else if (!col) continue;
    else if (k == "okno") { s.window = c; have_okno = true; }
    else if (k == "tekst") s.text = c;
    else if (k == "tresc") s.content = c;
    else if (k == "tekst_tresci") s.content_text = c;
    else if (k == "zaznaczenie") s.sel = c;
    else if (k == "tekst_zaznaczenia") s.sel_text = c;
    else if (k == "zaznaczenie_ikon") s.icon_sel = (int)c;
    else if (k == "obramowanie") s.border = c;
    else if (k == "pasek") { s.title = c; have_title = true; }
    else if (k == "pasek2") { s.title2 = c; have_t2 = true; }
    else if (k == "tekst_paska") s.title_text = c;
    else if (k == "pasek_nieaktywny") { s.title_i = c; have_i = true; }
    else if (k == "pasek_nieaktywny2") { s.title_i2 = c; have_i2 = true; }
    else if (k == "tekst_paska_nieaktywnego") s.title_i_text = c;
    else if (k == "akcent") s.accent = (int)c;
    else if (k == "biurko") s.desk = (int)c;
    else if (k == "jasny") s.light = (int)c;
    else if (k == "cien_sredni") s.mid = (int)c;
    else if (k == "cien") s.shadow = (int)c;
  }
  if (!have_okno) return false;
  if (!have_title) s.title = s.window;
  if (!have_t2) s.title2 = s.title;
  if (!have_i) s.title_i = s.window;
  if (!have_i2) s.title_i2 = s.title_i;
  if (s.name.empty()) {
    s.name = file.substr(file.rfind('/') + 1);
    s.name = s.name.substr(0, s.name.rfind('.'));
  }
  return true;
}

static void scan(const std::string &root, const std::string &rel, std::vector<Scheme> &out,
                 std::set<std::string> &seen, int depth) {
  DIR *d = opendir((root + (rel.empty() ? "" : "/" + rel)).c_str());
  if (!d) return;
  while (dirent *e = readdir(d)) {
    std::string n = e->d_name;
    if (n[0] == '.') continue;
    std::string r = rel.empty() ? n : rel + "/" + n;
    std::string full = root + "/" + r;
    if (is_dir(full)) {
      if (depth < 2) scan(root, r, out, seen, depth + 1);
    } else if (n.size() > 5 && n.compare(n.size() - 5, 5, ".conf") == 0) {
      std::string id = r.substr(0, r.size() - 5);
      if (seen.count(id)) continue;
      Scheme s;
      if (!parse_scheme(full, s)) continue;
      s.id = id;
      seen.insert(id);
      out.push_back(s);
    }
  }
  closedir(d);
}

std::vector<Scheme> list_schemes() {
  std::vector<Scheme> out;
  std::set<std::string> seen;
  for (auto &dir : scheme_dirs()) scan(dir, "", out, seen, 0);
  if (!seen.count("platinum")) out.push_back(builtin_platinum());
  // Najpierw własne schematy Nirvana Desktop, potem zaimportowane - alfabetycznie.
  std::sort(out.begin(), out.end(), [](const Scheme &a, const Scheme &b) {
    bool la = a.id.find('/') == std::string::npos, lb = b.id.find('/') == std::string::npos;
    if (la != lb) return la;
    if (la && (a.id == "platinum") != (b.id == "platinum")) return a.id == "platinum";
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  return out;
}

bool load_scheme(const std::string &id, Scheme &out) {
  if (id.empty() || id.find("..") != std::string::npos) return false;
  for (auto &dir : scheme_dirs())
    if (parse_scheme(dir + "/" + id + ".conf", out)) { out.id = id; return true; }
  if (id == "platinum") { out = builtin_platinum(); return true; }
  return false;
}

std::string appearance_file() {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  return (xdg && *xdg ? std::string(xdg) : home_dir() + "/.config") + "/nirvana/wyglad.conf";
}

Appearance read_appearance() {
  Appearance a;
  std::ifstream in(appearance_file());
  std::string line;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();
    unsigned c;
    if (k == "schemat" && !v.empty()) a.scheme = v;
    else if (k == "styl_paska")
      a.style = v == "prazki" ? (int)TitleStyle::Stripes : v == "kolor" ? (int)TitleStyle::Solid
              : v == "gradient" ? (int)TitleStyle::Gradient : -1;
    else if (k == "biurko") a.desk = parse_color(v, c) ? (int)c : -1;
    else if (k == "wzor") {
      for (int i = 0; PATTERNS[i]; i++) if (v == PATTERNS[i]) a.pattern = v;
    } else if (k == "animacje") a.animations = !(v == "0" || v == "nie");
    else if (k == "tapeta") a.wallpaper = v;
    else if (k == "ikony") a.icons = v == "platinum" ? "platinum" : "workbench";
    else if (k == "tlo") {
      for (int i = 0; BACKGROUNDS[i]; i++) if (v == BACKGROUNDS[i]) a.background = v;
    } else if (k == "biurko2") a.desk2 = parse_color(v, c) ? (int)c : -1;
    else if (k == "gradient") {
      for (int i = 0; GRADIENTS[i]; i++) if (v == GRADIENTS[i]) a.gradient = v;
    }
    else if (k == "skala") a.scale = std::clamp(atoi(v.c_str()), 0, 400);
    else if (k == "tapeta_uklad") {
      for (int i = 0; WALLPAPER_MODES[i]; i++) if (v == WALLPAPER_MODES[i]) a.wallpaper_mode = v;
    }
  }
  return a;
}

bool write_appearance(const Appearance &a) {
  std::string f = appearance_file();
  std::string dir = f.substr(0, f.rfind('/'));
  std::string parent = dir.substr(0, dir.rfind('/'));
  mkdir(parent.c_str(), 0755);
  mkdir(dir.c_str(), 0755);
  std::string tmp = f + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out) return false;
    static const char *st[] = {"prazki", "kolor", "gradient"};
    out << "# Nirvana Desktop - appearance settings (Appearance panel)\n"
        << "schemat=" << a.scheme << "\n"
        << "styl_paska=" << (a.style < 0 ? "" : st[a.style]) << "\n"
        << "biurko=" << (a.desk < 0 ? "" : color_str((unsigned)a.desk)) << "\n"
        << "tlo=" << a.background << "\n"
        << "wzor=" << a.pattern << "\n"
        << "biurko2=" << (a.desk2 < 0 ? "" : color_str((unsigned)a.desk2)) << "\n"
        << "gradient=" << a.gradient << "\n"
        << "animacje=" << (a.animations ? 1 : 0) << "\n"
        << "ikony=" << a.icons << "\n"
        << "tapeta=" << a.wallpaper << "\n"
        << "tapeta_uklad=" << a.wallpaper_mode << "\n"
        << "skala=" << a.scale << "\n";
    if (!out) return false;
  }
  return rename(tmp.c_str(), f.c_str()) == 0;
}

// ---------------------------------------------------------------------------
// Zastosowanie schematu
// ---------------------------------------------------------------------------
void apply(const Scheme &s, const Appearance &a) {
  g_scheme = s;
  g_app = a;
  g_style = a.style >= 0 ? (TitleStyle)a.style : s.style;
  // Gradient wybrany ręcznie dla schematu z jednolitym paskiem: drugi kolor
  // to rozjaśniony pierwszy (jak w Windows 98).
  if (g_style == TitleStyle::Gradient) {
    if (g_scheme.title2 == g_scheme.title) g_scheme.title2 = mix(g_scheme.title, 0xFFFFFF, 0.45);
    if (g_scheme.title_i2 == g_scheme.title_i) g_scheme.title_i2 = mix(g_scheme.title_i, 0xFFFFFF, 0.3);
  }
  unsigned face = s.window;
  bool light_face = lum(face) > 0.5;

  Fl::background((face >> 16) & 0xFF, (face >> 8) & 0xFF, face & 0xFF);
  Fl::background2((s.content >> 16) & 0xFF, (s.content >> 8) & 0xFF, s.content & 0xFF);
  Fl::foreground((s.text >> 16) & 0xFF, (s.text >> 8) & 0xFF, s.text & 0xFF);
  unsigned sel = s.sel;
  set(FL_SELECTION_COLOR, sel);

  set(CONTENT_TEXT, s.content_text);
  set(SEL_TEXT, s.sel_text);
  set(ICON_SEL, s.icon_sel >= 0 ? (unsigned)s.icon_sel : sel);
  set(ICON_SEL_TEXT, s.icon_sel < 0 ? s.sel_text : (lum((unsigned)s.icon_sel) > 0.5 ? 0x000000 : 0xFFFFFF));
  set(ALERT, 0xC0392B);
  set(LIGHT, s.light >= 0 ? (unsigned)s.light : mix(face, 0xFFFFFF, light_face ? 1.0 : 0.22));
  set(MID, s.mid >= 0 ? (unsigned)s.mid : mix(face, 0x000000, 0.23));
  set(SHADOW, s.shadow >= 0 ? (unsigned)s.shadow : mix(face, 0x000000, 0.46));
  set(DARK, s.border);
  set(HILITE, s.accent >= 0 ? (unsigned)s.accent : face);
  set(STRIPE, mix(face, 0x000000, light_face ? 0.37 : 0.45));

  // Tytuł: w prążkach (Platinum) piszemy kolorem tekstu na tle okna, na
  // kolorowym pasku - kolorami paska ze schematu.
  if (g_style == TitleStyle::Stripes && s.style != TitleStyle::Stripes) {
    set(TITLE_TEXT, s.text);
    set(TITLE_I_TEXT, mix(s.text, face, 0.55));
  } else {
    set(TITLE_TEXT, s.title_text);
    set(TITLE_I_TEXT, s.title_i_text);
  }

  unsigned desk = a.desk >= 0 ? (unsigned)a.desk
                : s.desk >= 0 ? (unsigned)s.desk
                : mix(s.sel, face, 0.15);
  set(DESK_A, desk);
  set(DESK_B, lum(desk) > 0.6 ? mix(desk, 0x000000, 0.08) : mix(desk, 0xFFFFFF, 0.09));
}

void reload_appearance() {
  Appearance a = read_appearance();
  Scheme s;
  if (!load_scheme(a.scheme, s)) s = builtin_platinum();
  apply(s, a);
}

// ---------------------------------------------------------------------------
// Wzory biurka
// ---------------------------------------------------------------------------
unsigned char *pattern_tile(const std::string &pattern, int &w, int &h) {
  w = h = 8;
  if (pattern == "cegly" || pattern == "gwiazdki" || pattern == "piasek" || pattern == "luski") w = h = 16;
  else if (pattern == "fale") { w = 16; h = 8; }
  unsigned char *d = new unsigned char[w * h * 3];
  uchar a[3], b[3];
  Fl::get_color(DESK_A, a[0], a[1], a[2]);
  Fl::get_color(DESK_B, b[0], b[1], b[2]);
  unsigned seed = 12345;   // "Piasek": zawsze ten sam, powtarzalny szum
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      bool on = false;
      const std::string &p = pattern;
      if (p == "kropki") on = (x % 4 == 0 && y % 4 == 0) || (x % 4 == 2 && y % 4 == 2);
      else if (p == "groszki") on = (x % 4 < 2 && y % 4 < 2 && ((x / 4 + y / 4) % 2 == 0));
      else if (p == "krata") on = x % 8 == 0 || y % 8 == 0;
      else if (p == "kratka") on = x % 4 == 0 || y % 4 == 0;
      else if (p == "szachownica") on = (x / 4 + y / 4) % 2 == 0;
      else if (p == "ukosne") on = (x + y) % 4 == 0;
      else if (p == "paski_poziome") on = y % 4 == 0;
      else if (p == "paski_pionowe") on = x % 4 == 0;
      else if (p == "cegly") on = y % 8 == 0 || (y < 8 ? x == 0 : x == 8);
      else if (p == "raster") on = (x + y) % 2 == 0;
      else if (p == "romby") on = x == y || x + y == 8 || (x == 0 && y == 8);   // siatka ukośnych linii
      else if (p == "krzyzyki") {
        int dx = x - 3, dy = y - 3;
        on = (dx == dy || dx == -dy) && std::abs(dx) <= 1;
      } else if (p == "zygzak") on = y == std::abs(x - 4) || y == std::abs(x - 4) + 4;
      else if (p == "jodelka") on = x < 4 ? (x + y) % 4 == 0 : ((x - y) % 4 + 4) % 4 == 0;
      else if (p == "tkanina") on = ((x / 4 + y / 4) % 2 == 0) ? y % 2 == 0 : x % 2 == 0;
      else if (p == "fale") on = y == (int)std::lround(3.5 + 2.5 * std::sin(x * 2 * M_PI / 16));
      else if (p == "luski") {
        // rzędy półkoli, co drugi przesunięty o pół łuski
        int row = y / 8;
        double cy = row * 8, best = 1e9;
        for (int cx = (row % 2 ? 0 : 8) - 16; cx <= 32; cx += 16)
          best = std::min(best, std::hypot(x - cx + 0.5, y - cy + 0.5));
        on = std::abs(best - 7.6) < 0.7;
      } else if (p == "gwiazdki") {
        // duża gwiazdka (krzyżyk o ramionach 2) i mała (ramiona 1), na przemian
        auto star = [&](int cx, int cy, int r) {
          int dx = std::abs(x - cx), dy = std::abs(y - cy);
          return (dx == 0 && dy <= r) || (dy == 0 && dx <= r);
        };
        on = star(3, 3, 2) || star(11, 11, 1);
      } else if (p == "piasek") {
        seed = seed * 1103515245u + 12345u;
        on = ((seed >> 16) % 100) < 18;
      }
      memcpy(d + (y * w + x) * 3, on ? b : a, 3);
    }
  return d;
}

unsigned gradient_second(const Appearance &a) {
  if (a.desk2 >= 0) return (unsigned)a.desk2;
  uchar r, g, b;
  Fl::get_color(DESK_A, r, g, b);
  // bez wyboru: ten sam kolor, wyraźnie ciemniejszy (jasne biurka) albo jaśniejszy (ciemne)
  double l = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
  double k = l > 0.25 ? 0.55 : 1.8;
  auto c = [&](uchar v) { return (unsigned)std::min(255.0, v * k + (k > 1 ? 18 : 0)); };
  return (c(r) << 16) | (c(g) << 8) | c(b);
}

// ---------------------------------------------------------------------------
// Powiadomienie innych programów (Rambler, Insight) o zmianie
// ---------------------------------------------------------------------------
void notify_appearance_changed() {
#if defined(FLTK_USE_X11)
  fl_open_display();
  Display *dpy = fl_display;
  if (!dpy) return;
  Atom at = XInternAtom(dpy, APPEARANCE_ATOM, False);
  long v = (long)time(nullptr) ^ (long)getpid() ^ (long)clock();
  XChangeProperty(dpy, RootWindow(dpy, fl_screen), at, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&v, 1);
  XFlush(dpy);
#endif
}

} // namespace plat
