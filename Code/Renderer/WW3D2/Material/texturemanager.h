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
 *                 Project Name : DX8 Texture Manager                                          *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/texturemanager.h                            $*
 *                                                                                             *
 *              Original Author:: Hector Yee                                                   *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 4                                                           $*
 *                                                                                             *
 * 06/27/02 KM Texture class abstraction																			*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H

#include "always.h"
#include "texture.h"
#include "WW3D2/Common/renderdevice.h"
#include "ww3dformat.h"
#include "meshrendererlist.h"
#include "ww3dformat.h"
#include "multilist.h"

class TextureManagerClass;

class TextureTrackerClass : public MultiListObjectClass
{
public:
	TextureTrackerClass
	(
		unsigned int w, 
		unsigned int h, 
		MipCountType count,
		TextureBaseClass *tex
	)
	: Width(w),
	  Height(h),
	  Mip_level_count(count),
	  Texture(tex)
	{
	}

	virtual void Recreate() const =0;

	void Release()
	{
		Texture->Set_D3D_Base_Texture(nullptr);
	}

	TextureBaseClass* Get_Texture() const { return Texture; }


protected:

	unsigned int Width;
	unsigned int Height;
	MipCountType Mip_level_count;
	TextureBaseClass *Texture;
};

class DeviceTextureTrackerClass : public TextureTrackerClass
{
public:
	DeviceTextureTrackerClass
	(
		unsigned int w, 
		unsigned int h, 
		WW3DFormat format,
		MipCountType count,
		TextureBaseClass *tex,
		bool rt
	) 
	: TextureTrackerClass(w,h,count,tex), Format(format), RenderTarget(rt)
	{
	}

	void Recreate() const override
	{
		WWASSERT(Texture->Peek_D3D_Base_Texture()==nullptr);
		Texture->Poke_Texture
		(
			RenderDevice::_Create_Device_Texture
			(
				Width,
				Height,
				Format,
				Mip_level_count,
				WW3D_POOL_DEFAULT,
				RenderTarget
			)
		);
	}

private:
	WW3DFormat Format;
	bool RenderTarget;
};

class DeviceZTextureTrackerClass : public TextureTrackerClass
{
public:
	DeviceZTextureTrackerClass
	(
		unsigned int w,
		unsigned int h,
		WW3DZFormat zformat,
		MipCountType count,
		TextureBaseClass* tex
	)
	: TextureTrackerClass(w,h,count,tex), ZFormat(zformat)
	{
	}

	void Recreate() const override
	{
		WWASSERT(Texture->Peek_D3D_Base_Texture()==nullptr);
		Texture->Poke_Texture
		(
			RenderDevice::_Create_Device_ZTexture
			(
				Width,
				Height,
				ZFormat,
				Mip_level_count,
				WW3D_POOL_DEFAULT
			)
		);
	}


private:
	WW3DZFormat ZFormat;
};


class TextureManagerClass
{
public:
	static void Shutdown();
	static void Add(TextureTrackerClass *track);
	static void Remove(TextureBaseClass *tex);
	static void Release_Textures();
	static void Recreate_Textures();
private:
	static TextureTrackerList Managed_Textures;
};

#endif // ifdef TEXTUREMANAGER