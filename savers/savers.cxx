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
#include <numeric>
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

// --- fireworks: rockets rising and bursting ------------------------------------------
struct Fireworks : Saver {
  struct P { double x, y, vx, vy, life, max; Fl_Color c; bool rocket; double hue; };
  std::vector<P> ps;
  double next = 0;
  void step(double dt) override {
    next -= dt;
    if (next <= 0) {
      next = rnd(0.3, 1.1);
      double hue = rnd(0, 360);
      ps.push_back({rnd(w * 0.15, w * 0.85), (double)h, rnd(-0.05, 0.05) * w, -rnd(0.55, 0.8) * h,
                    rnd(0.9, 1.3), 2, hsv(hue, 0.3, 1), true, hue});
    }
    double g = h * 0.35;
    std::vector<P> born;
    for (auto &p : ps) {
      p.vy += g * dt * (p.rocket ? 0.5 : 1);
      if (!p.rocket) { p.vx *= 1 - dt * 0.8; p.vy *= 1 - dt * 0.8; }
      p.x += p.vx * dt;
      p.y += p.vy * dt;
      p.life -= dt;
      if (p.rocket && (p.life <= 0 || p.vy > -h * 0.05)) {   // the burst
        int n = rndi(50, 110);
        double sp = std::min(w, h) * rnd(0.25, 0.45);
        bool two = rndi(0, 2) == 0;
        for (int i = 0; i < n; i++) {
          double a = rnd(0, 6.283), v = sp * sqrt(rnd(0.05, 1));
          double hue = two && i % 2 ? p.hue + 150 : p.hue;
          born.push_back({p.x, p.y, cos(a) * v, sin(a) * v, rnd(1.2, 2.2), 2.2, hsv(hue, 0.85, 1), false, hue});
        }
        p.life = -1;
      }
    }
    ps.erase(std::remove_if(ps.begin(), ps.end(), [](const P &p) { return p.life <= 0; }), ps.end());
    ps.insert(ps.end(), born.begin(), born.end());
  }
  void draw(int X, int Y) override {
    black(X, Y, w, h);
    int r = std::max(2, h / 220);
    for (auto &p : ps) {
      float f = (float)std::clamp(p.life / p.max, 0.0, 1.0);
      Fl_Color c = p.rocket ? p.c : fl_color_average(p.c, FL_BLACK, std::min(1.0f, f * 1.6f));
      if (!p.rocket) {   // a fading tail behind each spark
        fl_color(fl_color_average(c, FL_BLACK, 0.45f));
        fl_line(X + (int)p.x, Y + (int)p.y, X + (int)(p.x - p.vx * 0.06), Y + (int)(p.y - p.vy * 0.06));
      }
      fl_color(c);
      fl_rectf(X + (int)p.x, Y + (int)p.y, r + (p.rocket ? 1 : 0), r + (p.rocket ? 1 : 0));
      if (p.rocket) {   // a short trail of sparks
        fl_color(fl_rgb_color(0xFF, 0xB0, 0x40));
        fl_rectf(X + (int)(p.x - p.vx * 0.03), Y + (int)(p.y - p.vy * 0.03), r, r);
      }
    }
  }
};

// --- Conway's Game of Life ----------------------------------------------------------
struct Life : Saver {
  int cols = 1, rows = 1, cell = 8, gen = 0, still = 0;
  std::vector<unsigned char> a, b;   // 0 dead, otherwise the age (capped)
  double acc = 0, hue = 0;
  size_t last_pop = 0;
  void resize(int W, int H) override {
    Saver::resize(W, H);
    cell = std::max(3, std::min(W, H) / 80);
    cols = std::max(4, W / cell);
    rows = std::max(4, H / cell);
    seed();
  }
  void seed() {
    a.assign(cols * rows, 0);
    for (auto &c : a) c = rndi(0, 99) < 22 ? 1 : 0;
    gen = 0;
    still = 0;
    hue = rnd(0, 360);
  }
  void step(double dt) override {
    acc += dt;
    if (acc < 0.11) return;
    acc = 0;
    b.assign(a.size(), 0);
    size_t pop = 0;
    for (int y = 0; y < rows; y++)
      for (int x = 0; x < cols; x++) {
        int n = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++)
            if ((dx || dy) && a[((y + dy + rows) % rows) * cols + (x + dx + cols) % cols]) n++;
        unsigned char c = a[y * cols + x];
        unsigned char v = (c && (n == 2 || n == 3)) ? (unsigned char)std::min(200, c + 1) : (!c && n == 3) ? 1 : 0;
        b[y * cols + x] = v;
        if (v) pop++;
      }
    a.swap(b);
    gen++;
    still = (pop == last_pop) ? still + 1 : 0;
    last_pop = pop;
    if (gen > 900 || still > 40 || pop < (size_t)(cols * rows / 200)) seed();   // a new world
  }
  void draw(int X, int Y) override {
    black(X, Y, w, h);
    int ox = X + (w - cols * cell) / 2, oy = Y + (h - rows * cell) / 2;
    for (int y = 0; y < rows; y++)
      for (int x = 0; x < cols; x++) {
        unsigned char c = a[y * cols + x];
        if (!c) continue;
        fl_color(hsv(hue + std::min(120, (int)c * 3), 0.7, c < 3 ? 1.0 : 0.8));
        fl_rectf(ox + x * cell, oy + y * cell, std::max(1, cell - 1), std::max(1, cell - 1));
      }
  }
};

// --- a maze: carved, then walked through ---------------------------------------------
struct Maze : Saver {
  int cols = 1, rows = 1, cell = 16;
  std::vector<unsigned char> walls;   // bits: 1 right, 2 down (open passages)
  std::vector<char> seen;
  std::vector<int> stack, path;
  enum { Carve, Solve, Done } phase = Carve;
  double acc = 0, pause = 0;
  void resize(int W, int H) override {
    Saver::resize(W, H);
    cell = std::max(6, std::min(W, H) / 28);
    cols = std::max(3, W / cell - 1);
    rows = std::max(3, H / cell - 1);
    start();
  }
  void start() {
    walls.assign(cols * rows, 0);
    seen.assign(cols * rows, 0);
    stack = {0};
    seen[0] = 1;
    path.clear();
    phase = Carve;
  }
  bool open(int a, int b) const {   // a passage between neighbouring cells
    if (b == a + 1) return walls[a] & 1;
    if (b == a - 1) return walls[b] & 1;
    if (b == a + cols) return walls[a] & 2;
    if (b == a - cols) return walls[b] & 2;
    return false;
  }
  std::vector<int> nb(int c) const {
    std::vector<int> v;
    int x = c % cols, y = c / cols;
    if (x > 0) v.push_back(c - 1);
    if (x < cols - 1) v.push_back(c + 1);
    if (y > 0) v.push_back(c - cols);
    if (y < rows - 1) v.push_back(c + cols);
    return v;
  }
  void one() {
    if (phase == Carve) {
      if (stack.empty()) { phase = Solve; seen.assign(cols * rows, 0); path = {0}; seen[0] = 1; return; }
      int c = stack.back();
      std::vector<int> free;
      for (int n : nb(c)) if (!seen[n]) free.push_back(n);
      if (free.empty()) { stack.pop_back(); return; }
      int n = free[rndi(0, (int)free.size() - 1)];
      int lo = std::min(c, n), hi = std::max(c, n);
      walls[lo] |= hi == lo + 1 ? 1 : 2;
      seen[n] = 1;
      stack.push_back(n);
    } else if (phase == Solve) {
      int c = path.back();
      if (c == cols * rows - 1) { phase = Done; pause = 3; return; }
      for (int n : nb(c))
        if (!seen[n] && open(c, n)) { seen[n] = 1; path.push_back(n); return; }
      path.pop_back();   // a dead end: back
    }
  }
  void step(double dt) override {
    if (phase == Done) { pause -= dt; if (pause <= 0) start(); return; }
    acc += dt;
    int n = (int)(acc / (phase == Carve ? 0.004 : 0.02));
    acc -= n * (phase == Carve ? 0.004 : 0.02);
    for (int i = 0; i < n && phase != Done; i++) one();
  }
  void draw(int X, int Y) override {
    black(X, Y, w, h);
    int ox = X + (w - cols * cell) / 2, oy = Y + (h - rows * cell) / 2;
    // the walk
    fl_color(fl_rgb_color(0x3A, 0x7B, 0xD5));
    for (int c : path) fl_rectf(ox + c % cols * cell + 2, oy + c / cols * cell + 2, cell - 3, cell - 3);
    if (!path.empty()) {
      fl_color(fl_rgb_color(0xF2, 0xC2, 0x30));
      int c = path.back();
      fl_rectf(ox + c % cols * cell + 2, oy + c / cols * cell + 2, cell - 3, cell - 3);
    }
    fl_color(fl_rgb_color(0x3D, 0xB5, 0x4A));   // the way out
    fl_rectf(ox + (cols - 1) * cell + 2, oy + (rows - 1) * cell + 2, cell - 3, cell - 3);
    // the walls of the carved part
    fl_color(fl_rgb_color(0xC8, 0xC8, 0xD8));
    fl_line_style(FL_SOLID, std::max(1, cell / 8));
    for (int y = 0; y < rows; y++)
      for (int x = 0; x < cols; x++) {
        int c = y * cols + x;
        if (phase == Carve && !seen[c]) continue;
        int px = ox + x * cell, py = oy + y * cell;
        if (!(walls[c] & 1)) fl_line(px + cell, py, px + cell, py + cell);
        if (!(walls[c] & 2)) fl_line(px, py + cell, px + cell, py + cell);
        if (x == 0) fl_line(px, py, px, py + cell);
        if (y == 0) fl_line(px, py, px + cell, py);
      }
    fl_line_style(0);
  }
};

// --- a spirograph drawing itself ----------------------------------------------------
struct Spiro : Saver {
  double R = 1, r = 1, d = 1, t = 0, tmax = 0, hue = 0;
  std::vector<std::pair<float, float>> pts;
  double pause = 0;
  void resize(int W, int H) override { Saver::resize(W, H); start(); }
  void start() {
    int a = rndi(5, 13), b = rndi(2, 9);
    while (std::gcd(a, b) != 1 || a == b) { a = rndi(5, 13); b = rndi(2, 9); }
    double S = std::min(w, h) * 0.42;
    R = S;
    r = S * b / a;
    d = r * rnd(0.5, 1.2);
    t = 0;
    tmax = 2 * M_PI * b;
    hue = rnd(0, 360);
    pts.clear();
  }
  void step(double dt) override {
    if (pause > 0) { pause -= dt; if (pause <= 0) start(); return; }
    for (int i = 0; i < 12; i++) {
      t += dt * 0.6;
      double x = (R - r) * cos(t) + d * cos((R - r) / r * t), y = (R - r) * sin(t) - d * sin((R - r) / r * t);
      pts.push_back({(float)x, (float)y});
      if (t >= tmax) { pause = 4; break; }
    }
  }
  void draw(int X, int Y) override {
    black(X, Y, w, h);
    int cx = X + w / 2, cy = Y + h / 2;
    fl_line_style(FL_SOLID, std::max(1, h / 350));
    for (size_t i = 1; i < pts.size(); i++) {
      fl_color(hsv(hue + i * 0.15, 0.75, 1));
      fl_line(cx + (int)pts[i - 1].first, cy + (int)pts[i - 1].second, cx + (int)pts[i].first, cy + (int)pts[i].second);
    }
    fl_line_style(0);
  }
};

// --- Nirvana chips flying at you ----------------------------------------------------
struct Chips : Saver {
  struct C { double x, y, z; };
  std::vector<C> c;
  void resize(int W, int H) override {
    Saver::resize(W, H);
    c.resize(18);
    for (auto &p : c) p = {rnd(-1, 1), rnd(-1, 1), rnd(0.2, 1.4)};
  }
  void step(double dt) override {
    for (auto &p : c) {
      p.z -= dt * 0.22;
      if (p.z < 0.12) p = {rnd(-1, 1), rnd(-1, 1), 1.4};
    }
    std::sort(c.begin(), c.end(), [](const C &a, const C &b) { return a.z > b.z; });   // far ones first
  }
  void draw(int X, int Y) override {
    black(X, Y, w, h);
    double k = std::max(w, h) * 0.5;
    for (auto &p : c) {
      int size = (int)(std::min(w, h) * 0.07 / p.z);
      if (size < 4) continue;
      int sx = (int)(X + w / 2.0 + p.x / p.z * k * 0.6) - size / 2, sy = (int)(Y + h / 2.0 + p.y / p.z * k * 0.6) - size / 2;
      if (sx > X + w || sy > Y + h || sx + size < X || sy + size < Y) continue;
      plat::draw_logo(sx, sy, size);
    }
  }
};

// --- an aquarium: fish, bubbles and waving weed -------------------------------------
struct Aquarium : Saver {
  struct Fish { double x, y, v, size, ph; Fl_Color c, fin; };
  struct Bub { double x, y, r; };
  std::vector<Fish> fish;
  std::vector<Bub> bubs;
  double t = 0;
  void resize(int W, int H) override {
    Saver::resize(W, H);
    fish.clear();
    int n = std::clamp(W * H / 60000, 5, 14);
    for (int i = 0; i < n; i++) fish.push_back(new_fish(rnd(0, W)));
    bubs.clear();
  }
  Fish new_fish(double x) {
    double size = std::min(w, h) * rnd(0.04, 0.09);
    double dir = rndi(0, 1) ? 1 : -1;
    const unsigned col[] = {0xF07A2E, 0xF2C230, 0xE5483F, 0x3A9BD5, 0xC86FD8, 0xF4F0E0};
    unsigned cc = col[rndi(0, 5)];
    Fl_Color c = fl_rgb_color(cc >> 16, cc >> 8 & 255, cc & 255);
    return {x, rnd(h * 0.1, h * 0.75), dir * size * rnd(0.6, 1.4), size, rnd(0, 6), c, fl_color_average(c, FL_BLACK, 0.7f)};
  }
  void step(double dt) override {
    t += dt;
    for (auto &f : fish) {
      f.x += f.v * dt;
      f.y += sin(t * 0.8 + f.ph) * f.size * 0.15 * dt;
      if (f.x < -f.size * 2 || f.x > w + f.size * 2) {
        f = new_fish(0);
        f.x = f.v > 0 ? -f.size * 1.5 : w + f.size * 1.5;
      }
      if (rndi(0, 400) == 0) bubs.push_back({f.x, f.y, f.size * 0.08});
    }
    if (rndi(0, 8) == 0) bubs.push_back({w * 0.82 + rnd(-4, 4), h * 0.92, std::max(2.0, h * 0.008)});
    for (auto &b : bubs) { b.y -= h * 0.12 * dt; b.x += sin(t * 3 + b.y * 0.05) * 0.4; }
    bubs.erase(std::remove_if(bubs.begin(), bubs.end(), [](const Bub &b) { return b.y < 0; }), bubs.end());
  }
  void draw(int X, int Y) override {
    for (int y = 0; y < h; y += 4) {   // the water, darker deeper down
      fl_color(fl_color_average(fl_rgb_color(0x06, 0x22, 0x48), fl_rgb_color(0x1C, 0x6E, 0x9C), (float)y / h));
      fl_rectf(X, Y + y, w, 4);
    }
    // weed
    fl_line_style(FL_SOLID | FL_CAP_ROUND, std::max(2, w / 200));
    for (int i = 0; i < 9; i++) {
      double bx = w * (0.06 + i * 0.11), top = h * (0.55 + 0.08 * (i % 3));
      fl_color(fl_rgb_color(0x2E, 0x8A + 8 * (i % 3), 0x4E));
      double px = bx, py = h * 0.95;
      for (int s = 1; s <= 8; s++) {
        double yy = h * 0.95 - (h * 0.95 - top) * s / 8, xx = bx + sin(t * 1.2 + i + s * 0.5) * s * w * 0.004;
        fl_line(X + (int)px, Y + (int)py, X + (int)xx, Y + (int)yy);
        px = xx; py = yy;
      }
    }
    fl_line_style(0);
    // sand
    fl_color(fl_rgb_color(0xC8, 0xB0, 0x78));
    fl_rectf(X, Y + (int)(h * 0.94), w, h - (int)(h * 0.94));
    // fish: a body, a tail, an eye
    for (auto &f : fish) {
      int fx = X + (int)f.x, fy = Y + (int)f.y, L = (int)f.size, H = (int)(f.size * 0.55);
      double dir = f.v > 0 ? 1 : -1;
      double wag = sin(t * 6 + f.ph) * H * 0.25;
      fl_color(f.fin);
      fl_polygon(fx - (int)(dir * L * 0.45), fy, fx - (int)(dir * L * 0.85), fy - H / 2 + (int)wag,
                 fx - (int)(dir * L * 0.85), fy + H / 2 + (int)wag);
      fl_color(f.c);
      fl_pie(fx - L / 2, fy - H / 2, L, H, 0, 360);
      fl_color(FL_WHITE);
      int ex = fx + (int)(dir * L * 0.25), er = std::max(2, H / 5);
      fl_pie(ex - er / 2, fy - H / 5 - er / 2, er, er, 0, 360);
      fl_color(FL_BLACK);
      fl_pie(ex - er / 4, fy - H / 5 - er / 4, std::max(1, er / 2), std::max(1, er / 2), 0, 360);
    }
    fl_color(fl_rgb_color(0xC8, 0xEC, 0xFF));
    for (auto &b : bubs) { int r = (int)b.r; fl_arc(X + (int)b.x - r, Y + (int)b.y - r, 2 * r + 1, 2 * r + 1, 0, 360); }
  }
};

// --- the classic demo fire ------------------------------------------------------------
struct Fire : Saver {
  int cols = 1, rows = 1, cell = 6;
  std::vector<unsigned char> heat;
  double acc = 0;
  void resize(int W, int H) override {
    Saver::resize(W, H);
    cell = std::max(2, std::max(W, H) / 220);
    cols = W / cell + 1;
    rows = H / cell + 1;
    heat.assign(cols * rows, 0);
  }
  void step(double dt) override {
    acc += dt;
    while (acc > 1.0 / 30) {
      acc -= 1.0 / 30;
      for (int x = 0; x < cols; x++) heat[(rows - 1) * cols + x] = rndi(0, 9) < 6 ? (unsigned char)rndi(160, 255) : 0;
      for (int y = 0; y < rows - 1; y++)
        for (int x = 0; x < cols; x++) {
          int s = heat[(y + 1) * cols + x] + heat[(y + 1) * cols + (x + cols - 1) % cols] +
                  heat[(y + 1) * cols + (x + 1) % cols] + heat[std::min(rows - 1, y + 2) * cols + x];
          int v = s / 4 - rndi(0, 3) * 160 / std::max(1, rows);
          heat[y * cols + x] = (unsigned char)std::clamp(v, 0, 255);
        }
    }
  }
  void draw(int X, int Y) override {
    black(X, Y, w, h);
    for (int y = 0; y < rows; y++)
      for (int x = 0; x < cols; x++) {
        int v = heat[y * cols + x];
        if (v < 16) continue;
        uchar r = (uchar)std::min(255, v * 2), g = (uchar)std::clamp(v * 2 - 200, 0, 255), b = (uchar)std::clamp(v * 3 - 600, 0, 255);
        fl_color(fl_rgb_color(r, g, b));
        fl_rectf(X + x * cell, Y + y * cell, cell, cell);
      }
  }
};

// --- bubbles floating up -------------------------------------------------------------
struct Bubbles : Saver {
  struct B { double x, y, r, v, ph, hue; };
  std::vector<B> b;
  double t = 0;
  void resize(int W, int H) override {
    Saver::resize(W, H);
    b.clear();
    int n = std::clamp(W * H / 25000, 12, 60);
    for (int i = 0; i < n; i++) b.push_back(make1(rnd(0, H)));
  }
  B make1(double y) {
    double r = std::min(w, h) * rnd(0.015, 0.07);
    return {rnd(0, w), y, r, h * rnd(0.04, 0.1) * (0.06 / (r / std::min(w, h) + 0.01)), rnd(0, 6), rnd(170, 300)};
  }
  void step(double dt) override {
    t += dt;
    for (auto &p : b) {
      p.y -= p.v * dt;
      p.x += sin(t + p.ph) * p.r * 0.4 * dt;
      if (p.y < -p.r * 2) { p = make1(h + p.r * 2); }
    }
  }
  void draw(int X, int Y) override {
    for (int y = 0; y < h; y += 4) {
      fl_color(fl_color_average(fl_rgb_color(0x10, 0x10, 0x30), fl_rgb_color(0x20, 0x0C, 0x40), (float)y / h));
      fl_rectf(X, Y + y, w, 4);
    }
    for (auto &p : b) {
      int r = (int)p.r, cx = X + (int)p.x, cy = Y + (int)p.y;
      fl_color(hsv(p.hue + t * 10, 0.5, 0.9));
      fl_line_style(FL_SOLID, std::max(1, r / 12));
      fl_arc(cx - r, cy - r, 2 * r, 2 * r, 0, 360);
      fl_line_style(0);
      fl_color(fl_rgb_color(0xFF, 0xFF, 0xFF));
      fl_pie(cx - r / 2, cy - r / 2, std::max(2, r / 4), std::max(2, r / 4), 0, 360);
    }
  }
};

}  // namespace

std::vector<std::pair<std::string, std::string>> list() {
  return {{"blank", _("Black Screen")}, {"stars", _("Starfield")}, {"lines", _("Bouncing Lines")},
          {"logo", _("Nirvana Logo")},  {"pipes", _("Pipes")},     {"plasma", _("Plasma")},
          {"clock", _("Clock")},        {"snow", _("Snow")},       {"fireworks", _("Fireworks")},
          {"life", _("Game of Life")},  {"maze", _("Maze")},       {"spiro", _("Spirograph")},
          {"chips", _("Flying Chips")}, {"aquarium", _("Aquarium")}, {"fire", _("Fire")},
          {"bubbles", _("Bubbles")},    {"random", _("Random")}};
}

std::string name(const std::string &id) {
  for (auto &[i, n] : list()) if (i == id) return n;
  return id;
}

Saver *make(const std::string &id0, int W, int H) {
  std::string id = id0;
  if (id == "random") {
    const char *all[] = {"stars", "lines", "logo", "pipes", "plasma", "clock", "snow", "fireworks",
                         "life", "maze", "spiro", "chips", "aquarium", "fire", "bubbles"};
    id = all[rndi(0, (int)(sizeof all / sizeof *all) - 1)];
  }
  Saver *s;
  if (id == "stars") s = new Stars;
  else if (id == "lines") s = new Lines;
  else if (id == "logo") s = new Logo;
  else if (id == "pipes") s = new Pipes;
  else if (id == "plasma") s = new Plasma;
  else if (id == "clock") s = new Clock;
  else if (id == "snow") s = new Snow;
  else if (id == "fireworks") s = new Fireworks;
  else if (id == "life") s = new Life;
  else if (id == "maze") s = new Maze;
  else if (id == "spiro") s = new Spiro;
  else if (id == "chips") s = new Chips;
  else if (id == "aquarium") s = new Aquarium;
  else if (id == "fire") s = new Fire;
  else if (id == "bubbles") s = new Bubbles;
  else s = new Blank;
  s->resize(W, H);
  return s;
}

}  // namespace savers
