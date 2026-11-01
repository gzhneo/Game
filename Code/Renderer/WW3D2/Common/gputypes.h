/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// gputypes.h -------------------------------------------------------------------------------------
//
// Descriptions of the device layer (gpudevice.h). The render core describes resources, state and
// views with these structures; they have the members and the memory layout of their Direct3D 11
// counterparts (GPU_X = D3D11_X), so the D3D11 device passes them on unchanged and d3d11device.cpp
// checks size and member offsets with static_assert. Other devices translate them.
// ------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef GPUTYPES_H
#define GPUTYPES_H

#include <windows.h>	// UINT, BOOL, FLOAT, RECT, HRESULT -- the device layer runs on Windows

#include "gputypes_enums.h"

#define GPU_FLOAT32_MAX		3.402823466e+38f
/// Shader compiler flag: highest optimization (value of D3DCOMPILE_OPTIMIZATION_LEVEL3).
#define GPU_COMPILE_OPTIMIZATION_LEVEL3	(1u << 15)
#define GPU_DEFAULT_SAMPLE_MASK	0xFFFFFFFFu

struct GPU_SAMPLE_DESC
{
	UINT	Count;
	UINT	Quality;
};

struct GPU_BUFFER_DESC
{
	UINT		ByteWidth;
	GPU_USAGE	Usage;
	UINT		BindFlags;
	UINT		CPUAccessFlags;
	UINT		MiscFlags;
	UINT		StructureByteStride;
};

struct GPU_TEXTURE2D_DESC
{
	UINT			Width;
	UINT			Height;
	UINT			MipLevels;
	UINT			ArraySize;
	GPU_FORMAT		Format;
	GPU_SAMPLE_DESC	SampleDesc;
	GPU_USAGE		Usage;
	UINT			BindFlags;
	UINT			CPUAccessFlags;
	UINT			MiscFlags;
};

struct GPU_SUBRESOURCE_DATA
{
	const void*	pSysMem;
	UINT		SysMemPitch;
	UINT		SysMemSlicePitch;
};

struct GPU_MAPPED_SUBRESOURCE
{
	void*	pData;
	UINT	RowPitch;
	UINT	DepthPitch;
};

struct GPU_BOX
{
	UINT	left;
	UINT	top;
	UINT	front;
	UINT	right;
	UINT	bottom;
	UINT	back;
};

struct GPU_VIEWPORT
{
	FLOAT	TopLeftX;
	FLOAT	TopLeftY;
	FLOAT	Width;
	FLOAT	Height;
	FLOAT	MinDepth;
	FLOAT	MaxDepth;
};

typedef RECT GPU_RECT;

struct GPU_INPUT_ELEMENT_DESC
{
	LPCSTR						SemanticName;
	UINT						SemanticIndex;
	GPU_FORMAT					Format;
	UINT						InputSlot;
	UINT						AlignedByteOffset;
	GPU_INPUT_CLASSIFICATION	InputSlotClass;
	UINT						InstanceDataStepRate;
};

#define GPU_APPEND_ALIGNED_ELEMENT	0xFFFFFFFFu

struct GPU_RENDER_TARGET_BLEND_DESC
{
	BOOL			BlendEnable;
	GPU_BLEND		SrcBlend;
	GPU_BLEND		DestBlend;
	GPU_BLEND_OP	BlendOp;
	GPU_BLEND		SrcBlendAlpha;
	GPU_BLEND		DestBlendAlpha;
	GPU_BLEND_OP	BlendOpAlpha;
	UINT8			RenderTargetWriteMask;
};

struct GPU_BLEND_DESC
{
	BOOL							AlphaToCoverageEnable;
	BOOL							IndependentBlendEnable;
	GPU_RENDER_TARGET_BLEND_DESC	RenderTarget[8];
};

struct GPU_DEPTH_STENCILOP_DESC
{
	GPU_STENCIL_OP		StencilFailOp;
	GPU_STENCIL_OP		StencilDepthFailOp;
	GPU_STENCIL_OP		StencilPassOp;
	GPU_COMPARISON_FUNC	StencilFunc;
};

struct GPU_DEPTH_STENCIL_DESC
{
	BOOL						DepthEnable;
	GPU_DEPTH_WRITE_MASK		DepthWriteMask;
	GPU_COMPARISON_FUNC			DepthFunc;
	BOOL						StencilEnable;
	UINT8						StencilReadMask;
	UINT8						StencilWriteMask;
	GPU_DEPTH_STENCILOP_DESC	FrontFace;
	GPU_DEPTH_STENCILOP_DESC	BackFace;
};

struct GPU_RASTERIZER_DESC
{
	GPU_FILL_MODE	FillMode;
	GPU_CULL_MODE	CullMode;
	BOOL			FrontCounterClockwise;
	INT				DepthBias;
	FLOAT			DepthBiasClamp;
	FLOAT			SlopeScaledDepthBias;
	BOOL			DepthClipEnable;
	BOOL			ScissorEnable;
	BOOL			MultisampleEnable;
	BOOL			AntialiasedLineEnable;
};

struct GPU_SAMPLER_DESC
{
	GPU_FILTER					Filter;
	GPU_TEXTURE_ADDRESS_MODE	AddressU;
	GPU_TEXTURE_ADDRESS_MODE	AddressV;
	GPU_TEXTURE_ADDRESS_MODE	AddressW;
	FLOAT						MipLODBias;
	UINT						MaxAnisotropy;
	GPU_COMPARISON_FUNC			ComparisonFunc;
	FLOAT						BorderColor[4];
	FLOAT						MinLOD;
	FLOAT						MaxLOD;
};

// Views: only the 2D texture forms are used. The union keeps the size of the D3D11 structure.
struct GPU_TEX2D_SRV
{
	UINT	MostDetailedMip;
	UINT	MipLevels;
};

struct GPU_SHADER_RESOURCE_VIEW_DESC
{
	GPU_FORMAT			Format;
	GPU_SRV_DIMENSION	ViewDimension;
	union {
		GPU_TEX2D_SRV	Texture2D;
		UINT			Reserved[4];
	};
};

struct GPU_TEX2D_RTV
{
	UINT	MipSlice;
};

struct GPU_RENDER_TARGET_VIEW_DESC
{
	GPU_FORMAT			Format;
	GPU_RTV_DIMENSION	ViewDimension;
	union {
		GPU_TEX2D_RTV	Texture2D;
		UINT			Reserved[3];
	};
};

struct GPU_TEX2D_DSV
{
	UINT	MipSlice;
};

struct GPU_DEPTH_STENCIL_VIEW_DESC
{
	GPU_FORMAT			Format;
	GPU_DSV_DIMENSION	ViewDimension;
	UINT				Flags;
	union {
		GPU_TEX2D_DSV	Texture2D;
		UINT			Reserved[3];
	};
};

struct GPU_QUERY_DESC
{
	GPU_QUERY	Query;
	UINT		MiscFlags;
};

struct GPU_QUERY_DATA_TIMESTAMP_DISJOINT
{
	UINT64	Frequency;
	BOOL	Disjoint;
};

struct GPU_QUERY_DATA_PIPELINE_STATISTICS
{
	UINT64	IAVertices;
	UINT64	IAPrimitives;
	UINT64	VSInvocations;
	UINT64	GSInvocations;
	UINT64	GSPrimitives;
	UINT64	CInvocations;
	UINT64	CPrimitives;
	UINT64	PSInvocations;
	UINT64	HSInvocations;
	UINT64	DSInvocations;
	UINT64	CSInvocations;
};

/// Preprocessor definition for shader compilation (D3D_SHADER_MACRO).
struct GPU_SHADER_MACRO
{
	LPCSTR	Name;
	LPCSTR	Definition;
};

#endif // GPUTYPES_H
