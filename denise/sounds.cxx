// Nirvana Desktop - Rambler: the desktop's own sounds (see sounds.h)
#include "sounds.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace sounds {
namespace {

const int RATE = 22050;

bool write_wav(const std::string &f, const std::vector<float> &s) {
  FILE *fp = fopen(f.c_str(), "wb");
  if (!fp) return false;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, fp); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, fp); };
  uint32_t data = (uint32_t)(s.size() * 2);
  fwrite("RIFF", 1, 4, fp); u32(36 + data); fwrite("WAVE", 1, 4, fp);
  fwrite("fmt ", 1, 4, fp); u32(16); u16(1); u16(1); u32(RATE); u32(RATE * 2); u16(2); u16(16);
  fwrite("data", 1, 4, fp); u32(data);
  for (float v : s) {
    int16_t x = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32000);
    fwrite(&x, 2, 1, fp);
  }
  fclose(fp);
  return true;
}

// Paper crumpled into a ball and thrown into the basket: many short dry crackles (high
// noise bursts) whose density rises and falls, a soft rustle underneath, a quiet thump at
// the end. Always the same (a fixed seed).
std::vector<float> trash_empty() {
  const double LEN = 1.15;
  std::vector<float> out((size_t)(LEN * RATE), 0.0f);
  std::mt19937 rng(98);
  std::uniform_real_distribution<double> u(0.0, 1.0);
  std::normal_distribution<double> gauss(0.0, 1.0);
  // the crackles: the paper folding
  auto density = [](double t) {   // crackles per second
    if (t < 0.04) return t / 0.04 * 260;
    if (t < 0.55) return 260 - (t - 0.04) * 180;
    if (t < 0.80) return 170 * (0.80 - t) / 0.25;
    return 0.0;
  };
  for (double t = 0; t < 0.82;) {
    double d = density(t);
    t += d > 1 ? -std::log(1 - u(rng)) / d : 0.01;
    if (t >= 0.82) break;
    int start = (int)(t * RATE);
    int len = (int)((0.002 + 0.016 * u(rng) * u(rng)) * RATE);
    double amp = 0.15 + 0.55 * std::pow(u(rng), 1.6);
    double decay = 3.0 / std::max(1, len);
    double hp_prev_in = 0, hp_prev_out = 0;
    double brightness = 0.86 + 0.12 * u(rng);   // the high-pass: dry, papery
    for (int i = 0; i < len && start + i < (int)out.size(); i++) {
      double n = gauss(rng);
      double hp = brightness * (hp_prev_out + n - hp_prev_in);
      hp_prev_in = n;
      hp_prev_out = hp;
      out[start + i] += (float)(hp * amp * std::exp(-decay * i) * 0.5);
    }
  }
  // the rustle underneath: band-limited noise, swelling with the crumpling
  double lp = 0, lp2 = 0, hpi = 0, hpo = 0;
  for (size_t i = 0; i < out.size(); i++) {
    double t = (double)i / RATE;
    double env = t < 0.05 ? t / 0.05 : t < 0.7 ? 1.0 - (t - 0.05) * 0.6 : std::max(0.0, 0.61 - (t - 0.7) * 4);
    double wob = 0.6 + 0.4 * std::sin(2 * M_PI * 7.3 * t) * std::sin(2 * M_PI * 2.1 * t + 1);
    double n = gauss(rng);
    lp += 0.35 * (n - lp);
    lp2 += 0.35 * (lp - lp2);
    double hp = 0.96 * (hpo + lp2 - hpi);
    hpi = lp2;
    hpo = hp;
    out[i] += (float)(hp * env * wob * 0.07);
  }
  // the ball lands: a short low thump with a little noise
  int land = (int)(0.92 * RATE);
  for (int i = 0; land + i < (int)out.size(); i++) {
    double t = (double)i / RATE;
    double e = std::exp(-t * 38);
    double v = std::sin(2 * M_PI * (95 - 40 * t) * t) * 0.32 * e + gauss(rng) * 0.05 * std::exp(-t * 90);
    out[land + i] += (float)v;
  }
  // a soft fade-in and fade-out against clicks, and the level
  float peak = 0;
  for (float v : out) peak = std::max(peak, std::fabs(v));
  for (size_t i = 0; i < out.size(); i++) {
    double f = std::min(1.0, i / (0.004 * RATE)) * std::min(1.0, (out.size() - i) / (0.02 * RATE));
    out[i] = (float)(out[i] / (peak > 0 ? peak : 1) * 0.7 * f);
  }
  return out;
}

std::string own_file(const std::string &name) {
  const char *x = getenv("XDG_DATA_HOME");
  const char *h = getenv("HOME");
  std::string base = (x && *x ? std::string(x) : std::string(h ? h : "/tmp") + "/.local/share") + "/nirvana/sounds/" + name;
  for (const char *ext : {".wav", ".oga", ".ogg"})
    if (access((base + ext).c_str(), R_OK) == 0) return base + ext;
  return "";
}

std::string made_file(const std::string &name) {
  const char *rt = getenv("XDG_RUNTIME_DIR");
  // the version in the name: a better sound in a new release is made again
  std::string f = std::string(rt && *rt ? rt : "/tmp") + "/nirvana-" + name + "-1-" + std::to_string(getuid()) + ".wav";
  struct stat st;
  if (stat(f.c_str(), &st) == 0 && st.st_size > 1000) return f;
  if (name == "trash-empty" && write_wav(f, trash_empty())) return f;
  return "";
}

}  // namespace

void play(const std::string &name) {
  std::string f = own_file(name);
  if (!f.empty()) {
    struct stat st;
    if (stat(f.c_str(), &st) == 0 && st.st_size == 0) return;   // an empty file: this sound is off
  } else {
    f = made_file(name);
  }
  if (f.empty()) return;
  if (fork() == 0) {
    setsid();
    if (fork() == 0) {
      execl("/bin/sh", "sh", "-c",
            "command -v paplay >/dev/null 2>&1 && exec paplay \"$0\"; "
            "command -v pw-play >/dev/null 2>&1 && exec pw-play \"$0\"; "
            "command -v aplay >/dev/null 2>&1 && exec aplay -q \"$0\"; exit 1",
            f.c_str(), (char *)nullptr);
      _exit(1);
    }
    _exit(0);
  }
  while (waitpid(-1, nullptr, WNOHANG) > 0) {}
}

}  // namespace sounds
