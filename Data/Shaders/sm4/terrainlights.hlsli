/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// terrainlights.hlsli ------------------------------------------------------------------------------
//
// Terrain lighting per vertex in ffp.vs.hlsl, for terrain tiles only:
//
//  - Static prelighting (tl_static.x > 0), as BaseHeightMapRenderObjClass::doTheLight on the CPU:
//       color = ambient + sum over map lights (light model below) + sum(saturate(N.ray) * diffuse)
//    clamped and cut to 8 bits. The tile vertex colors are white then and only scale the result.
//  - Dynamic lights (explosions, muzzle flashes, ...), as HeightMapRenderObjClass::doTheDynamicLight:
//       color = prelit color + sum(saturate(N.L * factor) * diffuse + factor * ambient)
//       factor = saturate(1 - (dist - mid) / (range - mid)), 0 from range on; directional lights: 1
//    clamped and cut to 8 bits like the vertex color.
//
// The normal is the one of updateVB: central differences over two cells of the map height field (t11 of
// the vertex stage, R16_UNORM, one texel per grid point, byte / 255).
//
// terrainpixellight.hlsli evaluates the same sums per pixel; it names the height field of the pixel stage in
// TL_HEIGHT before including this file.
//
// Every other draw gets a buffer of zeros (RenderCore::Enable_Terrain_Dynamic_Lights).
// ------------------------------------------------------------------------------------------------

#ifndef TERRAIN_LIGHTS_HLSLI
#define TERRAIN_LIGHTS_HLSLI

#define TERRAIN_LIGHTS 20
#define TERRAIN_SCENE_LIGHTS 16

#include "slots.hlsli"
cbuffer cbTerrainLights : register(SLOT_CB_TERRAIN_LIGHTS)
{
	float4	tl_map;						///< x = 1/cell, y = border (cells), z = world height per texel value 1, w = light count
	int4	tl_size;					///< x = width, y = height of the height field
	float4	tl_position[TERRAIN_LIGHTS];	///< xyz = position (directional: direction to the light), w = 1 directional
	float4	tl_range[TERRAIN_LIGHTS];	///< x = full strength up to, y = no light from
	float4	tl_diffuse[TERRAIN_LIGHTS];
	float4	tl_ambient[TERRAIN_LIGHTS];
	float4	tl_static;					///< x = 1 static prelighting here, y = global light count, z = map light count
	float4	tl_static_ambient;
	float4	tl_static_ray[3];			///< direction to each global light (not normalized)
	float4	tl_static_diffuse[3];
	float4	tl_scene_position[TERRAIN_SCENE_LIGHTS];	///< map lights, as tl_position
	float4	tl_scene_range[TERRAIN_SCENE_LIGHTS];
	float4	tl_scene_diffuse[TERRAIN_SCENE_LIGHTS];
	float4	tl_scene_ambient[TERRAIN_SCENE_LIGHTS];
};

#ifndef TL_HEIGHT
Texture2D	tTerrainHeight : register(SLOT_T_TERRAIN_HEIGHT);
#define TL_HEIGHT tTerrainHeight
#endif

float Tl_Height(int2 g)
{
	g = clamp(g, int2(0, 0), tl_size.xy - int2(1, 1));
	return TL_HEIGHT.Load(int3(g, 0)).r * tl_map.z;
}

/// Terrain normal at the grid point of world position p.
float3 Tl_Normal(float3 p)
{
	const int2 g = int2(round(p.xy * tl_map.x)) + int2((int)tl_map.y, (int)tl_map.y);
	const float dx = Tl_Height(g + int2(1, 0)) - Tl_Height(g - int2(1, 0));
	const float dy = Tl_Height(g + int2(0, 1)) - Tl_Height(g - int2(0, 1));
	return normalize(float3(-dx, -dy, 2.0 / tl_map.x));
}

/// Light factor and direction to the light at p; false outside the range.
bool Tl_Factor(float3 p, float4 position, float4 range, out float3 to_light, out float factor)
{
	to_light = position.xyz;
	factor = 1.0;
	if (position.w == 0.0) {
		const float3 d = p - position.xyz;
		const float dist = length(d);
		if (dist >= range.y || dist <= 0.0) return false;
		factor = saturate(1.0 - (dist - range.x) / (range.y - range.x));
		to_light = -d / dist;
	}
	return true;
}

/// 8 bits per channel as the vertex color; the small offset keeps k/255 from dropping to k-1.
float3 Tl_Quantize(float3 c)
{
	return floor(saturate(c) * 255.0 + 1e-3) / 255.0;
}

/// Static prelighting at world position p with normal n, not clamped.
float3 Tl_Static_Sum(float3 p, float3 n)
{
	float3 sum = tl_static_ambient.rgb;
	const int scene = (int)tl_static.z;
	[loop] for (int k = 0; k < scene; ++k) {
		float3 to_light;
		float factor;
		if (!Tl_Factor(p, tl_scene_position[k], tl_scene_range[k], to_light, factor)) continue;
		sum += saturate(dot(to_light, n) * factor) * tl_scene_diffuse[k].rgb + factor * tl_scene_ambient[k].rgb;
	}
	const int global = (int)tl_static.y;
	[loop] for (int g = 0; g < global; ++g)
		sum += saturate(dot(tl_static_ray[g].xyz, n)) * tl_static_diffuse[g].rgb;
	return sum;
}

/// Static prelighting at world position p.
float3 Terrain_Static_Light(float3 p)
{
	return Tl_Quantize(Tl_Static_Sum(p, Tl_Normal(p)));
}

/// Prelit vertex color plus the dynamic lights at world position p.
float3 Terrain_Dynamic_Lights(float3 p, float3 color)
{
	float3 sum = float3(0, 0, 0);
	float3 n = float3(0, 0, 1);
	bool lit = false;
	const int count = (int)tl_map.w;
	[loop] for (int k = 0; k < count; ++k) {
		float3 to_light;
		float factor;
		if (!Tl_Factor(p, tl_position[k], tl_range[k], to_light, factor)) continue;
		if (!lit) {
			n = Tl_Normal(p);
			lit = true;
		}
		const float shade = saturate(dot(to_light, n) * factor);
		sum += shade * tl_diffuse[k].rgb + factor * tl_ambient[k].rgb;
	}
	if (!lit) return color;
	return Tl_Quantize(color + sum);
}

#endif // TERRAIN_LIGHTS_HLSLI
