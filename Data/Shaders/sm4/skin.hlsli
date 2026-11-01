/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// skin.hlsli ---------------------------------------------------------------------------------------
//
// Skinned meshes on the GPU. W3D skins bind every vertex to exactly one bone
// (MeshGeometryClass::get_deformed_vertices): position and normal are transformed by the bone's world
// matrix, the normal without translation and without renormalization. The bone index comes as UV set 2
// (one float), relative to the first bone of the palette; the palette holds the 3x4 world matrices
// (Matrix3D rows) of the bones the mesh uses.
//
// sk_info.x = bone count; 0 = no skinning (every draw that is not a GPU skin).
// ------------------------------------------------------------------------------------------------

#ifndef SKIN_HLSLI
#define SKIN_HLSLI

#define SKIN_BONES 64

#include "slots.hlsli"
cbuffer cbSkin : register(SLOT_CB_SKIN)
{
	float4	sk_info;
	float4	sk_bones[SKIN_BONES * 3];
};

uint Skin_Bone(float index)
{
	return min((uint)(index + 0.5), (uint)sk_info.x - 1);
}

float3 Skin_Point(uint b, float3 p)
{
	const float4 q = float4(p, 1.0);
	return float3(dot(sk_bones[b * 3], q), dot(sk_bones[b * 3 + 1], q), dot(sk_bones[b * 3 + 2], q));
}

float3 Skin_Vector(uint b, float3 n)
{
	return float3(dot(sk_bones[b * 3].xyz, n), dot(sk_bones[b * 3 + 1].xyz, n), dot(sk_bones[b * 3 + 2].xyz, n));
}

#endif // SKIN_HLSLI
