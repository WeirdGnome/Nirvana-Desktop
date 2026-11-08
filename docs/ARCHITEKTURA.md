# LisaOS: architektura (szkic)

## Warstwy

    Devuan (runit, elogind, polkit)  ->  X11 (Xorg/XLibre)  ->  lisaos-insight (WM)
                                                              ->  lisaos-gary (pasek menu)
                                                              ->  lisaos-denise (pliki/biurko)
                                                              ->  aplikacje AppImage

Wszystkie komponenty linkują `lisaos-platinum`, więc wygląd jest jeden.
FLTK 1.4 ma backend Wayland, dlatego przy ewentualnym przejściu na Waylanda
wymieniamy tylko `insight` (kompozytor na wlroots), a resztę zostawiamy.

## Prototyp a docelowy podział

W prototypie `denise` sam rysuje okna (klasa `plat::PWindow`) wewnątrz
jednego okna biurka. Docelowo:

1. `insight` rysuje ramki Platinum wokół prawdziwych okien X11, korzystając z
   `plat::draw_frame` / `plat::draw_titlebar` / `plat::draw_grow_box`.
2. `denise` otwiera każdy folder jako osobne okno X11, a biurko staje się oknem
   typu `_NET_WM_WINDOW_TYPE_DESKTOP`.
3. Pasek menu z `denise` przechodzi do `gary`.

## Globalny pasek menu (`gary`)

- Własne aplikacje publikują menu przez prosty protokół (propozycja: socket
  unix `$XDG_RUNTIME_DIR/lisaos/gary`, komunikaty tekstowe z drzewem menu).
- Obce aplikacje (GTK/Qt w AppImage) zachowują menu w oknie; `gary` pokazuje
  dla nich tylko menu logo i przełącznik aplikacji.

## Model uprawnień

- Tryb prosty: Rambler nie otworzy niczego poza `$HOME` (sprawdzanie po
  `weakly_canonical`, więc dowiązania symboliczne nie wyprowadzą poza folder LisaOS).
- AppImage docelowo uruchamiane przez `bubblewrap`: zapis tylko w `$HOME`,
  system tylko do odczytu, bez dostępu do innych użytkowników.
- Tryb root: `pkexec lisaos-denise --root`, osobny proces, wyraźne oznaczenie.

## Nazewnictwo

- Wersje: kodowe nazwy Mac OS 8/9: 1.0 „Tempo”, 2.0 „Allegro”, 3.0 „Sonata”.
- Komponenty: menedżer okien Insight; pozostałe nazwane po układach Amigi (Denise, Paula, Gary, Alice).
