// LisaOS - denise: widok folderu (ikony) z paskiem informacji
#pragma once

#include <FL/Fl_Group.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Widget.H>
#include <functional>
#include <string>
#include <vector>

#include "platinum.h"

struct Entry {
  std::string name, path;
  bool dir = false;
  plat::Icon icon = plat::Icon::Document;
};

plat::Icon icon_for(const std::string &name, bool dir);
bool is_appimage(const std::string &name);

class FolderView : public Fl_Widget {
public:
  FolderView(int x, int y, int w, int h);
  void load(const std::string &path, bool show_hidden);
  void relayout(int avail_w, int min_h);
  const Entry *selected() const { return sel_ >= 0 ? &entries_[sel_] : nullptr; }
  int count() const { return (int)entries_.size(); }

  std::function<void(const Entry &)> on_open;            // podwójne kliknięcie / Enter
  std::function<bool(const std::string &)> is_open;      // czy folder ma otwarte okno

  void draw() override;
  int handle(int e) override;

  static constexpr int CELL_W = 80, CELL_H = 66;

private:
  int index_at(int ex, int ey) const;
  std::vector<Entry> entries_;
  int sel_ = -1, cols_ = 1;
};

class FolderPane : public Fl_Group {
public:
  FolderPane(int x, int y, int w, int h, const std::string &path, bool show_hidden);
  void refresh();
  FolderView *view() { return view_; }
  const std::string &path() const { return path_; }
  void resize(int X, int Y, int W, int H) override;
  void draw() override;

  static constexpr int INFO_H = 18;

private:
  std::string path_, info_;
  bool hidden_;
  Fl_Scroll *scroll_;
  FolderView *view_;
  void update_info();
};
