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

// rendercore_terrain.cpp -- Direct3D 11 backend: terrain detail, rock layer, relief, tessellation, terrain lights.

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
// 10. Terrain detail: detail layer from CC0 photo scans on the terrain
// ==================================================================================================
//
// Only with "-d3d11Effects terraindetail" (also "detailboden"; Options.ini "D3D11Effects", detail level); otherwise
// Prepare_Draw never picks the permutation and no resource is created, so the image stays identical.
//
//  - Set_Terrain_Detail_Table (from WorldHeightMap::getTerrainTexture): which terrain texture sits in which
//    atlas cell, with its detail material (Data/INI/BodenDetail.ini) -> cbTerrainDetail.
//  - Set_Terrain_Detail(true/false) (from HeightMapRenderObjClass::Render around the base pass and the
//    three-way blends): draws in between whose pixel shader is a terrain shader (terrain*.ps,
//    roadnoise2.ps) use its permutation TERRAIN_DETAIL (terraindetail.hlsli).
//  - Detail textures: ProjectData\Textures\BodenDetail\detail_a.dds and detail_b.dds.
//  - GPU timing: timestamps 12/13 around the first terrain section per frame whenever any effect is on,
//    so runs with and without detail are comparable.

static const char TD_TEXTURE_A[] = "ProjectData\\Textures\\BodenDetail\\detail_a.dds";
static const char TD_TEXTURE_B[] = "ProjectData\\Textures\\BodenDetail\\detail_b.dds";

// Fade in by screen pixels per tile (one tile = 20 world units). Default camera at 1080p: about 55 to 80
// pixels per tile (more at the bottom than the top); close camera (zoom 0.35): 150 to 220. No detail below
// TD_FADE_START, full detail from TD_FADE_FULL.
static const float TD_FADE_START = 40.0f;
static const float TD_FADE_FULL = 110.0f;
// Second layer: rotated by 90 degrees, TD_LAYER2_SCALE times denser, weight TD_LAYER2_WEIGHT (0 = off).
static const float TD_LAYER2_SCALE = 3.0f;
static const float TD_LAYER2_WEIGHT = 0.0f;

// Flat snow (RENDER_EFFECT_FLAT_SNOW): detail material of snow ground (BodenDetail.ini "schnee"); full snow up to
// TR_SNOW_FLAT_FULL (1 - n.z, about 11 degrees), none from TR_SNOW_FLAT_NONE (about 20 degrees).
static const unsigned TR_SNOW_MATERIAL = 7;
static const float TR_SNOW_FLAT_FULL = 0.02f;
static const float TR_SNOW_FLAT_NONE = 0.06f;

/// Loads a detail texture: DDS, 32 bit RGBA (R8G8B8A8), full mip chain.
/// Read directly from the exe folder (ProjectData lies there loose): the game's file factory prefixes all
/// image names (.dds, .tga) with "Art/Textures/" (W3DFileSystem), so a ProjectData path would be lost there.
static bool Td_Load_Texture(GpuDevice* device, const char* name, GpuTexture2D** tex, GpuShaderResourceView** srv)
{
	*tex = nullptr;
	*srv = nullptr;
	char path[MAX_PATH];
	const DWORD n = ::GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) return false;
	char* slash = strrchr(path, '\\');
	if (!slash || static_cast<size_t>(slash + 1 - path) + strlen(name) >= MAX_PATH) return false;
	strcpy(slash + 1, name);
	std::vector<unsigned char> data;
	HANDLE file = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER size;
		if (::GetFileSizeEx(file, &size) && size.QuadPart > 128 && size.QuadPart < (64 << 20)) {
			data.resize(static_cast<size_t>(size.QuadPart));
			DWORD got = 0;
			if (!::ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &got, nullptr) || got != data.size())
				data.clear();
		}
		::CloseHandle(file);
	}
	if (data.size() < 128 || ::memcmp(data.data(), "DDS ", 4) != 0) {
		WWDEBUG_SAY(("Terrain detail: %s missing or not a DDS\n", name));
		return false;
	}
	unsigned h[31];			// DDS_HEADER without the magic
	::memcpy(h, data.data() + 4, sizeof(h));
	const unsigned height = h[2], width = h[3];
	const unsigned levels = h[6] ? h[6] : 1;
	if (h[21] != 32 || h[22] != 0x000000FFu || h[23] != 0x0000FF00u || h[24] != 0x00FF0000u || h[25] != 0xFF000000u ||
		!width || !height || width > 4096 || height > 4096 || levels > 13) {
		WWDEBUG_SAY(("Terrain detail: %s does not have the expected format (32-bit RGBA)\n", name));
		return false;
	}
	GPU_SUBRESOURCE_DATA init[13];
	size_t offset = 128;
	unsigned w = width, hh = height;
	for (unsigned i = 0; i < levels; ++i) {
		const size_t bytes = static_cast<size_t>(w) * hh * 4;
		if (offset + bytes > data.size()) {
			WWDEBUG_SAY(("Terrain detail: %s is too short\n", name));
			return false;
		}
		init[i].pSysMem = data.data() + offset;
		init[i].SysMemPitch = w * 4;
		init[i].SysMemSlicePitch = 0;
		offset += bytes;
		w = (w > 1) ? w / 2 : 1;
		hh = (hh > 1) ? hh / 2 : 1;
	}
	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width = width;  td.Height = height;  td.MipLevels = levels;  td.ArraySize = 1;
	td.Format = GPU_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = GPU_USAGE_IMMUTABLE;
	td.BindFlags = GPU_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&td, init, tex))) return false;
	if (FAILED(device->CreateShaderResourceView(*tex, nullptr, srv))) {
		Gpu_Release(*tex);
		return false;
	}
	return true;
}

bool RenderCore::Td_Ensure_Resources()
{
	if (M->td_failed || !M->device) return false;
	// The rock layer may create the table buffer without detail textures; ready only with the textures.
	if (M->td_cb && M->td_srv[0] && M->td_srv[1]) return true;
	bool ok = Td_Load_Texture(M->device, TD_TEXTURE_A, &M->td_tex[0], &M->td_srv[0])
		   && Td_Load_Texture(M->device, TD_TEXTURE_B, &M->td_tex[1], &M->td_srv[1]);
	if (ok && !M->td_cb) {
		M->td_cb = Create_Constant_Buffer(M->device, sizeof(FFPTerrainDetailCB));
		ok = M->td_cb != nullptr;
	}
	if (!ok) {
		WWDEBUG_SAY(("Terrain detail: cannot create resources, detail layer stays off\n"));
		Td_Release_Resources();
		M->td_failed = true;
		return false;
	}
	M->td_cb_dirty = true;
	return true;
}

void RenderCore::Td_Release_Resources()
{
	for (int i = 0; i < 2; ++i) {
		Gpu_Release(M->td_srv[i]);
		Gpu_Release(M->td_tex[i]);
	}
	Gpu_Release(M->td_sampler);
	Gpu_Release(M->td_cb);
	M->td_ts_open = false;
}

bool RenderCore::Td_Active()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	if (!(fx.flags & RENDER_EFFECT_TERRAIN_DETAIL) || fx.terrain_detail <= 0.0f) return false;
	// scene target only: the water reflection and screen filters draw the terrain without detail
	if (!M->td_table_valid || M->current_render_target || !M->context) return false;
	if (!Td_Ensure_Resources()) return false;
	if (!M->td_logged) {
		M->td_logged = true;
		WWDEBUG_SAY(("Terrain detail: on, strength %.2f, atlas grid %d x %d\n", fx.terrain_detail,
			M->td_cb_data.grid.x, M->td_cb_data.grid.y));
	}
	return true;
}

WW3DPixelShaderHandle RenderCore::Td_Pixel_Shader(WW3DPixelShaderHandle base)
{
	if (base->detail_variant || base->detail_failed || !base->detail_source) return base->detail_variant;
	GPU_SHADER_MACRO macros[2];
	macros[0].Name = "TERRAIN_DETAIL"; macros[0].Definition = "1";
	macros[1].Name = nullptr;          macros[1].Definition = nullptr;
	GpuBlob* code = nullptr;
	if (SUCCEEDED(Compile_FFP_Shader(base->detail_source, base->detail_source_len, base->detail_name, macros, "ps_4_0", &code)) && code) {
		WW3DPixelShaderObject* obj = W3DNEW WW3DPixelShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		if (SUCCEEDED(M->device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader)))
			base->detail_variant = obj;
		else
			delete obj;
	}
	if (code) code->Release();
	if (!base->detail_variant) {
		WWDEBUG_SAY(("Terrain detail: %s with TERRAIN_DETAIL does not compile, no detail there\n", base->detail_name));
		base->detail_failed = true;
	}
	return base->detail_variant;
}

void RenderCore::Td_Bind()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	// Same anisotropy as the terrain atlas; the detail texture tiles (WRAP) and, unlike the atlas,
	// cannot bleed into a neighbouring class.
	const unsigned aniso = (fx.terrain_anisotropy > 16) ? 16 : fx.terrain_anisotropy;
	const float bias = fx.terrain_lod_bias;
	if (!M->td_sampler || M->td_sampler_aniso != aniso || M->td_sampler_bias != bias) {
		Gpu_Release(M->td_sampler);
		GPU_SAMPLER_DESC sd;
		::ZeroMemory(&sd, sizeof(sd));
		sd.Filter         = (aniso > 1) ? GPU_FILTER_ANISOTROPIC : GPU_FILTER_MIN_MAG_MIP_LINEAR;
		sd.MaxAnisotropy  = (aniso > 1) ? aniso : 1;
		sd.AddressU       = GPU_TEXTURE_ADDRESS_WRAP;		// the detail texture tiles
		sd.AddressV       = GPU_TEXTURE_ADDRESS_WRAP;
		sd.AddressW       = GPU_TEXTURE_ADDRESS_WRAP;
		sd.ComparisonFunc = GPU_COMPARISON_NEVER;
		sd.MaxLOD         = GPU_FLOAT32_MAX;
		sd.MipLODBias     = bias;
		if (FAILED(M->device->CreateSamplerState(&sd, &M->td_sampler))) M->td_sampler = nullptr;
		M->td_sampler_aniso = aniso;
		M->td_sampler_bias = bias;
	}
	FFPTerrainDetailCB& cb = M->td_cb_data;
	FFPFloat4 fade;
	Fx_Set_Float4(fade, TD_FADE_START, 1.0f / (TD_FADE_FULL - TD_FADE_START), fx.terrain_detail, TD_LAYER2_WEIGHT);
	if (M->td_cb_dirty || ::memcmp(&fade, &cb.fade, sizeof(fade)) != 0) {
		cb.fade = fade;
		Fx_Set_Float4(cb.strengthA, 1.0f, 1.0f, 1.0f, 1.0f);
		Fx_Set_Float4(cb.strengthB, 1.0f, 1.0f, 1.0f, 1.0f);
		Fx_Set_Float4(cb.layer2, TD_LAYER2_SCALE, 1.0f / ::sqrtf(1.0f + TD_LAYER2_WEIGHT * TD_LAYER2_WEIGHT), 0.0f, 0.0f);
		Upload_Constant_Buffer(M->context, M->td_cb, &cb, sizeof(cb));
		M->td_cb_dirty = false;
	}
	GpuContext* c = M->context;
	c->PSSetConstantBuffers(RENDER_SLOT_CB_TERRAIN_DETAIL, 1, &M->td_cb);
	c->PSSetShaderResources(RENDER_SLOT_T_DETAIL_A, 2, M->td_srv);
	c->PSSetSamplers(RENDER_SLOT_S_DETAIL, 1, &M->td_sampler);
	number_of_API_calls += 3;
}

void RenderCore::Set_Terrain_Detail_Table(const RenderTerrainDetailTable& table)
{
	FFPTerrainDetailCB& cb = M->td_cb_data;
	unsigned columns = table.columns;
	unsigned rows = table.rows;
	if (columns * rows > FFP_TERRAIN_DETAIL_CELLS) rows = columns ? FFP_TERRAIN_DETAIL_CELLS / columns : 0;
	M->td_table_valid = columns > 0 && rows > 0 && table.cell_pixels > 0.0f && table.tile_pixels > 0.0f;
	Fx_Set_Float4(cb.atlas, table.atlas_width, table.atlas_height,
		(table.cell_pixels > 0.0f) ? 1.0f / table.cell_pixels : 0.0f, (table.tile_pixels > 0.0f) ? 1.0f / table.tile_pixels : 0.0f);
	cb.grid.x = static_cast<int>(columns);
	cb.grid.y = static_cast<int>(rows);
	cb.grid.z = 0;
	cb.grid.w = 0;
	int* cells = &cb.cells[0].x;
	int* colors = &M->tr_cb_data.colors[0].x;		// class colours for the rock layer
	M->td_stat_cells = 0;
	double snow[3] = { 0.0, 0.0, 0.0 };
	M->tr_snow_cells = 0;
	for (unsigned i = 0; i < FFP_TERRAIN_DETAIL_CELLS; ++i) {
		cells[i] = (i < columns * rows) ? static_cast<int>(table.cells[i]) : 0;
		colors[i] = (i < columns * rows) ? static_cast<int>(table.colors[i]) : 0;
		if (cells[i] & 0xFF) ++M->td_stat_cells;
		// snow ground (detail material 7) with a known class color: the color flat snow takes
		if ((cells[i] & 0xFF) == TR_SNOW_MATERIAL && (static_cast<unsigned>(colors[i]) >> 24) != 0) {
			snow[0] += (colors[i] >> 16) & 0xFF;
			snow[1] += (colors[i] >> 8) & 0xFF;
			snow[2] += colors[i] & 0xFF;
			++M->tr_snow_cells;
		}
	}
	for (int k = 0; k < 3; ++k)
		M->tr_snow_color[k] = M->tr_snow_cells ? static_cast<float>(snow[k] / (255.0 * M->tr_snow_cells)) : 0.0f;
	M->tr_cb_dirty = true;
	M->td_cb_dirty = true;
}

void RenderCore::Set_Terrain_Detail(bool enable)
{
	M->td_mode = enable;
	// GPU timing: the first terrain section per frame (the base pass), in the scene target, when any effect is on.
	if (enable) {
		if (!M->td_ts_open && M->td_ts_frame != M->fx_frame + 1 && !M->current_render_target &&
			RenderDevice::Get_Effect_Settings().flags != 0) {
			Fx_Timestamp(Fx_Ts_Begin(FX_RANGE_TERRAIN));
			M->td_ts_open = true;
			M->td_ts_frame = M->fx_frame + 1;
		}
	}
	else if (M->td_ts_open) {
		Fx_Timestamp(Fx_Ts_End(FX_RANGE_TERRAIN));
		M->td_ts_open = false;
	}
}

// ==================================================================================================
// 11. Rock layer: triplanar rock on steep slopes
// ==================================================================================================
//
// Only with "-d3d11Effects terrainrock" (also "berge"; Options.ini "D3D11Effects", detail level); otherwise
// Prepare_Draw never picks the permutation and no resource is created, so the image stays identical.
//
//  - Set_Terrain_Height_Field / Set_Terrain_Lighting (from HeightMapRenderObjClass::Render, per frame; the height
//    field only with the switch): the map height field (a copy; re-uploaded only when it changes, e.g. in WorldBuilder) and the
//    pre-lighting lights -> t11 (R16_UNORM) and the scene light in cbFrame (b0).
//  - The same terrain draws as the terrain detail layer (Set_Terrain_Detail) use the permutation
//    TERRAIN_ROCK (terrainrock.hlsli); the ground class comes from the detail table (cbTerrainDetail, b6).
//  - Rock textures: ProjectData\Textures\Berge\fels_grob.dds and fels_fein.dds.

static const char TR_TEXTURE_COARSE[] = "ProjectData\\Textures\\Berge\\fels_grob.dds";
static const char TR_TEXTURE_FINE[] = "ProjectData\\Textures\\Berge\\fels_fein.dds";

// Tuned against reference images. Slope as 1 - n.z: 0.13 = 30 degrees, 0.25 = 41 degrees.
static const float TR_PERIOD_COARSE = 64.0f;	///< world units per tile of the coarse rock texture (scan 5.7 m)
static const float TR_PERIOD_FINE = 16.0f;		///< world units per tile of the fine rock texture (scan 2.7 m)
static const float TR_START_ROCK = 0.13f;		///< start where EA already paints rock (material 6 of the detail table)
static const float TR_START_OTHER = 0.25f;		///< start on grass, dirt, sand
static const float TR_START_SAND = 0.20f;		///< start on sand and dry grass (desert): walls turn to rock a little earlier
static const float TR_START_SNOW = 0.40f;		///< start on snow (53 degrees): snow stays on moderately steep slopes
static const float TR_ROCK_CAP = 0.85f;		///< largest rock share on EA rock ground: a remainder of the painted structure stays
static const float TR_CONTRAST = 1.12f;			///< contrast gain of the rock structure (exponent on the brightness factor)
static const float TR_CONTRAST_SIGMA = 0.3f;	///< assumed spread of the factor, only for restoring its mean
static const float TR_EA_SHARE = 0.5f;			///< share of the coarse EA colour; the rest is the mean colour of the class
static const float TR_BLEND_WIDTH = 0.10f;		///< transition width (in 1 - n.z)
static const float TR_HEIGHT_EDGE = 0.35f;		///< share of the rock height at the edge (irregular transition)
static const float TR_EA_MIP = 4.0f;			///< mip level of the EA colour (logical): 16 x 16 atlas pixels, 5 world units
static const float TR_NORMAL_STRENGTH = 1.2f;	///< strength of the rock normals
static const float TR_RATIO_MIN = 0.5f;			///< limits of the light ratio rock normal / terrain normal
static const float TR_RATIO_MAX = 1.5f;

bool RenderCore::Tr_Active()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	if (!(fx.flags & RENDER_EFFECT_TERRAIN_ROCK) || fx.terrain_rock <= 0.0f) return false;
	// scene target only (not in the water reflection), with height field and atlas table (ground class per pixel)
	if (!M->td_table_valid || !M->tr_heights || M->current_render_target || !M->context) return false;
	if (!Tr_Ensure_Resources()) return false;
	if (!M->tr_logged) {
		M->tr_logged = true;
		WWDEBUG_SAY(("Mountains: rock layer on, strength %.2f, height field %d x %d, border %d\n", fx.terrain_rock,
			M->tr_width, M->tr_height, M->tr_border));
	}
	return true;
}

bool RenderCore::Tr_Ensure_Resources()
{
	if (M->tr_failed || !M->device) return false;
	if (!M->tr_cb) {
		bool ok = Td_Load_Texture(M->device, TR_TEXTURE_COARSE, &M->tr_tex[0], &M->tr_srv[0])
			   && Td_Load_Texture(M->device, TR_TEXTURE_FINE, &M->tr_tex[1], &M->tr_srv[1]);
		if (ok && !M->tr_height_sampler) {
			GPU_SAMPLER_DESC sd;
			::ZeroMemory(&sd, sizeof(sd));
			sd.Filter         = GPU_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU       = GPU_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV       = GPU_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW       = GPU_TEXTURE_ADDRESS_CLAMP;
			sd.MaxAnisotropy  = 1;
			sd.ComparisonFunc = GPU_COMPARISON_NEVER;
			sd.MaxLOD         = GPU_FLOAT32_MAX;
			ok = SUCCEEDED(M->device->CreateSamplerState(&sd, &M->tr_height_sampler));
		}
		if (ok) {
			M->tr_cb = Create_Constant_Buffer(M->device, sizeof(FFPTerrainRockCB));
			ok = M->tr_cb != nullptr;
		}
		if (!ok) {
			WWDEBUG_SAY(("Mountains: cannot create resources, rock layer stays off\n"));
			Tr_Release_Resources();
			M->tr_failed = true;
			return false;
		}
		M->tr_cb_dirty = true;
		M->tr_height_dirty = true;
	}
	return Tr_Ensure_Height_Field();
}

bool RenderCore::Tr_Ensure_Height_Field()
{
	if (!M->device) return false;
	if (M->tr_height_dirty && M->tr_heights) {
		// height field as R16_UNORM (byte * 257: value/65535 = byte/255, filtered exactly by bilinear sampling)
		Gpu_Release(M->tr_srv[2]);
		Gpu_Release(M->tr_tex[2]);
		const size_t n = static_cast<size_t>(M->tr_width) * static_cast<size_t>(M->tr_height);
		unsigned short* data = W3DNEWARRAY unsigned short[n];
		for (size_t i = 0; i < n; ++i) data[i] = static_cast<unsigned short>(M->tr_heights[i] * 257u);
		GPU_TEXTURE2D_DESC td;
		::ZeroMemory(&td, sizeof(td));
		td.Width = static_cast<UINT>(M->tr_width);  td.Height = static_cast<UINT>(M->tr_height);
		td.MipLevels = 1;  td.ArraySize = 1;
		td.Format = GPU_FORMAT_R16_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = GPU_USAGE_IMMUTABLE;
		td.BindFlags = GPU_BIND_SHADER_RESOURCE;
		GPU_SUBRESOURCE_DATA init;
		init.pSysMem = data;
		init.SysMemPitch = static_cast<UINT>(M->tr_width) * 2;
		init.SysMemSlicePitch = 0;
		if (SUCCEEDED(M->device->CreateTexture2D(&td, &init, &M->tr_tex[2])) &&
			FAILED(M->device->CreateShaderResourceView(M->tr_tex[2], nullptr, &M->tr_srv[2])))
			Gpu_Release(M->tr_tex[2]);
		delete[] data;
		M->tr_height_dirty = false;
		M->tr_cb_dirty = true;
		M->tl_cb_dirty = true;
	}
	return M->tr_srv[2] != nullptr;
}

void RenderCore::Tr_Release_Resources()
{
	for (int i = 0; i < 3; ++i) {
		Gpu_Release(M->tr_srv[i]);
		Gpu_Release(M->tr_tex[i]);
	}
	Gpu_Release(M->tr_sampler);
	Gpu_Release(M->tr_height_sampler);
	Gpu_Release(M->tr_cb);
	M->tr_height_dirty = true;		// recreate the height field from the copy
}

WW3DPixelShaderHandle RenderCore::Tr_Pixel_Shader(WW3DPixelShaderHandle base, bool with_detail)
{
	const int k = with_detail ? 1 : 0;
	if (base->rock_variant[k] || base->rock_failed[k] || !base->detail_source) return base->rock_variant[k];
	GPU_SHADER_MACRO macros[3];
	macros[0].Name = "TERRAIN_ROCK";   macros[0].Definition = "1";
	macros[1].Name = with_detail ? "TERRAIN_DETAIL" : nullptr;
	macros[1].Definition = with_detail ? "1" : nullptr;
	macros[2].Name = nullptr;          macros[2].Definition = nullptr;
	GpuBlob* code = nullptr;
	if (SUCCEEDED(Compile_FFP_Shader(base->detail_source, base->detail_source_len, base->detail_name, macros, "ps_4_0", &code)) && code) {
		WW3DPixelShaderObject* obj = W3DNEW WW3DPixelShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		if (SUCCEEDED(M->device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader)))
			base->rock_variant[k] = obj;
		else
			delete obj;
	}
	if (code) code->Release();
	if (!base->rock_variant[k]) {
		WWDEBUG_SAY(("Mountains: %s with TERRAIN_ROCK does not compile, no rock layer there\n", base->detail_name));
		base->rock_failed[k] = true;
	}
	return base->rock_variant[k];
}

void RenderCore::Tr_Bind(bool with_detail)
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	GpuContext* c = M->context;
	// Same anisotropy as the terrain atlas; the rock textures tile.
	const unsigned aniso = (fx.terrain_anisotropy > 16) ? 16 : fx.terrain_anisotropy;
	const float bias = fx.terrain_lod_bias;
	if (!M->tr_sampler || M->tr_sampler_aniso != aniso || M->tr_sampler_bias != bias) {
		Gpu_Release(M->tr_sampler);
		GPU_SAMPLER_DESC sd;
		::ZeroMemory(&sd, sizeof(sd));
		sd.Filter         = (aniso > 1) ? GPU_FILTER_ANISOTROPIC : GPU_FILTER_MIN_MAG_MIP_LINEAR;
		sd.MaxAnisotropy  = (aniso > 1) ? aniso : 1;
		sd.AddressU       = GPU_TEXTURE_ADDRESS_WRAP;
		sd.AddressV       = GPU_TEXTURE_ADDRESS_WRAP;
		sd.AddressW       = GPU_TEXTURE_ADDRESS_WRAP;
		sd.ComparisonFunc = GPU_COMPARISON_NEVER;
		sd.MaxLOD         = GPU_FLOAT32_MAX;
		sd.MipLODBias     = bias;
		if (FAILED(M->device->CreateSamplerState(&sd, &M->tr_sampler))) M->tr_sampler = nullptr;
		M->tr_sampler_aniso = aniso;
		M->tr_sampler_bias = bias;
	}
	FFPTerrainRockCB& cb = M->tr_cb_data;
	const float strength = (fx.terrain_rock > 1.0f) ? 1.0f : fx.terrain_rock;
	FFPFloat4 scale;
	Fx_Set_Float4(scale, 1.0f / TR_PERIOD_COARSE, 1.0f / TR_PERIOD_FINE, TR_EA_MIP, strength);
	// flat snow only on maps that have snow ground
	const float snow_share = ((fx.flags & RENDER_EFFECT_FLAT_SNOW) && M->tr_snow_cells) ?
		((fx.flat_snow > 1.0f) ? 1.0f : ((fx.flat_snow < 0.0f) ? 0.0f : fx.flat_snow)) : 0.0f;
	if (M->tr_cb_dirty || ::memcmp(&scale, &cb.scale, sizeof(scale)) != 0 || cb.snow.w != snow_share) {
		cb.scale = scale;
		Fx_Set_Float4(cb.snow, M->tr_snow_color[0], M->tr_snow_color[1], M->tr_snow_color[2], snow_share);
		Fx_Set_Float4(cb.map, 1.0f / (float)M->tr_width, 1.0f / (float)M->tr_height, (float)M->tr_border + 0.5f,
			(M->tr_cell > 0.0f) ? 1.0f / M->tr_cell : 0.1f);
		Fx_Set_Float4(cb.slope, TR_START_ROCK, TR_START_OTHER, 1.0f / TR_BLEND_WIDTH, TR_HEIGHT_EDGE);
		Fx_Set_Float4(cb.light, TR_NORMAL_STRENGTH, 255.0f * M->tr_height_scale, TR_RATIO_MIN, TR_RATIO_MAX);
		Fx_Set_Float4(cb.tint, TR_EA_SHARE, TR_START_SNOW, TR_START_SAND, TR_ROCK_CAP);
		// E[x^g] is about 1 + g (g - 1) s^2 / 2 for a factor with mean 1 and spread s: divide it out
		Fx_Set_Float4(cb.extra, TR_CONTRAST,
			1.0f / (1.0f + 0.5f * TR_CONTRAST * (TR_CONTRAST - 1.0f) * TR_CONTRAST_SIGMA * TR_CONTRAST_SIGMA), TR_SNOW_FLAT_FULL,
			TR_SNOW_FLAT_NONE);
		Upload_Constant_Buffer(c, M->tr_cb, &cb, sizeof(cb));
		M->tr_cb_dirty = false;
	}
	// Without the detail layer the rock layer still needs the atlas table (ground class per pixel): keep b6 here.
	if (!with_detail) {
		if (!M->td_cb) M->td_cb = Create_Constant_Buffer(M->device, sizeof(FFPTerrainDetailCB));
		if (M->td_cb && M->td_cb_dirty) {
			Upload_Constant_Buffer(c, M->td_cb, &M->td_cb_data, sizeof(M->td_cb_data));
			M->td_cb_dirty = false;
		}
		c->PSSetConstantBuffers(RENDER_SLOT_CB_TERRAIN_DETAIL, 1, &M->td_cb);
		++number_of_API_calls;
	}
	GpuShaderResourceView* srvs[3] = { M->tr_srv[2], M->tr_srv[0], M->tr_srv[1] };
	GpuSamplerState* samplers[2] = { M->tr_sampler, M->tr_height_sampler };
	c->PSSetConstantBuffers(RENDER_SLOT_CB_TERRAIN_ROCK, 1, &M->tr_cb);
	c->PSSetShaderResources(RENDER_SLOT_T_TERRAIN_HEIGHT, 3, srvs);	// height, rock A, rock B
	c->PSSetSamplers(RENDER_SLOT_S_ROCK, 2, samplers);	// rock, height
	number_of_API_calls += 3;
}

void RenderCore::Set_Terrain_Height_Field(const unsigned char* heights, int width, int height, int border,
	float cell_size, float height_scale)
{
	if (!heights || width < 2 || height < 2) return;
	const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);
	if (M->tr_heights && M->tr_width == width && M->tr_height == height && M->tr_border == border &&
		M->tr_cell == cell_size && M->tr_height_scale == height_scale && ::memcmp(M->tr_heights, heights, n) == 0)
		return;
	if (!M->tr_heights || static_cast<size_t>(M->tr_width) * static_cast<size_t>(M->tr_height) != n) {
		delete[] M->tr_heights;
		M->tr_heights = W3DNEWARRAY unsigned char[n];
	}
	::memcpy(M->tr_heights, heights, n);
	unsigned char lowest = 255;
	for (size_t i = 0; i < n; ++i) if (heights[i] < lowest) lowest = heights[i];
	M->tr_min_height = static_cast<float>(lowest) * height_scale;
	M->tr_width = width;
	M->tr_height = height;
	M->tr_border = border;
	M->tr_cell = cell_size;
	M->tr_height_scale = height_scale;
	M->tr_height_dirty = true;
	M->tr_cb_dirty = true;
}

// The scene light goes into cbFrame; the rock layer, the relief and later passes read it there.
void RenderCore::Set_Terrain_Lighting(const RenderTerrainLighting& lighting)
{
	FFPFrameCB& cb = M->frame_cb;
	FFPFloat4 ambient, ray[3], diffuse[3];
	Fx_Set_Float4(ambient, lighting.ambient[0], lighting.ambient[1], lighting.ambient[2], 0.0f);
	for (unsigned i = 0; i < 3; ++i) {
		const bool on = i < lighting.count;
		Fx_Set_Float4(ray[i], on ? lighting.ray[i][0] : 0.0f, on ? lighting.ray[i][1] : 0.0f, on ? lighting.ray[i][2] : 0.0f, on ? 1.0f : 0.0f);
		Fx_Set_Float4(diffuse[i], on ? lighting.diffuse[i][0] : 0.0f, on ? lighting.diffuse[i][1] : 0.0f, on ? lighting.diffuse[i][2] : 0.0f, 0.0f);
	}
	if (::memcmp(&ambient, &cb.ambient, sizeof(ambient)) != 0 || ::memcmp(ray, cb.sunRay, sizeof(ray)) != 0 ||
		::memcmp(diffuse, cb.sunDiffuse, sizeof(diffuse)) != 0) {
		cb.ambient = ambient;
		::memcpy(cb.sunRay, ray, sizeof(ray));
		::memcpy(cb.sunDiffuse, diffuse, sizeof(diffuse));
		M->frame_cb_dirty = true;
	}
}

// ==================================================================================================
// 11b. Terrain relief: normal maps and gloss of the terrain textures
// ==================================================================================================
//
// Only with "-d3d11Effects terrainnormal" (also "relief") and when the map has relief tiles (<texture>_n.dds next to
// the terrain textures); otherwise Prepare_Draw never picks the permutation and the image stays identical.
//
//  - Set_Terrain_Relief_Atlas (from WorldHeightMap::updateTerrainReliefAtlas): the relief atlas, laid out like the
//    terrain atlas -> t14, read with the atlas coordinates and samplers (s0/s1).
//  - Height field and prelighting light as for the rock layer (Set_Terrain_Height_Field, Set_Terrain_Lighting) ->
//    t11, s11, scene light in cbFrame (b0), the rest in cbTerrainNormal (b12).
//  - The terrain draws use the permutation TERRAIN_NORMAL (terrainnormal.hlsli), together with TERRAIN_DETAIL and
//    TERRAIN_ROCK when those are on.

static const float TN_STRENGTH = 1.0f;			///< relief strength at terrainnormal=1
static const float TN_RATIO_MIN = 0.5f;			///< limits of the light ratio relief normal / terrain normal
static const float TN_RATIO_MAX = 1.5f;
static const float TN_GLOSS_START = 0.15f;		///< gloss (1 - roughness of the scan) where highlights begin
static const float TN_GLOSS_FULL = 0.5f;		///< full highlights from this gloss on
static const float TN_SPECULAR = 0.25f;			///< highlight strength
static const float TN_SPECULAR_POWER = 48.0f;	///< highlight exponent at full gloss (8 where they begin)
// The free ground textures are shaded with a fixed light from the upper left of the image (boden_frei.py, scan_karten);
// its direction and the share of it that the relief takes out again.
static const float TN_BAKED_X = -0.45f, TN_BAKED_Y = 0.45f, TN_BAKED_Z = 0.77f;
static const float TN_BAKED_SHARE = 1.0f;

bool RenderCore::Tn_Active()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	if (!(fx.flags & RENDER_EFFECT_TERRAIN_NORMAL) || fx.terrain_normal <= 0.0f) return false;
	// scene target only (not in the water reflection), with relief atlas and height field
	if (!M->tn_atlas || !M->tr_heights || M->current_render_target || !M->context) return false;
	if (!Tn_Ensure_Resources()) return false;
	if (!M->tn_logged) {
		M->tn_logged = true;
		WWDEBUG_SAY(("Terrain relief: on, strength %.2f\n", fx.terrain_normal));
	}
	return true;
}

bool RenderCore::Tn_Ensure_Resources()
{
	if (M->tn_failed || !M->device) return false;
	if (!M->tn_cb) {
		M->tn_cb = Create_Constant_Buffer(M->device, sizeof(FFPTerrainNormalCB));
		if (!M->tn_cb) {
			WWDEBUG_SAY(("Terrain relief: cannot create the constant buffer, relief stays off\n"));
			M->tn_failed = true;
			return false;
		}
		M->tn_cb_dirty = true;
	}
	if (!M->tr_height_sampler) {
		GPU_SAMPLER_DESC sd;
		::ZeroMemory(&sd, sizeof(sd));
		sd.Filter         = GPU_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU       = GPU_TEXTURE_ADDRESS_CLAMP;
		sd.AddressV       = GPU_TEXTURE_ADDRESS_CLAMP;
		sd.AddressW       = GPU_TEXTURE_ADDRESS_CLAMP;
		sd.MaxAnisotropy  = 1;
		sd.ComparisonFunc = GPU_COMPARISON_NEVER;
		sd.MaxLOD         = GPU_FLOAT32_MAX;
		if (FAILED(M->device->CreateSamplerState(&sd, &M->tr_height_sampler))) {
			M->tr_height_sampler = nullptr;
			return false;
		}
	}
	return Tr_Ensure_Height_Field();
}

void RenderCore::Tn_Release_Resources()
{
	Gpu_Release(M->tn_cb);
}

WW3DPixelShaderHandle RenderCore::Tn_Pixel_Shader(WW3DPixelShaderHandle base, bool with_detail, bool with_rock)
{
	const int k = (with_detail ? 1 : 0) | (with_rock ? 2 : 0);
	if (base->normal_variant[k] || base->normal_failed[k] || !base->detail_source) return base->normal_variant[k];
	GPU_SHADER_MACRO macros[4];
	int m = 0;
	macros[m].Name = "TERRAIN_NORMAL"; macros[m++].Definition = "1";
	if (with_detail) { macros[m].Name = "TERRAIN_DETAIL"; macros[m++].Definition = "1"; }
	if (with_rock) { macros[m].Name = "TERRAIN_ROCK"; macros[m++].Definition = "1"; }
	macros[m].Name = nullptr;
	macros[m].Definition = nullptr;
	GpuBlob* code = nullptr;
	if (SUCCEEDED(Compile_FFP_Shader(base->detail_source, base->detail_source_len, base->detail_name, macros, "ps_4_0", &code)) && code) {
		WW3DPixelShaderObject* obj = W3DNEW WW3DPixelShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		if (SUCCEEDED(M->device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader)))
			base->normal_variant[k] = obj;
		else
			delete obj;
	}
	if (code) code->Release();
	if (!base->normal_variant[k]) {
		WWDEBUG_SAY(("Terrain relief: %s with TERRAIN_NORMAL does not compile, no relief there\n", base->detail_name));
		base->normal_failed[k] = true;
	}
	return base->normal_variant[k];
}

void RenderCore::Tn_Bind()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	GpuContext* c = M->context;
	const float strength = (fx.terrain_normal > 2.0f) ? 2.0f : fx.terrain_normal;
	FFPTerrainNormalCB cb;
	Fx_Set_Float4(cb.map, 1.0f / (float)M->tr_width, 1.0f / (float)M->tr_height, (float)M->tr_border + 0.5f,
		(M->tr_cell > 0.0f) ? 1.0f / M->tr_cell : 0.1f);
	Fx_Set_Float4(cb.light, TN_STRENGTH * strength, 255.0f * M->tr_height_scale, TN_RATIO_MIN, TN_RATIO_MAX);
	Fx_Set_Float4(cb.gloss, TN_GLOSS_START, 1.0f / (TN_GLOSS_FULL - TN_GLOSS_START), TN_SPECULAR * (strength > 1.0f ? 1.0f : strength),
		TN_SPECULAR_POWER);
	Fx_Set_Float4(cb.baked, TN_BAKED_X / TN_BAKED_Z, TN_BAKED_Y / TN_BAKED_Z, TN_BAKED_SHARE * (strength > 1.0f ? 1.0f : strength), 0.0f);
	if (M->tn_cb_dirty || ::memcmp(&cb, &M->tn_cb_data, sizeof(cb)) != 0) {
		M->tn_cb_data = cb;
		Upload_Constant_Buffer(c, M->tn_cb, &M->tn_cb_data, sizeof(M->tn_cb_data));
		M->tn_cb_dirty = false;
	}
	c->PSSetConstantBuffers(RENDER_SLOT_CB_TERRAIN_NORMAL, 1, &M->tn_cb);
	c->PSSetShaderResources(RENDER_SLOT_T_TERRAIN_HEIGHT, 1, &M->tr_srv[2]);
	c->PSSetShaderResources(RENDER_SLOT_T_RELIEF, 1, &M->tn_atlas);
	c->PSSetSamplers(RENDER_SLOT_S_TERRAIN_HEIGHT, 1, &M->tr_height_sampler);
	number_of_API_calls += 4;
}

void RenderCore::Set_Terrain_Relief_Atlas(WW3DTextureHandle atlas, bool active)
{
	if (!active) {
		if (atlas && atlas == M->tn_atlas_handle) {
			Gpu_Release(M->tn_atlas);
			M->tn_atlas_handle = nullptr;
		}
		return;
	}
	Gpu_Release(M->tn_atlas);
	M->tn_atlas_handle = atlas;
	if (atlas && atlas->srv) {
		M->tn_atlas = atlas->srv;
		M->tn_atlas->AddRef();
	}
	M->tn_logged = false;
}

// ==================================================================================================
// 11c. Terrain tessellation with displacement
// ==================================================================================================
//
// Only with "-d3d11Effects terraintess" (also "tessellation") and when the map has heights for its terrain textures
// (<texture>_h.dds next to them); otherwise Prepare_Draw never picks the patch path and the image stays identical.
//
//  - Set_Terrain_Tess_Data (from WorldHeightMap::updateTerrainHeightAtlas): height atlas (t3 of the domain stage, own
//    reference), class table (cbTerrainTess, b13 of hull and domain stage) and weights per grid point (t1).
//  - Set_Terrain_Tess_Mask (from TerrainTessMask): displacement mask per grid point (t2): water, shore, roads, buildings.
//  - Set_Terrain_Tessellation(true/false) (from HeightMapRenderObjClass around the base pass, the three-way blends and
//    the shroud pass): triangle lists in between, in the scene target and with the uber vertex shader, are drawn as
//    patches (terraintess.hs.hlsl, terraintess.ds.hlsl) with ffp.vs.hlsl as vs_5_0. Hull and domain shader are
//    unbound by the next other draw and at the end of the section; shadow casters are recorded as triangles.
//  - Height field (t0) for the normal, as for the rock layer (Set_Terrain_Height_Field).
//  - "rocktess" (with the rock layer on): steep slopes are displaced by the triplanar height of the rock textures (t4, t5,
//    s1 of the domain stage; height field in t0 of the hull stage for the slope). Without ground heights (EA textures)
//    the map gets a flat height atlas and only the rock is displaced.
//  - Statistics: one pipeline statistics query per section (patches, domain points, triangles).

static const float TT_AMPLITUDE = 0.9f;				///< world units per height unit at strength 1 (heights 0..1 around 0.5)
static const float TT_PROBE_AMPLITUDE = 8.0f;		///< crack probe: displacement this many times stronger
static const float TT_TILE_WORLD = 20.0f;		///< world units per terrain tile (two cells)
static const float TT_FADE_START = 40.0f;		///< pixels per tile where the displacement starts (as the detail layer)
static const float TT_FADE_FULL = 100.0f;		///< full displacement from here on
static const float TT_SLOPE_END = 0.13f;		///< slope (1 - n.z) where the rock layer begins: no displacement from here
static const float TT_SLOPE_FADE = 0.05f;		///< fade width below it
// Rock slopes (rocktess): displacement by the triplanar height of the rock layer textures (A channel, 0..1).
static const float TT_ROCK_AMPLITUDE = 4.0f;	///< world units per rock height unit at strength 1
static const float TT_ROCK_REACH = 0.6f;		///< largest deviation of the mixed rock height from its mean
static const float TT_ROCK_START = 0.20f;		///< slope (1 - n.z) where the rock displacement starts (rock layer: 0.13 to 0.40)
static const float TT_ROCK_WIDTH = 0.10f;		///< its fade-in width (as the rock layer)
static const float TT_ROCK_FADE_START = 20.0f;	///< pixels per tile where it starts: farther than the ground, slopes stand out
static const float TT_ROCK_FADE_FULL = 50.0f;	///< full from here on (the standard view has 55 to 80)
static const float TT_ROCK_LOD_BIAS = 0.5f;		///< mip bias of the rock heights against the spacing of the domain points

/// Largest displacement of the tessellated terrain (world units): ground heights or rock slopes.
float Tt_Max_Displacement(const RenderEffectSettings& fx, float rock_amplitude)
{
	const float ground = TT_AMPLITUDE * 0.5f * fx.terrain_tess * (fx.terrain_tess_probe ? TT_PROBE_AMPLITUDE : 1.0f);
	const float rock = TT_ROCK_REACH * rock_amplitude;
	return (rock > ground) ? rock : ground;
}

/// Compiles a hull or domain shader from the shader folder.
static GpuBlob* Tt_Compile(const char* name, const char* profile)
{
	unsigned length = 0;
	char* source = Load_Shader_Source(name, &length);
	if (!source) return nullptr;
	GpuBlob* code = nullptr;
	if (FAILED(Compile_FFP_Shader(source, length, name, nullptr, profile, &code)) && code) {
		code->Release();
		code = nullptr;
	}
	delete[] source;
	return code;
}

bool RenderCore::Tt_Active()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	if (!(fx.flags & RENDER_EFFECT_TERRAIN_TESS) || fx.terrain_tess < 0.0f) return false;
	// scene target only (not in the water reflection), with heights, weights and height field
	if (!M->tt_atlas || !M->tt_weights_cpu || !M->tr_heights || M->current_render_target || !M->context) return false;
	if (!Tt_Ensure_Resources()) return false;
	if (!M->tt_logged) {
		M->tt_logged = true;
		WWDEBUG_SAY(("Terrain tessellation: on, strength %.2f, largest factor %.0f, %.0f pixels per edge piece, grid %d x %d%s\n",
			fx.terrain_tess, fx.terrain_tess_max, fx.terrain_tess_pixels, M->tt_grid_w, M->tt_grid_h,
			fx.terrain_tess_probe ? ", crack probe" : ""));
	}
	return true;
}

bool RenderCore::Tt_Ensure_Resources()
{
	if (M->tt_failed || !M->device) return false;
	if (!M->tt_hs) {
		bool ok = true;
		GpuBlob* code = Tt_Compile("terraintess.hs.hlsl", "hs_5_0");
		ok = code && SUCCEEDED(M->device->CreateHullShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &M->tt_hs));
		if (code) code->Release();
		if (ok) {
			code = Tt_Compile("terraintess.ds.hlsl", "ds_5_0");
			ok = code && SUCCEEDED(M->device->CreateDomainShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &M->tt_ds));
			if (code) code->Release();
		}
		if (ok && RenderDevice::Get_Effect_Settings().terrain_tess_probe) {
			code = Tt_Compile("terraintessprobe.ps.hlsl", "ps_4_0");
			if (code) {
				WW3DPixelShaderObject* obj = W3DNEW WW3DPixelShaderObject;
				::ZeroMemory(obj, sizeof(*obj));
				obj->refcount = 1;
				if (SUCCEEDED(M->device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader)))
					M->tt_probe_ps = obj;
				else
					delete obj;
				code->Release();
			}
		}
		if (ok) {
			M->tt_cb = Create_Constant_Buffer(M->device, sizeof(FFPTerrainTessCB));
			ok = M->tt_cb != nullptr;
		}
		if (ok) {
			GPU_SAMPLER_DESC sd;
			::ZeroMemory(&sd, sizeof(sd));
			sd.Filter         = GPU_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU       = GPU_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV       = GPU_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW       = GPU_TEXTURE_ADDRESS_CLAMP;
			sd.MaxAnisotropy  = 1;
			sd.ComparisonFunc = GPU_COMPARISON_NEVER;
			sd.MaxLOD         = GPU_FLOAT32_MAX;
			ok = SUCCEEDED(M->device->CreateSamplerState(&sd, &M->tt_sampler));
			if (ok) {
				sd.AddressU = sd.AddressV = sd.AddressW = GPU_TEXTURE_ADDRESS_WRAP;
				ok = SUCCEEDED(M->device->CreateSamplerState(&sd, &M->tt_rock_sampler));
			}
		}
		if (!ok) {
			WWDEBUG_SAY(("Terrain tessellation: shaders or constant buffer cannot be created, tessellation stays off\n"));
			Tt_Release_Resources();
			M->tt_failed = true;
			return false;
		}
		M->tt_cb_dirty = true;
	}
	// weights (immutable) and mask (updated when buildings come and go) from the copies
	if (!M->tt_weights && M->tt_weights_cpu && M->tt_grid_w > 0 && M->tt_grid_h > 0) {
		GPU_TEXTURE2D_DESC td;
		::ZeroMemory(&td, sizeof(td));
		td.Width = static_cast<UINT>(M->tt_grid_w);  td.Height = static_cast<UINT>(M->tt_grid_h);
		td.MipLevels = 1;  td.ArraySize = 1;
		td.Format = GPU_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = GPU_USAGE_IMMUTABLE;
		td.BindFlags = GPU_BIND_SHADER_RESOURCE;
		GPU_SUBRESOURCE_DATA init;
		init.pSysMem = M->tt_weights_cpu;
		init.SysMemPitch = static_cast<UINT>(M->tt_grid_w) * 4;
		init.SysMemSlicePitch = 0;
		if (SUCCEEDED(M->device->CreateTexture2D(&td, &init, &M->tt_weights_tex)) &&
			FAILED(M->device->CreateShaderResourceView(M->tt_weights_tex, nullptr, &M->tt_weights)))
			Gpu_Release(M->tt_weights_tex);
		td.Format = GPU_FORMAT_R8_UNORM;
		td.Usage = GPU_USAGE_DEFAULT;
		init.pSysMem = M->tt_mask_cpu;
		init.SysMemPitch = static_cast<UINT>(M->tt_grid_w);
		if (M->tt_mask_cpu && SUCCEEDED(M->device->CreateTexture2D(&td, &init, &M->tt_mask_tex)) &&
			FAILED(M->device->CreateShaderResourceView(M->tt_mask_tex, nullptr, &M->tt_mask)))
			Gpu_Release(M->tt_mask_tex);
	}
	return M->tt_weights && M->tt_mask && Tr_Ensure_Height_Field();
}

void RenderCore::Tt_Release_Resources()
{
	if (M->tt_bound && M->context) Tt_Unbind();
	Gpu_Release(M->tt_hs);
	Gpu_Release(M->tt_ds);
	if (M->tt_probe_ps) { Release_Resource(M->tt_probe_ps); M->tt_probe_ps = nullptr; }
	Gpu_Release(M->tt_cb);
	Gpu_Release(M->tt_sampler);
	Gpu_Release(M->tt_rock_sampler);
	Gpu_Release(M->tt_weights);
	Gpu_Release(M->tt_weights_tex);
	Gpu_Release(M->tt_mask);
	Gpu_Release(M->tt_mask_tex);
	for (unsigned i = 0; i < TT_QUERIES; ++i) {
		Gpu_Release(M->tt_query[i]);
		M->tt_query_state[i] = 0;
	}
	M->tt_query_open = 0;
	M->tt_logged = false;
}

WW3DVertexShaderHandle RenderCore::Tt_Vertex_Shader(unsigned permutation)
{
	if (permutation >= FFP_VS_PERMUTATIONS) return nullptr;
	if (M->tt_vs[permutation]) return M->tt_vs[permutation];
	if ((M->tt_vs_failed >> permutation) & 1ull) return nullptr;
	// the source is loaded with the vs_4_0 permutation
	if (!M->ffp_vs_source && !Get_FFP_Vertex_Shader(permutation)) return nullptr;
	if (!M->ffp_vs_source) return nullptr;

	char num_texcoords[2];
	num_texcoords[0] = static_cast<char>('0' + ((permutation >> 3) & 7));
	num_texcoords[1] = 0;
	GPU_SHADER_MACRO macros[6];
	unsigned n = 0;
	if (permutation & 1) { macros[n].Name = "HAS_NORMAL";   macros[n].Definition = "1"; ++n; }
	if (permutation & 2) { macros[n].Name = "HAS_DIFFUSE";  macros[n].Definition = "1"; ++n; }
	if (permutation & 4) { macros[n].Name = "HAS_SPECULAR"; macros[n].Definition = "1"; ++n; }
	macros[n].Name = "NUM_TEXCOORDS"; macros[n].Definition = num_texcoords; ++n;
	macros[n].Name = "TERRAIN_TESS"; macros[n].Definition = "1"; ++n;
	macros[n].Name = nullptr; macros[n].Definition = nullptr;

	GpuBlob* code = nullptr;
	WW3DVertexShaderObject* obj = nullptr;
	if (SUCCEEDED(Compile_FFP_Shader(M->ffp_vs_source, M->ffp_vs_source_len, "ffp.vs.hlsl", macros, "vs_5_0", &code)) && code) {
		obj = W3DNEW WW3DVertexShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		obj->bytecode = code;				// kept for the input layouts
		if (FAILED(M->device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader))) {
			code->Release();
			delete obj;
			obj = nullptr;
		}
	}
	else if (code) code->Release();
	if (!obj) {
		WWDEBUG_SAY(("Terrain tessellation: vertex shader permutation %u does not compile, no tessellation there\n", permutation));
		M->tt_vs_failed |= 1ull << permutation;
		return nullptr;
	}
	M->tt_vs[permutation] = obj;
	return obj;
}

float RenderCore::Tt_Rock_Amplitude()
{
	// Rock slopes: only with the rock layer and its textures (same decision for every terrain section of the frame).
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	const bool rock = (fx.flags & RENDER_EFFECT_ROCK_TESS) && fx.rock_tess > 0.0f && Tr_Active();
	const float rock_strength = rock ? ((fx.rock_tess > 2.0f) ? 2.0f : fx.rock_tess) * ((fx.terrain_rock > 1.0f) ? 1.0f : fx.terrain_rock) : 0.0f;
	return TT_ROCK_AMPLITUDE * rock_strength * (fx.terrain_tess_probe ? TT_PROBE_AMPLITUDE : 1.0f);
}

float RenderCore::Terrain_Tess_Max_Displacement()
{
	if (!Tt_Active()) return 0.0f;
	return Tt_Max_Displacement(RenderDevice::Get_Effect_Settings(), Tt_Rock_Amplitude());
}

void RenderCore::Tt_Bind()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	GpuContext* c = M->context;
	const float strength = (fx.terrain_tess > 2.0f) ? 2.0f : fx.terrain_tess;
	const float amplitude = M->tt_heights ? TT_AMPLITUDE * strength * (fx.terrain_tess_probe ? TT_PROBE_AMPLITUDE : 1.0f) : 0.0f;
	const float rock_amplitude = Tt_Rock_Amplitude();
	const bool rock = rock_amplitude > 0.0f;
	M->tt_rock_amp = rock_amplitude;
	const float vp_w = (M->viewport.Width > 0) ? static_cast<float>(M->viewport.Width) : static_cast<float>(M->width);
	const float vp_h = (M->viewport.Height > 0) ? static_cast<float>(M->viewport.Height) : static_cast<float>(M->height);
	const float pixels = (fx.terrain_tess_pixels > 1.0f) ? fx.terrain_tess_pixels : 1.0f;
	const float largest = (fx.terrain_tess_max < 1.0f) ? 1.0f : ((fx.terrain_tess_max > 64.0f) ? 64.0f : fx.terrain_tess_max);
	FFPTerrainTessCB& cb = M->tt_cb_data;
	FFPFloat4 head[8];
	Fx_Set_Float4(head[0], 0.5f * vp_w, 0.5f * vp_h, 1.0f / pixels, largest);
	// pixels per tile at distance d: tile * (half viewport height) * projection y scale / d
	Fx_Set_Float4(head[1], TT_TILE_WORLD * 0.5f * vp_h * M->transforms[WW3D_TS_PROJECTION].m[1][1], TT_FADE_START,
		1.0f / (TT_FADE_FULL - TT_FADE_START), amplitude);
	Fx_Set_Float4(head[2], (M->tr_cell > 0.0f) ? 1.0f / M->tr_cell : 0.1f, (float)M->tr_border,
		(M->tt_grid_w > 0) ? 1.0f / (float)M->tt_grid_w : 1.0f, (M->tt_grid_h > 0) ? 1.0f / (float)M->tt_grid_h : 1.0f);
	const float rock_reach = TT_ROCK_REACH * rock_amplitude;
	Fx_Set_Float4(head[3], 255.0f * M->tr_height_scale, ((rock_reach > 0.5f * amplitude) ? rock_reach : 0.5f * amplitude) + 1.0f,
		TT_SLOPE_END, 1.0f / TT_SLOPE_FADE);
	Fx_Set_Float4(head[4], cb.atlas.x, cb.atlas.y, fx.terrain_tess_probe ? 1.0f : 0.0f, M->tt_heights ? 1.0f : 0.0f);
	Fx_Set_Float4(head[5], rock_amplitude, TT_ROCK_START, 1.0f / TT_ROCK_WIDTH, TR_HEIGHT_EDGE);
	Fx_Set_Float4(head[6], 1.0f / TR_PERIOD_COARSE, 1.0f / TR_PERIOD_FINE, TT_ROCK_FADE_START,
		1.0f / (TT_ROCK_FADE_FULL - TT_ROCK_FADE_START));
	// spacing of the domain points: target pixels per edge piece over pixels per world unit, at least cell / largest factor
	Fx_Set_Float4(head[7], TT_ROCK_LOD_BIAS, ((M->tr_cell > 0.0f) ? M->tr_cell : 10.0f) / largest,
		(head[1].x > 0.0f) ? TT_TILE_WORLD * pixels / head[1].x : 0.0f, 0.0f);
	if (M->tt_cb_dirty || ::memcmp(head, &cb.view, sizeof(head)) != 0) {
		cb.view = head[0];
		cb.fade = head[1];
		cb.map = head[2];
		cb.shape = head[3];
		cb.atlas = head[4];
		cb.rock = head[5];
		cb.rock_scale = head[6];
		cb.rock_lod = head[7];
		Upload_Constant_Buffer(c, M->tt_cb, &cb, sizeof(cb));
		M->tt_cb_dirty = false;
	}
	c->HSSetShader(M->tt_hs, nullptr, 0);
	c->DSSetShader(M->tt_ds, nullptr, 0);
	c->HSSetConstantBuffers(RENDER_SLOT_CB_FRAME, 1, &M->cb_frame);
	c->DSSetConstantBuffers(RENDER_SLOT_CB_FRAME, 1, &M->cb_frame);
	c->HSSetConstantBuffers(RENDER_SLOT_CB_TERRAIN_TESS, 1, &M->tt_cb);
	c->DSSetConstantBuffers(RENDER_SLOT_CB_TERRAIN_TESS, 1, &M->tt_cb);
	GpuShaderResourceView* views[4] = { M->tr_srv[2], M->tt_weights, M->tt_mask, M->tt_atlas };
	c->DSSetShaderResources(0, 4, views);
	c->DSSetSamplers(0, 1, &M->tt_sampler);
	number_of_API_calls += 8;
	if (rock) {
		// rock textures for the domain stage, height field for the slope test of the hull stage
		GpuShaderResourceView* rock_views[2] = { M->tr_srv[0], M->tr_srv[1] };
		c->DSSetShaderResources(4, 2, rock_views);
		c->DSSetSamplers(1, 1, &M->tt_rock_sampler);
		c->HSSetShaderResources(0, 1, &M->tr_srv[2]);
		c->HSSetSamplers(0, 1, &M->tt_sampler);
		number_of_API_calls += 4;
	}
	M->tt_bound = true;
	M->tt_drawn = true;
	M->tt_stat_draws += 1.0;
}

void RenderCore::Tt_Unbind()
{
	GpuContext* c = M->context;
	if (c) {
		c->HSSetShader(nullptr, nullptr, 0);
		c->DSSetShader(nullptr, nullptr, 0);
		number_of_API_calls += 2;
	}
	M->tt_bound = false;
}

void RenderCore::Tt_Query_Begin()
{
	if (M->tt_query_open || !M->device || !M->context) return;
	Tt_Poll_Queries();
	M->tt_stat_sections += 1.0;
	for (unsigned i = 0; i < TT_QUERIES; ++i) {
		if (M->tt_query_state[i] != 0) continue;
		if (!M->tt_query[i]) {
			GPU_QUERY_DESC qd;
			qd.Query = GPU_QUERY_PIPELINE_STATISTICS;
			qd.MiscFlags = 0;
			if (FAILED(M->device->CreateQuery(&qd, &M->tt_query[i]))) return;
		}
		M->context->Begin(M->tt_query[i]);
		M->tt_query_state[i] = 1;
		M->tt_query_open = i + 1;
		return;
	}
}

void RenderCore::Tt_Query_End()
{
	if (!M->tt_query_open || !M->context) return;
	const unsigned i = M->tt_query_open - 1;
	M->context->End(M->tt_query[i]);
	M->tt_query_state[i] = 2;
	M->tt_query_open = 0;
}

void RenderCore::Tt_Poll_Queries()
{
	if (!M->context) return;
	for (unsigned i = 0; i < TT_QUERIES; ++i) {
		if (M->tt_query_state[i] != 2) continue;
		GPU_QUERY_DATA_PIPELINE_STATISTICS s;
		if (M->context->GetData(M->tt_query[i], &s, sizeof(s), GPU_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
		M->tt_stat_patches += (double)s.HSInvocations;
		M->tt_stat_points += (double)s.DSInvocations;
		M->tt_stat_triangles += (double)s.CInvocations;
		M->tt_stat_measured += 1.0;
		M->tt_query_state[i] = 0;
	}
}

void RenderCore::Set_Terrain_Tessellation(bool enable)
{
	M->tt_mode = enable;
	if (enable) {
		if (Tt_Active()) Tt_Query_Begin();
	}
	else {
		if (M->tt_query_open) Tt_Query_End();
		if (M->tt_bound) Tt_Unbind();
	}
}

void RenderCore::Set_Terrain_Tess_Data(const RenderTerrainTessData& data, bool active)
{
	if (!active) {
		if (data.atlas && data.atlas == M->tt_atlas_handle) {
			Gpu_Release(M->tt_atlas);
			M->tt_atlas_handle = nullptr;
			delete[] M->tt_weights_cpu; M->tt_weights_cpu = nullptr;
			delete[] M->tt_mask_cpu; M->tt_mask_cpu = nullptr;
			M->tt_grid_w = M->tt_grid_h = 0;
			Gpu_Release(M->tt_weights);
			Gpu_Release(M->tt_weights_tex);
			Gpu_Release(M->tt_mask);
			Gpu_Release(M->tt_mask_tex);
		}
		return;
	}
	Gpu_Release(M->tt_atlas);
	M->tt_atlas_handle = data.atlas;
	if (data.atlas && data.atlas->srv) {
		M->tt_atlas = data.atlas->srv;
		M->tt_atlas->AddRef();
	}
	FFPTerrainTessCB& cb = M->tt_cb_data;
	Fx_Set_Float4(cb.atlas, data.cell_u, data.cell_v, 0.0f, 0.0f);
	M->tt_heights = false;
	for (unsigned i = 0; i < data.class_count && i < (unsigned)RenderTerrainTessData::MAX_CLASSES; ++i)
		if (data.classes[i][3] > 0.0f) M->tt_heights = true;
	for (unsigned i = 0; i < FFP_TERRAIN_TESS_CLASSES; ++i) {
		if (i < data.class_count && i < (unsigned)RenderTerrainTessData::MAX_CLASSES && data.classes[i][2] > 0.0f)
			Fx_Set_Float4(cb.classes[i], data.classes[i][0], data.classes[i][1], data.classes[i][2], data.classes[i][3]);
		else
			Fx_Set_Float4(cb.classes[i], 0.0f, 0.0f, 2.0f, 0.0f);		// no heights; z > 0 keeps the modulo finite
	}
	Gpu_Release(M->tt_weights);
	Gpu_Release(M->tt_weights_tex);
	Gpu_Release(M->tt_mask);
	Gpu_Release(M->tt_mask_tex);
	delete[] M->tt_weights_cpu; M->tt_weights_cpu = nullptr;
	delete[] M->tt_mask_cpu; M->tt_mask_cpu = nullptr;
	M->tt_grid_w = M->tt_grid_h = 0;
	if (data.weights && data.width > 0 && data.height > 0) {
		const size_t n = static_cast<size_t>(data.width) * static_cast<size_t>(data.height);
		M->tt_weights_cpu = W3DNEWARRAY unsigned char[n * 4];
		::memcpy(M->tt_weights_cpu, data.weights, n * 4);
		M->tt_mask_cpu = W3DNEWARRAY unsigned char[n];
		::memset(M->tt_mask_cpu, 255, n);		// full displacement until the mask arrives
		M->tt_grid_w = data.width;
		M->tt_grid_h = data.height;
	}
	M->tt_cb_dirty = true;
	M->tt_logged = false;
}

void RenderCore::Set_Terrain_Tess_Mask(const unsigned char* mask, int width, int height)
{
	if (!mask || !M->tt_mask_cpu || width != M->tt_grid_w || height != M->tt_grid_h) return;
	::memcpy(M->tt_mask_cpu, mask, static_cast<size_t>(width) * static_cast<size_t>(height));
	if (M->tt_mask_tex && M->context) {
		M->context->UpdateSubresource(M->tt_mask_tex, 0, nullptr, M->tt_mask_cpu, static_cast<UINT>(width), 0);
		Ud_Upload("tessmask", M->tt_mask_tex, 0, M->tt_mask_cpu, static_cast<unsigned>(width * height), true);
		number_of_API_calls++;
	}
}

// ==================================================================================================
// 12. Dynamic terrain lights
// ==================================================================================================
//
// Explosions, muzzle flashes and other W3DDynamicLight objects light the terrain. On the CPU,
// HeightMapRenderObjClass::On_Frame_Update rewrote the colors of every touched terrain tile and uploaded the
// whole tile buffer each frame. Here the vertex shader does it (terrainlights.hlsli):
//
//  - Set_Terrain_Dynamic_Lights (from On_Frame_Update): the lights of the frame into cbTerrainLights.
//  - Set_Terrain_Height_Field (from HeightMapRenderObjClass::Render): the height field for the normals,
//    the same texture as the rock layer (t11).
//  - Enable_Terrain_Dynamic_Lights(true/false) around the terrain tile draws; Prepare_Draw binds b8/t11 of
//    the vertex stage for them and a buffer of zeros for everything else.
//  - Set_Terrain_Static_Lighting (from HeightMapRenderObjClass::Render): the static prelighting of
//    BaseHeightMapRenderObjClass::doTheLight (ambient, global and map lights). The tile vertex buffers then
//    hold white instead of the lit color, so scrolling and the third blend layer compute no light on the CPU.

bool RenderCore::Supports_Terrain_Dynamic_Lights()
{
	return M->device != nullptr;
}

void RenderCore::Set_Terrain_Dynamic_Lights(const RenderTerrainDynamicLight* lights, unsigned count)
{
	static_assert(FFP_TERRAIN_LIGHTS == static_cast<int>(RenderTerrainDynamicLight::MAX_LIGHTS), "light count differs");
	if (!lights) count = 0;
	if (count > FFP_TERRAIN_LIGHTS) count = FFP_TERRAIN_LIGHTS;
	if (count > M->tl_stat_max) M->tl_stat_max = count;
	FFPTerrainLightsCB& cb = M->tl_cb_data;
	static FFPTerrainLightsCB neu;		// 2.4 KB, kept off the stack
	neu = cb;
	neu.map.w = (float)count;
	for (unsigned i = 0; i < FFP_TERRAIN_LIGHTS; ++i) {
		if (i < count) {
			const RenderTerrainDynamicLight& l = lights[i];
			Fx_Set_Float4(neu.position[i], l.position[0], l.position[1], l.position[2], l.directional);
			Fx_Set_Float4(neu.range[i], l.mid_range, l.range, 0.0f, 0.0f);
			Fx_Set_Float4(neu.diffuse[i], l.diffuse[0], l.diffuse[1], l.diffuse[2], 0.0f);
			Fx_Set_Float4(neu.ambient[i], l.ambient[0], l.ambient[1], l.ambient[2], 0.0f);
		}
		else {
			Fx_Set_Float4(neu.position[i], 0.0f, 0.0f, 0.0f, 0.0f);
			Fx_Set_Float4(neu.range[i], 0.0f, 0.0f, 0.0f, 0.0f);
			Fx_Set_Float4(neu.diffuse[i], 0.0f, 0.0f, 0.0f, 0.0f);
			Fx_Set_Float4(neu.ambient[i], 0.0f, 0.0f, 0.0f, 0.0f);
		}
	}
	if (::memcmp(&neu, &cb, sizeof(cb)) != 0) {
		cb = neu;
		M->tl_cb_dirty = true;
	}
}

void RenderCore::Enable_Terrain_Dynamic_Lights(bool enable)
{
	M->tl_mode = false;
	if (!enable) return;
	if (M->tl_cb_data.map.w <= 0.0f && M->tl_cb_data.stat.x <= 0.0f) return;
	M->tl_mode = Tl_Ensure_Buffer();
}

bool RenderCore::Tl_Ensure_Buffer()
{
	if (!M->device || !M->context || !M->tr_heights) return false;
	if (!M->tl_cb) {
		M->tl_cb = Create_Constant_Buffer(M->device, sizeof(FFPTerrainLightsCB));
		if (!M->tl_cb) return false;
		M->tl_cb_dirty = true;
	}
	if (!Tr_Ensure_Height_Field()) return false;
	FFPTerrainLightsCB& cb = M->tl_cb_data;
	FFPFloat4 map;
	Fx_Set_Float4(map, (M->tr_cell > 0.0f) ? 1.0f / M->tr_cell : 0.1f, (float)M->tr_border, 255.0f * M->tr_height_scale, cb.map.w);
	if (::memcmp(&map, &cb.map, sizeof(map)) != 0 || cb.size.x != M->tr_width || cb.size.y != M->tr_height) {
		cb.map = map;
		cb.size.x = M->tr_width;
		cb.size.y = M->tr_height;
		cb.size.z = cb.size.w = 0;
		M->tl_cb_dirty = true;
	}
	if (M->tl_cb_dirty) {
		Upload_Constant_Buffer(M->context, M->tl_cb, &cb, sizeof(cb));
		M->tl_cb_dirty = false;
	}
	return true;
}

void RenderCore::Set_Terrain_Static_Lighting(const RenderTerrainStaticLighting* lighting)
{
	static_assert(FFP_TERRAIN_SCENE_LIGHTS == static_cast<int>(RenderTerrainStaticLighting::MAX_SCENE_LIGHTS), "light count differs");
	FFPTerrainLightsCB& cb = M->tl_cb_data;
	static FFPTerrainLightsCB neu;		// 2.4 KB, kept off the stack
	neu = cb;
	const unsigned global = lighting ? (lighting->global.count < 3 ? lighting->global.count : 3) : 0;
	const unsigned scene = lighting ? (lighting->scene_count < FFP_TERRAIN_SCENE_LIGHTS ? lighting->scene_count : FFP_TERRAIN_SCENE_LIGHTS) : 0;
	Fx_Set_Float4(neu.stat, lighting ? 1.0f : 0.0f, (float)global, (float)scene, 0.0f);
	if (lighting)
		Fx_Set_Float4(neu.stat_ambient, lighting->global.ambient[0], lighting->global.ambient[1], lighting->global.ambient[2], 0.0f);
	else
		Fx_Set_Float4(neu.stat_ambient, 0.0f, 0.0f, 0.0f, 0.0f);
	for (unsigned i = 0; i < 3; ++i) {
		const RenderTerrainLighting* g = (i < global) ? &lighting->global : nullptr;
		Fx_Set_Float4(neu.stat_ray[i], g ? g->ray[i][0] : 0.0f, g ? g->ray[i][1] : 0.0f, g ? g->ray[i][2] : 0.0f, 0.0f);
		Fx_Set_Float4(neu.stat_diffuse[i], g ? g->diffuse[i][0] : 0.0f, g ? g->diffuse[i][1] : 0.0f, g ? g->diffuse[i][2] : 0.0f, 0.0f);
	}
	for (unsigned i = 0; i < FFP_TERRAIN_SCENE_LIGHTS; ++i) {
		if (i < scene) {
			const RenderTerrainDynamicLight& l = lighting->scene[i];
			Fx_Set_Float4(neu.scene_position[i], l.position[0], l.position[1], l.position[2], l.directional);
			Fx_Set_Float4(neu.scene_range[i], l.mid_range, l.range, 0.0f, 0.0f);
			Fx_Set_Float4(neu.scene_diffuse[i], l.diffuse[0], l.diffuse[1], l.diffuse[2], 0.0f);
			Fx_Set_Float4(neu.scene_ambient[i], l.ambient[0], l.ambient[1], l.ambient[2], 0.0f);
		}
		else {
			Fx_Set_Float4(neu.scene_position[i], 0.0f, 0.0f, 0.0f, 0.0f);
			Fx_Set_Float4(neu.scene_range[i], 0.0f, 0.0f, 0.0f, 0.0f);
			Fx_Set_Float4(neu.scene_diffuse[i], 0.0f, 0.0f, 0.0f, 0.0f);
			Fx_Set_Float4(neu.scene_ambient[i], 0.0f, 0.0f, 0.0f, 0.0f);
		}
	}
	if (lighting) M->tl_stat_static += 1.0;
	if (::memcmp(&neu, &cb, sizeof(cb)) != 0) {
		cb = neu;
		M->tl_cb_dirty = true;
	}
}
