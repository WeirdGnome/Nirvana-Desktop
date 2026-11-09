// LisaOS - libplatinum: ikony w stylu Workbencha (Amiga)
//
// Własne rysunki 32x32 w duchu ikon Workbencha 2/3 i MagicWB: paleta szarości,
// granatu i beżu, czarne obrysy, jasne krawędzie od lewej i u góry. Foldery to
// szuflady (na Amidze "drawer"), dyski to kostki twardych dysków i dyskietki 3,5",
// kosz ma pokrywę z uchwytem. Zaznaczona ikona ma odwrócone kolory - jak tryb
// "complement" w Workbenchu.
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

namespace plat {

// paleta (w duchu 8 kolorów MagicWB)
enum : unsigned {
  K = 0x000000,   // czerń - obrysy
  W = 0xFFFFFF,   // biel - światło
  G = 0x959595,   // szary - korpus
  L = 0xB8B8B8,   // jasnoszary
  D = 0x676767,   // ciemnoszary - cień
  B = 0x3B67A2,   // granat Workbencha
  BL = 0x7E9DD0,  // jasny granat
  O = 0xAA907C,   // beż
  P = 0xFFA997,   // brzoskwiniowy
  Y = 0xF2C447,   // żółty (kontrolki, słońce)
  R = 0xC8483A,   // czerwony (dioda)
};

static bool g_sel = false;
static void c(unsigned v) {
  if (g_sel) v = 0xFFFFFF ^ v;   // complement: odwrócone kolory
  fl_color(fl_rgb_color(v >> 16, v >> 8 & 255, v & 255));
}
static void r(int x, int y, int w, int h, unsigned v) { c(v); fl_rectf(x, y, w, h); }
// prostokąt z obrysem i wypukłą krawędzią (światło z lewej u góry)
static void raised(int x, int y, int w, int h, unsigned fill, bool outline = true) {
  r(x, y, w, h, fill);
  c(W); fl_xyline(x + 1, y + 1, x + w - 2); fl_yxline(x + 1, y + 1, y + h - 2);
  c(D); fl_xyline(x + 1, y + h - 2, x + w - 2); fl_yxline(x + w - 2, y + 1, y + h - 2);
  if (outline) { c(K); fl_rect(x, y, w, h); }
}
// wklęsły (wciśnięty) prostokąt
static void sunken(int x, int y, int w, int h, unsigned fill) {
  r(x, y, w, h, fill);
  c(D); fl_xyline(x, y, x + w - 1); fl_yxline(x, y, y + h - 1);
  c(W); fl_xyline(x + 1, y + h - 1, x + w - 1); fl_yxline(x + w - 1, y + 1, y + h - 1);
}

// --- szuflada (folder) -------------------------------------------------------
static void drawer(int x, int y, bool open) {
  // szafka
  raised(x + 2, y + 5, 28, 24, G);
  r(x + 3, y + 6, 26, 3, B);                       // granatowy wierzch
  c(BL); fl_xyline(x + 3, y + 6, x + 28);
  if (!open) {
    sunken(x + 6, y + 12, 20, 13, L);               // front szuflady
    raised(x + 12, y + 16, 8, 4, O);                // uchwyt
    c(K); fl_xyline(x + 13, y + 18, x + 18);
    r(x + 9, y + 13, 5, 2, W);                      // etykietka
  } else {
    r(x + 6, y + 12, 20, 13, K);                    // pusta wnęka
    // wysunięta szuflada z kartkami, większa (bliżej patrzącego)
    r(x + 6, y + 9, 9, 9, W); c(K); fl_rect(x + 6, y + 9, 9, 9);
    r(x + 13, y + 7, 9, 10, W); c(K); fl_rect(x + 13, y + 7, 9, 10);
    c(B); for (int i = 0; i < 3; i++) fl_xyline(x + 15, y + 10 + i * 2, x + 20);
    raised(x + 3, y + 17, 26, 12, L);
    raised(x + 12, y + 21, 8, 4, O);
    c(K); fl_xyline(x + 13, y + 23, x + 18);
  }
}

// --- projekt (dokument) ------------------------------------------------------
static void project(int x, int y) {
  // kartka z zagiętym rogiem
  c(W);
  fl_begin_polygon();
  fl_vertex(x + 6, y + 2); fl_vertex(x + 20, y + 2); fl_vertex(x + 26, y + 8);
  fl_vertex(x + 26, y + 30); fl_vertex(x + 6, y + 30);
  fl_end_polygon();
  c(L);
  fl_begin_polygon(); fl_vertex(x + 20, y + 2); fl_vertex(x + 20, y + 8); fl_vertex(x + 26, y + 8); fl_end_polygon();
  c(K);
  fl_begin_loop();
  fl_vertex(x + 6, y + 2); fl_vertex(x + 20, y + 2); fl_vertex(x + 26, y + 8);
  fl_vertex(x + 26, y + 30); fl_vertex(x + 6, y + 30);
  fl_end_loop();
  fl_line(x + 20, y + 2, x + 20, y + 8); fl_line(x + 20, y + 8, x + 26, y + 8);
  c(G); fl_xyline(x + 7, y + 29, x + 25);   // cień kartki
}

static void doc(int x, int y) {
  project(x, y);
  static const int len[] = {10, 14, 12, 14, 8, 13, 11};
  for (int i = 0; i < 7; i++) r(x + 9, y + 11 + i * 2 + (i > 3 ? 1 : 0), len[i], 1, i == 0 ? K : B);
}

static void picture(int x, int y) {
  project(x, y);
  r(x + 9, y + 11, 15, 15, BL);                    // niebo
  r(x + 18, y + 13, 4, 4, Y);                      // słońce
  c(O);                                            // wzgórza (wielokąt wklęsły)
  fl_begin_complex_polygon();
  fl_vertex(x + 9, y + 26); fl_vertex(x + 9, y + 21); fl_vertex(x + 13, y + 17);
  fl_vertex(x + 17, y + 21); fl_vertex(x + 20, y + 19); fl_vertex(x + 24, y + 23); fl_vertex(x + 24, y + 26);
  fl_end_complex_polygon();
  c(K); fl_rect(x + 9, y + 11, 15, 15);
}

static void music(int x, int y) {
  project(x, y);
  c(B);
  fl_rectf(x + 10, y + 21, 4, 3); fl_rectf(x + 18, y + 19, 4, 3);   // główki nut
  fl_yxline(x + 13, y + 12, y + 22); fl_yxline(x + 21, y + 10, y + 20);
  fl_rectf(x + 13, y + 11, 9, 2);                                      // belka
  c(K); fl_xyline(x + 10, y + 24, x + 13); fl_xyline(x + 18, y + 22, x + 21);
}

// --- narzędzie (program): okienko Workbencha z gadżetami ---------------------
static void tool(int x, int y) {
  raised(x + 2, y + 4, 28, 24, G);
  r(x + 3, y + 5, 26, 5, B);                       // pasek tytułu
  raised(x + 4, y + 6, 4, 3, W, false);            // gadżet zamykania
  c(K); fl_rect(x + 4, y + 6, 4, 3);
  c(W); fl_rect(x + 22, y + 6, 4, 3);              // gadżet głębi (dwa prostokąty)
  c(K); fl_rect(x + 24, y + 7, 4, 3);
  sunken(x + 5, y + 12, 22, 13, L);                // wnętrze okna
  raised(x + 8, y + 15, 7, 4, O);                  // przyciski programu
  raised(x + 17, y + 15, 7, 4, O);
  r(x + 8, y + 21, 16, 2, B);                      // suwak
  r(x + 14, y + 20, 3, 4, K);
}

// --- twardy dysk -------------------------------------------------------------
static void hard_disk(int x, int y, bool home) {
  // górna ściana (perspektywa)
  c(L);
  fl_begin_polygon();
  fl_vertex(x + 5, y + 9); fl_vertex(x + 30, y + 9); fl_vertex(x + 27, y + 14); fl_vertex(x + 2, y + 14);
  fl_end_polygon();
  c(K);
  fl_begin_loop();
  fl_vertex(x + 5, y + 9); fl_vertex(x + 30, y + 9); fl_vertex(x + 27, y + 14); fl_vertex(x + 2, y + 14);
  fl_end_loop();
  // boczna ściana
  c(D);
  fl_begin_polygon();
  fl_vertex(x + 27, y + 14); fl_vertex(x + 30, y + 9); fl_vertex(x + 30, y + 21); fl_vertex(x + 27, y + 26);
  fl_end_polygon();
  c(K);
  fl_begin_loop();
  fl_vertex(x + 27, y + 14); fl_vertex(x + 30, y + 9); fl_vertex(x + 30, y + 21); fl_vertex(x + 27, y + 26);
  fl_end_loop();
  // przód
  raised(x + 2, y + 14, 26, 13, G);
  r(x + 5, y + 18, home ? 9 : 7, 5, home ? B : D);   // tabliczka (LisaOS HD - granatowa)
  if (home) { c(W); fl_xyline(x + 6, y + 20, x + 12); }
  c(K); for (int i = 0; i < 4; i++) fl_yxline(x + 16 + i * 2, y + 18, y + 22);   // kratka wentylacji
  r(x + 24, y + 22, 2, 2, R);                       // dioda
}

// --- dyskietka 3,5" (nośnik wymienny) ----------------------------------------
static void floppy(int x, int y) {
  c(D);
  fl_begin_polygon();
  fl_vertex(x + 4, y + 3); fl_vertex(x + 28, y + 3); fl_vertex(x + 28, y + 29); fl_vertex(x + 6, y + 29);
  fl_vertex(x + 4, y + 27);
  fl_end_polygon();
  c(K);
  fl_begin_loop();
  fl_vertex(x + 4, y + 3); fl_vertex(x + 28, y + 3); fl_vertex(x + 28, y + 29); fl_vertex(x + 6, y + 29);
  fl_vertex(x + 4, y + 27);
  fl_end_loop();
  raised(x + 10, y + 3, 14, 10, L);                // metalowa zasuwka
  r(x + 18, y + 5, 3, 6, K);                        // okienko
  r(x + 7, y + 16, 19, 12, W);                      // etykieta
  c(K); fl_rect(x + 7, y + 16, 19, 12);
  r(x + 7, y + 16, 19, 3, B);
  c(B); fl_xyline(x + 9, y + 22, x + 23); fl_xyline(x + 9, y + 25, x + 20);
  r(x + 5, y + 5, 2, 2, K);                         // otwór zabezpieczenia
}

// --- płyta -------------------------------------------------------------------
static void optical(int x, int y) {
  c(L); fl_pie(x + 3, y + 3, 26, 26, 0, 360);
  c(BL); fl_pie(x + 3, y + 3, 26, 26, 20, 70);
  c(P); fl_pie(x + 3, y + 3, 26, 26, 200, 250);
  c(W); fl_pie(x + 11, y + 11, 10, 10, 0, 360);
  c(K); fl_arc(x + 3, y + 3, 26, 26, 0, 360);
  fl_arc(x + 11, y + 11, 10, 10, 0, 360);
  c(K); fl_pie(x + 14, y + 14, 4, 4, 0, 360);
}

static void padlock(int x, int y) {   // mała kłódka w prawym dolnym rogu
  c(K); fl_arc(x + 19, y + 15, 9, 10, 0, 180);
  fl_arc(x + 20, y + 16, 7, 8, 0, 180);
  raised(x + 18, y + 20, 11, 9, Y);
  r(x + 23, y + 23, 2, 3, K);
}

// --- kosz ---------------------------------------------------------------------
static void trashcan(int x, int y, bool full) {
  // korpus z żebrowaniem
  c(G);
  fl_begin_polygon();
  fl_vertex(x + 7, y + 10); fl_vertex(x + 25, y + 10); fl_vertex(x + 23, y + 30); fl_vertex(x + 9, y + 30);
  fl_end_polygon();
  c(W); fl_line(x + 8, y + 11, x + 10, y + 29);
  for (int i = 0; i < 4; i++) { c(D); fl_line(x + 11 + i * 3, y + 12, x + 12 + i * 3 - (i > 1 ? 1 : 0), y + 28); }
  c(K);
  fl_begin_loop();
  fl_vertex(x + 7, y + 10); fl_vertex(x + 25, y + 10); fl_vertex(x + 23, y + 30); fl_vertex(x + 9, y + 30);
  fl_end_loop();
  if (!full) {
    raised(x + 5, y + 7, 22, 4, L);                 // pokrywa
    raised(x + 13, y + 4, 6, 3, D);                 // uchwyt
  } else {
    // papiery i przekrzywiona pokrywa
    r(x + 9, y + 6, 6, 6, W); c(K); fl_rect(x + 9, y + 6, 6, 6);
    r(x + 15, y + 4, 7, 7, W); c(K); fl_rect(x + 15, y + 4, 7, 7);
    c(B); fl_xyline(x + 16, y + 6, x + 20); fl_xyline(x + 16, y + 8, x + 19);
    c(L);
    fl_begin_polygon();
    fl_vertex(x + 4, y + 6); fl_vertex(x + 24, y + 1); fl_vertex(x + 25, y + 4); fl_vertex(x + 5, y + 9);
    fl_end_polygon();
    c(K);
    fl_begin_loop();
    fl_vertex(x + 4, y + 6); fl_vertex(x + 24, y + 1); fl_vertex(x + 25, y + 4); fl_vertex(x + 5, y + 9);
    fl_end_loop();
  }
}

// --- wysuń, sieć, Wi-Fi --------------------------------------------------------
static void eject(int x, int y) {
  raised(x + 3, y + 3, 26, 26, G);
  c(B);
  fl_begin_polygon(); fl_vertex(x + 16, y + 7); fl_vertex(x + 25, y + 17); fl_vertex(x + 7, y + 17); fl_end_polygon();
  fl_rectf(x + 7, y + 20, 19, 4);
  c(K);
  fl_begin_loop(); fl_vertex(x + 16, y + 7); fl_vertex(x + 25, y + 17); fl_vertex(x + 7, y + 17); fl_end_loop();
  fl_rect(x + 7, y + 20, 19, 4);
}

static void monitor(int x, int y) {
  raised(x, y, 13, 11, O);
  r(x + 2, y + 2, 9, 6, B);
  c(BL); fl_xyline(x + 3, y + 3, x + 7);
  r(x + 4, y + 11, 5, 2, D);
  raised(x + 1, y + 13, 11, 3, O);
}
static void network(int x, int y) {
  monitor(x + 1, y + 3);
  monitor(x + 18, y + 13);
  c(K); fl_line(x + 7, y + 19, x + 7, y + 25); fl_line(x + 7, y + 25, x + 18, y + 25);
  c(Y); fl_rectf(x + 16, y + 24, 3, 3);
}

static void wifi(int x, int y) {
  raised(x + 6, y + 22, 20, 7, G);                 // podstawa (router)
  r(x + 9, y + 25, 2, 2, Y); r(x + 13, y + 25, 2, 2, Y);
  c(K); fl_yxline(x + 22, y + 13, y + 22);         // antena
  c(B);
  fl_line_style(FL_SOLID, 2);
  fl_arc(x + 16, y + 6, 12, 12, 20, 160);
  fl_arc(x + 12, y + 2, 20, 20, 25, 155);
  fl_line_style(0);
}

bool draw_icon_workbench(Icon icon, int x, int y, bool sel) {
  g_sel = sel;
  switch (icon) {
  case Icon::Folder:      drawer(x, y, false); break;
  case Icon::OpenFolder:  drawer(x, y, true); break;
  case Icon::Document:    doc(x, y); break;
  case Icon::Image:       picture(x, y); break;
  case Icon::Music:       music(x, y); break;
  case Icon::Application: tool(x, y); break;
  case Icon::Home:        hard_disk(x, y, true); break;
  case Icon::HardDisk:    hard_disk(x, y, false); break;
  case Icon::Removable:   floppy(x, y); break;
  case Icon::Optical:     optical(x, y); break;
  case Icon::Encrypted:   hard_disk(x, y, false); padlock(x, y); break;
  case Icon::Trash:       trashcan(x, y, false); break;
  case Icon::TrashFull:   trashcan(x, y, true); break;
  case Icon::Eject:       eject(x, y); break;
  case Icon::Network:     network(x, y); break;
  case Icon::Wifi:        wifi(x, y); break;
  default: g_sel = false; return false;
  }
  g_sel = false;
  return true;
}

}  // namespace plat
