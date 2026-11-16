/*
    Copyright (C) 2011-2026, The AROS Development Team. All rights reserved.
    Port to X11: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The decoration themes: see decor.h. Ported from
      workbench/libs/decortheme/themeconfig.c, themeimages.c, themeelements.c
      workbench/libs/decorator/drawfuncs.c, decoratorelement.c, decorimagefuncs.c
      workbench/c/Decoration/windowdecorclass.c, screendecorclass.c
      rom/intuition/openwindow.c (the border sizes)
*/
#include "decor.h"

#include <png.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

namespace decor {

namespace {
inline int A(uint32_t c) { return (c >> 24) & 0xff; }
inline int R(uint32_t c) { return (c >> 16) & 0xff; }
inline int G(uint32_t c) { return (c >> 8) & 0xff; }
inline int B(uint32_t c) { return c & 0xff; }
inline uint32_t ARGB(int a, int r, int g, int b) { return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }
}  // namespace

// ============================================================================================
// Files: the original's file system does not mind the case of names
// ============================================================================================
std::string find_path(const std::string &dir, const std::string &rel) {
    std::string cur = dir;
    size_t a = 0;
    while (a <= rel.size()) {
        size_t b = rel.find('/', a);
        if (b == std::string::npos) b = rel.size();
        std::string part = rel.substr(a, b - a);
        a = b + 1;
        if (part.empty()) continue;
        std::string found;
        if (DIR *d = opendir(cur.c_str())) {
            while (dirent *e = readdir(d)) {
                if (!strcmp(e->d_name, part.c_str())) { found = e->d_name; break; }
                if (found.empty() && !strcasecmp(e->d_name, part.c_str())) found = e->d_name;
            }
            closedir(d);
        }
        if (found.empty()) return "";
        cur += "/" + found;
    }
    return cur;
}

namespace {
// a PNG file as ARGB pixels (the original reads any picture through the datatypes; the
// themes' pictures are PNG files)
bool read_png(const std::string &file, Image &img) {
    png_image pi;
    memset(&pi, 0, sizeof pi);
    pi.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&pi, file.c_str())) return false;
    pi.format = PNG_FORMAT_BGRA;   // (as 32-bit words on a little-endian machine: 0xAARRGGBB)
    std::vector<uint8_t> buf(PNG_IMAGE_SIZE(pi));
    if (!png_image_finish_read(&pi, nullptr, buf.data(), 0, nullptr)) { png_image_free(&pi); return false; }
    img.w = (int)pi.width;
    img.h = (int)pi.height;
    img.data.resize((size_t)img.w * img.h);
    for (size_t i = 0; i < img.data.size(); i++)
        img.data[i] = ARGB(buf[i * 4 + 3], buf[i * 4 + 2], buf[i * 4 + 1], buf[i * 4 + 0]);
    return true;
}

// DGetImageFromFile: a theme's picture with its expected grid of subimages
ImagePtr image_from_file(const std::string &path, const std::string &name, int cols, int rows) {
    std::string f = find_path(path, name);
    if (f.empty()) return nullptr;
    auto img = std::make_shared<Image>();
    if (!read_png(f, *img) || img->w <= 0 || img->h <= 0) return nullptr;
    img->cols = cols;
    img->rows = rows;
    img->ok = true;
    return img;
}

// ============================================================================================
// themeconfig.c
// ============================================================================================
const char *skip_chars(const char *v) {
    const char *c = strchr(v, '=');
    return c ? c + 1 : v + strlen(v);
}
int get_int(const char *v) { return (int)atol(skip_chars(v)); }
void get_integers(const char *v, int *v1, int *v2) {
    char va1[32], va2[32];
    int cnt = sscanf(skip_chars(v), "%31s %31s", va1, va2);
    if (cnt == 1) { *v1 = -1; *v2 = (int)atol(va1); }
    else if (cnt == 2) { *v1 = (int)atol(va1); *v2 = (int)atol(va2); }
}
template <class T1, class T2, class T3>
void get_triple(const char *v, T1 *v1, T2 *v2, T3 *v3) {
    unsigned a, b;
    int c;
    if (sscanf(skip_chars(v), "%x %x %d", &a, &b, &c) == 3) { *v1 = (T1)a; *v2 = (T2)b; *v3 = (T3)c; }
}
void get_colors(const char *v, uint32_t *v1, uint32_t *v2) {
    unsigned a, b;
    int cnt = sscanf(skip_chars(v), "%x %x", &a, &b);
    if (cnt == 2) { *v1 = a; if (v2) *v2 = b; }
    else if (cnt == 1) *v1 = a;
}
bool get_bool(const char *v, const char *id) { return strstr(v, id) != nullptr; }
// (strstr(line, "Key ") == line: the key at the start of the line, a space after it)
bool is(const char *line, const char *key) {
    size_t n = strlen(key);
    return !strncmp(line, key, n) && line[n] == ' ';
}

void load_menu_config(const std::string &path, Config &dc) {
    std::string f = find_path(path, "Menu/Config");
    FILE *file = f.empty() ? nullptr : fopen(f.c_str(), "r");
    if (!file) return;
    char line[256];
    while (fgets(line, sizeof line, file)) {
        if (is(line, "TileLeft")) { dc.MenuTileLeft = get_int(line); dc.MenuIsTiled = true; }
        else if (is(line, "TileTop")) { dc.MenuTileTop = get_int(line); dc.MenuIsTiled = true; }
        else if (is(line, "TileRight")) { dc.MenuTileRight = get_int(line); dc.MenuIsTiled = true; }
        else if (is(line, "TileBottom")) { dc.MenuTileBottom = get_int(line); dc.MenuIsTiled = true; }
        else if (is(line, "InnerLeft")) dc.MenuInnerLeft = get_int(line);
        else if (is(line, "InnerTop")) dc.MenuInnerTop = get_int(line);
        else if (is(line, "InnerRight")) dc.MenuInnerRight = get_int(line);
        else if (is(line, "InnerBottom")) dc.MenuInnerBottom = get_int(line);
        else if (is(line, "HighlightTint")) get_colors(line, &dc.MenuHighlightTint, nullptr);
    }
    fclose(file);
}

bool load_system_config(const std::string &path, Config &dc) {
    std::string f = find_path(path, "System/Config");
    FILE *file = f.empty() ? nullptr : fopen(f.c_str(), "r");
    if (!file) return false;
    char l[256];
    while (fgets(l, sizeof l, file)) {
        if (is(l, "NoInactiveSelected")) dc.GadgetsThreeState = get_bool(l, "Yes");
        else if (is(l, "FrameStyle")) dc.WinFrameStyle = get_int(l);
        else if (is(l, "BarRounded")) dc.BarRounded = get_bool(l, "Yes");
        else if (is(l, "WindowTitleMode")) { dc.TitleOutline = get_bool(l, "Outline"); dc.TitleShadow = get_bool(l, "Shadow"); }
        else if (is(l, "FillTitleBar")) dc.FillTitleBar = get_bool(l, "Yes");
        else if (is(l, "BarMasking")) dc.BarMasking = get_bool(l, "Yes");
        else if (is(l, "CloseRight")) dc.CloseGadgetOnRight = get_bool(l, "Yes");
        else if (is(l, "UseGradients")) dc.UseGradients = get_bool(l, "Yes");
        else if (is(l, "BarLayout")) dc.BarVertical = get_bool(l, "Vertical");
        else if (is(l, "RightBorderGads")) dc.RightBorderGadgets = get_int(l);
        else if (is(l, "HorScrollerHeight")) dc.HorizScrollerHeight = get_int(l);
        else if (is(l, "ScrollerInnerSpacing")) dc.ScrollerInnerSpacing = get_int(l);
        else if (is(l, "BottomBorderGads")) dc.BottomBorderGadgets = get_int(l);
        else if (is(l, "RightBorderNoGads")) dc.RightBorderNoGadgets = get_int(l);
        else if (is(l, "BottomBorderNoGads")) dc.BottomBorderNoGadgets = get_int(l);
        else if (is(l, "BarHeight")) dc.BarHeight = get_int(l);
        else if (is(l, "BarFrame")) dc.BarFrame = get_int(l);
        else if (is(l, "BarJoinTB")) get_integers(l, &dc.BarJoinTB_o, &dc.BarJoinTB_s);
        else if (is(l, "BarPreGadget")) get_integers(l, &dc.BarPreGadget_o, &dc.BarPreGadget_s);
        else if (is(l, "BarPre")) get_integers(l, &dc.BarPre_o, &dc.BarPre_s);
        else if (is(l, "BarLGadgetFill")) get_integers(l, &dc.BarLGadgetFill_o, &dc.BarLGadgetFill_s);
        else if (is(l, "BarJoinGB")) get_integers(l, &dc.BarJoinGB_o, &dc.BarJoinGB_s);
        else if (is(l, "BarLFill")) get_integers(l, &dc.BarLFill_o, &dc.BarLFill_s);
        else if (is(l, "BarJoinBT")) get_integers(l, &dc.BarJoinBT_o, &dc.BarJoinBT_s);
        else if (is(l, "BarTitleFill")) get_integers(l, &dc.BarTitleFill_o, &dc.BarTitleFill_s);
        else if (is(l, "BarRFill")) get_integers(l, &dc.BarRFill_o, &dc.BarRFill_s);
        else if (is(l, "BarJoinBG")) get_integers(l, &dc.BarJoinBG_o, &dc.BarJoinBG_s);
        else if (is(l, "BarRGadgetFill")) get_integers(l, &dc.BarRGadgetFill_o, &dc.BarRGadgetFill_s);
        else if (is(l, "BarPostGadget")) get_integers(l, &dc.BarPostGadget_o, &dc.BarPostGadget_s);
        else if (is(l, "BarPost")) get_integers(l, &dc.BarPost_o, &dc.BarPost_s);
        else if (is(l, "ContainerTop")) get_integers(l, &dc.ContainerTop_o, &dc.ContainerTop_s);
        else if (is(l, "ContainerVertTile")) get_integers(l, &dc.ContainerVertTile_o, &dc.ContainerVertTile_s);
        else if (is(l, "KnobTop")) get_integers(l, &dc.KnobTop_o, &dc.KnobTop_s);
        else if (is(l, "KnobTileTop")) get_integers(l, &dc.KnobTileTop_o, &dc.KnobTileTop_s);
        else if (is(l, "KnobVertGripper")) get_integers(l, &dc.KnobVertGripper_o, &dc.KnobVertGripper_s);
        else if (is(l, "KnobTileBottom")) get_integers(l, &dc.KnobTileBottom_o, &dc.KnobTileBottom_s);
        else if (is(l, "KnobBottom")) get_integers(l, &dc.KnobBottom_o, &dc.KnobBottom_s);
        else if (is(l, "ContainerBottom")) get_integers(l, &dc.ContainerBottom_o, &dc.ContainerBottom_s);
        else if (is(l, "ContainerLeft")) get_integers(l, &dc.ContainerLeft_o, &dc.ContainerLeft_s);
        else if (is(l, "ContainerHorTile")) get_integers(l, &dc.ContainerHorTile_o, &dc.ContainerHorTile_s);
        else if (is(l, "KnobLeft")) get_integers(l, &dc.KnobLeft_o, &dc.KnobLeft_s);
        else if (is(l, "KnobTileLeft")) get_integers(l, &dc.KnobTileLeft_o, &dc.KnobTileLeft_s);
        else if (is(l, "KnobHorGripper")) get_integers(l, &dc.KnobHorGripper_o, &dc.KnobHorGripper_s);
        else if (is(l, "KnobTileRight")) get_integers(l, &dc.KnobTileRight_o, &dc.KnobTileRight_s);
        else if (is(l, "KnobRight")) get_integers(l, &dc.KnobRight_o, &dc.KnobRight_s);
        else if (is(l, "ContainerRight")) get_integers(l, &dc.ContainerRight_o, &dc.ContainerRight_s);
        else if (is(l, "AddSize")) get_integers(l, &dc.SizeAddX, &dc.SizeAddY);
        else if (is(l, "AddUpDown")) get_integers(l, &dc.UpDownAddX, &dc.UpDownAddY);
        else if (is(l, "AddLeftRight")) get_integers(l, &dc.LeftRightAddX, &dc.LeftRightAddY);
        else if (is(l, "ActivatedGradient"))
            get_triple(l, &dc.ActivatedGradientColor_s, &dc.ActivatedGradientColor_e, &dc.ActivatedGradientColor_a);
        else if (is(l, "DeactivatedGradient"))
            get_triple(l, &dc.DeactivatedGradientColor_s, &dc.DeactivatedGradientColor_e, &dc.DeactivatedGradientColor_a);
        else if (is(l, "ShadeValues")) get_triple(l, &dc.ShadeValues_l, &dc.ShadeValues_m, &dc.ShadeValues_d);
        else if (is(l, "BaseColors")) get_colors(l, &dc.BaseColors_a, &dc.BaseColors_d);
        else if (is(l, "WindowTitleColors")) get_colors(l, &dc.TitleColorText, &dc.TitleColorShadow);
        else if (is(l, "LeftBorder")) dc.LeftBorder = get_int(l);
        else if (is(l, "RightBorder")) dc.RightBorder = get_int(l);
        else if (is(l, "BottomBorder")) dc.BottomBorder = get_int(l);
        else if (is(l, "LogoOffset")) dc.SLogoOffset = get_int(l);
        else if (is(l, "TitleOffset")) dc.STitleOffset = get_int(l);
        else if (is(l, "SBarHeight")) dc.SBarHeight = get_int(l);
        else if (is(l, "SBarFrame")) dc.SBarFrame = get_int(l);
        else if (is(l, "SBarChildPre")) get_integers(l, &dc.SBarChildPre_o, &dc.SBarChildPre_s);
        else if (is(l, "SBarChildFill")) get_integers(l, &dc.SBarChildFill_o, &dc.SBarChildFill_s);
        else if (is(l, "SBarChildPost")) get_integers(l, &dc.SBarChildPost_o, &dc.SBarChildPost_s);
        else if (is(l, "SBarGadPre")) get_integers(l, &dc.SBarGadPre_o, &dc.SBarGadPre_s);
        else if (is(l, "SBarGadFill")) get_integers(l, &dc.SBarGadFill_o, &dc.SBarGadFill_s);
        else if (is(l, "SBarGadPost")) get_integers(l, &dc.SBarGadPost_o, &dc.SBarGadPost_s);
        else if (is(l, "LUTBaseColors")) get_colors(l, &dc.LUTBaseColors_a, &dc.LUTBaseColors_d);
        else if (is(l, "ScreenTitleColors")) get_colors(l, &dc.STitleColorText, &dc.STitleColorShadow);
        else if (is(l, "ScreenTitleMode")) { dc.STitleOutline = get_bool(l, "Outline"); dc.STitleShadow = get_bool(l, "Shadow"); }
    }
    fclose(file);
    return true;
}

// ============================================================================================
// themeimages.c
// ============================================================================================
Images load_images(const Config &dc) {
    int cols = dc.GadgetsThreeState ? 3 : 4;   // the subimage columns of a window gadget
    const std::string &p = dc.ThemePath;
    Images di;
    di.sdepth = image_from_file(p, "System/SDepth/Default", 2, 1);
    di.stitlebar = image_from_file(p, "System/STitlebar/Default", 1, 1);
    di.sbarlogo = image_from_file(p, "System/SBarLogo/Default", 1, 1);
    di.size = image_from_file(p, "System/Size/Default", cols, 1);
    di.close = image_from_file(p, "System/Close/Default", cols, 1);
    di.depth = image_from_file(p, "System/Depth/Default", cols, 1);
    di.zoom = image_from_file(p, "System/Zoom/Default", cols, 1);
    di.mui = image_from_file(p, "System/MUI/Default", cols, 1);
    di.popup = image_from_file(p, "System/PopUp/Default", cols, 1);
    di.snapshot = image_from_file(p, "System/Snapshot/Default", cols, 1);
    di.iconify = image_from_file(p, "System/Iconify/Default", cols, 1);
    di.lock = image_from_file(p, "System/Lock/Default", cols, 1);
    di.up = image_from_file(p, "System/ArrowUp/Default", cols, 1);
    di.down = image_from_file(p, "System/ArrowDown/Default", cols, 1);
    di.left = image_from_file(p, "System/ArrowLeft/Default", cols, 1);
    di.right = image_from_file(p, "System/ArrowRight/Default", cols, 1);
    di.winbar_normal = image_from_file(p, "System/Titlebar/Default", 1, 2);
    di.border_normal = image_from_file(p, "System/Borders/Default", 1, 1);
    di.border_deactivated = image_from_file(p, "System/Borders/Default_Deactivated", 1, 1);
    di.verticalcontainer = image_from_file(p, "System/Container/Vertical", 2, 1);
    di.verticalknob = image_from_file(p, "System/Knob/Vertical", 3, 1);
    di.horizontalcontainer = image_from_file(p, "System/Container/Horizontal", 1, 2);
    di.horizontalknob = image_from_file(p, "System/Knob/Horizontal", 1, 3);
    di.menu = image_from_file(p, "Menu/Background/Default", 1, 1);
    di.menucheck = image_from_file(p, "Menu/Checkmark/Default", 1, 1);
    di.submenu = image_from_file(p, "Menu/SubMenu/Default", 1, 1);
    // the menus' command key picture: the drawer in Menu whose name ends in "Key"
    if (std::string menu = find_path(p, "Menu"); !menu.empty())
        if (DIR *d = opendir(menu.c_str())) {
            while (dirent *e = readdir(d)) {
                size_t n = strlen(e->d_name);
                if (n > 3 && !strcasecmp(e->d_name + n - 3, "key")) {
                    di.menukey = image_from_file(menu, std::string(e->d_name) + "/Default", 1, 1);
                    break;
                }
            }
            closedir(d);
        }
    return di;
}

// ============================================================================================
// themeelements.c
// ============================================================================================
void init_element(Element &e, const ImagePtr &img, int type, int flags, int offset, int size, int cols, int rows) {
    e.image = img.get();
    e.type = type;
    e.flags = flags;
    e.src_offset = offset;
    e.src_size = size;
    e.cols = cols;
    e.rows = rows;
    e.pad_x = e.pad_y = 0;
}

void init_elements(Element *el, const Config &dc, const Images &di) {
    int gflags = dc.GadgetsThreeState ? DEF_THREE_STATE : 0, gcols = dc.GadgetsThreeState ? 3 : 4;
    auto bar = [&](int id, int o, int s) { init_element(el[id], di.winbar_normal, DE_TYPE_TILED_TITLE, 0, o, s, 1, 2); };
    auto gad = [&](int id, const ImagePtr &img) { init_element(el[id], img, DE_TYPE_STATEFUL_GADGET, gflags | DEF_TITLE_GADGET, 0, 0, gcols, 1); };
    auto scrbar = [&](int id, int o, int s) {
        init_element(el[id], di.stitlebar, DE_TYPE_SCALED_TILED_H, 0, o, s, 1, 1);
        el[id].src_height = dc.SBarHeight;
    };
    auto subw = [&](const ImagePtr &img) { return img ? (dc.GadgetsThreeState ? img->w / 3 : img->w >> 2) : 0; };

    bar(WinBarPreGadget, dc.BarPreGadget_o, dc.BarPreGadget_s);
    bar(WinBarPre, dc.BarPre_o, dc.BarPre_s);
    bar(WinBarLGadgetFill, dc.BarLGadgetFill_o, dc.BarLGadgetFill_s);
    bar(WinBarJoinGB, dc.BarJoinGB_o, dc.BarJoinGB_s);
    bar(WinBarLFill, dc.BarLFill_o, dc.BarLFill_s);
    bar(WinBarJoinBT, dc.BarJoinBT_o, dc.BarJoinBT_s);
    bar(WinBarTitleFill, dc.BarTitleFill_o, dc.BarTitleFill_s);
    bar(WinBarJoinTB, dc.BarJoinTB_o, dc.BarJoinTB_s);
    bar(WinBarRFill, dc.BarRFill_o, dc.BarRFill_s);
    bar(WinBarJoinBG, dc.BarJoinBG_o, dc.BarJoinBG_s);
    bar(WinBarRGadgetFill, dc.BarRGadgetFill_o, dc.BarRGadgetFill_s);
    bar(WinBarPostGadget, dc.BarPostGadget_o, dc.BarPostGadget_s);
    bar(WinBarPost, dc.BarPost_o, dc.BarPost_s);

    gad(WinClose, di.close);
    gad(WinDepth, di.depth);
    gad(WinZoom, di.zoom);
    gad(WinMUI, di.mui);
    gad(WinPopup, di.popup);
    gad(WinSnapshot, di.snapshot);
    gad(WinIconify, di.iconify);
    gad(WinLock, di.lock);
    // the size gadget and the arrows: not title gadgets, centred in their border
    init_element(el[WinSize], di.size, DE_TYPE_STATEFUL_GADGET, gflags, 0, 0, gcols, 1);
    el[WinSize].pad_x = (dc.RightBorderGadgets - subw(di.size)) / 2;
    el[WinSize].pad_y = (dc.BottomBorderGadgets - (di.size ? di.size->h : 0)) / 2;
    init_element(el[WinUp], di.up, DE_TYPE_STATEFUL_GADGET, gflags, 0, 0, gcols, 1);
    el[WinUp].pad_x = (dc.RightBorderGadgets - subw(di.up)) / 2;
    el[WinUp].pad_y = dc.UpDownAddY / 2;
    init_element(el[WinDown], di.down, DE_TYPE_STATEFUL_GADGET, gflags, 0, 0, gcols, 1);
    el[WinDown].pad_x = (dc.RightBorderGadgets - subw(di.down)) / 2;
    el[WinDown].pad_y = dc.UpDownAddY / 2;
    init_element(el[WinLeft], di.left, DE_TYPE_STATEFUL_GADGET, gflags, 0, 0, gcols, 1);
    el[WinLeft].pad_x = dc.LeftRightAddX / 2;
    el[WinLeft].pad_y = (dc.BottomBorderGadgets - (di.left ? di.left->h : 0)) / 2;
    init_element(el[WinRight], di.right, DE_TYPE_STATEFUL_GADGET, gflags, 0, 0, gcols, 1);
    el[WinRight].pad_x = dc.LeftRightAddX / 2;
    el[WinRight].pad_y = (dc.BottomBorderGadgets - (di.right ? di.right->h : 0)) / 2;

    init_element(el[VContainerTop], di.verticalcontainer, DE_TYPE_TILED_VERTICAL, 0, dc.ContainerTop_o, dc.ContainerTop_s, 2, 1);
    init_element(el[VContainerTile], di.verticalcontainer, DE_TYPE_TILED_VERTICAL, 0, dc.ContainerVertTile_o, dc.ContainerVertTile_s, 2, 1);
    init_element(el[VContainerBottom], di.verticalcontainer, DE_TYPE_TILED_VERTICAL, 0, dc.ContainerBottom_o, dc.ContainerBottom_s, 2, 1);
    init_element(el[VKnobTop], di.verticalknob, DE_TYPE_TILED_VERTICAL, 0, dc.KnobTop_o, dc.KnobTop_s, 3, 1);
    init_element(el[VKnobTileTop], di.verticalknob, DE_TYPE_TILED_VERTICAL, 0, dc.KnobTileTop_o, dc.KnobTileTop_s, 3, 1);
    init_element(el[VKnobGripper], di.verticalknob, DE_TYPE_TILED_VERTICAL, DEF_HAS_GRIPPER, dc.KnobVertGripper_o, dc.KnobVertGripper_s, 3, 1);
    init_element(el[VKnobTileBottom], di.verticalknob, DE_TYPE_TILED_VERTICAL, 0, dc.KnobTileBottom_o, dc.KnobTileBottom_s, 3, 1);
    init_element(el[VKnobBottom], di.verticalknob, DE_TYPE_TILED_VERTICAL, 0, dc.KnobBottom_o, dc.KnobBottom_s, 3, 1);
    init_element(el[HContainerLeft], di.horizontalcontainer, DE_TYPE_TILED_HORIZONTAL, 0, dc.ContainerLeft_o, dc.ContainerLeft_s, 1, 2);
    init_element(el[HContainerTile], di.horizontalcontainer, DE_TYPE_TILED_HORIZONTAL, 0, dc.ContainerHorTile_o, dc.ContainerHorTile_s, 1, 2);
    init_element(el[HContainerRight], di.horizontalcontainer, DE_TYPE_TILED_HORIZONTAL, 0, dc.ContainerRight_o, dc.ContainerRight_s, 1, 2);
    init_element(el[HKnobLeft], di.horizontalknob, DE_TYPE_TILED_HORIZONTAL, 0, dc.KnobLeft_o, dc.KnobLeft_s, 1, 3);
    init_element(el[HKnobTileLeft], di.horizontalknob, DE_TYPE_TILED_HORIZONTAL, 0, dc.KnobTileLeft_o, dc.KnobTileLeft_s, 1, 3);
    init_element(el[HKnobGripper], di.horizontalknob, DE_TYPE_TILED_HORIZONTAL, DEF_HAS_GRIPPER, dc.KnobHorGripper_o, dc.KnobHorGripper_s, 1, 3);
    init_element(el[HKnobTileRight], di.horizontalknob, DE_TYPE_TILED_HORIZONTAL, 0, dc.KnobTileRight_o, dc.KnobTileRight_s, 1, 3);
    init_element(el[HKnobRight], di.horizontalknob, DE_TYPE_TILED_HORIZONTAL, 0, dc.KnobRight_o, dc.KnobRight_s, 1, 3);

    {   // the screen bar's fill: up to the first part that is not the fill
        int filllen = 1;
        if (di.stitlebar) {
            if (dc.SBarChildPre_s > 0 && dc.SBarChildPre_s < di.stitlebar->w) filllen = dc.SBarChildPre_o;
            else if (dc.SBarGadPre_s > 0 && dc.SBarGadPre_s < di.stitlebar->w) filllen = dc.SBarGadPre_o;
            else filllen = di.stitlebar->w;
        }
        if (filllen == 0) filllen = 1;
        scrbar(ScrBarFill, 0, filllen);
    }
    scrbar(ScrBarGadPre, dc.SBarGadPre_o, dc.SBarGadPre_s);
    scrbar(ScrBarGadFill, dc.SBarGadFill_o, dc.SBarGadFill_s);
    scrbar(ScrBarGadPost, dc.SBarGadPost_o, dc.SBarGadPost_s);
    scrbar(ScrBarChildPre, dc.SBarChildPre_o, dc.SBarChildPre_s);
    scrbar(ScrBarChildFill, dc.SBarChildFill_o, dc.SBarChildFill_s);
    scrbar(ScrBarChildPost, dc.SBarChildPost_o, dc.SBarChildPost_s);
    init_element(el[ScrDepth], di.sdepth, DE_TYPE_STATEFUL_GADGET, DEF_SCALABLE, 0, 0, 2, 1);
    init_element(el[ScrBarLogo], di.sbarlogo, DE_TYPE_TILED_HORIZONTAL, 0, 0, di.sbarlogo ? di.sbarlogo->w : 0, 1, 1);

    init_element(el[MenuBackground], di.menu, DE_TYPE_TILED_BOTH, 0, 0, 0, 1, 1);
    init_element(el[MenuKey], di.menukey, DE_TYPE_STATEFUL_GADGET, 0, 0, 0, 1, 1);
    init_element(el[MenuCheck], di.menucheck, DE_TYPE_STATEFUL_GADGET, 0, 0, 0, 1, 1);
    init_element(el[MenuSubMenu], di.submenu, DE_TYPE_STATEFUL_GADGET, 0, 0, 0, 1, 1);

    init_element(el[WinBorderNormal], di.border_normal, DE_TYPE_TILED_BOTH, 0, 0, 0, 1, 1);
    init_element(el[WinBorderDeactivated], di.border_deactivated, DE_TYPE_TILED_BOTH, 0, 0, 0, 1, 1);
    init_element(el[WinBarNormal], di.winbar_normal, DE_TYPE_TILED_TITLE, 0, 0, 0, 1, 2);
}
}  // namespace

std::unique_ptr<Theme> load_theme(const std::string &dir) {
    auto t = std::make_unique<Theme>();
    t->dc.ThemePath = dir;
    load_menu_config(dir, t->dc);
    bool sys = load_system_config(dir, t->dc);
    t->di = load_images(t->dc);
    if (!sys && !t->di.winbar_normal) return nullptr;
    init_elements(t->el, t->dc, t->di);
    return t;
}

// ============================================================================================
// The drawing surface
// ============================================================================================
void RastPort::write(const uint32_t *src, int sx, int sy, int stride, int dx, int dy, int ww, int hh) {
    for (int j = 0; j < hh; j++) {
        int y = dy + j;
        if (y < 0 || y >= h) continue;
        const uint32_t *s = src + (size_t)(sy + j) * stride + sx;
        uint32_t *d = &px[(size_t)y * w];
        for (int i = 0; i < ww; i++) {
            int x = dx + i;
            if (x >= 0 && x < w) d[x] = s[i] | 0xFF000000u;
        }
    }
}

void RastPort::write_alpha(const uint32_t *src, int sx, int sy, int stride, int dx, int dy, int ww, int hh) {
    for (int j = 0; j < hh; j++) {
        int y = dy + j;
        if (y < 0 || y >= h) continue;
        const uint32_t *s = src + (size_t)(sy + j) * stride + sx;
        uint32_t *d = &px[(size_t)y * w];
        for (int i = 0; i < ww; i++) {
            int x = dx + i;
            if (x < 0 || x >= w) continue;
            uint32_t c = s[i];
            int a = A(c);
            if (a == 255) d[x] = c;
            else if (a) {
                uint32_t o = d[x];
                d[x] = ARGB(255, (R(c) * a + R(o) * (255 - a)) / 255, (G(c) * a + G(o) * (255 - a)) / 255,
                            (B(c) * a + B(o) * (255 - a)) / 255);
            }
        }
    }
}

void RastPort::fill(int x, int y, int ww, int hh, uint32_t rgb) {
    for (int j = std::max(0, y); j < std::min(h, y + hh); j++)
        for (int i = std::max(0, x); i < std::min(w, x + ww); i++) px[(size_t)j * w + i] = rgb | 0xFF000000u;
}

void RastPort::line(int x0, int y0, int x1, int y1, uint32_t rgb) {
    // (the decorator draws only straight lines)
    if (x0 == x1) { if (y1 < y0) std::swap(y0, y1); fill(x0, y0, 1, y1 - y0 + 1, rgb); }
    else if (y0 == y1) { if (x1 < x0) std::swap(x0, x1); fill(x0, y0, x1 - x0 + 1, 1, rgb); }
    else {
        int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            fill(x0, y0, 1, 1, rgb);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

// ============================================================================================
// decorimagefuncs.c
// ============================================================================================
namespace {
// ScaleBuffer (from BM__Hidd_BitMap__BitMapScale): samples, no interpolation
std::vector<uint32_t> scale_buffer(const uint32_t *src, int widthBuffer, int widthSrc, int heightSrc, int widthDest, int heightDest) {
    std::vector<uint32_t> out((size_t)widthDest * heightDest);
    std::vector<int> linepattern(widthDest);
    unsigned ys = 0, xs = 0;
    long dyd = heightDest, dxd = widthDest, accuys = dyd, accuxs = dxd, dxs = widthSrc, dys = heightSrc;
    long accuyd = -(dys >> 1), accuxd = -(dxs >> 1);
    for (int count = 0; count < widthDest; count++) {
        accuxd += dxs;
        while (accuxd > accuxs) { xs++; accuxs += dxd; }
        linepattern[count] = (int)xs;
    }
    long srcline = -1;
    std::vector<uint32_t> last(widthDest);
    for (int count = 0; count < heightDest; count++) {
        accuyd += dys;
        while (accuyd > accuys) { ys++; accuys += dyd; }
        if (srcline != (long)ys) {
            const uint32_t *s = src + (size_t)ys * widthBuffer;
            srcline = ys;
            for (int x = 0; x < widthDest; x++) last[x] = widthSrc == 1 ? s[0] : widthSrc == widthDest ? s[x] : s[linepattern[x]];
        }
        std::copy(last.begin(), last.end(), out.begin() + (size_t)count * widthDest);
    }
    return out;
}
}  // namespace

std::shared_ptr<Image> scale_image(const Image &src, int neww, int newh) {
    auto di = std::make_shared<Image>();
    di->w = neww;
    di->h = newh;
    di->cols = src.cols;
    di->rows = src.rows;
    di->data = scale_buffer(src.data.data(), src.w, src.w, src.h, neww, newh);
    di->ok = true;
    return di;
}

// ============================================================================================
// drawfuncs.c
// ============================================================================================
namespace {
// BltScaleDecorImageSubImageRastPort: a subimage (or a part of it) blended into rp, scaled
// when the sizes differ
void blt_scale_subimage(const Image &di, int col, int row, int xSrc, int ySrc, RastPort &rp, int xDest, int yDest,
                        int widthSrc, int heightSrc, int widthDest, int heightDest) {
    int subw = di.w / di.cols, subh = di.h / di.rows;
    if (col >= di.cols || row >= di.rows) return;
    if (widthSrc < 0) widthSrc = subw;
    if (heightSrc < 0) heightSrc = subh;
    if (widthDest < 0) widthDest = widthSrc;
    if (heightDest < 0) heightDest = heightSrc;
    if (widthSrc <= 0 || heightSrc <= 0 || widthDest <= 0 || heightDest <= 0) return;
    int x0 = subw * col + xSrc, y0 = subh * row + ySrc;
    if (x0 < 0 || y0 < 0 || x0 + widthSrc > di.w || y0 + heightSrc > di.h) {
        // (beyond the picture: the original reads on; here it is cut to the picture)
        widthSrc = std::min(widthSrc, di.w - x0);
        heightSrc = std::min(heightSrc, di.h - y0);
        if (x0 < 0 || y0 < 0 || widthSrc <= 0 || heightSrc <= 0) return;
    }
    if (widthSrc != widthDest || heightSrc != heightDest) {
        auto scaled = scale_buffer(di.data.data() + (size_t)y0 * di.w + x0, di.w, widthSrc, heightSrc, widthDest, heightDest);
        rp.write_alpha(scaled.data(), 0, 0, widthDest, xDest, yDest, widthDest, heightDest);
    } else
        rp.write_alpha(di.data.data(), x0, y0, di.w, xDest, yDest, widthSrc, heightSrc);
}

void horiz_vert_repeat(const Image *di, uint32_t color, int offx, int offy, RastPort &rp, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (!di || !di->ok) { rp.fill(x, y, w, h, color); return; }
    int ow = di->w, oh = di->h;
    int sy = offy % oh, dh = oh - sy, height = h, dy = y;
    while (height > 0) {
        if (height - dh < 0) dh = height;
        height -= dh;
        int sx = offx % ow, dw = ow - sx, width = w, dx = x;
        while (width > 0) {
            if (width - dw < 0) dw = width;
            width -= dw;
            blt_scale_subimage(*di, 0, 0, sx, sy, rp, dx, dy, dw, dh, -1, -1);
            dx += dw;
            sx = 0;
            dw = ow;
        }
        dy += dh;
        sy = 0;
        dh = oh;
    }
}

int write_tiled_title(int clipw, RastPort &rp, const Image &di, int sx, int sy, int sw, int /*sh*/, int xp, int yp, int dw, int dh) {
    int w = dw, x = xp;
    if (!di.ok) return x;
    if (sw == 0 || dw == 0) return xp;
    if (clipw > 0) {
        if (x > clipw) return xp;
        if (x + w > clipw) w = clipw - x;
    }
    while (w > 0) {
        int ddw = std::min(sw, w);
        blt_scale_subimage(di, 0, 0, sx, sy, rp, x, yp, ddw, -1, -1, dh);
        w -= ddw;
        x += ddw;
    }
    return x;
}

int write_vscaled_tiled_h(RastPort &rp, const Image &di, int subimage, int sx, int sw, int xp, int yp, int sh, int dw, int dh) {
    int w = dw, x = xp;
    if (!di.ok) return xp;
    if (sw == 0 || dw == 0) return xp;
    while (w > 0) {
        int ddw = std::min(sw, w);
        blt_scale_subimage(di, 0, subimage, sx, 0, rp, x, yp, ddw, sh, -1, dh);
        w -= ddw;
        x += ddw;
    }
    return x;
}

int write_tiled_v(RastPort &rp, const Image &di, int subimage, int sy, int sh, int xp, int yp, int dh) {
    int h = dh, y = yp;
    if (!di.ok) return y;
    if (sh == 0 || dh == 0) return yp;
    while (h > 0) {
        int ddh = std::min(sh, h);
        blt_scale_subimage(di, subimage, 0, 0, sy, rp, xp, y, -1, ddh, -1, -1);
        h -= ddh;
        y += ddh;
    }
    return y;
}

// FillMemoryBufferRGBGradient: an RGB gradient at an angle over the rectangle (xt,yt)-(xb,yb),
// the part (xp,yp) w x h of it into buf
void gradient_buffer(uint8_t *buf, int xt, int yt, int xb, int yb, int xp, int yp, int w, int h, uint32_t start_rgb,
                     uint32_t end_rgb, int angle) {
    double rad = angle * M_PI / 180;
    int vx = (int)(-cos(rad) * 0xff), vy = (int)(sin(rad) * 0xff);
    int width = xb - xt + 1, height = yb - yt + 1;
    int sr = R(start_rgb), sg = G(start_rgb), sb = B(start_rgb);
    int diffR = R(end_rgb) - sr, diffG = G(end_rgb) - sg, diffB = B(end_rgb) - sb;
    int xs, ys, xw, yw, xadd, ystart, yadd;
    if (angle < 0) angle = 360 - ((-angle) % 360);
    if (angle >= 0) angle = angle % 360;
    if (angle <= 90 || (angle > 180 && angle <= 270)) { xs = 0; ys = 0; xw = width; yw = height; }
    else { xs = 0; ys = height; xw = width; yw = -height; }
    if (angle > 90 && angle <= 270) { xadd = -1; yadd = -1; ystart = height; xs = -xs; ys = -ys + height; }
    else { xadd = 1; yadd = 1; ystart = 0; }
    int r = -vy * (yw * xs - xw * ys), t = -yw * vx + xw * vy;
    if (t == 0) t = 1;
    int incr_y1 = yw * vy * xadd;
    uint8_t *p = buf;
    int y = ystart + (yp - yt) * yadd;
    for (int l = 0; l < h; l++, y += yadd) {
        int acc = r - yw * vx * y;
        for (int c = 0; c < w; c++) {
            int y1 = acc / t;
            *p++ = (uint8_t)(sr + diffR * y1 / height);
            *p++ = (uint8_t)(sg + diffG * y1 / height);
            *p++ = (uint8_t)(sb + diffB * y1 / height);
            acc += incr_y1;
        }
    }
    (void)xp;
}

// HorizRepeatBuffer: each row the colour of the column buffer's row offy + y
void horiz_repeat_buffer(const uint8_t *buf, int buflen, int offy, RastPort &rp, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    for (int yi = 0; yi < h; yi++) {
        int i = std::clamp(offy + yi, 0, std::max(0, buflen - 1)) * 3;
        rp.fill(x, y + yi, w, 1, ARGB(255, buf[i], buf[i + 1], buf[i + 2]));
    }
}

// ShadeLine: a line of the border shaded (gradients: the base colour shaded; a picture: its
// pixels shaded)
void shade_line(bool usegradients, RastPort &rp, const Image *di, uint32_t basecolor, int fact, int x0, int y0, int x1, int y1,
                uint32_t pen) {
    if (x1 < x0 || y1 < y0) return;
    if (usegradients) rp.line(x0, y0, x1, y1, calc_shade(basecolor, fact));
    else if (di && di->ok) {
        for (int py = y0; py <= y1; py++)
            for (int px = x0; px <= x1; px++)
                rp.fill(px, py, 1, 1, calc_shade(di->data[(size_t)(py % di->h) * di->w + (px % di->w)], fact));
    } else rp.line(x0, y0, x1, y1, pen);
}

void draw_scaled_stateful(RastPort &rp, const Image &di, int state, int xp, int yp, int sw, int sh) {
    if (!di.ok) return;
    int col = state == IDS_SELECTED ? 1 : state == IDS_INACTIVENORMAL ? 2 : 0;
    blt_scale_subimage(di, col, 0, 0, 0, rp, xp, yp, -1, -1, sw, sh);
}
}  // namespace

uint32_t calc_shade(uint32_t base, int fact) {
    int c0 = std::min(255, A(base) * fact >> 8), c1 = std::min(255, R(base) * fact >> 8), c2 = std::min(255, G(base) * fact >> 8),
        c3 = std::min(255, B(base) * fact >> 8);
    return ARGB(c0, c1, c2, c3);
}

void fill_gradient(RastPort &rp, int xt, int yt, int xb, int yb, int xp, int yp, int w, int h, uint32_t s, uint32_t e, int angle, int dy) {
    if (w <= 0 || h <= 0 || yb <= 0) return;
    std::vector<uint8_t> buf((size_t)yb * 3);
    gradient_buffer(buf.data(), xt, yt, xb, yb, xp, yp, 1, yb, s, e, angle);
    horiz_repeat_buffer(buf.data(), yb, dy, rp, xp, yp, w, h);
}

// ============================================================================================
// decoratorelement.c
// ============================================================================================
int render_element(const Element &e, RastPort &rp, int subimage, int xp, int yp, int dw, int dh, int clipw) {
    const Image *di = e.image;
    if (!di || !di->ok) return e.type == DE_TYPE_TILED_VERTICAL ? yp : xp;
    xp += e.pad_x;
    yp += e.pad_y;
    switch (e.type) {
    case DE_TYPE_STATEFUL_GADGET: draw_scaled_stateful(rp, *di, subimage, xp, yp, dw, dh); return xp;
    case DE_TYPE_TILED_HORIZONTAL: return write_vscaled_tiled_h(rp, *di, subimage, e.src_offset, e.src_size, xp, yp, -1, dw, -1);
    case DE_TYPE_TILED_VERTICAL: return write_tiled_v(rp, *di, subimage, e.src_offset, e.src_size, xp, yp, dh);
    case DE_TYPE_SCALED_TILED_H: {
        int sh = e.src_height > 0 ? e.src_height : di->h / (e.rows ? e.rows : 1);
        return write_vscaled_tiled_h(rp, *di, subimage, e.src_offset, e.src_size, xp, yp, sh, dw, dh);
    }
    case DE_TYPE_TILED_TITLE: {
        int sh = di->h / (e.rows ? e.rows : 1);
        return write_tiled_title(clipw, rp, *di, e.src_offset, subimage * sh, e.src_size, sh, xp, yp, dw, dh);
    }
    case DE_TYPE_TILED_BOTH: horiz_vert_repeat(di, 0, 0, 0, rp, xp, yp, dw, dh); return xp;
    case DE_TYPE_IMAGE: rp.write(di->data.data(), 0, 0, di->w, xp, yp, di->w, di->h); return xp;
    }
    return xp;
}

// ============================================================================================
// windowdecorclass.c
// ============================================================================================
namespace {
int gadget_width(const Config &dc, const ImagePtr &img) {
    if (!img || !img->ok) return 0;
    return dc.GadgetsThreeState ? img->w / 3 : img->w >> 2;
}
}  // namespace

WindowFrame window_frame(const Theme &t, int font_height, int W, int H, bool has_close, bool has_depth, bool has_zoom,
                         bool has_size) {
    const Config &dc = t.dc;
    WindowFrame f;
    // scrdecor_initscreen and openwindow.c: BorderTop = WBorTop + the font's height + 1, the
    // bar's height when the font fits in it
    int wbortop = font_height + 2 > dc.BarHeight ? 1 : dc.BarHeight - 1 - font_height;
    f.border_top = wbortop + font_height + 1;
    f.border_left = dc.LeftBorder;
    f.border_right = dc.RightBorder;
    f.border_bottom = dc.BottomBorder;
    // the size gadget (SIZEBRIGHT unless asked otherwise): the right border at least as wide
    int sizew = dc.RightBorderGadgets > 0 ? dc.RightBorderGadgets : 16, sizeh = dc.BottomBorderGadgets > 0 ? dc.BottomBorderGadgets : 16;
    if (has_size) f.border_right = std::max(f.border_right, sizew);
    // the title gadgets: their pictures' size (taller with a font taller than the bar)
    auto title_gadget = [&](const ImagePtr &img, Box &b) {
        b.w = gadget_width(dc, img);
        b.h = img && img->ok ? img->h : f.border_top;
        if (img && img->ok && font_height + 2 > dc.BarHeight && dc.BarHeight > 0) b.h = b.h * (font_height + 2) / dc.BarHeight;
        b.y = (f.border_top - b.h) / 2;
        b.on = b.w > 0;
    };
    int cw = 0;
    if (has_close) {
        title_gadget(t.di.close, f.close);
        cw = f.close.w;
        f.close.x = dc.CloseGadgetOnRight ? W - dc.BarPostGadget_s - cw : dc.BarPreGadget_s;
    }
    int right = dc.BarPostGadget_s + (has_close && dc.CloseGadgetOnRight ? cw : 0);
    if (has_depth) {
        title_gadget(t.di.depth, f.depth);
        right += f.depth.w;
        f.depth.x = W - right;
    }
    if (has_zoom) {
        title_gadget(t.di.zoom, f.zoom);
        right += f.zoom.w;
        f.zoom.x = W - right;
    }
    if (has_size) {
        f.size.w = sizew;
        f.size.h = sizeh;
        f.size.x = W - sizew;
        f.size.y = H - sizeh;
        f.size.on = true;
    }
    return f;
}

namespace {
// getleftgadgetsdimensions, getrightgadgetsdimensions and the room the title bar's fixed
// parts take (defwidth)
struct BarLayout { int xl0 = 0, xl1 = 0, xr0 = 0, xr1 = 0, defwidth = 0; };
BarLayout bar_layout(const Config &dc, const WindowFrame &f, int W) {
    BarLayout b;
    b.xr0 = 1000000;
    if (f.close.on && !dc.CloseGadgetOnRight) { b.xl0 = f.close.x; b.xl1 = f.close.x + f.close.w; }
    for (const Box *g : {&f.depth, &f.zoom, &f.close}) {
        if (!g->on || g->w <= 0) continue;
        if (g == &f.close && !dc.CloseGadgetOnRight) continue;
        b.xr0 = std::min(b.xr0, g->x);
        b.xr1 = std::max(b.xr1, g->x + g->w);
    }
    if (b.xr0 == 1000000) b.xr0 = 0;
    b.defwidth = b.xl0 != b.xl1 ? dc.BarPreGadget_s : dc.BarPre_s;
    if (b.xr1 == 0) b.xr1 = b.xr0 = W - dc.BarPre_s;
    b.defwidth += b.xl1 - b.xl0;
    b.defwidth += dc.BarJoinGB_s + dc.BarJoinBT_s + dc.BarJoinTB_s + dc.BarJoinBG_s;
    b.defwidth += b.xr1 - b.xr0;
    b.defwidth += b.xr0 != b.xr1 ? dc.BarPostGadget_s : dc.BarPost_s;
    return b;
}
}  // namespace

void draw_window(Theme &t, RastPort &rp, const WindowFrame &f, bool active, const GadgetStates &gs, bool has_title,
                 int font_ascent, int font_height, int (*fit)(void *, int), void *fit_data, int &title_x,
                 int &title_baseline, int &title_width) {
    Config &dc = t.dc;
    const Images &di = t.di;
    int W = rp.w, H = rp.h, bt = f.border_top, bl = f.border_left, br = f.border_right, bb = f.border_bottom;
    const Image *border = di.border_normal && di.border_normal->ok ? di.border_normal.get() : nullptr;
    if (!border) dc.UseGradients = true;
    uint32_t s_col, e_col, bc;
    int arc;
    if (active) {
        s_col = dc.ActivatedGradientColor_s; e_col = dc.ActivatedGradientColor_e; arc = dc.ActivatedGradientColor_a;
        bc = dc.BaseColors_a;
    } else {
        s_col = dc.DeactivatedGradientColor_s; e_col = dc.DeactivatedGradientColor_e; arc = dc.DeactivatedGradientColor_a;
        bc = dc.BaseColors_d;
        if (!dc.UseGradients && di.border_deactivated && di.border_deactivated->ok) border = di.border_deactivated.get();
    }
    const uint32_t color = 0x00cccccc;

    // ---- DrawPartialTitleBar
    title_x = -1;
    title_width = 0;
    if (dc.FillTitleBar) {
        if (dc.UseGradients) fill_gradient(rp, 0, 0, W - 1, H - 1, 0, 0, W, bt, s_col, e_col, arc, 0);
        else horiz_vert_repeat(border, color, 0, 0, rp, 0, 0, W, bt);
    }
    // the gadgets on the left (the close gadget) and on the right
    BarLayout bl_ = bar_layout(dc, f, W);
    int xl0 = bl_.xl0, xl1 = bl_.xl1, xr0 = bl_.xr0, xr1 = bl_.xr1, defwidth = bl_.defwidth;
    bool hastitle = has_title && defwidth < W;
    int textpixellen = 0;
    if (hastitle) textpixellen = fit ? fit(fit_data, W - defwidth) : 0;
    if (textpixellen <= 0) hastitle = false;

    if (di.winbar_normal && di.winbar_normal->ok && bt > 0) {
        int x = 0, barh = bt, act = active ? 0 : 1;
        std::shared_ptr<Image> scaled;
        Image *bar = di.winbar_normal.get();
        if (dc.BarHeight != barh) { scaled = scale_image(*bar, bar->w, 2 * barh); bar = scaled.get(); }
        auto el = [&](int id, int dw) {
            Element e = t.el[id];
            e.image = bar;
            return render_element(e, rp, act, x, 0, dw, barh, W);
        };
        if (xl0 != xl1) {
            x = el(WinBarPreGadget, dc.BarPreGadget_s);
            if (xl1 - xl0 > 0) x = el(WinBarLGadgetFill, xl1 - xl0);
        } else
            x = el(WinBarPre, dc.BarPreGadget_s);
        x = el(WinBarJoinGB, dc.BarJoinGB_s);
        int textstart = 0;
        if (hastitle) {
            // (the title on the left: TextAlign is WD_DWTA_LEFT)
            x = el(WinBarJoinBT, dc.BarJoinBT_s);
            textstart = x;
            if (textpixellen > 0) x = el(WinBarTitleFill, textpixellen);
            x = el(WinBarJoinTB, dc.BarJoinTB_s);
        }
        x = el(WinBarRFill, xr0 - x - dc.BarJoinBG_s);
        x = el(WinBarJoinBG, dc.BarJoinBG_s);
        if (xr1 - xr0 > 0) x = el(WinBarRGadgetFill, xr1 - xr0);
        if (xr0 != xr1) x = el(WinBarPostGadget, dc.BarPostGadget_s);
        else x = el(WinBarPost, dc.BarPost_s);
        if (hastitle) title_x = textstart;
    } else if (hastitle)
        title_x = dc.BarPreGadget_s + (xl1 - xl0) + dc.BarJoinGB_s + dc.BarJoinBT_s;
    if (title_x >= 0) {
        int tymax = bt - (font_height - font_ascent) - 1, ty = (bt + font_ascent - 1) >> 1;
        title_baseline = std::min(ty, tymax);
        title_width = textpixellen;
    }

    // ---- the left, right and bottom borders
    int overlap = dc.WinFrameStyle > 0 ? 2 : 0;
    if (dc.UseGradients) {
        std::vector<uint8_t> buf((size_t)H * 3);
        gradient_buffer(buf.data(), 0, 0, W - 1, H - 1, 0, 0, 1, H, s_col, e_col, arc);
        if (bl > overlap) horiz_repeat_buffer(buf.data(), H, bt, rp, 0, bt, bl, H - bt);
        if (br > overlap) horiz_repeat_buffer(buf.data(), H, bt, rp, W - br, bt, br, H - bt);
        if (bb > overlap) horiz_repeat_buffer(buf.data(), H, H - bb, rp, 0, H - bb, W, bb);
    } else {
        if (bl > overlap) horiz_vert_repeat(border, color, 0, bt, rp, 0, bt, bl - 1, H - bt);
        if (br > overlap) horiz_vert_repeat(border, color, W - br, bt, rp, W - br, bt, br, H - bt);
        if (bb > overlap) horiz_vert_repeat(border, color, 0, H - bb, rp, 0, H - bb, W, bb);
    }
    // the shading of the borders
    if (dc.WinFrameStyle > 0) {
        const uint32_t dpen = 0x000000, lpen = 0xFFFFFF, mpen = 0xFFFFFF;   // SHADOWPEN, SHINEPEN
        bool g = dc.UseGradients;
        int ww = W, wh = H, bbt = bt;
        auto sl = [&](uint32_t pen, int fact, int x0, int y0, int x1, int y1) { shade_line(g, rp, border, bc, fact, x0, y0, x1, y1, pen); };
        if (bl > 0) sl(dpen, dc.ShadeValues_d, 0, bbt, 0, wh - 1);
        if (bb > 0) sl(dpen, dc.ShadeValues_d, 0, wh - 1, ww - 1, wh - 1);
        if (br > 0) sl(dpen, dc.ShadeValues_d, ww - 1, bbt, ww - 1, wh - 1);
        if (bl > 1) sl(dpen, dc.ShadeValues_d, bl - 1, bbt, bl - 1, wh - bb);
        if (bb > 1) sl(dpen, dc.ShadeValues_d, bl - 1, wh - bb, ww - br, wh - bb);
        if (br > 1) sl(dpen, dc.ShadeValues_d, ww - br, bbt, ww - br, wh - bb);
        if (bl > 2) sl(lpen, dc.ShadeValues_l, 1, bbt, 1, wh - 2);
        if (bl > 3) {
            if (bb > 1) sl(mpen, dc.ShadeValues_m, bl - 2, bbt, bl - 2, wh - bb + 1);
            else sl(mpen, dc.ShadeValues_m, bl - 2, bbt, bl - 2, wh - bb);
        }
        if (br > 2) sl(mpen, dc.ShadeValues_m, ww - 2, bbt, ww - 2, wh - 2);
        if (bb > 2) sl(mpen, dc.ShadeValues_m, 1, wh - 2, ww - 2, wh - 2);
        if (bb > 3 && bl > 0 && br > 0) sl(lpen, dc.ShadeValues_l, bl, wh - bb + 1, ww - br, wh - bb + 1);
        if (br > 3 && bb > 1) sl(lpen, dc.ShadeValues_l, ww - br + 1, bbt, ww - br + 1, wh - bb + 1);
    }

    // ---- windecor_draw_sysimage: the gadgets (title gadgets over the title bar, at their
    // gadget's height; the size gadget at its own size, centred in the corner)
    auto gadget = [&](int id, const Box &b, int state, bool titlegadget) {
        if (!b.on) return;
        const Element &e = t.el[id];
        if (!e.image || !e.image->ok) return;
        if (titlegadget) render_element(e, rp, state, b.x, b.y, -1, b.h, 0);
        else render_element(e, rp, state, b.x, b.y, -1, -1, 0);
    };
    gadget(WinClose, f.close, gs.close, true);
    gadget(WinZoom, f.zoom, gs.zoom, true);
    gadget(WinDepth, f.depth, gs.depth, true);
    gadget(WinSize, f.size, gs.size, false);
}

// windecor_windowshape: the window's shape - the title bar where its picture is not
// transparent (BarMasking), or corners rounded (BarRounded); empty: no shape
std::vector<Rect> window_shape(Theme &t, const WindowFrame &f, int W, int H, bool active, bool has_title,
                               int (*fit)(void *, int), void *fit_data) {
    const Config &dc = t.dc;
    std::vector<Rect> out;
    int bt = f.border_top;
    if (dc.BarMasking) {
        // DrawShapePartialTitleBar: the title bar's parts' alpha into a W x BorderTop map
        std::vector<uint8_t> lut((size_t)W * std::max(0, bt), 0);
        const Image *src = t.di.winbar_normal.get();
        if (src && src->ok && bt > 0) {
            std::shared_ptr<Image> scaled;
            if (dc.BarHeight != bt) { scaled = scale_image(*src, src->w, 2 * bt); src = scaled.get(); }
            BarLayout b = bar_layout(dc, f, W);
            bool hastitle = has_title && b.defwidth < W;
            int textpixellen = hastitle && fit ? fit(fit_data, W - b.defwidth) : 0;
            int dy = active ? 0 : bt, x = 0;
            auto part = [&](int sx, int sw, int dw) {
                // WriteTiledImageShape
                int w = dw;
                if (!src->ok || sw == 0 || dw == 0) return;
                if (x > W) return;
                if (x + w > W) w = W - x;
                while (w > 0) {
                    int ddw = std::min(sw, w);
                    for (int yy = 0; yy < bt; yy++)
                        for (int xx = 0; xx < ddw; xx++) {
                            int px = sx + xx, py = dy + yy;
                            if (px >= 0 && px < src->w && py >= 0 && py < src->h && x + xx >= 0 && x + xx < W)
                                lut[(size_t)yy * W + x + xx] = (uint8_t)A(src->data[(size_t)py * src->w + px]);
                        }
                    w -= ddw;
                    x += ddw;
                }
            };
            if (b.xl0 != b.xl1) {
                part(dc.BarPreGadget_o, dc.BarPreGadget_s, dc.BarPreGadget_s);
                if (b.xl1 - b.xl0 > 0) part(dc.BarLGadgetFill_o, dc.BarLGadgetFill_s, b.xl1 - b.xl0);
            } else
                part(dc.BarPre_o, dc.BarPre_s, dc.BarPreGadget_s);
            part(dc.BarJoinGB_o, dc.BarJoinGB_s, dc.BarJoinGB_s);
            if (hastitle && textpixellen > 0) {
                part(dc.BarJoinBT_o, dc.BarJoinBT_s, dc.BarJoinBT_s);
                part(dc.BarTitleFill_o, dc.BarTitleFill_s, textpixellen);
                part(dc.BarJoinTB_o, dc.BarJoinTB_s, dc.BarJoinTB_s);
            }
            part(dc.BarRFill_o, dc.BarRFill_s, b.xr0 - x - dc.BarJoinBG_s);
            part(dc.BarJoinBG_o, dc.BarJoinBG_s, dc.BarJoinBG_s);
            if (b.xr1 - b.xr0 > 0) part(dc.BarRGadgetFill_o, dc.BarRGadgetFill_s, b.xr1 - b.xr0);
            if (b.xr0 != b.xr1) part(dc.BarPostGadget_o, dc.BarPostGadget_s, dc.BarPostGadget_s);
            else part(dc.BarPost_o, dc.BarPost_s, dc.BarPost_s);
        }
        // RegionFromLUT8Image: below the bar all of it, in the bar the runs not transparent
        out.push_back({0, bt, W, H - bt});
        for (int y = 0; y < bt; y++) {
            int start = -1;
            for (int x = 0; x <= W; x++) {
                bool opaque = x < W && lut[(size_t)y * W + x] != 0;
                if (opaque && start < 0) start = x;
                if (!opaque && start >= 0) { out.push_back({start, y, x - start, 1}); start = -1; }
            }
        }
        return out;
    }
    if (!dc.BarRounded) return out;
    // the corners rounded: rows cut in by 9, 6, 4, 3, 2, 1, 0 pixels
    int x2 = W - 1, y2 = H - 1;
    const int cut[][2] = {{9, 0}, {6, 1}, {4, 2}, {3, 3}, {2, 4}, {1, 6}, {0, 9}};
    for (auto &c : cut) out.push_back({c[0], c[1], x2 - 2 * c[0] + 1, y2 - c[1] + 1});
    return out;
}

// ============================================================================================
// screendecorclass.c
// ============================================================================================
ScreenBar screen_bar(const Theme &t, int font_height, int SW) {
    const Config &dc = t.dc;
    ScreenBar b;
    // scrdecor_initscreen: the bar at least as high as the font
    b.bar_height = font_height > dc.SBarHeight - 1 ? font_height : dc.SBarHeight - 1;
    // the depth gadget: half its picture wide, at the right
    const ImagePtr &img = t.di.sdepth;
    b.depth.w = img && img->ok ? img->w >> 1 : 23;
    b.depth.h = b.bar_height + 1;
    b.depth.x = SW - (b.depth.w + dc.SBarGadPost_s);
    b.depth.y = img && img->ok && b.depth.h == img->h ? (dc.SBarHeight - img->h) >> 1 : 0;
    if (b.depth.y < 0) b.depth.y = 0;
    b.depth.on = true;
    // scr_findtitlearea
    int maxx = b.depth.x - 2;
    if (maxx != SW - 1) maxx -= dc.SBarGadPre_s + dc.SBarGadPost_s;
    b.title_right = maxx;
    return b;
}

void draw_screen_bar(Theme &t, RastPort &rp, const ScreenBar &b, int depth_state, int font_ascent, int font_height,
                     int &title_x, int &title_baseline) {
    const Config &dc = t.dc;
    const Images &di = t.di;
    int SW = rp.w, right = b.title_right, barh = b.bar_height + 1;
    if (di.stitlebar && di.stitlebar->ok) {
        if (dc.SBarGadPre_s > 0) render_element(t.el[ScrBarGadPre], rp, 0, right + 1, 0, dc.SBarGadPre_o, barh, 0);
        if (dc.SBarGadFill_s > 0)
            render_element(t.el[ScrBarGadFill], rp, 0, right + dc.SBarGadPre_s + 1, 0, SW - (right + dc.SBarGadPre_s + dc.SBarGadPost_s), barh, 0);
        if (dc.SBarGadPost_s > 0) render_element(t.el[ScrBarGadPost], rp, 0, SW - dc.SBarGadPost_s, 0, dc.SBarGadPost_s, barh, 0);
        render_element(t.el[ScrBarFill], rp, 0, 0, 0, right + 1, barh, 0);
    }
    if (di.sbarlogo && di.sbarlogo->ok)
        render_element(t.el[ScrBarLogo], rp, 0, dc.SLogoOffset, (barh - di.sbarlogo->h) / 2, di.sbarlogo->w, di.sbarlogo->h, 0);
    // the screen's title
    title_x = dc.STitleOffset;
    int tymax = b.bar_height - (font_height - font_ascent) - 1, ty = (b.bar_height + font_ascent - 1) >> 1;
    title_baseline = std::min(ty, tymax);
    // the depth gadget
    if (di.sdepth && di.sdepth->ok)
        render_element(t.el[ScrDepth], rp, depth_state, b.depth.x, b.depth.y, b.depth.w, di.sdepth->h == b.depth.h ? b.depth.h : b.depth.h, 0);
}

}  // namespace decor
