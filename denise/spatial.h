// Nirvana Desktop - denise: the file manager's spatial memory
// Every folder remembers where and how large its window was, and whether it was open.
// File: $XDG_CONFIG_HOME/nirvana/windows (by default ~/.config/...)
#pragma once
#include <map>
#include <string>
#include <vector>

struct WinState {
  int x = 0, y = 0, w = 0, h = 0;
  bool shaded = false;
  bool open = false;
  bool list = false;   // list view instead of icons
  int icons = 32;      // icon size: 16, 32, 64
};

class Spatial {
public:
  explicit Spatial(const std::string &home);
  bool get(const std::string &path, WinState &out) const;
  void set(const std::string &path, const WinState &s);
  std::vector<std::string> open_paths() const;   // windows open at exit
  void save() const;

private:
  std::string file_;
  std::map<std::string, WinState> map_;
};
