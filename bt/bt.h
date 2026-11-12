// Nirvana Desktop - Bluetooth through BlueZ (bluetoothctl)
//
// Like the network library: reads run bluetoothctl in non-interactive mode
// (LC_ALL=C - we parse the English output), longer actions (power, connect, ...)
// run in the background through Fl::add_fd, and a "bluetoothctl" process with an open
// stdin in the background reports every change ([NEW]/[CHG]/[DEL] lines) - after
// which the state is read again.
//
// Pairing needs an agent that answers BlueZ's questions (confirm the code, type
// a PIN). Session runs an interactive bluetoothctl on a pseudo terminal: it has an agent
// built in, and its questions come to us as prompts that the panel shows in a dialog.
//
// For testing: NIRVANA_BLUETOOTHCTL=/path/to/a/fake/bluetoothctl
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace bt {

struct Device {
  std::string mac, name;
  std::string icon;              // BlueZ "Icon": audio-headset, input-keyboard, phone, ...
  bool paired = false, trusted = false, connected = false, blocked = false;
  int battery = -1;              // percent, -1 unknown
  int rssi = 0;                  // signal (0 = unknown), only while searching
};

struct State {
  bool available = false;        // bluetoothctl works and there is an adapter
  std::string problem;           // why not
  std::string adapter;           // adapter name (the name others see)
  bool powered = false, discoverable = false, discovering = false;
  bool pairable = false;
  std::vector<Device> devices;   // paired first, then the others (sorted by name)
  int connected() const;         // how many are connected
};

bool installed();                // is bluetoothctl there
State query();                   // blocks briefly (a few bluetoothctl calls)
// "Headphones", "Keyboard"... from the BlueZ icon name (translated)
std::string kind_name(const std::string &icon);
// A readable message for a BlueZ error in the output ("org.bluez.Error.Blocked" ...)
std::string error_text(const std::string &out);

// Calls changed() (debounced) whenever BlueZ reports a change.
void watch(std::function<void()> changed);

using Done = std::function<void(bool ok, const std::string &message)>;
// The actions run in the background; done is called in the FLTK loop.
void set_power(bool on, Done done);          // also lifts an rfkill soft block when needed
void set_discoverable(bool on, Done done);
void connect(const std::string &mac, Done done);
void disconnect(const std::string &mac, Done done);
void remove(const std::string &mac, Done done);   // forget the device (unpair)

// An interactive bluetoothctl with its agent: searching and pairing.
class Session {
public:
  enum class Ask {
    Confirm,     // "Does the device show this code?" - yes/no (text = the code)
    Pin,         // type a PIN (legacy devices)
    Passkey,     // type a 6-digit number
    Show,        // type this code on the device (keyboards) - only information
    Authorize,   // the device wants a service / to pair - yes/no
  };
  std::function<void(Ask, const std::string &text)> on_ask;
  std::function<void(bool ok, const std::string &message)> on_paired;   // the end of pair()
  std::function<void()> on_change;                                       // [NEW]/[CHG]/[DEL]

  Session();
  ~Session();
  bool running() const { return pid_ > 0; }
  void scan(bool on);
  // Pair, then trust and connect. on_paired comes at the end (or at an error).
  void pair(const std::string &mac);
  void answer(const std::string &a);   // the reply to on_ask: "yes", "no", a PIN...
  void cancel_pairing();

private:
  int fd_ = -1;
  int pid_ = -1;
  std::string buf_, pairing_;
  int stage_ = 0;                      // 0 none, 1 pairing, 2 trusting, 3 connecting
  void send(const std::string &line);
  void feed(const std::string &text);
  void line(const std::string &l);
  void finish(bool ok, const std::string &msg);
  static void read_cb(int fd, void *d);
  static void timeout_cb(void *d);
};

}  // namespace bt
