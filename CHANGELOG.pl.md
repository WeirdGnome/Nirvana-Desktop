# Lista zmian
Wszystkie ważniejsze zmiany w Nirvana Desktop, od najnowszych. English: [CHANGELOG.md](CHANGELOG.md).

## 0.0.58 – 2026-11-12

- **Rambler nie skraca już nazw plików**: widok ikon pokazuje całą nazwę w kilku wierszach pod ikoną (wiersze siatki rosną, ile trzeba), widok listy poszerza kolumnę nazwy do najdłuższej nazwy i przewija się w bok (nagłówki kolumn idą za nim). Ikony biurka pokazują nazwę w dwóch wierszach, zaznaczona – całą.
- **Otwórz tutaj Nirvana Shell** w menu podręcznym Ramblera (na tle albo na folderze) i w menu Okno.
- **Opróżnianie Kosza z Ramblera**: Okno > Opróżnij kosz… oraz Opróżnij kosz… w menu podręcznym w Koszu. Kosz widać w adresie jako *Kosz*.
- **Przeciąganie plików**: na foldery, dyski i Kosz w drzewie Ramblera zaawansowanego (podświetlane pod kursorem) oraz **do innych programów** – po wyjściu poza okna Nirvany przeciąganie trwa jako standardowe XDND z listą `file://`.
- Zminimalizowane programy Nirvany mają na biurku własny obrazek (panele Preferencji – ikonę Preferencji, Nirvana Shell – terminal) zamiast ogólnego rombu.

## 0.0.57 – 2026-11-12

- Logo w pasku menu (menu „Nirvana”) też jest przezroczyste – wcześniej było rysowane na szarym kwadracie, widocznym na innych kolorach paska i przy otwartym menu.
- Rambler zaawansowany: **logo na prawym końcu paska przycisków**, we własnym polu (chowa się, gdy okno jest na nie za wąskie).

## 0.0.56 – 2026-11-12

- Menu kontekstowe Ramblera pokazywało **dwa razy Otwórz** (własne i pierwszą akcję typu pliku o tej samej nazwie) – teraz raz.
- Rambler zaawansowany: przycisk **Dwa panele** ma nowy obrazek (jedno okno podzielone na źródło z niebieskim nagłówkiem i cel), a przyciski paska są tak szerokie jak ich podpisy – *Kopiuj do* i *Przenieś do* już na siebie nie nachodzą.

## 0.0.55 – 2026-11-12

- Logo enso jest **przezroczyste** dookoła: tylko koło pędzla w pasku menu, w O systemie, na ekranie logowania i blokady oraz w wygaszaczach (bez granatowego kwadratu; granat wycięty z miękkimi krawędziami, bez niebieskiej obwódki).

## 0.0.54 – 2026-11-12

- **Wskaźnik zmienia się od razu wszędzie.** Wcześniej otwarte okna zachowywały stary wskaźnik, dopóki się ich nie zamknęło i nie otworzyło ponownie (FLTK, GTK i Qt trzymają utworzone wskaźniki). Teraz menedżer okien podmienia każdy już utworzony wskaźnik we wszystkich programach (XFixes), ogłasza wskaźnik programom GTK (XSETTINGS – tylko gdy nie działa inny menedżer ustawień), a każdy program Nirvany – także sam panel Wygląd – idzie za zmianą.
- Rozmiar wskaźnika *Automatyczny*: 24 px razy skala interfejsu (domysł Xcursora dawał malutki wskaźnik 16 px).
- **Retro 16 · klasyczny i · z obwódką narysowane od nowa** bliżej czerwonych wskaźników 16-bitowych komputerów domowych: pochylona strzałka z szerokim grotem i krótkim grubym ogonem; klasyczny w trzech kolorach – czerwony w środku, jasna krawędź u góry i z lewej, czarna u dołu i z prawej, bez obwódki, piksele podwojone przy 24 i 32 px; z obwódką to samo gładko, z cienką czarną linią i cieniem. Szklany bez zmian.

## 0.0.53 – 2026-11-12

- **Dźwięk opróżniania Kosza:** papier zgniatany w kulkę i wrzucany do kosza, w duchu pulpitów z końca lat 90. – własny Nirvany, syntetyzowany (nic nagranego ani skopiowanego). `~/.local/share/nirvana/sounds/trash-empty.wav` (albo .oga/.ogg) go zastępuje, pusty plik go wyłącza.
- **Dyski w Ramblerze:** drzewo Ramblera zaawansowanego pokazuje wszystkie dyski – partycje, pendrive’y, karty, płyty – zamontowane i nie, z własnymi ikonami (i Kosz pusty albo pełny). Klik na niezamontowanym dysku montuje go (zaszyfrowany pyta o hasło) i pokazuje; drzewo zmienia się po podłączeniu i odłączeniu dysku. Adres też nazywa dyski: `PENDRIVE/Zdjecia`.

## 0.0.52 – 2026-11-12

- **NirvanaSHIELD pyta o zablokowane zapytania o nazwy.** Program bez pozwolenia, który tylko szukał nazwy (DNS) – np. `git clone` / `cmake` pobierający FLTK w terminalu – kończył się błędem „Could not resolve host” i nikt nie pytał: odrzucony pakiet UDP zawodzi od razu, a gniazdo programu znikało, zanim usługa zdążyła sprawdzić, czyje było. Teraz te zapytania trafiają do usługi (`127.0.0.1:5399`, `::1`), która znajduje program, póki czeka, pyta „Czy X może korzystać z internetu?” z nazwą, której szukał, i odpowiada „odmowa” (program dalej kończy się od razu, jak wcześniej).

## 0.0.51 – 2026-11-12

- **Czerwone wskaźniki myszy Retro 16**, własne Nirvany (każdy kształt rysuje `tools/cursors/make_cursors.py`): *klasyczny* (piksele epoki 16-bit – pochylona czerwona strzałka z czarną krawędzią i jasnym połyskiem, powiększana o całe piksele), *z obwódką* (czerwony z białą obwódką i cieniem) i *szklany* (szklisty czerwony). Wszystkie wskaźniki potrzebne pulpitowi – strzałka, zegar oczekiwania (animowany), strzałka z zegarem, tekst, rączka, celownik, przesuwanie, cztery strzałki zmiany rozmiaru, zakaz, pomoc, kopiowanie, skrót – pod nazwami, o które pytają X11, GTK i Qt. Instalowane do `share/icons`.
- **Preferencje > Wygląd > Wskaźnik myszy:** wskaźnik systemowy, Retro 16 albo każdy zainstalowany motyw Xcursor, rozmiar Automatyczny albo 16–64 px, z podglądem strzałki, rączki i zegara. Zmienia się od razu nad biurkiem i oknami Nirvany; sesja przekazuje go innym programom (`XCURSOR_THEME`, `XCURSOR_SIZE`, `XCURSOR_PATH`). O systemie pokazuje jego nazwę.
- **Nowe logo:** enso – jedno czerwone pociągnięcie pędzla w kole, w czerwono-białą szachownicę, na nocnym granacie – w pasku menu, O systemie, na ekranie logowania i blokady oraz w wygaszaczach (wbudowane jako pliki PNG w kilku rozmiarach; `tools/logo/make_logo.py`). Wygaszacz z latającymi logami nazywa się teraz *Latające enso*.

## 0.0.50 – 2026-11-12

- **Poprawione: zamknięcie okna folderu resetowało pulpit.** Timer paska stanu eksploratora działał dalej po zwolnieniu okna i wywracał Ramblera, a sesja uruchamiała go od nowa. Teraz timery zatrzymują się razem z oknem.
- **Klasyczne okna 16-bit da się wyłączyć przy schemacie Retro 16.** Taki schemat wymuszał gadżety, a przełącznik w Preferencje > Pulpit nic nie robił. Teraz decyduje tylko przełącznik; wybór schematu Retro 16 w Wyglądzie go włącza, każdy inny schemat wyłącza.
- Folder domowy jest wszędzie pokazany jako **Nirvana HD** – pole Adres, nagłówki paneli, pasek stanu, Znajdź (`Nirvana HD/Muzyka`, nie `~/Muzyka`); wpisać można jedno i drugie.
- Style okien folderów nazywają się teraz **Rambler uproszczony** i **Rambler zaawansowany** (Preferencje > Pulpit > Okna folderów).

## 0.0.49 – 2026-11-12

Eksplorator Ramblera dorasta:
- **Zapisywalne archiwa:** zip, tar(.gz/.bz2/.xz), 7z i cpio otwierane przez `archivemount` – można do nich kopiować, zmieniać nazwy, usuwać, edytować; gdy żadne okno nie pokazuje archiwum, jest pakowane na nowo w tle we własnym formacie (tylko gdy coś się zmieniło). Inne formaty zostają kopią tylko do odczytu.
- **Serwery jak dyski:** SFTP, FTP i teraz **SMB**; połączony serwer to ikona dysku na biurku (Otwórz, Rozłącz), adresy `sftp://` `ftp://` `smb://` w polu Adres, *Łącz przy każdym starcie* (bez hasła – pytanie o nie, gdy trzeba).
- **Rozpoznawanie zawartości:** rodzaj pliku wynika z pierwszych bajtów (PNG, JPEG, ZIP, ELF, MP3… nawet bez rozszerzenia albo z błędnym); typy plików mogą pasować po zawartości (`image/*`); na serwerach decyduje nazwa (bez czytania przez sieć).
- **Zadania w tle:** kopiowanie, przenoszenie, usuwanie, Kosz i zapis archiwów we własnych wątkach, obok siebie; **Okno > Zadania…** z postępem, prędkością, pozostałym czasem, Wstrzymaj/Wznów, Anuluj; nie zabiera klawiatury, gdy otwiera się samo.
- **Zdarzenia dla typów plików:** Dwuklik, Shift + dwuklik i Spacja (podgląd) z własnymi poleceniami albo `@preview`.
- **Metadane:** panel *Informacje* z miniaturą, wymiarami obrazu i aparatem z EXIF, tagami MP3/FLAC/Ogg i długością, stronami PDF, architekturą programu, wierszami tekstu; filmy i zawartość archiwów w tle. **Spacja** = szybki podgląd.
- **Stan sesji:** foldery obu paneli, historia, zaznaczenie, lista/ikony, filtry, drzewo, panel i dwa panele wracają po zalogowaniu.
- **Filtr i Znajdź:** pole *Filtr* (`*.jpg`, `rodzaj:dźwięk`, `kind:picture`…) i **Okno > Znajdź…** (`Ctrl+F`): nazwa, rodzaj z zawartości, tekst w środku, znaczniki, rozmiar, data – w tle.
- Insight: okno, które nie chce klawiatury (`WM_HINTS` input = false, postęp zadań Ramblera), pojawia się bez zabierania fokusu.
- Poprawki: względny adres w polu Adres; przy starcie jedno okno na każde zapisane okno eksploratora.
- Paczka poleca `archivemount`, sugeruje `ffmpeg`.

## 0.0.48 – 2026-11-12

- **Rambler jako eksplorator** (Preferencje > Pulpit > Okna folderów): pasek dużych płaskich przycisków z podpisami, pole Adres, drzewo folderów, jeden albo dwa panele (ŹRÓDŁO/CEL: F5 kopiuj, F6 przenieś, F7 nowy folder, F8 usuń, Tab zamiana, przeciąganie między nimi) i pasek stanu. Domyślnie zostaje zwykły Rambler.
- **Typy plików** z akcjami (Narzędzia > Typy plików…): akcje dwukliku i menu kontekstowego ze wstawkami `{f} {F} {s} {d} {n}`, `@browse`, `@wallpaper`, `@shell`; wbudowane typy dla archiwów, skryptów, tekstu, zdjęć, pakietów.
- **Archiwa otwierają się jak foldery** (bsdtar), Rozpakuj tutaj / do drugiego panelu, Ikony > Spakuj do archiwum….
- **Serwery:** Okno > Połącz z serwerem… – SFTP (sshfs) i FTP (gio/gvfs) widoczne jak foldery, w drzewie i w obu panelach.
- Paczka poleca `libarchive-tools` i `sshfs`, sugeruje `gvfs-backends` i `gvfs-fuse`.

## 0.0.47 – 2026-11-12

- Ikona apletu NirvanaSHIELD to jednokolorowa tarcza w kolorze tekstu paska menu, jak pozostałe aplety: wypełniona z ptaszkiem (chroni), wypełniona z kreską (cały internet zablokowany), kontur (ochrona wyłączona), przygaszony kropkowany kontur (usługa nie działa).

## 0.0.46 – 2026-11-12

- **NirvanaSHIELD:** zapora, która wpuszcza do internetu tylko programy, którym pozwolisz – usługa `nirvana-shield` (nftables + cgroup net_cls + zdarzenia procesów z kernela), Preferencje > NirvanaSHIELD z dużym przełącznikiem przy każdym programie i trzema dużymi trybami (Chroń, Zablokuj cały internet, Wyłącz ochronę), tarcza na pasku menu i powiadomienie z Zezwól/Nie, gdy program pyta. Hasło rodzica i zawsze dozwolona sieć domowa.
- Własne powiadomienia Nirvany mogą mieć przyciski obsługiwane w Ramblerze.
- Paczka .deb poleca `nftables`.

## 0.0.45 – 2026-11-12

- **Wykonaj polecenie** pamięta historię (`~/.config/nirvana/run-history`, ostatnie 50) i podpowiada podczas pisania: pasujące wpisy z historii, programy z `$PATH` (np. /usr/bin) i AppImage użytkownika (z wersjami). ↑↓ wybór, Tab uzupełnia, Enter albo dwuklik uruchamia.

## 0.0.44 – 2026-11-12

- **Schematy Retro 16:** *1.3*, *2.1*, *3.1*, *3.9* i *4.1* – kolory pulpitów 16-bitowych komputerów domowych z pięciu epok, z klasycznymi gadżetami okien 16-bit.
- Schematy mogą włączać klasyczne gadżety (`window_style=classic`); klasyczne paski tytułu przyjmują jednolity kolor, gradient albo szkło ze schematu.

## 0.0.43 – 2026-11-12

- **Powiadomienia:** Rambler jest serwerem powiadomień (`org.freedesktop.Notifications`): okienka w prawym górnym rogu w bieżącym wyglądzie z obrazkiem programu, tekstem i przyciskami akcji, pilne z czerwonym paskiem; dźwięk (własny powiadomienia, z motywu albo wbudowany dzwonek Nirvany).
- **Aplet powiadomień** (dzwonek): kropka nieprzeczytanych, ostatnie powiadomienia, Nie przeszkadzać, Dźwięk powiadomień, Wyczyść.
- Sesja nie uruchamia już innych programów powiadomień z autostartu (`NIRVANA_NOTIFICATIONS=0` je zostawia).

## 0.0.42 – 2026-11-12

- Poprawka: kółko myszy zmieniało głośność w dowolnym miejscu pulpitu – teraz tylko nad ikoną głośności (FLTK oddaje niewykorzystane kółko wszystkim widżetom okna). Ta sama poprawka dla ikon zasobnika i kalendarza.

## 0.0.41 – 2026-11-12

- **Historia schowka:** aplet na pasku menu pamięta ostatnie 25 skopiowanych tekstów (tylko w pamięci); klik albo Ctrl+Alt+V je pokazuje, wybranie wpisu wkłada go z powrotem do schowka; Wstrzymaj zapisywanie i Wyczyść historię.

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
