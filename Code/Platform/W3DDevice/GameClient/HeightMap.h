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


#pragma once


#ifndef __HEIGHTMAP_H_
#define __HEIGHTMAP_H_

#include "always.h"
#include "rendobj.h"
#include "w3d_file.h"
#include "vertexbuffer.h"
#include "indexbuffer.h"
#include "WW3D2/Common/renderdevice.h"
#include "shader.h"
#include "vertmaterial.h"
#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include "WorldHeightMap.h"
#include "BaseHeightMap.h"

#define VERTEX_BUFFER_TILE_LENGTH	32		//tiles of side length 32 (grid of 33x33 vertices).

// Adjust the triangles to make cliff sides most attractive.  jba.
#define FLIP_TRIANGLES 1


/// Custom render object that draws the heightmap and handles intersection tests.
/**
Custom W3D render object that's used to process the terrain.  It handles
virtually everything to do with the terrain, including: drawing, lighting,
scorchmarks and intersection tests.
*/



class TerrainTessMask;

class HeightMapRenderObjClass : public BaseHeightMapRenderObjClass
{	

public:

	HeightMapRenderObjClass(void);
	virtual ~HeightMapRenderObjClass(void) override;

	// RenderCleanupHook methods
	virtual void ReleaseResources(void) override;	///< Release all device resources so the device can be reset.
	virtual void ReAcquireResources(void) override;  ///< Reacquire all resources after device reset.


	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface (W3D methods)
	/////////////////////////////////////////////////////////////////////////////
	virtual void					Render(RenderInfoClass & rinfo) override;
	virtual void					On_Frame_Update(void) override; 

	///allocate resources needed to render heightmap
	virtual int initHeightData(Int width, Int height, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator, Bool updateExtraPassTiles=TRUE) override;
	virtual Int freeMapResources(void) override;	///< free resources used to render heightmap
	virtual void updateCenter(CameraClass *camera, RefRenderObjListIterator *pLightsIterator) override;

	void renderExtraBlendTiles(void);			///< render 3-way blend tiles that have blend of 3 textures.


	virtual void staticLightingChanged(void) override;
	virtual void heightsChanged(const IRegion2D &range) override;
	virtual	void adjustTerrainLOD(Int adj) override;
	virtual void reset(void) override;
	virtual void doPartialUpdate(const IRegion2D &partialRange, WorldHeightMap *htMap, RefRenderObjListIterator *pLightsIterator) override;

	virtual void oversizeTerrain(Int tilesToOversize) override;
  
	virtual int updateBlock(Int x0, Int y0, Int x1, Int y1, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator) override;

protected:
	Int *m_extraBlendTilePositions;	///<array holding x,y tile positions of all extra blend tiles. (used for 3 textures per tile).
	Int m_numExtraBlendTiles;		///<number of blend tiles in m_extraBlendTilePositions.
	Int	m_numVisibleExtraBlendTiles; ///<number rendered last frame.	
	Int m_extraBlendTilePositionsSize;	//<total size of array including unused memory.
	DeviceVertexBufferClass	**m_vertexBufferTiles;	///<collection of smaller vertex buffers that make up 1 heightmap
	char	**m_vertexBufferBackup;	///< In memory copy of the vertex buffer data for quick update of dynamic lighting.
	AABoxClass *m_tileBounds;	///< world bounds of each vertex buffer tile, for culling
	Bool *m_tileBoundsDirty;	///< the tile was refilled since its bounds were computed
	Int m_originX; ///<  Origin point in the grid.  Slides around.
	Int m_originY; ///< Origin point in the grid.  Slides around.
	DeviceIndexBufferClass			*m_indexBuffer;	///<indices defining triangles in a VB tile.
	Int	m_numVBTilesX;	///<dimensions of array containing all the vertex buffers 
	Int	m_numVBTilesY;	///<dimensions of array containing all the vertex buffers
	Int m_numVertexBufferTiles;	///<number of vertex buffers needed to store this heightmap
	Int	m_numBlockColumnsInLastVB;///<a VB tile may be partially filled, this indicates how many 2x2 vertex blocks are filled.
	Int	m_numBlockRowsInLastVB;///<a VB tile may be partially filled, this indicates how many 2x2 vertex blocks are filled.
	Bool m_gpuDynamicLights;	///< dynamic lights are applied by the vertex shader, the vertex buffers keep the prelit colors
	Bool m_gpuStaticLighting;	///< the tile vertex buffers hold white, the vertex shader computes the prelighting
	Bool m_gpuStaticLightingWanted;	///< mode for the next full rebuild of the tile vertex buffers
	RenderTerrainStaticLighting m_staticLighting;	///< prelighting for the vertex shader, gathered each frame
	TerrainTessMask *m_tessMask = nullptr;	///< displacement mask of the terrain tessellation (only with the effect)

	Bool canLightOnGpu(void);	///< the vertex shader can compute the prelighting (device, switches, debug views)
	Bool isTileVisible(Int tile, CameraClass *camera);	///< tile bounds against the camera frustum
	Bool gatherStaticLighting(void);	///< fills m_staticLighting; false if the vertex shader cannot do it


	UnsignedInt doTheDynamicLight(VERTEX_FORMAT *vb, VERTEX_FORMAT *vbMirror, Vector3*light, Vector3*normal, W3DDynamicLight *pLights[], Int numLights);
	Int getXWithOrigin(Int x);
	Int getYWithOrigin(Int x);
	///update vertex diffuse color for dynamic lights inside given rectangle
	Int updateVBForLight(DeviceVertexBufferClass *pVB, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, W3DDynamicLight *pLights[], Int numLights);
	Int updateVBForLightOptimized(DeviceVertexBufferClass	*pVB, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, W3DDynamicLight *pLights[], Int numLights);
	///update vertex buffer vertices inside given rectangle
	Int updateVB(DeviceVertexBufferClass	*pVB, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator);
	///fill the rectangle into the backup and the locked vertex buffer (vbHardware points at row 0 of the tile)
	void fillVB(VERTEX_FORMAT *vbHardware, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator);
	///upate vertex buffers associated with the given rectangle
	void initDestAlphaLUT(void);	///<initialize water depth LUT stored in m_destAlphaTexture
	void renderTerrainPass(CameraClass *pCamera, Bool cullTiles);	///< renders additional terrain pass (cullTiles: skip tiles outside the view)
	Int	getNumExtraBlendTiles(Bool visible) override { return visible?m_numVisibleExtraBlendTiles:m_numExtraBlendTiles;}
	void freeIndexVertexBuffers(void);


  
};

#endif  // end __HEIGHTMAP_H_
