/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// taa.ps.hlsl ------------------------------------------------------------------------------------
//
// Temporal anti-aliasing by camera reprojection, without motion vectors. The scene is drawn with a sub-pixel
// jitter of the projection that changes every frame (rendercore_taa.cpp); this pass blends it with the history of
// the previous frames. Two passes, selected by TAA_PASS:
//
//   0  Resolve into the new history: the position of the pixel in the previous frame follows from the depth copy (t1)
//      and t_reproj (camera space of this frame -> clip space of the previous one). The history (t2) is read there
//      (Catmull-Rom) and limited to the color range of the 3x3 neighborhood in this frame (variance clipping in YCoCg),
//      which removes most of the trail of moving units and of everything without depth (particles, transparent
//      surfaces); then t_params.x of the new frame is mixed in. Off screen or without a valid history: the new frame.
//   1  Copy of the new history into the scene target.
// ------------------------------------------------------------------------------------------------

#include "effects.hlsli"

cbuffer cbTaa : register(SLOT_CB_TAA)
{
	row_major float4x4	t_reproj;	///< camera space of this frame -> clip space of the previous frame (row vectors)
	float4		t_params;	///< x: weight of the new frame, y: 1 = history valid, zw: this frame's jitter (pixels)
};

Texture2D        fx_cur   : register(t0);	///< pass 0: this frame; pass 1: the new history
Texture2D<float> fx_depth : register(t1);	///< depth copy of this frame
Texture2D        fx_hist  : register(t2);	///< history of the previous frames
SamplerState     fx_linear : register(s0);

#if TAA_PASS == 0
float3 To_YCoCg(float3 c)
{
	return float3(dot(c, float3(0.25, 0.5, 0.25)), dot(c, float3(0.5, 0.0, -0.5)), dot(c, float3(-0.25, 0.5, -0.25)));
}

float3 From_YCoCg(float3 y)
{
	return float3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z);
}

// History at a fractional position: Catmull-Rom from five bilinear reads. A plain bilinear read blurs by a different
// amount every frame of a slow pan (the fraction changes), which shows as flicker.
float3 History_At(float2 pixel)
{
	const float2 size = e_source.zw;
	const float2 t1 = floor(pixel - 0.5) + 0.5;
	const float2 f = pixel - t1;
	const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	const float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	const float2 w3 = f * f * (-0.5 + 0.5 * f);
	const float2 w12 = w1 + w2;
	const float2 t0 = (t1 - 1.0) / size;
	const float2 t3 = (t1 + 2.0) / size;
	const float2 t12 = (t1 + w2 / w12) / size;
	float3 c = fx_hist.SampleLevel(fx_linear, float2(t12.x, t0.y), 0).rgb * (w12.x * w0.y);
	c += fx_hist.SampleLevel(fx_linear, float2(t0.x, t12.y), 0).rgb * (w0.x * w12.y);
	c += fx_hist.SampleLevel(fx_linear, t12, 0).rgb * (w12.x * w12.y);
	c += fx_hist.SampleLevel(fx_linear, float2(t3.x, t12.y), 0).rgb * (w3.x * w12.y);
	c += fx_hist.SampleLevel(fx_linear, float2(t12.x, t3.y), 0).rgb * (w12.x * w3.y);
	const float wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
	return max(c / wsum, 0.0);
}

float4 main(float4 pos : SV_Position) : SV_Target
{
	const int2 p = int2(pos.xy);
	const int2 lo = int2(e_rect.xy), hi = int2(e_rect.zw) - 1;
	const float3 c = fx_cur.Load(int3(p, 0)).rgb;
	if (t_params.y < 0.5) return float4(c, 1.0);

	// Color range of the neighborhood: mean and deviation in YCoCg.
	float3 m1 = 0.0, m2 = 0.0;
	[unroll]
	for (int y = -1; y <= 1; ++y) {
		[unroll]
		for (int x = -1; x <= 1; ++x) {
			const float3 s = To_YCoCg(fx_cur.Load(int3(clamp(p + int2(x, y), lo, hi), 0)).rgb);
			m1 += s;
			m2 += s * s;
		}
	}
	m1 *= 1.0 / 9.0;
	const float3 sigma = sqrt(max(m2 * (1.0 / 9.0) - m1 * m1, 0.0));

	// Where this pixel lay in the previous frame. The pixel shows the surface the jittered projection put here; without
	// the jitter it lies at pos - jitter, and View_Position works without it.
	const float3 P = View_Position(pos.xy - t_params.zw, fx_depth.Load(int3(p, 0)));
	const float4 clip = mul(float4(P, 1.0), t_reproj);
	if (clip.w <= 1e-4) return float4(c, 1.0);
	const float2 ndc = clip.xy / clip.w;
	const float2 size = e_view.zw - e_view.xy;
	const float2 prev = e_view.xy + float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * size;
	if (any(prev < e_rect.xy) || any(prev > e_rect.zw)) return float4(c, 1.0);

	// Clip the history toward the mean until it lies inside mean +- sigma.
	float3 h = To_YCoCg(History_At(prev));
	const float3 d = h - m1;
	const float3 extent = max(sigma, 1.0 / 512.0);
	const float3 t = abs(d) / extent;
	const float k = max(t.x, max(t.y, t.z));
	if (k > 1.0) h = m1 + d / k;
	return float4(lerp(From_YCoCg(h), c, t_params.x), 1.0);
}
#else // TAA_PASS == 1
float4 main(float4 pos : SV_Position) : SV_Target
{
	return Fx_Scene_Out(float4(fx_cur.Load(int3(int2(pos.xy), 0)).rgb, 1.0));
}
#endif
