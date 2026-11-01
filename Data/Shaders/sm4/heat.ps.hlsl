/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// heat.ps.hlsl -----------------------------------------------------------------------------------
//
// Heat shimmer over fire and explosions, from the scene image itself (no particle mask, no extra
// render target during the scene). Two permutations:
//
//   HEAT_PASS 0  hot-spot mask: scene -> level 0 of the glow targets (1/4 resolution, R channel).
//                Hot = very bright AND saturated warm color (yellow-white core to orange): fire,
//                explosion fireballs, burning wrecks. Sand, snow and white walls are bright but not
//                saturated, shaded team-color vehicles are saturated but not bright enough. A strongly
//                lit yellow object can still pass. Averaged over 4x4 pixels.
//                (The mask is blurred to 1/8 resolution by bloom.ps.hlsl pass 1 afterwards.)
//   HEAT_PASS 1  distortion: the scene is sampled with a small offset that follows a few moving
//                sine waves, scaled by the mask. The mask is also sampled a little above the pixel,
//                so the shimmer reaches upward over the flame. Everything outside the mask stays
//                bit-identical to the source.
//
// Inputs: t0 = scene, t1 = mask (pass 1). e_source = 1/size of the
// target, e_bloom.xy = 1/size of the source (pass 0), e_rect = view rectangle, e_glow.z = amplitude
// in pixels, e_glow.w = phase 0..2 pi (the wave speeds are whole multiples, so the wrap is seamless).
// ------------------------------------------------------------------------------------------------

#include "effects.hlsli"

Texture2D    fx_src  : register(t0);
Texture2D    fx_mask : register(t1);
SamplerState fx_linear : register(s0);

float Hot(float3 c)
{
	const float mx = max(c.r, max(c.g, c.b));
	const float mn = min(c.r, min(c.g, c.b));
	const float sat = (mx - mn) / max(mx, 1e-4);
	const float luma = dot(c, float3(0.299, 0.587, 0.114));
	const float warm = (c.r >= c.g && c.g >= c.b) ? 1.0 : 0.0;
	return smoothstep(0.78, 0.95, luma) * smoothstep(0.35, 0.6, sat) * warm;
}

float4 main(float4 pos : SV_Position) : SV_Target
{
	const float2 uv = pos.xy * e_source.xy;
#if HEAT_PASS == 0
	const float2 t = e_bloom.xy;
	float h = Hot(fx_src.SampleLevel(fx_linear, uv + float2(-1, -1) * t, 0).rgb);
	h += Hot(fx_src.SampleLevel(fx_linear, uv + float2( 1, -1) * t, 0).rgb);
	h += Hot(fx_src.SampleLevel(fx_linear, uv + float2(-1,  1) * t, 0).rgb);
	h += Hot(fx_src.SampleLevel(fx_linear, uv + float2( 1,  1) * t, 0).rgb);
	return float4(h * 0.25, 0.0, 0.0, 0.0);
#else
	const float2 lo = (e_rect.xy + 0.5) * e_source.xy;
	const float2 hi = (e_rect.zw - 0.5) * e_source.xy;
	float m = max(fx_mask.SampleLevel(fx_linear, uv, 0).r,
				  fx_mask.SampleLevel(fx_linear, uv + float2(0.0, -28.0) * e_source.xy, 0).r);
	m = saturate(m * 2.0);
	float2 o = float2(0.0, 0.0);
	if (m > 0.01) {
		// Waves scroll upward; speeds 5, 7, 11, 13 rad/s-equivalents, integer multiples of the phase.
		const float a = sin(pos.y * 0.055 + e_glow.w * 7.0 + pos.x * 0.013);
		const float b = sin(pos.y * 0.131 - e_glow.w * 13.0 + pos.x * 0.029);
		const float c = sin(pos.x * 0.047 + e_glow.w * 5.0 + pos.y * 0.021);
		const float d = sin(pos.x * 0.113 + e_glow.w * 11.0 - pos.y * 0.037);
		o = float2(a + 0.5 * b, 0.6 * (c + 0.5 * d)) * (e_glow.z * m);
	}
	const float3 c = fx_src.SampleLevel(fx_linear, clamp(uv + o * e_source.xy, lo, hi), 0).rgb;
	return float4(c, 1.0);
#endif
}
