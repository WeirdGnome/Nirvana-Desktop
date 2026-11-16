# Changelog

## 0.20.0 (unreleased)

### Added
- Appearance: "Use theme configuration for.. Lune" takes the GUI settings a theme brings
  (the original's `Zune/global.prefs`: the window, group, button, list and other
  backgrounds - gradients included -, the frames, the spacings, the fonts, the strings'
  pens, the keys, the cycles' and the lists' settings); eight of the nine themes bring
  them. Lune reads the original's settings file (`lune::import_zune_prefs`).
- Every Lune program (the Tools, the preferences programs run on their own) takes up a
  change of the GUI settings in use at once, as Rambler does (IPrefs); its windows grow
  when the new fonts or frames need more room.

## 0.19.0 – 2026-11-16

### Added
- **Decoration themes** in the window manager, ported from AROS (decortheme.library,
  decorator.library, the window and screen decorator classes): the window frames (title
  bar from the theme's picture in its parts, borders filled with a gradient or a picture
  and shaded, the close, zoom, depth and size gadgets from their pictures in their states)
  and the screen bars (the bar's picture, the screen depth gadget) are drawn from a theme
  in the AROS format, with the same settings (`System/Config`, `Menu/Config`) and sizes
  (the title bar grows with the font, as on AROS), and the window shapes of the themes with
  rounded title bars (BarMasking, BarRounded). **All nine themes of the AROS source tree**
  come with it: Ice (AROS's default, used unless `theme.var` names another theme, or
  `none`, the look so far), AROSDefault, DarkEnder, Darkness, Xstream, Retro1, Retro3,
  Retro4 and Semaphore (the last four renamed). See `themes/README.md`.
- **Appearance preferences** (`rambler --prefs appearance`, in the Prefs drawer), ported
  from AROS: theming on or off, the theme (the drawers of THEMES:) with a preview drawn
  from it and its description, and "Use theme configuration for.." the palette and the
  fonts a theme brings (Lune, Rambler and pointers to follow); the compositing settings
  shown as the original has them (on X the window manager composes the screens). Save
  writes `theme.var`; the window manager takes the new theme up at once - the windows get
  their new frames where they are (the original needs a restart).
- The DrawInfo pens with colours of their own: TEXTPEN, FILLTEXTPEN, BARDETAILPEN and
  BARBLOCKPEN (from a palette such as a theme's): Lune's text, the windows' titles, the
  screen bars and the menus are drawn in them.
- Lune: `Window::recalc()` (MUIM_Window_RecalcDisplay): a window grows when its contents
  need more room.
- **KeyShow** (`rambler --utility keyshow`, in the Tools drawer): a full-size keyboard
  drawn with what each key types in the keymap in use (any script, dead keys as their
  accent). The keys held show pressed and Shift or AltGr held shows that level; a click
  on Shift, Ctrl, Alt or AltGr latches it. The keymap and the qualifiers are shown below.

## 0.18.0 – 2026-11-15

### Added
- **MultiView** (`rambler --utility multiview [FILE]`, in the Tools drawer): shows a text
  (in the fixed font, with word wrap), a picture (PNG, JPEG, GIF, BMP, SVG, XPM...; fitted
  to the window or at its size, dragged with the mouse) or anything else as a hex dump.
  Project: Open, Save As, Print, About, Quit; Edit: Copy, Select All, Clear Selected (a
  drag marks lines); Search: Find, Find Next; View: Word Wrap, Fit to Window. Without a
  file it asks for one. Rambler opens a text or a picture with it when the system has no
  program set for that kind of file.
- **Editor** (`rambler --utility editor [FILE]`, in the Tools drawer): a plain text
  editor in the fixed font. Project: New, Open, Save, Save As, Print, About, Quit; Edit:
  Undo, Cut, Copy, Paste, Select All; Search: Find, Find Next, Replace (one or all, with
  or without matching case), Go to Line; Settings: Word Wrap. The title shows a star
  while there are changes not saved, and they are asked about before the text goes; the
  line and column of the cursor are shown below the text.
- **Exchange** (`rambler --utility exchange`, in the Tools drawer) and four commodities
  built into the session: **ClickToFront** (a double click, or a click with Alt, Ctrl or
  Shift, brings a window to the front; a plain click only activates it, as before),
  **AutoPoint** (the window under the pointer becomes active), **Blanker** (the screen
  blanks after 1 to 120 minutes without input) and **NoCapsLock** (Caps Lock does not
  lock). Each is switched Active or Inactive; Show Interface opens the settings of
  ClickToFront and Blanker. A change takes effect at once and is kept for the next
  session.
- **ScreenGrabber** (`rambler --utility screengrabber`, in the Tools drawer): a picture of
  the whole screen or of the active window with its frame, after a delay of 0 to 30
  seconds, its own window hidden meanwhile if wished; shown small, saved as a PNG file
  (by default in the pictures drawer, named after the date and time).
- Lune: an object tells whether it is disabled.

## 0.17.0 – 2026-11-15

### Added
- **Printer preferences** (`rambler --prefs printer`, in the Prefs drawer): the CUPS
  printers with their state, description, location and device; the default printer; a
  test page; for each printer the paper size, type and source, the orientation, two-sided
  printing, colour, resolution and quality, as its driver offers them. The settings are the
  user's own (`lpoptions`), so no rights are asked. *Printers...* opens the system's
  printer tool (or CUPS's web pages) to set printers up.
- **Clock** (`rambler --utility clock`, in the Tools drawer): the time on an analog or a
  digital face, the date in the window's title. Settings menu: seconds, date, 24 hour,
  an alarm (it beeps as the Sound preferences set it up and says so), Save Settings.
- **Calculator** (`rambler --utility calculator`, in the Tools drawer): the keypad and
  the keyboard (digits, + - * /, Return or =, Backspace, Delete clears the entry, Escape
  all, n changes the sign), a tape of what was calculated (Windows > Show Tape), Cut,
  Copy and Paste of the number.
- Lune: a Cycle's entries can be set again; a List can jump to its last lines.
- Lune: **window menus** (Menustrip, Menu, Menuitem): a Lune window's menus show in the
  screen bar like Rambler's own, with check mark items, exclude groups and Super+key
  shortcuts.
- Lune: the Clock class can leave out the seconds hand.

### Fixed
- Window manager: a title a program set right after its window opened did not show (it
  was read before the window's changes were watched).
- Window manager: a dialog centred over a narrow window no longer opens partly off the
  screen.
- Sound preferences: the volume's label clashed with the disk volume's in the Polish
  catalog.

## 0.16.0 – 2026-11-15

### Added
- **Time preferences** (`rambler --prefs time`, in the Prefs drawer): the date in a
  calendar (month, year with - and +), the time on an analog clock that goes until it is
  set (hours, minutes, seconds, - and + for the field typed in). Use sets the system's
  clock, Save also the computer's own clock, with `timedatectl` (or `date` and `hwclock`
  through `pkexec` without it); the system asks for the right to do it. With
  `timedatectl` the time can be set automatically from the network (NTP).
- **Sound preferences** (`rambler --prefs sound`, in the Prefs drawer): what the beep
  does - the screen flashes, a sound is made, or both. The sound is a beep (its volume,
  pitch and length) or a sampled sound (a file), with a Test button. With PulseAudio or
  PipeWire (`pactl`) also the sound output: the device, the master volume and mute. The
  window manager takes a change up at once and plays the sound with `paplay` (or `aplay`).
- Lune: the **Calendar** and **Clock** classes.

### Changed
- Lune: a disabled (ghost) object is covered with dots of the shadow pen, as on the
  original system; until now it only stopped taking input.

## 0.15.0 – 2026-11-15

### Added
- **IControl preferences** (`rambler --prefs icontrol`, in the Prefs drawer): the window
  manager's settings, as Intuition's on the original system. Screens: new windows on the
  front screen or on the first one, whether a screen can be dragged down to the bottom
  edge, the key (Super, Alt or Ctrl) that drags a screen from anywhere. Menus: pull-down
  (from the screen bar) or **pop-up** (the menu titles in a box at the pointer, each
  opening beside it; the screen bar can still drop them down), the classic or the 3D look,
  sticky menus (a short click of the menu button leaves them open; a click
  picks an item or closes them). Windows: whether they can be moved partly off the
  screen. The window manager takes a change up at once.

### Changed
- **Window > View > All Files** works as on the original system: not ticked, a drawer shows
  only what has an icon file of its own (`.info`). It is ticked by default (few files on
  Linux have icons). The hidden files (`.name`) have an item of their own, **Hidden
  Files**, and the Rambler preferences a "Hidden files" switch beside the default view.

## 0.14.0 – 2026-11-15

### Added
- **Default icons** (as `ENV:Sys/def_*.info`): a file, drawer or volume without an icon of
  its own shows the default icon of its kind from `~/.config/rambler/icons` (or the
  session's `$XDG_RUNTIME_DIR/rambler/icons`, or the installation's
  `share/rambler/icons`): `def_drawer`, `def_tool`, `def_project`, `def_disk`,
  `def_harddisk`, `def_ram`, `def_trashcan`, `def_txt`, `def_picture`, `def_sound`,
  `def_movie`, `def_archive`, `def_document`, `def_script`, `def_cdrom`, `def_network`
  (`.info` files of any kind Rambler reads, dual ones too). Without them the built-in
  pictures stay.
- **Information saves into the icon file** (PutDiskObject): a file with an icon of its own
  keeps its tool types and default tool in it, as on the original system. A PNG icon is
  written again (both images of a dual icon stay); a classic icon - also with NewIcons or
  a colour icon - gets only its strings changed, everything else stays byte for byte,
  the NewIcons image lines too.
- **Snapshot writes the place into the icon file** (do_CurrentX/Y) of an icon that has
  one, so it goes with the file; Unsnapshot takes it away.

### Fixed
- Snapshot of an icon kept nothing since 0.13.0 (it was undone at once); it works again.

## 0.13.3 – 2026-11-15

### Fixed
- **"anylinux" AppImages had no icons**: they carry a DwarFS image instead of squashfs
  (the uruntime runtime). Their icons are now taken out with `dwarfsextract` (the program
  itself or the DwarFS project's `dwarfs-universal`, also from `~/.local/bin`), still
  without running the AppImage. DwarFS is not packaged by Debian yet:
  `./install.sh --with-dwarfs` downloads the DwarFS project's program from its releases
  on GitHub into `/usr/local/bin`.

### Added
- An AppImage put into the menus by a desktop tool (AppImageLauncher, Gear Lever,
  appimaged) gets the icon its `.desktop` file names, when its own cannot be taken out.

## 0.13.2 – 2026-11-15

### Fixed
- More AppImages show their icons: ones without the "AI" mark in their header (known by
  the name `*.AppImage`), ones whose file system is not right after the program (found by
  its magic), images with extended attributes (`unsquashfs -no-xattrs`; what was taken out
  counts whatever the exit code), and `.DirIcon` links that are absolute or go up a
  directory inside the image. JPEG and BMP pictures are taken too.

### Added
- `rambler --appimage-icon FILE` tells each step of taking an AppImage's icon out, to find
  why one has none.

## 0.13.1 – 2026-11-15

### Fixed
- **The Prefs drawer**: its launchers had no icons, showed as "Input.desktop" and could not
  be started (opening one gave "sh: Lune.desktop: not found"). Opening a `.desktop` file
  now starts its program; drawers show a `.desktop` file by its name and its icon (from
  the icon themes). The preferences programs have icons of their own, and the launchers
  of 0.13.0 are made again with them.
- **AppImages without icons**: when taking an icon out failed (no `unsquashfs` yet, or an
  older one that does not know the options used), the AppImage was marked as having no
  icon for good. Now only an AppImage really without an icon is marked, a failure is
  tried again (and said in the session's log), and the marks of 0.13.0 are not trusted.
  `install.sh` says when `unsquashfs` is missing.

## 0.13.0 – 2026-11-15

### Changed
- **The port is now called Rambler, and its toolkit Lune**, to keep it apart from the AROS
  originals (Wanderer and Zune). The programs are `rambler`, `rambler-wm` and
  `rambler-session`, the package is `rambler-x11` (it replaces `wanderer-x11`), the
  session is "Rambler", the settings are in `~/.config/rambler` and the X properties are
  `_RAMBLER_*`. Files and comments ported from AROS still give the original names.
- The settings of an earlier version are copied from `~/.config/wanderer` once, at the
  first start (`zune.prefs` becomes `lune.prefs`, `wanderer.prefs` becomes `rambler.prefs`);
  `install.sh` removes the `wanderer-x11` package and files of the old name.

### Added
- **Preferences programs on their own**, as in SYS:Prefs: the Prefs drawer has Input, Lune
  and Rambler (`rambler --prefs lune|rambler`). Use keeps the settings for the session
  (`$XDG_RUNTIME_DIR/rambler`, as ENV:), Save also in `~/.config/rambler` (as ENVARC:); a
  running Rambler takes the change up at once, as IPrefs does.
- Icons dragged from a drawer onto the root window's background are left out there, where
  they were dropped (as Leave Out does; they stay in their drawer on the disk).
- **ScreenMode preferences** (`rambler --prefs screenmode`, in the Prefs drawer): the
  display mode and refresh rate of an output, a screen larger than the mode with
  Autoscroll (xrandr panning), and Test, which puts the mode on for ten seconds and then
  goes back. Saved settings are put into effect at the start of a session.
- **Locale preferences** (`rambler --prefs locale`): the preferred languages (the first
  one with a catalog is the interface's; programs get them as `LANGUAGE`), the country
  (the formats of dates, numbers and money: an installed locale) and the timezone (`TZ`).
  The session exports the saved ones; after Use, the programs Rambler starts get them.
- **Pointer preferences** (`rambler --prefs pointer`): the X cursor theme and size, with
  the Normal and Busy pointers shown from the theme's own files. They go into the X
  resources, the root window and the programs started from then on.
- **Font preferences** (`rambler --prefs font`): the icons' font, the screen text and the
  system's fixed-width text, each any font of the system with its size, chosen in a new
  **font requester** (Lune's, as ASL's: families, sizes, a sample). The family choices in
  the GUI settings and Rambler preferences stay; picking one there replaces the font by name.
  The window manager's title bars and screen bar use the screen font too.
- **Network preferences** (`rambler --prefs network`), with NetworkManager doing the work
  (`nmcli`): Interfaces (up or down, DHCP or a static address with netmask, gateway and
  name servers), Host (the computer's name) and Wireless (the networks in reach with
  signal and security, connecting with a password, rescanning). Use changes an interface
  for now only, Save changes its connection for good; wrong addresses are pointed out
  before anything is changed.
- **Icon files and dual icons.** A file's icon comes from its `name.info` beside it (a
  drawer's from `drawer.info`, a volume's from `Disk.info` in its root), as icon.library
  reads them: classic icons (planar images in the screen's pens), NewIcons (images in the
  tool types), colour icons (FORM ICON with palette or ARGB images) and PNG icons. A
  **dual icon** shows its own second image when selected (SelectRender, `IM2=`, the second
  IMAG / ARGB chunk, the PNG attached after the first); an icon with one image is shown
  lighter, as on AROS. The `.info` files are not listed themselves, and they go with
  their file when it is copied, moved, renamed or deleted.
- **AppImages show their own icons.** The icon (`.DirIcon`, PNG, SVG or XPM, also through
  a link) is taken out of the AppImage's file system with `unsquashfs` - the program is
  never run - and kept in `~/.cache/rambler/appimage-icons`; without it, the thumbnail
  desktop tools left in `~/.cache/thumbnails` is used. A `.info` beside the AppImage still
  comes first. The package recommends `squashfs-tools`.
- **Dual icons for AppImages**: *Icon > Snapshot* on an AppImage without a `.info` writes
  one for it (as Snapshot gives a file shown with a default icon an icon of its own): a
  dual PNG icon - the AppImage's picture, then the selected image (made lighter), which
  can be painted over in any paint program. PNG icons are written as on AROS, with the
  `icOn` chunk (position, type, default tool, tool types), which is now also read.
- **What an icon file holds is used**: its tool types (shown and used, e.g. `STARTPRI`,
  unless set in Information), its default tool (when it names a program there is on this
  system) and its position in the drawer (until the drawer is snapshot). Information
  shows the file's own icon; clicking it shows the selected image.

### Fixed
- Polish: the Scrollbars page of the Lune preferences was named like the Sliders page.
- The close gadget of the Lune preferences now cancels, as Cancel does.
- A window that remembers its size is never opened smaller than its contents need.
- A click during the screen flash of the display beep was lost; it now goes to the window
  under it.
- A click in a window could leave the pointer frozen for that window while a menu or a
  drag was in progress in the window manager.

## 0.12.0 – 2026-11-15

- **Changed keys:** Return presses buttons and Space ticks checkmarks, as on the original
  system (in 0.11.0 Space pressed buttons too). Every key of the user interface can now be
  set in GUI Settings > Navigation.
- **List view** of drawers laid out like the original: Type, Name, Size, Protection, Last
  Accessed and Comment columns under a raised title row. Clicking Name, Size or Last
  Accessed sorts by it; a second click reverses the order.
- **Startup drawer** as the original tool: the `STARTPRI` tool type sets the order, `WAIT`
  a pause after a program. A program that cannot be started shows a warning.
- **Programs' icons:** Snapshot, Unsnapshot, Delete and Empty Trash are passed on to the
  program, for the items it declares in `_WANDERER_APPICON_SUPPORTS` (see the README).
- Clicking the root window makes it the active window, so its menus and keys apply.
- The tool bar's search button opens Find in the drawer.
- **GUI Settings** has every page of the original: System (bubble help delays),
  Sliders, Scrollbars, Listviews, Strings, Navigation, Special and Frames are new, and
  Buttons is complete.
- **Zune**
  - Keys as the original's key specs (`-repeat shift tab`, `control up`...), for buttons,
    cycles (the pop-up works from the keyboard), lists, sliders and windows.
  - Scrollbars with arrows, arranged as set; vertical sliders; Balance bars that share
    the room between objects; background patterns.
  - Custom frames made of pictures (a `frame.config` file, see the README).
  - Lists, strings and buttons take their colours, backgrounds and fonts from the settings.
  - Empty spaces lay out as in the original, so frames such as the slider examples
    take their full width.
- `install.sh` installs the package from a temporary copy, so apt shows no notice about
  an unsandboxed download.
- Dates follow the system language.

## 0.11.0 – 2026-11-14

- **Find** laid out like the original tool: a path with a drawer requester, a name pattern
  (`#?` matches anything) and an optional text the files must contain. The results are a
  list with columns (full path, size, attributes, date, time, comment). Clicking a column
  title sorts by it, and a second click reverses the order. Open, View and Drawer act on
  the chosen file. The search runs in steps, so Stop ends it at once.
- **Zune keyboard control**
  - Tab and Shift+Tab move between gadgets, with the dashed outline of the active one.
  - Space presses buttons and checkmarks; cycles step with Space, Up and Down.
  - Windows open without an active object, so a stray key does not answer a requester.
    A text field takes the keyboard.
  - A window remembers its active object when you leave it.
- **Zune**: lists with columns and a title row, vertical gauges, a horizontal bar.
- **Disk Info** laid out like the original tool: device and file system, sizes, a vertical
  gauge, block size and state.
- **Rename and New Drawer** laid out like the original: a group frame, a bar, a third of
  the screen wide. Names with `/` or `:` are refused.
- **Execute Command** laid out like the original: the label above the text field and its
  file button, the buttons together on the right.
- **Delete** asks first about files without write permission (Delete / All / Skip / Abort).
- **Copy** asks Skip / Skip All / Abort when a file or drawer cannot be read.
- **Super keys**: the left Super key with M or N flips screens. The right Super key goes on
  to the program, so Super+N in Wanderer makes a new drawer.
- **Wanderer preferences**
  - Icon auto-sort for drawer windows.
  - Image-label spacing, label text padding and label frame size.
  - X and Y offsets for background tiles.

## 0.10.0 – 2026-11-14

- **Wanderer preferences** laid out like the original preferences program, with three pages.
  - *General*: navigation, the root window, the screen title.
  - *Appearance*: a picture button for the root window and for drawers, and **Advanced**
    options with Icons, Labels and Background pages, including a default sort order.
  - *Toolbar*: the bars, with a preview.
- **Zune preferences** as the Zune preferences program: a page list (Windows, Groups,
  Buttons, Cycles, Strings, Listviews, Scrollbars, Palette). Every standard frame kind can
  be chosen, with window and group spacing and the cycle menu settings.
- **New Zune classes**
  - **Slider**: a knob with the number in it.
  - **Scale**: percent marks under a gauge.
  - **Pop-up buttons** (file, drawer, pop-up images).
  - **Cycle pop-up menu**: a press on the text opens the entries.
- **File access window** as in the original: an objects gauge, the file name and a bytes
  gauge, each gauge with a scale, and Stop.
- **Menus**
  - Multiple selection: the left button picks an item while the right button keeps the
    menus open.
  - Ticks and ghosting change while the menus are open.
- **Icons**
  - Shift + double click passes the icons selected in every window to the tool, or to
    the project's default tool, as arguments.
  - Opening a file that no program opens shows Execute Command with its name.
- **Requesters**: Super+V answers yes, Super+B or Escape answers no.
- **Wheel** in icon windows works as in the original: sideways when there is nothing to
  scroll down or with Alt held. Ctrl jumps to the edge, Shift scrolls a page.
- **Execute Command** has a file requester button.
- **Fixes**
  - The backdrop could open as a normal window at session start.
  - A crash in the preferences' picture preview.
  - The objects counter counted a file as done before it was.

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
