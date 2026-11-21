<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Nirvana Desktop themes

A theme is one Edje file (`NAME.edj`, compiled from `NAME.edc` with the images of
`images/NAME/`) in `Themes:` (`~/.nirvana/Themes`, `/opt/nirvana/Themes`). The desktop
uses the one named in `~/.nirvana/Prefs/desktop.conf` (`theme=NAME`), else `ambient`;
`nirvana-desktop --theme NAME` overrides both. Settings -> Theme lists the themes found and
writes the choice; the desktop starts again in it.

| Theme | Look |
|---|---|
| `ambient` | the default: soft gradients, rounded gadgets, shadows |
| `classic13` | four colours - blue, white, black, orange - sharp pixels, striped title bars |
| `classic21` | grey with raised edges, blue for what is selected |
| `classic31` | eight colours, raised edges, a blue-grey backdrop |
| `classic41` | smooth greys, gradient title bars, soft shadows |

## Making a theme

The groups are the same for every theme: they are in `common.edc`, which a theme's
`.edc` includes after it has said how it looks:

* the macros `FN`, `FNB` (fonts), `FS`, `FSS` (text sizes), `BAR_H`, `MENU_H`,
  `TITLE_H` (heights), `ICON_TEXT_EFFECT` (`PLAIN`, `SHADOW`, `SOFT_SHADOW`...);
* its data items (below) and its colour classes (below);
* its images, in `images/NAME/`, with the names `common.edc` lists: `grad_*` (stretched:
  the bar, a selected menu title, the title bars, a selected menu row, the backdrop,
  the dock), `icon_*` and `icon_*_sel` (64 × 64), `gadget_*`, `gadget_*_down`,
  `gadget_*_inactive` (18 × 18).

The images are drawn by `tools/make-theme-images.py` (Pillow): a theme there is a palette
and a few choices (smoothing, rounding, bevels, shadows, how a selected icon looks). Run
it after a change and commit the PNGs; the build needs only `edje_cc`. Add the theme's
name to `themes/meson.build`.

A theme of your own can also be any `.edc` giving the groups below, compiled with
`edje_cc` and put in `~/.nirvana/Themes`.

## Colour classes

| Class | Colours |
|---|---|
| `nirvana_bar_text` | the bar's title and clock (color3: their shadow) |
| `nirvana_bar_line` | the line under the bar |
| `nirvana_menu_text`, `_sel`, `_disabled` | menu rows |
| `nirvana_menu_bg`, `_border`, `_shadow`, `_separator` | menu panels |
| `nirvana_backdrop_text` | the version on the backdrop |
| `nirvana_icon_text` (color3: shadow), `nirvana_icon_text_bg`, `nirvana_icon_text_sel_bg` | icon labels |
| `nirvana_frame_border`, `nirvana_frame_bg` | window frames |
| `nirvana_title_text`, `nirvana_title_text_inactive` (color3: shadow) | window titles |
| `nirvana_dock_shadow`, `_border`, `_bg` (tints `grad_dock`), `_hover`, `_pressed`, `_text`, `_label`, `_separator`, `_track`, `_fill` | the dock and its dockies |

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
| `nirvana/dock/bg` | `nirvana.swallow.content` | | |
| `nirvana/dock/item` | `nirvana.swallow.icon` | `nirvana,state,subdock` | `nirvana,dock,activate` |
| `nirvana/dock/separator` | | | |
| `nirvana/dock/docky` | `nirvana.text.value`, `nirvana.text.label`, the drag part `level` (0 at the top: full) | `nirvana,state,nogauge` | |

The icons are 88 × 92 (a 64 × 64 picture and a label); the gadgets 18 × 18.
