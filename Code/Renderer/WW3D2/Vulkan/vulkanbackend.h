/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkanbackend.h --------------------------------------------------------------------------------
//
// The Vulkan render backend: the probe (load the loader, create a Vulkan 1.3 instance, describe every
// device and pick the one the backend uses, vulkanrequirements.h) and the registration of the Vulkan
// device layer (vulkandevice.h, gpulayers.h), usable when a device and the shader compiler are there;
// otherwise RenderDevice::Init starts the next layer with the reason.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef VULKANBACKEND_H
#define VULKANBACKEND_H

#include "vulkanrequirements.h"

struct VulkanProbeResult
{
	enum { MAX_DEVICES = 8 };
	bool				loader;				///< vulkan-1.dll found
	unsigned			instance_version;	///< VK_MAKE_API_VERSION layout, 0 if unknown
	bool				instance;			///< Vulkan 1.3 instance with window surfaces created
	unsigned			device_count;		///< devices described (at most MAX_DEVICES)
	VulkanDeviceInfo	devices[MAX_DEVICES];
	int					chosen;				///< Vulkan_Choose_Device, -1 = none usable
	char				error[256];			///< why no device is usable, empty otherwise
};

/// Runs the probe, logs each device and leaves no Vulkan object behind. true if a device is usable.
bool Vulkan_Probe(VulkanProbeResult& result);

#endif // VULKANBACKEND_H
