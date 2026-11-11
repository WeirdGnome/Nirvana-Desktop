// Nirvana Desktop - tłumaczenia (gettext, domena "nirvana")
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

// Przeniesienie ustawień i danych po zmianie nazwy LisaOS -> Nirvana Desktop:
// ~/.config/lisaos, ~/.local/share/lisaos (aplikacje, tapety), ~/.cache/lisaos
// -> .../nirvana. Raz, przy pierwszym starcie; woła ją init_i18n().
void migrate_old_config();
void init_i18n();                       // woła plat::init(); można też samodzielnie (Insight)
const char *tr(const char *msgid);
std::string trn(long n, const char *one, const char *other);
std::string fmt(const std::string &pattern, const std::vector<std::string> &args);
template <typename... A> std::string fmt(const std::string &pattern, const A &...args) {
  return fmt(pattern, std::vector<std::string>{std::string(args)...});
}
// Ścieżka menu FLTK ("Okno/Sortuj/według nazwy") - tłumaczony każdy człon osobno,
// więc ta sama nazwa podmenu w różnych pozycjach ma jedno tłumaczenie.
std::string tr_path(const char *path);
std::string date_time(long t);         // "8 lis 2026, 21:40" / "Nov 8, 2026, 21:40"
std::string decimal(std::string s);     // "3.4" -> "3,4" in Polish
std::string language();                 // "en", "pl"... (language the interface runs in)
std::string language_setting();         // saved choice: "", "en", "pl"
bool set_language_setting(const std::string &lang);
std::vector<std::pair<std::string, std::string>> available_languages();   // kod, nazwa własna

}  // namespace plat

#ifndef _
#define _(s) ::plat::tr(s)
#endif
#ifndef P_
#define P_(s) s                         // ścieżka menu (narzędzie .pot dzieli ją na człony)
#endif
#ifndef N_
#define N_(s) s
#endif
