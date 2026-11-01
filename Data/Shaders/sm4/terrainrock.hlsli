/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// terrainrock.hlsli ---------------------------------------------------------------------------------
//
// Mountains and cliffs: on steep slopes a triplanar rock structure from photographic CC0 scans replaces the
// stretched EA texture, lit per pixel with the rock normals. Only in the TERRAIN_ROCK permutation of the
// terrain shaders (terrain*.ps.hlsl, roadnoise2.ps.hlsl); without the macro the bytecode is unchanged.
//
// Inputs:
//  - world position from the vertex shader (ffp.vs.hlsl, VS_OUT.fog.yzw).
//  - map height field (t11, R16_UNORM, one texel per grid point): per pixel the normal as in the game
//    (central differences over two cells, HeightMapRenderObjClass::updateVB) -- bilinear, i.e. the vertex
//    normals interpolated per pixel.
//  - rock textures (t12 coarse, t13 fine): R brightness factor (mean 1), GB normal, A height.
//  - terrain atlas (stage 0/1): its color at a coarse mip level (the stretched EA structure disappears, color
//    and mean brightness stay).
//
// Computation:
//    rock  = EA color(coarse) * factor_coarse * factor_fine * light(rock normal) / light(terrain normal)
//    share by slope (1 - n.z) and rock height (irregular edge), starting earlier where EA already paints rock
//    (ground detail material 6 from cbTerrainDetail).
// Flat snow (Terrain_Snow, only with tr_snow.w > 0): on grass, dry grass, earth and gravel ground of maps that have snow
// ground, flat places take the mean class color of the snow, the edge broken by the fine rock height.
// The light is the prelighting (doTheLight: ambient + up to three directional lights, clamped); applied as a
// ratio, point lights, water depth and night mood from the vertex color are preserved.
// ------------------------------------------------------------------------------------------------

#ifndef TERRAIN_ROCK_HLSLI
#define TERRAIN_ROCK_HLSLI

// Td_Material: ground type from the atlas coordinate (cbTerrainDetail, b6)
#include "terraindetail.hlsli"

#include "slots.hlsli"
cbuffer cbTerrainRock : register(SLOT_CB_TERRAIN_ROCK)
{
	float4	tr_map;			///< x = 1/width, y = 1/height of the height field (texels), z = border + 0.5 (texels), w = 1/cell
	float4	tr_slope;		///< x = start (1 - n.z) on rock ground, y = start elsewhere, z = 1/transition width, w = rock height share
	float4	tr_scale;		///< x = 1/coarse period, y = 1/fine period, z = mip level of the EA color (logical), w = strength
	float4	tr_light;		///< x = rock normal strength, y = world height per texture value 1, z/w = limits of the light ratio
	float4	tr_tint;		///< x = share of the coarse EA color versus the class color, y = start on snow (1 - n.z), z = start on sand / dry grass, w = largest rock share on EA rock ground
	float4	tr_extra;		///< x = contrast gain of the rock structure (exponent), y = normalization of its mean, z/w = flat snow: full below, none above this slope
	float4	tr_snow;		///< rgb = mean color of the snow ground of the map, a = share of snow on flat ground (0: off)
	uint4	tr_colors[256];	///< per atlas grid cell the mean color of the class (0xAARRGGBB, A = 0: none)
};

Texture2D		tRockHeight	: register(SLOT_T_TERRAIN_HEIGHT);
Texture2D		tRockA		: register(SLOT_T_ROCK_A);
Texture2D		tRockB		: register(SLOT_T_ROCK_B);
SamplerState	sRock		: register(SLOT_S_ROCK);	///< WRAP, anisotropic like the terrain atlas
SamplerState	sRockHeight	: register(SLOT_S_TERRAIN_HEIGHT);	///< CLAMP, linear

/// Prelighting light for a normal (BaseHeightMapRenderObjClass::doTheLight without point lights).
float3 Tr_Light(float3 n)
{
	float3 l = f_ambient.rgb;
	[unroll] for (int k = 0; k < 3; ++k)
		l += f_sunRay[k].w * saturate(dot(f_sunRay[k].xyz, n)) * f_sunDiffuse[k].rgb;
	return saturate(l);
}

/// Start of the rock (1 - n.z) for a detail material of the ground.
float Tr_Start(uint m)
{
	return m == 6 ? tr_slope.x : (m == 7 ? tr_tint.y : ((m == 2 || m == 5) ? tr_tint.z : tr_slope.y));
}

/// Mean color of the terrain class under atlas coordinate uv (a = 0: unknown).
float4 Tr_Class_Color(float2 uv)
{
	float2 px = uv * td_atlas.xy;
	int2 feld = clamp(int2(floor(px * td_atlas.z)), int2(0, 0), int2(td_grid.xy) - int2(1, 1));
	uint i = (uint)feld.y * td_grid.x + (uint)feld.x;
	uint e = tr_colors[i >> 2][i & 3];
	return float4((e >> 16) & 0xFF, (e >> 8) & 0xFF, e & 0xFF, e >> 24) / 255.0;
}

float Tr_Height(float2 uv)
{
	return tRockHeight.SampleLevel(sRockHeight, uv, 0).r * tr_light.y;
}

/// Per-pixel terrain normal from the height field at world position p.
float3 Tr_Normal(float3 p)
{
	float2 uvh = (p.xy * tr_map.w + tr_map.z) * tr_map.xy;
	float hx = Tr_Height(uvh + float2(tr_map.x, 0)) - Tr_Height(uvh - float2(tr_map.x, 0));
	float hy = Tr_Height(uvh + float2(0, tr_map.y)) - Tr_Height(uvh - float2(0, tr_map.y));
	return normalize(float3(-hx, -hy, 2.0 / tr_map.w));
}

/// One projection: factor (x), tangent slope (yz, along the two axes of the plane), rock height (w).
/// c = coordinates in the plane (second axis = image up), dcx/dcy = their screen derivatives.
float4 Tr_Project(float2 c, float2 dcx, float2 dcy)
{
	const float SKALA = 255.0 / 128.0;
	const float2x2 DREH = float2x2(0.8, -0.6, 0.6, 0.8);	// fine layer rotated against visible repetition
	float2 ua = float2(c.x, -c.y) * tr_scale.x;
	float4 a = tRockA.SampleGrad(sRock, ua, float2(dcx.x, -dcx.y) * tr_scale.x, float2(dcy.x, -dcy.y) * tr_scale.x);
	float2 cr = mul(DREH, c);
	float2 dxr = mul(DREH, dcx), dyr = mul(DREH, dcy);
	float2 ub = float2(cr.x, -cr.y) * tr_scale.y + float2(0.37, 0.61);
	float4 b = tRockB.SampleGrad(sRock, ub, float2(dxr.x, -dxr.y) * tr_scale.y, float2(dyr.x, -dyr.y) * tr_scale.y);
	float2 na = a.gb * (255.0 / 127.5) - 1.0;
	float2 nb = mul(transpose(DREH), b.gb * (255.0 / 127.5) - 1.0);
	return float4((a.r * SKALA) * (b.r * SKALA), na + nb, (2.0 * a.a + b.a) / 3.0);
}

/// Coarse EA color (rgb) and rock share (a); fels = rock structure times light ratio (mean about 1).
/// p = world position, uv0/uv1/a as in the base pass.
float4 Terrain_Rock(float3 p, float2 uv0, float2 uv1, float a, Texture2D atlas0, SamplerState s0, Texture2D atlas1, SamplerState s1,
	out float3 fels)
{
	fels = float3(1, 1, 1);
	// derivatives outside the branch
	float3 dpx = ddx(p);
	float3 dpy = ddy(p);
	float3 fn = cross(dpx, dpy);
	float fz = abs(fn.z) / max(length(fn), 1e-6);
	float anfang = min(min(tr_slope.x, tr_slope.y), min(tr_tint.y, tr_tint.z));
	[branch] if (tr_scale.w <= 0.0 || 1.0 - fz < anfang - 0.12)
		return float4(0, 0, 0, 0);

	float3 n = Tr_Normal(p);
	float steil = 1.0 - n.z;

	float2 k0, k1;
	uint m0 = Td_Material(uv0, k0);
	uint m1 = Td_Material(uv1, k1);
	float beginn = lerp(Tr_Start(m0), Tr_Start(m1), a);
	[branch] if (steil < beginn - 0.5 * tr_slope.w / tr_slope.z)
		return float4(0, 0, 0, 0);

	// triplanar: weights from the terrain normal, sharp (little double image at the seams)
	float3 w = n * n;
	w *= w;
	w /= dot(w, float3(1, 1, 1));
	float4 fx = float4(1, 0, 0, 0.5), fy = fx, fz4 = fx;
	[branch] if (w.x > 0.01) fx = Tr_Project(p.yz, dpx.yz, dpy.yz);
	[branch] if (w.y > 0.01) fy = Tr_Project(p.xz, dpx.xz, dpy.xz);
	[branch] if (w.z > 0.01) fz4 = Tr_Project(p.xy, dpx.xy, dpy.xy);
	// Height-based blend: where two projections are equally strong (slope diagonal to the axes), the one with the
	// higher rock wins in places -- patches instead of a double image. Only projections within 0.12 of the
	// strongest count.
	float3 hw = w + (float3(fx.w, fy.w, fz4.w) - 0.5) * 0.6 * step(0.01, w);
	w = max(hw - (max(hw.x, max(hw.y, hw.z)) - 0.12), 0.0) * step(0.01, w);
	w /= max(dot(w, float3(1, 1, 1)), 1e-4);
	float faktor = w.x * fx.x + w.y * fy.x + w.z * fz4.x;
	// contrast gain in the exponent, mean restored by the normalization (first order, factors have mean 1)
	faktor = pow(max(faktor, 1e-3), tr_extra.x) * tr_extra.y;
	float hoehe = w.x * fx.w + w.y * fy.w + w.z * fz4.w;
	// tangent slope in world axes: X projection (y, z), Y projection (x, z), Z projection (x, y)
	float3 stoer = w.x * float3(0, fx.y, fx.z) + w.y * float3(fy.y, 0, fy.z) + w.z * float3(fz4.y, fz4.z, 0);
	stoer *= tr_light.x;

	// Where EA paints rock itself, keep a little of its structure (the log mix in Terrain_Rock_Mix carries it).
	float deckel = lerp(m0 == 6 ? tr_tint.w : 1.0, m1 == 6 ? tr_tint.w : 1.0, a);
	float anteil = min(saturate((steil - beginn) * tr_slope.z + (hoehe - 0.5) * tr_slope.w), deckel) * tr_scale.w;

	// coarse EA color: mip level from the logical level plus the physical/logical ratio (HD atlas)
	uint bw, bh, stufen;
	atlas0.GetDimensions(0, bw, bh, stufen);
	float lod = tr_scale.z + log2(max((float)bw / max(td_atlas.x, 1.0), 1.0));
	float3 ea = lerp(atlas0.SampleLevel(s0, uv0, lod).rgb, atlas1.SampleLevel(s1, uv1, lod).rgb, a);
	// The coarse EA color still carries the blurred crevices of the EA cliff (dark patches); blend in the mean
	// color of the class (computed from the atlas at load time).
	float4 k0c = Tr_Class_Color(uv0), k1c = Tr_Class_Color(uv1);
	float3 klasse = lerp(k0c.a > 0 ? k0c.rgb : ea, k1c.a > 0 ? k1c.rgb : ea, a);
	ea = lerp(klasse, ea, tr_tint.x);

	// Per-pixel light: the rock normal changes the prelighting linearly (change of N.L per light, only on the lit
	// side, faded in softly there), as a ratio to the light of the terrain normal. Linear and with the unreduced
	// perturbation the change averages 0, so mean brightness stays the same; L(normalize(n + perturbation)) / L(n)
	// would brighten lit slopes through the clamping at N.L = 0 and 1.
	float3 aenderung = float3(0, 0, 0);
	[unroll] for (int k = 0; k < 3; ++k)
		aenderung += f_sunRay[k].w * saturate(dot(f_sunRay[k].xyz, n) * 4.0) * dot(f_sunRay[k].xyz, stoer) * f_sunDiffuse[k].rgb;
	float3 licht = clamp(1.0 + aenderung / max(Tr_Light(n), 0.05), tr_light.z, tr_light.w);
	fels = faktor * licht;
	return float4(ea, anteil);
}

/// Ground -> rock blend in log space (geometric), so the transition does not average two independent structures
/// (which looks muddy): color = EA_coarse * (EA/EA_coarse)^(1-a) * rock^a.
float3 Terrain_Rock_Mix(float3 boden, float4 rock, float3 fels)
{
	[branch] if (rock.a <= 0.0)
		return boden;
	float3 grob = max(rock.rgb, 1.0 / 255.0);
	return grob * pow(max(boden / grob, 1e-3), 1.0 - rock.a) * pow(max(fels, 1e-3), rock.a);
}

/// Flat snow: blends ground color rgb toward the snow class color on flat grass, dry grass, earth and gravel ground
/// (detail materials 1 to 4; roads, sand, rock and snow ground stay). rock = rock share from Terrain_Rock.
float3 Terrain_Snow(float3 rgb, float3 p, float2 uv0, float2 uv1, float a, float rock)
{
	// derivatives outside the branches
	float2 us = p.xy * tr_scale.y;
	float2 dux = ddx(us), duy = ddy(us);
	[branch] if (tr_snow.w <= 0.0)
		return rgb;
	float2 k0, k1;
	uint m0 = Td_Material(uv0, k0);
	uint m1 = Td_Material(uv1, k1);
	float boden = lerp((m0 >= 1 && m0 <= 4) ? 1.0 : 0.0, (m1 >= 1 && m1 <= 4) ? 1.0 : 0.0, a);
	[branch] if (boden <= 0.0)
		return rgb;
	float bruch = tRockB.SampleGrad(sRock, us, dux, duy).a;
	float steil = 1.0 - Tr_Normal(p).z + (bruch - 0.5) * (tr_extra.w - tr_extra.z);
	float anteil = boden * (1.0 - smoothstep(tr_extra.z, tr_extra.w, steil)) * (1.0 - rock) * tr_snow.w;
	// a little of the rock height as structure, mean unchanged
	float3 schnee = tr_snow.rgb * (0.9 + 0.2 * bruch);
	return lerp(rgb, schnee, anteil);
}

#endif // TERRAIN_ROCK_HLSLI
