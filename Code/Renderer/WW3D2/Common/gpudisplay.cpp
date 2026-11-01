/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// gpudisplay.cpp -- see gpudisplay.h.

#include "gpudisplay.h"

#include <windows.h>
#include <algorithm>
#include <utility>

namespace {

// Refreshed once per enumeration: the engine asks for the count, then for each mode by index.
// A fixed array, not a vector: a heap block alive in a global would show up in the engine's leak
// report at shutdown and be freed after the debug system is gone.
const unsigned MAX_MODES = 512;
std::pair<unsigned, unsigned> Modes[MAX_MODES];
unsigned ModeCount = 0;

}

unsigned Gpu_Display_Refresh_Modes()
{
	ModeCount = 0;
	DEVMODEW dm;
	for (DWORD i = 0;; ++i) {
		::ZeroMemory(&dm, sizeof(dm));
		dm.dmSize = sizeof(dm);
		if (!::EnumDisplaySettingsW(nullptr, i, &dm)) break;
		// The list has every refresh rate and colour depth; the engine needs the sizes at 32 bit.
		if (dm.dmBitsPerPel != 32 || dm.dmPelsWidth == 0 || dm.dmPelsHeight == 0) continue;
		if (ModeCount == MAX_MODES) break;
		Modes[ModeCount++] = std::make_pair(dm.dmPelsWidth, dm.dmPelsHeight);
	}
	std::sort(Modes, Modes + ModeCount);
	ModeCount = static_cast<unsigned>(std::unique(Modes, Modes + ModeCount) - Modes);
	return ModeCount;
}

bool Gpu_Display_Mode(unsigned index, unsigned* width, unsigned* height)
{
	if (index >= ModeCount) return false;
	if (width)  *width  = Modes[index].first;
	if (height) *height = Modes[index].second;
	return true;
}

bool Gpu_Display_Desktop_Size(unsigned* width, unsigned* height)
{
	const POINT origin = { 0, 0 };
	MONITORINFO info;
	::ZeroMemory(&info, sizeof(info));
	info.cbSize = sizeof(info);
	if (!::GetMonitorInfoW(::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &info)) return false;
	if (width)  *width  = static_cast<unsigned>(info.rcMonitor.right - info.rcMonitor.left);
	if (height) *height = static_cast<unsigned>(info.rcMonitor.bottom - info.rcMonitor.top);
	return true;
}
