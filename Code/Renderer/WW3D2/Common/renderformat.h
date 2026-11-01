/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// renderformat.h ----------------------------------------------------------------------------------
//
// Format mapping WW3DFormat/WW3D_FMT_* <-> DXGI and conversion of formats that feature
// level 11.0 lacks.
//
// Separate translation unit with no device dependency, so renderformat.cpp can be compiled and
// linked without a GPU; the RenderUnitTests target relies on that.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef RENDERFORMAT_H
#define RENDERFORMAT_H

#include "renderobjects.h"

/// WW3DFormat -> DXGI. Formats without an equivalent on FL 11.0 return B8G8R8A8_UNORM;
/// Render_Format_Needs_Conversion() tells the load path to convert them.
GPU_FORMAT WW3DFormat_To_Gpu_Format(WW3DFormat format);
/// true if the format must be converted to B8G8R8A8 on upload.
bool        Render_Format_Needs_Conversion(WW3DFormat format);
/// Device format (WW3D_FMT_*, legacy D3DFORMAT values) -> DXGI.
GPU_FORMAT Device_Format_To_Gpu_Format(unsigned device_format);
/// Inverse: DXGI -> WW3D_FMT_*. WW3D_FMT_UNKNOWN if there is no equivalent.
unsigned    Gpu_Format_To_Device_Format(GPU_FORMAT format);

/// Convert one image row to B8G8R8A8 (byte order B,G,R,A). `dst` holds pixels*4 bytes.
/// false if the format needs no conversion or cannot be converted.
bool Render_Convert_Row_To_BGRA(WW3DFormat format, const unsigned char* src, unsigned char* dst, unsigned pixels);
/// Convert an image region.
bool Render_Convert_Image_To_BGRA(WW3DFormat format, const unsigned char* src, unsigned src_pitch,
								 unsigned char* dst, unsigned dst_pitch, unsigned width, unsigned height);

#endif // RENDERFORMAT_H
