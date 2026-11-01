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
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/composite.h                            $*
 *                                                                                             *
 *                       Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                     $Modtime:: 11/25/01 12:25p                                             $*
 *                                                                                             *
 *                    $Revision:: 5                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#if defined(_MSC_VER)
#pragma once
#endif

#ifndef COMPOSITE_H
#define COMPOSITE_H

#include "rendobj.h"
#include "wwstring.h"

/*
** CompositeRenderObjClass
** The sole purpose of this class is to encapsulate some of the chores that all
** "composite" (contain sub objects) render objects have to do.  Typically all
** of the functions are implemented through the existing sub-object interface
** so there is still no assumption on how you store/organize your sub-objects.
*/
class CompositeRenderObjClass : public RenderObjClass
{
public:

	CompositeRenderObjClass(void);
	CompositeRenderObjClass(const CompositeRenderObjClass & that);
	~CompositeRenderObjClass(void) override;
	CompositeRenderObjClass & operator = (const CompositeRenderObjClass & that);

	void					Restart(void) override;

	const char *			Get_Name(void) const override;
	void					Set_Name(const char * name) override;
	const char *			Get_Base_Model_Name (void) const override;
	void					Set_Base_Model_Name (const char *name) override;
	int						Get_Num_Polys(void) const override;
	void					Notify_Added(SceneClass * scene) override;
	void					Notify_Removed(SceneClass * scene) override;

	bool					Cast_Ray(RayCollisionTestClass & raytest) override;
	bool					Cast_AABox(AABoxCollisionTestClass & boxtest) override;
	bool					Cast_OBBox(OBBoxCollisionTestClass & boxtest) override;
	bool					Intersect_AABox(AABoxIntersectionTestClass & boxtest) override;
	bool					Intersect_OBBox(OBBoxIntersectionTestClass & boxtest) override;

	void					Create_Decal(DecalGeneratorClass * generator) override;
	void					Delete_Decal(uint32 decal_id) override;

	void					Get_Obj_Space_Bounding_Sphere(SphereClass	& sphere) const override { sphere = ObjSphere; }
   void					Get_Obj_Space_Bounding_Box(AABoxClass & box) const override { box = ObjBox; }
	void					Update_Obj_Space_Bounding_Volumes(void) override;

	void					Set_User_Data(void *value, bool recursive = false) override;

protected:

	StringClass						Name;						// name of the render object
	StringClass						BaseModelName;			// name of the original render obj (before aggregation)
	SphereClass						ObjSphere;				// object-space bounding sphere
	AABoxClass						ObjBox;					// object-space bounding box
};



#endif