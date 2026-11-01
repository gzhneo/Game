/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// gpudisplay.h -----------------------------------------------------------------------------------
//
// Properties of the monitor, the same for every device layer: the display modes and the desktop size of
// the primary monitor, read with Win32 (no DXGI, no Vulkan).
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef GPUDISPLAY_H
#define GPUDISPLAY_H

/// Reads the 32 bit display modes of the primary monitor again: each size once, ascending by width, then
/// height. Returns the count; Gpu_Display_Mode reads from this list until the next refresh.
unsigned	Gpu_Display_Refresh_Modes();
/// Size of mode index in the list of the last Gpu_Display_Refresh_Modes.
bool		Gpu_Display_Mode(unsigned index, unsigned* width, unsigned* height);
/// Size of the primary monitor's desktop in pixels.
bool		Gpu_Display_Desktop_Size(unsigned* width, unsigned* height);

#endif // GPUDISPLAY_H
