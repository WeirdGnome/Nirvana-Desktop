// Nirvana Desktop - Rambler: metadata - what is inside a file, for the information panel,
// the preview and the search
//
// Read by Rambler itself from the file's headers (no libraries): picture sizes and the
// camera's data (EXIF), MP3 tags (ID3v1/v2) with the length, FLAC and Ogg comments, WAV
// format, PDF pages and title, SVG size, program architecture, lines of text. The slow
// part (films through ffprobe, the number of files in an archive through bsdtar) is
// separate - the panel runs it in a thread.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "sniff.h"

namespace meta {

using Fields = std::vector<std::pair<std::string, std::string>>;   // (label, value), translated

Fields read(const std::string &path, const sniff::Kind &k);        // fast, safe in any thread
Fields read_slow(const std::string &path, const sniff::Kind &k);   // runs programs - in a thread
std::string text_of(const Fields &f);                              // all values, for searching

}  // namespace meta
