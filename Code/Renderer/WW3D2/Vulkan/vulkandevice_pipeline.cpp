/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkandevice_pipeline.cpp -- shaders, input layouts, state objects, descriptor set layouts and the
// pipelines built from them.

#include "vulkandevice_internal.h"
#include "wwdebug.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

namespace vkdev {

// ==================================================================================================
// Translation tables
// ==================================================================================================

namespace {

VkBlendFactor Vk_Blend(GPU_BLEND b)
{
	switch (b) {
	case GPU_BLEND_ZERO:				return VK_BLEND_FACTOR_ZERO;
	case GPU_BLEND_ONE:					return VK_BLEND_FACTOR_ONE;
	case GPU_BLEND_SRC_COLOR:			return VK_BLEND_FACTOR_SRC_COLOR;
	case GPU_BLEND_INV_SRC_COLOR:		return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	case GPU_BLEND_SRC_ALPHA:			return VK_BLEND_FACTOR_SRC_ALPHA;
	case GPU_BLEND_INV_SRC_ALPHA:		return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case GPU_BLEND_DEST_ALPHA:			return VK_BLEND_FACTOR_DST_ALPHA;
	case GPU_BLEND_INV_DEST_ALPHA:		return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	case GPU_BLEND_DEST_COLOR:			return VK_BLEND_FACTOR_DST_COLOR;
	case GPU_BLEND_INV_DEST_COLOR:		return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	case GPU_BLEND_SRC_ALPHA_SAT:		return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
	case GPU_BLEND_BLEND_FACTOR:		return VK_BLEND_FACTOR_CONSTANT_COLOR;
	case GPU_BLEND_INV_BLEND_FACTOR:	return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
	case GPU_BLEND_SRC1_COLOR:			return VK_BLEND_FACTOR_SRC1_COLOR;
	case GPU_BLEND_INV_SRC1_COLOR:		return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
	case GPU_BLEND_SRC1_ALPHA:			return VK_BLEND_FACTOR_SRC1_ALPHA;
	case GPU_BLEND_INV_SRC1_ALPHA:		return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
	default:							return VK_BLEND_FACTOR_ONE;
	}
}

VkBlendOp Vk_Blend_Op(GPU_BLEND_OP op)
{
	switch (op) {
	case GPU_BLEND_OP_SUBTRACT:		return VK_BLEND_OP_SUBTRACT;
	case GPU_BLEND_OP_REV_SUBTRACT:	return VK_BLEND_OP_REVERSE_SUBTRACT;
	case GPU_BLEND_OP_MIN:			return VK_BLEND_OP_MIN;
	case GPU_BLEND_OP_MAX:			return VK_BLEND_OP_MAX;
	default:						return VK_BLEND_OP_ADD;
	}
}

VkCompareOp Vk_Compare(GPU_COMPARISON_FUNC f)
{
	switch (f) {
	case GPU_COMPARISON_NEVER:			return VK_COMPARE_OP_NEVER;
	case GPU_COMPARISON_LESS:			return VK_COMPARE_OP_LESS;
	case GPU_COMPARISON_EQUAL:			return VK_COMPARE_OP_EQUAL;
	case GPU_COMPARISON_LESS_EQUAL:		return VK_COMPARE_OP_LESS_OR_EQUAL;
	case GPU_COMPARISON_GREATER:		return VK_COMPARE_OP_GREATER;
	case GPU_COMPARISON_NOT_EQUAL:		return VK_COMPARE_OP_NOT_EQUAL;
	case GPU_COMPARISON_GREATER_EQUAL:	return VK_COMPARE_OP_GREATER_OR_EQUAL;
	default:							return VK_COMPARE_OP_ALWAYS;
	}
}

VkStencilOp Vk_Stencil(GPU_STENCIL_OP op)
{
	switch (op) {
	case GPU_STENCIL_OP_ZERO:		return VK_STENCIL_OP_ZERO;
	case GPU_STENCIL_OP_REPLACE:	return VK_STENCIL_OP_REPLACE;
	case GPU_STENCIL_OP_INCR_SAT:	return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
	case GPU_STENCIL_OP_DECR_SAT:	return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
	case GPU_STENCIL_OP_INVERT:		return VK_STENCIL_OP_INVERT;
	case GPU_STENCIL_OP_INCR:		return VK_STENCIL_OP_INCREMENT_AND_WRAP;
	case GPU_STENCIL_OP_DECR:		return VK_STENCIL_OP_DECREMENT_AND_WRAP;
	default:						return VK_STENCIL_OP_KEEP;
	}
}

VkStencilOpState Vk_Stencil_State(const GPU_DEPTH_STENCILOP_DESC& d, const GPU_DEPTH_STENCIL_DESC& desc)
{
	VkStencilOpState s = {};
	s.failOp = Vk_Stencil(d.StencilFailOp);
	s.passOp = Vk_Stencil(d.StencilPassOp);
	s.depthFailOp = Vk_Stencil(d.StencilDepthFailOp);
	s.compareOp = Vk_Compare(d.StencilFunc);
	s.compareMask = desc.StencilReadMask;
	s.writeMask = desc.StencilWriteMask;
	return s;
}

VkSamplerAddressMode Vk_Address(GPU_TEXTURE_ADDRESS_MODE m)
{
	switch (m) {
	case GPU_TEXTURE_ADDRESS_MIRROR:		return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	case GPU_TEXTURE_ADDRESS_CLAMP:			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case GPU_TEXTURE_ADDRESS_BORDER:		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	case GPU_TEXTURE_ADDRESS_MIRROR_ONCE:	return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
	default:								return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

/// D3D11 filter encoding: bit 0 mip linear, bit 2 mag linear, bit 4 min linear, 0x40 anisotropic, 0x80 comparison.
void Vk_Filter(GPU_FILTER filter, VkFilter* min, VkFilter* mag, VkSamplerMipmapMode* mip, bool* anisotropic, bool* comparison)
{
	const unsigned f = static_cast<unsigned>(filter);
	*anisotropic = (f & 0x40u) != 0;
	*comparison = (f & 0x180u) == 0x80u;
	*mip = ((f & 0x01u) || *anisotropic) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
	*mag = ((f & 0x04u) || *anisotropic) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	*min = ((f & 0x10u) || *anisotropic) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

/// The border colors Vulkan offers; D3D11 allows any, the game uses black and white.
VkBorderColor Vk_Border(const FLOAT* c)
{
	if (c[3] == 0.0f && c[0] == 0.0f && c[1] == 0.0f && c[2] == 0.0f) return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	if (c[0] >= 1.0f && c[1] >= 1.0f && c[2] >= 1.0f) return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
}

uint64_t Hash(uint64_t h, const void* data, size_t size)
{
	const uint8_t* p = static_cast<const uint8_t*>(data);
	for (size_t i = 0; i < size; ++i) {
		h ^= p[i];
		h *= 0x100000001B3ull;
	}
	return h;
}

}  // namespace

VkPrimitiveTopology Vk_Topology(GPU_PRIMITIVE_TOPOLOGY topology, uint32_t* topology_class)
{
	VkPrimitiveTopology t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	uint32_t c = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	switch (topology) {
	case GPU_PRIMITIVE_TOPOLOGY_POINTLIST:		t = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; c = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
	case GPU_PRIMITIVE_TOPOLOGY_LINELIST:		t = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; c = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
	case GPU_PRIMITIVE_TOPOLOGY_LINESTRIP:		t = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; c = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
	case GPU_PRIMITIVE_TOPOLOGY_TRIANGLELIST:	break;
	case GPU_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:	t = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
	default: {
		const unsigned v = static_cast<unsigned>(topology);
		const unsigned first = static_cast<unsigned>(GPU_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST);
		if (v >= first && v < first + 32) {
			t = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
			c = 100 + (v - first + 1);
		}
		break;
	}
	}
	if (topology_class) *topology_class = c;
	return t;
}

bool PipelineKey::operator==(const PipelineKey& o) const { return memcmp(this, &o, sizeof(PipelineKey)) == 0; }

size_t PipelineKeyHash::operator()(const PipelineKey& k) const
{
	return static_cast<size_t>(Hash(0xCBF29CE484222325ull, &k, sizeof(k)));
}

// ==================================================================================================
// Layouts and defaults
// ==================================================================================================

/// Pipeline cache file beside the shader cache of the render core (%LOCALAPPDATA%\CnCGenerals\ShaderCache).
bool Gpu::Pipeline_Cache_Path(char* path, size_t size) const
{
	char base[MAX_PATH];
	const DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof(base));
	if (length == 0 || length >= sizeof(base)) return false;
	char folder[MAX_PATH];
	std::snprintf(folder, sizeof(folder), "%s\\CnCGenerals", base);
	CreateDirectoryA(folder, nullptr);
	std::snprintf(folder, sizeof(folder), "%s\\CnCGenerals\\ShaderCache", base);
	CreateDirectoryA(folder, nullptr);
	const int n = std::snprintf(path, size, "%s\\vulkan_pipelines_%04x_%04x.bin", folder, m_properties.vendorID, m_properties.deviceID);
	return n > 0 && static_cast<size_t>(n) < size;
}

bool Gpu::Create_Layouts()
{
	static const VkShaderStageFlags stages[STAGE_COUNT] = {
		VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT,
		VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT };
	for (unsigned s = 0; s < STAGE_COUNT; ++s) {
		VkDescriptorSetLayoutBinding bindings[CB_SLOTS + SRV_SLOTS + SAMPLER_SLOTS] = {};
		unsigned n = 0;
		for (unsigned i = 0; i < CB_SLOTS; ++i, ++n) {
			bindings[n].binding = SPIRV_BINDING_CB + i;
			bindings[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[n].descriptorCount = 1;
			bindings[n].stageFlags = stages[s];
		}
		for (unsigned i = 0; i < SRV_SLOTS; ++i, ++n) {
			bindings[n].binding = SPIRV_BINDING_SRV + i;
			bindings[n].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			bindings[n].descriptorCount = 1;
			bindings[n].stageFlags = stages[s];
		}
		for (unsigned i = 0; i < SAMPLER_SLOTS; ++i, ++n) {
			bindings[n].binding = SPIRV_BINDING_SAMPLER + i;
			bindings[n].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
			bindings[n].descriptorCount = 1;
			bindings[n].stageFlags = stages[s];
		}
		VkDescriptorSetLayoutCreateInfo create = {};
		create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		create.bindingCount = n;
		create.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(m_device, &create, nullptr, &m_set_layouts[s]) != VK_SUCCESS) return false;
	}
	VkPipelineLayoutCreateInfo layout = {};
	layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout.setLayoutCount = STAGE_COUNT;
	layout.pSetLayouts = m_set_layouts;
	if (vkCreatePipelineLayout(m_device, &layout, nullptr, &m_pipeline_layout) != VK_SUCCESS) return false;
	// The driver checks the header (vendor, device, cache UUID) and ignores data of another driver.
	std::vector<uint8_t> saved;
	char path[MAX_PATH];
	if (Pipeline_Cache_Path(path, sizeof(path))) {
		FILE* file = fopen(path, "rb");
		if (file) {
			if (fseek(file, 0, SEEK_END) == 0) {
				const long size = ftell(file);
				if (size > 0 && size < (256L << 20) && fseek(file, 0, SEEK_SET) == 0) {
					saved.resize(static_cast<size_t>(size));
					if (fread(saved.data(), 1, saved.size(), file) != saved.size()) saved.clear();
				}
			}
			fclose(file);
		}
	}
	VkPipelineCacheCreateInfo cache = {};
	cache.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	cache.initialDataSize = saved.size();
	cache.pInitialData = saved.empty() ? nullptr : saved.data();
	if (vkCreatePipelineCache(m_device, &cache, nullptr, &m_pipeline_cache) != VK_SUCCESS) {
		cache.initialDataSize = 0;
		cache.pInitialData = nullptr;
		if (vkCreatePipelineCache(m_device, &cache, nullptr, &m_pipeline_cache) != VK_SUCCESS) m_pipeline_cache = VK_NULL_HANDLE;
	}
	if (!saved.empty()) { WWDEBUG_SAY(("Vulkan: pipeline cache %u KB from disk\n", static_cast<unsigned>(saved.size() >> 10))); }
	m_pipeline_stats = PipelineStats();
	m_pipeline_stats.disk_kb = cache.initialDataSize ? static_cast<unsigned>(saved.size() >> 10) : 0;

	// What empty slots read in Direct3D 11: zeros, and the default sampler.
	GPU_TEXTURE2D_DESC td = {};
	td.Width = td.Height = td.MipLevels = td.ArraySize = 1;
	td.Format = GPU_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = GPU_USAGE_DEFAULT;
	td.BindFlags = GPU_BIND_SHADER_RESOURCE;
	const uint32_t zero = 0;
	GPU_SUBRESOURCE_DATA init = { &zero, 4, 4 };
	m_null_texture = new Texture(this, td);
	if (!m_null_texture->Create(&init)) return false;
	VkImageViewCreateInfo view = {};
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = m_null_texture->m_image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_R8G8B8A8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	if (vkCreateImageView(m_device, &view, nullptr, &m_null_view) != VK_SUCCESS) return false;
	GPU_BUFFER_DESC bd = {};
	bd.ByteWidth = 65536;
	bd.Usage = GPU_USAGE_DEFAULT;
	bd.BindFlags = GPU_BIND_CONSTANT_BUFFER;
	std::vector<uint8_t> zeros(bd.ByteWidth, 0);
	GPU_SUBRESOURCE_DATA binit = { zeros.data(), 0, 0 };
	m_null_buffer = new Buffer(this, bd);
	if (!m_null_buffer->Create(&binit)) return false;
	GPU_SAMPLER_DESC sd = {};
	sd.Filter = GPU_FILTER_MIN_MAG_MIP_LINEAR;
	sd.AddressU = sd.AddressV = sd.AddressW = GPU_TEXTURE_ADDRESS_CLAMP;
	sd.MaxAnisotropy = 1;
	sd.ComparisonFunc = GPU_COMPARISON_NEVER;
	sd.MaxLOD = FLT_MAX;
	m_default_sampler = Create_Sampler(sd);
	return m_default_sampler != VK_NULL_HANDLE;
}

void Gpu::Destroy_Layouts()
{
	// Written next to the temporary name first, so a crash cannot leave half a file.
	char path[MAX_PATH], temp[MAX_PATH + 8];
	size_t size = 0;
	if (m_pipeline_cache && Pipeline_Cache_Path(path, sizeof(path)) &&
		vkGetPipelineCacheData(m_device, m_pipeline_cache, &size, nullptr) == VK_SUCCESS && size > 0) {
		std::vector<uint8_t> data(size);
		if (vkGetPipelineCacheData(m_device, m_pipeline_cache, &size, data.data()) == VK_SUCCESS) {
			std::snprintf(temp, sizeof(temp), "%s.tmp", path);
			FILE* file = fopen(temp, "wb");
			if (file) {
				const bool ok = fwrite(data.data(), 1, size, file) == size;
				if (fclose(file) == 0 && ok && MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING)) {
					WWDEBUG_SAY(("Vulkan: pipeline cache %u KB written\n", static_cast<unsigned>(size >> 10)));
				}
				else DeleteFileA(temp);
			}
		}
	}
	for (auto& p : m_pipelines) vkDestroyPipeline(m_device, p.second, nullptr);
	m_pipelines.clear();
	if (m_pipeline_cache) vkDestroyPipelineCache(m_device, m_pipeline_cache, nullptr);
	if (m_pipeline_layout) vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);
	for (VkDescriptorSetLayout& l : m_set_layouts) {
		if (l) vkDestroyDescriptorSetLayout(m_device, l, nullptr);
		l = VK_NULL_HANDLE;
	}
	m_pipeline_cache = VK_NULL_HANDLE;
	m_pipeline_layout = VK_NULL_HANDLE;
	if (m_null_view) vkDestroyImageView(m_device, m_null_view, nullptr);
	m_null_view = VK_NULL_HANDLE;
	if (m_default_sampler) vkDestroySampler(m_device, m_default_sampler, nullptr);
	m_default_sampler = VK_NULL_HANDLE;
	if (m_null_texture) m_null_texture->Release();
	if (m_null_buffer) m_null_buffer->Release();
	m_null_texture = nullptr;
	m_null_buffer = nullptr;
	// The two releases above went to the garbage list of the current frame.
	for (Frame& f : m_frames) Destroy_Garbage(f.garbage);
}

// ==================================================================================================
// Shaders
// ==================================================================================================

template <class Interface>
Shader<Interface>::Shader(Gpu* gpu, unsigned s) : Object<Interface>(gpu)
{
	stage = s;
	id = this->m_id;
	memset(&reflection, 0, sizeof(reflection));
	module = VK_NULL_HANDLE;
}

template <class Interface>
Shader<Interface>::~Shader()
{
	this->m_gpu->Forget_Pipelines(id);
	this->m_gpu->Retire(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<uint64_t>(module));
	for (const Linked& l : linked) this->m_gpu->Retire(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<uint64_t>(l.module));
}

InputLayout::~InputLayout()
{
	m_gpu->Forget_Pipelines(m_id);
}

template <class Interface, class Desc>
StateObject<Interface, Desc>::~StateObject()
{
	this->m_gpu->Forget_Pipelines(this->m_id);
}

template class StateObject<GpuBlendState, GPU_BLEND_DESC>;
template class StateObject<GpuDepthStencilState, GPU_DEPTH_STENCIL_DESC>;
template class StateObject<GpuRasterizerState, GPU_RASTERIZER_DESC>;

template class Shader<GpuVertexShader>;
template class Shader<GpuPixelShader>;
template class Shader<GpuHullShader>;
template class Shader<GpuDomainShader>;

HRESULT Gpu::Create_Shader(unsigned stage, const void* code, SIZE_T size, GpuObject** out)
{
	if (!code || size < 20 || (size % 4) != 0) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = nullptr;
	ShaderBase* base = nullptr;
	GpuObject* object = nullptr;
	switch (stage) {
	case STAGE_VS: { VertexShader* s = new VertexShader(this, stage); base = s; object = s; break; }
	case STAGE_PS: { PixelShader* s = new PixelShader(this, stage); base = s; object = s; break; }
	case STAGE_HS: { HullShader* s = new HullShader(this, stage); base = s; object = s; break; }
	default:       { DomainShader* s = new DomainShader(this, stage); base = s; object = s; break; }
	}
	base->words.resize(size / 4);
	memcpy(base->words.data(), code, size);
	char error[256];
	if (!Spirv_Prepare(base->words, stage, base->reflection, error, sizeof(error))) {
		WWDEBUG_SAY(("Vulkan: shader not usable: %s\n", error));
		object->Release();
		return E_INVALIDARG;
	}
	if (!Create_Shader_Module(base->words, &base->module)) {
		WWDEBUG_SAY(("Vulkan: vkCreateShaderModule failed\n"));
		object->Release();
		return E_FAIL;
	}
	*out = object;
	return S_OK;
}

HRESULT Gpu::CreateVertexShader(const void* code, SIZE_T size, void*, GpuVertexShader** out)
{
	GpuObject* o = nullptr;
	const HRESULT hr = Create_Shader(STAGE_VS, code, size, out ? &o : nullptr);
	if (out) *out = static_cast<GpuVertexShader*>(o);
	return hr;
}

HRESULT Gpu::CreatePixelShader(const void* code, SIZE_T size, void*, GpuPixelShader** out)
{
	GpuObject* o = nullptr;
	const HRESULT hr = Create_Shader(STAGE_PS, code, size, out ? &o : nullptr);
	if (out) *out = static_cast<GpuPixelShader*>(o);
	return hr;
}

HRESULT Gpu::CreateHullShader(const void* code, SIZE_T size, void*, GpuHullShader** out)
{
	GpuObject* o = nullptr;
	const HRESULT hr = Create_Shader(STAGE_HS, code, size, out ? &o : nullptr);
	if (out) *out = static_cast<GpuHullShader*>(o);
	return hr;
}

HRESULT Gpu::CreateDomainShader(const void* code, SIZE_T size, void*, GpuDomainShader** out)
{
	GpuObject* o = nullptr;
	const HRESULT hr = Create_Shader(STAGE_DS, code, size, out ? &o : nullptr);
	if (out) *out = static_cast<GpuDomainShader*>(o);
	return hr;
}

VkShaderModule Gpu::Stage_Module(ShaderBase* shader, ShaderBase* previous)
{
	if (!previous) return shader->module;
	for (const ShaderBase::Linked& l : shader->linked) {
		if (l.previous == previous->id) return l.module ? l.module : shader->module;
	}
	ShaderBase::Linked link;
	link.previous = previous->id;
	link.module = shader->module;
	std::vector<uint32_t> words = shader->words;
	if (Spirv_Link_Inputs(words, shader->reflection, previous->reflection)) {
		if (!Create_Shader_Module(words, &link.module)) return VK_NULL_HANDLE;
	}
	else {
		link.module = VK_NULL_HANDLE;		// same locations: the plain module
	}
	shader->linked.push_back(link);
	return link.module ? link.module : shader->module;
}

// ==================================================================================================
// Input layouts and state objects
// ==================================================================================================

HRESULT Gpu::CreateInputLayout(const GPU_INPUT_ELEMENT_DESC* elements, UINT count, const void*, SIZE_T, GpuInputLayout** out)
{
	if (!elements && count) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = nullptr;
	InputLayout* layout = new InputLayout(this);
	unsigned next_offset[VB_SLOTS] = {};
	for (UINT i = 0; i < count; ++i) {
		const GPU_INPUT_ELEMENT_DESC& e = elements[i];
		const FormatInfo& f = Format_Info(e.Format);
		if (e.InputSlot >= VB_SLOTS || f.format == VK_FORMAT_UNDEFINED) {
			WWDEBUG_SAY(("Vulkan: input element %s%u with format %d not supported\n", e.SemanticName, e.SemanticIndex, static_cast<int>(e.Format)));
			layout->Release();
			return E_INVALIDARG;
		}
		InputElement el = {};
		Spirv_Split_Semantic(e.SemanticName ? e.SemanticName : "", el.semantic, sizeof(el.semantic), nullptr);
		el.index = e.SemanticIndex;
		el.format = f.format;
		el.slot = e.InputSlot;
		el.offset = (e.AlignedByteOffset == GPU_APPEND_ALIGNED_ELEMENT) ? next_offset[e.InputSlot] : e.AlignedByteOffset;
		el.per_instance = e.InputSlotClass == GPU_INPUT_PER_INSTANCE_DATA;
		next_offset[e.InputSlot] = el.offset + f.block_bytes;
		layout->m_elements.push_back(el);
	}
	*out = layout;
	return S_OK;
}

HRESULT Gpu::CreateBlendState(const GPU_BLEND_DESC* desc, GpuBlendState** out)
{
	if (!desc) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = new BlendState(this, *desc);
	return S_OK;
}

HRESULT Gpu::CreateDepthStencilState(const GPU_DEPTH_STENCIL_DESC* desc, GpuDepthStencilState** out)
{
	if (!desc) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = new DepthStencilState(this, *desc);
	return S_OK;
}

HRESULT Gpu::CreateRasterizerState(const GPU_RASTERIZER_DESC* desc, GpuRasterizerState** out)
{
	if (!desc) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = new RasterizerState(this, *desc);
	return S_OK;
}

VkSampler Gpu::Create_Sampler(const GPU_SAMPLER_DESC& d)
{
	VkSamplerCreateInfo create = {};
	create.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	bool anisotropic = false, comparison = false;
	Vk_Filter(d.Filter, &create.minFilter, &create.magFilter, &create.mipmapMode, &anisotropic, &comparison);
	create.addressModeU = Vk_Address(d.AddressU);
	create.addressModeV = Vk_Address(d.AddressV);
	create.addressModeW = Vk_Address(d.AddressW);
	create.mipLodBias = d.MipLODBias;
	create.anisotropyEnable = (anisotropic && d.MaxAnisotropy > 1) ? VK_TRUE : VK_FALSE;
	create.maxAnisotropy = static_cast<float>(d.MaxAnisotropy < 1 ? 1 : (d.MaxAnisotropy > 16 ? 16 : d.MaxAnisotropy));
	if (create.maxAnisotropy > m_properties.limits.maxSamplerAnisotropy) create.maxAnisotropy = m_properties.limits.maxSamplerAnisotropy;
	create.compareEnable = comparison ? VK_TRUE : VK_FALSE;
	create.compareOp = Vk_Compare(d.ComparisonFunc);
	create.minLod = d.MinLOD;
	create.maxLod = d.MaxLOD > 1000.0f ? VK_LOD_CLAMP_NONE : d.MaxLOD;
	create.borderColor = Vk_Border(d.BorderColor);
	VkSampler sampler = VK_NULL_HANDLE;
	if (vkCreateSampler(m_device, &create, nullptr, &sampler) != VK_SUCCESS) return VK_NULL_HANDLE;
	return sampler;
}

SamplerState::~SamplerState()
{
	m_gpu->Retire(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(m_sampler));
}

HRESULT Gpu::CreateSamplerState(const GPU_SAMPLER_DESC* desc, GpuSamplerState** out)
{
	if (!desc) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = nullptr;
	SamplerState* s = new SamplerState(this, *desc);
	s->m_sampler = Create_Sampler(*desc);
	if (!s->m_sampler) {
		s->Release();
		return E_FAIL;
	}
	*out = s;
	return S_OK;
}

Query::~Query()
{
	if (m_active) m_gpu->End(this);
	m_gpu->Retire(VK_OBJECT_TYPE_QUERY_POOL, reinterpret_cast<uint64_t>(m_pool));
}

HRESULT Gpu::CreateQuery(const GPU_QUERY_DESC* desc, GpuQuery** out)
{
	if (!desc) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = nullptr;
	Query* q = new Query(this, *desc);
	VkQueryPoolCreateInfo create = {};
	create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	create.queryCount = 1;
	if (desc->Query == GPU_QUERY_TIMESTAMP) {
		create.queryType = VK_QUERY_TYPE_TIMESTAMP;
		if (vkCreateQueryPool(m_device, &create, nullptr, &q->m_pool) != VK_SUCCESS) q->m_pool = VK_NULL_HANDLE;
	}
	else if (desc->Query == GPU_QUERY_PIPELINE_STATISTICS && m_pipeline_statistics) {
		create.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
		create.pipelineStatistics = PIPELINE_STATISTICS;
		if (vkCreateQueryPool(m_device, &create, nullptr, &q->m_pool) != VK_SUCCESS) q->m_pool = VK_NULL_HANDLE;
	}
	*out = q;
	return S_OK;
}

// ==================================================================================================
// Pipelines
// ==================================================================================================

VkPipeline Gpu::Find_Pipeline()
{
	PipelineKey key;
	memset(&key, 0, sizeof(key));
	for (unsigned s = 0; s < STAGE_COUNT; ++s) key.shader[s] = m_stages[s].code ? m_stages[s].code->id : 0;
	key.input_layout = m_input_layout ? m_input_layout->Id() : 0;
	key.blend = m_blend ? m_blend->Id() : 0;
	key.depth_stencil = m_depth_stencil ? m_depth_stencil->Id() : 0;
	key.rasterizer = m_rasterizer ? m_rasterizer->Id() : 0;
	Vk_Topology(m_topology, &key.topology_class);
	for (unsigned i = 0; i < RT_SLOTS; ++i) {
		if (!m_targets[i]) continue;
		key.color_formats[i] = static_cast<uint32_t>(m_targets[i]->format);
		key.color_count = i + 1;
		key.samples = static_cast<uint32_t>(m_targets[i]->texture->m_samples);
	}
	if (m_depth) {
		key.depth_format = static_cast<uint32_t>(m_depth->format);
		key.samples = static_cast<uint32_t>(m_depth->texture->m_samples);
	}
	if (!key.samples) key.samples = 1;
	auto found = m_pipelines.find(key);
	if (found != m_pipelines.end()) return found->second;
	LARGE_INTEGER start, end, frequency;
	QueryPerformanceCounter(&start);
	VkPipeline pipeline = Create_Pipeline(key);
	QueryPerformanceCounter(&end);
	QueryPerformanceFrequency(&frequency);
	const double ms = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	PipelineStats& st = m_pipeline_stats;
	if (pipeline) ++st.built; else ++st.failed;
	st.total_ms += ms;
	st.frame_ms += ms;
	++st.frame_built;
	m_pipelines.emplace(key, pipeline);		// failures too, so they are not retried every draw
	return pipeline;
}

void Gpu::End_Frame_Statistics()
{
	PipelineStats& st = m_pipeline_stats;
	++st.frames;
	if (m_frame_barriers.size() < FRAME_SAMPLES_MAX) {
		m_frame_barriers.push_back(static_cast<uint32_t>(st.barriers - st.mark_barriers));
		m_frame_transitions.push_back(static_cast<uint32_t>(st.transitions - st.mark_transitions));
		m_frame_renderings.push_back(static_cast<uint32_t>(st.renderings - st.mark_renderings));
	}
	st.mark_barriers = st.barriers;
	st.mark_transitions = st.transitions;
	st.mark_renderings = st.renderings;
	if (!st.frame_built) return;
	++st.frames_with_builds;
	if (st.frame_ms > st.worst_ms) {
		st.worst_ms = st.frame_ms;
		st.worst_built = st.frame_built;
		st.worst_frame = m_serial;
	}
	// Two milliseconds of a 16.7 ms frame are a noticeable stall.
	if (st.frame_ms >= 2.0) {
		++st.frames_over;
		if (st.spike_count < PIPELINE_SPIKES) st.spikes[st.spike_count++] = { m_serial, st.frame_built, static_cast<float>(st.frame_ms) };
	}
	st.frame_ms = 0.0;
	st.frame_built = 0;
}

// Median and maximum of per-frame values.
static void Median_Max(const std::vector<uint32_t>& values, unsigned* median, unsigned* maximum)
{
	*median = *maximum = 0;
	if (values.empty()) return;
	std::vector<uint32_t> sorted(values);
	std::sort(sorted.begin(), sorted.end());
	*median = sorted[sorted.size() / 2];
	*maximum = sorted.back();
}

void Gpu::Get_Device_Statistics(char* text, unsigned size)
{
	if (!text || !size) return;
	const PipelineStats& st = m_pipeline_stats;
	unsigned b_med, b_max, t_med, t_max, r_med, r_max;
	Median_Max(m_frame_barriers, &b_med, &b_max);
	Median_Max(m_frame_transitions, &t_med, &t_max);
	Median_Max(m_frame_renderings, &r_med, &r_max);
	int n = std::snprintf(text, size, "VULKANPIPELINES frames %u built %u failed %u time %.1f ms frames_with_builds %u over_2ms %u "
		"worst_frame %.1f ms (%u, frame %llu) disk_cache %u KB in_memory %u per_frame_median barriers %u transitions %u "
		"renderings %u per_frame_max barriers %u transitions %u renderings %u total barriers %llu transitions %llu",
		st.frames, st.built, st.failed, st.total_ms, st.frames_with_builds, st.frames_over, st.worst_ms, st.worst_built,
		static_cast<unsigned long long>(st.worst_frame), st.disk_kb, static_cast<unsigned>(m_pipelines.size()), b_med, t_med, r_med,
		b_max, t_max, r_max, static_cast<unsigned long long>(st.barriers), static_cast<unsigned long long>(st.transitions));
	for (unsigned i = 0; i < st.spike_count && n > 0 && static_cast<unsigned>(n) < size; ++i) {
		const PipelineSpike& s = st.spikes[i];
		n += std::snprintf(text + n, size - n, "%s%llu:%u:%.1f", i ? "," : " spikes ", static_cast<unsigned long long>(s.frame), s.count, s.ms);
	}
}

void Gpu::Forget_Pipelines(uint64_t id)
{
	// Without this the cache grows with every shader and state object a long session creates and drops.
	for (auto it = m_pipelines.begin(); it != m_pipelines.end();) {
		const PipelineKey& k = it->first;
		bool uses = k.input_layout == id || k.blend == id || k.depth_stencil == id || k.rasterizer == id;
		for (unsigned s = 0; s < STAGE_COUNT; ++s) uses = uses || k.shader[s] == id;
		if (!uses) {
			++it;
			continue;
		}
		if (it->second == m_bound_pipeline) m_bound_pipeline = VK_NULL_HANDLE;
		Retire(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(it->second));
		it = m_pipelines.erase(it);
	}
}

VkPipeline Gpu::Create_Pipeline(const PipelineKey& key)
{
	ShaderBase* vs = m_stages[STAGE_VS].code;
	ShaderBase* ps = m_stages[STAGE_PS].code;
	ShaderBase* hs = m_stages[STAGE_HS].code;
	ShaderBase* ds = m_stages[STAGE_DS].code;
	if (!vs) {
		WWDEBUG_SAY(("Vulkan: pipeline without vertex shader\n"));
		return VK_NULL_HANDLE;
	}
	const bool tessellation = hs && ds && key.topology_class > 100;

	VkPipelineShaderStageCreateInfo stages[4] = {};
	uint32_t stage_count = 0;
	auto add = [&](VkShaderStageFlagBits bit, VkShaderModule module) {
		stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[stage_count].stage = bit;
		stages[stage_count].module = module;
		stages[stage_count].pName = "main";
		++stage_count;
	};
	add(VK_SHADER_STAGE_VERTEX_BIT, vs->module);
	ShaderBase* last = vs;
	if (tessellation) {
		add(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, Stage_Module(hs, vs));
		add(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, Stage_Module(ds, hs));
		last = ds;
	}
	if (ps) add(VK_SHADER_STAGE_FRAGMENT_BIT, Stage_Module(ps, last));
	for (uint32_t i = 0; i < stage_count; ++i) {
		if (!stages[i].module) {
			WWDEBUG_SAY(("Vulkan: shader module for stage %u could not be linked\n", i));
			return VK_NULL_HANDLE;
		}
	}

	// Vertex input: every shader input to the element with the same semantic.
	VkVertexInputAttributeDescription attributes[SPIRV_MAX_IO] = {};
	VkVertexInputBindingDescription bindings[VB_SLOTS] = {};
	uint32_t attribute_count = 0, binding_count = 0;
	unsigned slots_used = 0;
	for (unsigned i = 0; i < vs->reflection.input_count; ++i) {
		const SpirvIo& in = vs->reflection.inputs[i];
		if (in.location == ~0u) continue;
		const InputElement* match = nullptr;
		if (m_input_layout) {
			for (const InputElement& e : m_input_layout->m_elements) {
				if (e.index == in.index && strcmp(e.semantic, in.semantic) == 0) { match = &e; break; }
			}
		}
		if (!match) {
			char text[96];
			std::snprintf(text, sizeof(text), "vertex shader input %s%u has no input layout element", in.semantic, in.index);
			WWDEBUG_SAY(("Vulkan: %s\n", text));
			return VK_NULL_HANDLE;
		}
		attributes[attribute_count].location = in.location;
		attributes[attribute_count].binding = match->slot;
		attributes[attribute_count].format = match->format;
		attributes[attribute_count].offset = match->offset;
		++attribute_count;
		if (!(slots_used & (1u << match->slot))) {
			slots_used |= 1u << match->slot;
			bindings[binding_count].binding = match->slot;
			bindings[binding_count].stride = 0;		// dynamic
			bindings[binding_count].inputRate = match->per_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
			++binding_count;
		}
	}
	VkPipelineVertexInputStateCreateInfo vertex_input = {};
	vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input.vertexBindingDescriptionCount = binding_count;
	vertex_input.pVertexBindingDescriptions = bindings;
	vertex_input.vertexAttributeDescriptionCount = attribute_count;
	vertex_input.pVertexAttributeDescriptions = attributes;

	VkPipelineInputAssemblyStateCreateInfo assembly = {};
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = key.topology_class > 100 ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : static_cast<VkPrimitiveTopology>(key.topology_class);
	VkPipelineTessellationStateCreateInfo tess = {};
	tess.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
	tess.patchControlPoints = key.topology_class > 100 ? key.topology_class - 100 : 1;
	if (key.topology_class > 100 && !tessellation) {
		WWDEBUG_SAY(("Vulkan: patch list without hull and domain shader\n"));
		return VK_NULL_HANDLE;
	}

	VkPipelineViewportStateCreateInfo viewport = {};
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;

	GPU_RASTERIZER_DESC rd = {};
	rd.FillMode = GPU_FILL_SOLID;
	rd.CullMode = GPU_CULL_BACK;
	rd.DepthClipEnable = TRUE;
	if (m_rasterizer) rd = m_rasterizer->m_desc;
	VkPipelineRasterizationStateCreateInfo raster = {};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.depthClampEnable = rd.DepthClipEnable ? VK_FALSE : VK_TRUE;
	raster.polygonMode = (rd.FillMode == GPU_FILL_WIREFRAME && m_fill_mode_non_solid) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	raster.cullMode = rd.CullMode == GPU_CULL_NONE ? VK_CULL_MODE_NONE : (rd.CullMode == GPU_CULL_FRONT ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
	// The viewport is flipped (negative height), so the D3D winding holds as is.
	raster.frontFace = rd.FrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
	raster.depthBiasEnable = (rd.DepthBias != 0 || rd.SlopeScaledDepthBias != 0.0f) ? VK_TRUE : VK_FALSE;
	raster.depthBiasConstantFactor = static_cast<float>(rd.DepthBias);
	raster.depthBiasClamp = rd.DepthBiasClamp;
	raster.depthBiasSlopeFactor = rd.SlopeScaledDepthBias;
	raster.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample = {};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(key.samples);
	GPU_BLEND_DESC bd = {};
	bd.RenderTarget[0].RenderTargetWriteMask = 0x0F;
	bd.RenderTarget[0].SrcBlend = bd.RenderTarget[0].SrcBlendAlpha = GPU_BLEND_ONE;
	bd.RenderTarget[0].DestBlend = bd.RenderTarget[0].DestBlendAlpha = GPU_BLEND_ZERO;
	bd.RenderTarget[0].BlendOp = bd.RenderTarget[0].BlendOpAlpha = GPU_BLEND_OP_ADD;
	if (m_blend) bd = m_blend->m_desc;
	multisample.alphaToCoverageEnable = bd.AlphaToCoverageEnable ? VK_TRUE : VK_FALSE;

	GPU_DEPTH_STENCIL_DESC dd = {};
	dd.DepthEnable = TRUE;
	dd.DepthWriteMask = GPU_DEPTH_WRITE_MASK_ALL;
	dd.DepthFunc = GPU_COMPARISON_LESS;
	if (m_depth_stencil) dd = m_depth_stencil->m_desc;
	VkPipelineDepthStencilStateCreateInfo depth = {};
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	if (key.depth_format) {
		depth.depthTestEnable = dd.DepthEnable ? VK_TRUE : VK_FALSE;
		depth.depthWriteEnable = (dd.DepthEnable && dd.DepthWriteMask == GPU_DEPTH_WRITE_MASK_ALL) ? VK_TRUE : VK_FALSE;
		depth.depthCompareOp = Vk_Compare(dd.DepthFunc);
		depth.stencilTestEnable = dd.StencilEnable ? VK_TRUE : VK_FALSE;
		depth.front = Vk_Stencil_State(dd.FrontFace, dd);
		depth.back = Vk_Stencil_State(dd.BackFace, dd);
	}

	VkPipelineColorBlendAttachmentState attachments[RT_SLOTS] = {};
	for (uint32_t i = 0; i < key.color_count; ++i) {
		const GPU_RENDER_TARGET_BLEND_DESC& b = bd.RenderTarget[bd.IndependentBlendEnable ? i : 0];
		attachments[i].blendEnable = b.BlendEnable ? VK_TRUE : VK_FALSE;
		attachments[i].srcColorBlendFactor = Vk_Blend(b.SrcBlend);
		attachments[i].dstColorBlendFactor = Vk_Blend(b.DestBlend);
		attachments[i].colorBlendOp = Vk_Blend_Op(b.BlendOp);
		attachments[i].srcAlphaBlendFactor = Vk_Blend(b.SrcBlendAlpha);
		attachments[i].dstAlphaBlendFactor = Vk_Blend(b.DestBlendAlpha);
		attachments[i].alphaBlendOp = Vk_Blend_Op(b.BlendOpAlpha);
		attachments[i].colorWriteMask = b.RenderTargetWriteMask & 0x0F;		// R, G, B, A in the same bit order
	}
	VkPipelineColorBlendStateCreateInfo blend = {};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = key.color_count;
	blend.pAttachments = attachments;

	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
		VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE };
	VkPipelineDynamicStateCreateInfo dynamic = {};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	// The stride state must be set before every draw of a pipeline that has it, so only pipelines with
	// vertex buffers get it (the last entry).
	dynamic.dynamicStateCount = static_cast<uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0])) - (binding_count ? 0 : 1);
	dynamic.pDynamicStates = dynamic_states;

	VkFormat color_formats[RT_SLOTS];
	for (uint32_t i = 0; i < RT_SLOTS; ++i) color_formats[i] = static_cast<VkFormat>(key.color_formats[i]);
	VkPipelineRenderingCreateInfo rendering = {};
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = key.color_count;
	rendering.pColorAttachmentFormats = color_formats;
	rendering.depthAttachmentFormat = static_cast<VkFormat>(key.depth_format);
	const VkFormat depth_format = static_cast<VkFormat>(key.depth_format);
	if (depth_format == VK_FORMAT_D24_UNORM_S8_UINT || depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT) rendering.stencilAttachmentFormat = depth_format;

	VkGraphicsPipelineCreateInfo create = {};
	create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create.pNext = &rendering;
	create.stageCount = stage_count;
	create.pStages = stages;
	create.pVertexInputState = &vertex_input;
	create.pInputAssemblyState = &assembly;
	create.pTessellationState = tessellation ? &tess : nullptr;
	create.pViewportState = &viewport;
	create.pRasterizationState = &raster;
	create.pMultisampleState = &multisample;
	create.pDepthStencilState = &depth;
	create.pColorBlendState = &blend;
	create.pDynamicState = &dynamic;
	create.layout = m_pipeline_layout;
	VkPipeline pipeline = VK_NULL_HANDLE;
	const VkResult result = vkCreateGraphicsPipelines(m_device, m_pipeline_cache, 1, &create, nullptr, &pipeline);
	if (result != VK_SUCCESS) {
		WWDEBUG_SAY(("Vulkan: vkCreateGraphicsPipelines failed (%d)\n", static_cast<int>(result)));
		return VK_NULL_HANDLE;
	}
	return pipeline;
}

}  // namespace vkdev
