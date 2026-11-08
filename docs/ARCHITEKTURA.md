# LisaOS: architektura (szkic)

## Warstwy

    Devuan (runit, elogind, polkit)  ->  X11 (Xorg/XLibre)  ->  lisaos-insight (WM)
                                                              ->  lisaos-gary (pasek menu)
                                                              ->  lisaos-denise (pliki/biurko)
                                                              ->  aplikacje AppImage

Wszystkie komponenty linkują `lisaos-platinum`, więc wygląd jest jeden.
FLTK 1.4 ma backend Wayland, dlatego przy ewentualnym przejściu na Waylanda
wymieniamy tylko `insight` (kompozytor na wlroots), a resztę zostawiamy.

## Okna: kto za co odpowiada

- **Insight** (menedżer okien) rysuje ramki Platinum, przesuwa, zwija, powiększa
  i zamyka wszystkie okna, także okna Ramblera. Aktywne okno publikuje
  w `_NET_ACTIVE_WINDOW`, a zwinięcie w `_NET_WM_STATE_SHADED`.
- **Rambler** pokazuje biurko (jedno okno typu DESKTOP z globalnym paskiem menu)
  oraz każdy folder i każdy dialog jako **osobne okno X**. Pasek menu działa na
  aktywny folder (odczyt `_NET_ACTIVE_WINDOW`); klik w pasek nie zmienia aktywnego
  okna, a skróty klawiszowe z okien folderów trafiają do paska menu.
- Pozycje okien zapisuje się jako położenie treści (bez ramki). FLTK ustawia
  `StaticGravity`, a Insight stawia ramkę tak, by treść wróciła w to samo miejsce.
- Okna o klasie `LisaOS-*` same animują otwieranie i zamykanie (z ikony do okna),
  więc Insight pomija dla nich własną animację. `LisaOS-Dialog` dostaje ramkę
  dialogu: bez przycisków i zmiany rozmiaru.
- **Pod innym menedżerem okien** (GNOME, KDE, Openbox) albo bez menedżera Rambler
  rysuje ramki Platinum sam (CSD, jak aplikacje GTK): okna są bez obcych dekoracji,
  przesuwanie za pasek tytułu i zmianę rozmiaru zleca menedżerowi przez
  `_NET_WM_MOVERESIZE` (dopiero po ruchu myszy, żeby podwójne kliknięcie zwijało okno),
  a zwijanie i zoom robi sam. Wykrycie: `_NET_SUPPORTING_WM_CHECK` → nazwa „insight”.
  Wymuszenie do testów: `LISAOS_CSD=1` lub `0`.
- Okno biurka FLTK mapuje od razu w `show()`, zanim da się nadać mu typ DESKTOP.
  Rambler wycofuje je więc zgodnie z ICCCM, czeka na potwierdzenie menedżera okien
  i pokazuje ponownie już jako biurko, żeby leżało pod oknami folderów.

## Globalny pasek menu (`gary`)

- Własne aplikacje publikują menu przez prosty protokół (propozycja: socket
  unix `$XDG_RUNTIME_DIR/lisaos/gary`, komunikaty tekstowe z drzewem menu).
- Obce aplikacje (GTK/Qt w AppImage) zachowują menu w oknie; `gary` pokazuje
  dla nich tylko menu logo i przełącznik aplikacji.

## Wygląd i schematy kolorów

Kolory libplatinum (`plat::GRAY`, `TEXT`, `CONTENT`, `SEL`, `LIGHT`, `SHADOW`…) to
**indeksy palety FLTK**, nie wartości RGB. Cztery z nich to standardowe kolory FLTK
(`FL_BACKGROUND_COLOR`, `FL_FOREGROUND_COLOR`, `FL_BACKGROUND2_COLOR`,
`FL_SELECTION_COLOR`), reszta zajmuje wolne indeksy `FL_FREE_COLOR`..`+15`.
Widget, który raz dostał `color(plat::GRAY)`, po zmianie schematu sam ma nowy kolor:
wystarczy `Fl::set_color()` i przerysowanie.

Przebieg zmiany:
1. `lisaos-alice` zapisuje `~/.config/lisaos/wyglad.conf`.
2. Ustawia właściwość `_LISAOS_WYGLAD` na oknie głównym X.
3. Rambler i Insight dostają `PropertyNotify`, wołają `plat::reload_appearance()`
   i przerysowują okna (Rambler odbudowuje też wzór biurka, Insight tło okna głównego).

Odcienie wypukłości (`LIGHT`, `MID`, `SHADOW`) i prążki paska są wyliczane z koloru okna,
chyba że schemat poda je wprost (`jasny`, `cien_sredni`, `cien`).

## Model uprawnień

- Tryb prosty: Rambler nie otworzy niczego poza `$HOME` (sprawdzanie po
  `weakly_canonical`, więc dowiązania symboliczne nie wyprowadzą poza LisaOS HD).
- AppImage są rozpakowywane przy instalacji (`unsquashfs` z offsetem z nagłówka
  ELF; runtime AppImage nigdy nie jest wykonywany) i uruchamiane przez
  `bubblewrap`: zapis tylko w `$HOME`, system tylko do odczytu, ukryte `/home`
  innych, `/root`, `/mnt`, `/media`, własny `/tmp` i PID namespace.
  Udostępniane: gniazdo X11, `XAUTHORITY`, PulseAudio/PipeWire i szyna sesji D-Bus.
  Uwaga: dostęp do serwera X oznacza, że aplikacja może podglądać inne okna -
  to ograniczenie X11 (do rozważenia: osobny serwer Xephyr lub rozszerzenie XACE).
- Tryb root: `pkexec lisaos-denise --root`, osobny proces, wyraźne oznaczenie.

## Nazewnictwo

- Wersje: kodowe nazwy Mac OS 8/9: 1.0 „Tempo”, 2.0 „Allegro”, 3.0 „Sonata”.
- Komponenty: menedżer okien Insight; pozostałe nazwane po układach Amigi (Denise, Paula, Gary, Alice).
