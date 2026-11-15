/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Locale preferences (workbench/prefs/locale): the preferred languages (the catalogs
    and LANGUAGE), the country (the formats of dates, numbers and money: an installed
    locale, LC_TIME, LC_NUMERIC ...) and the timezone (TZ). Use writes the settings in use
    for the session (ENV:), Save also ~/.config/rambler/locale.prefs (ENVARC:); the session
    exports them, and Rambler gives them to the programs it starts from then on.
*/
#pragma once
#include <string>
#include <vector>

namespace rambler {

struct LocalePrefs {
    std::vector<std::string> languages;   // ISO codes in order ("pl", "en"); empty - the system's
    std::string country;                  // a locale ("pl_PL.UTF-8"); "" - the system's
    std::string timezone;                 // "Europe/Warsaw"; "" - the system's
    static LocalePrefs load();            // the settings in use (ENV:), else the saved ones
    void save(bool archive) const;        // Use: false, Save: true
    void apply() const;                   // into this program's environment (for what it starts)
    static std::string in_use_file();
};

void edit_locale_prefs(bool standalone);

}  // namespace rambler
