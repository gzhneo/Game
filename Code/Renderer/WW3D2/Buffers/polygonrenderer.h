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
 *                     $Archive:: /Commando/Code/ww3d2/polygonrenderer.h                   $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                      $Author:: Jani_p                                                      $*
 *                                                                                             *
 *                     $Modtime:: 7/12/01 6:38p                                               $*
 *                                                                                             *
 *                    $Revision:: 22                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#if defined(_MSC_VER)
#pragma once
#endif

#ifndef POLYGONRENDERER_H
#define POLYGONRENDERER_H


#include "always.h"
#include "meshmdl.h"
#include "meshrendererlist.h"
#include "sortingrenderer.h"
#include "mesh.h"
#include "WW3D2/Common/renderdevice.h"

class PolygonRendererClass;
class TextureCategoryClass;


/**
** PolygonRendererClass
** This is a record of a batch/range of polygons to be rendered.  These hang off of the TextureCategoryClass's
** and are rendered after the system installs a vertex buffer and textures in the render device.
*/
class PolygonRendererClass : public MultiListObjectClass 
{
	MeshModelClass *				mmc;
	TextureCategoryClass *	texture_category;			
	unsigned							index_offset;				// absolute index of index 0 for our parent mesh
	unsigned							vertex_offset;				// absolute index of vertex 0 for our parent mesh
	unsigned							index_count;				// number of indices
	unsigned							min_vertex_index;			// relative index of the first vertex our polys reference
	unsigned							vertex_index_range;		// range to the last vertex our polys reference
	bool								strip;						// is this a strip?
	unsigned							pass;					// rendering pass

public:
	PolygonRendererClass(
		unsigned index_count,
		MeshModelClass* mmc_,
		TextureCategoryClass* tex_cat,
		unsigned vertex_offset,
		unsigned index_offset,
		bool strip,
		unsigned pass);
	PolygonRendererClass(const PolygonRendererClass& src,MeshModelClass* mmc_);
	~PolygonRendererClass() override;

	void								Render(/*const Matrix3D & tm,*/int base_vertex_offset);
	void								Render_Sorted(/*const Matrix3D & tm,*/int base_vertex_offset,const SphereClass & bounding_sphere);
	void								Set_Vertex_Index_Range(unsigned min_vertex_index_,unsigned vertex_index_range_);
	
	unsigned							Get_Vertex_Offset(void)	{ return vertex_offset; }
	unsigned							Get_Index_Offset(void)	{ return index_offset; }
	inline unsigned						Get_Pass(void)	{ return pass; }

	MeshModelClass*				Get_Mesh_Model_Class() { return mmc; }
	TextureCategoryClass*	Get_Texture_Category() { return texture_category; }
	void								Set_Texture_Category(TextureCategoryClass* tc) { texture_category=tc; }

	void Log();
};

// ----------------------------------------------------------------------------

inline void PolygonRendererClass::Set_Vertex_Index_Range(unsigned min_vertex_index_,unsigned vertex_index_range_)
{
//	WWDEBUG_SAY(("Set_Vertex_Index_Range - min: %d, range: %d\n",min_vertex_index_,vertex_index_range_));
//	if (vertex_index_range_>30000) {
//		int a=0;
//		a++;
//	}
	min_vertex_index=min_vertex_index_;
	vertex_index_range=vertex_index_range_;
}

// ----------------------------------------------------------------------------

inline void PolygonRendererClass::Render(/*const Matrix3D & tm,*/int base_vertex_offset)
{
//	RenderDevice::Set_Transform(WW3D_TS_WORLD,tm);
//	SNAPSHOT_SAY(("Set_Transform\n"));
	SNAPSHOT_SAY(("Set_Index_Buffer_Index_Offset(%d)\n",base_vertex_offset));

	RenderDevice::Set_Index_Buffer_Index_Offset(base_vertex_offset);
	if (strip) {
		SNAPSHOT_SAY(("Draw_Strip(%d,%d,%d,%d)\n",index_offset,index_count-2,min_vertex_index,vertex_index_range));
		RenderDevice::Draw_Strip(
			static_cast<unsigned short>(index_offset),
			static_cast<unsigned short>(index_count-2),
			static_cast<unsigned short>(min_vertex_index),
			static_cast<unsigned short>(vertex_index_range));
	}
	else {
		SNAPSHOT_SAY(("Draw_Triangles(%d,%d,%d,%d)\n",index_offset,index_count-2,min_vertex_index,vertex_index_range));
		RenderDevice::Draw_Triangles(
			static_cast<unsigned short>(index_offset),
			static_cast<unsigned short>(index_count/3),
			static_cast<unsigned short>(min_vertex_index),
			static_cast<unsigned short>(vertex_index_range));
	}
}

inline void PolygonRendererClass::Render_Sorted(/*const Matrix3D & tm,*/int base_vertex_offset,const SphereClass & bounding_sphere)
{
	WWASSERT(!strip);	// Strips can't be sorted for now
//	RenderDevice::Set_Transform(WW3D_TS_WORLD,tm);
//	SNAPSHOT_SAY(("Set_Transform\n"));
	SNAPSHOT_SAY(("Set_Index_Buffer_Index_Offset(%d)\n",base_vertex_offset));
	SNAPSHOT_SAY(("Insert_Sorting_Triangles(%d,%d,%d,%d)\n",index_offset,index_count-2,min_vertex_index,vertex_index_range));

	RenderDevice::Set_Index_Buffer_Index_Offset(base_vertex_offset);
	SortingRendererClass::Insert_Triangles(
		bounding_sphere,
		static_cast<unsigned short>(index_offset),
		static_cast<unsigned short>(index_count/3),
		static_cast<unsigned short>(min_vertex_index),
		static_cast<unsigned short>(vertex_index_range));

}

#endif