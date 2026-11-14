# Changelog

## 0.9.0 – 2026-11-14

- **Network Access**: an icon on the root window opens a window with the mounted network
  file systems (NFS, SMB/CIFS, SSHFS, rclone, WebDAV …) as volumes, with their own volume
  names (`outputs:`). It can be turned off in the preferences.
- **Programs' items in the Tools menu** (AppMenuItems) and **programs' icons on the root
  window** (AppIcons). Programs set an X property on their window and get a message back
  with the paths of the selected or dropped icons (see README).
- **Icons dragged onto another program's window** are passed on by drag and drop (XDND,
  file URIs). The icons go back to their places.
- **About Wanderer** window as in the original: the icon, the version, the copyright, the
  classes. Wanderer carries its own version string, so Information shows its version.
- **Preferences** on register pages: Icons, Drawer Windows, Screen, Background Pictures.
- **Keys** in drawer windows: Page Up and Page Down scroll, Home and End move the focus to
  the first or last icon. A new window's keys go to its icons.
- **The session no longer logs out silently** when Wanderer cannot start (e.g. a package
  built on another system with other libraries). It says why and offers a shell. Its log is
  `~/.cache/wanderer/session.log`.
- **Fixes**
  - Windows and requesters open centred, because the position asked for now reaches the
    window manager.
  - The window manager makes a window its backdrop even when the window gets the desktop
    type after it is shown.
  - The status bar counts volumes as drawers.

## 0.8.0 – 2026-11-14

- **System tray** (AppIcons): programs' tray icons (XEmbed) sit in the main screen's title
  bar, left of the depth gadget.
- **Icon > Format…**: a removable disk gets a new file system (FAT32, exFAT or ext4), with
  a name, Quick Format and an optional trash can. It asks first and works through polkit.
- **Information** works like the Info tool: the icon and the name above a register with
  three pages.
  - *Information*: type, date, the file's **version string** (`$VER:`), size, the path with
    **Open**, the default tool (with a file requester) and the comment.
  - *Protection*: read, write and execute for the owner, the group and the others.
  - *Tool Types*: a list of settings kept with the file (`user.tooltypes`), with New and
    Delete.
- **Zune Register**: page groups with tabs, drawn like the original's.
- **Preferences**, new settings:
  - **Screen title** template: `%wv` version, `%os` system, `%ov` kernel, `%pc` / `%pf` /
    `%pt` available, free and total memory.
  - **Navigation**: *Classic* opens a drawer in a new window, *Enhanced* in the same one.
    The tool bar's parent button and location field always browse in their own window.
  - **Dropped icons**: *Move* (within a volume, else copy) or always *Copy*.
  - **Only on focus**: a label's extra lines show only on the focused icon.
  - **Font** and **Font size** of the icon labels.
  - **Hidden volumes**: names, devices or mount points the root window leaves out.
  - Background pictures can use **fixed tiles**, which stay put while the view scrolls.
- **Fixes**
  - Menus showed the last title everywhere when a text was not in the catalog.
  - Grid groups now centre objects smaller than their cell, so checkmarks keep their size.

## 0.7.0 – 2026-11-14

- **Catalogs**: the interface in the user's language (from `LANG` and the other locale
  variables). Menus, requesters, window titles, labels, choices and the status bar all
  come from the catalog, including messages with names in them. **Polish** is included.
  More languages are added as `share/wanderer/catalogs/<language>.catalog`.
- **wanderer-wm**: the bell (XBell, a mistake typed) **flashes the screen** instead of
  beeping, as the system's screen flash.

## 0.6.6 – 2026-11-14

- **Assigns**: names of your own for drawers, kept in `~/.config/wanderer/assigns`
  (`Docs = ~/Dokumenty`).
  - Usable in every volume path, e.g. `Docs:Listy`.
  - *Wanderer > Assigns…* edits them: a name, Add… (the drawer from the file requester),
    Remove, Save.
- **Execute Command** understands volume paths and assigns (`cat Docs:list.txt`,
  `ls RAM:`). Words that name no volume, like `http://…`, stay as they are.

## 0.6.5 – 2026-11-14

- **Volume paths**: a drawer's location field shows the volume's name and the path inside
  it (`Home:Dokumenty`, `Ram Disk:`, `System:etc`).
  - Typed in, such a path opens its drawer.
  - Volume names: System / SYS, Ram Disk / RAM, Home, Trash and the names of mounted
    drives, in any case.
  - A Linux path or `~` still works.
  - The file requester's Drawer field uses them too, and Information shows where an icon
    is.

## 0.6.4 – 2026-11-14

- **Ram Disk**: a volume in memory, always on the root window.
  - Its files live on the memory file system (`/dev/shm/ramdisk-USER`, readable only by
    its owner).
  - It takes only the memory they need and is empty after a restart.
  - Its window shows how full it is.

## 0.6.3 – 2026-11-14

- **Find by contents**: besides a name pattern, a text the files must contain (no case,
  files up to 64 MB). An empty name with a text finds every file that contains it.
- **List view**: a click on a column's header sorts by Name, Size or Date. A second click
  turns the order round, and the sorted column shows an arrow.

## 0.6.2 – 2026-11-14

- **Drawer windows follow the disk** (inotify): files and drawers that are made,
  removed, renamed or written show a moment later. The selection, the view and the
  window's place are kept.
- **Window titles**: a drawer's window shows the drawer's name. A volume's window shows
  how full it is, e.g. "System  6% full, 26G free, 16G in use".
- *Window > Open Parent* of a volume brings up the root window with the volumes.

## 0.6.1 – 2026-11-14

- **Output window**: what a command started by *Execute Command* prints shows in a window
  of its own, in the fixed font. A command that prints nothing opens none (as the
  system's console window).
- **Wanderer > Shutdown…**: turns the computer off or restarts it (elogind / logind,
  else systemctl or shutdown), and asks again while files are still being copied.
- **Snapshot keeps a drawer's view**: icons or list, all files or not, and the order. The
  drawer opens that way again.

## 0.6.0 – 2026-11-14

- **Copying, moving and deleting in the background** (also moving to the trash):
  - a progress window (item, destination, a gauge in bytes or items, Abort) when the
    work takes longer than a moment;
  - names that exist already are asked about (Replace, Replace All, Skip, Abort);
  - no half-copied files are left behind;
  - Quit asks again while the work is running.
- **Volumes follow the drives**: removable devices that are not mounted show as
  ghosted volumes, and opening one mounts it (udisks). *Icon > Eject* (Super+J)
  unmounts a volume and powers its drive off.
- **Information** can be changed: the protection (read, write, execute for the owner,
  the group and others), the file's comment (kept in `user.comment`) and a project's
  **Default Tool**, the program that opens it.
- **List view**: Protection and Comment columns.
- **Startup drawer** (`~/.config/wanderer/startup`) and XDG autostart, run once per
  session.
- **Tools menu** with the user's tools from `~/.config/wanderer/tools`.
- *Icon > Copy* (Super+C): "Copy of NAME" beside the original.
- **Cursor keys** move between icons (Shift extends the selection, Space toggles,
  Return opens).
- Files dragged in from other programs (XDND) are moved within a volume and copied onto
  another one.
- **wanderer-wm**:
  - **Super + left button drags a screen from anywhere**;
  - the GUI settings' colours apply to the frames and bars at once;
  - full screen (`_NET_WM_STATE_FULLSCREEN`) and maximised windows;
  - no frames for docks, notifications, splash screens, tooltips and windows asking for
    none;
  - resize increments and maximum sizes;
  - `_NET_WORKAREA`, `_NET_FRAME_EXTENTS`, `_NET_CLOSE_WINDOW`.
- **Fixes**:
  - the window manager no longer lets go of windows that were already shown when it
    started;
  - Execute Command remembers the last command.

## 0.5.0 – 2026-11-14

- **GUI settings** (*Wanderer > GUI Settings…*), with Save, Use and Cancel. They are the
  Zune preferences:
  - the screen's colours: background, shine, shadow and text, fill, with the schemes
    Default, Warm, Cool, Green and Contrast;
  - the font (sans, serif, fixed) and its size;
  - the frames of buttons (bevel, window bevel, semi-round, round, thick border);
  - the frames of groups (thin border, bevel, grey border, rounded, round thin border).

  Use and Save change every open window at once. The settings are kept in
  `~/.config/wanderer/zune.prefs`.
- **wanderer-wm** takes the screen's colours from the GUI settings when it starts.
- **Zune**: an object with a standard frame follows the preferences, so changing a
  frame kind changes it everywhere.

## 0.4.1 – 2026-11-14

- **File requester** in the style of the system's file requester:
  - the drawer's list, with drawers marked "Drawer" and files with their size;
  - the Drawer and File fields;
  - OK, Volumes, Parent and Cancel buttons;
  - a double click opens a drawer or picks a file.

  The "?" buttons in *Preferences > Background Pictures* open it.
- **Zune**: the List class (a read list with a scroller, an active line, keys and the mouse
  wheel).

## 0.4.0 – 2026-11-14

- **Background pictures** for the backdrop and for drawer windows (PNG, JPEG, GIF or BMP),
  tiled, scaled or centred. Tiled pictures move with the view. Set them in
  *Wanderer > Preferences… > Background Pictures*.
- **The screen's title**: while a Wanderer window is active, the screen's title bar shows
  the Wanderer version and the free memory, updated every 5 seconds. Other programs
  can set it with the `_WANDERER_SCREEN_TITLE` property.

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
