/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ffplight.hlsli ---------------------------------------------------------------------------------
//
// Fixed-function lighting terms of one light (D3D9 semantics), shared by the vertex lighting of
// ffp.vs.hlsl and the PIXEL_LIGHT permutation of ffp.vs.hlsl / ffp.ps.hlsl. With PIXEL_LIGHT the
// vertex shader keeps ambient and emissive (smooth, no normal) and the pixel shader sums diffuse and
// specular with the interpolated normal.
// ------------------------------------------------------------------------------------------------

#ifndef FFP_LIGHT_HLSLI
#define FFP_LIGHT_HLSLI

#include "common.hlsli"

/// Direction to light i and its attenuation times spot factor at world_pos.
float Ffp_Light_Factor(int i, float3 world_pos, out float3 to_light)
{
	FFPLight L = o_lights[i];
	const int type = (int)L.position.w;

	float attenuation = 1.0;
	float spot        = 1.0;

	if (type == FFP_LIGHT_DIRECTIONAL) {
		to_light = -normalize(L.direction.xyz);
	}
	else {
		float3 delta = L.position.xyz - world_pos;
		float  dist  = length(delta);
		to_light = (dist > 0.0) ? delta / dist : float3(0, 0, 1);

		// 1 / (a0 + a1*d + a2*d*d), cut off at Range
		const float denom = L.attenuation.y + L.attenuation.z * dist + L.attenuation.w * dist * dist;
		attenuation = (denom > 0.0) ? (1.0 / denom) : 1.0;
		attenuation = (dist > L.attenuation.x) ? 0.0 : saturate(attenuation);

		if (type == FFP_LIGHT_SPOT) {
			// rho = dot(-Ldir, L), falling off between cos(phi/2) and cos(theta/2)
			const float rho    = dot(normalize(L.direction.xyz), -to_light);
			const float cosThe = L.spot.y;
			const float cosPhi = L.spot.z;
			if (rho <= cosPhi)      spot = 0.0;
			else if (rho > cosThe)  spot = 1.0;
			else                    spot = pow(saturate((rho - cosPhi) / max(cosThe - cosPhi, 1e-6)), L.spot.x);
		}
	}
	return attenuation * spot;
}

/// View direction for the half vector: toward the camera, or fixed (0,0,1) without LocalViewer.
float3 Ffp_To_Eye(float3 world_pos)
{
	return (o_lightFlags.w != 0) ? normalize(f_cameraPosWorld.xyz - world_pos) : float3(0, 0, 1);
}

/// Diffuse (unweighted by the material) and specular sums of all lights for a normal; the material colors are
/// applied by the caller.
void Ffp_Direct_Light(float3 world_normal, float3 world_pos, out float3 diffuse, out float3 specular)
{
	diffuse  = float3(0, 0, 0);
	specular = float3(0, 0, 0);
	const float3 to_eye = Ffp_To_Eye(world_pos);
	[unroll] for (int i = 0; i < FFP_MAX_LIGHTS; ++i) {
		if (((uint)o_lightFlags.x & (1u << i)) == 0u) continue;
		float3 to_light;
		const float factor = Ffp_Light_Factor(i, world_pos, to_light);
		const float ndotl = max(dot(world_normal, to_light), 0.0);
		diffuse += o_lights[i].diffuse.rgb * ndotl * factor;
		if (o_lightFlags.z != 0 && ndotl > 0.0) {
			const float3 halfway = normalize(to_light + to_eye);
			const float  ndoth   = max(dot(world_normal, halfway), 0.0);
			specular += o_lights[i].specular.rgb * pow(ndoth, max(o_matPower.x, 1e-3)) * factor;
		}
	}
}

/// Normal n bent by a tangent space normal map texel (R, G = x, y in image axes with G up, byte 128 = flat; the
/// format of the terrain relief tiles). The tangent frame comes from the screen derivatives of world position p and
/// texture coordinate uv (cotangent frame), so meshes need no tangents and mirrored coordinates work too. The
/// derivatives are taken by the caller outside non-uniform flow.
float3 Ffp_Normal_Map(float3 n, float3 dpx, float3 dpy, float2 dux, float2 duy, float4 texel)
{
	const float3 px = cross(n, dpx);
	const float3 py = cross(dpy, n);
	const float3 t = py * dux.x + px * duy.x;		// direction of +u
	const float3 b = py * dux.y + px * duy.y;		// direction of +v (image down)
	const float scale = rsqrt(max(max(dot(t, t), dot(b, b)), 1e-20));
	float2 m = texel.rg * (255.0 / 127.5) - 1.0;
	const float mz = sqrt(saturate(1.0 - dot(m, m)));
	const float3 bent = t * (m.x * scale) - b * (m.y * scale) + n * mz;
	const float len = length(bent);
	return (len > 1e-6) ? bent / len : n;
}

#endif // FFP_LIGHT_HLSLI
