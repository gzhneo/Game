/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// rendercore_taa.cpp -- Direct3D 11 backend: temporal anti-aliasing by camera reprojection (RENDER_EFFECT_TAA).
//
// While the 3D scene is drawn into the scene target, its projection carries a sub-pixel offset from a Halton (2, 3)
// sequence of eight phases (Taa_Jitter_Projection, used where the frame and object constants are filled). The depth
// copy and the screen space effects keep the projection without offset. At the start of the post-processing
// (Fx_Scene_3D_Done) Fx_Run_TAA blends the frame into a history (two targets of the scene size, used in turn) and
// copies the result back into the scene (taa.ps.hlsl); bloom, sharpening, grading and dithering follow on the
// smoothed image. Without its effect nothing here runs and the projection stays as the engine set it.

#include "renderobjects.h"
#include "rendercore.h"
#include "renderstates.h"
#include "renderformat.h"
#include "rendercb.h"
#include "renderdevice.h"
#include "wwdebug.h"
#include "rendercore_internal.h"

/// Constants of taa.ps.hlsl (cbTaa).
struct TaaCB
{
	WW3DMatrixRaw	reproj;
	FFPFloat4		params;
};

static const unsigned TAA_PHASES   = 8;
static const float    TAA_NEW_WEIGHT = 0.1f;		///< share of the new frame in the history

static float Taa_Halton(unsigned index, unsigned base)
{
	float f = 1.0f, r = 0.0f;
	while (index > 0) {
		f /= static_cast<float>(base);
		r += f * static_cast<float>(index % base);
		index /= base;
	}
	return r;
}

// Inverse of an affine matrix with row vectors (rotation and scale in the upper 3x3, translation in row 3).
static bool Taa_Affine_Inverse(const WW3DMatrixRaw& a, WW3DMatrixRaw& out)
{
	const float (*m)[4] = a.m;
	const float c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
	const float c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
	const float c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
	const float det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
	if (det > -1e-12f && det < 1e-12f) return false;
	const float id = 1.0f / det;
	float r[3][3];
	r[0][0] = c00 * id;
	r[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * id;
	r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * id;
	r[1][0] = c01 * id;
	r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * id;
	r[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * id;
	r[2][0] = c02 * id;
	r[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * id;
	r[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * id;
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) out.m[i][j] = r[i][j];
		out.m[i][3] = 0.0f;
	}
	for (int j = 0; j < 3; ++j)
		out.m[3][j] = -(m[3][0] * r[0][j] + m[3][1] * r[1][j] + m[3][2] * r[2][j]);
	out.m[3][3] = 1.0f;
	return true;
}

bool RenderCore::Taa_Wanted() const
{
	return (RenderDevice::Get_Effect_Settings().flags & RENDER_EFFECT_TAA) != 0 && !M->taa_failed;
}

void RenderCore::Taa_Scene_Begin()
{
	// Jitter only when the resolve can run: without a readable depth (multisampling with the depth effects off) the
	// frames would shake without being blended.
	M->taa_in_scene = Taa_Wanted() && !M->current_render_target && Fx_Depth_Readable();
	M->taa_jittered = M->taa_in_scene;
	M->frame_cb_dirty = true;
	M->object_cb_dirty = true;
}

void RenderCore::Taa_Scene_End()
{
	if (!M->taa_in_scene) return;
	M->taa_in_scene = false;
	M->frame_cb_dirty = true;
	M->object_cb_dirty = true;
}

// This frame's sub-pixel offset in pixels (x right, y down).
static void Taa_Jitter_Pixels(unsigned frame, float* x, float* y)
{
	const unsigned phase = (frame % TAA_PHASES) + 1;
	*x = Taa_Halton(phase, 2) - 0.5f;
	*y = Taa_Halton(phase, 3) - 0.5f;
}

// The projection for the frame and object constants: with TAA, inside the 3D scene and into the scene target, shifted
// by this frame's jitter (x and y of the clip position move by the offset times w).
const WW3DMatrixRaw& RenderCore::Taa_Jitter_Projection()
{
	const WW3DMatrixRaw& p = M->transforms[WW3D_TS_PROJECTION];
	if (!M->taa_in_scene || M->current_render_target || M->viewport.Width == 0 || M->viewport.Height == 0) return p;
	float px, py;
	Taa_Jitter_Pixels(M->fx_frame, &px, &py);
	const float jx = px * 2.0f / static_cast<float>(M->viewport.Width);
	const float jy = -py * 2.0f / static_cast<float>(M->viewport.Height);
	M->taa_projection = p;
	for (int r = 0; r < 4; ++r) {
		M->taa_projection.m[r][0] += jx * p.m[r][3];
		M->taa_projection.m[r][1] += jy * p.m[r][3];
	}
	return M->taa_projection;
}

bool RenderCore::Taa_Ensure_Resources()
{
	if (M->taa_failed || !M->device) return false;
	if (M->taa_tex[0] && (M->taa_format != M->scene_format || M->taa_width != M->scene_width || M->taa_height != M->scene_height))
		Taa_Release_Targets();
	if (!M->taa_tex[0]) {
		GPU_TEXTURE2D_DESC td;
		::ZeroMemory(&td, sizeof(td));
		td.Width = M->scene_width; td.Height = M->scene_height; td.MipLevels = 1; td.ArraySize = 1;
		td.Format = M->scene_format;
		td.SampleDesc.Count = 1;
		td.Usage = GPU_USAGE_DEFAULT;
		td.BindFlags = GPU_BIND_RENDER_TARGET | GPU_BIND_SHADER_RESOURCE;
		for (int i = 0; i < 2; ++i) {
			if (FAILED(M->device->CreateTexture2D(&td, nullptr, &M->taa_tex[i])) ||
				FAILED(M->device->CreateRenderTargetView(M->taa_tex[i], nullptr, &M->taa_rtv[i])) ||
				FAILED(M->device->CreateShaderResourceView(M->taa_tex[i], nullptr, &M->taa_srv[i]))) {
				Taa_Release_Targets();
				return false;
			}
		}
		M->taa_format = M->scene_format;
		M->taa_width = M->scene_width;
		M->taa_height = M->scene_height;
		M->taa_valid = false;
	}
	if (!M->taa_cb) M->taa_cb = Create_Constant_Buffer(M->device, sizeof(TaaCB));
	for (unsigned pass = 0; pass < 2; ++pass) {
		if (M->taa_ps[pass]) continue;
		char value[2] = { (char)('0' + pass), 0 };
		GPU_SHADER_MACRO macros[2];
		macros[0].Name = "TAA_PASS"; macros[0].Definition = value;
		macros[1].Name = nullptr;    macros[1].Definition = nullptr;
		M->taa_ps[pass] = Fx_Create_Pixel_Shader(M->device, "taa.ps.hlsl", macros);
		if (!M->taa_ps[pass]) {
			WWDEBUG_SAY(("D3D11 effects: taa.ps.hlsl (pass %u) does not compile, TAA off\n", pass));
			M->taa_failed = true;
			return false;
		}
	}
	return M->taa_cb != nullptr;
}

void RenderCore::Fx_Run_TAA(int x0, int y0, int x1, int y1)
{
	if (!Taa_Wanted()) {
		M->taa_valid = false;
		return;
	}
	// Once per frame, only with this frame's depth copy (the reprojection needs it).
	if (M->taa_frame == M->fx_frame + 1) return;
	M->taa_frame = M->fx_frame + 1;
	if (!M->fx_depth_valid || !M->fx_depth_handle || !Fx_Ensure_Post_Target() || !Taa_Ensure_Resources()) {
		M->taa_valid = false;
		return;
	}

	GpuShaderResourceView* source = M->scene_srv;
	if (M->sample_count > 1) {
		if (!M->resolve_target || !M->fx_resolve_srv) { M->taa_valid = false; return; }
		M->context->ResolveSubresource(M->resolve_target, 0, M->scene_color, 0, M->scene_format);
		source = M->fx_resolve_srv;
	}

	// Camera space of this frame -> clip space of the previous frame, both from the depth copy (the scene camera, without
	// jitter); by now the engine may have set another projection for the overlays.
	WW3DMatrixRaw view_proj, inv_view;
	Mat_Multiply(M->fx_view, M->fx_proj, view_proj);
	const bool rect_same = M->taa_rect[0] == x0 && M->taa_rect[1] == y0 && M->taa_rect[2] == x1 && M->taa_rect[3] == y1;
	TaaCB cb;
	::ZeroMemory(&cb, sizeof(cb));
	const bool history = M->taa_valid && rect_same && Taa_Affine_Inverse(M->fx_view, inv_view);
	if (history) Mat_Multiply(inv_view, M->taa_prev_view_proj, cb.reproj);
	float px, py;
	Taa_Jitter_Pixels(M->fx_frame, &px, &py);
	Fx_Set_Float4(cb.params, TAA_NEW_WEIGHT, history ? 1.0f : 0.0f, px, py);
	Upload_Constant_Buffer(M->context, M->taa_cb, &cb, sizeof(cb));

	FFPEffectsCB& ecb = M->fx_cb_data;
	const float out_mode = (M->scene_format == GPU_FORMAT_B8G8R8A8_UNORM) ? 0.0f :
		((M->scene_format == GPU_FORMAT_R11G11B10_FLOAT) ? 2.0f : 1.0f);
	if (ecb.haze.w != out_mode || M->fx_cb_dirty) {
		ecb.haze.w = out_mode;
		Fx_Upload_Constants();
	}

	GpuContext* c = M->context;
	const unsigned cur = M->taa_index, prev = cur ^ 1u;
	WW3DViewport vp;
	vp.X = (unsigned)x0;  vp.Y = (unsigned)y0;  vp.Width = (unsigned)(x1 - x0);  vp.Height = (unsigned)(y1 - y0);
	vp.MinZ = 0.0f;  vp.MaxZ = 1.0f;
	Fx_Unbind_Scene_Copies();
	c->PSSetConstantBuffers(RENDER_SLOT_CB_TAA, 1, &M->taa_cb);
	c->PSSetShaderResources(2, 1, &M->taa_srv[prev]);
	Fx_Full_Screen_Pass(M->taa_rtv[cur], vp, M->taa_ps[0], source, M->fx_depth_handle->srv, M->fx_blend_copy);
	GpuShaderResourceView* null_srv = nullptr;
	c->PSSetShaderResources(2, 1, &null_srv);
	Fx_Full_Screen_Pass(M->scene_rtv, vp, M->taa_ps[1], M->taa_srv[cur], nullptr, M->fx_blend_copy);
	number_of_API_calls += 4;
	Fx_Restore_Bindings();

	M->taa_index = prev;
	M->taa_prev_view_proj = view_proj;
	M->taa_rect[0] = x0;  M->taa_rect[1] = y0;  M->taa_rect[2] = x1;  M->taa_rect[3] = y1;
	M->taa_valid = true;
}

void RenderCore::Taa_Release_Targets()
{
	for (int i = 0; i < 2; ++i) {
		Gpu_Release(M->taa_srv[i]);
		Gpu_Release(M->taa_rtv[i]);
		Gpu_Release(M->taa_tex[i]);
	}
	M->taa_valid = false;
}

void RenderCore::Taa_Release_Resources()
{
	Taa_Release_Targets();
	for (int i = 0; i < 2; ++i) Gpu_Release(M->taa_ps[i]);
	Gpu_Release(M->taa_cb);
	M->taa_failed = false;
}
