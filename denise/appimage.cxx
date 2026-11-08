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
bool squashfs_offset(const std::string &path, uint64_t &offset) {
  std::ifstream f(path, std::ios::binary);
  unsigned char id[EI_NIDENT];
  if (!f.read((char *)id, EI_NIDENT) || memcmp(id, ELFMAG, SELFMAG) != 0) return false;
  f.seekg(0);
  // Koniec ELF-a = koniec tablicy nagłówków sekcji; tam zaczyna się squashfs.
  if (id[EI_CLASS] == ELFCLASS64) {
    Elf64_Ehdr h;
    if (!f.read((char *)&h, sizeof h)) return false;
    offset = h.e_shoff + (uint64_t)h.e_shentsize * h.e_shnum;
  } else if (id[EI_CLASS] == ELFCLASS32) {
    Elf32_Ehdr h;
    if (!f.read((char *)&h, sizeof h)) return false;
    offset = h.e_shoff + (uint64_t)h.e_shentsize * h.e_shnum;
  } else {
    return false;
  }
  char magic[4];
  f.seekg((std::streamoff)offset);
  return f.read(magic, 4) && memcmp(magic, "hsqs", 4) == 0;
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
  uint64_t off;
  static bool tool = have("unsquashfs");
  if (!tool || !squashfs_offset(path, off)) return "";

  fs::path tmp = cache / ("tmp-" + k);
  fs::remove_all(tmp, ec);
  std::string result;
  if (extract(path, off, tmp.string(), {".DirIcon"})) {
    fs::path ic = tmp / ".DirIcon";
    std::ifstream in(ic, std::ios::binary);   // dowiązanie wskazuje na rozpakowany plik
    std::string data((std::istreambuf_iterator<char>(in)), {});
    const char *ext = nullptr;
    if (data.size() > 8 && !memcmp(data.data(), "\x89PNG", 4)) ext = ".png";
    else if (data.find("<svg") != std::string::npos) ext = ".svg";
    if (ext) {
      std::ofstream(cache / (k + ext), std::ios::binary) << data;
      result = (cache / (k + ext)).string();
    }
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

std::string install(const std::string &path, const std::string &home) {
  if (!have("unsquashfs")) return "Brak programu unsquashfs (pakiet squashfs-tools).";
  uint64_t off;
  if (!squashfs_offset(path, off)) return "To nie jest obsługiwany AppImage (typ 2).";
  fs::path dir = install_dir(path, home), tmp = dir.string() + ".tmp";
  std::error_code ec;
  fs::create_directories(dir.parent_path(), ec);
  fs::remove_all(tmp, ec);
  if (!extract(path, off, tmp.string())) {
    fs::remove_all(tmp, ec);
    return "Nie udało się rozpakować aplikacji.";
  }
  if (!fs::exists(tmp / "AppRun", ec)) {
    fs::remove_all(tmp, ec);
    return "W obrazie brakuje pliku AppRun.";
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
