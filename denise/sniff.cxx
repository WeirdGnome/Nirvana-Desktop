// Nirvana Desktop - Rambler: content sniffing
#include "sniff.h"
#include "i18n.h"
#include "platinum.h"

#include <cstring>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace sniff {

static bool at(const unsigned char *d, size_t n, size_t off, const char *sig, size_t len = 0) {
  if (!len) len = strlen(sig);
  return n >= off + len && memcmp(d + off, sig, len) == 0;
}

static std::string ext_of(const std::string &name) {
  size_t dot = name.rfind('.');
  if (dot == std::string::npos) return "";
  std::string e = name.substr(dot + 1);
  for (auto &c : e) c = (char)tolower((unsigned char)c);
  return e;
}

static Kind make(const char *mime, const char *cat, const std::string &fmt_name) {
  Kind k{mime, cat, ""};
  std::string c = cat;
  if (c == "image") k.label = plat::fmt(_("%1 picture"), fmt_name);
  else if (c == "audio") k.label = plat::fmt(_("%1 sound"), fmt_name);
  else if (c == "video") k.label = plat::fmt(_("%1 video"), fmt_name);
  else if (c == "archive") k.label = plat::fmt(_("%1 archive"), fmt_name);
  else if (c == "document") k.label = plat::fmt(_("%1 document"), fmt_name);
  else if (c == "program") k.label = fmt_name.empty() ? std::string(_("program")) : plat::fmt(_("%1 program"), fmt_name);
  else if (c == "text") k.label = fmt_name.empty() ? std::string(_("text")) : plat::fmt(_("%1 text"), fmt_name);
  else k.label = fmt_name;
  return k;
}

static bool looks_text(const unsigned char *d, size_t n) {
  if (!n) return false;
  size_t i = 0;
  while (i < n) {
    unsigned char c = d[i];
    if (c == 0) return false;
    if (c < 0x80) {
      if (c < 9 || (c > 13 && c < 32 && c != 27)) return false;
      i++;
      continue;
    }
    int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (!len) return false;
    if (i + len > n) break;   // a character cut by the end of the sample
    for (int k = 1; k < len; k++) if ((d[i + k] & 0xC0) != 0x80) return false;
    i += len;
  }
  return true;
}

Kind of_data(const unsigned char *d, size_t n, const std::string &name) {
  std::string ext = ext_of(name);
  // pictures
  if (at(d, n, 0, "\x89PNG\r\n\x1a\n", 8)) return make("image/png", "image", "PNG");
  if (at(d, n, 0, "\xff\xd8\xff", 3)) return make("image/jpeg", "image", "JPEG");
  if (at(d, n, 0, "GIF87a") || at(d, n, 0, "GIF89a")) return make("image/gif", "image", "GIF");
  if (at(d, n, 0, "BM") && n > 14 && (ext == "bmp" || d[14] == 40 || d[14] == 12 || d[14] == 108 || d[14] == 124))
    return make("image/bmp", "image", "BMP");
  if (at(d, n, 0, "RIFF") && at(d, n, 8, "WEBP")) return make("image/webp", "image", "WebP");
  if (at(d, n, 0, "II*\0", 4) || at(d, n, 0, "MM\0*", 4)) return make("image/tiff", "image", "TIFF");
  if (at(d, n, 0, "FORM") && at(d, n, 8, "ILBM")) return make("image/x-ilbm", "image", "IFF ILBM");
  if (at(d, n, 0, "\0\0\1\0", 4) && ext == "ico") return make("image/x-icon", "image", "ICO");
  if (at(d, n, 4, "ftyp") && (at(d, n, 8, "avif") || at(d, n, 8, "heic") || at(d, n, 8, "mif1")))
    return make(at(d, n, 8, "avif") ? "image/avif" : "image/heic", "image", at(d, n, 8, "avif") ? "AVIF" : "HEIC");
  // sounds
  if (at(d, n, 0, "ID3")) return make("audio/mpeg", "audio", "MP3");
  if (n > 2 && d[0] == 0xFF && (d[1] & 0xE6) == 0xE2) return make("audio/mpeg", "audio", "MP3");   // an MPEG frame
  if (at(d, n, 0, "fLaC")) return make("audio/flac", "audio", "FLAC");
  if (at(d, n, 0, "RIFF") && at(d, n, 8, "WAVE")) return make("audio/wav", "audio", "WAV");
  if (at(d, n, 0, "OggS")) {
    if (memmem(d, n, "OpusHead", 8)) return make("audio/opus", "audio", "Opus");
    if (memmem(d, n, "\x01vorbis", 7)) return make("audio/ogg", "audio", "Ogg Vorbis");
    if (memmem(d, n, "theora", 6)) return make("video/ogg", "video", "Ogg Theora");
    return make("audio/ogg", "audio", "Ogg");
  }
  if (at(d, n, 0, "FORM") && (at(d, n, 8, "AIFF") || at(d, n, 8, "AIFC"))) return make("audio/aiff", "audio", "AIFF");
  if (at(d, n, 1080, "M.K.") || at(d, n, 1080, "4CHN") || at(d, n, 1080, "8CHN")) return make("audio/x-mod", "audio", "MOD");
  if (at(d, n, 0, "Extended Module:")) return make("audio/x-xm", "audio", "XM");
  if (at(d, n, 4, "ftyp") && (at(d, n, 8, "M4A ") || at(d, n, 8, "M4B "))) return make("audio/mp4", "audio", "AAC/M4A");
  // films
  if (at(d, n, 4, "ftyp")) return make("video/mp4", "video", at(d, n, 8, "qt  ") ? "QuickTime" : "MP4");
  if (at(d, n, 0, "\x1a\x45\xdf\xa3", 4)) {
    bool webm = memmem(d, n, "webm", 4) != nullptr;
    return make(webm ? "video/webm" : "video/x-matroska", "video", webm ? "WebM" : "Matroska");
  }
  if (at(d, n, 0, "RIFF") && at(d, n, 8, "AVI ")) return make("video/x-msvideo", "video", "AVI");
  if (at(d, n, 0, "\0\0\1\xba", 4) || at(d, n, 0, "\0\0\1\xb3", 4)) return make("video/mpeg", "video", "MPEG");
  if (at(d, n, 0, "FLV")) return make("video/x-flv", "video", "FLV");
  // documents
  if (at(d, n, 0, "%PDF-")) return make("application/pdf", "document", "PDF");
  if (at(d, n, 0, "PK\3\4", 4)) {
    // zip: an office document is a zip too - its first entry tells
    if (at(d, n, 30, "mimetype")) {
      const char *m = (const char *)d + 38;
      size_t left = n > 38 ? n - 38 : 0;
      std::string mt(m, strnlen(m, std::min<size_t>(left, 80)));
      if (mt.find("opendocument.text") != std::string::npos) return make("application/vnd.oasis.opendocument.text", "document", "ODT");
      if (mt.find("opendocument.spreadsheet") != std::string::npos) return make("application/vnd.oasis.opendocument.spreadsheet", "document", "ODS");
      if (mt.find("opendocument.presentation") != std::string::npos) return make("application/vnd.oasis.opendocument.presentation", "document", "ODP");
      if (mt.find("epub") != std::string::npos) return make("application/epub+zip", "document", "EPUB");
    }
    if (memmem(d, n, "[Content_Types].xml", 19) || memmem(d, n, "word/", 5) || memmem(d, n, "xl/", 3) || memmem(d, n, "ppt/", 4)) {
      if (ext == "xlsx") return make("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "document", "XLSX");
      if (ext == "pptx") return make("application/vnd.openxmlformats-officedocument.presentationml.presentation", "document", "PPTX");
      return make("application/vnd.openxmlformats-officedocument.wordprocessingml.document", "document", "DOCX");
    }
    if (ext == "jar") return make("application/java-archive", "archive", "JAR");
    if (ext == "apk") return make("application/vnd.android.package-archive", "archive", "APK");
    return make("application/zip", "archive", "ZIP");
  }
  if (at(d, n, 0, "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", 8)) return make("application/x-ole-storage", "document", ext.empty() ? "OLE" : ext == "xls" ? "XLS" : ext == "ppt" ? "PPT" : "DOC");
  if (at(d, n, 0, "{\\rtf")) return make("application/rtf", "document", "RTF");
  // archives and compression
  if (at(d, n, 0, "\x1f\x8b", 2)) return make("application/gzip", "archive", (ext == "tgz" || name.find(".tar.") != std::string::npos) ? "TAR.GZ" : "GZIP");
  if (at(d, n, 0, "BZh")) return make("application/x-bzip2", "archive", name.find(".tar.") != std::string::npos ? "TAR.BZ2" : "BZIP2");
  if (at(d, n, 0, "\xfd" "7zXZ\0", 6)) return make("application/x-xz", "archive", name.find(".tar.") != std::string::npos ? "TAR.XZ" : "XZ");
  if (at(d, n, 0, "\x28\xb5\x2f\xfd", 4)) return make("application/zstd", "archive", "ZSTD");
  if (at(d, n, 0, "7z\xbc\xaf\x27\x1c", 6)) return make("application/x-7z-compressed", "archive", "7-Zip");
  if (at(d, n, 0, "Rar!\x1a\x07", 6)) return make("application/vnd.rar", "archive", "RAR");
  if (at(d, n, 257, "ustar")) return make("application/x-tar", "archive", "TAR");
  if (at(d, n, 0, "070701") || at(d, n, 0, "070707")) return make("application/x-cpio", "archive", "CPIO");
  if (n > 7 && d[2] == '-' && d[3] == 'l' && d[4] == 'h' && d[6] == '-') return make("application/x-lha", "archive", "LHA");
  if (at(d, n, 0, "!<arch>\ndebian")) return make("application/vnd.debian.binary-package", "archive", _("Debian package"));
  if (at(d, n, 0x8001, "CD001")) return make("application/x-iso9660-image", "archive", _("disc image (ISO)"));
  if (at(d, n, 0, "SQLite format 3")) return make("application/vnd.sqlite3", "document", "SQLite");
  // programs
  if (at(d, n, 0, "\x7f" "ELF", 4)) {
    if (at(d, n, 8, "AI\x02", 3)) return make("application/vnd.appimage", "program", "AppImage");
    bool lib = n > 17 && d[16] == 3 && ext == "so";
    return make(lib ? "application/x-sharedlib" : "application/x-executable", "program", lib ? _("library") : "ELF");
  }
  if (at(d, n, 0, "MZ") && ext == "exe") return make("application/x-msdownload", "program", "EXE");
  if (at(d, n, 0, "#!")) {
    std::string line((const char *)d, strnlen((const char *)d, std::min<size_t>(n, 120)));
    line = line.substr(0, line.find('\n'));
    if (line.find("python") != std::string::npos) return make("text/x-python", "program", _("Python script"));
    if (line.find("perl") != std::string::npos) return make("text/x-perl", "program", _("Perl script"));
    return make("text/x-shellscript", "program", _("shell script"));
  }
  // marked-up and plain text
  if (looks_text(d, n)) {
    std::string head((const char *)d, std::min<size_t>(n, 512));
    for (auto &c : head) c = (char)tolower((unsigned char)c);
    if (head.find("<svg") != std::string::npos) return make("image/svg+xml", "image", "SVG");
    if (head.find("<!doctype html") != std::string::npos || head.find("<html") != std::string::npos) return make("text/html", "text", "HTML");
    if (head.rfind("<?xml", 0) == 0) return make("text/xml", "text", "XML");
    if (!head.empty() && (head[0] == '{' || head[0] == '[') && (ext == "json" || head.find('"') != std::string::npos)) return make("application/json", "text", "JSON");
    if (ext == "md") return make("text/markdown", "text", "Markdown");
    if (ext == "csv") return make("text/csv", "text", "CSV");
    return make("text/plain", "text", "");
  }
  return Kind{};
}

struct Cached { off_t size; time_t mtime; Kind k; };
static std::map<std::string, Cached> g_cache;
static std::mutex g_mutex;

bool allowed(const std::string &p) {
  return p.find("/.cache/nirvana/servers/") == std::string::npos && p.find("/gvfs/") == std::string::npos;
}

Kind of(const std::string &path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return Kind{};
  {
    std::lock_guard<std::mutex> l(g_mutex);
    auto it = g_cache.find(path);
    if (it != g_cache.end() && it->second.size == st.st_size && it->second.mtime == st.st_mtime) return it->second.k;
  }
  if (!allowed(path)) return by_name(path.substr(path.rfind('/') + 1));
  unsigned char buf[0x8010];   // enough for the ISO 9660 signature at 0x8001
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) return Kind{};
  std::string name = path.substr(path.rfind('/') + 1);
  std::string e = ext_of(name);
  // 4 KB is enough for everything but disc images (the signature at 32 KB)
  size_t want = (e == "iso" || e == "img" || e == "bin") ? sizeof buf : 4096;
  ssize_t n = read(fd, buf, (size_t)std::min<off_t>((off_t)want, std::max<off_t>(st.st_size, 0)));
  close(fd);
  Kind k = n > 0 ? of_data(buf, (size_t)n, name) : Kind{};
  if (n == 0) k = Kind{"application/x-empty", "other", _("empty file")};
  std::lock_guard<std::mutex> l(g_mutex);
  if (g_cache.size() > 20000) g_cache.clear();
  g_cache[path] = {st.st_size, st.st_mtime, k};
  return k;
}

// On servers (nothing is read over the network) the name decides: the usual endings
Kind by_name(const std::string &name) {
  std::string e = ext_of(name);
  if (e.empty()) return Kind{};
  struct N { const char *ext, *mime, *cat; };
  static const N names[] = {
      {"png", "image/png", "image"}, {"jpg", "image/jpeg", "image"}, {"jpeg", "image/jpeg", "image"}, {"gif", "image/gif", "image"},
      {"bmp", "image/bmp", "image"}, {"webp", "image/webp", "image"}, {"svg", "image/svg+xml", "image"}, {"tif", "image/tiff", "image"},
      {"tiff", "image/tiff", "image"}, {"ico", "image/x-icon", "image"}, {"iff", "image/x-ilbm", "image"},
      {"mp3", "audio/mpeg", "audio"}, {"ogg", "audio/ogg", "audio"}, {"oga", "audio/ogg", "audio"}, {"opus", "audio/ogg", "audio"},
      {"flac", "audio/flac", "audio"}, {"wav", "audio/x-wav", "audio"}, {"m4a", "audio/mp4", "audio"}, {"mod", "audio/x-mod", "audio"},
      {"mp4", "video/mp4", "video"}, {"m4v", "video/mp4", "video"}, {"mkv", "video/x-matroska", "video"}, {"webm", "video/webm", "video"},
      {"avi", "video/x-msvideo", "video"}, {"mov", "video/quicktime", "video"}, {"mpg", "video/mpeg", "video"},
      {"zip", "application/zip", "archive"}, {"tar", "application/x-tar", "archive"}, {"gz", "application/gzip", "archive"},
      {"tgz", "application/gzip", "archive"}, {"bz2", "application/x-bzip2", "archive"}, {"xz", "application/x-xz", "archive"},
      {"zst", "application/zstd", "archive"}, {"7z", "application/x-7z-compressed", "archive"}, {"rar", "application/vnd.rar", "archive"},
      {"lha", "application/x-lha", "archive"}, {"lzh", "application/x-lha", "archive"}, {"iso", "application/x-iso9660-image", "archive"},
      {"deb", "application/vnd.debian.binary-package", "archive"},
      {"pdf", "application/pdf", "document"}, {"odt", "application/vnd.oasis.opendocument.text", "document"},
      {"ods", "application/vnd.oasis.opendocument.spreadsheet", "document"}, {"odp", "application/vnd.oasis.opendocument.presentation", "document"},
      {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document", "document"},
      {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "document"},
      {"doc", "application/msword", "document"}, {"xls", "application/vnd.ms-excel", "document"}, {"rtf", "application/rtf", "document"},
      {"epub", "application/epub+zip", "document"},
      {"txt", "text/plain", "text"}, {"md", "text/markdown", "text"}, {"log", "text/plain", "text"}, {"csv", "text/csv", "text"},
      {"conf", "text/plain", "text"}, {"ini", "text/plain", "text"}, {"json", "application/json", "text"}, {"xml", "application/xml", "text"},
      {"html", "text/html", "text"}, {"htm", "text/html", "text"}, {"sh", "text/x-shellscript", "text"}, {"py", "text/x-python", "text"},
      {"c", "text/x-c", "text"}, {"cxx", "text/x-c++", "text"}, {"cpp", "text/x-c++", "text"}, {"h", "text/x-c", "text"},
      {"appimage", "application/vnd.appimage", "program"}, {"exe", "application/x-msdownload", "program"},
  };
  for (auto &x : names)
    if (e == x.ext) {
      std::string up = e;
      for (auto &c : up) c = (char)toupper((unsigned char)c);
      return make(x.mime, x.cat, strcmp(x.cat, "text") == 0 && (e == "txt" || e == "log") ? "" : up);
    }
  return Kind{};
}

std::string category_label(const std::string &c) {
  if (c == "image") return _("Pictures");
  if (c == "audio") return _("Sounds");
  if (c == "video") return _("Films");
  if (c == "archive") return _("Archives");
  if (c == "document") return _("Documents");
  if (c == "text") return _("Texts");
  if (c == "program") return _("Programs");
  return _("Other");
}

}  // namespace sniff
