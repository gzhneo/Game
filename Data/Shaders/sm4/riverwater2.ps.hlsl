/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// riverwater2.ps.hlsl (SM 4.0)
//
// Extended river water (W3DWater.cpp drawRiverWater, setupRiverWater2); without it riverwater.ps.hlsl draws.
// River color and opacity stay as in the EA look (river texture times vertex color, alpha edge), plus:
//   - flow direction from the river geometry: the river texture runs along v, the direction is the gradient
//     of v in world coordinates (from ddx/ddy), i.e. a flow map without its own texture;
//   - waves (wellen.dds) offset along that direction in two phases (flow map method, against distortion);
//   - environment map reflection by Fresnel and sun glint as for still water;
//   - foam at shores and obstacles from the depth copy.
// Constants as in realwater.ps.hlsl (mode 2, RenderWater2PS), plus w2DiagFlow: x river texture speed (v per second),
// y flow map phase (0..1), z seconds per flow map cycle.
// ------------------------------------------------------------------------------------------------

#include "common.hlsli"

// constants of water version 2 (renderpieces.h)
cbuffer RenderWater2PS : register(SLOT_CB_PIECE_PS)
{
	float4 unused0[5];
	float4 w2Screen;
	float4 w2AxisZ;
	float4 w2AxisX;
	float4 w2AxisY;
	float4 w2Sigma;
	float4 w2Scroll;
	float4 w2Wave;
	float4 w2Sky;
	float4 w2Sun;
	float4 w2SunColor;
	float4 w2Env;
	float4 w2Light;
	float4 w2Scale;
	float4 w2Move;
	float4 w2Move2;
	float4 w2Amp;
	float4 w2Foam;
	float4 w2Depth;
	float4 w2Ssr;
	float4 w2Shade;
	float4 w2DiagFlow;
	float4 w2Wind;
	float4 w2Wind2;
};

Texture2D    texRiver   : register(t0);
Texture2D    texFoam    : register(t1);
Texture2D    texEnv     : register(t2);
Texture2D    texEdge    : register(t3);
Texture2D    texWaves   : register(t5);
Texture2D    texWind    : register(t6);
Texture2D    texDepth   : register(t7);
SamplerState samp0 : register(s0);
SamplerState samp1 : register(s1);
SamplerState samp2 : register(s2);
SamplerState samp3 : register(s3);
SamplerState samp5 : register(s5);
SamplerState samp6 : register(s6);

float2 Slope(float2 world, float2 flow, float phase, float4 cScale, float4 cMove, float4 cAmp)
{
	float2 w = world - flow * phase;
	float2 a = (texWaves.Sample(samp5, w * cScale.x + cMove.xy).rg - 0.5) * 6.0;
	float2 b = (texWaves.Sample(samp5, w * cScale.y + cMove.zw).ba - 0.5) * 6.0;
	return a * cAmp.x + b * cAmp.y;
}

float4 main(VS_OUT i) : SV_Target
{
	const float4 cProj2  = f_proj[2];
	const float4 cProj3  = f_proj[3];
	const float4 cUp     = float4(f_view[2].xyz, w2AxisZ.w);	// world axes in camera space (view matrix rows)
	const float4 cAxisX  = float4(f_view[0].xyz, w2AxisX.w);
	const float4 cAxisY  = float4(f_view[1].xyz, w2AxisY.w);
	const float4 cSun    = w2Sun;
	const float4 cSunCol = w2SunColor;
	const float4 cEnv    = w2Env;
	const float4 cLight  = w2Light;
	const float4 cScale  = w2Scale;
	const float4 cMove   = w2Move;
	const float4 cMove2  = w2Move2;
	const float4 cAmp    = w2Amp;
	const float4 cFoamP  = w2Foam;
	const float4 cDepthP = float4(f_depthRange.x, f_depthRange.z, w2Depth.zw);
	const float4 cShade  = w2Shade;
	const float4 cFlow   = w2DiagFlow;
	const float3 posView = i.uv1.xyz;

	float4 t0 = texRiver.Sample(samp0, i.uv0.xy);
	float4 t3 = texEdge.Sample(samp3, i.uv3.xy);
	float4 body = i.color0 * t0;				// as riverwater.ps.hlsl
	body.rgb += t3.rgb;
	body.a *= t3.a;

	float2 world = (i.uv2.xy - cMove2.z) * 16.0;

	// flow direction: gradient of v (along-river coordinate) in world coordinates
	float2 dwx = ddx(world), dwy = ddy(world);
	float dvx = ddx(i.uv0.y), dvy = ddy(i.uv0.y);
	float det = dwx.x * dwy.y - dwx.y * dwy.x;
	float2 g = (abs(det) > 1e-8) ? float2(dvx * dwy.y - dvy * dwx.y, dwx.x * dvy - dwy.x * dvx) / det : float2(0.0, 0.0);
	float2 flow = (dot(g, g) > 1e-12) ? normalize(g) : float2(0.0, 0.0);

	// Waves in two phases along the current, cross-faded. Speed matches the river texture: cFlow.x v per second
	// divided by |g| (v per world unit), times cFlow.z seconds per cycle.
	float glen = length(g);
	float dist = (glen > 1e-6) ? min(cFlow.x / glen * cFlow.z, 40.0) : 0.0;
	float2 flowOff = flow * dist;
	float p0 = cFlow.y, p1 = frac(cFlow.y + 0.5);
	float2 s0 = Slope(world, flowOff, p0, cScale, cMove, cAmp);
	float2 s1 = Slope(world + 17.3, flowOff, p1, cScale, cMove, cAmp);
	float wgt = abs(1.0 - 2.0 * p0);
	float2 slope = lerp(s1, s0, wgt) * 1.2;
	// uneven on rivers too (gust fields as for still water, weaker)
	float4 wf = texWind.Sample(samp6, world * w2Wind.z + w2Wind2.xy);
	slope *= lerp(0.55, 1.2, smoothstep(0.3, 0.75, wf.r));

	float3 V = normalize(-posView);
	float3 N = normalize(cUp.xyz - slope.x * cAxisX.xyz - slope.y * cAxisY.xyz);
	float cosT = saturate(dot(N, V));
	float f = 1.0 - cosT;
	float f2 = f * f;
	float fresnel = cUp.w + (1.0 - cUp.w) * f2 * f2 * f;

	float3 R = reflect(-V, N);
	float3 Rw = float3(dot(R, cAxisX.xyz), dot(R, cAxisY.xyz), dot(R, cUp.xyz));
	Rw.z = max(Rw.z, 0.02);
	Rw = normalize(Rw);
	// Upper hemisphere stereographic, rotated to the map's sun direction (cEnv.xy = cos, sin)
	float2 Rr = float2(cEnv.x * Rw.x + cEnv.y * Rw.y, cEnv.x * Rw.y - cEnv.y * Rw.x);
	float2 uvE = Rr / (1.0 + Rw.z) * 0.5 + 0.5;
	float3 sky = texEnv.SampleLevel(samp2, uvE, cShade.z).rgb * 2.0 * cLight.rgb;

	// depth at shores and obstacles (only with the depth copy)
	float depthF = 100.0;
	if (cEnv.w > 0.5)
	{
		float d = texDepth.Load(int3(i.pos.xy, 0)).r;
		float ndc = (d - cDepthP.x) * cDepthP.y;
		float zg = (cProj3.z - ndc * cProj3.w) / (ndc * cProj2.w - cProj2.z);
		depthF = max((zg / posView.z - 1.0) * dot(posView, -cUp.xyz), 0.0);
	}

	// Horizontal distance to the waterline: depth divided by the ground slope (from the derivatives of depth over
	// world position). Otherwise the depth band is only a few pixels wide at steep shores and very wide at flat ones.
	float2 wdx = ddx(world), wdy = ddy(world);
	float gdx = ddx(depthF), gdy = ddy(depthF);
	float wdet = wdx.x * wdy.y - wdx.y * wdy.x;
	float2 grad = (abs(wdet) > 1e-6) ? float2(gdx * wdy.y - gdy * wdx.y, wdx.x * gdy - wdy.x * gdx) / wdet : float2(0.0, 0.0);
	float shoreDist = depthF / clamp(length(grad), 0.08, 1.5);

	float3 col = lerp(body.rgb, sky, saturate(fresnel * w2Sigma.w * w2Ssr.w * saturate(depthF * 2.0)));
	float3 L = cSun.xyz;
	float3 H = normalize(L + V);
	float nh = saturate(dot(N, H));
	float fh = 1.0 - saturate(dot(H, V));
	float fH = 0.02 + 0.98 * fh * fh * fh * fh * fh;
	float spec = (pow(nh, 900.0) * 115.0 + pow(nh, 90.0) * 12.0 * cSunCol.w) * fH;
	col += cSunCol.rgb * (spec * cSun.w * saturate(depthF / cShade.y));

	// Foam: only sparse at obstacles and shores (white water patchy, calm shores without a fringe), pattern moved
	// with the current, large gaps from the wind field
	if (cLight.w > 0.0 && cEnv.w > 0.5)
	{
		float edge = saturate(1.0 - shoreDist / cFoamP.x);
		float2 fuv = world * cFoamP.w;
		float2 fl = flowOff * cFoamP.w;
		float pat0 = texFoam.Sample(samp1, fuv - fl * p0).r;
		float pat1 = texFoam.Sample(samp1, fuv + 0.37 - fl * p1).r;
		float pat = lerp(pat1, pat0, wgt);
		pat *= smoothstep(0.4, 0.7, wf.b);
		float foam = smoothstep(1.0 - edge * 0.45 - 0.1, 1.0 - edge * 0.45 + 0.25, pat) * cLight.w * saturate(shoreDist * 1.5) * 0.8;
		float3 foamCol = saturate(cSunCol.rgb * 0.55 + cLight.rgb * 0.45) * 0.92;
		col = lerp(col, foamCol, foam);
		body.a = max(body.a, foam * t3.a);
	}
	return Scene_Out(float4(col, body.a));
}
