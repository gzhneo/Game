/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// terraintess.ds.hlsl (ds_5_0) ------------------------------------------------------------------------
//
// Domain shader of the terrain tessellation (terraintess.hlsli). Interpolates the control points like the
// rasterizer would and moves the point along the terrain normal by
//
//   height = sum over the four grid points of the cell (bilinear weight) and their two classes (weight per grid point)
//            of (height of the class texture at the world position - 0.5)
//   offset = height * amplitude * fade with distance * mask (bilinear) * fade on steep slopes (rock layer)
//
// The class height is read where the base pass shows that texture: the class texture is placed in the world
// like getUVForTileIndex places its tiles (cell x, y modulo twice the class width; u along x, v against y).
//
// On steep slopes, where the ground displacement has faded out, the rock displacement takes over: the triplanar
// height of the rock textures (alpha channel), projected and blended like the rock layer does it per pixel
// (terrainrock.hlsli, Terrain_Rock), times the rock share from the slope and that height, times its own fade with
// distance and the mask. The world position passed to the pixel shader keeps the ground offset but not the rock
// offset, so the rock texture stays on the rock it shades.
// ------------------------------------------------------------------------------------------------

#include "terraintess.hlsli"

Texture2D		tt_weights	: register(t1);	///< per grid point: class A, weight A, class B, weight B (RGBA8)
Texture2D		tt_mask		: register(t2);	///< per grid point: 1 = full displacement, 0 = none (R8)
Texture2D		tt_heights	: register(t3);	///< height atlas in the layout of the terrain atlas (R = height, 0.5 = mean)
Texture2D		tt_rock_a	: register(t4);	///< coarse rock texture of the rock layer (A = height)
Texture2D		tt_rock_b	: register(t5);	///< fine rock texture (A = height)
SamplerState	tt_wrap		: register(s1);	///< linear, wrap

/// Height of texture class c at grid coordinates g, mean free (0 for classes without heights).
float Tt_Class(uint c, float2 g)
{
	const float4 t = tt_class[min(c, TT_CLASSES - 1)];
	const float2 m = g - t.z * floor(g / t.z);
	const float2 uv = float2(t.x + m.x * tt_atlas.x, t.y - m.y * tt_atlas.y);
	return (tt_heights.SampleLevel(tt_clamp, uv, 0).r - 0.5) * t.w;
}

/// Mixed height of the terrain textures at grid coordinates g.
float Tt_Height(float2 g)
{
	const float2 c = floor(g);
	const float2 f = g - c;
	float h = 0.0;
	[unroll] for (int k = 0; k < 4; ++k) {
		const int2 o = int2(k & 1, k >> 1);
		const float b = (o.x ? f.x : 1.0 - f.x) * (o.y ? f.y : 1.0 - f.y);
		const float4 e = tt_weights.Load(int3(int2(c) + o, 0));		// outside the grid: zero weights
		[branch] if (b > 0.0 && e.g > 0.0) h += b * e.g * Tt_Class((uint)(e.r * 255.0 + 0.5), g);
		[branch] if (b > 0.0 && e.a > 0.0) h += b * e.a * Tt_Class((uint)(e.b * 255.0 + 0.5), g);
	}
	return h;
}

/// Rock height of one projection (coordinates c in the plane, second axis = image up), as Tr_Project.
float Tt_Rock_Project(float2 c, float lod_a, float lod_b)
{
	const float2x2 DREH = float2x2(0.8, -0.6, 0.6, 0.8);
	const float a = tt_rock_a.SampleLevel(tt_wrap, float2(c.x, -c.y) * tt_rock_scale.x, lod_a).a;
	const float2 cr = mul(DREH, c);
	const float b = tt_rock_b.SampleLevel(tt_wrap, float2(cr.x, -cr.y) * tt_rock_scale.y + float2(0.37, 0.61), lod_b).a;
	return (2.0 * a + b) / 3.0;
}

/// Mip level of rock texture t for domain points about `spacing` world units apart (period = 1 / scale).
float Tt_Rock_Lod(Texture2D t, float scale, float spacing, out float mean)
{
	uint w, h, levels;
	t.GetDimensions(0, w, h, levels);
	mean = t.SampleLevel(tt_wrap, float2(0.5, 0.5), (float)levels - 1.0).a;
	return max(log2(max(spacing * scale * (float)w, 1.0e-3)) + tt_rock_lod.x, 0.0);
}

/// Rock share (x) and mean-free rock height (y) at world position p with terrain normal n.
float2 Tt_Rock(float3 p, float3 n)
{
	const float d = max(length(p - f_cameraPosWorld.xyz), 1.0);
	const float spacing = max(d * tt_rock_lod.z, tt_rock_lod.y);
	float mean_a, mean_b;
	const float lod_a = Tt_Rock_Lod(tt_rock_a, tt_rock_scale.x, spacing, mean_a);
	const float lod_b = Tt_Rock_Lod(tt_rock_b, tt_rock_scale.y, spacing, mean_b);

	// triplanar weights and height-based blend as in Terrain_Rock
	float3 w = n * n;
	w *= w;
	w /= dot(w, float3(1, 1, 1));
	float3 h = float3(0.5, 0.5, 0.5);
	[branch] if (w.x > 0.01) h.x = Tt_Rock_Project(p.yz, lod_a, lod_b);
	[branch] if (w.y > 0.01) h.y = Tt_Rock_Project(p.xz, lod_a, lod_b);
	[branch] if (w.z > 0.01) h.z = Tt_Rock_Project(p.xy, lod_a, lod_b);
	const float3 on = step(0.01, w);
	const float3 hw = w + (h - 0.5) * 0.6 * on;
	w = max(hw - (max(hw.x, max(hw.y, hw.z)) - 0.12), 0.0) * on;
	w /= max(dot(w, float3(1, 1, 1)), 1e-4);
	const float height = dot(w, h);
	const float share = saturate((1.0 - n.z - tt_rock.y) * tt_rock.z + (height - 0.5) * tt_rock.w);
	return float2(share, height - (2.0 * mean_a + mean_b) / 3.0);
}

/// An edge point from the two end points in an order that does not depend on the patch.
void Tt_Edge(VS_OUT a, VS_OUT b, float ta, float tb, out float4 pos, out float3 world)
{
	if (Tt_Less(b.fog.yzw, a.fog.yzw)) {
		precise const float4 p = b.pos * tb + a.pos * ta;
		precise const float3 w = b.fog.yzw * tb + a.fog.yzw * ta;
		pos = p;
		world = w;
	}
	else {
		precise const float4 p = a.pos * ta + b.pos * tb;
		precise const float3 w = a.fog.yzw * ta + b.fog.yzw * tb;
		pos = p;
		world = w;
	}
}

[domain("tri")]
VS_OUT main(TT_PATCH patch, float3 bc : SV_DomainLocation, const OutputPatch<VS_OUT, 3> p)
{
	VS_OUT o;
	o.color0 = p[0].color0 * bc.x + p[1].color0 * bc.y + p[2].color0 * bc.z;
	o.color1 = p[0].color1 * bc.x + p[1].color1 * bc.y + p[2].color1 * bc.z;
	o.uv0 = p[0].uv0 * bc.x + p[1].uv0 * bc.y + p[2].uv0 * bc.z;
	o.uv1 = p[0].uv1 * bc.x + p[1].uv1 * bc.y + p[2].uv1 * bc.z;
	o.uv2 = p[0].uv2 * bc.x + p[1].uv2 * bc.y + p[2].uv2 * bc.z;
	o.uv3 = p[0].uv3 * bc.x + p[1].uv3 * bc.y + p[2].uv3 * bc.z;
	const float fog = p[0].fog.x * bc.x + p[1].fog.x * bc.y + p[2].fog.x * bc.z;

	// Position and world position: points on an edge only from its two end points, so the neighbouring patch
	// computes the same values.
	float4 pos;
	float3 world;
	if (bc.x == 0.0)
		Tt_Edge(p[1], p[2], bc.y, bc.z, pos, world);
	else if (bc.y == 0.0)
		Tt_Edge(p[0], p[2], bc.x, bc.z, pos, world);
	else if (bc.z == 0.0)
		Tt_Edge(p[0], p[1], bc.x, bc.y, pos, world);
	else {
		precise const float4 pi = p[0].pos * bc.x + p[1].pos * bc.y + p[2].pos * bc.z;
		precise const float3 wi = p[0].fog.yzw * bc.x + p[1].fog.yzw * bc.y + p[2].fog.yzw * bc.z;
		pos = pi;
		world = wi;
	}

	const float amplitude = tt_fade.w * Tt_Fade(world);
	const float rock_amplitude = (tt_rock.x > 0.0) ? tt_rock.x * Tt_Rock_Fade(world) : 0.0;
	[branch] if (amplitude > 0.0 || rock_amplitude > 0.0) {
		const float3 base = world;
		const float2 g = world.xy * tt_map.x + tt_map.y;
		const float3 n = Tt_Normal(g);
		const float steep = saturate((tt_shape.z - (1.0 - n.z)) * tt_shape.w);
		const float mask = tt_mask.SampleLevel(tt_clamp, (g + 0.5) * tt_map.zw, 0).r;
		const float k = amplitude * steep * mask;
		[branch] if (k > 0.0) {
			const float3 offset = n * (Tt_Height(g) * k);
			world += offset;
			pos += mul(float4(offset, 0.0), f_viewProj);
		}
		[branch] if (rock_amplitude * mask > 0.0 && Tt_Rock_Possible(n) > 0.0) {
			const float2 rock = Tt_Rock(base, n);
			const float3 offset = n * (rock.y * rock.x * rock_amplitude * mask);
			pos += mul(float4(offset, 0.0), f_viewProj);
		}
	}
	o.pos = pos;
	o.fog = float4(fog, world);
	return o;
}
