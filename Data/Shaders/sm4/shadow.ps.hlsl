/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// shadow.ps.hlsl ---------------------------------------------------------------------------------
//
// Alpha test of shadow casters while drawing the shadow map (trees, fences, grids). Only depth is
// written; draws without alpha test run without a pixel shader. Alpha is taken from the stage 0
// texture (the source for every alpha-tested draw in the game) and compared at 8 bits as in
// ffp.ps.hlsl.
// ------------------------------------------------------------------------------------------------

#include "shadow.hlsli"

Texture2D    sh_tex  : register(t0);
SamplerState sh_samp : register(s0);

bool Alpha_Passes(float func, float alpha, float reference)
{
	if (func < 1.5) return false;					// NEVER
	if (func < 2.5) return alpha <  reference;		// LESS
	if (func < 3.5) return alpha == reference;		// EQUAL
	if (func < 4.5) return alpha <= reference;		// LESSEQUAL
	if (func < 5.5) return alpha >  reference;		// GREATER
	if (func < 6.5) return alpha != reference;		// NOTEQUAL
	if (func < 7.5) return alpha >= reference;		// GREATEREQUAL
	return true;									// ALWAYS
}

void main(float4 pos : SV_Position, float2 uv : TEXCOORD0)
{
	const float a8 = floor(saturate(sh_tex.Sample(sh_samp, uv).a) * 255.0 + 0.5);
	if (!Alpha_Passes(sd_alpha.y, a8, sd_alpha.x)) discard;
}
