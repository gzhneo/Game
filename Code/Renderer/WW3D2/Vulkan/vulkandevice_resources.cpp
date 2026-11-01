/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkandevice_resources.cpp -- buffers, textures, views, maps, uploads and copies of the Vulkan
// device layer.

#include "vulkandevice_internal.h"
#include "wwdebug.h"

#include <algorithm>
#include <cstring>

namespace vkdev {

namespace {

const HRESULT HR_WAS_STILL_DRAWING = static_cast<HRESULT>(0x887A000AL);	// DXGI_ERROR_WAS_STILL_DRAWING

VkBufferUsageFlags Buffer_Usage(const GPU_BUFFER_DESC& desc)
{
	VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	if (desc.BindFlags & GPU_BIND_VERTEX_BUFFER)	usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (desc.BindFlags & GPU_BIND_INDEX_BUFFER)		usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (desc.BindFlags & GPU_BIND_CONSTANT_BUFFER)	usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	return usage;
}

/// Pipeline stages and accesses that use an image in a layout: a transition waits for these uses under the old
/// layout and makes its result visible to them under the new one. Unknown layouts take the full scope.
void Layout_Scope(VkImageLayout layout, VkPipelineStageFlags2* stages, VkAccessFlags2* access)
{
	switch (layout) {
	case VK_IMAGE_LAYOUT_UNDEFINED:
		*stages = VK_PIPELINE_STAGE_2_NONE;
		*access = VK_ACCESS_2_NONE;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		*stages = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
		*access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		*stages = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
		*access = VK_ACCESS_2_TRANSFER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		*stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		*access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		*stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		*access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		// sampled in every shader stage (the terrain reads heights in the vertex and tessellation stages)
		*stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
		*access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		break;
	default:
		*stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		*access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		break;
	}
}

/// The layout a texture rests in between uses, so the next draw needs no transition.
VkImageLayout Resting_Layout(const Texture* t)
{
	if (t->m_desc.BindFlags & GPU_BIND_SHADER_RESOURCE)	return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	if (t->m_desc.BindFlags & GPU_BIND_RENDER_TARGET)	return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	if (t->m_desc.BindFlags & GPU_BIND_DEPTH_STENCIL)	return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

unsigned Mip_Size(unsigned size, unsigned mip)
{
	const unsigned s = size >> mip;
	return s ? s : 1;
}

VkImageSubresourceLayers Layers(const Texture* t, unsigned subresource)
{
	VkImageSubresourceLayers l = {};
	l.aspectMask = t->m_format->aspect;
	l.mipLevel = subresource % t->m_desc.MipLevels;
	l.baseArrayLayer = subresource / t->m_desc.MipLevels;
	l.layerCount = 1;
	return l;
}

/// Copies rows of `row_bytes` from a source with `source_pitch` into a tight destination.
void Copy_Rows(uint8_t* dest, unsigned row_bytes, unsigned rows, const void* source, unsigned source_pitch)
{
	const uint8_t* s = static_cast<const uint8_t*>(source);
	if (source_pitch == row_bytes || rows == 1) {
		memcpy(dest, s, static_cast<size_t>(row_bytes) * rows);
		return;
	}
	for (unsigned r = 0; r < rows; ++r) memcpy(dest + static_cast<size_t>(r) * row_bytes, s + static_cast<size_t>(r) * source_pitch, row_bytes);
}

}  // namespace

// ==================================================================================================
// Buffers
// ==================================================================================================

Buffer::Buffer(Gpu* gpu, const GPU_BUFFER_DESC& desc) : Object<GpuBuffer>(gpu), m_desc(desc), m_used_since_discard(false)
{
	kind = RESOURCE_BUFFER;
	last_use = 0;
	memset(&m_current, 0, sizeof(m_current));
}

Buffer::~Buffer()
{
	m_gpu->Retire(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(m_current.buffer), m_current.allocation);
	for (const Slice& s : m_free) m_gpu->Retire(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(s.buffer), s.allocation);
	for (const Retired& r : m_retired) m_gpu->Retire(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(r.slice.buffer), r.slice.allocation);
}

bool Buffer::New_Slice(Slice& slice)
{
	if (!m_gpu->Allocate_Buffer(m_desc.ByteWidth, Buffer_Usage(m_desc), true, false, slice) || !slice.mapped) return false;
	// Suballocated memory may still hold an earlier resource's data; Direct3D 11 hands out zeroed memory.
	memset(slice.mapped, 0, m_desc.ByteWidth);
	return true;
}

bool Buffer::Create(const GPU_SUBRESOURCE_DATA* initial)
{
	if (Is_Dynamic()) {
		if (!New_Slice(m_current)) return false;
		if (initial && initial->pSysMem) memcpy(m_current.mapped, initial->pSysMem, m_desc.ByteWidth);
		return true;
	}
	if (Is_Staging()) {
		const bool readback = (m_desc.CPUAccessFlags & GPU_CPU_ACCESS_READ) != 0;
		if (!m_gpu->Allocate_Buffer(m_desc.ByteWidth, Buffer_Usage(m_desc), true, readback, m_current) || !m_current.mapped) return false;
		if (initial && initial->pSysMem) memcpy(m_current.mapped, initial->pSysMem, m_desc.ByteWidth);
		else memset(m_current.mapped, 0, m_desc.ByteWidth);
		return true;
	}
	if (!m_gpu->Allocate_Buffer(m_desc.ByteWidth, Buffer_Usage(m_desc), false, false, m_current)) return false;
	if (initial && initial->pSysMem) {
		Slice upload;
		if (!m_gpu->Upload_Space(m_desc.ByteWidth, 16, upload)) return false;
		memcpy(upload.mapped, initial->pSysMem, m_desc.ByteWidth);
		VkCommandBuffer cmd = m_gpu->Transfer_Command(this);
		VkBufferCopy region = {};
		region.srcOffset = upload.offset;
		region.size = m_desc.ByteWidth;
		vkCmdCopyBuffer(cmd, upload.buffer, m_current.buffer, 1, &region);
	}
	else {
		// Zero like Direct3D 11: callers fill such buffers piecewise (terrain rows, rings) and draw the rest.
		vkCmdFillBuffer(m_gpu->Transfer_Command(this), m_current.buffer, 0, VK_WHOLE_SIZE, 0);
	}
	return true;
}

bool Buffer::Discard()
{
	if (!m_used_since_discard || m_gpu->Completed(last_use)) {
		m_used_since_discard = false;
		return true;
	}
	Retired r;
	r.serial = last_use;
	r.slice = m_current;
	m_retired.push_back(r);
	for (size_t i = 0; i < m_retired.size();) {
		if (m_gpu->Completed(m_retired[i].serial)) {
			m_free.push_back(m_retired[i].slice);
			m_retired[i] = m_retired.back();
			m_retired.pop_back();
		}
		else ++i;
	}
	if (!m_free.empty()) {
		m_current = m_free.back();
		m_free.pop_back();
	}
	else if (!New_Slice(m_current)) {
		// Out of memory: keep writing into the oldest slice rather than failing the map.
		m_current = m_retired.front().slice;
		m_retired.erase(m_retired.begin());
		m_gpu->Log_Once("dynamic buffer could not grow; a discard overwrote data still in use");
	}
	m_used_since_discard = false;
	return true;
}

HRESULT Gpu::CreateBuffer(const GPU_BUFFER_DESC* desc, const GPU_SUBRESOURCE_DATA* initial, GpuBuffer** out)
{
	if (!desc || desc->ByteWidth == 0) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = nullptr;
	Buffer* buffer = new Buffer(this, *desc);
	if (!buffer->Create(initial)) {
		buffer->Release();
		return E_OUTOFMEMORY;
	}
	*out = buffer;
	return S_OK;
}

// ==================================================================================================
// Textures
// ==================================================================================================

Texture::Texture(Gpu* gpu, const GPU_TEXTURE2D_DESC& desc) : Object<GpuTexture2D>(gpu), m_desc(desc)
{
	kind = RESOURCE_TEXTURE;
	last_use = 0;
	m_format = &Format_Info(desc.Format);
	m_image = VK_NULL_HANDLE;
	m_allocation = nullptr;
	m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_samples = VK_SAMPLE_COUNT_1_BIT;
	memset(&m_staging, 0, sizeof(m_staging));
	memset(m_pending, 0, sizeof(m_pending));
	if (m_desc.MipLevels == 0) {
		unsigned levels = 1, size = std::max(desc.Width, desc.Height);
		while (size > 1) { size >>= 1; ++levels; }
		m_desc.MipLevels = levels;
	}
	if (m_desc.ArraySize == 0) m_desc.ArraySize = 1;
}

Texture::~Texture()
{
	for (const PendingMap& p : m_pending) {
		if (p.slice.buffer) m_gpu->Retire(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(p.slice.buffer), p.slice.allocation);
	}
	m_gpu->Retire(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_image), m_allocation);
	m_gpu->Retire(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(m_staging.buffer), m_staging.allocation);
}

VkImageSubresourceRange Texture::Full_Range() const
{
	VkImageSubresourceRange r = {};
	r.aspectMask = m_format->aspect;
	r.levelCount = m_desc.MipLevels;
	r.layerCount = m_desc.ArraySize;
	return r;
}

bool Texture::Create(const GPU_SUBRESOURCE_DATA* initial)
{
	// A typeless 32 or 16 bit texture that is not a depth target is a color texture.
	if (!(m_desc.BindFlags & GPU_BIND_DEPTH_STENCIL)) {
		if (m_desc.Format == GPU_FORMAT_R32_TYPELESS) m_format = &Format_Info(GPU_FORMAT_R32_FLOAT);
		else if (m_desc.Format == GPU_FORMAT_R16_TYPELESS) m_format = &Format_Info(GPU_FORMAT_R16_UNORM);
	}
	if (m_format->format == VK_FORMAT_UNDEFINED) {
		WWDEBUG_SAY(("Vulkan: texture format %d is not supported\n", static_cast<int>(m_desc.Format)));
		return false;
	}

	if (Is_Staging()) {
		// All subresources one after another, rows tight, each start 16 byte aligned.
		VkDeviceSize offset = 0;
		for (unsigned layer = 0; layer < m_desc.ArraySize; ++layer) {
			for (unsigned mip = 0; mip < m_desc.MipLevels; ++mip) {
				SubresourceLayout s;
				s.width = Mip_Size(m_desc.Width, mip);
				s.height = Mip_Size(m_desc.Height, mip);
				unsigned rows = 0;
				Format_Pitch(*m_format, s.width, s.height, &s.row_pitch, &rows);
				s.depth_pitch = s.row_pitch * rows;
				s.offset = offset;
				offset = (offset + s.depth_pitch + 15) & ~static_cast<VkDeviceSize>(15);
				m_subresources.push_back(s);
			}
		}
		const bool readback = (m_desc.CPUAccessFlags & GPU_CPU_ACCESS_READ) != 0;
		if (!m_gpu->Allocate_Buffer(offset, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, readback, m_staging) ||
			!m_staging.mapped) return false;
		memset(m_staging.mapped, 0, static_cast<size_t>(offset));
		if (initial) {
			for (unsigned i = 0; i < Subresource_Count(); ++i) {
				if (!initial[i].pSysMem) continue;
				const SubresourceLayout& s = m_subresources[i];
				Copy_Rows(m_staging.mapped + s.offset, s.row_pitch, s.depth_pitch / s.row_pitch, initial[i].pSysMem, initial[i].SysMemPitch);
			}
		}
		return true;
	}

	const unsigned count = m_desc.SampleDesc.Count ? m_desc.SampleDesc.Count : 1;
	// Vulkan only knows power-of-two counts up to 64; anything else is an invalid enum value.
	if (count > 64 || (count & (count - 1)) != 0) {
		char text[96];
		std::snprintf(text, sizeof(text), "image %ux%u with %u samples is not supported", m_desc.Width, m_desc.Height, count);
		Fail(text);
		return false;
	}
	m_samples = static_cast<VkSampleCountFlagBits>(count);
	VkImageCreateInfo image = {};
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = m_format->format;
	image.extent.width = m_desc.Width;
	image.extent.height = m_desc.Height;
	image.extent.depth = 1;
	image.mipLevels = m_desc.MipLevels;
	image.arrayLayers = m_desc.ArraySize;
	image.samples = m_samples;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (m_desc.BindFlags & GPU_BIND_SHADER_RESOURCE)	image.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if (m_desc.BindFlags & GPU_BIND_RENDER_TARGET)		image.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (m_desc.BindFlags & GPU_BIND_DEPTH_STENCIL)		image.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VmaAllocationCreateInfo alloc = {};
	alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	if (m_desc.BindFlags & (GPU_BIND_RENDER_TARGET | GPU_BIND_DEPTH_STENCIL)) alloc.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	const VkResult result = vmaCreateImage(m_gpu->Allocator(), &image, &alloc, &m_image, &m_allocation, nullptr);
	if (result != VK_SUCCESS) {
		char text[128];
		std::snprintf(text, sizeof(text), "image %ux%u format %d, %u mips, %u samples could not be created", m_desc.Width,
			m_desc.Height, static_cast<int>(m_desc.Format), m_desc.MipLevels, static_cast<unsigned>(m_samples));
		Fail(text, static_cast<int>(result));
		m_image = VK_NULL_HANDLE;
		m_allocation = nullptr;
		return false;
	}

	// Direct3D 11 hands out zeroed memory; suballocated Vulkan memory may still hold an earlier resource's
	// data. So every subresource without initial data starts at zero, as callers may rely on.
	bool complete = initial != nullptr;
	for (unsigned i = 0; complete && i < Subresource_Count(); ++i) complete = initial[i].pSysMem != nullptr;
	VkCommandBuffer cmd = m_gpu->Transfer_Command(this);
	m_gpu->Transition(cmd, this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	if (!complete && !Zero(cmd)) return false;
	if (initial) {
		for (unsigned i = 0; i < Subresource_Count(); ++i) {
			if (!initial[i].pSysMem) continue;
			const unsigned mip = i % m_desc.MipLevels;
			const unsigned w = Mip_Size(m_desc.Width, mip), h = Mip_Size(m_desc.Height, mip);
			unsigned row_bytes = 0, rows = 0;
			Format_Pitch(*m_format, w, h, &row_bytes, &rows);
			Slice upload;
			if (!m_gpu->Upload_Space(static_cast<VkDeviceSize>(row_bytes) * rows, m_format->block_bytes, upload)) return false;
			Copy_Rows(upload.mapped, row_bytes, rows, initial[i].pSysMem, initial[i].SysMemPitch);
			VkBufferImageCopy region = {};
			region.bufferOffset = upload.offset;
			region.imageSubresource = Layers(this, i);
			region.imageExtent.width = w;
			region.imageExtent.height = h;
			region.imageExtent.depth = 1;
			vkCmdCopyBufferToImage(cmd, upload.buffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		}
	}
	m_gpu->Transition(cmd, this, Resting_Layout(this));
	return true;
}

bool Texture::Zero(VkCommandBuffer cmd)
{
	const VkImageSubresourceRange range = Full_Range();
	if (m_format->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) {
		const VkClearDepthStencilValue zero = { 0.0f, 0 };
		vkCmdClearDepthStencilImage(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
		return true;
	}
	if (m_format->block_size == 1) {
		const VkClearColorValue zero = {};
		vkCmdClearColorImage(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
		return true;
	}
	// Block-compressed images cannot be cleared: copy zeros, one region of the size of mip 0 for all.
	unsigned row_bytes = 0, rows = 0;
	Format_Pitch(*m_format, m_desc.Width, m_desc.Height, &row_bytes, &rows);
	const VkDeviceSize bytes = static_cast<VkDeviceSize>(row_bytes) * rows;
	Slice zeros;
	if (!m_gpu->Upload_Space(bytes, m_format->block_bytes, zeros)) return false;
	memset(zeros.mapped, 0, static_cast<size_t>(bytes));
	for (unsigned i = 0; i < Subresource_Count(); ++i) {
		const unsigned mip = i % m_desc.MipLevels;
		VkBufferImageCopy region = {};
		region.bufferOffset = zeros.offset;
		region.imageSubresource = Layers(this, i);
		region.imageExtent.width = Mip_Size(m_desc.Width, mip);
		region.imageExtent.height = Mip_Size(m_desc.Height, mip);
		region.imageExtent.depth = 1;
		vkCmdCopyBufferToImage(cmd, zeros.buffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}
	return true;
}

HRESULT Gpu::CreateTexture2D(const GPU_TEXTURE2D_DESC* desc, const GPU_SUBRESOURCE_DATA* initial, GpuTexture2D** out)
{
	if (!desc || desc->Width == 0 || desc->Height == 0) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = nullptr;
	Texture* texture = new Texture(this, *desc);
	if (!texture->Create(initial)) {
		texture->Release();
		return E_OUTOFMEMORY;
	}
	*out = texture;
	return S_OK;
}

HRESULT Gpu::CheckMultisampleQualityLevels(GPU_FORMAT format, UINT samples, UINT* levels)
{
	if (!levels) return E_INVALIDARG;
	*levels = 0;
	const FormatInfo& info = Format_Info(format);
	if (info.format == VK_FORMAT_UNDEFINED) return S_OK;
	VkImageFormatProperties props = {};
	const bool depth = (info.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
	const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		(depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	if (vkGetPhysicalDeviceImageFormatProperties(m_physical, info.format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &props) != VK_SUCCESS)
		return S_OK;
	if (samples == 0 || samples > 64 || (samples & (samples - 1)) != 0) return S_OK;
	if ((props.sampleCounts & samples) == samples) *levels = 1;
	return S_OK;
}

// ==================================================================================================
// Views
// ==================================================================================================

template <class Interface>
View<Interface>::View(Gpu* gpu, Texture* t) : Object<Interface>(gpu)
{
	texture = t;
	t->AddRef();
	view = VK_NULL_HANDLE;
	format = VK_FORMAT_UNDEFINED;
	mip = 0;
	aspect = 0;
}

template <class Interface>
View<Interface>::~View()
{
	this->m_gpu->Retire(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(view));
	texture->Release();
}

template class View<GpuShaderResourceView>;
template class View<GpuRenderTargetView>;
template class View<GpuDepthStencilView>;

namespace {

Texture* As_Texture(GpuResource* resource)
{
	if (!resource) return nullptr;
	ResourceBase* base = static_cast<ResourceBase*>(resource->Native());
	return base->kind == RESOURCE_TEXTURE ? static_cast<Texture*>(base) : nullptr;
}

Buffer* As_Buffer(GpuResource* resource)
{
	if (!resource) return nullptr;
	ResourceBase* base = static_cast<ResourceBase*>(resource->Native());
	return base->kind == RESOURCE_BUFFER ? static_cast<Buffer*>(base) : nullptr;
}

bool Create_Image_View(VkDevice device, Texture* t, VkFormat format, const VkComponentSwizzle* swizzle, VkImageAspectFlags aspect,
					   unsigned first_mip, unsigned mips, VkImageView* out)
{
	VkImageViewCreateInfo create = {};
	create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	create.image = t->m_image;
	create.viewType = VK_IMAGE_VIEW_TYPE_2D;
	create.format = format;
	if (swizzle) {
		create.components.r = swizzle[0];
		create.components.g = swizzle[1];
		create.components.b = swizzle[2];
		create.components.a = swizzle[3];
	}
	create.subresourceRange.aspectMask = aspect;
	create.subresourceRange.baseMipLevel = first_mip;
	create.subresourceRange.levelCount = mips;
	create.subresourceRange.layerCount = 1;
	return vkCreateImageView(device, &create, nullptr, out) == VK_SUCCESS;
}

}  // namespace

HRESULT Gpu::CreateShaderResourceView(GpuResource* resource, const GPU_SHADER_RESOURCE_VIEW_DESC* desc, GpuShaderResourceView** out)
{
	Texture* t = As_Texture(resource);
	if (!t || t->Is_Staging() || !(t->m_desc.BindFlags & GPU_BIND_SHADER_RESOURCE)) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = nullptr;
	const bool depth = (t->m_format->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
	const FormatInfo& info = depth ? *t->m_format : ((desc && desc->Format != GPU_FORMAT_UNKNOWN) ? Format_Info(desc->Format) : *t->m_format);
	unsigned first = 0, count = t->m_desc.MipLevels;
	if (desc && desc->ViewDimension == GPU_SRV_DIMENSION_TEXTURE2D) {
		first = desc->Texture2D.MostDetailedMip;
		count = (desc->Texture2D.MipLevels == ~0u || desc->Texture2D.MipLevels == 0) ? t->m_desc.MipLevels - first : desc->Texture2D.MipLevels;
	}
	ShaderResourceView* v = new ShaderResourceView(this, t);
	v->format = info.format == VK_FORMAT_UNDEFINED ? t->m_format->format : info.format;
	v->aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	v->mip = first;
	if (!Create_Image_View(m_device, t, v->format, depth ? nullptr : info.swizzle, v->aspect, first, count, &v->view)) {
		v->Release();
		return E_FAIL;
	}
	*out = v;
	return S_OK;
}

HRESULT Gpu::CreateRenderTargetView(GpuResource* resource, const GPU_RENDER_TARGET_VIEW_DESC* desc, GpuRenderTargetView** out)
{
	Texture* t = As_Texture(resource);
	if (!t || !(t->m_desc.BindFlags & GPU_BIND_RENDER_TARGET)) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = nullptr;
	RenderTargetView* v = new RenderTargetView(this, t);
	v->format = t->m_format->format;
	v->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	v->mip = (desc && desc->ViewDimension == GPU_RTV_DIMENSION_TEXTURE2D) ? desc->Texture2D.MipSlice : 0;
	if (!Create_Image_View(m_device, t, v->format, nullptr, v->aspect, v->mip, 1, &v->view)) {
		v->Release();
		return E_FAIL;
	}
	*out = v;
	return S_OK;
}

HRESULT Gpu::CreateDepthStencilView(GpuResource* resource, const GPU_DEPTH_STENCIL_VIEW_DESC* desc, GpuDepthStencilView** out)
{
	Texture* t = As_Texture(resource);
	if (!t || !(t->m_desc.BindFlags & GPU_BIND_DEPTH_STENCIL)) return E_INVALIDARG;
	if (!out) return S_FALSE;
	*out = nullptr;
	DepthStencilView* v = new DepthStencilView(this, t);
	v->format = t->m_format->format;
	v->aspect = t->m_format->aspect;
	v->mip = (desc && desc->ViewDimension == GPU_DSV_DIMENSION_TEXTURE2D) ? desc->Texture2D.MipSlice : 0;
	if (!Create_Image_View(m_device, t, v->format, nullptr, v->aspect, v->mip, 1, &v->view)) {
		v->Release();
		return E_FAIL;
	}
	*out = v;
	return S_OK;
}

// ==================================================================================================
// Transfers
// ==================================================================================================

void Gpu::Transition(VkCommandBuffer cmd, Texture* t, VkImageLayout layout)
{
	if (!t->m_image || t->m_layout == layout) return;
	++m_pipeline_stats.transitions;
	VkImageMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	Layout_Scope(t->m_layout, &barrier.srcStageMask, &barrier.srcAccessMask);
	Layout_Scope(layout, &barrier.dstStageMask, &barrier.dstAccessMask);
	// only writes need making available; a use that only read is waited for through the stages
	barrier.srcAccessMask &= VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
	barrier.oldLayout = t->m_layout;
	barrier.newLayout = layout;
	barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = t->m_image;
	barrier.subresourceRange = t->Full_Range();
	VkDependencyInfo dep = {};
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2(cmd, &dep);
	t->m_layout = layout;
}

VkCommandBuffer Gpu::Transfer_Command(ResourceBase* target)
{
	Frame& f = m_frames[m_frame];
	if (target && target->last_use < m_serial && !m_serialized) {
		// Not used by this frame yet: before the frame, without splitting its rendering.
		// One barrier when the batch opens (the previous frame may still read what it overwrites); copies into
		// different resources need none between them, a second write into the same resource does.
		if (!f.upload_open) {
			VkCommandBufferBeginInfo begin = {};
			begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(f.upload, &begin);
			f.upload_open = true;
			++m_upload_batch;
			Barrier_All(f.upload);
		}
		else if (target->upload_batch == m_upload_batch) {
			Barrier_All(f.upload);
		}
		target->upload_batch = m_upload_batch;
		return f.upload;
	}
	End_Rendering();
	Barrier_All(f.cmd);
	m_barrier_pending = true;
	m_draw_since_barrier = false;
	if (target) Use(target);
	return f.cmd;
}

// ==================================================================================================
// Map, Unmap, UpdateSubresource
// ==================================================================================================

HRESULT Gpu::Map(GpuResource* resource, UINT subresource, GPU_MAP type, UINT flags, GPU_MAPPED_SUBRESOURCE* mapped)
{
	if (!resource || !mapped) return E_INVALIDARG;
	memset(mapped, 0, sizeof(*mapped));
	ResourceBase* base = static_cast<ResourceBase*>(resource->Native());
	const bool cpu_read_or_write_back = (type == GPU_MAP_READ || type == GPU_MAP_READ_WRITE || type == GPU_MAP_WRITE);

	if (base->kind == RESOURCE_BUFFER) {
		Buffer* b = static_cast<Buffer*>(base);
		if (b->Is_Dynamic()) {
			if (type == GPU_MAP_WRITE_DISCARD) b->Discard();
			else if (type != GPU_MAP_WRITE_NO_OVERWRITE) return E_INVALIDARG;
		}
		else if (b->Is_Staging() && cpu_read_or_write_back) {
			if (!Completed(b->last_use)) {
				if (flags & GPU_MAP_FLAG_DO_NOT_WAIT) return HR_WAS_STILL_DRAWING;
				Flush_And_Wait();
			}
		}
		else return E_INVALIDARG;
		mapped->pData = b->m_current.mapped;
		mapped->RowPitch = mapped->DepthPitch = b->m_desc.ByteWidth;
		return S_OK;
	}

	Texture* t = static_cast<Texture*>(base);
	if (subresource >= t->Subresource_Count()) return E_INVALIDARG;
	if (t->Is_Staging()) {
		if (!Completed(t->last_use)) {
			if (flags & GPU_MAP_FLAG_DO_NOT_WAIT) return HR_WAS_STILL_DRAWING;
			Flush_And_Wait();
		}
		const SubresourceLayout& s = t->m_subresources[subresource];
		mapped->pData = t->m_staging.mapped + s.offset;
		mapped->RowPitch = s.row_pitch;
		mapped->DepthPitch = s.depth_pitch;
		return S_OK;
	}
	if (t->m_desc.Usage != GPU_USAGE_DYNAMIC || type != GPU_MAP_WRITE_DISCARD || subresource >= MAX_MIPS) return E_INVALIDARG;
	const unsigned mip = subresource % t->m_desc.MipLevels;
	unsigned row_bytes = 0, rows = 0;
	Format_Pitch(*t->m_format, Mip_Size(t->m_desc.Width, mip), Mip_Size(t->m_desc.Height, mip), &row_bytes, &rows);
	// A buffer of its own, not the upload ring: the map may stay open while frames go by (texture loading).
	Texture::PendingMap& p = t->m_pending[subresource];
	const VkDeviceSize size = static_cast<VkDeviceSize>(row_bytes) * rows;
	const bool reuse = p.slice.buffer && !p.active && p.size == size && Completed(p.serial);
	if (!reuse) {
		if (p.slice.buffer) Retire(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(p.slice.buffer), p.slice.allocation);
		memset(&p.slice, 0, sizeof(p.slice));
		if (!Allocate_Buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, false, p.slice) || !p.slice.mapped) {
			memset(&p.slice, 0, sizeof(p.slice));
			p.active = false;
			return E_OUTOFMEMORY;
		}
		p.size = size;
	}
	p.active = true;
	p.row_pitch = row_bytes;
	mapped->pData = p.slice.mapped;
	mapped->RowPitch = row_bytes;
	mapped->DepthPitch = row_bytes * rows;
	return S_OK;
}

void Gpu::Unmap(GpuResource* resource, UINT subresource)
{
	if (!resource) return;
	ResourceBase* base = static_cast<ResourceBase*>(resource->Native());
	if (base->kind != RESOURCE_TEXTURE) return;
	Texture* t = static_cast<Texture*>(base);
	if (t->Is_Staging() || subresource >= MAX_MIPS || !t->m_pending[subresource].active) return;
	Texture::PendingMap& p = t->m_pending[subresource];
	p.active = false;
	const unsigned mip = subresource % t->m_desc.MipLevels;
	VkCommandBuffer cmd = Transfer_Command(t);
	Transition(cmd, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.imageSubresource = Layers(t, subresource);
	region.imageExtent.width = Mip_Size(t->m_desc.Width, mip);
	region.imageExtent.height = Mip_Size(t->m_desc.Height, mip);
	region.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(cmd, p.slice.buffer, t->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	Transition(cmd, t, Resting_Layout(t));
	// Loaded once: give the buffer back. Updated again and again: keep it for the next map.
	p.serial = m_serial;
	if (++p.maps < 2) {
		Retire(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(p.slice.buffer), p.slice.allocation);
		memset(&p.slice, 0, sizeof(p.slice));
	}
}

void Gpu::UpdateSubresource(GpuResource* resource, UINT subresource, const GPU_BOX* box, const void* data, UINT row_pitch, UINT)
{
	if (!resource || !data) return;
	ResourceBase* base = static_cast<ResourceBase*>(resource->Native());
	if (base->kind == RESOURCE_BUFFER) {
		Buffer* b = static_cast<Buffer*>(base);
		const UINT offset = box ? box->left : 0;
		const UINT size = box ? box->right - box->left : b->m_desc.ByteWidth;
		if (size == 0 || offset + size > b->m_desc.ByteWidth) return;
		if (b->Is_Dynamic() || b->Is_Staging()) {
			Log_Once("UpdateSubresource on a dynamic or staging buffer");
			return;
		}
		Slice upload;
		if (!Upload_Space(size, 16, upload)) return;
		memcpy(upload.mapped, data, size);
		VkCommandBuffer cmd = Transfer_Command(b);
		VkBufferCopy region = {};
		region.srcOffset = upload.offset;
		region.dstOffset = offset;
		region.size = size;
		vkCmdCopyBuffer(cmd, upload.buffer, b->m_current.buffer, 1, &region);
		return;
	}

	Texture* t = static_cast<Texture*>(base);
	if (subresource >= t->Subresource_Count()) return;
	const unsigned mip = subresource % t->m_desc.MipLevels;
	const unsigned mw = Mip_Size(t->m_desc.Width, mip), mh = Mip_Size(t->m_desc.Height, mip);
	unsigned x = 0, y = 0, w = mw, h = mh;
	if (box) {
		x = box->left;
		y = box->top;
		w = box->right - box->left;
		h = box->bottom - box->top;
		if (x + w > mw || y + h > mh || w == 0 || h == 0) return;
	}
	unsigned row_bytes = 0, rows = 0;
	Format_Pitch(*t->m_format, w, h, &row_bytes, &rows);
	if (t->Is_Staging()) {
		const SubresourceLayout& s = t->m_subresources[subresource];
		const unsigned bs = t->m_format->block_size;
		uint8_t* dest = t->m_staging.mapped + s.offset + static_cast<size_t>(y / bs) * s.row_pitch + static_cast<size_t>(x / bs) * t->m_format->block_bytes;
		for (unsigned r = 0; r < rows; ++r) memcpy(dest + static_cast<size_t>(r) * s.row_pitch, static_cast<const uint8_t*>(data) + static_cast<size_t>(r) * row_pitch, row_bytes);
		return;
	}
	Slice upload;
	if (!Upload_Space(static_cast<VkDeviceSize>(row_bytes) * rows, t->m_format->block_bytes, upload)) return;
	Copy_Rows(upload.mapped, row_bytes, rows, data, row_pitch ? row_pitch : row_bytes);
	VkCommandBuffer cmd = Transfer_Command(t);
	Transition(cmd, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	VkBufferImageCopy region = {};
	region.bufferOffset = upload.offset;
	region.imageSubresource = Layers(t, subresource);
	region.imageOffset.x = static_cast<int32_t>(x);
	region.imageOffset.y = static_cast<int32_t>(y);
	region.imageExtent.width = w;
	region.imageExtent.height = h;
	region.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(cmd, upload.buffer, t->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	Transition(cmd, t, Resting_Layout(t));
}

// ==================================================================================================
// Copies
// ==================================================================================================

void Gpu::Copy_Texture_Region(Texture* dest, unsigned dest_sub, unsigned x, unsigned y, Texture* source, unsigned source_sub, const GPU_BOX* box)
{
	if (dest_sub >= dest->Subresource_Count() || source_sub >= source->Subresource_Count()) return;
	const unsigned smip = source_sub % source->m_desc.MipLevels;
	unsigned sx = 0, sy = 0, w = Mip_Size(source->m_desc.Width, smip), h = Mip_Size(source->m_desc.Height, smip);
	if (box) {
		sx = box->left;
		sy = box->top;
		w = box->right - box->left;
		h = box->bottom - box->top;
	}
	if (w == 0 || h == 0) return;
	// Direct3D 11 drops a copy whose box leaves the source or whose region leaves the destination; a
	// Vulkan copy outside an image is undefined and can overwrite other allocations.
	const unsigned dmip = dest_sub % dest->m_desc.MipLevels;
	const unsigned sw = Mip_Size(source->m_desc.Width, smip), sh = Mip_Size(source->m_desc.Height, smip);
	const unsigned dw = Mip_Size(dest->m_desc.Width, dmip), dh = Mip_Size(dest->m_desc.Height, dmip);
	if (box && (box->right <= box->left || box->bottom <= box->top)) return;
	if (sx + w > sw || sy + h > sh || x + w > dw || y + h > dh || source->m_samples != dest->m_samples ||
		source->m_format->block_bytes != dest->m_format->block_bytes || source->m_format->block_size != dest->m_format->block_size) {
		static unsigned reported = 0;
		if (reported++ < 16) {
			WWDEBUG_SAY(("Vulkan: copy skipped like Direct3D 11 does: source %ux%u (mip %u, %u samples, format %d) box %u,%u %ux%u, "
				"destination %ux%u (mip %u, %u samples, format %d) at %u,%u\n", sw, sh, smip, static_cast<unsigned>(source->m_samples),
				static_cast<int>(source->m_desc.Format), sx, sy, w, h, dw, dh, dmip, static_cast<unsigned>(dest->m_samples),
				static_cast<int>(dest->m_desc.Format), x, y));
		}
		return;
	}
	VkCommandBuffer cmd = Transfer_Command(nullptr);
	Use(dest);
	Use(source);

	if (!source->Is_Staging() && !dest->Is_Staging()) {
		Transition(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		Transition(cmd, dest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		VkImageCopy region = {};
		region.srcSubresource = Layers(source, source_sub);
		region.srcOffset.x = static_cast<int32_t>(sx);
		region.srcOffset.y = static_cast<int32_t>(sy);
		region.dstSubresource = Layers(dest, dest_sub);
		region.dstOffset.x = static_cast<int32_t>(x);
		region.dstOffset.y = static_cast<int32_t>(y);
		region.extent.width = w;
		region.extent.height = h;
		region.extent.depth = 1;
		vkCmdCopyImage(cmd, source->m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		Transition(cmd, source, Resting_Layout(source));
		Transition(cmd, dest, Resting_Layout(dest));
		return;
	}
	if (source->Is_Staging() && dest->Is_Staging()) {
		const SubresourceLayout& s = source->m_subresources[source_sub];
		const SubresourceLayout& d = dest->m_subresources[dest_sub];
		const unsigned bs = source->m_format->block_size, bb = source->m_format->block_bytes;
		unsigned row_bytes = 0, rows = 0;
		Format_Pitch(*source->m_format, w, h, &row_bytes, &rows);
		std::vector<VkBufferCopy> regions(rows);
		for (unsigned r = 0; r < rows; ++r) {
			regions[r].srcOffset = s.offset + static_cast<VkDeviceSize>(sy / bs + r) * s.row_pitch + (sx / bs) * bb;
			regions[r].dstOffset = d.offset + static_cast<VkDeviceSize>(y / bs + r) * d.row_pitch + (x / bs) * bb;
			regions[r].size = row_bytes;
		}
		vkCmdCopyBuffer(cmd, source->m_staging.buffer, dest->m_staging.buffer, rows, regions.data());
		return;
	}
	// One side is a staging texture: buffer <-> image with the staging rows as buffer layout.
	Texture* staging = source->Is_Staging() ? source : dest;
	Texture* image = source->Is_Staging() ? dest : source;
	const unsigned staging_sub = source->Is_Staging() ? source_sub : dest_sub;
	const unsigned image_sub = source->Is_Staging() ? dest_sub : source_sub;
	const unsigned bx = source->Is_Staging() ? sx : x, by = source->Is_Staging() ? sy : y;	// staging side
	const unsigned ix = source->Is_Staging() ? x : sx, iy = source->Is_Staging() ? y : sy;	// image side
	const SubresourceLayout& s = staging->m_subresources[staging_sub];
	const unsigned bs = staging->m_format->block_size;
	VkBufferImageCopy region = {};
	region.bufferOffset = s.offset + static_cast<VkDeviceSize>(by / bs) * s.row_pitch + (bx / bs) * staging->m_format->block_bytes;
	region.bufferRowLength = s.row_pitch / staging->m_format->block_bytes * bs;
	region.bufferImageHeight = 0;
	region.imageSubresource = Layers(image, image_sub);
	region.imageOffset.x = static_cast<int32_t>(ix);
	region.imageOffset.y = static_cast<int32_t>(iy);
	region.imageExtent.width = w;
	region.imageExtent.height = h;
	region.imageExtent.depth = 1;
	if (source->Is_Staging()) {
		Transition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		vkCmdCopyBufferToImage(cmd, staging->m_staging.buffer, image->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}
	else {
		Transition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		vkCmdCopyImageToBuffer(cmd, image->m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->m_staging.buffer, 1, &region);
	}
	Transition(cmd, image, Resting_Layout(image));
}

void Gpu::CopyResource(GpuResource* dest, GpuResource* source)
{
	if (!dest || !source || dest == source) return;
	Buffer* db = As_Buffer(dest);
	Buffer* sb = As_Buffer(source);
	if (db && sb) {
		CopySubresourceRegion(dest, 0, 0, 0, 0, source, 0, nullptr);
		return;
	}
	Texture* dt = As_Texture(dest);
	Texture* st = As_Texture(source);
	if (!dt || !st) return;
	// Direct3D 11 copies whole resources only between identical shapes and ignores the call otherwise.
	if (dt->m_desc.Width != st->m_desc.Width || dt->m_desc.Height != st->m_desc.Height ||
		dt->m_desc.MipLevels != st->m_desc.MipLevels || dt->m_desc.ArraySize != st->m_desc.ArraySize) {
		static unsigned reported = 0;
		if (reported++ < 16) {
			WWDEBUG_SAY(("Vulkan: CopyResource skipped like Direct3D 11 does: %ux%u, %u mips -> %ux%u, %u mips\n", st->m_desc.Width,
				st->m_desc.Height, st->m_desc.MipLevels, dt->m_desc.Width, dt->m_desc.Height, dt->m_desc.MipLevels));
		}
		return;
	}
	const unsigned count = dt->Subresource_Count();
	for (unsigned i = 0; i < count; ++i) Copy_Texture_Region(dt, i, 0, 0, st, i, nullptr);
}

void Gpu::CopySubresourceRegion(GpuResource* dest, UINT dest_sub, UINT x, UINT y, UINT, GpuResource* source, UINT source_sub, const GPU_BOX* box)
{
	if (!dest || !source) return;
	Buffer* db = As_Buffer(dest);
	Buffer* sb = As_Buffer(source);
	if (db && sb) {
		const UINT from = box ? box->left : 0;
		UINT size = box ? box->right - box->left : std::min(db->m_desc.ByteWidth, sb->m_desc.ByteWidth);
		if (from + size > sb->m_desc.ByteWidth || x + size > db->m_desc.ByteWidth || size == 0) return;
		VkCommandBuffer cmd = Transfer_Command(nullptr);
		Use(db);
		Use(sb);
		db->m_used_since_discard = sb->m_used_since_discard = true;
		VkBufferCopy region = {};
		region.srcOffset = sb->m_current.offset + from;
		region.dstOffset = db->m_current.offset + x;
		region.size = size;
		vkCmdCopyBuffer(cmd, sb->m_current.buffer, db->m_current.buffer, 1, &region);
		return;
	}
	Texture* dt = As_Texture(dest);
	Texture* st = As_Texture(source);
	if (dt && st) Copy_Texture_Region(dt, dest_sub, x, y, st, source_sub, box);
}

void Gpu::ResolveSubresource(GpuResource* dest, UINT dest_sub, GpuResource* source, UINT source_sub, GPU_FORMAT)
{
	Texture* dt = As_Texture(dest);
	Texture* st = As_Texture(source);
	if (!dt || !st || dt->Is_Staging() || st->Is_Staging()) return;
	if (st->m_samples == VK_SAMPLE_COUNT_1_BIT) {
		Copy_Texture_Region(dt, dest_sub, 0, 0, st, source_sub, nullptr);
		return;
	}
	VkCommandBuffer cmd = Transfer_Command(nullptr);
	Use(dt);
	Use(st);
	Transition(cmd, st, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	Transition(cmd, dt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	VkImageResolve region = {};
	region.srcSubresource = Layers(st, source_sub);
	region.dstSubresource = Layers(dt, dest_sub);
	region.extent.width = std::min(st->m_desc.Width, dt->m_desc.Width);
	region.extent.height = std::min(st->m_desc.Height, dt->m_desc.Height);
	region.extent.depth = 1;
	vkCmdResolveImage(cmd, st->m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dt->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	Transition(cmd, st, Resting_Layout(st));
	Transition(cmd, dt, Resting_Layout(dt));
}

}  // namespace vkdev
