/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// fullscreen.vs.hlsl -----------------------------------------------------------------------------
//
// One triangle covering the whole viewport, without a vertex buffer (Draw(3) with SV_VertexID).
// The post-processing pixel shaders read their position from SV_Position (pixel centers at .5,
// as D3D11 rasterizes).
// ------------------------------------------------------------------------------------------------

float4 main(uint id : SV_VertexID) : SV_Position
{
	const float2 t = float2((id << 1) & 2, id & 2);
	return float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
