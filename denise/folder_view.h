// Nirvana Desktop - denise: the folder view (icons or list) with an information bar
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
  std::string label;       // display name (e.g. an AppImage without ".AppImage" and the version)
  std::string icon_file;   // own icon (PNG/SVG), e.g. extracted from an AppImage
  uintmax_t size = 0;      // file size (0 for folders)
  time_t mtime = 0;        // modification date
};

plat::Icon icon_for(const std::string &name, bool dir);
bool is_appimage(const std::string &name);
std::string kind_name(const Entry &e);   // "folder", "application", "document"...
// An item's icon at size 32 (icon view) or 16 (list) - also for dragging.
void draw_entry_icon(const Entry &e, int x, int y, bool sel, int size, bool open = false);
// The same icon as an image (e.g. for menu items); the images are cached.
// All small vector icons for the current scheme - call outside draw()
// (drawing into an Fl_Image_Surface during Fl::flush() breaks window refreshes).
void warm_small_icons();
Fl_Image *entry_icon_image(const Entry &e, int size, bool sel);

// Column layout of the list view - columns that do not fit are hidden
// (first "Kind", then "Size", the date last).
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
  void set_sort(SortKey k);          // the same field a second time: reverse order
  SortKey sort_key() const { return sort_; }
  bool sort_desc() const { return desc_; }

  FolderView(int x, int y, int w, int h);
  void load(const std::string &path, bool show_hidden);
  void relayout(int avail_w, int min_h);
  void set_mode(Mode m) { mode_ = m; }
  Mode mode() const { return mode_; }

  int count() const { return (int)entries_.size(); }
  const Entry *selected() const;                 // the first selected (or nullptr)
  std::vector<Entry> selection() const;          // all selected
  void select_all();
  void select_path(const std::string &path);     // select exactly this item
  void select_paths(const std::vector<std::string> &paths);   // select those that exist
  // Icon rectangle of the given item (window coordinates), if visible.
  bool icon_rect(const std::string &path, plat::Rect &out) const;
  // Label rectangle for renaming (window coordinates).
  bool label_rect(int index, plat::Rect &out) const;
  int index_of(const std::string &path) const;

  // Drop target while dragging: -2 none, -1 the whole folder, >=0 a folder in the view
  void set_drop_target(int t) { if (t != drop_) { drop_ = t; redraw(); } }
  int drop_target_at(int ex, int ey) const;      // ex, ey - window coordinates
  const Entry *entry(int i) const { return i >= 0 && i < count() ? &entries_[i] : nullptr; }

  std::function<void(const Entry &)> on_open;               // double click / Enter
  std::function<void()> on_context;                         // right button (context menu)
  std::function<bool(const std::string &)> is_open;         // does the folder have an open window
  std::function<void(int index)> on_rename;                 // ask to rename
  // dragging: start (selected items), move and end (screen coordinates)
  std::function<void(const std::vector<Entry> &)> on_drag_start;
  std::function<void(int xr, int yr)> on_drag_move;
  std::function<void(int xr, int yr, bool copy)> on_drag_end;
  // files dropped from another program (XDND): the file:// list and the target folder
  // (empty = this view's folder)
  std::function<void(const std::string &uris, const std::string &dest)> on_drop_uris;

  void draw() override;
  int handle(int e) override;

  // --- icon size and positions  ---
  void set_icon_size(int s);            // 16, 32 or 64
  int icon_size() const { return isize_; }
  int cell_w() const { return isize_ == 16 ? 76 : isize_ == 64 ? 104 : 80; }
  int cell_h() const { return isize_ == 16 ? 40 : isize_ == 64 ? 98 : 66; }
  // Moving icons within the window (dragging onto the background of the same window).
  void move_items(const std::vector<std::string> &paths, int dx, int dy);
  // Snapshot: positions saved permanently (~/.config/nirvana/icon-positions).
  void snapshot(const std::vector<std::string> &paths);
  void unsnapshot(const std::vector<std::string> &paths);
  void clean_up();                      // everything back into the grid
  bool is_snapshot(const std::string &path) const;
  std::vector<std::string> all_paths() const;
  int press_x() const { return press_x_; }   // where the mouse was pressed (window coordinates)
  int press_y() const { return press_y_; }

  static constexpr int CELL_W = 80, CELL_H = 66;   // icon view (32 px)
  static constexpr int ROW_H = 18;                  // list view
  static constexpr int COL_DATE = 140, COL_SIZE = 70, COL_KIND = 90;
  static void set_home(const std::string &home);   // needed for the AppImage icon cache

private:
  int index_at(int ex, int ey, bool *on_label = nullptr) const;
  plat::Rect item_rect(int i) const;            // area of the icon+label / of the row
  void draw_icons();
  void draw_list();
  void select_only(int i);
  static void rename_timeout(void *d);

  void sort_entries();
  struct Pt { int x, y; };
  Pt origin(int i) const;               // top left corner of the cell (window coordinates)
  std::vector<Pt> cells_;               // layout from relayout() (relative to the view)
  std::map<std::string, Pt> free_;      // icons placed by hand (relative to the view)
  int isize_ = 32;
  int last_w_ = 0, last_min_h_ = 0;
  SortKey sort_ = ByName;
  bool desc_ = false;
  std::vector<Entry> entries_;
  std::vector<char> sel_;
  Mode mode_ = Icons;
  int cols_ = 1, anchor_ = -1, drop_ = -2;
  // mouse state
  enum { IDLE, PRESSED, DRAGGING, BANDING } state_ = IDLE;
  int press_x_ = 0, press_y_ = 0, press_index_ = -1;
  bool press_label_ = false, was_selected_ = false;
  std::vector<char> band_base_;                 // selection from before the rubber band
  int band_x_ = 0, band_y_ = 0;
};

class FolderPane : public Fl_Group {
public:
  FolderPane(int x, int y, int w, int h, const std::string &path, bool show_hidden);
  void refresh();
  FolderView *view() { return view_; }
  const std::string &path() const { return path_; }
  void set_path(const std::string &p) { path_ = p; watch(); }   // folder moved / renamed
  void go(const std::string &p);   // show another folder in the same window (navigation)
  ~FolderPane() override;
  void set_mode(FolderView::Mode m);
  void set_hidden(bool h) { if (h != hidden_) { hidden_ = h; refresh(); } }
  void set_icon_size(int s);
  void start_rename(int index);
  void cancel_rename();
  void resize(int X, int Y, int W, int H) override;
  void draw() override;
  int handle(int e) override;   // resize handle in the bottom right corner

  // renaming confirmed: (old path, new name) -> error message or ""
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
  // watching the folder (inotify): outside changes refresh the view
  void watch();
  static void inotify_cb(int fd, void *d);
  static void refresh_later(void *d);
  int ino_fd_ = -1;
  bool growing_ = false;
  int grow_dx_ = 0, grow_dy_ = 0;
};
