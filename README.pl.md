# Rambler dla X11 (Nirvana Desktop)

**Rambler** to port Wanderera, pulpitu i menedżera plików systemu AROS, na Linuksa i X11.
**Lune** to port Zune, biblioteki interfejsu AROS. Nowe nazwy odróżniają ten port od
oryginałów AROS; pliki i komentarze przeniesione z AROS nadal je wymieniają. Pulpit
działa na FLTK, a okna obsługuje własny menedżer okien `rambler-wm`, który przejmuje
rolę Intuition: ekrany, ramki okien, gadżety i menu na pasku ekranu.

Wersja: **0.16.0**. Dotychczasowa Nirvana Desktop jest na gałęzi `nirvana-legacy`.

## Instalacja

```sh
tar xzf rambler-x11-0.16.0.tar.gz
cd rambler-x11
./install.sh
```

Instalator:

1. doinstalowuje pakiety potrzebne do budowania (apt);
2. buduje programy (jeśli w systemie nie ma FLTK 1.4, pobiera je i wkompilowuje);
3. **tworzy paczkę Debiana** `rambler-x11_WERSJA_ARCH.deb`;
4. usuwa pliki wcześniejszej instalacji zrobionej bez paczki (np. starszym `install.sh` do
   `/usr/local`), ale tylko te, które nie należą do żadnej paczki;
5. instaluje paczkę przez apt.

Paczka umieszcza programy w `/usr/bin`, a sesję w `/usr/share/xsessions/rambler.desktop`,
więc na ekranie logowania (LightDM, SDDM, GDM, XDM…) pojawia się sesja **Rambler**.

Paczka powstaje dla systemu, na którym jest budowana (jego bibliotek): buduj ją na
każdym systemie przez `./install.sh` zamiast instalować `.deb` zrobiony gdzie indziej.
Gdy Rambler nie może się uruchomić, sesja mówi dlaczego, zamiast od razu wylogować;
jej dziennik to `~/.cache/rambler/session.log`.

Opcje:

* `--package-only` – tylko buduje paczkę `.deb`;
* `--no-deps` – nie doinstalowuje pakietów do budowania;
* `--uninstall` – usuwa paczkę i pliki zainstalowane bez niej (można też użyć
  `sudo apt remove rambler-x11`).

Sesja uruchamia najpierw `rambler-wm`, a potem `rambler --backdrop`. Polecenie
*Rambler > Quit…* kończy sesję. Liczbę ekranów ustawisz w
`~/.config/rambler/session.conf`, na przykład `RAMBLER_SCREENS=3`.

## Obsługa

* **Menu**: przytrzymaj prawy przycisk nad oknem. Pasek ekranu pokaże menu tego okna, a
  puszczenie przycisku nad pozycją ją wybiera. Skróty mają postać Super+klawisz.
* **Ekrany**: ściągnij ekran w dół za pasek tytułu, żeby odsłonić ekrany pod spodem. Esc
  albo prawy przycisk w trakcie przeciągania przywraca go na miejsce. Gadżet na prawym
  końcu paska chowa ekran pod spód. Super+M wysuwa ekran z tyłu na wierzch, a Super+N
  chowa ekran z przodu.
* **Okna**: gadżet zamknięcia jest po lewej, zoom i głębia po prawej, rozmiar w rogu.
  Kliknięcie aktywuje okno, ale nie wysuwa go na wierzch. Gadżet głębi wysuwa okno na
  wierzch, a jeśli okno już jest na wierzchu, chowa je pod spód.
* **Ikony**: dwuklik otwiera. Shift dodaje do zaznaczenia. Przeciągnięcie po pustym
  miejscu zaznacza lassem. Ikony przeciągnięte do innego okna są przenoszone w obrębie
  dysku i kopiowane między dyskami.
* **Wyłożenie**: ikony przeciągnięte z szuflady na puste tło okna głównego zostają tam
  wyłożone; *Icon > Put Away* je zabiera.
* **Pliki ikon**: własna ikona pliku to `nazwa.info` obok niego (ikony klasyczne,
  NewIcons, kolorowe i PNG). Ikona podwójna ma drugi obraz na stan zaznaczenia; idzie
  razem z plikiem przy kopiowaniu, przenoszeniu, zmianie nazwy i usuwaniu.
* **Ikony domyślne**: pliki `def_drawer.info`, `def_tool.info`, `def_project.info`,
  `def_disk.info`, `def_txt.info` … w `~/.config/rambler/icons` zastępują ikony każdej
  szuflady, narzędzia, projektu, dysku, tekstu … bez własnej ikony.
* **AppImage** pokazują ikonę, którą zawierają (wyjmowaną przez `unsquashfs` z paczki
  `squashfs-tools`; AppImage nie jest przy tym uruchamiany). `rambler --appimage-icon PLIK`
  opisuje krok po kroku, co dzieje się z ikoną AppImage. AppImage z obrazem DwarFS zamiast
  squashfs („anylinux”, uruntime) wymagają `dwarfsextract`: `./install.sh --with-dwarfs`
  instaluje program projektu DwarFS w `/usr/local/bin`. AppImage dodany do menu przez
  narzędzie pulpitu dostaje też ikonę wskazaną w jego pliku `.desktop`.
  *Icon > Snapshot* zapisuje ją jako własną podwójną ikonę AppImage
  (`Nazwa.AppImage.info`: dwa obrazy PNG jeden po drugim); przemaluj drugi, by zmienić
  wygląd zaznaczenia.

## Preferencje

*Tools > Prefs Drawer* otwiera programy preferencji: Input, Lune (GUI Settings),
Rambler, IControl (ekrany, menu i okna menedżera okien), ScreenMode (tryb wyświetlania,
przez xrandr), Time (data i godzina), Sound (sygnał i wyjście dźwięku), Locale (języki, kraj, strefa
czasowa), Pointer (motyw i rozmiar kursora), Font (czcionki ikon, ekranu i systemu) i
Network (interfejsy, nazwa komputera, sieci bezprzewodowe; wymaga działającego
NetworkManagera). Każdy działa też samodzielnie
(`rambler --prefs font|icontrol|input|locale|lune|network|pointer|rambler|screenmode|time`). Jak w
oryginalnym systemie, **Use** wprowadza ustawienia tylko na tę sesję (zapisuje je w
`$XDG_RUNTIME_DIR/rambler`, odpowiedniku ENV:), a **Save** także je zachowuje
(`~/.config/rambler`, odpowiednik ENVARC:). Działający Rambler przyjmuje zmianę zrobioną
przez program preferencji w ciągu sekundy.

## Programy uruchamiane z Ramblerem

Na początku sesji Rambler uruchamia wszystko z `~/.config/rambler/startup` (programy,
skrypty, pliki `.desktop`), a potem wpisy autostartu XDG. Dwa typy narzędzi (ustawiane w
oknie Informacje) działają jak w oryginalnym systemie:

- `STARTPRI=n` (od -128 do 127): najwyższy startuje pierwszy; bez niego 0. Programy o tym
  samym priorytecie startują w kolejności nazw.
- `WAIT=n` (sekundy, najwyżej 60): przerwa po programie, zanim wystartuje następny.

Program, którego nie da się uruchomić, pokazuje okienko z ostrzeżeniem, a pozostałe
startują po nim.

## Pozycje programów w menu Tools

Program może dodać pozycje do menu Tools Ramblera (jak AddAppMenuItem). Ustawia na swoim
głównym oknie właściwość `_RAMBLER_APPMENU` (typ `UTF8_STRING`, nazwy pozycji rozdzielone
znakiem NUL). Po wybraniu pozycji Rambler:

1. wpisuje ścieżki zaznaczonych ikon do właściwości okna `_RAMBLER_APPMENU_ARGS`
   (rozdzielone znakiem NUL);
2. wysyła do okna ClientMessage `_RAMBLER_APPMENU_PICK`, gdzie `l[0]` to numer pozycji, a
   `l[1]` liczba ścieżek.

Pozycje znikają, gdy okno się zamknie albo właściwość zostanie usunięta.

## Ikony programów w oknie głównym

Program może też położyć ikonę w oknie głównym (jak AddAppIcon): właściwość
`_RAMBLER_APPICON` (`UTF8_STRING`, po jednej nazwie na ikonę, rozdzielone znakiem NUL) na
swoim głównym oknie. Otwarcie ikony albo upuszczenie na nią innych ikon wysyła do okna
ClientMessage `_RAMBLER_APPICON_MESSAGE` (`l[0]` numer ikony, `l[1]` liczba ścieżek);
ścieżki upuszczonych ikon są we właściwości `_RAMBLER_APPICON_ARGS` (rozdzielone znakiem
NUL).

`l[2]` mówi, dlaczego wysłano komunikat (numery AMCLASSICON): 0 ikonę otwarto albo
upuszczono na nią ikony; 4 Zapamiętaj położenie, 5 Zapomnij położenie, 8 Usuń, 10 Opróżnij
kosz wybrano z menu Ikona, gdy była zaznaczona. Te pozycje są dostępne dla ikony tylko
wtedy, gdy program deklaruje, że je obsługuje: właściwość `_RAMBLER_APPICON_SUPPORTS`
(`CARDINAL`, po jednej na ikonę, w kolejności) z ustawionym bitem *n* dla klasy *n* (jak
znaczniki WBAPPICONA_Supports). Bez niej ikona obsługuje tylko otwarcie.

## Własne ramki

GUI Settings > Ramki przyjmuje do 16 własnych ramek. Każda to plik `frame.config` z
dwoma obrazkami obok: `up/default` i `down/default` (PNG, BMP, GIF lub JPEG; rozszerzenie
można pominąć). Plik ustala, ile pikseli brzegów obrazka to narożniki i krawędzie
(`TileLeft`, `TileTop`, `TileRight`, `TileBottom`), oraz ile miejsca ramka zajmuje z
każdej strony (`InnerLeft`, `InnerTop`, `InnerRight`, `InnerBottom`), po jednym
ustawieniu w wierszu:

```
TileLeft 4
TileTop 4
TileRight 4
TileBottom 4
InnerLeft 3
InnerTop 3
InnerRight 3
InnerBottom 3
```

Narożniki są rysowane bez zmian, krawędzie i środek są powtarzane. Ramki pojawiają się
potem jako „Własna nr 1” do „Własna nr 16” w każdym wyborze ramki. Ramka, której obrazków
nie da się odczytać, jest rysowana jako prostokąt.

## Języki

Interfejs mówi pierwszym z preferowanych języków z preferencji Locale, a bez nich
językiem użytkownika (`LANGUAGE`, `LC_ALL`, `LC_MESSAGES`, `LANG`).
Teksty pochodzą z katalogów w `share/rambler/catalogs`, po jednym w wierszu: tekst
angielski, TAB, tłumaczenie. W paczce jest polski (`polski.catalog`). Tekst bez
tłumaczenia pokazuje się po angielsku.

## Licencja

AROS Public License 1.1 (plik `LICENSE`). Kod przeniesiony z AROS zachowuje informacje o
prawach autorskich zespołu AROS Development Team.
