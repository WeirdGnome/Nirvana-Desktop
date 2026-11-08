// LisaOS - denise: widok folderu (ikony lub lista) z paskiem informacji
#include "folder_view.h"

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <strings.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "appimage.h"

namespace fs = std::filesystem;
using namespace plat;

static bool ends_with_ci(const std::string &s, const char *suf) {
  size_t n = strlen(suf);
  return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, suf) == 0;
}

bool is_appimage(const std::string &name) { return ends_with_ci(name, ".appimage"); }

Icon icon_for(const std::string &name, bool dir) {
  if (dir) return Icon::Folder;
  if (is_appimage(name)) return Icon::Application;
  for (auto e : {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".iff", ".svg"})
    if (ends_with_ci(name, e)) return Icon::Image;
  for (auto e : {".mp3", ".ogg", ".flac", ".wav", ".opus", ".mod", ".xm", ".s3m"})
    if (ends_with_ci(name, e)) return Icon::Music;
  return Icon::Document;
}

std::string kind_name(const Entry &e) {
  if (e.dir) return "folder";
  switch (e.icon) {
  case Icon::Application: return "aplikacja";
  case Icon::Image: return "obraz";
  case Icon::Music: return "dźwięk";
  default: return "dokument";
  }
}

// "8 lis 2026, 21:40"
static std::string polish_date(time_t t) {
  static const char *mon[] = {"sty", "lut", "mar", "kwi", "maj", "cze",
                              "lip", "sie", "wrz", "paź", "lis", "gru"};
  struct tm tm;
  localtime_r(&t, &tm);
  char buf[64];
  snprintf(buf, sizeof buf, "%d %s %d, %02d:%02d", tm.tm_mday, mon[tm.tm_mon], tm.tm_year + 1900,
           tm.tm_hour, tm.tm_min);
  return buf;
}

// "12 KB", "3,4 MB"
static std::string human_size(uintmax_t b) {
  if (b < 1024) return std::to_string(b) + " B";
  const char *u[] = {"KB", "MB", "GB", "TB"};
  double v = (double)b;
  int i = -1;
  do { v /= 1024; i++; } while (v >= 1024 && i < 3);
  char buf[32];
  if (v < 10) snprintf(buf, sizeof buf, "%.1f %s", v, u[i]);
  else snprintf(buf, sizeof buf, "%.0f %s", v, u[i]);
  for (char *p = buf; *p; p++) if (*p == '.') *p = ',';
  return buf;
}

// ---------------------------------------------------------------------------
// Ikony
// ---------------------------------------------------------------------------
static std::string g_home;
void FolderView::set_home(const std::string &home) { g_home = home; }

// Ikony z plików (np. z AppImage): normalna i przyciemniona, w danym rozmiarze.
struct IconPair { Fl_Image *normal = nullptr, *selected = nullptr; };
static IconPair file_icon(const std::string &file, int size) {
  static std::map<std::string, IconPair> cache;
  static bool registered = false;
  if (!registered) { fl_register_images(); registered = true; }
  std::string key = file + "@" + std::to_string(size);
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  IconPair p;
  Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
  if (Fl_Shared_Image *img = Fl_Shared_Image::get(file.c_str())) {
    if (img->w() > 0 && img->h() > 0) {
      // Kopiujemy obraz źródłowy (nie Fl_Shared_Image, które dzieli dane między
      // kopiami), więc przyciemnienie wersji "zaznaczonej" nie psuje normalnej.
      const Fl_Image *base = img->image() ? img->image() : img;
      p.normal = base->copy(size, size);
      if (auto *r = dynamic_cast<Fl_RGB_Image *>(p.normal)) r->normalize();   // SVG
      p.selected = base->copy(size, size);
      if (auto *r = dynamic_cast<Fl_RGB_Image *>(p.selected)) r->normalize();
      p.selected->color_average(FL_BLACK, 0.5f);
    }
    img->release();
  }
  cache[key] = p;
  return p;
}

// Ikony wektorowe 16x16 do widoku listy: rysujemy 32x32 do obrazu i zmniejszamy.
static Fl_Image *small_icon(Icon icon, bool sel) {
  static std::map<int, Fl_Image *> cache;
  static Fl_Color bg = 0;   // ikony rysujemy na tle treści - po zmianie schematu od nowa
  if (Fl::get_color(CONTENT) != bg) {
    for (auto &kv : cache) delete kv.second;
    cache.clear();
    bg = Fl::get_color(CONTENT);
  }
  int key = (int)icon * 2 + sel;
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  Fl_Image_Surface surf(32, 32);
  Fl_Surface_Device::push_current(&surf);
  fl_color(CONTENT);
  fl_rectf(0, 0, 32, 32);
  draw_icon(icon, 0, 0, sel);
  Fl_Surface_Device::pop_current();
  Fl_RGB_Image *big = surf.image();
  Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
  Fl_Image *img = big->copy(16, 16);
  delete big;
  cache[key] = img;
  return img;
}

Fl_Image *entry_icon_image(const Entry &e, int size, bool sel) {
  if (!e.icon_file.empty()) {
    IconPair fi = file_icon(e.icon_file, size);
    if (fi.normal) return sel ? fi.selected : fi.normal;
  }
  return small_icon(e.icon, sel);   // wektorowa ikona 16x16
}

void draw_entry_icon(const Entry &e, int x, int y, bool sel, int size, bool open) {
  Icon ic = (e.dir && open) ? Icon::OpenFolder : e.icon;
  IconPair fi;
  if (!e.icon_file.empty()) fi = file_icon(e.icon_file, size);
  if (fi.normal) { (sel ? fi.selected : fi.normal)->draw(x, y); return; }
  if (size == 32) draw_icon(ic, x, y, sel);
  else small_icon(ic, sel)->draw(x, y);
}

// ---------------------------------------------------------------------------
FolderView::FolderView(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}

void FolderView::load(const std::string &path, bool show_hidden) {
  entries_.clear();
  std::error_code ec;
  for (auto &de : fs::directory_iterator(path, fs::directory_options::skip_permission_denied, ec)) {
    std::string name = de.path().filename().string();
    if (!show_hidden && !name.empty() && name[0] == '.') continue;
    Entry e;
    e.name = name;
    e.path = de.path().string();
    std::error_code ec2;
    e.dir = de.is_directory(ec2);
    e.icon = icon_for(name, e.dir);
    e.label = name;
    struct stat st;
    if (stat(e.path.c_str(), &st) == 0) {
      e.mtime = st.st_mtime;
      if (!e.dir) e.size = (uintmax_t)st.st_size;
    }
    if (!e.dir && is_appimage(name)) {
      e.label = appimage::display_name(name);
      if (!g_home.empty()) e.icon_file = appimage::icon(e.path, g_home);
    }
    entries_.push_back(std::move(e));
  }
  sort_entries();
  sel_.assign(entries_.size(), 0);
  anchor_ = -1;
  drop_ = -2;
  state_ = IDLE;
  Fl::remove_timeout(rename_timeout, this);
  redraw();
}

// Mac OS sortuje alfabetycznie bez wyróżniania folderów; w liście można
// sortować także po dacie, rozmiarze i rodzaju (remis: po nazwie).
void FolderView::sort_entries() {
  SortKey k = sort_;
  bool desc = desc_;
  std::stable_sort(entries_.begin(), entries_.end(), [k, desc](const Entry &a, const Entry &b) {
    int c = 0;
    if (k == ByDate) c = a.mtime < b.mtime ? -1 : a.mtime > b.mtime;
    else if (k == BySize) c = a.size < b.size ? -1 : a.size > b.size;
    else if (k == ByKind) c = kind_name(a).compare(kind_name(b));
    if (c == 0) c = strcasecmp(a.label.c_str(), b.label.c_str());
    return desc ? c > 0 : c < 0;
  });
}

void FolderView::set_sort(SortKey k) {
  if (k == sort_) desc_ = !desc_;
  else { sort_ = k; desc_ = (k == ByDate || k == BySize); }   // najnowsze / największe na górze
  std::vector<std::string> keep;
  for (auto &e : selection()) keep.push_back(e.path);
  sort_entries();
  select_paths(keep);
}

void FolderView::relayout(int avail_w, int min_h) {
  if (mode_ == Icons) {
    cols_ = std::max(1, (avail_w - 8) / CELL_W);
    int rows = ((int)entries_.size() + cols_ - 1) / cols_;
    size(avail_w, std::max(min_h, rows * CELL_H + 12));
  } else {
    cols_ = 1;
    size(avail_w, std::max(min_h, (int)entries_.size() * ROW_H + 4));
  }
}

// --- geometria -------------------------------------------------------------
ListCols list_cols(int x0, int w) {
  ListCols c;
  int avail = w - 8, min_name = 140;
  c.kind = avail - FolderView::COL_DATE - FolderView::COL_SIZE - FolderView::COL_KIND >= min_name;
  c.size = avail - FolderView::COL_DATE - FolderView::COL_SIZE >= min_name;
  c.date = avail - FolderView::COL_DATE >= min_name;
  c.name_x = x0;
  c.name_w = avail - (c.date ? FolderView::COL_DATE : 0) - (c.size ? FolderView::COL_SIZE : 0) -
             (c.kind ? FolderView::COL_KIND : 0);
  c.date_x = c.name_x + c.name_w;
  c.size_x = c.date_x + (c.date ? FolderView::COL_DATE : 0);
  c.kind_x = c.size_x + (c.size ? FolderView::COL_SIZE : 0);
  return c;
}

static int name_col_w(int w) { return list_cols(0, w).name_w; }

Rect FolderView::item_rect(int i) const {
  if (mode_ == Icons) {
    int cx = x() + 4 + (i % cols_) * CELL_W, cy = y() + 6 + (i / cols_) * CELL_H;
    return {cx + 4, cy, CELL_W - 8, 54};
  }
  return {x() + 4, y() + 2 + i * ROW_H, name_col_w(w()), ROW_H};
}

int FolderView::index_at(int ex, int ey, bool *on_label) const {
  if (on_label) *on_label = false;
  if (mode_ == List) {
    int i = (ey - y() - 2) / ROW_H;
    if (ey < y() + 2 || i < 0 || i >= (int)entries_.size()) return -1;
    if (ex < x() + 4 || ex >= x() + 4 + name_col_w(w())) return -1;   // tylko kolumna nazwy
    if (on_label) *on_label = ex >= x() + 26;
    return i;
  }
  int cx = (ex - x() - 4) / CELL_W, cy = (ey - y() - 6) / CELL_H;
  if (ex < x() + 4 || ey < y() + 6 || cx >= cols_) return -1;
  int i = cy * cols_ + cx;
  if (i < 0 || i >= (int)entries_.size()) return -1;
  // trafienie liczymy tylko w ikonę i etykietę, nie w całą komórkę
  int lx = ex - (x() + 4 + cx * CELL_W), ly = ey - (y() + 6 + cy * CELL_H);
  bool on_icon = lx >= (CELL_W - 32) / 2 && lx < (CELL_W + 32) / 2 && ly < 36;
  bool lbl = ly >= 36 && ly < 54;
  if (on_label) *on_label = lbl;
  return (on_icon || lbl) ? i : -1;
}

int FolderView::index_of(const std::string &path) const {
  for (int i = 0; i < (int)entries_.size(); i++) if (entries_[i].path == path) return i;
  return -1;
}

static bool visible_in_parent(const Fl_Widget *w, int X, int Y, int W, int H) {
  const Fl_Widget *p = w->parent();
  if (!p) return true;
  return !(Y + H <= p->y() || Y >= p->y() + p->h() - SCROLL_W ||
           X + W <= p->x() || X >= p->x() + p->w() - SCROLL_W);
}

bool FolderView::icon_rect(const std::string &path, Rect &out) const {
  int i = index_of(path);
  if (i < 0) return false;
  if (mode_ == Icons) {
    int cx = x() + 4 + (i % cols_) * CELL_W + (CELL_W - 32) / 2;
    int cy = y() + 6 + (i / cols_) * CELL_H + 2;
    out = {cx, cy, 32, 32};
  } else {
    out = {x() + 6, y() + 3 + i * ROW_H, 16, 16};
  }
  return visible_in_parent(this, out.x, out.y, out.w, out.h) && visible_r();
}

bool FolderView::label_rect(int i, Rect &out) const {
  if (i < 0 || i >= count()) return false;
  if (mode_ == Icons) {
    int cx = x() + 4 + (i % cols_) * CELL_W, cy = y() + 6 + (i / cols_) * CELL_H;
    out = {cx - 6, cy + 35, CELL_W + 12, 18};
  } else {
    out = {x() + 24, y() + 2 + i * ROW_H, name_col_w(w()) - 24, ROW_H};
  }
  return visible_in_parent(this, out.x, out.y, out.w, out.h);
}

int FolderView::drop_target_at(int ex, int ey) const {
  int i = index_at(ex, ey);
  if (i >= 0 && entries_[i].dir && !sel_[i]) return i;   // na folder (nie na samego siebie)
  return -1;                                                // do tego folderu
}

// --- zaznaczenie -------------------------------------------------------------
const Entry *FolderView::selected() const {
  for (int i = 0; i < count(); i++) if (sel_[i]) return &entries_[i];
  return nullptr;
}

std::vector<Entry> FolderView::selection() const {
  std::vector<Entry> v;
  for (int i = 0; i < count(); i++) if (sel_[i]) v.push_back(entries_[i]);
  return v;
}

void FolderView::select_only(int i) {
  std::fill(sel_.begin(), sel_.end(), 0);
  if (i >= 0 && i < count()) sel_[i] = 1;
  anchor_ = i;
  redraw();
}

void FolderView::select_all() {
  std::fill(sel_.begin(), sel_.end(), 1);
  redraw();
}

void FolderView::select_path(const std::string &path) { select_only(index_of(path)); }

void FolderView::select_paths(const std::vector<std::string> &paths) {
  std::fill(sel_.begin(), sel_.end(), 0);
  for (auto &p : paths) if (int i = index_of(p); i >= 0) { sel_[i] = 1; anchor_ = i; }
  redraw();
}

// --- rysowanie ---------------------------------------------------------------
void FolderView::draw() {
  fl_color(CONTENT);
  fl_rectf(x(), y(), w(), h());
  if (mode_ == Icons) draw_icons(); else draw_list();

  // prostokąt zaznaczania (kropkowany, jak w Finderze)
  if (state_ == BANDING) {
    int x0 = std::min(band_x_, press_x_), y0 = std::min(band_y_, press_y_);
    int x1 = std::max(band_x_, press_x_), y1 = std::max(band_y_, press_y_);
    fl_color(SHADOW);
    fl_line_style(FL_DOT);
    fl_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    fl_line_style(0);
  }
  // cały folder jest celem upuszczania: gruba ramka wokół widocznej części
  if (drop_ == -1 && parent()) {
    const Fl_Widget *p = parent();
    fl_color(CONTENT_TEXT);
    fl_rect(p->x() + 1, p->y() + 1, p->w() - SCROLL_W - 2, p->h() - SCROLL_W - 2);
    fl_rect(p->x() + 2, p->y() + 2, p->w() - SCROLL_W - 4, p->h() - SCROLL_W - 4);
  }
}

void FolderView::draw_icons() {
  fl_font(FONT_SMALL, SIZE_SMALL);
  for (int i = 0; i < count(); i++) {
    const Entry &e = entries_[i];
    int cx = x() + 4 + (i % cols_) * CELL_W, cy = y() + 6 + (i / cols_) * CELL_H;
    bool sel = sel_[i] || drop_ == i;
    bool open = e.dir && is_open && is_open(e.path);
    draw_entry_icon(e, cx + (CELL_W - 32) / 2, cy + 2, sel, 32, open);

    std::string lbl = ellipsize(e.label, CELL_W - 6);
    int tw = (int)fl_width(lbl.c_str());
    int tx = cx + (CELL_W - tw) / 2, ty = cy + 38;
    if (sel_[i]) {
      fl_color(ICON_SEL);
      fl_rectf(tx - 2, ty, tw + 4, fl_height());
      fl_color(ICON_SEL_TEXT);
    } else {
      fl_color(CONTENT_TEXT);
    }
    fl_draw(lbl.c_str(), tx, ty + fl_height() - fl_descent());
  }
}

void FolderView::draw_list() {
  fl_font(FONT_SMALL, SIZE_SMALL);
  ListCols lc = list_cols(x() + 4, w());
  int nw = lc.name_w, xd = lc.date_x, xs = lc.size_x, xk = lc.kind_x;
  for (int i = 0; i < count(); i++) {
    const Entry &e = entries_[i];
    int ry = y() + 2 + i * ROW_H;
    if (ry + ROW_H < y() || ry > y() + h()) continue;
    bool sel = sel_[i] || drop_ == i;
    bool open = e.dir && is_open && is_open(e.path);
    draw_entry_icon(e, x() + 6, ry + 1, sel, 16, open);
    int base = ry + ROW_H / 2 + fl_height() / 2 - fl_descent();
    std::string name = ellipsize(e.label, nw - 32);
    int tw = (int)fl_width(name.c_str());
    if (sel_[i]) {
      fl_color(ICON_SEL);
      fl_rectf(x() + 25, ry + 2, tw + 4, ROW_H - 3);
      fl_color(ICON_SEL_TEXT);
    } else {
      fl_color(CONTENT_TEXT);
    }
    fl_draw(name.c_str(), x() + 27, base);
    fl_color(CONTENT_TEXT);
    if (lc.date) fl_draw(ellipsize(polish_date(e.mtime), COL_DATE - 8).c_str(), xd + 4, base);
    if (lc.size) {
      std::string sz = e.dir ? "—" : human_size(e.size);
      fl_draw(sz.c_str(), xs + COL_SIZE - 8 - (int)fl_width(sz.c_str()), base);
    }
    if (lc.kind) fl_draw(ellipsize(kind_name(e), COL_KIND - 8).c_str(), xk + 4, base);
  }
}

// --- zdarzenia ---------------------------------------------------------------
void FolderView::rename_timeout(void *d) {
  auto *v = (FolderView *)d;
  if (v->state_ != IDLE) return;
  int n = 0, idx = -1;
  for (int i = 0; i < v->count(); i++) if (v->sel_[i]) { n++; idx = i; }
  if (n == 1 && v->on_rename) v->on_rename(idx);
}

int FolderView::handle(int e) {
  switch (e) {
  case FL_PUSH: {
    Fl::focus(this);
    Fl::remove_timeout(rename_timeout, this);
    bool on_label = false;
    int i = index_at(Fl::event_x(), Fl::event_y(), &on_label);
    press_x_ = Fl::event_x(); press_y_ = Fl::event_y();
    bool mod = Fl::event_state() & (FL_SHIFT | FL_CTRL | FL_META);
    if (i >= 0 && Fl::event_clicks() && on_open) {
      Fl::event_clicks(0);
      select_only(i);
      state_ = IDLE;
      Entry copy = entries_[i];   // kopia - callback może przebudować widok
      on_open(copy);
      return 1;
    }
    if (i >= 0) {
      was_selected_ = sel_[i];
      if (mod) sel_[i] = !sel_[i];
      else if (!sel_[i]) select_only(i);
      anchor_ = i;
      state_ = PRESSED;
      press_index_ = i;
      press_label_ = on_label;
    } else {
      if (!mod) std::fill(sel_.begin(), sel_.end(), 0);
      band_base_ = sel_;
      band_x_ = press_x_; band_y_ = press_y_;
      state_ = BANDING;
    }
    redraw();
    return 1;
  }
  case FL_DRAG:
    if (state_ == PRESSED) {
      int moved = abs(Fl::event_x() - press_x_) + abs(Fl::event_y() - press_y_);
      if (moved > 4 && press_index_ >= 0 && sel_[press_index_]) {
        state_ = DRAGGING;
        if (on_drag_start) on_drag_start(selection());
      }
    }
    if (state_ == DRAGGING) {
      if (on_drag_move) on_drag_move(Fl::event_x_root(), Fl::event_y_root());
      return 1;
    }
    if (state_ == BANDING) {
      band_x_ = Fl::event_x(); band_y_ = Fl::event_y();
      int x0 = std::min(band_x_, press_x_), y0 = std::min(band_y_, press_y_);
      int x1 = std::max(band_x_, press_x_), y1 = std::max(band_y_, press_y_);
      for (int i = 0; i < count(); i++) {
        Rect r = item_rect(i);
        bool hit = r.x < x1 && r.x + r.w > x0 && r.y < y1 && r.y + r.h > y0;
        sel_[i] = band_base_[i] || hit;
      }
      redraw();
      return 1;
    }
    return 1;
  case FL_RELEASE: {
    bool mod = Fl::event_state() & (FL_SHIFT | FL_CTRL | FL_META);
    if (state_ == DRAGGING) {
      state_ = IDLE;
      bool copy = Fl::event_state() & (FL_CTRL | FL_ALT);
      if (on_drag_end) on_drag_end(Fl::event_x_root(), Fl::event_y_root(), copy);
      return 1;
    }
    if (state_ == PRESSED && !mod && press_index_ >= 0) {
      // klik w już zaznaczony element bez przeciągania
      int n = 0;
      for (char s : sel_) n += s;
      if (n > 1) select_only(press_index_);
      else if (was_selected_ && press_label_)
        Fl::add_timeout(0.6, rename_timeout, this);   // jak w Finderze: chwila i edycja nazwy
    }
    state_ = IDLE;
    redraw();
    return 1;
  }
  case FL_FOCUS:
  case FL_UNFOCUS:
    return 1;
  // --- pliki przeciągane z innych programów (XDND) ---
  case FL_DND_ENTER:
    return on_drop_uris ? 1 : 0;
  case FL_DND_DRAG:
    set_drop_target(drop_target_at(Fl::event_x(), Fl::event_y()));
    return 1;
  case FL_DND_LEAVE:
    set_drop_target(-2);
    return 1;
  case FL_DND_RELEASE:
    return 1;
  case FL_PASTE: {
    std::string dest;
    if (drop_ >= 0 && drop_ < count()) dest = entries_[drop_].path;
    set_drop_target(-2);
    std::string text = Fl::event_text() ? Fl::event_text() : "";
    if (on_drop_uris && !text.empty()) on_drop_uris(text, dest);
    return 1;
  }
  case FL_KEYBOARD: {
    int k = Fl::event_key(), n = count();
    if (!n) return 0;
    if (k == FL_F + 2) {
      if (const Entry *s = selected()) if (on_rename) on_rename(index_of(s->path));
      return 1;
    }
    if ((k == FL_Enter || k == FL_KP_Enter) && on_open) {
      if (const Entry *s = selected()) { Entry copy = *s; on_open(copy); }
      return 1;
    }
    int s = anchor_ < 0 ? 0 : anchor_;
    int step = mode_ == Icons ? cols_ : 1;
    if (k == FL_Right && mode_ == Icons) s = std::min(n - 1, s + 1);
    else if (k == FL_Left && mode_ == Icons) s = std::max(0, s - 1);
    else if (k == FL_Down) s = std::min(n - 1, s + step);
    else if (k == FL_Up) s = std::max(0, s - step);
    else return 0;
    select_only(s);
    return 1;
  }
  }
  return Fl_Widget::handle(e);
}

// ---------------------------------------------------------------------------
// FolderPane
// ---------------------------------------------------------------------------
FolderPane::FolderPane(int X, int Y, int W, int H, const std::string &path, bool hidden)
    : Fl_Group(X, Y, W, H), path_(path), hidden_(hidden) {
  box(FL_NO_BOX);
  scroll_ = new Fl_Scroll(X, Y + INFO_H, W, H - INFO_H);
  scroll_->type(Fl_Scroll::BOTH_ALWAYS);
  scroll_->box(FL_FLAT_BOX);
  scroll_->color(CONTENT);
  scroll_->scrollbar_size(SCROLL_W);
  for (Fl_Scrollbar *sb : {&scroll_->scrollbar, &scroll_->hscrollbar}) {
    sb->box(TRACK_BOX);
    sb->slider(THUMB_BOX);
    sb->color(MID);
    sb->selection_color(HILITE);
  }
  view_ = new FolderView(X, Y + INFO_H, W - SCROLL_W, H - INFO_H - SCROLL_W);
  scroll_->end();
  end();
  view_->on_rename = [this](int i) { start_rename(i); };
  refresh();
  watch();
}

void FolderPane::update_info() {
  struct statvfs st;
  char buf[64] = "";
  if (statvfs(path_.c_str(), &st) == 0) {
    double gb = (double)st.f_bavail * st.f_frsize / 1e9;
    snprintf(buf, sizeof buf, "%.1f GB wolne", gb);
    for (char *p = buf; *p; p++) if (*p == '.') { *p = ','; break; }
  }
  info_ = polish_items(view_->count()) + (buf[0] ? std::string(", ") + buf : "");
}

// --- obserwacja zmian w folderze -------------------------------------------
void FolderPane::watch() {
  if (ino_fd_ >= 0) { Fl::remove_fd(ino_fd_); close(ino_fd_); ino_fd_ = -1; }
  ino_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (ino_fd_ < 0) return;
  uint32_t mask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB;
  if (inotify_add_watch(ino_fd_, path_.c_str(), mask) < 0) { close(ino_fd_); ino_fd_ = -1; return; }
  Fl::add_fd(ino_fd_, FL_READ, inotify_cb, this);
}

void FolderPane::inotify_cb(int fd, void *d) {
  char buf[4096];
  while (read(fd, buf, sizeof buf) > 0) {}   // tylko odbieramy; szczegóły nie są potrzebne
  // zbieramy serię zmian (np. kopiowanie wielu plików) w jedno odświeżenie
  Fl::remove_timeout(refresh_later, d);
  Fl::add_timeout(0.25, refresh_later, d);
}

void FolderPane::refresh_later(void *d) {
  auto *p = (FolderPane *)d;
  if (p->editor_) { Fl::add_timeout(0.5, refresh_later, d); return; }   // nie przerywaj zmiany nazwy
  p->refresh();
}

FolderPane::~FolderPane() {
  Fl::remove_timeout(refresh_later, this);
  if (ino_fd_ >= 0) { Fl::remove_fd(ino_fd_); close(ino_fd_); }
}

void FolderPane::refresh() {
  cancel_rename();
  // zachowaj zaznaczenie po odświeżeniu (np. po zmianie nazwy innego pliku)
  std::vector<std::string> keep;
  for (auto &e : view_->selection()) keep.push_back(e.path);
  view_->load(path_, hidden_);
  view_->select_paths(keep);
  update_info();
  resize(x(), y(), w(), h());
  redraw();
}

void FolderPane::set_mode(FolderView::Mode m) {
  if (m == view_->mode()) return;
  cancel_rename();
  view_->set_mode(m);
  scroll_->scroll_to(0, 0);
  resize(x(), y(), w(), h());
  redraw();
}

void FolderPane::resize(int X, int Y, int W, int H) {
  Fl_Widget::resize(X, Y, W, H);
  if (editor_) cancel_rename();
  int top = top_h();
  int sx = scroll_->xposition(), sy = scroll_->yposition();
  scroll_->resize(X, Y + top, W, H - top);
  int inner_w = W - SCROLL_W, inner_h = H - top - SCROLL_W;
  view_->relayout(inner_w, inner_h);
  view_->position(X - sx, Y + top - sy);
  scroll_->scroll_to(0, std::min(sy, std::max(0, view_->h() - inner_h)));
  // Gdy wszystko się mieści, pasek jest wygaszony (szary suwak), jak w Mac OS 9.
  bool need = view_->h() > inner_h;
  scroll_->scrollbar.selection_color(need ? HILITE : GRAY);
  scroll_->hscrollbar.selection_color(GRAY);
  scroll_->redraw();
}

void FolderPane::draw() {
  // pasek informacji: "7 elementów, 12,4 GB wolne"
  fl_color(GRAY);
  fl_rectf(x(), y(), w(), INFO_H);
  fl_color(LIGHT);
  fl_xyline(x(), y(), x() + w() - 1);
  fl_color(SHADOW);
  fl_xyline(x(), y() + INFO_H - 1, x() + w() - 1);
  fl_font(FONT_SMALL, SIZE_SMALL);
  fl_color(TEXT);
  std::string t = ellipsize(info_, w() - 10);
  fl_draw(t.c_str(), x(), y(), w(), INFO_H - 1, FL_ALIGN_CENTER);

  if (view_->mode() == FolderView::List) {
    // nagłówki kolumn
    int hy = y() + INFO_H;
    fl_color(GRAY);
    fl_rectf(x(), hy, w(), HEADER_H);
    fl_color(SHADOW);
    fl_xyline(x(), hy + HEADER_H - 1, x() + w() - 1);
    ListCols lc = list_cols(x() + 4, w() - SCROLL_W);
    fl_push_clip(x(), hy, w() - SCROLL_W, HEADER_H);
    fl_font(FONT_SYSTEM, SIZE_SMALL);
    int base = hy + HEADER_H / 2 + fl_height() / 2 - fl_descent();
    auto header = [&](FolderView::SortKey key, int cx, int cw, const char *txt, bool right) {
      bool on = view_->sort_key() == key;
      if (on) {   // kolumna sortowania jest ciemniejsza, jak w Finderze
        fl_color(MID);
        fl_rectf(cx + 2, hy + 1, cw - 2, HEADER_H - 2);
      }
      fl_color(TEXT);
      int tw = (int)fl_width(txt);
      int tx = right ? cx + cw - 8 - tw : (key == FolderView::ByName ? x() + 27 : cx + 4);
      fl_draw(txt, tx, base);
      if (on) {   // strzałka kierunku
        int ax = right ? tx - 10 : tx + tw + 5, ay = hy + HEADER_H / 2;
        if (view_->sort_desc()) fl_polygon(ax, ay - 2, ax + 6, ay - 2, ax + 3, ay + 2);
        else fl_polygon(ax, ay + 2, ax + 6, ay + 2, ax + 3, ay - 2);
      }
    };
    header(FolderView::ByName, x(), lc.date_x - x(), "Nazwa", false);
    if (lc.date) header(FolderView::ByDate, lc.date_x, FolderView::COL_DATE, "Data modyfikacji", false);
    if (lc.size) header(FolderView::BySize, lc.size_x, FolderView::COL_SIZE, "Rozmiar", true);
    if (lc.kind) header(FolderView::ByKind, lc.kind_x, FolderView::COL_KIND, "Rodzaj", false);
    for (int cx : {lc.date_x, lc.size_x, lc.kind_x}) {
      if (cx >= x() + w() - SCROLL_W) continue;
      fl_color(SHADOW); fl_yxline(cx, hy + 2, hy + HEADER_H - 3);
      fl_color(LIGHT);  fl_yxline(cx + 1, hy + 2, hy + HEADER_H - 3);
    }
    fl_pop_clip();
  }
  draw_children();
  // uchwyt zmiany rozmiaru w rogu między paskami przewijania (jak w Mac OS 9)
  draw_grow_box(x() + w() - SCROLL_W, y() + h() - SCROLL_W);
}

// --- zmiana nazwy w miejscu --------------------------------------------------
void FolderPane::start_rename(int index) {
  cancel_rename();
  Rect r;
  const Entry *e = view_->entry(index);
  if (!e || !view_->label_rect(index, r)) return;
  editing_path_ = e->path;
  begin();
  editor_ = new Fl_Input(r.x, r.y, r.w, r.h);
  end();
  editor_->box(FL_BORDER_BOX);
  editor_->textfont(FONT_SMALL);
  editor_->textsize(SIZE_SMALL);
  editor_->value(e->name.c_str());
  editor_->when(FL_WHEN_ENTER_KEY_ALWAYS | FL_WHEN_RELEASE_ALWAYS);
  editor_->callback(rename_cb, this);
  // zaznacz nazwę bez rozszerzenia (jak w Finderze)
  size_t dot = e->dir ? std::string::npos : e->name.rfind('.');
  int end_sel = (dot == std::string::npos || dot == 0) ? (int)e->name.size() : (int)dot;
  editor_->insert_position(0, end_sel);
  editor_->take_focus();
  redraw();
}

void FolderPane::cancel_rename() {
  if (!editor_) return;
  Fl_Input *ed = editor_;
  editor_ = nullptr;
  remove(ed);
  Fl::delete_widget(ed);
  editing_path_.clear();
  redraw();
}

void FolderPane::rename_cb(Fl_Widget *, void *d) { ((FolderPane *)d)->commit_rename(); }

void FolderPane::commit_rename() {
  if (!editor_) return;
  std::string name = editor_->value(), old = editing_path_;
  cancel_rename();
  view_->take_focus();
  if (name.empty() || name == fs::path(old).filename().string()) return;
  if (on_rename_commit) {
    std::string err = on_rename_commit(old, name);
    if (!err.empty() && on_error) on_error(err);
  }
}

int FolderPane::handle(int e) {
  // Escape w polu nazwy: anuluj edycję
  if (editor_ && (e == FL_KEYBOARD || e == FL_SHORTCUT) && Fl::event_key() == FL_Escape) {
    cancel_rename();
    view_->take_focus();
    return 1;
  }
  int gx = x() + w() - SCROLL_W, gy = y() + h() - SCROLL_W;
  Fl_Window *win = window();
  switch (e) {
  case FL_PUSH:
    // klik poza polem nazwy zatwierdza edycję
    if (editor_ && !Fl::event_inside(editor_)) commit_rename();
    // klik w nagłówek kolumny: sortowanie
    if (view_->mode() == FolderView::List && Fl::event_y() >= y() + INFO_H &&
        Fl::event_y() < y() + INFO_H + HEADER_H && Fl::event_x() < x() + w() - SCROLL_W) {
      ListCols lc = list_cols(x() + 4, w() - SCROLL_W);
      int ex = Fl::event_x();
      FolderView::SortKey k = FolderView::ByName;
      if (lc.kind && ex >= lc.kind_x) k = FolderView::ByKind;
      else if (lc.size && ex >= lc.size_x) k = FolderView::BySize;
      else if (lc.date && ex >= lc.date_x) k = FolderView::ByDate;
      view_->set_sort(k);
      redraw();
      return 1;
    }
    if (win && Fl::event_x() >= gx && Fl::event_y() >= gy) {
      growing_ = true;
      grow_dx_ = win->w() - Fl::event_x_root();
      grow_dy_ = win->h() - Fl::event_y_root();
      return 1;
    }
    break;
  case FL_DRAG:
    if (growing_) {
      win->size(std::max(200, Fl::event_x_root() + grow_dx_), std::max(120, Fl::event_y_root() + grow_dy_));
      return 1;
    }
    break;
  case FL_RELEASE:
    if (growing_) { growing_ = false; return 1; }
    break;
  }
  return Fl_Group::handle(e);
}
