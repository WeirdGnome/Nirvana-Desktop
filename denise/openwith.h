// Nirvana Desktop - Rambler: "Open With" - the programs that can open a file
#pragma once
#include <string>
#include <vector>

namespace openwith {

struct App {
  std::string name, exec, icon, id;   // id: the .desktop file ("org.gnome.eog.desktop")
  bool terminal = false;              // Terminal=true: run in Nirvana Shell
};

// Programs of the menus that declare this MIME type (exact or "type/*"; for text files also
// the text/plain ones, after the others), by name
std::vector<App> for_mime(const std::string &mime);
// The commands for the files (Exec with %f %F %u %U): with %f one command per file
std::vector<std::vector<std::string>> commands(const App &a, const std::vector<std::string> &files);
void reload();

}  // namespace openwith
