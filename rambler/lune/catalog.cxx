/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Catalogs (catalog.h).
*/
#include "catalog.h"
#include "lune.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace lune {

const Lang LANGS[] = {{"pl", "polski"}, {"de", "deutsch"}, {"fr", "français"}, {"it", "italiano"},
                      {"es", "español"}, {"cs", "čeština"}, {"sv", "svenska"}, {"nl", "nederlands"},
                      {"pt", "português"}, {"da", "dansk"}, {"no", "norsk"}, {"fi", "suomi"},
                      {"hu", "magyar"}, {"ru", "русский"}, {"el", "ελληνικά"}, {"tr", "türkçe"}, {nullptr, nullptr}};

namespace {

std::string g_lang;
std::unordered_map<std::string, std::string> g_cat;
bool g_loaded = false;

std::string unescape(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            o += c == 'n' ? '\n' : c == 't' ? '\t' : c;
        } else o += s[i];
    }
    return o;
}

// the preferred languages of the Locale preferences ("pl:en"; the session's in use first,
// then the saved ones), else of the environment
std::string preferred_languages() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    for (const std::string &f : {env_dir() + "/locale.prefs", base + "/rambler/locale.prefs"}) {
        std::ifstream in(f);
        if (!in) continue;
        std::string line;
        while (std::getline(in, line))
            if (line.rfind("languages=", 0) == 0 && line.size() > 10) return line.substr(10);
        break;   // (the file in use says nothing: the environment's)
    }
    const char *env = nullptr;
    for (const char *v : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"})
        if ((env = getenv(v)) && *env) break;
    return env ? env : "";
}

void load() {
    g_loaded = true;
    // the first of the preferred languages that has a catalog; English ends the search
    std::string langs = preferred_languages();
    std::vector<std::string> names;
    for (size_t a = 0; a < langs.size();) {
        size_t b = langs.find(':', a);
        if (b == std::string::npos) b = langs.size();
        std::string item = langs.substr(a, b - a);
        a = b + 1;
        std::string code = item.substr(0, strcspn(item.c_str(), "_.@"));
        if (code == "en" || code == "C" || code == "POSIX") break;
        for (const Lang *l = LANGS; l->code; l++) if (code == l->code) names.push_back(l->name);
    }
    if (names.empty()) return;
    // where the catalogs are: $RAMBLER_CATALOGS, next to the program (build tree), the
    // installation's share/rambler/catalogs
    std::vector<std::string> dirs;
    if (const char *d = getenv("RAMBLER_CATALOGS")) dirs.push_back(d);
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        std::string dir(exe);
        dir = dir.substr(0, dir.rfind('/'));
        dirs.push_back(dir + "/catalogs");
        dirs.push_back(dir + "/../share/rambler/catalogs");
    }
    dirs.push_back("/usr/share/rambler/catalogs");
    dirs.push_back("/usr/local/share/rambler/catalogs");
    for (auto &name : names)
    for (auto &d : dirs) {
        std::ifstream in(d + "/" + name + ".catalog");
        if (!in) continue;
        g_lang = name;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto tab = line.find('\t');
            if (tab == std::string::npos) continue;
            std::string t = line.substr(tab + 1);
            while (!t.empty() && t[0] == '\t') t.erase(0, 1);
            g_cat[unescape(line.substr(0, tab))] = unescape(t);
        }
        return;
    }
    g_lang.clear();   // no catalog: English
}

}  // namespace

const std::string &tr(const std::string &english) {
    if (!g_loaded) load();
    auto it = g_cat.find(english);
    if (it != g_cat.end()) return it->second;
    // not in the catalog: the English text, kept (the caller may hold on to it - menus do)
    return g_cat.emplace(english, english).first->second;
}

std::string tr_text(const std::string &text) {
    size_t i = 0;
    while (i + 1 < text.size() && text[i] == '\033') i += 2;   // the control sequences stay
    if (i >= text.size()) return text;
    std::string rest = text.substr(i);
    const std::string &t = tr(rest);
    return text.substr(0, i) + t;
}

std::string trs(const std::string &english, const std::string &a, const std::string &b) {
    std::string t = tr(english), out;
    int n = 0;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '%' && i + 1 < t.size() && t[i + 1] == 's') { out += n++ == 0 ? a : b; i++; }
        else out += t[i];
    }
    return out;
}

const char *language_name() {
    if (!g_loaded) load();
    return g_lang.c_str();
}

}  // namespace lune
