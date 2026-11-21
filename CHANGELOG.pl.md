<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Lista zmian

*[English](CHANGELOG.md)*

## 3.0.0-alpha5 — niewydane (NirvanaNG, etap 5)

### Dodane
- Cztery motywy obok Ambient, każdy z własnymi ikonami, gadżetami i kolorami: Classic
  1.3 (cztery kolory, ostre piksele, paskowane belki tytułowe), Classic 2.1 (szary z
  wypukłymi krawędziami), Classic 3.1 (osiem kolorów, niebieskoszare tło), Classic 4.1
  (gładkie szarości, gradientowe belki tytułowe).
- Wymienne dyski na pulpicie (UDisks2 na szynie systemowej): pendrive'y, karty pamięci i
  płyty dostają ikonę woluminu z ich etykietą; otwarcie montuje i pokazuje w listerze,
  Icons -> Eject (Super+Y) odmontowuje i wysuwa nośnik albo wyłącza napęd.
- Tools -> Theme: motywy znalezione w `Themes:`, używany zaznaczony; wybór jest
  zapisywany w `~/.nirvana/Prefs/desktop.conf`, a pulpit uruchamia się w nim od nowa.

### Zmienione
- Motywy mają wspólne grupy (`themes/common.edc`); motyw daje czcionki, rozmiary, klasy
  kolorów i obrazki. `tools/make-theme-images.py` rysuje obrazki wszystkich motywów.
- Gadżet zmiany rozmiaru pokazuje wciśnięcie.

## 3.0.0-alpha4 — 2026-11-21 (NirvanaNG, etap 4)

### Dodane
- Nirvana Dock: programy (.desktop, AppImage, pliki wykonywalne), pod-doki otwierane
  nad swoją ikoną, separatory i docky (zegar, CPU, RAM); upuszczone pliki są
  przypinane; menu ikony (Open, Move Left/Right, Remove from Dock); zapis w
  `~/.nirvana/Prefs/dock.conf`. Powierzchnia warstwy pod Waylandem.
- Lister: widok ikon lub listy, sortowanie po nazwie, dacie, rozmiarze, typie, tryb
  przeglądarki, pole położenia, archiwa jako szuflady (libarchive), menu pozycji (Open,
  Information, Copy Path, Extract Icon... dla AppImage), przeciąganie plików.
- `install.sh` dla Rocky Linux 10 (i AlmaLinux 10, dnf): `--deps` włącza EPEL i CRB i
  instaluje pakiety; EFL 1.28.1 i labwc 0.9.3 (z wlroots 0.19.2), których te systemy nie
  mają w pakietach, są budowane ze sprawdzonych tarballi wydań i pakowane.
  `--uninstall` usuwa także je.

### Zmienione
- `install.sh` robi pakiety (.deb lub .rpm: `nirvana-desktop`, `nirvana-libdwarfs`, a
  gdzie trzeba także `nirvana-efl`, `nirvana-labwc`) i instaluje je przez apt lub dnf,
  zamiast kopiować pliki do systemu; `--uninstall` usuwa pakiety (i pliki skopiowane
  przez starszy install.sh), `--no-install` tylko je robi.
- Tylko Wayland: sesja X11 (`nirvana-session --x11`, wpis X11 w menedżerze logowania) i
  tryb X11 pulpitu (`nirvana-desktop --x11`) zostały usunięte. Programy X11 działają
  przez XWayland, a globalne menu nadal je obsługuje.
- Tytuły menu pokazują się, gdy trzymany jest prawy przycisk myszy - na pasku lub na
  pulpicie - jak na klasycznych pulpitach; poza tym pasek pokazuje tytuł ekranu lub
  programu (więc „Nirvana Screen” nie jest już dwa razy). Najechanie na tytuł rozwija
  jego menu, puszczenie nad pozycją ją wybiera. `menus=always` w
  `~/.nirvana/Prefs/desktop.conf` zostawia tytuły na stałe.
- Ikona woluminu systemowego ma podpis „System” (assign pozostaje `SYS:`).

### Poprawione
- Na sprzęcie, którego sterownik nie daje wlroots buforów GPU (błąd gbm_bo_create:
  czarny ekran), sesja uruchamia labwc ponownie - najpierw na GPU bez
  modyfikatorów buforów (Intel), potem z renderowaniem programowym - i zapamiętuje to,
  co zadziałało, w `~/.nirvana/Prefs/wlr-renderer`. `install.sh --deps` instaluje
  sterowniki Mesa (`libgl1-mesa-dri`, `libgbm1`).
- Pod Waylandem menu zostawiały po sobie napisy po otwarciu innego menu lub zamknięciu
  (wiersze nie były usuwane razem z panelem).
- Sesje uruchamiane przez menedżer logowania bez elogind (np. LightDM na Devuanie)
  dawały czarny ekran: brak `XDG_RUNTIME_DIR`, bez którego labwc nie startuje.
  `nirvana-session` sam go teraz tworzy, uruchamia szynę sesji, gdy jej brak, zapisuje
  dziennik w `~/.nirvana/Logs/session.log`, restartuje pulpit po awarii i kończy sesję
  (powrót do ekranu logowania), gdy pulpit w ogóle nie startuje.

## 3.0.0-alpha3 — niewydane (NirvanaNG, etap 3)

### Dodane
- Globalne menu: menu programu z fokusem na pasku ekranu, po menu Nirvana Screen.
  Programy GTK przez `org.gtk.Menus`/`org.gtk.Actions` (sekcje, podmenu, skróty, pozycje
  zaznaczane i radiowe); inne przez `com.canonical.AppMenu.Registrar`, którym jest teraz
  pulpit, i `com.canonical.dbusmenu` (skróty, pozycje wyłączone/zaznaczane/radiowe,
  podmenu na żądanie, zmiany na żywo). Fokus z `_NET_ACTIVE_WINDOW` pod X11 i w
  XWayland; natywne programy Waylanda dostają nazwę i menu Window (zamknij, ikonizuj,
  powiększ) przez wlr-foreign-toplevel-management. Kliknięcie w pulpit przywraca jego
  menu.
- `install.sh --deps` instaluje `appmenu-gtk3-module` (i `appmenu-gtk2-module`).

### Poprawione
- Wpisy sesji są instalowane także tam, gdzie `/usr/share/wayland-sessions` jeszcze nie
  istniał; dochodzi wpis X11 w `/usr/share/xsessions`.

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
- Gadżet głębi spycha okno pod spód pod Waylandem w labwc (przycisk „Task” powiązany
  z akcją Lower, klikany wirtualnym wskaźnikiem); setup-env.sh podpowiada, co dopisać
  do własnego rc.xml labwc.
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
