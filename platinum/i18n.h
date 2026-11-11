// Nirvana Desktop - translations (gettext, domain "nirvana")
//
// Texts in the code are in English and are the translation keys (msgid);
// other languages come from po/<lang>.po (e.g. po/pl.po).
//
//   _("Close")                    text to translate
//   N_("Plain")                   mark only (static tables) - _() translates it where used
//   trn(n, "%1 item", "%1 items") plural: English forms in code, translation follows its language
//   P_("Window/New Folder")       menu path - tr_path() translates each part
//   fmt(_("Delete “%1”?"), name)  template with %1..%9 - translators may reorder the sentence
//
// Language: ~/.config/nirvana/jezyk ("en", "pl" or empty = system language).
// English by default; Polish when the system locale is Polish.
#pragma once

#include <string>
#include <vector>

namespace plat {

// Moving settings and data from older versions (Polish file names, keys and
// values until 0.0.6) to the current names. Once, at the first start;
// called by init_i18n().
void migrate_old_config();
void init_i18n();                       // called by plat::init(); can also be called on its own (Insight)
const char *tr(const char *msgid);
std::string trn(long n, const char *one, const char *other);
std::string fmt(const std::string &pattern, const std::vector<std::string> &args);
template <typename... A> std::string fmt(const std::string &pattern, const A &...args) {
  return fmt(pattern, std::vector<std::string>{std::string(args)...});
}
// An FLTK menu path ("Window/Sort/by Name") - every segment is translated separately,
// so the same submenu name in different places has one translation.
std::string tr_path(const char *path);
std::string date_time(long t);         // "Nov 8, 2026, 21:40" (in Polish "8 lis 2026, 21:40")
std::string decimal(std::string s);     // "3.4" -> "3,4" in Polish
std::string language();                 // "en", "pl"... (language the interface runs in)
std::string language_setting();         // saved choice: "", "en", "pl"
bool set_language_setting(const std::string &lang);
std::vector<std::pair<std::string, std::string>> available_languages();   // code, native name

}  // namespace plat

#ifndef _
#define _(s) ::plat::tr(s)
#endif
#ifndef P_
#define P_(s) s                         // a menu path (the .pot tool splits it into segments)
#endif
#ifndef N_
#define N_(s) s
#endif
