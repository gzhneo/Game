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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : W3DAssetManager                                                  *
 *                                                                                             *
 *                     $Archive::                                                             $*
 *                                                                                             *
 *              Original Author:: Hector Yee (Enbassetmgr)
 * 			     Added/Modified:: Mark Wilczynski
 *                                                                                             *
 *                      $Author::                                                             $*
 *                                                                                             *
 *                     $Modtime::                                                             $*
 *                                                                                             *
 *                    $Revision::                                                             $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include <always.h>
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "formatinto.h"
#include "proto.h"
#include "rendobj.h"
#include <vector3.h>
#include "mesh.h"
#include "hlod.h"
#include "matinfo.h"
#include "meshmdl.h"
#include "part_emt.h"
#include "vertmaterial.h"
#include "WW3D2/Common/renderdevice.h"
#include "texture.h"
#include "surfaceclass.h"
#include "textureloader.h"
#include "ww3dformat.h"
#include "colorspace.h"
#include <wwprofile.h>
#include "ffactory.h"
#include "font3d.h"
#include "render2dsentence.h"
#include <stdio.h>
#include "Common/PerfTimer.h"
#include "Common/GlobalData.h"
#include "Common/GameCommon.h"
#include "Lib/SafeString.h"
#include "formatinto.h"
#include "Common/FileSystem.h"
#include "Common/file.h"
#include "chunkio.h"
#include "ddsfile.h"
#include "w3d_file.h"
#include "htreemgr.h"
#include "hanimmgr.h"
#include <string>
#include <vector>

//---------------------------------------------------------------------
// Constants
//---------------------------------------------------------------------

const float ident_scale(1.0f);
const float scale_epsilon(0.01f);
const Vector3 ident_HSV(0,0,0);
const float H_epsilon(1.0f);
const float S_epsilon(0.01f);
const float V_epsilon(0.01f);

//---------------------------------------------------------------------
// Externs defined somewhere in W3D.
//---------------------------------------------------------------------

unsigned int PixelSize(const SurfaceClass::SurfaceDescription &sd);
void Convert_Pixel(Vector3 &rgb, const SurfaceClass::SurfaceDescription &sd, const unsigned char * pixel);
void Convert_Pixel(unsigned char * pixel,const SurfaceClass::SurfaceDescription &sd, const Vector3 &rgb);

//---------------------------------------------------------------------
// W3DPrototype
//---------------------------------------------------------------------

//---------------------------------------------------------------------
class W3DPrototypeClass : public MemoryPoolObject, public PrototypeClass
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( W3DPrototypeClass, "W3DPrototypeClass" )

public:
	W3DPrototypeClass(RenderObjClass * proto, const AsciiString& name);

	virtual const char*					Get_Name(void) const override			{ return Name.str(); }
	virtual int									Get_Class_ID(void) const override	{ return Proto->Class_ID(); }
	virtual RenderObjClass *		Create(void) override;
	virtual void								DeleteSelf() override							{	deleteInstance(); }

protected:
	//virtual ~W3DPrototypeClass(void);

private:
	RenderObjClass *					Proto;
	AsciiString								Name;
};

//---------------------------------------------------------------------
W3DPrototypeClass::W3DPrototypeClass(RenderObjClass * proto, const AsciiString& name) :
	Proto(proto),
	Name(name)
{ 
	assert(Proto); 
	Proto->Add_Ref(); 
}

//---------------------------------------------------------------------
W3DPrototypeClass::~W3DPrototypeClass(void)						
{ 
	if (Proto) { 
		Proto->Release_Ref(); 
		Proto = nullptr;
	}
}

//---------------------------------------------------------------------
RenderObjClass * W3DPrototypeClass::Create(void)					
{ 
	return static_cast<RenderObjClass *>( SET_REF_OWNER( Proto->Clone() ) ); 
}
	
//---------------------------------------------------------------------
// W3DAssetManager
//---------------------------------------------------------------------

//---------------------------------------------------------------------
W3DAssetManager::W3DAssetManager(void)
{
}

//---------------------------------------------------------------------
W3DAssetManager::~W3DAssetManager(void)
{
}

#ifdef DUMP_PERF_STATS
__int64 Total_Get_Texture_Time=0;
#endif

TextureClass *	W3DAssetManager::Get_Texture
	(
		const char * filename, 
		MipCountType mip_level_count,
		WW3DFormat texture_format,
		bool allow_compression,
		TextureBaseClass::TexAssetType type,
		bool allow_reduction
	)
{
	//Just call the base implementation after adjusting reduction to deal
	//with our special types.

	// ZHCD keeps its palette in the top row and needs exact pixels; ZHCA (alpha mask, coloured in the pixel shader) is
	// reduced and comes from the texture package like any other texture.
	if (filename && *filename && _strnicmp(filename,"ZHCD",4) == 0)
		allow_reduction = false;

	return WW3DAssetManager::Get_Texture(	filename, 
		mip_level_count,
		texture_format,
		allow_compression,
		type,
		allow_reduction
	);
}


//---------------------------------------------------------------------
RenderObjClass * W3DAssetManager::Create_Render_Obj(const char* name)
{
	return WW3DAssetManager::Create_Render_Obj(name);
}

//---------------------------------------------------------------------
/** 'Generals' specific munging to encode team color and scale in model name */
static inline void Munge_Render_Obj_Name(char *newname, size_t newnameSize, const char *oldname, float scale, const int color, const char *textureName)
{
	char lower_case_name[255];
	safe_strcpy(lower_case_name, oldname);
	_strlwr(lower_case_name);

	if (!textureName)
		textureName = "";

	WWFormat::Into(newname, newnameSize, "#{:d}!{:g}!{}#{}", color, scale, textureName, lower_case_name);
}

//---------------------------------------------------------------------
static inline void Munge_Texture_Name(char *newname, size_t newnameSize, const char *oldname, const int color)
{
	char lower_case_name[255];
	safe_strcpy(lower_case_name, oldname);
	_strlwr(lower_case_name);
	WWFormat::Into(newname, newnameSize, "#{:d}#{}", color, lower_case_name);
}

//---------------------------------------------------------------------
int W3DAssetManager::replaceAssetTexture(RenderObjClass *robj, TextureClass *oldTex, TextureClass *newTex)
{
	switch (robj->Class_ID())	{	
	case RenderObjClass::CLASSID_MESH:
		return replaceMeshTexture(robj, oldTex, newTex);
		break;
	case RenderObjClass::CLASSID_HLOD:
		return replaceHLODTexture(robj, oldTex, newTex);
		break;
	}
	return 0;
}

//---------------------------------------------------------------------
Int W3DAssetManager::replaceHLODTexture(RenderObjClass *robj, TextureClass *oldTex, TextureClass *newTex)
{
	int didReplace=0;

	int num_sub = robj->Get_Num_Sub_Objects();
	for(int i = 0; i < num_sub; i++) {
		RenderObjClass *sub_obj = robj->Get_Sub_Object(i);
		didReplace |= replaceAssetTexture(sub_obj, oldTex, newTex);
		REF_PTR_RELEASE(sub_obj);
	}
	return didReplace;
}

//---------------------------------------------------------------------
Int W3DAssetManager::replaceMeshTexture(RenderObjClass *robj, TextureClass *oldTex, TextureClass *newTex)
{
	int i;
	int didReplace=0;

	MeshClass *mesh=static_cast<MeshClass*>(robj);	
	MeshModelClass * model = mesh->Get_Model();
	MaterialInfoClass	*material = mesh->Get_Material_Info();

	for (i=0; i<material->Texture_Count(); i++)
	{
		if (material->Peek_Texture(i) == oldTex)
		{	
			model->Replace_Texture(oldTex,newTex);
			material->Replace_Texture(i,newTex);
			didReplace=1;
		}
	}

	REF_PTR_RELEASE(material);	
	REF_PTR_RELEASE(model);
	return didReplace;
}

//---------------------------------------------------------------------
/** Replaces all references to old texture with new texture.  Operation is performed on
	asset prototype so it will affect most instances of this object.  Objects which have
	been customized with house color will not be affected unless they are created after
	this function is called.
*/
int W3DAssetManager::replacePrototypeTexture(RenderObjClass *robj, const char * oldname, const char * newname)
{
	//search model for old texture
	TextureClass *oldTex=Get_Texture(oldname);
	TextureClass *newTex=Get_Texture(newname);

	int retCode=replaceAssetTexture(robj,oldTex,newTex);

	REF_PTR_RELEASE(oldTex);
	REF_PTR_RELEASE(newTex);

	return retCode;
}

//---------------------------------------------------------------------
/** Generals specific version that looks for a texture which has been house-color tinted.
	Don't use this unless you really need it.  For normal textures, use Get_Texture().
*/
TextureClass * W3DAssetManager::Find_Texture(const char * name, const int color)
{
	char newname[512];	
	Munge_Texture_Name(newname, sizeof(newname), name, color);

	// see if we have a cached copy
	TextureClass *newtex = TextureHash.Get(newname);
	if (newtex) {
		newtex->Add_Ref();
	}
	return newtex;
}

//---------------------------------------------------------------------
TextureClass * W3DAssetManager::Recolor_Texture(TextureClass *texture, const int color)
{
	const char *name=texture->Get_Texture_Name();	

	TextureClass *newtex = Find_Texture(name, color);
	if (newtex) {
		return newtex;
	}

	return Recolor_Texture_One_Time(texture, color);
}

//---------------------------------------------------------------------
const Int TEAM_COLOR_PALETTE_SIZE = 16;
const UnsignedShort houseColorScale[TEAM_COLOR_PALETTE_SIZE] = 
{
	255,239,223,211,195,174,167,151,135,123,107,91,79,63,47,35
};

//---------------------------------------------------------------------
static void remapPalette16Bit(SurfaceClass::SurfaceDescription *sd, UnsignedShort *palette, unsigned int color)
{
	UnsignedShort pal[TEAM_COLOR_PALETTE_SIZE];
	Vector3 rgb,v_color(static_cast<float>((color>>16)&0xff)/255.0f/255.0f,static_cast<float>((color>>8)&0xff)/255.0f/255.0f,static_cast<float>(color&0xff)/255.0f/255.0f);

	//Generate a new color gradient palette based on reference color
	for (Int y=0; y<TEAM_COLOR_PALETTE_SIZE; y++)
	{	
		rgb.X=static_cast<Real>(houseColorScale[y])*v_color.X;
		rgb.Y=static_cast<Real>(houseColorScale[y])*v_color.Y;
		rgb.Z=static_cast<Real>(houseColorScale[y])*v_color.Z;
		pal[y]=0xffff;	//preset alpha to known value
		Convert_Pixel(reinterpret_cast<unsigned char *>(&pal[y]),*sd,rgb);
	}

	//check if this pixel is part of team color palette
	for (Int p=0; p<TEAM_COLOR_PALETTE_SIZE; p++)
	{	palette[p]=pal[p];	//replace color with house color
	}
}

//---------------------------------------------------------------------
static void remapTexture16Bit(Int dx, Int dy, Int pitch, SurfaceClass::SurfaceDescription *sd, UnsignedShort *palette, UnsignedShort *data, unsigned int color)
{
	UnsignedShort pal[TEAM_COLOR_PALETTE_SIZE];
	Vector3 rgb,v_color(static_cast<float>((color>>16)&0xff)/255.0f/255.0f,static_cast<float>((color>>8)&0xff)/255.0f/255.0f,static_cast<float>(color&0xff)/255.0f/255.0f);
	Int y;

	//Generate a new color gradient palette based on reference color
	for (y=0; y<TEAM_COLOR_PALETTE_SIZE; y++)
	{	
		rgb.X=static_cast<Real>(houseColorScale[y])*v_color.X;
		rgb.Y=static_cast<Real>(houseColorScale[y])*v_color.Y;
		rgb.Z=static_cast<Real>(houseColorScale[y])*v_color.Z;
		pal[y]=0xffff;	//preset alpha to known value
		Convert_Pixel(reinterpret_cast<unsigned char *>(&pal[y]),*sd,rgb);
	}

	for (y=0; y<dy; y++)
	{	for (Int x=0; x<dx; x++)
		{	//check if this pixel is part of team color palette
			for (Int p=0; p<TEAM_COLOR_PALETTE_SIZE; p++)
			{	if (palette[p]==data[x])
				{	data[x]=pal[p];	//replace color with house color
					break;
				}
			}
		}

		data += pitch;
	}
}

//Use hue shift instead of alpha blend to apply house color to textures
#define DO_HUE_SHIFT

//---------------------------------------------------------------------
//Input texture is assumed to be in ARGB 4444 format.
static void remapAlphaTexture16Bit(Int dx, Int dy, Int pitch, SurfaceClass::SurfaceDescription *sd, UnsignedShort *data, unsigned int color)
{
	UnsignedShort pixel;
	UnsignedShort pixelAlpha;
#ifndef DO_HUE_SHIFT
	float fpixelAlpha,fpixelAlphaInv;
#endif
	Vector3 rgb,v_color(static_cast<float>((color>>16)&0xff)/255.0f,static_cast<float>((color>>8)&0xff)/255.0f,static_cast<float>(color&0xff)/255.0f);
	Int x,y;

#ifdef DO_HUE_SHIFT
	Vector3 hsv;
	Vector3 hsv_color;
	RGB_To_HSV(hsv_color,v_color);
#endif

	for (y=0; y<dy; y++)
	{	
		for (x=0; x<dx; x++)
		{
			pixel=data[x];
			pixelAlpha=15-(pixel>>12);	//get alpha for house color
			if (pixelAlpha)
			{	//some house color needs to show through
				///@todo: optimize this alpha blend to use fixed point math.
#ifdef DO_HUE_SHIFT
				RGB_To_HSV(hsv,Vector3(((pixel>>8)&0xf)/15.0f,((pixel>>4)&0xf)/15.0f,(pixel &0xf)/15.0f));
				hsv.X=hsv_color.X;
				hsv.Y*=hsv_color.Y;
				HSV_To_RGB(rgb,hsv);
#else
				fpixelAlpha=pixelAlpha/15.0f;
				fpixelAlphaInv=1.0f-fpixelAlpha;
				rgb.X=fpixelAlpha * v_color.X + fpixelAlphaInv*(Real)((pixel>>8)&0xf)/15.0f;	//red
				rgb.Y=fpixelAlpha * v_color.Y + fpixelAlphaInv*(Real)((pixel>>4)&0xf)/15.0f; //green
				rgb.Z=fpixelAlpha * v_color.Z + fpixelAlphaInv*(Real)(pixel&0xf)/15.0f; //blue
#endif
				data[x] = static_cast<UnsignedShort>(REAL_TO_INT(rgb.X*15.0f)<<8 | REAL_TO_INT(rgb.Y*15.0f)<<4 | REAL_TO_INT(rgb.Z*15.0f));
			}
			data[x] |= 0xf000;	//force alpha to opaque.
		}
		data += pitch;
	}
}

//---------------------------------------------------------------------
static void remapPalette32Bit(SurfaceClass::SurfaceDescription *sd, UnsignedInt *palette, unsigned int color)
{
	UnsignedInt pal[TEAM_COLOR_PALETTE_SIZE];
	Vector3 rgb,v_color(static_cast<float>((color>>16)&0xff)/255.0f/255.0f,static_cast<float>((color>>8)&0xff)/255.0f/255.0f,static_cast<float>(color&0xff)/255.0f/255.0f);

	//Generate a new color gradient palette based on reference color
	for (Int y=0; y<TEAM_COLOR_PALETTE_SIZE; y++)
	{	
		rgb.X=static_cast<Real>(houseColorScale[y])*v_color.X;
		rgb.Y=static_cast<Real>(houseColorScale[y])*v_color.Y;
		rgb.Z=static_cast<Real>(houseColorScale[y])*v_color.Z;
		pal[y]=0xffffffff;	//preset alpha to known value
		Convert_Pixel(reinterpret_cast<unsigned char *>(&pal[y]),*sd,rgb);
	}

	//check if this pixel is part of team color palette
	for (Int p=0; p<TEAM_COLOR_PALETTE_SIZE; p++)
	{	palette[p]=pal[p];	//replace color with house color
	}
}

//---------------------------------------------------------------------
static void remapTexture32Bit(Int dx, Int dy, Int pitch, SurfaceClass::SurfaceDescription *sd, UnsignedInt *palette, UnsignedInt *data, unsigned int color)
{
	UnsignedInt pal[TEAM_COLOR_PALETTE_SIZE];
	Vector3 rgb,v_color(static_cast<float>((color>>16)&0xff)/255.0f/255.0f,static_cast<float>((color>>8)&0xff)/255.0f/255.0f,static_cast<float>(color&0xff)/255.0f/255.0f);
	Int y;

	//Generate a new color gradient palette based on reference color
	for (y=0; y<TEAM_COLOR_PALETTE_SIZE; y++)
	{	
		rgb.X=static_cast<Real>(houseColorScale[y])*v_color.X;
		rgb.Y=static_cast<Real>(houseColorScale[y])*v_color.Y;
		rgb.Z=static_cast<Real>(houseColorScale[y])*v_color.Z;
		pal[y]=0xffffffff;	//preset alpha to known value
		Convert_Pixel(reinterpret_cast<unsigned char *>(&pal[y]),*sd,rgb);
	}

	for (y=0; y<dy; y++)
	{	for (Int x=0; x<dx; x++)
		{	//check if this pixel is part of team color palette
			for (Int p=0; p<TEAM_COLOR_PALETTE_SIZE; p++)
			{	if (palette[p]==data[x])
				{	data[x]=pal[p];	//replace color with house color
					break;
				}
			}
		}
		data += pitch;
	}
}

//---------------------------------------------------------------------
static void remapAlphaTexture32Bit(Int dx, Int dy, Int pitch, SurfaceClass::SurfaceDescription *sd, UnsignedInt *data, unsigned int color)
{
	UnsignedInt pixel;
	UnsignedInt pixelAlpha;
#ifndef DO_HUE_SHIFT
	float fpixelAlpha,fpixelAlphaInv;
#endif
	Vector3 rgb,v_color(static_cast<float>((color>>16)&0xff)/255.0f,static_cast<float>((color>>8)&0xff)/255.0f,static_cast<float>(color&0xff)/255.0f);
	Int x,y;
#ifdef DO_HUE_SHIFT
	Vector3 hsv;
	Vector3 hsv_color;
	RGB_To_HSV(hsv_color,v_color);
#endif

	for (y=0; y<dy; y++)
	{	for (x=0; x<dx; x++)
		{
			pixel=data[x];
			pixelAlpha=255-(pixel>>24);	//get alpha for house color
			if (pixelAlpha)
			{	//some house color needs to show through
#ifdef DO_HUE_SHIFT
				RGB_To_HSV(hsv,Vector3(((pixel>>16)&0xff)/255.0f,((pixel>>8)&0xff)/255.0f,(pixel &0xff)/255.0f));
				hsv.X=hsv_color.X;
				hsv.Y*=hsv_color.Y;
				HSV_To_RGB(rgb,hsv);
#else
				///@todo: optimize this alpha blend to use fixed point math.
				fpixelAlpha=pixelAlpha/255.0f;
				fpixelAlphaInv=1.0f-fpixelAlpha;
				rgb.X=fpixelAlpha * v_color.X + fpixelAlphaInv*(Real)((pixel>>16)&0xff)/255.0f;	//red
				rgb.Y=fpixelAlpha * v_color.Y + fpixelAlphaInv*(Real)((pixel>>8)&0xff)/255.0f; //green
				rgb.Z=fpixelAlpha * v_color.Z + fpixelAlphaInv*(Real)(pixel&0xff)/255.0f; //blue
#endif
				data[x] = REAL_TO_INT(rgb.X*255.0f)<<16 |	REAL_TO_INT(rgb.Y*255.0f)<<8 | REAL_TO_INT(rgb.Z*255.0f);
			}
			data[x] |= 0xff000000;	//force alpha to opaque.
		}
		data += pitch;
	}
}

//---------------------------------------------------------------------
/** Surface is assumed to come in the following format:
First 16 pixels are a palette composed of 24-Bit RGB values.
Any pixels in remainder of image that use these 24-bit values
will be remapped using a pre-defined formula.
*/
void W3DAssetManager::Remap_Palette(SurfaceClass *surface, const int color, Bool doPaletteOnly, Bool useAlpha)
{
//	unsigned int x;
	SurfaceClass::SurfaceDescription sd;
	surface->Get_Description(sd);
	int pitch,size;
//	UnsignedInt newPalette[TEAM_COLOR_PALETTE_SIZE];

	size=PixelSize(sd);
	unsigned char *bits=static_cast<unsigned char*>(surface->Lock(&pitch));

	if (doPaletteOnly)
	{	//only recolor the palette which is stored in top row.  Model only references these pixels.
		if (size == 2)
			remapPalette16Bit(&sd, reinterpret_cast<UnsignedShort *>(bits), color);
		else
		if (size == 4)
			remapPalette32Bit(&sd, reinterpret_cast<UnsignedInt *>(bits),color);
	}
	else
	{	
		if (useAlpha)
		{
			if (size == 2)
				remapAlphaTexture16Bit(sd.Width, sd.Height, pitch>>1, &sd, reinterpret_cast<UnsignedShort *>(bits),color);
			else
			if (size == 4)
				remapAlphaTexture32Bit(sd.Width, sd.Height, pitch>>2, &sd, reinterpret_cast<UnsignedInt *>(bits),color);
		}
		else
		{	//Recolor the image using the palette stored in top row
			if (size == 2)
				remapTexture16Bit(sd.Width, sd.Height-1, pitch>>1, &sd, reinterpret_cast<UnsignedShort *>(bits), reinterpret_cast<UnsignedShort *>(bits+pitch), color);
			else
			if (size == 4)
				remapTexture32Bit(sd.Width, sd.Height-1, pitch>>2, &sd, reinterpret_cast<UnsignedInt *>(bits), reinterpret_cast<UnsignedInt *>(bits+pitch),color);
		}
	}

	surface->Unlock();
}

//---------------------------------------------------------------------
TextureClass * W3DAssetManager::Recolor_Texture_One_Time(TextureClass *texture, const int color)
{
	const char *name=texture->Get_Texture_Name();	

	// if texture is procedural return NULL
	if (name && name[0]=='!') return nullptr;

	// make sure texture is loaded
	if (!texture->Is_Initialized())	
		TextureLoader::Request_Foreground_Loading(texture);

	// ZHCA: the pixel shader draws the team colour on a second handle of the same texture (RenderCore, ffp.ps.hlsl),
	// so one texture serves every side and need not be uncompressed. The CPU copy below remains for ZHCD (palette)
	// and for a backend without the view.
	if (*(name+3) == 'A' || *(name+3) == 'a')
	{
		WW3DTextureHandle view = RenderDevice::Create_Team_Color_View(texture->Peek_D3D_Base_Texture(), static_cast<unsigned>(color));
		if (view)
		{
			TextureClass *viewtex = NEW_REF(TextureClass, (view));
			RenderDevice::Release_Resource(view);	// the texture holds its own reference
			viewtex->Get_Filter().Set_Mag_Filter(texture->Get_Filter().Get_Mag_Filter());
			viewtex->Get_Filter().Set_Min_Filter(texture->Get_Filter().Get_Min_Filter());
			viewtex->Get_Filter().Set_Mip_Mapping(texture->Get_Filter().Get_Mip_Mapping());
			viewtex->Get_Filter().Set_U_Addr_Mode(texture->Get_Filter().Get_U_Addr_Mode());
			viewtex->Get_Filter().Set_V_Addr_Mode(texture->Get_Filter().Get_V_Addr_Mode());
			char viewname[512];
			Munge_Texture_Name(viewname, sizeof(viewname), name, color);
			viewtex->Set_Texture_Name(viewname);
			TextureHash.Insert(viewtex->Get_Texture_Name(), viewtex);
			viewtex->Add_Ref();
			return viewtex;
		}
	}

	SurfaceClass::SurfaceDescription desc;
	SurfaceClass *newsurf, *oldsurf;
	texture->Get_Level_Description(desc);		

	Int psize;
	psize=PixelSize(desc);
	DEBUG_ASSERTCRASH( psize == 2 || psize == 4, ("Can't Recolor Texture %s", name) );

	oldsurf=texture->Get_Surface_Level();

	newsurf=NEW_REF(SurfaceClass,(desc.Width,desc.Height,desc.Format));
	newsurf->Copy(0,0,0,0,desc.Width,desc.Height,oldsurf);

	if (*(name+3) == 'D' || *(name+3) == 'd')
		Remap_Palette(newsurf,color, true, false );	//texture only contains a palette stored in top row.
	else
	if (*(name+3) == 'A' || *(name+3) == 'a')
		Remap_Palette(newsurf,color, false, true );	//texture only contains a palette stored in top row.

	TextureClass * newtex=NEW_REF(TextureClass,(newsurf,(MipCountType)texture->Get_Mip_Level_Count()));
	newtex->Get_Filter().Set_Mag_Filter(texture->Get_Filter().Get_Mag_Filter());
	newtex->Get_Filter().Set_Min_Filter(texture->Get_Filter().Get_Min_Filter());
	newtex->Get_Filter().Set_Mip_Mapping(texture->Get_Filter().Get_Mip_Mapping());
	newtex->Get_Filter().Set_U_Addr_Mode(texture->Get_Filter().Get_U_Addr_Mode());
	newtex->Get_Filter().Set_V_Addr_Mode(texture->Get_Filter().Get_V_Addr_Mode());

	char newname[512];	
	Munge_Texture_Name(newname, sizeof(newname), name, color);
	newtex->Set_Texture_Name(newname);

	TextureHash.Insert(newtex->Get_Texture_Name(), newtex);
	newtex->Add_Ref();

	REF_PTR_RELEASE(oldsurf);
	REF_PTR_RELEASE(newsurf);

	return newtex;
}

#ifdef DUMP_PERF_STATS
__int64 Total_Create_Render_Obj_Time=0;
#endif
//---------------------------------------------------------------------
/** Generals specific code to generate customized render objects for each team color
	Scale==1.0, color==0x00000000, and oldTexure==NULL are defaults that do nothing.
*/
RenderObjClass * W3DAssetManager::Create_Render_Obj(
	const char * name,
	float scale, 
	const int color,
	const char *oldTexture, 
	const char *newTexture
)
{
	#ifdef DUMP_PERF_STATS
	__int64 startTime64,endTime64;
	GetPrecisionTimer(&startTime64);
	#endif

	Bool reallyscale = (WWMath::Fabs(scale - ident_scale) > scale_epsilon);
	Bool reallycolor = (color & 0xFFFFFF) != 0;	//black is not a valid color and assumes no custom coloring.
	Bool reallytexture = (oldTexture != nullptr && newTexture != nullptr);

	// base case, no scale or color
	if (!reallyscale && !reallycolor && !reallytexture) 
	{
		RenderObjClass *robj=WW3DAssetManager::Create_Render_Obj(name);
	#ifdef DUMP_PERF_STATS
		GetPrecisionTimer(&endTime64);
		Total_Create_Render_Obj_Time += endTime64-startTime64;
	#endif
		return robj;
	}

	char newname[512];
	Munge_Render_Obj_Name(newname, sizeof(newname), name, scale, color, newTexture);

	// see if we got a cached version
	RenderObjClass *rendobj = nullptr;

	Set_WW3D_Load_On_Demand(false); // munged name will never be found in a file.
	rendobj = WW3DAssetManager::Create_Render_Obj(newname);
	if (rendobj)
	{	//store the color that we used to create asset so we can read it back out
		//when we need to save this render object to a file.  Used during saving
		//of fog of war ghost objects.
		rendobj->Set_ObjectColor(color);
		Set_WW3D_Load_On_Demand(true); // Auto Load.

	#ifdef DUMP_PERF_STATS
		GetPrecisionTimer(&endTime64);
		Total_Create_Render_Obj_Time += endTime64-startTime64;
	#endif
		return rendobj;
	}

	// create a new one based on exisiting prototype

	WWPROFILE( "WW3DAssetManager::Create_Render_Obj" );

	// Try to find a prototype
	PrototypeClass * proto = Find_Prototype(name);

	Set_WW3D_Load_On_Demand(true); // Auto Load.
	if (WW3D_Load_On_Demand && proto == nullptr) 
	{	
		// If we didn't find one, try to load on demand
		char filename [MAX_PATH];
		const char *mesh_name = ::strchr (name, '.');
		if (mesh_name != nullptr) 
		{
			::lstrcpyn(filename, name, static_cast<int>(mesh_name - name) + 1 < MAX_PATH ? static_cast<int>(mesh_name - name) + 1 : MAX_PATH);	// pointer difference, not a truncated pointer
				safe_strcat(filename, ".w3d");
		} else {
			WWFormat::Into(filename, "{}.w3d", name);
		}

		// If we can't find it, try the parent directory
		if ( Load_3D_Assets( filename ) == false ) 
		{
			StringClass	new_filename = StringClass("..\\") + filename;
			if (Load_3D_Assets( new_filename ) == false)
			{
			}
		}

		proto = Find_Prototype(name);		// try again
	}

	if (proto == nullptr) 
	{
		static int warning_count = 0;
		if (++warning_count <= 20) 
		{
			WWDEBUG_SAY(("WARNING: Failed to create Render Object: %s\r\n",name));
		}
	#ifdef DUMP_PERF_STATS
		GetPrecisionTimer(&endTime64);
		Total_Create_Render_Obj_Time += endTime64-startTime64;
	#endif
		return nullptr;		// Failed to find a prototype
	}

	rendobj = Create_From_Prototype(proto);

	if (!rendobj) 
	{
	#ifdef DUMP_PERF_STATS
		GetPrecisionTimer(&endTime64);
		Total_Create_Render_Obj_Time += endTime64-startTime64;
	#endif
		return nullptr;
	}
	{	
		if (reallyscale)
			rendobj->Scale(scale);	//this also makes it unique

		Make_Unique(rendobj,reallyscale,reallycolor);

		if (reallytexture)
		{	
			TextureClass *oldTex = Get_Texture(oldTexture);
			TextureClass *newTex = Get_Texture(newTexture);
			replaceAssetTexture(rendobj,oldTex,newTex);
			REF_PTR_RELEASE(newTex);
			REF_PTR_RELEASE(oldTex);
		}

		if (reallycolor)
			Recolor_Asset(rendobj,color);
	}

	W3DPrototypeClass *w3dproto = newInstance(W3DPrototypeClass)(rendobj, newname);	
	rendobj->Release_Ref();
	Add_Prototype(w3dproto);

	rendobj = Create_From_Prototype(w3dproto);
	rendobj->Set_ObjectColor(color);

#ifdef DUMP_PERF_STATS
	GetPrecisionTimer(&endTime64);
	Total_Create_Render_Obj_Time += endTime64-startTime64;
#endif

	return rendobj;
}

//---------------------------------------------------------------------
/** Generals specific code to generate customized render objects for each team color
*/
int W3DAssetManager::Recolor_Asset(RenderObjClass *robj, const int color)
{
	switch (robj->Class_ID())	{	
	case RenderObjClass::CLASSID_MESH:
		return Recolor_Mesh(robj,color);
		break;
	case RenderObjClass::CLASSID_HLOD:
		return Recolor_HLOD(robj,color);
		break;
	}
	return 0;
}

//---------------------------------------------------------------------
/** Generals specific code to generate customized render objects for each team color
*/
int W3DAssetManager::Recolor_Mesh(RenderObjClass *robj, const int color)
{
	int i;
	int didRecolor=0;
	const char *meshName;

	MeshClass *mesh=static_cast<MeshClass*>(robj);	
	MeshModelClass * model = mesh->Get_Model();
	MaterialInfoClass	*material = mesh->Get_Material_Info();

	// recolor vertex material (assuming mesh is housecolor)
	if ( (( (meshName=strchr(mesh->Get_Name(),'.') ) != 0 && *(meshName++)) || ( (meshName=mesh->Get_Name()) != nullptr)) &&
		_strnicmp(meshName,"HOUSECOLOR", 10) == 0)
	{	for (i=0; i<material->Vertex_Material_Count(); i++)
			Recolor_Vertex_Material(material->Peek_Vertex_Material(i),color);
		didRecolor=1;
	}

	// recolor textures
	TextureClass *newtex,*oldtex;
	for (i=0; i<material->Texture_Count(); i++)
	{
		oldtex=material->Peek_Texture(i);
		if (_strnicmp(oldtex->Get_Texture_Name(),"ZHC", 3) == 0)
		{	//This texture needs to be adjusted for housecolor
			newtex=Recolor_Texture(oldtex,color);
			if (newtex)
			{
				model->Replace_Texture(oldtex,newtex);
				material->Replace_Texture(i,newtex);
				REF_PTR_RELEASE(newtex);
				didRecolor=1;
			}
		}
	}

	REF_PTR_RELEASE(material);	
	REF_PTR_RELEASE(model);
	return didRecolor;
}

//---------------------------------------------------------------------
/** Generals specific code to generate customized render objects for each team color
*/

int W3DAssetManager::Recolor_HLOD(RenderObjClass *robj, const int color)
{
	int didRecolor=0;

	int num_sub = robj->Get_Num_Sub_Objects();
	for(int i = 0; i < num_sub; i++) {
		RenderObjClass *sub_obj = robj->Get_Sub_Object(i);
		didRecolor |= Recolor_Asset(sub_obj,color);
		REF_PTR_RELEASE(sub_obj);
	}
	return didRecolor;
}

//---------------------------------------------------------------------
/** Generals specific code to generate customized render objects for each team color
*/
void W3DAssetManager::Recolor_Vertex_Material(VertexMaterialClass *vmat, const int color)
{
	Vector3 rgb,rgb2;

	rgb.X = static_cast<Real>((color >> 16) & 0xff )/255.0f;
	rgb.Y = static_cast<Real>((color >> 8) & 0xff )/255.0f;
	rgb.Z = static_cast<Real>(color & 0xff)/255.0f;

	//We ignore the existing ambinent/diffuse and assume they were 1.0.  We can change
	//to scaling them if required.

//	vmat->Get_Ambient(&rgb2);
//	Recolor(rgb,hsv_shift);
	rgb2.X = rgb.X;	//scale colors
	rgb2.Y = rgb.Y;	//scale colors
	rgb2.Z = rgb.Z;	//scale colors
	vmat->Set_Ambient(rgb2);

//	vmat->Get_Diffuse(&rgb2);
//	Recolor(rgb,hsv_shift);
	rgb2.X = rgb.X;	//scale colors
	rgb2.Y = rgb.Y;	//scale colors
	rgb2.Z = rgb.Z;	//scale colors
	vmat->Set_Diffuse(rgb2);
}

#ifdef DUMP_PERF_STATS
__int64 Total_Load_3D_Assets=0;
static Int Load_3D_Asset_Recursions=0;
#endif
//---------------------------------------------------------------------
bool W3DAssetManager::Load_3D_Assets( const char * filename )
{
#ifdef DUMP_PERF_STATS
		Load_3D_Asset_Recursions++;

		__int64 startTime64,endTime64;
		GetPrecisionTimer(&startTime64);
#endif


	// Try to find an existing prototype
	char basename[512];
	safe_strcpy(basename, filename);
	char *pext = strrchr(basename, '.');	//find file extension
	if (pext)
		*pext = '\0';	//drop the extension
	PrototypeClass * proto = Find_Prototype(basename);
	if (proto)
	{
#ifdef DUMP_PERF_STATS
		if (Load_3D_Asset_Recursions == 1)
		{	GetPrecisionTimer(&endTime64);
			Total_Load_3D_Assets += endTime64-startTime64;
		}
		Load_3D_Asset_Recursions--;
#endif
		return TRUE;	//this file has already been loaded.
	}

	bool result = WW3DAssetManager::Load_3D_Assets(filename);

#if defined(_DEBUG)
	if (result && TheGlobalData->m_preloadReport)
	{	
		//loading a new asset and app is requesting a log of all loaded assets.
		FILE *logfile=fopen("PreloadedAssets.txt","a+");	//append to log
		if (logfile)
		{	
			StringClass lower_case_name(filename,true);
			_strlwr(lower_case_name.Peek_Buffer());
			fprintf(logfile,"3D: %s\n",lower_case_name.Peek_Buffer());
			fclose(logfile);
		}
	}
#endif
#ifdef DUMP_PERF_STATS
	if (Load_3D_Asset_Recursions == 1)
	{	GetPrecisionTimer(&endTime64);
		Total_Load_3D_Assets += endTime64-startTime64;
	}
	Load_3D_Asset_Recursions--;
#endif
	return result;

}

#ifdef DUMP_PERF_STATS
__int64 Total_Get_HAnim_Time=0;
static Int HAnim_Recursions=0;
#endif
//---------------------------------------------------------------------
HAnimClass *	W3DAssetManager::Get_HAnim(const char * name)
{
#ifdef DUMP_PERF_STATS
	HAnim_Recursions++;

	__int64 startTime64,endTime64;
	GetPrecisionTimer(&startTime64);
#endif
	WWPROFILE( "WW3DAssetManager::Get_HAnim" );

	{
		HAnimClass *anim=WW3DAssetManager::Get_HAnim(name);	//we only do custom granny processing.
#ifdef DUMP_PERF_STATS
		if (HAnim_Recursions == 1)
		{
			GetPrecisionTimer(&endTime64);
			Total_Get_HAnim_Time += endTime64-startTime64;
		}
		HAnim_Recursions--;
#endif
		return anim;
	}

}

//---------------------------------------------------------------------
// Uniqing
//---------------------------------------------------------------------

//---------------------------------------------------------------------
/** Generals specific code to generate customized render objects for each team color
*/
void W3DAssetManager::Make_HLOD_Unique(RenderObjClass *robj, Bool geometry, Bool colors)
{
	int num_sub = robj->Get_Num_Sub_Objects();
	for(int i = 0; i < num_sub; i++) {
		RenderObjClass *sub_obj = robj->Get_Sub_Object(i);
		Make_Unique(sub_obj, geometry, colors);
		REF_PTR_RELEASE(sub_obj);
	}
}

//---------------------------------------------------------------------
/** Generals specific code to generate customized render objects for each team color
*/
void W3DAssetManager::Make_Unique(RenderObjClass *robj, Bool geometry, Bool colors)
{
	switch (robj->Class_ID())	{	
	case RenderObjClass::CLASSID_MESH:
		Make_Mesh_Unique(robj,geometry,colors);
		break;
	case RenderObjClass::CLASSID_HLOD:
		Make_HLOD_Unique(robj,geometry,colors);
		break;
	}
}

//---------------------------------------------------------------------
/** Determine what method is used to apply house color to this mesh (if any) */
static Bool getMeshColorMethods(MeshClass *mesh, Bool &vertexColor, Bool &textureColor)
{
	vertexColor = false;
	textureColor = false;

	//Check if mesh is using custom texture containing house color
	MaterialInfoClass *material = mesh->Get_Material_Info();
	if (material)
	{	for (int j=0; j<material->Texture_Count(); j++)
			if (_strnicmp(material->Peek_Texture(j)->Get_Texture_Name(),"ZHC",3) == 0)
			{	textureColor = true;
				break;
			}
		REF_PTR_RELEASE(material);
	}

	//Check if mesh is using a custom mesh which contains house color in material.
	//Meshes which are part of another model have names in the form "name.name" while
	//isolated meshes are just "name".  We check for both starting with "HOUSECOLOR".
	const char *meshName;
	if ( ( (meshName=strchr(mesh->Get_Name(),'.') ) != 0 && *(meshName++)) || ( (meshName=mesh->Get_Name()) != nullptr) )
	{	//Check if this object has housecolors on mesh
		if ( _strnicmp(meshName,"HOUSECOLOR", 10) == 0)
			vertexColor = true;
	}

	return (vertexColor || textureColor);
}

//---------------------------------------------------------------------
/** Generals specific code to generate customized render objects for each team color
*/
void W3DAssetManager::Make_Mesh_Unique(RenderObjClass *robj, Bool geometry, Bool colors)
{
	int i;
	MeshClass *mesh=static_cast<MeshClass*>(robj);
	Bool isVertexColor = false, isTextureColor = false;

	//figure out what type of coloring this mesh requires (if any)
	if ((colors && getMeshColorMethods(mesh,isVertexColor,isTextureColor)) || geometry)
	{	//mesh has some house color applied so make those components unique to mesh.

		//Create unique data for this mesh
		if (!geometry)	//scaling geometry automatically makes it unique so not needed here.
			mesh->Make_Unique();
		
		MeshModelClass * model = mesh->Get_Model();

		if (colors && isVertexColor)
		{
			MaterialInfoClass	*material=mesh->Get_Material_Info();
			for (i=0; i<material->Vertex_Material_Count(); i++)
				material->Peek_Vertex_Material(i)->Make_Unique();	
			REF_PTR_RELEASE(material);
		}

		REF_PTR_RELEASE(model);
	}
}

//---------------------------------------------------------------------
/**Report prototypes that have all assets with reference count
equal to 1*/
void W3DAssetManager::Report_Used_Prototypes(void)
{
	int count = Prototypes.Count();
	while (count-- > 0) {

		PrototypeClass * proto = Prototypes[count];
		if (proto->Get_Class_ID() == RenderObjClass::CLASSID_HLOD || proto->Get_Class_ID() == RenderObjClass::CLASSID_MESH)
		{
			DEBUG_LOG(("**Unfreed Prototype On Map Reset: %s\n",proto->Get_Name()));
		}
	}
}

//---------------------------------------------------------------------
/**Report any assets with reference counts > 1.  This means they are still
referenced by something besides the asset manager.*/
void W3DAssetManager::Report_Used_Assets(void)
{
	Report_Used_Prototypes();

	///@todo: Report unfreed skeletons and animations
	//	HAnimManager.Free_All_Anims();
	//	HTreeManager.Free_All_Trees();

	Report_Used_Textures();
	Report_Used_Font3DDatas();
	Report_Used_FontChars();
}

//---------------------------------------------------------------------
void W3DAssetManager::Report_Used_FontChars(void)
{
	Int count=FontCharsList.Count();

	while (count-- > 0)
	{
		if (FontCharsList[count]->Num_Refs() >= 1)
		{
			DEBUG_LOG(("**Unfreed FontChar On Map Reset: %s\n",FontCharsList[count]->Get_Name()));
			//FontCharsList[count]->Release_Ref();
			//FontCharsList.Delete(count);
		}
	}
}

//---------------------------------------------------------------------
/**Report all textures with refcounts >= 1*/
void W3DAssetManager::Report_Used_Textures(void)
{
	/*
	** for each texture in the list, get it, check it's refcount, and and release ref it if the
	** refcount is one.
	*/

//	unsigned count=0;
//	TextureClass* temp_textures[256];

	HashTemplateIterator<StringClass,TextureClass*> ite(TextureHash);
	for (ite.First();!ite.Is_Done();ite.Next()) {
		TextureClass* tex=ite.Peek_Value();
		if (tex->Num_Refs() <= 1) {
	/*		temp_textures[count++]=tex;
			if (count==256) {
				for (unsigned i=0;i<256;++i) {
					TextureHash.Remove(temp_textures[i]->Get_Texture_Name());
					temp_textures[i]->Release_Ref();
				}
				count=0;
				ite.First();	// iterator doesn't support modifying the hash table while iterating, so start from the
									// beginning.
			}*/
		}
		else
		{
			DEBUG_LOG(("**Texture \"%s\" referenced %d times on map reset\n",tex->Get_Texture_Name().Peek_Buffer(),tex->Num_Refs()-1));
		}
	}
/*	for (unsigned i=0;i<count;++i) {
		TextureHash.Remove(temp_textures[i]->Get_Texture_Name());
		temp_textures[i]->Release_Ref();
	}*/
}

//---------------------------------------------------------------------
/**Report all used fonts*/
void W3DAssetManager::Report_Used_Font3DDatas( void )
{
	/*
	** for each font data in the list, get it, check it's refcount, and and release ref it if the
	** refcount is one.
	*/
	SLNode<Font3DDataClass> *node, * next;
	for (	node = Font3DDatas.Head(); node; node = next) {
		next = node->Next();
		Font3DDataClass *font = node->Data();
		if (font->Num_Refs() == 1) {
/*			Font3DDatas.Remove(font);
			font->Release_Ref();*/
		}
		else
		{
			DEBUG_LOG(("**Unfreed Font3DDatas On Map Reset: %s\n",font->Name));
		}
	}
}

//---------------------------------------------------------------------
// E&B Coloring Code
//---------------------------------------------------------------------
/*

//---------------------------------------------------------------------
// Uniqing
//---------------------------------------------------------------------

void W3DAssetManager::Make_Mesh_Unique(RenderObjClass *robj, Bool geometry, Bool colors)
{
	int i;
	MeshClass *mesh=(MeshClass*) robj;
	mesh->Make_Unique();
	MeshModelClass * model = mesh->Get_Model();

	if (colors)	{
		// make all vertex materials unique
		MaterialInfoClass	*material = mesh->Get_Material_Info();
		for (i=0; i<material->Vertex_Material_Count(); i++)
			material->Peek_Vertex_Material(i)->Make_Unique();	
		REF_PTR_RELEASE(material);
		// make all color arrays unique
		model->Make_Color_Array_Unique(0);
		model->Make_Color_Array_Unique(1);
		// do not do textures yet
		// because we want to do the color conversion
		// for the top mip level and then
		// mip filter instead of converting all mip levels
	}
		
	if (geometry)	{
		// make geometry unique		
		model->Make_Geometry_Unique();		
	}

	REF_PTR_RELEASE(model);
}

void W3DAssetManager::Make_HLOD_Unique(RenderObjClass *robj, Bool geometry, Bool colors)
{
	int num_sub = robj->Get_Num_Sub_Objects();
	for(int i = 0; i < num_sub; i++) {
		RenderObjClass *sub_obj = robj->Get_Sub_Object(i);
		Make_Unique(sub_obj,geometry,colors);
		REF_PTR_RELEASE(sub_obj);
	}
}

void W3DAssetManager::Make_Unique(RenderObjClass *robj, Bool geometry, Bool colors)
{
	switch (robj->Class_ID())	{	
	case RenderObjClass::CLASSID_MESH:
		Make_Mesh_Unique(robj,geometry,colors);
		break;
	case RenderObjClass::CLASSID_HLOD:
		Make_HLOD_Unique(robj,geometry,colors);
		break;
	}
}

static inline void Munge_Render_Obj_Name(char *newname, const char *oldname, float scale, const Vector3 &hsv_shift)
{
	char lower_case_name[255];
	strcpy(lower_case_name, oldname);
	_strlwr(lower_case_name);
	WWFormat::Into(newname, "#{}!{:g}H{:g}S{:g}V{:g}", lower_case_name, scale, hsv_shift.X, hsv_shift.Y, hsv_shift.Z);
}

static inline void Munge_Texture_Name(char *newname, const char *oldname, const Vector3 &hsv_shift)
{
	char lower_case_name[255];
	strcpy(lower_case_name, oldname);
	_strlwr(lower_case_name);
	WWFormat::Into(newname, "#{}!H{:g}S{:g}V{:g}", lower_case_name, hsv_shift.X, hsv_shift.Y, hsv_shift.Z);
}

RenderObjClass * W3DAssetManager::Create_Render_Obj(const char * name,float scale, const Vector3 &hsv_shift)
{
	Bool isGranny = false;
	Bool reallyscale = (WWMath::Fabs(scale - ident_scale) > scale_epsilon);
	Bool reallyhsv_shift = (WWMath::Fabs(hsv_shift.X - ident_HSV.X) > H_epsilon ||
		WWMath::Fabs(hsv_shift.Y - ident_HSV.Y) > S_epsilon || WWMath::Fabs(hsv_shift.Z - ident_HSV.Z) > V_epsilon);

	// base case, no scale or hue shifting
	if (!reallyscale && !reallyhsv_shift) return WW3DAssetManager::Create_Render_Obj(name);

	char newname[512];
	Munge_Render_Obj_Name(newname, name, scale, hsv_shift);

	// see if we got a cached version
	RenderObjClass *rendobj=NULL;

	if (isGranny)
	{	//Granny objects share the same prototype since they allow instance scaling.
		strcpy(newname,name);	//use same name for all granny objects at any scale.
	}
	Set_WW3D_Load_On_Demand(false); // munged name will never be found in a file.
	rendobj=WW3DAssetManager::Create_Render_Obj(newname);
	if (rendobj)
	{	if (isGranny)
			///@todo Granny objects are realtime scaled - fix to scale like W3D.
			rendobj->Set_ObjectScale(scale);
		Set_WW3D_Load_On_Demand(true); // Auto Load.
		return rendobj;
	}

	// create a new one based on
	// exisiting prototype

	WWPROFILE( "WW3DAssetManager::Create_Render_Obj" );

	// Try to find a prototype
	PrototypeClass * proto = Find_Prototype(name);

	Set_WW3D_Load_On_Demand(true); // Auto Load.
	if (WW3D_Load_On_Demand && proto == NULL) {	// If we didn't find one, try to load on demand
		char filename [MAX_PATH];
		const char *mesh_name = ::strchr (name, '.');
		if (mesh_name != NULL) {
			::lstrcpyn (filename, name, ((int)mesh_name) - ((int)name) + 1);
			if (isGranny)
				::lstrcat (filename, ".gr2");
			else
				::lstrcat (filename, ".w3d");
		} else {
			WWFormat::Into(filename, "{}.w3d", name);
		}

		// If we can't find it, try the parent directory
		if ( Load_3D_Assets( filename ) == false ) {
			StringClass	new_filename = StringClass("..\\") + filename;
			if (Load_3D_Assets( new_filename ) == false)
			{
				char *mesh_name = ::strchr (filename, '.');
				::lstrcpyn (mesh_name, ".gr2",5);
				Load_3D_Assets( filename );
				isGranny=true;
			}
		}

		proto = Find_Prototype(name);		// try again
	}

	if (proto == NULL) {
		static int warning_count = 0;
		if (++warning_count <= 20) {
			WWDEBUG_SAY(("WARNING: Failed to create Render Object: %s\r\n",name));
		}
		return NULL;		// Failed to find a prototype
	}

	rendobj=Create_From_Prototype(proto);

	if (!rendobj) return NULL;
	
	if (!isGranny)
	{	Make_Unique(rendobj,reallyscale,reallyhsv_shift);
		if (reallyscale) rendobj->Scale(scale);
		if (reallyhsv_shift) Recolor_Asset(rendobj,hsv_shift);
	}
	else
	{	///@todo Granny objects are realtime scaled - fix to scale like W3D.
		rendobj->Set_ObjectScale(scale);
		return rendobj;
	}

	W3DPrototypeClass *w3dproto=NEW W3DPrototypeClass(rendobj,newname);
	rendobj->Release_Ref();
	Add_Prototype(w3dproto);

	return Create_From_Prototype(w3dproto);
}

TextureClass * W3DAssetManager::Get_Texture_With_HSV_Shift(const char * filename, const Vector3 &hsv_shift, TextureClass::MipCountType mip_level_count)
{
	WWPROFILE( "W3DAssetManager::Get_Texture with HSV shift" );

	Bool is_hsv_shift = (WWMath::Fabs(hsv_shift.X - ident_HSV.X) > H_epsilon ||
		WWMath::Fabs(hsv_shift.Y - ident_HSV.Y) > S_epsilon || WWMath::Fabs(hsv_shift.Z - ident_HSV.Z) > V_epsilon);

	if (!is_hsv_shift) {

		return Get_Texture(filename, mip_level_count);

	} else {

		//
		// Bail if the user isn't really asking for anything
		//
		if ((filename == NULL) || (strlen(filename) == 0)) {
			return NULL;
		}

		TextureClass *newtex = Find_Texture(filename, hsv_shift);

		if (!newtex) {
		
			// No cached texture - need to create
			char lower_case_name[255];
			strcpy(lower_case_name, filename);
			_strlwr(lower_case_name);
			TextureClass *oldtex = TextureHash.Get(lower_case_name);
			if (!oldtex) {
				oldtex = NEW_REF(TextureClass,(lower_case_name, NULL, mip_level_count));
				TextureHash.Insert(oldtex->Get_Texture_Name(), oldtex);
			}

			newtex = Recolor_Texture_One_Time(oldtex, hsv_shift);

			// If the recolorization failed, return the original texture
			if (!newtex) {
				newtex = oldtex;
				newtex->Add_Ref();
			}

		}

		return newtex;

	}
}

void W3DAssetManager::Recolor_Vertex_Material(VertexMaterialClass *vmat, const Vector3 &hsv_shift)
{
	Vector3 rgb;

	vmat->Get_Ambient(&rgb);
	Recolor(rgb,hsv_shift);
	vmat->Set_Ambient(rgb);

	vmat->Get_Diffuse(&rgb);
	Recolor(rgb,hsv_shift);
	vmat->Set_Diffuse(rgb);

	vmat->Get_Emissive(&rgb);
	Recolor(rgb,hsv_shift);
	vmat->Set_Emissive(rgb);

	vmat->Get_Specular(&rgb);
	Recolor(rgb,hsv_shift);
	vmat->Set_Specular(rgb);
}

void W3DAssetManager::Recolor_Vertices(unsigned int *color, int count, const Vector3 &hsv_shift)
{
	int i;	
	Vector4 rgba;	

	for (i=0; i<count; i++)
	{
		rgba=RenderDevice::Convert_Color(color[i]);
		Recolor(reinterpret_cast<Vector3&>(rgba),hsv_shift);
		color[i]=RenderDevice::Convert_Color_Clamp(rgba);
	}
}

TextureClass * W3DAssetManager::Recolor_Texture(TextureClass *texture, const Vector3 &hsv_shift)
{
	const char *name=texture->Get_Texture_Name();	

	TextureClass *newtex = Find_Texture(name, hsv_shift);
	if (newtex) {
		return newtex;
	}

	return Recolor_Texture_One_Time(texture, hsv_shift);
}

TextureClass * W3DAssetManager::Recolor_Texture_One_Time(TextureClass *texture, const Vector3 &hsv_shift)
{
	const char *name=texture->Get_Texture_Name();	

	// if texture is procedural return NULL
	if (name && name[0]=='!') return NULL;

	// make sure texture is loaded
	if (!texture->Is_Initialized())	
		TextureLoader::Request_High_Priority_Loading(texture, (TextureClass::MipCountType)texture->Get_Mip_Level_Count());

	SurfaceClass::SurfaceDescription desc;
	SurfaceClass *newsurf, *oldsurf, *smallsurf;
	texture->Get_Level_Description(desc);		

	// if texture is monochrome and no value shifting
	// return NULL	
	smallsurf=texture->Get_Surface_Level((TextureClass::MipCountType)texture->Get_Mip_Level_Count()-1);
	if (hsv_shift.Z==0.0f && smallsurf->Is_Monochrome())
	{
		REF_PTR_RELEASE(smallsurf);
		return NULL;
	}
	REF_PTR_RELEASE(smallsurf);

	oldsurf=texture->Get_Surface_Level();

	newsurf=NEW_REF(SurfaceClass,(desc.Width,desc.Height,desc.Format));
	newsurf->Copy(0,0,0,0,desc.Width,desc.Height,oldsurf);
	newsurf->Hue_Shift(hsv_shift);
	TextureClass * newtex=NEW_REF(TextureClass,(newsurf,(TextureClass::MipCountType)texture->Get_Mip_Level_Count()));
	newtex->Set_Mag_Filter(texture->Get_Mag_Filter());
	newtex->Set_Min_Filter(texture->Get_Min_Filter());
	newtex->Set_Mip_Mapping(texture->Get_Mip_Mapping());
	newtex->Set_U_Addr_Mode(texture->Get_U_Addr_Mode());
	newtex->Set_V_Addr_Mode(texture->Get_V_Addr_Mode());

	char newname[512];	
	Munge_Texture_Name(newname, name, hsv_shift);
	newtex->Set_Texture_Name(newname);

	TextureHash.Insert(newtex->Get_Texture_Name(), newtex);
	newtex->Add_Ref();

	REF_PTR_RELEASE(oldsurf);
	REF_PTR_RELEASE(newsurf);

	return newtex;
}

TextureClass * W3DAssetManager::Find_Texture(const char * name, const Vector3 &hsv_shift)
{
	char newname[512];	
	Munge_Texture_Name(newname, name, hsv_shift);

	// see if we have a cached copy
	TextureClass *newtex = TextureHash.Get(newname);
	if (newtex) {
		newtex->Add_Ref();
	}
	return newtex;
}

void W3DAssetManager::Recolor_Mesh(RenderObjClass *robj, const Vector3 &hsv_shift)
{
	int i;

	MeshClass *mesh=(MeshClass*) robj;	
	MeshModelClass * model = mesh->Get_Model();
	MaterialInfoClass	*material = mesh->Get_Material_Info();

	// recolor vertex material
	for (i=0; i<material->Vertex_Material_Count(); i++)
			Recolor_Vertex_Material(material->Peek_Vertex_Material(i),hsv_shift);	

	// recolor color arrays
	unsigned int * color;
	color=model->Get_Color_Array(0,false);
	if (color) Recolor_Vertices(color,model->Get_Vertex_Count(),hsv_shift);
	color=model->Get_Color_Array(1,false);
	if (color) Recolor_Vertices(color,model->Get_Vertex_Count(),hsv_shift);

	// recolor textures

	TextureClass *newtex,*oldtex;
	for (i=0; i<material->Texture_Count(); i++)
	{
		oldtex=material->Peek_Texture(i);
		newtex=Recolor_Texture(oldtex,hsv_shift);
		if (newtex)
		{
			model->Replace_Texture(oldtex,newtex);
			material->Replace_Texture(i,newtex);
			REF_PTR_RELEASE(newtex);
		}
	}

	REF_PTR_RELEASE(material);	
	REF_PTR_RELEASE(model);
}

void W3DAssetManager::Recolor_HLOD(RenderObjClass *robj, const Vector3 &hsv_shift)
{
	int num_sub = robj->Get_Num_Sub_Objects();
	for(int i = 0; i < num_sub; i++) {
		RenderObjClass *sub_obj = robj->Get_Sub_Object(i);
		Recolor_Asset(sub_obj,hsv_shift);
		REF_PTR_RELEASE(sub_obj);
	}
}

void W3DAssetManager::Recolor_ParticleEmitter(RenderObjClass *robj, const Vector3 &hsv_shift)
{
	unsigned int i;

	ParticleEmitterClass* emit=(ParticleEmitterClass*) robj;
	ParticlePropertyStruct<Vector3> colors;

	emit->Get_Color_Key_Frames(colors);
	Recolor(colors.Start,hsv_shift);
	Recolor(colors.Rand,hsv_shift);
	for (i=0; i<colors.NumKeyFrames; i++)
		Recolor(colors.Values[i],hsv_shift);
	emit->Reset_Colors(colors);

	delete colors.Values;
	delete colors.KeyTimes;

	TextureClass *tex=emit->Get_Texture();
	TextureClass *newtex=Recolor_Texture(tex,hsv_shift);
	if (newtex)
	{
		emit->Set_Texture(newtex);
		REF_PTR_RELEASE(newtex);
	}
	REF_PTR_RELEASE(tex);
}

void W3DAssetManager::Recolor_Asset(RenderObjClass *robj, const Vector3 &hsv_shift)
{
	switch (robj->Class_ID())	{	
	case RenderObjClass::CLASSID_MESH:
		Recolor_Mesh(robj,hsv_shift);
		break;
	case RenderObjClass::CLASSID_HLOD:
		Recolor_HLOD(robj,hsv_shift);
		break;
	case RenderObjClass::CLASSID_PARTICLEEMITTER:
		Recolor_ParticleEmitter(robj,hsv_shift);
		break;
	}
}
*/

//---------------------------------------------------------------------
// -modellPruefung
//---------------------------------------------------------------------
namespace
{
	/// A top-level chunk of a model file as the raw bytes show it: id and the name its header names.
	struct ModelChunk
	{
		unsigned id;
		std::string name;
	};

	std::string fixedName(const unsigned char *p, size_t length)
	{
		size_t n = 0;
		while (n < length && p[n] != 0)
			++n;
		return std::string(reinterpret_cast<const char *>(p), n);
	}

	/// Top-level chunks of a model file with the name of what each defines (empty if not known).
	std::vector<ModelChunk> listModelChunks(const std::vector<unsigned char> &data)
	{
		std::vector<ModelChunk> chunks;
		size_t pos = 0;
		while (pos + 8 <= data.size())
		{
			unsigned id = 0, size = 0;
			memcpy(&id, &data[pos], 4);
			memcpy(&size, &data[pos + 4], 4);
			size &= 0x7FFFFFFF;
			const size_t body = pos + 8;
			if (size > data.size() - body)
			{
				chunks.push_back({ id, std::string() });
				break;
			}
			std::string name;
			// first sub chunk: the header of the object
			if (size >= 8)
			{
				unsigned subSize = 0;
				memcpy(&subSize, &data[body + 4], 4);
				subSize &= 0x7FFFFFFF;
				const unsigned char *h = &data[body + 8];
				const size_t avail = size - 8 < subSize ? size - 8 : subSize;
				switch (id)
				{
					case W3D_CHUNK_HIERARCHY:
						if (avail >= 4 + W3D_NAME_LEN)
							name = fixedName(h + 4, W3D_NAME_LEN);
						break;
					case W3D_CHUNK_ANIMATION:
					case W3D_CHUNK_COMPRESSED_ANIMATION:
						if (avail >= 4 + 2 * W3D_NAME_LEN)
							name = fixedName(h + 4 + W3D_NAME_LEN, W3D_NAME_LEN) + "." + fixedName(h + 4, W3D_NAME_LEN);
						break;
					case W3D_CHUNK_MESH:
						if (avail >= 8 + 2 * W3D_NAME_LEN)
						{
							const std::string mesh = fixedName(h + 8, W3D_NAME_LEN);
							const std::string container = fixedName(h + 8 + W3D_NAME_LEN, W3D_NAME_LEN);
							name = container.empty() ? mesh : container + "." + mesh;
						}
						break;
					case W3D_CHUNK_HLOD:
						if (avail >= 8 + W3D_NAME_LEN)
							name = fixedName(h + 8, W3D_NAME_LEN);
						break;
					default:
						break;
				}
			}
			chunks.push_back({ id, name });
			pos = body + size;
		}
		return chunks;
	}

	bool readWholeFile(const AsciiString &path, std::vector<unsigned char> &data)
	{
		File *file = TheFileSystem->openFile(path.str(), File::READ | File::BINARY);
		if (file == nullptr)
			return false;
		const Int size = file->size();
		data.resize(size > 0 ? static_cast<size_t>(size) : 0);
		const Bool ok = size >= 0 && file->read(data.data(), size) == size;
		file->close();
		return ok;
	}

	const char *baseName(const AsciiString &path)
	{
		const char *slash = strrchr(path.str(), '\\');
		const char *other = strrchr(path.str(), '/');
		if (other != nullptr && (slash == nullptr || other > slash))
			slash = other;
		return slash != nullptr ? slash + 1 : path.str();
	}
}

int W3DAssetManager::Check_Model_Files(const char *outPath)
{
	FILE *out = fopen(outPath, "w");
	if (out == nullptr || TheFileSystem == nullptr)
	{
		if (out != nullptr)
			fclose(out);
		return -1;
	}
	const bool onDemand = Get_WW3D_Load_On_Demand();
	Set_WW3D_Load_On_Demand(false);

	enum { KIND_MESH, KIND_HIERARCHY, KIND_ANIMATION, KIND_HLOD, KIND_OTHER, KIND_COUNT };
	static const char *const kindNames[KIND_COUNT] = { "meshes", "hierarchies", "animations", "hlods", "other" };
	int loaded[KIND_COUNT] = {}, duplicate[KIND_COUNT] = {}, rejected[KIND_COUNT] = {};
	int files = 0, unreadable = 0;

	FilenameList modelFiles;
	TheFileSystem->getFileListInDirectory(AsciiString("Art\\W3D\\"), AsciiString("*.w3d"), modelFiles, TRUE);
	for (FilenameListIter it = modelFiles.begin(); it != modelFiles.end(); ++it)
	{
		std::vector<unsigned char> data;
		if (!readWholeFile(*it, data))
		{
			fprintf(out, "MODELCHECK unreadable %s\n", it->str());
			++unreadable;
			continue;
		}
		++files;
		const std::vector<ModelChunk> chunks = listModelChunks(data);
		file_auto_ptr file(_TheFileFactory, baseName(*it));
		if (!file->Open())
		{
			fprintf(out, "MODELCHECK unreadable %s\n", it->str());
			++unreadable;
			continue;
		}
		ChunkLoadClass cload(file);
		size_t index = 0;
		for (; index < chunks.size() && cload.Open_Chunk(); ++index)
		{
			const ModelChunk &chunk = chunks[index];
			const unsigned id = cload.Cur_Chunk_ID();
			int kind = KIND_OTHER;
			bool existed = false;
			bool ok = false;
			if (id == W3D_CHUNK_HIERARCHY)
			{
				kind = KIND_HIERARCHY;
				existed = !chunk.name.empty() && HTreeManager.Get_Tree_ID(chunk.name.c_str()) != -1;
				ok = HTreeManager.Load_Tree(cload) == 0;
			}
			else if (id == W3D_CHUNK_ANIMATION || id == W3D_CHUNK_COMPRESSED_ANIMATION || id == W3D_CHUNK_MORPH_ANIMATION)
			{
				kind = KIND_ANIMATION;
				existed = !chunk.name.empty() && HAnimManager.Peek_Anim(chunk.name.c_str()) != nullptr;
				// The game loads the hierarchy file of an animation on demand; files are visited in name
				// order, so the skeleton often comes after its animations.
				Set_WW3D_Load_On_Demand(true);
				ok = HAnimManager.Load_Anim(cload) == 0;
				Set_WW3D_Load_On_Demand(false);
			}
			else
			{
				kind = (id == W3D_CHUNK_MESH) ? KIND_MESH : (id == W3D_CHUNK_HLOD ? KIND_HLOD : KIND_OTHER);
				existed = !chunk.name.empty() && Render_Obj_Exists(chunk.name.c_str());
				ok = Load_Prototype(cload);
			}
			cload.Close_Chunk();
			if (ok)
				++loaded[kind];
			else if (existed)
				++duplicate[kind];
			else
			{
				++rejected[kind];
				fprintf(out, "MODELCHECK rejected %s chunk %d id 0x%X name %s\n", it->str(), static_cast<int>(index), id,
					chunk.name.empty() ? "?" : chunk.name.c_str());
			}
		}
		// chunks the reader did not reach (a damaged length stops it)
		for (; index < chunks.size(); ++index)
		{
			++rejected[KIND_OTHER];
			fprintf(out, "MODELCHECK rejected %s chunk %d id 0x%X name %s (not reached)\n", it->str(), static_cast<int>(index),
				chunks[index].id, chunks[index].name.empty() ? "?" : chunks[index].name.c_str());
		}
		file->Close();
	}

	// DDS textures: header and data as the texture loader reads them
	int ddsFiles = 0, ddsRejected = 0;
	FilenameList textureFiles;
	TheFileSystem->getFileListInDirectory(AsciiString("Art\\Textures\\"), AsciiString("*.dds"), textureFiles, TRUE);
	for (FilenameListIter it = textureFiles.begin(); it != textureFiles.end(); ++it)
	{
		++ddsFiles;
		DDSFileClass dds(baseName(*it), 0);
		if (!dds.Is_Available() || !dds.Load())
		{
			++ddsRejected;
			fprintf(out, "MODELCHECK rejected %s (DDS)\n", it->str());
		}
	}

	int totalRejected = ddsRejected + unreadable;
	fprintf(out, "MODELCHECK files %d unreadable %d", files, unreadable);
	for (int k = 0; k < KIND_COUNT; ++k)
	{
		fprintf(out, " %s %d/%d/%d", kindNames[k], loaded[k], duplicate[k], rejected[k]);
		totalRejected += rejected[k];
	}
	fprintf(out, " (loaded/duplicate/rejected) dds %d rejected %d; rejected total %d\n", ddsFiles, ddsRejected, totalRejected);
	fclose(out);
	Set_WW3D_Load_On_Demand(onDemand);
	return totalRejected;
}
