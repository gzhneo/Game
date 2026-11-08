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

// rendercore_shadows.cpp -- Direct3D 11 backend: sun shadows via shadow map.

#include "renderobjects.h"
#include "rendercore.h"
#include "renderstates.h"
#include "renderformat.h"
#include "rendercb.h"
#include "renderpieces.h"
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
// 9. Shadows: sun shadows via shadow map
// ==================================================================================================
//
// Only with "-d3d11Effects shadowmap" (Options.ini "D3D11Effects"); otherwise all entry points return at once
// and the image stays identical. Per frame:
//
//  - Shadow_Scene_Begin (from RTS3DScene::Render, before the terrain): light matrix from camera, sun direction and
//    height range of the map (Sh_Compute_Light); after that draws are recorded.
//  - Draw_Indexed_Primitive/Draw_Primitive_UP: every opaque draw with depth write (also alpha tested,
//    e.g. trees and fences) and the scene pass camera is recorded as a shadow caster (Sh_Record) --
//    buffers, range, world matrix times light matrix, and for alpha test the texture and UV set of stage 0.
//  - If a used buffer is overwritten (DISCARD of a dynamic buffer, static update) or released
//    before that, Sh_Before_Write preserves the used range with a GPU copy; the record then points to the
//    copy. Ranges written with NO_OVERWRITE stay untouched anyway.
//  - Scene_Opaque_Done: draw the shadow map from the sun's view (Sh_Render_Map), then apply the shadows to the
//    scene target using the depth copy (Sh_Apply, shadowmask.ps.hlsl) -- before SSAO, water and particles.
//
// While Shadow_Maps_Active() returns true, the game draws no stencil shadow volumes and no
// projected model shadows (W3DShadow.cpp, W3DProjectedShadow.cpp, W3DTreeBuffer.cpp); decals remain.

/// General 4x4 inverse (cofactors). false if the matrix is not invertible.
static bool Mat_Inverse(const WW3DMatrixRaw& a, WW3DMatrixRaw& out)
{
	const float* m = &a.m[0][0];
	float inv[16];
	inv[0]  =  m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4]  = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8]  =  m[4] * m[9]  * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9]  * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1]  = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5]  =  m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9]  = -m[0] * m[9]  * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] =  m[0] * m[9]  * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2]  =  m[1] * m[6]  * m[15] - m[1] * m[7]  * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7]  - m[13] * m[3] * m[6];
	inv[6]  = -m[0] * m[6]  * m[15] + m[0] * m[7]  * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7]  + m[12] * m[3] * m[6];
	inv[10] =  m[0] * m[5]  * m[15] - m[0] * m[7]  * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7]  - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5]  * m[14] + m[0] * m[6]  * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6]  + m[12] * m[2] * m[5];
	inv[3]  = -m[1] * m[6]  * m[11] + m[1] * m[7]  * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9]  * m[2] * m[7]  + m[9]  * m[3] * m[6];
	inv[7]  =  m[0] * m[6]  * m[11] - m[0] * m[7]  * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8]  * m[2] * m[7]  - m[8]  * m[3] * m[6];
	inv[11] = -m[0] * m[5]  * m[11] + m[0] * m[7]  * m[9]  + m[4] * m[1] * m[11] - m[4] * m[3] * m[9]  - m[8]  * m[1] * m[7]  + m[8]  * m[3] * m[5];
	inv[15] =  m[0] * m[5]  * m[10] - m[0] * m[6]  * m[9]  - m[4] * m[1] * m[10] + m[4] * m[2] * m[9]  + m[8]  * m[1] * m[6]  - m[8]  * m[2] * m[5];
	const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (det > -1e-20f && det < 1e-20f) return false;
	const float s = 1.0f / det;
	float* o = &out.m[0][0];
	for (int i = 0; i < 16; ++i) o[i] = inv[i] * s;
	return true;
}

/// Point (row vector, w = 1) times matrix, with perspective divide.
static bool Mat_Transform_Point(const WW3DMatrixRaw& m, float x, float y, float z, float* out3)
{
	float r[4];
	for (int j = 0; j < 4; ++j) r[j] = x * m.m[0][j] + y * m.m[1][j] + z * m.m[2][j] + m.m[3][j];
	if (r[3] > -1e-12f && r[3] < 1e-12f) return false;
	out3[0] = r[0] / r[3]; out3[1] = r[1] / r[3]; out3[2] = r[2] / r[3];
	return true;
}

static inline float Sh_Dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static inline bool Sh_Normalize3(float* v)
{
	const float l = ::sqrtf(Sh_Dot3(v, v));
	if (l < 1e-12f) return false;
	v[0] /= l; v[1] /= l; v[2] /= l;
	return true;
}

// Tuning values from reference images.
static const float SH_MIN_SUN_HEIGHT   = 0.08f;		///< sun lower than about 5 degrees: no shadows (infinitely long)
static const float SH_ABOVE_GROUND     = 60.0f;		///< receivers up to this height above the highest terrain point (roofs)
static const float SH_DEPTH_MARGIN     = 20.0f;		///< depth margin in front of and behind the receivers (world)
static const float SH_NORMAL_OFFSET    = 1.2f;		///< normal offset in shadow texels
static const float SH_DEPTH_BIAS_WORLD = 0.25f;		///< fixed depth bias (world) ...
static const float SH_DEPTH_BIAS_TEXEL = 0.5f;		///< ... plus a share of the texel size
static const float SH_PCF_SPACING      = 1.0f;		///< spacing of the 3x3 PCF taps in texels
static const float SH_EDGE_FADE        = 0.03f;		///< fade towards the map border (share of the edge)
static const float SH_DISTANCE_FADE    = 0.15f;		///< fade length at the end of the shadow distance (share)
static const float SH_BACK_FULL        = 0.5f;		///< back faces fully self-shadowed from N.L = -0.5
// Soft shadows: angular size of the light at shadow_soft 1 (penumbra per distance to the caster, larger than the real
// sun for a visible effect), narrowest and widest penumbra (texels, world).
static const float SH_SOFT_LIGHT       = 0.04f;
static const float SH_SOFT_MIN_TEXELS  = 2.0f;
static const float SH_SOFT_MAX_WORLD   = 8.0f;
static const float SH_SOFT_MAX_TEXELS  = 32.0f;
static const float SH_SOFT_SLOPE       = 2.0f;		///< depth bias per texel of tap offset, in texel sizes (slopes to ~63 degrees)
// Cascades: the far map reaches SH_CASCADE_RANGE times the shadow distance, the near map SH_CASCADE_NEAR times it; the
// near map fades into the far one over SH_CASCADE_BLEND of its edge.
static const float SH_CASCADE_RANGE    = 2.0f;
static const float SH_CASCADE_NEAR     = 0.6f;
static const float SH_CASCADE_BLEND    = 0.1f;
// Contact shadows: ray length and occluder thickness (world), depth bias (share of the distance to the camera).
static const float SH_CONTACT_LENGTH   = 8.0f;
static const float SH_CONTACT_THICK    = 4.0f;
static const float SH_CONTACT_BIAS     = 0.002f;
// From this strength on there is no terrain split (a single map); a pixel counts as terrain if the scene
// depth lies at most this far in front of the terrain depth (world plus a share of the distance).
static const float SH_TERRAIN_FULL     = 0.999f;
static const float SH_TERRAIN_TOL      = 0.25f;
static const float SH_TERRAIN_TOL_REL  = 0.001f;
// Atmosphere: fog density at the lowest terrain per world unit at strength 1 and its scale height (world); a view down
// from the usual camera height collects less than 0.1 over the plain, a view along a valley floor much more. The fog takes
// the ambient light plus a share of the sun, slightly bluish, and covers at most ATMO_FOG_MAX. The shafts scatter
// sun light with forward weight ATMO_SHAFT_G; without fog they use a thin medium of their own (share of the density).
static const float ATMO_FOG_DENSITY    = 0.001f;
static const float ATMO_SCALE_HEIGHT   = 30.0f;
static const float ATMO_FOG_SUN        = 0.6f;
static const float ATMO_FOG_TINT[3]    = { 0.86f, 0.92f, 1.0f };
static const float ATMO_FOG_MAX        = 0.85f;
static const float ATMO_SHAFT_MEDIUM   = 1.0f;
static const float ATMO_SHAFT_GAIN     = 1.0f;
static const float ATMO_SHAFT_G        = 0.5f;

bool RenderCore::Sh_Ensure_Resources()
{
	if (M->sh_failed || !M->device) return false;
	if (M->sh_tex) return true;
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	unsigned size = fx.shadow_map_size;
	if (size < 512) size = 512;
	if (size > 8192) size = 8192;
	M->sh_size = size;

	bool ok = true;
	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width = size; td.Height = size; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = GPU_FORMAT_R32_TYPELESS;
	td.SampleDesc.Count = 1;
	td.Usage = GPU_USAGE_DEFAULT;
	td.BindFlags = GPU_BIND_DEPTH_STENCIL | GPU_BIND_SHADER_RESOURCE;
	ok = ok && SUCCEEDED(M->device->CreateTexture2D(&td, nullptr, &M->sh_tex));
	if (ok) {
		GPU_DEPTH_STENCIL_VIEW_DESC dv;
		::ZeroMemory(&dv, sizeof(dv));
		dv.Format = GPU_FORMAT_D32_FLOAT;
		dv.ViewDimension = GPU_DSV_DIMENSION_TEXTURE2D;
		ok = SUCCEEDED(M->device->CreateDepthStencilView(M->sh_tex, &dv, &M->sh_dsv));
		GPU_SHADER_RESOURCE_VIEW_DESC sv;
		::ZeroMemory(&sv, sizeof(sv));
		sv.Format = GPU_FORMAT_R32_FLOAT;
		sv.ViewDimension = GPU_SRV_DIMENSION_TEXTURE2D;
		sv.Texture2D.MipLevels = 1;
		ok = ok && SUCCEEDED(M->device->CreateShaderResourceView(M->sh_tex, &sv, &M->sh_srv));
	}
	M->sh_cb_draw  = Create_Constant_Buffer(M->device, sizeof(FFPShadowDrawCB));
	M->sh_cb_apply = Create_Constant_Buffer(M->device, sizeof(FFPShadowApplyCB));
	ok = ok && M->sh_cb_draw && M->sh_cb_apply;

	// Casters: no back-face culling (terrain is a one-sided surface, billboards), slope bias against acne,
	// no depth clip -- casters in front of the near plane (high aircraft) land at depth 0 and still cast.
	GPU_RASTERIZER_DESC rd;
	::ZeroMemory(&rd, sizeof(rd));
	rd.FillMode = GPU_FILL_SOLID;
	rd.CullMode = GPU_CULL_NONE;
	rd.DepthBias = 0;
	rd.SlopeScaledDepthBias = 1.5f;
	rd.DepthBiasClamp = 0.01f;
	rd.DepthClipEnable = FALSE;
	ok = ok && SUCCEEDED(M->device->CreateRasterizerState(&rd, &M->sh_raster));

	GPU_DEPTH_STENCIL_DESC dd;
	::ZeroMemory(&dd, sizeof(dd));
	dd.DepthEnable = TRUE;
	dd.DepthWriteMask = GPU_DEPTH_WRITE_MASK_ALL;
	dd.DepthFunc = GPU_COMPARISON_LESS;
	ok = ok && SUCCEEDED(M->device->CreateDepthStencilState(&dd, &M->sh_depth));

	GPU_SAMPLER_DESC sd;
	::ZeroMemory(&sd, sizeof(sd));
	sd.Filter = GPU_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	sd.AddressU = sd.AddressV = sd.AddressW = GPU_TEXTURE_ADDRESS_BORDER;
	sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0f;
	sd.ComparisonFunc = GPU_COMPARISON_LESS_EQUAL;
	sd.MaxLOD = GPU_FLOAT32_MAX;
	ok = ok && SUCCEEDED(M->device->CreateSamplerState(&sd, &M->sh_cmp));
	::ZeroMemory(&sd, sizeof(sd));
	sd.Filter = GPU_FILTER_MIN_MAG_MIP_LINEAR;
	sd.AddressU = sd.AddressV = sd.AddressW = GPU_TEXTURE_ADDRESS_WRAP;
	sd.ComparisonFunc = GPU_COMPARISON_NEVER;
	sd.MaxLOD = GPU_FLOAT32_MAX;
	ok = ok && SUCCEEDED(M->device->CreateSamplerState(&sd, &M->sh_alpha_sampler));

	M->sh_alpha_ps = Fx_Create_Pixel_Shader(M->device, "shadow.ps.hlsl", nullptr);
	M->sh_mask_ps  = Fx_Create_Pixel_Shader(M->device, "shadowmask.ps.hlsl", nullptr);
	ok = ok && M->sh_alpha_ps && M->sh_mask_ps;
	{
		// without it the atmosphere stays off, the shadows do not depend on it
		GPU_SHADER_MACRO atmo[2] = { { "ATMOSPHERE", "1" }, { nullptr, nullptr } };
		M->sh_atmo_ps = Fx_Create_Pixel_Shader(M->device, "shadowmask.ps.hlsl", atmo);
	}

	M->sh_casters = W3DNEWARRAY RenderShadowCaster[SH_MAX_CASTERS];
	M->sh_sway = W3DNEWARRAY FFPFloat4[static_cast<unsigned>(SH_MAX_SWAY) * static_cast<unsigned>(FFP_SHADOW_SWAY)];
	M->sh_skin_rows = W3DNEWARRAY FFPFloat4[SH_MAX_SKIN_ROWS];
	M->sh_skin_row_count = 0;
	M->sh_cb_skin = Create_Constant_Buffer(M->device, sizeof(FFPSkinCB));
	ok = ok && M->sh_cb_skin;
	{
		// Without the instance stream every caster is drawn on its own (as before).
		GPU_BUFFER_DESC bd;
		::ZeroMemory(&bd, sizeof(bd));
		bd.ByteWidth      = SH_MAX_CASTERS * static_cast<unsigned>(sizeof(WW3DMatrixRaw));
		bd.Usage          = GPU_USAGE_DYNAMIC;
		bd.BindFlags      = GPU_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = GPU_CPU_ACCESS_WRITE;
		if (FAILED(M->device->CreateBuffer(&bd, nullptr, &M->sh_inst))) M->sh_inst = nullptr;
	}
	M->sh_count = 0;
	M->sh_sway_count = 0;
	M->sh_copy_used = 0;
	if (M->sh_frame == 0) M->sh_frame = 1;

	if (!ok) {
		WWDEBUG_SAY(("Shadows: cannot create shadow map resources, shadows stay classic\n"));
		Sh_Release_Resources();
		M->sh_failed = true;
		return false;
	}
	WWDEBUG_SAY(("Shadows: shadow map %ux%u (D32), range %.0f\n", size, size, fx.shadow_distance));
	return true;
}

void RenderCore::Sh_Release_Resources()
{
	M->sh_recording = false;
	Sh_Clear_Records();
	for (unsigned i = 0; i < SH_VS_VARIANTS; ++i) {
		if (M->sh_vs[i]) { Release_Resource(M->sh_vs[i]); M->sh_vs[i] = nullptr; }
	}
	M->sh_vs_failed = 0;
	for (unsigned i = 0; i < SH_MAX_COPIES; ++i) {
		Gpu_Release(M->sh_copies[i].buffer);
		M->sh_copies[i].size = 0;
	}
	Gpu_Release(M->sh_alpha_ps);
	Gpu_Release(M->sh_mask_ps);
	Gpu_Release(M->sh_atmo_ps);
	Gpu_Release(M->sh_cb_draw);
	Gpu_Release(M->sh_cb_apply);
	Gpu_Release(M->sh_raster);
	Gpu_Release(M->sh_depth);
	Gpu_Release(M->sh_cmp);
	Gpu_Release(M->sh_alpha_sampler);
	Gpu_Release(M->sh_srv);
	Gpu_Release(M->sh_dsv);
	Gpu_Release(M->sh_tex);
	Gpu_Release(M->sh_near_srv);
	Gpu_Release(M->sh_near_dsv);
	Gpu_Release(M->sh_near_tex);
	M->sh_near_failed = false;
	M->sh_cascade = false;
	Sh_Release_Terrain();
	M->sh_ter_failed = false;
	delete[] M->sh_casters; M->sh_casters = nullptr;
	delete[] M->sh_sway; M->sh_sway = nullptr;
	delete[] M->sh_skin_rows; M->sh_skin_rows = nullptr;
	Gpu_Release(M->sh_cb_skin);
	Gpu_Release(M->sh_inst);
	M->sh_count = 0;
}

void RenderCore::Sh_Clear_Records()
{
	for (unsigned i = 0; i < M->sh_count; ++i) {
		RenderShadowCaster& r = M->sh_casters[i];
		Gpu_Release(r.layout);
		Gpu_Release(r.inst_layout);
		Gpu_Release(r.alpha_srv);
	}
	M->sh_count = 0;
	M->sh_sway_count = 0;
	M->sh_skin_row_count = 0;
	M->sh_copy_used = 0;
	M->sh_up_frame = 0;
	// New id: stamps of earlier recordings on the buffers are no longer valid.
	if (++M->sh_frame == 0) M->sh_frame = 1;
}

WW3DVertexShaderHandle RenderCore::Sh_Vertex_Shader(unsigned variant)
{
	if (variant >= SH_VS_VARIANTS) return nullptr;
	if (M->sh_vs[variant]) return M->sh_vs[variant];
	if (M->sh_vs_failed & (1u << variant)) return nullptr;

	unsigned length = 0;
	char* source = Load_Shader_Source("shadow.vs.hlsl", &length);
	static_assert(SH_VS_VARIANTS <= 32 && SH_VS_INSTANCED + 5 == SH_VS_VARIANTS, "variant bits and instanced range");
	const bool instanced = variant >= SH_VS_INSTANCED;
	const unsigned base = instanced ? variant - SH_VS_INSTANCED : variant;
	char uv_set[2] = { static_cast<char>('0' + ((base >= 1 && base <= 4) ? base - 1 : 0)), 0 };
	GPU_SHADER_MACRO macros[3];
	for (unsigned m = 0; m < 3; ++m) { macros[m].Name = nullptr; macros[m].Definition = nullptr; }
	unsigned macro = 0;
	if (base == SH_VS_TREES)		{ macros[macro].Name = "SHADOW_TREES"; macros[macro++].Definition = "1"; }
	else if (base == SH_VS_SKIN)	{ macros[macro].Name = "SHADOW_SKIN";  macros[macro++].Definition = "1"; }
	else if (base >= 1)				{ macros[macro].Name = "SHADOW_UV";    macros[macro++].Definition = uv_set; }
	if (instanced)					{ macros[macro].Name = "SHADOW_INSTANCED"; macros[macro++].Definition = "1"; }

	GpuBlob* code = nullptr;
	WW3DVertexShaderObject* obj = nullptr;
	if (source && SUCCEEDED(Compile_FFP_Shader(source, length, "shadow.vs.hlsl", macros, "vs_4_0", &code)) && code) {
		obj = W3DNEW WW3DVertexShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		obj->bytecode = code;				// kept for the input layouts
		obj->shadow_kind = instanced ? 2u : 0u;
		if (FAILED(M->device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader))) {
			delete obj;
			obj = nullptr;
		}
		else code = nullptr;
	}
	if (code) code->Release();
	delete[] source;
	if (!obj) {
		WWDEBUG_SAY(("Shadows: shadow.vs.hlsl (variant %u) does not compile\n", variant));
		M->sh_vs_failed |= 1u << variant;
		return nullptr;
	}
	M->sh_vs[variant] = obj;
	return obj;
}

// Receiver area in light space: the view volume clipped to the slab z_lo..z_hi and limited to the share t_limit of the
// view axis, sampled along 32 rays around the screen border. false if fewer than three points remain.
static bool Sh_Footprint(const WW3DMatrixRaw& inv, float t_limit, float z_lo, float z_hi, const float* right, const float* up,
						 const float* fwd, float* lo, float* hi)
{
	for (int c = 0; c < 3; ++c) { lo[c] = 1e30f; hi[c] = -1e30f; }
	unsigned points = 0;
	static const float CX[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
	static const float CY[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
	const unsigned STEPS = 8;
	for (unsigned e = 0; e < 4; ++e) {
		for (unsigned s = 0; s < STEPS; ++s) {
			const float f = static_cast<float>(s) / static_cast<float>(STEPS);
			const float x = CX[e] + (CX[(e + 1) & 3] - CX[e]) * f;
			const float y = CY[e] + (CY[(e + 1) & 3] - CY[e]) * f;
			float n[3], fa[3];
			if (!Mat_Transform_Point(inv, x, y, 0.0f, n) || !Mat_Transform_Point(inv, x, y, 1.0f, fa)) continue;
			const float d[3] = { fa[0] - n[0], fa[1] - n[1], fa[2] - n[2] };
			float t0 = 0.0f, t1 = t_limit;
			if (d[2] > 1e-6f || d[2] < -1e-6f) {
				const float ta = (z_lo - n[2]) / d[2];
				const float tb = (z_hi - n[2]) / d[2];
				const float tmin = (ta < tb) ? ta : tb;
				const float tmax = (ta < tb) ? tb : ta;
				if (tmin > t0) t0 = tmin;
				if (tmax < t1) t1 = tmax;
			}
			else if (n[2] < z_lo || n[2] > z_hi) continue;
			if (t0 > t1) continue;
			for (int k = 0; k < 2; ++k) {
				const float t = k ? t1 : t0;
				const float q[3] = { n[0] + d[0] * t, n[1] + d[1] * t, n[2] + d[2] * t };
				const float l[3] = { Sh_Dot3(q, right), Sh_Dot3(q, up), Sh_Dot3(q, fwd) };
				for (int c = 0; c < 3; ++c) {
					if (l[c] < lo[c]) lo[c] = l[c];
					if (l[c] > hi[c]) hi[c] = l[c];
				}
				++points;
			}
		}
	}
	return points >= 3;
}

// Edge length of a map over lo..hi in steps of about 9 % (texel size stays constant while panning) and origin snapped
// to the texel grid: a static caster lands on the same texels every frame, so the edge does not shimmer.
static bool Sh_Fit_Map(const float* lo, const float* hi, unsigned size, float* S, float* x0, float* y0)
{
	const float extent = ((hi[0] - lo[0]) > (hi[1] - lo[1])) ? (hi[0] - lo[0]) : (hi[1] - lo[1]);
	if (extent < 1.0f) return false;
	float edge = extent * 1.04f + 8.0f;
	edge = ::powf(2.0f, ::ceilf(::log2f(edge) * 8.0f) / 8.0f);
	const float texel = edge / static_cast<float>(size);
	*S = edge;
	*x0 = ::floorf((0.5f * (lo[0] + hi[0]) - 0.5f * edge) / texel) * texel;
	*y0 = ::floorf((0.5f * (lo[1] + hi[1]) - 0.5f * edge) / texel) * texel;
	return true;
}

bool RenderCore::Sh_Compute_Light(const RenderShadowParams& p)
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	float sun[3] = { p.sun_direction[0], p.sun_direction[1], p.sun_direction[2] };
	if (!Sh_Normalize3(sun) || sun[2] < SH_MIN_SUN_HEIGHT) return false;

	WW3DMatrixRaw vp, inv;
	Mat_Multiply(p.view, p.projection, vp);
	if (!Mat_Inverse(vp, inv)) return false;

	// Depth along the view axis: fraction between near and far plane up to which shadows are computed.
	float cn[3], cf[3];
	if (!Mat_Transform_Point(inv, 0.0f, 0.0f, 0.0f, cn) || !Mat_Transform_Point(inv, 0.0f, 0.0f, 1.0f, cf)) return false;
	const float axis[3] = { cf[0] - cn[0], cf[1] - cn[1], cf[2] - cn[2] };
	const float axis_len = ::sqrtf(Sh_Dot3(axis, axis));
	if (axis_len < 1e-3f) return false;
	const float distance = Sh_Distance();
	const float t_limit = (distance < axis_len) ? distance / axis_len : 1.0f;

	// Light space: fixed axes that depend only on the sun (no rotation with the camera, else the edges shimmer).
	const float fwd[3] = { -sun[0], -sun[1], -sun[2] };
	float ref[3] = { 0.0f, 0.0f, 1.0f };
	if (fwd[2] < -0.99f || fwd[2] > 0.99f) { ref[0] = 0.0f; ref[1] = 1.0f; ref[2] = 0.0f; }
	float right[3] = { ref[1] * fwd[2] - ref[2] * fwd[1], ref[2] * fwd[0] - ref[0] * fwd[2], ref[0] * fwd[1] - ref[1] * fwd[0] };
	if (!Sh_Normalize3(right)) return false;
	const float up[3] = { fwd[1] * right[2] - fwd[2] * right[1], fwd[2] * right[0] - fwd[0] * right[2], fwd[0] * right[1] - fwd[1] * right[0] };

	// Receiver area: from the lowest terrain point to above the highest, limited to the shadow distance.
	const float z_lo = p.ground_min_z - 5.0f;
	const float z_hi = p.ground_max_z + SH_ABOVE_GROUND;
	float lo[3], hi[3];
	if (!Sh_Footprint(inv, t_limit, z_lo, z_hi, right, up, fwd, lo, hi)) return false;
	float S, x0, y0;
	if (!Sh_Fit_Map(lo, hi, M->sh_size, &S, &x0, &y0)) return false;
	const float texel = S / static_cast<float>(M->sh_size);
	const float z0 = lo[2] - SH_DEPTH_MARGIN;
	const float range = (hi[2] + SH_DEPTH_MARGIN) - z0;

	WW3DMatrixRaw& L = M->sh_light;
	for (int i = 0; i < 3; ++i) {
		L.m[i][0] = right[i] * 2.0f / S;
		L.m[i][1] = up[i] * 2.0f / S;
		L.m[i][2] = fwd[i] / range;
		L.m[i][3] = 0.0f;
	}
	L.m[3][0] = -x0 * 2.0f / S - 1.0f;
	L.m[3][1] = -y0 * 2.0f / S - 1.0f;
	L.m[3][2] = -z0 / range;
	L.m[3][3] = 1.0f;
	M->sh_texel_world = texel;
	M->sh_depth_range = range;
	M->sh_sun[0] = sun[0]; M->sh_sun[1] = sun[1]; M->sh_sun[2] = sun[2];
	M->sh_stat_extent += S;

	// Near cascade: the same axes and depth mapping over the near part; its clip coordinates follow from the far map's
	// by a scale and an offset (x_near = x_far * scale + offset).
	M->sh_cascade = false;
	if ((fx.flags & RENDER_EFFECT_SHADOW_CASCADES) && Sh_Ensure_Near()) {
		const float near_distance = fx.shadow_distance * SH_CASCADE_NEAR;
		const float t_near = (near_distance < axis_len) ? near_distance / axis_len : 1.0f;
		float nlo[3], nhi[3], Sn, xn, yn;
		if (t_near < t_limit && Sh_Footprint(inv, t_near, z_lo, z_hi, right, up, fwd, nlo, nhi) &&
			Sh_Fit_Map(nlo, nhi, M->sh_size, &Sn, &xn, &yn) && Sn < S * 0.9f) {
			const float scale = S / Sn;
			M->sh_near_adjust[0] = scale;
			M->sh_near_adjust[1] = scale + 2.0f * (x0 - xn) / Sn - 1.0f;
			M->sh_near_adjust[2] = scale + 2.0f * (y0 - yn) / Sn - 1.0f;
			M->sh_cascade = true;
		}
	}
	return true;
}

void RenderCore::Shadow_Scene_Begin(const RenderShadowParams& params)
{
	M->sh_recording = false;
	M->sh_ter_valid = false;
	if (M->sh_count) Sh_Clear_Records();
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	if (!(fx.flags & RENDER_EFFECT_SHADOW_MAP) || !M->context || M->current_render_target) return;
	// Without a readable depth there is no depth copy: keep the classic shadows then.
	if (!Fx_Depth_Readable() || M->sh_failed || M->fx_failed) return;
	if (M->fx_opaque_frame == M->fx_frame + 1) return;		// second scene pass in this frame
	if (!Fx_Ensure_Resources() || !Sh_Ensure_Resources() || !Fx_Ensure_Depth_Copy()) return;
	M->sh_view = params.view;
	M->sh_proj = params.projection;
	M->sh_shadow_color[0] = params.shadow_color[0];
	M->sh_shadow_color[1] = params.shadow_color[1];
	M->sh_shadow_color[2] = params.shadow_color[2];
	if (!Sh_Compute_Light(params)) return;
	M->sh_recording = true;
}

bool RenderCore::Shadow_Maps_Active()
{
	return M->sh_recording;
}

void RenderCore::Set_Shadow_Caster_Only(bool enable)
{
	M->sh_caster_only = enable && M->sh_recording;
}

// --- Terrain shadows -------------------------------------------------------------------------------------
// The terrain is pre-lit by EA (slopes facing away are already dark); full shadows of the mountains on flanks
// and valleys would darken mountain maps a second time. So on the terrain (shadowmask.ps.hlsl, 4.): no sun
// shadow on slopes facing away; on lit ground the shadow cast by the terrain only applies with
// shadow_terrain_strength; shadows of objects on lit ground and all shadows on objects stay full.
// Two distinctions are needed:
//  - Casters: everything recorded up to Scene_Terrain_Done is terrain (HeightMapRenderObjClass::Render
//    draws it first) and goes into its own map (sh_ter_tex); the other casters stay in sh_tex.
//  - Receivers: depth copy right after the terrain. A pixel is terrain if the scene depth there is still the
//    terrain depth, i.e. nothing was drawn in front of it afterwards. Roads and decals write no depth and
//    count as ground; bridges, props, trees and units do not.
// The stencil cannot be used for this: the building occlusion markers use it
// (flushOccludedObjectsIntoStencil), and without shadow maps the shadow volumes do.

void RenderCore::Scene_Terrain_Done()
{
	if (!M->sh_recording || M->sh_ter_valid || M->current_render_target || !M->scene_depth) return;
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	if (fx.shadow_terrain_strength >= SH_TERRAIN_FULL) return;		// full strength: a single map
	if (!Sh_Ensure_Terrain()) return;
	Fx_Timestamp(Fx_Ts_Begin(FX_RANGE_TERRAIN_DEPTH));
	if (M->sample_count > 1) {
		if (!Fx_Resolve_Depth(M->sh_ter_depth_rtv)) {
			Fx_Timestamp(Fx_Ts_End(FX_RANGE_TERRAIN_DEPTH));
			return;
		}
	}
	else
		M->context->CopyResource(M->sh_ter_depth_tex, M->scene_depth);
	Fx_Timestamp(Fx_Ts_End(FX_RANGE_TERRAIN_DEPTH));
	++number_of_API_calls;
	M->sh_ter_split = M->sh_count;
	M->sh_ter_valid = true;
}

bool RenderCore::Sh_Ensure_Terrain()
{
	if (M->sh_ter_failed || !M->device || !M->sh_tex) return false;
	bool ok = true;
	if (!M->sh_ter_tex) {
		GPU_TEXTURE2D_DESC td;
		::ZeroMemory(&td, sizeof(td));
		td.Width = M->sh_size; td.Height = M->sh_size; td.MipLevels = 1; td.ArraySize = 1;
		td.Format = GPU_FORMAT_R32_TYPELESS;
		td.SampleDesc.Count = 1;
		td.Usage = GPU_USAGE_DEFAULT;
		td.BindFlags = GPU_BIND_DEPTH_STENCIL | GPU_BIND_SHADER_RESOURCE;
		ok = SUCCEEDED(M->device->CreateTexture2D(&td, nullptr, &M->sh_ter_tex));
		if (ok) {
			GPU_DEPTH_STENCIL_VIEW_DESC dv;
			::ZeroMemory(&dv, sizeof(dv));
			dv.Format = GPU_FORMAT_D32_FLOAT;
			dv.ViewDimension = GPU_DSV_DIMENSION_TEXTURE2D;
			ok = SUCCEEDED(M->device->CreateDepthStencilView(M->sh_ter_tex, &dv, &M->sh_ter_dsv));
			GPU_SHADER_RESOURCE_VIEW_DESC sv;
			::ZeroMemory(&sv, sizeof(sv));
			sv.Format = GPU_FORMAT_R32_FLOAT;
			sv.ViewDimension = GPU_SRV_DIMENSION_TEXTURE2D;
			sv.Texture2D.MipLevels = 1;
			ok = ok && SUCCEEDED(M->device->CreateShaderResourceView(M->sh_ter_tex, &sv, &M->sh_ter_srv));
		}
	}
	if (ok && !M->sh_ter_depth_tex)
		ok = Fx_Create_Depth_Copy(&M->sh_ter_depth_tex, &M->sh_ter_depth_rtv, &M->sh_ter_depth_srv);
	if (!ok) {
		WWDEBUG_SAY(("Terrain shadows: cannot create resources, terrain shadows stay full\n"));
		Sh_Release_Terrain();
		M->sh_ter_failed = true;
		return false;
	}
	return true;
}

// World-light matrix of the far map -> the near map: x and y scaled and shifted in light clip space (row vectors, so the
// columns change; w carries the shift).
void RenderCore::Sh_Near_Matrix(const WW3DMatrixRaw& far_matrix, WW3DMatrixRaw& out) const
{
	const float s = M->sh_near_adjust[0], ox = M->sh_near_adjust[1], oy = M->sh_near_adjust[2];
	out = far_matrix;
	for (int r = 0; r < 4; ++r) {
		out.m[r][0] = far_matrix.m[r][0] * s + far_matrix.m[r][3] * ox;
		out.m[r][1] = far_matrix.m[r][1] * s + far_matrix.m[r][3] * oy;
	}
}

bool RenderCore::Sh_Ensure_Near()
{
	if (M->sh_near_tex) return true;
	if (M->sh_near_failed || !M->device || !M->sh_tex) return false;
	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width = M->sh_size; td.Height = M->sh_size; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = GPU_FORMAT_R32_TYPELESS;
	td.SampleDesc.Count = 1;
	td.Usage = GPU_USAGE_DEFAULT;
	td.BindFlags = GPU_BIND_DEPTH_STENCIL | GPU_BIND_SHADER_RESOURCE;
	bool ok = SUCCEEDED(M->device->CreateTexture2D(&td, nullptr, &M->sh_near_tex));
	if (ok) {
		GPU_DEPTH_STENCIL_VIEW_DESC dv;
		::ZeroMemory(&dv, sizeof(dv));
		dv.Format = GPU_FORMAT_D32_FLOAT;
		dv.ViewDimension = GPU_DSV_DIMENSION_TEXTURE2D;
		ok = SUCCEEDED(M->device->CreateDepthStencilView(M->sh_near_tex, &dv, &M->sh_near_dsv));
		GPU_SHADER_RESOURCE_VIEW_DESC sv;
		::ZeroMemory(&sv, sizeof(sv));
		sv.Format = GPU_FORMAT_R32_FLOAT;
		sv.ViewDimension = GPU_SRV_DIMENSION_TEXTURE2D;
		sv.Texture2D.MipLevels = 1;
		ok = ok && SUCCEEDED(M->device->CreateShaderResourceView(M->sh_near_tex, &sv, &M->sh_near_srv));
	}
	if (!ok) {
		WWDEBUG_SAY(("Shadows: cannot create the near cascade, one map only\n"));
		Gpu_Release(M->sh_near_srv);
		Gpu_Release(M->sh_near_dsv);
		Gpu_Release(M->sh_near_tex);
		M->sh_near_failed = true;
	}
	return ok;
}

float RenderCore::Sh_Distance()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	return ((fx.flags & RENDER_EFFECT_SHADOW_CASCADES) && Sh_Ensure_Near()) ? fx.shadow_distance * SH_CASCADE_RANGE : fx.shadow_distance;
}

void RenderCore::Sh_Release_Terrain()
{
	M->sh_ter_valid = false;
	Gpu_Release(M->sh_ter_srv);
	Gpu_Release(M->sh_ter_dsv);
	Gpu_Release(M->sh_ter_tex);
	Gpu_Release(M->sh_ter_depth_srv);
	Gpu_Release(M->sh_ter_depth_rtv);
	Gpu_Release(M->sh_ter_depth_tex);
}

void RenderCore::Sh_Record(bool indexed, unsigned count, unsigned start, int base_vertex,
							 unsigned min_index, unsigned num_vertices, unsigned up_offset, unsigned up_stride)
{
	if (M->current_render_target || !count || !M->sh_casters) return;
	const unsigned* rs = M->render_states;
	if (!rs[WW3D_RS_ZENABLE] || !rs[WW3D_RS_ZWRITEENABLE] || !rs[WW3D_RS_COLORWRITEENABLE]) return;
	bool alpha_test = rs[WW3D_RS_ALPHATESTENABLE] != 0;
	if (rs[WW3D_RS_ALPHABLENDENABLE] && !alpha_test) return;		// translucent geometry casts no shadow
	// Only world geometry with the scene pass camera (no screen or 2D draws). The Z bias may change
	// the projection in [2][2].
	if (::memcmp(&M->transforms[WW3D_TS_VIEW], &M->sh_view, sizeof(WW3DMatrixRaw)) != 0) return;
	const WW3DMatrixRaw& proj = M->transforms[WW3D_TS_PROJECTION];
	if (proj.m[0][0] != M->sh_proj.m[0][0] || proj.m[1][1] != M->sh_proj.m[1][1] || proj.m[2][3] != M->sh_proj.m[2][3]) return;
	const bool trees = M->vertex_shader != nullptr;
	if (trees && M->vertex_shader->shadow_kind != 1) return;		// other custom vertex shaders (water): no caster
	const WW3DVertexDeclHandle decl = M->vertex_decl;
	if (!decl) return;
	bool has_position = false;
	for (unsigned i = 0; i < decl->element_count; ++i) {
		if (decl->elements[i].Usage == WW3D_DECLUSAGE_POSITION && decl->elements[i].Type == WW3D_DECLTYPE_FLOAT3) has_position = true;
	}
	if (!has_position) return;
	if (M->sh_count >= SH_MAX_CASTERS) {
		if (!M->sh_full_logged) {
			M->sh_full_logged = true;
			WWDEBUG_SAY(("Shadows: more than %u casters in one frame, the rest casts no shadow\n", (unsigned)SH_MAX_CASTERS));
		}
		return;
	}

	// GPU skins cast with their palette (the bone index is UV set 2), always solid.
	const bool skin = !trees && M->sk_count > 0 && Decl_Texcoord_Size(decl, 2) > 0;
	if (skin) {
		alpha_test = false;
		if (M->sh_skin_row_count + M->sk_count * 3 > SH_MAX_SKIN_ROWS) return;
	}
	// Alpha test: texture and UV set of stage 0 (without texgen); otherwise the draw casts as solid.
	GpuShaderResourceView* srv = nullptr;
	unsigned variant = trees ? static_cast<unsigned>(SH_VS_TREES) : (skin ? static_cast<unsigned>(SH_VS_SKIN) : 0u);
	float uv_matrix = 0.0f, uv_comps = 0.0f;
	if (alpha_test) {
		const WW3DTextureHandle t = M->textures[0];
		// a team colour view draws opaque (ffp.ps sets its alpha to 1), so it casts as solid
		srv = (t && t->srv && !t->team_color) ? t->srv : nullptr;
		if (srv && !trees) {
			const unsigned tci = M->stage_states[0][WW3D_TSS_TEXCOORDINDEX];
			const unsigned set = tci & 0xFFFF;
			const unsigned comps = (set < 4) ? Decl_Texcoord_Size(decl, set) : 0;
			if ((tci >> 16) != 0 || comps == 0) srv = nullptr;
			else {
				variant = 1 + set;
				uv_comps = static_cast<float>(comps);
				uv_matrix = (M->stage_states[0][WW3D_TSS_TEXTURETRANSFORMFLAGS] & 0xFF) ? 1.0f : 0.0f;
			}
		}
	}
	if (!srv) alpha_test = false;
	const WW3DVertexShaderHandle vs = Sh_Vertex_Shader(variant);
	if (!vs) return;
	GpuInputLayout* layout = static_cast<GpuInputLayout*>(Get_Input_Layout(decl, vs));
	if (!layout) return;

	// Buffers and the used range (for Sh_Before_Write).
	GpuBuffer* vb_gpu = nullptr;
	GpuBuffer* ib_gpu = nullptr;
	unsigned vb_offset = 0, vb_stride = 0;
	if (indexed) {
		const WW3DVertexBufferHandle vb = M->stream_vb;
		const WW3DIndexBufferHandle ib = M->indices;
		if (!vb || !ib || !vb->gpu || !ib->gpu || !M->stream_stride) return;
		vb_gpu = vb->gpu; ib_gpu = ib->gpu;
		vb_offset = M->stream_offset; vb_stride = M->stream_stride;
		long long first = 0, last = vb->size;
		if (num_vertices) {
			first = static_cast<long long>(vb_offset) + (static_cast<long long>(base_vertex) + min_index) * vb_stride;
			last = first + static_cast<long long>(num_vertices) * vb_stride;
			if (first < 0) first = 0;
			if (last > vb->size) last = vb->size;
			if (last < first) last = first;
		}
		if (vb->shadow_frame != M->sh_frame) {
			vb->shadow_frame = M->sh_frame;
			vb->shadow_lo = static_cast<unsigned>(first);
			vb->shadow_hi = static_cast<unsigned>(last);
		}
		else {
			if (static_cast<unsigned>(first) < vb->shadow_lo) vb->shadow_lo = static_cast<unsigned>(first);
			if (static_cast<unsigned>(last) > vb->shadow_hi) vb->shadow_hi = static_cast<unsigned>(last);
		}
		const unsigned ilo = start * 2;
		unsigned ihi = (start + count) * 2;
		if (ihi > ib->size) ihi = ib->size;
		if (ib->shadow_frame != M->sh_frame) {
			ib->shadow_frame = M->sh_frame;
			ib->shadow_lo = ilo;
			ib->shadow_hi = ihi;
		}
		else {
			if (ilo < ib->shadow_lo) ib->shadow_lo = ilo;
			if (ihi > ib->shadow_hi) ib->shadow_hi = ihi;
		}
	}
	else {
		if (!M->up_ring || !up_stride) return;
		vb_gpu = M->up_ring;
		vb_offset = up_offset; vb_stride = up_stride;
		const unsigned ulo = up_offset, uhi = up_offset + count * up_stride;
		if (M->sh_up_frame != M->sh_frame) {
			M->sh_up_frame = M->sh_frame;
			M->sh_up_lo = ulo;
			M->sh_up_hi = uhi;
		}
		else {
			if (ulo < M->sh_up_lo) M->sh_up_lo = ulo;
			if (uhi > M->sh_up_hi) M->sh_up_hi = uhi;
		}
	}

	static_assert(FFP_SHADOW_SWAY == sizeof(RenderTreesVS::sway) / sizeof(Vector4), "shadow sway table matches the tree layout");
	unsigned sway = 0;
	if (trees) {
		// The tree buffer draws once per tree texture with the same sway constants; equal sets share a slot.
		const float* sway_src = &M->vs_constants[RENDER_PIECE_REG(RenderTreesVS, sway) * 4];
		if (M->sh_sway_count > 0 &&
			::memcmp(&M->sh_sway[(M->sh_sway_count - 1) * FFP_SHADOW_SWAY], sway_src, FFP_SHADOW_SWAY * sizeof(FFPFloat4)) == 0) {
			sway = M->sh_sway_count - 1;
		}
		else {
			if (M->sh_sway_count >= SH_MAX_SWAY) return;
			sway = M->sh_sway_count++;
			::memcpy(&M->sh_sway[sway * FFP_SHADOW_SWAY], sway_src, FFP_SHADOW_SWAY * sizeof(FFPFloat4));
		}
	}

	RenderShadowCaster& r = M->sh_casters[M->sh_count++];
	r.skin_first = 0;
	r.skin_count = 0;
	if (skin) {
		r.skin_first = M->sh_skin_row_count;
		r.skin_count = M->sk_count;
		::memcpy(&M->sh_skin_rows[r.skin_first], M->sk_cb_data.bones, M->sk_count * 3 * sizeof(FFPFloat4));
		M->sh_skin_row_count += M->sk_count * 3;
	}
	r.vb = vb_gpu;
	r.ib = ib_gpu;
	r.layout = layout;
	layout->AddRef();
	// Indexed solid and alpha-tested casters can be merged with their neighbours (Sh_Draw_Casters).
	r.inst_layout = nullptr;
	if (indexed && !trees && !skin) {
		const WW3DVertexShaderHandle ivs = Sh_Vertex_Shader(SH_VS_INSTANCED + variant);
		GpuInputLayout* il = ivs ? static_cast<GpuInputLayout*>(Get_Input_Layout(decl, ivs)) : nullptr;
		if (il) {
			r.inst_layout = il;
			il->AddRef();
		}
	}
	r.alpha_srv = alpha_test ? srv : nullptr;
	if (r.alpha_srv) r.alpha_srv->AddRef();
	r.vb_offset = vb_offset;
	r.vb_stride = vb_stride;
	// patch draws of the terrain are replayed as their triangles (the caster stays untessellated)
	r.topology = (M->bound_topology == static_cast<unsigned>(GPU_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST)) ?
		static_cast<unsigned>(GPU_PRIMITIVE_TOPOLOGY_TRIANGLELIST) : M->bound_topology;
	r.count = count;
	r.start = start;
	r.base_vertex = base_vertex;
	r.vs = variant;
	r.sway = sway;
	Mat_Multiply(M->world_transform, M->sh_light, r.world_light);
	r.tex_matrix = M->transforms[WW3D_TS_TEXTURE0];
	r.uv_matrix = uv_matrix;
	r.uv_comps = uv_comps;
	r.alpha_ref = static_cast<float>(rs[WW3D_RS_ALPHAREF] & 0xFF);
	r.alpha_func = static_cast<float>(rs[WW3D_RS_ALPHAFUNC]);
}

void RenderCore::Sh_Before_Write(WW3DBufferCommon* b, unsigned offset, unsigned size, bool discard, bool index)
{
	if (!b || !b->gpu || b->shadow_frame != M->sh_frame || !M->sh_count) return;
	if (b->dynamic) {
			if (!discard) return;			// NO_OVERWRITE: used ranges stay untouched
	}
	else if (!discard) {
		const unsigned lo = offset;
		const unsigned hi = size ? offset + size : b->size;
		if (hi <= b->shadow_lo || lo >= b->shadow_hi) return;
	}
	Sh_Preserve(b->gpu, b->shadow_lo, b->shadow_hi, index);
	b->shadow_frame = 0;
}

void RenderCore::Sh_Preserve(void* buffer, unsigned lo, unsigned hi, bool index)
{
	GpuBuffer* src = static_cast<GpuBuffer*>(buffer);
	if (!src || hi <= lo || !M->device) return;
	// Pick a copy of the right kind that is large enough, or create one. The first sh_copy_used
	// entries are taken in this frame.
	RenderShadowCopy* copy = nullptr;
	for (unsigned i = M->sh_copy_used; i < SH_MAX_COPIES && !copy; ++i) {
		RenderShadowCopy& c = M->sh_copies[i];
		if (!c.buffer || c.index != index || c.size < hi) continue;
		const RenderShadowCopy t = c; c = M->sh_copies[M->sh_copy_used]; M->sh_copies[M->sh_copy_used] = t;
		copy = &M->sh_copies[M->sh_copy_used];
	}
	if (!copy && M->sh_copy_used < SH_MAX_COPIES) {
		unsigned slot = M->sh_copy_used;
		for (unsigned i = M->sh_copy_used; i < SH_MAX_COPIES; ++i) {
			if (!M->sh_copies[i].buffer) { slot = i; break; }
		}
		RenderShadowCopy& c = M->sh_copies[slot];
		Gpu_Release(c.buffer);
		GPU_BUFFER_DESC bd;
		::ZeroMemory(&bd, sizeof(bd));
		bd.ByteWidth = (hi + 0xFFFFu) & ~0xFFFFu;		// in 64 KiB steps so copies can be reused
		bd.Usage = GPU_USAGE_DEFAULT;
		bd.BindFlags = index ? GPU_BIND_INDEX_BUFFER : GPU_BIND_VERTEX_BUFFER;
		if (SUCCEEDED(M->device->CreateBuffer(&bd, nullptr, &c.buffer))) {
			c.size = bd.ByteWidth;
			c.index = index;
			const RenderShadowCopy t = c; c = M->sh_copies[M->sh_copy_used]; M->sh_copies[M->sh_copy_used] = t;
			copy = &M->sh_copies[M->sh_copy_used];
		}
		else {
			c.buffer = nullptr;
			c.size = 0;
		}
	}
	if (!copy) {
		// Out of copies: the affected casters drop out for this frame.
		for (unsigned i = 0; i < M->sh_count; ++i) {
			RenderShadowCaster& r = M->sh_casters[i];
			if (r.vb == src || r.ib == src) r.count = 0;
		}
		if (!M->sh_copies_logged) {
			M->sh_copies_logged = true;
			WWDEBUG_SAY(("Shadows: no buffer copy left, some casters dropped\n"));
		}
		return;
	}
	++M->sh_copy_used;
	GPU_BOX box;
	box.left = lo; box.right = hi; box.top = 0; box.bottom = 1; box.front = 0; box.back = 1;
	M->context->CopySubresourceRegion(copy->buffer, 0, lo, 0, 0, src, 0, &box);
	for (unsigned i = 0; i < M->sh_count; ++i) {
		RenderShadowCaster& r = M->sh_casters[i];
		if (r.vb == src) r.vb = copy->buffer;
		if (r.ib == src) r.ib = copy->buffer;
	}
	++M->sh_stat_copies;
	number_of_API_calls++;
}

void RenderCore::Sh_Render_Map()
{
	GpuContext* c = M->context;
	Fx_Timestamp(Fx_Ts_Begin(FX_RANGE_SHADOW_MAP));
	GpuShaderResourceView* nulls[3] = { nullptr, nullptr, nullptr };
	c->PSSetShaderResources(0, 3, nulls);
	GPU_VIEWPORT v;
	v.TopLeftX = 0.0f; v.TopLeftY = 0.0f;
	v.Width = static_cast<FLOAT>(M->sh_size); v.Height = static_cast<FLOAT>(M->sh_size);
	v.MinDepth = 0.0f; v.MaxDepth = 1.0f;
	c->RSSetViewports(1, &v);
	c->RSSetState(M->sh_raster);
	c->OMSetDepthStencilState(M->sh_depth, 0);
	c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	c->VSSetConstantBuffers(RENDER_SLOT_CB_SHADOW_DRAW, 1, &M->sh_cb_draw);
	c->PSSetConstantBuffers(RENDER_SLOT_CB_SHADOW_DRAW, 1, &M->sh_cb_draw);
	c->PSSetSamplers(0, 1, &M->sh_alpha_sampler);
	number_of_API_calls += 10;

	// The world-light matrices of all casters, once for the instanced draws (index = caster).
	bool instanced = false;
	if (M->sh_inst && M->sh_count) {
		GPU_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(c->Map(M->sh_inst, 0, GPU_MAP_WRITE_DISCARD, 0, &mapped))) {
			WW3DMatrixRaw* out = static_cast<WW3DMatrixRaw*>(mapped.pData);
			for (unsigned i = 0; i < M->sh_count; ++i) out[i] = M->sh_casters[i].world_light;
			Ud_Upload("shadow", M->sh_inst, 0, out, M->sh_count * static_cast<unsigned>(sizeof(WW3DMatrixRaw)), true);
			c->Unmap(M->sh_inst, 0);
			const UINT stride = static_cast<UINT>(sizeof(WW3DMatrixRaw)), offset = 0;
			c->IASetVertexBuffers(1, 1, &M->sh_inst, &stride, &offset);
			number_of_API_calls += 2;
			instanced = true;
		}
	}
	M->sh_instanced = instanced;

	// With a separate terrain map, draw the terrain casters into their own map first, then the rest
	// into sh_tex; otherwise all go into sh_tex.
	unsigned drawn = 0;
	const bool split = M->sh_ter_valid && M->sh_ter_dsv;
	if (split) {
		c->OMSetRenderTargets(0, nullptr, M->sh_ter_dsv);
		c->ClearDepthStencilView(M->sh_ter_dsv, GPU_CLEAR_DEPTH, 1.0f, 0);
		drawn += Sh_Draw_Casters(0, M->sh_ter_split);
		number_of_API_calls += 2;
	}
	c->OMSetRenderTargets(0, nullptr, M->sh_dsv);
	c->ClearDepthStencilView(M->sh_dsv, GPU_CLEAR_DEPTH, 1.0f, 0);
	drawn += Sh_Draw_Casters(split ? M->sh_ter_split : 0, M->sh_count);
	number_of_API_calls += 2;
	// Near cascade: the same casters (without the terrain when it has its own map) with the adjusted matrices.
	if (M->sh_cascade && M->sh_near_dsv) {
		const unsigned first = split ? M->sh_ter_split : 0;
		if (instanced) {
			GPU_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(c->Map(M->sh_inst, 0, GPU_MAP_WRITE_DISCARD, 0, &mapped))) {
				WW3DMatrixRaw* out = static_cast<WW3DMatrixRaw*>(mapped.pData);
				for (unsigned i = 0; i < M->sh_count; ++i) Sh_Near_Matrix(M->sh_casters[i].world_light, out[i]);
				Ud_Upload("shadow", M->sh_inst, 0, out, M->sh_count * static_cast<unsigned>(sizeof(WW3DMatrixRaw)), true);
				c->Unmap(M->sh_inst, 0);
				++number_of_API_calls;
			}
			else M->sh_instanced = false;
		}
		c->OMSetRenderTargets(0, nullptr, M->sh_near_dsv);
		c->ClearDepthStencilView(M->sh_near_dsv, GPU_CLEAR_DEPTH, 1.0f, 0);
		M->sh_adjust_active = true;
		drawn += Sh_Draw_Casters(first, M->sh_count);
		M->sh_adjust_active = false;
		number_of_API_calls += 2;
	}
	M->sh_stat_casters += drawn;
	++M->sh_stat_frames;
	Fx_Timestamp(Fx_Ts_End(FX_RANGE_SHADOW_MAP));

	// Restore the engine's target, viewport and input buffers: RenderDevice rebinds vertex and index
	// buffers only when they change.
	GpuBuffer* engine_vb = M->stream_vb ? M->stream_vb->gpu : nullptr;
	UINT s = M->stream_stride, o = M->stream_offset;
	c->IASetVertexBuffers(0, 1, &engine_vb, &s, &o);
	c->IASetIndexBuffer(M->indices ? M->indices->gpu : nullptr, GPU_FORMAT_R16_UINT, 0);
	c->PSSetShaderResources(0, 1, nulls);
	if (instanced) {
		GpuBuffer* no_vb = nullptr;
		const UINT zero = 0;
		c->IASetVertexBuffers(1, 1, &no_vb, &zero, &zero);
	}
	number_of_API_calls += 3;
	Fx_Restore_Bindings();
}

// Consecutive casters that draw the same geometry with the same state differ only in their matrix.
static bool Sh_Same_Draw(const RenderShadowCaster& a, const RenderShadowCaster& b)
{
	return a.vb == b.vb && a.ib == b.ib && a.layout == b.layout && a.alpha_srv == b.alpha_srv && a.vb_offset == b.vb_offset &&
		a.vb_stride == b.vb_stride && a.topology == b.topology && a.count == b.count && a.start == b.start &&
		a.base_vertex == b.base_vertex && a.vs == b.vs && a.inst_layout == b.inst_layout && a.skin_count == 0 && b.skin_count == 0 &&
		a.uv_matrix == b.uv_matrix && a.uv_comps == b.uv_comps && a.alpha_ref == b.alpha_ref && a.alpha_func == b.alpha_func &&
		(!a.alpha_srv || ::memcmp(&a.tex_matrix, &b.tex_matrix, sizeof(a.tex_matrix)) == 0);
}

unsigned RenderCore::Sh_Draw_Casters(unsigned first, unsigned last)
{
	GpuContext* c = M->context;
	GpuInputLayout* layout = nullptr;
	unsigned topology = 0xFFFFFFFF;
	GpuVertexShader* vs = nullptr;
	GpuPixelShader* ps = reinterpret_cast<GpuPixelShader*>(1);
	GpuBuffer* vb = nullptr;
	unsigned vb_offset = 0xFFFFFFFF, vb_stride = 0;
	GpuBuffer* ib = reinterpret_cast<GpuBuffer*>(1);
	GpuShaderResourceView* srv = nullptr;
	bool uploaded = false;
	unsigned generation = M->cbr_generation;
	FFPShadowDrawCB& d = M->sh_draw_data;
	unsigned drawn = 0;

	if (last > M->sh_count) last = M->sh_count;
	for (unsigned i = first; i < last; ++i) {
		const RenderShadowCaster& r = M->sh_casters[i];
		if (!r.count || !r.vb || !M->sh_vs[r.vs]) continue;
		// Instanced: this caster and the following ones with the same draw, each with its matrix from vertex
		// stream 1. Trees (sway), skins (palette per caster) and draws without index buffer stay single.
		const bool can_instance = M->sh_instanced && r.inst_layout && M->sh_vs[SH_VS_INSTANCED + r.vs];
		unsigned instances = 1;
		if (can_instance) {
			while (i + instances < last && Sh_Same_Draw(r, M->sh_casters[i + instances])) ++instances;
		}
		GpuInputLayout* want_layout = can_instance ? r.inst_layout : r.layout;
		GpuVertexShader* want_vs = M->sh_vs[can_instance ? SH_VS_INSTANCED + r.vs : r.vs]->shader;
		if (want_layout != layout) { c->IASetInputLayout(want_layout); layout = want_layout; ++number_of_API_calls; }
		if (r.topology != topology) {
			c->IASetPrimitiveTopology(static_cast<GPU_PRIMITIVE_TOPOLOGY>(r.topology));
			topology = r.topology; ++number_of_API_calls;
		}
		if (want_vs != vs) { vs = want_vs; c->VSSetShader(vs, nullptr, 0); ++number_of_API_calls; }
		GpuPixelShader* want_ps = r.alpha_srv ? M->sh_alpha_ps : nullptr;
		if (want_ps != ps) { ps = want_ps; c->PSSetShader(ps, nullptr, 0); ++number_of_API_calls; }
		if (r.alpha_srv && r.alpha_srv != srv) { srv = r.alpha_srv; c->PSSetShaderResources(0, 1, &srv); ++number_of_API_calls; }
		if (r.vb != vb || r.vb_offset != vb_offset || r.vb_stride != vb_stride) {
			vb = r.vb; vb_offset = r.vb_offset; vb_stride = r.vb_stride;
			UINT s = vb_stride, o = vb_offset;
			c->IASetVertexBuffers(0, 1, &vb, &s, &o);
			++number_of_API_calls;
		}
		if (r.ib && r.ib != ib) { ib = r.ib; c->IASetIndexBuffer(ib, GPU_FORMAT_R16_UINT, 0); ++number_of_API_calls; }

		FFPShadowDrawCB n;
		::ZeroMemory(&n, sizeof(n));
		if (!can_instance) {
			if (M->sh_adjust_active) Sh_Near_Matrix(r.world_light, n.worldLight);
			else n.worldLight = r.world_light;
		}
		if (r.alpha_srv) {
			n.texMatrix = r.tex_matrix;
			Fx_Set_Float4(n.uv, r.uv_matrix, r.uv_comps, 0.0f, 0.0f);
			Fx_Set_Float4(n.alpha, r.alpha_ref, r.alpha_func, 0.0f, 0.0f);
		}
		if (r.vs == SH_VS_TREES) ::memcpy(n.sway, &M->sh_sway[r.sway * FFP_SHADOW_SWAY], sizeof(n.sway));
		// The draw constants and a skin palette must land in the same ring generation.
		if (M->cbr_context) Cbr_Reserve(Cbr_Size(sizeof(FFPShadowDrawCB), 0) + (r.skin_count ? Cbr_Size(sizeof(FFPSkinCB), 0) : 0u));
		if (!uploaded || generation != M->cbr_generation || ::memcmp(&n, &d, sizeof(n)) != 0) {
			d = n;
			UINT firstConst = 0, count = 0;
			if (M->cbr_context && Cbr_Write(&d, sizeof(d), 0, &firstConst, &count)) {
				M->cbr_context->VSSetConstantBuffers1(RENDER_SLOT_CB_SHADOW_DRAW, 1, &M->cbr_buffer, &firstConst, &count);
				M->cbr_context->PSSetConstantBuffers1(RENDER_SLOT_CB_SHADOW_DRAW, 1, &M->cbr_buffer, &firstConst, &count);
				number_of_API_calls += 2;
			}
			else {
				if (M->cbr_context) {
					// The ring failed: back to the own buffer for the rest of the pass.
					c->VSSetConstantBuffers(RENDER_SLOT_CB_SHADOW_DRAW, 1, &M->sh_cb_draw);
					c->PSSetConstantBuffers(RENDER_SLOT_CB_SHADOW_DRAW, 1, &M->sh_cb_draw);
					number_of_API_calls += 2;
				}
				Upload_Constant_Buffer(c, M->sh_cb_draw, &d, sizeof(d));
			}
			generation = M->cbr_generation;
			uploaded = true;
		}
		if (r.skin_count) {
			// Palette for shadow.vs.hlsl (SHADOW_SKIN); the scene draws rebind b9 afterwards.
			static FFPSkinCB palette;		// 3 KB, kept off the stack
			Fx_Set_Float4(palette.info, (float)r.skin_count, 0.0f, 0.0f, 0.0f);
			::memcpy(palette.bones, &M->sh_skin_rows[r.skin_first], r.skin_count * 3 * sizeof(FFPFloat4));
			UINT firstConst = 0, count = 0;
			if (M->cbr_context && Cbr_Write(&palette, 16 + r.skin_count * 3 * 16, sizeof(FFPSkinCB), &firstConst, &count)) {
				M->cbr_context->VSSetConstantBuffers1(RENDER_SLOT_CB_SKIN, 1, &M->cbr_buffer, &firstConst, &count);
				M->bound_sk_cb = M->cbr_buffer;
				++number_of_API_calls;
			}
			else {
				Upload_Constant_Buffer(c, M->sh_cb_skin, &palette, 16 + r.skin_count * 3 * 16);
				if (M->bound_sk_cb != M->sh_cb_skin) {
					c->VSSetConstantBuffers(RENDER_SLOT_CB_SKIN, 1, &M->sh_cb_skin);
					M->bound_sk_cb = M->sh_cb_skin;
					++number_of_API_calls;
				}
			}
		}
		if (can_instance) c->DrawIndexedInstanced(r.count, instances, r.start, r.base_vertex, i);
		else if (r.ib)    c->DrawIndexed(r.count, r.start, r.base_vertex);
		else              c->Draw(r.count, 0);
		++number_of_API_calls;
		drawn += instances;
		M->sh_stat_draws += 1.0;
		i += instances - 1;
	}
	return drawn;
}

void RenderCore::Sh_Apply()
{
	if (!M->fx_depth_valid || !M->fx_depth_handle) return;
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	Fx_Timestamp(Fx_Ts_Begin(FX_RANGE_SHADOW_MASK));

	FFPShadowApplyCB a;
	::ZeroMemory(&a, sizeof(a));
	WW3DMatrixRaw inv_view;
	if (!Mat_Inverse(M->sh_view, inv_view)) return;
	Mat_Multiply(inv_view, M->sh_light, a.viewToLight);
	// Sun direction into view space (rotation only, row vector times the 3x3 of the view matrix).
	float sv[3];
	for (int j = 0; j < 3; ++j)
		sv[j] = M->sh_sun[0] * M->sh_view.m[0][j] + M->sh_sun[1] * M->sh_view.m[1][j] + M->sh_sun[2] * M->sh_view.m[2][j];
	Sh_Normalize3(sv);
	const float texel = M->sh_texel_world;
	Fx_Set_Float4(a.sunView, sv[0], sv[1], sv[2], SH_NORMAL_OFFSET * texel);
	Fx_Set_Float4(a.color, M->sh_shadow_color[0], M->sh_shadow_color[1], M->sh_shadow_color[2],
				  (SH_DEPTH_BIAS_WORLD + SH_DEPTH_BIAS_TEXEL * texel) / M->sh_depth_range);
	Fx_Set_Float4(a.map, 1.0f / static_cast<float>(M->sh_size), SH_PCF_SPACING, SH_EDGE_FADE, SH_BACK_FULL);
	const float dist = Sh_Distance();
	// The tessellated terrain lies up to the largest displacement off its (untessellated) casters: extra bias in the
	// terrain map.
	float tess_bias = 0.0f;
	if (M->tt_drawn && M->sh_depth_range > 0.0f)
		tess_bias = Tt_Max_Displacement(fx, M->tt_rock_amp) / M->sh_depth_range;
	Fx_Set_Float4(a.fade, dist, 1.0f / (SH_DISTANCE_FADE * dist), tess_bias, 0.0f);
	// Share of terrain shadows on the terrain, only with a separate terrain map.
	const bool split = M->sh_ter_valid && M->sh_ter_srv && M->sh_ter_depth_srv;
	float terrain = fx.shadow_terrain_strength;
	if (terrain < 0.0f) terrain = 0.0f;
	if (terrain > 1.0f) terrain = 1.0f;
	Fx_Set_Float4(a.terrain, split ? terrain : 1.0f, split ? 1.0f : 0.0f, SH_TERRAIN_TOL, SH_TERRAIN_TOL_REL);
	if ((fx.flags & RENDER_EFFECT_SOFT_SHADOWS) && fx.shadow_soft > 0.0f && texel > 0.0f) {
		float widest = SH_SOFT_MAX_WORLD / texel;
		if (widest > SH_SOFT_MAX_TEXELS) widest = SH_SOFT_MAX_TEXELS;
		if (widest < SH_SOFT_MIN_TEXELS) widest = SH_SOFT_MIN_TEXELS;
		Fx_Set_Float4(a.soft, M->sh_depth_range * SH_SOFT_LIGHT * fx.shadow_soft / texel, SH_SOFT_MIN_TEXELS, widest, 1.0f);
		a.fade.w = SH_SOFT_SLOPE * texel / M->sh_depth_range;
	}
	if (fx.flags & RENDER_EFFECT_CONTACT_SHADOWS)
		Fx_Set_Float4(a.contact, SH_CONTACT_LENGTH, SH_CONTACT_THICK, SH_CONTACT_BIAS, 1.0f);
	const bool cascade = M->sh_cascade && M->sh_near_srv;
	if (cascade)
		Fx_Set_Float4(a.cascade, M->sh_near_adjust[0], M->sh_near_adjust[1], M->sh_near_adjust[2], SH_CASCADE_BLEND);
	Upload_Constant_Buffer(M->context, M->sh_cb_apply, &a, sizeof(a));
	M->sh_apply_data = a;
	M->sh_apply_valid = true;
	if (M->fx_cb_dirty) Fx_Upload_Constants();		// depth parameters from Fx_Copy_Depth

	GpuContext* c = M->context;
	c->PSSetConstantBuffers(RENDER_SLOT_CB_SHADOW_APPLY, 1, &M->sh_cb_apply);
	// Without the near cascade its slot gets the main map: the shader does not read it then (weight 0), but Vulkan checks
	// that a comparison sampler only meets depth formats, and an empty slot would get the colour placeholder.
	GpuShaderResourceView* maps[4] = { M->sh_srv, split ? M->sh_ter_srv : nullptr, split ? M->sh_ter_depth_srv : nullptr,
									   cascade ? M->sh_near_srv : M->sh_srv };
	c->PSSetShaderResources(2, 4, maps);
	c->PSSetSamplers(1, 1, &M->sh_cmp);
	Fx_Full_Screen_Pass(M->scene_rtv, M->viewport, M->sh_mask_ps, nullptr, M->fx_depth_handle->srv, M->fx_blend_mul);
	GpuShaderResourceView* null_srv[4] = { nullptr, nullptr, nullptr, nullptr };
	c->PSSetShaderResources(2, 4, null_srv);
	number_of_API_calls += 4;
	Fx_Restore_Bindings();
	Fx_Timestamp(Fx_Ts_End(FX_RANGE_SHADOW_MASK));
}

// Atmosphere (RENDER_EFFECT_ATMOSPHERE, shadowmask.ps.hlsl with ATMOSPHERE): height fog and sun shafts, blended over
// the opaque scene with the depth copy and the shadow map of this scene pass. Its GPU time counts in "tiefe_ssao".
void RenderCore::Sh_Atmosphere()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	if (!(fx.flags & RENDER_EFFECT_ATMOSPHERE) || !M->sh_atmo_ps || !M->fx_blend_premul) return;
	const float fog = (fx.height_fog > 2.0f) ? 2.0f : fx.height_fog;
	const float shafts = (fx.sun_shafts > 2.0f) ? 2.0f : fx.sun_shafts;
	if (fog <= 0.0f && shafts <= 0.0f) return;
	if (!M->fx_depth_valid || !M->fx_depth_handle) return;

	FFPShadowApplyCB a = M->sh_apply_data;
	WW3DMatrixRaw inv_view;
	if (!Mat_Inverse(M->sh_view, inv_view)) return;
	// world z of a view space point (row vector times the inverse view)
	Fx_Set_Float4(a.worldZ, inv_view.m[0][2], inv_view.m[1][2], inv_view.m[2][2], inv_view.m[3][2]);
	// The shafts scatter in the same medium; without fog they use a thin one of their own.
	const float density = ATMO_FOG_DENSITY * ((fog > 0.0f) ? fog : ATMO_SHAFT_MEDIUM);
	Fx_Set_Float4(a.atmo, density, 1.0f / ATMO_SCALE_HEIGHT, M->tr_min_height, shafts * ATMO_SHAFT_GAIN);
	const FFPFloat4& amb = M->frame_cb.ambient;
	const FFPFloat4& sun = M->frame_cb.sunDiffuse[0];
	float fc[3];
	for (int k = 0; k < 3; ++k) {
		const float v = (amb.x * (k == 0) + amb.y * (k == 1) + amb.z * (k == 2)) +
			ATMO_FOG_SUN * (sun.x * (k == 0) + sun.y * (k == 1) + sun.z * (k == 2));
		fc[k] = ((v > 1.0f) ? 1.0f : v) * ATMO_FOG_TINT[k];
	}
	Fx_Set_Float4(a.fogColor, fc[0], fc[1], fc[2], (fog > 0.0f) ? ATMO_FOG_MAX : 0.0f);
	Fx_Set_Float4(a.sunColor, sun.x, sun.y, sun.z, ATMO_SHAFT_G);
	Upload_Constant_Buffer(M->context, M->sh_cb_apply, &a, sizeof(a));
	if (M->fx_cb_dirty) Fx_Upload_Constants();

	GpuContext* c = M->context;
	c->PSSetConstantBuffers(RENDER_SLOT_CB_SHADOW_APPLY, 1, &M->sh_cb_apply);
	c->PSSetShaderResources(2, 1, &M->sh_srv);
	c->PSSetSamplers(1, 1, &M->sh_cmp);
	Fx_Full_Screen_Pass(M->scene_rtv, M->viewport, M->sh_atmo_ps, nullptr, M->fx_depth_handle->srv, M->fx_blend_premul);
	GpuShaderResourceView* null_srv = nullptr;
	c->PSSetShaderResources(2, 1, &null_srv);
	number_of_API_calls += 4;
	Fx_Restore_Bindings();
	M->at_stat_frames += 1.0;
}
