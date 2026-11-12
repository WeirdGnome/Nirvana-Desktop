// Nirvana Desktop - Rambler: the desktop's own sounds (emptying the Trash)
//
// Made by Nirvana itself (synthesized, nothing recorded or copied) into WAV files in
// $XDG_RUNTIME_DIR the first time. Your own file replaces a sound:
//   ~/.local/share/nirvana/sounds/<name>.wav (or .oga, .ogg) - an empty file: no sound.
#pragma once

#include <string>

namespace sounds {

void play(const std::string &name);   // "trash-empty"

}  // namespace sounds
