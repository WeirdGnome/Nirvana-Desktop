// Nirvana Desktop - power: battery, brightness, profiles, sleeping, projection mode
//
// Battery and brightness are read from /sys (works without UPower, also on Devuan),
// brightness is set through logind/elogind (Session.SetBrightness - without root),
// actions (sleep, hibernate, shut down, restart) through logind/elogind.
//
// Projection mode suspends at once:
// - blanking and sleeping of the X screen (XScreenSaverSuspend + DPMS),
// - the screen lock (the screensaver does not start, so neither does the lock),
// - sleeping when idle and manual sleep from logind ("idle:sleep"),
// - the same in GNOME/KDE/Xfce (org.gnome.SessionManager, org.freedesktop.ScreenSaver).
#pragma once

#include <string>
#include <vector>

namespace power {

struct Battery {
  std::string name;
  int percent = 0;
  std::string status;          // Charging, Discharging, Full, Not charging
  int minutes = -1;            // until empty or full; -1 unknown
};

struct State {
  bool on_ac = true;           // AC adapter connected (or a desktop computer)
  std::vector<Battery> batteries;
  int percent = -1;            // total, -1 when there is no battery
  bool charging = false;
  int minutes = -1;
  // screen brightness
  std::string backlight;       // e.g. intel_backlight; empty - none
  int brightness = -1;         // percent
  // power profiles: power-profiles-daemon, otherwise the kernel (platform profile, EPP, governor)
  std::vector<std::string> profiles;   // power-saver, balanced, performance
  std::string profile;
  bool kernel_profiles = false;        // without power-profiles-daemon (set through nirvana-power-helper)
  // logind
  bool logind = false;
  bool can_suspend = false, can_hibernate = false, can_poweroff = false, can_reboot = false;
};

State query();
std::string battery_text(const State &s);   // "78%, about 3 h 10 min left"
const char *profile_name(const std::string &p);

bool set_brightness(const State &s, int percent);
void step_brightness(int delta);
bool set_profile(const std::string &p);
void restore_profile();   // at the session start: the profile chosen last time (kernel profiles only)

// System actions; they return an error message or "".
std::string suspend();
std::string hibernate();
std::string poweroff();
std::string reboot();

// Screen lock with an external program (xsecurelock, i3lock, slock, xscreensaver...).
std::string locker();          // the command, or "" when there is none
std::string nirvana_lock();    // the path of nirvana-lock (Nirvana's own saver and lock), or ""
bool saver_running();          // nirvana-lock is on the screen
bool start_saver();            // nirvana-lock --saver (when idle)
bool lock_screen();

// --- projection mode ----------------------------------------------------------------
bool projection();
// Returns the list of what could be suspended (to show), empty when turning off.
std::vector<std::string> set_projection(bool on);

// --- idleness (only in a Nirvana Desktop session, under Insight) ---------------------------------
struct IdleSettings {
  int blank_min = 10;          // blank the screen after N min (0 = never)
  bool lock_on_blank = true;   // lock when blanking
  int sleep_battery_min = 15;  // sleep on battery (0 = never)
  int sleep_ac_min = 0;        // sleep on AC power
  int critical_action = 1;     // at 3% battery: 0 nothing, 1 hibernate (or sleep), 2 sleep
  // the screen saver (nirvana-lock): blank_min is when it starts
  std::string saver = "stars"; // savers::list() id
  int lock_delay_min = 0;      // lock after the saver ran N min (0 = at once); needs lock_on_blank
  int monitor_off_min = 10;    // the monitor goes to sleep N min after the saver started (0 = never)
};
IdleSettings read_idle();
bool write_idle(const IdleSettings &s);
void apply_idle(const IdleSettings &s);      // screensaver and DPMS times in X
int idle_seconds();                          // time without mouse/keyboard activity
constexpr const char *SETTINGS_ATOM = "_NIRVANA_POWER";
void notify_settings_changed();

}  // namespace power
