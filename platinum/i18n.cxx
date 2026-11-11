// Nirvana Desktop - translations (see i18n.h)
#include "i18n.h"

#include <algorithm>
#include <cerrno>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <vector>
#include <sstream>
#include <functional>
#include <fstream>
#include <cstdint>
#include <unordered_map>
#include <unistd.h>

namespace fs = std::filesystem;

namespace plat {

static const char *DOMAIN = "nirvana";
static bool g_active = false;          // do we translate (false = the English texts from the code)
static std::string g_lang = "en";

// We read the .mo catalog ourselves instead of through gettext(): glibc does not translate with
// LC_MESSAGES=C/C.UTF-8, and a minimal system often has no generated
// locales (en_US.UTF-8). The .mo format: a header, two tables (length, offset).
static std::string g_mo;                                         // the whole file
static std::unordered_map<std::string, const char *> g_tr;      // msgid -> translation
static std::unordered_map<std::string, std::vector<const char *>> g_trp;   // msgid -> plural forms
static int g_nplurals = 2;

static bool load_mo(const std::string &file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return false;
  g_mo.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  auto u32 = [](const char *p, bool swap) {
    uint32_t v;
    memcpy(&v, p, 4);
    return swap ? __builtin_bswap32(v) : v;
  };
  if (g_mo.size() < 28) return false;
  uint32_t magic;
  memcpy(&magic, g_mo.data(), 4);
  bool swap = magic == 0xde120495;
  if (!swap && magic != 0x950412de) return false;
  uint32_t n = u32(&g_mo[8], swap), oo = u32(&g_mo[12], swap), to = u32(&g_mo[16], swap);
  if ((uint64_t)oo + 8ull * n > g_mo.size() || (uint64_t)to + 8ull * n > g_mo.size()) return false;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t ol = u32(&g_mo[oo + 8 * i], swap), op = u32(&g_mo[oo + 8 * i + 4], swap);
    uint32_t tl = u32(&g_mo[to + 8 * i], swap), tp = u32(&g_mo[to + 8 * i + 4], swap);
    if ((uint64_t)op + ol >= g_mo.size() || (uint64_t)tp + tl >= g_mo.size()) continue;
    const char *orig = &g_mo[op], *tr = &g_mo[tp];
    std::string key(orig, strnlen(orig, ol));
    if (key.empty()) {   // header: Plural-Forms: nplurals=N
      const char *np = strstr(tr, "nplurals=");
      if (np) g_nplurals = std::max(1, atoi(np + 9));
      continue;
    }
    if (!*tr) continue;
    g_tr[key] = tr;
    if (key.size() < ol) {   // an entry with plurals: forms separated by \0
      std::vector<const char *> forms;
      for (const char *p = tr; p < tr + tl; p += strlen(p) + 1) forms.push_back(p);
      g_trp[key] = forms;
    }
  }
  return true;
}

// Plural rules for languages that may appear; English-like by default.
static size_t plural_index(unsigned long n) {
  if (g_nplurals == 1) return 0;
  if (g_nplurals == 3) {   // Polish, Czech, Russian... (the Polish variant)
    unsigned long d = n % 10, h = n % 100;
    if (n == 1) return 0;
    return d >= 2 && d <= 4 && (h < 12 || h > 14) ? 1 : 2;
  }
  return n != 1;
}

// Settings from older versions: the Polish file names, keys and values (until 0.0.6)
// are moved to the current English ones once.
namespace {
using Map = std::vector<std::pair<const char *, const char *>>;

std::string read_text(const fs::path &p) {
  std::ifstream in(p);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void replace_all(std::string &s, const std::string &a, const std::string &b) {
  for (size_t p = 0; (p = s.find(a, p)) != std::string::npos; p += b.size()) s.replace(p, a.size(), b);
}

// "key=value" lines: keys renamed with `keys`, values of selected keys with `values`
// (pairs "key:old" -> "new"); unknown lines are kept, comments dropped.
std::string convert_conf(const std::string &text, const Map &keys, const Map &values) {
  std::istringstream in(text);
  std::string line, out;
  while (std::getline(in, line)) {
    if (!line.empty() && line[0] == '#') continue;   // old (Polish) comments are dropped
    auto eq = line.find('=');
    if (!line.empty() && eq != std::string::npos) {
      std::string k = line.substr(0, eq), v = line.substr(eq + 1);
      for (auto &[o, n] : keys) if (k == o) { k = n; break; }
      for (auto &[o, n] : values) {
        std::string ov = o;
        auto colon = ov.find(':');
        if (ov.substr(0, colon) == k && ov.substr(colon + 1) == v) { v = n; break; }
      }
      line = k + "=" + v;
    }
    out += line + "\n";
  }
  return out;
}

// Rename old -> new (only when new does not exist yet); convert the content on the way.
void move_file(const fs::path &dir, const char *old_name, const char *new_name,
               const std::function<std::string(std::string)> &convert = nullptr) {
  std::error_code ec;
  fs::path o = dir / old_name, n = dir / new_name;
  if (!fs::exists(o, ec) || fs::exists(n, ec)) return;
  if (!convert) { fs::rename(o, n, ec); return; }
  std::ofstream(n) << convert(read_text(o));
  fs::remove(o, ec);
}

const Map APPEARANCE_KEYS = {
  {"schemat", "scheme"}, {"styl_paska", "title_style"}, {"biurko", "desktop_color"}, {"tlo", "background"},
  {"wzor", "pattern"}, {"biurko2", "desktop_color2"}, {"animacje", "animations"}, {"ikony", "icons"},
  {"tapeta", "wallpaper"}, {"tapeta_uklad", "wallpaper_layout"}, {"skala", "scale"},
};
const Map APPEARANCE_VALUES = {
  {"scheme:grafit", "graphite"}, {"title_style:prazki", "stripes"}, {"title_style:kolor", "solid"},
  {"background:wzor", "pattern"}, {"background:kolor", "color"},
  {"gradient:pionowy", "vertical"}, {"gradient:poziomy", "horizontal"}, {"gradient:ukosny", "diagonal"},
  {"gradient:promienisty", "radial"},
  {"wallpaper_layout:wypelnij", "fill"}, {"wallpaper_layout:dopasuj", "fit"},
  {"wallpaper_layout:rozciagnij", "stretch"}, {"wallpaper_layout:srodek", "center"},
  {"wallpaper_layout:kafelki", "tile"},
  {"pattern:gladki", "plain"}, {"pattern:kropki", "dots"}, {"pattern:groszki", "polka_dots"},
  {"pattern:krata", "grid"}, {"pattern:kratka", "fine_grid"}, {"pattern:szachownica", "checkerboard"},
  {"pattern:ukosne", "diagonal"}, {"pattern:paski_poziome", "horizontal_stripes"},
  {"pattern:paski_pionowe", "vertical_stripes"}, {"pattern:cegly", "bricks"}, {"pattern:romby", "diamonds"},
  {"pattern:krzyzyki", "crosses"}, {"pattern:zygzak", "zigzag"}, {"pattern:jodelka", "herringbone"},
  {"pattern:tkanina", "weave"}, {"pattern:fale", "waves"}, {"pattern:luski", "scales"},
  {"pattern:gwiazdki", "stars"}, {"pattern:piasek", "sand"},
};
const Map SCHEME_KEYS = {
  {"nazwa", "name"}, {"zrodlo", "source"}, {"styl_paska", "title_style"}, {"okno", "window"}, {"tekst", "text"},
  {"tresc", "content"}, {"tekst_tresci", "content_text"}, {"zaznaczenie", "selection"},
  {"tekst_zaznaczenia", "selection_text"}, {"zaznaczenie_ikon", "icon_selection"}, {"obramowanie", "border"},
  {"pasek", "title"}, {"pasek2", "title2"}, {"tekst_paska", "title_text"}, {"pasek_nieaktywny", "title_inactive"},
  {"pasek_nieaktywny2", "title_inactive2"}, {"tekst_paska_nieaktywnego", "title_inactive_text"},
  {"akcent", "accent"}, {"biurko", "desktop"}, {"jasny", "light"}, {"cien_sredni", "mid"}, {"cien", "shadow"},
};
const Map SCHEME_VALUES = {{"title_style:prazki", "stripes"}, {"title_style:kolor", "solid"}};
// bundled wallpapers that got English file names
const Map WALLPAPER_FILES = {
  {"/tapety/Góry o świcie.jpg", "/wallpapers/Mountains at Dawn.jpg"}, {"/tapety/Miedź.png", "/wallpapers/Copper.png"},
  {"/tapety/Platyna.png", "/wallpapers/Platinum.png"}, {"/tapety/", "/wallpapers/"},
};
}  // namespace

void migrate_old_config() {
  static bool done = false;
  if (done) return;
  done = true;
  const char *h = getenv("HOME");
  std::string home = h ? h : "/";
  auto base = [&](const char *var, const char *def) {
    const char *x = getenv(var);
    return x && *x ? std::string(x) : home + def;
  };
  std::error_code ec;
  fs::path config = base("XDG_CONFIG_HOME", "/.config"), data = base("XDG_DATA_HOME", "/.local/share"),
           cache = base("XDG_CACHE_HOME", "/.cache");
  fs::path cfg = config / "nirvana";
  // Polish file names and keys -> English (0.0.7)
  move_file(cfg, "wyglad.conf", "appearance.conf", [](std::string t) {
    for (auto &[o, n] : WALLPAPER_FILES) replace_all(t, std::string("/nirvana") + o, std::string("/nirvana") + n);
    return convert_conf(t, APPEARANCE_KEYS, APPEARANCE_VALUES);
  });
  move_file(cfg, "zasilanie.conf", "power.conf", [](std::string t) {
    return convert_conf(t, {{"wygaszanie", "blank_minutes"}, {"blokada", "lock_on_blank"},
                            {"usypianie_bateria", "sleep_battery_minutes"}, {"usypianie_zasilacz", "sleep_ac_minutes"},
                            {"krytyczna", "critical_action"}}, {});
  });
  {   // terminal.conf keeps its name, only the keys and profile names change
    fs::path f = cfg / "terminal.conf";
    std::string t = "\n" + read_text(f);
    if (t.find("\nprofil=") != std::string::npos || t.find("\nczcionka=") != std::string::npos)
      std::ofstream(f) << convert_conf(t.substr(1),
                                       {{"profil", "profile"}, {"czcionka", "font"}, {"rozmiar", "size"},
                                        {"kolumny", "columns"}, {"wiersze", "rows"}, {"przewijanie", "scrollback"}},
                                       {{"profile:klasyczny", "classic"}, {"profile:ciemny", "dark"},
                                        {"profile:zielony", "green"}, {"profile:bursztyn", "amber"},
                                        {"profile:schemat", "scheme"}});
  }
  {   // rambler.conf keeps its name, only the keys change
    fs::path r = cfg / "rambler.conf";
    std::string t = read_text(r);
    if (t.find("ukryte=") != std::string::npos || t.find("dok_zwiniety=") != std::string::npos)
      std::ofstream(r) << convert_conf(t, {{"ukryte", "show_hidden"}, {"dok_zwiniety", "dock_collapsed"}}, {});
  }
  move_file(cfg, "jezyk", "language");
  move_file(cfg, "dok", "dock");
  move_file(cfg, "denise.okna", "windows");
  move_file(cfg, "ikony.pozycje", "icon-positions");
  move_file(cfg, "parametry", "tooltypes");
  move_file(cfg, "aplikacje.klasy", "app-classes", [](std::string t) {
    replace_all(t, "/nirvana/ikony/", "/nirvana/icons/");
    return t;
  });
  // data: extracted applications, own schemes and wallpapers
  fs::path dat = data / "nirvana";
  if (fs::is_directory(dat / "aplikacje", ec) && !fs::exists(dat / "apps", ec)) fs::rename(dat / "aplikacje", dat / "apps", ec);
  if (fs::is_directory(dat / "tapety", ec) && !fs::exists(dat / "wallpapers", ec)) fs::rename(dat / "tapety", dat / "wallpapers", ec);
  if (fs::is_directory(dat / "schematy", ec) && !fs::exists(dat / "schemes", ec)) {
    fs::rename(dat / "schematy", dat / "schemes", ec);
    for (auto it = fs::recursive_directory_iterator(dat / "schemes", ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) break;
      if (it->path().extension() != ".conf") continue;
      std::string t = read_text(it->path());
      std::ofstream(it->path()) << convert_conf(t, SCHEME_KEYS, SCHEME_VALUES);
    }
  }
  // icon cache: the old one (Polish markers and keys) is simply rebuilt
  if (fs::is_directory(cache / "nirvana" / "ikony", ec)) fs::remove_all(cache / "nirvana" / "ikony", ec);
}

static std::string config_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  std::string dir = x && *x ? x : std::string(h ? h : "/") + "/.config";
  return dir + "/nirvana/language";
}

std::string language_setting() {
  std::ifstream in(config_file());
  std::string s;
  std::getline(in, s);
  while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
  return s;
}

bool set_language_setting(const std::string &lang) {
  std::error_code ec;
  fs::create_directories(fs::path(config_file()).parent_path(), ec);
  std::ofstream out(config_file());
  out << lang << "\n";
  return (bool)out;
}

// Folders with .mo files: next to the program in build/ or in the installation prefix.
static std::vector<std::string> locale_dirs() {
  std::vector<std::string> d;
  if (const char *e = getenv("NIRVANA_LOCALEDIR")) d.push_back(e);
  std::error_code ec;
  fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    d.push_back((exe.parent_path() / "locale").string());                   // build/locale
    d.push_back((exe.parent_path() / "../share/nirvana/locale").string());   // prefiks/share/nirvana/locale
    d.push_back((exe.parent_path() / "../share/locale").string());          // older installations
  }
  d.push_back("/usr/share/nirvana/locale");
  d.push_back("/usr/local/share/locale");
  d.push_back("/usr/share/locale");
  return d;
}

static std::string find_catalog(const std::string &lang) {
  std::error_code ec;
  for (auto &d : locale_dirs())
    if (fs::exists(fs::path(d) / lang / "LC_MESSAGES" / (std::string(DOMAIN) + ".mo"), ec))
      return fs::weakly_canonical(d, ec).string();
  return "";
}

std::vector<std::pair<std::string, std::string>> available_languages() {
  std::vector<std::pair<std::string, std::string>> v = {{"en", "English"}};
  if (!find_catalog("pl").empty()) v.push_back({"pl", "Polski"});
  return v;
}

// The system language: LANGUAGE, LC_ALL, LC_MESSAGES, LANG - the first two letters.
static std::string system_language() {
  for (const char *var : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
    const char *v = getenv(var);
    if (!v || !*v) continue;
    std::string s = v;
    s = s.substr(0, s.find_first_of(":_.@"));
    if (s == "C" || s == "POSIX") return "";
    if (!s.empty()) return s;
  }
  return "";
}

void init_i18n() {
  static bool done = false;
  if (done) return;
  done = true;
  migrate_old_config();
  std::string want = language_setting();
  if (want.empty() || want == "auto") want = system_language();   // pl_PL.UTF-8 -> pl
  // English is in the code; another language only when its .mo catalog exists
  g_lang = "en";
  if (want.empty() || want == "en") return;
  std::string dir = find_catalog(want);
  if (dir.empty()) return;
  g_lang = want;

  if (!load_mo(dir + "/" + want + "/LC_MESSAGES/" + DOMAIN + ".mo")) { g_lang = "en"; return; }
  g_active = true;
}

const char *tr(const char *msgid) {
  if (!g_active || !msgid || !*msgid) return msgid;
  auto it = g_tr.find(msgid);
  return it == g_tr.end() ? msgid : it->second;
}

std::string trn(long n, const char *one, const char *other) {
  unsigned long a = (unsigned long)(n < 0 ? -n : n);
  if (g_active) {
    // in the .po file: msgid = the singular form, msgstr[0..] = the forms by Plural-Forms
    auto it = g_trp.find(one);
    if (it != g_trp.end() && !it->second.empty())
      return it->second[std::min(plural_index(a), it->second.size() - 1)];
  }
  return a == 1 ? one : other;
}

std::string fmt(const std::string &p, const std::vector<std::string> &args) {
  std::string out;
  for (size_t i = 0; i < p.size(); i++) {
    if (p[i] == '%' && i + 1 < p.size() && p[i + 1] >= '1' && p[i + 1] <= '9') {
      size_t k = p[i + 1] - '1';
      if (k < args.size()) out += args[k];
      i++;
    } else {
      out += p[i];
    }
  }
  return out;
}

std::string tr_path(const char *path) {
  std::string out, seg;
  if (!path) return out;
  for (const char *c = path;; c++) {
    if (*c == '\\' && c[1]) { seg += *c; seg += *++c; continue; }   // "\\/" in a name
    if (*c == '/' || !*c) {
      out += tr(seg.c_str());
      if (!*c) break;
      out += '/';
      seg.clear();
    } else {
      seg += *c;
    }
  }
  return out;
}

std::string date_time(long t) {
  static const char *mon[] = {N_("Jan"), N_("Feb"), N_("Mar"), N_("Apr"), N_("May"), N_("Jun"),
                              N_("Jul"), N_("Aug"), N_("Sep"), N_("Oct"), N_("Nov"), N_("Dec")};
  time_t tt = (time_t)t;
  struct tm tm;
  localtime_r(&tt, &tm);
  char hm[16];
  snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
  // TRANSLATORS: date: %1 day, %2 month (abbreviated), %3 year, %4 time - the order may change
  return fmt(tr("%2 %1, %3, %4"), std::to_string(tm.tm_mday), tr(mon[tm.tm_mon]),
             std::to_string(tm.tm_year + 1900), hm);
}

std::string decimal(std::string s) {
  if (g_lang == "pl") for (auto &c : s) if (c == '.') c = ',';
  return s;
}

std::string language() { return g_lang; }

}  // namespace plat
