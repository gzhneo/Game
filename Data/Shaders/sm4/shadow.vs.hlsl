/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// shadow.vs.hlsl ---------------------------------------------------------------------------------
//
// Vertex shader of the shadow casters while drawing the shadow map. The backend replays the draws
// recorded in the opaque part of the scene from the sun's view, with the same vertex and index
// buffers. Permutations:
//
//   (none)         position only (draws without alpha test)
//   SHADOW_UV=n    plus UV set n (0..3) for the stage 0 alpha test, with texture matrix
//   SHADOW_TREES   grass tufts and tree billboards (Trees.vs.hlsl): same wind sway, UV set 0
//   SHADOW_SKIN    GPU skins: bone index in UV set 2, palette in cbSkin (skin.hlsli)
//   SHADOW_INSTANCED  with no or one SHADOW_UV: the matrix per instance from vertex stream 1
// ------------------------------------------------------------------------------------------------

#include "shadow.hlsli"
#ifdef SHADOW_SKIN
#include "skin.hlsli"
#endif
#ifdef SHADOW_TREES
#include "treesway.hlsli"
#endif

struct VS_IN
{
#ifdef SHADOW_TREES
	float4	pos    : POSITION;
	float4	normal : NORMAL;		///< x = sway index, z = base height (see Trees.vs.hlsl)
	float4	uv     : TEXCOORD0;
#else
	float3	pos    : POSITION;
#ifdef SHADOW_SKIN
	float4	bone   : TEXCOORD2;
#endif
#if defined(SHADOW_UV)
#if SHADOW_UV == 0
	float4	uv     : TEXCOORD0;
#elif SHADOW_UV == 1
	float4	uv     : TEXCOORD1;
#elif SHADOW_UV == 2
	float4	uv     : TEXCOORD2;
#else
	float4	uv     : TEXCOORD3;
#endif
#endif
#endif
#ifdef SHADOW_INSTANCED
	// Instanced casters: consecutive draws of the same geometry in one call, the world-light matrix per
	// instance from the second vertex stream (one row per element).
	float4	wl0    : WORLDLIGHT0;
	float4	wl1    : WORLDLIGHT1;
	float4	wl2    : WORLDLIGHT2;
	float4	wl3    : WORLDLIGHT3;
#endif
};

struct VS_OUT_SHADOW
{
	float4	pos : SV_Position;
	float2	uv  : TEXCOORD0;
};

VS_OUT_SHADOW main(VS_IN v)
{
	VS_OUT_SHADOW o;
#ifdef SHADOW_TREES
	// As in Trees.vs.hlsl: the base stays put, the top sways (treesway.hlsli).
	const float height = v.pos.z - v.normal.z;
	const int index = clamp((int)v.normal.x, 0, 10);
	const float4 swayed = float4(v.pos.xyz + TreeSwayOffset(v.pos.xyz, height, sd_sway[index]), 1.0);
	o.pos = mul(swayed, sd_worldLight);
	o.uv = v.uv.xy;
#elif defined(SHADOW_SKIN)
	o.pos = mul(float4(Skin_Point(Skin_Bone(v.bone.x), v.pos), 1.0), sd_worldLight);
	o.uv = float2(0.0, 0.0);
#else
#ifdef SHADOW_INSTANCED
	o.pos = mul(float4(v.pos, 1.0), float4x4(v.wl0, v.wl1, v.wl2, v.wl3));
#else
	o.pos = mul(float4(v.pos, 1.0), sd_worldLight);
#endif
#if defined(SHADOW_UV)
	// Short coordinates are extended with a 1 after the last component before the texture matrix,
	// matching fixed-function texture transform rules (as Stage_TexCoord in ffp.vs.hlsl).
	float4 uv = v.uv;
	if (sd_uv.x != 0.0) {
		if (sd_uv.y == 1.0) uv = float4(uv.x, 1.0, 0.0, 0.0);
		else if (sd_uv.y == 2.0) uv = float4(uv.xy, 1.0, 0.0);
		uv = mul(uv, sd_texMatrix);
	}
	o.uv = uv.xy;
#else
	o.uv = float2(0.0, 0.0);
#endif
#endif
	return o;
}
