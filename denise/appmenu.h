// Nirvana Desktop - Rambler: the menus of other programs in the menu bar (global menu)
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace appmenu {

// One item of a program's menu (com.canonical.dbusmenu)
struct Item {
  int id = 0;
  std::string label;                 // mnemonics removed
  bool enabled = true, visible = true, separator = false;
  int toggle = 0;                    // 0 none, 1 check box, 2 radio
  bool checked = false;
  std::string shortcut;              // "Ctrl+S" (shown only)
  std::vector<Item> children;
};

// Starts the registrar (com.canonical.AppMenu.Registrar) on the session bus: Qt programs,
// GTK programs with the appmenu module and others register their windows' menus with it.
// changed() is called (in the interface thread) when the menus of the active window change.
void start(std::function<void()> changed);
// The active window changed (its X id; 0 - none)
void set_active(unsigned long xid);
// The top-level menus of the active window (empty: it has none registered)
const std::vector<Item> &menus();
// The program's name for the bar (from the active window: its class) - set by the caller
void activate(int id);   // an item clicked
void refresh();          // before the menu bar opens: fresh contents (programs fill menus late)

}  // namespace appmenu
