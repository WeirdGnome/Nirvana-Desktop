// LisaOS - libplatinum: tapeta biurka
//
// Tło biurka to zawsze wzór w kolorach schematu, a na nim (opcjonalnie) obraz.
// Wzór widać w pasach przy układzie "Dopasuj", wokół małego obrazu "Na środku"
// i pod przezroczystymi fragmentami PNG.
#include "platinum.h"

#include <FL/Fl.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Shared_Image.H>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <strings.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace plat {

const char *const WALLPAPER_MODES[] = {"wypelnij", "dopasuj", "rozciagnij", "srodek", "kafelki", nullptr};
const char *const WALLPAPER_MODE_NAMES[] = {"Wypełnij ekran", "Dopasuj", "Rozciągnij", "Na środku",
                                            "Sąsiadująco", nullptr};

bool is_image_file(const std::string &path) {
  static const char *ext[] = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".svg", ".svgz", ".xpm", ".xbm"};
  for (auto e : ext) {
    size_t n = strlen(e);
    if (path.size() > n && strcasecmp(path.c_str() + path.size() - n, e) == 0) return true;
  }
  return false;
}

bool image_loadable(const std::string &path) {
  static bool reg = false;
  if (!reg) { fl_register_images(); reg = true; }
  Fl_Shared_Image *sh = Fl_Shared_Image::get(path.c_str());
  if (!sh) return false;
  bool ok = sh->w() > 0 && sh->h() > 0 && !sh->fail();
  sh->release();
  return ok;
}

static std::string home() { const char *h = getenv("HOME"); return h ? h : "/"; }

std::vector<std::string> wallpaper_dirs() {
  std::vector<std::string> d;
  const char *xdg = getenv("XDG_DATA_HOME");
  d.push_back((xdg && *xdg ? std::string(xdg) : home() + "/.local/share") + "/lisaos/tapety");
  std::error_code ec;
  fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    d.push_back((exe.parent_path() / "../share/lisaos/tapety").string());   // prefiks instalacji
    d.push_back((exe.parent_path() / "../tapety").string());                // katalog build/
  }
  d.push_back("/usr/local/share/lisaos/tapety");
  d.push_back("/usr/share/lisaos/tapety");
  std::vector<std::string> out;
  for (auto &x : d) if (fs::is_directory(x, ec)) out.push_back(fs::weakly_canonical(x, ec).string());
  return out;
}

std::string wallpaper_title(const std::string &path) {
  fs::path p(path);
  // KDE: Nazwa/contents/images/1920x1080.jpg -> "Nazwa"
  if (p.parent_path().filename() == "images" && p.parent_path().parent_path().filename() == "contents")
    return p.parent_path().parent_path().parent_path().filename().string();
  std::string s = p.stem().string();
  for (char &c : s) if (c == '_') c = ' ';
  return s;
}

std::vector<std::string> wallpaper_candidates() {
  std::vector<std::string> out;
  std::set<std::string> seen;
  std::error_code ec;
  auto scan = [&](const fs::path &dir, int depth) {
    std::vector<std::string> found;
    std::map<std::string, std::pair<uintmax_t, std::string>> kde;   // motyw KDE -> największy obraz
    auto opts = fs::directory_options::skip_permission_denied;
    for (auto it = fs::recursive_directory_iterator(dir, opts, ec); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
      if (ec) break;
      if (it.depth() >= depth) it.disable_recursion_pending();
      if (!it->is_regular_file(ec)) continue;
      std::string p = it->path().string();
      if (!is_image_file(p) || it->file_size(ec) == 0) continue;
      fs::path pp = it->path();
      if (pp.parent_path().filename() == "images" && pp.parent_path().parent_path().filename() == "contents") {
        std::string theme = pp.parent_path().parent_path().parent_path().string();
        uintmax_t sz = it->file_size(ec);
        if (!kde.count(theme) || kde[theme].first < sz) kde[theme] = {sz, p};
        continue;
      }
      found.push_back(p);
    }
    for (auto &[t, v] : kde) found.push_back(v.second);
    std::sort(found.begin(), found.end(), [](const std::string &a, const std::string &b) {
      return strcasecmp(wallpaper_title(a).c_str(), wallpaper_title(b).c_str()) < 0;
    });
    for (auto &p : found)
      if (out.size() < 80 && seen.insert(fs::weakly_canonical(p, ec).string()).second) out.push_back(p);
  };
  for (auto &d : wallpaper_dirs()) scan(d, 1);
  for (auto d : {"/usr/share/backgrounds", "/usr/share/wallpapers", "/usr/share/images/desktop-base"})
    if (fs::is_directory(d, ec)) scan(d, 4);
  for (auto d : {home() + "/Obrazy", home() + "/Pictures"})
    if (fs::is_directory(d, ec)) scan(d, 0);   // tylko pierwszy poziom
  return out;
}

// ---------------------------------------------------------------------------
// Skalowanie: przy dużym zmniejszeniu najpierw połowimy (dwuliniowo = średnia
// z 2x2), żeby zdjęcie z aparatu nie dostało schodków i mory.
static Fl_Image *scaled(const Fl_Image *src, int W, int H) {
  Fl_Image::RGB_scaling(FL_RGB_SCALING_BILINEAR);
  const Fl_Image *cur = src;
  bool own = false;
  while (cur->data_w() >= 2 * W && cur->data_h() >= 2 * H && cur->d() >= 1) {
    Fl_Image *half = cur->copy(cur->data_w() / 2, cur->data_h() / 2);
    if (own) delete cur;
    cur = half;
    own = true;
  }
  Fl_Image *out = cur->copy(W, H);
  if (own) delete cur;
  return out;
}

// Nałóż obraz (dowolna głębia 1-4) na bufor RGB w punkcie (ox, oy), z przycięciem.
static void blit(std::vector<unsigned char> &buf, int W, int H, Fl_Image *img, int ox, int oy) {
  if (auto *r = dynamic_cast<Fl_RGB_Image *>(img)) r->normalize();   // SVG rasteryzuje się dopiero tu
  if (!img || img->d() < 1 || !img->data() || !img->data()[0]) return;
  int iw = img->data_w(), ih = img->data_h(), d = img->d();
  int ld = img->ld() ? img->ld() : iw * d;
  const unsigned char *px = (const unsigned char *)img->data()[0];
  for (int y = std::max(0, -oy); y < ih && oy + y < H; y++) {
    const unsigned char *row = px + (size_t)y * ld;
    unsigned char *dst = &buf[((size_t)(oy + y) * W) * 3];
    for (int x = std::max(0, -ox); x < iw && ox + x < W; x++) {
      const unsigned char *s = row + (size_t)x * d;
      unsigned char r, g, b, a = 255;
      if (d <= 2) { r = g = b = s[0]; if (d == 2) a = s[1]; }
      else { r = s[0]; g = s[1]; b = s[2]; if (d == 4) a = s[3]; }
      unsigned char *o = dst + (size_t)(ox + x) * 3;
      if (a == 255) { o[0] = r; o[1] = g; o[2] = b; }
      else if (a) {
        o[0] = (unsigned char)((r * a + o[0] * (255 - a)) / 255);
        o[1] = (unsigned char)((g * a + o[1] * (255 - a)) / 255);
        o[2] = (unsigned char)((b * a + o[2] * (255 - a)) / 255);
      }
    }
  }
}

Fl_RGB_Image *render_desktop(const Appearance &a, int W, int H, bool *loaded) {
  if (loaded) *loaded = false;
  W = std::max(1, W); H = std::max(1, H);
  std::vector<unsigned char> buf((size_t)W * H * 3);
  int tw, th;
  unsigned char *tile = pattern_tile(a.pattern, tw, th);
  for (int y = 0; y < H; y++) {
    unsigned char *row = &buf[(size_t)y * W * 3];
    const unsigned char *trow = tile + (y % th) * tw * 3;
    for (int x = 0; x < W; x++) memcpy(row + x * 3, trow + (x % tw) * 3, 3);
  }
  delete[] tile;

  if (!a.wallpaper.empty()) {
    static bool reg = false;
    if (!reg) { fl_register_images(); reg = true; }
    if (Fl_Shared_Image *sh = Fl_Shared_Image::get(a.wallpaper.c_str())) {
      const Fl_Image *src = sh->image() ? sh->image() : sh;
      int iw = src->data_w(), ih = src->data_h();
      if (iw > 0 && ih > 0 && !src->fail()) {
        const std::string &m = a.wallpaper_mode;
        if (m == "kafelki" || m == "srodek") {
          // bez skalowania (SVG nie ma "naturalnej" wielkości w pikselach - rasteryzujemy 1:1)
          Fl_Image *img = src->copy(iw, ih);
          if (m == "kafelki")
            for (int y = 0; y < H; y += ih)
              for (int x = 0; x < W; x += iw) blit(buf, W, H, img, x, y);
          else
            blit(buf, W, H, img, (W - iw) / 2, (H - ih) / 2);
          delete img;
        } else {
          int dw = W, dh = H;
          if (m != "rozciagnij") {
            double sx = (double)W / iw, sy = (double)H / ih;
            double s = m == "dopasuj" ? std::min(sx, sy) : std::max(sx, sy);
            dw = std::max(1, (int)(iw * s + 0.5));
            dh = std::max(1, (int)(ih * s + 0.5));
          }
          Fl_Image *img = scaled(src, dw, dh);
          blit(buf, W, H, img, (W - dw) / 2, (H - dh) / 2);
          delete img;
        }
        if (loaded) *loaded = true;
      }
      sh->release();
    }
  }
  auto *out = new Fl_RGB_Image(new unsigned char[buf.size()], W, H, 3);
  memcpy((unsigned char *)out->data()[0], buf.data(), buf.size());
  out->alloc_array = 1;
  return out;
}

}  // namespace plat
