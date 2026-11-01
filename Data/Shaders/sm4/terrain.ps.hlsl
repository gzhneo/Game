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

// terrain.ps.hlsl (SM 4.0)
//
// Direct3D 11 version of the original terrain, terrainnoise and terrainnoise2 pixel shaders, in one body.
// Terrain base pass: two terrain textures (stages 0/1) are blended by vertex alpha and multiplied by the
// vertex lighting. TERRAIN_NOISE_LAYERS (set by terrainnoise.ps.hlsl and terrainnoise2.ps.hlsl, default 0)
// multiplies the result by one or two noise textures (stages 2 and 3: clouds and light map, coordinates from
// the camera space position, see W3DShaderManager::updateNoise1/2).
// ------------------------------------------------------------------------------------------------

#ifndef TERRAIN_NOISE_LAYERS
#define TERRAIN_NOISE_LAYERS 0
#endif

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
#if TERRAIN_NOISE_LAYERS >= 1
Texture2D    tex2  : register(t2);
#endif
#if TERRAIN_NOISE_LAYERS >= 2
Texture2D    tex3  : register(t3);
#endif
SamplerState samp0 : register(s0);
SamplerState samp1 : register(s1);
#if TERRAIN_NOISE_LAYERS >= 1
SamplerState samp2 : register(s2);
#endif
#if TERRAIN_NOISE_LAYERS >= 2
SamplerState samp3 : register(s3);
#endif

float4 main(VS_OUT i) : SV_Target
{
	float4 t0 = tex0.Sample(samp0, i.uv0.xy);
	float4 t1 = tex1.Sample(samp1, i.uv1.xy);
#if TERRAIN_NOISE_LAYERS >= 1
	float4 t2 = tex2.Sample(samp2, i.uv2.xy);
#endif
#if TERRAIN_NOISE_LAYERS >= 2
	float4 t3 = tex3.Sample(samp3, i.uv3.xy);
#endif
	float4 r0 = lerp(t0, t1, i.color0.a);
#ifdef TERRAIN_ROCK
	// rock layer on steep slopes; the detail layer fades out there
	float3 fels;
	float4 rock = Terrain_Rock(i.fog.yzw, i.uv0.xy, i.uv1.xy, i.color0.a, tex0, samp0, tex1, samp1, fels);
#endif
#ifdef TERRAIN_DETAIL
#ifdef TERRAIN_ROCK
	r0.rgb *= lerp(Terrain_Detail(i.uv0.xy, i.uv1.xy, i.color0.a), 1.0, rock.a);
#else
	r0.rgb *= Terrain_Detail(i.uv0.xy, i.uv1.xy, i.color0.a);
#endif
#endif
#ifdef TERRAIN_ROCK
	r0.rgb = Terrain_Rock_Mix(r0.rgb, rock, fels);
	r0.rgb = Terrain_Snow(r0.rgb, i.fog.yzw, i.uv0.xy, i.uv1.xy, i.color0.a, rock.a);
#endif
#ifdef TERRAIN_NORMAL
	// relief of the terrain textures; the rock layer brings its own normals
	float3 tn_ratio, tn_highlight;
	Terrain_Normal(i.fog.yzw, i.uv0.xy, i.uv1.xy, i.color0.a, samp0, samp1, tn_ratio, tn_highlight);
#ifdef TERRAIN_ROCK
	tn_ratio = lerp(tn_ratio, 1.0, rock.a);
	tn_highlight *= 1.0 - rock.a;
#endif
	r0.rgb *= tn_ratio;
#endif
	r0 *= i.color0;
#ifdef TERRAIN_LIGHT
	r0.rgb *= Terrain_Pixel_Light(i.fog.yzw);
#endif
#ifdef TERRAIN_NORMAL
	r0.rgb += tn_highlight;
#endif
#if TERRAIN_NOISE_LAYERS >= 2
	r0 *= t2;
	return Scene_Out(r0 * t3);
#elif TERRAIN_NOISE_LAYERS == 1
	return Scene_Out(r0 * t2);
#else
	return Scene_Out(r0);
#endif
}
