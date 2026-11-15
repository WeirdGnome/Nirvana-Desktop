/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Icon files (icon.library: GetDiskObject and its readers, diskobjio.c, diskobj35io.c,
    diskobjPNGio.c): the ".info" file beside a file or drawer ("Disk.info" in a volume's
    root) gives its icon. The formats read:

      - the classic icon: a DiskObject with planar images in the screen's first pens - the
        normal image (GadgetRender) and, for a dual icon, the selected one (SelectRender);
        without it the selected image is the normal one complemented (GADGHCOMP);
      - the colour icon of the later system versions (an IFF FORM ICON after the classic
        part): FACE, then one or two IMAG chunks (palette images, RLE packed) or ARGB
        chunks (zlib packed);
      - the PNG icon: a PNG file, and a second PNG after the first one's IEND is the
        selected image.

    An icon with one image only gets its selected image made from the normal one, lighter
    (ChangeToSelectedIconColor, EFFECT_LIGHTEN).
*/
#pragma once
#include <memory>
#include <string>
#include <vector>

class Fl_RGB_Image;

namespace rambler {

struct DiskObject {
    int width = 0, height = 0;
    std::vector<unsigned char> normal, selected;   // RGBA, width * height * 4
    bool dual = false;                             // the file had its own selected image
    int type = 0;                                  // do_Type: WBDISK 1, WBDRAWER 2, WBTOOL 3 ...
    std::string default_tool;
    std::vector<std::string> tool_types;
    int current_x = (int)0x80000000, current_y = (int)0x80000000;   // NO_ICON_POSITION
    Fl_RGB_Image *image(bool selected) const;      // (made the first time)

private:
    // the images made for drawing: each object its own (a copy starts without them)
    struct Images {
        Fl_RGB_Image *p[2] = {nullptr, nullptr};
        Images() = default;
        Images(const Images &) {}
        Images &operator=(const Images &) { return *this; }
        ~Images();
    };
    mutable Images img_;
};

// the icon file of a file, drawer or volume (path + ".info"; a volume's root: Disk.info);
// nullptr - none (or not readable). Kept until the file changes.
std::shared_ptr<DiskObject> get_disk_object(const std::string &path, bool volume = false);
// reading an icon file itself
std::shared_ptr<DiskObject> read_icon_file(const std::string &info_file);
// an icon made from a picture (PNG, SVG ...), scaled to fit max x max; one image, the
// selected one lighter
std::shared_ptr<DiskObject> icon_from_picture(const std::string &picture, int max);
// writing a PNG icon (PutDiskObject in the PNG form): the normal image, the icOn chunk
// (position, type, default tool, tool types), the selected image after it - a dual icon
bool put_png_icon(const std::string &info_file, const DiskObject &o);
// a named icon of the icon themes (a .desktop file's Icon=: a name, or a picture's path),
// the size nearest an icon's; nullptr - not found
std::shared_ptr<DiskObject> theme_icon(const std::string &name);
// a default icon (def_drawer ...): def_NAME.info in the session's icons ($XDG_RUNTIME_DIR/
// rambler/icons, as ENV:Sys), the user's (~/.config/rambler/icons, as ENVARC:Sys) or the
// installation's (share/rambler/icons); the name's case does not matter
std::shared_ptr<DiskObject> default_disk_object(const std::string &name);
// the tool types and (when not nullptr) the default tool written into an icon file of any
// kind Rambler reads (PutDiskObject): a PNG icon is written again, a classic one (also with
// NewIcons or a colour icon) gets only its strings changed; false - it could not be done
bool put_icon_strings(const std::string &info_file, const std::string *default_tool,
                      const std::vector<std::string> &tool_types);
// an AppImage's own icon (appimage.cxx): nullptr - not an AppImage, or no icon in it
std::shared_ptr<DiskObject> appimage_icon(const std::string &path);
// the steps of taking an AppImage's icon out, told on stdout (rambler --appimage-icon FILE)
int appimage_icon_trace(const std::string &path);

}  // namespace rambler
