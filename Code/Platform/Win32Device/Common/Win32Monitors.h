/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Win32Monitors.h -- which monitor the game window is on: the choice at start (-bildschirm, the monitor
// of the last run), the size of the window's monitor, the DPI for the cursor size.

#pragma once

#ifndef WIN32DEVICE_WIN32MONITORS_H
#define WIN32DEVICE_WIN32MONITORS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "Common/AsciiString.h"

/// Moves the window onto a monitor: `index` from 1 (1 is the primary one, the others in the order
/// Windows lists them), else the one with this device name ("\\.\DISPLAY2"). A window keeps its size
/// and is centered in the monitor's work area; borderless and fullscreen windows cover the monitor.
/// False if neither names a monitor that exists.
Bool PlaceWindowOnMonitor( HWND window, Int index, const AsciiString &deviceName, Bool coverMonitor );

/// Size of the monitor the window is (mostly) on.
Bool WindowMonitorSize( HWND window, Int &width, Int &height );

/// Device name of the monitor the window is on; empty if unknown.
AsciiString WindowMonitorName( HWND window );

/// DPI of the window's monitor; 96 where Windows cannot tell (before Windows 10 1607).
UnsignedInt WindowDpi( HWND window );

#endif // WIN32DEVICE_WIN32MONITORS_H
