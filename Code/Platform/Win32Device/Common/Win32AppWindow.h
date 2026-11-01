/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Win32AppWindow.h -- the application instance and main window of the running program.
//
// Defined by the program (App/WinMain.cpp, the editors' engine DLL); declared here so the platform layer
// does not include the program's headers.

#pragma once

#include <windows.h>

extern HINSTANCE ApplicationHInstance;  ///< our application instance
extern HWND ApplicationHWnd;  ///< our application window handle
