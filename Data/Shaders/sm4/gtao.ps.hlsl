/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// gtao.ps.hlsl -----------------------------------------------------------------------------------
//
// Ground truth ambient occlusion (GTAO, after Jimenez et al. 2016, "Practical Real-Time Strategies for Accurate
// Indirect Occlusion"; own implementation of the published method) at full resolution, in place of ssao.ps.hlsl.
// Same inputs as SSAO: the depth copy (t1), normals from the depth of neighbor pixels, radius, strength and range
// from e_ssao. Two passes, selected by GTAO_PASS:
//
//   0  Per pixel two slices through the view vector; per slice and side the highest horizon along the screen
//      direction (six steps up to the radius, falling off toward its edge), then the cosine weighted visible arc
//      between the two horizons around the projected normal. Slice angle and step offset from a fixed 4x4 pattern
//      (reproducible image comparisons); the eight slices of a 4x4 block cover the half circle evenly.
//   1  4x4 average with depth weights (cancels the pattern), strength and distance fade; the output is multiplied
//      into the scene target.
// ------------------------------------------------------------------------------------------------

#include "effects.hlsli"

Texture2D        fx_ao    : register(t0);	///< pass 1: visibility from pass 0
Texture2D<float> fx_depth : register(t1);	///< depth copy at full resolution

static const float PI = 3.14159265;

// arccos to about 1e-4 (Lagarde); the intrinsic is missing in some HLSL compilers (vkd3d-shader).
float Fast_Acos(float x)
{
	const float a = abs(x);
	const float r = (-0.156583 * a + PI * 0.5) * sqrt(saturate(1.0 - a));
	return (x >= 0.0) ? r : PI - r;
}

float W_At(int2 p)
{
	p = clamp(p, int2(e_rect.xy), int2(e_rect.zw) - 1);
	return Depth_To_W(fx_depth.Load(int3(p, 0)), e_depthRange, e_projB);
}

float3 Pos_At(int2 p)
{
	p = clamp(p, int2(e_rect.xy), int2(e_rect.zw) - 1);
	return View_Position(float2(p) + 0.5, fx_depth.Load(int3(p, 0)));
}

#if GTAO_PASS == 0
static const int SLICES = 2;
static const int STEPS = 6;

// Horizon cosine of one sample, pulled down to the lowest possible value as it leaves the radius. Outside the view
// rectangle nothing is known: no occluder (a clamped sample would repeat the edge row as a wall).
float Sample_Horizon(float3 P, float3 V, int2 q, float low, float falloff_mul, float falloff_add)
{
	if (any(q < int2(e_rect.xy)) || any(q >= int2(e_rect.zw))) return low;
	const float3 d = Pos_At(q) - P;
	const float len = length(d);
	const float c = (len > 1e-4) ? dot(d, V) / len : low;
	return lerp(low, c, saturate(len * falloff_mul + falloff_add));
}

float main(float4 pos : SV_Position) : SV_Target
{
	const int2 p = int2(pos.xy);
	const float d = fx_depth.Load(int3(p, 0));
	if (d >= 0.999999) return 1.0;			// sky/cleared
	const float w = Depth_To_W(d, e_depthRange, e_projB);
	if (w >= e_ssao.w) return 1.0;

	const float3 P = Pos_At(p);
	const float3 Pr = Pos_At(p + int2(1, 0)), Pl = Pos_At(p - int2(1, 0));
	const float3 Pd = Pos_At(p + int2(0, 1)), Pu = Pos_At(p - int2(0, 1));
	float3 n = normalize(cross(Neighbor_Step(P, Pr, Pl), Neighbor_Step(P, Pd, Pu)));
	if (dot(n, P) > 0.0) n = -n;			// toward the camera (origin)
	const float3 V = normalize(-P);

	const float radius = e_ssao.x;
	const float radius_px = clamp(radius * e_projA.x / w * 0.5 * (e_rect.z - e_rect.x), 4.0, 96.0);
	// Full weight up to 40 % of the radius, then linear to zero at the radius.
	const float falloff_range = 0.6 * radius;
	const float falloff_mul = -1.0 / falloff_range;
	const float falloff_add = (radius - falloff_range) / falloff_range + 1.0;

	static const float BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
	const float noise = (BAYER[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0;
	const float jitter = frac(noise * 4.0 + 0.37);

	float visibility = 0.0;
	[unroll]
	for (int s = 0; s < SLICES; ++s) {
		const float phi = (s + noise) * (PI / SLICES);
		const float2 dir = float2(cos(phi), sin(phi));
		const float2 omega = float2(dir.x, -dir.y);			// screen y runs down, camera y up
		const float3 dir_v = float3(dir, 0.0);
		const float3 ortho = dir_v - dot(dir_v, V) * V;
		const float3 axis = normalize(cross(ortho, V));
		const float3 proj_n = n - axis * dot(n, axis);
		const float proj_len = length(proj_n);
		if (proj_len < 1e-4) continue;
		const float cos_n = saturate(dot(proj_n, V) / proj_len);
		const float n_angle = ((dot(ortho, proj_n) < 0.0) ? -1.0 : 1.0) * Fast_Acos(cos_n);
		const float low0 = cos(n_angle + PI * 0.5);
		const float low1 = cos(n_angle - PI * 0.5);
		float h0 = low0, h1 = low1;
		[unroll]
		for (int i = 0; i < STEPS; ++i) {
			const float t = (i + jitter) / STEPS;
			const float2 o = omega * (t * t * radius_px + 1.0);	// denser near the pixel
			const int2 off = int2(round(o));
			h0 = max(h0, Sample_Horizon(P, V, p + off, low0, falloff_mul, falloff_add));
			h1 = max(h1, Sample_Horizon(P, V, p - off, low1, falloff_mul, falloff_add));
		}
		// Side +omega bounds the arc toward dir (positive angle), side -omega the other way.
		float a1 = Fast_Acos(clamp(h0, -1.0, 1.0));
		float a0 = -Fast_Acos(clamp(h1, -1.0, 1.0));
		a0 = n_angle + clamp(a0 - n_angle, -PI * 0.5, PI * 0.5);
		a1 = n_angle + clamp(a1 - n_angle, -PI * 0.5, PI * 0.5);
		const float sin_n = sin(n_angle);
		const float arc0 = (cos_n + 2.0 * a0 * sin_n - cos(2.0 * a0 - n_angle)) * 0.25;
		const float arc1 = (cos_n + 2.0 * a1 * sin_n - cos(2.0 * a1 - n_angle)) * 0.25;
		visibility += proj_len * (arc0 + arc1);
	}
	return saturate(visibility / SLICES);
}
#else // GTAO_PASS == 1
static const float GTAO_THRESHOLD = 0.1;

float4 main(float4 pos : SV_Position) : SV_Target
{
	const int2 p = int2(pos.xy);
	const float w0 = W_At(p);
	float sum = 0.0, weight = 0.0;
	[unroll]
	for (int y = -2; y < 2; ++y) {
		[unroll]
		for (int x = -2; x < 2; ++x) {
			const int2 q = clamp(p + int2(x, y), int2(e_rect.xy), int2(e_rect.zw) - 1);
			const float g = 1.0 / (1.0 + abs(W_At(q) - w0) / (0.02 * w0 + 0.5));
			sum += fx_ao.Load(int3(q, 0)).r * g;
			weight += g;
		}
	}
	const float visibility = sum / weight;
	// Weak occlusion (terrain undulation, the slight bias of normals from depth) is dropped below a threshold, so open
	// ground and roofs keep their brightness; strength as an exponent, darkening capped at 50 %, faded out in the distance.
	const float occlusion = max(0.0, (1.0 - visibility) - GTAO_THRESHOLD) / (1.0 - GTAO_THRESHOLD);
	float ao = max(pow(1.0 - occlusion, e_ssao.y), 0.5);
	ao = lerp(ao, 1.0, saturate((w0 - 0.75 * e_ssao.w) / (0.25 * e_ssao.w)));
	return float4(ao, ao, ao, 1.0);
}
#endif
