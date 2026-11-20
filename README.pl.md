<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Nirvana Desktop 3 (NirvanaNG)

*[English](README.md)*

Następna generacja Nirvana Desktop: lekka powłoka pulpitu dla Devuan GNU/Linux (x86_64,
bez systemd), zbudowana na Enlightenment Foundation Libraries pod Waylandem
(wlr-layer-shell), w duchu klasycznych pulpitów przestrzennych — pasek ekranu z globalnym
menu, tło z ikonami woluminów, logiczne woluminy („assigny”), dok i listery plików. To
nowy kod, pisany od zera; nie dzieli niczego z gałęzią `main` (Rambler) i nie emuluje
bibliotek innych systemów.

Gałąź powstaje etapami:

| Etap | Co | Stan |
|---|---|---|
| 1 | `libnirvana` (assigny, czytnik AppImage), `assign`, `wbrun`, `appimage-info`, testy | **gotowe** |
| 2 | Pulpit EFL: pasek „Nirvana Screen”, tło, gadżety okien, edytor assignów | następny |
| 3 | Globalne menu (`com.canonical.AppMenu.Registrar`, Eldbus) | |
| 4 | Dok i listery plików (wielowątkowe) | |
| 5 | Motywy Edje: Classic13, Classic21, Classic31, Classic41, Ambient | |

## libnirvana (LGPL-3.0-or-later)

**Assigny** — logiczne nazwy woluminów oznaczające jeden lub więcej katalogów
(`include/nirvana.h`). Assigny sesji są w `$XDG_RUNTIME_DIR/nirvana-ram/assigns.db`
(tmpfs), jeden na linię: `NAZWA:|DESKTOP|ŚCIEŻKA_1;ŚCIEŻKA_2;...`.

`nirvana_assign_init()` zapisuje domyślne przy starcie sesji: `SYS:` (/), `Home:`,
`RAM:`, `Nirvana:` (~/.nirvana;/opt/nirvana), `Themes:`, `Tools:`, katalogi użytkownika
XDG jako `Downloads:`, `Docs:`, `Pics:`, `Music:`, `Videos:` oraz assign dla każdego
widocznego katalogu na wierzchu `$HOME`. Multi-assigny są przeszukiwane od lewej;
`nirvana_to_posix()` i `posix_to_nirvana()` tłumaczą w obie strony (wygrywa najdłuższy
prefiks). Zmiany są atomowe (blokada, plik tymczasowy, rename). Assigny użytkownika mogą
być trwale zapisane w `~/.nirvana/Startup-Sequence`.

**AppImage** — ikona i metadane `.desktop` czytane bez uruchamiania czegokolwiek
(`include/nirvana_appimage.h`): plik jest mapowany tylko do odczytu, ładunek znajdowany
za runtime'em ELF (`e_shoff + e_shnum * e_shentsize`) i czytany jako system plików,
którym jest: SquashFS 4.0 (własny czytnik: gzip, lzma, xz, lz4, zstd; dowiązania,
fragmenty, bloki rzadkie; każdy offset z obrazu sprawdzany), ISO 9660 (przez libarchive),
DwarFS (przez libdwarfs, jeśli jest przy budowaniu).

## Polecenia (GPL-3.0-or-later)

```
assign                               lista assignów
assign Work: /srv/work;~/work --desktop
assign Work: /mnt/more --add
assign Work: REMOVE --save
wbrun Downloads:                     otwiera szufladę (w listerze pulpitu)
wbrun Tools:org.example.App.desktop  uruchamia program
wbrun Docs:list.odt                  otwiera plik domyślnym programem
appimage-info Coś.AppImage [--icon out.png | --desktop | --extract ŚCIEŻKA WYJŚCIE]
```

## Budowanie

```
sudo apt install meson ninja-build pkg-config libarchive-dev zlib1g-dev liblzma-dev \
                 libzstd-dev liblz4-dev libefl-all-dev squashfs-tools
meson setup build && ninja -C build && meson test -C build
sudo ninja -C build install        # do /opt/nirvana (C/, Libs/)
sudo ./setup-env.sh --system
./setup-env.sh --user
```

## Znane ograniczenia

* **DwarFS**: libdwarfs nie ma w pakietach Devuana ani Debiana; AppImage z DwarFS są
  czytane tylko, gdy libdwarfs (0.12+, z pakietem CMake) jest zainstalowana przy
  budowaniu. Bez niej zgłaszają „nieobsługiwane”.
* **Globalne menu pod Waylandem**: `com.canonical.AppMenu.Registrar` rozpoznaje okna po
  identyfikatorach okien X11. Pod Waylandem działa dla programów X11 (przez XWayland);
  natywne programy Waylanda nie mają takich identyfikatorów, a kompozytory wlroots nie
  mają jeszcze wspólnego protokołu, więc ich menu zostają w oknach.
* Obrazy SquashFS kompresowane LZO nie są czytane.

## Licencje

`libnirvana` (`src/lib/`, `include/`): LGPL-3.0-or-later; reszta: GPL-3.0-or-later.
