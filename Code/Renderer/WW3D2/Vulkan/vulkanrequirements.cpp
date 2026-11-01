/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkanrequirements.cpp -- see vulkanrequirements.h.

#include "vulkanrequirements.h"

#include <cstdio>
#include <cstring>

namespace {

// Version layout of VK_MAKE_API_VERSION: variant 3 bits, major 7, minor 10, patch 12.
constexpr unsigned Major(unsigned v) { return (v >> 22) & 0x7Fu; }
constexpr unsigned Minor(unsigned v) { return (v >> 12) & 0x3FFu; }
constexpr unsigned Patch(unsigned v) { return v & 0xFFFu; }

const char* const MISSING_NAMES[VULKAN_MISSING_COUNT] = {
	"Vulkan 1.3",
	"VK_KHR_swapchain",
	"graphics queue with window presentation",
	"dynamicRendering",
	"synchronization2",
	"tessellationShader",
	"samplerAnisotropy",
	"textureCompressionBC",
	"depthClamp",
	"depthBiasClamp",
	"B8G8R8A8_UNORM render target",
	"depth/stencil format D24S8 or D32S8",
};

// Higher is preferred.
int Type_Rank(int type)
{
	switch (type) {
	case VULKAN_DEVICE_DISCRETE:	return 4;
	case VULKAN_DEVICE_INTEGRATED:	return 3;
	case VULKAN_DEVICE_VIRTUAL:		return 2;
	case VULKAN_DEVICE_CPU:			return 0;
	default:						return 1;
	}
}

}  // namespace

unsigned Vulkan_Missing_Requirements(const VulkanDeviceInfo& d)
{
	unsigned missing = 0;
	if (Major(d.api_version) < 1 || (Major(d.api_version) == 1 && Minor(d.api_version) < 3))
		missing |= VULKAN_MISSING_API_1_3;
	if (!d.swapchain)			missing |= VULKAN_MISSING_SWAPCHAIN;
	if (!d.present)				missing |= VULKAN_MISSING_PRESENT;
	if (!d.dynamic_rendering)	missing |= VULKAN_MISSING_DYNAMIC_RENDERING;
	if (!d.synchronization2)	missing |= VULKAN_MISSING_SYNCHRONIZATION2;
	if (!d.tessellation)		missing |= VULKAN_MISSING_TESSELLATION;
	if (!d.anisotropy)			missing |= VULKAN_MISSING_ANISOTROPY;
	if (!d.bc_textures)			missing |= VULKAN_MISSING_BC_TEXTURES;
	if (!d.depth_clamp)			missing |= VULKAN_MISSING_DEPTH_CLAMP;
	if (!d.depth_bias_clamp)	missing |= VULKAN_MISSING_DEPTH_BIAS_CLAMP;
	if (!d.bgra_target)			missing |= VULKAN_MISSING_BGRA_TARGET;
	if (!d.depth_d24s8 && !d.depth_d32s8) missing |= VULKAN_MISSING_DEPTH_STENCIL;
	return missing;
}

void Vulkan_Describe_Missing(unsigned missing, char* text, unsigned size)
{
	if (!text || size == 0) return;
	text[0] = 0;
	size_t used = 0;
	for (unsigned bit = 0; bit < VULKAN_MISSING_COUNT; ++bit) {
		if (!(missing & (1u << bit))) continue;
		const int n = std::snprintf(text + used, size - used, "%s%s", used ? ", " : "", MISSING_NAMES[bit]);
		if (n < 0 || used + static_cast<size_t>(n) >= size) {
			text[size - 1] = 0;
			return;
		}
		used += static_cast<size_t>(n);
	}
}

int Vulkan_Choose_Device(const VulkanDeviceInfo* devices, unsigned count)
{
	int best = -1;
	for (unsigned i = 0; i < count; ++i) {
		const VulkanDeviceInfo& d = devices[i];
		if (Vulkan_Missing_Requirements(d) != 0) continue;
		if (best < 0) { best = static_cast<int>(i); continue; }
		const VulkanDeviceInfo& b = devices[best];
		const int rank = Type_Rank(d.type), best_rank = Type_Rank(b.type);
		if (rank > best_rank || (rank == best_rank && d.local_memory > b.local_memory))
			best = static_cast<int>(i);
	}
	return best;
}

const char* Vulkan_Device_Type_Name(int type)
{
	switch (type) {
	case VULKAN_DEVICE_INTEGRATED:	return "integrated";
	case VULKAN_DEVICE_DISCRETE:	return "discrete";
	case VULKAN_DEVICE_VIRTUAL:		return "virtual";
	case VULKAN_DEVICE_CPU:			return "cpu";
	default:						return "other";
	}
}

void Vulkan_Version_Text(unsigned version, char* text, unsigned size)
{
	if (!text || size == 0) return;
	std::snprintf(text, size, "%u.%u.%u", Major(version), Minor(version), Patch(version));
}
