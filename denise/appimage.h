// Nirvana Desktop - denise: obsługa AppImage
//
// AppImage (typ 2) to plik ELF (mały "runtime") z doklejonym obrazem squashfs.
// Rambler nigdy nie uruchamia runtime'u, żeby coś odczytać:
//   * offset obrazu squashfs liczymy z nagłówka ELF,
//   * ikonę (.DirIcon) wyciągamy przez `unsquashfs -o OFFSET`,
//   * "instalacja" = rozpakowanie obrazu do ~/.local/share/nirvana/aplikacje/,
//   * uruchomienie = AppRun z rozpakowanego katalogu, w piaskownicy bubblewrap:
//     system tylko do odczytu, zapis wyłącznie w katalogu domowym (Nirvana HD),
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
std::string app_name(const std::string &filename);
// "Firefox-128.0-x86_64.AppImage" -> "128.0" ("" gdy nazwa nie ma wersji)
std::string version(const std::string &filename);
// "Firefox-128.0-x86_64.AppImage" -> "Firefox 128.0" (tylko z nazwy pliku)
std::string display_name(const std::string &filename);

// Dane z pliku .desktop w AppImage (zapamiętywane w ~/.cache/nirvana/ikony).
struct Meta {
  std::string version;      // X-AppImage-Version
  std::string categories;   // Categories, np. "Game;ArcadeGame;"
};
Meta meta(const std::string &path, const std::string &home);

// Podpis ikony: nazwa i wersja (z nazwy pliku, a gdy jej tam nie ma - z .desktop).
// Dzięki wersji kilka wydań tego samego programu da się rozróżnić obok siebie.
std::string label(const std::string &path, const std::string &home);

// Kategorie aplikacji (angielskie nazwy, N_), w kolejności wyboru przy instalacji.
const std::vector<const char *> &categories();
// Kategoria wykryta z pliku .desktop w AppImage (jedna z categories(), "Other" gdy brak).
const char *detect_category(const std::string &path, const std::string &home);
// Folder kategorii w katalogu aplikacji: "Gry", "Internet"... w języku interfejsu,
// chyba że folder już istnieje pod inną nazwą (zmiana języka nie dzieli aplikacji).
std::string category_dir_name(const std::string &home, const char *category);
std::string category_folder(const std::string &path, const std::string &home);   // wykryta

// Katalog aplikacji: ~/.nirvana-apps (ukryty - aplikacje otwiera się ikoną na biurku).
std::string apps_dir(const std::string &home);
// Przenosi dawny ~/Aplikacje (~/Applications) do ~/.nirvana-apps. Zwraca starą ścieżkę
// (do poprawienia zapamiętanych ścieżek, np. doku) albo "", gdy nic nie przeniesiono.
std::string migrate_apps_dir(const std::string &home);

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
