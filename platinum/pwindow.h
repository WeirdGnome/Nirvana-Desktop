// LisaOS - libplatinum: okno w stylu Platinum
//
// PWindow to grupa FLTK rysująca pełne "chrome" okna Mac OS 9:
// prążkowany pasek tytułu, przycisk zamknięcia, zoom, zwijanie (windowshade)
// i uchwyt zmiany rozmiaru. Na etapie prototypu okna żyją wewnątrz biurka
// (jeden proces). Te same funkcje rysujące wykorzysta później lisaos-insight,
// rysując ramki wokół prawdziwych okien X11.
#pragma once

#include <FL/Fl_Group.H>
#include <functional>

namespace plat {

class PWindow : public Fl_Group {
public:
  PWindow(int x, int y, int w, int h, const char *title);

  Fl_Group *content() { return content_; }

  void set_active(bool a) { if (active_ != a) { active_ = a; redraw(); } }
  bool is_active() const { return active_; }
  bool shaded() const { return shaded_; }
  void set_dialog(bool d) { dialog_ = d; redraw(); }   // bez przycisków i zmiany rozmiaru
  void toggle_shade();
  void toggle_zoom();

  // Sygnały do menedżera okien (biurka).
  std::function<void(PWindow *)> on_close;   // kliknięto przycisk zamknięcia
  std::function<void(PWindow *)> on_raise;   // kliknięto w okno
  std::function<void(PWindow *)> on_geometry;// przesunięto / zmieniono rozmiar

  int handle(int e) override;
  void draw() override;
  void resize(int X, int Y, int W, int H) override;

  static constexpr int MIN_W = 200, MIN_H = 120;

private:
  enum Drag { NONE, MOVE, GROW, BUTTON };
  int hit_button(int ex, int ey) const;   // 0 brak, 1 zamknij, 2 zoom, 3 zwiń
  bool in_grow(int ex, int ey) const;
  void layout_content();

  Fl_Group *content_;
  bool active_ = true, shaded_ = false, zoomed_ = false, dialog_ = false;
  int full_h_;                        // wysokość przed zwinięciem
  int unzoom_[4] = {0, 0, 0, 0};       // geometria przed zoomem
  Drag drag_ = NONE;
  int drag_dx_ = 0, drag_dy_ = 0, armed_ = 0, pressed_ = 0;
};

} // namespace plat
