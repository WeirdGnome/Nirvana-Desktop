// Nirvana Desktop - nirvana-denise (file manager and desktop)
//
// Usage:
// nirvana-denise                 simple mode: only $HOME is visible
// nirvana-denise --home DIR      another home folder (e.g. for testing)
// nirvana-denise --size 1024x768 desktop size (the whole screen by default)
// nirvana-denise --center        the desktop in the middle of the work area, i.e. avoiding
// the panels of another desktop (e.g. the GNOME top bar);
// without --size it fills the whole work area.
// The same: the variable NIRVANA_CENTER=1
#include <FL/Fl.H>
#include <FL/platform.H>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <string>

#include <filesystem>

#include "desktop.h"
#include "appimage.h"
#include "gary.h"
#include <fstream>
#include <sstream>
#include "platinum.h"

int main(int argc, char **argv) {
  std::string home = getenv("HOME") ? getenv("HOME") : "/";
  int W = 0, H = 0;
  bool center = getenv("NIRVANA_CENTER") && !strcmp(getenv("NIRVANA_CENTER"), "1");

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--home") && i + 1 < argc) home = argv[++i];
    else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &W, &H);
    else if (!strcmp(argv[i], "--center") || !strcmp(argv[i], "--srodek")) center = true;
    else if (!strcmp(argv[i], "--help")) {
      printf("nirvana-denise [--home DIR] [--size WxH] [--center]\n"
             "  --center  desktop centred in the work area (e.g. under the GNOME panel);\n"
             "            without --size it fills the whole work area (also: NIRVANA_CENTER=1)\n");
      return 0;
    }
  }
  while (home.size() > 1 && home.back() == '/') home.pop_back();

  plat::init();
  FolderView::set_home(home);
  {
    // ~/Applications -> ~/.nirvana-apps; remembered paths (dock, windows, icons) follow it
    std::string old = appimage::migrate_apps_dir(home);
    if (!old.empty()) {
      const char *x = getenv("XDG_CONFIG_HOME");
      std::string cfg = (x && *x ? std::string(x) : home + "/.config") + "/nirvana/";
      std::string neu = appimage::apps_dir(home);
      for (const char *f : {"dock", "windows", "icon-positions"}) {
        std::ifstream in(cfg + f);
        if (!in) continue;
        std::stringstream all;
        all << in.rdbuf();
        std::string t = all.str(), out;
        size_t pos = 0, hit;
        while ((hit = t.find(old, pos)) != std::string::npos) {
          char next = hit + old.size() < t.size() ? t[hit + old.size()] : '\n';
          out += t.substr(pos, hit - pos);
          out += (next == '/' || next == '\n' || next == '\t') ? neu : old;
          pos = hit + old.size();
        }
        out += t.substr(pos);
        in.close();
        std::ofstream(cfg + f) << out;
      }
    }
    std::error_code ec;   // the applications folder always exists
    std::filesystem::create_directories(appimage::apps_dir(home), ec);
  }

  int X = 0, Y = 0;
  if (center) {
    // Work area = the screen without the desktop panels (from the window manager's _NET_WORKAREA).
    int wx, wy, ww, wh;
    Fl::screen_work_area(wx, wy, ww, wh);
    if (!W || !H) { W = ww; H = wh; }
    X = std::max(wx, wx + (ww - W) / 2);
    Y = std::max(wy, wy + (wh - H) / 2);
  } else if (!W || !H) {
    int sx, sy;
    Fl::screen_xywh(sx, sy, W, H);
  }

  Desktop::set_argv(argc, argv);   // Special > Reset Nirvana restarts Rambler
  Desktop desk(W, H, home);
  gary::init(home);
  desk.position(X, Y);
  desk.border(0);
  desk.xclass("Nirvana-Desktop");   // insight recognizes the desktop and gives it no frame
  desk.show();
  // For other window managers: the standard marking of a desktop window (EWMH).
  {
    Atom type = XInternAtom(fl_display, "_NET_WM_WINDOW_TYPE", False);
    Atom desk_t = XInternAtom(fl_display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    Window xid = fl_xid(&desk);
    // FLTK maps the window already in show(), so the window manager saw it as a normal one.
    // We take it down, mark it as a desktop and show it again - then the WM
    // (Openbox, GNOME, KDE...) puts it at the very bottom and gives it no frame.
    // Withdrawing as in ICCCM: unmap + a synthetic UnmapNotify to the root,
    // then we wait until the window manager confirms (WM_STATE=Normal disappears).
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
    // First let the window manager finish taking the window (WM_STATE=Normal). If
    // the withdrawal overtook its MapRequest, it would take the desktop down after our
    // remapping and the screen stayed black (it happened under Openbox).
    for (int i = 0; desk.wm_present && i < 150 && !normal_state(); i++) usleep(10000);
    bool managed = normal_state();   // without a window manager there is nothing to wait for
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
    // Initial state: always below other windows and without an entry in the desktop's task list.
    Atom states[] = {XInternAtom(fl_display, "_NET_WM_STATE_BELOW", False),
                     XInternAtom(fl_display, "_NET_WM_STATE_SKIP_TASKBAR", False),
                     XInternAtom(fl_display, "_NET_WM_STATE_SKIP_PAGER", False)};
    XChangeProperty(fl_display, xid, XInternAtom(fl_display, "_NET_WM_STATE", False), XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)states, 3);
    // Under a foreign window manager (GNOME, Openbox...) a click on the Nirvana Desktop menu bar must not
    // take the activity away from the window you work in - the application menu and File/Edit act
    // on it. So the desktop does not take the focus (ICCCM: WM_HINTS input = False).
    // Insight handles the desktop itself and does not need this exception.
    if (desk.csd_mode) {
      if (XWMHints *hints = XGetWMHints(fl_display, xid)) {
        hints->flags |= InputHint;
        hints->input = False;
        XSetWMHints(fl_display, xid, hints);
        XFree(hints);
      }
    }
    XMapWindow(fl_display, xid);
    XMoveWindow(fl_display, xid, X, Y);   // the window manager could change the position on the first mapping
    XLowerWindow(fl_display, xid);
    XSync(fl_display, False);
  }
  desk.restore();
  return Fl::run();
}
