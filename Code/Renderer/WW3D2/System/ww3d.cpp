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
 *                     $Archive:: /Commando/Code/ww3d2/ww3d.cpp                               $*
 *                                                                                             *
 *                   Org Author:: Greg_h                                                       *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *								$Modtime:: 08/05/02 10:03a                                             $*
 *                                                                                             *
 *                    $Revision:: 98                                                          $*
 *                                                                                             *
 * 07/01/02 KM Scalable shader library integration				                               *
 * 08/05/02 KM Texture class redesign 
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   WW3D::Init -- Initialize the WW3D Library                                                 *
 *   WW3D::Shutdown -- shutdown the WW3D Library                                               *
 *   WW3D::Set_Render_Device -- set the render device being currently used                     *
 *   WW3D::Set_Next_Render_Device -- just go to the next device in the list                    *
 *   WW3D::Set_Device_Resolution -- set the current resolution and bitdepth                    *
 *   WW3D::Get_Render_Device -- Get the index of the current render device                     *
 *   WW3D::Get_Render_Device_Desc -- returns description of the current render device          *
 *   WW3D::Get_Render_Device_Count -- returns the number of render devices available           *
 *   WW3D::Get_Render_Device_Name -- returns the name of the n-th render device                *
 *	  WW3D::Get_Render_Target_Resolution -- get the resolution and bitdepth of the current target*
 *   WW3D::Get_Device_Resolution -- get the current resolution and bitdepth                    *
 *   WW3D::Begin_Render -- mark the start of rendering for a new frame                         *
 *   WW3D::Render -- Render a 3D Scene using the given camera                                  *
 *   WW3D::Render -- Render a single render object                                             *
 *   WW3D::End_Render -- Mark the completion of a frame                                        *
 *   WW3D::Sync -- Time sychronization                                                         *
 *   WW3D::Set_Ext_Swap_Interval -- Sets the swap interval the device should aim sync for.     *
 *   WW3D::Get_Ext_Swap_Interval -- Queries the swap interval the device is aiming sync for.   *
 *   WW3D::Get_Polygon_Mode -- returns the current rendering mode                              *
 *   WW3D::Set_Collision_Box_Display_Mask -- control rendering of collision boxes              *
 *   WW3D::Get_Collision_Box_Display_Mask -- returns the current display mask for collision bo *
 *   WW3D::Normalize_Coordinates -- Convert pixel coords to normalized screen coords 0..1      *
 *   WW3D::Update_Render_Device_Description -- updates the description of the current render d *
 *   WW3D::Make_Screen_Shot -- saves a screenshot with the given base filename                 *
 *   WW3D::Set_Texture_Reduction -- sets the (hacky) texture reduction factor                  *
 *   WW3D::Get_Texture_Reduction -- gets the (hacky) texture reduction factor                  *
 *   WW3D::Flush_Texture_Cache -- dump all textures from the texture cache                     *
 *   WW3D::Allocate_Debug_Resources -- allocates the debug resources					              *
 *   WW3D::Release_Debug_Resources -- releases the debug resources									  *
 *   WW3D::Get_Last_Frame_Poly_Count -- returns the number of polys submitted in the previous  *
 *   WW3D::Flush -- Process all pending rendering tasks                                        *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "ww3d.h"
#include "formatinto.h"
#include "rinfo.h"
#include "assetmgr.h"
#include "boxrobj.h"
#include "predlod.h"
#include "camera.h"
#include "scene.h"
#include "segline.h"
#include "shader.h"
#include "vertmaterial.h"
#include "wwdebug.h"
#include "wwprofile.h"
#include "shattersystem.h"
#include "textureloader.h"
#include "statistics.h"
#include "pointgr.h"
#include "ffactory.h"
#include "INI.H"
#include "dazzle.h"
#include "meshmdl.h"
#include "meshrenderer.h"
#include "render2d.h"
#include "bound.h"
#include "rddesc.h"
#include "Vector3i.h"
#include <cstdio>
#include "WW3D2/Common/renderdevice.h"
#include "TARGA.H"
#include "sortingrenderer.h"
#include "thread.h"
#include "texturemanager.h"
#include "formconv.h"
#include "animatedsoundmgr.h"
#include "static_sort_list.h"

#include "shdlib.h"

// timeBeginPeriod/timeEndPeriod
#include <windows.h>
#include <mmsystem.h>
#include "formatinto.h"



const char* DAZZLE_INI_FILENAME="DAZZLE.INI";

#define DEFAULT_DEBUG_SHADER_BITS	(		SHADE_CNST(\
												ShaderClass::PASS_LEQUAL,\
												ShaderClass::DEPTH_WRITE_ENABLE,\
												ShaderClass::COLOR_WRITE_ENABLE,\
												ShaderClass::SRCBLEND_ONE,\
												ShaderClass::DSTBLEND_ZERO,\
												ShaderClass::FOG_DISABLE,\
												ShaderClass::GRADIENT_MODULATE,\
												ShaderClass::SECONDARY_GRADIENT_DISABLE,\
												ShaderClass::TEXTURING_DISABLE,\
												ShaderClass::ALPHATEST_DISABLE,\
												ShaderClass::CULL_MODE_ENABLE, \
												ShaderClass::DETAILCOLOR_DISABLE,\
												ShaderClass::DETAILALPHA_DISABLE) )

#define LIGHTMAP_DEBUG_SHADER_BITS	(		SHADE_CNST(\
												ShaderClass::PASS_LEQUAL,\
												ShaderClass::DEPTH_WRITE_ENABLE,\
												ShaderClass::COLOR_WRITE_ENABLE,\
												ShaderClass::SRCBLEND_ONE,\
												ShaderClass::DSTBLEND_ZERO,\
												ShaderClass::FOG_DISABLE,\
												ShaderClass::GRADIENT_DISABLE,\
												ShaderClass::SECONDARY_GRADIENT_DISABLE,\
												ShaderClass::TEXTURING_ENABLE,\
												ShaderClass::ALPHATEST_DISABLE,\
												ShaderClass::CULL_MODE_ENABLE, \
												ShaderClass::DETAILCOLOR_DISABLE,\
												ShaderClass::DETAILALPHA_DISABLE) )



/**********************************************************************************
**
**  WW3D Static Globals
**
***********************************************************************************/

unsigned int											WW3D::SyncTime = 0;
unsigned int											WW3D::PreviousSyncTime = 0;
bool														WW3D::IsSortingEnabled = true;

float														WW3D::PixelCenterX = 0.0f;
float														WW3D::PixelCenterY = 0.0f;


bool														WW3D::IsInitted = false;
bool														WW3D::IsRendering = false;
bool														WW3D::IsScreenUVBiased = false;

bool														WW3D::AreDecalsEnabled = true;
float														WW3D::DecalRejectionDistance = 1000000.0f;

bool														WW3D::AreStaticSortListsEnabled = false;
bool														WW3D::MungeSortOnLoad = false;

bool														WW3D::OverbrightModifyOnLoad = false;


int														WW3D::FrameCount = 0;
long														WW3D::UserStat0 = 0;
long														WW3D::UserStat1 = 0;
long														WW3D::UserStat2 = 0;

float														WW3D::DefaultNativeScreenSize = 1.0f;

StaticSortListClass *								WW3D::DefaultStaticSortLists = nullptr;
StaticSortListClass *								WW3D::CurrentStaticSortLists = nullptr;


VertexMaterialClass *								WW3D::DefaultDebugMaterial  = nullptr;
ShaderClass												WW3D::DefaultDebugShader(DEFAULT_DEBUG_SHADER_BITS);
ShaderClass												WW3D::LightmapDebugShader(LIGHTMAP_DEBUG_SHADER_BITS);

WW3D::PrelitModeEnum									WW3D::PrelitMode = PRELIT_MODE_LIGHTMAP_MULTI_PASS;
bool														WW3D::ExposePrelit = false;

bool														WW3D::SnapshotActivated=false;

WW3D::MeshDrawModeEnum								WW3D::MeshDrawMode = MESH_DRAW_MODE_OLD;
WW3D::NPatchesGapFillingModeEnum					WW3D::NPatchesGapFillingMode = NPATCHES_GAP_FILLING_ENABLED;
unsigned													WW3D::NPatchesLevel=1;
bool														WW3D::IsTexturingEnabled=true;
bool										WW3D::IsColoringEnabled=false;

static HWND												_Hwnd = nullptr;		// Not a member to hide windows from WW3D users
static int												_TextureReduction = 0;
static int												_TextureMinDim = 1;
static bool												_LargeTextureExtraReductionEnabled = false;

int														WW3D::TextureFilter = 0;

bool														WW3D::Lite = false;

/**********************************************************************************
**
**  WW3D Static Functions
**
***********************************************************************************/

void WW3D::Set_NPatches_Gap_Filling_Mode(NPatchesGapFillingModeEnum mode)
{
	if (NPatchesGapFillingMode!=mode) {
		NPatchesGapFillingMode=mode;
		TheMeshRenderer.Invalidate();
	}
}

void WW3D::Set_NPatches_Level(unsigned level)
{
	if (level>8) level=8;
	if (level<1) level=1;
	if (NPatchesLevel==1 && level>1) TheMeshRenderer.Invalidate();
	if (NPatchesLevel>1 && level==1) TheMeshRenderer.Invalidate();
	NPatchesLevel = level;
}

/***********************************************************************************************
 * WW3D::Init -- Initialize the WW3D Library                                                   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Init(void *hwnd, char *defaultpal, bool lite)
{
	assert(IsInitted == false);
	WWDEBUG_SAY(("WW3D::Init hwnd = %p\n",hwnd));
	_Hwnd = static_cast<HWND>(hwnd);
	Lite = lite;

	/*
	** Initialize d3d, this also enumerates the available devices and resolutions.
	*/
	Init_D3D_To_WW3_Conversion();
	WWDEBUG_SAY(("Init RenderDevice\n"));
	if (!RenderDevice::Init(_Hwnd, lite)) {
		return(WW3D_ERROR_INITIALIZATION_FAILED);
	}
	WWDEBUG_SAY(("Allocate Debug Resources\n"));
	Allocate_Debug_Resources();

	[[maybe_unused]] MMRESULT r=timeBeginPeriod(1);
	WWASSERT(r==TIMERR_NOERROR);

	/*
	** Initialize the dazzle system
	*/
	if (!lite) {
		WWDEBUG_SAY(("Init Dazzles\n"));
		FileClass * dazzle_ini_file = _TheFileFactory->Get_File(DAZZLE_INI_FILENAME);
		if (dazzle_ini_file) {
			INIClass dazzle_ini(*dazzle_ini_file);
			DazzleRenderObjClass::Init_From_INI(&dazzle_ini);
			_TheFileFactory->Return_File(dazzle_ini_file);
		}
	}
	/*
	** Initialize the default static sort lists
	** Note that DefaultStaticSortLists[0] is unused.
	*/
	DefaultStaticSortLists = W3DNEW DefaultStaticSortListClass();
	Reset_Current_Static_Sort_Lists_To_Default();

	/*
	** Initialize the animation-triggered sound system
	*/
	if (!lite) {
		AnimatedSoundMgrClass::Initialize ();
		IsInitted = true;
	}
	WWDEBUG_SAY(("WW3D Init completed\n"));
	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * WW3D::Shutdown -- shutdown the WW3D Library                                                 *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Shutdown(void)
{
	assert(Lite || IsInitted == true);
//	WWDEBUG_SAY(("WW3D::Shutdown\n"));


	//restore the previous timer resolution
	[[maybe_unused]] MMRESULT r=timeEndPeriod(1);
	WWASSERT(r==TIMERR_NOERROR);
	/*
	** Free memory in predictive LOD optimizer
	*/
	PredictiveLODOptimizerClass::Free();

	/*
	** Free the DazzleRenderObject class stuff. Whatever it is. ST - 6/11/2001 8:20PM
	*/
	if (!Lite) {
		DazzleRenderObjClass::Deinit ();
	}

	/*
	** Release all of our assets
	*/
	Release_Debug_Resources();
	if (WW3DAssetManager::Get_Instance()) {
		WW3DAssetManager::Get_Instance()->Free_Assets();
	}

	TextureManagerClass::Shutdown();
	if (!Lite) {
		RenderDevice::Shutdown();
	}

	/*
	** Clear the default static sort lists
	*/
	delete DefaultStaticSortLists;

	/*
	** Release the animation-triggered sound data
	*/
	AnimatedSoundMgrClass::Shutdown ();

	IsInitted = false;
	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * WW3D::Set_Render_Device -- set the render device being currently used                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Set_Render_Device( const char * dev_name, int width, int height, int bits, int windowed, bool resize_window )
{
	bool success = RenderDevice::Set_Render_Device(dev_name,width,height,bits,windowed,resize_window);
	if (success) {
		return WW3D_ERROR_OK;
	} else {
		return WW3D_ERROR_INITIALIZATION_FAILED;
	}
}


/***********************************************************************************************
 * WW3D::Set_Any_Render_Device -- set any render device you can find                           *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Set_Any_Render_Device( void )
{
	bool success = RenderDevice::Set_Any_Render_Device();
	if (success) {
		return WW3D_ERROR_OK;
	} else {
		return WW3D_ERROR_INITIALIZATION_FAILED;
	}
}


/***********************************************************************************************
 * WW3D::Set_Render_Device -- set the render device being currently used                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Set_Render_Device(int dev, int width, int height, int bits, int windowed, bool resize_window, bool reset_device, bool restore_assets )
{
	bool success = RenderDevice::Set_Render_Device(dev,width,height,bits,windowed,resize_window,reset_device, restore_assets );
	if (success) {
		return WW3D_ERROR_OK;
	} else {
		return WW3D_ERROR_INITIALIZATION_FAILED;
	}
}


/***********************************************************************************************
 * WW3D::Set_Next_Render_Device -- just go to the next device in the list                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/26/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Set_Next_Render_Device(void)
{
	bool success = RenderDevice::Set_Next_Render_Device();
	if (success) {
		return WW3D_ERROR_OK;
	} else {
		return WW3D_ERROR_INITIALIZATION_FAILED;
	}
}

/***********************************************************************************************
 * WW3D::Get_Window -- returns the handle of the render window.										  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/28/2001  pds : Created.                                                                 *
 *=============================================================================================*/
void *WW3D::Get_Window( void )
{
	return _Hwnd;
}

/***********************************************************************************************
 * WW3D::Is_Windowed -- returns wether we are currently in a windowed mode                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/26/2001  gth : Created.                                                                 *
 *=============================================================================================*/
bool WW3D::Is_Windowed( void )
{
	return RenderDevice::Is_Windowed();
}

/***********************************************************************************************
 * WW3D::Get_Render_Device -- Get the index of the current render device                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *   1/25/2001  gth : converted to dx8                                                         *
 *=============================================================================================*/
int WW3D::Get_Render_Device(void)
{
	return RenderDevice::Get_Render_Device();
}


/***********************************************************************************************
 * WW3D::Get_Render_Device_Desc -- returns description of the current render device            *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/26/98    GTH : Created.                                                                 *
 *   1/25/2001  gth : converted to dx8                                                         *
 *=============================================================================================*/
const RenderDeviceDescClass & WW3D::Get_Render_Device_Desc(int deviceidx)
{
	return RenderDevice::Get_Render_Device_Desc(deviceidx);
}



/***********************************************************************************************
 * WW3D::Get_Render_Device_Count -- returns the number of render devices available             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/19/99    GTH : Created.                                                                 *
 *   1/25/2001  gth : converted to DX8                                                         *
 *=============================================================================================*/
const int WW3D::Get_Render_Device_Count(void)
{
	return RenderDevice::Get_Render_Device_Count();
}


/***********************************************************************************************
 * WW3D::Get_Render_Device_Name -- returns the name of the n-th render device                  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/19/99    GTH : Created.                                                                 *
 *   1/25/2001  gth : converted to dx8                                                         *
 *=============================================================================================*/
const char * WW3D::Get_Render_Device_Name(int device_index)
{
	return RenderDevice::Get_Render_Device_Name(device_index);
}


/***********************************************************************************************
 * WW3D::Set_Device_Resolution -- set the current resolution and bitdepth                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Set_Device_Resolution(int width,int height,int bits,int windowed, bool resize_window)
{
	bool success = RenderDevice::Set_Device_Resolution(width,height,bits,windowed,resize_window);

	if (success) {
		return WW3D_ERROR_OK;
	} else {
		return WW3D_ERROR_INITIALIZATION_FAILED;
	}
}


/***********************************************************************************************
 * WW3D::Get_Render_Target_Resolution -- get the resolution and bitdepth of the current target *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *   1/25/2001  gth : converted to dx8                                                         *
 *=============================================================================================*/
void WW3D::Get_Render_Target_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed)
{
	RenderDevice::Get_Render_Target_Resolution(set_w,set_h,set_bits,set_windowed);
}


/***********************************************************************************************
 * WW3D::Get_Device_Resolution -- get the current resolution and bitdepth                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *   1/25/2001  gth : converted to dx8                                                         *
 *=============================================================================================*/
void WW3D::Get_Device_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed)
{
	RenderDevice::Get_Device_Resolution(set_w,set_h,set_bits,set_windowed);
}


void WW3D::_Invalidate_Mesh_Cache()
{
	TheMeshRenderer.Invalidate();
}

void WW3D::_Invalidate_Textures()
{
	if (!WW3DAssetManager::Get_Instance()) return;

	TextureLoader::Flush_Pending_Load_Tasks();

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());

	// Loop through all the textures in the manager
	for (ite.First();!ite.Is_Done();ite.Next()) {
		// Get the current texture
		TextureClass* tex=ite.Peek_Value();
		tex->Invalidate();
	}
}

void WW3D::Set_Texture_Filter(int texture_filter)
{
	if (texture_filter<0) texture_filter=0;
	if (texture_filter>TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC) texture_filter=TextureFilterClass::TEXTURE_FILTER_ANISOTROPIC;
	TextureFilter=texture_filter;
	TextureFilterClass::_Init_Filters(static_cast<TextureFilterClass::TextureFilterMode>(TextureFilter));
}


/***********************************************************************************************
 * WW3D::Begin_Render -- mark the start of rendering for a new frame                           *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Begin_Render(bool clear,bool clearz,const Vector3 & color, float dest_alpha, void(*network_callback)(void))
{
	if (!IsInitted) {
		return(WW3D_ERROR_OK);
	}

	WWPROFILE("WW3D::Begin_Render");
	WWASSERT(IsInitted);
	HRESULT hr;

	SNAPSHOT_SAY(("==========================================\r\n"));
	SNAPSHOT_SAY(("========== WW3D::Begin_Render ============\r\n"));
	SNAPSHOT_SAY(("==========================================\r\n\r\n"));

	// Query the device state through the wrapper.
	if (RenderDevice::Has_Device() && (hr=RenderDevice::Get_Device_State()) != static_cast<HRESULT>(WW3D_OK))
	{
        // If the device was lost, do not render until we get it back
        if( static_cast<HRESULT>(WW3D_ERR_DEVICELOST) == hr )
            return WW3D_ERROR_GENERIC;	//other app has the device

        // Check if the device needs to be reset
        if( static_cast<HRESULT>(WW3D_ERR_DEVICENOTRESET) == hr )
        {
			RenderDevice::Reset_Device();
        }

		return WW3D_ERROR_GENERIC;
	}

	TextureLoader::Update(network_callback);
//	TextureClass::_Reset_Time_Stamp();
	DynamicVBAccessClass::_Reset(true);
	DynamicIBAccessClass::_Reset(true);
	Debug_Statistics::Begin_Statistics();


	WWASSERT(!IsRendering);
	IsRendering = true;

	// If we want to clear the screen, we need to set the viewport to include the entire screen:
	if (clear || clearz) {
		WW3DViewport vp;
		int width, height, bits;
		bool windowed;
		WW3D::Get_Render_Target_Resolution(width, height, bits, windowed);
		vp.X = 0;
		vp.Y = 0;
		vp.Width = width;
		vp.Height = height;
		vp.MinZ = 0.0f;;
		vp.MaxZ = 1.0f;
		RenderDevice::Set_Viewport(&vp);
		RenderDevice::Clear(clear, clearz, color, dest_alpha);
	}

	// Notify D3D that we are beginning to render the frame
	RenderDevice::Begin_Scene();

	return WW3D_ERROR_OK;
}

/***********************************************************************************************
 * WW3D::Render -- Render a list of layers, starting at the back.                              *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/2/98    EHC : Created.                                                                  *
 *=============================================================================================*/
WW3DErrorType WW3D::Render(const LayerListClass &LayerList)
{
	if (!IsInitted) {
		return(WW3D_ERROR_OK);
	}

	WWASSERT(IsRendering);

	LayerClass *layer = LayerList.Last();

	while (layer->Is_Valid()) {
		WW3DErrorType result = Render(*layer);

		if (result != WW3D_ERROR_OK) {
			return result;
		}

		layer = layer->Prev();
	}

	return WW3D_ERROR_OK;
}

/***********************************************************************************************
 * WW3D::Render -- Render a Layer                                                              *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/2/98    EHC : Created.                                                                  *
 *=============================================================================================*/
WW3DErrorType WW3D::Render(const LayerClass &Layer)
{
	if (!IsInitted) {
		return(WW3D_ERROR_OK);
	}

	WWASSERT(IsRendering);
	return Render(Layer.Scene, Layer.Camera, Layer.Clear, Layer.ClearZ, Layer.ClearColor);

}


/***********************************************************************************************
 * WW3D::Render -- Render a 3D Scene using the given camera                                    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Render(SceneClass * scene,CameraClass * cam,bool clear,bool clearz,const Vector3 & color)
{
	if (!IsInitted) {
		return(WW3D_ERROR_OK);
	}

	WWPROFILE("WW3D::Render");
	WWASSERT(IsInitted);
	WWASSERT(IsRendering);
	WWASSERT(scene);
	WWASSERT(cam);

	cam->On_Frame_Update();
	RenderInfoClass rinfo(*cam);

	// Apply the camera and viewport (including depth range)
	cam->Apply();

	// Clear the viewport
	if (clear || clearz) {
		RenderDevice::Clear(clear, clearz, color);
	}

	// set the rendering mode
	switch(scene->Get_Polygon_Mode()) {
		case SceneClass::POINT:
			RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_POINT);
			break;
		case SceneClass::LINE:
			RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_WIREFRAME);
			break;
		case SceneClass::FILL:
			RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_SOLID);
			break;
	}

	// Set the global ambient light value here.  If the scene is using the LightEnvironment system
	// this setting will get overriden.
	RenderDevice::Set_Ambient(scene->Get_Ambient_Light());

	// render the scene

	TheMeshRenderer.Set_Camera(&rinfo.Camera);

	scene->Render(rinfo);

	Flush(rinfo);

	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * WW3D::Render -- Render a single render object                                               *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/4/2001   gth : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Render(
	RenderObjClass & obj,
	RenderInfoClass & rinfo
)
{
	if (!IsInitted) {
		return(WW3D_ERROR_OK);
	}

	WWPROFILE("WW3D::Render");
	WWASSERT(IsInitted);
	WWASSERT(IsRendering);

	{
		WWPROFILE("On_Frame_Update");
		rinfo.Camera.On_Frame_Update();
	}

	// Apply the camera and viewport (including depth range)
	rinfo.Camera.Apply();

	// set the rendering mode
	RenderDevice::Set_Render_State(WW3D_RS_FILLMODE,WW3D_FILL_SOLID);

	// Install the lighting environment if one is supplied
	if (rinfo.light_environment != nullptr) {
		RenderDevice::Set_Light_Environment(rinfo.light_environment);
	}

	// Render the object
	TheMeshRenderer.Set_Camera(&rinfo.Camera);

	obj.Render(rinfo);

	Flush(rinfo);

	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * WW3D::Flush -- Process all pending rendering tasks                                          *
 *                                                                                             *
 *    NOTE: This normally happens AUTOMATICALLY. The user should almost *NEVER* have to call   *
 *    this function.  Anyway, this function causes all of the deferred rendering systems to    *
 *    actually perform all of their rendering tasks.  This includes the MeshRenderer and    *
 *    the sorting system.                                                                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *    Don't call this unless you know what you're doing                                        *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/17/2001  gth : Created.                                                                 *
 * 07/01/02 KM Scalable shader library integration				                               *
 *=============================================================================================*/
void WW3D::Flush(RenderInfoClass & rinfo)
{
	TheMeshRenderer.Flush();
	SHD_FLUSH;
	WW3D::Render_And_Clear_Static_Sort_Lists(rinfo);	//draws things like water

	SortingRendererClass::Flush();
	TheMeshRenderer.Clear_Pending_Delete_Lists();
}


/***********************************************************************************************
 * WW3D::End_Render -- Mark the completion of a frame                                          *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::End_Render(bool flip_frame)
{
	if (!IsInitted) {
		return(WW3D_ERROR_OK);
	}

	WWPROFILE("WW3D::End_Render");

	WWASSERT(IsRendering);
	WWASSERT(IsInitted);

	// If sorting renderer flush isn't called from within any of the render functions
	// the sorting arrays will overflow!

	SortingRendererClass::Flush();

	IsRendering = false;

	{
		WWPROFILE("RenderDevice::End_Scene");
		RenderDevice::End_Scene(flip_frame);
	}

	FrameCount++;

	{
		WWPROFILE("End_Statistics");
		Debug_Statistics::End_Statistics();
	}

	SNAPSHOT_SAY(("==========================================\r\n"));
	SNAPSHOT_SAY(("========== WW3D::End_Render ==============\r\n"));
	SNAPSHOT_SAY(("==========================================\r\n\r\n"));

	Activate_Snapshot(false);
	
	// (gth) I've found some cases where its not safe to rely on our "shadow" copy (of 
	// matrices for example) across multiple frames.  So even though this is slightly
	// less "optimal", lets just reset the caches each frame.
	RenderDevice::Invalidate_Cached_Render_States();

	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * WW3D::Get_Last_Frame_Poly_Count -- returns the number of polys submitted in the previous fr *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/28/99    GTH : Created.                                                                 *
 *=============================================================================================*/
unsigned int WW3D::Get_Last_Frame_Poly_Count(void)
{
	return Debug_Statistics::Get_Device_Polygons();
}

unsigned int WW3D::Get_Last_Frame_Vertex_Count(void)
{
	return Debug_Statistics::Get_Device_Vertices();
}


/***********************************************************************************************
 * WW3D::Sync -- Time sychronization                                                           *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void WW3D::Sync(unsigned int sync_time)
{
	PreviousSyncTime = SyncTime;
   SyncTime = sync_time;
}


/***********************************************************************************************
 * WW3D::Set_Ext_Swap_Interval -- Sets the swap interval the device should aim sync for.       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:   Not supported by all rendering devices.                                         *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/07/98    NH : Created.                                                                  *
 *=============================================================================================*/
void WW3D::Set_Ext_Swap_Interval(long swap)
{
	RenderDevice::Set_Swap_Interval(swap);
}


/***********************************************************************************************
 * WW3D::Get_Ext_Swap_Interval -- Queries the swap interval the device is aiming sync for.     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:   Not supported by all rendering devices.                                         *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/07/98    NH : Created.                                                                  *
 *=============================================================================================*/
long WW3D::Get_Ext_Swap_Interval(void)
{
	return RenderDevice::Get_Swap_Interval();
}


/***********************************************************************************************
 * WW3D::Set_Collision_Box_Display_Mask -- control rendering of collision boxes                *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/17/99    GTH : Created.                                                                 *
 *=============================================================================================*/
void WW3D::Set_Collision_Box_Display_Mask(int mask)
{
	BoxRenderObjClass::Set_Box_Display_Mask(mask);
}

/***********************************************************************************************
 * WW3D::Get_Collision_Box_Display_Mask -- returns the current display mask for collision boxe *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   6/1/99     GTH : Created.                                                                 *
 *=============================================================================================*/
int WW3D::Get_Collision_Box_Display_Mask(void)
{
	return BoxRenderObjClass::Get_Box_Display_Mask();
}


/***********************************************************************************************
 * WW3D::Normalize_Coordinates -- Convert pixel coords to normalized screen coords 0..1        *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/27/99    EHC : Created.                                                                 *
 *=============================================================================================*/
void WW3D::Normalize_Coordinates(int x, int y, float &fx, float &fy)
{
	// clip the coordinates back into the resolution of the screen
	x = Bound(x, 0, RenderDevice::Get_Device_Resolution_Width());
	y = Bound(y, 0, RenderDevice::Get_Device_Resolution_Height());

	// now that the coordinates are clipped convert them to their normalized values.
	fx = static_cast<float>(x) / RenderDevice::Get_Device_Resolution_Width();
	fy = static_cast<float>(y) / RenderDevice::Get_Device_Resolution_Height();
}


/***********************************************************************************************
 * WW3D::Make_Screen_Shot -- saves a screenshot with the given base filename                   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/19/99    GTH : Created.                                                                 *
 *   2/26/2001  hy : Updated to DX8                                                            *
 *=============================================================================================*/
void WW3D::Make_Screen_Shot( const char * filename_base , const float gamma, const ScreenShotFormatEnum format)
{

	WWASSERT(!IsRendering);

	char filename[80];

	char ext[4];
	switch (format) {
		case TGA:
			WWFormat::Into(ext, "tga");
			break;
		case BMP:
			WWFormat::Into(ext, "bmp");
			break;
		default:
			WWASSERT(0);
			return;
			break;
	}

	static int frame_number = 1;

	bool done = false;
	while (!done) {
		WWFormat::Into(filename, "{}{:02d}.{}", filename_base, frame_number++, ext);
		FileClass*file=_TheFileFactory->Get_File( filename );
		if ( file ) {
			file->Open();
			done = !file->Is_Available();
			_TheFileFactory->Return_File( file );
		} else {
			done = true;
		}
	}

	WWDEBUG_SAY(( "Creating Screen Shot %s\n", filename ));

	// make the gamma look up table
	int i;
	unsigned char gamma_lut[256];
	float recip = 1.0f;
	if (gamma > WWMATH_EPSILON) {
		recip = 1.0f / gamma;
	}
	for (i = 0; i < 256; i++) {
		gamma_lut[i] = static_cast<unsigned char>(256.0f * powf(i / 256.0f, recip));
	}

	// Lock front buffer and copy

	WW3DSurfaceHandle fb;
	fb=RenderDevice::_Get_Front_Buffer();
	WW3DSurfaceDesc desc;
	RenderDevice::Get_Surface_Desc(fb, &desc);

	RECT bounds;
	GetWindowRect(_Hwnd,&bounds);

	WW3DLockedRect lrect;

	Device_ErrorCode(RenderDevice::Lock_Surface(fb, &lrect, &bounds, WW3D_LOCK_READONLY));

	unsigned int x,y,index,index2,width,height;

	width=bounds.right-bounds.left;
	height=bounds.bottom-bounds.top;

	unsigned char *image=W3DNEWARRAY unsigned char[3*width*height];

	for (y=0; y<height; y++)
	{
		for (x=0; x<width; x++)
		{
			// index for image
			index=3*(x+y*width);
			// index for fb
			index2=y*lrect.Pitch+4*x;

			image[index]   = gamma_lut[*(static_cast<unsigned char *>(lrect.pBits) + index2+2)];
			image[index+1] = gamma_lut[*(static_cast<unsigned char *>(lrect.pBits) + index2+1)];
			image[index+2] = gamma_lut[*(static_cast<unsigned char *>(lrect.pBits) + index2+0)];
		}
	}

	RenderDevice::Release_Resource(fb);

	switch (format) {
		case TGA:
			{
				Targa targ;
				memset(&targ.Header,0,sizeof(targ.Header));
				targ.Header.Width=static_cast<short>(width);
				targ.Header.Height=static_cast<short>(height);
				targ.Header.PixelDepth=24;
				targ.Header.ImageType=TGA_TRUECOLOR;
				targ.SetImage(reinterpret_cast<char *>(image));
				targ.YFlip();

				FileClass*file=_TheWritingFileFactory->Get_File( filename );
				if ( file ) {
					file->Create();
					file->Close();
					_TheWritingFileFactory->Return_File( file );
				}

				targ.Save(filename,TGAF_IMAGE,false);
			}
		break;
		case BMP:
			{
				BITMAPFILEHEADER fileheader;
				BITMAPINFOHEADER header;
				memset(&header, 0, sizeof(BITMAPINFOHEADER));
				header.biSize = sizeof(BITMAPINFOHEADER);
				header.biWidth = width;
				header.biHeight = height;
				header.biPlanes = 1;
				header.biBitCount = 24;
				header.biCompression = BI_RGB;
				header.biXPelsPerMeter = 0xB12;
				header.biYPelsPerMeter = 0xB12;
				int len = ((width * 24 +31) & ~31) /8;
    
				memset(&fileheader, 0, sizeof(BITMAPFILEHEADER));
				fileheader.bfType = 19778; // BM
				fileheader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
				fileheader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 3 * len * height * sizeof(char);

				FileClass *file = _TheWritingFileFactory->Get_File( filename );
				if ( file ) {
					file->Create();
					file->Open(FileClass::WRITE);
					int num;
					num = file->Write(&fileheader, sizeof(BITMAPFILEHEADER));
					WWASSERT(num == sizeof(BITMAPFILEHEADER));
					num = file->Write(&header, sizeof(BITMAPINFOHEADER));
					WWASSERT(num == sizeof(BITMAPINFOHEADER));
					char *temp = new char [3 * len];
					memset(temp, 0, 3 * len * sizeof(char));
					// invert image, pad and swap R and B
					for (y = 0; y < static_cast<int>(height); y++) {
						memcpy(&temp[0], &image[ 3 * width * (height - y - 1)], 3 * width * sizeof(char));
						for (x = 0; x < width; x++) {
							char t2 = temp[3 * x];
							temp[3 * x] = temp[3 * x + 2];
							temp[3 * x + 2] = t2;
						}
						num = file->Write(&temp[0], len * sizeof(char));
						WWASSERT(num == len * (int)sizeof(char));
					}
					delete [] temp;
					file->Close();
					_TheWritingFileFactory->Return_File( file );
				}
			}
			break;
	}

	delete [] image;
}



/***********************************************************************************************
 * WW3D::Set_Texture_Reduction -- sets the (hacky) texture reduction factor                    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/19/99    GTH : Created.                                                                 *
 *=============================================================================================*/
void	WW3D::Set_Texture_Reduction( int value, int minDim )
{
	if (_TextureReduction != value || _TextureMinDim != minDim) {
		_TextureReduction=value;
		_TextureMinDim=minDim;
		_Invalidate_Textures();
	}
}


void WW3D::Enable_Texturing(bool b)
{
	if (b==IsTexturingEnabled) return;
	IsTexturingEnabled=b;
//	_Invalidate_Textures();
}

void WW3D::Enable_Coloring(unsigned int color)
{
	IsColoringEnabled = (color == 0) ? false : true;
}

/***********************************************************************************************
 * WW3D::Get_Texture_Reduction -- gets the (hacky) texture reduction factor                    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/25/99    TSS : Created.                                                                 *
 *=============================================================================================*/
int	WW3D::Get_Texture_Reduction( void )
{
	return _TextureReduction;
}

/***********************************************************************************************
 * WW3D::Get_Texture_Min_Mip_Levels -- gets the minimum number of mip levels permitted		   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/25/99    TSS : Created.                                                                 *
 *=============================================================================================*/
int	WW3D::Get_Texture_Min_Dimension( void )
{
	return _TextureMinDim;
}

void WW3D::Enable_Large_Texture_Extra_Reduction(bool onoff)
{
	if (_LargeTextureExtraReductionEnabled != onoff) {
		_LargeTextureExtraReductionEnabled = onoff;
		_Invalidate_Textures();
	}
}

bool WW3D::Is_Large_Texture_Extra_Reduction_Enabled(void)
{
	return _LargeTextureExtraReductionEnabled;
}

/***********************************************************************************************
 * WW3D::Peek_Default_Debug_Material -- returns a pointer to the default debug mtl				  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/21/99    GTH : Created.                                                                 *
 *=============================================================================================*/
VertexMaterialClass * WW3D::Peek_Default_Debug_Material(void)
{
	return nullptr;
}

/***********************************************************************************************
 * WW3D::Peek_Default_Debug_Shader -- returns the default shader for debugging.	              *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/21/99    GTH : Created.                                                                 *
 *=============================================================================================*/
ShaderClass	WW3D::Peek_Default_Debug_Shader(void)
{
	return DefaultDebugShader;
}

/***********************************************************************************************
 * WW3D::Peek_Lightmap_Debug_Shader -- returns the shader for lightmap debugging.              *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/21/99    GTH : Created.                                                                 *
 *=============================================================================================*/
ShaderClass	WW3D::Peek_Lightmap_Debug_Shader(void)
{
	return LightmapDebugShader;
}

/***********************************************************************************************
 * WW3D::Allocate_Debug_Resources -- allocates the debug resources									  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/21/99    GTH : Created.                                                                 *
 *=============================================================================================*/
void WW3D::Allocate_Debug_Resources(void)
{
}

/***********************************************************************************************
 * WW3D::Release_Debug_Resources -- releases the debug resources										  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/21/99    GTH : Created.                                                                 *
 *=============================================================================================*/
void WW3D::Release_Debug_Resources(void)
{
}


WW3DErrorType WW3D::On_Deactivate_App(void)
{
	_Invalidate_Textures();
	_Invalidate_Mesh_Cache();

	return WW3D_ERROR_OK;
}


WW3DErrorType WW3D::On_Activate_App(void)
{
	return WW3D_ERROR_OK;
}


void WW3D::Get_Pixel_Center(float &x, float &y)
{
	x = PixelCenterX; y = PixelCenterY;
}


void WW3D::Update_Pixel_Center(void)
{
}

void WW3D::Add_To_Static_Sort_List(RenderObjClass *robj, unsigned int sort_level)
{
	CurrentStaticSortLists->Add_To_List(robj, sort_level);
}

void WW3D::Render_And_Clear_Static_Sort_Lists(RenderInfoClass & rinfo)
{
	// The ststic sort lists need to be disabled while we are rendering from them otherwise the
	// Render() function will just dump the objects right back on the same lists.
	bool old_enable = AreStaticSortListsEnabled;
	AreStaticSortListsEnabled = false;
	CurrentStaticSortLists->Render_And_Clear(rinfo);
	AreStaticSortListsEnabled = old_enable;
}

void WW3D::Enable_Sorting(bool onoff)
{
	IsSortingEnabled = onoff;
	// Have to invalidate mesh rendering system because
	// meshes are put into different fvfs depending on their sort state
	TheMeshRenderer.Invalidate();
}

void WW3D::Override_Current_Static_Sort_Lists(StaticSortListClass * sort_list)
{
	if (sort_list) {
		CurrentStaticSortLists = sort_list;
	} else {
		WWASSERT(sort_list);
	}
}


void WW3D::Reset_Current_Static_Sort_Lists_To_Default(void)
{
	CurrentStaticSortLists = DefaultStaticSortLists;
}

void WW3D::Set_Gamma(float gamma,float bright,float contrast,bool calibrate)
{
	RenderDevice::Set_Gamma(gamma,bright,contrast,calibrate);
}