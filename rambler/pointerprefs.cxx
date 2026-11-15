/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Pointer preferences (prefs/pointer/peeditor.c): see pointerprefs.h. The pointer
    pictures are read from the theme's Xcursor files (no library needed for showing them).
*/
#include "pointerprefs.h"
#include "tools.h"
#include "lune.h"
#include "catalog.h"

#include <FL/Fl.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <vector>

namespace fs = std::filesystem;
using namespace lune;

namespace rambler {

namespace {
std::string saved_file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/rambler/pointer.prefs";
}

const int SIZES[] = {0, 24, 32, 48, 64, 96};

// where cursor themes are (as libXcursor looks for them)
std::vector<std::string> icon_dirs() {
    std::vector<std::string> v;
    std::string home = getenv("HOME") ? getenv("HOME") : "";
    if (!home.empty()) { v.push_back(home + "/.local/share/icons"); v.push_back(home + "/.icons"); }
    const char *xd = getenv("XDG_DATA_DIRS");
    std::string dirs = xd && *xd ? xd : "/usr/local/share:/usr/share";
    for (size_t a = 0; a < dirs.size();) {
        size_t b = dirs.find(':', a);
        if (b == std::string::npos) b = dirs.size();
        if (b > a) v.push_back(dirs.substr(a, b - a) + "/icons");
        a = b + 1;
    }
    v.push_back("/usr/share/pixmaps");
    return v;
}

// the themes that have pointers (a cursors directory)
std::vector<std::string> cursor_themes() {
    std::set<std::string> s;
    std::error_code ec;
    for (auto &d : icon_dirs())
        for (auto &e : fs::directory_iterator(d, ec))
            if (fs::is_directory(e.path() / "cursors", ec)) s.insert(e.path().filename().string());
    return {s.begin(), s.end()};
}

std::string inherits(const std::string &theme) {
    std::error_code ec;
    for (auto &d : icon_dirs()) {
        std::ifstream in(d + "/" + theme + "/index.theme");
        std::string line;
        while (std::getline(in, line))
            if (line.rfind("Inherits=", 0) == 0) {
                std::string v = line.substr(9);
                return v.substr(0, v.find_first_of(",;"));
            }
    }
    return "";
}

// a pointer of a theme (its own or one it inherits): the Xcursor file
std::string cursor_file(std::string theme, const std::vector<std::string> &names) {
    std::error_code ec;
    for (int depth = 0; depth < 4 && !theme.empty(); depth++) {
        for (auto &d : icon_dirs())
            for (auto &n : names) {
                std::string f = d + "/" + theme + "/cursors/" + n;
                if (fs::exists(f, ec)) return f;
            }
        theme = inherits(theme);
    }
    return "";
}

// An Xcursor file: "Xcur", header size, version, the number of entries; each entry type,
// subtype (the nominal size), position; an image chunk: header (36 bytes: size, type,
// subtype, version, width, height, xhot, yhot, delay), then width*height ARGB words
// (premultiplied). The image nearest the size asked for.
struct Picture { int w = 0, h = 0; std::vector<uint8_t> rgba; };

Picture read_xcursor(const std::string &file, int want) {
    Picture p;
    std::ifstream in(file, std::ios::binary);
    auto u32 = [&in](uint32_t &v) {
        uint8_t b[4];
        if (!in.read((char *)b, 4)) return false;
        v = b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
        return true;
    };
    char magic[4];
    uint32_t hsize, version, ntoc;
    if (!in.read(magic, 4) || memcmp(magic, "Xcur", 4) || !u32(hsize) || !u32(version) || !u32(ntoc) || ntoc > 1000) return p;
    in.seekg(hsize);
    uint32_t best_pos = 0, best_size = 0;
    for (uint32_t i = 0; i < ntoc; i++) {
        uint32_t type, sub, pos;
        if (!u32(type) || !u32(sub) || !u32(pos)) return p;
        if (type != 0xfffd0002) continue;   // an image
        if (!best_pos || std::abs((int)sub - want) < std::abs((int)best_size - want)) { best_pos = pos; best_size = sub; }
    }
    if (!best_pos) return p;
    in.seekg(best_pos);
    uint32_t h[9];
    for (auto &v : h) if (!u32(v)) return p;
    uint32_t w = h[4], ht = h[5];
    if (!w || !ht || w > 512 || ht > 512) return p;
    p.w = (int)w;
    p.h = (int)ht;
    p.rgba.resize(w * ht * 4);
    for (uint32_t i = 0; i < w * ht; i++) {
        uint32_t px;
        if (!u32(px)) { p.w = p.h = 0; return p; }
        uint8_t a = px >> 24, r = px >> 16, g = px >> 8, b = px;
        // un-premultiplied for FLTK's alpha blending
        if (a && a < 255) { r = std::min(255, r * 255 / a); g = std::min(255, g * 255 / a); b = std::min(255, b * 255 / a); }
        p.rgba[i * 4] = r; p.rgba[i * 4 + 1] = g; p.rgba[i * 4 + 2] = b; p.rgba[i * 4 + 3] = a;
    }
    return p;
}

std::string system_theme() {
    if (const char *t = getenv("XCURSOR_THEME")) if (*t) return t;
    return "default";
}
}  // namespace

std::string PointerPrefs::in_use_file() { return env_dir() + "/pointer.prefs"; }

PointerPrefs PointerPrefs::load() {
    PointerPrefs p;
    std::ifstream in(in_use_file());
    if (!in) in.open(saved_file());
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "theme") p.theme = v;
        else if (k == "size") p.size = std::clamp(atoi(v.c_str()), 0, 256);
    }
    return p;
}

void PointerPrefs::save(bool archive) const {
    std::string text = "theme=" + theme + "\nsize=" + std::to_string(size) + "\n";
    std::vector<std::string> files = {in_use_file()};
    if (archive) files.push_back(saved_file());
    for (auto &f : files) {
        std::error_code ec;
        fs::create_directories(fs::path(f).parent_path(), ec);
        std::ofstream(f) << text;
    }
}

void PointerPrefs::apply() const {
    if (theme.empty() && !size) return;
    // the X resources (what programs read when they open the display)
    std::string res;
    if (!theme.empty()) res += "Xcursor.theme: " + theme + "\n";
    if (size) res += "Xcursor.size: " + std::to_string(size) + "\n";
    if (FILE *p = popen("xrdb -merge 2>/dev/null", "w")) { fputs(res.c_str(), p); pclose(p); }
    // the environment of the programs started from now on
    if (!theme.empty()) setenv("XCURSOR_THEME", theme.c_str(), 1);
    if (size) setenv("XCURSOR_SIZE", std::to_string(size).c_str(), 1);
    // the root window's pointer at once
    launch({"xsetroot", "-cursor_name", "left_ptr"}, "/");
}

static Window *g_pointer_editor = nullptr;

void edit_pointer_prefs(bool standalone) {
    if (g_pointer_editor) { g_pointer_editor->show(); return; }
    PointerPrefs saved = PointerPrefs::load();
    auto themes = std::make_shared<std::vector<std::string>>(cursor_themes());
    // the pictures shown: the Normal and the Busy pointer of the chosen theme and size
    struct Shown { Picture normal, busy; };
    auto shown = std::make_shared<Shown>();

    auto *root = VGroup();
    auto *top = HGroup();
    auto *tg = new Group(Group::Vertical, Frame_Group);
    tg->frame_title("Pointer Theme");
    auto *list = new List();
    list->frame(Frame_InputList);
    list->add({tr("System default")});
    for (auto &t : *themes) list->add({t});
    list->done();
    tg->end();
    auto *right = VGroup();
    auto *pg = new Group(Group::Horizontal, Frame_Group);
    pg->frame_title("Preview");
    auto draw_pic = [](const Picture &pic, const std::string &label, int x, int y, int w, int h) {
        fl_color(pens()[MPEN_HALFSHINE]);
        fl_rectf(x, y, w, h);
        if (pic.w > 0) {
            Fl_RGB_Image img(pic.rgba.data(), pic.w, pic.h, 4);
            int iw = std::min(pic.w, w), ih = std::min(pic.h, h - 18);
            if (iw != pic.w || ih != pic.h) {
                Fl_Image *s = img.copy(iw, ih);
                s->draw(x + (w - iw) / 2, y + (h - 18 - ih) / 2);
                delete s;
            } else img.draw(x + (w - iw) / 2, y + (h - 18 - ih) / 2);
        }
        fl_color(pens()[MPEN_TEXT]);
        fl_font(prefs().font, prefs().font_size);
        fl_draw(tr(label).c_str(), x, y + h - 18, w, 18, FL_ALIGN_CENTER);
    };
    auto *normal = new ImageButton(130, 120, [shown, draw_pic](int x, int y, int w, int h, bool) {
        draw_pic(shown->normal, "Normal Pointer", x, y, w, h);
    });
    auto *busy = new ImageButton(130, 120, [shown, draw_pic](int x, int y, int w, int h, bool) {
        draw_pic(shown->busy, "Busy Pointer", x, y, w, h);
    });
    normal->frame(Frame_ImageButton);
    busy->frame(Frame_ImageButton);
    pg->end();
    auto *sg = new Group(Group::Vertical, Frame_Group);
    sg->frame_title("Size");
    sg->columns(2);
    new Text("\033rSize:");
    int si = 0;
    for (int i = 0; i < 6; i++) if (SIZES[i] == saved.size) si = i;
    auto *size = new Cycle({tr("System default"), "24", "32", "48", "64", "96"}, si);
    sg->end();
    auto *note = new Text("\033c" + tr("Programs started from now on\nuse the new pointer."));
    (void)note;
    HVSpace();
    right->end();
    top->end();
    auto *btns = HGroup();
    btns->same_width(true);
    auto *save = new Button("Save");
    HSpace();
    auto *use = new Button("Use");
    HSpace();
    auto *cancel = new Button("Cancel");
    btns->end();
    root->end();

    auto show = [=] {
        int i = list->active();
        std::string theme = i > 0 ? (*themes)[i - 1] : system_theme();
        int sz = SIZES[size->active()] ? SIZES[size->active()] : 32;
        shown->normal = read_xcursor(cursor_file(theme, {"left_ptr", "default", "arrow"}), sz);
        shown->busy = read_xcursor(cursor_file(theme, {"watch", "wait", "left_ptr_watch"}), sz);
        normal->redraw();
        busy->redraw();
    };
    list->on_active = [=](int) { show(); };
    size->on_change = [=](int) { show(); };
    int ti = 0;
    for (size_t i = 0; i < themes->size(); i++) if ((*themes)[i] == saved.theme) ti = (int)i + 1;
    list->active(ti);
    show();

    auto collect = [=] {
        PointerPrefs p;
        int i = list->active();
        p.theme = i > 0 ? (*themes)[i - 1] : "";
        p.size = SIZES[size->active()];
        return p;
    };
    auto *w = new Window("Pointer Preferences", root);
    w->id("POINTERPREFS");
    g_pointer_editor = w;
    auto close = [w, standalone] {
        w->hide();
        Fl::delete_widget(w);
        g_pointer_editor = nullptr;
        if (standalone) exit(0);
    };
    save->on_press = [=] { auto p = collect(); p.save(true); p.apply(); close(); };
    use->on_press = [=] { auto p = collect(); p.save(false); p.apply(); close(); };
    cancel->on_press = close;
    w->callback([](Fl_Widget *, void *d) { (*(std::function<void()> *)d)(); }, new std::function<void()>(close));
    int sx, sy, swd, sh;
    Fl::screen_work_area(sx, sy, swd, sh);
    w->position(sx + (swd - w->w()) / 2, sy + (sh - w->h()) / 3);
    w->show();
}

}  // namespace rambler
