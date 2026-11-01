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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/renderdevice.h                           $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 08/05/02 2:40p                                              $*
 *                                                                                             *
 *                    $Revision:: 92                                                          $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Render to shadow buffer texture support														*
 * 08/05/02 KM Texture class redesign 
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef RENDERDEVICE_H
#define RENDERDEVICE_H

#include "always.h"
#include "dllist.h"
#include "ww3dtypes.h"
#include "renderbackend.h"	// device-facing interface


#include "matrix4.h"
#include "statistics.h"
#include "wwstring.h"
#include "lightenvironment.h"
#include "shader.h"
#include "vector4.h"
#include "rendercaps.h"
#include "formconv.h"

#include "texture.h"
#include "vertexbuffer.h"
#include "indexbuffer.h"
#include "vertmaterial.h"

/*
** Registry value names
*/
#define	VALUE_NAME_RENDER_DEVICE_NAME					"RenderDeviceName"
#define	VALUE_NAME_RENDER_DEVICE_WIDTH				"RenderDeviceWidth"
#define	VALUE_NAME_RENDER_DEVICE_HEIGHT				"RenderDeviceHeight"
#define	VALUE_NAME_RENDER_DEVICE_DEPTH				"RenderDeviceDepth"
#define	VALUE_NAME_RENDER_DEVICE_WINDOWED			"RenderDeviceWindowed"
#define	VALUE_NAME_RENDER_DEVICE_TEXTURE_DEPTH		"RenderDeviceTextureDepth"

// Texture stages the engine tracks (UV sets in shdsubmesh.h, filter tables in texturefilter.cpp,
// render_state.Textures). Must match MeshMatDescClass::MAX_TEX_STAGES.
const unsigned MAX_TEXTURE_STAGES=8;
// Texture slots mirrored by the wrapper's shadow state; kept separate from the stage count so the
// backend finds all slots in one place.
const unsigned MAX_TEXTURE_SLOTS=8;
const unsigned MAX_VERTEX_STREAMS=2;
const unsigned MAX_VERTEX_SHADER_CONSTANTS=96;
// Matches the backend's pixel shader constant buffer (FFP_PIECE_PS_REGISTERS). Water version 2 uses registers
// up to 27 (RenderWater2PS), so a smaller array would overflow in Set_Pixel_Shader_Constant.
const unsigned MAX_PIXEL_SHADER_CONSTANTS=32;
const unsigned MAX_SHADOW_MAPS=1;

#define prevVer
#define nextVer
#undef __volatile	// also an SDK SAL macro (specstrings_strict.h); avoids C4005
#define __volatile unsigned


/// Parts of the frame with their own GPU time (RenderDevice::Gpu_Pass_Begin/End).
enum RenderGpuPass
{
	RENDER_GPU_PASS_MIRROR = 0,		///< water mirror (its own scene)
	RENDER_GPU_PASS_OBJECTS,			///< opaque objects after the terrain
	RENDER_GPU_PASS_TREES,
	RENDER_GPU_PASS_WATER,
	RENDER_GPU_PASS_TRANSLUCENT,		///< translucent objects and particles
	RENDER_GPU_PASS_UI,				///< health bars, 2D interface, mouse
	RENDER_GPU_PASS_PROJECTED_SHADOWS,	///< all projected shadow textures of the frame
	RENDER_GPU_PASS_SCREEN_FILTER,	///< the 3D scene while a screen filter draws it into its own surface
	RENDER_GPU_PASS_COUNT,
	RENDER_GPU_PASS_UNTIMED = RENDER_GPU_PASS_COUNT	///< target pass without a time range of its own
};

enum {
	BUFFER_TYPE_DEVICE,
	BUFFER_TYPE_SORTING,
	BUFFER_TYPE_DYNAMIC_DEVICE,
	BUFFER_TYPE_DYNAMIC_SORTING,
	BUFFER_TYPE_INVALID
};

class VertexMaterialClass;
class CameraClass;
class LightEnvironmentClass;
class RenderDeviceDescClass;
struct GpuLayerInfo;
class VertexBufferClass;
class DynamicVBAccessClass;
class IndexBufferClass;
class DynamicIBAccessClass;
class TextureClass;
class ZTextureClass;
class LightClass;
class SurfaceClass;
class RenderCaps;

#define WW3D_RECORD_MATRIX_CHANGE()				matrix_changes++
#define WW3D_RECORD_MATERIAL_CHANGE()			material_changes++
#define WW3D_RECORD_VERTEX_BUFFER_CHANGE()		vertex_buffer_changes++
#define WW3D_RECORD_INDEX_BUFFER_CHANGE()		index_buffer_changes++
#define WW3D_RECORD_LIGHT_CHANGE()				light_changes++
#define WW3D_RECORD_TEXTURE_CHANGE()				texture_changes++
#define WW3D_RECORD_RENDER_STATE_CHANGE()		render_state_changes++
#define WW3D_RECORD_TEXTURE_STAGE_STATE_CHANGE() texture_stage_state_changes++
#define WW3D_RECORD_DRAW_CALLS()					draw_calls++

extern unsigned number_of_API_calls;
extern bool _DeviceSingleThreaded;

void Device_Assert();
void Log_Device_ErrorCode(unsigned res);

WWINLINE void Device_ErrorCode(unsigned res)
{
	if (res==WW3D_OK) return;
	Log_Device_ErrorCode(res);
}

#define RENDER_THREAD_ASSERT() ;


#define no_EXTENDED_STATS
// EXTENDED_STATS collects additional timing statistics by turning off parts
// of the 3D drawing system (terrain, objects, etc.)
#ifdef EXTENDED_STATS
class RenderExtendedStats
{
public:
	bool m_showingStats;
	bool m_disableTerrain;
	bool m_disableWater;
	bool m_disableObjects;
	bool m_disableOverhead;
	bool m_disableConsole;
	int  m_debugLinesToShow;
	int	 m_sleepTime;
public:
	RenderExtendedStats::RenderExtendedStats(void) {
		m_disableConsole = m_showingStats = m_disableTerrain = m_disableWater = m_disableOverhead = m_disableObjects = false;
		m_sleepTime = 0;
		m_debugLinesToShow = -1; // -1 means show all expected lines of output
	}
};
#endif


// This virtual interface was added for the Generals RTS.
// It is called before resetting the device to ensure
// that all device resources are released.  Otherwise reset fails. jba.
class RenderCleanupHook
{
public:
	virtual void ReleaseResources(void)=0;
	virtual void ReAcquireResources(void)=0;
};


// Describes one fixed-function texture combiner stage. Fields left at WW3D_STAGE_KEEP are untouched;
// RenderDevice::Set_Texture_Stage applies only the set fields through the state cache. The backend
// uses these values as parameters of its uber pixel shader.
enum { WW3D_STAGE_KEEP = 0xFFFFFFFF };
struct TextureStageDesc
{
	unsigned ColorOp, ColorArg1, ColorArg2, ColorArg0;
	unsigned AlphaOp, AlphaArg1, AlphaArg2, AlphaArg0;
	unsigned TexCoordIndex;		///< WW3D_TSS_TEXCOORDINDEX including the WW3D_TCI_* generation mode
	unsigned TexTransformFlags;	///< WW3D_TSS_TEXTURETRANSFORMFLAGS

	TextureStageDesc() :
		// WW3D_STAGE_KEEP is an int enum (-1) and the fields are unsigned (C4245)
		ColorOp(STAGE_KEEP_U), ColorArg1(STAGE_KEEP_U), ColorArg2(STAGE_KEEP_U), ColorArg0(STAGE_KEEP_U),
		AlphaOp(STAGE_KEEP_U), AlphaArg1(STAGE_KEEP_U), AlphaArg2(STAGE_KEEP_U), AlphaArg0(STAGE_KEEP_U),
		TexCoordIndex(STAGE_KEEP_U), TexTransformFlags(STAGE_KEEP_U) {}
	static constexpr unsigned STAGE_KEEP_U = static_cast<unsigned>(WW3D_STAGE_KEEP);

	TextureStageDesc& Color(unsigned op, unsigned arg1, unsigned arg2) { ColorOp = op; ColorArg1 = arg1; ColorArg2 = arg2; return *this; }
	TextureStageDesc& Color_Op(unsigned op) { ColorOp = op; return *this; }
	TextureStageDesc& Color_Arg1(unsigned a) { ColorArg1 = a; return *this; }
	TextureStageDesc& Color_Arg2(unsigned a) { ColorArg2 = a; return *this; }
	TextureStageDesc& Color_Arg0(unsigned a) { ColorArg0 = a; return *this; }
	TextureStageDesc& Alpha(unsigned op, unsigned arg1, unsigned arg2) { AlphaOp = op; AlphaArg1 = arg1; AlphaArg2 = arg2; return *this; }
	TextureStageDesc& Alpha_Op(unsigned op) { AlphaOp = op; return *this; }
	TextureStageDesc& Alpha_Arg1(unsigned a) { AlphaArg1 = a; return *this; }
	TextureStageDesc& Alpha_Arg2(unsigned a) { AlphaArg2 = a; return *this; }
	TextureStageDesc& Alpha_Arg0(unsigned a) { AlphaArg0 = a; return *this; }
	TextureStageDesc& Tex_Coord_Index(unsigned v) { TexCoordIndex = v; return *this; }
	TextureStageDesc& Tex_Transform(unsigned v) { TexTransformFlags = v; return *this; }
};

// Optional graphics effects. The selection comes from -d3d11Effects or Options.ini "D3D11Effects"
// (W3DDisplay::init).
enum
{
	RENDER_EFFECT_FXAA				= 1 << 0,	///< FXAA anti-aliasing (post-process)
	RENDER_EFFECT_SHARPEN			= 1 << 1,	///< sharpening (CAS)
	RENDER_EFFECT_SOFT_PARTICLES	= 1 << 2,	///< soft particles using the depth copy
	RENDER_EFFECT_SSAO				= 1 << 3,	///< screen-space ambient occlusion
	RENDER_EFFECT_COLOR_GRADE		= 1 << 4,	///< color grading (saturation, contrast, warmth)
	RENDER_EFFECT_ALL				= 0x1F,	///< the five post-process effects only
	// Sun shadows via shadow map; not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_SHADOW_MAP		= 1 << 5,
	// Terrain detail layer from photo scans; not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_TERRAIN_DETAIL	= 1 << 6,
	// Triplanar rock layer on steep slopes with per-pixel lighting; not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_TERRAIN_ROCK		= 1 << 7,
	// Post-process glow, heat shimmer and distance haze; not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_BLOOM				= 1 << 8,
	RENDER_EFFECT_HEAT				= 1 << 9,
	RENDER_EFFECT_HAZE				= 1 << 10,
	// Per-pixel point lights from explosions, fire and muzzle flashes; not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_LIGHTS			= 1 << 11,
	// Particles on the GPU: smoke and dust take the time of day and the dynamic lights; billboards do not
	// sink into the terrain. Not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_PARTICLE_LIGHT	= 1 << 12,
	RENDER_EFFECT_PARTICLE_GROUND	= 1 << 13,
	// Relief of the terrain textures: normal maps and gloss next to them (<texture>_n.dds); not part of
	// RENDER_EFFECT_ALL.
	RENDER_EFFECT_TERRAIN_NORMAL	= 1 << 14,
	// Terrain tessellation with displacement from the heights of the terrain textures (<texture>_h.dds); not part
	// of RENDER_EFFECT_ALL.
	RENDER_EFFECT_TERRAIN_TESS		= 1 << 15,
	// Rock slopes in the terrain tessellation: displacement by the triplanar height of the rock layer textures (needs
	// RENDER_EFFECT_TERRAIN_TESS and RENDER_EFFECT_TERRAIN_ROCK); not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_ROCK_TESS			= 1 << 16,
	// With multisampling: alpha-tested draws (trees, fences, grilles) cover samples by their alpha, so cut-out edges
	// are smoothed like geometry edges. No effect without multisampling; not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_ALPHA_COVERAGE	= 1 << 17,
	// Sun shadows (with RENDER_EFFECT_SHADOW_MAP): penumbra growing with the distance to the caster (blocker search),
	// and short screen space rays toward the sun for contact shadows. Not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_SOFT_SHADOWS		= 1 << 18,
	RENDER_EFFECT_CONTACT_SHADOWS	= 1 << 19,
	// Sun shadows in two cascades: a near map over the near part of the range and the far map over twice the range
	// (flat cameras, cutscenes). Needs RENDER_EFFECT_SHADOW_MAP; not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_SHADOW_CASCADES	= 1 << 20,
	// With multisampling the effects that read the scene depth or color (depth copy, SSAO, soft particles, shadow map,
	// haze, lights, heat, water color copy) run on the resolved depth, unless RENDER_EFFECT_NO_MSAA_DEPTH is set; this bit
	// asks for it explicitly. Not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_MSAA_DEPTH		= 1 << 21,
	// Values above 1 in the 3D scene and a tone curve into 8 bits (RenderCore::Fx_Scene_To_8Bit_Target); the scene
	// target becomes R11G11B10 unless -sceneFormat names a wider one. Without multisampling only; not part of
	// RENDER_EFFECT_ALL.
	RENDER_EFFECT_HDR				= 1 << 22,
	// Triangular noise of one 8-bit step in the last post-processing pass: no visible steps in sky, haze and water
	// gradients. Not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_DEBAND			= 1 << 23,
	// Screen space reflection on the water (quality 2, flat cameras only). Not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_WATER_SSR			= 1 << 24,
	// Ambient occlusion by horizon search at full resolution (gtao.ps.hlsl) instead of the SSAO method; runs with or
	// without RENDER_EFFECT_SSAO and uses its radius, strength and range. Not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_GTAO				= 1 << 25,
	// Temporal anti-aliasing by camera reprojection: sub-pixel jitter of the scene projection and a history blended at
	// the start of the post-processing (rendercore_taa.cpp); replaces FXAA. Not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_TAA				= 1 << 26,
	// Terrain light per pixel: the prelighting and the dynamic lights of the terrain in the pixel shader instead of
	// the vertex color; not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_TERRAIN_LIGHT		= 1 << 27,
	// The depth effects explicitly off with multisampling ("-msaadepth", Settings.ini Effects.MsaaDepth = off): as before
	// the resolved depth existed (comparisons, scarce GPU time).
	RENDER_EFFECT_NO_MSAA_DEPTH		= 1 << 28,
	// Snow on flat grass, dirt and gravel of maps with snow ground (with RENDER_EFFECT_TERRAIN_ROCK, which carries the
	// slope and the ground classes); not part of RENDER_EFFECT_ALL.
	RENDER_EFFECT_FLAT_SNOW			= 1 << 29,
	// Models lit per pixel: diffuse and specular of the fixed-function lights with the interpolated normal in the
	// pixel shader (ffp.vs/ffp.ps PIXEL_LIGHT) for lit draws of the uber shaders, terrain excepted; not part of
	// RENDER_EFFECT_ALL.
	RENDER_EFFECT_MODEL_LIGHT		= 1 << 30,
	// Atmosphere after the ambient occlusion, with the shadow map only: height fog (RenderEffectSettings::height_fog)
	// and sun shafts through the shadow map (sun_shafts); a strength of 0 leaves that part out. Not part of
	// RENDER_EFFECT_ALL. The last free bit.
	RENDER_EFFECT_ATMOSPHERE		= 1 << 31
};

/// Copies of the opaque scene for other consumers (RenderDevice::Request_Scene_Copies).
enum
{
	RENDER_SCENE_COPY_DEPTH			= 1 << 0,
	RENDER_SCENE_COPY_COLOR			= 1 << 1
};

/// Filter that scales the 3D scene up from the internal render resolution (RenderEffectSettings::render_scale_filter).
enum RenderUpscaleFilter
{
	RENDER_UPSCALE_BILINEAR	= 0,
	RENDER_UPSCALE_BICUBIC	= 1,	///< Catmull-Rom
	RENDER_UPSCALE_CAS		= 2		///< Catmull-Rom with contrast adaptive sharpening
	// a render scale above 1 (supersampling) always scales down with an area filter
};

/// Scene target formats. RGBA16 (UNORM) clamps after every blend like BGRA8, RGBA16F does not. R11G11B10 has no
/// alpha; the renderer keeps the destination alpha in a target of its own.
enum
{
	SCENE_FORMAT_BGRA8		= 0,
	SCENE_FORMAT_RGBA16F	= 1,
	SCENE_FORMAT_RGBA16		= 2,
	SCENE_FORMAT_R11G11B10	= 3
};

struct RenderEffectSettings
{
	unsigned	flags;			///< RENDER_EFFECT_*
	float		sharpen;		///< CAS sharpness 0..1
	float		soft_distance;	///< fade distance of soft particles (world units)
	float		ssao_radius;	///< ambient occlusion radius (world units)
	float		ssao_strength;	///< ambient occlusion strength
	float		ssao_range;		///< view distance; occlusion fades out from 75 % of it
	float		saturation;		///< color grading: saturation (1 = unchanged)
	float		contrast;		///< color grading: S-curve amount 0..1
	float		warmth;			///< color grading: red up, blue down (0 = unchanged)
	unsigned	shadow_map_size;	///< shadow map edge length in texels
	float		shadow_distance;	///< shadow range from the camera (world units); faded out beyond
	/// Strength of shadows the terrain casts onto itself (1 = full). The terrain is prelit, so full
	/// terrain self-shadows would darken hilly maps twice.
	float		shadow_terrain_strength;
	/// Soft shadows: size of the light (0..2, 1 = tuned default); larger gives wider penumbrae.
	float		shadow_soft;
	/// Strength of the terrain detail layer (1 = tuned default, 0..2).
	float		terrain_detail;
	/// Terrain anisotropy (0/1 = trilinear), also used for the detail layer texture.
	unsigned	terrain_anisotropy;
	/// Mip LOD bias of the terrain detail and rock layer textures (0 = none, negative = sharper).
	float		terrain_lod_bias;
	/// Strength of the rock layer on steep slopes (0..1).
	float		terrain_rock;
	/// Share of snow on flat ground with RENDER_EFFECT_FLAT_SNOW (0..1).
	float		flat_snow;
	/// With RENDER_EFFECT_ATMOSPHERE: height fog density and sun shaft strength (0..2 each, 0 = off).
	float		height_fog;
	float		sun_shafts;
	/// Bloom strength (0..2, 1 = tuned default), heat shimmer amplitude (0..2), haze strength (0..1).
	float		bloom;
	float		heat;
	float		haze;
	/// Strength of the dynamic lights (0..2, 1 = tuned default).
	float		lights;
	/// Strength of the particle lighting (0..1, 1 = full).
	float		particle_light;
	/// Strength of the terrain relief from normal maps (0..2, 1 = tuned default).
	float		terrain_normal;
	/// Terrain tessellation: displacement strength (0..2, 1 = tuned default, 0 = tessellated without displacement),
	/// largest factor, target screen length of an edge piece (pixels), crack probe (1 = terrain green on red).
	float		terrain_tess;
	float		terrain_tess_max;
	float		terrain_tess_pixels;
	unsigned	terrain_tess_probe;
	/// Strength of the rock displacement on steep slopes (0..2, 1 = tuned default).
	float		rock_tess;
	/// Internal render resolution: share of the window resolution the 3D scene is drawn in (0.5..1, 1 = full),
	/// the filter that scales it up (RenderUpscaleFilter) and the sharpness of RENDER_UPSCALE_CAS (0..1).
	float		render_scale;
	unsigned	render_scale_filter;
	float		render_scale_sharpen;
	/// GPU timestamps also without effects and render scale (-gpuTiming).
	unsigned	gpu_timing;
	/// Format of the scene target (-sceneFormat, SCENE_FORMAT_*); read when the target is created.
	unsigned	scene_format;
	/// With RENDER_EFFECT_HDR: knee of the tone curve (-hdrKnee), exposure before the curve (the display sets it per
	/// frame, -hdrNight on night maps) and the factor for additively blended draws (-hdrAdditive).
	float		hdr_knee;
	float		hdr_exposure;
	float		hdr_additive;

	RenderEffectSettings()
		: flags(0), sharpen(0.5f), soft_distance(12.0f), ssao_radius(14.0f), ssao_strength(1.0f),
		  ssao_range(1600.0f), saturation(1.15f), contrast(0.15f), warmth(0.015f),
		  shadow_map_size(2048), shadow_distance(1500.0f), shadow_terrain_strength(0.5f), shadow_soft(1.0f), terrain_detail(1.0f),
		  terrain_anisotropy(0), terrain_lod_bias(0.0f), terrain_rock(1.0f), flat_snow(0.6f), height_fog(0.0f), sun_shafts(0.0f), bloom(1.0f), heat(1.0f), haze(0.4f), lights(1.0f),
		  particle_light(1.0f), terrain_normal(1.0f), terrain_tess(1.0f), terrain_tess_max(16.0f), terrain_tess_pixels(8.0f),
		  terrain_tess_probe(0), rock_tess(1.0f), render_scale(1.0f), render_scale_filter(RENDER_UPSCALE_BICUBIC),
		  render_scale_sharpen(0.5f), gpu_timing(0), scene_format(0),
		  hdr_knee(0.8f), hdr_exposure(1.0f), hdr_additive(1.0f) {}
};

struct RenderStateStruct
{
	ShaderClass shader;
	VertexMaterialClass* material;
	TextureBaseClass * Textures[MAX_TEXTURE_STAGES];
	WW3DLight Lights[4];
	bool LightEnable[4];
  //unsigned lightsHash;
	Matrix4x4 world;
	Matrix4x4 view;
	unsigned vertex_buffer_types[MAX_VERTEX_STREAMS];
	unsigned index_buffer_type;
	unsigned short vba_offset;
	unsigned short vba_count;
	unsigned short iba_offset;
	VertexBufferClass* vertex_buffers[MAX_VERTEX_STREAMS];
	IndexBufferClass* index_buffer;
	unsigned short index_base_offset;
	bool soft_particles;	///< billboard particles; carried through the sorter

	RenderStateStruct();
	~RenderStateStruct();

	RenderStateStruct& operator= (const RenderStateStruct& src);
};

/**
** RenderDevice
**
** Render device: wraps the device, filters redundant state changes, keeps statistics.  In general,
** we will wrap all device calls with at least an WWINLINE function so that we can add stat tracking,
** etc if needed.  Direct access to the device will require "friend" status and should be granted
** only in extreme circumstances :-)
*/
class RenderDevice
{
	enum ChangedStates {
		WORLD_CHANGED	=	1<<0,
		VIEW_CHANGED	=	1<<1,
		LIGHT0_CHANGED	=	1<<2,
		LIGHT1_CHANGED	=	1<<3,
		LIGHT2_CHANGED	=	1<<4,
		LIGHT3_CHANGED	=	1<<5,
		TEXTURE0_CHANGED=	1<<6,
		TEXTURE1_CHANGED=	1<<7,
		TEXTURE2_CHANGED=	1<<8,
		TEXTURE3_CHANGED=	1<<9,
		MATERIAL_CHANGED=	1<<14,
		SHADER_CHANGED	=	1<<15,
		VERTEX_BUFFER_CHANGED = 1<<16,
		INDEX_BUFFER_CHANGED = 1 << 17,
		WORLD_IDENTITY=	1<<18,
		VIEW_IDENTITY=		1<<19,

		TEXTURES_CHANGED=
			TEXTURE0_CHANGED|TEXTURE1_CHANGED|TEXTURE2_CHANGED|TEXTURE3_CHANGED,
		LIGHTS_CHANGED=
			LIGHT0_CHANGED|LIGHT1_CHANGED|LIGHT2_CHANGED|LIGHT3_CHANGED,
	};

	static void Draw_Sorting_IB_VB(
		unsigned primitive_type,
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	static void Draw(
		unsigned primitive_type,
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index=0,
		unsigned short vertex_count=0);

public:
#ifdef EXTENDED_STATS
	static RenderExtendedStats stats;
#endif

	static bool Init(void * hwnd, bool lite = false);
	static void Shutdown(void);

	static void SetCleanupHook(RenderCleanupHook *pCleanupHook) {m_pCleanupHook = pCleanupHook;};
	/*
	** Some WW3D sub-systems need to be initialized after the device is created and shutdown
	** before the device is released.
	*/
	static void	Do_Onetime_Device_Dependent_Inits(void);
	static void Do_Onetime_Device_Dependent_Shutdowns(void);

	static bool Is_Device_Lost() { return IsDeviceLost; }
	static bool Is_Initted(void) { return IsInitted; }

	// Device queries for game code, which has no access to the raw device.
	static bool		Has_Device(void);
	static bool		Is_Device_Ready(void);
	// Device state as an error code; the caller in ww3d.cpp must tell "lost" from "needs reset".
	static HRESULT	Get_Device_State(void);		///< device present and usable (not removed)

	// Buffer creation and stream source for game code that manages its own buffers (shadow paths).
	static HRESULT	Create_Device_Vertex_Buffer(unsigned length, unsigned usage, unsigned fvf, unsigned pool, WW3DVertexBufferHandle * out);
	static HRESULT	Create_Device_Index_Buffer(unsigned length, unsigned usage, WW3DDeviceFormat format, unsigned pool, WW3DIndexBufferHandle * out);
	static void		Set_Stream_Source(unsigned stream, WW3DVertexBufferHandle vb, unsigned offset, unsigned stride);

	// Hardware mouse cursor.
	static BOOL		Show_Cursor(BOOL show);
	static HRESULT	Set_Cursor_Properties(UINT hot_x, UINT hot_y, WW3DSurfaceHandle bitmap);
	static void		Set_Cursor_Position(int x, int y, DWORD flags);

	// Access to device resources through opaque handles.
	static HRESULT	Get_Texture_Surface_Level(WW3DTextureHandle texture, unsigned level, WW3DSurfaceHandle * out);
	static HRESULT	Get_Texture_Level_Desc(WW3DTextureHandle texture, unsigned level, WW3DSurfaceDesc* desc);
	static HRESULT	Get_Surface_Desc(WW3DSurfaceHandle surface, WW3DSurfaceDesc* desc);
	static HRESULT	Lock_Surface(WW3DSurfaceHandle surface, WW3DLockedRect* out, const RECT* rect, unsigned flags);
	static HRESULT	Unlock_Surface(WW3DSurfaceHandle surface);
	static HRESULT	Lock_Texture(WW3DTextureHandle texture, unsigned level, WW3DLockedRect* out, const RECT* rect, unsigned flags);
	static HRESULT	Unlock_Texture(WW3DTextureHandle texture, unsigned level);
	static unsigned	Get_Texture_Level_Count(WW3DTextureHandle texture);
	static WW3DTextureHandle Create_Team_Color_View(WW3DTextureHandle texture, unsigned color);	///< see RenderBackend
	// Resource management priority of managed textures, via RenderBackend.
	static unsigned	Get_Texture_Priority(WW3DTextureHandle texture);
	static unsigned	Set_Texture_Priority(WW3DTextureHandle texture, unsigned priority);
	static void		Set_Texture_LOD(WW3DTextureHandle texture, unsigned lod);
	// Handles have no methods, so reference counting and locking go through the wrapper,
	// overloaded per handle type.
	static void		Add_Ref_Resource(WW3DTextureHandle texture);
	static void		Add_Ref_Resource(WW3DSurfaceHandle surface);
	static void		Release_Resource(WW3DTextureHandle texture);
	static void		Release_Resource(WW3DSurfaceHandle surface);
	static void		Release_Resource(WW3DVertexShaderHandle shader);
	static void		Release_Resource(WW3DPixelShaderHandle shader);
	static void		Release_Resource(WW3DVertexDeclHandle decl);
	static void		Release_Resource(WW3DVertexBufferHandle vb);
	static void		Release_Resource(WW3DIndexBufferHandle ib);
	static HRESULT	Lock_Vertex_Buffer(WW3DVertexBufferHandle vb, unsigned offset, unsigned size, void** data, unsigned flags);
	static HRESULT	Unlock_Vertex_Buffer(WW3DVertexBufferHandle vb);
	static HRESULT	Lock_Index_Buffer(WW3DIndexBufferHandle ib, unsigned offset, unsigned size, void** data, unsigned flags);
	static HRESULT	Unlock_Index_Buffer(WW3DIndexBufferHandle ib);

	// Set a real shader object (not the FVF path of Set_Vertex_Shader(DWORD)). A separate name
	// instead of an overload, because Set_Vertex_Shader(NULL) would be ambiguous without a diagnostic.
	static void		Set_Vertex_Shader_Object(WW3DVertexShaderHandle shader);
	static void		Set_Pixel_Shader_Object(WW3DPixelShaderHandle shader);

	// Query the render target and create a system memory surface; W3DSmudge reads back part of
	// the frame with them.
	static HRESULT	Get_Render_Target(WW3DSurfaceHandle * out);
	static HRESULT	Create_Device_Offscreen_Surface(unsigned width, unsigned height, WW3DDeviceFormat format, WW3DSurfaceHandle * out);

	// Set an index buffer and draw indexed primitives directly; the shadow paths own their buffers
	// and bypass the wrapper's Draw_Triangles path.
	static void		Set_Indices(WW3DIndexBufferHandle ib);
	static void		Draw_Indexed_Primitive(WW3DPrimitiveType type, int base_vertex, unsigned min_index,
	                                       unsigned num_vertices, unsigned start_index, unsigned prim_count);

	// Switch render target and depth buffer without the bookkeeping of Set_Render_Target.
	// W3DShaderManager switches to its own texture for screen effects and restores the previous
	// state afterwards; the wrapper's bookkeeping would interfere with that.
	static HRESULT	Get_Depth_Stencil_Surface(WW3DSurfaceHandle * out);
	static HRESULT	Set_Render_Target_Raw(WW3DSurfaceHandle render_target, WW3DSurfaceHandle depth_stencil);

	// Create a render target texture. Shaders are created from HLSL only (Create_*_Shader_From_HLSL).
	static HRESULT	Create_Device_Render_Target_Texture(unsigned width, unsigned height, WW3DDeviceFormat format,
	                                                    WW3DTextureHandle * out);
	// Copy the current color target including alpha into a render target texture of the same size
	// (scene copy for water refraction).
	static bool		Copy_Render_Target_To_Texture(WW3DTextureHandle dest);

	static void					Report_Renderer_Unavailable(const char* stage);	///< no device layer starts: tell the user once
	static RenderBackend*		Get_Backend() { return Backend; }	///< active render backend
	/// Device layer for the next Init by name ("d3d11", "vulkan"; nullptr or empty: the first usable one in
	/// the default order). If the requested one is not built in or cannot be used, Init starts the next
	/// usable layer and Get_Backend_Fallback_Reason says why.
	static void					Set_Backend_Request(const char* name);
	static const char*			Get_Backend_Name();		///< device layer of the last Init ("d3d11", "vulkan"), empty before
	static const char*			Get_Backend_Title();	///< its name for the user ("Direct3D 11", "Vulkan")
	static bool					Is_Backend(const char* name);	///< the last Init runs on this device layer
	static unsigned				Get_Backend_Choices();	///< number of device layers built in
	static const char*			Get_Backend_Fallback_Reason() { return BackendFallbackReason; }	///< empty if the request was met
	/// Scene depth as 32-bit float with 8-bit stencil instead of D24S8, for the next Init (the format Vulkan
	/// has to use on devices without D24S8).
	static void					Set_Float_Depth(bool on) { FloatDepth = on; }
	static bool					Get_Float_Depth() { return FloatDepth; }

	static bool Has_Stencil (void);
	static void Get_Format_Name(unsigned int format, StringClass *tex_format);

	/*
	** Rendering
	*/
	static void Begin_Scene(void);
	static void End_Scene(bool flip_frame = true);

	static void Clear(bool clear_color, bool clear_z_stencil, const Vector3 &color, float dest_alpha=0.0f, float z=1.0f, unsigned int stencil=0);

	static void	Set_Viewport(CONST WW3DViewport* pViewport);

	static void Set_Vertex_Buffer(const VertexBufferClass* vb, unsigned stream=0);
	static void Set_Vertex_Buffer(const DynamicVBAccessClass& vba);
	static void Set_Index_Buffer(const IndexBufferClass* ib,unsigned short index_base_offset);
	static void Set_Index_Buffer(const DynamicIBAccessClass& iba,unsigned short index_base_offset);
	static void Set_Index_Buffer_Index_Offset(unsigned offset);

	static void Get_Render_State(RenderStateStruct& state);
	static void Set_Render_State(const RenderStateStruct& state);
	static void Release_Render_State();

	static void Set_Device_Material(const WW3DMaterial* mat);

	static void Set_Gamma(float gamma,float bright,float contrast,bool calibrate=true,bool uselimit=true);

	// Set_ and Get_Transform() functions take the matrix in Westwood convention format.

	static void Set_ZBias(int zbias);
	static void Set_Projection_Transform_With_Z_Bias(const Matrix4x4& matrix,float znear, float zfar);	// pointer to 16 matrices

	static void Set_Transform(WW3DTransformType transform,const Matrix4x4& m);
	static void Set_Transform(WW3DTransformType transform,const Matrix3D& m);
	static void Get_Transform(WW3DTransformType transform, Matrix4x4& m);
	static void	Set_World_Identity();
	static void Set_View_Identity();
	static bool	Is_World_Identity();
	static bool Is_View_Identity();

	// Note that *_Device_Transform() functions take the matrix in Direct3D layout - transposed from Westwood convention.

	static void _Set_Device_Transform(WW3DTransformType transform,const Matrix4x4& m);
	static void _Set_Device_Transform(WW3DTransformType transform,const Matrix3D& m);
	static void _Get_Device_Transform(WW3DTransformType transform, Matrix4x4& m);
	static void _Get_Device_Shader_Projection(Matrix4x4& m);	///< projection for custom vertex shaders, including the backend's raster offset

	static void Set_Device_Light(int index,WW3DLight* light);
	static void Set_Render_State(WW3DRenderStateType state, unsigned value);
	static void Set_Clip_Plane(DWORD Index, CONST float* pPlane);
	static void Set_Texture_Stage_State(unsigned stage, WW3DTextureStageStateType state, unsigned value);

	// Set a whole combiner stage as one recipe. It is applied through Set_Texture_Stage_State and
	// therefore through the redundancy cache, in a fixed order within the stage.
	static void Set_Texture_Stage(unsigned stage, const TextureStageDesc& desc);
	static void Disable_Texture_Stage(unsigned stage);	///< COLOROP and ALPHAOP to WW3D_TOP_DISABLE
	// Texture filtering and addressing are sampler state (WW3DSamplerStateType), used by
	// texturefilter.cpp.
	static void Set_Sampler_State(unsigned stage, WW3DSamplerStateType state, unsigned value);
	// Converts an integer z-bias level (0-16) into a WW3D_RS_DEPTHBIAS value (float depth offset as a
	// DWORD bit pattern).
	static unsigned Convert_ZBias_Level(int zbias_level);
	static void Set_Device_Texture(unsigned int stage, WW3DTextureHandle texture);
	static void Set_Light_Environment(LightEnvironmentClass* light_env);
	static LightEnvironmentClass* Get_Light_Environment() { return Light_Environment; }
	static void Set_Fog(bool enable, const Vector3 &color, float start, float end);

	static WWINLINE const WW3DLight& Peek_Light(unsigned index);
	static WWINLINE bool Is_Light_Enabled(unsigned index);

	static bool Validate_Device(void);

	// Deferred

	static void Set_Shader(const ShaderClass& shader);
	static void Get_Shader(ShaderClass& shader);
	static void Set_Texture(unsigned stage,TextureBaseClass* texture);
	static void Set_Material(const VertexMaterialClass* material);
	static void Set_Light(unsigned index,const WW3DLight* light);
	static void Set_Light(unsigned index,const LightClass &light);

	static void Apply_Render_State_Changes();	// Apply deferred render state changes (will be called automatically by Draw...)

	static void Draw_Triangles(
		unsigned buffer_type,
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);
	static void Draw_Triangles(
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);
	static void Draw_Strip(
		unsigned short start_index,
		unsigned short index_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	/*
	** Resources
	*/


	static WW3DTextureHandle _Create_Device_ZTexture
	(
		unsigned int width,
		unsigned int height,
		WW3DZFormat zformat,
		MipCountType mip_level_count,
		unsigned pool=WW3D_POOL_MANAGED
	);


	static WW3DTextureHandle _Create_Device_Texture
	(
		unsigned int width,
		unsigned int height,
		WW3DFormat format,
		MipCountType mip_level_count,
		unsigned pool=WW3D_POOL_MANAGED,
		bool rendertarget=false
	);
	static WW3DTextureHandle _Create_Device_Texture(WW3DSurfaceHandle surface, MipCountType mip_level_count);

	static WW3DSurfaceHandle _Create_Device_Surface(unsigned int width, unsigned int height, WW3DFormat format);
	static WW3DSurfaceHandle _Create_Device_Surface(const char *filename);
	static WW3DSurfaceHandle _Get_Front_Buffer();
	static SurfaceClass * _Get_Back_Buffer(unsigned int num=0);
	/// Fullscreen covers only an area of width x height centred on the monitor (0 = whole monitor).
	/// For tests of the fullscreen path without covering the screen.
	static void Set_Fullscreen_Area(int width, int height);
	/// Converts a point from window client coordinates to display (scene) coordinates.
	static void Window_To_Render(int & x, int & y);
	static void Request_Back_Buffer_Capture(const char *bmp_path);	///< save the back buffer as a 24-bit BMP before the next Present

	// Optional graphics effects, implemented by the backend.
	static void Set_Effect_Settings(const RenderEffectSettings& settings);	///< call before the device is created
	static const RenderEffectSettings& Get_Effect_Settings();
	static void Set_Soft_Particles(bool enable);			///< following draws are billboard particles
	static void Scene_Opaque_Done();						///< opaque 3D scene done (before water/particles)
	static void Scene_3D_Done(int x, int y, int width, int height);	///< 3D scene done, 2D follows
	static void Scene_3D_Begin(int x, int y, int width, int height);	///< 3D scene of the view begins (internal render resolution)
	static void Request_Scene_Copies(unsigned flags);		///< RENDER_SCENE_COPY_* (for water etc.)
	static void Set_Dynamic_Lights(const RenderDynamicLight* lights, unsigned count);	///< lights of the next 3D scene
	static WW3DTextureHandle Get_Scene_Depth_Copy();		///< depth copy of this frame or NULL
	static WW3DTextureHandle Get_Scene_Color_Copy();		///< color copy of this frame or NULL
	static void Get_Effect_Statistics(char* text, unsigned size);	///< statistics line for the runner log
	static void Get_Device_Statistics(char* text, unsigned size);	///< device line for the runner log (Vulkan pipelines)
	static void Get_Shader_Failures(char* text, unsigned size);		///< line for the runner log if a shader did not compile, else empty
	// Sun shadows via shadow map.
	static void Shadow_Scene_Begin(RenderShadowParams& params);	///< sets view and projection itself
	static bool Shadow_Maps_Active();						///< shadow maps instead of stencil/projected shadows
	/// Following draws are only recorded as shadow casters, not drawn and not counted (geometry outside the
	/// view that can still shadow the visible ground); only while Shadow_Maps_Active().
	static void Set_Shadow_Caster_Only(bool enable);
	static void Scene_Terrain_Done();						///< terrain drawn, objects follow
	/// GPU time of a part of the frame (GPUEFFEKTE line); only measured with timing on. A part that runs more
	/// than once per frame (the water mirror draws a scene too) is counted for its last run.
	static void Gpu_Pass_Begin(RenderGpuPass pass);
	static void Gpu_Pass_End(RenderGpuPass pass);
	/// A pass that draws into a texture of its own: Begin sets the target (with depth, or the default depth buffer)
	/// and starts the time range, End goes back to the frame's target and ends it.
	static void Begin_Target_Pass(RenderGpuPass pass, TextureClass* target, ZTextureClass* depth = nullptr);
	static void End_Target_Pass(RenderGpuPass pass);
	/// The same for surfaces without the target bookkeeping (Set_Render_Target_Raw): the screen filters draw the scene
	/// into a surface of their own and keep the scene's depth buffer. End restores the surfaces the caller kept.
	static HRESULT Begin_Surface_Pass(RenderGpuPass pass, WW3DSurfaceHandle target, WW3DSurfaceHandle depth);
	static HRESULT End_Surface_Pass(RenderGpuPass pass, WW3DSurfaceHandle target, WW3DSurfaceHandle depth);
	// Terrain detail layer.
	static void Set_Terrain_Detail_Table(const RenderTerrainDetailTable& table);	///< placement of terrain textures in the atlas
	static void Set_Terrain_Detail(bool enable);			///< following draws: terrain with atlas coordinates
	// Inputs of the terrain rock layer.
	static void Set_Terrain_Height_Field(const unsigned char* heights, int width, int height, int border,
		float cell_size, float height_scale);				///< map height field (grid points, bytes)
	static void Set_Terrain_Lighting(const RenderTerrainLighting& lighting);	///< prelighting light = scene light of the frame constants
	// Terrain relief: atlas of normals and gloss in the layout of the terrain atlas.
	static void Set_Terrain_Relief_Atlas(WW3DTextureHandle atlas, bool active);	///< false: drop it if it is this one
	static void Set_Model_Normal_Texture(WW3DTextureHandle owner, WW3DTextureHandle normal);	///< see RenderBackend
	// Terrain tessellation (see RenderBackend).
	static void Set_Terrain_Tessellation(bool enable);		///< following draws: terrain geometry
	static float Terrain_Tess_Max_Displacement();			///< largest move of the tessellated surface (world units), 0 when off
	static void Set_Terrain_Tess_Data(const RenderTerrainTessData& data, bool active);	///< false: drop it if it is this atlas
	static void Set_Terrain_Tess_Mask(const unsigned char* mask, int width, int height);
	// Dynamic terrain lights on the GPU (see RenderBackend).
	static bool Terrain_Dynamic_Lights_On_GPU();
	static void Set_Terrain_Dynamic_Lights(const RenderTerrainDynamicLight* lights, unsigned count);
	static void Enable_Terrain_Dynamic_Lights(bool enable);	///< following draws: terrain tiles
	static void Set_Terrain_Static_Lighting(const RenderTerrainStaticLighting* lighting);	///< nullptr: prelit tiles
	// Skinning on the GPU (see RenderBackend).
	static unsigned Max_Skin_Bones();
	static void Set_Skin_Bones(const float* rows, unsigned count);
	// Particles built on the GPU (see RenderBackend).
	static bool Supports_Particle_Instancing();
	/// Instanced meshes (TextureCategoryClass::Render); on by default, off for comparisons (-noMeshInstancing).
	static void Enable_Mesh_Instancing(bool enable) { _MeshInstancing = enable; }
	static bool Mesh_Instancing_Enabled() { return _MeshInstancing; }
	static void Begin_Mesh_Instances();
	static void End_Mesh_Instances();
	static void Set_Particle_Tables(const float* rotation, const float* ground);
	static void Draw_Particles(const RenderParticleInstance* particles, unsigned count, const RenderParticleSetup& setup);
	static bool Upload_Particle_Batch(const RenderParticleInstance* particles, unsigned count);
	static void Draw_Particle_Batch(unsigned first, unsigned count, const RenderParticleSetup& setup);
	static void Set_Particle_Lighting(const float scale[3]);	///< light of the time of day relative to the afternoon
	static void Set_Effect_Clock(unsigned ms);	///< clock of the animated post effects (heat waves); without it the wall clock
	static unsigned Get_Effect_Clock();
	// Upload diagnosis: with track the backend hashes everything it uploads (content per resource); between
	// Begin_Upload_Log and End_Upload_Log it reports a summary line, with detail also one line per upload and draw.
	// fill >= 0 presets memory handed out unwritten with that byte; set it before resources are created.
	static void Set_Upload_Diagnosis(bool track, int fill);
	static void Begin_Upload_Log(void (*sink)(const char* line), unsigned frame, bool detail);
	static void End_Upload_Log();

	static void _Copy_Rects(
			WW3DSurfaceHandle pSourceSurface,
			CONST RECT* pSourceRectsArray,
			UINT cRects,
			WW3DSurfaceHandle pDestinationSurface,
			CONST POINT* pDestPointsArray
	);

	static void _Update_Texture(TextureClass *system, TextureClass *video);
	static void Flush_Resource_Manager(unsigned int bytes=0);
	static unsigned __int64 Get_Free_Texture_RAM();	// 64 bit, video memory can exceed 4 GB

	static unsigned _Get_Main_Thread_ID() { return _MainThreadID; }
	/// The thread that may use the device; the simulation thread while the main thread waits for it.
	static void _Set_Main_Thread_ID(unsigned id) { _MainThreadID = id; }
	static const WW3DAdapterIdentifier& Get_Current_Adapter_Identifier() { return CurrentAdapterIdentifier; }
	static void Set_Current_Adapter_Identifier(const WW3DAdapterIdentifier& id) { CurrentAdapterIdentifier = id; }	///< set by the backend when it creates the device

	/*
	** Statistics
	*/
	static void Begin_Statistics();
	static void End_Statistics();
	static unsigned Get_Last_Frame_Matrix_Changes();
	static unsigned Get_Last_Frame_Material_Changes();
	static unsigned Get_Last_Frame_Vertex_Buffer_Changes();
	static unsigned Get_Last_Frame_Index_Buffer_Changes();
	static unsigned Get_Last_Frame_Light_Changes();
	static unsigned Get_Last_Frame_Texture_Changes();
	static unsigned Get_Last_Frame_Render_State_Changes();
	static unsigned Get_Last_Frame_Texture_Stage_State_Changes();
	static unsigned Get_Last_Frame_API_Calls();
	static unsigned Get_Last_Frame_Draw_Calls();

	static unsigned long Get_FrameCount(void);

	// Needed by shader class
	static bool						Get_Fog_Enable() { return FogEnable; }
	static WW3DColor				Get_Fog_Color() { return FogColor; }

	// Utilities
	static Vector4 Convert_Color(unsigned color);
	static unsigned int Convert_Color(const Vector4& color);
	static unsigned int Convert_Color(const Vector3& color, const float alpha);
	static void Clamp_Color(Vector4& color);
	static unsigned int Convert_Color_Clamp(const Vector4& color);

	static void			  Set_Alpha (const float alpha, unsigned int &color);

	static void _Enable_Triangle_Draw(bool enable) { _EnableTriangleDraw=enable; }
	/// Constant buffer ring with Direct3D 11.1 offsets (default on; off for comparisons). Read at device creation.
	static void Enable_Constant_Buffer_Ring(bool enable) { _ConstantBufferRing = enable; }
	static bool Constant_Buffer_Ring_Enabled() { return _ConstantBufferRing; }
	static bool _Is_Triangle_Draw_Enabled() { return _EnableTriangleDraw; }

	/*
	** Additional swap chain interface
	**
	**		Use this interface to render to multiple windows (in windowed mode).
	**	To render to an additional window, the sequence of calls should look
	**	something like this:
	**
	**	RenderDevice::Set_Render_Target (swap_chain_ptr);
	**
	**	WW3D::Begin_Render (true, true, Vector3 (0, 0, 0));
	**	WW3D::Render (scene, camera, FALSE, FALSE);
	**	WW3D::End_Render ();
	**
	**	swap_chain_ptr->Present (NULL, NULL, NULL, NULL);
	**
	**	RenderDevice::Set_Render_Target ((WW3DSurfaceHandle)NULL);
	**
	*/
	static WW3DSwapChainHandle 	Create_Additional_Swap_Chain (HWND render_window);

	/*
	** Render target interface. If render target format is WW3D_FORMAT_UNKNOWN, current display format is used.
	*/
	static TextureClass *	Create_Render_Target (int width, int height, WW3DFormat format = WW3D_FORMAT_UNKNOWN);

	static void					Set_Render_Target (WW3DSurfaceHandle render_target, bool use_default_depth_buffer = false);
	static void					Set_Render_Target (WW3DSurfaceHandle render_target, WW3DSurfaceHandle dpeth_buffer);

	static void					Set_Render_Target (WW3DSwapChainHandle swap_chain);
	static bool					Is_Render_To_Texture(void) { return IsRenderToTexture; }

	// for depth map support KJM V
	static void Create_Render_Target
	(
		int width, 
		int height, 
		WW3DFormat format,
		WW3DZFormat zformat,
		TextureClass** target,
		ZTextureClass** depth_buffer
	);
	static void					Set_Render_Target_With_Z (TextureClass * texture, ZTextureClass* ztexture=nullptr);

	static void Set_Shadow_Map(int idx, ZTextureClass* ztex) { Shadow_Map[idx]=ztex; }
	static ZTextureClass* Get_Shadow_Map(int idx) { return Shadow_Map[idx]; }
	// for depth map support KJM ^

	// shader system udpates KJM v
	static void Apply_Default_State();

	static void Set_Vertex_Shader(DWORD vertex_shader);
	static void Set_Pixel_Shader(DWORD pixel_shader);

	static void Set_Vertex_Shader_Constant(int reg, const void* data, int count);
	static void Set_Pixel_Shader_Constant(int reg, const void* data, int count);
	// Set one 4-component register directly from a Vector4.
	static void Set_Vertex_Shader_Constant(int reg, const Vector4& v) { Set_Vertex_Shader_Constant(reg, &v.X, 1); }
	static void Set_Pixel_Shader_Constant(int reg, const Vector4& v) { Set_Pixel_Shader_Constant(reg, &v.X, 1); }

	// Vertex declarations (input layouts) instead of FVF codes.
	static HRESULT	Create_Vertex_Declaration(const WW3DVertexElement* elements, WW3DVertexDeclHandle * out);
	static void		Set_Vertex_Declaration(WW3DVertexDeclHandle decl);
	static WW3DVertexDeclHandle Get_Vertex_Declaration_For_FVF(DWORD fvf);
	/// Draws a screen quad: 4 vertices (triangle strip, position as XYZ in pixel coordinates of the
	/// current viewport) with identity world/view and a pixel ortho projection. Lighting and fog are
	/// off; the matrices are restored afterwards.
	static void		Draw_Screen_Quad_UP(const void* vertices, UINT stride, DWORD fvf);
	// Compile HLSL source at runtime (d3dcompiler_47).
	static HRESULT	Create_Vertex_Shader_From_HLSL(const char* source, unsigned length, const char* debug_name, WW3DVertexShaderHandle * out);
	static HRESULT	Create_Pixel_Shader_From_HLSL(const char* source, unsigned length, const char* debug_name, WW3DPixelShaderHandle * out);

	// Needed by scene lighting class
	static void						Set_Ambient(const Vector3& color);
	static const Vector3&		Get_Ambient() { return Ambient_Color; }
	// shader system updates KJM ^




	/// Returns the display format - added by TR for video playback - not part of W3D
	static WW3DFormat	getBackBufferFormat( void );
	static bool Reset_Device(bool reload_assets=true);


	// Requested multisample count (0 = off, 2/4/8/16 samples). Set before device creation; the next
	// Set_Render_Device checks it against the device and lowers it to the next supported level.
	static void	Set_Multisample_Samples(int samples);
	static int	Get_Multisample_Samples(void);


	static const char* Get_Render_State_Name(WW3DRenderStateType state);
	static const char* Get_Texture_Stage_State_Name(WW3DTextureStageStateType state);
	static unsigned Get_Render_State(WW3DRenderStateType state) { return RenderStates[state]; }

	// Names of the specific values of render states and texture stage states
	static void Get_Texture_Stage_State_Value_Name(StringClass& name, WW3DTextureStageStateType state, unsigned value);
	static void Get_Render_State_Value_Name(StringClass& name, WW3DRenderStateType state, unsigned value);

	static const char* Get_Texture_Address_Name(unsigned value);
	static const char* Get_Texture_Filter_Name(unsigned value);
	static const char* Get_Texture_Arg_Name(unsigned value);
	static const char* Get_Texture_Op_Name(unsigned value);
	static const char* Get_Texture_Transform_Flag_Name(unsigned value);
	static const char* Get_ZBuffer_Type_Name(unsigned value);
	static const char* Get_Fill_Mode_Name(unsigned value);
	static const char* Get_Shade_Mode_Name(unsigned value);
	static const char* Get_Blend_Name(unsigned value);
	static const char* Get_Cull_Mode_Name(unsigned value);
	static const char* Get_Cmp_Func_Name(unsigned value);
	static const char* Get_Fog_Mode_Name(unsigned value);
	static const char* Get_Stencil_Op_Name(unsigned value);
	static const char* Get_Material_Source_Name(unsigned value);
	static const char* Get_Vertex_Blend_Flag_Name(unsigned value);
	static const char* Get_Patch_Edge_Style_Name(unsigned value);
	static const char* Get_Debug_Monitor_Token_Name(unsigned value);
	static const char* Get_Blend_Op_Name(unsigned value);

	static void Invalidate_Cached_Render_States(void);

	static void Set_Draw_Polygon_Low_Bound_Limit(unsigned n) { DrawPolygonLowBoundLimit=n; }

protected:

	static bool	Create_Device(void);
	static void Release_Device(void);

	static void Reset_Statistics();
	static void Enumerate_Devices();
	static void Set_Default_Global_Render_States(void);

	/*
	** Device Selection Code.
	** For backward compatibility, the public interface for these functions is in the ww3d.
	** header file.  These functions are protected so that we aren't exposing two interfaces.
	*/
	static bool Set_Any_Render_Device(void);
	static bool	Set_Render_Device(const char * dev_name,int width=-1,int height=-1,int bits=-1,int windowed=-1,bool resize_window=false);
	static bool	Set_Render_Device(int dev=-1,int resx=-1,int resy=-1,int bits=-1,int windowed=-1,bool resize_window = false, bool reset_device = false, bool restore_assets=true);
	static bool Set_Next_Render_Device(void);
	static void Fit_Window_To_Resolution(bool windowed);

	static int	Get_Render_Device_Count(void);
	static int	Get_Render_Device(void);
	static const RenderDeviceDescClass & Get_Render_Device_Desc(int deviceidx);
	static const char * Get_Render_Device_Name(int device_index);
	static bool Set_Device_Resolution(int width=-1,int height=-1,int bits=-1,int windowed=-1, bool resize_window=false);
	static void Get_Device_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed);
	static void Get_Render_Target_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed);
	static int	Get_Device_Resolution_Width(void) { return ResolutionWidth; }
	static int	Get_Device_Resolution_Height(void) { return ResolutionHeight; }

	static bool Is_Windowed(void) { return IsWindowed; }

	static void	Set_Swap_Interval(int swap);
	static int	Get_Swap_Interval(void);
	static void Set_Polygon_Mode(int mode);

	/*
	** Internal functions
	*/
	static void Compute_Caps();

	/*
	** Protected Member Variables
	*/

	static RenderCleanupHook *m_pCleanupHook;

	static RenderStateStruct			render_state;
	static unsigned						render_state_changed;
	static Matrix4x4						DeviceTransforms[WW3D_TS_WORLD+1];

	static bool								IsInitted;
	static bool								IsDeviceLost;
	static void *							Hwnd;
	static unsigned						_MainThreadID;

	static bool								_EnableTriangleDraw;
	static bool								_ConstantBufferRing;
	static bool								_MeshInstancing;

	static int								CurRenderDevice;
	static int								ResolutionWidth;
	static int								ResolutionHeight;
	static int								BitDepth;
	static bool								IsWindowed;
	
	static WW3DMatrixRaw						old_world;
	static WW3DMatrixRaw						old_view;
	static WW3DMatrixRaw						old_prj;

	// shader system updates KJM v
	static DWORD							Vertex_Shader;
	static DWORD							Pixel_Shader;

	// Cache for Set_Vertex_Declaration/Release_Vertex_Declarations
	static WW3DVertexDeclHandle 	CurrentVertexDeclaration;
	static void	Release_Vertex_Declarations();	///< clear the FVF declaration cache (before Release_Device)

	// Base vertex index for the next indexed draw; setting the index buffer does not take it.
	static unsigned						CachedIndexBaseOffset;

	static Vector4							Vertex_Shader_Constants[MAX_VERTEX_SHADER_CONSTANTS];
	static Vector4							Pixel_Shader_Constants[MAX_PIXEL_SHADER_CONSTANTS];

	static LightEnvironmentClass*		Light_Environment;
	static RenderInfoClass*				Render_Info;


	static ZTextureClass*				Shadow_Map[MAX_SHADOW_MAPS];

	static Vector3							Ambient_Color;
	// shader system updates KJM ^

	static bool								world_identity;
	static unsigned						RenderStates[256];
	// WW3DTextureStageStateType goes up to WW3D_TSS_CONSTANT (=32), so 33 valid values (0..32).
	static unsigned						TextureStageStates[MAX_TEXTURE_SLOTS][33];
	// Cached sampler states (address, filter, anisotropy).
	static unsigned						SamplerStates[MAX_TEXTURE_SLOTS][16];
	static WW3DTextureHandle 	Textures[MAX_TEXTURE_SLOTS];

	// These fog settings are constant for all objects in a given scene,
	// unlike the matching renderstates which vary based on shader settings.
	static bool								FogEnable;
	static WW3DColor						FogColor;

	static unsigned						matrix_changes;
	static unsigned						material_changes;
	static unsigned						vertex_buffer_changes;
	static unsigned						index_buffer_changes;
	static unsigned						light_changes;
	static unsigned						texture_changes;
	static unsigned						render_state_changes;
	static unsigned						texture_stage_state_changes;
	static unsigned						draw_calls;
	static bool								CurrentDeviceLightEnables[4];

	static unsigned long FrameCount;


	static WW3DAdapterIdentifier		CurrentAdapterIdentifier;

	static RenderBackend*						Backend;				///< active render backend
	static char									BackendRequest[16];
	static const GpuLayerInfo*					BackendLayer;
	static char									BackendFallbackReason[512];
	static bool									FloatDepth;
	static WW3DInterfaceHandle 					D3DInterface;
	static WW3DDeviceHandle 			D3DDevice;

	static WW3DSurfaceHandle 			CurrentRenderTarget;
	static WW3DSurfaceHandle 			CurrentDepthBuffer;
	static WW3DSurfaceHandle 			DefaultRenderTarget;
	static WW3DSurfaceHandle 			DefaultDepthBuffer;

	static unsigned							DrawPolygonLowBoundLimit;

	static bool								IsRenderToTexture;

	static int								ZBias;
	static float							ZNear;
	static float							ZFar;
	static Matrix4x4						ProjectionMatrix;

	friend void Device_Assert();
	friend class WW3D;
	friend class DeviceIndexBufferClass;
	friend class DeviceVertexBufferClass;
};

// shader system updates KJM v



// shader system updates KJM ^






// ----------------------------------------------------------------------------
//
// Set the index offset for the current index buffer
//
// ----------------------------------------------------------------------------

WWINLINE void RenderDevice::Set_Index_Buffer_Index_Offset(unsigned offset)
{
	if (render_state.index_base_offset==offset) return;
	render_state.index_base_offset=static_cast<unsigned short>(offset);
	render_state_changed|=INDEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
// Set the fog settings. This function should be used, rather than setting the
// appropriate renderstates directly, because the shader sets some of the
// renderstates on a per-mesh / per-pass basis depending on global fog states
// (stored in the wrapper) as well as the shader settings.
// This function should be called rarely - once per scene would be appropriate.
// ----------------------------------------------------------------------------

WWINLINE void RenderDevice::Set_Fog(bool enable, const Vector3 &color, float start, float end)
{
	// Set global states
	FogEnable = enable;
	FogColor = Convert_Color(color,0.0f);

	// Invalidate the current shader (since the renderstates set by the shader
	// depend on the global fog settings as well as the actual shader settings)
	ShaderClass::Invalidate();

	// Set renderstates which are not affected by the shader
	Set_Render_State(WW3D_RS_FOGSTART, *(DWORD *)(&start));
	Set_Render_State(WW3D_RS_FOGEND,   *(DWORD *)(&end));
}


WWINLINE void RenderDevice::Set_Ambient(const Vector3& color)
{
	Ambient_Color=color;
	Set_Render_State(WW3D_RS_AMBIENT, RenderDevice::Convert_Color(color,0.0f));
}

// ----------------------------------------------------------------------------
//
// Set vertex buffer to be used in the subsequent render calls. If there was
// a vertex buffer being used earlier, release the reference to it. Passing
// NULL just will release the vertex buffer.
//
// ----------------------------------------------------------------------------







// WW3D_RS_DEPTHBIAS is a plain float offset on the depth value, unlike the old discrete 0..16 z-bias
// levels. The factor -0.000005 per level is an approximation; the visible shift also depends on the
// depth buffer format. If z-fighting shows, WW3D_RS_SLOPESCALEDEPTHBIAS can compensate steep angles.
WWINLINE unsigned RenderDevice::Convert_ZBias_Level(int zbias_level)
{
	float depth_bias = -(float)zbias_level * 0.000005f;
	return *(unsigned*)&depth_bias;
}



WWINLINE Vector4 RenderDevice::Convert_Color(unsigned color)
{
	Vector4 col;
	col[3]=((color&0xff000000)>>24)/255.0f;
	col[0]=((color&0xff0000)>>16)/255.0f;
	col[1]=((color&0xff00)>>8)/255.0f;
	col[2]=((color&0xff)>>0)/255.0f;
//	col=Vector4(1.0f,1.0f,1.0f,1.0f);
	return col;
}


// ----------------------------------------------------------------------------
//
// Convert RGBA color from float vector to 32 bit integer
// Note: Color vector needs to be clamped to [0...1] range!
//
// ----------------------------------------------------------------------------

// Truncates like the (int) cast (cvttss2si). Result format AAAAAAAARRRRRRRRGGGGGGGGBBBBBBBB.
WWINLINE unsigned int RenderDevice::Convert_Color(const Vector3& color,float alpha)
{
	const float scale = 255.0f;
	const int a = (int)(alpha * scale);
	const int r = (int)(color.X * scale);
	const int g = (int)(color.Y * scale);
	const int b = (int)(color.Z * scale);
	return ((unsigned)a << 24) | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}

// ----------------------------------------------------------------------------
//
// Clamp color vertor to [0...1] range
//
// ----------------------------------------------------------------------------

WWINLINE void RenderDevice::Clamp_Color(Vector4& color)
{
	for (int i=0;i<4;++i) {
		float f=(color[i]<0.0f) ? 0.0f : color[i];
		color[i]=(f>1.0f) ? 1.0f : f;
	}
}

// ----------------------------------------------------------------------------
//
// Convert RGBA color from float vector to 32 bit integer
//
// ----------------------------------------------------------------------------

WWINLINE unsigned int RenderDevice::Convert_Color(const Vector4& color)
{
	return Convert_Color(reinterpret_cast<const Vector3&>(color),color[3]);
}

WWINLINE unsigned int RenderDevice::Convert_Color_Clamp(const Vector4& color)
{
	Vector4 clamped_color=color;
	RenderDevice::Clamp_Color(clamped_color);
	return Convert_Color(reinterpret_cast<const Vector3&>(clamped_color),clamped_color[3]);
}



WWINLINE void RenderDevice::Set_Alpha (const float alpha, unsigned int &color)
{
	unsigned char *component = (unsigned char*) &color;

	component [3] = static_cast<unsigned char>(255.0f * alpha);
}

WWINLINE void RenderDevice::Get_Render_State(RenderStateStruct& state)
{
	state=render_state;
}

WWINLINE void RenderDevice::Get_Shader(ShaderClass& shader)
{
	shader=render_state.shader;
}

WWINLINE void RenderDevice::Set_Texture(unsigned stage,TextureBaseClass* texture)
{
	WWASSERT(stage<(unsigned int)RenderCaps::Get_Max_Textures_Per_Pass());
	if (texture==render_state.Textures[stage]) return;
	REF_PTR_SET(render_state.Textures[stage],texture);
	render_state_changed|=(TEXTURE0_CHANGED<<stage);
}

WWINLINE void RenderDevice::Set_Material(const VertexMaterialClass* material)
{
/*	if (material && render_state.material &&
		// !stricmp(material->Get_Name(),render_state.material->Get_Name())) {
		material->Get_CRC()!=render_state.material->Get_CRC()) {
		return;
	}
*/
//	if (material==render_state.material) {
//		return;
//	}
	REF_PTR_SET(render_state.material,const_cast<VertexMaterialClass*>(material));
	render_state_changed|=MATERIAL_CHANGED;
	SNAPSHOT_SAY(("RenderDevice::Set_Material(%s)\n",material ? material->Get_Name() : "NULL"));
}

WWINLINE void RenderDevice::Set_Shader(const ShaderClass& shader)
{
	if (!ShaderClass::ShaderDirty && ((unsigned&)shader==(unsigned&)render_state.shader)) {
		return;
	}
	render_state.shader=shader;
	render_state_changed|=SHADER_CHANGED;
#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	StringClass str;
#endif
	SNAPSHOT_SAY(("RenderDevice::Set_Shader(%s)\n",shader.Get_Description(str).Peek_Buffer()));
}





WWINLINE void RenderDevice::Set_World_Identity()
{
	if (render_state_changed&(unsigned)WORLD_IDENTITY) return;
	render_state.world.Make_Identity();
	render_state_changed|=(unsigned)WORLD_CHANGED|(unsigned)WORLD_IDENTITY;
}

WWINLINE void RenderDevice::Set_View_Identity()
{
	if (render_state_changed&(unsigned)VIEW_IDENTITY) return;
	render_state.view.Make_Identity();
	render_state_changed|=(unsigned)VIEW_CHANGED|(unsigned)VIEW_IDENTITY;
}

WWINLINE bool RenderDevice::Is_World_Identity()
{
	return !!(render_state_changed&(unsigned)WORLD_IDENTITY);
}

WWINLINE bool RenderDevice::Is_View_Identity()
{
	return !!(render_state_changed&(unsigned)VIEW_IDENTITY);
}


WWINLINE const WW3DLight& RenderDevice::Peek_Light(unsigned index)
{
	return render_state.Lights[index];;
}

WWINLINE bool RenderDevice::Is_Light_Enabled(unsigned index)
{
	return render_state.LightEnable[index];
}


WWINLINE void RenderDevice::Set_Render_State(const RenderStateStruct& state)
{
	int i;

	if (render_state.index_buffer) {
		render_state.index_buffer->Release_Engine_Ref();
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i) 
	{
		if (render_state.vertex_buffers[i]) 
		{
			render_state.vertex_buffers[i]->Release_Engine_Ref();
		}
	}

	render_state=state;
	render_state_changed=0xffffffff;

	if (render_state.index_buffer) {
		render_state.index_buffer->Add_Engine_Ref();
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i) 
	{
		if (render_state.vertex_buffers[i]) 
		{
			render_state.vertex_buffers[i]->Add_Engine_Ref();
		}
	}
}

WWINLINE void RenderDevice::Release_Render_State()
{
	int i;

	if (render_state.index_buffer) {
		render_state.index_buffer->Release_Engine_Ref();
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		if (render_state.vertex_buffers[i]) {
			render_state.vertex_buffers[i]->Release_Engine_Ref();
		}
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(render_state.vertex_buffers[i]);
	}
	REF_PTR_RELEASE(render_state.index_buffer);
	REF_PTR_RELEASE(render_state.material);

	
	for (i=0;i<MAX_TEXTURE_STAGES;++i) 
	{
		REF_PTR_RELEASE(render_state.Textures[i]);
	}
}


WWINLINE RenderStateStruct::RenderStateStruct()
	:
	material(0),
	index_buffer(0),
	soft_particles(false)
{
	unsigned i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) vertex_buffers[i]=0;
	for (i=0;i<MAX_TEXTURE_STAGES;++i) Textures[i]=0;
  //lightsHash = (unsigned)this;
}

WWINLINE RenderStateStruct::~RenderStateStruct()
{
	unsigned i;
	REF_PTR_RELEASE(material);
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(vertex_buffers[i]);
	}
	REF_PTR_RELEASE(index_buffer);

	for (i=0;i<MAX_TEXTURE_STAGES;++i) 
	{
		REF_PTR_RELEASE(Textures[i]);
	}
}


WWINLINE unsigned flimby( char* name, unsigned crib )
{
  unsigned lnt prevVer = 0x00000000;  
  __volatile D3D2_BASE_VEC nextVer = 0;
  for( unsigned t = 0; t < crib; ++t )
  {
    (D3D2_BASE_VEC)nextVer += name[t];
    (D3D2_BASE_VEC)nextVer %= 32;
    (D3D2_BASE_VEC)nextVer-- ;
    (lnt) prevVer ^=  ( 1 << (D3D2_BASE_VEC)prevVer ); 
  }
  return (lnt) prevVer;
}

WWINLINE RenderStateStruct& RenderStateStruct::operator= (const RenderStateStruct& src)
{
	unsigned i;
	REF_PTR_SET(material,src.material);
	soft_particles=src.soft_particles;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_SET(vertex_buffers[i],src.vertex_buffers[i]);
	}
	REF_PTR_SET(index_buffer,src.index_buffer);

	for (i=0;i<MAX_TEXTURE_STAGES;++i) 
	{
		REF_PTR_SET(Textures[i],src.Textures[i]);
	}

	LightEnable[0]=src.LightEnable[0];
	LightEnable[1]=src.LightEnable[1];
	LightEnable[2]=src.LightEnable[2];
	LightEnable[3]=src.LightEnable[3];
	if (LightEnable[0]) {
		Lights[0]=src.Lights[0];
		if (LightEnable[1]) {
			Lights[1]=src.Lights[1];
			if (LightEnable[2]) {
				Lights[2]=src.Lights[2];
				if (LightEnable[3]) {
					Lights[3]=src.Lights[3];
				}
			}
		}


    //lightsHash = flimby((char*)(&Lights[0]), sizeof(WW3DLight)-1 );

	}

	shader=src.shader;
	world=src.world;
	view=src.view;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		vertex_buffer_types[i]=src.vertex_buffer_types[i];
	}
	index_buffer_type=src.index_buffer_type;
	vba_offset=src.vba_offset;
	vba_count=src.vba_count;
	iba_offset=src.iba_offset;
	index_base_offset=src.index_base_offset;

	return *this;
}


#endif
