/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// renderobjects.h --------------------------------------------------------------------------------
//
// The object structures behind the opaque handles from ww3dtypes.h. A "texture" consists of several
// device objects (resource, shader view, render target view, staging copy for locking), so each handle
// points to a struct that bundles them and counts references. The device objects come from the device
// layer (gpudevice.h).
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef RENDEROBJECTS_H
#define RENDEROBJECTS_H

#include "always.h"
#include "gpudevice.h"
// windows.h/winnt.h defines its own "BitTest" macro, which overrides the one from
// Lib/BaseType.h when included after it.
#undef BitTest
#define BitTest( x, i ) ( ( (x) & (i) ) != 0 )
#include "ww3dtypes.h"
#include "ww3dformat.h"

/// Releases a device layer object and nulls the pointer.
template <class T> inline void Gpu_Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

// --- Object structures behind the handles -------------------------------------------------------

/// 2D texture. `gpu` is the actual resource, `srv` the sampling view, `rtv` only for render
/// targets. `staging` is created on the first lock (Lock_Texture) and kept; `Unlock` copies it
/// into `gpu`. Dynamic textures (video) are mapped directly.
struct WW3DTextureObject
{
	GpuTexture2D*			gpu;
	GpuShaderResourceView*	srv;
	GpuRenderTargetView*		rtv;
	GpuTexture2D*			staging;
	GPU_FORMAT					gpu_format;
	unsigned					width;
	unsigned					height;
	unsigned					levels;
	unsigned					usage;			///< WW3D_USAGE_*
	unsigned					pool;			///< WW3D_POOL_* (bookkeeping only)
	bool						dynamic;
	// Formats without a DXGI equivalent on feature level 11.0 (A4R4G4B4, X1R5G5B5, L8, A8L8,
	// R8G8B8) are stored on the GPU as B8G8R8A8. Game code still writes in the original layout
	// (fonts use A4R4G4B4), so the texture keeps its logical format and provides a staging
	// buffer in exactly that layout while locked.
	unsigned					source_format;	///< WW3D_FMT_* as requested by the caller
	bool						converts;		///< logical format != GPU format
	// One staging buffer per mip level: the texture loader locks all
	// levels at once and writes the smaller ones from the larger ones.
	enum { MAX_CPU_LEVELS = 16 };
	unsigned char*				cpu[MAX_CPU_LEVELS];		///< staging buffer in the logical format
	unsigned					cpu_pitch[MAX_CPU_LEVELS];
	// Block formats (DXT/BC): if Map() returns a wider pitch than the block row, callers still
	// write tightly packed rows. They get the buffer above instead, and Unlock_Texture copies
	// row by row into the mapped memory.
	void*						block_mapped[MAX_CPU_LEVELS];		///< mapped memory while redirected
	unsigned					block_mapped_pitch[MAX_CPU_LEVELS];
	unsigned					staging_mapped;	///< bit mask of staging levels currently mapped
	unsigned					staging_dirty;	///< bit mask of written levels not yet uploaded
	// Levels written repeatedly (radar: SurfaceClass::DrawPixel locks per pixel) keep their
	// contents as a shadow copy in cpu[] and upload only the locked rectangle via
	// UpdateSubresource on unlock. Mapping the staging copy would stall until the GPU had
	// finished the previous copy from it.
	unsigned					shadow_levels;	///< bit mask of levels with a shadow copy
	unsigned					shadow_write;	///< bit mask of shadow levels locked for writing
	RECT						shadow_rect[MAX_CPU_LEVELS];	///< written rectangle per level
	unsigned char				write_locks[MAX_CPU_LEVELS];	///< write locks through the staging copy
	unsigned					team_color;		///< 0xRRGGBB of a team colour view (Create_Team_Color_View), else 0
	int							refcount;
};

/// One mip level of a texture or a standalone system memory surface. With `owner != NULL`
/// the memory belongs to the texture.
struct WW3DSurfaceObject
{
	WW3DTextureObject*			owner;			///< texture the level belongs to (or NULL)
	unsigned					level;
	GpuTexture2D*			standalone;		///< own staging resource if owner == NULL
	// Standalone surfaces in uncompressed formats live in main memory instead of a staging
	// resource: callers such as W3DShroud keep the locked pointer after unlocking, while a
	// pointer from Map() on a staging resource is only valid until Unmap().
	unsigned char*				cpu;			///< pixels in main memory (else NULL)
	unsigned					pitch;
	unsigned					cpu_format;		///< logical WW3D_FMT_* of the main memory surface
	// Block formats: tightly packed buffer while locked (as WW3DTextureObject::block_mapped)
	unsigned char*				block_cpu;
	void*						block_mapped;
	unsigned					block_mapped_pitch;
	GpuTexture2D*			depth;			///< depth buffer resource (render target use)
	GpuDepthStencilView*		dsv;
	GpuRenderTargetView*		rtv;			///< render target view of this level
	GPU_FORMAT					gpu_format;
	unsigned					width;
	unsigned					height;
	int							refcount;
};

/// Vertex or index buffer. Static buffers are filled through a CPU staging buffer (Lock returns
/// it, Unlock calls UpdateSubresource), dynamic ones via Map with DISCARD/NO_OVERWRITE -- the
/// pattern of DynamicVBAccessClass/DynamicIBAccessClass.
struct WW3DBufferCommon
{
	GpuBuffer*				gpu;
	unsigned char*				cpu;			///< staging buffer of static buffers (else NULL)
	unsigned					size;
	unsigned					lock_offset;
	unsigned					lock_size;
	bool						dynamic;
	bool						mapped;			///< dynamic buffer is currently mapped
	bool						mapped_discard;	///< the current mapping discarded the old content
	unsigned char*				mapped_data;	///< start of the current mapping (upload diagnosis)
	int							refcount;
	// Shadow casters of a frame are drawn into the shadow map only at the end of the opaque
	// part. If a buffer is overwritten before that (DISCARD, static update) or released, the
	// backend saves the used range into a copy.
	unsigned					shadow_frame;	///< recording in which the buffer was used (0 = none)
	unsigned					shadow_lo;		///< used range in bytes [lo, hi)
	unsigned					shadow_hi;
};
// Vertex and index buffers are separate types (ww3dtypes.h forward-declares them separately)
// but have the same contents.
struct WW3DVertexBufferObject : public WW3DBufferCommon {};
struct WW3DIndexBufferObject  : public WW3DBufferCommon {};

struct WW3DVertexShaderObject
{
	GpuVertexShader*			shader;
	GpuBlob*					bytecode;		///< for the input layouts
	int							refcount;
	unsigned					shadow_kind;	///< 1 = Trees.vs.hlsl (shadow caster with wind sway), 2 = instanced shadow caster
};

struct WW3DPixelShaderObject
{
	GpuPixelShader*			shader;
	int							refcount;
	// Terrain shaders (terrain*.ps, roadnoise2.ps) keep their source so the TERRAIN_DETAIL
	// permutation can be compiled on demand.
	char*						detail_source;
	unsigned					detail_source_len;
	char						detail_name[64];
	WW3DPixelShaderObject*		detail_variant;
	bool						detail_failed;
	// TERRAIN_ROCK permutation, [0] without, [1] with TERRAIN_DETAIL
	WW3DPixelShaderObject*		rock_variant[2];
	bool						rock_failed[2];
	// TERRAIN_NORMAL permutation, index bit 0 = with TERRAIN_DETAIL, bit 1 = with TERRAIN_ROCK
	WW3DPixelShaderObject*		normal_variant[4];
	bool						normal_failed[4];
	// TERRAIN_LIGHT permutation, index bit 0 = with TERRAIN_DETAIL, bit 1 = with TERRAIN_ROCK, bit 2 = with TERRAIN_NORMAL
	WW3DPixelShaderObject*		light_variant[8];
	bool						light_failed[8];
};

/// Vertex declaration. The input layout depends on the shader bytecode, so one is created per
/// vertex shader and cached here.
enum { WW3D_MAX_INPUT_LAYOUTS = 16 };
struct WW3DVertexDeclObject
{
	WW3DVertexElement			elements[WW3D_MAX_DECL_LENGTH];
	unsigned					element_count;
	struct LayoutEntry {
		const void*				vs_bytecode;	///< key: the vertex shader blob
		GpuInputLayout*		layout;
	}							layouts[WW3D_MAX_INPUT_LAYOUTS];
	unsigned					layout_count;
	int							refcount;
};

// Additional swap chains are not implemented (Create_Additional_Swap_Chain returns NULL).
struct WW3DSwapChainObject
{
	int							refcount;
};

#endif // RENDEROBJECTS_H
