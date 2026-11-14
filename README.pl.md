# Wanderer dla X11 (Nirvana Desktop)

Port **Wanderera**, pulpitu i menedżera plików systemu AROS, na Linuksa i X11. Pulpit
działa na FLTK, a okna obsługuje własny menedżer okien `wanderer-wm`, który przejmuje
rolę Intuition: ekrany, ramki okien, gadżety i menu na pasku ekranu.

Wersja: **0.10.0**. Dotychczasowa Nirvana Desktop jest na gałęzi `nirvana-legacy`.

## Instalacja

```sh
tar xzf wanderer-x11-0.10.0.tar.gz
cd wanderer-x11
./install.sh
```

Instalator:

1. doinstalowuje pakiety potrzebne do budowania (apt);
2. buduje programy (jeśli w systemie nie ma FLTK 1.4, pobiera je i wkompilowuje);
3. **tworzy paczkę Debiana** `wanderer-x11_WERSJA_ARCH.deb`;
4. usuwa pliki wcześniejszej instalacji zrobionej bez paczki (np. starszym `install.sh` do
   `/usr/local`), ale tylko te, które nie należą do żadnej paczki;
5. instaluje paczkę przez apt.

Paczka umieszcza programy w `/usr/bin`, a sesję w `/usr/share/xsessions/wanderer.desktop`,
więc na ekranie logowania (LightDM, SDDM, GDM, XDM…) pojawia się sesja **Wanderer**.

Paczka powstaje dla systemu, na którym jest budowana (jego bibliotek): buduj ją na
każdym systemie przez `./install.sh` zamiast instalować `.deb` zrobiony gdzie indziej.
Gdy Wanderer nie może się uruchomić, sesja mówi dlaczego, zamiast od razu wylogować;
jej dziennik to `~/.cache/wanderer/session.log`.

Opcje:

* `--package-only` – tylko buduje paczkę `.deb`;
* `--no-deps` – nie doinstalowuje pakietów do budowania;
* `--uninstall` – usuwa paczkę i pliki zainstalowane bez niej (można też użyć
  `sudo apt remove wanderer-x11`).

Sesja uruchamia najpierw `wanderer-wm`, a potem `wanderer --backdrop`. Polecenie
*Wanderer > Quit…* kończy sesję. Liczbę ekranów ustawisz w
`~/.config/wanderer/session.conf`, na przykład `WANDERER_SCREENS=3`.

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

## Pozycje programów w menu Tools

Program może dodać pozycje do menu Tools Wanderera (jak AddAppMenuItem). Ustawia na swoim
głównym oknie właściwość `_WANDERER_APPMENU` (typ `UTF8_STRING`, nazwy pozycji rozdzielone
znakiem NUL). Po wybraniu pozycji Wanderer:

1. wpisuje ścieżki zaznaczonych ikon do właściwości okna `_WANDERER_APPMENU_ARGS`
   (rozdzielone znakiem NUL);
2. wysyła do okna ClientMessage `_WANDERER_APPMENU_PICK`, gdzie `l[0]` to numer pozycji, a
   `l[1]` liczba ścieżek.

Pozycje znikają, gdy okno się zamknie albo właściwość zostanie usunięta.

## Ikony programów w oknie głównym

Program może też położyć ikonę w oknie głównym (jak AddAppIcon): właściwość
`_WANDERER_APPICON` (`UTF8_STRING`, po jednej nazwie na ikonę, rozdzielone znakiem NUL) na
swoim głównym oknie. Otwarcie ikony albo upuszczenie na nią innych ikon wysyła do okna
ClientMessage `_WANDERER_APPICON_MESSAGE` (`l[0]` numer ikony, `l[1]` liczba ścieżek);
ścieżki upuszczonych ikon są we właściwości `_WANDERER_APPICON_ARGS` (rozdzielone znakiem
NUL).

## Języki

Interfejs mówi językiem użytkownika (`LANGUAGE`, `LC_ALL`, `LC_MESSAGES`, `LANG`).
Teksty pochodzą z katalogów w `share/wanderer/catalogs`, po jednym w wierszu: tekst
angielski, TAB, tłumaczenie. W paczce jest polski (`polski.catalog`). Tekst bez
tłumaczenia pokazuje się po angielsku.

## Licencja

AROS Public License 1.1 (plik `LICENSE`). Kod przeniesiony z AROS zachowuje informacje o
prawach autorskich zespołu AROS Development Team.
