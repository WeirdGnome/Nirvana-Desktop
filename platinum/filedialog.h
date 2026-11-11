// Nirvana Desktop - libplatinum: the Nirvana file dialog (Open, Save, Choose Folder)
//
// Used by the control panels and, through nirvana-portal (xdg-desktop-portal), by other programs:
// GTK, Qt, Firefox, Chromium, Flatpaks.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace plat {

struct FileFilter {
  std::string name;                    // "Images"
  std::vector<std::string> patterns;   // globs: "*.png"; or MIME types: "image/png", "image/*"
};

struct FileDialogOptions {
  enum Mode { Open, Save, Folder } mode = Open;
  std::string title;                   // empty: "Open", "Save", "Choose Folder"
  std::string accept_label;            // empty: "Open", "Save", "Choose"
  bool multiple = false;               // Open: more than one file
  std::string folder;                  // the starting folder (empty: the last one, or home)
  std::string name;                    // Save: the proposed name
  std::vector<FileFilter> filters;     // "All files" is added at the end unless one is there
  int current_filter = 0;
  unsigned long parent_xid = 0;        // X11 window to stay above (another program's)
};

// Shows the dialog and returns at once; done() gets the chosen paths (empty = cancelled)
// and the index of the chosen filter.
void show_file_dialog(const FileDialogOptions &o,
                      std::function<void(const std::vector<std::string> &paths, int filter)> done);

// The same, waiting for the answer (a nested event loop).
std::vector<std::string> run_file_dialog(const FileDialogOptions &o, int *filter = nullptr);

// One file (Open) or a path to save to: "" when cancelled. Filters as in FLTK:
// "Images\t*.{png,jpg}" style is not used - give FileFilter entries.
std::string choose_file(const std::string &title, const std::vector<FileFilter> &filters,
                        const std::string &folder = "");

}  // namespace plat
