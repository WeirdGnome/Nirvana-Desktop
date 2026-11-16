/*
    Copyright (C) 2002-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The original's GUI settings file (ENV:Zune/global.prefs, as a theme brings it) read into
    Lune's preferences: an IFF FORM PREF with a MUIC chunk of records (an identifier, a
    length and the data - classes/dataspace.c), the identifiers those of
    classes/configdata.h. Numbers are big-endian ULONGs; fonts ("Name Style.font/size"),
    frames, image and pen specs are strings, in the formats Lune reads too. The records of
    other classes (MCCs) are passed over.
*/
#include "lune.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <strings.h>

namespace lune {

namespace {
uint32_t be32(const std::string &d, size_t i) {
    if (i + 4 > d.size()) return 0;
    return ((uint32_t)(uint8_t)d[i] << 24) | ((uint32_t)(uint8_t)d[i + 1] << 16) | ((uint32_t)(uint8_t)d[i + 2] << 8) |
           (uint32_t)(uint8_t)d[i + 3];
}

// "Open Sans Regular/30" -> "Open Sans", 30
void font_spec(const std::string &s, std::string &family, int &size) {
    auto slash = s.rfind('/');
    std::string n = s.substr(0, slash);
    size = slash == std::string::npos ? 0 : atoi(s.c_str() + slash + 1);
    if (n.size() > 5 && !strcasecmp(n.c_str() + n.size() - 5, ".font")) n.resize(n.size() - 5);
    for (const char *style : {" Regular", " Normal", " Book", " Roman", " Medium"})
        if (n.size() > strlen(style) && !strcasecmp(n.c_str() + n.size() - strlen(style), style)) n.resize(n.size() - strlen(style));
    family = n;
}
}  // namespace

bool import_zune_prefs(const std::string &file, Prefs &p) {
    std::ifstream in(file, std::ios::binary);
    std::string d((std::istreambuf_iterator<char>(in)), {});
    if (d.size() < 12 || d.compare(0, 4, "FORM") || d.compare(8, 4, "PREF")) return false;
    bool any = false;
    size_t i = 12;
    while (i + 8 <= d.size()) {
        uint32_t clen = be32(d, i + 4);
        if (!d.compare(i, 4, "MUIC")) {
            size_t q = i + 8, end = std::min(d.size(), q + clen);
            while (q + 8 <= end) {
                uint32_t id = be32(d, q), len = be32(d, q + 4);
                q += 8;
                if (len > end - q) break;
                std::string v = d.substr(q, len);
                q += len;
                uint32_t u = len == 4 ? be32(v, 0) : 0;
                std::string s(v.c_str(), strnlen(v.c_str(), v.size()));   // (a string's record)
                auto num = [&](int &field) { if (len == 4) { field = (int)u; any = true; } };
                auto flag = [&](bool &field) { if (len == 4) { field = u != 0; any = true; } };
                auto str = [&](std::string &field) { if (!s.empty()) { field = s; any = true; } };
                auto frame = [&](int f) { if (!s.empty()) { p.frame[f] = s; any = true; } };
                std::string fam;
                int size = 0;
                switch (id) {
                case 0x01: num(p.window_left); break;
                case 0x02: num(p.window_right); break;
                case 0x03: num(p.window_top); break;
                case 0x04: num(p.window_bottom); break;
                case 0x05: num(p.radio_hspacing); break;
                case 0x06: num(p.radio_vspacing); break;
                case 0x07: num(p.group_hspacing); break;
                case 0x08: num(p.group_vspacing); break;
                case 0x09: num(p.scrollbar_arrangement); break;
                case 0x0a: num(p.list_refresh); break;
                case 0x0b: num(p.list_leading); break;
                case 0x0c: num(p.list_smoothval); break;
                case 0x0d: num(p.list_multi); break;
                case 0x0f: num(p.group_title_position); break;
                case 0x10: num(p.group_title_color); break;
                case 0x11: num(p.cycle_menu_min); break;
                case 0x12: num(p.cycle_menu_position); break;
                case 0x18: frame(Frame_Drag); break;
                case 0x19: flag(p.cycle_menu_recessed); break;
                case 0x1b: flag(p.list_smoothed); break;
                case 0x1d: num(p.window_redraw); break;
                // the fonts: the normal (screen) one by name, the others' sizes
                case 0x1e: if (!s.empty()) { font_spec(s, fam, size); p.font_name = fam; if (size > 0) p.font_size = size; any = true; } break;
                case 0x1f: if (!s.empty()) { font_spec(s, fam, size); p.list_font_size = size; any = true; } break;
                case 0x20: if (!s.empty()) { font_spec(s, fam, size); p.tiny_font_size = size; any = true; } break;
                case 0x21: if (!s.empty()) { font_spec(s, fam, size); p.fixed_font_name = fam; p.fixed_font_size = size; any = true; } break;
                case 0x22: if (!s.empty()) { font_spec(s, fam, size); p.title_font_size = size; any = true; } break;
                case 0x23: if (!s.empty()) { font_spec(s, fam, size); p.big_font_size = size; any = true; } break;
                case 0x80: if (!s.empty()) { font_spec(s, fam, size); p.button_font_size = size; any = true; } break;
                case 0x2b: frame(Frame_Button); break;
                case 0x2c: frame(Frame_ImageButton); break;
                case 0x2d: frame(Frame_Text); break;
                case 0x2e: frame(Frame_String); break;
                case 0x2f: frame(Frame_ReadList); break;
                case 0x30: frame(Frame_InputList); break;
                case 0x31: frame(Frame_Prop); break;
                case 0x32: frame(Frame_Gauge); break;
                case 0x33: frame(Frame_Group); break;
                case 0x34: frame(Frame_PopUp); break;
                case 0x35: frame(Frame_Virtual); break;
                case 0x36: frame(Frame_Slider); break;
                case 0x90: frame(Frame_Knob); break;
                case 0x37: str(p.window_back); break;
                case 0x38: str(p.requester_back); break;
                case 0x39: str(p.button_back); break;
                case 0x3a: str(p.list_back); break;
                case 0x3b: str(p.text_back); break;
                case 0x3c: str(p.prop_back); break;
                case 0x3d: str(p.popup_back); break;
                case 0x3e: str(p.selected_back); break;
                case 0x3f: str(p.list_cursor); break;
                case 0x40: str(p.list_select); break;
                case 0x41: str(p.list_selcur); break;
                case 0x54: str(p.register_back); break;
                case 0x5a: str(p.group_back); break;
                case 0x5b: str(p.slider_back); break;
                case 0x5c: str(p.slider_knob); break;
                case 0x95: str(p.page_back); break;
                case 0x96: str(p.readlist_back); break;
                case 0x7a: num(p.window_positions); break;
                case 0x7b: num(p.balancing_look); break;
                case 0x83: num(p.scrollbar_type); break;
                case 0x84: str(p.string_bg); break;
                case 0x85: str(p.string_text); break;
                case 0x86: str(p.string_active_bg); break;
                case 0x87: str(p.string_active_text); break;
                case 0x89: flag(p.drag_left); break;
                case 0x8a: flag(p.drag_middle); break;
                case 0x8b: str(p.drag_left_modifier); break;
                case 0x8c: str(p.drag_middle_modifier); break;
                case 0x8d: flag(p.drag_autostart); break;
                case 0x8e: num(p.drag_autostart_length); break;
                case 0x8f: str(p.active_object_color); break;
                case 0x94: num(p.drag_look); break;
                case 0x400: str(p.string_cursor); break;
                case 0x401: str(p.string_marked_bg); break;
                case 0x402: str(p.string_marked_text); break;
                case 0x403: flag(p.register_truncate_titles); break;
                case 0x404: num(p.window_refresh); break;
                case 0x50a: num(p.window_buttons); break;
                case 0x706: num(p.bubble_first); break;
                case 0x707: num(p.bubble_next); break;
                default:
                    // the keys (MUIKEY_PRESS .. MUIKEY_POPUP) and the custom frames
                    if (id >= 0x5f && id <= 0x74 && !s.empty()) { p.keys[id - 0x5f] = s; any = true; }
                    else if (id >= 0x600 && id <= 0x60f) { p.custom_frame[id - 0x600] = s; any = true; }
                    break;
                }
            }
        }
        i += 8 + clen + (clen & 1);
    }
    return any;
}

}  // namespace lune
