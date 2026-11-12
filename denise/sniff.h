// Nirvana Desktop - Rambler: content sniffing - what a file is, from its first bytes
//
// The type comes from the data (the "magic" signature at the start of the file), not from
// its name: a PNG named "photo.dat" is a picture, a zip without an ending is an archive.
// Results are cached by path, size and modification time. Folders on servers are not
// sniffed (every file would be read over the network) - there the name decides.
#pragma once

#include <string>

namespace sniff {

struct Kind {
  std::string mime;    // "image/png", "application/zip", "text/x-shellscript"...
  std::string cat;     // image audio video archive document text program other
  std::string label;   // for people: "PNG picture" (translated)
};

Kind of(const std::string &path);                 // "" mime: unknown / not a regular file
Kind of_data(const unsigned char *d, size_t n, const std::string &name);
Kind by_name(const std::string &name);            // a guess from the ending (on servers)
bool allowed(const std::string &path);            // false on servers (no reading over the network)
std::string category_label(const std::string &cat);   // "pictures", "sounds"... (for the search)

}  // namespace sniff
