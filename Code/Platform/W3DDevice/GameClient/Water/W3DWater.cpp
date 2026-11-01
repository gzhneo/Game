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

// FILE: W3DWater.cpp /////////////////////////////////////////////////////////////////////////////
// Created:   Mark Wilczynski, June 2001
// Desc:      Draw reflective water surface.  Also handles drawing of waves/ripples
//			  on the surface.
///////////////////////////////////////////////////////////////////////////////////////////////////

										 
// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "stdio.h"
#include "formatinto.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DWaterTracks.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "texture.h"
#include "assetmgr.h"
#include "rinfo.h"
#include "camera.h"
#include "scene.h"
#include "WW3D2/Common/renderdevice.h"
#include "WW3D2/Common/renderbackend.h"	// viewport for the scene copy
#include "light.h"
#include "wwd3dmath.h"	// matrix math helpers
#include "simplevec.h"
#include "mesh.h"
#include "matinfo.h"

#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/Xfer.h"
#include "Common/GameLOD.h"
#include "Common/GameEngine.h"	// GameEngine_RenderTimeMs
#include "Common/FileSystem.h"		// water textures from ProjectData
#include "Common/file.h"
#include <vector>

#include "GameClient/Water.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DPoly.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"
#include "Engine/TexturePackage.h"
#include "formatinto.h"
#include "W3DWaterInternal.h"


// DEFINES ////////////////////////////////////////////////////////////////////////////////////////
constexpr char SKYBODY_TEXTURE[] = "TSMoonLarg.tga";

// River texture motion per tick (WaterRenderObjClass::update).
static const Real RIVER_V_PER_TICK = 0.002f;
static const Real RIVER_X_PER_TICK = static_cast<Real>(0.0125*33/5000);
static const Real RIVER_Y_PER_TICK = static_cast<Real>(2*0.0125*33/5000);
constexpr auto SEA_REFLECTION_SIZE = 256;		//dimensions of reflection texture

constexpr auto SEA_BUMP_SCALE = 0.06f;		//scales the du/dv offsets stored in bump map (~ amount to perturb)

//3D Grid Mesh Water defines.
constexpr auto WATER_MESH_OPACITY = 0.5f;
constexpr auto WATER_MESH_X_VERTICES = 128;
constexpr auto WATER_MESH_Y_VERTICES = 128;
#define WATER_MESH_SPACING	MAP_XY_FACTOR	//same as terrain

#define DRAW_WATER_WAKES
/// @todo: Fix clipping of objects that intersect the mirror surface
//#define CLIP_GEOMETRY_TO_PLANE	// this enables clipping of objects that intersect the mirror surfaces

// Some shader combinations that can be useful in rendering water:

// Modulate stage0 with stage1 texture.  Also modulate stage 0 with vertex color.
#define SC_DETAIL_BLEND ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_SRC_ALPHA,ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, 	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, ShaderClass::DETAILCOLOR_DETAILBLEND, ShaderClass::DETAILALPHA_DISABLE) )

// Just a z-buffer fill, nothing is written to the color buffer.
#define SC_ZFILL_BLEND ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_DISABLE, ShaderClass::SRCBLEND_ZERO, \
	ShaderClass::DSTBLEND_ONE, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE) )

// No texturing, just vertex color with vertex alpha
#define SC_ZFILL_BLENDx ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, \
	ShaderClass::SRCBLEND_ZERO, ShaderClass::DSTBLEND_SRC_COLOR, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Modulate blended with vertex alpha modulation
#define SC_ZFILL_MODULATE_TEX ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_ZERO, ShaderClass::DSTBLEND_SRC_COLOR, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Alpha blended with vertex alpha modulation
#define SC_ZFILL_ALPHA_TEX ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_SRC_ALPHA, ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_DISABLE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Alpha blended with vertex alpha modulation
#define SC_OPAQUE_TEXONLY ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_ONE, ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_DISABLE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Alpha blended with vertex alpha modulation
#define SC_ZFILL_BLEND3 ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE,\
	ShaderClass::SRCBLEND_SRC_ALPHA, ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, \
	ShaderClass::TEXTURING_ENABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass zFillAlphaShader(SC_ZFILL_BLEND3);
static ShaderClass blendStagesShader(SC_DETAIL_BLEND);


void doSkyBoxSet(Bool startDraw)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_drawSkyBox = startDraw;
}


#define DONUT_SIDES	90
#define INNER_RADIUS 200.0f
#define OUTER_RADIUS 250.0f
#define TEXTURE_REPEAT_COUNT 16
#define DONUT_HEIGHT	15.0f
//#define DO_FLAT_DONUT
#define AMP_SCALE	(30.0f/120.0f)
#define WAVE_FREQ	0.3f
#define AMP_SCALE2	(10.0f/120.0f)
#define NOISE_FREQ	(2.0f*PI/WAVE_FREQ)


					


//-------------------------------------------------------------------------------------------------
/** Destructor. Releases w3d assets. */
//-------------------------------------------------------------------------------------------------
WaterRenderObjClass::~WaterRenderObjClass(void)
{
	REF_PTR_RELEASE(m_meshVertexMaterialClass);
	REF_PTR_RELEASE(m_vertexMaterialClass);
	REF_PTR_RELEASE(m_meshLight);
	REF_PTR_RELEASE(m_alphaClippingTexture);
	REF_PTR_RELEASE (m_skyBox);

	REF_PTR_RELEASE (m_riverTexture);
	REF_PTR_RELEASE (m_whiteTexture);
	REF_PTR_RELEASE (m_waterNoiseTexture);
	REF_PTR_RELEASE (m_riverAlphaEdge);
	REF_PTR_RELEASE (m_waterSparklesTexture);
	REF_PTR_RELEASE (m_waveNormalTexture);
	releaseWater2Textures();
	releaseFreeWaterTextures();

	Int i;

	for(i=0; i<TIME_OF_DAY_COUNT; i++)
	{	REF_PTR_RELEASE(m_settings[i].skyTexture);
		REF_PTR_RELEASE(m_settings[i].waterTexture);
	}

	i=NUM_BUMP_FRAMES;
	while (i--)
	{	SAFE_RELEASE( m_pBumpTexture[i]);
		SAFE_RELEASE( m_pBumpTexture2[i]);
	}

	if (m_meshData)
		delete [] m_meshData;
	m_meshData = nullptr;
	m_meshDataSize = 0;

	//Release strings allocated inside global water settings.
	for  (i=0; i<TIME_OF_DAY_COUNT; i++)
	{	WaterSettings[i].m_skyTextureFile.clear();
		WaterSettings[i].m_waterTextureFile.clear();
	}
	// WorldBuilder creates a second water object in DrawObject; whichever is destroyed second
	// finds the setting already released and set to NULL.
	if (TheWaterTransparency.getNonOverloadedPointer())
		deleteInstanceOf((const_cast<WaterTransparencySetting*>(TheWaterTransparency.getNonOverloadedPointer())));
	TheWaterTransparency = nullptr;
	ReleaseResources();

	m_waterTrackSystem.reset();
}

//-------------------------------------------------------------------------------------------------
/** Constructor. Just nulls out some variables. */
//-------------------------------------------------------------------------------------------------
WaterRenderObjClass::WaterRenderObjClass(void)
{
	memset( &m_settings, 0, sizeof( m_settings ) );
	m_dx=0;
	m_dy=0;
	m_indexBuffer=nullptr;
	m_waterTrackSystem = nullptr;
	m_doWaterGrid = FALSE;
	m_meshVertexMaterialClass=nullptr;
	m_meshLight=nullptr;
	m_vertexMaterialClass=nullptr;
	m_alphaClippingTexture=nullptr;
	m_useCloudLayer=true;
	m_waterType = WATER_TYPE_0_TRANSLUCENT;
	m_tod=TIME_OF_DAY_AFTERNOON;
	m_pReflectionTexture=nullptr;
	m_skyBox=nullptr;
	m_vertexBufferD3D=nullptr;
	m_indexBufferD3D=nullptr;
	m_vertexBufferD3DOffset=0;

	m_dwWavePixelShader=nullptr;
	m_dwWaveVertexShader=nullptr;
	m_waveVertexDeclaration=nullptr;
	m_meshData=nullptr;
	m_meshDataSize = 0;
	m_meshInMotion = FALSE;
	m_gridOrigin=Vector2(0,0);
	m_gridDirectionX=Vector2(1.0f,0.0f);
	m_gridDirectionY=Vector2(1.0f,0.0f);

	m_gridCellSize=WATER_MESH_SPACING;
	m_gridCellsX=WATER_MESH_X_VERTICES;
	m_gridCellsY=WATER_MESH_Y_VERTICES;
	m_gridWidth = m_gridCellsX * m_gridCellSize;
	m_gridHeight = m_gridCellsY * m_gridCellSize;
	
	Int i=NUM_BUMP_FRAMES;
	while (i--)
	{	m_pBumpTexture[i]=nullptr;
		m_pBumpTexture2[i]=nullptr;	// the destructor releases both arrays
	}

	m_riverVOrigin=0;
	m_riverTexture=nullptr;
	m_whiteTexture=nullptr;
	m_waterNoiseTexture=nullptr;
	m_riverAlphaEdge=nullptr;
	m_riverWaterPixelShader=0;		///<D3D handle to pixel shader.
	m_trapezoidWaterPixelShader=0;		///<D3D handle to pixel shader.
	m_waterSparklesTexture=0;
	m_riverXOffset=0;
	m_riverYOffset=0;
	m_riverVOriginTick=0;
	m_riverXOffsetTick=0;
	m_riverYOffsetTick=0;

	// Realistic water
	m_realWaterPixelShader=0;
	m_waveNormalTexture=nullptr;
	m_sceneCopyTexture=nullptr;
	m_sceneCopyWidth=0;
	m_sceneCopyHeight=0;
	m_sceneCopyFormat=0;
	m_sceneCopyPending=FALSE;
	m_sceneCopyValid=FALSE;

	// Water version 2
	m_water2Waves=nullptr;
	m_water2Foam=nullptr;
	m_water2Wind=nullptr;
	for (Int i2=0; i2<4; i2++)
		m_water2Sky[i2]=nullptr;
	m_water2Tried=FALSE;
	m_water2Active=FALSE;
	for (Int i3=0; i3<3; i3++)
	{	m_freeWaterTexture[i3]=nullptr;
		m_freeWaterTried[i3]=FALSE;
	}
	m_riverWater2PixelShader=0;
	m_riverRate=0.06f;
	m_riverRateOrigin=0.0f;
	m_riverRateTime=0;
}

//-------------------------------------------------------------------------------------------------
/** WW3D method that returns object bounding sphere used in frustum culling*/
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	//Since this object is more of a system (containing lots of water pieces),
	//let's disable culling by making bounds huge.  Let each piece do it's own cull.
	Vector3	ObjSpaceCenter(0,0,0);
//	Vector3	ObjSpaceRadius(m_dx,m_dy,0);
	Vector3	ObjSpaceRadius(50000,50000,0);

	sphere.Init(ObjSpaceCenter,ObjSpaceRadius.Length());
}

//-------------------------------------------------------------------------------------------------
/** WW3D method that returns object bounding box used in collision detection*/
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	//Since this object is more of a system (containing lots of water pieces),
	//let's disable culling by making bounds huge.  Let each piece do it's own cull.

	Vector3	ObjSpaceCenter(0,0,0);
	Vector3	ObjSpaceExtents(50000,50000,0.001f*m_dy);	//since mirror is a plane, it has no thickness. Set to m_dy/1000.

	box.Init(ObjSpaceCenter,ObjSpaceExtents);
}

//-------------------------------------------------------------------------------------------------
/** returns the class id, so the scene can tell what kind of render object it has. */
//-------------------------------------------------------------------------------------------------
Int WaterRenderObjClass::Class_ID(void) const
{
	return RenderObjClass::CLASSID_UNKNOWN;
}

//-------------------------------------------------------------------------------------------------
/** Not used, but required virtual method. */
//-------------------------------------------------------------------------------------------------
RenderObjClass *	 WaterRenderObjClass::Clone(void) const
{
	assert(false);
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** Copies raw bits from pBumpSrc (a regular grayscale texture) into a D3D
	*   bump-map format. */
//-------------------------------------------------------------------------------------------------
HRESULT WaterRenderObjClass::initBumpMap(WW3DTextureHandle *pTex, TextureClass *pBumpSource)
{
    SurfaceClass::SurfaceDescription    d3dsd;
	SurfaceClass * surf;
    WW3DLockedRect     d3dlr;
	DWORD dwSrcPitch;
	BYTE* pSrc;
	Int numLevels;

#ifdef MIPMAP_BUMP_TEXTURE

	pBumpSource->Get_Level_Description(d3dsd);

	if (Get_Bytes_Per_Pixel(d3dsd.Format) != 4)
	{
		// LORENZEN WAS BUGGED BY THIS, 
		//		DEBUG_CRASH(("WaterRenderObjClass::Invalid BumpMap format - Was it compressed?") );
		return S_OK;
	}
	
	if (pBumpSource->Peek_D3D_Texture())
	{
		numLevels=RenderDevice::Get_Texture_Level_Count(pBumpSource->Peek_D3D_Texture());
	}
	else
		return S_OK;

	pTex[0]=RenderDevice::_Create_Device_Texture(d3dsd.Width,d3dsd.Height,WW3D_FORMAT_U8V8,MIP_LEVELS_ALL,WW3D_POOL_MANAGED,false);

	for (Int level=0; level < numLevels; level++)
	{
		surf=pBumpSource->Get_Surface_Level(level);
		surf->Get_Description(d3dsd);
		pSrc=static_cast<unsigned char *>(surf->Lock(reinterpret_cast<int *>(&dwSrcPitch)));

		RenderDevice::Lock_Texture(pTex[0], level, &d3dlr, nullptr, 0);
		DWORD dwDstPitch = static_cast<DWORD>(d3dlr.Pitch);
		BYTE* pDst       = static_cast<BYTE*>(d3dlr.pBits);

		for( DWORD y=0; y<d3dsd.Height; y++ )
		{
			BYTE* pDstT  = pDst;
			BYTE* pSrcB0 = static_cast<BYTE*>(pSrc);
			BYTE* pSrcB1 = ( pSrcB0 + dwSrcPitch );
			BYTE* pSrcB2 = ( pSrcB0 - dwSrcPitch );

			if( y == d3dsd.Height-1 )  // Don't go past the last line
				pSrcB1 = pSrcB0;
			if( y == 0 )               // Don't go before first line
				pSrcB2 = pSrcB0;

			for( DWORD x=0; x<d3dsd.Width; x++ )
			{
				// Clamp the column neighbours at the row edges like the row neighbours above;
				// otherwise the last pixel reads past the end of the buffer and the first before it.
				LONG v00 = 256-*(pSrcB0+0); // Get the current pixel
				LONG v01 = 256-*(x<d3dsd.Width-1 ? pSrcB0+4 : pSrcB0); // and the pixel to the right
				LONG vM1 = 256-*(x>0 ? pSrcB0-4 : pSrcB0); // and the pixel to the left
				LONG v10 = 256-*(pSrcB1+0); // and the pixel one line below.
				LONG v1M = 256-*(pSrcB2+0); // and the pixel one line above.

				LONG iDu = (vM1-v01); // The delta-u bump value
				LONG iDv = (v1M-v10); // The delta-v bump value

				if( (v00 < vM1) && (v00 < v01) )  // If we are at valley
				{
					iDu = vM1-v00;                 // Choose greater of 1st order diffs
					if( iDu < v00-v01 )
						iDu = v00-v01;
				}

				// The luminance bump value (land masses are less shiny)
				WORD uL = ( v00>1 ) ? 63 : 127;

				switch( WW3D_FMT_V8U8)//m_BumpMapFormat )
				{
					case WW3D_FMT_V8U8:
						*pDstT++ = static_cast<BYTE>(iDu);
						*pDstT++ = static_cast<BYTE>(iDv);
						break;

					case WW3D_FMT_L6V5U5:
						*reinterpret_cast<WORD*>(pDstT)  = static_cast<WORD>( ( (iDu>>3) & 0x1f ) <<  0 );
						*reinterpret_cast<WORD*>(pDstT) |= static_cast<WORD>( ( (iDv>>3) & 0x1f ) <<  5 );
						*reinterpret_cast<WORD*>(pDstT) |= static_cast<WORD>( ( ( uL>>2) & 0x3f ) << 10 );
						pDstT += 2;
						break;

					case WW3D_FMT_X8L8V8U8:
						*pDstT++ = static_cast<BYTE>(iDu);
						*pDstT++ = static_cast<BYTE>(iDv);
						*pDstT++ = static_cast<BYTE>(uL);
						*pDstT++ = static_cast<BYTE>(0L);
						break;
				}

				// Move one pixel to the left (src is 32-bpp)
				pSrcB0+=4;   pSrcB1+=4;   pSrcB2+=4;
			}

			// Move to the next line
			pSrc += dwSrcPitch;    pDst += dwDstPitch;
		}

		RenderDevice::Unlock_Texture(pTex[0], level);
		surf->Unlock();
		REF_PTR_RELEASE (surf);
	}//for each level

#else
	surf=pBumpSource->Get_Surface_Level();
	surf->Get_Description(d3dsd);
	pSrc=(unsigned char *)surf->Lock((int *)&dwSrcPitch);

    // Create the bumpmap's surface and texture objects
	m_pBumpTexture[i]=RenderDevice::_Create_Device_Texture(d3dsd.Width,d3dsd.Height,WW3D_FORMAT_U8V8,MIP_LEVELS_1,WW3D_POOL_MANAGED,false);

    // Fill the bits of the new texture surface with bits from
    // a private format.

    RenderDevice::Lock_Texture(m_pBumpTexture[i], 0, &d3dlr, 0, 0 );
    DWORD dwDstPitch = (DWORD)d3dlr.Pitch;
    BYTE* pDst       = (BYTE*)d3dlr.pBits;

    for( DWORD y=0; y<d3dsd.Height; y++ )
    {
        BYTE* pDstT  = pDst;
        BYTE* pSrcB0 = (BYTE*)pSrc;
        BYTE* pSrcB1 = ( pSrcB0 + dwSrcPitch );
        BYTE* pSrcB2 = ( pSrcB0 - dwSrcPitch );

        if( y == d3dsd.Height-1 )  // Don't go past the last line
            pSrcB1 = pSrcB0;
        if( y == 0 )               // Don't go before first line
            pSrcB2 = pSrcB0;

        for( DWORD x=0; x<d3dsd.Width; x++ )
        {
            // Clamp the column neighbours at the row edges to stay inside the buffer.
            LONG v00 = 256-*(pSrcB0+0); // Get the current pixel
            LONG v01 = 256-*(x<d3dsd.Width-1 ? pSrcB0+4 : pSrcB0); // and the pixel to the right
            LONG vM1 = 256-*(x>0 ? pSrcB0-4 : pSrcB0); // and the pixel to the left
            LONG v10 = 256-*(pSrcB1+0); // and the pixel one line below.
            LONG v1M = 256-*(pSrcB2+0); // and the pixel one line above.

            LONG iDu = (vM1-v01); // The delta-u bump value
            LONG iDv = (v1M-v10); // The delta-v bump value

            if( (v00 < vM1) && (v00 < v01) )  // If we are at valley
            {
                iDu = vM1-v00;                 // Choose greater of 1st order diffs
                if( iDu < v00-v01 )
                    iDu = v00-v01;
            }

            // The luminance bump value (land masses are less shiny)
            WORD uL = ( v00>1 ) ? 63 : 127;

            switch( WW3D_FMT_V8U8)//m_BumpMapFormat )
            {
                case WW3D_FMT_V8U8:
                    *pDstT++ = (BYTE)iDu;
                    *pDstT++ = (BYTE)iDv;
                    break;

                case WW3D_FMT_L6V5U5:
                    *(WORD*)pDstT  = (WORD)( ( (iDu>>3) & 0x1f ) <<  0 );
                    *(WORD*)pDstT |= (WORD)( ( (iDv>>3) & 0x1f ) <<  5 );
                    *(WORD*)pDstT |= (WORD)( ( ( uL>>2) & 0x3f ) << 10 );
                    pDstT += 2;
                    break;

                case WW3D_FMT_X8L8V8U8:
                    *pDstT++ = (BYTE)iDu;
                    *pDstT++ = (BYTE)iDv;
                    *pDstT++ = (BYTE)uL;
                    *pDstT++ = (BYTE)0L;
                    break;
            }

            // Move one pixel to the left (src is 32-bpp)
            pSrcB0+=4;   pSrcB1+=4;   pSrcB2+=4;
        }

        // Move to the next line
        pSrc += dwSrcPitch;    pDst += dwDstPitch;
    }

    RenderDevice::Unlock_Texture(m_pBumpTexture[i], 0);
    surf->Unlock();
#endif

    return S_OK;
}

//-------------------------------------------------------------------------------------------------
/** Create and fill a D3D vertex buffer with water surface vertices */
//-------------------------------------------------------------------------------------------------
HRESULT WaterRenderObjClass::generateVertexBuffer( Int sizeX, Int sizeY, Int vertexSize, Bool doStatic)
{
	m_numVertices=sizeX*sizeY;
	//Assuming dynamic vertex buffer, allocate maximum multiple of required size to allow rendering from
 	//different parts of the buffer. 5-15-03: Disabled this since we use DISCARD mode instead to avoid Nvidia Runtime bug. -MW
 	//m_numVertices=(65536 / (sizeX*sizeY))*sizeX*sizeY;

	SEA_PATCH_VERTEX* pVertices;

	Setting *setting=&m_settings[m_tod];

	HRESULT hr;

	//default setting for a dynamic vertex buffer
	unsigned pool = WW3D_POOL_DEFAULT;
	DWORD usage = WW3D_USAGE_WRITEONLY | WW3D_USAGE_DYNAMIC;
	DWORD fvf = WATER_MESH_FVF;

	if (doStatic)
	{	//change settings for a static vertex buffer
		pool = WW3D_POOL_MANAGED;
		usage = WW3D_USAGE_WRITEONLY;
		fvf=0;// DX8 Docs confusing on this. Say no FVF for vertex shaders. Else RENDER_FVF_XYZDUV1;
		m_numVertices=sizeX*sizeY;
	}

	if (m_vertexBufferD3D == nullptr)
	{	// Create vertex buffer

		if (FAILED(hr=RenderDevice::Create_Device_Vertex_Buffer
		(
			m_numVertices*vertexSize,
			usage,
			fvf,
			pool,
			&m_vertexBufferD3D
		)))
			return hr;
	}

	m_vertexBufferD3DOffset=0;

	if (!doStatic)
		return S_OK;	//only create the buffer, other code will fill it.

	// load results into buffer
	if (FAILED(hr=RenderDevice::Lock_Vertex_Buffer
	(
		m_vertexBufferD3D,
		0,
		m_numVertices*sizeof(SEA_PATCH_VERTEX), 
		(void**)&pVertices,
		0//WW3D_LOCK_DISCARD
	)))
		return hr;

	Int x,z;
	for (z=0; z<sizeY; z++)
	{
		for (x=0; x<sizeX; x++)
		{
			pVertices->x=static_cast<float>(x);
			pVertices->y=m_level;
			pVertices->z=static_cast<float>(z);

			pVertices->tu=static_cast<float>(x)*PATCH_UV_SCALE;
			pVertices->tv=static_cast<float>(z)*PATCH_UV_SCALE;
			pVertices->c=setting->transparentWaterDiffuse;	//vertex alpha/color
			pVertices++;
		}
	}

	if (FAILED(hr=RenderDevice::Unlock_Vertex_Buffer(m_vertexBufferD3D))) return hr;

	return S_OK;
}

//-------------------------------------------------------------------------------------------------
/** Create and fill a D3D index buffer with water surface strip indices */
//-------------------------------------------------------------------------------------------------
HRESULT WaterRenderObjClass::generateIndexBuffer(Int sizeX, Int sizeY)
{
	HRESULT hr;

	//Will need SizeY-1 strips, each of length SizeX*2 (2 indices per strip segment).
	//Will also need 2 extra indices to connect each strip to next one (except last strip)
	//Total index buffer size = (SizeY-1)*(SizeX*2+2) - 2 (drop the extra 2 indices from last strip)

	m_numIndices=(sizeY-1)*(sizeX*2+2) - 2;

	//old way

	// Create index buffer
	WORD* pIndices;

	if (FAILED(hr=RenderDevice::Create_Device_Index_Buffer
	(
		(m_numIndices+2)*sizeof(WORD),
		WW3D_USAGE_WRITEONLY,
		WW3D_FMT_INDEX16,
		WW3D_POOL_MANAGED,
		&m_indexBufferD3D
	)))
		return hr;

	if (FAILED(hr=RenderDevice::Lock_Index_Buffer
	(
		m_indexBufferD3D,
		0, 
		m_numIndices*sizeof(WORD), 
		(void**)&pIndices, 
		0
	)))
		return hr;

	Int i,j,k;

	for (i=0,j=0,k=0; i<m_numIndices; j++)
	{
		for (;k<(sizeX*(j+1)); k++,i+=2)
		{
			pIndices[i]=static_cast<WORD>(static_cast<UnsignedShort>(k)+sizeX);
			pIndices[i+1]=static_cast<UnsignedShort>(k);
		}
		//Generate 4 degenerate triangle to connect current strip to next strip/row of map
		//To do this, we just repeat the last index of first strip and first index of new strip.
		//Any triangles with repeated vertices will be skipped during rendering.
		if (i<m_numIndices) //check if there is at least 1 more strip to go
		{
			pIndices[i]=static_cast<WORD>(k-1);
			pIndices[i+1]=static_cast<WORD>(k+sizeX);
			i+=2;
		}
	}

	/*Old way
	Int step=1;
	Int psize=(size-1)/step;

	m_numIndices=psize*((psize+1)*2)+(psize*2)-2;


	Int x,z,s_toggle=1;
	for (z=step; z<size; z+=step)
	{
		if (s_toggle)
		{
			for (x=0; x<(size-step); x+=step)
			{
				*pIndices++=(WORD)((z-0)*size+(x));
				*pIndices++=(WORD)((z-step)*size+(x));
			}
				*pIndices++=(WORD)((z-0)*size+(size-1));
			*pIndices++=(WORD)((z-step)*size+(size-1));
			// insert additional degenerate to start next row
			*pIndices++=pIndices[-2];
			*pIndices++=pIndices[-1];
		}
		else
		{
			*pIndices++=(WORD)((z-step)*size+(size-1));
			*pIndices++=(WORD)((z-0)*size+(size-1));
			for (x=size-1; x>0; x-=step)
			{
				*pIndices++=(WORD)((z-step)*size+(x-step));
				*pIndices++=(WORD)((z-0)*size+(x-step));
			}
			// insert additional degenerate to start next row
			*pIndices++=pIndices[-1];
			*pIndices++=pIndices[-1];
		}

		s_toggle=!s_toggle;
	}
*/
	if (FAILED(hr=RenderDevice::Unlock_Index_Buffer(m_indexBufferD3D))) return hr;

	return S_OK;
}

//-------------------------------------------------------------------------------------------------
/** Releases all w3d assets, to prepare for Reset device call. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::ReleaseResources(void)
{

	REF_PTR_RELEASE(m_indexBuffer);

	REF_PTR_RELEASE(m_pReflectionTexture);
	SAFE_RELEASE(m_vertexBufferD3D);
	SAFE_RELEASE(m_indexBufferD3D);

	if (m_waterTrackSystem)
		m_waterTrackSystem->ReleaseResources();

	if (m_dwWavePixelShader)
		RenderDevice::Release_Resource(m_dwWavePixelShader);

	if (m_dwWaveVertexShader)
		RenderDevice::Release_Resource(m_dwWaveVertexShader);

	if (m_waveVertexDeclaration)
		RenderDevice::Release_Resource(m_waveVertexDeclaration);


	if (m_trapezoidWaterPixelShader)
		RenderDevice::Release_Resource(m_trapezoidWaterPixelShader);

	if (m_riverWaterPixelShader)
		RenderDevice::Release_Resource(m_riverWaterPixelShader);

	// The scene copy lives in the default pool and must be released before a reset.
	if (m_realWaterPixelShader)
		RenderDevice::Release_Resource(m_realWaterPixelShader);
	if (m_riverWater2PixelShader)
		RenderDevice::Release_Resource(m_riverWater2PixelShader);
	releaseSceneCopy();

	m_dwWavePixelShader=0;
	m_dwWaveVertexShader=0;
	m_waveVertexDeclaration=0;
	m_trapezoidWaterPixelShader=0;
	m_riverWaterPixelShader=0;
	m_realWaterPixelShader=0;
	m_riverWater2PixelShader=0;
}

//-------------------------------------------------------------------------------------------------
/** (Re)allocates all W3D assets after a reset.. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::ReAcquireResources(void)
{
	HRESULT hr;

	m_indexBuffer=NEW_REF(DeviceIndexBufferClass,(6));
	// Fill up the IB
	{
		DeviceIndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
		//quad of 2 triangles:
		//	3-----2
		//  |    /|
		//  |  /  |
		//	|/    |
		//  0-----1
		ib[0]=3;
		ib[1]=0;
		ib[2]=2;
		ib[3]=2;
		ib[4]=0;
		ib[5]=1;
	}

	//We're using the same grid for either 3D Water Mesh or Pixel/Vertex shader.  Just
	//allocate the right size depending on usage
	if (m_meshData)
	{
		//Create new grid data
		if (FAILED(generateIndexBuffer(m_gridCellsX+1,m_gridCellsY+1)))
			return;
		if (FAILED(generateVertexBuffer(m_gridCellsX+1,m_gridCellsY+1,sizeof(MaterMeshVertexFormat),false)))
			return;
	}
	else
	if (m_waterType == WATER_TYPE_2_PVSHADER)
	{	//pixel/vertex shader based water assets.
		if (FAILED(hr=generateIndexBuffer(PATCH_SIZE,PATCH_SIZE)))
			return;

		if (FAILED(hr=generateVertexBuffer(PATCH_SIZE,PATCH_SIZE,sizeof(SEA_PATCH_VERTEX),true)))
			return;

		// LoadAndCreateD3DShader ignores pDeclaration; the vertex declaration is created below.
		hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\wave.pso", nullptr, 0, false, reinterpret_cast<void**>(&m_dwWavePixelShader));
		if (FAILED(hr))
			return;

		hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\wave.vso", nullptr, 0, true, reinterpret_cast<void**>(&m_dwWaveVertexShader));
		if (FAILED(hr))
			return;

		// An active vertex shader needs an explicit vertex declaration matching SEA_PATCH_VERTEX
		// (position, diffuse, texcoord); without it drawSea has no valid input for wave.vs.hlsl.
		static const WW3DVertexElement waveVertexDecl[] =
		{
			{ 0, 0,  WW3D_DECLTYPE_FLOAT3,   WW3D_DECLMETHOD_DEFAULT, WW3D_DECLUSAGE_POSITION, 0 },
			{ 0, 12, WW3D_DECLTYPE_D3DCOLOR, WW3D_DECLMETHOD_DEFAULT, WW3D_DECLUSAGE_COLOR,    0 },
			{ 0, 16, WW3D_DECLTYPE_FLOAT2,   WW3D_DECLMETHOD_DEFAULT, WW3D_DECLUSAGE_TEXCOORD, 0 },
			WW3D_DECL_END()
		};
		hr = RenderDevice::Create_Vertex_Declaration(waveVertexDecl, &m_waveVertexDeclaration);
		if (FAILED(hr))
			return;

		// Create reflection texture
		m_pReflectionTexture = RenderDevice::Create_Render_Target (SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE);
	}

	if (m_waterTrackSystem)
		m_waterTrackSystem->ReAcquireResources();

	// Pixel shaders are loaded from Data/Shaders (Shader Model 4 variants under Shaders/sm4).
	// If compiling fails, the handle stays NULL and the code falls back to the fixed-function path.
	W3DShaderManager::LoadAndCreateD3DShader("shaders\\riverwater.ps.hlsl", nullptr, 0, false, reinterpret_cast<void**>(&m_riverWaterPixelShader));
	W3DShaderManager::LoadAndCreateD3DShader("shaders\\trapezoidwater.ps.hlsl", nullptr, 0, false, reinterpret_cast<void**>(&m_trapezoidWaterPixelShader));
	// Realistic water; without this shader the classic water is drawn.
	W3DShaderManager::LoadAndCreateD3DShader("shaders\\realwater.ps.hlsl", nullptr, 0, false, reinterpret_cast<void**>(&m_realWaterPixelShader));
	// Rivers for water version 2 (Shader Model 4 only)
	W3DShaderManager::LoadAndCreateD3DShader("shaders\\riverwater2.ps.hlsl", nullptr, 0, false, reinterpret_cast<void**>(&m_riverWater2PixelShader));

	//W3D Invalidate textures after losing the device and since we peek at the textures directly, it won't
	//know to reinit them for us.  Do it here manually:
	if (m_riverTexture && !m_riverTexture->Is_Initialized())
		m_riverTexture->Init();
	if (m_waterNoiseTexture && !m_waterNoiseTexture->Is_Initialized())
		m_waterNoiseTexture->Init();
	if (m_riverAlphaEdge && !m_riverAlphaEdge->Is_Initialized())
		m_riverAlphaEdge->Init();
	if (m_waterSparklesTexture && !m_waterSparklesTexture->Is_Initialized())
		m_waterSparklesTexture->Init();
	if (m_whiteTexture && !m_whiteTexture->Is_Initialized())
	{	m_whiteTexture->Init();
		SurfaceClass *surface=m_whiteTexture->Get_Surface_Level();
		surface->DrawPixel(0,0,0xffffffff);
		REF_PTR_RELEASE(surface);
	}
}

void WaterRenderObjClass::load(void)
{
	if (m_waterTrackSystem)
		m_waterTrackSystem->loadTracks();
}

//-------------------------------------------------------------------------------------------------
/** Initializes water with dimensions and parent scene.
	* During rendering, we will render a water surface of given dimensions
	* and reflect the parent scene in its surface.  For now, waters are
	* forced to be rectangles. */
//-------------------------------------------------------------------------------------------------
Int WaterRenderObjClass::init(Real waterLevel, Real dx, Real dy, SceneClass *parentScene, WaterType type)
{

	m_iBumpFrame=0;
	m_fBumpScale=SEA_BUMP_SCALE;

	m_dx=dx;
	m_dy=dy;
	m_level=waterLevel;

	m_LastUpdateTime=GameEngine_RenderTimeMs();	// render clock (fixed time step in image comparison runs)
	m_uScrollPerMs=0.001f;
	m_vScrollPerMs=0.001f;
	m_uOffset=0;
	m_vOffset=0;

	m_parentScene=parentScene;
	m_waterType = type;

	// Water.ini may lack the WaterTransparency block; the drawing code reads it unchecked.
	if (TheWaterTransparency == nullptr)
		TheWaterTransparency = newInstance(WaterTransparencySetting);

	/// Hack for now
	//m_waterType = WATER_TYPE_0_TRANSLUCENT;

	///@todo: calculate a real normal/distance for arbitrary planes.
	m_planeNormal=Vector3(0,0,1);		//water plane normal
	m_planeDistance=m_level;	//water plane distance(always at zero for now)

	m_meshLight=NEW_REF(LightClass,(LightClass::DIRECTIONAL));	
	m_meshLight->Set_Ambient(Vector3(0.1f,0.1f,0.1f));
	m_meshLight->Set_Diffuse(Vector3(1.0f,1.0f,1.0f));
	m_meshLight->Set_Specular(Vector3(1.0f,1.0f,1.0f));
	m_meshLight->Set_Position(Vector3(1000,1000,1000));
	//testLight->Set_Spot_Direction(Vector3(TheGlobalData->m_terrainLightX,TheGlobalData->m_terrainLightY,TheGlobalData->m_terrainLightZ));
	m_meshLight->Set_Spot_Direction(Vector3(-0.57f,-0.57f,-0.57f));

	//Setup material for 3D Mesh water.
	m_meshVertexMaterialClass=NEW_REF(VertexMaterialClass,());
	m_meshVertexMaterialClass->Set_Shininess(20.0);
	m_meshVertexMaterialClass->Set_Ambient(1.0f,1.0f,1.0f);
	m_meshVertexMaterialClass->Set_Diffuse(1.0f,1.0f,1.0f);
	m_meshVertexMaterialClass->Set_Specular(0.5,0.5,0.5);
	m_meshVertexMaterialClass->Set_Opacity(WATER_MESH_OPACITY);
	m_meshVertexMaterialClass->Set_Lighting(true);

	//
	// assign the data from the WaterSettings[] global to the data for this
	// render object (we at present only have one water plane)
	//
	loadSetting( &m_settings[ TIME_OF_DAY_MORNING ], TIME_OF_DAY_MORNING );
	loadSetting( &m_settings[ TIME_OF_DAY_AFTERNOON ], TIME_OF_DAY_AFTERNOON );
	loadSetting( &m_settings[ TIME_OF_DAY_EVENING ], TIME_OF_DAY_EVENING );
	loadSetting( &m_settings[ TIME_OF_DAY_NIGHT ], TIME_OF_DAY_NIGHT );

	Set_Sort_Level(2);	//force water to be drawn after all other non translucent objects in scene.
	Set_Force_Visible(TRUE);	//water is always visible since it's a composite object made of multiple planes all over the map.

	ReAcquireResources();

	// The bump maps are only used by drawSea, i.e. for WATER_TYPE_2_PVSHADER; load them exactly then.
	if (m_waterType == WATER_TYPE_2_PVSHADER)
	{	//geforce3 specific water requires some extra D3D assets
		//load bump map textures off disk
		TextureClass *pBumpSource;	//temporary textures in a format W3D understands
		TextureClass *pBumpSource2;	//temporary textures in a format W3D understands
		Int i;
		i=NUM_BUMP_FRAMES;
		while (i--)
		{
			char bump_name[128];

			WWFormat::Into(bump_name, "caust{:02d}.tga", i);
			pBumpSource=WW3DAssetManager::Get_Instance()->Get_Texture(bump_name);
			WWFormat::Into(bump_name, "caustS{:02d}.tga", i);
			pBumpSource2=WW3DAssetManager::Get_Instance()->Get_Texture(bump_name);
			initBumpMap(m_pBumpTexture+i, pBumpSource);
			initBumpMap(m_pBumpTexture2+i, pBumpSource2);
			WW3DAssetManager::Get_Instance()->Release_Texture(pBumpSource);
			WW3DAssetManager::Get_Instance()->Release_Texture(pBumpSource2);
			REF_PTR_RELEASE(pBumpSource);
			REF_PTR_RELEASE(pBumpSource2);
		}
	}

	//Setup material for regular water
	m_vertexMaterialClass=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	


	m_shaderClass = zFillAlphaShader;//ShaderClass::_PresetAlphaShader;ShaderClass::_PresetOpaqueShader;//detailOpaqueShader;
	m_shaderClass.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);	//water should be visible from both sides

	//Assets used for all types of water
	m_alphaClippingTexture=WW3DAssetManager::Get_Instance()->Get_Texture(SKYBODY_TEXTURE);

#ifdef CLIP_GEOMETRY_TO_PLANE
	m_alphaClippingTexture=WW3DAssetManager::Get_Instance()->Get_Texture("alphaclip.tga");
#endif

	m_skyBox = (static_cast<W3DAssetManager*>(W3DAssetManager::Get_Instance()))->Create_Render_Obj( "new_skybox", TheGlobalData->m_skyBoxScale, 0);

	//Enable clamping on all textures used by the skybox (to reduce corner seams).
	if (m_skyBox && m_skyBox->Class_ID() == RenderObjClass::CLASSID_MESH)
	{
		MeshClass *mesh=static_cast<MeshClass*>(m_skyBox);
		MaterialInfoClass	*material = mesh->Get_Material_Info();

		for (Int i=0; i<material->Texture_Count(); i++)
		{
			if (material->Peek_Texture(i))
			{		
				material->Peek_Texture(i)->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
				material->Peek_Texture(i)->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
			}
		}

		REF_PTR_RELEASE(material);
	}

	m_riverTexture=WW3DAssetManager::Get_Instance()->Get_Texture(TheWaterTransparency->m_standingWaterTexture.str()); 

	//For some reason setting a NULL texture does not result in 0xffffffff for pixel shaders so using explicit "white" texture.
	m_whiteTexture=MSGNEW("TextureClass") TextureClass(1,1,WW3D_FORMAT_A4R4G4B4,MIP_LEVELS_1);
	SurfaceClass *surface=m_whiteTexture->Get_Surface_Level();
	surface->DrawPixel(0,0,0xffffffff);
	REF_PTR_RELEASE(surface);

	m_waterNoiseTexture = LoadFreeWaterTexture("wasser_rauschen.dds", "Noise0000.tga");
	m_riverAlphaEdge = LoadFreeWaterTexture("wasser_flusskante.dds", "TWAlphaEdge.tga");
	m_waterSparklesTexture = LoadFreeWaterTexture("wasser_glitzern.dds", "WaterSurfaceBubbles.tga");
	createWaveNormalTexture();
#ifdef DRAW_WATER_WAKES
	m_waterTrackSystem = std::make_unique<WaterTracksRenderSystem>();
	m_waterTrackSystem->init();
#endif

	return 0;
}

void WaterRenderObjClass::updateMapOverrides(void)
{
	if (m_riverTexture && TheWaterTransparency->m_standingWaterTexture.compareNoCase(m_riverTexture->Get_Texture_Name()) != 0)
	{
		REF_PTR_RELEASE(m_riverTexture);
		m_riverTexture = WW3DAssetManager::Get_Instance()->Get_Texture(TheWaterTransparency->m_standingWaterTexture.str());
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::reset( void )
{

	// for vertex animated water mesh reset the values
	if( m_meshData)
	{
		Int i, j;
		WaterMeshData *pData;
		Int	mx = m_gridCellsX + 1;
		Int my = m_gridCellsY + 1;

		// go through each mesh point and adjust the height according to the velocity
		for( j = 0, pData = m_meshData; j < (my + 2); j++ )
		{	

			for( i = 0; i < (mx + 2); i++ )
			{

				// areset grid values for this cell
				pData->velocity = 0.0f;
				pData->height = 0.0f;
				pData->preferredHeight = static_cast<UnsignedByte>(0.0f);
				pData->status = WaterRenderObjClass::AT_REST;

				// on to the next one
				pData++;

			}  // end for i

		}  // end for j

		// mesh data is no longer in motion
		m_meshInMotion = FALSE;

	}  // end if, water type 3

	if (m_waterTrackSystem)
		m_waterTrackSystem->reset();
} 

// ------------------------------------------------------------------------------------------------
// Drawn frames between two ticks show the river motion of the last tick advanced by the interpolation fraction, so it
// flows smoothly at any frame rate; alpha 0 (the tick's own frame without interpolation) shows the tick's values.
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::setRenderInterpolation( Real alpha )
{
	m_riverVOrigin = m_riverVOriginTick + alpha * RIVER_V_PER_TICK;
	m_riverXOffset = m_riverXOffsetTick + alpha * RIVER_X_PER_TICK;
	m_riverYOffset = m_riverYOffsetTick + alpha * RIVER_Y_PER_TICK;
	if (m_riverXOffset > 1) m_riverXOffset -= 1;
	if (m_riverYOffset > 1) m_riverYOffset -= 1;
}

// ------------------------------------------------------------------------------------------------
/** Update phase for water if we need it.  This called once per client frame reguardless
	* of how fast the logic framerate is running */
// ------------------------------------------------------------------------------------------------
void WaterRenderObjClass::update( void )
{
	static UnsignedInt lastLogicFrame = 0;
	UnsignedInt currLogicFrame = 0;
	
	if( TheGameLogic )
		currLogicFrame = TheGameLogic->getFrame();

	m_riverVOriginTick += RIVER_V_PER_TICK;
	m_riverXOffsetTick += RIVER_X_PER_TICK;
	m_riverYOffsetTick += RIVER_Y_PER_TICK;
	if (m_riverXOffsetTick > 1) m_riverXOffsetTick -= 1;
	if (m_riverYOffsetTick > 1) m_riverYOffsetTick -= 1;
	if (m_riverXOffsetTick < -1) m_riverXOffsetTick += 1;
	if (m_riverYOffsetTick < -1) m_riverYOffsetTick += 1;
	m_riverVOrigin = m_riverVOriginTick;
	m_riverXOffset = m_riverXOffsetTick;
	m_riverYOffset = m_riverYOffsetTick;
 	m_iBumpFrame++;
	if (m_iBumpFrame >= NUM_BUMP_FRAMES) {
		m_iBumpFrame = 0;
	}



	// we only process some things if the logic frame has changed
	if( lastLogicFrame != currLogicFrame )
	{

		// for vertex animated water we need to update the vector field
		if( m_doWaterGrid && m_meshInMotion == TRUE )
		{
			const Real PREFERRED_HEIGHT_FUDGE = 1.0f;		///< this is close enough to at rest
			const Real AT_REST_VELOCITY_FUDGE = 1.0f;		///< when we're close enought to at rest height and velocity we will stop
			const Real WATER_DAMPENING = 0.93f;					///< use with up force of 15.0
			Int i, j;
			Int	mx = m_gridCellsX+1;
			Int my = m_gridCellsY+1;
			WaterMeshData *pData;

			//
			// we will mark the mesh as clean now ... if any of the fields are still in motion
			// they will continue to mark the mesh as dirty so processing continues next frame
			//
			m_meshInMotion = FALSE;

			// go through each mesh point and adjust the height according to the velocity
			for( j = 0, pData = m_meshData; j < (my + 2); j++ )
			{	

				for( i = 0; i < (mx + 2); i++ )
				{

					// only pay attention to mesh points that are in motion
					if( BitTest( pData->status, WaterRenderObjClass::IN_MOTION ) )
					{

						// DAMPENING to slow the changes down
						pData->velocity *= WATER_DAMPENING;

						// if the height here is below our preferred height, we want to add upward force to counteract it
						if( pData->height < pData->preferredHeight )
							pData->velocity -= TheGlobalData->m_gravity * 3.0f;
						else				
							pData->velocity += TheGlobalData->m_gravity * 3.0f;

						// adjust the height at this grid location according to the current velocity		
						pData->height = pData->height + pData->velocity;

						//
						// if we are close enough to our preferred height and our velocity is small enough
						// this will be our resting location
						//
						if( fabs( pData->height - pData->preferredHeight ) < PREFERRED_HEIGHT_FUDGE &&
								fabs( pData->velocity ) < AT_REST_VELOCITY_FUDGE )
						{

							BitClear( pData->status, WaterRenderObjClass::IN_MOTION );
							pData->height = pData->preferredHeight;
							pData->velocity = 0.0f;

						}  // end if
						else
						{

							// there is still motion in the mesh, we need to process next frame
							m_meshInMotion = TRUE;

						}  // end else

					}  // end if

					// on to the next one
					pData++;

				}  // end for i

			}  // end for j

		}  // end if

		// mark the last logic frame we processed on
		lastLogicFrame = currLogicFrame;

	}  // end if, a logic frame has passed

}  // end update


//-------------------------------------------------------------------------------------------------
/** Adjusts various water/sky rendering settings that depend on time of day. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::setTimeOfDay(TimeOfDay tod)
{
	m_tod=tod;
	if (m_waterType == WATER_TYPE_2_PVSHADER)
		generateVertexBuffer(PATCH_SIZE,PATCH_SIZE,sizeof(SEA_PATCH_VERTEX),true);	//update the water mesh with new lighting/alpha
}

//-------------------------------------------------------------------------------------------------
/** Loaded width of a texture over its original width. From the texture package if it knows the
texture, else 1 (original, mod or loose file). */
//-------------------------------------------------------------------------------------------------
static Real textureScaleFactor(const AsciiString &textureName, unsigned loadedWidth)
{
	if (textureName.isEmpty())
		return 1.0f;
	unsigned logicalWidth = 0, logicalHeight = 0;
	if (::Engine::TexturePackage::instance().logicalSize(textureName.str(), logicalWidth, logicalHeight))
		return ::Engine::TexturePackage::instance().scaleFactor(textureName.str(), loadedWidth);
	return 1.0f;
}

//-------------------------------------------------------------------------------------------------
/**Copies GDF settings dealing with a particular time of day into our own
	* structures.  Also allocates any required W3D assets (textures). */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::loadSetting( Setting *setting, TimeOfDay timeOfDay )
{
	SurfaceClass::SurfaceDescription surfaceDesc;

	// sanity
	DEBUG_ASSERTCRASH( setting, ("WaterRenderObjClass::loadSetting, NULL setting\n") );
	
	// textures
	setting->skyTexture = WW3DAssetManager::Get_Instance()->Get_Texture( WaterSettings[ timeOfDay ].m_skyTextureFile.str() );
	setting->waterTexture = WW3DAssetManager::Get_Instance()->Get_Texture( WaterSettings[ timeOfDay ].m_waterTextureFile.str() );

	// texelss per unit
	setting->skyTexelsPerUnit = WaterSettings[ timeOfDay ].m_skyTexelsPerUnit;
	// Water.ini may lack the WaterSet of a time of day; then there is no texture to measure.
	if (setting->waterTexture)
	{
		setting->waterTexture->Get_Level_Description( surfaceDesc, 0 );
		// SkyTexelsPerUnit counts texels of the original size. An HD or UHD water texture is 2 or 4 times
		// as wide, so divide by that factor to keep the sky tiling density.
		if (surfaceDesc.Width > 0)
			setting->skyTexelsPerUnit /= static_cast<Real>(surfaceDesc.Width) / textureScaleFactor(WaterSettings[ timeOfDay ].m_waterTextureFile, surfaceDesc.Width);
	}

	// water repeat
	setting->waterRepeatCount = WaterSettings[ timeOfDay ].m_waterRepeatCount;

	// U and V scroll per ms
	setting->uScrollPerMs = WaterSettings[ timeOfDay ].m_uScrollPerMs;
	setting->vScrollPerMs = WaterSettings[ timeOfDay ].m_vScrollPerMs;

	//
	// vertex colors
	//
	// bottom left
	setting->vertex00Diffuse = (WaterSettings[ timeOfDay ].m_vertex00Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex00Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex00Diffuse.blue;
	// top left
	setting->vertex01Diffuse = (WaterSettings[ timeOfDay ].m_vertex01Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex01Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex01Diffuse.blue;
	// bottom right
	setting->vertex10Diffuse = (WaterSettings[ timeOfDay ].m_vertex10Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex10Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex10Diffuse.blue;
	// top right
	setting->vertex11Diffuse = (WaterSettings[ timeOfDay ].m_vertex11Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex11Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex11Diffuse.blue;

	// diffuse water color
	setting->waterDiffuse = (WaterSettings[ timeOfDay ].m_waterDiffuseColor.alpha << 24) |
												  (WaterSettings[ timeOfDay ].m_waterDiffuseColor.red		<< 16) |
													(WaterSettings[ timeOfDay ].m_waterDiffuseColor.green << 8) |
												   WaterSettings[ timeOfDay ].m_waterDiffuseColor.blue;

	// transparent water color
	setting->transparentWaterDiffuse = (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.alpha << 24) |
																		 (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.red	 << 16) |
																		 (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.green << 8) |
																		  WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.blue;

}

//-------------------------------------------------------------------------------------------------
/** Our water may use effects that require run-time rendered textures.  These
	*	textures need to be updated before we start rendering to the main screen
	* render target because D3D doesn't multiple render targets. */
//-------------------------------------------------------------------------------------------------
void WaterRenderObjClass::updateRenderTargetTextures(CameraClass *cam)
{
	if (m_waterType == WATER_TYPE_2_PVSHADER && getClippedWaterPlane(cam, nullptr) &&
		TheTerrainRenderObject && TheTerrainRenderObject->getMap())
		renderMirror(cam);	//generate texture containing reflected scene
}


/**Utility function used to query water heights in a manner that works in both RTS and WB.*/
Real WaterRenderObjClass::getWaterHeight(Real x, Real y)
{
	const WaterHandle *waterHandle = nullptr;
	Real waterZ = 0.0f;
	ICoord3D iLoc;

	iLoc.x = REAL_TO_INT_FLOOR( x + 0.5f );
	iLoc.y = REAL_TO_INT_FLOOR( y + 0.5f );
	iLoc.z = 0;

	for( PolygonTrigger *pTrig = PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext() ) 
	{

		if( !pTrig->isWaterArea() ) 
			continue;

		// See if point is in a water area
		if( pTrig->pointInTrigger( iLoc ) ) 
		{

			if( pTrig->getPoint( 0 )->z >= waterZ )
			{

				waterZ = static_cast<Real>(pTrig->getPoint( 0 )->z);
				waterHandle = pTrig->getWaterHandle();

			}  // end if

		}  // end if

	}  // end for

	if (waterHandle)
		return static_cast<Real>(waterHandle->m_polygon->getPoint( 0 )->z);
	return INVALID_WATER_HEIGHT;	//point not underwater
}


//-------------------------------------------------------------------------------------------------
//debug version where moon rotates with the camera	(always upright on screen)
//-------------------------------------------------------------------------------------------------


