/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Force-included by tools/wine/build_game.sh only; never part of the MSVC build.
// Declarations the Windows 10 SDK has but the MinGW-w64 headers (11.x) lack. Plain types so the
// header does not pull in windows.h and change the include order the build relies on.
#pragma once

extern "C" __declspec(dllimport) long __stdcall SetThreadDescription(void *thread, const wchar_t *description);

// D3D11_RLDO_FLAGS gained this value after the MinGW d3d11sdklayers.h snapshot.
#define D3D11_RLDO_IGNORE_INTERNAL 4
