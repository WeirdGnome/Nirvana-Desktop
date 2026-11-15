/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Printer preferences (prefs/printer): see printerprefs.h.
*/
#include "printerprefs.h"
#include "tools.h"
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

namespace {
struct Result { std::string out; bool ok = false; };

std::string quote(const std::string &s) {
    std::string q = "'";
    for (char c : s) { if (c == '\'') q += "'\\''"; else q += c; }
    return q + "'";
}

Result run(const std::vector<std::string> &argv) {
    std::string cmd;
    for (auto &a : argv) cmd += (cmd.empty() ? "" : " ") + quote(a);
    cmd += " 2>&1";
    Result r;
    if (FILE *p = popen(cmd.c_str(), "r")) {
        char buf[1024];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) r.out.append(buf, n);
        r.ok = pclose(p) == 0;
    }
    while (!r.out.empty() && (r.out.back() == '\n' || r.out.back() == ' ')) r.out.pop_back();
    return r;
}

bool have(const char *prog) { return run({"sh", "-c", std::string("command -v ") + prog}).ok; }

std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

// an option the printer's driver offers ("PageSize/Media Size: Letter *A4 ...")
struct Option { std::string key; std::vector<std::string> values; int current = 0; };

struct Printer {
    std::string name, state, description, location, uri;
    bool loaded = false;
    std::map<std::string, Option> options;
    int orientation = 0;                    // 0 portrait, 1 landscape
};

std::vector<Printer> read_printers(std::string &def) {
    std::vector<Printer> ps;
    std::istringstream in(run({"lpstat", "-l", "-p"}).out);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("printer ", 0) == 0) {
            Printer p;
            std::istringstream w(line.substr(8));
            w >> p.name;
            // "is idle.", "now printing Office-12.", "disabled since ..."
            std::string rest;
            std::getline(w, rest);
            rest = trim(rest);
            if (rest.rfind("is ", 0) == 0) rest = rest.substr(3);
            auto dot = rest.find('.');
            p.state = rest.substr(0, dot);
            if (p.state.rfind("disabled", 0) == 0) p.state = "disabled";
            else if (p.state.rfind("now printing", 0) == 0) p.state = "printing";
            ps.push_back(p);
        } else if (!ps.empty()) {
            std::string t = trim(line);
            if (t.rfind("Description:", 0) == 0) ps.back().description = trim(t.substr(12));
            else if (t.rfind("Location:", 0) == 0) ps.back().location = trim(t.substr(9));
        }
    }
    // "device for Office: ipp://..."
    std::istringstream v(run({"lpstat", "-v"}).out);
    while (std::getline(v, line)) {
        if (line.rfind("device for ", 0) != 0) continue;
        auto c = line.find(": ", 11);
        if (c == std::string::npos) continue;
        std::string n = line.substr(11, c - 11);
        for (auto &p : ps) if (p.name == n) p.uri = line.substr(c + 2);
    }
    // "system default destination: Office" (the user's own default from lpoptions counts)
    std::string d = run({"lpstat", "-d"}).out;
    auto c = d.find(": ");
    def = c == std::string::npos || d.rfind("no ", 0) == 0 ? "" : trim(d.substr(c + 2));
    return ps;
}

void load_options(Printer &p) {
    if (p.loaded) return;
    p.loaded = true;
    std::istringstream in(run({"lpoptions", "-p", p.name, "-l"}).out);
    std::string line;
    while (std::getline(in, line)) {
        auto colon = line.find(": ");
        if (colon == std::string::npos) continue;
        std::string head = line.substr(0, colon);
        Option o;
        o.key = head.substr(0, head.find('/'));
        std::istringstream w(line.substr(colon + 2));
        std::string val;
        while (w >> val) {
            if (val[0] == '*') { val = val.substr(1); o.current = (int)o.values.size(); }
            o.values.push_back(val);
        }
        if (!o.values.empty()) p.options[o.key] = o;
    }
    // the orientation is not the driver's: lpoptions -p NAME lists it among the settings
    std::string cur = " " + run({"lpoptions", "-p", p.name}).out + " ";
    p.orientation = cur.find(" orientation-requested=4 ") != std::string::npos ? 1 : 0;
}

// the settings shown, each as the drivers may call it
struct Shown { const char *label; std::vector<const char *> keys; };
const Shown SHOWN[] = {
    {"Paper size:", {"PageSize", "media"}},
    {"Paper type:", {"MediaType"}},
    {"Paper source:", {"InputSlot"}},
    {"Two-sided:", {"Duplex", "sides"}},
    {"Colour:", {"ColorModel", "print-color-mode"}},
    {"Resolution:", {"Resolution"}},
    {"Quality:", {"cupsPrintQuality", "print-quality"}},
};
const int NSHOWN = sizeof SHOWN / sizeof SHOWN[0];

// the drivers' words as people say them
std::string pretty(const std::string &v) {
    static const std::map<std::string, std::string> P = {
        {"None", "Off"}, {"DuplexNoTumble", "Long edge"}, {"DuplexTumble", "Short edge"},
        {"one-sided", "Off"}, {"two-sided-long-edge", "Long edge"}, {"two-sided-short-edge", "Short edge"},
        {"Gray", "Grey"}, {"Grayscale", "Grey"}, {"monochrome", "Grey"}, {"RGB", "Colour"}, {"CMYK", "Colour"}, {"color", "Colour"},
        {"Draft", "Draft"}, {"Normal", "Normal"}, {"High", "High"}, {"3", "Draft"}, {"4", "Normal"}, {"5", "High"}};
    auto i = P.find(v);
    return i == P.end() ? v : tr(i->second);
}

std::string test_page() {
    for (const char *f : {"/usr/share/cups/data/testprint", "/usr/share/cups/data/default-testpage.pdf"})
        if (fs::exists(f)) return f;
    // (without CUPS's own page: a few lines of text)
    std::string f = env_dir() + "/testpage.txt";
    std::error_code ec;
    fs::create_directories(env_dir(), ec);
    std::ofstream(f) << "Rambler\n\n" << tr("This is a test page.") << "\n";
    return f;
}
}  // namespace

static lune::Window *g_printer_editor = nullptr;

void edit_printer_prefs(bool standalone) {
    if (g_printer_editor) { g_printer_editor->show(); return; }
    bool cups = have("lpstat") && have("lpoptions");
    auto def = std::make_shared<std::string>();
    auto printers = std::make_shared<std::vector<Printer>>(cups ? read_printers(*def) : std::vector<Printer>());
    auto new_def = std::make_shared<std::string>(*def);
    // the changes, a printer's own: key -> value
    auto changes = std::make_shared<std::map<std::string, std::map<std::string, std::string>>>();

    auto *root = VGroup();
    auto *reg = new Register({"Printer", "Settings"});
    // -- Printer
    auto *pp = VGroup();
    auto *list = new List();
    list->frame(Frame_InputList);
    list->columns({tr("Printer"), tr("State"), tr("Description")});
    auto *ig = new Group(Group::Vertical);
    ig->columns(2);
    new Text("\033rDefault printer:");
    auto *is_def = new Checkmark(false);
    new Text("\033rLocation:");
    auto *loc = new Text("", Frame_Text);
    new Text("\033rDevice:");
    auto *dev = new Text("", Frame_Text);
    ig->end();
    auto *prow = HGroup();
    auto *testb = new Button("Print Test Page");
    HSpace();
    auto *manage = new Button("Printers...");
    prow->end();
    pp->end();
    // -- Settings (the printer chosen on the first page)
    auto *sp = VGroup();
    auto *sg = new Group(Group::Vertical, Frame_Group);
    sg->frame_title("Paper and Print");
    sg->columns(2);
    new Text("\033rPrinter:");
    auto *for_name = new Text("", Frame_Text);
    new Text("\033rOrientation:");
    auto *orient = new Cycle({"Portrait", "Landscape"}, 0);
    std::vector<Cycle *> cycles;
    for (int i = 0; i < NSHOWN; i++) {
        new Text(std::string("\033r") + SHOWN[i].label);
        cycles.push_back(new Cycle({"--------------------"}, 0));
    }
    sg->end();
    HVSpace();
    sp->end();
    reg->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();

    // which of a printer's options a row shows (its first key the driver has)
    auto option_of = [=](Printer &p, int row) -> Option * {
        for (const char *k : SHOWN[row].keys) {
            auto i = p.options.find(k);
            if (i != p.options.end()) return &i->second;
        }
        return nullptr;
    };
    auto value_of = [=](Printer &p, const Option &o) {
        auto c = changes->find(p.name);
        if (c != changes->end()) {
            auto v = c->second.find(o.key);
            if (v != c->second.end())
                for (size_t i = 0; i < o.values.size(); i++) if (o.values[i] == v->second) return (int)i;
        }
        return o.current;
    };
    // the printer chosen: its details and its settings
    auto show = [=] {
        int a = list->active();
        bool any = a >= 0 && a < (int)printers->size();
        is_def->disabled(!any);
        testb->disabled(!any);
        orient->disabled(!any);
        if (!any) {
            for_name->contents(printers->empty() ? tr(cups ? "No printers are set up." : "CUPS is not installed.") : "");
            for (Cycle *c : cycles) { c->entries({""}); c->disabled(true); }
            root->redraw();
            return;
        }
        Printer &p = (*printers)[a];
        load_options(p);
        is_def->on(p.name == *new_def);
        loc->contents(p.location);
        dev->contents(p.uri);
        for_name->contents(p.description.empty() ? p.name : p.description);
        int o = p.orientation;
        auto c = changes->find(p.name);
        if (c != changes->end() && c->second.count("orientation-requested")) o = c->second["orientation-requested"] == "4";
        orient->active(o);
        for (int i = 0; i < NSHOWN; i++) {
            Option *op = option_of(p, i);
            if (!op) { cycles[i]->entries({"-"}); cycles[i]->disabled(true); continue; }
            std::vector<std::string> shown;
            for (auto &v : op->values) shown.push_back(pretty(v));
            cycles[i]->entries(shown, value_of(p, *op));
            cycles[i]->disabled(op->values.size() < 2);
        }
        root->redraw();
    };
    auto fill = [=] {
        list->clear();
        for (auto &p : *printers)
            list->add({"", "", false, {(p.name == *new_def ? "* " : "") + p.name, tr(p.state), p.description}});
        list->done();
        if (!printers->empty()) {
            int a = 0;
            for (size_t i = 0; i < printers->size(); i++) if ((*printers)[i].name == *new_def) a = (int)i;
            list->active(a);
        }
    };
    fill();
    show();
    list->on_active = [=](int) { show(); };
    is_def->on_press = [=] {
        int a = list->active();
        if (a < 0) return;
        *new_def = is_def->on() ? (*printers)[a].name : (*new_def == (*printers)[a].name ? "" : *new_def);
        fill();
        list->active(a);
    };
    orient->on_change = [=](int v) {
        int a = list->active();
        if (a >= 0) (*changes)[(*printers)[a].name]["orientation-requested"] = v ? "4" : "3";
    };
    for (int i = 0; i < NSHOWN; i++) {
        Cycle *c = cycles[i];
        c->on_change = [=](int v) {
            int a = list->active();
            if (a < 0) return;
            Printer &p = (*printers)[a];
            if (Option *op = option_of(p, i)) if (v < (int)op->values.size()) (*changes)[p.name][op->key] = op->values[v];
        };
    }
    testb->on_press = [=] {
        int a = list->active();
        if (a < 0) return;
        const std::string &n = (*printers)[a].name;
        Result r = run({"lp", "-d", n, "-t", tr("Test page"), test_page()});
        request("Printer Preferences", r.ok ? tr("The test page has been sent to the printer.") + "\n" + r.out
                                             : tr("The test page could not be printed:") + "\n" + r.out, "OK");
    };
    // the printers set up (the system's own tool, else CUPS's web pages)
    manage->on_press = [=] {
        const char *cmd = have("system-config-printer") ? "system-config-printer" : "xdg-open http://localhost:631/printers/";
        run({"sh", "-c", std::string("setsid ") + cmd + " >/dev/null 2>&1 &"});
    };
    manage->disabled(!cups);

    // CUPS keeps the user's settings itself (lpoptions, ~/.cups): Use and Save set them alike
    auto put = [=]() -> bool {
        std::string err;
        if (*new_def != *def && !new_def->empty()) {
            Result r = run({"lpoptions", "-d", *new_def});
            if (!r.ok) err += r.out + "\n";
        }
        for (auto &[name, kv] : *changes) {
            if (kv.empty()) continue;
            std::vector<std::string> argv = {"lpoptions", "-p", name};
            for (auto &[k, v] : kv) { argv.push_back("-o"); argv.push_back(k + "=" + v); }
            Result r = run(argv);
            if (!r.ok) err += r.out + "\n";
        }
        if (!err.empty()) { request("Printer Preferences", tr("The settings could not be set:") + "\n" + err, "OK"); return false; }
        return true;
    };

    auto *w = new lune::Window("Printer Preferences", root);
    w->id("PRINTERPREFS");
    g_printer_editor = w;
    auto close = [w, standalone] {
        w->hide();
        Fl::delete_widget(w);
        g_printer_editor = nullptr;
        if (standalone) exit(0);
    };
    save->on_press = [=] { if (put()) close(); };
    use->on_press = [=] { if (put()) close(); };
    cancel->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    {
        int minw = 0, minh = 0, maxw = 0, maxh = 0;
        w->get_size_range(&minw, &minh, &maxw, &maxh);
        if (minw > 0) w->size(std::max(minw, 480), std::max(minh, 360));
    }
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
    if (!cups) request("Printer Preferences", tr("CUPS is not installed.") + "\n" + tr("Install the cups package to print."), "OK");
}

}  // namespace rambler
