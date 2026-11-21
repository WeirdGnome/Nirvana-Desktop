<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Nirvana and the Ambient desktop: what is the same, what is to come

Nirvana Desktop is meant to behave almost exactly as the Ambient desktop does, built from
Linux parts (EFL, Wayland and labwc, D-Bus, UDisks2, the XDG specifications). The
reference is Ambient's source, <https://github.com/zapek/Ambient> (GPL-2.0-or-later):
its menus (`menus.c`), context menus (`contextmenu.c`, `docs/contexmenus.doc`), windows
(`*winclass.c`), views (`viewclass.c`, `iconviewclass.c`, `listviewclass.c`) and
catalog (`locale/ambient.cd`, with a Polish translation). Nirvana's code is its own; the
names of the operating system Ambient was written for are not used.

States: **done**, **part** (there, not complete), **to do**.

## The right mouse button

| Where | Ambient | Nirvana |
|---|---|---|
| empty desktop, the bar | the screen's menus (held, released over an item) | **done** |
| an icon (desktop, window) | the icon's context menu at the pointer | **done** |
| a window's background | the view's context menu | **done** |
| a panel (dock) item | the panel item's menu | **part** (the dock's own menu) |

## The screen's menus (`menus.c`)

| Menu | Items | Nirvana |
|---|---|---|
| (the desktop's name) | My Computer..., —, Execute Command... (E), New Shell... (N), —, About... (?), About the System..., —, Quit (Q) | **done** (My Computer: phase D) |
| Edit | Cut (X), Copy (C), Paste (V), —, Select All (A), Invert | **done** |
| View | New Drawer..., —, List Mode (L), —, Sort By > Name (.), Type, Size, Date | **done** |
| Icons | Information... (I), —, Put Away, Eject (J), —, Rename... (R), Delete (Del), —, Move to Trashcan (T), Restore (Y), Empty Trashcan, —, Format... | **part** (Put Away, Format: to do) |
| Utilities | Exchange... (H), System Monitor..., —, System Log..., —, Format Disk..., Find... (F) | **part** (there, disabled: phases C, D, H) |
| Settings | Desktop..., Toolkit..., —, System... | **part** (Theme, Assigns; Desktop...: phase F) |
| Tools | the user's own items | **done** (the programs in `Tools:`) |

Nirvana's Window menu (Close, Iconify, Zoom, Depth) was not Ambient's and is gone; those
stay on the windows' gadgets.

## Context menus (`contextmenu.c`)

* An icon: Open / Open in New Window... / Open Shell... / Find... / Copy To... /
  Move To... (the file type's actions), —, Information..., —, Snapshot, UnSnapshot, —,
  Sort By >, —, New Drawer..., —, Cut, Copy, Paste Into, —, Rename..., Delete,
  Format..., Eject or Unmount, Put Away, Move to Trashcan, Restore, Empty Trashcan.
  Each item only where it applies (a file, a drawer, a volume, a removable volume, the
  trashcan, something in it).
* Several icons: Information..., New Drawer..., Snapshot/UnSnapshot, Sort By >, Cut,
  Copy, Rename..., Delete, Put Away, Trash, Restore, Empty.
* A window's background: View > (Icons, List...), Show > (modes), —, Snapshot >
  (Icons, Window, All), UnSnapshot >, —, Sort By >, —, Select All, Invert, —,
  New Drawer..., —, Cut, Copy, Paste, Paste As....
* A list's column titles: the columns shown (Name, Size, Date, Attrs, Comment, Filetype,
  Version, Date & Time, User, Group), Get Directory Sizes, Reset to Default Format.

## Windows

| Window | Ambient | Nirvana |
|---|---|---|
| Drawer (view) | icons or list (columns), spatial or browsing, a toolbar (parent, history, views, bookmarks, actions), a clickable path, a search bar, a status bar with free space, image and text views | **part** (icons/list, browsing, location, filter) |
| Information | name, type, size, dates, protection, comment, the icon, tool types, the default tool | **done** (no tool types: Linux files have none) |
| Execute Command | a command line with history | **part** |
| Rename, New Drawer, Pattern Rename | small requesters | **part** (Pattern Rename: to do) |
| Copy/Move/Delete | with progress, ask before replacing | **done** |
| Find | patterns, contents, results list | **to do** |
| Format | a volume's file system, name | **to do** (UDisks2 Format) |
| My Computer | volumes and assigns | **to do** |
| About, About the System | | **part** |
| System Monitor, System Log | | **to do** |
| Exchange (commodities) | | **to do** (the session's autostarted programs) |
| Settings | pages: Background, Bookmarks, Shell launch, Drag & Drop, Icon display, Keyboard, List, Lister, File types, Miscellaneous, Panels, Window, Advanced | **to do** |

## Other parts

| Part | Nirvana |
|---|---|
| Removable volumes (Eject/Unmount) | **done** (UDisks2) |
| File notification (windows follow changes) | **done** (inotify) |
| Trashcan | **done** (the XDG trash, an icon on the desktop) |
| The file clipboard (cut, copy, paste) | **done** (inside Nirvana; the paths also as text) |
| File types and their actions (Open with...) | **part** (the default program) |
| Panels: several, sub-panels, bookmarks, a directory panel | **part** (one dock with sub-docks and dockies) |
| Bookmarks | **to do** |
| Thumbnails | **to do** |
| Snapshot of windows (place, size, view) | **to do** |

## Phases

* **A** (done) - the menus as Ambient's: the screen's menus, the context menus of icons and of
  windows' backgrounds, held or clicked.
* **B** (done) - files: copy, move, delete with progress, rename, new drawer, the trashcan, the
  file clipboard, Put Away, the Information window.
* **C** - drawer windows: toolbar, clickable path, history, status bar with free space,
  list columns, image and text views, search bar, Find.
* **D** - My Computer, Execute Command with history, Format, About the System.
* **E** - panels and bookmarks as Ambient's.
* **F** - the Settings window and its pages.
* **G** - file types and actions, thumbnails.
* **H** - System Monitor, System Log, Exchange.
