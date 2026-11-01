/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// bloom.ps.hlsl ----------------------------------------------------------------------------------
//
// Bloom on the 8-bit scene image. One pass per permutation (BLOOM_PASS), all full-screen triangles
// from fullscreen.vs.hlsl; the levels are R11G11B10 targets at 1/4, 1/8, 1/16 and 1/32 of the scene:
//
//   0  prefilter: scene -> level 0. Four bilinear taps cover an exact 4x4 block; each tap is
//      multiplied by a soft-knee mask on luma (high threshold, so snow and white roofs stay below
//      it, explosions, muzzle flashes and lamps are above).
//   1  downsample: level n -> n+1, 4x4 box from four bilinear taps.
//   2  upsample: level n+1 -> level n, small tent filter; drawn with additive blending, so each
//      level ends up as the sum of itself and all coarser levels.
//   3  composite: scene += glow. Glow is the level-0 sum minus a weight times the coarsest level (a
//      band-pass): a large uniformly bright area is as bright in the coarsest level as in the sum
//      and cancels, only small bright spots and the rim of bright areas keep their halo. Drawn
//      with additive blending into the scene target.
//
// Inputs: t0 = source level, t1 = coarsest level (pass 3). e_source = 1/size of the target,
// e_bloom.xy = 1/size of the source, e_bloom.z/w = threshold and knee.
// ------------------------------------------------------------------------------------------------

#include "effects.hlsli"

Texture2D    fx_src  : register(t0);
Texture2D    fx_wide : register(t1);
SamplerState fx_linear : register(s0);

float Luma(float3 c)
{
	return dot(c, float3(0.299, 0.587, 0.114));
}

float3 Prefilter(float3 c)
{
	return c * smoothstep(e_bloom.z - e_bloom.w, e_bloom.z + e_bloom.w, Luma(c));
}

float4 main(float4 pos : SV_Position) : SV_Target
{
	const float2 uv = pos.xy * e_source.xy;
	const float2 t = e_bloom.xy;
#if BLOOM_PASS == 0
	float3 c = Prefilter(fx_src.SampleLevel(fx_linear, uv + float2(-1, -1) * t, 0).rgb);
	c += Prefilter(fx_src.SampleLevel(fx_linear, uv + float2( 1, -1) * t, 0).rgb);
	c += Prefilter(fx_src.SampleLevel(fx_linear, uv + float2(-1,  1) * t, 0).rgb);
	c += Prefilter(fx_src.SampleLevel(fx_linear, uv + float2( 1,  1) * t, 0).rgb);
	return float4(c * 0.25, 0.0);
#elif BLOOM_PASS == 1
	float3 c = fx_src.SampleLevel(fx_linear, uv + float2(-1, -1) * t, 0).rgb;
	c += fx_src.SampleLevel(fx_linear, uv + float2( 1, -1) * t, 0).rgb;
	c += fx_src.SampleLevel(fx_linear, uv + float2(-1,  1) * t, 0).rgb;
	c += fx_src.SampleLevel(fx_linear, uv + float2( 1,  1) * t, 0).rgb;
	return float4(c * 0.25, 0.0);
#elif BLOOM_PASS == 2
	float3 c = fx_src.SampleLevel(fx_linear, uv + float2(-0.5, -0.5) * t, 0).rgb;
	c += fx_src.SampleLevel(fx_linear, uv + float2( 0.5, -0.5) * t, 0).rgb;
	c += fx_src.SampleLevel(fx_linear, uv + float2(-0.5,  0.5) * t, 0).rgb;
	c += fx_src.SampleLevel(fx_linear, uv + float2( 0.5,  0.5) * t, 0).rgb;
	return float4(c * 0.25, 0.0);
#else
	// Level 0 holds the sum of four levels (mean = sum / 4).
	const float3 sum = fx_src.SampleLevel(fx_linear, uv, 0).rgb * 0.25;
	const float3 wide = fx_wide.SampleLevel(fx_linear, uv, 0).rgb;
	return float4(max(sum - wide * e_glow.y, 0.0) * e_glow.x, 0.0);
#endif
}
