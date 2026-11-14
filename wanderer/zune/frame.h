/*
    Copyright (C) 2002-2026, The AROS Development Team.
    Copyright (C) 1999, David Le Corfec.
    All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The built-in Zune frames (workbench/libs/muimaster/frame.c) and frame specs
    (classes/configdata.c): a spec "TSLRTB" - T the frame type (hex digit),
    S the state (0 up, 1 down), then the inner spacing left, right, top, bottom.
*/
#pragma once
#include <string>

namespace zune {

enum FrameType {
    FST_NONE, FST_RECT, FST_BEVEL, FST_THIN_BORDER, FST_THICK_BORDER, FST_ROUND_BEVEL,
    FST_WIN_BEVEL, FST_ROUND_THICK_BORDER, FST_ROUND_THIN_BORDER, FST_GRAY_BORDER,
    FST_SEMIROUND_BEVEL, FST_ROUNDED, FST_COUNT
};

struct FrameSpec {
    int type = FST_NONE;
    bool down = false;
    int inner_left = 0, inner_right = 0, inner_top = 0, inner_bottom = 0;
    static FrameSpec parse(const std::string &spec);
};

// The frame's own width on each side (the "border" of the ZuneFrameGfx table)
struct FrameBorders { int left, right, top, bottom; };
FrameBorders frame_borders(int type, bool down);

// Draws the frame (state: down = pressed / recessed). Does not fill the inside.
void draw_frame(int type, bool down, int x, int y, int w, int h);
inline void draw_frame(const FrameSpec &f, int x, int y, int w, int h, bool selected = false) {
    draw_frame(f.type, f.down != selected, x, y, w, h);
}

// The frames Zune gives its classes by default (MUIV_Frame_*)
enum StdFrame {
    Frame_None, Frame_Button, Frame_ImageButton, Frame_Text, Frame_String, Frame_ReadList,
    Frame_InputList, Frame_Prop, Frame_Gauge, Frame_Group, Frame_PopUp, Frame_Virtual,
    Frame_Slider, Frame_Knob, Frame_Drag, Frame_Count
};
const FrameSpec &std_frame(StdFrame which);

}  // namespace zune
