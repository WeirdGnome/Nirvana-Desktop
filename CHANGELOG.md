<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Changelog

*[Polski](CHANGELOG.pl.md)*

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
