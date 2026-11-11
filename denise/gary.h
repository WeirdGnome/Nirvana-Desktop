// Nirvana Desktop - gary: przełącznik aplikacji (menu aplikacji w prawym górnym rogu)
//
// Na razie mieszka w procesie Ramblera, bo to on rysuje globalny pasek menu.
// Działa na standardzie EWMH, więc pokazuje aplikacje pod Insightem i pod
// innymi pulpitami (GNOME, KDE, Openbox):
//   * lista okien:        _NET_CLIENT_LIST
//   * aktywne okno:       _NET_ACTIVE_WINDOW
//   * ukrywanie:          WM_CHANGE_STATE (IconicState), stan _NET_WM_STATE_HIDDEN
//   * przywracanie:       _NET_ACTIVE_WINDOW
//   * zamykanie:          _NET_CLOSE_WINDOW
// Aplikacja = grupa okien o tej samej klasie WM_CLASS.
#pragma once

#include <FL/Fl_Image.H>
#include <X11/Xlib.h>
#include <string>
#include <vector>

namespace gary {

struct App {
  std::string cls;            // WM_CLASS (klucz grupy)
  std::string name;           // nazwa do wyświetlenia
  std::string icon_file;      // ikona z AppImage (jeśli to aplikacja uruchomiona z Ramblera)
  std::vector<Window> wins;   // okna tej aplikacji (kolejność z menedżera okien)
  bool hidden = false;        // wszystkie okna ukryte
};

void init(const std::string &home);

// Rambler uruchamia AppImage: pierwsza nowa klasa okien, która pojawi się
// w ciągu kilkunastu sekund, dostaje tę nazwę i ikonę (zapamiętywane na stałe).
void note_launch(const std::string &name, const std::string &icon_file);

// Lista okien zmieniła się (PropertyNotify _NET_CLIENT_LIST) - przypisz nowe klasy.
void clients_changed();

// Aplikacje poza Ramblerem; skip(w) == true dla okien Ramblera i biurka.
std::vector<App> list_apps(bool (*skip)(Window));

Window active_window();
bool is_hidden(Window w);
void activate(Window w);       // pokaż (jeśli ukryte), wyciągnij na wierzch, daj fokus
void hide(Window w);           // ukryj (zminimalizuj)
void close(Window w);          // poproś menedżer okien o zamknięcie

// Ikona okna z _NET_WM_ICON w rozmiarze size x size (nullptr, jeśli program jej nie podaje).
Fl_Image *window_icon(Window w, int size = 16);
std::string window_title(Window w);   // _NET_WM_NAME albo WM_NAME

} // namespace gary
