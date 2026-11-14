// Nirvana Desktop - Rambler: NirvanaDock, the dock
//
// The programs, folders and files kept at hand (~/.config/nirvana/dock). Two forms
// (Preferences > Desktop > Dock and Backdrop):
//   floating  a bevelled tray centered above the bottom edge: a grip on the left (a click folds
//             the dock to it), dividers between groups (folders, each applications category,
//             other programs and files)
//   strip     tiles in the bottom left corner with a collapse tab and a rounded end
// The dock places its tiles and draws itself; the desktop canvas shows the tiles as its icons
// (selection, dragging, opening, tooltips) - the dock asks it for the selected and dropped tile.
#pragma once
#include <functional>
#include <string>
#include <vector>

class NirvanaDesktopCanvas;

class NirvanaDock {
public:
  static constexpr int TAB = 14;    // collapse tab (strip)
  static constexpr int GRIP = 10;   // the grip (floating)
  static constexpr int DIV = 8;     // a divider between groups
  static constexpr int PAD = 4;     // the floating tray around its tiles
  static constexpr int LIFT = 6;    // ... and above the bottom edge
  static constexpr int END = 12;    // rounded end of the strip

  explicit NirvanaDock(NirvanaDesktopCanvas *canvas) : c_(canvas) {}

  // the items (paths)
  std::vector<std::string> items;
  void load();
  void save() const;
  void add(const std::vector<std::string> &paths);
  void remove(const std::vector<std::string> &paths);
  bool empty() const { return items.empty(); }

  // the geometry
  int tile() const;                          // a tile: the icon (Dock size) + its frame
  bool floating() const;
  int space() const;                         // the room it takes at the bottom (windows stay above)
  void rect(int &X, int &Y, int &W, int &H) const;   // the whole dock (empty: 0 0 0 0)
  bool grip_at(int ex, int ey) const;        // the grip / the collapse tab
  struct Slot { std::string path; int x, y; };       // a tile's top left corner
  // places the tiles that fit (group: the group of a path - a divider between different ones)
  std::vector<Slot> layout(const std::function<std::string(const std::string &)> &group);

  void draw();
  void toggle();                             // folds / unfolds
  bool collapsed = false;
  int press = -1;                            // the pressed tile (opens on release)

private:
  NirvanaDesktopCanvas *c_;
  int shown_ = 0, x_ = 0, w_ = 0;            // how many tiles fit, where the floating tray is
  std::vector<int> divs_;                    // its dividers (x)
};
