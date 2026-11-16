/*
    Copyright (C) 2011-2026, The AROS Development Team. All rights reserved.
    Port to X11: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The decoration themes (workbench/libs/decortheme, workbench/libs/decorator and the
    decorator classes of workbench/c/Decoration) for the window manager:

      - Config: System/Config and Menu/Config of a theme directory (themeconfig.c),
        the same keys, values and defaults;
      - Image: a theme picture as 32-bit ARGB pixels with its grid of subimages
        (struct DecorImage; the pictures are PNG files, found without regard to case
        as the original's file system does);
      - Element: a part of a picture and how it is drawn (struct DecoratorElement,
        themeelements.c);
      - RastPort: an ARGB drawing surface standing for the RastPort the decorator draws
        into (WritePixelArray, WritePixelArrayAlpha, RectFill, Draw);
      - the drawing functions of drawfuncs.c and decoratorelement.c;
      - the window frame (windowdecorclass.c: the title bar, the borders and their
        shading, the gadgets' places and pictures) and the screen bar
        (screendecorclass.c).

    The title texts are drawn by the window manager itself (Xft), at the places these
    functions give.
*/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace decor {

// ---- the configuration (struct DecorConfig) --------------------------------------------------
struct Config {
    std::string ThemePath;

    // the screen
    int LeftBorder = 4, RightBorder = 4, BottomBorder = 4;
    int SLogoOffset = 0, STitleOffset = 0, SBarHeight = 0, SBarFrame = 0;
    int SBarChildPre_o = 0, SBarChildPre_s = 0, SBarChildFill_o = 0, SBarChildFill_s = 0;
    int SBarChildPost_o = 0, SBarChildPost_s = 0;
    int SBarGadPre_o = 0, SBarGadPre_s = 0, SBarGadFill_o = 0, SBarGadFill_s = 0, SBarGadPost_o = 0, SBarGadPost_s = 0;
    bool STitleOutline = false, STitleShadow = false;
    uint32_t LUTBaseColors_a = 0x00CCCCCC, LUTBaseColors_d = 0x00888888;
    uint32_t STitleColorText = 0xFFFFFFFF, STitleColorShadow = 0xFFFFFFFF;

    // the windows
    int WinFrameStyle = 1;
    bool BarRounded = false, BarVertical = false;
    int BarHeight = 0, BarFrame = 0;
    int BarJoinTB_o = 0, BarJoinTB_s = 0, BarPreGadget_o = 0, BarPreGadget_s = 0, BarPre_o = 0, BarPre_s = 0;
    int BarLGadgetFill_o = 0, BarLGadgetFill_s = 0, BarJoinGB_o = 0, BarJoinGB_s = 0, BarLFill_o = 0, BarLFill_s = 0;
    int BarJoinBT_o = 0, BarJoinBT_s = 0, BarTitleFill_o = 0, BarTitleFill_s = 0, BarRFill_o = 0, BarRFill_s = 0;
    int BarJoinBG_o = 0, BarJoinBG_s = 0, BarRGadgetFill_o = 0, BarRGadgetFill_s = 0;
    int BarPostGadget_o = 0, BarPostGadget_s = 0, BarPost_o = 0, BarPost_s = 0;
    int ContainerTop_o = 0, ContainerTop_s = 0, ContainerVertTile_o = 0, ContainerVertTile_s = 0;
    int ContainerBottom_o = 0, ContainerBottom_s = 0;
    int KnobTop_o = 0, KnobTop_s = 0, KnobTileTop_o = 0, KnobTileTop_s = 0, KnobVertGripper_o = 0, KnobVertGripper_s = 0;
    int KnobTileBottom_o = 0, KnobTileBottom_s = 0, KnobBottom_o = 0, KnobBottom_s = 0;
    int ContainerLeft_o = 0, ContainerLeft_s = 0, ContainerHorTile_o = 0, ContainerHorTile_s = 0;
    int ContainerRight_o = 0, ContainerRight_s = 0;
    int KnobLeft_o = 0, KnobLeft_s = 0, KnobTileLeft_o = 0, KnobTileLeft_s = 0, KnobHorGripper_o = 0, KnobHorGripper_s = 0;
    int KnobTileRight_o = 0, KnobTileRight_s = 0, KnobRight_o = 0, KnobRight_s = 0;
    bool GadgetsThreeState = false, TitleOutline = false, TitleShadow = false, FillTitleBar = false;
    uint32_t TitleColorText = 0x00CCCCCC, TitleColorShadow = 0x00444444;
    bool BarMasking = false, CloseGadgetOnRight = false, UseGradients = false;
    int BottomBorderGadgets = 0, RightBorderGadgets = 0, UpDownAddY = 0, LeftRightAddX = 0;
    uint32_t ActivatedGradientColor_s = 0xAAAAAAAA, ActivatedGradientColor_e = 0xEEEEEEFF;
    int ActivatedGradientColor_a = 0;
    uint32_t DeactivatedGradientColor_s = 0x66666666, DeactivatedGradientColor_e = 0xAAAAAABB;
    int DeactivatedGradientColor_a = 0;
    int ShadeValues_l = 320, ShadeValues_m = 240, ShadeValues_d = 128;
    uint32_t BaseColors_a = 0, BaseColors_d = 0;
    // (read, not used by the original either)
    int SizeAddX = 0, SizeAddY = 0, UpDownAddX = 0, LeftRightAddY = 0;
    int BottomBorderNoGadgets = 0, RightBorderNoGadgets = 0, HorizScrollerHeight = 0, ScrollerInnerSpacing = 0;

    // the menus
    bool MenuIsTiled = false;
    int MenuTileLeft = 0, MenuTileTop = 0, MenuTileRight = 0, MenuTileBottom = 0;
    uint32_t MenuHighlightTint = 0x770044dd;
    int MenuInnerLeft = 0, MenuInnerTop = 0, MenuInnerRight = 0, MenuInnerBottom = 0;
};

// ---- a picture (struct DecorImage) -----------------------------------------------------------
struct Image {
    std::vector<uint32_t> data;     // ARGB, 0xAARRGGBB
    int w = 0, h = 0;
    bool ok = false;
    int cols = 1, rows = 1;         // subimagescols, subimagesrows
};
using ImagePtr = std::shared_ptr<Image>;

struct Images {
    ImagePtr sdepth, sbarlogo, stitlebar;
    ImagePtr size, close, depth, zoom, up, down, left, right, mui, popup, snapshot, iconify, lock;
    ImagePtr winbar_normal, border_normal, border_deactivated;
    ImagePtr verticalcontainer, verticalknob, horizontalcontainer, horizontalknob;
    ImagePtr menu, menukey, menucheck, submenu;
};

// ---- an element (struct DecoratorElement) ----------------------------------------------------
enum ElementType { DE_TYPE_NONE, DE_TYPE_STATEFUL_GADGET, DE_TYPE_TILED_HORIZONTAL, DE_TYPE_TILED_VERTICAL,
                   DE_TYPE_SCALED_TILED_H, DE_TYPE_TILED_TITLE, DE_TYPE_IMAGE, DE_TYPE_TILED_BOTH };
enum ElementFlags { DEF_THREE_STATE = 1, DEF_TITLE_GADGET = 2, DEF_SCALABLE = 4, DEF_HAS_GRIPPER = 8, DEF_FILL = 16 };

struct Element {
    Image *image = nullptr;
    int type = DE_TYPE_NONE, flags = 0;
    int cols = 1, rows = 1;
    int src_offset = 0, src_size = 0, src_height = 0;
    int pad_x = 0, pad_y = 0;
};

enum {
    WinBarPreGadget, WinBarPre, WinBarLGadgetFill, WinBarJoinGB, WinBarLFill, WinBarJoinBT, WinBarTitleFill,
    WinBarJoinTB, WinBarRFill, WinBarJoinBG, WinBarRGadgetFill, WinBarPostGadget, WinBarPost,
    WinClose, WinDepth, WinZoom, WinSize, WinMUI, WinPopup, WinSnapshot, WinIconify, WinLock,
    WinUp, WinDown, WinLeft, WinRight,
    VContainerTop, VContainerTile, VContainerBottom, VKnobTop, VKnobTileTop, VKnobGripper, VKnobTileBottom, VKnobBottom,
    HContainerLeft, HContainerTile, HContainerRight, HKnobLeft, HKnobTileLeft, HKnobGripper, HKnobTileRight, HKnobRight,
    ScrBarFill, ScrBarGadPre, ScrBarGadFill, ScrBarGadPost, ScrBarChildPre, ScrBarChildFill, ScrBarChildPost,
    ScrDepth, ScrBarLogo,
    MenuBackground, MenuKey, MenuCheck, MenuSubMenu,
    WinBorderNormal, WinBorderDeactivated, WinBarNormal,
    NUM_ELEMENTS
};

// the image states (imageclass.h)
enum { IDS_NORMAL = 0, IDS_SELECTED = 1, IDS_DISABLED = 2, IDS_BUSY = 3, IDS_INDETERMINATE = 4, IDS_INACTIVENORMAL = 5,
       IDS_INACTIVESELECTED = 6, IDS_INACTIVEDISABLED = 7 };

// ---- a loaded theme (struct DecorTheme) ------------------------------------------------------
struct Theme {
    Config dc;
    Images di;
    Element el[NUM_ELEMENTS];
};

// DTLoadTheme: the theme in a directory (nullptr when it is no theme: no System/Config and
// no title bar picture)
std::unique_ptr<Theme> load_theme(const std::string &dir);

// a path below a directory, found without regard to case ("" when there is none)
std::string find_path(const std::string &dir, const std::string &rel);

// ---- the drawing surface ---------------------------------------------------------------------
struct RastPort {
    int w = 0, h = 0;
    std::vector<uint32_t> px;       // ARGB (the alpha byte unused: the surface is opaque)
    void size(int W, int H) { w = W; h = H; px.assign((size_t)W * H, 0xFF000000u); }
    // WritePixelArray (a copy) and WritePixelArrayAlpha (blended over what is there)
    void write(const uint32_t *src, int sx, int sy, int stride, int dx, int dy, int ww, int hh);
    void write_alpha(const uint32_t *src, int sx, int sy, int stride, int dx, int dy, int ww, int hh);
    void fill(int x, int y, int ww, int hh, uint32_t rgb);                 // RectFill / FillPixelArray
    void line(int x0, int y0, int x1, int y1, uint32_t rgb);               // Move + Draw
};

// ---- the drawing functions (drawfuncs.c, decoratorelement.c) ---------------------------------
int render_element(const Element &e, RastPort &rp, int subimage, int xp, int yp, int dw, int dh, int clipw);
std::shared_ptr<Image> scale_image(const Image &src, int neww, int newh);          // DScaleDecorImage
void fill_gradient(RastPort &rp, int xt, int yt, int xb, int yb, int xp, int yp, int w, int h,
                   uint32_t start_rgb, uint32_t end_rgb, int angle, int dy);       // FillPixelArrayGradient
uint32_t calc_shade(uint32_t base, int fact);

// ---- the window frame (windowdecorclass.c) ---------------------------------------------------
struct Box { int x = 0, y = 0, w = 0, h = 0; bool on = false; bool contains(int px, int py) const {
    return on && px >= x && py >= y && px < x + w && py < y + h; } };

struct WindowFrame {
    int border_top = 0, border_left = 0, border_right = 0, border_bottom = 0;
    Box close, depth, zoom, size;
};

// the borders and the gadgets of a window W x H (the frame's size) with these gadgets, for a
// font of this height (the window's BorderTop grows with it, as openwindow.c makes it)
WindowFrame window_frame(const Theme &t, int font_height, int W, int H, bool has_close, bool has_depth,
                         bool has_zoom, bool has_size);

// the gadgets' states
struct GadgetStates { int close = IDS_NORMAL, depth = IDS_NORMAL, zoom = IDS_NORMAL, size = IDS_NORMAL; };

// windecor_draw_winborder + windecor_draw_sysimage: the frame drawn into rp (sized W x H);
// fit(maxw) tells how wide the title is when cut to maxw pixels (TextFit); the title's place
// comes back in title_x (-1: no title shown), title_baseline, title_width
void draw_window(Theme &t, RastPort &rp, const WindowFrame &f, bool active, const GadgetStates &gs,
                 bool has_title, int font_ascent, int font_height, int (*fit)(void *, int), void *fit_data,
                 int &title_x, int &title_baseline, int &title_width);

// windecor_windowshape: the window's shape as rectangles (none: the whole window)
struct Rect { int x, y, w, h; };
std::vector<Rect> window_shape(Theme &t, const WindowFrame &f, int W, int H, bool active, bool has_title,
                               int (*fit)(void *, int), void *fit_data);

// ---- the screen bar (screendecorclass.c) -----------------------------------------------------
struct ScreenBar {
    int bar_height = 0;             // the screen's BarHeight (the bar is BarHeight + 1 high)
    Box depth;
    int title_right = 0;            // the title's room ends here
};
ScreenBar screen_bar(const Theme &t, int font_height, int SW);
void draw_screen_bar(Theme &t, RastPort &rp, const ScreenBar &b, int depth_state, int font_ascent, int font_height,
                     int &title_x, int &title_baseline);

}  // namespace decor
