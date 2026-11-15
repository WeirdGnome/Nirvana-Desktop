/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The icons of AppImages (a Linux addition; the original system has no such programs):
    an AppImage shows the icon it carries, as a program with its own .info would.

    A type 2 AppImage is an ELF program (the runtime, with "AI\2" at byte 8 of its header)
    and a squashfs file system after it, where the ELF ends (its section headers are last).
    The icon is ".DirIcon" in that file system (often a link to the picture). It is taken
    out with unsquashfs - the AppImage is never run - and kept in
    ~/.cache/rambler/appimage-icons, under a name made from the AppImage's path, size and
    date. Without unsquashfs (or for an older type 1 AppImage) the picture is the
    thumbnail that desktop tools leave in ~/.cache/thumbnails (the freedesktop thumbnail
    names: the MD5 of the file's URI).
*/
#include "diskobject.h"
#include "icons.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace rambler {

namespace {

// "AI" and the type at byte 8 of the ELF header (0 - not an AppImage)
int appimage_type(const std::string &path, unsigned long long &squashfs_offset) {
    std::ifstream in(path, std::ios::binary);
    unsigned char h[64];
    if (!in.read((char *)h, sizeof h)) return 0;
    if (memcmp(h, "\x7f" "ELF", 4) || h[8] != 'A' || h[9] != 'I' || (h[10] != 1 && h[10] != 2)) return 0;
    bool le = h[5] == 1;
    auto get = [&](int at, int n) {
        unsigned long long v = 0;
        for (int i = 0; i < n; i++) v |= (unsigned long long)h[at + (le ? i : n - 1 - i)] << (8 * i);
        return v;
    };
    if (h[4] == 2) squashfs_offset = get(0x28, 8) + get(0x3A, 2) * get(0x3C, 2);   // ELF64
    else squashfs_offset = get(0x20, 4) + get(0x2E, 2) * get(0x30, 2);              // ELF32
    return h[10];
}

std::string cache_dir() {
    const char *c = getenv("XDG_CACHE_HOME");
    std::string base = c && *c ? c : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.cache";
    return base + "/rambler/appimage-icons";
}

// a program run without a shell; true when it ended well
bool run(const std::vector<std::string> &argv) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        int null = open("/dev/null", 1);
        if (null >= 0) { dup2(null, 1); dup2(null, 2); }
        std::vector<char *> a;
        for (auto &s : argv) a.push_back(const_cast<char *>(s.c_str()));
        a.push_back(nullptr);
        execvp(a[0], a.data());
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

// .DirIcon taken out of the squashfs into dir (a link followed inside the image)
std::string extract_dir_icon(const std::string &appimage, unsigned long long offset, const std::string &dir) {
    std::error_code ec;
    std::string name = ".DirIcon";
    for (int hops = 0; hops < 4; hops++) {
        fs::remove_all(dir, ec);
        if (!run({"unsquashfs", "-q", "-n", "-no-xattrs", "-o", std::to_string(offset), "-d", dir, "-f", appimage, name}))
            return "";
        std::string got = dir + "/" + name;
        if (fs::is_symlink(got, ec)) {
            // a link (".DirIcon -> app.png", or "usr/share/icons/.../app.png"): that one, inside
            std::string target = fs::read_symlink(got, ec).string();
            while (target.rfind("./", 0) == 0) target = target.substr(2);
            if (target.empty() || target[0] == '/' || target.find("..") != std::string::npos) return "";
            name = target;
            continue;
        }
        return fs::is_regular_file(got, ec) ? got : "";
    }
    return "";
}

// the thumbnail desktop tools made of it (freedesktop: ~/.cache/thumbnails/<size>/<MD5 of URI>.png)
std::string md5_hex(const std::string &s);
std::string thumbnail_of(const std::string &path) {
    std::string uri = "file://";
    for (unsigned char c : fs::absolute(path).string()) {
        if (isalnum(c) || strchr("/-_.~", c)) uri += (char)c;
        else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); uri += b; }
    }
    const char *c = getenv("XDG_CACHE_HOME");
    std::string base = c && *c ? c : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.cache";
    std::string h = md5_hex(uri);
    std::error_code ec;
    for (const char *size : {"large", "normal", "x-large"}) {
        std::string f = base + "/thumbnails/" + size + "/" + h + ".png";
        if (fs::is_regular_file(f, ec)) return f;
    }
    return "";
}

// MD5 (RFC 1321) for the thumbnail names
std::string md5_hex(const std::string &msg) {
    static const unsigned K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                              14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    unsigned a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    std::string m = msg;
    unsigned long long bits = (unsigned long long)msg.size() * 8;
    m += (char)0x80;
    while (m.size() % 64 != 56) m += (char)0;
    for (int i = 0; i < 8; i++) m += (char)(bits >> (8 * i));
    for (size_t off = 0; off < m.size(); off += 64) {
        unsigned M[16];
        for (int i = 0; i < 16; i++)
            M[i] = (unsigned char)m[off + i * 4] | (unsigned char)m[off + i * 4 + 1] << 8 |
                   (unsigned char)m[off + i * 4 + 2] << 16 | (unsigned)(unsigned char)m[off + i * 4 + 3] << 24;
        unsigned A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; i++) {
            unsigned F;
            int g;
            if (i < 16) { F = (B & C) | (~B & D); g = i; }
            else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
            else { F = C ^ (B | ~D); g = (7 * i) % 16; }
            F = F + A + K[i] + M[g];
            A = D; D = C; C = B;
            B = B + (F << R[i] | F >> (32 - R[i]));
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }
    char out[33];
    unsigned v[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02x", (v[i / 4] >> (8 * (i % 4))) & 255);
    return out;
}
}  // namespace

std::shared_ptr<DiskObject> appimage_icon(const std::string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 4096) return nullptr;
    unsigned long long offset = 0;
    int type = appimage_type(path, offset);
    if (!type) return nullptr;
    // kept while the AppImage stays as it is
    struct Kept { time_t mtime; off_t size; std::shared_ptr<DiskObject> obj; };
    static std::map<std::string, Kept> kept;
    auto it = kept.find(path);
    if (it != kept.end() && it->second.mtime == st.st_mtime && it->second.size == st.st_size) return it->second.obj;
    std::shared_ptr<DiskObject> obj;
    // the picture taken out before, else now
    std::string key = md5_hex(path + "|" + std::to_string(st.st_size) + "|" + std::to_string(st.st_mtime));
    std::string cached = cache_dir() + "/" + key;
    std::error_code ec;
    std::string picture;
    for (const char *ext : {".png", ".svg", ".xpm"})
        if (fs::is_regular_file(cached + ext, ec)) { picture = cached + ext; break; }
    bool tried = fs::exists(cached + ".none", ec);   // (taken out before, with no picture: not again)
    if (picture.empty() && !tried && type == 2 && offset > 0 && offset < (unsigned long long)st.st_size) {
        std::string dir = cache_dir() + "/" + key + ".tmp";
        fs::create_directories(cache_dir(), ec);
        std::string got = extract_dir_icon(path, offset, dir);
        if (!got.empty()) {
            // the kind of picture from its first bytes (.DirIcon has no extension)
            std::ifstream in(got, std::ios::binary);
            char head[256] = {0};
            in.read(head, sizeof head - 1);
            std::string h(head, (size_t)in.gcount());
            const char *ext = !h.compare(0, 4, "\x89PNG") ? ".png"
                              : h.find("<svg") != std::string::npos || h.find("<?xml") != std::string::npos ? ".svg"
                              : h.find("XPM") != std::string::npos ? ".xpm" : nullptr;
            if (ext) {
                fs::rename(got, cached + ext, ec);
                if (!ec) picture = cached + ext;
            }
        }
        fs::remove_all(dir, ec);
        if (picture.empty()) std::ofstream(cached + ".none");
    }
    if (picture.empty()) picture = thumbnail_of(path);
    if (!picture.empty()) obj = icon_from_picture(picture, ICON_W);
    kept[path] = {st.st_mtime, st.st_size, obj};
    return obj;
}

}  // namespace rambler
