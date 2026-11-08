// LisaOS - lisaos-denise (menedżer plików i biurko)
//
// Użycie:
//   lisaos-denise                 tryb prosty: widać tylko $HOME
//   lisaos-denise --root          tryb root: cały system plików (uruchamiany przez pkexec)
//   lisaos-denise --home DIR      inny katalog domowy (np. do testów)
//   lisaos-denise --size 1024x768 rozmiar biurka (domyślnie cały ekran)
#include <FL/Fl.H>
#include <FL/platform.H>
#include <X11/Xatom.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "desktop.h"
#include "platinum.h"

int main(int argc, char **argv) {
  bool root = false;
  std::string home = getenv("HOME") ? getenv("HOME") : "/";
  int W = 0, H = 0;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--root")) root = true;
    else if (!strcmp(argv[i], "--home") && i + 1 < argc) home = argv[++i];
    else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &W, &H);
    else if (!strcmp(argv[i], "--help")) {
      printf("lisaos-denise [--root] [--home KATALOG] [--size SZERxWYS]\n");
      return 0;
    }
  }
  while (home.size() > 1 && home.back() == '/') home.pop_back();

  signal(SIGCHLD, SIG_IGN);   // uruchomione aplikacje nie zostawiają zombie
  plat::init(root);

  if (!W || !H) {
    int sx, sy;
    Fl::screen_xywh(sx, sy, W, H);
  }

  Desktop desk(W, H, home, root);
  desk.border(0);
  desk.xclass("LisaOS-Desktop");   // agnus rozpozna biurko i nie da mu ramki
  desk.show();
  // Dla innych menedżerów okien: standardowe oznaczenie okna biurka (EWMH).
  {
    Atom type = XInternAtom(fl_display, "_NET_WM_WINDOW_TYPE", False);
    Atom desk_t = XInternAtom(fl_display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(fl_display, fl_xid(&desk), type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&desk_t, 1);
  }
  desk.restore();
  return Fl::run();
}
