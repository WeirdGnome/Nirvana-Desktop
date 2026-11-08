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
#include <X11/Xutil.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <string>

#include <filesystem>

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

  plat::init(root);
  FolderView::set_home(home);
  {
    std::error_code ec;   // folder na aplikacje zawsze istnieje
    std::filesystem::create_directories(home + "/Aplikacje", ec);
  }

  if (!W || !H) {
    int sx, sy;
    Fl::screen_xywh(sx, sy, W, H);
  }

  Desktop desk(W, H, home, root);
  desk.border(0);
  desk.xclass("LisaOS-Desktop");   // insight rozpozna biurko i nie da mu ramki
  desk.show();
  // Dla innych menedżerów okien: standardowe oznaczenie okna biurka (EWMH).
  {
    Atom type = XInternAtom(fl_display, "_NET_WM_WINDOW_TYPE", False);
    Atom desk_t = XInternAtom(fl_display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    Window xid = fl_xid(&desk);
    // FLTK mapuje okno już w show(), więc menedżer okien widział je jako zwykłe.
    // Zdejmujemy je, oznaczamy jako biurko i pokazujemy ponownie - wtedy WM
    // (Openbox, GNOME, KDE...) położy je na samym dole i nie da mu ramki.
    // Wycofanie zgodne z ICCCM: unmap + syntetyczne UnmapNotify do korzenia,
    // potem czekamy, aż menedżer okien potwierdzi (zniknie WM_STATE=Normal).
    Atom wm_state = XInternAtom(fl_display, "WM_STATE", False);
    auto normal_state = [&]() {
      Atom t; int f; unsigned long n, after; unsigned char *d = nullptr;
      bool normal = false;
      if (XGetWindowProperty(fl_display, xid, wm_state, 0, 2, False, wm_state, &t, &f, &n, &after,
                             &d) == Success && d) {
        normal = n && ((long *)d)[0] == NormalState;
        XFree(d);
      }
      return normal;
    };
    XSync(fl_display, False);
    bool managed = normal_state();   // bez menedżera okien nie ma na co czekać
    XUnmapWindow(fl_display, xid);
    XEvent ev{};
    ev.xunmap.type = UnmapNotify;
    ev.xunmap.event = DefaultRootWindow(fl_display);
    ev.xunmap.window = xid;
    ev.xunmap.from_configure = False;
    XSendEvent(fl_display, DefaultRootWindow(fl_display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XSync(fl_display, False);
    for (int i = 0; managed && i < 100 && normal_state(); i++) usleep(10000);
    XChangeProperty(fl_display, xid, type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&desk_t, 1);
    XMapWindow(fl_display, xid);
    XLowerWindow(fl_display, xid);
    XSync(fl_display, False);
  }
  desk.restore();
  return Fl::run();
}
