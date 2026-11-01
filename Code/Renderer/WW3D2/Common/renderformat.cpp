/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// renderformat.cpp --------------------------------------------------------------------------------
//
// Format mapping and conversion of formats that feature level 11.0 lacks. No device access at
// all, so unit tests can check the tables and bit math without a GPU.
// ------------------------------------------------------------------------------------------------

#include "renderformat.h"
#include <string.h>

// ==================================================================================================
// Format mapping
// ==================================================================================================
//
// Formats without a DXGI equivalent on feature level 11.0 are converted to B8G8R8A8 on load;
// the table returns B8G8R8A8 for them and Needs_Conversion() reports it.

GPU_FORMAT WW3DFormat_To_Gpu_Format(WW3DFormat format)
{
	switch (format) {
	case WW3D_FORMAT_A8R8G8B8:	return GPU_FORMAT_B8G8R8A8_UNORM;
	case WW3D_FORMAT_X8R8G8B8:	return GPU_FORMAT_B8G8R8X8_UNORM;
	case WW3D_FORMAT_R5G6B5:	return GPU_FORMAT_B5G6R5_UNORM;
	case WW3D_FORMAT_A1R5G5B5:	return GPU_FORMAT_B5G5R5A1_UNORM;
	case WW3D_FORMAT_A8:		return GPU_FORMAT_A8_UNORM;
	case WW3D_FORMAT_U8V8:		return GPU_FORMAT_R8G8_SNORM;
	case WW3D_FORMAT_DXT1:		return GPU_FORMAT_BC1_UNORM;
	case WW3D_FORMAT_DXT2:
	case WW3D_FORMAT_DXT3:		return GPU_FORMAT_BC2_UNORM;
	case WW3D_FORMAT_DXT4:
	case WW3D_FORMAT_DXT5:		return GPU_FORMAT_BC3_UNORM;
	case WW3D_FORMAT_BC7:		return GPU_FORMAT_BC7_UNORM;	// UNORM like DXT, not sRGB, so brightness matches
	// No DXGI equivalent on FL 11.0 (A4R4G4B4 only from 11.1, L8/A8L8 without swizzle,
	// X1R5G5B5 and 24-bit RGB not at all): convert to B8G8R8A8 on load.
	case WW3D_FORMAT_A4R4G4B4:
	case WW3D_FORMAT_X1R5G5B5:
	case WW3D_FORMAT_L8:
	case WW3D_FORMAT_A8L8:
	case WW3D_FORMAT_R8G8B8:	return GPU_FORMAT_B8G8R8A8_UNORM;
	default:					return GPU_FORMAT_UNKNOWN;
	}
}

/// true if the format must be converted to B8G8R8A8 on load.
bool Render_Format_Needs_Conversion(WW3DFormat format)
{
	switch (format) {
	case WW3D_FORMAT_A4R4G4B4:
	case WW3D_FORMAT_X1R5G5B5:
	case WW3D_FORMAT_L8:
	case WW3D_FORMAT_A8L8:
	case WW3D_FORMAT_R8G8B8:	return true;
	default:					return false;
	}
}

GPU_FORMAT Device_Format_To_Gpu_Format(unsigned device_format)
{
	// Device formats (WW3D_FMT_*) use the legacy D3DFORMAT numeric values; the subset the
	// engine uses matches WW3DFormat.
	switch (device_format) {
	case WW3D_FMT_A8R8G8B8:	return GPU_FORMAT_B8G8R8A8_UNORM;
	case WW3D_FMT_X8R8G8B8:	return GPU_FORMAT_B8G8R8X8_UNORM;
	case WW3D_FMT_R5G6B5:	return GPU_FORMAT_B5G6R5_UNORM;
	case WW3D_FMT_A1R5G5B5:	return GPU_FORMAT_B5G5R5A1_UNORM;
	case WW3D_FMT_A8:		return GPU_FORMAT_A8_UNORM;
	case WW3D_FMT_V8U8:		return GPU_FORMAT_R8G8_SNORM;
	case WW3D_FMT_DXT1:		return GPU_FORMAT_BC1_UNORM;
	case WW3D_FMT_DXT2:
	case WW3D_FMT_DXT3:		return GPU_FORMAT_BC2_UNORM;
	case WW3D_FMT_DXT4:
	case WW3D_FMT_DXT5:		return GPU_FORMAT_BC3_UNORM;
	case WW3D_FMT_BC7:		return GPU_FORMAT_BC7_UNORM;
	case WW3D_FMT_D24S8:	return GPU_FORMAT_D24_UNORM_S8_UINT;
	case WW3D_FMT_D32:		return GPU_FORMAT_D32_FLOAT;
	case WW3D_FMT_D16:		return GPU_FORMAT_D16_UNORM;
	default:				return GPU_FORMAT_B8G8R8A8_UNORM;
	}
}

// Device formats (WW3D_FMT_*) use the legacy D3DFORMAT numeric values; inverse of the table above.
unsigned Gpu_Format_To_Device_Format(GPU_FORMAT format)
{
	switch (format) {
	case GPU_FORMAT_B8G8R8A8_UNORM:      return WW3D_FMT_A8R8G8B8;
	case GPU_FORMAT_B8G8R8X8_UNORM:      return WW3D_FMT_X8R8G8B8;
	case GPU_FORMAT_B5G6R5_UNORM:        return WW3D_FMT_R5G6B5;
	case GPU_FORMAT_B5G5R5A1_UNORM:      return WW3D_FMT_A1R5G5B5;
	case GPU_FORMAT_A8_UNORM:            return WW3D_FMT_A8;
	case GPU_FORMAT_R8G8_SNORM:          return WW3D_FMT_V8U8;
	case GPU_FORMAT_BC1_UNORM:           return WW3D_FMT_DXT1;
	case GPU_FORMAT_BC2_UNORM:           return WW3D_FMT_DXT3;
	case GPU_FORMAT_BC3_UNORM:           return WW3D_FMT_DXT5;
	case GPU_FORMAT_BC7_UNORM:           return WW3D_FMT_BC7;
	case GPU_FORMAT_D24_UNORM_S8_UINT:   return WW3D_FMT_D24S8;
	case GPU_FORMAT_D32_FLOAT:           return WW3D_FMT_D32;
	case GPU_FORMAT_D16_UNORM:           return WW3D_FMT_D16;
	default:                              return WW3D_FMT_UNKNOWN;
	}
}

// ==================================================================================================
// Format conversion
// ==================================================================================================
//
// Feature level 11.0 lacks A4R4G4B4, X1R5G5B5, L8, A8L8 and 24-bit R8G8B8. Such textures are
// stored on the GPU as B8G8R8A8, but game code still writes them in the original layout (fonts
// use A4R4G4B4), so they are converted here on upload. The target is always
// GPU_FORMAT_B8G8R8A8_UNORM, byte order B,G,R,A.

bool Render_Convert_Row_To_BGRA(WW3DFormat format, const unsigned char* src, unsigned char* dst, unsigned pixels)
{
	switch (format) {
	case WW3D_FORMAT_A4R4G4B4:
		// 16 bit: AAAA RRRR GGGG BBBB. The 4-bit value is scaled by 17 (= 255/15).
		for (unsigned i = 0; i < pixels; ++i) {
			const unsigned short v = (reinterpret_cast<const unsigned short*>(src))[i];
			dst[i * 4 + 0] = static_cast<unsigned char>(( v        & 0xF) * 17);	// B
			dst[i * 4 + 1] = static_cast<unsigned char>(((v >>  4) & 0xF) * 17);	// G
			dst[i * 4 + 2] = static_cast<unsigned char>(((v >>  8) & 0xF) * 17);	// R
			dst[i * 4 + 3] = static_cast<unsigned char>(((v >> 12) & 0xF) * 17);	// A
		}
		return true;

	case WW3D_FORMAT_X1R5G5B5:
		// 16 bit: X RRRRR GGGGG BBBBB. 5 bit -> 8 bit via (x << 3) | (x >> 2).
		for (unsigned i = 0; i < pixels; ++i) {
			const unsigned short v = (reinterpret_cast<const unsigned short*>(src))[i];
			const unsigned b =  v        & 0x1F;
			const unsigned g = (v >>  5) & 0x1F;
			const unsigned r = (v >> 10) & 0x1F;
			dst[i * 4 + 0] = static_cast<unsigned char>((b << 3) | (b >> 2));
			dst[i * 4 + 1] = static_cast<unsigned char>((g << 3) | (g >> 2));
			dst[i * 4 + 2] = static_cast<unsigned char>((r << 3) | (r >> 2));
			dst[i * 4 + 3] = 0xFF;
		}
		return true;

	case WW3D_FORMAT_L8:
		for (unsigned i = 0; i < pixels; ++i) {
			const unsigned char l = src[i];
			dst[i * 4 + 0] = l; dst[i * 4 + 1] = l; dst[i * 4 + 2] = l; dst[i * 4 + 3] = 0xFF;
		}
		return true;

	case WW3D_FORMAT_A8L8:
		// 16 bit: alpha in the high byte, luminance in the low byte.
		for (unsigned i = 0; i < pixels; ++i) {
			const unsigned char l = src[i * 2 + 0];
			const unsigned char a = src[i * 2 + 1];
			dst[i * 4 + 0] = l; dst[i * 4 + 1] = l; dst[i * 4 + 2] = l; dst[i * 4 + 3] = a;
		}
		return true;

	case WW3D_FORMAT_R8G8B8:
		// 24 bit, in memory B,G,R (as D3DFMT_R8G8B8).
		for (unsigned i = 0; i < pixels; ++i) {
			dst[i * 4 + 0] = src[i * 3 + 0];
			dst[i * 4 + 1] = src[i * 3 + 1];
			dst[i * 4 + 2] = src[i * 3 + 2];
			dst[i * 4 + 3] = 0xFF;
		}
		return true;

	default:
		return false;
	}
}

bool Render_Convert_Image_To_BGRA(WW3DFormat format, const unsigned char* src, unsigned src_pitch,
								 unsigned char* dst, unsigned dst_pitch, unsigned width, unsigned height)
{
	for (unsigned row = 0; row < height; ++row) {
		if (!Render_Convert_Row_To_BGRA(format, src + row * src_pitch, dst + row * dst_pitch, width))
			return false;
	}
	return true;
}
