// Nirvana Desktop - tłumaczenia (patrz i18n.h)
#include "i18n.h"

#include <algorithm>
#include <cerrno>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <unordered_map>
#include <unistd.h>

namespace fs = std::filesystem;

namespace plat {

static const char *DOMAIN = "nirvana";
static bool g_active = false;          // czy tłumaczymy (false = teksty polskie z kodu)
static std::string g_lang = "en";

// Katalog .mo czytamy sami zamiast przez gettext(): glibc nie tłumaczy przy
// LC_MESSAGES=C/C.UTF-8, a minimalny system często nie ma wygenerowanych
// locale (en_US.UTF-8). Format .mo: nagłówek, dwie tablice (długość, offset).
static std::string g_mo;                                         // cały plik
static std::unordered_map<std::string, const char *> g_tr;      // msgid -> tłumaczenie
static std::unordered_map<std::string, std::vector<const char *>> g_trp;   // msgid -> formy mnogie
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
    if (key.empty()) {   // nagłówek: Plural-Forms: nplurals=N
      const char *np = strstr(tr, "nplurals=");
      if (np) g_nplurals = std::max(1, atoi(np + 9));
      continue;
    }
    if (!*tr) continue;
    g_tr[key] = tr;
    if (key.size() < ol) {   // wpis z liczbą mnogą: formy oddzielone \0
      std::vector<const char *> forms;
      for (const char *p = tr; p < tr + tl; p += strlen(p) + 1) forms.push_back(p);
      g_trp[key] = forms;
    }
  }
  return true;
}

// Reguły liczby mnogiej dla języków, które mogą się pojawić; domyślnie jak angielski.
static size_t plural_index(unsigned long n) {
  if (g_nplurals == 1) return 0;
  if (g_nplurals == 3) {   // polski, czeski, rosyjski... (wariant polski)
    unsigned long d = n % 10, h = n % 100;
    if (n == 1) return 0;
    return d >= 2 && d <= 4 && (h < 12 || h > 14) ? 1 : 2;
  }
  return n != 1;
}

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
  for (const std::string &b : {base("XDG_CONFIG_HOME", "/.config"), base("XDG_DATA_HOME", "/.local/share"),
                               base("XDG_CACHE_HOME", "/.cache")}) {
    fs::path oldp = fs::path(b) / "lisaos", newp = fs::path(b) / "nirvana";
    if (fs::is_directory(oldp, ec) && !fs::exists(newp, ec)) fs::rename(oldp, newp, ec);
  }
  // tapeta z pakietu: /usr/share/lisaos/tapety/Lisa Desktop.jpg -> nirvana/.../Nirvana Desktop.jpg
  fs::path conf = fs::path(base("XDG_CONFIG_HOME", "/.config")) / "nirvana" / "wyglad.conf";
  std::ifstream in(conf);
  if (!in) return;
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()), out;
  in.close();
  out = text;
  auto repl = [&](const std::string &a, const std::string &b) {
    for (size_t p = 0; (p = out.find(a, p)) != std::string::npos; p += b.size()) out.replace(p, a.size(), b);
  };
  repl("/share/lisaos/", "/share/nirvana/");
  repl("/lisaos/tapety/", "/nirvana/tapety/");
  repl("Lisa Desktop.jpg", "Nirvana Desktop.jpg");
  if (out != text) std::ofstream(conf) << out;
}

static std::string config_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  std::string dir = x && *x ? x : std::string(h ? h : "/") + "/.config";
  return dir + "/nirvana/jezyk";
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

// Katalogi z plikami .mo: obok programu w build/ albo w prefiksie instalacji.
static std::vector<std::string> locale_dirs() {
  std::vector<std::string> d;
  if (const char *e = getenv("NIRVANA_LOCALEDIR")) d.push_back(e);
  std::error_code ec;
  fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    d.push_back((exe.parent_path() / "locale").string());                   // build/locale
    d.push_back((exe.parent_path() / "../share/nirvana/locale").string());   // prefiks/share/nirvana/locale
    d.push_back((exe.parent_path() / "../share/locale").string());          // starsze instalacje
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

// Język systemu: LANGUAGE, LC_ALL, LC_MESSAGES, LANG - pierwsze dwie litery.
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
  // angielski jest w kodzie; inny język tylko wtedy, gdy jest jego katalog .mo
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
    // w pliku .po: msgid = forma pojedyncza, msgstr[0..] = formy wg Plural-Forms
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
    if (*c == '\\' && c[1]) { seg += *c; seg += *++c; continue; }   // "\\/" w nazwie
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
  // TRANSLATORS: data: %1 dzień, %2 miesiąc (skrót), %3 rok, %4 godzina - można zmienić szyk
  return fmt(tr("%2 %1, %3, %4"), std::to_string(tm.tm_mday), tr(mon[tm.tm_mon]),
             std::to_string(tm.tm_year + 1900), hm);
}

std::string decimal(std::string s) {
  if (g_lang == "pl") for (auto &c : s) if (c == '.') c = ',';
  return s;
}

std::string language() { return g_lang; }

}  // namespace plat
