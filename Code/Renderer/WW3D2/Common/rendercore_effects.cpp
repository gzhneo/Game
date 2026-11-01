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

// rendercore_effects.cpp -- Direct3D 11 backend: graphics effects, post-processing, soft particles, GPU timing.

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

// ==================================================================================================
// 8. Graphics effects
// ==================================================================================================
//
// Which effects run is given by RenderDevice::Get_Effect_Settings() (-d3d11Effects or Options.ini
// "D3D11Effects"). If none is selected, all entry points here return at once, no resource and no
// query is created -- the image stays identical to the plain renderer.
//
//  - Scene_Opaque_Done (from RTS3DScene::Flush, before water, translucent geometry and particles): depth copy
//    of the opaque scene, on request also a colour copy (Request_Scene_Copies, for users such as
//    the water), then ambient occlusion (ssao.ps.hlsl, three passes).
//  - Prepare_Draw: blended billboard particles draw with the SOFT_PARTICLES permutation of
//    ffp.ps.hlsl and read the depth copy in t8.
//  - Scene_3D_Done (from W3DView::draw, before health bars and the 2D UI): post-processing
//    (post.ps.hlsl: FXAA, CAS, colour grading) in at most two full-screen passes.
//  - Present (before the copy to the back buffer): brightness control (Fx_Apply_Gamma) -- its own pass,
//    independent of the effects above and of -d3d11Effects (driven only by Set_Gamma); runs after the
//    complete 2D UI and the cursor so it applies to the whole image, also in test captures
//    (-shotAfter, back buffer before Present).
//
// The passes set target, shaders and states themselves; afterwards Fx_Restore_Bindings restores the
// engine's target and viewport and drops the binding cache (Invalidate_Bindings), so the engine's
// next draw rebinds everything. With multisampling the passes read the resolve (resolve_target) and the
// resolved depth (Fx_Resolve_Depth) instead of the scene target.

GpuPixelShader* Fx_Create_Pixel_Shader(GpuDevice* device, const char* file, const GPU_SHADER_MACRO* macros, const char* profile)
{
	unsigned length = 0;
	char* source = Load_Shader_Source(file, &length);
	if (!source) return nullptr;
	GpuBlob* code = nullptr;
	GpuPixelShader* ps = nullptr;
	if (SUCCEEDED(Compile_FFP_Shader(source, length, file, macros, profile, &code)) && code)
		device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps);
	if (code) code->Release();
	delete[] source;
	return ps;
}

bool RenderCore::Fx_Ensure_Resources()
{
	if (M->fx_failed || !M->device) return false;
	if (M->fx_vs) return true;
	if (!Ensure_Draw_Resources()) return false;

	bool ok = true;
	{
		unsigned length = 0;
		char* source = Load_Shader_Source("fullscreen.vs.hlsl", &length);
		GpuBlob* code = nullptr;
		if (source && SUCCEEDED(Compile_FFP_Shader(source, length, "fullscreen.vs.hlsl", nullptr, "vs_4_0", &code)) && code)
			M->device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &M->fx_vs);
		if (code) code->Release();
		delete[] source;
		ok = ok && M->fx_vs;
	}
	M->fx_cb = Create_Constant_Buffer(M->device, sizeof(FFPEffectsCB));
	ok = ok && M->fx_cb;

	GPU_SAMPLER_DESC sd;
	::ZeroMemory(&sd, sizeof(sd));
	sd.Filter         = GPU_FILTER_MIN_MAG_MIP_LINEAR;
	sd.AddressU       = GPU_TEXTURE_ADDRESS_CLAMP;
	sd.AddressV       = GPU_TEXTURE_ADDRESS_CLAMP;
	sd.AddressW       = GPU_TEXTURE_ADDRESS_CLAMP;
	sd.ComparisonFunc = GPU_COMPARISON_NEVER;
	sd.MaxLOD         = GPU_FLOAT32_MAX;
	ok = ok && SUCCEEDED(M->device->CreateSamplerState(&sd, &M->fx_linear));

	// Write colour only: the scene target's alpha channel stays as the engine left it.
	GPU_BLEND_DESC bd;
	::ZeroMemory(&bd, sizeof(bd));
	GPU_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
	rt.BlendEnable = FALSE;
	rt.SrcBlend = GPU_BLEND_ONE;       rt.DestBlend = GPU_BLEND_ZERO;       rt.BlendOp = GPU_BLEND_OP_ADD;
	rt.SrcBlendAlpha = GPU_BLEND_ONE;  rt.DestBlendAlpha = GPU_BLEND_ZERO;  rt.BlendOpAlpha = GPU_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = GPU_COLOR_WRITE_ENABLE_RED | GPU_COLOR_WRITE_ENABLE_GREEN | GPU_COLOR_WRITE_ENABLE_BLUE;
	ok = ok && SUCCEEDED(M->device->CreateBlendState(&bd, &M->fx_blend_copy));
	rt.RenderTargetWriteMask = GPU_COLOR_WRITE_ENABLE_ALL;
	ok = ok && SUCCEEDED(M->device->CreateBlendState(&bd, &M->fx_blend_all));
	rt.RenderTargetWriteMask = GPU_COLOR_WRITE_ENABLE_RED | GPU_COLOR_WRITE_ENABLE_GREEN | GPU_COLOR_WRITE_ENABLE_BLUE;
	// dest * source (ambient occlusion)
	rt.BlendEnable = TRUE;
	rt.SrcBlend = GPU_BLEND_ZERO;      rt.DestBlend = GPU_BLEND_SRC_COLOR;
	rt.SrcBlendAlpha = GPU_BLEND_ZERO; rt.DestBlendAlpha = GPU_BLEND_ONE;
	ok = ok && SUCCEEDED(M->device->CreateBlendState(&bd, &M->fx_blend_mul));
	// dest + source (bloom)
	rt.SrcBlend = GPU_BLEND_ONE;       rt.DestBlend = GPU_BLEND_ONE;
	rt.SrcBlendAlpha = GPU_BLEND_ZERO; rt.DestBlendAlpha = GPU_BLEND_ONE;
	ok = ok && SUCCEEDED(M->device->CreateBlendState(&bd, &M->fx_blend_add));
	// source + dest * (1 - source alpha) (atmosphere: fog over the scene plus scattered light)
	rt.SrcBlend = GPU_BLEND_ONE;       rt.DestBlend = GPU_BLEND_INV_SRC_ALPHA;
	ok = ok && SUCCEEDED(M->device->CreateBlendState(&bd, &M->fx_blend_premul));

	GPU_DEPTH_STENCIL_DESC dd;
	::ZeroMemory(&dd, sizeof(dd));
	dd.DepthEnable    = FALSE;
	dd.DepthWriteMask = GPU_DEPTH_WRITE_MASK_ZERO;
	dd.DepthFunc      = GPU_COMPARISON_ALWAYS;
	dd.StencilEnable  = FALSE;
	ok = ok && SUCCEEDED(M->device->CreateDepthStencilState(&dd, &M->fx_depth_off));

	GPU_RASTERIZER_DESC rd;
	::ZeroMemory(&rd, sizeof(rd));
	rd.FillMode        = GPU_FILL_SOLID;
	rd.CullMode        = GPU_CULL_NONE;
	rd.DepthClipEnable = TRUE;
	ok = ok && SUCCEEDED(M->device->CreateRasterizerState(&rd, &M->fx_raster));

	if (!ok) {
		WWDEBUG_SAY(("D3D11 effects: cannot create effect resources, effects stay off\n"));
		Fx_Release_Resources();
		M->fx_failed = true;
		return false;
	}
	return true;
}

/// Handle to a copy (shader view only), bindable like an ordinary texture via Set_Device_Texture.
static WW3DTextureObject* Fx_Make_Handle(GpuTexture2D* texture, GpuShaderResourceView* srv,
										 GPU_FORMAT format, unsigned width, unsigned height)
{
	WW3DTextureObject* obj = W3DNEW WW3DTextureObject;
	::ZeroMemory(obj, sizeof(*obj));
	obj->gpu = texture;
	texture->AddRef();
	obj->srv = srv;			// the handle owns the view
	obj->gpu_format = format;
	obj->width = width;
	obj->height = height;
	obj->levels = 1;
	obj->refcount = 1;
	return obj;
}

bool RenderCore::Fx_Depth_Readable() const
{
	return M->scene_depth && (M->sample_count == 1 || (Fx_Msaa_Depth() && M->scene_depth_srv && !M->fx_depth_resolve_failed));
}

bool RenderCore::Fx_Msaa_Depth() const
{
	const unsigned flags = RenderDevice::Get_Effect_Settings().flags;
	return (flags & RENDER_EFFECT_MSAA_DEPTH) != 0 || (flags & RENDER_EFFECT_NO_MSAA_DEPTH) == 0;
}

// Single sample: the typeless format family of the scene depth (D24 or D32, Scene_Depth_Typeless), filled by CopyResource.
// Multisampled: R32_FLOAT render target, filled by Fx_Resolve_Depth. Shaders read the red channel either way.
bool RenderCore::Fx_Create_Depth_Copy(GpuTexture2D** tex, GpuRenderTargetView** rtv, GpuShaderResourceView** srv)
{
	if (!M->device || !Fx_Depth_Readable()) return false;
	const bool resolve = M->sample_count > 1;
	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width = M->scene_width; td.Height = M->scene_height; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = resolve ? GPU_FORMAT_R32_FLOAT : Scene_Depth_Typeless();
	td.SampleDesc.Count = 1;
	td.Usage = GPU_USAGE_DEFAULT;
	td.BindFlags = GPU_BIND_SHADER_RESOURCE | (resolve ? GPU_BIND_RENDER_TARGET : 0);
	if (FAILED(M->device->CreateTexture2D(&td, nullptr, tex))) return false;
	GPU_SHADER_RESOURCE_VIEW_DESC sv;
	::ZeroMemory(&sv, sizeof(sv));
	sv.Format = resolve ? GPU_FORMAT_R32_FLOAT : Scene_Depth_Sampled();
	sv.ViewDimension = GPU_SRV_DIMENSION_TEXTURE2D;
	sv.Texture2D.MipLevels = 1;
	if ((resolve && FAILED(M->device->CreateRenderTargetView(*tex, nullptr, rtv))) ||
		FAILED(M->device->CreateShaderResourceView(*tex, &sv, srv))) {
		Gpu_Release(*srv);
		Gpu_Release(*rtv);
		Gpu_Release(*tex);
		return false;
	}
	return true;
}

// The scene copies (depth, color) may still be bound as input from the previous scene pass: t8 for soft particles, a
// texture stage for the water. A resource bound as input cannot become a render target; Fx_Restore_Bindings makes the
// engine bind its textures again.
void RenderCore::Fx_Unbind_Scene_Copies()
{
	GpuShaderResourceView* nulls[RENDER_SLOT_T_SCENE_DEPTH + 1] = {};
	M->context->PSSetShaderResources(0, RENDER_SLOT_T_SCENE_DEPTH + 1, nulls);
	++number_of_API_calls;
}

bool RenderCore::Fx_Resolve_Depth(void* rtv)
{
	if (!rtv || !M->scene_depth_srv || M->fx_depth_resolve_failed || !Fx_Ensure_Resources()) return false;
	if (!M->fx_depth_resolve_ps) {
		// Texture2DMS without a fixed sample count needs shader model 4.1.
		M->fx_depth_resolve_ps = Fx_Create_Pixel_Shader(M->device, "depthresolve.ps.hlsl", nullptr, "ps_5_0");
		if (!M->fx_depth_resolve_ps) {
			WWDEBUG_SAY(("D3D11 effects: depthresolve.ps.hlsl does not compile, no depth effects with multisampling\n"));
			M->fx_depth_resolve_failed = true;
			return false;
		}
	}
	Fx_Unbind_Scene_Copies();
	WW3DViewport full;
	full.X = 0;  full.Y = 0;  full.Width = M->scene_width;  full.Height = M->scene_height;  full.MinZ = 0.0f;  full.MaxZ = 1.0f;
	Fx_Full_Screen_Pass(rtv, full, M->fx_depth_resolve_ps, M->scene_depth_srv, nullptr, M->fx_blend_all);
	Fx_Restore_Bindings();
	return true;
}

bool RenderCore::Fx_Ensure_Depth_Copy()
{
	if (M->fx_depth_tex) return true;
	GpuShaderResourceView* srv = nullptr;
	if (!Fx_Create_Depth_Copy(&M->fx_depth_tex, &M->fx_depth_rtv, &srv)) return false;
	const GPU_FORMAT format = (M->sample_count > 1) ? GPU_FORMAT_R32_FLOAT : Scene_Depth_Sampled();
	M->fx_depth_handle = Fx_Make_Handle(M->fx_depth_tex, srv, format, M->scene_width, M->scene_height);
	return true;
}

bool RenderCore::Fx_Ensure_Color_Copy()
{
	if (M->fx_color_tex) return true;
	if (!M->device || !M->scene_color || (M->sample_count > 1 && !Fx_Msaa_Depth())) return false;
	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width = M->scene_width; td.Height = M->scene_height; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = GPU_FORMAT_B8G8R8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = GPU_USAGE_DEFAULT;
	const bool convert = M->scene_format != GPU_FORMAT_B8G8R8A8_UNORM;
	td.BindFlags = GPU_BIND_SHADER_RESOURCE | (convert ? GPU_BIND_RENDER_TARGET : 0);
	if (FAILED(M->device->CreateTexture2D(&td, nullptr, &M->fx_color_tex))) return false;
	if (convert && FAILED(M->device->CreateRenderTargetView(M->fx_color_tex, nullptr, &M->fx_color_rtv))) {
		Gpu_Release(M->fx_color_tex);
		return false;
	}
	GpuShaderResourceView* srv = nullptr;
	if (FAILED(M->device->CreateShaderResourceView(M->fx_color_tex, nullptr, &srv))) {
		Gpu_Release(M->fx_color_tex);
		return false;
	}
	M->fx_color_handle = Fx_Make_Handle(M->fx_color_tex, srv, td.Format, M->scene_width, M->scene_height);
	return true;
}

bool RenderCore::Fx_Ensure_Post_Target()
{
	if (M->fx_post_tex) return true;
	if (!M->device) return false;
	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width = M->scene_width; td.Height = M->scene_height; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = GPU_FORMAT_B8G8R8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = GPU_USAGE_DEFAULT;
	td.BindFlags = GPU_BIND_RENDER_TARGET | GPU_BIND_SHADER_RESOURCE;
	if (FAILED(M->device->CreateTexture2D(&td, nullptr, &M->fx_post_tex)) ||
		FAILED(M->device->CreateRenderTargetView(M->fx_post_tex, nullptr, &M->fx_post_rtv)) ||
		FAILED(M->device->CreateShaderResourceView(M->fx_post_tex, nullptr, &M->fx_post_srv))) {
		Gpu_Release(M->fx_post_srv);
		Gpu_Release(M->fx_post_rtv);
		Gpu_Release(M->fx_post_tex);
		return false;
	}
	if (M->sample_count > 1 && M->resolve_target) {
		if (!M->fx_resolve_srv) M->device->CreateShaderResourceView(M->resolve_target, nullptr, &M->fx_resolve_srv);
		if (!M->fx_resolve_rtv) M->device->CreateRenderTargetView(M->resolve_target, nullptr, &M->fx_resolve_rtv);
	}
	return true;
}

bool RenderCore::Fx_Ensure_AO_Targets()
{
	if (M->fx_ao_tex[0]) return true;
	if (!M->device) return false;
	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width = (M->scene_width + 1) / 2; td.Height = (M->scene_height + 1) / 2; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = GPU_FORMAT_R8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = GPU_USAGE_DEFAULT;
	td.BindFlags = GPU_BIND_RENDER_TARGET | GPU_BIND_SHADER_RESOURCE;
	for (int i = 0; i < 2; ++i) {
		if (FAILED(M->device->CreateTexture2D(&td, nullptr, &M->fx_ao_tex[i])) ||
			FAILED(M->device->CreateRenderTargetView(M->fx_ao_tex[i], nullptr, &M->fx_ao_rtv[i])) ||
			FAILED(M->device->CreateShaderResourceView(M->fx_ao_tex[i], nullptr, &M->fx_ao_srv[i]))) {
			for (int k = 0; k < 2; ++k) {
				Gpu_Release(M->fx_ao_srv[k]);
				Gpu_Release(M->fx_ao_rtv[k]);
				Gpu_Release(M->fx_ao_tex[k]);
			}
			return false;
		}
	}
	return true;
}

bool RenderCore::Fx_Ensure_Glow_Targets()
{
	if (M->fx_glow_tex[0]) return true;
	if (!M->device) return false;
	static const unsigned divisor[FX_GLOW_LEVELS] = { 4, 8, 16, 32, 4, 8 };
	for (unsigned i = 0; i < FX_GLOW_LEVELS; ++i) {
		GPU_TEXTURE2D_DESC td;
		::ZeroMemory(&td, sizeof(td));
		td.Width  = (M->scene_width  + divisor[i] - 1) / divisor[i];
		td.Height = (M->scene_height + divisor[i] - 1) / divisor[i];
		if (td.Width < 1) td.Width = 1;
		if (td.Height < 1) td.Height = 1;
		td.MipLevels = 1; td.ArraySize = 1;
		td.Format = GPU_FORMAT_R11G11B10_FLOAT;
		td.SampleDesc.Count = 1;
		td.Usage = GPU_USAGE_DEFAULT;
		td.BindFlags = GPU_BIND_RENDER_TARGET | GPU_BIND_SHADER_RESOURCE;
		M->fx_glow_w[i] = td.Width;
		M->fx_glow_h[i] = td.Height;
		if (FAILED(M->device->CreateTexture2D(&td, nullptr, &M->fx_glow_tex[i])) ||
			FAILED(M->device->CreateRenderTargetView(M->fx_glow_tex[i], nullptr, &M->fx_glow_rtv[i])) ||
			FAILED(M->device->CreateShaderResourceView(M->fx_glow_tex[i], nullptr, &M->fx_glow_srv[i]))) {
			for (unsigned k = 0; k < FX_GLOW_LEVELS; ++k) {
				Gpu_Release(M->fx_glow_srv[k]);
				Gpu_Release(M->fx_glow_rtv[k]);
				Gpu_Release(M->fx_glow_tex[k]);
			}
			return false;
		}
	}
	return true;
}

void RenderCore::Fx_Release_Targets()
{
	for (unsigned i = 0; i < FX_GLOW_LEVELS; ++i) {
		Gpu_Release(M->fx_glow_srv[i]);
		Gpu_Release(M->fx_glow_rtv[i]);
		Gpu_Release(M->fx_glow_tex[i]);
	}
	M->fx_depth_valid = false;
	M->fx_color_valid = false;
	M->sh_ter_valid = false;					// the terrain depth copy has the back buffer size
	Gpu_Release(M->sh_ter_depth_srv);
	Gpu_Release(M->sh_ter_depth_rtv);
	Gpu_Release(M->sh_ter_depth_tex);
	if (M->fx_depth_handle) { Release_Resource(M->fx_depth_handle); M->fx_depth_handle = nullptr; }
	if (M->fx_color_handle) { Release_Resource(M->fx_color_handle); M->fx_color_handle = nullptr; }
	Gpu_Release(M->fx_depth_rtv);
	Gpu_Release(M->fx_depth_tex);
	Gpu_Release(M->fx_color_rtv);
	Gpu_Release(M->fx_color_tex);
	Gpu_Release(M->fx_post_srv);
	Gpu_Release(M->fx_post_rtv);
	Gpu_Release(M->fx_post_tex);
	Gpu_Release(M->fx_resolve_srv);
	Gpu_Release(M->fx_resolve_rtv);
	for (int i = 0; i < 2; ++i) {
		Gpu_Release(M->fx_ao_srv[i]);
		Gpu_Release(M->fx_ao_rtv[i]);
		Gpu_Release(M->fx_ao_tex[i]);
	}
	Fx_Release_GTAO_Targets();
	Taa_Release_Targets();
}

void RenderCore::Fx_Release_Resources()
{
	Sh_Release_Resources();
	Rs_Release_Resources();
	Fx_Release_Targets();
	Gpu_Release(M->fx_vs);
	for (int i = 0; i < FX_POST_VARIANTS; ++i) Gpu_Release(M->fx_post_ps[i]);
	for (int i = 0; i < FX_SSAO_PASSES; ++i) Gpu_Release(M->fx_ssao_ps[i]);
	Fx_Release_GTAO_Shaders();
	Taa_Release_Resources();
	for (int i = 0; i < FX_GLOW_SHADERS; ++i) Gpu_Release(M->fx_glow_ps[i]);
	Gpu_Release(M->fx_gamma_ps);		// fx_gamma_active/-curve stay (no GPU object)
	Gpu_Release(M->fx_convert_ps);
	Gpu_Release(M->fx_depth_resolve_ps);
	M->fx_depth_resolve_failed = false;
	if (M->ffp_ps_soft) { Release_Resource(M->ffp_ps_soft); M->ffp_ps_soft = nullptr; }
	M->ffp_soft_failed = false;
	if (M->ffp_ps_coverage) { Release_Resource(M->ffp_ps_coverage); M->ffp_ps_coverage = nullptr; }
	M->ffp_coverage_failed = false;
	for (int i = 0; i < 2; ++i) {
		if (M->ffp_ps_light[i]) { Release_Resource(M->ffp_ps_light[i]); M->ffp_ps_light[i] = nullptr; }
		M->ffp_light_failed[i] = false;
	}
	Gpu_Release(M->ml_normal_srv);
	M->ml_normal_owner = nullptr;
	Td_Release_Resources();
	M->td_failed = false;
	Tr_Release_Resources();
	M->tr_failed = false;
	Tn_Release_Resources();
	M->tn_failed = false;
	Tt_Release_Resources();
	M->tt_failed = false;
	Gpu_Release(M->fx_cb);
	Gpu_Release(M->fx_light_cb);
	Gpu_Release(M->fx_linear);
	Gpu_Release(M->fx_blend_copy);
	Gpu_Release(M->fx_blend_all);
	Gpu_Release(M->fx_blend_mul);
	Gpu_Release(M->fx_blend_add);
	Gpu_Release(M->fx_blend_premul);
	Gpu_Release(M->fx_depth_off);
	Gpu_Release(M->fx_raster);
	for (int f = 0; f < FX_TIMER_FRAMES; ++f) {
		Gpu_Release(M->fx_disjoint[f]);
		for (int t = 0; t < FX_TIMESTAMPS; ++t) Gpu_Release(M->fx_ts[f][t]);
		M->fx_ts_used[f] = 0;
	}
	M->fx_disjoint_open = false;
	M->fx_soft_open = false;
}

void* RenderCore::Fx_Post_Shader(unsigned variant)
{
	if (variant == 0 || variant >= FX_POST_VARIANTS) return nullptr;
	if (!M->fx_post_ps[variant]) {
		GPU_SHADER_MACRO macros[8];
		unsigned n = 0;
		if (variant == FX_POST_HEAT) {		// the distortion pass is its own shader, never combined
			macros[n].Name = "HEAT_PASS"; macros[n].Definition = "1"; ++n;
			macros[n].Name = nullptr; macros[n].Definition = nullptr;
			M->fx_post_ps[variant] = Fx_Create_Pixel_Shader(M->device, "heat.ps.hlsl", macros);
		}
		else {
			if (variant & FX_POST_FXAA)  { macros[n].Name = "DO_FXAA";  macros[n].Definition = "1"; ++n; }
			if (variant & FX_POST_CAS)   { macros[n].Name = "DO_CAS";   macros[n].Definition = "1"; ++n; }
			if (variant & FX_POST_GRADE) { macros[n].Name = "DO_GRADE"; macros[n].Definition = "1"; ++n; }
			if (variant & FX_POST_HAZE)  { macros[n].Name = "DO_HAZE";  macros[n].Definition = "1"; ++n; }
			if (variant & FX_POST_LIGHTS) { macros[n].Name = "DO_LIGHTS"; macros[n].Definition = "1"; ++n; }
			if (variant & FX_POST_DEBAND) { macros[n].Name = "DO_DEBAND"; macros[n].Definition = "1"; ++n; }
			macros[n].Name = nullptr; macros[n].Definition = nullptr;
			M->fx_post_ps[variant] = Fx_Create_Pixel_Shader(M->device, "post.ps.hlsl", macros);
		}
		if (!M->fx_post_ps[variant]) {
			WWDEBUG_SAY(("D3D11 effects: post.ps.hlsl (variant %u) does not compile, post-processing off\n", variant));
			M->fx_failed = true;
		}
	}
	return M->fx_post_ps[variant];
}

void* RenderCore::Fx_Glow_Shader(unsigned index)
{
	if (index >= FX_GLOW_SHADERS) return nullptr;
	if (!M->fx_glow_ps[index]) {
		const bool heat = (index == FX_GLOW_HEAT_MASK);
		char value[2] = { (char)('0' + (heat ? 0 : index)), 0 };
		GPU_SHADER_MACRO macros[2];
		macros[0].Name = heat ? "HEAT_PASS" : "BLOOM_PASS"; macros[0].Definition = value;
		macros[1].Name = nullptr;                          macros[1].Definition = nullptr;
		M->fx_glow_ps[index] = Fx_Create_Pixel_Shader(M->device, heat ? "heat.ps.hlsl" : "bloom.ps.hlsl", macros);
		if (!M->fx_glow_ps[index]) {
			WWDEBUG_SAY(("D3D11 effects: glow shader %u does not compile, bloom and heat off\n", index));
			M->fx_failed = true;
		}
	}
	return M->fx_glow_ps[index];
}

void* RenderCore::Fx_SSAO_Shader(unsigned pass)
{
	if (pass >= FX_SSAO_PASSES) return nullptr;
	if (!M->fx_ssao_ps[pass]) {
		char value[2] = { (char)('0' + pass), 0 };
		GPU_SHADER_MACRO macros[2];
		macros[0].Name = "SSAO_PASS"; macros[0].Definition = value;
		macros[1].Name = nullptr;     macros[1].Definition = nullptr;
		M->fx_ssao_ps[pass] = Fx_Create_Pixel_Shader(M->device, "ssao.ps.hlsl", macros);
		if (!M->fx_ssao_ps[pass]) {
			WWDEBUG_SAY(("D3D11 effects: ssao.ps.hlsl (pass %u) does not compile, SSAO off\n", pass));
			M->fx_failed = true;
		}
	}
	return M->fx_ssao_ps[pass];
}

void* RenderCore::Fx_Gamma_Shader()
{
	if (!M->fx_gamma_ps) {
		GPU_SHADER_MACRO macros[2];
		macros[0].Name = "DO_GAMMA"; macros[0].Definition = "1";
		macros[1].Name = nullptr;    macros[1].Definition = nullptr;
		M->fx_gamma_ps = Fx_Create_Pixel_Shader(M->device, "post.ps.hlsl", macros);
		if (!M->fx_gamma_ps) {
			WWDEBUG_SAY(("D3D11 effects: post.ps.hlsl (DO_GAMMA) does not compile, brightness control off\n"));
			M->fx_failed = true;
		}
	}
	return M->fx_gamma_ps;
}

// Gamma runs as its own pass from Present(), not from Scene_3D_Done -- that runs before the 2D UI
// (health bars, icons, cursor, subtitles), but the gamma curve must apply to the finished image. The
// source is a copy of M->scene_color after all drawing of this frame, the target is M->scene_color itself
// (via M->scene_rtv) -- the following copy to the back buffer (Present) carries the result, as do
// Save_Back_Buffer_BMP/Get_Back_Buffer (they read M->scene_color). Independent of RenderEffectSettings;
// also runs with no D3D11 effect selected (-d3d11Effects none).
bool RenderCore::Fx_Apply_Gamma()
{
	if (!M->context || !M->scene_color) return false;
	if (!Fx_Ensure_Resources() || !Fx_Ensure_Post_Target()) return false;
	void* ps = Fx_Gamma_Shader();
	if (!ps) return false;

	GpuShaderResourceView* source = nullptr;
	if (M->sample_count > 1) {
		if (!M->resolve_target || !M->fx_resolve_srv) return false;
		M->context->ResolveSubresource(M->resolve_target, 0, M->scene_color, 0, M->scene_format);
		source = M->fx_resolve_srv;
	}
	else {
		if (M->scene_format == GPU_FORMAT_B8G8R8A8_UNORM)
			M->context->CopyResource(M->fx_post_tex, M->scene_color);
		else {
			WW3DViewport full;
			full.X = 0;  full.Y = 0;  full.Width = M->width;  full.Height = M->height;  full.MinZ = 0.0f;  full.MaxZ = 1.0f;
			if (!Fx_Convert_Scene(M->fx_post_rtv, full)) return false;
		}
		source = M->fx_post_srv;
	}
	if (!source) return false;

	FFPEffectsCB& cb = M->fx_cb_data;
	Fx_Set_Float4(cb.source, 1.0f / (float)M->scene_width, 1.0f / (float)M->scene_height, (float)M->scene_width, (float)M->scene_height);
	Fx_Set_Float4(cb.rect, 0.0f, 0.0f, (float)M->width, (float)M->height);
	cb.gamma = M->fx_gamma_curve;
	Fx_Upload_Constants();

	// Target M->scene_rtv (not the swap chain back buffer): Save_Back_Buffer_BMP/Get_Back_Buffer read
	// M->scene_color, so test captures (-shotAfter) include the curve too; the following copy to the
	// back buffer (Present) carries the gamma-corrected image.
	WW3DViewport vp;
	vp.X = 0;  vp.Y = 0;  vp.Width = M->width;  vp.Height = M->height;  vp.MinZ = 0.0f;  vp.MaxZ = 1.0f;
	Fx_Full_Screen_Pass(M->scene_rtv, vp, ps, source, nullptr, M->fx_blend_copy);
	Fx_Restore_Bindings();
	return true;
}

WW3DPixelShaderHandle RenderCore::Get_FFP_Soft_Pixel_Shader()
{
	if (M->ffp_ps_soft) return M->ffp_ps_soft;
	if (M->ffp_soft_failed || !M->ffp_ps_source) return nullptr;
	GPU_SHADER_MACRO macros[2];
	macros[0].Name = "SOFT_PARTICLES"; macros[0].Definition = "1";
	macros[1].Name = nullptr;          macros[1].Definition = nullptr;
	GpuBlob* code = nullptr;
	if (SUCCEEDED(Compile_FFP_Shader(M->ffp_ps_source, M->ffp_ps_source_len, "ffp.ps.hlsl", macros, "ps_4_0", &code)) && code) {
		WW3DPixelShaderObject* obj = W3DNEW WW3DPixelShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		if (SUCCEEDED(M->device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader)))
			M->ffp_ps_soft = obj;
		else
			delete obj;
	}
	if (code) code->Release();
	if (!M->ffp_ps_soft) {
		WWDEBUG_SAY(("D3D11 effects: ffp.ps.hlsl with SOFT_PARTICLES does not compile, soft particles off\n"));
		M->ffp_soft_failed = true;
	}
	return M->ffp_ps_soft;
}

// SV_Coverage as an output needs shader model 4.1; ps_5_0 is within the minimum feature level 11.
WW3DPixelShaderHandle RenderCore::Get_FFP_Coverage_Pixel_Shader()
{
	if (M->ffp_ps_coverage) return M->ffp_ps_coverage;
	if (M->ffp_coverage_failed || !M->ffp_ps_source) return nullptr;
	GPU_SHADER_MACRO macros[2];
	macros[0].Name = "ALPHA_COVERAGE"; macros[0].Definition = "1";
	macros[1].Name = nullptr;          macros[1].Definition = nullptr;
	GpuBlob* code = nullptr;
	if (SUCCEEDED(Compile_FFP_Shader(M->ffp_ps_source, M->ffp_ps_source_len, "ffp.ps.hlsl", macros, "ps_5_0", &code)) && code) {
		WW3DPixelShaderObject* obj = W3DNEW WW3DPixelShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		if (SUCCEEDED(M->device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader)))
			M->ffp_ps_coverage = obj;
		else
			delete obj;
	}
	if (code) code->Release();
	if (!M->ffp_ps_coverage) {
		WWDEBUG_SAY(("D3D11 effects: ffp.ps.hlsl with ALPHA_COVERAGE does not compile, alpha test stays a plain test\n"));
		M->ffp_coverage_failed = true;
	}
	return M->ffp_ps_coverage;
}

void RenderCore::Set_Model_Normal_Texture(WW3DTextureHandle owner, WW3DTextureHandle normal)
{
	GpuShaderResourceView* srv = (owner && normal) ? normal->srv : nullptr;
	if (owner == M->ml_normal_owner && srv == M->ml_normal_srv) return;
	Gpu_Release(M->ml_normal_srv);
	M->ml_normal_owner = srv ? owner : nullptr;
	M->ml_normal_srv = srv;
	if (srv) srv->AddRef();
}

// The PIXEL_LIGHT pair of the uber shaders; with coverage also ALPHA_COVERAGE (ps_5_0 as above).
WW3DPixelShaderHandle RenderCore::Get_FFP_Light_Pixel_Shader(bool coverage)
{
	const int k = coverage ? 1 : 0;
	if (M->ffp_ps_light[k]) return M->ffp_ps_light[k];
	if (M->ffp_light_failed[k] || !M->ffp_ps_source) return nullptr;
	GPU_SHADER_MACRO macros[3];
	unsigned n = 0;
	macros[n].Name = "PIXEL_LIGHT"; macros[n].Definition = "1"; ++n;
	if (coverage) { macros[n].Name = "ALPHA_COVERAGE"; macros[n].Definition = "1"; ++n; }
	macros[n].Name = nullptr; macros[n].Definition = nullptr;
	GpuBlob* code = nullptr;
	if (SUCCEEDED(Compile_FFP_Shader(M->ffp_ps_source, M->ffp_ps_source_len, "ffp.ps.hlsl", macros,
									 coverage ? "ps_5_0" : "ps_4_0", &code)) && code) {
		WW3DPixelShaderObject* obj = W3DNEW WW3DPixelShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		if (SUCCEEDED(M->device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader)))
			M->ffp_ps_light[k] = obj;
		else
			delete obj;
	}
	if (code) code->Release();
	if (!M->ffp_ps_light[k]) {
		WWDEBUG_SAY(("D3D11 effects: ffp.ps.hlsl with PIXEL_LIGHT%s does not compile, models stay lit per vertex\n",
					 coverage ? " and ALPHA_COVERAGE" : ""));
		M->ffp_light_failed[k] = true;
	}
	return M->ffp_ps_light[k];
}

void RenderCore::Fx_Upload_Constants()
{
	Upload_Constant_Buffer(M->context, M->fx_cb, &M->fx_cb_data, sizeof(M->fx_cb_data));
	M->fx_cb_dirty = false;
}

void RenderCore::Fx_Full_Screen_Pass(void* rtv_, const WW3DViewport& vp, void* ps, void* srv0, void* srv1, void* blend)
{
	GpuContext* c = M->context;
	GpuRenderTargetView* rtv = (GpuRenderTargetView*)rtv_;
	GpuShaderResourceView* nulls[2] = { nullptr, nullptr };
	c->PSSetShaderResources(0, 2, nulls);			// unbind the previous pass's source before it becomes a target
	c->OMSetRenderTargets(1, &rtv, nullptr);
	GPU_VIEWPORT v;
	v.TopLeftX = (FLOAT)vp.X;      v.TopLeftY = (FLOAT)vp.Y;
	v.Width    = (FLOAT)vp.Width;  v.Height   = (FLOAT)vp.Height;
	v.MinDepth = 0.0f;             v.MaxDepth = 1.0f;
	c->RSSetViewports(1, &v);
	c->IASetInputLayout(nullptr);
	c->IASetPrimitiveTopology(GPU_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	c->VSSetShader(M->fx_vs, nullptr, 0);
	c->PSSetShader((GpuPixelShader*)ps, nullptr, 0);
	c->PSSetConstantBuffers(RENDER_SLOT_CB_EFFECTS, 1, &M->fx_cb);
	GpuShaderResourceView* srvs[2] = { (GpuShaderResourceView*)srv0, (GpuShaderResourceView*)srv1 };
	c->PSSetShaderResources(0, 2, srvs);
	c->PSSetSamplers(0, 1, &M->fx_linear);
	c->OMSetBlendState((GpuBlendState*)blend, nullptr, 0xFFFFFFFF);
	c->OMSetDepthStencilState(M->fx_depth_off, 0);
	c->RSSetState(M->fx_raster);
	c->Draw(3, 0);
	c->PSSetShaderResources(0, 2, nulls);
	number_of_API_calls += 14;
}

// A float scene target (-sceneFormat rgba16f) into an 8-bit target of the same size: every texel read exactly
// (sceneconvert.ps.hlsl), all four channels written -- the alpha carries the water depth code (from the alpha
// target with a scene target without alpha). Replaces the
// CopyResource of the 8-bit path, which cannot convert formats. vp limits the pass to a rectangle; source
// and target coordinates are the same.
bool RenderCore::Fx_Convert_Scene(void* rtv, const WW3DViewport& vp, bool tonemap)
{
	if (!rtv || !Fx_Ensure_Resources()) return false;
	GpuShaderResourceView* source = M->scene_srv;
	if (M->sample_count > 1) {
		if (!M->resolve_target) return false;
		if (!M->fx_resolve_srv)
			M->device->CreateShaderResourceView(M->resolve_target, nullptr, &M->fx_resolve_srv);
		if (!M->fx_resolve_srv) return false;
		M->context->ResolveSubresource(M->resolve_target, 0, M->scene_color, 0, M->scene_format);
		source = M->fx_resolve_srv;
	}
	if (!source) return false;
	GpuPixelShader*& ps = tonemap ? M->fx_tonemap_ps : M->fx_convert_ps;
	if (!ps) {
		static const GPU_SHADER_MACRO alpha_macros[] = { { "SCENE_ALPHA", "1" }, { nullptr, nullptr } };
		static const GPU_SHADER_MACRO tone_macros[] = { { "TONEMAP", "1" }, { nullptr, nullptr } };
		static const GPU_SHADER_MACRO tone_alpha_macros[] = { { "TONEMAP", "1" }, { "SCENE_ALPHA", "1" }, { nullptr, nullptr } };
		const GPU_SHADER_MACRO* macros = tonemap ? (M->scene_alpha_srv ? tone_alpha_macros : tone_macros)
													   : (M->scene_alpha_srv ? alpha_macros : nullptr);
		ps = Fx_Create_Pixel_Shader(M->device, "sceneconvert.ps.hlsl", macros);
		if (!ps) return false;
	}
	if (tonemap) {
		const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
		Fx_Set_Float4(M->fx_cb_data.tone, fx.hdr_knee, fx.hdr_exposure, 0.0f, 0.0f);
		Fx_Upload_Constants();
	}
	Fx_Unbind_Scene_Copies();
	Fx_Full_Screen_Pass(rtv, vp, ps, source, M->scene_alpha_srv, M->fx_blend_all);
	Fx_Restore_Bindings();
	return true;
}

// The scene target as an 8-bit texture of the scene size for copies (back buffer, screenshots, water scene copy):
// the target itself, its resolve with multisampling, or with a float target its conversion into scene_out.
GpuTexture2D* RenderCore::Fx_Scene_8Bit(bool tonemap)
{
	if (!M->scene_color) return nullptr;
	if (M->scene_format == GPU_FORMAT_B8G8R8A8_UNORM) {
		if (M->sample_count > 1 && M->resolve_target) {
			M->context->ResolveSubresource(M->resolve_target, 0, M->scene_color, 0, GPU_FORMAT_B8G8R8A8_UNORM);
			return M->resolve_target;
		}
		return M->scene_color;
	}
	if (!M->scene_out) {
		GPU_TEXTURE2D_DESC td;
		::ZeroMemory(&td, sizeof(td));
		td.Width = M->scene_width; td.Height = M->scene_height; td.MipLevels = 1; td.ArraySize = 1;
		td.Format = GPU_FORMAT_B8G8R8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = GPU_USAGE_DEFAULT;
		td.BindFlags = GPU_BIND_RENDER_TARGET | GPU_BIND_SHADER_RESOURCE;
		if (FAILED(M->device->CreateTexture2D(&td, nullptr, &M->scene_out)) ||
			FAILED(M->device->CreateRenderTargetView(M->scene_out, nullptr, &M->scene_out_rtv)) ||
			FAILED(M->device->CreateShaderResourceView(M->scene_out, nullptr, &M->scene_out_srv))) {
			Gpu_Release(M->scene_out_srv);
			Gpu_Release(M->scene_out_rtv);
			Gpu_Release(M->scene_out);
			return nullptr;
		}
	}
	// The window, or during a supersampled scene the whole larger target (scene copies of the water).
	WW3DViewport full;
	full.X = 0;  full.Y = 0;  full.MinZ = 0.0f;  full.MaxZ = 1.0f;
	full.Width  = M->rs_active ? M->scene_width  : M->width;
	full.Height = M->rs_active ? M->scene_height : M->height;
	return Fx_Convert_Scene(M->scene_out_rtv, full, tonemap) ? M->scene_out : nullptr;
}

// The current effect settings ask for a float scene target (Create_Scene_Target, Present).
bool RenderCore::Hdr_Wanted() const
{
	return (RenderDevice::Get_Effect_Settings().flags & RENDER_EFFECT_HDR) && M->sample_count <= 1;
}

// With a float scene target the 3D scene ends here: it is converted into scene_out, which becomes the scene target
// until Present, so the 2D interface draws into 8 bits as before (and later gets no tone mapping). While swapped
// scene_format says BGRA8, so copies, brightness, screenshots and Present take the 8-bit path. Not with
// multisampling (the depth target has several samples, scene_out one).
bool RenderCore::Hdr_Active() const
{
	return M->hdr_target && (RenderDevice::Get_Effect_Settings().flags & RENDER_EFFECT_HDR) && !M->scene_swapped &&
		   M->scene_format != GPU_FORMAT_B8G8R8A8_UNORM && M->sample_count == 1;
}

void RenderCore::Fx_Scene_To_8Bit_Target()
{
	if (M->scene_swapped || M->scene_format == GPU_FORMAT_B8G8R8A8_UNORM || M->sample_count > 1) return;
	// With HDR the conversion applies the tone curve; it runs before the post-processing (Scene_3D_Done), which
	// then works on the 8-bit image as without HDR.
	GpuTexture2D* converted = Fx_Scene_8Bit(Hdr_Active());
	if (!converted) return;
	const bool bound = M->bound_rtv == M->scene_rtv;
	GpuTexture2D* t = M->scene_color;  M->scene_color = M->scene_out;  M->scene_out = t;
	GpuRenderTargetView* r = M->scene_rtv;  M->scene_rtv = M->scene_out_rtv;  M->scene_out_rtv = r;
	GpuShaderResourceView* v = M->scene_srv;  M->scene_srv = M->scene_out_srv;  M->scene_out_srv = v;
	M->scene_float_format = M->scene_format;
	M->scene_format = GPU_FORMAT_B8G8R8A8_UNORM;
	M->scene_swapped = true;
	M->object_cb_dirty = true;			// Scene_Out stops rounding
	if (M->scene_surface) {
		Gpu_Release(M->scene_surface->rtv);
		M->scene_surface->rtv = M->scene_rtv;
		M->scene_surface->rtv->AddRef();
	}
	if (bound) M->bound_rtv = M->scene_rtv;
	Fx_Restore_Bindings();
}

void RenderCore::Fx_Scene_Swap_Back()
{
	if (!M->scene_swapped) return;
	const bool bound = M->bound_rtv == M->scene_rtv;
	GpuTexture2D* t = M->scene_color;  M->scene_color = M->scene_out;  M->scene_out = t;
	GpuRenderTargetView* r = M->scene_rtv;  M->scene_rtv = M->scene_out_rtv;  M->scene_out_rtv = r;
	GpuShaderResourceView* v = M->scene_srv;  M->scene_srv = M->scene_out_srv;  M->scene_out_srv = v;
	M->scene_format = M->scene_float_format;
	M->scene_swapped = false;
	M->object_cb_dirty = true;
	if (M->scene_surface) {
		Gpu_Release(M->scene_surface->rtv);
		M->scene_surface->rtv = M->scene_rtv;
		M->scene_surface->rtv->AddRef();
	}
	if (bound) M->bound_rtv = M->scene_rtv;
	Fx_Restore_Bindings();
}

void RenderCore::Fx_Restore_Bindings()
{
	M->context->OMSetRenderTargets(1, &M->bound_rtv, M->bound_dsv);
	Set_Viewport(&M->rs_engine_viewport);
	Invalidate_Bindings(M);
}

void RenderCore::Fx_Copy_Depth()
{
	if (!Fx_Ensure_Depth_Copy()) return;
	if (M->sample_count > 1) {
		if (!Fx_Resolve_Depth(M->fx_depth_rtv)) return;
	}
	else
		M->context->CopyResource(M->fx_depth_tex, M->scene_depth);
	M->fx_depth_valid = true;

	// Projection and viewport at this moment: SSAO and soft particles use them to convert depth
	// back to distances (effects.hlsli, Depth_To_W).
	const WW3DMatrixRaw& p = M->transforms[WW3D_TS_PROJECTION];
	M->fx_view = M->transforms[WW3D_TS_VIEW];
	M->fx_proj = p;
	FFPEffectsCB& cb = M->fx_cb_data;
	Fx_Set_Float4(cb.projA, p.m[0][0], p.m[1][1], p.m[2][0], p.m[2][1]);
	Fx_Set_Float4(cb.projB, p.m[2][2], p.m[2][3], p.m[3][2], p.m[3][3]);
	const float range = M->viewport.MaxZ - M->viewport.MinZ;
	Fx_Set_Float4(cb.depthRange, M->viewport.MinZ, M->viewport.MaxZ, (range > 1e-6f) ? 1.0f / range : 1.0f, 0.0f);
	Fx_Set_Float4(cb.rect, (float)M->viewport.X, (float)M->viewport.Y,
				  (float)(M->viewport.X + M->viewport.Width), (float)(M->viewport.Y + M->viewport.Height));
	Fx_Set_Float4(cb.source, 1.0f / (float)M->scene_width, 1.0f / (float)M->scene_height, (float)M->scene_width, (float)M->scene_height);
	M->fx_cb_dirty = true;
}

void RenderCore::Fx_Run_SSAO()
{
	if (!M->fx_depth_valid || !Fx_Ensure_AO_Targets()) return;
	void* ps0 = Fx_SSAO_Shader(0);
	void* ps1 = Fx_SSAO_Shader(1);
	void* ps2 = Fx_SSAO_Shader(2);
	if (!ps0 || !ps1 || !ps2) return;

	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	Fx_Set_Float4(M->fx_cb_data.ssao, fx.ssao_radius, fx.ssao_strength, 0.002f, fx.ssao_range);
	Fx_Upload_Constants();

	const WW3DViewport& full = M->viewport;
	WW3DViewport half;
	half.X = full.X / 2;  half.Y = full.Y / 2;
	half.Width = (full.Width + 1) / 2;  half.Height = (full.Height + 1) / 2;
	half.MinZ = 0.0f;  half.MaxZ = 1.0f;
	GpuShaderResourceView* depth = M->fx_depth_handle->srv;

	Fx_Full_Screen_Pass(M->fx_ao_rtv[0], half, ps0, nullptr, depth, M->fx_blend_copy);
	Fx_Full_Screen_Pass(M->fx_ao_rtv[1], half, ps1, M->fx_ao_srv[0], depth, M->fx_blend_copy);
	Fx_Full_Screen_Pass(M->scene_rtv, full, ps2, M->fx_ao_srv[1], depth, M->fx_blend_mul);
	Fx_Restore_Bindings();
}

// Constants of the glow effects (see bloom.ps.hlsl, heat.ps.hlsl, post.ps.hlsl DO_HAZE).
static const float FX_BLOOM_THRESHOLD = 0.80f;		// luma where the glow mask starts
static const float FX_BLOOM_KNEE      = 0.12f;		// half width of the soft transition
static const float FX_HDR_BLOOM_THRESHOLD = 1.0f;	// with HDR, on the float scene: white and above
static const float FX_HDR_BLOOM_KNEE      = 0.25f;
static const float FX_BLOOM_SCALE     = 0.60f;		// glow strength at RenderEffectSettings::bloom = 1
static const float FX_BLOOM_BANDPASS  = 0.95f;		// weight of the coarsest level subtracted from the level mean
static const float FX_HEAT_PIXELS     = 2.5f;		// distortion amplitude in pixels at 1080 lines, heat = 1
static const float FX_HAZE_START      = 500.0f;		// world units along the view axis where haze starts
static const float FX_HAZE_END        = 2400.0f;	// ... and where it reaches full strength

/// One pass into a glow level (full level viewport). src_w/src_h give the size of the texture sampled in
/// srv0 (texel size for the taps), the target size follows from the level.
void RenderCore::Fx_Glow_Pass(unsigned level, void* ps, void* srv0, void* srv1, unsigned src_w, unsigned src_h, void* blend)
{
	FFPEffectsCB& cb = M->fx_cb_data;
	const float tw = (float)M->fx_glow_w[level], th = (float)M->fx_glow_h[level];
	Fx_Set_Float4(cb.source, 1.0f / tw, 1.0f / th, tw, th);
	// With HDR the prefilter reads the float scene: only what lies above white glows (Fx_Hdr_Bloom_Down).
	Fx_Set_Float4(cb.bloom, 1.0f / (float)src_w, 1.0f / (float)src_h, M->fx_glow_from_hdr ? FX_HDR_BLOOM_THRESHOLD : FX_BLOOM_THRESHOLD,
		M->fx_glow_from_hdr ? FX_HDR_BLOOM_KNEE : FX_BLOOM_KNEE);
	Fx_Upload_Constants();
	WW3DViewport vp;
	vp.X = 0;  vp.Y = 0;  vp.Width = M->fx_glow_w[level];  vp.Height = M->fx_glow_h[level];  vp.MinZ = 0.0f;  vp.MaxZ = 1.0f;
	Fx_Full_Screen_Pass(M->fx_glow_rtv[level], vp, ps, srv0, srv1, blend);
}

void RenderCore::Fx_Run_Bloom_Down(void* scene_srv)
{
	Fx_Glow_Pass(0, M->fx_glow_ps[FX_GLOW_BLOOM_PREFILTER], scene_srv, nullptr, M->scene_width, M->scene_height, M->fx_blend_copy);
	for (unsigned i = 1; i < 4; ++i)
		Fx_Glow_Pass(i, M->fx_glow_ps[FX_GLOW_BLOOM_DOWN], M->fx_glow_srv[i - 1], nullptr,
					 M->fx_glow_w[i - 1], M->fx_glow_h[i - 1], M->fx_blend_copy);
	for (int i = 2; i >= 0; --i)			// additive: each level becomes the sum of itself and the coarser ones
		Fx_Glow_Pass((unsigned)i, M->fx_glow_ps[FX_GLOW_BLOOM_UP], M->fx_glow_srv[i + 1], nullptr,
					 M->fx_glow_w[i + 1], M->fx_glow_h[i + 1], M->fx_blend_add);
}

// With HDR the bloom levels come from the float scene before the tone curve (Scene_3D_Done), so only values above
// white glow; Fx_Scene_3D_Done then only adds them to the 8-bit image. Single-sampled scene only (as HDR itself).
void RenderCore::Fx_Hdr_Bloom_Down()
{
	M->fx_glow_from_hdr = false;
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	if (!(fx.flags & RENDER_EFFECT_BLOOM) || !M->context || M->current_render_target || !M->scene_srv) return;
	if (!Fx_Ensure_Resources() || !Fx_Ensure_Glow_Targets()) return;
	for (unsigned i = 0; i < FX_GLOW_SHADERS; ++i)
		if (i != FX_GLOW_HEAT_MASK && !Fx_Glow_Shader(i)) return;
	M->fx_glow_from_hdr = true;
	Fx_Run_Bloom_Down(M->scene_srv);
	Fx_Restore_Bindings();
}

void RenderCore::Fx_Run_Bloom_Composite(int x0, int y0, int x1, int y1)
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	FFPEffectsCB& cb = M->fx_cb_data;
	Fx_Set_Float4(cb.source, 1.0f / (float)M->scene_width, 1.0f / (float)M->scene_height, (float)M->scene_width, (float)M->scene_height);
	Fx_Set_Float4(cb.glow, FX_BLOOM_SCALE * fx.bloom, FX_BLOOM_BANDPASS, 0.0f, 0.0f);
	Fx_Upload_Constants();
	WW3DViewport vp;
	vp.X = (unsigned)x0;  vp.Y = (unsigned)y0;
	vp.Width = (unsigned)(x1 - x0);  vp.Height = (unsigned)(y1 - y0);
	vp.MinZ = 0.0f;  vp.MaxZ = 1.0f;
	Fx_Full_Screen_Pass(M->scene_rtv, vp, M->fx_glow_ps[FX_GLOW_BLOOM_COMPOSITE], M->fx_glow_srv[0], M->fx_glow_srv[3],
						M->fx_blend_add);
}

void RenderCore::Fx_Run_Heat_Mask(void* scene_srv)
{
	Fx_Glow_Pass(FX_GLOW_MASK_LEVEL, M->fx_glow_ps[FX_GLOW_HEAT_MASK], scene_srv, nullptr, M->scene_width, M->scene_height, M->fx_blend_copy);
	Fx_Glow_Pass(FX_GLOW_MASK_LEVEL + 1, M->fx_glow_ps[FX_GLOW_BLOOM_DOWN], M->fx_glow_srv[FX_GLOW_MASK_LEVEL], nullptr,
				 M->fx_glow_w[FX_GLOW_MASK_LEVEL], M->fx_glow_h[FX_GLOW_MASK_LEVEL], M->fx_blend_copy);
}

void RenderCore::Scene_Opaque_Done()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	// Shadow caster recording always ends here; the casters are drawn below and dropped afterwards
	// (at the latest in Fx_End_Frame).
	const bool want_shadow = M->sh_recording && M->sh_count > 0;
	M->sh_recording = false;
	M->sh_caster_only = false;
	const bool want_depth = (fx.flags & (RENDER_EFFECT_SOFT_PARTICLES | RENDER_EFFECT_SSAO | RENDER_EFFECT_GTAO | RENDER_EFFECT_TAA | RENDER_EFFECT_HAZE |
										  RENDER_EFFECT_LIGHTS)) != 0
						 || (M->fx_copy_requests & RENDER_SCENE_COPY_DEPTH) != 0 || want_shadow;
	const bool want_color = (M->fx_copy_requests & RENDER_SCENE_COPY_COLOR) != 0;
	if (!want_depth && !want_color) return;
	// Only in the scene target (not in the water reflection, not under a screen filter) and once per frame.
	if (!M->context || M->current_render_target) return;
	if (M->fx_opaque_frame == M->fx_frame + 1) return;
	M->fx_opaque_frame = M->fx_frame + 1;
	if (!Fx_Depth_Readable()) {
		static bool reported = false;
		if (!reported) {
			reported = true;
			WWDEBUG_SAY(("D3D11 effects: multisampled depth not readable -- depth copy, SSAO, soft particles, haze and lights off\n"));
		}
		return;
	}
	if (!Fx_Ensure_Resources()) return;

	Fx_Timestamp(Fx_Ts_Begin(FX_RANGE_DEPTH_SSAO));
	if (want_depth) Fx_Copy_Depth();
	if (want_shadow && M->fx_depth_valid) {
		Sh_Render_Map();
		Sh_Apply();
	}
	if (M->sh_count) Sh_Clear_Records();
	M->sh_ter_valid = false;				// valid only for this scene pass
	if (fx.flags & RENDER_EFFECT_GTAO) Fx_Run_GTAO();		// replaces the SSAO method
	else if (fx.flags & RENDER_EFFECT_SSAO) Fx_Run_SSAO();
	// after the occlusion (it would darken the fog) and before the color copy (water refraction sees the fog)
	if (M->sh_apply_valid) Sh_Atmosphere();
	M->sh_apply_valid = false;
	if (want_color && Fx_Ensure_Color_Copy()) {
		if (M->fx_color_rtv) {
			WW3DViewport full;
			full.X = 0;  full.Y = 0;  full.Width = M->scene_width;  full.Height = M->scene_height;  full.MinZ = 0.0f;  full.MaxZ = 1.0f;
			M->fx_color_valid = Fx_Convert_Scene(M->fx_color_rtv, full);
		}
		else {
			if (M->sample_count > 1)
				M->context->ResolveSubresource(M->fx_color_tex, 0, M->scene_color, 0, GPU_FORMAT_B8G8R8A8_UNORM);
			else
				M->context->CopyResource(M->fx_color_tex, M->scene_color);
			M->fx_color_valid = true;
		}
	}
	Fx_Timestamp(Fx_Ts_End(FX_RANGE_DEPTH_SSAO));
}

void RenderCore::Set_Dynamic_Lights(const RenderDynamicLight* lights, unsigned count)
{
	if (!lights) count = 0;
	if (count > RENDER_MAX_DYNAMIC_LIGHT_CANDIDATES) count = RENDER_MAX_DYNAMIC_LIGHT_CANDIDATES;
	for (unsigned i = 0; i < count; ++i) M->dl_lights[i] = lights[i];
	M->dl_count = count;
}

bool RenderCore::Fx_Ensure_Light_Buffer()
{
	if (!M->fx_light_cb && M->device) M->fx_light_cb = Create_Constant_Buffer(M->device, sizeof(FFPDynLightsCB));
	return M->fx_light_cb != nullptr;
}

/// Moves the submitted lights to view space (the camera of the depth copy), drops those outside the view
/// and keeps the RENDER_MAX_DYNAMIC_LIGHTS strongest (color times radius squared). Returns how many.
unsigned RenderCore::Fx_Build_Lights(FFPDynLightsCB& out)
{
	static_assert(static_cast<int>(FFP_DYN_LIGHTS) == static_cast<int>(RENDER_MAX_DYNAMIC_LIGHTS), "constant buffer and interface capacity differ");
	struct Item { float x, y, z, radius, r, g, b, score; };
	Item items[RENDER_MAX_DYNAMIC_LIGHT_CANDIDATES];
	unsigned n = 0;
	const WW3DMatrixRaw& v = M->fx_view;
	const FFPEffectsCB& fx = M->fx_cb_data;			// projection of the depth copy
	const float side_x = sqrtf(fx.projA.x * fx.projA.x + 1.0f);
	const float side_y = sqrtf(fx.projA.y * fx.projA.y + 1.0f);
	for (unsigned i = 0; i < M->dl_count; ++i) {
		const RenderDynamicLight& l = M->dl_lights[i];
		if (!(l.radius > 0.0f)) continue;
		const float cx = l.x * v.m[0][0] + l.y * v.m[1][0] + l.z * v.m[2][0] + v.m[3][0];
		const float cy = l.x * v.m[0][1] + l.y * v.m[1][1] + l.z * v.m[2][1] + v.m[3][1];
		const float cz = l.x * v.m[0][2] + l.y * v.m[1][2] + l.z * v.m[2][2] + v.m[3][2];
		// Sphere against the frustum in clip space: w is the distance along the view axis, the side planes
		// are |clip| = w. (The sign of the view space z depends on the projection, so nothing assumes it.)
		const float w  = cz * fx.projB.y + fx.projB.w;
		const float px = cx * fx.projA.x + cz * fx.projA.z;
		const float py = cy * fx.projA.y + cz * fx.projA.w;
		const float near_extent = l.radius * ((fx.projB.y < 0.0f) ? -fx.projB.y : fx.projB.y);
		if (w + near_extent <= 0.0f) continue;				// behind the camera
		if (((px < 0.0f) ? -px : px) - w > l.radius * side_x) continue;	// outside the side planes
		if (((py < 0.0f) ? -py : py) - w > l.radius * side_y) continue;
		const float peak = (l.r > l.g) ? ((l.r > l.b) ? l.r : l.b) : ((l.g > l.b) ? l.g : l.b);
		Item& it = items[n++];
		it.x = cx; it.y = cy; it.z = cz; it.radius = l.radius;
		it.r = l.r; it.g = l.g; it.b = l.b;
		it.score = peak * l.radius * l.radius;
	}
	// Strongest first (few items, insertion sort).
	for (unsigned i = 1; i < n; ++i) {
		const Item key = items[i];
		unsigned j = i;
		while (j > 0 && items[j - 1].score < key.score) { items[j] = items[j - 1]; --j; }
		items[j] = key;
	}
	if (n > RENDER_MAX_DYNAMIC_LIGHTS) n = RENDER_MAX_DYNAMIC_LIGHTS;
	for (unsigned i = 0; i < n; ++i) {
		Fx_Set_Float4(out.pos[i], items[i].x, items[i].y, items[i].z, items[i].radius);
		Fx_Set_Float4(out.color[i], items[i].r, items[i].g, items[i].b, 0.0f);
	}
	return n;
}

void RenderCore::Fx_Scene_3D_Done(int x, int y, int width, int height)
{
	if (M->fx_soft_open) {			// the particle section ends with the 3D scene
		Fx_Timestamp(Fx_Ts_End(FX_RANGE_PARTICLES));
		M->fx_soft_open = false;
	}
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	const unsigned wanted = fx.flags & (RENDER_EFFECT_FXAA | RENDER_EFFECT_SHARPEN | RENDER_EFFECT_COLOR_GRADE |
										RENDER_EFFECT_BLOOM | RENDER_EFFECT_HEAT | RENDER_EFFECT_HAZE | RENDER_EFFECT_LIGHTS |
										RENDER_EFFECT_DEBAND | RENDER_EFFECT_TAA);
	if (!wanted || !M->context || M->current_render_target) return;
	if (!M->fx_logged) {
		M->fx_logged = true;
		WWDEBUG_SAY(("D3D11 effects: flags 0x%X, sharpen %.2f, %u sample(s)\n", fx.flags, fx.sharpen, M->sample_count));
	}
	if (!Fx_Ensure_Resources()) return;

	// Clip the view rectangle to the scene target.
	const int x0 = (x > 0) ? x : 0;
	const int y0 = (y > 0) ? y : 0;
	const int x1 = (x + width  < (int)M->scene_width)  ? x + width  : (int)M->scene_width;
	const int y1 = (y + height < (int)M->scene_height) ? y + height : (int)M->scene_height;
	if (x1 <= x0 || y1 <= y0) return;
	// TAA first: bloom, sharpening, grading and dithering work on the smoothed image.
	if (fx.flags & RENDER_EFFECT_TAA) Fx_Run_TAA(x0, y0, x1, y1);

	// Haze needs this frame's depth copy; bloom and heat read the resolved image with multisampling.
	const bool single = (M->sample_count == 1);
	bool bloom = (fx.flags & RENDER_EFFECT_BLOOM) != 0;
	// With multisampling only with msaadepth: the heat mask reads the resolve, haze and lights the resolved depth.
	const bool depth_effects = single || Fx_Msaa_Depth();
	bool heat  = depth_effects && (fx.flags & RENDER_EFFECT_HEAT) != 0;
	const bool haze = depth_effects && (fx.flags & RENDER_EFFECT_HAZE) && M->fx_depth_valid && M->fx_depth_handle;
	if (!single && !Fx_Ensure_Post_Target()) return;
	// Dynamic lights: only with this frame's depth copy and at least one light in view (otherwise no pass).
	FFPDynLightsCB lights_cb;
	bool lights = depth_effects && (fx.flags & RENDER_EFFECT_LIGHTS) && M->fx_depth_valid && M->fx_depth_handle && M->dl_count > 0;
	if (lights) {
		::ZeroMemory(&lights_cb, sizeof(lights_cb));
		lights_cb.info.x = (float)Fx_Build_Lights(lights_cb);
		lights_cb.info.y = fx.lights;
		lights = lights_cb.info.x > 0.0f && Fx_Ensure_Light_Buffer();
	}
	if (bloom || heat) {
		bool glow_ok = Fx_Ensure_Glow_Targets();
		for (unsigned i = 0; glow_ok && i < FX_GLOW_SHADERS; ++i) {
			const bool needed = (i == FX_GLOW_HEAT_MASK) ? heat : (bloom || heat);
			if (needed && !Fx_Glow_Shader(i)) glow_ok = false;
		}
		if (!glow_ok) bloom = heat = false;
	}

	// Bloom and the heat mask read the unmodified scene; the glow is added to the scene before the
	// distortion and the filter passes, so they treat it like any other part of the image.
	if (bloom || heat) {
		GpuShaderResourceView* glow_source = M->scene_srv;
		if (!single) {
			if (!M->resolve_target || !M->fx_resolve_srv) return;
			M->context->ResolveSubresource(M->resolve_target, 0, M->scene_color, 0, M->scene_format);
			glow_source = M->fx_resolve_srv;
		}
		if (glow_source) {
			Fx_Timestamp(Fx_Ts_Begin(FX_RANGE_GLOW_HEAT));
			if (heat)  Fx_Run_Heat_Mask(glow_source);
			if (bloom) {
				if (!M->fx_glow_from_hdr) Fx_Run_Bloom_Down(glow_source);
				Fx_Run_Bloom_Composite(x0, y0, x1, y1);
			}
			Fx_Timestamp(Fx_Ts_End(FX_RANGE_GLOW_HEAT));
		}
	}

	// Passes: heat distortion first (own shader), then FXAA (needs the unmodified image), then CAS; grading
	// and haze are folded into the last of FXAA/CAS, or run as a pass of their own.
	unsigned passes[4];
	unsigned n = 0;
	if (heat)                             passes[n++] = FX_POST_HEAT;
	if ((fx.flags & RENDER_EFFECT_FXAA) && !M->taa_jittered) passes[n++] = FX_POST_FXAA;	// TAA replaces FXAA
	if (fx.flags & RENDER_EFFECT_SHARPEN) passes[n++] = FX_POST_CAS;
	unsigned tail = 0;
	if (fx.flags & RENDER_EFFECT_COLOR_GRADE) tail |= FX_POST_GRADE;
	if (haze)                                 tail |= FX_POST_HAZE;
	if (lights)                               tail |= FX_POST_LIGHTS;
	if (fx.flags & RENDER_EFFECT_DEBAND)      tail |= FX_POST_DEBAND;	// in the pass that writes the scene
	if (tail) {
		if (n && passes[n - 1] != FX_POST_HEAT) passes[n - 1] |= tail;
		else                                    passes[n++] = tail;
	}
	if (n == 0) {
		Fx_Restore_Bindings();
		return;
	}
	// Bloom and heat may have drawn already, so their targets must not stay bound.
	if (!Fx_Ensure_Post_Target()) { Fx_Restore_Bindings(); return; }
	for (unsigned i = 0; i < n; ++i) {
		if (!Fx_Post_Shader(passes[i])) { Fx_Restore_Bindings(); return; }
	}

	Fx_Timestamp(Fx_Ts_Begin(FX_RANGE_POST));
	// Targets alternate between the scene and the intermediate target so that the last pass writes the scene.
	// The first pass reads the scene directly if it writes the intermediate target; if it writes the scene
	// (odd pass count) it reads a copy. The multisampled scene cannot be read: the first pass reads its resolve,
	// the passes before the last alternate between the intermediate target and the resolve target.
	if (!single) {
		if (!M->resolve_target || !M->fx_resolve_srv || !M->fx_resolve_rtv) { Fx_Restore_Bindings(); return; }
		M->context->ResolveSubresource(M->resolve_target, 0, M->scene_color, 0, M->scene_format);
	}
	else if (((n - 1) & 1) == 0) {
		if (M->scene_format == GPU_FORMAT_B8G8R8A8_UNORM)
			M->context->CopyResource(M->fx_post_tex, M->scene_color);
		else {
			WW3DViewport full;
			full.X = 0;  full.Y = 0;  full.Width = M->scene_width;  full.Height = M->scene_height;  full.MinZ = 0.0f;  full.MaxZ = 1.0f;
			if (!Fx_Convert_Scene(M->fx_post_rtv, full)) { Fx_Restore_Bindings(); return; }
		}
	}

	FFPEffectsCB& cb = M->fx_cb_data;
	Fx_Set_Float4(cb.source, 1.0f / (float)M->scene_width, 1.0f / (float)M->scene_height, (float)M->scene_width, (float)M->scene_height);
	Fx_Set_Float4(cb.rect, (float)x0, (float)y0, (float)x1, (float)y1);
	Fx_Set_Float4(cb.post, fx.sharpen, fx.saturation, fx.contrast, fx.warmth);
	Fx_Set_Float4(cb.haze, fx.haze, FX_HAZE_START, 1.0f / (FX_HAZE_END - FX_HAZE_START),
		(M->scene_format != GPU_FORMAT_B8G8R8A8_UNORM) ? 1.0f : 0.0f);
	// Wave phase in 0..2 pi; the shader uses whole multiples of it, so the wrap at 62832 ms is seamless.
	const float phase = (float)(RenderDevice::Get_Effect_Clock() % 62832u) * 1e-4f;
	Fx_Set_Float4(cb.glow, 0.0f, FX_BLOOM_BANDPASS,
				  FX_HEAT_PIXELS * fx.heat * ((float)M->scene_height / 1080.0f), phase);
	Fx_Upload_Constants();
	if (lights) {
		Upload_Constant_Buffer(M->context, M->fx_light_cb, &lights_cb, sizeof(lights_cb));
		M->context->PSSetConstantBuffers(RENDER_SLOT_CB_DYN_LIGHTS, 1, &M->fx_light_cb);
		number_of_API_calls += 2;
	}

	WW3DViewport vp;
	vp.X = (unsigned)x0;  vp.Y = (unsigned)y0;
	vp.Width = (unsigned)(x1 - x0);  vp.Height = (unsigned)(y1 - y0);
	vp.MinZ = 0.0f;  vp.MaxZ = 1.0f;
	for (unsigned i = 0; i < n; ++i) {
		GpuShaderResourceView* source;
		GpuRenderTargetView* target;
		if (single) {
			const bool to_scene = (((n - 1 - i) & 1) == 0);
			source = to_scene ? M->fx_post_srv : M->scene_srv;
			target = to_scene ? M->scene_rtv : M->fx_post_rtv;
		}
		else {
			// Pass i writes the intermediate target when i is even, the resolve target when odd; the last the scene.
			source = (i == 0 || ((i - 1) & 1) == 1) ? M->fx_resolve_srv : M->fx_post_srv;
			target = (i == n - 1) ? M->scene_rtv : (((i & 1) == 0) ? M->fx_post_rtv : M->fx_resolve_rtv);
		}
		GpuShaderResourceView* extra = nullptr;
		if (passes[i] == FX_POST_HEAT)      extra = M->fx_glow_srv[FX_GLOW_MASK_LEVEL + 1];
		else if (passes[i] & (FX_POST_HAZE | FX_POST_LIGHTS)) extra = M->fx_depth_handle->srv;
		// Fx_Scene_Out: the R11G11B10 grid only for passes that write the scene target.
		const float out_mode = (M->scene_format == GPU_FORMAT_B8G8R8A8_UNORM) ? 0.0f :
			((target == M->scene_rtv && M->scene_format == GPU_FORMAT_R11G11B10_FLOAT) ? 2.0f : 1.0f);
		if (cb.haze.w != out_mode) {
			cb.haze.w = out_mode;
			Fx_Upload_Constants();
		}
		Fx_Full_Screen_Pass(target, vp, Fx_Post_Shader(passes[i]), source, extra, M->fx_blend_copy);
	}
	Fx_Restore_Bindings();
	Fx_Timestamp(Fx_Ts_End(FX_RANGE_POST));
}

void RenderCore::Set_Soft_Particles(bool enable)
{
	M->fx_soft = enable;
	// The section is timed whenever any effect is on, even without soft particles -- that is the baseline.
	if (enable && !M->fx_soft_open && RenderDevice::Get_Effect_Settings().flags != 0) {
		Fx_Timestamp(Fx_Ts_Begin(FX_RANGE_PARTICLES));
		M->fx_soft_open = true;
	}
}

/// Soft particle blend mode for the current draw (effects.hlsli, e_soft.y), or -1 if the draw
/// stays hard: effect off, no depth copy this frame, other target, alpha test or no blending,
/// unknown blend factors.
int RenderCore::Fx_Soft_Mode()
{
	if (!(RenderDevice::Get_Effect_Settings().flags & RENDER_EFFECT_SOFT_PARTICLES)) return -1;
	if (!M->fx_depth_valid || M->current_render_target || M->fx_failed) return -1;
	const unsigned* rs = M->render_states;
	if (!rs[WW3D_RS_ALPHABLENDENABLE] || rs[WW3D_RS_ALPHATESTENABLE]) return -1;
	const unsigned src = rs[WW3D_RS_SRCBLEND];
	const unsigned dst = rs[WW3D_RS_DESTBLEND];
	if (src == WW3D_BLEND_SRCALPHA) return 0;												// alpha or additive alpha
	if (src == WW3D_BLEND_ONE && (dst == WW3D_BLEND_ONE || dst == WW3D_BLEND_INVSRCALPHA)) return 1;	// additive, premultiplied
	if ((src == WW3D_BLEND_ZERO && dst == WW3D_BLEND_SRCCOLOR) ||
		(src == WW3D_BLEND_DESTCOLOR && dst == WW3D_BLEND_ZERO)) return 2;					// multiplicative
	return -1;
}

void RenderCore::Fx_Bind_Soft_Particles(int mode)
{
	if (!Fx_Ensure_Resources()) return;
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	const WW3DMatrixRaw& p = M->transforms[WW3D_TS_PROJECTION];
	const float range = M->viewport.MaxZ - M->viewport.MinZ;
	FFPEffectsCB& cb = M->fx_cb_data;
	FFPFloat4 soft, projB, depth;
	Fx_Set_Float4(soft, 1.0f / ((fx.soft_distance > 0.01f) ? fx.soft_distance : 0.01f), (float)mode, 0.0f, 0.0f);
	Fx_Set_Float4(projB, p.m[2][2], p.m[2][3], p.m[3][2], p.m[3][3]);
	Fx_Set_Float4(depth, M->viewport.MinZ, M->viewport.MaxZ, (range > 1e-6f) ? 1.0f / range : 1.0f, 0.0f);
	if (M->fx_cb_dirty || ::memcmp(&soft, &cb.soft, sizeof(soft)) != 0 || ::memcmp(&projB, &cb.softProjB, sizeof(projB)) != 0 ||
		::memcmp(&depth, &cb.softRange, sizeof(depth)) != 0) {
		cb.soft = soft;
		cb.softProjB = projB;
		cb.softRange = depth;
		Fx_Upload_Constants();
	}
	M->context->PSSetConstantBuffers(RENDER_SLOT_CB_EFFECTS, 1, &M->fx_cb);
	GpuShaderResourceView* srv = M->fx_depth_handle ? M->fx_depth_handle->srv : nullptr;
	M->context->PSSetShaderResources(RENDER_SLOT_T_SCENE_DEPTH, 1, &srv);
	number_of_API_calls += 2;
}

void RenderCore::Request_Scene_Copies(unsigned flags)
{
	M->fx_copy_requests = flags;
}

WW3DTextureHandle RenderCore::Get_Scene_Depth_Copy()
{
	return M->fx_depth_valid ? M->fx_depth_handle : nullptr;
}

WW3DTextureHandle RenderCore::Get_Scene_Color_Copy()
{
	return M->fx_color_valid ? M->fx_color_handle : nullptr;
}

// --- GPU timing -------------------------------------------------------------------------------------
// GPU timestamps around the effect sections, only when an effect is on. A frame is read back only
// when its slot comes round again (four frames later) and only if the results are already there --
// the CPU never waits for the GPU.

void RenderCore::Fx_Timestamp(unsigned index)
{
	if (!M->fx_disjoint_open || index >= FX_TIMESTAMPS || !M->device) return;
	const unsigned slot = M->fx_frame % FX_TIMER_FRAMES;
	if (!M->fx_ts[slot][index]) {
		GPU_QUERY_DESC qd;
		qd.Query = GPU_QUERY_TIMESTAMP;
		qd.MiscFlags = 0;
		if (FAILED(M->device->CreateQuery(&qd, &M->fx_ts[slot][index]))) return;
	}
	M->context->End(M->fx_ts[slot][index]);
	M->fx_ts_used[slot] |= 1ull << index;
}

static_assert(FX_RANGES - FX_RANGE_PASS_FIRST == RENDER_GPU_PASS_COUNT, "one range per RenderGpuPass");
static_assert(FX_TIMESTAMPS <= 64, "fx_ts_used is a 64-bit mask");

void RenderCore::Gpu_Pass_Mark(unsigned pass, bool end)
{
	if (pass < RENDER_GPU_PASS_COUNT)
		Fx_Timestamp(end ? Fx_Ts_End(FX_RANGE_PASS_FIRST + pass) : Fx_Ts_Begin(FX_RANGE_PASS_FIRST + pass));
}

void RenderCore::Fx_End_Frame()
{
	M->sh_recording = false;				// drop a recording that got no Scene_Opaque_Done
	M->sh_caster_only = false;
	M->sh_ter_valid = false;
	if (M->sh_count) Sh_Clear_Records();
	if (M->fx_soft_open) {
		Fx_Timestamp(Fx_Ts_End(FX_RANGE_PARTICLES));
		M->fx_soft_open = false;
	}
	if (M->td_ts_open) {				// close a terrain section that got no end
		Fx_Timestamp(Fx_Ts_End(FX_RANGE_TERRAIN));
		M->td_ts_open = false;
	}
	M->td_mode = false;
	M->tl_mode = false;
	if (M->tt_query_open) Tt_Query_End();
	if (M->tt_bound) Tt_Unbind();
	M->tt_mode = false;
	M->tt_drawn = false;
	Tt_Poll_Queries();
	const unsigned slot = M->fx_frame % FX_TIMER_FRAMES;
	if (M->fx_disjoint_open && M->fx_disjoint[slot]) M->context->End(M->fx_disjoint[slot]);
	M->fx_disjoint_open = false;

	++M->fx_frame;
	M->fx_depth_valid = false;
	M->fx_color_valid = false;

	if (!Fx_Timing_Wanted() || !M->device || !M->context) return;

	// Read back the slot that is reused now. If its result is not there yet, this frame stays
	// unmeasured: a new Begin on a query that was not read back discards it (debug layer warning 408).
	const unsigned next = M->fx_frame % FX_TIMER_FRAMES;
	if (M->fx_disjoint[next]) {
		GPU_QUERY_DATA_TIMESTAMP_DISJOINT dj;
		const HRESULT ready = M->context->GetData(M->fx_disjoint[next], &dj, sizeof(dj), GPU_ASYNC_GETDATA_DONOTFLUSH);
		if (ready != S_OK) return;
		if (M->fx_ts_used[next] && !dj.Disjoint && dj.Frequency) {
			for (unsigned r = 0; r < FX_RANGES; ++r) {
				const unsigned a = Fx_Ts_Begin(r), b = Fx_Ts_End(r);
				if ((M->fx_ts_used[next] & (1ull << a)) == 0 || (M->fx_ts_used[next] & (1ull << b)) == 0) continue;
				UINT64 ta = 0, tb = 0;
				if (M->context->GetData(M->fx_ts[next][a], &ta, sizeof(ta), GPU_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
				if (M->context->GetData(M->fx_ts[next][b], &tb, sizeof(tb), GPU_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
				if (tb < ta) continue;
				M->fx_sum_ms[r] += (double)(tb - ta) * 1000.0 / (double)dj.Frequency;
				++M->fx_count[r];
			}
			++M->fx_frames_measured;
		}
	}
	M->fx_ts_used[next] = 0;
	if (!M->fx_disjoint[next]) {
		GPU_QUERY_DESC qd;
		qd.Query = GPU_QUERY_TIMESTAMP_DISJOINT;
		qd.MiscFlags = 0;
		if (FAILED(M->device->CreateQuery(&qd, &M->fx_disjoint[next]))) return;
	}
	M->context->Begin(M->fx_disjoint[next]);
	M->fx_disjoint_open = true;
	Fx_Timestamp(Fx_Ts_Begin(FX_RANGE_FRAME));
}

void RenderCore::Get_Device_Statistics(char* text, unsigned size)
{
	if (!text || !size) return;
	text[0] = 0;
	if (M->device) M->device->Get_Device_Statistics(text, size);
}

void RenderCore::Get_Effect_Statistics(char* text, unsigned size)
{
	if (!text || !size) return;
	text[0] = 0;
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	static const char* names[FX_RANGES] = { "tiefe_ssao", "partikel", "nachbearbeitung", "schattenkarte", "schattenmaske",
											"gelaendetiefe", "gelaende", "glanz_hitze", "hochskalieren", "szene", "bild",
											"spiegelung", "objekte", "baeume", "wasser", "durchsichtig", "oberflaeche",
											"projektionsschatten", "bildfilter" };
	char part[FX_RANGES][64];
	for (unsigned r = 0; r < FX_RANGES; ++r) {
		if (M->fx_count[r])
			_snprintf_s(part[r], sizeof(part[r]), _TRUNCATE, " %s %.4f ms (%u)", names[r], M->fx_sum_ms[r] / M->fx_count[r], M->fx_count[r]);
		else
			part[r][0] = 0;
	}
	// Shadows: casters and preserved buffer ranges per frame, mean edge length of the map (world).
	char shadow[128];
	shadow[0] = 0;
	if (M->sh_stat_frames)
		_snprintf_s(shadow, sizeof(shadow), _TRUNCATE, " schattenwerfer %.0f zeichnungen %.0f kopien %.1f kante %.0f",
			M->sh_stat_casters / M->sh_stat_frames, M->sh_stat_draws / M->sh_stat_frames, M->sh_stat_copies / M->sh_stat_frames,
			M->sh_stat_extent / M->sh_stat_frames);
	// Terrain detail: terrain draws and those with detail per frame, atlas cells with detail, state
	char detail[160];
	detail[0] = 0;
	if (fx.flags & RENDER_EFFECT_TERRAIN_DETAIL) {
		const double frames = M->fx_frame ? (double)M->fx_frame : 1.0;
		_snprintf_s(detail, sizeof(detail), _TRUNCATE, " bodendetail zeichnungen %.1f von %.1f felder %u tabelle %d texturen %d",
			M->td_stat_draws / frames, M->td_stat_terrain / frames, M->td_stat_cells, M->td_table_valid ? 1 : 0,
			M->td_failed ? 0 : (M->td_srv[0] ? 1 : -1));
	}
	// Rock layer: rock draws per frame, height field, state of the rock textures
	char rock[128];
	rock[0] = 0;
	if (fx.flags & RENDER_EFFECT_TERRAIN_ROCK) {
		const double frames = M->fx_frame ? (double)M->fx_frame : 1.0;
		_snprintf_s(rock, sizeof(rock), _TRUNCATE, " berge zeichnungen %.1f hoehenfeld %d x %d texturen %d",
			M->tr_stat_draws / frames, M->tr_width, M->tr_height, M->tr_failed ? 0 : (M->tr_cb ? 1 : -1));
	}
	// Relief: relief draws per frame, state of the relief atlas
	char relief[96];
	relief[0] = 0;
	if (fx.flags & RENDER_EFFECT_TERRAIN_NORMAL) {
		const double frames = M->fx_frame ? (double)M->fx_frame : 1.0;
		_snprintf_s(relief, sizeof(relief), _TRUNCATE, " relief zeichnungen %.1f atlas %d",
			M->tn_stat_draws / frames, M->tn_atlas ? 1 : 0);
	}
	// Tessellation: patch draws and terrain sections per frame; patches, domain points and triangles per frame from
	// the measured sections
	char tess[192];
	tess[0] = 0;
	if (fx.flags & RENDER_EFFECT_TERRAIN_TESS) {
		const double frames = M->fx_frame ? (double)M->fx_frame : 1.0;
		const double per_frame = (M->tt_stat_measured > 0.0) ? (M->tt_stat_sections / frames) / M->tt_stat_measured : 0.0;
		_snprintf_s(tess, sizeof(tess), _TRUNCATE,
			" tessellation zeichnungen %.1f abschnitte %.2f flecken %.0f punkte %.0f dreiecke %.0f gemessen %.0f hoehen %d fels %.2f",
			M->tt_stat_draws / frames, M->tt_stat_sections / frames, M->tt_stat_patches * per_frame, M->tt_stat_points * per_frame,
			M->tt_stat_triangles * per_frame, M->tt_stat_measured, (M->tt_atlas && M->tt_heights) ? 1 : 0, M->tt_rock_amp);
	}
	// Terrain lighting in the vertex shader: lit terrain draws and renders with static prelighting per frame,
	// most dynamic lights in one frame
	// and model draws lit per pixel
	char lights[224];
	lights[0] = 0;
	if (M->tl_stat_draws > 0.0 || M->tb_stat_draws > 0.0) {
		const double frames = M->fx_frame ? (double)M->fx_frame : 1.0;
		_snprintf_s(lights, sizeof(lights), _TRUNCATE, " gelaendelichter zeichnungen %.1f vorbeleuchtung %.1f hoechstens %u je_pixel %.1f",
			M->tl_stat_draws / frames, M->tl_stat_static / frames, M->tl_stat_max, M->tb_stat_draws / frames);
	}
	if (M->ml_stat_draws > 0.0) {
		const double frames = M->fx_frame ? (double)M->fx_frame : 1.0;
		char model[48];
		_snprintf_s(model, sizeof(model), _TRUNCATE, " modelllicht zeichnungen %.1f normalen %.1f", M->ml_stat_draws / frames,
			M->ml_stat_normal / frames);
		strncat_s(lights, sizeof(lights), model, _TRUNCATE);
	}
	if (M->at_stat_frames > 0.0) {
		char atmo[48];
		_snprintf_s(atmo, sizeof(atmo), _TRUNCATE, " atmosphaere bilder %.0f", M->at_stat_frames);
		strncat_s(lights, sizeof(lights), atmo, _TRUNCATE);
	}
	// Constant buffer ring: writes and wraps per frame
	char ring[96];
	ring[0] = 0;
	if (M->cbr_stat_writes > 0.0) {
		const double frames = M->fx_frame ? (double)M->fx_frame : 1.0;
		_snprintf_s(ring, sizeof(ring), _TRUNCATE, " cbring schreiben %.1f umlaeufe %.2f",
			M->cbr_stat_writes / frames, M->cbr_stat_wraps / frames);
	}
	// GPU skinning: skinned draws and uploaded palette rows (bones) per frame
	char skin[96];
	skin[0] = 0;
	if (M->sk_stat_draws > 0.0) {
		const double frames = M->fx_frame ? (double)M->fx_frame : 1.0;
		_snprintf_s(skin, sizeof(skin), _TRUNCATE, " skinning zeichnungen %.1f knochen %.1f",
			M->sk_stat_draws / frames, M->sk_stat_bones / frames);
	}
	// Particles on the GPU: instanced draws and particles per frame
	char particles[96];
	particles[0] = 0;
	if (M->pa_stat_draws > 0.0) {
		const double frames = M->fx_frame ? (double)M->fx_frame : 1.0;
		_snprintf_s(particles, sizeof(particles), _TRUNCATE, " partikel zeichnungen %.1f anzahl %.1f",
			M->pa_stat_draws / frames, M->pa_stat_particles / frames);
	}
	// Instanced meshes: instanced draws and the single draws they replace per frame
	char meshes[96];
	meshes[0] = 0;
	if (M->mi_stat_draws > 0.0) {
		const double frames = M->fx_frame ? (double)M->fx_frame : 1.0;
		_snprintf_s(meshes, sizeof(meshes), _TRUNCATE, " meshinstanzen zeichnungen %.1f statt %.1f",
			M->mi_stat_draws / frames, M->mi_stat_instances / frames);
	}
	// Internal render resolution: share, filter and frames drawn scaled
	char rscale[96];
	rscale[0] = 0;
	if (M->rs_stat_frames > 0.0)
		_snprintf_s(rscale, sizeof(rscale), _TRUNCATE, " renderskalierung %.3f filter %u bilder %.0f",
			fx.render_scale, fx.render_scale_filter, M->rs_stat_frames);
	char passes[RENDER_GPU_PASS_COUNT * 64];
	passes[0] = 0;
	for (unsigned r = FX_RANGE_PASS_FIRST; r < FX_RANGES; ++r)
		strncat_s(passes, sizeof(passes), part[r], _TRUNCATE);
	_snprintf_s(text, size, _TRUNCATE, "GPUEFFEKTE flags 0x%X samples %u bilder %u%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
		fx.flags, M->sample_count, M->fx_frames_measured, part[0], part[1], part[2], part[3], part[4], part[5], part[6], part[7],
		part[8], part[9], part[10], passes, shadow, detail, rock, relief, tess, lights, ring, skin, particles, meshes, rscale);
}
