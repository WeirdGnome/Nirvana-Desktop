# Rambler for X11 (Nirvana Desktop)

**Rambler** is a port of Wanderer, the desktop and file manager of the AROS Research
Operating System, to Linux and X11. It runs as an ordinary X11 program on top of FLTK.
**Lune** is its port of Zune, the AROS toolkit. The new names keep this port apart from
the AROS originals; files and comments that come from AROS still name them.

This repository starts the port from scratch. The earlier Nirvana Desktop code (FLTK
desktop, GPL-3.0) is kept on the `nirvana-legacy` branch.

Parts of the port:

* **Lune on FLTK** (`rambler/lune`) – the toolkit classes (ported from Zune) Rambler is built on;
* **Rambler** (`rambler`) – the desktop, drawer windows, icons, menus and tools;
* **window manager** (`wm`, later) – what Intuition does on AROS: window frames and
  gadgets, screens, screen dragging.

Version: **0.16.0**

## Installing

```sh
tar xzf rambler-x11-0.16.0.tar.gz
cd rambler-x11
./install.sh
```

The installer:

1. installs the packages needed for building (apt);
2. builds the programs (FLTK 1.4 is downloaded and built in if the system has none);
3. **makes a Debian package** `rambler-x11_VERSION_ARCH.deb`;
4. removes the files of an earlier installation that is not from the package (e.g. an
   older `install.sh` into `/usr/local`) - only files that no package owns;
5. installs the package with apt.

The package puts the programs into `/usr/bin` and the session into
`/usr/share/xsessions/rambler.desktop`, so **Rambler** appears among the sessions of
the display manager (LightDM, SDDM, GDM, XDM …).

The package is made for the system it is built on (its libraries): build it on each
system with `./install.sh` rather than installing a `.deb` made elsewhere. If Rambler
cannot start, the session says why instead of logging out at once; its log is
`~/.cache/rambler/session.log`.

Options:

* `--package-only` - only builds the `.deb`;
* `--no-deps` - does not install the build dependencies;
* `--uninstall` - removes the package and any files installed without it (or
  `sudo apt remove rambler-x11`).

The session (`rambler-session`) starts the window manager `rambler-wm`, then
`rambler --backdrop`. Choosing *Rambler > Quit…* ends the session. The number of
screens is set in `~/.config/rambler/session.conf`, e.g. `RAMBLER_SCREENS=3`.

To try it in a running X session without another window manager:

```sh
rambler-wm & rambler --backdrop
```

## Using it

* **Menus**: hold the right mouse button over a window. The screen's title bar shows the
  window's menus, and releasing the button over an item picks it. Shortcuts:
  Super+key.
* **Screens**: drag a screen down by its title bar to uncover the screens behind it.
  Esc or the right button during the drag puts it back. The gadget at the right end of
  the bar sends a screen to the back. Super+M brings the back screen to the front and
  Super+N sends the front screen to the back.
* **Windows**: close gadget on the left, zoom and depth gadgets on the right, size gadget
  in the corner. A click activates a window without raising it. The depth gadget raises
  it, or lowers it if it is already in front.
* **Icons**: a double click opens; Shift adds to the selection; a lasso selects in the
  empty space. Dragging icons to another window moves them within a volume and copies
  them across volumes.
* **Leave Out**: icons dragged from a drawer onto the empty background of the root window
  are left out there; *Icon > Put Away* takes them back.
* **Icon files**: a file's own icon is `name.info` beside it (classic, NewIcons, colour
  and PNG icons). A dual icon has a second image for its selected state; it goes with its
  file when the file is copied, moved, renamed or deleted.
* **Default icons**: put `def_drawer.info`, `def_tool.info`, `def_project.info`,
  `def_disk.info`, `def_txt.info` ... into `~/.config/rambler/icons` and they stand for
  every drawer, tool, project, disk, text ... without an icon of its own.
* **AppImages** show the icon they carry (taken out with `unsquashfs` from the
  `squashfs-tools` package; the AppImage is not run for it). `rambler --appimage-icon FILE`
  tells, step by step, what becomes of an AppImage's icon. AppImages with a DwarFS image
  instead of squashfs ("anylinux", uruntime) need `dwarfsextract`: `./install.sh
  --with-dwarfs` puts the DwarFS project's program into `/usr/local/bin`. An AppImage put
  into the menus by a desktop tool also gets the icon its `.desktop` file names.
  *Icon > Snapshot* writes it out as the AppImage's own dual icon (`Name.AppImage.info`:
  two PNG images one after the other); paint over the second one for the selected look.

## Preferences

*Tools > Tools Drawer* holds the utilities: Clock (`rambler --utility clock`; analog or
digital, the date in its title, an alarm).

*Tools > Prefs Drawer* opens the preferences programs: Input, Lune (the GUI settings),
Rambler, IControl (the window manager's screens, menus and windows), ScreenMode (the
display mode, with xrandr), Time (the date and the time), Sound (the beep and the sound output), Printer (the CUPS printers and their paper), Locale (languages, country,
timezone), Pointer (the cursor theme and size), Font (the icons', screen and system
fonts) and Network (interfaces, host name, wireless networks; NetworkManager needs to be
running). Each also runs on its own
(`rambler --prefs font|icontrol|input|locale|lune|network|pointer|printer|rambler|screenmode|sound|time`). As on the
original system, **Use** puts the settings into effect for this session only (they are
written to `$XDG_RUNTIME_DIR/rambler`, the counterpart of ENV:) and **Save** also keeps
them (`~/.config/rambler`, the counterpart of ENVARC:). A running Rambler takes up a change
made by a preferences program within a second.

## Programs started with Rambler

At the start of a session Rambler starts everything in `~/.config/rambler/startup`
(programs, scripts, `.desktop` files), then the XDG autostart entries. Two tool types (set in
Information) work as on the original system:

- `STARTPRI=n` (from -128 to 127): the highest is started first; without one, 0. Programs
  with the same priority start in the order of their names.
- `WAIT=n` (seconds, at most 60): a pause after the program before the next one starts.

A program that cannot be started shows a requester, and the rest start after it.

## Programs' items in the Tools menu

A program can add items to Rambler's Tools menu (as AddAppMenuItem does). It sets the
property `_RAMBLER_APPMENU` (type `UTF8_STRING`, the item names separated by NUL) on its
top-level window. When an item is picked, Rambler:

1. puts the paths of the selected icons into the window's property
   `_RAMBLER_APPMENU_ARGS` (separated by NUL);
2. sends the window a ClientMessage `_RAMBLER_APPMENU_PICK`, with `l[0]` the item's
   number and `l[1]` the number of paths.

The items go away when the window closes or the property is deleted.

## Programs' icons on the root window

A program can also put an icon on the root window (as AddAppIcon does): the property
`_RAMBLER_APPICON` (`UTF8_STRING`, one name for each icon, separated by NUL) on its
top-level window. Opening the icon, or dropping icons onto it, sends the window a
ClientMessage `_RAMBLER_APPICON_MESSAGE` (`l[0]` the icon's number, `l[1]` the number of
paths); the dropped icons' paths are in `_RAMBLER_APPICON_ARGS` (separated by NUL).

`l[2]` says why the message was sent (the AMCLASSICON numbers): 0 the icon was opened or
icons were dropped on it; 4 Snapshot, 5 Unsnapshot, 8 Delete, 10 Empty Trash were picked
in the Icon menu while it was selected. Those items are enabled for the icon only when the
program says it takes them: the property `_RAMBLER_APPICON_SUPPORTS` (`CARDINAL`, one for
each icon, in order) with bit *n* set for class *n* (as the WBAPPICONA_Supports tags).
Without it an icon takes Open only.

## Custom frames

GUI Settings > Frames takes up to 16 custom frames, each a `frame.config` file with
two pictures beside it: `up/default` and `down/default` (PNG, BMP, GIF or JPEG; the
extension may be left out). The file sets how many pixels of the picture's edges are
corners and edges (`TileLeft`, `TileTop`, `TileRight`, `TileBottom`) and the room the
frame takes on each side (`InnerLeft`, `InnerTop`, `InnerRight`, `InnerBottom`), one
setting a line:

```
TileLeft 4
TileTop 4
TileRight 4
TileBottom 4
InnerLeft 3
InnerTop 3
InnerRight 3
InnerBottom 3
```

The corners are drawn as they are; the edges and the middle are repeated. The frames
then appear as "Custom #1" to "Custom #16" in every frame choice. A frame whose pictures
cannot be read is drawn as a rectangle.

## Languages

The interface follows the preferred languages of the Locale preferences, else the
user's language (`LANGUAGE`, `LC_ALL`, `LC_MESSAGES`, `LANG`).
The texts come from catalogs in `share/rambler/catalogs`, one line per text: the English
text, a TAB, the translation. Polish (`polski.catalog`) is included. A text without a
translation shows in English.

## Licence

Rambler and Lune (AROS's toolkit compatible with the classic object-oriented GUI
toolkit) are distributed by the AROS Development Team under the **AROS Public License,
version 1.1** (APL). The licence is in [`LICENSE`](LICENSE).

All files in this directory are covered by the APL 1.1:

* files ported from AROS keep the AROS copyright notice and say what they were
  ported from;
* the new FLTK code is contributed under the same licence.

The whole repository is under the APL 1.1. The programs link only FLTK (LGPL-2.0 with
the FLTK static linking exception).

Source of the port: <https://github.com/aros-development-team/AROS>,
`workbench/system/Wanderer` and `workbench/libs/muimaster`, as of commit
`2a3000acab4794b4801e0fe115a6d66eb3d1714c`.

## Layout

| Here | Ported from (AROS) |
|---|---|
| `lune/pens.*` | `muimaster` pens (`MPEN_*`) and the default screen palette |
| `lune/frame.*` | `muimaster/frame.c` – the built-in frames |
| `lune/area.*`, `lune/group.*` … | the Lune classes Rambler uses (Area, Group, Text, Button, String, Window, Application, Menustrip) |
| `iconlist.*`, `icon.*` … | `Wanderer/Classes` |
| `rambler.*`, `iconwindow*` … | `Wanderer/*.c` |
| `tools/*` | `Wanderer/Tools` (Execute Command, Info, Rename, New Drawer, Disk Info) |

## Naming

The user interface uses no trademarks of third parties. Identifiers that name such
trademarks in the AROS sources are given neutral names in the port. The old names are
noted in comments where that helps comparison with the AROS sources.
