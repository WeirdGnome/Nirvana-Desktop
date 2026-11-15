/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Network preferences (prefs/network/netpeditor.c, prefsdata.c): see networkprefs.h.
*/
#include "networkprefs.h"
#include "tools.h"
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/Fl_Input.H>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
#include <sys/wait.h>
#include <vector>

using namespace lune;

namespace rambler {

namespace {

// a program run with its arguments (no shell): its output (with the errors) and whether it
// succeeded
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
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return r;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) r.out.append(buf, n);
    int st = pclose(p);
    r.ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    return r;
}

std::vector<std::string> lines(const std::string &s) {
    std::vector<std::string> v;
    std::istringstream in(s);
    std::string l;
    while (std::getline(in, l)) v.push_back(l);
    return v;
}

// a terse nmcli line split at the colons ("\:" is a colon in a field)
std::vector<std::string> fields(const std::string &line) {
    std::vector<std::string> v(1);
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '\\' && i + 1 < line.size()) { v.back() += line[++i]; continue; }
        if (line[i] == ':') { v.emplace_back(); continue; }
        v.back() += line[i];
    }
    return v;
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// 24 <-> 255.255.255.0
std::string prefix_to_mask(int p) {
    unsigned m = p <= 0 ? 0 : p >= 32 ? 0xffffffffu : ~((1u << (32 - p)) - 1);
    return std::to_string(m >> 24) + "." + std::to_string((m >> 16) & 255) + "." + std::to_string((m >> 8) & 255) + "." +
           std::to_string(m & 255);
}

int mask_to_prefix(const std::string &mask) {
    if (mask.find('.') == std::string::npos) return atoi(mask.c_str());   // (a prefix typed in)
    unsigned a, b, c, d;
    if (sscanf(mask.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return -1;
    unsigned m = a << 24 | b << 16 | c << 8 | d;
    int p = 0;
    while (m & 0x80000000u) { p++; m <<= 1; }
    return m ? -1 : p;   // (ones, then only zeros)
}

bool valid_ip(const std::string &s) {
    unsigned a, b, c, d;
    char x;
    return sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &x) == 4 && a < 256 && b < 256 && c < 256 && d < 256;
}

// an interface as the preferences show it (InterfaceDesc: name, up, DHCP or the address)
struct Iface {
    std::string device, type, state, connection;
    bool up = false, dhcp = true;
    std::string ip, mask, gateway, dns;
    bool operator==(const Iface &o) const {
        return up == o.up && dhcp == o.dhcp && ip == o.ip && mask == o.mask && gateway == o.gateway && dns == o.dns;
    }
};

std::vector<Iface> read_interfaces() {
    std::vector<Iface> v;
    for (auto &l : lines(run({"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device"}).out)) {
        auto f = fields(l);
        if (f.size() < 4 || f[1] == "loopback" || f[1].empty()) continue;
        Iface i;
        i.device = f[0];
        i.type = f[1];
        i.state = f[2];
        i.connection = f[3];
        i.up = i.state.rfind("connected", 0) == 0;
        // the connection's settings (what Save keeps)
        if (!i.connection.empty()) {
            auto c = lines(run({"nmcli", "-g", "ipv4.method,ipv4.addresses,ipv4.gateway,ipv4.dns", "connection", "show",
                                i.connection}).out);
            if (c.size() >= 4) {
                i.dhcp = c[0] != "manual";
                std::string addr = fields(c[1])[0];
                addr = addr.substr(0, addr.find(','));
                auto slash = addr.find('/');
                if (slash != std::string::npos) {
                    i.ip = addr.substr(0, slash);
                    i.mask = prefix_to_mask(atoi(addr.c_str() + slash + 1));
                }
                i.gateway = c[2];
                std::string d;   // (shown "a, b")
                for (char ch : c[3]) d += ch == ',' ? std::string(", ") : std::string(1, ch);
                i.dns = d;
            }
        }
        // a DHCP interface: the address it has now, to see
        if (i.dhcp && i.up) {
            auto d = lines(run({"nmcli", "-g", "IP4.ADDRESS,IP4.GATEWAY", "device", "show", i.device}).out);
            if (d.size() >= 1) {
                std::string addr = d[0].substr(0, d[0].find(" | "));
                auto slash = addr.find('/');
                if (slash != std::string::npos) {
                    i.ip = addr.substr(0, slash);
                    i.mask = prefix_to_mask(atoi(addr.c_str() + slash + 1));
                }
            }
            if (d.size() >= 2) i.gateway = d[1];
        }
        v.push_back(i);
    }
    return v;
}

struct Wifi { std::string ssid, security; int signal = 0; bool in_use = false; };

std::vector<Wifi> read_wifi(bool rescan) {
    std::vector<Wifi> v;
    std::vector<std::string> a = {"nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list"};
    if (rescan) { a.push_back("--rescan"); a.push_back("yes"); }
    for (auto &l : lines(run(a).out)) {
        auto f = fields(l);
        if (f.size() < 4 || f[1].empty()) continue;
        if (std::any_of(v.begin(), v.end(), [&](const Wifi &w) { return w.ssid == f[1]; })) continue;   // (one per name)
        v.push_back({f[1], f[3], atoi(f[2].c_str()), f[0] == "*"});
    }
    return v;
}

// the arguments of an interface's IPv4 settings
std::vector<std::string> ipv4_args(const Iface &i) {
    // the name servers separated by commas or spaces -> "a,b"
    std::string dns;
    std::istringstream in(i.dns);
    std::string tok;
    while (std::getline(in, tok, ',')) {
        std::istringstream words(tok);
        std::string w;
        while (words >> w) dns += (dns.empty() ? "" : ",") + w;
    }
    if (i.dhcp) return {"ipv4.method", "auto", "ipv4.addresses", "", "ipv4.gateway", "", "ipv4.dns", dns};
    return {"ipv4.method", "manual", "ipv4.addresses", i.ip + "/" + std::to_string(mask_to_prefix(i.mask)),
            "ipv4.gateway", i.gateway, "ipv4.dns", dns};
}

// what is wrong with an interface's settings ("" - nothing)
std::string check(const Iface &i) {
    if (!i.up || i.dhcp) return "";
    if (!valid_ip(i.ip)) return trs("%s: the IP address is not valid.", i.device);
    if (mask_to_prefix(i.mask) < 0) return trs("%s: the netmask is not valid.", i.device);
    if (!i.gateway.empty() && !valid_ip(i.gateway)) return trs("%s: the gateway is not valid.", i.device);
    return "";
}
}  // namespace

static Window *g_net_editor = nullptr;

void edit_network_prefs(bool standalone) {
    if (g_net_editor) { g_net_editor->show(); return; }
    if (!run({"nmcli", "-t", "general", "status"}).ok) {
        request("Network Preferences",
                tr("The network is set up by NetworkManager, which is not running\n(or nmcli is not installed)."), "OK");
        if (standalone) exit(0);
        return;
    }
    auto loaded = std::make_shared<std::vector<Iface>>(read_interfaces());
    auto work = std::make_shared<std::vector<Iface>>(*loaded);
    auto wifi = std::make_shared<std::vector<Wifi>>(read_wifi(false));
    std::string hostname = trim(run({"nmcli", "general", "hostname"}).out);
    auto cur = std::make_shared<int>(-1);   // the interface shown

    auto *root = VGroup();
    auto *reg = new Register({"Interfaces", "Host", "Wireless"});
    // -- Interfaces: the list, the chosen one's settings
    auto *ip = HGroup();
    auto *lg = new Group(Group::Vertical, Frame_Group);
    lg->frame_title("Interfaces");
    auto *ilist = new List();
    ilist->frame(Frame_InputList);
    for (auto &i : *work) ilist->add({i.device + " (" + i.type + ")"});
    ilist->done();
    lg->end();
    auto *rg = VGroup();
    auto *ig = new Group(Group::Vertical, Frame_Group);
    ig->frame_title("Interface");
    ig->columns(2);
    new Text("\033rName:");
    auto *name = new Text("\033l" + std::string(24, 'n'), Frame_Text);
    new Text("\033rState:");
    auto *state = new Text("\033l", Frame_Text);
    new Text("\033rUp:");
    auto *uprow = HGroup();
    auto *up = new Checkmark(false);
    HSpace();
    uprow->end();
    new Text("\033rIP Mode:");
    auto *mode = new Cycle({"DHCP", "Static"}, 0);
    new Text("\033rIP:");
    auto *ipf = new String("", 15);
    new Text("\033rNetmask:");
    auto *maskf = new String("", 15);
    new Text("\033rGateway:");
    auto *gwf = new String("", 15);
    new Text("\033rDNS:");
    auto *dnsf = new String("", 64);
    ig->end();
    HVSpace();
    rg->end();
    ip->end();
    // -- Host: the computer's name
    auto *hp = VGroup();
    auto *hg = new Group(Group::Vertical, Frame_Group);
    hg->frame_title("Host");
    hg->columns(2);
    new Text("\033rHost name:");
    auto *hostf = new String(hostname, 64);
    hg->end();
    HVSpace();
    hp->end();
    // -- Wireless: the networks in reach, connecting to one
    auto *wp = VGroup();
    auto *wg = new Group(Group::Vertical, Frame_Group);
    wg->frame_title("Networks");
    auto *wlist = new List();
    wlist->frame(Frame_InputList);
    wlist->columns({tr("Network"), tr("Signal"), tr("Security")}, {false, true, false});
    wg->end();
    auto *wc = new Group(Group::Vertical, Frame_Group);
    wc->frame_title("Connect");
    wc->columns(2);
    new Text("\033rPassword:");
    auto *pw = new String("", 64);
    pw->input()->type(FL_SECRET_INPUT);
    wc->end();
    auto *wb = HGroup();
    auto *rescan = new Button("Rescan");
    HSpace();
    auto *connect = new Button("Connect");
    wb->end();
    wp->end();
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

    auto fill_wifi = [=] {
        wlist->clear();
        for (auto &w : *wifi) {
            List::Line l;
            l.cols = {(w.in_use ? "* " : "") + w.ssid, std::to_string(w.signal) + "%", w.security.empty() ? tr("None") : w.security};
            wlist->add(l);
        }
        wlist->done();
    };
    fill_wifi();
    // the fields from the interface's settings, and back
    auto enable = [=] {
        bool fixed = mode->active() == 1;
        ipf->disabled(!fixed);
        maskf->disabled(!fixed);
        gwf->disabled(!fixed);
    };
    auto store = [=] {
        if (*cur < 0) return;
        Iface &i = (*work)[*cur];
        i.up = up->on();
        i.dhcp = mode->active() == 0;
        if (!i.dhcp) { i.ip = trim(ipf->value()); i.mask = trim(maskf->value()); i.gateway = trim(gwf->value()); }
        i.dns = trim(dnsf->value());
    };
    auto show = [=](int n) {
        store();
        *cur = n;
        if (n < 0 || n >= (int)work->size()) return;
        Iface &i = (*work)[n];
        name->contents("\033l" + i.device + (i.connection.empty() ? "" : "  (" + i.connection + ")"));
        state->contents("\033l" + tr(i.state));
        up->on(i.up);
        mode->active(i.dhcp ? 0 : 1);
        ipf->value(i.ip);
        maskf->value(i.mask);
        gwf->value(i.gateway);
        dnsf->value(i.dns);
        enable();
        for (Fl_Widget *wd : {(Fl_Widget *)ipf, (Fl_Widget *)maskf, (Fl_Widget *)gwf, (Fl_Widget *)dnsf}) wd->redraw();
    };
    ilist->on_active = show;
    mode->on_change = [=](int) { enable(); };
    rescan->on_press = [=] { *wifi = read_wifi(true); fill_wifi(); };
    connect->on_press = [=] {
        int i = wlist->active();
        if (i < 0 || i >= (int)wifi->size()) return;
        std::vector<std::string> a = {"nmcli", "device", "wifi", "connect", (*wifi)[i].ssid};
        std::string p = pw->value();
        if (!p.empty()) { a.push_back("password"); a.push_back(p); }
        Fl::check();
        Result r = run(a);
        request("Network Preferences", r.ok ? trs("Connected to %s.", (*wifi)[i].ssid) : trim(r.out), "OK");
        *wifi = read_wifi(false);
        fill_wifi();
    };

    // Use: the interfaces now (device modify, not kept); Save: their connections (kept), the
    // host name. What fails is told in one requester.
    auto put = [=](bool keep) -> bool {
        store();
        for (auto &i : *work)
            if (std::string e = check(i); !e.empty()) { request("Network Preferences", e, "OK"); return false; }
        std::string errors;
        auto note = [&errors](const Result &r) { if (!r.ok) errors += trim(r.out) + "\n"; };
        for (size_t n = 0; n < work->size(); n++) {
            Iface &i = (*work)[n], &was = (*loaded)[n];
            if (i == was) continue;
            if (!i.up) { if (was.up) note(run({"nmcli", "device", "disconnect", i.device})); continue; }
            bool settings = i.dhcp != was.dhcp || i.ip != was.ip || i.mask != was.mask || i.gateway != was.gateway || i.dns != was.dns;
            if (settings) {
                std::vector<std::string> a;
                if (keep && !i.connection.empty()) a = {"nmcli", "connection", "modify", i.connection};
                else a = {"nmcli", "device", "modify", i.device};
                for (auto &x : ipv4_args(i)) a.push_back(x);
                note(run(a));
                if (keep && !i.connection.empty()) note(run({"nmcli", "connection", "up", i.connection}));
            } else if (!was.up) note(run({"nmcli", "device", "connect", i.device}));
        }
        std::string h = trim(hostf->value());
        if (keep && !h.empty() && h != hostname) note(run({"nmcli", "general", "hostname", h}));
        if (!errors.empty()) { request("Network Preferences", errors, "OK"); return false; }
        return true;
    };
    auto *w = new Window("Network Preferences", root);
    w->id("NETWORKPREFS");
    g_net_editor = w;
    auto close = [w, standalone] {
        w->hide();
        Fl::delete_widget(w);
        g_net_editor = nullptr;
        if (standalone) exit(0);
    };
    save->on_press = [=] { if (put(true)) close(); };
    use->on_press = [=] { if (put(false)) close(); };
    cancel->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    if (!work->empty()) { ilist->active(0); show(0); }
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace rambler
