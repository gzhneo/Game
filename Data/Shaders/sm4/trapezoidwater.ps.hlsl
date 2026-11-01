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

// trapezoidwater.ps.hlsl (SM 4.0)
//
// Direct3D 11 version of the original trapezoidwater pixel shader.
// Trapezoid water areas (drawTrapezoidWater) with shroud (stage 3) applied in the pixel shader.
// ------------------------------------------------------------------------------------------------

#include "common.hlsli"

Texture2D    tex0  : register(t0);
Texture2D    tex1  : register(t1);
Texture2D    tex2  : register(t2);
Texture2D    tex3  : register(t3);
SamplerState samp0 : register(s0);
SamplerState samp1 : register(s1);
SamplerState samp2 : register(s2);
SamplerState samp3 : register(s3);

float4 main(VS_OUT i) : SV_Target
{
	float4 t0 = tex0.Sample(samp0, i.uv0.xy);
	float4 t1 = tex1.Sample(samp1, i.uv1.xy);
	float4 t2 = tex2.Sample(samp2, i.uv2.xy);
	float4 t3 = tex3.Sample(samp3, i.uv3.xy);

	float4 r0 = i.color0 * t0;
	r0.rgb = t1.rgb * t2.rgb + r0.rgb;
	r0.rgb *= t3.rgb;
	return Scene_Out(r0, i.pos);
}
