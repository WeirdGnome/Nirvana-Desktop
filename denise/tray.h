// Nirvana Desktop - the system tray in Rambler's menu bar
//
// Supports both protocols programs use:
// * XEmbed (freedesktop System Tray): Rambler takes the selection
// _NET_SYSTEM_TRAY_S<screen> and embeds the icon windows (GTK2/3, Qt4, Wine, xembed),
// * StatusNotifierItem over D-Bus (Qt5/6, KDE, Electron, libappindicator):
// when nobody else does it, Rambler is also org.kde.StatusNotifierWatcher.
// We draw the icons ourselves (icon theme or pixmaps), context menus
// come through com.canonical.dbusmenu and are shown as Platinum menus.
#pragma once

#include <FL/Fl_Widget.H>
#include <functional>

class Fl_Window;

namespace tray {

// Widget with the icons: put it in the menu bar; width() gives its width.
Fl_Widget *make(int X, int Y, int H);
// Start the tray. changed() is called when the number of icons changes (re-layout the bar).
void init(Fl_Window *desk, std::function<void()> changed);
int width();          // needed width (0 when empty)
void recolor();       // after a scheme change: background of the XEmbed windows
void shutdown();      // release the selection and hand back the icon windows (before a restart/exit)

}  // namespace tray
