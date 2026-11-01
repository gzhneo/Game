/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// terraindetail.hlsli ------------------------------------------------------------------------------
//
// Ground detail: fine, tiling detail structure from photographic CC0 scans over the original terrain
// texture, faded in when zooming in. Only in the TERRAIN_DETAIL permutation of the terrain shaders
// (terrain*.ps.hlsl) and the road shader (roadnoise2.ps.hlsl, there only for the three-way blends);
// without the macro the bytecode is unchanged.
//
// Ground type per pixel: the atlas coordinate tells which terrain texture is read. The atlas is split into
// a grid of 72-pixel cells (tile 64 + border 8, WorldHeightMap::updateTileTexturePositions); per cell
// td_slots holds the detail material of the class stored there and the position of its origin (modulo one
// tile). That gives the position inside the tile -- the detail texture covers exactly one tile (64 atlas
// pixels = 2 x 2 map cells = 20 world units) and tiles seamlessly, so it matches at all cell borders and
// between different classes (all classes are aligned to the world).
//
// Detail textures: detail_a.dds RGBA = materials 1..4 (grass, dry grass, soil, gravel), detail_b.dds RGBA =
// 5..8 (sand, rock, snow, asphalt/concrete). Byte b stands for the factor b/128; each mip level averages 128.
// The color is multiplied, so mean brightness stays the same, even from afar (mip levels).
// Blends: the weights of both layers are mixed by vertex alpha like the colors.
// Fade-in: by screen pixels per tile from the derivatives of the atlas coordinate (continuous in the triangle).
// ------------------------------------------------------------------------------------------------

#ifndef TERRAIN_DETAIL_HLSLI
#define TERRAIN_DETAIL_HLSLI

#include "slots.hlsli"
cbuffer cbTerrainDetail : register(SLOT_CB_TERRAIN_DETAIL)
{
	float4	td_atlas;		///< x = atlas width, y = atlas height (pixels, logical), z = 1/grid cell, w = 1/tile
	float4	td_fade;		///< x = pixels per tile where fade-in starts, y = 1/(full - x), z = strength, w = share of the second layer
	float4	td_strengthA;	///< strength per material 1..4
	float4	td_strengthB;	///< strength per material 5..8
	float4	td_layer2;		///< x = repeats of the second layer per tile, y = 1/sqrt(1 + w^2) (keeps the variance)
	uint4	td_grid;		///< x = columns, y = rows of the grid
	uint4	td_slots[256];	///< per cell material | OffsetU << 8 | OffsetV << 16, four cells per register
};

Texture2D		tDetailA	: register(SLOT_T_DETAIL_A);
Texture2D		tDetailB	: register(SLOT_T_DETAIL_B);
SamplerState	sDetail		: register(SLOT_S_DETAIL);

/// Material of the grid cell under uv and the position in tile units (fractional part = place in the tile).
uint Td_Material(float2 uv, out float2 kachel)
{
	float2 px = uv * td_atlas.xy;
	int2 feld = clamp(int2(floor(px * td_atlas.z)), int2(0, 0), int2(td_grid.xy) - int2(1, 1));
	uint i = (uint)feld.y * td_grid.x + (uint)feld.x;
	uint e = td_slots[i >> 2][i & 3];
	kachel = (px - float2((float)((e >> 8) & 0xFF), (float)((e >> 16) & 0xFF))) * td_atlas.w;
	return e & 0xFF;
}

float4 Td_WeightA(uint m) { return float4(m == 1 ? 1.0 : 0.0, m == 2 ? 1.0 : 0.0, m == 3 ? 1.0 : 0.0, m == 4 ? 1.0 : 0.0); }
float4 Td_WeightB(uint m) { return float4(m == 5 ? 1.0 : 0.0, m == 6 ? 1.0 : 0.0, m == 7 ? 1.0 : 0.0, m == 8 ? 1.0 : 0.0); }

/// Deviation from the mean (-1..+1) per material at position k (tile units).
float Td_Sample(float2 k, float2 dx, float2 dy, float4 wa, float4 wb)
{
	const float SKALA = 255.0 / 128.0;
	return dot(tDetailA.SampleGrad(sDetail, k, dx, dy) * SKALA - 1.0, wa)
	     + dot(tDetailB.SampleGrad(sDetail, k, dx, dy) * SKALA - 1.0, wb);
}

/// Color factor (mean 1). uv0/uv1 = atlas coordinates of the two layers, a = share of the second.
float Terrain_Detail(float2 uv0, float2 uv1, float a)
{
	// Tiles per pixel: from the first layer, whose coordinate is continuous in the triangle (the lookup table
	// does not enter the derivative, not even in the helper pixels at the triangle edge).
	float2 dx = ddx(uv0) * td_atlas.xy * td_atlas.w;
	float2 dy = ddy(uv0) * td_atlas.xy * td_atlas.w;
	float g = max(length(dx), length(dy));
	float fade = saturate((1.0 / max(g, 1e-6) - td_fade.x) * td_fade.y) * td_fade.z;

	float2 k0, k1;
	uint m0 = Td_Material(uv0, k0);
	uint m1 = Td_Material(uv1, k1);
	float4 wa = lerp(Td_WeightA(m0), Td_WeightA(m1), a) * td_strengthA;
	float4 wb = lerp(Td_WeightB(m0), Td_WeightB(m1), a) * td_strengthB;
	[branch] if (fade <= 0.0 || dot(wa + wb, float4(1.0, 1.0, 1.0, 1.0)) <= 0.0)
		return 1.0;

	// Both layers are world aligned; the position in the tile is the same for both (k0).
	float d = Td_Sample(k0, dx, dy, wa, wb);
	[branch] if (td_fade.w > 0.0)
	{
		// second layer: rotated by 90 degrees and denser -- breaks up the repetition of the first
		float s = td_layer2.x;
		float d2 = Td_Sample(k0.yx * s, dx.yx * s, dy.yx * s, wa, wb);
		d = (d + td_fade.w * d2) * td_layer2.y;
	}
	return max(1.0 + d * fade, 0.0);
}

#endif // TERRAIN_DETAIL_HLSLI
