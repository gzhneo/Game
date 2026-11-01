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

#include "renderfvf.h"
#include "wwstring.h"

// Vertex size of an FVF code, same rules as the old D3DXGetFVFVertexSize: position by
// WW3D_FVF_POSITION_MASK, normal, point size, diffuse, specular, then per texture coordinate
// set the size encoded by WW3D_FVF_TEXCOORDSIZEn.
static unsigned Get_FVF_Vertex_Size(unsigned FVF)
{
	unsigned size = 0;
	switch (FVF & WW3D_FVF_POSITION_MASK) {
		case WW3D_FVF_XYZ:    size += 3 * sizeof(float); break;
		case WW3D_FVF_XYZRHW: size += 4 * sizeof(float); break;
		case WW3D_FVF_XYZB1:  size += 4 * sizeof(float); break;
		case WW3D_FVF_XYZB2:  size += 5 * sizeof(float); break;
		case WW3D_FVF_XYZB3:  size += 6 * sizeof(float); break;
		case WW3D_FVF_XYZB4:  size += 7 * sizeof(float); break;
		case WW3D_FVF_XYZB5:  size += 8 * sizeof(float); break;
		case WW3D_FVF_XYZW:   size += 4 * sizeof(float); break;
		default: break;
	}
	if (FVF & WW3D_FVF_NORMAL)   size += 3 * sizeof(float);
	if (FVF & WW3D_FVF_PSIZE)    size += sizeof(float);
	if (FVF & WW3D_FVF_DIFFUSE)  size += sizeof(DWORD);
	if (FVF & WW3D_FVF_SPECULAR) size += sizeof(DWORD);

	const unsigned texCount = (FVF & WW3D_FVF_TEXCOUNT_MASK) >> WW3D_FVF_TEXCOUNT_SHIFT;
	for (unsigned i = 0; i < texCount; ++i) {
		// two bits per set from bit 16: 0 = 2D (default), 3 = 1D, 1 = 3D, 2 = 4D
		switch ((FVF >> (16 + i * 2)) & 3) {
			case 0: size += 2 * sizeof(float); break;
			case 3: size += 1 * sizeof(float); break;
			case 1: size += 3 * sizeof(float); break;
			case 2: size += 4 * sizeof(float); break;
		}
	}
	return size;
}

// FVF -> WW3DVertexElement[] (vertex declaration equivalent of an FVF code).
unsigned FVF_To_Vertex_Elements(DWORD fvf, WW3DVertexElement* out, unsigned max_count)
{
	unsigned n = 0;
	WORD offset = 0;
	struct Adder {
		WW3DVertexElement* out; unsigned& n; WORD& offset; unsigned max_count;
		void operator()(BYTE type, BYTE usage, BYTE usage_index, unsigned size) {
			if (n + 1 < max_count) {
				out[n].Stream = 0; out[n].Offset = offset; out[n].Type = type;
				out[n].Method = WW3D_DECLMETHOD_DEFAULT; out[n].Usage = usage; out[n].UsageIndex = usage_index;
				++n;
			}
			offset = static_cast<WORD>(offset + size);
		}
	} add = { out, n, offset, max_count };
	static const BYTE float_types[5] = { 0, WW3D_DECLTYPE_FLOAT1, WW3D_DECLTYPE_FLOAT2, WW3D_DECLTYPE_FLOAT3, WW3D_DECLTYPE_FLOAT4 };

	switch (fvf & WW3D_FVF_POSITION_MASK) {
		case WW3D_FVF_XYZ:    add(WW3D_DECLTYPE_FLOAT3, WW3D_DECLUSAGE_POSITION, 0, 12); break;
		case WW3D_FVF_XYZRHW: add(WW3D_DECLTYPE_FLOAT4, WW3D_DECLUSAGE_POSITIONT, 0, 16); break;
		case WW3D_FVF_XYZW:   add(WW3D_DECLTYPE_FLOAT4, WW3D_DECLUSAGE_POSITION, 0, 16); break;
		case WW3D_FVF_XYZB1: case WW3D_FVF_XYZB2: case WW3D_FVF_XYZB3: case WW3D_FVF_XYZB4: case WW3D_FVF_XYZB5: {
			add(WW3D_DECLTYPE_FLOAT3, WW3D_DECLUSAGE_POSITION, 0, 12);
			unsigned betas = ((fvf & WW3D_FVF_POSITION_MASK) - WW3D_FVF_XYZB1) / 2 + 1;	// XYZB1=0x006 .. XYZB5=0x00E
			const bool ubyte4 = (fvf & WW3D_FVF_LASTBETA_UBYTE4) != 0;
			const bool d3dcolor = (fvf & WW3D_FVF_LASTBETA_D3DCOLOR) != 0;
			unsigned weights = (ubyte4 || d3dcolor) ? betas - 1 : betas;
			if (weights > 4) weights = 4;
			if (weights) add(float_types[weights], WW3D_DECLUSAGE_BLENDWEIGHT, 0, weights * 4);
			if (ubyte4) add(WW3D_DECLTYPE_UBYTE4, WW3D_DECLUSAGE_BLENDINDICES, 0, 4);
			else if (d3dcolor) add(WW3D_DECLTYPE_D3DCOLOR, WW3D_DECLUSAGE_BLENDINDICES, 0, 4);
			break;
		}
		default: break;
	}
	if (fvf & WW3D_FVF_NORMAL)   add(WW3D_DECLTYPE_FLOAT3,   WW3D_DECLUSAGE_NORMAL, 0, 12);
	if (fvf & WW3D_FVF_PSIZE)    add(WW3D_DECLTYPE_FLOAT1,   WW3D_DECLUSAGE_PSIZE,  0, 4);
	if (fvf & WW3D_FVF_DIFFUSE)  add(WW3D_DECLTYPE_D3DCOLOR, WW3D_DECLUSAGE_COLOR,  0, 4);
	if (fvf & WW3D_FVF_SPECULAR) add(WW3D_DECLTYPE_D3DCOLOR, WW3D_DECLUSAGE_COLOR,  1, 4);

	const unsigned texCount = (fvf & WW3D_FVF_TEXCOUNT_MASK) >> WW3D_FVF_TEXCOUNT_SHIFT;
	for (unsigned i = 0; i < texCount; ++i) {
		unsigned comps;
		switch ((fvf >> (16 + i * 2)) & 3) {	// as in Get_FVF_Vertex_Size
			case 3:  comps = 1; break;
			case 1:  comps = 3; break;
			case 2:  comps = 4; break;
			default: comps = 2; break;
		}
		add(float_types[comps], WW3D_DECLUSAGE_TEXCOORD, static_cast<BYTE>(i), comps * 4);
	}

	if (n < max_count) {
		const WW3DVertexElement end = WW3D_DECL_END();
		out[n] = end;
	}
	return n;
}

FVFInfoClass::FVFInfoClass(unsigned FVF_, unsigned vertex_size)
	:
	FVF(FVF_),
	fvf_size(FVF!=0 ? Get_FVF_Vertex_Size(FVF) : vertex_size)
{
	location_offset=0;
	blend_offset=location_offset;
	
	if ((FVF&WW3D_FVF_XYZ)==WW3D_FVF_XYZ) blend_offset+=3*sizeof(float);
	normal_offset=blend_offset;

	if ( ((FVF&WW3D_FVF_XYZB4)==WW3D_FVF_XYZB4) &&
		  ((FVF&WW3D_FVF_LASTBETA_UBYTE4)==WW3D_FVF_LASTBETA_UBYTE4) ) normal_offset+=3*sizeof(float)+sizeof(DWORD);
	diffuse_offset=normal_offset;

	if ((FVF&WW3D_FVF_NORMAL)==WW3D_FVF_NORMAL) diffuse_offset+=3*sizeof(float);
	specular_offset=diffuse_offset;

	if ((FVF&WW3D_FVF_DIFFUSE)==WW3D_FVF_DIFFUSE) specular_offset+=sizeof(DWORD);
	texcoord_offset[0]=specular_offset;

	if ((FVF&WW3D_FVF_SPECULAR)==WW3D_FVF_SPECULAR) texcoord_offset[0]+=sizeof(DWORD);	

	for (unsigned int i=1; i<WW3D_MAX_TEXCOORD; i++)
	{
		texcoord_offset[i]=texcoord_offset[i-1];

		if ((int(FVF)&WW3D_FVF_TEXCOORDSIZE1(i-1))==WW3D_FVF_TEXCOORDSIZE1(i-1)) texcoord_offset[i]+=sizeof(float);
		else if ((int(FVF)&WW3D_FVF_TEXCOORDSIZE2(i-1))==WW3D_FVF_TEXCOORDSIZE2(i-1)) texcoord_offset[i]+=2*sizeof(float);
		else if ((int(FVF)&WW3D_FVF_TEXCOORDSIZE3(i-1))==WW3D_FVF_TEXCOORDSIZE3(i-1)) texcoord_offset[i]+=3*sizeof(float);
		else if ((int(FVF)&WW3D_FVF_TEXCOORDSIZE4(i-1))==WW3D_FVF_TEXCOORDSIZE4(i-1)) texcoord_offset[i]+=4*sizeof(float);
	}
}

void FVFInfoClass::Get_FVF_Name(StringClass& fvfname) const
{
	switch (Get_FVF()) {
	case RENDER_FVF_XYZ: fvfname="WW3D_FVF_XYZ"; break;
	case RENDER_FVF_XYZN: fvfname="WW3D_FVF_XYZ|WW3D_FVF_NORMAL"; break;
	case RENDER_FVF_XYZNUV1: fvfname="WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_TEX1"; break;
	case RENDER_FVF_XYZNUV2: fvfname="WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_TEX2"; break;
	case RENDER_FVF_XYZNDUV1: fvfname="WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_TEX1|WW3D_FVF_DIFFUSE"; break;
	case RENDER_FVF_XYZNDUV2: fvfname="WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_TEX2|WW3D_FVF_DIFFUSE"; break;
	case RENDER_FVF_XYZDUV1: fvfname="WW3D_FVF_XYZ|WW3D_FVF_TEX1|WW3D_FVF_DIFFUSE"; break;
	case RENDER_FVF_XYZDUV2: fvfname="WW3D_FVF_XYZ|WW3D_FVF_TEX2|WW3D_FVF_DIFFUSE"; break;
	case RENDER_FVF_XYZUV1: fvfname="WW3D_FVF_XYZ|WW3D_FVF_TEX1"; break;
	case RENDER_FVF_XYZUV2: fvfname="WW3D_FVF_XYZ|WW3D_FVF_TEX2"; break;
	case RENDER_FVF_XYZNDUV1TG3 : fvfname="(WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_DIFFUSE|WW3D_FVF_TEX4|WW3D_FVF_TEXCOORDSIZE2(0)|WW3D_FVF_TEXCOORDSIZE3(1)|WW3D_FVF_TEXCOORDSIZE3(2)|WW3D_FVF_TEXCOORDSIZE3(3))"; break;
	case RENDER_FVF_XYZNUV2DMAP :	fvfname="(WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_TEX3|WW3D_FVF_TEXCOORDSIZE1(0)|WW3D_FVF_TEXCOORDSIZE4(1)|WW3D_FVF_TEXCOORDSIZE2(2))"; break;
	case RENDER_FVF_XYZNDCUBEMAP : fvfname="(WW3D_FVF_XYZ|WW3D_FVF_NORMAL|WW3D_FVF_DIFFUSE|WW3D_FVF_TEX1|D3DFVFTEXCOORDSIZE3(0)"; break;
	default: fvfname="Unknown!";
	}
}
