// LisaOS - denise: obsługa AppImage
#include "appimage.h"

#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <vector>

namespace fs = std::filesystem;

namespace appimage {

// ---------------------------------------------------------------------------
// Procesy
// ---------------------------------------------------------------------------

// Uruchom i poczekaj; wyjście programu idzie do /dev/null. Zwraca kod wyjścia.
static int run_wait(const std::vector<std::string> &args) {
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    int null = open("/dev/null", O_RDWR);
    if (null >= 0) { dup2(null, 1); dup2(null, 2); }
    std::vector<char *> argv;
    for (auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  int st = 0;
  if (waitpid(pid, &st, 0) < 0) return -1;
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

// Podwójny fork: aplikacja staje się dzieckiem init, więc nie zostawia zombie,
// a Rambler może bezpiecznie czekać na własne procesy (unsquashfs).
void spawn(const char *const argv[]) {
  pid_t pid = fork();
  if (pid < 0) return;
  if (pid == 0) {
    if (fork() == 0) {
      setsid();
      execvp(argv[0], const_cast<char *const *>(argv));
      _exit(127);
    }
    _exit(0);
  }
  waitpid(pid, nullptr, 0);
}

static void spawn(const std::vector<std::string> &args) {
  std::vector<const char *> argv;
  for (auto &a : args) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  spawn(argv.data());
}

static bool have(const char *prog) {
  return run_wait({"sh", "-c", std::string("command -v ") + prog}) == 0;
}

// ---------------------------------------------------------------------------
// ELF i squashfs
// ---------------------------------------------------------------------------
// Koniec obrazu ELF = koniec tablicy nagłówków sekcji.
static bool elf_end(const std::string &path, uint64_t &end) {
  std::ifstream f(path, std::ios::binary);
  unsigned char id[EI_NIDENT];
  if (!f.read((char *)id, EI_NIDENT) || memcmp(id, ELFMAG, SELFMAG) != 0) return false;
  f.seekg(0);
  if (id[EI_CLASS] == ELFCLASS64) {
    Elf64_Ehdr h;
    if (!f.read((char *)&h, sizeof h)) return false;
    end = h.e_shoff + (uint64_t)h.e_shentsize * h.e_shnum;
  } else if (id[EI_CLASS] == ELFCLASS32) {
    Elf32_Ehdr h;
    if (!f.read((char *)&h, sizeof h)) return false;
    end = h.e_shoff + (uint64_t)h.e_shentsize * h.e_shnum;
  } else {
    return false;
  }
  return true;
}

// Prawdziwy superblok squashfs 4.x, a nie przypadkowe "hsqs" w danych.
static bool squashfs_super(const unsigned char *p) {
  if (memcmp(p, "hsqs", 4) != 0) return false;
  uint32_t block_size; uint16_t block_log, major;
  memcpy(&block_size, p + 12, 4);
  memcpy(&block_log, p + 22, 2);
  memcpy(&major, p + 28, 2);
  return major == 4 && block_log >= 12 && block_log <= 20 && block_size == (1u << block_log);
}

// Szuka obrazu za ELF-em. Klasyczny runtime AppImage kładzie go dokładnie na końcu
// ELF-a, ale nowsze (np. uruntime) potrafią dodać wyrównanie albo dane pośrodku.
// kind: "squashfs", "dwarfs" albo "" (nie znaleziono).
static bool find_payload(const std::string &path, uint64_t &offset, std::string &kind) {
  kind.clear();
  uint64_t start;
  if (!elf_end(path, start)) return false;
  std::ifstream f(path, std::ios::binary);
  const uint64_t LIMIT = 64ull << 20;          // szukamy w pierwszych 64 MB za ELF-em
  const size_t CHUNK = 1 << 20, OVER = 64;
  std::vector<unsigned char> buf(CHUNK + OVER);
  for (uint64_t pos = start; pos < start + LIMIT; pos += CHUNK) {
    f.clear();
    f.seekg((std::streamoff)pos);
    f.read((char *)buf.data(), (std::streamsize)buf.size());
    size_t got = (size_t)f.gcount();
    if (got < 32) break;
    for (size_t k = 0; k + 32 <= got && k < CHUNK; k++) {
      if (buf[k] == 'h' && squashfs_super(&buf[k])) { offset = pos + k; kind = "squashfs"; return true; }
      if (buf[k] == 'D' && k + 6 <= got && !memcmp(&buf[k], "DWARFS", 6)) {
        offset = pos + k; kind = "dwarfs"; return false;   // nie umiemy czytać bez uruchamiania
      }
    }
  }
  return false;
}

bool squashfs_offset(const std::string &path, uint64_t &offset) {
  std::string kind;
  return find_payload(path, offset, kind);
}

// Klucz pliku: zmienia się, gdy plik zostanie podmieniony na inną wersję.
static std::string key(const std::string &path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return "";
  std::string k = fs::path(path).filename().string() + "|" + std::to_string(st.st_size) + "|" +
                  std::to_string(st.st_mtime);
  char buf[20];
  snprintf(buf, sizeof buf, "%016zx", std::hash<std::string>{}(k));
  return buf;
}

static bool extract(const std::string &path, uint64_t off, const std::string &dest,
                    const std::vector<std::string> &only = {}) {
  std::vector<std::string> args = {"unsquashfs", "-q", "-n", "-f", "-o", std::to_string(off),
                                   "-d", dest};
  if (!only.empty()) args.push_back("-follow-symlinks");
  args.push_back(path);
  for (auto &o : only) args.push_back(o);
  return run_wait(args) == 0;
}

// ---------------------------------------------------------------------------
std::string icon(const std::string &path, const std::string &home) {
  std::string k = key(path);
  if (k.empty()) return "";
  fs::path cache = fs::path(home) / ".cache/lisaos/ikony";
  std::error_code ec;
  for (auto ext : {".png", ".svg"})
    if (fs::exists(cache / (k + ext), ec)) return (cache / (k + ext)).string();
  if (fs::exists(cache / (k + ".brak"), ec)) return "";   // już wiemy, że ikony nie ma

  fs::create_directories(cache, ec);
  auto store = [&](const std::string &data) -> std::string {
    const char *ext = nullptr;
    if (data.size() > 8 && !memcmp(data.data(), "\x89PNG", 4)) ext = ".png";
    else if (data.find("<svg") != std::string::npos) ext = ".svg";
    if (!ext) return "";
    std::ofstream(cache / (k + ext), std::ios::binary) << data;
    return (cache / (k + ext)).string();
  };
  // Aplikacja już zainstalowana (np. rozpakowana przez własny runtime) - ikona jest na dysku.
  fs::path installed_icon = fs::path(install_dir(path, home)) / ".DirIcon";
  if (fs::exists(installed_icon, ec)) {
    std::ifstream in(installed_icon, std::ios::binary);
    std::string r = store(std::string((std::istreambuf_iterator<char>(in)), {}));
    if (!r.empty()) return r;
  }
  uint64_t off;
  static bool tool = have("unsquashfs");
  // Bez squashfs (np. DwarFS) ikonę pokażemy po instalacji - bez znacznika "brak".
  if (!tool || !squashfs_offset(path, off)) return "";

  fs::path tmp = cache / ("tmp-" + k);
  fs::remove_all(tmp, ec);
  std::string result;
  if (extract(path, off, tmp.string(), {".DirIcon"})) {
    fs::path ic = tmp / ".DirIcon";
    std::ifstream in(ic, std::ios::binary);   // dowiązanie wskazuje na rozpakowany plik
    result = store(std::string((std::istreambuf_iterator<char>(in)), {}));
  }
  fs::remove_all(tmp, ec);
  if (result.empty()) std::ofstream(cache / (k + ".brak"));
  return result;
}

std::string display_name(const std::string &filename) {
  std::string s = filename;
  if (s.size() > 9 && strcasecmp(s.c_str() + s.size() - 9, ".appimage") == 0) s.resize(s.size() - 9);
  // architektura i numer wersji na końcu nazwy
  static const std::regex arch(R"([-_. ](x86[-_]64|amd64|aarch64|arm64|armhf|i[36]86)$)", std::regex::icase);
  static const std::regex ver(R"([-_ ]v?[0-9][0-9A-Za-z.\-_+~]*$)");
  s = std::regex_replace(s, arch, "");
  s = std::regex_replace(s, ver, "");
  for (char &c : s) if (c == '_') c = ' ';
  return s.empty() ? filename : s;
}

std::string install_dir(const std::string &path, const std::string &home) {
  std::string name = display_name(fs::path(path).filename().string());
  for (char &c : name) if (c == '/' || c == ' ') c = '-';
  return (fs::path(home) / ".local/share/lisaos/aplikacje" / (name + "-" + key(path).substr(0, 8))).string();
}

bool is_installed(const std::string &path, const std::string &home) {
  std::error_code ec;
  return fs::exists(fs::path(install_dir(path, home)) / "AppRun", ec);
}

// Awaryjnie: niech AppImage rozpakuje się sam (--appimage-extract). Robimy to
// w piaskownicy bez sieci, z zapisem tylko do katalogu tymczasowego.
static bool self_extract(const std::string &path, const fs::path &tmp) {
  std::error_code ec;
  fs::create_directories(tmp, ec);
  chmod(path.c_str(), 0755);
  int r;
  if (sandbox_available()) {
    r = run_wait({"bwrap", "--ro-bind", "/", "/", "--dev", "/dev", "--proc", "/proc",
                  "--tmpfs", "/tmp", "--bind", tmp.string(), tmp.string(),
                  "--ro-bind", path, path,           // sam plik AppImage, tylko do odczytu
                  "--unshare-all", "--new-session", "--die-with-parent",
                  "--setenv", "HOME", tmp.string(), "--chdir", tmp.string(),
                  path, "--appimage-extract"});
  } else {
    r = run_wait({"sh", "-c", "cd \"$1\" && exec \"$2\" --appimage-extract", "sh", tmp.string(), path});
  }
  return r == 0;
}

// Katalog z AppRun wewnątrz tego, co rozpakował runtime ("squashfs-root", "AppDir"...).
static fs::path find_appdir(const fs::path &tmp) {
  std::error_code ec;
  if (fs::exists(tmp / "AppRun", ec)) return tmp;
  for (auto &e : fs::directory_iterator(tmp, ec))
    if (e.is_directory(ec) && fs::exists(e.path() / "AppRun", ec)) return e.path();
  return {};
}

std::string install(const std::string &path, const std::string &home) {
  fs::path dir = install_dir(path, home), tmp = dir.string() + ".tmp";
  std::error_code ec;
  fs::create_directories(dir.parent_path(), ec);
  fs::remove_all(tmp, ec);

  uint64_t off;
  std::string kind;
  bool sqfs = find_payload(path, off, kind);
  fs::path appdir;
  if (sqfs && have("unsquashfs")) {
    // Droga podstawowa: rozpakowanie bez uruchamiania czegokolwiek z AppImage.
    if (extract(path, off, tmp.string())) appdir = find_appdir(tmp);
  }
  if (appdir.empty()) {
    fs::remove_all(tmp, ec);
    uint64_t e;
    if (!elf_end(path, e)) return "To nie jest plik AppImage (brak nagłówka ELF).";
    if (self_extract(path, tmp)) appdir = find_appdir(tmp);
  }
  if (appdir.empty()) {
    fs::remove_all(tmp, ec);
    if (kind == "dwarfs")
      return "Ten AppImage używa formatu DwarFS, a jego runtime\nnie potrafił się rozpakować.";
    return "Nie udało się rozpakować aplikacji.\nTo może nie być AppImage albo plik jest uszkodzony.";
  }
  if (appdir != tmp) {          // przenieś katalog aplikacji na miejsce tymczasowego
    fs::path moved = tmp.string() + "-appdir";
    fs::rename(appdir, moved, ec);
    fs::remove_all(tmp, ec);
    fs::rename(moved, tmp, ec);
  }
  // Starsze wersje tej samej aplikacji są już niepotrzebne.
  std::string prefix = dir.filename().string();
  prefix = prefix.substr(0, prefix.size() - 8);
  for (auto &e : fs::directory_iterator(dir.parent_path(), ec)) {
    std::string n = e.path().filename().string();
    if (n.rfind(prefix, 0) == 0 && n.size() == prefix.size() + 8 && e.path() != dir)
      fs::remove_all(e.path(), ec);
  }
  fs::remove_all(dir, ec);
  fs::rename(tmp, dir, ec);
  return ec ? "Nie udało się zapisać aplikacji: " + ec.message() : "";
}

bool sandbox_available() {
  static int ok = -1;
  if (ok < 0) ok = run_wait({"bwrap", "--ro-bind", "/", "/", "--dev", "/dev", "true"}) == 0;
  return ok;
}

void launch(const std::string &path, const std::string &home) {
  std::string dir = install_dir(path, home), apprun = dir + "/AppRun";
  if (!sandbox_available()) {
    spawn({"env", "APPDIR=" + dir, "APPIMAGE=" + path, "ARGV0=" + path, "HOME=" + home, apprun});
    return;
  }
  std::vector<std::string> a = {
      "bwrap",
      // cały system tylko do odczytu
      "--ro-bind", "/", "/",
      "--dev", "/dev",
      "--proc", "/proc",
      // własny, pusty /tmp; gniazdo X11 udostępniamy, żeby okna działały
      "--tmpfs", "/tmp",
      "--bind-try", "/tmp/.X11-unix", "/tmp/.X11-unix",
      // inni użytkownicy i nośniki są niewidoczne
      "--tmpfs", "/home", "--tmpfs", "/root", "--tmpfs", "/mnt", "--tmpfs", "/media",
      // LisaOS HD - jedyne miejsce z prawem zapisu
      "--bind", home, home,
      // sama aplikacja nie może się modyfikować
      "--ro-bind", dir, dir,
  };
  if (const char *xa = getenv("XAUTHORITY")) {
    a.insert(a.end(), {"--ro-bind-try", xa, xa});
  }
  if (const char *rt = getenv("XDG_RUNTIME_DIR")) {
    // dźwięk i szyna sesji, jeśli są
    for (auto s : {"/pulse", "/pipewire-0", "/bus"}) {
      std::string p = std::string(rt) + s;
      a.insert(a.end(), {"--bind-try", p, p});
    }
  }
  a.insert(a.end(), {"--unshare-pid", "--unshare-ipc", "--unshare-uts", "--unshare-cgroup-try",
                     "--new-session",
                     "--setenv", "APPDIR", dir, "--setenv", "APPIMAGE", path,
                     "--setenv", "ARGV0", path, "--setenv", "LISAOS_SANDBOX", "1",
                     "--setenv", "HOME", home,
                     "--chdir", home, apprun});
  spawn(a);
}

} // namespace appimage
