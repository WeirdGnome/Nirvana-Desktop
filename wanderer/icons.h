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

namespace wanderer {

enum class IconKind {
    Drawer, OpenDrawer, Harddisk, Removable, Optical, RamDisk, Trash,
    Project, Text, Picture, Sound, Movie, Archive, Document, Tool, Script, Link
};

// the kind of a file or directory (by type and name)
IconKind icon_kind(const std::string &path, bool is_dir, bool is_volume, bool executable);
const int ICON_W = 48, ICON_H = 42;
void draw_icon(IconKind kind, int x, int y, bool selected);

}  // namespace wanderer
