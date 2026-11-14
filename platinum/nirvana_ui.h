// Nirvana Desktop - the Nirvana UI classes
//
//   NirvanaThemeManager   the clean-room themes (Classic 1.3 ... NextGen 4.1, Redmond97 SE,
//                         Nirvana Default, Slate, Ice & Silver): choosing, applying, telling
//                         every Nirvana program; drawing bevels and title bars in the theme
//   NirvanaGroup          a layout group in the spirit of the classic object-oriented toolkits:
//   NirvanaVGroup         children are placed by the group - one under another (V) or side by
//   NirvanaHGroup         side (H), with spacing, an inner margin, an optional framed title and
//                         weights (how the free room is shared); a fixed size per child is
//                         possible. No coordinates in the panels any more.
//   NirvanaButton         a bevelled push button of the theme (also a default button)
//   NirvanaString         a recessed text field with its label on the left
//   NirvanaLabel          a text (wraps), NirvanaSpace - an empty, stretching space
//   NirvanaWindow         (framewin.h) a window with Nirvana's own frame when no window manager
//                         draws one
#pragma once
#include "framewin.h"
#include "platinum.h"

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Tabs.H>
#include <functional>

#include <map>
#include <string>
#include <vector>

namespace plat {

// ---------------------------------------------------------------------------------------------
class NirvanaThemeManager {
public:
  // The themes Nirvana brings (clean-room - only the eras are named)
  enum ThemeType { CLASSIC_1_3, CLASSIC_2_1, CLASSIC_3_1, CLASSIC_3_9, CLASSIC_3_2, NEXTGEN_4_1, REDMOND_97,
                   NIRVANA_DEFAULT, SLATE, ICE_SILVER, PLATINUM, GRAPHITE, CUSTOM };
  static NirvanaThemeManager &instance();
  static const char *scheme_id(ThemeType t);   // "retro16/3-2" ...
  static ThemeType type_of(const std::string &scheme_id);
  ThemeType current() const;
  std::string current_name() const;            // "Nirvana Classic 3.2"
  // Chooses the theme: saved in the appearance settings, applied here and announced to every
  // Nirvana program (they reload it at once); classic themes switch on the classic gadgets
  void setTheme(ThemeType type);
  void setTheme(const std::string &scheme_id);
  // Drawing in the theme
  void drawBevel(int x, int y, int w, int h, bool recessed);
  void drawTitlebar(int x, int y, int w, int h, const char *title, bool active);
  void drawButton(int x, int y, int w, int h, bool pressed, bool is_default);
};

// ---------------------------------------------------------------------------------------------
// The layout: every child gets a size along the group's direction - its fixed size, or a share
// of the free room by its weight (0: its preferred size). Across, it fills the group.
class NirvanaGroup : public Fl_Group {
public:
  enum Direction { VERTICAL, HORIZONTAL };
  NirvanaGroup(Direction d, int X, int Y, int W, int H, const char *title = nullptr);
  // the next child (added with begin()/end() or add()): its weight or fixed size
  void weight(Fl_Widget *w, int weight);
  void fixed(Fl_Widget *w, int size);
  void spacing(int s) { spacing_ = s; }
  void margin(int m) { margin_ = m; }
  void framed(bool f) { framed_ = f; }   // a groove around (with the title in it)
  void show_title(bool s) { title_shown_ = s; }   // off: the label is only a name (a tab's title)
  // the size the children need (their preferred sizes, spacing and margins)
  virtual int preferred_w();
  virtual int preferred_h();
  void layout();                          // places the children (also called by resize)
  void resize(int X, int Y, int W, int H) override;
  void draw() override;

protected:
  Direction dir_;
  int spacing_ = 6, margin_ = 0;
  bool framed_ = false, title_shown_ = true;
  std::map<Fl_Widget *, int> weight_, fixed_;
  int title_h() const;
  int pref_along(Fl_Widget *w);
  int pref_across(Fl_Widget *w);
};

class NirvanaVGroup : public NirvanaGroup {
public:
  NirvanaVGroup(int X, int Y, int W, int H, const char *title = nullptr) : NirvanaGroup(VERTICAL, X, Y, W, H, title) {}
};
class NirvanaHGroup : public NirvanaGroup {
public:
  NirvanaHGroup(int X, int Y, int W, int H, const char *title = nullptr) : NirvanaGroup(HORIZONTAL, X, Y, W, H, title) {}
};

// A widget's preferred size for the layout (any FLTK widget; the Nirvana ones say it themselves)
void preferred_size(Fl_Widget *w, int &pw, int &ph);

class NirvanaButton : public Fl_Button {
public:
  NirvanaButton(const char *label, bool is_default = false);
  void draw() override;
  bool is_default;
};

class NirvanaString : public Fl_Group {
public:
  NirvanaString(const char *label, const char *value = "");
  Fl_Input *input() { return in_; }
  const char *value() const { return in_->value(); }
  void value(const char *v) { in_->value(v); }
  void label_width(int w) { lw_ = w; resize(x(), y(), this->w(), h()); }
  void resize(int X, int Y, int W, int H) override;
private:
  Fl_Box *lb_;
  Fl_Input *in_;
  int lw_ = 90;
};

class NirvanaLabel : public Fl_Box {
public:
  explicit NirvanaLabel(const char *text, bool bold = false);
};

class NirvanaSpace : public Fl_Box {
public:
  NirvanaSpace() : Fl_Box(0, 0, 1, 1) {}
};

// ---------------------------------------------------------------------------------------------
// A preferences panel window: pages in tabs that can be added and removed at any time, an OK
// button. Each page is a NirvanaVGroup - the panel fills it, the window takes the size the
// largest page needs.
class NirvanaPreferencesWindow : public NirvanaWindow {
public:
  NirvanaPreferencesWindow(const char *title, int width = 520);
  NirvanaVGroup *add_tab(const std::string &title);     // a new page (begin() is open on it)
  void end_tab();                                       // closes the page
  bool remove_tab(const std::string &title);
  NirvanaVGroup *tab(const std::string &title);
  void show_tab(const std::string &title);
  std::vector<std::string> tab_titles() const;
  NirvanaHGroup *buttons() { return buttons_; }         // OK (more can be added)
  void fit();                                           // the size from the pages
  void prepare();                                       // fit() + the frame (before place())
  void show() override;
  std::function<void()> on_ok;

private:
  Fl_Tabs *tabs_;
  NirvanaHGroup *buttons_;
  std::vector<std::pair<std::string, NirvanaVGroup *>> pages_;
  int width_;
  bool finished_ = false;
};

}  // namespace plat
