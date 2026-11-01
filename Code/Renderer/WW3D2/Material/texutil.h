/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// texutil.h ---------------------------------------------------------------------------------
//
// Surface copy and mip generation on top of DirectXTex (vcpkg "directxtex"), with the semantics
// of the old D3DXLoadSurfaceFromSurface and D3DXFilterTexture. DirectXTex works on CPU memory;
// only the surface Lock/Unlock in texutil.cpp touches the render device.
//
// Supported formats: A8R8G8B8, X8R8G8B8, A8B8G8R8, R5G6B5, A1R5G5B5, X1R5G5B5, A4R4G4B4, A8, L8,
// A8L8, V8U8, DXT1-5, BC7 (everything Get_Valid_Texture_Format can return). Other formats only
// support the 1:1 copy with same format and size; otherwise E_NOTIMPL.
// ----------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef TEXUTIL_H
#define TEXUTIL_H

#include "ww3dtypes.h"



namespace TexUtil
{
	enum Filter
	{
		FILTER_NONE,		///< no scaling, pixels 1:1 (point sampling if sizes differ)
		FILTER_POINT,		///< D3DX_FILTER_POINT
		FILTER_BOX,			///< D3DX_FILTER_BOX
		FILTER_TRIANGLE		///< D3DX_FILTER_TRIANGLE
	};

	/// Like D3DXLoadSurfaceFromSurface(dst, NULL, dst_rect, src, NULL, src_rect, filter, 0):
	/// copies src_rect to dst_rect, scaling and converting the format as needed.
	/// NULL rectangles mean the whole surface. Both surfaces must be lockable.
	HRESULT Copy_Surface(WW3DSurfaceHandle dst, const RECT* dst_rect, WW3DSurfaceHandle src, const RECT* src_rect, Filter filter);

	/// Like D3DXFilterTexture(tex, NULL, 0, filter): generates all mip levels from level 0.
	HRESULT Generate_Mip_Levels(WW3DTextureHandle texture, Filter filter);
}

#endif // TEXUTIL_H
