/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// post.ps.hlsl -----------------------------------------------------------------------------------
//
// Post processing of the 3D scene before the 2D interface. One source (t0), one target; the
// macros select which steps the pass computes (the backend compiles one permutation per
// combination):
//
//   DO_FXAA   Anti-aliasing after the FXAA method (Lottes 2009/2011): find the edge from the
//             luminance gradient, search along it to both ends, then sample offset across the
//             edge by the matching fraction of a pixel; plus a subpixel term for isolated
//             bright/dark pixels. Own implementation.
//   DO_CAS    Contrast adaptive sharpening after AMD FidelityFX CAS (MIT license, own
//             implementation of the published method): per-pixel sharpness follows the distance
//             of the local min/max to 0 or 1, so nothing overshoots.
//   DO_GRADE  Color grading: saturation that spares already strong colors, soft S-curve for
//             contrast, slight warmth shift.
//   DO_HAZE   Distance haze: fades far terrain toward a light blue-grey by the distance along the
//             view axis (depth copy in t1, valid only with the opaque-scene depth copy of this frame).
//             Runs on the finished color of the pass, before grading. Skipped where nothing was drawn.
//   DO_LIGHTS Dynamic point lights from explosions, fire and muzzle flashes: each pixel's camera space
//             position comes from the depth copy (t1), the surface normal from its screen space
//             derivatives, and the lights (cbDynLights, camera space) are added, tinted by the color
//             already in the image as a stand-in for the surface color. Runs before the haze.
//   DO_DEBAND Dithering: triangular noise of one 8-bit step (two interleaved gradient noise values, fixed per
//             pixel, so image comparisons stay reproducible) before the pass is stored in 8 bits; gradients in
//             sky, haze and water lose their steps. Always the last step of the last pass.
//   DO_GAMMA  Brightness control: the same curve as the Set_Gamma gamma ramp, equal per color
//             channel. A separate pass from RenderCore::Present, never combined with the other
//             three macros -- those run before the 2D interface (Scene_3D_Done), the gamma curve
//             after it, on the finished image.
//
// FXAA reads several samples along the edge and therefore always runs as the first pass on the
// unmodified image; CAS and grading follow.
// ------------------------------------------------------------------------------------------------

#include "effects.hlsli"

Texture2D    fx_source : register(t0);
#if defined(DO_HAZE) || defined(DO_LIGHTS)
Texture2D<float> fx_depth : register(t1);
#endif
SamplerState fx_linear : register(s0);

float3 Tex(float2 uv)
{
	const float2 lo = (e_rect.xy + 0.5) * e_source.xy;
	const float2 hi = (e_rect.zw - 0.5) * e_source.xy;
	return fx_source.SampleLevel(fx_linear, clamp(uv, lo, hi), 0).rgb;
}

float Luma(float3 c)
{
	return dot(c, float3(0.299, 0.587, 0.114));
}

#ifdef DO_FXAA
// Search step sizes along the edge (in pixels), as quality preset 29 of FXAA 3.11.
static const float FXAA_STEPS[12] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0 };
#define FXAA_EDGE_THRESHOLD      0.125		// relative minimum contrast of an edge
#define FXAA_EDGE_THRESHOLD_MIN  0.0312		// absolute minimum contrast (dark areas)
#define FXAA_SUBPIXEL            0.75		// amount of subpixel smoothing

float3 Fxaa(float2 uv)
{
	const float2 inv = e_source.xy;
	const float3 rgbM = Tex(uv);
	const float lM = Luma(rgbM);
	const float lN = Luma(Tex(uv + float2( 0, -1) * inv));
	const float lS = Luma(Tex(uv + float2( 0,  1) * inv));
	const float lW = Luma(Tex(uv + float2(-1,  0) * inv));
	const float lE = Luma(Tex(uv + float2( 1,  0) * inv));

	const float lMax = max(lM, max(max(lN, lS), max(lW, lE)));
	const float lMin = min(lM, min(min(lN, lS), min(lW, lE)));
	const float range = lMax - lMin;
	if (range < max(FXAA_EDGE_THRESHOLD_MIN, lMax * FXAA_EDGE_THRESHOLD))
		return rgbM;

	const float lNW = Luma(Tex(uv + float2(-1, -1) * inv));
	const float lNE = Luma(Tex(uv + float2( 1, -1) * inv));
	const float lSW = Luma(Tex(uv + float2(-1,  1) * inv));
	const float lSE = Luma(Tex(uv + float2( 1,  1) * inv));

	const float lNS = lN + lS;
	const float lWE = lW + lE;
	const float lWCorners = lNW + lSW;
	const float lECorners = lNE + lSE;
	const float lNCorners = lNW + lNE;
	const float lSCorners = lSW + lSE;

	// Horizontal edge = luminance changes vertically.
	const float edgeH = abs(-2.0 * lW + lWCorners) + abs(-2.0 * lM + lNS) * 2.0 + abs(-2.0 * lE + lECorners);
	const float edgeV = abs(-2.0 * lN + lNCorners) + abs(-2.0 * lM + lWE) * 2.0 + abs(-2.0 * lS + lSCorners);
	const bool horizontal = edgeH >= edgeV;

	// Neighbors across the edge: 1 = negative direction, 2 = positive direction.
	const float l1 = horizontal ? lN : lW;
	const float l2 = horizontal ? lS : lE;
	const float g1 = l1 - lM;
	const float g2 = l2 - lM;
	const bool steep1 = abs(g1) >= abs(g2);
	const float gradientScaled = 0.25 * max(abs(g1), abs(g2));

	float stepLength = horizontal ? inv.y : inv.x;
	float lLocalAverage;
	if (steep1) {
		stepLength = -stepLength;
		lLocalAverage = 0.5 * (l1 + lM);
	}
	else {
		lLocalAverage = 0.5 * (l2 + lM);
	}

	// Move onto the edge itself (half a pixel across) and search both directions from there.
	float2 edgeUv = uv;
	if (horizontal) edgeUv.y += stepLength * 0.5;
	else            edgeUv.x += stepLength * 0.5;
	const float2 offset = horizontal ? float2(inv.x, 0.0) : float2(0.0, inv.y);

	float2 uv1 = edgeUv - offset * FXAA_STEPS[0];
	float2 uv2 = edgeUv + offset * FXAA_STEPS[0];
	float end1 = Luma(Tex(uv1)) - lLocalAverage;
	float end2 = Luma(Tex(uv2)) - lLocalAverage;
	bool reached1 = abs(end1) >= gradientScaled;
	bool reached2 = abs(end2) >= gradientScaled;

	[loop]
	for (int i = 1; i < 12 && !(reached1 && reached2); ++i) {
		if (!reached1) {
			uv1 -= offset * FXAA_STEPS[i];
			end1 = Luma(Tex(uv1)) - lLocalAverage;
			reached1 = abs(end1) >= gradientScaled;
		}
		if (!reached2) {
			uv2 += offset * FXAA_STEPS[i];
			end2 = Luma(Tex(uv2)) - lLocalAverage;
			reached2 = abs(end2) >= gradientScaled;
		}
	}

	const float dist1 = horizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
	const float dist2 = horizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
	const bool toward1 = dist1 < dist2;
	const float distFinal = min(dist1, dist2);
	const float thickness = dist1 + dist2;
	const float pixelOffset = -distFinal / max(thickness, 1e-8) + 0.5;

	// Offset only if the nearer edge end points in the matching direction.
	const bool centerSmaller = lM < lLocalAverage;
	const bool goodVariation = ((toward1 ? end1 : end2) < 0.0) != centerSmaller;
	float finalOffset = goodVariation ? pixelOffset : 0.0;

	// Subpixel term: deviation of the pixel from the weighted mean of its neighbors.
	const float lAverage = (1.0 / 12.0) * (2.0 * (lNS + lWE) + lWCorners + lECorners);
	const float sub1 = saturate(abs(lAverage - lM) / range);
	const float sub2 = (-2.0 * sub1 + 3.0) * sub1 * sub1;
	finalOffset = max(finalOffset, sub2 * sub2 * FXAA_SUBPIXEL);

	float2 finalUv = uv;
	if (horizontal) finalUv.y += finalOffset * stepLength;
	else            finalUv.x += finalOffset * stepLength;
	return Tex(finalUv);
}
#endif // DO_FXAA

#ifdef DO_CAS
float3 Cas(float2 uv)
{
	const float2 inv = e_source.xy;
	const float3 a = Tex(uv + float2(-1, -1) * inv);
	const float3 b = Tex(uv + float2( 0, -1) * inv);
	const float3 c = Tex(uv + float2( 1, -1) * inv);
	const float3 d = Tex(uv + float2(-1,  0) * inv);
	const float3 e = Tex(uv);
	const float3 f = Tex(uv + float2( 1,  0) * inv);
	const float3 g = Tex(uv + float2(-1,  1) * inv);
	const float3 h = Tex(uv + float2( 0,  1) * inv);
	const float3 i = Tex(uv + float2( 1,  1) * inv);

	// Soft minimum/maximum: cross plus all nine (range 0..2).
	float3 mn = min(min(min(d, e), min(f, b)), h);
	float3 mx = max(max(max(d, e), max(f, b)), h);
	mn += min(mn, min(min(a, c), min(g, i)));
	mx += max(mx, max(max(a, c), max(g, i)));

	// Headroom to 0 or 1 sets the sharpness; sqrt softens the transition.
	const float3 amp = sqrt(saturate(min(mn, 2.0 - mx) / max(mx, 1e-5)));
	const float peak = -1.0 / lerp(8.0, 5.0, saturate(e_post.x));
	const float3 w = amp * peak;
	return saturate((b * w + d * w + f * w + h * w + e) / (1.0 + 4.0 * w));
}
#endif // DO_CAS

#ifdef DO_GRADE
float3 Smooth(float3 x)
{
	return x * x * (3.0 - 2.0 * x);
}

float3 Grade(float3 c)
{
	c = saturate(c);
	const float l = dot(c, float3(0.2126, 0.7152, 0.0722));
	// Saturation: raise already strong colors less (otherwise team colors and fire clip).
	const float chroma = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
	const float s = 1.0 + (e_post.y - 1.0) * (1.0 - chroma);
	c = saturate(lerp(l.xxx, c, s));
	// Contrast: soft S-curve around the pivot 0.32 (about the mean brightness of game scenes), black
	// and white stay fixed. Pivoting near the mean keeps the mostly dark images from getting darker.
	const float pivot = 0.32;
	const float3 lo = pivot * 2.0 * Smooth(saturate(c / pivot) * 0.5);
	const float3 hi = pivot + (1.0 - pivot) * 2.0 * (Smooth(0.5 + saturate((c - pivot) / (1.0 - pivot)) * 0.5) - 0.5);
	const float3 curve = (c < pivot) ? lo : hi;
	c = lerp(c, curve, e_post.z);
	// Warmth: red slightly up, blue slightly down.
	c *= float3(1.0 + e_post.w, 1.0, 1.0 - e_post.w);
	return saturate(c);
}
#endif // DO_GRADE

#ifdef DO_HAZE
float3 Haze(float3 c, int2 p)
{
	const float d = fx_depth.Load(int3(p, 0));
	if (d >= 0.99999)
		return c;					// nothing drawn here (clear color)
	const float w = Depth_To_W(d, e_depthRange, e_projB);
	const float t = saturate((w - e_haze.y) * e_haze.z);
	const float f = e_haze.x * t * t * (3.0 - 2.0 * t);
	// Dark pixels get a dark haze, so night maps are not lifted to grey.
	const float l = dot(c, float3(0.299, 0.587, 0.114));
	const float3 tint = float3(0.62, 0.72, 0.86) * lerp(0.35, 1.0, saturate(l * 1.5));
	return lerp(c, tint, f);
}
#endif // DO_HAZE

#ifdef DO_LIGHTS
#define DYN_LIGHT_MAX 32
#include "slots.hlsli"
cbuffer cbDynLights : register(SLOT_CB_DYN_LIGHTS)
{
	float4	dl_info;				///< x = count, y = strength
	float4	dl_pos[DYN_LIGHT_MAX];	///< xyz camera space position, w = radius
	float4	dl_color[DYN_LIGHT_MAX];///< rgb color times intensity
};

float3 Dyn_Lights(float3 c, int2 p)
{
	// Derivatives need uniform control flow, so they come before the early return.
	const float d = fx_depth.Load(int3(p, 0));
	const float3 pos = View_Position(float2(p) + 0.5, d);
	float3 n = normalize(cross(ddx(pos), ddy(pos)) + 1e-9);
	if (dot(n, pos) > 0.0) n = -n;					// face the camera
	if (d >= 0.99999)
		return c;									// nothing drawn here (clear color)

	float3 sum = 0.0;
	const int count = min((int)dl_info.x, DYN_LIGHT_MAX);
	[loop]
	for (int i = 0; i < count; ++i) {
		const float3 to_light = dl_pos[i].xyz - pos;
		const float dist2 = dot(to_light, to_light);
		const float radius = dl_pos[i].w;
		if (dist2 >= radius * radius) continue;
		const float t = 1.0 - dist2 / (radius * radius);
		// Soft falloff; the normal term is wrapped so walls and slopes facing away still get some light.
		const float ndl = 0.35 + 0.65 * saturate(dot(n, to_light * rsqrt(dist2 + 1e-4)));
		sum += dl_color[i].rgb * (t * t * ndl);
	}
	// The pixel's own color stands in for the surface color (dark ground stays darker than bright sand).
	const float peak = max(c.r, max(c.g, c.b));
	// Black (shrouded) pixels stay black.
	const float3 albedo = (0.35 + 0.65 * (c / max(peak, 0.15))) * saturate(peak * 40.0);
	return c + sum * albedo * dl_info.y;
}
#endif // DO_LIGHTS

#ifdef DO_GAMMA
// Same formula as the Set_Gamma gamma ramp: input minus lower limit, to the power 1/gamma, times
// contrast plus brightness, equal per color channel (no detour through luminance).
float3 Gamma(float3 c)
{
	c = saturate(c - e_gamma.w);
	c = pow(max(c, 0.0), e_gamma.x);
	return saturate(e_gamma.z * c + e_gamma.y);
}
#endif // DO_GAMMA

#ifdef DO_DEBAND
float Gradient_Noise(float2 p)
{
	return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

float3 Deband(float3 c, float2 p)
{
	const float n = Gradient_Noise(p) + Gradient_Noise(p + float2(47.0, 17.0)) - 1.0;	// -1..1, triangular
	return saturate(c + n * (1.0 / 255.0));
}
#endif // DO_DEBAND

float4 main(float4 pos : SV_Position) : SV_Target
{
	const float2 uv = pos.xy * e_source.xy;
#if defined(DO_FXAA)
	float3 c = Fxaa(uv);
#elif defined(DO_CAS)
	float3 c = Cas(uv);
#else
	float3 c = Tex(uv);
#endif
#ifdef DO_LIGHTS
	c = Dyn_Lights(c, int2(pos.xy));
#endif
#ifdef DO_HAZE
	c = Haze(c, int2(pos.xy));
#endif
#ifdef DO_GRADE
	c = Grade(c);
#endif
#ifdef DO_GAMMA
	c = Gamma(c);
#endif
#ifdef DO_DEBAND
	c = Deband(c, pos.xy);
#endif
	return Fx_Scene_Out(float4(c, 1.0));
}
