// Nirvana Desktop - denise: AppImage support
#include "appimage.h"
#include "i18n.h"

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
#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace appimage {

// ---------------------------------------------------------------------------
// Processes
// ---------------------------------------------------------------------------

// Run and wait; the program's output goes to /dev/null. Returns the exit code.
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

// Run and collect the standard output (stderr to /dev/null).
static std::string run_capture(const std::vector<std::string> &args) {
  int fds[2];
  if (pipe(fds) < 0) return "";
  pid_t pid = fork();
  if (pid < 0) { close(fds[0]); close(fds[1]); return ""; }
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], 1);
    int null = open("/dev/null", O_RDWR);
    if (null >= 0) dup2(null, 2);
    std::vector<char *> argv;
    for (auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  close(fds[1]);
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = read(fds[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
  close(fds[0]);
  waitpid(pid, nullptr, 0);
  return out;
}

// Double fork: the application becomes a child of init, so it leaves no zombie,
// and Rambler can safely wait for its own processes (unsquashfs).
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
// End of the ELF image = end of the section header table.
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

// A real squashfs 4.x superblock, not a random "hsqs" in the data.
static bool squashfs_super(const unsigned char *p) {
  if (memcmp(p, "hsqs", 4) != 0) return false;
  uint32_t block_size; uint16_t block_log, major;
  memcpy(&block_size, p + 12, 4);
  memcpy(&block_log, p + 22, 2);
  memcpy(&major, p + 28, 2);
  return major == 4 && block_log >= 12 && block_log <= 20 && block_size == (1u << block_log);
}

// Looks for the image after the ELF. The classic AppImage runtime puts it exactly at the end
// of the ELF, but newer ones (e.g. uruntime) can add padding or data in between.
// kind: "squashfs", "dwarfs" or "" (not found).
static bool find_payload(const std::string &path, uint64_t &offset, std::string &kind) {
  kind.clear();
  uint64_t start;
  if (!elf_end(path, start)) return false;
  std::ifstream f(path, std::ios::binary);
  const uint64_t LIMIT = 64ull << 20;          // search the first 64 MB after the ELF
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
        offset = pos + k; kind = "dwarfs"; return false;   // we cannot read it without running it
      }
    }
  }
  return false;
}

bool squashfs_offset(const std::string &path, uint64_t &offset) {
  std::string kind;
  return find_payload(path, offset, kind);
}

// File key: changes when the file is replaced with another version.
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
  fs::path cache = fs::path(home) / ".cache/nirvana/icons";
  std::error_code ec;
  for (auto ext : {".png", ".svg"})
    if (fs::exists(cache / (k + ext), ec)) return (cache / (k + ext)).string();
  if (fs::exists(cache / (k + ".none"), ec)) return "";   // we already know there is no icon

  fs::create_directories(cache, ec);
  auto store = [&](const std::string &data) -> std::string {
    const char *ext = nullptr;
    if (data.size() > 8 && !memcmp(data.data(), "\x89PNG", 4)) ext = ".png";
    else if (data.find("<svg") != std::string::npos) ext = ".svg";
    if (!ext) return "";
    std::ofstream(cache / (k + ext), std::ios::binary) << data;
    return (cache / (k + ext)).string();
  };
  // Application already installed (e.g. extracted by its own runtime) - the icon is on disk.
  fs::path installed_icon = fs::path(install_dir(path, home)) / ".DirIcon";
  if (fs::exists(installed_icon, ec)) {
    std::ifstream in(installed_icon, std::ios::binary);
    std::string r = store(std::string((std::istreambuf_iterator<char>(in)), {}));
    if (!r.empty()) return r;
  }
  uint64_t off;
  static bool tool = have("unsquashfs");
  // Without squashfs (e.g. DwarFS) the icon is shown after installation. We remember that in this
  // session so that every folder refresh does not search tens of MB of the file again.
  static std::set<std::string> no_payload;
  if (no_payload.count(k)) return "";
  if (!tool || !squashfs_offset(path, off)) { no_payload.insert(k); return ""; }

  fs::path tmp = cache / ("tmp-" + k);
  fs::remove_all(tmp, ec);
  std::string result;
  if (extract(path, off, tmp.string(), {".DirIcon"})) {
    fs::path ic = tmp / ".DirIcon";
    std::ifstream in(ic, std::ios::binary);   // the symlink points to an extracted file
    result = store(std::string((std::istreambuf_iterator<char>(in)), {}));
  }
  fs::remove_all(tmp, ec);
  if (result.empty()) std::ofstream(cache / (k + ".none"));
  return result;
}

// File name without the extension and architecture: "Firefox-128.0-x86_64.AppImage" -> "Firefox-128.0"
static std::string strip(const std::string &filename) {
  std::string s = filename;
  if (s.size() > 9 && strcasecmp(s.c_str() + s.size() - 9, ".appimage") == 0) s.resize(s.size() - 9);
  static const std::regex arch(R"([-_. ](x86[-_]64|x64|amd64|aarch64|arm64|armhf|i[36]86)$)", std::regex::icase);
  s = std::regex_replace(s, arch, "");
  static const std::regex tag(R"([-_ ]AppImage(?=[-_ ]|$))", std::regex::icase);   // "GIMP_AppImage-git"
  return std::regex_replace(s, tag, "");
}
static const std::regex &ver_re() {
  static const std::regex ver(R"([-_ ]v?([0-9][0-9A-Za-z.\-_+~]*)$)");
  return ver;
}

std::string app_name(const std::string &filename) {
  std::string s = std::regex_replace(strip(filename), ver_re(), "");
  for (char &c : s) if (c == '_') c = ' ';
  return s.empty() ? filename : s;
}

std::string version(const std::string &filename) {
  std::smatch m;
  std::string s = strip(filename);
  if (!std::regex_search(s, m, ver_re())) return "";
  // just the version number with an optional suffix: "2.10.25-20210610-withplugins" -> "2.10.25",
  // "24.8.2.1.basic" -> "24.8.2.1", "3.0-beta2" -> "3.0-beta2"
  static const std::regex num(R"(^[0-9]+(\.[0-9]+)*([-~.]?(alpha|beta|rc|pre|a|b)[0-9]*(?![A-Za-z]))?)", std::regex::icase);
  std::string tail = m[1].str();
  std::smatch n;
  return std::regex_search(tail, n, num) ? n[0].str() : tail;
}

std::string display_name(const std::string &filename) {
  std::string v = version(filename);
  return v.empty() ? app_name(filename) : app_name(filename) + " " + v;
}

// ---------------------------------------------------------------------------
// Metadata from the .desktop file in the AppImage: categories and version
// ---------------------------------------------------------------------------
static Meta parse_desktop(std::istream &in) {
  Meta m;
  std::string line;
  bool entry = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] == '[') { entry = line == "[Desktop Entry]"; continue; }
    if (!entry) continue;
    auto val = [&](const char *k) -> const char * {
      size_t n = strlen(k);
      return line.compare(0, n, k) == 0 ? line.c_str() + n : nullptr;
    };
    if (const char *v = val("Categories=")) m.categories = v;
    else if (const char *v = val("X-AppImage-Version=")) m.version = v;
  }
  return m;
}

static Meta read_meta_file(const fs::path &f) {
  std::ifstream in(f);
  Meta m;
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("version=", 0) == 0) m.version = line.substr(8);
    else if (line.rfind("categories=", 0) == 0) m.categories = line.substr(11);
  }
  return m;
}

Meta meta(const std::string &path, const std::string &home) {
  std::string k = key(path);
  if (k.empty()) return {};
  fs::path cache = fs::path(home) / ".cache/nirvana/icons";
  fs::path mf = cache / (k + ".meta");
  std::error_code ec;
  if (fs::exists(mf, ec)) return read_meta_file(mf);
  fs::create_directories(cache, ec);

  Meta m;
  bool found = false;
  auto from_dir = [&](const fs::path &dir) {
    for (auto &e : fs::directory_iterator(dir, ec))
      if (e.path().extension() == ".desktop") {
        std::ifstream in(e.path());   // the symlink points to a file in usr/share/applications
        m = parse_desktop(in);
        found = true;
        return;
      }
  };
  fs::path inst = install_dir(path, home);
  if (fs::exists(inst / "AppRun", ec)) from_dir(inst);
  static bool tool = have("unsquashfs");
  uint64_t off;
  if (!found && tool && squashfs_offset(path, off)) {
    // the name of the .desktop file in the image root, then extracting only that file
    std::string list = run_capture({"unsquashfs", "-o", std::to_string(off), "-l", "-d", "", path});
    std::string name;
    size_t pos = 0;
    while (pos < list.size()) {
      size_t nl = list.find('\n', pos);
      std::string l = list.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
      pos = nl == std::string::npos ? list.size() : nl + 1;
      if (!l.empty() && l[0] == '/') l.erase(0, 1);
      if (l.find('/') == std::string::npos && l.size() > 8 && l.compare(l.size() - 8, 8, ".desktop") == 0) {
        name = l;
        break;
      }
    }
    if (!name.empty()) {
      fs::path tmp = cache / ("tmp-meta-" + k);
      fs::remove_all(tmp, ec);
      if (extract(path, off, tmp.string(), {name})) from_dir(tmp);
      fs::remove_all(tmp, ec);
    }
  }
  // We also remember missing data (no search on every folder refresh),
  // but not before an application in a format other than squashfs is installed.
  if (found || (tool && squashfs_offset(path, off))) {
    std::ofstream out(mf);
    out << "version=" << m.version << "\ncategories=" << m.categories << "\n";
  }
  return m;
}

std::string label(const std::string &path, const std::string &home) {
  std::string file = fs::path(path).filename().string();
  if (!version(file).empty()) return display_name(file);
  std::string v = meta(path, home).version;
  return v.empty() ? app_name(file) : app_name(file) + " " + v;
}

// Folder name: the one that already exists (English, translated, or Polish
// from earlier versions when Polish was the default), otherwise translated.
static const std::pair<const char *, const char *> legacy[] = {
  {"Applications", "Aplikacje"}, {"Games", "Gry"}, {"Graphics", "Grafika"}, {"Office", "Biuro"},
  {"Development", "Programowanie"}, {"Education", "Edukacja"}, {"Science", "Nauka"},
  {"Accessories", "Akcesoria"}, {"Other", "Inne"},
};

static std::string folder_name(const std::string &parent, const char *en) {
  std::error_code ec;
  auto exists = [&](const std::string &n) { return fs::is_directory(fs::path(parent) / n, ec); };
  if (exists(en)) return en;
  if (exists(_(en))) return _(en);
  for (auto &[e, pl] : legacy)
    if (!strcmp(e, en) && exists(pl)) return pl;
  return _(en);
}

std::string apps_dir(const std::string &home) { return home + "/.nirvana-apps"; }

std::string migrate_apps_dir(const std::string &home) {
  std::error_code ec;
  fs::path dst = apps_dir(home);
  for (const char *n : {"Applications", "Aplikacje"}) {
    fs::path old = fs::path(home) / n;
    if (!fs::is_directory(old, ec) || fs::is_symlink(old, ec)) continue;
    if (!fs::exists(dst, ec)) {
      fs::rename(old, dst, ec);
      if (!ec) return old.string();
      ec.clear();
      continue;
    }
    // both exist: move the content, without overwriting
    for (auto &de : fs::directory_iterator(old, ec)) {
      fs::path to = dst / de.path().filename();
      if (fs::exists(to, ec)) {
        if (fs::is_directory(de.path(), ec) && fs::is_directory(to, ec))
          for (auto &x : fs::directory_iterator(de.path(), ec))
            if (!fs::exists(to / x.path().filename(), ec)) fs::rename(x.path(), to / x.path().filename(), ec);
        continue;
      }
      fs::rename(de.path(), to, ec);
    }
    fs::remove(old, ec);   // only when empty
    return old.string();
  }
  return "";
}

const std::vector<const char *> &categories() {
  static const std::vector<const char *> c = {
    N_("Games"), N_("Internet"), N_("Office"), N_("Graphics"), N_("Multimedia"), N_("Development"),
    N_("Education"), N_("Science"), N_("System"), N_("Accessories"), N_("Other"),
  };
  return c;
}

const char *detect_category(const std::string &path, const std::string &home) {
  // main freedesktop.org categories -> folders in the applications folder
  static const std::pair<const char *, const char *> map[] = {
    {"Game", N_("Games")},          {"Graphics", N_("Graphics")},   {"AudioVideo", N_("Multimedia")},
    {"Audio", N_("Multimedia")},  {"Video", N_("Multimedia")},   {"Network", N_("Internet")},
    {"Office", N_("Office")},      {"Development", N_("Development")}, {"Education", N_("Education")},
    {"Science", N_("Science")},     {"System", N_("System")},      {"Settings", N_("System")},
    {"Utility", N_("Accessories")},
  };
  std::string cats = meta(path, home).categories;
  size_t pos = 0;
  while (pos < cats.size()) {
    size_t sc = cats.find(';', pos);
    std::string c = cats.substr(pos, sc == std::string::npos ? std::string::npos : sc - pos);
    pos = sc == std::string::npos ? cats.size() : sc + 1;
    for (auto &[k, v] : map) if (c == k) return v;
  }
  return N_("Other");
}

std::string category_dir_name(const std::string &home, const char *category) {
  return folder_name(apps_dir(home), category);
}

int category_of_folder(const std::string &name) {
  const auto &cats = categories();
  for (size_t i = 0; i < cats.size(); i++) {
    const char *en = cats[i];
    if (strcasecmp(name.c_str(), en) == 0 || strcasecmp(name.c_str(), _(en)) == 0) return (int)i;
    for (auto &[e, pl] : legacy)
      if (!strcmp(e, en) && strcasecmp(name.c_str(), pl) == 0) return (int)i;
  }
  return -1;
}

std::string category_folder(const std::string &path, const std::string &home) {
  return category_dir_name(home, detect_category(path, home));
}

std::string install_dir(const std::string &path, const std::string &home) {
  std::string name = display_name(fs::path(path).filename().string());
  for (char &c : name) if (c == '/' || c == ' ') c = '-';
  return (fs::path(home) / ".local/share/nirvana/apps" / (name + "-" + key(path).substr(0, 8))).string();
}

bool is_installed(const std::string &path, const std::string &home) {
  std::error_code ec;
  return fs::exists(fs::path(install_dir(path, home)) / "AppRun", ec);
}

// Fallback: let the AppImage extract itself (--appimage-extract). We do that
// in a sandbox without network, writing only to a temporary folder.
static bool self_extract(const std::string &path, const fs::path &tmp) {
  std::error_code ec;
  fs::create_directories(tmp, ec);
  chmod(path.c_str(), 0755);
  int r;
  if (sandbox_available()) {
    r = run_wait({"bwrap", "--ro-bind", "/", "/", "--dev", "/dev", "--proc", "/proc",
                  "--tmpfs", "/tmp", "--bind", tmp.string(), tmp.string(),
                  "--ro-bind", path, path,           // the AppImage file itself, read-only
                  "--unshare-all", "--new-session", "--die-with-parent",
                  "--setenv", "HOME", tmp.string(), "--chdir", tmp.string(),
                  path, "--appimage-extract"});
  } else {
    r = run_wait({"sh", "-c", "cd \"$1\" && exec \"$2\" --appimage-extract", "sh", tmp.string(), path});
  }
  return r == 0;
}

// The folder with AppRun inside what the runtime extracted ("squashfs-root", "AppDir"...).
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
    // The main way: extraction without running anything from the AppImage.
    if (extract(path, off, tmp.string())) appdir = find_appdir(tmp);
  }
  if (appdir.empty()) {
    fs::remove_all(tmp, ec);
    uint64_t e;
    if (!elf_end(path, e)) return _("This is not an AppImage file (no ELF header).");
    if (self_extract(path, tmp)) appdir = find_appdir(tmp);
  }
  if (appdir.empty()) {
    fs::remove_all(tmp, ec);
    if (kind == "dwarfs")
      return _("This AppImage uses the DwarFS format and its runtime\ncould not extract it.");
    return _("Could not extract the application.\nIt may not be an AppImage, or the file is damaged.");
  }
  if (appdir != tmp) {          // move the application folder in place of the temporary one
    fs::path moved = tmp.string() + "-appdir";
    fs::rename(appdir, moved, ec);
    fs::remove_all(tmp, ec);
    fs::rename(moved, tmp, ec);
  }
  // Older versions of the same application are no longer needed.
  std::string prefix = dir.filename().string();
  prefix = prefix.substr(0, prefix.size() - 8);
  for (auto &e : fs::directory_iterator(dir.parent_path(), ec)) {
    std::string n = e.path().filename().string();
    if (n.rfind(prefix, 0) == 0 && n.size() == prefix.size() + 8 && e.path() != dir)
      fs::remove_all(e.path(), ec);
  }
  fs::remove_all(dir, ec);
  fs::rename(tmp, dir, ec);
  return ec ? plat::fmt(_("Could not save the application: %1"), ec.message()) : "";
}

bool sandbox_available() {
  static int ok = -1;
  if (ok < 0) ok = run_wait({"bwrap", "--ro-bind", "/", "/", "--dev", "/dev", "true"}) == 0;
  return ok;
}

void launch(const std::string &path, const std::string &home, const std::vector<std::string> &args) {
  std::string dir = install_dir(path, home), apprun = dir + "/AppRun";
  if (!sandbox_available()) {
    std::vector<std::string> a = {"env", "APPDIR=" + dir, "APPIMAGE=" + path, "ARGV0=" + path, "HOME=" + home, apprun};
    a.insert(a.end(), args.begin(), args.end());   // arguments from the Information window
    spawn(a);
    return;
  }
  std::vector<std::string> a = {
      "bwrap",
      // the whole system read-only
      "--ro-bind", "/", "/",
      "--dev", "/dev",
      "--proc", "/proc",
      // our own empty /tmp; the X11 socket is shared so that windows work
      "--tmpfs", "/tmp",
      "--bind-try", "/tmp/.X11-unix", "/tmp/.X11-unix",
      // other users and media are invisible
      "--tmpfs", "/home", "--tmpfs", "/root", "--tmpfs", "/mnt", "--tmpfs", "/media",
      // Nirvana HD - the only place with write access (plus the user's media below)
      "--bind", home, home,
      // the application cannot modify itself
      "--ro-bind", dir, dir,
  };
  // Mounted USB sticks, discs and disks of the user (UDisks2 mounts them in
  // /media/USER or /run/media/USER) - to open files from them.
  if (const char *user = getenv("USER")) {
    for (std::string base : {"/media/", "/run/media/"}) {
      std::string p = base + user;
      a.insert(a.end(), {"--bind-try", p, p});
    }
  }
  if (const char *xa = getenv("XAUTHORITY")) {
    a.insert(a.end(), {"--ro-bind-try", xa, xa});
  }
  if (const char *rt = getenv("XDG_RUNTIME_DIR")) {
    // sound and the session bus, if present
    for (auto s : {"/pulse", "/pipewire-0", "/bus"}) {
      std::string p = std::string(rt) + s;
      a.insert(a.end(), {"--bind-try", p, p});
    }
  }
  a.insert(a.end(), {"--unshare-pid", "--unshare-ipc", "--unshare-uts", "--unshare-cgroup-try",
                     "--new-session",
                     "--setenv", "APPDIR", dir, "--setenv", "APPIMAGE", path,
                     "--setenv", "ARGV0", path, "--setenv", "NIRVANA_SANDBOX", "1",
                     "--setenv", "HOME", home,
                     "--chdir", home, apprun});
  a.insert(a.end(), args.begin(), args.end());
  spawn(a);
}

} // namespace appimage
