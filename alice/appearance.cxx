// Nirvana Desktop - alice: the "Appearance" panel.
// Color scheme, title bar style, desktop background (pattern, color or
// gradient), wallpaper, icon style, window animations. Changes show at once in the panel
// itself (live preview); "Apply" saves ~/.config/nirvana/appearance.conf and
// sets the _NIRVANA_APPEARANCE property on the X root window - Rambler and Insight
// then reload the palette without a restart.
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Color_Chooser.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include <FL/Fl_RGB_Image.H>
#include "framewin.h"
#include <FL/Fl_Hold_Browser.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace plat;

static std::vector<Scheme> g_schemes;
static Appearance g_cur;            // settings shown in the panel
static int g_scheme = 0;            // index into g_schemes
static FrameWindow *g_win;
static Fl_Hold_Browser *g_list;
static Fl_Box *g_source;
static Fl_Choice *g_style, *g_pattern, *g_wall, *g_wall_mode, *g_bg, *g_grad;
static Fl_Button *g_desk2;
static Fl_Choice *g_icons;
static std::vector<std::string> g_walls;   // wallpaper list items (0 = none, the last = "Other file…")
static Fl_Check_Button *g_anim;
static Fl_Choice *g_scale;
static Fl_Button *g_desk_default;

// ---------------------------------------------------------------------------
// Preview: a desktop thumbnail with two windows, the menu bar and a selection.
// ---------------------------------------------------------------------------
class Preview : public Fl_Widget {
public:
  Preview(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) {}
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    if (!g_cur.wallpaper.empty() || g_cur.background != "pattern") {
      // wallpaper or gradient: the desktop at screen size, scaled down for the preview
      // (layout and color transition as in reality)
      uchar r, g, b;
      Fl::get_color(DESK_A, r, g, b);
      std::string key = g_cur.wallpaper + "|" + g_cur.wallpaper_mode + "|" + g_cur.pattern + "|" +
                        std::to_string((r << 16) | (g << 8) | b) + "|" + g_cur.background + "|" +
                        g_cur.gradient + "|" + std::to_string(gradient_second(g_cur));
      if (key != key_) {
        key_ = key;
        int sx, sy, sw, sh;
        Fl::screen_xywh(sx, sy, sw, sh);
        std::unique_ptr<Fl_RGB_Image> full(render_desktop(g_cur, sw, sh));
        Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
        img_.reset((Fl_RGB_Image *)full->copy(w(), h()));
      }
      img_->draw(x(), y());
    } else {
      // desktop pattern
      int tw, th;
      unsigned char *tile = pattern_tile(g_cur.pattern, tw, th);
      std::vector<unsigned char> buf((size_t)w() * h() * 3);
      for (int yy = 0; yy < h(); yy++)
        for (int xx = 0; xx < w(); xx++)
          memcpy(&buf[((size_t)yy * w() + xx) * 3], tile + ((yy % th) * tw + xx % tw) * 3, 3);
      delete[] tile;
      fl_draw_image(buf.data(), x(), y(), w(), h(), 3);
    }

    // menu bar
    fl_push_clip(x(), y(), w(), MENUBAR_H);
    fl_draw_box(MENUBAR_BOX, x(), y(), w(), MENUBAR_H, GRAY);
    fl_pop_clip();
    draw_logo(x() + 8, y() + 2, 16);
    fl_font(FONT_SYSTEM, SIZE_SYSTEM);
    fl_color(TEXT);
    fl_draw("Nirvana", x() + 27, y() + 14);
    int mx = x() + 34 + (int)fl_width("Nirvana");
    const char *menus[] = {_("Window"), _("Icons"), _("Special")};
    for (int i = 0; i < 3; i++) {
      int mw = (int)fl_width(menus[i]) + 14;
      if (i == 1) { fl_color(SEL); fl_rectf(mx, y(), mw, MENUBAR_H - 1); }
      fl_color(i == 1 ? SEL_TEXT : TEXT);
      fl_draw(menus[i], mx + 7, y() + 14);
      mx += mw;
    }

    // window in the background (inactive)
    int bx = x() + 14, by = y() + 34, bw = 170, bh = 104;
    draw_window(bx, by, bw, bh, _("Documents"), false);
    // window on top (active)
    int fx = x() + 104, fy = y() + 70, fw = w() - 118, fh = h() - 82;
    draw_window(fx, fy, fw, fh, "Nirvana HD", true);

    // disk and Trash icons on the desktop
    fl_font(FONT_SMALL, SIZE_SMALL);
    label_icon(Icon::Home, x() + 30, y() + h() - 58, "Nirvana HD", false);
    fl_pop_clip();
  }

private:
  std::string key_;
  std::unique_ptr<Fl_RGB_Image> img_;
  static void label_icon(Icon ic, int cx, int iy, const char *txt, bool sel) {
    draw_icon(ic, cx - 16, iy, sel);
    int tw = (int)fl_width(txt);
    int tx = cx - tw / 2, ty = iy + 35;
    fl_color(sel ? ICON_SEL : CONTENT);
    fl_rectf(tx - 2, ty, tw + 4, fl_height());
    fl_color(sel ? ICON_SEL_TEXT : CONTENT_TEXT);
    fl_draw(txt, tx, ty + fl_height() - fl_descent());
  }

  static void draw_window(int X, int Y, int W, int H, const char *title, bool active) {
    draw_frame(X, Y, W, H, active, false);
    draw_titlebar(X, Y, W, title, active, 0, true);
    int cx = X + FRAME, cy = Y + TITLE_H, cw = W - 2 * FRAME, ch = H - TITLE_H - FRAME;
    // information bar
    const int INFO = 16;
    fl_color(GRAY); fl_rectf(cx, cy, cw, INFO);
    fl_color(SHADOW); fl_xyline(cx, cy + INFO - 1, cx + cw - 1);
    fl_font(FONT_SMALL, 10);
    fl_color(TEXT);
    fl_draw(active ? _("3 items, 12.4 GB free") : _("2 items"), cx, cy, cw, INFO - 1, FL_ALIGN_CENTER);
    // content
    int ty = cy + INFO, th = ch - INFO;
    fl_color(CONTENT); fl_rectf(cx, ty, cw - SCROLL_W, th);
    fl_draw_box(TRACK_BOX, cx + cw - SCROLL_W, ty, SCROLL_W, th, MID);
    if (active) {
      fl_draw_box(THUMB_BOX, cx + cw - SCROLL_W, ty + 8, SCROLL_W, th / 2, HILITE);
      fl_push_clip(cx, ty, cw - SCROLL_W, th);
      fl_font(FONT_SMALL, SIZE_SMALL);
      label_icon(Icon::Folder, cx + 34, ty + 8, _("Music"), false);
      label_icon(Icon::Document, cx + 98, ty + 8, _("Letter.txt"), true);
      label_icon(Icon::Application, cx + 162, ty + 8, _("Game"), false);
      fl_pop_clip();
    } else {
      fl_push_clip(cx, ty, cw - SCROLL_W, th);
      fl_font(FONT_SMALL, SIZE_SMALL);
      label_icon(Icon::Folder, cx + 34, ty + 8, _("Photos"), false);
      fl_pop_clip();
    }
  }
};

// ---------------------------------------------------------------------------
// Desktop color swatch: a button opening the color chooser
// ---------------------------------------------------------------------------
class Swatch : public Fl_Button {
public:
  bool second = false;   // second gradient color
  Swatch(int X, int Y, int W, int H) : Fl_Button(X, Y, W, H) { box(BUTTON_BOX); down_box(BUTTON_DOWN); }
  void draw() override {
    Fl_Button::draw();
    Fl_Color c = second ? fl_rgb_color(gradient_second(g_cur) >> 16, gradient_second(g_cur) >> 8 & 255,
                                       gradient_second(g_cur) & 255)
                        : DESK_A;
    if (!active_r()) c = fl_color_average(c, GRAY, 0.35f);
    fl_draw_box(WELL_BOX, x() + 5, y() + 4, w() - 10, h() - 8, c);
  }
};

static void preview() {
  if (g_scheme >= 0 && g_scheme < (int)g_schemes.size()) apply(g_schemes[g_scheme], g_cur);
  const Scheme &s = g_schemes[g_scheme];
  static std::string src;
  src = fmt(_("Source: %1"), s.source.empty() ? std::string(_("custom")) : s.source);
  if (s.source.find("Redmond97") != std::string::npos) src += " (GPL-3.0)";
  g_source->label(src.c_str());
  g_desk_default->deactivate();
  if (g_cur.desk >= 0 || g_cur.desk2 >= 0) g_desk_default->activate();
  // pattern or gradient direction, depending on the kind of background
  bool grad = g_cur.background == "gradient";
  if (grad) { g_pattern->hide(); g_grad->show(); g_desk2->activate(); }
  else { g_grad->hide(); g_pattern->show(); g_desk2->deactivate(); }
  if (g_cur.background == "color") g_pattern->deactivate();
  else g_pattern->activate();
  if (g_cur.wallpaper.empty()) g_wall_mode->deactivate();
  else g_wall_mode->activate();
  g_win->redraw();
}

static void fill_list() {
  g_list->clear();
  std::string group;
  for (int i = 0; i < (int)g_schemes.size(); i++) {
    const Scheme &s = g_schemes[i];
    std::string gname = s.id.find('/') == std::string::npos ? "Nirvana Desktop" : s.source.empty() ? s.id.substr(0, s.id.find('/')) : s.source;
    if (gname != group) {
      group = gname;
      g_list->add(("@b@." + gname).c_str(), (void *)(long)-1);
    }
    g_list->add(("   " + std::string(_(s.name.c_str()))).c_str(), (void *)(long)i);
    if (s.id == g_cur.scheme) { g_scheme = i; g_list->value(g_list->size()); }
  }
}

static void list_cb(Fl_Widget *, void *) {
  int line = g_list->value();
  if (line <= 0) return;
  long i = (long)g_list->data(line);
  if (i < 0) {   // group header - go to the first scheme below it
    if (line < g_list->size()) { g_list->value(line + 1); i = (long)g_list->data(line + 1); }
    else return;
  }
  g_scheme = (int)i;
  g_cur.scheme = g_schemes[i].id;
  preview();
}

static void save() {
  write_appearance(g_cur);
  notify_appearance_changed();
}

int panel_appearance() {
  g_cur = current_appearance();
  g_schemes = list_schemes();

  const int W = 600, H = 558;
  g_win = new FrameWindow(W, H, _("Appearance"));
  g_win->xclass("nirvana-alice");

  auto *t1 = new Fl_Box(15, 8, 220, 20, _("Color Scheme"));
  t1->labelfont(FONT_SYSTEM); t1->labelsize(SIZE_SYSTEM);
  t1->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  g_list = new Fl_Hold_Browser(15, 30, 220, 432);
  g_list->box(WELL_BOX);
  g_list->textfont(FONT_SMALL);
  g_list->textsize(SIZE_SMALL + 1);
  g_list->callback(list_cb);
  g_list->when(FL_WHEN_CHANGED);
  g_source = new Fl_Box(15, 464, 220, 20);
  g_source->labelsize(10);
  g_source->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  auto *t2 = new Fl_Box(250, 8, 220, 20, _("Preview"));
  t2->labelfont(FONT_SYSTEM); t2->labelsize(SIZE_SYSTEM);
  t2->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  auto *pw = new Fl_Box(250, 30, 335, 215);
  pw->box(WELL_BOX);
  new Preview(252, 32, 331, 211);

  g_style = new Fl_Choice(400, 256, 185, 22, _("Title bar:"));
  g_style->add(_("As in scheme|Stripes (Platinum)|Solid color|Gradient|Glass"));
  g_style->value(g_cur.style + 1);
  g_style->callback([](Fl_Widget *, void *) { g_cur.style = g_style->value() - 1; preview(); });

  g_bg = new Fl_Choice(400, 284, 185, 22, _("Desktop background:"));
  for (int i = 0; BACKGROUNDS[i]; i++) {
    g_bg->add(_(BACKGROUND_NAMES[i]));
    if (g_cur.background == BACKGROUNDS[i]) g_bg->value(i);
  }
  g_bg->callback([](Fl_Widget *, void *) { g_cur.background = BACKGROUNDS[g_bg->value()]; preview(); });

  auto *desk = new Swatch(400, 312, 40, 24);
  desk->label(_("Colors:"));
  desk->align(FL_ALIGN_LEFT);
  desk->tooltip(_("Desktop color (gradient start)"));
  desk->callback([](Fl_Widget *, void *) {
    uchar r, g, b;
    Fl::get_color(DESK_A, r, g, b);
    if (fl_color_chooser(_("Desktop color"), r, g, b, 1)) {
      g_cur.desk = (r << 16) | (g << 8) | b;
      preview();
    }
  });
  auto *desk2 = new Swatch(443, 312, 40, 24);
  desk2->second = true;
  desk2->tooltip(_("Second gradient color"));
  g_desk2 = desk2;
  desk2->callback([](Fl_Widget *, void *) {
    unsigned c = gradient_second(g_cur);
    uchar r = c >> 16, g = c >> 8 & 255, b = c & 255;
    if (fl_color_chooser(_("Second gradient color"), r, g, b, 1)) {
      g_cur.desk2 = (r << 16) | (g << 8) | b;
      preview();
    }
  });
  g_desk_default = new Fl_Button(488, 312, 97, 24, _("From scheme"));
  g_desk_default->labelsize(SIZE_SMALL + 1);
  g_desk_default->box(BUTTON_BOX); g_desk_default->down_box(BUTTON_DOWN);
  g_desk_default->callback([](Fl_Widget *, void *) { g_cur.desk = g_cur.desk2 = -1; preview(); });

  g_pattern = new Fl_Choice(400, 342, 185, 22, _("Desktop pattern:"));
  for (int i = 0; PATTERNS[i]; i++) {
    g_pattern->add(_(PATTERN_NAMES[i]));
    if (g_cur.pattern == PATTERNS[i]) g_pattern->value(i);
  }
  g_pattern->callback([](Fl_Widget *, void *) { g_cur.pattern = PATTERNS[g_pattern->value()]; preview(); });
  g_grad = new Fl_Choice(400, 342, 185, 22, _("Direction:"));
  for (int i = 0; GRADIENTS[i]; i++) {
    g_grad->add(_(GRADIENT_NAMES[i]));
    if (g_cur.gradient == GRADIENTS[i]) g_grad->value(i);
  }
  g_grad->callback([](Fl_Widget *, void *) { g_cur.gradient = GRADIENTS[g_grad->value()]; preview(); });

  // wallpaper: ready images (Nirvana Desktop, system ones, the Pictures folder) or any file
  g_wall = new Fl_Choice(400, 370, 185, 22, _("Wallpaper:"));
  g_walls.clear();
  g_walls.push_back("");
  for (auto &p : wallpaper_candidates()) g_walls.push_back(p);
  if (!g_cur.wallpaper.empty() &&
      std::find(g_walls.begin(), g_walls.end(), g_cur.wallpaper) == g_walls.end())
    g_walls.insert(g_walls.begin() + 1, g_cur.wallpaper);
  g_walls.push_back("*");
  auto add_item = [](const std::string &label) {
    int i = g_wall->add("x");             // add() treats '/' as a submenu - the text is set separately
    g_wall->replace(i, label.c_str());
  };
  for (size_t i = 0; i < g_walls.size(); i++) {
    if (i == 0) add_item(_("None (pattern only)"));
    else if (g_walls[i] == "*") add_item(_("Other File…"));
    else add_item(wallpaper_title(g_walls[i]));
    if (g_walls[i] == g_cur.wallpaper) g_wall->value((int)i);
  }
  if (g_walls.size() > 2) g_wall->mode(0, FL_MENU_DIVIDER);
  g_wall->mode((int)g_walls.size() - 2, FL_MENU_DIVIDER);
  g_wall->callback([](Fl_Widget *, void *) {
    int i = g_wall->value();
    if (i < 0 || i >= (int)g_walls.size()) return;
    if (g_walls[i] == "*") {
      std::string start = pictures_dir() + "/";
      localize_file_chooser();
      const char *f = fl_file_chooser(_("Choose Wallpaper"), _("Images (*.{png,jpg,jpeg,gif,bmp,svg,svgz,xpm})"),
                                      start.c_str(), 0);
      if (!f) {   // cancelled - back to the previous item
        auto it = std::find(g_walls.begin(), g_walls.end(), g_cur.wallpaper);
        g_wall->value(it == g_walls.end() ? 0 : (int)(it - g_walls.begin()));
        return;
      }
      if (!image_loadable(f)) {
        fl_message_title(_("Wallpaper"));
        fl_alert("%s", _("Could not open this image.\nThe file may be damaged or in an unsupported format."));
        auto it = std::find(g_walls.begin(), g_walls.end(), g_cur.wallpaper);
        g_wall->value(it == g_walls.end() ? 0 : (int)(it - g_walls.begin()));
        return;
      }
      g_cur.wallpaper = f;
      // a new file goes into the list (before "Other file…")
      if (std::find(g_walls.begin(), g_walls.end(), g_cur.wallpaper) == g_walls.end()) {
        g_walls.insert(g_walls.end() - 1, g_cur.wallpaper);
        int pos = (int)g_walls.size() - 2;
        g_wall->insert(pos, "x", 0, nullptr);
        g_wall->replace(pos, wallpaper_title(g_cur.wallpaper).c_str());
      }
      auto it = std::find(g_walls.begin(), g_walls.end(), g_cur.wallpaper);
      g_wall->value((int)(it - g_walls.begin()));
    } else {
      g_cur.wallpaper = g_walls[i];
    }
    preview();
  });
  g_wall_mode = new Fl_Choice(400, 398, 185, 22, _("Wallpaper layout:"));
  for (int i = 0; WALLPAPER_MODES[i]; i++) {
    g_wall_mode->add(_(WALLPAPER_MODE_NAMES[i]));
    if (g_cur.wallpaper_mode == WALLPAPER_MODES[i]) g_wall_mode->value(i);
  }
  g_wall_mode->callback([](Fl_Widget *, void *) {
    g_cur.wallpaper_mode = WALLPAPER_MODES[g_wall_mode->value()];
    preview();
  });

  g_icons = new Fl_Choice(400, 428, 185, 22, _("Icon style:"));
  g_icons->add(_("Retro|Platinum"));
  g_icons->value(g_cur.icons == "platinum" ? 1 : 0);
  g_icons->callback([](Fl_Widget *, void *) {
    g_cur.icons = g_icons->value() == 1 ? "platinum" : "retro";
    preview();
  });

  // Interface scale: all Nirvana Desktop programs at once; the desktop and the window manager
  // start again on a change (like Special > Reset Nirvana).
  g_scale = new Fl_Choice(400, 456, 185, 22, _("Interface scale:"));
  for (int i = 0; SCALES[i] >= 0; i++) {
    if (SCALES[i] == 0) {
      int a = (int)(configured_scale(Appearance{}) * 100 + 0.5);
      g_scale->add(fmt(_("Automatic (%1%)"), std::to_string(a)).c_str());
    } else {
      g_scale->add((std::to_string(SCALES[i]) + "%").c_str());
    }
    if (SCALES[i] == g_cur.scale) g_scale->value(i);
  }
  if (g_scale->value() < 0) g_scale->value(0);
  g_scale->tooltip(_("Larger text, icons and windows on high-resolution screens.\n"
                     "After applying, the desktop restarts (application windows stay open)."));
  g_scale->callback([](Fl_Widget *, void *) { g_cur.scale = SCALES[std::max(0, g_scale->value())]; });

  g_anim = new Fl_Check_Button(396, 484, 190, 22, _("Window animations"));
  g_anim->value(g_cur.animations);
  g_anim->callback([](Fl_Widget *, void *) { g_cur.animations = g_anim->value(); });

  auto button = [](int X, const char *l) {
    auto *b = new Fl_Button(X, H - 36, 86, 24, l);
    b->box(BUTTON_BOX); b->down_box(BUTTON_DOWN);
    return b;
  };
  button(311, _("Cancel"))->callback([](Fl_Widget *, void *) { g_win->hide(); });
  button(405, _("Apply"))->callback([](Fl_Widget *, void *) { save(); });
  auto *ok = button(499, _("OK"));
  ok->shortcut(FL_Enter);
  ok->callback([](Fl_Widget *, void *) { save(); g_win->hide(); });

  for (Fl_Choice *c : {g_style, g_pattern, g_wall, g_wall_mode, g_bg, g_grad, g_icons, g_scale}) {
    c->labelsize(SIZE_SMALL + 1);
    c->textsize(SIZE_SMALL + 1);
  }
  desk->labelsize(SIZE_SMALL + 1);
  g_anim->labelsize(SIZE_SMALL + 1);
  g_win->end();
  g_win->finish();   // under GNOME/KDE: our own Platinum frame

  fill_list();
  preview();
  // In the middle of the work area (below the Nirvana Desktop or GNOME menu bar).
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  g_win->place(sx + (sw - W) / 2, sy + std::max(MENUBAR_H + 10 + g_win->oy(), (sh - H) / 3));
  g_win->show();
  return Fl::run();
}
