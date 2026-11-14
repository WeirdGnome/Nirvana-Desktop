/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Catalogs (catalog.h).
*/
#include "catalog.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace zune {

namespace {

struct Lang { const char *code, *name; };
const Lang LANGS[] = {{"pl", "polski"}, {"de", "deutsch"}, {"fr", "français"}, {"it", "italiano"},
                      {"es", "español"}, {"cs", "čeština"}, {"sv", "svenska"}, {"nl", "nederlands"}};

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

void load() {
    g_loaded = true;
    const char *env = nullptr;
    for (const char *v : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"})
        if ((env = getenv(v)) && *env) break;
    if (!env || !*env) return;
    std::string code(env, strcspn(env, "_.@:"));
    for (auto &l : LANGS) if (code == l.code) g_lang = l.name;
    if (g_lang.empty()) return;
    // where the catalogs are: $WANDERER_CATALOGS, next to the program (build tree), the
    // installation's share/wanderer/catalogs
    std::vector<std::string> dirs;
    if (const char *d = getenv("WANDERER_CATALOGS")) dirs.push_back(d);
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        std::string dir(exe);
        dir = dir.substr(0, dir.rfind('/'));
        dirs.push_back(dir + "/catalogs");
        dirs.push_back(dir + "/../share/wanderer/catalogs");
    }
    dirs.push_back("/usr/share/wanderer/catalogs");
    dirs.push_back("/usr/local/share/wanderer/catalogs");
    for (auto &d : dirs) {
        std::ifstream in(d + "/" + g_lang + ".catalog");
        if (!in) continue;
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
    return it == g_cat.end() ? english : it->second;
}

std::string tr_text(const std::string &text) {
    size_t i = 0;
    while (i + 1 < text.size() && text[i] == '\033') i += 2;   // the control sequences stay
    if (i >= text.size()) return text;
    std::string rest = text.substr(i);
    const std::string &t = tr(rest);
    return &t == &rest ? text : text.substr(0, i) + t;
}

const char *language_name() {
    if (!g_loaded) load();
    return g_lang.c_str();
}

}  // namespace zune
