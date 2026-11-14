# Lista zmian

## 0.6.0 – 2026-11-14

- **Kopiowanie, przenoszenie i usuwanie w tle** (także do kosza):
  - okno postępu (element, cel, pasek w bajtach lub elementach, Abort), gdy praca trwa
    dłużej niż chwilę;
  - pytanie o nazwy, które już istnieją (Replace, Replace All, Skip, Abort);
  - nie zostają żadne niedokończone kopie;
  - Quit pyta ponownie, gdy praca jeszcze trwa.
- **Woluminy podążają za napędami**: niezamontowane nośniki wymienne widać jako
  wyszarzone woluminy, a otwarcie takiego woluminu go montuje (udisks). *Icon > Eject*
  (Super+J) odmontowuje wolumin i wyłącza napęd.
- **Information** pozwala na zmiany: uprawnienia (odczyt, zapis, wykonanie dla
  właściciela, grupy i pozostałych), komentarz pliku (w `user.comment`) i **Default Tool**
  projektu, czyli program, którym się go otwiera.
- **Widok listy**: kolumny Protection i Comment.
- **Szuflada Startup** (`~/.config/wanderer/startup`) i autostart XDG, uruchamiane raz na
  sesję.
- **Menu Tools** z narzędziami użytkownika z `~/.config/wanderer/tools`.
- *Icon > Copy* (Super+C): „Copy of NAZWA” obok oryginału.
- **Strzałki** przechodzą między ikonami (Shift rozszerza zaznaczenie, Spacja przełącza,
  Enter otwiera).
- Pliki przeciągnięte z innych programów (XDND) są przenoszone w obrębie woluminu, a na
  inny wolumin kopiowane.
- **wanderer-wm**:
  - **Super + lewy przycisk przeciąga ekran z dowolnego miejsca**;
  - kolory z GUI Settings od razu obejmują ramki i paski;
  - pełny ekran (`_NET_WM_STATE_FULLSCREEN`) i okna zmaksymalizowane;
  - brak ramek dla doków, powiadomień, ekranów startowych, podpowiedzi i okien, które
    o to proszą;
  - kroki zmiany rozmiaru i rozmiary maksymalne;
  - `_NET_WORKAREA`, `_NET_FRAME_EXTENTS`, `_NET_CLOSE_WINDOW`.
- **Poprawki**:
  - menedżer okien nie gubi już okien, które były otwarte w chwili jego startu;
  - Execute Command pamięta ostatnie polecenie.

## 0.5.0 – 2026-11-14

- **Ustawienia GUI** (*Wanderer > GUI Settings…*), z przyciskami Save, Use i Cancel. To
  preferencje Zune:
  - kolory ekranu: tło, rozjaśnienie, cień i tekst, wypełnienie, z gotowymi schematami
    Default, Warm, Cool, Green i Contrast;
  - czcionka (bezszeryfowa, szeryfowa, stałej szerokości) i jej rozmiar;
  - ramki przycisków (bevel, window bevel, semi-round, round, thick border);
  - ramki grup (thin border, bevel, grey border, rounded, round thin border).

  Use i Save od razu zmieniają wszystkie otwarte okna. Ustawienia są zapisywane w
  `~/.config/wanderer/zune.prefs`.
- **wanderer-wm** bierze kolory ekranu z ustawień GUI przy starcie.
- **Zune**: element ze standardową ramką podąża za preferencjami, więc zmiana rodzaju ramki
  działa wszędzie.

## 0.4.1 – 2026-11-14

- **Okno wyboru pliku** w stylu systemowego okna wyboru plików:
  - lista zawartości szuflady, w której szuflady mają oznaczenie „Drawer”, a pliki rozmiar;
  - pola Drawer i File;
  - przyciski OK, Volumes, Parent i Cancel;
  - dwuklik otwiera szufladę albo wybiera plik.

  Otwierają je przyciski „?” w *Preferences > Background Pictures*.
- **Zune**: klasa List (lista z suwakiem, aktywnym wierszem, obsługą klawiatury i kółka
  myszy).

## 0.4.0 – 2026-11-14

- **Obrazy tła** dla pulpitu i okien szuflad (PNG, JPEG, GIF albo BMP): kafelkowane,
  rozciągnięte albo wyśrodkowane. Kafelki przesuwają się razem z widokiem. Ustawia się je
  w *Wanderer > Preferences… > Background Pictures*.
- **Tytuł ekranu**: gdy aktywne jest okno Wanderera, pasek ekranu pokazuje wersję
  Wanderera i ilość wolnej pamięci, odświeżane co 5 sekund. Inne programy mogą ustawić ten
  tytuł właściwością `_WANDERER_SCREEN_TITLE`.

## 0.3.1 – 2026-11-14

- **Instalator buduje paczkę Debiana** (`wanderer-x11_WERSJA_ARCH.deb`, z zależnościami
  wyliczonymi z bibliotek używanych przez programy) i instaluje ją przez apt.
- Przed instalacją instalator usuwa pliki instalacji zrobionej bez paczki (starszym
  `install.sh` albo `cmake --install`). Usuwa tylko pliki, które nie należą do żadnej
  paczki.
- Programy są instalowane do `/usr/bin`.
- Nowe opcje instalatora:
  - `--package-only` tylko buduje paczkę;
  - `--uninstall` usuwa paczkę i pliki zainstalowane bez niej.

## 0.3.0 – 2026-11-14

- **Kosz** (kosz freedesktop.org, wspólny z innymi programami):
  - kosz na głównym oknie;
  - ikony upuszczone na kosz trafiają do niego;
  - *Icon > Delete…* pyta, czy przenieść do kosza, czy usunąć na zawsze;
  - *Icon > Empty Trash*.
- **Leave Out / Put Away**: ikony z szuflad można wystawić na główne okno (i z niego
  zabrać); lista jest zapisywana w `~/.config/wanderer/leftout`.
- **Find** (*Window > Find…*, Super+F): szuka plików i szuflad o nazwach pasujących do
  wzorca (`*` i `?`, bez rozróżniania wielkości liter) wewnątrz szuflady i pokazuje wyniki
  w osobnym oknie.
- *Icon > Disk Info* jest dostępne tylko dla woluminów.

## 0.2.0 – 2026-11-14

- **Ikony**: własne obrazki szuflad, dysków twardych, nośników wymiennych, płyt, RAM-dysku i
  kosza oraz plików według typu (tekst, dokument, obraz, dźwięk, film, archiwum, skrypt,
  narzędzie, dowiązanie), z ciemniejszym obrazkiem zaznaczenia.
- **Preferencje Wanderera** (*Wanderer > Preferences…*), z przyciskami Save, Use i Cancel:
  - wygląd podpisów (obrys, zwykłe albo cień);
  - liczba wierszy podpisu i długość wiersza;
  - siatka albo swobodne ułożenie i odstępy ikon;
  - pasek narzędzi i pasek stanu okien szuflad;
  - wszystkie pliki;
  - szuflady na początku.

  Ustawienia są zapisywane w `~/.config/wanderer/wanderer.prefs`.
- **Zune**: klasy Cycle i Checkmark.
- Tło pulpitu nie ma już suwaków.

## 0.1.0 – 2026-11-14

Pierwsza wersja portu Wanderera (pulpitu AROS) na X11. Port zaczyna się od zera, a
dotychczasowa Nirvana Desktop jest na gałęzi `nirvana-legacy`.

- **Zune na FLTK**:
  - pióra i domyślna paleta;
  - wszystkie wbudowane ramki;
  - układ grup (rozmiary minimalne, domyślne i maksymalne, wagi, kolumny);
  - Text, Button, String, Prop, ImageButton i Window.
- **IconList**:
  - ikony z obrysowanymi podpisami, dzielonymi na wiersze i skracanymi „ ..”;
  - siatka albo swobodne ułożenie, w wierszach lub w kolumnach;
  - sortowanie po nazwie, dacie, rozmiarze lub typie, z rozmiarem albo datą pod podpisem;
  - widok listy;
  - zaznaczanie kliknięciem, Shift+kliknięciem i lassem;
  - przesuwanie ikon i upuszczanie ich na szuflady.
- **Okna szuflad**:
  - pasek lokalizacji z gadżetami „w górę” i „szukaj”;
  - suwaki;
  - pasek stanu.
- **Wanderer**:
  - woluminy w oknie albo jako tło;
  - jedno okno na szufladę;
  - menu Wanderer, Window i Icon;
  - zapamiętywanie pozycji okien i ikon;
  - przenoszenie w obrębie dysku i kopiowanie między dyskami.
- **Narzędzia**: Execute Command, New Drawer, Rename, Information, Disk Info.
- **wanderer-wm**, nowy menedżer okien:
  - ekrany ściągane w dół za pasek tytułu, a Esc albo prawy przycisk przywraca ich
    położenie;
  - gadżet głębi ekranu, Super+M i Super+N;
  - ramki okien z gadżetami zamknięcia, zoomu, głębi i rozmiaru;
  - menu na pasku ekranu pod prawym przyciskiem.
- **Sesja**: `wanderer-session`, wpis dla menedżera logowania
  (`/usr/share/xsessions/wanderer.desktop`) i instalator (`install.sh`).
