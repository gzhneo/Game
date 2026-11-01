/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// realwater.ps.hlsl (SM 4.0)
//
// Water surface with two modes. Mode 1 computes turbidity, Fresnel and refraction. Mode 2
// (W3DWater.cpp setupRealWater2) adds:
//   - waves from two counter-running layers of a slope map (sea spectrum, wellen.dds) at two scales, a third
//     rotated layer of the large waves, and large-scale variation against tiling,
//   - reflection of an environment map (CC0 HDRI per time of day, upper hemisphere stereographic, rotated to the
//     map's sun direction),
//   - sun glint from the map's light direction,
//   - foam and surf at the shore from the water depth (scene depth copy, else depth code in the target alpha),
//   - from quality level 2 on, refraction and (level 2) screen space reflection (SSR) falling back to the
//     environment map.
// If w2Env.z = 0 (mode 2 not set up), the shader computes mode 1.
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

Texture2D    texWater   : register(t0);
Texture2D    texFoam    : register(t1);		///< mode 1: sparkle; mode 2: schaum.dds (R, B foam; G variation)
Texture2D    texNoise   : register(t2);		///< mode 1: noise; mode 2: environment map (stereographic)
Texture2D    texShroud  : register(t3);
Texture2D    texScene   : register(t4);
Texture2D    texWaves   : register(t5);		///< mode 1: computed normals; mode 2: wellen.dds (slopes)
Texture2D    texWind    : register(t6);		///< mode 2: wind.dds (gusts, layer mix, ripple patches, color)
Texture2D    texDepth   : register(t7);		///< mode 2: scene depth copy (only if w2Env.w = 1)
SamplerState samp0 : register(s0);
SamplerState samp1 : register(s1);
SamplerState samp2 : register(s2);
SamplerState samp3 : register(s3);
SamplerState samp4 : register(s4);
SamplerState samp5 : register(s5);
SamplerState samp6 : register(s6);

static const float PI2 = 6.2831853;

// Tuning of the rough-water and surf look (FINE_RIPPLE, FINE_RIPPLE_CALM, COLOR_DETAIL, SURF_BREAKER, SURF_GAIN 0 and PAINT_ROUGH_KEEP 1
// give the plain look).
static const float PAINT_ROUGH_KEEP = 0.55;	// share of the painted EA pattern kept in full gust fields (1 = unchanged)
static const float FINE_RIPPLE      = 0.30;	// fine procedural ripple on rough water, relative to the ripple layer
static const float FINE_RIPPLE_CALM = 0.50;	// share of the fine ripple that stays on calm water as well
static const float COLOR_DETAIL     = 1.0;	// fine structure of the water colour (detail layer of the same tile)
static const float SURF_BREAKER     = 0.85;	// breaking wave profile (sharp crest, trailing foam) on open-sea shores
static const float SSR_FLAT_COS     = 0.55;	// SSR only where cos(view, up) is below this (flat camera); fades out over 0.25
static const float SURF_GAIN        = 0.25;	// extra foam of the wave fronts on open-sea shores

float3 WorldToView(float3 w, float3 ax, float3 ay, float3 up) { return w.x * ax + w.y * ay + w.z * up; }

/// Water depth below point p (camera space) from the depth copy or the depth code in the scene copy alpha:
/// vertical distance from p to the ground the view ray through p hits; negative if the ray hits land or an
/// obstacle first (then roughly its height above the water).
float DepthAtSigned(float3 p, bool haveCopy, float2 dsize)
{
	const float4 cProj0 = f_proj[0], cProj1 = f_proj[1], cProj2 = f_proj[2], cProj3 = f_proj[3];
	const float4 cScreen = w2Screen, cUp = float4(f_view[2].xyz, w2AxisZ.w);
	const float4 cDepthP = float4(f_depthRange.x, f_depthRange.z, w2Depth.zw);
	float4 c = p.x * cProj0 + p.y * cProj1 + p.z * cProj2 + cProj3;
	float2 suv = c.xy / c.w * cScreen.xy + cScreen.zw;
	if (c.w <= 0.0 || any(suv < 0.0) || any(suv > 1.0))
		return 99.0;
	if (haveCopy)
	{
		float d = texDepth.Load(int3(min(suv * dsize, dsize - 1.0), 0)).r;
		float ndc = (d - cDepthP.x) * cDepthP.y;
		float zg = (cProj3.z - ndc * cProj3.w) / (ndc * cProj2.w - cProj2.z);
		return (zg / p.z - 1.0) * dot(p, -cUp.xyz);
	}
	return texScene.SampleLevel(samp4, suv, 0).a * w2AxisY.w;
}

float DepthAt(float3 p, bool haveCopy, float2 dsize) { return max(DepthAtSigned(p, haveCopy, dsize), 0.0); }

/// For the shore test: depth below p, but land or obstacles more than 2.5 units above the water (piers,
/// bridges, buildings) do not count as shore -- no foam fringe forms under a pier.
float ShoreDepthAt(float3 p, bool haveCopy, float2 dsize)
{
	float d = DepthAtSigned(p, haveCopy, dsize);
	return (d < -2.5) ? 99.0 : max(d, 0.0);
}

float4 main(VS_OUT i) : SV_Target
{
	const float4 cProj0  = f_proj[0];
	const float4 cProj1  = f_proj[1];
	const float4 cProj2  = f_proj[2];
	const float4 cProj3  = f_proj[3];
	const float4 cScreen = w2Screen;
	const float4 cUp     = float4(f_view[2].xyz, w2AxisZ.w);	// world axes in camera space (view matrix rows)
	const float4 cAxisX  = float4(f_view[0].xyz, w2AxisX.w);
	const float4 cAxisY  = float4(f_view[1].xyz, w2AxisY.w);
	const float4 cSigma  = w2Sigma;
	const float4 cScroll = w2Scroll;
	const float4 cWave   = w2Wave;
	const float4 cSky    = w2Sky;
	const float3 posView = i.uv1.xyz;

	float4 t0 = texWater.Sample(samp0, i.uv0.xy);
	float4 t3 = texShroud.Sample(samp3, i.uv3.xy);
	float3 V = normalize(-posView);

	// screen position of this pixel in the scene copy
	float4 clip = posView.x * cProj0 + posView.y * cProj1 + posView.z * cProj2 + cProj3;
	float2 uv = clip.xy / clip.w * cScreen.xy + cScreen.zw;
	float4 ground0 = texScene.Sample(samp4, uv);

	if (w2Env.z < 0.5)
	{
		// ---------------- mode 1 ----------------
		float4 t1 = texFoam.Sample(samp1, i.uv0.xy);
		float4 t2 = texNoise.Sample(samp2, i.uv2.xy);
		float2 n1 = texWaves.Sample(samp5, i.uv2.xy * cWave.x + cScroll.xy).rg;
		float2 n2 = texWaves.Sample(samp5, i.uv2.xy * cWave.y + cScroll.zw).rg;
		float2 wave = (n1 + n2 - 1.0) * cWave.z;
		float3 N = normalize(cUp.xyz + wave.x * cAxisX.xyz + wave.y * cAxisY.xyz);
		float cosT = saturate(dot(N, V));
		float f = 1.0 - cosT;
		float f2 = f * f;
		float fresnel = cUp.w + (1.0 - cUp.w) * f2 * f2 * f;
		float2 uvR = uv + wave * (cAxisX.w * saturate(ground0.a * 4.0));
		float4 ground = texScene.Sample(samp4, uvR);
		if (ground.a < 0.003)
			ground = ground0;
		float depth = ground.a * cAxisY.w;
		float cosR = sqrt(1.0 - (1.0 - cosT * cosT) * 0.5653);
		float3 T = exp(-cSigma.rgb * (depth * (1.0 + 1.0 / cosR)));
		float3 body = i.color0.rgb * t0.rgb * cSky.w;
		float3 under = ground.rgb * T + body * (1.0 - T);
		float refl = fresnel * cSigma.w * saturate(depth * cWave.w);
		float3 col = lerp(under, cSky.rgb, refl);
		col = t1.rgb * t2.rgb + col;
		col *= t3.rgb;
		return Scene_Out(float4(col, 1.0));
	}

	// ---------------- mode 2 ----------------
	const float4 cSun    = w2Sun;	// xyz direction to the sun (camera space), w glint strength
	const float4 cSunCol = w2SunColor;	// rgb sun color, w share of the broad glint
	const float4 cEnv    = w2Env;	// xy cos/sin of the sun direction (sky rotation), z = 1, w depth copy valid
	const float4 cLight  = w2Light;	// rgb sky tint (map light, brightness included), w foam strength
	const float4 cScale  = w2Scale;	// uv per world unit: x layer A, y layer B, z layer A rotated, w variation
	const float4 cMove   = w2Move;	// offset: xy layer A, zw layer B
	const float4 cMove2  = w2Move2;	// xy layer A rotated, z offset of uv2, w surf phase
	const float4 cAmp    = w2Amp;	// slope: x layer A, y layer B, z layer A rotated, w variation (0..1)
	const float4 cFoamP  = w2Foam;	// foam (horizontal, world units): x fringe width, y spacing of wave fronts,
									// z reach of the fronts (times width), w pattern uv per world unit
	const float4 cDepthP = float4(f_depthRange.x, f_depthRange.z, w2Depth.zw);	// x MinZ, y 1/(MaxZ-MinZ), zw foam pattern offset
	const float4 cSsr    = w2Ssr;	// x strength (0 = off), y length in world units, z steps, w reflection gain
	const float4 cShade  = w2Shade;	// x darkening with depth, y fade glint at the shore (world units), z sky mip
									// level, w share of the painted pattern of the EA water texture

	// world position (stage 2 carries world / 16 + clock, W3DWater setupFlatWaterShader)
	float2 world = (i.uv2.xy - cMove2.z) * 16.0;

	const float4 cWind   = w2Wind;	// xy wind direction (world), z uv per world unit of the wind fields, w roughness in calm
	const float4 cWind2  = w2Wind2;	// xy wind field drift, z ripple patch strength, w upwind distance of the wind shadow

	// Water depth: from the depth copy (exact, also at steep shores, rocks and objects in the water), else from
	// the depth code in the target alpha (linear per tile corner; far too shallow at steep shores).
	uint dw, dh;
	texDepth.GetDimensions(dw, dh);
	const float2 dsize = float2(dw, dh);
	const bool haveCopy = cEnv.w > 0.5;
	float depthF = haveCopy ? DepthAt(posView, true, dsize) : ground0.a * cAxisY.w;

	// Wind: real water does not move evenly. Gust fields (elongated in the wind direction, drifting with it)
	// alternate with almost mirror-smooth streaks, plus fine ripple patches; it is calm in the lee of shores and
	// islands and in shallow water.
	float4 wf = texWind.Sample(samp6, world * cWind.z + cWind2.xy);
	float4 wf2 = texWind.Sample(samp6, world * (cWind.z * 2.7) - cWind2.xy * 1.9 + 0.37);
	float gust = smoothstep(0.2, 0.85, saturate(0.5 + (wf.r - 0.5) * 2.4 + (wf2.r - 0.5) * 1.3));
	float paws = smoothstep(0.52, 0.78, texWind.Sample(samp6, world * (cWind.z * 6.1) + cWind2.xy * 3.3).b) * cWind2.z;
	const float3 upwind = -(cWind.x * cAxisX.xyz + cWind.y * cAxisY.xyz);
	float lee = 1.0;
	float fetch = 1.0;
	if (haveCopy)
	{
		// land upwind: near = wind shadow (calm); land within 640 units upwind = short fetch, no surf
		lee = lerp(0.2, 1.0, saturate(ShoreDepthAt(posView + upwind * (cWind2.w * 0.35), true, dsize) * 1.5))
			* lerp(0.45, 1.0, saturate(ShoreDepthAt(posView + upwind * cWind2.w, true, dsize) * 1.5));
		fetch = saturate(ShoreDepthAt(posView + upwind * (cWind2.w * 8.0), true, dsize) * 1.5)
			  * saturate(ShoreDepthAt(posView + upwind * (cWind2.w * 16.0), true, dsize) * 1.5);
	}
	float shallowK = lerp(0.3, 1.0, saturate(depthF / 6.0));
	float rough = max(lerp(cWind.w, 1.3, gust), paws) * lee * shallowK;
	float var = wf.a;								// large-scale color variation

	// Waves: layer A (wind waves) and layer B (ripples) run against each other, plus layer A rotated by 37 degrees
	// at another scale. Which layer dominates changes over large areas (direction and scale vary). Slopes in units
	// of the standard deviation (map: +-3 mapped to 0..1).
	float2 wA = (texWaves.Sample(samp5, world * cScale.x + cMove.xy).rg - 0.5) * 6.0;
	float2 wB = (texWaves.Sample(samp5, world * cScale.y + cMove.zw).ba - 0.5) * 6.0;
	const float2x2 rot = float2x2(0.7986, -0.6018, 0.6018, 0.7986);
	float2 wA2 = mul(rot, (texWaves.Sample(samp5, mul(world, rot) * cScale.z + cMove2.xy).rg - 0.5) * 6.0);
	float mixA = smoothstep(0.3, 0.7, wf.g);
	float2 slope = (wA * cAmp.x * (1.25 - mixA) + wA2 * cAmp.z * (0.4 + 1.2 * mixA)) * rough + wB * cAmp.y * (0.25 + rough);
	// fine ripple only in rough fields: replaces the coarse look of the painted pattern there
	const float roughF = saturate((rough - 0.5) * 2.0);
	float2 wC = (texWaves.Sample(samp5, world * (cScale.y * 2.9) - cMove.zw * 1.7 + 0.21).ba - 0.5) * 6.0;
	slope += wC * cAmp.y * FINE_RIPPLE * max(roughF, FINE_RIPPLE_CALM);

	float3 N = normalize(cUp.xyz - slope.x * cAxisX.xyz - slope.y * cAxisY.xyz);
	float cosT = saturate(dot(N, V));
	float f = 1.0 - cosT;
	float f2 = f * f;
	float fresnel = cUp.w + (1.0 - cUp.w) * f2 * f2 * f;

	// Refraction (level 2: cAxisX.w > 0): offset by the wave slope, less in shallow water. If the offset point lies
	// above water (land, object), the unoffset one is used.
	float2 uvR = uv + slope * (cAxisX.w * saturate(depthF / cAxisY.w * 4.0));
	float4 ground = texScene.Sample(samp4, uvR);
	float depth;
	if (haveCopy)
	{
		float d = texDepth.Load(int3(min(uvR * dsize, dsize - 1.0), 0)).r;
		float ndc = (d - cDepthP.x) * cDepthP.y;
		float zg = (cProj3.z - ndc * cProj3.w) / (ndc * cProj2.w - cProj2.z);
		depth = (zg / posView.z - 1.0) * dot(posView, -cUp.xyz);
		if (depth < 0.02)
		{	ground = ground0;
			depth = depthF;
		}
	}
	else
	{
		if (ground.a < 0.003)
			ground = ground0;
		depth = ground.a * cAxisY.w;
	}

	float cosR = sqrt(1.0 - (1.0 - cosT * cosT) * 0.5653);	// 1 / 1.33^2
	float3 T = exp(-cSigma.rgb * (depth * (1.0 + 1.0 / cosR)));

	// Horizontal distance to the waterline: depth divided by the ground slope (from the derivatives of depth over
	// world position). Otherwise the depth band is only a few pixels wide at steep shores and very wide at flat ones.
	float2 wdx = ddx(world), wdy = ddy(world);
	float gdx = ddx(depthF), gdy = ddy(depthF);
	float wdet = wdx.x * wdy.y - wdx.y * wdy.x;
	float2 grad = (abs(wdet) > 1e-6) ? float2(gdx * wdy.y - gdy * wdx.y, wdx.x * gdy - wdy.x * gdx) / wdet : float2(0.0, 0.0);
	float shoreDist = depthF / clamp(length(grad), 0.08, 1.5);

	// Water color: the painted wave pattern of the EA texture is partly replaced by its mean (smallest mip) since
	// the waves come from the normals; plus light on the wave flanks (brighter toward the sun, darker away, equal
	// on average), deep water slightly darker, large-scale variation.
	float3 t0avg = texWater.SampleLevel(samp0, i.uv0.xy, 12.0).rgb;
	float3 tex = lerp(t0avg, t0.rgb, cShade.w * lerp(1.0, PAINT_ROUGH_KEEP, roughF));
	{
		// Detail layer: up close the colour tile (150 world units) is magnified and soft. The same tile 4.3 times
		// smaller and turned adds its fine structure (sample minus a blurred sample) as a brightness factor, so the
		// hue and the large shapes stay.
		const float2x2 rotD = float2x2(0.8480, -0.5299, 0.5299, 0.8480);
		float2 uvD = mul(rotD, i.uv0.xy) * 4.3 + 0.31;
		float lFine = dot(texWater.Sample(samp0, uvD).rgb, float3(0.2126, 0.7152, 0.0722));
		float lBlur = dot(texWater.SampleBias(samp0, uvD, 2.5).rgb, float3(0.2126, 0.7152, 0.0722));
		float lAvg = max(dot(t0avg, float3(0.2126, 0.7152, 0.0722)), 0.02);
		tex *= clamp(1.0 + COLOR_DETAIL * (lFine - lBlur) / lAvg, 0.5, 1.6);
	}
	float flank = dot(N, cSun.xyz) - dot(cUp.xyz, cSun.xyz);
	float deep = saturate(depth / cAxisY.w * 2.0);
	float3 body = i.color0.rgb * tex * cSky.w * (1.0 - cShade.x * deep) * (0.9 + 0.2 * var) * (1.0 + cSunCol.w * 4.5 * clamp(flank, -0.12, 0.12));
	body = lerp(body, body * float3(0.97, 1.04, 0.86), saturate((var - 0.45) * 3.0));	// olive green in places (turbidity)
	float3 under = ground.rgb * T + body * (1.0 - T);

	// Reflection: environment map in the reflected view direction, rotated to the map's sun direction
	float3 R = reflect(-V, N);
	float3 Rw = float3(dot(R, cAxisX.xyz), dot(R, cAxisY.xyz), dot(R, cUp.xyz));
	Rw.z = max(Rw.z, 0.02);
	Rw = normalize(Rw);
	// Upper hemisphere stereographic, rotated to the map's sun direction (cEnv.xy = cos, sin)
	float2 Rr = float2(cEnv.x * Rw.x + cEnv.y * Rw.y, cEnv.x * Rw.y - cEnv.y * Rw.x);
	float2 uvE = Rr / (1.0 + Rw.z) * 0.5 + 0.5;
	float3 sky = texNoise.SampleLevel(samp2, uvE, cShade.z * saturate(0.3 + rough)).rgb * 2.0 * cLight.rgb;

	// Screen space reflection (SSR): the reflected ray is marched through the depth copy; if it hits something
	// above the water, that color from the scene copy is used, else the sky.
	const float ssrView = 1.0 - smoothstep(SSR_FLAT_COS - 0.25, SSR_FLAT_COS, dot(V, cUp.xyz));
	if (cSsr.x > 0.0 && cEnv.w > 0.5 && ssrView > 0.0)
	{
		float3 p = posView;
		float3 stepV = R * (cSsr.y / cSsr.z);
		float hit = 0.0;
		float2 hitUv = uv;
		uint dw, dh;
		texDepth.GetDimensions(dw, dh);
		const float2 dsize = float2(dw, dh);
		[loop] for (int s = 1; s <= 16; ++s)
		{
			if (s > (int)cSsr.z) break;
			p += stepV;
			float4 c = p.x * cProj0 + p.y * cProj1 + p.z * cProj2 + cProj3;
			if (c.w <= 0.0) break;
			float2 suv = c.xy / c.w * cScreen.xy + cScreen.zw;
			if (any(suv < 0.0) || any(suv > 1.0)) break;
			float d = texDepth.Load(int3(min(suv * dsize, dsize - 1.0), 0)).r;
			float ndc = (d - cDepthP.x) * cDepthP.y;
			float zs = (cProj3.z - ndc * cProj3.w) / (ndc * cProj2.w - cProj2.z);
			float dz = p.z - zs;
			if (dz > 0.0 && dz < cSsr.y * 0.5)
			{
				hit = 1.0 - (float)s / (cSsr.z + 1.0);	// fade out toward the end of the ray
				hitUv = suv;
				break;
			}
		}
		if (hit > 0.0)
		{
			float2 edge = saturate(min(hitUv, 1.0 - hitUv) * 12.0);
			hit *= edge.x * edge.y * cSsr.x * ssrView;
			sky = lerp(sky, texScene.SampleLevel(samp4, hitUv, 0).rgb, hit);
		}
	}

	float shore = saturate(depthF * cWave.w);				// fade the reflection in softly at the shore
	float calm = saturate(1.0 - rough);
	float refl = saturate((fresnel * cSsr.w + 0.10 * calm * calm) * cSigma.w * shore);
	under *= 1.0 - 0.07 * saturate(rough - 0.3);
	float3 col = lerp(under, sky, refl);

	// Sun glint: sharp sparkle points and a broad shimmer, weighted by Fresnel
	float3 L = cSun.xyz;
	float3 H = normalize(L + V);
	float nh = saturate(dot(N, H));
	float fh = 1.0 - saturate(dot(H, V));
	float fH = 0.02 + 0.98 * fh * fh * fh * fh * fh;
	float spec = (pow(nh, 900.0) * 115.0 + pow(nh, 90.0) * 12.0 * cSunCol.w) * fH;
	col += cSunCol.rgb * (spec * cSun.w * saturate(depthF / cShade.y) * saturate(0.35 + rough));
	// Sparkle on rough water (bright points on wave crests in gust fields, none on smooth water): fine pattern,
	// only in gusts and nearby; stronger toward the sun. Used instead of the evenly spread sparkle of the EA
	// texture.
	float glit = smoothstep(0.90, 0.985, texFoam.Sample(samp1, world * 0.21 + cMove.zw * 3.0).b);
	glit *= saturate(rough - 0.45) * (0.5 + 2.0 * pow(nh, 12.0)) * saturate(depthF - 0.5);
	col += glit * saturate(cSunCol.rgb * 0.7 + cLight.rgb * 0.5) * 0.55;
	float foamDiag = 0.0, nearDiag = 99.0;

	// Shore foam: a fringe at the waterline and wave fronts running toward the shore.
	// Foam only where waves can break -- long upwind fetch, steep shore or obstacle, gust. No foam at calm lake
	// shores or in the wind shadow.
	float exposure = fetch * lee * (0.5 + 0.5 * gust);
	if (cLight.w > 0.0 && exposure > 0.05)
	{
		// Horizontal distance to the waterline: four rings of four points each at 0.3 / 0.6 / 1.2 / 2.4 times the
		// fringe width, rotated from ring to ring; the smallest depth per ring. Between the last ring without and the
		// first with land or obstacle, interpolate linearly by depth -- a continuous distance, also at steep shores
		// and around rocks, where the depth at the pixel jumps.
		float shoreD = shoreDist;
		[branch] if (depthF < cFoamP.x * cFoamP.z * 1.6)		// deep water: skip the test to save cost
		{
			const float radius[4] = { 0.3, 0.6, 1.2, 2.4 };
			float prevR = 0.0, prevM = depthF, found = 99.0;
			bool done = false;
			[unroll] for (int r = 0; r < 4; ++r)
			{
				float m = 99.0;
				[unroll] for (int k = 0; k < 4; ++k)
				{
					float a = (k + 0.37 * r) * 1.5707963;			// 90 degrees, offset per ring
					float3 off = cos(a) * cAxisX.xyz + sin(a) * cAxisY.xyz;
					m = min(m, ShoreDepthAt(posView + off * (radius[r] * cFoamP.x), haveCopy, dsize));
				}
				const float rr = radius[r] * cFoamP.x;
				if (!done && m < 0.4)
				{
					found = lerp(prevR, rr, saturate((prevM - 0.4) / max(prevM - m, 1e-3)));
					done = true;
				}
				prevR = rr;
				prevM = m;
			}
			shoreD = min(shoreD, found);
		}
		float edge = saturate(1.0 - shoreD / cFoamP.x);
		float reach = saturate(1.0 - shoreD / (cFoamP.x * cFoamP.z));
		float front = 0.5 + 0.5 * sin(PI2 * (cMove2.w + shoreD / cFoamP.y + var * 3.0));
		front *= front * front;
		// Breaking wave on open-sea shores: the crest runs toward the shore (same phase as above), foam trails
		// behind it seaward and decays; abrupt at the crest.
		float ph = frac(cMove2.w + shoreD / cFoamP.y + var * 3.0);
		float breaker = exp(-ph * 5.5) * (1.0 - smoothstep(0.93, 1.0, ph));
		float openSea = fetch * SURF_BREAKER;
		front = lerp(front, breaker, openSea);
		// steep: depth grows fast with distance to the waterline (rocks, cliffs), barely at flat beaches
		float steep = saturate((depthF / max(shoreD, 0.5) - 0.25) * 1.5);
		// surf at flat beaches only with a long fetch (sea), at rocks and cliffs earlier
		float intensity = saturate(edge * 0.75 + front * reach * (0.45 + SURF_GAIN * openSea)) * exposure * max(steep, 0.7 * fetch);
		// pattern stretched along the wind and broken up (streaky, patchy, never a solid fringe)
		float2 fw = float2(dot(world, cWind.xy) * 0.45, dot(world, float2(-cWind.y, cWind.x))) * cFoamP.w;
		float pat = texFoam.Sample(samp1, fw + cDepthP.zw).r * 0.6 + texFoam.Sample(samp1, fw * 2.3 - cDepthP.zw * 0.6).b * 0.4;
		pat *= smoothstep(0.35, 0.65, wf2.b);								// large gaps
		float foam = smoothstep(1.0 - intensity - 0.1, 1.0 - intensity + 0.25, pat) * cLight.w;
		foam *= saturate(depthF * 6.0);						// only on water, fading out at the waterline
		// color: white at the brightness of the map light, only a quarter of the sun color (else pink in the morning)
		float lumSun = dot(cSunCol.rgb, float3(0.2126, 0.7152, 0.0722));
		float lumSky = dot(cLight.rgb, float3(0.2126, 0.7152, 0.0722));
		float3 foamCol = saturate(lerp(float3(lumSun, lumSun, lumSun), cSunCol.rgb, 0.25) * 0.62 + lumSky * 0.45) * float3(0.96, 0.98, 1.0);
		col = lerp(col, foamCol, foam);
		foamDiag = foam;
		nearDiag = shoreD;
	}

	col *= t3.rgb;				// Shroud
	if (w2DiagFlow.x > 0.5)		// diagnostics (CNC_WASSER_FASSUNG=9): depths as color
		return Scene_Out((w2DiagFlow.x > 1.5) ? float4(foamDiag, saturate(nearDiag / 8.0), saturate(depth / 8.0), 1.0)
								  : float4(saturate(depthF / 8.0), saturate(depth / cAxisY.w), cEnv.w, 1.0));
	return Scene_Out(float4(col, 1.0));
}
