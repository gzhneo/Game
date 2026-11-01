/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkandevice.cpp -- the Vulkan output (instance, adapter, swap chain) and the device's frames and
// submissions. See vulkandevice_internal.h for the overall design.

#include "vulkandevice_internal.h"
#include "vulkandevice.h"
#include "vulkanbackend.h"
#include "vulkandxc.h"
#include "wwdebug.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>

namespace vkdev {

namespace {

const HRESULT HR_DEVICE_REMOVED = static_cast<HRESULT>(0x887A0005L);	// DXGI_ERROR_DEVICE_REMOVED
const VkDeviceSize UPLOAD_CHUNK_BYTES = 8u << 20;

#define S_ID	VK_COMPONENT_SWIZZLE_IDENTITY
#define S_ZERO	VK_COMPONENT_SWIZZLE_ZERO
#define S_ONE	VK_COMPONENT_SWIZZLE_ONE
#define S_R		VK_COMPONENT_SWIZZLE_R
#define COLOR	VK_IMAGE_ASPECT_COLOR_BIT
#define DEPTH	VK_IMAGE_ASPECT_DEPTH_BIT
#define DEPTH_STENCIL	(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)

const FormatInfo FORMAT_NONE         = { VK_FORMAT_UNDEFINED, { S_ID, S_ID, S_ID, S_ID }, COLOR, 0, 1 };
const FormatInfo FORMAT_BGRA         = { VK_FORMAT_B8G8R8A8_UNORM, { S_ID, S_ID, S_ID, S_ID }, COLOR, 4, 1 };
const FormatInfo FORMAT_BGRX         = { VK_FORMAT_B8G8R8A8_UNORM, { S_ID, S_ID, S_ID, S_ONE }, COLOR, 4, 1 };
const FormatInfo FORMAT_RGBA         = { VK_FORMAT_R8G8B8A8_UNORM, { S_ID, S_ID, S_ID, S_ID }, COLOR, 4, 1 };
const FormatInfo FORMAT_RGBA_UINT    = { VK_FORMAT_R8G8B8A8_UINT, { S_ID, S_ID, S_ID, S_ID }, COLOR, 4, 1 };
const FormatInfo FORMAT_A8           = { VK_FORMAT_R8_UNORM, { S_ZERO, S_ZERO, S_ZERO, S_R }, COLOR, 1, 1 };
const FormatInfo FORMAT_R8           = { VK_FORMAT_R8_UNORM, { S_ID, S_ID, S_ID, S_ID }, COLOR, 1, 1 };
const FormatInfo FORMAT_RG8_SNORM    = { VK_FORMAT_R8G8_SNORM, { S_ID, S_ID, S_ID, S_ID }, COLOR, 2, 1 };
const FormatInfo FORMAT_565          = { VK_FORMAT_R5G6B5_UNORM_PACK16, { S_ID, S_ID, S_ID, S_ID }, COLOR, 2, 1 };
const FormatInfo FORMAT_5551         = { VK_FORMAT_A1R5G5B5_UNORM_PACK16, { S_ID, S_ID, S_ID, S_ID }, COLOR, 2, 1 };
const FormatInfo FORMAT_4444         = { VK_FORMAT_A4R4G4B4_UNORM_PACK16, { S_ID, S_ID, S_ID, S_ID }, COLOR, 2, 1 };
const FormatInfo FORMAT_R16          = { VK_FORMAT_R16_UNORM, { S_ID, S_ID, S_ID, S_ID }, COLOR, 2, 1 };
const FormatInfo FORMAT_R16_UINT     = { VK_FORMAT_R16_UINT, { S_ID, S_ID, S_ID, S_ID }, COLOR, 2, 1 };
const FormatInfo FORMAT_R32_UINT     = { VK_FORMAT_R32_UINT, { S_ID, S_ID, S_ID, S_ID }, COLOR, 4, 1 };
const FormatInfo FORMAT_R32F         = { VK_FORMAT_R32_SFLOAT, { S_ID, S_ID, S_ID, S_ID }, COLOR, 4, 1 };
const FormatInfo FORMAT_RG32F        = { VK_FORMAT_R32G32_SFLOAT, { S_ID, S_ID, S_ID, S_ID }, COLOR, 8, 1 };
const FormatInfo FORMAT_RGB32F       = { VK_FORMAT_R32G32B32_SFLOAT, { S_ID, S_ID, S_ID, S_ID }, COLOR, 12, 1 };
const FormatInfo FORMAT_RGBA32F      = { VK_FORMAT_R32G32B32A32_SFLOAT, { S_ID, S_ID, S_ID, S_ID }, COLOR, 16, 1 };
const FormatInfo FORMAT_RGBA16F      = { VK_FORMAT_R16G16B16A16_SFLOAT, { S_ID, S_ID, S_ID, S_ID }, COLOR, 8, 1 };
const FormatInfo FORMAT_RGBA16       = { VK_FORMAT_R16G16B16A16_UNORM, { S_ID, S_ID, S_ID, S_ID }, COLOR, 8, 1 };
const FormatInfo FORMAT_RG16_SINT    = { VK_FORMAT_R16G16_SINT, { S_ID, S_ID, S_ID, S_ID }, COLOR, 4, 1 };
const FormatInfo FORMAT_RGBA16_SINT  = { VK_FORMAT_R16G16B16A16_SINT, { S_ID, S_ID, S_ID, S_ID }, COLOR, 8, 1 };
const FormatInfo FORMAT_R11G11B10F   = { VK_FORMAT_B10G11R11_UFLOAT_PACK32, { S_ID, S_ID, S_ID, S_ID }, COLOR, 4, 1 };
const FormatInfo FORMAT_BC1          = { VK_FORMAT_BC1_RGBA_UNORM_BLOCK, { S_ID, S_ID, S_ID, S_ID }, COLOR, 8, 4 };
const FormatInfo FORMAT_BC2          = { VK_FORMAT_BC2_UNORM_BLOCK, { S_ID, S_ID, S_ID, S_ID }, COLOR, 16, 4 };
const FormatInfo FORMAT_BC3          = { VK_FORMAT_BC3_UNORM_BLOCK, { S_ID, S_ID, S_ID, S_ID }, COLOR, 16, 4 };
const FormatInfo FORMAT_BC7          = { VK_FORMAT_BC7_UNORM_BLOCK, { S_ID, S_ID, S_ID, S_ID }, COLOR, 16, 4 };
const FormatInfo FORMAT_D32          = { VK_FORMAT_D32_SFLOAT, { S_ID, S_ID, S_ID, S_ID }, DEPTH, 4, 1 };
const FormatInfo FORMAT_D16          = { VK_FORMAT_D16_UNORM, { S_ID, S_ID, S_ID, S_ID }, DEPTH, 2, 1 };
const FormatInfo FORMAT_D32S8        = { VK_FORMAT_D32_SFLOAT_S8_UINT, { S_ID, S_ID, S_ID, S_ID }, DEPTH_STENCIL, 8, 1 };
// D24S8 family: AMD has no D24_UNORM_S8_UINT, the device switches these to D32_SFLOAT_S8_UINT.
FormatInfo FormatD24S8               = { VK_FORMAT_D24_UNORM_S8_UINT, { S_ID, S_ID, S_ID, S_ID }, DEPTH_STENCIL, 4, 1 };

// Why the last device creation failed (Vulkan_Device_Error).
char Device_Error[256] = "";

// Validation layer messages, collected from any thread, logged per frame. The store lives exactly as
// long as the debug messenger: global containers would keep memory from the game's allocator past
// its leak report at shutdown.
struct MessageStore
{
	std::vector<std::string>	lines;
	std::vector<int32_t>		reported_ids;
};
CRITICAL_SECTION	Message_Lock;
bool				Message_Lock_Ready = false;
MessageStore*		Messages = nullptr;

VKAPI_ATTR VkBool32 VKAPI_CALL Debug_Callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
											  const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
	if (!data || !Message_Lock_Ready) return VK_FALSE;
	EnterCriticalSection(&Message_Lock);
	std::vector<int32_t>* ids = Messages ? &Messages->reported_ids : nullptr;
	if (ids && (std::find(ids->begin(), ids->end(), data->messageIdNumber) == ids->end() || data->messageIdNumber == 0)) {
		ids->push_back(data->messageIdNumber);
		const char* level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "FEHLER" :
							(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "WARNUNG" : "Info";
		char line[2048];
		std::snprintf(line, sizeof(line), "Vulkan-Validierung [%s, %s]: %s", level,
			data->pMessageIdName ? data->pMessageIdName : "?", data->pMessage ? data->pMessage : "");
		if (Messages->lines.size() < 1000) Messages->lines.push_back(line);
	}
	LeaveCriticalSection(&Message_Lock);
	return VK_FALSE;
}

bool Has_Name(const std::vector<VkExtensionProperties>& list, const char* name)
{
	for (const VkExtensionProperties& e : list) {
		if (strcmp(e.extensionName, name) == 0) return true;
	}
	return false;
}

/// A whole word of the process command line, case-insensitive (the device layer has no access to GlobalData).
/// The wide command line: WinMain splits the narrow one in place (MSVC hands it the GetCommandLineA buffer).
bool Has_Command_Line_Switch(const char* name)
{
	const wchar_t* line = GetCommandLineW();
	const size_t length = strlen(name);
	for (const wchar_t* p = line; p && *p; ) {
		while (*p == L' ' || *p == L'\t' || *p == L'"') ++p;
		const wchar_t* end = p;
		while (*end && *end != L' ' && *end != L'\t' && *end != L'"') ++end;
		bool same = static_cast<size_t>(end - p) == length;
		for (size_t i = 0; same && i < length; ++i) same = towlower(p[i]) == towlower(static_cast<unsigned char>(name[i]));
		if (same) return true;
		p = end;
	}
	return false;
}

}  // namespace

void Fail(const char* text, int code)
{
	if (code) std::snprintf(Device_Error, sizeof(Device_Error), "%s (%d)", text, code);
	else std::snprintf(Device_Error, sizeof(Device_Error), "%s", text);
	WWDEBUG_SAY(("Vulkan: %s\n", Device_Error));
}

const FormatInfo& Format_Info(GPU_FORMAT format)
{
	switch (format) {
	case GPU_FORMAT_B8G8R8A8_UNORM:
	case GPU_FORMAT_B8G8R8A8_TYPELESS:		return FORMAT_BGRA;
	case GPU_FORMAT_B8G8R8X8_UNORM:			return FORMAT_BGRX;
	case GPU_FORMAT_R8G8B8A8_UNORM:
	case GPU_FORMAT_R8G8B8A8_TYPELESS:		return FORMAT_RGBA;
	case GPU_FORMAT_R8G8B8A8_UINT:			return FORMAT_RGBA_UINT;
	case GPU_FORMAT_A8_UNORM:				return FORMAT_A8;
	case GPU_FORMAT_R8_UNORM:				return FORMAT_R8;
	case GPU_FORMAT_R8G8_SNORM:				return FORMAT_RG8_SNORM;
	case GPU_FORMAT_B5G6R5_UNORM:			return FORMAT_565;
	case GPU_FORMAT_B5G5R5A1_UNORM:			return FORMAT_5551;
	case GPU_FORMAT_B4G4R4A4_UNORM:			return FORMAT_4444;
	case GPU_FORMAT_R16_UNORM:				return FORMAT_R16;
	case GPU_FORMAT_R16_UINT:				return FORMAT_R16_UINT;
	case GPU_FORMAT_R32_UINT:				return FORMAT_R32_UINT;
	case GPU_FORMAT_R32_FLOAT:				return FORMAT_R32F;
	case GPU_FORMAT_R32G32_FLOAT:			return FORMAT_RG32F;
	case GPU_FORMAT_R32G32B32_FLOAT:		return FORMAT_RGB32F;
	case GPU_FORMAT_R32G32B32A32_FLOAT:		return FORMAT_RGBA32F;
	case GPU_FORMAT_R16G16B16A16_FLOAT:		return FORMAT_RGBA16F;
	case GPU_FORMAT_R16G16B16A16_UNORM:		return FORMAT_RGBA16;
	case GPU_FORMAT_R16G16_SINT:			return FORMAT_RG16_SINT;
	case GPU_FORMAT_R16G16B16A16_SINT:		return FORMAT_RGBA16_SINT;
	case GPU_FORMAT_R11G11B10_FLOAT:		return FORMAT_R11G11B10F;
	case GPU_FORMAT_BC1_UNORM:
	case GPU_FORMAT_BC1_TYPELESS:			return FORMAT_BC1;
	case GPU_FORMAT_BC2_UNORM:
	case GPU_FORMAT_BC2_TYPELESS:			return FORMAT_BC2;
	case GPU_FORMAT_BC3_UNORM:
	case GPU_FORMAT_BC3_TYPELESS:			return FORMAT_BC3;
	case GPU_FORMAT_BC7_UNORM:
	case GPU_FORMAT_BC7_TYPELESS:			return FORMAT_BC7;
	case GPU_FORMAT_D32_FLOAT:
	case GPU_FORMAT_R32_TYPELESS:			return FORMAT_D32;
	case GPU_FORMAT_D16_UNORM:
	case GPU_FORMAT_R16_TYPELESS:			return FORMAT_D16;
	case GPU_FORMAT_D24_UNORM_S8_UINT:
	case GPU_FORMAT_R24G8_TYPELESS:
	case GPU_FORMAT_R24_UNORM_X8_TYPELESS:	return FormatD24S8;
	case GPU_FORMAT_D32_FLOAT_S8X24_UINT:
	case GPU_FORMAT_R32G8X24_TYPELESS:
	case GPU_FORMAT_R32_FLOAT_X8X24_TYPELESS:	return FORMAT_D32S8;
	default:								return FORMAT_NONE;
	}
}

void Format_Pitch(const FormatInfo& info, unsigned width, unsigned height, unsigned* row_bytes, unsigned* rows)
{
	const unsigned b = info.block_size;
	const unsigned blocks_x = (width + b - 1) / b, blocks_y = (height + b - 1) / b;
	if (row_bytes) *row_bytes = (blocks_x ? blocks_x : 1) * info.block_bytes;
	if (rows) *rows = blocks_y ? blocks_y : 1;
}

// ==================================================================================================
// Device creation
// ==================================================================================================

Gpu::Gpu()
{
	memset(&m_properties, 0, sizeof(m_properties));
	m_depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
	m_instance = VK_NULL_HANDLE;
	m_physical = VK_NULL_HANDLE;
	m_device = VK_NULL_HANDLE;
	m_allocator = nullptr;
	m_queue = VK_NULL_HANDLE;
	m_queue_family = 0;
	m_debug = false;
	m_fill_mode_non_solid = false;
	m_memory_budget = false;
	m_host_query_reset = false;
	m_pipeline_statistics = false;
	m_open_statistics = nullptr;
	m_lost = false;
	m_hwnd = nullptr;
	m_surface = VK_NULL_HANDLE;
	m_swapchain = VK_NULL_HANDLE;
	m_swapchain_format = VK_FORMAT_UNDEFINED;
	m_swapchain_extent.width = m_swapchain_extent.height = 0;
	m_present_mode = VK_PRESENT_MODE_FIFO_KHR;
	m_immediate_supported = false;
	m_mailbox_supported = false;
	m_back_buffer = nullptr;
	m_present_image = 0;
	for (Frame& f : m_frames) {
		f.pool = VK_NULL_HANDLE;
		f.cmd = f.upload = VK_NULL_HANDLE;
		f.upload_open = false;
		f.fence = VK_NULL_HANDLE;
		f.acquired = VK_NULL_HANDLE;
		f.serial = 0;
		f.descriptor_pool = 0;
	}
	m_frame = 0;
	m_serial = 1;
	m_completed = 0;
	m_next_id = 0;
	m_live_objects = 0;
	m_serialized = false;
	for (VkDescriptorSetLayout& l : m_set_layouts) l = VK_NULL_HANDLE;
	m_pipeline_layout = VK_NULL_HANDLE;
	m_pipeline_cache = VK_NULL_HANDLE;
	m_pipeline_stats = PipelineStats();
	m_upload_batch = 0;
	m_null_texture = nullptr;
	m_null_view = VK_NULL_HANDLE;
	m_null_buffer = nullptr;
	m_default_sampler = VK_NULL_HANDLE;
	for (StageState& s : m_stages) {
		s.code = nullptr;
		for (unsigned i = 0; i < CB_SLOTS; ++i) s.cb_first[i] = s.cb_count[i] = 0;
		memset(&s.written, 0, sizeof(s.written));
	}
	m_topology = GPU_PRIMITIVE_TOPOLOGY_UNDEFINED;
	memset(m_vertex_strides, 0, sizeof(m_vertex_strides));
	memset(m_vertex_offsets, 0, sizeof(m_vertex_offsets));
	m_index_format = GPU_FORMAT_R16_UINT;
	m_index_offset = 0;
	for (FLOAT& f : m_blend_factor) f = 1.0f;
	m_stencil_ref = 0;
	memset(&m_viewport, 0, sizeof(m_viewport));
	m_viewport_set = false;
	m_rendering = false;
	m_barrier_pending = false;
	m_draw_since_barrier = false;
	m_bound_pipeline = VK_NULL_HANDLE;
	m_bound_topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
	m_dynamic_dirty = true;
	memset(m_bound_vb, 0, sizeof(m_bound_vb));
	memset(m_bound_vb_offset, 0, sizeof(m_bound_vb_offset));
	memset(m_bound_vb_stride, 0, sizeof(m_bound_vb_stride));
	m_bound_ib = VK_NULL_HANDLE;
	m_bound_ib_offset = 0;
	m_render_extent.width = m_render_extent.height = 0;
}

Gpu::~Gpu()
{
}

void Gpu::Log_Once(const char* what)
{
	for (const char* known : m_logged) {
		if (strcmp(known, what) == 0) return;
	}
	m_logged.push_back(what);
	WWDEBUG_SAY(("Vulkan: %s\n", what));
}

bool Gpu::Create(VkInstance instance, VkPhysicalDevice physical, HWND hwnd, unsigned width, unsigned height, bool debug)
{
	m_instance = instance;
	m_physical = physical;
	m_hwnd = hwnd;
	m_debug = debug;
	m_serialized = Has_Command_Line_Switch("-vulkanSeriell");
	if (m_serialized) { WWDEBUG_SAY(("Vulkan: serialized (-vulkanSeriell): a barrier before every draw, uploads in the frame, wait after every submission\n")); }
	vkGetPhysicalDeviceProperties(physical, &m_properties);

	// Queue family: graphics with presentation to the window.
	uint32_t families = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
	std::vector<VkQueueFamilyProperties> family(families);
	if (families) vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, family.data());
	bool found = false;
	for (uint32_t i = 0; i < families; ++i) {
		if ((family[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && vkGetPhysicalDeviceWin32PresentationSupportKHR(physical, i)) {
			m_queue_family = i;
			found = true;
			break;
		}
	}
	if (!found) {
		Fail("no graphics queue that presents to windows");
		return false;
	}

	uint32_t count = 0;
	vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> available(count);
	if (count) vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, available.data());
	std::vector<const char*> extensions;
	extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	m_memory_budget = Has_Name(available, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	if (m_memory_budget) extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

	VkPhysicalDeviceFeatures2 supported = {};
	supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	VkPhysicalDeviceVulkan12Features supported12 = {};
	supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	supported.pNext = &supported12;
	vkGetPhysicalDeviceFeatures2(physical, &supported);

	VkPhysicalDeviceVulkan13Features features13 = {};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;
	features13.shaderDemoteToHelperInvocation = VK_TRUE;		// DXC turns discard into OpDemoteToHelperInvocation
	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.hostQueryReset = supported12.hostQueryReset;
	m_host_query_reset = supported12.hostQueryReset != VK_FALSE;
	features12.pNext = &features13;
	VkPhysicalDeviceFeatures2 features = {};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features.pNext = &features12;
	features.features.tessellationShader = VK_TRUE;
	features.features.samplerAnisotropy = VK_TRUE;
	features.features.textureCompressionBC = VK_TRUE;
	features.features.depthClamp = VK_TRUE;
	features.features.depthBiasClamp = VK_TRUE;
	features.features.fillModeNonSolid = supported.features.fillModeNonSolid;
	features.features.independentBlend = supported.features.independentBlend;
	features.features.pipelineStatisticsQuery = supported.features.pipelineStatisticsQuery;
	m_pipeline_statistics = supported.features.pipelineStatisticsQuery != VK_FALSE;
	m_fill_mode_non_solid = supported.features.fillModeNonSolid != VK_FALSE;

	const float priority = 1.0f;
	VkDeviceQueueCreateInfo queue = {};
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueFamilyIndex = m_queue_family;
	queue.queueCount = 1;
	queue.pQueuePriorities = &priority;
	VkDeviceCreateInfo create = {};
	create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	create.pNext = &features;
	create.queueCreateInfoCount = 1;
	create.pQueueCreateInfos = &queue;
	create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	create.ppEnabledExtensionNames = extensions.data();
	const VkResult created = vkCreateDevice(physical, &create, nullptr, &m_device);
	if (created != VK_SUCCESS) {
		Fail("vkCreateDevice failed", static_cast<int>(created));
		m_device = VK_NULL_HANDLE;
		return false;
	}
	volkLoadDevice(m_device);
	vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);

	VmaVulkanFunctions functions = {};
	functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
	VmaAllocatorCreateInfo allocator = {};
	allocator.physicalDevice = physical;
	allocator.device = m_device;
	allocator.instance = instance;
	allocator.vulkanApiVersion = VK_API_VERSION_1_3;
	allocator.pVulkanFunctions = &functions;
	if (m_memory_budget) allocator.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	if (vmaCreateAllocator(&allocator, &m_allocator) != VK_SUCCESS) {
		Fail("vmaCreateAllocator failed");
		m_allocator = nullptr;
		return false;
	}

	// The D24S8 family on devices without D24_UNORM_S8_UINT.
	VkFormatProperties d24 = {};
	vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_D24_UNORM_S8_UINT, &d24);
	m_depth_format = (d24.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
		? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;
	FormatD24S8.format = m_depth_format;
	FormatD24S8.block_bytes = (m_depth_format == VK_FORMAT_D24_UNORM_S8_UINT) ? 4 : 8;

	VkWin32SurfaceCreateInfoKHR surface = {};
	surface.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surface.hinstance = GetModuleHandleA(nullptr);
	surface.hwnd = hwnd;
	const VkResult surfaced = vkCreateWin32SurfaceKHR(instance, &surface, nullptr, &m_surface);
	if (surfaced != VK_SUCCESS) {
		Fail("no Vulkan surface for the window (vkCreateWin32SurfaceKHR)", static_cast<int>(surfaced));
		m_surface = VK_NULL_HANDLE;
		return false;
	}
	VkBool32 presentable = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR(physical, m_queue_family, m_surface, &presentable);
	if (!presentable) {
		Fail("the queue cannot present to the window surface");
		return false;
	}

	for (Frame& f : m_frames) {
		VkCommandPoolCreateInfo pool = {};
		pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool.queueFamilyIndex = m_queue_family;
		if (vkCreateCommandPool(m_device, &pool, nullptr, &f.pool) != VK_SUCCESS) return false;
		VkCommandBufferAllocateInfo alloc = {};
		alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		alloc.commandPool = f.pool;
		alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		alloc.commandBufferCount = 2;
		VkCommandBuffer buffers[2];
		if (vkAllocateCommandBuffers(m_device, &alloc, buffers) != VK_SUCCESS) return false;
		f.cmd = buffers[0];
		f.upload = buffers[1];
		VkFenceCreateInfo fence = {};
		fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		if (vkCreateFence(m_device, &fence, nullptr, &f.fence) != VK_SUCCESS) return false;
		VkSemaphoreCreateInfo semaphore = {};
		semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		if (vkCreateSemaphore(m_device, &semaphore, nullptr, &f.acquired) != VK_SUCCESS) return false;
	}

	m_frame = 0;
	Begin_Frame();
	if (!Create_Layouts()) {
		Fail("descriptor layouts or default objects could not be created");
		return false;
	}
	if (!Create_Back_Buffer(width, height)) return false;
	Create_Swap_Chain(width, height);		// a minimized window has none yet; Present retries

	char api[32];
	Vulkan_Version_Text(m_properties.apiVersion, api, sizeof(api));
	WWDEBUG_SAY(("Vulkan: device '%s', API %s, depth format %s, swap chain %ux%u%s%s\n", m_properties.deviceName, api,
		m_depth_format == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : "D32S8", m_swapchain_extent.width, m_swapchain_extent.height,
		m_immediate_supported ? ", tearing allowed" : "", m_debug ? ", validation messages to the log" : ""));
	return true;
}

void Gpu::Destroy()
{
	if (!m_device) return;
	vkDeviceWaitIdle(m_device);
	ClearState();
	for (std::vector<uint32_t>* v : { &m_frame_barriers, &m_frame_transitions, &m_frame_renderings }) {
		v->clear();
		v->shrink_to_fit();
	}
	if (m_back_buffer) {
		m_back_buffer->Release();
		m_back_buffer = nullptr;
	}
	Destroy_Layouts();
	for (Frame& f : m_frames) Destroy_Garbage(f.garbage);
	for (Frame& f : m_frames) {
		for (VkDescriptorPool p : f.descriptor_pools) vkDestroyDescriptorPool(m_device, p, nullptr);
		f.descriptor_pools.clear();
		for (UploadChunk& c : f.upload_chunks) vmaDestroyBuffer(m_allocator, c.slice.buffer, c.slice.allocation);
		f.upload_chunks.clear();
		if (f.pool) vkDestroyCommandPool(m_device, f.pool, nullptr);
		if (f.fence) vkDestroyFence(m_device, f.fence, nullptr);
		if (f.acquired) vkDestroySemaphore(m_device, f.acquired, nullptr);
		f.pool = VK_NULL_HANDLE;
		f.fence = VK_NULL_HANDLE;
		f.acquired = VK_NULL_HANDLE;
	}
	Destroy_Swap_Chain();
	if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	m_surface = VK_NULL_HANDLE;
	if (m_live_objects > 0) {
		WWDEBUG_SAY(("Vulkan: %ld live objects at device shutdown\n", m_live_objects));
	}
	else {
		WWDEBUG_SAY(("Vulkan: 0 live objects at device shutdown\n"));
	}
	if (m_allocator && m_live_objects == 0) vmaDestroyAllocator(m_allocator);
	m_allocator = nullptr;
	vkDestroyDevice(m_device, nullptr);
	m_device = VK_NULL_HANDLE;
}

void Gpu::Object_Destroyed()
{
	--m_live_objects;
	if (m_live_objects == 0 && m_device == VK_NULL_HANDLE && m_instance == VK_NULL_HANDLE) delete this;
}

bool Gpu::Create_Shader_Module(const std::vector<uint32_t>& words, VkShaderModule* module)
{
	VkShaderModuleCreateInfo create = {};
	create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create.codeSize = words.size() * sizeof(uint32_t);
	create.pCode = words.data();
	return vkCreateShaderModule(m_device, &create, nullptr, module) == VK_SUCCESS;
}

// ==================================================================================================
// Memory
// ==================================================================================================

bool Gpu::Allocate_Buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host, bool readback, Slice& slice)
{
	memset(&slice, 0, sizeof(slice));
	if (!m_allocator) return false;
	VkBufferCreateInfo buffer = {};
	buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer.size = size ? size : 16;
	buffer.usage = usage;
	buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VmaAllocationCreateInfo alloc = {};
	alloc.usage = host ? VMA_MEMORY_USAGE_AUTO : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	if (host) {
		// Coherent memory: writes through the mapping need no flush before the submission.
		alloc.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
			(readback ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		alloc.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		if (readback) alloc.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	}
	VmaAllocationInfo info = {};
	if (vmaCreateBuffer(m_allocator, &buffer, &alloc, &slice.buffer, &slice.allocation, &info) != VK_SUCCESS) {
		memset(&slice, 0, sizeof(slice));
		return false;
	}
	slice.mapped = static_cast<uint8_t*>(info.pMappedData);
	return true;
}

bool Gpu::Upload_Space(VkDeviceSize size, VkDeviceSize alignment, Slice& slice)
{
	Frame& f = m_frames[m_frame];
	if (alignment < 16) alignment = 16;
	for (UploadChunk& c : f.upload_chunks) {
		const VkDeviceSize at = (c.used + alignment - 1) & ~(alignment - 1);
		if (at + size <= c.size) {
			slice = c.slice;
			slice.offset = at;
			slice.mapped = c.slice.mapped + at;
			c.used = at + size;
			return true;
		}
	}
	UploadChunk chunk;
	chunk.size = std::max(size, UPLOAD_CHUNK_BYTES);
	chunk.used = 0;
	if (!Allocate_Buffer(chunk.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, false, chunk.slice) || !chunk.slice.mapped) {
		Log_Once("upload memory exhausted");
		return false;
	}
	chunk.used = size;
	f.upload_chunks.push_back(chunk);
	slice = chunk.slice;
	slice.offset = 0;
	return true;
}

void Gpu::Retire(VkObjectType type, uint64_t handle, VmaAllocation allocation)
{
	if (!m_device || (!handle && !allocation)) return;
	Garbage g;
	g.type = type;
	g.handle = handle;
	g.allocation = allocation;
	m_frames[m_frame].garbage.push_back(g);
}

void Gpu::Destroy_Garbage(std::vector<Garbage>& list)
{
	for (const Garbage& g : list) {
		switch (g.type) {
		case VK_OBJECT_TYPE_BUFFER:
			if (g.allocation) vmaDestroyBuffer(m_allocator, reinterpret_cast<VkBuffer>(g.handle), g.allocation);
			else vkDestroyBuffer(m_device, reinterpret_cast<VkBuffer>(g.handle), nullptr);
			break;
		case VK_OBJECT_TYPE_IMAGE:
			if (g.allocation) vmaDestroyImage(m_allocator, reinterpret_cast<VkImage>(g.handle), g.allocation);
			else vkDestroyImage(m_device, reinterpret_cast<VkImage>(g.handle), nullptr);
			break;
		case VK_OBJECT_TYPE_IMAGE_VIEW:		vkDestroyImageView(m_device, reinterpret_cast<VkImageView>(g.handle), nullptr); break;
		case VK_OBJECT_TYPE_SAMPLER:		vkDestroySampler(m_device, reinterpret_cast<VkSampler>(g.handle), nullptr); break;
		case VK_OBJECT_TYPE_SHADER_MODULE:	vkDestroyShaderModule(m_device, reinterpret_cast<VkShaderModule>(g.handle), nullptr); break;
		case VK_OBJECT_TYPE_QUERY_POOL:		vkDestroyQueryPool(m_device, reinterpret_cast<VkQueryPool>(g.handle), nullptr); break;
		case VK_OBJECT_TYPE_PIPELINE:		vkDestroyPipeline(m_device, reinterpret_cast<VkPipeline>(g.handle), nullptr); break;
		default: break;
		}
	}
	list.clear();
}

// ==================================================================================================
// Frames and submissions
// ==================================================================================================

void Gpu::Recycle(Frame& f)
{
	Destroy_Garbage(f.garbage);
	for (VkDescriptorPool p : f.descriptor_pools) vkResetDescriptorPool(m_device, p, 0);
	f.descriptor_pool = 0;
	// Keep one chunk; a frame that needed more was a loading frame.
	while (f.upload_chunks.size() > 1) {
		vmaDestroyBuffer(m_allocator, f.upload_chunks.back().slice.buffer, f.upload_chunks.back().slice.allocation);
		f.upload_chunks.pop_back();
	}
	for (UploadChunk& c : f.upload_chunks) c.used = 0;
}

void Gpu::Begin_Frame()
{
	Frame& f = m_frames[m_frame];
	const VkResult waited = vkWaitForFences(m_device, 1, &f.fence, VK_TRUE, UINT64_MAX);
	if (waited == VK_ERROR_DEVICE_LOST) m_lost = true;
	if (f.serial > m_completed) m_completed = f.serial;
	Recycle(f);
	vkResetFences(m_device, 1, &f.fence);
	vkResetCommandPool(m_device, f.pool, 0);
	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(f.cmd, &begin);
	f.upload_open = false;
	Reset_Command_State();
}

void Gpu::Submit(bool present_wait)
{
	Frame& f = m_frames[m_frame];
	End_Rendering();
	if (m_open_statistics) {
		// A query cannot outlive its command buffer; this one ends here and its result is dropped.
		vkCmdEndQuery(f.cmd, m_open_statistics->m_pool, 0);
		m_open_statistics->m_active = false;
		m_open_statistics->m_written = false;
		m_open_statistics = nullptr;
	}
	VkCommandBufferSubmitInfo buffers[2] = {};
	uint32_t count = 0;
	if (f.upload_open) {
		Barrier_All(f.upload);
		vkEndCommandBuffer(f.upload);
		buffers[count].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		buffers[count++].commandBuffer = f.upload;
		f.upload_open = false;
	}
	vkEndCommandBuffer(f.cmd);
	buffers[count].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	buffers[count++].commandBuffer = f.cmd;

	VkSubmitInfo2 submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.commandBufferInfoCount = count;
	submit.pCommandBufferInfos = buffers;
	VkSemaphoreSubmitInfo wait = {};
	VkSemaphoreSubmitInfo signal = {};
	if (present_wait) {
		wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		wait.semaphore = f.acquired;
		wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		submit.waitSemaphoreInfoCount = 1;
		submit.pWaitSemaphoreInfos = &wait;
	}
	if (present_wait && !m_rendered.empty()) {
		signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		signal.semaphore = m_rendered[m_present_image];
		signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		submit.signalSemaphoreInfoCount = 1;
		submit.pSignalSemaphoreInfos = &signal;
	}
	const VkResult result = vkQueueSubmit2(m_queue, 1, &submit, f.fence);
	if (m_serialized && result == VK_SUCCESS) vkQueueWaitIdle(m_queue);
	if (result == VK_ERROR_DEVICE_LOST) m_lost = true;
	else if (result != VK_SUCCESS) { WWDEBUG_SAY(("Vulkan: vkQueueSubmit2 failed (%d)\n", static_cast<int>(result))); }
	f.serial = m_serial++;
}

void Gpu::Flush_And_Wait()
{
	Frame& f = m_frames[m_frame];
	Submit(false);
	const VkResult waited = vkWaitForFences(m_device, 1, &f.fence, VK_TRUE, UINT64_MAX);
	if (waited == VK_ERROR_DEVICE_LOST) m_lost = true;
	m_completed = f.serial;
	Recycle(f);
	vkResetFences(m_device, 1, &f.fence);
	vkResetCommandPool(m_device, f.pool, 0);
	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(f.cmd, &begin);
	Reset_Command_State();
}

// ==================================================================================================
// Swap chain and back buffer
// ==================================================================================================

bool Gpu::Create_Back_Buffer(unsigned width, unsigned height)
{
	GPU_TEXTURE2D_DESC desc = {};
	desc.Width = width ? width : 1;
	desc.Height = height ? height : 1;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = GPU_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = GPU_USAGE_DEFAULT;
	desc.BindFlags = GPU_BIND_RENDER_TARGET | GPU_BIND_SHADER_RESOURCE;
	Texture* texture = new Texture(this, desc);
	if (!texture->Create(nullptr)) {
		texture->Release();
		Fail("back buffer could not be created");
		return false;
	}
	m_back_buffer = texture;
	return true;
}

void Gpu::Destroy_Swap_Chain()
{
	if (!m_device) return;
	for (VkSemaphore s : m_rendered) vkDestroySemaphore(m_device, s, nullptr);
	m_rendered.clear();
	m_swapchain_images.clear();
	if (m_swapchain) vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	m_swapchain = VK_NULL_HANDLE;
}

bool Gpu::Create_Swap_Chain(unsigned width, unsigned height)
{
	VkSurfaceCapabilitiesKHR caps = {};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical, m_surface, &caps) != VK_SUCCESS) return false;
	VkExtent2D extent;
	if (caps.currentExtent.width != 0xFFFFFFFFu) extent = caps.currentExtent;
	else {
		extent.width = std::min(std::max(width, caps.minImageExtent.width), caps.maxImageExtent.width);
		extent.height = std::min(std::max(height, caps.minImageExtent.height), caps.maxImageExtent.height);
	}
	VkSwapchainKHR old = m_swapchain;
	for (VkSemaphore s : m_rendered) vkDestroySemaphore(m_device, s, nullptr);
	m_rendered.clear();
	m_swapchain_images.clear();
	m_swapchain = VK_NULL_HANDLE;
	if (extent.width == 0 || extent.height == 0) {
		if (old) vkDestroySwapchainKHR(m_device, old, nullptr);
		m_swapchain_extent = extent;
		return false;		// minimized
	}

	uint32_t count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &count, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(count);
	if (count) vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &count, formats.data());
	VkSurfaceFormatKHR format = {};
	format.format = VK_FORMAT_UNDEFINED;
	for (const VkSurfaceFormatKHR& f : formats) {
		if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
			format = f;
			if (f.format == VK_FORMAT_B8G8R8A8_UNORM) break;
		}
	}
	if (format.format == VK_FORMAT_UNDEFINED && !formats.empty()) format = formats[0];

	count = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical, m_surface, &count, nullptr);
	std::vector<VkPresentModeKHR> modes(count);
	if (count) vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical, m_surface, &count, modes.data());
	m_immediate_supported = std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end();
	m_mailbox_supported = std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end();
	if (m_present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR && !m_immediate_supported) m_present_mode = VK_PRESENT_MODE_FIFO_KHR;
	if (m_present_mode == VK_PRESENT_MODE_MAILBOX_KHR && !m_mailbox_supported) m_present_mode = VK_PRESENT_MODE_FIFO_KHR;

	VkSwapchainCreateInfoKHR create = {};
	create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create.surface = m_surface;
	create.minImageCount = std::max(2u, caps.minImageCount);
	if (caps.maxImageCount && create.minImageCount > caps.maxImageCount) create.minImageCount = caps.maxImageCount;
	create.imageFormat = format.format;
	create.imageColorSpace = format.colorSpace;
	create.imageExtent = extent;
	create.imageArrayLayers = 1;
	create.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
	create.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
		? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1));
	create.presentMode = m_present_mode;
	create.clipped = VK_TRUE;
	create.oldSwapchain = old;
	const VkResult result = vkCreateSwapchainKHR(m_device, &create, nullptr, &m_swapchain);
	if (old) vkDestroySwapchainKHR(m_device, old, nullptr);
	if (result != VK_SUCCESS) {
		WWDEBUG_SAY(("Vulkan: vkCreateSwapchainKHR failed (%d)\n", static_cast<int>(result)));
		m_swapchain = VK_NULL_HANDLE;
		return false;
	}
	m_swapchain_format = format.format;
	m_swapchain_extent = extent;
	count = 0;
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr);
	m_swapchain_images.resize(count);
	if (count) vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, m_swapchain_images.data());
	for (uint32_t i = 0; i < count; ++i) {
		VkSemaphoreCreateInfo semaphore = {};
		semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkSemaphore s = VK_NULL_HANDLE;
		vkCreateSemaphore(m_device, &semaphore, nullptr, &s);
		m_rendered.push_back(s);
	}
	return true;
}

GpuTexture2D* Gpu::Back_Buffer()
{
	if (!m_back_buffer) return nullptr;
	m_back_buffer->AddRef();
	return m_back_buffer;
}

HRESULT Gpu::Resize(unsigned width, unsigned height)
{
	if (!m_device) return E_FAIL;
	if (m_back_buffer && m_back_buffer->AddRef() > 2) {
		// Like DXGI: every reference besides the device's own must be gone.
		m_back_buffer->Release();
		return E_FAIL;
	}
	if (m_back_buffer) m_back_buffer->Release();
	Flush_And_Wait();
	vkDeviceWaitIdle(m_device);
	if (m_back_buffer) {
		m_back_buffer->Release();
		m_back_buffer = nullptr;
	}
	if (!Create_Back_Buffer(width, height)) return E_OUTOFMEMORY;
	Create_Swap_Chain(width, height);
	return S_OK;
}

HRESULT Gpu::Present(int swap_interval)
{
	if (!m_device) return E_FAIL;
	if (m_lost) return HR_DEVICE_REMOVED;
	Frame& f = m_frames[m_frame];

	// Swap interval 0: no waiting for the display (tearing where possible), else FIFO. Every second or
	// third refresh has no Vulkan present mode; those intervals run as 1.
	if (swap_interval > 1) Log_Once("swap interval 2 and 3 run as 1 (every refresh) under Vulkan");
	VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
	if (swap_interval == 0) mode = m_immediate_supported ? VK_PRESENT_MODE_IMMEDIATE_KHR :
								   (m_mailbox_supported ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR);
	if (mode != m_present_mode || !m_swapchain) {
		m_present_mode = mode;
		End_Rendering();
		vkDeviceWaitIdle(m_device);
		Create_Swap_Chain(m_back_buffer->m_desc.Width, m_back_buffer->m_desc.Height);
	}

	bool acquired = false;
	if (m_swapchain) {
		VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &m_present_image);
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			End_Rendering();
			vkDeviceWaitIdle(m_device);
			if (Create_Swap_Chain(m_back_buffer->m_desc.Width, m_back_buffer->m_desc.Height))
				result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &m_present_image);
		}
		if (result == VK_ERROR_DEVICE_LOST) m_lost = true;
		acquired = (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && m_swapchain;
	}

	if (acquired) {
		End_Rendering();
		VkCommandBuffer cmd = f.cmd;
		if (m_barrier_pending || m_draw_since_barrier) Barrier_All(cmd);
		m_barrier_pending = m_draw_since_barrier = false;
		Transition(cmd, m_back_buffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		VkImage target = m_swapchain_images[m_present_image];
		VkImageMemoryBarrier2 to_dst = {};
		to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		to_dst.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		to_dst.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		to_dst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to_dst.srcQueueFamilyIndex = to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		to_dst.image = target;
		to_dst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		to_dst.subresourceRange.levelCount = 1;
		to_dst.subresourceRange.layerCount = 1;
		VkDependencyInfo dep = {};
		dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &to_dst;
		vkCmdPipelineBarrier2(cmd, &dep);

		VkImageBlit blit = {};
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = 1;
		blit.srcOffsets[1].x = static_cast<int32_t>(m_back_buffer->m_desc.Width);
		blit.srcOffsets[1].y = static_cast<int32_t>(m_back_buffer->m_desc.Height);
		blit.srcOffsets[1].z = 1;
		blit.dstSubresource = blit.srcSubresource;
		blit.dstOffsets[1].x = static_cast<int32_t>(m_swapchain_extent.width);
		blit.dstOffsets[1].y = static_cast<int32_t>(m_swapchain_extent.height);
		blit.dstOffsets[1].z = 1;
		const bool same = m_back_buffer->m_desc.Width == m_swapchain_extent.width && m_back_buffer->m_desc.Height == m_swapchain_extent.height;
		vkCmdBlitImage(cmd, m_back_buffer->m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit, same ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);

		VkImageMemoryBarrier2 to_present = to_dst;
		to_present.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		to_present.dstAccessMask = 0;
		to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		dep.pImageMemoryBarriers = &to_present;
		vkCmdPipelineBarrier2(cmd, &dep);
	}

	Submit(acquired);
	HRESULT hr = S_OK;
	if (acquired) {
		VkPresentInfoKHR present = {};
		present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &m_rendered[m_present_image];
		present.swapchainCount = 1;
		present.pSwapchains = &m_swapchain;
		present.pImageIndices = &m_present_image;
		const VkResult result = vkQueuePresentKHR(m_queue, &present);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			vkDeviceWaitIdle(m_device);
			Create_Swap_Chain(m_back_buffer->m_desc.Width, m_back_buffer->m_desc.Height);
		}
		else if (result == VK_ERROR_DEVICE_LOST) m_lost = true;
	}
	if (m_lost) hr = HR_DEVICE_REMOVED;

	End_Frame_Statistics();
	m_frame = (m_frame + 1) % FRAMES_IN_FLIGHT;
	Begin_Frame();
	return hr;
}

unsigned __int64 Gpu::Free_Video_Memory()
{
	if (!m_allocator) return 0;
	VkPhysicalDeviceMemoryProperties memory = {};
	vkGetPhysicalDeviceMemoryProperties(m_physical, &memory);
	if (m_memory_budget) {
		VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
		vmaGetHeapBudgets(m_allocator, budgets);
		unsigned __int64 free_bytes = 0;
		for (uint32_t h = 0; h < memory.memoryHeapCount; ++h) {
			if (!(memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) continue;
			if (budgets[h].budget > budgets[h].usage) free_bytes += budgets[h].budget - budgets[h].usage;
		}
		return free_bytes;
	}
	unsigned __int64 local = 0;
	for (uint32_t h = 0; h < memory.memoryHeapCount; ++h) {
		if (memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) local += memory.memoryHeaps[h].size;
	}
	return local;
}

UINT Gpu::ShaderCompilerVersion() { return Dxc_Version(); }

HRESULT Gpu::CompileShader(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
						   GpuShaderInclude* include, const char* entry, const char* profile, UINT flags,
						   GpuBlob** code, GpuBlob** errors)
{
	return Dxc_Compile(source, length, name, macros, include, entry, profile, flags, code, errors);
}

HRESULT Gpu::PreprocessShader(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
							  GpuShaderInclude* include, GpuBlob** text)
{
	return Dxc_Preprocess(source, length, name, macros, include, text);
}

// ==================================================================================================
// Output
// ==================================================================================================

namespace {

class VulkanOutput final : public GpuOutput
{
public:
	bool Create_Interface() override
	{
		if (m_instance) return true;
		if (!Message_Lock_Ready) {
			InitializeCriticalSection(&Message_Lock);
			Message_Lock_Ready = true;
		}
		if (!Vulkan_Loader_Ready()) return false;
		uint32_t count = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> available(count);
		if (count) vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
		std::vector<const char*> extensions;
		extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
		extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
		std::vector<const char*> layers;
#ifdef _DEBUG
		// Validation where installed; its messages go to the game log (Drain_Debug_Messages).
		m_debug = Has_Name(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		if (m_debug) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		uint32_t layer_count = 0;
		vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
		std::vector<VkLayerProperties> layer_list(layer_count);
		if (layer_count) vkEnumerateInstanceLayerProperties(&layer_count, layer_list.data());
		for (const VkLayerProperties& l : layer_list) {
			if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) layers.push_back("VK_LAYER_KHRONOS_validation");
		}
#endif
		VkApplicationInfo app = {};
		app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app.pApplicationName = "GZH Neo";
		app.pEngineName = "WW3D";
		app.apiVersion = VK_API_VERSION_1_3;
		VkInstanceCreateInfo create = {};
		create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		create.pApplicationInfo = &app;
		create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		create.ppEnabledExtensionNames = extensions.data();
		create.enabledLayerCount = static_cast<uint32_t>(layers.size());
		create.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
		if (vkCreateInstance(&create, nullptr, &m_instance) != VK_SUCCESS) {
			m_instance = VK_NULL_HANDLE;
			return false;
		}
		volkLoadInstanceOnly(m_instance);
		if (m_debug) {
			EnterCriticalSection(&Message_Lock);
			if (!Messages) Messages = new MessageStore;
			LeaveCriticalSection(&Message_Lock);
			VkDebugUtilsMessengerCreateInfoEXT messenger = {};
			messenger.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			messenger.pfnUserCallback = Debug_Callback;
			if (vkCreateDebugUtilsMessengerEXT(m_instance, &messenger, nullptr, &m_messenger) != VK_SUCCESS) m_messenger = VK_NULL_HANDLE;
			WWDEBUG_SAY(("Vulkan: validation layer %s\n", layers.empty() ? "not installed (messages only if enabled outside the game)" : "on"));
		}

		// The adapter: the device the probe would choose.
		uint32_t devices = 0;
		vkEnumeratePhysicalDevices(m_instance, &devices, nullptr);
		std::vector<VkPhysicalDevice> physical(devices);
		if (devices) vkEnumeratePhysicalDevices(m_instance, &devices, physical.data());
		std::vector<VulkanDeviceInfo> infos(devices);
		for (uint32_t i = 0; i < devices; ++i) Vulkan_Describe_Device(physical[i], infos[i]);
		const int chosen = Vulkan_Choose_Device(infos.data(), devices);
		if (chosen >= 0) {
			m_physical = physical[chosen];
			m_info = infos[chosen];
		}
		return true;
	}

	void Release_Interface() override
	{
		// The shader compiler belongs to the Vulkan output: after a fall back to Direct3D 11 nothing
		// may keep it alive until process exit.
		Dxc_Unload();
		if (!m_instance) return;
		if (m_messenger) vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
		m_messenger = VK_NULL_HANDLE;
		vkDestroyInstance(m_instance, nullptr);
		m_instance = VK_NULL_HANDLE;
		m_physical = VK_NULL_HANDLE;
		Drain_Debug_Messages();
		if (Message_Lock_Ready) {
			EnterCriticalSection(&Message_Lock);
			delete Messages;
			Messages = nullptr;
			LeaveCriticalSection(&Message_Lock);
		}
	}

	void* Interface_Handle() override { return m_instance; }
	unsigned Get_Adapter_Count() override { return m_physical ? 1 : 0; }

	bool Get_Adapter_Info(unsigned adapter, GPU_ADAPTER_INFO* info) override
	{
		memset(info, 0, sizeof(GPU_ADAPTER_INFO));
		if (adapter != 0 || !m_physical) return false;
		std::snprintf(info->Description, sizeof(info->Description), "%s (Vulkan)", m_info.name);
		info->VendorId = m_info.vendor_id;
		info->DeviceId = m_info.device_id;
		return true;
	}

	bool Create_Device(void* hwnd, unsigned buffer_width, unsigned buffer_height, GpuDevice** device, GpuContext** context) override
	{
		*device = nullptr;
		*context = nullptr;
		if (!Create_Interface() || !m_physical) {
			Fail("no Vulkan instance or no usable device");
			return false;
		}
		Device_Error[0] = 0;
		char reason[256] = "";
		if (!Dxc_Load(reason, sizeof(reason))) {
			Fail(reason);
			return false;
		}
		Gpu* gpu = new Gpu();
		if (!gpu->Create(m_instance, m_physical, static_cast<HWND>(hwnd), buffer_width, buffer_height, m_debug)) {
			gpu->Destroy();
			gpu->m_instance = VK_NULL_HANDLE;
			if (gpu->Live_Objects() == 0) delete gpu;
			return false;
		}
		m_gpu = gpu;
		*device = gpu;
		*context = gpu;
		return true;
	}

	void Release_Device(GpuDevice*& device, GpuContext*& context) override
	{
		if (m_gpu) {
			Drain_Debug_Messages();
			m_gpu->Destroy();
			// Objects the game still holds keep the C++ object; the last one deletes it.
			m_gpu->m_instance = VK_NULL_HANDLE;
			Drain_Debug_Messages();
			Gpu* gpu = m_gpu;
			m_gpu = nullptr;
			if (gpu->Live_Objects() == 0) delete gpu;
		}
		device = nullptr;
		context = nullptr;
	}

	GpuTexture2D* Get_Back_Buffer() override { return m_gpu ? m_gpu->Back_Buffer() : nullptr; }
	HRESULT Resize_Buffers(unsigned w, unsigned h) override { return m_gpu ? m_gpu->Resize(w, h) : E_FAIL; }
	HRESULT Present(int swap_interval) override { return m_gpu ? m_gpu->Present(swap_interval) : E_FAIL; }
	bool Allows_Tearing() override { return m_gpu && m_gpu->Allows_Tearing(); }
	bool Is_Device_Lost(HRESULT hr) override { return hr == HR_DEVICE_REMOVED; }
	bool Check_Device_Lost() override { return m_gpu && m_gpu->Lost(); }

	void Drain_Debug_Messages() override
	{
		if (!Message_Lock_Ready) return;
		std::vector<std::string> lines;
		EnterCriticalSection(&Message_Lock);
		if (Messages) lines.swap(Messages->lines);
		LeaveCriticalSection(&Message_Lock);
		for ([[maybe_unused]] const std::string& line : lines) { WWDEBUG_SAY(("%s\n", line.c_str())); }
	}

	unsigned __int64 Get_Free_Video_Memory() override { return m_gpu ? m_gpu->Free_Video_Memory() : 0; }

private:
	VkInstance					m_instance = VK_NULL_HANDLE;
	VkPhysicalDevice			m_physical = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT	m_messenger = VK_NULL_HANDLE;
	VulkanDeviceInfo			m_info = {};
	bool						m_debug = false;
	Gpu*						m_gpu = nullptr;
};

}  // namespace

}  // namespace vkdev

const char* Vulkan_Device_Error()
{
	return vkdev::Device_Error[0] ? vkdev::Device_Error : "unknown reason";
}

GpuOutput* Vulkan_Get_Gpu_Output()
{
	// Lives until process exit like the render core.
	static vkdev::VulkanOutput TheOutput;
	return &TheOutput;
}
