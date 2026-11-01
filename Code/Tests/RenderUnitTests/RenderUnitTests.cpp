/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RenderUnitTests.cpp ----------------------------------------------------------------------------
//
// Unit tests for the device-independent parts of the render backend. No Direct3D device, no
// window -- only the three translation units with pure conversions are linked:
//
//   renderformat.cpp  format mapping WW3DFormat/WW3D_FMT_* <-> DXGI and conversion of the
//                    formats that feature level 11.0 lacks
//   renderstates.cpp  state shadow -> D3D11 descriptions, declaration types, semantics,
//                    primitive topologies
//   renderfvf.cpp    FVF -> WW3DVertexElement[]
//   vulkanrequirements.cpp  what the Vulkan backend needs from a device and which device it takes
//   TerrainTileImage.cpp  terrain tile files (TGA, DDS) to BGRA; the DDS test files are encoded
//                    with DirectXTex
//
// Same structure as UnitTests: no framework, each test function returns true/false, main()
// collects the failures and returns them to ctest.
// ------------------------------------------------------------------------------------------------

#include "WW3D2/Common/renderformat.h"
#include "WW3D2/Common/renderstates.h"
#include "WW3D2/Common/renderfvf.h"
#include "presentlayout.h"
#include "WW3D2/Vulkan/vulkanrequirements.h"
#include "WW3D2/Vulkan/vulkanspirv.h"
#include "W3DDevice/GameClient/TerrainTileImage.h"

#include <DirectXTex.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int g_checks = 0;

#define CHECK(cond, ...) \
	do { \
		++g_checks; \
		if (!(cond)) { printf("    FAIL: "); printf(__VA_ARGS__); printf("\n"); return false; } \
	} while (0)

// ==================================================================================================
// Format mapping
// ==================================================================================================

static bool test_Format_Mapping(void)
{
	// The formats feature level 11.0 supports directly.
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_A8R8G8B8) == GPU_FORMAT_B8G8R8A8_UNORM, "A8R8G8B8");
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_X8R8G8B8) == GPU_FORMAT_B8G8R8X8_UNORM, "X8R8G8B8");
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_R5G6B5)   == GPU_FORMAT_B5G6R5_UNORM,   "R5G6B5");
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_A1R5G5B5) == GPU_FORMAT_B5G5R5A1_UNORM, "A1R5G5B5");
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_A8)       == GPU_FORMAT_A8_UNORM,       "A8");
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_U8V8)     == GPU_FORMAT_R8G8_SNORM,     "U8V8");
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_DXT1)     == GPU_FORMAT_BC1_UNORM,      "DXT1");
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_DXT3)     == GPU_FORMAT_BC2_UNORM,      "DXT3");
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_DXT5)     == GPU_FORMAT_BC3_UNORM,      "DXT5");
	// DXT2/DXT4 are DXT3/DXT5 with premultiplied alpha; DXGI does not distinguish them.
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_DXT2)     == GPU_FORMAT_BC2_UNORM,      "DXT2 as DXT3");
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_DXT4)     == GPU_FORMAT_BC3_UNORM,      "DXT4 as DXT5");
	// BC7 as UNORM like the DXT formats (no sRGB, otherwise brightness changes)
	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_BC7)      == GPU_FORMAT_BC7_UNORM,      "BC7");

	// Formats without an equivalent report B8G8R8A8 AND require conversion -- both must agree,
	// otherwise the load path loads wrongly.
	const WW3DFormat converted[] = { WW3D_FORMAT_A4R4G4B4, WW3D_FORMAT_X1R5G5B5,
									 WW3D_FORMAT_L8, WW3D_FORMAT_A8L8, WW3D_FORMAT_R8G8B8 };
	for (unsigned i = 0; i < sizeof(converted) / sizeof(converted[0]); ++i) {
		CHECK(WW3DFormat_To_Gpu_Format(converted[i]) == GPU_FORMAT_B8G8R8A8_UNORM,
			  "converted format %u does not report B8G8R8A8", (unsigned)converted[i]);
		CHECK(Render_Format_Needs_Conversion(converted[i]),
			  "format %u should be reported as converted", (unsigned)converted[i]);
	}

	// Conversely: native formats must not be flagged for conversion.
	const WW3DFormat native[] = { WW3D_FORMAT_A8R8G8B8, WW3D_FORMAT_X8R8G8B8, WW3D_FORMAT_R5G6B5,
								  WW3D_FORMAT_A1R5G5B5, WW3D_FORMAT_A8, WW3D_FORMAT_U8V8,
								  WW3D_FORMAT_DXT1, WW3D_FORMAT_DXT3, WW3D_FORMAT_DXT5, WW3D_FORMAT_BC7 };
	for (unsigned i = 0; i < sizeof(native) / sizeof(native[0]); ++i) {
		CHECK(!Render_Format_Needs_Conversion(native[i]),
			  "format %u wrongly reported as converted", (unsigned)native[i]);
	}

	CHECK(WW3DFormat_To_Gpu_Format(WW3D_FORMAT_UNKNOWN) == GPU_FORMAT_UNKNOWN, "UNKNOWN");
	return true;
}

static bool test_Device_Format_Mapping(void)
{
	CHECK(Device_Format_To_Gpu_Format(WW3D_FMT_A8R8G8B8) == GPU_FORMAT_B8G8R8A8_UNORM, "FMT_A8R8G8B8");
	CHECK(Device_Format_To_Gpu_Format(WW3D_FMT_D24S8)    == GPU_FORMAT_D24_UNORM_S8_UINT, "FMT_D24S8");
	CHECK(Device_Format_To_Gpu_Format(WW3D_FMT_D16)      == GPU_FORMAT_D16_UNORM, "FMT_D16");

	// The reverse mapping must match the forward one, otherwise Get_Surface_Desc reports a wrong
	// format and the caller creates a texture with the wrong layout.
	const unsigned roundtrip[] = { WW3D_FMT_A8R8G8B8, WW3D_FMT_X8R8G8B8, WW3D_FMT_R5G6B5,
								   WW3D_FMT_A1R5G5B5, WW3D_FMT_A8, WW3D_FMT_V8U8,
								   WW3D_FMT_DXT1, WW3D_FMT_BC7, WW3D_FMT_D24S8, WW3D_FMT_D32, WW3D_FMT_D16 };
	for (unsigned i = 0; i < sizeof(roundtrip) / sizeof(roundtrip[0]); ++i) {
		const GPU_FORMAT dxgi = Device_Format_To_Gpu_Format(roundtrip[i]);
		CHECK(Gpu_Format_To_Device_Format(dxgi) == roundtrip[i],
			  "round trip of WW3D_FMT %u gave %u", roundtrip[i], Gpu_Format_To_Device_Format(dxgi));
	}
	return true;
}

// ==================================================================================================
// Format conversion -- the bit math that text rendering depends on
// ==================================================================================================

static bool test_Convert_A4R4G4B4(void)
{
	// Destination order is B,G,R,A (GPU_FORMAT_B8G8R8A8_UNORM).
	const unsigned short src[4] = { 0xFFFF, 0xF000, 0x0F00, 0x000F };
	unsigned char dst[16];
	CHECK(Render_Convert_Row_To_BGRA(WW3D_FORMAT_A4R4G4B4, (const unsigned char*)src, dst, 4), "conversion rejected");

	const unsigned char expect[16] = {
		255, 255, 255, 255,		// 0xFFFF: all full
		  0,   0,   0, 255,		// 0xF000: alpha only
		  0,   0, 255,   0,		// 0x0F00: red only
		255,   0,   0,   0 };	// 0x000F: blue only
	for (unsigned i = 0; i < 16; ++i) {
		CHECK(dst[i] == expect[i], "byte %u is %u, expected %u", i, dst[i], expect[i]);
	}
	return true;
}

static bool test_Convert_X1R5G5B5(void)
{
	const unsigned short src[2] = { 0x7FFF, 0x7C00 };
	unsigned char dst[8];
	CHECK(Render_Convert_Row_To_BGRA(WW3D_FORMAT_X1R5G5B5, (const unsigned char*)src, dst, 2), "conversion rejected");
	// 5 bits -> 8 bits via (x << 3) | (x >> 2): 31 -> 255, 0 -> 0. Alpha is always full.
	const unsigned char expect[8] = { 255, 255, 255, 255,   0, 0, 255, 255 };
	for (unsigned i = 0; i < 8; ++i) {
		CHECK(dst[i] == expect[i], "byte %u is %u, expected %u", i, dst[i], expect[i]);
	}
	return true;
}

static bool test_Convert_L8_A8L8(void)
{
	const unsigned char l8[2] = { 0x00, 0x80 };
	unsigned char dst[8];
	CHECK(Render_Convert_Row_To_BGRA(WW3D_FORMAT_L8, l8, dst, 2), "L8 rejected");
	CHECK(dst[0] == 0 && dst[1] == 0 && dst[2] == 0 && dst[3] == 255, "L8 Pixel 0");
	CHECK(dst[4] == 0x80 && dst[5] == 0x80 && dst[6] == 0x80 && dst[7] == 255, "L8 Pixel 1");

	// A8L8: luminance in the low byte, alpha in the high byte.
	const unsigned char a8l8[4] = { 0x40, 0x80,  0xFF, 0x00 };
	CHECK(Render_Convert_Row_To_BGRA(WW3D_FORMAT_A8L8, a8l8, dst, 2), "A8L8 rejected");
	CHECK(dst[0] == 0x40 && dst[1] == 0x40 && dst[2] == 0x40 && dst[3] == 0x80, "A8L8 Pixel 0");
	CHECK(dst[4] == 0xFF && dst[5] == 0xFF && dst[6] == 0xFF && dst[7] == 0x00, "A8L8 Pixel 1");
	return true;
}

static bool test_Convert_Rejects_Unsupported(void)
{
	unsigned char dst[8];
	const unsigned char src[8] = { 0 };
	// Native formats must not be converted here -- otherwise the load path would mangle data
	// that is already correct.
	CHECK(!Render_Convert_Row_To_BGRA(WW3D_FORMAT_A8R8G8B8, src, dst, 2), "A8R8G8B8 was converted");
	CHECK(!Render_Convert_Row_To_BGRA(WW3D_FORMAT_DXT1, src, dst, 2), "DXT1 was converted");
	return true;
}

static bool test_Convert_Image_Uses_Pitch(void)
{
	// Two L8 rows with padding (pitch 4 instead of 2): the padding must not be copied.
	const unsigned char src[8] = { 0x10, 0x20, 0xEE, 0xEE,
								   0x30, 0x40, 0xEE, 0xEE };
	unsigned char dst[16];
	::memset(dst, 0xCD, sizeof(dst));
	CHECK(Render_Convert_Image_To_BGRA(WW3D_FORMAT_L8, src, 4, dst, 8, 2, 2), "conversion rejected");
	CHECK(dst[0] == 0x10 && dst[4] == 0x20, "row 0 wrong");
	CHECK(dst[8] == 0x30 && dst[12] == 0x40, "row 1 wrong (pitch ignored)");
	return true;
}

// ==================================================================================================
// State mapping
// ==================================================================================================

static bool test_Cull_And_Fill(void)
{
	unsigned rs[RENDER_STATE_COUNT];
	::memset(rs, 0, sizeof(rs));
	GPU_RASTERIZER_DESC desc;

	// The WW3D cull mode (D3D9 semantics) names the side to discard. With
	// FrontCounterClockwise = FALSE "front" is clockwise, so WW3D_CULL_CW = CULL_FRONT. Swapping
	// this turns every model inside out -- the test pins the mapping.
	rs[WW3D_RS_CULLMODE] = WW3D_CULL_CW;
	rs[WW3D_RS_FILLMODE] = WW3D_FILL_SOLID;
	Render_Build_Rasterizer_Desc(rs, desc, false);
	CHECK(desc.CullMode == GPU_CULL_FRONT, "CULL_CW must be CULL_FRONT");
	CHECK(desc.FrontCounterClockwise == FALSE, "FrontCounterClockwise must stay FALSE");
	CHECK(desc.FillMode == GPU_FILL_SOLID, "FILL_SOLID");

	rs[WW3D_RS_CULLMODE] = WW3D_CULL_CCW;
	Render_Build_Rasterizer_Desc(rs, desc, false);
	CHECK(desc.CullMode == GPU_CULL_BACK, "CULL_CCW must be CULL_BACK");

	rs[WW3D_RS_CULLMODE] = WW3D_CULL_NONE;
	rs[WW3D_RS_FILLMODE] = WW3D_FILL_WIREFRAME;
	Render_Build_Rasterizer_Desc(rs, desc, false);
	CHECK(desc.CullMode == GPU_CULL_NONE, "CULL_NONE");
	CHECK(desc.FillMode == GPU_FILL_WIREFRAME, "FILL_WIREFRAME");

	// Multisampling only if the target supports it AND the state requests it.
	rs[WW3D_RS_MULTISAMPLEANTIALIAS] = 1;
	Render_Build_Rasterizer_Desc(rs, desc, false);
	CHECK(desc.MultisampleEnable == FALSE, "must stay off without a multisampled target");
	Render_Build_Rasterizer_Desc(rs, desc, true);
	CHECK(desc.MultisampleEnable == TRUE, "must be on with a multisampled target");
	return true;
}

static bool test_Blend_Alpha_Channel(void)
{
	unsigned rs[RENDER_STATE_COUNT];
	::memset(rs, 0, sizeof(rs));
	GPU_BLEND_DESC desc;

	// D3D11 allows only alpha factors for the alpha channel; the WW3D states (D3D9 semantics)
	// use color factors there, meaning their alpha part. Without this mapping CreateBlendState
	// rejects the state and nothing blends at all.
	rs[WW3D_RS_ALPHABLENDENABLE] = 1;
	rs[WW3D_RS_SRCBLEND]         = WW3D_BLEND_SRCCOLOR;
	rs[WW3D_RS_DESTBLEND]        = WW3D_BLEND_INVDESTCOLOR;
	rs[WW3D_RS_BLENDOP]          = WW3D_BLENDOP_ADD;
	rs[WW3D_RS_COLORWRITEENABLE] = 0x0F;
	Render_Build_Blend_Desc(rs, desc);

	CHECK(desc.RenderTarget[0].BlendEnable == TRUE, "BlendEnable");
	CHECK(desc.RenderTarget[0].SrcBlend  == GPU_BLEND_SRC_COLOR, "SrcBlend");
	CHECK(desc.RenderTarget[0].DestBlend == GPU_BLEND_INV_DEST_COLOR, "DestBlend");
	CHECK(desc.RenderTarget[0].SrcBlendAlpha  == GPU_BLEND_SRC_ALPHA, "SrcBlendAlpha must be alpha");
	CHECK(desc.RenderTarget[0].DestBlendAlpha == GPU_BLEND_INV_DEST_ALPHA, "DestBlendAlpha must be alpha");
	CHECK(desc.RenderTarget[0].RenderTargetWriteMask == 0x0F, "write mask");
	return true;
}

static bool test_Stencil_Masks_Are_Eight_Bit(void)
{
	unsigned rs[RENDER_STATE_COUNT];
	::memset(rs, 0, sizeof(rs));
	GPU_DEPTH_STENCIL_DESC desc;

	// The shadow paths pass 32-bit masks (0x80808080) and mean the low 8 bits -- the scene
	// target's stencil is 8 bits wide.
	rs[WW3D_RS_STENCILENABLE]    = 1;
	rs[WW3D_RS_STENCILMASK]      = 0x80808080;
	rs[WW3D_RS_STENCILWRITEMASK] = 0xFFFFFFFF;
	rs[WW3D_RS_STENCILFUNC]      = WW3D_CMP_GREATEREQUAL;
	rs[WW3D_RS_STENCILPASS]      = WW3D_STENCILOP_INCR;
	rs[WW3D_RS_STENCILFAIL]      = WW3D_STENCILOP_KEEP;
	rs[WW3D_RS_STENCILZFAIL]     = WW3D_STENCILOP_KEEP;
	Render_Build_Depth_Stencil_Desc(rs, desc);

	CHECK(desc.StencilReadMask  == 0x80, "read mask truncated to 8 bits");
	CHECK(desc.StencilWriteMask == 0xFF, "write mask truncated to 8 bits");
	CHECK(desc.FrontFace.StencilFunc == GPU_COMPARISON_GREATER_EQUAL, "comparison function");
	CHECK(desc.FrontFace.StencilPassOp == GPU_STENCIL_OP_INCR, "pass operation");
	// No two-sided stencil: both faces get the same ops (the shadow paths draw two passes with
	// different CULLMODE).
	CHECK(desc.BackFace.StencilFunc == desc.FrontFace.StencilFunc, "back face equals front face");
	CHECK(desc.BackFace.StencilPassOp == desc.FrontFace.StencilPassOp, "back face equals front face");
	return true;
}

static bool test_Sampler_Filter_And_LOD(void)
{
	unsigned ss[RENDER_SAMPLER_STATE_COUNT];
	::memset(ss, 0, sizeof(ss));
	GPU_SAMPLER_DESC desc;

	ss[WW3D_SAMP_MINFILTER] = WW3D_TEXF_LINEAR;
	ss[WW3D_SAMP_MAGFILTER] = WW3D_TEXF_LINEAR;
	ss[WW3D_SAMP_MIPFILTER] = WW3D_TEXF_LINEAR;
	ss[WW3D_SAMP_ADDRESSU]  = WW3D_TADDRESS_CLAMP;
	ss[WW3D_SAMP_ADDRESSV]  = WW3D_TADDRESS_WRAP;
	Render_Build_Sampler_Desc(ss, desc);
	CHECK(desc.Filter == GPU_FILTER_MIN_MAG_MIP_LINEAR, "trilinear");
	CHECK(desc.AddressU == GPU_TEXTURE_ADDRESS_CLAMP, "AddressU");
	CHECK(desc.AddressV == GPU_TEXTURE_ADDRESS_WRAP, "AddressV");

	ss[WW3D_SAMP_MIPFILTER] = WW3D_TEXF_POINT;
	Render_Build_Sampler_Desc(ss, desc);
	CHECK(desc.Filter == GPU_FILTER_MIN_MAG_LINEAR_MIP_POINT, "linear with point mip");

	ss[WW3D_SAMP_MINFILTER] = WW3D_TEXF_POINT;
	ss[WW3D_SAMP_MAGFILTER] = WW3D_TEXF_POINT;
	Render_Build_Sampler_Desc(ss, desc);
	CHECK(desc.Filter == GPU_FILTER_MIN_MAG_MIP_POINT, "point");

	ss[WW3D_SAMP_MINFILTER]     = WW3D_TEXF_ANISOTROPIC;
	ss[WW3D_SAMP_MAXANISOTROPY] = 16;
	Render_Build_Sampler_Desc(ss, desc);
	CHECK(desc.Filter == GPU_FILTER_ANISOTROPIC, "anisotropic");
	CHECK(desc.MaxAnisotropy == 16, "anisotropy level");

	// MIPFILTER_NONE means base level only. In D3D11 this becomes an upper LOD bound --
	// otherwise mip levels would be used that should not exist.
	::memset(ss, 0, sizeof(ss));
	ss[WW3D_SAMP_MINFILTER]   = WW3D_TEXF_LINEAR;
	ss[WW3D_SAMP_MAGFILTER]   = WW3D_TEXF_LINEAR;
	ss[WW3D_SAMP_MIPFILTER]   = WW3D_TEXF_NONE;
	ss[WW3D_SAMP_MAXMIPLEVEL] = 0;
	Render_Build_Sampler_Desc(ss, desc);
	CHECK(desc.MinLOD == 0.0f && desc.MaxLOD == 0.0f, "MIPFILTER_NONE must clamp to the base level");
	return true;
}

static bool test_Primitive_Topology_And_Counts(void)
{
	CHECK(Render_Primitive_Topology(WW3D_PT_TRIANGLELIST)  == GPU_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "TRIANGLELIST");
	CHECK(Render_Primitive_Topology(WW3D_PT_TRIANGLESTRIP) == GPU_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, "TRIANGLESTRIP");
	CHECK(Render_Primitive_Topology(WW3D_PT_LINELIST)      == GPU_PRIMITIVE_TOPOLOGY_LINELIST, "LINELIST");
	// TRIANGLEFAN has no D3D11 counterpart and must map to UNDEFINED so the draw path detects
	// it instead of drawing something wrong.
	CHECK(Render_Primitive_Topology(WW3D_PT_TRIANGLEFAN)   == GPU_PRIMITIVE_TOPOLOGY_UNDEFINED, "TRIANGLEFAN");

	CHECK(Render_Vertex_Count_For_Primitives(WW3D_PT_TRIANGLELIST, 4)  == 12, "4 triangles = 12 indices");
	CHECK(Render_Vertex_Count_For_Primitives(WW3D_PT_TRIANGLESTRIP, 4) == 6,  "4 strip triangles = 6 indices");
	CHECK(Render_Vertex_Count_For_Primitives(WW3D_PT_LINELIST, 3)      == 6,  "3 lines = 6 indices");
	CHECK(Render_Vertex_Count_For_Primitives(WW3D_PT_LINESTRIP, 3)     == 4,  "3 strip lines = 4 indices");
	CHECK(Render_Vertex_Count_For_Primitives(WW3D_PT_POINTLIST, 7)     == 7,  "7 points");
	// Zero primitives must never yield a positive count, or the caller draws garbage.
	CHECK(Render_Vertex_Count_For_Primitives(WW3D_PT_TRIANGLESTRIP, 0) == 0, "0 strip triangles");
	CHECK(Render_Vertex_Count_For_Primitives(WW3D_PT_LINESTRIP, 0)     == 0, "0 strip lines");
	return true;
}

static bool test_Decl_Types_And_Semantics(void)
{
	CHECK(Render_Decl_Type_To_Gpu_Format(WW3D_DECLTYPE_FLOAT3) == GPU_FORMAT_R32G32B32_FLOAT, "FLOAT3");
	CHECK(Render_Decl_Type_To_Gpu_Format(WW3D_DECLTYPE_FLOAT2) == GPU_FORMAT_R32G32_FLOAT, "FLOAT2");
	// D3DCOLOR is BGRA in memory. It is declared as R8G8B8A8_UNORM and the shader swizzles
	// .bgra -- an easy combination to get wrong.
	CHECK(Render_Decl_Type_To_Gpu_Format(WW3D_DECLTYPE_D3DCOLOR) == GPU_FORMAT_R8G8B8A8_UNORM, "D3DCOLOR");
	CHECK(Render_Decl_Type_To_Gpu_Format(WW3D_DECLTYPE_UNUSED) == GPU_FORMAT_UNKNOWN, "UNUSED");

	CHECK(::strcmp(Render_Decl_Usage_To_Semantic(WW3D_DECLUSAGE_POSITION), "POSITION") == 0, "POSITION");
	CHECK(::strcmp(Render_Decl_Usage_To_Semantic(WW3D_DECLUSAGE_NORMAL), "NORMAL") == 0, "NORMAL");
	CHECK(::strcmp(Render_Decl_Usage_To_Semantic(WW3D_DECLUSAGE_COLOR), "COLOR") == 0, "COLOR");
	CHECK(::strcmp(Render_Decl_Usage_To_Semantic(WW3D_DECLUSAGE_TEXCOORD), "TEXCOORD") == 0, "TEXCOORD");
	CHECK(Render_Decl_Usage_To_Semantic(99) == nullptr, "unknown usage");
	return true;
}

static bool test_Color_To_Float4(void)
{
	float c[4];
	Render_Color_To_Float4(0xFF804020, c);	// AARRGGBB
	CHECK(c[0] > 0.501f && c[0] < 0.503f, "red (0x80) is %f", c[0]);
	CHECK(c[1] > 0.250f && c[1] < 0.252f, "green (0x40) is %f", c[1]);
	CHECK(c[2] > 0.125f && c[2] < 0.127f, "blue (0x20) is %f", c[2]);
	CHECK(c[3] == 1.0f, "alpha (0xFF) is %f", c[3]);
	return true;
}

// ==================================================================================================
// FVF -> vertex declaration
// ==================================================================================================

static bool test_FVF_To_Vertex_Elements(void)
{
	WW3DVertexElement e[WW3D_MAX_DECL_LENGTH + 1];

	// XYZ|NORMAL|DIFFUSE|TEX1 -- the engine's most common format.
	const unsigned n = FVF_To_Vertex_Elements(
		WW3D_FVF_XYZ | WW3D_FVF_NORMAL | WW3D_FVF_DIFFUSE | WW3D_FVF_TEX1,
		e, WW3D_MAX_DECL_LENGTH + 1);
	CHECK(n == 4, "expected four elements, got %u", n);

	CHECK(e[0].Usage == WW3D_DECLUSAGE_POSITION && e[0].Offset == 0 && e[0].Type == WW3D_DECLTYPE_FLOAT3, "Position");
	CHECK(e[1].Usage == WW3D_DECLUSAGE_NORMAL   && e[1].Offset == 12 && e[1].Type == WW3D_DECLTYPE_FLOAT3, "normal at 12");
	CHECK(e[2].Usage == WW3D_DECLUSAGE_COLOR    && e[2].Offset == 24 && e[2].Type == WW3D_DECLTYPE_D3DCOLOR, "diffuse at 24");
	CHECK(e[2].UsageIndex == 0, "diffuse is COLOR0");
	CHECK(e[3].Usage == WW3D_DECLUSAGE_TEXCOORD && e[3].Offset == 28 && e[3].Type == WW3D_DECLTYPE_FLOAT2, "texcoord at 28");
	// The terminator must be set, or the caller runs past the end.
	CHECK(e[n].Type == WW3D_DECLTYPE_UNUSED, "Abschlusselement");

	// The offsets must match the vertex size the rest of the engine uses
	// (FVFInfoClass::Get_FVF_Size) -- otherwise the input layout reads at wrong positions.
	const FVFInfoClass info(WW3D_FVF_XYZ | WW3D_FVF_NORMAL | WW3D_FVF_DIFFUSE | WW3D_FVF_TEX1);
	CHECK(info.Get_FVF_Size() == 36, "vertex size is %u, expected 36", info.Get_FVF_Size());
	CHECK(unsigned(e[3].Offset) + 8 == info.Get_FVF_Size(), "last element does not end at the vertex end");

	// Specular is COLOR1, not COLOR0.
	const unsigned n2 = FVF_To_Vertex_Elements(
		WW3D_FVF_XYZ | WW3D_FVF_DIFFUSE | WW3D_FVF_SPECULAR, e, WW3D_MAX_DECL_LENGTH + 1);
	CHECK(n2 == 3, "expected three elements, got %u", n2);
	CHECK(e[1].Usage == WW3D_DECLUSAGE_COLOR && e[1].UsageIndex == 0, "Diffuse COLOR0");
	CHECK(e[2].Usage == WW3D_DECLUSAGE_COLOR && e[2].UsageIndex == 1, "Specular COLOR1");

	// Multiple texture coordinate sets are indexed consecutively.
	const unsigned n3 = FVF_To_Vertex_Elements(WW3D_FVF_XYZ | WW3D_FVF_TEX2, e, WW3D_MAX_DECL_LENGTH + 1);
	CHECK(n3 == 3, "expected three elements, got %u", n3);
	CHECK(e[1].Usage == WW3D_DECLUSAGE_TEXCOORD && e[1].UsageIndex == 0, "TEXCOORD0");
	CHECK(e[2].Usage == WW3D_DECLUSAGE_TEXCOORD && e[2].UsageIndex == 1, "TEXCOORD1");
	CHECK(e[2].Offset == 20, "TEXCOORD1 at 20, is %u", e[2].Offset);
	return true;
}

// ==================================================================================================
// Present layout (scene in a window of another size)
// ==================================================================================================

static bool test_Present_Layout(void)
{
	// Same size (windowed mode): buffer is the scene, points stay unchanged.
	PresentLayout l = Compute_Present_Layout(800, 600, 800, 600);
	CHECK(l.bufferWidth == 800 && l.bufferHeight == 600 && l.offsetX == 0 && l.offsetY == 0, "same size");
	int x = 799, y = 0;
	Present_Window_To_Render(l, 800, 600, x, y);
	CHECK(x == 799 && y == 0, "window equals image: point unchanged (%d,%d)", x, y);

	// Same aspect ratio, larger window: no bars, uniform scale.
	l = Compute_Present_Layout(1280, 720, 2560, 1440);
	CHECK(l.bufferWidth == 1280 && l.bufferHeight == 720 && l.offsetX == 0, "16:9 in 16:9");
	x = 2559; y = 1439;
	Present_Window_To_Render(l, 2560, 1440, x, y);
	CHECK(x == 1279 && y == 719, "bottom right corner (%d,%d)", x, y);
	x = 1280; y = 720;
	Present_Window_To_Render(l, 2560, 1440, x, y);
	CHECK(x == 640 && y == 360, "centre (%d,%d)", x, y);

	// 800x600 on a 21:9 monitor: bars left and right.
	l = Compute_Present_Layout(800, 600, 3440, 1440);
	CHECK(l.bufferHeight == 600 && l.bufferWidth == 1433 && l.offsetX == 316 && l.offsetY == 0,
		  "4:3 in 21:9: buffer %dx%d at %d", l.bufferWidth, l.bufferHeight, l.offsetX);
	x = 1720; y = 720;
	Present_Window_To_Render(l, 3440, 1440, x, y);
	CHECK(x >= 399 && x <= 400 && y == 300, "centre 4:3 in 21:9 (%d,%d)", x, y);
	x = 0; y = 0;
	Present_Window_To_Render(l, 3440, 1440, x, y);
	CHECK(x == 0 && y == 0, "left edge clamped (%d,%d)", x, y);
	x = 3439;
	Present_Window_To_Render(l, 3440, 1440, x, y);
	CHECK(x == 799, "right edge clamped (%d)", x);

	// 21:9 on a 16:9 monitor: bars top and bottom.
	l = Compute_Present_Layout(2560, 1080, 1920, 1080);
	CHECK(l.bufferWidth == 2560 && l.bufferHeight == 1440 && l.offsetY == 180 && l.offsetX == 0,
		  "21:9 in 16:9: buffer %dx%d at %d", l.bufferWidth, l.bufferHeight, l.offsetY);
	x = 960; y = 540;
	Present_Window_To_Render(l, 1920, 1080, x, y);
	CHECK(x == 1280 && y == 540, "centre 21:9 in 16:9 (%d,%d)", x, y);

	// Nearly equal aspect ratio (2560x1080 on 3440x1440): thin bars.
	l = Compute_Present_Layout(2560, 1080, 3440, 1440);
	CHECK(l.bufferWidth == 2580 && l.offsetX == 10, "2560x1080 on 3440x1440: %d at %d", l.bufferWidth, l.offsetX);

	// Invalid input leaves everything unchanged.
	l = Compute_Present_Layout(1024, 768, 0, 0);
	CHECK(l.bufferWidth == 1024 && l.bufferHeight == 768 && l.offsetX == 0 && l.offsetY == 0, "window 0x0");
	return true;
}

// ==================================================================================================
// Terrain tile files (TerrainTileImage): TGA and DDS of the same picture must decode alike
// ==================================================================================================

// Test picture, row 0 = top. Smooth gradients (BC1 friendly) with a dark top band and a bright
// bottom band, so a flipped picture fails clearly.
static void tilePicture(int width, int height, std::vector<unsigned char> &rgba, bool withAlpha)
{
	rgba.resize(static_cast<size_t>(width) * height * 4);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			unsigned char *p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
			p[0] = static_cast<unsigned char>(40 + (x * 160) / width);			// R
			p[1] = static_cast<unsigned char>(y < height / 8 ? 20 : (y >= height - height / 8 ? 230 : 120));	// G
			p[2] = static_cast<unsigned char>(60 + (y * 120) / height);			// B
			p[3] = withAlpha ? static_cast<unsigned char>((x * 255) / (width - 1)) : 255;
		}
	}
}

// TGA as EA and boden_frei.py write it: lower left origin, rows bottom-up; 24 bit (BGR) or 32 bit
// (BGRA), raw (type 2) or RLE (type 10, raw packets only), optional ID field.
static std::vector<unsigned char> tgaFile(int width, int height, const std::vector<unsigned char> &rgba, int bits,
	bool rle, const char *id)
{
	std::vector<unsigned char> f(18, 0);
	const size_t idLength = id ? strlen(id) : 0;
	f[0] = static_cast<unsigned char>(idLength);
	f[2] = rle ? 10 : 2;
	f[12] = static_cast<unsigned char>(width & 0xff);
	f[13] = static_cast<unsigned char>(width >> 8);
	f[14] = static_cast<unsigned char>(height & 0xff);
	f[15] = static_cast<unsigned char>(height >> 8);
	f[16] = static_cast<unsigned char>(bits);
	f[17] = bits == 32 ? 8 : 0;
	for (size_t i = 0; i < idLength; ++i)
		f.push_back(static_cast<unsigned char>(id[i]));
	for (int y = height - 1; y >= 0; --y) {
		for (int x = 0; x < width; ++x) {
			if (rle && x % 128 == 0) {
				const int run = width - x < 128 ? width - x : 128;
				f.push_back(static_cast<unsigned char>(run - 1));	// raw packet
			}
			const unsigned char *p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
			f.push_back(p[2]);
			f.push_back(p[1]);
			f.push_back(p[0]);
			if (bits == 32)
				f.push_back(p[3]);
		}
	}
	return f;
}

// DDS of the picture (top-down as DDS stores it) in the given format, encoded with DirectXTex.
static bool ddsFile(int width, int height, const std::vector<unsigned char> &rgba, DXGI_FORMAT format,
	std::vector<unsigned char> &out)
{
	// BGRA is swizzled here; DirectX::Convert would need COM for WIC.
	const bool bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM;
	DirectX::ScratchImage source;
	if (FAILED(source.Initialize2D(bgra ? format : DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1)))
		return false;
	const DirectX::Image *img = source.GetImage(0, 0, 0);
	for (int y = 0; y < height; ++y) {
		unsigned char *row = img->pixels + y * img->rowPitch;
		memcpy(row, &rgba[static_cast<size_t>(y) * width * 4], static_cast<size_t>(width) * 4);
		for (int x = 0; bgra && x < width; ++x) {
			const unsigned char r = row[x * 4];
			row[x * 4] = row[x * 4 + 2];
			row[x * 4 + 2] = r;
		}
	}
	DirectX::ScratchImage converted;
	const DirectX::ScratchImage *save = &source;
	if (DirectX::IsCompressed(format)) {
		if (FAILED(DirectX::Compress(*img, format, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted)))
			return false;
		save = &converted;
	} else if (!bgra && format != DXGI_FORMAT_R8G8B8A8_UNORM) {
		return false;
	}
	DirectX::Blob blob;
	if (FAILED(DirectX::SaveToDDSMemory(save->GetImages(), save->GetImageCount(), save->GetMetadata(), DirectX::DDS_FLAGS_NONE, blob)))
		return false;
	const unsigned char *b = static_cast<const unsigned char *>(blob.GetBufferPointer());
	out.assign(b, b + blob.GetBufferSize());
	return true;
}

// Largest and mean difference per channel between two decoded pictures of equal size.
static void tileDifference(const TerrainTileImage::Image &a, const TerrainTileImage::Image &b, int &maxDiff, double &meanDiff)
{
	maxDiff = 0;
	double sum = 0.0;
	for (size_t i = 0; i < a.bgra.size(); ++i) {
		const int d = abs(static_cast<int>(a.bgra[i]) - static_cast<int>(b.bgra[i]));
		sum += d;
		if (d > maxDiff)
			maxDiff = d;
	}
	meanDiff = a.bgra.empty() ? 0.0 : sum / static_cast<double>(a.bgra.size());
}

static bool test_TerrainTile_Dds_Extent(void)
{
	CHECK(TerrainTileImage::ddsTileExtent(256, 4) == 64, "4 x 4 tiles of 64");
	CHECK(TerrainTileImage::ddsTileExtent(256, 2) == 128, "2 x 2 tiles of 128");
	CHECK(TerrainTileImage::ddsTileExtent(256, 1) == 256, "1 tile of 256");
	CHECK(TerrainTileImage::ddsTileExtent(2560, 10) == 256, "10 x 10 tiles of 256");
	CHECK(TerrainTileImage::ddsTileExtent(512, 1) == 64, "512 per tile is no tile size");
	CHECK(TerrainTileImage::ddsTileExtent(256, 3) == 64, "not divisible");
	CHECK(TerrainTileImage::ddsTileExtent(96, 1) == 64, "96 is no tile size");
	CHECK(TerrainTileImage::ddsTileExtent(256, 0) == 64, "unknown tiles per row");
	return true;
}

static bool test_TerrainTile_Tga_Variants(void)
{
	const int w = 128, h = 64;
	std::vector<unsigned char> rgba;
	tilePicture(w, h, rgba, true);
	TerrainTileImage::Image raw, rle, rgb;
	const char *error = nullptr;
	std::vector<unsigned char> f = tgaFile(w, h, rgba, 32, false, nullptr);
	CHECK(TerrainTileImage::decode(f.data(), f.size(), 0, raw, &error), "raw 32 bit TGA: %s", error);
	CHECK(raw.info.format == TerrainTileImage::Format::TGA && raw.info.width == w && raw.info.height == h, "TGA size");
	CHECK(raw.info.tileExtent == 64, "TGA without ID: tile 64, got %d", raw.info.tileExtent);
	// First decoded row is the bottom row of the picture (BGRA).
	const unsigned char *bottom = &rgba[static_cast<size_t>(h - 1) * w * 4];
	CHECK(raw.bgra[0] == bottom[2] && raw.bgra[1] == bottom[1] && raw.bgra[2] == bottom[0] && raw.bgra[3] == bottom[3],
		"bottom row first, BGRA");
	f = tgaFile(w, h, rgba, 32, true, "ZHTS2");
	CHECK(TerrainTileImage::decode(f.data(), f.size(), 0, rle, &error), "RLE TGA: %s", error);
	CHECK(rle.info.tileExtent == 128, "ZHTS2: tile 128, got %d", rle.info.tileExtent);
	CHECK(rle.bgra == raw.bgra, "RLE and raw TGA differ");
	f = tgaFile(w, h, rgba, 24, false, "ZHTS4");
	CHECK(TerrainTileImage::decode(f.data(), f.size(), 0, rgb, &error), "24 bit TGA: %s", error);
	CHECK(rgb.info.tileExtent == 256, "ZHTS4: tile 256");
	for (size_t i = 0; i < rgb.bgra.size(); i += 4) {
		CHECK(rgb.bgra[i] == raw.bgra[i] && rgb.bgra[i + 1] == raw.bgra[i + 1] && rgb.bgra[i + 2] == raw.bgra[i + 2]
			&& rgb.bgra[i + 3] == 255, "24 bit TGA pixel %d", static_cast<int>(i / 4));
	}
	// Colour-mapped and 16-bit files are refused.
	f = tgaFile(w, h, rgba, 24, false, nullptr);
	f[1] = 1;
	CHECK(!TerrainTileImage::decode(f.data(), f.size(), 0, rgb, &error) && rgb.bgra.empty(), "colour map accepted");
	f[1] = 0;
	f[16] = 16;
	CHECK(!TerrainTileImage::decode(f.data(), f.size(), 0, rgb, &error), "16 bit accepted");
	return true;
}

static bool test_TerrainTile_Dds_Rgba8_Equals_Tga(void)
{
	const int w = 128, h = 128;
	std::vector<unsigned char> rgba;
	tilePicture(w, h, rgba, true);
	const std::vector<unsigned char> tga = tgaFile(w, h, rgba, 32, false, nullptr);
	TerrainTileImage::Image fromTga;
	const char *error = nullptr;
	CHECK(TerrainTileImage::decode(tga.data(), tga.size(), 0, fromTga, &error), "TGA: %s", error);
	const DXGI_FORMAT formats[] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM };
	for (DXGI_FORMAT format : formats) {
		std::vector<unsigned char> dds;
		CHECK(ddsFile(w, h, rgba, format, dds), "DDS encode %d", static_cast<int>(format));
		CHECK(TerrainTileImage::isDds(dds.data(), dds.size()), "DDS magic");
		TerrainTileImage::Image fromDds;
		CHECK(TerrainTileImage::decode(dds.data(), dds.size(), 2, fromDds, &error), "DDS %d: %s", static_cast<int>(format), error);
		CHECK(fromDds.info.format == TerrainTileImage::Format::DDS && fromDds.info.width == w && fromDds.info.height == h, "DDS size");
		CHECK(fromDds.info.tileExtent == 64, "128 / 2 tiles per row = 64, got %d", fromDds.info.tileExtent);
		CHECK(fromDds.bgra == fromTga.bgra, "uncompressed DDS (format %d) differs from TGA", static_cast<int>(format));

		// The header alone gives the size, as countTiles reads it.
		TerrainTileImage::Info info;
		CHECK(TerrainTileImage::readDdsInfo(dds.data(), TerrainTileImage::MAX_HEADER_BYTES, 1, info, &error), "header: %s", error);
		CHECK(info.width == w && info.height == h && info.tileExtent == 128, "header size %dx%d tile %d", info.width, info.height, info.tileExtent);
	}
	return true;
}

static bool test_TerrainTile_Dds_BC1_BC7_Near_Tga(void)
{
	const int w = 256, h = 256;
	std::vector<unsigned char> rgba;
	tilePicture(w, h, rgba, false);
	const std::vector<unsigned char> tga = tgaFile(w, h, rgba, 24, false, "ZHTS4");
	TerrainTileImage::Image fromTga;
	const char *error = nullptr;
	CHECK(TerrainTileImage::decode(tga.data(), tga.size(), 0, fromTga, &error), "TGA: %s", error);
	struct Case { DXGI_FORMAT format; int maxDiff; double meanDiff; };
	const Case cases[] = { { DXGI_FORMAT_BC1_UNORM, 24, 3.0 }, { DXGI_FORMAT_BC7_UNORM, 8, 1.0 } };
	for (const Case &c : cases) {
		std::vector<unsigned char> dds;
		CHECK(ddsFile(w, h, rgba, c.format, dds), "DDS encode %d", static_cast<int>(c.format));
		TerrainTileImage::Image fromDds;
		CHECK(TerrainTileImage::decode(dds.data(), dds.size(), 1, fromDds, &error), "DDS %d: %s", static_cast<int>(c.format), error);
		CHECK(fromDds.info.tileExtent == fromTga.info.tileExtent, "tile size DDS %d, TGA %d", fromDds.info.tileExtent, fromTga.info.tileExtent);
		CHECK(fromDds.bgra.size() == fromTga.bgra.size(), "size");
		int maxDiff = 0;
		double meanDiff = 0.0;
		tileDifference(fromDds, fromTga, maxDiff, meanDiff);
		printf("    format %d: max %d, mean %.2f\n", static_cast<int>(c.format), maxDiff, meanDiff);
		CHECK(maxDiff <= c.maxDiff && meanDiff <= c.meanDiff, "format %d: max %d mean %.2f", static_cast<int>(c.format), maxDiff, meanDiff);
	}
	return true;
}

// Height tiles of the terrain tessellation: BC4 (one channel) decodes to R; the other channels are not used.
static bool test_TerrainTile_Dds_BC4_Height(void)
{
	const int w = 256, h = 256;
	std::vector<unsigned char> rgba;
	tilePicture(w, h, rgba, false);
	const std::vector<unsigned char> tga = tgaFile(w, h, rgba, 24, false, nullptr);
	TerrainTileImage::Image fromTga;
	const char *error = nullptr;
	CHECK(TerrainTileImage::decode(tga.data(), tga.size(), 0, fromTga, &error), "TGA: %s", error);
	std::vector<unsigned char> dds;
	CHECK(ddsFile(w, h, rgba, DXGI_FORMAT_BC4_UNORM, dds), "BC4 encode");
	TerrainTileImage::Image fromDds;
	CHECK(TerrainTileImage::decode(dds.data(), dds.size(), 4, fromDds, &error), "BC4: %s", error);
	CHECK(fromDds.info.tileExtent == 64 && fromDds.bgra.size() == fromTga.bgra.size(), "BC4 size, tile %d", fromDds.info.tileExtent);
	int maxDiff = 0;
	double sum = 0.0;
	for (size_t i = 0; i < fromDds.bgra.size(); i += 4) {
		const int d = abs(static_cast<int>(fromDds.bgra[i + 2]) - static_cast<int>(fromTga.bgra[i + 2]));
		maxDiff = d > maxDiff ? d : maxDiff;
		sum += d;
	}
	const double meanDiff = sum / (fromDds.bgra.size() / 4);
	printf("    BC4 red: max %d, mean %.2f\n", maxDiff, meanDiff);
	CHECK(maxDiff <= 4 && meanDiff <= 1.0, "BC4 red: max %d mean %.2f", maxDiff, meanDiff);
	return true;
}

// Texture package: a tile file holds the highest tier with its mip chain; the loader takes the level of the
// chosen tier. DDS: the level from the file; TGA (scaled, ID ZHTS<s>): halved with a box filter.
static bool test_TerrainTile_Package_MipLevel(void)
{
	const int w = 256, h = 256;
	std::vector<unsigned char> rgba;
	tilePicture(w, h, rgba, true);
	DirectX::ScratchImage source;
	CHECK(SUCCEEDED(source.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, 1)), "image");
	const DirectX::Image *img = source.GetImage(0, 0, 0);
	for (int y = 0; y < h; ++y)
		memcpy(img->pixels + y * img->rowPitch, &rgba[static_cast<size_t>(y) * w * 4], static_cast<size_t>(w) * 4);
	DirectX::ScratchImage chain;
	CHECK(SUCCEEDED(DirectX::GenerateMipMaps(*img, DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_FORCE_NON_WIC, 0, chain)), "mip chain");
	DirectX::Blob blob;
	CHECK(SUCCEEDED(DirectX::SaveToDDSMemory(chain.GetImages(), chain.GetImageCount(), chain.GetMetadata(), DirectX::DDS_FLAGS_NONE, blob)), "save");
	const unsigned char *dds = static_cast<const unsigned char *>(blob.GetBufferPointer());
	const size_t ddsSize = blob.GetBufferSize();
	const char *error = nullptr;

	TerrainTileImage::Info info;
	CHECK(TerrainTileImage::readDdsInfo(dds, TerrainTileImage::MAX_HEADER_BYTES, 1, info, &error), "header: %s", error);
	CHECK(info.mipLevels == 9 && info.mipLevel == 0 && info.width == 256 && info.tileExtent == 256, "level 0: %d levels, %dx%d tile %d",
		info.mipLevels, info.width, info.height, info.tileExtent);
	CHECK(TerrainTileImage::readDdsInfo(dds, TerrainTileImage::MAX_HEADER_BYTES, 1, info, &error, 2), "header level 2: %s", error);
	CHECK(info.mipLevel == 2 && info.width == 64 && info.height == 64 && info.tileExtent == 64, "level 2: %dx%d tile %d",
		info.width, info.height, info.tileExtent);
	CHECK(TerrainTileImage::readDdsInfo(dds, TerrainTileImage::MAX_HEADER_BYTES, 1, info, &error, 20), "header level 20: %s", error);
	CHECK(info.mipLevel == 8 && info.width == 1, "level clamped to the last one: %d", info.mipLevel);

	TerrainTileImage::Image fromDds;
	CHECK(TerrainTileImage::decode(dds, ddsSize, 1, fromDds, &error, 2), "DDS level 2: %s", error);
	CHECK(fromDds.info.width == 64 && fromDds.info.height == 64 && fromDds.info.tileExtent == 64 && fromDds.info.mipLevel == 2,
		"DDS level 2: %dx%d tile %d", fromDds.info.width, fromDds.info.height, fromDds.info.tileExtent);

	// The same picture as a TGA of the highest tier, halved twice
	const std::vector<unsigned char> tga = tgaFile(w, h, rgba, 32, false, "ZHTS4");
	TerrainTileImage::Image fromTga;
	CHECK(TerrainTileImage::decode(tga.data(), tga.size(), 0, fromTga, &error, 2), "TGA level 2: %s", error);
	CHECK(fromTga.info.width == 64 && fromTga.info.height == 64 && fromTga.info.tileExtent == 64 && fromTga.info.mipLevel == 2,
		"TGA level 2: %dx%d tile %d", fromTga.info.width, fromTga.info.height, fromTga.info.tileExtent);
	int maxDiff = 0;
	double meanDiff = 0.0;
	tileDifference(fromDds, fromTga, maxDiff, meanDiff);
	printf("    level 2 DDS against halved TGA: max %d, mean %.2f\n", maxDiff, meanDiff);
	CHECK(maxDiff <= 2 && meanDiff <= 0.5, "level 2: max %d mean %.2f", maxDiff, meanDiff);

	// Never below tiles of 64 pixels: an original TGA (tile 64) stays as it is
	const std::vector<unsigned char> original = tgaFile(128, 64, std::vector<unsigned char>(rgba.begin(), rgba.begin() + 128 * 64 * 4), 32, false, nullptr);
	CHECK(TerrainTileImage::decode(original.data(), original.size(), 0, fromTga, &error, 3), "original TGA: %s", error);
	CHECK(fromTga.info.width == 128 && fromTga.info.tileExtent == 64 && fromTga.info.mipLevel == 0, "original TGA halved");
	return true;
}

static bool test_TerrainTile_Dds_Rejected(void)
{
	const int w = 64, h = 64;
	std::vector<unsigned char> rgba;
	tilePicture(w, h, rgba, false);
	TerrainTileImage::Image image;
	TerrainTileImage::Info info;
	const char *error = nullptr;

	std::vector<unsigned char> dds;
	CHECK(ddsFile(w, h, rgba, DXGI_FORMAT_BC3_UNORM, dds), "BC3 encode");
	CHECK(!TerrainTileImage::decode(dds.data(), dds.size(), 1, image, &error) && error != nullptr, "BC3 accepted");
	CHECK(!TerrainTileImage::readDdsInfo(dds.data(), dds.size(), 1, info, &error), "BC3 header accepted");

	CHECK(ddsFile(w, h, rgba, DXGI_FORMAT_BC1_UNORM, dds), "BC1 encode");
	const std::vector<unsigned char> truncated(dds.begin(), dds.begin() + static_cast<std::ptrdiff_t>(dds.size() / 2));
	CHECK(!TerrainTileImage::decode(truncated.data(), truncated.size(), 1, image, &error) && image.bgra.empty(), "truncated DDS accepted");
	CHECK(!TerrainTileImage::decode(dds.data(), 64, 1, image, &error), "DDS header only accepted");

	const unsigned char garbage[] = { 'D', 'D', 'S', ' ', 1, 2, 3 };
	CHECK(!TerrainTileImage::decode(garbage, sizeof(garbage), 1, image, &error), "garbage DDS accepted");
	CHECK(!TerrainTileImage::readDdsInfo(garbage, sizeof(garbage), 1, info, &error), "garbage DDS header accepted");
	CHECK(!TerrainTileImage::decode(garbage, 3, 1, image, &error), "three bytes accepted");
	CHECK(!TerrainTileImage::decode(nullptr, 0, 1, image, &error), "no data accepted");

	// A cube map is not a tile file.
	DirectX::ScratchImage cube;
	CHECK(SUCCEEDED(cube.InitializeCube(DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, 1)), "cube init");
	DirectX::Blob blob;
	CHECK(SUCCEEDED(DirectX::SaveToDDSMemory(cube.GetImages(), cube.GetImageCount(), cube.GetMetadata(), DirectX::DDS_FLAGS_NONE, blob)), "cube save");
	CHECK(!TerrainTileImage::decode(blob.GetBufferPointer(), blob.GetBufferSize(), 1, image, &error), "cube map accepted");
	return true;
}

// ==================================================================================================
// Vulkan device requirements and choice (vulkanrequirements.cpp)
// ==================================================================================================

static unsigned Vk_Version(unsigned major, unsigned minor, unsigned patch)
{
	return (major << 22) | (minor << 12) | patch;
}

// A device that meets every requirement.
static VulkanDeviceInfo Vk_Usable_Device(const char* name, int type, unsigned long long memory)
{
	VulkanDeviceInfo d;
	memset(&d, 0, sizeof(d));
	snprintf(d.name, sizeof(d.name), "%s", name);
	d.api_version = Vk_Version(1, 3, 275);
	d.type = type;
	d.local_memory = memory;
	d.swapchain = d.present = d.dynamic_rendering = d.synchronization2 = true;
	d.tessellation = d.anisotropy = d.bc_textures = d.depth_clamp = d.depth_bias_clamp = d.bgra_target = true;
	d.depth_d24s8 = true;
	return d;
}

static bool test_Vulkan_Requirements(void)
{
	VulkanDeviceInfo d = Vk_Usable_Device("ok", VULKAN_DEVICE_DISCRETE, 1ull << 33);
	CHECK(Vulkan_Missing_Requirements(d) == 0, "usable device reports 0x%X", Vulkan_Missing_Requirements(d));

	// Vulkan 1.2 is too old, 1.4 and 2.0 are fine.
	d.api_version = Vk_Version(1, 2, 999);
	CHECK(Vulkan_Missing_Requirements(d) == VULKAN_MISSING_API_1_3, "1.2 accepted");
	d.api_version = Vk_Version(1, 4, 0);
	CHECK(Vulkan_Missing_Requirements(d) == 0, "1.4 rejected");
	d.api_version = Vk_Version(2, 0, 0);
	CHECK(Vulkan_Missing_Requirements(d) == 0, "2.0 rejected");

	// Each flag maps to its own bit.
	struct { bool VulkanDeviceInfo::*field; unsigned bit; } flags[] = {
		{ &VulkanDeviceInfo::swapchain,         VULKAN_MISSING_SWAPCHAIN },
		{ &VulkanDeviceInfo::present,           VULKAN_MISSING_PRESENT },
		{ &VulkanDeviceInfo::dynamic_rendering, VULKAN_MISSING_DYNAMIC_RENDERING },
		{ &VulkanDeviceInfo::synchronization2,  VULKAN_MISSING_SYNCHRONIZATION2 },
		{ &VulkanDeviceInfo::tessellation,      VULKAN_MISSING_TESSELLATION },
		{ &VulkanDeviceInfo::anisotropy,        VULKAN_MISSING_ANISOTROPY },
		{ &VulkanDeviceInfo::bc_textures,       VULKAN_MISSING_BC_TEXTURES },
		{ &VulkanDeviceInfo::depth_clamp,       VULKAN_MISSING_DEPTH_CLAMP },
		{ &VulkanDeviceInfo::depth_bias_clamp,  VULKAN_MISSING_DEPTH_BIAS_CLAMP },
		{ &VulkanDeviceInfo::bgra_target,       VULKAN_MISSING_BGRA_TARGET },
	};
	for (const auto& f : flags) {
		VulkanDeviceInfo m = Vk_Usable_Device("m", VULKAN_DEVICE_DISCRETE, 0);
		m.*f.field = false;
		CHECK(Vulkan_Missing_Requirements(m) == f.bit, "bit 0x%X, got 0x%X", f.bit, Vulkan_Missing_Requirements(m));
	}

	// One of the two depth/stencil formats is enough (AMD has no D24S8).
	d = Vk_Usable_Device("amd", VULKAN_DEVICE_DISCRETE, 0);
	d.depth_d24s8 = false;
	d.depth_d32s8 = true;
	CHECK(Vulkan_Missing_Requirements(d) == 0, "D32S8 alone rejected");
	d.depth_d32s8 = false;
	CHECK(Vulkan_Missing_Requirements(d) == VULKAN_MISSING_DEPTH_STENCIL, "no depth format accepted");
	return true;
}

static bool test_Vulkan_Describe_Missing(void)
{
	char text[256];
	Vulkan_Describe_Missing(0, text, sizeof(text));
	CHECK(text[0] == 0, "nothing missing gives '%s'", text);
	Vulkan_Describe_Missing(VULKAN_MISSING_API_1_3 | VULKAN_MISSING_SWAPCHAIN, text, sizeof(text));
	CHECK(strcmp(text, "Vulkan 1.3, VK_KHR_swapchain") == 0, "got '%s'", text);
	// Every bit has a name.
	for (unsigned bit = 0; bit < VULKAN_MISSING_COUNT; ++bit) {
		Vulkan_Describe_Missing(1u << bit, text, sizeof(text));
		CHECK(text[0] != 0 && strchr(text, ',') == nullptr, "bit %u gives '%s'", bit, text);
	}
	// A short buffer is cut, but terminated.
	char short_text[12];
	Vulkan_Describe_Missing(0xFFFFFFFFu, short_text, sizeof(short_text));
	CHECK(strlen(short_text) < sizeof(short_text), "not terminated");

	char version[32];
	Vulkan_Version_Text(Vk_Version(1, 3, 275), version, sizeof(version));
	CHECK(strcmp(version, "1.3.275") == 0, "version '%s'", version);
	return true;
}

static bool test_Vulkan_Choose_Device(void)
{
	CHECK(Vulkan_Choose_Device(nullptr, 0) == -1, "no devices");

	// Discrete before integrated, whatever the order.
	VulkanDeviceInfo two[2] = {
		Vk_Usable_Device("igpu", VULKAN_DEVICE_INTEGRATED, 1ull << 31),
		Vk_Usable_Device("dgpu", VULKAN_DEVICE_DISCRETE, 1ull << 30),
	};
	CHECK(Vulkan_Choose_Device(two, 2) == 1, "integrated preferred");

	// An unusable discrete device does not win.
	two[1].swapchain = false;
	CHECK(Vulkan_Choose_Device(two, 2) == 0, "unusable device chosen");
	two[0].bc_textures = false;
	CHECK(Vulkan_Choose_Device(two, 2) == -1, "nothing usable but chosen");

	// A software renderer only when nothing else is there.
	VulkanDeviceInfo soft[2] = {
		Vk_Usable_Device("llvmpipe", VULKAN_DEVICE_CPU, 1ull << 34),
		Vk_Usable_Device("other", VULKAN_DEVICE_OTHER, 1ull << 20),
	};
	CHECK(Vulkan_Choose_Device(soft, 2) == 1, "CPU device before another device");
	CHECK(Vulkan_Choose_Device(soft, 1) == 0, "CPU device alone not taken");

	// Same type: more memory wins, a tie keeps the lower index.
	VulkanDeviceInfo same[3] = {
		Vk_Usable_Device("a", VULKAN_DEVICE_DISCRETE, 4ull << 30),
		Vk_Usable_Device("b", VULKAN_DEVICE_DISCRETE, 8ull << 30),
		Vk_Usable_Device("c", VULKAN_DEVICE_DISCRETE, 8ull << 30),
	};
	CHECK(Vulkan_Choose_Device(same, 3) == 1, "memory or tie rule broken");

	CHECK(strcmp(Vulkan_Device_Type_Name(VULKAN_DEVICE_CPU), "cpu") == 0, "type name");
	CHECK(strcmp(Vulkan_Device_Type_Name(99), "other") == 0, "unknown type name");
	return true;
}

// ==================================================================================================
// SPIR-V reflection and linking (vulkanspirv.cpp) on hand-written modules
// ==================================================================================================

namespace {

struct SpirvBuilder
{
	std::vector<uint32_t> words;

	SpirvBuilder() { words = { 0x07230203u, 0x00010600u, 0u, 100u, 0u }; }
	void Op(uint32_t op, std::initializer_list<uint32_t> operands)
	{
		words.push_back((static_cast<uint32_t>(operands.size() + 1) << 16) | op);
		words.insert(words.end(), operands.begin(), operands.end());
	}
	void Op_String(uint32_t op, std::initializer_list<uint32_t> operands, const char* text)
	{
		std::vector<uint32_t> literal((strlen(text) + 4) / 4, 0u);
		memcpy(literal.data(), text, strlen(text));
		words.push_back((static_cast<uint32_t>(operands.size() + literal.size() + 1) << 16) | op);
		words.insert(words.end(), operands.begin(), operands.end());
		words.insert(words.end(), literal.begin(), literal.end());
	}
};

// Opcodes and decorations of the SPIR-V specification used below.
enum : uint32_t { OP_EXTENSION = 10, OP_VARIABLE = 59, OP_DECORATE = 71, OP_DECORATE_STRING = 5632,
				  DEC_BUILTIN = 11, DEC_LOCATION = 30, DEC_BINDING = 33, DEC_SET = 34, DEC_SEMANTIC = 5635 };

/// A pixel shader as DXC writes it with -fspv-reflect: b3, t2, s1, two inputs, SV_Target0.
SpirvBuilder Spirv_Pixel_Shader(uint32_t cb_binding, uint32_t cb_set)
{
	SpirvBuilder b;
	b.Op_String(OP_EXTENSION, {}, "SPV_GOOGLE_hlsl_functionality1");
	b.Op_String(OP_EXTENSION, {}, "SPV_GOOGLE_user_type");
	b.Op(OP_DECORATE, { 10, DEC_SET, cb_set });
	b.Op(OP_DECORATE, { 10, DEC_BINDING, cb_binding });
	b.Op(OP_DECORATE, { 11, DEC_SET, 0 });
	b.Op(OP_DECORATE, { 11, DEC_BINDING, 16 + 2 });
	b.Op(OP_DECORATE, { 12, DEC_SET, 0 });
	b.Op(OP_DECORATE, { 12, DEC_BINDING, 32 + 1 });
	b.Op(OP_DECORATE, { 20, DEC_LOCATION, 0 });
	b.Op_String(OP_DECORATE_STRING, { 20, DEC_SEMANTIC }, "texcoord1");
	b.Op(OP_DECORATE, { 21, DEC_LOCATION, 1 });
	b.Op_String(OP_DECORATE_STRING, { 21, DEC_SEMANTIC }, "COLOR");
	b.Op(OP_DECORATE, { 22, DEC_BUILTIN, 15 });
	b.Op_String(OP_DECORATE_STRING, { 22, DEC_SEMANTIC }, "SV_Position");
	b.Op(OP_DECORATE, { 30, DEC_LOCATION, 0 });
	b.Op_String(OP_DECORATE_STRING, { 30, DEC_SEMANTIC }, "SV_Target0");
	b.Op(OP_VARIABLE, { 1, 10, 2 });	// Uniform
	b.Op(OP_VARIABLE, { 2, 11, 0 });	// UniformConstant
	b.Op(OP_VARIABLE, { 3, 12, 0 });
	b.Op(OP_VARIABLE, { 4, 20, 1 });	// Input
	b.Op(OP_VARIABLE, { 4, 21, 1 });
	b.Op(OP_VARIABLE, { 4, 22, 1 });
	b.Op(OP_VARIABLE, { 5, 30, 3 });	// Output
	return b;
}

/// The literal of decoration `dec` on `id`, ~0u if there is none.
uint32_t Spirv_Decoration(const std::vector<uint32_t>& w, uint32_t id, uint32_t dec)
{
	for (size_t i = 5; i < w.size(); i += w[i] >> 16) {
		if ((w[i] >> 16) == 0) break;
		if ((w[i] & 0xFFFFu) == OP_DECORATE && w[i + 1] == id && w[i + 2] == dec && (w[i] >> 16) >= 4) return w[i + 3];
	}
	return ~0u;
}

bool Spirv_Has_Op(const std::vector<uint32_t>& w, uint32_t op)
{
	for (size_t i = 5; i < w.size(); i += w[i] >> 16) {
		if ((w[i] >> 16) == 0) break;
		if ((w[i] & 0xFFFFu) == op) return true;
	}
	return false;
}

}  // namespace

static bool test_Spirv_Prepare(void)
{
	SpirvBuilder ps = Spirv_Pixel_Shader(3, 0);
	SpirvReflection r;
	char error[128];
	CHECK(Spirv_Prepare(ps.words, 1, r, error, sizeof(error)), "prepare failed: %s", error);
	CHECK(r.cb_mask == (1u << 3) && r.srv_mask == (1u << 2) && r.sampler_mask == (1u << 1),
		  "masks %x %x %x", r.cb_mask, r.srv_mask, r.sampler_mask);
	CHECK(r.input_count == 3 && r.output_count == 1, "io %u/%u", r.input_count, r.output_count);
	CHECK(strcmp(r.inputs[0].semantic, "TEXCOORD") == 0 && r.inputs[0].index == 1 && r.inputs[0].location == 0, "input 0");
	CHECK(strcmp(r.inputs[1].semantic, "COLOR") == 0 && r.inputs[1].index == 0 && r.inputs[1].location == 1, "input 1");
	CHECK(r.inputs[2].location == ~0u, "built-in input got a location");
	CHECK(strcmp(r.outputs[0].semantic, "SV_TARGET") == 0 && r.outputs[0].location == 0, "output");
	// Every resource now in set 1; reflection instructions removed.
	CHECK(Spirv_Decoration(ps.words, 10, DEC_SET) == 1 && Spirv_Decoration(ps.words, 11, DEC_SET) == 1 &&
		  Spirv_Decoration(ps.words, 12, DEC_SET) == 1, "set not moved");
	CHECK(Spirv_Decoration(ps.words, 11, DEC_BINDING) == 18, "binding changed");
	CHECK(!Spirv_Has_Op(ps.words, OP_EXTENSION) && !Spirv_Has_Op(ps.words, OP_DECORATE_STRING), "reflection left in");

	SpirvBuilder beyond = Spirv_Pixel_Shader(50, 0);
	CHECK(!Spirv_Prepare(beyond.words, 1, r, error, sizeof(error)), "binding beyond the s range accepted");
	SpirvBuilder space = Spirv_Pixel_Shader(3, 2);
	CHECK(!Spirv_Prepare(space.words, 1, r, error, sizeof(error)), "register space accepted");
	std::vector<uint32_t> junk = { 1, 2, 3, 4, 5, 6 };
	CHECK(!Spirv_Prepare(junk, 0, r, error, sizeof(error)), "no SPIR-V accepted");
	return true;
}

static bool test_Spirv_Link_Inputs(void)
{
	SpirvBuilder ps = Spirv_Pixel_Shader(3, 0);
	SpirvReflection self;
	CHECK(Spirv_Prepare(ps.words, 1, self, nullptr, 0), "prepare failed");

	// The vertex shader writes COLOR0 to 0 and TEXCOORD1 to 2: the inputs follow by semantic.
	SpirvReflection vs;
	memset(&vs, 0, sizeof(vs));
	vs.output_count = 3;
	snprintf(vs.outputs[0].semantic, sizeof(vs.outputs[0].semantic), "%s", "COLOR");    vs.outputs[0].index = 0; vs.outputs[0].location = 0;
	snprintf(vs.outputs[1].semantic, sizeof(vs.outputs[1].semantic), "%s", "TEXCOORD"); vs.outputs[1].index = 0; vs.outputs[1].location = 1;
	snprintf(vs.outputs[2].semantic, sizeof(vs.outputs[2].semantic), "%s", "TEXCOORD"); vs.outputs[2].index = 1; vs.outputs[2].location = 2;
	std::vector<uint32_t> linked = ps.words;
	CHECK(Spirv_Link_Inputs(linked, self, vs), "nothing linked");
	CHECK(Spirv_Decoration(linked, 20, DEC_LOCATION) == 2, "TEXCOORD1 not at 2");
	CHECK(Spirv_Decoration(linked, 21, DEC_LOCATION) == 0, "COLOR0 not at 0");
	CHECK(Spirv_Decoration(linked, 30, DEC_LOCATION) == 0, "output moved");

	// Same order as the shader's own: unchanged.
	vs.outputs[0].location = 1; vs.outputs[2].location = 0; vs.outputs[1].location = 5;
	linked = ps.words;
	CHECK(!Spirv_Link_Inputs(linked, self, vs), "identical locations reported as change");
	CHECK(linked == ps.words, "words changed");

	char name[16];
	unsigned index = 0;
	Spirv_Split_Semantic("texcoord12", name, sizeof(name), &index);
	CHECK(strcmp(name, "TEXCOORD") == 0 && index == 12, "split '%s' %u", name, index);
	Spirv_Split_Semantic("POSITION", name, sizeof(name), &index);
	CHECK(strcmp(name, "POSITION") == 0 && index == 0, "split without index");
	return true;
}

// ==================================================================================================

int main(void)
{
	struct TestCase { const char* name; bool (*fn)(void); };
	const TestCase tests[] = {
		{ "Format_Mapping",               test_Format_Mapping },
		{ "Device_Format_Mapping",        test_Device_Format_Mapping },
		{ "Convert_A4R4G4B4",             test_Convert_A4R4G4B4 },
		{ "Convert_X1R5G5B5",             test_Convert_X1R5G5B5 },
		{ "Convert_L8_A8L8",              test_Convert_L8_A8L8 },
		{ "Convert_Rejects_Unsupported",  test_Convert_Rejects_Unsupported },
		{ "Convert_Image_Uses_Pitch",     test_Convert_Image_Uses_Pitch },
		{ "Cull_And_Fill",                test_Cull_And_Fill },
		{ "Blend_Alpha_Channel",          test_Blend_Alpha_Channel },
		{ "Stencil_Masks_Are_Eight_Bit",  test_Stencil_Masks_Are_Eight_Bit },
		{ "Sampler_Filter_And_LOD",       test_Sampler_Filter_And_LOD },
		{ "Primitive_Topology_And_Counts",test_Primitive_Topology_And_Counts },
		{ "Decl_Types_And_Semantics",     test_Decl_Types_And_Semantics },
		{ "Color_To_Float4",              test_Color_To_Float4 },
		{ "FVF_To_Vertex_Elements",       test_FVF_To_Vertex_Elements },
		{ "Present_Layout",               test_Present_Layout },
		{ "TerrainTile_Dds_Extent",       test_TerrainTile_Dds_Extent },
		{ "TerrainTile_Tga_Variants",     test_TerrainTile_Tga_Variants },
		{ "TerrainTile_Dds_Rgba8_Equals_Tga", test_TerrainTile_Dds_Rgba8_Equals_Tga },
		{ "TerrainTile_Dds_BC1_BC7_Near_Tga", test_TerrainTile_Dds_BC1_BC7_Near_Tga },
		{ "TerrainTile_Dds_BC4_Height",   test_TerrainTile_Dds_BC4_Height },
		{ "TerrainTile_Dds_Rejected",     test_TerrainTile_Dds_Rejected },
		{ "TerrainTile_Package_MipLevel", test_TerrainTile_Package_MipLevel },
		{ "Vulkan_Requirements",          test_Vulkan_Requirements },
		{ "Vulkan_Describe_Missing",      test_Vulkan_Describe_Missing },
		{ "Vulkan_Choose_Device",         test_Vulkan_Choose_Device },
		{ "Spirv_Prepare",                test_Spirv_Prepare },
		{ "Spirv_Link_Inputs",            test_Spirv_Link_Inputs },
	};

	int failCount = 0;
	const int count = static_cast<int>(sizeof(tests) / sizeof(tests[0]));
	for (int i = 0; i < count; ++i) {
		printf("[RUN ] %s\n", tests[i].name);
		fflush(stdout);
		const bool ok = tests[i].fn();
		printf("[%s] %s\n", ok ? "PASS" : "FAIL", tests[i].name);
		fflush(stdout);
		if (!ok) ++failCount;
	}

	if (failCount == 0) {
		printf("All %d tests passed (%d Einzelpruefungen).\n", count, g_checks);
		return 0;
	}
	printf("%d of %d tests FAILED.\n", failCount, count);
	return 1;
}
