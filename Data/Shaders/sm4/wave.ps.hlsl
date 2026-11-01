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

// wave.ps.hlsl (SM 4.0)
//
// Direct3D 11 version of the original wave pixel shader.
// Sea water (WATER_TYPE_2_PVSHADER): the bump map (stage 0, V8U8 -> R8G8_SNORM, signed) offsets
// the lookup in the reflection texture (stage 1, coordinates from wave.vs.hlsl), result times
// vertex color. The bump matrix of stage 1 comes in RenderSeaWavePS (renderpieces.h).
// ------------------------------------------------------------------------------------------------

#include "common.hlsli"

Texture2D    tex0  : register(t0);	// bump map
Texture2D    tex1  : register(t1);	// reflection
SamplerState samp0 : register(s0);
SamplerState samp1 : register(s1);

cbuffer RenderSeaWavePS : register(SLOT_CB_PIECE_PS)
{
	float4 bumpEnvMat;
};

float4 main(VS_OUT i) : SV_Target
{
	float2 dudv = tex0.Sample(samp0, i.uv0.xy).rg;
	float2 uv = i.uv1.xy + float2(dudv.x * bumpEnvMat.x + dudv.y * bumpEnvMat.z,
	                              dudv.x * bumpEnvMat.y + dudv.y * bumpEnvMat.w);
	return Scene_Out(tex1.Sample(samp1, uv) * i.color0);
}
