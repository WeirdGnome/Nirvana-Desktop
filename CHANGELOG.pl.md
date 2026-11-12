# Lista zmian
Wszystkie ważniejsze zmiany w Nirvana Desktop, od najnowszych. English: [CHANGELOG.md](CHANGELOG.md).

## 0.0.40 – 2026-11-12

- Insight szanuje okna o stałym rozmiarze (ten sam minimalny i maksymalny rozmiar w `WM_NORMAL_HINTS`, np. Preferencje i ich panele): bez przycisku zoom, bez zmiany rozmiaru krawędziami i rogiem, prośby o maksymalizację są pomijane; w klasycznych oknach 16-bit bez gadżetu powiększenia i rozmiaru.

## 0.0.39 – 2026-11-12

- **Bluetooth:** aplet na pasku menu (włącz/wyłącz, sparowane urządzenia z łączeniem/rozłączaniem i poziomem baterii, Dodaj urządzenie…) i Preferencje > Bluetooth (wyszukiwanie, parowanie z potwierdzeniem kodu albo PIN-em, łączenie, rozłączanie, zapominanie, widoczność). Działa przez `bluetoothctl` (bluez); czytelne komunikaty dla typowych błędów BlueZ.
- Paczka .deb poleca `bluez` i `rfkill`.
- Od teraz CHANGELOG, po angielsku i po polsku.

## 0.0.38 – 2026-11-12

- Preferencje > Pulpit > Zachowanie okien: **Klasyczne okna 16-bit** – gadżet zamykania po lewej, powiększenia i głębi po prawej, gadżet rozmiaru w rogu, kliknięcie aktywuje bez wysuwania na wierzch, gadżet głębi chowa okno z wierzchu pod spód albo wysuwa inne na wierzch.
- Opcjonalne przesuwanie i zmiana rozmiaru konturem XOR.

## 0.0.37 – 2026-11-11

- Narzędzia > Zabij okno… (`Ctrl+Alt+K`, jak xkill, okna Nirvany chronione).

## 0.0.36 – 2026-11-11

- nirvana-pkexec: okno hasła Nirvany działa też bez sesji logind (zmiana zdjęcia logowania znów działa).

## 0.0.35 – 2026-11-11

- Przełącznik okien Alt+Tab z podglądami.

## 0.0.34 – 2026-11-11

- Osiem nowych wygaszaczy ekranu.

## 0.0.33 – 2026-11-11

- Pasek menu pokazuje osobno pamięć shadera.

## 0.0.32 – 2026-11-11

- Nowe logo i tapeta Nirvana Desktop.
- Okna błędów z tytułem „Guru Meditation”.
- AppImage: sprawdzanie architektury procesora przed instalacją i uruchomieniem.

## 0.0.31 – 2026-11-11

- Aplet aktualizacji: tylko pakiety systemowe (bez Flatpaka).

## 0.0.30 – 2026-11-11

- Natywny wygaszacz i blokada ekranu (nirvana-lock, PAM) oraz Preferencje > Wygaszacz ekranu.

## 0.0.29 – 2026-11-11

- Nirvana-DM: wpisywanie nie zamazuje już wpisanych liter.

## 0.0.28 – 2026-11-11

- Paczka .deb poleca biblioteki xcb potrzebne AppImage'om z Qt 6.

## 0.0.27 – 2026-11-11

- AppImage: wykrywanie brakujących bibliotek systemowych (nagłówki ELF) i instalacja jednym kliknięciem.

## 0.0.26 – 2026-11-11

- Przyciski w okienkach tak szerokie jak ich etykiety.

## 0.0.25 – 2026-11-11

- Okno wyboru plików Nirvany, także dla programów GTK i Qt przez xdg-desktop-portal.
- Agent polkit Nirvany (okno hasła).

## 0.0.24 – 2026-11-11

- Preferencje > Użytkownik: zdjęcie logowania, imię i nazwisko, hasło, automatyczne logowanie.

## 0.0.23 – 2026-11-11

- Nirvana-DM: ekran logowania w stylu lat 90. (greeter LightDM) z menu motywu, tła i języka, które przetrwają restart.

## 0.0.22 – 2026-11-11

- Aplet drukarek i Preferencje > Drukarki z instalacją sterowników.

## 0.0.21 – 2026-11-11

- Szklane paski menu ze szklanym stylem tytułu (Glacier).

## 0.0.20 – 2026-11-11

- Schemat Glacier ze szklanymi paskami tytułu i własną tapetą.

## 0.0.19 – 2026-11-11

- Preferencje: najwyżej 5 ikon paneli w rzędzie.

## 0.0.18 – 2026-11-11

- Minimalizacja do ikony na biurku animowana w obie strony.

## 0.0.17 – 2026-11-11

- Preferencje > Pulpit: okna mogą zasłaniać kolumnę ikon i dok.

## 0.0.16 – 2026-11-11

- Pamięć samej Nirvany na pasku menu, równe odstępy ikon paska.

## 0.0.15 – 2026-11-11

- Aplet zegara z kalendarzem, pomodoro, minutnikiem i stoperem; panel Data i czas.

## 0.0.14 – 2026-11-11

- Aplet aktualizacji, ikony stanu baterii, profile zasilania bez power-profiles-daemon.

## 0.0.13 – 2026-11-11

- Pełnoekranowe shadery (Preferencje > Shader).

## 0.0.12 – 2026-11-11

- Ikony folderów kategorii, okna bez dekoracji, dziennik systemu bez roota.

## 0.0.11 – 2026-11-11

- O tym komputerze, licencja GPL-3.0 i podziękowania; bez nazw zastrzeżonych.

## 0.0.10 – 2026-11-11

- Przyciski paska tytułu w kolorach schematu, z wyraźniejszymi symbolami.

## 0.0.9 – 2026-11-11

- Przyciski paska tytułu także w oknach nieaktywnych.

## 0.0.8 – 2026-11-11

- Żółte etykiety zminimalizowanych okien; cichszy apt w instalatorze.

## 0.0.7 – 2026-11-11

- Domyślnie wszystko po angielsku; kolorowe przyciski tytułu; ikony Aplikacji i Nirvana Shell.

## 0.0.6 – 2026-11-11

- Aplikacje w ~/.nirvana-apps, ikony Aplikacje i Preferencje na biurku, menu kontekstowe, poprawki awarii.

## 0.0.5 – 2026-11-11

- Usunięty tryb root.

## 0.0.4 – 2026-11-11

- Ikony zikonizowanych okien znanych aplikacji.

## 0.0.3 – 2026-11-11

- Pierwsza numerowana wersja: menedżer okien Insight, pulpit i menedżer plików Rambler, aplety, panele ustawień, Nirvana Shell, angielski i polski, instalator dla Devuana 6.
