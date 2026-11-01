/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// sceneformat.hlsli ------------------------------------------------------------------------------
//
// Helpers for writing into a scene target wider than 8 bits (-sceneFormat), shared by the scene shaders
// (common.hlsli, Scene_Out) and the full screen passes (effects.hlsli, Fx_Scene_Out).
// ------------------------------------------------------------------------------------------------

#ifndef SCENEFORMAT_HLSLI
#define SCENEFORMAT_HLSLI

// Nearest value of the R11G11B10 grid (6, 6 and 5 mantissa bits). The conversion on write may truncate, which
// over several layers darkens the image; a value on the grid is stored exactly.
float3 Grid_R11G11B10(float3 v)
{
	const float3 steps = float3(64.0, 64.0, 32.0);
	const float3 e = exp2(floor(log2(max(v, 1.0e-6))));
	return round(v / e * steps) / steps * e;
}

#endif // SCENEFORMAT_HLSLI
