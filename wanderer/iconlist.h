/*
    Copyright (C) 2002-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The IconList class of Wanderer (workbench/system/Wanderer/Classes/iconlist.c) and its
    subclasses IconDrawerList (the contents of a directory) and IconVolumeList (the mounted
    volumes). An IconList shows entries as icons with labels under them (or as a list),
    lays them out in a grid or "rough" (each icon as wide as it needs), in rows or in
    columns, sorts them, selects them with a click, Shift-click or a lasso, and moves them.
*/
#pragma once
#include "zune.h"

#include <FL/Fl_Scrollbar.H>

#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace wanderer {

// the entry types (dos.library ST_*)
enum EntryType { ST_ROOT = 1, ST_USERDIR = 2, ST_SOFTLINK = 3, ST_LINKDIR = 4, ST_FILE = -3, ST_LINKFILE = -4,
                 ST_PIPEFILE = -5 };

// MUIV_IconList_Sort_*
enum SortFlags {
    Sort_DrawersMixed = 1 << 0, Sort_DrawersLast = 1 << 1, Sort_Reverse = 1 << 2,
    Sort_ByName = 1 << 4, Sort_ByDate = 1 << 5, Sort_BySize = 1 << 6, Sort_ByType = 1 << 7,
    Sort_AutoSort = 1 << 16,
    Sort_Orders = Sort_ByName | Sort_ByDate | Sort_BySize | Sort_ByType
};

// ICONLIST_DISP_*
enum DisplayFlags {
    Disp_ModeDefault = 1 << 1, Disp_ModeLabelRight = 1 << 2, Disp_ModeList = 1 << 3,
    Disp_ShowHidden = 1 << 6, Disp_ShowInfo = 1 << 7, Disp_NoIcons = 1 << 10, Disp_Vertical = 1 << 15,
    Disp_ModeMask = Disp_ModeDefault | Disp_ModeLabelRight | Disp_ModeList
};

enum ListMode { ListMode_Grid = 0, ListMode_Rough = 1 };
enum TextMode { TextMode_Outline = 0, TextMode_Plain = 1, TextMode_DropShadow = 2 };

constexpr int NO_ICON_POSITION = (int)0x80000000;

struct IconEntry {
    std::string path, label;
    EntryType type = ST_FILE;
    long long size = 0;
    time_t mtime = 0;
    bool selected = false, focus = false, visible = true;
    bool executable = false;          // a tool (the x bit)
    unsigned mode = 0;                // the protection (st_mode)
    std::string comment;              // the file's comment (user.comment)
    bool left_out = false;            // on the root window by Leave Out
    bool trash = false;               // the trash can
    std::string device;               // a volume: its block device (/dev/sdb1)
    bool unmounted = false;           // a volume not mounted yet (ghosted; opening mounts it)
    unsigned long app_window = 0;     // an AppIcon: the program's window
    long app_id = 0;                  // and the icon's number there
    int provided_x = NO_ICON_POSITION, provided_y = NO_ICON_POSITION;   // a snapshot position
    // computed (iconlist.c: ie_IconX/Y, ie_AreaWidth/Height, ie_IconWidth/Height ...)
    int x = 0, y = 0, area_w = 0, area_h = 0, icon_w = 0, icon_h = 0;
    std::vector<std::string> lines;   // the displayed label, split
    int label_w = 0;                  // the widest line
    std::string info;                 // size or date under the label (sorting by size / date)
    bool is_drawer() const { return type == ST_USERDIR || type == ST_ROOT || type == ST_LINKDIR; }
};

// ---------------------------------------------------------------------------------------------
class IconList : public zune::Area {
public:
    IconList();
    ~IconList() override;

    // the entries
    void clear();
    IconEntry &add(IconEntry e);
    std::vector<IconEntry> &entries() { return entries_; }
    std::vector<IconEntry *> selection();
    void select_all();
    void unselect_all();
    virtual void update() {}            // MUIM_IconList_Update: re-read the contents

    // the options (MUIA_IconList_*)
    int display_flags = Disp_ModeDefault;
    int sort_flags = Sort_ByName;
    ListMode list_mode = ListMode_Grid;
    TextMode text_mode = TextMode_Outline;
    int label_maxlen = 10;            // ILC_ICONLABEL_MAXLINELEN_DEFAULT
    int label_multiline = 1;
    bool label_multiline_on_focus = false;
    int icon_font_family = -1;   // the icon font (font.prefs FP_ICONFONT): -1 the screen's, 0 sans, 1 serif, 2 fixed
    int icon_font_size = 0;      // 0: the screen font's size less one   // all the lines only on the focused entry's label
    int hspacing = 5, vspacing = 5;   // ILC_ICON_HORIZONTALMARGIN_DEFAULT ...
    int image_spacing = 1;            // between the image and the label
    int label_hpad = 4, label_vpad = 4, label_border_w = 1, label_border_h = 1;
    int border_overlap = 10;
    bool trim_volume_names = true;
    Fl_Color label_pen, label_shadow_pen, info_pen;
    // the background picture (iconwindowbackfill.c): a PNG/JPEG/GIF/BMP file; 0 tiled,
    // 1 scaled to the view, 2 centred; "" - the background pen
    void backfill(const std::string &path, int mode);

    void sort();                      // MUIM_IconList_Sort
    void rethink();                   // MUIM_IconList_RethinkDimensions + PositionIcons
    void scroll_to(int x, int y);
    int view_left() const { return view_x_; }
    int view_top() const { return view_y_; }
    int content_w() const { return content_w_; }
    int content_h() const { return content_h_; }

    // what happens
    std::function<void(IconEntry &)> on_open;            // double click / Return
    std::function<void()> on_selection_changed;
    std::function<void(int x_root, int y_root)> on_context_menu;
    // icons dropped somewhere: onto an entry of this list (target), or elsewhere (nullptr)
    std::function<void(std::vector<IconEntry *> &, IconEntry *target)> on_drop;
    std::function<void()> on_view_changed;               // scrolled / contents resized
    // files dropped from another program (XDND): their paths, the drawer under the pointer
    std::function<void(const std::vector<std::string> &, IconEntry *target)> on_external_drop;

    void ask_min_max(int &minw, int &minh, int &defw, int &defh, int &maxw, int &maxh) override;
    void resize(int X, int Y, int W, int H) override;
    int handle(int e) override;
    IconEntry *entry_at(int ex, int ey);
    void move_focus(int key, bool extend);

protected:
    void draw_contents() override;
    void draw_background(int X, int Y, int W, int H) override;
    std::string backfill_path_;
    int backfill_mode_ = 0;
    class Fl_Shared_Image *backfill_ = nullptr, *backfill_scaled_ = nullptr;
    void draw_entry(IconEntry &e);
    void draw_entry_label(IconEntry &e, int lx, int ly);
    virtual void draw_icon_image(IconEntry &e, int x, int y, bool selected);
    virtual void icon_size(IconEntry &e, int &w, int &h);
    void create_label(IconEntry &e);
    void split_label(IconEntry &e);
    void label_rect(IconEntry &e, int &w, int &h);
    void area_rect(IconEntry &e);
    void position_icons();
    void draw_list_mode();
    int col_x_[6] = {0, 0, 0, 0, 0, 0};   // the list's columns (from its last drawing)

    std::vector<IconEntry> entries_;
    int view_x_ = 0, view_y_ = 0, content_w_ = 0, content_h_ = 0;
    int icon_largest_h_ = 0, area_largest_w_ = 0, area_largest_h_ = 0, label_largest_h_ = 0;
    // input
    int press_x_ = 0, press_y_ = 0;
    bool lasso_ = false, dragging_ = false, moved_ = false;
    int lasso_x_ = 0, lasso_y_ = 0;
    IconEntry *pressed_ = nullptr;
    std::vector<bool> lasso_before_;
    int label_font_size() const { return icon_font_size > 0 ? icon_font_size : zune::prefs().font_size - 1; }
    Fl_Font label_font(bool bold = false) const {
        static const Fl_Font fams[3][2] = {{FL_HELVETICA, FL_HELVETICA_BOLD}, {FL_TIMES, FL_TIMES_BOLD}, {FL_COURIER, FL_COURIER_BOLD}};
        if (icon_font_family >= 0 && icon_font_family < 3) return fams[icon_font_family][bold];
        return bold ? zune::prefs().font_bold : zune::prefs().font;
    }
};

// ---------------------------------------------------------------------------------------------
// IconDrawerList: the contents of a directory
class IconDrawerList : public IconList {
public:
    explicit IconDrawerList(const std::string &dir);
    const std::string &directory() const { return dir_; }
    void directory(const std::string &d);
    void update() override;

private:
    std::string dir_;
};

// IconSearchList: the entries found by Find (a name pattern, under a directory)
class IconSearchList : public IconList {
public:
    IconSearchList(const std::string &dir, const std::string &pattern, const std::string &contents = "")
        : dir_(dir), pattern_(pattern), contents_(contents) {}
    void update() override;
    int found() const { return (int)entries_.size(); }

private:
    std::string dir_, pattern_, contents_;
};

// Removable block devices with a file system (lsblk)
struct BlockVolume { std::string device, label, mountpoint; };
std::vector<BlockVolume> removable_volumes();

// The places the root window shows besides the volumes
std::string trash_dir();                       // ~/.local/share/Trash/files
std::string ramdisk_dir();                     // the Ram Disk (/dev/shm/ramdisk-USER)
// "Home:Dokumenty" <-> "/home/user/Dokumenty" (volume names: System / SYS, Ram Disk / RAM,
// Home, Trash, the names of mounted drives; a Linux path is taken as it is)
std::string to_volume_path(const std::string &path);
std::string from_volume_path(const std::string &path);
// the user's assigns (~/.config/wanderer/assigns: "Name = path"): name, directory
std::vector<std::pair<std::string, std::string>> assigns();
std::vector<std::string> left_out_paths();     // ~/.config/wanderer/leftout
void set_left_out(const std::vector<std::string> &paths);
// The volumes the root window does not show (~/.config/wanderer/hiddenvolumes, one a line:
// a volume name - a trailing colon allowed, no case -, its device or its mount point)
std::vector<std::string> hidden_volumes();
// Programs' AppIcons / AppMenuItems: the names in a property (UTF8_STRING, NUL-separated)
// on the top-level windows (_NET_CLIENT_LIST); a message back with the paths it concerns
struct AppItem { unsigned long window; long id; std::string label; };
std::vector<AppItem> app_items(const char *property);
void app_message(const AppItem &item, const char *message, const char *args_property,
                 const std::vector<std::string> &paths);
void set_hidden_volumes(const std::vector<std::string> &names);

// IconVolumeList: the mounted volumes (the desktop's root window)
class IconVolumeList : public IconList {
public:
    IconVolumeList();
    void update() override;
};

}  // namespace wanderer
