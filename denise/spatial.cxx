// Nirvana Desktop - denise: the file manager's spatial memory
#include "spatial.h"

#include <cstdio>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

Spatial::Spatial(const std::string &home) {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  fs::path dir = xdg && *xdg ? fs::path(xdg) : fs::path(home) / ".config";
  dir /= "nirvana";
  std::error_code ec;
  fs::create_directories(dir, ec);
  file_ = (dir / "windows").string();

  // Line format: x y w h shaded open [list [icons]]<TAB>path
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
      int li = 0;
      if (ss >> li) s.list = li;   // a field added in Rambler 0.2 - older files do not have it
      int is = 0;
      if (ss >> is && (is == 16 || is == 32 || is == 64)) s.icons = is;   // od 0.3
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
          << s.shaded << ' ' << s.open << ' ' << s.list << ' ' << s.icons << '\t' << p << '\n';
  }
  std::error_code ec;
  fs::rename(tmp, file_, ec);
}

void Spatial::list_default_once() {
  std::string marker = file_.substr(0, file_.rfind('/') + 1) + ".list-default";
  if (access(marker.c_str(), F_OK) == 0) return;
  for (auto &[path, st] : map_) st.list = true;
  save();
  if (FILE *f = fopen(marker.c_str(), "w")) fclose(f);
}
