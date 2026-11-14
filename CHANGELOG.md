# Changelog

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
