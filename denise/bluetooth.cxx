// Nirvana Desktop - the Bluetooth applet in the menu bar
//
// The rune icon (dimmed when Bluetooth is off, with a dot when a device is connected).
// A click opens a menu: turning Bluetooth on and off, the paired devices with a check mark
// at the connected ones (a click connects or disconnects, the battery level in brackets)
// and the Bluetooth panel (searching and pairing new devices).
// Without an adapter (or without bluez) the applet is hidden (width 0).
#include "applets.h"
#include "bt.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/fl_draw.H>

#include <set>

using namespace plat;

namespace applets {

namespace {

bt::State g_bt;
std::set<std::string> g_busy;   // devices being connected/disconnected
bool g_power_busy = false;
bool g_phase = false;           // blinking while busy
class BtMenu;
BtMenu *g_menu;

std::string menu_text(const std::string &s) {   // FLTK menus: '&' is a shortcut, '/' a submenu
  std::string o;
  for (char c : s) {
    if (c == '&') o += "&&";
    else o += c;
  }
  return o;
}

void report(const std::string &title, bool ok, const std::string &msg) {
  if (ok || msg.empty()) return;
  if (current_host().dialog) current_host().dialog(title, msg, {{_("OK"), [] {}}});
}

class BtMenu : public Fl_Menu_Button {
public:
  BtMenu(int X, int Y, int W, int H) : Fl_Menu_Button(X, Y, W, H) {
    box(FL_FLAT_BOX);
    down_box(FL_FLAT_BOX);
    textfont(FONT_SYSTEM);
    textsize(SIZE_SYSTEM);
    selection_color(SEL);
  }
  std::vector<std::string> labels_;   // the menu keeps pointers to these
  std::vector<std::string> macs_;
  std::vector<Fl_Menu_Item> items_;

  void build() {
    labels_.clear();
    macs_.clear();
    items_.clear();
    labels_.reserve(64);
    struct It { std::string l; Fl_Callback *cb; long v; int flags; };
    std::vector<It> its;
    if (!g_bt.powered) {
      its.push_back({_("Bluetooth: Off"), nullptr, 0, FL_MENU_INACTIVE});
      its.push_back({std::string("   ") + (g_power_busy ? _("Turning On…") : _("Turn Bluetooth On")), power_cb, 1,
                     g_power_busy ? FL_MENU_INACTIVE | FL_MENU_DIVIDER : FL_MENU_DIVIDER});
    } else {
      its.push_back({_("Bluetooth: On"), nullptr, 0, FL_MENU_INACTIVE});
      its.push_back({std::string("   ") + _("Turn Bluetooth Off"), power_cb, 0, FL_MENU_DIVIDER});
      int n = 0;
      for (auto &d : g_bt.devices) {
        if (!d.paired) continue;
        std::string l = menu_text(d.name);
        if (g_busy.count(d.mac)) l += d.connected ? std::string("  ") + _("(disconnecting…)") : std::string("  ") + _("(connecting…)");
        else if (d.connected && d.battery >= 0) l += "  (" + std::to_string(d.battery) + "%)";
        macs_.push_back(d.mac);
        int f = FL_MENU_TOGGLE | (d.connected ? FL_MENU_VALUE : 0) | (g_busy.count(d.mac) ? FL_MENU_INACTIVE : 0);
        its.push_back({l, device_cb, (long)macs_.size() - 1, f});
        n++;
      }
      if (!n) its.push_back({_("No paired devices"), nullptr, 0, FL_MENU_INACTIVE});
      if (!its.empty()) its.back().flags |= FL_MENU_DIVIDER;
      its.push_back({_("Add a Device…"), panel_cb, 1, 0});
    }
    its.push_back({_("Bluetooth Settings…"), panel_cb, 0, 0});
    for (auto &i : its) {
      labels_.push_back(i.l);
      Fl_Menu_Item m{};
      m.text = labels_.back().c_str();
      m.callback_ = i.cb;
      m.user_data_ = (void *)i.v;
      m.flags = i.flags;
      m.labelfont_ = FONT_SYSTEM;
      m.labelsize_ = SIZE_SYSTEM;
      items_.push_back(m);
    }
    items_.push_back(Fl_Menu_Item{});
    menu(items_.data());
  }

  static void power_cb(Fl_Widget *, void *v) {
    bool on = v != nullptr;
    g_power_busy = true;
    if (g_menu) g_menu->redraw();
    Fl::add_timeout(0.25, blink);
    bt::set_power(on, [](bool ok, const std::string &msg) {
      g_power_busy = false;
      report(_("Bluetooth"), ok, msg);
      refresh_bluetooth();
    });
  }
  static void device_cb(Fl_Widget *w, void *v) {
    auto *m = (BtMenu *)w;
    size_t i = (size_t)(long)v;
    if (i >= m->macs_.size()) return;
    std::string mac = m->macs_[i];
    bool connected = false;
    std::string name = mac;
    for (auto &d : g_bt.devices) if (d.mac == mac) { connected = d.connected; name = d.name; }
    g_busy.insert(mac);
    m->redraw();
    auto done = [mac, name](bool ok, const std::string &msg) {
      g_busy.erase(mac);
      report(fmt(_("Bluetooth: %1"), name), ok, msg);
      refresh_bluetooth();
    };
    if (connected) bt::disconnect(mac, done);
    else bt::connect(mac, done);
    Fl::add_timeout(0.25, blink);
  }
  static void panel_cb(Fl_Widget *, void *v) {
    if (current_host().open_panel) current_host().open_panel(v ? "bluetooth-add" : "bluetooth");
  }
  static void blink(void *) {
    g_phase = !g_phase;
    if (g_busy.empty() && !g_power_busy) { g_phase = false; if (g_menu) g_menu->redraw(); return; }
    if (g_menu) g_menu->redraw();
    Fl::repeat_timeout(0.25, blink);
  }

  int handle(int e) override {
    if (e == FL_PUSH) {
      build();
      open_ = true; redraw(); Fl::flush();
      int r = Fl_Menu_Button::handle(e);
      open_ = false; redraw();
      return r;
    }
    return Fl_Menu_Button::handle(e);
  }
  void draw() override {
    if (w() <= 0) return;
    fl_push_clip(x(), y(), w(), h());
    draw_box(MENUBAR_BOX, GRAY);
    if (open_) { fl_color(selection_color()); fl_rectf(x(), y(), w(), h() - 1); }
    Fl_Color on = open_ ? SEL_TEXT : TEXT;
    Fl_Color off = fl_color_average(on, open_ ? selection_color() : GRAY, 0.35f);
    bool busy = !g_busy.empty() || g_power_busy;
    bool lit = g_bt.powered ? !(busy && g_phase) : (busy && g_phase);
    int ix = x() + (w() - 9) / 2, iy = y() + 3;
    draw_bluetooth(ix, iy, lit ? on : off);
    if (g_bt.powered && g_bt.connected() > 0) {   // a connected device: a dot at the bottom right
      fl_color(on);
      fl_rectf(ix + 9, iy + 10, 3, 3);
    }
    fl_pop_clip();
  }
private:
  bool open_ = false;
};

}  // namespace

Fl_Widget *make_bluetooth(int X, int Y, int W, int H) {
  if (!bt::installed()) return nullptr;
  g_menu = new BtMenu(X, Y, W, H);
  g_bt = bt::query();
  if (!g_bt.available) g_menu->hide();
  bt::watch([] { refresh_bluetooth(); });
  return g_menu;
}

int bluetooth_width() { return g_menu && g_bt.available ? 22 : 0; }

void refresh_bluetooth() {
  if (!g_menu) return;
  bool was = g_bt.available;
  g_bt = bt::query();
  if (was != g_bt.available && current_host().relayout) current_host().relayout();
  if (g_bt.available) g_menu->show(); else g_menu->hide();
  g_menu->redraw();
}

}  // namespace applets
