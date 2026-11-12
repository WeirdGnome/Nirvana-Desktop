// Nirvana Desktop - NirvanaSHIELD: talking to the firewall service, and the programs a user has
//
// The service (shield/daemon.cxx, root) keeps every program of the user away from the
// network until the user allows it. This library is what the panel (alice) and the
// applet (Rambler) use: the status, the list of allowed programs, the blocked attempts,
// changing the list - and a list of the user's programs (from the .desktop files and the
// AppImages) with the program files each one runs, so that "Firefox" can be switched on
// as one tile even though it is a script starting another program file.
#pragma once

#include <string>
#include <vector>

namespace shield {

constexpr unsigned DNS_TRAP_PORT = 5399;     // the name lookups of not allowed programs (127.0.0.1/::1)
constexpr unsigned CLASSID = 0x4E530001;   // net_cls class id of the allowed group ("NS")
constexpr const char *RUN_DIR = "/run/nirvana-shield";
std::string socket_path();                 // /run/nirvana-shield.sock (NIRVANA_SHIELD_SOCKET for tests)

// One request, the reply up to the "." line. false: the service is not running.
bool request(const std::string &line, std::string &reply);

struct Status {
  bool running = false;
  std::string mode;      // protect | block | off
  bool lock = false;     // parental lock: changes need the administrator password
  bool lan = true;       // the local network always reachable
  std::string error;     // the service cannot tell programs apart (no net_cls, no nft...)
};
Status status();

struct Blocked { std::string path, dest; long when = 0; int count = 0; };
std::vector<std::string> allowed();          // program files on this user's list
std::vector<Blocked> blocked();              // recent attempts of not allowed programs

// Changes - "" when done, otherwise the error ("locked" when the parental lock is on).
std::string allow(const std::vector<std::string> &paths, bool on);
std::string set_mode(const std::string &mode);
std::string set_lan(bool on);
// The same through pkexec as root (the parental lock): asks for the administrator password.
std::string admin(const std::vector<std::string> &commands);   // "ALLOW /x", "MODE off", "LOCK 1"
std::string enable_service(bool on);                           // install/start or stop the service

// A long-lived connection that receives "blocked <path>\t<dest>" lines (-1: no service)
int watch_fd();

// The user's programs
struct Program {
  std::string name, icon, comment;
  std::vector<std::string> paths;   // the program files that mean this program
  bool appimage = false;
};
std::vector<Program> programs(const std::string &apps_dir);   // sorted by name
// A friendly name for a program file (from the programs list, else the file name)
std::string name_of(const std::string &path, const std::vector<Program> &progs, std::string *icon = nullptr);
std::string shield_exe();   // nirvana-shield (for pkexec)

}  // namespace shield
