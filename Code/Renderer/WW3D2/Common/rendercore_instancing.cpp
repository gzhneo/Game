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

// rendercore_instancing.cpp -- Direct3D 11 backend: GPU skinning, particles, instanced meshes, internal render resolution.

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
// 13. Skinning on the GPU
// ==================================================================================================
//
// W3D skins bind every vertex to one bone. On the CPU, SkinFVFCategoryContainer::Render transformed all
// visible skin vertices every frame into a dynamic buffer. With GPU skinning the mesh keeps its vertices
// in bone space in a static buffer (bone index in UV set 2, MeshModelClass::Get_GPU_Skin) and
// Set_Skin_Bones passes the bone matrices; ffp.vs.hlsl (skin.hlsli) deforms position and normal.
// Shadow casters record their palette and draw with shadow.vs.hlsl (SHADOW_SKIN).

unsigned RenderCore::Max_Skin_Bones()
{
	return M->device ? static_cast<unsigned>(FFP_SKIN_BONES) : 0u;
}

void RenderCore::Set_Skin_Bones(const float* rows, unsigned count)
{
	if (!rows) count = 0;
	if (count > FFP_SKIN_BONES) count = 0;		// the caller checks Max_Skin_Bones; never draw half a palette
	if (count == 0) {
		M->sk_count = 0;
		return;
	}
	FFPFloat4 info;
	Fx_Set_Float4(info, (float)count, 0.0f, 0.0f, 0.0f);
	const size_t bytes = count * 3 * sizeof(FFPFloat4);
	if (M->sk_count == count && !M->sk_dirty && ::memcmp(M->sk_cb_data.bones, rows, bytes) == 0) return;
	M->sk_cb_data.info = info;
	::memcpy(M->sk_cb_data.bones, rows, bytes);
	M->sk_count = count;
	M->sk_dirty = true;
}

// ==================================================================================================
// 14. Particles on the GPU
// ==================================================================================================
//
// PointGroupClass built four vertices per particle on the CPU (view transform, rotated corners, colour and
// UV per vertex) and the sorting renderer sorted two triangles per particle. Now a particle is one instance
// (RenderParticleInstance, 24 bytes) in the Draw_Primitive_UP ring buffer; particles.vs.hlsl builds the quad
// from SV_VertexID with the corner tables of PointGroupClass (cbParticleTable, b11) and the corner UVs
// (cbParticle, b10). Prepare_Draw binds everything else (blend, depth, textures, pixel shader, soft particles)
// as for any draw. Particles cast no shadow-map shadow (not recorded).
//
// With RENDER_EFFECT_PARTICLE_LIGHT, smoke and dust (not additive particles) take the light of the time of day
// relative to the afternoon (Set_Particle_Lighting) and the dynamic lights of cbTerrainLights; with
// RENDER_EFFECT_PARTICLE_GROUND, billboards are kept above the terrain (height field t11).

static const unsigned PA_MAX_PER_DRAW = UP_RING_BYTES / sizeof(RenderParticleInstance);

bool RenderCore::Pa_Ensure_Resources()
{
	if (M->pa_failed || !M->device) return false;
	if (M->pa_vs && M->pa_layout && M->pa_cb && M->pa_table_cb) return true;
	Pa_Release_Resources();
	unsigned length = 0;
	char* source = Load_Shader_Source("particles.vs.hlsl", &length);
	GpuBlob* code = nullptr;
	if (source && SUCCEEDED(Compile_FFP_Shader(source, length, "particles.vs.hlsl", nullptr, "vs_4_0", &code)) && code) {
		WW3DVertexShaderObject* obj = W3DNEW WW3DVertexShaderObject;
		::ZeroMemory(obj, sizeof(*obj));
		obj->refcount = 1;
		obj->bytecode = code;
		if (SUCCEEDED(M->device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &obj->shader))) {
			M->pa_vs = obj;
			code = nullptr;
		}
		else delete obj;
	}
	if (code) code->Release();
	delete[] source;
	if (M->pa_vs) {
		static_assert(sizeof(RenderParticleInstance) == 24, "instance layout below");
		GPU_INPUT_ELEMENT_DESC el[3] = {
			{ "POSITION", 0, GPU_FORMAT_R32G32B32A32_FLOAT, 0, 0,  GPU_INPUT_PER_INSTANCE_DATA, 1 },
			{ "COLOR",    0, GPU_FORMAT_R8G8B8A8_UNORM,     0, 16, GPU_INPUT_PER_INSTANCE_DATA, 1 },
			{ "TEXCOORD", 0, GPU_FORMAT_R8G8B8A8_UINT,      0, 20, GPU_INPUT_PER_INSTANCE_DATA, 1 },
		};
		GpuBlob* bc = static_cast<GpuBlob*>(M->pa_vs->bytecode);
		if (FAILED(M->device->CreateInputLayout(el, 3, bc->GetBufferPointer(), bc->GetBufferSize(), &M->pa_layout)))
			M->pa_layout = nullptr;
	}
	M->pa_cb = Create_Constant_Buffer(M->device, sizeof(FFPParticleCB));
	M->pa_table_cb = Create_Constant_Buffer(M->device, sizeof(FFPParticleTableCB));
	if (!M->pa_vs || !M->pa_layout || !M->pa_cb || !M->pa_table_cb) {
		WWDEBUG_SAY(("Render: particles.vs.hlsl or its resources unavailable, particles stay on the CPU\n"));
		Pa_Release_Resources();
		M->pa_failed = true;
		return false;
	}
	M->pa_table_dirty = true;
	return true;
}

void RenderCore::Pa_Release_Resources()
{
	if (M->pa_vs) { Release_Resource(M->pa_vs); M->pa_vs = nullptr; }
	Gpu_Release(M->pa_layout);
	Gpu_Release(M->pa_cb);
	Gpu_Release(M->pa_table_cb);
	M->pa_table_dirty = true;
	::memset(&M->pa_cb_data, 0xFF, sizeof(M->pa_cb_data));		// no valid upload yet
}

// ==================================================================================================
// Instanced meshes
// ==================================================================================================
//
// Between Begin_Mesh_Instances and End_Mesh_Instances (TextureCategoryClass::Render, runs of the same
// polygon renderer) indexed draws of the uber shader are collected instead of drawn. A draw joins the
// collected ones if its primitive range and its object constants apart from the four matrices are the
// same; the caller guarantees that nothing else changes in between (only lights, transform and the
// NORMALIZENORMALS state, all of them in the object constants). Mi_Flush draws them as one
// DrawIndexedInstanced; the vertex shader reads each instance's matrices from a constant buffer, indexed
// by an instance number from stream 1 (not 16 per-instance attributes: more than some drivers take; not
// SV_InstanceID or Buffer<>: Wine's HLSL compiler lacks them). A single draw goes out unchanged, and so
// does everything if the INSTANCED permutation does not compile.

void RenderCore::Begin_Mesh_Instances()
{
	if (M->mi_failed) return;
	M->mi_open = true;
}

void RenderCore::End_Mesh_Instances()
{
	if (!M->mi_open) return;
	Mi_Flush();
	M->mi_open = false;
}

bool RenderCore::Mi_Capture(unsigned type, int base_vertex, unsigned min_index, unsigned num_vertices,
							  unsigned start_index, unsigned index_count)
{
	// Only the plain uber shader: custom vertex shaders, skins, terrain and particle modes draw as they are.
	if (M->vertex_shader || M->sk_count > 0 || M->td_mode || M->tl_mode || M->pa_mode || M->pa_terrain || !M->context) {
		Mi_Flush();
		return false;
	}
	Fill_Object_Constants();
	M->object_cb_dirty = true;

	const size_t matrix_bytes = offsetof(FFPObjectCB, matDiffuse);
	if (M->mi_count > 0) {
		const bool same = M->mi_type == type && M->mi_base_vertex == base_vertex && M->mi_min_index == min_index &&
			M->mi_num_vertices == num_vertices && M->mi_start_index == start_index && M->mi_index_count == index_count &&
			::memcmp(reinterpret_cast<const char*>(&M->object_cb) + matrix_bytes,
					 reinterpret_cast<const char*>(&M->mi_cb) + matrix_bytes, sizeof(FFPObjectCB) - matrix_bytes) == 0;
		if (!same || M->mi_count >= MI_MAX_INSTANCES) {
			Mi_Flush();
			Fill_Object_Constants();		// Mi_Flush left the collected draws' constants behind
			M->object_cb_dirty = true;
		}
	}
	if (M->mi_count == 0) {
		M->mi_cb = M->object_cb;
		M->mi_type = type;
		M->mi_base_vertex = base_vertex;
		M->mi_min_index = min_index;
		M->mi_num_vertices = num_vertices;
		M->mi_start_index = start_index;
		M->mi_index_count = index_count;
	}
	::memcpy(&M->mi_matrices[M->mi_count * 4], &M->object_cb, matrix_bytes);
	++M->mi_count;

	if (M->sh_recording) Sh_Record(true, index_count, start_index, base_vertex, min_index, num_vertices, 0, 0);
	return true;
}

bool RenderCore::Mi_Upload(unsigned count)
{
	const unsigned bytes = count * 4 * sizeof(WW3DMatrixRaw);
	const unsigned window = MI_MAX_INSTANCES * 4 * sizeof(WW3DMatrixRaw);
	M->mi_ring_count = 0;
	// In the ring (appended without waiting) together with the constants Prepare_Draw writes next: a wrap
	// in between would discard the matrices.
	if (M->cbr_context && Cbr_Reserve(Cbr_Size(bytes, window) + Cbr_Size(sizeof(M->object_cb), 0) +
									  Cbr_Size(FFP_PIECE_VS_REGISTERS * 16, 0) + Cbr_Size(FFP_PIECE_PS_REGISTERS * 16, 0)) &&
		Cbr_Write(M->mi_matrices, bytes, window, &M->mi_ring_first, &M->mi_ring_count)) {
		return true;
	}
	M->mi_ring_count = 0;
	Upload_Constant_Buffer(M->context, M->mi_matrix_cb, M->mi_matrices, bytes);
	return true;
}

void RenderCore::Mi_Flush()
{
	const unsigned count = M->mi_count;
	if (count == 0) return;
	M->mi_count = 0;

	// The constants of the collected draws, not of the current state (a mismatching draw has already set
	// its lights and matrices).
	M->object_cb = M->mi_cb;
	M->object_cb_dirty = true;
	M->mi_preset = true;

	bool instanced = false;
	if (count > 1 && !M->mi_failed) {
		if (!M->mi_matrix_cb) {
			unsigned numbers[MI_MAX_INSTANCES];
			for (unsigned i = 0; i < MI_MAX_INSTANCES; ++i) numbers[i] = i;
			GPU_BUFFER_DESC bd;
			::ZeroMemory(&bd, sizeof(bd));
			bd.ByteWidth = sizeof(numbers);
			bd.Usage     = GPU_USAGE_IMMUTABLE;
			bd.BindFlags = GPU_BIND_VERTEX_BUFFER;
			GPU_SUBRESOURCE_DATA init;
			::ZeroMemory(&init, sizeof(init));
			init.pSysMem = numbers;
			M->mi_matrix_cb = Create_Constant_Buffer(M->device, MI_MAX_INSTANCES * 4 * sizeof(WW3DMatrixRaw));
			if (!M->mi_matrix_cb || FAILED(M->device->CreateBuffer(&bd, &init, &M->mi_index_vb))) {
				Gpu_Release(M->mi_matrix_cb);
				Gpu_Release(M->mi_index_vb);
				M->mi_failed = true;
			}
		}
		if (M->mi_matrix_cb) instanced = Mi_Upload(count);
	}

	if (instanced) {
		M->mi_instanced = true;
		Ud_Draw_Args("instanced", M->mi_index_count, M->mi_start_index, M->mi_base_vertex);
		const bool ok = Prepare_Draw(M->mi_type);
		M->mi_instanced = false;
		if (!ok) {
			// The INSTANCED permutation is missing: from now on every draw on its own.
			M->mi_failed = true;
			instanced = false;
		}
		else {
			const UINT stride = sizeof(unsigned);
			const UINT offset = 0;
			M->context->IASetVertexBuffers(1, 1, &M->mi_index_vb, &stride, &offset);
			if (M->mi_ring_count) {
				M->cbr_context->VSSetConstantBuffers1(RENDER_SLOT_CB_INSTANCES, 1, &M->cbr_buffer, &M->mi_ring_first, &M->mi_ring_count);
			}
			else {
				M->context->VSSetConstantBuffers(RENDER_SLOT_CB_INSTANCES, 1, &M->mi_matrix_cb);
			}
			M->context->DrawIndexedInstanced(M->mi_index_count, count, M->mi_start_index, M->mi_base_vertex, 0);
			GpuBuffer* none = nullptr;
			M->context->IASetVertexBuffers(1, 1, &none, &offset, &offset);
			number_of_API_calls += 4;
			M->mi_stat_draws += 1.0;
			M->mi_stat_instances += (double)count;
		}
	}
	if (!instanced) {
		// One draw (or no instancing): each with its own matrices, as without batching.
		for (unsigned i = 0; i < count; ++i) {
			::memcpy(&M->object_cb, &M->mi_matrices[i * 4], offsetof(FFPObjectCB, matDiffuse));
			M->object_cb_dirty = true;
			Ud_Draw_Args("indexed", M->mi_index_count, M->mi_start_index, M->mi_base_vertex);
			if (Prepare_Draw(M->mi_type)) {
				M->context->DrawIndexed(M->mi_index_count, M->mi_start_index, M->mi_base_vertex);
				number_of_API_calls++;
			}
		}
	}
	M->mi_preset = false;
	M->object_cb_dirty = true;
}

bool RenderCore::Supports_Particle_Instancing()
{
	return M->device && M->pa_table_valid && Pa_Ensure_Resources();
}

void RenderCore::Set_Particle_Tables(const float* rotation, const float* ground)
{
	if (!rotation || !ground) return;
	for (unsigned i = 0; i < 512; ++i)
		Fx_Set_Float4(M->pa_table.corner[i], rotation[i * 4], rotation[i * 4 + 1], rotation[i * 4 + 2], rotation[i * 4 + 3]);
	for (unsigned i = 0; i < 256; ++i)
		Fx_Set_Float4(M->pa_table.ground[i], ground[i * 4], ground[i * 4 + 1], ground[i * 4 + 2], ground[i * 4 + 3]);
	M->pa_table_valid = true;
	M->pa_table_dirty = true;
}

bool RenderCore::Pa_Prepare(const RenderParticleSetup& setup)
{
	if (!M->context || !Ensure_Draw_Resources() || !Pa_Ensure_Resources()) return false;
	GpuContext* c = M->context;
	if (M->pa_table_dirty) {
		Upload_Constant_Buffer(c, M->pa_table_cb, &M->pa_table, sizeof(M->pa_table));
		M->pa_table_dirty = false;
	}
	// Lighting and the terrain floor read cbTerrainLights (dynamic lights, height field constants) and the
	// height field; without them (no terrain height field yet) both stay off.
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	const bool want_light = setup.lit && (fx.flags & RENDER_EFFECT_PARTICLE_LIGHT) && fx.particle_light > 0.0f;
	const bool want_ground = setup.billboard && (fx.flags & RENDER_EFFECT_PARTICLE_GROUND);
	const bool terrain = (want_light || want_ground) && Tl_Ensure_Buffer();
	FFPParticleCB cb;
	Fx_Set_Float4(cb.flags, setup.billboard ? 1.0f : 0.0f, want_light ? 1.0f : 0.0f, (want_ground && terrain) ? 1.0f : 0.0f,
		(want_light && terrain) ? 1.0f : 0.0f);
	Fx_Set_Float4(cb.uv01, setup.uv[0][0], setup.uv[0][1], setup.uv[1][0], setup.uv[1][1]);
	Fx_Set_Float4(cb.uv23, setup.uv[2][0], setup.uv[2][1], setup.uv[3][0], setup.uv[3][1]);
	if (M->pa_light_valid)
		Fx_Set_Float4(cb.light, M->pa_light[0], M->pa_light[1], M->pa_light[2], fx.particle_light);
	else
		Fx_Set_Float4(cb.light, 1.0f, 1.0f, 1.0f, fx.particle_light);
	if (::memcmp(&cb, &M->pa_cb_data, sizeof(cb)) != 0) {
		M->pa_cb_data = cb;
		Upload_Constant_Buffer(c, M->pa_cb, &cb, sizeof(cb));
	}
	GpuBuffer* cbs[2] = { M->pa_cb, M->pa_table_cb };
	c->VSSetConstantBuffers(RENDER_SLOT_CB_PARTICLE, 2, cbs);	// particle, table
	number_of_API_calls++;

	M->pa_mode = true;
	M->pa_terrain = terrain;
	const bool ready = Prepare_Draw(WW3D_PT_TRIANGLELIST);
	M->pa_mode = false;
	M->pa_terrain = false;
	// The particle layout and shader must not stay in the binding cache for the next ordinary draw.
	M->bound_layout = (GpuInputLayout*)1;
	M->bound_vs = (GpuVertexShader*)1;
	return ready;
}

bool RenderCore::Pa_Write_Ring(const void* data, unsigned bytes, unsigned* offset_out)
{
	// Ring buffer as in Draw_Primitive_UP (NO_OVERWRITE, DISCARD on overflow).
	if (bytes > UP_RING_BYTES || !M->up_ring) return false;
	unsigned offset = (M->up_ring_offset + 15u) & ~15u;
	GPU_MAP map = GPU_MAP_WRITE_NO_OVERWRITE;
	if (offset + bytes > UP_RING_BYTES) {
		offset = 0;
		map = GPU_MAP_WRITE_DISCARD;
		if (M->sh_count && M->sh_up_frame == M->sh_frame) {
			Sh_Preserve(M->up_ring, M->sh_up_lo, M->sh_up_hi, false);
			M->sh_up_frame = 0;
		}
	}
	GPU_MAPPED_SUBRESOURCE mapped;
	if (FAILED(M->context->Map(M->up_ring, 0, map, 0, &mapped))) return false;
	::memcpy(static_cast<unsigned char*>(mapped.pData) + offset, data, bytes);
	Ud_Upload("particles", M->up_ring, offset, data, bytes, false);
	M->context->Unmap(M->up_ring, 0);
	M->up_ring_offset = offset + bytes;
	number_of_API_calls++;
	*offset_out = offset;
	return true;
}

void RenderCore::Draw_Particles(const RenderParticleInstance* particles, unsigned count, const RenderParticleSetup& setup)
{
	if (!particles || !count) return;
	Ud_Draw_Args("particles", count, 0, 0);
	if (!Pa_Prepare(setup)) return;
	GpuContext* c = M->context;
	for (unsigned first = 0; first < count; first += PA_MAX_PER_DRAW) {
		const unsigned n = (count - first < PA_MAX_PER_DRAW) ? count - first : PA_MAX_PER_DRAW;
		unsigned offset = 0;
		if (!Pa_Write_Ring(particles + first, n * static_cast<unsigned>(sizeof(RenderParticleInstance)), &offset)) break;
		UINT s = static_cast<UINT>(sizeof(RenderParticleInstance)), o = offset;
		c->IASetVertexBuffers(0, 1, &M->up_ring, &s, &o);
		c->DrawInstanced(6, n, 0, 0);
		number_of_API_calls += 2;
		M->pa_stat_draws += 1.0;
		M->pa_stat_particles += (double)n;
	}
	// The engine's stream is bound again by the next Set_Vertex_Buffer (RenderDevice marks it changed).
	M->stream_vb = nullptr;
	M->pa_batch_count = 0;		// a batch may have been overwritten
}

bool RenderCore::Upload_Particle_Batch(const RenderParticleInstance* particles, unsigned count)
{
	M->pa_batch_count = 0;
	if (!particles || !count || count > PA_MAX_PER_DRAW || !M->context || !Ensure_Draw_Resources()) return false;
	unsigned offset = 0;
	if (!Pa_Write_Ring(particles, count * static_cast<unsigned>(sizeof(RenderParticleInstance)), &offset)) return false;
	M->pa_batch_offset = offset;
	M->pa_batch_count = count;
	return true;
}

void RenderCore::Draw_Particle_Batch(unsigned first, unsigned count, const RenderParticleSetup& setup)
{
	if (!count || first + count > M->pa_batch_count) return;
	Ud_Draw_Args("particles", count, first, 0);
	if (!Pa_Prepare(setup)) return;
	UINT s = static_cast<UINT>(sizeof(RenderParticleInstance)), o = M->pa_batch_offset;
	M->context->IASetVertexBuffers(0, 1, &M->up_ring, &s, &o);
	M->context->DrawInstanced(6, count, 0, first);
	number_of_API_calls += 2;
	M->pa_stat_draws += 1.0;
	M->pa_stat_particles += (double)count;
	M->stream_vb = nullptr;
}

void RenderCore::Set_Particle_Lighting(const float* scale)
{
	if (!scale) return;
	M->pa_light[0] = scale[0];
	M->pa_light[1] = scale[1];
	M->pa_light[2] = scale[2];
	M->pa_light_valid = true;
}

// ==================================================================================================
// 15. Internal render resolution
// ==================================================================================================
//
// The 3D scene of the view is drawn into a share of the scene target (RenderEffectSettings::render_scale) and scaled
// up before the 2D interface. Between Scene_3D_Begin and Scene_3D_Done every viewport the engine sets for the scene
// target is scaled (Set_Viewport); the small scene lies at the top left of the full-size target. So all readers of
// the scene copies (depth copy, colour copy, SSAO, soft particles, shadow mask, water) work unchanged: they read by
// pixel position or compute with the actual viewport (Get_Viewport). Other targets (reflection, shadow map, screen
// filter textures) keep their size. Scene_3D_Done runs the post-processing on the small rectangle, copies it and
// scales it up into the view rectangle (upscale.ps.hlsl). Without a scale other than 1 nothing here runs.
//
// A scale above 1 is supersampling: the scene target is created larger than the window (Create_Scene_Target), the
// scaled scene fills it, and Scene_3D_Done scales it down with an area filter into the window-sized rectangle at its
// top left, where the 2D interface follows and from where Present copies. While the scene is drawn the scene surface
// reports the larger size, so the scene copies of the water and the heat smudges follow it.

/// Constants of upscale.ps.hlsl (cbUpscale, b5).
struct RsUpscaleCB
{
	FFPFloat4 source;
	FFPFloat4 src_rect;
	FFPFloat4 dst_rect;
	FFPFloat4 params;
};
static_assert(sizeof(RsUpscaleCB) == 64, "cbUpscale in upscale.ps.hlsl");

static unsigned Rs_Round(float v)
{
	return (v > 0.0f) ? static_cast<unsigned>(v + 0.5f) : 0u;
}

bool RenderCore::Fx_Timing_Wanted()
{
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	return fx.flags != 0 || fx.render_scale < 0.999f || fx.render_scale > 1.001f || fx.gpu_timing != 0;
}

void RenderCore::Rs_Scale_Viewport(WW3DViewport& vp)
{
	const unsigned x0 = Rs_Round(static_cast<float>(vp.X) * M->rs_sx);
	const unsigned y0 = Rs_Round(static_cast<float>(vp.Y) * M->rs_sy);
	const unsigned x1 = Rs_Round(static_cast<float>(vp.X + vp.Width) * M->rs_sx);
	const unsigned y1 = Rs_Round(static_cast<float>(vp.Y + vp.Height) * M->rs_sy);
	vp.X = x0;
	vp.Y = y0;
	vp.Width  = (x1 > x0) ? x1 - x0 : 1;
	vp.Height = (y1 > y0) ? y1 - y0 : 1;
}

unsigned RenderCore::Rs_Sampler_Bias_Bits()
{
	if (!M->rs_active || M->current_render_target || M->rs_bias == 0.0f) return 0;
	unsigned bits;
	::memcpy(&bits, &M->rs_bias, sizeof(bits));
	return bits;
}

void RenderCore::Get_Render_Scale(float* sx, float* sy)
{
	const bool scaled = M->rs_active && !M->current_render_target;
	if (sx) *sx = scaled ? M->rs_sx : 1.0f;
	if (sy) *sy = scaled ? M->rs_sy : 1.0f;
}

void RenderCore::Scene_3D_Begin(int, int, int, int)
{
	Fx_Timestamp(Fx_Ts_Begin(FX_RANGE_SCENE));
	Taa_Scene_Begin();
	M->rs_active = false;
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	float s = fx.render_scale;
	const bool up = s > 1.001f;
	if (!(s < 0.999f || up) || M->rs_failed || !M->context || M->current_render_target || M->width == 0 || M->height == 0) return;
	if (s < 0.25f) s = 0.25f;
	// The same factor for every viewport of this scene, from the rounded size of the whole target.
	unsigned w = Rs_Round(static_cast<float>(M->width) * s);
	unsigned h = Rs_Round(static_cast<float>(M->height) * s);
	if (up) {
		// The enlarged target comes with the next Present after a change; until then the scene stays unscaled.
		if (w > M->scene_width)  w = M->scene_width;
		if (h > M->scene_height) h = M->scene_height;
		if (w <= M->width || h <= M->height) return;
	}
	M->rs_sx = static_cast<float>(w ? w : 1) / static_cast<float>(M->width);
	M->rs_sy = static_cast<float>(h ? h : 1) / static_cast<float>(M->height);
	// Texture detail as at full resolution: finer mips by the scale (log2 of the mean factor). Supersampling keeps
	// the mips of the larger image, the downsampling averages them.
	M->rs_bias = up ? 0.0f : ::log2f(0.5f * (M->rs_sx + M->rs_sy));
	M->rs_active = true;
	Rs_Report_Scene_Size(up);
	Set_Viewport(&M->rs_engine_viewport);
}

void RenderCore::Scene_3D_Done(int x, int y, int width, int height)
{
	Fx_Timestamp(Fx_Ts_End(FX_RANGE_SCENE));
	Taa_Scene_End();
	// HDR: the tone curve into 8 bits comes first, the post-processing then works on the 8-bit image.
	M->fx_glow_from_hdr = false;
	if (Hdr_Active()) {
		Fx_Hdr_Bloom_Down();
		Fx_Scene_To_8Bit_Target();
	}
	if (!M->rs_active) {
		Fx_Scene_3D_Done(x, y, width, height);
		M->fx_glow_from_hdr = false;
		Fx_Scene_To_8Bit_Target();
		return;
	}
	// View rectangle clipped to the target, and its scaled counterpart.
	const int x0 = (x > 0) ? x : 0;
	const int y0 = (y > 0) ? y : 0;
	const int x1 = (x + width  < (int)M->width)  ? x + width  : (int)M->width;
	const int y1 = (y + height < (int)M->height) ? y + height : (int)M->height;
	if (x1 <= x0 || y1 <= y0) {
		M->rs_active = false;
		Rs_Report_Scene_Size(false);
		Fx_Restore_Bindings();
		return;
	}
	WW3DViewport scaled;
	scaled.X = (unsigned)x0;  scaled.Y = (unsigned)y0;
	scaled.Width = (unsigned)(x1 - x0);  scaled.Height = (unsigned)(y1 - y0);
	scaled.MinZ = 0.0f;  scaled.MaxZ = 1.0f;
	Rs_Scale_Viewport(scaled);

	Fx_Scene_3D_Done((int)scaled.X, (int)scaled.Y, (int)scaled.Width, (int)scaled.Height);
	M->fx_glow_from_hdr = false;

	WW3DViewport full;
	full.X = (unsigned)x0;  full.Y = (unsigned)y0;
	full.Width = (unsigned)(x1 - x0);  full.Height = (unsigned)(y1 - y0);
	full.MinZ = 0.0f;  full.MaxZ = 1.0f;
	Fx_Timestamp(Fx_Ts_Begin(FX_RANGE_UPSCALE));
	if (scaled.Width > full.Width || scaled.Height > full.Height) {
		// Supersampling: the scaled scene reaches beyond the view rectangle into the window-sized area (under the
		// control bar, for instance). Scaling the whole scaled window down puts back what lies around the view there,
		// the frame's clear, as without supersampling.
		full.X = 0;  full.Y = 0;  full.Width = M->width;  full.Height = M->height;
		scaled = full;
		Rs_Scale_Viewport(scaled);
	}
	const bool ok = Rs_Upscale(scaled, full);
	Fx_Timestamp(Fx_Ts_End(FX_RANGE_UPSCALE));
	M->rs_active = false;
	Rs_Report_Scene_Size(false);
	Fx_Restore_Bindings();
	Fx_Scene_To_8Bit_Target();
	if (ok) {
		M->rs_stat_frames += 1.0;
		if (M->rs_stat_frames == 1.0) {
			WWDEBUG_SAY(("Render: internal render resolution %ux%u of %dx%d (scale %.3f, filter %u)\n", scaled.Width, scaled.Height,
				x1 - x0, y1 - y0, RenderDevice::Get_Effect_Settings().render_scale, RenderDevice::Get_Effect_Settings().render_scale_filter));
		}
	}
	else {
		// Without the upscale only the scaled scene would show; draw at full resolution from now on.
		M->rs_failed = true;
		WWDEBUG_SAY(("Render: upscale of the internal render resolution not possible, scene stays at full resolution\n"));
	}
}

// The scene surface the engine sees: the window size, while a supersampled scene is drawn the larger target.
void RenderCore::Rs_Report_Scene_Size(bool larger)
{
	if (!M->scene_surface) return;
	M->scene_surface->width  = larger ? M->scene_width  : M->width;
	M->scene_surface->height = larger ? M->scene_height : M->height;
}

bool RenderCore::Rs_Ensure_Resources()
{
	if (M->rs_cb) return true;
	if (!M->device) return false;
	M->rs_cb = Create_Constant_Buffer(M->device, sizeof(RsUpscaleCB));
	GPU_BLEND_DESC bd;
	::ZeroMemory(&bd, sizeof(bd));
	GPU_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
	rt.BlendEnable = FALSE;
	rt.SrcBlend = GPU_BLEND_ONE;       rt.DestBlend = GPU_BLEND_ZERO;       rt.BlendOp = GPU_BLEND_OP_ADD;
	rt.SrcBlendAlpha = GPU_BLEND_ONE;  rt.DestBlendAlpha = GPU_BLEND_ZERO;  rt.BlendOpAlpha = GPU_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = GPU_COLOR_WRITE_ENABLE_ALL;
	if (!M->rs_cb || FAILED(M->device->CreateBlendState(&bd, &M->rs_blend))) {
		Rs_Release_Resources();
		return false;
	}
	return true;
}

void RenderCore::Rs_Release_Resources()
{
	for (unsigned i = 0; i < RS_FILTERS; ++i) Gpu_Release(M->rs_ps[i]);
	Gpu_Release(M->rs_cb);
	Gpu_Release(M->rs_blend);
}

bool RenderCore::Rs_Upscale(const WW3DViewport& src, const WW3DViewport& dst)
{
	if (!Fx_Ensure_Resources() || !Fx_Ensure_Post_Target() || !Rs_Ensure_Resources()) return false;
	const RenderEffectSettings& fx = RenderDevice::Get_Effect_Settings();
	unsigned filter = (fx.render_scale_filter < RS_FILTER_DOWN) ? fx.render_scale_filter : RENDER_UPSCALE_BICUBIC;
	if (src.Width > dst.Width || src.Height > dst.Height) filter = RS_FILTER_DOWN;
	if (!M->rs_ps[filter]) {
		char value[2] = { (char)('0' + filter), 0 };
		GPU_SHADER_MACRO macros[2];
		macros[0].Name = "UPSCALE_FILTER";  macros[0].Definition = value;
		macros[1].Name = nullptr;           macros[1].Definition = nullptr;
		M->rs_ps[filter] = Fx_Create_Pixel_Shader(M->device, "upscale.ps.hlsl", macros);
		if (!M->rs_ps[filter]) return false;
	}

	// Source: a copy of the small rectangle (the pass writes the scene target), or the resolve with multisampling.
	GpuShaderResourceView* source = nullptr;
	if (M->sample_count > 1) {
		if (!M->resolve_target || !M->fx_resolve_srv) return false;
		M->context->ResolveSubresource(M->resolve_target, 0, M->scene_color, 0, M->scene_format);
		source = M->fx_resolve_srv;
	}
	else if (M->scene_format != GPU_FORMAT_B8G8R8A8_UNORM) {
		WW3DViewport region;
		region.X = src.X;  region.Y = src.Y;  region.Width = src.Width;  region.Height = src.Height;
		region.MinZ = 0.0f;  region.MaxZ = 1.0f;
		if (!Fx_Convert_Scene(M->fx_post_rtv, region)) return false;
		source = M->fx_post_srv;
	}
	else {
		GPU_BOX box;
		box.left = src.X;  box.top = src.Y;  box.front = 0;
		box.right = src.X + src.Width;  box.bottom = src.Y + src.Height;  box.back = 1;
		M->context->CopySubresourceRegion(M->fx_post_tex, 0, src.X, src.Y, 0, M->scene_color, 0, &box);
		source = M->fx_post_srv;
	}
	number_of_API_calls++;

	RsUpscaleCB cb;
	Fx_Set_Float4(cb.source, 1.0f / (float)M->scene_width, 1.0f / (float)M->scene_height, (float)M->scene_width, (float)M->scene_height);
	Fx_Set_Float4(cb.src_rect, (float)src.X, (float)src.Y, (float)(src.X + src.Width), (float)(src.Y + src.Height));
	Fx_Set_Float4(cb.dst_rect, (float)dst.X, (float)dst.Y, (float)(dst.X + dst.Width), (float)(dst.Y + dst.Height));
	Fx_Set_Float4(cb.params, fx.render_scale_sharpen, 0.0f, 0.0f, 0.0f);
	Upload_Constant_Buffer(M->context, M->rs_cb, &cb, sizeof(cb));
	M->context->PSSetConstantBuffers(RENDER_SLOT_CB_UPSCALE, 1, &M->rs_cb);
	number_of_API_calls += 2;
	Fx_Full_Screen_Pass(M->scene_rtv, dst, M->rs_ps[filter], source, nullptr, M->rs_blend);
	return true;
}
