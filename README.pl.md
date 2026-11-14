# Wanderer dla X11 (Nirvana Desktop)

Port **Wanderera**, pulpitu i menedżera plików systemu AROS, na Linuksa i X11. Pulpit
działa na FLTK, a okna obsługuje własny menedżer okien `wanderer-wm`, który przejmuje
rolę Intuition: ekrany, ramki okien, gadżety i menu na pasku ekranu.

Wersja: **0.2.0**. Dotychczasowa Nirvana Desktop jest na gałęzi `nirvana-legacy`.

## Instalacja

```sh
tar xzf wanderer-x11-0.2.0.tar.gz
cd wanderer-x11
./install.sh
```

Instalator robi kolejno:

1. doinstalowuje przez apt pakiety potrzebne do budowania;
2. buduje programy (jeśli w systemie nie ma FLTK 1.4, pobiera je i buduje razem z nimi);
3. instaluje je do `/usr/local`;
4. dodaje plik sesji `/usr/share/xsessions/wanderer.desktop`.

Dzięki temu na ekranie logowania (LightDM, SDDM, GDM, XDM…) pojawia się sesja
**Wanderer**. Opcje instalatora: `--prefix KATALOG`, `--no-deps`, `--uninstall`.

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

## Licencja

AROS Public License 1.1 (plik `LICENSE`). Kod przeniesiony z AROS zachowuje informacje o
prawach autorskich zespołu AROS Development Team.
