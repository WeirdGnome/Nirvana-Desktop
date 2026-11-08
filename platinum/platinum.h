// LisaOS - libplatinum
// Wygląd "Platinum" (w duchu wytycznych Mac OS 8/9) dla FLTK 1.4.
// Wszystkie komponenty LisaOS (insight, denise, gary, alice...) rysują
// przez tę bibliotekę, żeby całość wyglądała spójnie.
#pragma once

#include <FL/Enumerations.H>
#include <string>
#include <vector>

namespace plat {

// ---- Paleta ---------------------------------------------------------------
// Wszystkie kolory to indeksy palety FLTK, a nie wartości RGB. Dzięki temu
// zmiana schematu (panel Wygląd) to tylko Fl::set_color() + redraw() - widgety,
// które zapamiętały np. color(plat::GRAY), same dostają nowy kolor.
extern Fl_Color GRAY;          // tło okien i paneli ("okno")         = FL_BACKGROUND_COLOR
extern Fl_Color TEXT;          // tekst na tle okien ("tekst")         = FL_FOREGROUND_COLOR
extern Fl_Color CONTENT;       // wnętrze list i pól ("tresc")         = FL_BACKGROUND2_COLOR
extern Fl_Color SEL;           // zaznaczenie ("zaznaczenie")          = FL_SELECTION_COLOR
extern Fl_Color CONTENT_TEXT;  // tekst w listach i polach
extern Fl_Color SEL_TEXT;      // tekst zaznaczony
extern Fl_Color ICON_SEL;      // tło podpisu zaznaczonej ikony
extern Fl_Color ICON_SEL_TEXT; // tekst podpisu zaznaczonej ikony
extern Fl_Color LIGHT;         // jasna krawędź wypukłości
extern Fl_Color MID;           // średni cień
extern Fl_Color SHADOW;        // ciemny cień
extern Fl_Color DARK;          // obrys ("obramowanie")
extern Fl_Color HILITE;        // akcent: suwak przewijania ("akcent")
extern Fl_Color STRIPE;        // ciemne prążki paska tytułu
extern Fl_Color TITLE_TEXT;    // tytuł aktywnego okna
extern Fl_Color TITLE_I_TEXT;  // tytuł nieaktywnego okna
extern Fl_Color DESK_A;        // kolor biurka
extern Fl_Color DESK_B;        // drugi ton wzoru biurka
extern Fl_Color ROOT_TINT;     // akcent trybu root

// ---- Schematy kolorów i ustawienia wyglądu --------------------------------
// Schemat to plik klucz=wartość w katalogu "schematy" (np. platinum.conf,
// redmond97-se/ouroboros.conf); identyfikatorem jest ścieżka bez ".conf".
enum class TitleStyle { Stripes, Solid, Gradient };

struct Scheme {
  std::string id, name, source;
  TitleStyle style = TitleStyle::Stripes;
  unsigned window = 0xDDDDDD, text = 0x000000, content = 0xFFFFFF, content_text = 0x000000;
  unsigned sel = 0x4444AA, sel_text = 0xFFFFFF, border = 0x000000;
  unsigned title = 0xDDDDDD, title2 = 0xDDDDDD, title_text = 0x000000;
  unsigned title_i = 0xDDDDDD, title_i2 = 0xDDDDDD, title_i_text = 0x777777;
  int icon_sel = -1, accent = -1, desk = -1;   // -1: wylicz z pozostałych
  int light = -1, mid = -1, shadow = -1;       // -1: wylicz z koloru okna
};

struct Appearance {                 // ~/.config/lisaos/wyglad.conf
  std::string scheme = "platinum";
  int style = -1;                   // -1: jak w schemacie, inaczej (int)TitleStyle
  int desk = -1;                    // -1: kolor biurka ze schematu
  std::string pattern = "kropki";   // gladki, kropki, krata, ukosne, cegly, raster
  bool animations = true;
};

std::vector<std::string> scheme_dirs();                      // kolejność wyszukiwania
std::vector<Scheme> list_schemes();                          // posortowane, bez duplikatów
bool load_scheme(const std::string &id, Scheme &out);        // po identyfikatorze
bool parse_scheme(const std::string &file, Scheme &out);
Scheme builtin_platinum();
Appearance read_appearance();
bool write_appearance(const Appearance &a);
std::string appearance_file();

void apply(const Scheme &s, const Appearance &a);   // ustaw paletę
void reload_appearance();          // odczytaj ustawienia i schemat, zastosuj
const Scheme &current_scheme();
const Appearance &current_appearance();
TitleStyle title_style();

extern const char *const PATTERNS[];        // identyfikatory wzorów (nullptr na końcu)
extern const char *const PATTERN_NAMES[];   // nazwy do wyświetlenia
// Kafelek wzoru biurka (RGB, 8x8 albo 16x16) w kolorach DESK_A/DESK_B.
// Wywołujący zwalnia obraz (delete).
unsigned char *pattern_tile(const std::string &pattern, int &w, int &h);   // RGB w*h*3, zwolnij delete[]

// Powiadomienia o zmianie wyglądu: panel ustawia właściwość _LISAOS_WYGLAD
// na oknie głównym X, a Rambler i Insight po jej zmianie wołają
// reload_appearance() i odświeżają okna.
constexpr const char *APPEARANCE_ATOM = "_LISAOS_WYGLAD";
void notify_appearance_changed();

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

// Inicjalizacja: box types, czcionki, wygląd z ~/.config/lisaos/wyglad.conf.
// root=true włącza akcent trybu administratora.
void init(bool root = false);
bool root_mode();

// ---- Ikony (32x32, rysowane wektorowo) ------------------------------------
enum class Icon { Folder, OpenFolder, Document, Application, Home, Trash, TrashFull, Image, Music,
                  HardDisk, Removable, Optical, Encrypted, Eject };
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
