// Nirvana Desktop - menu bar applets: volume and power (with projection mode)
//
// The icons sit on the right side of Rambler's menu bar. A click opens
// a Platinum style popup with sliders and buttons; the mouse wheel over the volume
// icon changes the volume. Multimedia keys (volume, mute,
// brightness) are grabbed on the X root window and shown in an OSD.
//
// In a Nirvana Desktop session (under Insight) the power applet also handles idleness:
// screen blanking and lock, sleeping and battery warnings.
#pragma once

#include <FL/Fl_Widget.H>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace applets {

using Buttons = std::vector<std::pair<std::string, std::function<void()>>>;

struct Host {
  std::function<void(const std::string &title, const std::string &text, Buttons)> dialog;
  std::function<void()> logout;              // end of the session
  std::function<void()> before_shutdown;     // saving the window state before shutting down
  std::function<void()> open_power_panel;    // the Power panel (nirvana-alice)
  std::function<void(const char *)> open_panel;
  std::function<void()> relayout;            // an applet changed its width: lay the menu bar out again   // any panel of nirvana-alice ("datetime"...)
  bool nirvana_session = true;                // under Insight: Nirvana Desktop handles idleness
};

Fl_Widget *make_volume(int X, int Y, int W, int H);
Fl_Widget *make_power(int X, int Y, int W, int H);
// The widths the applets need now (icon + 4 px on each side, so all gaps are equal)
int power_width();
int updates_width();
// System updates (updates.cxx): the number of updates on the icon, installing with progress.
Fl_Widget *make_updates(int X, int Y, int W, int H);
bool updates_supported();
// Printers (printer.cxx, CUPS): hidden (width 0) without printers and print jobs
Fl_Widget *make_printer(int X, int Y, int W, int H);
int printer_width();
void refresh_printers();
// Bluetooth (bluetooth.cxx, BlueZ): hidden (width 0) without an adapter
Fl_Widget *make_bluetooth(int X, int Y, int W, int H);
int bluetooth_width();
void refresh_bluetooth();
// Clipboard history (clipboard.cxx): the copied texts in memory, Ctrl+Alt+V shows them
Fl_Widget *make_clipboard(int X, int Y, int W, int H);
void clipboard_hotkey();   // grabs Ctrl+Alt+V on the root window
// The clock (clock.cxx): time, calendar, pomodoro, timer, stopwatch. Its width follows
// the content; relayout is called when it changes.
Fl_Widget *make_clock(int H, std::function<void()> relayout);   // a known package manager (or flatpak) is installed
void init(const Host &host);   // multimedia keys, idleness, refreshing
const Host &current_host();

// System menu commands (menu bar): lock, sleep, hibernate, log out,
// restart, shut down - with confirmation and a projection mode check.
enum class PowerAction { Lock = 1, Sleep, Hibernate, LogOut, Restart, ShutDown };
void power_action(PowerAction a);
bool power_action_available(PowerAction a);   // e.g. no lock program, no hibernation

}  // namespace applets
