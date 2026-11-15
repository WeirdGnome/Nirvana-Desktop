/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The icon images of the port: original pictures drawn in code (no icon set of another
    project is used), in the style of the system's icons - a light from the top left, a
    black outline, the screen's palette with a few extra colours. Every kind has a normal
    and a selected image (the selected one darker, as the icon library's "selected" state).
*/
#pragma once
#include <string>
#include <vector>

namespace rambler {

enum class IconKind {
    Drawer, OpenDrawer, Harddisk, Removable, Optical, RamDisk, Trash,
    Project, Text, Picture, Sound, Movie, Archive, Document, Tool, Script, Link,
    Network, NetworkDisk,
    // the preferences programs (the Prefs drawer): a panel and the program's sign
    PrefsFont, PrefsIControl, PrefsInput, PrefsLocale, PrefsLune, PrefsNetwork, PrefsPointer, PrefsPrinter, PrefsRambler, PrefsScreenMode, PrefsSound, PrefsTime, UtilityClock
};
// the default icon files that stand for a kind (ENV:Sys/def_*.info), the first found used:
// e.g. Text -> def_txt, def_ascii, def_text, def_project
std::vector<std::string> default_icon_names(IconKind k);
// the built-in icon a .desktop file asks for (Icon=rambler-prefs-input ...); false - none
bool builtin_icon(const std::string &name, IconKind &k);

// the kind of a file or directory (by type and name)
IconKind icon_kind(const std::string &path, bool is_dir, bool is_volume, bool executable);
const int ICON_W = 48, ICON_H = 42;
void draw_icon(IconKind kind, int x, int y, bool selected);

}  // namespace rambler
