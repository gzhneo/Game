/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Force-included by tools/fuzz/linux/run.sh only: the few MSVC spellings the parsers under test need
// so they build natively on Linux with clang and libFuzzer. Never part of a game build.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <strings.h>

#define __int64 long long
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define __cdecl
#define _cdecl
#define __forceinline inline
