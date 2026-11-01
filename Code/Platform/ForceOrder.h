/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Force-included (/FI) ahead of every GameEngineDevice .cpp file. These files
// do not include "PreRTS.h", which otherwise guarantees the order
// windows.h -> Lib/BaseType.h. Without that order either BaseType.h's
// "BitTest(x,i)" macro (bitwise AND) or winnt.h's "BitTest" (takes a pointer)
// wins, depending on the include chain. Including windows.h first lets its
// include guard block any later redefinition, so BaseType.h's version wins.
#include <windows.h>
// WIN32_LEAN_AND_MEAN (set globally) hides mmsystem.h/timeGetTime; files
// without PreRTS.h need it from here.
#include <mmsystem.h>
#include "Lib/BaseType.h"
