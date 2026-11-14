# Wanderer for X11 (Nirvana Desktop)

A port of **Wanderer**, the desktop and file manager of the AROS Research Operating
System, to Linux and X11. It runs as an ordinary X11 program on top of FLTK.

This repository starts the port from scratch. The earlier Nirvana Desktop code (FLTK
desktop, GPL-3.0) is kept on the `nirvana-legacy` branch.

Parts of the port:

* **Zune on FLTK** (`wanderer/zune`) – the AROS toolkit classes Wanderer is built on;
* **Wanderer** (`wanderer`) – the desktop, drawer windows, icons, menus and tools;
* **window manager** (`wm`, later) – what Intuition does on AROS: window frames and
  gadgets, screens, screen dragging.

Version: **0.6.0**

## Installing

```sh
tar xzf wanderer-x11-0.6.0.tar.gz
cd wanderer-x11
./install.sh
```

The installer:

1. installs the packages needed for building (apt);
2. builds the programs (FLTK 1.4 is downloaded and built in if the system has none);
3. **makes a Debian package** `wanderer-x11_VERSION_ARCH.deb`;
4. removes the files of an earlier installation that is not from the package (e.g. an
   older `install.sh` into `/usr/local`) - only files that no package owns;
5. installs the package with apt.

The package puts the programs into `/usr/bin` and the session into
`/usr/share/xsessions/wanderer.desktop`, so **Wanderer** appears among the sessions of
the display manager (LightDM, SDDM, GDM, XDM …).

Options:

* `--package-only` - only builds the `.deb`;
* `--no-deps` - does not install the build dependencies;
* `--uninstall` - removes the package and any files installed without it (or
  `sudo apt remove wanderer-x11`).

The session (`wanderer-session`) starts the window manager `wanderer-wm`, then
`wanderer --backdrop`. Choosing *Wanderer > Quit…* ends the session. The number of
screens is set in `~/.config/wanderer/session.conf`, e.g. `WANDERER_SCREENS=3`.

To try it in a running X session without another window manager:

```sh
wanderer-wm & wanderer --backdrop
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

## Licence

Wanderer and Zune (AROS's toolkit compatible with the classic object-oriented GUI
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
| `zune/pens.*` | `muimaster` pens (`MPEN_*`) and the default screen palette |
| `zune/frame.*` | `muimaster/frame.c` – the built-in frames |
| `zune/area.*`, `zune/group.*` … | the Zune classes Wanderer uses (Area, Group, Text, Button, String, Window, Application, Menustrip) |
| `iconlist.*`, `icon.*` … | `Wanderer/Classes` |
| `wanderer.*`, `iconwindow*` … | `Wanderer/*.c` |
| `tools/*` | `Wanderer/Tools` (Execute Command, Info, Rename, New Drawer, Disk Info) |

## Naming

The user interface uses no trademarks of third parties. Identifiers that name such
trademarks in the AROS sources are given neutral names in the port. The old names are
noted in comments where that helps comparison with the AROS sources.
