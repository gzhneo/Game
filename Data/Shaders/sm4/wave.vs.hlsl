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

// wave.vs.hlsl (SM 4.0)
//
// Direct3D 11 version of the original wave vertex shader.
// Sea water: transform the position and derive the reflection texture coordinates from the
// screen position.
//
// Constants in the layout RenderSeaWaveVS (renderpieces.h): WaterRenderObjClass loads the transposed
// WorldViewProj matrix and the projection rows (row 0 = 0.5, -0.5, 0.5, 0.5). The four dp4 of the
// original are reproduced directly instead of building a matrix, so row/column major does not matter.
//
// Output is the shared VS_OUT from common.hlsli, so the shader pairs with wave.ps.hlsl (and any
// other pixel shader here).
// ------------------------------------------------------------------------------------------------

#include "common.hlsli"

cbuffer RenderSeaWaveVS : register(SLOT_CB_PIECE_VS)
{
	float4 zero;
	float4 one;
	float4 worldViewProj[4];
	float4 texProj[4];
};

struct VS_IN
{
	float4 pos   : POSITION;
	float4 color : COLOR0;
	float4 uv    : TEXCOORD0;
};

VS_OUT main(VS_IN v)
{
	VS_OUT o;

	float4 p;
	p.x = dot(v.pos, worldViewProj[0]);
	p.y = dot(v.pos, worldViewProj[1]);
	p.z = dot(v.pos, worldViewProj[2]);
	p.w = dot(v.pos, worldViewProj[3]);
	o.pos = p;

	const float2 ndc = p.xy / p.w;
	o.uv0 = float4(v.uv.xy, 0, 1);
	o.uv1 = float4(ndc * texProj[0].xy + texProj[0].zw, 0, 1);
	o.uv2 = float4(0, 0, 0, 1);
	o.uv3 = float4(0, 0, 0, 1);

	// D3DCOLOR is BGRA in memory; the input layout declares R8G8B8A8_UNORM.
	o.color0 = v.color.bgra;
	o.color1 = float4(0, 0, 0, 0);
	o.fog    = float4(1, 0, 0, 0);
	return o;
}
