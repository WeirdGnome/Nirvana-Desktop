// LisaOS - denise: widok folderu (ikony) z paskiem informacji
#include "folder_view.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Shared_Image.H>
#include <map>

#include "appimage.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <strings.h>
#include <sys/statvfs.h>

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

// ---------------------------------------------------------------------------
FolderView::FolderView(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}

static std::string g_home;
void FolderView::set_home(const std::string &home) { g_home = home; }

// Ikony z plików: normalna i przyciemniona (zaznaczona), 32x32, wspólny cache.
struct IconPair { Fl_Image *normal = nullptr, *selected = nullptr; };
static IconPair file_icon(const std::string &file) {
  static std::map<std::string, IconPair> cache;
  static bool registered = false;
  if (!registered) { fl_register_images(); registered = true; }
  auto it = cache.find(file);
  if (it != cache.end()) return it->second;
  IconPair p;
  Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
  if (Fl_Shared_Image *img = Fl_Shared_Image::get(file.c_str())) {
    if (img->w() > 0 && img->h() > 0) {
      // dwie niezależne kopie - kopia w tym samym rozmiarze współdzieliłaby piksele
      // Kopiujemy obraz źródłowy (nie Fl_Shared_Image, które dzieli dane między
      // kopiami), więc przyciemnienie wersji "zaznaczonej" nie psuje normalnej.
      const Fl_Image *base = img->image() ? img->image() : img;
      p.normal = base->copy(32, 32);
      if (auto *r = dynamic_cast<Fl_RGB_Image *>(p.normal)) r->normalize();   // SVG: rasteryzacja od razu
      p.selected = base->copy(32, 32);
      if (auto *r = dynamic_cast<Fl_RGB_Image *>(p.selected)) r->normalize();
      p.selected->color_average(FL_BLACK, 0.5f);
    }
    img->release();
  }
  cache[file] = p;
  return p;
}

void FolderView::load(const std::string &path, bool show_hidden) {
  entries_.clear();
  sel_ = -1;
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
    if (!e.dir && is_appimage(name)) {
      e.label = appimage::display_name(name);
      if (!g_home.empty()) e.icon_file = appimage::icon(e.path, g_home);
    }
    entries_.push_back(std::move(e));
  }
  // Mac OS sortuje alfabetycznie bez wyróżniania folderów.
  std::sort(entries_.begin(), entries_.end(), [](const Entry &a, const Entry &b) {
    return strcasecmp(a.label.c_str(), b.label.c_str()) < 0;
  });
  redraw();
}

void FolderView::relayout(int avail_w, int min_h) {
  cols_ = std::max(1, (avail_w - 8) / CELL_W);
  int rows = ((int)entries_.size() + cols_ - 1) / cols_;
  size(avail_w, std::max(min_h, rows * CELL_H + 12));
}

bool FolderView::icon_rect(const std::string &path, Rect &out) const {
  for (int i = 0; i < (int)entries_.size(); i++) {
    if (entries_[i].path != path) continue;
    int cx = x() + 4 + (i % cols_) * CELL_W + (CELL_W - 32) / 2;
    int cy = y() + 6 + (i / cols_) * CELL_H + 2;
    out = {cx, cy, 32, 32};
    // ikona musi być w widocznej części przewijanego obszaru
    const Fl_Widget *p = parent();
    if (p && (cy + 32 <= p->y() || cy >= p->y() + p->h() - SCROLL_W ||
              cx + 32 <= p->x() || cx >= p->x() + p->w() - SCROLL_W)) return false;
    return visible_r();
  }
  return false;
}

int FolderView::index_at(int ex, int ey) const {
  int cx = (ex - x() - 4) / CELL_W, cy = (ey - y() - 6) / CELL_H;
  if (ex < x() + 4 || ey < y() + 6 || cx >= cols_) return -1;
  int i = cy * cols_ + cx;
  if (i < 0 || i >= (int)entries_.size()) return -1;
  // trafienie liczymy tylko w ikonę i etykietę, nie w całą komórkę
  int lx = ex - (x() + 4 + cx * CELL_W), ly = ey - (y() + 6 + cy * CELL_H);
  bool on_icon = lx >= (CELL_W - 32) / 2 && lx < (CELL_W + 32) / 2 && ly < 36;
  bool on_label = ly >= 36 && ly < 54;
  return (on_icon || on_label) ? i : -1;
}

void FolderView::draw() {
  fl_color(FL_WHITE);
  fl_rectf(x(), y(), w(), h());
  fl_font(FONT_SMALL, SIZE_SMALL);
  for (int i = 0; i < (int)entries_.size(); i++) {
    const Entry &e = entries_[i];
    int cx = x() + 4 + (i % cols_) * CELL_W;
    int cy = y() + 6 + (i / cols_) * CELL_H;
    bool sel = i == sel_;
    Icon ic = e.icon;
    if (e.dir && is_open && is_open(e.path)) ic = Icon::OpenFolder;
    IconPair fi;
    if (!e.icon_file.empty()) fi = file_icon(e.icon_file);
    if (fi.normal) (sel ? fi.selected : fi.normal)->draw(cx + (CELL_W - 32) / 2, cy + 2);
    else draw_icon(ic, cx + (CELL_W - 32) / 2, cy + 2, sel);

    std::string lbl = ellipsize(e.label, CELL_W - 6);
    int tw = (int)fl_width(lbl.c_str());
    int tx = cx + (CELL_W - tw) / 2, ty = cy + 38;
    if (sel) {
      fl_color(FL_BLACK);
      fl_rectf(tx - 2, ty, tw + 4, fl_height());
      fl_color(FL_WHITE);
    } else {
      fl_color(FL_BLACK);
    }
    fl_draw(lbl.c_str(), tx, ty + fl_height() - fl_descent());
  }
}

int FolderView::handle(int e) {
  switch (e) {
  case FL_PUSH: {
    int i = index_at(Fl::event_x(), Fl::event_y());
    if (i != sel_) { sel_ = i; redraw(); }
    Fl::focus(this);
    if (i >= 0 && Fl::event_clicks() && on_open) {
      Fl::event_clicks(0);
      Entry copy = entries_[i];   // kopia - callback może przebudować widok
      on_open(copy);
    }
    return 1;
  }
  case FL_FOCUS:
  case FL_UNFOCUS:
    return 1;
  case FL_KEYBOARD: {
    int k = Fl::event_key(), n = (int)entries_.size();
    if (!n) return 0;
    int s = sel_ < 0 ? 0 : sel_;
    if (k == FL_Right) s = std::min(n - 1, s + 1);
    else if (k == FL_Left) s = std::max(0, s - 1);
    else if (k == FL_Down) s = std::min(n - 1, s + cols_);
    else if (k == FL_Up) s = std::max(0, s - cols_);
    else if ((k == FL_Enter || k == FL_KP_Enter) && sel_ >= 0 && on_open) {
      Entry copy = entries_[sel_];
      on_open(copy);
      return 1;
    } else return 0;
    sel_ = s;
    redraw();
    return 1;
  }
  }
  return Fl_Widget::handle(e);
}

// ---------------------------------------------------------------------------
FolderPane::FolderPane(int X, int Y, int W, int H, const std::string &path, bool hidden)
    : Fl_Group(X, Y, W, H), path_(path), hidden_(hidden) {
  box(FL_NO_BOX);
  scroll_ = new Fl_Scroll(X, Y + INFO_H, W, H - INFO_H);
  scroll_->type(Fl_Scroll::BOTH_ALWAYS);
  scroll_->box(FL_FLAT_BOX);
  scroll_->color(FL_WHITE);
  scroll_->scrollbar_size(SCROLL_W);
  for (Fl_Scrollbar *sb : {&scroll_->scrollbar, &scroll_->hscrollbar}) {
    sb->box(TRACK_BOX);
    sb->slider(THUMB_BOX);
    sb->color(MID);
    sb->selection_color(fl_rgb_color(0xAA, 0xAA, 0xE6));
  }
  view_ = new FolderView(X, Y + INFO_H, W - SCROLL_W, H - INFO_H - SCROLL_W);
  scroll_->end();
  end();
  refresh();
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

void FolderPane::refresh() {
  view_->load(path_, hidden_);
  update_info();
  resize(x(), y(), w(), h());
  redraw();
}

void FolderPane::resize(int X, int Y, int W, int H) {
  Fl_Widget::resize(X, Y, W, H);
  int sx = scroll_->xposition(), sy = scroll_->yposition();
  scroll_->resize(X, Y + INFO_H, W, H - INFO_H);
  int inner_w = W - SCROLL_W, inner_h = H - INFO_H - SCROLL_W;
  view_->relayout(inner_w, inner_h);
  view_->position(X - sx, Y + INFO_H - sy);
  scroll_->scroll_to(0, std::min(sy, std::max(0, view_->h() - inner_h)));
  // Gdy wszystko się mieści, pasek jest wygaszony (szary suwak), jak w Mac OS 9.
  bool need = view_->h() > inner_h;
  scroll_->scrollbar.selection_color(need ? fl_rgb_color(0xAA, 0xAA, 0xE6) : GRAY);
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
  fl_color(FL_BLACK);
  std::string t = ellipsize(info_, w() - 10);
  fl_draw(t.c_str(), x(), y(), w(), INFO_H - 1, FL_ALIGN_CENTER);
  draw_children();
}
