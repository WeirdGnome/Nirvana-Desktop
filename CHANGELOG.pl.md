# Lista zmian

## 0.17.0 (w przygotowaniu)

### Nowe
- **Preferencje Printer** (`rambler --prefs printer`, w szufladzie Prefs): drukarki CUPS
  z ich stanem, opisem, położeniem i urządzeniem; drukarka domyślna; strona próbna; dla
  każdej drukarki rozmiar, rodzaj i podajnik papieru, orientacja, druk dwustronny, kolor,
  rozdzielczość i jakość - tak, jak oferuje je sterownik. Ustawienia są własne
  użytkownika (`lpoptions`), więc system nie pyta o uprawnienia. *Drukarki...* otwiera
  systemowe narzędzie do drukarek (albo strony WWW CUPS), by je skonfigurować.
- **Clock** (`rambler --utility clock`, w szufladzie Tools): czas na tarczy analogowej
  albo cyfrowej, data w tytule okna. Menu Ustawienia: sekundy, data, zegar 24-godzinny,
  budzik (daje sygnał tak, jak ustawiają go preferencje Sound, i o tym informuje),
  Zapisz ustawienia.
- Lune: listę pozycji Cycle można ustawić ponownie.
- Lune: **menu okien** (Menustrip, Menu, Menuitem): menu okna Lune pokazują się w pasku
  ekranu tak jak menu Ramblera, z pozycjami zaznaczanymi, grupami wykluczającymi i
  skrótami Super+klawisz.
- Lune: klasa Clock może nie pokazywać wskazówki sekundowej.

### Poprawione
- Menedżer okien: tytuł ustawiony przez program tuż po otwarciu okna nie był widoczny
  (był czytany, zanim menedżer zaczął śledzić zmiany okna).
- Menedżer okien: okno dialogowe wyśrodkowane nad wąskim oknem nie wychodzi już
  częściowo poza ekran.
- Preferencje Sound: etykieta głośności kolidowała w katalogu polskim z „Wolumin:”.

## 0.16.0 – 2026-11-15

### Nowe
- **Preferencje Time** (`rambler --prefs time`, w szufladzie Prefs): data w kalendarzu
  (miesiąc, rok z - i +), czas na zegarze ze wskazówkami, który chodzi, dopóki czas nie
  jest ustawiany (godziny, minuty, sekundy, - i + dla pola, w którym się pisze). Use
  ustawia zegar systemu, Save także własny zegar komputera, przez `timedatectl` (a bez
  niego `date` i `hwclock` przez `pkexec`); system pyta o uprawnienia. Z `timedatectl`
  czas może być ustawiany samoczynnie z sieci (NTP).
- **Preferencje Sound** (`rambler --prefs sound`, w szufladzie Prefs): co robi sygnał -
  ekran miga, słychać dźwięk albo jedno i drugie. Dźwięk to krótki ton (głośność,
  wysokość i długość) albo próbka dźwięku (plik), z przyciskiem Próba. Z PulseAudio albo
  PipeWire (`pactl`) także wyjście dźwięku: urządzenie, głośność główna i wyciszenie.
  Menedżer okien przyjmuje zmianę od razu i odtwarza dźwięk przez `paplay` (albo `aplay`).
- Lune: klasy **Calendar** i **Clock**.

### Zmienione
- Lune: wyłączony obiekt jest pokryty kropkami w kolorze cienia, jak w oryginale; dotąd
  tylko przestawał przyjmować wejście.

## 0.15.0 – 2026-11-15

### Nowe
- **Preferencje IControl** (`rambler --prefs icontrol`, w szufladzie Prefs): ustawienia
  menedżera okien, jak ustawienia Intuition w oryginale. Ekrany: nowe okna na ekranie z
  przodu albo na pierwszym, czy ekran można ściągnąć do samej dolnej krawędzi, klawisz
  (Super, Alt albo Ctrl) do przeciągania ekranu z dowolnego miejsca. Menu: rozwijane z
  paska ekranu albo **przy wskaźniku** (tytuły menu w okienku przy wskaźniku, każdy
  otwiera się obok; pasek ekranu może nadal je rozwijać), wygląd klasyczny albo 3D, menu
  lepkie (krótkie kliknięcie przyciskiem menu zostawia je
  otwarte; kliknięcie wybiera pozycję albo je zamyka). Okna: czy można je przesuwać
  częściowo poza ekran. Menedżer okien przyjmuje zmianę od razu.

### Zmiany
- **Window > View > All Files** działa jak w oryginale: gdy nie jest zaznaczone, szuflada
  pokazuje tylko to, co ma własny plik ikony (`.info`). Domyślnie jest zaznaczone (w
  Linuksie mało plików ma ikony). Pliki ukryte (`.nazwa`) mają osobną pozycję **Hidden
  Files**, a preferencje Ramblera przełącznik „Ukryte pliki” obok domyślnego widoku.

## 0.14.0 – 2026-11-15

### Nowe
- **Ikony domyślne** (jak `ENV:Sys/def_*.info`): plik, szuflada albo dysk bez własnej ikony
  pokazuje ikonę domyślną swojego rodzaju z `~/.config/rambler/icons` (albo z sesyjnego
  `$XDG_RUNTIME_DIR/rambler/icons`, albo z `share/rambler/icons` instalacji): `def_drawer`,
  `def_tool`, `def_project`, `def_disk`, `def_harddisk`, `def_ram`, `def_trashcan`,
  `def_txt`, `def_picture`, `def_sound`, `def_movie`, `def_archive`, `def_document`,
  `def_script`, `def_cdrom`, `def_network` (pliki `.info` dowolnego rodzaju, który Rambler
  czyta, także podwójne). Bez nich zostają wbudowane rysunki.
- **Informacje zapisują do pliku ikony** (PutDiskObject): plik z własną ikoną trzyma w niej
  typy narzędzi i narzędzie domyślne, jak w oryginale. Ikona PNG jest zapisywana od nowa
  (oba obrazy ikony podwójnej zostają); ikona klasyczna – także z NewIcons albo ikoną
  kolorową – ma zmieniane tylko napisy, cała reszta zostaje bajt w bajt, także wiersze
  obrazów NewIcons.
- **Snapshot zapisuje położenie w pliku ikony** (do_CurrentX/Y), jeśli ikona go ma, więc
  idzie ono razem z plikiem; Unsnapshot je usuwa.

### Poprawki
- Snapshot ikony od wersji 0.13.0 niczego nie zapamiętywał (był od razu cofany); znów
  działa.

## 0.13.3 – 2026-11-15

### Poprawki
- **AppImage „anylinux” nie miały ikon**: zawierają obraz DwarFS zamiast squashfs (program
  startowy uruntime). Ich ikony są teraz wyjmowane przez `dwarfsextract` (sam program albo
  `dwarfs-universal` projektu DwarFS, także z `~/.local/bin`), nadal bez uruchamiania
  AppImage. Debian nie ma jeszcze paczki DwarFS: `./install.sh --with-dwarfs` pobiera
  program projektu DwarFS z jego wydań na GitHubie do `/usr/local/bin`.

### Nowe
- AppImage dodany do menu przez narzędzie pulpitu (AppImageLauncher, Gear Lever,
  appimaged) dostaje ikonę wskazaną w jego pliku `.desktop`, gdy własnej nie da się wyjąć.

## 0.13.2 – 2026-11-15

### Poprawki
- Więcej AppImage pokazuje ikony: te bez znacznika „AI” w nagłówku (rozpoznawane po nazwie
  `*.AppImage`), te, których system plików nie zaczyna się zaraz po programie (szukany po
  sygnaturze), obrazy z atrybutami rozszerzonymi (`unsquashfs -no-xattrs`; liczy się to,
  co wyjęto, niezależnie od kodu wyjścia) oraz dowiązania `.DirIcon` bezwzględne albo
  sięgające katalogu wyżej wewnątrz obrazu. Obsługiwane są też obrazki JPEG i BMP.

### Nowe
- `rambler --appimage-icon PLIK` opisuje każdy krok wyjmowania ikony AppImage, żeby
  znaleźć przyczynę, gdy jej brak.

## 0.13.1 – 2026-11-15

### Poprawki
- **Szuflada Prefs**: programy w niej nie miały ikon, pokazywały się jako „Input.desktop” i
  nie dawały się uruchomić (otwarcie dawało „sh: Lune.desktop: not found”). Otwarcie pliku
  `.desktop` uruchamia teraz jego program; szuflady pokazują plik `.desktop` pod jego
  nazwą i z jego ikoną (z motywów ikon). Programy preferencji mają własne ikony, a pliki
  z 0.13.0 są tworzone od nowa z nimi.
- **AppImage bez ikon**: gdy wyjęcie ikony się nie udało (brak `unsquashfs` albo starsza
  wersja nieznająca użytych opcji), AppImage był na stałe oznaczany jako bez ikony. Teraz
  oznaczany jest tylko AppImage naprawdę bez ikony, nieudana próba jest ponawiana (i
  opisana w dzienniku sesji), a oznaczenia z 0.13.0 są pomijane. `install.sh` mówi, gdy
  brakuje `unsquashfs`.

## 0.13.0 – 2026-11-15

### Zmiany
- **Port nazywa się teraz Rambler, a jego biblioteka interfejsu Lune**, żeby odróżnić go
  od oryginałów AROS (Wanderer i Zune). Programy to `rambler`, `rambler-wm` i
  `rambler-session`, paczka to `rambler-x11` (zastępuje `wanderer-x11`), sesja nazywa się
  „Rambler”, ustawienia są w `~/.config/rambler`, a właściwości X to `_RAMBLER_*`. Pliki i
  komentarze przeniesione z AROS nadal podają oryginalne nazwy.
- Ustawienia wcześniejszej wersji są kopiowane z `~/.config/wanderer` raz, przy pierwszym
  uruchomieniu (`zune.prefs` staje się `lune.prefs`, `wanderer.prefs` – `rambler.prefs`);
  `install.sh` usuwa paczkę `wanderer-x11` i pliki o starej nazwie.

### Nowe
- **Samodzielne programy preferencji**, jak w SYS:Prefs: szuflada Prefs ma Input, Lune i
  Rambler (`rambler --prefs lune|rambler`). Use zachowuje ustawienia na sesję
  (`$XDG_RUNTIME_DIR/rambler`, jak ENV:), Save także w `~/.config/rambler` (jak ENVARC:);
  działający Rambler od razu przyjmuje zmianę, jak IPrefs.
- Ikony przeciągnięte z szuflady na tło okna głównego zostają tam wyłożone, w miejscu
  upuszczenia (jak Leave Out; na dysku zostają w swojej szufladzie).
- **Preferencje ScreenMode** (`rambler --prefs screenmode`, w szufladzie Prefs): tryb
  wyświetlania i odświeżanie wyjścia, ekran większy niż tryb z autoprzewijaniem (panning
  xrandr) oraz Próba, która włącza tryb na dziesięć sekund i wraca do poprzedniego.
  Zapisane ustawienia są wprowadzane na początku sesji.
- **Preferencje Locale** (`rambler --prefs locale`): preferowane języki (pierwszy, który
  ma katalog, jest językiem interfejsu; programy dostają je jako `LANGUAGE`), kraj
  (formaty dat, liczb i walut: zainstalowane ustawienia regionalne) i strefa czasowa
  (`TZ`). Sesja eksportuje zapisane ustawienia; po Use dostają je programy uruchamiane
  przez Ramblera.
- **Preferencje Pointer** (`rambler --prefs pointer`): motyw i rozmiar kursora X, z
  podglądem zwykłego wskaźnika i wskaźnika zajętości z plików motywu. Trafiają do zasobów
  X, okna głównego i programów uruchamianych od tej chwili.
- **Preferencje Font** (`rambler --prefs font`): czcionka ikon, tekstu ekranu i tekstu
  systemu o stałej szerokości – dowolna czcionka systemu z rozmiarem, wybierana w nowym
  **wyborze czcionki** (Lune, jak ASL: rodziny, rozmiary, próbka). Wybór rodziny w GUI
  Settings i preferencjach Ramblera zostaje; wybranie jej tam zastępuje czcionkę z nazwy.
  Paski tytułowe okien i pasek ekranu w menedżerze okien też używają czcionki ekranu.
- **Preferencje Network** (`rambler --prefs network`), przez NetworkManagera (`nmcli`):
  Interfejsy (włączony lub nie, DHCP albo stały adres z maską, bramą i serwerami DNS),
  Komputer (nazwa komputera) i Sieci bezprzewodowe (sieci w zasięgu z sygnałem i
  zabezpieczeniami, łączenie z hasłem, ponowne szukanie). Use zmienia interfejs tylko na
  teraz, Save zmienia jego połączenie na stałe; błędne adresy są wskazywane, zanim
  cokolwiek się zmieni.
- **Pliki ikon i ikony podwójne.** Ikona pliku pochodzi z leżącego obok `nazwa.info`
  (szuflady z `szuflada.info`, dysku z `Disk.info` w jego katalogu głównym), czytanego jak
  w icon.library: ikony klasyczne (obrazy planarne w kolorach ekranu), NewIcons (obrazy w
  typach narzędzi), ikony kolorowe (FORM ICON z paletą albo ARGB) i ikony PNG. **Ikona
  podwójna** po zaznaczeniu pokazuje swój drugi obraz (SelectRender, `IM2=`, drugi blok
  IMAG / ARGB, PNG doklejony po pierwszym); ikona z jednym obrazem jest wtedy
  rozjaśniana, jak w AROS. Same pliki `.info` nie są pokazywane, a przy kopiowaniu,
  przenoszeniu, zmianie nazwy i usuwaniu idą razem ze swoim plikiem.
- **AppImage pokazują własne ikony.** Ikona (`.DirIcon`, PNG, SVG albo XPM, także przez
  dowiązanie) jest wyjmowana z systemu plików AppImage przez `unsquashfs` – program nie
  jest uruchamiany – i przechowywana w `~/.cache/rambler/appimage-icons`; bez tego
  używana jest miniatura zostawiona przez narzędzia pulpitu w `~/.cache/thumbnails`.
  Plik `.info` obok AppImage nadal ma pierwszeństwo. Paczka zaleca `squashfs-tools`.
- **Ikony podwójne dla AppImage**: *Icon > Snapshot* na AppImage bez `.info` zapisuje mu
  własną ikonę (jak Snapshot daje plikowi z ikoną domyślną jego własną): podwójną ikonę
  PNG – obrazek z AppImage, a po nim obraz zaznaczenia (rozjaśniony), który można
  przemalować w dowolnym programie graficznym. Ikony PNG są zapisywane jak w AROS, z
  blokiem `icOn` (położenie, typ, narzędzie domyślne, typy narzędzi), który jest teraz
  także odczytywany.
- **Zawartość pliku ikony jest używana**: jego typy narzędzi (pokazywane i używane, np.
  `STARTPRI`, o ile nie ustawiono ich w Informacjach), narzędzie domyślne (gdy wskazuje
  program istniejący w tym systemie) i położenie w szufladzie (do zrobienia zdjęcia
  szuflady). Okno Informacje pokazuje własną ikonę pliku; kliknięcie pokazuje obraz
  zaznaczenia.

### Poprawki
- Po polsku strona Paski przewijania w preferencjach Lune nazywała się tak samo jak strona
  Suwaki.
- Gadżet zamknięcia w preferencjach Lune anuluje teraz zmiany, jak Cancel.
- Okno, które pamięta swój rozmiar, nie otwiera się już mniejsze, niż potrzebuje jego
  zawartość.
- Kliknięcie w trakcie mignięcia ekranu (sygnał dźwiękowy) ginęło; teraz trafia do okna pod
  spodem.
- Kliknięcie w oknie mogło zamrozić wskaźnik dla tego okna, gdy w menedżerze okien trwało
  menu albo przeciąganie.

## 0.12.0 – 2026-11-15

- **Zmienione klawisze:** Return naciska przyciski, a Spacja zaznacza pola wyboru, jak w
  oryginalnym systemie (w 0.11.0 Spacja naciskała też przyciski). Każdy klawisz interfejsu
  da się teraz ustawić w GUI Settings > Nawigacja.
- **Widok listy** szuflad ułożony jak w oryginale: kolumny Typ, Nazwa, Rozmiar, Uprawnienia,
  Ostatni dostęp i Komentarz pod wypukłym wierszem tytułów. Kliknięcie Nazwy, Rozmiaru lub
  Ostatniego dostępu sortuje według tej kolumny, a drugie odwraca kolejność.
- **Programy uruchamiane z Wandererem** jak w oryginalnym narzędziu: typ narzędzia
  `STARTPRI` ustala kolejność, `WAIT` przerwę po programie. Program, którego nie da się
  uruchomić, pokazuje ostrzeżenie.
- **Ikony programów:** Zapamiętaj położenie, Zapomnij położenie, Usuń i Opróżnij kosz
  trafiają do programu, dla pozycji zadeklarowanych w `_WANDERER_APPICON_SUPPORTS` (patrz
  README).
- Kliknięcie okna głównego czyni je aktywnym, więc działają jego menu i klawisze.
- Przycisk lupy na pasku narzędzi otwiera Szukaj w danej szufladzie.
- **GUI Settings** ma wszystkie strony oryginału: nowe są System (opóźnienia dymków
  pomocy), Suwaki, Paski przewijania, Listy, Pola tekstowe, Nawigacja, Specjalne i Ramki,
  a strona Przyciski jest pełna.
- **Zune**
  - Klawisze zapisane jak w oryginale (`-repeat shift tab`, `control up`...) dla
    przycisków, list wyboru (menu działa z klawiatury), list, suwaków i okien.
  - Paski przewijania ze strzałkami w wybranym układzie, pionowe suwaki, dzielniki
    Balance dzielące miejsce między obiekty, wzory tła.
  - Własne ramki z obrazków (plik `frame.config`, patrz README).
  - Listy, pola tekstowe i przyciski biorą kolory, tła i czcionki z ustawień.
  - Puste odstępy układają się jak w oryginale, więc ramki takie jak przykładowe suwaki
    zajmują całą szerokość.
- `install.sh` instaluje paczkę z kopii tymczasowej, więc apt nie pokazuje komunikatu o
  pobieraniu poza piaskownicą.
- Daty są w języku systemu.

## 0.11.0 – 2026-11-14

- **Szukaj** ułożone jak w oryginalnym narzędziu: ścieżka z wyborem szuflady, wzorzec
  nazwy (`#?` pasuje do wszystkiego) i opcjonalny tekst, który pliki muszą zawierać.
  Wyniki to lista z kolumnami (pełna ścieżka, rozmiar, atrybuty, data, czas, komentarz).
  Kliknięcie tytułu kolumny sortuje według niej, a drugie odwraca kolejność. Otwórz,
  Widok i Szuflada działają na wybranym pliku. Wyszukiwanie idzie krokami, więc
  Zatrzymaj kończy je od razu.
- **Obsługa Zune z klawiatury**
  - Tab i Shift+Tab przechodzą między gadżetami, a aktywny ma przerywaną ramkę.
  - Spacja naciska przyciski i pola wyboru; listy wyboru zmieniają się Spacją, strzałką
    w górę i w dół.
  - Okna otwierają się bez aktywnego obiektu, więc przypadkowy klawisz nie odpowiada
    na pytanie. Pole tekstowe przejmuje klawiaturę.
  - Okno pamięta aktywny obiekt, gdy się z niego wychodzi.
- **Zune**: listy z kolumnami i wierszem tytułów, pionowe wskaźniki, pozioma kreska.
- **Informacje o dysku** ułożone jak w oryginalnym narzędziu: urządzenie i system plików,
  rozmiary, pionowy wskaźnik, rozmiar bloku i stan.
- **Zmień nazwę i Nowa szuflada** ułożone jak w oryginale: ramka grupy, kreska, szerokość
  jednej trzeciej ekranu. Nazwy z `/` lub `:` są odrzucane.
- **Wykonaj polecenie** ułożone jak w oryginale: etykieta nad polem tekstowym i jego
  przyciskiem pliku, przyciski razem po prawej.
- **Usuwanie** najpierw pyta o pliki bez prawa zapisu (Usuń / Wszystkie / Pomiń /
  Przerwij).
- **Kopiowanie** pyta Pomiń / Pomiń wszystkie / Przerwij, gdy pliku lub szuflady nie da
  się odczytać.
- **Klawisze Super**: lewy Super z M lub N przełącza ekrany. Prawy Super trafia do
  programu, więc Super+N w Wandererze tworzy nową szufladę.
- **Preferencje Wanderera**
  - Automatyczne sortowanie ikon w oknach szuflad.
  - Odstęp obrazka od etykiety, margines tekstu etykiety i rozmiar ramki etykiety.
  - Przesunięcia X i Y kafelków tła.

## 0.10.0 – 2026-11-14

- **Preferencje Wanderera** ułożone jak w oryginalnym programie preferencji, w trzech
  stronach.
  - *General*: nawigacja, okno główne, tytuł ekranu.
  - *Appearance*: przycisk z obrazkiem dla okna głównego i dla szuflad oraz opcje
    **Advanced** ze stronami Icons, Labels i Background, w tym domyślne sortowanie.
  - *Toolbar*: paski, z podglądem.
- **Preferencje Zune** jak program preferencji Zune: lista stron (Windows, Groups,
  Buttons, Cycles, Strings, Listviews, Scrollbars, Palette). Można wybrać każdy
  standardowy rodzaj ramki, odstępy okien i grup oraz ustawienia menu cykli.
- **Nowe klasy Zune**
  - **Slider**: uchwyt z liczbą w środku.
  - **Scale**: podziałka procentowa pod wskaźnikiem.
  - **Przyciski pop-up** (obrazki pliku, szuflady i listy).
  - **Menu cyklu**: naciśnięcie na tekście otwiera listę pozycji.
- **Okno dostępu do plików** jak w oryginale: wskaźnik obiektów, nazwa pliku i wskaźnik
  bajtów, każdy wskaźnik z podziałką, oraz przycisk Stop.
- **Menu**
  - Wielokrotny wybór: lewy przycisk wybiera pozycję, a prawy trzyma menu otwarte.
  - Znaczniki i wyszarzenie zmieniają się, gdy menu jest otwarte.
- **Ikony**
  - Shift + dwuklik przekazuje narzędziu, albo domyślnemu narzędziu projektu, ikony
    zaznaczone we wszystkich oknach jako argumenty.
  - Otwarcie pliku, którego nic nie otwiera, pokazuje Execute Command z jego nazwą.
- **Zapytania**: Super+V odpowiada tak, Super+B lub Escape odpowiada nie.
- **Kółko myszy** w oknach ikon działa jak w oryginale: przewija w bok, gdy nie ma czego
  przewijać w dół albo z wciśniętym Altem. Ctrl skacze do krawędzi, Shift przewija o
  stronę.
- **Execute Command** ma przycisk wyboru pliku.
- **Poprawki**
  - Tło potrafiło otworzyć się jako zwykłe okno na starcie sesji.
  - Awaria przy podglądzie obrazka w preferencjach.
  - Licznik obiektów liczył plik jako skończony, zanim był.

## 0.9.0 – 2026-11-14

- **Dostęp do sieci**: ikona w oknie głównym otwiera okno z zamontowanymi sieciowymi
  systemami plików (NFS, SMB/CIFS, SSHFS, rclone, WebDAV …) jako woluminami, z własnymi
  nazwami woluminów (`outputs:`). Można ją wyłączyć w preferencjach.
- **Pozycje programów w menu Tools** (AppMenuItems) i **ikony programów w oknie głównym**
  (AppIcons). Program ustawia właściwość X na swoim oknie i dostaje komunikat ze ścieżkami
  zaznaczonych albo upuszczonych ikon (opis w README).
- **Ikony przeciągnięte na okno innego programu** są przekazywane przez przeciągnij i upuść
  (XDND, adresy plików). Ikony wracają na swoje miejsca.
- **Okno O Wandererze** jak w oryginale: ikona, wersja, prawa autorskie, klasy. Wanderer
  ma własny ciąg wersji, więc Information pokazuje jego wersję.
- **Preferencje** na stronach rejestru: Icons, Drawer Windows, Screen, Background
  Pictures.
- **Klawisze** w oknach szuflad: Page Up i Page Down przewijają, Home i End przenoszą
  fokus na pierwszą lub ostatnią ikonę. Klawisze nowego okna trafiają do jego ikon.
- **Sesja nie wylogowuje już bez słowa**, gdy Wanderer nie może się uruchomić (np. paczka
  zbudowana na innym systemie z innymi bibliotekami). Mówi dlaczego i daje powłokę. Jej
  dziennik to `~/.cache/wanderer/session.log`.
- **Poprawki**
  - Okna i zapytania otwierają się na środku, bo żądana pozycja dociera teraz do menedżera
    okien.
  - Menedżer okien robi z okna tło nawet wtedy, gdy okno dostaje typ pulpitu po
    pokazaniu.
  - Pasek stanu liczy woluminy jako szuflady.

## 0.8.0 – 2026-11-14

- **Zasobnik systemowy** (AppIcons): ikony programów w zasobniku (XEmbed) siedzą na pasku
  tytułu głównego ekranu, na lewo od gadżetu głębi.
- **Icon > Format…**: dysk wymienny dostaje nowy system plików (FAT32, exFAT lub ext4) z
  nazwą, szybkim formatowaniem i opcjonalnym koszem. Najpierw pyta, działa przez polkit.
- **Information** działa jak narzędzie Info: ikona i nazwa nad rejestrem z trzema
  stronami.
  - *Information*: typ, data, **ciąg wersji** pliku (`$VER:`), rozmiar, ścieżka z
    przyciskiem **Open**, domyślne narzędzie (z oknem wyboru pliku) i komentarz.
  - *Protection*: odczyt, zapis i wykonywanie dla właściciela, grupy i pozostałych.
  - *Tool Types*: lista ustawień zapisanych przy pliku (`user.tooltypes`), z przyciskami
    New i Delete.
- **Rejestr Zune**: grupy stron z zakładkami, rysowanymi jak w oryginale.
- **Preferencje**, nowe ustawienia:
  - Szablon **tytułu ekranu**: `%wv` wersja, `%os` system, `%ov` jądro, `%pc` / `%pf` /
    `%pt` pamięć dostępna, wolna i całkowita.
  - **Nawigacja**: *Classic* otwiera szufladę w nowym oknie, *Enhanced* w tym samym.
    Przycisk „w górę” i pole lokalizacji na pasku narzędzi zawsze przeglądają w swoim
    oknie.
  - **Upuszczone ikony**: *Move* (w obrębie woluminu, inaczej kopia) albo zawsze *Copy*.
  - **Tylko przy fokusie**: dodatkowe linie etykiety widać tylko przy ikonie z fokusem.
  - **Czcionka** i **rozmiar czcionki** etykiet ikon.
  - **Ukryte woluminy**: nazwy, urządzenia lub punkty montowania, których okno główne nie
    pokazuje.
  - Obrazy tła mogą używać **stałych kafelków**, które nie przesuwają się przy przewijaniu.
- **Poprawki**
  - Menu pokazywały wszędzie ostatni tytuł, gdy tekstu nie było w katalogu.
  - Grupy w siatce centrują obiekty mniejsze od komórki, więc pola wyboru zachowują
    rozmiar.

## 0.7.0 – 2026-11-14

- **Katalogi**: interfejs w języku użytkownika (według `LANG` i pozostałych zmiennych
  locale). Menu, okna zapytań, tytuły okien, etykiety, opcje do wyboru i pasek stanu
  pochodzą z katalogu, łącznie z komunikatami zawierającymi nazwy. W paczce jest **język
  polski**. Kolejne języki dodaje się jako `share/wanderer/catalogs/<język>.catalog`.
- **wanderer-wm**: sygnał dźwiękowy (XBell, błąd przy pisaniu) **błyska ekranem** zamiast
  piszczeć, jak systemowe mignięcie ekranu.

## 0.6.6 – 2026-11-14

- **Przypisania (assigns)**: własne nazwy dla szuflad, zapisane w
  `~/.config/wanderer/assigns` (`Docs = ~/Dokumenty`).
  - Działają w każdej ścieżce woluminu, np. `Docs:Listy`.
  - Edytuje je *Wanderer > Assigns…*: nazwa, Add… (szuflada z okna wyboru), Remove, Save.
- **Execute Command** rozumie ścieżki woluminów i przypisania (`cat Docs:list.txt`,
  `ls RAM:`). Słowa, które nie wskazują woluminu, np. `http://…`, zostają bez zmian.

## 0.6.5 – 2026-11-14

- **Ścieżki woluminów**: pole lokalizacji szuflady pokazuje nazwę woluminu i ścieżkę w
  nim (`Home:Dokumenty`, `Ram Disk:`, `System:etc`).
  - Wpisana tak ścieżka otwiera szufladę.
  - Nazwy woluminów: System / SYS, Ram Disk / RAM, Home, Trash i nazwy zamontowanych
    napędów, bez rozróżniania wielkości liter.
  - Ścieżka linuksowa i `~` nadal działają.
  - Z tych ścieżek korzysta też pole Drawer w oknie wyboru pliku, a Information pokazuje,
    gdzie leży ikona.

## 0.6.4 – 2026-11-14

- **Ram Disk**: wolumin w pamięci, zawsze widoczny na głównym oknie.
  - Pliki leżą w systemie plików w pamięci (`/dev/shm/ramdisk-UŻYTKOWNIK`, dostępne tylko
    dla właściciela).
  - Zajmuje tyle pamięci, ile potrzebują pliki, i po restarcie komputera jest pusty.
  - Jego okno pokazuje zajętość.

## 0.6.3 – 2026-11-14

- **Find po zawartości**: oprócz wzorca nazwy można podać tekst, który pliki muszą
  zawierać (bez rozróżniania wielkości liter, pliki do 64 MB). Pusta nazwa z tekstem
  znajduje wszystkie pliki, które go zawierają.
- **Widok listy**: kliknięcie nagłówka kolumny sortuje według nazwy, rozmiaru albo daty.
  Drugie kliknięcie odwraca kolejność, a posortowana kolumna ma strzałkę.

## 0.6.2 – 2026-11-14

- **Okna szuflad nadążają za dyskiem** (inotify): pliki i szuflady utworzone, usunięte,
  przemianowane albo zapisane pojawiają się po chwili. Zaznaczenie, widok i miejsce okna
  zostają zachowane.
- **Tytuły okien**: okno szuflady pokazuje jej nazwę, a okno woluminu jego zajętość, np.
  „System  6% full, 26G free, 16G in use”.
- *Window > Open Parent* w oknie woluminu wysuwa główne okno z woluminami.

## 0.6.1 – 2026-11-14

- **Okno wyjścia**: to, co wypisze polecenie uruchomione przez *Execute Command*, pojawia
  się w osobnym oknie, czcionką o stałej szerokości. Polecenie, które nic nie wypisze,
  nie otwiera okna (jak systemowa konsola).
- **Wanderer > Shutdown…**: wyłącza albo restartuje komputer (elogind / logind, a w razie
  potrzeby systemctl albo shutdown) i pyta ponownie, gdy trwa jeszcze kopiowanie plików.
- **Snapshot zapamiętuje widok szuflady**: ikony albo listę, wszystkie pliki albo nie
  oraz kolejność. Szuflada otwiera się potem tak samo.

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
