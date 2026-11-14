# Lista zmian

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
