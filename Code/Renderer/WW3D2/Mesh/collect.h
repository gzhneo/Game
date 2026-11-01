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
 *                     $Archive:: /Commando/Code/ww3d2/collect.h                              $*
 *                                                                                             *
 *                       Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                     $Modtime:: 1/08/01 10:04a                                              $*
 *                                                                                             *
 *                    $Revision:: 1                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef COLLECT_H
#define COLLECT_H

#include "rendobj.h"
#include "composite.h"
#include "Vector.H"
#include "proto.h"
#include "w3d_file.h"
#include "wwstring.h"
#include "proxy.h"

class CollectionDefClass;
class SnapPointsClass;


/*
** CollectionClass
** This is a render object which contains a collection of render objects.
*/
class CollectionClass : public CompositeRenderObjClass
{
public:

	CollectionClass(void);
	CollectionClass(const CollectionDefClass & def);
	CollectionClass(const CollectionClass & src);
	CollectionClass & operator = (const CollectionClass &);
	~CollectionClass(void) override;
	RenderObjClass *	Clone(void) const override;		
	
	int						Class_ID(void)	const override;
	int						Get_Num_Polys(void) const override;

	/////////////////////////////////////////////////////////////////////////////
	// Proxy interface
	/////////////////////////////////////////////////////////////////////////////
	virtual int						Get_Proxy_Count (void) const;
	virtual bool					Get_Proxy (int index, ProxyClass &proxy) const;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Rendering
	/////////////////////////////////////////////////////////////////////////////
	void					Render(RenderInfoClass & rinfo) override;
	void					Special_Render(SpecialRenderInfoClass & rinfo) override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - "Scene Graph"
	/////////////////////////////////////////////////////////////////////////////
	void 					Set_Transform(const Matrix3D &m) override;
	void 					Set_Position(const Vector3 &v) override;
	int						Get_Num_Sub_Objects(void) const override;	
	RenderObjClass *	Get_Sub_Object(int index) const override;
	int						Add_Sub_Object(RenderObjClass * subobj) override;
	int						Remove_Sub_Object(RenderObjClass * robj) override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Collision Detection, Ray Tracing
	/////////////////////////////////////////////////////////////////////////////
	bool					Cast_Ray(RayCollisionTestClass & raytest) override;
	bool					Cast_AABox(AABoxCollisionTestClass & boxtest) override;
	bool					Cast_OBBox(OBBoxCollisionTestClass & boxtest) override;
	bool					Intersect_AABox(AABoxIntersectionTestClass & boxtest) override;
	bool					Intersect_OBBox(OBBoxIntersectionTestClass & boxtest) override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Bounding Volumes
	/////////////////////////////////////////////////////////////////////////////
	void		 			Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const override;
	void					Get_Obj_Space_Bounding_Box(AABoxClass & box) const override;


	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Attributes, Options, Properties, etc
	/////////////////////////////////////////////////////////////////////////////
	virtual int						Snap_Point_Count(void);
	void					Get_Snap_Point(int index,Vector3 * set) override;
	void					Scale(float scale) override;
	void					Scale(float scalex, float scaley, float scalez) override;
   void               Update_Obj_Space_Bounding_Volumes(void) override;

protected:

	void								Free(void);
	void								Update_Sub_Object_Transforms(void) override;
	
	DynamicVectorClass <ProxyClass>			ProxyList;
	DynamicVectorClass <RenderObjClass *>	SubObjects;
	SnapPointsClass *								SnapPoints;

	SphereClass										BoundSphere;
	AABoxClass										BoundBox;
};


/*
** CollectionLoaderClass
** Loader for collection objects
*/
class CollectionLoaderClass : public PrototypeLoaderClass
{
public:

	int						Chunk_Type(void) override { return W3D_CHUNK_COLLECTION; }
	PrototypeClass *	Load_W3D(ChunkLoadClass & cload) override;
};

extern CollectionLoaderClass _CollectionLoader;

#endif
