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
 *                     $Archive:: /Commando/Code/ww3d2/nullrobj.h                             $*
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

#ifndef NULL_H
#define NULL_H

#ifndef RENDOBJ_H
#include "rendobj.h"
#endif

#include "proto.h"

class Null3DObjClass : public RenderObjClass
{
public:

	Null3DObjClass(const char * name = "NULL");
	Null3DObjClass(const Null3DObjClass & src);
	Null3DObjClass & operator = (const Null3DObjClass & that);
			
	int						Class_ID(void) const override;
	RenderObjClass *	Clone(void) const override;
	const char *			Get_Name(void) const						override { return Name; }
	void					Render(RenderInfoClass & rinfo) override;
	void					Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const override;
	void					Get_Obj_Space_Bounding_Box(AABoxClass & box) const override;

protected:

	char								Name[2*W3D_NAME_LEN];
};


class NullPrototypeClass : public W3DMPO, public PrototypeClass
{
	W3DMPO_GLUE(NullPrototypeClass)
public:
	NullPrototypeClass(void);
	NullPrototypeClass(const W3dNullObjectStruct &null);

	const char *			Get_Name(void)	const			override { return Definition.Name; }
	int								Get_Class_ID(void) const	override { return RenderObjClass::CLASSID_NULL; }
	RenderObjClass *	Create(void) override					{ return NEW_REF(Null3DObjClass,(Definition.Name)); }
	void							DeleteSelf() override						{ delete this; }

protected:
	W3dNullObjectStruct			Definition;
};


class NullLoaderClass : public PrototypeLoaderClass
{
public:
	int						Chunk_Type(void) override { return W3D_CHUNK_NULL_OBJECT; }
	PrototypeClass *	Load_W3D(ChunkLoadClass & cload) override;
};


/*
** Instance of the default loader which the asset manager can
** automatically install at creation time
*/
extern NullLoaderClass _NullLoader;


#endif

