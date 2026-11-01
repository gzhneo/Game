/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// wwtracy.cpp -- see wwtracy.h.

#include "wwtracy.h"

#include <windows.h>

void WWDebug_Set_Thread_Name(const char *name)
{
	if (name == nullptr)
		return;

	// SetThreadDescription requires Windows 10 1607 or later, which is the minimum target.
	wchar_t wide[64];
	if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, static_cast<int>(sizeof(wide) / sizeof(wide[0]))) > 0)
		SetThreadDescription(GetCurrentThread(), wide);

#ifdef TRACY_ENABLE
	tracy::SetThreadName(name);
#endif
}
