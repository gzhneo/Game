/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// d3d11device.cpp -- see d3d11device.h.

#include "d3d11device.h"
#include "d3d11d3d.h"
#include "WW3D2/Common/gpulayers.h"
#include <d3dcompiler.h>

#include <cstddef>
#include <new>

// --- The descriptions are the D3D11 structures ----------------------------------------------------

#include "d3d11device_enums.inl"

#define GPU_SAME_LAYOUT(gpu, d3d) static_assert(sizeof(gpu) == sizeof(d3d) && alignof(gpu) == alignof(d3d), #gpu)
#define GPU_SAME_MEMBER(gpu, d3d, member) static_assert(offsetof(gpu, member) == offsetof(d3d, member), #gpu "::" #member)

GPU_SAME_LAYOUT(GPU_SAMPLE_DESC, DXGI_SAMPLE_DESC);
GPU_SAME_MEMBER(GPU_SAMPLE_DESC, DXGI_SAMPLE_DESC, Quality);
GPU_SAME_LAYOUT(GPU_BUFFER_DESC, D3D11_BUFFER_DESC);
GPU_SAME_MEMBER(GPU_BUFFER_DESC, D3D11_BUFFER_DESC, Usage);
GPU_SAME_MEMBER(GPU_BUFFER_DESC, D3D11_BUFFER_DESC, BindFlags);
GPU_SAME_MEMBER(GPU_BUFFER_DESC, D3D11_BUFFER_DESC, CPUAccessFlags);
GPU_SAME_MEMBER(GPU_BUFFER_DESC, D3D11_BUFFER_DESC, MiscFlags);
GPU_SAME_MEMBER(GPU_BUFFER_DESC, D3D11_BUFFER_DESC, StructureByteStride);
GPU_SAME_LAYOUT(GPU_TEXTURE2D_DESC, D3D11_TEXTURE2D_DESC);
GPU_SAME_MEMBER(GPU_TEXTURE2D_DESC, D3D11_TEXTURE2D_DESC, Format);
GPU_SAME_MEMBER(GPU_TEXTURE2D_DESC, D3D11_TEXTURE2D_DESC, SampleDesc);
GPU_SAME_MEMBER(GPU_TEXTURE2D_DESC, D3D11_TEXTURE2D_DESC, Usage);
GPU_SAME_MEMBER(GPU_TEXTURE2D_DESC, D3D11_TEXTURE2D_DESC, MiscFlags);
GPU_SAME_LAYOUT(GPU_SUBRESOURCE_DATA, D3D11_SUBRESOURCE_DATA);
GPU_SAME_MEMBER(GPU_SUBRESOURCE_DATA, D3D11_SUBRESOURCE_DATA, SysMemPitch);
GPU_SAME_MEMBER(GPU_SUBRESOURCE_DATA, D3D11_SUBRESOURCE_DATA, SysMemSlicePitch);
GPU_SAME_LAYOUT(GPU_MAPPED_SUBRESOURCE, D3D11_MAPPED_SUBRESOURCE);
GPU_SAME_MEMBER(GPU_MAPPED_SUBRESOURCE, D3D11_MAPPED_SUBRESOURCE, RowPitch);
GPU_SAME_MEMBER(GPU_MAPPED_SUBRESOURCE, D3D11_MAPPED_SUBRESOURCE, DepthPitch);
GPU_SAME_LAYOUT(GPU_BOX, D3D11_BOX);
GPU_SAME_MEMBER(GPU_BOX, D3D11_BOX, back);
GPU_SAME_LAYOUT(GPU_VIEWPORT, D3D11_VIEWPORT);
GPU_SAME_MEMBER(GPU_VIEWPORT, D3D11_VIEWPORT, MaxDepth);
GPU_SAME_LAYOUT(GPU_RECT, D3D11_RECT);
GPU_SAME_LAYOUT(GPU_INPUT_ELEMENT_DESC, D3D11_INPUT_ELEMENT_DESC);
GPU_SAME_MEMBER(GPU_INPUT_ELEMENT_DESC, D3D11_INPUT_ELEMENT_DESC, SemanticIndex);
GPU_SAME_MEMBER(GPU_INPUT_ELEMENT_DESC, D3D11_INPUT_ELEMENT_DESC, Format);
GPU_SAME_MEMBER(GPU_INPUT_ELEMENT_DESC, D3D11_INPUT_ELEMENT_DESC, InputSlot);
GPU_SAME_MEMBER(GPU_INPUT_ELEMENT_DESC, D3D11_INPUT_ELEMENT_DESC, AlignedByteOffset);
GPU_SAME_MEMBER(GPU_INPUT_ELEMENT_DESC, D3D11_INPUT_ELEMENT_DESC, InputSlotClass);
GPU_SAME_MEMBER(GPU_INPUT_ELEMENT_DESC, D3D11_INPUT_ELEMENT_DESC, InstanceDataStepRate);
GPU_SAME_LAYOUT(GPU_RENDER_TARGET_BLEND_DESC, D3D11_RENDER_TARGET_BLEND_DESC);
GPU_SAME_MEMBER(GPU_RENDER_TARGET_BLEND_DESC, D3D11_RENDER_TARGET_BLEND_DESC, SrcBlend);
GPU_SAME_MEMBER(GPU_RENDER_TARGET_BLEND_DESC, D3D11_RENDER_TARGET_BLEND_DESC, BlendOpAlpha);
GPU_SAME_MEMBER(GPU_RENDER_TARGET_BLEND_DESC, D3D11_RENDER_TARGET_BLEND_DESC, RenderTargetWriteMask);
GPU_SAME_LAYOUT(GPU_BLEND_DESC, D3D11_BLEND_DESC);
GPU_SAME_MEMBER(GPU_BLEND_DESC, D3D11_BLEND_DESC, RenderTarget);
GPU_SAME_LAYOUT(GPU_DEPTH_STENCILOP_DESC, D3D11_DEPTH_STENCILOP_DESC);
GPU_SAME_MEMBER(GPU_DEPTH_STENCILOP_DESC, D3D11_DEPTH_STENCILOP_DESC, StencilFunc);
GPU_SAME_LAYOUT(GPU_DEPTH_STENCIL_DESC, D3D11_DEPTH_STENCIL_DESC);
GPU_SAME_MEMBER(GPU_DEPTH_STENCIL_DESC, D3D11_DEPTH_STENCIL_DESC, StencilEnable);
GPU_SAME_MEMBER(GPU_DEPTH_STENCIL_DESC, D3D11_DEPTH_STENCIL_DESC, StencilWriteMask);
GPU_SAME_MEMBER(GPU_DEPTH_STENCIL_DESC, D3D11_DEPTH_STENCIL_DESC, FrontFace);
GPU_SAME_MEMBER(GPU_DEPTH_STENCIL_DESC, D3D11_DEPTH_STENCIL_DESC, BackFace);
GPU_SAME_LAYOUT(GPU_RASTERIZER_DESC, D3D11_RASTERIZER_DESC);
GPU_SAME_MEMBER(GPU_RASTERIZER_DESC, D3D11_RASTERIZER_DESC, DepthBias);
GPU_SAME_MEMBER(GPU_RASTERIZER_DESC, D3D11_RASTERIZER_DESC, SlopeScaledDepthBias);
GPU_SAME_MEMBER(GPU_RASTERIZER_DESC, D3D11_RASTERIZER_DESC, AntialiasedLineEnable);
GPU_SAME_LAYOUT(GPU_SAMPLER_DESC, D3D11_SAMPLER_DESC);
GPU_SAME_MEMBER(GPU_SAMPLER_DESC, D3D11_SAMPLER_DESC, MipLODBias);
GPU_SAME_MEMBER(GPU_SAMPLER_DESC, D3D11_SAMPLER_DESC, BorderColor);
GPU_SAME_MEMBER(GPU_SAMPLER_DESC, D3D11_SAMPLER_DESC, MaxLOD);
GPU_SAME_LAYOUT(GPU_SHADER_RESOURCE_VIEW_DESC, D3D11_SHADER_RESOURCE_VIEW_DESC);
GPU_SAME_MEMBER(GPU_SHADER_RESOURCE_VIEW_DESC, D3D11_SHADER_RESOURCE_VIEW_DESC, Texture2D);
GPU_SAME_LAYOUT(GPU_RENDER_TARGET_VIEW_DESC, D3D11_RENDER_TARGET_VIEW_DESC);
GPU_SAME_MEMBER(GPU_RENDER_TARGET_VIEW_DESC, D3D11_RENDER_TARGET_VIEW_DESC, Texture2D);
GPU_SAME_LAYOUT(GPU_DEPTH_STENCIL_VIEW_DESC, D3D11_DEPTH_STENCIL_VIEW_DESC);
GPU_SAME_MEMBER(GPU_DEPTH_STENCIL_VIEW_DESC, D3D11_DEPTH_STENCIL_VIEW_DESC, Flags);
GPU_SAME_MEMBER(GPU_DEPTH_STENCIL_VIEW_DESC, D3D11_DEPTH_STENCIL_VIEW_DESC, Texture2D);
GPU_SAME_LAYOUT(GPU_QUERY_DESC, D3D11_QUERY_DESC);
GPU_SAME_LAYOUT(GPU_QUERY_DATA_TIMESTAMP_DISJOINT, D3D11_QUERY_DATA_TIMESTAMP_DISJOINT);
GPU_SAME_MEMBER(GPU_QUERY_DATA_TIMESTAMP_DISJOINT, D3D11_QUERY_DATA_TIMESTAMP_DISJOINT, Disjoint);
GPU_SAME_LAYOUT(GPU_QUERY_DATA_PIPELINE_STATISTICS, D3D11_QUERY_DATA_PIPELINE_STATISTICS);
GPU_SAME_MEMBER(GPU_QUERY_DATA_PIPELINE_STATISTICS, D3D11_QUERY_DATA_PIPELINE_STATISTICS, HSInvocations);
GPU_SAME_MEMBER(GPU_QUERY_DATA_PIPELINE_STATISTICS, D3D11_QUERY_DATA_PIPELINE_STATISTICS, DSInvocations);
GPU_SAME_LAYOUT(GPU_SHADER_MACRO, D3D_SHADER_MACRO);
GPU_SAME_MEMBER(GPU_SHADER_MACRO, D3D_SHADER_MACRO, Definition);
static_assert(GPU_COMPILE_OPTIMIZATION_LEVEL3 == D3DCOMPILE_OPTIMIZATION_LEVEL3, "compile flag");
static_assert(GPU_DEFAULT_SAMPLE_MASK == D3D11_DEFAULT_SAMPLE_MASK, "sample mask");
static_assert(GPU_APPEND_ALIGNED_ELEMENT == D3D11_APPEND_ALIGNED_ELEMENT, "append aligned");

#undef GPU_SAME_LAYOUT
#undef GPU_SAME_MEMBER

namespace {

template <class T, class U> inline const T* As(const U* p) { return reinterpret_cast<const T*>(p); }
template <class T, class U> inline T* As(U* p) { return reinterpret_cast<T*>(p); }

// --- Objects --------------------------------------------------------------------------------------

/// A device layer object around one COM interface; the wrapper owns one reference of it.
template <class Base, class Native_Type>
class D3D11Wrap : public Base
{
public:
	explicit D3D11Wrap(Native_Type* native) : m_native(native), m_refs(1) {}
	ULONG AddRef() override { return ++m_refs; }
	ULONG Release() override
	{
		const ULONG refs = --m_refs;
		if (refs == 0) {
			m_native->Release();
			delete this;
		}
		return refs;
	}
	void* Native() override { return m_native; }
	Native_Type* Get() const { return m_native; }
protected:
	~D3D11Wrap() override {}
	Native_Type*	m_native;
	ULONG			m_refs;
};

class D3D11Buffer final : public D3D11Wrap<GpuBuffer, ID3D11Buffer>
{
public:
	using D3D11Wrap::D3D11Wrap;
	void GetDesc(GPU_BUFFER_DESC* desc) override { m_native->GetDesc(As<D3D11_BUFFER_DESC>(desc)); }
};

class D3D11Texture final : public D3D11Wrap<GpuTexture2D, ID3D11Texture2D>
{
public:
	using D3D11Wrap::D3D11Wrap;
	void GetDesc(GPU_TEXTURE2D_DESC* desc) override { m_native->GetDesc(As<D3D11_TEXTURE2D_DESC>(desc)); }
};

typedef D3D11Wrap<GpuShaderResourceView, ID3D11ShaderResourceView>	D3D11SRV;
typedef D3D11Wrap<GpuRenderTargetView, ID3D11RenderTargetView>		D3D11RTV;
typedef D3D11Wrap<GpuDepthStencilView, ID3D11DepthStencilView>		D3D11DSV;
typedef D3D11Wrap<GpuVertexShader, ID3D11VertexShader>				D3D11VS;
typedef D3D11Wrap<GpuPixelShader, ID3D11PixelShader>				D3D11PS;
typedef D3D11Wrap<GpuHullShader, ID3D11HullShader>					D3D11HS;
typedef D3D11Wrap<GpuDomainShader, ID3D11DomainShader>				D3D11DS;
typedef D3D11Wrap<GpuInputLayout, ID3D11InputLayout>				D3D11Layout;
typedef D3D11Wrap<GpuBlendState, ID3D11BlendState>					D3D11Blend;
typedef D3D11Wrap<GpuDepthStencilState, ID3D11DepthStencilState>	D3D11Depth;
typedef D3D11Wrap<GpuRasterizerState, ID3D11RasterizerState>		D3D11Raster;
typedef D3D11Wrap<GpuSamplerState, ID3D11SamplerState>				D3D11Sampler;
typedef D3D11Wrap<GpuQuery, ID3D11Query>							D3D11Query;

class D3D11Blob final : public D3D11Wrap<GpuBlob, ID3DBlob>
{
public:
	using D3D11Wrap::D3D11Wrap;
	void* GetBufferPointer() override { return m_native->GetBufferPointer(); }
	SIZE_T GetBufferSize() override { return m_native->GetBufferSize(); }
};

/// Native interface of a device layer object, nullptr for nullptr. Every object of this device is a
/// D3D11Wrap, so Native() returns the COM interface of the expected type.
template <class T> inline T* N(GpuObject* object) { return object ? static_cast<T*>(object->Native()) : nullptr; }

/// Wraps a new COM object; on failure the result stays nullptr.
template <class Wrapper, class Out, class NativeT> inline HRESULT Wrap(HRESULT hr, NativeT* native, Out** out)
{
	if (!out) { if (native) native->Release(); return hr; }
	*out = nullptr;
	if (FAILED(hr) || !native) return FAILED(hr) ? hr : E_FAIL;
	Wrapper* w = new (std::nothrow) Wrapper(native);
	if (!w) { native->Release(); return E_OUTOFMEMORY; }
	*out = w;
	return hr;
}

// Arrays of interfaces for the binding calls; D3D11 binds at most 16 constant buffers or
// samplers and 128 views per call, the core never more than 16.
enum { MAX_BIND = 16 };
template <class T> struct Native_Array
{
	T* items[MAX_BIND];
	Native_Array(GpuObject* const* objects, UINT count)
	{
		for (UINT i = 0; i < count && i < MAX_BIND; ++i) items[i] = objects ? N<T>(objects[i]) : nullptr;
	}
};

/// ID3DInclude over the core's include handler.
class Include_Bridge final : public ID3DInclude
{
public:
	explicit Include_Bridge(GpuShaderInclude* include) : m_include(include) {}
	STDMETHOD(Open)(D3D_INCLUDE_TYPE, LPCSTR file_name, LPCVOID, LPCVOID* data, UINT* bytes) override
	{
		return m_include ? m_include->Open(file_name, data, bytes) : E_FAIL;
	}
	STDMETHOD(Close)(LPCVOID data) override
	{
		if (m_include) m_include->Close(data);
		return S_OK;
	}
private:
	GpuShaderInclude* m_include;
};

// --- Device ---------------------------------------------------------------------------------------

class D3D11GpuDevice final : public GpuDevice
{
public:
	explicit D3D11GpuDevice(ID3D11Device* device) : m_device(device) { m_device->AddRef(); }
	~D3D11GpuDevice() override { m_device->Release(); }

	HRESULT CreateBuffer(const GPU_BUFFER_DESC* desc, const GPU_SUBRESOURCE_DATA* initial, GpuBuffer** out) override
	{
		ID3D11Buffer* p = nullptr;
		const HRESULT hr = m_device->CreateBuffer(As<D3D11_BUFFER_DESC>(desc), As<D3D11_SUBRESOURCE_DATA>(initial), out ? &p : nullptr);
		return Wrap<D3D11Buffer>(hr, p, out);
	}
	HRESULT CreateTexture2D(const GPU_TEXTURE2D_DESC* desc, const GPU_SUBRESOURCE_DATA* initial, GpuTexture2D** out) override
	{
		ID3D11Texture2D* p = nullptr;
		const HRESULT hr = m_device->CreateTexture2D(As<D3D11_TEXTURE2D_DESC>(desc), As<D3D11_SUBRESOURCE_DATA>(initial), out ? &p : nullptr);
		return Wrap<D3D11Texture>(hr, p, out);
	}
	HRESULT CreateShaderResourceView(GpuResource* resource, const GPU_SHADER_RESOURCE_VIEW_DESC* desc, GpuShaderResourceView** out) override
	{
		ID3D11ShaderResourceView* p = nullptr;
		const HRESULT hr = m_device->CreateShaderResourceView(N<ID3D11Resource>(resource), As<D3D11_SHADER_RESOURCE_VIEW_DESC>(desc), out ? &p : nullptr);
		return Wrap<D3D11SRV>(hr, p, out);
	}
	HRESULT CreateRenderTargetView(GpuResource* resource, const GPU_RENDER_TARGET_VIEW_DESC* desc, GpuRenderTargetView** out) override
	{
		ID3D11RenderTargetView* p = nullptr;
		const HRESULT hr = m_device->CreateRenderTargetView(N<ID3D11Resource>(resource), As<D3D11_RENDER_TARGET_VIEW_DESC>(desc), out ? &p : nullptr);
		return Wrap<D3D11RTV>(hr, p, out);
	}
	HRESULT CreateDepthStencilView(GpuResource* resource, const GPU_DEPTH_STENCIL_VIEW_DESC* desc, GpuDepthStencilView** out) override
	{
		ID3D11DepthStencilView* p = nullptr;
		const HRESULT hr = m_device->CreateDepthStencilView(N<ID3D11Resource>(resource), As<D3D11_DEPTH_STENCIL_VIEW_DESC>(desc), out ? &p : nullptr);
		return Wrap<D3D11DSV>(hr, p, out);
	}
	HRESULT CreateVertexShader(const void* code, SIZE_T size, void*, GpuVertexShader** out) override
	{
		ID3D11VertexShader* p = nullptr;
		const HRESULT hr = m_device->CreateVertexShader(code, size, nullptr, out ? &p : nullptr);
		return Wrap<D3D11VS>(hr, p, out);
	}
	HRESULT CreatePixelShader(const void* code, SIZE_T size, void*, GpuPixelShader** out) override
	{
		ID3D11PixelShader* p = nullptr;
		const HRESULT hr = m_device->CreatePixelShader(code, size, nullptr, out ? &p : nullptr);
		return Wrap<D3D11PS>(hr, p, out);
	}
	HRESULT CreateHullShader(const void* code, SIZE_T size, void*, GpuHullShader** out) override
	{
		ID3D11HullShader* p = nullptr;
		const HRESULT hr = m_device->CreateHullShader(code, size, nullptr, out ? &p : nullptr);
		return Wrap<D3D11HS>(hr, p, out);
	}
	HRESULT CreateDomainShader(const void* code, SIZE_T size, void*, GpuDomainShader** out) override
	{
		ID3D11DomainShader* p = nullptr;
		const HRESULT hr = m_device->CreateDomainShader(code, size, nullptr, out ? &p : nullptr);
		return Wrap<D3D11DS>(hr, p, out);
	}
	HRESULT CreateInputLayout(const GPU_INPUT_ELEMENT_DESC* elements, UINT count, const void* vs_code, SIZE_T vs_size, GpuInputLayout** out) override
	{
		ID3D11InputLayout* p = nullptr;
		const HRESULT hr = m_device->CreateInputLayout(As<D3D11_INPUT_ELEMENT_DESC>(elements), count, vs_code, vs_size, out ? &p : nullptr);
		return Wrap<D3D11Layout>(hr, p, out);
	}
	HRESULT CreateBlendState(const GPU_BLEND_DESC* desc, GpuBlendState** out) override
	{
		ID3D11BlendState* p = nullptr;
		const HRESULT hr = m_device->CreateBlendState(As<D3D11_BLEND_DESC>(desc), out ? &p : nullptr);
		return Wrap<D3D11Blend>(hr, p, out);
	}
	HRESULT CreateDepthStencilState(const GPU_DEPTH_STENCIL_DESC* desc, GpuDepthStencilState** out) override
	{
		ID3D11DepthStencilState* p = nullptr;
		const HRESULT hr = m_device->CreateDepthStencilState(As<D3D11_DEPTH_STENCIL_DESC>(desc), out ? &p : nullptr);
		return Wrap<D3D11Depth>(hr, p, out);
	}
	HRESULT CreateRasterizerState(const GPU_RASTERIZER_DESC* desc, GpuRasterizerState** out) override
	{
		ID3D11RasterizerState* p = nullptr;
		const HRESULT hr = m_device->CreateRasterizerState(As<D3D11_RASTERIZER_DESC>(desc), out ? &p : nullptr);
		return Wrap<D3D11Raster>(hr, p, out);
	}
	HRESULT CreateSamplerState(const GPU_SAMPLER_DESC* desc, GpuSamplerState** out) override
	{
		ID3D11SamplerState* p = nullptr;
		const HRESULT hr = m_device->CreateSamplerState(As<D3D11_SAMPLER_DESC>(desc), out ? &p : nullptr);
		return Wrap<D3D11Sampler>(hr, p, out);
	}
	HRESULT CreateQuery(const GPU_QUERY_DESC* desc, GpuQuery** out) override
	{
		ID3D11Query* p = nullptr;
		const HRESULT hr = m_device->CreateQuery(As<D3D11_QUERY_DESC>(desc), out ? &p : nullptr);
		return Wrap<D3D11Query>(hr, p, out);
	}
	HRESULT CheckMultisampleQualityLevels(GPU_FORMAT format, UINT samples, UINT* levels) override
	{
		return m_device->CheckMultisampleQualityLevels(static_cast<DXGI_FORMAT>(format), samples, levels);
	}

	HRESULT CompileShader(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
						  GpuShaderInclude* include, const char* entry, const char* profile, UINT flags,
						  GpuBlob** code, GpuBlob** errors) override
	{
		Include_Bridge bridge(include);
		ID3DBlob* c = nullptr;
		ID3DBlob* e = nullptr;
		const HRESULT hr = D3DCompile(source, length, name, As<D3D_SHADER_MACRO>(macros), &bridge, entry, profile, flags, 0, &c, &e);
		if (errors) *errors = nullptr;
		if (code) *code = nullptr;
		if (e) { if (errors) Wrap<D3D11Blob>(S_OK, e, errors); else e->Release(); }
		if (c) { if (code) Wrap<D3D11Blob>(S_OK, c, code); else c->Release(); }
		return hr;
	}
	HRESULT PreprocessShader(const char* source, SIZE_T length, const char* name, const GPU_SHADER_MACRO* macros,
							 GpuShaderInclude* include, GpuBlob** text) override
	{
		Include_Bridge bridge(include);
		ID3DBlob* t = nullptr;
		ID3DBlob* e = nullptr;
		const HRESULT hr = D3DPreprocess(source, length, name, As<D3D_SHADER_MACRO>(macros), &bridge, &t, &e);
		if (e) e->Release();
		if (text) *text = nullptr;
		if (t) { if (text && SUCCEEDED(hr)) Wrap<D3D11Blob>(S_OK, t, text); else t->Release(); }
		return hr;
	}
	const char* ShaderFormat() override { return "dxbc"; }
	UINT ShaderCompilerVersion() override { return D3D_COMPILER_VERSION; }

	ID3D11Device* Get() const { return m_device; }

private:
	ID3D11Device*	m_device;
};

// --- Context --------------------------------------------------------------------------------------

class D3D11GpuContext final : public GpuContext
{
public:
	D3D11GpuContext(ID3D11Device* device, ID3D11DeviceContext* context)
		: m_context(context), m_context1(nullptr), m_clear_view(false), m_cb_offsets(false)
	{
		m_context->AddRef();
		if (FAILED(m_context->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&m_context1)))) {
			m_context1 = nullptr;
		}
		// Direct3D 11.1: the interface alone is not enough, the driver must support the feature
		// (D3D11_FEATURE_D3D11_OPTIONS). ClearView is the only clear that takes rectangles; constant
		// buffer offsets need offsetting and NO_OVERWRITE on dynamic constant buffers.
		if (m_context1) {
			D3D11_FEATURE_DATA_D3D11_OPTIONS options;
			::ZeroMemory(&options, sizeof(options));
			if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)))) {
				m_clear_view = options.ClearView != FALSE;
				m_cb_offsets = options.ConstantBufferOffsetting && options.MapNoOverwriteOnDynamicConstantBuffer;
			}
		}
	}
	~D3D11GpuContext() override
	{
		if (m_context1) m_context1->Release();
		m_context->Release();
	}

	void IASetInputLayout(GpuInputLayout* layout) override { m_context->IASetInputLayout(N<ID3D11InputLayout>(layout)); }
	void IASetPrimitiveTopology(GPU_PRIMITIVE_TOPOLOGY topology) override
	{
		m_context->IASetPrimitiveTopology(static_cast<D3D11_PRIMITIVE_TOPOLOGY>(topology));
	}
	void IASetVertexBuffers(UINT start, UINT count, GpuBuffer* const* buffers, const UINT* strides, const UINT* offsets) override
	{
		Native_Array<ID3D11Buffer> a(reinterpret_cast<GpuObject* const*>(buffers), count);
		m_context->IASetVertexBuffers(start, count, a.items, strides, offsets);
	}
	void IASetIndexBuffer(GpuBuffer* buffer, GPU_FORMAT format, UINT offset) override
	{
		m_context->IASetIndexBuffer(N<ID3D11Buffer>(buffer), static_cast<DXGI_FORMAT>(format), offset);
	}

	void VSSetShader(GpuVertexShader* shader, void* const*, UINT) override { m_context->VSSetShader(N<ID3D11VertexShader>(shader), nullptr, 0); }
	void VSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) override
	{
		Native_Array<ID3D11Buffer> a(reinterpret_cast<GpuObject* const*>(buffers), count);
		m_context->VSSetConstantBuffers(start, count, a.items);
	}
	void VSSetConstantBuffers1(UINT start, UINT count, GpuBuffer* const* buffers, const UINT* first, const UINT* num) override
	{
		Native_Array<ID3D11Buffer> a(reinterpret_cast<GpuObject* const*>(buffers), count);
		m_context1->VSSetConstantBuffers1(start, count, a.items, first, num);
	}
	void VSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) override
	{
		Native_Array<ID3D11ShaderResourceView> a(reinterpret_cast<GpuObject* const*>(views), count);
		m_context->VSSetShaderResources(start, count, a.items);
	}
	void VSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) override
	{
		Native_Array<ID3D11SamplerState> a(reinterpret_cast<GpuObject* const*>(samplers), count);
		m_context->VSSetSamplers(start, count, a.items);
	}

	void HSSetShader(GpuHullShader* shader, void* const*, UINT) override { m_context->HSSetShader(N<ID3D11HullShader>(shader), nullptr, 0); }
	void HSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) override
	{
		Native_Array<ID3D11Buffer> a(reinterpret_cast<GpuObject* const*>(buffers), count);
		m_context->HSSetConstantBuffers(start, count, a.items);
	}
	void HSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) override
	{
		Native_Array<ID3D11ShaderResourceView> a(reinterpret_cast<GpuObject* const*>(views), count);
		m_context->HSSetShaderResources(start, count, a.items);
	}
	void HSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) override
	{
		Native_Array<ID3D11SamplerState> a(reinterpret_cast<GpuObject* const*>(samplers), count);
		m_context->HSSetSamplers(start, count, a.items);
	}

	void DSSetShader(GpuDomainShader* shader, void* const*, UINT) override { m_context->DSSetShader(N<ID3D11DomainShader>(shader), nullptr, 0); }
	void DSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) override
	{
		Native_Array<ID3D11Buffer> a(reinterpret_cast<GpuObject* const*>(buffers), count);
		m_context->DSSetConstantBuffers(start, count, a.items);
	}
	void DSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) override
	{
		Native_Array<ID3D11ShaderResourceView> a(reinterpret_cast<GpuObject* const*>(views), count);
		m_context->DSSetShaderResources(start, count, a.items);
	}
	void DSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) override
	{
		Native_Array<ID3D11SamplerState> a(reinterpret_cast<GpuObject* const*>(samplers), count);
		m_context->DSSetSamplers(start, count, a.items);
	}

	void PSSetShader(GpuPixelShader* shader, void* const*, UINT) override { m_context->PSSetShader(N<ID3D11PixelShader>(shader), nullptr, 0); }
	void PSSetConstantBuffers(UINT start, UINT count, GpuBuffer* const* buffers) override
	{
		Native_Array<ID3D11Buffer> a(reinterpret_cast<GpuObject* const*>(buffers), count);
		m_context->PSSetConstantBuffers(start, count, a.items);
	}
	void PSSetConstantBuffers1(UINT start, UINT count, GpuBuffer* const* buffers, const UINT* first, const UINT* num) override
	{
		Native_Array<ID3D11Buffer> a(reinterpret_cast<GpuObject* const*>(buffers), count);
		m_context1->PSSetConstantBuffers1(start, count, a.items, first, num);
	}
	void PSSetShaderResources(UINT start, UINT count, GpuShaderResourceView* const* views) override
	{
		Native_Array<ID3D11ShaderResourceView> a(reinterpret_cast<GpuObject* const*>(views), count);
		m_context->PSSetShaderResources(start, count, a.items);
	}
	void PSSetSamplers(UINT start, UINT count, GpuSamplerState* const* samplers) override
	{
		Native_Array<ID3D11SamplerState> a(reinterpret_cast<GpuObject* const*>(samplers), count);
		m_context->PSSetSamplers(start, count, a.items);
	}

	void OMSetRenderTargets(UINT count, GpuRenderTargetView* const* targets, GpuDepthStencilView* depth) override
	{
		Native_Array<ID3D11RenderTargetView> a(reinterpret_cast<GpuObject* const*>(targets), count);
		m_context->OMSetRenderTargets(count, targets ? a.items : nullptr, N<ID3D11DepthStencilView>(depth));
	}
	void OMSetBlendState(GpuBlendState* state, const FLOAT factor[4], UINT sample_mask) override
	{
		m_context->OMSetBlendState(N<ID3D11BlendState>(state), factor, sample_mask);
	}
	void OMSetDepthStencilState(GpuDepthStencilState* state, UINT stencil_ref) override
	{
		m_context->OMSetDepthStencilState(N<ID3D11DepthStencilState>(state), stencil_ref);
	}
	void RSSetViewports(UINT count, const GPU_VIEWPORT* viewports) override { m_context->RSSetViewports(count, As<D3D11_VIEWPORT>(viewports)); }
	void RSSetState(GpuRasterizerState* state) override { m_context->RSSetState(N<ID3D11RasterizerState>(state)); }

	void Draw(UINT vertices, UINT first) override { m_context->Draw(vertices, first); }
	void DrawIndexed(UINT indices, UINT first, INT base_vertex) override { m_context->DrawIndexed(indices, first, base_vertex); }
	void DrawInstanced(UINT vertices, UINT instances, UINT first_vertex, UINT first_instance) override
	{
		m_context->DrawInstanced(vertices, instances, first_vertex, first_instance);
	}
	void DrawIndexedInstanced(UINT indices, UINT instances, UINT first_index, INT base_vertex, UINT first_instance) override
	{
		m_context->DrawIndexedInstanced(indices, instances, first_index, base_vertex, first_instance);
	}

	HRESULT Map(GpuResource* resource, UINT subresource, GPU_MAP type, UINT flags, GPU_MAPPED_SUBRESOURCE* mapped) override
	{
		return m_context->Map(N<ID3D11Resource>(resource), subresource, static_cast<D3D11_MAP>(type), flags, As<D3D11_MAPPED_SUBRESOURCE>(mapped));
	}
	void Unmap(GpuResource* resource, UINT subresource) override { m_context->Unmap(N<ID3D11Resource>(resource), subresource); }
	void UpdateSubresource(GpuResource* resource, UINT subresource, const GPU_BOX* box, const void* data, UINT row_pitch, UINT depth_pitch) override
	{
		m_context->UpdateSubresource(N<ID3D11Resource>(resource), subresource, As<D3D11_BOX>(box), data, row_pitch, depth_pitch);
	}
	void CopyResource(GpuResource* dest, GpuResource* source) override
	{
		m_context->CopyResource(N<ID3D11Resource>(dest), N<ID3D11Resource>(source));
	}
	void CopySubresourceRegion(GpuResource* dest, UINT dest_sub, UINT x, UINT y, UINT z,
							   GpuResource* source, UINT source_sub, const GPU_BOX* box) override
	{
		m_context->CopySubresourceRegion(N<ID3D11Resource>(dest), dest_sub, x, y, z, N<ID3D11Resource>(source), source_sub, As<D3D11_BOX>(box));
	}
	void ResolveSubresource(GpuResource* dest, UINT dest_sub, GpuResource* source, UINT source_sub, GPU_FORMAT format) override
	{
		m_context->ResolveSubresource(N<ID3D11Resource>(dest), dest_sub, N<ID3D11Resource>(source), source_sub, static_cast<DXGI_FORMAT>(format));
	}

	void ClearRenderTargetView(GpuRenderTargetView* view, const FLOAT rgba[4]) override
	{
		m_context->ClearRenderTargetView(N<ID3D11RenderTargetView>(view), rgba);
	}
	void ClearDepthStencilView(GpuDepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil) override
	{
		m_context->ClearDepthStencilView(N<ID3D11DepthStencilView>(view), flags, depth, stencil);
	}
	void ClearView(GpuView* view, const FLOAT rgba[4], const GPU_RECT* rects, UINT count) override
	{
		if (m_clear_view) m_context1->ClearView(N<ID3D11View>(view), rgba, rects, count);
	}

	void Begin(GpuQuery* query) override { m_context->Begin(N<ID3D11Query>(query)); }
	void End(GpuQuery* query) override { m_context->End(N<ID3D11Query>(query)); }
	HRESULT GetData(GpuQuery* query, void* data, UINT size, UINT flags) override
	{
		return m_context->GetData(N<ID3D11Query>(query), data, size, flags);
	}

	void ClearState() override { m_context->ClearState(); }
	void Flush() override { m_context->Flush(); }

	bool Supports_Clear_View() override { return m_clear_view; }
	bool Supports_Constant_Buffer_Offsets() override { return m_cb_offsets; }

	ID3D11DeviceContext* Get() const { return m_context; }

private:
	ID3D11DeviceContext*	m_context;
	ID3D11DeviceContext1*	m_context1;
	bool					m_clear_view;
	bool					m_cb_offsets;
};

}  // namespace

bool D3D11_Create_Gpu_Device(ID3D11Device* device, ID3D11DeviceContext* context, GpuDevice** gpu_device, GpuContext** gpu_context)
{
	if (!device || !context || !gpu_device || !gpu_context) return false;
	*gpu_device = new (std::nothrow) D3D11GpuDevice(device);
	*gpu_context = new (std::nothrow) D3D11GpuContext(device, context);
	if (!*gpu_device || !*gpu_context) {
		D3D11_Destroy_Gpu_Device(*gpu_device, *gpu_context);
		return false;
	}
	return true;
}

void D3D11_Destroy_Gpu_Device(GpuDevice*& gpu_device, GpuContext*& gpu_context)
{
	delete static_cast<D3D11GpuContext*>(gpu_context);
	delete static_cast<D3D11GpuDevice*>(gpu_device);
	gpu_context = nullptr;
	gpu_device = nullptr;
}

GpuTexture2D* D3D11_Wrap_Texture(ID3D11Texture2D* texture)
{
	if (!texture) return nullptr;
	texture->AddRef();
	GpuTexture2D* out = nullptr;
	Wrap<D3D11Texture>(S_OK, texture, &out);
	return out;
}

ID3D11Resource* D3D11_Native_Resource(GpuResource* resource)
{
	return N<ID3D11Resource>(resource);
}

// --- Output: DXGI adapters, swap chain, presentation ----------------------------------------------

#include "wwdebug.h"

namespace {

enum { MAX_REPORTED_MESSAGES = 128 };

class D3D11Output final : public GpuOutput
{
public:
	bool Create_Interface() override
	{
		if (m_factory) return true;
		const HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&m_factory));
		if (FAILED(hr)) {
			WWDEBUG_SAY(("D3D11: CreateDXGIFactory1 failed (0x%08X)\n", (unsigned)hr));
			m_factory = nullptr;
			return false;
		}
		return true;
	}

	void Release_Interface() override { D3D11_Release(m_factory); }
	void* Interface_Handle() override { return m_factory; }

	unsigned Get_Adapter_Count() override
	{
		if (!m_factory) return 0;
		unsigned count = 0;
		IDXGIAdapter1* a = nullptr;
		while (m_factory->EnumAdapters1(count, &a) != DXGI_ERROR_NOT_FOUND) {
			a->Release();
			++count;
		}
		return count;
	}

	bool Get_Adapter_Info(unsigned adapter, GPU_ADAPTER_INFO* info) override
	{
		::ZeroMemory(info, sizeof(GPU_ADAPTER_INFO));
		IDXGIAdapter1* a = nullptr;
		if (!m_factory || m_factory->EnumAdapters1(adapter, &a) == DXGI_ERROR_NOT_FOUND) return false;
		DXGI_ADAPTER_DESC1 desc;
		::ZeroMemory(&desc, sizeof(desc));
		a->GetDesc1(&desc);
		a->Release();
		// The description is UTF-16; the engine keeps names as 8 bit text.
		::WideCharToMultiByte(CP_ACP, 0, desc.Description, -1, info->Description, sizeof(info->Description), nullptr, nullptr);
		info->VendorId = desc.VendorId;
		info->DeviceId = desc.DeviceId;
		info->SubSysId = desc.SubSysId;
		info->Revision = desc.Revision;
		return true;
	}

	bool Create_Device(void* hwnd, unsigned buffer_width, unsigned buffer_height, GpuDevice** gpu_device, GpuContext** gpu_context) override
	{
		*gpu_device = nullptr;
		*gpu_context = nullptr;
		if (!Create_Interface()) return false;
		m_hwnd = static_cast<HWND>(hwnd);

		UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
		// Only if the SDK layers are installed; otherwise silently without.
		UINT debug_flags = flags | D3D11_CREATE_DEVICE_DEBUG;
#endif
		const D3D_FEATURE_LEVEL wanted[] = { D3D_FEATURE_LEVEL_11_0 };
		D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;

		HRESULT hr = E_FAIL;
#ifdef _DEBUG
		hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, debug_flags,
							   wanted, 1, D3D11_SDK_VERSION, &m_device, &got, &m_context);
		if (FAILED(hr)) {
			WWDEBUG_SAY(("D3D11: debug layer not available, device without debug layer.\n"));
		}
#endif
		if (FAILED(hr)) {
			hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
								   wanted, 1, D3D11_SDK_VERSION, &m_device, &got, &m_context);
		}
		if (FAILED(hr)) {
			WWDEBUG_SAY(("D3D11: no device with feature level 11.0 (0x%08X)\n", (unsigned)hr));
			m_device = nullptr;
			m_context = nullptr;
			return false;
		}

		// If the debug layer runs, grab its message queue. Without the layer the interface does not
		// exist; the pointer stays NULL and Drain_Debug_Messages does nothing.
#ifdef _DEBUG
		m_device->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&m_info_queue));
		if (m_info_queue) {
			// Do not break into the debugger -- the messages go to the log instead of stopping the game.
			m_info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, FALSE);
			m_info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, FALSE);
			m_info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, FALSE);
			WWDEBUG_SAY(("D3D11: debug layer active, messages go to the game log\n"));
		}
#endif

		// Tearing (VRR) only if the runtime reports it.
		m_allow_tearing = false;
		{
			IDXGIFactory5* f5 = nullptr;
			if (SUCCEEDED(m_factory->QueryInterface(__uuidof(IDXGIFactory5), (void**)&f5)) && f5) {
				BOOL tearing = FALSE;
				if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))))
					m_allow_tearing = (tearing != FALSE);
				f5->Release();
			}
		}

		// Flip model swap chain; fullscreen is a borderless window covering the monitor.
		IDXGIFactory2* factory2 = nullptr;
		if (FAILED(m_factory->QueryInterface(__uuidof(IDXGIFactory2), (void**)&factory2)) || !factory2) {
			WWDEBUG_SAY(("D3D11: IDXGIFactory2 not available -- flip model swap chain not possible.\n"));
			Release_Native();
			return false;
		}
		DXGI_SWAP_CHAIN_DESC1 sd;
		::ZeroMemory(&sd, sizeof(sd));
		sd.Width       = buffer_width;
		sd.Height      = buffer_height;
		sd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
		sd.SampleDesc.Count = 1;			// MSAA lives in the scene target, not the back buffer
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 2;
		sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		// The buffer has the aspect ratio of the window (presentlayout.h), so DXGI's stretch onto the
		// window is uniform.
		sd.Scaling     = DXGI_SCALING_STRETCH;
		sd.Flags       = m_allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		hr = factory2->CreateSwapChainForHwnd(m_device, m_hwnd, &sd, nullptr, nullptr, &m_swap_chain);
		factory2->Release();
		if (FAILED(hr)) {
			WWDEBUG_SAY(("D3D11: CreateSwapChainForHwnd failed (0x%08X)\n", (unsigned)hr));
			m_swap_chain = nullptr;
			Release_Native();
			return false;
		}
		m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

		// Limit input latency. By default DXGI lets the CPU run three frames ahead, which means
		// noticeable cursor lag in a real-time game. This goes through the device (IDXGIDevice1),
		// not IDXGISwapChain2 -- its SetMaximumFrameLatency requires a swap chain with
		// FRAME_LATENCY_WAITABLE_OBJECT, which would need its own wait pattern per frame.
		{
			IDXGIDevice1* dxgi_device = nullptr;
			if (SUCCEEDED(m_device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgi_device)) && dxgi_device) {
				dxgi_device->SetMaximumFrameLatency(1);
				dxgi_device->Release();
			}
		}

		if (!D3D11_Create_Gpu_Device(m_device, m_context, gpu_device, gpu_context)) {
			Release_Native();
			return false;
		}

		// With multiple monitors a windowed swap chain follows the window by itself. For
		// debugging, log which output it is currently on.
		IDXGIOutput* output = nullptr;
		if (SUCCEEDED(m_swap_chain->GetContainingOutput(&output)) && output) {
			DXGI_OUTPUT_DESC od;
			if (SUCCEEDED(output->GetDesc(&od))) {
				char name[64];
				::WideCharToMultiByte(CP_ACP, 0, od.DeviceName, -1, name, sizeof(name), nullptr, nullptr);
				WWDEBUG_SAY(("D3D11: output %s, desktop area %dx%d at (%d,%d)\n", name,
					(int)(od.DesktopCoordinates.right  - od.DesktopCoordinates.left),
					(int)(od.DesktopCoordinates.bottom - od.DesktopCoordinates.top),
					(int)od.DesktopCoordinates.left, (int)od.DesktopCoordinates.top));
			}
			output->Release();
		}
		return true;
	}

	void Release_Device(GpuDevice*& gpu_device, GpuContext*& gpu_context) override
	{
		if (m_swap_chain) { m_swap_chain->Release(); m_swap_chain = nullptr; }
		Drain_Debug_Messages();		// pick up the last debug layer messages
		if (gpu_context) { gpu_context->ClearState(); gpu_context->Flush(); }
		D3D11_Destroy_Gpu_Device(gpu_device, gpu_context);
		if (m_context) { m_context->ClearState(); m_context->Flush(); }
		D3D11_Release(m_context);
		Report_Live_Objects();		// leak check
		D3D11_Release(m_info_queue);
		m_reported_count = 0;
		D3D11_Release(m_device);
	}

	GpuTexture2D* Get_Back_Buffer() override
	{
		ID3D11Texture2D* buffer = nullptr;
		if (!m_swap_chain || FAILED(m_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&buffer)) || !buffer) return nullptr;
		GpuTexture2D* wrapped = D3D11_Wrap_Texture(buffer);
		buffer->Release();
		return wrapped;
	}

	HRESULT Resize_Buffers(unsigned buffer_width, unsigned buffer_height) override
	{
		if (!m_swap_chain) return E_FAIL;
		const UINT flags = m_allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		return m_swap_chain->ResizeBuffers(0, buffer_width, buffer_height, DXGI_FORMAT_UNKNOWN, flags);
	}

	HRESULT Present(int swap_interval) override
	{
		if (!m_swap_chain) return E_FAIL;
		const UINT flags = (swap_interval == 0 && m_allow_tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
		return m_swap_chain->Present(swap_interval, flags);
	}

	bool Allows_Tearing() override { return m_allow_tearing; }
	bool Is_Device_Lost(HRESULT hr) override { return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET; }

	bool Check_Device_Lost() override
	{
		if (!m_device) return false;
		const HRESULT reason = m_device->GetDeviceRemovedReason();
		if (FAILED(reason)) {
			WWDEBUG_SAY(("D3D11: device lost (0x%08X)\n", (unsigned)reason));
			return true;
		}
		return false;
	}

	// The Direct3D 11 debug layer writes its findings to OutputDebugString, where they only show up
	// with a debugger attached. To have them in the ordinary game log, the queue is drained once per
	// frame and redirected to WWDEBUG_SAY. Each message is reported only once: otherwise the debug layer
	// repeats the same finding on every draw and the log would be unreadable within seconds.
	void Drain_Debug_Messages() override
	{
#ifdef _DEBUG
		if (!m_info_queue) return;
		const UINT64 total = m_info_queue->GetNumStoredMessages();
		for (UINT64 i = 0; i < total; ++i) {
			SIZE_T length = 0;
			if (FAILED(m_info_queue->GetMessage(i, nullptr, &length)) || length == 0) continue;
			D3D11_MESSAGE* message = static_cast<D3D11_MESSAGE*>(::malloc(length));
			if (!message) break;
			if (SUCCEEDED(m_info_queue->GetMessage(i, message, &length)) && message->pDescription) {
				bool known = false;
				for (unsigned k = 0; k < m_reported_count; ++k) {
					if (m_reported[k] == static_cast<unsigned>(message->ID)) { known = true; break; }
				}
				if (!known) {
					if (m_reported_count < MAX_REPORTED_MESSAGES)
						m_reported[m_reported_count++] = static_cast<unsigned>(message->ID);
					const char* severity = "Info";
					switch (message->Severity) {
					case D3D11_MESSAGE_SEVERITY_CORRUPTION: severity = "KORRUPTION"; break;
					case D3D11_MESSAGE_SEVERITY_ERROR:      severity = "FEHLER";     break;
					case D3D11_MESSAGE_SEVERITY_WARNING:    severity = "WARNUNG";    break;
					default: break;
					}
					WWDEBUG_SAY(("D3D11-Debug-Layer [%s, ID %d]: %s\n",
						severity, (int)message->ID, message->pDescription));
				}
			}
			::free(message);
		}
		m_info_queue->ClearStoredMessages();
#endif
	}

	// Free video memory = budget minus the process's usage (IDXGIAdapter3::QueryVideoMemoryInfo,
	// segment LOCAL; on UMA graphics this is the shared memory). Without IDXGIAdapter3 falls back to
	// DedicatedVideoMemory, as 64 bit so 8 GB does not truncate to 0.
	unsigned __int64 Get_Free_Video_Memory() override
	{
		if (!m_factory) return 0;
		IDXGIAdapter1* a = nullptr;
		unsigned __int64 bytes = 0;
		if (m_factory->EnumAdapters1(0, &a) != DXGI_ERROR_NOT_FOUND) {
			IDXGIAdapter3* a3 = nullptr;
			DXGI_QUERY_VIDEO_MEMORY_INFO info;
			if (SUCCEEDED(a->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3))
				&& SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
				bytes = (info.Budget > info.CurrentUsage) ? (info.Budget - info.CurrentUsage) : 0;
			} else {
				DXGI_ADAPTER_DESC1 desc;
				if (SUCCEEDED(a->GetDesc1(&desc))) bytes = desc.DedicatedVideoMemory;
			}
			if (a3) a3->Release();
			a->Release();
		}
		return bytes;
	}

private:
	void Release_Native()
	{
		if (m_swap_chain) { m_swap_chain->Release(); m_swap_chain = nullptr; }
		D3D11_Release(m_info_queue);
		D3D11_Release(m_context);
		D3D11_Release(m_device);
	}

	// When the device is released, the game has freed all textures, buffers, shaders and state objects
	// (RenderDevice::Shutdown, then Release_Draw_Resources). Anything the debug layer still reports as
	// live -- except the device itself -- is a leak. Logs one "D3D11-LEBEND" line per object.
	// Only with the debug layer (debug build).
	void Report_Live_Objects()
	{
#ifdef _DEBUG
		if (m_device == nullptr || m_info_queue == nullptr) return;
		ID3D11Debug* debug = nullptr;
		if (FAILED(m_device->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void**>(&debug))) || debug == nullptr) return;
		m_info_queue->ClearStoredMessages();
		m_info_queue->SetMessageCountLimit(100000);
		debug->ReportLiveDeviceObjects(static_cast<D3D11_RLDO_FLAGS>(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL));
		debug->Release();
		unsigned live = 0;
		const UINT64 total = m_info_queue->GetNumStoredMessages();
		for (UINT64 i = 0; i < total; ++i) {
			SIZE_T length = 0;
			if (FAILED(m_info_queue->GetMessage(i, nullptr, &length)) || length == 0) continue;
			D3D11_MESSAGE* message = static_cast<D3D11_MESSAGE*>(::malloc(length));
			if (!message) break;
			if (SUCCEEDED(m_info_queue->GetMessage(i, message, &length)) && message->pDescription
				&& message->ID != D3D11_MESSAGE_ID_LIVE_DEVICE) {
				WWDEBUG_SAY(("D3D11-LEBEND %s\n", message->pDescription));
				++live;
			}
			::free(message);
		}
		m_info_queue->ClearStoredMessages();
		// Total including the device itself: at least 1, otherwise the report delivered nothing (no proof)
		WWDEBUG_SAY(("D3D11: %u live objects at device shutdown (debug layer report: %u messages)\n",
			live, static_cast<unsigned>(total)));
#endif
	}

	IDXGIFactory1*			m_factory = nullptr;
	ID3D11Device*			m_device = nullptr;
	ID3D11DeviceContext*	m_context = nullptr;
	IDXGISwapChain1*		m_swap_chain = nullptr;
	ID3D11InfoQueue*		m_info_queue = nullptr;
	HWND					m_hwnd = nullptr;
	bool					m_allow_tearing = false;
	unsigned				m_reported[MAX_REPORTED_MESSAGES] = {};
	unsigned				m_reported_count = 0;
};

}  // namespace

GpuOutput* D3D11_Get_Gpu_Output()
{
	// Lives until process exit like the render core (RenderCore::Create).
	static D3D11Output TheOutput;
	return &TheOutput;
}

// Whether feature level 11.0 is there shows only when the device is created; that failure ends the start
// with a message (RenderDevice::Create_Device).
static bool D3D11_Probe(char*, unsigned)
{
	return true;
}

void Gpu_Register_D3D11()
{
	static const GpuLayerInfo Info = {
		"d3d11", "Direct3D 11", "a graphics card with Direct3D feature level 11.0 and a current graphics driver",
		0, D3D11_Probe, D3D11_Get_Gpu_Output, nullptr };
	Gpu_Register_Layer(Info);
}
