/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SecuritySoftware.h -- which firewalls may drop the game's network traffic on this computer.
//
// For the router log: when the router's answers do not arrive, a firewall on this computer is a
// likely cause. Reports the Windows Firewall (on or off in the current profile, rules for this
// program) and the firewalls and virus scanners registered with the Windows Security Center. It
// cannot tell whether one of them actually drops a packet, only which ones could.

#pragma once

#include <string>
#include <vector>

/// One line per finding, in English. Calls COM; run it on a worker thread (it initialises COM there).
std::vector<std::string> DescribeSecuritySoftware();
