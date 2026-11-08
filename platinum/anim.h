// LisaOS - libplatinum: animacje okien w stylu klasycznego Mac OS
//
// "Zoom rects": przy otwieraniu, zamykaniu, zwijaniu i powiększaniu okna
// po ekranie przelatuje seria konturów prostokątów. Kontury rysujemy metodą
// XOR prosto na oknie głównym X (IncludeInferiors), więc nie potrzeba
// kompozytora, a drugie narysowanie tego samego konturu dokładnie go ściera.
//
// Wyłączanie: zmienna środowiskowa LISAOS_ANIMATIONS=0
// (docelowo przełącznik w panelu sterowania "Wygląd").
#pragma once

namespace plat {

struct Rect { int x, y, w, h; };

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
