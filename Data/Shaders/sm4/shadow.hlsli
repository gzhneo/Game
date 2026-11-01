/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// shadow.hlsli -----------------------------------------------------------------------------------
//
// Constant buffer for sun shadows via shadow map. The C++ counterpart is in WW3D2/rendercb.h
// (FFPShadowDrawCB, FFPShadowApplyCB, static_assert on the sizes). Used only with
// "-d3d11Effects shadowmap"; all other shaders stay unchanged.
// ------------------------------------------------------------------------------------------------

#ifndef FFP_SHADOW_HLSLI
#define FFP_SHADOW_HLSLI

// Per shadow caster while drawing the shadow map (shadow.vs.hlsl, shadow.ps.hlsl).
#include "slots.hlsli"
cbuffer cbShadowDraw : register(SLOT_CB_SHADOW_DRAW)
{
	row_major float4x4	sd_worldLight;		///< object space -> light clip (world matrix times light view projection)
	row_major float4x4	sd_texMatrix;		///< texture matrix of stage 0 (alpha test only)
	float4				sd_uv;				///< x = texture matrix on, y = component count of the UV set (1..4)
	float4				sd_alpha;			///< x = reference value 0..255, y = compare function (WW3D_CMP_*)
	float4				sd_sway[11];		///< trees only: the sway vectors of RenderTreesVS (renderpieces.h)
};

#endif // FFP_SHADOW_HLSLI
