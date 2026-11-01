/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// depthresolve.ps.hlsl ---------------------------------------------------------------------------
//
// Multisampled scene depth into a single-sample float target of the same size, for everything that reads the
// depth copy (SSAO, soft particles, haze, lights, shadow mask, water). Takes sample 0 instead of averaging: an
// average of foreground and background depth at an edge is a depth where nothing is.
// ------------------------------------------------------------------------------------------------

Texture2DMS<float> texDepth : register(t0);

float main(float4 pos : SV_Position) : SV_Target
{
	return texDepth.Load(int2(pos.xy), 0);
}
