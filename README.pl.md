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
| 2 | Pulpit EFL: pasek „Nirvana Screen”, tło, gadżety okien, edytor assignów | **gotowe** |
| 3 | Globalne menu (`com.canonical.AppMenu.Registrar`, `org.gtk.Menus`, Eldbus) | **gotowe** |
| 4 | Dok i listery plików (wielowątkowe) | następny |
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
DwarFS (przez libdwarfs 0.15, którą `install.sh` pobiera, sprawdza i buduje; most
jest w C++23, tak jak nagłówki libdwarfs).

## nirvana-desktop (GPL-3.0-or-later)

```
nirvana-session                      sesja: labwc z pulpitem (Wayland)
nirvana-desktop [--theme NAZWA]      sam pulpit, w działającym labwc
```

* **Pasek ekranu** „Nirvana Screen”: powierzchnia warstwy (wlr-layer-shell, warstwa top)
  rezerwująca swój pas ekranu. Tytuły menu
  (Nirvana Screen, Window, Icons, Tools), zegar, gadżet głębi. Menu rozwijają się po
  kliknięciu (lub przytrzymane prawym przyciskiem: puszczenie nad pozycją ją wybiera),
  obsługują klawiaturę (strzałki, Enter, Escape, podkreślone litery) i skróty z Super.
* **Tło**: powierzchnia warstwy tła z ikoną woluminu dla każdego assignu z `DESKTOP=1`;
  zaznaczanie, przeciąganie, ramka zaznaczenia, dwuklik otwiera, Snapshot i Clean Up.
  Ikony nadążają za assignami (SIGUSR1 z `assign`).
* **Gadżety okien** na własnych oknach Nirvany: zamknięcie po lewej; prefs, jump,
  iconify, zoom i depth po prawej; gadżet zmiany rozmiaru; przesuwanie i zmiana rozmiaru
  przez serwer wyświetlania, więc działają też pod Waylandem.
* **Edytor assignów** (Nirvana Screen -> Assigns...): New, Clone, Delete; nazwa, „Show
  volume icon on Desktop”, ścieżki w kolejności przeszukiwania z Add Path (wybór
  katalogu), Remove Path, Move Up/Down, Open in Lister; Use, Save (także do
  `~/.nirvana/Startup-Sequence`), Cancel.
* **Lister** (pierwsza wersja; etap 4 go uzupełni): szuflada jako ikony, czytana w
  wątku, ikony AppImage bez uruchamiania, wzorce `#?` w filtrze.
* D-Bus: `org.nirvana.Desktop` na szynie sesji (`OpenDrawer`, `ReloadAssigns`,
  `ShowAssignsEditor`).

Okna programów mają ramki od kompozytora (lub menedżera okien); gadżety są rysowane na
oknach Nirvany. Niektóre skróty z Super może przechwycić kompozytor.

Nirvana działa tylko pod Waylandem (nie ma sesji X11); programy X11 działają w niej przez
XWayland.

Gadżet głębi spycha okno pod spód w labwc: Wayland nie daje
programom polecenia „przesuń okno pod spód”, więc ustawienia labwc od Nirvany wiążą
przycisk myszy „Task” (którego myszy nie mają) nad oknem z akcją Lower, a gadżet
klika go wirtualnym wskaźnikiem. Przy własnym `~/.config/labwc/rc.xml` dopisz do sekcji
`<mouse>`:
`<context name="Client"><mousebind button="Task" action="Press"><action name="Lower"/></mousebind></context>`.
W innych kompozytorach gadżet wysuwa zamiast tego następne okno Nirvany.

## Globalne menu

Menu programu, który ma fokus, pokazuje się na pasku ekranu po menu Nirvana Screen;
własny pasek menu programu jest ukryty. Obsługiwane są dwa rodzaje programów:

* **GTK** (2 i 3, przez `appmenu-gtk-module`; paski menu GtkApplication): właściwości
  okna `_GTK_UNIQUE_BUS_NAME` / `_GTK_MENUBAR_OBJECT_PATH` i interfejsy `org.gtk.Menus` /
  `org.gtk.Actions` - sekcje, podmenu, skróty, pozycje zaznaczane i radiowe.
* programy **Registrara** (Qt z motywem platformy eksportującym menu, inne eksportery
  `com.canonical.dbusmenu`): `nirvana-desktop` jest `com.canonical.AppMenu.Registrar`;
  skróty, pozycje wyłączone, zaznaczane i radiowe, podmenu wypełniane na żądanie
  (AboutToShow), menu zmieniające się w trakcie.

Program X11 z fokusem jest czytany z `_NET_ACTIVE_WINDOW` XWaylanda, więc globalne menu
działa dla programów uruchomionych przez XWayland. Natywne programy Waylanda nie mają identyfikatora okna, pod którym mogłyby
zgłosić menu (protokoły go nie dają), więc dla nich pasek pokazuje nazwę programu i menu
Window (zamknij, ikonizuj, powiększ; przez wlr-foreign-toplevel-management), a menu
zostają w ich oknach. Program GTK z menu na pasku pod Waylandem: `GDK_BACKEND=x11`.

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

Najprościej: `./install.sh` (`./install.sh --deps` najpierw instaluje pakiety potrzebne
do budowania, `./install.sh --uninstall` usuwa instalację). Zna Debiana, Devuana i Ubuntu
(apt) oraz Rocky Linux 10, AlmaLinux 10 i pokrewne (dnf).

`install.sh` niczego nie kopiuje do systemu ręcznie: robi pakiety - `.deb` albo `.rpm` -
i instaluje je przez apt lub dnf, więc menedżer pakietów zna każdy plik, a
`apt remove` / `dnf remove` usuwa wszystko. Pakiety zostają w
`~/.cache/nirvana-desktop/packages` (`--no-install` tylko je robi):

* `nirvana-desktop` - pulpit, jego polecenia, motywy, sesje, `/nirvana`;
* `nirvana-libdwarfs` - libdwarfs, dla AppImage z DwarFS;
* `nirvana-efl`, `nirvana-labwc` - EFL i labwc, tylko tam, gdzie system ich nie ma.

**Rocky Linux 10**: nie ma tam w pakietach ani EFL, ani labwc (w EPEL też nie), więc
`./install.sh --deps` włącza EPEL i CRB, instaluje to, co jest w pakietach, a EFL 1.28.1
i labwc 0.9.3 (z wbudowanym wlroots 0.19.2) buduje z tarballi wydań, sprawdzonych
SHA-256, jako pakiety `nirvana-efl` i `nirvana-labwc` (w `/opt/nirvana/Libs`) - jeden raz; EFL buduje
się od kwadransa do godziny. Czego wlroots potrzebuje, a system nie ma, meson pobiera
gitem. Rocky Linux 10 nie ma serwera X, więc jest tylko sesja Wayland (GDM ją pokazuje).
Bez sieci włóż tarballe, które skrypt wymienia, do `~/.cache/nirvana-desktop`.

Ręcznie (Debian):

```
sudo apt install meson ninja-build pkg-config libarchive-dev zlib1g-dev liblzma-dev \
                 libzstd-dev liblz4-dev libefl-all-dev libwayland-dev wayland-protocols \
                 squashfs-tools labwc
meson setup build && ninja -C build && meson test -C build
sudo ninja -C build install        # do /opt/nirvana (C/, Libs/)
sudo ./setup-env.sh --system
./setup-env.sh --user
```

## Znane ograniczenia

* **DwarFS**: libdwarfs nie ma w pakietach Devuana ani Debiana; `install.sh` ją buduje
  (raz, od kilku minut do pół godziny). Bez niej (`--no-dwarfs`) AppImage z DwarFS
  zgłaszają „nieobsługiwane”.
* **Globalne menu pod Waylandem**: `com.canonical.AppMenu.Registrar` rozpoznaje okna po
  identyfikatorach okien X11. Pod Waylandem działa dla programów X11 (przez XWayland);
  natywne programy Waylanda nie mają takich identyfikatorów, a kompozytory wlroots nie
  mają jeszcze wspólnego protokołu, więc ich menu zostają w oknach.
* Obrazy SquashFS kompresowane LZO nie są czytane.

## Licencje

`libnirvana` (`src/lib/`, `include/`): LGPL-3.0-or-later; reszta: GPL-3.0-or-later.
Pliki protokołów Waylanda w `protocols/` mają licencje swoich autorów (MIT).
