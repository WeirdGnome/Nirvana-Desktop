/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Catalogs: the texts of the interface in the user's language, as the system's locale
    catalogs. The language comes from LANGUAGE / LC_ALL / LC_MESSAGES / LANG ("pl_PL.UTF-8"
    -> polski); its catalog (share/rambler/catalogs/polski.catalog) has one line per text:
    the English text, a TAB, the translation ("\n" for a new line). A text without a
    translation shows as it is.
*/
#pragma once
#include <string>

namespace lune {

const std::string &tr(const std::string &english);
// the same for a text with Lune's control sequences at its start ("\033c...")
std::string tr_text(const std::string &text);
const char *language_name();   // "polski", "" - English

// a translated text with %s in it filled in, in order ("Delete %s?")
std::string trs(const std::string &english, const std::string &a = "", const std::string &b = "");

}  // namespace lune
