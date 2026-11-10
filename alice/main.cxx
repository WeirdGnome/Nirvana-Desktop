// Nirvana Desktop - alice: panele sterowania
//
//   nirvana-alice [wyglad]          panel Wygląd (domyślny)
//   nirvana-alice siec [--ukryta]   panel Sieć (--ukryta: od razu okno "Inna sieć")
//   nirvana-alice zasilanie          panel Zasilanie
//   nirvana-alice ustawienia         Preferencje: okno z ikonami wszystkich paneli
//   nirvana-alice schemat NAZWA     ustaw schemat kolorów bez okna (np. redmond97-se/ouroboros)
#include "platinum.h"

#include <FL/Fl.H>
#include <cstdio>
#include <cstring>

int panel_wyglad();
int panel_siec(bool hidden_dialog);
int panel_zasilanie();
int panel_ustawienia();

int main(int argc, char **argv) {
  plat::init(false);
  Fl::visual(FL_RGB);
  const char *which = argc > 1 ? argv[1] : "wyglad";
  if (!strcmp(which, "schemat") && argc > 2) {
    plat::Scheme sc;
    if (!plat::load_scheme(argv[2], sc)) { fprintf(stderr, "%s\n", plat::fmt(_("Nie ma schematu: %1"), argv[2]).c_str()); return 1; }
    plat::Appearance a = plat::read_appearance();
    a.scheme = argv[2];
    if (!plat::write_appearance(a)) return 1;
    plat::notify_appearance_changed();
    return 0;
  }
  if (!strcmp(which, "siec") || !strcmp(which, "sieć")) {
    bool hidden = argc > 2 && !strcmp(argv[2], "--ukryta");
    return panel_siec(hidden);
  }
  if (!strcmp(which, "zasilanie")) return panel_zasilanie();
  if (!strcmp(which, "ustawienia") || !strcmp(which, "preferencje")) return panel_ustawienia();
  return panel_wyglad();
}
