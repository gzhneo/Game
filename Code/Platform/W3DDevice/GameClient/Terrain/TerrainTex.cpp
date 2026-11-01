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

// FILE: TerrainTex.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: TerrainTex.cpp
//
// Created:   John Ahlquist, April 2001
//
// Desc:      TextureClass overrides to perform custom texturing for the terrain.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes                                                      
//-----------------------------------------------------------------------------
#include <stdlib.h>

#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/TileData.h"
#include "Common/GlobalData.h"
#include "Common/GameEngine.h"	// GameEngine_RenderTimeMs
#include "WW3D2/Common/renderdevice.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"	// applyTerrainAtlasFilter
#include "WW3D2/Material/texutil.h"	// DirectXTex-based texture helpers
#include "WW3D2/System/wwd3dmath.h"	// replacement for the D3DX math helpers

/******************************************************************************
						TerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// TerrainTextureClass::TerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to create a 32 bit per pixel 
texture of the desired height and mip level. */
// The atlas is A8R8G8B8 like the edge texture (AlphaEdgeTextureClass) so the 24-bit source textures
// keep all color bits (no banding in soft gradients).
//=============================================================================
TerrainTextureClass::TerrainTextureClass(int height) :
	TextureClass(TEXTURE_WIDTH, height, 
		WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_3 )
{
}

//=============================================================================
// TerrainTextureClass::update
//=============================================================================
/** Sets the tile bitmap data into the texture.  The tiles are placed with 4
	pixel borders around them, so that when the tiles are scaled and bilinearly
	interpolated, you don't get seams between the tiles.  */
//=============================================================================
int TerrainTextureClass::update(WorldHeightMap *htMap)
{
	// D3DTexture is our texture;

	WW3DSurfaceHandle surface_level;
	WW3DSurfaceDesc surface_desc;
	WW3DLockedRect locked_rect;
	Device_ErrorCode(RenderDevice::Get_Texture_Surface_Level(Peek_D3D_Texture(), 0, &surface_level));
	Device_ErrorCode(RenderDevice::Get_Surface_Desc(surface_level, &surface_desc));
	if (surface_desc.Width < TEXTURE_WIDTH) {
		// release the surface in the error path too
		RenderDevice::Release_Resource(surface_level);
		return 0;
	}

	Device_ErrorCode(RenderDevice::Lock_Surface(surface_level, &locked_rect, nullptr, 0));

	Int tilePixelExtent = TILE_PIXEL_EXTENT;		 
	Int tilesPerRow = surface_desc.Width/(2*TILE_PIXEL_EXTENT+TILE_OFFSET);
	tilesPerRow *= 2;
//	Int numRows = surface_desc.Height/(tilePixelExtent+TILE_OFFSET);
#ifdef _DEBUG
	//DEBUG_ASSERTCRASH(tilesPerRow*numRows >= htMap->m_numBitmapTiles, ("Too many tiles."));
	DEBUG_ASSERTCRASH((Int)surface_desc.Width >= tilePixelExtent*tilesPerRow, ("Bitmap too small."));
#endif
	if (surface_desc.Format == WW3D_FMT_A8R8G8B8) {
		Int tileNdx;
		Int pixelBytes = 4;
		// The lock returns memory whose content is undefined; only the tiles and the borders of the texture classes
		// are written. The smaller mip levels average the gaps between the classes into their edges, so preset the
		// whole level with 0 to give the atlas the same content on every load.
		::memset(locked_rect.pBits, 0, static_cast<size_t>(locked_rect.Pitch)*surface_desc.Height);
		for (tileNdx=0; tileNdx < htMap->m_numBitmapTiles; tileNdx++) {
			TileData *pTile = htMap->getSourceTile(tileNdx);
			if (!pTile) continue;
			ICoord2D position = pTile->m_tileLocationInTexture;
			if (position.x<=0) continue; // all real tile offsets start at 2.  jba.

			Int i,j;
			for (j=0; j<tilePixelExtent; j++) {
				UnsignedByte *pBGR = pTile->getRGBDataForWidth(tilePixelExtent);
				pBGR += (tilePixelExtent-1-j)*TILE_BYTES_PER_PIXEL*tilePixelExtent; // invert to match.
				Int row = position.y+j;
				UnsignedByte *pBGRX = (static_cast<UnsignedByte*>(locked_rect.pBits)) +
							(row)*surface_desc.Width*pixelBytes;

				Int column = position.x;
				pBGRX += column*pixelBytes;
				for (i=0; i<tilePixelExtent; i++) {
					// Source is BGR(A) with 8 bits per channel, destination BGRA; alpha fully opaque.
					pBGRX[0] = pBGR[0];
					pBGRX[1] = pBGR[1];
					pBGRX[2] = pBGR[2];
					pBGRX[3] = 0xff;
					pBGRX +=pixelBytes;
					pBGR +=TILE_BYTES_PER_PIXEL;
				}
			}
		}
		// Now draw the 4 pixel border around each tile class.
		Int texClass;
		for (texClass=0; texClass<htMap->m_numTextureClasses; texClass++) {
			Int width = htMap->m_textureClasses[texClass].width;
			ICoord2D origin = htMap->m_textureClasses[texClass].positionInTexture;
			if (origin.x<=0) continue;
			width *= TILE_PIXEL_EXTENT;
			// Duplicate 4 columns of pixels before and after.
			Int j;
			for (j=0; j<width; j++) {
				Int row = origin.y+j;
				UnsignedByte *pBGRX = (static_cast<UnsignedByte*>(locked_rect.pBits)) +
							(row)*surface_desc.Width*pixelBytes;

				Int column = origin.x;
				pBGRX += column*pixelBytes;
				// copy before
				memcpy(pBGRX-(4)*pixelBytes, pBGRX+(width-4)*pixelBytes, 4*pixelBytes);
				// copy after
				memcpy(pBGRX+(width*pixelBytes), pBGRX, 4*pixelBytes);
			}

			// Duplicate 4 rows of pixels before and after.
			for (j=0; j<4; j++) {
				// copy before.
				Int row = origin.y-j-1;
				UnsignedByte *pBGRX = (static_cast<UnsignedByte*>(locked_rect.pBits)) +
							(row)*surface_desc.Width*pixelBytes;
				UnsignedByte *target = pBGRX+(origin.x-4)*pixelBytes; 
				memcpy(target, target+width*surface_desc.Width*pixelBytes, (width+8)*pixelBytes);
				// copy after.
				row = origin.y+j;
				pBGRX = (static_cast<UnsignedByte*>(locked_rect.pBits)) +
							(row)*surface_desc.Width*pixelBytes;
				target = pBGRX+(origin.x-4)*pixelBytes; 
				memcpy(target+width*surface_desc.Width*pixelBytes, target, (width+8)*pixelBytes);
			}

		}

	}
	RenderDevice::Unlock_Surface(surface_level);
	RenderDevice::Release_Resource(surface_level);
	Device_ErrorCode(TexUtil::Generate_Mip_Levels(Peek_D3D_Texture(), TexUtil::FILTER_BOX));	
	if (TheWritableGlobalData->m_textureReductionFactor) {
		RenderDevice::Set_Texture_LOD(Peek_D3D_Texture(), TheWritableGlobalData->m_textureReductionFactor);
	}
	return(surface_desc.Height);
}


//=============================================================================
// TerrainTextureClass::setLOD
//=============================================================================
/** Sets the lod of the texture to be loaded into the video card.  */
//=============================================================================
void TerrainTextureClass::setLOD(Int LOD)
{
	RenderDevice::Set_Texture_LOD(Peek_D3D_Texture(), LOD);	// checks for NULL itself
}
//=============================================================================
// TerrainTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup
(standard D3D setup, but beyond the scope of W3D).  */
//=============================================================================
void TerrainTextureClass::Apply(unsigned int stage)
{
	// Do the base apply.
	TextureClass::Apply(stage);
}

/******************************************************************************
						AlphaTerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// AlphaTerrainTextureClass::AlphaTerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to creat a throw away 8x8 texture, 
then uses the base texture's D3D texture. This way the base tiles pass, drawn
using TerrainTextureClass shares the same texture with the blended edges pass,
saving lots of texture memory, and preventing seams between blended tiles. */
//=============================================================================
AlphaTerrainTextureClass::AlphaTerrainTextureClass( TextureClass *pBaseTex ): 
	TextureClass(8, 8, 
		WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1 )	// same format as the base texture
{ 
	// Attach the base texture's d3d texture.
	WW3DTextureHandle d3d_tex = pBaseTex->Peek_D3D_Texture();
	Set_D3D_Base_Texture(d3d_tex);
}


//=============================================================================
// AlphaTerrainTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup.
This may be applied in either single pass, as the second texture in the pipe, 
or multipass.  If stage==0, we are doing multipass and we set up the pipe 
for a single texture.  If stage==1, then we are doing a single pass, and we
set up the pipe so that we blend onto the base texture in stage 0.
(standard D3D setup, but beyond the scope of W3D). */
//=============================================================================
void AlphaTerrainTextureClass::Apply(unsigned int stage)
{
	// Do the base apply.
	TextureClass::Apply(stage);
	
	// Set the bilinear or trilinear filtering.
	if (TheGlobalData && TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex) {
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	} else {
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MAGFILTER, WW3D_TEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
	} else {
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MIPFILTER, WW3D_TEXF_POINT);
	}
	// same filter as the base pass (three-way blends)
	W3DShaderManager::applyTerrainAtlasFilter(static_cast<Int>(stage));
	// Since we are using multiple distinct tiles, the textures doesn't wrap, so clamp it.
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);
	// Now setup the texture pipeline.
	if (stage==0) {
		// Modulate the diffuse color with the texture as lighting comes from diffuse.
		RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
			.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
			.Alpha_Op(WW3D_TOP_MODULATE)
			.Tex_Coord_Index(1)
		);
		// Blend the result using the alpha. (came from diffuse mod texture)
		RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,true);
		RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND,WW3D_BLEND_SRCALPHA);
		RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND,WW3D_BLEND_INVSRCALPHA);
		// Disable stage 2.
		RenderDevice::Disable_Texture_Stage(1);
	}	else if (stage==1) {

		if (TheGlobalData && !TheGlobalData->m_multiPassTerrain)
		{
			///@todo: Remove 8-Stage Nvidia hack after drivers are fixed.
			//This method is a backdoor specific to Nvidia based cards.  It will fail on
			//other hardware.  Allows single pass blend of 2 textures and post modulate diffuse.
			RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
				.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
				.Alpha(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
				.Tex_Coord_Index(0)
			);

			RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
				.Color(WW3D_TOP_ADD, WW3D_TA_DIFFUSE | WW3D_TA_COMPLEMENT | WW3D_TA_ALPHAREPLICATE, WW3D_TA_DIFFUSE)
				.Alpha(WW3D_TOP_ADD, WW3D_TA_TFACTOR | WW3D_TA_COMPLEMENT, WW3D_TA_TFACTOR)
				.Tex_Coord_Index(1)
			);

			RenderDevice::Set_Device_Texture(2, nullptr);
			RenderDevice::Set_Texture_Stage(2, TextureStageDesc()
				.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_TEXTURE)
				.Alpha(WW3D_TOP_MODULATE, WW3D_TA_TFACTOR, WW3D_TA_TFACTOR)
				.Tex_Coord_Index(2)
			);

			RenderDevice::Set_Device_Texture(3, nullptr);
			RenderDevice::Set_Texture_Stage(3, TextureStageDesc()
				.Color(WW3D_TOP_SELECTARG1, WW3D_TA_DIFFUSE | 0 | WW3D_TA_ALPHAREPLICATE, WW3D_TA_DIFFUSE)
				.Alpha(WW3D_TOP_SELECTARG1, WW3D_TA_TFACTOR, WW3D_TA_TFACTOR)
				.Tex_Coord_Index(3)
			);

			RenderDevice::Set_Device_Texture(4, nullptr);
			RenderDevice::Set_Texture_Stage(4, TextureStageDesc()
				.Color(WW3D_TOP_MODULATE, WW3D_TA_CURRENT, WW3D_TA_DIFFUSE)
				.Alpha(WW3D_TOP_MODULATE, WW3D_TA_CURRENT, WW3D_TA_DIFFUSE)
				.Tex_Coord_Index(4)
			);

			RenderDevice::Set_Device_Texture(5, nullptr);
			RenderDevice::Set_Texture_Stage(5, TextureStageDesc()
				.Color(WW3D_TOP_ADD, WW3D_TA_DIFFUSE, WW3D_TA_DIFFUSE)
				.Alpha(WW3D_TOP_ADD, WW3D_TA_TFACTOR | WW3D_TA_COMPLEMENT, WW3D_TA_TFACTOR)
				.Tex_Coord_Index(5)
			);

			RenderDevice::Set_Device_Texture(6, nullptr);
			RenderDevice::Set_Texture_Stage(6, TextureStageDesc()
				.Color(WW3D_TOP_MODULATE, WW3D_TA_TFACTOR, WW3D_TA_TFACTOR)
				.Alpha(WW3D_TOP_MODULATE, WW3D_TA_TFACTOR, WW3D_TA_TFACTOR)
				.Tex_Coord_Index(6)
			);

			RenderDevice::Set_Device_Texture(7, nullptr);
			RenderDevice::Set_Texture_Stage(7, TextureStageDesc()
				.Color(WW3D_TOP_SELECTARG1, WW3D_TA_TFACTOR, WW3D_TA_TFACTOR)
				.Alpha(WW3D_TOP_SELECTARG1, WW3D_TA_TFACTOR, WW3D_TA_TFACTOR)
				.Tex_Coord_Index(7)
			);
		}
		else
		{
  			RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Color_Arg1(WW3D_TA_TEXTURE));
			RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
				.Color_Op(WW3D_TOP_SELECTARG1)
				.Alpha_Op(WW3D_TOP_SELECTARG1)
				.Alpha_Arg1(WW3D_TA_TEXTURE)
			);

			RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
				.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_CURRENT)
				.Alpha_Op(WW3D_TOP_SELECTARG1)
				.Alpha_Arg1(WW3D_TA_TEXTURE)
			);
		}
	}
}


/******************************************************************************
						LightMapTerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// LightMapTerrainTextureClass::LightMapTerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to load the .tga texture. */
//=============================================================================
LightMapTerrainTextureClass::LightMapTerrainTextureClass(AsciiString name, MipCountType mipLevelCount) :
TextureClass(name.isEmpty()?"TSNoiseUrb.tga":name.str(),name.isEmpty()?"TSNoiseUrb.tga":name.str(), mipLevelCount )
{ 
	Get_Filter().Set_Min_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	Get_Filter().Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
	Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
}

#define STRETCH_FACTOR ((float)(1/(63.0*MAP_XY_FACTOR/2))) /* covers 63/2 tiles */		

//=============================================================================
// LightMapTerrainTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup.
The LightMapTerrainTextureClass may be applied by itself, or with the 
CloudMapTerrainTextureClass.  This may be applied in either single pass, 
as the second texture in the pipe, 
or multipass.  If stage==0, we are doing multipass and we set up the pipe 
for a single texture.  If stage==1, then we are doing a single pass, and we
set up the pipe so that we blend onto the cloud map texture in stage 0.
Also, texture is mapped using the x/y coordinates of the map, saving us 
yet another set of uv coordinates.
(standard D3D setup, but beyond the scope of W3D). */
//=============================================================================
void LightMapTerrainTextureClass::Apply(unsigned int stage)
{
	TextureClass::Apply(stage);
}









/******************************************************************************
						AlphaEdgeTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

/**
* AlphaEdgeTextureClass - Generates the alpha edge blending for terrain.
*		
*/
AlphaEdgeTextureClass::AlphaEdgeTextureClass( int height, MipCountType mipLevelCount) :
//	TextureClass("EdgingTemplate.tga","EdgingTemplate.tga", mipLevelCount )
	TextureClass(TEXTURE_WIDTH, height, WW3D_FORMAT_A8R8G8B8, mipLevelCount )
{ 

}

int AlphaEdgeTextureClass::update256(WorldHeightMap *htMap)
{
	return 1;
}

int AlphaEdgeTextureClass::update(WorldHeightMap *htMap)
{
	// D3DTexture is our texture;

	WW3DSurfaceHandle surface_level;
	WW3DSurfaceDesc surface_desc;
	WW3DLockedRect locked_rect;
	Device_ErrorCode(RenderDevice::Get_Texture_Surface_Level(Peek_D3D_Texture(), 0, &surface_level));
	Device_ErrorCode(RenderDevice::Lock_Surface(surface_level, &locked_rect, nullptr, 0));
	Device_ErrorCode(RenderDevice::Get_Surface_Desc(surface_level, &surface_desc));

	Int tilePixelExtent = TILE_PIXEL_EXTENT; // blend tiles are 1/4 tiles.
//	Int tilesPerRow = surface_desc.Width / (tilePixelExtent+8);

//	Int numRows = surface_desc.Height/(tilePixelExtent+8);

	if (surface_desc.Format == WW3D_FMT_A8R8G8B8) {
#if 1
#if 1
		Int cellX, cellY;
		for (cellX = 0; static_cast<UnsignedInt>(cellX) < surface_desc.Width; cellX++) {
			for (cellY = 0; static_cast<UnsignedInt>(cellY) < surface_desc.Height; cellY++) {
				UnsignedByte *pBGR = (static_cast<UnsignedByte *>(locked_rect.pBits))+(cellY*surface_desc.Width+cellX)*4;
				pBGR[2] = static_cast<UnsignedByte>(255-cellY/2);
				pBGR[0] = static_cast<UnsignedByte>(cellX/2);
				pBGR[3] = static_cast<UnsignedByte>(cellX/2);  // alpha.
				pBGR[3] = 128;  // alpha.
			}
		}
#endif
#if 1
		Int tileNdx;
		Int pixelBytes = 4;
		for (tileNdx=0; tileNdx < htMap->m_numEdgeTiles; tileNdx++) {
			TileData *pTile = htMap->getEdgeTile(tileNdx);
			if (!pTile) continue;
			ICoord2D position = pTile->m_tileLocationInTexture;
			if (position.x<=0) continue; // all real edge offsets start at 4.  jba.
			Int i,j;
			Int column = position.x;
			for (j=0; j<tilePixelExtent; j++) {
				Int row = position.y+j;
				UnsignedByte *pBGR = htMap->getEdgeTile(tileNdx)->getRGBDataForWidth(tilePixelExtent);
				pBGR += (tilePixelExtent-1-j)*TILE_BYTES_PER_PIXEL*tilePixelExtent; // invert to match.
				UnsignedByte *pBGRX = (static_cast<UnsignedByte*>(locked_rect.pBits)) +
							(row)*surface_desc.Width*pixelBytes;
				pBGRX += column*pixelBytes;

				for (i=0; i<tilePixelExtent; i++) {
					pBGRX[0] = pBGR[0];  //r
					pBGRX[1] = pBGR[1];	//g
					pBGRX[2] = pBGR[2];	//b
					if (pBGR[0]==0 && pBGR[1]==0 && pBGR[2]==0) {
						pBGRX[3] = 0x80;
					} else if (pBGR[0]==0xff && pBGR[1]==0xff && pBGR[2]==0xff) {
						pBGRX[3] = 0x00;
					}	else {
						pBGRX[3] = 0xff;
					}
						
					pBGRX += pixelBytes;
					pBGR += TILE_BYTES_PER_PIXEL;
				}
			}
		}
#endif
#endif
	}
	RenderDevice::Unlock_Surface(surface_level);
	RenderDevice::Release_Resource(surface_level);
	Device_ErrorCode(TexUtil::Generate_Mip_Levels(Peek_D3D_Texture(), TexUtil::FILTER_BOX));
	return(surface_desc.Height);
}

void AlphaEdgeTextureClass::Apply(unsigned int stage)
{
	// Do the base apply.
	TextureClass::Apply(stage);
}

/******************************************************************************
		Terrain atlases for higher resolution tiles
******************************************************************************/
// If the terrain tiles have 64*s pixels (WorldHeightMap::getTerrainTexture unifies all tiles, s > 1), the
// atlases are built physically s times larger. Layout, borders and texture coordinates stay logical
// (TEXTURE_WIDTH, TILE_PIXEL_EXTENT, TILE_OFFSET); every logical position is multiplied by s, and the
// height returned is the logical one. The mip chain is log2(s) levels longer so the smallest level
// matches the SD atlas. Separate functions keep the SD path (update) untouched. Always 32 bit.

static MipCountType scaledMipLevels(int scale)
{
	return (scale >= 4) ? MIP_LEVELS_5 : (scale >= 2 ? MIP_LEVELS_4 : MIP_LEVELS_3);
}

TerrainTextureClass::TerrainTextureClass(int height, int scale) :
	TextureClass(TEXTURE_WIDTH*scale, height*scale,
		WW3D_FORMAT_A8R8G8B8, scaledMipLevels(scale) )
{
}

/// Copies a tile (BGRA, bottom row first) to the physical position x, y of the 32-bit atlas.
static void copyTileScaled(UnsignedByte *bits, Int pitch, TileData *pTile, Int x, Int y, Int extent, Bool edgeAlpha)
{
	UnsignedByte *pBGRBase = pTile->getRGBDataForWidth(extent);
	for (Int j = 0; j < extent; j++) {
		UnsignedByte *pBGR = pBGRBase + (extent-1-j)*TILE_BYTES_PER_PIXEL*extent; // invert to match.
		UnsignedByte *pBGRX = bits + (y+j)*pitch + x*4;
		for (Int i = 0; i < extent; i++) {
			pBGRX[0] = pBGR[0];
			pBGRX[1] = pBGR[1];
			pBGRX[2] = pBGR[2];
			if (!edgeAlpha) {
				pBGRX[3] = 0xff;
			} else if (pBGR[0]==0 && pBGR[1]==0 && pBGR[2]==0) {		// like AlphaEdgeTextureClass::update
				pBGRX[3] = 0x80;
			} else if (pBGR[0]==0xff && pBGR[1]==0xff && pBGR[2]==0xff) {
				pBGRX[3] = 0x00;
			} else {
				pBGRX[3] = 0xff;
			}
			pBGRX += 4;
			pBGR += TILE_BYTES_PER_PIXEL;
		}
	}
}

int TerrainTextureClass::updateScaled(WorldHeightMap *htMap, int scale)
{
	WW3DSurfaceHandle surface_level;
	WW3DSurfaceDesc surface_desc;
	WW3DLockedRect locked_rect;
	Device_ErrorCode(RenderDevice::Get_Texture_Surface_Level(Peek_D3D_Texture(), 0, &surface_level));
	Device_ErrorCode(RenderDevice::Get_Surface_Desc(surface_level, &surface_desc));
	if (surface_desc.Width < (UnsignedInt)(TEXTURE_WIDTH*scale) || surface_desc.Format != WW3D_FMT_A8R8G8B8) {
		RenderDevice::Release_Resource(surface_level);
		return 0;
	}
	Device_ErrorCode(RenderDevice::Lock_Surface(surface_level, &locked_rect, nullptr, 0));

	UnsignedByte *bits = (UnsignedByte*)locked_rect.pBits;
	const Int pitch = locked_rect.Pitch;
	const Int extent = TILE_PIXEL_EXTENT*scale;
	const Int pixelBytes = 4;
	::memset(bits, 0, static_cast<size_t>(pitch)*surface_desc.Height);	// gaps between the classes, as in update
	for (Int tileNdx=0; tileNdx < htMap->m_numBitmapTiles; tileNdx++) {
		TileData *pTile = htMap->getSourceTile(tileNdx);
		if (!pTile) continue;
		ICoord2D position = pTile->m_tileLocationInTexture;
		if (position.x<=0) continue; // all real tile offsets start at 2.  jba.
		if (pTile->getExtent() != extent) continue;	// tile of another scale -- must not happen
		copyTileScaled(bits, pitch, pTile, position.x*scale, position.y*scale, extent, false);
	}
	// Border around each texture class: 4 logical = 4*scale physical pixels, as in update.
	const Int border = 4*scale;
	for (Int texClass=0; texClass<htMap->m_numTextureClasses; texClass++) {
		Int width = htMap->m_textureClasses[texClass].width;
		ICoord2D origin = htMap->m_textureClasses[texClass].positionInTexture;
		if (origin.x<=0) continue;
		width *= extent;
		origin.x *= scale;
		origin.y *= scale;
		for (Int j=0; j<width; j++) {
			UnsignedByte *pBGRX = bits + (origin.y+j)*pitch + origin.x*pixelBytes;
			memcpy(pBGRX-border*pixelBytes, pBGRX+(width-border)*pixelBytes, border*pixelBytes);	// copy before
			memcpy(pBGRX+(width*pixelBytes), pBGRX, border*pixelBytes);						// copy after
		}
		for (Int j=0; j<border; j++) {
			UnsignedByte *target = bits + (origin.y-j-1)*pitch + (origin.x-border)*pixelBytes;
			memcpy(target, target+width*pitch, (width+2*border)*pixelBytes);
			target = bits + (origin.y+j)*pitch + (origin.x-border)*pixelBytes;
			memcpy(target+width*pitch, target, (width+2*border)*pixelBytes);
		}
	}
	RenderDevice::Unlock_Surface(surface_level);
	RenderDevice::Release_Resource(surface_level);
	Device_ErrorCode(TexUtil::Generate_Mip_Levels(Peek_D3D_Texture(), TexUtil::FILTER_BOX));
	if (TheWritableGlobalData->m_textureReductionFactor) {
		RenderDevice::Set_Texture_LOD(Peek_D3D_Texture(), TheWritableGlobalData->m_textureReductionFactor);
	}
	return(surface_desc.Height/scale);
}

// Relief atlas: the same layout as the colour atlas (updateScaled), filled from WorldHeightMap::m_reliefTiles. Bytes
// B G R A = gloss, normal y, normal x, strength of the light baked into the colour texture; 128 is a flat normal, so
// everything without relief is (0, 128, 128, 0).
int TerrainTextureClass::updateRelief(WorldHeightMap *htMap, int scale)
{
	static const UnsignedByte flat[4] = { 0, 128, 128, 0 };
	return updateTiles(htMap, htMap->m_reliefTiles, scale, flat);
}

// Height atlas of the terrain tessellation: the same layout, filled from WorldHeightMap::m_heightTiles (R = height of
// the ground scan, 128 = mean of the texture); everything without heights is 128.
int TerrainTextureClass::updateHeights(WorldHeightMap *htMap, int scale)
{
	static const UnsignedByte flat[4] = { 0, 0, 128, 255 };
	return updateTiles(htMap, htMap->m_heightTiles, scale, flat);
}

int TerrainTextureClass::updateTiles(WorldHeightMap *htMap, TileData **tiles, int scale, const UnsignedByte flat[4])
{
	WW3DSurfaceHandle surface_level;
	WW3DSurfaceDesc surface_desc;
	WW3DLockedRect locked_rect;
	Device_ErrorCode(RenderDevice::Get_Texture_Surface_Level(Peek_D3D_Texture(), 0, &surface_level));
	Device_ErrorCode(RenderDevice::Get_Surface_Desc(surface_level, &surface_desc));
	if (surface_desc.Width < (UnsignedInt)(TEXTURE_WIDTH*scale) || surface_desc.Format != WW3D_FMT_A8R8G8B8) {
		RenderDevice::Release_Resource(surface_level);
		return 0;
	}
	Device_ErrorCode(RenderDevice::Lock_Surface(surface_level, &locked_rect, nullptr, 0));

	UnsignedByte *bits = (UnsignedByte*)locked_rect.pBits;
	const Int pitch = locked_rect.Pitch;
	for (UnsignedInt y = 0; y < surface_desc.Height; y++) {
		UnsignedByte *p = bits + y*pitch;
		for (UnsignedInt x = 0; x < surface_desc.Width; x++, p += 4) {
			p[0] = flat[0];
			p[1] = flat[1];
			p[2] = flat[2];
			p[3] = flat[3];
		}
	}
	const Int extent = TILE_PIXEL_EXTENT*scale;
	const Int pixelBytes = 4;
	for (Int tileNdx=0; tileNdx < htMap->m_numBitmapTiles && tileNdx < NUM_SOURCE_TILES; tileNdx++) {
		TileData *pTile = tiles[tileNdx];
		if (!pTile) continue;
		ICoord2D position = pTile->m_tileLocationInTexture;
		if (position.x<=0) continue;
		if (pTile->getExtent() != extent) continue;
		// all four channels carry data here (copyTileScaled sets alpha opaque)
		UnsignedByte *source = pTile->getRGBDataForWidth(extent);
		for (Int j = 0; j < extent; j++) {
			memcpy(bits + (position.y*scale + j)*pitch + position.x*scale*pixelBytes,
				source + (extent-1-j)*TILE_BYTES_PER_PIXEL*extent, extent*pixelBytes);
		}
	}
	// Border around each texture class as in updateScaled; classes without tiles copy their flat area.
	const Int border = 4*scale;
	for (Int texClass=0; texClass<htMap->m_numTextureClasses; texClass++) {
		Int width = htMap->m_textureClasses[texClass].width;
		ICoord2D origin = htMap->m_textureClasses[texClass].positionInTexture;
		if (origin.x<=0) continue;
		width *= extent;
		origin.x *= scale;
		origin.y *= scale;
		for (Int j=0; j<width; j++) {
			UnsignedByte *pBGRX = bits + (origin.y+j)*pitch + origin.x*pixelBytes;
			memcpy(pBGRX-border*pixelBytes, pBGRX+(width-border)*pixelBytes, border*pixelBytes);
			memcpy(pBGRX+(width*pixelBytes), pBGRX, border*pixelBytes);
		}
		for (Int j=0; j<border; j++) {
			UnsignedByte *target = bits + (origin.y-j-1)*pitch + (origin.x-border)*pixelBytes;
			memcpy(target, target+width*pitch, (width+2*border)*pixelBytes);
			target = bits + (origin.y+j)*pitch + (origin.x-border)*pixelBytes;
			memcpy(target+width*pitch, target, (width+2*border)*pixelBytes);
		}
	}
	RenderDevice::Unlock_Surface(surface_level);
	RenderDevice::Release_Resource(surface_level);
	Device_ErrorCode(TexUtil::Generate_Mip_Levels(Peek_D3D_Texture(), TexUtil::FILTER_BOX));
	if (TheWritableGlobalData->m_textureReductionFactor) {
		RenderDevice::Set_Texture_LOD(Peek_D3D_Texture(), TheWritableGlobalData->m_textureReductionFactor);
	}
	return(surface_desc.Height/scale);
}

AlphaEdgeTextureClass::AlphaEdgeTextureClass( int height, int scale) :
	TextureClass(TEXTURE_WIDTH*scale, height*scale, WW3D_FORMAT_A8R8G8B8, scaledMipLevels(scale) )
{
}

int AlphaEdgeTextureClass::updateScaled(WorldHeightMap *htMap, int scale)
{
	WW3DSurfaceHandle surface_level;
	WW3DSurfaceDesc surface_desc;
	WW3DLockedRect locked_rect;
	Device_ErrorCode(RenderDevice::Get_Texture_Surface_Level(Peek_D3D_Texture(), 0, &surface_level));
	Device_ErrorCode(RenderDevice::Lock_Surface(surface_level, &locked_rect, nullptr, 0));
	Device_ErrorCode(RenderDevice::Get_Surface_Desc(surface_level, &surface_desc));

	if (surface_desc.Format == WW3D_FMT_A8R8G8B8) {
		UnsignedByte *bits = (UnsignedByte*)locked_rect.pBits;
		const Int pitch = locked_rect.Pitch;
		// background like update (pattern in logical pixels)
		for (UnsignedInt cellY = 0; cellY < surface_desc.Height; cellY++) {
			UnsignedByte *pBGR = bits + cellY*pitch;
			for (UnsignedInt cellX = 0; cellX < surface_desc.Width; cellX++, pBGR += 4) {
				pBGR[2] = (UnsignedByte)(255-cellY/(2*scale));
				pBGR[0] = (UnsignedByte)(cellX/(2*scale));
				pBGR[3] = 128;
			}
		}
		const Int extent = TILE_PIXEL_EXTENT*scale;
		for (Int tileNdx=0; tileNdx < htMap->m_numEdgeTiles; tileNdx++) {
			TileData *pTile = htMap->getEdgeTile(tileNdx);
			if (!pTile) continue;
			ICoord2D position = pTile->m_tileLocationInTexture;
			if (position.x<=0) continue; // all real edge offsets start at 4.  jba.
			if (pTile->getExtent() != extent) continue;
			copyTileScaled(bits, pitch, pTile, position.x*scale, position.y*scale, extent, true);
		}
	}
	RenderDevice::Unlock_Surface(surface_level);
	RenderDevice::Release_Resource(surface_level);
	Device_ErrorCode(TexUtil::Generate_Mip_Levels(Peek_D3D_Texture(), TexUtil::FILTER_BOX));
	return(surface_desc.Height/scale);
}


/******************************************************************************
						CloudMapTerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// CloudMapTerrainTextureClass::CloudMapTerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to load the .tga texture, and sets
up the "sliding" parameters for the clouds to slide over the terrain. */
//=============================================================================
//@todo - Allow adjustment of the cloud slide rate, and lose the hard coded "cloudmap.tga"
CloudMapTerrainTextureClass::CloudMapTerrainTextureClass(MipCountType mipLevelCount) :
	TextureClass("TSCloudMed.tga","TSCloudMed.tga", mipLevelCount )
{ 
	Get_Filter().Set_Mip_Mapping( TextureFilterClass::FILTER_TYPE_FAST );
	m_xSlidePerSecond = -0.02f;	 
	m_ySlidePerSecond =  1.50f * m_xSlidePerSecond;
	m_curTick = 0;
	m_xOffset = 0;
	m_yOffset = 0;

}

//=============================================================================
// CloudMapTerrainTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup.
The CloudMapTerrainTextureClass may be applied by itself, or with the 
LightMapTerrainTexture.  This may be applied in either single pass, 
as the first texture in the pipe with LightMapTerrainTextureClass as the 
second stage of the pape, or multipass.  We setup for stage 0, assuming that
we are the only texture, as LightMapTerrainTexture will adjust for multitexture
if it is applied to stage 1.
Also, texture is mapped using the x/y coordinates of the map, saving us 
yet another set of uv coordinates.
(standard D3D setup, but beyond the scope of W3D). */
//=============================================================================
void CloudMapTerrainTextureClass::Apply(unsigned int stage)
{


	// Do the base apply.
	TextureClass::Apply(stage);
	return;
}

//=============================================================================
// CloudMapTerrainTextureClass::restore
//=============================================================================
/** Cleans up any custom settings to the texturing pipeline that may not be 
understood by w3d. */
//=============================================================================
void CloudMapTerrainTextureClass::restore(void)
{
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
		.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
		.Alpha_Op(WW3D_TOP_DISABLE)
	);

	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Coord_Index(0).Tex_Transform(WW3D_TTFF_DISABLE));

	RenderDevice::Set_Texture_Stage(1, TextureStageDesc()
		.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
		.Alpha_Op(WW3D_TOP_DISABLE)
	);

	RenderDevice::Set_Sampler_State( 1, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Sampler_State( 1, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_WRAP);
	RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(0).Tex_Transform(WW3D_TTFF_DISABLE));
	RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,false);
	RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND,WW3D_BLEND_SRCALPHA);
	RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND,WW3D_BLEND_INVSRCALPHA);


	if (TheGlobalData && !TheGlobalData->m_multiPassTerrain)
	{
		///@todo: Remove 8-Stage Nvidia hack after drivers are fixed.
		//This method is a backdoor specific to Nvidia based cards.  It will fail on
		//other hardware.  Allows single pass blend of 2 textures and post modulate diffuse.
		Int i;
		for (i=0; i<8; i++) {
			RenderDevice::Set_Texture_Stage(i, TextureStageDesc()
				.Color(WW3D_TOP_DISABLE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
				.Alpha(WW3D_TOP_DISABLE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
				.Tex_Coord_Index(i)
			);

			RenderDevice::Set_Device_Texture(i, nullptr);
		}
	}
}

/******************************************************************************
						ScorchTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// ScorchTextureClass::ScorchTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to load the .tga texture. */
//=============================================================================
/// @todo - get "EXScorch01.tga" from not hard coded location.
ScorchTextureClass::ScorchTextureClass(MipCountType mipLevelCount) :
	TextureClass("EXScorch01.tga","EXScorch01.tga", mipLevelCount )
// Hack to disable texture reduction.
//	TextureClass("EXScorch01.tga","EXScorch01.tga", mipLevelCount,WW3D_FORMAT_UNKNOWN,true,false)
{ 
}

//=============================================================================
// ScorchTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup.
The ScorchTextureClass is applied by iteself, as it's mesh is a subset of the 
terrain mesh.
(standard D3D setup, but beyond the scope of W3D). */
//=============================================================================
void ScorchTextureClass::Apply(unsigned int stage)
{
	// Do the base apply.
	TextureClass::Apply(stage);
	// Setup bilinear or trilinear filtering as specified in global data.
	if (TheGlobalData && TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex) {
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MINFILTER, WW3D_TEXF_LINEAR);
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MAGFILTER, WW3D_TEXF_LINEAR);
	} else {
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MINFILTER, WW3D_TEXF_POINT);
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MAGFILTER, WW3D_TEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MIPFILTER, WW3D_TEXF_LINEAR);
	} else {
		RenderDevice::Set_Sampler_State(stage, WW3D_SAMP_MIPFILTER, WW3D_TEXF_POINT);
	}

	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Transform(WW3D_TTFF_DISABLE));
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSU, WW3D_TADDRESS_CLAMP);
	RenderDevice::Set_Sampler_State( 0, WW3D_SAMP_ADDRESSV, WW3D_TADDRESS_CLAMP);
	// Now setup the texture pipeline.

	RenderDevice::Set_Texture_Stage(0, TextureStageDesc()
		.Color(WW3D_TOP_MODULATE, WW3D_TA_TEXTURE, WW3D_TA_DIFFUSE)
		.Alpha_Op(WW3D_TOP_SELECTARG1)
		.Tex_Coord_Index(0)
	);
	RenderDevice::Set_Render_State(WW3D_RS_ALPHABLENDENABLE,true);
	RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND,WW3D_BLEND_SRCALPHA);
	RenderDevice::Set_Render_State(WW3D_RS_DESTBLEND,WW3D_BLEND_INVSRCALPHA);

	RenderDevice::Disable_Texture_Stage(1);
}


