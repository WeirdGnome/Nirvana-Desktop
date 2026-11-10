// Nirvana Desktop - libplatinum: animacje okien w stylu klasycznego Mac OS
//
// "Zoom rects": przy otwieraniu, zamykaniu, zwijaniu i powiększaniu okna
// po ekranie przelatuje seria konturów prostokątów. Kontury rysujemy metodą
// XOR prosto na oknie głównym X (IncludeInferiors), więc nie potrzeba
// kompozytora, a drugie narysowanie tego samego konturu dokładnie go ściera.
//
// Wyłączanie: zmienna środowiskowa NIRVANA_ANIMATIONS=0
// (docelowo przełącznik w panelu sterowania "Wygląd").
#pragma once

namespace plat {

struct Rect { int x, y, w, h; };

// Skala interfejsu (HiDPI). Programy Nirvana Desktop liczą w jednostkach FLTK, a X11
// w pikselach: px() zamienia jednostki na piksele, un() piksele na jednostki.
// Potrzebne tylko przy bezpośrednich wywołaniach Xlib (menedżer okien, XEmbed,
// komunikaty EWMH) - zwykłe rysowanie FLTK skaluje samo.
float ui_scale();
inline int px(int u) { float s = ui_scale(); return (int)(u * s + (u < 0 ? -0.5f : 0.5f)); }
inline int un(int p) { float s = ui_scale(); return (int)(p / s + (p < 0 ? -0.5f : 0.5f)); }
inline Rect px(Rect r) { return {px(r.x), px(r.y), px(r.x + r.w) - px(r.x), px(r.y + r.h) - px(r.y)}; }

bool animations_enabled();
void animations_suspend(bool s);   // np. przy odtwarzaniu okien po starcie

// Animuje przejście od prostokąta `from` do `to` (współrzędne ekranu).
// Wywołanie blokuje na czas animacji (~150 ms).
void zoom_rects(Rect from, Rect to);

// Mały prostokąt w środku danego - punkt startu/końca, gdy nie ma ikony.
inline Rect center_of(Rect r, int size = 16) {
  return {r.x + r.w / 2 - size / 2, r.y + r.h / 2 - size / 2, size, size};
}

} // namespace plat
