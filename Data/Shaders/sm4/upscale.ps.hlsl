/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// upscale.ps.hlsl --------------------------------------------------------------------------------
//
// Internal render resolution: the 3D scene was drawn into a smaller rectangle at the top left of the
// scene target (u_srcRect); this pass scales it up to the view rectangle (u_dstRect) before the 2D
// interface is drawn. With supersampling the source rectangle is larger and filter 3 scales it down.
// The source is a copy of the scene target (t0), sampled bilinearly (s0).
// UPSCALE_FILTER selects the filter:
//
//   0  bilinear (one tap)
//   1  bicubic Catmull-Rom: four taps per axis folded into bilinear taps; the four corner taps are
//      dropped and the weights renormalised (five taps)
//   2  Catmull-Rom as in 1, then contrast adaptive sharpening after AMD FidelityFX CAS (MIT license, own
//      implementation of the published method) from the four direct neighbours one source pixel away:
//      the sharpness follows the distance of the local min/max to 0 or 1, so nothing overshoots
//   3  downsampling (supersampling): mean of a 4 x 4 grid of bilinear taps over the footprint of the
//      destination pixel in the source, a box filter for factors up to 2 (each tap averages 2 x 2 pixels)
//
// All taps are clamped to the source rectangle; outside of it lies whatever the target held before.
// ------------------------------------------------------------------------------------------------

#ifndef UPSCALE_FILTER
#define UPSCALE_FILTER 1
#endif

#include "slots.hlsli"
cbuffer cbUpscale : register(SLOT_CB_UPSCALE)
{
	float4	u_source;	///< size of the source texture: x = 1/width, y = 1/height, z = width, w = height
	float4	u_srcRect;	///< source rectangle in pixels: x0, y0, x1, y1 (exclusive)
	float4	u_dstRect;	///< destination rectangle in pixels: x0, y0, x1, y1 (exclusive)
	float4	u_params;	///< x = sharpness 0..1 (filter 2)
};

Texture2D		u_scene  : register(t0);
SamplerState	u_linear : register(s0);

float4 Tap(float2 p)
{
	p = clamp(p, u_srcRect.xy + 0.5, u_srcRect.zw - 0.5);
	return u_scene.SampleLevel(u_linear, p * u_source.xy, 0.0);
}

float4 Catmull_Rom(float2 p)
{
	const float2 c  = floor(p - 0.5) + 0.5;		// centre of the source pixel left of / above p
	const float2 f  = p - c;
	const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	const float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	const float2 w3 = f * f * (-0.5 + 0.5 * f);
	const float2 w12 = w1 + w2;
	const float2 p0  = c - 1.0;
	const float2 p3  = c + 2.0;
	const float2 p12 = c + w2 / w12;
	const float k0 = w12.x * w0.y;
	const float k1 = w0.x * w12.y;
	const float k2 = w12.x * w12.y;
	const float k3 = w3.x * w12.y;
	const float k4 = w12.x * w3.y;
	const float4 sum = Tap(float2(p12.x, p0.y)) * k0 + Tap(float2(p0.x, p12.y)) * k1 + Tap(p12) * k2
					 + Tap(float2(p3.x, p12.y)) * k3 + Tap(float2(p12.x, p3.y)) * k4;
	return sum / (k0 + k1 + k2 + k3 + k4);
}

float4 main(float4 pos : SV_Position) : SV_Target
{
	const float2 t = (pos.xy - u_dstRect.xy) / (u_dstRect.zw - u_dstRect.xy);
	const float2 p = u_srcRect.xy + t * (u_srcRect.zw - u_srcRect.xy);

#if UPSCALE_FILTER == 0
	return Tap(p);
#elif UPSCALE_FILTER == 3
	const float2 foot = (u_srcRect.zw - u_srcRect.xy) / (u_dstRect.zw - u_dstRect.xy);
	float4 sum = 0.0;
	[unroll] for (int j = 0; j < 4; ++j)
	{
		[unroll] for (int i = 0; i < 4; ++i)
			sum += Tap(p + (float2(i, j) * 0.25 - 0.375) * foot);
	}
	return sum * (1.0 / 16.0);
#else
	float4 c = saturate(Catmull_Rom(p));
#if UPSCALE_FILTER == 2
	const float3 n = Tap(p + float2(0.0, -1.0)).rgb;
	const float3 s = Tap(p + float2(0.0,  1.0)).rgb;
	const float3 w = Tap(p + float2(-1.0, 0.0)).rgb;
	const float3 e = Tap(p + float2( 1.0, 0.0)).rgb;
	const float3 mn = min(c.rgb, min(min(n, s), min(w, e)));
	const float3 mx = max(c.rgb, max(max(n, s), max(w, e)));
	const float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1.0 / 65536.0)));
	const float3 wt = amp * (-1.0 / lerp(8.0, 5.0, saturate(u_params.x)));
	c.rgb = saturate((c.rgb + (n + s + w + e) * wt) / (1.0 + 4.0 * wt));
#endif
	return c;
#endif
}
