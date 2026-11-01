/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkanloader.h ---------------------------------------------------------------------------------
//
// The Vulkan API for the backend files, through volk: the game does not link vulkan-1.lib, volk
// loads vulkan-1.dll at runtime (volkInitialize), so the game starts on systems without a Vulkan
// driver and only -vulkan fails. The volk implementation is compiled once, in vulkanloader.cpp.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef VULKANLOADER_H
#define VULKANLOADER_H

#include <windows.h>	// vulkan_win32.h needs HWND, HINSTANCE, ... but does not include it

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <volk.h>

/// Loads vulkan-1.dll on the first call and keeps it until process exit; false without a loader.
bool Vulkan_Loader_Ready();

#endif // VULKANLOADER_H
