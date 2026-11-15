/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Icon files: ported from icon.library (diskobjio.c - the classic DiskObject and its
    images; diskobj35io.c - Decode35 and the FORM ICON chunks; diskobjPNGio.c - the PNG
    icon and its attached second image; support.c / changetoselectediconcolor.c - the
    selected image made from the normal one). See diskobject.h.
*/
#include "diskobject.h"
#include "lune.h"

#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Shared_Image.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/Fl_RGB_Image.H>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef RAMBLER_HAVE_ZLIB
#include <zlib.h>
#endif

namespace rambler {

namespace {

// a big-endian reader over the file's bytes
struct Reader {
    const std::vector<unsigned char> &d;
    size_t pos = 0;
    bool ok = true;
    bool need(size_t n) { if (pos + n > d.size()) ok = false; return ok; }
    unsigned u8() { return need(1) ? d[pos++] : 0; }
    unsigned u16() { if (!need(2)) return 0; unsigned v = d[pos] << 8 | d[pos + 1]; pos += 2; return v; }
    unsigned u32() {
        if (!need(4)) return 0;
        unsigned v = (unsigned)d[pos] << 24 | d[pos + 1] << 16 | d[pos + 2] << 8 | d[pos + 3];
        pos += 4;
        return v;
    }
    void skip(size_t n) { if (need(n)) pos += n; }
};

// the screen's pens the classic icons are drawn in: 0 background, 1 shadow (black), 2 shine
// (white), 3 fill; 4 - 7 the usual eight-colour additions; further ones a grey ramp
unsigned classic_pen(int i) {
    const auto &p = lune::prefs();
    static const unsigned EXTRA[] = {0x999999, 0xBBBBBB, 0xBBAA99, 0xFFBBAA};
    switch (i) {
    case 0: return p.background;
    case 1: return p.shadow;
    case 2: return p.shine;
    case 3: return p.fill;
    default:
        if (i < 8) return EXTRA[i - 4];
        unsigned g = (unsigned)(i * 255 / 255) & 255;
        return g << 16 | g << 8 | g;
    }
}

// struct Image (20 bytes) and its planes -> pen numbers; PlanePick / PlaneOnOff as
// graphics.library's DrawImage applies them
bool read_planar(Reader &r, int &w, int &h, std::vector<unsigned char> &pens) {
    r.u16(); r.u16();                    // LeftEdge, TopEdge
    w = (short)r.u16();
    h = (short)r.u16();
    int depth = (short)r.u16();
    r.u32();                             // ImageData (a flag)
    unsigned pick = r.u8(), onoff = r.u8();
    r.u32();                             // NextImage
    if (!r.ok || w <= 0 || h <= 0 || w > 1024 || h > 1024 || depth < 0 || depth > 8) return false;
    int words = (w + 15) >> 4, plane_bytes = words * 2 * h;
    pens.assign((size_t)w * h, 0);
    int stored = 0;
    for (int bit = 0; bit < 8; bit++) {
        if (stored < depth && (pick & (1u << bit))) {
            if (!r.need((size_t)plane_bytes)) return false;
            const unsigned char *pl = &r.d[r.pos];
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    if (pl[y * words * 2 + (x >> 3)] & (0x80 >> (x & 7))) pens[y * w + x] |= 1u << bit;
            r.pos += plane_bytes;
            stored++;
        } else if (onoff & (1u << bit))
            for (auto &p : pens) p |= 1u << bit;
    }
    // planes stored beyond PlanePick's (a wrong mask): read past them
    if (stored < depth) r.skip((size_t)(depth - stored) * plane_bytes);
    return r.ok;
}

std::vector<unsigned char> pens_to_rgba(const std::vector<unsigned char> &pens, bool complement, int depth_mask) {
    std::vector<unsigned char> out(pens.size() * 4);
    for (size_t i = 0; i < pens.size(); i++) {
        int pen = complement ? (pens[i] ^ depth_mask) : pens[i];
        unsigned c = classic_pen(pen);
        out[i * 4] = c >> 16;
        out[i * 4 + 1] = c >> 8;
        out[i * 4 + 2] = c;
        out[i * 4 + 3] = pen == 0 && !complement ? 0 : 255;   // pen 0 is the background: see-through
    }
    return out;
}

// Decode35: the RLE of the colour icons - an 8-bit control (0..127: that many + 1 values
// follow, 129..255: the next value 257 - n times), the values `bits` wide, in a bit stream
bool decode35(const unsigned char *buf, long numbytes, unsigned char *out, int bits, long entries) {
    int cop = 0, loop = 0, numbits = 0, mask = (1 << bits) - 1;
    long cur = 0;
    unsigned bitbuf = 0;
    while ((numbytes || (cop && numbits >= bits)) && cur < entries) {
        if (!cop) {
            if (numbits < 8) { bitbuf = (bitbuf << 8) | *buf++; --numbytes; numbits += 8; }
            int byte = (bitbuf >> (numbits - 8)) & 0xFF;
            numbits -= 8;
            if (byte <= 127) cop = byte + 1;
            else if (byte > 128) loop = 256 - byte + 1;
            else continue;
        }
        if (cop) ++loop;
        if (numbits < bits) {
            if (!numbytes) break;
            bitbuf = (bitbuf << 8) | *buf++; --numbytes; numbits += 8;
        }
        int v = (bitbuf >> (numbits - bits)) & mask;
        while (loop && cur < entries) { *out++ = v; ++cur; --loop; }
        if (cop) --cop;
        numbits -= bits;
    }
    return cur == entries;
}

// the FORM ICON after the classic part (ReadIcon35)
bool read_icon35(const std::vector<unsigned char> &d, size_t start, DiskObject &o) {
    Reader r{d, start};
    if (r.u32() != 0x464F524D) return false;   // FORM
    size_t len = r.u32();   // (read before r.pos is used: the end is after the length)
    size_t end = std::min(d.size(), r.pos + len);
    if (r.u32() != 0x49434F4E) return false;   // ICON
    int fw = 0, fh = 0;
    bool face = false;
    std::vector<unsigned char> imgs[2];
    std::vector<unsigned char> pal[2];
    int ncolors[2] = {0, 0}, transp[2] = {-1, -1}, nimag = 0, nargb = 0;
    std::vector<unsigned char> argb[2];
    while (r.ok && r.pos + 8 <= end) {
        unsigned id = r.u32(), size = r.u32();
        size_t next = r.pos + size + (size & 1);
        if (r.pos + size > end) break;
        if (id == 0x46414345 && size >= 6) {   // FACE
            fw = r.u8() + 1;
            fh = r.u8() + 1;
            face = true;
        } else if (id == 0x494D4147 && face && size >= 10 && nimag < 2) {   // IMAG
            int n = nimag;
            int tc = r.u8(), nc = r.u8() + 1, flags = r.u8(), ifmt = r.u8(), pfmt = r.u8(), depth = r.u8();
            long isize = r.u16() + 1, psize = r.u16() + 1;
            if (!(flags & 2)) psize = 0;
            if (!r.need((size_t)isize + psize) || depth < 1 || depth > 8) break;
            imgs[n].assign((size_t)fw * fh, 0);
            const unsigned char *src = &d[r.pos];
            if (ifmt == 0) memcpy(imgs[n].data(), src, std::min<size_t>((size_t)isize, imgs[n].size()));
            else decode35(src, isize, imgs[n].data(), depth, (long)fw * fh);
            if (flags & 2) {
                pal[n].assign((size_t)nc * 3, 0);
                if (pfmt == 0) memcpy(pal[n].data(), src + isize, std::min<size_t>((size_t)psize, pal[n].size()));
                else decode35(src + isize, psize, pal[n].data(), 8, (long)nc * 3);
            }
            ncolors[n] = nc;
            transp[n] = (flags & 1) ? tc : -1;
            nimag++;
        } else if (id == 0x41524742 && face && size > 10 && nargb < 2) {   // ARGB
#ifdef RAMBLER_HAVE_ZLIB
            const unsigned char *src = &d[r.pos];
            uLong zsize = (uLong)src[4] << 24 | src[5] << 16 | src[6] << 8 | src[7];
            uLongf len = (uLongf)fw * fh * 4;
            std::vector<unsigned char> out(len);
            if (zsize <= size - 10 && uncompress(out.data(), &len, src + 10, zsize) == Z_OK && len == out.size())
                argb[nargb++] = std::move(out);
#endif
        }
        r.pos = next;
    }
    if (!face || (nimag == 0 && nargb == 0)) return false;
    o.width = fw;
    o.height = fh;
    auto to_rgba = [&](int n, int paln) {
        std::vector<unsigned char> out((size_t)fw * fh * 4, 0);
        const auto &p = pal[paln];
        for (size_t i = 0; i < imgs[n].size(); i++) {
            int c = imgs[n][i];
            if (c == transp[n] || c >= ncolors[paln] || (size_t)c * 3 + 2 >= p.size()) continue;
            out[i * 4] = p[c * 3];
            out[i * 4 + 1] = p[c * 3 + 1];
            out[i * 4 + 2] = p[c * 3 + 2];
            out[i * 4 + 3] = 255;
        }
        return out;
    };
    auto argb_to_rgba = [&](const std::vector<unsigned char> &a) {
        std::vector<unsigned char> out(a.size());
        for (size_t i = 0; i + 3 < a.size(); i += 4) {
            out[i] = a[i + 1]; out[i + 1] = a[i + 2]; out[i + 2] = a[i + 3]; out[i + 3] = a[i];
        }
        return out;
    };
    if (nargb) {
        o.normal = argb_to_rgba(argb[0]);
        if (nargb > 1) { o.selected = argb_to_rgba(argb[1]); o.dual = true; }
    } else {
        if (pal[0].empty()) return false;
        o.normal = to_rgba(0, 0);
        if (nimag > 1) {
            if (pal[1].empty()) { pal[1] = pal[0]; ncolors[1] = ncolors[0]; }
            o.selected = to_rgba(1, 1);
            o.dual = true;
        }
    }
    return true;
}

// a PNG icon: the image, and a second PNG after IEND (diskobjPNGio.c)
// The icOn chunk of a PNG icon (diskobjPNGio.c): attributes, each a 32-bit tag and a
// 32-bit value, or a string ending with NUL for the default tool and the tool types
const unsigned ATTR_ICONX = 0x80001001, ATTR_ICONY = 0x80001002, ATTR_DEFAULTTOOL = 0x8000100a,
               ATTR_TOOLTYPE = 0x8000100b, ATTR_TYPE = 0x8000100f;

void read_icon_chunk(const unsigned char *p, size_t n, DiskObject &o) {
    auto be = [](const unsigned char *q) { return (unsigned)q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3]; };
    size_t i = 0;
    while (i + 4 <= n) {
        unsigned attr = be(p + i);
        i += 4;
        if (attr == ATTR_DEFAULTTOOL || attr == ATTR_TOOLTYPE) {
            size_t e = i;
            while (e < n && p[e]) e++;
            std::string s((const char *)p + i, e - i);
            i = e + 1;
            if (attr == ATTR_DEFAULTTOOL) o.default_tool = s; else o.tool_types.push_back(s);
            continue;
        }
        if (attr < 0x80001001 || attr > 0x80001107 || i + 4 > n) break;   // (unknown: no telling its size)
        unsigned v = be(p + i);
        i += 4;
        if (attr == ATTR_ICONX) o.current_x = (int)v;
        else if (attr == ATTR_ICONY) o.current_y = (int)v;
        else if (attr == ATTR_TYPE) o.type = (int)v;
    }
}

bool read_png_icon(const std::vector<unsigned char> &d, DiskObject &o) {
    size_t pos = 8, first_end = 0;
    while (pos + 8 <= d.size()) {
        unsigned size = (unsigned)d[pos] << 24 | d[pos + 1] << 16 | d[pos + 2] << 8 | d[pos + 3];
        if (!memcmp(&d[pos + 4], "icOn", 4) && pos + 8 + size <= d.size()) read_icon_chunk(&d[pos + 8], size, o);
        bool iend = !memcmp(&d[pos + 4], "IEND", 4);
        pos += (size_t)size + 12;
        if (iend) { first_end = pos; break; }
    }
    if (!first_end) return false;
    auto decode = [](const unsigned char *p, size_t n, int &w, int &h) {
        std::vector<unsigned char> out;
        Fl_PNG_Image img(nullptr, p, (int)n);
        if (img.fail() || img.w() <= 0 || img.h() <= 0) return out;
        w = img.w();
        h = img.h();
        int dpt = img.d(), ld = img.ld() ? img.ld() : w * dpt;
        const unsigned char *px = (const unsigned char *)img.data()[0];
        out.resize((size_t)w * h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const unsigned char *s = px + y * ld + x * dpt;
                unsigned char *t = &out[((size_t)y * w + x) * 4];
                if (dpt >= 3) { t[0] = s[0]; t[1] = s[1]; t[2] = s[2]; t[3] = dpt == 4 ? s[3] : 255; }
                else { t[0] = t[1] = t[2] = s[0]; t[3] = dpt == 2 ? s[1] : 255; }
            }
        return out;
    };
    o.normal = decode(d.data(), first_end, o.width, o.height);
    if (o.normal.empty()) return false;
    if (first_end + 8 < d.size() && !memcmp(&d[first_end + 1], "PNG", 3)) {
        int w2 = 0, h2 = 0;
        auto s = decode(&d[first_end], d.size() - first_end, w2, h2);
        if (w2 == o.width && h2 == o.height) { o.selected = std::move(s); o.dual = true; }
    }
    return true;
}

// NewIcons (diskobjNIio.c, DecodeNI): the images in the tool types after the line "*** DON'T
// EDIT THE FOLLOWING LINES!! ***" - "IM1=" lines the normal image, "IM2=" the selected one.
// A line's first five characters: transparency ('B' - colour 0 is see-through), width + 0x21,
// height + 0x21, the colours' number (two characters of six bits); then the palette and the
// pixels, seven bits a character (0x20..0x9F, 0xA1..0xD0; 0xD1.. a run of zeros).
struct NIDecoder {
    const std::vector<std::string> &tt;
    size_t line;      // the line being read
    size_t col;       // in it
    char which;       // '1' or '2'
    bool decode(std::vector<unsigned char> &out, int bits, long entries) {
        long numbits = 0, cur = 0, loop = 0;
        unsigned long bitbuf = 0;
        long mask = (1L << bits) - 1;
        while (cur < entries) {
            unsigned byte;
            if (loop) { byte = 0; --loop; }
            else {
                if (col >= tt[line].size()) {   // the next line of this image
                    if (++line >= tt.size()) return false;
                    const std::string &l = tt[line];
                    if (l.size() < 4 || l[0] != 'I' || l[1] != 'M' || l[2] != which || l[3] != '=') return false;
                    col = 4;
                    numbits = 0;
                    if (col >= l.size()) continue;
                }
                byte = (unsigned char)tt[line][col++];
                if (byte < 0xA0) byte -= 0x20;
                else if (byte < 0xD1) byte -= 0x51;
                else { loop = byte - 0xD1; byte = 0; }
            }
            bitbuf = (bitbuf << 7) + byte;
            numbits += 7;
            while (numbits >= bits && cur < entries) {
                out.push_back((bitbuf >> (numbits - bits)) & mask);
                numbits -= bits;
                cur++;
            }
        }
        return true;
    }
};

bool read_newicon(const std::vector<std::string> &tt, size_t start, char which, int &w, int &h,
                  std::vector<unsigned char> &rgba) {
    size_t i = start;
    while (i < tt.size() && tt[i].compare(0, 4, std::string("IM") + which + "=") != 0) i++;
    if (i >= tt.size() || tt[i].size() < 9) return false;
    const std::string &l = tt[i];
    int iw = (unsigned char)l[5] - 0x21, ih = (unsigned char)l[6] - 0x21;
    int ncol = (((unsigned char)l[7] - 0x21) << 6) + ((unsigned char)l[8] - 0x21);
    bool transp = l[4] == 'B';
    if (iw <= 0 || ih <= 0 || ncol <= 0 || ncol > 256) return false;
    if (which == '2' && (iw != w || ih != h)) return false;   // (the selected one: the same size)
    NIDecoder dec{tt, i, 9, which};
    std::vector<unsigned char> pal, px;
    if (!dec.decode(pal, 8, (long)ncol * 3)) return false;
    dec.col = tt[dec.line].size();   // (the pixels start on the next line)
    int bits = 1;
    while ((1 << bits) < ncol) bits++;
    if (!dec.decode(px, bits, (long)iw * ih)) return false;
    w = iw;
    h = ih;
    rgba.assign((size_t)iw * ih * 4, 0);
    for (size_t k = 0; k < px.size(); k++) {
        int c = px[k];
        if ((transp && c == 0) || c >= ncol) continue;
        rgba[k * 4] = pal[c * 3];
        rgba[k * 4 + 1] = pal[c * 3 + 1];
        rgba[k * 4 + 2] = pal[c * 3 + 2];
        rgba[k * 4 + 3] = 255;
    }
    return true;
}

// the classic DiskObject (IconDesc): its images, tool, tool types; then the colour icon
bool read_classic(const std::vector<unsigned char> &d, DiskObject &o) {
    Reader r{d};
    if (r.u16() != 0xE310) return false;   // WB_DISKMAGIC
    r.u16();                               // version
    // struct Gadget (44 bytes)
    r.u32(); r.u16(); r.u16(); r.u16(); r.u16();
    unsigned gflags = r.u16();
    r.u16(); r.u16();
    unsigned render = r.u32(), select = r.u32();
    r.u32(); r.u32(); r.u32(); r.u16();
    unsigned userdata = r.u32();
    o.type = r.u8();
    r.u8();
    unsigned deftool = r.u32(), tooltypes = r.u32();
    o.current_x = (int)r.u32();
    o.current_y = (int)r.u32();
    unsigned drawerdata = r.u32(), toolwindow = r.u32();
    r.u32();                               // stack size
    if (!r.ok) return false;
    if (drawerdata) r.skip(56);            // the old DrawerData: NewWindow + CurrentX/Y
    std::vector<unsigned char> pens0, pens1;
    int w = 0, h = 0, w1 = 0, h1 = 0;
    const unsigned GADGIMAGE = 0x0004, GADGHIGHBITS = 0x0003, GADGHIMAGE = 0x0002;
    bool have0 = false, have1 = false;
    if (render || (gflags & GADGIMAGE)) have0 = read_planar(r, w, h, pens0);
    if (select || (gflags & GADGHIGHBITS) == GADGHIMAGE) have1 = read_planar(r, w1, h1, pens1);
    auto read_string = [&r]() {
        unsigned len = r.u32();
        std::string s;
        if (len && len < 65536 && r.need(len)) { s.assign((const char *)&r.d[r.pos], len); r.pos += len; }
        while (!s.empty() && s.back() == 0) s.pop_back();
        return s;
    };
    if (deftool) o.default_tool = read_string();
    if (tooltypes) {
        unsigned count = r.u32();
        int n = (int)(count >> 2) - 1;
        for (int i = 0; i < n && r.ok; i++) o.tool_types.push_back(read_string());
    }
    if (toolwindow) read_string();
    unsigned rev = userdata & 0xff;
    if (drawerdata && rev > 0 && rev <= 1) r.skip(6);   // the new DrawerData: dd_Flags, dd_ViewModes
    // the colour icon after it, if any: it takes the place of the planar images
    if (r.ok && r.pos + 12 <= d.size() && read_icon35(d, r.pos, o)) return true;
    // NewIcons in the tool types (only without a colour icon, as ReadIconNI)
    for (size_t i = 0; i < o.tool_types.size(); i++)
        if (o.tool_types[i] == "*** DON'T EDIT THE FOLLOWING LINES!! ***") {
            int nw = 0, nh = 0;
            std::vector<unsigned char> n0, n1;
            if (read_newicon(o.tool_types, i + 1, '1', nw, nh, n0)) {
                o.width = nw;
                o.height = nh;
                o.normal = std::move(n0);
                if (read_newicon(o.tool_types, i + 1, '2', nw, nh, n1)) { o.selected = std::move(n1); o.dual = true; }
                o.tool_types.resize(i);   // (the image lines are no tool types)
                return true;
            }
            break;
        }
    if (!have0) return false;
    // (the depth's mask for the complement: the pens used)
    int maxpen = 0;
    for (auto p : pens0) maxpen = std::max(maxpen, (int)p);
    int mask = 1;
    while (mask < maxpen) mask = mask << 1 | 1;
    mask = std::max(mask, 3);
    o.width = w;
    o.height = h;
    o.normal = pens_to_rgba(pens0, false, mask);
    if (have1 && w1 == w && h1 == h) {
        o.selected = pens_to_rgba(pens1, false, mask);
        o.dual = true;
    } else if ((gflags & GADGHIGHBITS) <= 1) {   // GADGHCOMP / GADGHBOX (backfill): complemented
        o.selected = pens_to_rgba(pens0, true, mask);
    }
    return true;
}

// the selected image an icon with one image gets: the normal one, half way to white
void make_selected(DiskObject &o) {
    if (!o.selected.empty()) return;
    o.selected = o.normal;
    for (size_t i = 0; i + 3 < o.selected.size(); i += 4)
        for (int c = 0; c < 3; c++) o.selected[i + c] = (o.selected[i + c] >> 1) + 0x7F;
}
}  // namespace

Fl_RGB_Image *DiskObject::image(bool sel) const {
    int i = sel ? 1 : 0;
    if (!img_.p[i]) {
        const auto &px = sel ? selected : normal;
        if (px.empty() || width <= 0 || height <= 0) return nullptr;
        img_.p[i] = new Fl_RGB_Image(px.data(), width, height, 4);
    }
    return img_.p[i];
}

DiskObject::Images::~Images() {
    delete p[0];
    delete p[1];
}

std::shared_ptr<DiskObject> read_icon_file(const std::string &file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return nullptr;
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 8 || d.size() > (16u << 20)) return nullptr;
    auto o = std::make_shared<DiskObject>();
    bool ok = false;
    if (!memcmp(d.data(), "\x89PNG", 4)) ok = read_png_icon(d, *o);
    else if (d[0] == 0xE3 && d[1] == 0x10) ok = read_classic(d, *o);
    if (!ok || o->normal.empty()) return nullptr;
    make_selected(*o);
    return o;
}

std::shared_ptr<DiskObject> icon_from_picture(const std::string &picture, int max) {
    static bool registered = false;
    if (!registered) { fl_register_images(); registered = true; }
    Fl_Shared_Image *si = Fl_Shared_Image::get(picture.c_str());
    if (!si || si->w() <= 0 || si->h() <= 0) { if (si) si->release(); return nullptr; }
    int w = si->w(), h = si->h();
    if (w > max || h > max) {   // (the proportions kept)
        if (w >= h) { h = std::max(1, h * max / w); w = max; }
        else { w = std::max(1, w * max / h); h = max; }
    }
    Fl_Image *img = si->image() ? si->image()->copy(w, h) : nullptr;   // (an SVG is drawn at that size)
    si->release();
    if (auto *svg = dynamic_cast<Fl_SVG_Image *>(img)) svg->normalize();   // (drawn now, at that size)
    auto *rgb = dynamic_cast<Fl_RGB_Image *>(img);
    if (!rgb || !rgb->data() || !rgb->data()[0] || rgb->d() < 1) { delete img; return nullptr; }
    auto o = std::make_shared<DiskObject>();
    o->width = rgb->w();
    o->height = rgb->h();
    int dpt = rgb->d(), ld = rgb->ld() ? rgb->ld() : rgb->w() * dpt;
    const unsigned char *px = (const unsigned char *)rgb->data()[0];
    o->normal.resize((size_t)o->width * o->height * 4);
    for (int y = 0; y < o->height; y++)
        for (int x = 0; x < o->width; x++) {
            const unsigned char *s = px + y * ld + x * dpt;
            unsigned char *t = &o->normal[((size_t)y * o->width + x) * 4];
            if (dpt >= 3) { t[0] = s[0]; t[1] = s[1]; t[2] = s[2]; t[3] = dpt == 4 ? s[3] : 255; }
            else { t[0] = t[1] = t[2] = s[0]; t[3] = dpt == 2 ? s[1] : 255; }
        }
    delete img;
    o->type = 3;   // WBTOOL
    make_selected(*o);
    return o;
}

namespace {
unsigned crc32_of(const unsigned char *p, size_t n, unsigned crc = 0) {
    static unsigned table[256];
    static bool made = false;
    if (!made) {
        for (unsigned i = 0; i < 256; i++) {
            unsigned c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

std::vector<unsigned char> png_bytes(const std::vector<unsigned char> &rgba, int w, int h) {
    char tmp[] = "/tmp/rambler-icon-XXXXXX";
    int fd = mkstemp(tmp);
    std::vector<unsigned char> out;
    if (fd < 0) return out;
    close(fd);
    if (fl_write_png(tmp, rgba.data(), w, h, 4) == 0) {
        std::ifstream in(tmp, std::ios::binary);
        out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    unlink(tmp);
    return out;
}
}  // namespace

bool put_png_icon(const std::string &info_file, const DiskObject &o) {
    if (o.normal.empty() || o.width <= 0 || o.height <= 0) return false;
    std::vector<unsigned char> first = png_bytes(o.normal, o.width, o.height);
    // the selected image only for a dual icon (one with a single image gets its lighter
    // selected image when read, as before)
    std::vector<unsigned char> second;
    if (o.dual && !o.selected.empty()) second = png_bytes(o.selected, o.width, o.height);
    if (first.size() < 20 || (o.dual && second.empty())) return false;
    // the icOn chunk before the first image's IEND: the place, the type, the tool, the tool types
    std::vector<unsigned char> data;
    auto u32 = [&data](unsigned v) { for (int s = 24; s >= 0; s -= 8) data.push_back((v >> s) & 255); };
    if (o.current_x != (int)0x80000000 && o.current_y != (int)0x80000000) {
        u32(ATTR_ICONX); u32((unsigned)o.current_x);
        u32(ATTR_ICONY); u32((unsigned)o.current_y);
    }
    if (o.type) { u32(ATTR_TYPE); u32((unsigned)o.type); }
    if (!o.default_tool.empty()) { u32(ATTR_DEFAULTTOOL); data.insert(data.end(), o.default_tool.begin(), o.default_tool.end()); data.push_back(0); }
    for (auto &t : o.tool_types) { u32(ATTR_TOOLTYPE); data.insert(data.end(), t.begin(), t.end()); data.push_back(0); }
    std::vector<unsigned char> chunk;
    for (int s = 24; s >= 0; s -= 8) chunk.push_back((data.size() >> s) & 255);
    const unsigned char id[4] = {'i', 'c', 'O', 'n'};
    chunk.insert(chunk.end(), id, id + 4);
    chunk.insert(chunk.end(), data.begin(), data.end());
    unsigned crc = crc32_of(chunk.data() + 4, chunk.size() - 4);
    for (int s = 24; s >= 0; s -= 8) chunk.push_back((crc >> s) & 255);
    std::vector<unsigned char> file(first.begin(), first.end() - 12);   // (IEND is the last 12 bytes)
    if (!data.empty()) file.insert(file.end(), chunk.begin(), chunk.end());
    file.insert(file.end(), first.end() - 12, first.end());
    file.insert(file.end(), second.begin(), second.end());   // the selected image after it: a dual icon
    std::string tmp = info_file + ".new";
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write((const char *)file.data(), (std::streamsize)file.size());
        if (!out) { unlink(tmp.c_str()); return false; }
    }
    return rename(tmp.c_str(), info_file.c_str()) == 0;
}

// The tool types and the default tool put into a classic icon file (also one with NewIcons
// or a colour icon): the strings' part of the file is made again, the rest - the DiskObject,
// the drawer data, the images, the tool window, the new drawer data, the FORM ICON after
// it - is kept as it was, byte for byte; the NewIcons image lines stay after the new tool
// types (PutDiskObject writes them back the same way)
bool put_classic_strings(const std::string &info_file, const std::string *default_tool,
                         const std::vector<std::string> &tool_types) {
    std::ifstream in(info_file, std::ios::binary);
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    Reader r{d};
    if (d.size() < 78 || r.u16() != 0xE310) return false;
    r.pos = 16;
    unsigned gflags = r.u16();
    r.pos = 22;
    unsigned render = r.u32(), select = r.u32();
    r.pos = 50;
    unsigned deftool = r.u32(), tooltypes = r.u32();
    r.pos = 66;
    unsigned drawerdata = r.u32();
    r.pos = 78;
    if (drawerdata) r.skip(56);
    std::vector<unsigned char> pens;
    int w, h;
    const unsigned GADGIMAGE = 0x0004, GADGHIGHBITS = 0x0003, GADGHIMAGE = 0x0002;
    if ((render || (gflags & GADGIMAGE)) && !read_planar(r, w, h, pens)) return false;
    if ((select || (gflags & GADGHIGHBITS) == GADGHIMAGE) && !read_planar(r, w, h, pens)) return false;
    size_t start = r.pos;   // the strings begin
    auto read_string = [&r]() {
        unsigned len = r.u32();
        std::string s;
        if (len && len < 65536 && r.need(len)) { s.assign((const char *)&r.d[r.pos], len); r.pos += len; }
        while (!s.empty() && s.back() == 0) s.pop_back();
        return s;
    };
    std::string old_tool;
    std::vector<std::string> old_types;
    if (deftool) old_tool = read_string();
    if (tooltypes) {
        int n = (int)(r.u32() >> 2) - 1;
        for (int i = 0; i < n && r.ok; i++) old_types.push_back(read_string());
    }
    if (!r.ok) return false;
    size_t end = r.pos;     // the strings end: the tool window and the rest after it
    // the new strings: the NewIcons lines (the marker and after) kept after the tool types
    std::vector<std::string> types = tool_types;
    for (size_t i = 0; i < old_types.size(); i++)
        if (old_types[i] == "*** DON'T EDIT THE FOLLOWING LINES!! ***") {
            types.insert(types.end(), old_types.begin() + (long)i, old_types.end());
            break;
        }
    std::string tool = default_tool ? *default_tool : old_tool;
    std::vector<unsigned char> mid;
    auto u32 = [&mid](unsigned v) { for (int s = 24; s >= 0; s -= 8) mid.push_back((v >> s) & 255); };
    auto str = [&](const std::string &s) { u32((unsigned)s.size() + 1); mid.insert(mid.end(), s.begin(), s.end()); mid.push_back(0); };
    if (!tool.empty()) str(tool);
    if (!types.empty()) {
        u32((unsigned)(types.size() + 1) * 4);
        for (auto &t : types) str(t);
    }
    std::vector<unsigned char> out(d.begin(), d.begin() + (long)start);
    // do_DefaultTool, do_ToolTypes: "there is one" (a pointer in memory, only not 0 on disk)
    auto put32 = [&out](size_t at, unsigned v) { for (int i = 0; i < 4; i++) out[at + i] = (v >> (24 - 8 * i)) & 255; };
    put32(50, tool.empty() ? 0 : 1);
    put32(54, types.empty() ? 0 : 1);
    out.insert(out.end(), mid.begin(), mid.end());
    out.insert(out.end(), d.begin() + (long)end, d.end());
    std::string tmp = info_file + ".new";
    {
        std::ofstream o(tmp, std::ios::binary);
        o.write((const char *)out.data(), (std::streamsize)out.size());
        if (!o) { unlink(tmp.c_str()); return false; }
    }
    return rename(tmp.c_str(), info_file.c_str()) == 0;
}

bool put_icon_strings(const std::string &info_file, const std::string *default_tool,
                      const std::vector<std::string> &tool_types) {
    char magic[4] = {0};
    if (FILE *f = fopen(info_file.c_str(), "rb")) { (void)!fread(magic, 1, 4, f); fclose(f); }
    if (!memcmp(magic, "\x89PNG", 4)) {
        auto o = read_icon_file(info_file);
        if (!o) return false;
        DiskObject n = *o;
        n.tool_types = tool_types;
        if (default_tool) n.default_tool = *default_tool;
        return put_png_icon(info_file, n);
    }
    if ((unsigned char)magic[0] == 0xE3 && (unsigned char)magic[1] == 0x10)
        return put_classic_strings(info_file, default_tool, tool_types);
    return false;
}

std::shared_ptr<DiskObject> default_disk_object(const std::string &name) {
    // the directories, and the icon files in them by their names in lower case (looked at
    // again when a directory changes)
    struct Dir { std::string path; time_t mtime = -1; std::map<std::string, std::string> files; };
    static std::vector<Dir> dirs;
    if (dirs.empty()) {
        if (const char *r = getenv("XDG_RUNTIME_DIR"); r && *r) dirs.push_back({std::string(r) + "/rambler/icons"});
        const char *x = getenv("XDG_CONFIG_HOME");
        std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
        dirs.push_back({base + "/rambler/icons"});
        char exe[4096];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0) {
            exe[n] = 0;
            std::string d(exe);
            dirs.push_back({d.substr(0, d.rfind('/')) + "/../share/rambler/icons"});
        }
    }
    std::string want = name + ".info";
    for (auto &c : want) c = (char)tolower(c);
    // (the directories looked at once a second at most: this is asked for every icon drawn)
    static time_t looked = 0;
    time_t now = time(nullptr);
    bool look = now != looked;
    looked = now;
    for (auto &d : dirs) {
        struct stat st;
        if (look && stat(d.path.c_str(), &st) != 0) { d.files.clear(); d.mtime = -1; continue; }
        if (look && st.st_mtime != d.mtime) {
            d.mtime = st.st_mtime;
            d.files.clear();
            if (DIR *dp = opendir(d.path.c_str())) {
                while (dirent *de = readdir(dp)) {
                    std::string f = de->d_name, l = f;
                    for (auto &c : l) c = (char)tolower(c);
                    if (l.size() > 9 && l.rfind("def_", 0) == 0) d.files[l] = d.path + "/" + f;
                }
                closedir(dp);
            }
        }
        auto it = d.files.find(want);
        if (it == d.files.end()) continue;
        // (read through the icon file cache: kept while the file stays as it is)
        std::string p = it->second;
        if (auto o = get_disk_object(p.substr(0, p.size() - 5))) return o;
    }
    return nullptr;
}

std::shared_ptr<DiskObject> theme_icon(const std::string &name) {
    static std::map<std::string, std::shared_ptr<DiskObject>> found;
    auto it = found.find(name);
    if (it != found.end()) return it->second;
    std::shared_ptr<DiskObject> o;
    struct stat st;
    if (!name.empty() && name[0] == '/') {
        if (stat(name.c_str(), &st) == 0) o = icon_from_picture(name, 48);
    } else if (!name.empty() && name.find('/') == std::string::npos) {
        // the icon theme directories (the user's first), the sizes nearest 48 first, then pixmaps
        std::vector<std::string> bases;
        std::string home = getenv("HOME") ? getenv("HOME") : "";
        if (!home.empty()) { bases.push_back(home + "/.local/share/icons"); bases.push_back(home + "/.icons"); }
        const char *xd = getenv("XDG_DATA_DIRS");
        std::string dirs = xd && *xd ? xd : "/usr/local/share:/usr/share";
        for (size_t a = 0; a < dirs.size();) {
            size_t b = dirs.find(':', a);
            if (b == std::string::npos) b = dirs.size();
            if (b > a) bases.push_back(dirs.substr(a, b - a) + "/icons");
            a = b + 1;
        }
        static const char *const SIZES[] = {"48x48", "64x64", "scalable", "96x96", "128x128", "256x256", "32x32", "512x512", "24x24"};
        static const char *const THEMES[] = {"hicolor", "Adwaita", "breeze", "Papirus", "gnome", "oxygen"};
        static const char *const KINDS[] = {"apps", "categories", "devices", "places", "mimetypes"};
        std::string file;
        for (auto &b : bases) {
            for (auto *t : THEMES) {
                for (auto *sz : SIZES)
                    for (auto *k : KINDS)
                        for (const char *ext : {".png", ".svg"}) {
                            std::string f = b + "/" + t + "/" + sz + "/" + k + "/" + name + ext;
                            if (stat(f.c_str(), &st) == 0) { file = f; goto done; }
                        }
            }
            for (const char *ext : {".png", ".svg", ".xpm"}) {   // (an icon straight in the directory)
                std::string f = b + "/" + name + ext;
                if (stat(f.c_str(), &st) == 0) { file = f; goto done; }
            }
        }
        for (const char *ext : {".png", ".svg", ".xpm"}) {
            std::string f = std::string("/usr/share/pixmaps/") + name + ext;
            if (stat(f.c_str(), &st) == 0) { file = f; break; }
        }
    done:
        if (!file.empty()) o = icon_from_picture(file, 48);
    }
    found[name] = o;
    return o;
}

std::shared_ptr<DiskObject> get_disk_object(const std::string &path, bool volume) {
    std::string file = volume ? (path.empty() || path.back() == '/' ? path : path + "/") + "Disk.info" : path + ".info";
    struct stat st;
    // no icon file: an AppImage brings its own icon
    if (stat(file.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return volume ? nullptr : appimage_icon(path);
    // kept while the file stays as it is (and, for classic icons, the screen's pens)
    struct Kept { time_t mtime; off_t size; unsigned pens; std::shared_ptr<DiskObject> obj; };
    static std::map<std::string, Kept> cache;
    const auto &p = lune::prefs();
    unsigned pens = p.background ^ (p.shadow << 1) ^ (p.shine << 2) ^ (p.fill << 3);
    auto it = cache.find(file);
    if (it != cache.end() && it->second.mtime == st.st_mtime && it->second.size == st.st_size && it->second.pens == pens)
        return it->second.obj;
    auto o = read_icon_file(file);
    cache[file] = {st.st_mtime, st.st_size, pens, o};
    return o;
}

}  // namespace rambler
