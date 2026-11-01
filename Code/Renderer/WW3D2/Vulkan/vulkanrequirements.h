/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkanrequirements.h ---------------------------------------------------------------------------
//
// What the Vulkan backend needs from a device and which device it takes. Deliberately without the
// Vulkan headers: the probe (vulkanbackend.cpp) fills VulkanDeviceInfo from the driver, the rules
// here stay testable without a device (RenderUnitTests).
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef VULKANREQUIREMENTS_H
#define VULKANREQUIREMENTS_H

/// Device types with the values of VkPhysicalDeviceType.
enum VulkanDeviceType
{
	VULKAN_DEVICE_OTHER = 0,
	VULKAN_DEVICE_INTEGRATED = 1,
	VULKAN_DEVICE_DISCRETE = 2,
	VULKAN_DEVICE_VIRTUAL = 3,
	VULKAN_DEVICE_CPU = 4,
};

/// One bit per requirement a device does not meet (Vulkan_Missing_Requirements).
enum VulkanMissing : unsigned
{
	VULKAN_MISSING_API_1_3          = 1u << 0,	///< Vulkan 1.3
	VULKAN_MISSING_SWAPCHAIN        = 1u << 1,	///< VK_KHR_swapchain
	VULKAN_MISSING_PRESENT          = 1u << 2,	///< a graphics queue that can present to a window
	VULKAN_MISSING_DYNAMIC_RENDERING = 1u << 3,	///< dynamicRendering
	VULKAN_MISSING_SYNCHRONIZATION2 = 1u << 4,	///< synchronization2
	VULKAN_MISSING_TESSELLATION     = 1u << 5,	///< tessellationShader (terrain tessellation)
	VULKAN_MISSING_ANISOTROPY       = 1u << 6,	///< samplerAnisotropy
	VULKAN_MISSING_BC_TEXTURES      = 1u << 7,	///< textureCompressionBC (DXT and BC7 textures)
	VULKAN_MISSING_DEPTH_CLAMP      = 1u << 8,	///< depthClamp (shadow map without depth clipping)
	VULKAN_MISSING_DEPTH_BIAS_CLAMP = 1u << 9,	///< depthBiasClamp (shadow map bias)
	VULKAN_MISSING_BGRA_TARGET      = 1u << 10,	///< B8G8R8A8_UNORM as color target and sampled image
	VULKAN_MISSING_DEPTH_STENCIL    = 1u << 11,	///< D24_UNORM_S8_UINT or D32_SFLOAT_S8_UINT as depth target
	VULKAN_MISSING_COUNT            = 12
};

/// What the probe found out about one device.
struct VulkanDeviceInfo
{
	char				name[256];
	unsigned			api_version;		///< VK_MAKE_API_VERSION layout
	unsigned			driver_version;
	unsigned			vendor_id;
	unsigned			device_id;
	int					type;				///< VulkanDeviceType
	unsigned long long	local_memory;		///< bytes in device local heaps
	bool				swapchain;
	bool				present;
	bool				dynamic_rendering;
	bool				synchronization2;
	bool				tessellation;
	bool				anisotropy;
	bool				bc_textures;
	bool				depth_clamp;
	bool				depth_bias_clamp;
	bool				bgra_target;
	bool				depth_d24s8;		///< D24_UNORM_S8_UINT (AMD has none)
	bool				depth_d32s8;		///< D32_SFLOAT_S8_UINT
};

/// VULKAN_MISSING_* bits of everything the device lacks; 0 = usable.
unsigned	Vulkan_Missing_Requirements(const VulkanDeviceInfo& device);
/// Comma separated names of the missing requirements ("Vulkan 1.3, VK_KHR_swapchain").
void		Vulkan_Describe_Missing(unsigned missing, char* text, unsigned size);
/// The device the backend takes: a usable one, discrete before integrated before virtual before
/// other before CPU (software renderers only when nothing else is there), then the larger local
/// memory, then the lower index. -1 if no device is usable.
int			Vulkan_Choose_Device(const VulkanDeviceInfo* devices, unsigned count);
/// "discrete", "integrated", ...
const char*	Vulkan_Device_Type_Name(int type);
/// "1.3.275" from an API or driver version in VK_MAKE_API_VERSION layout.
void		Vulkan_Version_Text(unsigned version, char* text, unsigned size);

#endif // VULKANREQUIREMENTS_H
