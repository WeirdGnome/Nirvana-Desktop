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
| 2 | The EFL desktop: screen bar "Nirvana Screen", backdrop, window gadgets, Assigns editor | next |
| 3 | The global menu (`com.canonical.AppMenu.Registrar`, Eldbus) | |
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
* DwarFS — through libdwarfs, when it is available at build time (`-Ddwarfs=enabled`).

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

```
sudo apt install meson ninja-build pkg-config libarchive-dev zlib1g-dev liblzma-dev \
                 libzstd-dev liblz4-dev libefl-all-dev squashfs-tools
meson setup build
ninja -C build
meson test -C build
sudo ninja -C build install        # to /opt/nirvana (C/, Libs/)
sudo ./setup-env.sh --system       # /opt/nirvana/{C,Libs,Themes,S}, /nirvana
./setup-env.sh --user              # ~/.nirvana, Startup-Sequence, GTK settings
```

`wbrun` needs EFL (Ecore, Efreet, Eldbus); without it the rest builds. The tests need
`mksquashfs` (and `bsdtar` for the ISO test).

## Known limits

* **DwarFS**: libdwarfs is not packaged for Devuan or Debian; DwarFS AppImages are read
  only when libdwarfs (0.12 or later, with its CMake package) is installed and found at
  build time. Without it they report "not supported".
* **The global menu on Wayland**: `com.canonical.AppMenu.Registrar` identifies windows
  by their X11 window ids. Under Wayland it works for X11 programs (through XWayland);
  native Wayland programs export no such ids, and wlroots compositors have no common
  protocol for it yet, so their menus stay in their windows. Stage 3 will say more.
* LZO-compressed SquashFS images are not read.

## Licences

* `libnirvana` (`src/lib/`, `include/`): LGPL-3.0-or-later (`COPYING.LESSER`).
* Everything else — commands, desktop, themes, scripts: GPL-3.0-or-later (`COPYING`).

Each file carries an SPDX line.
