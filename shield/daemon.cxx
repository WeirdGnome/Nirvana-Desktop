// Nirvana Desktop - NirvanaSHIELD: the firewall service (runs as root)
//
// The idea: no program of a user gets to the network unless the user allowed it.
//
//  * nftables (table inet nirvana_shield): outgoing packets of real users (uid 1000-59999)
//    pass only from the "allowed" group of processes - a net_cls cgroup with the class id
//    0x4E530001 ("NS"). System services (root and the other system accounts: updates,
//    DHCP, time, printing) are never blocked. Incoming connections are dropped, except
//    replies, ICMP, DHCP and (optionally) the local network.
//  * The process watcher (the kernel's proc connector) sees every program start and puts
//    it into the allowed group when its program file - or its AppImage, or the script an
//    interpreter runs - is on the user's list. Children stay where their parent is.
//    A full rescan every 10 s and after each change puts things right (also taking
//    programs out when the user takes the permission back).
//  * Blocked attempts: a rate-limited "log" rule writes the packet to the kernel log; we
//    read /dev/kmsg, find the socket by its port and the process by the socket, and
//    tell the user's session (WATCH) - Rambler then asks "May X use the internet?".
//  * Name lookups (DNS, UDP port 53) of programs that are not allowed: a dropped UDP packet
//    fails at once (EPERM) and the resolver closes its socket before we could see whose
//    it was - the program never even gets to connecting, and nobody is asked. So these
//    queries are redirected to us (127.0.0.1 / ::1, DNS_TRAP_PORT): the socket still waits
//    for the answer, we find the program, ask about it (with the name it looked up) and
//    answer "refused" - the program fails at once, as before.
//
// Modes: protect (the list decides), block (no user program gets out), off (no rules).
// Settings: /etc/nirvana-shield.conf. Control: the socket /run/nirvana-shield.sock -
// every user changes their own list; with "lock" (parental lock) only root can (the
// panel asks for the administrator password through pkexec: nirvana-shield ctl as UID ...).
#include "shield.h"

#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <map>
#include <poll.h>
#include <set>
#include <sstream>
#include <string>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>
#include <vector>

namespace {

using namespace shield;

std::string conf_path() {
  const char *e = getenv("NIRVANA_SHIELD_CONF");
  return e && *e ? e : "/etc/nirvana-shield.conf";
}

struct Config {
  std::string mode = "protect";   // protect | block | off
  bool lock = false;              // parental lock: only root changes anything
  bool lan = true;                // the local network is always reachable (printers, sharing)
  std::map<unsigned, std::set<std::string>> allow;
};
Config g;
std::string g_error;              // why the service cannot protect (shown in the panel)
std::string g_cg;                 // the net_cls hierarchy
volatile sig_atomic_t g_stop = 0;

void log(const std::string &m) { syslog(LOG_INFO, "%s", m.c_str()); fprintf(stderr, "nirvana-shield: %s\n", m.c_str()); }

bool user_uid(unsigned u) { return u >= 1000 && u < 60000; }

void load() {
  std::ifstream in(conf_path());
  std::string l;
  Config c;
  bool any = false;
  while (std::getline(in, l)) {
    any = true;
    if (l.empty() || l[0] == '#') continue;
    if (l.rfind("mode=", 0) == 0) c.mode = l.substr(5);
    else if (l.rfind("lock=", 0) == 0) c.lock = l.substr(5) == "1";
    else if (l.rfind("lan=", 0) == 0) c.lan = l.substr(4) != "0";
    else if (l.rfind("allow ", 0) == 0) {
      std::istringstream s(l.substr(6));
      unsigned uid;
      std::string path;
      if (s >> uid) {
        std::getline(s, path);
        if (!path.empty() && path[0] == ' ') path.erase(0, 1);
        if (!path.empty() && path[0] == '/') c.allow[uid].insert(path);
      }
    }
  }
  if (c.mode != "protect" && c.mode != "block" && c.mode != "off") c.mode = "protect";
  (void)any;
  g = c;
}

void save() {
  std::string f = conf_path(), tmp = f + ".new";
  {
    std::ofstream out(tmp);
    out << "# NirvanaSHIELD - which programs may use the network (Preferences > NirvanaSHIELD)\n"
        << "mode=" << g.mode << "\nlock=" << g.lock << "\nlan=" << g.lan << "\n";
    for (auto &[uid, set] : g.allow)
      for (auto &p : set) out << "allow " << uid << " " << p << "\n";
  }
  chmod(tmp.c_str(), 0644);
  rename(tmp.c_str(), f.c_str());
}

// ---------------------------------------------------------------------------
// nftables
// ---------------------------------------------------------------------------
int run(const std::vector<std::string> &args, std::string *out = nullptr) {
  int p[2];
  if (pipe2(p, O_CLOEXEC) != 0) return -1;
  pid_t pid = fork();
  if (pid == 0) {
    dup2(p[1], 1);
    dup2(p[1], 2);
    std::vector<char *> a;
    for (auto &s : args) a.push_back((char *)s.c_str());
    a.push_back(nullptr);
    execvp(a[0], a.data());
    _exit(127);
  }
  close(p[1]);
  char buf[1024];
  ssize_t n;
  while ((n = read(p[0], buf, sizeof buf)) > 0) if (out) out->append(buf, n);
  close(p[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

const char *PRIVATE4 = "{ 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16, 169.254.0.0/16, 224.0.0.0/4, 255.255.255.255 }";
const char *PRIVATE6 = "{ fe80::/10, fc00::/7, ff00::/8 }";

bool apply_rules() {
  std::ostringstream r;
  r << "table inet nirvana_shield\ndelete table inet nirvana_shield\n";
  if (g.mode != "off") {
    char cls[16];
    snprintf(cls, sizeof cls, "0x%08x", CLASSID);
    r << "table inet nirvana_shield {\n"
      << "  chain out {\n"
      << "    type filter hook output priority filter; policy accept;\n"
      << "    oifname \"lo\" accept\n"
      << "    meta skuid != 1000-59999 accept\n"   // system services always get out
      // the name lookups redirected to the DNS trap (still marked for the old interface here)
      << "    ip daddr 127.0.0.1 udp dport " << DNS_TRAP_PORT << " accept\n"
      << "    ip6 daddr ::1 udp dport " << DNS_TRAP_PORT << " accept\n";
    if (g.lan) r << "    ip daddr " << PRIVATE4 << " accept\n    ip6 daddr " << PRIVATE6 << " accept\n";
    if (g.mode == "protect") {
      r << "    meta cgroup " << cls << " accept\n"
        // a blocked attempt: into the kernel log (we read it), then dropped - the socket
        // waits, so we can still find out which program it was
        << "    limit rate 5/second burst 10 packets log prefix \"nirvana-shield: \" flags skuid\n"
        << "    drop\n";
    } else {
      // everything blocked: a quick refusal, programs say "no connection" at once
      r << "    meta l4proto tcp reject with tcp reset\n    reject with icmpx admin-prohibited\n";
    }
    r << "  }\n";
    if (g.mode == "protect") {   // the name lookups of not allowed programs come to us (see the top)
      r << "  chain dns {\n"
        << "    type nat hook output priority -100; policy accept;\n"
        << "    oifname \"lo\" return\n"
        << "    meta skuid != 1000-59999 return\n"
        << "    meta cgroup " << cls << " return\n";
      if (g.lan) r << "    ip daddr " << PRIVATE4 << " return\n    ip6 daddr " << PRIVATE6 << " return\n";
      r << "    udp dport 53 redirect to :" << DNS_TRAP_PORT << "\n"
        << "  }\n";
    }
    r << "  chain in {\n"
      << "    type filter hook input priority filter; policy accept;\n"
      << "    iifname \"lo\" accept\n"
      << "    ct state established,related accept\n"
      << "    ct state invalid drop\n"
      << "    meta l4proto { icmp, ipv6-icmp } accept\n"
      << "    udp dport { 68, 546 } accept\n";
    if (g.lan) r << "    ip saddr " << PRIVATE4 << " accept\n    ip6 saddr " << PRIVATE6 << " accept\n";
    r << "    drop\n  }\n}\n";
  }
  std::string f = std::string(RUN_DIR) + "/rules.nft";
  { std::ofstream o(f); o << r.str(); }
  std::string out;
  int rc = run({"nft", "-f", f}, &out);
  if (rc != 0) {
    g_error = "nft: " + out;
    log("cannot load the rules: " + out);
    return false;
  }
  return true;
}

void remove_rules() {
  run({"nft", "delete", "table", "inet", "nirvana_shield"});
}

// ---------------------------------------------------------------------------
// The allowed group (net_cls cgroup)
// ---------------------------------------------------------------------------
bool write_file(const std::string &f, const std::string &v) {
  int fd = open(f.c_str(), O_WRONLY | O_CLOEXEC);
  if (fd < 0) return false;
  bool ok = write(fd, v.data(), v.size()) == (ssize_t)v.size();
  close(fd);
  return ok;
}

bool setup_cgroup() {
  // an existing net_cls hierarchy (e.g. "net_cls,net_prio") or our own
  std::ifstream m("/proc/mounts");
  std::string dev, dir, type, opts, rest;
  while (m >> dev >> dir >> type >> opts) {
    std::getline(m, rest);
    if (type != "cgroup") continue;
    std::string o = "," + opts + ",";
    if (o.find(",net_cls,") != std::string::npos) { g_cg = dir; break; }
  }
  if (g_cg.empty()) {
    g_cg = std::string(RUN_DIR) + "/net_cls";
    mkdir(g_cg.c_str(), 0755);
    if (mount("nirvana-shield", g_cg.c_str(), "cgroup", 0, "net_cls") != 0) {
      g_error = std::string("net_cls: ") + strerror(errno);
      log("cannot mount the net_cls cgroup: " + std::string(strerror(errno)));
      g_cg.clear();
      return false;
    }
  }
  std::string d = g_cg + "/nirvana-shield";
  mkdir(d.c_str(), 0755);
  char cls[16];
  snprintf(cls, sizeof cls, "%u", CLASSID);
  if (!write_file(d + "/net_cls.classid", cls)) {
    g_error = "net_cls.classid: " + std::string(strerror(errno));
    return false;
  }
  return true;
}

// the net_cls path of a process ("/nirvana-shield" = allowed)
bool in_allowed(int pid) {
  std::ifstream f("/proc/" + std::to_string(pid) + "/cgroup");
  std::string l;
  while (std::getline(f, l)) {
    size_t a = l.find(':'), b = l.find(':', a + 1);
    if (a == std::string::npos || b == std::string::npos) continue;
    std::string ctl = "," + l.substr(a + 1, b - a - 1) + ",";
    if (ctl.find(",net_cls,") != std::string::npos) return l.substr(b + 1) == "/nirvana-shield";
  }
  return false;
}

void move(int pid, bool allow) {
  if (g_cg.empty()) return;
  write_file(allow ? g_cg + "/nirvana-shield/cgroup.procs" : g_cg + "/cgroup.procs", std::to_string(pid));
}

// ---------------------------------------------------------------------------
// Processes
// ---------------------------------------------------------------------------
struct ProcInfo { int ppid = 0; unsigned uid = 0; bool ok = false; };

ProcInfo proc_info(int pid) {
  ProcInfo p;
  std::ifstream f("/proc/" + std::to_string(pid) + "/status");
  std::string l;
  while (std::getline(f, l)) {
    if (l.rfind("PPid:", 0) == 0) p.ppid = atoi(l.c_str() + 5);
    else if (l.rfind("Uid:", 0) == 0) { p.uid = (unsigned)strtoul(l.c_str() + 4, nullptr, 10); p.ok = true; }
  }
  return p;
}

std::string link(const std::string &p) {
  char b[4096];
  ssize_t n = readlink(p.c_str(), b, sizeof b - 1);
  if (n <= 0) return "";
  b[n] = 0;
  std::string s = b;
  const std::string del = " (deleted)";
  if (s.size() > del.size() && s.compare(s.size() - del.size(), del.size(), del) == 0) s.erase(s.size() - del.size());
  return s;
}

std::vector<std::string> read_nul(const std::string &f) {
  std::ifstream in(f, std::ios::binary);
  std::vector<std::string> v;
  std::string s;
  while (std::getline(in, s, '\0')) v.push_back(s);
  return v;
}

bool interpreter(const std::string &exe) {
  std::string b = exe.substr(exe.rfind('/') + 1);
  static const char *names[] = {"sh", "dash", "bash", "perl", "ruby", "node", "nodejs", "lua", "tclsh", "wish"};
  for (auto *n : names) if (b == n) return true;
  return b.rfind("python", 0) == 0;
}

// The names a process can be allowed by: its program file, its AppImage, the script it runs.
// The first one is the "main" one, shown to the user.
std::vector<std::string> identities(int pid) {
  std::vector<std::string> ids;
  std::string base = "/proc/" + std::to_string(pid);
  std::string exe = link(base + "/exe");
  if (exe.empty()) return ids;
  if (exe.find("/.mount_") != std::string::npos || exe.rfind("/tmp/", 0) == 0) {
    for (auto &e : read_nul(base + "/environ"))
      if (e.rfind("APPIMAGE=", 0) == 0 && e.size() > 9 && e[9] == '/') ids.push_back(e.substr(9));
  }
  if (interpreter(exe)) {
    auto args = read_nul(base + "/cmdline");
    for (size_t i = 1; i < args.size(); i++) {
      const std::string &a = args[i];
      if (a.empty()) continue;
      if (a == "-c" || a == "-m" || a == "-e") break;   // inline code, a module: no script file
      if (a[0] == '-') continue;
      std::string p = a[0] == '/' ? a : link(base + "/cwd") + "/" + a;
      char real[4096];
      if (realpath(p.c_str(), real)) ids.push_back(real);
      break;
    }
  }
  char real[4096];
  ids.push_back(realpath(exe.c_str(), real) ? std::string(real) : exe);
  return ids;
}

bool matches(int pid, unsigned uid) {
  auto it = g.allow.find(uid);
  if (it == g.allow.end() || it->second.empty()) return false;
  for (auto &id : identities(pid)) if (it->second.count(id)) return true;
  return false;
}

std::vector<int> all_pids() {
  std::vector<int> v;
  DIR *d = opendir("/proc");
  if (!d) return v;
  while (dirent *e = readdir(d))
    if (e->d_name[0] >= '1' && e->d_name[0] <= '9') v.push_back(atoi(e->d_name));
  closedir(d);
  return v;
}

// Every user process in the allowed group exactly when it, or a user process above it,
// is allowed.
void rescan() {
  if (g_cg.empty()) return;
  std::map<int, ProcInfo> procs;
  for (int pid : all_pids()) {
    ProcInfo p = proc_info(pid);
    if (p.ok && user_uid(p.uid)) procs[pid] = p;
  }
  std::map<int, int> memo;   // -1 unknown, 0 no, 1 yes
  std::function<bool(int, int)> want = [&](int pid, int depth) -> bool {
    auto m = memo.find(pid);
    if (m != memo.end()) return m->second == 1;
    auto it = procs.find(pid);
    if (it == procs.end() || depth > 64) return false;
    bool w = matches(pid, it->second.uid) || want(it->second.ppid, depth + 1);
    memo[pid] = w;
    return w;
  };
  for (auto &[pid, p] : procs) {
    bool w = want(pid, 0), in = in_allowed(pid);
    if (w != in) move(pid, w);
  }
}

void on_exec(int pid) {
  ProcInfo p = proc_info(pid);
  if (!p.ok || !user_uid(p.uid)) return;
  if (!in_allowed(pid) && matches(pid, p.uid)) move(pid, true);
}

int open_proc_connector() {
  int s = socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_CONNECTOR);
  if (s < 0) return -1;
  sockaddr_nl sa{};
  sa.nl_family = AF_NETLINK;
  sa.nl_groups = CN_IDX_PROC;
  sa.nl_pid = getpid();
  if (bind(s, (sockaddr *)&sa, sizeof sa) != 0) { close(s); return -1; }
  // nlmsghdr + cn_msg + the operation, built in a plain buffer
  char buf[NLMSG_SPACE(sizeof(cn_msg) + sizeof(enum proc_cn_mcast_op))] = {0};
  auto *h = (nlmsghdr *)buf;
  h->nlmsg_len = NLMSG_LENGTH(sizeof(cn_msg) + sizeof(enum proc_cn_mcast_op));
  h->nlmsg_type = NLMSG_DONE;
  h->nlmsg_pid = getpid();
  auto *m = (cn_msg *)NLMSG_DATA(h);
  m->id.idx = CN_IDX_PROC;
  m->id.val = CN_VAL_PROC;
  m->len = sizeof(enum proc_cn_mcast_op);
  enum proc_cn_mcast_op op = PROC_CN_MCAST_LISTEN;
  memcpy(m->data, &op, sizeof op);
  if (send(s, buf, h->nlmsg_len, 0) < 0) { close(s); return -1; }
  return s;
}

void read_proc_connector(int s) {
  char buf[4096] __attribute__((aligned(NLMSG_ALIGNTO)));
  for (;;) {
    ssize_t n = recv(s, buf, sizeof buf, MSG_DONTWAIT);
    if (n <= 0) return;
    for (nlmsghdr *h = (nlmsghdr *)buf; NLMSG_OK(h, (unsigned)n); h = NLMSG_NEXT(h, n)) {
      auto *cn = (cn_msg *)NLMSG_DATA(h);
      auto *ev = (proc_event *)cn->data;
      if (ev->what == PROC_EVENT_EXEC) on_exec(ev->event_data.exec.process_tgid);
    }
  }
}

// ---------------------------------------------------------------------------
// Blocked attempts (from the kernel log)
// ---------------------------------------------------------------------------
struct Attempt { std::string path, dest; time_t when = 0; int count = 0; };
std::map<unsigned, std::vector<Attempt>> g_blocked;   // per user, newest first

std::string field(const std::string &l, const char *key) {
  std::string k = std::string(" ") + key + "=";
  size_t a = l.find(k);
  if (a == std::string::npos) return "";
  a += k.size();
  size_t b = l.find(' ', a);
  return l.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

// uid: the socket's owner; with any_uid the first user socket on the port (its owner into uid)
unsigned long socket_inode(const std::string &proto, unsigned port, unsigned &uid, bool any_uid = false) {
  for (const char *v : {"", "6"}) {
    std::ifstream f("/proc/net/" + proto + v);
    std::string l;
    std::getline(f, l);
    while (std::getline(f, l)) {
      std::istringstream s(l);
      std::string sl, local, remote, st, q, tr, retr, u, tmo, inode;
      if (!(s >> sl >> local >> remote >> st >> q >> tr >> retr >> u >> tmo >> inode)) continue;
      size_t c = local.rfind(':');
      if (c == std::string::npos) continue;
      if (strtoul(local.c_str() + c + 1, nullptr, 16) != port) continue;
      unsigned owner = (unsigned)strtoul(u.c_str(), nullptr, 10);
      if (any_uid ? (owner < 1000 || owner > 59999) : owner != uid) continue;
      uid = owner;
      return strtoul(inode.c_str(), nullptr, 10);
    }
  }
  return 0;
}

int pid_of_socket(unsigned long inode, unsigned uid) {
  std::string want = "socket:[" + std::to_string(inode) + "]";
  for (int pid : all_pids()) {
    ProcInfo p = proc_info(pid);
    if (!p.ok || p.uid != uid) continue;
    std::string fd = "/proc/" + std::to_string(pid) + "/fd";
    DIR *d = opendir(fd.c_str());
    if (!d) continue;
    bool hit = false;
    while (dirent *e = readdir(d)) {
      if (e->d_name[0] == '.') continue;
      if (link(fd + "/" + e->d_name) == want) { hit = true; break; }
    }
    closedir(d);
    if (hit) return pid;
  }
  return 0;
}

struct Client { int fd; unsigned uid; bool watch = false; std::string in; };
std::vector<Client> g_clients;

void send_str(int fd, const std::string &s) {
  size_t off = 0;
  while (off < s.size()) {
    ssize_t n = send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n <= 0) return;
    off += (size_t)n;
  }
}

// A program of the user tried to get out: remember it and tell the user's session
void report(int pid, unsigned uid, const std::string &dest) {
  auto ids = identities(pid);
  if (ids.empty()) return;
  if (g.mode == "protect" && matches(pid, uid)) {   // allowed meanwhile, just not moved yet
    move(pid, true);
    return;
  }
  std::string path = ids.front();
  auto &list = g_blocked[uid];
  time_t now = time(nullptr);
  for (auto it = list.begin(); it != list.end(); ++it) {
    if (it->path != path) continue;
    Attempt a = *it;
    a.count++;
    a.dest = dest;
    bool tell = now - a.when > 30;   // the same program: tell again after half a minute
    a.when = now;
    list.erase(it);
    list.insert(list.begin(), a);
    if (!tell) return;
    goto notify;
  }
  list.insert(list.begin(), Attempt{path, dest, now, 1});
  if (list.size() > 40) list.resize(40);
notify:
  for (auto &c : g_clients)
    if (c.watch && (c.uid == uid || c.uid == 0)) send_str(c.fd, "blocked " + path + "\t" + dest + "\n");
}

void blocked(const std::string &line) {
  std::string proto = field(line, "PROTO"), spt = field(line, "SPT"), uid_s = field(line, "UID");
  std::string dst = field(line, "DST"), dpt = field(line, "DPT");
  if (uid_s.empty() || spt.empty()) return;
  unsigned uid = (unsigned)strtoul(uid_s.c_str(), nullptr, 10);
  std::string pr = proto == "TCP" ? "tcp" : proto == "UDP" ? "udp" : "";
  if (pr.empty()) return;
  unsigned long ino = socket_inode(pr, (unsigned)atoi(spt.c_str()), uid);
  int pid = ino ? pid_of_socket(ino, uid) : 0;
  if (!pid) return;
  report(pid, uid, dst + (dpt.empty() ? "" : ":" + dpt));
}

// ---------------------------------------------------------------------------
// The DNS trap: the name lookups of not allowed programs (see the top)
// ---------------------------------------------------------------------------
int open_trap(int family) {
  int s = socket(family, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (s < 0) return -1;
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  int rc;
  if (family == AF_INET) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(DNS_TRAP_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    rc = bind(s, (sockaddr *)&a, sizeof a);
  } else {
    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
    sockaddr_in6 a{};
    a.sin6_family = AF_INET6;
    a.sin6_port = htons(DNS_TRAP_PORT);
    a.sin6_addr = in6addr_loopback;
    rc = bind(s, (sockaddr *)&a, sizeof a);
  }
  if (rc != 0) { close(s); return -1; }
  return s;
}

// the name asked about in a DNS query ("" if it cannot be read)
std::string dns_name(const unsigned char *d, size_t n) {
  std::string name;
  size_t p = 12;
  while (p < n && d[p]) {
    unsigned len = d[p];
    if (len > 63 || p + 1 + len > n) return "";
    if (!name.empty()) name += '.';
    for (unsigned i = 0; i < len; i++) {
      char c = (char)d[p + 1 + i];
      name += (c >= 33 && c < 127) ? c : '?';
    }
    p += 1 + len;
  }
  return name;
}

void read_trap(int fd) {
  for (;;) {
    unsigned char buf[1500];
    sockaddr_storage from{};
    socklen_t fl = sizeof from;
    ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (sockaddr *)&from, &fl);
    if (n < 0) return;
    if (n < 12) continue;
    unsigned port = from.ss_family == AF_INET ? ntohs(((sockaddr_in *)&from)->sin_port)
                                              : ntohs(((sockaddr_in6 *)&from)->sin6_port);
    // first find the program (its socket waits for the answer), then answer
    unsigned uid = 0;
    unsigned long ino = socket_inode("udp", port, uid, true);
    int pid = ino ? pid_of_socket(ino, uid) : 0;
    if (pid) {
      std::string name = dns_name(buf, (size_t)n);
      report(pid, uid, name.empty() ? "DNS" : name);
    }
    // the answer: the same question, "refused" - the resolver gives up at once
    buf[2] = (unsigned char)(0x80 | (buf[2] & 0x79));   // QR = 1, the opcode and RD kept
    buf[3] = 0x05;                                      // RCODE = REFUSED
    sendto(fd, buf, (size_t)n, 0, (sockaddr *)&from, fl);
  }
}

void read_kmsg(int fd) {
  char buf[8192];
  for (;;) {
    ssize_t n = read(fd, buf, sizeof buf - 1);
    if (n < 0 && errno == EPIPE) continue;   // overwritten records - go on
    if (n <= 0) return;
    buf[n] = 0;
    std::string l = buf;
    size_t semi = l.find(';');
    if (semi == std::string::npos) continue;
    l = l.substr(semi + 1);
    if (l.rfind("nirvana-shield: ", 0) != 0) continue;
    blocked(" " + l);
  }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
std::string unescape(const std::string &s) { return s; }

std::string command(Client &c, std::string line, unsigned as_uid, bool root) {
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  std::string cmd = line.substr(0, line.find(' '));
  std::string arg = line.size() > cmd.size() ? line.substr(cmd.size() + 1) : "";
  auto locked = [&] { return g.lock && !root; };
  if (cmd == "AS") {
    if (!root) return "err root only\n.\n";
    std::istringstream s(arg);
    unsigned uid;
    if (!(s >> uid)) return "err uid\n.\n";
    std::string rest;
    std::getline(s, rest);
    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    return command(c, rest, uid, true);
  }
  if (cmd == "STATUS") {
    std::ostringstream o;
    o << "mode=" << g.mode << "\nlock=" << g.lock << "\nlan=" << g.lan << "\nerror=" << g_error << "\n.\n";
    return o.str();
  }
  if (cmd == "LIST") {
    std::string o;
    for (auto &p : g.allow[as_uid]) o += "allow " + p + "\n";
    return o + ".\n";
  }
  if (cmd == "BLOCKED") {
    std::string o;
    for (auto &a : g_blocked[as_uid])
      o += "blocked " + std::to_string((long)a.when) + " " + std::to_string(a.count) + " " + a.path + "\t" + a.dest + "\n";
    return o + ".\n";
  }
  if (cmd == "WATCH") { c.watch = true; return "ok\n.\n"; }
  if (cmd == "ALLOW" || cmd == "DENY") {
    if (locked()) return "err locked\n.\n";
    if (arg.empty() || arg[0] != '/') return "err path\n.\n";
    if (cmd == "ALLOW") g.allow[as_uid].insert(arg);
    else g.allow[as_uid].erase(arg);
    if (cmd == "ALLOW") {   // forget its blocked attempts
      auto &l = g_blocked[as_uid];
      l.erase(std::remove_if(l.begin(), l.end(), [&](const Attempt &a) { return a.path == arg; }), l.end());
    }
    save();
    rescan();
    return "ok\n.\n";
  }
  if (cmd == "MODE") {
    if (locked()) return "err locked\n.\n";
    if (arg != "protect" && arg != "block" && arg != "off") return "err mode\n.\n";
    g.mode = arg;
    save();
    apply_rules();
    return "ok\n.\n";
  }
  if (cmd == "LAN") {
    if (locked()) return "err locked\n.\n";
    g.lan = arg == "1";
    save();
    apply_rules();
    return "ok\n.\n";
  }
  if (cmd == "LOCK") {
    if (!root) return "err root only\n.\n";
    g.lock = arg == "1";
    save();
    return "ok\n.\n";
  }
  return "err unknown\n.\n";
}

int open_server() {
  std::string path = socket_path();
  unlink(path.c_str());
  int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  strncpy(a.sun_path, path.c_str(), sizeof a.sun_path - 1);
  if (bind(s, (sockaddr *)&a, sizeof a) != 0 || listen(s, 16) != 0) { close(s); return -1; }
  chmod(path.c_str(), 0666);   // everyone may talk; each one changes only their own list
  return s;
}

int daemon_main() {
  openlog("nirvana-shield", LOG_PID, LOG_DAEMON);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, [](int) { g_stop = 1; });
  signal(SIGINT, [](int) { g_stop = 1; });
  signal(SIGHUP, [](int) {});
  mkdir(RUN_DIR, 0755);
  load();
  bool cg = setup_cgroup();
  if (!cg) g.mode = g.mode == "off" ? "off" : "block";   // without the groups we cannot tell programs apart
  run({"modprobe", "-q", "nf_log_syslog"});
  if (!apply_rules()) log("running without rules");
  rescan();
  int pc = open_proc_connector();
  if (pc < 0) log("no proc connector - the list is checked every 2 s");
  int km = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (km >= 0) lseek(km, 0, SEEK_END);
  int srv = open_server();
  if (srv < 0) { log("cannot open the control socket"); return 1; }
  int trap4 = open_trap(AF_INET), trap6 = open_trap(AF_INET6);
  if (trap4 < 0) log("cannot open the DNS trap - blocked name lookups are not asked about");
  log("running, mode " + g.mode);
  time_t last_scan = time(nullptr);
  while (!g_stop) {
    std::vector<pollfd> fds = {{srv, POLLIN, 0}};
    if (pc >= 0) fds.push_back({pc, POLLIN, 0});
    if (km >= 0) fds.push_back({km, POLLIN, 0});
    if (trap4 >= 0) fds.push_back({trap4, POLLIN, 0});
    if (trap6 >= 0) fds.push_back({trap6, POLLIN, 0});
    size_t first_client = fds.size();
    for (auto &c : g_clients) fds.push_back({c.fd, POLLIN, 0});
    int r = poll(fds.data(), fds.size(), 1000);
    if (r < 0 && errno != EINTR) break;
    time_t now = time(nullptr);
    if (now - last_scan >= (pc >= 0 ? 10 : 2)) { rescan(); last_scan = now; }
    if (r <= 0) continue;
    if (fds[0].revents & POLLIN) {
      int cfd = accept4(srv, nullptr, nullptr, SOCK_CLOEXEC);
      if (cfd >= 0) {
        ucred cr{};
        socklen_t len = sizeof cr;
        getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cr, &len);
        g_clients.push_back({cfd, cr.uid});
      }
    }
    for (size_t i = 1; i < first_client; i++) {
      if (!(fds[i].revents & POLLIN)) continue;
      if (fds[i].fd == pc) read_proc_connector(pc);
      else if (fds[i].fd == km) read_kmsg(km);
      else if (fds[i].fd == trap4 || fds[i].fd == trap6) read_trap(fds[i].fd);
    }
    for (size_t i = first_client; i < fds.size(); i++) {
      if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
      auto it = std::find_if(g_clients.begin(), g_clients.end(), [&](const Client &c) { return c.fd == fds[i].fd; });
      if (it == g_clients.end()) continue;
      char buf[4096];
      ssize_t n = recv(it->fd, buf, sizeof buf, 0);
      if (n <= 0) { close(it->fd); g_clients.erase(it); continue; }
      it->in.append(buf, n);
      size_t nl;
      while ((nl = it->in.find('\n')) != std::string::npos) {
        std::string line = it->in.substr(0, nl);
        it->in.erase(0, nl + 1);
        send_str(it->fd, command(*it, line, it->uid, it->uid == 0));
      }
    }
  }
  // a clean stop gives the network back; a crash keeps the rules (fail closed)
  remove_rules();
  if (!g_cg.empty()) {
    std::ifstream procs(g_cg + "/nirvana-shield/cgroup.procs");
    int pid;
    while (procs >> pid) move(pid, false);
  }
  unlink(socket_path().c_str());
  log("stopped");
  return 0;
}

// ---------------------------------------------------------------------------
// ctl: what the panel runs through pkexec
// ---------------------------------------------------------------------------
bool exists(const char *p) { return access(p, F_OK) == 0; }
bool have(const char *prog) { return run({"sh", "-c", std::string("command -v ") + prog + " >/dev/null"}) == 0; }

int ctl(int argc, char **argv) {
  std::string what = argc > 2 ? argv[2] : "";
  if (what == "enable" || what == "disable") {
    bool on = what == "enable";
    int rc;
    if (exists("/run/systemd/system")) {
      rc = run({"systemctl", on ? "enable" : "disable", "--now", "nirvana-shield"});
    } else if (have("rc-update") && exists("/run/openrc")) {
      if (on) { run({"rc-update", "add", "nirvana-shield", "default"}); rc = run({"rc-service", "nirvana-shield", "start"}); }
      else { run({"rc-service", "nirvana-shield", "stop"}); rc = run({"rc-update", "del", "nirvana-shield", "default"}); }
    } else {
      if (on) { run({"update-rc.d", "nirvana-shield", "defaults"}); rc = run({"/etc/init.d/nirvana-shield", "start"}); }
      else { run({"/etc/init.d/nirvana-shield", "stop"}); rc = run({"update-rc.d", "nirvana-shield", "disable"}); }
    }
    return rc == 0 ? 0 : 1;
  }
  if (what == "send" && argc > 3) {   // root: one command to the service
    std::string reply;
    if (!shield::request(argv[3], reply)) { fprintf(stderr, "the service is not running\n"); return 2; }
    fputs(reply.c_str(), stdout);
    return reply.rfind("ok", 0) == 0 || reply.rfind("mode=", 0) == 0 ? 0 : 1;
  }
  fprintf(stderr, "usage: nirvana-shield [daemon] | ctl enable|disable | ctl send \"AS UID ALLOW /path\"\n");
  return 2;
}

}  // namespace

int main(int argc, char **argv) {
  std::string m = argc > 1 ? argv[1] : "daemon";
  if (m == "ctl") return ctl(argc, argv);
  if (getuid() != 0) { fprintf(stderr, "nirvana-shield: the service runs as root\n"); return 1; }
  return daemon_main();
}
