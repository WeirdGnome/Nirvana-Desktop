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
  bool nirvana_session = true;                // under Insight: Nirvana Desktop handles idleness
};

Fl_Widget *make_volume(int X, int Y, int W, int H);
Fl_Widget *make_power(int X, int Y, int W, int H);
void init(const Host &host);   // multimedia keys, idleness, refreshing

// System menu commands (menu bar): lock, sleep, hibernate, log out,
// restart, shut down - with confirmation and a projection mode check.
enum class PowerAction { Lock = 1, Sleep, Hibernate, LogOut, Restart, ShutDown };
void power_action(PowerAction a);
bool power_action_available(PowerAction a);   // e.g. no lock program, no hibernation

}  // namespace applets
