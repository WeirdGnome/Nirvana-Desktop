/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Editor (workbench/tools/Editor, EditPad): a plain text editor. Project (New,
    Open..., Save, Save As..., Print, About..., Quit), Edit (Undo, Cut, Copy, Paste, Select
    All), Search (Find..., Find Next, Replace..., Go to Line...), Settings (Word Wrap). The
    title shows the file's name, with a star while it has changes that are not saved; they
    are asked about before the text goes.
*/
#pragma once
#include <string>

namespace rambler {

void run_editor(const std::string &file);

}  // namespace rambler
