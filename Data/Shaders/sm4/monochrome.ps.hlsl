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

// monochrome.ps.hlsl (SM 4.0)
//
// Direct3D 11 version of the original monochrome pixel shader, same math. Constants in the layout
// RenderScreenFilterPS (renderpieces.h) on the piece pixel shader slot; input is the shared VS_OUT struct.
//
// Screen filter (ScreenBWFilter): image luminance (c0 = weights 0.3/0.59/0.11), tinted by c1
// and blended against the original image by c2.
// ------------------------------------------------------------------------------------------------

#include "common.hlsli"

Texture2D    tex0  : register(t0);
SamplerState samp0 : register(s0);

cbuffer RenderScreenFilterPS : register(SLOT_CB_PIECE_PS)
{
	float4 lumWeights;
	float4 tint;
	float4 fade;
};

float4 main(VS_OUT i) : SV_Target
{
	float4 t0 = tex0.Sample(samp0, i.uv0.xy);
	// dp3 in ps_1_1 writes all four channels, so the dot product is replicated.
	float4 r1 = dot(t0.rgb, lumWeights.rgb);
	r1 *= tint;
	return Scene_Out(lerp(t0, r1, fade));
}
