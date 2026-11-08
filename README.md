# LisaOS

Lekkie środowisko graficzne dla Linuksa (baza: minimalny Devuan) w duchu
**Mac OS 9 (Platinum)** z nutą **Amigi**. Napisane w C++ i FLTK 1.4.

- **Tryb prosty**: użytkownik widzi tylko swój Dom (`$HOME`), jak w Androidzie.
  Aplikacje to pliki AppImage leżące w `~/Aplikacje`; instalacja = przeciągnięcie pliku.
- **Tryb root**: pełny system plików i ustawienia zaawansowane, uruchamiany przez
  `pkexec`, wyraźnie oznaczony (czerwony chip w logo, plakietka ROOT).
- **Viewer, przestrzenny menedżer plików** (komponent `lisaos-denise`): jeden folder = jedno okno, które pamięta swoje położenie.

## Komponenty

| Pakiet              | Rola                                   | Stan        |
|---------------------|----------------------------------------|-------------|
| `lisaos-platinum`   | biblioteka wyglądu: kolory, ramki, ikony, chrome okien | prototyp |
| `lisaos-denise`     | Viewer: menedżer plików + biurko        | prototyp    |
| `lisaos-agnus`      | menedżer okien X11 (chrome z libplatinum) | planowany |
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

Najwygodniej w zagnieżdżonym serwerze X:

    Xephyr :2 -screen 1024x768 &
    DISPLAY=:2 ./build/lisaos-denise

Opcje: `--root`, `--home KATALOG`, `--size 1024x768`.
Czcionkę systemową w stylu Chicago można podać przez `LISAOS_SYSTEM_FONT`
(np. wolny font ChicagoFLF).

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
