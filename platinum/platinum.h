// Nirvana Desktop - libplatinum
// The "Platinum" look (bevelled gray, in the spirit of the classic 1990s desktops) for FLTK 1.4.
// All Nirvana Desktop components (insight, denise, gary, alice...) draw
// through this library, so that everything looks consistent.
#pragma once

#include "i18n.h"

#include <FL/Enumerations.H>
#include <ctime>
#include <string>
#include <vector>

class Fl_RGB_Image;

namespace plat {

// ---- Palette --------------------------------------------------------------
// All colors are FLTK palette indexes, not RGB values. Thanks to that
// a scheme change (Appearance panel) is just Fl::set_color() + redraw() - widgets
// that stored e.g. color(plat::GRAY) get the new color by themselves.
extern Fl_Color GRAY;          // window and panel background ("window") = FL_BACKGROUND_COLOR
extern Fl_Color TEXT;          // text on the window background ("text") = FL_FOREGROUND_COLOR
extern Fl_Color CONTENT;       // inside of lists and fields ("content") = FL_BACKGROUND2_COLOR
extern Fl_Color SEL;           // selection ("selection")            = FL_SELECTION_COLOR
extern Fl_Color CONTENT_TEXT;  // text in lists and fields
extern Fl_Color SEL_TEXT;      // selected text
extern Fl_Color ICON_SEL;      // background of a selected icon's label
extern Fl_Color ICON_SEL_TEXT; // text of a selected icon's label
extern Fl_Color LIGHT;         // light edge of a bevel
extern Fl_Color MID;           // mid shadow
extern Fl_Color SHADOW;        // dark shadow
extern Fl_Color DARK;          // outline ("border")
extern Fl_Color HILITE;        // accent: the scroll thumb ("accent")
extern Fl_Color STRIPE;        // dark stripes of the title bar
extern Fl_Color TITLE_TEXT;    // title of the active window
extern Fl_Color TITLE_I_TEXT;  // title of an inactive window
extern Fl_Color DESK_A;        // desktop color
extern Fl_Color DESK_B;        // second tone of the desktop pattern
extern Fl_Color ALERT;         // warning red

// ---- Color schemes and appearance settings --------------------------------
// A scheme is a key=value file in the "schemes" folder (e.g. platinum.conf,
// redmond97-se/ouroboros.conf); its identifier is the path without ".conf".
enum class TitleStyle { Stripes, Solid, Gradient, Glossy };   // Glossy: a glass bar with a shine

struct Scheme {
  std::string id, name, source;
  TitleStyle style = TitleStyle::Stripes;
  unsigned window = 0xDDDDDD, text = 0x000000, content = 0xFFFFFF, content_text = 0x000000;
  unsigned sel = 0x4444AA, sel_text = 0xFFFFFF, border = 0x000000;
  unsigned title = 0xDDDDDD, title2 = 0xDDDDDD, title_text = 0x000000;
  unsigned title_i = 0xDDDDDD, title_i2 = 0xDDDDDD, title_i_text = 0x777777;
  int icon_sel = -1, accent = -1, desk = -1;   // -1: computed from the others
  int light = -1, mid = -1, shadow = -1;       // -1: computed from the window color
};

// The desktop (~/.config/nirvana/desktop.conf, Preferences > Desktop)
struct DesktopSettings {
  bool icon_margin = true;   // zoomed and new windows leave the column of desktop icons free, with gaps
  bool dock_room = true;     // windows stay above the dock
  // "Classic 16-bit" windows: close gadget at the left, zoom and depth gadgets at the right,
  // a size gadget in the corner; a click activates a window without raising it
  bool classic = false;
  bool outline = false;      // move and resize with an XOR outline (the window jumps on release)
};
DesktopSettings read_desktop();
bool write_desktop(const DesktopSettings &d);
void notify_desktop_changed();   // Rambler and Insight follow at once (_NIRVANA_DESKTOP)
constexpr const char *DESKTOP_ATOM = "_NIRVANA_DESKTOP";

// The clock in the menu bar and its timers (~/.config/nirvana/clock.conf, Preferences > Date & Time)
struct ClockSettings {
  bool h24 = true;          // 24-hour clock (otherwise 3:07 PM)
  bool seconds = false;
  bool date = false;        // "11 Nov" before the time
  bool weekday = false;     // "Wed" before the time
  int work = 25, short_break = 5, long_break = 15, rounds = 4;   // pomodoro, minutes
  bool sound = true;        // a sound when a timer ends
  bool auto_next = false;   // pomodoro: start the next phase without asking
};
ClockSettings read_clock();
bool write_clock(const ClockSettings &c);
std::string clock_file();
// Names for dates in the interface language (0 = January / Sunday as in struct tm)
std::string month_name(int m);          // "January"
std::string month_in_date(int m);       // "November" ("listopada" in Polish)
std::string month_short(int m);         // "Jan"
std::string weekday_name(int d);        // "Wednesday"
std::string weekday_short(int d);       // "Wed"
std::string weekday_letters(int d);     // "We" (calendar header)
std::string clock_text(const ClockSettings &c, time_t t, bool with_seconds);   // "Wed 11 Nov 15:07"

struct Appearance {                 // ~/.config/nirvana/wyglad.conf
  std::string scheme = "redmond97-se/in-tenebris";   // In Tenebris by default
  int style = -1;                   // -1: as in the scheme, otherwise (int)TitleStyle
  int desk = -1;                    // -1: the scheme's desktop color
  std::string background = "pattern";  // pattern, color (solid), gradient
  int desk2 = -1;                   // second gradient color; -1: a darker shade of the desktop color
  std::string gradient = "vertical"; // vertical, horizontal, diagonal, radial
  std::string pattern = "bricks";    // see PATTERNS
  bool animations = true;
  std::string icons = "platinum";   // icon style: retro or platinum
  std::string wallpaper;            // wallpaper file; empty = just the pattern
  std::string wallpaper_mode = "fill";   // see WALLPAPER_MODES
  int scale = 0;                    // interface scale in percent; 0 = automatic (Xft.dpi)
};

// Scales offered in the Appearance panel (percent); 0 = automatic
extern const int SCALES[];
// Scale from the settings / NIRVANA_SCALE / Xft.dpi - set in init() for all screens.
float configured_scale(const Appearance &a);

std::vector<std::string> scheme_dirs();                      // search order
std::vector<Scheme> list_schemes();                          // sorted, without duplicates
bool load_scheme(const std::string &id, Scheme &out);        // by identifier
bool parse_scheme(const std::string &file, Scheme &out);
Scheme builtin_platinum();
Appearance read_appearance();
bool write_appearance(const Appearance &a);
std::string appearance_file();

void apply(const Scheme &s, const Appearance &a);   // set the palette
void reload_appearance();          // read the settings and the scheme, apply
const Scheme &current_scheme();
std::string data_file(const std::string &rel);   // a file from share/nirvana (or the source tree), "" if missing
const Appearance &current_appearance();
TitleStyle title_style();

void localize_file_chooser();   // labels of the FLTK file chooser in the interface language

// Wallpaper: layouts, the list of ready images and a desktop background ready to draw.
extern const char *const WALLPAPER_MODES[];        // fill, fit, stretch, center, tile
extern const char *const WALLPAPER_MODE_NAMES[];   // display names
bool is_image_file(const std::string &path);
bool image_loadable(const std::string &path);       // can FLTK open this image
std::vector<std::string> wallpaper_dirs();          // Nirvana Desktop wallpapers (share/nirvana/wallpapers)
std::string pictures_dir();                         // XDG_PICTURES_DIR (user-dirs.dirs), else ~/Pictures
std::vector<std::string> wallpaper_candidates();    // Nirvana Desktop, system ones, the Pictures folder
std::string wallpaper_title(const std::string &path);   // "Mountains" from ".../Mountains.jpg"
// Desktop W x H: the pattern in the scheme colors, the wallpaper on top (if set and loadable).
// *loaded = false when the wallpaper could not be opened.
::Fl_RGB_Image *render_desktop(const Appearance &a, int W, int H, bool *loaded = nullptr);

extern const char *const BACKGROUNDS[];       // pattern, color, gradient
extern const char *const BACKGROUND_NAMES[];
extern const char *const GRADIENTS[];         // vertical, horizontal, diagonal, radial
extern const char *const GRADIENT_NAMES[];
unsigned gradient_second(const Appearance &a); // second gradient color (RGB)

extern const char *const PATTERNS[];        // pattern identifiers (nullptr at the end)
extern const char *const PATTERN_NAMES[];   // display names
// A desktop pattern tile (RGB, 8x8 or 16x16) in the DESK_A/DESK_B colors.
// The caller frees the image (delete).
unsigned char *pattern_tile(const std::string &pattern, int &w, int &h);   // RGB w*h*3, free with delete[]

// Appearance change notifications: the panel sets the _NIRVANA_APPEARANCE property
// on the X root window, and Rambler and Insight call reload_appearance()
// when it changes and refresh their windows.
constexpr const char *APPEARANCE_ATOM = "_NIRVANA_APPEARANCE";
void notify_appearance_changed();

// ---- Box types (Fl_Boxtype) ----------------------------------------------
extern Fl_Boxtype UP_BOX;       // raised panel
extern Fl_Boxtype DOWN_BOX;     // sunken panel
extern Fl_Boxtype BUTTON_BOX;   // rounded button
extern Fl_Boxtype BUTTON_DOWN;  // pressed button
extern Fl_Boxtype WELL_BOX;     // "well" - text field, list
extern Fl_Boxtype MENUBAR_BOX;  // menu bar
extern Fl_Boxtype THUMB_BOX;    // lavender scroll thumb with grooves
extern Fl_Boxtype TRACK_BOX;    // scroll track

// ---- Fonts -------------------------------------------------------------------
extern Fl_Font FONT_SYSTEM;     // titles, menus (like Charcoal 12)
extern Fl_Font FONT_SMALL;      // icon labels (like Geneva 9/10)
constexpr int SIZE_SYSTEM = 12;
constexpr int SIZE_SMALL  = 11;

// Initialization: box types, fonts, the look from ~/.config/nirvana/appearance.conf.
// Also moves old settings to the current names (migrate_old_config).
void init();

// ---- Icons (32x32, vector drawn) ------------------------------------------
enum class Icon { Folder, OpenFolder, Document, Application, Home, Trash, TrashFull, Image, Music,
                  HardDisk, Removable, Optical, Encrypted, Eject, Network, Wifi, Preferences, Applications, Terminal,
                  // category folders in the Applications folder (a folder with an emblem),
                  // in the order of appimage::categories()
                  CatGames, CatInternet, CatOffice, CatGraphics, CatMultimedia, CatDevelopment,
                  CatEducation, CatScience, CatSystem, CatAccessories, CatOther,
                  Last = CatOther };
// The emblem of a category folder (0 = Games … 10 = Other) in a 14x14 box at x, y.
// style 1 (Platinum) darkens a selected emblem, style 0 (Retro) inverts its colors.
void draw_category_emblem(int category, int x, int y, bool selected, int style);
void draw_icon(Icon icon, int x, int y, bool selected);   // in the style from the appearance settings
bool draw_icon_retro(Icon icon, int x, int y, bool selected);
int icon_style();   // 0 = Retro, 1 = Platinum (key for the icon caches)
void draw_logo(int x, int y, int size);
// Error windows have this title (a nod to the error screens of 1980s home computers).
constexpr const char *GURU_TITLE = "Guru Meditation";
// "#00000004.4B3A91C0": a guru number made from the message, for the error windows
std::string guru_number(const std::string &message);
void guru_alert(const std::string &message);
// "nirvana-pkexec" (pkexec that brings its own password window) or plain "pkexec"
std::string pkexec_command();   // fl_alert with the guru title
// An icon rendered to an RGBA image (e.g. a window icon for _NET_WM_ICON); delete it.
Fl_RGB_Image *icon_image(Icon icon, int size);

// Small network icons (menu bar, lists). on - color of the lit elements,
// off - of the dimmed ones.
void draw_wifi(int x, int y, int level /*0-4, -1: off*/, Fl_Color on, Fl_Color off);   // 16x13
void draw_wired(int x, int y, Fl_Color c);                       // 16x13
void draw_signal(int x, int y, int level /*0-4*/, Fl_Color on, Fl_Color off);   // 13x10
void draw_lock(int x, int y, Fl_Color c);                        // 7x9
// Sound and power (scale > 1 for large versions, e.g. in the OSD).
void draw_speaker(int x, int y, int level /*0-3*/, bool muted, Fl_Color c, double scale = 1);   // 16x13
void draw_battery(int x, int y, int percent, bool charging, Fl_Color c, double scale = 1);       // 22x11
void draw_plug(int x, int y, Fl_Color c);                        // 14x13
void draw_sun(int x, int y, Fl_Color c, double scale = 1);       // 13x13
void draw_display(int x, int y, Fl_Color c);
// A depth gadget: a back rectangle and a front one overlapping it.
void draw_depth_gadget(int x, int y, bool pressed);              // 18x14

// ---- Window chrome -------------------------------------------------------------
constexpr int TITLE_H = 20;   // title bar height
constexpr int FRAME   = 5;    // frame thickness
constexpr int BOX     = 13;   // size of the title bar buttons
constexpr int MENUBAR_H = 20; // height of the global menu bar
constexpr int SCROLL_W  = 15; // width of the scrollbars
void draw_titlebar(int x, int y, int w, const char *title, bool active,
                   int pressed_box = 0 /* 0 none, 1 close, 2 zoom, 3 iconify */,
                   int boxes = 1 /* 0: dialog without buttons, 1: all, 2: without "zoom" */);
void draw_frame(int x, int y, int w, int h, bool active, bool shaded);
void draw_grow_box(int x, int y);
// "Classic 16-bit" windows (Preferences > Desktop): the bar is a row of beveled cells.
// boxes: 0 no gadgets, 1 close + zoom + depth, 2 depth only (dialogs), 3 close + depth
// pressed: 1 close, 2 zoom, 4 depth
constexpr int CLASSIC_GADGET = 22;   // width of a gadget cell
constexpr int CLASSIC_SIZE = 16;     // the size gadget in the bottom right corner (L on the frame)
void draw_classic_titlebar(int x, int y, int w, const char *title, bool active, int pressed = 0, int boxes = 1);
void draw_classic_frame(int x, int y, int w, int h, bool active, bool shaded, bool size_gadget);
int classic_gadget_hit(int w, int ex, int ey, int boxes);   // the gadget under the point (title bar coordinates)

// Helpers
std::string ellipsize(const std::string &s, int max_w);   // text cut with "…"
std::string items_text(int n);                           // "1 item", "3 items" (with plural forms of the language)

} // namespace plat
