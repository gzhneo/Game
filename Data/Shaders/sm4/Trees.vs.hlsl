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

// Trees.vs.hlsl (SM 4.0)
//
// Direct3D 11 version of the original Trees vertex shader (EA vs_1_1). Grass tufts and tree
// billboards: wind bends the top edge while the bottom edge stays put (profile and flutter in
// treesway.hlsli; sway[1..10].w is the flutter clock). The normal carries three
// helper values instead of a normal (n.x = sway table index, n.y = darkening, n.z = base height).
//
// Constants in the layout RenderTreesVS (renderpieces.h): W3DTreeBuffer::drawTrees loads the transposed
// WorldViewProj matrix, the zero vector as sway[0] (sway index 0 = still), the MAX_SWAY_TYPES sway
// vectors and the shroud offset/scale. The four dp4 of the original are reproduced directly instead of
// building a matrix, so row/column major does not matter.
//
// Output is the shared VS_OUT from common.hlsli. The caller binds no pixel shader of its own, so
// the fixed-function emulation in ffp.ps.hlsl draws: uv0 is the tree texture, uv1 the shroud.
// ------------------------------------------------------------------------------------------------

#include "common.hlsli"
#include "treesway.hlsli"

cbuffer RenderTreesVS : register(SLOT_CB_PIECE_VS)
{
	float4 unused0[4];
	float4 worldViewProj[4];
	float4 sway[11];
	float4 unused19[13];
	float4 shroudOffset;
	float4 shroudScale;
};

struct VS_IN
{
	float4 pos    : POSITION;
	float4 normal : NORMAL;
	float4 color  : COLOR0;
	float4 uv     : TEXCOORD0;
};

VS_OUT main(VS_IN v)
{
	VS_OUT o;

	// Position and normal come from the buffer as float3, so w is 1 -- as in vs_1_1.
	const float height = v.pos.z - v.normal.z;
	// n.x is always a small exact integer (W3DTreeBuffer stores an int), so truncating matches
	// the rounding of the address register (mova). The clamp turns an out-of-range value into
	// "still" instead of undefined.
	const int index = clamp((int)v.normal.x, 0, 10);

	const float4 swayed = float4(v.pos.xyz + TreeSwayOffset(v.pos.xyz, height, sway[index]), 1.0f);

	float4 p;
	p.x = dot(swayed, worldViewProj[0]);
	p.y = dot(swayed, worldViewProj[1]);
	p.z = dot(swayed, worldViewProj[2]);
	p.w = dot(swayed, worldViewProj[3]);
	o.pos = p;

	const float4 shroud = (float4(v.pos.xyz, 1.0f) + shroudOffset) * shroudScale;

	o.uv0 = float4(v.uv.xy, 0, 1);
	o.uv1 = float4(shroud.xy, 0, 1);
	o.uv2 = float4(0, 0, 0, 1);
	o.uv3 = float4(0, 0, 0, 1);

	// D3DCOLOR is BGRA in memory; the input layout declares R8G8B8A8_UNORM.
	o.color0 = v.color.bgra * float4(v.normal.yyy, 1.0f);
	o.color1 = float4(0, 0, 0, 0);
	o.fog    = float4(1, 0, 0, 0);
	return o;
}
