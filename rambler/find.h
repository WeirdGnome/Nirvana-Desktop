/*
    Copyright (C) 2016-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    Find (workbench/system/find): a window to search a drawer and the drawers in it for names
    matching a pattern and, if given, files containing a text. The results are a list with
    columns (full path, size, attributes, date, time, comment); Open, View and Drawer act on the
    active one.
*/
#pragma once
#include <functional>
#include <string>

namespace rambler {

// open: open a path as Rambler does (a double click on its icon); drawer: show a drawer
void find_tool(const std::string &start, std::function<void(const std::string &)> open,
               std::function<void(const std::string &)> drawer);

}  // namespace rambler
