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
    ~DiskObject();

private:
    mutable Fl_RGB_Image *img_[2] = {nullptr, nullptr};
};

// the icon file of a file, drawer or volume (path + ".info"; a volume's root: Disk.info);
// nullptr - none (or not readable). Kept until the file changes.
std::shared_ptr<DiskObject> get_disk_object(const std::string &path, bool volume = false);
// reading an icon file itself
std::shared_ptr<DiskObject> read_icon_file(const std::string &info_file);

}  // namespace rambler
