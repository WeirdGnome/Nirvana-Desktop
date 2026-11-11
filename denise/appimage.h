// Nirvana Desktop - denise: AppImage support
//
// An AppImage (type 2) is an ELF file (a small "runtime") with a squashfs image appended.
// Rambler never runs the runtime to read something:
// * the squashfs image offset is computed from the ELF header,
// * the icon (.DirIcon) is extracted with `unsquashfs -o OFFSET`,
// * "installation" = extracting the image to ~/.local/share/nirvana/apps/,
// * running = AppRun from the extracted folder, in a bubblewrap sandbox:
// the system read-only, writing only in the home folder (Nirvana HD),
// other users and the rest of /home invisible.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace appimage {

// Offset of the squashfs image in the file; false if this is not a type 2 AppImage.
bool squashfs_offset(const std::string &path, uint64_t &offset);

// Path to the application icon in the cache (PNG or SVG), or "" when there is none.
std::string icon(const std::string &path, const std::string &home);

// "Firefox-128.0-x86_64.AppImage" -> "Firefox"
std::string app_name(const std::string &filename);
// "Firefox-128.0-x86_64.AppImage" -> "128.0" ("" when the name has no version)
std::string version(const std::string &filename);
// "Firefox-128.0-x86_64.AppImage" -> "Firefox 128.0" (from the file name only)
std::string display_name(const std::string &filename);

// Data from the .desktop file in the AppImage (cached in ~/.cache/nirvana/icons).
struct Meta {
  std::string version;      // X-AppImage-Version
  std::string categories;   // Categories, np. "Game;ArcadeGame;"
};
Meta meta(const std::string &path, const std::string &home);

// Icon label: name and version (from the file name, and when it is not there - from .desktop).
// Thanks to the version several releases of the same program can be told apart side by side.
std::string label(const std::string &path, const std::string &home);

// Application categories (English names, N_), in the order offered at installation.
const std::vector<const char *> &categories();
// The category detected from the .desktop file in the AppImage (one of categories(), "Other" when none).
const char *detect_category(const std::string &path, const std::string &home);
// Category folder in the applications folder: "Games", "Internet"... in the interface language,
// unless the folder already exists under another name (a language change does not split the applications).
std::string category_dir_name(const std::string &home, const char *category);
// Which category a folder in the applications folder is (index into categories()), -1 if none.
int category_of_folder(const std::string &name);
std::string category_folder(const std::string &path, const std::string &home);   // detected

// The applications folder: ~/.nirvana-apps (hidden - applications are opened with the desktop icon).
std::string apps_dir(const std::string &home);
// Moves the former ~/Applications (~/Aplikacje) to ~/.nirvana-apps. Returns the old path
// (to fix remembered paths, e.g. of the dock) or "" when nothing was moved.
std::string migrate_apps_dir(const std::string &home);

// Folder with the extracted application (may not exist yet).
std::string install_dir(const std::string &path, const std::string &home);
bool is_installed(const std::string &path, const std::string &home);

// Extracts the application (blocks for the time of the extraction). Returns an error description or "".
std::string install(const std::string &path, const std::string &home);

// Does bubblewrap work on this system (checked once).
bool sandbox_available();

// Runs an installed application (in a sandbox, if possible).
void launch(const std::string &path, const std::string &home, const std::vector<std::string> &args = {});

// A child process in the background, without waiting (argv[0] looked up in PATH).
void spawn(const char *const argv[]);

} // namespace appimage
