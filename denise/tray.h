// Nirvana Desktop - zasobnik systemowy (system tray) na pasku menu Ramblera
//
// Obsługuje oba protokoły, których używają programy:
//   * XEmbed (freedesktop System Tray): Rambler przejmuje selekcję
//     _NET_SYSTEM_TRAY_S<ekran> i osadza okienka ikon (GTK2/3, Qt4, Wine, xembed),
//   * StatusNotifierItem przez D-Bus (Qt5/6, KDE, Electron, libappindicator):
//     gdy nikt inny tego nie robi, Rambler jest też org.kde.StatusNotifierWatcher.
//     Ikony rysujemy sami (motyw ikon albo piksmapy), menu kontekstowe
//     przychodzą przez com.canonical.dbusmenu i pokazujemy je jako menu Platinum.
#pragma once

#include <FL/Fl_Widget.H>
#include <functional>

class Fl_Window;

namespace tray {

// Widget z ikonami: postaw go na pasku menu; jego szerokość podaje width().
Fl_Widget *make(int X, int Y, int H);
// Uruchom zasobnik. changed() woła się, gdy zmienia się liczba ikon (przestaw pasek).
void init(Fl_Window *desk, std::function<void()> changed);
int width();          // potrzebna szerokość (0, gdy pusto)
void recolor();       // po zmianie schematu: tło okienek XEmbed
void shutdown();      // zwolnij selekcję i oddaj okienka ikon (przed restartem/wyjściem)

}  // namespace tray
