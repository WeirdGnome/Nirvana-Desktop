# Changelog
All notable changes to Nirvana Desktop, newest first. Polish: [CHANGELOG.pl.md](CHANGELOG.pl.md).

## 0.0.46 – 2026-11-12

- **NirvanaSHIELD:** a firewall that lets only the programs you allowed use the internet – the `nirvana-shield` service (nftables + net_cls cgroup + the kernel's process events), Preferences > NirvanaSHIELD with big switches per program and three big modes (Protect, Block all internet, Protection off), a shield in the menu bar, and a notification with Allow/No when a program asks. Parental lock and an always-allowed home network.
- Nirvana's own notifications can carry action buttons handled inside Rambler.
- The .deb recommends `nftables`.

## 0.0.45 – 2026-11-12

- **Execute Command** remembers its history (`~/.config/nirvana/run-history`, the last 50) and suggests while typing: matching history, programs from `$PATH` (e.g. /usr/bin) and the user's AppImages (with their versions). ↑↓ choose, Tab completes, Enter or a double click runs.

## 0.0.44 – 2026-11-12

- **Retro 16 schemes:** *1.3*, *2.1*, *3.1*, *3.9* and *4.1* – the colors of the 16-bit home computer desktops of five eras, with the classic 16-bit window gadgets.
- Schemes can bring the classic gadgets (`window_style=classic`); classic title bars take the scheme's solid, gradient or glass bar colors.

## 0.0.43 – 2026-11-12

- **Notifications:** Rambler is the notification server (`org.freedesktop.Notifications`): windows at the top right in the current appearance with the program's picture, text and action buttons, urgent ones with a red stripe; a sound (the notification's own, the theme's or Nirvana's built-in chime).
- **Notification applet** (a bell): unread dot, the recent notifications, Do Not Disturb, Notification Sound, Clear.
- The session no longer starts other notification programs from autostart (`NIRVANA_NOTIFICATIONS=0` keeps them).

## 0.0.42 – 2026-11-12

- Fixed: the mouse wheel changed the volume anywhere on the desktop – now only over the volume icon (FLTK offers an unused wheel to all widgets of the window). The same fix for the tray icons and the calendar.

## 0.0.41 – 2026-11-12

- **Clipboard history:** a menu bar applet remembers the last 25 copied texts (in memory only); a click or Ctrl+Alt+V shows them, choosing one puts it back into the clipboard; Pause Recording and Clear History.

## 0.0.40 – 2026-11-12

- Insight respects windows with a fixed size (the same minimum and maximum size in `WM_NORMAL_HINTS`, e.g. Preferences and its panels): no zoom button, no resizing from the edges or the corner, maximize requests are ignored; in classic 16-bit windows no zoom and no size gadget.

## 0.0.39 – 2026-11-12

- **Bluetooth:** an applet in the menu bar (on/off, paired devices with connect/disconnect and battery level, Add a Device…) and Preferences > Bluetooth (search, pairing with code confirmation or PIN, connect, disconnect, forget, visibility). Works through `bluetoothctl` (bluez); readable messages for typical BlueZ errors.
- The .deb recommends `bluez` and `rfkill`.
- A CHANGELOG from now on, in English and Polish.

## 0.0.38 – 2026-11-12

- Preferences > Desktop > Window Behavior: **Classic 16-bit windows** – close gadget on the left, zoom and depth gadgets on the right, size gadget in the corner, a click activates without raising, the depth gadget sends the top window to the back or brings another to the front.
- Optional moving and resizing with an XOR outline.

## 0.0.37 – 2026-11-11

- Special > Force Quit a Window… (`Ctrl+Alt+K`, like xkill, Nirvana's own windows protected).

## 0.0.36 – 2026-11-11

- nirvana-pkexec: the Nirvana password window also works without a logind session (changing the login picture works again).

## 0.0.35 – 2026-11-11

- Alt+Tab window switcher with previews.

## 0.0.34 – 2026-11-11

- Eight more screen savers.

## 0.0.33 – 2026-11-11

- The menu bar shows the shader's memory separately.

## 0.0.32 – 2026-11-11

- New Nirvana Desktop logo and wallpaper.
- Error windows titled "Guru Meditation".
- AppImages: the processor architecture is checked before installing and running.

## 0.0.31 – 2026-11-11

- Updates applet: system packages only (no Flatpak).

## 0.0.30 – 2026-11-11

- Native screen saver and screen lock (nirvana-lock, PAM) with Preferences > Screen Saver.

## 0.0.29 – 2026-11-11

- Nirvana-DM: typing no longer paints over the letters already typed.

## 0.0.28 – 2026-11-11

- The .deb recommends the xcb libraries that Qt 6 AppImages need.

## 0.0.27 – 2026-11-11

- AppImages: missing system libraries are detected (ELF headers) and can be installed with one click.

## 0.0.26 – 2026-11-11

- Dialog buttons as wide as their labels.

## 0.0.25 – 2026-11-11

- Nirvana file dialog, also for GTK and Qt programs through xdg-desktop-portal.
- Nirvana polkit agent (password window).

## 0.0.24 – 2026-11-11

- Preferences > User: login picture, full name, password, automatic login.

## 0.0.23 – 2026-11-11

- Nirvana-DM: a 1990s-style login screen (LightDM greeter) with theme, background and language menus that persist.

## 0.0.22 – 2026-11-11

- Printer applet and Preferences > Printers with driver installation.

## 0.0.21 – 2026-11-11

- Glass menu bars with the glass title style (Glacier).

## 0.0.20 – 2026-11-11

- Glacier scheme with glass title bars and its wallpaper.

## 0.0.19 – 2026-11-11

- Preferences: at most 5 panel icons in a row.

## 0.0.18 – 2026-11-11

- Minimizing to the desktop icon is animated both ways.

## 0.0.17 – 2026-11-11

- Preferences > Desktop: windows may cover the icon column and the dock.

## 0.0.16 – 2026-11-11

- Nirvana's own memory in the menu bar, even spacing of the menu bar icons.

## 0.0.15 – 2026-11-11

- Clock applet with calendar, pomodoro, timer and stopwatch; Date & Time panel.

## 0.0.14 – 2026-11-11

- Software updates applet, battery state icons, power profiles without power-profiles-daemon.

## 0.0.13 – 2026-11-11

- Full-screen shaders (Preferences > Shader).

## 0.0.12 – 2026-11-11

- Category folder icons, undecorated windows, the system log without root.

## 0.0.11 – 2026-11-11

- About This Computer, GPL-3.0 licence and acknowledgements; no trademarked names.

## 0.0.10 – 2026-11-11

- Title bar buttons in scheme colors with bolder glyphs.

## 0.0.9 – 2026-11-11

- Title bar buttons also in inactive windows.

## 0.0.8 – 2026-11-11

- Yellow labels for minimized windows; quieter apt in the installer.

## 0.0.7 – 2026-11-11

- English everywhere by default; colored title buttons; Applications and Nirvana Shell icons.

## 0.0.6 – 2026-11-11

- Applications in ~/.nirvana-apps, desktop Applications and Preferences icons, context menus, crash fixes.

## 0.0.5 – 2026-11-11

- Root mode removed.

## 0.0.4 – 2026-11-11

- Icons of iconified windows of known applications.

## 0.0.3 – 2026-11-11

- First numbered version: the Insight window manager, the Rambler desktop and file manager, applets, control panels, Nirvana Shell, English and Polish, installer for Devuan 6.
