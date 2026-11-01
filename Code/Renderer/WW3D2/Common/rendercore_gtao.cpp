/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// rendercore_gtao.cpp -- Direct3D 11 backend: ambient occlusion by horizon search (GTAO) at full resolution.
//
// Runs in place of the SSAO method (Fx_Run_SSAO) after the opaque scene and its depth copy, with the same radius,
// strength and range. Pass 0 writes the visibility of every pixel into an R8 target of the scene size; pass 1 averages
// it over 4x4 pixels with depth weights and multiplies the result into the scene target (gtao.ps.hlsl).

#include "renderobjects.h"
#include "rendercore.h"
#include "renderstates.h"
#include "renderformat.h"
#include "rendercb.h"
#include "renderdevice.h"
#include "wwdebug.h"
#include "rendercore_internal.h"

bool RenderCore::Fx_Ensure_GTAO_Target()
{
	if (M->fx_gtao_tex) return true;
	if (!M->device) return false;
	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width = M->scene_width; td.Height = M->scene_height; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = GPU_FORMAT_R8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = GPU_USAGE_DEFAULT;
	td.BindFlags = GPU_BIND_RENDER_TARGET | GPU_BIND_SHADER_RESOURCE;
	if (FAILED(M->device->CreateTexture2D(&td, nullptr, &M->fx_gtao_tex)) ||
		FAILED(M->device->CreateRenderTargetView(M->fx_gtao_tex, nullptr, &M->fx_gtao_rtv)) ||
		FAILED(M->device->CreateShaderResourceView(M->fx_gtao_tex, nullptr, &M->fx_gtao_srv))) {
		Gpu_Release(M->fx_gtao_srv);
		Gpu_Release(M->fx_gtao_rtv);
		Gpu_Release(M->fx_gtao_tex);
		return false;
	}
	return true;
}

void* RenderCore::Fx_GTAO_Shader(unsigned pass)
{
	if (!M->fx_gtao_ps[pass] && !M->fx_gtao_failed) {
		char value[2] = { (char)('0' + pass), 0 };
		GPU_SHADER_MACRO macros[2];
		macros[0].Name = "GTAO_PASS"; macros[0].Definition = value;
		macros[1].Name = nullptr;     macros[1].Definition = nullptr;
		M->fx_gtao_ps[pass] = Fx_Create_Pixel_Shader(M->device, "gtao.ps.hlsl", macros);
		if (!M->fx_gtao_ps[pass]) {
			WWDEBUG_SAY(("D3D11 effects: gtao.ps.hlsl (pass %u) does not compile, GTAO off\n", pass));
			M->fx_gtao_failed = true;
		}
	}
	return M->fx_gtao_ps[pass];
}

void RenderCore::Fx_Run_GTAO()
{
	if (!M->fx_depth_valid || !Fx_Ensure_GTAO_Target()) return;
	void* ps0 = Fx_GTAO_Shader(0);
	void* ps1 = Fx_GTAO_Shader(1);
	if (!ps0 || !ps1) return;

	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	Fx_Set_Float4(M->fx_cb_data.ssao, fx.ssao_radius, fx.ssao_strength, 0.0f, fx.ssao_range);
	Fx_Upload_Constants();

	const WW3DViewport& full = M->viewport;
	GpuShaderResourceView* depth = M->fx_depth_handle->srv;
	Fx_Full_Screen_Pass(M->fx_gtao_rtv, full, ps0, nullptr, depth, M->fx_blend_copy);
	Fx_Full_Screen_Pass(M->scene_rtv, full, ps1, M->fx_gtao_srv, depth, M->fx_blend_mul);
	Fx_Restore_Bindings();
}

void RenderCore::Fx_Release_GTAO_Targets()
{
	Gpu_Release(M->fx_gtao_srv);
	Gpu_Release(M->fx_gtao_rtv);
	Gpu_Release(M->fx_gtao_tex);
}

void RenderCore::Fx_Release_GTAO_Shaders()
{
	for (int i = 0; i < 2; ++i) Gpu_Release(M->fx_gtao_ps[i]);
	M->fx_gtao_failed = false;
}
