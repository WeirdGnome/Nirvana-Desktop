/*
    Copyright (C) 2002-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Image specs (workbench/libs/muimaster/imspec.c, penspec.c, imspec_gradient.c): a
    background or an image given as a string -
      "0:<n>"      a pattern (MUII_BACKGROUND 128 ... MUII_MARKBACKGROUND 145)
      "2:m<n>"     a MUI pen (MPEN_*)
      "2:r<r>,<g>,<b>"   a colour, each 32 bit hexadecimal
      "5:<file>"   a picture, repeated
      "7:<angle>,<r>,<g>,<b>-<r>,<g>,<b>"   a gradient over the object (angle 0: from the
                   top down, 90: from the left)
    A bare number is taken as a pattern.
*/
#pragma once
#include <string>

namespace zune {

enum ImSpecType { IMS_PATTERN, IMS_PEN, IMS_RGB, IMS_BITMAP, IMS_GRADIENT, IMS_NONE };

struct ImSpec {
    ImSpecType type = IMS_NONE;
    int pattern = 128, pen = 2;
    unsigned rgb = 0, rgb2 = 0;   // 0xRRGGBB (the gradient's start and end)
    int angle = 0;
    std::string file;
    static ImSpec parse(const std::string &spec);
    std::string str() const;
};

void draw_imspec(const std::string &spec, int x, int y, int w, int h);
void draw_imspec(const ImSpec &spec, int x, int y, int w, int h);

// the colour of a pen spec ("m<n>", "r<r>,<g>,<b>"; penspec.c)
unsigned penspec_rgb(const std::string &pen);

}  // namespace zune
