<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Nirvana Desktop themes

A theme is one Edje file (`NAME.edj`, compiled from `NAME.edc` with the images of
`images/NAME/`) in `Themes:` (`~/.nirvana/Themes`, `/opt/nirvana/Themes`). The desktop
uses the one named in `~/.nirvana/Prefs/desktop.conf` (`theme=NAME`), else `ambient`;
`nirvana-desktop --theme NAME` overrides both.

The images of Ambient are drawn by `tools/make-theme-images.py` (Pillow); run it after a
change and commit the PNGs.

## Data items

| Key | Meaning |
|---|---|
| `nirvana/theme/name` | the name shown to users |
| `nirvana/screenbar/height` | the bar's height in pixels (it keeps this strip of the screen) |
| `nirvana/frame/gadgets/left`, `.../right` | the gadgets on each side of a title bar, in order: `close prefs jump iconify zoom depth` |
| `nirvana/widgets/palette` | the Elementary palette for the widgets (`light`, `default`...) |

## Groups

| Group | Parts | Signals in | Signals out |
|---|---|---|---|
| `nirvana/screenbar` | `nirvana.text.title`, `nirvana.swallow.menus`, `nirvana.swallow.status`, `nirvana.text.clock` | | `nirvana,gadget,depth` |
| `nirvana/screenbar/menutitle` | `nirvana.text` | `nirvana,state,selected` / `unselected` | |
| `nirvana/menu/bg` | `nirvana.swallow.content` | | |
| `nirvana/menu/item` | `nirvana.text.label`, `nirvana.text.shortcut` | `nirvana,state,selected` / `unselected` / `disabled` / `checked` / `submenu` | |
| `nirvana/menu/separator` | | | |
| `nirvana/backdrop` | `nirvana.swallow.wallpaper`, `nirvana.text.version` | | |
| `nirvana/icon/volume`, `.../volume/system`, `.../volume/home`, `.../volume/ram`, `.../volume/removable`, `nirvana/icon/drawer`, `.../file`, `.../program` | `nirvana.text.label`, `nirvana.swallow.image` | `nirvana,state,selected` / `unselected` | |
| `nirvana/lister/icon/drawer`, `.../file`, `.../program` | | | |
| `nirvana/frame` | `nirvana.text.title`, `nirvana.swallow.content`, `nirvana.swallow.gadgets.left`, `nirvana.swallow.gadgets.right` | `nirvana,state,active` / `inactive` | `nirvana,frame,move`, `nirvana,frame,resize`, `nirvana,gadget,zoom` |
| `nirvana/frame/gadget/NAME` | | `nirvana,state,active` / `inactive` | `nirvana,gadget,NAME` |

The icons are 88 × 92 (a 64 × 64 picture and a label); the gadgets 18 × 18.
