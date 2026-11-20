<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Nirvana Desktop 3 (NirvanaNG)

*[Polski](README.pl.md)*

The next generation of Nirvana Desktop: a light desktop shell for Devuan GNU/Linux
(x86_64, without systemd), built with the Enlightenment Foundation Libraries on Wayland
(wlr-layer-shell), in the spirit of the classic spatial desktops — a screen bar with a
global menu, a backdrop with volume icons, logical volumes ("assigns"), a dock and file
listers. It is new code, written from scratch; it shares nothing with the `main` branch
(Rambler) and emulates no other system's libraries.

This branch is built in stages:

| Stage | What | State |
|---|---|---|
| 1 | `libnirvana` (assigns, AppImage reader), `assign`, `wbrun`, `appimage-info`, tests | **done** |
| 2 | The EFL desktop: screen bar "Nirvana Screen", backdrop, window gadgets, Assigns editor | **done** |
| 3 | The global menu (`com.canonical.AppMenu.Registrar`, Eldbus) | next |
| 4 | Dock and file listers (threaded) | |
| 5 | The Edje themes: Classic13, Classic21, Classic31, Classic41, Ambient | |

## libnirvana (LGPL-3.0-or-later)

**Assigns** — logical volume names standing for one or more directories
(`include/nirvana.h`). The session's assigns live in
`$XDG_RUNTIME_DIR/nirvana-ram/assigns.db` (tmpfs), one per line:

```
NAME:|DESKTOP|PATH_1;PATH_2;...
```

`nirvana_assign_init()` writes the defaults at the session's start: `SYS:` (/),
`Home:`, `RAM:`, `Nirvana:` (~/.nirvana;/opt/nirvana), `Themes:`, `Tools:`, the XDG user
directories as `Downloads:`, `Docs:`, `Pics:`, `Music:`, `Videos:`, and an assign for
each visible directory at the top of `$HOME`. Multi-assigns are searched left to right;
`nirvana_to_posix()` and `posix_to_nirvana()` translate both ways (the longest prefix
wins). Changes are atomic (a lock, a temporary file, a rename). User assigns can be kept
in `~/.nirvana/Startup-Sequence`.

**AppImages** — icon and `.desktop` metadata read without running anything
(`include/nirvana_appimage.h`): the file is mapped read-only, the payload found after
the ELF runtime (`e_shoff + e_shnum * e_shentsize`), and read as the file system it is:

* SquashFS 4.0 — libnirvana's own reader (gzip, lzma, xz, lz4, zstd; links followed,
  fragments, sparse blocks); every offset taken from the image is bounds-checked;
* ISO 9660 (type-1 images) — through libarchive;
* DwarFS — through libdwarfs (0.15), which `install.sh` downloads, checks and builds
  (it is not packaged by Devuan or Debian); the bridge is C++23, as libdwarfs' headers
  are. Built by hand: `-Ddwarfs=enabled -Dcmake_prefix_path=<dir of dwarfs-config.cmake>`.

## nirvana-desktop (GPL-3.0-or-later)

```
nirvana-session                      a Wayland session: labwc with the desktop
nirvana-session --x11                on X11, beside a window manager (from ~/.xinitrc)
nirvana-desktop [--theme NAME] [--wayland | --x11]
```

* **The screen bar** "Nirvana Screen": a layer surface (wlr-layer-shell, top layer) that
  keeps its strip of the screen; on X11 a dock window with a strut. Menu titles
  (Nirvana Screen, Window, Icons, Tools), the clock, the depth gadget. The menus drop
  down on a click (or held with the right button: releasing over an item chooses it),
  follow the keyboard (arrows, Return, Escape, underlined letters) and have Super
  shortcuts. While a menu is open on Wayland the bar's surface grows over the screen.
* **The backdrop**: a layer surface of the background layer with a volume icon for each
  assign with `DESKTOP=1`; select, drag, rubber band, double click to open, Snapshot and
  Clean Up (Icons menu). The icons follow the assigns (SIGUSR1 from `assign`).
* **Window gadgets** on the desktop's own windows: close at the left; prefs, jump,
  iconify, zoom and depth at the right; a resize gadget; moving and resizing go through
  the display server, so they work on Wayland too.
* **The Assigns editor** (Nirvana Screen -> Assigns...): the assigns with New, Clone,
  Delete; a name, "Show volume icon on Desktop", the paths in search order with Add Path
  (file selector), Remove Path, Move Up/Down, Open in Lister; Use, Save (also to
  `~/.nirvana/Startup-Sequence`), Cancel.
* **The lister** (a first version; stage 4 makes it complete): a drawer as icons, read in
  a thread, AppImage icons read without running them, `#?` patterns in the filter.
* D-Bus: `org.nirvana.Desktop` on the session bus (`OpenDrawer`, `ReloadAssigns`,
  `ShowAssignsEditor`) - `wbrun` opens drawers through it.

Programs' own windows keep the frames their compositor (or window manager) gives them;
the gadgets are drawn on Nirvana's windows. Some Super shortcuts may be taken by the
compositor or window manager first.

The depth gadget lowers its window on X11 and, on Wayland, under labwc: Wayland gives a
program no request to lower a window, so Nirvana's labwc settings bind the "Task" mouse
button (which mice do not have) over a window to labwc's Lower, and the gadget clicks it
on a virtual pointer. With your own `~/.config/labwc/rc.xml`, add to its `<mouse>`:
`<context name="Client"><mousebind button="Task" action="Press"><action name="Lower"/></mousebind></context>`.
On other compositors the gadget brings the next of Nirvana's windows forward instead.

## Commands (GPL-3.0-or-later)

```
assign                               list the assigns
assign Work: /srv/work;~/work --desktop
assign Work: /mnt/more --add
assign Work: REMOVE --save
wbrun Downloads:                     open a drawer (in the desktop's lister)
wbrun Tools:org.example.App.desktop  start a program
wbrun Docs:letter.odt                open a file with its default program
appimage-info Some.AppImage [--icon out.png | --desktop | --extract PATH OUT]
```

## Building

The short way: `./install.sh` (`./install.sh --deps` installs the packages first,
`./install.sh --uninstall` removes it again). By hand:

```
sudo apt install meson ninja-build pkg-config libarchive-dev zlib1g-dev liblzma-dev \
                 libzstd-dev liblz4-dev libefl-all-dev libwayland-dev wayland-protocols \
                 squashfs-tools labwc
meson setup build
ninja -C build
meson test -C build
sudo ninja -C build install        # to /opt/nirvana (C/, Libs/)
sudo ./setup-env.sh --system       # /opt/nirvana/{C,Libs,Themes,S}, /nirvana
./setup-env.sh --user              # ~/.nirvana, Startup-Sequence, GTK settings
```

`wbrun` and `nirvana-desktop` need EFL (Elementary, Efreet, Eldbus, Ecore-X,
Ecore-Wl2) and `edje_cc`; without them libnirvana and `assign` still build. The tests need
`mksquashfs` (and `bsdtar` for the ISO test).

## Known limits

* **DwarFS**: libdwarfs is not packaged for Devuan or Debian; `install.sh` builds it
  (once, a few minutes up to half an hour). Without it (`--no-dwarfs`) DwarFS AppImages
  report "not supported".
* **The global menu on Wayland**: `com.canonical.AppMenu.Registrar` identifies windows
  by their X11 window ids. Under Wayland it works for X11 programs (through XWayland);
  native Wayland programs export no such ids, and wlroots compositors have no common
  protocol for it yet, so their menus stay in their windows. Stage 3 will say more.
* LZO-compressed SquashFS images are not read.

## Licences

* `libnirvana` (`src/lib/`, `include/`): LGPL-3.0-or-later (`COPYING.LESSER`).
* Everything else — commands, desktop, themes, scripts: GPL-3.0-or-later (`COPYING`).

Each file carries an SPDX line. The Wayland protocol files in `protocols/` keep their
authors' (MIT) licences.
