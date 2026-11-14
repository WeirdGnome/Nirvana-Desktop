/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The icon images (icons.h): original pictures drawn with FLTK.
*/
#include "icons.h"
#include "zune.h"

#include <FL/fl_draw.H>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace wanderer {

namespace {

bool g_sel = false;

// a colour; the selected image is darker
Fl_Color C(unsigned rgb) {
    Fl_Color c = fl_rgb_color((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    return g_sel ? fl_color_average(c, FL_BLACK, 0.55f) : c;
}

void rect(int x, int y, int w, int h, unsigned fill) {
    fl_color(C(fill));
    fl_rectf(x, y, w, h);
    fl_color(C(0x000000));
    fl_rect(x, y, w, h);
}

// a bevelled box: light top / left, dark bottom / right, a black outline
void box3d(int x, int y, int w, int h, unsigned face, unsigned light, unsigned dark) {
    fl_color(C(face));
    fl_rectf(x, y, w, h);
    fl_color(C(light));
    fl_xyline(x + 1, y + 1, x + w - 2);
    fl_yxline(x + 1, y + 1, y + h - 2);
    fl_color(C(dark));
    fl_xyline(x + 1, y + h - 2, x + w - 2);
    fl_yxline(x + w - 2, y + 1, y + h - 2);
    fl_color(C(0x000000));
    fl_rect(x, y, w, h);
}

void poly(unsigned fill, std::initializer_list<int> pts) {
    std::vector<int> p(pts);
    fl_color(C(fill));
    fl_begin_complex_polygon();
    for (size_t i = 0; i + 1 < p.size(); i += 2) fl_vertex(p[i], p[i + 1]);
    fl_end_complex_polygon();
    fl_color(C(0x000000));
    fl_begin_loop();
    for (size_t i = 0; i + 1 < p.size(); i += 2) fl_vertex(p[i], p[i + 1]);
    fl_end_loop();
}

// ---------------------------------------------------------------------------------------------
void drawer(int x, int y, bool open) {
    // a cabinet seen a little from above: the top, the front with a drawer and a handle
    poly(0x8FA8CF, {x + 8, y + 6, x + 42, y + 6, x + 46, y + 12, x + 4, y + 12});          // top
    box3d(x + 4, y + 12, 42, 28, 0x6688BB, 0xA9C0E4, 0x3C5A8A);                           // body
    if (open) {
        // the drawer pulled out, papers in it
        poly(0xD8D8D8, {x + 9, y + 15, x + 41, y + 15, x + 44, y + 22, x + 6, y + 22});
        rect(x + 12, y + 9, 12, 9, 0xFFFFFF);
        rect(x + 22, y + 7, 12, 11, 0xFFFFF0);
        box3d(x + 6, y + 22, 38, 14, 0x7C97C6, 0xB8CCEC, 0x40608F);
    } else box3d(x + 8, y + 17, 34, 18, 0x7C97C6, 0xB8CCEC, 0x40608F);
    // the handle
    int hy = open ? y + 27 : y + 24;
    box3d(x + 19, hy, 12, 4, 0xD9D9D9, 0xFFFFFF, 0x777777);
}

void harddisk(int x, int y, unsigned light_colour) {
    poly(0xC8C8C8, {x + 6, y + 10, x + 44, y + 10, x + 47, y + 16, x + 3, y + 16});       // top
    box3d(x + 3, y + 16, 44, 20, 0xAAAAAA, 0xE6E6E6, 0x5E5E5E);                          // front
    // vents and a light
    fl_color(C(0x404040));
    for (int i = 0; i < 6; i++) fl_yxline(x + 8 + i * 3, y + 20, y + 31);
    rect(x + 36, y + 29, 6, 3, light_colour);
    fl_color(C(0x5E5E5E));
    fl_xyline(x + 28, y + 22, x + 42);
    fl_xyline(x + 28, y + 25, x + 42);
}

void removable(int x, int y) {
    // a memory stick: a body and a plug
    poly(0x6688BB, {x + 6, y + 30, x + 30, y + 6, x + 40, y + 16, x + 16, y + 40});
    poly(0xD0D0D0, {x + 30, y + 6, x + 36, y + 0, x + 46, y + 10, x + 40, y + 16});
    fl_color(C(0x000000));
    fl_rectf(x + 37, y + 5, 3, 3);
    fl_rectf(x + 41, y + 9, 3, 3);
    rect(x + 16, y + 24, 6, 4, 0xEE4444);
}

void optical(int x, int y) {
    // a disc: a ring with a light reflection and a hole
    int cx = x + 24, cy = y + 21, r = 19;
    fl_color(C(0xDADADA));
    fl_pie(cx - r, cy - r, 2 * r, 2 * r, 0, 360);
    fl_color(C(0xB9C8E8));
    fl_pie(cx - r + 4, cy - r + 4, 2 * r - 8, 2 * r - 8, 20, 70);
    fl_color(C(0xE8D0F0));
    fl_pie(cx - r + 4, cy - r + 4, 2 * r - 8, 2 * r - 8, 200, 250);
    fl_color(C(0x000000));
    fl_arc(cx - r, cy - r, 2 * r, 2 * r, 0, 360);
    fl_color(C(0xAAAAAA));
    fl_pie(cx - 6, cy - 6, 12, 12, 0, 360);
    fl_color(C(0x000000));
    fl_arc(cx - 6, cy - 6, 12, 12, 0, 360);
    fl_color(zune::pens()[zune::MPEN_BACKGROUND]);
    fl_pie(cx - 2, cy - 2, 5, 5, 0, 360);
}

void ramdisk(int x, int y) {
    // a memory chip on a board
    box3d(x + 4, y + 10, 40, 22, 0x3B8A4A, 0x6CC07B, 0x1F4F28);
    box3d(x + 12, y + 14, 24, 14, 0x303030, 0x606060, 0x101010);
    fl_color(C(0xD8B040));
    for (int i = 0; i < 9; i++) fl_rectf(x + 7 + i * 4, y + 32, 2, 5);
    fl_color(C(0xFFFFFF));
    fl_font(FL_HELVETICA_BOLD, 8);
    fl_draw("RAM", x + 15, y + 24);
}

void trash(int x, int y) {
    poly(0x9AA5A9, {x + 10, y + 12, x + 38, y + 12, x + 35, y + 40, x + 13, y + 40});    // can
    box3d(x + 7, y + 7, 34, 6, 0xB8C2C6, 0xE4EAEC, 0x5A6468);                           // lid
    box3d(x + 19, y + 3, 10, 4, 0xB8C2C6, 0xE4EAEC, 0x5A6468);                          // knob
    fl_color(C(0x5A6468));
    for (int i = 0; i < 4; i++) fl_line(x + 16 + i * 5, y + 16, x + 17 + i * 4, y + 37);
}

// a page with a folded corner; the content is drawn on it by the caller
void page(int x, int y, unsigned paper) {
    int px = x + 9, pw = 30, ph = 40;
    poly(paper, {px, y, px + pw - 9, y, px + pw, y + 9, px + pw, y + ph, px, y + ph});
    poly(0xC8C8C8, {px + pw - 9, y, px + pw, y + 9, px + pw - 9, y + 9});
}

void lines(int x, int y, int n, unsigned colour) {
    fl_color(C(colour));
    for (int i = 0; i < n; i++) fl_rectf(x + 13, y + 14 + i * 5, i % 3 == 2 ? 14 : 22, 2);
}

void project(IconKind k, int x, int y) {
    switch (k) {
    case IconKind::Text: page(x, y, 0xFFFFFF); lines(x, y, 5, 0x6688BB); break;
    case IconKind::Document:
        page(x, y, 0xFFFFFF);
        rect(x + 13, y + 12, 10, 8, 0x6688BB);
        lines(x, y + 10, 3, 0x404040);
        break;
    case IconKind::Picture:
        page(x, y, 0xFFFFFF);
        rect(x + 12, y + 13, 24, 20, 0x9CC8F0);
        poly(0x3B8A4A, {x + 12, y + 33, x + 20, y + 21, x + 26, y + 28, x + 30, y + 24, x + 36, y + 33});
        fl_color(C(0xF0D040));
        fl_pie(x + 27, y + 15, 6, 6, 0, 360);
        break;
    case IconKind::Sound: {
        page(x, y, 0xFFFFFF);
        fl_color(C(0x000000));
        fl_pie(x + 14, y + 27, 7, 6, 0, 360);
        fl_pie(x + 26, y + 25, 7, 6, 0, 360);
        fl_rectf(x + 20, y + 13, 2, 17);
        fl_rectf(x + 32, y + 11, 2, 17);
        fl_rectf(x + 20, y + 11, 14, 3);
        break;
    }
    case IconKind::Movie:
        page(x, y, 0xFFFFFF);
        rect(x + 12, y + 14, 24, 18, 0x303030);
        fl_color(C(0xFFFFFF));
        for (int i = 0; i < 5; i++) { fl_rectf(x + 13 + i * 5, y + 15, 2, 2); fl_rectf(x + 13 + i * 5, y + 29, 2, 2); }
        poly(0xF0D040, {x + 21, y + 19, x + 28, y + 23, x + 21, y + 27});
        break;
    case IconKind::Archive:
        box3d(x + 6, y + 10, 36, 28, 0xC89A5A, 0xEAC48C, 0x7A5A2A);
        poly(0xDDB070, {x + 6, y + 10, x + 12, y + 3, x + 36, y + 3, x + 42, y + 10});
        fl_color(C(0x404040));
        for (int i = 0; i < 6; i++) fl_rectf(i % 2 ? x + 23 : x + 21, y + 12 + i * 4, 4, 2);
        break;
    case IconKind::Tool: {
        // a tool: a hammer over a wrench
        poly(0xB0B0B0, {x + 8, y + 34, x + 30, y + 12, x + 34, y + 16, x + 12, y + 38});
        fl_color(C(0xB0B0B0));
        fl_pie(x + 28, y + 4, 14, 14, 0, 360);
        fl_color(zune::pens()[zune::MPEN_BACKGROUND]);
        fl_pie(x + 33, y + 4, 8, 8, 0, 360);
        fl_color(C(0x000000));
        fl_arc(x + 28, y + 4, 14, 14, 0, 360);
        poly(0x9A6A3A, {x + 14, y + 10, x + 18, y + 6, x + 40, y + 34, x + 36, y + 38});
        box3d(x + 6, y + 4, 16, 9, 0x707070, 0xB0B0B0, 0x303030);
        break;
    }
    case IconKind::Script:
        page(x, y, 0xFFFFF0);
        fl_color(C(0x3B8A4A));
        fl_font(FL_COURIER_BOLD, 10);
        fl_draw(">_", x + 13, y + 22);
        lines(x, y + 10, 3, 0x404040);
        break;
    case IconKind::Link:
        page(x, y, 0xFFFFFF);
        poly(0x6688BB, {x + 14, y + 30, x + 24, y + 20, x + 21, y + 17, x + 31, y + 15, x + 29, y + 25, x + 26, y + 22, x + 16, y + 32});
        break;
    default: page(x, y, 0xFFFFFF); break;
    }
}

bool ends(const std::string &s, const char *suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, suffix) == 0;
}

}  // namespace

IconKind icon_kind(const std::string &path, bool is_dir, bool is_volume, bool executable) {
    std::string name = path.substr(path.find_last_of('/') + 1);
    if (is_volume) {
        if (path == "/" || (getenv("HOME") && path == getenv("HOME"))) return IconKind::Harddisk;
        if (path.find("cdrom") != std::string::npos || path.find("dvd") != std::string::npos) return IconKind::Optical;
        if (path.rfind("/dev/shm", 0) == 0 || path.find("ram") != std::string::npos) return IconKind::RamDisk;
        return IconKind::Removable;
    }
    if (is_dir) {
        std::string l = name;
        for (auto &c : l) c = (char)tolower(c);
        if (l == "trash" || l == ".trash" || l == "kosz") return IconKind::Trash;
        return IconKind::Drawer;
    }
    static const char *text[] = {".txt", ".md", ".log", ".conf", ".cfg", ".ini", ".c", ".h", ".cpp", ".cxx", ".py", ".json", ".xml"};
    static const char *pics[] = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".svg", ".webp", ".ilbm", ".iff", ".tif", ".tiff"};
    static const char *snd[] = {".mp3", ".ogg", ".flac", ".wav", ".mod", ".opus", ".m4a", ".aiff"};
    static const char *mov[] = {".mp4", ".mkv", ".avi", ".webm", ".mov", ".mpg"};
    static const char *arc[] = {".zip", ".tar", ".gz", ".xz", ".bz2", ".lha", ".lzh", ".7z", ".rar", ".deb", ".zst"};
    static const char *doc[] = {".pdf", ".odt", ".doc", ".docx", ".rtf", ".html", ".htm", ".epub"};
    for (auto *e : pics) if (ends(name, e)) return IconKind::Picture;
    for (auto *e : snd) if (ends(name, e)) return IconKind::Sound;
    for (auto *e : mov) if (ends(name, e)) return IconKind::Movie;
    for (auto *e : arc) if (ends(name, e)) return IconKind::Archive;
    for (auto *e : doc) if (ends(name, e)) return IconKind::Document;
    if (ends(name, ".sh")) return IconKind::Script;
    for (auto *e : text) if (ends(name, e)) return IconKind::Text;
    if (executable) return IconKind::Tool;
    return IconKind::Project;
}

void draw_icon(IconKind k, int x, int y, bool selected) {
    g_sel = selected;
    switch (k) {
    case IconKind::Drawer: drawer(x, y, false); break;
    case IconKind::OpenDrawer: drawer(x, y, true); break;
    case IconKind::Harddisk: harddisk(x, y, 0x40D040); break;
    case IconKind::Removable: removable(x, y); break;
    case IconKind::Optical: optical(x, y); break;
    case IconKind::RamDisk: ramdisk(x, y); break;
    case IconKind::Trash: trash(x, y); break;
    default: project(k, x, y); break;
    }
    g_sel = false;
}

}  // namespace wanderer
