// LisaOS - denise: obsługa AppImage
//
// AppImage (typ 2) to plik ELF (mały "runtime") z doklejonym obrazem squashfs.
// Rambler nigdy nie uruchamia runtime'u, żeby coś odczytać:
//   * offset obrazu squashfs liczymy z nagłówka ELF,
//   * ikonę (.DirIcon) wyciągamy przez `unsquashfs -o OFFSET`,
//   * "instalacja" = rozpakowanie obrazu do ~/.local/share/lisaos/aplikacje/,
//   * uruchomienie = AppRun z rozpakowanego katalogu, w piaskownicy bubblewrap:
//     system tylko do odczytu, zapis wyłącznie w katalogu domowym (LisaOS HD),
//     inni użytkownicy i reszta /home niewidoczni.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace appimage {

// Offset obrazu squashfs w pliku; false, jeśli to nie AppImage typu 2.
bool squashfs_offset(const std::string &path, uint64_t &offset);

// Ścieżka do ikony aplikacji w cache (PNG lub SVG) albo "" gdy brak.
std::string icon(const std::string &path, const std::string &home);

// "Firefox-128.0-x86_64.AppImage" -> "Firefox"
std::string display_name(const std::string &filename);

// Katalog z rozpakowaną aplikacją (może jeszcze nie istnieć).
std::string install_dir(const std::string &path, const std::string &home);
bool is_installed(const std::string &path, const std::string &home);

// Rozpakowuje aplikację (blokuje na czas rozpakowania). Zwraca opis błędu lub "".
std::string install(const std::string &path, const std::string &home);

// Czy bubblewrap działa w tym systemie (sprawdzane raz).
bool sandbox_available();

// Uruchamia zainstalowaną aplikację (w piaskownicy, jeśli to możliwe).
void launch(const std::string &path, const std::string &home, const std::vector<std::string> &args = {});

// Proces potomny w tle, bez czekania (argv[0] szukane w PATH).
void spawn(const char *const argv[]);

} // namespace appimage
