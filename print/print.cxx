// Nirvana Desktop - printing through CUPS (see print.h)
#include "print.h"
#include "i18n.h"

#include <cups/cups.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <unistd.h>

namespace printing {

using plat::fmt;

static bool have(const char *prog) {
  const char *path = getenv("PATH");
  std::string dirs = std::string(path ? path : "") + ":/usr/sbin:/sbin";
  size_t a = 0;
  while (a <= dirs.size()) {
    size_t b = dirs.find(':', a);
    if (b == std::string::npos) b = dirs.size();
    if (!access((dirs.substr(a, b - a) + "/" + prog).c_str(), X_OK)) return true;
    a = b + 1;
  }
  return false;
}

// a short command, its output; "timeout" guards against a hanging scheduler
static int run(const std::string &cmd, std::string *out = nullptr) {
  std::string c = (have("timeout") ? "timeout 10 " : "") + cmd + " 2>&1";
  FILE *p = popen(c.c_str(), "r");
  if (!p) return -1;
  char buf[4096];
  size_t n;
  std::string o;
  while ((n = fread(buf, 1, sizeof buf, p)) > 0) o.append(buf, n);
  int st = pclose(p);
  if (out) *out = o;
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static std::string quote(const std::string &s) {   // for the shell
  std::string r = "'";
  for (char c : s) r += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return r + "'";
}

State query() {
  State s;
  s.installed = have("lpstat");
  // configured queues (CUPS-Get-Printers) - not the slow network discovery of cupsGetDests
  ipp_t *req = ippNewRequest(IPP_OP_CUPS_GET_PRINTERS);
  static const char *attrs[] = {"printer-name", "printer-state", "printer-state-reasons", "printer-state-message",
                                "printer-info", "printer-make-and-model", "device-uri", "printer-is-accepting-jobs",
                                "printer-location", "queued-job-count"};
  ippAddStrings(req, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", sizeof attrs / sizeof attrs[0], nullptr,
                attrs);
  ipp_t *resp = cupsDoRequest(CUPS_HTTP_DEFAULT, req, "/");
  if (!resp) return s;   // the scheduler does not run (or is not installed)
  s.running = true;
  s.installed = true;
  Printer cur;
  bool any = false;
  auto flush = [&] {
    if (any && !cur.name.empty()) s.printers.push_back(cur);
    cur = Printer();
    any = false;
  };
  for (ipp_attribute_t *a = ippFirstAttribute(resp); a; a = ippNextAttribute(resp)) {
    if (!ippGetName(a)) { flush(); continue; }   // the next printer
    if (ippGetGroupTag(a) != IPP_TAG_PRINTER) continue;
    any = true;
    std::string n = ippGetName(a);
    const char *str = ippGetString(a, 0, nullptr);
    if (n == "printer-name" && str) cur.name = str;
    else if (n == "printer-info" && str) cur.info = str;
    else if (n == "printer-make-and-model" && str) cur.make_model = str;
    else if (n == "printer-location" && str) cur.location = str;
    else if (n == "device-uri" && str) cur.uri = str;
    else if (n == "printer-state-message" && str) cur.message = str;
    else if (n == "printer-state") cur.state = ippGetInteger(a, 0);
    else if (n == "printer-is-accepting-jobs") cur.accepting = ippGetBoolean(a, 0);
    else if (n == "queued-job-count") cur.queued = ippGetInteger(a, 0);
    else if (n == "printer-state-reasons")
      for (int i = 0; i < ippGetCount(a); i++) {
        const char *r = ippGetString(a, i, nullptr);
        if (r && strcmp(r, "none")) cur.reasons.push_back(r);
      }
  }
  flush();
  ippDelete(resp);
  if (const char *d = cupsGetDefault2(CUPS_HTTP_DEFAULT)) s.default_printer = d;
  for (auto &p : s.printers) p.is_default = p.name == s.default_printer;
  std::sort(s.printers.begin(), s.printers.end(), [](const Printer &a, const Printer &b) {
    if (a.is_default != b.is_default) return a.is_default;
    return a.name < b.name;
  });
  cups_job_t *jobs = nullptr;
  int n = cupsGetJobs2(CUPS_HTTP_DEFAULT, &jobs, nullptr, 0, CUPS_WHICHJOBS_ACTIVE);
  for (int i = 0; i < n; i++) {
    Job j;
    j.id = jobs[i].id;
    j.printer = jobs[i].dest ? jobs[i].dest : "";
    j.title = jobs[i].title ? jobs[i].title : "";
    j.user = jobs[i].user ? jobs[i].user : "";
    j.size_kb = jobs[i].size;
    j.state = jobs[i].state;
    j.created = (long)jobs[i].creation_time;
    s.jobs.push_back(j);
  }
  if (jobs) cupsFreeJobs(n, jobs);
  return s;
}

std::string state_text(const Printer &p) {
  std::string base = p.state == 4 ? _("Printing") : p.state == 5 ? _("Stopped") : _("Idle");
  if (!p.accepting) base = _("Not accepting jobs");
  // the most common reasons in words; others as the printer says them
  static const struct { const char *key, *text; } R[] = {
      {"media-empty", N_("out of paper")},          {"media-jam", N_("paper jam")},
      {"toner-empty", N_("out of toner")},          {"toner-low", N_("toner low")},
      {"marker-supply-empty", N_("out of ink")},    {"marker-supply-low", N_("ink low")},
      {"door-open", N_("a door is open")},          {"cover-open", N_("the cover is open")},
      {"offline", N_("offline")},                   {"paused", N_("paused")},
      {"connecting-to-device", N_("connecting…")}, {"cups-missing-filter", N_("the driver is missing")},
  };
  for (auto &r : p.reasons)
    for (auto &k : R)
      if (r.rfind(k.key, 0) == 0) return base + " – " + _(k.text);
  if (p.state == 5 && !p.message.empty()) return base + " – " + p.message;
  return base;
}

bool cancel_job(const std::string &printer, int id, std::string *err) {
  if (cupsCancelJob2(CUPS_HTTP_DEFAULT, printer.c_str(), id, 0) == IPP_STATUS_OK) return true;
  if (err) *err = cupsLastErrorString();
  return false;
}

bool cancel_my_jobs(std::string *err) {
  const char *u = cupsUser();
  std::string out;
  if (run("cancel -u " + quote(u ? u : ""), &out) == 0) return true;
  if (err) *err = out;
  return false;
}

bool set_user_default(const std::string &printer) { return run("lpoptions -d " + quote(printer)) == 0; }

bool print_test_page(const std::string &printer, std::string *err) {
  for (const char *f : {"/usr/share/cups/data/testprint", "/usr/share/cups/data/default-testpage.pdf"})
    if (!access(f, R_OK)) {
      std::string out;
      if (run("lp -d " + quote(printer) + " -t " + quote(_("Test page")) + " " + f, &out) == 0) return true;
      if (err) *err = out;
      return false;
    }
  // without the CUPS test page: a few lines of text
  std::string out;
  std::string cmd = "printf '%s\\n\\n%s\\n' " + quote(_("Nirvana Desktop - printer test page")) + " " +
                    quote(_("If you can read this, the printer works.")) + " | lp -d " + quote(printer) + " -t " +
                    quote(_("Test page"));
  if (run(cmd, &out) == 0) return true;
  if (err) *err = out;
  return false;
}

// --- devices -------------------------------------------------------------------------------
std::string Device::manufacturer() const {
  // device ID: "MFG:HP;MDL:LaserJet 1020;..." (or MANUFACTURER:)
  for (const char *key : {"MFG:", "MANUFACTURER:"}) {
    size_t p = device_id.find(key);
    if (p != std::string::npos) {
      p += strlen(key);
      size_t e = device_id.find(';', p);
      return device_id.substr(p, e == std::string::npos ? std::string::npos : e - p);
    }
  }
  std::string m = make_model.empty() ? info : make_model;
  return m.substr(0, m.find(' '));
}

bool Device::driverless() const {
  for (const char *s : {"ipp://", "ipps://", "http://", "https://"})
    if (!uri.compare(0, strlen(s), s)) return true;
  // dnssd://...._ipp._tcp... or ._ipps._tcp
  return !uri.compare(0, 8, "dnssd://") && uri.find("._ipp") != std::string::npos;
}

std::vector<Device> parse_devices(const std::string &out) {
  // lpinfo -l -v:
  // Device: uri = usb://HP/LaserJet%201020?serial=...
  //         class = direct
  //         info = HP LaserJet 1020
  //         make-and-model = HP LaserJet 1020
  //         device-id = MFG:Hewlett-Packard;MDL:...
  //         location =
  std::vector<Device> v;
  std::istringstream in(out);
  std::string l;
  Device cur;
  auto val = [](const std::string &line) {
    size_t e = line.find(" = ");
    return e == std::string::npos ? std::string() : line.substr(e + 3);
  };
  auto flush = [&] {
    // only real devices - not the bare backends ("network ipp", "direct usb")
    if (cur.uri.find("://") != std::string::npos || cur.uri.find(":/") != std::string::npos) v.push_back(cur);
    cur = Device();
  };
  while (std::getline(in, l)) {
    std::string t = l;
    t.erase(0, t.find_first_not_of(" \t"));
    if (!t.compare(0, 7, "Device:")) { if (!cur.uri.empty()) flush(); cur.uri = val(t); }
    else if (!t.compare(0, 5, "class")) cur.cls = val(t);
    else if (!t.compare(0, 4, "info")) cur.info = val(t);
    else if (!t.compare(0, 14, "make-and-model")) cur.make_model = val(t);
    else if (!t.compare(0, 9, "device-id")) cur.device_id = val(t);
  }
  if (!cur.uri.empty()) flush();
  return v;
}

std::vector<Model> models() {
  std::string out;
  std::vector<Model> v;
  if (run("lpinfo -m", &out) != 0) return v;
  std::istringstream in(out);
  std::string l;
  while (std::getline(in, l)) {
    size_t sp = l.find(' ');
    if (sp == std::string::npos) continue;
    v.push_back({l.substr(0, sp), l.substr(sp + 1)});
  }
  std::sort(v.begin(), v.end(), [](const Model &a, const Model &b) { return a.desc < b.desc; });
  return v;
}

const std::vector<DriverInfo> &all_drivers() {
  static const std::vector<DriverInfo> d = {
      {"filters", N_("Driverless printing and PDF (cups-filters)")},
      {"gutenprint", N_("Gutenprint – many inkjet and photo printers (Canon, Epson…)")},
      {"hplip", N_("HPLIP – HP printers and multifunction devices")},
      {"brother", N_("Brother lasers (brlaser)")},
      {"epson", N_("Epson inkjets (ESC/P-R)")},
      {"samsung", N_("Samsung and Xerox SPL lasers (SpliX)")},
      {"foo2zjs", N_("ZjStream lasers – HP, Minolta, Oki… (foo2zjs)")},
      {"foomatic", N_("Foomatic – a large database of older drivers")},
      {"ppds", N_("Manufacturer PPDs – Kyocera, Ricoh, Lexmark, Sharp…")},
  };
  return d;
}

std::vector<std::string> suggested_drivers(const std::string &mfg) {
  std::string m = mfg;
  std::transform(m.begin(), m.end(), m.begin(), ::tolower);
  auto has = [&](const char *s) { return m.find(s) != std::string::npos; };
  if (has("hp") || has("hewlett")) return {"hplip", "foo2zjs"};
  if (has("brother")) return {"brother", "foomatic"};
  if (has("epson")) return {"epson", "gutenprint"};
  if (has("canon")) return {"gutenprint", "foomatic"};
  if (has("samsung")) return {"samsung", "foomatic"};
  if (has("xerox")) return {"samsung", "ppds"};
  if (has("kyocera") || has("ricoh") || has("lexmark") || has("sharp") || has("konica") || has("toshiba") ||
      has("lanier") || has("savin") || has("gestetner") || has("infotec") || has("nrg"))
    return {"ppds", "foomatic"};
  if (has("minolta") || has("oki")) return {"foo2zjs", "foomatic"};
  return {"gutenprint", "foomatic"};
}

std::string helper_path() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  std::string e = n > 0 ? std::string(buf, (size_t)n) : "";
  e = e.substr(0, e.rfind('/'));
  for (const std::string &c : {e + "/../libexec/nirvana-print-helper", e + "/../update/nirvana-print-helper",
                               std::string("/usr/libexec/nirvana-print-helper")})
    if (!access(c.c_str(), R_OK)) return c;
  return "";
}

}  // namespace printing
