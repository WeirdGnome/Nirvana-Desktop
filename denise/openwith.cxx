// Nirvana Desktop - Rambler: "Open With" - the programs that can open a file
//
// The programs come from the .desktop files of the menus (XDG_DATA_HOME and XDG_DATA_DIRS,
// "applications"), by the MimeType they declare for the file's type (image/png, also image/*;
// a text/... file also matches the programs for text/plain). The command is made from Exec
// with the field codes of the Desktop Entry specification (%f %F %u %U).
#include "openwith.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <strings.h>

namespace fs = std::filesystem;

namespace openwith {

namespace {

std::vector<std::string> app_dirs() {
  std::vector<std::string> v;
  const char *h = getenv("HOME");
  const char *x = getenv("XDG_DATA_HOME");
  v.push_back((x && *x ? std::string(x) : std::string(h ? h : "") + "/.local/share") + "/applications");
  const char *d = getenv("XDG_DATA_DIRS");
  std::string s = d && *d ? d : "/usr/local/share:/usr/share";
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, ':'))
    if (!part.empty()) v.push_back(part + "/applications");
  return v;
}

// "pl_PL.UTF-8" -> {"pl_PL", "pl"}: the localized Name[...] keys, best first
std::vector<std::string> lang_keys() {
  std::vector<std::string> out;
  const char *l = getenv("LANGUAGE");
  if (!l || !*l) l = getenv("LC_ALL");
  if (!l || !*l) l = getenv("LC_MESSAGES");
  if (!l || !*l) l = getenv("LANG");
  std::string s = l ? l : "";
  s = s.substr(0, s.find(':'));
  s = s.substr(0, s.find('.'));
  s = s.substr(0, s.find('@'));
  if (!s.empty() && s != "C" && s != "POSIX") {
    out.push_back(s);
    if (s.find('_') != std::string::npos) out.push_back(s.substr(0, s.find('_')));
  }
  return out;
}

struct Desk {
  App app;
  std::vector<std::string> mimes;
};

std::vector<Desk> load_all() {
  std::vector<Desk> all;
  std::set<std::string> seen;   // desktop file ids: the first directory wins (the user's own)
  auto langs = lang_keys();
  for (auto &dir : app_dirs()) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) break;
      if (it->path().extension() != ".desktop") continue;
      std::string id = fs::relative(it->path(), dir, ec).string();
      std::replace(id.begin(), id.end(), '/', '-');
      if (!seen.insert(id).second) continue;
      std::ifstream in(it->path());
      std::string line, name, exec, icon, type, mime;
      std::map<std::string, std::string> loc;
      bool section = false, hidden = false, terminal = false;
      while (std::getline(in, line)) {
        if (!line.empty() && line[0] == '[') { section = line == "[Desktop Entry]"; continue; }
        if (!section) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        while (!v.empty() && v.front() == ' ') v.erase(0, 1);
        if (k == "Name") name = v;
        else if (k.rfind("Name[", 0) == 0 && k.back() == ']') loc[k.substr(5, k.size() - 6)] = v;
        else if (k == "Exec") exec = v;
        else if (k == "Icon") icon = v;
        else if (k == "Type") type = v;
        else if (k == "MimeType") mime = v;
        else if ((k == "NoDisplay" || k == "Hidden") && v == "true") hidden = true;
        else if (k == "Terminal" && v == "true") terminal = true;
      }
      if (hidden || type != "Application" || exec.empty() || mime.empty()) continue;
      for (auto &l : langs)
        if (loc.count(l)) { name = loc[l]; break; }
      Desk d;
      d.app = {name.empty() ? id : name, exec, icon, id, terminal};
      std::stringstream ms(mime);
      std::string m;
      while (std::getline(ms, m, ';'))
        if (!m.empty()) d.mimes.push_back(m);
      all.push_back(d);
    }
  }
  return all;
}

std::vector<Desk> &apps() {
  static std::vector<Desk> cache;
  static bool loaded = false;
  if (!loaded) { cache = load_all(); loaded = true; }
  return cache;
}

bool matches(const std::string &want, const std::string &have) {
  if (strcasecmp(want.c_str(), have.c_str()) == 0) return true;
  if (have.size() > 2 && have.compare(have.size() - 2, 2, "/*") == 0)   // image/*
    return strncasecmp(want.c_str(), have.c_str(), have.size() - 1) == 0;
  return false;
}

}  // namespace

void reload() {
  // the next for_mime() reads the .desktop files again (a program was installed)
  apps() = load_all();
}

std::vector<App> for_mime(const std::string &mime) {
  std::vector<App> out, plain;
  if (mime.empty()) return out;
  bool text = mime.rfind("text/", 0) == 0 || mime == "application/x-shellscript" || mime == "application/json" ||
              mime == "application/xml";
  for (auto &d : apps()) {
    bool exact = false, as_text = false;
    for (auto &m : d.mimes) {
      if (matches(mime, m)) exact = true;
      else if (text && strcasecmp(m.c_str(), "text/plain") == 0) as_text = true;
    }
    if (exact) out.push_back(d.app);
    else if (as_text) plain.push_back(d.app);
  }
  auto by_name = [](const App &a, const App &b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; };
  std::sort(out.begin(), out.end(), by_name);
  std::sort(plain.begin(), plain.end(), by_name);
  out.insert(out.end(), plain.begin(), plain.end());
  // one entry per name (the same program in two .desktop files)
  std::set<std::string> names;
  out.erase(std::remove_if(out.begin(), out.end(), [&](const App &a) { return !names.insert(a.name).second; }),
            out.end());
  return out;
}

// The Exec line split into words (quotes and backslashes of the specification)
static std::vector<std::string> split_exec(const std::string &s) {
  std::vector<std::string> out;
  std::string cur;
  bool q = false, any = false;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (q) {
      if (c == '\\' && i + 1 < s.size()) cur += s[++i];
      else if (c == '"') q = false;
      else cur += c;
    } else if (c == '"') { q = true; any = true; }
    else if (c == ' ' || c == '\t') {
      if (any || !cur.empty()) out.push_back(cur);
      cur.clear();
      any = false;
    } else cur += c;
  }
  if (any || !cur.empty()) out.push_back(cur);
  return out;
}

std::vector<std::vector<std::string>> commands(const App &a, const std::vector<std::string> &files) {
  std::vector<std::vector<std::string>> runs;
  auto words = split_exec(a.exec);
  bool single = false, multi = false;
  for (auto &w : words) {
    if (w.find("%f") != std::string::npos || w.find("%u") != std::string::npos) single = true;
    if (w == "%F" || w == "%U") multi = true;
  }
  auto build = [&](const std::vector<std::string> &fs) {
    std::vector<std::string> argv;
    for (auto &w : words) {
      if (w == "%F" || w == "%U") { for (auto &f : fs) argv.push_back(f); continue; }
      if (w == "%i" || w == "%c" || w == "%k" || w == "%d" || w == "%D" || w == "%n" || w == "%N" ||
          w == "%v" || w == "%m")
        continue;
      std::string o;
      for (size_t i = 0; i < w.size(); i++) {
        if (w[i] == '%' && i + 1 < w.size()) {
          char c = w[++i];
          if (c == '%') o += '%';
          else if ((c == 'f' || c == 'u') && !fs.empty()) o += fs[0];
          else if (c == 'c') o += a.name;
          continue;   // other codes: nothing
        }
        o += w[i];
      }
      argv.push_back(o);
    }
    if (!single && !multi)
      for (auto &f : fs) argv.push_back(f);   // no field code: the files at the end
    return argv;
  };
  if (single && !multi && files.size() > 1)
    for (auto &f : files) runs.push_back(build({f}));   // %f: the program once for every file
  else
    runs.push_back(build(files));
  return runs;
}

}  // namespace openwith
