/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ww3dtypes.h ----------------------------------------------------------------------------------
//
// Backend-neutral types and constants of the render interface. The shared layer (renderdevice.cpp,
// rendercaps.cpp, texture*.cpp, surfaceclass.cpp, ww3dformat/formconv, texutil, renderfvf.cpp,
// vertexbuffer.cpp/indexbuffer.cpp, texturemanager.cpp, ddsfile.cpp, missingtexture.cpp, ww3d.cpp)
// uses only these names.
//
// The numeric values follow Direct3D 9 because the engine's fixed-function model is shaped like it;
// the D3D11 backend maps them to its state objects and the uber shader. They must stay unchanged
// because assets, ini files and the uber shader expect them.
//
// Handles: device objects are opaque pointers. Only the backend calls methods on them; everything
// else goes through RenderDevice::Lock_*/Unlock_*/Get_*_Desc/Release_Resource.
// ----------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef WW3DTYPES_H
#define WW3DTYPES_H

#include "always.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>	// HWND, HRESULT, RECT, POINT, DWORD
// windows.h/winnt.h defines its own "BitTest" (bit test intrinsic taking a pointer), which would
// override Lib/BaseType.h's "BitTest" (plain bitwise AND) when included after it.
#undef BitTest
#define BitTest( x, i ) ( ( (x) & (i) ) != 0 )

// --- Handles ----------------------------------------------------------------------------------
// Pointers to incomplete marker structs. Outside the backend they are plain identifiers -- they
// can be passed on, compared and checked for NULL, but not dereferenced. The D3D11 backend puts
// its own object structs behind them.
struct WW3DDeviceObject;
struct WW3DInterfaceObject;
struct WW3DTextureObject;
struct WW3DSurfaceObject;
struct WW3DVertexShaderObject;
struct WW3DPixelShaderObject;
struct WW3DVertexDeclObject;
struct WW3DVertexBufferObject;
struct WW3DIndexBufferObject;
struct WW3DSwapChainObject;

using WW3DDeviceHandle = WW3DDeviceObject*; ///< backend internal only (device)
using WW3DInterfaceHandle = WW3DInterfaceObject*; ///< backend internal only (DXGI factory)
using WW3DTextureHandle = WW3DTextureObject*; ///< 2D texture (also serves as base texture handle)
using WW3DBaseTextureHandle = WW3DTextureHandle;
using WW3DSurfaceHandle = WW3DSurfaceObject*;
using WW3DVertexShaderHandle = WW3DVertexShaderObject*;
using WW3DPixelShaderHandle = WW3DPixelShaderObject*;
using WW3DVertexDeclHandle = WW3DVertexDeclObject*;
using WW3DVertexBufferHandle = WW3DVertexBufferObject*;
using WW3DIndexBufferHandle = WW3DIndexBufferObject*;
using WW3DSwapChainHandle = WW3DSwapChainObject*;

// --- Scalar types (state numbers; unsigned so backend D3D enums fit as well) ---------------------
using WW3DRenderStateType = unsigned; ///< WW3D_RS_*
using WW3DTextureStageStateType = unsigned; ///< WW3D_TSS_*
using WW3DSamplerStateType = unsigned; ///< WW3D_SAMP_*
using WW3DTransformType = unsigned; ///< WW3D_TS_*
using WW3DPrimitiveType = unsigned; ///< WW3D_PT_*
using WW3DTextureOp = unsigned; ///< WW3D_TOP_*
using WW3DBlend = unsigned; ///< WW3D_BLEND_*
using WW3DCmpFunc = unsigned; ///< WW3D_CMP_*
using WW3DDeviceFormat = unsigned; ///< WW3D_FMT_* (device format; WW3DFormat in ww3dformat.h is the engine subset)
using WW3DColor = unsigned; ///< AARRGGBB

// --- Structures (same layout as their D3D9 counterparts) -----------------------------------------
struct WW3DColorValue { float r, g, b, a; };
struct WW3DVector { float x, y, z; };

struct WW3DLight
{
	unsigned		Type;			///< WW3D_LIGHT_*
	WW3DColorValue	Diffuse;
	WW3DColorValue	Specular;
	WW3DColorValue	Ambient;
	WW3DVector		Position;
	WW3DVector		Direction;
	float			Range;
	float			Falloff;
	float			Attenuation0;
	float			Attenuation1;
	float			Attenuation2;
	float			Theta;
	float			Phi;
};

struct WW3DMaterial
{
	WW3DColorValue	Diffuse;
	WW3DColorValue	Ambient;
	WW3DColorValue	Specular;
	WW3DColorValue	Emissive;
	float			Power;
};

struct WW3DViewport
{
	unsigned	X, Y, Width, Height;
	float		MinZ, MaxZ;
};

struct WW3DLockedRect
{
	int		Pitch;
	void*	pBits;
};

struct WW3DSurfaceDesc
{
	unsigned	Format;				///< WW3D_FMT_*
	unsigned	Type;				///< resource type (backend internal)
	unsigned	Usage;				///< WW3D_USAGE_*
	unsigned	Pool;				///< WW3D_POOL_*
	unsigned	MultiSampleType;
	unsigned	MultiSampleQuality;
	unsigned	Width;
	unsigned	Height;
};

struct WW3DVertexElement
{
	unsigned short	Stream;
	unsigned short	Offset;
	unsigned char	Type;			///< WW3D_DECLTYPE_*
	unsigned char	Method;			///< WW3D_DECLMETHOD_*
	unsigned char	Usage;			///< WW3D_DECLUSAGE_*
	unsigned char	UsageIndex;
};
#define WW3D_DECL_END() { 0xFF, 0, 17, 0, 0, 0 }

struct WW3DMatrixRaw { float m[4][4]; };	///< rows like D3DMATRIX (transposed relative to the Westwood Matrix4x4)

// Natural x64 alignment, 1104 bytes like D3DADAPTER_IDENTIFIER9.
struct WW3DAdapterIdentifier
{
	char				Driver[512];
	char				Description[512];
	char				DeviceName[32];
	unsigned __int64	DriverVersion;
	unsigned			VendorId;
	unsigned			DeviceId;
	unsigned			SubSysId;
	unsigned			Revision;
	unsigned char		DeviceIdentifier[16];	///< GUID
	unsigned			WHQLLevel;
};

// --- Color macros -----------------------------------------------------------------------------
#define WW3D_COLOR_ARGB(a,r,g,b)	((WW3DColor)((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff)))
#define WW3D_COLOR_RGBA(r,g,b,a)	WW3D_COLOR_ARGB(a,r,g,b)
#define WW3D_COLOR_XRGB(r,g,b)		WW3D_COLOR_ARGB(0xff,r,g,b)
#define WW3D_COLOR_COLORVALUE(r,g,b,a)	WW3D_COLOR_RGBA((unsigned)((r)*255.f),(unsigned)((g)*255.f),(unsigned)((b)*255.f),(unsigned)((a)*255.f))

// --- Render states ----------------------------------------------------------------------------
enum {
	WW3D_RS_ZENABLE = 7,
	WW3D_RS_FILLMODE = 8,
	WW3D_RS_SHADEMODE = 9,
	WW3D_RS_ZWRITEENABLE = 14,
	WW3D_RS_ALPHATESTENABLE = 15,
	WW3D_RS_LASTPIXEL = 16,
	WW3D_RS_SRCBLEND = 19,
	WW3D_RS_DESTBLEND = 20,
	WW3D_RS_CULLMODE = 22,
	WW3D_RS_ZFUNC = 23,
	WW3D_RS_ALPHAREF = 24,
	WW3D_RS_ALPHAFUNC = 25,
	WW3D_RS_DITHERENABLE = 26,
	WW3D_RS_ALPHABLENDENABLE = 27,
	WW3D_RS_FOGENABLE = 28,
	WW3D_RS_SPECULARENABLE = 29,
	WW3D_RS_FOGCOLOR = 34,
	WW3D_RS_FOGTABLEMODE = 35,
	WW3D_RS_FOGSTART = 36,
	WW3D_RS_FOGEND = 37,
	WW3D_RS_FOGDENSITY = 38,
	WW3D_RS_RANGEFOGENABLE = 48,
	WW3D_RS_STENCILENABLE = 52,
	WW3D_RS_STENCILFAIL = 53,
	WW3D_RS_STENCILZFAIL = 54,
	WW3D_RS_STENCILPASS = 55,
	WW3D_RS_STENCILFUNC = 56,
	WW3D_RS_STENCILREF = 57,
	WW3D_RS_STENCILMASK = 58,
	WW3D_RS_STENCILWRITEMASK = 59,
	WW3D_RS_TEXTUREFACTOR = 60,
	WW3D_RS_WRAP0 = 128,
	WW3D_RS_WRAP1 = 129,
	WW3D_RS_WRAP2 = 130,
	WW3D_RS_WRAP3 = 131,
	WW3D_RS_CLIPPING = 136,
	WW3D_RS_LIGHTING = 137,
	WW3D_RS_AMBIENT = 139,
	WW3D_RS_FOGVERTEXMODE = 140,
	WW3D_RS_COLORVERTEX = 141,
	WW3D_RS_LOCALVIEWER = 142,
	WW3D_RS_NORMALIZENORMALS = 143,
	WW3D_RS_DIFFUSEMATERIALSOURCE = 145,
	WW3D_RS_SPECULARMATERIALSOURCE = 146,
	WW3D_RS_AMBIENTMATERIALSOURCE = 147,
	WW3D_RS_EMISSIVEMATERIALSOURCE = 148,
	WW3D_RS_VERTEXBLEND = 151,
	WW3D_RS_CLIPPLANEENABLE = 152,
	WW3D_RS_POINTSIZE = 154,
	// Point sprite states (the backend reports no point sprite support).
	WW3D_RS_POINTSIZE_MIN = 155,
	WW3D_RS_POINTSPRITEENABLE = 156,
	WW3D_RS_POINTSCALEENABLE = 157,
	WW3D_RS_POINTSCALE_A = 158,
	WW3D_RS_POINTSCALE_B = 159,
	WW3D_RS_POINTSCALE_C = 160,
	WW3D_RS_POINTSIZE_MAX = 166,
	// Legacy D3D8 states without backend support; they only give disabled code paths a name.
	WW3D_RS_PATCHSEGMENTS = 164,
	WW3D_RS_SOFTWAREVERTEXPROCESSING = 153,
	WW3D_RS_MULTISAMPLEANTIALIAS = 161,
	WW3D_RS_MULTISAMPLEMASK = 162,
	WW3D_RS_COLORWRITEENABLE = 168,
	WW3D_RS_BLENDOP = 171,
	WW3D_RS_SLOPESCALEDEPTHBIAS = 175,
	WW3D_RS_DEPTHBIAS = 195
};

enum { WW3D_COLORWRITEENABLE_RED = 1, WW3D_COLORWRITEENABLE_GREEN = 2, WW3D_COLORWRITEENABLE_BLUE = 4, WW3D_COLORWRITEENABLE_ALPHA = 8 };
enum { WW3D_CLIPPLANE0 = 1 };

enum { WW3D_BLEND_ZERO = 1, WW3D_BLEND_ONE = 2, WW3D_BLEND_SRCCOLOR = 3, WW3D_BLEND_INVSRCCOLOR = 4, WW3D_BLEND_SRCALPHA = 5,
       WW3D_BLEND_INVSRCALPHA = 6, WW3D_BLEND_DESTALPHA = 7, WW3D_BLEND_INVDESTALPHA = 8, WW3D_BLEND_DESTCOLOR = 9,
       WW3D_BLEND_INVDESTCOLOR = 10, WW3D_BLEND_SRCALPHASAT = 11 };
enum { WW3D_BLENDOP_ADD = 1, WW3D_BLENDOP_SUBTRACT = 2, WW3D_BLENDOP_REVSUBTRACT = 3, WW3D_BLENDOP_MIN = 4, WW3D_BLENDOP_MAX = 5 };
enum { WW3D_CMP_NEVER = 1, WW3D_CMP_LESS = 2, WW3D_CMP_EQUAL = 3, WW3D_CMP_LESSEQUAL = 4, WW3D_CMP_GREATER = 5,
       WW3D_CMP_NOTEQUAL = 6, WW3D_CMP_GREATEREQUAL = 7, WW3D_CMP_ALWAYS = 8 };
enum { WW3D_CULL_NONE = 1, WW3D_CULL_CW = 2, WW3D_CULL_CCW = 3 };
enum { WW3D_FILL_POINT = 1, WW3D_FILL_WIREFRAME = 2, WW3D_FILL_SOLID = 3 };
enum { WW3D_SHADE_FLAT = 1, WW3D_SHADE_GOURAUD = 2 };
enum { WW3D_STENCILOP_KEEP = 1, WW3D_STENCILOP_ZERO = 2, WW3D_STENCILOP_REPLACE = 3, WW3D_STENCILOP_INCRSAT = 4,
       WW3D_STENCILOP_DECRSAT = 5, WW3D_STENCILOP_INVERT = 6, WW3D_STENCILOP_INCR = 7, WW3D_STENCILOP_DECR = 8 };
enum { WW3D_FOG_NONE = 0, WW3D_FOG_EXP = 1, WW3D_FOG_EXP2 = 2, WW3D_FOG_LINEAR = 3 };
enum { WW3D_MCS_MATERIAL = 0, WW3D_MCS_COLOR1 = 1, WW3D_MCS_COLOR2 = 2 };
enum { WW3D_WRAP_U = 1, WW3D_WRAP_V = 2, WW3D_WRAP_W = 4 };

// --- Texture stages and samplers ---------------------------------------------------------------
enum {
	WW3D_TSS_COLOROP = 1, WW3D_TSS_COLORARG1 = 2, WW3D_TSS_COLORARG2 = 3,
	WW3D_TSS_ALPHAOP = 4, WW3D_TSS_ALPHAARG1 = 5, WW3D_TSS_ALPHAARG2 = 6,
	WW3D_TSS_BUMPENVMAT00 = 7, WW3D_TSS_BUMPENVMAT01 = 8, WW3D_TSS_BUMPENVMAT10 = 9, WW3D_TSS_BUMPENVMAT11 = 10,
	WW3D_TSS_TEXCOORDINDEX = 11,
	WW3D_TSS_BUMPENVLSCALE = 22, WW3D_TSS_BUMPENVLOFFSET = 23,
	WW3D_TSS_TEXTURETRANSFORMFLAGS = 24,
	WW3D_TSS_COLORARG0 = 26, WW3D_TSS_ALPHAARG0 = 27, WW3D_TSS_RESULTARG = 28,
	WW3D_TSS_CONSTANT = 32
};
enum { WW3D_TCI_PASSTHRU = 0x00000000, WW3D_TCI_CAMERASPACENORMAL = 0x00010000, WW3D_TCI_CAMERASPACEPOSITION = 0x00020000,
       WW3D_TCI_CAMERASPACEREFLECTIONVECTOR = 0x00030000, WW3D_TCI_SPHEREMAP = 0x00040000 };
enum { WW3D_TTFF_DISABLE = 0, WW3D_TTFF_COUNT1 = 1, WW3D_TTFF_COUNT2 = 2, WW3D_TTFF_COUNT3 = 3, WW3D_TTFF_COUNT4 = 4, WW3D_TTFF_PROJECTED = 256 };

enum {
	WW3D_TOP_DISABLE = 1, WW3D_TOP_SELECTARG1 = 2, WW3D_TOP_SELECTARG2 = 3,
	WW3D_TOP_MODULATE = 4, WW3D_TOP_MODULATE2X = 5, WW3D_TOP_MODULATE4X = 6,
	WW3D_TOP_ADD = 7, WW3D_TOP_ADDSIGNED = 8, WW3D_TOP_ADDSIGNED2X = 9, WW3D_TOP_SUBTRACT = 10, WW3D_TOP_ADDSMOOTH = 11,
	WW3D_TOP_BLENDDIFFUSEALPHA = 12, WW3D_TOP_BLENDTEXTUREALPHA = 13, WW3D_TOP_BLENDFACTORALPHA = 14,
	WW3D_TOP_BLENDTEXTUREALPHAPM = 15, WW3D_TOP_BLENDCURRENTALPHA = 16,
	WW3D_TOP_PREMODULATE = 17, WW3D_TOP_MODULATEALPHA_ADDCOLOR = 18, WW3D_TOP_MODULATECOLOR_ADDALPHA = 19,
	WW3D_TOP_MODULATEINVALPHA_ADDCOLOR = 20, WW3D_TOP_MODULATEINVCOLOR_ADDALPHA = 21,
	WW3D_TOP_BUMPENVMAP = 22, WW3D_TOP_BUMPENVMAPLUMINANCE = 23, WW3D_TOP_DOTPRODUCT3 = 24,
	WW3D_TOP_MULTIPLYADD = 25, WW3D_TOP_LERP = 26
};
enum { WW3D_TA_DIFFUSE = 0, WW3D_TA_CURRENT = 1, WW3D_TA_TEXTURE = 2, WW3D_TA_TFACTOR = 3, WW3D_TA_SPECULAR = 4,
       WW3D_TA_TEMP = 5, WW3D_TA_CONSTANT = 6, WW3D_TA_COMPLEMENT = 0x10, WW3D_TA_ALPHAREPLICATE = 0x20 };

enum { WW3D_SAMP_ADDRESSU = 1, WW3D_SAMP_ADDRESSV = 2, WW3D_SAMP_ADDRESSW = 3, WW3D_SAMP_BORDERCOLOR = 4,
       WW3D_SAMP_MAGFILTER = 5, WW3D_SAMP_MINFILTER = 6, WW3D_SAMP_MIPFILTER = 7, WW3D_SAMP_MIPMAPLODBIAS = 8,
       WW3D_SAMP_MAXMIPLEVEL = 9, WW3D_SAMP_MAXANISOTROPY = 10 };
enum { WW3D_TEXF_NONE = 0, WW3D_TEXF_POINT = 1, WW3D_TEXF_LINEAR = 2, WW3D_TEXF_ANISOTROPIC = 3 };
enum { WW3D_TADDRESS_WRAP = 1, WW3D_TADDRESS_MIRROR = 2, WW3D_TADDRESS_CLAMP = 3, WW3D_TADDRESS_BORDER = 4, WW3D_TADDRESS_MIRRORONCE = 5 };

// Texture combiner capabilities (RenderCaps::Get_Texture_Op_Caps) and other caps bits
enum {
	WW3D_TEXOPCAPS_DISABLE = 0x1, WW3D_TEXOPCAPS_SELECTARG1 = 0x2, WW3D_TEXOPCAPS_SELECTARG2 = 0x4, WW3D_TEXOPCAPS_MODULATE = 0x8,
	WW3D_TEXOPCAPS_MODULATE2X = 0x10, WW3D_TEXOPCAPS_MODULATE4X = 0x20, WW3D_TEXOPCAPS_ADD = 0x40, WW3D_TEXOPCAPS_ADDSIGNED = 0x80,
	WW3D_TEXOPCAPS_ADDSIGNED2X = 0x100, WW3D_TEXOPCAPS_SUBTRACT = 0x200, WW3D_TEXOPCAPS_ADDSMOOTH = 0x400,
	WW3D_TEXOPCAPS_BLENDDIFFUSEALPHA = 0x800, WW3D_TEXOPCAPS_BLENDTEXTUREALPHA = 0x1000, WW3D_TEXOPCAPS_BLENDFACTORALPHA = 0x2000,
	WW3D_TEXOPCAPS_BLENDTEXTUREALPHAPM = 0x4000, WW3D_TEXOPCAPS_BLENDCURRENTALPHA = 0x8000, WW3D_TEXOPCAPS_PREMODULATE = 0x10000,
	WW3D_TEXOPCAPS_MODULATEALPHA_ADDCOLOR = 0x20000, WW3D_TEXOPCAPS_MODULATECOLOR_ADDALPHA = 0x40000,
	WW3D_TEXOPCAPS_MODULATEINVALPHA_ADDCOLOR = 0x80000, WW3D_TEXOPCAPS_MODULATEINVCOLOR_ADDALPHA = 0x100000,
	WW3D_TEXOPCAPS_BUMPENVMAP = 0x200000, WW3D_TEXOPCAPS_BUMPENVMAPLUMINANCE = 0x400000, WW3D_TEXOPCAPS_DOTPRODUCT3 = 0x800000,
	WW3D_TEXOPCAPS_MULTIPLYADD = 0x1000000, WW3D_TEXOPCAPS_LERP = 0x2000000
};
enum { WW3D_PMISCCAPS_COLORWRITEENABLE = 0x80 };
// Raster and filter capabilities (values as D3DPRASTERCAPS_*/D3DPTFILTERCAPS_*); RenderCaps reports
// them as fixed values.
enum { WW3D_PRASTERCAPS_FOGRANGE = 0x00010000, WW3D_PRASTERCAPS_DEPTHBIAS = 0x04000000 };
enum {
	WW3D_PTFILTERCAPS_MINFPOINT = 0x00000100, WW3D_PTFILTERCAPS_MINFLINEAR = 0x00000200, WW3D_PTFILTERCAPS_MINFANISOTROPIC = 0x00000400,
	WW3D_PTFILTERCAPS_MIPFPOINT = 0x00010000, WW3D_PTFILTERCAPS_MIPFLINEAR = 0x00020000,
	WW3D_PTFILTERCAPS_MAGFPOINT = 0x01000000, WW3D_PTFILTERCAPS_MAGFLINEAR = 0x02000000, WW3D_PTFILTERCAPS_MAGFANISOTROPIC = 0x04000000
};
enum { WW3D_CAPS2_FULLSCREENGAMMA = 0x20000 };

// --- Transforms, primitives, lights -------------------------------------------------------------
enum { WW3D_TS_VIEW = 2, WW3D_TS_PROJECTION = 3,
       WW3D_TS_TEXTURE0 = 16, WW3D_TS_TEXTURE1 = 17, WW3D_TS_TEXTURE2 = 18, WW3D_TS_TEXTURE3 = 19,
       WW3D_TS_TEXTURE4 = 20, WW3D_TS_TEXTURE5 = 21, WW3D_TS_TEXTURE6 = 22, WW3D_TS_TEXTURE7 = 23,
       WW3D_TS_WORLD = 256 };
enum { WW3D_PT_POINTLIST = 1, WW3D_PT_LINELIST = 2, WW3D_PT_LINESTRIP = 3, WW3D_PT_TRIANGLELIST = 4, WW3D_PT_TRIANGLESTRIP = 5, WW3D_PT_TRIANGLEFAN = 6 };
enum { WW3D_LIGHT_POINT = 1, WW3D_LIGHT_SPOT = 2, WW3D_LIGHT_DIRECTIONAL = 3 };

// --- Resources ----------------------------------------------------------------------------------
enum { WW3D_LOCK_READONLY = 0x10, WW3D_LOCK_NOSYSLOCK = 0x800, WW3D_LOCK_NOOVERWRITE = 0x1000, WW3D_LOCK_DISCARD = 0x2000, WW3D_LOCK_NO_DIRTY_UPDATE = 0x8000 };
enum { WW3D_USAGE_RENDERTARGET = 0x1, WW3D_USAGE_DEPTHSTENCIL = 0x2, WW3D_USAGE_WRITEONLY = 0x8,
       WW3D_USAGE_SOFTWAREPROCESSING = 0x10, WW3D_USAGE_NPATCHES = 0x100, WW3D_USAGE_DYNAMIC = 0x200,
       WW3D_USAGE_POINTS = 0x40 };	///< point sprites

enum { WW3D_POOL_DEFAULT = 0, WW3D_POOL_MANAGED = 1, WW3D_POOL_SYSTEMMEM = 2, WW3D_POOL_SCRATCH = 3 };
enum { WW3D_CURSOR_IMMEDIATE_UPDATE = 1 };

// Device formats (subset; the engine's own enum is WW3DFormat in ww3dformat.h)
enum {
	WW3D_FMT_UNKNOWN = 0,
	WW3D_FMT_R8G8B8 = 20, WW3D_FMT_A8R8G8B8 = 21, WW3D_FMT_X8R8G8B8 = 22, WW3D_FMT_R5G6B5 = 23, WW3D_FMT_X1R5G5B5 = 24,
	WW3D_FMT_A1R5G5B5 = 25, WW3D_FMT_A4R4G4B4 = 26, WW3D_FMT_R3G3B2 = 27, WW3D_FMT_A8 = 28, WW3D_FMT_A8R3G3B2 = 29,
	WW3D_FMT_X4R4G4B4 = 30, WW3D_FMT_A2B10G10R10 = 31, WW3D_FMT_A8B8G8R8 = 32, WW3D_FMT_X8B8G8R8 = 33,
	WW3D_FMT_A2R10G10B10 = 35, WW3D_FMT_L16 = 81,
	WW3D_FMT_A8P8 = 40, WW3D_FMT_P8 = 41, WW3D_FMT_L8 = 50, WW3D_FMT_A8L8 = 51, WW3D_FMT_A4L4 = 52,
	WW3D_FMT_V8U8 = 60, WW3D_FMT_L6V5U5 = 61, WW3D_FMT_X8L8V8U8 = 62, WW3D_FMT_Q8W8V8U8 = 63, WW3D_FMT_V16U16 = 64,
	WW3D_FMT_D16_LOCKABLE = 70, WW3D_FMT_D32 = 71, WW3D_FMT_D15S1 = 73, WW3D_FMT_D24S8 = 75, WW3D_FMT_D24X8 = 77,
	WW3D_FMT_D24X4S4 = 79, WW3D_FMT_D16 = 80,
	WW3D_FMT_INDEX16 = 101, WW3D_FMT_INDEX32 = 102,
	WW3D_FMT_DXT1 = 0x31545844, WW3D_FMT_DXT2 = 0x32545844, WW3D_FMT_DXT3 = 0x33545844, WW3D_FMT_DXT4 = 0x34545844, WW3D_FMT_DXT5 = 0x35545844,
	WW3D_FMT_UYVY = 0x59565955, WW3D_FMT_YUY2 = 0x32595559,
	// BC7 has no D3D9 format code; own FourCC "BC7 ", mapped to DXGI_FORMAT_BC7_UNORM by the backend.
	WW3D_FMT_BC7 = 0x20374342
};

// --- Vertex formats (FVF) and declarations -----------------------------------------------------
enum {
	WW3D_FVF_XYZ = 0x002, WW3D_FVF_XYZRHW = 0x004, WW3D_FVF_XYZB1 = 0x006, WW3D_FVF_XYZB2 = 0x008, WW3D_FVF_XYZB3 = 0x00a,
	WW3D_FVF_XYZB4 = 0x00c, WW3D_FVF_XYZB5 = 0x00e, WW3D_FVF_XYZW = 0x4002, WW3D_FVF_POSITION_MASK = 0x400E,
	WW3D_FVF_NORMAL = 0x010, WW3D_FVF_PSIZE = 0x020, WW3D_FVF_DIFFUSE = 0x040, WW3D_FVF_SPECULAR = 0x080,
	WW3D_FVF_TEX0 = 0x000, WW3D_FVF_TEX1 = 0x100, WW3D_FVF_TEX2 = 0x200, WW3D_FVF_TEX3 = 0x300, WW3D_FVF_TEX4 = 0x400,
	WW3D_FVF_TEX5 = 0x500, WW3D_FVF_TEX6 = 0x600, WW3D_FVF_TEX7 = 0x700, WW3D_FVF_TEX8 = 0x800,
	WW3D_FVF_TEXCOUNT_MASK = 0xf00, WW3D_FVF_TEXCOUNT_SHIFT = 8,
	WW3D_FVF_LASTBETA_UBYTE4 = 0x1000, WW3D_FVF_LASTBETA_D3DCOLOR = 0x8000
};
#define WW3D_FVF_TEXCOORDSIZE1(i)	(3 << ((i)*2+16))
#define WW3D_FVF_TEXCOORDSIZE2(i)	(0)
#define WW3D_FVF_TEXCOORDSIZE3(i)	(1 << ((i)*2+16))
#define WW3D_FVF_TEXCOORDSIZE4(i)	(2 << ((i)*2+16))
const unsigned WW3D_MAX_TEXCOORD = 8;

enum { WW3D_DECLTYPE_FLOAT1 = 0, WW3D_DECLTYPE_FLOAT2 = 1, WW3D_DECLTYPE_FLOAT3 = 2, WW3D_DECLTYPE_FLOAT4 = 3,
       WW3D_DECLTYPE_D3DCOLOR = 4, WW3D_DECLTYPE_UBYTE4 = 5, WW3D_DECLTYPE_SHORT2 = 6, WW3D_DECLTYPE_SHORT4 = 7,
       WW3D_DECLTYPE_UBYTE4N = 8, WW3D_DECLTYPE_UNUSED = 17 };
enum { WW3D_DECLMETHOD_DEFAULT = 0 };
enum { WW3D_DECLUSAGE_POSITION = 0, WW3D_DECLUSAGE_BLENDWEIGHT = 1, WW3D_DECLUSAGE_BLENDINDICES = 2, WW3D_DECLUSAGE_NORMAL = 3,
       WW3D_DECLUSAGE_PSIZE = 4, WW3D_DECLUSAGE_TEXCOORD = 5, WW3D_DECLUSAGE_TANGENT = 6, WW3D_DECLUSAGE_BINORMAL = 7,
       WW3D_DECLUSAGE_TESSFACTOR = 8, WW3D_DECLUSAGE_POSITIONT = 9, WW3D_DECLUSAGE_COLOR = 10 };

// --- Return values ----------------------------------------------------------------------------
enum { WW3D_OK = 0 };

// --- Device error codes --------------------------------------------------------------------------
// Values as in D3D9 (MAKE_D3DHRESULT); the D3D11 backend reports device loss with these codes.
enum {
	WW3D_ERR_DEVICELOST				= static_cast<int>(0x88760868),
	WW3D_ERR_DEVICENOTRESET			= static_cast<int>(0x88760869),
	WW3D_ERR_NOTAVAILABLE			= static_cast<int>(0x8876086A),
	WW3D_ERR_INVALIDCALL			= static_cast<int>(0x8876086C),
	WW3D_ERR_DRIVERINTERNALERROR	= static_cast<int>(0x88760827),
	WW3D_ERR_OUTOFVIDEOMEMORY		= static_cast<int>(0x8876017C),
	WW3D_ERR_WASSTILLDRAWING		= static_cast<int>(0x8876021C)
};

/// Maximum vertex declaration length including the end marker (like MAXD3DDECLLENGTH).
const unsigned WW3D_MAX_DECL_LENGTH = 64;

#endif // WW3DTYPES_H
