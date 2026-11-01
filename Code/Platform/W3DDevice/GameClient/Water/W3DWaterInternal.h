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

// W3DWaterInternal.h -- what the W3DWater*.cpp files share and nobody else needs.

#pragma once

#define MIPMAP_BUMP_TEXTURE

//GeForce3 water system defines
#define PATCH_SIZE 15		//number of vertices on patch edge.  Large patches may waste vertices off edge of screen.
#define PATCH_UV_TILES	42	//number of times the bump map texture is tiled across patch (must be integer!).
#define PATCH_SCALE (4.0f * MAP_XY_FACTOR)	//horizontal scale factor. Adjust this and size to get desired vertex density.

#define PATCH_WIDTH (PATCH_SIZE-1)	//internal defines
#define PATCH_UV_SCALE	((Real)PATCH_UV_TILES/(Real)PATCH_WIDTH)	

constexpr auto BUMP_SIZE = 50.f;

#ifdef USE_MESH_NORMALS
#define WATER_MESH_FVF	RENDER_FVF_XYZNDUV2
typedef VertexFormatXYZNDUV2 MaterMeshVertexFormat;
#else
#define WATER_MESH_FVF	RENDER_FVF_XYZDUV2
using MaterMeshVertexFormat = VertexFormatXYZDUV2;
#endif

// Handles have no methods; release through the wrapper.
#define SAFE_RELEASE(p)      { if(p) { RenderDevice::Release_Resource(p); (p)=nullptr; } }

// Measurement helper for the standing water (defined in W3DWaterReal.cpp).
Int water2Measurement(Bool repeat);
