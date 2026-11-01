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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/renderfvf.h                               $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/26/02 5:06p                                             $*
 *                                                                                             *
 *                    $Revision:: 7                                                          $*
 *                                                                                             *
 * 06/26/02 KM VB Vertex format update for shaders                                       *
 * 07/17/02 KM VB Vertex format update for displacement mapping                               *
 * 08/01/02 KM VB Vertex format update for cube mapping                               *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#if defined(_MSC_VER)
#pragma once
#endif

#ifndef RENDERFVF_H
#define RENDERFVF_H

#include "always.h"
#include "ww3dtypes.h"

class StringClass;

// Translate an FVF code into a vertex declaration (canonical FVF order: position, blend
// weights/indices, normal, point size, diffuse, specular, texture coordinates). Writes at most
// max_count elements including WW3D_DECL_END() and returns the element count without the
// terminator. Pure conversion, no device access.
unsigned FVF_To_Vertex_Elements(DWORD fvf, WW3DVertexElement* out, unsigned max_count);

enum {
	RENDER_FVF_XYZ				= WW3D_FVF_XYZ,
	RENDER_FVF_XYZN			= WW3D_FVF_XYZ|WW3D_FVF_NORMAL,
	RENDER_FVF_XYZNUV1		= WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_TEX1,
	RENDER_FVF_XYZNUV2		= WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_TEX2,
	RENDER_FVF_XYZNDUV1		= WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_TEX1|WW3D_FVF_DIFFUSE,
	RENDER_FVF_XYZNDUV2		= WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_TEX2|WW3D_FVF_DIFFUSE,
	RENDER_FVF_XYZDUV1		= WW3D_FVF_XYZ|WW3D_FVF_TEX1|WW3D_FVF_DIFFUSE,
	RENDER_FVF_XYZDUV2		= WW3D_FVF_XYZ|WW3D_FVF_TEX2|WW3D_FVF_DIFFUSE,
	RENDER_FVF_XYZUV1			= WW3D_FVF_XYZ|WW3D_FVF_TEX1,
	RENDER_FVF_XYZUV2			= WW3D_FVF_XYZ|WW3D_FVF_TEX2,
 	RENDER_FVF_XYZNDUV1TG3	= (WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_DIFFUSE|WW3D_FVF_TEX4|WW3D_FVF_TEXCOORDSIZE2(0)|WW3D_FVF_TEXCOORDSIZE3(1)|WW3D_FVF_TEXCOORDSIZE3(2)|WW3D_FVF_TEXCOORDSIZE3(3)),
 	RENDER_FVF_XYZNUV2DMAP	= (WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_TEX3 | WW3D_FVF_TEXCOORDSIZE1(0) | WW3D_FVF_TEXCOORDSIZE4(1) | WW3D_FVF_TEXCOORDSIZE2(2) ),
	RENDER_FVF_XYZNDCUBEMAP	= WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_DIFFUSE, //|WW3D_FVF_TEX1|WW3D_FVF_TEXCOORDSIZE3(0)
	// GPU skins: XYZNDUV2 plus the bone index as a one-component UV set 2
	RENDER_FVF_XYZNDUV2BONE	= WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_DIFFUSE|WW3D_FVF_TEX3|WW3D_FVF_TEXCOORDSIZE1(2)
};

// ----------------------------------------------------------------------------
//
// Util structures for vertex buffer handling. Cast the void pointer returned
// by the vertex buffer to one of these structures.
//
// ----------------------------------------------------------------------------

struct VertexFormatXYZ
{
	float x;
	float y;
	float z;
};

struct VertexFormatXYZNUV1
{
	float x;
	float y;
	float z;
	float nx;
	float ny;
	float nz;
	float u1;
	float v1;
};

struct VertexFormatXYZNUV2
{
	float x;
	float y;
	float z;
	float nx;
	float ny;
	float nz;
	float u1;
	float v1;
	float u2;
	float v2;
};

struct VertexFormatXYZN
{
	float x;
	float y;
	float z;
	float nx;
	float ny;
	float nz;
};

struct VertexFormatXYZNDUV1
{
	float x;
	float y;
	float z;
	float nx;
	float ny;
	float nz;
	unsigned diffuse;
	float u1;
	float v1;
};

struct VertexFormatXYZNDUV2
{
	float x;
	float y;
	float z;
	float nx;
	float ny;
	float nz;
	unsigned diffuse;
	float u1;
	float v1;
	float u2;
	float v2;
};

struct VertexFormatXYZNDUV2Bone
{
	float x;
	float y;
	float z;
	float nx;
	float ny;
	float nz;
	unsigned diffuse;
	float u1;
	float v1;
	float u2;
	float v2;
	float bone;		///< bone index relative to the first bone of the palette
};

struct VertexFormatXYZDUV1
{
	float x;
	float y;
	float z;
	unsigned diffuse;
	float u1;
	float v1;
};

struct VertexFormatXYZDUV2
{
	float x;
	float y;
	float z;
	unsigned diffuse;
	float u1;
	float v1;
	float u2;
	float v2;
};

struct VertexFormatXYZUV1
{
	float x;
	float y;
	float z;
	float u1;
	float v1;
};

struct VertexFormatXYZUV2
{
	float x;
	float y;
	float z;
	float u1;
	float v1;
	float u2;
	float v2;
};

// todo KJM compress
struct VertexFormatXYZNDUV1TG3
{
	float x;
	float y;
	float z;
	float nx;
	float ny;
	float nz;
	unsigned diffuse;
	float u1;
	float v1;
	float Sx;
	float Sy;
	float Sz;
	float Tx;
	float Ty;
	float Tz;
	float SxTx;
	float SxTy;
	float SxTz;
};


// displacement mapping format
struct VertexFormatXYZNUV2DMAP
{
	float x;
	float y;
	float z;
	float nx;
	float ny;
	float nz;
	float T1x;
	float T1y;
	float T1z;
	float T1w;
	float T2x;
	float T2y;
};

// cube map format (texcoords are normally generated)
struct VertexFormatXYZNDCUBEMAP
{
	float x;
	float y;
	float z;
	float nx;
	float ny;
	float nz;
	unsigned diffuse;
//	float u1;
//	float v1;
//	float w1;
};

// FVF info class can be created for any legal FVF. It constructs information
// of offsets to various elements in the vertex buffer.

class FVFInfoClass : public W3DMPO
{
	W3DMPO_GLUE(FVFInfoClass)

	mutable unsigned						FVF;
	mutable unsigned						fvf_size;

	unsigned							location_offset;
	unsigned							normal_offset;
	unsigned							blend_offset;
	unsigned							texcoord_offset[WW3D_MAX_TEXCOORD];	
	unsigned							diffuse_offset;
	unsigned							specular_offset;
public:
	FVFInfoClass(unsigned FVF, unsigned vertex_size=0);

	inline unsigned Get_Location_Offset() const { return location_offset; }
	inline unsigned Get_Normal_Offset() const { return normal_offset; }
	inline unsigned Get_Tex_Offset(unsigned int n) const { return texcoord_offset[n]; }	

	inline unsigned Get_Diffuse_Offset() const { return diffuse_offset; }
	inline unsigned Get_Specular_Offset() const { return specular_offset; }
	inline unsigned Get_FVF() const { return FVF; }
	inline unsigned Get_FVF_Size() const { return fvf_size; }

	void Get_FVF_Name(StringClass& fvfname) const;	// For debug purposes

	// for enabling vertex shaders
	inline void Set_FVF(unsigned fvf) const { FVF=fvf; }
	inline void Set_FVF_Size(unsigned size) const { fvf_size=size; }
};


#endif
