// Nirvana Desktop - the Nirvana UI classes (nirvana_ui.h)
#include "nirvana_ui.h"
#include "i18n.h"

#include <FL/Fl.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Round_Button.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstring>

namespace plat {

// ============================================================================================
// NirvanaThemeManager
// ============================================================================================
NirvanaThemeManager &NirvanaThemeManager::instance() {
  static NirvanaThemeManager m;
  return m;
}

static const struct { NirvanaThemeManager::ThemeType t; const char *id; } THEMES[] = {
    {NirvanaThemeManager::CLASSIC_1_3, "retro16/1-3"},     {NirvanaThemeManager::CLASSIC_2_1, "retro16/2-1"},
    {NirvanaThemeManager::CLASSIC_3_1, "retro16/3-1"},     {NirvanaThemeManager::CLASSIC_3_9, "retro16/3-9"},
    {NirvanaThemeManager::CLASSIC_3_2, "retro16/3-2"},     {NirvanaThemeManager::NEXTGEN_4_1, "retro16/4-1"},
    {NirvanaThemeManager::REDMOND_97, "redmond97-se/classic"},
    {NirvanaThemeManager::NIRVANA_DEFAULT, "nirvana-default"}, {NirvanaThemeManager::SLATE, "slate"},
    {NirvanaThemeManager::ICE_SILVER, "ice-silver"},       {NirvanaThemeManager::PLATINUM, "platinum"},
    {NirvanaThemeManager::GRAPHITE, "graphite"},
};

const char *NirvanaThemeManager::scheme_id(ThemeType t) {
  for (auto &e : THEMES) if (e.t == t) return e.id;
  return "nirvana-default";
}

NirvanaThemeManager::ThemeType NirvanaThemeManager::type_of(const std::string &id) {
  for (auto &e : THEMES) if (id == e.id) return e.t;
  return CUSTOM;
}

NirvanaThemeManager::ThemeType NirvanaThemeManager::current() const { return type_of(current_appearance().scheme); }
std::string NirvanaThemeManager::current_name() const { return current_scheme().name; }

void NirvanaThemeManager::setTheme(ThemeType type) {
  if (type != CUSTOM) setTheme(std::string(scheme_id(type)));
}

void NirvanaThemeManager::setTheme(const std::string &id) {
  Scheme s;
  if (!load_scheme(id, s)) return;
  Appearance a = read_appearance();
  a.scheme = id;
  a.style = -1;   // the theme's own title bars
  a.desk = -1;    // ... and desktop color
  write_appearance(a);
  // a classic theme brings the classic gadgets (the switch in Preferences > Desktop follows)
  DesktopSettings d = read_desktop();
  if (d.classic != s.classic) {
    d.classic = s.classic;
    write_desktop(d);
  }
  reload_appearance();
  notify_appearance_changed();   // Rambler, Insight and every open panel reload it
}

void NirvanaThemeManager::drawBevel(int x, int y, int w, int h, bool recessed) {
  fl_draw_box(recessed ? DOWN_BOX : UP_BOX, x, y, w, h, GRAY);
}

void NirvanaThemeManager::drawTitlebar(int x, int y, int w, int h, const char *title, bool active) {
  (void)h;   // the title bar has the theme's height
  if (classic_look(read_desktop().classic)) draw_classic_titlebar(x, y, w, title, active, 0, 1);
  else draw_titlebar(x, y, w, title, active, 0, 1);
}

void NirvanaThemeManager::drawButton(int x, int y, int w, int h, bool pressed, bool is_default) {
  if (is_default) {   // the default button: a second, bold border around
    fl_color(DARK);
    fl_rect(x, y, w, h);
    fl_rect(x + 1, y + 1, w - 2, h - 2);
    x += 3; y += 3; w -= 6; h -= 6;
  }
  fl_draw_box(pressed ? BUTTON_DOWN : BUTTON_BOX, x, y, w, h, GRAY);
}

// ============================================================================================
// The layout
// ============================================================================================
void preferred_size(Fl_Widget *w, int &pw, int &ph) {
  if (auto *g = dynamic_cast<NirvanaGroup *>(w)) { pw = g->preferred_w(); ph = g->preferred_h(); return; }
  fl_font(w->labelfont(), w->labelsize());
  int lw = w->label() ? (int)fl_width(w->label()) : 0;
  if (dynamic_cast<NirvanaString *>(w)) { pw = 260; ph = 24; return; }
  if (dynamic_cast<Fl_Check_Button *>(w) || dynamic_cast<Fl_Round_Button *>(w)) { pw = lw + 26; ph = 20; return; }
  if (auto *c = dynamic_cast<Fl_Choice *>(w)) {
    int mw = 60;
    fl_font(c->textfont(), c->textsize());
    for (int i = 0; i < c->size(); i++)
      if (const char *t = c->menu()[i].label()) mw = std::max(mw, (int)fl_width(t) + 34);
    pw = mw; ph = 22; return;
  }
  if (dynamic_cast<NirvanaButton *>(w) || dynamic_cast<Fl_Button *>(w)) { pw = std::max(86, lw + 24); ph = 26; return; }
  if (auto *b = dynamic_cast<Fl_Box *>(w)) {
    if (!b->label() || !*b->label()) { pw = 1; ph = 1; return; }
    int mw = (b->align() & FL_ALIGN_WRAP) ? std::max(40, b->w()) : 0, mh = 0;
    fl_measure(b->label(), mw, mh, 0);
    pw = mw + 2; ph = mh + 2; return;
  }
  pw = w->w(); ph = w->h();
}

NirvanaGroup::NirvanaGroup(Direction d, int X, int Y, int W, int H, const char *title)
    : Fl_Group(X, Y, W, H), dir_(d) {
  if (title) { copy_label(title); framed_ = true; margin_ = 10; }
  labelfont(FONT_SYSTEM);
  labelsize(SIZE_SYSTEM);
  align(FL_ALIGN_TOP_LEFT | FL_ALIGN_INSIDE);
  box(FL_NO_BOX);
}

void NirvanaGroup::weight(Fl_Widget *w, int wt) { weight_[w] = wt; }
void NirvanaGroup::fixed(Fl_Widget *w, int size) { fixed_[w] = size; }

int NirvanaGroup::title_h() const { return title_shown_ && label() && *label() ? 18 : 0; }

int NirvanaGroup::pref_along(Fl_Widget *w) {
  auto f = fixed_.find(w);
  if (f != fixed_.end()) return f->second;
  int pw, ph;
  preferred_size(w, pw, ph);
  return dir_ == VERTICAL ? ph : pw;
}
int NirvanaGroup::pref_across(Fl_Widget *w) {
  int pw, ph;
  preferred_size(w, pw, ph);
  return dir_ == VERTICAL ? pw : ph;
}

int NirvanaGroup::preferred_w() {
  int sum = 0, mx = 0, n = 0;
  for (int i = 0; i < children(); i++) {
    Fl_Widget *c = child(i);
    if (!c->visible()) continue;
    if (dir_ == HORIZONTAL) sum += pref_along(c); else mx = std::max(mx, pref_across(c));
    n++;
  }
  int inner = dir_ == HORIZONTAL ? sum + spacing_ * std::max(0, n - 1) : mx;
  if (title_h()) {
    fl_font(labelfont(), labelsize());
    inner = std::max(inner, (int)fl_width(label()) + 8);
  }
  return inner + 2 * margin_;
}

int NirvanaGroup::preferred_h() {
  // a wrapping text needs the width it will get: the children are given it first
  int iw = w() - 2 * margin_;
  int sum = 0, mx = 0, n = 0;
  for (int i = 0; i < children(); i++) {
    Fl_Widget *c = child(i);
    if (!c->visible()) continue;
    if (dir_ == VERTICAL && c->w() != iw && iw > 0) c->size(iw, c->h());
    if (dir_ == VERTICAL) sum += pref_along(c); else mx = std::max(mx, pref_across(c));
    n++;
  }
  int inner = dir_ == VERTICAL ? sum + spacing_ * std::max(0, n - 1) : mx;
  return inner + 2 * margin_ + title_h();
}

void NirvanaGroup::layout() {
  int X = x() + margin_, Y = y() + margin_ + title_h();
  int W = w() - 2 * margin_, H = h() - 2 * margin_ - title_h();
  std::vector<Fl_Widget *> kids;
  for (int i = 0; i < children(); i++) if (child(i)->visible()) kids.push_back(child(i));
  if (kids.empty()) return;
  int along = dir_ == VERTICAL ? H : W;
  if (dir_ == VERTICAL)   // the width first: wrapping texts measure their height with it
    for (auto *c : kids) if (c->w() != W) c->size(W, c->h());
  int used = spacing_ * ((int)kids.size() - 1), weights = 0;
  std::vector<int> size(kids.size());
  for (size_t i = 0; i < kids.size(); i++) {
    int wt = weight_.count(kids[i]) ? weight_[kids[i]] : 0;
    if (wt > 0 && !fixed_.count(kids[i])) { weights += wt; size[i] = -wt; continue; }
    size[i] = pref_along(kids[i]);
    used += size[i];
  }
  int free_room = std::max(0, along - used), given = 0, last_weighted = -1;
  for (size_t i = 0; i < kids.size(); i++)
    if (size[i] < 0) last_weighted = (int)i;
  for (size_t i = 0; i < kids.size(); i++)
    if (size[i] < 0) {
      int wt = -size[i];
      size[i] = (int)i == last_weighted ? free_room - given : free_room * wt / std::max(1, weights);
      given += size[i];
    }
  int pos = dir_ == VERTICAL ? Y : X;
  for (size_t i = 0; i < kids.size(); i++) {
    Fl_Widget *c = kids[i];
    if (dir_ == VERTICAL) c->resize(X, pos, W, size[i]);
    else c->resize(pos, Y, size[i], H);
    if (auto *g = dynamic_cast<NirvanaGroup *>(c)) g->layout();
    pos += size[i] + spacing_;
  }
}

void NirvanaGroup::resize(int X, int Y, int W, int H) {
  Fl_Widget::resize(X, Y, W, H);   // (not Fl_Group's: the children are placed by layout())
  layout();
}

void NirvanaGroup::draw() {
  if (framed_) {
    int ty = y() + (title_h() ? title_h() / 2 : 0);
    fl_draw_box(FL_ENGRAVED_FRAME, x(), ty, w(), h() - (ty - y()), GRAY);
    if (title_h()) {
      fl_font(labelfont(), labelsize());
      int tw = (int)fl_width(label());
      fl_color(GRAY);
      fl_rectf(x() + 8, y(), tw + 8, title_h());
      fl_color(TEXT);
      fl_draw(label(), x() + 12, y(), tw, title_h(), FL_ALIGN_LEFT | FL_ALIGN_INSIDE, nullptr, 0);
    }
  } else if (title_h()) {
    fl_font(labelfont(), labelsize());
    fl_color(TEXT);
    fl_draw(label(), x() + margin_, y() + margin_, w(), title_h(), FL_ALIGN_LEFT | FL_ALIGN_INSIDE, nullptr, 0);
  }
  draw_children();
}

// ============================================================================================
// The widgets
// ============================================================================================
NirvanaButton::NirvanaButton(const char *l, bool def) : Fl_Button(0, 0, 86, 26), is_default(def) {
  copy_label(l);
  labelsize(SIZE_SMALL + 1);
  if (def) { labelfont(FONT_SYSTEM); shortcut(FL_Enter); }
}

void NirvanaButton::draw() {
  bool down = value() != 0;
  NirvanaThemeManager::instance().drawButton(x(), y(), w(), h(), down, is_default);
  draw_label(x() + (down ? 1 : 0), y() + (down ? 1 : 0), w(), h());
  if (Fl::focus() == this) draw_focus(BUTTON_BOX, x() + 3, y() + 3, w() - 6, h() - 6);
}

NirvanaString::NirvanaString(const char *l, const char *v) : Fl_Group(0, 0, 260, 24) {
  lb_ = new Fl_Box(0, 0, lw_, 24);
  lb_->copy_label(l);
  lb_->labelsize(SIZE_SMALL + 1);
  lb_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  in_ = new Fl_Input(lw_, 1, 170, 22);
  in_->box(WELL_BOX);
  in_->textsize(SIZE_SMALL + 1);
  in_->value(v);
  end();
  resizable(in_);
}

void NirvanaString::resize(int X, int Y, int W, int H) {
  Fl_Widget::resize(X, Y, W, H);
  lb_->resize(X, Y, lw_, H);
  in_->resize(X + lw_, Y + (H - 22) / 2, std::max(20, W - lw_), 22);
}

NirvanaLabel::NirvanaLabel(const char *text, bool bold) : Fl_Box(0, 0, 200, 18) {
  copy_label(text);
  labelsize(bold ? SIZE_SYSTEM : SIZE_SMALL);
  if (bold) labelfont(FONT_SYSTEM);
  align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
}

// ============================================================================================
// NirvanaPreferencesWindow
// ============================================================================================
static const int TAB_H = 24, BTN_H = 40;

NirvanaPreferencesWindow::NirvanaPreferencesWindow(const char *title, int width)
    : NirvanaWindow(width, 400, title), width_(width) {
  tabs_ = new Fl_Tabs(8, 8, width - 16, 400 - 8 - BTN_H);
  tabs_->box(UP_BOX);
  tabs_->color(GRAY);
  tabs_->selection_color(GRAY);
  tabs_->labelsize(SIZE_SMALL + 1);
  tabs_->end();
  buttons_ = new NirvanaHGroup(8, 400 - BTN_H + 6, width - 16, 28);
  buttons_->spacing(8);
  {
    auto *sp = new NirvanaSpace;
    buttons_->weight(sp, 1);
    auto *ok = new NirvanaButton(_("OK"), true);
    ok->callback([](Fl_Widget *w, void *) {
      auto *pw = (NirvanaPreferencesWindow *)w->window();
      if (pw->on_ok) pw->on_ok();
      pw->hide();
    });
  }
  buttons_->end();
  end();
  resizable(tabs_);
}

NirvanaVGroup *NirvanaPreferencesWindow::add_tab(const std::string &title) {
  tabs_->begin();
  auto *g = new NirvanaVGroup(tabs_->x() + 2, tabs_->y() + TAB_H, tabs_->w() - 4, tabs_->h() - TAB_H - 2);
  g->copy_label(title.c_str());
  g->show_title(false);
  g->labelfont(FONT_SMALL);
  g->labelsize(SIZE_SMALL + 1);
  g->margin(12);
  g->spacing(5);
  if (!pages_.empty()) g->hide();
  pages_.push_back({title, g});
  return g;   // the page stays open (begin) for its widgets
}

void NirvanaPreferencesWindow::end_tab() {
  if (!pages_.empty()) pages_.back().second->end();
  tabs_->end();
  Fl_Group::current(nullptr);
  if (finished_) fit();
}

bool NirvanaPreferencesWindow::remove_tab(const std::string &title) {
  for (auto it = pages_.begin(); it != pages_.end(); ++it)
    if (it->first == title) {
      NirvanaVGroup *g = it->second;
      bool was = tabs_->value() == g;
      tabs_->remove(g);
      Fl::delete_widget(g);
      pages_.erase(it);
      if (was && !pages_.empty()) tabs_->value(pages_.front().second);
      if (finished_) fit();
      redraw();
      return true;
    }
  return false;
}

NirvanaVGroup *NirvanaPreferencesWindow::tab(const std::string &title) {
  for (auto &[t, g] : pages_) if (t == title) return g;
  return nullptr;
}

void NirvanaPreferencesWindow::show_tab(const std::string &title) {
  if (auto *g = tab(title)) { tabs_->value(g); redraw(); }
}

std::vector<std::string> NirvanaPreferencesWindow::tab_titles() const {
  std::vector<std::string> v;
  for (auto &p : pages_) v.push_back(p.first);
  return v;
}

void NirvanaPreferencesWindow::fit() {
  int ox = finished_ ? this->ox() : 0, oy = finished_ ? this->oy() : 0;
  int W = width_, ph = 120;
  // twice: the first pass gives every text its width, the second measures the wrapped heights
  for (int pass = 0; pass < 2; pass++)
    for (auto &[t, g] : pages_) {
      g->resize(ox + 10, oy + 8 + TAB_H, W - 20, std::max(ph, g->h()));
      ph = std::max(ph, g->preferred_h());
    }
  int H = 8 + TAB_H + ph + 4 + BTN_H;
  tabs_->resize(ox + 8, oy + 8, W - 16, TAB_H + ph + 2);
  for (auto &[t, g] : pages_) g->resize(ox + 10, oy + 8 + TAB_H, W - 20, ph);
  buttons_->resize(ox + 8, oy + H - BTN_H + 6, W - 16, 28);
  Fl_Widget *r = resizable();
  resizable(nullptr);   // (size() would scale the children)
  size(W + 2 * ox, H + oy + ox);
  resizable(r);
  init_sizes();
  redraw();
}

void NirvanaPreferencesWindow::prepare() {
  if (finished_) return;
  fit();
  finish();
  finished_ = true;
}

void NirvanaPreferencesWindow::show() {
  prepare();
  NirvanaWindow::show();
}

}  // namespace plat
