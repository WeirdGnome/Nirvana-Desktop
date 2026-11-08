// LisaOS - denise: widok folderu (ikony lub lista) z paskiem informacji
#pragma once

#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Widget.H>
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "anim.h"
#include "platinum.h"

struct Entry {
  std::string name, path;
  bool dir = false;
  plat::Icon icon = plat::Icon::Document;
  std::string label;       // nazwa wyświetlana (np. AppImage bez ".AppImage" i wersji)
  std::string icon_file;   // własna ikona (PNG/SVG), np. wyciągnięta z AppImage
  uintmax_t size = 0;      // rozmiar pliku (dla folderów 0)
  time_t mtime = 0;        // data modyfikacji
};

plat::Icon icon_for(const std::string &name, bool dir);
bool is_appimage(const std::string &name);
std::string kind_name(const Entry &e);   // "folder", "aplikacja", "dokument"...
// Ikona elementu w rozmiarze 32 (widok ikon) lub 16 (lista) - także do przeciągania.
void draw_entry_icon(const Entry &e, int x, int y, bool sel, int size, bool open = false);

class FolderView : public Fl_Widget {
public:
  enum Mode { Icons, List };

  FolderView(int x, int y, int w, int h);
  void load(const std::string &path, bool show_hidden);
  void relayout(int avail_w, int min_h);
  void set_mode(Mode m) { mode_ = m; }
  Mode mode() const { return mode_; }

  int count() const { return (int)entries_.size(); }
  const Entry *selected() const;                 // pierwszy zaznaczony (lub nullptr)
  std::vector<Entry> selection() const;          // wszystkie zaznaczone
  void select_all();
  void select_path(const std::string &path);     // zaznacz dokładnie ten element
  void select_paths(const std::vector<std::string> &paths);   // zaznacz te, które istnieją
  // Prostokąt ikony danego elementu (współrzędne okna), jeśli jest widoczna.
  bool icon_rect(const std::string &path, plat::Rect &out) const;
  // Prostokąt etykiety do edycji nazwy (współrzędne okna).
  bool label_rect(int index, plat::Rect &out) const;
  int index_of(const std::string &path) const;

  // Cel upuszczania podczas przeciągania: -2 brak, -1 cały folder, >=0 folder w widoku
  void set_drop_target(int t) { if (t != drop_) { drop_ = t; redraw(); } }
  int drop_target_at(int ex, int ey) const;      // ex, ey - współrzędne okna
  const Entry *entry(int i) const { return i >= 0 && i < count() ? &entries_[i] : nullptr; }

  std::function<void(const Entry &)> on_open;               // podwójne kliknięcie / Enter
  std::function<bool(const std::string &)> is_open;         // czy folder ma otwarte okno
  std::function<void(int index)> on_rename;                 // poproś o edycję nazwy
  // przeciąganie: start (zaznaczone elementy), ruch i koniec (współrzędne ekranu)
  std::function<void(const std::vector<Entry> &)> on_drag_start;
  std::function<void(int xr, int yr)> on_drag_move;
  std::function<void(int xr, int yr, bool copy)> on_drag_end;

  void draw() override;
  int handle(int e) override;

  static constexpr int CELL_W = 80, CELL_H = 66;   // widok ikon
  static constexpr int ROW_H = 18;                  // widok listy
  static constexpr int COL_DATE = 140, COL_SIZE = 70, COL_KIND = 90;
  static void set_home(const std::string &home);   // potrzebne do cache ikon AppImage

private:
  int index_at(int ex, int ey, bool *on_label = nullptr) const;
  plat::Rect item_rect(int i) const;            // obszar ikony+etykiety / wiersza
  void draw_icons();
  void draw_list();
  void select_only(int i);
  static void rename_timeout(void *d);

  std::vector<Entry> entries_;
  std::vector<char> sel_;
  Mode mode_ = Icons;
  int cols_ = 1, anchor_ = -1, drop_ = -2;
  // stan myszy
  enum { IDLE, PRESSED, DRAGGING, BANDING } state_ = IDLE;
  int press_x_ = 0, press_y_ = 0, press_index_ = -1;
  bool press_label_ = false, was_selected_ = false;
  std::vector<char> band_base_;                 // zaznaczenie sprzed prostokąta
  int band_x_ = 0, band_y_ = 0;
};

class FolderPane : public Fl_Group {
public:
  FolderPane(int x, int y, int w, int h, const std::string &path, bool show_hidden);
  void refresh();
  FolderView *view() { return view_; }
  const std::string &path() const { return path_; }
  void set_path(const std::string &p) { path_ = p; }   // folder przeniesiony / przemianowany
  void set_mode(FolderView::Mode m);
  void start_rename(int index);
  void cancel_rename();
  void resize(int X, int Y, int W, int H) override;
  void draw() override;
  int handle(int e) override;   // uchwyt zmiany rozmiaru w prawym dolnym rogu

  // zmiana nazwy zatwierdzona: (stara ścieżka, nowa nazwa) -> komunikat błędu lub ""
  std::function<std::string(const std::string &, const std::string &)> on_rename_commit;
  std::function<void(const std::string &)> on_error;

  static constexpr int INFO_H = 18, HEADER_H = 16;

private:
  int top_h() const { return INFO_H + (view_->mode() == FolderView::List ? HEADER_H : 0); }
  void commit_rename();
  static void rename_cb(Fl_Widget *, void *);
  std::string path_, info_;
  bool hidden_;
  Fl_Scroll *scroll_;
  FolderView *view_;
  Fl_Input *editor_ = nullptr;
  std::string editing_path_;
  void update_info();
  bool growing_ = false;
  int grow_dx_ = 0, grow_dy_ = 0;
};
