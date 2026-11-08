# LisaOS

Lekkie środowisko graficzne dla Linuksa (baza: minimalny Devuan) w duchu
**Mac OS 9 (Platinum)** z nutą **Amigi**. Napisane w C++ i FLTK 1.4.

- **Tryb prosty**: użytkownik widzi tylko swój dysk LisaOS HD (czyli `$HOME`), jak w Androidzie.
  Aplikacje to pliki AppImage leżące w `~/Aplikacje`; instalacja = przeciągnięcie pliku.
- **Tryb root**: pełny system plików i ustawienia zaawansowane, uruchamiany przez
  `pkexec`, wyraźnie oznaczony (czerwony chip w logo, plakietka ROOT).
- **Rambler, przestrzenny menedżer plików** (komponent `lisaos-denise`): jeden folder = jedno okno, które pamięta swoje położenie.

## Komponenty

| Pakiet              | Rola                                   | Stan        |
|---------------------|----------------------------------------|-------------|
| `lisaos-platinum`   | biblioteka wyglądu: kolory, ramki, ikony, chrome okien | prototyp |
| `lisaos-denise`     | Rambler: menedżer plików + biurko        | prototyp    |
| `lisaos-insight`      | menedżer okien X11 (Xorg/XLibre), ramki Platinum | działa (0.1) |
| `lisaos-gary`       | przełącznik aplikacji (na razie w procesie Ramblera) | działa (0.1) |
| `lisaos-alice`      | panele sterowania                       | planowany   |
| `lisaos-paula`      | dźwięk                                  | planowany   |
| `lisaos-kickstart`  | logowanie i start sesji                 | planowany   |
| `lisaos-guru`       | okno błędów, tryb root                  | planowany   |

## Budowanie

Zależności (Devuan/Debian):

    sudo apt install build-essential cmake git libx11-dev libxft-dev \
        libxinerama-dev libxcursor-dev libxfixes-dev libxrender-dev \
        squashfs-tools bubblewrap

Jeśli FLTK 1.4 nie jest zainstalowany, CMake sam pobierze i zbuduje wersję 1.4.4:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j

Z własnym FLTK: `-DFLTK_DIR=/ścieżka/do/share/fltk`.

## Uruchamianie

**Pełna sesja** (insight + biurko Ramblera), np. w zagnieżdżonym serwerze X:

    Xephyr :2 -screen 1024x768 &
    DISPLAY=:2 ./session/lisaos-session

Na Devuanie po `sudo cmake --install build`:

    startx /usr/local/bin/lisaos-session

albo wybierz „LisaOS” na ekranie logowania (plik `share/xsessions/lisaos.desktop`).

**Sam Rambler pod innym pulpitem** (np. GNOME na Rocky/RHEL):

    ./build/lisaos-denise --center                  # cały obszar pod paskiem GNOME
    ./build/lisaos-denise --center --size 1024x768  # okno 1024x768 na środku

`--center` (albo zmienna `LISAOS_CENTER=1`) stawia biurko na środku obszaru roboczego,
czyli z ominięciem pasków pulpitu, więc górny pasek GNOME nie zasłania paska menu LisaOS.
Bez `--size` biurko wypełnia cały obszar roboczy. Pozycje okien folderów są zapisywane
względem biurka, więc przesuwają się razem z nim.

Opcje Ramblera: `--root`, `--home KATALOG`, `--size 1024x768`, `--center`.
Czcionkę systemową w stylu Chicago można podać przez `LISAOS_SYSTEM_FONT`
(np. wolny font ChicagoFLF).

## Insight: obsługa okien

- pasek tytułu: przeciąganie przesuwa okno, podwójne kliknięcie je zwija
- przyciski: zamknij (z lewej, uprzejmie przez `WM_DELETE_WINDOW`), zwiń i zoom (z prawej)
- prawa i dolna krawędź ramki oraz prawy dolny róg: zmiana rozmiaru
- klik w okno aplikacji aktywuje je i wyciąga na wierzch; klik w biurko dezaktywuje okna,
  a klik w pasek menu nie zmienia aktywnego okna (menu działa na nie)
- foldery i dialogi Ramblera to zwykłe okna X, więc zachowują się jak każda aplikacja
- okna nie wchodzą pod pasek menu; biurko Ramblera zawsze leży na dole
- bez kompozytora: aplikacje rysują prosto na ekran (mniej pamięci)

## Aplikacje (AppImage)

- **Ikony i nazwy**: Rambler czyta ikonę (`.DirIcon`, PNG lub SVG) prosto z obrazu
  squashfs wewnątrz AppImage, **bez uruchamiania go**. Offset obrazu liczy z nagłówka
  ELF i używa `unsquashfs`. Nazwa jest oczyszczana: `Firefox-128.0-x86_64.AppImage` → „Firefox”.
  Ikony trafiają do `~/.cache/lisaos/ikony`.
- **Instalacja**: otwarcie AppImage spoza folderu Aplikacje (np. z Pobranych) pyta,
  czy zainstalować. Plik trafia do `~/Aplikacje`, a jego zawartość jest rozpakowywana
  do `~/.local/share/lisaos/aplikacje/`. Bez FUSE i bez wykonywania runtime'u AppImage.
- **Uruchamianie w piaskownicy** (bubblewrap): cały system tylko do odczytu, zapis
  wyłącznie w LisaOS HD, inni użytkownicy, `/root`, `/mnt` i `/media` niewidoczne,
  własny `/tmp` i własna przestrzeń procesów; sama aplikacja jest tylko do odczytu.
  Bez bubblewrap aplikacja startuje normalnie, a dialog instalacji o tym ostrzega.
- **Różne runtime'y**: obraz squashfs jest szukany za nagłówkiem ELF (z weryfikacją
  superbloku), więc działa też z runtime'ami, które dodają wyrównanie. Gdy w środku
  nie ma squashfs (np. DwarFS w „uruntime”), AppImage rozpakowuje się sam przez
  `--appimage-extract`, ale w piaskownicy bez sieci i z zapisem tylko do katalogu
  tymczasowego. Ikona takiej aplikacji pojawia się po instalacji.
- Testowe AppImage: `tools/testowe-appimage.sh KATALOG`. „Zegar” zapisuje
  `~/piaskownica.txt` z tym, co widzi z wnętrza piaskownicy.

## Animacje okien

Jak w klasycznym Mac OS: przy otwieraniu, zamykaniu, zwijaniu i powiększaniu
okien po ekranie przelatują kontury prostokątów („zoom rects”). Foldery w Ramblerze
wylatują z ikony, z której je otwarto, i wracają do niej przy zamknięciu.
Sposób rysowania dobiera się sam:
- **XOR na ekranie**: w sesji LisaOS na Xorg/XLibre bez kompozytora. Najlżejszy.
- **Okno wycięte w kształt konturów** (rozszerzenie X SHAPE): pod XWaylandem
  (np. GNOME na Waylandzie w Rocky/RHEL 10) i na pulpitach z kompozytorem, gdzie
  rysunek na głównym oknie X byłby niewidoczny albo zamalowany.

![Animacje](docs/img/animacje.gif)

- `LISAOS_ANIMATIONS=0`: wyłącza animacje
- `LISAOS_ANIM_MS=200`: wolniejsze klatki (do testów; domyślnie 12 ms)
- `LISAOS_ANIM_NOGRAB=1`: bez blokady serwera X na czas animacji (do zrzutów ekranu)
- `LISAOS_ANIM_MODE=xor` lub `ksztalt`: wymusza sposób rysowania
- Jeśli FLTK zbudowano z backendem Wayland, animacje działają tylko w trybie X11:
  uruchom z `FLTK_BACKEND=x11`.

## Przełącznik aplikacji (gary)

Prawy górny róg paska menu pokazuje ikonę i nazwę aplikacji na pierwszym planie,
jak w Mac OS 9. Po kliknięciu:

- lista uruchomionych aplikacji z ikonami; wybór wyciąga wszystkie jej okna na wierzch,
  ukryte aplikacje mają wyszarzoną ikonę,
- **Ukryj …**, **Ukryj inne**, **Pokaż wszystkie**, **Zakończ …**,
- podmenu **Okna Ramblera**: otwarte foldery, zwiń/rozwiń/zamknij wszystkie.

Aplikacja to grupa okien o tej samej klasie `WM_CLASS`. AppImage uruchomione z Ramblera
pojawiają się pod swoją nazwą i ikoną (np. „Zegar” zamiast „XClock”); przypisanie jest
zapamiętywane w `~/.config/lisaos/aplikacje.klasy`.

**Alt+Tab** (Insight): przytrzymaj Alt i naciskaj Tab, żeby przechodzić po aplikacjach
od ostatnio używanej; Shift cofa, Escape przerywa, puszczenie Alt wybiera.

Działa na standardzie EWMH (`_NET_CLIENT_LIST`, `_NET_ACTIVE_WINDOW`, `WM_CHANGE_STATE`,
`_NET_CLOSE_WINDOW`), więc także pod GNOME, KDE czy Openboksem. Pod obcym menedżerem
okno biurka nie przyjmuje fokusu, żeby klik w pasek menu nie zmieniał aktywnej aplikacji.

## Rambler: praca z plikami

- **Otwieranie**: podwójne kliknięcie lub Enter. AppImage z innego folderu proponuje instalację.
- **Zaznaczanie**: klik, Shift/Ctrl+klik dodaje lub odejmuje, ramka zaznaczania myszą, `Ctrl+A`.
- **Zmiana nazwy**: `F2` albo klik w nazwę już zaznaczonego elementu (jak w Finderze).
  Zmiana nazwy folderu aktualizuje jego otwarte okna i zapamiętane pozycje.
- **Przeciąganie**: między oknami przenosi, z `Ctrl` kopiuje; na folder w oknie wrzuca do
  środka; na ikonę Kosza wyrzuca, na LisaOS HD przenosi do katalogu domowego. Nietrafione
  upuszczenie „odlatuje” z powrotem na miejsce.
- **Z innych programów** (XDND, np. przeglądarka, inny menedżer plików): pliki są kopiowane
  do folderu, na którym je upuszczono; upuszczone na Kosz trafiają do kosza.
- **Edycja > Kopiuj / Wklej** (`Ctrl+C` / `Ctrl+V`), **Plik > Duplikuj** (`Ctrl+D`) tworzy
  „nazwa kopia”. Kopiuj zapisuje też adresy `file://` w schowku systemowym.
- **Konflikty nazw** pytają o zastąpienie; folderu nie da się wrzucić w niego samego.
- **Widok**: `Ctrl+1` ikony, `Ctrl+2` lista (zapamiętywane dla każdego okna). W liście klik
  w nagłówek sortuje po nazwie, dacie, rozmiarze lub rodzaju; drugi klik odwraca kolejność.
  Kolumny, które się nie mieszczą, są chowane.
- **Automatyczne odświeżanie** (inotify): zmiany z zewnątrz pojawiają się same, ikona kosza
  zmienia się na pełny/pusty.
- `Ctrl+W` zamyka okno, `Ctrl+Backspace` przenosi zaznaczone do kosza, `Ctrl+N` nowy folder,
  `Ctrl+R` odświeża, `Ctrl+Q` wyłącza.

Pozycje i tryb widoku okien: `~/.config/lisaos/denise.okna`.

## Licencja

Do ustalenia (proponowana: GPL-3.0-or-later; FLTK jest na LGPL z wyjątkiem
pozwalającym na linkowanie statyczne).
