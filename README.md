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
