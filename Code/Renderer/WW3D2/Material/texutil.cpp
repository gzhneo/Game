/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// texutil.cpp -- texture copy and mip generation helpers, see header.
//
// DirectXTex is included before all engine headers: it needs <windows.h> without the min/max
// macros (NOMINMAX) and must not see the engine's own macros (e.g. from always.h).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "ww3dtypes.h"
#include <DirectXTex.h>

#include "texutil.h"
#include "WW3D2/Common/renderdevice.h"
#include "wwdebug.h"
#include <string.h>

namespace
{
	DXGI_FORMAT To_DXGI(WW3DDeviceFormat f)
	{
		switch (f) {
			case WW3D_FMT_A8R8G8B8:	return DXGI_FORMAT_B8G8R8A8_UNORM;
			case WW3D_FMT_X8R8G8B8:	return DXGI_FORMAT_B8G8R8X8_UNORM;
			case WW3D_FMT_A8B8G8R8:	return DXGI_FORMAT_R8G8B8A8_UNORM;
			case WW3D_FMT_R5G6B5:		return DXGI_FORMAT_B5G6R5_UNORM;
			case WW3D_FMT_A1R5G5B5:	return DXGI_FORMAT_B5G5R5A1_UNORM;
			case WW3D_FMT_X1R5G5B5:	return DXGI_FORMAT_B5G5R5A1_UNORM;	// the X bit travels as alpha and is ignored by the format
			case WW3D_FMT_A4R4G4B4:	return DXGI_FORMAT_B4G4R4A4_UNORM;
			case WW3D_FMT_A8:			return DXGI_FORMAT_A8_UNORM;
			case WW3D_FMT_L8:			return DXGI_FORMAT_R8_UNORM;		// same bits, per-channel filtering is identical
			case WW3D_FMT_A8L8:		return DXGI_FORMAT_R8G8_UNORM;
			case WW3D_FMT_V8U8:		return DXGI_FORMAT_R8G8_SNORM;
			case WW3D_FMT_DXT1:		return DXGI_FORMAT_BC1_UNORM;
			case WW3D_FMT_DXT2:
			case WW3D_FMT_DXT3:		return DXGI_FORMAT_BC2_UNORM;
			case WW3D_FMT_DXT4:
			case WW3D_FMT_DXT5:		return DXGI_FORMAT_BC3_UNORM;
			case WW3D_FMT_BC7:		return DXGI_FORMAT_BC7_UNORM;
			default:				return DXGI_FORMAT_UNKNOWN;
		}
	}

	bool Is_Block_Format(WW3DDeviceFormat f)
	{
		return f == WW3D_FMT_DXT1 || f == WW3D_FMT_DXT2 || f == WW3D_FMT_DXT3 || f == WW3D_FMT_DXT4 || f == WW3D_FMT_DXT5
			|| f == WW3D_FMT_BC7;
	}

	// bytes per pixel (uncompressed) or per 4x4 block (DXT/BC7); 0 = unknown
	unsigned Bytes_Per_Unit(WW3DDeviceFormat f)
	{
		switch (f) {
			case WW3D_FMT_A8R8G8B8: case WW3D_FMT_X8R8G8B8: case WW3D_FMT_A8B8G8R8: case WW3D_FMT_X8B8G8R8:
			case WW3D_FMT_X8L8V8U8: case WW3D_FMT_Q8W8V8U8: case WW3D_FMT_V16U16: case WW3D_FMT_A2R10G10B10:
				return 4;
			case WW3D_FMT_R8G8B8:
				return 3;
			case WW3D_FMT_R5G6B5: case WW3D_FMT_A1R5G5B5: case WW3D_FMT_X1R5G5B5: case WW3D_FMT_A4R4G4B4:
			case WW3D_FMT_X4R4G4B4: case WW3D_FMT_A8R3G3B2: case WW3D_FMT_A8L8: case WW3D_FMT_V8U8:
			case WW3D_FMT_L6V5U5: case WW3D_FMT_A8P8: case WW3D_FMT_L16:
				return 2;
			case WW3D_FMT_A8: case WW3D_FMT_L8: case WW3D_FMT_P8: case WW3D_FMT_R3G3B2: case WW3D_FMT_A4L4:
				return 1;
			case WW3D_FMT_DXT1:
				return 8;
			case WW3D_FMT_DXT2: case WW3D_FMT_DXT3: case WW3D_FMT_DXT4: case WW3D_FMT_DXT5:
			case WW3D_FMT_BC7:
				return 16;
			default:
				return 0;
		}
	}

	DirectX::TEX_FILTER_FLAGS To_Filter_Flags(TexUtil::Filter filter)
	{
		switch (filter) {
			case TexUtil::FILTER_NONE:
			case TexUtil::FILTER_POINT:		return DirectX::TEX_FILTER_POINT;
			case TexUtil::FILTER_TRIANGLE:	return DirectX::TEX_FILTER_TRIANGLE;
			case TexUtil::FILTER_BOX:
			default:							return DirectX::TEX_FILTER_BOX;
		}
	}

	// Row-wise copy of a rectangle (in pixels) between two locked surfaces of the same format.
	void Copy_Rows(WW3DDeviceFormat format, const void* src_bits, int src_pitch, int src_x, int src_y,
	               void* dst_bits, int dst_pitch, int dst_x, int dst_y, int width, int height)
	{
		const unsigned unit = Bytes_Per_Unit(format);
		int rows, row_bytes, src_off, dst_off;
		if (Is_Block_Format(format)) {
			rows = (height + 3) / 4;
			row_bytes = ((width + 3) / 4) * unit;
			src_off = (src_y / 4) * src_pitch + (src_x / 4) * unit;
			dst_off = (dst_y / 4) * dst_pitch + (dst_x / 4) * unit;
		} else {
			rows = height;
			row_bytes = width * unit;
			src_off = src_y * src_pitch + src_x * unit;
			dst_off = dst_y * dst_pitch + dst_x * unit;
		}
		const unsigned char* s = static_cast<const unsigned char*>(src_bits) + src_off;
		unsigned char* d = static_cast<unsigned char*>(dst_bits) + dst_off;
		for (int r = 0; r < rows; ++r) {
			memcpy(d, s, row_bytes);
			s += src_pitch;
			d += dst_pitch;
		}
	}

	void Clamp_Rect(RECT& r, const RECT* in, unsigned width, unsigned height)
	{
		if (in) {
			r = *in;
			if (r.left < 0) r.left = 0;
			if (r.top < 0) r.top = 0;
			if (r.right > static_cast<LONG>(width)) r.right = static_cast<LONG>(width);
			if (r.bottom > static_cast<LONG>(height)) r.bottom = static_cast<LONG>(height);
		} else {
			r.left = 0; r.top = 0; r.right = static_cast<LONG>(width); r.bottom = static_cast<LONG>(height);
		}
	}

	// Wraps a region of a locked surface as a DirectXTex image (no copy).
	DirectX::Image Make_Image(WW3DDeviceFormat format, DXGI_FORMAT dxgi, const void* bits, int pitch, int x, int y, int width, int height)
	{
		DirectX::Image img;
		img.width = width;
		img.height = height;
		img.format = dxgi;
		img.rowPitch = pitch;
		const unsigned unit = Bytes_Per_Unit(format);
		if (Is_Block_Format(format)) {
			img.slicePitch = static_cast<size_t>(pitch) * ((height + 3) / 4);
			img.pixels = (uint8_t*)bits + (y / 4) * pitch + (x / 4) * unit;
		} else {
			img.slicePitch = static_cast<size_t>(pitch) * height;
			img.pixels = (uint8_t*)bits + y * pitch + x * unit;
		}
		return img;
	}

	// Converts an image to the target size and format; the result lives in one of the scratch buffers.
	HRESULT Convert_Image(const DirectX::Image& src, unsigned dst_width, unsigned dst_height, DXGI_FORMAT dst_format,
	                      DirectX::TEX_FILTER_FLAGS filter, DirectX::ScratchImage scratch[3], const DirectX::Image** out)
	{
		const DirectX::Image* work = &src;
		HRESULT hr;
		if (DirectX::IsCompressed(work->format)) {
			hr = DirectX::Decompress(*work, DXGI_FORMAT_B8G8R8A8_UNORM, scratch[0]);
			if (FAILED(hr)) return hr;
			work = scratch[0].GetImage(0, 0, 0);
		}
		if (work->width != dst_width || work->height != dst_height) {
			hr = DirectX::Resize(*work, dst_width, dst_height, filter, scratch[1]);
			if (FAILED(hr)) return hr;
			work = scratch[1].GetImage(0, 0, 0);
		}
		if (work->format != dst_format) {
			if (DirectX::IsCompressed(dst_format)) {
				hr = DirectX::Compress(*work, dst_format, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, scratch[2]);
			} else {
				hr = DirectX::Convert(*work, dst_format, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, scratch[2]);
			}
			if (FAILED(hr)) return hr;
			work = scratch[2].GetImage(0, 0, 0);
		}
		*out = work;
		return S_OK;
	}
}

HRESULT TexUtil::Copy_Surface(WW3DSurfaceHandle dst, const RECT* dst_rect_in, WW3DSurfaceHandle src, const RECT* src_rect_in, Filter filter)
{
	if (!dst || !src) return E_INVALIDARG;

	WW3DSurfaceDesc sd, dd;
	HRESULT hr = RenderDevice::Get_Surface_Desc(src, &sd);
	if (FAILED(hr)) return hr;
	hr = RenderDevice::Get_Surface_Desc(dst, &dd);
	if (FAILED(hr)) return hr;

	RECT sr, dr;
	Clamp_Rect(sr, src_rect_in, sd.Width, sd.Height);
	Clamp_Rect(dr, dst_rect_in, dd.Width, dd.Height);
	const int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
	const int dw = dr.right - dr.left, dh = dr.bottom - dr.top;
	if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return S_OK;

	// block formats: rectangles must be aligned to multiples of 4
	if ((Is_Block_Format(sd.Format) && ((sr.left | sr.top) & 3)) || (Is_Block_Format(dd.Format) && ((dr.left | dr.top) & 3))) {
		WWDEBUG_SAY(("TexUtil::Copy_Surface: block format rectangle not 4-aligned\n"));
		return E_INVALIDARG;
	}

	WW3DLockedRect sl;
	hr = RenderDevice::Lock_Surface(src, &sl, nullptr, WW3D_LOCK_READONLY);
	if (FAILED(hr)) {
		WWDEBUG_SAY(("TexUtil::Copy_Surface: cannot lock source (Pool %d)\n", (int)sd.Pool));
		return hr;
	}

	const bool same_size = (sw == dw && sh == dh);
	if (sd.Format == dd.Format && same_size && Bytes_Per_Unit(sd.Format) != 0) {
		// 1:1 copy, any format
		WW3DLockedRect dl;
		hr = RenderDevice::Lock_Surface(dst, &dl, nullptr, 0);
		if (SUCCEEDED(hr)) {
			Copy_Rows(sd.Format, sl.pBits, sl.Pitch, sr.left, sr.top, dl.pBits, dl.Pitch, dr.left, dr.top, sw, sh);
			RenderDevice::Unlock_Surface(dst);
		} else {
			WWDEBUG_SAY(("TexUtil::Copy_Surface: cannot lock destination (Pool %d)\n", (int)dd.Pool));
		}
		RenderDevice::Unlock_Surface(src);
		return hr;
	}

	// scaling and/or format conversion via DirectXTex
	const DXGI_FORMAT sfmt = To_DXGI(sd.Format);
	const DXGI_FORMAT dfmt = To_DXGI(dd.Format);
	if (sfmt == DXGI_FORMAT_UNKNOWN || dfmt == DXGI_FORMAT_UNKNOWN) {
		WWDEBUG_SAY(("TexUtil::Copy_Surface: Format %d -> %d not supported\n", (int)sd.Format, (int)dd.Format));
		RenderDevice::Unlock_Surface(src);
		return E_NOTIMPL;
	}

	DirectX::ScratchImage scratch[3];
	const DirectX::Image* result = nullptr;
	const DirectX::Image srcimg = Make_Image(sd.Format, sfmt, sl.pBits, sl.Pitch, sr.left, sr.top, sw, sh);
	hr = Convert_Image(srcimg, dw, dh, dfmt, To_Filter_Flags(filter), scratch, &result);
	RenderDevice::Unlock_Surface(src);
	if (FAILED(hr)) {
		WWDEBUG_SAY(("TexUtil::Copy_Surface: DirectXTex error 0x%08X\n", (unsigned)hr));
		return hr;
	}

	WW3DLockedRect dl;
	hr = RenderDevice::Lock_Surface(dst, &dl, nullptr, 0);
	if (FAILED(hr)) {
		WWDEBUG_SAY(("TexUtil::Copy_Surface: cannot lock destination (Pool %d)\n", (int)dd.Pool));
		return hr;
	}
	Copy_Rows(dd.Format, result->pixels, (int)result->rowPitch, 0, 0, dl.pBits, dl.Pitch, dr.left, dr.top, dw, dh);
	RenderDevice::Unlock_Surface(dst);
	return S_OK;
}

HRESULT TexUtil::Generate_Mip_Levels(WW3DTextureHandle texture, Filter filter)
{
	if (!texture) return E_INVALIDARG;
	const unsigned levels = RenderDevice::Get_Texture_Level_Count(texture);
	if (levels <= 1) return S_OK;

	WW3DSurfaceDesc d0;
	HRESULT hr = RenderDevice::Get_Texture_Level_Desc(texture, 0, &d0);
	if (FAILED(hr)) return hr;
	const DXGI_FORMAT fmt = To_DXGI(d0.Format);
	if (fmt == DXGI_FORMAT_UNKNOWN) {
		WWDEBUG_SAY(("TexUtil::Generate_Mip_Levels: Format %d not supported\n", (int)d0.Format));
		return E_NOTIMPL;
	}

	// copy level 0 into a separate buffer (decompressed for block formats)
	DirectX::ScratchImage base;
	{
		WW3DLockedRect l0;
		hr = RenderDevice::Lock_Texture(texture, 0, &l0, nullptr, WW3D_LOCK_READONLY);
		if (FAILED(hr)) {
			WWDEBUG_SAY(("TexUtil::Generate_Mip_Levels: cannot lock level 0 (Pool %d)\n", (int)d0.Pool));
			return hr;
		}
		const DirectX::Image img = Make_Image(d0.Format, fmt, l0.pBits, l0.Pitch, 0, 0, d0.Width, d0.Height);
		if (DirectX::IsCompressed(fmt)) {
			hr = DirectX::Decompress(img, DXGI_FORMAT_B8G8R8A8_UNORM, base);
		} else {
			hr = base.InitializeFromImage(img);
		}
		RenderDevice::Unlock_Texture(texture, 0);
		if (FAILED(hr)) return hr;
	}

	DirectX::ScratchImage chain;
	hr = DirectX::GenerateMipMaps(*base.GetImage(0, 0, 0), To_Filter_Flags(filter) | DirectX::TEX_FILTER_SEPARATE_ALPHA, levels, chain);
	if (FAILED(hr)) {
		WWDEBUG_SAY(("TexUtil::Generate_Mip_Levels: GenerateMipMaps error 0x%08X\n", (unsigned)hr));
		return hr;
	}

	for (unsigned i = 1; i < levels && i < chain.GetMetadata().mipLevels; ++i) {
		WW3DSurfaceDesc di;
		hr = RenderDevice::Get_Texture_Level_Desc(texture, i, &di);
		if (FAILED(hr)) return hr;
		const DirectX::Image* mip = chain.GetImage(i, 0, 0);
		DirectX::ScratchImage conv;
		if (mip->format != fmt) {
			if (DirectX::IsCompressed(fmt)) {
				hr = DirectX::Compress(*mip, fmt, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv);
			} else {
				hr = DirectX::Convert(*mip, fmt, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv);
			}
			if (FAILED(hr)) return hr;
			mip = conv.GetImage(0, 0, 0);
		}
		WW3DLockedRect li;
		hr = RenderDevice::Lock_Texture(texture, i, &li, nullptr, 0);
		if (FAILED(hr)) return hr;
		Copy_Rows(di.Format, mip->pixels, (int)mip->rowPitch, 0, 0, li.pBits, li.Pitch, 0, 0, di.Width, di.Height);
		RenderDevice::Unlock_Texture(texture, i);
	}
	return S_OK;
}
