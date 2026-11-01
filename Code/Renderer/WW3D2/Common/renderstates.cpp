/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// renderstates.cpp --------------------------------------------------------------------------------
//
// Translates render, depth/stencil, rasterizer and sampler states from the state shadow into
// D3D11 descriptions.
//
// Not mapped (D3D11 has no equivalent, and the difference does not matter for this game):
//   WW3D_RS_SHADEMODE     -- Gouraud is the only mode; FLAT is never used.
//   WW3D_RS_DITHERENABLE  -- no equivalent; no visible effect at 8 bits per channel.
//   WW3D_RS_LASTPIXEL     -- lines only; the engine never sets it to FALSE.
//   WW3D_RS_CLIPPING      -- D3D11 always clips; DepthClipEnable stays on.
//   WW3D_RS_WRAP0..3      -- fixed-function texture coordinate wrapping; no callers.
//   WW3D_RS_MULTISAMPLEMASK -- always 0xFFFFFFFF; D3D11 sets the mask at bind time.
//   WW3D_RS_AMBIENT, WW3D_RS_*MATERIALSOURCE, WW3D_RS_NORMALIZENORMALS, WW3D_RS_LOCALVIEWER,
//   WW3D_RS_COLORVERTEX, WW3D_RS_SPECULARENABLE, WW3D_RS_FOG*, WW3D_RS_ALPHATESTENABLE,
//   WW3D_RS_ALPHAFUNC, WW3D_RS_ALPHAREF, WW3D_RS_TEXTUREFACTOR
//                          -- part of the fixed-function emulation; they go into the constant
//                             buffers (rendercore.cpp) instead of a state object.
// ------------------------------------------------------------------------------------------------

#include "renderstates.h"

// ==================================================================================================
// Helpers
// ==================================================================================================

/// A float stored as a DWORD (the convention for FOGSTART, DEPTHBIAS, BUMPENVMAT, ...).
static inline float As_Float(unsigned bits)
{
	float f;
	::memcpy(&f, &bits, sizeof(f));
	return f;
}

void Render_Color_To_Float4(unsigned argb, float* out4)
{
	out4[0] = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
	out4[1] = static_cast<float>((argb >>  8) & 0xFF) / 255.0f;
	out4[2] = static_cast<float>((argb      ) & 0xFF) / 255.0f;
	out4[3] = static_cast<float>((argb >> 24) & 0xFF) / 255.0f;
}

static GPU_BLEND To_Blend(unsigned ww3d_blend, bool alpha_channel)
{
	switch (ww3d_blend) {
	case WW3D_BLEND_ZERO:           return GPU_BLEND_ZERO;
	case WW3D_BLEND_ONE:            return GPU_BLEND_ONE;
	// D3D11 allows only alpha factors in the alpha channel; the WW3D blend model implicitly
	// reduces color factors there to their alpha part.
	case WW3D_BLEND_SRCCOLOR:       return alpha_channel ? GPU_BLEND_SRC_ALPHA      : GPU_BLEND_SRC_COLOR;
	case WW3D_BLEND_INVSRCCOLOR:    return alpha_channel ? GPU_BLEND_INV_SRC_ALPHA  : GPU_BLEND_INV_SRC_COLOR;
	case WW3D_BLEND_SRCALPHA:       return GPU_BLEND_SRC_ALPHA;
	case WW3D_BLEND_INVSRCALPHA:    return GPU_BLEND_INV_SRC_ALPHA;
	case WW3D_BLEND_DESTALPHA:      return GPU_BLEND_DEST_ALPHA;
	case WW3D_BLEND_INVDESTALPHA:   return GPU_BLEND_INV_DEST_ALPHA;
	case WW3D_BLEND_DESTCOLOR:      return alpha_channel ? GPU_BLEND_DEST_ALPHA     : GPU_BLEND_DEST_COLOR;
	case WW3D_BLEND_INVDESTCOLOR:   return alpha_channel ? GPU_BLEND_INV_DEST_ALPHA : GPU_BLEND_INV_DEST_COLOR;
	case WW3D_BLEND_SRCALPHASAT:    return GPU_BLEND_SRC_ALPHA_SAT;
	default:                        return GPU_BLEND_ONE;
	}
}

static GPU_BLEND_OP To_Blend_Op(unsigned op)
{
	switch (op) {
	case WW3D_BLENDOP_SUBTRACT:     return GPU_BLEND_OP_SUBTRACT;
	case WW3D_BLENDOP_REVSUBTRACT:  return GPU_BLEND_OP_REV_SUBTRACT;
	case WW3D_BLENDOP_MIN:          return GPU_BLEND_OP_MIN;
	case WW3D_BLENDOP_MAX:          return GPU_BLEND_OP_MAX;
	default:                        return GPU_BLEND_OP_ADD;
	}
}

static GPU_COMPARISON_FUNC To_Compare(unsigned func)
{
	switch (func) {
	case WW3D_CMP_NEVER:         return GPU_COMPARISON_NEVER;
	case WW3D_CMP_LESS:          return GPU_COMPARISON_LESS;
	case WW3D_CMP_EQUAL:         return GPU_COMPARISON_EQUAL;
	case WW3D_CMP_LESSEQUAL:     return GPU_COMPARISON_LESS_EQUAL;
	case WW3D_CMP_GREATER:       return GPU_COMPARISON_GREATER;
	case WW3D_CMP_NOTEQUAL:      return GPU_COMPARISON_NOT_EQUAL;
	case WW3D_CMP_GREATEREQUAL:  return GPU_COMPARISON_GREATER_EQUAL;
	default:                     return GPU_COMPARISON_ALWAYS;
	}
}

static GPU_STENCIL_OP To_Stencil_Op(unsigned op)
{
	switch (op) {
	case WW3D_STENCILOP_ZERO:     return GPU_STENCIL_OP_ZERO;
	case WW3D_STENCILOP_REPLACE:  return GPU_STENCIL_OP_REPLACE;
	case WW3D_STENCILOP_INCRSAT:  return GPU_STENCIL_OP_INCR_SAT;
	case WW3D_STENCILOP_DECRSAT:  return GPU_STENCIL_OP_DECR_SAT;
	case WW3D_STENCILOP_INVERT:   return GPU_STENCIL_OP_INVERT;
	case WW3D_STENCILOP_INCR:     return GPU_STENCIL_OP_INCR;
	case WW3D_STENCILOP_DECR:     return GPU_STENCIL_OP_DECR;
	default:                      return GPU_STENCIL_OP_KEEP;
	}
}

// ==================================================================================================
// Blend
// ==================================================================================================

void Render_Build_Blend_Desc(const unsigned* rs, GPU_BLEND_DESC& desc)
{
	::ZeroMemory(&desc, sizeof(desc));
	desc.AlphaToCoverageEnable  = FALSE;
	desc.IndependentBlendEnable = FALSE;

	GPU_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];
	rt.BlendEnable = rs[WW3D_RS_ALPHABLENDENABLE] ? TRUE : FALSE;
	rt.SrcBlend    = To_Blend(rs[WW3D_RS_SRCBLEND],  false);
	rt.DestBlend   = To_Blend(rs[WW3D_RS_DESTBLEND], false);
	rt.BlendOp     = To_Blend_Op(rs[WW3D_RS_BLENDOP]);
	// Without separate alpha blending the alpha channel uses the same factors.
	rt.SrcBlendAlpha  = To_Blend(rs[WW3D_RS_SRCBLEND],  true);
	rt.DestBlendAlpha = To_Blend(rs[WW3D_RS_DESTBLEND], true);
	rt.BlendOpAlpha   = rt.BlendOp;

	// WW3D_RS_COLORWRITEENABLE uses the same bits as D3D11_COLOR_WRITE_ENABLE_*.
	const unsigned mask = rs[WW3D_RS_COLORWRITEENABLE];
	rt.RenderTargetWriteMask = static_cast<UINT8>(mask & 0x0F);
}

// ==================================================================================================
// Depth and stencil
// ==================================================================================================

void Render_Build_Depth_Stencil_Desc(const unsigned* rs, GPU_DEPTH_STENCIL_DESC& desc)
{
	::ZeroMemory(&desc, sizeof(desc));

	// D3DZB_FALSE = 0, D3DZB_TRUE = 1, D3DZB_USEW = 2. D3D11 has no W buffer; the engine never
	// sets it, and USEW is treated like TRUE.
	desc.DepthEnable    = rs[WW3D_RS_ZENABLE] ? TRUE : FALSE;
	desc.DepthWriteMask = rs[WW3D_RS_ZWRITEENABLE] ? GPU_DEPTH_WRITE_MASK_ALL : GPU_DEPTH_WRITE_MASK_ZERO;
	desc.DepthFunc      = To_Compare(rs[WW3D_RS_ZFUNC]);

	desc.StencilEnable    = rs[WW3D_RS_STENCILENABLE] ? TRUE : FALSE;
	desc.StencilReadMask  = static_cast<UINT8>(rs[WW3D_RS_STENCILMASK] & 0xFF);
	desc.StencilWriteMask = static_cast<UINT8>(rs[WW3D_RS_STENCILWRITEMASK] & 0xFF);

	GPU_DEPTH_STENCILOP_DESC op;
	op.StencilFailOp      = To_Stencil_Op(rs[WW3D_RS_STENCILFAIL]);
	op.StencilDepthFailOp = To_Stencil_Op(rs[WW3D_RS_STENCILZFAIL]);
	op.StencilPassOp      = To_Stencil_Op(rs[WW3D_RS_STENCILPASS]);
	op.StencilFunc        = To_Compare(rs[WW3D_RS_STENCILFUNC]);
	// No two-sided stencil needed: both faces get the same operations.
	desc.FrontFace = op;
	desc.BackFace  = op;
}

// ==================================================================================================
// Rasterizer
// ==================================================================================================

void Render_Build_Rasterizer_Desc(const unsigned* rs, GPU_RASTERIZER_DESC& desc, bool multisampled_target)
{
	::ZeroMemory(&desc, sizeof(desc));

	switch (rs[WW3D_RS_FILLMODE]) {
	case WW3D_FILL_WIREFRAME:  desc.FillMode = GPU_FILL_WIREFRAME; break;
	// D3D11 has no WW3D_FILL_POINT; the engine never sets it (only SOLID/WIREFRAME).
	default:                   desc.FillMode = GPU_FILL_SOLID;     break;
	}

	// WW3D_CULL_* names the winding to discard in screen space. With FrontCounterClockwise =
	// FALSE "front" is clockwise, so D3DCULL_CW = CULL_FRONT and D3DCULL_CCW = CULL_BACK.
	desc.FrontCounterClockwise = FALSE;
	switch (rs[WW3D_RS_CULLMODE]) {
	case WW3D_CULL_CW:   desc.CullMode = GPU_CULL_FRONT; break;
	case WW3D_CULL_CCW:  desc.CullMode = GPU_CULL_BACK;  break;
	default:             desc.CullMode = GPU_CULL_NONE;  break;
	}

	// WW3D_RS_DEPTHBIAS is a float in depth buffer range, D3D11 wants an integer multiple of
	// the smallest representable step: 2^-24 for D24S8, and for float depth from 0.5 up as well.
	const float bias = As_Float(rs[WW3D_RS_DEPTHBIAS]);
	desc.DepthBias            = static_cast<INT>(bias * 16777216.0f);
	desc.SlopeScaledDepthBias = As_Float(rs[WW3D_RS_SLOPESCALEDEPTHBIAS]);
	desc.DepthBiasClamp       = 0.0f;

	desc.DepthClipEnable     = TRUE;
	desc.ScissorEnable       = FALSE;
	desc.MultisampleEnable   = (multisampled_target && rs[WW3D_RS_MULTISAMPLEANTIALIAS]) ? TRUE : FALSE;
	desc.AntialiasedLineEnable = FALSE;
}

// ==================================================================================================
// Sampler
// ==================================================================================================

static GPU_TEXTURE_ADDRESS_MODE To_Address(unsigned mode)
{
	switch (mode) {
	case WW3D_TADDRESS_MIRROR:      return GPU_TEXTURE_ADDRESS_MIRROR;
	case WW3D_TADDRESS_CLAMP:       return GPU_TEXTURE_ADDRESS_CLAMP;
	case WW3D_TADDRESS_BORDER:      return GPU_TEXTURE_ADDRESS_BORDER;
	case WW3D_TADDRESS_MIRRORONCE:  return GPU_TEXTURE_ADDRESS_MIRROR_ONCE;
	default:                        return GPU_TEXTURE_ADDRESS_WRAP;
	}
}

void Render_Build_Sampler_Desc(const unsigned* ss, GPU_SAMPLER_DESC& desc)
{
	::ZeroMemory(&desc, sizeof(desc));

	const unsigned min_f = ss[WW3D_SAMP_MINFILTER] ? ss[WW3D_SAMP_MINFILTER] : WW3D_TEXF_POINT;
	const unsigned mag_f = ss[WW3D_SAMP_MAGFILTER] ? ss[WW3D_SAMP_MAGFILTER] : WW3D_TEXF_POINT;
	const unsigned mip_f = ss[WW3D_SAMP_MIPFILTER];		// WW3D_TEXF_NONE = no mip levels

	if (min_f == WW3D_TEXF_ANISOTROPIC || mag_f == WW3D_TEXF_ANISOTROPIC) {
		desc.Filter = GPU_FILTER_ANISOTROPIC;
	}
	else {
		// D3D11_ENCODE_BASIC_FILTER bit pattern by hand: bit 4 = min, bit 2 = mag, bit 0 = mip.
		unsigned bits = 0;
		if (min_f == WW3D_TEXF_LINEAR) bits |= 0x10;
		if (mag_f == WW3D_TEXF_LINEAR) bits |= 0x04;
		if (mip_f == WW3D_TEXF_LINEAR) bits |= 0x01;
		desc.Filter = static_cast<GPU_FILTER>(bits);
	}

	desc.AddressU = To_Address(ss[WW3D_SAMP_ADDRESSU]);
	desc.AddressV = To_Address(ss[WW3D_SAMP_ADDRESSV]);
	desc.AddressW = To_Address(ss[WW3D_SAMP_ADDRESSW]);

	desc.MipLODBias   = As_Float(ss[WW3D_SAMP_MIPMAPLODBIAS]);
	desc.MaxAnisotropy = ss[WW3D_SAMP_MAXANISOTROPY] ? ss[WW3D_SAMP_MAXANISOTROPY] : 1;
	if (desc.MaxAnisotropy > 16) desc.MaxAnisotropy = 16;
	desc.ComparisonFunc = GPU_COMPARISON_NEVER;

	Render_Color_To_Float4(ss[WW3D_SAMP_BORDERCOLOR], desc.BorderColor);

	// WW3D_SAMP_MAXMIPLEVEL is the finest usable level -- the lower LOD limit in D3D11.
	desc.MinLOD = static_cast<float>(ss[WW3D_SAMP_MAXMIPLEVEL]);
	// MIPFILTER_NONE means: base level only.
	desc.MaxLOD = (mip_f == WW3D_TEXF_NONE) ? desc.MinLOD : GPU_FLOAT32_MAX;
}

// ==================================================================================================
// Vertex declaration and primitives
// ==================================================================================================

GPU_FORMAT Render_Decl_Type_To_Gpu_Format(unsigned decl_type)
{
	switch (decl_type) {
	case WW3D_DECLTYPE_FLOAT1:    return GPU_FORMAT_R32_FLOAT;
	case WW3D_DECLTYPE_FLOAT2:    return GPU_FORMAT_R32G32_FLOAT;
	case WW3D_DECLTYPE_FLOAT3:    return GPU_FORMAT_R32G32B32_FLOAT;
	case WW3D_DECLTYPE_FLOAT4:    return GPU_FORMAT_R32G32B32A32_FLOAT;
	case WW3D_DECLTYPE_D3DCOLOR:  return GPU_FORMAT_R8G8B8A8_UNORM;	// shader swizzles .bgra
	case WW3D_DECLTYPE_UBYTE4:    return GPU_FORMAT_R8G8B8A8_UINT;
	case WW3D_DECLTYPE_SHORT2:    return GPU_FORMAT_R16G16_SINT;
	case WW3D_DECLTYPE_SHORT4:    return GPU_FORMAT_R16G16B16A16_SINT;
	case WW3D_DECLTYPE_UBYTE4N:   return GPU_FORMAT_R8G8B8A8_UNORM;
	default:                      return GPU_FORMAT_UNKNOWN;
	}
}

const char* Render_Decl_Usage_To_Semantic(unsigned decl_usage)
{
	switch (decl_usage) {
	case WW3D_DECLUSAGE_POSITION:      return "POSITION";
	case WW3D_DECLUSAGE_BLENDWEIGHT:   return "BLENDWEIGHT";
	case WW3D_DECLUSAGE_BLENDINDICES:  return "BLENDINDICES";
	case WW3D_DECLUSAGE_NORMAL:        return "NORMAL";
	case WW3D_DECLUSAGE_PSIZE:         return "PSIZE";
	case WW3D_DECLUSAGE_TEXCOORD:      return "TEXCOORD";
	case WW3D_DECLUSAGE_TANGENT:       return "TANGENT";
	case WW3D_DECLUSAGE_BINORMAL:      return "BINORMAL";
	case WW3D_DECLUSAGE_TESSFACTOR:    return "TESSFACTOR";
	case WW3D_DECLUSAGE_POSITIONT:     return "POSITIONT";
	case WW3D_DECLUSAGE_COLOR:         return "COLOR";
	default:                           return nullptr;
	}
}

GPU_PRIMITIVE_TOPOLOGY Render_Primitive_Topology(unsigned primitive_type)
{
	switch (primitive_type) {
	case WW3D_PT_POINTLIST:      return GPU_PRIMITIVE_TOPOLOGY_POINTLIST;
	case WW3D_PT_LINELIST:       return GPU_PRIMITIVE_TOPOLOGY_LINELIST;
	case WW3D_PT_LINESTRIP:      return GPU_PRIMITIVE_TOPOLOGY_LINESTRIP;
	case WW3D_PT_TRIANGLELIST:   return GPU_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	case WW3D_PT_TRIANGLESTRIP:  return GPU_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
	// D3D11 has no TRIANGLEFAN. The engine never produces one (only the index count in
	// RenderDevice::Draw_Sorting_IB_VB mentions it); the caller reports the case once.
	default:                     return GPU_PRIMITIVE_TOPOLOGY_UNDEFINED;
	}
}

unsigned Render_Vertex_Count_For_Primitives(unsigned primitive_type, unsigned prim_count)
{
	switch (primitive_type) {
	case WW3D_PT_POINTLIST:      return prim_count;
	case WW3D_PT_LINELIST:       return prim_count * 2;
	case WW3D_PT_LINESTRIP:      return prim_count ? prim_count + 1 : 0;
	case WW3D_PT_TRIANGLELIST:   return prim_count * 3;
	case WW3D_PT_TRIANGLESTRIP:  return prim_count ? prim_count + 2 : 0;
	case WW3D_PT_TRIANGLEFAN:    return prim_count ? prim_count + 2 : 0;
	default:                     return 0;
	}
}
