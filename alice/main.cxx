// Nirvana Desktop - alice: control panels
//
//   nirvana-alice [appearance]          Appearance panel (default)
//   nirvana-alice network [--hidden]    Network panel (--hidden: open the "Other network" window)
//   nirvana-alice power                 Power panel
//   nirvana-alice printers             Printers panel (CUPS)
//   nirvana-alice desktop              Desktop panel (room for windows)
//   nirvana-alice datetime             Date & Time panel (clock, timers, time zone)
//   nirvana-alice user                 User panel (picture, name, password, automatic login)
//   nirvana-alice shader               Shader panel (full-screen effects)
//   nirvana-alice preferences           Preferences: a window with the icons of all panels
//   nirvana-alice scheme NAME           set the color scheme without a window
//                                       (e.g. redmond97-se/ouroboros)
#include "platinum.h"

#include <FL/Fl.H>
#include <cstdio>
#include <cstring>

int panel_appearance();
int panel_network(bool hidden_dialog);
int panel_power();
int panel_preferences();
int panel_shader();
int panel_datetime();
int panel_desktop();
int panel_printers();
int panel_user();

int main(int argc, char **argv) {
  plat::init();
  Fl::visual(FL_RGB);
  const char *which = argc > 1 ? argv[1] : "appearance";
  if (!strcmp(which, "scheme") && argc > 2) {
    plat::Scheme sc;
    if (!plat::load_scheme(argv[2], sc)) { fprintf(stderr, "%s\n", plat::fmt(_("No such scheme: %1"), argv[2]).c_str()); return 1; }
    plat::Appearance a = plat::read_appearance();
    a.scheme = argv[2];
    if (!plat::write_appearance(a)) return 1;
    plat::notify_appearance_changed();
    return 0;
  }
  if (!strcmp(which, "network")) return panel_network(argc > 2 && !strcmp(argv[2], "--hidden"));
  if (!strcmp(which, "power")) return panel_power();
  if (!strcmp(which, "preferences")) return panel_preferences();
  if (!strcmp(which, "shader")) return panel_shader();
  if (!strcmp(which, "datetime")) return panel_datetime();
  if (!strcmp(which, "desktop")) return panel_desktop();
  if (!strcmp(which, "printers")) return panel_printers();
  if (!strcmp(which, "user")) return panel_user();
  return panel_appearance();
}
