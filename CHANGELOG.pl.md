<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Lista zmian

*[English](CHANGELOG.md)*

## 3.0.0-alpha2 — niewydane (NirvanaNG, etap 2)

### Dodane
- `nirvana-desktop` (EFL): pasek ekranu „Nirvana Screen” i tło jako powierzchnie
  wlr-layer-shell pod Waylandem (okna dock i desktop pod X11); rozwijane menu (Nirvana
  Screen, Window, Icons, Tools) z myszą, przytrzymanym prawym przyciskiem, klawiaturą i
  skrótami Super; zegar; ikony woluminów dla assignów z DESKTOP=1 (zaznaczanie,
  przeciąganie, ramka, otwieranie, Snapshot, Clean Up, przeładowanie na SIGUSR1).
- Gadżety okien Nirvany: zamknięcie; prefs, jump, iconify, zoom, depth; zmiana
  rozmiaru - przesuwanie i zmiana rozmiaru przez serwer wyświetlania.
- Edytor assignów: New, Clone, Delete; nazwa, ikona na pulpicie, ścieżki w kolejności
  (Add Path, Remove Path, Move Up/Down, Open in Lister); Use, Save, Cancel.
- Pierwszy lister: ikony czytane w wątku, ikony AppImage, wzorce `#?` w filtrze.
- Usługa D-Bus `org.nirvana.Desktop` (OpenDrawer, ReloadAssigns, ShowAssignsEditor).
- Motyw Ambient (`themes/ambient.edc`, obrazki rysowane przez
  `tools/make-theme-images.py`); opis kontraktu motywów w `themes/README.md`.
- `install.sh`: budowanie, testy, instalacja i konfiguracja jednym poleceniem
  (`--deps`, `--no-tests`, `--no-dwarfs`, `--uninstall`); pobiera libdwarfs 0.15.10
  (ze sprawdzeniem SHA-256), buduje ją i instaluje dla AppImage z DwarFS, radzi sobie
  z plikami datowanymi w przyszłość (zegar opóźniony) i ze źródłami bez prawa zapisu.
- AppImage z DwarFS czytane przez libdwarfs 0.15 (most w C++23, jak jej nagłówki);
  test DwarFS z mkdwarfs, gdy jest dostępny.
- `nirvana-session` (labwc pod Waylandem lub obok menedżera okien pod X11), ustawienia
  labwc, wpis sesji dla menedżerów logowania.

## 3.0.0-alpha1 — niewydane (NirvanaNG, etap 1)

Nowy początek na gałęzi `NirvanaNG`: nowy kod, Meson, C11.

### Dodane
- `libnirvana` (LGPL-3.0-or-later):
  - baza assignów w `$XDG_RUNTIME_DIR/nirvana-ram/assigns.db` ze zmianami atomowymi pod
    blokadą; domyślne assigny (SYS:, Home:, RAM:, Nirvana:, Themes:, Tools:, katalogi
    użytkownika XDG, katalogi z $HOME); multi-assigny; tworzenie, zastępowanie,
    dodawanie, usuwanie, zmiana kolejności, zmiana nazwy i kasowanie;
    `nirvana_to_posix()` i `posix_to_nirvana()` (najdłuższy prefiks); trwały zapis w
    `~/.nirvana/Startup-Sequence`; powiadamianie działającego pulpitu sygnałem SIGUSR1;
  - czytnik AppImage, który niczego nie uruchamia: offset ładunku ELF (ELF64 i ELF32),
    własny czytnik SquashFS 4.0 (gzip, lzma, xz, lz4, zstd), ISO 9660 przez libarchive,
    DwarFS przez libdwarfs (opcjonalnie); ikona (.DirIcon, inaczej Icon z pliku
    .desktop) i klucze pliku .desktop;
  - środowisko uruchamiania dla globalnego menu, domyślny program typu MIME
    (mimeapps.list), procesy potomne odłączone od sesji.
- `assign`, `wbrun` (EFL: Efreet, Eldbus) i `appimage-info` (GPL-3.0-or-later).
- `setup-env.sh` i szablon `Startup-Sequence`.
- Testy: assigny, polecenie `assign`, AppImage spakowane każdym kompresorem i jako ISO,
  uszkodzone obrazy.
