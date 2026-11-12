# Changelog
All notable changes to Nirvana Desktop, newest first. Polish: [CHANGELOG.pl.md](CHANGELOG.pl.md).

## 0.0.61 – 2026-11-12

- **Applications as big icons**, like the Preferences window, instead of a list – in the Applications window and the category folders (the list is still under `Ctrl+2`, remembered). Application folders remembered as a list switch to icons once.
- **A one-color Nirvana logo**: black on light schemes, light on dark ones (menu bar, Rambler Advanced, About, the login and lock screens); screen savers draw it light on black. It changes at once with the scheme.
- **Fixed the "System default" pointer**: going back from a Retro 16 pointer to the system's changed nothing – the session stored Nirvana's choice as the starting pointer, and Xcursor turned "no theme" into that starting one anyway. Now the session remembers the system's real pointer and "System default" picks it by name (`Xcursor.theme` of the X resources or the `default` theme).

## 0.0.60 – 2026-11-12

- **Double click everywhere, Preferences too**: a single click on a panel icon in the Preferences window only selects it, a double click (or Enter) opens the panel.
- **The Applications icon opens a plain Rambler Simple window** with the category folders (Games, Graphics…), also when folder windows are set to Rambler Advanced. Category folders open in the same window, double-clicking the icon again brings it to the front, and the address reads *Applications* instead of *Nirvana HD/.nirvana-apps*.
- Fixed the label of a selected icon in Rambler being cut off at the top.

## 0.0.59 – 2026-11-12

- **The file list is the default view** in Rambler (both styles). Folders remembered with the old default (icons) are shown as a list once, after the update; a folder switched to icons afterwards keeps them.
- **A new color scheme reaches every window at once**: every Nirvana program (Nirvana Shell, the Preferences panels…) reloads the scheme and redraws when Appearance changes – before, only the desktop and the window manager did, and the other windows kept the old colors until they were opened again.

## 0.0.58 – 2026-11-12

- **Rambler no longer cuts file names**: the icon view shows the whole name in more lines under the icon (the grid rows grow as needed), the list view widens the name column to the longest name and scrolls sideways (the column headers follow). Desktop icons show their name in two lines, all of it when selected.
- **Open Nirvana Shell Here** in Rambler's context menu (on the background, or on a folder) and in the Window menu.
- **Emptying the Trash from Rambler**: Window > Empty Trash…, and Empty Trash… in the context menu inside the Trash. The Trash shows as *Trash* in the address.
- **Dragging files**: onto the folders, disks and the Trash of Rambler Advanced's tree (highlighted under the pointer), and **out to other programs** – leaving Nirvana's windows, the drag goes on as standard XDND with a `file://` list.
- Minimized Nirvana programs get their own picture on the desktop (Preferences panels the Preferences icon, Nirvana Shell the terminal) instead of the generic diamond.

## 0.0.57 – 2026-11-12

- The logo in the menu bar (the "Nirvana" menu) is transparent too – before it was drawn on a gray square, seen on other bar colors and on the open menu.
- Rambler Advanced: **the logo at the right end of the button bar**, in its own field (hidden when the window is too narrow for it).

## 0.0.56 – 2026-11-12

- Rambler's context menu showed **Open twice** (its own and the file type's first action of the same name) – now once.
- Rambler Advanced: the **Two Panes** button has a new picture (one window split into the source with a blue header and the destination), and the toolbar buttons are as wide as their labels – *Kopiuj do* and *Przenieś do* no longer run into each other.

## 0.0.55 – 2026-11-12

- The enso logo is **transparent** around: only the brush circle in the menu bar, About This Computer, the login and lock screens and the screen savers (the night-blue square is gone; the blue is taken out with soft edges and no blue fringe).

## 0.0.54 – 2026-11-12

- **The pointer changes at once everywhere.** Before, the windows already open kept the old pointer until they were closed and opened again (FLTK, GTK and Qt keep the pointers they made). Now the window manager gives every pointer already made, in every program, its new picture (XFixes), publishes the pointer for GTK programs (XSETTINGS – only when no other settings manager runs), and every Nirvana program – the Appearance panel itself too – follows the change.
- *Automatic* pointer size: 24 px times the interface scale (Xcursor's own guess gave a tiny 16 px pointer).
- **Retro 16 · classic and · outline redrawn** closer to the red pointers of the 16-bit home computer desktops: a leaning arrow with a wide head and a short thick tail; classic in three colors – red inside, a light edge up and left, black down and right, no line around, the pixels doubled at 24 and 32 px; outline the same made smooth with a thin black line and a shadow. Glass stays.

## 0.0.53 – 2026-11-12

- **A sound for emptying the Trash:** paper crumpled into a ball and thrown into the basket, in the spirit of the late 1990s desktops – Nirvana's own, synthesized (nothing recorded or copied). `~/.local/share/nirvana/sounds/trash-empty.wav` (or .oga/.ogg) replaces it, an empty file turns it off.
- **Disks in Rambler:** the tree of Rambler Advanced shows every disk – partitions, USB sticks, cards, discs – mounted or not, with their own icons (and the Trash empty or full). A click on a disk that is not mounted mounts it (an encrypted one asks for its password) and shows it; the tree changes when a disk is plugged in or taken out. The address names disks too: `PENDRIVE/Zdjecia`.

## 0.0.52 – 2026-11-12

- **NirvanaSHIELD asks about blocked name lookups.** A program that was not allowed and only looked up a name (DNS) – e.g. `git clone` / `cmake` fetching FLTK in a terminal – failed with "Could not resolve host" and nobody was asked: a dropped UDP packet fails at once and the program's socket was gone before the service could see whose it was. Now these lookups are redirected to the service (`127.0.0.1:5399`, `::1`), which finds the program while it waits, asks "May X use the internet?" with the name it looked up, and answers "refused" (the program still fails at once, as before).

## 0.0.51 – 2026-11-12

- **Red Retro 16 mouse pointers**, Nirvana's own (`tools/cursors/make_cursors.py` draws every shape): *classic* (the pixels of the 16-bit era – a slanted red arrow with a black edge and a light shine, enlarged by whole pixels), *outline* (red with a white outline and a shadow) and *glass* (a glassy red). Every pointer a desktop needs – arrow, busy clock (animated), arrow with a clock, text, hand, crosshair, move, the four resize arrows, forbidden, help, copy, link – with the names X11, GTK and Qt ask for. Installed into `share/icons`.
- **Preferences > Appearance > Mouse Pointer:** the system's pointer, the Retro 16 ones or any installed Xcursor theme, size Automatic or 16–64 px, with a preview of the arrow, the hand and the clock. It changes at once over the desktop and Nirvana's windows; the session passes it to the other programs (`XCURSOR_THEME`, `XCURSOR_SIZE`, `XCURSOR_PATH`). About This Computer shows its name.
- **The new logo:** an enso – one red brush stroke in a circle, checkered red and white, on night blue – in the menu bar, About, the login and lock screens and the screen savers (built in as PNG files of several sizes; `tools/logo/make_logo.py`). The screen saver with flying logos is now *Flying Enso*.

## 0.0.50 – 2026-11-12

- **Fixed: closing a folder window reset the desktop.** The explorer window's status timer kept running after the window was freed and crashed Rambler, and the session started it again. The timers now stop with the window.
- **Classic 16-bit windows can be switched off with a Retro 16 scheme.** Such a scheme used to force the gadgets, and the switch in Preferences > Desktop did nothing. Now the switch alone decides; choosing a Retro 16 scheme in Appearance turns it on, any other scheme turns it off.
- The home folder is shown as **Nirvana HD** everywhere – the Address field, the pane headers, the status bar, Find (`Nirvana HD/Muzyka`, not `~/Muzyka`); typing either works.
- The folder window styles are now called **Rambler Simple** and **Rambler Advanced** (Preferences > Desktop > Folder Windows).

## 0.0.49 – 2026-11-12

Rambler's explorer grows up:
- **Writable archives:** zip, tar(.gz/.bz2/.xz), 7z and cpio open through `archivemount` – copy into them, rename, delete, edit; when no window shows the archive, it is packed again in the background in its own format (only when something changed). Other formats stay a read-only copy.
- **Servers like disks:** SFTP, FTP and now **SMB**; a connected server is a disk icon on the desktop (Open, Disconnect), `sftp://` `ftp://` `smb://` addresses in the Address field, *Connect at every start* (without the password – it is asked for when needed).
- **Content sniffing:** the kind of a file comes from its first bytes (PNG, JPEG, ZIP, ELF, MP3… even without an ending or with a wrong one); file types can match the content (`image/*`); on servers the name decides (no network reading).
- **Jobs in the background:** copy, move, delete, Trash and archive saving run in their own threads, side by side; **Window > Jobs…** with progress, speed, time left, Pause/Resume, Cancel; it does not take the keyboard when it opens by itself.
- **Events per file type:** Double click, Shift + double click and Space (preview) with their own commands or `@preview`.
- **Metadata:** the *Information* panel with a thumbnail, picture dimensions and EXIF camera, MP3/FLAC/Ogg tags and length, PDF pages, program architecture, text lines; films and archive contents in the background. **Space** = quick look.
- **Session state:** both panes' folders, the history, the selection, list/icons, filters, the tree, the panel and two panes come back after logging in.
- **Filter and Find:** the *Filter* field (`*.jpg`, `kind:sound`, `rodzaj:obraz`…) and **Window > Find…** (`Ctrl+F`): name, kind from the content, text inside, tags, size, date – in the background.
- Insight: a window that does not want the keyboard (`WM_HINTS` input = false, Rambler's job progress) comes up without taking the focus.
- Fixes: a relative address in the Address field; only one window per saved explorer window at the start.
- The package recommends `archivemount`, suggests `ffmpeg`.

## 0.0.48 – 2026-11-12

- **Rambler as an explorer** (Preferences > Desktop > Folder Windows): a bar of big flat buttons with labels, the Address field, the folder tree, one or two panes (SOURCE/DESTINATION: F5 copy, F6 move, F7 new folder, F8 delete, Tab swap, dragging between them) and a status bar. The usual Rambler stays the default.
- **File types** with actions (Special > File Types…): the double click and context menu actions with `{f} {F} {s} {d} {n}` placeholders, `@browse`, `@wallpaper`, `@shell`; built-in types for archives, scripts, text, pictures, packages.
- **Archives open like folders** (bsdtar), Extract Here / to the Other Pane, Icons > Pack into an Archive….
- **Servers:** Window > Connect to a Server… – SFTP (sshfs) and FTP (gio/gvfs) shown like folders, in the tree and in both panes.
- The package recommends `libarchive-tools` and `sshfs`, suggests `gvfs-backends` and `gvfs-fuse`.

## 0.0.47 – 2026-11-12

- The NirvanaSHIELD applet icon is a monochrome shield in the menu bar text color, like the other applets: filled with a check mark (protecting), filled with a bar (all internet blocked), an outline (protection off), a dotted, dimmed outline (the service is not running).

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
