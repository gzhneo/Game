/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// terrainnormal.hlsli -------------------------------------------------------------------------------
//
// Terrain relief: normal maps and gloss of the terrain textures (<texture>_n.dds next to them). Only in the
// TERRAIN_NORMAL permutation of the terrain shaders (terrain*.ps.hlsl, roadnoise2.ps.hlsl); without the macro the
// bytecode is unchanged.
//
// Inputs:
//  - relief atlas (t14): laid out like the terrain atlas, read with the same coordinates and samplers. R, G = normal
//    x, y in image axes (G points up in the image), byte 128 = flat; B = gloss (1 - roughness of the scan); A = how
//    strongly the colour texture carries the fixed light of the tool (slope / 4, constant per texture).
//  - world position from the vertex shader (VS_OUT.fog.yzw) and its screen derivatives: the world directions of the
//    atlas axes per triangle (also on stretched cliff coordinates and rotated or mirrored tiles).
//  - map height field (t11) for the terrain normal and the prelighting light (b12), as in the rock layer.
//
// Light: the relief changes the prelighting linearly (change of N.L per light on the lit side, as a ratio to the
// light of the terrain normal), like the rock layer. The normal maps are mean free per texture, so the mean
// brightness stays the same; the vertex color (point lights, night mood) is kept. Highlights only where the scan is
// glossy, added after the vertex light.
// ------------------------------------------------------------------------------------------------

#ifndef TERRAIN_NORMAL_HLSLI
#define TERRAIN_NORMAL_HLSLI

#include "slots.hlsli"
cbuffer cbTerrainNormal : register(SLOT_CB_TERRAIN_NORMAL)
{
	float4	tn_map;			///< x = 1/width, y = 1/height of the height field (texels), z = border + 0.5 (texels), w = 1/cell
	float4	tn_light;		///< x = relief strength, y = world height per texture value 1, z/w = limits of the light ratio
	float4	tn_gloss;		///< x = gloss where highlights start, y = 1/(full - start), z = highlight strength, w = exponent at full gloss
	float4	tn_baked;		///< xy = baked light direction in image axes / its z, z = share of the baked light removed, w = 0
};

// The rock layer declares the same height field and sampler.
#ifdef TERRAIN_ROCK
#define tNormalHeight tRockHeight
#define sNormalHeight sRockHeight
#else
Texture2D		tNormalHeight	: register(SLOT_T_TERRAIN_HEIGHT);
SamplerState	sNormalHeight	: register(SLOT_S_TERRAIN_HEIGHT);	///< CLAMP, linear
#endif
Texture2D		tRelief			: register(SLOT_T_RELIEF);

float Tn_Height(float2 uv)
{
	return tNormalHeight.SampleLevel(sNormalHeight, uv, 0).r * tn_light.y;
}

/// World directions of the atlas axes on this triangle: tu = +u, tv = image up (-v). Zero where the
/// coordinates do not change (degenerate mapping).
void Tn_Frame(float3 dpx, float3 dpy, float2 uv, out float3 tu, out float3 tv)
{
	const float2 dux = ddx(uv);
	const float2 duy = ddy(uv);
	const float det = dux.x * duy.y - dux.y * duy.x;
	// dp = T du + B dv solved for T and B; only the directions are needed, so the sign of det suffices
	const float s = det < 0.0 ? -1.0 : 1.0;
	const float3 t = (dpx * duy.y - dpy * dux.y) * s;
	const float3 b = (dpy * dux.x - dpx * duy.x) * s;
	const float lt = length(t);
	const float lb = length(b);
	tu = lt > 1e-12 ? t / lt : float3(0, 0, 0);
	tv = lb > 1e-12 ? -b / lb : float3(0, 0, 0);
}

/// Normal x, y (image axes), gloss and the strength of the light baked into the colour texture of one layer.
float4 Tn_Sample(float2 uv, SamplerState s)
{
	const float4 t = tRelief.Sample(s, uv);
	return float4((t.rg * 255.0 - 128.0) / 127.0, t.b, t.a * 4.0);
}

/// Light ratio (multiplies the color before the vertex light) and highlight (added after it).
/// p = world position, uv0/uv1/a as in the base pass, s0/s1 = the atlas samplers of both layers.
void Terrain_Normal(float3 p, float2 uv0, float2 uv1, float a, SamplerState s0, SamplerState s1,
	out float3 ratio, out float3 highlight)
{
	const float3 dpx = ddx(p);
	const float3 dpy = ddy(p);
	float3 tu0, tv0, tu1, tv1;
	Tn_Frame(dpx, dpy, uv0, tu0, tv0);
	Tn_Frame(dpx, dpy, uv1, tu1, tv1);
	const float4 r0 = Tn_Sample(uv0, s0);
	const float4 r1 = Tn_Sample(uv1, s1);
	// slope of the relief in world axes (tangent plane), scaled
	const float3 stoer = lerp(tu0 * r0.x + tv0 * r0.y, tu1 * r1.x + tv1 * r1.y, a) * tn_light.x;
	const float glanz = lerp(r0.z, r1.z, a);

	// terrain normal from the height field (the vertex normals, interpolated per pixel)
	const float2 uvh = (p.xy * tn_map.w + tn_map.z) * tn_map.xy;
	const float hx = Tn_Height(uvh + float2(tn_map.x, 0)) - Tn_Height(uvh - float2(tn_map.x, 0));
	const float hy = Tn_Height(uvh + float2(0, tn_map.y)) - Tn_Height(uvh - float2(0, tn_map.y));
	const float3 n = normalize(float3(-hx, -hy, 2.0 / tn_map.w));
	const float3 nr = normalize(n + stoer);
	const float3 v = normalize(f_cameraPosWorld.xyz - p);

	const float g = saturate((glanz - tn_gloss.x) * tn_gloss.y);
	const float exponent = lerp(8.0, tn_gloss.w, g);
	float3 licht = f_ambient.rgb;
	float3 aenderung = float3(0, 0, 0);
	highlight = float3(0, 0, 0);
	[unroll] for (int k = 0; k < 3; ++k) {
		const float3 l = f_sunRay[k].xyz;
		const float nl = dot(l, n);
		licht += f_sunRay[k].w * saturate(nl) * f_sunDiffuse[k].rgb;
		aenderung += f_sunRay[k].w * saturate(nl * 4.0) * dot(l, stoer) * f_sunDiffuse[k].rgb;
		const float3 ln = l * rsqrt(max(dot(l, l), 1e-12));	// inactive lights are zero
		const float3 h = normalize(ln + v);
		highlight += f_sunRay[k].w * pow(saturate(dot(nr, h)), exponent) * saturate(dot(nr, ln) * 4.0) * f_sunDiffuse[k].rgb;
	}
	// The colour textures carry a fixed light from the upper left of the image (the scans are delit, the tool shades
	// them, log brightness about slope * term); divide it out so only the light of the map shapes the relief. The
	// exponential keeps the mean within about 1.5 % even on the strongest textures (a linear factor: up to 8 %).
	const float gebacken = lerp(r0.w * dot(r0.xy, tn_baked.xy), r1.w * dot(r1.xy, tn_baked.xy), a) * tn_baked.z;
	ratio = clamp((1.0 + aenderung / max(saturate(licht), 0.05)) * exp(-gebacken), tn_light.z, tn_light.w);
	highlight *= g * tn_gloss.z;
}

#endif // TERRAIN_NORMAL_HLSLI
