/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// rendercore_resources.cpp -- Direct3D 11 backend: textures, buffers, locking, surfaces, render targets, cursor, caps.

#include "renderobjects.h"
#include "rendercore.h"
#include "renderstates.h"
#include "renderformat.h"
#include "rendercb.h"
#include "renderdevice.h"
#include "rendercaps.h"
#include "wwdebug.h"
#include "wwstring.h"
#include "ffactory.h"
#include "WWFILE.H"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "rendercore_internal.h"

// ==================================================================================================
// 5. Resources
// ==================================================================================================

static unsigned Block_Row_Size(GPU_FORMAT format, unsigned width);

/// Upload diagnosis: initial data with the fill byte for every level of a new texture; nullptr without fill
/// or for a format of unknown size.
static const GPU_SUBRESOURCE_DATA* Fill_Texture_Data(const GPU_TEXTURE2D_DESC& td, std::vector<unsigned char>& bytes,
													 std::vector<GPU_SUBRESOURCE_DATA>& levels)
{
	if (Ud_Fill() < 0) return nullptr;
	unsigned count = td.MipLevels;
	if (count == 0) {
		count = 1;
		for (unsigned s = std::max(td.Width, td.Height); s > 1; s >>= 1) ++count;
	}
	const unsigned block = Block_Row_Size(td.Format, td.Width);
	const unsigned bpp = block ? 0 : Get_Bytes_Per_Pixel(Device_Format_To_WW3DFormat(Gpu_Format_To_Device_Format(td.Format)));
	if (!block && !bpp) return nullptr;
	const unsigned rows = block ? (td.Height + 3) / 4 : td.Height;
	bytes.assign(static_cast<size_t>(block ? block : td.Width * bpp) * rows, static_cast<unsigned char>(Ud_Fill()));
	levels.resize(count);
	for (unsigned l = 0; l < count; ++l) {
		const unsigned w = std::max(td.Width >> l, 1u);
		levels[l].pSysMem          = bytes.data();
		levels[l].SysMemPitch      = block ? Block_Row_Size(td.Format, w) : w * bpp;
		levels[l].SysMemSlicePitch = 0;
	}
	return levels.data();
}

HRESULT RenderCore::Create_Texture(unsigned width, unsigned height, unsigned levels, unsigned usage,
									 unsigned format, unsigned pool, WW3DTextureHandle* out)
{
	if (!out) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	*out = nullptr;
	if (!M->device) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	const GPU_FORMAT dxgi = Device_Format_To_Gpu_Format(format);
	const bool render_target = (usage & WW3D_USAGE_RENDERTARGET) != 0;
	const bool dynamic       = (usage & WW3D_USAGE_DYNAMIC) != 0;

	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width            = width;
	td.Height           = height;
	td.MipLevels        = levels;
	td.ArraySize        = 1;
	td.Format           = dxgi;
	td.SampleDesc.Count = 1;
	td.Usage            = dynamic ? GPU_USAGE_DYNAMIC : GPU_USAGE_DEFAULT;
	td.BindFlags        = GPU_BIND_SHADER_RESOURCE | (render_target ? GPU_BIND_RENDER_TARGET : 0);
	td.CPUAccessFlags   = dynamic ? GPU_CPU_ACCESS_WRITE : 0;
	if (dynamic) td.MipLevels = 1;		// dynamic textures have a single level

	WW3DTextureObject* obj = W3DNEW WW3DTextureObject;
	::ZeroMemory(obj, sizeof(*obj));
	std::vector<unsigned char> fill;
	std::vector<GPU_SUBRESOURCE_DATA> fill_levels;
	HRESULT hr = M->device->CreateTexture2D(&td, render_target ? nullptr : Fill_Texture_Data(td, fill, fill_levels), &obj->gpu);
	if (FAILED(hr)) {
		WWDEBUG_SAY(("Render: CreateTexture2D %ux%u format %u failed (0x%08X)\n", width, height, format, (unsigned)hr));
		delete obj;
		return hr;
	}
	M->device->CreateShaderResourceView(obj->gpu, nullptr, &obj->srv);
	if (render_target) M->device->CreateRenderTargetView(obj->gpu, nullptr, &obj->rtv);

	// With levels = 0 (full mip chain, MIP_LEVELS_ALL) D3D11 creates all levels but leaves 0 in
	// the description. Read it back so Get_Texture_Level_Count reports the real count; otherwise
	// TexUtil::Generate_Mip_Levels would treat the texture as single-level and the smaller levels
	// of recoloured textures (team colours, Recolor_Texture_One_Time) would stay black.
	obj->gpu->GetDesc(&td);

	obj->gpu_format   = dxgi;
	obj->width         = width;
	obj->height        = height;
	obj->levels        = td.MipLevels;
	obj->usage         = usage;
	obj->pool          = pool;
	obj->dynamic       = dynamic;
	obj->source_format = format;
	// Formats without a DXGI equivalent are stored as B8G8R8A8 and converted on upload
	// (Convert_Image_To_BGRA).
	obj->converts      = Render_Format_Needs_Conversion(Device_Format_To_WW3DFormat(format));
	obj->refcount      = 1;
	Ud_Created(obj);
	*out = obj;
	number_of_API_calls++;
	return static_cast<HRESULT>(WW3D_OK);
}

template <class T>
static HRESULT Create_D3D11_Buffer(GpuDevice* device, unsigned length, unsigned usage, bool index_buffer,
								   T** out)
{
	*out = nullptr;
	if (!device || length == 0) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	const bool dynamic = (usage & WW3D_USAGE_DYNAMIC) != 0;

	GPU_BUFFER_DESC bd;
	::ZeroMemory(&bd, sizeof(bd));
	bd.ByteWidth      = length;
	bd.Usage          = dynamic ? GPU_USAGE_DYNAMIC : GPU_USAGE_DEFAULT;
	bd.BindFlags      = index_buffer ? GPU_BIND_INDEX_BUFFER : GPU_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = dynamic ? GPU_CPU_ACCESS_WRITE : 0;

	T* obj = W3DNEW T;
	::ZeroMemory(obj, sizeof(*obj));
	if (!dynamic) obj->cpu = W3DNEWARRAY unsigned char[length];	// staging buffer for static locks
	// Upload diagnosis: preset the staging buffer and the GPU buffer, which are otherwise left unwritten.
	std::vector<unsigned char> fill;
	GPU_SUBRESOURCE_DATA init;
	::ZeroMemory(&init, sizeof(init));
	if (Ud_Fill() >= 0) {
		if (obj->cpu) ::memset(obj->cpu, Ud_Fill(), length);
		else fill.assign(length, static_cast<unsigned char>(Ud_Fill()));
		init.pSysMem = obj->cpu ? obj->cpu : fill.data();
	}
	HRESULT hr = device->CreateBuffer(&bd, init.pSysMem ? &init : nullptr, &obj->gpu);
	if (FAILED(hr)) {
		delete[] obj->cpu;
		delete obj;
		return hr;
	}
	obj->size     = length;
	obj->dynamic  = dynamic;
	obj->refcount = 1;
	Ud_Created(obj);
	*out = obj;
	number_of_API_calls++;
	return static_cast<HRESULT>(WW3D_OK);
}

HRESULT RenderCore::Create_Vertex_Buffer(unsigned length, unsigned usage, unsigned, unsigned, WW3DVertexBufferHandle* out)
{
	return Create_D3D11_Buffer(M->device, length, usage, false, out);
}

HRESULT RenderCore::Create_Index_Buffer(unsigned length, unsigned usage, unsigned, unsigned, WW3DIndexBufferHandle* out)
{
	return Create_D3D11_Buffer(M->device, length, usage, true, out);
}

WW3DSurfaceHandle RenderCore::Create_Offscreen_Surface(unsigned width, unsigned height, unsigned device_format)
{
	if (!M->device) return nullptr;

	// Uncompressed formats get a system-memory buffer whose pointer stays valid after Unlock():
	// W3DShroud relies on that (it keeps pBits and writes into it for the whole game). A D3D11
	// staging resource cannot do that. Compressed formats (DXT) use the staging path; no caller
	// holds on to a pointer there.
	const WW3DFormat ww3d_format = Device_Format_To_WW3DFormat(device_format);
	const unsigned bpp = Get_Bytes_Per_Pixel(ww3d_format);
	if (bpp > 0 && width > 0 && height > 0) {
		WW3DSurfaceObject* cpu_obj = W3DNEW WW3DSurfaceObject;
		::ZeroMemory(cpu_obj, sizeof(*cpu_obj));
		cpu_obj->pitch       = width * bpp;
		cpu_obj->cpu         = W3DNEWARRAY unsigned char[cpu_obj->pitch * height];
		::memset(cpu_obj->cpu, 0, cpu_obj->pitch * height);
		cpu_obj->gpu_format = Device_Format_To_Gpu_Format(device_format);
		cpu_obj->cpu_format  = device_format;	// Get_Surface_Desc reports the logical format
		cpu_obj->width       = width;
		cpu_obj->height      = height;
		cpu_obj->refcount    = 1;
		return cpu_obj;
	}

	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width            = width;
	td.Height           = height;
	td.MipLevels        = 1;
	td.ArraySize        = 1;
	td.Format           = Device_Format_To_Gpu_Format(device_format);
	td.SampleDesc.Count = 1;
	td.Usage            = GPU_USAGE_STAGING;
	td.CPUAccessFlags   = GPU_CPU_ACCESS_READ | GPU_CPU_ACCESS_WRITE;

	WW3DSurfaceObject* obj = W3DNEW WW3DSurfaceObject;
	::ZeroMemory(obj, sizeof(*obj));
	std::vector<unsigned char> fill;
	std::vector<GPU_SUBRESOURCE_DATA> fill_levels;
	if (FAILED(M->device->CreateTexture2D(&td, Fill_Texture_Data(td, fill, fill_levels), &obj->standalone))) {
		delete obj;
		return nullptr;
	}
	obj->gpu_format = td.Format;
	obj->width       = width;
	obj->height      = height;
	obj->refcount    = 1;
	Ud_Created(obj);
	return obj;
}

HRESULT RenderCore::Get_Texture_Surface_Level(WW3DTextureHandle texture, unsigned level, WW3DSurfaceHandle* out)
{
	if (!texture || !out) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	WW3DSurfaceObject* obj = W3DNEW WW3DSurfaceObject;
	::ZeroMemory(obj, sizeof(*obj));
	obj->owner       = texture;
	obj->level       = level;
	obj->gpu_format = texture->gpu_format;
	obj->width       = texture->width  >> level ? texture->width  >> level : 1;
	obj->height      = texture->height >> level ? texture->height >> level : 1;
	obj->refcount    = 1;
	++texture->refcount;
	*out = obj;
	return static_cast<HRESULT>(WW3D_OK);
}

HRESULT RenderCore::Get_Texture_Level_Desc(WW3DTextureHandle texture, unsigned level, WW3DSurfaceDesc* desc)
{
	if (!texture || !desc) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	::ZeroMemory(desc, sizeof(WW3DSurfaceDesc));
	// Report the logical format (WW3D_FMT_*, as requested in Create_Texture); callers such as
	// W3DWater::initBumpMap pass it to Get_Bytes_Per_Pixel. Lock_Texture returns the buffer in
	// exactly this format.
	desc->Format = texture->source_format;
	desc->Width  = texture->width  >> level ? texture->width  >> level : 1;
	desc->Height = texture->height >> level ? texture->height >> level : 1;
	desc->Usage  = texture->usage;
	desc->Pool   = texture->pool;
	desc->MultiSampleType = 0;
	return static_cast<HRESULT>(WW3D_OK);
}

HRESULT RenderCore::Get_Surface_Desc(WW3DSurfaceHandle surface, WW3DSurfaceDesc* desc)
{
	if (!surface || !desc) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	::ZeroMemory(desc, sizeof(WW3DSurfaceDesc));
	desc->Width  = surface->width;
	desc->Height = surface->height;
	// Levels of a converting texture (A4R4G4B4, L8, ...) and system-memory surfaces report their
	// logical format -- Lock_Surface returns the buffer in exactly that layout. Reporting the device
	// format (B8G8R8A8, 4 bytes) would make SurfaceClass::Clear or W3DAssetManager::Remap_Palette
	// write past the end of a 2-byte-per-pixel buffer.
	desc->Format = (surface->owner && surface->owner->converts)
		? surface->owner->source_format
		: (surface->cpu && surface->cpu_format) ? surface->cpu_format
		: Gpu_Format_To_Device_Format(surface->gpu_format);	// the caller creates a texture
	desc->Pool   = WW3D_POOL_DEFAULT;							// of the same format from this
	if (surface->rtv || (surface->owner && surface->owner->rtv)) desc->Usage = WW3D_USAGE_RENDERTARGET;
	else if (surface->dsv) desc->Usage = WW3D_USAGE_DEPTHSTENCIL;
	desc->MultiSampleType = 0;
	return static_cast<HRESULT>(WW3D_OK);
}

unsigned RenderCore::Get_Texture_Level_Count(WW3DTextureHandle texture)	{ return texture ? texture->levels : 0; }

// Shares the resource and view of the texture; only sampling, never locked (no staging, no CPU copies).
WW3DTextureHandle RenderCore::Create_Team_Color_View(WW3DTextureHandle texture, unsigned color)
{
	if (!texture || !texture->gpu || !texture->srv || (color & 0xFFFFFF) == 0) return nullptr;
	WW3DTextureObject* view = W3DNEW WW3DTextureObject;
	::ZeroMemory(view, sizeof(*view));
	view->gpu = texture->gpu;
	view->gpu->AddRef();
	view->srv = texture->srv;
	view->srv->AddRef();
	view->gpu_format = texture->gpu_format;
	view->width = texture->width;
	view->height = texture->height;
	view->levels = texture->levels;
	view->usage = texture->usage;
	view->pool = texture->pool;
	view->source_format = texture->source_format;
	view->converts = texture->converts;
	view->team_color = color & 0xFFFFFF;
	view->refcount = 1;
	return view;
}
unsigned RenderCore::Get_Texture_Priority(WW3DTextureHandle)				{ return 0; }	// D3D11 has no priorities
unsigned RenderCore::Set_Texture_Priority(WW3DTextureHandle, unsigned)	{ return 0; }

// Like SetLOD: the finest `lod` levels are no longer used (texture reduction for terrain and trees).
// Implemented as a new view that starts at that level.
void RenderCore::Set_Texture_LOD(WW3DTextureHandle texture, unsigned lod)
{
	if (!texture || !texture->gpu || !M->device) return;
	if (texture->levels > 0 && lod >= texture->levels) lod = texture->levels - 1;
	GPU_TEXTURE2D_DESC td;
	texture->gpu->GetDesc(&td);
	if (!(td.BindFlags & GPU_BIND_SHADER_RESOURCE)) return;
	GPU_SHADER_RESOURCE_VIEW_DESC sd;
	::ZeroMemory(&sd, sizeof(sd));
	sd.Format                    = td.Format;
	sd.ViewDimension             = GPU_SRV_DIMENSION_TEXTURE2D;
	sd.Texture2D.MostDetailedMip = lod;
	sd.Texture2D.MipLevels       = static_cast<UINT>(-1);
	GpuShaderResourceView* srv = nullptr;
	if (FAILED(M->device->CreateShaderResourceView(texture->gpu, &sd, &srv))) return;
	Gpu_Release(texture->srv);
	texture->srv = srv;
}

/// Packed size of a block row (DXT/BC) for a level of width `width`, 0 for
/// non-block formats.
static unsigned Block_Row_Size(GPU_FORMAT format, unsigned width)
{
	const unsigned blocks = (width + 3) / 4;
	switch (format) {
	case GPU_FORMAT_BC1_UNORM:	return blocks * 8;
	case GPU_FORMAT_BC2_UNORM:
	case GPU_FORMAT_BC3_UNORM:
	case GPU_FORMAT_BC7_UNORM:	return blocks * 16;
	default:					return 0;
	}
}

/// Callers such as DDSFileClass::Copy_Level_To_Surface copy compressed levels with a single
/// memcpy, i.e. with tightly packed block rows. D3D11 may return a larger row pitch from Map --
/// the AMD driver rounds small levels up to 128 bytes. Then everything from the second block row
/// on would land in the wrong place and the small mips would be corrupt (invisible under
/// RenderDoc, which hands out its own tightly packed memory on Map). So if Map() returns a
/// different pitch, the caller gets a packed staging buffer; Unlock_Texture/Unlock_Surface write
/// it back (not for read-only locks, where the memory is mapped read-only).
static void Block_Copy_Rows(unsigned char* dst, unsigned dst_pitch, const unsigned char* src,
							unsigned src_pitch, unsigned row_size, unsigned height)
{
	const unsigned rows = (height + 3) / 4;
	for (unsigned r = 0; r < rows; ++r) {
		::memcpy(dst + r * dst_pitch, src + r * src_pitch, row_size);
	}
}

/// Lock with a rectangle: pBits points to the top-left corner of the rectangle, the pitch stays
/// that of the whole level. SurfaceClass::DrawPixel and the radar textures lock single pixels
/// this way; without the offset everything would land at (0, 0) and the minimap would stay black.
static void Offset_Locked_Rect(WW3DLockedRect* out, const RECT* rect, WW3DFormat format)
{
	if (!out || !rect || !out->pBits) return;
	unsigned bytes = 0;
	int x = rect->left, y = rect->top;
	switch (format) {
	case WW3D_FORMAT_X8R8G8B8:
	case WW3D_FORMAT_A8R8G8B8:	bytes = 4; break;
	case WW3D_FORMAT_R8G8B8:	bytes = 3; break;
	case WW3D_FORMAT_A4R4G4B4:
	case WW3D_FORMAT_A1R5G5B5:
	case WW3D_FORMAT_X1R5G5B5:
	case WW3D_FORMAT_R5G6B5:
	case WW3D_FORMAT_A8L8:
	case WW3D_FORMAT_U8V8:		bytes = 2; break;
	case WW3D_FORMAT_L8:
	case WW3D_FORMAT_A8:
	case WW3D_FORMAT_P8:		bytes = 1; break;
	case WW3D_FORMAT_DXT1:		bytes = 8;  x /= 4; y /= 4; break;
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:
	case WW3D_FORMAT_BC7:		bytes = 16; x /= 4; y /= 4; break;
	default:					return;
	}
	out->pBits = static_cast<unsigned char*>(out->pBits) + y * out->Pitch + x * static_cast<int>(bytes);
}

static unsigned Level_Extent(unsigned size, unsigned level)
{
	const unsigned s = size >> level;
	return s ? s : 1;
}

/// Bytes per row as the caller writes a locked level (block rows for DXT/BC) and the row count.
static unsigned Level_Row_Bytes(const WW3DTextureObject* texture, unsigned level, unsigned* rows)
{
	const unsigned w = Level_Extent(texture->width, level);
	const unsigned h = Level_Extent(texture->height, level);
	const unsigned block = Block_Row_Size(texture->gpu_format, w);
	if (block) {
		*rows = (h + 3) / 4;
		return block;
	}
	*rows = h;
	return w * Get_Bytes_Per_Pixel(Device_Format_To_WW3DFormat(Gpu_Format_To_Device_Format(texture->gpu_format)));
}

/// Drops a level's shadow copy when the GPU resource is written some other way
/// (Copy_Rects, Update_Texture). The next lock then reads from the GPU again.
static void Drop_Texture_Shadow(WW3DTextureObject* texture, unsigned level)
{
	if (!texture || level >= WW3DTextureObject::MAX_CPU_LEVELS) return;
	const unsigned bit = 1u << level;
	if (!(texture->shadow_levels & bit)) return;
	texture->shadow_levels &= ~bit;
	texture->shadow_write  &= ~bit;
	delete[] texture->cpu[level];
	texture->cpu[level]         = nullptr;
	texture->cpu_pitch[level]   = 0;
	texture->write_locks[level] = 0;
}

HRESULT RenderCore::Lock_Surface(WW3DSurfaceHandle surface, WW3DLockedRect* out, const RECT* rect, unsigned flags)
{
	if (!surface || !out || !M->context) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	if (surface->owner) return Lock_Texture(surface->owner, surface->level, out, rect, flags);
	if (rect) {
		const HRESULT hr = Lock_Surface(surface, out, nullptr, flags);
		if (SUCCEEDED(hr))
			Offset_Locked_Rect(out, rect, Device_Format_To_WW3DFormat(Gpu_Format_To_Device_Format(surface->gpu_format)));
		return hr;
	}

	if (surface->cpu) {			// system-memory surface, the pointer stays valid
		out->Pitch = static_cast<int>(surface->pitch);
		out->pBits = surface->cpu;
		return static_cast<HRESULT>(WW3D_OK);
	}

	GPU_MAPPED_SUBRESOURCE mapped;
	const GPU_MAP map = (flags & WW3D_LOCK_READONLY) ? GPU_MAP_READ : GPU_MAP_READ_WRITE;
	HRESULT hr = M->context->Map(surface->standalone, 0, map, 0, &mapped);
	if (FAILED(hr)) return hr;

	// Block formats: packed staging buffer, see Block_Copy_Rows.
	const unsigned row_size = Block_Row_Size(surface->gpu_format, surface->width);
	if (row_size && row_size != mapped.RowPitch) {
		if (!surface->block_cpu) {
			surface->block_cpu = W3DNEWARRAY unsigned char[row_size * ((surface->height + 3) / 4)];
		}
		Block_Copy_Rows(surface->block_cpu, row_size, static_cast<const unsigned char*>(mapped.pData), mapped.RowPitch, row_size, surface->height);
		surface->block_mapped       = (flags & WW3D_LOCK_READONLY) ? nullptr : mapped.pData;
		surface->block_mapped_pitch = mapped.RowPitch;
		if (!(flags & WW3D_LOCK_READONLY)) Ud_Texture_Locked(surface, 0, surface->block_cpu, row_size);
		out->Pitch = static_cast<int>(row_size);
		out->pBits = surface->block_cpu;
		return static_cast<HRESULT>(WW3D_OK);
	}

	if (!(flags & WW3D_LOCK_READONLY)) Ud_Texture_Locked(surface, 0, mapped.pData, mapped.RowPitch);
	out->Pitch = static_cast<int>(mapped.RowPitch);
	out->pBits = mapped.pData;
	return static_cast<HRESULT>(WW3D_OK);
}

HRESULT RenderCore::Unlock_Surface(WW3DSurfaceHandle surface)
{
	if (!surface || !M->context) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	if (surface->owner) return Unlock_Texture(surface->owner, surface->level);
	if (surface->cpu) return static_cast<HRESULT>(WW3D_OK);		// nothing to unlock
	if (Ud_Tracking()) {
		const unsigned block = Block_Row_Size(surface->gpu_format, surface->width);
		const unsigned bpp = Get_Bytes_Per_Pixel(Device_Format_To_WW3DFormat(Gpu_Format_To_Device_Format(surface->gpu_format)));
		Ud_Texture_Unlocked("surface", surface, 0, block ? block : surface->width * bpp, block ? (surface->height + 3) / 4 : surface->height);
	}
	if (surface->block_mapped) {
		const unsigned row_size = Block_Row_Size(surface->gpu_format, surface->width);
		Block_Copy_Rows(static_cast<unsigned char*>(surface->block_mapped), surface->block_mapped_pitch,
						surface->block_cpu, row_size, row_size, surface->height);
		surface->block_mapped = nullptr;
	}
	M->context->Unmap(surface->standalone, 0);
	return static_cast<HRESULT>(WW3D_OK);
}

HRESULT RenderCore::Lock_Texture(WW3DTextureHandle texture, unsigned level, WW3DLockedRect* out, const RECT* rect, unsigned flags)
{
	if (!texture || !out || !M->context) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	if (level < WW3DTextureObject::MAX_CPU_LEVELS && (texture->shadow_levels & (1u << level))) {
		const unsigned bit = 1u << level;
		RECT r;
		if (rect) {
			r = *rect;
		} else {
			r.left = 0; r.top = 0;
			r.right  = static_cast<LONG>(Level_Extent(texture->width, level));
			r.bottom = static_cast<LONG>(Level_Extent(texture->height, level));
		}
		if (!(flags & WW3D_LOCK_READONLY)) {
			RECT& dirty = texture->shadow_rect[level];
			if (texture->shadow_write & bit) {
				if (r.left   < dirty.left)   dirty.left   = r.left;
				if (r.top    < dirty.top)    dirty.top    = r.top;
				if (r.right  > dirty.right)  dirty.right  = r.right;
				if (r.bottom > dirty.bottom) dirty.bottom = r.bottom;
			} else {
				dirty = r;
				texture->shadow_write |= bit;
			}
		}
		out->Pitch = static_cast<int>(texture->cpu_pitch[level]);
		out->pBits = texture->cpu[level];
		if (rect) Offset_Locked_Rect(out, rect, Device_Format_To_WW3DFormat(Gpu_Format_To_Device_Format(texture->gpu_format)));
		return static_cast<HRESULT>(WW3D_OK);
	}
	if (rect) {
		const HRESULT hr = Lock_Texture(texture, level, out, nullptr, flags);
		if (SUCCEEDED(hr)) Offset_Locked_Rect(out, rect, Device_Format_To_WW3DFormat(texture->source_format));
		return hr;
	}

	if (texture->dynamic) {
		GPU_MAPPED_SUBRESOURCE mapped;
		HRESULT hr = M->context->Map(texture->gpu, level, GPU_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr)) return hr;
		if (Ud_Fill() >= 0) {
			unsigned rows = 0;
			Level_Row_Bytes(texture, level, &rows);
			::memset(mapped.pData, Ud_Fill(), static_cast<size_t>(mapped.RowPitch) * rows);
		}
		Ud_Texture_Locked(texture, level, mapped.pData, mapped.RowPitch);
		out->Pitch = static_cast<int>(mapped.RowPitch);
		out->pBits = mapped.pData;
		return static_cast<HRESULT>(WW3D_OK);
	}

	if (texture->converts) {
		// The caller writes in the logical format (e.g. A4R4G4B4). It gets a staging buffer in
		// exactly that layout; Unlock_Texture converts and uploads it. No caller reads these
		// formats back, so the buffer is not read back from the device.
		const unsigned w = (texture->width  >> level) ? (texture->width  >> level) : 1;
		const unsigned h = (texture->height >> level) ? (texture->height >> level) : 1;
		const unsigned bpp = Get_Bytes_Per_Pixel(Device_Format_To_WW3DFormat(texture->source_format));
		if (!bpp || level >= WW3DTextureObject::MAX_CPU_LEVELS) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
		// Each level has its own buffer: a caller may lock all levels at once, so a shared
		// buffer would be freed while still being written. The buffer lives until the texture is
		// released, so locking the same level again (fonts) sees the old content.
		if (!texture->cpu[level]) {
			texture->cpu_pitch[level] = w * bpp;
			texture->cpu[level]       = W3DNEWARRAY unsigned char[texture->cpu_pitch[level] * h];
			::memset(texture->cpu[level], 0, texture->cpu_pitch[level] * h);
		}
		out->Pitch = static_cast<int>(texture->cpu_pitch[level]);
		out->pBits = texture->cpu[level];
		return static_cast<HRESULT>(WW3D_OK);
	}

	// A level written a second time gets a shadow copy (see WW3DTextureObject::shadow_levels).
	// The texture loader locks each level only once and therefore stays on the staging path;
	// so do block formats and render targets.
	if (!(flags & WW3D_LOCK_READONLY) && level < WW3DTextureObject::MAX_CPU_LEVELS && texture->staging
		&& !texture->rtv && texture->staging_mapped == 0 && texture->staging_dirty == 0 && !texture->cpu[level]
		&& Block_Row_Size(texture->gpu_format, 1) == 0 && texture->write_locks[level] >= 1) {
		const unsigned bpp = Get_Bytes_Per_Pixel(Device_Format_To_WW3DFormat(Gpu_Format_To_Device_Format(texture->gpu_format)));
		const unsigned w = Level_Extent(texture->width, level);
		const unsigned h = Level_Extent(texture->height, level);
		M->context->CopyResource(texture->staging, texture->gpu);
		GPU_MAPPED_SUBRESOURCE mapped;
		if (bpp && SUCCEEDED(M->context->Map(texture->staging, level, GPU_MAP_READ, 0, &mapped))) {
			texture->cpu_pitch[level] = w * bpp;
			texture->cpu[level]       = W3DNEWARRAY unsigned char[texture->cpu_pitch[level] * h];
			for (unsigned row = 0; row < h; ++row) {
				::memcpy(texture->cpu[level] + row * texture->cpu_pitch[level],
						 static_cast<const unsigned char*>(mapped.pData) + row * mapped.RowPitch, texture->cpu_pitch[level]);
			}
			M->context->Unmap(texture->staging, level);
			texture->shadow_levels |= (1u << level);
			return Lock_Texture(texture, level, out, nullptr, flags);
		}
	}
	if (!(flags & WW3D_LOCK_READONLY) && level < WW3DTextureObject::MAX_CPU_LEVELS && texture->write_locks[level] < 1)
		++texture->write_locks[level];

	// Static texture: create the staging copy on first lock and keep it.
	if (!texture->staging) {
		GPU_TEXTURE2D_DESC td;
		texture->gpu->GetDesc(&td);
		td.Usage          = GPU_USAGE_STAGING;
		td.BindFlags      = 0;
		td.CPUAccessFlags = GPU_CPU_ACCESS_READ | GPU_CPU_ACCESS_WRITE;
		td.MiscFlags      = 0;
		std::vector<unsigned char> fill;
		std::vector<GPU_SUBRESOURCE_DATA> fill_levels;
		if (FAILED(M->device->CreateTexture2D(&td, Fill_Texture_Data(td, fill, fill_levels), &texture->staging)))
			return static_cast<HRESULT>(WW3D_ERR_OUTOFVIDEOMEMORY);
		if (flags & WW3D_LOCK_READONLY)
			M->context->CopyResource(texture->staging, texture->gpu);
	}
	else if ((flags & WW3D_LOCK_READONLY) && texture->staging_mapped == 0 && texture->staging_dirty == 0) {
		// Only with no level mapped (debug layer: no copy into/out of mapped resources); otherwise
		// the newest data is in the staging copy anyway.
		M->context->CopyResource(texture->staging, texture->gpu);
	}

	GPU_MAPPED_SUBRESOURCE mapped;
	const GPU_MAP map = (flags & WW3D_LOCK_READONLY) ? GPU_MAP_READ : GPU_MAP_READ_WRITE;
	HRESULT hr = M->context->Map(texture->staging, level, map, 0, &mapped);
	if (FAILED(hr)) return hr;
	if (level < 32) {
		texture->staging_mapped |= (1u << level);
		if (!(flags & WW3D_LOCK_READONLY)) texture->staging_dirty |= (1u << level);
	}

	const unsigned w = (texture->width  >> level) ? (texture->width  >> level) : 1;
	const unsigned h = (texture->height >> level) ? (texture->height >> level) : 1;
	const unsigned row_size = Block_Row_Size(texture->gpu_format, w);
	if (row_size && row_size != mapped.RowPitch && level < WW3DTextureObject::MAX_CPU_LEVELS) {
		if (!texture->cpu[level]) {
			texture->cpu_pitch[level] = row_size;
			texture->cpu[level]       = W3DNEWARRAY unsigned char[row_size * ((h + 3) / 4)];
		}
		Block_Copy_Rows(texture->cpu[level], row_size, static_cast<const unsigned char*>(mapped.pData), mapped.RowPitch, row_size, h);
		texture->block_mapped[level]       = (flags & WW3D_LOCK_READONLY) ? nullptr : mapped.pData;
		texture->block_mapped_pitch[level] = mapped.RowPitch;
		out->Pitch = static_cast<int>(row_size);
		out->pBits = texture->cpu[level];
		if (!(flags & WW3D_LOCK_READONLY)) Ud_Texture_Locked(texture, level, texture->cpu[level], row_size);
		return static_cast<HRESULT>(WW3D_OK);
	}

	if (!(flags & WW3D_LOCK_READONLY)) Ud_Texture_Locked(texture, level, mapped.pData, mapped.RowPitch);
	out->Pitch = static_cast<int>(mapped.RowPitch);
	out->pBits = mapped.pData;
	return static_cast<HRESULT>(WW3D_OK);
}

HRESULT RenderCore::Unlock_Texture(WW3DTextureHandle texture, unsigned level)
{
	if (!texture || !M->context) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	if (Ud_Tracking() && !texture->converts && !(level < WW3DTextureObject::MAX_CPU_LEVELS && (texture->shadow_levels & (1u << level)))) {
		unsigned rows = 0;
		const unsigned row_bytes = Level_Row_Bytes(texture, level, &rows);
		Ud_Texture_Unlocked("texture", texture, level, row_bytes, rows);
	}
	if (texture->dynamic) {
		M->context->Unmap(texture->gpu, level);
		return static_cast<HRESULT>(WW3D_OK);
	}
	if (texture->converts) {
		if (level >= WW3DTextureObject::MAX_CPU_LEVELS || !texture->cpu[level]) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
		const unsigned w = (texture->width  >> level) ? (texture->width  >> level) : 1;
		const unsigned h = (texture->height >> level) ? (texture->height >> level) : 1;
		const unsigned dst_pitch = w * 4;
		unsigned char* converted = W3DNEWARRAY unsigned char[dst_pitch * h];
		if (Render_Convert_Image_To_BGRA(Device_Format_To_WW3DFormat(texture->source_format),
								  texture->cpu[level], texture->cpu_pitch[level], converted, dst_pitch, w, h)) {
			M->context->UpdateSubresource(texture->gpu, level, nullptr, converted, dst_pitch, 0);
			Ud_Upload_Rows("texture", texture, level, converted, dst_pitch, h, dst_pitch, false);
			number_of_API_calls++;
		}
		delete[] converted;
		return static_cast<HRESULT>(WW3D_OK);
	}
	if (level < WW3DTextureObject::MAX_CPU_LEVELS && (texture->shadow_levels & (1u << level))) {
		const unsigned bit = 1u << level;
		if (texture->shadow_write & bit) {
			texture->shadow_write &= ~bit;
			const RECT& r = texture->shadow_rect[level];
			if (r.right > r.left && r.bottom > r.top) {
				const unsigned pitch = texture->cpu_pitch[level];
				const unsigned bpp = pitch / Level_Extent(texture->width, level);
				GPU_BOX box;
				box.left = static_cast<UINT>(r.left);   box.right  = static_cast<UINT>(r.right);
				box.top  = static_cast<UINT>(r.top);    box.bottom = static_cast<UINT>(r.bottom);
				box.front = 0;             box.back   = 1;
				M->context->UpdateSubresource(texture->gpu, level, &box,
					texture->cpu[level] + r.top * pitch + r.left * bpp, pitch, 0);
				Ud_Upload_Rows("texture", texture, level, texture->cpu[level] + r.top * pitch + r.left * bpp,
							   (r.right - r.left) * bpp, r.bottom - r.top, pitch, false);
				number_of_API_calls++;
			}
		}
		return static_cast<HRESULT>(WW3D_OK);
	}
	if (!texture->staging) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	if (level < WW3DTextureObject::MAX_CPU_LEVELS && texture->block_mapped[level]) {
		const unsigned w = (texture->width  >> level) ? (texture->width  >> level) : 1;
		const unsigned h = (texture->height >> level) ? (texture->height >> level) : 1;
		const unsigned row_size = Block_Row_Size(texture->gpu_format, w);
		Block_Copy_Rows(static_cast<unsigned char*>(texture->block_mapped[level]), texture->block_mapped_pitch[level],
						texture->cpu[level], row_size, row_size, h);
		texture->block_mapped[level] = nullptr;
	}
	M->context->Unmap(texture->staging, level);
	if (level < 32) texture->staging_mapped &= ~(1u << level);

	// The texture loader locks all mip levels at once and releases them one after another. A resource
	// with a level still mapped must not be copied from (debug layer ID 283). So upload only when no
	// level is mapped any more -- then all written levels.
	if (texture->staging_mapped == 0 && texture->staging_dirty != 0) {
		for (unsigned l = 0; l < texture->levels && l < 32; ++l) {
			if (texture->staging_dirty & (1u << l))
				M->context->CopySubresourceRegion(texture->gpu, l, 0, 0, 0, texture->staging, l, nullptr);
		}
		texture->staging_dirty = 0;
	}
	return static_cast<HRESULT>(WW3D_OK);
}

static HRESULT Lock_Buffer_Object(GpuContext* context, WW3DBufferCommon* vb,
								  unsigned offset, unsigned size, void** data, unsigned flags)
{
	if (!vb || !data || !context) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	if (vb->dynamic) {
		// Exactly the ring buffer pattern of DynamicVBAccessClass: DISCARD at offset 0, else NO_OVERWRITE.
		// Vertex and index buffers may both be mapped with NO_OVERWRITE on Direct3D 11.0
		// (only constant buffers and buffer SRVs need 11.1 for that).
		const GPU_MAP map = (flags & WW3D_LOCK_DISCARD) ? GPU_MAP_WRITE_DISCARD : GPU_MAP_WRITE_NO_OVERWRITE;
		GPU_MAPPED_SUBRESOURCE mapped;
		HRESULT hr = context->Map(vb->gpu, 0, map, 0, &mapped);
		if (FAILED(hr)) return hr;
		vb->mapped = true;
		vb->mapped_discard = (map == GPU_MAP_WRITE_DISCARD);
		vb->mapped_data = static_cast<unsigned char*>(mapped.pData);
		vb->lock_offset = offset;
		vb->lock_size   = size ? size : (vb->size - offset);
		if (vb->mapped_discard && Ud_Fill() >= 0) ::memset(mapped.pData, Ud_Fill(), vb->size);
		*data = static_cast<unsigned char*>(mapped.pData) + offset;
		return static_cast<HRESULT>(WW3D_OK);
	}
	vb->lock_offset = offset;
	vb->lock_size   = size ? size : (vb->size - offset);
	*data = vb->cpu + offset;
	return static_cast<HRESULT>(WW3D_OK);
}

static HRESULT Unlock_Buffer_Object(GpuContext* context, WW3DBufferCommon* vb, const char* kind)
{
	if (!vb || !context) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	if (vb->dynamic) {
		if (vb->mapped) {
			Ud_Upload(kind, vb, vb->lock_offset, vb->mapped_data + vb->lock_offset, vb->lock_size, vb->mapped_discard);
			context->Unmap(vb->gpu, 0);
			vb->mapped = false;
		}
		return static_cast<HRESULT>(WW3D_OK);
	}
	Ud_Upload(kind, vb, vb->lock_offset, vb->cpu + vb->lock_offset, vb->lock_size, false);
	GPU_BOX box;
	box.left = vb->lock_offset; box.right = vb->lock_offset + vb->lock_size;
	box.top = 0; box.bottom = 1; box.front = 0; box.back = 1;
	context->UpdateSubresource(vb->gpu, 0, &box, vb->cpu + vb->lock_offset, 0, 0);
	return static_cast<HRESULT>(WW3D_OK);
}

HRESULT RenderCore::Lock_Vertex_Buffer(WW3DVertexBufferHandle vb, unsigned offset, unsigned size, void** data, unsigned flags)
{
	// Preserve ranges that recorded shadow casters still need.
	if (M->sh_count && vb) Sh_Before_Write(vb, offset, size, (flags & WW3D_LOCK_DISCARD) != 0, false);
	return Lock_Buffer_Object(M->context, vb, offset, size, data, flags);
}

HRESULT RenderCore::Unlock_Vertex_Buffer(WW3DVertexBufferHandle vb)
{
	return Unlock_Buffer_Object(M->context, vb, "vb");
}

HRESULT RenderCore::Lock_Index_Buffer(WW3DIndexBufferHandle ib, unsigned offset, unsigned size, void** data, unsigned flags)
{
	if (M->sh_count && ib) Sh_Before_Write(ib, offset, size, (flags & WW3D_LOCK_DISCARD) != 0, true);
	return Lock_Buffer_Object(M->context, ib, offset, size, data, flags);
}

HRESULT RenderCore::Unlock_Index_Buffer(WW3DIndexBufferHandle ib)
{
	return Unlock_Buffer_Object(M->context, ib, "ib");
}

void RenderCore::Add_Ref_Resource(WW3DTextureHandle texture)	{ if (texture) ++texture->refcount; }
void RenderCore::Add_Ref_Resource(WW3DSurfaceHandle surface)	{ if (surface) ++surface->refcount; }

void RenderCore::Release_Resource(WW3DTextureHandle texture)
{
	if (!texture || --texture->refcount > 0) return;
	for (unsigned l = 0; l < WW3DTextureObject::MAX_CPU_LEVELS; ++l) delete[] texture->cpu[l];
	Gpu_Release(texture->staging);
	Gpu_Release(texture->rtv);
	Gpu_Release(texture->srv);
	Gpu_Release(texture->gpu);
	delete texture;
}

void RenderCore::Release_Resource(WW3DSurfaceHandle surface)
{
	if (!surface || --surface->refcount > 0) return;
	if (surface->owner) Release_Resource(surface->owner);
	delete[] surface->cpu;
	delete[] surface->block_cpu;
	Gpu_Release(surface->rtv);
	Gpu_Release(surface->dsv);
	Gpu_Release(surface->depth);
	Gpu_Release(surface->standalone);
	delete surface;
}

void RenderCore::Release_Resource(WW3DVertexShaderHandle shader)
{
	if (!shader || --shader->refcount > 0) return;
	Gpu_Release(shader->bytecode);
	Gpu_Release(shader->shader);
	delete shader;
}

void RenderCore::Release_Resource(WW3DPixelShaderHandle shader)
{
	if (!shader || --shader->refcount > 0) return;
	Gpu_Release(shader->shader);
	// the source and its permutations belong to the shader
	delete[] shader->detail_source;
	if (shader->detail_variant) Release_Resource(shader->detail_variant);
	for (int i = 0; i < 2; ++i) if (shader->rock_variant[i]) Release_Resource(shader->rock_variant[i]);
	for (int i = 0; i < 4; ++i) if (shader->normal_variant[i]) Release_Resource(shader->normal_variant[i]);
	for (int i = 0; i < 8; ++i) if (shader->light_variant[i]) Release_Resource(shader->light_variant[i]);
	delete shader;
}

void RenderCore::Release_Resource(WW3DVertexDeclHandle decl)
{
	if (!decl || --decl->refcount > 0) return;
	for (unsigned i = 0; i < decl->layout_count; ++i)
		Gpu_Release(decl->layouts[i].layout);
	delete decl;
}

static void Release_Buffer_Object(WW3DBufferCommon* b)
{
	if (!b || --b->refcount > 0) return;
	Gpu_Release(b->gpu);
	delete [] b->cpu;
	delete b;
}

// A buffer that recorded shadow casters still need is preserved first.
void RenderCore::Release_Resource(WW3DVertexBufferHandle vb)
{
	if (vb && vb->refcount == 1 && M->sh_count) Sh_Before_Write(vb, 0, 0, true, false);
	Release_Buffer_Object(vb);
}

void RenderCore::Release_Resource(WW3DIndexBufferHandle ib)
{
	if (ib && ib->refcount == 1 && M->sh_count) Sh_Before_Write(ib, 0, 0, true, true);
	Release_Buffer_Object(ib);
}

/// Bytes per pixel of a surface (0 for compressed or unknown formats).
/// System-memory surfaces are in the logical format (pitch = width * bytes), but their gpu_format
/// is B8G8R8A8 for converted formats (A4R4G4B4, R8G8B8, L8 ...); using 4 instead of 2 bytes per
/// pixel would make Copy_Rects write past the end of the buffer.
static unsigned Surface_Bytes_Per_Pixel(const WW3DSurfaceObject* surface)
{
	if (surface->cpu && surface->width) return surface->pitch / surface->width;
	return Get_Bytes_Per_Pixel(Device_Format_To_WW3DFormat(Gpu_Format_To_Device_Format(surface->gpu_format)));
}

void RenderCore::Copy_Rects(WW3DSurfaceHandle source, const RECT* source_rects, unsigned num_rects,
							  WW3DSurfaceHandle dest, const POINT* dest_points)
{
	if (!source || !dest || !M->context) return;

	const unsigned count = num_rects ? num_rects : 1;
	for (unsigned i = 0; i < count; ++i) {
		GPU_BOX box;
		if (source_rects) {
			box.left = source_rects[i].left;   box.right  = source_rects[i].right;
			box.top  = source_rects[i].top;    box.bottom = source_rects[i].bottom;
		} else {
			box.left = 0; box.right = source->width; box.top = 0; box.bottom = source->height;
		}
		box.front = 0; box.back = 1;
		const unsigned x = dest_points ? dest_points[i].x : 0;
		if (dest->owner) Drop_Texture_Shadow(dest->owner, dest->level);
		const unsigned y = dest_points ? dest_points[i].y : 0;
		if (box.right <= box.left || box.bottom <= box.top) continue;

		// --- system-memory surfaces (see Create_Offscreen_Surface) -----------------------------
		if (source->cpu && dest->cpu) {
			const unsigned bpp = Surface_Bytes_Per_Pixel(source);
			if (!bpp) continue;
			const unsigned rows = box.bottom - box.top;
			const unsigned bytes = (box.right - box.left) * bpp;
			for (unsigned row = 0; row < rows; ++row) {
				::memcpy(dest->cpu   + (y + row) * dest->pitch   + x * bpp,
						 source->cpu + (box.top + row) * source->pitch + box.left * bpp,
						 bytes);
			}
			continue;
		}

		if (source->cpu) {
			// system memory -> GPU resource
			GpuResource* dst = dest->owner ? static_cast<GpuResource*>(dest->owner->gpu) : static_cast<GpuResource*>(dest->standalone);
			if (!dst) continue;
			const unsigned bpp = Surface_Bytes_Per_Pixel(source);
			if (!bpp) continue;

			GPU_BOX dest_box;
			dest_box.left   = x;
			dest_box.right  = x + (box.right - box.left);
			dest_box.top    = y;
			dest_box.bottom = y + (box.bottom - box.top);
			dest_box.front  = 0;
			dest_box.back   = 1;

			const unsigned char* rows = source->cpu + box.top * source->pitch + box.left * bpp;
			Ud_Upload_Rows("copy", dest->owner ? static_cast<const void*>(dest->owner) : static_cast<const void*>(dest), dest->level,
						   rows, (box.right - box.left) * bpp, box.bottom - box.top, source->pitch, false);

			// If the target resource is B8G8R8A8 because its logical format is missing on
			// feature level 11.0 (A4R4G4B4 for fonts), convert here.
			if (dest->owner && dest->owner->converts) {
				const unsigned w = dest_box.right - dest_box.left;
				const unsigned h = dest_box.bottom - dest_box.top;
				const unsigned dst_pitch = w * 4;
				unsigned char* converted = W3DNEWARRAY unsigned char[dst_pitch * h];
				if (Render_Convert_Image_To_BGRA(Device_Format_To_WW3DFormat(dest->owner->source_format),
										  rows, source->pitch, converted, dst_pitch, w, h)) {
					M->context->UpdateSubresource(dst, dest->level, &dest_box, converted, dst_pitch, 0);
					number_of_API_calls++;
				}
				delete[] converted;
				continue;
			}

			M->context->UpdateSubresource(dst, dest->owner ? dest->level : 0, &dest_box,
				rows, source->pitch, 0);
			number_of_API_calls++;
			continue;
		}

		if (dest->cpu) {
			// GPU resource -> system memory. Happens exactly once (MissingTexture at
			// startup), hence the simple path through a staging copy.
			GpuResource* src = source->owner ? static_cast<GpuResource*>(source->owner->gpu) : static_cast<GpuResource*>(source->standalone);
			const unsigned bpp = Surface_Bytes_Per_Pixel(dest);
			if (!src || !bpp) continue;

			// Level of a converting texture (A4R4G4B4 ...): on the GPU it is B8G8R8A8, but the caller
			// wants the logical format. The texture keeps the data in the logical format anyway
			// (cpu[level], written on load) -- copy from there (W3DAssetManager::
			// Recolor_Texture_One_Time, team colour in WorldBuilder).
			if (source->owner && source->owner->converts) {
				const WW3DTextureObject* t = source->owner;
				const unsigned lv = source->level;
				const unsigned src_bpp = Get_Bytes_Per_Pixel(Device_Format_To_WW3DFormat(t->source_format));
				if (lv >= WW3DTextureObject::MAX_CPU_LEVELS || !t->cpu[lv] || src_bpp != bpp) {
					Log_Missing("Copy_Rects from a converted texture without data in the logical format");
					continue;
				}
				const unsigned bytes = (box.right - box.left) * bpp;
				for (unsigned row = 0; row < box.bottom - box.top; ++row) {
					::memcpy(dest->cpu + (y + row) * dest->pitch + x * bpp,
							 t->cpu[lv] + (box.top + row) * t->cpu_pitch[lv] + box.left * bpp,
							 bytes);
				}
				continue;
			}
			// Same pixel size on both sides, else the row copy would overrun the target.
			if (bpp != Get_Bytes_Per_Pixel(Device_Format_To_WW3DFormat(Gpu_Format_To_Device_Format(dest->gpu_format)))) {
				Log_Missing("Copy_Rects from the GPU into a converted system memory surface");
				continue;
			}

			GPU_TEXTURE2D_DESC td;
			::ZeroMemory(&td, sizeof(td));
			td.Width            = box.right - box.left;
			td.Height           = box.bottom - box.top;
			td.MipLevels        = 1;
			td.ArraySize        = 1;
			td.Format           = dest->gpu_format;
			td.SampleDesc.Count = 1;
			td.Usage            = GPU_USAGE_STAGING;
			td.CPUAccessFlags   = GPU_CPU_ACCESS_READ;
			GpuTexture2D* staging = nullptr;
			if (FAILED(M->device->CreateTexture2D(&td, nullptr, &staging))) continue;

			M->context->CopySubresourceRegion(staging, 0, 0, 0, 0, src,
				source->owner ? source->level : 0, &box);
			GPU_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(M->context->Map(staging, 0, GPU_MAP_READ, 0, &mapped))) {
				const unsigned bytes = td.Width * bpp;
				for (unsigned row = 0; row < td.Height; ++row) {
					::memcpy(dest->cpu + (y + row) * dest->pitch + x * bpp,
							 static_cast<const unsigned char*>(mapped.pData) + row * mapped.RowPitch,
							 bytes);
				}
				M->context->Unmap(staging, 0);
			}
			staging->Release();
			number_of_API_calls++;
			continue;
		}

		// --- both sides on the GPU ------------------------------------------------------------
		GpuResource* src = source->owner ? static_cast<GpuResource*>(source->owner->gpu) : static_cast<GpuResource*>(source->standalone);
		GpuResource* dst = dest->owner   ? static_cast<GpuResource*>(dest->owner->gpu)   : static_cast<GpuResource*>(dest->standalone);
		if (!src || !dst) continue;
		M->context->CopySubresourceRegion(dst, dest->owner ? dest->level : 0, x, y, 0,
										  src, source->owner ? source->level : 0, &box);
		Ud_Copy("copy", dest->owner ? static_cast<const void*>(dest->owner) : static_cast<const void*>(dest), dest->level,
				source->owner ? static_cast<const void*>(source->owner) : static_cast<const void*>(source));
		number_of_API_calls++;
	}
}

void RenderCore::Update_Texture(WW3DTextureHandle system, WW3DTextureHandle video)
{
	if (!system || !video || !M->context) return;
	for (unsigned l = 0; l < WW3DTextureObject::MAX_CPU_LEVELS; ++l) Drop_Texture_Shadow(video, l);
	M->context->CopyResource(video->gpu, system->gpu);
	Ud_Copy("update", video, 0, system);
	number_of_API_calls++;
}

// Scene copy for water refraction. The source is the currently bound colour target: the scene
// target (resolved with multisampling) or a screen filter's texture. The target is a separate
// texture, never the bound target itself -- D3D11 cannot write and sample a resource at once.
bool RenderCore::Copy_Render_Target_To_Texture(WW3DTextureHandle dest)
{
	if (!dest || !dest->gpu || !M->context) return false;
	GpuTexture2D* source = nullptr;
	unsigned subresource = 0, width = 0, height = 0;
	GPU_FORMAT format = GPU_FORMAT_UNKNOWN;
	if (M->current_render_target) {
		WW3DSurfaceHandle rt = M->current_render_target;
		if (!rt->owner || !rt->owner->gpu) return false;
		source      = rt->owner->gpu;
		subresource = rt->level;
		width       = rt->width;
		height      = rt->height;
		format      = rt->owner->gpu_format;
	}
	else {
		// The scene target in the size the game sees (larger during a supersampled scene); a float target via
		// its 8-bit conversion (the game creates the copy in 8 bits), with multisampling via the resolve.
		source = Fx_Scene_8Bit();
		width  = M->scene_surface ? M->scene_surface->width  : M->width;
		height = M->scene_surface ? M->scene_surface->height : M->height;
		format = GPU_FORMAT_B8G8R8A8_UNORM;
	}
	if (!source || dest->width != width || dest->height != height || dest->gpu_format != format)
		return false;
	if (M->current_render_target)
		M->context->CopySubresourceRegion(dest->gpu, 0, 0, 0, 0, source, subresource, nullptr);
	else {
		GPU_BOX box;
		box.left = 0;  box.top = 0;  box.front = 0;
		box.right = width;  box.bottom = height;  box.back = 1;
		M->context->CopySubresourceRegion(dest->gpu, 0, 0, 0, 0, source, 0, &box);
	}
	Ud_Copy("scene", dest, 0, nullptr);
	number_of_API_calls++;
	return true;
}

HRESULT RenderCore::Get_Render_Target(WW3DSurfaceHandle* out)
{
	// The caller (W3DShaderManager::init) queries size and format of the current target and
	// keeps the handle to switch back to it later. So a real handle must come out here, not
	// NULL. The caller gets its own reference.
	if (!out) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	WW3DSurfaceHandle target = M->current_render_target ? M->current_render_target : M->scene_surface;
	if (target) ++target->refcount;
	*out = target;
	return target ? static_cast<HRESULT>(WW3D_OK) : static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
}

HRESULT RenderCore::Get_Depth_Stencil_Surface(WW3DSurfaceHandle* out)
{
	if (!out) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	if (M->scene_depth_surface) ++M->scene_depth_surface->refcount;
	*out = M->scene_depth_surface;
	return M->scene_depth_surface ? static_cast<HRESULT>(WW3D_OK) : static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
}

/// Sets the viewport to the whole target. D3D11 does not do this on a render target change --
/// without it a 512x512 shadow target would keep the screen viewport and almost nothing of it
/// would be visible.
void RenderCore::Reset_Viewport_To_Target(unsigned width, unsigned height)
{
	WW3DViewport vp;
	vp.X = 0;
	vp.Y = 0;
	vp.Width  = width;
	vp.Height = height;
	vp.MinZ = 0.0f;
	vp.MaxZ = 1.0f;
	Set_Viewport(&vp);
}

HRESULT RenderCore::Set_Render_Target_Surfaces(WW3DSurfaceHandle render_target, WW3DSurfaceHandle depth_stencil)
{
	if (!M->context) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	Sa_Done();
	if (render_target == nullptr || render_target == M->scene_surface) {
		M->context->OMSetRenderTargets(1, &M->scene_rtv, M->scene_dsv);
		Invalidate_Texture_Bindings(M);	// D3D11 unbinds textures that become targets
		M->current_render_target = nullptr;
		M->bound_rtv = M->scene_rtv;
		M->bound_dsv = M->scene_dsv;
		number_of_API_calls++;
		Reset_Viewport_To_Target(M->width, M->height);
		return static_cast<HRESULT>(WW3D_OK);
	}

	GpuRenderTargetView* rtv = render_target->rtv;
	if (!rtv && render_target->owner) rtv = render_target->owner->rtv;
	if (!rtv) {
		Log_Missing("Set_Render_Target_Surface without a render target view");
		return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	}

	// Depth buffer: prefer the one passed in, if it fits. D3D11 requires the same size and
	// sample count for all bound views -- a multisampled scene depth buffer is therefore unusable
	// for a texture, which is always single-sampled. Otherwise the target gets its own.
	GpuDepthStencilView* dsv = nullptr;
	if (depth_stencil && depth_stencil->dsv &&
		depth_stencil->width  == render_target->width &&
		depth_stencil->height == render_target->height &&
		!(depth_stencil == M->scene_depth_surface && M->sample_count > 1)) {
		dsv = depth_stencil->dsv;
	}
	else {
		if (!render_target->dsv) {
			GPU_TEXTURE2D_DESC dd;
			::ZeroMemory(&dd, sizeof(dd));
			dd.Width            = render_target->width;
			dd.Height           = render_target->height;
			dd.MipLevels        = 1;
			dd.ArraySize        = 1;
			dd.Format           = Scene_Depth_Format();
			dd.SampleDesc.Count = 1;
			dd.Usage            = GPU_USAGE_DEFAULT;
			dd.BindFlags        = GPU_BIND_DEPTH_STENCIL;
			if (SUCCEEDED(M->device->CreateTexture2D(&dd, nullptr, &render_target->depth)))
				M->device->CreateDepthStencilView(render_target->depth, nullptr, &render_target->dsv);
		}
		dsv = render_target->dsv;
	}

	M->context->OMSetRenderTargets(1, &rtv, dsv);
	Invalidate_Texture_Bindings(M);	// D3D11 unbinds textures that become targets
	M->current_render_target = render_target;
	M->bound_rtv = rtv;
	M->bound_dsv = dsv;
	number_of_API_calls++;
	Reset_Viewport_To_Target(render_target->width, render_target->height);
	return static_cast<HRESULT>(WW3D_OK);
}

void RenderCore::Set_Render_Target_Surface(WW3DSurfaceHandle render_target)
{
	// No own depth buffer: the target gets its matching one (see above).
	Set_Render_Target_Surfaces(render_target, nullptr);
}

void RenderCore::Set_Render_Target_Swap_Chain(WW3DSwapChainHandle)
{
	Log_Missing("Set_Render_Target_Swap_Chain");
}

void RenderCore::Release_Render_Target_Depth_Stencils()
{
	// D3D11 has no Reset and no DEFAULT pool; the substitute depth buffers belong to the
	// render targets and are released with them.
}

WW3DSurfaceHandle RenderCore::Get_Current_Render_Target() const
{
	return M->current_render_target;
}

WW3DSurfaceHandle RenderCore::Get_Front_Buffer_Copy()
{
	// DXGI offers no front buffer access; the back buffer is the right source
	// (the screenshot path uses the back buffer anyway).
	WW3DSurfaceHandle out = nullptr;
	Get_Back_Buffer(0, &out);
	return out;
}

HRESULT RenderCore::Get_Back_Buffer(unsigned, WW3DSurfaceHandle* out)
{
	if (!out) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	*out = nullptr;
	if (!M->device || !M->context || !M->scene_color) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	GpuTexture2D* source = Fx_Scene_8Bit();
	if (!source) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	// A staging resource is needed here, not the system-memory surface from
	// Create_Offscreen_Surface -- CopyResource writes into a GPU resource, and the caller
	// then locks it once for reading.
	GPU_TEXTURE2D_DESC td;
	::ZeroMemory(&td, sizeof(td));
	td.Width            = M->width;
	td.Height           = M->height;
	td.MipLevels        = 1;
	td.ArraySize        = 1;
	td.Format           = GPU_FORMAT_B8G8R8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage            = GPU_USAGE_STAGING;
	td.CPUAccessFlags   = GPU_CPU_ACCESS_READ;

	WW3DSurfaceObject* surface = W3DNEW WW3DSurfaceObject;
	::ZeroMemory(surface, sizeof(*surface));
	if (FAILED(M->device->CreateTexture2D(&td, nullptr, &surface->standalone))) {
		delete surface;
		return static_cast<HRESULT>(WW3D_ERR_OUTOFVIDEOMEMORY);
	}
	surface->gpu_format = td.Format;
	surface->width       = M->width;
	surface->height      = M->height;
	surface->refcount    = 1;

	// With supersampling the scene target is larger; the finished image lies at its top left.
	GPU_BOX box;
	box.left = 0;  box.top = 0;  box.front = 0;
	box.right = M->width;  box.bottom = M->height;  box.back = 1;
	M->context->CopySubresourceRegion(surface->standalone, 0, 0, 0, 0, source, 0, &box);
	*out = surface;
	return static_cast<HRESULT>(WW3D_OK);
}

// Free video memory: budget minus the process's usage, or the dedicated memory (GpuOutput).
unsigned __int64 RenderCore::Get_Free_Texture_RAM()
{
	return M->output ? M->output->Get_Free_Video_Memory() : 0;
}

// A narrow path is UTF-8 when it is valid UTF-8 with a byte above 127 (a user folder outside the ANSI code
// page), otherwise in the ANSI code page.
static FILE* Open_Narrow_For_Write(const char* path)
{
	bool high = false;
	for (const char* c = path; *c; ++c) high |= static_cast<unsigned char>(*c) > 127;
	if (high) {
		wchar_t wide[MAX_PATH];
		if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, MAX_PATH) > 0)
			return _wfopen(wide, L"wb");
	}
	return fopen(path, "wb");
}

bool RenderCore::Save_Back_Buffer_BMP(const char* path)
{
	WW3DSurfaceHandle surface = nullptr;
	if (FAILED(Get_Back_Buffer(0, &surface)) || !surface) return false;

	WW3DLockedRect locked;
	bool ok = false;
	if (SUCCEEDED(Lock_Surface(surface, &locked, nullptr, WW3D_LOCK_READONLY))) {
		FILE* file = Open_Narrow_For_Write(path);
		if (file) {
			// 24 bit BMP, rows bottom to top, padded to 4 bytes.
			const unsigned row_size   = ((M->width * 3 + 3) / 4) * 4;
			const unsigned image_size = row_size * M->height;
			unsigned char header[54] = { 0 };
			unsigned char* h = header;
			h[0] = 'B'; h[1] = 'M';
			// fill the header by hand (little endian)
			#define PUT32(off,val) { unsigned v_=(unsigned)(val); h[off]=(unsigned char)(v_&0xFF); h[(off)+1]=(unsigned char)((v_>>8)&0xFF); h[(off)+2]=(unsigned char)((v_>>16)&0xFF); h[(off)+3]=(unsigned char)((v_>>24)&0xFF); }
			#define PUT16(off,val) { unsigned v_=(unsigned)(val); h[off]=(unsigned char)(v_&0xFF); h[(off)+1]=(unsigned char)((v_>>8)&0xFF); }
			PUT32(2, 54 + image_size); PUT32(10, 54); PUT32(14, 40);
			PUT32(18, M->width); PUT32(22, M->height);
			PUT16(26, 1); PUT16(28, 24);
			PUT32(34, image_size); PUT32(38, 2835); PUT32(42, 2835);
			#undef PUT32
			#undef PUT16
			bool written = fwrite(header, sizeof(header), 1, file) == 1;
			unsigned char* row = W3DNEWARRAY unsigned char[row_size];
			for (int y = static_cast<int>(M->height) - 1; y >= 0 && written; --y) {
				const unsigned char* src = static_cast<const unsigned char*>(locked.pBits) + y * locked.Pitch;
				memset(row, 0, row_size);
				for (unsigned x = 0; x < M->width; ++x) {
					row[x * 3]     = src[x * 4];		// B
					row[x * 3 + 1] = src[x * 4 + 1];	// G
					row[x * 3 + 2] = src[x * 4 + 2];	// R
				}
				written = fwrite(row, row_size, 1, file) == 1;
			}
			delete [] row;
			ok = (fclose(file) == 0) && written;
		}
		Unlock_Surface(surface);
	}
	Release_Resource(surface);
	return ok;
}

// Registers a capture for the next Present.
void RenderCore::Request_Back_Buffer_Capture(const char* bmp_path)
{
	if (bmp_path == nullptr) {
		M->pending_capture[0] = 0;
		return;
	}
	strncpy(M->pending_capture, bmp_path, MAX_PATH - 1);
	M->pending_capture[MAX_PATH - 1] = 0;
}

// ==================================================================================================
// 6. Mouse cursor and caps
// ==================================================================================================
//
// Direct3D 11 has no hardware cursor (no SetCursorProperties/ShowCursor/SetCursorPosition).
// The Win32 cursor replaces it: an HICON is built from the pixels of the cursor surface and set
// with SetCursor. The game code (W3DMouse) stays unchanged -- it sets the image on every call
// anyway to avoid flicker, and that is exactly what keeps it working here.

BOOL RenderCore::Show_Cursor(BOOL show)
{
	const BOOL was_visible = M->cursor_visible ? TRUE : FALSE;
	M->cursor_visible = (show != FALSE);
	::SetCursor(M->cursor_visible ? static_cast<HCURSOR>(M->cursor) : nullptr);
	return was_visible;
}

HRESULT RenderCore::Set_Cursor_Properties(unsigned hot_x, unsigned hot_y, WW3DSurfaceHandle bitmap)
{
	if (!bitmap || !M->device || !M->context) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	const unsigned width  = bitmap->width;
	const unsigned height = bitmap->height;
	if (!width || !height) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	// Cursor images are .tga textures; the engine never loads those compressed
	// (TextureLoader: "No compressed destination format if reading from targa"), so this
	// is always a 32 bit format. Anything else is an error and is reported.
	if (bitmap->gpu_format != GPU_FORMAT_B8G8R8A8_UNORM &&
		bitmap->gpu_format != GPU_FORMAT_B8G8R8X8_UNORM) {
		Log_Missing("mouse cursor in a format other than B8G8R8A8");
		return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
	}

	GpuResource* source = bitmap->owner ? static_cast<GpuResource*>(bitmap->owner->gpu)
										   : static_cast<GpuResource*>(bitmap->standalone);
	const unsigned char* pixels = nullptr;
	unsigned pitch = 0;
	GpuTexture2D* staging = nullptr;
	GPU_MAPPED_SUBRESOURCE mapped;

	if (bitmap->cpu) {					// system-memory surface
		pixels = bitmap->cpu;
		pitch  = bitmap->pitch;
	}
	else {
		if (!source) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
		GPU_TEXTURE2D_DESC td;
		::ZeroMemory(&td, sizeof(td));
		td.Width            = width;
		td.Height           = height;
		td.MipLevels        = 1;
		td.ArraySize        = 1;
		td.Format           = bitmap->gpu_format;
		td.SampleDesc.Count = 1;
		td.Usage            = GPU_USAGE_STAGING;
		td.CPUAccessFlags   = GPU_CPU_ACCESS_READ;
		if (FAILED(M->device->CreateTexture2D(&td, nullptr, &staging))) return static_cast<HRESULT>(WW3D_ERR_OUTOFVIDEOMEMORY);

		GPU_BOX box;
		box.left = 0; box.right = width; box.top = 0; box.bottom = height; box.front = 0; box.back = 1;
		M->context->CopySubresourceRegion(staging, 0, 0, 0, 0, source, bitmap->owner ? bitmap->level : 0, &box);
		if (FAILED(M->context->Map(staging, 0, GPU_MAP_READ, 0, &mapped))) {
			staging->Release();
			return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);
		}
		pixels = static_cast<const unsigned char*>(mapped.pData);
		pitch  = mapped.RowPitch;
	}

	// Colour image as a top-down 32 bit DIB (negative height). Windows uses the alpha
	// channel when the colour image has 32 bits; the mask must still exist.
	BITMAPINFO bi;
	::ZeroMemory(&bi, sizeof(bi));
	bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth       = static_cast<LONG>(width);
	bi.bmiHeader.biHeight      = -static_cast<LONG>(height);
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	void* dib_bits = nullptr;
	HBITMAP color = ::CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &dib_bits, nullptr, 0);
	HICON icon = nullptr;
	if (color && dib_bits) {
		for (unsigned row = 0; row < height; ++row) {
			::memcpy(static_cast<unsigned char*>(dib_bits) + row * width * 4, pixels + row * pitch, width * 4);
		}
		// X8 formats have no usable alpha channel -- make everything opaque.
		if (bitmap->gpu_format == GPU_FORMAT_B8G8R8X8_UNORM) {
			unsigned char* p = static_cast<unsigned char*>(dib_bits);
			for (unsigned i = 0; i < width * height; ++i) p[i * 4 + 3] = 0xFF;
		}

		// Monochrome mask, all zero. CreateBitmap rows are WORD aligned.
		const unsigned mask_pitch = ((width + 15) / 16) * 2;
		unsigned char* mask_bits = W3DNEWARRAY unsigned char[mask_pitch * height];
		::memset(mask_bits, 0, mask_pitch * height);
		HBITMAP mask = ::CreateBitmap(static_cast<int>(width), static_cast<int>(height), 1, 1, mask_bits);
		delete[] mask_bits;

		if (mask) {
			ICONINFO info;
			::ZeroMemory(&info, sizeof(info));
			info.fIcon    = FALSE;			// cursor, not icon -- so the hotspot counts
			info.xHotspot = hot_x;
			info.yHotspot = hot_y;
			info.hbmMask  = mask;
			info.hbmColor = color;
			icon = ::CreateIconIndirect(&info);
			::DeleteObject(mask);
		}
		::DeleteObject(color);
	}

	if (staging) {
		M->context->Unmap(staging, 0);
		staging->Release();
	}

	if (!icon) return static_cast<HRESULT>(WW3D_ERR_INVALIDCALL);

	if (M->cursor) ::DestroyIcon(static_cast<HICON>(M->cursor));
	M->cursor = icon;
	if (M->cursor_visible) ::SetCursor(static_cast<HCURSOR>(M->cursor));
	return static_cast<HRESULT>(WW3D_OK);
}

void RenderCore::Set_Cursor_Position(int, int, unsigned)
{
	// The OS draws the cursor at the real mouse position, so there is nothing to move by hand;
	// intentionally a no-op.
}

void RenderCore::Compute_Caps()
{
	// Feature level 11.0 guarantees everything the engine queries.
	RenderCaps::Set_Fixed_Caps(
		true,		// DXTC
		false,		// gamma ramp: not available in windowed mode
		false,		// no N-patches
		true,		// DOT3 (emulated in the uber shader)
		true,		// BumpEnv (emulated in the uber shader)
		true,		// BumpEnv with luminance
		0x0400,		// vertex shader 4.0
		0x0400,		// pixel shader 4.0
		8,			// simultaneous textures
		16384, 16384,	// maximum texture size
		16,			// anisotropy
		true, true,	// anisotropic min/mag filtering
		true, true, true,	// linear min/mag/mip filtering
		false,		// no range fog
		// Combiner caps: the uber pixel shader emulates the full set, so all bits are reported.
		// ShaderClass::Apply queries them for every shader and would otherwise drop operations.
		WW3D_TEXOPCAPS_DISABLE | WW3D_TEXOPCAPS_SELECTARG1 | WW3D_TEXOPCAPS_SELECTARG2 |
		WW3D_TEXOPCAPS_MODULATE | WW3D_TEXOPCAPS_MODULATE2X | WW3D_TEXOPCAPS_MODULATE4X |
		WW3D_TEXOPCAPS_ADD | WW3D_TEXOPCAPS_ADDSIGNED | WW3D_TEXOPCAPS_ADDSIGNED2X |
		WW3D_TEXOPCAPS_SUBTRACT | WW3D_TEXOPCAPS_ADDSMOOTH |
		WW3D_TEXOPCAPS_BLENDDIFFUSEALPHA | WW3D_TEXOPCAPS_BLENDTEXTUREALPHA |
		WW3D_TEXOPCAPS_BLENDFACTORALPHA | WW3D_TEXOPCAPS_BLENDTEXTUREALPHAPM |
		WW3D_TEXOPCAPS_BLENDCURRENTALPHA | WW3D_TEXOPCAPS_PREMODULATE |
		WW3D_TEXOPCAPS_MODULATEALPHA_ADDCOLOR | WW3D_TEXOPCAPS_MODULATECOLOR_ADDALPHA |
		WW3D_TEXOPCAPS_BUMPENVMAP | WW3D_TEXOPCAPS_BUMPENVMAPLUMINANCE |
		WW3D_TEXOPCAPS_DOTPRODUCT3 | WW3D_TEXOPCAPS_MULTIPLYADD | WW3D_TEXOPCAPS_LERP,
		// MODULATEINVALPHA_ADDCOLOR and MODULATEINVCOLOR_ADDALPHA are left out on purpose: the
		// uber shader does not emulate them, and no caller asks for them.
		WW3D_PMISCCAPS_COLORWRITEENABLE);

	for (unsigned i = 0; i < WW3D_FORMAT_COUNT; ++i) {
		const WW3DFormat format = static_cast<WW3DFormat>(i);
		// R8G8B8 could be converted but is not reported as a texture format: callers that prefer it
		// (radar) would then write with SurfaceClass::DrawPixel, which knows no 3-byte pixels, and the
		// minimap would stay empty. This way they pick X8R8G8B8.
		RenderCaps::Set_Texture_Format_Supported(format,
			format != WW3D_FORMAT_UNKNOWN && format != WW3D_FORMAT_R8G8B8 &&
			WW3DFormat_To_Gpu_Format(format) != GPU_FORMAT_UNKNOWN);
	}

	// Log adapter, feature level and formats once, in the normal startup log so they are part
	// of every bug report.
	{
		WW3DAdapterIdentifier id;
		::ZeroMemory(&id, sizeof(id));
		if (Get_Adapter_Identifier(0, &id)) {
			WWDEBUG_SAY(("Render: adapter \"%s\", vendor 0x%04X, device 0x%04X\n",
				id.Description, id.VendorId, id.DeviceId));
		}
		WWDEBUG_SAY(("Render: feature level 11.0, max. texture %ux%u, %d simultaneous textures, "
					 "anisotropy up to %u\n",
			RenderCaps::Get_Max_Texture_Width(), RenderCaps::Get_Max_Texture_Height(),
			RenderCaps::Get_Max_Simultaneous_Textures(), RenderCaps::Get_Max_Anisotropy()));

		StringClass supported(true), converted(true), missing(true);
		for (unsigned i = 0; i < WW3D_FORMAT_COUNT; ++i) {
			const WW3DFormat format = static_cast<WW3DFormat>(i);
			if (format == WW3D_FORMAT_UNKNOWN) continue;
			StringClass name(true);
			RenderDevice::Get_Format_Name(WW3DFormat_To_Device_Format(format), &name);
			StringClass* target = &missing;
			if (WW3DFormat_To_Gpu_Format(format) != GPU_FORMAT_UNKNOWN)
				target = Render_Format_Needs_Conversion(format) ? &converted : &supported;
			*target += name;
			*target += " ";
		}
		WWDEBUG_SAY(("Render: native formats: %s\n", supported.Peek_Buffer()));
		WWDEBUG_SAY(("Render: formats converted to B8G8R8A8: %s\n", converted.Peek_Buffer()));
		WWDEBUG_SAY(("Render: formats without equivalent: %s\n", missing.Peek_Buffer()));
	}
}
