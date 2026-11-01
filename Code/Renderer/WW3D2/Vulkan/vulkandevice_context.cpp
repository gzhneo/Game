/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkandevice_context.cpp -- the immediate context of the Vulkan device layer: bindings, rendering
// scopes, draws, clears and queries.

#include "vulkandevice_internal.h"
#include "wwdebug.h"

#include <algorithm>
#include <cstring>

namespace vkdev {

namespace {

const unsigned DESCRIPTOR_SETS_PER_POOL = 256;

template <class T, class I>
T* Concrete(I* object)
{
	return object ? static_cast<T*>(object) : nullptr;
}

ShaderBase* Code_Of(GpuObject* shader)
{
	return shader ? static_cast<ShaderBase*>(shader->Native()) : nullptr;
}

void Use_Buffer(Gpu* gpu, Buffer* b)
{
	b->last_use = gpu->Serial();
	b->m_used_since_discard = true;
}

}  // namespace

// ==================================================================================================
// Command buffer state
// ==================================================================================================

void Gpu::Reset_Command_State()
{
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
	for (StageState& s : m_stages) memset(&s.written, 0, sizeof(s.written));
}

void Gpu::Barrier_All(VkCommandBuffer cmd)
{
	++m_pipeline_stats.barriers;
	VkMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
	VkDependencyInfo dep = {};
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.memoryBarrierCount = 1;
	dep.pMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2(cmd, &dep);
}

void Gpu::End_Rendering()
{
	if (!m_rendering) return;
	vkCmdEndRendering(m_frames[m_frame].cmd);
	m_rendering = false;
}

bool Gpu::Begin_Rendering()
{
	++m_pipeline_stats.renderings;
	VkCommandBuffer cmd = m_frames[m_frame].cmd;
	VkRenderingAttachmentInfo colors[RT_SLOTS] = {};
	uint32_t count = 0;
	uint32_t width = ~0u, height = ~0u;
	for (unsigned i = 0; i < RT_SLOTS; ++i) {
		colors[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		if (!m_targets[i]) continue;
		ViewBase* v = m_targets[i].Get();
		colors[i].imageView = v->view;
		colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		count = i + 1;
		width = std::min(width, std::max(1u, v->texture->m_desc.Width >> v->mip));
		height = std::min(height, std::max(1u, v->texture->m_desc.Height >> v->mip));
	}
	VkRenderingAttachmentInfo depth = {};
	depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	VkRenderingAttachmentInfo stencil = depth;
	VkRenderingInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	if (m_depth) {
		ViewBase* v = m_depth.Get();
		depth.imageView = v->view;
		depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		info.pDepthAttachment = &depth;
		if (v->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) {
			stencil = depth;
			info.pStencilAttachment = &stencil;
		}
		width = std::min(width, std::max(1u, v->texture->m_desc.Width >> v->mip));
		height = std::min(height, std::max(1u, v->texture->m_desc.Height >> v->mip));
	}
	if (count == 0 && !m_depth) return false;
	info.renderArea.extent.width = width;
	info.renderArea.extent.height = height;
	info.layerCount = 1;
	info.colorAttachmentCount = count;
	info.pColorAttachments = colors;
	vkCmdBeginRendering(cmd, &info);
	m_rendering = true;
	m_render_extent = info.renderArea.extent;
	m_dynamic_dirty = true;
	return true;
}

bool Gpu::Is_Target(const Texture* texture) const
{
	for (unsigned i = 0; i < RT_SLOTS; ++i) {
		if (m_targets[i] && m_targets[i]->texture == texture) return true;
	}
	return m_depth && m_depth->texture == texture;
}

// ==================================================================================================
// Input assembler, shaders, output merger
// ==================================================================================================

void Gpu::IASetInputLayout(GpuInputLayout* layout)			{ m_input_layout.Set(Concrete<InputLayout>(layout)); }
void Gpu::IASetPrimitiveTopology(GPU_PRIMITIVE_TOPOLOGY topology)	{ m_topology = topology; }

void Gpu::IASetVertexBuffers(UINT start, UINT count, GpuBuffer* const* buffers, const UINT* strides, const UINT* offsets)
{
	for (UINT i = 0; i < count && start + i < VB_SLOTS; ++i) {
		m_vertex_buffers[start + i].Set(buffers ? Concrete<Buffer>(buffers[i]) : nullptr);
		m_vertex_strides[start + i] = strides ? strides[i] : 0;
		m_vertex_offsets[start + i] = offsets ? offsets[i] : 0;
	}
}

void Gpu::IASetIndexBuffer(GpuBuffer* buffer, GPU_FORMAT format, UINT offset)
{
	m_index_buffer.Set(Concrete<Buffer>(buffer));
	m_index_format = format;
	m_index_offset = offset;
}

void Gpu::Set_Shader(unsigned stage, GpuObject* shader)
{
	StageState& s = m_stages[stage];
	s.shader.Set(shader);
	s.code = Code_Of(shader);
}

void Gpu::Set_Constant_Buffers(unsigned stage, UINT start, UINT count, GpuBuffer* const* buffers, const UINT* first, const UINT* num)
{
	StageState& s = m_stages[stage];
	for (UINT i = 0; i < count && start + i < CB_SLOTS; ++i) {
		s.cb[start + i].Set(buffers ? Concrete<Buffer>(buffers[i]) : nullptr);
		s.cb_first[start + i] = first ? first[i] : 0;
		s.cb_count[start + i] = num ? num[i] : 0;
	}
}

void Gpu::Set_Shader_Resources(unsigned stage, UINT start, UINT count, GpuShaderResourceView* const* views)
{
	StageState& s = m_stages[stage];
	for (UINT i = 0; i < count && start + i < SRV_SLOTS; ++i) {
		ShaderResourceView* v = views ? Concrete<ShaderResourceView>(views[i]) : nullptr;
		// Direct3D 11 does not bind a resource that is a current target; it binds nothing instead.
		if (v && Is_Target(v->texture)) {
			Log_Once("a render target was bound as shader resource; the slot stays empty like in Direct3D 11");
			v = nullptr;
		}
		s.srv[start + i].Set(v);
	}
}

void Gpu::Set_Samplers(unsigned stage, UINT start, UINT count, GpuSamplerState* const* samplers)
{
	StageState& s = m_stages[stage];
	for (UINT i = 0; i < count && start + i < SAMPLER_SLOTS; ++i) s.sampler[start + i].Set(samplers ? Concrete<SamplerState>(samplers[i]) : nullptr);
}

void Gpu::VSSetShader(GpuVertexShader* shader, void* const*, UINT)	{ Set_Shader(STAGE_VS, shader); }
void Gpu::PSSetShader(GpuPixelShader* shader, void* const*, UINT)	{ Set_Shader(STAGE_PS, shader); }
void Gpu::HSSetShader(GpuHullShader* shader, void* const*, UINT)	{ Set_Shader(STAGE_HS, shader); }
void Gpu::DSSetShader(GpuDomainShader* shader, void* const*, UINT)	{ Set_Shader(STAGE_DS, shader); }
void Gpu::VSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* b)	{ Set_Constant_Buffers(STAGE_VS, start, count, b, nullptr, nullptr); }
void Gpu::PSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* b)	{ Set_Constant_Buffers(STAGE_PS, start, count, b, nullptr, nullptr); }
void Gpu::HSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* b)	{ Set_Constant_Buffers(STAGE_HS, start, count, b, nullptr, nullptr); }
void Gpu::DSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* b)	{ Set_Constant_Buffers(STAGE_DS, start, count, b, nullptr, nullptr); }
void Gpu::VSSetConstantBuffers1(UINT start, UINT count, GpuBuffer* const* b, const UINT* first, const UINT* num) { Set_Constant_Buffers(STAGE_VS, start, count, b, first, num); }
void Gpu::PSSetConstantBuffers1(UINT start, UINT count, GpuBuffer* const* b, const UINT* first, const UINT* num) { Set_Constant_Buffers(STAGE_PS, start, count, b, first, num); }
void Gpu::VSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* v)	{ Set_Shader_Resources(STAGE_VS, start, count, v); }
void Gpu::PSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* v)	{ Set_Shader_Resources(STAGE_PS, start, count, v); }
void Gpu::HSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* v)	{ Set_Shader_Resources(STAGE_HS, start, count, v); }
void Gpu::DSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* v)	{ Set_Shader_Resources(STAGE_DS, start, count, v); }
void Gpu::VSSetSamplers(UINT start, UINT count, GpuSamplerState* const* s)	{ Set_Samplers(STAGE_VS, start, count, s); }
void Gpu::PSSetSamplers(UINT start, UINT count, GpuSamplerState* const* s)	{ Set_Samplers(STAGE_PS, start, count, s); }
void Gpu::HSSetSamplers(UINT start, UINT count, GpuSamplerState* const* s)	{ Set_Samplers(STAGE_HS, start, count, s); }
void Gpu::DSSetSamplers(UINT start, UINT count, GpuSamplerState* const* s)	{ Set_Samplers(STAGE_DS, start, count, s); }

void Gpu::OMSetRenderTargets(UINT count, GpuRenderTargetView* const* targets, GpuDepthStencilView* depth)
{
	bool changed = (m_depth.Get() != Concrete<DepthStencilView>(depth));
	for (UINT i = 0; i < RT_SLOTS; ++i) {
		RenderTargetView* v = (i < count && targets) ? Concrete<RenderTargetView>(targets[i]) : nullptr;
		if (m_targets[i].Get() != v) changed = true;
	}
	if (!changed) return;
	End_Rendering();
	for (UINT i = 0; i < RT_SLOTS; ++i) m_targets[i].Set((i < count && targets) ? Concrete<RenderTargetView>(targets[i]) : nullptr);
	m_depth.Set(Concrete<DepthStencilView>(depth));
	// Direct3D 11 unbinds shader resources that are now targets.
	for (StageState& s : m_stages) {
		for (unsigned i = 0; i < SRV_SLOTS; ++i) {
			if (s.srv[i] && Is_Target(s.srv[i]->texture)) s.srv[i].Reset();
		}
	}
}

void Gpu::OMSetBlendState(GpuBlendState* state, const FLOAT factor[4], UINT)
{
	m_blend.Set(Concrete<BlendState>(state));
	for (unsigned i = 0; i < 4; ++i) m_blend_factor[i] = factor ? factor[i] : 1.0f;
	m_dynamic_dirty = true;
}

void Gpu::OMSetDepthStencilState(GpuDepthStencilState* state, UINT stencil_ref)
{
	m_depth_stencil.Set(Concrete<DepthStencilState>(state));
	m_stencil_ref = stencil_ref;
	m_dynamic_dirty = true;
}

void Gpu::RSSetViewports(UINT count, const GPU_VIEWPORT* viewports)
{
	if (count == 0 || !viewports) return;
	m_viewport = viewports[0];
	m_viewport_set = true;
	m_dynamic_dirty = true;
}

void Gpu::RSSetState(GpuRasterizerState* state)	{ m_rasterizer.Set(Concrete<RasterizerState>(state)); }

void Gpu::ClearState()
{
	End_Rendering();
	for (StageState& s : m_stages) {
		s.shader.Reset();
		s.code = nullptr;
		for (unsigned i = 0; i < CB_SLOTS; ++i) {
			s.cb[i].Reset();
			s.cb_first[i] = s.cb_count[i] = 0;
		}
		for (unsigned i = 0; i < SRV_SLOTS; ++i) s.srv[i].Reset();
		for (unsigned i = 0; i < SAMPLER_SLOTS; ++i) s.sampler[i].Reset();
	}
	m_input_layout.Reset();
	m_topology = GPU_PRIMITIVE_TOPOLOGY_UNDEFINED;
	for (unsigned i = 0; i < VB_SLOTS; ++i) {
		m_vertex_buffers[i].Reset();
		m_vertex_strides[i] = m_vertex_offsets[i] = 0;
	}
	m_index_buffer.Reset();
	m_blend.Reset();
	for (FLOAT& f : m_blend_factor) f = 1.0f;
	m_depth_stencil.Reset();
	m_stencil_ref = 0;
	m_rasterizer.Reset();
	m_viewport_set = false;
	for (unsigned i = 0; i < RT_SLOTS; ++i) m_targets[i].Reset();
	m_depth.Reset();
	m_dynamic_dirty = true;
}

// ==================================================================================================
// Descriptors
// ==================================================================================================

VkDescriptorSet Gpu::Allocate_Set(unsigned stage)
{
	Frame& f = m_frames[m_frame];
	for (;;) {
		if (f.descriptor_pool >= f.descriptor_pools.size()) {
			VkDescriptorPoolSize sizes[3] = {
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DESCRIPTOR_SETS_PER_POOL * CB_SLOTS },
				{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, DESCRIPTOR_SETS_PER_POOL * SRV_SLOTS },
				{ VK_DESCRIPTOR_TYPE_SAMPLER, DESCRIPTOR_SETS_PER_POOL * SAMPLER_SLOTS } };
			VkDescriptorPoolCreateInfo create = {};
			create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			create.maxSets = DESCRIPTOR_SETS_PER_POOL;
			create.poolSizeCount = 3;
			create.pPoolSizes = sizes;
			VkDescriptorPool pool = VK_NULL_HANDLE;
			if (vkCreateDescriptorPool(m_device, &create, nullptr, &pool) != VK_SUCCESS) return VK_NULL_HANDLE;
			f.descriptor_pools.push_back(pool);
		}
		VkDescriptorSetAllocateInfo alloc = {};
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.descriptorPool = f.descriptor_pools[f.descriptor_pool];
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &m_set_layouts[stage];
		VkDescriptorSet set = VK_NULL_HANDLE;
		const VkResult result = vkAllocateDescriptorSets(m_device, &alloc, &set);
		if (result == VK_SUCCESS) return set;
		if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) return VK_NULL_HANDLE;
		++f.descriptor_pool;
	}
}

bool Gpu::Bind_Descriptors(unsigned stage)
{
	StageState& s = m_stages[stage];
	ShaderBase* code = s.code;
	const SpirvReflection& r = code->reflection;
	StageState::Written want;
	memset(&want, 0, sizeof(want));
	want.shader = code->id;
	const VkDeviceSize max_range = m_properties.limits.maxUniformBufferRange;
	for (unsigned i = 0; i < CB_SLOTS; ++i) {
		if (!(r.cb_mask & (1u << i))) continue;
		Buffer* b = s.cb[i].Get();
		if (!b) b = m_null_buffer;
		else Use_Buffer(this, b);
		const VkDeviceSize first = static_cast<VkDeviceSize>(s.cb_first[i]) * 16;
		const VkDeviceSize size = b->m_desc.ByteWidth > first ? b->m_desc.ByteWidth - first : 0;
		VkDeviceSize range = s.cb_count[i] ? std::min<VkDeviceSize>(static_cast<VkDeviceSize>(s.cb_count[i]) * 16, size) : size;
		want.cb_buffer[i] = b->m_current.buffer;
		want.cb_offset[i] = b->m_current.offset + first;
		want.cb_range[i] = std::min(range ? range : 16, max_range);
	}
	for (unsigned i = 0; i < SRV_SLOTS; ++i) {
		if (!(r.srv_mask & (1u << i))) continue;
		want.srv[i] = s.srv[i] ? s.srv[i]->view : m_null_view;
	}
	for (unsigned i = 0; i < SAMPLER_SLOTS; ++i) {
		if (!(r.sampler_mask & (1u << i))) continue;
		want.sampler[i] = s.sampler[i] ? s.sampler[i]->m_sampler : m_default_sampler;
	}
	if (s.written.set) {
		want.set = s.written.set;
		if (memcmp(&want, &s.written, sizeof(want)) == 0) return true;
	}

	VkDescriptorSet set = Allocate_Set(stage);
	if (!set) {
		Log_Once("descriptor set allocation failed");
		return false;
	}
	VkWriteDescriptorSet writes[CB_SLOTS + SRV_SLOTS + SAMPLER_SLOTS] = {};
	VkDescriptorBufferInfo buffers[CB_SLOTS] = {};
	VkDescriptorImageInfo images[SRV_SLOTS + SAMPLER_SLOTS] = {};
	uint32_t n = 0;
	for (unsigned i = 0; i < CB_SLOTS; ++i) {
		if (!(r.cb_mask & (1u << i))) continue;
		buffers[i].buffer = want.cb_buffer[i];
		buffers[i].offset = want.cb_offset[i];
		buffers[i].range = want.cb_range[i];
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].dstSet = set;
		writes[n].dstBinding = SPIRV_BINDING_CB + i;
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[n].pBufferInfo = &buffers[i];
		++n;
	}
	for (unsigned i = 0; i < SRV_SLOTS; ++i) {
		if (!(r.srv_mask & (1u << i))) continue;
		images[i].imageView = want.srv[i];
		images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].dstSet = set;
		writes[n].dstBinding = SPIRV_BINDING_SRV + i;
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		writes[n].pImageInfo = &images[i];
		++n;
	}
	for (unsigned i = 0; i < SAMPLER_SLOTS; ++i) {
		if (!(r.sampler_mask & (1u << i))) continue;
		images[SRV_SLOTS + i].sampler = want.sampler[i];
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].dstSet = set;
		writes[n].dstBinding = SPIRV_BINDING_SAMPLER + i;
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		writes[n].pImageInfo = &images[SRV_SLOTS + i];
		++n;
	}
	if (n) vkUpdateDescriptorSets(m_device, n, writes, 0, nullptr);
	vkCmdBindDescriptorSets(m_frames[m_frame].cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, stage, 1, &set, 0, nullptr);
	want.set = set;
	s.written = want;
	return true;
}

// ==================================================================================================
// Draws
// ==================================================================================================

bool Gpu::Prepare_Draw()
{
	if (m_lost || !m_stages[STAGE_VS].code) return false;
	if (m_viewport_set && (m_viewport.Width <= 0.0f || m_viewport.Height <= 0.0f)) return false;
	uint32_t topology_class = 0;
	const VkPrimitiveTopology topology = Vk_Topology(m_topology, &topology_class);
	const bool tessellation = topology_class > 100;
	VkPipeline pipeline = Find_Pipeline();
	if (!pipeline) {
		Log_Once("draws without a usable pipeline are skipped");
		return false;
	}
	VkCommandBuffer cmd = m_frames[m_frame].cmd;
	if (m_serialized) m_barrier_pending = true;

	// Layouts the draw needs: sampled images readable, targets writable.
	Texture* moves[STAGE_COUNT * SRV_SLOTS + RT_SLOTS + 1];
	VkImageLayout layouts[STAGE_COUNT * SRV_SLOTS + RT_SLOTS + 1];
	unsigned count = 0;
	for (unsigned s = 0; s < STAGE_COUNT; ++s) {
		const StageState& st = m_stages[s];
		if (!st.code || ((s == STAGE_HS || s == STAGE_DS) && !tessellation)) continue;
		for (unsigned i = 0; i < SRV_SLOTS; ++i) {
			if (!(st.code->reflection.srv_mask & (1u << i)) || !st.srv[i]) continue;
			Texture* t = st.srv[i]->texture;
			if (t->m_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
				moves[count] = t;
				layouts[count++] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
		}
	}
	for (unsigned i = 0; i < RT_SLOTS; ++i) {
		if (m_targets[i] && m_targets[i]->texture->m_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
			moves[count] = m_targets[i]->texture;
			layouts[count++] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
	}
	if (m_depth && m_depth->texture->m_layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		moves[count] = m_depth->texture;
		layouts[count++] = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	if (count || m_barrier_pending) {
		End_Rendering();
		if (m_barrier_pending) Barrier_All(cmd);
		m_barrier_pending = false;
		for (unsigned i = 0; i < count; ++i) Transition(cmd, moves[i], layouts[i]);
	}
	if (!m_rendering && !Begin_Rendering()) {
		Log_Once("draws without render target are skipped");
		return false;
	}

	if (pipeline != m_bound_pipeline) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		m_bound_pipeline = pipeline;
		// A pipeline without vertex buffers has a static stride state, which voids the strides set before.
		memset(m_bound_vb, 0, sizeof(m_bound_vb));
	}
	if (m_dynamic_dirty) {
		VkViewport vp;
		if (m_viewport_set) {
			// Flipped: y up in clip space like Direct3D, same pixels and winding.
			vp.x = m_viewport.TopLeftX;
			vp.y = m_viewport.TopLeftY + m_viewport.Height;
			vp.width = m_viewport.Width;
			vp.height = -m_viewport.Height;
			vp.minDepth = m_viewport.MinDepth;
			vp.maxDepth = m_viewport.MaxDepth;
		}
		else {
			vp.x = 0.0f;
			vp.y = static_cast<float>(m_render_extent.height);
			vp.width = static_cast<float>(m_render_extent.width);
			vp.height = -static_cast<float>(m_render_extent.height);
			vp.minDepth = 0.0f;
			vp.maxDepth = 1.0f;
		}
		vkCmdSetViewport(cmd, 0, 1, &vp);
		VkRect2D scissor = {};
		scissor.extent = m_render_extent;
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdSetBlendConstants(cmd, m_blend_factor);
		vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, m_stencil_ref);
		m_dynamic_dirty = false;
	}
	if (topology != m_bound_topology) {
		vkCmdSetPrimitiveTopology(cmd, topology);
		m_bound_topology = topology;
	}

	// Vertex buffers: every slot the input layout reads; a discard may have moved a dynamic buffer.
	unsigned slots = 0;
	if (m_input_layout) {
		for (const InputElement& e : m_input_layout->m_elements) slots |= 1u << e.slot;
	}
	for (unsigned i = 0; i < VB_SLOTS; ++i) {
		if (!(slots & (1u << i))) continue;
		Buffer* b = m_vertex_buffers[i].Get();
		VkBuffer buffer;
		VkDeviceSize offset, stride = m_vertex_strides[i];
		if (b) {
			Use_Buffer(this, b);
			buffer = b->m_current.buffer;
			offset = b->m_current.offset + m_vertex_offsets[i];
		}
		else {
			buffer = m_null_buffer->m_current.buffer;
			offset = 0;
			stride = 0;
		}
		if (buffer != m_bound_vb[i] || offset != m_bound_vb_offset[i] || stride != m_bound_vb_stride[i]) {
			vkCmdBindVertexBuffers2(cmd, i, 1, &buffer, &offset, nullptr, &stride);
			m_bound_vb[i] = buffer;
			m_bound_vb_offset[i] = offset;
			m_bound_vb_stride[i] = stride;
		}
	}

	for (unsigned s = 0; s < STAGE_COUNT; ++s) {
		if (!m_stages[s].code || ((s == STAGE_HS || s == STAGE_DS) && !tessellation)) continue;
		if (!Bind_Descriptors(s)) return false;
	}
	Mark_Used();
	m_draw_since_barrier = true;
	return true;
}

void Gpu::Mark_Used()
{
	for (StageState& s : m_stages) {
		if (!s.code) continue;
		for (unsigned i = 0; i < SRV_SLOTS; ++i) {
			if (s.srv[i]) Use(s.srv[i]->texture);
		}
	}
	for (unsigned i = 0; i < RT_SLOTS; ++i) {
		if (m_targets[i]) Use(m_targets[i]->texture);
	}
	if (m_depth) Use(m_depth->texture);
}

void Gpu::Draw(UINT vertices, UINT first)
{
	if (!Prepare_Draw()) return;
	vkCmdDraw(m_frames[m_frame].cmd, vertices, 1, first, 0);
}

void Gpu::DrawInstanced(UINT vertices, UINT instances, UINT first_vertex, UINT first_instance)
{
	if (!Prepare_Draw()) return;
	vkCmdDraw(m_frames[m_frame].cmd, vertices, instances, first_vertex, first_instance);
}

void Gpu::DrawIndexed(UINT indices, UINT first, INT base_vertex)
{
	DrawIndexedInstanced(indices, 1, first, base_vertex, 0);
}

void Gpu::DrawIndexedInstanced(UINT indices, UINT instances, UINT first_index, INT base_vertex, UINT first_instance)
{
	if (!m_index_buffer || !Prepare_Draw()) return;
	VkCommandBuffer cmd = m_frames[m_frame].cmd;
	Buffer* b = m_index_buffer.Get();
	Use_Buffer(this, b);
	const VkDeviceSize offset = b->m_current.offset + m_index_offset;
	if (b->m_current.buffer != m_bound_ib || offset != m_bound_ib_offset) {
		vkCmdBindIndexBuffer(cmd, b->m_current.buffer, offset, m_index_format == GPU_FORMAT_R32_UINT ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
		m_bound_ib = b->m_current.buffer;
		m_bound_ib_offset = offset;
	}
	vkCmdDrawIndexed(cmd, indices, instances, first_index, base_vertex, first_instance);
}

// ==================================================================================================
// Clears
// ==================================================================================================

void Gpu::Clear_Image_Rects(ViewBase* view, const FLOAT rgba[4], const GPU_RECT* rects, UINT count)
{
	// Depth views carry the clear in rgba[0] (depth) and rgba[1] (stencil) and the aspects in rgba[2].
	if (!view) return;
	VkCommandBuffer cmd = m_frames[m_frame].cmd;
	const bool depth = (view->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
	int attachment = -1;
	for (unsigned i = 0; i < RT_SLOTS && !depth; ++i) {
		if (m_targets[i].Get() == view) attachment = static_cast<int>(i);
	}
	const bool current = depth ? (m_depth.Get() == view) : attachment >= 0;
	const VkImageLayout wanted = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	const unsigned width = std::max(1u, view->texture->m_desc.Width >> view->mip);
	const unsigned height = std::max(1u, view->texture->m_desc.Height >> view->mip);

	VkExtent2D area;
	if (current) {
		bool moves = view->texture->m_layout != wanted || m_barrier_pending;
		for (unsigned i = 0; i < RT_SLOTS; ++i) {
			if (m_targets[i] && m_targets[i]->texture->m_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) moves = true;
		}
		if (m_depth && m_depth->texture->m_layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) moves = true;
		if (moves) {
			End_Rendering();
			if (m_barrier_pending) Barrier_All(cmd);
			m_barrier_pending = false;
			for (unsigned i = 0; i < RT_SLOTS; ++i) {
				if (m_targets[i]) Transition(cmd, m_targets[i]->texture, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			}
			if (m_depth) Transition(cmd, m_depth->texture, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		}
		if (!m_rendering && !Begin_Rendering()) return;
		area = m_render_extent;
	}
	else {
		// Not a current target: a rendering scope of its own for the clear.
		End_Rendering();
		if (m_barrier_pending || m_draw_since_barrier) Barrier_All(cmd);
		m_barrier_pending = false;
		Transition(cmd, view->texture, wanted);
		VkRenderingAttachmentInfo a = {};
		a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		a.imageView = view->view;
		a.imageLayout = wanted;
		a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		VkRenderingInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		info.renderArea.extent.width = width;
		info.renderArea.extent.height = height;
		info.layerCount = 1;
		if (depth) {
			info.pDepthAttachment = &a;
			if (view->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) info.pStencilAttachment = &a;
		}
		else {
			info.colorAttachmentCount = 1;
			info.pColorAttachments = &a;
		}
		vkCmdBeginRendering(cmd, &info);
		attachment = 0;
		area = info.renderArea.extent;
	}

	VkClearAttachment clear = {};
	if (depth) {
		clear.aspectMask = static_cast<VkImageAspectFlags>(rgba[2]) & view->aspect;
		clear.clearValue.depthStencil.depth = rgba[0];
		clear.clearValue.depthStencil.stencil = static_cast<uint32_t>(rgba[1]);
	}
	else {
		clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		clear.colorAttachment = static_cast<uint32_t>(attachment);
		for (unsigned i = 0; i < 4; ++i) clear.clearValue.color.float32[i] = rgba[i];
	}
	std::vector<VkClearRect> list;
	const UINT n = rects ? count : 1;
	for (UINT i = 0; i < n; ++i) {
		LONG l = 0, t = 0, r = static_cast<LONG>(width), b = static_cast<LONG>(height);
		if (rects) {
			l = std::max<LONG>(rects[i].left, 0);
			t = std::max<LONG>(rects[i].top, 0);
			r = std::min<LONG>(rects[i].right, static_cast<LONG>(width));
			b = std::min<LONG>(rects[i].bottom, static_cast<LONG>(height));
		}
		r = std::min<LONG>(r, static_cast<LONG>(area.width));
		b = std::min<LONG>(b, static_cast<LONG>(area.height));
		if (r <= l || b <= t) continue;
		VkClearRect rect = {};
		rect.rect.offset.x = l;
		rect.rect.offset.y = t;
		rect.rect.extent.width = static_cast<uint32_t>(r - l);
		rect.rect.extent.height = static_cast<uint32_t>(b - t);
		rect.layerCount = 1;
		list.push_back(rect);
	}
	if (clear.aspectMask && !list.empty())
		vkCmdClearAttachments(cmd, 1, &clear, static_cast<uint32_t>(list.size()), list.data());
	if (!current) vkCmdEndRendering(cmd);
	Use(view->texture);
	m_draw_since_barrier = true;
}

void Gpu::ClearRenderTargetView(GpuRenderTargetView* view, const FLOAT rgba[4])
{
	if (view) Clear_Image_Rects(static_cast<ViewBase*>(view->Native()), rgba, nullptr, 0);
}

void Gpu::ClearDepthStencilView(GpuDepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil)
{
	if (!view) return;
	float aspects = 0.0f;
	if (flags & GPU_CLEAR_DEPTH) aspects += static_cast<float>(VK_IMAGE_ASPECT_DEPTH_BIT);
	if (flags & GPU_CLEAR_STENCIL) aspects += static_cast<float>(VK_IMAGE_ASPECT_STENCIL_BIT);
	const FLOAT values[4] = { depth, static_cast<FLOAT>(stencil), aspects, 0.0f };
	Clear_Image_Rects(static_cast<ViewBase*>(view->Native()), values, nullptr, 0);
}

void Gpu::ClearView(GpuView* view, const FLOAT rgba[4], const GPU_RECT* rects, UINT count)
{
	if (!view) return;
	ViewBase* v = static_cast<ViewBase*>(view->Native());
	if (v->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) {
		Log_Once("ClearView on a depth view is not supported");
		return;
	}
	Clear_Image_Rects(v, rgba, rects, count);
}

// ==================================================================================================
// Queries
// ==================================================================================================

void Gpu::Begin(GpuQuery* query)
{
	if (!query) return;
	Query* q = static_cast<Query*>(query);
	if (q->m_desc.Query != GPU_QUERY_PIPELINE_STATISTICS || q->m_active || m_open_statistics) return;
	if (!q->m_pool) {
		Log_Once("pipeline statistics queries are not supported by the device, they never complete");
		return;
	}
	// Begin and end outside rendering scopes, so the query may span several of them.
	End_Rendering();
	VkCommandBuffer cmd = m_frames[m_frame].cmd;
	vkCmdResetQueryPool(cmd, q->m_pool, 0, 1);
	vkCmdBeginQuery(cmd, q->m_pool, 0, 0);
	q->m_active = true;
	q->m_written = false;
	m_open_statistics = q;
}

void Gpu::End(GpuQuery* query)
{
	if (!query) return;
	Query* q = static_cast<Query*>(query);
	if (q->m_desc.Query == GPU_QUERY_TIMESTAMP) {
		if (!q->m_pool || !m_host_query_reset) return;
		// The query is reset on the host, which needs the previous result to be done.
		if (q->m_written && !Completed(q->m_serial)) return;
		vkResetQueryPool(m_device, q->m_pool, 0, 1);
		vkCmdWriteTimestamp2(m_frames[m_frame].cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, q->m_pool, 0);
	}
	else if (q->m_desc.Query == GPU_QUERY_PIPELINE_STATISTICS) {
		if (!q->m_active) return;
		End_Rendering();
		vkCmdEndQuery(m_frames[m_frame].cmd, q->m_pool, 0);
		q->m_active = false;
		if (m_open_statistics == q) m_open_statistics = nullptr;
	}
	q->m_serial = m_serial;
	q->m_written = true;
}

HRESULT Gpu::GetData(GpuQuery* query, void* data, UINT size, UINT)
{
	if (!query) return E_INVALIDARG;
	Query* q = static_cast<Query*>(query);
	if (!q->m_written || !Completed(q->m_serial)) return S_FALSE;
	switch (q->m_desc.Query) {
	case GPU_QUERY_TIMESTAMP_DISJOINT:
		if (data && size >= sizeof(GPU_QUERY_DATA_TIMESTAMP_DISJOINT)) {
			GPU_QUERY_DATA_TIMESTAMP_DISJOINT* d = static_cast<GPU_QUERY_DATA_TIMESTAMP_DISJOINT*>(data);
			const double period = m_properties.limits.timestampPeriod > 0.0f ? m_properties.limits.timestampPeriod : 1.0;
			d->Frequency = static_cast<UINT64>(1.0e9 / period);
			d->Disjoint = FALSE;
		}
		return S_OK;
	case GPU_QUERY_TIMESTAMP: {
		if (!q->m_pool) return S_FALSE;
		uint64_t ticks = 0;
		if (vkGetQueryPoolResults(m_device, q->m_pool, 0, 1, sizeof(ticks), &ticks, sizeof(ticks), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
			return S_FALSE;
		if (data && size >= sizeof(UINT64)) *static_cast<UINT64*>(data) = ticks;
		return S_OK;
	}
	case GPU_QUERY_PIPELINE_STATISTICS: {
		if (!q->m_pool) return S_FALSE;
		uint64_t counters[16] = {};
		if (vkGetQueryPoolResults(m_device, q->m_pool, 0, 1, sizeof(counters), counters, sizeof(counters), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
			return S_FALSE;
		if (data && size >= sizeof(GPU_QUERY_DATA_PIPELINE_STATISTICS)) {
			// Vulkan returns the requested counters packed in bit order; D3D11 has a member for every bit.
			UINT64* out = static_cast<UINT64*>(data);
			memset(out, 0, sizeof(GPU_QUERY_DATA_PIPELINE_STATISTICS));
			unsigned next = 0;
			for (unsigned bit = 0; bit < 11; ++bit) {
				if (PIPELINE_STATISTICS & (1u << bit)) out[bit] = counters[next++];
			}
		}
		return S_OK;
	}
	default:
		return S_FALSE;
	}
}

}  // namespace vkdev
