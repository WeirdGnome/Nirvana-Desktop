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
| `lisaos-gary`       | globalny pasek menu, przełącznik aplikacji | planowany |
| `lisaos-alice`      | panele sterowania                       | planowany   |
| `lisaos-paula`      | dźwięk                                  | planowany   |
| `lisaos-kickstart`  | logowanie i start sesji                 | planowany   |
| `lisaos-guru`       | okno błędów, tryb root                  | planowany   |

## Budowanie

Zależności (Devuan/Debian):

    sudo apt install build-essential cmake git libx11-dev libxft-dev \
        libxinerama-dev libxcursor-dev libxfixes-dev libxrender-dev

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

**Sam Rambler** (bez menedżera okien, np. pod GNOME): `./build/lisaos-denise --size 1024x768`.

Opcje Ramblera: `--root`, `--home KATALOG`, `--size 1024x768`.
Czcionkę systemową w stylu Chicago można podać przez `LISAOS_SYSTEM_FONT`
(np. wolny font ChicagoFLF).

## Insight: obsługa okien

- pasek tytułu: przeciąganie przesuwa okno, podwójne kliknięcie je zwija
- przyciski: zamknij (z lewej, uprzejmie przez `WM_DELETE_WINDOW`), zwiń i zoom (z prawej)
- prawa i dolna krawędź ramki oraz prawy dolny róg: zmiana rozmiaru
- klik w okno aplikacji aktywuje je i wyciąga na wierzch; klik w biurko dezaktywuje okna
- okna nie wchodzą pod pasek menu; biurko Ramblera zawsze leży na dole
- bez kompozytora: aplikacje rysują prosto na ekran (mniej pamięci)

## Animacje okien

Jak w klasycznym Mac OS: przy otwieraniu, zamykaniu, zwijaniu i powiększaniu
okien po ekranie przelatują kontury prostokątów („zoom rects”). Foldery w Ramblerze
wylatują z ikony, z której je otwarto, i wracają do niej przy zamknięciu.
Kontury są rysowane metodą XOR prosto na ekranie, bez kompozytora.

![Animacje](docs/img/animacje.gif)

- `LISAOS_ANIMATIONS=0`: wyłącza animacje
- `LISAOS_ANIM_MS=200`: wolniejsze klatki (do testów; domyślnie 12 ms)
- `LISAOS_ANIM_NOGRAB=1`: bez blokady serwera X na czas animacji (do zrzutów ekranu)

## Obsługa prototypu

- podwójne kliknięcie: otwiera folder / uruchamia AppImage / `xdg-open` dla plików
- podwójne kliknięcie w pasek tytułu albo przycisk z kreskami: zwinięcie okna (windowshade)
- przycisk z kwadratem: zoom, prawy dolny róg: zmiana rozmiaru
- `Ctrl+N` nowy folder, `Ctrl+O` otwórz, `Ctrl+W` zamknij okno,
  `Ctrl+Backspace` do kosza (kosz zgodny z freedesktop.org), `Ctrl+R` odśwież, `Ctrl+Q` wyłącz

Pozycje okien: `~/.config/lisaos/denise.okna`.

## Licencja

Do ustalenia (proponowana: GPL-3.0-or-later; FLTK jest na LGPL z wyjątkiem
pozwalającym na linkowanie statyczne).
