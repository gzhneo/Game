/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Shared between the engine harnesses and their stand-ins (stubs.cpp).

#pragma once

namespace Harness
{
inline bool verbose = false;		///< print the debug log and every assertion
inline long long assertions = 0;	///< DEBUG_CRASH/DEBUG_ASSERTCRASH hits so far
}
