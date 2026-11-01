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

// FILE: W3DTreeBuffer.cpp ////////////////////////////////////////////////
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
// File name: W3DTreeBuffer.cpp
//
// Created:   John Ahlquist, May 2001
//
// Desc:      Draw buffer to handle all the trees in a scene.
//
//-----------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
/** Topple options */
// ------------------------------------------------------------------------------------------------
enum
{
	W3D_TOPPLE_OPTIONS_NONE			 = 0x00000000,
	W3D_TOPPLE_OPTIONS_NO_BOUNCE = 0x00000001,  ///< do not bounce when hit the ground
	W3D_TOPPLE_OPTIONS_NO_FX		 = 0x00000002	///< do not play any FX when hit the ground
};
//-----------------------------------------------------------------------------
//         Includes                                                      
//-----------------------------------------------------------------------------
#include "W3DDevice/GameClient/W3DTreeBuffer.h"
#include "formatinto.h"
#include "Common/Xfer.h"

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <assetmgr.h>
#include <texture.h>
#include "Common/MapReaderWriterInfo.h"
#include "Common/FileSystem.h" 
#include "Common/file.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameClient/ClientRandomValue.h"
#include "GameClient/FXList.h"
#include "Common/GameEngine.h"	// GameEngine_RenderTimeMs
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/Module/W3DTreeDraw.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DProjectedShadow.h"
#include "WW3D2/Scene/camera.h"
#include "WW3D2/Common/renderdevice.h"
#include "WW3D2/Common/renderpieces.h"
#include "WW3D2/Mesh/meshrenderer.h"
#include "WW3D2/Material/matinfo.h"
#include "WW3D2/Mesh/mesh.h"
#include "WW3D2/Mesh/meshmdl.h"
#include "WW3D2/Material/texutil.h"	// texture helpers based on DirectXTex
#include "WW3D2/System/wwd3dmath.h"	// replacement for the D3DX math helpers
#include "formatinto.h"

// If TEST_AND_BLEND is defined, it will do an alpha test and blend.  Otherwise just alpha test. jba. [5/30/2003]
#define dontTEST_AND_BLEND 1

#define USE_STATIC 1

constexpr auto END_OF_PARTITION = -1;

constexpr auto DELETED_TREE_TYPE = -2;

/******************************************************************************
						W3DTreeTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// W3DTreeBuffer::W3DTreeTextureClass::W3DTreeTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to create a 16 bit per pixel D3D 
texture of the desired height and mip level. */
//=============================================================================
W3DTreeBuffer::W3DTreeTextureClass::W3DTreeTextureClass(unsigned width, unsigned height) :
	TextureClass(width, height, 
		WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_ALL )
{
}

//=============================================================================
// W3DTreeBuffer::W3DTreeTextureClass::update
//=============================================================================
/** Sets the tile bitmap data into the texture.  The tiles are placed with 4
	pixel borders around them, so that when the tiles are scaled and bilinearly
	interpolated, you don't get seams between the tiles.  */
//=============================================================================
int W3DTreeBuffer::W3DTreeTextureClass::update(W3DTreeBuffer *buffer)
{

	//Set to clamp.
	Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);

	WW3DSurfaceHandle surface_level;
	WW3DSurfaceDesc surface_desc;
	WW3DLockedRect locked_rect;
	Device_ErrorCode(RenderDevice::Get_Texture_Surface_Level(Peek_D3D_Texture(), 0, &surface_level));
	Device_ErrorCode(RenderDevice::Get_Surface_Desc(surface_level, &surface_desc));

	Device_ErrorCode(RenderDevice::Lock_Surface(surface_level, &locked_rect, nullptr, 0));

	// Tiles have scale s (buffer->m_tileScale): the atlas is physically s times larger, logical positions
	// (m_tileLocationInTexture) are multiplied by s, the return value stays logical.
	const Int tileScale = buffer->m_tileScale;
	Int tilePixelExtent = TILE_PIXEL_EXTENT*tileScale;
//	Int numRows = surface_desc.Height/(tilePixelExtent+TILE_OFFSET);
#ifdef _DEBUG
	//DASSERT_MSG(tilesPerRow*numRows >= htMap->m_numBitmapTiles,Debug::Format ("Too many tiles.")); 
	//DEBUG_ASSERTCRASH((Int)surface_desc.Width >= tilePixelExtent*tilesPerRow, ("Bitmap too small."));
#endif
	if (surface_desc.Format == WW3D_FMT_A8R8G8B8) {
		Int tileNdx;
		Int pixelBytes = 4;
		// The lock returns memory whose content is undefined, and only the tiles are written below. Bilinear sampling at
		// tile edges and the smaller mip levels read the free cells next to them, and the whole level including the row
		// padding is uploaded, so preset all of it with transparent black: the atlas has the same content on every load.
		::memset(locked_rect.pBits, 0, static_cast<size_t>(locked_rect.Pitch)*surface_desc.Height);
		for (tileNdx=0; tileNdx < buffer->getNumTiles(); tileNdx++) {
			TileData *pTile = buffer->getSourceTile(tileNdx);
			if (!pTile) continue;
			ICoord2D position = pTile->m_tileLocationInTexture;
			if (position.x<0) {
				continue;
			}
			if (pTile->getExtent() != tilePixelExtent) continue;	// hd-ki2
			position.x *= tileScale;
			position.y *= tileScale;
			Int i,j;
			for (j=0; j<tilePixelExtent; j++) {
				UnsignedByte *pBGR = pTile->getRGBDataForWidth(tilePixelExtent);
				pBGR += (tilePixelExtent-(1+j))*TILE_BYTES_PER_PIXEL*tilePixelExtent; // invert to match.
				Int row = position.y+j;
				UnsignedByte *pBGRA = (static_cast<UnsignedByte*>(locked_rect.pBits)) +
							(row)*locked_rect.Pitch;

				Int column = position.x;
				pBGRA += column*pixelBytes;
				for (i=0; i<tilePixelExtent; i++) {
					// 15 bit color *((Short*)pBGRA) = 0x8000 + ((pBGR[2]>>3)<<10) + ((pBGR[1]>>3)<<5) + (pBGR[0]>>3);
					*(reinterpret_cast<Int *>(pBGRA)) = (pBGR[3]<<24) + (pBGR[2]<<16) + (pBGR[1]<<8) + (pBGR[0]); 
					pBGRA +=pixelBytes;
					pBGR +=TILE_BYTES_PER_PIXEL;
				}
			}
		}

	}
	Device_ErrorCode(RenderDevice::Unlock_Surface(surface_level));
	RenderDevice::Release_Resource(surface_level);
	Device_ErrorCode(TexUtil::Generate_Mip_Levels(Peek_D3D_Texture(), TexUtil::FILTER_BOX));
	if (TheWritableGlobalData->m_textureReductionFactor) {
		RenderDevice::Set_Texture_LOD(Peek_D3D_Texture(), static_cast<DWORD>(TheWritableGlobalData->m_textureReductionFactor));
	}
	return(surface_desc.Height/tileScale);	// hd-ki2: logische Hoehe
}


//=============================================================================
// W3DTreeBuffer::W3DTreeTextureClass::setLOD
//=============================================================================
/** Sets the lod of the texture to be loaded into the video card.  */
//=============================================================================
void W3DTreeBuffer::W3DTreeTextureClass::setLOD(Int LOD) const
{
	if (Peek_D3D_Texture()) {
		RenderDevice::Set_Texture_LOD(Peek_D3D_Texture(), static_cast<DWORD>(LOD));
	}
}
//=============================================================================
// W3DTreeBuffer::W3DTreeTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup
(standard D3D setup, but beyond the scope of W3D).  */
//=============================================================================
void W3DTreeBuffer::W3DTreeTextureClass::Apply(unsigned int stage)
{
	// Do the base apply.
	TextureClass::Apply(stage);
}
//-----------------------------------------------------------------------------
//         Private Data                                                     
//-----------------------------------------------------------------------------

#ifdef TEST_AND_BLEND
// A W3D shader that does alpha, texturing, tests zbuffer, doesn't update zbuffer.
#define SC_ALPHA_DETAIL ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

#define SC_ALPHA_DETAIL_2X ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE2X, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

#else
#define SC_ALPHA_DETAIL ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

#define SC_ALPHA_DETAIL_2X ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE2X, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )
#endif
static ShaderClass detailAlphaShader(SC_ALPHA_DETAIL);
static ShaderClass detailAlphaShader2X(SC_ALPHA_DETAIL_2X);


/*
#define SC_ALPHA_DETAIL ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailAlphaShader(SC_ALPHA_DETAIL);
*/

/*
#define SC_ALPHA_DETAIL ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailAlphaShader(SC_ALPHA_DETAIL);
*/

/*
#define SC_ALPHA_MIRROR ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass mirrorAlphaShader(SC_ALPHA_DETAIL);

// ShaderClass::PASS_ALWAYS, 

#define SC_ALPHA_2D ( SHADE_CNST(PASS_ALWAYS, DEPTH_WRITE_DISABLE, COLOR_WRITE_ENABLE, \
	SRCBLEND_SRC_ALPHA, DSTBLEND_ONE_MINUS_SRC_ALPHA, FOG_DISABLE, GRADIENT_DISABLE, \
	SECONDARY_GRADIENT_DISABLE, TEXTURING_ENABLE, DETAILCOLOR_DISABLE, DETAILALPHA_DISABLE, \
	ALPHATEST_DISABLE, CULL_MODE_ENABLE, DETAILCOLOR_DISABLE, DETAILALPHA_DISABLE) )
ShaderClass ShaderClass::_PresetAlpha2DShader(SC_ALPHA_2D);
*/
//-----------------------------------------------------------------------------
//         Private Functions                                               
//-----------------------------------------------------------------------------

//=============================================================================
// W3DTreeBuffer::cull
//=============================================================================
/** Culls the trees, marking the visible flag.  If a tree becomes visible, it sets
it's sortKey */
//=============================================================================
void W3DTreeBuffer::cull(const CameraClass * camera)
{
	Int curTree;

	// Calulate the vector direction that the camera is looking at.
	Matrix3D camera_matrix = camera->Get_Transform();
	float zmod = -1;
	float x = zmod * camera_matrix[0][2] ;
	float y = zmod * camera_matrix[1][2] ;
	float z = zmod * camera_matrix[2][2] ;
	m_cameraLookAtVector.Set(x,y,z);

	for (curTree=0; curTree<m_numTrees; curTree++) {
		Bool doKey = false;	// We calculate the key when a tree becomes visible.
		Bool visible = !camera->Cull_Sphere(m_trees[curTree].bounds);
		if (visible != m_trees[curTree].visible) {
			m_trees[curTree].visible=visible;
			m_anythingChanged = true;
			if (visible) {
				doKey = true;
			}
		}
		// Also calculate sort key if a tree is visible, and the view changed setting m_updateAllKeys to true.
		if (doKey || (visible&&m_updateAllKeys)) {
			// The sort key is essentially the distance of location in the direction of the 
			// camera look at.
			m_trees[curTree].sortKey = Vector3::Dot_Product(m_trees[curTree].location, m_cameraLookAtVector); 
		}
	}
	m_updateAllKeys = false;
}
//=============================================================================
// W3DTreeBuffer::getPartitionBucket
//=============================================================================
/** Returns the bucket index into m_areaPartition for a given location. */
//=============================================================================
Int W3DTreeBuffer::getPartitionBucket(const Coord3D &pos) const
{
	Real x = pos.x;
	Real y = pos.y;
	if (x<m_bounds.lo.x) x = m_bounds.lo.x;
	if (y<m_bounds.lo.y) y = m_bounds.lo.y;
	if (x>m_bounds.hi.x) x = m_bounds.hi.x;
	if (y>m_bounds.hi.y) y = m_bounds.hi.y;
	Int xIndex = REAL_TO_INT_FLOOR ( (x/(m_bounds.hi.x-m_bounds.lo.x)) * (static_cast<Real>(PARTITION_WIDTH_HEIGHT)-0.1f) );
	Int yIndex = REAL_TO_INT_FLOOR ( (y/(m_bounds.hi.y-m_bounds.lo.y)) * (static_cast<Real>(PARTITION_WIDTH_HEIGHT)-0.1f) );
	DEBUG_ASSERTCRASH(xIndex>=0 && yIndex>=0 && xIndex<PARTITION_WIDTH_HEIGHT && yIndex<PARTITION_WIDTH_HEIGHT, ("Invalid range."));
	return yIndex*PARTITION_WIDTH_HEIGHT + xIndex;
}

//=============================================================================
// W3DTreeBuffer::cull
//=============================================================================
/** Culls the trees, marking the visible flag.  If a tree becomes visible, it sets
it's sortKey */
//=============================================================================
void W3DTreeBuffer::updateSway(const BreezeInfo& info)
{
	Int i;
	for	(i=0; i<NUM_SWAY_ENTRIES; i++) {
		Real factor = Cos(i*2.0f*PI/(static_cast<Real>(NUM_SWAY_ENTRIES)+1.0f));
		Real angle = info.m_lean + (info.m_intensity  * factor);
		Real S = Sin(angle);
		Real C = Cos(angle);
		m_swayOffsets[i].X = info.m_directionVec.x * S;
		m_swayOffsets[i].Y = info.m_directionVec.y * S;
		m_swayOffsets[i].Z = C - 1.0f;
	}
	
	Real delta				= info.m_randomness * 0.5f;
	for	(i=0; i<m_numTrees; i++) {
		m_trees[i].swayType = 1+GameClientRandomValue(0, MAX_SWAY_TYPES-1);
	}
	for (i=0; i<MAX_SWAY_TYPES; i++) {
		m_curSwayStep[i] = static_cast<Real>(NUM_SWAY_ENTRIES) / static_cast<Real>(info.m_breezePeriod);
		m_curSwayStep[i]	*= GameClientRandomValueReal(1.0f-delta, 1.0f+delta);
		if (m_curSwayStep[i]<0.0f) {
			m_curSwayStep[i] = 0.0f;
		}
		m_curSwayOffset[i] = 0;
		m_curSwayFactor[i] = GameClientRandomValueReal(1.0f-delta, 1.0f+delta);
	}	
	m_curSwayVersion = info.m_breezeVersion;
}


/********** GDIFileStream2 class ****************************/
class GDIFileStream2 : public InputStream
{
protected:
	File* m_file;
public:
	GDIFileStream2():m_file(nullptr) {};
	GDIFileStream2(File* pFile):m_file(pFile) {};
	virtual Int read(void *pData, Int numBytes) override {
		return(m_file?m_file->read(pData, numBytes):0);
	};
};

//=============================================================================
// W3DTreeBuffer::updateTexture
//=============================================================================
/** Creates a new texture. */
//=============================================================================
void W3DTreeBuffer::updateTexture(void)
{
	
	const Int MAX_TEX_WIDTH = 2048;

	Int i, j;
	Int maxHeight = 0;
	const Int maxTilesPerRow = MAX_TEX_WIDTH/(TILE_PIXEL_EXTENT);

	REF_PTR_RELEASE(m_treeTexture);

	Bool availableGrid[maxTilesPerRow][maxTilesPerRow];
	Int row, column;
	for (row=0; row<maxTilesPerRow; row++) {
		for (column=0; column<maxTilesPerRow; column++) {
			availableGrid[row][column] = true;
		}
	}

	for (i=0; i<m_numTiles; i++) {
		REF_PTR_RELEASE (m_sourceTiles[i]);
	}
	m_numTiles = 0;
	File *theFile = nullptr;
	for (i=0; i<m_numTreeTypes; i++) {
		char texturePath[ _MAX_PATH ];
		m_treeTypes[i].m_numTiles = 0;
		if (loadOwnTexture(i)) {
			m_treeTypes[i].m_firstTile = 0;
			m_treeTypes[i].m_tileWidth = 0;
			m_treeTypes[i].m_halfTile = false;
			continue;
		}
		WWFormat::Into(texturePath, "{}{}", TERRAIN_TGA_DIR_PATH, m_treeTypes[i].m_data->m_textureName.str());
		theFile = TheFileSystem->openFile( texturePath, File::READ|File::BINARY);
		if (theFile==nullptr) {
			WWFormat::Into(texturePath, "{}{}", TGA_DIR_PATH, m_treeTypes[i].m_data->m_textureName.str());
			theFile = TheFileSystem->openFile( texturePath, File::READ|File::BINARY);
		}
		if (theFile != nullptr) {
			GDIFileStream2 theStream(theFile);
			InputStream *pStr = &theStream;
			Bool halfTile;
			WorldHeightMap::setTilePackageName(texturePath);
			Int numTiles = WorldHeightMap::countTiles(pStr, &halfTile);
			Int width;
			for (width = 10; width >= 1; width--) {
				if (numTiles >= width*width) {
					numTiles = width*width;
					break;
				}
			}
			Bool texFound = false;
			for	(j=0; j<i; j++) {
				if (m_treeTypes[j].m_data->m_textureName.compareNoCase(m_treeTypes[i].m_data->m_textureName)==0) {					
					m_treeTypes[i].m_firstTile = 0;
					m_treeTypes[i].m_tileWidth = width;
					m_treeTypes[i].m_numTiles = 0;
					texFound = true;
					break;
				}
			}
			if (texFound) {
				theFile->close();
				continue;
			}
			if (m_numTiles+numTiles<=MAX_TILES) {
				theFile->seek(0, File::seekMode::START);
				m_treeTypes[i].m_firstTile = m_numTiles;
				m_treeTypes[i].m_tileWidth = width;
				m_treeTypes[i].m_numTiles = numTiles;
				m_treeTypes[i].m_halfTile = halfTile;
				WorldHeightMap::readTiles(pStr, m_sourceTiles+m_treeTypes[i].m_firstTile, width);	
				m_numTiles += numTiles;
			} else {
				m_treeTypes[i].m_firstTile = 0;
				m_treeTypes[i].m_tileWidth = 0;
				m_treeTypes[i].m_numTiles = 0;
			}
			theFile->close();
		} else {
			DEBUG_CRASH(("Could not find texture %s\n", m_treeTypes[i].m_data->m_textureName.str()));
			m_treeTypes[i].m_firstTile = 0;
			m_treeTypes[i].m_tileWidth = 0;
			m_treeTypes[i].m_numTiles = 0;
		}
	}

	WorldHeightMap::setTilePackageName(nullptr);

	// Draw groups: the atlas, then one per distinct own texture (kinds may share one by texture name).
	m_groupTextures[0] = nullptr;
	m_numTextureGroups = 1;
	for (i=0; i<m_numTreeTypes; i++) {
		m_treeTypes[i].m_drawGroup = 0;
		if (m_treeTypes[i].m_ownTexture == nullptr) {
			continue;
		}
		Int group;
		for (group=1; group<m_numTextureGroups; group++) {
			if (m_groupTextures[group] == m_treeTypes[i].m_ownTexture) {
				break;
			}
		}
		if (group == m_numTextureGroups) {
			m_groupTextures[group] = m_treeTypes[i].m_ownTexture;
			m_numTextureGroups++;
		}
		m_treeTypes[i].m_drawGroup = group;
	}
	// UVs and groups of the buffered trees depend on the layout.
	m_anythingChanged = true;

	// Tree tiles may be 64*s pixels depending on the texture quality; bring all to the largest size. The atlas
	// becomes physically s times larger while the layout stays logical.
	m_tileScale = TileData::unifyExtent(m_sourceTiles, m_numTiles, false);

	Int tmpWidth = 8;
	while (tmpWidth*tmpWidth<m_numTiles) {
		tmpWidth*=2;
	}
	Int tilesPerRow = tmpWidth;
	m_textureWidth = tmpWidth*TILE_PIXEL_EXTENT;
	
	if (m_textureWidth>MAX_TEX_WIDTH) {
		m_textureWidth = 64;
		m_textureHeight = 64;
		if (m_treeTexture==nullptr) {
			m_treeTexture = new TextureClass("missing.tga");
		}
		DEBUG_CRASH(("Too many trees in a scene."));
		return;
	}

	for (i=0; i<m_numTiles; i++) {
		if (m_sourceTiles[i]) {
			m_sourceTiles[i]->m_tileLocationInTexture.x = -1;
			m_sourceTiles[i]->m_tileLocationInTexture.y = -1;
		}
	}	 

	/* put the tree tiles into the texture */
	Int texClass;
	Int tileWidth;
	for (tileWidth = tilesPerRow; tileWidth>0; tileWidth--) {
		for (texClass=0; texClass<m_numTreeTypes; texClass++) {
			Int width = m_treeTypes[texClass].m_tileWidth;
			if (width != tileWidth) continue;
			Bool texFound = false;
			for	(i=0; i<texClass; i++) {
				if (m_treeTypes[i].m_data->m_textureName.compareNoCase(m_treeTypes[texClass].m_data->m_textureName)==0) {					
					m_treeTypes[texClass].m_textureOrigin.x = m_treeTypes[i].m_textureOrigin.x;
					m_treeTypes[texClass].m_textureOrigin.y = m_treeTypes[i].m_textureOrigin.y;
					texFound = true;
					break;
				}
			}
			if (texFound) {
				continue;
			}

			// Find an available block of space.
			Bool found = false;
			for (row=0; row<(tilesPerRow-width)+1 && !found; row++) {
				for (column=0; column<(tilesPerRow-width)+1 && !found; column++) {
					if (availableGrid[row][column]) {
						Bool open = true;
						for (i=0; i<width && open; i++) {
							for (j=0; j<width&&open; j++) {
								if (!availableGrid[row+j][column+i]) {
									open = false;
								}
							}
						}
						if (open) found = true;
						break;
					}
				}
				if (found) break;
			}
			if (!found) {
				m_treeTypes[texClass].m_textureOrigin.x = 0;
				m_treeTypes[texClass].m_textureOrigin.y = 0;
				continue;
			}

			Int xOrigin = column*(TILE_PIXEL_EXTENT);
			Int yOrigin = row*(TILE_PIXEL_EXTENT);
			m_treeTypes[texClass].m_textureOrigin.x = xOrigin;
			m_treeTypes[texClass].m_textureOrigin.y = yOrigin;
			Int classHeight = yOrigin + width*TILE_PIXEL_EXTENT;
			if (maxHeight < classHeight) maxHeight = classHeight;

			for (i=0; i<width; i++) {
				for (j=0; j<width; j++) {
					availableGrid[row+j][column+i] = false;
					Int baseNdx = m_treeTypes[texClass].m_firstTile + i + j*width;
					Int x = xOrigin + i*TILE_PIXEL_EXTENT;
					Int y = yOrigin + ((width-j)-1)*TILE_PIXEL_EXTENT;
					m_sourceTiles[baseNdx]->m_tileLocationInTexture.x = x;
					m_sourceTiles[baseNdx]->m_tileLocationInTexture.y = y;
				}
			}
		}
	}
	DEBUG_ASSERTCRASH(maxHeight<=m_textureWidth, ("Bad max height."));
	// physical size includes the tile scale; m_textureWidth/-Height stay logical (texture coordinates)
	const Int physWidth = m_textureWidth*m_tileScale;
	W3DTreeTextureClass *tex = new W3DTreeTextureClass(static_cast<DWORD>(physWidth), static_cast<DWORD>(physWidth));
	m_textureHeight = tex->update(this);

	m_treeTexture = tex;
	
	for (i=0; i<m_numTiles; i++) {
		REF_PTR_RELEASE (m_sourceTiles[i]);
	}
	//	m_treeTexture = NEW_REF (TextureClass, (m_treeTypes[0].m_textureName.str()));

}

//=============================================================================
// W3DTreeBuffer::loadOwnTexture
//=============================================================================
/** A tree kind may bring its own texture, <TextureName without extension>_tree.dds among the
textures. It is drawn as is with its own mip levels instead of being cut into 64 pixel atlas
tiles; kinds without one keep the atlas. */
//=============================================================================
Bool W3DTreeBuffer::loadOwnTexture(Int type)
{
	REF_PTR_RELEASE(m_treeTypes[type].m_ownTexture);
	const char *texName = m_treeTypes[type].m_data->m_textureName.str();
	const char *dot = strrchr(texName, '.');
	const std::string_view stem(texName, dot ? static_cast<size_t>(dot - texName) : strlen(texName));
	char ownName[ _MAX_PATH ];
	WWFormat::Into(ownName, "{}_tree.dds", stem);
	char path[ _MAX_PATH ];
	WWFormat::Into(path, "{}{}", TGA_DIR_PATH, ownName);
	if (!TheFileSystem->doesFileExist(path)) {
		return false;
	}
	TextureClass *tex = WW3DAssetManager::Get_Instance()->Get_Texture(ownName);
	if (tex == nullptr) {
		return false;
	}
	// The cards use the whole texture; clamp keeps the opposite edge out of the filter.
	tex->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	tex->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	m_treeTypes[type].m_ownTexture = tex;
	return true;
}

/**Adjust the resolution of tree texture uploaded to the video card.  The system memory
version always remains at full resolution.  Someone should probably optimize this at
some point since it wastes a lot of system memory on low-end systems. -MW
*/
void W3DTreeBuffer::setTextureLOD(Int lod)
{
	if (m_treeTexture)
		(static_cast<W3DTreeTextureClass*>(m_treeTexture))->setLOD(lod);
}

//=============================================================================
// W3DTreeBuffer::doLighting
//=============================================================================
/** Calculates the diffuse lighting as affected by dynamic lighting. */
//=============================================================================
UnsignedInt W3DTreeBuffer::doLighting(const Vector3 *normal,  
															const GlobalData::TerrainLighting	*objectLighting, 
															const Vector3 *emissive, UnsignedInt vertDiffuse, Real scale) const
{

	Real shadeR, shadeG, shadeB;
	Real shade;
	shadeR = objectLighting[0].ambient.red+emissive->X;	//only the first light contributes to ambient
	shadeG = objectLighting[0].ambient.green+emissive->Y;
	shadeB = objectLighting[0].ambient.blue+emissive->Z;

	Int i;
	for	(i=0; i<MAX_GLOBAL_LIGHTS; i++) {
		Vector3 lightDirection(objectLighting[i].lightPos.x, objectLighting[i].lightPos.y, objectLighting[i].lightPos.z);
		lightDirection.Normalize();
		Vector3 lightRay(-lightDirection.X, -lightDirection.Y, -lightDirection.Z);
		shade = Vector3::Dot_Product(lightRay, *normal); 

		if (shade > 1.0) shade = 1.0;
		if(shade < 0.0f) shade = 0.0f;
		shadeR += shade*objectLighting[i].diffuse.red;
		shadeG += shade*objectLighting[i].diffuse.green;
		shadeB += shade*objectLighting[i].diffuse.blue;	
	}

	shadeR *= scale;
	shadeG *= scale;
	shadeB *= scale;
	
	if (shadeR > 1.0) shadeR = 1.0;
	if(shadeR < 0.0f) shadeR = 0.0f;
	if (shadeG > 1.0) shadeG = 1.0;
	if(shadeG < 0.0f) shadeG = 0.0f;
	if (shadeB > 1.0) shadeB = 1.0;
	if(shadeB < 0.0f) shadeB = 0.0f;
	
	if (vertDiffuse!=0xFFFFFFFF) {
		shade = static_cast<Real>(vertDiffuse&0xff); //blue;
		shadeB *= shade/255.0f;
		shade = static_cast<Real>((vertDiffuse>>8)&0xFF); // green;
		shadeG *= shade/255.0f;
		shade = static_cast<Real>((vertDiffuse>>16)&0xFF); // red;
		shadeR *= shade/255.0f;
	}

	shadeR*=255.0f;
	shadeG*=255.0f;
	shadeB*=255.0f;
	const Real alpha = 255.0;
	return REAL_TO_UNSIGNEDINT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16) | (static_cast<Int>(alpha) << 24);
	
}

//=============================================================================
// W3DTreeBuffer::loadTreesInVertexAndIndexBuffers
//=============================================================================
/** Loads the trees into the vertex buffer for drawing. */
//=============================================================================
void W3DTreeBuffer::loadTreesInVertexAndIndexBuffers(RefRenderObjListIterator *pDynamicLightsIterator)
{
	if (!m_indexTree[0] || !m_vertexTree[0] || !m_initialized) {
		return;
	}
	if (!m_anythingChanged) {
		return;
	}

	if (m_shadow == nullptr && TheW3DProjectedShadowManager) {
		Shadow::ShadowTypeInfo shadowInfo;
		shadowInfo.m_ShadowName[0] = 0;
		shadowInfo.allowUpdates=FALSE;	//shadow image will never update
		shadowInfo.allowWorldAlign=TRUE;	//shadow image will wrap around world objects
		shadowInfo.m_type = static_cast<ShadowType>(SHADOW_DECAL);
		shadowInfo.m_sizeX=20;
		shadowInfo.m_sizeY=20;
		shadowInfo.m_offsetX=0;
		shadowInfo.m_offsetY=0;
		m_shadow = TheW3DProjectedShadowManager->createDecalShadow(&shadowInfo);
	}
	
	m_anythingChanged = false;
	m_numDrawGroups = 0;
	// Only trees that fit into the buffer get a slot below; the others must not be rewritten by updateVertexBuffer().
	for (Int resetTree=0; resetTree<m_numTrees; resetTree++) {
		m_trees[resetTree].bufferNdx = -1;
	}
	Int curTree=0;

	// Assign every visible tree to a buffer (first fit). When the visible trees need more than all buffers hold,
	// the ones nearest to the camera win. Drawing keeps the order of m_trees within a buffer.
	Int treeBuffer[MAX_TREES];
	Int numBuffersUsed = 0;
	{
		Int order[MAX_TREES];
		Int numCandidates = 0;
		Int totalVertices = 0;
		Int totalIndices = 0;
		for (curTree=0; curTree<m_numTrees; curTree++) {
			treeBuffer[curTree] = -1;
			const Int treeType = m_trees[curTree].treeType;
			if (treeType<0 || !m_trees[curTree].visible || m_treeTypes[treeType].m_mesh == 0) {
				continue;
			}
			order[numCandidates++] = curTree;
			totalVertices += m_treeTypes[treeType].m_mesh->Peek_Model()->Get_Vertex_Count();
			totalIndices += 3*m_treeTypes[treeType].m_mesh->Peek_Model()->Get_Polygon_Count();
		}
		Int usedVertices[MAX_BUFFERS];
		Int usedIndices[MAX_BUFFERS];
		auto pack = [&](const Int *list, Int count) -> Int {
			Int placed = 0;
			for (Int i=0; i<MAX_BUFFERS; i++) {
				usedVertices[i] = 0;
				usedIndices[i] = 0;
			}
			for (Int i=0; i<count; i++) {
				const Int tree = list[i];
				treeBuffer[tree] = -1;
				const Int treeType = m_trees[tree].treeType;
				const Int numVertex = m_treeTypes[treeType].m_mesh->Peek_Model()->Get_Vertex_Count();
				const Int numIndex = 3*m_treeTypes[treeType].m_mesh->Peek_Model()->Get_Polygon_Count();
				for (Int buffer=0; buffer<MAX_BUFFERS; buffer++) {
					if (usedVertices[buffer]+numVertex+2 >= MAX_TREE_VERTEX || usedIndices[buffer]+numIndex+6 >= MAX_TREE_INDEX) {
						continue;
					}
					usedVertices[buffer] += numVertex;
					usedIndices[buffer] += numIndex;
					treeBuffer[tree] = buffer;
					placed++;
					break;
				}
			}
			return placed;
		};
		Int numPlaced = pack(order, numCandidates);
		if (numPlaced < numCandidates) {
			std::sort(order, order+numCandidates, [this](Int a, Int b) { return m_trees[a].sortKey < m_trees[b].sortKey; });
			numPlaced = pack(order, numCandidates);
			static Int s_lastCandidates = -1;
			static Int s_lastPlaced = -1;
			if (numCandidates != s_lastCandidates || numPlaced != s_lastPlaced) {
				s_lastCandidates = numCandidates;
				s_lastPlaced = numPlaced;
				DEBUG_LOG(("Tree buffers full: %d of %d visible trees placed (%d vertices and %d indices wanted, %d buffers of %d vertices and %d indices)\n",
					numPlaced, numCandidates, totalVertices, totalIndices, static_cast<Int>(MAX_BUFFERS), static_cast<Int>(MAX_TREE_VERTEX), static_cast<Int>(MAX_TREE_INDEX)));
			}
		}
		for (Int i=0; i<MAX_BUFFERS; i++) {
			if (usedVertices[i] > 0) {
				numBuffersUsed = i+1;
			}
		}
	}
	Int bNdx;
	const GlobalData::TerrainLighting *objectLighting = TheGlobalData->m_terrainObjectsLighting[TheGlobalData->m_timeOfDay];
	for (bNdx=0; bNdx<MAX_BUFFERS; bNdx++) {
		m_curNumTreeVertices[bNdx] = 0;
		m_curNumTreeIndices[bNdx] = 0;
	}
	for (bNdx=0; bNdx<numBuffersUsed; bNdx++) {
		VertexFormatXYZNDUV1 *vb;
		UnsignedShort *ib;
		// Lock the buffers.
	#ifdef USE_STATIC
		DeviceIndexBufferClass::WriteLockClass lockIdxBuffer(m_indexTree[bNdx], 0);
		DeviceVertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexTree[bNdx], 0);
	#else 
		DeviceIndexBufferClass::WriteLockClass lockIdxBuffer(m_indexTree[bNdx], WW3D_LOCK_DISCARD);
		DeviceVertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexTree[bNdx], WW3D_LOCK_DISCARD);
	#endif
		vb=static_cast<VertexFormatXYZNDUV1*>(lockVtxBuffer.Get_Vertex_Array());
		ib = lockIdxBuffer.Get_Index_Array();
		// Add to the index buffer & vertex buffer.
		Vector2 lookAtVector(m_cameraLookAtVector.X, m_cameraLookAtVector.Y);
		lookAtVector.Normalize();
		// We draw from back to front, so we put the indexes in the buffer 
		// from back to front.
		UnsignedShort *curIb = ib;

		VertexFormatXYZNDUV1 *curVb = vb;

		// One pass per draw group; with the atlas alone this is the single pass over all trees.
		Bool bufferFull = false;
		for (Int group=0; group<m_numTextureGroups && !bufferFull; group++) {
		const Int groupFirstIndex = m_curNumTreeIndices[bNdx];
		const Int groupFirstVertex = m_curNumTreeVertices[bNdx];
		for (curTree=0 ;curTree<m_numTrees;curTree++) {
			Int type = m_trees[curTree].treeType;
			if (type<0) {
				continue; // Deleted tree. [6/9/2003]
			}
			if (m_treeTypes[type].m_drawGroup != group) {
				continue;
			}
			if (!m_trees[curTree].visible) continue;
			Real scale = m_trees[curTree].scale;
			Vector3 loc = m_trees[curTree].location;
			Real theSin = m_trees[curTree].sin;
			Real theCos = m_trees[curTree].cos;
			if (type<0 || m_treeTypes[type].m_mesh == 0) {
				continue;
			}
			if (treeBuffer[curTree] != bNdx) {
				continue;
			}

			Bool doVertexLighting = true;

			Vector3 emissive(0.0f,0.0f,0.0f);
			MaterialInfoClass * matInfo = m_treeTypes[type].m_mesh->Get_Material_Info();
			if (matInfo) {
				VertexMaterialClass *vertMat = matInfo->Peek_Vertex_Material(0);
				if (vertMat) {
					vertMat->Get_Emissive(&emissive);
				}
			}
			REF_PTR_RELEASE(matInfo);


			Int startVertex = m_curNumTreeVertices[bNdx];
			Int i;
			Int numVertex = m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Count();
			Vector3 *pVert = m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Array();



			// If we happen to have too many trees, stop.
			if (m_curNumTreeVertices[bNdx]+numVertex+2>= MAX_TREE_VERTEX) {
				bufferFull = true;
				break;
			}
			Int numIndex = m_treeTypes[type].m_mesh->Peek_Model()->Get_Polygon_Count();
			const TriIndex *pPoly = m_treeTypes[type].m_mesh->Peek_Model()->Get_Polygon_Array();
			if (m_curNumTreeIndices[bNdx]+3*numIndex+6 >= MAX_TREE_INDEX) {
				bufferFull = true;
				break;
			}
			m_trees[curTree].firstIndex = startVertex;
			m_trees[curTree].bufferNdx = bNdx;

			const Vector2*uvs=m_treeTypes[type].m_mesh->Peek_Model()->Get_UV_Array_By_Index(0);
			
			const Vector3*normals = m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Normal_Array();
			const unsigned *vecDiffuse = m_treeTypes[type].m_mesh->Peek_Model()->Get_Color_Array(0, false);

			Int diffuse = 0;
			if (normals == nullptr) {
				doVertexLighting = false;
				Vector3 normal(0.0f,0.0f,1.0f);
				diffuse = doLighting(&normal, objectLighting, &emissive, 0xFFFFFFFF, 1.0f);
			}
	/*
	 *	
			// If we are doing reduced resolution terrain, do reduced
			// poly trees.
			Bool doPanel = (TheGlobalData->m_useHalfHeightMap || TheGlobalData->m_stretchTerrain);

			if (doPanel) {
				if (m_trees[curTree].rotates) {
					theSin = -lookAtVector.X;
					theCos = lookAtVector.Y;
				}
				// panel start is index offset, there are 3 index per triangle.
				if (m_trees[curTree].panelStart/3 + 2 > numIndex) {
					continue; // not enought polygons for the offset.  jba.
				}
				for (j=0; j<6; j++) {
					i = ((Int *)pPoly)[j+m_trees[curTree].panelStart];
					if (m_curNumTreeVertices >= MAX_TREE_VERTEX) 
						break;

					// Update the uv values.  The W3D models each have their own texture, and 
					// we use one texture with all images in one, so we have to change the uvs to 
					// match.
					Real U, V;
					if (type==SHRUB) {
						// shrub texture is tucked in the corner
						U = ((512-64)+uvs[i].U*64.0f)/512.0f;
						V = ((256-64)+uvs[i].V*64.0f)/256.0f;
					} else if (type==FENCE) {
						U = uvs[i].U*0.5f; 
						V = 1.0f + uvs[i].V;		
					} else {
						U = typeOffset+uvs[i].U*0.5f; 
						V = uvs[i].V;		
					}

					curVb->u1 = U;
					curVb->v1 = V/2.0;
					Vector3 vLoc;
					vLoc.X = pVert[i].X*scale*theCos - pVert[i].Y*scale*theSin;
					vLoc.Y = pVert[i].Y*scale*theCos + pVert[i].X*scale*theSin;

					vLoc.X += loc.X;
					vLoc.Y += loc.Y;
					vLoc.Z = loc.Z + pVert[i].Z*scale; 

					curVb->x = vLoc.X;
					curVb->y = vLoc.Y;
					curVb->z = vLoc.Z;	 
					if (doVertexLighting) {
						curVb->diffuse = doLighting(&vLoc, shadeR, shadeG, shadeB, m_trees[curTree].bounds, pDynamicLightsIterator);
					} else {
						curVb->diffuse = diffuse;
					}
					curVb++;
					m_curNumTreeVertices++;
				}

				for (i=0; i<6; i++) {
					if (m_curNumTreeIndices+4 > MAX_TREE_INDEX) 
						break;
					curIb--;
					*curIb = startVertex + i;
					m_curNumTreeIndices++;
				}
			} else {
	 */
			Real Uscale = m_treeTypes[type].m_tileWidth * static_cast<Real>(TILE_PIXEL_EXTENT) / static_cast<Real>(m_textureWidth);
			Real Vscale = m_treeTypes[type].m_tileWidth * static_cast<Real>(TILE_PIXEL_EXTENT) / static_cast<Real>(m_textureHeight);
			Real UOffset = m_treeTypes[type].m_textureOrigin.x/static_cast<Real>(m_textureWidth); 
			Real VOffset = m_treeTypes[type].m_textureOrigin.y/static_cast<Real>(m_textureHeight); 
			if (m_treeTypes[type].m_ownTexture) {
				// own texture: the model UVs as they are
				Uscale = 1.0f;
				Vscale = 1.0f;
				UOffset = 0.0f;
				VOffset = 0.0f;
			} else if (m_treeTypes[type].m_halfTile) {
				Uscale *= 0.5f;
				Vscale *= 0.5f;
				VOffset += (TILE_PIXEL_EXTENT/2) / static_cast<Real>(m_textureHeight);
			}
			for (i=0; i<numVertex; i++) {
				if (m_curNumTreeVertices[bNdx] >= MAX_TREE_VERTEX) 
					break;

				// Update the uv values.  The W3D models each have their own texture, and 
				// we use one texture with all images in one, so we have to change the uvs to 
				// match.
				Real U, V;
				U = uvs[i].U; 
				V = uvs[i].V;	
				
				if (U>1.0f) U=1.0f;
				if (U<0.0f) U=0.0f;
				if (V>1.0f) V=1.0f;
				if (V<0.0f) V=0.0f;

				curVb->u1 = U*Uscale + UOffset;
				curVb->v1 = V*Vscale + VOffset;
				Real x = pVert[i].X;
				Real y = pVert[i].Y;

				
				Vector3 vLoc;
				x += m_treeTypes[type].m_offset.X;
				y += m_treeTypes[type].m_offset.Y;
				vLoc.X = x*scale*theCos - y*scale*theSin;
				vLoc.Y = y*scale*theCos + x*scale*theSin;
				vLoc.Z = pVert[i].Z*scale; 
				vLoc.Z += m_treeTypes[type].m_offset.Z;

				if (m_trees[curTree].m_toppleState != TOPPLE_UPRIGHT) {
					Matrix3D::Transform_Vector(m_trees[curTree].m_mtx, vLoc, &vLoc);
				} else {
					if (m_trees[curTree].pushAside>0.0f) {
						vLoc.X += pVert[i].Z * m_trees[curTree].pushAside * m_trees[curTree].pushAsideCos * m_treeTypes[type].m_data->m_maxOutwardMovement;
						vLoc.Y += pVert[i].Z * m_trees[curTree].pushAside * m_trees[curTree].pushAsideSin* m_treeTypes[type].m_data->m_maxOutwardMovement;
					}
					vLoc.X += loc.X;
					vLoc.Y += loc.Y;
					vLoc.Z += loc.Z;
				}


				curVb->x = vLoc.X;
				curVb->y = vLoc.Y;
				curVb->z = vLoc.Z;
				curVb->nx = static_cast<float>(m_trees[curTree].swayType);
				curVb->ny = 1.0f - m_treeTypes[type].m_data->m_darkening*m_trees[curTree].pushAside;
				curVb->nz = loc.Z;
				if (doVertexLighting) {
					Vector3 normal(0.0f, 0.0f, 1.0f);
					if (normals) {
						normal.X = normals[i].X*theCos - normals[i].Y*theSin;
						normal.Y = normals[i].Y*theCos + normals[i].X*theSin;
						normal.Z = normals[i].Z;
					}
					UnsignedInt vertexDiffuse;
					if (vecDiffuse) {
						vertexDiffuse = vecDiffuse[i];
					} else {
						vertexDiffuse = 0xffffffff;
					}
					curVb->diffuse = doLighting(&normal, objectLighting, &emissive, 
														vertexDiffuse, 1.0f);
				} else {
					curVb->diffuse = diffuse;
				}
				curVb++;
				m_curNumTreeVertices[bNdx]++;
			}

			try {
			for (i=0; i<numIndex; i++) {
				if (m_curNumTreeIndices[bNdx]+4 > MAX_TREE_INDEX) 
					break;
				*curIb++ = static_cast<UnsignedShort>(startVertex + pPoly[i].I);
				*curIb++ = static_cast<UnsignedShort>(startVertex + pPoly[i].J);
				*curIb++ = static_cast<UnsignedShort>(startVertex + pPoly[i].K);
				m_curNumTreeIndices[bNdx]+=3;
			}
			IndexBufferExceptionFunc();
			} catch(...) {
				IndexBufferExceptionFunc();
			}
		}		
		if (m_curNumTreeIndices[bNdx] > groupFirstIndex) {
			TTreeDrawGroup &drawGroup = m_drawGroups[m_numDrawGroups++];
			drawGroup.texture = m_groupTextures[group];
			drawGroup.bufferNdx = bNdx;
			drawGroup.firstIndex = groupFirstIndex;
			drawGroup.numIndices = m_curNumTreeIndices[bNdx] - groupFirstIndex;
			drawGroup.firstVertex = groupFirstVertex;
			drawGroup.numVertices = m_curNumTreeVertices[bNdx] - groupFirstVertex;
		}
		}
	}

}
//=============================================================================
// W3DTreeBuffer::updateVertexBuffer
//=============================================================================
/** Updates the push aside offset in vertex buffer. */
//=============================================================================
void W3DTreeBuffer::updateVertexBuffer(void)
{
	if (!m_indexTree[0] || !m_vertexTree[0] || !m_initialized) {
		return;
	}
	Int bNdx;
	for	(bNdx = 0; bNdx<MAX_BUFFERS; bNdx++) {
		if (m_curNumTreeIndices[bNdx]==0) {
			break;
		}
		VertexFormatXYZNDUV1 *vb;
		// Lock the buffers.
	#ifdef USE_STATIC
		DeviceVertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexTree[bNdx], 0);
	#else 
		DeviceVertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexTree[bNdx], WW3D_LOCK_DISCARD);
	#endif
		vb=static_cast<VertexFormatXYZNDUV1*>(lockVtxBuffer.Get_Vertex_Array());

		VertexFormatXYZNDUV1 *curVb;

		Int curTree;
		for (curTree=0; curTree<m_numTrees; curTree++) {
			if (m_trees[curTree].bufferNdx!=bNdx) {
				continue;
			}
			Int type = m_trees[curTree].treeType;
			if (type<0) {
				continue; // Deleted tree. [6/9/2003]
			}
			if (m_trees[curTree].pushAsideDelta==0.0f && m_trees[curTree].m_toppleState == TOPPLE_UPRIGHT) {
				continue; // not toppling or pushed, no need to update. jba [7/11/2003]
			}
			m_anyPushChanged = true;
			if (!m_trees[curTree].visible) continue;
			Real scale = m_trees[curTree].scale;
			Vector3 loc = m_trees[curTree].location;
			Real theSin = m_trees[curTree].sin;
			Real theCos = m_trees[curTree].cos;
			if (type<0 || m_treeTypes[type].m_mesh == 0) {
				type = 0;
			}

			Int startVertex = m_trees[curTree].firstIndex;
			curVb = vb+startVertex;
			Int i;
			Int numVertex = m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Count();
			if (startVertex + numVertex > m_curNumTreeVertices[bNdx]) {
				continue; // does not match the layout written at load time
			}
			Vector3 *pVert = m_treeTypes[type].m_mesh->Peek_Model()->Get_Vertex_Array();

			for (i=0; i<numVertex; i++) {
				Real x = pVert[i].X;
				Real y = pVert[i].Y;

				
				Vector3 vLoc;
				x += m_treeTypes[type].m_offset.X;
				y += m_treeTypes[type].m_offset.Y;
				vLoc.X = x*scale*theCos - y*scale*theSin;
				vLoc.Y = y*scale*theCos + x*scale*theSin;
				vLoc.Z = pVert[i].Z*scale; 
				vLoc.Z += m_treeTypes[type].m_offset.Z;

				if (m_trees[curTree].m_toppleState != TOPPLE_UPRIGHT) {
					m_trees[curTree].m_mtx.Transform_Vector(m_trees[curTree].m_mtx, vLoc, &vLoc);
				} else {
					if (m_trees[curTree].pushAside>0.0f) {
						vLoc.X += pVert[i].Z * m_trees[curTree].pushAside * m_trees[curTree].pushAsideCos * m_treeTypes[type].m_data->m_maxOutwardMovement;
						vLoc.Y += pVert[i].Z * m_trees[curTree].pushAside * m_trees[curTree].pushAsideSin* m_treeTypes[type].m_data->m_maxOutwardMovement;
					}
					vLoc.X += loc.X;
					vLoc.Y += loc.Y;
					vLoc.Z += loc.Z;
				}

				curVb->x = vLoc.X;
				curVb->y = vLoc.Y;
				curVb->z = vLoc.Z;
				curVb->ny = 1.0f - m_treeTypes[type].m_data->m_darkening*m_trees[curTree].pushAside;
				curVb++;
			}
		}		
	}
}

//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// W3DTreeBuffer::~W3DTreeBuffer
//=============================================================================
/** Destructor. Releases w3d assets. */
//=============================================================================
W3DTreeBuffer::~W3DTreeBuffer(void)
{
	freeTreeBuffers();
	REF_PTR_RELEASE(m_treeTexture);
	Int i;
	for (i=0; i<MAX_TYPES; i++) {
		REF_PTR_RELEASE(m_treeTypes[i].m_mesh);
		REF_PTR_RELEASE(m_treeTypes[i].m_ownTexture);
	}
	if (m_shadow) {
		delete m_shadow;
		m_shadow = nullptr;
	}
}

//=============================================================================
// W3DTreeBuffer::W3DTreeBuffer
//=============================================================================
/** Constructor. Sets m_initialized to true if it finds the w3d models it needs
for the trees. */
//=============================================================================
W3DTreeBuffer::W3DTreeBuffer(void)
{
	// No memset: the class has a vtable. clearAllTrees() and loadTreeTypes() release the type
	// meshes and source tiles, so those pointers and counts must be null first.
	m_initialized = false;
	Int i;
	for (i=0; i<MAX_TYPES; i++) {
		m_treeTypes[i].m_mesh = nullptr;
		m_treeTypes[i].m_numTiles = 0;
		m_treeTypes[i].m_ownTexture = nullptr;
		m_treeTypes[i].m_drawGroup = 0;
	}
	m_groupTextures[0] = nullptr;
	m_numTextureGroups = 1;
	m_numDrawGroups = 0;
	for (i=0; i<MAX_TILES; i++) {
		m_sourceTiles[i] = nullptr;
	}
	for (i=0; i<MAX_SWAY_TYPES; i++) {
		m_curSwayOffset[i] = 0;
		m_curSwayStep[i] = 0;
		m_curSwayFactor[i] = 0;
	}
	m_numTiles = 0;
	m_numTrees = 0;
	m_numTreeTypes = 0;
	m_textureWidth = 0;
	m_textureHeight = 0;
	m_anythingChanged = false;
	m_anyPushChanged = false;
	m_updateAllKeys = false;
	m_isTerrainPass = false;
	m_needToUpdateTexture = false;
	m_cameraLookAtVector.Set(0, 0, 0);
	for	(i=0; i<MAX_BUFFERS; i++) {
		m_vertexTree[i] = nullptr;
		m_indexTree[i] = nullptr;
		m_curNumTreeVertices[i]=0;
		m_curNumTreeIndices[i]=0;
	}
	m_treeTexture = nullptr;
	m_tileScale = 1;
	m_dwTreeVertexShader = nullptr;
	clearAllTrees();
	allocateTreeBuffers();
	m_initialized = true;
	m_curSwayVersion = -1;
	m_lastSwayTimeMs = 0;
	m_swayTimeValid = false;
	m_toppleStepCarry = 0.0f;

	m_shadow = nullptr;

}


//=============================================================================
// W3DTreeBuffer::freeTreeBuffers
//=============================================================================
/** Frees the index and vertex buffers. */
//=============================================================================
void W3DTreeBuffer::freeTreeBuffers(void)
{
	Int i;
	for	(i=0; i<MAX_BUFFERS; i++) {
		REF_PTR_RELEASE(m_vertexTree[i]);
		REF_PTR_RELEASE(m_indexTree[i]);
	}
	
	if (m_dwTreeVertexShader)
		RenderDevice::Release_Resource(m_dwTreeVertexShader);
	m_dwTreeVertexShader = nullptr;
}

//=============================================================================
// W3DTreeBuffer::unitMoved
//=============================================================================
/** Check to see if a unit collided with a tree/grass/bush. */
//=============================================================================
void W3DTreeBuffer::unitMoved(Object *unit)
{
	if (unit->isKindOf(KINDOF_IMMOBILE)) {
		// This is the initial positioning of the object, and we don't care. jba. [6/5/2003]
		return;
	}
	Real radius = unit->getGeometryInfo().getMajorRadius();
	if (unit->getGeometryInfo().getGeomType()==GEOMETRY_BOX) {
		if (radius>unit->getGeometryInfo().getMinorRadius()) {
			radius = unit->getGeometryInfo().getMinorRadius();
		}
	}
	// Value to assume for the tree radius.
#define TREE_RADIUS_APPROX 7.0f
	radius += TREE_RADIUS_APPROX;

	Coord3D pos = *unit->getPosition();
	Real x = pos.x-radius;
	Real y = pos.y-radius;
	if (x<m_bounds.lo.x) x = m_bounds.lo.x;
	if (y<m_bounds.lo.y) y = m_bounds.lo.y;
	if (x>m_bounds.hi.x) x = m_bounds.hi.x;
	if (y>m_bounds.hi.y) y = m_bounds.hi.y;
	Int xIndex = REAL_TO_INT_FLOOR ( (x/(m_bounds.hi.x-m_bounds.lo.x)) * (static_cast<Real>(PARTITION_WIDTH_HEIGHT)-0.1f) );
	Int yIndex = REAL_TO_INT_FLOOR ( (y/(m_bounds.hi.y-m_bounds.lo.y)) * (static_cast<Real>(PARTITION_WIDTH_HEIGHT)-0.1f) );
	DEBUG_ASSERTCRASH(xIndex>=0 && yIndex>=0 && xIndex<PARTITION_WIDTH_HEIGHT && yIndex<PARTITION_WIDTH_HEIGHT, ("Invalid range."));

	x = pos.x+radius;
	y = pos.y+radius;
	if (x<m_bounds.lo.x) x = m_bounds.lo.x;
	if (y<m_bounds.lo.y) y = m_bounds.lo.y;
	if (x>m_bounds.hi.x) x = m_bounds.hi.x;
	if (y>m_bounds.hi.y) y = m_bounds.hi.y;
	Int xMax = REAL_TO_INT_CEIL ( (x/(m_bounds.hi.x-m_bounds.lo.x)) * (static_cast<Real>(PARTITION_WIDTH_HEIGHT)-0.1f) );
	Int yMax = REAL_TO_INT_CEIL ( (y/(m_bounds.hi.y-m_bounds.lo.y)) * (static_cast<Real>(PARTITION_WIDTH_HEIGHT)-0.1f) );
	DEBUG_ASSERTCRASH(xMax>=0 && yMax>=0 && xMax<=PARTITION_WIDTH_HEIGHT && yMax<=PARTITION_WIDTH_HEIGHT, ("Invalid range."));
	Int i, j;
	for (i=xIndex; i<xMax; i++) {
		for (j=yIndex; j<yMax; j++) {
			Int treeNdx = m_areaPartition[i + PARTITION_WIDTH_HEIGHT*j];
			while (treeNdx != END_OF_PARTITION) {
				// paranoia [7/7/2003]
				if (treeNdx<0 || treeNdx>=m_numTrees) {
					DEBUG_CRASH(("Invalid index."));
					break;
				}
				if (m_trees[treeNdx].treeType<0) {
					treeNdx = m_trees[treeNdx].nextInPartition;
					continue;	//  Tree is deleted. [7/11/2003]
				}
				Coord3D delta;
				delta.set(m_trees[treeNdx].location.X, m_trees[treeNdx].location.Y, m_trees[treeNdx].location.Z );
				delta.sub(&pos);
				if (radius*radius>delta.lengthSqr()) {
					bool canTopple = unit->getCrusherLevel() > 1;
					if (canTopple && m_treeTypes[m_trees[treeNdx].treeType].m_data->m_doTopple) {
						// Give a vector with direction to thing.
						Coord3D toppleVector;
						toppleVector.set(m_trees[treeNdx].location.X, m_trees[treeNdx].location.Y, 0);
						toppleVector.x -= unit->getPosition()->x;
						toppleVector.y -= unit->getPosition()->y;
						applyTopplingForce(m_trees+treeNdx, &toppleVector, 0, W3D_TOPPLE_OPTIONS_NONE);
					} else if (m_treeTypes[m_trees[treeNdx].treeType].m_data->m_framesToMoveOutward>1) {
						pushAsideTree(m_trees[treeNdx].drawableID, &pos, unit->getUnitDirectionVector2D(), unit->getID());
					}
				}
				treeNdx = m_trees[treeNdx].nextInPartition;
			}
		}
	}


}

//=============================================================================
// W3DTreeBuffer::allocateTreeBuffers
//=============================================================================
/** Allocates the index and vertex buffers. */
//=============================================================================
void W3DTreeBuffer::allocateTreeBuffers(void)
{
	Int i;
	for	(i=0; i<MAX_BUFFERS; i++) {
	#ifdef USE_STATIC
		m_vertexTree[i]=NEW_REF(DeviceVertexBufferClass,(RENDER_FVF_XYZNDUV1,MAX_TREE_VERTEX+4,DeviceVertexBufferClass::USAGE_DEFAULT));
		m_indexTree[i]=NEW_REF(DeviceIndexBufferClass,(MAX_TREE_INDEX+4, DeviceIndexBufferClass::USAGE_DEFAULT));
	#else
		m_vertexTree[i]=NEW_REF(DeviceVertexBufferClass,(RENDER_FVF_XYZNDUV1,MAX_TREE_VERTEX+4,DeviceVertexBufferClass::USAGE_DYNAMIC));
		m_indexTree[i]=NEW_REF(DeviceIndexBufferClass,(MAX_TREE_INDEX+4, DeviceIndexBufferClass::USAGE_DYNAMIC));
	#endif
		m_curNumTreeVertices[i]=0;
		m_curNumTreeIndices[i]=0;
	}

	HRESULT hr;
	hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\Trees.vso", nullptr, 0, true, reinterpret_cast<void**>(&m_dwTreeVertexShader));
	if (FAILED(hr))
		return;

}

//=============================================================================
// W3DTreeBuffer::clearAllTrees
//=============================================================================
/** Removes all trees. */
//=============================================================================
void W3DTreeBuffer::clearAllTrees(void)
{
	m_numTrees=0;
	m_bounds.lo.x = m_bounds.lo.y = 0;
	m_bounds.hi.x = m_bounds.hi.y = 1;
	REF_PTR_RELEASE(m_treeTexture);
	for (Int buffer=0; buffer<MAX_BUFFERS; buffer++) {
		m_curNumTreeVertices[buffer]=0;
		m_curNumTreeIndices[buffer]=0;
	}
	m_anythingChanged = true;
	Int i;
	for (i=0; i<MAX_TYPES; i++) {
		REF_PTR_RELEASE(m_treeTypes[i].m_mesh);
		REF_PTR_RELEASE(m_treeTypes[i].m_ownTexture);
		m_treeTypes[i].m_drawGroup = 0;
	}
	m_numTextureGroups = 1;
	m_numDrawGroups = 0;
	for (i=0; i<PARTITION_WIDTH_HEIGHT*PARTITION_WIDTH_HEIGHT; i++) {
		m_areaPartition[i] = END_OF_PARTITION;
	}
	m_numTreeTypes = 0;
}

//=============================================================================
// W3DTreeBuffer::removeTree
//=============================================================================
/** Removes a tree.  */
//=============================================================================
void W3DTreeBuffer::removeTree(DrawableID id)
{
	Int i;
	for (i=0; i<m_numTrees; i++) {
		if (m_trees[i].drawableID == id) {
			m_trees[i].location = Vector3(0,0,0);
			m_trees[i].treeType = DELETED_TREE_TYPE;
			// Translate the bounding sphere of the model.
			m_trees[i].bounds.Center = Vector3(0,0,0);
			m_trees[i].bounds.Radius = 1;
			m_anythingChanged = true;
		}
	}
}

//=============================================================================
// W3DTreeBuffer::removeTree
//=============================================================================
/** Removes any trees that would be under a building.  */
//=============================================================================
void W3DTreeBuffer::removeTreesForConstruction(const Coord3D* pos, const GeometryInfo& geom, Real angle )
{
	// Just iterate all trees, as even non-collidable ones get removed. jba. [7/11/2003]
	Int i;
	for (i=0; i<m_numTrees; i++) {				// small, height,							radius,									minor radius
		if (m_trees[i].treeType < 0) {
			continue; // already deleted. jba [7/11/2003]
		}
		GeometryInfo info(GEOMETRY_CYLINDER, false, 5*TREE_RADIUS_APPROX, 2*TREE_RADIUS_APPROX, 2*TREE_RADIUS_APPROX);
		Coord3D treePos;
		treePos.set(m_trees[i].location.X, m_trees[i].location.Y, m_trees[i].location.Z);
		if (ThePartitionManager->geomCollidesWithGeom( pos, geom, angle, &treePos, info, 0.0f)) {
			// remove it [7/11/2003]
			m_trees[i].treeType = DELETED_TREE_TYPE;
			m_anythingChanged = true;
		} 
	}
}


//=============================================================================
// W3DTreeBuffer::addTreeTypes
//=============================================================================
/** Adds a type of tree (model & texture). */
//=============================================================================
Int W3DTreeBuffer::addTreeType(const W3DTreeDrawModuleData *data)
{
	if (m_numTreeTypes>=MAX_TYPES) {
		DEBUG_CRASH(("Too many kinds of trees in map.  Reduce kinds of trees, or raise tree limit. jba."));
		return 0;
	}
	m_needToUpdateTexture = true;

	m_treeTypes[m_numTreeTypes].m_mesh = nullptr;

	RenderObjClass *robj=WW3DAssetManager::Get_Instance()->Create_Render_Obj(data->m_modelName.str());

	if (robj==nullptr) {
		DEBUG_CRASH(("Unable to find model for tree %s\n", data->m_modelName.str()));
		return 0;
	}
	AABoxClass box;

	robj->Get_Obj_Space_Bounding_Box(box);
	Vector3 offset(0,0,0);
	if (robj->Class_ID() == RenderObjClass::CLASSID_HLOD) {
		RenderObjClass *hlod = robj;
		robj = hlod->Get_Sub_Object(0);
		const Matrix3D xfm = robj->Get_Bone_Transform(0);
		xfm.Get_Translation(&offset);
		REF_PTR_RELEASE(hlod);
	}

	if (robj->Class_ID() == RenderObjClass::CLASSID_MESH)
		m_treeTypes[m_numTreeTypes].m_mesh = static_cast<MeshClass*>(robj);

	if (m_treeTypes[m_numTreeTypes].m_mesh==nullptr) {
		DEBUG_CRASH(("Tree %s is not simple mesh. Tell artist to re-export. Don't Ignore!!!\n", data->m_modelName.str()));
		return 0;
	}

	Int numVertex = m_treeTypes[m_numTreeTypes].m_mesh->Peek_Model()->Get_Vertex_Count();
	Vector3 *pVert = m_treeTypes[m_numTreeTypes].m_mesh->Peek_Model()->Get_Vertex_Array();

	const Matrix3D xfm = m_treeTypes[m_numTreeTypes].m_mesh->Get_Transform();
	SphereClass bounds(pVert, numVertex);
	bounds.Center += offset;
	m_treeTypes[m_numTreeTypes].m_bounds = bounds;
	m_treeTypes[m_numTreeTypes].m_textureOrigin.x = 0;
	m_treeTypes[m_numTreeTypes].m_textureOrigin.y = 0;
	m_treeTypes[m_numTreeTypes].m_data = data;
	m_treeTypes[m_numTreeTypes].m_offset = offset;
	m_treeTypes[m_numTreeTypes].m_shadowSize = (box.Extent.X + box.Extent.Y); // Average extent * 2. jba.
	m_treeTypes[m_numTreeTypes].m_doShadow = data->m_doShadow;
	m_numTreeTypes++;
	return m_numTreeTypes-1;
}

//=============================================================================
// W3DTreeBuffer::addTree
//=============================================================================
/** Adds a tree.  Name is the W3D model name, supported models are
ALPINE, DECIDUOUS and SHRUB. */
//=============================================================================
void W3DTreeBuffer::addTree(DrawableID id, Coord3D location, Real scale, Real angle,
								Real randomScaleAmount, const W3DTreeDrawModuleData *data)
{
	if (m_numTrees >= MAX_TREES) {
		return;  
	}
	if (!m_initialized) {
		return;  
	}
	Int treeType = DELETED_TREE_TYPE;
	Int i;
	for (i=0; i<m_numTreeTypes; i++) {
		if (m_treeTypes[i].m_data->m_modelName.compareNoCase(data->m_modelName)==0 && 
				m_treeTypes[i].m_data->m_textureName.compareNoCase(data->m_textureName)==0) {
			treeType = i;
			break;
		}
	}
	if (treeType<0) {
		treeType = addTreeType(data);
		if (treeType<0) {
			return;
		}
		m_needToUpdateTexture = true;
	}
	if (data->m_framesToMoveOutward > 2 || data->m_doTopple) {
		// Trees/grass that topples or gets pushed aside (outward) gets put in the area partition. jba [7/7/2003]
		Short bucket = static_cast<Short>(getPartitionBucket(location));
		m_trees[m_numTrees].nextInPartition = m_areaPartition[bucket];
		m_areaPartition[bucket] = static_cast<Short>(m_numTrees);
	} else {
		m_trees[m_numTrees].nextInPartition = END_OF_PARTITION;
	}

	Real randomScale = GameClientRandomValueReal( 1.0f - randomScaleAmount, 1.0f+ randomScaleAmount );
	m_trees[m_numTrees].sin = WWMath::Sin(angle);
	m_trees[m_numTrees].cos = WWMath::Cos(angle);
	if (randomScaleAmount>0.0f) {
		// Randomizes the scale and orientation of trees.
		m_trees[m_numTrees].scale = scale*randomScale;
	} else {
		// Don't randomly scale & orient
		m_trees[m_numTrees].scale = scale;
	}
	m_trees[m_numTrees].location = Vector3(location.x, location.y, location.z);
	m_trees[m_numTrees].treeType = treeType;
	// Translate the bounding sphere of the model.
	m_trees[m_numTrees].bounds = m_treeTypes[treeType].m_bounds;
	m_trees[m_numTrees].bounds.Center *= m_trees[m_numTrees].scale;
	m_trees[m_numTrees].bounds.Radius *= m_trees[m_numTrees].scale;
	m_trees[m_numTrees].bounds.Center += m_trees[m_numTrees].location;
	// Initially set it invisible.  cull will update it's visiblity flag.
	m_trees[m_numTrees].visible = false;
	m_trees[m_numTrees].drawableID = id;

	m_trees[m_numTrees].firstIndex = 0;
	m_trees[m_numTrees].bufferNdx = -1;

	m_trees[m_numTrees].swayType = GameClientRandomValue(0, MAX_SWAY_TYPES-1);
	m_trees[m_numTrees].pushAside = 0;
	m_trees[m_numTrees].lastFrameUpdated = 0;
	m_trees[m_numTrees].pushAsideSource = INVALID_ID;
	m_trees[m_numTrees].pushAsideDelta = 0;
	m_trees[m_numTrees].pushAsideCos = 1;
	m_trees[m_numTrees].pushAsideSin = 1;
	m_trees[m_numTrees].m_toppleState = TOPPLE_UPRIGHT;
	m_numTrees++;
}

//=============================================================================
// W3DTreeBuffer::updateTreePosition
//=============================================================================
/** Updates a tree's position */
//=============================================================================
Bool W3DTreeBuffer::updateTreePosition(DrawableID id, Coord3D location, Real angle)
{
	Int i;
	for (i=0; i<m_numTrees; i++) {
		if (m_trees[i].drawableID == id) {
			m_trees[i].location = Vector3(location.x, location.y, location.z);
			m_trees[i].sin = WWMath::Sin(angle);
			m_trees[i].cos = WWMath::Cos(angle);
			// Translate the bounding sphere of the model.
			m_trees[i].bounds = m_treeTypes[m_trees[i].treeType].m_bounds;
			m_trees[i].bounds.Center *= m_trees[i].scale;
			m_trees[i].bounds.Radius *= m_trees[i].scale;
			m_trees[i].bounds.Center += m_trees[i].location;
			m_anythingChanged = true;
			return true;
		}
	}
	return false;
}

//=============================================================================
// W3DTreeBuffer::pushAsideTree
//=============================================================================
/** Push sideways tree or grass. */
//=============================================================================
void W3DTreeBuffer::pushAsideTree(DrawableID id, const Coord3D *pusherPos, 
																	const Coord3D *pusherDirection, ObjectID pusherID )
{
	Int i;
	for (i=0; i<m_numTrees; i++) {
		if (m_trees[i].drawableID == id) {
			UnsignedInt lastFrame = m_trees[i].lastFrameUpdated;
			m_trees[i].lastFrameUpdated = TheGameLogic->getFrame();
			if(m_trees[i].pushAsideSource == pusherID) {
				if (m_trees[i].lastFrameUpdated - lastFrame < 3)
					return; // already pushing. [5/28/2003]
			}

			if(m_trees[i].pushAside != 0.0f) {
				return; // already pushing. [5/28/2003]
			}
			m_trees[i].pushAsideSource = pusherID;
			Coord3D delta;
			delta.set(m_trees[i].location.X, m_trees[i].location.Y, m_trees[i].location.Z);
			delta.sub(pusherPos);

			if (pusherDirection->x*delta.y - pusherDirection->y*delta.x > 0.0f) {
				m_trees[i].pushAsideCos = -pusherDirection->y;
				m_trees[i].pushAsideSin = pusherDirection->x;
			} else {
				m_trees[i].pushAsideCos = pusherDirection->y;
				m_trees[i].pushAsideSin = -pusherDirection->x;
			}
			m_anyPushChanged = true;
			m_trees[i].pushAsideDelta = 1.0f/static_cast<Real>(m_treeTypes[m_trees[i].treeType].m_data->m_framesToMoveOutward);
		}
	}
}

DECLARE_PERF_TIMER(Tree_Render)

//=============================================================================
// W3DTreeBuffer::drawTrees
//=============================================================================
/** Draws the trees.  Uses camera to cull. */
//=============================================================================
void W3DTreeBuffer::drawTrees(CameraClass * camera, RefRenderObjListIterator *pDynamicLightsIterator)
{
	USE_PERF_TIMER(Tree_Render)
	if (!m_isTerrainPass) {
		return;
	}

	// if breeze changes, always process the full update, even if not visible, 
	// so that things offscreen won't 'pop' when first viewed
	const BreezeInfo& info = TheScriptEngine->getBreezeInfo();
	Bool pause = TheScriptEngine->isTimeFrozenScript() || TheScriptEngine->isTimeFrozenDebug();
	if (TheGameLogic && TheGameLogic->isGamePaused()) {
		pause = true;
	}
	Int i;
	if (!pause) {
		if (info.m_breezeVersion != m_curSwayVersion) 
		{
			updateSway(info);
		}		
	}
	// Sway advances by the render clock, one step per 33 ms, so it is independent of the frame rate
	// (RenderFPSLimit) and stays reproducible under -fixedTimeStep (one step per tick). No elapsed time,
	// including the first call, means no step.
	Real swayTicks = 0.0f;
	Real swayClock = 0.0f;	// seconds, wrapped at 200 periods of the shader's 5 rad/s flutter
	{
		const UnsignedInt nowMs = GameEngine_RenderTimeMs();
		swayClock = static_cast<Real>(nowMs % 1256637u) * 0.001f;
		if (m_swayTimeValid) {
			UnsignedInt dt = nowMs - m_lastSwayTimeMs;
			if (dt > 250) {
				dt = 250;	// do not catch up after pause, loading or a stall
			}
			swayTicks = (Real)dt / 33.0f;
		}
		m_lastSwayTimeMs = nowMs;
		m_swayTimeValid = true;
	}
	Vector3 swayFactor[MAX_SWAY_TYPES];
	for (i=0; i<MAX_SWAY_TYPES; i++) {
		if (!pause) {
			m_curSwayOffset[i] += m_curSwayStep[i] * swayTicks;
			while (m_curSwayOffset[i] > NUM_SWAY_ENTRIES-1) {	// may wrap more than once after a large time step
				m_curSwayOffset[i] -= NUM_SWAY_ENTRIES-1;
			}
		}
		Int minOffset = REAL_TO_INT_FLOOR(m_curSwayOffset[i]);
		if (minOffset>=0 && minOffset+1<NUM_SWAY_ENTRIES) {
			Real f2 = m_curSwayOffset[i] - minOffset;
			Real f1 = 1.0f - f2;
			swayFactor[i] = f1*m_swayOffsets[minOffset] + f2*m_swayOffsets[minOffset+1];
			swayFactor[i] *= m_curSwayFactor[i];
			// slow gust envelope, a different phase per sway type
			const Real gust = 1.0f + 0.3f * Sin(swayClock * 0.31f + i * 1.7f) * Sin(swayClock * 0.17f + i * 0.9f);
			swayFactor[i] *= gust;
		}
	}
	
	m_isTerrainPass = false;

	if (m_needToUpdateTexture) {
		m_needToUpdateTexture = false;
		updateTexture();
	}
	if (m_treeTexture==nullptr) {
		return;
	}
	if (m_updateAllKeys) {
		cull(camera);
	}

	Int curTree;
	// Draw tree shadows.
	// With shadow maps the trees cast real shadows, so no decal.
	if (m_shadow && TheW3DProjectedShadowManager && TheGlobalData->m_useShadowDecals && !RenderDevice::Shadow_Maps_Active()) {
		for (curTree=0; curTree<m_numTrees; curTree++) {
			Int type = m_trees[curTree].treeType;
			if (type<0) { // deleted.
				continue;
			}
			if (!m_trees[curTree].visible || !m_treeTypes[type].m_doShadow) {
				continue;
			}
			Real factor = 1.0f;
			if (m_trees[curTree].m_toppleState == TOPPLE_FALLING ||
					m_trees[curTree].m_toppleState == TOPPLE_DOWN) {
				continue;
			}
			m_shadow->setSize(m_treeTypes[type].m_shadowSize, -m_treeTypes[type].m_shadowSize*factor);
			m_shadow->setPosition(m_trees[curTree].location.X, m_trees[curTree].location.Y, m_trees[curTree].location.Z);
			TheW3DProjectedShadowManager->queueDecal(m_shadow);
		}
		TheW3DProjectedShadowManager->flushDecals(m_shadow->getTexture(0), SHADOW_DECAL);
	}

	// Update pushed aside and toppling trees: one step per 1/30 s of render time (once per drawn frame at 30 frames
	// per second), at most 8 per frame.
	Int toppleSteps = 0;
	if (pause) {
		m_toppleStepCarry = 0.0f;
	} else {
		m_toppleStepCarry += GameEngine_RenderFrameSteps();
		toppleSteps = static_cast<Int>(m_toppleStepCarry);
		m_toppleStepCarry -= static_cast<Real>(toppleSteps);
		if (toppleSteps > 8)
			toppleSteps = 8;
	}
	for (Int toppleStep = 0; toppleStep < toppleSteps; toppleStep++)
	for (curTree=0; curTree<m_numTrees; curTree++) {
		Int type = m_trees[curTree].treeType;
		if (type<0) { // deleted.
			continue;
		}
		if(m_trees[curTree].m_toppleState == TOPPLE_FALLING ||
			 m_trees[curTree].m_toppleState == TOPPLE_FOGGED) {
			updateTopplingTree(m_trees+curTree);
		} else if(m_trees[curTree].m_toppleState == TOPPLE_DOWN) {
			if (m_treeTypes[type].m_data->m_killWhenToppled) {
				if (m_trees[curTree].m_sinkFramesLeft==0) {
					m_trees[curTree].treeType = DELETED_TREE_TYPE; // delete it. [7/11/2003]
					m_anythingChanged = true; // need to regenerate trees. [7/11/2003]
				}
				m_trees[curTree].m_sinkFramesLeft--;
				m_trees[curTree].location.Z -= m_treeTypes[type].m_data->m_sinkDistance/m_treeTypes[type].m_data->m_sinkFrames;
				m_trees[curTree].m_mtx.Set_Translation(m_trees[curTree].location);
			}	
		} else if (m_trees[curTree].pushAsideDelta!=0.0f) {
			m_trees[curTree].pushAside += m_trees[curTree].pushAsideDelta;
			if (m_trees[curTree].pushAside>=1.0f) {
				m_trees[curTree].pushAsideDelta = static_cast<Real>(-1.0/static_cast<Real>(m_treeTypes[type].m_data->m_framesToMoveInward));
			} else if (m_trees[curTree].pushAside<=0.0f) {
				m_trees[curTree].pushAsideDelta = 0.0f;
				m_trees[curTree].pushAside = 0.0f;
			}
		} 
	}

	if (m_anythingChanged) {
		loadTreesInVertexAndIndexBuffers(pDynamicLightsIterator);
		m_anythingChanged = false;
	} else if (m_anyPushChanged) {
		m_anyPushChanged = false;
		updateVertexBuffer();
	}

//#define DEBUG_TEXTURE 1
#ifdef DEBUG_TEXTURE // Draw the combined texture for debugging. jba. [4/21/2003]
	// Setup the vertex buffer, shader & texture.
	RenderDevice::Set_Shader(detailAlphaShader);
	RenderDevice::Set_Texture(0,m_treeTexture);
	DynamicIBAccessClass ib_access(BUFFER_TYPE_DYNAMIC_DEVICE, 6);
	//draw an infinite sky plane
	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DEVICE, RENDER_FVF_XYZNDUV2, 4);
	{
		DynamicIBAccessClass::WriteLockClass ibLock(&ib_access);
		UnsignedShort *ndx = ibLock.Get_Index_Array();

		if (ndx) {
			ndx[0] = 0;
			ndx[1] = 1;
			ndx[2] = 2;
			ndx[3] = 1;
			ndx[4] = 3;
			ndx[5] = 2;
		}
		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();
		if(verts)
		{
			Real width = 300;
			Real origin = 40;
			verts[0].x=origin;
			verts[0].y=origin;
			verts[0].z=15;
			verts[0].u1=0;
			verts[0].v1=0;
			verts[0].diffuse=0xffffffff;
	
			verts[1].x=origin+width;
			verts[1].y=origin;
			verts[1].z=15;
			verts[1].u1=1;
			verts[1].v1=0;
			verts[1].diffuse=0xffffffff;
	
			verts[2].x=origin;
			verts[2].y=origin+width;
			verts[2].z=15;
			verts[2].u1=0;
			verts[2].v1=1;
			verts[2].diffuse=0xffffffff;
	
			verts[3].x=origin+width;
			verts[3].y=origin+width;
			verts[3].z=15;
			verts[3].u1=1;
			verts[3].v1=1;
			verts[3].diffuse=0xffffffff;
		}
	}

	RenderDevice::Set_Index_Buffer(ib_access,0);
	RenderDevice::Set_Vertex_Buffer(vb_access);

	Matrix3D tm(1);
	RenderDevice::Set_Transform(WW3D_TS_WORLD,tm);

	RenderDevice::Draw_Triangles(	0,2, 0,	4);	//draw a quad, 2 triangles, 4 verts
#endif


	if (m_curNumTreeIndices[0] == 0) {
		return;
	}
	RenderDevice::Set_Shader(detailAlphaShader);	

	RenderDevice::Set_Texture(0,m_treeTexture);
	RenderDevice::Set_Texture(1,nullptr);
	RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Coord_Index(0));
	RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(1));
	// Draw all the trees.
	RenderDevice::Apply_Render_State_Changes();
	W3DShaderManager::setShroudTex(1);
	RenderDevice::Apply_Render_State_Changes();
 
	if (m_dwTreeVertexShader) {
		WWD3DMatrix matProj, matView, matWorld;
		RenderDevice::_Get_Device_Transform(WW3D_TS_WORLD, *reinterpret_cast<Matrix4x4*>(&matWorld));
		RenderDevice::_Get_Device_Transform(WW3D_TS_VIEW, *reinterpret_cast<Matrix4x4*>(&matView));
		// projection including the backend's raster offset, so trees match the fixed-function path
		RenderDevice::_Get_Device_Shader_Projection(*reinterpret_cast<Matrix4x4*>(&matProj));
		WWD3DMatrix mat;
		WWD3D::MatrixMultiply( &mat, &matView, &matProj );
		WWD3D::MatrixMultiply( &mat, &matWorld, &mat );
		WWD3D::MatrixTranspose( &mat, &mat );

		static_assert(MAX_SWAY_TYPES + 1 == sizeof(RenderTreesVS::sway) / sizeof(Vector4), "sway table size");
		// Composite World-View-Projection Matrix
		RenderDevice::Set_Vertex_Shader_Constant(RENDER_PIECE_REG(RenderTreesVS, worldViewProj), reinterpret_cast<const float*>(&mat), 4 );
		Vector4 noSway(0,0,0,0);
		const Int swayReg = RENDER_PIECE_REG(RenderTreesVS, sway);
		RenderDevice::Set_Vertex_Shader_Constant(swayReg, reinterpret_cast<const float*>(&noSway), 1 );

		// sway[1..MAX_SWAY_TYPES] - the sway amount.
		for	(i=0; i<MAX_SWAY_TYPES; i++) {
			Vector4 sway4(swayFactor[i].X, swayFactor[i].Y, swayFactor[i].Z, swayClock);
			RenderDevice::Set_Vertex_Shader_Constant(swayReg+1+i, reinterpret_cast<const float*>(&sway4), 1 );
		}

		W3DShroud *shroud;
		if ((shroud=TheTerrainRenderObject->getShroud()) != 0) {
			// Setup shroud texture info [6/6/2003]
			float xoffset = 0;
			float yoffset = 0;
			Real width=shroud->getCellWidth();
			Real height=shroud->getCellHeight();

			xoffset = -static_cast<float>(shroud->getDrawOriginX()) + width;
			yoffset = -static_cast<float>(shroud->getDrawOriginY()) + height;
			Vector4 offset(xoffset, yoffset, 0, 0);
			RenderDevice::Set_Vertex_Shader_Constant(RENDER_PIECE_REG(RenderTreesVS, shroudOffset), reinterpret_cast<const float*>(&offset), 1 );
			width = 1.0f/(width*shroud->getTextureWidth());
			height = 1.0f/(height*shroud->getTextureHeight());
			offset.Set(width, height, 1, 1);
			RenderDevice::Set_Vertex_Shader_Constant(RENDER_PIECE_REG(RenderTreesVS, shroudScale), reinterpret_cast<const float*>(&offset), 1 );

		} else {
			Vector4 offset(0,0,0,0);
			RenderDevice::Set_Vertex_Shader_Constant(RENDER_PIECE_REG(RenderTreesVS, shroudOffset), reinterpret_cast<const float*>(&offset), 1 );
			RenderDevice::Set_Vertex_Shader_Constant(RENDER_PIECE_REG(RenderTreesVS, shroudScale), reinterpret_cast<const float*>(&offset), 1 );
		}

		// Set_Vertex_Shader takes an FVF code (vertex layout); the tree buffers are RENDER_FVF_XYZNDUV1.
		// The actual vertex shader is set per buffer below.
		RenderDevice::Set_Vertex_Shader(RENDER_FVF_XYZNDUV1);

	} else {
		RenderDevice::Set_Vertex_Shader(RENDER_FVF_XYZNDUV1);
	}


	Int bNdx;
	for (bNdx=0;bNdx<MAX_BUFFERS; bNdx++) {
		if (m_curNumTreeIndices[bNdx]==0) {
			break;
		}
		RenderDevice::Set_Index_Buffer(m_indexTree[bNdx],0);
		RenderDevice::Set_Vertex_Buffer(m_vertexTree[bNdx]);
		// Render the waving grass
		RenderDevice::Apply_Render_State_Changes();
		if (m_dwTreeVertexShader) {
			RenderDevice::Set_Vertex_Shader_Object(m_dwTreeVertexShader);
			RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Tex_Coord_Index(0));
			RenderDevice::Set_Texture_Stage(1, TextureStageDesc().Tex_Coord_Index(1).Tex_Transform(WW3D_TTFF_DISABLE));
		}
		for (Int g=0; g<m_numDrawGroups; g++) {
			const TTreeDrawGroup &drawGroup = m_drawGroups[g];
			if (drawGroup.bufferNdx != bNdx) {
				continue;
			}
			RenderDevice::Set_Texture(0, drawGroup.texture ? drawGroup.texture : m_treeTexture);
			RenderDevice::Draw_Triangles(static_cast<unsigned short>(drawGroup.firstIndex), static_cast<unsigned short>(drawGroup.numIndices/3), static_cast<unsigned short>(drawGroup.firstVertex), static_cast<unsigned short>(drawGroup.numVertices));
		}
	}

	RenderDevice::Set_Vertex_Shader(RENDER_FVF_XYZNDUV1);
	RenderDevice::Set_Pixel_Shader(NULL);
	RenderDevice::Invalidate_Cached_Render_States();	//code above mucks around with W3D states so make sure we reset

}

//-------------------------------------------------------------------------------------------------
///< Start the toppling process by giving a force vector
//-------------------------------------------------------------------------------------------------
void W3DTreeBuffer::applyTopplingForce( TTree *tree, const Coord3D* toppleDirection, Real toppleSpeed,
																			 UnsignedInt options )
{
	if (tree->m_toppleState != TOPPLE_UPRIGHT) {
		return;
	}
	const W3DTreeDrawModuleData* d = m_treeTypes[tree->treeType].m_data;
  // Having a low toppleSpeed is BAD. In particular, if the toppleSpeed is exactly 0, the
  // tree will stay upright forever, frozen in place (because the sway update is dead) 
  // but never dying
  if ( toppleSpeed < d->m_minimumToppleSpeed )
  {
    toppleSpeed = d->m_minimumToppleSpeed;
  }
  
	tree->m_toppleDirection = *toppleDirection;
	tree->m_toppleDirection.normalize();
	tree->m_angularAccumulation = 0;

	tree->m_angularVelocity = toppleSpeed * d->m_initialVelocityPercent;
	tree->m_angularAcceleration = toppleSpeed * d->m_initialAccelPercent;
	tree->m_toppleState = TOPPLE_FALLING;
	tree->m_options = options;
	Coord3D pos;
	pos.set(tree->location.X, tree->location.Y, tree->location.Z);
	FXList::doFXPos(d->m_toppleFX, &pos);
	m_anyPushChanged = true;
	tree->m_mtx.Make_Identity();
	tree->m_mtx.Set_Translation(tree->location);

}

// this is our "bounce" limit -- slightly less that 90 degrees, to account for slop.
static const Real ANGULAR_LIMIT = PI/2 - PI/64;		

//-------------------------------------------------------------------------------------------------
///< Keep track of rotational fall distance, bounce and/or stop when needed.
//-------------------------------------------------------------------------------------------------
void W3DTreeBuffer::updateTopplingTree(TTree *tree)
{
	//DLOG(Debug::Format("updating W3DTreeBuffer %08lx\n",this));
	DEBUG_ASSERTCRASH(tree->m_toppleState != TOPPLE_UPRIGHT, ("hmm, we should be sleeping here"));
	if ( (tree->m_toppleState == TOPPLE_UPRIGHT)  ||  (tree->m_toppleState == TOPPLE_DOWN) )
		return;

	const W3DTreeDrawModuleData* d = m_treeTypes[tree->treeType].m_data;
	Int localPlayerIndex = ThePlayerList ? ThePlayerList->getLocalPlayer()->getPlayerIndex() : 0;
	Coord3D pos;
	pos.set(tree->location.X, tree->location.Y, tree->location.Z);
	ObjectShroudStatus ss = ThePartitionManager->getPropShroudStatusForPlayer(localPlayerIndex, &pos);
	if (ss==OBJECTSHROUD_FOGGED) {
		// Don't update fogged trees. [8/11/2003]
		tree->m_toppleState = TOPPLE_FOGGED;
		return;
	} else if (tree->m_toppleState == TOPPLE_FOGGED) {
		// was fogged, now isn't.  
		tree->m_angularVelocity = 0;
		tree->m_toppleState = TOPPLE_DOWN;
		tree->m_mtx.In_Place_Pre_Rotate_X(-ANGULAR_LIMIT * tree->m_toppleDirection.y);
		tree->m_mtx.In_Place_Pre_Rotate_Y(ANGULAR_LIMIT * tree->m_toppleDirection.x);
		if (d->m_killWhenToppled) {
			// If got killed in the fog, just remove. jba [8/11/2003]
			tree->m_sinkFramesLeft = 0;
		}
		return;
	}
	const Real VELOCITY_BOUNCE_LIMIT = 0.01f;				// if the velocity after a bounce will be this or lower, just stop at zero
	const Real VELOCITY_BOUNCE_SOUND_LIMIT = 0.03f;	// and if this low, then skip the bounce sound

	Real curVelToUse = tree->m_angularVelocity;
	if (tree->m_angularAccumulation + curVelToUse > ANGULAR_LIMIT)
		curVelToUse = ANGULAR_LIMIT - tree->m_angularAccumulation;

	tree->m_mtx.In_Place_Pre_Rotate_X(-curVelToUse * tree->m_toppleDirection.y);
	tree->m_mtx.In_Place_Pre_Rotate_Y(curVelToUse * tree->m_toppleDirection.x);

	
	tree->m_angularAccumulation += curVelToUse;
	if ((tree->m_angularAccumulation >= ANGULAR_LIMIT) && (tree->m_angularVelocity > 0))
	{
		// Hit so either bounce or stop if too little remaining velocity.
		tree->m_angularVelocity *= -d->m_bounceVelocityPercent;

		if( BitTest( tree->m_options, W3D_TOPPLE_OPTIONS_NO_BOUNCE ) == TRUE || 
				fabs(tree->m_angularVelocity) < VELOCITY_BOUNCE_LIMIT )
		{
			// too slow, just stop
			tree->m_angularVelocity = 0;
			tree->m_toppleState = TOPPLE_DOWN;
			if (d->m_killWhenToppled) {
				tree->m_sinkFramesLeft = d->m_sinkFrames;
			}
		}
		else if( fabs(tree->m_angularVelocity) >= VELOCITY_BOUNCE_SOUND_LIMIT )
		{
			// fast enough bounce to warrant the bounce fx
			if( BitTest( tree->m_options, W3D_TOPPLE_OPTIONS_NO_FX ) == FALSE ) {
				Vector3 loc(0, 0, 3*TREE_RADIUS_APPROX); // Kinda towards the top of the tree. jba. [7/11/2003]
				Vector3 xloc;
				tree->m_mtx.Transform_Vector(tree->m_mtx, loc, &xloc);
				Coord3D pos2;
				pos2.set(xloc.X, xloc.Y, xloc.Z);
				FXList::doFXPos(d->m_bounceFX, &pos2);
			}
		}
	}
	else
	{
		tree->m_angularVelocity += tree->m_angularAcceleration;
	}

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DTreeBuffer::crc( Xfer *xfer )
{
	// empty. jba [8/11/2003]	
}  // end CRC

// ------------------------------------------------------------------------------------------------
/** Xfer
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DTreeBuffer::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	Int i;
	Int numTrees = m_numTrees;
	xfer->xferInt(&numTrees);
	if (xfer->getXferMode() == XFER_LOAD)	{	
		m_numTrees = 0;
		for (i=0; i<PARTITION_WIDTH_HEIGHT*PARTITION_WIDTH_HEIGHT; i++) {
			m_areaPartition[i] = END_OF_PARTITION;
		}
	}

	// Save trees. [8/11/2003]
	for	(i=0; i<numTrees; i++) {
		TTree tree;
		memset(&tree, 0, sizeof(tree));
		AsciiString modelName;
		AsciiString modelTexture;
		Int treeType = DELETED_TREE_TYPE;
		if (xfer->getXferMode() != XFER_LOAD) {
			tree = m_trees[i];
			treeType = m_trees[i].treeType;
			if (treeType != DELETED_TREE_TYPE) {
				modelName = m_treeTypes[treeType].m_data->m_modelName;
				modelTexture = m_treeTypes[treeType].m_data->m_textureName;
			}
		}
		xfer->xferAsciiString(&modelName);
		xfer->xferAsciiString(&modelTexture);
		if (xfer->getXferMode() == XFER_LOAD) {
			Int j;
			for (j=0; j<m_numTreeTypes; j++) {
				if (m_treeTypes[j].m_data->m_modelName.compareNoCase(modelName)==0 && 
						m_treeTypes[j].m_data->m_textureName.compareNoCase(modelTexture)==0) {
					treeType = j;
					break;
				}
			}
		}
				
		xfer->xferReal(&tree.location.X);
		xfer->xferReal(&tree.location.Y);
		xfer->xferReal(&tree.location.Z);

		xfer->xferReal(&tree.scale);	///< Scale at location.
		xfer->xferReal(&tree.sin);	///< Sine of the rotation angle at location.
		xfer->xferReal(&tree.cos);	///< Cosine of the rotation angle at location.

		xfer->xferDrawableID(&tree.drawableID);	///< Drawable this tree corresponds to.

		// Topple parameters. [7/7/2003]
		xfer->xferReal(&tree.m_angularVelocity);	///< Velocity in degrees per frame (or is it radians per frame?)
		xfer->xferReal(&tree.m_angularAcceleration);	///< Acceleration angularVelocity is increasing
		xfer->xferCoord3D(&tree.m_toppleDirection);	///< Z-less direction we are toppling
		xfer->xferUser(&tree.m_toppleState, sizeof(tree.m_toppleState));	///< Stage this module is in.
		xfer->xferReal(&tree.m_angularAccumulation);	///< How much have I rotated so I know when to bounce.
		xfer->xferUnsignedInt(&tree.m_options);	///< topple options
		xfer->xferMatrix3D(&tree.m_mtx);
		xfer->xferUnsignedInt(&tree.m_sinkFramesLeft);	///< Toppled trees sink into the terrain & disappear, how many frames left.

		if (xfer->getXferMode() == XFER_LOAD && treeType != DELETED_TREE_TYPE && treeType < m_numTreeTypes) {
			Coord3D pos;
			pos.set(tree.location.X, tree.location.Y, tree.location.Z);
			Real angle = 0;
			addTree(tree.drawableID, pos, tree.scale, angle, 0, m_treeTypes[treeType].m_data);
			if (m_numTrees) {
				TTree *curTree = &m_trees[m_numTrees-1];
				curTree->m_angularAcceleration = tree.m_angularAcceleration;
				curTree->m_angularVelocity = tree.m_angularVelocity;
				curTree->m_toppleDirection = tree.m_toppleDirection;
				curTree->m_toppleState = tree.m_toppleState;
				curTree->m_options = tree.m_options;
				curTree->m_mtx = tree.m_mtx;
				curTree->m_sinkFramesLeft = tree.m_sinkFramesLeft;
			}
		}
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DTreeBuffer::loadPostProcess( void )
{
	// empty. jba [8/11/2003]	
}  // end loadPostProcess




