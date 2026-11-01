/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// slots.hlsli ------------------------------------------------------------------------------------
//
// Register slots of the constant buffers and of the textures and samplers with a fixed role. The C++
// side is WW3D2/renderslots.h; tools/render/plaetze_pruefen.py checks that both say the same and that
// no shader names such a slot by number.
//
// D3D11 has 14 constant buffer slots per stage and all are used, so some slots carry two or three
// buffers. They never meet: either on different stages (VS against PS) or in passes of their own.
// Textures and samplers 0..7 are the fixed-function stages (texture stage n = slot n); full-screen
// passes, the shadow mask and the tessellation stages use them for their own inputs.
// ------------------------------------------------------------------------------------------------

#ifndef SLOTS_HLSLI
#define SLOTS_HLSLI

// --- Constant buffers ----------------------------------------------------------------------------
#define SLOT_CB_FRAME			b0		// all stages: camera, projection, fog, scene light, viewport
#define SLOT_CB_OBJECT			b1		// VS, PS: world matrices, material, fixed-function state
#define SLOT_CB_PIECE_VS		b2		// VS: layout of the drawing piece (renderpieces.h: trees, sea)
#define SLOT_CB_PIECE_PS		b3		// PS: layout of the drawing piece (renderpieces.h: water, sea, filters)
#define SLOT_CB_EFFECTS			b4		// PS: effects, post-processing, soft particles
#define SLOT_CB_SHADOW_DRAW		b5		// VS, PS: shadow map pass only
#define SLOT_CB_SHADOW_APPLY	b5		// PS: shadow mask pass only
#define SLOT_CB_UPSCALE			b5		// PS: upscale pass only
#define SLOT_CB_TAA				b5		// PS: TAA pass only
#define SLOT_CB_TERRAIN_DETAIL	b6		// PS: terrain detail layer
#define SLOT_CB_TERRAIN_ROCK	b7		// PS: rock layer
#define SLOT_CB_TERRAIN_LIGHTS	b8		// VS: terrain lights
#define SLOT_CB_DYN_LIGHTS		b8		// PS: dynamic lights of the post-processing pass
#define SLOT_CB_SKIN			b9		// VS: bone palette
#define SLOT_CB_PARTICLE		b10		// VS: particle draw
#define SLOT_CB_PARTICLE_TABLE	b11		// VS: particle orientation table
#define SLOT_CB_INSTANCES		b12		// VS: mesh instances
#define SLOT_CB_TERRAIN_NORMAL	b12		// PS: terrain relief
#define SLOT_CB_TERRAIN_TESS	b13		// HS, DS: terrain tessellation

// --- Textures with a fixed role (PS unless noted) -------------------------------------------------
#define SLOT_T_SCENE_DEPTH		t8		// depth copy for soft particles
#define SLOT_T_DETAIL_A			t9		// terrain detail textures
#define SLOT_T_DETAIL_B			t10
#define SLOT_T_TERRAIN_HEIGHT	t11		// map height field (PS and VS)
#define SLOT_T_ROCK_A			t12		// rock textures
#define SLOT_T_ROCK_B			t13
#define SLOT_T_RELIEF			t14		// relief atlas
#define SLOT_T_MODEL_NORMAL		t14		// normal map of a model lit per pixel
#define SLOT_T_SCENE_ALPHA		t15		// destination alpha of a scene target without alpha

// --- Samplers with a fixed role (PS) -------------------------------------------------------------
#define SLOT_S_DETAIL			s9
#define SLOT_S_ROCK				s10
#define SLOT_S_TERRAIN_HEIGHT	s11

#endif // SLOTS_HLSLI
