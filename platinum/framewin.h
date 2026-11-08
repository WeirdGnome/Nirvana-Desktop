// LisaOS - libplatinum: okno z ramką Platinum rysowaną przez program (CSD)
//
// Pod Insightem okna dostają ramkę od menedżera okien. Pod każdym innym
// (GNOME, KDE, Openbox...) FrameWindow wyłącza obce dekoracje i rysuje ramkę
// Platinum sam: pasek tytułu z przyciskami zamknij i zwiń, przesuwanie za pasek
// (przez _NET_WM_MOVERESIZE, gdy menedżer to umie), zwijanie dwuklikiem.
//
// Użycie: budujemy treść jak w zwykłym oknie, we współrzędnych od (0,0),
// a po end() wołamy finish() - przy własnej ramce przesuwa ona treść do środka
// ramki i powiększa okno.
#pragma once

#include <FL/Fl_Double_Window.H>

namespace plat {

class FrameWindow : public Fl_Double_Window {
public:
  // dialog=true: okno dialogowe - bez przycisków na pasku, nie da się zwinąć
  FrameWindow(int W, int H, const char *title, bool dialog = false);
  ~FrameWindow() override;
  void finish();                       // po end(): ramka i przesunięcie treści
  bool csd() const { return csd_; }
  int ox() const { return csd_ ? FRAME_W : 0; }
  int oy() const { return csd_ ? TITLE_W : 0; }
  // położenie treści na ekranie (do wyśrodkowania okien dialogowych)
  void place(int X, int Y) { position(X - ox(), Y - oy()); }
  int content_w() const { return w() - 2 * ox(); }
  int content_h() const { return full_h_ - oy() - ox(); }

  int handle(int e) override;
  void draw() override;

  static bool wm_is_insight();         // czy ramki zapewnia Insight

private:
  static constexpr int FRAME_W = 5, TITLE_W = 20;   // = plat::FRAME, plat::TITLE_H
  bool active();
  int hit(int ex, int ey) const;
  void toggle_shade();
  void start_move();
  bool csd_, dialog_, shaded_ = false;
  int full_h_ = 0;
  enum { NONE, PENDING, MOVE, BTN } drag_ = NONE;
  int press_x_ = 0, press_y_ = 0, dx_ = 0, dy_ = 0, armed_ = 0, pressed_ = 0;
};

}  // namespace plat
