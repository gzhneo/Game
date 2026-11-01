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

// roadnoise2.ps.hlsl (SM 4.0)
//
// Direct3D 11 version of the original roadnoise2 pixel shader.
// Roads with two noise textures: road texture (stage 0) times noise 1 (stage 1) times
// noise 2 (stage 2) times vertex color.
// ------------------------------------------------------------------------------------------------

#include "common.hlsli"
#ifdef TERRAIN_DETAIL
// ground detail permutation
#include "terraindetail.hlsli"
#endif
#ifdef TERRAIN_ROCK
// rock layer permutation
#include "terrainrock.hlsli"
#endif
#ifdef TERRAIN_NORMAL
// relief permutation (normal maps and gloss)
#include "terrainnormal.hlsli"
#endif
#ifdef TERRAIN_LIGHT
// terrain light per pixel
#include "terrainpixellight.hlsli"
#endif

Texture2D    tex0  : register(t0);
Texture2D    tex1  : register(t1);
Texture2D    tex2  : register(t2);
SamplerState samp0 : register(s0);
SamplerState samp1 : register(s1);
SamplerState samp2 : register(s2);

float4 main(VS_OUT i) : SV_Target
{
	float4 t0 = tex0.Sample(samp0, i.uv0.xy);
	float4 t1 = tex1.Sample(samp1, i.uv1.xy);
	float4 t2 = tex2.Sample(samp2, i.uv2.xy);
#ifdef TERRAIN_ROCK
	// the third blend layer gets the rock layer too (stage 0 is the atlas)
	float3 fels;
	float4 rock = Terrain_Rock(i.fog.yzw, i.uv0.xy, i.uv0.xy, 0.0, tex0, samp0, tex0, samp0, fels);
#endif
	float4 r0 = t0 * t1;
#ifdef TERRAIN_DETAIL
	// only for the terrain's three-way blends (HeightMap renderExtraBlendTiles): stage 0 is the atlas
#ifdef TERRAIN_ROCK
	r0.rgb *= lerp(Terrain_Detail(i.uv0.xy, i.uv0.xy, 0.0), 1.0, rock.a);
#else
	r0.rgb *= Terrain_Detail(i.uv0.xy, i.uv0.xy, 0.0);
#endif
#endif
#ifdef TERRAIN_ROCK
	r0.rgb = Terrain_Rock_Mix(r0.rgb, float4(rock.rgb * t1.rgb, rock.a), fels);
#endif
#ifdef TERRAIN_NORMAL
	// three-way blends of the terrain: the highlight is darkened by both noise textures like in the base pass
	float3 tn_ratio, tn_highlight;
	Terrain_Normal(i.fog.yzw, i.uv0.xy, i.uv0.xy, 0.0, samp0, samp0, tn_ratio, tn_highlight);
#ifdef TERRAIN_ROCK
	tn_ratio = lerp(tn_ratio, 1.0, rock.a);
	tn_highlight *= 1.0 - rock.a;
#endif
	r0.rgb *= tn_ratio;
	r0 *= t2;
	r0 *= i.color0;
#ifdef TERRAIN_LIGHT
	r0.rgb *= Terrain_Pixel_Light(i.fog.yzw);
#endif
	r0.rgb += tn_highlight * t1.rgb * t2.rgb;
	return Scene_Out(r0);
#elif defined(TERRAIN_LIGHT)
	r0 *= t2;
	r0 *= i.color0;
	r0.rgb *= Terrain_Pixel_Light(i.fog.yzw);
	return Scene_Out(r0);
#else
	r0 *= t2;
	return Scene_Out(r0 * i.color0);
#endif
}
