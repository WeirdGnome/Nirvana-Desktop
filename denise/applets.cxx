// LisaOS - aplety paska menu: głośność i zasilanie
#include "applets.h"

#include "audio.h"
#include "platinum.h"
#include "power.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Slider.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <X11/XF86keysym.h>
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

#include <algorithm>
#include <cstring>
#include <memory>

using namespace plat;

namespace applets {

static Host g_host;
static audio::State g_audio;
static power::State g_power;

// ---------------------------------------------------------------------------
// Okienko pod ikoną paska menu (jak menu, ale z suwakami)
// ---------------------------------------------------------------------------
class Popup : public Fl_Double_Window {
public:
  std::function<void()> on_close;
  Popup(int W, int H) : Fl_Double_Window(W, H) {
    border(0);
    set_override();
    color(GRAY);
    end();
    resizable(nullptr);   // zmiana rozmiaru po zbudowaniu treści nie może jej skalować
  }
  void open_at(int X, int Y) {
    int sx, sy, sw, sh;
    Fl::screen_xywh(sx, sy, sw, sh, X, Y);
    position(std::clamp(X, sx, sx + sw - w()), Y);
    show();
    Fl::grab(this);
  }
  void close() {
    if (Fl::grab() == this) Fl::grab(nullptr);
    hide();
    if (on_close) on_close();
  }
  std::function<void()> track_hover;   // ustawiane niżej - podświetlanie wierszy menu
  int handle(int e) override {
    if (e == FL_PUSH) {
      int ex = Fl::event_x(), ey = Fl::event_y();
      if (ex < 0 || ey < 0 || ex >= w() || ey >= h()) { close(); return 1; }
    }
    if ((e == FL_MOVE || e == FL_DRAG || e == FL_ENTER || e == FL_LEAVE) && track_hover) {
      int r = Fl_Double_Window::handle(e);
      track_hover();
      return r ? r : 1;
    }
    if (e == FL_KEYBOARD && Fl::event_key() == FL_Escape) { close(); return 1; }
    return Fl_Double_Window::handle(e);
  }
  void draw() override {
    // ramka jak w menu Platinum: czarny obrys i cień
    fl_color(GRAY);
    fl_rectf(0, 0, w(), h());
    fl_color(DARK);
    fl_rect(0, 0, w() - 1, h() - 1);
    fl_color(SHADOW);
    fl_yxline(w() - 1, 1, h() - 1);
    fl_xyline(1, h() - 1, w() - 1);
    fl_color(LIGHT);
    fl_xyline(1, 1, w() - 3);
    fl_yxline(1, 1, h() - 3);
    for (int i = 0; i < children(); i++) {
      draw_child(*child(i));
      draw_outside_label(*child(i));
    }
  }
};

// Fl_Choice zamyka własne menu przez Fl::grab(0) - po nim przywracamy chwyt okienka.
class GrabChoice : public Fl_Choice {
public:
  GrabChoice(int X, int Y, int W, int H, const char *l = nullptr) : Fl_Choice(X, Y, W, H, l) {
    textfont(FONT_SMALL);
    textsize(SIZE_SMALL + 1);
    labelsize(SIZE_SMALL + 1);
  }
  int handle(int e) override {
    int r = Fl_Choice::handle(e);
    if ((e == FL_PUSH || e == FL_KEYBOARD) && window() && window()->shown()) Fl::grab(window());
    return r;
  }
};

// Suwak Platinum: rowek, wypełnienie w kolorze zaznaczenia, uchwyt.
class PSlider : public Fl_Slider {
public:
  PSlider(int X, int Y, int W, int H) : Fl_Slider(X, Y, W, H) {
    type(FL_HOR_SLIDER);
    bounds(0, 100);
    step(1);
    box(FL_NO_BOX);
  }
  void draw() override {
    int tw = 11, th = h() - 2;
    double f = (value() - minimum()) / (maximum() - minimum());
    int tx = x() + (int)((w() - tw) * f);
    int gy = y() + h() / 2 - 3;
    fl_draw_box(WELL_BOX, x(), gy, w(), 6, active_r() ? CONTENT : GRAY);
    if (active_r()) { fl_color(SEL); fl_rectf(x() + 2, gy + 2, std::max(0, tx - x()), 2); }
    fl_draw_box(THUMB_BOX, tx, y() + 1, tw, th, active_r() ? HILITE : GRAY);
    fl_color(SHADOW);
    for (int i = 0; i < 3; i++) fl_yxline(tx + 3 + i * 2, y() + 5, y() + th - 4);
  }
  int handle(int e) override {
    if (e == FL_MOUSEWHEEL) {
      value(std::clamp(value() - Fl::event_dy() * 5, minimum(), maximum()));
      do_callback();
      redraw();
      return 1;
    }
    return Fl_Slider::handle(e);
  }
};

// Pozycja jak w menu: tekst, podświetlenie pod myszą, klik wywołuje akcję.
class MenuRow : public Fl_Button {
public:
  MenuRow(int X, int Y, int W, const char *l) : Fl_Button(X, Y, W, 20, l) {
    box(FL_FLAT_BOX);
    labelfont(FONT_SYSTEM);
    labelsize(SIZE_SYSTEM);
    align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  }
  // Podświetlenie ustawia okienko (Popup) według położenia myszy: przy chwycie
  // myszy (Fl::grab) FL_LEAVE nie zawsze dochodzi i wiersze zostawały podświetlone.
  void hover(bool h) { if (h != hover_) { hover_ = h; redraw(); } }
  int handle(int e) override {
    if (e == FL_ENTER || e == FL_LEAVE) return 1;
    return Fl_Button::handle(e);
  }
  void draw() override {
    bool hl = (hover_ || value()) && active_r();
    fl_color(hl ? SEL : GRAY);
    fl_rectf(x(), y(), w(), h());
    fl_font(labelfont(), labelsize());
    // nieaktywne: pół drogi między tekstem a tłem - czytelne też w ciemnych schematach
    fl_color(!active_r() ? fl_color_average(TEXT, GRAY, 0.45f) : hl ? SEL_TEXT : TEXT);
    fl_draw(label(), x() + 14, y(), w() - 18, h(), FL_ALIGN_LEFT);
  }
private:
  bool hover_ = false;
};

// Wiersz pod myszą podświetlony, pozostałe nie.
static void update_hover(Fl_Window *w) {
  int ex = Fl::event_x(), ey = Fl::event_y();
  bool inside = ex >= 0 && ey >= 0 && ex < w->w() && ey < w->h();
  for (int i = 0; i < w->children(); i++)
    if (auto *r = dynamic_cast<MenuRow *>(w->child(i)))
      r->hover(inside && ex >= r->x() && ex < r->x() + r->w() && ey >= r->y() && ey < r->y() + r->h());
}

class Sep : public Fl_Widget {
public:
  Sep(int X, int Y, int W) : Fl_Widget(X, Y, W, 6) {}
  void draw() override {
    fl_color(SHADOW); fl_xyline(x(), y() + 2, x() + w() - 1);
    fl_color(LIGHT); fl_xyline(x(), y() + 3, x() + w() - 1);
  }
};

static Fl_Box *text_box(int X, int Y, int W, const char *t, bool bold = false, int H = 18) {
  auto *b = new Fl_Box(X, Y, W, H);
  b->copy_label(t);
  b->labelfont(bold ? FONT_SYSTEM : FONT_SMALL);
  b->labelsize(bold ? SIZE_SYSTEM : SIZE_SMALL + 1);
  b->labelcolor(TEXT);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  return b;
}

template <class F>
static Fl_Check_Button *check(int X, int Y, int W, const char *t, bool v, F cb) {
  auto *c = new Fl_Check_Button(X, Y, W, 20, t);
  c->labelsize(SIZE_SMALL + 1);
  c->value(v);
  c->callback([](Fl_Widget *w, void *d) { (*(F *)d)(((Fl_Check_Button *)w)->value()); }, new F(cb));
  return c;
}

// ---------------------------------------------------------------------------
// OSD: pasek głośności/jasności na dole ekranu
// ---------------------------------------------------------------------------
class Osd : public Fl_Double_Window {
public:
  enum Kind { Volume, Brightness } kind = Volume;
  int value = 0;
  bool muted = false;
  Osd() : Fl_Double_Window(250, 64) { border(0); set_override(); end(); }
  void draw() override {
    fl_draw_box(UP_BOX, 0, 0, w(), h(), GRAY);
    fl_color(DARK);
    fl_rect(0, 0, w(), h());
    if (kind == Volume) draw_speaker(12, 13, muted ? 0 : 3, muted, TEXT, 2.6);
    else draw_sun(12, 14, TEXT, 2.6);
    int segs = 16, sx = 66, sw = 9, gap = 2, sy = 22, sh = 20;
    int lit = muted ? 0 : (value * segs + 50) / 100;
    for (int i = 0; i < segs; i++) {
      fl_draw_box(i < lit ? THUMB_BOX : WELL_BOX, sx + i * (sw + gap), sy, sw, sh, i < lit ? SEL : CONTENT);
    }
  }
  void pop(Kind k, int v, bool m) {
    kind = k; value = v; muted = m;
    int sx, sy, sw, sh;
    Fl::screen_xywh(sx, sy, sw, sh);
    position(sx + (sw - w()) / 2, sy + sh - h() - 90);
    show();
    redraw();
    Fl::remove_timeout(hide_cb, this);
    Fl::add_timeout(1.5, hide_cb, this);
  }
  static void hide_cb(void *d) { ((Osd *)d)->hide(); }
};
static Osd *g_osd;
static void osd(Osd::Kind k, int v, bool m) {
  if (!g_osd) g_osd = new Osd();
  g_osd->pop(k, v, m);
}

// ---------------------------------------------------------------------------
// Głośność
// ---------------------------------------------------------------------------
class VolumeApplet;
static VolumeApplet *g_vol;

class VolumeApplet : public Fl_Widget {
public:
  VolumeApplet(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) { g_vol = this; }
  Popup *pop = nullptr;
  std::string layout_key;   // co jest w okienku (urządzenia, strumienie) - zmiana = przebudowa
  struct Row { PSlider *slider; Fl_Box *pct; Fl_Check_Button *mute; };
  Row out{}, in{};
  std::vector<std::pair<int, Row>> streams;   // indeks strumienia -> wiersz

  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_draw_box(MENUBAR_BOX, x(), y(), w(), h(), GRAY);
    fl_pop_clip();
    bool open = pop && pop->shown();
    if (open) { fl_color(SEL); fl_rectf(x(), y(), w(), h() - 1); }
    Fl_Color c = open ? SEL_TEXT : TEXT;
    const audio::Device *d = g_audio.sink();
    int lvl = !d ? 0 : d->volume == 0 ? 0 : d->volume < 34 ? 1 : d->volume < 67 ? 2 : 3;
    draw_speaker(x() + (w() - 16) / 2, y() + 3, lvl, !d || d->mute || d->volume == 0,
                 d ? c : fl_color_average(c, GRAY, 0.4f));
  }
  int handle(int e) override {
    if (e == FL_PUSH) {
      if (pop && pop->shown()) { pop->close(); return 1; }
      open_popup();
      return 1;
    }
    if (e == FL_MOUSEWHEEL) {
      audio::step_volume(-Fl::event_dy() * 5);
      refresh(true);
      return 1;
    }
    if (e == FL_ENTER || e == FL_LEAVE) return 1;
    return Fl_Widget::handle(e);
  }

  static std::string key_of(const audio::State &s) {
    std::string k = std::to_string((int)s.backend) + "|" + s.def_sink + "|" + s.def_source + "|";
    for (auto &d : s.sinks) k += d.name + ":" + d.active_port + ",";
    for (auto &d : s.sources) k += d.name + ":" + d.active_port + ",";
    for (auto &t : s.streams) k += std::to_string(t.index) + "@" + std::to_string(t.sink) + ",";
    return k;
  }

  void build() {
    const int W = 300;
    pop->begin();
    pop->clear();             // clear() ustawia resizable(this) - wyłączamy z powrotem
    pop->resizable(nullptr);
    pop->begin();
    int y = 8;
    streams.clear();
    out = in = Row{};
    const audio::State &s = g_audio;
    if (s.backend == audio::State::Brak) {
      text_box(12, y, W - 24, "Dźwięk", true);
      y += 22;
      auto *b = text_box(12, y, W - 24, s.problem.c_str(), false, 36);
      b->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
      y += 44;
    } else {
      auto device_rows = [&](bool output, Row &row, int &y) {
        const audio::Device *d = output ? s.sink() : s.source();
        const auto &list = output ? s.sinks : s.sources;
        text_box(12, y, 160, output ? "Głośność" : "Mikrofon", true);
        if (!d) {
          y += 22;
          text_box(12, y, W - 24, output ? "Brak wyjścia dźwięku." : "Brak mikrofonu.");
          y += 24;
          return;
        }
        std::string dev = d->name;
        row.mute = check(W - 92, y - 1, 84, "Wycisz", d->mute, [output, dev](bool v) {
          audio::set_mute(g_audio, output, dev, v);
        });
        y += 22;
        row.slider = new PSlider(12, y, W - 70, 18);
        row.slider->value(std::min(100, d->volume));
        row.slider->callback([](Fl_Widget *w, void *o) {
          bool output = o != nullptr;
          const audio::Device *d = output ? g_audio.sink() : g_audio.source();
          if (!d) return;
          int v = (int)((PSlider *)w)->value();
          audio::set_volume(g_audio, output, d->name, v);
          if (output && d->mute && v > 0) audio::set_mute(g_audio, true, d->name, false);
          Row &r = output ? g_vol->out : g_vol->in;
          if (r.pct) { r.pct->copy_label((std::to_string(v) + "%").c_str()); r.pct->redraw(); }
        }, output ? (void *)1 : nullptr);
        row.pct = text_box(W - 52, y, 44, (std::to_string(d->volume) + "%").c_str());
        row.pct->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        y += 26;
        // wybór urządzenia (i portu, gdy ma kilka: głośniki/słuchawki)
        if (s.backend == audio::State::Pulse && (list.size() > 1 || d->ports.size() > 1)) {
          auto *ch = new GrabChoice(82, y, W - 94, 22, output ? "Wyjście:" : "Wejście:");
          static std::vector<std::pair<std::string, std::string>> opts_out, opts_in;   // urządzenie, port
          auto &opts = output ? opts_out : opts_in;
          opts.clear();
          int sel = 0;
          for (auto &dv : list) {
            auto esc = [](std::string t) {
              std::string r;
              for (char c : t) { if (c == '/' || c == '\\' || c == '&' || c == '_') r += '\\'; r += c; }
              return r;
            };
            if (dv.ports.size() > 1) {
              for (auto &p : dv.ports) {
                if (!p.available && !(dv.name == d->name && p.name == dv.active_port)) continue;
                if (dv.name == d->name && p.name == dv.active_port) sel = (int)opts.size();
                opts.push_back({dv.name, p.name});
                ch->add(esc(dv.desc + " — " + p.desc).c_str());
              }
            } else {
              if (dv.name == d->name) sel = (int)opts.size();
              opts.push_back({dv.name, ""});
              ch->add(esc(dv.desc).c_str());
            }
          }
          ch->value(sel);
          ch->callback([](Fl_Widget *w, void *o) {
            bool output = o != nullptr;
            auto &opts = output ? opts_out : opts_in;
            int v = ((Fl_Choice *)w)->value();
            if (v < 0 || v >= (int)opts.size()) return;
            const audio::Device *cur = output ? g_audio.sink() : g_audio.source();
            if (!cur || cur->name != opts[v].first) audio::set_default(output, opts[v].first);
            if (!opts[v].second.empty()) audio::set_port(output, opts[v].first, opts[v].second);
          }, output ? (void *)1 : nullptr);
          y += 30;
        }
      };
      device_rows(true, out, y);
      new Sep(8, y, W - 16);
      y += 10;
      device_rows(false, in, y);
      if (!s.streams.empty()) {
        new Sep(8, y, W - 16);
        y += 10;
        text_box(12, y, W - 24, "Aplikacje", true);
        y += 24;
        for (auto &t : s.streams) {
          if (streams.size() >= 8) break;
          Row r{};
          text_box(12, y, 100, t.app.c_str());
          int idx = t.index;
          r.mute = new Fl_Check_Button(112, y, 20, 20);
          r.mute->value(!t.mute);   // zaznaczone = słychać
          r.mute->tooltip("Słychać tę aplikację");
          r.mute->callback([](Fl_Widget *w, void *i) {
            audio::set_stream_mute((int)(long)i, !((Fl_Check_Button *)w)->value());
          }, (void *)(long)idx);
          r.slider = new PSlider(136, y + 1, W - 148, 18);
          r.slider->value(std::min(100, t.volume));
          r.slider->callback([](Fl_Widget *w, void *i) {
            audio::set_stream_volume((int)(long)i, (int)((PSlider *)w)->value());
          }, (void *)(long)idx);
          streams.push_back({idx, r});
          y += 24;
        }
      }
    }
    pop->end();
    pop->size(W, y + 6);
    layout_key = key_of(s);
  }

  // Nowy stan z serwera: przy tych samych urządzeniach tylko wartości, inaczej przebudowa.
  void sync() {
    redraw();
    if (!pop || !pop->shown()) return;
    if (key_of(g_audio) != layout_key) {
      if (Fl::pushed()) return;   // nie przebudowujemy w trakcie przeciągania
      int X = pop->x(), Y = pop->y();
      build();
      pop->position(X, Y);
      pop->redraw();
      return;
    }
    auto upd = [](Row &r, int vol, bool mute, bool mute_is_on) {
      if (r.slider && Fl::pushed() != r.slider) { r.slider->value(std::min(100, vol)); r.slider->redraw(); }
      if (r.pct) { r.pct->copy_label((std::to_string(vol) + "%").c_str()); r.pct->redraw(); }
      if (r.mute) { r.mute->value(mute_is_on ? mute : !mute); r.mute->redraw(); }
    };
    if (auto *d = g_audio.sink()) upd(out, d->volume, d->mute, true);
    if (auto *d = g_audio.source()) upd(in, d->volume, d->mute, true);
    for (auto &[idx, r] : streams)
      for (auto &t : g_audio.streams) if (t.index == idx) upd(r, t.volume, t.mute, false);
  }

  void open_popup() {
    g_audio = audio::query();
    if (!pop) {
      pop = new Popup(300, 100);
      pop->on_close = [this] { redraw(); };
    }
    build();
    pop->open_at(x_root() + w() - pop->w(), window()->y_root() + MENUBAR_H);
    redraw();
  }

  int x_root() const { return window()->x_root() + x(); }

  void refresh(bool show_osd) {
    g_audio = audio::query();
    sync();
    if (show_osd)
      if (const audio::Device *d = g_audio.sink()) osd(Osd::Volume, d->volume, d->mute);
  }
};

Fl_Widget *make_volume(int X, int Y, int W, int H) { return new VolumeApplet(X, Y, W, H); }

// ---------------------------------------------------------------------------
// Zasilanie
// ---------------------------------------------------------------------------
class PowerApplet;
static PowerApplet *g_pow;

class PowerApplet : public Fl_Widget {
public:
  PowerApplet(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) { g_pow = this; }
  Popup *pop = nullptr;
  PSlider *bright = nullptr;
  Fl_Box *bat_label = nullptr;

  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_draw_box(MENUBAR_BOX, x(), y(), w(), h(), GRAY);
    fl_pop_clip();
    bool open = pop && pop->shown();
    if (open) { fl_color(SEL); fl_rectf(x(), y(), w(), h() - 1); }
    Fl_Color c = open ? SEL_TEXT : TEXT;
    // do prawej - aplety stoją ciasno, bez dziury przed ikoną głośności
    int iw = g_power.percent >= 0 ? 23 : 14;
    int ix = x() + w() - 4 - iw;
    if (power::projection()) draw_display(ix - 14, y() + 5, c);   // tryb projekcji: monitor przed ikoną
    if (g_power.percent >= 0) draw_battery(ix, y() + 4, g_power.percent, g_power.charging, c);
    else draw_plug(ix, y() + 3, c);
  }
  int handle(int e) override {
    if (e == FL_PUSH) {
      if (pop && pop->shown()) { pop->close(); return 1; }
      open_popup();
      return 1;
    }
    if (e == FL_ENTER || e == FL_LEAVE) return 1;
    return Fl_Widget::handle(e);
  }

  static void act(Fl_Widget *w, void *d);
  void build() {
    const int W = 290;
    pop->begin();
    pop->clear();             // clear() ustawia resizable(this) - wyłączamy z powrotem
    pop->resizable(nullptr);
    pop->begin();
    int y = 8;
    std::string head = g_power.percent >= 0 ? "Bateria: " + power::battery_text(g_power) : "Zasilanie sieciowe";
    bat_label = text_box(12, y, W - 24, head.c_str(), true);
    y += 24;
    if (g_power.brightness >= 0) {
      struct Sun : Fl_Widget {
        Sun(int X, int Y) : Fl_Widget(X, Y, 14, 14) {}
        void draw() override { draw_sun(x(), y(), TEXT); }
      };
      new Sun(12, y + 2);
      bright = new PSlider(34, y, W - 46, 18);
      bright->bounds(1, 100);
      bright->value(g_power.brightness);
      bright->callback([](Fl_Widget *w, void *) { power::set_brightness(g_power, (int)((PSlider *)w)->value()); });
      y += 28;
    } else {
      bright = nullptr;
    }
    if (!g_power.profiles.empty()) {
      auto *ch = new GrabChoice(70, y, W - 82, 22, "Profil:");
      int sel = 0;
      for (size_t i = 0; i < g_power.profiles.size(); i++) {
        ch->add(power::profile_name(g_power.profiles[i]));
        if (g_power.profiles[i] == g_power.profile) sel = (int)i;
      }
      ch->value(sel);
      ch->callback([](Fl_Widget *w, void *) {
        int v = ((Fl_Choice *)w)->value();
        if (v >= 0 && v < (int)g_power.profiles.size()) power::set_profile(g_power.profiles[v]);
      });
      y += 30;
    }
    new Sep(8, y, W - 16);
    y += 10;
    auto *pj = check(10, y, W - 20, "Tryb projekcji", power::projection(), [](bool on) {
      power::set_projection(on);
      if (g_pow) g_pow->redraw();
    });
    pj->labelfont(FONT_SYSTEM);
    pj->labelsize(SIZE_SYSTEM);
    y += 22;
    auto *hint = text_box(30, y, W - 40, "Ekran nie wygaśnie ani się nie zablokuje, a komputer nie uśnie.", false, 30);
    hint->labelsize(SIZE_SMALL);
    hint->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    y += 32;
    new Sep(8, y, W - 16);
    y += 8;
    struct Item { const char *label; long id; bool ok; };
    std::string lock = power::locker();
    Item items[] = {
        {"Zablokuj ekran", 1, !lock.empty()},
        {"Uśpij", 2, g_power.can_suspend},
        {"Hibernuj", 3, g_power.can_hibernate},
    };
    for (auto &it : items) {
      if (it.id == 3 && !g_power.can_hibernate) continue;
      auto *r = new MenuRow(2, y, W - 4, it.label);
      r->callback(act, (void *)it.id);
      if (!it.ok) r->deactivate();
      if (it.id == 1 && lock.empty()) r->tooltip("Zainstaluj program blokady: xsecurelock, i3lock albo slock.");
      y += 20;
    }
    new Sep(8, y, W - 16);
    y += 8;
    Item items2[] = {
        {"Wyloguj…", 4, true},
        {"Uruchom ponownie…", 5, g_power.can_reboot},
        {"Wyłącz…", 6, g_power.can_poweroff},
    };
    for (auto &it : items2) {
      auto *r = new MenuRow(2, y, W - 4, it.label);
      r->callback(act, (void *)it.id);
      if (!it.ok) r->deactivate();
      y += 20;
    }
    new Sep(8, y, W - 16);
    y += 8;
    auto *r = new MenuRow(2, y, W - 4, "Ustawienia zasilania…");
    r->callback(act, (void *)7L);
    y += 22;
    pop->end();
    pop->size(W, y + 4);
  }

  void open_popup() {
    g_power = power::query();
    if (!pop) {
      pop = new Popup(290, 100);
      pop->on_close = [this] {
        for (int i = 0; i < pop->children(); i++)   // po zamknięciu bez podświetleń
          if (auto *r = dynamic_cast<MenuRow *>(pop->child(i))) r->hover(false);
        redraw();
      };
      pop->track_hover = [this] { update_hover(pop); };
    }
    build();
    pop->open_at(window()->x_root() + x() + w() - pop->w(), window()->y_root() + MENUBAR_H);
    redraw();
  }

  void sync() {
    redraw();
    if (!pop || !pop->shown()) return;
    if (bat_label) {
      std::string head = g_power.percent >= 0 ? "Bateria: " + power::battery_text(g_power) : "Zasilanie sieciowe";
      bat_label->copy_label(head.c_str());
      bat_label->redraw();
    }
    if (bright && Fl::pushed() != bright && g_power.brightness >= 0) { bright->value(g_power.brightness); bright->redraw(); }
  }
};

static void confirm(const char *title, const char *text, const char *yes, std::function<void()> f) {
  if (g_host.dialog) g_host.dialog(title, text, {{"Anuluj", nullptr}, {yes, f}});
  else f();
}
static void report(const std::string &err) {
  if (!err.empty() && g_host.dialog) g_host.dialog("Zasilanie", err, {{"OK", nullptr}});
}

void PowerApplet::act(Fl_Widget *, void *d) {
  long id = (long)d;
  if (g_pow && g_pow->pop) g_pow->pop->close();
  switch (id) {
    case 1: power::lock_screen(); break;
    case 2:
      if (power::projection()) { report("Tryb projekcji jest włączony - wyłącz go, żeby uśpić komputer."); break; }
      if (g_host.lisaos_session) power::lock_screen();   // po wybudzeniu ekran jest zablokowany
      report(power::suspend());
      break;
    case 3:
      if (power::projection()) { report("Tryb projekcji jest włączony - wyłącz go, żeby hibernować komputer."); break; }
      report(power::hibernate());
      break;
    case 4:
      confirm("Wyloguj", "Zakończyć sesję? Otwarte programy zostaną zamknięte.", "Wyloguj", [] {
        if (g_host.logout) g_host.logout();
      });
      break;
    case 5:
      confirm("Uruchom ponownie", "Uruchomić komputer ponownie?", "Uruchom", [] {
        if (g_host.before_shutdown) g_host.before_shutdown();
        report(power::reboot());
      });
      break;
    case 6:
      confirm("Wyłącz", "Wyłączyć komputer?", "Wyłącz", [] {
        if (g_host.before_shutdown) g_host.before_shutdown();
        report(power::poweroff());
      });
      break;
    case 7:
      if (g_host.open_power_panel) g_host.open_power_panel();
      break;
  }
}

Fl_Widget *make_power(int X, int Y, int W, int H) { return new PowerApplet(X, Y, W, H); }

// ---------------------------------------------------------------------------
// Klawisze multimedialne, bezczynność, odświeżanie
// ---------------------------------------------------------------------------
static KeyCode k_up, k_down, k_mute, k_micmute, k_bup, k_bdown;
static int g_ss_event = -1;
static power::IdleSettings g_idle;
static int g_last_sleep_idle = 0;
static bool g_warned_low = false, g_critical_done = false;

static int ignore_errors(Display *, XErrorEvent *) { return 0; }

static void grab_keys() {
  Display *d = fl_display;
  Window root = DefaultRootWindow(d);
  auto kc = [&](KeySym s) { return XKeysymToKeycode(d, s); };
  k_up = kc(XF86XK_AudioRaiseVolume);
  k_down = kc(XF86XK_AudioLowerVolume);
  k_mute = kc(XF86XK_AudioMute);
  k_micmute = kc(XF86XK_AudioMicMute);
  k_bup = kc(XF86XK_MonBrightnessUp);
  k_bdown = kc(XF86XK_MonBrightnessDown);
  // GNOME/KDE trzymają te klawisze same - wtedy XGrabKey kończy się BadAccess i nic nie robimy
  XSync(d, False);
  auto old = XSetErrorHandler(ignore_errors);
  for (KeyCode k : {k_up, k_down, k_mute, k_micmute, k_bup, k_bdown})
    if (k) XGrabKey(d, k, AnyModifier, root, True, GrabModeAsync, GrabModeAsync);
  XSync(d, False);
  XSetErrorHandler(old);
}

static void apply_settings() {
  g_idle = power::read_idle();
  if (g_host.lisaos_session) power::apply_idle(g_idle);
}

static int x_events(void *event, void *) {
  XEvent *e = (XEvent *)event;
  if (e->type == KeyPress && e->xkey.window == DefaultRootWindow(fl_display)) {
    KeyCode k = e->xkey.keycode;
    if (k == k_up || k == k_down) {
      audio::step_volume(k == k_up ? 5 : -5);
      if (g_vol) g_vol->refresh(true);
      return 1;
    }
    if (k == k_mute) {
      audio::toggle_mute(true);
      if (g_vol) g_vol->refresh(true);
      return 1;
    }
    if (k == k_micmute) { audio::toggle_mute(false); if (g_vol) g_vol->refresh(false); return 1; }
    if (k == k_bup || k == k_bdown) {
      power::step_brightness(k == k_bup ? 10 : -10);
      g_power = power::query();
      if (g_power.brightness >= 0) osd(Osd::Brightness, g_power.brightness, false);
      if (g_pow) g_pow->sync();
      return 1;
    }
  }
  if (g_ss_event >= 0 && e->type == g_ss_event) {
    // wygaszacz X się włączył: blokada (jak xss-lock), chyba że trwa projekcja
    auto *se = (XScreenSaverNotifyEvent *)e;
    if (se->state == ScreenSaverOn && g_host.lisaos_session && g_idle.lock_on_blank && !power::projection())
      power::lock_screen();
    return 0;
  }
  if (e->type == PropertyNotify && e->xproperty.window == DefaultRootWindow(fl_display) &&
      e->xproperty.atom == XInternAtom(fl_display, power::SETTINGS_ATOM, False)) {
    apply_settings();
  }
  return 0;
}

// Co 20 s: bateria, jasność, usypianie po bezczynności, ostrzeżenia.
static void power_tick(void *) {
  g_power = power::query();
  if (g_pow) g_pow->sync();
  if (g_host.lisaos_session && !power::projection()) {
    int limit = g_power.on_ac ? g_idle.sleep_ac_min : g_idle.sleep_battery_min;
    int idle = power::idle_seconds();
    if (idle < g_last_sleep_idle) g_last_sleep_idle = 0;   // był ruch od ostatniego uśpienia
    if (limit > 0 && idle >= limit * 60 && !g_last_sleep_idle && g_power.can_suspend) {
      g_last_sleep_idle = idle;
      if (g_idle.lock_on_blank) power::lock_screen();
      power::suspend();
    }
  }
  // bateria: ostrzeżenie przy 10%, przy 3% hibernacja albo uśpienie
  if (g_power.percent >= 0 && !g_power.on_ac) {
    if (g_power.percent <= 10 && !g_warned_low && g_host.dialog) {
      g_warned_low = true;
      g_host.dialog("Bateria", "Bateria jest prawie wyczerpana (" + std::to_string(g_power.percent) +
                                   "%). Podłącz zasilacz.", {{"OK", nullptr}});
    }
    if (g_power.percent <= 3 && !g_critical_done && g_idle.critical_action && g_host.lisaos_session) {
      g_critical_done = true;
      if (g_idle.critical_action == 1 && g_power.can_hibernate) power::hibernate();
      else if (g_power.can_suspend) power::suspend();
    }
  } else {
    g_warned_low = g_critical_done = false;
  }
  Fl::repeat_timeout(20.0, power_tick);
}

void init(const Host &host) {
  g_host = host;
  fl_open_display();
  grab_keys();
  int ev, er;
  if (XScreenSaverQueryExtension(fl_display, &ev, &er)) {
    g_ss_event = ev + ScreenSaverNotify;
    XScreenSaverSelectInput(fl_display, DefaultRootWindow(fl_display), ScreenSaverNotifyMask);
  }
  Fl::add_system_handler(x_events, nullptr);
  apply_settings();
  g_power = power::query();
  Fl::add_timeout(20.0, power_tick);
  audio::watch([] { if (g_vol) g_vol->refresh(false); });
}

}  // namespace applets
