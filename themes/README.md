# Decoration themes

The window manager (rambler-wm) draws the window frames, the screen bars and their gadgets
from a theme in this drawer, the way AROS's decoration does (decortheme.library,
decorator.library and the decorator classes). A theme is a drawer with:

    System/Config     the settings (the keys and values of AROS's themes)
    System/<Gadget>/Default   the pictures (PNG): Close, Depth, Zoom, Size, SDepth,
                      Titlebar, STitlebar, Borders, Container, Knob, Arrow...
    Menu/Config       the menus' settings
    Menu/...          the menus' pictures

Names are found without regard to case, as on the original system.

The theme in use is named in `theme.var`: `$XDG_RUNTIME_DIR/rambler/theme.var` (in use,
ENV:), else `~/.config/rambler/theme.var` (saved, ENVARC:). It holds `THEMES:<Name>`
(looked for in `~/.local/share/rambler/themes` and then in the installation's
`share/rambler/themes`) or the path of a theme's drawer; `none` gives the default look
without a theme. Nothing set: `THEMES:Ice`.

## Ice

The default theme of AROS, by Damir Sijakovic, from the AROS source tree
(`images/Themes/Ice`, AROS commit 10ff84d828ff9a074b7d74fb753e3bd731a421a5). It is part of
AROS and distributed under the AROS Public License 1.1 (AROS's `LEGAL` file lists no other
licence for it). Changes: the menus' command key picture is in `menu/CommandKey` (the
window manager takes the drawer in `menu` whose name ends in "Key"); the icon files
(`.info`) and the colour maps (`.omap`, `.amap`) that came with a picture are left out.
