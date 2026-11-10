// Nirvana Desktop - denise: pamięć przestrzenna menedżera plików
// Każdy folder pamięta, gdzie i jak duże było jego okno oraz czy było otwarte.
// Plik: $XDG_CONFIG_HOME/nirvana/denise.okna (domyślnie ~/.config/...)
#pragma once
#include <map>
#include <string>
#include <vector>

struct WinState {
  int x = 0, y = 0, w = 0, h = 0;
  bool shaded = false;
  bool open = false;
  bool list = false;   // widok listy zamiast ikon
  int icons = 32;      // wielkość ikon: 16, 32, 64
};

class Spatial {
public:
  explicit Spatial(const std::string &home);
  bool get(const std::string &path, WinState &out) const;
  void set(const std::string &path, const WinState &s);
  std::vector<std::string> open_paths() const;   // okna otwarte przy wyjściu
  void save() const;

private:
  std::string file_;
  std::map<std::string, WinState> map_;
};
