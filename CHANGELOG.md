<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Changelog

*[Polski](CHANGELOG.pl.md)*

## 3.0.0-alpha5 — unreleased (NirvanaNG, stage 5)

### Added
- Four themes beside Ambient, each with its own icons, gadgets and colours: Classic 1.3
  (four colours, sharp pixels, striped title bars), Classic 2.1 (grey with raised
  edges), Classic 3.1 (eight colours, a blue-grey backdrop), Classic 4.1 (smooth greys,
  gradient title bars).
- Removable drives on the desktop (UDisks2 on the system bus): USB sticks, memory cards
  and discs get a volume icon named by their label; opening one mounts it and shows it
  in a lister, Icons -> Eject (Super+J) unmounts it and ejects the medium or powers the
  drive off.
- Settings -> Theme: the themes found in `Themes:`, the one in use checked; a choice is
  kept in `~/.nirvana/Prefs/desktop.conf` and the desktop starts again in it.

### Changed
- The menus as Ambient's (`docs/AMBIENT-PARITY.md` follows how far Nirvana is): the
  screen's menus are Nirvana (My Computer, Execute Command, New Shell, About, About the
  System, Quit), Edit (Cut, Copy, Paste, Select All, Invert), View (New Drawer, List
  Mode, Sort By, Show All Files, Update), Icons (Information, Put Away, Eject, Rename,
  Delete, Trashcan, Format), Utilities, Settings (Theme, Assigns) and Tools (the
  programs in `Tools:`); Edit, View and Icons act on the lister last used, or on the
  desktop. What is not done yet is shown disabled. The Window menu is gone.
- Context menus: the right button on an icon (on the desktop, in a lister) or on a
  lister's background gives Ambient's context menu at the pointer - held and let go
  over an item, or clicked. A lister's spatial or browsing way is set with
  `lister=browse` in `~/.nirvana/Prefs/desktop.conf`.
- Disabled menu items are no longer lit under the pointer.
- The themes share their groups (`themes/common.edc`); a theme gives fonts, sizes,
  colour classes and images. `tools/make-theme-images.py` draws every theme's images.
- The resize gadget shows itself pressed.

## 3.0.0-alpha4 — 2026-11-21 (NirvanaNG, stage 4)

### Added
- The Nirvana Dock: launchers (.desktop files, AppImages, programs), sub-docks that
  open above their item, separators and dockies (clock, CPU, RAM); files dropped on it
  are pinned; an item's menu (Open, Move Left/Right, Remove from Dock); kept in
  `~/.nirvana/Prefs/dock.conf`. A layer surface on Wayland.
- The lister: views as icons or a list, sorting by name, date, size or type, browser
  mode, a location field, archives browsed as drawers (libarchive), an item menu
  (Open, Information, Copy Path, Extract Icon... for AppImages), dragging files out.
- `install.sh` for Rocky Linux 10 (and AlmaLinux 10, dnf): `--deps` turns on EPEL and
  CRB and installs the packages; EFL 1.28.1 and labwc 0.9.3 (with wlroots 0.19.2), which
  those systems do not package, are built from checked release tarballs and packaged. `--uninstall` removes them too.

### Changed
- `install.sh` makes packages (.deb or .rpm: `nirvana-desktop`, `nirvana-libdwarfs`,
  and `nirvana-efl`, `nirvana-labwc` where needed) and installs them with apt or dnf
  instead of copying files into the system; `--uninstall` removes the packages (and
  the files an older install.sh copied in), `--no-install` only makes them.
- Wayland only: the X11 session (`nirvana-session --x11`, the X11 login entry) and the
  desktop's X11 mode (`nirvana-desktop --x11`) are gone. X11 programs run through
  XWayland, and the global menu still serves them.
- The menu titles show while the right mouse button is held - on the bar or on the
  desktop - as on the classic desktops; the bar shows the screen's or the program's
  title otherwise (so "Nirvana Screen" is no longer there twice). Moving onto a title
  drops its menu, releasing over an item chooses it. `menus=always` in
  `~/.nirvana/Prefs/desktop.conf` keeps the titles on the bar.
- The system's volume icon is labelled "System" (its assign stays `SYS:`).

### Fixed
- On graphics hardware whose driver gives wlroots no GPU buffers (gbm_bo_create
  failing: a black screen), the session starts labwc again, first on the GPU without
  buffer modifiers (Intel), then with software rendering, and keeps what worked in `~/.nirvana/Prefs/wlr-renderer`. `install.sh --deps`
  installs the Mesa drivers (`libgl1-mesa-dri`, `libgbm1`).
- Menus left their texts behind on Wayland when another menu was opened or they were
  shut (the rows were not deleted with their panel).
- Sessions started by a display manager without elogind (LightDM on Devuan, say) were
  a black screen: `XDG_RUNTIME_DIR` was not set, and labwc does not start without it.
  `nirvana-session` now makes one, starts a session bus when there is none, logs to
  `~/.nirvana/Logs/session.log`, restarts the desktop after a crash and ends the
  session (back to the login screen) when it cannot start at all.

## 3.0.0-alpha3 — unreleased (NirvanaNG, stage 3)

### Added
- The global menu: the focused program's menus in the screen bar, after the Nirvana
  Screen menu. GTK programs through `org.gtk.Menus`/`org.gtk.Actions` (sections,
  submenus, accelerators, check and radio items); other programs through
  `com.canonical.AppMenu.Registrar`, which the desktop now is, and
  `com.canonical.dbusmenu` (shortcuts, disabled/check/radio items, on-demand submenus,
  live updates). The focus from `_NET_ACTIVE_WINDOW` on X11 and in XWayland; native
  Wayland programs get their name and a Window menu (close, iconify, zoom) through
  wlr-foreign-toplevel-management. A click on the desktop brings its own menus back.
- `install.sh --deps` installs `appmenu-gtk3-module` (and `appmenu-gtk2-module`).

### Fixed
- The session entries are installed even where `/usr/share/wayland-sessions` did not
  exist yet; an X11 entry is added to `/usr/share/xsessions`.

## 3.0.0-alpha2 — unreleased (NirvanaNG, stage 2)

### Added
- `nirvana-desktop` (EFL): the screen bar "Nirvana Screen" and the backdrop as
  wlr-layer-shell surfaces on Wayland (dock and desktop windows on X11); pull-down
  menus (Nirvana Screen, Window, Icons, Tools) with mouse, held right button, keyboard
  and Super shortcuts; the clock; volume icons for assigns with DESKTOP=1 (select, drag,
  rubber band, open, Snapshot, Clean Up, reloaded on SIGUSR1).
- Window gadgets on the desktop's windows: close; prefs, jump, iconify, zoom, depth;
  resize - move and resize through the display server.
- The Assigns editor: New, Clone, Delete; name, desktop icon, paths in order (Add Path,
  Remove Path, Move Up/Down, Open in Lister); Use, Save, Cancel.
- A first lister: icons read in a thread, AppImage icons, `#?` filter patterns.
- D-Bus service `org.nirvana.Desktop` (OpenDrawer, ReloadAssigns, ShowAssignsEditor).
- The Ambient theme (`themes/ambient.edc`, images drawn by
  `tools/make-theme-images.py`); the theme contract in `themes/README.md`.
- `install.sh`: build, test, install and set up in one go (`--deps`, `--no-tests`,
  `--no-dwarfs`, `--uninstall`); it downloads libdwarfs 0.15.10 (SHA-256 checked),
  builds and installs it for DwarFS AppImages, and copes with sources dated in the
  future (a clock behind) and sources it may not write in.
- The depth gadget lowers its window on Wayland under labwc (a "Task" button mouse
  binding to labwc's Lower, clicked on a virtual pointer); setup-env.sh says what to add
  to an own labwc rc.xml.
- DwarFS AppImages read through libdwarfs 0.15 (the bridge is C++23, as its headers
  are); a DwarFS test made with mkdwarfs when it is there.
- `nirvana-session` (labwc on Wayland, or beside a window manager on X11), labwc
  settings, a session entry for display managers.

## 3.0.0-alpha1 — unreleased (NirvanaNG, stage 1)

A new start on the `NirvanaNG` branch: new code, Meson, C11.

### Added
- `libnirvana` (LGPL-3.0-or-later):
  - the assigns database in `$XDG_RUNTIME_DIR/nirvana-ram/assigns.db` with atomic,
    locked changes; default assigns (SYS:, Home:, RAM:, Nirvana:, Themes:, Tools:, the
    XDG user directories, the folders of $HOME); multi-assigns; making, replacing,
    adding, removing, reordering, renaming and deleting; `nirvana_to_posix()` and
    `posix_to_nirvana()` (longest prefix); persistence in `~/.nirvana/Startup-Sequence`;
    the running desktop told with SIGUSR1;
  - an AppImage reader that runs nothing: the ELF payload offset (ELF64 and ELF32), an
    own SquashFS 4.0 reader (gzip, lzma, xz, lz4, zstd), ISO 9660 through libarchive,
    DwarFS through libdwarfs (optional); the icon (.DirIcon, else the .desktop file's
    Icon) and the .desktop keys;
  - the launch environment for the global menu, the default program of a MIME type
    (mimeapps.list), detached children.
- `assign`, `wbrun` (EFL: Efreet, Eldbus) and `appimage-info` (GPL-3.0-or-later).
- `setup-env.sh` and the `Startup-Sequence` template.
- Tests: the assigns, the `assign` command, AppImages packed with every compressor and as
  ISO, broken images.
