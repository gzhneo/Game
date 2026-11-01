/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// modulpruefung.cpp -- loads every SPIR-V module of a folder into a Vulkan device with the
// validation layer on.
//
// tools/vulkan/shader_spirv.py --out writes the modules. This program creates an instance with
// VK_LAYER_KHRONOS_validation, picks the first device (in the Linux checks Mesa lavapipe), enables
// every feature the device offers and creates a VkShaderModule per file. The layer checks each
// module against the device (capabilities, limits) when it is created. Any error or warning from
// the layer fails the run.
//
// Build:  g++ -std=c++20 -O1 -o modulpruefung tools/vulkan/modulpruefung.cpp -lvulkan
// Usage:  modulpruefung <folder with *.spv> [--ohne-validierung]
// Exit:   0 all modules accepted without messages, 1 a module failed or the layer reported something,
//         2 no Vulkan instance or device, or the validation layer is missing.

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_messages = 0;

VKAPI_ATTR VkBool32 VKAPI_CALL On_Message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
	if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		++g_messages;
		std::printf("   %s: %s\n", severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "Fehler" : "Warnung",
			data && data->pMessage ? data->pMessage : "?");
	}
	return VK_FALSE;
}

bool Has_Layer(const char* name)
{
	uint32_t count = 0;
	vkEnumerateInstanceLayerProperties(&count, nullptr);
	std::vector<VkLayerProperties> layers(count);
	vkEnumerateInstanceLayerProperties(&count, layers.data());
	return std::any_of(layers.begin(), layers.end(), [&](const VkLayerProperties& l) { return std::strcmp(l.layerName, name) == 0; });
}

std::vector<uint32_t> Read_Words(const std::filesystem::path& path)
{
	std::ifstream in(path, std::ios::binary);
	std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::vector<uint32_t> words(bytes.size() / 4);
	if (!words.empty()) std::memcpy(words.data(), bytes.data(), words.size() * 4);
	return words;
}

}  // namespace

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "Aufruf: modulpruefung <Ordner mit *.spv> [--ohne-validierung]\n");
		return 2;
	}
	const std::filesystem::path folder = argv[1];
	const bool validate = !(argc > 2 && std::strcmp(argv[2], "--ohne-validierung") == 0);
	const char* layer = "VK_LAYER_KHRONOS_validation";
	if (validate && !Has_Layer(layer)) {
		std::fprintf(stderr, "Validierungsschicht fehlt (Paket vulkan-validationlayers)\n");
		return 2;
	}

	VkApplicationInfo app{};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "modulpruefung";
	app.apiVersion = VK_API_VERSION_1_3;
	VkDebugUtilsMessengerCreateInfoEXT messenger_info{};
	messenger_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
	messenger_info.pfnUserCallback = On_Message;
	const char* extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	VkInstanceCreateInfo instance_info{};
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &app;
	if (validate) {
		instance_info.pNext = &messenger_info;		// also covers instance creation
		instance_info.enabledLayerCount = 1;
		instance_info.ppEnabledLayerNames = &layer;
		instance_info.enabledExtensionCount = 1;
		instance_info.ppEnabledExtensionNames = &extension;
	}
	VkInstance instance = VK_NULL_HANDLE;
	if (vkCreateInstance(&instance_info, nullptr, &instance) != VK_SUCCESS) {
		std::fprintf(stderr, "keine Vulkan-1.3-Instanz\n");
		return 2;
	}
	VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
	auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
		vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
	auto destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
		vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
	if (validate && create_messenger) create_messenger(instance, &messenger_info, nullptr, &messenger);

	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
	std::vector<VkPhysicalDevice> devices(device_count);
	vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties properties{};
	for (VkPhysicalDevice d : devices) {
		vkGetPhysicalDeviceProperties(d, &properties);
		if (properties.apiVersion >= VK_API_VERSION_1_3) { physical = d; break; }
	}
	if (!physical) {
		std::fprintf(stderr, "kein Geraet mit Vulkan 1.3\n");
		return 2;
	}

	// Enable everything the device has, so the layer only complains about what is really missing.
	VkPhysicalDeviceVulkan13Features features13{};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	VkPhysicalDeviceVulkan12Features features12{};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	VkPhysicalDeviceVulkan11Features features11{};
	features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	VkPhysicalDeviceFeatures2 features{};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features.pNext = &features11;
	features11.pNext = &features12;
	features12.pNext = &features13;
	vkGetPhysicalDeviceFeatures2(physical, &features);

	const float priority = 1.0f;
	VkDeviceQueueCreateInfo queue_info{};
	queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_info.queueFamilyIndex = 0;
	queue_info.queueCount = 1;
	queue_info.pQueuePriorities = &priority;
	VkDeviceCreateInfo device_info{};
	device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_info.pNext = &features;
	device_info.queueCreateInfoCount = 1;
	device_info.pQueueCreateInfos = &queue_info;
	VkDevice device = VK_NULL_HANDLE;
	if (vkCreateDevice(physical, &device_info, nullptr, &device) != VK_SUCCESS) {
		std::fprintf(stderr, "Geraet laesst sich nicht anlegen\n");
		return 2;
	}

	std::vector<std::filesystem::path> files;
	for (const auto& entry : std::filesystem::directory_iterator(folder)) {
		if (entry.is_regular_file() && entry.path().extension() == ".spv") files.push_back(entry.path());
	}
	std::sort(files.begin(), files.end());

	int failed = 0;
	for (const auto& file : files) {
		const int before = g_messages;
		const std::vector<uint32_t> words = Read_Words(file);
		VkShaderModuleCreateInfo module_info{};
		module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		module_info.codeSize = words.size() * 4;
		module_info.pCode = words.data();
		VkShaderModule module = VK_NULL_HANDLE;
		const VkResult result = vkCreateShaderModule(device, &module_info, nullptr, &module);
		if (result != VK_SUCCESS || g_messages != before) {
			++failed;
			std::printf("   FEHLER %s (VkResult %d)\n", file.filename().string().c_str(), static_cast<int>(result));
		}
		if (module) vkDestroyShaderModule(device, module, nullptr);
	}

	vkDestroyDevice(device, nullptr);
	if (messenger && destroy_messenger) destroy_messenger(instance, messenger, nullptr);
	vkDestroyInstance(instance, nullptr);

	std::printf("%zu Module auf %s (Vulkan %u.%u)%s: %d abgelehnt, %d Meldungen\n", files.size(), properties.deviceName,
		VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion),
		validate ? " mit Validierungsschicht" : "", failed, g_messages);
	return (failed || g_messages || files.empty()) ? 1 : 0;
}
