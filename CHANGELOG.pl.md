# Lista zmian

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
