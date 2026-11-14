// Nirvana Desktop - Rambler: NirvanaDock (dock.h)
#include "dock.h"
#include "desktop.h"

#include <FL/Fl.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <tuple>

namespace fs = std::filesystem;
using namespace plat;

static std::string dock_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  std::string base = x && *x ? std::string(x) : std::string(h ? h : "") + "/.config";
  return base + "/nirvana/dock";
}

// ---------------------------------------------------------------------------------------------
// The items
// ---------------------------------------------------------------------------------------------
void NirvanaDock::load() {
  items.clear();
  std::ifstream in(dock_file());
  std::string line;
  std::error_code ec;
  while (std::getline(in, line))
    if (!line.empty() && fs::exists(line, ec)) items.push_back(line);
}

void NirvanaDock::save() const {
  std::ofstream out(dock_file());
  for (auto &p : items) out << p << '\n';
}

void NirvanaDock::add(const std::vector<std::string> &paths) {
  for (auto &p : paths)
    if (std::find(items.begin(), items.end(), p) == items.end()) items.push_back(p);
  save();
}

void NirvanaDock::remove(const std::vector<std::string> &paths) {
  for (auto &p : paths) items.erase(std::remove(items.begin(), items.end(), p), items.end());
  save();
}

// ---------------------------------------------------------------------------------------------
// The geometry
// ---------------------------------------------------------------------------------------------
int NirvanaDock::tile() const { return c_->desk_set_.dock_size + 8; }
bool NirvanaDock::floating() const { return c_->desk_set_.dock_floating; }

int NirvanaDock::space() const { return floating() ? tile() + 2 * PAD + LIFT : tile(); }

void NirvanaDock::rect(int &X, int &Y, int &W, int &H) const {
  if (items.empty()) { X = Y = W = H = 0; return; }
  if (floating()) {
    H = tile() + 2 * PAD;
    Y = c_->h() - LIFT - H;
    if (collapsed) { W = GRIP + 8; X = (c_->w() - W) / 2; return; }   // only the grip
    X = x_;
    W = w_;
    return;
  }
  X = 0; H = tile(); Y = c_->h() - H;
  W = TAB + shown_ * tile() + END;
}

bool NirvanaDock::grip_at(int ex, int ey) const {
  int X, Y, W, H;
  rect(X, Y, W, H);
  if (!W) return false;
  if (!floating()) return ex < TAB && ey >= c_->h() - tile();
  return ex >= X && ex < X + GRIP + 6 && ey >= Y && ey < Y + H;
}

std::vector<NirvanaDock::Slot> NirvanaDock::layout(const std::function<std::string(const std::string &)> &group) {
  std::vector<Slot> out;
  shown_ = 0;
  divs_.clear();
  if (collapsed) return out;
  std::vector<std::string> shown;
  for (auto &p : items) {
    std::error_code ec;
    if (fs::exists(p, ec)) shown.push_back(p);
  }
  bool fl = floating();
  int T = tile(), W = c_->w(), H = c_->h();
  int lead = fl ? GRIP + 4 : TAB, tail = fl ? 6 : END;
  // how wide it would be: the tiles that fit
  int width = lead + tail, n = 0;
  std::string prev;
  for (auto &p : shown) {
    std::string g = group(p);
    int add = T + (fl && n && g != prev ? DIV : 0);
    if (width + add > W - 20) break;
    width += add;
    prev = g;
    n++;
  }
  int x0 = fl ? (W - width) / 2 : 0;
  int tx = x0 + lead;
  int ty = fl ? H - LIFT - PAD - T : H - T;
  prev.clear();
  for (int i = 0; i < n; i++) {
    std::string g = group(shown[i]);
    if (fl && i && g != prev) { divs_.push_back(tx + DIV / 2); tx += DIV; }
    prev = g;
    out.push_back({shown[i], tx, ty});
    tx += T;
    shown_++;
  }
  w_ = items.empty() ? 0 : tx - x0 + tail;
  x_ = x0;
  return out;
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------
// A dock icon of any size: pictures of programs scaled, Nirvana's vector icons drawn at 32 px
// on the tile color and enlarged (64: by whole pixels, crisp) or made smaller
static Fl_Image *dock_vector_icon(Icon icon, int size) {
  static std::map<std::tuple<Fl_Color, int, int, int>, Fl_Image *> cache;
  auto key = std::make_tuple(Fl::get_color(GRAY), (int)icon, size, (int)icon_style());
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  Fl_Image_Surface surf(32, 32);
  Fl_Surface_Device::push_current(&surf);
  fl_color(GRAY);
  fl_rectf(0, 0, 32, 32);
  draw_icon(icon, 0, 0, false);
  Fl_Surface_Device::pop_current();
  Fl_RGB_Image *src = surf.image();
  Fl_RGB_Scaling was = Fl_Image::RGB_scaling();
  Fl_Image::RGB_scaling(size % 32 == 0 ? FL_RGB_SCALING_NEAREST : FL_RGB_SCALING_BILINEAR);
  Fl_Image *img = src->copy(size, size);
  Fl_Image::RGB_scaling(was);
  delete src;
  cache[key] = img;
  return img;
}

static void draw_dock_icon(const Entry &e, int x, int y, int size) {
  if (size == 32 || !e.icon_file.empty()) draw_entry_icon(e, x, y, false, size);
  else dock_vector_icon(e.icon, size)->draw(x, y);
}

void NirvanaDock::draw() {
  int X, Y, W, H;
  rect(X, Y, W, H);
  if (!W) return;
  const int T = tile(), size = c_->desk_set_.dock_size;
  auto &icons = c_->icons_;
  if (floating()) {
    // a bevelled tray with rounded ends, a black outline, a lit top edge
    fl_color(DARK);
    fl_rounded_rectf(X, Y, W, H, 6);
    fl_color(GRAY);
    fl_rounded_rectf(X + 1, Y + 1, W - 2, H - 2, 5);
    fl_color(LIGHT);
    fl_xyline(X + 5, Y + 1, X + W - 6);
    fl_yxline(X + 1, Y + 5, Y + H - 6);
    fl_color(SHADOW);
    fl_xyline(X + 5, Y + H - 2, X + W - 6);
    fl_yxline(X + W - 2, Y + 5, Y + H - 6);
    // the grip: ribs
    int gx = X + 5;
    for (int i = 0; i < 3; i++) {
      fl_color(LIGHT); fl_yxline(gx + i * 3, Y + 8, Y + H - 9);
      fl_color(SHADOW); fl_yxline(gx + i * 3 + 1, Y + 8, Y + H - 9);
    }
    // a little arrow under the grip: which way it folds
    int cx = gx + 4, cy = Y + H - 5;
    fl_color(TEXT);
    if (collapsed) fl_polygon(cx - 2, cy - 2, cx + 2, cy - 2, cx, cy);
    else fl_polygon(cx - 2, cy, cx + 2, cy, cx, cy - 2);
    // the dividers between groups
    for (int dx : divs_) {
      fl_color(SHADOW); fl_yxline(dx, Y + 6, Y + H - 7);
      fl_color(LIGHT); fl_yxline(dx + 1, Y + 6, Y + H - 7);
    }
    for (int i = 0; i < (int)icons.size(); i++) {
      auto &ic = icons[i];
      if (!ic.dock) continue;
      bool sel = i == c_->icon_sel_ || i == c_->icon_drop_;
      int bx = ic.x + 20, by = ic.y - 4;
      if (sel) fl_draw_box(DOWN_BOX, bx, by, T, T, fl_color_average(GRAY, SHADOW, 0.6f));
      draw_dock_icon(ic.entry, ic.x + 24 + (sel ? 1 : 0), ic.y + (sel ? 1 : 0), size);
    }
    return;
  }
  // rounded end: background with an outline, an arc on the right
  int ex = X + W - END - 6;
  fl_color(DARK);
  fl_rounded_rectf(ex, Y, END + 6, H, 7);
  fl_color(GRAY);
  fl_rounded_rectf(ex + 1, Y + 1, END + 4, H - 1, 6);
  fl_color(LIGHT);
  fl_xyline(ex + 1, Y + 1, X + W - 6);
  fl_color(SHADOW);
  for (int i = 0; i < 3; i++) fl_yxline(X + W - 9 + i * 2 - 2, Y + 12, Y + H - 12);   // handle
  // collapse tab
  fl_draw_box(UP_BOX, X, Y, TAB, H, GRAY);
  fl_color(DARK);
  fl_rect(X, Y, TAB + 1, H + 1);
  int cx = X + TAB / 2, cy = Y + H / 2;
  fl_color(TEXT);
  if (collapsed) fl_polygon(cx - 2, cy - 4, cx + 2, cy, cx - 2, cy + 4);
  else fl_polygon(cx + 2, cy - 4, cx - 2, cy, cx + 2, cy + 4);
  // tiles
  for (int i = 0; i < (int)icons.size(); i++) {
    auto &ic = icons[i];
    if (!ic.dock) continue;
    bool sel = i == c_->icon_sel_ || i == c_->icon_drop_;
    int bx = ic.x + 20, by = ic.y - 4;
    fl_draw_box(sel ? DOWN_BOX : UP_BOX, bx, by, T, T, sel ? fl_color_average(GRAY, SHADOW, 0.6f) : GRAY);
    fl_color(DARK);
    fl_rect(bx, by, T + 1, T + 1);
    draw_dock_icon(ic.entry, ic.x + 24, ic.y, size);
  }
}

void NirvanaDock::toggle() {
  collapsed = !collapsed;
  auto &icons = c_->icons_;
  if (collapsed && c_->icon_sel_ >= 0 && c_->icon_sel_ < (int)icons.size() && icons[c_->icon_sel_].dock) c_->icon_sel_ = -1;
  c_->save_rambler_conf();
  c_->layout_icons();
  c_->redraw();
}
