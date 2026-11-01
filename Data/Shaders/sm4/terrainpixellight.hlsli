/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// terrainpixellight.hlsli --------------------------------------------------------------------------
//
// Terrain light per pixel, permutation TERRAIN_LIGHT of the terrain pixel shaders: the static prelighting and the
// dynamic lights of terrainlights.hlsli, evaluated with the terrain normal at the pixel instead of at the grid
// points. The normal is the bilinear interpolation of the grid point normals (central differences over two cells,
// sampled like the relief and the rock layer), so at the grid points the light equals the vertex light.
//
// The vertex shader gets the buffer of zeros for these draws (RenderCore::Tb_Bind): the vertex color carries
// only the tile color (white) and the blend weight in alpha. The 8-bit cut of the vertex color is left out.
// ------------------------------------------------------------------------------------------------

#ifndef TERRAIN_PIXEL_LIGHT_HLSLI
#define TERRAIN_PIXEL_LIGHT_HLSLI

// The rock layer and the relief declare the height field and its sampler already.
#if defined(TERRAIN_ROCK)
#define TL_HEIGHT tRockHeight
#define TL_HEIGHT_SAMPLER sRockHeight
#elif defined(TERRAIN_NORMAL)
#define TL_HEIGHT tNormalHeight
#define TL_HEIGHT_SAMPLER sNormalHeight
#endif

#include "terrainlights.hlsli"

#ifndef TL_HEIGHT_SAMPLER
SamplerState	sLightHeight	: register(SLOT_S_TERRAIN_HEIGHT);	///< CLAMP, linear
#define TL_HEIGHT_SAMPLER sLightHeight
#endif

float Tp_Height(float2 uv)
{
	return TL_HEIGHT.SampleLevel(TL_HEIGHT_SAMPLER, uv, 0).r * tl_map.z;
}

/// Terrain normal at world position p, interpolated between the grid points.
float3 Tp_Normal(float3 p)
{
	const float2 texel = 1.0 / float2(tl_size.xy);
	const float2 uv = (p.xy * tl_map.x + tl_map.y + 0.5) * texel;
	const float hx = Tp_Height(uv + float2(texel.x, 0)) - Tp_Height(uv - float2(texel.x, 0));
	const float hy = Tp_Height(uv + float2(0, texel.y)) - Tp_Height(uv - float2(0, texel.y));
	return normalize(float3(-hx, -hy, 2.0 / tl_map.x));
}

/// Light of the terrain at world position p: static prelighting plus the dynamic lights, clamped like the vertex
/// color (prelighting first, then the sum).
float3 Terrain_Pixel_Light(float3 p)
{
	const float3 n = Tp_Normal(p);
	float3 sum = saturate(Tl_Static_Sum(p, n));
	const int count = (int)tl_map.w;
	[loop] for (int k = 0; k < count; ++k) {
		float3 to_light;
		float factor;
		if (!Tl_Factor(p, tl_position[k], tl_range[k], to_light, factor)) continue;
		sum += saturate(dot(to_light, n) * factor) * tl_diffuse[k].rgb + factor * tl_ambient[k].rgb;
	}
	return saturate(sum);
}

#endif // TERRAIN_PIXEL_LIGHT_HLSLI
