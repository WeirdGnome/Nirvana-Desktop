/*
    Copyright (C) 2004-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The tools Wanderer starts (Wanderer/Tools): Execute Command, New Drawer, Rename,
    Information, Disk Info - and a requester (the EasyRequest of the system).
*/
#pragma once
#include <string>
#include <functional>
#include <vector>

namespace wanderer {

// A requester: a title, a text, the buttons ("Yes|No"); returns the button's number counted
// like EasyRequest: the first is 1, ..., the last is 0
int request(const std::string &title, const std::string &text, const std::string &buttons);

void execute_command(const std::string &dir);              // Tools/ExecuteCommand
bool new_drawer(const std::string &dir, std::string &made); // Tools/WBNewDrawer
bool rename_entry(const std::string &path, std::string &renamed);   // Tools/WBRename
void information(const std::string &path);                  // Tools/Info
std::vector<std::string> tool_types(const std::string &path);    // the tool types of a file
extern std::function<void(const std::string &)> open_drawer_hook;  // Info's "Open" (the path)
std::string default_tool(const std::string &path);          // a project's default tool ("" - none)
void set_default_tool(const std::string &path, const std::string &tool);
std::string file_comment(const std::string &path);
void disk_info(const std::string &volume);                  // Tools/DiskInfo

// the trash can (the freedesktop.org trash: files/ and info/*.trashinfo)
bool move_to_trash(const std::string &path, std::string &error);
void empty_trash();
// Format: a removable disk gets a new file system (polkit / pkexec)
void format_disk(const std::string &device, const std::string &label);
// the assigns' editor (~/.config/wanderer/assigns)
void edit_assigns();
// Find: a pattern asked for; false - cancelled
bool ask_find(std::string &pattern, std::string &contents);   // contents: "" - names only

// A file requester (as the system's ASL file requester): the list of the drawer, the
// Drawer and File fields, OK / Volumes / Parent / Cancel. Returns the path chosen.
bool file_request(const std::string &title, std::string &path);

// .desktop files
struct DesktopEntry {
    std::string name, exec, try_exec, only_show_in, not_show_in;
    bool hidden = false, no_display = false, terminal = false;
};
bool read_desktop_entry(const std::string &file, DesktopEntry &e);

// the Startup drawer and XDG autostart (when Wanderer starts)
void run_startup();

// the Tools menu: label and the command (for /bin/sh -c)
struct ToolItem { std::string label, command; };
std::vector<ToolItem> tool_items();

// a command line with its volume paths and assigns turned into the drawers they name
std::string volume_paths_in_command(const std::string &cmd);
// runs a command line; what it prints shows in an output window (none without output)
void run_with_output(const std::string &command, const std::string &dir, const std::string &title = "");

// starts a program / opens a file with the system's handler, detached
void launch(const std::vector<std::string> &argv, const std::string &dir);

}  // namespace wanderer
