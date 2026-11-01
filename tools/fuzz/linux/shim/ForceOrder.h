/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/
// Native fuzz builds only (tools/fuzz/linux/run.sh): stands in for Platform/ForceOrder.h,
// which pulls in windows.h. The parsers under test only need the base types.
#pragma once
#include "Lib/BaseType.h"
#include <cstdint>
// Windows integer types with their Windows sizes (DWORD is 32 bits there).
typedef std::uint8_t BYTE;
typedef std::uint16_t WORD;
typedef std::uint32_t DWORD;
