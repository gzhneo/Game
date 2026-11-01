/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ffp.vs.hlsl ------------------------------------------------------------------------------------
//
// Uber vertex shader. Emulates the fixed-function pipeline (D3D9 semantics): transformation,
// vertex lighting with up to four lights, material, fog and texture coordinate generation with
// a texture matrix per stage.
//
// Permutations depend only on the vertex signature (defines below); everything else is uniform
// from cbObject, because branching on constants is cheap in SM 4.0.
//
// Defines set by the backend (rendercore.cpp, derived from the vertex declaration):
//   HAS_NORMAL, HAS_DIFFUSE, HAS_SPECULAR, NUM_TEXCOORDS (0..4). The component count of the
//   UV sets is not part of the permutation; it is in o_stages[].bump_scale.z.
//   INSTANCED: the four object matrices come per instance from cbInstances (instanced meshes); the rest
//   of cbObject is the same for all instances of the draw.
//   PIXEL_LIGHT (with HAS_NORMAL): diffuse and specular of the lights move to the pixel shader (ffp.ps.hlsl with
//   PIXEL_LIGHT); color0 carries ambient plus emissive and the diffuse alpha, color1 the specular material, and
//   VS_OUT.normal / .diffuse the world normal and the diffuse material.
//
// Color byte order: D3DCOLOR is BGRA in memory. The input layout declares vertex colors as
// R8G8B8A8_UNORM, so they are swizzled with .bgra here.
// ------------------------------------------------------------------------------------------------

#include "common.hlsli"
#include "terrainlights.hlsli"
#include "skin.hlsli"
#include "ffplight.hlsli"

#ifndef NUM_TEXCOORDS
#define NUM_TEXCOORDS 0
#endif

struct VS_IN
{
	float3	pos      : POSITION;
#ifdef HAS_NORMAL
	float3	normal   : NORMAL;
#endif
#ifdef HAS_DIFFUSE
	float4	diffuse  : COLOR0;
#endif
#ifdef HAS_SPECULAR
	float4	specular : COLOR1;
#endif
#if NUM_TEXCOORDS > 0
	float4	uv0      : TEXCOORD0;
#endif
#if NUM_TEXCOORDS > 1
	float4	uv1      : TEXCOORD1;
#endif
#if NUM_TEXCOORDS > 2
	float4	uv2      : TEXCOORD2;
#endif
#if NUM_TEXCOORDS > 3
	float4	uv3      : TEXCOORD3;
#endif
#ifdef INSTANCED
	uint	instance : INSTANCEINDEX;		// 0, 1, 2, ... per instance from stream 1 (not every compiler knows SV_InstanceID)
#endif
};

#ifdef INSTANCED
// o_world, o_worldView, o_worldViewProj and o_worldInvTranspose of each instance, 16 rows per instance
// (RenderCore::Mi_Flush, MI_MAX_INSTANCES); read into locals of main.
#include "slots.hlsli"
cbuffer cbInstances : register(SLOT_CB_INSTANCES)
{
	float4	inst_rows[64 * 16];
};
#define o_world				g_world
#define o_worldView			g_worldView
#define o_worldViewProj		g_worldViewProj
#define o_worldInvTranspose	g_worldInvTranspose
#endif

// Material source per channel: material, first or second vertex color (D3DRS_*MATERIALSOURCE).
// If the vertex lacks the selected color, fixed function uses the material color.
float4 Pick_Source(int source, float4 material, float4 color1, float4 color2)
{
#ifdef HAS_DIFFUSE
	if (source == FFP_MCS_COLOR1) return color1;
#endif
#ifdef HAS_SPECULAR
	if (source == FFP_MCS_COLOR2) return color2;
#endif
	return material;
}

// Raw texture coordinates of the selected set.
float4 Fetch_UV(VS_IN v, int index)
{
	float4 uv = float4(0, 0, 0, 1);
#if NUM_TEXCOORDS > 0
	if (index == 0) uv = v.uv0;
#endif
#if NUM_TEXCOORDS > 1
	if (index == 1) uv = v.uv1;
#endif
#if NUM_TEXCOORDS > 2
	if (index == 2) uv = v.uv2;
#endif
#if NUM_TEXCOORDS > 3
	if (index == 3) uv = v.uv3;
#endif
	return uv;
}

// Texture coordinates of one stage: pick the source (pass through or generate in view space),
// then apply the texture matrix. TTFF_COUNT3|PROJECTED yields xyz; the pixel shader divides.
float4 Stage_TexCoord(VS_IN v, int stage, float3 view_pos, float3 view_normal)
{
	FFPStage s = o_stages[stage];
	float4 uv;

	if (s.texgen.x == FFP_TEXGEN_CAMERASPACEPOSITION) {
		uv = float4(view_pos, 1.0);
	}
	else if (s.texgen.x == FFP_TEXGEN_CAMERASPACENORMAL) {
		uv = float4(view_normal, 1.0);
	}
	else if (s.texgen.x == FFP_TEXGEN_CAMERASPACEREFLECTIONVECTOR) {
		// Reflect the view direction at the normal, both in view space.
		float3 eye = normalize(view_pos);
		uv = float4(reflect(eye, view_normal), 1.0);
	}
	else {
		uv = Fetch_UV(v, s.texgen.y);
		// Fixed function extends short coordinates for the texture matrix with a 1 after the
		// last component: (u, 1, 0, 0) or (u, v, 1, 0), so the translation of a 2D matrix sits
		// in row 3. The input layout pads with (0, 0, 0, 1) instead.
		if (s.texgen.z > 0) {
			if (s.bump_scale.z == 1.0) uv = float4(uv.x, 1.0, 0.0, 0.0);
			else if (s.bump_scale.z == 2.0) uv = float4(uv.xy, 1.0, 0.0);
		}
	}

	if (s.texgen.z > 0) {
		uv = mul(uv, o_texMatrix[stage]);
	}
	return uv;
}

VS_OUT main(VS_IN v)
{
	VS_OUT o;

#ifdef INSTANCED
	const uint row = v.instance * 16;
	const float4x4 g_world             = float4x4(inst_rows[row + 0],  inst_rows[row + 1],  inst_rows[row + 2],  inst_rows[row + 3]);
	const float4x4 g_worldView         = float4x4(inst_rows[row + 4],  inst_rows[row + 5],  inst_rows[row + 6],  inst_rows[row + 7]);
	const float4x4 g_worldViewProj     = float4x4(inst_rows[row + 8],  inst_rows[row + 9],  inst_rows[row + 10], inst_rows[row + 11]);
	const float4x4 g_worldInvTranspose = float4x4(inst_rows[row + 12], inst_rows[row + 13], inst_rows[row + 14], inst_rows[row + 15]);
#endif

	float3 in_pos = v.pos;
#ifdef HAS_NORMAL
	float3 in_normal = v.normal;
#endif
#if NUM_TEXCOORDS > 2
	// GPU skin: UV set 2 holds the bone index (skin.hlsli).
	if (sk_info.x > 0.0) {
		const uint bone = Skin_Bone(v.uv2.x);
		in_pos = Skin_Point(bone, in_pos);
#ifdef HAS_NORMAL
		in_normal = Skin_Vector(bone, in_normal);
#endif
	}
#endif

	const float4 position = float4(in_pos, 1.0);

	o.pos = mul(position, o_worldViewProj);
	const float3 view_pos = mul(position, o_worldView).xyz;

#ifdef HAS_NORMAL
	float3 view_normal = mul(float4(in_normal, 0.0), o_worldView).xyz;
	if (o_flags.x != 0) view_normal = normalize(view_normal);
	// As in fixed function, normalize only with NORMALIZENORMALS (the engine sets it for scaled
	// meshes). Otherwise the normal length from the model file affects the lighting.
	float3 world_normal = mul(float4(in_normal, 0.0), o_worldInvTranspose).xyz;
	if (o_flags.x != 0) world_normal = normalize(world_normal);
	const float3 world_pos    = mul(position, o_world).xyz;
#else
	// Without a normal, fixed function uses the zero vector: ambient and emissive only.
	const float3 view_normal = float3(0, 0, 0);
	const float3 world_normal = float3(0, 0, 0);
	const float3 world_pos    = mul(position, o_world).xyz;
#endif

#ifdef HAS_DIFFUSE
	float4 vcolor0 = v.diffuse.bgra;
	// Terrain tiles with static prelighting in the shader: the vertex color is white and scales it.
	if (tl_static.x > 0.0) vcolor0.rgb *= Terrain_Static_Light(world_pos);
#else
	const float4 vcolor0 = float4(1, 1, 1, 1);
#endif
#ifdef HAS_SPECULAR
	const float4 vcolor1 = v.specular.bgra;
#else
	const float4 vcolor1 = float4(0, 0, 0, 0);
#endif

	float4 diffuse_out  = vcolor0;
	float4 specular_out = float4(0, 0, 0, 0);
#ifdef PIXEL_LIGHT
	// xyz world normal, w its length (fixed function lights with the unnormalized normal without NORMALIZENORMALS)
	o.normal  = float4(world_normal, length(world_normal));
	o.diffuse = float4(0, 0, 0, 0);
#endif

	if (o_lightFlags.y != 0) {

		// Material sources per channel (D3DRS_DIFFUSEMATERIALSOURCE etc.)
		const float4 m_diffuse  = Pick_Source(o_colorSource.x, o_matDiffuse,  vcolor0, vcolor1);
		const float4 m_ambient  = Pick_Source(o_colorSource.y, o_matAmbient,  vcolor0, vcolor1);
		const float4 m_emissive = Pick_Source(o_colorSource.z, o_matEmissive, vcolor0, vcolor1);
		const float4 m_specular = Pick_Source(o_colorSource.w, o_matSpecular, vcolor0, vcolor1);

		// Fixed-function lighting math:
		//   ambient  = globalAmbient * Ma + sum(La * Ma)
		//   diffuse  = sum(Ld * Md * max(N.L,0) * att * spot)
		//   specular = sum(Ls * Ms * pow(max(N.H,0), P) * att * spot)
		float3 ambient  = o_globalAmbient.rgb * m_ambient.rgb;
		float3 diffuse  = float3(0, 0, 0);
		float3 specular = float3(0, 0, 0);

		// View direction for the half vector: toward the camera, or fixed (0,0,1) without LocalViewer.
		const float3 to_eye = Ffp_To_Eye(world_pos);

		[unroll] for (int i = 0; i < FFP_MAX_LIGHTS; ++i) {
			if ((o_lightFlags.x & (1 << i)) == 0) continue;

			FFPLight L = o_lights[i];
			float3 to_light;
			const float factor = Ffp_Light_Factor(i, world_pos, to_light);
			ambient += L.ambient.rgb * m_ambient.rgb * factor;
#ifndef PIXEL_LIGHT
			const float ndotl = max(dot(world_normal, to_light), 0.0);
			diffuse += L.diffuse.rgb * m_diffuse.rgb * ndotl * factor;

			if (o_lightFlags.z != 0 && ndotl > 0.0) {
				const float3 halfway = normalize(to_light + to_eye);
				const float  ndoth   = max(dot(world_normal, halfway), 0.0);
				specular += L.specular.rgb * m_specular.rgb * pow(ndoth, max(o_matPower.x, 1e-3)) * factor;
			}
#endif
		}

#ifdef PIXEL_LIGHT
		// the pixel shader adds diffuse and specular and clamps
		diffuse_out  = float4(ambient + m_emissive.rgb, m_diffuse.a);
		specular_out = float4(m_specular.rgb, 0.0);
		o.diffuse    = float4(m_diffuse.rgb, 0.0);
#else
		diffuse_out  = float4(saturate(ambient + diffuse + m_emissive.rgb), m_diffuse.a);
		specular_out = float4(saturate(specular), 0.0);
#endif
	}
	else {
		// Without lighting: vertex color (or white), specular only if the vertex has one.
		specular_out = (o_lightFlags.z != 0) ? vcolor1 : float4(0, 0, 0, 0);
	}

#ifdef HAS_DIFFUSE
	// Terrain tiles: dynamic lights (the count is 0 for every other draw).
	if (tl_map.w > 0.0) diffuse_out.rgb = Terrain_Dynamic_Lights(world_pos, diffuse_out.rgb);
#endif

	o.color0 = diffuse_out;
	o.color1 = specular_out;

	// Linear vertex fog from the view space depth (every caller uses a linear FOGVERTEXMODE,
	// RANGEFOG is unused). 1 = clear, 0 = fully fogged.
	float fog_factor = 1.0;
	if (f_fogParams.z != 0.0) {
		const float d = -view_pos.z;
		fog_factor = saturate((f_fogParams.y - d) / max(f_fogParams.y - f_fogParams.x, 1e-6));
	}
	// yzw = world position for the terrain rock layer (terrainrock.hlsli); no other pixel
	// shader reads it, so the image is unchanged without the rock layer.
	o.fog = float4(fog_factor, world_pos);

	// All four sets go through texgen and texture matrix: the terrain shaders generate the
	// coordinates of stages 2 and 3 from the camera space position. There are no stages
	// beyond 3.
	o.uv0 = Stage_TexCoord(v, 0, view_pos, view_normal);
	o.uv1 = Stage_TexCoord(v, 1, view_pos, view_normal);
	o.uv2 = Stage_TexCoord(v, 2, view_pos, view_normal);
	o.uv3 = Stage_TexCoord(v, 3, view_pos, view_normal);

	return o;
}
