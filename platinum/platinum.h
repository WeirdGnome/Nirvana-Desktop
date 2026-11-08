// LisaOS - libplatinum
// Wygląd "Platinum" (w duchu wytycznych Mac OS 8/9) dla FLTK 1.4.
// Wszystkie komponenty LisaOS (insight, denise, gary, alice...) rysują
// przez tę bibliotekę, żeby całość wyglądała spójnie.
#pragma once

#include <FL/Enumerations.H>
#include <string>

namespace plat {

// ---- Paleta ---------------------------------------------------------------
// Kolory jako Fl_Color (RGB), ustawiane w init().
extern Fl_Color GRAY;       // tło "Platinum"        #DDDDDD
extern Fl_Color LIGHT;      // jasna krawędź          #FFFFFF
extern Fl_Color MID;        // średni cień            #AAAAAA
extern Fl_Color SHADOW;     // ciemny cień            #777777
extern Fl_Color DARK;       // obrys                  #000000
extern Fl_Color HILITE;     // zaznaczenie tekstu     lawendowy
extern Fl_Color DESK_A;     // wzór biurka, ton 1
extern Fl_Color DESK_B;     // wzór biurka, ton 2
extern Fl_Color ROOT_TINT;  // akcent trybu root

// ---- Typy ramek (Fl_Boxtype) ----------------------------------------------
extern Fl_Boxtype UP_BOX;       // wypukły panel
extern Fl_Boxtype DOWN_BOX;     // wklęsły panel
extern Fl_Boxtype BUTTON_BOX;   // zaokrąglony przycisk
extern Fl_Boxtype BUTTON_DOWN;  // wciśnięty przycisk
extern Fl_Boxtype WELL_BOX;     // "studnia" - pole tekstowe, lista
extern Fl_Boxtype MENUBAR_BOX;  // pasek menu
extern Fl_Boxtype THUMB_BOX;    // lawendowy suwak przewijania z rowkami
extern Fl_Boxtype TRACK_BOX;    // tor suwaka

// ---- Czcionki ----------------------------------------------------------------
extern Fl_Font FONT_SYSTEM;     // tytuły, menu (odpowiednik Charcoal 12)
extern Fl_Font FONT_SMALL;      // etykiety ikon (odpowiednik Geneva 9/10)
constexpr int SIZE_SYSTEM = 12;
constexpr int SIZE_SMALL  = 11;

// Inicjalizacja: kolory, box types, czcionki, schemat FLTK.
// root=true włącza akcent trybu administratora.
void init(bool root = false);
bool root_mode();

// ---- Ikony (32x32, rysowane wektorowo) ------------------------------------
enum class Icon { Folder, OpenFolder, Document, Application, Home, Trash, TrashFull, Image, Music };
void draw_icon(Icon icon, int x, int y, bool selected);
void draw_logo(int x, int y, int size);   // logo LisaOS: mały chip w stylu Amigi

// ---- Chrome okna -------------------------------------------------------------
constexpr int TITLE_H = 20;   // wysokość paska tytułu
constexpr int FRAME   = 5;    // grubość ramki
constexpr int BOX     = 11;   // rozmiar przycisków na pasku tytułu
constexpr int MENUBAR_H = 20; // wysokość globalnego paska menu
constexpr int SCROLL_W  = 15; // szerokość pasków przewijania
void draw_titlebar(int x, int y, int w, const char *title, bool active,
                   int pressed_box = 0 /* 0 brak, 1 zamknij, 2 zoom, 3 zwiń */,
                   bool boxes = true /* false: okno dialogowe bez przycisków */);
void draw_frame(int x, int y, int w, int h, bool active, bool shaded);
void draw_grow_box(int x, int y);

// Pomocnicze
std::string ellipsize(const std::string &s, int max_w);   // obcięcie tekstu z "…"
std::string polish_items(int n);                           // "1 element", "3 elementy", "7 elementów"

} // namespace plat
