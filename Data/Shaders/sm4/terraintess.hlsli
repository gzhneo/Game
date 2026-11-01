/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// terraintess.hlsli --------------------------------------------------------------------------------
//
// Terrain tessellation with displacement, shared by terraintess.hs.hlsl and terraintess.ds.hlsl (shader model 5).
// The terrain draws become patches of three control points (the triangles of the index buffer); the control
// points are the outputs of ffp.vs.hlsl, compiled as vs_5_0 with TERRAIN_TESS. Only draws between
// RenderDevice::Set_Terrain_Tessellation(true/false) in the scene target (base pass, three-way blends, shroud).
//
// No cracks: an edge factor depends only on the two end points of the edge and is computed symmetrically; the
// domain shader interpolates edge points from the two end points in an order fixed by their world positions;
// the displacement is a function of the world position alone (height of the terrain textures placed in the world
// like the base pass places their tiles, weights per grid point, mask per grid point, height field normal; on
// steep slopes the triplanar height of the rock textures of the rock layer).
// The hull shader rotates each patch so its first control point is the one with the smallest world position:
// the base pass, the three-way blends and the shroud pass (depth test EQUAL) tessellate a triangle bit for bit
// the same way.
// ------------------------------------------------------------------------------------------------

#ifndef TERRAIN_TESS_HLSLI
#define TERRAIN_TESS_HLSLI

#include "common.hlsli"

#define TT_CLASSES 256

#include "slots.hlsli"
cbuffer cbTerrainTess : register(SLOT_CB_TERRAIN_TESS)
{
	float4	tt_view;		///< x, y = half viewport size (pixels), z = 1 / target pixels per edge piece, w = largest factor
	float4	tt_fade;		///< x = pixels per tile at distance 1, y = fade start (pixels per tile), z = 1 / (full - start),
							///< w = amplitude (world units per height unit)
	float4	tt_map;			///< x = 1 / cell, y = border (cells), z = 1 / grid width, w = 1 / grid height
	float4	tt_shape;		///< x = world height per height field value 1, y = cull margin (world), z = slope (1 - n.z)
							///< where the displacement ends, w = 1 / width of its fade
	float4	tt_atlas;		///< x = u, y = v of one cell in the height atlas, z = 1 probe colours, w = 1 if the terrain textures have heights
	float4	tt_rock;		///< rock: x = amplitude (world units per rock height unit, 0 = off), y = slope (1 - n.z) where
							///< it starts, z = 1 / width of its fade-in, w = share of the rock height at that edge
	float4	tt_rock_scale;	///< x = 1 / coarse period, y = 1 / fine period (world units), z = fade start (pixels per
							///< tile), w = 1 / (full - start)
	float4	tt_rock_lod;	///< x = mip bias, y = smallest spacing of the domain points (world units),
							///< z = spacing per unit of camera distance, w unused
	float4	tt_class[TT_CLASSES];	///< per texture class: x = u of the class origin, y = v of its upper edge,
									///< z = cells per class edge (twice the width in tiles), w = 1 if it has heights
};

Texture2D		tt_field	: register(t0);	///< map height field (R16_UNORM, one texel per grid point)
SamplerState	tt_clamp	: register(s0);	///< linear, clamp

struct TT_PATCH
{
	float	edges[3]	: SV_TessFactor;
	float	inside		: SV_InsideTessFactor;
};

/// Order of world positions: x first, then y (the three corners of a terrain triangle never share both).
bool Tt_Less(float3 a, float3 b)
{
	return a.x < b.x || (a.x == b.x && a.y < b.y);
}

/// 0 far away, 1 near: screen pixels per terrain tile (20 world units) at world position p.
float Tt_Fade(float3 p)
{
	const float d = max(length(p - f_cameraPosWorld.xyz), 1.0);
	return saturate((tt_fade.x / d - tt_fade.y) * tt_fade.z);
}

/// Like Tt_Fade with the thresholds of the rock displacement (it starts farther away).
float Tt_Rock_Fade(float3 p)
{
	const float d = max(length(p - f_cameraPosWorld.xyz), 1.0);
	return saturate((tt_fade.x / d - tt_rock_scale.z) * tt_rock_scale.w);
}

/// Height field in world units at grid coordinates g (grid points, border included).
float Tt_Field(float2 g)
{
	return tt_field.SampleLevel(tt_clamp, (g + 0.5) * tt_map.zw, 0).r * tt_shape.x;
}

/// Terrain normal at grid coordinates g: central differences over two cells, bilinear (as the vertex normals).
float3 Tt_Normal(float2 g)
{
	const float hx = Tt_Field(g + float2(1.0, 0.0)) - Tt_Field(g - float2(1.0, 0.0));
	const float hy = Tt_Field(g + float2(0.0, 1.0)) - Tt_Field(g - float2(0.0, 1.0));
	return normalize(float3(-hx, -hy, 2.0 / tt_map.x));
}

/// Largest rock share the slope of normal n allows (the rock height can only raise it by half its edge share).
float Tt_Rock_Possible(float3 n)
{
	return saturate((1.0 - n.z - tt_rock.y) * tt_rock.z + 0.5 * tt_rock.w);
}

#endif // TERRAIN_TESS_HLSLI
