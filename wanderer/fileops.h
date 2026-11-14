/*
    Port to FLTK: Nirvana Desktop contributors.
    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Copying, moving, deleting and moving to the trash in the background, with a progress
    window (the source, the destination, a gauge, Abort) as Wanderer shows while it copies.
    The work runs in a thread of its own; the interface stays alive. A name that already
    exists in the destination is asked about (Replace, Replace All, Skip, Abort).
*/
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace wanderer {

enum class FileOpKind { Copy, Move, Delete, Trash, Duplicate };   // Duplicate: "Copy of NAME" beside it

// starts the operation; done() is called in the interface when it has finished (or was
// aborted) - to update the windows
void file_operation(FileOpKind kind, std::vector<std::string> sources, std::string dest,
                    std::function<void()> done);

// operations still running (Quit asks about them)
int file_operations_running();

}  // namespace wanderer
