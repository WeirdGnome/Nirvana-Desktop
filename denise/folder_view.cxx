// Nirvana Desktop - denise: the folder view (icons or list) with an information bar
#include "folder_view.h"
#include "sniff.h"

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
#include <fstream>
#include <map>
#include <strings.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/statvfs.h>
#include <thread>
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

static std::string g_home;   // home folder (set by Rambler)

Icon icon_for_path(const std::string &path, bool dir) {
  fs::path p(path);
  if (dir) {
    const char *h = getenv("HOME");
    std::string home = !g_home.empty() ? g_home : h ? h : "/";
    std::error_code ec;
    if (fs::equivalent(p.parent_path(), appimage::apps_dir(home), ec)) {
      int cat = appimage::category_of_folder(p.filename().string());
      if (cat >= 0) return (Icon)((int)Icon::CatGames + cat);
    }
  }
  return icon_for(p.filename().string(), dir);
}

std::string kind_name(const Entry &e) {
  if (e.dir) return _("folder");
  if (!e.kind.empty()) return e.kind;   // recognized from the content
  switch (e.icon) {
  case Icon::Application: return _("application");
  case Icon::Image: return _("image");
  case Icon::Music: return _("sound");
  default: return _("document");
  }
}

// "8 lis 2026, 21:40"
static std::string date_text(time_t t) { return plat::date_time(t); }

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
  return plat::decimal(buf);
}

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------
void FolderView::set_home(const std::string &home) { g_home = home; }

// Icons from files (e.g. from an AppImage): normal and darkened, in the given size.
struct IconPair { Fl_Image *normal = nullptr, *selected = nullptr; };
static IconPair file_icon(const std::string &file, int size) {
  static std::map<std::string, IconPair> cache;
  static bool registered = false;
  if (!registered) { fl_register_images(); registered = true; }
  std::string key = file + "@" + std::to_string(size) + "@" + std::to_string(icon_style());
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  IconPair p;
  Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
  if (Fl_Shared_Image *img = Fl_Shared_Image::get(file.c_str())) {
    if (img->w() > 0 && img->h() > 0) {
      // We copy the source image (not the Fl_Shared_Image, which shares data between
      // copies), so darkening the "selected" version does not spoil the normal one.
      const Fl_Image *base = img->image() ? img->image() : img;
      p.normal = base->copy(size, size);
      if (auto *r = dynamic_cast<Fl_RGB_Image *>(p.normal)) r->normalize();   // SVG
      if (icon_style() == 2 && dynamic_cast<Fl_RGB_Image *>(p.normal)) {
        // Nirvana 3D: the selected state of a single picture is made like the system icons' one
        p.selected = synthesize_selected((Fl_RGB_Image *)p.normal);
      } else {
        p.selected = base->copy(size, size);
        if (auto *r = dynamic_cast<Fl_RGB_Image *>(p.selected)) r->normalize();
        p.selected->color_average(FL_BLACK, 0.5f);
      }
    }
    img->release();
  }
  cache[key] = p;
  return p;
}

// 16x16 vector icons for the list view: we draw 32x32 into an image and scale it down.
static Fl_Image *small_icon(Icon icon, bool sel) {
  // Icons are drawn on the content background, so the key includes the background color. Old ones
  // are not deleted: the application menu (gary) may still hold pointers to them.
  static std::map<std::pair<Fl_Color, int>, Fl_Image *> cache;
  std::pair<Fl_Color, int> key{Fl::get_color(CONTENT), ((int)icon * 2 + sel) * 3 + icon_style()};
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

// Large 64x64 icons: the 32x32 drawing enlarged 2x without smoothing - crisp
// pixels like on an old computer screen.
static Fl_Image *big_icon(Icon icon, bool sel) {
  static std::map<std::pair<Fl_Color, int>, Fl_Image *> cache;
  std::pair<Fl_Color, int> key{Fl::get_color(CONTENT), ((int)icon * 2 + sel) * 3 + icon_style()};
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  Fl_Image_Surface surf(32, 32);
  Fl_Surface_Device::push_current(&surf);
  fl_color(CONTENT);
  fl_rectf(0, 0, 32, 32);
  draw_icon(icon, 0, 0, sel);
  Fl_Surface_Device::pop_current();
  Fl_RGB_Image *src = surf.image();
  int d = src->d(), ld = src->ld() ? src->ld() : src->w() * d;
  auto *pix = new uchar[64 * 64 * 3];
  const uchar *sp = (const uchar *)src->data()[0];
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++) memcpy(pix + (y * 64 + x) * 3, sp + (y / 2) * ld + (x / 2) * d, 3);
  delete src;
  auto *img = new Fl_RGB_Image(pix, 64, 64, 3);
  img->alloc_array = 1;
  cache[key] = img;
  return img;
}

void warm_small_icons() {
  for (int i = 0; i <= (int)Icon::Last; i++)
    for (bool sel : {false, true}) { small_icon((Icon)i, sel); big_icon((Icon)i, sel); }
}

Fl_Image *entry_icon_image(const Entry &e, int size, bool sel) {
  if (!e.icon_file.empty()) {
    IconPair fi = file_icon(e.icon_file, size);
    if (fi.normal) return sel ? fi.selected : fi.normal;
  }
  return size == 64 ? big_icon(e.icon, sel) : small_icon(e.icon, sel);   // vector
}

void draw_entry_icon(const Entry &e, int x, int y, bool sel, int size, bool open) {
  Icon ic = (e.dir && open) ? Icon::OpenFolder : e.icon;
  IconPair fi;
  if (!e.icon_file.empty()) fi = file_icon(e.icon_file, size);
  if (fi.normal) { (sel ? fi.selected : fi.normal)->draw(x, y); return; }
  if (size == 32) draw_icon(ic, x, y, sel);
  else if (size == 64) big_icon(ic, sel)->draw(x, y);
  else small_icon(ic, sel)->draw(x, y);
}

// ---------------------------------------------------------------------------
// Snapshot icon positions (Snapshot): "x y<TAB>path".
// ---------------------------------------------------------------------------
static std::map<std::string, std::pair<int, int>> g_snap;
static bool g_snap_loaded = false;
static std::string snap_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/icon-positions";
}
static void snap_load() {
  if (g_snap_loaded) return;
  g_snap_loaded = true;
  std::ifstream in(snap_file());
  std::string line;
  while (std::getline(in, line)) {
    auto tab = line.find('\t');
    if (tab == std::string::npos) continue;
    int x = 0, y = 0;
    if (sscanf(line.c_str(), "%d %d", &x, &y) == 2) g_snap[line.substr(tab + 1)] = {x, y};
  }
}
static void snap_save() {
  std::string f = snap_file();
  std::ofstream out(f + ".tmp");
  for (auto &[p, xy] : g_snap)
    if (::access(p.c_str(), F_OK) == 0) out << xy.first << ' ' << xy.second << '\t' << p << '\n';
  out.close();
  rename((f + ".tmp").c_str(), f.c_str());
}

FolderView::FolderView(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}

// The reading part of load(): no FLTK, no shared state without a lock (sniff has its own) -
// it runs in a worker thread. AppImage names and pictures (their cache) are filled later, in
// the interface thread.
void scan_folder(FolderScan &sc) {
  std::vector<Entry> out;
  std::error_code ec;
  int sniffed = 0;
  for (auto &de : fs::directory_iterator(sc.path, fs::directory_options::skip_permission_denied, ec)) {
    std::string name = de.path().filename().string();
    if (!sc.hidden && !name.empty() && name[0] == '.') continue;
    Entry e;
    e.name = name;
    e.path = de.path().string();
    std::error_code ec2;
    e.dir = de.is_directory(ec2);
    e.icon = icon_for_path(e.path, e.dir);
    e.label = name;
    struct stat st;
    if (stat(e.path.c_str(), &st) == 0) {
      e.mtime = st.st_mtime;
      if (!e.dir) e.size = (uintmax_t)st.st_size;
    }
    if (!e.dir && sniffed < 3000) {   // the type from the first bytes, not from the name
      sniffed++;
      sniff::Kind k = sniff::of(e.path);
      e.mime = k.mime;
      e.kind = k.label;
      e.cat = k.cat;
      if (e.icon == Icon::Document) {   // a file without a known ending: the icon after its content
        if (k.cat == "image") e.icon = Icon::Image;
        else if (k.cat == "audio") e.icon = Icon::Music;
        else if (k.mime == "application/vnd.appimage" || k.mime == "application/x-executable") e.icon = Icon::Application;
      }
    }
    out.push_back(std::move(e));
  }
  // tests only: NIRVANA_SCAN_SLOW=ms - every folder reads that long (a slow disk)
  static const int slow = getenv("NIRVANA_SCAN_SLOW") ? atoi(getenv("NIRVANA_SCAN_SLOW")) : 0;
  if (slow > 0) usleep(slow * 1000);
  struct statvfs sv;
  if (statvfs(sc.path.c_str(), &sv) == 0) sc.free_gb = (double)sv.f_bavail * sv.f_frsize / 1e9;
  sc.entries = std::move(out);
  sc.done = true;
}

void FolderView::load(const std::string &path, bool show_hidden) {
  FolderScan sc;
  sc.path = path;
  sc.hidden = show_hidden;
  scan_folder(sc);
  load_entries(std::move(sc.entries));
}

std::function<bool(const std::string &, Icon &)> FolderView::icon_override;

void FolderView::load_entries(std::vector<Entry> &&all) {
  entries_.clear();
  for (auto &e : all) {
    if (icon_override) icon_override(e.path, e.icon);
    if (!e.dir && is_appimage(e.name)) {
      e.label = !g_home.empty() ? appimage::label(e.path, g_home) : appimage::display_name(e.name);
      if (!g_home.empty()) e.icon_file = appimage::icon(e.path, g_home);
    }
    if (filter && !e.dir && !filter(e)) continue;   // the explorer's filter
    entries_.push_back(std::move(e));
  }
  sort_entries();
  // positions: snapshot ones from the file, manual ones from before the refresh stay
  snap_load();
  std::map<std::string, Pt> keep;
  for (auto &e : entries_) {
    auto f = free_.find(e.path);
    if (f != free_.end()) keep[e.path] = f->second;
    else if (auto sn = g_snap.find(e.path); sn != g_snap.end()) keep[e.path] = {sn->second.first, sn->second.second};
  }
  free_ = std::move(keep);
  sel_.assign(entries_.size(), 0);
  anchor_ = -1;
  drop_ = -2;
  state_ = IDLE;
  Fl::remove_timeout(rename_timeout, this);
  redraw();
}

// Sorting is alphabetical without putting folders first; the list can
// also be sorted by date, size and kind (ties: by name).
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
  else { sort_ = k; desc_ = (k == ByDate || k == BySize); }   // newest / largest at the top
  std::vector<std::string> keep;
  for (auto &e : selection()) keep.push_back(e.path);
  sort_entries();
  select_paths(keep);
}

void FolderView::relayout(int avail_w, int min_h) {
  last_w_ = avail_w;
  last_min_h_ = min_h;
  fl_font(FONT_SMALL, SIZE_SMALL);
  line_h_ = fl_height();
  if (mode_ == Icons) {
    // names are never cut: the rows are as high as the longest name needs (8 lines at most)
    label_lines_ = 1;
    for (auto &e : entries_)
      if (fl_width(e.label.c_str()) > cell_w() - 6)
        label_lines_ = std::max(label_lines_, std::min(8, (int)wrap_text(e.label, cell_w() - 6).size()));
    int cw = cell_w(), ch = cell_h();
    cols_ = std::max(1, (avail_w - 8) / cw);
    // Icons placed by hand take their grid cells; the rest fills
    // the free places in order (for icons without a saved position).
    cells_.assign(entries_.size(), Pt{0, 0});
    std::vector<std::vector<char>> used;
    auto mark = [&](int col, int row) {
      if (col < 0 || row < 0 || col >= cols_) return;
      if ((int)used.size() <= row) used.resize(row + 1, std::vector<char>(cols_, 0));
      used[row][col] = 1;
    };
    int max_x = 0, max_y = 0;
    for (size_t i = 0; i < entries_.size(); i++) {
      auto f = free_.find(entries_[i].path);
      if (f == free_.end()) continue;
      cells_[i] = f->second;
      // grid cells this icon overlaps
      int c0 = std::max(0, f->second.x - 4) / cw, c1 = std::max(0, f->second.x - 4 + cw - 1) / cw;
      int r0 = std::max(0, f->second.y - 6) / ch, r1 = std::max(0, f->second.y - 6 + ch - 1) / ch;
      for (int c = c0; c <= c1; c++)
        for (int r = r0; r <= r1; r++) mark(c, r);
      max_x = std::max(max_x, f->second.x + cw);
      max_y = std::max(max_y, f->second.y + ch);
    }
    int slot = 0;
    for (size_t i = 0; i < entries_.size(); i++) {
      if (free_.count(entries_[i].path)) continue;
      while (true) {
        int col = slot % cols_, row = slot / cols_;
        bool taken = row < (int)used.size() && used[row][col];
        slot++;
        if (taken) continue;
        cells_[i] = {4 + col * cw, 6 + row * ch};
        max_y = std::max(max_y, 6 + (row + 1) * ch);
        break;
      }
    }
    size(std::max(avail_w, max_x + 4), std::max(min_h, max_y + 6));
  } else {
    cols_ = 1;
    // the name column as wide as the longest name: the list scrolls sideways when it must
    int longest = 0;
    for (auto &e : entries_) longest = std::max(longest, (int)fl_width(e.label.c_str()));
    int need = longest + 40 + COL_DATE + COL_SIZE + COL_KIND + 8;
    size(std::max(avail_w, need), std::max(min_h, (int)entries_.size() * ROW_H + 4));
  }
}

// --- icon size, moving, snapshot ------------------------------------------------
void FolderView::set_icon_size(int s) {
  s = s <= 16 ? 16 : s >= 64 ? 64 : 32;
  if (s == isize_) return;
  // manual positions are scaled to the new grid proportionally
  double fx = (double)(s == 16 ? 76 : s == 64 ? 104 : 80) / cell_w();
  double fy = (double)(s == 16 ? 40 : s == 64 ? 98 : 66) / cell_h();
  snap_load();
  for (auto &[p, pt] : free_)
    if (!g_snap.count(p)) pt = {(int)(pt.x * fx), (int)(pt.y * fy)};   // snapshot ones stay in place
  isize_ = s;
  if (last_w_) relayout(last_w_, last_min_h_);
  redraw();
}

void FolderView::move_items(const std::vector<std::string> &paths, int dx, int dy) {
  // The other icons stay where they are  - we remember their
  // current places so they do not jump into a freed grid cell.
  for (size_t i = 0; i < entries_.size() && i < cells_.size(); i++)
    if (!free_.count(entries_[i].path)) free_[entries_[i].path] = cells_[i];
  for (auto &p : paths) {
    int i = index_of(p);
    if (i < 0) continue;
    Pt c = i < (int)cells_.size() ? cells_[i] : Pt{4, 6};
    free_[p] = {std::max(0, c.x + dx), std::max(0, c.y + dy)};
  }
  if (last_w_) relayout(last_w_, last_min_h_);
  redraw();
}

void FolderView::snapshot(const std::vector<std::string> &paths) {
  snap_load();
  for (auto &p : paths) {
    int i = index_of(p);
    if (i < 0 || i >= (int)cells_.size()) continue;
    free_[p] = cells_[i];
    g_snap[p] = {cells_[i].x, cells_[i].y};
  }
  snap_save();
}

void FolderView::unsnapshot(const std::vector<std::string> &paths) {
  snap_load();
  for (auto &p : paths) { g_snap.erase(p); free_.erase(p); }
  snap_save();
  if (last_w_) relayout(last_w_, last_min_h_);
  redraw();
}

void FolderView::clean_up() {
  snap_load();
  for (auto &e : entries_) g_snap.erase(e.path);
  snap_save();
  free_.clear();
  if (last_w_) relayout(last_w_, last_min_h_);
  redraw();
}

bool FolderView::is_snapshot(const std::string &path) const {
  snap_load();
  return g_snap.count(path) > 0;
}

std::vector<std::string> FolderView::all_paths() const {
  std::vector<std::string> v;
  for (auto &e : entries_) v.push_back(e.path);
  return v;
}

// --- geometry ----------------------------------------------------------------
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

FolderView::Pt FolderView::origin(int i) const {
  if (i >= 0 && i < (int)cells_.size()) return {x() + cells_[i].x, y() + cells_[i].y};
  return {x() + 4 + (i % cols_) * cell_w(), y() + 6 + (i / cols_) * cell_h()};
}

Rect FolderView::item_rect(int i) const {
  if (mode_ == Icons) {
    Pt o = origin(i);
    return {o.x + 4, o.y, cell_w() - 8, cell_h() - 12};
  }
  return {x() + 4, y() + 2 + i * ROW_H, name_col_w(w()), ROW_H};
}

int FolderView::index_at(int ex, int ey, bool *on_label) const {
  if (on_label) *on_label = false;
  if (mode_ == List) {
    int i = (ey - y() - 2) / ROW_H;
    if (ey < y() + 2 || i < 0 || i >= (int)entries_.size()) return -1;
    if (ex < x() + 4 || ex >= x() + 4 + name_col_w(w())) return -1;   // only the name column
    if (on_label) *on_label = ex >= x() + 26;
    return i;
  }
  // from the end: an icon drawn later lies on top
  int cw = cell_w(), is = isize_;
  for (int i = (int)entries_.size() - 1; i >= 0; i--) {
    Pt o = origin(i);
    int lx = ex - o.x, ly = ey - o.y;
    if (lx < 0 || ly < 0 || lx >= cw || ly >= cell_h()) continue;
    // a hit counts only on the icon and the label, not the whole cell
    bool on_icon = lx >= (cw - is) / 2 && lx < (cw + is) / 2 && ly < is + 4;
    bool lbl = ly >= is + 4 && ly < is + 8 + label_lines_ * line_h_;
    if (!on_icon && !lbl) continue;
    if (on_label) *on_label = lbl;
    return i;
  }
  return -1;
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
    Pt o = origin(i);
    out = {o.x + (cell_w() - isize_) / 2, o.y + 2, isize_, isize_};
  } else {
    out = {x() + 6, y() + 3 + i * ROW_H, 16, 16};
  }
  return visible_in_parent(this, out.x, out.y, out.w, out.h) && visible_r();
}

bool FolderView::label_rect(int i, Rect &out) const {
  if (i < 0 || i >= count()) return false;
  if (mode_ == Icons) {
    Pt o = origin(i);
    out = {o.x - 6, o.y + isize_ + 3, cell_w() + 12, 18};
  } else {
    out = {x() + 24, y() + 2 + i * ROW_H, name_col_w(w()) - 24, ROW_H};
  }
  return visible_in_parent(this, out.x, out.y, out.w, out.h);
}

int FolderView::drop_target_at(int ex, int ey) const {
  int i = index_at(ex, ey);
  if (i >= 0 && entries_[i].dir && !sel_[i]) return i;   // onto a folder (not onto itself)
  // onto a program (an AppImage, an executable file): it starts with the files
  if (i >= 0 && !sel_[i] && (is_appimage(entries_[i].name) || entries_[i].icon == Icon::Application)) return i;
  return -1;                                                // into this folder
}

// --- selection ---------------------------------------------------------------
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

// --- drawing -----------------------------------------------------------------
void pane_redraw_later(void *pane) { ((Fl_Widget *)pane)->redraw(); }

void FolderView::draw() {
  // the list scrolled sideways: the column headers (drawn by the pane) follow it
  // (after this drawing: a redraw asked for during it would be forgotten)
  if (mode_ == List && x() != drawn_x_) {
    drawn_x_ = x();
    if (parent() && parent()->parent()) Fl::add_timeout(0.0, pane_redraw_later, parent()->parent());
  }
  fl_color(CONTENT);
  fl_rectf(x(), y(), w(), h());
  if (state_ == BANDING) {   // the fill of the selection rectangle - under the items
    int x0 = std::min(band_x_, press_x_), y0 = std::min(band_y_, press_y_);
    int x1 = std::max(band_x_, press_x_), y1 = std::max(band_y_, press_y_);
    fl_color(fl_color_average(SEL, CONTENT, 0.2f));
    fl_rectf(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
  }
  if (mode_ == Icons) draw_icons(); else draw_list();

  // selection rectangle: a light fill of the selection color and a clear dashed edge
  // (the gray dotted line alone was hardly visible on the white list)
  if (state_ == BANDING) {
    int x0 = std::min(band_x_, press_x_), y0 = std::min(band_y_, press_y_);
    int x1 = std::max(band_x_, press_x_), y1 = std::max(band_y_, press_y_);
    int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    fl_color(SEL);
    fl_rect(x0, y0, bw, bh);
    fl_color(CONTENT_TEXT);
    fl_line_style(FL_DASH, 1);
    fl_rect(x0, y0, bw, bh);
    fl_line_style(0);
  }
  // the whole folder is the drop target: a thick frame around the visible part
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
    Pt o = origin(i);
    int cx = o.x, cy = o.y, cw = cell_w();
    bool sel = sel_[i] || drop_ == i;
    bool open = e.dir && is_open && is_open(e.path);
    draw_entry_icon(e, cx + (cw - isize_) / 2, cy + 2, sel, isize_, open);

    // the whole name, in lines under the icon
    auto lines = wrap_text(e.label, cw - 6);
    int ty = cy + isize_ + 6;
    for (auto &l : lines) {
      int tw = (int)fl_width(l.c_str());
      int tx = cx + (cw - tw) / 2;
      if (sel_[i]) {
        fl_color(ICON_SEL);
        fl_rectf(tx - 2, ty, tw + 4, fl_height());
        fl_color(ICON_SEL_TEXT);
      } else {
        fl_color(CONTENT_TEXT);
      }
      fl_draw(l.c_str(), tx, ty, tw, fl_height(), FL_ALIGN_LEFT | FL_ALIGN_INSIDE, nullptr, 0);   // a box: "@" stays text
      ty += fl_height();
    }
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
    const std::string &name = e.label;   // the column is as wide as the longest name
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
    if (lc.date) fl_draw(ellipsize(date_text(e.mtime), COL_DATE - 8).c_str(), xd + 4, base);
    if (lc.size) {
      std::string sz = e.dir ? "—" : human_size(e.size);
      fl_draw(sz.c_str(), xs + COL_SIZE - 8 - (int)fl_width(sz.c_str()), base);
    }
    if (lc.kind) fl_draw(ellipsize(kind_name(e), COL_KIND - 8).c_str(), xk + 4, base);
  }
}

// --- events ------------------------------------------------------------------
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
    // right button: a context menu for the clicked item (or the empty space)
    if (Fl::event_button() == FL_RIGHT_MOUSE && on_context) {
      if (i >= 0) { if (!sel_[i]) select_only(i); anchor_ = i; }
      else std::fill(sel_.begin(), sel_.end(), 0);
      state_ = IDLE;
      redraw();
      on_context();
      return 1;
    }
    if (i >= 0 && Fl::event_clicks() && on_open) {
      Fl::event_clicks(0);
      select_only(i);
      state_ = IDLE;
      Entry copy = entries_[i];   // a copy - the callback may rebuild the view
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
      // a click on an already selected item without dragging
      int n = 0;
      for (char s : sel_) n += s;
      if (n > 1) select_only(press_index_);
      else if (was_selected_ && press_label_)
        Fl::add_timeout(0.6, rename_timeout, this);   // a moment, then editing the name
    }
    state_ = IDLE;
    redraw();
    return 1;
  }
  case FL_FOCUS:
  case FL_UNFOCUS:
    return 1;
  // --- files dragged from other programs (XDND) ---
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
    bool arrow = k == FL_Left || k == FL_Right || k == FL_Up || k == FL_Down;
    // empty folder: arrows do not move the focus out of the window content
    if (!n) return arrow && !(Fl::event_state() & (FL_ALT | FL_CTRL | FL_META)) ? 1 : 0;
    if (k == FL_F + 2) {
      if (const Entry *s = selected()) if (on_rename) on_rename(index_of(s->path));
      return 1;
    }
    if ((k == FL_Enter || k == FL_KP_Enter) && on_open) {
      if (const Entry *s = selected()) { Entry copy = *s; on_open(copy); }
      return 1;
    }
    // Alt/Ctrl + arrows are window shortcuts (back, forward, parent folder)
    if (Fl::event_state() & (FL_ALT | FL_CTRL | FL_META)) return 0;
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
  char buf[64] = "";
  if (free_gb_ >= 0) snprintf(buf, sizeof buf, "%.1f GB", free_gb_);   // (from the scan: no disk access here)
  if (loading_) { info_ = _("Loading…"); return; }
  info_ = items_text(view_->count()) + (buf[0] ? ", " + plat::fmt(_("%1 free"), plat::decimal(buf)) : "");
}

// --- watching the folder for changes -------------------------------------------
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
  while (read(fd, buf, sizeof buf) > 0) {}   // we only drain it; the details are not needed
  // a series of changes (e.g. copying many files) is collected into one refresh
  Fl::remove_timeout(refresh_later, d);
  Fl::add_timeout(0.25, refresh_later, d);
}

void FolderPane::refresh_later(void *d) {
  auto *p = (FolderPane *)d;
  if (p->editor_) { Fl::add_timeout(0.5, refresh_later, d); return; }   // do not interrupt renaming
  p->refresh();
}

void pane_redraw_later(void *pane);
FolderPane::~FolderPane() {
  Fl::remove_timeout(refresh_later, this);
  Fl::remove_timeout(scan_poll, this);   // (a scan still running only fills its own FolderScan)
  Fl::remove_timeout(pane_redraw_later, this);
  if (ino_fd_ >= 0) { Fl::remove_fd(ino_fd_); close(ino_fd_); }
}

// Refreshing reads the folder in a worker thread. The interface waits for it 150 ms at most -
// a folder on the computer's own disk is there by then and shows at once, as before; a slow or
// busy one (a USB stick during copying, a network folder) shows "Loading…" and comes when it
// is read - the windows, menus and the desktop go on meanwhile. A folder changed during
// reading is read once more afterwards (never two threads for one window).
void FolderPane::refresh() {
  cancel_rename();
  // keep the selection after a refresh (e.g. after renaming another file)
  if (!loading_) {
    keep_sel_.clear();
    for (auto &e : view_->selection()) keep_sel_.push_back(e.path);
  }
  if (scan_) { rescan_ = true; return; }   // being read: once more after it
  start_scan();
}

void FolderPane::start_scan() {
  auto sc = std::make_shared<FolderScan>();
  sc->path = path_;
  sc->hidden = hidden_;
  scan_ = sc;
  rescan_ = false;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  sc->started = ts.tv_sec + ts.tv_nsec / 1e9;
  std::thread([sc] { scan_folder(*sc); }).detach();
  // a short wait - not while the folder is known to be slow (copying to a USB stick refreshes
  // its window every quarter of a second: the desktop would wait most of the time)
  for (int i = 0; i < (slow_ ? 0 : 150) && !sc->done; i++) usleep(1000);
  if (sc->done) { apply_scan(); return; }
  if (!loading_) {
    loading_ = true;
    update_info();
    redraw();
  }
  Fl::add_timeout(0.05, scan_poll, this);
}

void FolderPane::scan_poll(void *d) {
  auto *p = (FolderPane *)d;
  if (p->scan_ && p->scan_->done) p->apply_scan();
  else Fl::repeat_timeout(0.05, scan_poll, d);
}

void FolderPane::apply_scan() {
  auto sc = scan_;
  scan_.reset();
  Fl::remove_timeout(scan_poll, this);
  if (!sc) return;
  if (sc->path != path_ || sc->hidden != hidden_) { start_scan(); return; }   // another folder meanwhile
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  slow_ = ts.tv_sec + ts.tv_nsec / 1e9 - sc->started > 0.15;
  loading_ = false;
  free_gb_ = sc->free_gb;
  view_->load_entries(std::move(sc->entries));
  view_->select_paths(keep_sel_);
  update_info();
  resize(x(), y(), w(), h());
  redraw();
  if (rescan_) { rescan_ = false; refresh(); }
}

void FolderPane::go(const std::string &p) {
  cancel_rename();
  Fl::remove_timeout(refresh_later, this);
  path_ = p;
  watch();
  view_->select_paths({});
  keep_sel_.clear();
  loading_ = false;
  scroll_->scroll_to(0, 0);
  if (scan_) {   // the old folder is still being read: the new one after it (apply_scan sees the change)
    rescan_ = false;
    view_->load_entries({});
    loading_ = true;
    update_info();
    redraw();
    return;
  }
  refresh();
}

void FolderPane::set_icon_size(int s) {
  view_->set_icon_size(s);
  resize(x(), y(), w(), h());   // recompute scrolling
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
  // When everything fits, the scrollbar is dimmed (gray thumb).
  bool need = view_->h() > inner_h;
  scroll_->scrollbar.selection_color(need ? HILITE : GRAY);
  scroll_->hscrollbar.selection_color(view_->w() > inner_w ? HILITE : GRAY);   // a long name: sideways
  scroll_->redraw();
}

void FolderPane::draw() {
  // information bar: "7 items, 12.4 GB free"
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
    // column headers
    int hy = y() + INFO_H;
    fl_color(GRAY);
    fl_rectf(x(), hy, w(), HEADER_H);
    fl_color(SHADOW);
    fl_xyline(x(), hy + HEADER_H - 1, x() + w() - 1);
    ListCols lc = list_cols(view_->x() + 4, view_->w());   // with the list, also scrolled sideways
    fl_push_clip(x(), hy, w() - SCROLL_W, HEADER_H);
    fl_font(FONT_SYSTEM, SIZE_SMALL);
    int base = hy + HEADER_H / 2 + fl_height() / 2 - fl_descent();
    auto header = [&](FolderView::SortKey key, int cx, int cw, const char *txt, bool right) {
      bool on = view_->sort_key() == key;
      if (on) {   // the sort column is darker
        fl_color(MID);
        fl_rectf(cx + 2, hy + 1, cw - 2, HEADER_H - 2);
      }
      fl_color(TEXT);
      int tw = (int)fl_width(txt);
      int tx = right ? cx + cw - 8 - tw : (key == FolderView::ByName ? x() + 27 : cx + 4);
      fl_draw(txt, tx, base);
      if (on) {   // direction arrow
        int ax = right ? tx - 10 : tx + tw + 5, ay = hy + HEADER_H / 2;
        if (view_->sort_desc()) fl_polygon(ax, ay - 2, ax + 6, ay - 2, ax + 3, ay + 2);
        else fl_polygon(ax, ay + 2, ax + 6, ay + 2, ax + 3, ay - 2);
      }
    };
    header(FolderView::ByName, x(), lc.date_x - x(), _("Name"), false);
    if (lc.date) header(FolderView::ByDate, lc.date_x, FolderView::COL_DATE, _("Date Modified"), false);
    if (lc.size) header(FolderView::BySize, lc.size_x, FolderView::COL_SIZE, _("Size"), true);
    if (lc.kind) header(FolderView::ByKind, lc.kind_x, FolderView::COL_KIND, _("Kind"), false);
    for (int cx : {lc.date_x, lc.size_x, lc.kind_x}) {
      if (cx >= x() + w() - SCROLL_W) continue;
      fl_color(SHADOW); fl_yxline(cx, hy + 2, hy + HEADER_H - 3);
      fl_color(LIGHT);  fl_yxline(cx + 1, hy + 2, hy + HEADER_H - 3);
    }
    fl_pop_clip();
  }
  draw_children();
  // resize handle in the corner between the scrollbars
  draw_grow_box(x() + w() - SCROLL_W, y() + h() - SCROLL_W);
}

// --- renaming in place --------------------------------------------------------
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
  // select the name without the extension
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
  // Escape in the name field: cancel editing
  if (editor_ && (e == FL_KEYBOARD || e == FL_SHORTCUT) && Fl::event_key() == FL_Escape) {
    cancel_rename();
    view_->take_focus();
    return 1;
  }
  int gx = x() + w() - SCROLL_W, gy = y() + h() - SCROLL_W;
  Fl_Window *win = window();
  switch (e) {
  case FL_PUSH:
    // a click outside the name field confirms the edit
    if (editor_ && !Fl::event_inside(editor_)) commit_rename();
    // a click on a column header: sorting
    if (view_->mode() == FolderView::List && Fl::event_y() >= y() + INFO_H &&
        Fl::event_y() < y() + INFO_H + HEADER_H && Fl::event_x() < x() + w() - SCROLL_W) {
      ListCols lc = list_cols(view_->x() + 4, view_->w());
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
