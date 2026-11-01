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

// rendercore_internal.h -- what the rendercore*.cpp files share and nobody else needs.

#pragma once

#include "renderobjects.h"
#include "rendercore.h"
#include "renderstates.h"
#include "renderformat.h"
#include "rendercb.h"
#include "renderslots.h"
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

extern unsigned number_of_API_calls;

// ==================================================================================================
// Internals
// ==================================================================================================

// Formats of the scene depth target and its sampled copies: D24S8, or the 32-bit float family with
// RenderDevice::Set_Float_Depth.
inline GPU_FORMAT Scene_Depth_Format()		{ return RenderDevice::Get_Float_Depth() ? GPU_FORMAT_D32_FLOAT_S8X24_UINT : GPU_FORMAT_D24_UNORM_S8_UINT; }
inline GPU_FORMAT Scene_Depth_Typeless()	{ return RenderDevice::Get_Float_Depth() ? GPU_FORMAT_R32G8X24_TYPELESS : GPU_FORMAT_R24G8_TYPELESS; }
inline GPU_FORMAT Scene_Depth_Sampled()		{ return RenderDevice::Get_Float_Depth() ? GPU_FORMAT_R32_FLOAT_X8X24_TYPELESS : GPU_FORMAT_R24_UNORM_X8_TYPELESS; }

// ==================================================================================================
// Draw path: cache for state objects
// ==================================================================================================
//
// State objects are created from the shadow state and found again by their key. The key is the
// list of the state values involved; that keeps lookup cheap and allows comparison with memcmp.
// When a cache is full it is emptied once (D3D11 deduplicates the objects itself anyway).

enum { STATE_CACHE_CAPACITY = 128, STATE_KEY_WORDS = 12 };

struct RenderStateKey
{
	unsigned w[STATE_KEY_WORDS];
};

inline void Key_Clear(RenderStateKey& k) { ::ZeroMemory(&k, sizeof(k)); }

template <class T>
struct RenderStateCache
{
	RenderStateKey   keys[STATE_CACHE_CAPACITY];
	T*              objects[STATE_CACHE_CAPACITY];
	unsigned        count;
	// Hit counters. "front" are hits on the first comparison -- the normal case, because the
	// state almost always repeats while drawing.
	unsigned        hits;
	unsigned        front_hits;
	unsigned        misses;

	/// Finds the entry and moves it to the front. The state almost always repeats while drawing,
	/// so the first comparison hits and the list is never walked.
	T* Find(const RenderStateKey& k)
	{
		for (unsigned i = 0; i < count; ++i) {
			if (::memcmp(&keys[i], &k, sizeof(RenderStateKey)) != 0) continue;
			T* object = objects[i];
			++hits;
			if (i == 0) ++front_hits;
			else {
				keys[i]    = keys[0];    keys[0]    = k;
				objects[i] = objects[0]; objects[0] = object;
			}
			return object;
		}
		++misses;
		return nullptr;
	}

	void Insert(const RenderStateKey& k, T* object)
	{
		if (count >= STATE_CACHE_CAPACITY) Release_All();
		keys[count]    = k;
		objects[count] = object;
		++count;
		// move to the front so the next Find hits at once
		if (count > 1) {
			const RenderStateKey tk = keys[count - 1];
			T* to = objects[count - 1];
			keys[count - 1]    = keys[0];    keys[0]    = tk;
			objects[count - 1] = objects[0]; objects[0] = to;
		}
	}

	void Release_All()
	{
		for (unsigned i = 0; i < count; ++i) Gpu_Release(objects[i]);
		count = 0;
	}

	/// One summary line. Written when the device is released, not per frame.
	void Log_Statistics(const char* name) const
	{
		const unsigned total = hits + misses;
		if (!total) return;
		WWDEBUG_SAY(("D3D11: state cache %-14s %u entries, %u lookups, %u%% hits "
					 "(%u%% on the first compare), %u creations\n",
			name, count, total, (hits * 100) / total,
			hits ? (front_hits * 100) / hits : 0, misses));
	}
};

/// Permutation key of the uber vertex shader: bit 0 normal, bit 1 diffuse, bit 2 specular,
/// bits 3..5 number of texture coordinate sets (0..4), bit 6 INSTANCED (object matrices per instance).
enum { FFP_VS_INSTANCED = 64, FFP_VS_PIXEL_LIGHT = 128, FFP_VS_PERMUTATIONS = 256 };

/// Instanced meshes: instances per draw (four matrices, 256 bytes, each; the array in cbInstances of
/// ffp.vs.hlsl has the same size).
enum { MI_MAX_INSTANCES = 64 };

/// Pipeline statistics queries of the terrain tessellation (one per terrain section, read back frames later).
enum { TT_QUERIES = 32 };

/// Size of the ring buffer for Draw_Primitive_UP. The largest caller is the screen quad
/// with four vertices; 256 KiB is plenty for a frame.
enum { UP_RING_BYTES = 256 * 1024 };
// Constant buffer ring: about two frames of cbObject (1.5 KB per draw) for a few thousand draws.
enum { CB_RING_BYTES = 4 * 1024 * 1024 };


/// Post-processing permutations (bits, post.ps.hlsl), ambient occlusion passes and frames in flight
/// for GPU timing. Timestamps per frame: 0/1 opaque scene done (depth copy, SSAO), 2/3 particle
/// section, 4/5 post-processing, 6/7 draw shadow map, 8/9 apply shadows (inside 0/1), 10/11 depth
/// copy after the terrain, 12/13 terrain base pass (measured whenever any effect is on, with and
/// without detail).
enum { FX_POST_FXAA = 1, FX_POST_CAS = 2, FX_POST_GRADE = 4, FX_POST_HAZE = 8, FX_POST_HEAT = 16, FX_POST_LIGHTS = 32, FX_POST_DEBAND = 64, FX_POST_VARIANTS = 128 };
// Glow shaders: bloom passes 0..3 (bloom.ps.hlsl), heat mask (heat.ps.hlsl). Levels 0..3 are the bloom chain
// (1/4 .. 1/32), 4 and 5 the heat mask (1/4, 1/8).
enum { FX_GLOW_BLOOM_PREFILTER = 0, FX_GLOW_BLOOM_DOWN = 1, FX_GLOW_BLOOM_UP = 2, FX_GLOW_BLOOM_COMPOSITE = 3,
	   FX_GLOW_HEAT_MASK = 4, FX_GLOW_SHADERS = 5, FX_GLOW_LEVELS = 6, FX_GLOW_MASK_LEVEL = 4 };
// GPU time ranges, each with a begin and an end timestamp (2r, 2r + 1). The order is the order of the names in
// the GPUEFFEKTE line (Get_Effect_Statistics). FX_RANGE_FRAME runs from the previous Present to this one (idle time
// of the GPU included); the ranges from FX_RANGE_PASS_FIRST on are the parts of RenderGpuPass.
enum FxRange : unsigned
{
	FX_RANGE_DEPTH_SSAO, FX_RANGE_PARTICLES, FX_RANGE_POST, FX_RANGE_SHADOW_MAP, FX_RANGE_SHADOW_MASK,
	FX_RANGE_TERRAIN_DEPTH, FX_RANGE_TERRAIN, FX_RANGE_GLOW_HEAT, FX_RANGE_UPSCALE, FX_RANGE_SCENE, FX_RANGE_FRAME,
	FX_RANGE_PASS_FIRST, FX_RANGES = FX_RANGE_PASS_FIRST + 8
};
constexpr unsigned Fx_Ts_Begin(unsigned range) { return 2u * range; }
constexpr unsigned Fx_Ts_End(unsigned range) { return 2u * range + 1u; }
enum { FX_SSAO_PASSES = 3, FX_TIMER_FRAMES = 4, FX_TIMESTAMPS = 2 * FX_RANGES };
// Internal render resolution (section 15): upscale filters.
enum { RS_FILTERS = 4 };	///< RenderUpscaleFilter plus the downsampling filter (RS_FILTER_DOWN)
enum { RS_FILTER_DOWN = 3 };

/// A recorded shadow caster. The buffers hold no reference of their own -- Sh_Before_Write preserves
/// them before they are overwritten or released --, the layout and texture view do.
/// Shadow vertex shader: 0 position only, 1..4 with UV set 0..3 (alpha test), 5 trees (Trees.vs.hlsl), 6 GPU skins,
/// 7..11 = 0..4 instanced (matrix from vertex stream 1).
enum { SH_MAX_CASTERS = 8192, SH_MAX_SWAY = 16, SH_MAX_COPIES = 64, SH_VS_VARIANTS = 12, SH_VS_TREES = 5, SH_VS_SKIN = 6,
	   SH_VS_INSTANCED = 7,
	   SH_MAX_SKIN_ROWS = 3 * 64 * 512 };
struct RenderShadowCaster
{
	GpuBuffer*				vb;
	GpuBuffer*				ib;				///< nullptr: not indexed (Draw_Primitive_UP)
	GpuInputLayout*			layout;			///< with AddRef
	GpuShaderResourceView*	alpha_srv;		///< with AddRef; nullptr: no alpha test
	unsigned					vb_offset;
	unsigned					vb_stride;
	unsigned					topology;		///< GPU_PRIMITIVE_TOPOLOGY
	unsigned					count;			///< indices or vertices; 0 = dropped
	unsigned					start;
	int							base_vertex;
	unsigned					vs;				///< shadow vertex shader (variant)
	GpuInputLayout*			inst_layout;	///< with AddRef; layout of the instanced variant (nullptr: single draws)
	unsigned					sway;			///< trees: set in sh_sway
	unsigned					skin_first;		///< GPU skins: first row of the palette in sh_skin_rows
	unsigned					skin_count;		///< GPU skins: bones (0 = no skin)
	WW3DMatrixRaw				world_light;	///< world matrix times light matrix
	WW3DMatrixRaw				tex_matrix;		///< texture matrix of stage 0
	float						uv_matrix;
	float						uv_comps;
	float						alpha_ref;
	float						alpha_func;
};
/// Shadows: copy of a buffer range that would be overwritten before the shadow map is drawn.
struct RenderShadowCopy
{
	GpuBuffer*	buffer;
	unsigned		size;
	bool			index;
};

/// Include handler for common.hlsli when compiling the uber shaders.
class RenderShaderInclude : public GpuShaderInclude
{
public:
	HRESULT	Open(const char* file_name, const void** data, UINT* bytes) override;
	void	Close(const void* data) override;
};

struct RenderCore::Impl
{
	GpuOutput*				output;				///< adapters, swap chain, presentation
	GpuDevice*				device;
	GpuContext*				context;
	GpuTexture2D*			back_buffer;
	GpuRenderTargetView*	back_buffer_rtv;

	// Scene target (always used)
	GpuTexture2D*		scene_color;
	GpuRenderTargetView*	scene_rtv;
	GpuShaderResourceView* scene_srv;
	GpuTexture2D*		scene_depth;
	GpuDepthStencilView*	scene_dsv;
	GpuShaderResourceView* scene_depth_srv;	///< multisampled depth as a texture (only with MSAA, for the depth resolve)
	GpuTexture2D*		resolve_target;		///< non-multisampled copy for Present/screenshot
	GPU_FORMAT			scene_format;		///< format of scene_color and resolve_target
	GpuTexture2D*		scene_out;			///< BGRA8 copy of a float scene target (Fx_Scene_8Bit)
	GpuRenderTargetView*	scene_out_rtv;
	GpuShaderResourceView* scene_out_srv;		///< swapped with scene_srv together with the targets
	bool				hdr_target;			///< the scene target was created for RENDER_EFFECT_HDR
	bool				fx_glow_from_hdr;	///< this frame's bloom levels come from the float scene (Fx_Hdr_Bloom_Down)
	bool				scene_swapped;		///< after the 3D scene: scene_out is the scene target until Present
	GPU_FORMAT			scene_float_format;	///< format of the float scene target while swapped
	// Destination alpha of a scene target without alpha (R11G11B10): draws that write only alpha go here, draws
	// that blend with destination alpha read it, clears and 8-bit copies include it.
	GpuTexture2D*		scene_alpha;
	GpuRenderTargetView*	scene_alpha_rtv;
	GpuShaderResourceView* scene_alpha_srv;
	unsigned			sa_mode;			///< current draw: 0, 1 = reads destination alpha, 2 = writes only alpha
	bool				sa_bound;			///< scene_alpha bound for the current draw (Sa_Done undoes it)
	// Handles to the scene target so the game code can query, remember and restore it
	// (W3DShaderManager). They stay the same objects across a resolution change -- only their
	// content is updated.
	WW3DSurfaceObject*		scene_surface;
	WW3DSurfaceObject*		scene_depth_surface;
	WW3DSurfaceHandle		current_render_target;	///< NULL = scene target
	GpuRenderTargetView*	bound_rtv;			///< currently bound views (for Clear)
	GpuDepthStencilView*	bound_dsv;
	void*					cursor;				///< HICON of the Win32 cursor
	bool					cursor_visible;

	HWND					hwnd;
	unsigned				width;
	unsigned				height;
	unsigned				scene_width;		///< size of the scene target: the window size, larger with supersampling
	unsigned				scene_height;
	PresentLayout			layout;				///< swap chain buffer size and scene position in it
	int						output_width;		///< client size the layout was computed for
	int						output_height;
	unsigned				sample_count;
	int						swap_interval;
	bool					allow_tearing;
	bool					windowed;
	bool					device_removed;

	WW3DViewport			viewport;
	char					pending_capture[MAX_PATH];

	// Shadow state (evaluated at draw time)
	unsigned				render_states[256];
	unsigned				stage_states[8][33];
	unsigned				sampler_states[8][16];
	WW3DTextureHandle		textures[8];
	WW3DMaterial			material;
	WW3DLight				lights[8];
	bool					light_enabled[8];
	WW3DMatrixRaw			transforms[24];
	WW3DMatrixRaw			world_transform;

	WW3DVertexBufferHandle	stream_vb;
	unsigned				stream_offset;
	unsigned				stream_stride;
	WW3DIndexBufferHandle	indices;
	WW3DVertexShaderHandle	vertex_shader;
	WW3DPixelShaderHandle	pixel_shader;
	WW3DVertexDeclHandle	vertex_decl;
	float					vs_constants[256 * 4];
	float					ps_constants[224 * 4];

	// --- Draw path ----------------------------------------------------------------------------
	RenderStateCache<GpuBlendState>        blend_cache;
	RenderStateCache<GpuDepthStencilState> depth_cache;
	RenderStateCache<GpuRasterizerState>   raster_cache;
	RenderStateCache<GpuSamplerState>      sampler_cache;

	GpuBuffer*           cb_frame;
	GpuBuffer*           cb_object;
	GpuBuffer*           cb_piece_vs;
	GpuBuffer*           cb_piece_ps;
	FFPFrameCB              frame_cb;
	FFPObjectCB             object_cb;
	bool                    frame_cb_dirty;
	bool                    object_cb_dirty;
	bool                    piece_vs_dirty;
	bool                    piece_ps_dirty;

	// Constant buffer ring (Direct3D 11.1): cbObject, the piece registers and the shadow casters are written
	// one after another into one large buffer (MAP_WRITE_NO_OVERWRITE) and bound with an offset, instead of
	// mapping a small buffer with DISCARD for every draw. Null context: the ring is off.
	GpuContext*   cbr_context;
	GpuBuffer*           cbr_buffer;
	unsigned                cbr_offset;			///< next free byte
	unsigned                cbr_generation;		///< counts the wraps (DISCARD of the whole ring)
	UINT                    cbr_first[4];		///< slots 1..3: first constant of the current data
	UINT                    cbr_count[4];
	unsigned                cbr_bound_mask;		///< slots 1..3 (bits 1..3) bound with their current offset
	double                  cbr_stat_writes;	///< statistics: writes into the ring
	double                  cbr_stat_wraps;

	WW3DVertexShaderHandle  ffp_vs[FFP_VS_PERMUTATIONS];
	WW3DPixelShaderHandle   ffp_ps;
	char*                   ffp_vs_source;      ///< source of ffp.vs.hlsl (from the FileFactory)
	unsigned                ffp_vs_source_len;
	char*                   ffp_ps_source;
	unsigned                ffp_ps_source_len;
	bool                    ffp_load_failed;    ///< failed once: do not retry

	GpuTexture2D*        white_texture;      ///< 1x1 white for empty texture slots
	GpuShaderResourceView* white_srv;

	GpuBuffer*           up_ring;            ///< dynamic ring buffer for Draw_Primitive_UP
	unsigned                up_ring_offset;

	// Objects last bound to the context. The draw path sets only what changed; any code that
	// changes the context another way drops the affected part
	// (Invalidate_Bindings, Invalidate_Texture_Bindings).
	GpuInputLayout*          bound_layout;
	unsigned                    bound_topology;
	GpuVertexShader*         bound_vs;
	GpuPixelShader*          bound_ps;
	bool                        bound_cbuffers;
	GpuBlendState*           bound_blend;
	GpuDepthStencilState*    bound_depth;
	unsigned                    bound_stencil_ref;
	GpuRasterizerState*      bound_raster;
	// State objects for the current render states; looked up again only after a render state outside the object
	// constant buffer changed (Set_Render_State) or the bindings were invalidated.
	bool                        state_keys_dirty;
	GpuBlendState*           state_blend;
	GpuDepthStencilState*    state_depth;
	GpuRasterizerState*      state_raster;
	unsigned                    state_stencil_ref;
	GpuShaderResourceView*   bound_srv[8];
	GpuSamplerState*         bound_sampler[8];
	GpuSamplerState*         slot_sampler[8];		///< cache hit per stage for slot_sampler_key
	unsigned                    slot_sampler_key[8][10];
	bool                        slot_sampler_valid[8];

	// --- Graphics effects ---------------------------------------------------------------------------
	bool                        fx_soft;			///< draw flag from RenderStateStruct (billboard particles)
	bool                        fx_failed;			///< effect resources could not be created: effects stay off
	bool                        fx_logged;			///< selection logged once
	bool                        ffp_soft_failed;
	bool                        ffp_coverage_failed;
	bool                        ffp_light_failed[2];	///< per ffp_ps_light entry
	unsigned                    fx_frame;			///< counts Present
	unsigned                    fx_opaque_frame;	///< fx_frame + 1 of the last Scene_Opaque_Done (once per frame)
	unsigned                    fx_copy_requests;	///< RENDER_SCENE_COPY_* (Request_Scene_Copies)
	bool                        fx_depth_valid;		///< the depth copy belongs to the current frame
	bool                        fx_color_valid;
	GpuTexture2D*            fx_depth_tex;		///< typeless copy of scene_depth (Scene_Depth_Typeless); R32_FLOAT resolve with MSAA
	GpuRenderTargetView*     fx_depth_rtv;		///< only with MSAA (depth resolve pass)
	WW3DTextureObject*          fx_depth_handle;	///< handle (srv) for Get_Scene_Depth_Copy and t8
	GpuTexture2D*            fx_color_tex;
	GpuRenderTargetView*     fx_color_rtv;		///< only with a float scene target (conversion pass)
	WW3DTextureObject*          fx_color_handle;
	GpuTexture2D*            fx_post_tex;		///< intermediate post-processing target
	GpuRenderTargetView*     fx_post_rtv;
	GpuShaderResourceView*   fx_post_srv;
	GpuShaderResourceView*   fx_resolve_srv;		///< view of resolve_target (MSAA)
	GpuRenderTargetView*     fx_resolve_rtv;		///< resolve_target as the second intermediate target of the post chain (MSAA)
	GpuTexture2D*            fx_ao_tex[2];		///< half resolution, R8
	GpuRenderTargetView*     fx_ao_rtv[2];
	GpuShaderResourceView*   fx_ao_srv[2];
	GpuTexture2D*            fx_gtao_tex;		///< GTAO visibility at full resolution, R8
	GpuRenderTargetView*     fx_gtao_rtv;
	GpuShaderResourceView*   fx_gtao_srv;
	GpuPixelShader*          fx_gtao_ps[2];		///< gtao.ps.hlsl passes
	bool                        fx_gtao_failed;		///< a pass does not compile: GTAO stays off
	GpuTexture2D*            taa_tex[2];		///< TAA history, scene size and format, used in turn
	GpuRenderTargetView*     taa_rtv[2];
	GpuShaderResourceView*   taa_srv[2];
	GpuPixelShader*          taa_ps[2];			///< taa.ps.hlsl passes
	GpuBuffer*               taa_cb;
	GPU_FORMAT                  taa_format;
	unsigned                    taa_width, taa_height;
	unsigned                    taa_index;			///< history written this frame
	unsigned                    taa_frame;			///< fx_frame + 1 of the last resolve (once per frame)
	int                         taa_rect[4];		///< view rectangle of the history
	WW3DMatrixRaw               taa_prev_view_proj;	///< view times projection (without jitter) of the history
	WW3DMatrixRaw               taa_projection;		///< jittered projection of this frame
	bool                        taa_valid;			///< the history belongs to the previous frame
	bool                        taa_in_scene;		///< inside the 3D scene: draws get the jittered projection
	bool                        taa_jittered;		///< this frame's scene was drawn with jitter (FXAA stays off)
	bool                        taa_failed;
	GpuTexture2D*            fx_glow_tex[FX_GLOW_LEVELS];	///< R11G11B10, bloom levels and heat mask
	GpuRenderTargetView*     fx_glow_rtv[FX_GLOW_LEVELS];
	GpuShaderResourceView*   fx_glow_srv[FX_GLOW_LEVELS];
	unsigned                    fx_glow_w[FX_GLOW_LEVELS];
	unsigned                    fx_glow_h[FX_GLOW_LEVELS];
	GpuPixelShader*          fx_glow_ps[FX_GLOW_SHADERS];
	GpuVertexShader*         fx_vs;
	GpuPixelShader*          fx_post_ps[FX_POST_VARIANTS];
	GpuPixelShader*          fx_ssao_ps[FX_SSAO_PASSES];
	GpuPixelShader*          fx_gamma_ps;		///< post.ps.hlsl with DO_GAMMA
	GpuPixelShader*          fx_convert_ps;		///< sceneconvert.ps.hlsl: float scene target to 8 bits
	GpuPixelShader*          fx_depth_resolve_ps;	///< depthresolve.ps.hlsl: multisampled depth to R32_FLOAT
	bool                        fx_depth_resolve_failed;	///< the shader does not compile: depth effects stay off with MSAA
	GpuPixelShader*          fx_tonemap_ps;		///< its TONEMAP variant
	bool                        fx_gamma_active;	///< Set_Gamma: curve differs from the identity
	FFPFloat4                   fx_gamma_curve;	///< 1/gamma, brightness, contrast, lower limit
	WW3DPixelShaderHandle       ffp_ps_soft;		///< ffp.ps.hlsl with SOFT_PARTICLES
	WW3DPixelShaderHandle       ffp_ps_coverage;	///< ffp.ps.hlsl with ALPHA_COVERAGE
	WW3DPixelShaderHandle       ffp_ps_light[2];	///< ffp.ps.hlsl with PIXEL_LIGHT, [1] also with ALPHA_COVERAGE
	GpuBuffer*               fx_cb;
	GpuBuffer*               fx_light_cb;		///< cbDynLights (b8), created with the first lights pass
	RenderDynamicLight          dl_lights[RENDER_MAX_DYNAMIC_LIGHT_CANDIDATES];	///< Set_Dynamic_Lights, world space
	unsigned                    dl_count;
	WW3DMatrixRaw               fx_view;			///< view matrix at the depth copy (lights go to view space)
	WW3DMatrixRaw               fx_proj;			///< projection at the depth copy (scene camera, without TAA jitter)
	FFPEffectsCB                fx_cb_data;
	bool                        fx_cb_dirty;		///< fx_cb_data changed, not uploaded yet
	GpuSamplerState*         fx_linear;
	GpuBlendState*           fx_blend_copy;		///< no blending, RGB only
	GpuBlendState*           fx_blend_all;		///< no blending, all channels (scene conversion)
	GpuBlendState*           fx_blend_mul;		///< dest * source, RGB only
	GpuBlendState*           fx_blend_add;		///< dest + source, RGB only
	GpuBlendState*           fx_blend_premul;	///< source + dest * (1 - source alpha), RGB only
	GpuDepthStencilState*    fx_depth_off;
	GpuRasterizerState*      fx_raster;
	// GPU timing: timestamps per frame, FX_TIMER_FRAMES frames in flight, read back without waiting.
	GpuQuery*                fx_disjoint[FX_TIMER_FRAMES];
	GpuQuery*                fx_ts[FX_TIMER_FRAMES][FX_TIMESTAMPS];
	unsigned long long          fx_ts_used[FX_TIMER_FRAMES];
	bool                        fx_disjoint_open;
	bool                        fx_soft_open;		///< this frame's particle section is running
	double                      fx_sum_ms[FX_RANGES];
	unsigned                    fx_count[FX_RANGES];
	unsigned                    fx_frames_measured;

	// --- Shadows (section 9) -------------------------------------------------------------------------
	bool                        sh_recording;		///< draws of this scene pass are recorded as casters
	bool                        sh_caster_only;		///< indexed draws are only recorded, not drawn
	bool                        sh_failed;			///< resources could not be created: classic shadows stay
	bool                        sh_full_logged;
	bool                        sh_copies_logged;
	unsigned                    sh_frame;			///< id of the current recording (buffer stamp), never 0
	unsigned                    sh_size;			///< edge length of the shadow map
	unsigned                    sh_vs_failed;		///< bit mask of variants that failed to compile
	WW3DMatrixRaw               sh_view;			///< camera of the scene pass
	WW3DMatrixRaw               sh_proj;
	WW3DMatrixRaw               sh_light;			///< world -> light clip
	float                       sh_sun[3];			///< direction to the sun (world)
	float                       sh_shadow_color[3];	///< factor in full shadow
	float                       sh_texel_world;		///< edge length of a texel (world)
	float                       sh_depth_range;		///< light depth range (world)
	RenderShadowCaster*          sh_casters;			///< recording, SH_MAX_CASTERS entries
	unsigned                    sh_count;
	FFPFloat4*                  sh_sway;			///< sway vectors of the tree draws
	unsigned                    sh_sway_count;
	FFPFloat4*                  sh_skin_rows;		///< bone palettes of the recorded GPU skins
	unsigned                    sh_skin_row_count;
	GpuBuffer*               sh_cb_skin;			///< cbSkin while drawing the shadow map
	unsigned                    sh_up_frame;		///< stamp and range of the Draw_Primitive_UP ring buffer
	unsigned                    sh_up_lo;
	unsigned                    sh_up_hi;
	RenderShadowCopy             sh_copies[SH_MAX_COPIES];
	unsigned                    sh_copy_used;		///< of these, taken in this frame
	GpuTexture2D*            sh_tex;				///< R32_TYPELESS
	GpuDepthStencilView*     sh_dsv;
	GpuShaderResourceView*   sh_srv;
	// Near cascade: a second map of the same size over the near part of the shadow range. Same light axes and depth
	// mapping as sh_tex; light clip of the near map = light clip of sh_tex times (scale, scale) plus offset (sh_near_adjust).
	GpuTexture2D*            sh_near_tex;
	GpuDepthStencilView*     sh_near_dsv;
	GpuShaderResourceView*   sh_near_srv;
	bool                        sh_near_failed;
	bool                        sh_cascade;			///< the near cascade is drawn in this scene pass
	bool                        sh_adjust_active;	///< Sh_Draw_Casters draws into the near map (world_light times the adjustment)
	float                       sh_near_adjust[3];	///< scale, x and y offset in light clip space
	// Terrain casters in their own map, depth after the terrain. While sh_ter_valid holds,
	// sh_tex contains only the other casters.
	GpuTexture2D*            sh_ter_tex;			///< shadow map of the terrain casters only (R32_TYPELESS)
	GpuDepthStencilView*     sh_ter_dsv;
	GpuShaderResourceView*   sh_ter_srv;
	GpuTexture2D*            sh_ter_depth_tex;	///< depth copy after the terrain (as fx_depth_tex, scene target size)
	GpuRenderTargetView*     sh_ter_depth_rtv;	///< only with MSAA (depth resolve pass)
	GpuShaderResourceView*   sh_ter_depth_srv;
	unsigned                    sh_ter_split;		///< casters [0, sh_ter_split) are terrain
	bool                        sh_ter_valid;		///< Scene_Terrain_Done ran in this scene pass
	bool                        sh_ter_failed;		///< resources could not be created: full terrain shadows
	WW3DVertexShaderHandle      sh_vs[SH_VS_VARIANTS];
	GpuPixelShader*          sh_alpha_ps;		///< shadow.ps.hlsl
	GpuPixelShader*          sh_mask_ps;			///< shadowmask.ps.hlsl
	GpuPixelShader*          sh_atmo_ps;			///< shadowmask.ps.hlsl with ATMOSPHERE (nullptr: atmosphere off)
	FFPShadowApplyCB         sh_apply_data;		///< constants of the last Sh_Apply
	bool                     sh_apply_valid;		///< Sh_Apply ran in this scene pass (Sh_Atmosphere may follow)
	GpuBuffer*               sh_cb_draw;
	GpuBuffer*               sh_cb_apply;
	FFPShadowDrawCB             sh_draw_data;		///< last uploaded
	GpuRasterizerState*      sh_raster;
	GpuDepthStencilState*    sh_depth;
	GpuSamplerState*         sh_cmp;
	GpuSamplerState*         sh_alpha_sampler;
	GpuBuffer*               sh_inst;			///< world-light matrices of all casters of the frame (vertex stream 1)
	bool                        sh_instanced;		///< matrices uploaded for this shadow map
	double                      sh_stat_draws;		///< statistics: shadow map draws after merging
	double                      sh_stat_casters;	///< sums for the statistics line
	double                      sh_stat_copies;
	double                      sh_stat_extent;
	unsigned                    sh_stat_frames;

	// --- Terrain detail (section 10) -----------------------------------------------------------------------
	bool                        td_mode;			///< flag: the following draws are terrain (Set_Terrain_Detail)
	bool                        td_failed;			///< textures or buffer could not be created: detail stays off
	bool                        td_table_valid;		///< a table for the atlas of the loaded map is present
	bool                        td_cb_dirty;
	bool                        td_logged;
	bool                        td_ts_open;			///< terrain timing section is running
	unsigned                    td_ts_frame;		///< fx_frame + 1 of the last measured section (once per frame)
	GpuTexture2D*            td_tex[2];			///< detail_a.dds, detail_b.dds
	GpuShaderResourceView*   td_srv[2];
	GpuSamplerState*         td_sampler;
	unsigned                    td_sampler_aniso;	///< anisotropy td_sampler was created with
	float                       td_sampler_bias;	///< mip LOD bias td_sampler was created with
	GpuBuffer*               td_cb;
	FFPTerrainDetailCB          td_cb_data;
	double                      td_stat_draws;		///< statistics: detail draws and terrain draws in total
	double                      td_stat_terrain;
	unsigned                    td_stat_cells;		///< atlas cells with detail in the table

	// --- Rock layer (section 11) -----------------------------------------------------------------------------
	bool                        tr_failed;			///< rock textures or buffer could not be created: rock layer stays off
	bool                        tr_cb_dirty;
	float                       tr_snow_color[3];	///< mean class color of the snow ground of the map (Set_Terrain_Detail_Table)
	unsigned                    tr_snow_cells;		///< atlas cells with snow ground; 0: the map has none
	bool                        tr_height_dirty;	///< height field changed, recreate the texture
	bool                        tr_logged;
	unsigned char*              tr_heights;			///< copy of the height field (compared per frame)
	int                         tr_width, tr_height, tr_border;
	float                       tr_cell, tr_height_scale;
	float                       tr_min_height;		///< lowest grid point (world), base of the height fog
	GpuTexture2D*            tr_tex[3];			///< fels_grob.dds, fels_fein.dds, height field (R16_UNORM)
	GpuShaderResourceView*   tr_srv[3];			///< t12, t13, t11
	GpuSamplerState*         tr_sampler;			///< WRAP, anisotropy like the terrain atlas
	unsigned                    tr_sampler_aniso;
	float                       tr_sampler_bias;
	GpuSamplerState*         tr_height_sampler;	///< CLAMP, linear
	GpuBuffer*               tr_cb;
	FFPTerrainRockCB            tr_cb_data;
	double                      tr_stat_draws;		///< statistics: rock draws

	// --- Terrain relief (section 11b) --------------------------------------------------------------------------
	bool                        tn_failed;			///< constant buffer could not be created: relief stays off
	bool                        tn_cb_dirty;
	bool                        tn_logged;
	WW3DTextureHandle           tn_atlas_handle;	///< relief atlas of the loaded map (WorldHeightMap), only compared
	GpuShaderResourceView*   tn_atlas;			///< its view, own reference (t14)
	GpuBuffer*               tn_cb;
	FFPTerrainNormalCB          tn_cb_data;
	double                      tn_stat_draws;		///< statistics: relief draws

	// --- Terrain light per pixel (rendercore_boden.cpp) --------------------------------------------------------
	bool                        tb_logged;
	double                      tb_stat_draws;		///< statistics: terrain draws lit per pixel
	double                      ml_stat_draws;		///< statistics: model draws lit per pixel (PIXEL_LIGHT)
	double                      ml_stat_normal;		///< statistics: of those with a normal map
	double                      at_stat_frames;		///< statistics: frames with the atmosphere pass
	GpuShaderResourceView*      ml_normal_srv;		///< normal map of ml_normal_owner (own reference)
	WW3DTextureHandle           ml_normal_owner;	///< stage 0 texture it belongs to (compared only)
	bool                        ml_normal_active;	///< the object constants of the current draw say "normal map" (teamColor.w)

	// --- Terrain tessellation (section 11c) --------------------------------------------------------------------
	bool                        tt_mode;			///< flag: the following draws are terrain geometry (Set_Terrain_Tessellation)
	bool                        tt_failed;			///< shaders or buffer could not be created: tessellation stays off
	bool                        tt_logged;
	bool                        tt_bound;			///< hull and domain shader are bound
	bool                        tt_cb_dirty;
	bool                        tt_drawn;			///< a patch draw in this frame
	GpuHullShader*           tt_hs;
	GpuDomainShader*         tt_ds;
	WW3DPixelShaderHandle       tt_probe_ps;		///< crack probe: plain green
	WW3DVertexShaderHandle      tt_vs[FFP_VS_PERMUTATIONS];	///< ffp.vs.hlsl as vs_5_0
	unsigned long long          tt_vs_failed;		///< bit per permutation that did not compile
	GpuBuffer*               tt_cb;
	FFPTerrainTessCB            tt_cb_data;
	GpuSamplerState*         tt_sampler;			///< linear, clamp
	GpuSamplerState*         tt_rock_sampler;	///< linear, wrap (rock textures in the domain stage)
	bool                        tt_heights;			///< the height atlas has ground heights (else only rock displacement)
	float                       tt_rock_amp;		///< rock amplitude of the last patch draw (world units per height unit)
	WW3DTextureHandle           tt_atlas_handle;	///< height atlas of the loaded map, only compared
	GpuShaderResourceView*   tt_atlas;			///< its view, own reference (t3 of the domain stage)
	unsigned char*              tt_weights_cpu;		///< copy of the weights (4 bytes per grid point)
	unsigned char*              tt_mask_cpu;		///< copy of the mask (1 byte per grid point)
	int                         tt_grid_w, tt_grid_h;
	GpuTexture2D*            tt_weights_tex;
	GpuShaderResourceView*   tt_weights;			///< t1 of the domain stage
	GpuTexture2D*            tt_mask_tex;
	GpuShaderResourceView*   tt_mask;			///< t2 of the domain stage
	GpuQuery*                tt_query[TT_QUERIES];
	unsigned char               tt_query_state[TT_QUERIES];	///< 0 free, 1 running, 2 waiting for the result
	unsigned                    tt_query_open;		///< running query + 1, 0 = none
	double                      tt_stat_draws;		///< statistics: patch draws, terrain sections
	double                      tt_stat_sections;
	double                      tt_stat_measured;	///< sections with a result
	double                      tt_stat_patches;	///< sums over the measured sections
	double                      tt_stat_points;
	double                      tt_stat_triangles;

	// --- Dynamic terrain lights (section 12) -------------------------------------------------------------------
	bool                        tl_mode;			///< flag: the following draws are terrain tiles with dynamic lights
	bool                        tl_cb_dirty;
	GpuBuffer*               tl_cb;				///< lights of this frame (b8 of the vertex stage)
	GpuBuffer*               tl_cb_off;			///< zeros: no dynamic lights, bound for every other draw
	FFPTerrainLightsCB          tl_cb_data;
	GpuBuffer*               bound_tl_cb;		///< currently bound to b8 of the vertex stage
	GpuShaderResourceView*   bound_tl_srv;		///< currently bound to t11 of the vertex stage
	double                      tl_stat_draws;		///< statistics: terrain draws lit by the vertex shader
	unsigned                    tl_stat_max;		///< statistics: most lights in one frame
	double                      tl_stat_static;		///< statistics: terrain renders with static prelighting in the shader

	// --- GPU skinning (section 13) ----------------------------------------------------------------------------
	GpuBuffer*               sk_cb;				///< palette of the current skin (b9 of the vertex stage)
	GpuBuffer*               sk_cb_off;			///< zeros: no skinning, bound for every other draw
	FFPSkinCB                   sk_cb_data;
	unsigned                    sk_count;			///< bones of the current palette, 0 = off
	bool                        sk_dirty;			///< palette changed since the last upload
	GpuBuffer*               bound_sk_cb;		///< currently bound to b9 of the vertex stage
	double                      sk_stat_draws;		///< statistics: skinned draws
	double                      sk_stat_bones;		///< statistics: palette rows uploaded

	// --- Particles on the GPU (section 14) ---------------------------------------------------------------------
	bool                        pa_mode;			///< Prepare_Draw: particle vertex shader and instance layout
	bool                        pa_terrain;			///< Prepare_Draw: bind cbTerrainLights and the height field for particles
	float                       pa_light[3];		///< light of the time of day relative to the afternoon
	bool                        pa_light_valid;
	bool                        pa_failed;
	bool                        pa_table_dirty;
	WW3DVertexShaderHandle      pa_vs;
	GpuInputLayout*          pa_layout;
	GpuBuffer*               pa_cb;
	GpuBuffer*               pa_table_cb;
	FFPParticleCB               pa_cb_data;
	FFPParticleTableCB          pa_table;			///< copy of the tables (uploaded again after a device change)
	bool                        pa_table_valid;
	unsigned                    pa_batch_offset;	///< ring offset of the last Upload_Particle_Batch
	unsigned                    pa_batch_count;
	double                      pa_stat_draws;		///< statistics: instanced particle draws
	double                      pa_stat_particles;

	// Instanced meshes (Begin_Mesh_Instances/End_Mesh_Instances): consecutive indexed draws of the uber
	// shader that differ only in the object matrices become one instanced draw.
	bool                        mi_open;			///< draws are collected
	bool                        mi_preset;			///< object_cb holds the batch's constants; uploaded as they are
	bool                        mi_instanced;		///< Prepare_Draw picks the INSTANCED permutation
	unsigned                    mi_count;			///< collected draws
	unsigned                    mi_type;
	unsigned                    mi_index_count;
	unsigned                    mi_start_index;
	unsigned                    mi_min_index;
	unsigned                    mi_num_vertices;
	int                         mi_base_vertex;
	FFPObjectCB                 mi_cb;				///< constants of the first collected draw
	WW3DMatrixRaw               mi_matrices[MI_MAX_INSTANCES * 4];
	GpuBuffer*               mi_matrix_cb;		///< instance matrices (b12 of the vertex stage)
	GpuBuffer*               mi_index_vb;		///< instance numbers 0..MI_MAX_INSTANCES-1 (vertex stream 1)
	unsigned                    mi_ring_first;		///< the matrices in the constant buffer ring (count 0: in mi_matrix_cb)
	unsigned                    mi_ring_count;
	bool                        mi_failed;
	double                      mi_stat_draws;		///< statistics: instanced mesh draws
	double                      mi_stat_instances;	///< and the draws they replace

	// --- Internal render resolution (section 15) -----------------------------------------------------------------
	bool                        rs_active;			///< between Scene_3D_Begin and Scene_3D_Done with a scale other than 1
	bool                        rs_failed;			///< upscale not possible: the scene stays at full resolution
	float                       rs_sx;				///< target pixels per engine pixel while active
	float                       rs_sy;
	float                       rs_bias;			///< mip LOD bias of the engine samplers while active
	unsigned                    rs_bound_bias;		///< bias (bits) the cached slot samplers were built with
	WW3DViewport                rs_engine_viewport;	///< last viewport set by the engine, unscaled
	GpuPixelShader*          rs_ps[RS_FILTERS];
	GpuBuffer*               rs_cb;
	GpuBlendState*           rs_blend;			///< no blending, writes colour and alpha
	double                      rs_stat_frames;		///< statistics: frames drawn scaled
};

template <class ImplT> inline void Invalidate_Texture_Bindings(ImplT* M)
{
	for (unsigned i = 0; i < 8; ++i) M->bound_srv[i] = (GpuShaderResourceView*)1;
}

template <class ImplT> inline void Invalidate_Bindings(ImplT* M)
{
	M->bound_layout = (GpuInputLayout*)1;
	M->bound_topology = 0xFFFFFFFF;
	M->bound_vs = (GpuVertexShader*)1;
	M->bound_ps = (GpuPixelShader*)1;
	M->bound_cbuffers = false;
	M->state_keys_dirty = true;
	M->bound_tl_cb = (GpuBuffer*)1;
	M->bound_tl_srv = (GpuShaderResourceView*)1;
	M->bound_sk_cb = (GpuBuffer*)1;
	M->bound_blend = (GpuBlendState*)1;
	M->bound_depth = (GpuDepthStencilState*)1;
	M->bound_raster = (GpuRasterizerState*)1;
	for (unsigned i = 0; i < 8; ++i) {
		M->bound_sampler[i] = (GpuSamplerState*)1;
		M->slot_sampler_valid[i] = false;
	}
	Invalidate_Texture_Bindings(M);
}

// --- Helpers used by several parts ----------------------------------------------------------------

inline void Mat_Multiply(const WW3DMatrixRaw& a, const WW3DMatrixRaw& b, WW3DMatrixRaw& out)
{
	WW3DMatrixRaw r;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j]
					  + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
		}
	}
	out = r;
}

/// Component count (1..4) of texture coordinate set `index` in the declaration, 0 = missing.
inline unsigned Decl_Texcoord_Size(WW3DVertexDeclHandle decl, unsigned index)
{
	if (!decl) return 0;
	for (unsigned i = 0; i < decl->element_count; ++i) {
		const WW3DVertexElement& e = decl->elements[i];
		if (e.Usage == WW3D_DECLUSAGE_TEXCOORD && e.UsageIndex == index) {
			if (e.Type <= WW3D_DECLTYPE_FLOAT4) return static_cast<unsigned>(e.Type) + 1;
			return 4;
		}
	}
	return 0;
}

inline GpuBuffer* Create_Constant_Buffer(GpuDevice* device, unsigned bytes)
{
	GPU_BUFFER_DESC desc;
	::ZeroMemory(&desc, sizeof(desc));
	desc.ByteWidth      = (bytes + 15) & ~15u;
	desc.Usage          = GPU_USAGE_DYNAMIC;
	desc.BindFlags      = GPU_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = GPU_CPU_ACCESS_WRITE;
	GpuBuffer* buffer = nullptr;
	if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer))) return nullptr;
	return buffer;
}

// --- Upload diagnosis (rendercore_diagnosis.cpp, RenderDevice::Set_Upload_Diagnosis) --------------------------

bool Ud_Tracking();
/// Byte that presets memory handed out unwritten (new buffers and textures, discarded mappings), -1 = off.
int  Ud_Fill();
/// A new object may reuse the address of a released one; its content starts empty.
void Ud_Created(const void* resource);
/// Hashes an upload into the content of `resource`; `replace` for a write that discards the old content.
void Ud_Upload(const char* kind, const void* resource, unsigned offset, const void* data, unsigned bytes, bool replace);
void Ud_Upload_Rows(const char* kind, const void* resource, unsigned offset, const void* data, unsigned row_bytes,
					unsigned rows, unsigned pitch, bool replace);
/// GPU-side copy: the content of `dest` takes up that of `source` (nullptr: written by the GPU, not known).
void Ud_Copy(const char* kind, const void* dest, unsigned offset, const void* source);
/// Texture levels locked for writing are hashed on unlock, while the memory is still mapped.
void Ud_Texture_Locked(const void* texture, unsigned level, const void* data, unsigned pitch);
void Ud_Texture_Unlocked(const char* kind, const void* texture, unsigned level, unsigned row_bytes, unsigned rows);
/// Kind and counts of the next draw; Prepare_Draw writes its line.
void Ud_Draw_Args(const char* kind, unsigned count, unsigned start, int base);

/// Rewrites a dynamic buffer completely.
inline void Upload_Constant_Buffer(GpuContext* context, GpuBuffer* buffer,
								   const void* data, unsigned bytes)
{
	GPU_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(context->Map(buffer, 0, GPU_MAP_WRITE_DISCARD, 0, &mapped))) {
		if (Ud_Fill() >= 0) {
			GPU_BUFFER_DESC desc;
			buffer->GetDesc(&desc);
			::memset(mapped.pData, Ud_Fill(), desc.ByteWidth);
		}
		Ud_Upload("cb", buffer, 0, data, bytes, true);
		::memcpy(mapped.pData, data, bytes);
		context->Unmap(buffer, 0);
		number_of_API_calls++;
	}
}

inline unsigned Cbr_Size(unsigned bytes, unsigned window_bytes)
{
	return (((bytes > window_bytes) ? bytes : window_bytes) + 255u) & ~255u;
}

inline void Fx_Set_Float4(FFPFloat4& d, float x, float y, float z, float w)
{
	d.x = x; d.y = y; d.z = z; d.w = w;
}

/// Loads a file through the FileFactory and returns it null-terminated (caller frees).
char* Load_Shader_Source(const char* name, unsigned* length);
/// The device that compiles shaders (set while a device exists).
void Set_Shader_Compile_Device(GpuDevice* device);
/// Compiles an HLSL shader through the on-disk shader cache.
HRESULT Compile_FFP_Shader(const char* source, unsigned length, const char* debug_name,
						   const GPU_SHADER_MACRO* macros, const char* profile, GpuBlob** code);
/// Compiles a pixel shader from the shader folder; nullptr on failure.
GpuPixelShader* Fx_Create_Pixel_Shader(GpuDevice* device, const char* file, const GPU_SHADER_MACRO* macros,
									   const char* profile = "ps_4_0");
/// Largest displacement of the tessellated terrain (world units): ground heights or rock slopes.
float Tt_Max_Displacement(const RenderEffectSettings& fx, float rock_amplitude);
