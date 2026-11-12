// Nirvana Desktop - Bluetooth: implementation (bluetoothctl)
#include "bt.h"
#include "i18n.h"

#include <FL/Fl.H>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <poll.h>
#include <pty.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

namespace bt {

static const char *ctl() {
  const char *e = getenv("NIRVANA_BLUETOOTHCTL");   // for testing: a fake bluetoothctl
  return e && *e ? e : "bluetoothctl";
}

bool installed() {
  std::string n = ctl();
  if (n.find('/') != std::string::npos) return access(n.c_str(), X_OK) == 0;
  const char *path = getenv("PATH");
  std::string p = path ? path : "/usr/bin:/bin";
  size_t a = 0;
  while (a <= p.size()) {
    size_t b = p.find(':', a);
    if (b == std::string::npos) b = p.size();
    if (access((p.substr(a, b - a) + "/" + n).c_str(), X_OK) == 0) return true;
    a = b + 1;
  }
  return false;
}

int State::connected() const {
  int n = 0;
  for (auto &d : devices) n += d.connected;
  return n;
}

// ---------------------------------------------------------------------------
// Running bluetoothctl
// ---------------------------------------------------------------------------
// keep_stdin: the write end of stdin stays with us (the monitor: bluetoothctl ends at EOF)
static pid_t start(const std::vector<std::string> &args, int &out_fd, int *in_fd = nullptr) {
  int p[2], q[2] = {-1, -1};
  if (pipe2(p, O_CLOEXEC) != 0) return -1;
  if (in_fd && pipe2(q, O_CLOEXEC) != 0) { close(p[0]); close(p[1]); return -1; }
  pid_t pid = fork();
  if (pid < 0) { close(p[0]); close(p[1]); if (in_fd) { close(q[0]); close(q[1]); } return -1; }
  if (pid == 0) {
    prctl(PR_SET_PDEATHSIG, SIGTERM);   // the monitor does not outlive Rambler
    dup2(p[1], 1);
    dup2(p[1], 2);
    if (in_fd) dup2(q[0], 0);
    else {
      int devnull = open("/dev/null", O_RDONLY);
      if (devnull >= 0) dup2(devnull, 0);
    }
    setenv("LC_ALL", "C", 1);
    std::vector<char *> argv;
    argv.push_back((char *)ctl());
    for (auto &a : args) argv.push_back((char *)a.c_str());
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  close(p[1]);
  if (in_fd) { close(q[0]); *in_fd = q[1]; }
  out_fd = p[0];
  return pid;
}

// Synchronous, with a time limit: without bluetoothd, bluetoothctl waits forever
// ("Waiting to connect to bluetoothd...").
static int run(const std::vector<std::string> &args, std::string &out, int limit_ms = 3000) {
  int fd;
  pid_t pid = start(args, fd);
  if (pid < 0) return -1;
  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  bool killed = false;
  for (;;) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    int left = limit_ms - (int)((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000);
    if (left <= 0) { kill(pid, SIGTERM); killed = true; break; }
    struct pollfd pf = {fd, POLLIN, 0};
    int r = poll(&pf, 1, left);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) continue;
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof buf);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    out.append(buf, (size_t)n);
  }
  close(fd);
  int st = 0;
  while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
  if (killed) return -2;
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// "\tKey: value" lines -> map
static std::map<std::string, std::string> fields(const std::string &out) {
  std::map<std::string, std::string> m;
  size_t a = 0;
  while (a < out.size()) {
    size_t b = out.find('\n', a);
    if (b == std::string::npos) b = out.size();
    std::string l = out.substr(a, b - a);
    a = b + 1;
    if (l.empty() || (l[0] != '\t' && l[0] != ' ')) continue;
    l = trim(l);
    size_t c = l.find(": ");
    if (c == std::string::npos) continue;
    std::string k = l.substr(0, c);
    if (!m.count(k)) m[k] = l.substr(c + 2);   // the first one (e.g. UUID lines repeat)
  }
  return m;
}

static bool yes(const std::map<std::string, std::string> &m, const char *k) {
  auto it = m.find(k);
  return it != m.end() && it->second.rfind("yes", 0) == 0;
}

std::string error_text(const std::string &out) {
  auto has = [&](const char *s) { return out.find(s) != std::string::npos; };
  if (has("Error.Blocked") || has("rfkill")) return _("Bluetooth is blocked (radio switch or airplane mode).");
  if (has("Error.NotReady")) return _("The Bluetooth adapter is not ready - turn it on first.");
  if (has("Error.AuthenticationFailed") || has("Error.AuthenticationRejected"))
    return _("Pairing was rejected - the codes did not match or the device refused.");
  if (has("Error.AuthenticationCanceled")) return _("Pairing was canceled.");
  if (has("Error.AuthenticationTimeout") || has("Timeout")) return _("The device did not answer in time.");
  if (has("Error.ConnectionAttemptFailed") || has("Page Timeout") || has("Host is down"))
    return _("The device does not answer - is it turned on and nearby?");
  if (has("profile-unavailable")) return _("The device offers no service this computer can use (for sound: is PipeWire or PulseAudio with Bluetooth support installed?).");
  if (has("Error.AlreadyExists")) return _("The device is already paired.");
  if (has("Error.InProgress")) return _("Another operation with this device is in progress.");
  if (has("not available")) return _("The device is no longer in range.");
  if (has("No default controller")) return _("No Bluetooth adapter found.");
  if (has("Waiting to connect to bluetoothd")) return _("The Bluetooth service (bluetoothd) is not running.");
  // the last non-empty line as it is
  std::string t = trim(out);
  size_t n = t.rfind('\n');
  return n == std::string::npos ? t : t.substr(n + 1);
}

std::string kind_name(const std::string &i) {
  if (i == "audio-headset") return _("Headset");
  if (i == "audio-headphones") return _("Headphones");
  if (i == "audio-card") return _("Speaker");
  if (i == "input-keyboard") return _("Keyboard");
  if (i == "input-mouse") return _("Mouse");
  if (i == "input-tablet") return _("Tablet");
  if (i == "input-gaming") return _("Game Controller");
  if (i == "phone") return _("Phone");
  if (i == "computer") return _("Computer");
  if (i.rfind("camera", 0) == 0) return _("Camera");
  if (i == "printer") return _("Printer");
  if (i == "multimedia-player") return _("Media Player");
  if (i == "video-display") return _("Display");
  return _("Device");
}

State query() {
  State st;
  if (!installed()) { st.problem = _("bluetoothctl is not installed (bluez package)."); return st; }
  std::string out;
  int code = run({"show"}, out);
  if (code == -2 || out.find("Waiting to connect") != std::string::npos) {
    st.problem = _("The Bluetooth service (bluetoothd) is not running.");
    return st;
  }
  if (code == 127 || out.find("No default controller") != std::string::npos || out.find("Controller") == std::string::npos) {
    st.problem = _("No Bluetooth adapter found.");
    return st;
  }
  auto f = fields(out);
  st.available = true;
  st.adapter = f.count("Alias") ? f["Alias"] : f["Name"];
  st.powered = yes(f, "Powered");
  st.discoverable = yes(f, "Discoverable");
  st.discovering = yes(f, "Discovering");
  st.pairable = yes(f, "Pairable");
  // the devices BlueZ knows (paired and recently found)
  out.clear();
  run({"devices"}, out);
  std::vector<std::string> macs;
  size_t a = 0;
  while (a < out.size()) {
    size_t b = out.find('\n', a);
    if (b == std::string::npos) b = out.size();
    std::string l = trim(out.substr(a, b - a));
    a = b + 1;
    if (l.rfind("Device ", 0) == 0 && l.size() >= 24) macs.push_back(l.substr(7, 17));
  }
  for (auto &m : macs) {
    std::string info;
    run({"info", m}, info);
    auto g = fields(info);
    Device d;
    d.mac = m;
    d.name = g.count("Alias") ? g["Alias"] : g.count("Name") ? g["Name"] : m;
    d.icon = g["Icon"];
    d.paired = yes(g, "Paired") || yes(g, "Bonded");
    d.trusted = yes(g, "Trusted");
    d.connected = yes(g, "Connected");
    d.blocked = yes(g, "Blocked");
    if (g.count("RSSI")) d.rssi = atoi(g["RSSI"].c_str());
    if (g.count("Battery Percentage")) {   // "0x46 (70)"
      std::string v = g["Battery Percentage"];
      size_t p = v.find('(');
      d.battery = p != std::string::npos ? atoi(v.c_str() + p + 1) : (int)strtol(v.c_str(), nullptr, 0);
    }
    st.devices.push_back(d);
  }
  std::sort(st.devices.begin(), st.devices.end(), [](const Device &x, const Device &y) {
    if (x.paired != y.paired) return x.paired;
    return x.name < y.name;
  });
  return st;
}

// ---------------------------------------------------------------------------
// Background actions
// ---------------------------------------------------------------------------
struct Job {
  int fd;
  pid_t pid;
  std::string out;
  std::function<bool(const std::string &)> ok;
  Done done;
};

static void job_cb(int fd, void *d) {
  auto *j = (Job *)d;
  char buf[2048];
  ssize_t n = read(fd, buf, sizeof buf);
  if (n > 0) { j->out.append(buf, (size_t)n); return; }
  if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
  Fl::remove_fd(fd);
  close(fd);
  int st = 0;
  waitpid(j->pid, &st, 0);
  bool ok = j->ok(j->out);
  if (j->done) j->done(ok, ok ? std::string() : error_text(j->out));
  delete j;
}

static void job(const std::vector<std::string> &args, std::function<bool(const std::string &)> ok, Done done) {
  int fd;
  pid_t pid = start(args, fd);
  if (pid < 0) { if (done) done(false, strerror(errno)); return; }
  Fl::add_fd(fd, FL_READ, job_cb, new Job{fd, pid, {}, std::move(ok), std::move(done)});
}

static auto contains(const char *s) {
  return [s](const std::string &o) { return o.find(s) != std::string::npos; };
}

void set_power(bool on, Done done) {
  auto ok = contains("succeeded");
  job({"power", on ? "on" : "off"}, ok, [on, ok, done](bool good, const std::string &msg) {
    if (good || !on || msg != error_text("Error.Blocked")) { if (done) done(good, msg); return; }
    // a soft block (rfkill): lift it and try once more
    pid_t rp = fork();
    if (rp == 0) {
      int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); }
      execlp("rfkill", "rfkill", "unblock", "bluetooth", (char *)nullptr);
      _exit(127);
    }
    if (rp > 0) waitpid(rp, nullptr, 0);
    usleep(300000);
    job({"power", "on"}, ok, done);
  });
}

void set_discoverable(bool on, Done done) {
  job({"discoverable", on ? "on" : "off"}, contains("succeeded"), std::move(done));
}

void connect(const std::string &mac, Done done) {
  job({"--timeout", "20", "connect", mac}, contains("Connection successful"), std::move(done));
}

void disconnect(const std::string &mac, Done done) {
  job({"disconnect", mac}, [](const std::string &o) {
    return o.find("Successful disconnected") != std::string::npos || o.find("Disconnection successful") != std::string::npos;
  }, std::move(done));
}

void remove(const std::string &mac, Done done) {
  job({"remove", mac}, contains("has been removed"), std::move(done));
}

// ---------------------------------------------------------------------------
// Watching: a bluetoothctl with an open stdin prints a line for every change
// ---------------------------------------------------------------------------
static std::function<void()> g_changed;
static pid_t g_mon_pid = -1;
static int g_mon_in = -1;
static void fire(void *) { if (g_changed) g_changed(); }
static void poke() { Fl::remove_timeout(fire); Fl::add_timeout(0.4, fire); }
static void start_monitor(void *);
static void mon_cb(int fd, void *) {
  char buf[1024];
  ssize_t n = read(fd, buf, sizeof buf);
  if (n > 0) {
    std::string s(buf, (size_t)n);
    if (s.find("[CHG]") != std::string::npos || s.find("[NEW]") != std::string::npos ||
        s.find("[DEL]") != std::string::npos)
      poke();
    return;
  }
  if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
  Fl::remove_fd(fd);
  close(fd);
  if (g_mon_in >= 0) { close(g_mon_in); g_mon_in = -1; }
  if (g_mon_pid > 0) waitpid(g_mon_pid, nullptr, WNOHANG);
  g_mon_pid = -1;
  poke();
  Fl::add_timeout(10.0, start_monitor);   // bluetoothd restarted - try again later
}
static void start_monitor(void *) {
  int fd;
  g_mon_pid = start({}, fd, &g_mon_in);
  if (g_mon_pid < 0) return;
  Fl::add_fd(fd, FL_READ, mon_cb);
}

void watch(std::function<void()> changed) {
  g_changed = std::move(changed);
  signal(SIGPIPE, SIG_IGN);
  start_monitor(nullptr);
}

// ---------------------------------------------------------------------------
// Session: interactive bluetoothctl on a pseudo terminal
// ---------------------------------------------------------------------------
Session::Session() {
  signal(SIGPIPE, SIG_IGN);
  struct winsize ws = {24, 200, 0, 0};   // wide: no wrapped lines
  pid_t pid = forkpty(&fd_, nullptr, nullptr, &ws);
  if (pid < 0) { fd_ = -1; return; }
  if (pid == 0) {
    setenv("LC_ALL", "C", 1);
    setenv("TERM", "dumb", 1);
    execlp(ctl(), ctl(), (char *)nullptr);
    _exit(127);
  }
  pid_ = pid;
  fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
  Fl::add_fd(fd_, FL_READ, read_cb, this);
  send("agent KeyboardDisplay");
  send("default-agent");
}

Session::~Session() {
  Fl::remove_timeout(timeout_cb, this);
  if (fd_ >= 0) {
    send("scan off");
    send("quit");
    Fl::remove_fd(fd_);
    close(fd_);
  }
  if (pid_ > 0) {
    kill(pid_, SIGTERM);
    waitpid(pid_, nullptr, 0);
  }
}

void Session::send(const std::string &l) {
  if (fd_ < 0) return;
  std::string s = l + "\n";
  ssize_t r = write(fd_, s.data(), s.size());
  (void)r;
}

void Session::scan(bool on) { send(on ? "scan on" : "scan off"); }

void Session::pair(const std::string &mac) {
  pairing_ = mac;
  stage_ = 1;
  send("pair " + mac);
  Fl::remove_timeout(timeout_cb, this);
  Fl::add_timeout(60, timeout_cb, this);
}

void Session::answer(const std::string &a) { send(a); }

void Session::cancel_pairing() {
  if (!stage_) return;
  send("cancel-pairing " + pairing_);
  finish(false, _("Pairing was canceled."));
}

void Session::timeout_cb(void *d) {
  auto *s = (Session *)d;
  if (s->stage_ == 3) { s->finish(true, _("Paired, but the device did not connect yet - turn it on and connect from the menu.")); return; }
  if (s->stage_) { s->send("cancel-pairing " + s->pairing_); s->finish(false, _("The device did not answer in time.")); }
}

void Session::finish(bool ok, const std::string &msg) {
  stage_ = 0;
  Fl::remove_timeout(timeout_cb, this);
  if (on_paired) on_paired(ok, msg);
}

void Session::read_cb(int fd, void *d) {
  auto *s = (Session *)d;
  char buf[2048];
  ssize_t n = read(fd, buf, sizeof buf);
  if (n > 0) { s->feed(std::string(buf, (size_t)n)); return; }
  if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
  Fl::remove_fd(fd);   // EIO: bluetoothctl is gone
  close(fd);
  s->fd_ = -1;
  if (s->pid_ > 0) { waitpid(s->pid_, nullptr, 0); s->pid_ = -1; }
  if (s->stage_) s->finish(false, _("The Bluetooth service (bluetoothd) is not running."));
}

// Strip the terminal codes (colors, line clearing, readline markers); \r becomes \n.
static std::string clean(const std::string &in) {
  std::string o;
  for (size_t i = 0; i < in.size(); i++) {
    char c = in[i];
    if (c == '\x1b') {
      if (i + 1 < in.size() && in[i + 1] == '[') {
        i += 2;
        while (i < in.size() && !isalpha((unsigned char)in[i])) i++;
      }
      continue;
    }
    if (c == '\x01' || c == '\x02') continue;
    o += c == '\r' ? '\n' : c;
  }
  return o;
}

void Session::feed(const std::string &text) {
  buf_ += clean(text);
  size_t nl;
  while ((nl = buf_.find('\n')) != std::string::npos) {
    std::string l = trim(buf_.substr(0, nl));
    buf_.erase(0, nl + 1);
    if (!l.empty()) line(l);
  }
  // a question without a new line: the agent waits for the answer
  std::string p = trim(buf_);
  auto ask = [&](Ask a, const std::string &t) { buf_.clear(); if (on_ask) on_ask(a, t); };
  if (p.find("[agent]") == std::string::npos) return;
  if (p.find("Confirm passkey") != std::string::npos && p.find("(yes/no)") != std::string::npos) {
    size_t k = p.find("passkey ") + 8;
    ask(Ask::Confirm, p.substr(k, p.find(' ', k) - k));
  } else if (p.find("Enter PIN code") != std::string::npos) {
    ask(Ask::Pin, "");
  } else if (p.find("Enter passkey") != std::string::npos) {
    ask(Ask::Passkey, "");
  } else if (p.find("(yes/no)") != std::string::npos) {   // Authorize service / Accept pairing
    ask(Ask::Authorize, "");
  }
}

void Session::line(const std::string &l) {
  auto has = [&](const char *s) { return l.find(s) != std::string::npos; };
  if (has("[NEW]") || has("[CHG]") || has("[DEL]")) { if (on_change) on_change(); }
  if (has("[agent] Passkey: ") || has("[agent] PIN code: ")) {   // type it on the device
    std::string code = trim(l.substr(l.find(": ", l.find("[agent]")) + 2));
    if (on_ask) on_ask(Ask::Show, code);
  }
  if (!stage_) return;
  if (stage_ == 1) {
    if (has("Pairing successful")) {
      stage_ = 2;
      send("trust " + pairing_);
    } else if (has("Failed to pair")) {
      finish(false, error_text(l));
    }
  } else if (stage_ == 2) {
    if (has("trust succeeded") || has("Failed to set trust")) {
      stage_ = 3;
      send("connect " + pairing_);
      Fl::remove_timeout(timeout_cb, this);
      Fl::add_timeout(25, timeout_cb, this);
    }
  } else if (stage_ == 3) {
    if (has("Connection successful")) finish(true, "");
    else if (has("Failed to connect")) {
      // paired anyway: some devices (phones, computers) offer nothing to connect to
      finish(true, error_text(l));
    }
  }
}

}  // namespace bt
