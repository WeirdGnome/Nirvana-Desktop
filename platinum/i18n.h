// LisaOS - tłumaczenia (gettext, domena "lisaos")
//
// Teksty w kodzie są po polsku i to one są kluczami tłumaczeń (msgid);
// inne języki przychodzą z plików po/<język>.po (np. po/en.po).
//
//   _("Zamknij")                  tekst do przetłumaczenia
//   N_("Gładki")                  tylko oznaczenie (tablice statyczne) - tłumaczy _() przy użyciu
//   trp(n, "element", "elementy", "elementów")
//                                 liczba mnoga: po polsku 3 formy, w tłumaczeniu wg reguł języka
//   P_("Okno/Nowy folder")        ścieżka menu - tr_path() tłumaczy człony
//   fmt(_("Usunąć „%1”?"), nazwa) szablon z %1..%9 - tłumacz może zmienić szyk zdania
//
// Język: ~/.config/lisaos/jezyk ("pl", "en" albo pusty = jak w systemie).
// Przy systemie w innym języku niż polski i bez tłumaczenia - angielski.
#pragma once

#include <string>
#include <vector>

namespace plat {

void init_i18n();                       // woła plat::init(); można też samodzielnie (Insight)
const char *tr(const char *msgid);
std::string trp(long n, const char *one, const char *few, const char *many);
std::string fmt(const std::string &pattern, const std::vector<std::string> &args);
template <typename... A> std::string fmt(const std::string &pattern, const A &...args) {
  return fmt(pattern, std::vector<std::string>{std::string(args)...});
}
// Ścieżka menu FLTK ("Okno/Sortuj/według nazwy") - tłumaczony każdy człon osobno,
// więc ta sama nazwa podmenu w różnych pozycjach ma jedno tłumaczenie.
std::string tr_path(const char *path);
std::string date_time(long t);         // "8 lis 2026, 21:40" / "Nov 8, 2026, 21:40"
std::string decimal(std::string s);     // "3.4" -> "3,4" po polsku
std::string language();                 // "pl", "en"... (język, w którym działa interfejs)
std::string language_setting();         // zapisany wybór: "", "pl", "en"
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
