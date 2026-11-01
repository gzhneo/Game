/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// terraintess.hs.hlsl (hs_5_0) ------------------------------------------------------------------------
//
// Hull shader of the terrain tessellation (terraintess.hlsli). Edge factor = screen length of the edge divided
// by the target length, limited to 1 .. largest factor, blended to 1 where the displacement fades out (far away;
// on rock slopes the rock displacement reaches farther). Patches outside the view frustum, widened by the largest
// displacement, get factor 0.
// ------------------------------------------------------------------------------------------------

#include "terraintess.hlsli"

/// Index of the control point with the smallest world position.
uint Tt_First(InputPatch<VS_OUT, 3> p)
{
	uint r = 0;
	if (Tt_Less(p[1].fog.yzw, p[0].fog.yzw)) r = 1;
	if (Tt_Less(p[2].fog.yzw, p[r].fog.yzw)) r = 2;
	return r;
}

/// Rock share possible at world position p (0 = no rock displacement there).
float Tt_Rock_At(float3 p)
{
	return Tt_Rock_Possible(Tt_Normal(p.xy * tt_map.x + tt_map.y));
}

/// Factor of the edge a-b; symmetric in a and b, so both patches at an edge get the same value.
float Tt_Edge_Factor(VS_OUT a, VS_OUT b)
{
	precise float pixels = 1.0e6;			// an edge through the camera plane gets the largest factor
	if (a.pos.w > 1.0e-3 && b.pos.w > 1.0e-3) {
		precise const float2 sa = a.pos.xy / a.pos.w * tt_view.xy;
		precise const float2 sb = b.pos.xy / b.pos.w * tt_view.xy;
		precise const float2 d = sa - sb;
		pixels = sqrt(d.x * d.x + d.y * d.y);
	}
	precise const float3 mid = (a.fog.yzw + b.fog.yzw) * 0.5;
	precise float fade = Tt_Fade(mid) * tt_atlas.w;		// w = 0: no ground heights, only rock slopes tessellate
	// Rock slopes: the rock displacement starts farther away; the slope is taken at both ends and the middle.
	[branch] if (tt_rock.x > 0.0) {
		precise const float rock_fade = Tt_Rock_Fade(mid);
		[branch] if (rock_fade > fade) {
			precise const float share = max(max(Tt_Rock_At(a.fog.yzw), Tt_Rock_At(b.fog.yzw)), Tt_Rock_At(mid));
			fade = max(fade, rock_fade * share);
		}
	}
	precise const float f = clamp(pixels * tt_view.z, 1.0, tt_view.w);
	return 1.0 + (f - 1.0) * fade;
}

/// True if all three points lie beyond one side plane of the view frustum (widened by the cull margin).
bool Tt_Outside(float4 p0, float4 p1, float4 p2)
{
	const float3 cx = float3(f_viewProj[0][0], f_viewProj[1][0], f_viewProj[2][0]);
	const float3 cy = float3(f_viewProj[0][1], f_viewProj[1][1], f_viewProj[2][1]);
	const float3 cw = float3(f_viewProj[0][3], f_viewProj[1][3], f_viewProj[2][3]);
	const float4 margin = -tt_shape.y * float4(length(cw - cx), length(cw + cx), length(cw - cy), length(cw + cy));
	const float4 s0 = float4(p0.w - p0.x, p0.w + p0.x, p0.w - p0.y, p0.w + p0.y);
	const float4 s1 = float4(p1.w - p1.x, p1.w + p1.x, p1.w - p1.y, p1.w + p1.y);
	const float4 s2 = float4(p2.w - p2.x, p2.w + p2.x, p2.w - p2.y, p2.w + p2.y);
	return any((s0 < margin) && (s1 < margin) && (s2 < margin));
}

/// Patch constants from the rotated control points.
TT_PATCH Tt_Patch(const OutputPatch<VS_OUT, 3> p)
{
	TT_PATCH o;
	if (Tt_Outside(p[0].pos, p[1].pos, p[2].pos)) {
		o.edges[0] = 0.0;
		o.edges[1] = 0.0;
		o.edges[2] = 0.0;
		o.inside = 0.0;
		return o;
	}
	// edge i lies opposite control point i (domain coordinate i = 0 on it)
	o.edges[0] = Tt_Edge_Factor(p[1], p[2]);
	o.edges[1] = Tt_Edge_Factor(p[2], p[0]);
	o.edges[2] = Tt_Edge_Factor(p[0], p[1]);
	o.inside = (o.edges[0] + o.edges[1] + o.edges[2]) * (1.0 / 3.0);
	return o;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("Tt_Patch")]
[maxtessfactor(64.0)]
VS_OUT main(InputPatch<VS_OUT, 3> p, uint i : SV_OutputControlPointID)
{
	return p[(i + Tt_First(p)) % 3];
}
