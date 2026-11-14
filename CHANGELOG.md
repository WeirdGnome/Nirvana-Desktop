# Changelog

## 0.3.1 – 2026-11-14

- **The installer builds a Debian package** (`wanderer-x11_VERSION_ARCH.deb`, with its
  dependencies worked out from the libraries the programs use) and installs it with apt.
- Before installing, the installer removes the files of an installation that is not from
  the package (an older `install.sh` or `cmake --install`). Only files that no package
  owns are removed.
- The programs are installed into `/usr/bin`.
- New installer options:
  - `--package-only` builds only the package;
  - `--uninstall` removes the package and any files installed without it.

## 0.3.0 – 2026-11-14

- **Trash** (the freedesktop.org trash, shared with other programs):
  - the trash can on the root window;
  - icons dropped on the trash are moved to it;
  - *Icon > Delete…* asks whether to move to the trash or delete for good;
  - *Icon > Empty Trash*.
- **Leave Out / Put Away**: icons of drawers placed on the root window (and taken back
  off it); the list is kept in `~/.config/wanderer/leftout`.
- **Find** (*Window > Find…*, Super+F): finds the files and drawers whose names match a
  pattern (`*` and `?`, case-insensitive) under the drawer, and shows the results in a
  window of their own.
- *Icon > Disk Info* is available only for volumes.

## 0.2.0 – 2026-11-14

- **Icons**: original images for drawers, hard disks, removable, optical and RAM disks,
  the trash, and for files by type (text, document, picture, sound, movie, archive, script,
  tool, link), with a darker selected image.
- **Wanderer preferences** (*Wanderer > Preferences…*), with Save, Use and Cancel:
  - label text (outline, plain or shadow);
  - number of label lines and line length;
  - grid or free layout and icon spacing;
  - tool bar and status bar of drawer windows;
  - all files;
  - drawers first.

  The settings are kept in `~/.config/wanderer/wanderer.prefs`.
- **Zune**: the Cycle and Checkmark classes.
- The backdrop has no scrollers.

## 0.1.0 – 2026-11-14

The first version of the port of Wanderer (the AROS desktop) to X11. It starts from
scratch; the earlier Nirvana Desktop is on the `nirvana-legacy` branch.

- **Zune on FLTK**:
  - the pens and the default palette;
  - all built-in frames;
  - the layout of groups (minimum, default and maximum sizes, weights, columns);
  - Text, Button, String, Prop, ImageButton and Window.
- **IconList**:
  - icons with outlined labels, split into lines and shortened with " ..";
  - grid or free layout, in rows or columns;
  - sorting by name, date, size or type, with the size or date shown under the label;
  - list view;
  - selection by click, Shift-click and lasso;
  - moving icons and dropping them onto drawers.
- **Drawer windows**:
  - the location bar with the parent and search gadgets;
  - scrollers;
  - the status bar.
- **Wanderer**:
  - the volumes as a window or as the backdrop;
  - one window per drawer;
  - the Wanderer, Window and Icon menus;
  - snapshots of windows and icons;
  - moving within a volume and copying across volumes.
- **Tools**: Execute Command, New Drawer, Rename, Information, Disk Info.
- **wanderer-wm**, a new window manager:
  - screens dragged down by their title bar, with Esc or the right button putting
    them back;
  - screen depth gadget, Super+M and Super+N;
  - window frames with close, zoom, depth and size gadgets;
  - menus in the screen's title bar on the right mouse button.
- **The session**: `wanderer-session`, an entry for the display manager
  (`/usr/share/xsessions/wanderer.desktop`) and an installer (`install.sh`).
