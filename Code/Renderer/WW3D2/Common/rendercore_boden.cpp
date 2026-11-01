/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// rendercore_boden.cpp -- render core: terrain light per pixel ("terrainlight", RENDER_EFFECT_TERRAIN_LIGHT).
//
// The terrain is prelit on the GPU per vertex (ffp.vs.hlsl, terrainlights.hlsli: ambient, map lights, global
// lights, dynamic lights). With the switch the terrain pixel shaders take the permutation TERRAIN_LIGHT and evaluate
// the same model per pixel with the interpolated terrain normal (terrainpixellight.hlsli):
//
//  - Prepare_Draw asks Tb_Select for the permutation of a lit terrain draw in the scene target, on top of the
//    detail, rock and relief permutations that are on;
//  - the vertex stage then gets the buffer of zeros instead of cbTerrainLights, so the vertex color carries only the
//    tile color and the blend weight;
//  - Tb_Bind binds cbTerrainLights, the height field and its sampler to the pixel stage.
//
// Only while the static prelighting is in the shader. In the CPU cases (HeightMapRenderObjClass::canLightOnGpu) the
// vertex color holds the light and the draws keep the vertex path. The water reflection keeps it as well.

#include <string.h>
#include "rendercore_internal.h"
#include "renderobjects.h"
#include "rendercore.h"
#include "rendercb.h"
#include "renderdevice.h"
#include "renderslots.h"
#include "wwdebug.h"

bool RenderCore::Tb_Active()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	if (!(fx.flags & RENDER_EFFECT_TERRAIN_LIGHT)) return false;
	if (!M->tl_mode || M->tl_cb_data.stat.x <= 0.0f || !M->tl_cb || !M->tr_srv[2]) return false;
	if (M->current_render_target || !M->context || !M->device) return false;
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
	if (!M->tb_logged) {
		M->tb_logged = true;
		WWDEBUG_SAY(("Terrain light: per pixel\n"));
	}
	return true;
}

WW3DPixelShaderHandle RenderCore::Tb_Pixel_Shader(WW3DPixelShaderHandle base, bool with_detail, bool with_rock, bool with_normal)
{
	const int k = (with_detail ? 1 : 0) | (with_rock ? 2 : 0) | (with_normal ? 4 : 0);
	if (base->light_variant[k] || base->light_failed[k] || !base->detail_source) return base->light_variant[k];
	GPU_SHADER_MACRO macros[5];
	int m = 0;
	macros[m].Name = "TERRAIN_LIGHT"; macros[m++].Definition = "1";
	if (with_detail) { macros[m].Name = "TERRAIN_DETAIL"; macros[m++].Definition = "1"; }
	if (with_rock) { macros[m].Name = "TERRAIN_ROCK"; macros[m++].Definition = "1"; }
	if (with_normal) { macros[m].Name = "TERRAIN_NORMAL"; macros[m++].Definition = "1"; }
	macros[m].Name = nullptr;
	macros[m].Definition = nullptr;
	GpuBlob* code = nullptr;
	if (SUCCEEDED(Compile_FFP_Shader(base->detail_source, base->detail_source_len, base->detail_name, macros, "ps_4_0", &code)) && code) {
		WW3DPixelShaderObject* obj = W3DNEW WW3DPixelShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		if (SUCCEEDED(M->device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader)))
			base->light_variant[k] = obj;
		else
			delete obj;
	}
	if (code) code->Release();
	if (!base->light_variant[k]) {
		WWDEBUG_SAY(("Terrain light: %s with TERRAIN_LIGHT does not compile, vertex light there\n", base->detail_name));
		base->light_failed[k] = true;
	}
	return base->light_variant[k];
}

bool RenderCore::Tb_Select(WW3DPixelShaderHandle& ps, bool with_detail, bool with_rock, bool with_normal)
{
	WW3DPixelShaderHandle base = M->pixel_shader;
	if (!base || !base->detail_source) return false;
	// ps is the terrain shader itself or the permutation of its layers chosen before
	if (ps != base && !with_detail && !with_rock && !with_normal) return false;
	if (!Tb_Active()) return false;
	WW3DPixelShaderHandle variant = Tb_Pixel_Shader(base, with_detail, with_rock, with_normal);
	if (!variant) return false;
	ps = variant;
	M->tb_stat_draws += 1.0;
	return true;
}

void RenderCore::Tb_Bind()
{
	GpuContext* c = M->context;
	c->PSSetConstantBuffers(RENDER_SLOT_CB_TERRAIN_LIGHTS, 1, &M->tl_cb);
	c->PSSetShaderResources(RENDER_SLOT_T_TERRAIN_HEIGHT, 1, &M->tr_srv[2]);
	c->PSSetSamplers(RENDER_SLOT_S_TERRAIN_HEIGHT, 1, &M->tr_height_sampler);
	number_of_API_calls += 3;
}
