// LisaOS - alice: panele sterowania
//
//   lisaos-alice [wyglad]          panel Wygląd (domyślny)
//   lisaos-alice siec [--ukryta]   panel Sieć (--ukryta: od razu okno "Inna sieć")
#include "platinum.h"

#include <FL/Fl.H>
#include <cstring>

int panel_wyglad();
int panel_siec(bool hidden_dialog);

int main(int argc, char **argv) {
  plat::init(false);
  Fl::visual(FL_RGB);
  const char *which = argc > 1 ? argv[1] : "wyglad";
  if (!strcmp(which, "siec") || !strcmp(which, "sieć")) {
    bool hidden = argc > 2 && !strcmp(argv[2], "--ukryta");
    return panel_siec(hidden);
  }
  return panel_wyglad();
}
