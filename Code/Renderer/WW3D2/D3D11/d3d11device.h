/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// d3d11device.h ----------------------------------------------------------------------------------
//
// The Direct3D 11 device layer (gpudevice.h): wraps an ID3D11Device and its immediate context. Every
// call is passed on unchanged; the descriptions have the layout of their D3D11 counterparts.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef D3D11DEVICE_H
#define D3D11DEVICE_H

#include "WW3D2/Common/gpudevice.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11Resource;

/// Wraps device and immediate context (each gets one more reference). The context uses
/// ID3D11DeviceContext1 where the driver offers it. Release with D3D11_Destroy_Gpu_Device.
bool D3D11_Create_Gpu_Device(ID3D11Device* device, ID3D11DeviceContext* context, GpuDevice** gpu_device, GpuContext** gpu_context);
void D3D11_Destroy_Gpu_Device(GpuDevice*& gpu_device, GpuContext*& gpu_context);

/// The DXGI output (static object; Release_Interface at the end).
GpuOutput*		D3D11_Get_Gpu_Output();

/// A texture of the swap chain or another D3D11 texture as device layer object (adds a reference).
GpuTexture2D*	D3D11_Wrap_Texture(ID3D11Texture2D* texture);
/// The D3D11 resource below a device layer object (no reference added).
ID3D11Resource*	D3D11_Native_Resource(GpuResource* resource);

#endif // D3D11DEVICE_H
