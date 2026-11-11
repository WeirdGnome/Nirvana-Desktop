// Nirvana Desktop - gary: the application switcher (the application menu in the top right corner)
//
// For now it lives in the Rambler process, because Rambler draws the global menu bar.
// It works on the EWMH standard, so it shows applications under Insight and under
// other desktops (GNOME, KDE, Openbox):
// * window list:       _NET_CLIENT_LIST
// * active window:     _NET_ACTIVE_WINDOW
// * hiding:            WM_CHANGE_STATE (IconicState), the _NET_WM_STATE_HIDDEN state
// * restoring:         _NET_ACTIVE_WINDOW
// * closing:           _NET_CLOSE_WINDOW
// An application = a group of windows with the same WM_CLASS.
#pragma once

#include <FL/Fl_Image.H>
#include <X11/Xlib.h>
#include <string>
#include <vector>

namespace gary {

struct App {
  std::string cls;            // WM_CLASS (the group key)
  std::string name;           // display name
  std::string icon_file;      // icon from the AppImage (if this is an application started from Rambler)
  std::vector<Window> wins;   // windows of this application (order from the window manager)
  bool hidden = false;        // all windows hidden
};

void init(const std::string &home);

// Rambler starts an AppImage: the first new window class that appears
// within some seconds gets this name and icon (remembered permanently).
void note_launch(const std::string &name, const std::string &icon_file);

// The window list changed (PropertyNotify _NET_CLIENT_LIST) - assign the new classes.
void clients_changed();

// Applications outside Rambler; skip(w) == true for Rambler and desktop windows.
std::vector<App> list_apps(bool (*skip)(Window));

Window active_window();
bool is_hidden(Window w);
void activate(Window w);       // show (if hidden), raise, give the focus
void hide(Window w);           // hide (minimize)
void close(Window w);          // ask the window manager to close

// Window icon from _NET_WM_ICON at size x size (nullptr if the program does not give one).
Fl_Image *window_icon(Window w, int size = 16);
// The program's icon from the .desktop file (by WM_CLASS) and the icon theme - when the window
// has no _NET_WM_ICON or only a small one. size x size, nullptr when not found.
Fl_Image *desktop_icon(Window w, int size = 32);
std::string window_title(Window w);   // _NET_WM_NAME or WM_NAME

} // namespace gary
