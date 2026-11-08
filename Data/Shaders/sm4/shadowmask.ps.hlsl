/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// shadowmask.ps.hlsl -----------------------------------------------------------------------------
//
// Applies sun shadows to the image, after the opaque part of the scene and before water, transparent
// objects and particles (like ambient occlusion). Per pixel:
//
//   1. Camera space position from the depth copy (t1), normal from the neighbor pixels (as ssao.ps.hlsl).
//   2. Offset by a fraction of a shadow texel along the normal (against shadow acne) into light space,
//      compared with the shadow map (t2): 3x3 bilinear compares (PCF), i.e. a soft edge over about four
//      texels.
//   3. Output is multiplied into the scene target: in full shadow the map's shadow color -- the same factor
//      the stencil shadows darken with --, faded out at the map edge and at the end of the range.
//      At the terminator (N.L around 0) the shadow fades to zero on both sides: on the lit side over
//      N.L 0..0.25, on the far side over N.L 0..-sa_map.w (self shadow like the stencil volumes, which also
//      darkened the far sides of their casters; terrain facing slightly away stays almost untouched).
//   4. Separate terrain shadows: if enabled (sa_terrain.y), the terrain casters are in their own map (t3),
//      the rest in t2. The terrain is prelit (sun N.L in the vertex color), so slopes facing away are already
//      dark. On terrain pixels -- where the scene depth still equals the depth right after the terrain (t4) --
//      facing away gets no sun shadow (prelighting already removed the sun there), lit terrain gets object
//      shadows fully and terrain shadow only by the share sa_terrain.x. Shadows on objects are unaffected;
//      both shares combine like two darkenings in series.
//   5. Soft shadows (sa_soft.w): instead of the fixed 3x3 PCF a blocker search estimates the distance to the caster;
//      the penumbra grows with it (sa_soft.x texels per light depth unit, clamped to sa_soft.y..z texels) and a 5x5
//      PCF covers it -- sharp at the foot of a caster, soft far from it. The depth bias grows with the tap offset
//      (sa_fade.w), else a receiver sloped in light space would find itself as blocker.
//   6. Contact shadows (sa_contact.w): a short ray toward the sun through the depth copy catches what the map
//      cannot resolve (small details, the gap the bias leaves at the foot of a caster). On terrain only objects
//      count as occluders when the terrain depth is known, so the prelit terrain does not darken itself again.
//   7. Cascades (sa_cascade.w): a near map (t5) with finer texels covers the near part of the range; its light clip
//      coordinates follow from the far map's by a scale and an offset. Where a point lies in the near map its object
//      shadows come from there, faded into the far map over the edge band sa_cascade.w (uv). Terrain shadows stay in
//      the far terrain map.
//
// The target has 8 bits per channel; a factor of 1 leaves the pixel unchanged.
//
// ATMOSPHERE: a second pass after the ambient occlusion, blended as premultiplied color (ONE, INV_SRC_ALPHA):
//   - height fog: density d0 * exp(-(h - base) / H) over world height h, integrated in closed form along the view ray
//     from the camera to the scene point; the fog takes the scene light (ambient plus a share of the sun).
//   - sun shafts: 16 steps along the view ray (up to the shadow range, per pixel offset by a fixed pattern) compare
//     the shadow map; lit steps scatter sun light by the same density, weighted toward the sun (Henyey-Greenstein).
// ------------------------------------------------------------------------------------------------

#include "effects.hlsli"

#include "slots.hlsli"
cbuffer cbShadowApply : register(SLOT_CB_SHADOW_APPLY)
{
	row_major float4x4	sa_viewToLight;		///< camera space -> light clip (x, y in -1..1, z in 0..1)
	float4				sa_sunView;			///< xyz: direction to the sun in camera space, w: normal offset (world)
	float4				sa_color;			///< rgb: factor in full shadow, w: depth bias (light depth 0..1)
	float4				sa_map;				///< x: 1/edge length, y: PCF tap spacing (texels), z: edge fade (uv),
											///< w: N.L from which far-side surfaces are in full self shadow
	float4				sa_fade;			///< x: range (distance to the camera), y: 1/fade distance, z: extra depth bias of
												///< the terrain map (tessellated terrain lies off its untessellated casters),
												///< w: soft shadows: depth bias per texel of tap offset (sloped receivers)
	float4				sa_terrain;			///< terrain shadows: x share of terrain shadow on terrain,
											///< y 1 = terrain separate (t3/t4 bound), z/w tolerance (world, share)
	float4				sa_soft;			///< soft shadows: x penumbra texels per light depth unit, y/z min/max penumbra
											///< (texels), w 1 = on
	float4				sa_contact;			///< contact shadows: x ray length, y thickness (world), z depth bias (share of
											///< the distance), w strength (0 = off)
	float4				sa_cascade;			///< near cascade: x scale, y/z offset (far light clip -> near), w edge band (0 = off)
	float4				sa_worldZ;			///< ATMOSPHERE: world height = dot(float4(camera space position, 1), sa_worldZ)
	float4				sa_atmo;			///< ATMOSPHERE: x fog density at the base, y 1/scale height, z base height (world),
											///< w sun shaft strength (0 = off)
	float4				sa_fogColor;		///< ATMOSPHERE: rgb fog color, w largest fog share
	float4				sa_sunColor;		///< ATMOSPHERE: rgb sun light, w Henyey-Greenstein g
};

Texture2D<float>		fx_depth : register(t1);	///< depth copy of the opaque scene
Texture2D<float>		sh_map   : register(t2);	///< shadow map (light depth)
Texture2D<float>		sh_terrain       : register(t3);	///< terrain shadows: shadow map of the terrain casters only
Texture2D<float>		fx_terrain_depth : register(t4);	///< terrain shadows: depth copy right after the terrain
Texture2D<float>		sh_near          : register(t5);	///< near cascade (object casters)
SamplerComparisonState	sh_cmp   : register(s1);	///< LESS_EQUAL, bilinear, border = 1 (lit)

// 3x3 bilinear compares (PCF): lit fraction 0..1.
float Lit_PCF(Texture2D<float> map, float2 uv, float z, float spacing)
{
	float lit = 0.0;
	[unroll]
	for (int y = -1; y <= 1; ++y) {
		[unroll]
		for (int x = -1; x <= 1; ++x) {
			lit += map.SampleCmpLevelZero(sh_cmp, uv + float2(x, y) * spacing, z);
		}
	}
	return lit * (1.0 / 9.0);
}

// Blocker search and PCF over the estimated penumbra: lit fraction 0..1. The search taps lie on a golden-angle
// spiral that fills the disk evenly (computed, no constant table).
float Lit_Soft(Texture2D<float> map, float2 uv, float z, float scale)
{
	// scale: texels of this map per texel of the far map (near cascade), so penumbrae keep their width in the world.
	const float size = 1.0 / sa_map.x;
	const float widest = min(sa_soft.z * scale, 32.0);
	const float slope = sa_fade.w / scale;
	float blockers = 0.0, depth_sum = 0.0;
	[unroll]
	for (int i = 0; i < 16; ++i) {
		const float a = (float)i * 2.39996323;
		const float r = sqrt(((float)i + 0.5) / 16.0) * widest;
		const float2 t = uv + float2(cos(a), sin(a)) * (r * sa_map.x);
		const float d = map.Load(int3(clamp(int2(t * size), 0, int(size) - 1), 0));
		if (d < z - r * slope) { blockers += 1.0; depth_sum += d; }
	}
	if (blockers < 0.5) return 1.0;
	const float penumbra = clamp((z - depth_sum / blockers) * sa_soft.x * scale, sa_soft.y, widest);
	const float step = penumbra * 0.25 * sa_map.x;			// 5x5 taps over -2..2 steps: the full penumbra
	float lit = 0.0;
	[unroll]
	for (int y = -2; y <= 2; ++y) {
		[unroll]
		for (int x = -2; x <= 2; ++x) {
			lit += map.SampleCmpLevelZero(sh_cmp, uv + float2(x, y) * step, z - length(float2(x, y)) * penumbra * 0.25 * slope);
		}
	}
	return lit * (1.0 / 25.0);
}

float Lit_Map(Texture2D<float> map, float2 uv, float z, float spacing, float scale)
{
	[branch]
	if (sa_soft.w > 0.5) return Lit_Soft(map, uv, z, scale);
	return Lit_PCF(map, uv, z, spacing);
}

// Camera space position -> pixel position in the scene target (inverse of View_Position) and w.
float3 Pixel_Of(float3 V)
{
	const float w = V.z * e_projB.y + e_projB.w;
	const float2 ndc = float2(V.x * e_projA.x + V.z * e_projA.z, V.y * e_projA.y + V.z * e_projA.w) / w;
	const float2 size = e_view.zw - e_view.xy;
	return float3(e_view.x + (ndc.x * 0.5 + 0.5) * size.x, e_view.y + (0.5 - ndc.y * 0.5) * size.y, w);
}

// Contact shadow: march toward the sun through the depth copy; 0..1 occlusion.
float Contact_Shadow(float3 P, float w, bool ground)
{
	const float3 start = Pixel_Of(P);
	const float bias = sa_contact.z * w;
	float hit = 0.0;
	[loop]
	for (int i = 1; i <= 16; ++i) {
		const float f = i * (1.0 / 16.0);
		const float3 q = Pixel_Of(P + sa_sunView.xyz * (sa_contact.x * f));
		if (any(q.xy < e_rect.xy) || any(q.xy >= e_rect.zw) || q.z <= 0.0) break;
		const int2 qp = int2(q.xy);
		if (all(qp == int2(start.xy))) continue;
		const float scene_w = Depth_To_W(fx_depth.Load(int3(qp, 0)), e_depthRange, e_projB);
		const float in_front = q.z - scene_w;
		if (in_front > bias && in_front < sa_contact.y) {
			// On terrain only objects occlude: the occluding pixel must lie in front of the terrain there.
			bool counts = true;
			if (ground) {
				const float wt = Depth_To_W(fx_terrain_depth.Load(int3(qp, 0)), e_depthRange, e_projB);
				counts = (wt - scene_w > sa_terrain.z + sa_terrain.w * scene_w);
			}
			if (counts) {
				hit = 1.0 - f * f;				// nearer hits count fully, the far end fades out
				break;
			}
		}
	}
	return hit;
}

float3 Pos_At(int2 p)
{
	p = clamp(p, int2(e_rect.xy), int2(e_rect.zw) - 1);
	return View_Position(float2(p) + 0.5, fx_depth.Load(int3(p, 0)));
}

#ifdef ATMOSPHERE
/// Optical depth of the height fog along the ray from height hc (camera) to height hp over length len.
float Fog_Depth(float hc, float hp, float len)
{
	const float ec = exp(-(hc - sa_atmo.z) * sa_atmo.y);
	const float ep = exp(-(hp - sa_atmo.z) * sa_atmo.y);
	const float dh = hc - hp;
	return sa_atmo.x * len * ((abs(dh * sa_atmo.y) > 1e-3) ? (ep - ec) / (dh * sa_atmo.y) : ep);
}

float4 main(float4 pos : SV_Position) : SV_Target
{
	const int2 p = int2(pos.xy);
	const float d = fx_depth.Load(int3(p, 0));
	if (d >= 0.999999) return float4(0.0, 0.0, 0.0, 0.0);		// sky/cleared: unchanged
	const float3 P = View_Position(float2(p) + 0.5, d);
	const float len = length(P);
	const float hc = sa_worldZ.w;
	const float hp = dot(float4(P, 1.0), sa_worldZ);
	const float fog = min(1.0 - exp(-Fog_Depth(hc, hp, len)), sa_fogColor.w);
	float3 light = sa_fogColor.rgb * fog;

	[branch] if (sa_atmo.w > 0.0) {
		// fixed per pixel offset of the steps (interleaved gradient noise): no visible bands, reproducible images
		const float jitter = frac(52.9829189 * frac(dot(float2(p), float2(0.06711056, 0.00583715))));
		const float reach = min(len, sa_fade.x);
		const float ds = reach / 16.0;
		float scatter = 0.0;
		[loop] for (int k = 0; k < 16; ++k) {
			const float s = (k + jitter) * ds;
			const float3 V = P * (s / len);
			const float4 lc = mul(float4(V, 1.0), sa_viewToLight);
			const float2 uv = float2(lc.x * 0.5 + 0.5, 0.5 - lc.y * 0.5);
			const float lit = sh_map.SampleCmpLevelZero(sh_cmp, uv, lc.z - sa_color.w);
			const float h = dot(float4(V, 1.0), sa_worldZ);
			scatter += lit * exp(-(h - sa_atmo.z) * sa_atmo.y);
		}
		const float g = sa_sunColor.w;
		const float mu = dot(P / len, sa_sunView.xyz);
		const float phase = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * mu, 1e-4), 1.5);
		light += sa_sunColor.rgb * (sa_atmo.w * sa_atmo.x * ds * scatter * phase * (1.0 - fog));
	}
	return float4(light, fog);
}
#else
float4 main(float4 pos : SV_Position) : SV_Target
{
	const int2 p = int2(pos.xy);
	const float d = fx_depth.Load(int3(p, 0));
	if (d >= 0.999999) return float4(1.0, 1.0, 1.0, 1.0);		// sky/cleared
	const float w = Depth_To_W(d, e_depthRange, e_projB);
	if (w >= sa_fade.x) return float4(1.0, 1.0, 1.0, 1.0);

	// Normal from the neighbors; per axis the side with the smaller depth step (edges).
	const float3 P  = Pos_At(p);
	const float3 Pr = Pos_At(p + int2(1, 0)), Pl = Pos_At(p - int2(1, 0));
	const float3 Pd = Pos_At(p + int2(0, 1)), Pu = Pos_At(p - int2(0, 1));
	const float3 dx = Neighbor_Step(P, Pr, Pl);
	const float3 dy = Neighbor_Step(P, Pd, Pu);
	float3 n = normalize(cross(dx, dy));
	if (dot(n, P) > 0.0) n = -n;			// toward the camera (origin)

	const float ndl = dot(n, sa_sunView.xyz);

	// Normal offset: grazing sun angles need more (acne), surfaces lit head-on barely any.
	const float3 Q = P + n * (sa_sunView.w * (1.0 + 2.0 * saturate(1.0 - ndl)));
	const float4 lc = mul(float4(Q, 1.0), sa_viewToLight);
	const float2 uv = float2(lc.x * 0.5 + 0.5, 0.5 - lc.y * 0.5);
	const float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
	if (edge <= 0.0) return float4(1.0, 1.0, 1.0, 1.0);
	const float z = lc.z - sa_color.w;

	const float spacing = sa_map.x * sa_map.y;
	// Object casters: the near cascade where the point lies in it, faded into the far map at its edge (step 7).
	float near_weight = 0.0;
	float shadow_near = 0.0;
	[branch]
	if (sa_cascade.w > 0.0) {
		const float2 nc = lc.xy * sa_cascade.x + sa_cascade.yz;
		const float2 nuv = float2(nc.x * 0.5 + 0.5, 0.5 - nc.y * 0.5);
		const float nedge = min(min(nuv.x, 1.0 - nuv.x), min(nuv.y, 1.0 - nuv.y));
		near_weight = saturate(nedge / sa_cascade.w);
		if (near_weight > 0.0) shadow_near = 1.0 - Lit_Map(sh_near, nuv, z, spacing, sa_cascade.x);
	}
	float shadow = 0.0;
	[branch]
	if (near_weight < 1.0) shadow = (1.0 - Lit_Map(sh_map, uv, z, spacing, 1.0)) * saturate(edge / sa_map.z);	// map edge
	shadow = lerp(shadow, shadow_near, near_weight);

	shadow *= saturate((sa_fade.x - w) * sa_fade.y);			// end of range
	// Terminator: fade in over N.L 0..0.25 on the lit side, over 0..-sa_map.w on the far side.
	const float lit_side = saturate(ndl * 4.0);
	const float terminator = (ndl >= 0.0) ? lit_side : saturate(-ndl / sa_map.w);
	bool ground = false;					// known only with separate terrain shadows
	[branch]
	if (sa_terrain.y > 0.5) {
		// Separate terrain casters (step 4). The receiver is terrain if nothing was drawn in front of it after the
		// terrain (with a tolerance for later draws at the same depth).
		float shadowT = 1.0 - Lit_Map(sh_terrain, uv, z - sa_fade.z, spacing, 1.0);
		shadowT *= saturate(edge / sa_map.z) * saturate((sa_fade.x - w) * sa_fade.y);
		const float wt = Depth_To_W(fx_terrain_depth.Load(int3(p, 0)), e_depthRange, e_projB);
		ground = (wt - w <= sa_terrain.z + sa_terrain.w * w);
		shadowT *= ground ? sa_terrain.x * lit_side : terminator;
		shadow = 1.0 - (1.0 - shadow * (ground ? lit_side : terminator)) * (1.0 - shadowT);
	}
	else
		shadow *= terminator;
	[branch]
	if (sa_contact.w > 0.0 && lit_side > 0.0) {
		const float contact = Contact_Shadow(P, w, ground) * sa_contact.w * lit_side * saturate((sa_fade.x - w) * sa_fade.y);
		shadow = max(shadow, contact);
	}
	const float3 f = lerp(float3(1.0, 1.0, 1.0), sa_color.rgb, shadow);
	return float4(f, 1.0);
}
#endif // ATMOSPHERE
