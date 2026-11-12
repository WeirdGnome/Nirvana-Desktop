// Nirvana Desktop - Rambler: metadata of files
#include "metadata.h"
#include "i18n.h"
#include "platinum.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace meta {

static std::string read_bytes(const std::string &path, long off, size_t n) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  if (off < 0) { in.seekg(0, std::ios::end); long sz = (long)in.tellg(); off = std::max(0L, sz + off); }
  in.seekg(off);
  std::string s(n, '\0');
  in.read(&s[0], (std::streamsize)n);
  s.resize((size_t)in.gcount());
  return s;
}

static unsigned be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned le16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static uint32_t be32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint32_t le32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static std::string duration(double s) {
  if (s <= 0) return "";
  int t = (int)(s + 0.5);
  char b[32];
  if (t >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
  else snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
  return b;
}

static std::string dims(unsigned w, unsigned h) { return std::to_string(w) + " × " + std::to_string(h) + " px"; }

// text from ID3 frames: 0 latin-1, 1 UTF-16 with BOM, 2 UTF-16BE, 3 UTF-8
static std::string id3_text(const unsigned char *p, size_t n) {
  if (!n) return "";
  int enc = p[0];
  std::string o;
  if (enc == 0) {
    for (size_t i = 1; i < n && p[i]; i++) {
      unsigned char c = p[i];
      if (c < 0x80) o += (char)c; else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    }
  } else if (enc == 3) {
    o.assign((const char *)p + 1, strnlen((const char *)p + 1, n - 1));
  } else {
    size_t i = 1;
    bool le = enc == 1;
    if (enc == 1 && n >= 3) { le = !(p[1] == 0xFE && p[2] == 0xFF); i = 3; }
    for (; i + 1 < n; i += 2) {
      unsigned c = le ? (p[i] | (p[i + 1] << 8)) : ((p[i] << 8) | p[i + 1]);
      if (!c) break;
      if (c < 0x80) o += (char)c;
      else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
      else { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
  }
  while (!o.empty() && (o.back() == ' ' || o.back() == '\0')) o.pop_back();
  return o;
}

static void mp3(const std::string &path, Fields &f) {
  std::string h = read_bytes(path, 0, 256 * 1024);
  auto d = (const unsigned char *)h.data();
  size_t n = h.size(), pos = 0;
  std::string title, artist, album, year, track, genre;
  if (n > 10 && memcmp(d, "ID3", 3) == 0) {
    int ver = d[3];
    size_t size = ((d[6] & 0x7F) << 21) | ((d[7] & 0x7F) << 14) | ((d[8] & 0x7F) << 7) | (d[9] & 0x7F);
    size_t end = std::min(n, 10 + size);
    size_t p = 10;
    if (d[5] & 0x40 && p + 4 < end) p += ver == 4 ? (((d[p] & 0x7F) << 21) | ((d[p + 1] & 0x7F) << 14) | ((d[p + 2] & 0x7F) << 7) | (d[p + 3] & 0x7F)) : be32(d + p) + 4;
    while (p + 10 <= end && d[p]) {
      std::string id((const char *)d + p, 4);
      size_t fs = ver == 4 ? (((d[p + 4] & 0x7F) << 21) | ((d[p + 5] & 0x7F) << 14) | ((d[p + 6] & 0x7F) << 7) | (d[p + 7] & 0x7F)) : be32(d + p + 4);
      if (p + 10 + fs > end || fs == 0) break;
      std::string v = id[0] == 'T' ? id3_text(d + p + 10, fs) : "";
      if (id == "TIT2") title = v;
      else if (id == "TPE1") artist = v;
      else if (id == "TALB") album = v;
      else if (id == "TYER" || id == "TDRC") year = v;
      else if (id == "TRCK") track = v;
      else if (id == "TCON") genre = v;
      p += 10 + fs;
    }
    pos = 10 + size;
  }
  if (title.empty() && artist.empty()) {   // ID3v1 at the end
    std::string t = read_bytes(path, -128, 128);
    if (t.size() == 128 && t.compare(0, 3, "TAG") == 0) {
      auto field = [&](int o, int l) {
        std::string s = t.substr(o, l);
        s = s.substr(0, strnlen(s.c_str(), l));
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return id3_text((const unsigned char *)("\0" + s).c_str(), s.size() + 1);
      };
      title = field(3, 30); artist = field(33, 30); album = field(63, 30); year = field(93, 4);
    }
  }
  // the first frame: bit rate and sample rate (MPEG-1/2 layer III), Xing/Info frame count
  struct stat st;
  stat(path.c_str(), &st);
  for (size_t p = pos; p + 4 < n; p++) {
    if (d[p] != 0xFF || (d[p + 1] & 0xE0) != 0xE0) continue;
    int vid = (d[p + 1] >> 3) & 3, layer = (d[p + 1] >> 1) & 3, bri = d[p + 2] >> 4, sri = (d[p + 2] >> 2) & 3;
    if (layer != 1 || bri == 0 || bri == 15 || sri == 3 || vid == 1) continue;
    static const int br1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const int br2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static const int sr1[] = {44100, 48000, 32000};
    int kbps = vid == 3 ? br1[bri] : br2[bri];
    int rate = sr1[sri] / (vid == 3 ? 1 : vid == 2 ? 2 : 4);
    int spf = vid == 3 ? 1152 : 576;
    double secs = 0;
    const unsigned char *x = (const unsigned char *)memmem(d + p, std::min<size_t>(n - p, 200), "Xing", 4);
    if (!x) x = (const unsigned char *)memmem(d + p, std::min<size_t>(n - p, 200), "Info", 4);
    if (x && (x[7] & 1)) secs = (double)be32(x + 8) * spf / rate;
    else if (kbps) secs = (double)(st.st_size - p) * 8 / (kbps * 1000.0);
    f.push_back({_("Duration"), duration(secs)});
    f.push_back({_("Bit rate"), std::to_string(x && secs > 0 ? (int)((st.st_size - p) * 8 / secs / 1000) : kbps) + " kbit/s"});
    f.push_back({_("Sample rate"), std::to_string(rate) + " Hz"});
    f.push_back({_("Channels"), ((d[p + 3] >> 6) == 3) ? _("mono") : _("stereo")});
    break;
  }
  // the tags first (what people look for), then the sound's technical data
  Fields tags;
  if (!title.empty()) tags.push_back({_("Title"), title});
  if (!artist.empty()) tags.push_back({_("Artist"), artist});
  if (!album.empty()) tags.push_back({_("Album"), album});
  if (!year.empty()) tags.push_back({_("Year"), year});
  if (!track.empty()) tags.push_back({_("Track"), track});
  if (!genre.empty()) tags.push_back({_("Genre"), genre});
  f.insert(f.begin(), tags.begin(), tags.end());
}

// "ARTIST=Someone" lines of Vorbis comments (FLAC, Ogg Vorbis, Opus)
static void vorbis_comments(const unsigned char *p, size_t n, Fields &f) {
  if (n < 8) return;
  uint32_t vl = le32(p);
  if (4 + vl + 4 > n) return;
  const unsigned char *q = p + 4 + vl;
  uint32_t cnt = le32(q);
  q += 4;
  static const std::pair<const char *, const char *> keys[] = {
      {"TITLE", N_("Title")}, {"ARTIST", N_("Artist")}, {"ALBUM", N_("Album")}, {"DATE", N_("Year")},
      {"TRACKNUMBER", N_("Track")}, {"GENRE", N_("Genre")}};
  for (uint32_t i = 0; i < cnt && i < 200; i++) {
    if (q + 4 > p + n) break;
    uint32_t l = le32(q);
    q += 4;
    if (q + l > p + n) break;
    std::string c((const char *)q, l);
    q += l;
    size_t eq = c.find('=');
    if (eq == std::string::npos) continue;
    std::string k = c.substr(0, eq);
    for (auto &ch : k) ch = (char)toupper((unsigned char)ch);
    for (auto &[key, label] : keys)
      if (k == key) f.push_back({_(label), c.substr(eq + 1)});
  }
}

static void flac(const std::string &path, Fields &f) {
  std::string h = read_bytes(path, 0, 512 * 1024);
  auto d = (const unsigned char *)h.data();
  size_t n = h.size(), p = 4;
  while (p + 4 <= n) {
    int type = d[p] & 0x7F;
    bool last = d[p] & 0x80;
    size_t len = (d[p + 1] << 16) | (d[p + 2] << 8) | d[p + 3];
    const unsigned char *b = d + p + 4;
    if (p + 4 + len > n) break;
    if (type == 0 && len >= 18) {
      unsigned rate = (b[10] << 12) | (b[11] << 4) | (b[12] >> 4);
      unsigned ch = ((b[12] >> 1) & 7) + 1, bits = (((b[12] & 1) << 4) | (b[13] >> 4)) + 1;
      uint64_t total = ((uint64_t)(b[13] & 0x0F) << 32) | be32(b + 14);
      if (rate) f.push_back({_("Duration"), duration((double)total / rate)});
      f.push_back({_("Sample rate"), std::to_string(rate) + " Hz, " + std::to_string(bits) + " bit"});
      f.push_back({_("Channels"), std::to_string(ch)});
    } else if (type == 4) {
      Fields c;
      vorbis_comments(b, len, c);
      f.insert(f.begin(), c.begin(), c.end());
    }
    p += 4 + len;
    if (last) break;
  }
}

static void ogg(const std::string &path, const sniff::Kind &k, Fields &f) {
  std::string h = read_bytes(path, 0, 128 * 1024);
  auto d = (const unsigned char *)h.data();
  size_t n = h.size();
  unsigned rate = 48000;
  if (const void *v = memmem(d, n, "\x01vorbis", 7)) rate = le32((const unsigned char *)v + 12);
  const unsigned char *c = (const unsigned char *)memmem(d, n, "\x03vorbis", 7);
  size_t skip = 7;
  if (!c) { c = (const unsigned char *)memmem(d, n, "OpusTags", 8); skip = 8; }
  Fields com;
  if (c) vorbis_comments(c + skip, n - (size_t)(c + skip - d), com);
  std::string t = read_bytes(path, -65536, 65536);   // the last page: the granule = samples
  for (size_t i = t.size(); i-- > 4;) {
    if (t.compare(i, 4, "OggS") == 0 && i + 14 <= t.size()) {
      uint64_t g = le32((const unsigned char *)t.data() + i + 6) | ((uint64_t)le32((const unsigned char *)t.data() + i + 10) << 32);
      if (rate) f.push_back({_("Duration"), duration((double)g / rate)});
      break;
    }
  }
  if (k.mime != "audio/opus" && rate) f.push_back({_("Sample rate"), std::to_string(rate) + " Hz"});
  f.insert(f.begin(), com.begin(), com.end());
}

static void wav(const std::string &path, Fields &f) {
  std::string h = read_bytes(path, 0, 4096);
  auto d = (const unsigned char *)h.data();
  size_t n = h.size(), p = 12;
  unsigned ch = 0, rate = 0, bits = 0, byterate = 0;
  while (p + 8 <= n) {
    uint32_t len = le32(d + p + 4);
    if (!memcmp(d + p, "fmt ", 4) && p + 24 <= n) {
      ch = le16(d + p + 10); rate = le32(d + p + 12); byterate = le32(d + p + 16); bits = le16(d + p + 22);
    } else if (!memcmp(d + p, "data", 4)) {
      if (byterate) f.push_back({_("Duration"), duration((double)len / byterate)});
      break;
    }
    p += 8 + len + (len & 1);
  }
  if (rate) f.push_back({_("Sample rate"), std::to_string(rate) + " Hz, " + std::to_string(bits) + " bit"});
  if (ch) f.push_back({_("Channels"), std::to_string(ch)});
}

// JPEG: the size from the SOF marker, the camera from EXIF (IFD0: make, model, date)
static void jpeg(const std::string &path, Fields &f) {
  std::string h = read_bytes(path, 0, 256 * 1024);
  auto d = (const unsigned char *)h.data();
  size_t n = h.size(), p = 2;
  std::string make, model, date;
  while (p + 4 <= n && d[p] == 0xFF) {
    unsigned m = d[p + 1], len = be16(d + p + 2);
    if (m == 0xE1 && p + 10 < n && !memcmp(d + p + 4, "Exif\0\0", 6)) {
      const unsigned char *t = d + p + 10;
      size_t tn = std::min<size_t>(len - 8, n - (p + 10));
      bool le = tn > 8 && t[0] == 'I';
      auto u16 = [&](size_t o) { return o + 2 <= tn ? (le ? le16(t + o) : be16(t + o)) : 0u; };
      auto u32 = [&](size_t o) { return o + 4 <= tn ? (le ? le32(t + o) : be32(t + o)) : 0u; };
      size_t ifd = u32(4);
      unsigned cnt = u16(ifd);
      for (unsigned i = 0; i < cnt && i < 64; i++) {
        size_t e = ifd + 2 + i * 12;
        unsigned tag = u16(e), type = u16(e + 2);
        uint32_t count = u32(e + 4), off = u32(e + 8);
        if (type != 2 || count == 0) continue;
        std::string v = count <= 4 ? std::string((const char *)t + e + 8, count) : (off + count <= tn ? std::string((const char *)t + off, count) : "");
        v = v.substr(0, strnlen(v.c_str(), v.size()));
        if (tag == 0x010F) make = v;
        else if (tag == 0x0110) model = v;
        else if (tag == 0x0132) date = v;
      }
    }
    if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC && p + 9 < n) {
      f.insert(f.begin(), {_("Dimensions"), dims(be16(d + p + 7), be16(d + p + 5))});
      break;
    }
    if (m == 0xDA) break;
    p += 2 + len;
  }
  if (!model.empty()) f.push_back({_("Camera"), model.find(make) == 0 || make.empty() ? model : make + " " + model});
  if (!date.empty()) {   // "2024:07:15 10:30:00" -> "2024-07-15 10:30"
    if (date.size() >= 16) { date[4] = '-'; date[7] = '-'; date = date.substr(0, 16); }
    f.push_back({_("Taken"), date});
  }
}

static void image(const std::string &path, const sniff::Kind &k, Fields &f) {
  if (k.mime == "image/jpeg") { jpeg(path, f); return; }
  std::string h = read_bytes(path, 0, 64);
  auto d = (const unsigned char *)h.data();
  size_t n = h.size();
  if (k.mime == "image/png" && n >= 26) {
    f.push_back({_("Dimensions"), dims(be32(d + 16), be32(d + 20))});
    static const char *ct[] = {"gray", "", "RGB", "palette", "gray + alpha", "", "RGBA"};
    f.push_back({_("Colors"), std::to_string(d[24]) + " bit, " + (d[25] < 7 ? ct[d[25]] : "")});
  } else if (k.mime == "image/gif" && n >= 10) f.push_back({_("Dimensions"), dims(le16(d + 6), le16(d + 8))});
  else if (k.mime == "image/bmp" && n >= 26) f.push_back({_("Dimensions"), dims(le32(d + 18), (unsigned)std::abs((int32_t)le32(d + 22)))});
  else if (k.mime == "image/webp" && n >= 30) {
    if (!memcmp(d + 12, "VP8X", 4)) f.push_back({_("Dimensions"), dims((d[24] | (d[25] << 8) | (d[26] << 16)) + 1, (d[27] | (d[28] << 8) | (d[29] << 16)) + 1)});
    else if (!memcmp(d + 12, "VP8 ", 4)) f.push_back({_("Dimensions"), dims(le16(d + 26) & 0x3FFF, le16(d + 28) & 0x3FFF)});
    else if (!memcmp(d + 12, "VP8L", 4)) {
      uint32_t b = le32(d + 21);
      f.push_back({_("Dimensions"), dims((b & 0x3FFF) + 1, ((b >> 14) & 0x3FFF) + 1)});
    }
  } else if (k.mime == "image/svg+xml") {
    std::string s = read_bytes(path, 0, 4096);
    auto attr = [&](const char *a) {
      size_t p = s.find(std::string(" ") + a + "=\"");
      if (p == std::string::npos) return std::string();
      p += strlen(a) + 3;
      return s.substr(p, s.find('"', p) - p);
    };
    std::string w = attr("width"), hh = attr("height");
    if (!w.empty() && !hh.empty()) f.push_back({_("Dimensions"), w + " × " + hh});
  }
}

static void pdf(const std::string &path, Fields &f) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return;
  std::string h = read_bytes(path, 0, 16);
  if (h.size() > 8) f.push_back({_("Version"), "PDF " + h.substr(5, 3)});
  if (st.st_size > 30 * 1024 * 1024) return;   // counting pages in a huge file - not now
  std::string s = read_bytes(path, 0, (size_t)st.st_size);
  int pages = 0;
  for (size_t p = 0; (p = s.find("/Type", p)) != std::string::npos; p += 5) {
    size_t q = p + 5;
    while (q < s.size() && (s[q] == ' ' || s[q] == '\n' || s[q] == '\r')) q++;
    if (s.compare(q, 5, "/Page") == 0 && (q + 5 >= s.size() || s[q + 5] != 's')) pages++;
  }
  if (pages) f.push_back({_("Pages"), std::to_string(pages)});
  size_t t = s.find("/Title");
  if (t != std::string::npos) {
    size_t a = s.find('(', t);
    if (a != std::string::npos && a < t + 12) {
      size_t b = s.find(')', a);
      std::string title = s.substr(a + 1, std::min<size_t>(b - a - 1, 200));
      if (!title.empty() && (unsigned char)title[0] < 0xFE) f.insert(f.begin(), {_("Title"), title});
    }
  }
}

static void elf(const std::string &path, const sniff::Kind &k, Fields &f) {
  std::string h = read_bytes(path, 0, 20);
  if (h.size() < 20) return;
  auto d = (const unsigned char *)h.data();
  unsigned m = d[5] == 1 ? le16(d + 18) : be16(d + 18);
  const char *a = m == 62 ? "x86-64" : m == 3 ? "x86 (32 bit)" : m == 183 ? "ARM64 (AArch64)" : m == 40 ? "ARM (32 bit)"
                : m == 243 ? "RISC-V" : m == 21 ? "PowerPC 64" : "?";
  f.push_back({_("Architecture"), std::string(a) + (d[4] == 2 ? "" : "")});
  if (k.mime == "application/vnd.appimage") f.push_back({_("Kind"), "AppImage"});
}

static void text(const std::string &path, Fields &f) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0 || st.st_size > 50 * 1024 * 1024) return;
  std::ifstream in(path, std::ios::binary);
  long lines = 0, words = 0;
  bool ascii = true, inword = false;
  char buf[65536];
  while (in.read(buf, sizeof buf) || in.gcount()) {
    std::streamsize n = in.gcount();
    for (std::streamsize i = 0; i < n; i++) {
      unsigned char c = (unsigned char)buf[i];
      if (c == '\n') lines++;
      if (c >= 0x80) ascii = false;
      bool sp = c == ' ' || c == '\n' || c == '\t' || c == '\r';
      if (!sp && !inword) words++;
      inword = !sp;
    }
  }
  f.push_back({_("Lines"), std::to_string(lines)});
  f.push_back({_("Words"), std::to_string(words)});
  f.push_back({_("Encoding"), ascii ? "ASCII" : "UTF-8"});
}

Fields read(const std::string &path, const sniff::Kind &k) {
  Fields f;
  if (k.mime.empty() || !sniff::allowed(path)) return f;
  if (k.cat == "image") image(path, k, f);
  else if (k.mime == "audio/mpeg") mp3(path, f);
  else if (k.mime == "audio/flac") flac(path, f);
  else if (k.mime == "audio/ogg" || k.mime == "audio/opus") ogg(path, k, f);
  else if (k.mime == "audio/wav") wav(path, f);
  else if (k.mime == "application/pdf") pdf(path, f);
  else if (k.cat == "program" && k.mime.rfind("text/", 0) != 0) elf(path, k, f);
  else if (k.cat == "text" || k.mime.rfind("text/", 0) == 0) text(path, f);
  return f;
}

static std::string capture(const std::vector<std::string> &args) {
  int p[2];
  if (pipe(p) != 0) return "";
  pid_t pid = fork();
  if (pid == 0) {
    dup2(p[1], 1);
    int dn = open("/dev/null", O_WRONLY);
    if (dn >= 0) dup2(dn, 2);
    close(p[0]);
    std::vector<char *> a;
    for (auto &s : args) a.push_back((char *)s.c_str());
    a.push_back(nullptr);
    execvp(a[0], a.data());
    _exit(127);
  }
  close(p[1]);
  std::string o;
  char buf[4096];
  ssize_t n;
  while ((n = ::read(p[0], buf, sizeof buf)) > 0 && o.size() < 4 * 1024 * 1024) o.append(buf, n);
  close(p[0]);
  int st;
  waitpid(pid, &st, 0);
  return o;
}

Fields read_slow(const std::string &path, const sniff::Kind &k) {
  Fields f;
  if (!sniff::allowed(path)) return f;
  if (k.cat == "video" || (k.cat == "audio" && k.mime == "audio/mp4")) {
    // ffprobe (package ffmpeg), if it is there
    std::string o = capture({"ffprobe", "-v", "quiet", "-show_entries",
                             "format=duration:stream=codec_type,codec_name,width,height,r_frame_rate", "-of", "default=nw=1", path});
    std::istringstream s(o);
    std::string l, vcodec, acodec, w, h, fps, dur, type;
    while (std::getline(s, l)) {
      size_t e = l.find('=');
      if (e == std::string::npos) continue;
      std::string key = l.substr(0, e), v = l.substr(e + 1);
      if (key == "codec_type") type = v;
      else if (key == "codec_name") {
        if (type == "video" && vcodec.empty()) vcodec = v;
        else if (type == "audio" && acodec.empty()) acodec = v;
      }
      else if (key == "width" && w.empty()) w = v;
      else if (key == "height" && h.empty()) h = v;
      else if (key == "r_frame_rate" && type == "video" && fps.empty()) {
        size_t sl = v.find('/');
        if (sl != std::string::npos && atof(v.c_str() + sl + 1) > 0) {
          char b[16];
          snprintf(b, sizeof b, "%.2f", atof(v.c_str()) / atof(v.c_str() + sl + 1));
          fps = b;
        }
      } else if (key == "duration") dur = v;
    }
    if (!w.empty() && w != "N/A") f.push_back({_("Dimensions"), w + " × " + h + " px" + (fps.empty() ? "" : ", " + fps + " fps")});
    if (!dur.empty() && dur != "N/A") f.push_back({_("Duration"), duration(atof(dur.c_str()))});
    if (!vcodec.empty()) f.push_back({_("Video"), vcodec});
    if (!acodec.empty()) f.push_back({_("Sound"), acodec});
  } else if (k.cat == "archive" && k.mime != "application/vnd.debian.binary-package") {
    std::string o = capture({"bsdtar", "-tvf", path});
    long files = 0;
    uint64_t total = 0;
    std::istringstream s(o);
    std::string l;
    while (std::getline(s, l)) {
      std::istringstream w(l);
      std::string perm, links, user, group;
      uint64_t size = 0;
      if (!(w >> perm >> links >> user >> group >> size)) continue;
      if (perm[0] != 'd') files++;
      total += size;
    }
    if (files) {
      f.push_back({_("Files inside"), std::to_string(files)});
      char b[32];
      snprintf(b, sizeof b, "%.1f MB", total / 1048576.0);
      f.push_back({_("Unpacked size"), b});
    }
  }
  return f;
}

std::string text_of(const Fields &f) {
  std::string s;
  for (auto &[k, v] : f) s += v + "\n";
  return s;
}

}  // namespace meta
