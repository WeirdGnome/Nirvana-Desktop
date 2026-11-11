// Nirvana Desktop - dźwięk: implementacja (pactl / amixer)
#include "audio.h"
#include "i18n.h"

#include <FL/Fl.H>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace audio {

static pid_t start(const std::vector<std::string> &args, int &out_fd, bool want_out = true) {
  int p[2];
  if (pipe2(p, O_CLOEXEC) != 0) return -1;
  pid_t pid = fork();
  if (pid < 0) { close(p[0]); close(p[1]); return -1; }
  if (pid == 0) {
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    int devnull = open("/dev/null", O_RDWR);
    dup2(want_out ? p[1] : devnull, 1);
    dup2(devnull, 2);
    dup2(devnull, 0);
    setenv("LC_ALL", "C", 1);
    std::vector<char *> av;
    for (auto &a : args) av.push_back((char *)a.c_str());
    av.push_back(nullptr);
    execvp(av[0], av.data());
    _exit(127);
  }
  close(p[1]);
  out_fd = p[0];
  return pid;
}

static int run(const std::vector<std::string> &args, std::string *out = nullptr) {
  int fd;
  pid_t pid = start(args, fd, out != nullptr);
  if (pid < 0) return -1;
  char buf[8192];
  ssize_t n;
  while ((n = read(fd, buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR))
    if (n > 0 && out) out->append(buf, n);
  close(fd);
  int st = 0;
  while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static bool have(const char *prog) {
  std::string out;
  return run({"sh", "-c", std::string("command -v ") + prog}, &out) == 0 && !out.empty();
}

static std::vector<std::string> split_lines(const std::string &s) {
  std::vector<std::string> v;
  size_t a = 0;
  while (a < s.size()) {
    size_t b = s.find('\n', a);
    if (b == std::string::npos) b = s.size();
    v.push_back(s.substr(a, b - a));
    a = b + 1;
  }
  return v;
}

// "front-left: 65536 / 100% / 0.00 dB,   front-right: 32768 /  50% / ..." -> średnia procentów
static int parse_volume(const std::string &v) {
  int sum = 0, n = 0;
  size_t p = 0;
  while ((p = v.find('%', p)) != std::string::npos) {
    size_t a = p;
    while (a > 0 && isdigit((unsigned char)v[a - 1])) a--;
    if (a < p) { sum += atoi(v.substr(a, p - a).c_str()); n++; }
    p++;
  }
  return n ? (sum + n / 2) / n : 0;
}

static std::string unquote(std::string v) {
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
  return v;
}

// Bloki "Sink #0", "Source #2", "Sink Input #5": pola z jednym tabem, właściwości z dwoma.
struct Block {
  int index = -1;
  std::vector<std::pair<std::string, std::string>> fields, props;
  std::vector<std::string> port_lines;
  std::string get(const char *k) const {
    for (auto &f : fields) if (f.first == k) return f.second;
    return "";
  }
  std::string prop(const char *k) const {
    for (auto &f : props) if (f.first == k) return f.second;
    return "";
  }
};

static std::vector<Block> parse_blocks(const std::string &out) {
  std::vector<Block> v;
  std::string section;
  for (auto &l : split_lines(out)) {
    if (l.empty()) continue;
    if (l[0] != '\t' && l[0] != ' ') {
      auto h = l.find('#');
      v.emplace_back();
      v.back().index = h == std::string::npos ? -1 : atoi(l.c_str() + h + 1);
      section.clear();
      continue;
    }
    if (v.empty()) continue;
    Block &b = v.back();
    if (l.rfind("\t\t", 0) == 0) {
      std::string t = l.substr(2);
      if (section == "Properties") {
        auto eq = t.find(" = ");
        if (eq != std::string::npos) b.props.emplace_back(t.substr(0, eq), unquote(t.substr(eq + 3)));
      } else if (section == "Ports") {
        b.port_lines.push_back(t);
      }
      continue;
    }
    if (l[0] == '\t') {
      std::string t = l.substr(1);
      auto c = t.find(':');
      if (c == std::string::npos) continue;
      std::string k = t.substr(0, c), val = c + 1 < t.size() ? t.substr(c + 1) : "";
      while (!val.empty() && val[0] == ' ') val.erase(0, 1);
      section = k;
      b.fields.emplace_back(k, val);
    }
  }
  return v;
}

static Device to_device(const Block &b) {
  Device d;
  d.index = b.index;
  d.name = b.get("Name");
  d.desc = b.get("Description");
  if (d.desc.empty()) d.desc = b.prop("device.description");
  if (d.desc.empty()) d.desc = d.name;
  d.mute = b.get("Mute") == "yes";
  d.volume = parse_volume(b.get("Volume"));
  d.active_port = b.get("Active Port");
  // "analog-output-headphones: Headphones (type: Headphones, priority: 9900, not available)"
  for (auto &pl : b.port_lines) {
    auto c = pl.find(": ");
    if (c == std::string::npos) continue;
    Port p;
    p.name = pl.substr(0, c);
    std::string rest = pl.substr(c + 2);
    auto par = rest.rfind(" (");
    p.desc = par == std::string::npos ? rest : rest.substr(0, par);
    p.available = rest.find("not available") == std::string::npos;
    d.ports.push_back(p);
  }
  return d;
}

const Device *State::sink() const {
  for (auto &d : sinks) if (d.name == def_sink) return &d;
  return sinks.empty() ? nullptr : &sinks[0];
}
const Device *State::source() const {
  for (auto &d : sources) if (d.name == def_source) return &d;
  return sources.empty() ? nullptr : &sources[0];
}

// --- ALSA (bez serwera dźwięku) ---------------------------------------------------
static bool alsa_control(const char *ctl, Device &d) {
  std::string out;
  if (run({"amixer", "-M", "get", ctl}, &out) != 0) return false;
  // "  Front Left: Playback 45 [69%] [-17.25dB] [on]"
  int sum = 0, n = 0;
  bool any_on = false, has_switch = false;
  for (auto &l : split_lines(out)) {
    auto a = l.find('[');
    if (a == std::string::npos) continue;
    auto pct = l.find("%]", a);
    if (pct != std::string::npos) {
      size_t s = pct;
      while (s > 0 && isdigit((unsigned char)l[s - 1])) s--;
      sum += atoi(l.substr(s, pct - s).c_str());
      n++;
    }
    if (l.find("[on]") != std::string::npos) { any_on = true; has_switch = true; }
    if (l.find("[off]") != std::string::npos) has_switch = true;
  }
  if (!n) return false;
  d.name = ctl;
  d.desc = !strcmp(ctl, "Master") ? _("Sound card (ALSA)") : _("Microphone (ALSA)");
  d.volume = (sum + n / 2) / n;
  d.mute = has_switch && !any_on;
  d.index = 0;
  return true;
}

State query() {
  State st;
  std::string out;
  if (have("pactl") && run({"pactl", "info"}, &out) == 0) {
    st.backend = State::Pulse;
    for (auto &l : split_lines(out)) {
      if (l.rfind("Default Sink: ", 0) == 0) st.def_sink = l.substr(14);
      if (l.rfind("Default Source: ", 0) == 0) st.def_source = l.substr(16);
    }
    out.clear();
    run({"pactl", "list", "sinks"}, &out);
    for (auto &b : parse_blocks(out)) st.sinks.push_back(to_device(b));
    out.clear();
    run({"pactl", "list", "sources"}, &out);
    for (auto &b : parse_blocks(out)) {
      std::string mon = b.get("Monitor of Sink");
      if (!mon.empty() && mon != "n/a") continue;   // monitor wyjścia to nie mikrofon
      st.sources.push_back(to_device(b));
    }
    out.clear();
    run({"pactl", "list", "sink-inputs"}, &out);
    for (auto &b : parse_blocks(out)) {
      Stream s;
      s.index = b.index;
      s.app = b.prop("application.name");
      s.media = b.prop("media.name");
      s.binary = b.prop("application.process.binary");
      if (s.app.empty()) s.app = s.binary.empty() ? s.media : s.binary;
      s.mute = b.get("Mute") == "yes";
      s.volume = parse_volume(b.get("Volume"));
      s.sink = atoi(b.get("Sink").c_str());
      // dźwięki zdarzeń (np. kliknięcia z pulseaudio) pomijamy
      if (b.prop("media.role") == "event" || b.prop("media.role") == "filter") continue;
      st.streams.push_back(s);
    }
    return st;
  }
  if (have("amixer")) {
    Device d;
    if (alsa_control("Master", d)) {
      st.backend = State::Alsa;
      st.sinks.push_back(d);
      st.def_sink = d.name;
      Device c;
      if (alsa_control("Capture", c)) { st.sources.push_back(c); st.def_source = c.name; }
      return st;
    }
  }
  st.problem = have("pactl") ? _("The sound server (PulseAudio/PipeWire) is not running.")
                             : _("Neither pactl (pulseaudio-utils) nor amixer (alsa-utils) is installed.");
  return st;
}

// --- operacje ------------------------------------------------------------------------
void set_volume(const State &s, bool output, const std::string &dev, int pct) {
  pct = std::clamp(pct, 0, 150);
  if (s.backend == State::Alsa) { run({"amixer", "-q", "-M", "set", dev, std::to_string(pct) + "%"}); return; }
  run({"pactl", output ? "set-sink-volume" : "set-source-volume", dev, std::to_string(pct) + "%"});
}
void set_mute(const State &s, bool output, const std::string &dev, bool mute) {
  if (s.backend == State::Alsa) { run({"amixer", "-q", "set", dev, mute ? "mute" : "unmute"}); return; }
  run({"pactl", output ? "set-sink-mute" : "set-source-mute", dev, mute ? "1" : "0"});
}
void set_default(bool output, const std::string &dev) {
  run({"pactl", output ? "set-default-sink" : "set-default-source", dev});
  if (!output) return;
  // PulseAudio przenosi tylko nowe strumienie - grające przenosimy sami
  State st = query();
  for (auto &s : st.streams) run({"pactl", "move-sink-input", std::to_string(s.index), dev});
}
void set_port(bool output, const std::string &dev, const std::string &port) {
  run({"pactl", output ? "set-sink-port" : "set-source-port", dev, port});
}
void set_stream_volume(int index, int pct) {
  run({"pactl", "set-sink-input-volume", std::to_string(index), std::to_string(std::clamp(pct, 0, 150)) + "%"});
}
void set_stream_mute(int index, bool mute) {
  run({"pactl", "set-sink-input-mute", std::to_string(index), mute ? "1" : "0"});
}
void move_stream(int index, const std::string &sink) {
  run({"pactl", "move-sink-input", std::to_string(index), sink});
}

void step_volume(int delta) {
  State st = query();
  const Device *d = st.sink();
  if (!d) return;
  if (d->mute && delta > 0) set_mute(st, true, d->name, false);
  set_volume(st, true, d->name, std::clamp(d->volume + delta, 0, 100));
}
void toggle_mute(bool output) {
  State st = query();
  const Device *d = output ? st.sink() : st.source();
  if (d) set_mute(st, output, d->name, !d->mute);
}

// --- obserwowanie ----------------------------------------------------------------------
static std::function<void()> g_changed;
static void fire(void *) { if (g_changed) g_changed(); }
static void poke() { Fl::remove_timeout(fire); Fl::add_timeout(0.15, fire); }
static void start_sub(void *);
static pid_t g_sub = -1;
static void sub_cb(int fd, void *) {
  char buf[2048];
  ssize_t n = read(fd, buf, sizeof buf);
  if (n > 0) {
    // "Event 'change' on sink #0" - reagujemy na wszystko poza klientami
    std::string s(buf, n);
    if (s.find("sink") != std::string::npos || s.find("source") != std::string::npos ||
        s.find("server") != std::string::npos || s.find("card") != std::string::npos)
      poke();
    return;
  }
  if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
  Fl::remove_fd(fd);
  close(fd);
  if (g_sub > 0) waitpid(g_sub, nullptr, WNOHANG);
  g_sub = -1;
  poke();
  Fl::add_timeout(3.0, start_sub);   // serwer dźwięku wstał na nowo albo jeszcze nie działa
}
static void start_sub(void *) {
  State st = query();
  if (st.backend != State::Pulse) {   // ALSA albo nic: sprawdzamy co 2 s
    poke();
    Fl::add_timeout(2.0, start_sub);
    return;
  }
  int fd;
  g_sub = start({"pactl", "subscribe"}, fd);
  if (g_sub < 0) return;
  Fl::add_fd(fd, FL_READ, sub_cb);
  poke();
}

void watch(std::function<void()> changed) {
  g_changed = std::move(changed);
  signal(SIGPIPE, SIG_IGN);
  start_sub(nullptr);
}

}  // namespace audio
