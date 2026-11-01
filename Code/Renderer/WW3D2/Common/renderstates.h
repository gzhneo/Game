/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// renderstates.h ----------------------------------------------------------------------------------
//
// Translation of the state shadow into D3D11 descriptions.
//
// The functions are pure mappings without device access -- input is the shadow from
// RenderCore (the WW3D_RS_* and WW3D_SAMP_* fields), output a finished *_DESC. That keeps
// them testable on their own; rendercore.cpp only holds the caches.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef RENDERSTATES_H
#define RENDERSTATES_H

#include "renderobjects.h"

enum {
	RENDER_STATE_COUNT  = 256,    ///< size of the render state shadow
	RENDER_SAMPLER_STATE_COUNT = 16      ///< size of the sampler state shadow per slot
};

/// D3DCOLOR (AARRGGBB) to linear float4 (r,g,b,a) in 0..1.
void Render_Color_To_Float4(unsigned argb, float* out4);

void Render_Build_Blend_Desc(const unsigned* render_states, GPU_BLEND_DESC& desc);
void Render_Build_Depth_Stencil_Desc(const unsigned* render_states, GPU_DEPTH_STENCIL_DESC& desc);
void Render_Build_Rasterizer_Desc(const unsigned* render_states, GPU_RASTERIZER_DESC& desc,
								 bool multisampled_target);
void Render_Build_Sampler_Desc(const unsigned* sampler_states, GPU_SAMPLER_DESC& desc);

/// WW3D_DECLTYPE_* -> DXGI format of the vertex attribute. D3DCOLOR is declared as
/// R8G8B8A8_UNORM; the shader swizzles .bgra.
GPU_FORMAT Render_Decl_Type_To_Gpu_Format(unsigned decl_type);
/// WW3D_DECLUSAGE_* -> HLSL semantic. NULL if the usage is not mapped.
const char* Render_Decl_Usage_To_Semantic(unsigned decl_usage);

/// WW3D_PT_* -> topology. TRIANGLEFAN has no D3D11 equivalent and returns UNDEFINED.
GPU_PRIMITIVE_TOPOLOGY Render_Primitive_Topology(unsigned primitive_type);
/// Number of indices/vertices for prim_count primitives of the type.
unsigned Render_Vertex_Count_For_Primitives(unsigned primitive_type, unsigned prim_count);

#endif // RENDERSTATES_H
