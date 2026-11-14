/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The tools Wanderer starts (Wanderer/Tools): Execute Command, New Drawer, Rename,
    Information, Disk Info - and a requester (the EasyRequest of the system).
*/
#pragma once
#include <string>
#include <vector>

namespace wanderer {

// A requester: a title, a text, the buttons ("Yes|No"); returns the button's number counted
// like EasyRequest: the first is 1, ..., the last is 0
int request(const std::string &title, const std::string &text, const std::string &buttons);

void execute_command(const std::string &dir);              // Tools/ExecuteCommand
bool new_drawer(const std::string &dir, std::string &made); // Tools/WBNewDrawer
bool rename_entry(const std::string &path, std::string &renamed);   // Tools/WBRename
void information(const std::string &path);                  // Tools/Info
void disk_info(const std::string &volume);                  // Tools/DiskInfo

// the trash can (the freedesktop.org trash: files/ and info/*.trashinfo)
bool move_to_trash(const std::string &path, std::string &error);
void empty_trash();
// Find: a pattern asked for; false - cancelled
bool ask_find(std::string &pattern);

// A file requester (as the system's ASL file requester): the list of the drawer, the
// Drawer and File fields, OK / Volumes / Parent / Cancel. Returns the path chosen.
bool file_request(const std::string &title, std::string &path);

// starts a program / opens a file with the system's handler, detached
void launch(const std::vector<std::string> &argv, const std::string &dir);

}  // namespace wanderer
