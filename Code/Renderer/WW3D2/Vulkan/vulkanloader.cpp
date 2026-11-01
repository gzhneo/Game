/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkanloader.cpp -- the volk implementation (third-party C code, MIT), compiled as C++ in this one
// translation unit. Its warnings are not ours to fix, so they are off here; WW3D2 builds with /W4 /WX.

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif

#define VOLK_IMPLEMENTATION
#include "vulkanloader.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

bool Vulkan_Loader_Ready()
{
	// Never unloaded: unloading vulkan-1.dll unloads the driver and its layers too, and the process
	// loads them again for the device after the probe or keeps running on Direct3D 11 after a fall back.
	static const bool ready = volkInitialize() == VK_SUCCESS;
	return ready;
}
