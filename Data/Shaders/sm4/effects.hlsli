/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// effects.hlsli ----------------------------------------------------------------------------------
//
// Shared constant buffer and helpers of the Direct3D 11 effects: post processing (FXAA,
// sharpening, color grading, haze), bloom, heat shimmer, ambient occlusion (SSAO) and soft particles. The C++ counterpart
// is FFPEffectsCB in WW3D2/rendercb.h (static_assert on the size).
//
// Used only when the respective effect is enabled. Without effects, ffp.ps.hlsl and all other
// shaders run unchanged (soft particles are a separate permutation).
// ------------------------------------------------------------------------------------------------

#ifndef FFP_EFFECTS_HLSLI
#define FFP_EFFECTS_HLSLI

#include "slots.hlsli"
#include "sceneformat.hlsli"
cbuffer cbEffects : register(SLOT_CB_EFFECTS)
{
	float4	e_source;		///< source: x = 1/width, y = 1/height, z = width, w = height (pixels)
	float4	e_rect;			///< view rectangle in pixels: x0, y0, x1, y1 (x1/y1 exclusive)
	float4	e_projA;		///< projection of the depth copy: x = P11, y = P22, z = P31, w = P32
	float4	e_projB;		///< x = P33, y = P34, z = P43, w = P44
	float4	e_depthRange;	///< viewport of the depth copy: x = MinZ, y = MaxZ, z = 1/(MaxZ-MinZ)
	float4	e_ssao;			///< x = radius (world), y = strength, z = bias, w = range (faded out from 75 %)
	float4	e_post;			///< x = CAS sharpness 0..1, y = saturation, z = contrast 0..1, w = warmth
	float4	e_soft;			///< x = 1/fade distance (world), y = blend mode (0 alpha, 1 color+alpha, 2 multiplicative)
	float4	e_softProjB;	///< projection of the particle draw: x = P33, y = P34, z = P43, w = P44
	float4	e_softRange;	///< viewport of the particle draw: x = MinZ, y = MaxZ, z = 1/(MaxZ-MinZ)
	float4	e_gamma;		///< gamma: x = 1/gamma, y = brightness, z = contrast, w = lower limit
	float4	e_bloom;		///< x, y = 1/size of the source texture, z = bloom threshold (luma), w = knee
	float4	e_glow;			///< x = bloom strength, y = band-pass weight, z = heat amplitude (pixels), w = heat phase (0..2 pi)
	float4	e_haze;			///< x = strength, y = start distance (world), z = 1/(end - start), w = 1: float scene target
	float4	e_tone;			///< tone curve (sceneconvert TONEMAP): x = knee, y = exposure
	float4	e_view;			///< 3D viewport of the depth copy in pixels: x0, y0, x1, y1; the view rectangle (e_rect) of the
							///< post-processing may be smaller (the part above the command bar)
};

// Output of a pass that writes the scene or the post-processing target; rounded to 8 bits with a wider scene
// target (e_haze.w = 1), and to the R11G11B10 grid when the pass writes such a scene target (2; see Scene_Out
// in common.hlsli).
float4 Fx_Scene_Out(float4 c)
{
	if (e_haze.w > 0.5) c = round(saturate(c) * 255.0) / 255.0;
	if (e_haze.w > 1.5) c.rgb = Grid_R11G11B10(c.rgb);
	return c;
}

// Depth buffer value -> distance along the view axis (clip w). Valid for any D3D projection with
// row vectors: d = (z*P33 + P43) / (z*P34 + P44), solved for z; w = z*P34 + P44.
float Depth_To_W(float d_buffer, float4 range, float4 pB)
{
	const float d = (d_buffer - range.x) * range.z;
	const float z = (pB.z - d * pB.w) / (d * pB.y - pB.x);
	return z * pB.y + pB.w;
}

// Pixel position (center, in the scene target) and depth buffer value -> camera space position. The pixel maps to the
// projection over the 3D viewport of the depth copy (e_view), not over the view rectangle.
float3 View_Position(float2 pixel, float d_buffer)
{
	const float d = (d_buffer - e_depthRange.x) * e_depthRange.z;
	const float z = (e_projB.z - d * e_projB.w) / (d * e_projB.y - e_projB.x);
	const float w = z * e_projB.y + e_projB.w;
	const float2 size = e_view.zw - e_view.xy;
	const float2 ndc = float2((pixel.x - e_view.x) / size.x * 2.0 - 1.0,
							  1.0 - (pixel.y - e_view.y) / size.y * 2.0);
	return float3((ndc.x * w - z * e_projA.z) / e_projA.x,
				  (ndc.y * w - z * e_projA.w) / e_projA.y,
				  z);
}

// Step to the neighbor with the smaller depth difference (keeps silhouettes sharp), for normals from
// neighbor positions. At the image edge one neighbor is clamped to P itself; then the other one is
// taken, a zero step would give a NaN normal whose handling differs between drivers.
float3 Neighbor_Step(float3 P, float3 next, float3 prev)
{
	const bool use_next = all(prev == P) || (any(next != P) && abs(next.z - P.z) < abs(P.z - prev.z));
	return use_next ? next - P : P - prev;
}

#endif // FFP_EFFECTS_HLSLI
