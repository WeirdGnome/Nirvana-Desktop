// Nirvana Desktop - denise: widok folderu (ikony lub lista) z paskiem informacji
#pragma once

#include <FL/Fl_Group.H>
#include <FL/Fl_Image.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Widget.H>
#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
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
// Ta sama ikona jako obraz (np. do pozycji menu); obrazy są w pamięci podręcznej.
// Wszystkie małe ikony wektorowe dla bieżącego schematu - wołać poza draw()
// (rysowanie do Fl_Image_Surface w trakcie Fl::flush() psuje odświeżanie okien).
void warm_small_icons();
Fl_Image *entry_icon_image(const Entry &e, int size, bool sel);

// Układ kolumn widoku listy - kolumny, które się nie mieszczą, są chowane
// (najpierw "Rodzaj", potem "Rozmiar", na końcu data).
struct ListCols {
  int name_x, name_w;
  bool date, size, kind;
  int date_x, size_x, kind_x;
};
ListCols list_cols(int x0, int w);

class FolderView : public Fl_Widget {
public:
  enum Mode { Icons, List };
  enum SortKey { ByName, ByDate, BySize, ByKind };
  void set_sort(SortKey k);          // to samo pole drugi raz: odwrotna kolejność
  SortKey sort_key() const { return sort_; }
  bool sort_desc() const { return desc_; }

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
  // pliki upuszczone z innego programu (XDND): lista file:// i folder docelowy
  // (pusty = folder tego widoku)
  std::function<void(const std::string &uris, const std::string &dest)> on_drop_uris;

  void draw() override;
  int handle(int e) override;

  // --- wielkość ikon i ich położenie (jak w Workbenchu) ---
  void set_icon_size(int s);            // 16, 32 albo 64
  int icon_size() const { return isize_; }
  int cell_w() const { return isize_ == 16 ? 76 : isize_ == 64 ? 104 : 80; }
  int cell_h() const { return isize_ == 16 ? 40 : isize_ == 64 ? 98 : 66; }
  // Przesunięcie ikon w obrębie okna (przeciąganie na tło tego samego okna).
  void move_items(const std::vector<std::string> &paths, int dx, int dy);
  // Zamrożenie: pozycje zapisane na stałe (~/.config/nirvana/ikony.pozycje).
  void snapshot(const std::vector<std::string> &paths);
  void unsnapshot(const std::vector<std::string> &paths);
  void clean_up();                      // wszystko z powrotem do siatki
  bool is_snapshot(const std::string &path) const;
  std::vector<std::string> all_paths() const;
  int press_x() const { return press_x_; }   // gdzie wciśnięto mysz (współrzędne okna)
  int press_y() const { return press_y_; }

  static constexpr int CELL_W = 80, CELL_H = 66;   // widok ikon (32 px)
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

  void sort_entries();
  struct Pt { int x, y; };
  Pt origin(int i) const;               // lewy górny róg komórki (współrzędne okna)
  std::vector<Pt> cells_;               // ułożenie z relayout() (względem widoku)
  std::map<std::string, Pt> free_;      // ikony postawione ręcznie (względem widoku)
  int isize_ = 32;
  int last_w_ = 0, last_min_h_ = 0;
  SortKey sort_ = ByName;
  bool desc_ = false;
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
  void set_path(const std::string &p) { path_ = p; watch(); }   // folder przeniesiony / przemianowany
  void go(const std::string &p);   // pokaż inny folder w tym samym oknie (nawigacja)
  ~FolderPane() override;
  void set_mode(FolderView::Mode m);
  void set_hidden(bool h) { if (h != hidden_) { hidden_ = h; refresh(); } }
  void set_icon_size(int s);
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
  // obserwacja folderu (inotify): zmiany z zewnątrz odświeżają widok
  void watch();
  static void inotify_cb(int fd, void *d);
  static void refresh_later(void *d);
  int ino_fd_ = -1;
  bool growing_ = false;
  int grow_dx_ = 0, grow_dy_ = 0;
};
