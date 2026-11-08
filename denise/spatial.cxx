// LisaOS - denise: pamięć przestrzenna Findera
#include "spatial.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

Spatial::Spatial(const std::string &home) {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  fs::path dir = xdg && *xdg ? fs::path(xdg) : fs::path(home) / ".config";
  dir /= "lisaos";
  std::error_code ec;
  fs::create_directories(dir, ec);
  file_ = (dir / "denise.okna").string();

  // Format linii: x y w h zwinięte otwarte<TAB>ścieżka
  std::ifstream in(file_);
  std::string line;
  while (std::getline(in, line)) {
    auto tab = line.find('\t');
    if (tab == std::string::npos) continue;
    std::istringstream ss(line.substr(0, tab));
    WinState s;
    int sh = 0, op = 0;
    if (ss >> s.x >> s.y >> s.w >> s.h >> sh >> op) {
      s.shaded = sh; s.open = op;
      map_[line.substr(tab + 1)] = s;
    }
  }
}

bool Spatial::get(const std::string &path, WinState &out) const {
  auto it = map_.find(path);
  if (it == map_.end()) return false;
  out = it->second;
  return true;
}

void Spatial::set(const std::string &path, const WinState &s) {
  map_[path] = s;
  save();
}

std::vector<std::string> Spatial::open_paths() const {
  std::vector<std::string> v;
  for (auto &[p, s] : map_)
    if (s.open) v.push_back(p);
  return v;
}

void Spatial::save() const {
  std::string tmp = file_ + ".tmp";
  {
    std::ofstream out(tmp);
    for (auto &[p, s] : map_)
      out << s.x << ' ' << s.y << ' ' << s.w << ' ' << s.h << ' '
          << s.shaded << ' ' << s.open << '\t' << p << '\n';
  }
  std::error_code ec;
  fs::rename(tmp, file_, ec);
}
