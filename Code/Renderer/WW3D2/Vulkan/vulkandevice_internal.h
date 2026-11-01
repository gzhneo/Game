/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// vulkandevice_internal.h ------------------------------------------------------------------------
//
// The Vulkan device layer: the device layer objects of gpudevice.h on Vulkan 1.3. Only the
// vulkandevice*.cpp files include this header.
//
// How the Direct3D 11 semantics the render core relies on are met:
//
//   Recording     One command buffer per frame in flight (two). Uploads of resources the frame has
//                 not used yet go into a second command buffer submitted before it, so loading
//                 does not split the frame's rendering.
//   Rendering     Dynamic rendering. A rendering scope starts lazily at the first draw or clear and
//                 ends when the targets change, a layout transition or a transfer is needed.
//   Layouts       Each texture remembers its layout; transitions happen before the scope starts.
//                 Barriers are conservative (all commands to all commands).
//   Discard       A dynamic buffer moves to a new slice on MAP_WRITE_DISCARD when the GPU may still
//                 read the current one; slices return to the buffer when their frame is done.
//   Bindings      One descriptor set per shader stage (set = stage). Per draw the sets whose bound
//                 objects changed are written anew from a pool of the frame. Empty slots read a
//                 zero texture, zero constants and the default sampler like Direct3D 11.
//   Pipelines     Built from the bound objects at draw time and cached by their ids. Viewport,
//                 scissor, blend factor, stencil reference, topology and vertex strides are dynamic.
//   Shaders       SPIR-V from DXC (vulkandxc.h); the inputs of each stage are linked to the outputs
//                 of the previous stage by semantic (vulkanspirv.h).
//   Back buffer   An image of the device; Present copies it into the swap chain image.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef VULKANDEVICE_INTERNAL_H
#define VULKANDEVICE_INTERNAL_H

#include "vulkanloader.h"
#include "WW3D2/Common/gpudevice.h"
#include "vulkanspirv.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace vkdev {

class Gpu;

enum
{
	FRAMES_IN_FLIGHT	= 2,
	STAGE_VS			= 0,
	STAGE_PS			= 1,
	STAGE_HS			= 2,
	STAGE_DS			= 3,
	STAGE_COUNT			= 4,
	CB_SLOTS			= 14,		///< D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
	SRV_SLOTS			= 16,
	SAMPLER_SLOTS		= 16,
	VB_SLOTS			= 16,
	RT_SLOTS			= 8,
	MAX_MIPS			= 16,
};

// ------------------------------------------------------------------------------------------------
// Formats
// ------------------------------------------------------------------------------------------------

struct FormatInfo
{
	VkFormat			format;			///< VK_FORMAT_UNDEFINED: not supported
	VkComponentSwizzle	swizzle[4];		///< view swizzle (A8 lives in an R8 image, X8 reads alpha 1)
	VkImageAspectFlags	aspect;			///< color, or depth with stencil if the format has one
	unsigned			block_bytes;	///< bytes per texel or per 4x4 block
	unsigned			block_size;		///< 1, or 4 for block compressed formats
};

/// Vulkan form of a device layer format; typeless depth formats map to the depth format.
const FormatInfo&	Format_Info(GPU_FORMAT format);
/// Bytes of one row and number of rows of a subresource width x height in the format.
void				Format_Pitch(const FormatInfo& info, unsigned width, unsigned height, unsigned* row_bytes, unsigned* rows);
/// Records why device creation failed (Vulkan_Device_Error) and logs it.
void				Fail(const char* text, int code = 0);

// ------------------------------------------------------------------------------------------------
// Objects
// ------------------------------------------------------------------------------------------------

/// Reference counting and the device link shared by every object.
template <class Interface>
class Object : public Interface
{
public:
	ULONG	AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&m_refs)); }
	ULONG	Release() override
	{
		const LONG refs = InterlockedDecrement(&m_refs);
		if (refs == 0) delete this;
		return static_cast<ULONG>(refs);
	}
	void*	Native() override { return this; }
	uint64_t Id() const { return m_id; }
	Gpu*	Device() const { return m_gpu; }

protected:
	explicit Object(Gpu* gpu);
	~Object() override;

	Gpu*			m_gpu;
	volatile LONG	m_refs;
	uint64_t		m_id;
};

/// A piece of a VkBuffer; mapped is set for host visible memory.
struct Slice
{
	VkBuffer		buffer;
	VmaAllocation	allocation;
	VkDeviceSize	offset;
	uint8_t*		mapped;
};

enum ResourceKind { RESOURCE_BUFFER, RESOURCE_TEXTURE };

/// Common part of buffers and textures (what GpuResource::Native returns).
struct ResourceBase
{
	ResourceKind	kind;
	uint64_t		last_use;		///< serial of the last submission that references the resource
	uint64_t		upload_batch = 0;	///< upload command buffer that last wrote the resource (Transfer_Command)
};

class Buffer final : public Object<GpuBuffer>, public ResourceBase
{
public:
	Buffer(Gpu* gpu, const GPU_BUFFER_DESC& desc);
	~Buffer() override;
	void	GetDesc(GPU_BUFFER_DESC* desc) override { *desc = m_desc; }
	void*	Native() override { return static_cast<ResourceBase*>(this); }

	bool	Create(const GPU_SUBRESOURCE_DATA* initial);
	/// A fresh slice for MAP_WRITE_DISCARD (the current one if the GPU cannot be using it).
	bool	Discard();
	bool	Is_Dynamic() const { return m_desc.Usage == GPU_USAGE_DYNAMIC; }
	bool	Is_Staging() const { return m_desc.Usage == GPU_USAGE_STAGING; }

	GPU_BUFFER_DESC		m_desc;
	Slice				m_current;
	bool				m_used_since_discard;
	std::vector<Slice>	m_free;
	struct Retired { uint64_t serial; Slice slice; };
	std::vector<Retired> m_retired;

private:
	bool	New_Slice(Slice& slice);
};

struct SubresourceLayout
{
	VkDeviceSize	offset;
	unsigned		row_pitch;
	unsigned		depth_pitch;
	unsigned		width;
	unsigned		height;
};

class Texture final : public Object<GpuTexture2D>, public ResourceBase
{
public:
	Texture(Gpu* gpu, const GPU_TEXTURE2D_DESC& desc);
	~Texture() override;
	void	GetDesc(GPU_TEXTURE2D_DESC* desc) override { *desc = m_desc; }
	void*	Native() override { return static_cast<ResourceBase*>(this); }

	bool	Create(const GPU_SUBRESOURCE_DATA* initial);
	/// Records the clear of every subresource to zero (image in TRANSFER_DST_OPTIMAL).
	bool	Zero(VkCommandBuffer cmd);
	bool	Is_Staging() const { return m_desc.Usage == GPU_USAGE_STAGING; }
	unsigned Subresource_Count() const { return m_desc.MipLevels * m_desc.ArraySize; }
	VkImageSubresourceRange Full_Range() const;

	GPU_TEXTURE2D_DESC		m_desc;
	const FormatInfo*		m_format;
	VkImage					m_image;
	VmaAllocation			m_allocation;
	VkImageLayout			m_layout;
	VkSampleCountFlagBits	m_samples;
	Slice					m_staging;			///< staging textures: all subresources in one host buffer
	std::vector<SubresourceLayout> m_subresources;

	/// MAP_WRITE_DISCARD of a dynamic texture: the data waits in a buffer of its own until Unmap. A subresource
	/// mapped again and again keeps that buffer and reuses it once the last copy from it is done.
	struct PendingMap { bool active; Slice slice; VkDeviceSize size; unsigned row_pitch; unsigned maps; uint64_t serial; };
	PendingMap				m_pending[MAX_MIPS];
};

/// Common part of the three view kinds (what GpuView::Native returns).
struct ViewBase
{
	Texture*			texture;
	VkImageView			view;
	VkFormat			format;
	unsigned			mip;
	VkImageAspectFlags	aspect;
};

template <class Interface>
class View final : public Object<Interface>, public ViewBase
{
public:
	View(Gpu* gpu, Texture* texture);
	~View() override;
	void*	Native() override { return static_cast<ViewBase*>(this); }
};

typedef View<GpuShaderResourceView>	ShaderResourceView;
typedef View<GpuRenderTargetView>	RenderTargetView;
typedef View<GpuDepthStencilView>	DepthStencilView;

/// Common part of the four shader kinds.
struct ShaderBase
{
	unsigned				stage;
	uint64_t				id;
	std::vector<uint32_t>	words;
	SpirvReflection			reflection;
	VkShaderModule			module;			///< inputs as DXC numbered them
	struct Linked { uint64_t previous; VkShaderModule module; };
	std::vector<Linked>		linked;			///< inputs moved to the outputs of a previous stage
};

template <class Interface>
class Shader final : public Object<Interface>, public ShaderBase
{
public:
	Shader(Gpu* gpu, unsigned stage);
	~Shader() override;
	void*	Native() override { return static_cast<ShaderBase*>(this); }
};

typedef Shader<GpuVertexShader>	VertexShader;
typedef Shader<GpuPixelShader>	PixelShader;
typedef Shader<GpuHullShader>	HullShader;
typedef Shader<GpuDomainShader>	DomainShader;

struct InputElement
{
	char		semantic[32];		///< upper case, without index
	unsigned	index;
	VkFormat	format;
	unsigned	slot;
	unsigned	offset;
	bool		per_instance;
};

class InputLayout final : public Object<GpuInputLayout>
{
public:
	explicit InputLayout(Gpu* gpu) : Object<GpuInputLayout>(gpu) {}
	~InputLayout() override;
	std::vector<InputElement>	m_elements;
};

template <class Interface, class Desc>
class StateObject final : public Object<Interface>
{
public:
	StateObject(Gpu* gpu, const Desc& desc) : Object<Interface>(gpu), m_desc(desc) {}
	~StateObject() override;
	Desc	m_desc;
};

typedef StateObject<GpuBlendState, GPU_BLEND_DESC>					BlendState;
typedef StateObject<GpuDepthStencilState, GPU_DEPTH_STENCIL_DESC>	DepthStencilState;
typedef StateObject<GpuRasterizerState, GPU_RASTERIZER_DESC>		RasterizerState;

class SamplerState final : public Object<GpuSamplerState>
{
public:
	SamplerState(Gpu* gpu, const GPU_SAMPLER_DESC& desc) : Object<GpuSamplerState>(gpu), m_desc(desc), m_sampler(VK_NULL_HANDLE) {}
	~SamplerState() override;
	GPU_SAMPLER_DESC	m_desc;
	VkSampler			m_sampler;
};

/// Pipeline statistics a query collects: the D3D11 counters without geometry and compute shaders, whose
/// counters stay 0. The Vulkan bits have the order of the D3D11 structure members.
const VkQueryPipelineStatisticFlags PIPELINE_STATISTICS =
	VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT | VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
	VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
	VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
	VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT |
	VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT;

class Query final : public Object<GpuQuery>
{
public:
	Query(Gpu* gpu, const GPU_QUERY_DESC& desc) : Object<GpuQuery>(gpu), m_desc(desc), m_pool(VK_NULL_HANDLE), m_serial(0), m_written(false), m_active(false) {}
	~Query() override;
	GPU_QUERY_DESC	m_desc;
	VkQueryPool		m_pool;			///< timestamps: one query
	uint64_t		m_serial;		///< submission of the last End
	bool			m_written;
	bool			m_active;		///< pipeline statistics: between Begin and End
};

// ------------------------------------------------------------------------------------------------
// The device and its immediate context
// ------------------------------------------------------------------------------------------------

/// A reference held by the context state (Direct3D 11 keeps bound objects alive).
template <class T>
class Ref
{
public:
	Ref() : m_p(nullptr) {}
	~Ref() { Reset(); }
	Ref(const Ref&) = delete;
	Ref& operator=(const Ref&) = delete;
	void Set(T* p)
	{
		if (p == m_p) return;
		if (p) p->AddRef();
		if (m_p) m_p->Release();
		m_p = p;
	}
	void Reset() { Set(nullptr); }
	T* Get() const { return m_p; }
	T* operator->() const { return m_p; }
	explicit operator bool() const { return m_p != nullptr; }
private:
	T* m_p;
};

struct StageState
{
	Ref<GpuObject>			shader;			///< VertexShader, PixelShader, ...
	ShaderBase*				code;			///< the shader's ShaderBase, nullptr if none
	Ref<Buffer>				cb[CB_SLOTS];
	unsigned				cb_first[CB_SLOTS];	///< in 16 byte constants
	unsigned				cb_count[CB_SLOTS];	///< 0 = whole buffer
	Ref<ShaderResourceView>	srv[SRV_SLOTS];
	Ref<SamplerState>		sampler[SAMPLER_SLOTS];

	/// What the stage's descriptor set holds, to see whether it needs writing.
	struct Written
	{
		VkDescriptorSet		set;
		uint64_t			shader;
		VkBuffer			cb_buffer[CB_SLOTS];
		VkDeviceSize		cb_offset[CB_SLOTS];
		VkDeviceSize		cb_range[CB_SLOTS];
		VkImageView			srv[SRV_SLOTS];
		VkSampler			sampler[SAMPLER_SLOTS];
	};
	Written					written;
};

struct UploadChunk
{
	Slice			slice;
	VkDeviceSize	size;
	VkDeviceSize	used;
};

struct Garbage
{
	VkObjectType	type;
	uint64_t		handle;
	VmaAllocation	allocation;
};

struct Frame
{
	VkCommandPool				pool;
	VkCommandBuffer				cmd;			///< the frame's rendering
	VkCommandBuffer				upload;			///< uploads before the frame (resources not used yet)
	bool						upload_open;
	VkFence						fence;
	VkSemaphore					acquired;
	uint64_t					serial;			///< submission the frame was last submitted as
	std::vector<VkDescriptorPool> descriptor_pools;
	unsigned					descriptor_pool;	///< the pool sets are allocated from
	std::vector<UploadChunk>	upload_chunks;
	std::vector<Garbage>		garbage;
};

struct PipelineKey
{
	uint64_t	shader[STAGE_COUNT];
	uint64_t	input_layout;
	uint64_t	blend;
	uint64_t	depth_stencil;
	uint64_t	rasterizer;
	uint32_t	topology_class;		///< VkPrimitiveTopology of the class, or 100 + control points for patches
	uint32_t	color_formats[RT_SLOTS];
	uint32_t	color_count;
	uint32_t	depth_format;
	uint32_t	samples;
	uint32_t	vertex_strides_used;	///< bit per slot with a vertex binding
	uint32_t	per_instance;			///< bit per slot stepping per instance

	bool operator==(const PipelineKey& o) const;
};

struct PipelineKeyHash
{
	size_t operator()(const PipelineKey& k) const;
};

class Gpu final : public GpuDevice, public GpuContext
{
public:
	Gpu();
	~Gpu() override;

	/// Device, queue, allocator, frames and the swap chain for the window.
	bool	Create(VkInstance instance, VkPhysicalDevice physical, HWND hwnd, unsigned width, unsigned height, bool debug);
	/// Waits for the GPU and destroys everything; objects still alive keep a dead device pointer
	/// (their Release then only frees memory).
	void	Destroy();

	// ---- GpuDevice ------------------------------------------------------------------------------
	HRESULT	CreateBuffer(const GPU_BUFFER_DESC* desc, const GPU_SUBRESOURCE_DATA* initial, GpuBuffer** out) override;
	HRESULT	CreateTexture2D(const GPU_TEXTURE2D_DESC* desc, const GPU_SUBRESOURCE_DATA* initial, GpuTexture2D** out) override;
	HRESULT	CreateShaderResourceView(GpuResource* resource, const GPU_SHADER_RESOURCE_VIEW_DESC* desc, GpuShaderResourceView** out) override;
	HRESULT	CreateRenderTargetView(GpuResource* resource, const GPU_RENDER_TARGET_VIEW_DESC* desc, GpuRenderTargetView** out) override;
	HRESULT	CreateDepthStencilView(GpuResource* resource, const GPU_DEPTH_STENCIL_VIEW_DESC* desc, GpuDepthStencilView** out) override;
	HRESULT	CreateVertexShader(const void* code, SIZE_T size, void* linkage, GpuVertexShader** out) override;
	HRESULT	CreatePixelShader(const void* code, SIZE_T size, void* linkage, GpuPixelShader** out) override;
	HRESULT	CreateHullShader(const void* code, SIZE_T size, void* linkage, GpuHullShader** out) override;
	HRESULT	CreateDomainShader(const void* code, SIZE_T size, void* linkage, GpuDomainShader** out) override;
	HRESULT	CreateInputLayout(const GPU_INPUT_ELEMENT_DESC* elements, UINT count, const void* vs_code, SIZE_T vs_size, GpuInputLayout** out) override;
	HRESULT	CreateBlendState(const GPU_BLEND_DESC* desc, GpuBlendState** out) override;
	HRESULT	CreateDepthStencilState(const GPU_DEPTH_STENCIL_DESC* desc, GpuDepthStencilState** out) override;
	HRESULT	CreateRasterizerState(const GPU_RASTERIZER_DESC* desc, GpuRasterizerState** out) override;
	HRESULT	CreateSamplerState(const GPU_SAMPLER_DESC* desc, GpuSamplerState** out) override;
	HRESULT	CreateQuery(const GPU_QUERY_DESC* desc, GpuQuery** out) override;
	HRESULT	CheckMultisampleQualityLevels(GPU_FORMAT format, UINT samples, UINT* levels) override;
	HRESULT	CompileShader(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
						  GpuShaderInclude* include, const char* entry, const char* profile, UINT flags,
						  GpuBlob** code, GpuBlob** errors) override;
	HRESULT	PreprocessShader(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
							 GpuShaderInclude* include, GpuBlob** text) override;
	const char*	ShaderFormat() override { return "spirv"; }
	UINT	ShaderCompilerVersion() override;
	void	Get_Device_Statistics(char* text, unsigned size) override;

	// ---- GpuContext -----------------------------------------------------------------------------
	void	IASetInputLayout(GpuInputLayout* layout) override;
	void	IASetPrimitiveTopology(GPU_PRIMITIVE_TOPOLOGY topology) override;
	void	IASetVertexBuffers(UINT start, UINT count, GpuBuffer* const* buffers, const UINT* strides, const UINT* offsets) override;
	void	IASetIndexBuffer(GpuBuffer* buffer, GPU_FORMAT format, UINT offset) override;
	void	VSSetShader(GpuVertexShader* shader, void* const* instances, UINT count) override;
	void	VSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) override;
	void	VSSetConstantBuffers1(UINT start, UINT count, GpuBuffer* const* buffers, const UINT* first, const UINT* num) override;
	void	VSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) override;
	void	VSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) override;
	void	HSSetShader(GpuHullShader* shader, void* const* instances, UINT count) override;
	void	HSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) override;
	void	HSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) override;
	void	HSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) override;
	void	DSSetShader(GpuDomainShader* shader, void* const* instances, UINT count) override;
	void	DSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) override;
	void	DSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) override;
	void	DSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) override;
	void	PSSetShader(GpuPixelShader* shader, void* const* instances, UINT count) override;
	void	PSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) override;
	void	PSSetConstantBuffers1(UINT start, UINT count, GpuBuffer* const* buffers, const UINT* first, const UINT* num) override;
	void	PSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) override;
	void	PSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) override;
	void	OMSetRenderTargets(UINT count, GpuRenderTargetView* const* targets, GpuDepthStencilView* depth) override;
	void	OMSetBlendState(GpuBlendState* state, const FLOAT factor[4], UINT sample_mask) override;
	void	OMSetDepthStencilState(GpuDepthStencilState* state, UINT stencil_ref) override;
	void	RSSetViewports(UINT count, const GPU_VIEWPORT* viewports) override;
	void	RSSetState(GpuRasterizerState* state) override;
	void	Draw(UINT vertices, UINT first) override;
	void	DrawIndexed(UINT indices, UINT first, INT base_vertex) override;
	void	DrawInstanced(UINT vertices, UINT instances, UINT first_vertex, UINT first_instance) override;
	void	DrawIndexedInstanced(UINT indices, UINT instances, UINT first_index, INT base_vertex, UINT first_instance) override;
	HRESULT	Map(GpuResource* resource, UINT subresource, GPU_MAP type, UINT flags, GPU_MAPPED_SUBRESOURCE* mapped) override;
	void	Unmap(GpuResource* resource, UINT subresource) override;
	void	UpdateSubresource(GpuResource* resource, UINT subresource, const GPU_BOX* box, const void* data, UINT row_pitch, UINT depth_pitch) override;
	void	CopyResource(GpuResource* dest, GpuResource* source) override;
	void	CopySubresourceRegion(GpuResource* dest, UINT dest_sub, UINT x, UINT y, UINT z,
								  GpuResource* source, UINT source_sub, const GPU_BOX* box) override;
	void	ResolveSubresource(GpuResource* dest, UINT dest_sub, GpuResource* source, UINT source_sub, GPU_FORMAT format) override;
	void	ClearRenderTargetView(GpuRenderTargetView* view, const FLOAT rgba[4]) override;
	void	ClearDepthStencilView(GpuDepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil) override;
	void	ClearView(GpuView* view, const FLOAT rgba[4], const GPU_RECT* rects, UINT count) override;
	void	Begin(GpuQuery* query) override;
	void	End(GpuQuery* query) override;
	HRESULT	GetData(GpuQuery* query, void* data, UINT size, UINT flags) override;
	void	ClearState() override;
	void	Flush() override {}
	bool	Supports_Clear_View() override { return true; }
	bool	Supports_Constant_Buffer_Offsets() override { return true; }

	// ---- Swap chain (for the output) ------------------------------------------------------------
	GpuTexture2D*	Back_Buffer();
	HRESULT			Resize(unsigned width, unsigned height);
	HRESULT			Present(int swap_interval);
	bool			Allows_Tearing() const { return m_immediate_supported; }
	bool			Lost() const { return m_lost; }
	unsigned __int64 Free_Video_Memory();

	// ---- For the objects ------------------------------------------------------------------------
	uint64_t	Next_Id() { return ++m_next_id; }
	void		Object_Created() { ++m_live_objects; }
	long		Live_Objects() const { return m_live_objects; }
	void		Object_Destroyed();
	bool		Alive() const { return m_device != VK_NULL_HANDLE; }
	/// Destroys the handle once the GPU is done with the current submission.
	void		Retire(VkObjectType type, uint64_t handle, VmaAllocation allocation = nullptr);
	/// Drops the cached pipelines built with the shader or state object `id` (it is being destroyed).
	void		Forget_Pipelines(uint64_t id);
	bool		Allocate_Buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host, bool readback, Slice& slice);
	/// Room in the frame's upload ring.
	bool		Upload_Space(VkDeviceSize size, VkDeviceSize alignment, Slice& slice);
	/// Command buffer for a transfer into `target`: the upload buffer if the frame has not used the
	/// resource yet, else the frame's buffer with rendering ended and a barrier before the transfer.
	VkCommandBuffer	Transfer_Command(ResourceBase* target);
	/// Moves the texture's whole image to the layout (barrier in cmd).
	void		Transition(VkCommandBuffer cmd, Texture* texture, VkImageLayout layout);
	/// Submits everything recorded and waits until the GPU is idle (reading back).
	void		Flush_And_Wait();
	bool		Completed(uint64_t serial) const { return serial <= m_completed; }
	uint64_t	Serial() const { return m_serial; }
	void		Use(ResourceBase* resource) { resource->last_use = m_serial; }
	void		Log_Once(const char* what);
	VkDevice	Vk() const { return m_device; }
	VmaAllocator Allocator() const { return m_allocator; }
	bool		Create_Shader_Module(const std::vector<uint32_t>& words, VkShaderModule* module);

	VkPhysicalDeviceProperties			m_properties;
	VkFormat							m_depth_format;		///< D24S8 or D32S8 (for typeless D24 families)

private:
	// vulkandevice.cpp
	bool	Create_Swap_Chain(unsigned width, unsigned height);
	void	Destroy_Swap_Chain();
	bool	Create_Back_Buffer(unsigned width, unsigned height);
	void	Begin_Frame();
	void	Submit(bool present_wait);
	void	Recycle(Frame& frame);
	void	Destroy_Garbage(std::vector<Garbage>& list);

	// vulkandevice_context.cpp
	bool	Prepare_Draw();
	void	End_Rendering();
	bool	Begin_Rendering();
	void	Barrier_All(VkCommandBuffer cmd);
	void	Mark_Used();
	void	Reset_Command_State();
	void	Set_Shader(unsigned stage, GpuObject* shader);
	void	Set_Constant_Buffers(unsigned stage, UINT start, UINT count, GpuBuffer* const* buffers, const UINT* first, const UINT* num);
	void	Set_Shader_Resources(unsigned stage, UINT start, UINT count, GpuShaderResourceView* const* views);
	void	Set_Samplers(unsigned stage, UINT start, UINT count, GpuSamplerState* const* samplers);
	bool	Bind_Descriptors(unsigned stage);
	VkDescriptorSet	Allocate_Set(unsigned stage);
	bool	Is_Target(const Texture* texture) const;
	void	Copy_Texture_Region(Texture* dest, unsigned dest_sub, unsigned x, unsigned y, Texture* source, unsigned source_sub, const GPU_BOX* box);
	void	Clear_Image_Rects(ViewBase* view, const FLOAT rgba[4], const GPU_RECT* rects, UINT count);

	// vulkandevice_pipeline.cpp
	bool	Create_Layouts();
	bool	Pipeline_Cache_Path(char* path, size_t size) const;
	void	Destroy_Layouts();
	VkPipeline	Find_Pipeline();
	VkPipeline	Create_Pipeline(const PipelineKey& key);
	VkShaderModule	Stage_Module(ShaderBase* shader, ShaderBase* previous);
	HRESULT	Create_Shader(unsigned stage, const void* code, SIZE_T size, GpuObject** out);
	VkSampler	Create_Sampler(const GPU_SAMPLER_DESC& desc);

public:
	// Objects shared by the files of the device.
	VkInstance							m_instance;
	VkPhysicalDevice					m_physical;
	VkDevice							m_device;
	VmaAllocator						m_allocator;
	VkQueue								m_queue;
	uint32_t							m_queue_family;
	bool								m_debug;
	bool								m_serialized;	///< -vulkanSeriell: no overlap anywhere (diagnosis of synchronization faults)
	bool								m_fill_mode_non_solid;
	bool								m_memory_budget;
	bool								m_host_query_reset;
	bool								m_pipeline_statistics;
	Query*								m_open_statistics;	///< pipeline statistics query between Begin and End
	bool								m_lost;

private:
	// Swap chain and back buffer
	HWND								m_hwnd;
	VkSurfaceKHR						m_surface;
	VkSwapchainKHR						m_swapchain;
	VkFormat							m_swapchain_format;
	VkExtent2D							m_swapchain_extent;
	std::vector<VkImage>				m_swapchain_images;
	std::vector<VkSemaphore>			m_rendered;			///< per swap chain image
	uint32_t							m_present_image;
	VkPresentModeKHR					m_present_mode;
	bool								m_immediate_supported;
	bool								m_mailbox_supported;
	Texture*							m_back_buffer;

	// Frames and submissions
	Frame								m_frames[FRAMES_IN_FLIGHT];
	unsigned							m_frame;
	uint64_t							m_serial;			///< submission being recorded
	uint64_t							m_upload_batch;		///< counts opened upload command buffers
	uint64_t							m_completed;		///< last submission the GPU finished
	uint64_t							m_next_id;
	long								m_live_objects;

	// Layouts, dummies, pipelines
	VkDescriptorSetLayout				m_set_layouts[STAGE_COUNT];
	VkPipelineLayout					m_pipeline_layout;
	VkPipelineCache						m_pipeline_cache;
	std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> m_pipelines;

	// Pipelines built while drawing: they stall the frame that needs them first.
	struct PipelineSpike { uint64_t frame; unsigned count; float ms; };
	enum { PIPELINE_SPIKES = 16 };
	struct PipelineStats
	{
		unsigned	built, failed;
		double		total_ms, frame_ms, worst_ms;
		unsigned	frame_built, worst_built, frames_with_builds, frames_over;
		uint64_t	worst_frame;
		unsigned	spike_count;
		PipelineSpike spikes[PIPELINE_SPIKES];
		unsigned	disk_kb;	///< pipeline cache data taken from disk at start
		// Synchronization: full barriers, image layout transitions, rendering scopes (totals, and at the
		// end of the previous frame).
		uint64_t	barriers, transitions, renderings;
		uint64_t	mark_barriers, mark_transitions, mark_renderings;
		unsigned	frames;
	};
	PipelineStats						m_pipeline_stats;
	// The same per shown frame (loading a map uploads thousands of textures, so means say little).
	enum { FRAME_SAMPLES_MAX = 100000 };
	std::vector<uint32_t>				m_frame_barriers, m_frame_transitions, m_frame_renderings;
	void	End_Frame_Statistics();
	Texture*							m_null_texture;
	VkImageView							m_null_view;
	Buffer*								m_null_buffer;
	VkSampler							m_default_sampler;

	// Context state
	StageState							m_stages[STAGE_COUNT];
	Ref<InputLayout>					m_input_layout;
	GPU_PRIMITIVE_TOPOLOGY				m_topology;
	Ref<Buffer>							m_vertex_buffers[VB_SLOTS];
	UINT								m_vertex_strides[VB_SLOTS];
	UINT								m_vertex_offsets[VB_SLOTS];
	Ref<Buffer>							m_index_buffer;
	GPU_FORMAT							m_index_format;
	UINT								m_index_offset;
	Ref<BlendState>						m_blend;
	FLOAT								m_blend_factor[4];
	Ref<DepthStencilState>				m_depth_stencil;
	UINT								m_stencil_ref;
	Ref<RasterizerState>				m_rasterizer;
	GPU_VIEWPORT						m_viewport;
	bool								m_viewport_set;
	Ref<RenderTargetView>				m_targets[RT_SLOTS];
	Ref<DepthStencilView>				m_depth;

	// Command buffer state
	bool								m_rendering;		///< inside vkCmdBeginRendering
	bool								m_barrier_pending;	///< a transfer happened since the last draw
	bool								m_draw_since_barrier;
	VkPipeline							m_bound_pipeline;
	VkPrimitiveTopology					m_bound_topology;
	bool								m_dynamic_dirty;
	VkBuffer							m_bound_vb[VB_SLOTS];
	VkDeviceSize						m_bound_vb_offset[VB_SLOTS];
	VkDeviceSize						m_bound_vb_stride[VB_SLOTS];
	VkBuffer							m_bound_ib;
	VkDeviceSize						m_bound_ib_offset;
	VkExtent2D							m_render_extent;

	// Messages logged once
	std::vector<const char*>			m_logged;
};

/// The handles of the pipeline being built (vulkandevice_pipeline.cpp).
VkPrimitiveTopology	Vk_Topology(GPU_PRIMITIVE_TOPOLOGY topology, uint32_t* topology_class);

template <class Interface>
Object<Interface>::Object(Gpu* gpu) : m_gpu(gpu), m_refs(1), m_id(gpu->Next_Id())
{
	gpu->Object_Created();
}

template <class Interface>
Object<Interface>::~Object()
{
	m_gpu->Object_Destroyed();
}

}  // namespace vkdev

#endif // VULKANDEVICE_INTERNAL_H
