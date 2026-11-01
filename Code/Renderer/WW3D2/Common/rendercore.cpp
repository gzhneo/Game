/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// rendercore.cpp -------------------------------------------------------------------------------
//
// Direct3D 11 render backend.
//
// Device, flip model swap chain, offscreen scene target, Present, Clear, viewport and resize;
// textures, surfaces, vertex/index buffers, locking, render targets, back buffer copies and the
// WW3DFormat -> DXGI mapping. Render state is kept as a shadow copy and applied in the draw path,
// which emulates the fixed-function pipeline with uber shaders.
//
// Basics: feature level 11.0, B8G8R8A8 back buffer, flip model swap chain (FLIP_DISCARD, 2 buffers),
// no exclusive fullscreen, and rendering always goes to an offscreen scene target (colour +
// depth/stencil, optional MSAA) that Present resolves into the back buffer.
//
// This file holds device, swap chain, state, shader cache and the draw path; resources, effects, shadows,
// terrain and the GPU instancing paths live in the other rendercore_*.cpp files, the shared internals in
// rendercore_internal.h.
// ------------------------------------------------------------------------------------------------

#include "renderobjects.h"
#include "rendercore.h"
#include "renderstates.h"
#include "renderformat.h"
#include "rendercb.h"
#include "renderdevice.h"
#include "rendercaps.h"
#include "wwdebug.h"
#include "wwstring.h"
#include "ffactory.h"
#include "WWFILE.H"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "rendercore_internal.h"
#include "gpudisplay.h"
#include "gpulayers.h"

static const unsigned TT_PROBE_CLEAR = 0xFFFF0000u;	///< crack probe: clear colour (AARRGGBB, red)

// Collect one-time messages for missing paths so the log does not fill up.
static const char*	_missing[64];
static unsigned		_missing_count = 0;

void RenderCore::Log_Missing(const char* what)
{
	for (unsigned i = 0; i < _missing_count; ++i) {
		if (strcmp(_missing[i], what) == 0) return;
	}
	if (_missing_count < 64) _missing[_missing_count++] = what;
	WWDEBUG_SAY(("Render: %s is not implemented.\n", what));
}

RenderCore::RenderCore()
{
	// The state lives in a static Impl, not in the memory pool: the backend itself lives until
	// process exit (see RenderCore::Create), and a pool block would show up in the leak
	// report. There is only ever one backend at a time.
	static Impl TheImpl;
	M = &TheImpl;
	Reset_State();
}

RenderCore::~RenderCore()
{
	Release_Device();
	Release_Interface();
}

// Initial state: after construction and again when RenderDevice::Init runs after a Shutdown.
void RenderCore::Reset_State()
{
	::ZeroMemory(M, sizeof(Impl));
	M->swap_interval = 1;
	M->sample_count  = 1;
	M->windowed      = true;
	Init_Default_States();
}

// The device layer the core runs on; outside Impl because Reset_State clears Impl.
static const GpuLayerInfo* Selected_Layer = nullptr;

RenderBackend* RenderCore::Create(const GpuLayerInfo& layer)
{
	// The object lives until process exit: RenderDevice::Shutdown keeps it because subsystems torn
	// down later still release their graphics resources through Release_Resource. A memory pool block
	// that never comes back would show up in the leak report at exit, hence a static object instead
	// of W3DNEW. Reset_State restores the initial state if Init runs again after a Shutdown.
	// The layer outputs are static objects too: create them first so that they are destroyed after the core,
	// whose destructor releases its interface through the output.
	for (unsigned i = 0; i < Gpu_Layer_Count(); ++i) Gpu_Layer(i)->output();
	static RenderCore TheRenderCore;
	TheRenderCore.Reset_State();
	Selected_Layer = &layer;
	return &TheRenderCore;
}

bool RenderCore::Use_Layer(const GpuLayerInfo& layer)
{
	GpuOutput* output = layer.output();
	Release_Device();
	M->device = nullptr;
	M->context = nullptr;
	if (M->output && M->output != output) M->output->Release_Interface();
	M->output = output;
	Selected_Layer = &layer;
	Init_Default_States();
	return Create_Interface();
}

RenderBackend* Render_Core_Create(const GpuLayerInfo& layer)
{
	return RenderCore::Create(layer);
}

bool Render_Core_Use_Layer(RenderBackend* backend, const GpuLayerInfo& layer)
{
	// RenderCore is the only backend; it runs on every device layer.
	return backend && static_cast<RenderCore*>(backend)->Use_Layer(layer);
}

// ==================================================================================================
// 1. Device, swap chain, frame
// ==================================================================================================

bool RenderCore::Create_Interface()
{
	if (!M->output && Selected_Layer) M->output = Selected_Layer->output();
	return M->output && M->output->Create_Interface();
}

void RenderCore::Release_Interface()
{
	if (M->output) M->output->Release_Interface();
}

void RenderCore::Set_Window(void* hwnd)			{ M->hwnd = static_cast<HWND>(hwnd); }
WW3DInterfaceHandle RenderCore::Get_Interface() const	{ return reinterpret_cast<WW3DInterfaceHandle>(M->output ? M->output->Interface_Handle() : nullptr); }
WW3DDeviceHandle	RenderCore::Get_Device() const	{ return reinterpret_cast<WW3DDeviceHandle>(M->device); }

unsigned RenderCore::Get_Adapter_Count()
{
	return M->output ? M->output->Get_Adapter_Count() : 0;
}

bool RenderCore::Get_Adapter_Identifier(unsigned adapter, WW3DAdapterIdentifier* id)
{
	::ZeroMemory(id, sizeof(WW3DAdapterIdentifier));
	GPU_ADAPTER_INFO info;
	if (!M->output || !M->output->Get_Adapter_Info(adapter, &info)) return false;
	strncpy(id->Description, info.Description, sizeof(id->Description) - 1);
	strncpy(id->Driver, Selected_Layer ? Selected_Layer->title : "", sizeof(id->Driver) - 1);
	id->VendorId      = info.VendorId;
	id->DeviceId      = info.DeviceId;
	id->SubSysId      = info.SubSysId;
	id->Revision      = info.Revision;
	id->DriverVersion = 0;
	id->WHQLLevel     = 1;
	return true;
}

// The display modes belong to the monitor, not to the device layer: the primary monitor's 32 bit sizes for
// the default adapter (the one the device is created on). The count reads the list, the index reads it back.
unsigned RenderCore::Get_Adapter_Mode_Count(unsigned adapter, unsigned device_format)
{
	if (adapter != 0 || (device_format != WW3D_FMT_X8R8G8B8 && device_format != WW3D_FMT_A8R8G8B8)) return 0;
	return Gpu_Display_Refresh_Modes();
}

bool RenderCore::Enum_Adapter_Mode(unsigned adapter, unsigned device_format, unsigned index,
									 unsigned* width, unsigned* height, unsigned* format)
{
	if (adapter != 0 || !Gpu_Display_Mode(index, width, height)) return false;
	if (format) *format = device_format;
	return true;
}

bool RenderCore::Get_Adapter_Display_Mode(unsigned adapter, unsigned* width, unsigned* height, unsigned* format)
{
	// The "desktop mode" is the monitor resolution, the format is always 32 bit.
	if (adapter != 0 || !Gpu_Display_Desktop_Size(width, height)) return false;
	if (format) *format = WW3D_FMT_X8R8G8B8;
	return true;
}

bool RenderCore::Build_Device_Parameters(RenderDeviceSetup& setup)
{
	M->hwnd          = static_cast<HWND>(setup.hwnd);
	M->width         = setup.width  > 0 ? setup.width  : 640;
	M->height        = setup.height > 0 ? setup.height : 480;
	M->windowed      = setup.windowed;
	M->swap_interval = setup.swap_interval;
	M->sample_count  = (setup.multisample_samples >= 2) ? static_cast<unsigned>(setup.multisample_samples) : 1;

	// The back buffer is always 32 bit; there are no 16 bit modes.
	setup.bit_depth = 32;
	return true;
}

bool RenderCore::Create_Device()
{
	if (!Create_Interface()) return false;

	Update_Present_Layout();
	if (!M->output->Create_Device(M->hwnd, M->layout.bufferWidth, M->layout.bufferHeight, &M->device, &M->context))
		return false;
	M->allow_tearing = M->output->Allows_Tearing();
	Set_Shader_Compile_Device(M->device);

	// Constant buffer offsets (Direct3D 11.1, D3D11_FEATURE_D3D11_OPTIONS): needs offsetting and NO_OVERWRITE
	// on dynamic constant buffers; -noConstantBufferRing keeps one DISCARD per buffer and draw (comparisons).
	M->cbr_context = nullptr;
	if (RenderDevice::Constant_Buffer_Ring_Enabled()) {
		if (M->context->Supports_Constant_Buffer_Offsets()) M->cbr_context = M->context;
		WWDEBUG_SAY(("Render: constant buffer ring %s\n", M->cbr_context ? "on" : "off (no Direct3D 11.1 offsets)"));
	}

	M->back_buffer = M->output->Get_Back_Buffer();
	if (!M->back_buffer || FAILED(M->device->CreateRenderTargetView(M->back_buffer, nullptr, &M->back_buffer_rtv))) {
		WWDEBUG_SAY(("Render: back buffer view failed\n"));
		return false;
	}

	if (!Create_Scene_Target(M->width, M->height))
		return false;

	M->device_removed = false;
	WWDEBUG_SAY(("Render: device created, feature level 11.0, %ux%u, %u sample(s)%s\n",
		M->width, M->height, M->sample_count, M->allow_tearing ? ", tearing allowed" : ""));
	return true;
}

/// Updates the two scene target handles. The objects are reused on purpose: the game code keeps
/// the "old" render target across a resolution change, and a new object would invalidate its copy.
void RenderCore::Update_Scene_Surfaces(unsigned width, unsigned height)
{
	if (!M->scene_surface) {
		M->scene_surface = W3DNEW WW3DSurfaceObject;
		::ZeroMemory(M->scene_surface, sizeof(WW3DSurfaceObject));
		M->scene_surface->refcount = 1;
	}
	if (!M->scene_depth_surface) {
		M->scene_depth_surface = W3DNEW WW3DSurfaceObject;
		::ZeroMemory(M->scene_depth_surface, sizeof(WW3DSurfaceObject));
		M->scene_depth_surface->refcount = 1;
	}

	WW3DSurfaceObject* color = M->scene_surface;
	Gpu_Release(color->rtv);
	color->rtv = M->scene_rtv;
	if (color->rtv) color->rtv->AddRef();
	color->width       = width;
	color->height      = height;
	// The game sees an 8-bit target either way: copies of a float target go through Fx_Scene_8Bit.
	color->gpu_format = GPU_FORMAT_B8G8R8A8_UNORM;

	WW3DSurfaceObject* depth = M->scene_depth_surface;
	Gpu_Release(depth->dsv);
	depth->dsv = M->scene_dsv;
	if (depth->dsv) depth->dsv->AddRef();
	depth->width       = width;
	depth->height      = height;
	depth->gpu_format = GPU_FORMAT_D24_UNORM_S8_UINT;	// what the engine knows; Scene_Depth_Format is the real one
}

// Size of the scene target for a window of width x height: the same, or with supersampling (render scale above 1)
// the scaled size, at most the largest texture.
static void Scene_Target_Size(unsigned width, unsigned height, unsigned* target_width, unsigned* target_height)
{
	*target_width = width;
	*target_height = height;
	const float s = RenderDevice::Get_Effect_Settings().render_scale;
	if (!(s > 1.001f)) return;
	const float c = (s < 2.0f) ? s : 2.0f;
	const unsigned w = static_cast<unsigned>(static_cast<float>(width) * c + 0.5f);
	const unsigned h = static_cast<unsigned>(static_cast<float>(height) * c + 0.5f);
	if (w > 16384u || h > 16384u) return;
	*target_width = w;
	*target_height = h;
}

bool RenderCore::Create_Scene_Target(unsigned width, unsigned height)
{
	Release_Scene_Target();
	if (!M->device || width == 0 || height == 0) return false;
	unsigned target_width, target_height;
	Scene_Target_Size(width, height, &target_width, &target_height);

	unsigned scene_format = RenderDevice::Get_Effect_Settings().scene_format;
	// RENDER_EFFECT_HDR decides with the target: it needs a float target without multisampling (Hdr_Active). A change
	// of the switch (detail level in the menu) creates the next frame's target anew (Present).
	M->hdr_target = Hdr_Wanted();
	if (scene_format == SCENE_FORMAT_BGRA8 && M->hdr_target)
		scene_format = SCENE_FORMAT_R11G11B10;
	switch (scene_format) {
		case SCENE_FORMAT_RGBA16F:	M->scene_format = GPU_FORMAT_R16G16B16A16_FLOAT; break;
		case SCENE_FORMAT_RGBA16:	M->scene_format = GPU_FORMAT_R16G16B16A16_UNORM; break;
		case SCENE_FORMAT_R11G11B10:
			// The alpha target is single-sampled and the water copies need it; with multisampling RGBA16F instead.
			M->scene_format = (M->sample_count > 1) ? GPU_FORMAT_R16G16B16A16_FLOAT : GPU_FORMAT_R11G11B10_FLOAT;
			break;
		default:					M->scene_format = GPU_FORMAT_B8G8R8A8_UNORM; break;
	}

	// Clamp the sample count to what the device supports for both the color and the depth target.
	unsigned samples = M->sample_count;
	while (samples > 1) {
		UINT color_quality = 0, depth_quality = 0;
		if (SUCCEEDED(M->device->CheckMultisampleQualityLevels(M->scene_format, samples, &color_quality)) && color_quality > 0 &&
			SUCCEEDED(M->device->CheckMultisampleQualityLevels(Scene_Depth_Format(), samples, &depth_quality)) && depth_quality > 0)
			break;
		--samples;
	}
	if (samples < 1) samples = 1;

	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width              = target_width;
	td.Height             = target_height;
	td.MipLevels          = 1;
	td.ArraySize          = 1;
	td.Format             = M->scene_format;
	td.SampleDesc.Count   = samples;
	td.Usage              = GPU_USAGE_DEFAULT;
	td.BindFlags          = GPU_BIND_RENDER_TARGET | (samples == 1 ? GPU_BIND_SHADER_RESOURCE : 0);
	if (FAILED(M->device->CreateTexture2D(&td, nullptr, &M->scene_color))) return false;
	if (FAILED(M->device->CreateRenderTargetView(M->scene_color, nullptr, &M->scene_rtv))) return false;
	if (samples == 1)
		M->device->CreateShaderResourceView(M->scene_color, nullptr, &M->scene_srv);

	GPU_TEXTURE2D_DESC dd = td;
	dd.Format    = Scene_Depth_Format();
	dd.BindFlags = GPU_BIND_DEPTH_STENCIL;
	if (samples > 1) {
		// Readable as a texture, so the effects can resolve the depth (Fx_Resolve_Depth); a single-sample
		// depth buffer is copied with CopyResource instead. Without the view the effects that need depth stay off.
		GPU_TEXTURE2D_DESC rd = dd;
		rd.Format    = Scene_Depth_Typeless();
		rd.BindFlags = GPU_BIND_DEPTH_STENCIL | GPU_BIND_SHADER_RESOURCE;
		if (SUCCEEDED(M->device->CreateTexture2D(&rd, nullptr, &M->scene_depth))) {
			GPU_DEPTH_STENCIL_VIEW_DESC dv;
			::ZeroMemory(&dv, sizeof(dv));
			dv.Format = Scene_Depth_Format();
			dv.ViewDimension = GPU_DSV_DIMENSION_TEXTURE2DMS;
			GPU_SHADER_RESOURCE_VIEW_DESC sv;
			::ZeroMemory(&sv, sizeof(sv));
			sv.Format = Scene_Depth_Sampled();
			sv.ViewDimension = GPU_SRV_DIMENSION_TEXTURE2DMS;
			if (FAILED(M->device->CreateDepthStencilView(M->scene_depth, &dv, &M->scene_dsv)) ||
				FAILED(M->device->CreateShaderResourceView(M->scene_depth, &sv, &M->scene_depth_srv))) {
				Gpu_Release(M->scene_depth_srv);
				Gpu_Release(M->scene_dsv);
				Gpu_Release(M->scene_depth);
			}
		}
		if (!M->scene_depth_srv) {
			WWDEBUG_SAY(("RenderCore: multisampled depth buffer not readable, effects that need depth stay off\n"));
		}
	}
	if (!M->scene_depth) {
		if (FAILED(M->device->CreateTexture2D(&dd, nullptr, &M->scene_depth))) return false;
		if (FAILED(M->device->CreateDepthStencilView(M->scene_depth, nullptr, &M->scene_dsv))) return false;
	}

	if (M->scene_format == GPU_FORMAT_R11G11B10_FLOAT) {
		GPU_TEXTURE2D_DESC ad = td;
		ad.Format    = GPU_FORMAT_R8_UNORM;
		ad.BindFlags = GPU_BIND_RENDER_TARGET | GPU_BIND_SHADER_RESOURCE;
		if (FAILED(M->device->CreateTexture2D(&ad, nullptr, &M->scene_alpha)) ||
			FAILED(M->device->CreateRenderTargetView(M->scene_alpha, nullptr, &M->scene_alpha_rtv)) ||
			FAILED(M->device->CreateShaderResourceView(M->scene_alpha, nullptr, &M->scene_alpha_srv))) return false;
	}

	if (samples > 1) {
		// Target for ResolveSubresource before Present, screen effects and screenshots.
		GPU_TEXTURE2D_DESC rd = td;
		rd.SampleDesc.Count = 1;
		rd.BindFlags        = GPU_BIND_RENDER_TARGET | GPU_BIND_SHADER_RESOURCE;
		M->device->CreateTexture2D(&rd, nullptr, &M->resolve_target);
	}

	M->sample_count = samples;
	M->scene_width  = target_width;
	M->scene_height = target_height;
	M->context->OMSetRenderTargets(1, &M->scene_rtv, M->scene_dsv);
	Invalidate_Bindings(M);

	M->viewport.X = 0; M->viewport.Y = 0;
	M->viewport.Width = width; M->viewport.Height = height;
	M->viewport.MinZ = 0.0f;  M->viewport.MaxZ = 1.0f;
	Set_Viewport(&M->viewport);
	Update_Scene_Surfaces(width, height);
	// Initial state: scene target bound, viewport covering the whole target.
	Set_Render_Target_Surfaces(nullptr, nullptr);
	return true;
}

void RenderCore::Release_Scene_Target()
{
	if (M->context) M->context->OMSetRenderTargets(0, nullptr, nullptr);
	M->current_render_target = nullptr;
	M->bound_rtv = nullptr;
	M->bound_dsv = nullptr;
	// The handles stay (the game code holds them); only their views go away.
	if (M->scene_surface)       Gpu_Release(M->scene_surface->rtv);
	if (M->scene_depth_surface) Gpu_Release(M->scene_depth_surface->dsv);
	M->scene_swapped = false;			// both targets are released below
	M->sa_mode = 0;
	M->sa_bound = false;
	Gpu_Release(M->scene_alpha_srv);
	Gpu_Release(M->scene_alpha_rtv);
	Gpu_Release(M->scene_alpha);
	Gpu_Release(M->fx_convert_ps);	// its variant depends on the alpha target
	Gpu_Release(M->fx_tonemap_ps);
	Gpu_Release(M->resolve_target);
	Gpu_Release(M->scene_out_rtv);
	Gpu_Release(M->scene_out_srv);
	Gpu_Release(M->scene_out);
	Gpu_Release(M->scene_depth_srv);
	Gpu_Release(M->scene_dsv);
	Gpu_Release(M->scene_depth);
	Gpu_Release(M->scene_srv);
	Gpu_Release(M->scene_rtv);
	Gpu_Release(M->scene_color);
	Fx_Release_Targets();			// effect targets depend on the size
}

void RenderCore::Release_Device()
{
	if (M->cursor) { ::DestroyIcon(static_cast<HICON>(M->cursor)); M->cursor = nullptr; }

	// Without a device there is nothing to release. The backend is a static object; its destructor
	// calls Release_Device a second time at process exit, after RenderDevice::Shutdown and after the
	// debug system is gone. Logging the state cache statistics again at that point would trigger
	// the "DebugLog - Debug not inited properly" box.
	if (M->device == nullptr)
		return;

	Release_Draw_Resources();
	Release_Scene_Target();
	if (M->scene_surface)       { Release_Resource(M->scene_surface);       M->scene_surface = nullptr; }
	if (M->scene_depth_surface) { Release_Resource(M->scene_depth_surface); M->scene_depth_surface = nullptr; }
	Gpu_Release(M->back_buffer_rtv);
	Gpu_Release(M->back_buffer);
	M->cbr_context = nullptr;
	// Swap chain, context and device; the debug layer reports live objects (leak check).
	Set_Shader_Compile_Device(nullptr);
	M->output->Release_Device(M->device, M->context);
}

HRESULT RenderCore::Reset_Device()
{
	// D3D11 has no Reset: recreate the size-dependent buffers (ResizeBuffers + scene target).
	if (!M->device) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	// After a device loss (TDR, driver change) ResizeBuffers no longer helps -- everything
	// must be recreated. The caller (RenderDevice::Reset_Device) has released all resources
	// before and recreates them afterwards.
	if (M->device_removed) {
		WWDEBUG_SAY(("Render: device was lost, recreating it\n"));
		Release_Draw_Resources();
		Release_Scene_Target();
		Gpu_Release(M->back_buffer_rtv);
		Gpu_Release(M->back_buffer);
		M->cbr_context = nullptr;
		Set_Shader_Compile_Device(nullptr);
		M->output->Release_Device(M->device, M->context);
		Release_Interface();
		Init_Default_States();
		return Create_Device() ? static_cast<HRESULT>(WW3D_OK) : static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	}

	// Minimised window: ResizeBuffers to 0x0 fails, and a zero-sized scene target cannot exist.
	// The last valid size stays until the window reappears; meanwhile drawing goes into the
	// old buffers.
	if (M->width == 0 || M->height == 0) {
		WWDEBUG_SAY(("Render: reset to size 0 skipped (window minimized)\n"));
		return static_cast<HRESULT>(WW3D_OK);
	}

	Release_Scene_Target();
	HRESULT hr = Resize_Swap_Chain();
	if (FAILED(hr))
		return hr;

	return Create_Scene_Target(M->width, M->height) ? static_cast<HRESULT>(WW3D_OK) : static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
}

// Computes the swap chain buffer size for the current client area (presentlayout.h). A minimised
// window (client size 0) keeps the previous output size.
void RenderCore::Update_Present_Layout()
{
	RECT client = { 0, 0, 0, 0 };
	if (M->hwnd && ::GetClientRect(M->hwnd, &client) && client.right > 0 && client.bottom > 0) {
		M->output_width  = client.right;
		M->output_height = client.bottom;
	}
	if (M->output_width <= 0 || M->output_height <= 0) {
		M->output_width  = static_cast<int>(M->width);
		M->output_height = static_cast<int>(M->height);
	}
	M->layout = Compute_Present_Layout(static_cast<int>(M->width), static_cast<int>(M->height),
									   M->output_width, M->output_height);
}

HRESULT RenderCore::Resize_Swap_Chain()
{
	Gpu_Release(M->back_buffer_rtv);
	Gpu_Release(M->back_buffer);

	Update_Present_Layout();
	HRESULT hr = M->output->Resize_Buffers(M->layout.bufferWidth, M->layout.bufferHeight);
	if (FAILED(hr)) {
		WWDEBUG_SAY(("Render: ResizeBuffers failed (0x%08X)\n", (unsigned)hr));
		if (M->output->Is_Device_Lost(hr)) M->device_removed = true;
		return hr;
	}
	M->back_buffer = M->output->Get_Back_Buffer();
	if (!M->back_buffer || FAILED(M->device->CreateRenderTargetView(M->back_buffer, nullptr, &M->back_buffer_rtv)))
		return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	WWDEBUG_SAY(("Render: output %dx%d, buffer %dx%d, image %ux%u at (%d,%d)\n", M->output_width, M->output_height,
		M->layout.bufferWidth, M->layout.bufferHeight, M->width, M->height, M->layout.offsetX, M->layout.offsetY));
	return static_cast<HRESULT>(WW3D_OK);
}

HRESULT RenderCore::Get_Device_State()
{
	if (!M->device) return static_cast<HRESULT>(WW3D_OK);
	if (M->device_removed) return static_cast<HRESULT>(WW3D_ERR_DEVICENOTRESET);
	if (M->output->Check_Device_Lost()) {
		M->device_removed = true;
		return static_cast<HRESULT>(WW3D_ERR_DEVICENOTRESET);
	}
	return static_cast<HRESULT>(WW3D_OK);
}

bool RenderCore::Validate_Device()	{ return true; }

void RenderCore::Begin_Scene()
{
	// The render target is intentionally *not* touched here: binding the scene target would break
	// the water reflection, which switches to its texture before WW3D::Begin_Render (W3DWater).
	// The scene target is bound once on creation and afterwards via
	// Set_Render_Target_Surface(NULL).
}

void RenderCore::End_Scene()	{ }

// ==================================================================================================
// Debug layer messages
// ==================================================================================================
//
// The Direct3D 11 debug layer writes its findings to OutputDebugString, where they only show up
// with a debugger attached. To have them in the ordinary game log, the queue is drained once per
// frame and redirected to WWDEBUG_SAY.
//
// Each message is reported only once: otherwise the debug layer repeats the same finding on
// every draw and the log would be unreadable within seconds.

void RenderCore::Drain_Debug_Messages()
{
	if (M->output) M->output->Drain_Debug_Messages();
}

HRESULT RenderCore::Present()
{
	if (!M->device || !M->context) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	Fx_Timestamp(Fx_Ts_End(FX_RANGE_FRAME));
	M->rs_active = false;			// a scaled 3D scene ends with the frame at the latest
	Rs_Report_Scene_Size(false);

	// Apply the brightness control in the scene target before the copy to the back buffer
	// (Fx_Apply_Gamma writes to M->scene_color via M->scene_rtv) -- so it acts like the other
	// post-processing passes, and test captures (Save_Back_Buffer_BMP/Get_Back_Buffer also read
	// M->scene_color) include it. With neutral settings (fx_gamma_active false) or if the pass fails
	// (no resources) the scene target stays unchanged.
	if (M->fx_gamma_active) Fx_Apply_Gamma();

	// Bring the scene target to the back buffer: resolve with MSAA, convert a float target, copy directly otherwise.
	GpuTexture2D* source = Fx_Scene_8Bit();
	// The window was resized without a device reset (e.g. restored from minimised, moved to
	// another monitor): adapt the buffer so the image keeps its aspect ratio.
	{
		RECT client = { 0, 0, 0, 0 };
		if (::GetClientRect(M->hwnd, &client) && client.right > 0 && client.bottom > 0 &&
			(client.right != M->output_width || client.bottom != M->output_height))
			Resize_Swap_Chain();
	}
	if (M->back_buffer && source) {
		// With supersampling the scene target is larger; the finished image lies at its top left.
		const bool larger = M->scene_width != M->width || M->scene_height != M->height;
		GPU_BOX box;
		box.left = 0;  box.top = 0;  box.front = 0;
		box.right = M->width;  box.bottom = M->height;  box.back = 1;
		if (!larger && M->layout.bufferWidth == static_cast<int>(M->width) && M->layout.bufferHeight == static_cast<int>(M->height)) {
			M->context->CopyResource(M->back_buffer, source);
		}
		else {
			const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			if (M->back_buffer_rtv && (M->layout.bufferWidth != static_cast<int>(M->width) || M->layout.bufferHeight != static_cast<int>(M->height)))
				M->context->ClearRenderTargetView(M->back_buffer_rtv, black);
			M->context->CopySubresourceRegion(M->back_buffer, 0, M->layout.offsetX, M->layout.offsetY, 0, source, 0, larger ? &box : nullptr);
		}
	}

	if (M->pending_capture[0]) {
		if (!Save_Back_Buffer_BMP(M->pending_capture)) {
			WWDEBUG_SAY(("back buffer screenshot %s failed\n", M->pending_capture));
		}
		M->pending_capture[0] = 0;
	}

	Fx_Scene_Swap_Back();
	HRESULT hr = M->output->Present(M->swap_interval);
	number_of_API_calls++;
	Fx_End_Frame();					// frame counter, GPU timing

	// Supersampling switched on, off or to another factor, or hdr switched: the next frame needs a scene target of the
	// new size or format.
	unsigned target_width, target_height;
	Scene_Target_Size(M->width, M->height, &target_width, &target_height);
	if (M->scene_color && (target_width != M->scene_width || target_height != M->scene_height || Hdr_Wanted() != M->hdr_target)) {
		if (!Create_Scene_Target(M->width, M->height)) {
			WWDEBUG_SAY(("Render: scene target %ux%u (hdr %d) not possible\n", target_width, target_height, Hdr_Wanted() ? 1 : 0));
		}
	}

	Drain_Debug_Messages();		// debug layer findings into the game log
	if (M->output->Is_Device_Lost(hr)) {
		M->device_removed = true;
		return static_cast<HRESULT>(WW3D_ERR_DEVICELOST);
	}
	return hr;
}

void RenderCore::Clear(bool clear_color, bool clear_z_stencil, WW3DColor color, float z, unsigned stencil)
{
	if (!M->context) return;

	// Clear the currently bound target -- not necessarily the scene target. Projected shadows
	// and screen filters draw into a texture and clear it there.
	GpuRenderTargetView* rtv = M->bound_rtv ? M->bound_rtv : M->scene_rtv;
	GpuDepthStencilView* dsv = M->bound_dsv ? M->bound_dsv : M->scene_dsv;

	// Does the viewport cover the whole target? The engine expects Clear to be limited to the
	// viewport; the D3D11 Clear* calls ignore it.
	unsigned target_width = M->scene_width, target_height = M->scene_height;
	if (M->current_render_target) {
		target_width  = M->current_render_target->width;
		target_height = M->current_render_target->height;
	}
	else if (!M->rs_active) {
		// Outside the 3D scene the engine sees the scene target in window size, even if supersampling made it larger.
		target_width  = M->width;
		target_height = M->height;
	}
	const bool full_target = (M->viewport.Width == 0 && M->viewport.Height == 0) ||
							 (M->viewport.X == 0 && M->viewport.Y == 0 &&
							  M->viewport.Width >= target_width && M->viewport.Height >= target_height);

	// Crack probe of the terrain tessellation: red behind the terrain.
	if (clear_color && rtv == M->scene_rtv && !M->current_render_target) {
		const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
		if ((fx.flags & RENDER_EFFECT_TERRAIN_TESS) && fx.terrain_tess_probe) color = TT_PROBE_CLEAR;
	}
	if (clear_color && rtv) {
		// WW3DColor is AARRGGBB.
		const float rgba[4] = {
			((color >> 16) & 0xFF) / 255.0f,
			((color >>  8) & 0xFF) / 255.0f,
			((color      ) & 0xFF) / 255.0f,
			((color >> 24) & 0xFF) / 255.0f };
		// A scene target without alpha clears its alpha target to the alpha of the color.
		GpuRenderTargetView* alpha_rtv = (rtv == M->scene_rtv && !M->scene_swapped) ? M->scene_alpha_rtv : nullptr;
		const float alpha[4] = { rgba[3], rgba[3], rgba[3], rgba[3] };
		if (full_target || !M->context->Supports_Clear_View()) {
			if (!full_target) Log_Missing("Clear of a sub-rectangle (GpuContext missing)");
			M->context->ClearRenderTargetView(rtv, rgba);
			if (alpha_rtv) M->context->ClearRenderTargetView(alpha_rtv, alpha);
		}
		else {
			// ClearView is the only clear path that takes rectangles (Direct3D 11.1).
			GPU_RECT rect;
			rect.left   = static_cast<LONG>(M->viewport.X);
			rect.top    = static_cast<LONG>(M->viewport.Y);
			rect.right  = static_cast<LONG>(M->viewport.X + M->viewport.Width);
			rect.bottom = static_cast<LONG>(M->viewport.Y + M->viewport.Height);
			M->context->ClearView(rtv, rgba, &rect, 1);
			if (alpha_rtv) M->context->ClearView(alpha_rtv, alpha, &rect, 1);
		}
		number_of_API_calls += alpha_rtv ? 2 : 1;
	}
	if (clear_z_stencil && dsv) {
		// ClearDepthStencilView takes no rectangles. A viewport-limited depth clear would need its
		// own draw pass; both callers (W3DView before the extra pass, W3DWater in the reflection)
		// clear at the start of a frame and the rest of the buffer is not read afterwards. So the
		// whole buffer is cleared and the case is reported once.
		if (!full_target) Log_Missing("clearing the depth buffer only inside the viewport");
		M->context->ClearDepthStencilView(dsv, GPU_CLEAR_DEPTH | GPU_CLEAR_STENCIL, z, static_cast<UINT8>(stencil));
		number_of_API_calls++;
	}
}

void RenderCore::Set_Viewport(const WW3DViewport* viewport)
{
	if (!M->context || !viewport) return;
	M->rs_engine_viewport = *viewport;
	const WW3DViewport old = M->viewport;
	M->viewport = *viewport;
	if (M->rs_active && !M->current_render_target) Rs_Scale_Viewport(M->viewport);
	if (::memcmp(&old, &M->viewport, sizeof(old)) != 0) M->frame_cb_dirty = true;
	GPU_VIEWPORT vp;
	// No half-pixel offset: D3D11 rasterises with pixel centres at .5.
	vp.TopLeftX = static_cast<FLOAT>(M->viewport.X);
	vp.TopLeftY = static_cast<FLOAT>(M->viewport.Y);
	vp.Width    = static_cast<FLOAT>(M->viewport.Width);
	vp.Height   = static_cast<FLOAT>(M->viewport.Height);
	vp.MinDepth = M->viewport.MinZ;
	vp.MaxDepth = M->viewport.MaxZ;
	M->context->RSSetViewports(1, &vp);
	number_of_API_calls++;
}

void RenderCore::Get_Viewport(WW3DViewport* viewport)
{
	if (viewport) *viewport = M->viewport;
}

// D3D11 has no gamma ramp that works in windowed mode. The curve runs as its own post-processing
// pass in Present() (Fx_Apply_Gamma) on the finished image including the 2D UI. Formula, per colour
// channel: out = saturate(contrast * pow(saturate(in - limit), 1/gamma) + bright);
// limit = uselimit ? (contrast-1)/2*contrast : 0.
void RenderCore::Set_Gamma(float gamma, float bright, float contrast, bool /*calibrate*/, bool uselimit)
{
	gamma    = std::max(0.6f, std::min(6.0f, gamma));
	bright   = std::max(-0.5f, std::min(0.5f, bright));
	contrast = std::max(0.5f, std::min(2.0f, contrast));

	M->fx_gamma_curve.x = 1.0f / gamma;
	M->fx_gamma_curve.y = bright;
	M->fx_gamma_curve.z = contrast;
	M->fx_gamma_curve.w = uselimit ? (contrast - 1.0f) / 2.0f * contrast : 0.0f;
	// At neutral settings (gamma 1, brightness 0, contrast 1) the curve is the identity: no
	// extra pass, Present() just copies -- image unchanged.
	M->fx_gamma_active = !(gamma == 1.0f && bright == 0.0f && contrast == 1.0f);
}

WW3DFormat RenderCore::Get_Back_Buffer_Format()	{ return WW3D_FORMAT_A8R8G8B8; }
bool RenderCore::Has_Stencil()					{ return true; }	// the scene target always has an 8-bit stencil

void RenderCore::Set_Swap_Interval(int swap)		{ M->swap_interval = swap; }
int  RenderCore::Get_Swap_Interval()				{ return M->swap_interval; }

void RenderCore::Set_Present_Size(int width, int height)
{
	if (width  > 0) M->width  = width;
	if (height > 0) M->height = height;
}

PresentLayout RenderCore::Get_Present_Layout()
{
	return M->layout;
}

WW3DSwapChainHandle RenderCore::Create_Additional_Swap_Chain(void*)
{
	// The game uses no additional swap chains; not implemented.
	Log_Missing("Create_Additional_Swap_Chain");
	return nullptr;
}

// ==================================================================================================
// 2. State -- kept as a shadow copy and evaluated only at draw time
// ==================================================================================================
//
// The shadow state starts at the classic fixed-function defaults. This matters because the
// redundancy cache in RenderDevice does not pass on a state that already looks "right" -- so the
// device must start from the same defaults the cache assumes.

static void Mat_Identity(WW3DMatrixRaw& m);		// defined in the drawing section

/// A float stored as a DWORD, as expected for FOGSTART, DEPTHBIAS etc.
static unsigned Float_As_State(float f)
{
	unsigned bits;
	::memcpy(&bits, &f, sizeof(bits));
	return bits;
}

void RenderCore::Init_Default_States()
{
	unsigned* rs = M->render_states;
	::ZeroMemory(rs, sizeof(M->render_states));
	M->state_keys_dirty = true;

	rs[WW3D_RS_ZENABLE]                 = 1;					// D3DZB_TRUE
	rs[WW3D_RS_FILLMODE]                = WW3D_FILL_SOLID;
	rs[WW3D_RS_SHADEMODE]               = 2;					// D3DSHADE_GOURAUD
	rs[WW3D_RS_ZWRITEENABLE]            = TRUE;
	rs[WW3D_RS_ALPHATESTENABLE]         = FALSE;
	rs[WW3D_RS_LASTPIXEL]               = TRUE;
	rs[WW3D_RS_SRCBLEND]                = WW3D_BLEND_ONE;
	rs[WW3D_RS_DESTBLEND]               = WW3D_BLEND_ZERO;
	rs[WW3D_RS_CULLMODE]                = WW3D_CULL_CCW;
	rs[WW3D_RS_ZFUNC]                   = WW3D_CMP_LESSEQUAL;
	rs[WW3D_RS_ALPHAREF]                = 0;
	rs[WW3D_RS_ALPHAFUNC]               = WW3D_CMP_ALWAYS;
	rs[WW3D_RS_DITHERENABLE]            = FALSE;
	rs[WW3D_RS_ALPHABLENDENABLE]        = FALSE;
	rs[WW3D_RS_FOGENABLE]               = FALSE;
	rs[WW3D_RS_SPECULARENABLE]          = FALSE;
	rs[WW3D_RS_FOGCOLOR]                = 0;
	rs[WW3D_RS_FOGTABLEMODE]            = WW3D_FOG_NONE;
	rs[WW3D_RS_FOGSTART]                = Float_As_State(0.0f);
	rs[WW3D_RS_FOGEND]                  = Float_As_State(1.0f);
	rs[WW3D_RS_FOGDENSITY]              = Float_As_State(1.0f);
	rs[WW3D_RS_RANGEFOGENABLE]          = FALSE;
	rs[WW3D_RS_STENCILENABLE]           = FALSE;
	rs[WW3D_RS_STENCILFAIL]             = WW3D_STENCILOP_KEEP;
	rs[WW3D_RS_STENCILZFAIL]            = WW3D_STENCILOP_KEEP;
	rs[WW3D_RS_STENCILPASS]             = WW3D_STENCILOP_KEEP;
	rs[WW3D_RS_STENCILFUNC]             = WW3D_CMP_ALWAYS;
	rs[WW3D_RS_STENCILREF]              = 0;
	rs[WW3D_RS_STENCILMASK]             = 0xFFFFFFFF;
	rs[WW3D_RS_STENCILWRITEMASK]        = 0xFFFFFFFF;
	rs[WW3D_RS_TEXTUREFACTOR]           = 0xFFFFFFFF;
	rs[WW3D_RS_CLIPPING]                = TRUE;
	rs[WW3D_RS_LIGHTING]                = TRUE;
	rs[WW3D_RS_AMBIENT]                 = 0;
	rs[WW3D_RS_FOGVERTEXMODE]           = WW3D_FOG_NONE;
	rs[WW3D_RS_COLORVERTEX]             = TRUE;
	rs[WW3D_RS_LOCALVIEWER]             = TRUE;
	rs[WW3D_RS_NORMALIZENORMALS]        = FALSE;
	rs[WW3D_RS_DIFFUSEMATERIALSOURCE]   = WW3D_MCS_COLOR1;
	rs[WW3D_RS_SPECULARMATERIALSOURCE]  = WW3D_MCS_COLOR2;
	rs[WW3D_RS_AMBIENTMATERIALSOURCE]   = WW3D_MCS_MATERIAL;
	rs[WW3D_RS_EMISSIVEMATERIALSOURCE]  = WW3D_MCS_MATERIAL;
	rs[WW3D_RS_VERTEXBLEND]             = 0;					// D3DVBF_DISABLE
	rs[WW3D_RS_CLIPPLANEENABLE]         = 0;
	rs[WW3D_RS_POINTSIZE]               = Float_As_State(1.0f);
	rs[WW3D_RS_MULTISAMPLEANTIALIAS]    = TRUE;
	rs[WW3D_RS_MULTISAMPLEMASK]         = 0xFFFFFFFF;
	rs[WW3D_RS_COLORWRITEENABLE]        = 0x0000000F;
	rs[WW3D_RS_BLENDOP]                 = WW3D_BLENDOP_ADD;
	rs[WW3D_RS_SLOPESCALEDEPTHBIAS]     = Float_As_State(0.0f);
	rs[WW3D_RS_DEPTHBIAS]               = Float_As_State(0.0f);

	::ZeroMemory(M->stage_states, sizeof(M->stage_states));
	for (unsigned stage = 0; stage < 8; ++stage) {
		unsigned* ts = M->stage_states[stage];
		ts[WW3D_TSS_COLOROP]   = (stage == 0) ? WW3D_TOP_MODULATE   : WW3D_TOP_DISABLE;
		ts[WW3D_TSS_ALPHAOP]   = (stage == 0) ? WW3D_TOP_SELECTARG1 : WW3D_TOP_DISABLE;
		ts[WW3D_TSS_COLORARG1] = WW3D_TA_TEXTURE;
		ts[WW3D_TSS_COLORARG2] = WW3D_TA_CURRENT;
		ts[WW3D_TSS_COLORARG0] = WW3D_TA_CURRENT;
		ts[WW3D_TSS_ALPHAARG1] = WW3D_TA_TEXTURE;
		ts[WW3D_TSS_ALPHAARG2] = WW3D_TA_CURRENT;
		ts[WW3D_TSS_ALPHAARG0] = WW3D_TA_CURRENT;
		ts[WW3D_TSS_TEXCOORDINDEX] = stage;
		ts[WW3D_TSS_TEXTURETRANSFORMFLAGS] = WW3D_TTFF_DISABLE;
		ts[WW3D_TSS_RESULTARG] = WW3D_TA_CURRENT;
	}

	::ZeroMemory(M->sampler_states, sizeof(M->sampler_states));
	for (unsigned slot = 0; slot < 8; ++slot) {
		unsigned* ss = M->sampler_states[slot];
		ss[WW3D_SAMP_ADDRESSU]     = WW3D_TADDRESS_WRAP;
		ss[WW3D_SAMP_ADDRESSV]     = WW3D_TADDRESS_WRAP;
		ss[WW3D_SAMP_ADDRESSW]     = WW3D_TADDRESS_WRAP;
		ss[WW3D_SAMP_MAGFILTER]    = WW3D_TEXF_POINT;
		ss[WW3D_SAMP_MINFILTER]    = WW3D_TEXF_POINT;
		ss[WW3D_SAMP_MIPFILTER]    = WW3D_TEXF_NONE;
		ss[WW3D_SAMP_MIPMAPLODBIAS] = Float_As_State(0.0f);
		ss[WW3D_SAMP_MAXMIPLEVEL]  = 0;
		ss[WW3D_SAMP_MAXANISOTROPY] = 1;
	}

	Mat_Identity(M->world_transform);
	for (unsigned i = 0; i < 24; ++i) Mat_Identity(M->transforms[i]);

	::ZeroMemory(&M->material, sizeof(M->material));
	::ZeroMemory(M->lights, sizeof(M->lights));
	::ZeroMemory(M->light_enabled, sizeof(M->light_enabled));

	M->frame_cb_dirty = M->object_cb_dirty = true;
	M->piece_vs_dirty = M->piece_ps_dirty = true;
}

void RenderCore::Set_Render_State(WW3DRenderStateType state, unsigned value)
{
	if (state >= RENDER_STATE_COUNT) return;
	if (M->render_states[state] == value) return;
	M->render_states[state] = value;
	// Fog colour and range live in the frame constant buffer, the states Fill_Object_Constants reads in the
	// object buffer, the rest in state objects (looked up again at the next draw).
	switch (state) {
		case WW3D_RS_FOGCOLOR: case WW3D_RS_FOGSTART: case WW3D_RS_FOGEND: case WW3D_RS_FOGENABLE:
			M->frame_cb_dirty = true;
			break;
		case WW3D_RS_AMBIENT: case WW3D_RS_TEXTUREFACTOR: case WW3D_RS_ALPHAREF: case WW3D_RS_ALPHAFUNC:
		case WW3D_RS_ALPHATESTENABLE: case WW3D_RS_LIGHTING: case WW3D_RS_SPECULARENABLE: case WW3D_RS_LOCALVIEWER:
		case WW3D_RS_COLORVERTEX: case WW3D_RS_NORMALIZENORMALS: case WW3D_RS_DIFFUSEMATERIALSOURCE:
		case WW3D_RS_AMBIENTMATERIALSOURCE: case WW3D_RS_EMISSIVEMATERIALSOURCE: case WW3D_RS_SPECULARMATERIALSOURCE:
			M->object_cb_dirty = true;
			break;
		default:
			M->state_keys_dirty = true;
			// With a float scene target the object buffer says whether the draw blends (Scene_Out).
			if (state == WW3D_RS_ALPHABLENDENABLE && M->scene_format != GPU_FORMAT_B8G8R8A8_UNORM)
				M->object_cb_dirty = true;
			break;
	}
}

void RenderCore::Get_Render_State(WW3DRenderStateType state, unsigned* value)
{
	if (value) *value = (state < RENDER_STATE_COUNT) ? M->render_states[state] : 0;
}

void RenderCore::Set_Texture_Stage_State(unsigned stage, WW3DTextureStageStateType state, unsigned value)
{
	if (stage >= 8 || state >= 33) return;
	if (M->stage_states[stage][state] == value) return;
	M->stage_states[stage][state] = value;
	if (stage < FFP_MAX_STAGES) M->object_cb_dirty = true;
}

void RenderCore::Set_Sampler_State(unsigned stage, WW3DSamplerStateType state, unsigned value)
{
	if (stage < 8 && state < RENDER_SAMPLER_STATE_COUNT) M->sampler_states[stage][state] = value;
}

void RenderCore::Set_Texture(unsigned stage, WW3DTextureHandle texture)
{
	if (stage >= 8) return;
	// Whether a slot is bound is part of the object constants (bump_scale.w), so is its team colour.
	if (stage < FFP_MAX_STAGES && ((M->textures[stage] == nullptr) != (texture == nullptr) ||
		(M->textures[stage] ? M->textures[stage]->team_color : 0u) != (texture ? texture->team_color : 0u))) M->object_cb_dirty = true;
	M->textures[stage] = texture;
}

void RenderCore::Set_Material(const WW3DMaterial* material)
{
	if (!material) return;
	M->material = *material;
	M->object_cb_dirty = true;
}

void RenderCore::Set_Light(unsigned index, const WW3DLight* light)
{
	if (index >= 8) return;
	M->light_enabled[index] = (light != nullptr);
	if (light) M->lights[index] = *light;
	if (index < FFP_MAX_LIGHTS) M->object_cb_dirty = true;
}

void RenderCore::Set_Clip_Plane(unsigned, const float*)
{
	// Unused: the calls in the water code are commented out.
	Log_Missing("Set_Clip_Plane");
}

void RenderCore::Set_Transform(WW3DTransformType transform, const WW3DMatrixRaw* m)
{
	if (!m) return;
	if (transform == WW3D_TS_WORLD) {
		M->world_transform = *m;
		M->object_cb_dirty = true;
	}
	else if (transform < 24) {
		M->transforms[transform] = *m;
		// View and projection go into both buffers (worldView/worldViewProj depend on them).
		if (transform == WW3D_TS_VIEW || transform == WW3D_TS_PROJECTION) M->frame_cb_dirty = true;
		M->object_cb_dirty = true;
	}
}

void RenderCore::Get_Transform(WW3DTransformType transform, WW3DMatrixRaw* m)
{
	if (!m) return;
	if (transform == WW3D_TS_WORLD) *m = M->world_transform;
	else if (transform < 24)        *m = M->transforms[transform];
	else                            ::ZeroMemory(m, sizeof(WW3DMatrixRaw));
}

// ==================================================================================================
// 3. Drawing (fixed-function emulation)
// ==================================================================================================

void RenderCore::Set_Stream_Source(unsigned stream, WW3DVertexBufferHandle vb, unsigned offset, unsigned stride)
{
	if (stream != 0) return;
	M->stream_vb     = vb;
	M->stream_offset = offset;
	M->stream_stride = stride;
	if (M->context) {
		GpuBuffer* buffer = vb ? vb->gpu : nullptr;
		UINT o = offset, s = stride;
		M->context->IASetVertexBuffers(0, 1, &buffer, &s, &o);
		number_of_API_calls++;
	}
}

void RenderCore::Set_Indices(WW3DIndexBufferHandle ib)
{
	M->indices = ib;
	if (M->context) {
		M->context->IASetIndexBuffer(ib ? ib->gpu : nullptr, GPU_FORMAT_R16_UINT, 0);
		number_of_API_calls++;
	}
}

// --- Matrix helpers -------------------------------------------------------------------------------
// All matrices are in D3D layout (row vectors, translation in row 4), exactly as
// RenderDevice::_Set_Device_Transform delivers them. Nothing is transposed.

static void Mat_Identity(WW3DMatrixRaw& m)
{
	::ZeroMemory(&m, sizeof(m));
	m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
}

/// Normal matrix for a world matrix: (L^-1)^T of the linear 3x3 part, i.e. the
/// cofactor matrix divided by the determinant. The rest stays identity.
static void Mat_Normal_Matrix(const WW3DMatrixRaw& w, WW3DMatrixRaw& out)
{
	const float a = w.m[0][0], b = w.m[0][1], c = w.m[0][2];
	const float d = w.m[1][0], e = w.m[1][1], f = w.m[1][2];
	const float g = w.m[2][0], h = w.m[2][1], i = w.m[2][2];

	const float c00 =  (e * i - f * h), c01 = -(d * i - f * g), c02 =  (d * h - e * g);
	const float c10 = -(b * i - c * h), c11 =  (a * i - c * g), c12 = -(a * h - b * g);
	const float c20 =  (b * f - c * e), c21 = -(a * f - c * d), c22 =  (a * e - b * d);

	const float det = a * c00 + b * c01 + c * c02;
	const float inv = (det > 1e-12f || det < -1e-12f) ? (1.0f / det) : 1.0f;

	Mat_Identity(out);
	out.m[0][0] = c00 * inv; out.m[0][1] = c01 * inv; out.m[0][2] = c02 * inv;
	out.m[1][0] = c10 * inv; out.m[1][1] = c11 * inv; out.m[1][2] = c12 * inv;
	out.m[2][0] = c20 * inv; out.m[2][1] = c21 * inv; out.m[2][2] = c22 * inv;
}

/// Camera position in world space from the view matrix. For p * V = 0, c = -t * L^-1; the
/// engine's view matrices are rigid, so L^-1 = L^T and c_i = -dot(t, row i of L).
static void Mat_Camera_Position(const WW3DMatrixRaw& v, float* out3)
{
	const float tx = v.m[3][0], ty = v.m[3][1], tz = v.m[3][2];
	for (int i = 0; i < 3; ++i) {
		out3[i] = -(tx * v.m[i][0] + ty * v.m[i][1] + tz * v.m[i][2]);
	}
}

/// A float stored as a DWORD (render state convention).
static inline float State_As_Float(unsigned bits)
{
	float f;
	::memcpy(&f, &bits, sizeof(f));
	return f;
}

static inline void Copy_Color(const WW3DColorValue& src, FFPFloat4& dst)
{
	dst.x = src.r; dst.y = src.g; dst.z = src.b; dst.w = src.a;
}

// --- Uber shader sources --------------------------------------------------------------------------

// Like the other HLSL shaders: Data/Shaders is copied to <exe>/Shaders at build time,
// so the FileFactory looks under "shaders\\...".
constexpr char FFP_SHADER_DIRECTORY[] = "shaders\\sm4\\";

/// Loads a file through the FileFactory and returns it null-terminated (caller frees).
char* Load_Shader_Source(const char* name, unsigned* length)
{
	if (length) *length = 0;
	StringClass path(FFP_SHADER_DIRECTORY, true);
	path += name;

	FileClass* file = _TheFileFactory->Get_File(path);
	if (!file) return nullptr;

	char* buffer = nullptr;
	if (file->Is_Available() && file->Open(FileClass::READ)) {
		const int size = file->Size();
		if (size > 0) {
			buffer = W3DNEWARRAY char[size + 1];
			const int got = file->Read(buffer, size);
			buffer[(got > 0) ? got : 0] = 0;
			if (length) *length = static_cast<unsigned>((got > 0) ? got : 0);
		}
		file->Close();
	}
	if (!buffer) {
		WWDEBUG_SAY(("Render: shader source '%s' not readable\n", path.Peek_Buffer()));
	}
	_TheFileFactory->Return_File(file);
	return buffer;
}

HRESULT RenderShaderInclude::Open(const char* file_name, const void** data, UINT* bytes)
{
	unsigned length = 0;
	char* source = Load_Shader_Source(file_name, &length);
	if (!source) return E_FAIL;
	*data  = source;
	*bytes = length;
	return S_OK;
}

void RenderShaderInclude::Close(const void* data)
{
	delete[] (const char*)data;
}

// The device compiles the shaders (D3D11: d3dcompiler); set while a device exists.
static GpuDevice* Shader_Device = nullptr;

void Set_Shader_Compile_Device(GpuDevice* device)
{
	Shader_Device = device;
}

// --- Shader cache ---------------------------------------------------------------------------------
// D3DCompile needs about 330 ms for ffp.ps.hlsl and 70 to 100 ms per vertex permutation, which adds
// up to a stall of almost a second at game start. So the compiled bytecode is kept under
// %LOCALAPPDATA%\CnCGenerals\ShaderCache. The key is a hash over the preprocessed source (so includes
// and macros are covered), the profile and the compiler settings -- a changed shader file gives a new
// entry. Every hit refreshes the file's timestamp; on the first access per game start
// Shader_Cache_Prune cleans up (long unused entries, size limit, leftover temp files).
// Any read or write error falls back to normal compilation.

static const UINT FFP_SHADER_FLAGS = GPU_COMPILE_OPTIMIZATION_LEVEL3;
static const unsigned SHADER_CACHE_MAGIC = 0x31435357;	// "WSC1"
static const unsigned SHADER_CACHE_MAX_AGE_DAYS = 30;			///< delete unused entries after this
static const uint64_t SHADER_CACHE_MAX_BYTES = 16u << 20;		///< above this, oldest first
static const uint64_t FILETIME_PER_DAY = 24ull * 60 * 60 * 10000000;

static uint64_t Shader_Hash(uint64_t hash, const void* data, size_t size)
{
	const unsigned char* p = static_cast<const unsigned char*>(data);
	for (size_t i = 0; i < size; ++i) {
		hash ^= p[i];
		hash *= 0x100000001B3ull;
	}
	return hash;
}

static uint64_t Filetime_Value(const FILETIME& time)
{
	return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

/// Deletes, in the directory (with trailing backslash), leftover temp files, entries not used for
/// SHADER_CACHE_MAX_AGE_DAYS and -- while the rest is larger than SHADER_CACHE_MAX_BYTES --
/// the least recently used entries.
static void Shader_Cache_Prune(const char* directory)
{
	struct Entry
	{
		uint64_t time;
		uint64_t size;
		char name[MAX_PATH];
	};
	std::vector<Entry> entries;

	FILETIME now_ft;
	::GetSystemTimeAsFileTime(&now_ft);
	const uint64_t now = Filetime_Value(now_ft);
	const uint64_t max_age = SHADER_CACHE_MAX_AGE_DAYS * FILETIME_PER_DAY;

	char pattern[MAX_PATH];
	char path[MAX_PATH];
	if (_snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%s*", directory) < 0) return;
	WIN32_FIND_DATAA found;
	HANDLE find = ::FindFirstFileA(pattern, &found);
	if (find == INVALID_HANDLE_VALUE) return;
	do {
		if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		const size_t len = strlen(found.cFileName);
		const bool is_entry = len > 4 && _stricmp(found.cFileName + len - 4, ".cso") == 0;
		const bool is_temp  = len > 4 && _stricmp(found.cFileName + len - 4, ".tmp") == 0;
		if (!is_entry && !is_temp) continue;		// foreign files stay untouched
		const uint64_t time = Filetime_Value(found.ftLastWriteTime);
		const uint64_t age = (now > time) ? now - time : 0;
		if (_snprintf_s(path, sizeof(path), _TRUNCATE, "%s%s", directory, found.cFileName) < 0) continue;
		// Temp files of a running game are at most seconds old.
		if ((is_temp && age > FILETIME_PER_DAY) || (is_entry && age > max_age)) {
			::DeleteFileA(path);
			continue;
		}
		if (!is_entry) continue;
		Entry entry;
		entry.time = time;
		entry.size = (static_cast<uint64_t>(found.nFileSizeHigh) << 32) | found.nFileSizeLow;
		strcpy_s(entry.name, sizeof(entry.name), path);
		entries.push_back(entry);
	} while (::FindNextFileA(find, &found));
	::FindClose(find);

	uint64_t total = 0;
	for (size_t i = 0; i < entries.size(); ++i) total += entries[i].size;
	if (total <= SHADER_CACHE_MAX_BYTES) return;
	std::sort(entries.begin(), entries.end(),
			  [](const Entry& a, const Entry& b) { return a.time < b.time; });
	for (size_t i = 0; i < entries.size() && total > SHADER_CACHE_MAX_BYTES; ++i) {
		if (::DeleteFileA(entries[i].name)) total -= entries[i].size;
	}
}

/// Cache directory with trailing backslash, created on demand.
/// Empty if there is none.
static const char* Shader_Cache_Directory()
{
	static char directory[MAX_PATH] = "";
	static bool resolved = false;
	if (resolved) return directory;
	resolved = true;

	char base[MAX_PATH];
	const DWORD len = ::GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof(base));
	if (len == 0 || len >= sizeof(base)) return directory;
	char path[MAX_PATH];
	if (_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\CnCGenerals", base) < 0) return directory;
	::CreateDirectoryA(path, nullptr);
	if (_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\CnCGenerals\\ShaderCache", base) < 0) return directory;
	::CreateDirectoryA(path, nullptr);
	const DWORD attributes = ::GetFileAttributesA(path);
	if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return directory;
	if (_snprintf_s(directory, sizeof(directory), _TRUNCATE, "%s\\", path) < 0) {
		directory[0] = 0;
		return directory;
	}
	Shader_Cache_Prune(directory);
	return directory;
}

/// Shader key; 0 = not cacheable (preprocessing failed).
static uint64_t Shader_Cache_Key(const char* source, unsigned length, const char* debug_name,
								 const GPU_SHADER_MACRO* macros, const char* profile)
{
	RenderShaderInclude includes;
	GpuBlob* text = nullptr;
	if (!Shader_Device) return 0;
	const HRESULT hr = Shader_Device->PreprocessShader(source, length, debug_name, macros, &includes, &text);
	if (FAILED(hr) || !text) {
		if (text) text->Release();
		return 0;
	}
	uint64_t hash = 0xCBF29CE484222325ull;
	hash = Shader_Hash(hash, text->GetBufferPointer(), text->GetBufferSize());
	hash = Shader_Hash(hash, profile, strlen(profile) + 1);
	hash = Shader_Hash(hash, &FFP_SHADER_FLAGS, sizeof(FFP_SHADER_FLAGS));
	const unsigned compiler = Shader_Device->ShaderCompilerVersion();
	hash = Shader_Hash(hash, &compiler, sizeof(compiler));
	// DXBC keys stay as they were; other code formats get their own entries.
	const char* format = Shader_Device->ShaderFormat();
	if (strcmp(format, "dxbc") != 0) hash = Shader_Hash(hash, format, strlen(format) + 1);
	text->Release();
	return hash ? hash : 1;
}

static bool Shader_Cache_Path(uint64_t key, char* path, size_t size)
{
	const char* directory = Shader_Cache_Directory();
	if (!*directory) return false;
	return _snprintf_s(path, size, _TRUNCATE, "%s%016llx.cso", directory, static_cast<unsigned long long>(key)) > 0;
}

struct ShaderCacheHeader
{
	unsigned magic;
	unsigned size;
	uint64_t key;
};

static bool Shader_Cache_Load(uint64_t key, GpuBlob** code)
{
	char path[MAX_PATH];
	if (!key || !Shader_Cache_Path(key, path, sizeof(path))) return false;
	FILE* file = nullptr;
	if (fopen_s(&file, path, "rb") != 0 || !file) return false;

	bool ok = false;
	ShaderCacheHeader header;
	if (fread(&header, sizeof(header), 1, file) == 1 && header.magic == SHADER_CACHE_MAGIC
		&& header.key == key && header.size > 0 && header.size < (16u << 20)
		&& (*code = Gpu_Create_Blob(header.size)) != nullptr) {
		if (fread((*code)->GetBufferPointer(), header.size, 1, file) == 1) {
			ok = true;
		} else {
			(*code)->Release();
			*code = nullptr;
		}
	}
	fclose(file);
	if (!ok) return false;
	// Used: refresh the timestamp so Shader_Cache_Prune keeps the entry.
	const HANDLE handle = ::CreateFileA(path, FILE_WRITE_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (handle != INVALID_HANDLE_VALUE) {
		FILETIME now;
		::GetSystemTimeAsFileTime(&now);
		::SetFileTime(handle, nullptr, nullptr, &now);
		::CloseHandle(handle);
	}
	return ok;
}

static void Shader_Cache_Store(uint64_t key, GpuBlob* code)
{
	char path[MAX_PATH];
	char temp[MAX_PATH];
	if (!key || !code || !Shader_Cache_Path(key, path, sizeof(path))) return;
	// Write under a unique name first, then rename: if both games run at the same time,
	// neither sees a half-written file.
	if (_snprintf_s(temp, sizeof(temp), _TRUNCATE, "%s.%lu.tmp", path, ::GetCurrentProcessId()) < 0) return;
	FILE* file = nullptr;
	if (fopen_s(&file, temp, "wb") != 0 || !file) return;
	ShaderCacheHeader header;
	header.magic = SHADER_CACHE_MAGIC;
	header.size  = static_cast<unsigned>(code->GetBufferSize());
	header.key   = key;
	const bool ok = fwrite(&header, sizeof(header), 1, file) == 1
				 && fwrite(code->GetBufferPointer(), header.size, 1, file) == 1;
	if (fclose(file) != 0 || !ok || !::MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING)) {
		::DeleteFileA(temp);
	}
}

// Shaders that did not compile: the effect or draw path that needs one falls away without a picture of its own, so
// tests must see it in the release build too (RenderDevice::Get_Shader_Failures, line SHADERFEHLER).
static unsigned Shader_Failure_Count = 0;
static char Shader_Failure_Names[512];

static void Note_Shader_Failure(const char* debug_name, const char* profile)
{
	++Shader_Failure_Count;
	char entry[96];
	_snprintf_s(entry, sizeof(entry), _TRUNCATE, " %s(%s)", debug_name ? debug_name : "?", profile ? profile : "?");
	if (strstr(Shader_Failure_Names, entry)) return;
	const size_t used = strlen(Shader_Failure_Names), add = strlen(entry);
	if (used + add < sizeof(Shader_Failure_Names))
		memcpy(Shader_Failure_Names + used, entry, add + 1);
}

void RenderDevice::Get_Shader_Failures(char* text, unsigned size)
{
	if (!text || !size) return;
	text[0] = 0;
	if (Shader_Failure_Count)
		_snprintf_s(text, size, _TRUNCATE, "SHADERFEHLER %u%s", Shader_Failure_Count, Shader_Failure_Names);
}

HRESULT Compile_FFP_Shader(const char* source, unsigned length, const char* debug_name,
								  const GPU_SHADER_MACRO* macros, const char* profile, GpuBlob** code)
{
	const uint64_t key = Shader_Cache_Key(source, length, debug_name, macros, profile);
	if (Shader_Cache_Load(key, code)) return S_OK;

	if (!Shader_Device) return E_FAIL;
	RenderShaderInclude includes;
	GpuBlob* errors = nullptr;
	HRESULT hr = Shader_Device->CompileShader(source, length, debug_name, macros, &includes, "main", profile,
											  FFP_SHADER_FLAGS, code, &errors);
	if (errors) {
		WWDEBUG_SAY(("HLSL %s (%s): %s\n", debug_name, profile, (const char*)errors->GetBufferPointer()));
		errors->Release();
	}
	if (FAILED(hr)) {
		WWDEBUG_SAY(("HLSL %s (%s): D3DCompile failed, 0x%08X\n", debug_name, profile, (unsigned)hr));
		Note_Shader_Failure(debug_name, profile);
	}
	else Shader_Cache_Store(key, *code);
	return hr;
}

/// Permutation key from the vertex declaration (see FFP_VS_PERMUTATIONS).
static unsigned FFP_VS_Permutation(WW3DVertexDeclHandle decl)
{
	if (!decl) return 0;
	unsigned key = 0;
	unsigned texcoord_mask = 0;
	for (unsigned i = 0; i < decl->element_count; ++i) {
		const WW3DVertexElement& e = decl->elements[i];
		switch (e.Usage) {
		case WW3D_DECLUSAGE_NORMAL:
			key |= 1;
			break;
		case WW3D_DECLUSAGE_COLOR:
			if (e.UsageIndex == 0) key |= 2;
			else if (e.UsageIndex == 1) key |= 4;
			break;
		case WW3D_DECLUSAGE_TEXCOORD:
			if (e.UsageIndex < 4) texcoord_mask |= (1u << e.UsageIndex);
			break;
		default:
			break;
		}
	}
	unsigned num_texcoords = 0;
	for (unsigned i = 0; i < 4; ++i) {
		if (texcoord_mask & (1u << i)) num_texcoords = i + 1;
	}
	// The shader declares TEXCOORD0..n-1 without gaps; a gap would not be covered by the input
	// layout and would make CreateInputLayout fail.
	const unsigned contiguous = (1u << num_texcoords) - 1u;
	if (texcoord_mask != contiguous) {
		// Report once: no current declaration has such a gap, but it would make
		// CreateInputLayout fail (the shader declares TEXCOORD0..n-1).
		static bool reported = false;
		if (!reported) {
			reported = true;
			WWDEBUG_SAY(("Render: vertex declaration with a gap in the texture coordinates (0x%X)\n", texcoord_mask));
		}
	}
	return key | (num_texcoords << 3);
}

// --- Draw path resources --------------------------------------------------------------------------

bool RenderCore::Ensure_Draw_Resources()
{
	if (!M->device) return false;
	if (M->cb_frame) return true;

	Invalidate_Bindings(M);		// caches and buffers are recreated
	M->cb_frame     = Create_Constant_Buffer(M->device, sizeof(FFPFrameCB));
	M->cb_object    = Create_Constant_Buffer(M->device, sizeof(FFPObjectCB));
	M->cb_piece_vs = Create_Constant_Buffer(M->device, sizeof(FFPPieceVSCB));
	M->cb_piece_ps = Create_Constant_Buffer(M->device, sizeof(FFPPiecePSCB));
	M->tl_cb_off    = Create_Constant_Buffer(M->device, sizeof(FFPTerrainLightsCB));
	M->sk_cb_off    = Create_Constant_Buffer(M->device, sizeof(FFPSkinCB));
	M->sk_cb        = Create_Constant_Buffer(M->device, sizeof(FFPSkinCB));
	if (M->tl_cb_off && M->sk_cb_off && M->context) {
		static const FFPTerrainLightsCB zeros = {};
		Upload_Constant_Buffer(M->context, M->tl_cb_off, &zeros, sizeof(zeros));
		static const FFPSkinCB no_skin = {};
		Upload_Constant_Buffer(M->context, M->sk_cb_off, &no_skin, sizeof(no_skin));
	}
	M->sk_dirty = true;
	M->cbr_offset = 0;
	M->cbr_bound_mask = 0;
	if (M->cbr_context && !M->cbr_buffer) {
		GPU_BUFFER_DESC desc;
		::ZeroMemory(&desc, sizeof(desc));
		desc.ByteWidth      = CB_RING_BYTES;
		desc.Usage          = GPU_USAGE_DYNAMIC;
		desc.BindFlags      = GPU_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = GPU_CPU_ACCESS_WRITE;
		if (FAILED(M->device->CreateBuffer(&desc, nullptr, &M->cbr_buffer))) {
			M->cbr_buffer = nullptr;
			WWDEBUG_SAY(("Render: constant buffer ring cannot be created, buffers are mapped per draw\n"));
			M->cbr_context = nullptr;		// ring off; the context itself stays
		}
	}
	if (!M->cb_frame || !M->cb_object || !M->cb_piece_vs || !M->cb_piece_ps || !M->tl_cb_off || !M->sk_cb_off || !M->sk_cb) {
		WWDEBUG_SAY(("Render: cannot create constant buffer\n"));
		Release_Draw_Resources();
		return false;
	}

	// 1x1 white for empty texture slots, so the TEXTURE argument is always defined.
	const unsigned white = 0xFFFFFFFF;
	GPU_TEXTURE2D_DESC tex;
	::ZeroMemory(&tex, sizeof(tex));
	tex.Width = tex.Height = tex.MipLevels = tex.ArraySize = 1;
	tex.Format             = GPU_FORMAT_B8G8R8A8_UNORM;
	tex.SampleDesc.Count   = 1;
	tex.Usage              = GPU_USAGE_IMMUTABLE;
	tex.BindFlags          = GPU_BIND_SHADER_RESOURCE;
	GPU_SUBRESOURCE_DATA init;
	::ZeroMemory(&init, sizeof(init));
	init.pSysMem     = &white;
	init.SysMemPitch = sizeof(white);
	if (FAILED(M->device->CreateTexture2D(&tex, &init, &M->white_texture)) ||
		FAILED(M->device->CreateShaderResourceView(M->white_texture, nullptr, &M->white_srv))) {
		WWDEBUG_SAY(("Render: cannot create fallback texture\n"));
		Release_Draw_Resources();
		return false;
	}

	// Ring buffer for Draw_Primitive_UP.
	GPU_BUFFER_DESC ring;
	::ZeroMemory(&ring, sizeof(ring));
	ring.ByteWidth      = UP_RING_BYTES;
	ring.Usage          = GPU_USAGE_DYNAMIC;
	ring.BindFlags      = GPU_BIND_VERTEX_BUFFER;
	ring.CPUAccessFlags = GPU_CPU_ACCESS_WRITE;
	if (FAILED(M->device->CreateBuffer(&ring, nullptr, &M->up_ring))) {
		WWDEBUG_SAY(("Render: cannot create ring buffer for Draw_Primitive_UP\n"));
		Release_Draw_Resources();
		return false;
	}
	M->up_ring_offset = 0;

	M->frame_cb_dirty = M->object_cb_dirty = true;
	M->piece_vs_dirty = M->piece_ps_dirty = true;
	return true;
}

void RenderCore::Release_Draw_Resources()
{
	M->blend_cache.Log_Statistics("Blend");
	M->depth_cache.Log_Statistics("Depth/Stencil");
	M->raster_cache.Log_Statistics("Rasterizer");
	M->sampler_cache.Log_Statistics("Sampler");

	// Order: RenderDevice::Release_Device releases the vertex declarations (and with them their
	// input layouts) before the backend. So no layout entry points to the uber shader bytecode
	// released below any more.
	M->blend_cache.Release_All();
	M->depth_cache.Release_All();
	M->raster_cache.Release_All();
	M->sampler_cache.Release_All();
	Fx_Release_Resources();

	for (unsigned i = 0; i < FFP_VS_PERMUTATIONS; ++i) {
		if (M->ffp_vs[i]) { Release_Resource(M->ffp_vs[i]); M->ffp_vs[i] = nullptr; }
	}
	if (M->ffp_ps) { Release_Resource(M->ffp_ps); M->ffp_ps = nullptr; }

	delete[] M->ffp_vs_source; M->ffp_vs_source = nullptr; M->ffp_vs_source_len = 0;
	delete[] M->ffp_ps_source; M->ffp_ps_source = nullptr; M->ffp_ps_source_len = 0;
	M->ffp_load_failed = false;
	delete[] M->tr_heights; M->tr_heights = nullptr; M->tr_width = M->tr_height = 0;
	Gpu_Release(M->tn_atlas); M->tn_atlas_handle = nullptr;
	for (unsigned i = 0; i < FFP_VS_PERMUTATIONS; ++i) {
		if (M->tt_vs[i]) { Release_Resource(M->tt_vs[i]); M->tt_vs[i] = nullptr; }
	}
	M->tt_vs_failed = 0;
	Gpu_Release(M->tt_atlas); M->tt_atlas_handle = nullptr;
	delete[] M->tt_weights_cpu; M->tt_weights_cpu = nullptr;
	delete[] M->tt_mask_cpu; M->tt_mask_cpu = nullptr;
	M->tt_grid_w = M->tt_grid_h = 0;

	Gpu_Release(M->up_ring);
	M->up_ring_offset = 0;
	Gpu_Release(M->white_srv);
	Gpu_Release(M->white_texture);
	Gpu_Release(M->cb_piece_ps);
	Gpu_Release(M->cb_piece_vs);
	Gpu_Release(M->cb_object);
	Gpu_Release(M->cb_frame);
	Gpu_Release(M->cbr_buffer);
	M->cbr_offset = 0;
	M->cbr_bound_mask = 0;
	Gpu_Release(M->tl_cb_off);
	Gpu_Release(M->tl_cb);
	M->tl_mode = false;
	Gpu_Release(M->sk_cb_off);
	Gpu_Release(M->sk_cb);
	M->sk_count = 0;
	Pa_Release_Resources();
	Gpu_Release(M->mi_matrix_cb);
	Gpu_Release(M->mi_index_vb);
	M->mi_count = 0;
	M->mi_open = false;
	M->mi_failed = false;
}

WW3DVertexShaderHandle RenderCore::Get_FFP_Vertex_Shader(unsigned permutation)
{
	if (permutation >= FFP_VS_PERMUTATIONS) return nullptr;
	if (M->ffp_vs[permutation]) return M->ffp_vs[permutation];
	if (M->ffp_load_failed) return nullptr;

	if (!M->ffp_vs_source) {
		M->ffp_vs_source = Load_Shader_Source("ffp.vs.hlsl", &M->ffp_vs_source_len);
		if (!M->ffp_vs_source) { M->ffp_load_failed = true; return nullptr; }
	}

	char num_texcoords[2];
	num_texcoords[0] = static_cast<char>('0' + ((permutation >> 3) & 7));
	num_texcoords[1] = 0;

	GPU_SHADER_MACRO macros[7];
	unsigned n = 0;
	if (permutation & FFP_VS_INSTANCED) { macros[n].Name = "INSTANCED"; macros[n].Definition = "1"; ++n; }
	if (permutation & FFP_VS_PIXEL_LIGHT) { macros[n].Name = "PIXEL_LIGHT"; macros[n].Definition = "1"; ++n; }
	if (permutation & 1) { macros[n].Name = "HAS_NORMAL";   macros[n].Definition = "1"; ++n; }
	if (permutation & 2) { macros[n].Name = "HAS_DIFFUSE";  macros[n].Definition = "1"; ++n; }
	if (permutation & 4) { macros[n].Name = "HAS_SPECULAR"; macros[n].Definition = "1"; ++n; }
	macros[n].Name = "NUM_TEXCOORDS"; macros[n].Definition = num_texcoords; ++n;
	macros[n].Name = nullptr; macros[n].Definition = nullptr;

	GpuBlob* code = nullptr;
	if (FAILED(Compile_FFP_Shader(M->ffp_vs_source, M->ffp_vs_source_len, "ffp.vs.hlsl",
								  macros, "vs_4_0", &code))) {
		if (code) code->Release();
		// Without the instanced or the pixel light variant the plain ones still draw everything.
		if (permutation & FFP_VS_PIXEL_LIGHT) M->ffp_light_failed[0] = true;
		else if (permutation & FFP_VS_INSTANCED) M->mi_failed = true;
		else M->ffp_load_failed = true;
		return nullptr;
	}

	WW3DVertexShaderObject* obj = W3DNEW WW3DVertexShaderObject;
	::ZeroMemory(obj, sizeof(*obj));
	obj->refcount = 1;
	obj->bytecode = code;					// kept for the input layouts
	obj->shadow_kind = (permutation & FFP_VS_INSTANCED) ? 3u : 0u;
	if (FAILED(M->device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader))) {
		code->Release();
		delete obj;
		M->ffp_load_failed = true;
		return nullptr;
	}
	M->ffp_vs[permutation] = obj;
	return obj;
}

void* RenderCore::Get_Input_Layout(WW3DVertexDeclHandle decl, WW3DVertexShaderHandle vs)
{
	if (!decl || !vs || !vs->bytecode) return nullptr;
	const void* key = vs->bytecode->GetBufferPointer();

	for (unsigned i = 0; i < decl->layout_count; ++i) {
		if (decl->layouts[i].vs_bytecode == key) return decl->layouts[i].layout;
	}

	GPU_INPUT_ELEMENT_DESC elements[WW3D_MAX_DECL_LENGTH];
	unsigned n = 0;
	for (unsigned i = 0; i < decl->element_count && n < WW3D_MAX_DECL_LENGTH; ++i) {
		const WW3DVertexElement& e = decl->elements[i];
		const char* semantic = Render_Decl_Usage_To_Semantic(e.Usage);
		const GPU_FORMAT format = Render_Decl_Type_To_Gpu_Format(e.Type);
		if (!semantic || format == GPU_FORMAT_UNKNOWN) continue;
		elements[n].SemanticName         = semantic;
		elements[n].SemanticIndex        = e.UsageIndex;
		elements[n].Format               = format;
		elements[n].InputSlot            = e.Stream;
		elements[n].AlignedByteOffset    = e.Offset;
		elements[n].InputSlotClass       = GPU_INPUT_PER_VERTEX_DATA;
		elements[n].InstanceDataStepRate = 0;
		++n;
	}
	if (!n) return nullptr;
	if (vs->shadow_kind == 2) {
		// Instanced shadow casters: world-light matrix per instance on stream 1 (Sh_Render_Map).
		if (n + 4 > WW3D_MAX_DECL_LENGTH) return nullptr;
		for (unsigned row = 0; row < 4; ++row, ++n) {
			elements[n].SemanticName         = "WORLDLIGHT";
			elements[n].SemanticIndex        = row;
			elements[n].Format               = GPU_FORMAT_R32G32B32A32_FLOAT;
			elements[n].InputSlot            = 1;
			elements[n].AlignedByteOffset    = row * 16;
			elements[n].InputSlotClass       = GPU_INPUT_PER_INSTANCE_DATA;
			elements[n].InstanceDataStepRate = 1;
		}
	}
	else if (vs->shadow_kind == 3) {
		// Instanced meshes: the instance number on stream 1 (Mi_Flush).
		if (n + 1 > WW3D_MAX_DECL_LENGTH) return nullptr;
		elements[n].SemanticName         = "INSTANCEINDEX";
		elements[n].SemanticIndex        = 0;
		elements[n].Format               = GPU_FORMAT_R32_UINT;
		elements[n].InputSlot            = 1;
		elements[n].AlignedByteOffset    = 0;
		elements[n].InputSlotClass       = GPU_INPUT_PER_INSTANCE_DATA;
		elements[n].InstanceDataStepRate = 1;
		++n;
	}

	GpuInputLayout* layout = nullptr;
	const HRESULT hr = M->device->CreateInputLayout(elements, n, key, vs->bytecode->GetBufferSize(), &layout);
	if (FAILED(hr) || !layout) {
		WWDEBUG_SAY(("Render: CreateInputLayout failed (0x%08X, %u elements)\n", (unsigned)hr, n));
		return nullptr;
	}

	if (decl->layout_count < WW3D_MAX_INPUT_LAYOUTS) {
		decl->layouts[decl->layout_count].vs_bytecode = key;
		decl->layouts[decl->layout_count].layout      = layout;
		++decl->layout_count;
	}
	else {
		// Full: the layout is not cached and gets rebuilt next time. D3D11 returns the same
		// object for identical inputs, so nothing piles up.
		WWDEBUG_SAY(("Render: input layout cache of the declaration full\n"));
		layout->Release();
		return nullptr;
	}
	return layout;
}

// --- State objects --------------------------------------------------------------------------------

// A scene target without alpha (R11G11B10) keeps the destination alpha in scene_alpha. Draws that write only alpha
// (water depth code at the shore, alpha masks) are drawn into it, draws that blend with destination alpha read it
// as their source alpha; Scene_Out (common.hlsli) and the blend state (Find_State_Objects) do the rest.
void RenderCore::Sa_Select()
{
	Sa_Done();
	unsigned mode = 0;
	if (M->scene_alpha_rtv && !M->scene_swapped && M->bound_rtv == M->scene_rtv) {
		const unsigned* rs = M->render_states;
		const unsigned src = rs[WW3D_RS_SRCBLEND], dst = rs[WW3D_RS_DESTBLEND];
		if ((rs[WW3D_RS_COLORWRITEENABLE] & 0x0F) == WW3D_COLORWRITEENABLE_ALPHA)
			mode = 2;
		else if (rs[WW3D_RS_ALPHABLENDENABLE] &&
				 (src == WW3D_BLEND_DESTALPHA || src == WW3D_BLEND_INVDESTALPHA ||
				  dst == WW3D_BLEND_DESTALPHA || dst == WW3D_BLEND_INVDESTALPHA))
			mode = 1;
	}
	if (mode != M->sa_mode) {
		M->sa_mode = mode;
		M->state_keys_dirty = true;
		M->object_cb_dirty = true;
	}
}

void RenderCore::Sa_Bind()
{
	if (M->sa_mode == 2) {
		M->context->OMSetRenderTargets(1, &M->scene_alpha_rtv, M->bound_dsv);
		M->sa_bound = true;
		number_of_API_calls++;
	}
	else if (M->sa_mode == 1) {
		M->context->PSSetShaderResources(RENDER_SLOT_T_SCENE_ALPHA, 1, &M->scene_alpha_srv);
		M->sa_bound = true;
		number_of_API_calls++;
	}
}

void RenderCore::Sa_Done()
{
	if (!M->sa_bound) return;
	M->sa_bound = false;
	if (M->sa_mode == 2) {
		M->context->OMSetRenderTargets(1, &M->bound_rtv, M->bound_dsv);
	}
	else {
		GpuShaderResourceView* none = nullptr;
		M->context->PSSetShaderResources(RENDER_SLOT_T_SCENE_ALPHA, 1, &none);
	}
	number_of_API_calls++;
}

void RenderCore::Bind_State_Objects()
{
	if (M->state_keys_dirty) {
		Find_State_Objects();
		M->state_keys_dirty = false;
	}
	if (M->state_blend && M->state_blend != M->bound_blend) {
		M->context->OMSetBlendState(M->state_blend, nullptr, 0xFFFFFFFF);
		M->bound_blend = M->state_blend;
		number_of_API_calls++;
	}
	if (M->state_depth && (M->state_depth != M->bound_depth || M->state_stencil_ref != M->bound_stencil_ref)) {
		M->context->OMSetDepthStencilState(M->state_depth, M->state_stencil_ref);
		M->bound_depth = M->state_depth;
		M->bound_stencil_ref = M->state_stencil_ref;
		number_of_API_calls++;
	}
	if (M->state_raster && M->state_raster != M->bound_raster) {
		M->context->RSSetState(M->state_raster);
		M->bound_raster = M->state_raster;
		number_of_API_calls++;
	}
}

void RenderCore::Find_State_Objects()
{
	const unsigned* rs = M->render_states;
	RenderStateKey key;
	// Blend
	Key_Clear(key);
	key.w[0] = rs[WW3D_RS_ALPHABLENDENABLE];
	key.w[1] = rs[WW3D_RS_SRCBLEND];
	key.w[2] = rs[WW3D_RS_DESTBLEND];
	key.w[3] = rs[WW3D_RS_BLENDOP];
	key.w[4] = rs[WW3D_RS_COLORWRITEENABLE];
	key.w[5] = M->sa_mode;
	GpuBlendState* blend = M->blend_cache.Find(key);
	if (!blend) {
		GPU_BLEND_DESC desc;
		Render_Build_Blend_Desc(rs, desc);
		GPU_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];
		if (M->sa_mode == 2) {
			// Only alpha, written as red into the alpha target (Scene_Out moves it there).
			rt.RenderTargetWriteMask = GPU_COLOR_WRITE_ENABLE_RED;
		}
		else if (M->sa_mode == 1) {
			// Destination alpha comes from the alpha target as the source alpha (Scene_Out).
			GPU_BLEND* f[4] = { &rt.SrcBlend, &rt.DestBlend, &rt.SrcBlendAlpha, &rt.DestBlendAlpha };
			for (GPU_BLEND* b : f) {
				if (*b == GPU_BLEND_SRC_ALPHA || *b == GPU_BLEND_INV_SRC_ALPHA)
					Log_Missing("blending with source and destination alpha on a scene target without alpha");
				else if (*b == GPU_BLEND_DEST_ALPHA)		*b = GPU_BLEND_SRC_ALPHA;
				else if (*b == GPU_BLEND_INV_DEST_ALPHA)	*b = GPU_BLEND_INV_SRC_ALPHA;
			}
		}
		if (SUCCEEDED(M->device->CreateBlendState(&desc, &blend))) M->blend_cache.Insert(key, blend);
	}
	M->state_blend = blend;

	// Depth and stencil
	Key_Clear(key);
	key.w[0] = rs[WW3D_RS_ZENABLE];
	key.w[1] = rs[WW3D_RS_ZWRITEENABLE];
	key.w[2] = rs[WW3D_RS_ZFUNC];
	key.w[3] = rs[WW3D_RS_STENCILENABLE];
	key.w[4] = rs[WW3D_RS_STENCILFAIL];
	key.w[5] = rs[WW3D_RS_STENCILZFAIL];
	key.w[6] = rs[WW3D_RS_STENCILPASS];
	key.w[7] = rs[WW3D_RS_STENCILFUNC];
	key.w[8] = rs[WW3D_RS_STENCILMASK];
	key.w[9] = rs[WW3D_RS_STENCILWRITEMASK];
	GpuDepthStencilState* depth = M->depth_cache.Find(key);
	if (!depth) {
		GPU_DEPTH_STENCIL_DESC desc;
		Render_Build_Depth_Stencil_Desc(rs, desc);
		if (SUCCEEDED(M->device->CreateDepthStencilState(&desc, &depth))) M->depth_cache.Insert(key, depth);
	}
	// The scene target's stencil is 8 bits wide (D24S8); the upper bits of the reference are
	// dropped. The shadow paths pass 0x80808080 and mean 0x80.
	const unsigned stencil_ref = rs[WW3D_RS_STENCILREF] & 0xFF;
	M->state_depth = depth;
	M->state_stencil_ref = stencil_ref;

	// Rasterizer
	Key_Clear(key);
	key.w[0] = rs[WW3D_RS_CULLMODE];
	key.w[1] = rs[WW3D_RS_FILLMODE];
	key.w[2] = rs[WW3D_RS_DEPTHBIAS];
	key.w[3] = rs[WW3D_RS_SLOPESCALEDEPTHBIAS];
	key.w[4] = rs[WW3D_RS_MULTISAMPLEANTIALIAS];
	key.w[5] = M->sample_count;
	GpuRasterizerState* raster = M->raster_cache.Find(key);
	if (!raster) {
		GPU_RASTERIZER_DESC desc;
		Render_Build_Rasterizer_Desc(rs, desc, M->sample_count > 1);
		if (SUCCEEDED(M->device->CreateRasterizerState(&desc, &raster))) M->raster_cache.Insert(key, raster);
	}
	M->state_raster = raster;
}

void RenderCore::Bind_Textures()
{
	GpuShaderResourceView* srvs[8];
	GpuSamplerState*       samplers[8];
	bool srv_changed = false, sampler_changed = false;

	// Scaled scene: the samplers get the mip LOD bias of the scale (section 15); rebuilt when it changes.
	const unsigned rs_bias = Rs_Sampler_Bias_Bits();
	if (rs_bias != M->rs_bound_bias) {
		M->rs_bound_bias = rs_bias;
		for (unsigned i = 0; i < 8; ++i) M->slot_sampler_valid[i] = false;
	}

	// D3D11 cannot sample a texture that is the current render target; the driver would null the
	// input and the debug layer would report "still bound on output" (ID 7). Do the same here without
	// the warning (the slot reads 0), and log size and slot once so the caller can be found.
	const WW3DTextureObject* target_texture = M->current_render_target ? M->current_render_target->owner : nullptr;
	for (unsigned slot = 0; slot < 8; ++slot) {
		WW3DTextureHandle texture = M->textures[slot];
		srvs[slot] = (texture && texture->srv) ? texture->srv : M->white_srv;
		if (texture && texture == target_texture) {
			static bool logged = false;
			if (!logged) {
				logged = true;
				WWDEBUG_SAY(("Render: texture %ux%u in slot %u is also the render target -- slot left empty\n",
							 texture->width, texture->height, slot));
			}
			srvs[slot] = nullptr;
		}
		if (srvs[slot] != M->bound_srv[slot]) srv_changed = true;

		// Look up the sampler again only if this stage's states changed.
		const unsigned* ss = &M->sampler_states[slot][1];
		if (!M->slot_sampler_valid[slot] || ::memcmp(M->slot_sampler_key[slot], ss, sizeof(M->slot_sampler_key[slot])) != 0) {
			RenderStateKey key;
			Key_Clear(key);
			for (unsigned i = 0; i < 10; ++i) key.w[i] = ss[i];
			key.w[10] = rs_bias;
			GpuSamplerState* sampler = M->sampler_cache.Find(key);
			if (!sampler) {
				GPU_SAMPLER_DESC desc;
				Render_Build_Sampler_Desc(M->sampler_states[slot], desc);
				if (rs_bias) desc.MipLODBias += M->rs_bias;
				if (SUCCEEDED(M->device->CreateSamplerState(&desc, &sampler))) M->sampler_cache.Insert(key, sampler);
			}
			::memcpy(M->slot_sampler_key[slot], ss, sizeof(M->slot_sampler_key[slot]));
			M->slot_sampler[slot] = sampler;
			M->slot_sampler_valid[slot] = true;
		}
		samplers[slot] = M->slot_sampler[slot];
		if (samplers[slot] != M->bound_sampler[slot]) sampler_changed = true;
	}

	if (srv_changed) {
		M->context->PSSetShaderResources(0, 8, srvs);
		::memcpy(M->bound_srv, srvs, sizeof(srvs));
		number_of_API_calls++;
	}
	if (sampler_changed) {
		M->context->PSSetSamplers(0, 8, samplers);
		::memcpy(M->bound_sampler, samplers, sizeof(samplers));
		number_of_API_calls++;
	}
}

// --- Constant buffers -----------------------------------------------------------------------------

void RenderCore::Fill_Frame_Constants()
{
	FFPFrameCB& cb = M->frame_cb;
	cb.view = M->transforms[WW3D_TS_VIEW];
	cb.proj = Taa_Jitter_Projection();
	Mat_Multiply(cb.view, cb.proj, cb.viewProj);

	Render_Color_To_Float4(M->render_states[WW3D_RS_FOGCOLOR], &cb.fogColor.x);

	cb.fogParams.x = State_As_Float(M->render_states[WW3D_RS_FOGSTART]);
	cb.fogParams.y = State_As_Float(M->render_states[WW3D_RS_FOGEND]);
	cb.fogParams.z = M->render_states[WW3D_RS_FOGENABLE] ? 1.0f : 0.0f;
	cb.fogParams.w = 0.0f;

	Mat_Camera_Position(cb.view, &cb.cameraPosWorld.x);
	cb.cameraPosWorld.w = 1.0f;

	// The scene light (ambient, sunRay, sunDiffuse) is written by Set_Terrain_Lighting.
	const WW3DViewport& vp = M->viewport;
	Fx_Set_Float4(cb.viewport, static_cast<float>(vp.X), static_cast<float>(vp.Y), static_cast<float>(vp.Width),
		static_cast<float>(vp.Height));
	Fx_Set_Float4(cb.depthRange, vp.MinZ, vp.MaxZ, (vp.MaxZ > vp.MinZ) ? 1.0f / (vp.MaxZ - vp.MinZ) : 1.0f, 0.0f);
	Fx_Set_Float4(cb.hdr, RenderDevice::Get_Effect_Settings().hdr_additive, 0.0f, 0.0f, 0.0f);
}

void RenderCore::Fill_Object_Constants()
{
	FFPObjectCB& cb = M->object_cb;
	const unsigned* rs = M->render_states;

	cb.world = M->world_transform;
	Mat_Multiply(cb.world, M->transforms[WW3D_TS_VIEW], cb.worldView);
	Mat_Multiply(cb.worldView, Taa_Jitter_Projection(), cb.worldViewProj);
	Mat_Normal_Matrix(cb.world, cb.worldInvTranspose);

	Copy_Color(M->material.Diffuse,  cb.matDiffuse);
	Copy_Color(M->material.Ambient,  cb.matAmbient);
	Copy_Color(M->material.Specular, cb.matSpecular);
	Copy_Color(M->material.Emissive, cb.matEmissive);
	cb.matPower.x = M->material.Power;
	cb.matPower.y = cb.matPower.z = cb.matPower.w = 0.0f;

	Render_Color_To_Float4(rs[WW3D_RS_AMBIENT],        &cb.globalAmbient.x);
	Render_Color_To_Float4(rs[WW3D_RS_TEXTUREFACTOR],  &cb.tfactor.x);

	// The alpha value is compared against an 8 bit reference value.
	cb.alphaRef.x = static_cast<float>(rs[WW3D_RS_ALPHAREF] & 0xFF) / 255.0f;
	cb.alphaRef.y = cb.alphaRef.z = cb.alphaRef.w = 0.0f;

	int mask = 0;
	for (int i = 0; i < FFP_MAX_LIGHTS; ++i) {
		if (M->light_enabled[i]) mask |= (1 << i);
	}
	cb.lightFlags.x = mask;
	cb.lightFlags.y = rs[WW3D_RS_LIGHTING] ? 1 : 0;
	cb.lightFlags.z = rs[WW3D_RS_SPECULARENABLE] ? 1 : 0;
	cb.lightFlags.w = rs[WW3D_RS_LOCALVIEWER] ? 1 : 0;

	// Without COLORVERTEX all four channels come from the material.
	const bool color_vertex = rs[WW3D_RS_COLORVERTEX] != 0;
	cb.colorSource.x = color_vertex ? static_cast<int>(rs[WW3D_RS_DIFFUSEMATERIALSOURCE])  : WW3D_MCS_MATERIAL;
	cb.colorSource.y = color_vertex ? static_cast<int>(rs[WW3D_RS_AMBIENTMATERIALSOURCE])  : WW3D_MCS_MATERIAL;
	cb.colorSource.z = color_vertex ? static_cast<int>(rs[WW3D_RS_EMISSIVEMATERIALSOURCE]) : WW3D_MCS_MATERIAL;
	cb.colorSource.w = color_vertex ? static_cast<int>(rs[WW3D_RS_SPECULARMATERIALSOURCE]) : WW3D_MCS_MATERIAL;

	cb.alphaTest.x = rs[WW3D_RS_ALPHATESTENABLE] ? 1 : 0;
	cb.alphaTest.y = static_cast<int>(rs[WW3D_RS_ALPHAFUNC]);
	cb.alphaTest.z = (M->sample_count > 1) ? static_cast<int>(M->sample_count) : 0;	// read only by ALPHA_COVERAGE
	cb.alphaTest.w = 0;

	cb.flags.x = rs[WW3D_RS_NORMALIZENORMALS] ? 1 : 0;
	cb.flags.y = color_vertex ? 1 : 0;
	// With a wider scene target Scene_Out rounds the output of draws that do not blend (with R11G11B10 also to its
	// grid) and clamps that of draws that blend (common.hlsli). With HDR nothing is clamped or rounded to 8 bits;
	// R11G11B10 keeps the rounding to its grid.
	const bool r11 = M->scene_format == GPU_FORMAT_R11G11B10_FLOAT;
	if (M->scene_format == GPU_FORMAT_B8G8R8A8_UNORM)	cb.flags.z = 0;
	else if (Hdr_Active()) {
		if (!rs[WW3D_RS_ALPHABLENDENABLE])				cb.flags.z = r11 ? 4 : 0;
		else cb.flags.z = (rs[WW3D_RS_DESTBLEND] == WW3D_BLEND_ONE && RenderDevice::Get_Effect_Settings().hdr_additive != 1.0f) ? 5 : 0;
	}
	else if (rs[WW3D_RS_ALPHABLENDENABLE])				cb.flags.z = 2;
	else cb.flags.z = r11 ? 3 : 1;
	cb.flags.w = static_cast<int>(M->sa_mode);

	for (int i = 0; i < FFP_MAX_LIGHTS; ++i) {
		const WW3DLight& l = M->lights[i];
		FFPLightCB& d = cb.lights[i];
		d.position.x = l.Position.x; d.position.y = l.Position.y; d.position.z = l.Position.z;
		d.position.w = static_cast<float>(l.Type);
		d.direction.x = l.Direction.x; d.direction.y = l.Direction.y; d.direction.z = l.Direction.z;
		d.direction.w = 0.0f;
		Copy_Color(l.Diffuse,  d.diffuse);
		Copy_Color(l.Specular, d.specular);
		Copy_Color(l.Ambient,  d.ambient);
		d.attenuation.x = l.Range;
		d.attenuation.y = l.Attenuation0;
		d.attenuation.z = l.Attenuation1;
		d.attenuation.w = l.Attenuation2;
		// Theta and Phi are full cone angles; the shader compares against the cosine of
		// the half angle.
		d.spot.x = l.Falloff;
		d.spot.y = ::cosf(l.Theta * 0.5f);
		d.spot.z = ::cosf(l.Phi * 0.5f);
		d.spot.w = 0.0f;
	}

	for (int stage = 0; stage < FFP_MAX_STAGES; ++stage) {
		const unsigned* ts = M->stage_states[stage];
		FFPStageCB& d = cb.stages[stage];

		d.color_op.x = static_cast<int>(ts[WW3D_TSS_COLOROP]);
		d.color_op.y = static_cast<int>(ts[WW3D_TSS_COLORARG1]);
		d.color_op.z = static_cast<int>(ts[WW3D_TSS_COLORARG2]);
		d.color_op.w = static_cast<int>(ts[WW3D_TSS_COLORARG0]);

		d.alpha_op.x = static_cast<int>(ts[WW3D_TSS_ALPHAOP]);
		d.alpha_op.y = static_cast<int>(ts[WW3D_TSS_ALPHAARG1]);
		d.alpha_op.z = static_cast<int>(ts[WW3D_TSS_ALPHAARG2]);
		d.alpha_op.w = static_cast<int>(ts[WW3D_TSS_ALPHAARG0]);

		// TEXCOORDINDEX: low 16 bits = coordinate set, high 16 bits = texgen mode.
		const unsigned tci = ts[WW3D_TSS_TEXCOORDINDEX];
		const unsigned texgen = tci >> 16;
		if (texgen > 3) {
			Log_Missing("Texgen SPHEREMAP");
			d.texgen.x = 0;
		}
		else {
			d.texgen.x = static_cast<int>(texgen);
		}
		d.texgen.y = static_cast<int>(tci & 0xFFFF);

		const unsigned ttff = ts[WW3D_TSS_TEXTURETRANSFORMFLAGS];
		d.texgen.z = static_cast<int>(ttff & 0xFF);							// 0 = no texture matrix
		d.texgen.w = (ttff & WW3D_TTFF_PROJECTED) ? 1 : 0;

		d.bump.x = State_As_Float(ts[WW3D_TSS_BUMPENVMAT00]);
		d.bump.y = State_As_Float(ts[WW3D_TSS_BUMPENVMAT01]);
		d.bump.z = State_As_Float(ts[WW3D_TSS_BUMPENVMAT10]);
		d.bump.w = State_As_Float(ts[WW3D_TSS_BUMPENVMAT11]);
		d.bump_scale.x = State_As_Float(ts[WW3D_TSS_BUMPENVLSCALE]);
		d.bump_scale.y = State_As_Float(ts[WW3D_TSS_BUMPENVLOFFSET]);
		d.bump_scale.z = static_cast<float>(Decl_Texcoord_Size(M->vertex_decl, tci & 0xFFFF));
		// 1 if a texture is bound; without one ffp.ps follows the fixed-function rules.
		d.bump_scale.w = M->textures[stage] ? 1.0f : 0.0f;

		cb.texMatrix[stage] = M->transforms[WW3D_TS_TEXTURE0 + stage];
	}

	// Team colour views (ZHCA textures): ffp.ps gives their non-opaque texels the hue and saturation of the side's
	// colour, as W3DAssetManager did on the CPU (RGB_To_HSV of colorspace.h).
	cb.teamColor.x = -1.0f;
	cb.teamColor.y = cb.teamColor.z = cb.teamColor.w = 0.0f;
	unsigned team_stages = 0;
	for (unsigned stage = 0; stage < FFP_COMBINER_STAGES; ++stage) {
		const WW3DTextureHandle t = M->textures[stage];
		if (!t || !t->team_color) continue;
		if (team_stages == 0) {
			const float r = static_cast<float>((t->team_color >> 16) & 0xFF) / 255.0f;
			const float g = static_cast<float>((t->team_color >> 8) & 0xFF) / 255.0f;
			const float b = static_cast<float>(t->team_color & 0xFF) / 255.0f;
			const float hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
			const float lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
			const float s = hi != 0.0f ? (hi - lo) / hi : 0.0f;
			float h = -1.0f;
			if (s != 0.0f) {
				const float delta = hi - lo;
				if (r == hi)		h = (g - b) / delta;
				else if (g == hi)	h = 2.0f + (b - r) / delta;
				else				h = 4.0f + (r - g) / delta;
				h *= 60.0f;
				if (h < 0.0f) h += 360.0f;
			}
			cb.teamColor.x = h;
			cb.teamColor.y = s;
		}
		team_stages |= 1u << stage;
	}
	cb.teamColor.z = static_cast<float>(team_stages);
	cb.teamColor.w = M->ml_normal_active ? 1.0f : 0.0f;

	// Combiner stages 2 and up are not emulated; texgen and the texture matrix exist for all four
	// UV sets. With a custom pixel shader the combiner states are meaningless anyway.
	if (!M->pixel_shader) {
		for (unsigned stage = FFP_COMBINER_STAGES; stage < 8; ++stage) {
			if (M->stage_states[stage][WW3D_TSS_COLOROP] > WW3D_TOP_DISABLE) {
				Log_Missing("combiner stage 2 or higher without its own pixel shader");
				break;
			}
		}
	}
}

bool RenderCore::Cbr_Reserve(unsigned bytes)
{
	// A wrap discards the whole ring, also data written for the same draw just before; so all writes of one
	// draw must fit behind the current offset.
	if (!M->cbr_buffer || bytes > CB_RING_BYTES) return false;
	if (M->cbr_offset + bytes <= CB_RING_BYTES) return true;
	GPU_MAPPED_SUBRESOURCE mapped;
	if (FAILED(M->context->Map(M->cbr_buffer, 0, GPU_MAP_WRITE_DISCARD, 0, &mapped))) return false;
	M->context->Unmap(M->cbr_buffer, 0);
	number_of_API_calls++;
	M->cbr_offset = 0;
	++M->cbr_generation;
	M->cbr_stat_wraps += 1.0;
	M->object_cb_dirty = M->piece_vs_dirty = M->piece_ps_dirty = true;
	M->cbr_bound_mask = 0;
	return true;
}

bool RenderCore::Cbr_Write(const void* data, unsigned bytes, unsigned window_bytes, unsigned* first, unsigned* count)
{
	// Offsets and windows are multiples of 16 constants (256 bytes); the window covers what the shader declares.
	const unsigned size = Cbr_Size(bytes, window_bytes);
	if (!M->cbr_buffer || size > CB_RING_BYTES) return false;
	GPU_MAP map = GPU_MAP_WRITE_NO_OVERWRITE;
	if (M->cbr_offset + size > CB_RING_BYTES) {
		// Wrap: the new ring content no longer holds what slots 1..3 point at, so they are written again.
		map = GPU_MAP_WRITE_DISCARD;
		M->cbr_offset = 0;
		++M->cbr_generation;
		M->cbr_stat_wraps += 1.0;
		M->object_cb_dirty = M->piece_vs_dirty = M->piece_ps_dirty = true;
		M->cbr_bound_mask = 0;
	}
	GPU_MAPPED_SUBRESOURCE mapped;
	if (FAILED(M->context->Map(M->cbr_buffer, 0, map, 0, &mapped))) return false;
	::memcpy(static_cast<unsigned char*>(mapped.pData) + M->cbr_offset, data, bytes);
	// The window may reach past the data; the shader may read that tail.
	if (Ud_Fill() >= 0) ::memset(static_cast<unsigned char*>(mapped.pData) + M->cbr_offset + bytes, Ud_Fill(), size - bytes);
	Ud_Upload("cbr", M->cbr_buffer, M->cbr_offset, data, bytes, false);
	M->context->Unmap(M->cbr_buffer, 0);
	number_of_API_calls++;
	*first = M->cbr_offset / 16u;
	*count = size / 16u;
	M->cbr_offset += size;
	M->cbr_stat_writes += 1.0;
	return true;
}

void RenderCore::Cbr_Disable()
{
	WWDEBUG_SAY(("Render: constant buffer ring failed, buffers are mapped per draw\n"));
	Gpu_Release(M->cbr_buffer);
	M->cbr_context = nullptr;		// ring off; the context itself stays
	M->object_cb_dirty = M->piece_vs_dirty = M->piece_ps_dirty = true;
	M->bound_cbuffers = false;
}

void RenderCore::Update_Constant_Buffers()
{
	if (M->frame_cb_dirty) {
		Fill_Frame_Constants();
		Upload_Constant_Buffer(M->context, M->cb_frame, &M->frame_cb, sizeof(M->frame_cb));
		M->frame_cb_dirty = false;
	}
	if (M->cbr_context) {
		// All dirty slots fit behind the offset (a wrap would discard what the other slots point at). A wrap
		// marks all three dirty; the second pass then writes them at the start of the ring.
		bool ok = Cbr_Reserve(Cbr_Size(sizeof(M->object_cb), 0) + Cbr_Size(FFP_PIECE_VS_REGISTERS * 16, 0) +
							  Cbr_Size(FFP_PIECE_PS_REGISTERS * 16, 0));
		for (int pass = 0; pass < 2 && ok; ++pass) {
			const unsigned generation = M->cbr_generation;
			if (M->object_cb_dirty) {
				if (!M->mi_preset) Fill_Object_Constants();
				M->object_cb_dirty = false;
				ok = ok && Cbr_Write(&M->object_cb, sizeof(M->object_cb), 0, &M->cbr_first[1], &M->cbr_count[1]);
				M->cbr_bound_mask &= ~2u;
			}
			if (M->piece_vs_dirty) {
				M->piece_vs_dirty = false;
				ok = ok && Cbr_Write(M->vs_constants, FFP_PIECE_VS_REGISTERS * 4 * sizeof(float), 0, &M->cbr_first[2], &M->cbr_count[2]);
				M->cbr_bound_mask &= ~4u;
			}
			if (M->piece_ps_dirty) {
				M->piece_ps_dirty = false;
				ok = ok && Cbr_Write(M->ps_constants, FFP_PIECE_PS_REGISTERS * 4 * sizeof(float), 0, &M->cbr_first[3], &M->cbr_count[3]);
				M->cbr_bound_mask &= ~8u;
			}
			if (generation == M->cbr_generation) break;
		}
		if (ok) return;
		Cbr_Disable();
	}
	if (M->object_cb_dirty) {
		if (!M->mi_preset) Fill_Object_Constants();
		Upload_Constant_Buffer(M->context, M->cb_object, &M->object_cb, sizeof(M->object_cb));
		M->object_cb_dirty = false;
	}
	if (M->piece_vs_dirty) {
		Upload_Constant_Buffer(M->context, M->cb_piece_vs, M->vs_constants,
							   FFP_PIECE_VS_REGISTERS * 4 * sizeof(float));
		M->piece_vs_dirty = false;
	}
	if (M->piece_ps_dirty) {
		Upload_Constant_Buffer(M->context, M->cb_piece_ps, M->ps_constants,
							   FFP_PIECE_PS_REGISTERS * 4 * sizeof(float));
		M->piece_ps_dirty = false;
	}
}

// --- Drawing --------------------------------------------------------------------------------------

bool RenderCore::Prepare_Draw(unsigned primitive_type)
{
	if (!M->context || !M->device) return false;

	const GPU_PRIMITIVE_TOPOLOGY topology = Render_Primitive_Topology(primitive_type);
	if (topology == GPU_PRIMITIVE_TOPOLOGY_UNDEFINED) {
		// TRIANGLEFAN does not exist in D3D11. No caller produces it.
		Log_Missing("drawing with TRIANGLEFAN");
		return false;
	}
	if (!Ensure_Draw_Resources()) return false;
	Sa_Select();

	// Choose shaders: a custom shader takes precedence, otherwise the uber permutation. Particles use
	// their own vertex shader (Draw_Particles).
	WW3DVertexShaderHandle vs = M->pa_mode ? M->pa_vs : M->vertex_shader;
	const unsigned ffp_key = FFP_VS_Permutation(M->vertex_decl) | (M->mi_instanced ? FFP_VS_INSTANCED : 0u);
	// Models lit per pixel: lit draws of both uber shaders with a normal; terrain (own lights in the vertex shader,
	// tessellation) and soft particles keep the vertex lighting.
	bool pixel_light = !vs && !M->pixel_shader && !M->fx_soft && !M->td_mode && !M->tl_mode && !M->tt_mode &&
		!M->pa_terrain && (ffp_key & 1) && M->render_states[WW3D_RS_LIGHTING] && !M->ffp_light_failed[0] &&
		(RenderDevice::Get_Effect_Settings().flags & RENDER_EFFECT_MODEL_LIGHT);
	if (pixel_light) {
		vs = Get_FFP_Vertex_Shader(ffp_key | FFP_VS_PIXEL_LIGHT);
		if (!vs) pixel_light = false;
	}
	if (!vs) vs = Get_FFP_Vertex_Shader(ffp_key);
	if (!vs) return false;
	// Terrain tessellation (section 11c): triangle lists of the terrain sections with the uber vertex shader become
	// patches; its vs_5_0 permutation feeds the hull shader.
	bool tessellate = false;
	if (M->tt_mode && !M->pa_mode && !M->vertex_shader && topology == GPU_PRIMITIVE_TOPOLOGY_TRIANGLELIST && Tt_Active()) {
		WW3DVertexShaderHandle tess_vs = Tt_Vertex_Shader(FFP_VS_Permutation(M->vertex_decl));
		if (tess_vs) {
			vs = tess_vs;
			tessellate = true;
		}
	}
	const GPU_PRIMITIVE_TOPOLOGY draw_topology = tessellate ? GPU_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : topology;

	WW3DPixelShaderHandle ps = M->pixel_shader;
	if (!ps) {
		if (!M->ffp_ps && !M->ffp_load_failed) {
			if (!M->ffp_ps_source) {
				M->ffp_ps_source = Load_Shader_Source("ffp.ps.hlsl", &M->ffp_ps_source_len);
			}
			if (M->ffp_ps_source) {
				GpuBlob* code = nullptr;
				if (SUCCEEDED(Compile_FFP_Shader(M->ffp_ps_source, M->ffp_ps_source_len,
												 "ffp.ps.hlsl", nullptr, "ps_4_0", &code))) {
					WW3DPixelShaderObject* obj = W3DNEW WW3DPixelShaderObject;
					::ZeroMemory(obj, sizeof(*obj));
					obj->refcount = 1;
					if (SUCCEEDED(M->device->CreatePixelShader(code->GetBufferPointer(),
															   code->GetBufferSize(), nullptr, &obj->shader))) {
						M->ffp_ps = obj;
					}
					else delete obj;
					code->Release();
				}
				else if (code) code->Release();
			}
			if (!M->ffp_ps) M->ffp_load_failed = true;
		}
		ps = M->ffp_ps;
		if (pixel_light) {
			WW3DPixelShaderHandle light_ps = Get_FFP_Light_Pixel_Shader(false);
			if (light_ps) ps = light_ps;
			else {
				pixel_light = false;
				vs = Get_FFP_Vertex_Shader(ffp_key);
				if (!vs) return false;
			}
		}
	}
	// Blended billboard particles get the SOFT_PARTICLES permutation (only without a custom pixel
	// shader, only when the effect is on and the depth copy belongs to this frame).
	int soft_mode = -1;
	if (M->fx_soft && !M->pixel_shader && ps) {
		soft_mode = Fx_Soft_Mode();
		if (soft_mode >= 0) {
			WW3DPixelShaderHandle soft_ps = Get_FFP_Soft_Pixel_Shader();
			if (soft_ps) ps = soft_ps;
			else soft_mode = -1;
		}
	}
	// Alpha-tested draws without blending into the multisampled scene: the ALPHA_COVERAGE permutation turns the
	// alpha test into a coverage mask (only for "greater" tests, the ones cut-out textures use).
	const unsigned* rs = M->render_states;
	if (soft_mode < 0 && !M->pixel_shader && ps && M->sample_count > 1 && !M->current_render_target &&
		(RenderDevice::Get_Effect_Settings().flags & RENDER_EFFECT_ALPHA_COVERAGE) &&
		rs[WW3D_RS_ALPHATESTENABLE] && !rs[WW3D_RS_ALPHABLENDENABLE] &&
		(rs[WW3D_RS_ALPHAFUNC] == WW3D_CMP_GREATER || rs[WW3D_RS_ALPHAFUNC] == WW3D_CMP_GREATEREQUAL)) {
		WW3DPixelShaderHandle coverage_ps = pixel_light ? Get_FFP_Light_Pixel_Shader(true) : Get_FFP_Coverage_Pixel_Shader();
		if (coverage_ps) ps = coverage_ps;
	}
	// Normal map of the stage 0 texture (t14, flag in the object constants)
	const bool normal_map = pixel_light && M->ml_normal_srv && M->textures[0] && M->textures[0] == M->ml_normal_owner;
	if (normal_map != M->ml_normal_active) {
		M->ml_normal_active = normal_map;
		M->object_cb_dirty = true;
	}
	// Terrain draws (Set_Terrain_Detail) with one of the terrain shaders use its permutation
	// TERRAIN_DETAIL and read the detail textures and table (t9, t10, s9, b6). With "terrainrock"
	// the same draws use the permutation TERRAIN_ROCK (plus TERRAIN_DETAIL when the detail layer is
	// on too); height field, rock textures and light in t11..t13, s10, s11, b7.
	bool terrain_detail = false;
	bool terrain_rock = false;
	bool terrain_normal = false;
	if (M->td_mode) {
		M->td_stat_terrain += 1.0;
		if (M->pixel_shader && ps == M->pixel_shader && M->pixel_shader->detail_source) {
			const bool detail = Td_Active();
			const bool rock = Tr_Active();
			WW3DPixelShaderHandle variant = nullptr;
			// The relief comes as one permutation together with the detail and rock layers that are on.
			if (Tn_Active()) {
				variant = Tn_Pixel_Shader(M->pixel_shader, detail, rock);
				if (variant) {
					terrain_normal = true;
					terrain_rock = rock;
					terrain_detail = detail;
					M->tn_stat_draws += 1.0;
					if (rock) M->tr_stat_draws += 1.0;
				}
			}
			if (!variant && rock) {
				variant = Tr_Pixel_Shader(M->pixel_shader, detail);
				if (variant) {
					terrain_rock = true;
					terrain_detail = detail;
					M->tr_stat_draws += 1.0;
				}
			}
			if (!variant && detail) {
				variant = Td_Pixel_Shader(M->pixel_shader);
				terrain_detail = variant != nullptr;
			}
			if (variant) {
				ps = variant;
				if (terrain_detail) M->td_stat_draws += 1.0;
			}
		}
	}
	if (!ps) return false;
	if (tessellate && RenderDevice::Get_Effect_Settings().terrain_tess_probe && M->tt_probe_ps) {
		ps = M->tt_probe_ps;
		terrain_detail = terrain_rock = terrain_normal = false;
	}
	// Terrain light per pixel: TERRAIN_LIGHT permutation of the shader chosen above (rendercore_boden.cpp).
	const bool terrain_light = M->td_mode && Tb_Select(ps, terrain_detail, terrain_rock, terrain_normal);

	GpuInputLayout* layout = M->pa_mode ? M->pa_layout : static_cast<GpuInputLayout*>(Get_Input_Layout(M->vertex_decl, vs));
	if (!layout) return false;

	if (M->bound_layout != layout) {
		M->context->IASetInputLayout(layout);
		M->bound_layout = layout;
		number_of_API_calls++;
	}
	if (M->bound_topology != static_cast<unsigned>(draw_topology)) {
		M->context->IASetPrimitiveTopology(draw_topology);
		M->bound_topology = static_cast<unsigned>(draw_topology);
		number_of_API_calls++;
	}
	if (M->bound_vs != vs->shader) {
		M->context->VSSetShader(vs->shader, nullptr, 0);
		M->bound_vs = vs->shader;
		number_of_API_calls++;
	}
	if (M->bound_ps != ps->shader) {
		M->context->PSSetShader(ps->shader, nullptr, 0);
		M->bound_ps = ps->shader;
		number_of_API_calls++;
	}

	Update_Constant_Buffers();
	// Without the ring the four buffers are always the same objects (Map with DISCARD only swaps the content).
	// With the ring, slots 1..3 point into it and are bound again whenever their data moved.
	if (!M->bound_cbuffers) {
		GpuBuffer* buffers[4] = { M->cb_frame, M->cb_object, M->cb_piece_vs, M->cb_piece_ps };
		const UINT bind = M->cbr_context ? 1u : 4u;
		M->context->VSSetConstantBuffers(RENDER_SLOT_CB_FRAME, bind, buffers);
		M->context->PSSetConstantBuffers(RENDER_SLOT_CB_FRAME, bind, buffers);
		M->cbr_bound_mask = 0;
		M->bound_cbuffers = true;
		number_of_API_calls += 2;
	}
	if (M->cbr_context && M->cbr_bound_mask != 0xEu) {
		GpuBuffer* ring[3] = { M->cbr_buffer, M->cbr_buffer, M->cbr_buffer };
		M->cbr_context->VSSetConstantBuffers1(RENDER_SLOT_CB_OBJECT, 3, ring, &M->cbr_first[1], &M->cbr_count[1]);
		M->cbr_context->PSSetConstantBuffers1(RENDER_SLOT_CB_OBJECT, 3, ring, &M->cbr_first[1], &M->cbr_count[1]);
		M->cbr_bound_mask = 0xEu;
		number_of_API_calls += 2;
	}
	// Dynamic terrain lights (ffp.vs.hlsl): terrain tiles get the lights in b8 and the height field in t11
	// of the vertex stage, every other draw a buffer of zeros. Terrain lit per pixel takes the zeros as well.
	{
		const bool lights = (M->tl_mode || M->pa_terrain) && M->tl_cb && M->tr_srv[2] && !terrain_light;
		GpuBuffer* tl_cb = lights ? M->tl_cb : M->tl_cb_off;
		if (M->bound_tl_cb != tl_cb) {
			M->context->VSSetConstantBuffers(RENDER_SLOT_CB_TERRAIN_LIGHTS, 1, &tl_cb);
			M->bound_tl_cb = tl_cb;
			number_of_API_calls++;
		}
		if (lights) {
			if (M->bound_tl_srv != M->tr_srv[2]) {
				M->context->VSSetShaderResources(RENDER_SLOT_T_TERRAIN_HEIGHT, 1, &M->tr_srv[2]);
				M->bound_tl_srv = M->tr_srv[2];
				number_of_API_calls++;
			}
			if (M->tl_mode) M->tl_stat_draws += 1.0;
		}
	}
	// GPU skins (ffp.vs.hlsl, skin.hlsli): the palette in b9 of the vertex stage, zeros for every other draw.
	{
		const bool skin = M->sk_count > 0;
		if (skin && M->sk_dirty) {
			Upload_Constant_Buffer(M->context, M->sk_cb, &M->sk_cb_data, 16 + M->sk_count * 3 * 16);
			M->sk_dirty = false;
			M->sk_stat_bones += (double)M->sk_count;
		}
		GpuBuffer* sk_cb = skin ? M->sk_cb : M->sk_cb_off;
		if (M->bound_sk_cb != sk_cb) {
			M->context->VSSetConstantBuffers(RENDER_SLOT_CB_SKIN, 1, &sk_cb);
			M->bound_sk_cb = sk_cb;
			number_of_API_calls++;
		}
		if (skin) M->sk_stat_draws += 1.0;
	}

	if (tessellate) Tt_Bind();
	else if (M->tt_bound) Tt_Unbind();

	Bind_State_Objects();
	Bind_Textures();
	Sa_Bind();
	if (soft_mode >= 0) Fx_Bind_Soft_Particles(soft_mode);
	if (terrain_detail) Td_Bind();
	if (terrain_rock) Tr_Bind(terrain_detail);
	if (terrain_normal) Tn_Bind();
	if (terrain_light) Tb_Bind();
	if (pixel_light) M->ml_stat_draws += 1.0;
	if (normal_map) {
		M->context->PSSetShaderResources(RENDER_SLOT_T_MODEL_NORMAL, 1, &M->ml_normal_srv);
		number_of_API_calls++;
		M->ml_stat_normal += 1.0;
	}
	if (Ud_Tracking()) Ud_Draw();
	return true;
}

void RenderCore::Draw_Indexed_Primitive(WW3DPrimitiveType type, int base_vertex, unsigned min_index,
										  unsigned num_vertices, unsigned start_index, unsigned prim_count)
{
	const unsigned index_count = Render_Vertex_Count_For_Primitives(type, prim_count);
	if (!index_count) return;
	if (M->mi_open && Mi_Capture(type, base_vertex, min_index, num_vertices, start_index, index_count)) return;
	Ud_Draw_Args("indexed", index_count, start_index, base_vertex);
	if (!Prepare_Draw(type)) return;

	// Outside the view but a possible shadow caster: the caster record needs the bound state, not the draw.
	if (M->sh_caster_only) {
		Sa_Done();
		if (M->sh_recording) Sh_Record(true, index_count, start_index, base_vertex, min_index, num_vertices, 0, 0);
		return;
	}
	M->context->DrawIndexed(index_count, start_index, base_vertex);
	number_of_API_calls++;
	Sa_Done();
	// Record opaque world geometry as a shadow caster.
	if (M->sh_recording) Sh_Record(true, index_count, start_index, base_vertex, min_index, num_vertices, 0, 0);
}

void RenderCore::Draw_Primitive_UP(WW3DPrimitiveType type, unsigned prim_count,
									 const void* data, unsigned stride)
{
	const unsigned vertex_count = Render_Vertex_Count_For_Primitives(type, prim_count);
	if (!vertex_count || !data || !stride) return;
	if (!M->context || !Ensure_Draw_Resources()) return;

	const unsigned bytes = vertex_count * stride;
	if (bytes > UP_RING_BYTES) {
		WWDEBUG_SAY(("Render: Draw_Primitive_UP with %u bytes exceeds the ring buffer\n", bytes));
		return;
	}

	// The ring buffer is written sequentially (NO_OVERWRITE) and discarded only on overflow
	// (DISCARD) -- the same pattern as DynamicVBAccessClass.
	unsigned offset = (M->up_ring_offset + 15u) & ~15u;
	GPU_MAP map = GPU_MAP_WRITE_NO_OVERWRITE;
	if (offset + bytes > UP_RING_BYTES) {
		offset = 0;
		map = GPU_MAP_WRITE_DISCARD;
		// Preserve recorded casters from the ring before discarding it.
		if (M->sh_count && M->sh_up_frame == M->sh_frame) {
			Sh_Preserve(M->up_ring, M->sh_up_lo, M->sh_up_hi, false);
			M->sh_up_frame = 0;
		}
	}

	GPU_MAPPED_SUBRESOURCE mapped;
	if (FAILED(M->context->Map(M->up_ring, 0, map, 0, &mapped))) return;
	::memcpy(static_cast<unsigned char*>(mapped.pData) + offset, data, bytes);
	Ud_Upload("up", M->up_ring, offset, data, bytes, false);
	M->context->Unmap(M->up_ring, 0);
	M->up_ring_offset = offset + bytes;

	UINT s = stride, o = offset;
	M->context->IASetVertexBuffers(0, 1, &M->up_ring, &s, &o);
	number_of_API_calls += 2;

	Ud_Draw_Args("up", vertex_count, offset / stride, 0);
	if (!Prepare_Draw(type)) return;
	M->context->Draw(vertex_count, 0);
	number_of_API_calls++;
	Sa_Done();
	if (M->sh_recording) Sh_Record(false, vertex_count, 0, 0, 0, 0, offset, stride);

	// The wrapper then sets VERTEX_BUFFER_CHANGED and rebinds the real source.
	M->stream_vb = nullptr;
	M->pa_batch_count = 0;		// the ring may have wrapped over a particle batch
}

// ==================================================================================================
// 4. Shaders and vertex declarations
// ==================================================================================================

HRESULT RenderCore::Set_Vertex_Shader(WW3DVertexShaderHandle shader)
{
	M->vertex_shader = shader;
	if (M->context) {
		M->context->VSSetShader(shader ? shader->shader : nullptr, nullptr, 0);
		M->bound_vs = shader ? shader->shader : nullptr;
		number_of_API_calls++;
	}
	return static_cast<HRESULT>(WW3D_OK);
}

HRESULT RenderCore::Set_Pixel_Shader(WW3DPixelShaderHandle shader)
{
	M->pixel_shader = shader;
	if (M->context) {
		M->context->PSSetShader(shader ? shader->shader : nullptr, nullptr, 0);
		M->bound_ps = shader ? shader->shader : nullptr;
		number_of_API_calls++;
	}
	return static_cast<HRESULT>(WW3D_OK);
}

void RenderCore::Set_Vertex_Shader_Constant(unsigned reg, const float* data, unsigned count4)
{
	// The registers go to the piece buffer (b2) on the next draw; only store them here.
	if (!data) return;
	for (unsigned i = 0; i < count4 && (reg + i) < 256; ++i)
		memcpy(&M->vs_constants[(reg + i) * 4], &data[i * 4], 4 * sizeof(float));
	M->piece_vs_dirty = true;
}

void RenderCore::Set_Pixel_Shader_Constant(unsigned reg, const float* data, unsigned count4)
{
	if (!data) return;
	for (unsigned i = 0; i < count4 && (reg + i) < 224; ++i)
		memcpy(&M->ps_constants[(reg + i) * 4], &data[i * 4], 4 * sizeof(float));
	M->piece_ps_dirty = true;
}

/// terrain.ps, terrainnoise.ps, terrainnoise2.ps (terrain) and roadnoise2.ps (terrain three-way
/// blends) have the permutation TERRAIN_DETAIL.
static bool Td_Is_Terrain_Shader(const char* path)
{
	if (!path) return false;
	const char* base = path;
	for (const char* p = path; *p; ++p) {
		if (*p == '\\' || *p == '/') base = p + 1;
	}
	return _strnicmp(base, "terrain", 7) == 0 || _strnicmp(base, "roadnoise2.", 11) == 0;
}

/// Case-insensitive substring search.
static bool Contains_No_Case(const char* text, const char* part)
{
	if (!text || !part) return false;
	const size_t n = strlen(part);
	for (; *text; ++text) {
		if (_strnicmp(text, part, n) == 0) return true;
	}
	return false;
}

HRESULT RenderCore::Create_Vertex_Shader_From_HLSL(const char* source, unsigned length, const char* debug_name, WW3DVertexShaderHandle* out)
{
	if (out) *out = nullptr;
	if (!M->device || !out) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	GpuBlob* code = nullptr;
	HRESULT hr = Compile_FFP_Shader(source, length, debug_name ? debug_name : "?", nullptr, "vs_4_0", &code);
	if (SUCCEEDED(hr)) {
		WW3DVertexShaderObject* obj = W3DNEW WW3DVertexShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		obj->bytecode = code;			// kept for the input layouts
		// Grass and tree billboards cast shadows through a dedicated shadow vertex shader with the
		// same wind; other custom vertex shaders (water) cast none.
		if (Contains_No_Case(debug_name, "trees.vs")) obj->shadow_kind = 1;
		hr = M->device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader);
		if (SUCCEEDED(hr)) {
			*out = obj;
			return static_cast<HRESULT>(WW3D_OK);
		}
		code->Release();
		delete obj;
	}
	else if (code) code->Release();
	return hr;
}

HRESULT RenderCore::Create_Pixel_Shader_From_HLSL(const char* source, unsigned length, const char* debug_name, WW3DPixelShaderHandle* out)
{
	if (out) *out = nullptr;
	if (!M->device || !out) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	GpuBlob* code = nullptr;
	HRESULT hr = Compile_FFP_Shader(source, length, debug_name ? debug_name : "?", nullptr, "ps_4_0", &code);
	if (SUCCEEDED(hr)) {
		WW3DPixelShaderObject* obj = W3DNEW WW3DPixelShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		hr = M->device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader);
		code->Release();
		if (SUCCEEDED(hr)) {
			// Terrain shaders keep their source for the TERRAIN_DETAIL permutation (compiled on the
			// first detail draw).
			if (Td_Is_Terrain_Shader(debug_name) && source && length) {
				obj->detail_source = W3DNEWARRAY char[length + 1];
				::memcpy(obj->detail_source, source, length);
				obj->detail_source[length] = 0;
				obj->detail_source_len = length;
				strncpy(obj->detail_name, debug_name, sizeof(obj->detail_name) - 1);
			}
			*out = obj;
			return static_cast<HRESULT>(WW3D_OK);
		}
		delete obj;
	}
	else if (code) code->Release();
	return hr;
}

HRESULT RenderCore::Create_Vertex_Declaration(const WW3DVertexElement* elements, WW3DVertexDeclHandle* out)
{
	if (!out || !elements) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	WW3DVertexDeclObject* obj = W3DNEW WW3DVertexDeclObject;
	::ZeroMemory(obj, sizeof(*obj));
	obj->refcount = 1;
	unsigned n = 0;
	while (n < WW3D_MAX_DECL_LENGTH && elements[n].Type != WW3D_DECLTYPE_UNUSED) {
		obj->elements[n] = elements[n];
		++n;
	}
	obj->element_count = n;
	*out = obj;
	return static_cast<HRESULT>(WW3D_OK);
}

void RenderCore::Set_Vertex_Declaration(WW3DVertexDeclHandle decl)
{
	// The input layout depends on the vertex shader; it is set in the draw path.
	// The component count of the UV sets goes into the object buffer (texture matrix, see ffp.vs.hlsl).
	if (M->vertex_decl != decl) M->object_cb_dirty = true;
	M->vertex_decl = decl;
}
