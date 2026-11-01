/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// particles.vs.hlsl ------------------------------------------------------------------------------
//
// Particle quads built on the GPU (instancing): one instance per particle, six vertices per quad
// (SV_VertexID, triangles 0-1-2 and 2-3-0 as the quad index buffer of PointGroupClass). The same
// computation as PointGroupClass::Update_Arrays for QUADS with size and orientation:
//   billboard: corner = view space position + rotated unit corner * size (in the view plane)
//   ground:    corner = world position + (+-X +-Y ground vector of the orientation) * size, then view
// The corner and ground tables are the CPU's own (PointGroupClass, cbParticleTable), so the corners are
// the ones the CPU computed. The output matches ffp.vs.hlsl for an unlit draw with vertex
// color (PRELIT_DIFFUSE) and UV set 0, so every pixel shader of the fixed-function path fits.
//
// Optional (cbParticle flags):
//   terrain floor: a billboard centre below the terrain (drifting into a slope) is lifted onto it; the
//     height follows the two terrain triangles of the cell (diagonal top left - bottom right).
//   lighting (smoke, dust): color * lerp(1, time of day light + dynamic lights, strength). The time of
//     day light is relative to the afternoon, so the authored daylight look stays; the dynamic lights
//     (cbTerrainLights) add factor * (diffuse / 2 + ambient), a billboard has no normal.
// ------------------------------------------------------------------------------------------------

#include "common.hlsli"
#include "terrainlights.hlsli"

#include "slots.hlsli"
cbuffer cbParticle : register(SLOT_CB_PARTICLE)
{
	float4	pa_flags;		///< x = 1 billboard, 0 ground aligned; y = 1 lit; z = 1 terrain floor; w = 1 dynamic lights
	float4	pa_uv01;		///< UV of corners 0 and 1 (frame of the point group)
	float4	pa_uv23;		///< UV of corners 2 and 3
	float4	pa_light;		///< xyz = light of the time of day relative to the afternoon, w = strength
};

cbuffer cbParticleTable : register(SLOT_CB_PARTICLE_TABLE)
{
	float4	pa_corner[512];	///< _QuadVertexLocationOrientationTable: orientation o, corner k in [o * 2 + k / 2], xy or zw
	float4	pa_ground[256];	///< xy = rotated ground X vector, zw = rotated ground Y vector
};

struct VS_IN
{
	float4	pos_size : POSITION;	///< xyz = world position, w = size
	float4	color    : COLOR0;		///< D3DCOLOR, swizzled below as in ffp.vs.hlsl
	uint4	of       : TEXCOORD0;	///< x = orientation 0..255
	uint	vid      : SV_VertexID;
};

// Corner of the quad for vertex 0..5 (triangles 0-1-2 and 2-3-0).
uint Quad_Corner(uint vid)
{
	if (vid < 3) return vid;
	if (vid == 3) return 2;
	if (vid == 4) return 3;
	return 0;
}

// Terrain height at world position p (the two triangles of the cell, as the terrain tiles draw them).
float Pa_Terrain_Height(float2 p)
{
	const float2 g = p * tl_map.x + tl_map.yy;
	const int2 i = int2(floor(g));
	const float2 f = g - floor(g);
	const float h00 = Tl_Height(i);
	const float h10 = Tl_Height(i + int2(1, 0));
	const float h01 = Tl_Height(i + int2(0, 1));
	const float h11 = Tl_Height(i + int2(1, 1));
	if (f.x >= f.y) return h00 + f.x * (h10 - h00) + f.y * (h11 - h10);
	return h00 + f.y * (h01 - h00) + f.x * (h11 - h01);
}

// Light on a lit particle at world position p.
float3 Pa_Light(float3 p)
{
	float3 light = pa_light.xyz;
	if (pa_flags.w != 0.0) {
		const int count = (int)tl_map.w;
		[loop] for (int k = 0; k < count; ++k) {
			float3 to_light;
			float factor;
			if (!Tl_Factor(p, tl_position[k], tl_range[k], to_light, factor)) continue;
			light += factor * (0.5 * tl_diffuse[k].rgb + tl_ambient[k].rgb);
		}
	}
	return lerp(float3(1, 1, 1), light, pa_light.w);
}

VS_OUT main(VS_IN v)
{
	const uint corner = Quad_Corner(v.vid);
	const uint orient = v.of.x & 255u;
	const float size = v.pos_size.w;
	float3 view_pos;
	float3 center_pos = v.pos_size.xyz;
	if (pa_flags.z != 0.0) center_pos.z = max(center_pos.z, Pa_Terrain_Height(center_pos.xy));
	float3 world_pos = center_pos;

	if (pa_flags.x != 0.0) {
		const float4 pair = pa_corner[orient * 2u + corner / 2u];
		float2 offset = pair.xy;
		if ((corner & 1u) != 0u) offset = pair.zw;
		const float3 center = mul(float4(center_pos, 1.0), f_view).xyz;
		view_pos = center + float3(offset, 0.0) * size;
	}
	else {
		// Corners 0..3: +X+Y, +X-Y, -X-Y, -X+Y (Update_Arrays, ground aligned)
		const float4 g = pa_ground[orient];
		float sx = 1.0;
		float sy = 1.0;
		if (corner >= 2u) sx = -1.0;
		if (corner == 1u || corner == 2u) sy = -1.0;
		world_pos.xy += (sx * g.xy + sy * g.zw) * size;
		view_pos = mul(float4(world_pos, 1.0), f_view).xyz;
	}

	VS_OUT o;
	o.pos = mul(float4(view_pos, 1.0), f_proj);
	o.color0 = v.color.bgra;
	if (pa_flags.y != 0.0) o.color0.rgb = saturate(o.color0.rgb * Pa_Light(center_pos));
	o.color1 = float4(0, 0, 0, 0);
	float2 uv = pa_uv01.xy;
	if (corner == 1u) uv = pa_uv01.zw;
	if (corner == 2u) uv = pa_uv23.xy;
	if (corner == 3u) uv = pa_uv23.zw;
	o.uv0 = float4(uv, 0.0, 1.0);
	o.uv1 = float4(0, 0, 0, 1);
	o.uv2 = float4(0, 0, 0, 1);
	o.uv3 = float4(0, 0, 0, 1);

	float fog_factor = 1.0;
	if (f_fogParams.z != 0.0) {
		const float d = -view_pos.z;
		fog_factor = saturate((f_fogParams.y - d) / max(f_fogParams.y - f_fogParams.x, 1e-6));
	}
	o.fog = float4(fog_factor, world_pos);
	return o;
}
