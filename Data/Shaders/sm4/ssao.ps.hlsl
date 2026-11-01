/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ssao.ps.hlsl -----------------------------------------------------------------------------------
//
// Screen space ambient occlusion (SSAO), after the opaque part of the scene and before water,
// transparent objects and particles. Input is the depth copy (t1, R24 UNORM); normals are derived
// from the depth of neighbor pixels. Three passes, selected by SSAO_PASS:
//
//   0  Occlusion at half resolution: twelve samples on a spiral in the image plane, radius in world
//      units, per-pixel rotation from a 4x4 pattern (fixed so image comparisons stay reproducible).
//      Formula based on "Alchemy AO" (McGuire 2011).
//   1  4x4 average at half resolution with depth weights (cancels the 4x4 pattern).
//   2  Apply at full resolution: the four nearest half-resolution values, weighted by depth
//      similarity; the output is multiplied into the scene target.
// ------------------------------------------------------------------------------------------------

#include "effects.hlsli"

Texture2D        fx_ao    : register(t0);	///< pass 1/2: result of the previous pass
Texture2D<float> fx_depth : register(t1);	///< depth copy at full resolution

float Raw_Depth(int2 p)
{
	p = clamp(p, int2(e_rect.xy), int2(e_rect.zw) - 1);
	return fx_depth.Load(int3(p, 0));
}

float3 Pos_At(int2 p)
{
	p = clamp(p, int2(e_rect.xy), int2(e_rect.zw) - 1);
	return View_Position(float2(p) + 0.5, fx_depth.Load(int3(p, 0)));
}

float W_At(int2 p)
{
	return Depth_To_W(Raw_Depth(p), e_depthRange, e_projB);
}

#if SSAO_PASS == 0
float main(float4 pos : SV_Position) : SV_Target
{
	const int2 half_p = int2(pos.xy);
	const int2 p = half_p * 2;
	const float d = Raw_Depth(p);
	if (d >= 0.999999) return 1.0;			// sky/cleared

	const float3 P = Pos_At(p);
	const float w = Depth_To_W(d, e_depthRange, e_projB);
	if (w >= e_ssao.w) return 1.0;

	// Normal from the neighbors; per axis the side with the smaller depth step (edges).
	const float3 Pr = Pos_At(p + int2(1, 0)), Pl = Pos_At(p - int2(1, 0));
	const float3 Pd = Pos_At(p + int2(0, 1)), Pu = Pos_At(p - int2(0, 1));
	const float3 dx = Neighbor_Step(P, Pr, Pl);
	const float3 dy = Neighbor_Step(P, Pd, Pu);
	float3 n = normalize(cross(dx, dy));
	if (dot(n, P) > 0.0) n = -n;			// toward the camera (origin)

	// Radius of the world space circle in pixels.
	const float radius = e_ssao.x;
	const float radius_px = clamp(radius * e_projA.x / w * 0.5 * (e_rect.z - e_rect.x), 3.0, 64.0);

	// Fixed rotation from a 4x4 Bayer pattern.
	static const float BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
	const float rot = BAYER[(half_p.y & 3) * 4 + (half_p.x & 3)] * (6.2831853 / 16.0);

	const int N = 12;
	float occlusion = 0.0;
	[unroll]
	for (int i = 0; i < N; ++i) {
		const float t = (i + 0.5) / N;
		const float angle = i * 2.3999632 + rot;			// golden angle
		const float2 offs = float2(cos(angle), sin(angle)) * (t * radius_px);
		const float3 Q = Pos_At(p + int2(round(offs)));
		const float3 v = Q - P;
		const float vv = dot(v, v);
		const float vn = dot(v, n);
		// Contribution only above the surface; falls off toward the edge of the radius.
		occlusion += max(0.0, vn - e_ssao.z * w) / (vv + 0.01 * radius * radius)
					 * saturate(1.0 - vv / (radius * radius * 4.0)) * radius;
	}
	// Weak occlusion (terrain undulation, grazing angles) is dropped below a threshold so there is no gray
	// veil, only corners and contact points, and darkening is capped at 40 %. This keeps roofs and facades
	// clean and the mean brightness close to the unshaded image.
	const float o = e_ssao.y * occlusion * (1.2 / N);
	float ao = 1.0 - min(max(0.0, o - 0.15) * 0.8, 0.4);
	// Fade out in the distance (fog, detail levels).
	ao = lerp(ao, 1.0, saturate((w - 0.75 * e_ssao.w) / (0.25 * e_ssao.w)));
	return ao;
}
#elif SSAO_PASS == 1
float main(float4 pos : SV_Position) : SV_Target
{
	const int2 half_p = int2(pos.xy);
	const float w0 = W_At(half_p * 2);
	const int2 half_max = int2((e_rect.zw + 1.0) * 0.5) - 1;
	float sum = 0.0, weight = 0.0;
	[unroll]
	for (int y = -2; y < 2; ++y) {
		[unroll]
		for (int x = -2; x < 2; ++x) {
			const int2 q = clamp(half_p + int2(x, y), int2(e_rect.xy * 0.5), half_max);
			const float wq = W_At(q * 2);
			const float g = 1.0 / (1.0 + abs(wq - w0) / (0.02 * w0 + 0.5));
			sum += fx_ao.Load(int3(q, 0)).r * g;
			weight += g;
		}
	}
	return sum / weight;
}
#else // SSAO_PASS == 2
float4 main(float4 pos : SV_Position) : SV_Target
{
	const int2 p = int2(pos.xy);
	const float w0 = W_At(p);
	// The four nearest half-resolution values.
	const float2 hp = pos.xy * 0.5 - 0.5;
	const int2 base = int2(floor(hp));
	const float2 f = hp - base;
	const int2 half_max = int2((e_rect.zw + 1.0) * 0.5) - 1;
	float sum = 0.0, weight = 0.0;
	[unroll]
	for (int k = 0; k < 4; ++k) {
		const int2 o = int2(k & 1, k >> 1);
		const int2 q = clamp(base + o, int2(e_rect.xy * 0.5), half_max);
		const float bilinear = (o.x ? f.x : 1.0 - f.x) * (o.y ? f.y : 1.0 - f.y);
		const float g = bilinear / (1.0 + abs(W_At(q * 2) - w0) / (0.02 * w0 + 0.5)) + 1e-4;
		sum += fx_ao.Load(int3(q, 0)).r * g;
		weight += g;
	}
	const float ao = sum / weight;
	return float4(ao, ao, ao, 1.0);
}
#endif
