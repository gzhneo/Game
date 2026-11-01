/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// renderslots.h -- register slots of the D3D11 shaders, C++ side of Data/Shaders/sm4/slots.hlsli
// (same names without the RENDER_ prefix). tools/render/plaetze_pruefen.py checks both files.
//
// D3D11 has 14 constant buffer slots per stage and all are used, so some slots carry two or three
// buffers that never meet: on different stages or in passes of their own (comments). Textures and
// samplers 0..7 are the fixed-function stages; only the slots with a fixed role are named here.
#pragma once
#ifndef RENDERSLOTS_H
#define RENDERSLOTS_H

enum RenderSlot : unsigned
{
	// Constant buffers
	RENDER_SLOT_CB_FRAME			= 0,	///< all stages
	RENDER_SLOT_CB_OBJECT			= 1,	///< VS, PS
	RENDER_SLOT_CB_PIECE_VS			= 2,	///< VS, layout of the drawing piece (renderpieces.h)
	RENDER_SLOT_CB_PIECE_PS			= 3,	///< PS, layout of the drawing piece (renderpieces.h)
	RENDER_SLOT_CB_EFFECTS			= 4,	///< PS
	RENDER_SLOT_CB_SHADOW_DRAW		= 5,	///< VS, PS: shadow map pass only
	RENDER_SLOT_CB_SHADOW_APPLY		= 5,	///< PS: shadow mask pass only
	RENDER_SLOT_CB_UPSCALE			= 5,	///< PS: upscale pass only
	RENDER_SLOT_CB_TAA				= 5,	///< PS: TAA pass only
	RENDER_SLOT_CB_TERRAIN_DETAIL	= 6,	///< PS
	RENDER_SLOT_CB_TERRAIN_ROCK		= 7,	///< PS
	RENDER_SLOT_CB_TERRAIN_LIGHTS	= 8,	///< VS
	RENDER_SLOT_CB_DYN_LIGHTS		= 8,	///< PS: post-processing pass
	RENDER_SLOT_CB_SKIN				= 9,	///< VS
	RENDER_SLOT_CB_PARTICLE			= 10,	///< VS
	RENDER_SLOT_CB_PARTICLE_TABLE	= 11,	///< VS
	RENDER_SLOT_CB_INSTANCES		= 12,	///< VS
	RENDER_SLOT_CB_TERRAIN_NORMAL	= 12,	///< PS
	RENDER_SLOT_CB_TERRAIN_TESS		= 13,	///< HS, DS

	// Textures with a fixed role
	RENDER_SLOT_T_SCENE_DEPTH		= 8,
	RENDER_SLOT_T_DETAIL_A			= 9,
	RENDER_SLOT_T_DETAIL_B			= 10,
	RENDER_SLOT_T_TERRAIN_HEIGHT	= 11,	///< PS and VS
	RENDER_SLOT_T_ROCK_A			= 12,
	RENDER_SLOT_T_ROCK_B			= 13,
	RENDER_SLOT_T_RELIEF			= 14,
	RENDER_SLOT_T_MODEL_NORMAL		= 14,	///< PS, model draws lit per pixel
	RENDER_SLOT_T_SCENE_ALPHA		= 15,	///< destination alpha of a scene target without alpha

	// Samplers with a fixed role
	RENDER_SLOT_S_DETAIL			= 9,
	RENDER_SLOT_S_ROCK				= 10,
	RENDER_SLOT_S_TERRAIN_HEIGHT	= 11,
};

#endif // RENDERSLOTS_H
