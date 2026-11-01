/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkanbackend.cpp -- see vulkanbackend.h.

#include "vulkanloader.h"
#include "vulkanbackend.h"
#include "vulkandevice.h"
#include "WW3D2/Common/gpulayers.h"
#include "vulkandxc.h"
#include "wwdebug.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

bool Has_Extension(const std::vector<VkExtensionProperties>& list, const char* name)
{
	for (const VkExtensionProperties& e : list) {
		if (std::strcmp(e.extensionName, name) == 0) return true;
	}
	return false;
}

bool Has_Format_Features(VkPhysicalDevice device, VkFormat format, VkFormatFeatureFlags wanted)
{
	VkFormatProperties properties = {};
	vkGetPhysicalDeviceFormatProperties(device, format, &properties);
	return (properties.optimalTilingFeatures & wanted) == wanted;
}

void Log_Device([[maybe_unused]] unsigned index, const VulkanDeviceInfo& info)
{
	char api[32], missing[512];
	Vulkan_Version_Text(info.api_version, api, sizeof(api));
	Vulkan_Describe_Missing(Vulkan_Missing_Requirements(info), missing, sizeof(missing));
	WWDEBUG_SAY(("Vulkan: device %u '%s' (%s, vendor 0x%04X, device 0x%04X), API %s, driver 0x%08X, %llu MB local: %s%s\n",
		index, info.name, Vulkan_Device_Type_Name(info.type), info.vendor_id, info.device_id, api, info.driver_version,
		info.local_memory >> 20, missing[0] ? "missing " : "usable", missing));
}

}  // namespace

void Vulkan_Describe_Device(VkPhysicalDevice device, VulkanDeviceInfo& info)
{
	std::memset(&info, 0, sizeof(info));

	VkPhysicalDeviceProperties properties = {};
	vkGetPhysicalDeviceProperties(device, &properties);
	std::snprintf(info.name, sizeof(info.name), "%s", properties.deviceName);
	info.api_version = properties.apiVersion;
	info.driver_version = properties.driverVersion;
	info.vendor_id = properties.vendorID;
	info.device_id = properties.deviceID;
	info.type = static_cast<int>(properties.deviceType);

	uint32_t count = 0;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> extensions(count);
	if (count) vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
	extensions.resize(count);
	info.swapchain = Has_Extension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

	// The 1.3 feature structure may only be chained on a 1.3 device.
	VkPhysicalDeviceFeatures2 features = {};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	VkPhysicalDeviceVulkan13Features features13 = {};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	const bool is13 = properties.apiVersion >= VK_API_VERSION_1_3;
	if (is13) features.pNext = &features13;
	vkGetPhysicalDeviceFeatures2(device, &features);
	info.dynamic_rendering = is13 && features13.dynamicRendering;
	info.synchronization2 = is13 && features13.synchronization2;
	info.tessellation = features.features.tessellationShader != VK_FALSE;
	info.anisotropy = features.features.samplerAnisotropy != VK_FALSE;
	info.bc_textures = features.features.textureCompressionBC != VK_FALSE;
	info.depth_clamp = features.features.depthClamp != VK_FALSE;
	info.depth_bias_clamp = features.features.depthBiasClamp != VK_FALSE;

	uint32_t families = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &families, nullptr);
	std::vector<VkQueueFamilyProperties> family(families);
	if (families) vkGetPhysicalDeviceQueueFamilyProperties(device, &families, family.data());
	for (uint32_t i = 0; i < families && i < family.size(); ++i) {
		if ((family[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && vkGetPhysicalDeviceWin32PresentationSupportKHR(device, i)) {
			info.present = true;
			break;
		}
	}

	info.bgra_target = Has_Format_Features(device, VK_FORMAT_B8G8R8A8_UNORM,
		VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
	info.depth_d24s8 = Has_Format_Features(device, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
	info.depth_d32s8 = Has_Format_Features(device, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

	VkPhysicalDeviceMemoryProperties memory = {};
	vkGetPhysicalDeviceMemoryProperties(device, &memory);
	for (uint32_t h = 0; h < memory.memoryHeapCount; ++h) {
		if (memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) info.local_memory += memory.memoryHeaps[h].size;
	}
}

bool Vulkan_Probe(VulkanProbeResult& result)
{
	std::memset(&result, 0, sizeof(result));
	result.chosen = -1;

	if (!Vulkan_Loader_Ready()) {
		std::snprintf(result.error, sizeof(result.error), "Vulkan loader (vulkan-1.dll) not found");
		WWDEBUG_SAY(("Vulkan: %s\n", result.error));
		return false;
	}
	result.loader = true;
	result.instance_version = volkGetInstanceVersion();
	char version[32];
	Vulkan_Version_Text(result.instance_version, version, sizeof(version));
	if (result.instance_version < VK_API_VERSION_1_3) {
		std::snprintf(result.error, sizeof(result.error), "Vulkan runtime %s, at least 1.3 is needed", version);
		WWDEBUG_SAY(("Vulkan: %s\n", result.error));
		return false;
	}

	uint32_t count = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> extensions(count);
	if (count) vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
	extensions.resize(count);
	const char* const wanted[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
	for (const char* name : wanted) {
		if (!Has_Extension(extensions, name)) {
			std::snprintf(result.error, sizeof(result.error), "Vulkan runtime %s without %s", version, name);
			WWDEBUG_SAY(("Vulkan: %s\n", result.error));
			return false;
		}
	}

	VkApplicationInfo app = {};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "GZH Neo";
	app.pEngineName = "WW3D";
	app.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo create = {};
	create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create.pApplicationInfo = &app;
	create.enabledExtensionCount = static_cast<uint32_t>(sizeof(wanted) / sizeof(wanted[0]));
	create.ppEnabledExtensionNames = wanted;
	VkInstance instance = VK_NULL_HANDLE;
	const VkResult created = vkCreateInstance(&create, nullptr, &instance);
	if (created != VK_SUCCESS) {
		std::snprintf(result.error, sizeof(result.error), "vkCreateInstance failed (%d)", static_cast<int>(created));
		WWDEBUG_SAY(("Vulkan: %s\n", result.error));
		return false;
	}
	volkLoadInstanceOnly(instance);
	result.instance = true;

	uint32_t devices = 0;
	vkEnumeratePhysicalDevices(instance, &devices, nullptr);
	std::vector<VkPhysicalDevice> physical(devices);
	if (devices) vkEnumeratePhysicalDevices(instance, &devices, physical.data());
	physical.resize(devices);
	WWDEBUG_SAY(("Vulkan: runtime %s, %u device(s)\n", version, devices));
	for (VkPhysicalDevice device : physical) {
		if (result.device_count >= VulkanProbeResult::MAX_DEVICES) break;
		VulkanDeviceInfo& info = result.devices[result.device_count];
		Vulkan_Describe_Device(device, info);
		Log_Device(result.device_count, info);
		++result.device_count;
	}

	vkDestroyInstance(instance, nullptr);

	result.chosen = Vulkan_Choose_Device(result.devices, result.device_count);
	if (result.chosen < 0) {
		if (result.device_count == 0) {
			std::snprintf(result.error, sizeof(result.error), "no Vulkan device");
		}
		else {
			// Name the device that would be preferred and what it lacks.
			char missing[200];
			Vulkan_Describe_Missing(Vulkan_Missing_Requirements(result.devices[0]), missing, sizeof(missing));
			std::snprintf(result.error, sizeof(result.error), "no usable Vulkan device; '%s' lacks %s",
				result.devices[0].name, missing);
		}
		WWDEBUG_SAY(("Vulkan: %s\n", result.error));
		return false;
	}
	WWDEBUG_SAY(("Vulkan: device %d '%s' would be used\n", result.chosen, result.devices[result.chosen].name));
	return true;
}

static bool Vulkan_Layer_Probe(char* reason, unsigned reason_size)
{
	VulkanProbeResult probe;
	if (!Vulkan_Probe(probe)) {
		if (reason && reason_size) std::snprintf(reason, reason_size, "%s", probe.error);
		return false;
	}
	// The shader compiler is part of the game, but a missing DLL must not end in a black screen.
	char missing[256] = "";
	if (!Dxc_Load(missing, sizeof(missing))) {
		if (reason && reason_size) std::snprintf(reason, reason_size, "%s", missing);
		WWDEBUG_SAY(("Vulkan: %s\n", missing));
		return false;
	}
	return true;
}

void Gpu_Register_Vulkan()
{
	static const GpuLayerInfo Info = {
		"vulkan", "Vulkan", "a graphics card with Vulkan 1.3 and a current graphics driver",
		10, Vulkan_Layer_Probe, Vulkan_Get_Gpu_Output, Vulkan_Device_Error };
	Gpu_Register_Layer(Info);
}
