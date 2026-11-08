// LisaOS - denise: biurko, pasek menu i okna menedżera plików (Rambler)
//
// Biurko (wzór, ikony dysku i kosza, globalny pasek menu) to jedno okno X
// typu DESKTOP. Każdy folder i każdy dialog to osobne okno X - ramki,
// przesuwanie, zwijanie i fokus zapewnia menedżer okien (Insight).
// Pasek menu działa na aktywne okno, które odczytujemy z _NET_ACTIVE_WINDOW.
#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Tiled_Image.H>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "anim.h"
#include "folder_view.h"
#include "spatial.h"

class Desktop;

// Okno najwyższego poziomu Ramblera: folder albo dialog.
class NWin : public Fl_Double_Window {
public:
  NWin(Desktop *d, int X, int Y, int W, int H, const char *title, bool dialog);

  Desktop *desk;
  FolderPane *pane = nullptr;                  // nullptr dla dialogów
  std::string path;
  std::vector<std::function<void()>> actions;  // akcje przycisków dialogu
  bool dialog;

  bool is_active();     // czy to okno ma fokus (wg menedżera okien)
  bool shaded();       // czy menedżer okien je zwinął
  void set_shaded(bool s);    // poproś menedżer okien o zwinięcie/rozwinięcie
  void activate_window();     // wyciągnij na wierzch i daj fokus
  plat::Rect frame_rect();   // przybliżony obrys z ramką (do animacji)

  ~NWin() override;
  int handle(int e) override;   // skróty klawiszowe trafiają do globalnego paska menu
  void resize(int X, int Y, int W, int H) override;
};

class Desktop : public Fl_Double_Window {
public:
  Desktop(int W, int H, const std::string &home, bool root);

  void open_folder(const std::string &path, bool animate = true);
  void restore();                 // otwórz okna zapamiętane z poprzedniej sesji
  void close(NWin *w);            // zamknięcie okna (przycisk ramki, Ctrl+W, dialog)
  void remember(NWin *w, bool open);
  void active_changed();          // menedżer okien zmienił aktywne okno
  int shortcut();                 // skrót z innego okna -> pasek menu

  int handle(int e) override;
  void draw() override;

  // akcje menu
  void about();
  void new_folder();
  void open_selected();
  void close_active();
  void move_to_trash();
  void empty_trash();
  void ask_root();
  void refresh_all();
  void quit();
  void build_windows_menu(Fl_Menu_Button *m);
  void shade_all(bool shade);
  void close_all();

private:
  struct DeskIcon {
    std::string name, path;
    plat::Icon icon;
    int x, y;
  };

  NWin *find(const std::string &path);
  bool known(NWin *w) const;
  NWin *active_folder();          // aktywne okno folderu (lub nullptr)
  void layout_icons();
  int icon_at(int ex, int ey) const;
  bool allowed(const std::string &path) const;
  bool source_rect(const std::string &path, plat::Rect &out);  // ikona, z której "wylatuje" okno
  plat::Rect to_screen(plat::Rect r) const { return {r.x + x_root(), r.y + y_root(), r.w, r.h}; }
  std::string title_for(const std::string &path) const;
  std::string trash_dir() const;
  bool trash_full() const;
  void open_entry(const Entry &e);
  void open_appimage(const std::string &path);
  void run_app(const std::string &path);
  std::string apps_dir() const;
  void dialog(const std::string &title, const std::string &text,
              std::vector<std::pair<std::string, std::function<void()>>> buttons);
  static void dialog_cb(Fl_Widget *b, void *idx);

  std::string home_, root_path_;
  bool root_;
  Spatial spatial_;
  std::vector<NWin *> wins_;      // kolejność: ostatnio aktywne na końcu
  std::vector<DeskIcon> icons_;
  int icon_sel_ = -1;
  Fl_Menu_Bar *menu_;
  Fl_Widget *extras_;
  std::unique_ptr<Fl_Tiled_Image> pattern_;
};
