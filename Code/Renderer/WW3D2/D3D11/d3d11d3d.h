/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// d3d11d3d.h -------------------------------------------------------------------------------------
//
// The only place that includes <d3d11_1.h>/<dxgi1_6.h>: the Direct3D 11 device layer
// (d3d11device.cpp). The render core sees only gpudevice.h and renderobjects.h.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef D3D11D3D_H
#define D3D11D3D_H

#include "always.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11_1.h>
#include <dxgi1_6.h>
#ifdef _DEBUG
// ID3D11InfoQueue/D3D11_MESSAGE. d3d11.h usually pulls this in; including it explicitly keeps
// other SDK versions working.
#include <d3d11sdklayers.h>
#endif
// windows.h/winnt.h defines its own "BitTest" macro, which overrides the one from
// Lib/BaseType.h when included after it.
#undef BitTest
#define BitTest( x, i ) ( ( (x) & (i) ) != 0 )

// --- Small helper: release a COM pointer and null it --------------------------------------------
template <class T> inline void D3D11_Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

#endif // D3D11D3D_H
