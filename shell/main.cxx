// Nirvana Desktop - Nirvana Shell: emulator terminala
//
// Zgodny z xterm (TERM=xterm-256color): stan ekranu, sekwencje sterujące,
// klawiatura i mysz to libvterm; tutaj jest pty, rysowanie w FLTK,
// przewijanie wstecz, zaznaczanie, schowek, profile kolorów i okna.
//
//   nirvana-shell [--katalog DIR] [-e POLECENIE ARG...]
//
// Skróty: Ctrl+Shift+C/V kopiuj/wklej, Ctrl+Shift+N nowe okno, Ctrl+Shift+W
// zamknij, Ctrl+Shift+= / - / 0 czcionka, Shift+PgUp/PgDn przewijanie.
// Prawy przycisk myszy: menu (profile, czcionka, reset...).
#include "framewin.h"
#include "platinum.h"

extern "C" {
#include <vterm.h>
}

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/fl_draw.H>
#include <FL/fl_utf8.h>
#include <FL/platform.H>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <clocale>
#include <langinfo.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <pty.h>
#include <pwd.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace plat;

namespace {

// ---------------------------------------------------------------------------
// Profile kolorów
// ---------------------------------------------------------------------------
struct Profile {
  const char *id, *name;
  unsigned fg, bg, cursor, sel;
  unsigned pal[16];
};

// Paleta 16 kolorów: dla jasnego tła ciemniejsze odcienie, żeby żółty i
// biały dało się przeczytać.
const unsigned PAL_LIGHT[16] = {0x000000, 0xB21818, 0x18A218, 0x8C6D00, 0x1A3FB8, 0xA020A0, 0x0F8A8A, 0xA0A0A0,
                                0x606060, 0xE03030, 0x2BBF2B, 0xB89400, 0x3D63E8, 0xD040D0, 0x1FB3B3, 0xFFFFFF};
const unsigned PAL_DARK[16] = {0x2E3436, 0xCC0000, 0x4E9A06, 0xC4A000, 0x3465A4, 0x75507B, 0x06989A, 0xD3D7CF,
                               0x555753, 0xEF2929, 0x8AE234, 0xFCE94F, 0x729FCF, 0xAD7FA8, 0x34E2E2, 0xEEEEEC};

Profile PROFILES[] = {
    {"klasyczny", N_("Classic (light)"), 0x000000, 0xFFFFFF, 0x000000, 0xB4C8FF, {}},
    {"ciemny", N_("Dark"), 0xD3D7CF, 0x1E1F22, 0xD3D7CF, 0x3D4F7A, {}},
    {"zielony", N_("Green phosphor"), 0x33FF66, 0x081208, 0x33FF66, 0x1E5A2A, {}},
    {"bursztyn", N_("Amber"), 0xFFB000, 0x160E00, 0xFFB000, 0x5A3E00, {}},
    {"workbench", N_("Workbench (Amiga)"), 0xFFFFFF, 0x0055AA, 0xFF8800, 0x000022, {}},
    {"schemat", N_("Like the color scheme"), 0, 0, 0, 0, {}},   // kolory Nirvana Desktop
};
const int NPROFILES = sizeof PROFILES / sizeof PROFILES[0];

struct Settings {
  std::string profile = "klasyczny";
  std::string font = "DejaVu Sans Mono";
  int size = 12, cols = 80, rows = 24, scrollback = 5000;
};
Settings g_set;

std::string config_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  const char *h = getenv("HOME");
  return (x && *x ? std::string(x) : std::string(h ? h : "/") + "/.config") + "/nirvana/terminal.conf";
}
void load_settings() {
  std::ifstream in(config_file());
  std::string line;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (k == "profil") g_set.profile = v;
    else if (k == "czcionka" && !v.empty()) g_set.font = v;
    else if (k == "rozmiar") g_set.size = std::clamp(atoi(v.c_str()), 6, 48);
    else if (k == "kolumny") g_set.cols = std::clamp(atoi(v.c_str()), 20, 500);
    else if (k == "wiersze") g_set.rows = std::clamp(atoi(v.c_str()), 5, 200);
    else if (k == "przewijanie") g_set.scrollback = std::clamp(atoi(v.c_str()), 0, 100000);
  }
}
void save_settings() {
  std::string f = config_file();
  std::string dir = f.substr(0, f.rfind('/'));
  mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
  mkdir(dir.c_str(), 0755);
  std::ofstream out(f);
  out << "# Nirvana Shell\nprofil=" << g_set.profile << "\nczcionka=" << g_set.font << "\nrozmiar=" << g_set.size
      << "\nkolumny=" << g_set.cols << "\nwiersze=" << g_set.rows << "\nprzewijanie=" << g_set.scrollback << "\n";
}

const Profile &profile_by_id(const std::string &id) {
  for (auto &p : PROFILES) if (id == p.id) return p;
  return PROFILES[0];
}

unsigned rgb_of(Fl_Color c) {
  uchar r, g, b;
  Fl::get_color(c, r, g, b);
  return (r << 16) | (g << 8) | b;
}

// Profil z uzupełnionymi kolorami (dla "schemat" - z palety Nirvana Desktop).
Profile resolved(const Profile &p) {
  Profile r = p;
  if (!strcmp(p.id, "schemat")) {
    r.fg = rgb_of(CONTENT_TEXT);
    r.bg = rgb_of(CONTENT);
    r.cursor = rgb_of(CONTENT_TEXT);
    r.sel = rgb_of(SEL);
  }
  double lum = (0.299 * ((r.bg >> 16) & 255) + 0.587 * ((r.bg >> 8) & 255) + 0.114 * (r.bg & 255)) / 255;
  memcpy(r.pal, lum > 0.5 ? PAL_LIGHT : PAL_DARK, sizeof r.pal);
  return r;
}

// Czcionki: cztery odmiany w wolnych numerach FLTK (Xft: ' ' zwykła, B, I, P).
Fl_Font FONT_BASE = FL_FREE_FONT + 20;
void set_fonts() {
  static std::string names[4];
  const char pre[4] = {' ', 'B', 'I', 'P'};
  for (int i = 0; i < 4; i++) {
    names[i] = std::string(1, pre[i]) + g_set.font;
    Fl::set_font(FONT_BASE + i, names[i].c_str());
  }
}

class Term;
std::vector<Term *> g_terms;

// ---------------------------------------------------------------------------
// Okno terminala
// ---------------------------------------------------------------------------
using Line = std::vector<VTermScreenCell>;

class TermArea : public Fl_Widget {
public:
  TermArea(Term *t, int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H), term(t) {}
  Term *term;
  int handle(int e) override;
  void draw() override;
  void resize(int X, int Y, int W, int H) override;
};

class GrowBox : public Fl_Widget {
public:
  GrowBox(int X, int Y) : Fl_Widget(X, Y, SCROLL_W, SCROLL_W) {}
  void draw() override { draw_grow_box(x(), y()); }
};

class Term {
public:
  Term(const std::string &dir, const std::vector<std::string> &cmd);
  ~Term();

  FrameWindow *win;
  TermArea *area;
  Fl_Scrollbar *sbar;
  Profile prof;
  VTerm *vt = nullptr;
  VTermScreen *vs = nullptr;
  int pty = -1;
  pid_t child = -1;
  int rows = 24, cols = 80;
  int cw = 7, chh = 14, asc = 11;          // komórka: szerokość, wysokość, linia bazowa
  static constexpr int PAD = 3;
  std::deque<Line> sb;                     // przewijanie wstecz (najstarsze na początku)
  int scroll = 0;                          // ile wierszy w górę od dołu
  VTermPos cursor{0, 0};
  bool cursor_visible = true, altscreen = false, reverse = false, focus_report = false;
  int cursor_shape = VTERM_PROP_CURSORSHAPE_BLOCK, mouse_mode = 0;
  std::string title = "Nirvana Shell";
  // zaznaczenie we współrzędnych bufora: wiersz < 0 to przewijanie wstecz
  bool sel_active = false, selecting = false;
  int sel_r0 = 0, sel_c0 = 0, sel_r1 = 0, sel_c1 = 0;
  int sel_mode = 0;                        // 0 znaki, 1 słowa, 2 wiersze
  bool flash = false;
  bool closing = false;

  void metrics();
  void apply_profile(const Profile &p);
  void set_grid(int r, int c);
  void update_scrollbar();
  bool get_cell(int row, int col, VTermScreenCell &cell) const;   // wiersz bufora
  int buf_row(int screen_row) const { return screen_row - scroll; }
  void scroll_to(int s);
  std::string selection_text() const;
  bool in_sel(int r, int c) const;
  void copy(int clipboard);
  void paste_text(const char *t, int len);
  void send_key(int key, int state, const char *text);
  void write_pty(const char *d, size_t n);
  void close_request();
  std::string running_program() const;

  // wywołania libvterm
  static int cb_damage(VTermRect r, void *u) { ((Term *)u)->area->redraw(); (void)r; return 1; }
  static int cb_moverect(VTermRect, VTermRect, void *u) { ((Term *)u)->area->redraw(); return 1; }
  static int cb_movecursor(VTermPos p, VTermPos, int vis, void *u) {
    Term *t = (Term *)u;
    t->cursor = p;
    t->cursor_visible = vis;
    t->area->redraw();
    return 1;
  }
  static int cb_settermprop(VTermProp prop, VTermValue *v, void *u);
  static int cb_bell(void *u);
  static int cb_pushline(int cols, const VTermScreenCell *cells, void *u);
  static int cb_popline(int cols, VTermScreenCell *cells, void *u);
  static int cb_sbclear(void *u) { Term *t = (Term *)u; t->sb.clear(); t->scroll = 0; t->update_scrollbar(); return 1; }
  static void out_cb(const char *s, size_t len, void *u) { ((Term *)u)->write_pty(s, len); }
  static void pty_cb(int fd, void *u);
};

int Term::cb_settermprop(VTermProp prop, VTermValue *v, void *u) {
  Term *t = (Term *)u;
  switch (prop) {
    case VTERM_PROP_TITLE: {
      std::string s(v->string.str, v->string.len);
      if (v->string.initial) t->title.clear();
      t->title += s;
      if (v->string.final) {
        t->win->copy_label(t->title.empty() ? "Nirvana Shell" : t->title.c_str());
        t->win->redraw();
      }
      break;
    }
    case VTERM_PROP_CURSORVISIBLE: t->cursor_visible = v->boolean; break;
    case VTERM_PROP_ALTSCREEN: t->altscreen = v->boolean; t->scroll = 0; t->update_scrollbar(); break;
    case VTERM_PROP_REVERSE: t->reverse = v->boolean; break;
    case VTERM_PROP_CURSORSHAPE: t->cursor_shape = v->number; break;
    case VTERM_PROP_MOUSE: t->mouse_mode = v->number; break;
    case VTERM_PROP_FOCUSREPORT: t->focus_report = v->boolean; break;
    default: break;
  }
  t->area->redraw();
  return 1;
}

int Term::cb_bell(void *u) {
  // dzwonek wizualny: krótkie odwrócenie kolorów
  Term *t = (Term *)u;
  t->flash = true;
  t->area->redraw();
  Fl::add_timeout(0.08, [](void *d) {
    Term *t = (Term *)d;
    if (std::find(g_terms.begin(), g_terms.end(), t) == g_terms.end()) return;
    t->flash = false;
    t->area->redraw();
  }, t);
  return 1;
}

static bool blank_default(const VTermScreenCell &c) {
  return (c.chars[0] == 0 || c.chars[0] == ' ') && VTERM_COLOR_IS_DEFAULT_BG(&c.bg) && !c.attrs.reverse;
}

int Term::cb_pushline(int ncols, const VTermScreenCell *cells, void *u) {
  Term *t = (Term *)u;
  if (g_set.scrollback <= 0) return 1;
  int n = ncols;
  while (n > 0 && blank_default(cells[n - 1])) n--;   // bez pustego ogona - mniej pamięci
  t->sb.emplace_back(cells, cells + n);
  if ((int)t->sb.size() > g_set.scrollback) t->sb.pop_front();
  else if (t->scroll > 0) t->scroll++;   // widok stoi w miejscu, gdy czytasz historię
  t->update_scrollbar();
  return 1;
}

int Term::cb_popline(int ncols, VTermScreenCell *cells, void *u) {
  Term *t = (Term *)u;
  if (t->sb.empty()) return 0;
  Line &l = t->sb.back();
  VTermScreenCell blank{};
  blank.width = 1;
  vterm_color_rgb(&blank.fg, 0, 0, 0);
  blank.fg.type = VTERM_COLOR_DEFAULT_FG;
  blank.bg.type = VTERM_COLOR_DEFAULT_BG;
  for (int i = 0; i < ncols; i++) cells[i] = i < (int)l.size() ? l[i] : blank;
  t->sb.pop_back();
  t->scroll = std::min(t->scroll, (int)t->sb.size());
  t->update_scrollbar();
  return 1;
}

// --- rysowanie --------------------------------------------------------------------
static Fl_Color fl_rgb(unsigned c) { return fl_rgb_color((c >> 16) & 255, (c >> 8) & 255, c & 255); }

bool Term::get_cell(int row, int col, VTermScreenCell &cell) const {
  if (row >= 0) {
    VTermPos p{row, col};
    return vterm_screen_get_cell(vs, p, &cell);
  }
  int idx = (int)sb.size() + row;
  if (idx < 0) return false;
  const Line &l = sb[idx];
  if (col < (int)l.size()) { cell = l[col]; return true; }
  cell = VTermScreenCell{};
  cell.width = 1;
  cell.fg.type = VTERM_COLOR_DEFAULT_FG;
  cell.bg.type = VTERM_COLOR_DEFAULT_BG;
  return true;
}

bool Term::in_sel(int r, int c) const {
  if (!sel_active) return false;
  int r0 = sel_r0, c0 = sel_c0, r1 = sel_r1, c1 = sel_c1;
  if (r1 < r0 || (r1 == r0 && c1 < c0)) { std::swap(r0, r1); std::swap(c0, c1); }
  if (r < r0 || r > r1) return false;
  if (r == r0 && c < c0) return false;
  if (r == r1 && c > c1) return false;
  return true;
}

void TermArea::draw() {
  Term *t = term;
  fl_push_clip(x(), y(), w(), h());
  unsigned defbg = t->prof.bg, deffg = t->prof.fg;
  if (t->reverse != t->flash) std::swap(defbg, deffg);
  fl_color(fl_rgb(defbg));
  fl_rectf(x(), y(), w(), h());
  char utf[8 * VTERM_MAX_CHARS_PER_CELL];
  VTermScreenCell cell;
  for (int r = 0; r < t->rows; r++) {
    int br = t->buf_row(r);
    int py = y() + Term::PAD + r * t->chh;
    for (int c = 0; c < t->cols; c++) {
      if (!t->get_cell(br, c, cell)) continue;
      if (cell.width == 0) continue;   // druga połowa szerokiego znaku
      VTermColor fg = cell.fg, bg = cell.bg;
      bool dfg = VTERM_COLOR_IS_DEFAULT_FG(&fg), dbg = VTERM_COLOR_IS_DEFAULT_BG(&bg);
      if (!dfg) vterm_screen_convert_color_to_rgb(t->vs, &fg);
      if (!dbg) vterm_screen_convert_color_to_rgb(t->vs, &bg);
      unsigned f = dfg ? deffg : (fg.rgb.red << 16) | (fg.rgb.green << 8) | fg.rgb.blue;
      unsigned b = dbg ? defbg : (bg.rgb.red << 16) | (bg.rgb.green << 8) | bg.rgb.blue;
      if (cell.attrs.reverse) std::swap(f, b);
      bool sel = t->in_sel(br, c);
      if (sel) b = t->prof.sel;
      int px = x() + Term::PAD + c * t->cw, cwid = t->cw * std::max(1, (int)cell.width);
      if (b != defbg) { fl_color(fl_rgb(b)); fl_rectf(px, py, cwid, t->chh); }
      bool cur = !t->scroll && t->cursor_visible && r == t->cursor.row && c == t->cursor.col;
      bool focused = Fl::focus() == this && t->win == Fl::first_window();
      if (cur && focused && t->cursor_shape == VTERM_PROP_CURSORSHAPE_BLOCK) {
        fl_color(fl_rgb(t->prof.cursor));
        fl_rectf(px, py, cwid, t->chh);
        f = b == t->prof.cursor ? defbg : b;
        if (f == t->prof.cursor) f = defbg;
      }
      if (cell.chars[0] && cell.chars[0] != ' ' && !cell.attrs.conceal) {
        int n = 0;
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++) n += fl_utf8encode(cell.chars[i], utf + n);
        utf[n] = 0;
        fl_font(FONT_BASE + (cell.attrs.bold ? 1 : 0) + (cell.attrs.italic ? 2 : 0), g_set.size);
        fl_color(fl_rgb(f));
        fl_draw(utf, n, px, py + t->asc);
      }
      if (cell.attrs.underline) {
        fl_color(fl_rgb(f));
        fl_xyline(px, py + t->asc + 2, px + cwid - 1);
        if (cell.attrs.underline == VTERM_UNDERLINE_DOUBLE) fl_xyline(px, py + t->asc + 4, px + cwid - 1);
      }
      if (cell.attrs.strike) { fl_color(fl_rgb(f)); fl_xyline(px, py + t->chh / 2, px + cwid - 1); }
      if (cur && (!focused || t->cursor_shape != VTERM_PROP_CURSORSHAPE_BLOCK)) {
        fl_color(fl_rgb(t->prof.cursor));
        if (!focused) fl_rect(px, py, cwid, t->chh);   // bez fokusu: pusty prostokąt
        else if (t->cursor_shape == VTERM_PROP_CURSORSHAPE_UNDERLINE) fl_rectf(px, py + t->chh - 2, cwid, 2);
        else fl_rectf(px, py, 2, t->chh);
      }
    }
  }
  fl_pop_clip();
}

void Term::metrics() {
  fl_font(FONT_BASE, g_set.size);
  cw = std::max(1, (int)(fl_width("M") + 0.5));
  chh = fl_height();
  asc = chh - fl_descent();
}

void Term::set_grid(int r, int c) {
  r = std::max(2, r);
  c = std::max(10, c);
  if (r == rows && c == cols) return;
  rows = r;
  cols = c;
  vterm_set_size(vt, rows, cols);
  vterm_screen_flush_damage(vs);
  if (pty >= 0) {
    winsize ws{};
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = cols * cw;
    ws.ws_ypixel = rows * chh;
    ioctl(pty, TIOCSWINSZ, &ws);
  }
  update_scrollbar();
}

void TermArea::resize(int X, int Y, int W, int H) {
  Fl_Widget::resize(X, Y, W, H);
  if (term->vt) term->set_grid((H - 2 * Term::PAD) / term->chh, (W - 2 * Term::PAD) / term->cw);
}

void Term::update_scrollbar() {
  if (!sbar) return;
  int total = (int)sb.size() + rows;
  sbar->value((int)sb.size() - scroll, rows, 0, total);
  sbar->redraw();
}

void Term::scroll_to(int s) {
  s = std::clamp(s, 0, altscreen ? 0 : (int)sb.size());
  if (s == scroll) return;
  scroll = s;
  update_scrollbar();
  area->redraw();
}

void Term::apply_profile(const Profile &p) {
  prof = resolved(p);
  VTermColor fg, bg;
  vterm_color_rgb(&fg, (prof.fg >> 16) & 255, (prof.fg >> 8) & 255, prof.fg & 255);
  vterm_color_rgb(&bg, (prof.bg >> 16) & 255, (prof.bg >> 8) & 255, prof.bg & 255);
  vterm_screen_set_default_colors(vs, &fg, &bg);
  VTermState *st = vterm_obtain_state(vt);
  for (int i = 0; i < 16; i++) {
    VTermColor c;
    vterm_color_rgb(&c, (prof.pal[i] >> 16) & 255, (prof.pal[i] >> 8) & 255, prof.pal[i] & 255);
    vterm_state_set_palette_color(st, i, &c);
  }
  area->redraw();
}

// --- zaznaczenie i schowek ----------------------------------------------------------
std::string Term::selection_text() const {
  if (!sel_active) return "";
  int r0 = sel_r0, c0 = sel_c0, r1 = sel_r1, c1 = sel_c1;
  if (r1 < r0 || (r1 == r0 && c1 < c0)) { std::swap(r0, r1); std::swap(c0, c1); }
  std::string out;
  char buf[8];
  VTermScreenCell cell;
  for (int r = r0; r <= r1; r++) {
    std::string line;
    int from = r == r0 ? c0 : 0, to = r == r1 ? c1 : cols - 1;
    for (int c = from; c <= to; c++) {
      if (!get_cell(r, c, cell) || cell.width == 0) continue;
      if (!cell.chars[0]) { line += ' '; continue; }
      for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++) line.append(buf, fl_utf8encode(cell.chars[i], buf));
    }
    while (!line.empty() && line.back() == ' ') line.pop_back();
    out += line;
    if (r != r1) out += '\n';
  }
  return out;
}

void Term::copy(int clipboard) {
  std::string s = selection_text();
  if (!s.empty()) Fl::copy(s.c_str(), (int)s.size(), clipboard);
}

void Term::paste_text(const char *t, int len) {
  // wklejanie w nawiasach (bracketed paste) - libvterm wie, czy program o to prosił
  vterm_keyboard_start_paste(vt);
  std::string s(t, len);
  for (auto &ch : s) if (ch == '\n') ch = '\r';
  int i = 0;
  while (i < (int)s.size()) {
    unsigned cp = fl_utf8decode(s.c_str() + i, s.c_str() + s.size(), &len);
    vterm_keyboard_unichar(vt, cp, VTERM_MOD_NONE);
    i += std::max(1, len);
  }
  vterm_keyboard_end_paste(vt);
  scroll_to(0);
}

void Term::write_pty(const char *d, size_t n) {
  while (n > 0 && pty >= 0) {
    ssize_t w = write(pty, d, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN) { pollfd p{pty, POLLOUT, 0}; poll(&p, 1, 100); continue; }
      return;
    }
    d += w;
    n -= w;
  }
}

// --- klawiatura ----------------------------------------------------------------------
void Term::send_key(int key, int state, const char *text) {
  int mod = VTERM_MOD_NONE;
  if (state & FL_SHIFT) mod |= VTERM_MOD_SHIFT;
  if (state & FL_ALT) mod |= VTERM_MOD_ALT;
  if (state & FL_CTRL) mod |= VTERM_MOD_CTRL;
  VTermModifier m = (VTermModifier)mod;
  VTermKey vk = VTERM_KEY_NONE;
  switch (key) {
    case FL_Enter: case FL_KP_Enter: vk = VTERM_KEY_ENTER; break;
    case FL_Tab: vk = VTERM_KEY_TAB; break;
    case FL_BackSpace: vk = VTERM_KEY_BACKSPACE; break;
    case FL_Escape: vk = VTERM_KEY_ESCAPE; break;
    case FL_Up: vk = VTERM_KEY_UP; break;
    case FL_Down: vk = VTERM_KEY_DOWN; break;
    case FL_Left: vk = VTERM_KEY_LEFT; break;
    case FL_Right: vk = VTERM_KEY_RIGHT; break;
    case FL_Insert: vk = VTERM_KEY_INS; break;
    case FL_Delete: vk = VTERM_KEY_DEL; break;
    case FL_Home: vk = VTERM_KEY_HOME; break;
    case FL_End: vk = VTERM_KEY_END; break;
    case FL_Page_Up: vk = VTERM_KEY_PAGEUP; break;
    case FL_Page_Down: vk = VTERM_KEY_PAGEDOWN; break;
    default:
      if (key >= FL_F + 1 && key <= FL_F + 24) vk = (VTermKey)VTERM_KEY_FUNCTION(key - FL_F);
  }
  if (vk != VTERM_KEY_NONE) {
    vterm_keyboard_key(vt, vk, m);
  } else if ((state & (FL_CTRL | FL_ALT)) && key > 0 && key < 0x7f) {
    // Ctrl+litera, Alt+litera: z kodu klawisza (tekst bywa już przetworzony)
    unsigned cp = key;
    if (state & FL_SHIFT && cp >= 'a' && cp <= 'z') cp -= 32;
    vterm_keyboard_unichar(vt, cp, (VTermModifier)(mod & ~VTERM_MOD_SHIFT));
  } else if (text && *text) {
    const char *p = text, *e = text + strlen(text);
    while (p < e) {
      int len;
      unsigned cp = fl_utf8decode(p, e, &len);
      vterm_keyboard_unichar(vt, cp, (VTermModifier)(mod & ~VTERM_MOD_SHIFT));
      p += std::max(1, len);
    }
  } else {
    return;
  }
  scroll_to(0);
}

// --- pty -----------------------------------------------------------------------------
void Term::pty_cb(int fd, void *u) {
  Term *t = (Term *)u;
  char buf[65536];
  ssize_t n = read(fd, buf, sizeof buf);
  if (n > 0) {
    vterm_input_write(t->vt, buf, n);
    vterm_screen_flush_damage(t->vs);
    return;
  }
  if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
  // powłoka się zakończyła
  Fl::remove_fd(fd);
  t->closing = true;
  Fl::add_timeout(0.0, [](void *d) { delete (Term *)d; }, t);
}

std::string Term::running_program() const {
  if (pty < 0 || child <= 0) return "";
  pid_t fg = tcgetpgrp(pty);
  if (fg <= 0 || fg == child) return "";
  std::ifstream in("/proc/" + std::to_string(fg) + "/comm");
  std::string name;
  std::getline(in, name);
  return name.empty() ? "program" : name;
}

// Pytanie przed zamknięciem, gdy w terminalu coś działa (np. vim, kopiowanie).
bool ask_close(Term *t, const std::string &prog) {
  int W = 400, H = 130;
  auto *w = new FrameWindow(W, H, "Nirvana Shell", true);
  struct Pic : Fl_Widget {
    Pic(int X, int Y) : Fl_Widget(X, Y, 34, 34) {}
    void draw() override { draw_icon(Icon::Application, x(), y(), false); }
  };
  new Pic(16, 16);
  std::string msg = plat::fmt(_("“%1” is running in this window.\nClose it anyway?"), prog);
  auto *b = new Fl_Box(64, 14, W - 80, 50);
  b->copy_label(msg.c_str());
  b->labelfont(FONT_SYSTEM);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
  int result = -1;
  auto mk = [&](int X, const char *l, int v) {
    auto *bt = new Fl_Button(X, H - 38, 86, 24, l);
    bt->box(BUTTON_BOX);
    bt->down_box(BUTTON_DOWN);
    bt->callback([](Fl_Widget *bw, void *) {
      *(int *)bw->window()->user_data() = (int)(long)bw->argument();
    });
    bt->argument(v);
    return bt;
  };
  mk(W - 196, _("Cancel"), 0);
  auto *ok = mk(W - 102, _("Close"), 1);
  ok->labelfont(FONT_SYSTEM);
  ok->shortcut(FL_Enter);
  w->user_data(&result);
  w->callback([](Fl_Widget *ww, void *) { *(int *)ww->user_data() = 0; });
  w->end();
  w->finish();
  w->set_modal();
  w->position(t->win->x() + (t->win->w() - w->w()) / 2, t->win->y() + 40);
  w->show();
  while (result < 0 && w->shown()) Fl::wait();
  w->hide();
  Fl::delete_widget(w);
  return result == 1;
}

void Term::close_request() {
  std::string prog = running_program();
  if (!prog.empty() && !ask_close(this, prog)) return;
  closing = true;
  Fl::add_timeout(0.0, [](void *d) { delete (Term *)d; }, this);
}

// --- menu kontekstowe ------------------------------------------------------------------
void new_window(const std::string &dir);
std::string cwd_of(pid_t pid) {
  char buf[4096];
  ssize_t n = readlink(("/proc/" + std::to_string(pid) + "/cwd").c_str(), buf, sizeof buf - 1);
  if (n <= 0) return "";
  buf[n] = 0;
  return buf;
}

void context_menu(Term *t) {
  Fl_Menu_Button m(Fl::event_x(), Fl::event_y(), 1, 1);
  m.textfont(FONT_SYSTEM);
  m.textsize(SIZE_SYSTEM);
  m.box(FL_NO_BOX);
  int sel = t->sel_active ? 0 : FL_MENU_INACTIVE;
  m.add(_("New Window"), FL_CTRL | FL_SHIFT | 'n', nullptr, (void *)1L, FL_MENU_DIVIDER);
  m.add(_("Copy"), FL_CTRL | FL_SHIFT | 'c', nullptr, (void *)2L, sel);
  m.add(_("Paste"), FL_CTRL | FL_SHIFT | 'v', nullptr, (void *)3L);
  m.add(_("Select All"), 0, nullptr, (void *)4L, FL_MENU_DIVIDER);
  for (int i = 0; i < NPROFILES; i++) {
    std::string l = plat::tr_path(P_("Profile")) + "/" + _(PROFILES[i].name);
    m.add(l.c_str(), 0, nullptr, (void *)(long)(100 + i),
          FL_MENU_RADIO | (!strcmp(t->prof.id, PROFILES[i].id) ? FL_MENU_VALUE : 0));
  }
  m.add(plat::tr_path(P_("Font/Bigger")).c_str(), FL_CTRL | FL_SHIFT | '=', nullptr, (void *)5L);
  m.add(plat::tr_path(P_("Font/Smaller")).c_str(), FL_CTRL | FL_SHIFT | '-', nullptr, (void *)6L);
  m.add(plat::tr_path(P_("Font/Normal Size")).c_str(), FL_CTRL | FL_SHIFT | '0', nullptr, (void *)7L, FL_MENU_DIVIDER);
  m.add(_("Clear Scrollback"), 0, nullptr, (void *)8L);
  m.add(_("Reset Terminal"), 0, nullptr, (void *)9L, FL_MENU_DIVIDER);
  m.add(_("Close Window"), FL_CTRL | FL_SHIFT | 'w', nullptr, (void *)10L);
  const Fl_Menu_Item *it = m.menu()->popup(Fl::event_x(), Fl::event_y(), nullptr, nullptr, &m);
  if (!it) return;
  long v = (long)it->user_data();
  void set_font_size(int);
  switch (v) {
    case 1: new_window(cwd_of(t->child)); break;
    case 2: t->copy(1); break;
    case 3: Fl::paste(*t->area, 1); break;
    case 4:
      t->sel_active = true;
      t->sel_r0 = -(int)t->sb.size(); t->sel_c0 = 0;
      t->sel_r1 = t->rows - 1; t->sel_c1 = t->cols - 1;
      t->copy(0);
      t->area->redraw();
      break;
    case 5: set_font_size(g_set.size + 1); break;
    case 6: set_font_size(g_set.size - 1); break;
    case 7: set_font_size(12); break;
    case 8: t->sb.clear(); t->scroll = 0; t->update_scrollbar(); t->area->redraw(); break;
    case 9: vterm_screen_reset(t->vs, 1); t->sb.clear(); t->scroll = 0; t->update_scrollbar(); break;
    case 10: t->close_request(); break;
    default:
      if (v >= 100 && v < 100 + NPROFILES) {
        g_set.profile = PROFILES[v - 100].id;
        for (Term *x : g_terms) x->apply_profile(PROFILES[v - 100]);
        save_settings();
      }
  }
}

void set_font_size(int s) {
  g_set.size = std::clamp(s, 6, 48);
  save_settings();
  for (Term *t : g_terms) {
    t->metrics();
    // rozmiar okna zostaje, zmienia się liczba wierszy i kolumn
    t->area->resize(t->area->x(), t->area->y(), t->area->w(), t->area->h());
    t->area->redraw();
  }
}

// --- zdarzenia -----------------------------------------------------------------------
int TermArea::handle(int e) {
  Term *t = term;
  auto cell_at = [&](int &r, int &c) {
    c = std::clamp((Fl::event_x() - x() - Term::PAD) / t->cw, 0, t->cols - 1);
    r = std::clamp((Fl::event_y() - y() - Term::PAD) / t->chh, 0, t->rows - 1);
  };
  bool app_mouse = t->mouse_mode != VTERM_PROP_MOUSE_NONE && !(Fl::event_state() & FL_SHIFT);
  int mod = 0;
  if (Fl::event_state() & FL_SHIFT) mod |= VTERM_MOD_SHIFT;
  if (Fl::event_state() & FL_CTRL) mod |= VTERM_MOD_CTRL;
  if (Fl::event_state() & FL_ALT) mod |= VTERM_MOD_ALT;
  switch (e) {
    case FL_FOCUS:
      if (t->focus_report) vterm_state_focus_in(vterm_obtain_state(t->vt));
      redraw();
      return 1;
    case FL_UNFOCUS:
      if (t->focus_report) vterm_state_focus_out(vterm_obtain_state(t->vt));
      redraw();
      return 1;
    case FL_ENTER: fl_cursor(FL_CURSOR_INSERT); return 1;
    case FL_LEAVE: fl_cursor(FL_CURSOR_DEFAULT); return 1;
    case FL_MOVE:
      if (app_mouse && t->mouse_mode == VTERM_PROP_MOUSE_MOVE) {
        int r, c; cell_at(r, c);
        vterm_mouse_move(t->vt, r, c, (VTermModifier)mod);
      }
      return 1;
    case FL_PUSH: {
      take_focus();
      int r, c; cell_at(r, c);
      int b = Fl::event_button();
      if (app_mouse) {
        vterm_mouse_move(t->vt, r, c, (VTermModifier)mod);
        vterm_mouse_button(t->vt, b, true, (VTermModifier)mod);
        return 1;
      }
      if (b == FL_RIGHT_MOUSE || (b == FL_LEFT_MOUSE && (Fl::event_state() & FL_CTRL))) {
        context_menu(t);
        return 1;
      }
      if (b == FL_MIDDLE_MOUSE) { Fl::paste(*this, 0); return 1; }
      int br = t->buf_row(r);
      int clicks = Fl::event_clicks();
      t->sel_mode = std::min(clicks, 2);
      t->selecting = true;
      if (t->sel_mode == 0) {
        t->sel_active = false;
        t->sel_r0 = t->sel_r1 = br;
        t->sel_c0 = t->sel_c1 = c;
      } else if (t->sel_mode == 1) {   // słowo
        auto word = [&](int cc) {
          VTermScreenCell cell;
          if (!t->get_cell(br, cc, cell)) return false;
          uint32_t ch = cell.chars[0];
          return ch && ch != ' ' && !strchr("\"'()[]{}<>|;,`", ch < 128 ? (int)ch : 'x');
        };
        int a = c, z = c;
        while (a > 0 && word(a - 1)) a--;
        while (z < t->cols - 1 && word(z + 1)) z++;
        t->sel_active = true;
        t->sel_r0 = t->sel_r1 = br; t->sel_c0 = a; t->sel_c1 = z;
        t->copy(0);
      } else {                          // wiersz
        t->sel_active = true;
        t->sel_r0 = t->sel_r1 = br; t->sel_c0 = 0; t->sel_c1 = t->cols - 1;
        t->copy(0);
      }
      redraw();
      return 1;
    }
    case FL_DRAG: {
      int r, c; cell_at(r, c);
      if (app_mouse) {
        if (t->mouse_mode >= VTERM_PROP_MOUSE_DRAG) vterm_mouse_move(t->vt, r, c, (VTermModifier)mod);
        return 1;
      }
      if (!t->selecting) return 1;
      // przewijanie przy przeciąganiu poza okno
      if (Fl::event_y() < y()) t->scroll_to(t->scroll + 1);
      else if (Fl::event_y() > y() + h()) t->scroll_to(t->scroll - 1);
      t->sel_active = true;
      t->sel_r1 = t->buf_row(r);
      t->sel_c1 = t->sel_mode == 2 ? t->cols - 1 : c;
      redraw();
      return 1;
    }
    case FL_RELEASE:
      if (app_mouse) {
        int r, c; cell_at(r, c);
        vterm_mouse_button(t->vt, Fl::event_button(), false, (VTermModifier)mod);
        return 1;
      }
      if (t->selecting) {
        t->selecting = false;
        if (t->sel_active) t->copy(0);   // zaznaczenie = schowek X (PRIMARY)
      }
      return 1;
    case FL_MOUSEWHEEL: {
      int dy = Fl::event_dy();
      if (app_mouse) {
        int r, c; cell_at(r, c);
        vterm_mouse_move(t->vt, r, c, (VTermModifier)mod);
        int b = dy < 0 ? 4 : 5;
        vterm_mouse_button(t->vt, b, true, (VTermModifier)mod);
        return 1;
      }
      if (t->altscreen) {   // less, man, vim bez myszy: kółko jako strzałki
        for (int i = 0; i < 3; i++) vterm_keyboard_key(t->vt, dy < 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
        return 1;
      }
      t->scroll_to(t->scroll - dy * 3);
      return 1;
    }
    case FL_PASTE:
      t->paste_text(Fl::event_text(), Fl::event_length());
      return 1;
    case FL_SHORTCUT:
    case FL_KEYBOARD: {
      int k = Fl::event_key(), st = Fl::event_state();
      bool cs = (st & FL_CTRL) && (st & FL_SHIFT);
      if (cs) {
        switch (k) {
          case 'c': t->copy(1); return 1;
          case 'v': Fl::paste(*this, 1); return 1;
          case 'n': new_window(cwd_of(t->child)); return 1;
          case 'w': t->close_request(); return 1;
          case '=': case '+': set_font_size(g_set.size + 1); return 1;
          case '-': set_font_size(g_set.size - 1); return 1;
          case '0': set_font_size(12); return 1;
        }
      }
      if ((st & FL_SHIFT) && !(st & FL_CTRL)) {
        if (k == FL_Page_Up) { t->scroll_to(t->scroll + t->rows - 1); return 1; }
        if (k == FL_Page_Down) { t->scroll_to(t->scroll - t->rows + 1); return 1; }
        if (k == FL_Home && !t->altscreen) { t->scroll_to((int)t->sb.size()); return 1; }
        if (k == FL_End && !t->altscreen) { t->scroll_to(0); return 1; }
      }
      if (e == FL_SHORTCUT) return 0;
      if (k == FL_Shift_L || k == FL_Shift_R || k == FL_Control_L || k == FL_Control_R || k == FL_Alt_L ||
          k == FL_Alt_R || k == FL_Meta_L || k == FL_Meta_R || k == FL_Caps_Lock)
        return 1;
      t->send_key(k, st, Fl::event_text());
      return 1;
    }
  }
  return Fl_Widget::handle(e);
}

// --- tworzenie ------------------------------------------------------------------------
Term::Term(const std::string &dir, const std::vector<std::string> &cmd) {
  metrics();
  rows = g_set.rows;
  cols = g_set.cols;
  int aw = cols * cw + 2 * PAD, ah = rows * chh + 2 * PAD;
  int W = aw + SCROLL_W, H = ah;
  win = new FrameWindow(W, H, "Nirvana Shell");
  win->xclass("nirvana-shell");
  area = new TermArea(this, 0, 0, aw, ah);
  sbar = new Fl_Scrollbar(aw, 0, SCROLL_W, ah - SCROLL_W);
  sbar->box(TRACK_BOX);
  sbar->slider(THUMB_BOX);
  sbar->color(MID);
  sbar->selection_color(HILITE);
  sbar->callback([](Fl_Widget *w, void *d) {
    Term *t = (Term *)d;
    t->scroll_to((int)t->sb.size() - ((Fl_Scrollbar *)w)->value());
  }, this);
  new GrowBox(aw, ah - SCROLL_W);
  auto *rz = new Fl_Box(0, 0, aw, ah - SCROLL_W);   // to rośnie przy zmianie rozmiaru
  rz->box(FL_NO_BOX);
  win->end();
  win->sizable(rz);
  win->finish();
  win->size_range(20 * cw + SCROLL_W, 5 * chh, 0, 0, cw, chh);
  win->user_data(this);
  win->callback([](Fl_Widget *w, void *) { ((Term *)w->user_data())->close_request(); });

  vt = vterm_new(rows, cols);
  vterm_set_utf8(vt, 1);
  vs = vterm_obtain_screen(vt);
  static VTermScreenCallbacks cbs = {};
  cbs.damage = cb_damage;
  cbs.moverect = cb_moverect;
  cbs.movecursor = cb_movecursor;
  cbs.settermprop = cb_settermprop;
  cbs.bell = cb_bell;
  cbs.sb_pushline = cb_pushline;
  cbs.sb_popline = cb_popline;
  cbs.sb_clear = cb_sbclear;
  vterm_screen_set_callbacks(vs, &cbs, this);
  vterm_screen_enable_altscreen(vs, 1);
  vterm_screen_enable_reflow(vs, true);
  vterm_screen_set_damage_merge(vs, VTERM_DAMAGE_SCROLL);
  vterm_output_set_callback(vt, out_cb, this);
  vterm_screen_reset(vs, 1);
  apply_profile(profile_by_id(g_set.profile));

  // powłoka w pty
  winsize ws{};
  ws.ws_row = rows;
  ws.ws_col = cols;
  child = forkpty(&pty, nullptr, nullptr, &ws);
  if (child == 0) {
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    setenv("TERM_PROGRAM", "NirvanaShell", 1);
    // Terminal mówi tylko UTF-8: bez tego vim i inne programy w locale "C"
    // pokazywałyby polskie litery jako krzaki.
    if (strcmp(nl_langinfo(CODESET), "UTF-8") != 0) setenv("LC_CTYPE", "C.UTF-8", 1);
    unsetenv("LINES");
    unsetenv("COLUMNS");
    // Domyślna obsługa sygnałów: proces uruchomiony w tle dziedziczy np.
    // zignorowany SIGINT i wtedy Ctrl+C nie przerywałby programów w terminalu.
    for (int sig : {SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD, SIGTSTP, SIGTTIN, SIGTTOU, SIGALRM})
      signal(sig, SIG_DFL);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    if (!dir.empty() && chdir(dir.c_str()) != 0) {}
    if (!cmd.empty()) {
      std::vector<char *> av;
      for (auto &a : cmd) av.push_back((char *)a.c_str());
      av.push_back(nullptr);
      execvp(av[0], av.data());
      perror(av[0]);
      _exit(127);
    }
    const char *sh = getenv("SHELL");
    if (!sh || !*sh) {
      passwd *pw = getpwuid(getuid());
      sh = pw && pw->pw_shell && *pw->pw_shell ? pw->pw_shell : "/bin/sh";
    }
    execl(sh, sh, (char *)nullptr);
    _exit(127);
  }
  if (child > 0) {
    fcntl(pty, F_SETFL, fcntl(pty, F_GETFL) | O_NONBLOCK);
    fcntl(pty, F_SETFD, FD_CLOEXEC);
    Fl::add_fd(pty, FL_READ, pty_cb, this);
  }
  g_terms.push_back(this);

  // kaskada: każde kolejne okno trochę niżej i w prawo
  int sx, sy, sw, sh;
  Fl::screen_work_area(sx, sy, sw, sh);
  int n = (int)g_terms.size() - 1;
  int px = sx + 40 + (n % 8) * 22, py = std::max(sy, MENUBAR_H) + 30 + (n % 8) * 22;
  win->place(std::min(px, sx + sw - win->w()), std::min(py, sy + sh - win->h()));
  win->show();
  area->take_focus();
}

Term::~Term() {
  g_terms.erase(std::remove(g_terms.begin(), g_terms.end(), this), g_terms.end());
  if (pty >= 0) {
    Fl::remove_fd(pty);
    close(pty);
  }
  if (child > 0) {
    kill(child, SIGHUP);
    waitpid(child, nullptr, WNOHANG);
  }
  win->hide();
  Fl::delete_widget(win);
  if (vt) vterm_free(vt);
}

void new_window(const std::string &dir) { new Term(dir, {}); }

}  // namespace

int main(int argc, char **argv) {
  setlocale(LC_ALL, "");
  signal(SIGCHLD, [](int) { while (waitpid(-1, nullptr, WNOHANG) > 0) {} });
  plat::init();
  Fl::visual(FL_DOUBLE | FL_RGB);
  load_settings();
  set_fonts();
  std::string dir;
  std::vector<std::string> cmd;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if ((a == "--katalog" || a == "--working-directory") && i + 1 < argc) dir = argv[++i];
    else if (a == "-e" && i + 1 < argc) { for (int j = i + 1; j < argc; j++) cmd.push_back(argv[j]); break; }
  }
  if (dir.empty()) { const char *h = getenv("HOME"); if (h) dir = h; }
  new Term(dir, cmd);
  // koniec, gdy zamknięto ostatnie okno
  while (!g_terms.empty()) Fl::wait();
  return 0;
}
