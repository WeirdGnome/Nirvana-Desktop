// Nirvana Desktop - printing: printers and print jobs through CUPS (libcups, IPP),
// for the printer applet in the menu bar and the Printers panel.
//
// Reading the state and cancelling one's own jobs need no rights. Adding and
// removing printers, the system default and installing drivers go through
// nirvana-print-helper (pkexec) - see run_helper().
#pragma once

#include <string>
#include <vector>

namespace printing {

struct Printer {
  std::string name;            // the queue name
  std::string info;            // the description ("HP LaserJet 1020")
  std::string make_model;      // the driver ("HP LaserJet 1020, hpcups 3.23")
  std::string location;
  std::string uri;             // the device ("usb://HP/LaserJet%201020")
  int state = 3;               // 3 idle, 4 printing, 5 stopped
  std::string message;         // printer-state-message ("Waiting for printer to become available.")
  std::vector<std::string> reasons;   // printer-state-reasons without "none"
  bool accepting = true;
  bool is_default = false;
  int queued = 0;
};

struct Job {
  int id = 0;
  std::string printer, title, user;
  int size_kb = 0;
  int state = 3;               // 3 pending, 4 held, 5 processing, 6 stopped
  long created = 0;
};

struct State {
  bool installed = false;      // the CUPS client (lpstat) exists
  bool running = false;        // the scheduler answers
  std::vector<Printer> printers;
  std::vector<Job> jobs;       // active jobs of all users
  std::string default_printer;
};

State query();                                 // fast: local IPP requests
std::string state_text(const Printer &p);      // "Idle", "Printing", "Stopped - out of paper"
bool cancel_job(const std::string &printer, int id, std::string *err);
bool cancel_my_jobs(std::string *err);
bool set_user_default(const std::string &printer);   // ~/.cups/lpoptions, no root
bool print_test_page(const std::string &printer, std::string *err);

// Devices seen by CUPS (from "nirvana-print-helper devices" = lpinfo -l -v)
struct Device {
  std::string uri, cls, info, make_model, device_id;
  std::string manufacturer() const;   // from the device ID or the make and model
  bool driverless() const;            // IPP: works without a driver (IPP Everywhere / AirPrint)
};
std::vector<Device> parse_devices(const std::string &lpinfo_out);

// Drivers CUPS knows (lpinfo -m, no root): model name and description
struct Model { std::string name, desc; };
std::vector<Model> models();

// Driver packages that help with a manufacturer (IDs of nirvana-print-helper)
std::vector<std::string> suggested_drivers(const std::string &manufacturer);
struct DriverInfo { const char *id, *label; };
const std::vector<DriverInfo> &all_drivers();

std::string helper_path();   // nirvana-print-helper, "" when missing

}  // namespace printing
