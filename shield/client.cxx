// Nirvana Desktop - NirvanaSHIELD: the client side (status, list, programs)
#include "shield.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace shield {

std::string socket_path() {
  const char *e = getenv("NIRVANA_SHIELD_SOCKET");
  return e && *e ? e : "/run/nirvana-shield.sock";
}

static int connect_service() {
  int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (s < 0) return -1;
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  strncpy(a.sun_path, socket_path().c_str(), sizeof a.sun_path - 1);
  if (connect(s, (sockaddr *)&a, sizeof a) != 0) { close(s); return -1; }
  timeval tv{3, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  return s;
}

bool request(const std::string &line, std::string &reply) {
  reply.clear();
  int s = connect_service();
  if (s < 0) return false;
  std::string l = line + "\n";
  if (send(s, l.data(), l.size(), MSG_NOSIGNAL) != (ssize_t)l.size()) { close(s); return false; }
  char buf[4096];
  while (true) {
    ssize_t n = recv(s, buf, sizeof buf, 0);
    if (n <= 0) break;
    reply.append(buf, n);
    if (reply == ".\n" || (reply.size() >= 3 && reply.compare(reply.size() - 3, 3, "\n.\n") == 0)) break;
  }
  close(s);
  if (reply == ".\n") reply.clear();
  else if (reply.size() >= 3 && reply.compare(reply.size() - 3, 3, "\n.\n") == 0) reply.erase(reply.size() - 2);
  return true;
}

static std::vector<std::string> lines(const std::string &s) {
  std::vector<std::string> v;
  std::istringstream in(s);
  std::string l;
  while (std::getline(in, l)) if (!l.empty()) v.push_back(l);
  return v;
}

Status status() {
  Status st;
  std::string r;
  if (!request("STATUS", r)) return st;
  st.running = true;
  for (auto &l : lines(r)) {
    if (l.rfind("mode=", 0) == 0) st.mode = l.substr(5);
    else if (l.rfind("lock=", 0) == 0) st.lock = l.substr(5) == "1";
    else if (l.rfind("lan=", 0) == 0) st.lan = l.substr(4) != "0";
    else if (l.rfind("error=", 0) == 0) st.error = l.substr(6);
  }
  return st;
}

std::vector<std::string> allowed() {
  std::vector<std::string> v;
  std::string r;
  if (!request("LIST", r)) return v;
  for (auto &l : lines(r)) if (l.rfind("allow ", 0) == 0) v.push_back(l.substr(6));
  return v;
}

std::vector<Blocked> blocked() {
  std::vector<Blocked> v;
  std::string r;
  if (!request("BLOCKED", r)) return v;
  for (auto &l : lines(r)) {
    if (l.rfind("blocked ", 0) != 0) continue;
    std::istringstream s(l.substr(8));
    Blocked b;
    s >> b.when >> b.count;
    std::string rest;
    std::getline(s, rest);
    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    size_t t = rest.find('\t');
    b.path = rest.substr(0, t);
    if (t != std::string::npos) b.dest = rest.substr(t + 1);
    v.push_back(b);
  }
  return v;
}

static std::string one(const std::string &cmd) {
  std::string r;
  if (!request(cmd, r)) return "not running";
  while (!r.empty() && r.back() == '\n') r.pop_back();
  if (r == "ok") return "";
  return r.rfind("err ", 0) == 0 ? r.substr(4) : r;
}

std::string allow(const std::vector<std::string> &paths, bool on) {
  for (auto &p : paths) {
    std::string e = one(std::string(on ? "ALLOW " : "DENY ") + p);
    if (!e.empty()) return e;
  }
  return "";
}
std::string set_mode(const std::string &mode) { return one("MODE " + mode); }
std::string set_lan(bool on) { return one(std::string("LAN ") + (on ? "1" : "0")); }

std::string shield_exe() {
  char self[4096];
  ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
  if (n > 0) {
    self[n] = 0;
    std::string dir = fs::path(self).parent_path().string();
    for (const char *c : {"/nirvana-shield", "/../sbin/nirvana-shield"})
      if (access((dir + c).c_str(), X_OK) == 0) return dir + c;
  }
  for (const char *c : {"/usr/sbin/nirvana-shield", "/usr/local/sbin/nirvana-shield"})
    if (access(c, X_OK) == 0) return c;
  return "nirvana-shield";
}

// pkexec (through nirvana-pkexec, so the Nirvana password window is there) - one call
// carries all the commands, so the password is asked once
static int pkexec(const std::vector<std::string> &args, std::string &out) {
  std::vector<std::string> a;
  const char *wrap[] = {"/usr/libexec/nirvana-pkexec", "/usr/local/libexec/nirvana-pkexec"};
  std::string pk = "pkexec";
  for (auto *w : wrap) if (access(w, X_OK) == 0) { pk = w; break; }
  a.push_back(pk);
  for (auto &s : args) a.push_back(s);
  int p[2];
  if (pipe(p) != 0) return -1;
  pid_t pid = fork();
  if (pid == 0) {
    dup2(p[1], 1);
    dup2(p[1], 2);
    close(p[0]);
    std::vector<char *> v;
    for (auto &s : a) v.push_back((char *)s.c_str());
    v.push_back(nullptr);
    execvp(v[0], v.data());
    _exit(127);
  }
  close(p[1]);
  char buf[1024];
  ssize_t n;
  while ((n = read(p[0], buf, sizeof buf)) > 0) out.append(buf, n);
  close(p[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

std::string admin(const std::vector<std::string> &commands) {
  std::string uid = std::to_string(getuid());
  for (auto &c : commands) {
    std::string out;
    int rc = pkexec({shield_exe(), "ctl", "send", "AS " + uid + " " + c}, out);
    if (rc == 126 || rc == 127) return "canceled";
    if (rc != 0) return out.empty() ? "failed" : out;
  }
  return "";
}

std::string enable_service(bool on) {
  std::string out;
  int rc = pkexec({shield_exe(), "ctl", on ? "enable" : "disable"}, out);
  if (rc == 126 || rc == 127) return "canceled";
  return rc == 0 ? "" : (out.empty() ? "failed" : out);
}

int watch_fd() {
  int s = connect_service();
  if (s < 0) return -1;
  timeval tv{0, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  const char *w = "WATCH\n";
  send(s, w, strlen(w), MSG_NOSIGNAL);
  return s;
}

// ---------------------------------------------------------------------------
// The user's programs
// ---------------------------------------------------------------------------
static std::string lang_key(const std::map<std::string, std::string> &kv, const std::string &k) {
  const char *l = getenv("LANGUAGE");
  if (!l || !*l) l = getenv("LC_ALL");
  if (!l || !*l) l = getenv("LANG");
  std::string lang = l ? l : "";
  lang = lang.substr(0, lang.find_first_of(":.@"));
  for (std::string v : {lang, lang.substr(0, lang.find('_'))}) {
    if (v.empty()) continue;
    auto it = kv.find(k + "[" + v + "]");
    if (it != kv.end()) return it->second;
  }
  auto it = kv.find(k);
  return it == kv.end() ? "" : it->second;
}

static std::string which(const std::string &prog) {
  if (prog.find('/') != std::string::npos) return prog;
  const char *p = getenv("PATH");
  std::string path = p && *p ? p : "/usr/local/bin:/usr/bin:/bin";
  size_t a = 0;
  while (a <= path.size()) {
    size_t b = path.find(':', a);
    if (b == std::string::npos) b = path.size();
    std::string f = path.substr(a, b - a) + "/" + prog;
    a = b + 1;
    if (access(f.c_str(), X_OK) == 0) return f;
  }
  return "";
}

static bool is_elf(const std::string &f) {
  std::ifstream in(f, std::ios::binary);
  char m[4] = {0};
  in.read(m, 4);
  return m[0] == 0x7f && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
}

// The program files behind a command: its real file, and when it is a launcher script,
// the program files the script mentions (exec /usr/lib/firefox-esr/firefox-esr) or the
// usual /usr/lib/<name>/<name>.
static std::vector<std::string> files_of(const std::string &cmd) {
  std::vector<std::string> v;
  std::string f = which(cmd);
  if (f.empty()) return v;
  std::error_code ec;
  std::string real = fs::canonical(f, ec).string();
  if (ec) real = f;
  v.push_back(real);
  if (!is_elf(real)) {
    std::ifstream in(real);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    if (text.size() < 200000) {
      for (size_t i = 0; (i = text.find("/usr/", i)) != std::string::npos; i++) {
        size_t e = text.find_first_of(" \t\n\"'$;)|&", i);
        std::string p = text.substr(i, e == std::string::npos ? std::string::npos : e - i);
        if (p.size() > 6 && access(p.c_str(), X_OK) == 0 && fs::is_regular_file(p, ec) && is_elf(p)) {
          std::string r = fs::canonical(p, ec).string();
          if (!ec && std::find(v.begin(), v.end(), r) == v.end()) v.push_back(r);
        }
      }
    }
    std::string base = fs::path(real).filename().string();
    for (std::string c : {"/usr/lib/" + base + "/" + base, "/usr/lib64/" + base + "/" + base, "/opt/" + base + "/" + base}) {
      if (access(c.c_str(), X_OK) == 0 && is_elf(c)) {
        std::string r = fs::canonical(c, ec).string();
        if (!ec && std::find(v.begin(), v.end(), r) == v.end()) v.push_back(r);
      }
    }
  }
  return v;
}

static std::string exec_command(const std::string &exec) {
  // the first word that is not "env" or VAR=value; quotes removed
  std::istringstream s(exec);
  std::string w;
  while (s >> w) {
    if (!w.empty() && (w[0] == '"' || w[0] == '\'')) w = w.substr(1, w.size() - 2);
    if (w == "env" || w.find('=') != std::string::npos) continue;
    return w;
  }
  return "";
}

std::vector<Program> programs(const std::string &apps_dir) {
  std::vector<Program> out;
  std::set<std::string> seen_files;
  const char *h = getenv("HOME");
  const char *xd = getenv("XDG_DATA_HOME");
  std::vector<std::string> dirs = {(xd && *xd ? std::string(xd) : std::string(h ? h : "") + "/.local/share") + "/applications",
                                   "/usr/local/share/applications", "/usr/share/applications"};
  std::set<std::string> ids;
  for (auto &d : dirs) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(d, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (it->path().extension() != ".desktop") continue;
      std::string id = it->path().filename().string();
      if (!ids.insert(id).second) continue;   // the user's copy wins
      std::ifstream in(it->path());
      std::map<std::string, std::string> kv;
      std::string l;
      bool main = false;
      while (std::getline(in, l)) {
        if (!l.empty() && l[0] == '[') { main = l == "[Desktop Entry]"; continue; }
        if (!main) continue;
        size_t e = l.find('=');
        if (e != std::string::npos) kv[l.substr(0, e)] = l.substr(e + 1);
      }
      if (kv["Type"] != "Application" || kv["NoDisplay"] == "true" || kv["Hidden"] == "true") continue;
      std::string cmd = exec_command(kv["Exec"]);
      if (cmd.empty() || cmd == "flatpak" || cmd == "snap") continue;
      auto files = files_of(cmd);
      if (files.empty()) continue;
      // programs of the desktop itself do not need the network, nor do shells and settings tools
      std::string base = fs::path(files.front()).filename().string();
      if (base.rfind("nirvana-", 0) == 0) continue;
      if (seen_files.count(files.front())) continue;   // two menu entries for one program
      seen_files.insert(files.front());
      Program p;
      p.name = lang_key(kv, "Name");
      p.comment = lang_key(kv, "Comment");
      p.icon = kv["Icon"];
      p.paths = files;
      if (!p.name.empty()) out.push_back(p);
    }
  }
  // AppImages in the applications folder (also in the category folders)
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(apps_dir, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    std::string n = it->path().filename().string();
    std::string ln = n;
    std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
    if (ln.size() < 9 || ln.compare(ln.size() - 9, 9, ".appimage") != 0) continue;
    Program p;
    p.appimage = true;
    p.name = n.substr(0, n.size() - 9);
    for (char &c : p.name) if (c == '_' || c == '-') c = ' ';
    for (const char *arch : {" x86 64", " amd64", " i686", " aarch64"})
      if (size_t a = p.name.find(arch); a != std::string::npos) p.name.erase(a);
    p.paths = {fs::canonical(it->path(), ec).string()};
    if (ec) p.paths = {it->path().string()};
    p.icon = it->path().string();
    out.push_back(p);
  }
  std::sort(out.begin(), out.end(), [](const Program &a, const Program &b) {
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  return out;
}

std::string name_of(const std::string &path, const std::vector<Program> &progs, std::string *icon) {
  for (auto &p : progs)
    for (auto &f : p.paths)
      if (f == path) { if (icon) *icon = p.icon; return p.name; }
  return fs::path(path).filename().string();
}

}  // namespace shield
