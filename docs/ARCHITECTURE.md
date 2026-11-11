# Nirvana Desktop: architecture (draft)

## Layers

    Devuan (runit, elogind, polkit)  ->  X11 (Xorg/XLibre)  ->  nirvana-insight (WM)
                                                              ->  nirvana-gary (menu bar)
                                                              ->  nirvana-denise (files/desktop)
                                                              ->  AppImage applications

All components link `nirvana-platinum`, so there is one look.
FLTK 1.4 has a Wayland backend, so a possible move to Wayland means replacing
only `insight` (a wlroots compositor) and keeping the rest.

## Windows: who is responsible for what

- **Insight** (the window manager) draws the Platinum frames, moves, iconifies, zooms
  and closes all windows, also Rambler's. It publishes the active window
  in `_NET_ACTIVE_WINDOW`, and iconified windows in `_NIRVANA_ICONS`.
- **Rambler** shows the desktop (a single DESKTOP window with the global menu bar)
  and every folder and dialog as a **separate X window**. The global menu bar holds
  the Nirvana, System and Special menus; every folder window has its own Window and
  Icons menus and a navigation bar. Clicking the menu bar does not change the active window.
- Window positions are stored as the position of the content (without the frame). FLTK sets
  `StaticGravity`, and Insight places the frame so that the content returns to the same place.
- Windows of class `Nirvana-*` animate opening and closing themselves (from the icon to the window),
  so Insight skips its own animation for them. `Nirvana-Dialog` gets a dialog
  frame: no buttons and no resizing.
- **Under another window manager** (GNOME, KDE, Openbox) or without one, Rambler
  draws the Platinum frames itself (CSD, like GTK applications): the windows have no foreign
  decorations, moving by the title bar and resizing are handed to the manager through
  `_NET_WM_MOVERESIZE` (only after the mouse moves, so that a double click shades the window),
  and shading and zoom are done by Rambler. Detection: `_NET_SUPPORTING_WM_CHECK` → the name "insight".
  Forcing for tests: `NIRVANA_CSD=1` or `0`.
- FLTK maps the desktop window right away in `show()`, before it can be given the DESKTOP type.
  So Rambler withdraws it as ICCCM describes, waits for the window manager's confirmation
  and shows it again as a desktop, so that it lies below the folder windows.

## Global menu bar (`gary`)

- Our own applications would publish their menus through a simple protocol (a proposal: a unix
  socket `$XDG_RUNTIME_DIR/nirvana/gary`, text messages with the menu tree).
- Foreign applications (GTK/Qt in an AppImage) keep their menus in the window; `gary` shows
  only the logo menu and the application switcher for them.

## Appearance and color schemes

The libplatinum colors (`plat::GRAY`, `TEXT`, `CONTENT`, `SEL`, `LIGHT`, `SHADOW`…) are
**FLTK palette indexes**, not RGB values. Four of them are the standard FLTK colors
(`FL_BACKGROUND_COLOR`, `FL_FOREGROUND_COLOR`, `FL_BACKGROUND2_COLOR`,
`FL_SELECTION_COLOR`), the rest take the free indexes `FL_FREE_COLOR`..`+15`.
A widget that once got `color(plat::GRAY)` has the new color after a scheme change by itself:
`Fl::set_color()` and a redraw are enough.

The course of a change:
1. `nirvana-alice` saves `~/.config/nirvana/appearance.conf`.
2. It sets the `_NIRVANA_APPEARANCE` property on the X root window.
3. Rambler and Insight get a `PropertyNotify`, call `plat::reload_appearance()`
   and redraw their windows (Rambler also rebuilds the desktop pattern, Insight the root window background).

The bevel shades (`LIGHT`, `MID`, `SHADOW`) and the title bar stripes are computed from the window color,
unless the scheme gives them explicitly (`light`, `mid`, `shadow`).

## Permission model

- Rambler does not open anything outside `$HOME` and the mounted disks (checked with
  `weakly_canonical`, so symbolic links do not lead outside Nirvana HD).
- AppImages are extracted at installation (`unsquashfs` with the offset from the ELF
  header; the AppImage runtime is never executed) and run through
  `bubblewrap`: writing only in `$HOME`, the system read-only, hidden `/home`
  of others, `/root`, `/mnt`, `/media`, our own `/tmp` and a PID namespace.
  Shared: the X11 socket, `XAUTHORITY`, PulseAudio/PipeWire and the D-Bus session bus.
  Note: access to the X server means an application can spy on other windows -
  a limitation of X11 (to consider: a separate Xephyr server or the XACE extension).

## Naming

- Versions: Mac OS 8/9 code names: 1.0 "Tempo", 2.0 "Allegro", 3.0 "Sonata".
- Components: the window manager Insight; the others are named after Amiga chips (Denise, Paula, Gary, Alice).
