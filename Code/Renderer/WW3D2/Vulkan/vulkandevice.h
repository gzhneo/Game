/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkandevice.h ---------------------------------------------------------------------------------
//
// The Vulkan device layer (gpudevice.h). The render core runs on it unchanged; see
// vulkandevice_internal.h for how it meets the Direct3D 11 semantics.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef VULKANDEVICE_H
#define VULKANDEVICE_H

#include "WW3D2/Common/gpudevice.h"
#include "vulkanrequirements.h"

/// The Vulkan output (static object; Release_Interface at the end). Its device is the one the
/// probe chooses (Vulkan_Choose_Device).
GpuOutput*	Vulkan_Get_Gpu_Output();
/// Why the last Create_Device of the Vulkan output failed.
const char*	Vulkan_Device_Error();

#ifdef VK_VERSION_1_0
/// Fills the requirement view of a device (vulkanbackend.cpp).
void		Vulkan_Describe_Device(VkPhysicalDevice device, VulkanDeviceInfo& info);
#endif

#endif // VULKANDEVICE_H
