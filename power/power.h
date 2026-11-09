// LisaOS - zasilanie: bateria, jasność, profile, usypianie, tryb projekcji
//
// Bateria i jasność czytane z /sys (działa bez UPowera, także na Devuanie),
// jasność ustawiana przez logind/elogind (Session.SetBrightness - bez roota),
// akcje (uśpij, hibernuj, wyłącz, uruchom ponownie) przez logind/elogind.
//
// Tryb projekcji wstrzymuje naraz:
//   - wygaszanie i usypianie ekranu X (XScreenSaverSuspend + DPMS),
//   - blokadę ekranu (wygaszacz nie startuje, więc blokada też nie),
//   - usypianie po bezczynności i ręczne uśpienie z logind ("idle:sleep"),
//   - to samo w GNOME/KDE/Xfce (org.gnome.SessionManager, org.freedesktop.ScreenSaver).
#pragma once

#include <string>
#include <vector>

namespace power {

struct Battery {
  std::string name;
  int percent = 0;
  std::string status;          // Charging, Discharging, Full, Not charging
  int minutes = -1;            // do rozładowania albo naładowania; -1 nieznane
};

struct State {
  bool on_ac = true;           // zasilacz podłączony (albo komputer stacjonarny)
  std::vector<Battery> batteries;
  int percent = -1;            // łącznie, -1 gdy brak baterii
  bool charging = false;
  int minutes = -1;
  // jasność ekranu
  std::string backlight;       // np. intel_backlight; puste - brak
  int brightness = -1;         // procent
  // profile zasilania (power-profiles-daemon)
  std::vector<std::string> profiles;   // power-saver, balanced, performance
  std::string profile;
  // logind
  bool logind = false;
  bool can_suspend = false, can_hibernate = false, can_poweroff = false, can_reboot = false;
};

State query();
std::string battery_text(const State &s);   // "78%, zostało ok. 3 h 10 min"
const char *profile_name(const std::string &p);

bool set_brightness(const State &s, int percent);
void step_brightness(int delta);
bool set_profile(const std::string &p);

// Akcje systemowe; zwracają komunikat błędu albo "".
std::string suspend();
std::string hibernate();
std::string poweroff();
std::string reboot();

// Blokada ekranu zewnętrznym programem (xsecurelock, i3lock, slock, xscreensaver...).
std::string locker();          // polecenie albo "" gdy brak
bool lock_screen();

// --- tryb projekcji -----------------------------------------------------------------
bool projection();
// Zwraca listę tego, co udało się wstrzymać (do pokazania), pustą przy wyłączaniu.
std::vector<std::string> set_projection(bool on);

// --- bezczynność (tylko w sesji LisaOS, pod Insightem) ----------------------------------
struct IdleSettings {
  int blank_min = 10;          // wygaszenie ekranu po N min (0 = nigdy)
  bool lock_on_blank = true;   // blokada przy wygaszeniu
  int sleep_battery_min = 15;  // uśpienie na baterii (0 = nigdy)
  int sleep_ac_min = 0;        // uśpienie z zasilacza
  int critical_action = 1;     // przy 3% baterii: 0 nic, 1 hibernuj (albo uśpij), 2 uśpij
};
IdleSettings read_idle();
bool write_idle(const IdleSettings &s);
void apply_idle(const IdleSettings &s);      // czasy wygaszacza i DPMS w X
int idle_seconds();                          // czas bez ruchu myszy/klawiatury
constexpr const char *SETTINGS_ATOM = "_LISAOS_ZASILANIE";
void notify_settings_changed();

}  // namespace power
