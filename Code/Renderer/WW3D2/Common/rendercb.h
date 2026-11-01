/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// rendercb.h --------------------------------------------------------------------------------------
//
// C++ mirror of the constant buffers in Data/Shaders/sm4/common.hlsli. Both sides must match
// field by field; the static_asserts below at least check the sizes and 16-byte alignment.
//
// All matrices are row_major and taken unchanged from _Set_Device_Transform (row vectors);
// the shaders compute mul(v, M) and never transpose.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef RENDERCB_H
#define RENDERCB_H

#include "always.h"
#include "ww3dtypes.h"

// Four stage descriptions because the vertex part needs that many (texgen and texture matrix
// of stages 2/3 for the terrain shaders); the pixel shader combiner chain uses two stages.
// Must match common.hlsli.
#define FFP_MAX_STAGES       4
#define FFP_COMBINER_STAGES  2
#define FFP_MAX_LIGHTS       4

struct FFPFloat4 { float x, y, z, w; };
struct FFPInt4   { int   x, y, z, w; };

struct FFPLightCB
{
	FFPFloat4	position;		///< xyz world position, w = light type (WW3D_LIGHT_*)
	FFPFloat4	direction;
	FFPFloat4	diffuse;
	FFPFloat4	specular;
	FFPFloat4	ambient;
	FFPFloat4	attenuation;	///< x = Range, y/z/w = Attenuation0/1/2
	FFPFloat4	spot;			///< x = Falloff, y = cos(Theta/2), z = cos(Phi/2)
};

struct FFPStageCB
{
	FFPInt4		color_op;		///< x = Op, y = Arg1, z = Arg2, w = Arg0
	FFPInt4		alpha_op;
	FFPInt4		texgen;			///< x = texgen mode, y = UV set, z = component count, w = projected
	FFPFloat4	bump;			///< BUMPENVMAT00/01/10/11
	FFPFloat4	bump_scale;		///< x = LSCALE, y = LOFFSET, z = component count of the UV set (0 = missing), w = texture bound
};

struct FFPFrameCB
{
	WW3DMatrixRaw	view;
	WW3DMatrixRaw	proj;
	WW3DMatrixRaw	viewProj;
	FFPFloat4		fogColor;
	FFPFloat4		fogParams;		///< x = start, y = end, z = 1 if fog is on
	FFPFloat4		cameraPosWorld;
	FFPFloat4		ambient;		///< rgb terrain ambient light
	FFPFloat4		sunRay[3];		///< xyz direction to the light (not normalized), w 1 = active
	FFPFloat4		sunDiffuse[3];	///< rgb light color
	FFPFloat4		viewport;		///< x, y, width, height of the current viewport
	FFPFloat4		depthRange;		///< MinZ, MaxZ, 1/(MaxZ-MinZ)
	FFPFloat4		hdr;			///< x = factor for additively blended draws with RENDER_EFFECT_HDR
};

struct FFPObjectCB
{
	WW3DMatrixRaw	world;
	WW3DMatrixRaw	worldView;
	WW3DMatrixRaw	worldViewProj;
	WW3DMatrixRaw	worldInvTranspose;

	FFPFloat4		matDiffuse;
	FFPFloat4		matAmbient;
	FFPFloat4		matSpecular;
	FFPFloat4		matEmissive;
	FFPFloat4		matPower;
	FFPFloat4		globalAmbient;
	FFPFloat4		tfactor;
	FFPFloat4		alphaRef;

	FFPInt4			lightFlags;		///< x = bit mask, y = lighting on, z = specular on, w = LocalViewer
	FFPInt4			colorSource;	///< Diffuse/Ambient/Emissive/Specular (WW3D_MCS_*)
	FFPInt4			alphaTest;		///< x = on, y = compare function
	FFPInt4			flags;			///< x = NormalizeNormals, y = ColorVertex, z = 1: Scene_Out rounds

	FFPLightCB		lights[FFP_MAX_LIGHTS];
	FFPStageCB		stages[FFP_MAX_STAGES];
	WW3DMatrixRaw	texMatrix[FFP_MAX_STAGES];
	FFPFloat4		teamColor;		///< x = hue in degrees (-1: grey), y = saturation, z = stages with a team colour view (bit mask), w = 1: normal map in t14 (model lit per pixel)
};

/// Constant registers set via Set_*_Shader_Constant; the individual shaders read from them.
enum { FFP_PIECE_VS_REGISTERS = 96, FFP_PIECE_PS_REGISTERS = 32 };
struct FFPPieceVSCB { FFPFloat4 c[FFP_PIECE_VS_REGISTERS]; };
struct FFPPiecePSCB { FFPFloat4 c[FFP_PIECE_PS_REGISTERS]; };

/// cbEffects (b4) from Data/Shaders/sm4/effects.hlsli: post processing, ambient occlusion and
/// soft particles. See there for field meanings.
struct FFPEffectsCB
{
	FFPFloat4	source;			///< x = 1/width, y = 1/height, z = width, w = height
	FFPFloat4	rect;			///< x0, y0, x1, y1 in pixels
	FFPFloat4	projA;			///< P11, P22, P31, P32 for the depth copy
	FFPFloat4	projB;			///< P33, P34, P43, P44 for the depth copy
	FFPFloat4	depthRange;		///< MinZ, MaxZ, 1/(MaxZ-MinZ) for the depth copy
	FFPFloat4	ssao;			///< radius, strength, bias, range
	FFPFloat4	post;			///< CAS sharpness, saturation, contrast, warmth
	FFPFloat4	soft;			///< 1/fade distance, blend mode
	FFPFloat4	softProjB;		///< P33, P34, P43, P44 of the particle draw
	FFPFloat4	softRange;		///< MinZ, MaxZ, 1/(MaxZ-MinZ) of the particle draw
	FFPFloat4	gamma;			///< 1/gamma, brightness, contrast, lower limit
	FFPFloat4	bloom;			///< xy = 1/size of the source texture, z = threshold, w = knee
	FFPFloat4	glow;			///< x = bloom strength, y = band-pass weight, z = heat amplitude (pixels), w = heat phase
	FFPFloat4	haze;			///< x = strength, y = start distance, z = 1/(end - start), w = 1: float scene target
	FFPFloat4	tone;			///< tone curve: x = knee, y = exposure
};

/// cbDynLights (b8) from Data/Shaders/sm4/post.ps.hlsl (DO_LIGHTS): point lights in view space.
enum { FFP_DYN_LIGHTS = 32 };
struct FFPDynLightsCB
{
	FFPFloat4	info;					///< x = count, y = strength
	FFPFloat4	pos[FFP_DYN_LIGHTS];	///< xyz view space position, w = radius
	FFPFloat4	color[FFP_DYN_LIGHTS];	///< rgb color times intensity
};

/// cbShadowDraw (b5) from Data/Shaders/sm4/shadow.hlsli -- per shadow caster while drawing the
/// shadow map.
enum { FFP_SHADOW_SWAY = 11 };
struct FFPShadowDrawCB
{
	WW3DMatrixRaw	worldLight;		///< object space -> light clip
	WW3DMatrixRaw	texMatrix;		///< texture matrix of stage 0
	FFPFloat4		uv;				///< x = texture matrix on, y = component count of the UV set
	FFPFloat4		alpha;			///< x = reference 0..255, y = compare function
	FFPFloat4		sway[FFP_SHADOW_SWAY];	///< trees: RenderTreesVS::sway
};

/// cbShadowApply (b5) from Data/Shaders/sm4/shadowmask.ps.hlsl -- applies shadows to the image.
struct FFPShadowApplyCB
{
	WW3DMatrixRaw	viewToLight;	///< view space -> light clip
	FFPFloat4		sunView;		///< xyz direction to the sun (view space), w normal offset (world)
	FFPFloat4		color;			///< rgb factor in full shadow, w depth bias
	FFPFloat4		map;			///< x 1/edge length, y PCF spacing (texels), z edge fade (uv), w N.L for full self-shadow
	FFPFloat4		fade;			///< x range, y 1/fade distance
	/// x share of terrain shadows on the terrain, y 1 = terrain separate (t3/t4 bound),
	/// z/w tolerance of the terrain test (world, fraction of distance)
	FFPFloat4		terrain;
	FFPFloat4		soft;			///< x penumbra texels per light depth unit, y/z min/max penumbra (texels), w 1 = on
	FFPFloat4		contact;		///< x ray length, y thickness (world), z depth bias (share of distance), w strength
	FFPFloat4		cascade;		///< near cascade: x scale, y/z offset (light clip of the far map to the near one), w 1 = on
	FFPFloat4		worldZ;			///< atmosphere: world height = (view position, 1) . worldZ
	FFPFloat4		atmo;			///< atmosphere: x fog density at the base, y 1/scale height, z base height, w shaft strength
	FFPFloat4		fogColor;		///< atmosphere: rgb fog color, w largest fog share
	FFPFloat4		sunColor;		///< atmosphere: rgb sun light, w Henyey-Greenstein g
};

/// cbTerrainDetail (b6) from Data/Shaders/sm4/terraindetail.hlsli.
enum { FFP_TERRAIN_DETAIL_CELLS = 1024 };
struct FFPTerrainDetailCB
{
	FFPFloat4		atlas;			///< x atlas width, y atlas height (pixels, logical), z 1/grid cell, w 1/tile
	FFPFloat4		fade;			///< x pixels per tile where fade-in starts, y 1/(full - x), z strength, w share of second layer
	FFPFloat4		strengthA;		///< strength per material 1..4
	FFPFloat4		strengthB;		///< strength per material 5..8
	FFPFloat4		layer2;			///< x repeats of the second layer per tile, y 1/sqrt(1 + w^2)
	FFPInt4			grid;			///< x columns, y rows
	FFPInt4			cells[FFP_TERRAIN_DETAIL_CELLS / 4];	///< Material | OffsetU << 8 | OffsetV << 16
};

/// cbTerrainRock (b7) from Data/Shaders/sm4/terrainrock.hlsli.
struct FFPTerrainRockCB
{
	FFPFloat4		map;			///< x 1/width, y 1/height of the height field, z border + 0.5 (texels), w 1/cell
	FFPFloat4		slope;			///< x start on rock ground, y start elsewhere (1 - n.z), z 1/transition width, w share of rock height
	FFPFloat4		scale;			///< x 1/coarse period, y 1/fine period, z mip level of the EA color, w strength
	FFPFloat4		light;			///< x rock normal strength, y world height per texture value 1, z/w light ratio limits
	FFPFloat4		tint;			///< x share of the coarse EA color versus the class color, y start on snow, z start on sand / dry grass, w largest rock share on EA rock ground
	FFPFloat4		extra;			///< x contrast gain of the rock structure, y normalization of its mean, z/w flat snow: full below, none above this slope
	FFPFloat4		snow;			///< rgb mean color of the snow ground of the map, a share of snow on flat ground (0: off)
	FFPInt4			colors[FFP_TERRAIN_DETAIL_CELLS / 4];	///< average class color per grid cell (0xAARRGGBB)
};

/// cbTerrainNormal (b12) from Data/Shaders/sm4/terrainnormal.hlsli.
struct FFPTerrainNormalCB
{
	FFPFloat4		map;			///< x 1/width, y 1/height of the height field, z border + 0.5 (texels), w 1/cell
	FFPFloat4		light;			///< x relief strength, y world height per texture value 1, z/w light ratio limits
	FFPFloat4		gloss;			///< x gloss where highlights start, y 1/(full - start), z highlight strength, w exponent at full gloss
	FFPFloat4		baked;			///< xy baked light direction in image axes / its z, z share of the baked light removed
};

/// cbTerrainTess (b13, hull and domain stage) from Data/Shaders/sm4/terraintess.hlsli.
enum { FFP_TERRAIN_TESS_CLASSES = 256 };
struct FFPTerrainTessCB
{
	FFPFloat4		view;			///< xy half viewport size (pixels), z 1/target pixels per edge piece, w largest factor
	FFPFloat4		fade;			///< x pixels per tile at distance 1, y fade start, z 1/(full - start), w amplitude
	FFPFloat4		map;			///< x 1/cell, y border (cells), z 1/grid width, w 1/grid height
	FFPFloat4		shape;			///< x world height per height field value 1, y cull margin, z slope end, w 1/slope fade
	FFPFloat4		atlas;			///< x u, y v of one cell in the height atlas, z 1 = crack probe, w 1 = ground heights
	FFPFloat4		rock;			///< x rock amplitude (0 = off), y slope where it starts, z 1/fade width, w height share at the edge
	FFPFloat4		rock_scale;		///< x 1/coarse period, y 1/fine period, z fade start, w 1/(full - start)
	FFPFloat4		rock_lod;		///< x mip bias, y smallest point spacing, z spacing per unit of camera distance
	FFPFloat4		classes[FFP_TERRAIN_TESS_CLASSES];	///< u origin, v upper edge, cells per class edge, 1 = has heights
};

/// cbTerrainLights (b8, vertex stage) from Data/Shaders/sm4/terrainlights.hlsli.
enum { FFP_TERRAIN_LIGHTS = 20, FFP_TERRAIN_SCENE_LIGHTS = 16 };
struct FFPTerrainLightsCB
{
	FFPFloat4		map;			///< x 1/cell, y border (cells), z world height per texel value 1, w light count
	FFPInt4			size;			///< x width, y height of the height field
	FFPFloat4		position[FFP_TERRAIN_LIGHTS];	///< xyz position (directional: direction to the light), w 1 = directional
	FFPFloat4		range[FFP_TERRAIN_LIGHTS];		///< x full strength up to, y no light from
	FFPFloat4		diffuse[FFP_TERRAIN_LIGHTS];
	FFPFloat4		ambient[FFP_TERRAIN_LIGHTS];
	FFPFloat4		stat;			///< x 1 = static prelighting in the shader, y global light count, z scene light count
	FFPFloat4		stat_ambient;
	FFPFloat4		stat_ray[3];	///< direction to each global light (not normalized)
	FFPFloat4		stat_diffuse[3];
	FFPFloat4		scene_position[FFP_TERRAIN_SCENE_LIGHTS];	///< as position[] of the dynamic lights
	FFPFloat4		scene_range[FFP_TERRAIN_SCENE_LIGHTS];
	FFPFloat4		scene_diffuse[FFP_TERRAIN_SCENE_LIGHTS];
	FFPFloat4		scene_ambient[FFP_TERRAIN_SCENE_LIGHTS];
};

/// cbSkin (b9, vertex stage) from Data/Shaders/sm4/skin.hlsli.
enum { FFP_SKIN_BONES = 64 };
struct FFPSkinCB
{
	FFPFloat4		info;			///< x bone count (0 = no skinning)
	FFPFloat4		bones[FFP_SKIN_BONES * 3];	///< 3x4 world matrix per bone (Matrix3D rows)
};

/// cbParticle (b10) and cbParticleTable (b11) of the vertex stage from Data/Shaders/sm4/particles.vs.hlsl.
struct FFPParticleCB
{
	FFPFloat4		flags;			///< x 1 = billboard, y 1 = lit, z 1 = keep billboards above the terrain
	FFPFloat4		uv01;
	FFPFloat4		uv23;
	FFPFloat4		light;			///< xyz light of the time of day relative to the afternoon, w strength
};
struct FFPParticleTableCB
{
	FFPFloat4		corner[512];	///< quad corners per orientation, two per register
	FFPFloat4		ground[256];	///< xy X vector, zw Y vector
};

// --- Layout checks against common.hlsli ---------------------------------------------------------
// HLSL packs constant buffers in 16-byte registers; all fields here are multiples of that.
#if defined(__cplusplus) && (__cplusplus >= 201103L || defined(_MSC_VER))
static_assert(sizeof(FFPFloat4)   == 16,  "FFPFloat4 must be one register");
static_assert(sizeof(FFPInt4)     == 16,  "FFPInt4 must be one register");
static_assert(sizeof(WW3DMatrixRaw) == 64, "matrices take four registers");
static_assert(sizeof(FFPLightCB)  == 7 * 16,  "FFPLightCB: seven registers");
static_assert(sizeof(FFPStageCB)  == 5 * 16,  "FFPStageCB: five registers");
static_assert(sizeof(FFPFrameCB)  == 3 * 64 + 13 * 16, "cbFramelayout differs from common.hlsli");
static_assert(sizeof(FFPObjectCB) == 4 * 64 + 8 * 16 + 4 * 16
									 + FFP_MAX_LIGHTS * sizeof(FFPLightCB)
									 + FFP_MAX_STAGES * sizeof(FFPStageCB)
									 + FFP_MAX_STAGES * 64 + 16, "cbObjectlayout differs from common.hlsli");
static_assert((sizeof(FFPFrameCB)  % 16) == 0, "cbFrame must be 16-byte aligned");
static_assert((sizeof(FFPObjectCB) % 16) == 0, "cbObject must be 16-byte aligned");
static_assert(sizeof(FFPEffectsCB) == 15 * 16, "cbEffectslayout differs from effects.hlsli");
static_assert(sizeof(FFPDynLightsCB) == (1 + 2 * FFP_DYN_LIGHTS) * 16, "cbDynLights differs from post.ps.hlsl");
static_assert(sizeof(FFPShadowDrawCB) == 2 * 64 + (2 + FFP_SHADOW_SWAY) * 16, "cbShadowDraw differs from shadow.hlsli");
static_assert(sizeof(FFPShadowApplyCB) == 64 + 12 * 16, "cbShadowApply differs from shadowmask.ps.hlsl");
static_assert(sizeof(FFPTerrainDetailCB) == 6 * 16 + FFP_TERRAIN_DETAIL_CELLS * 4, "cbTerrainDetail differs from terraindetail.hlsli");
static_assert(sizeof(FFPTerrainRockCB) == 7 * 16 + FFP_TERRAIN_DETAIL_CELLS * 4, "cbTerrainRock differs from terrainrock.hlsli");
static_assert(sizeof(FFPTerrainNormalCB) == 4 * 16, "cbTerrainNormal differs from terrainnormal.hlsli");
static_assert(sizeof(FFPTerrainTessCB) == (8 + FFP_TERRAIN_TESS_CLASSES) * 16, "cbTerrainTess differs from terraintess.hlsli");
static_assert(sizeof(FFPParticleCB) == 4 * 16, "cbParticle differs from particles.vs.hlsl");
static_assert(sizeof(FFPParticleTableCB) == 768 * 16, "cbParticleTable differs from particles.vs.hlsl");
static_assert(sizeof(FFPSkinCB) == 16 + FFP_SKIN_BONES * 3 * 16, "cbSkin differs from skin.hlsli");
static_assert(sizeof(FFPTerrainLightsCB) == 10 * 16 + 4 * (FFP_TERRAIN_LIGHTS + FFP_TERRAIN_SCENE_LIGHTS) * 16, "cbTerrainLights differs from terrainlights.hlsli");
#endif

#endif // RENDERCB_H
