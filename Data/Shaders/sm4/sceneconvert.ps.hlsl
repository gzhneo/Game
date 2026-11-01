/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// sceneconvert.ps.hlsl ---------------------------------------------------------------------------
//
// Writes a float scene target (-sceneFormat rgba16f) into an 8-bit target of the same size: each texel is
// read exactly (Load, no filtering) at the pixel it is written to, all four channels (the alpha carries the
// water depth code). The 8-bit target rounds on write, as every draw into an 8-bit scene target does.
// ------------------------------------------------------------------------------------------------

Texture2D<float4> texScene : register(t0);
#ifdef SCENE_ALPHA
Texture2D<float>  texAlpha : register(t1);		// destination alpha of a scene target without alpha
#endif

#ifdef TONEMAP
#include "effects.hlsli"

// Tone curve of RENDER_EFFECT_HDR: exposure e_tone.y, then linear up to the knee e_tone.x (0.8: the image as without
// HDR), above it a soft shoulder towards 1 with the same slope at the knee (tanh of the part above it): with knee 0.8,
// 1.0 becomes 0.95, 1.5 almost 1. Applied to the largest channel and the color scaled with it, so the hue stays and
// nothing clips.
float3 Tone_Curve(float3 c)
{
	c *= e_tone.y;
	const float knee = e_tone.x;
	const float m = max(c.r, max(c.g, c.b));
	if (m <= knee) return c;
	// tanh written out: not every HLSL compiler the game meets has it.
	const float x = min((m - knee) / (1.0 - knee), 20.0);	// tanh is 1 there; keeps exp finite
	const float t = knee + (1.0 - knee) * (1.0 - 2.0 / (exp(2.0 * x) + 1.0));
	return c * (t / m);
}
#endif

float4 main(float4 pos : SV_Position) : SV_Target
{
	float4 c = texScene.Load(int3(int2(pos.xy), 0));
#ifdef TONEMAP
	c.rgb = Tone_Curve(max(c.rgb, 0.0));
#endif
#ifdef SCENE_ALPHA
	c.a = texAlpha.Load(int3(int2(pos.xy), 0));
#endif
	return c;
}
