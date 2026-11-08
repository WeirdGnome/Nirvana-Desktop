// LisaOS - denise: biurko, pasek menu i zarządzanie oknami Findera
#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Tiled_Image.H>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "folder_view.h"
#include "pwindow.h"
#include "spatial.h"

class Desktop : public Fl_Double_Window {
public:
  Desktop(int W, int H, const std::string &home, bool root);

  void open_folder(const std::string &path);
  void restore();                 // otwórz okna zapamiętane z poprzedniej sesji

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

private:
  struct Win {
    plat::PWindow *win = nullptr;
    FolderPane *pane = nullptr;     // nullptr dla okien dialogowych
    std::string path;
    std::vector<std::function<void()>> actions;   // przyciski dialogu
  };
  struct DeskIcon {
    std::string name, path;
    plat::Icon icon;
    int x, y;
  };

  Win *find(plat::PWindow *w);
  Win *find(const std::string &path);
  Win *top();
  void raise(plat::PWindow *w);
  void close(plat::PWindow *w);
  void remember(Win &w, bool open);
  void wire(plat::PWindow *w);
  void layout_icons();
  int icon_at(int ex, int ey) const;
  bool allowed(const std::string &path) const;
  std::string title_for(const std::string &path) const;
  std::string trash_dir() const;
  bool trash_full() const;
  void open_entry(const Entry &e);
  void dialog(const std::string &title, const std::string &text,
              std::vector<std::pair<std::string, std::function<void()>>> buttons);
  static void dialog_cb(Fl_Widget *b, void *idx);

  std::string home_, root_path_;
  bool root_;
  Spatial spatial_;
  std::vector<Win> wins_;
  std::vector<DeskIcon> icons_;
  int icon_sel_ = -1;
  Fl_Menu_Bar *menu_;
  Fl_Widget *extras_;
  std::unique_ptr<Fl_Tiled_Image> pattern_;
};
