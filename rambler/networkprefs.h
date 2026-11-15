/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Network preferences (workbench/prefs/network): the interfaces (up or not, an
    address by DHCP or a fixed one with its netmask and gateway), the host name and the
    name servers, and the wireless networks. The original configures its TCP/IP stack;
    here NetworkManager does the work (nmcli). Use changes the interface for now only
    (nmcli device modify: as ENV:), Save changes its connection, which is kept (as ENVARC:).
*/
#pragma once

namespace rambler {

void edit_network_prefs(bool standalone);

}  // namespace rambler
