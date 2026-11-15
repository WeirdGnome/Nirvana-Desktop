/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Locale preferences (prefs/locale/locale.c, languagelist.c, country.c, timezone.c):
    see localeprefs.h.
*/
#include "localeprefs.h"
#include "tools.h"
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <locale.h>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

namespace {
std::string saved_file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/rambler/locale.prefs";
}

// the language's own name ("polski"); English is "english"
std::string language_name_of(const std::string &code) {
    if (code == "en") return "english";
    for (const Lang *l = LANGS; l->code; l++) if (code == l->code) return l->name;
    return code;
}

// the locales installed (locale -a), the UTF-8 ones of countries ("pl_PL.utf8")
std::vector<std::string> installed_locales() {
    std::vector<std::string> v;
    if (FILE *p = popen("locale -a 2>/dev/null", "r")) {
        char buf[256];
        while (fgets(buf, sizeof buf, p)) {
            std::string l(buf);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            std::string low = l;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            if (l.find('_') != std::string::npos && (low.find("utf8") != std::string::npos || low.find("utf-8") != std::string::npos))
                v.push_back(l);
        }
        pclose(p);
    }
    std::sort(v.begin(), v.end());
    return v;
}

// the timezones (zone1970.tab: country codes, coordinates, the zone)
std::vector<std::string> timezones() {
    std::vector<std::string> v;
    std::ifstream in("/usr/share/zoneinfo/zone1970.tab");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string cc, coord, zone;
        ls >> cc >> coord >> zone;
        if (!zone.empty()) v.push_back(zone);
    }
    std::sort(v.begin(), v.end());
    return v;
}

// a date, a time and a number as the locale writes them (the country page's example)
std::string example(const std::string &loc) {
    std::string out;
    locale_t l = newlocale(LC_TIME_MASK | LC_NUMERIC_MASK | LC_MONETARY_MASK, loc.c_str(), (locale_t)0);
    if (!l) return tr("This locale is not installed.");
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[128];
    strftime_l(buf, sizeof buf, "%x  %X", &tm, l);
    out = tr("Date and time") + ": " + buf;
    locale_t old = uselocale(l);
    lconv *lc = localeconv();
    char num[64];
    snprintf(num, sizeof num, "1%s234%s56", lc->thousands_sep && *lc->thousands_sep ? lc->thousands_sep : "",
             lc->decimal_point ? lc->decimal_point : ".");
    out += "\n" + tr("Number") + ": " + num;
    if (lc->currency_symbol && *lc->currency_symbol) out += "\n" + tr("Currency") + ": " + lc->currency_symbol;
    uselocale(old);
    freelocale(l);
    return out;
}

// the time now in a timezone
std::string time_in(const std::string &zone) {
    const char *old = getenv("TZ");
    std::string keep = old ? old : "";
    if (zone.empty()) unsetenv("TZ"); else setenv("TZ", zone.c_str(), 1);
    tzset();
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[64];
    strftime(buf, sizeof buf, "%H:%M  (%Z, UTC%z)", &tm);
    if (old) setenv("TZ", keep.c_str(), 1); else unsetenv("TZ");
    tzset();
    return buf;
}
}  // namespace

std::string LocalePrefs::in_use_file() { return env_dir() + "/locale.prefs"; }

LocalePrefs LocalePrefs::load() {
    LocalePrefs p;
    std::ifstream in(in_use_file());
    if (!in) in.open(saved_file());
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "languages") {
            for (size_t a = 0; a < v.size();) {
                size_t b = v.find(':', a);
                if (b == std::string::npos) b = v.size();
                if (b > a) p.languages.push_back(v.substr(a, b - a));
                a = b + 1;
            }
        } else if (k == "country") p.country = v;
        else if (k == "timezone") p.timezone = v;
    }
    return p;
}

void LocalePrefs::save(bool archive) const {
    std::string langs;
    for (auto &l : languages) langs += (langs.empty() ? "" : ":") + l;
    std::string text = "languages=" + langs + "\ncountry=" + country + "\ntimezone=" + timezone + "\n";
    std::vector<std::string> files = {in_use_file()};
    if (archive) files.push_back(saved_file());
    for (auto &f : files) {
        std::error_code ec;
        fs::create_directories(fs::path(f).parent_path(), ec);
        std::ofstream(f) << text;
    }
}

void LocalePrefs::apply() const {
    // the programs Rambler starts from now on get them (as the session gives them at its start)
    if (!languages.empty()) {
        std::string v;
        for (auto &l : languages) v += (v.empty() ? "" : ":") + l;
        setenv("LANGUAGE", v.c_str(), 1);
    }
    if (!country.empty())
        for (const char *c : {"LC_TIME", "LC_NUMERIC", "LC_MONETARY", "LC_PAPER", "LC_MEASUREMENT"})
            setenv(c, country.c_str(), 1);
    if (!timezone.empty()) setenv("TZ", timezone.c_str(), 1);
    tzset();
}

static Window *g_locale_editor = nullptr;

void edit_locale_prefs(bool standalone) {
    if (g_locale_editor) { g_locale_editor->show(); return; }
    LocalePrefs saved = LocalePrefs::load();
    auto pref = std::make_shared<std::vector<std::string>>(saved.languages);
    auto avail = std::make_shared<std::vector<std::string>>();
    avail->push_back("en");
    for (const Lang *l = LANGS; l->code; l++) avail->push_back(l->code);
    std::sort(avail->begin(), avail->end(), [](const std::string &a, const std::string &b) {
        return language_name_of(a) < language_name_of(b);
    });
    auto locs = std::make_shared<std::vector<std::string>>(installed_locales());
    auto zones = std::make_shared<std::vector<std::string>>(timezones());

    auto *root = VGroup();
    auto *reg = new Register({"Language", "Country", "Timezone"});
    // -- Language (languagelist.c): the languages there are, and the preferred ones in order
    auto *lp = HGroup();
    auto *ag = new Group(Group::Vertical, Frame_Group);
    ag->frame_title("Available Languages");
    auto *alist = new List();
    alist->frame(Frame_InputList);
    for (auto &c : *avail) alist->add({language_name_of(c)});
    alist->done();
    ag->end();
    auto *pg = VGroup();
    auto *pf = new Group(Group::Vertical, Frame_Group);
    pf->frame_title("Preferred Languages");
    auto *plist = new List();
    plist->frame(Frame_InputList);
    pf->end();
    auto *clear = new Button("Clear List");
    pg->end();
    lp->end();
    // -- Country (country.c): the formats of dates, numbers and money
    auto *cp = VGroup();
    auto *cf = new Group(Group::Vertical, Frame_Group);
    cf->frame_title("Country");
    auto *clist = new List();
    clist->frame(Frame_InputList);
    clist->add({tr("System default")});
    for (auto &l : *locs) clist->add({l});
    clist->done();
    cf->end();
    auto *ce = new Group(Group::Vertical, Frame_Group);
    ce->frame_title("Example");
    auto *cex = new Text("\033l\n\n");
    ce->end();
    cp->end();
    // -- Timezone (timezone.c)
    auto *tp = VGroup();
    auto *tf = new Group(Group::Vertical, Frame_Group);
    tf->frame_title("Timezone");
    auto *tlist = new List();
    tlist->frame(Frame_InputList);
    tlist->add({tr("System default")});
    for (auto &z : *zones) tlist->add({z});
    tlist->done();
    tf->end();
    auto *tex = new Text("\033c");
    tp->end();
    reg->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();

    // the preferred list: a click on an available language adds it, on a preferred one
    // takes it away (as the original's two lists)
    auto fill_pref = [=] {
        plist->clear();
        for (auto &c : *pref) plist->add({language_name_of(c)});
        plist->done();
    };
    fill_pref();
    alist->on_active = [=](int i) {
        if (i < 0 || i >= (int)avail->size()) return;
        auto &c = (*avail)[i];
        if (std::find(pref->begin(), pref->end(), c) == pref->end() && pref->size() < 10) pref->push_back(c);
        fill_pref();
    };
    plist->on_active = [=](int i) {
        if (i < 0 || i >= (int)pref->size()) return;
        pref->erase(pref->begin() + i);
        fill_pref();
    };
    clear->on_press = [=] { pref->clear(); fill_pref(); };
    clist->on_active = [=](int i) {
        cex->contents("\033l" + (i <= 0 ? tr("The system's formats.") : example((*locs)[i - 1])));
    };
    tlist->on_active = [=](int i) {
        tex->contents("\033c" + tr("Time") + ": " + time_in(i <= 0 ? "" : (*zones)[i - 1]));
    };
    int ci = 0, ti = 0;
    for (size_t i = 0; i < locs->size(); i++) if ((*locs)[i] == saved.country) ci = (int)i + 1;
    for (size_t i = 0; i < zones->size(); i++) if ((*zones)[i] == saved.timezone) ti = (int)i + 1;
    clist->active(ci);
    clist->on_active(ci);
    tlist->active(ti);
    tlist->on_active(ti);

    auto collect = [=] {
        LocalePrefs p;
        p.languages = *pref;
        int c = clist->active(), t = tlist->active();
        p.country = c > 0 ? (*locs)[c - 1] : "";
        p.timezone = t > 0 ? (*zones)[t - 1] : "";
        return p;
    };
    auto *w = new Window("Locale Preferences", root);
    w->id("LOCALEPREFS");
    g_locale_editor = w;
    auto close = [w, standalone] {
        w->hide();
        Fl::delete_widget(w);
        g_locale_editor = nullptr;
        if (standalone) exit(0);
    };
    save->on_press = [=] { auto p = collect(); p.save(true); p.apply(); close(); };
    use->on_press = [=] { auto p = collect(); p.save(false); p.apply(); close(); };
    cancel->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace rambler
