// Nirvana Desktop - the printer applet in the menu bar
//
// Shown when CUPS has at least one printer or a job. The icon carries the number of
// print jobs; a red mark when the default printer has stopped (no paper, a jam...).
// The popup: the printers with their state, the jobs (each can be cancelled), a test
// page and the Printers panel. Without libcups this file compiles to nothing.
#include "applets.h"
#include "applet_ui.h"
#include "i18n.h"

#ifdef NIRVANA_PRINT
#include "print.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

namespace applets {

class PrinterApplet;
static PrinterApplet *g_pa = nullptr;

namespace {

printing::State g_ps;
std::string g_note;   // the result of the last action

bool trouble() {
  for (auto &p : g_ps.printers)
    if (p.is_default && (p.state == 5 || !p.accepting)) return true;
  return false;
}

}  // namespace

class PrinterApplet : public Fl_Widget {
public:
  Popup *pop = nullptr;
  PrinterApplet(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) { g_pa = this; }

  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_draw_box(MENUBAR_BOX, x(), y(), w(), h(), GRAY);
    bool open = pop && pop->shown();
    if (open) { fl_color(SEL); fl_rectf(x(), y(), w(), h() - 1); }
    Fl_Color c = open ? SEL_TEXT : TEXT;
    int ix = x() + 4, iy = y() + 3;
    // a printer: the paper on top, the body, the output slot
    if (glossy_glyphs()) {   // Nirvana 3D: a beige body, white paper, a green lamp
      glyph_color(0xFFFFFF, c); fl_rectf(ix + 4, iy, 8, 6);
      glyph_color(0xE2DCCB, c); fl_rectf(ix, iy + 5, 16, 7);
      glyph_color(0xFFFFFF, c); fl_xyline(ix + 1, iy + 6, ix + 14);
      glyph_color(0x8E887A, c); fl_xyline(ix + 1, iy + 10, ix + 14);
      glyph_color(0xFFFFFF, c); fl_rectf(ix + 4, iy + 10, 8, 4);
      glyph_color(0x3A7CE0, c); fl_xyline(ix + 5, iy + 12, ix + 10);
      glyph_color(0x30D050, c); fl_rectf(ix + 12, iy + 7, 2, 2);
      glyph_color(0x000000, c);
      fl_rect(ix + 4, iy, 8, 6); fl_rect(ix, iy + 5, 16, 7); fl_rect(ix + 4, iy + 10, 8, 5);
    } else {
      fl_color(open ? SEL : GRAY);
      fl_rectf(ix + 4, iy + 1, 8, 5);
      fl_color(c);
      fl_rect(ix + 4, iy, 8, 6);
      fl_rect(ix, iy + 5, 16, 7);
      fl_rectf(ix + 1, iy + 6, 14, 1);
      fl_color(open ? SEL : GRAY);
      fl_rectf(ix + 4, iy + 10, 8, 4);
      fl_color(c);
      fl_rect(ix + 4, iy + 10, 8, 5);
      fl_point(ix + 13, iy + 8);
    }
    if (!g_ps.jobs.empty()) {   // the number of jobs in a blue badge
      std::string n = g_ps.jobs.size() > 99 ? "99+" : std::to_string(g_ps.jobs.size());
      fl_font(FONT_SYSTEM, 9);
      int bw = std::max(11, (int)fl_width(n.c_str()) + 5), bh = 11, bx = ix + 16 - 5, by = y() + 1;
      fl_color(trouble() ? fl_rgb_color(0xE5, 0x48, 0x3F) : fl_rgb_color(0x3A, 0x7B, 0xD5));
      fl_pie(bx, by, bh, bh, 90, 270);
      fl_pie(bx + bw - bh, by, bh, bh, 270, 450);
      fl_rectf(bx + bh / 2, by, bw - bh, bh);
      fl_color(FL_WHITE);
      fl_draw(n.c_str(), bx, by, bw, bh, FL_ALIGN_CENTER);
    } else if (trouble()) {
      fl_color(fl_rgb_color(0xE5, 0x48, 0x3F));
      fl_font(FONT_SYSTEM, 10);
      fl_draw("!", ix + 17, y() + 11);
    }
    fl_pop_clip();
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

  void build() {
    const int W = 330;
    pop->begin();
    pop->clear();
    pop->resizable(nullptr);
    pop->begin();
    int y = 8;
    if (!g_ps.running) {
      text_box(12, y, W - 24, _("The printing system is not running"), true);
      y += 24;
    }
    for (auto &p : g_ps.printers) {
      std::string name = (p.info.empty() ? p.name : p.info) + (p.is_default ? "  ✓" : "");
      auto *b = text_box(12, y, W - 24, "", true);
      b->copy_label(name.c_str());
      y += 18;
      auto *s = text_box(20, y, W - 32, "", false, 16);
      s->copy_label(printing::state_text(p).c_str());
      s->labelsize(SIZE_SMALL);
      if (p.state == 5 || !p.accepting) s->labelcolor(fl_rgb_color(0xC0, 0x20, 0x20));
      y += 20;
    }
    if (!g_ps.jobs.empty()) {
      new Sep(8, y, W - 16);
      y += 8;
      text_box(12, y, W - 24, fmt(trn((long)g_ps.jobs.size(), "%1 print job", "%1 print jobs"),
                                  std::to_string(g_ps.jobs.size())).c_str(), true);
      y += 20;
      size_t shown = 0;
      for (auto &j : g_ps.jobs) {
        if (++shown > 8) break;
        std::string t = (j.title.empty() ? fmt(_("Job %1"), std::to_string(j.id)) : j.title);
        std::string row = t + "  ·  " + j.printer + (j.state == 5 ? "  ·  " + std::string(_("printing")) : "");
        auto *b = text_box(20, y, W - 60, "", false, 18);
        b->copy_label(row.c_str());
        b->labelsize(SIZE_SMALL);
        b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        auto *x = new Fl_Button(W - 34, y, 20, 18, "×");
        x->box(BUTTON_BOX);
        x->down_box(BUTTON_DOWN);
        x->labelsize(SIZE_SMALL + 2);
        x->tooltip(_("Cancel this job"));
        x->callback([](Fl_Widget *, void *d) {
          long id = (long)d;
          for (auto &j : g_ps.jobs)
            if (j.id == id) {
              std::string err;
              if (!printing::cancel_job(j.printer, j.id, &err)) g_note = err;
              break;
            }
          Fl::add_timeout(0.0, [](void *) { refresh_printers(); });
        }, (void *)(long)j.id);
        y += 20;
      }
    }
    if (!g_note.empty()) {
      auto *n = text_box(12, y, W - 24, g_note.c_str(), false, 30);
      n->labelsize(SIZE_SMALL);
      n->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_CLIP);
      y += 32;
    }
    new Sep(8, y, W - 16);
    y += 8;
    auto row = [&](const char *label, long id, bool active) {
      auto *r = new MenuRow(2, y, W - 4, label);
      r->callback(act, (void *)id);
      if (!active) r->deactivate();
      y += 22;
    };
    bool has_def = !g_ps.default_printer.empty();
    row(_("Cancel All My Jobs"), 1, !g_ps.jobs.empty());
    row(_("Print Test Page"), 2, has_def);
    row(_("Printer Settings…"), 3, true);
    pop->end();
    pop->size(W, y + 4);
  }

  static void act(Fl_Widget *, void *d) {
    if (g_pa && g_pa->pop) g_pa->pop->close();
    std::string err;
    switch ((long)d) {
    case 1:
      g_note = printing::cancel_my_jobs(&err) ? "" : err;
      break;
    case 2:
      g_note = printing::print_test_page(g_ps.default_printer, &err)
                   ? fmt(_("A test page was sent to %1."), g_ps.default_printer) : err;
      break;
    case 3:
      if (current_host().open_panel) current_host().open_panel("printers");
      return;
    }
    refresh_printers();
  }

  void open_popup() {
    if (!pop) {
      pop = new Popup(330, 100);
      pop->on_close = [this] {
        for (int i = 0; i < pop->children(); i++)
          if (auto *r = dynamic_cast<MenuRow *>(pop->child(i))) r->hover(false);
        redraw();
      };
      pop->track_hover = [this] { update_hover(pop); };
    }
    g_ps = printing::query();
    build();
    pop->open_at(window()->x_root() + x() + w() - pop->w(), window()->y_root() + MENUBAR_H);
    redraw();
  }

  void sync() {
    std::string tip = g_ps.jobs.empty() ? std::string(_("Printers"))
                                        : fmt(trn((long)g_ps.jobs.size(), "%1 print job", "%1 print jobs"),
                                              std::to_string(g_ps.jobs.size()));
    static std::string t;
    t = tip;
    tooltip(t.c_str());
    redraw();
    if (pop && pop->shown()) {
      int right = pop->x() + pop->w();
      build();
      pop->position(right - pop->w(), pop->y());
      pop->redraw();
    }
  }
};

// The width the applet needs: hidden without printers and jobs; wider with the badge.
int printer_width() {
  if (g_ps.printers.empty() && g_ps.jobs.empty()) return 0;
  if (g_ps.jobs.empty()) return 4 + 16 + 4 + (trouble() ? 4 : 0);
  std::string n = g_ps.jobs.size() > 99 ? "99+" : std::to_string(g_ps.jobs.size());
  fl_font(FONT_SYSTEM, 9);
  return 4 + 16 - 5 + std::max(11, (int)fl_width(n.c_str()) + 5) + 2;
}

void refresh_printers() {
  size_t jobs = g_ps.jobs.size(), printers = g_ps.printers.size();
  bool trbl = trouble();
  g_ps = printing::query();
  if (!g_pa) return;
  bool changed = jobs != g_ps.jobs.size() || printers != g_ps.printers.size() || trbl != trouble();
  if (g_pa->w() != printer_width() && current_host().relayout) current_host().relayout();
  if (changed || (g_pa->pop && g_pa->pop->shown())) g_pa->sync();
}

static void printer_tick(void *) {
  refresh_printers();
  bool busy = !g_ps.jobs.empty() || (g_pa && g_pa->pop && g_pa->pop->shown());
  Fl::repeat_timeout(busy ? 3.0 : 20.0, printer_tick);
}

Fl_Widget *make_printer(int X, int Y, int W, int H) {
  auto *p = new PrinterApplet(X, Y, W, H);
  g_ps = printing::query();
  Fl::add_timeout(3.0, printer_tick);
  return p;
}

}  // namespace applets

#else   // no libcups

namespace applets {
int printer_width() { return 0; }
void refresh_printers() {}
Fl_Widget *make_printer(int, int, int, int) { return nullptr; }
}  // namespace applets

#endif
