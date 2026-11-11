// Nirvana Desktop - screen savers
//
// Ideas from the screen savers of the 1990s and from xscreensaver (written anew here, no code
// or pictures taken): stars flying at you, bouncing lines with trails, pipes growing across the
// screen, a colour plasma, a wandering clock, snow, the bouncing Nirvana logo.
#include "savers.h"
#include "i18n.h"
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <array>
#include <deque>
#include <random>

namespace savers {

namespace {

std::mt19937 &rng() {
  static std::mt19937 r((unsigned)time(nullptr) ^ 0x5eed);
  return r;
}
double rnd(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng()); }
int rndi(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng()); }

Fl_Color hsv(double h, double s, double v) {
  h = fmod(h, 360.0);
  if (h < 0) h += 360;
  double c = v * s, x = c * (1 - fabs(fmod(h / 60.0, 2) - 1)), m = v - c, r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
  return fl_rgb_color((uchar)((r + m) * 255), (uchar)((g + m) * 255), (uchar)((b + m) * 255));
}

void black(int X, int Y, int W, int H) {
  fl_color(FL_BLACK);
  fl_rectf(X, Y, W, H);
}

// --- a black screen ----------------------------------------------------------------
struct Blank : Saver {
  void step(double) override {}
  void draw(int X, int Y) override { black(X, Y, w, h); }
};

// --- stars flying at you ------------------------------------------------------------
struct Stars : Saver {
  struct S { double x, y, z; };
  std::vector<S> s;
  void resize(int W, int H) override {
    Saver::resize(W, H);
    s.resize(std::clamp(W * H / 4000, 60, 600));
    for (auto &p : s) p = {rnd(-1, 1), rnd(-1, 1), rnd(0.05, 1)};
  }
  void step(double dt) override {
    for (auto &p : s) {
      p.z -= dt * 0.35;
      if (p.z < 0.02) p = {rnd(-1, 1), rnd(-1, 1), 1};
    }
  }
  void draw(int X, int Y) override {
    black(X, Y, w, h);
    double cx = X + w / 2.0, cy = Y + h / 2.0, k = std::max(w, h) * 0.5;
    for (auto &p : s) {
      int sx = (int)(cx + p.x / p.z * k * 0.5), sy = (int)(cy + p.y / p.z * k * 0.5);
      if (sx < X || sy < Y || sx >= X + w || sy >= Y + h) continue;
      int b = (int)std::clamp(255 * (1.2 - p.z), 40.0, 255.0);
      fl_color(fl_rgb_color(b, b, std::min(255, b + 20)));
      int r = p.z < 0.25 ? 3 : p.z < 0.5 ? 2 : 1;
      if (h < 300) r = std::max(1, r - 1);
      fl_rectf(sx, sy, r, r);
    }
  }
};

// --- two shapes of bouncing lines with fading trails ---------------------------------
struct Lines : Saver {
  struct Shape { double x[4], y[4], vx[4], vy[4], hue; std::deque<std::array<int, 8>> trail; };
  Shape sh[2];
  void resize(int W, int H) override {
    Saver::resize(W, H);
    for (int k = 0; k < 2; k++) {
      auto &s = sh[k];
      s.hue = k * 160;
      s.trail.clear();
      for (int i = 0; i < 4; i++) {
        s.x[i] = rnd(0, W);
        s.y[i] = rnd(0, H);
        double sp = std::max(W, H) * rnd(0.15, 0.35);
        s.vx[i] = rnd(0.3, 1) * sp * (rndi(0, 1) ? 1 : -1);
        s.vy[i] = rnd(0.3, 1) * sp * (rndi(0, 1) ? 1 : -1);
      }
    }
  }
  void step(double dt) override {
    for (auto &s : sh) {
      for (int i = 0; i < 4; i++) {
        s.x[i] += s.vx[i] * dt;
        s.y[i] += s.vy[i] * dt;
        if (s.x[i] < 0) { s.x[i] = 0; s.vx[i] = fabs(s.vx[i]); }
        if (s.x[i] > w - 1) { s.x[i] = w - 1; s.vx[i] = -fabs(s.vx[i]); }
        if (s.y[i] < 0) { s.y[i] = 0; s.vy[i] = fabs(s.vy[i]); }
        if (s.y[i] > h - 1) { s.y[i] = h - 1; s.vy[i] = -fabs(s.vy[i]); }
      }
      s.hue += dt * 25;
      std::array<int, 8> p;
      for (int i = 0; i < 4; i++) { p[i * 2] = (int)s.x[i]; p[i * 2 + 1] = (int)s.y[i]; }
      s.trail.push_back(p);
      while (s.trail.size() > 24) s.trail.pop_front();
    }
  }
  void draw(int X, int Y) override {
    black(X, Y, w, h);
    for (auto &s : sh) {
      int n = (int)s.trail.size();
      for (int t = 0; t < n; t++) {
        auto &p = s.trail[t];
        fl_color(hsv(s.hue + t * 3, 0.8, 0.25 + 0.75 * t / std::max(1, n - 1)));
        fl_loop(X + p[0], Y + p[1], X + p[2], Y + p[3], X + p[4], Y + p[5], X + p[6], Y + p[7]);
      }
    }
  }
};

// --- the Nirvana logo bouncing around, leaving a coloured name ---------------------
struct Logo : Saver {
  double x = 0, y = 0, vx = 0, vy = 0, hue = 0;
  int size = 64;
  void resize(int W, int H) override {
    Saver::resize(W, H);
    size = std::max(24, std::min(W, H) / 8);
    x = rnd(0, std::max(1, W - size));
    y = rnd(0, std::max(1, H - size - size / 2));
    double sp = std::max(W, H) * 0.12;
    vx = sp * (rndi(0, 1) ? 1 : -1);
    vy = sp * 0.8 * (rndi(0, 1) ? 1 : -1);
  }
  void step(double dt) override {
    int bh = size + size / 2;
    x += vx * dt;
    y += vy * dt;
    if (x < 0) { x = 0; vx = fabs(vx); hue += 50; }
    if (x > w - size) { x = w - size; vx = -fabs(vx); hue += 50; }
    if (y < 0) { y = 0; vy = fabs(vy); hue += 50; }
    if (y > h - bh) { y = h - bh; vy = -fabs(vy); hue += 50; }
  }
  void draw(int X, int Y) override {
    black(X, Y, w, h);
    plat::draw_logo(X + (int)x, Y + (int)y, size);
    fl_font(plat::FONT_SYSTEM, std::max(8, size / 4));
    fl_color(hsv(hue, 0.6, 1));
    fl_draw("Nirvana", X + (int)x, Y + (int)y + size, size, size / 2, FL_ALIGN_CENTER);
  }
};

// --- pipes growing on a grid --------------------------------------------------------
struct Pipes : Saver {
  struct Seg { int x0, y0, x1, y1; Fl_Color c; };
  std::vector<Seg> segs;
  int cell = 24, cols = 1, rows = 1, cx = 0, cy = 0, dir = 0, run = 0;
  Fl_Color col = FL_WHITE;
  std::vector<char> used;
  double acc = 0;
  void resize(int W, int H) override {
    Saver::resize(W, H);
    cell = std::max(6, std::min(W, H) / 22);
    cols = std::max(2, W / cell);
    rows = std::max(2, H / cell);
    restart();
  }
  void restart() {
    segs.clear();
    used.assign(cols * rows, 0);
    new_pipe();
  }
  void new_pipe() {
    for (int t = 0; t < 50; t++) {
      cx = rndi(0, cols - 1);
      cy = rndi(0, rows - 1);
      if (!used[cy * cols + cx]) break;
    }
    used[cy * cols + cx] = 1;
    dir = rndi(0, 3);
    run = 0;
    col = hsv(rnd(0, 360), rnd(0.5, 0.9), 1);
  }
  void step(double dt) override {
    acc += dt;
    while (acc > 0.05) {
      acc -= 0.05;
      if (rndi(0, 5) == 0 || run > 8) { dir = (dir + (rndi(0, 1) ? 1 : 3)) % 4; run = 0; }
      static const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
      int nx = cx + dx[dir], ny = cy + dy[dir];
      bool ok = nx >= 0 && ny >= 0 && nx < cols && ny < rows && !used[ny * cols + nx];
      if (!ok) {   // try the other ways, otherwise a new pipe
        for (int d = 0; d < 4 && !ok; d++) {
          nx = cx + dx[d]; ny = cy + dy[d];
          if (nx >= 0 && ny >= 0 && nx < cols && ny < rows && !used[ny * cols + nx]) { dir = d; ok = true; }
        }
      }
      if (!ok) { new_pipe(); continue; }
      segs.push_back({cx, cy, nx, ny, col});
      used[ny * cols + nx] = 1;
      cx = nx; cy = ny; run++;
      if ((int)segs.size() > cols * rows * 6 / 10) restart();
    }
  }
  void draw(int X, int Y) override {
    black(X, Y, w, h);
    int ox = X + (w - cols * cell) / 2 + cell / 2, oy = Y + (h - rows * cell) / 2 + cell / 2;
    int lw = std::max(2, cell / 3);
    // the dark edge of all pipes first, then the colour on top: smooth tubes without beads
    for (int pass = 0; pass < 2; pass++)
      for (auto &s : segs) {
        fl_color(pass ? s.c : fl_color_average(s.c, FL_BLACK, 0.45f));
        fl_line_style(FL_SOLID | FL_CAP_ROUND, pass ? lw : lw + 3);
        fl_line(ox + s.x0 * cell, oy + s.y0 * cell, ox + s.x1 * cell, oy + s.y1 * cell);
      }
    // a highlight along each pipe
    fl_line_style(FL_SOLID | FL_CAP_ROUND, std::max(1, lw / 4));
    for (auto &s : segs) {
      fl_color(fl_color_average(s.c, FL_WHITE, 0.5f));
      int o = -lw / 4;
      fl_line(ox + s.x0 * cell + o, oy + s.y0 * cell + o, ox + s.x1 * cell + o, oy + s.y1 * cell + o);
    }
    fl_line_style(0);
  }
};

// --- a slowly moving colour plasma ---------------------------------------------------
struct Plasma : Saver {
  double t = 0;
  void step(double dt) override { t += dt; }
  void draw(int X, int Y) override {
    int b = std::max(4, std::max(w, h) / 120);
    for (int y = 0; y < h; y += b) {
      double fy = (double)y / h;
      for (int x = 0; x < w; x += b) {
        double fx = (double)x / w;
        double v = sin(fx * 10 + t) + sin((fy * 10 + t) * 0.7) + sin((fx * 6 + fy * 8 + t * 1.3)) +
                   sin(sqrt((fx - 0.5) * (fx - 0.5) * 100 + (fy - 0.5) * (fy - 0.5) * 100 + 1) - t * 1.6);
        fl_color(hsv(v * 45 + t * 20, 0.7, 0.55 + 0.1 * sin(v)));
        fl_rectf(X + x, Y + y, b, b);
      }
    }
  }
};

// --- the time, slowly wandering (no burn-in) -----------------------------------------
struct Clock : Saver {
  double x = 0, y = 0, vx = 0, vy = 0;
  void resize(int W, int H) override {
    Saver::resize(W, H);
    x = W / 4.0;
    y = H / 3.0;
    vx = W * 0.02;
    vy = H * 0.025;
  }
  void step(double dt) override {
    x += vx * dt;
    y += vy * dt;
    int bw = w / 3, bh = h / 4;
    if (x < 0 || x > w - bw) { vx = -vx; x = std::clamp(x, 0.0, (double)std::max(0, w - bw)); }
    if (y < 0 || y > h - bh) { vy = -vy; y = std::clamp(y, 0.0, (double)std::max(0, h - bh)); }
  }
  void draw(int X, int Y) override {
    black(X, Y, w, h);
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char t[16];
    strftime(t, sizeof t, "%H:%M", &tm);
    int bw = w / 3, bh = h / 4;
    fl_font(plat::FONT_SYSTEM, std::max(10, bh / 2));
    fl_color(fl_rgb_color(0x9C, 0xB4, 0xE8));
    fl_draw(t, X + (int)x, Y + (int)y, bw, bh * 2 / 3, FL_ALIGN_CENTER);
    std::string d = plat::weekday_name(tm.tm_wday) + ", " + std::to_string(tm.tm_mday) + " " +
                    plat::month_in_date(tm.tm_mon);
    fl_font(plat::FONT_SMALL, std::max(7, bh / 8));
    fl_color(fl_rgb_color(0x70, 0x80, 0xA8));
    fl_draw(d.c_str(), X + (int)x, Y + (int)y + bh * 2 / 3, bw, bh / 3, FL_ALIGN_CENTER | FL_ALIGN_TOP);
  }
};

// --- snow falling and drifting ------------------------------------------------------
struct Snow : Saver {
  struct F { double x, y, v, ph, r; };
  std::vector<F> f;
  std::vector<double> ground;   // the height of the snow on the ground
  double t = 0;
  void resize(int W, int H) override {
    Saver::resize(W, H);
    f.resize(std::clamp(W * H / 2500, 80, 900));
    for (auto &p : f) p = {rnd(0, W), rnd(0, H), rnd(0.03, 0.09) * H, rnd(0, 6.28), rnd(1, 3.2) * std::max(1, H / 400)};
    ground.assign(std::max(1, W / 4 + 1), 0);
  }
  void step(double dt) override {
    t += dt;
    for (auto &p : f) {
      p.y += p.v * dt;
      p.x += sin(t * 0.8 + p.ph) * 12 * dt * std::max(1, h / 400);
      int gi = std::clamp((int)(p.x / 4), 0, (int)ground.size() - 1);
      if (p.y > h - ground[gi]) {
        if (ground[gi] < h * 0.15) ground[gi] += 0.6;
        p = {rnd(0, w), -5, rnd(0.03, 0.09) * h, rnd(0, 6.28), rnd(1, 3.2) * std::max(1, h / 400)};
      }
    }
    if (t > 600) { t = 0; std::fill(ground.begin(), ground.end(), 0); }   // a new winter
  }
  void draw(int X, int Y) override {
    for (int y = 0; y < h; y += 4) {   // the night sky
      fl_color(fl_color_average(fl_rgb_color(0x22, 0x2C, 0x4A), fl_rgb_color(0x05, 0x07, 0x14), (float)y / h));
      fl_rectf(X, Y + y, w, 4);
    }
    fl_color(fl_rgb_color(0xE8, 0xEE, 0xF8));
    for (size_t i = 0; i < ground.size(); i++) {
      int gh = (int)ground[i];
      if (gh > 0) fl_rectf(X + (int)i * 4, Y + h - gh, 4, gh);
    }
    for (auto &p : f) {
      int r = (int)p.r;
      fl_pie(X + (int)p.x - r, Y + (int)p.y - r, r * 2 + 1, r * 2 + 1, 0, 360);
    }
  }
};

}  // namespace

std::vector<std::pair<std::string, std::string>> list() {
  return {{"blank", _("Black Screen")}, {"stars", _("Starfield")}, {"lines", _("Bouncing Lines")},
          {"logo", _("Nirvana Logo")},  {"pipes", _("Pipes")},     {"plasma", _("Plasma")},
          {"clock", _("Clock")},        {"snow", _("Snow")},       {"random", _("Random")}};
}

std::string name(const std::string &id) {
  for (auto &[i, n] : list()) if (i == id) return n;
  return id;
}

Saver *make(const std::string &id0, int W, int H) {
  std::string id = id0;
  if (id == "random") {
    const char *all[] = {"stars", "lines", "logo", "pipes", "plasma", "clock", "snow"};
    id = all[rndi(0, 6)];
  }
  Saver *s;
  if (id == "stars") s = new Stars;
  else if (id == "lines") s = new Lines;
  else if (id == "logo") s = new Logo;
  else if (id == "pipes") s = new Pipes;
  else if (id == "plasma") s = new Plasma;
  else if (id == "clock") s = new Clock;
  else if (id == "snow") s = new Snow;
  else s = new Blank;
  s->resize(W, H);
  return s;
}

}  // namespace savers
