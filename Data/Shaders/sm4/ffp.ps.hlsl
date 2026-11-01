/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ffp.ps.hlsl ------------------------------------------------------------------------------------
//
// Uber pixel shader. Emulates the fixed-function texture stage combiners (D3D9 semantics) -- two
// stages with an operation and up to three arguments, then specular, fog and alpha test.
//
// Order as in fixed function: stages first, then add the specular color, then blend the fog,
// alpha test last.
//
// The full operation set is implemented, not only the combinations the game uses, so every
// operation reachable through ShaderClass bits works.
//
// PIXEL_LIGHT: diffuse and specular of the lights per pixel with the interpolated normal (ffplight.hlsli); the
// vertex shader of the same permutation passes ambient plus emissive and the material colors.
// ------------------------------------------------------------------------------------------------

#include "common.hlsli"
#ifdef PIXEL_LIGHT
#include "ffplight.hlsli"
#include "slots.hlsli"
// Normal map of the stage 0 texture (<texture>_n.dds) when o_teamColor.w is 1; read with the coordinates and
// sampler of stage 0. B = gloss: scales the highlight (byte 128 = as without the map).
Texture2D    ffp_normal : register(SLOT_T_MODEL_NORMAL);
#endif

Texture2D    ffp_tex0 : register(t0);
Texture2D    ffp_tex1 : register(t1);
SamplerState ffp_samp0 : register(s0);
SamplerState ffp_samp1 : register(s1);

#ifdef SOFT_PARTICLES
// Soft particles. A separate permutation the backend selects only for blended billboard
// particles; without the macro the shader is unchanged. t8 is the depth copy of the opaque
// scene (Scene_Opaque_Done).
#include "effects.hlsli"
#include "slots.hlsli"
Texture2D<float> fx_scene_depth : register(SLOT_T_SCENE_DEPTH);
#endif

// Fetch one combiner argument, including the COMPLEMENT and ALPHAREPLICATE modifiers.
float4 Combiner_Arg(int arg, float4 texture_color, float4 diffuse, float4 current, float4 specular)
{
	float4 value;
	const int selector = arg & FFP_TA_SELECTMASK;

	if      (selector == FFP_TA_TEXTURE)  value = texture_color;
	else if (selector == FFP_TA_DIFFUSE)  value = diffuse;
	else if (selector == FFP_TA_CURRENT)  value = current;
	else if (selector == FFP_TA_TFACTOR)  value = o_tfactor;
	else if (selector == FFP_TA_SPECULAR) value = specular;
	else                                  value = current;

	if ((arg & FFP_TA_ALPHAREPLICATE) != 0) value = value.aaaa;
	if ((arg & FFP_TA_COMPLEMENT) != 0)     value = 1.0 - value;
	return value;
}

// One combiner operation on three already fetched arguments.
// `current_alpha`, `texture_alpha`, `diffuse_alpha` are needed by the BLEND*ALPHA variants,
// which take the blend factor from a fixed channel.
float4 Combiner_Op(int op, float4 a1, float4 a2, float4 a0,
				   float current_alpha, float texture_alpha, float diffuse_alpha)
{
	if (op == FFP_TOP_SELECTARG1)              return a1;
	if (op == FFP_TOP_SELECTARG2)              return a2;
	if (op == FFP_TOP_MODULATE)                return a1 * a2;
	if (op == FFP_TOP_MODULATE2X)              return (a1 * a2) * 2.0;
	if (op == FFP_TOP_MODULATE4X)              return (a1 * a2) * 4.0;
	if (op == FFP_TOP_ADD)                     return a1 + a2;
	if (op == FFP_TOP_ADDSIGNED)               return a1 + a2 - 0.5;
	if (op == FFP_TOP_ADDSIGNED2X)             return (a1 + a2 - 0.5) * 2.0;
	if (op == FFP_TOP_SUBTRACT)                return a1 - a2;
	if (op == FFP_TOP_ADDSMOOTH)               return a1 + a2 * (1.0 - a1);
	if (op == FFP_TOP_BLENDDIFFUSEALPHA)       return lerp(a2, a1, diffuse_alpha);
	if (op == FFP_TOP_BLENDTEXTUREALPHA)       return lerp(a2, a1, texture_alpha);
	if (op == FFP_TOP_BLENDFACTORALPHA)        return lerp(a2, a1, o_tfactor.a);
	if (op == FFP_TOP_BLENDTEXTUREALPHAPM)     return a1 + a2 * (1.0 - texture_alpha);
	if (op == FFP_TOP_BLENDCURRENTALPHA)       return lerp(a2, a1, current_alpha);
	if (op == FFP_TOP_PREMODULATE)             return a1;
	if (op == FFP_TOP_MODULATEALPHA_ADDCOLOR)  return float4(a1.rgb + a1.a * a2.rgb, a1.a * a2.a);
	if (op == FFP_TOP_MODULATECOLOR_ADDALPHA)  return float4(a1.rgb * a2.rgb + a1.a, a1.a * a2.a);
	if (op == FFP_TOP_DOTPRODUCT3) {
		// Fixed function computes on signed values and replicates the result.
		const float d = 4.0 * dot(a1.rgb - 0.5, a2.rgb - 0.5);
		return float4(d, d, d, d);
	}
	if (op == FFP_TOP_MULTIPLYADD)             return a0 + a1 * a2;
	if (op == FFP_TOP_LERP)                    return lerp(a2, a1, a0);
	return a1;	// the caller handles DISABLE
}

bool Alpha_Test_Passes(int func, float alpha, float reference)
{
	if (func == FFP_CMP_NEVER)        return false;
	if (func == FFP_CMP_LESS)         return alpha <  reference;
	if (func == FFP_CMP_EQUAL)        return alpha == reference;
	if (func == FFP_CMP_LESSEQUAL)    return alpha <= reference;
	if (func == FFP_CMP_GREATER)      return alpha >  reference;
	if (func == FFP_CMP_NOTEQUAL)     return alpha != reference;
	if (func == FFP_CMP_GREATEREQUAL) return alpha >= reference;
	return true;	// ALWAYS
}

// Team colour of a ZHCA texture (o_teamColor, RenderCore::Fill_Object_Constants): a texel that is not fully opaque
// takes the side's hue, its saturation is scaled by the side's, its value stays; every texel becomes opaque. The
// same rule as W3DAssetManager's CPU recolouring (RGB_To_HSV / HSV_To_RGB of colorspace.h), applied after filtering.
float4 Team_Color(float4 tex)
{
	if (tex.a < 1.0) {
		const float v = max(tex.r, max(tex.g, tex.b));
		const float s = ((v != 0.0) ? (v - min(tex.r, min(tex.g, tex.b))) / v : 0.0) * o_teamColor.y;
		if (s == 0.0 || o_teamColor.x < 0.0) {
			tex.rgb = v;
		} else {
			const float h = ((o_teamColor.x >= 360.0) ? 0.0 : o_teamColor.x) / 60.0;
			const float k = floor(h);
			const float f = h - k;
			const float p = v * (1.0 - s);
			const float q = v * (1.0 - s * f);
			const float t = v * (1.0 - s * (1.0 - f));
			tex.rgb = (k < 1.0) ? float3(v, t, p) : (k < 2.0) ? float3(q, v, p) : (k < 3.0) ? float3(p, v, t)
					: (k < 4.0) ? float3(p, q, v) : (k < 5.0) ? float3(t, p, v) : float3(v, p, q);
		}
	}
	tex.a = 1.0;
	return tex;
}

#ifdef ALPHA_COVERAGE
// Alpha test as coverage with multisampling. A separate permutation the backend selects only for alpha-tested
// draws without blending into a multisampled target (compare function GREATER or GREATEREQUAL). The alpha around
// the reference becomes a gradient about one pixel wide, its value the share of samples covered: cut-out edges
// (trees, fences, grilles) get the same smoothing as geometry edges. The color output, alpha included, stays as
// without the permutation (the scene alpha carries the water depth code).
struct PS_OUT
{
	float4	color    : SV_Target;
	uint	coverage : SV_Coverage;
};

uint Coverage_Mask(float alpha, float reference, uint samples)
{
	const float width = max(fwidth(alpha), 1.0 / 255.0);		// before any branch (derivative)
	const float share = saturate((alpha - reference) / width + 0.5);
	// Fully transparent texels stay out and opaque ones in, also with a reference of 0 or 1.
	const uint  n     = (alpha <= 0.0) ? 0u : ((alpha >= 1.0) ? samples : (uint)(share * (float)samples + 0.5));
	return (n >= 32u) ? 0xFFFFFFFFu : ((1u << n) - 1u);
}

PS_OUT main(VS_OUT i)
#else
float4 main(VS_OUT i) : SV_Target
#endif
{
#ifdef PIXEL_LIGHT
	// The vertex color and specular of fixed function, with the normal per pixel: normalized, then scaled to the
	// interpolated length of the vertex normals (fixed function does not normalize without NORMALIZENORMALS).
	const float2 nm_uv = (o_stages[0].texgen.w != 0) ? (i.uv0.xy / max(i.uv0.w, 1e-6)) : i.uv0.xy;
	const float3 nm_dpx = ddx(i.fog.yzw), nm_dpy = ddy(i.fog.yzw);
	const float2 nm_dux = ddx(nm_uv), nm_duy = ddy(nm_uv);
	if (o_lightFlags.y != 0) {
		const float len = length(i.normal.xyz);
		float3 n = (len > 1e-6) ? i.normal.xyz * (i.normal.w / len) : float3(0, 0, 0);
		float gloss = 1.0;
		[branch] if (o_teamColor.w > 0.0 && len > 1e-6) {
			const float4 texel = ffp_normal.SampleGrad(ffp_samp0, nm_uv, nm_dux, nm_duy);
			n = Ffp_Normal_Map(i.normal.xyz / len, nm_dpx, nm_dpy, nm_dux, nm_duy, texel) * i.normal.w;
			gloss = texel.b * (255.0 / 128.0);
		}
		float3 diffuse, specular;
		Ffp_Direct_Light(n, i.fog.yzw, diffuse, specular);
		specular *= gloss;
		i.color0.rgb = saturate(i.color0.rgb + diffuse * i.diffuse.rgb);
		i.color1 = float4(saturate(specular * i.color1.rgb), 0.0);
	}
#endif
	const uint team_stages = (uint)o_teamColor.z;
	// --- Stage 0 --------------------------------------------------------------------------------
	FFPStage s0 = o_stages[0];
	float2 uv0 = (s0.texgen.w != 0) ? (i.uv0.xy / max(i.uv0.w, 1e-6)) : i.uv0.xy;
	float4 tex0 = ffp_tex0.Sample(ffp_samp0, uv0);
	if (team_stages & 1u) tex0 = Team_Color(tex0);

	// BUMPENVMAP: stage 0 offsets the coordinates of stage 1.
	float2 uv1 = (o_stages[1].texgen.w != 0) ? (i.uv1.xy / max(i.uv1.w, 1e-6)) : i.uv1.xy;
	float  bump_luminance = 1.0;
	if (s0.color_op.x == FFP_TOP_BUMPENVMAP || s0.color_op.x == FFP_TOP_BUMPENVMAPLUMINANCE) {
		uv1 += float2(s0.bump.x * tex0.r + s0.bump.z * tex0.g,
					  s0.bump.y * tex0.r + s0.bump.w * tex0.g);
		if (s0.color_op.x == FFP_TOP_BUMPENVMAPLUMINANCE)
			bump_luminance = saturate(tex0.b * s0.bump_scale.x + s0.bump_scale.y);
	}
	float4 tex1 = ffp_tex1.Sample(ffp_samp1, uv1);
	if (team_stages & 2u) tex1 = Team_Color(tex1);

	// A stage without a bound texture follows fixed-function rules: D3DTA_TEXTURE yields the vertex
	// color (D3DTA_DIFFUSE), and with COLORARG1 = TEXTURE the chain ends at that stage. Otherwise
	// SELECTARG1(TEXTURE) without a texture would draw white instead of the vertex color.
	// bump_scale.w = 1 if the slot holds a texture (RenderCore::Fill_Object_Constants).
	FFPStage s1 = o_stages[1];
	const bool tex0_bound = s0.bump_scale.w != 0.0;
	const bool tex1_bound = s1.bump_scale.w != 0.0;
	if (!tex0_bound) tex0 = i.color0;
	if (!tex1_bound) tex1 = i.color0;
	const bool run0 = s0.color_op.x != FFP_TOP_DISABLE &&
					  (tex0_bound || (s0.color_op.y & FFP_TA_SELECTMASK) != FFP_TA_TEXTURE);
	const bool run1 = run0 && s1.color_op.x != FFP_TOP_DISABLE &&
					  (tex1_bound || (s1.color_op.y & FFP_TA_SELECTMASK) != FFP_TA_TEXTURE);

	float4 current = i.color0;

	if (run0) {
		const float4 c1 = Combiner_Arg(s0.color_op.y, tex0, i.color0, current, i.color1);
		const float4 c2 = Combiner_Arg(s0.color_op.z, tex0, i.color0, current, i.color1);
		const float4 c0 = Combiner_Arg(s0.color_op.w, tex0, i.color0, current, i.color1);
		const float4 rgb = Combiner_Op(s0.color_op.x, c1, c2, c0, current.a, tex0.a, i.color0.a);

		float alpha = current.a;
		if (s0.alpha_op.x != FFP_TOP_DISABLE) {
			const float4 a1 = Combiner_Arg(s0.alpha_op.y, tex0, i.color0, current, i.color1);
			const float4 a2 = Combiner_Arg(s0.alpha_op.z, tex0, i.color0, current, i.color1);
			const float4 a0 = Combiner_Arg(s0.alpha_op.w, tex0, i.color0, current, i.color1);
			alpha = Combiner_Op(s0.alpha_op.x, a1, a2, a0, current.a, tex0.a, i.color0.a).a;
		}
		// Fixed function clamps the result of each stage to 0..1 (MODULATE2X, ADD, SUBTRACT ...).
		current = saturate(float4(rgb.rgb, alpha));
	}

	// --- Stage 1 --------------------------------------------------------------------------------
	// A disabled stage ends the fixed-function chain; stage 1 then does not count.
	if (run1) {
		const float4 c1 = Combiner_Arg(s1.color_op.y, tex1, i.color0, current, i.color1);
		const float4 c2 = Combiner_Arg(s1.color_op.z, tex1, i.color0, current, i.color1);
		const float4 c0 = Combiner_Arg(s1.color_op.w, tex1, i.color0, current, i.color1);
		const float4 rgb = Combiner_Op(s1.color_op.x, c1, c2, c0, current.a, tex1.a, i.color0.a);

		float alpha = current.a;
		if (s1.alpha_op.x != FFP_TOP_DISABLE) {
			const float4 a1 = Combiner_Arg(s1.alpha_op.y, tex1, i.color0, current, i.color1);
			const float4 a2 = Combiner_Arg(s1.alpha_op.z, tex1, i.color0, current, i.color1);
			const float4 a0 = Combiner_Arg(s1.alpha_op.w, tex1, i.color0, current, i.color1);
			alpha = Combiner_Op(s1.alpha_op.x, a1, a2, a0, current.a, tex1.a, i.color0.a).a;
		}
		current = saturate(float4(rgb.rgb * bump_luminance, alpha));
	}

	// --- Specular, fog, alpha test ---------------------------------------------------------------
	float4 result = current;
	if (o_lightFlags.z != 0) result.rgb += i.color1.rgb;		// SPECULARENABLE: after the stages

	if (f_fogParams.z != 0.0) result.rgb = lerp(f_fogColor.rgb, result.rgb, saturate(i.fog.x));

#ifdef SOFT_PARTICLES
	// Distance to the scene behind along the view axis; goes from 1 to 0 over the fade distance.
	// Blend mode 0: alpha (SRCALPHA blend), 1: color and alpha (additive), 2: toward 1 (multiplicative).
	{
		const float scene_w = Depth_To_W(fx_scene_depth.Load(int3(int2(i.pos.xy), 0)), e_depthRange, e_projB);
		const float own_w   = Depth_To_W(i.pos.z, e_softRange, e_softProjB);
		const float fade    = saturate((scene_w - own_w) * e_soft.x);
		if (e_soft.y < 0.5)      result.a  *= fade;
		else if (e_soft.y < 1.5) result    *= fade;
		else                     result.rgb = lerp(float3(1.0, 1.0, 1.0), result.rgb, fade);
	}
#endif

#ifdef ALPHA_COVERAGE
	PS_OUT o;
	o.coverage = 0xFFFFFFFFu;
	if (o_alphaTest.x != 0 && o_alphaTest.z > 1) {
		o.coverage = Coverage_Mask(saturate(result.a), o_alphaRef.x, (uint)o_alphaTest.z);
		if (o.coverage == 0u) discard;
	}
	else
#endif
	// Fixed-function alpha test compares the 8-bit alpha with ALPHAREF (0..255); in floating point,
	// EQUAL/NOTEQUAL and boundary values would come out differently.
	if (o_alphaTest.x != 0) {
		const float alpha8 = floor(saturate(result.a) * 255.0 + 0.5);
		const float ref8   = floor(o_alphaRef.x * 255.0 + 0.5);
		if (!Alpha_Test_Passes(o_alphaTest.y, alpha8, ref8)) discard;
	}

#ifdef ALPHA_COVERAGE
	o.color = Scene_Out(result, i.pos);
	return o;
#else
	return Scene_Out(result, i.pos);
#endif
}
