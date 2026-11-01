/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
**	Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// treesway.hlsli -- wind displacement shared by Trees.vs.hlsl and shadow.vs.hlsl (the shadow must
// move with the tree).
//
// sway.xyz is the sway vector of the tree's sway type (displacement per unit of height), sway.w the
// render clock in seconds (wrapped so that sin(5 * t) is continuous). height is the point's height
// above the base. Grass and bushes (small height) behave as a rigid lean; tall trees get a stiff
// trunk and a crown that bends more (quadratic profile), a smaller overall angle, and a small fast
// flutter of branches and leaves with its own phase per point.

float3 TreeSwayOffset(float3 pos, float height, float4 sway)
{
	// 0 for grass and bushes, 1 for tall trees
	const float tall = saturate((height - 6.0f) / 40.0f);

	const float bent = lerp(height, height * height * (1.0f / 48.0f), tall);
	float3 offset = sway.xyz * bent * lerp(1.0f, 0.8f, tall);

	const float phase = dot(pos.xy, float2(0.173f, 0.241f)) + pos.z * 0.31f;
	const float flutter = sin(sway.w * 5.0f + phase);
	offset.xy += sway.xy * (flutter * 0.12f * height * tall);
	return offset;
}
