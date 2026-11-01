/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// gpudevice.h ------------------------------------------------------------------------------------
//
// The device layer below the render core. It reproduces the part of the Direct3D 11 device and
// immediate context that the render core uses -- same objects, same methods, same semantics
// (renaming on MAP_WRITE_DISCARD, views, state objects bound by the core) -- with neutral types
// from gputypes.h. The render core makes every decision about the image; a device only carries
// out these calls:
//
//   D3D11 device (d3d11device.cpp)  passes every call on to ID3D11Device/ID3D11DeviceContext1.
//   Vulkan device (later)           records the same calls into command buffers and derives
//                                   pipelines, render passes and barriers from them.
//
// Objects are reference counted like COM objects: Create* returns one reference, Release drops it.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef GPUDEVICE_H
#define GPUDEVICE_H

#include "gputypes.h"

class GpuObject
{
public:
	virtual ULONG	AddRef() = 0;
	virtual ULONG	Release() = 0;
	/// The object of the device below (D3D11: the COM interface). Only the device that created it uses it.
	virtual void*	Native() = 0;
protected:
	virtual ~GpuObject() {}
};

class GpuResource : public GpuObject {};

class GpuBuffer : public GpuResource
{
public:
	virtual void	GetDesc(GPU_BUFFER_DESC* desc) = 0;
};

class GpuTexture2D : public GpuResource
{
public:
	virtual void	GetDesc(GPU_TEXTURE2D_DESC* desc) = 0;
};

class GpuView : public GpuObject {};
class GpuShaderResourceView : public GpuView {};
class GpuRenderTargetView : public GpuView {};
class GpuDepthStencilView : public GpuView {};

class GpuVertexShader : public GpuObject {};
class GpuPixelShader : public GpuObject {};
class GpuHullShader : public GpuObject {};
class GpuDomainShader : public GpuObject {};
class GpuInputLayout : public GpuObject {};
class GpuBlendState : public GpuObject {};
class GpuDepthStencilState : public GpuObject {};
class GpuRasterizerState : public GpuObject {};
class GpuSamplerState : public GpuObject {};
class GpuQuery : public GpuObject {};

/// Compiled shader code or a compiler message (ID3DBlob).
class GpuBlob : public GpuObject
{
public:
	virtual void*	GetBufferPointer() = 0;
	virtual SIZE_T	GetBufferSize() = 0;
};

/// A blob in main memory with room for size bytes (shader cache, compiler output of other devices).
GpuBlob* Gpu_Create_Blob(SIZE_T size);

/// Resolves #include during shader compilation (ID3DInclude).
class GpuShaderInclude
{
public:
	virtual HRESULT	Open(const char* file_name, const void** data, UINT* bytes) = 0;
	virtual void	Close(const void* data) = 0;
protected:
	virtual ~GpuShaderInclude() {}
};

/// Object creation (ID3D11Device).
class GpuDevice
{
public:
	virtual HRESULT	CreateBuffer(const GPU_BUFFER_DESC* desc, const GPU_SUBRESOURCE_DATA* initial, GpuBuffer** out) = 0;
	virtual HRESULT	CreateTexture2D(const GPU_TEXTURE2D_DESC* desc, const GPU_SUBRESOURCE_DATA* initial, GpuTexture2D** out) = 0;
	virtual HRESULT	CreateShaderResourceView(GpuResource* resource, const GPU_SHADER_RESOURCE_VIEW_DESC* desc, GpuShaderResourceView** out) = 0;
	virtual HRESULT	CreateRenderTargetView(GpuResource* resource, const GPU_RENDER_TARGET_VIEW_DESC* desc, GpuRenderTargetView** out) = 0;
	virtual HRESULT	CreateDepthStencilView(GpuResource* resource, const GPU_DEPTH_STENCIL_VIEW_DESC* desc, GpuDepthStencilView** out) = 0;
	// The class linkage argument of D3D11 is unused and always nullptr.
	virtual HRESULT	CreateVertexShader(const void* code, SIZE_T size, void* linkage, GpuVertexShader** out) = 0;
	virtual HRESULT	CreatePixelShader(const void* code, SIZE_T size, void* linkage, GpuPixelShader** out) = 0;
	virtual HRESULT	CreateHullShader(const void* code, SIZE_T size, void* linkage, GpuHullShader** out) = 0;
	virtual HRESULT	CreateDomainShader(const void* code, SIZE_T size, void* linkage, GpuDomainShader** out) = 0;
	virtual HRESULT	CreateInputLayout(const GPU_INPUT_ELEMENT_DESC* elements, UINT count, const void* vs_code, SIZE_T vs_size, GpuInputLayout** out) = 0;
	virtual HRESULT	CreateBlendState(const GPU_BLEND_DESC* desc, GpuBlendState** out) = 0;
	virtual HRESULT	CreateDepthStencilState(const GPU_DEPTH_STENCIL_DESC* desc, GpuDepthStencilState** out) = 0;
	virtual HRESULT	CreateRasterizerState(const GPU_RASTERIZER_DESC* desc, GpuRasterizerState** out) = 0;
	virtual HRESULT	CreateSamplerState(const GPU_SAMPLER_DESC* desc, GpuSamplerState** out) = 0;
	virtual HRESULT	CreateQuery(const GPU_QUERY_DESC* desc, GpuQuery** out) = 0;
	virtual HRESULT	CheckMultisampleQualityLevels(GPU_FORMAT format, UINT samples, UINT* levels) = 0;

	/// Compiles HLSL for this device (D3D11: d3dcompiler into DXBC). profile is the D3D name ("vs_4_0", "hs_5_0").
	virtual HRESULT	CompileShader(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
								  GpuShaderInclude* include, const char* entry, const char* profile, UINT flags,
								  GpuBlob** code, GpuBlob** errors) = 0;
	/// Runs only the preprocessor (for the shader cache key: includes and macros resolved).
	virtual HRESULT	PreprocessShader(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
									 GpuShaderInclude* include, GpuBlob** text) = 0;
	/// Identifies the code CompileShader produces (part of the shader cache key).
	virtual const char*	ShaderFormat() = 0;
	/// Version of the shader compiler (part of the shader cache key).
	virtual UINT	ShaderCompilerVersion() = 0;
	/// One line for the runner log about objects the device builds behind the scenes (empty if none).
	virtual void	Get_Device_Statistics(char* text, unsigned size) { if (text && size) text[0] = 0; }

protected:
	virtual ~GpuDevice() {}
};

/// Commands (ID3D11DeviceContext1). The shader setters take the unused class instance list of D3D11.
class GpuContext
{
public:
	virtual void	IASetInputLayout(GpuInputLayout* layout) = 0;
	virtual void	IASetPrimitiveTopology(GPU_PRIMITIVE_TOPOLOGY topology) = 0;
	virtual void	IASetVertexBuffers(UINT start, UINT count, GpuBuffer* const* buffers, const UINT* strides, const UINT* offsets) = 0;
	virtual void	IASetIndexBuffer(GpuBuffer* buffer, GPU_FORMAT format, UINT offset) = 0;

	virtual void	VSSetShader(GpuVertexShader* shader, void* const* instances, UINT count) = 0;
	virtual void	VSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) = 0;
	virtual void	VSSetConstantBuffers1(UINT start, UINT count, GpuBuffer* const* buffers, const UINT* first, const UINT* num) = 0;
	virtual void	VSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) = 0;
	virtual void	VSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) = 0;

	virtual void	HSSetShader(GpuHullShader* shader, void* const* instances, UINT count) = 0;
	virtual void	HSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) = 0;
	virtual void	HSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) = 0;
	virtual void	HSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) = 0;

	virtual void	DSSetShader(GpuDomainShader* shader, void* const* instances, UINT count) = 0;
	virtual void	DSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) = 0;
	virtual void	DSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) = 0;
	virtual void	DSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) = 0;

	virtual void	PSSetShader(GpuPixelShader* shader, void* const* instances, UINT count) = 0;
	virtual void	PSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) = 0;
	virtual void	PSSetConstantBuffers1(UINT start, UINT count, GpuBuffer* const* buffers, const UINT* first, const UINT* num) = 0;
	virtual void	PSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) = 0;
	virtual void	PSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) = 0;

	virtual void	OMSetRenderTargets(UINT count, GpuRenderTargetView* const* targets, GpuDepthStencilView* depth) = 0;
	virtual void	OMSetBlendState(GpuBlendState* state, const FLOAT factor[4], UINT sample_mask) = 0;
	virtual void	OMSetDepthStencilState(GpuDepthStencilState* state, UINT stencil_ref) = 0;
	virtual void	RSSetViewports(UINT count, const GPU_VIEWPORT* viewports) = 0;
	virtual void	RSSetState(GpuRasterizerState* state) = 0;

	virtual void	Draw(UINT vertices, UINT first) = 0;
	virtual void	DrawIndexed(UINT indices, UINT first, INT base_vertex) = 0;
	virtual void	DrawInstanced(UINT vertices, UINT instances, UINT first_vertex, UINT first_instance) = 0;
	virtual void	DrawIndexedInstanced(UINT indices, UINT instances, UINT first_index, INT base_vertex, UINT first_instance) = 0;

	virtual HRESULT	Map(GpuResource* resource, UINT subresource, GPU_MAP type, UINT flags, GPU_MAPPED_SUBRESOURCE* mapped) = 0;
	virtual void	Unmap(GpuResource* resource, UINT subresource) = 0;
	virtual void	UpdateSubresource(GpuResource* resource, UINT subresource, const GPU_BOX* box, const void* data, UINT row_pitch, UINT depth_pitch) = 0;
	virtual void	CopyResource(GpuResource* dest, GpuResource* source) = 0;
	virtual void	CopySubresourceRegion(GpuResource* dest, UINT dest_sub, UINT x, UINT y, UINT z,
										  GpuResource* source, UINT source_sub, const GPU_BOX* box) = 0;
	virtual void	ResolveSubresource(GpuResource* dest, UINT dest_sub, GpuResource* source, UINT source_sub, GPU_FORMAT format) = 0;

	virtual void	ClearRenderTargetView(GpuRenderTargetView* view, const FLOAT rgba[4]) = 0;
	virtual void	ClearDepthStencilView(GpuDepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil) = 0;
	/// Clears rectangles of a view (D3D11.1 ClearView); only if Supports_Clear_View.
	virtual void	ClearView(GpuView* view, const FLOAT rgba[4], const GPU_RECT* rects, UINT count) = 0;

	virtual void	Begin(GpuQuery* query) = 0;
	virtual void	End(GpuQuery* query) = 0;
	virtual HRESULT	GetData(GpuQuery* query, void* data, UINT size, UINT flags) = 0;

	virtual void	ClearState() = 0;
	virtual void	Flush() = 0;

	/// D3D11.1 features the core uses when present.
	virtual bool	Supports_Clear_View() = 0;
	/// Constant buffers bound with an offset (VS/PSSetConstantBuffers1) and NO_OVERWRITE maps of dynamic constant buffers.
	virtual bool	Supports_Constant_Buffer_Offsets() = 0;

protected:
	virtual ~GpuContext() {}
};

/// One graphics adapter.
struct GPU_ADAPTER_INFO
{
	char	Description[128];	///< 8 bit text in the system code page
	UINT	VendorId;
	UINT	DeviceId;
	UINT	SubSysId;
	UINT	Revision;
};

/// Adapters, the window's swap chain and presentation (DXGI for D3D11). Creates the device and context.
class GpuOutput
{
public:
	virtual bool		Create_Interface() = 0;			///< false if the runtime is missing
	virtual void		Release_Interface() = 0;
	virtual void*		Interface_Handle() = 0;			///< opaque, non-null while the interface exists
	virtual unsigned	Get_Adapter_Count() = 0;
	virtual bool		Get_Adapter_Info(unsigned adapter, GPU_ADAPTER_INFO* info) = 0;

	/// Device, immediate context and a flip model swap chain of buffer_width x buffer_height (B8G8R8A8,
	/// two buffers) for the window. false: no device with the minimum feature set.
	virtual bool		Create_Device(void* hwnd, unsigned buffer_width, unsigned buffer_height,
									  GpuDevice** device, GpuContext** context) = 0;
	/// Releases swap chain, context and device (reports live objects in debug builds).
	virtual void		Release_Device(GpuDevice*& device, GpuContext*& context) = 0;
	/// The swap chain's buffer (one reference).
	virtual GpuTexture2D* Get_Back_Buffer() = 0;
	/// Resizes the swap chain; every reference to the old buffer must be gone.
	virtual HRESULT		Resize_Buffers(unsigned buffer_width, unsigned buffer_height) = 0;
	/// Shows the buffer; swap_interval 0 = no VSync (with tearing if the display allows it).
	virtual HRESULT		Present(int swap_interval) = 0;
	virtual bool		Allows_Tearing() = 0;
	/// true if the HRESULT means the device was lost.
	virtual bool		Is_Device_Lost(HRESULT hr) = 0;
	/// true if the device has been lost meanwhile.
	virtual bool		Check_Device_Lost() = 0;
	/// Writes new messages of the debug layer to the log (each message once).
	virtual void		Drain_Debug_Messages() = 0;
	/// Free video memory in bytes (budget minus usage, or the dedicated memory).
	virtual unsigned __int64 Get_Free_Video_Memory() = 0;
protected:
	virtual ~GpuOutput() {}
};

#endif // GPUDEVICE_H
