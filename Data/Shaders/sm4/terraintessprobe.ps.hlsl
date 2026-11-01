/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// terraintessprobe.ps.hlsl -----------------------------------------------------------------------------
//
// Crack probe of the terrain tessellation (-terrainTessProbe): tessellated terrain in plain green, the scene is
// cleared to red, so every red pixel inside the terrain is a gap between patches.
// ------------------------------------------------------------------------------------------------

#include "common.hlsli"

float4 main(VS_OUT i) : SV_Target
{
	return float4(0.0, 1.0, 0.0, 1.0);
}
