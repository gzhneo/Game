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

// FILE: Heightmap.cpp ////////////////////////////////////////////////
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
// File name: Heightmap.cpp
//
// Created:   Mark W., John Ahlquist, April/May 2001
//
// Desc:      Draw the terrain and scorchmarks in a scene.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes                                                      
//-----------------------------------------------------------------------------


#include "W3DDevice/GameClient/HeightMap.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assetmgr.h>
#include <texture.h>
#include <tri.h>
#include <colmath.h>
#include <coltest.h>
#include <rinfo.h>
#include <camera.h>
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Engine/JobSystem.h"

#include <cfloat>
#include <memory>
#include <vector>

#include "GameClient/TerrainVisual.h"
#include "GameClient/View.h"
#include "GameClient/Water.h"

#include "GameLogic/AIPathfind.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/TerrainTessMask.h"
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/W3DBibBuffer.h"
#include "W3DDevice/GameClient/W3DTreeBuffer.h"
#include "W3DDevice/GameClient/W3DPropBuffer.h"
#include "W3DDevice/GameClient/W3DRoadBuffer.h"
#include "W3DDevice/GameClient/W3DBridgeBuffer.h"
#include "W3DDevice/GameClient/W3DWaypointBuffer.h"
#include "W3DDevice/GameClient/W3DCustomEdging.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "WW3D2/Common/renderdevice.h"
#include "WW3D2/Scene/light.h"
#include "WW3D2/Scene/scene.h"
#include "W3DDevice/GameClient/W3DPoly.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"

#include "Common/PerfTimer.h"

#define no_OPTIMIZED_HEIGHTMAP_LIGHTING	01
// Doesn't work well.  jba.

const Bool HALF_RES_MESH = false;

//-----------------------------------------------------------------------------
//         Private Data                                                     
//-----------------------------------------------------------------------------
#define SC_DETAIL_BLEND ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailOpaqueShader(SC_DETAIL_BLEND);

constexpr auto DEFAULT_MAX_FRAME_EXTRABLEND_TILES = 256;	//default number of terrain tiles rendered per call (must fit in one VB)
constexpr auto DEFAULT_MAX_MAP_EXTRABLEND_TILES = 2048;	//default size of array allocated to hold all map extra blend tiles.
#define DEFAULT_MAX_BATCH_SHORELINE_TILES		512	//maximum number of terrain tiles rendered per call (must fit in one VB)
#define DEFAULT_MAX_MAP_SHORELINE_TILES		4096	//default size of array allocated to hold all map shoreline tiles.

#define ADJUST_FROM_INDEX_TO_REAL(k) ((k-m_map->getBorderSizeInline())*MAP_XY_FACTOR)
inline Int IABS(Int x) {	if (x>=0) return x; return -x;};

//-----------------------------------------------------------------------------
//         Private Functions                                               
//-----------------------------------------------------------------------------

//=============================================================================
// HeightMapRenderObjClass::freeIndexVertexBuffers
//=============================================================================
/** Frees the w3d resources used to draw the terrain. */
//=============================================================================
void HeightMapRenderObjClass::freeIndexVertexBuffers(void)
{
	REF_PTR_RELEASE(m_indexBuffer);
	if (m_vertexBufferTiles) {
		for (int i=0; i<m_numVertexBufferTiles; i++)
			REF_PTR_RELEASE(m_vertexBufferTiles[i]);
		delete[] m_vertexBufferTiles;
		m_vertexBufferTiles = nullptr;
	}
	if (m_vertexBufferBackup) {
		for (int i=0; i<m_numVertexBufferTiles; i++)
			delete[] m_vertexBufferBackup[i];
		delete[] m_vertexBufferBackup;
		m_vertexBufferBackup = nullptr;
	}
	delete[] m_tileBounds;
	m_tileBounds = nullptr;
	delete[] m_tileBoundsDirty;
	m_tileBoundsDirty = nullptr;
	m_numVertexBufferTiles = 0;

}

//=============================================================================
// HeightMapRenderObjClass::freeMapResources
//=============================================================================
/** Frees the w3d resources used to draw the terrain. */
//=============================================================================
Int HeightMapRenderObjClass::freeMapResources(void)
{
	BaseHeightMapRenderObjClass::freeMapResources();
	freeIndexVertexBuffers();
	delete m_tessMask;
	m_tessMask = nullptr;

	return 0;
}


//=============================================================================
// HeightMapRenderObjClass::doTheDynamicLight
//=============================================================================
/** Calculates the diffuse lighting as affected by dynamic lighting. */
//=============================================================================
UnsignedInt HeightMapRenderObjClass::doTheDynamicLight(VERTEX_FORMAT *vb, VERTEX_FORMAT *vbMirror, Vector3*light, Vector3*normal,  W3DDynamicLight *pLights[], Int numLights)
{
#ifdef USE_NORMALS
	return;
#else
	Real shadeR, shadeG, shadeB;
	Int diffuse = vbMirror->diffuse;
#ifdef _DEBUG
	//vbMirror->diffuse += 30;	// Shows which vertexes are geting touched by dynamic light. debug only.
#endif
	
	// (gth) avoiding the extra divides (compiler unfortunately didn't do this automatically...)
	const float oo255 = (1.0f/255.0f);  
	shadeR = ((diffuse>>16)&0x00FF) * oo255;
	shadeG = ((diffuse>>8)&0x00FF) * oo255;
	shadeB = (diffuse&0x00FF) * oo255;

	Int alpha = (diffuse>>24)&0x00FF;
	Int k;
	for (k=0; k<numLights; k++) {
		W3DDynamicLight *pLight = pLights[k];
		if (!pLight->isEnabled()) {
			continue; // he is turned off.
		}
		Vector3 lightDirection(vbMirror->x, vbMirror->y, vbMirror->z);
		Real factor = 1.0f;
		switch(pLight->Get_Type()) {	  
		case LightClass::POINT:
		case LightClass::SPOT: {
				Vector3 lightLoc = pLight->Get_Position();
				lightDirection -= lightLoc;
				double range, midRange;
				pLight->Get_Far_Attenuation_Range(midRange, range);
				Real dist = lightDirection.Length();
				if (dist >= range) continue;
				if (midRange < 0.1) continue;
				factor = static_cast<Real>(1.0f - (dist - midRange) / (range - midRange));
				factor = WWMath::Clamp(factor,0.0f,1.0f);

				// (gth) normalize here since we have the length
				lightDirection /= dist;
			} 
			break;
		case LightClass::DIRECTIONAL:
			pLight->Get_Spot_Direction(lightDirection);
			factor = 1.0;
			break;
		};
		
		// (gth) unneeded due to above normalization
		//lightDirection.Normalize();

		Vector3 lightRay(-lightDirection.X, -lightDirection.Y, -lightDirection.Z);
		Real shade = Vector3::Dot_Product(lightRay, *normal); 
		shade *= factor;
		Vector3 diffuse2;
		pLight->Get_Diffuse(&diffuse2);
		Vector3 ambient;
		pLight->Get_Ambient(&ambient);
		if (shade > 1.0) shade = 1.0;
		if(shade < 0.0f) shade = 0.0f;
		shadeR += shade*diffuse2.X;
		shadeG += shade*diffuse2.Y;
		shadeB += shade*diffuse2.Z;		
		shadeR += factor*ambient.X;
		shadeG += factor*ambient.Y;
		shadeB += factor*ambient.Z;		

	} 
	if (shadeR > 1.0) shadeR = 1.0;
	if(shadeR < 0.0f) shadeR = 0.0f;
	if (shadeG > 1.0) shadeG = 1.0;
	if(shadeG < 0.0f) shadeG = 0.0f;
	if (shadeB > 1.0) shadeB = 1.0;
	if(shadeB < 0.0f) shadeB = 0.0f;
	shadeR*=255.0f;
	shadeG*=255.0f;
	shadeB*=255.0f;

//	(gth) faster float to int conversion, return the result so we can re-use it.
//	vb->diffuse=REAL_TO_INT(shadeB) | (REAL_TO_INT(shadeG) << 8) | (REAL_TO_INT(shadeR) << 16) | ((int)alpha << 24);
	UnsignedInt light_val = WWMath::Float_To_Int_Chop(shadeB) | (WWMath::Float_To_Int_Chop(shadeG) << 8) | (WWMath::Float_To_Int_Chop(shadeR) << 16) | (static_cast<int>(alpha) << 24);
	vb->diffuse = light_val;
	return light_val;

#endif
}

//=============================================================================
// HeightMapRenderObjClass::getXWithOrigin
//=============================================================================
/** Gets the x index that corresponds to the data.  For example, if the columns
are shifted by 3, index 3 is actually the first row of polygons, or 0.  Yes it
is confusing, but it makes sliding the map 10x faster.  */
//=============================================================================
Int HeightMapRenderObjClass::getXWithOrigin(Int x)
{
	x -= m_originX;
	if (x<0) x+= m_x-1;
	if (x>= m_x-1) x-=m_x-1;
#ifdef _DEBUG
	DEBUG_ASSERTCRASH (x>=0, ("X out of range."));
	DEBUG_ASSERTCRASH (x<m_x-1, ("X out of range."));
#endif
	if (x<0) x = 0;
	if (x>= m_x-1) x=m_x-1;
	return x;
}

//=============================================================================
// HeightMapRenderObjClass::getYWithOrigin
//=============================================================================
/** Gets the y index that corresponds to the data.  For example, if the rows
are shifted by 3, index 3 is actually the first row of polygons, or 0.  Yes it
is confusing, but it makes sliding the map 10x faster.  */
//=============================================================================
Int HeightMapRenderObjClass::getYWithOrigin(Int y)
{
	y -= m_originY;
	if (y<0) y+= m_y-1;
	if (y>= m_y-1) y-=m_y-1;
#ifdef _DEBUG
	DEBUG_ASSERTCRASH (y>=0, ("Y out of range."));
	DEBUG_ASSERTCRASH (y<m_y-1, ("Y out of range."));
#endif
	if (y<0) y = 0;
	if (y>= m_y-1) y=m_y-1;
	return y;
}

// Only rows y0..y1-1 of a tile are written; locking just those makes the unlock upload only them.
static DeviceVertexBufferClass::AppendLockClass *lockTileRows(DeviceVertexBufferClass *pVB, Int y0, Int y1, Int originY, VERTEX_FORMAT **vbHardware)
{
	const Int vertsPerRow = VERTEX_BUFFER_TILE_LENGTH*4;
	Int firstRow = y0-originY;
	Int endRow = y1-originY;
	if (HALF_RES_MESH) {
		firstRow = 0;
		endRow = VERTEX_BUFFER_TILE_LENGTH;
	}
	DeviceVertexBufferClass::AppendLockClass *lock = NEW DeviceVertexBufferClass::AppendLockClass(pVB, firstRow*vertsPerRow, (endRow-firstRow)*vertsPerRow);
	*vbHardware = static_cast<VERTEX_FORMAT*>(lock->Get_Vertex_Array()) - firstRow*vertsPerRow;
	return lock;
}

//=============================================================================
// HeightMapRenderObjClass::updateVB
//=============================================================================
/** Update a rectangular block of the given Vertex Buffer. 
data is expected to be an array same dimensions as current heightmap
mapped into this VB.
*/
//=============================================================================
Int HeightMapRenderObjClass::updateVB(DeviceVertexBufferClass	*pVB, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator)
{
	REF_PTR_SET(m_map, pMap);	//update our heightmap pointer in case it changed since last call.
	if (m_vertexBufferTiles && pMap)
	{
		VERTEX_FORMAT *vbHardware;
		std::unique_ptr<DeviceVertexBufferClass::AppendLockClass> lock(lockTileRows(pVB, y0, y1, originY, &vbHardware));
		fillVB(vbHardware, data, x0, y0, x1, y1, originX, originY, pMap, pLightsIterator);
		return 0; //success.
	}
	return -1;
}

// Reads only the height map and the window placement and writes only this tile's backup and vertex buffer, so
// different tiles can be filled on different threads -- except with CPU lighting (pLightsIterator is shared).
void HeightMapRenderObjClass::fillVB(VERTEX_FORMAT *vbHardware, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator)
{
	Int i,j;
	Vector3 lightRay[MAX_GLOBAL_LIGHTS];
	const Coord3D *lightPos;
	Int xCoord, yCoord;
	Int vn0,un0,vp1,up1;
	Vector3 l2r,n2f,normalAtTexel;
	Int	vertsPerRow=(VERTEX_BUFFER_TILE_LENGTH)*4;	//vertices per row of VB

	Int cellOffset = 1;
	if (HALF_RES_MESH) {
		cellOffset = 2;
	}

	{
#ifdef _DEBUG
		assert(x0 >= originX && y0 >= originY && x1>x0 && y1>y0 && x1<=originX+VERTEX_BUFFER_TILE_LENGTH && y1<=originY+VERTEX_BUFFER_TILE_LENGTH);
#endif 

		VERTEX_FORMAT *vBase = reinterpret_cast<VERTEX_FORMAT*>(data);
		// Note that we are building the vertex buffer data in the memory buffer, data.
		// At the bottom, we will copy the final vertex data for one cell into the 
		// hardware vertex buffer. 
		
		for (j=y0; j<y1; j++)
		{
			VERTEX_FORMAT *vb = vBase;
			if (HALF_RES_MESH) {
				if (j&1) continue;
				vb += ((j-originY)/2)*vertsPerRow/2;	//skip to correct row in vertex buffer
				vb += ((x0-originX)/2)*4;		//skip to correct vertex in row.
			} else {
				vb += (j-originY)*vertsPerRow;	//skip to correct row in vertex buffer
				vb += (x0-originX)*4;		//skip to correct vertex in row.
			}
			vn0 = getYWithOrigin(j)-cellOffset;
			if (vn0 < -pMap->getDrawOrgY())
				vn0=-pMap->getDrawOrgY();
			vp1 = getYWithOrigin(j+cellOffset)+cellOffset;
			if (vp1 >= pMap->getYExtent()-pMap->getDrawOrgY())
				vp1=pMap->getYExtent()-pMap->getDrawOrgY()-1;

			yCoord = getYWithOrigin(j)+pMap->getDrawOrgY();
			for (i=x0; i<x1; i++)
			{
				if (HALF_RES_MESH) {
					if (i&1) continue;
				}
				un0 = getXWithOrigin(i)-cellOffset;
				if (un0 < -pMap->getDrawOrgX())
					un0=-pMap->getDrawOrgX();
				up1 = getXWithOrigin(i+cellOffset)+cellOffset;
				if (up1 >= pMap->getXExtent()-pMap->getDrawOrgX())
					up1=pMap->getXExtent()-pMap->getDrawOrgX()-1;
				xCoord = getXWithOrigin(i)+pMap->getDrawOrgX();

				//update the 4 vertices in this block
				float U[4], V[4];
				UnsignedByte alpha[4];
				float UA[4], VA[4];
				Bool flipForBlend = false;			 // True if the blend needs the triangles flipped.

				if (pMap) {
					pMap->getUVData(getXWithOrigin(i),getYWithOrigin(j),U, V, HALF_RES_MESH);
					pMap->getAlphaUVData(getXWithOrigin(i),getYWithOrigin(j), UA, VA, alpha, &flipForBlend, HALF_RES_MESH);
				} 


				// With static lighting in the vertex shader the colors are white and no normal is needed.
				const Bool cpuLight = !m_gpuStaticLighting;
				if (cpuLight) {
					for (Int lightIndex=0; lightIndex < TheGlobalData->m_numGlobalLights; lightIndex++)
					{
						lightPos=&TheGlobalData->m_terrainLightPos[lightIndex];
						lightRay[lightIndex].Set(-lightPos->x,-lightPos->y,	-lightPos->z);
					}
				}

				//top-left sample
				if (cpuLight) {
					l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset, getYWithOrigin(j)) - pMap->getDisplayHeight(un0, getYWithOrigin(j))));
					n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i), (getYWithOrigin(j)+cellOffset)) - pMap->getDisplayHeight(getXWithOrigin(i), vn0)));
					Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
				}

				vb->x=static_cast<float>(xCoord);
				vb->y=static_cast<float>(yCoord);
				vb->z=  (static_cast<float>(pMap->getDisplayHeight(getXWithOrigin(i), getYWithOrigin(j))))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[0];
				vb->v1=V[0];
				vb->u2=UA[0];
				vb->v2=VA[0];
				if (cpuLight) {
					doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[0]);
				} else {
					vb->diffuse = (static_cast<UnsignedInt>(alpha[0]) << 24) | 0x00FFFFFF;
				}
				vb++;

				//top-right sample
				if (cpuLight) {
					l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(up1 , getYWithOrigin(j) ) - pMap->getDisplayHeight(getXWithOrigin(i) , getYWithOrigin(j) )));
					n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset , (getYWithOrigin(j)+cellOffset) ) - pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset , vn0 )));
					Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
				}

				vb->x=static_cast<float>(xCoord+cellOffset);
				vb->y=static_cast<float>(yCoord);
				vb->z=  (static_cast<float>(pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset, getYWithOrigin(j))))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[1];
				vb->v1=V[1];
				vb->u2=UA[1];
				vb->v2=VA[1];
				if (cpuLight) {
					doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[1]);
				} else {
					vb->diffuse = (static_cast<UnsignedInt>(alpha[1]) << 24) | 0x00FFFFFF;
				}
				vb++;

				//bottom-right sample
				if (cpuLight) {
					l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(up1 , (getYWithOrigin(j)+cellOffset) ) - pMap->getDisplayHeight(getXWithOrigin(i) , (getYWithOrigin(j)+cellOffset) )));
					n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset , vp1 ) - pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset , getYWithOrigin(j) )));
					Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
				}

				vb->x=static_cast<float>(xCoord+cellOffset);
				if (yCoord + 1 == pMap->getDrawOrgY() + m_y - 1) { 
					vb->y=static_cast<float>(yCoord+1);
				} else {
					vb->y=static_cast<float>(yCoord+cellOffset);
				}
				vb->z=  (static_cast<float>(pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset, getYWithOrigin(j)+cellOffset)))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[2];
				vb->v1=V[2];
				vb->u2=UA[2];
				vb->v2=VA[2];
				if (cpuLight) {
					doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[2]);
				} else {
					vb->diffuse = (static_cast<UnsignedInt>(alpha[2]) << 24) | 0x00FFFFFF;
				}
				vb++;

				//bottom-left sample
				if (cpuLight) {
					l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i)+cellOffset , (getYWithOrigin(j)+cellOffset) ) - pMap->getDisplayHeight(un0 , (getYWithOrigin(j)+cellOffset) )));
					n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(pMap->getDisplayHeight(getXWithOrigin(i) , vp1 ) - pMap->getDisplayHeight(getXWithOrigin(i) , getYWithOrigin(j) )));
					Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
				}

				if (xCoord == pMap->getDrawOrgX()) { 
					vb->x=static_cast<float>(xCoord);
					//if (vb->x < 0) vb->x = 0;
				} else {
					vb->x=static_cast<float>(xCoord);
				}
				if (yCoord + 1 == pMap->getDrawOrgY() + m_y - 1) { 
					vb->y=static_cast<float>(yCoord+1);
				} else {
					vb->y=static_cast<float>(yCoord+cellOffset);
				}
				vb->z=  (static_cast<float>(pMap->getDisplayHeight(getXWithOrigin(i), getYWithOrigin(j)+cellOffset)))*MAP_HEIGHT_SCALE;
				vb->x = ADJUST_FROM_INDEX_TO_REAL(vb->x);
				vb->y = ADJUST_FROM_INDEX_TO_REAL(vb->y);
				vb->u1=U[3];
				vb->v1=V[3];
				vb->u2=UA[3];
				vb->v2=VA[3];
				if (cpuLight) {
					doTheLight(vb, lightRay, &normalAtTexel, pLightsIterator, alpha[3]);
				} else {
					vb->diffuse = (static_cast<UnsignedInt>(alpha[3]) << 24) | 0x00FFFFFF;
				}
				vb++;

				VERTEX_FORMAT *pCurVertices = vb-4;
#ifdef FLIP_TRIANGLES // jba - reduces "diamonding" in some cases, not others.  Better cliffs, though.
				VERTEX_FORMAT tmpVertex;
				if (flipForBlend) {
					tmpVertex = pCurVertices[0];
					pCurVertices[0] = pCurVertices[1];
					pCurVertices[1] = pCurVertices[2];
					pCurVertices[2] = pCurVertices[3];
					pCurVertices[3] = tmpVertex;
				}
#endif

				if (m_showImpassableAreas) {
					// Color impassable cells "red"
					DEBUG_ASSERTCRASH(PATHFIND_CELL_SIZE_F == MAP_XY_FACTOR, ("Pathfind must be terrain cell size, or this code needs reworking.  John A."));
					Real borderHiX = (pMap->getXExtent()-2*pMap->getBorderSizeInline())*MAP_XY_FACTOR;
					Real borderHiY = (pMap->getYExtent()-2*pMap->getBorderSizeInline())*MAP_XY_FACTOR;
					Bool border = pCurVertices[0].x == -MAP_XY_FACTOR || pCurVertices[0].y == -MAP_XY_FACTOR;
					Bool cliffMapped = pMap->isCliffMappedTexture(getXWithOrigin(i), getYWithOrigin(j));
					if (pCurVertices[0].x == borderHiX) {
						border = true;
					}
					if (pCurVertices[0].y == borderHiY) {
						border = true;
					}
					Bool isCliff = pMap->getCliffState(getXWithOrigin(i)+pMap->getDrawOrgX(), getYWithOrigin(j)+pMap->getDrawOrgY())
												 || showAsVisibleCliff(getXWithOrigin(i) + pMap->getDrawOrgX(), getYWithOrigin(j)+pMap->getDrawOrgY());

					if ( isCliff || border || cliffMapped) {
						Int cellX, cellY;
						for (cellX=0; cellX<2; cellX++) {
							for (cellY=0; cellY<2; cellY++) {
								Int vertex = cellX+2*cellY;
								if (border) {
									Bool doBorder = false;
									if (pCurVertices[vertex].y >= 0 && pCurVertices[vertex].y <= borderHiY) {
										if (pCurVertices[vertex].x == 0 || pCurVertices[vertex].x == borderHiX) {
											doBorder = true;
										}
									}
									if (pCurVertices[vertex].x >= 0 && pCurVertices[vertex].x <= borderHiX) {
										if (pCurVertices[vertex].y == 0 || pCurVertices[vertex].y == borderHiY) {
											doBorder = true;
										}
									}
									if (doBorder) {
										pCurVertices[vertex].diffuse &= 0xFF0000ff; // blue with alpha.
									}
								} else if (isCliff) {
									pCurVertices[vertex].diffuse &= 0xFFFF0000; // red with alpha.
								}
								if (cliffMapped && vertex==0) {
									pCurVertices[vertex].diffuse &= 0xFF000000; // Black.
									pCurVertices[vertex].diffuse |= 0xff00; // Add green.
								}
							}
						}
					}
				}

				// Note - We have been building the vertex buffer in the memory location.
				// Now copy the set of vertices into the hardware buffer.
				// We don't copy the whole vertex buffer because we often update only
				// a couple of rows and its a lot faster to just copy the ones that change.
				Int offset = static_cast<Int>(pCurVertices - vBase);
				memcpy(vbHardware+offset, pCurVertices, 4*sizeof(VERTEX_FORMAT));
			}
		}
	}
}

//=============================================================================
// HeightMapRenderObjClass::updateVBForLight
//=============================================================================
/** Update the dynamic lighting values only in a rectangular block of the given Vertex Buffer. 
The vertex locations and texture coords are unchanged.
*/
Int HeightMapRenderObjClass::updateVBForLight(DeviceVertexBufferClass	*pVB, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, W3DDynamicLight *pLights[], Int numLights)
{

#if (OPTIMIZED_HEIGHTMAP_LIGHTING)	// (gth) if optimizations are enabled, jump over to the "optimized" version of this function.
	return updateVBForLightOptimized( pVB, data, x0, y0, x1, y1, originX, originY, pLights, numLights );
#endif	
	
	Int i,j,k;
	Int vn0,un0,vp1,up1;
	Vector3 l2r,n2f,normalAtTexel;
	Int	vertsPerRow=(VERTEX_BUFFER_TILE_LENGTH)*4;	//vertices per row of VB

	if (m_vertexBufferTiles && m_map)
	{
#ifdef _DEBUG
		assert(x0 >= originX && y0 >= originY && x1>x0 && y1>y0 && x1<=originX+VERTEX_BUFFER_TILE_LENGTH && y1<=originY+VERTEX_BUFFER_TILE_LENGTH);
#endif 

		auto rowIsLit = [&](Int row) {
			Int yCoord = getYWithOrigin(row)+m_map->getDrawOrgY()-m_map->getBorderSizeInline();
			for (Int n=0; n<numLights; n++) {
				if (pLights[n]->m_minY <= yCoord+1 && pLights[n]->m_maxY >= yCoord) {
					return true;
				}
				if (pLights[n]->m_prevMinY <= yCoord+1 && pLights[n]->m_prevMaxY >= yCoord) {
					return true;
				}
			}
			return false;
		};

		// Only vertices of lit rows are written. Lock just the span from the first to the last lit
		// row: the tile buffer is static, and its unlock uploads exactly the locked range.
		Int firstRow = VERTEX_BUFFER_TILE_LENGTH;
		Int endRow = 0;
		for (j=y0; j<y1; j++) {
			if (HALF_RES_MESH && (j&1)) continue;
			if (rowIsLit(j)) {
				if (firstRow > j-originY) firstRow = j-originY;
				endRow = j-originY+1;
			}
		}
		if (firstRow >= endRow) {
			return 0;
		}
		if (HALF_RES_MESH) {
			firstRow = 0;
			endRow = VERTEX_BUFFER_TILE_LENGTH;
		}
		DeviceVertexBufferClass::AppendLockClass lockVtxBuffer(pVB, firstRow*vertsPerRow, (endRow-firstRow)*vertsPerRow);
		VERTEX_FORMAT *vBase = static_cast<VERTEX_FORMAT*>(lockVtxBuffer.Get_Vertex_Array()) - firstRow*vertsPerRow;
		VERTEX_FORMAT *vb;
		
		for (j=y0; j<y1; j++)
		{
			if (HALF_RES_MESH) {
				if (j&1) continue;
			}
			Int yCoord = getYWithOrigin(j)+m_map->getDrawOrgY()-m_map->getBorderSizeInline();
			if (!rowIsLit(j)) {
				continue;
			}
			vn0 = getYWithOrigin(j)-1;
			if (vn0 < -m_map->getDrawOrgY())
				vn0=-m_map->getDrawOrgY();
			vp1 = getYWithOrigin(j+1)+1;
			if (vp1 >= m_map->getYExtent()-m_map->getDrawOrgY())
				vp1=m_map->getYExtent()-m_map->getDrawOrgY()-1;

			for (i=x0; i<x1; i++)
			{
				if (HALF_RES_MESH) {
					if (i&1) continue;
				}
				Int xCoord = getXWithOrigin(i)+m_map->getDrawOrgX()-m_map->getBorderSizeInline();
				Bool intersect4 = false;
				for (k=0; k<numLights; k++) {
					if (pLights[k]->m_minX <= xCoord+1 && 
						pLights[k]->m_maxX >= xCoord &&
						pLights[k]->m_minY <= yCoord+1 && 
						pLights[k]->m_maxY >= yCoord) {
						intersect4 = true;
					}
					if (pLights[k]->m_prevMinX <= xCoord+1 && 
						pLights[k]->m_prevMaxX >= xCoord &&
						pLights[k]->m_prevMinY <= yCoord+1 && 
						pLights[k]->m_prevMaxY >= yCoord) {
						intersect4 = true;
					}
				}
				if (!intersect4) {
					continue;
				}
				// vb is the pointer to the vertex in the hardware vertex buffer.
				Int offset = (j-originY)*vertsPerRow+4*(i-originX);
				if (HALF_RES_MESH) {
					offset = (j-originY)*vertsPerRow/4+2*(i-originX);
				}
				vb = vBase + offset;	//skip to correct row in vertex buffer
				// vbMirror is the pointer to the vertex in our memory based copy.
				// The important point is that we can read out of our copy to get the original
				// diffuse color, and xyz location.  It is VERY SLOW to read out of the 
				// hardware vertex buffer, possibly worse... jba.
				VERTEX_FORMAT *vbMirror = (reinterpret_cast<VERTEX_FORMAT*>(data))  + offset;
				un0 = getXWithOrigin(i)-1;
				if (un0 < -m_map->getDrawOrgX())
					un0=-m_map->getDrawOrgX();
				up1 = getXWithOrigin(i+1)+1;
				if (up1 >= m_map->getXExtent()-m_map->getDrawOrgX())
					up1=m_map->getXExtent()-m_map->getDrawOrgX()-1;

				Vector3 lightRay(0,0,0);

				//top-left sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1, getYWithOrigin(j)) - m_map->getDisplayHeight(un0, getYWithOrigin(j))));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i), (getYWithOrigin(j)+1)) - m_map->getDisplayHeight(getXWithOrigin(i), vn0)));
				
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				vb++;	vbMirror++;

				//top-right sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(up1 , getYWithOrigin(j) ) - m_map->getDisplayHeight(getXWithOrigin(i) , getYWithOrigin(j) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1 , (getYWithOrigin(j)+1) ) - m_map->getDisplayHeight(getXWithOrigin(i)+1 , vn0 )));
				
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				vb++;	vbMirror++;

				//bottom-right sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(up1 , (getYWithOrigin(j)+1) ) - m_map->getDisplayHeight(getXWithOrigin(i) , (getYWithOrigin(j)+1) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1 , vp1 ) - m_map->getDisplayHeight(getXWithOrigin(i)+1 , getYWithOrigin(j) )));
				
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				vb++;	vbMirror++;

				//bottom-left sample
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1 , (getYWithOrigin(j)+1) ) - m_map->getDisplayHeight(un0 , (getYWithOrigin(j)+1) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i) , vp1 ) - m_map->getDisplayHeight(getXWithOrigin(i) , getYWithOrigin(j) )));
				
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);

				doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				vb++;	vbMirror++;
			}
		}
		return 0; //success.
	}
	return -1;
}


Int HeightMapRenderObjClass::updateVBForLightOptimized(DeviceVertexBufferClass	*pVB, char *data, Int x0, Int y0, Int x1, Int y1, Int originX, Int originY, W3DDynamicLight *pLights[], Int numLights)
{
	Int i,j,k;
	Int vn0,un0,vp1,up1;
	Vector3 l2r,n2f,normalAtTexel;
	Int	vertsPerRow=(VERTEX_BUFFER_TILE_LENGTH)*4;	//vertices per row of VB

	if (m_vertexBufferTiles && m_map)
	{
#ifdef _DEBUG
		assert(x0 >= originX && y0 >= originY && x1>x0 && y1>y0 && x1<=originX+VERTEX_BUFFER_TILE_LENGTH && y1<=originY+VERTEX_BUFFER_TILE_LENGTH);
#endif 

		DeviceVertexBufferClass::WriteLockClass lockVtxBuffer(pVB);
		VERTEX_FORMAT *vBase = static_cast<VERTEX_FORMAT*>(lockVtxBuffer.Get_Vertex_Array());
		VERTEX_FORMAT *vb;
		
		//
		// (gth) the optimization in this function is to take advantage of verts in the same
		// x,y position who have already computed their lighting.  To do this, we need to set up
		// some offsets in the vertex buffer.  I've computed these offsets to be consistent with
		// the formula's that Generals is using but in the case of the "half-res-mesh" I'm not
		// sure things are correct...
		//
		Int quad_right_offset;
		Int quad_below_offset;
		Int quad_below_right_offset;

		if (HALF_RES_MESH == false) {
			// offset = (j-originY)*vertsPerRow+4*(i-originX);
			quad_right_offset = 4;
			quad_below_offset = vertsPerRow;
			quad_below_right_offset = vertsPerRow + 4;

		} else {
			// offset = (j-originY)*vertsPerRow/4+2*(i-originX);
			quad_right_offset = 2;
			quad_below_offset = vertsPerRow/4;
			quad_below_right_offset = vertsPerRow/4 + 2;
		}

		// 
		// i,j loop over the quads affected by the light.  Each quad has its *own* 4 vertices.  This
		// means that for any vertex position on the map, there are actually 4 copies of the vertex. 
		//
		for (j=y0; j<y1; j++)
		{
			if (HALF_RES_MESH) {
				if (j&1) continue;
			}
			Int yCoord = getYWithOrigin(j)+m_map->getDrawOrgY()-m_map->getBorderSizeInline();
			Bool intersect = false;
			for (k=0; k<numLights; k++) {
				if (pLights[k]->m_minY <= yCoord+1 && 
					pLights[k]->m_maxY >= yCoord) {
					intersect = true;
				}
				if (pLights[k]->m_prevMinY <= yCoord+1 && 
					pLights[k]->m_prevMaxY >= yCoord) {
					intersect = true;
				}
			}
			if (!intersect) {
				continue;
			}
			vn0 = getYWithOrigin(j)-1;
			if (vn0 < -m_map->getDrawOrgY())
				vn0=-m_map->getDrawOrgY();
			vp1 = getYWithOrigin(j+1)+1;
			if (vp1 >= m_map->getYExtent()-m_map->getDrawOrgY())
				vp1=m_map->getYExtent()-m_map->getDrawOrgY()-1;

			for (i=x0; i<x1; i++)
			{
				if (HALF_RES_MESH) {
					if (i&1) continue;
				}
				Int xCoord = getXWithOrigin(i)+m_map->getDrawOrgX()-m_map->getBorderSizeInline();
				Bool intersect3 = false;
				for (k=0; k<numLights; k++) {
					if (pLights[k]->m_minX <= xCoord+1 && 
						pLights[k]->m_maxX >= xCoord &&
						pLights[k]->m_minY <= yCoord+1 && 
						pLights[k]->m_maxY >= yCoord) {
						intersect3 = true;
					}
					if (pLights[k]->m_prevMinX <= xCoord+1 && 
						pLights[k]->m_prevMaxX >= xCoord &&
						pLights[k]->m_prevMinY <= yCoord+1 && 
						pLights[k]->m_prevMaxY >= yCoord) {
						intersect3 = true;
					}
				}
				if (!intersect3) {
					continue;
				}
				// vb is the pointer to the vertex in the hardware vertex buffer.
				Int offset = (j-originY)*vertsPerRow+4*(i-originX);
				if (HALF_RES_MESH) {
					offset = (j-originY)*vertsPerRow/4+2*(i-originX);
				}
				vb = vBase + offset;	//skip to correct row in vertex buffer
				// vbMirror is the pointer to the vertex in our memory based copy.
				// The important point is that we can read out of our copy to get the original
				// diffuse color, and xyz location.  It is VERY SLOW to read out of the 
				// hardware vertex buffer, possibly worse... jba.
				VERTEX_FORMAT *vbMirror = (reinterpret_cast<VERTEX_FORMAT*>(data))  + offset;
				VERTEX_FORMAT *vbaseMirror = (reinterpret_cast<VERTEX_FORMAT*>(data));
				un0 = getXWithOrigin(i)-1;
				if (un0 < -m_map->getDrawOrgX())
					un0=-m_map->getDrawOrgX();
				up1 = getXWithOrigin(i+1)+1;
				if (up1 >= m_map->getXExtent()-m_map->getDrawOrgX())
					up1=m_map->getXExtent()-m_map->getDrawOrgX()-1;

				Vector3 lightRay(0,0,0);

				//
				// (gth) Following the set of rules below lets us take advantage of lighting values that have
				// been previously computed.  The idea is to copy them ahead to future quads that will need them
				// and then not compute them when we get to those quads.  This also avoids having to read-back
				// from the vertex buffer but we do jump around in memory... probably bad anyway, maybe we should
				// compute into a temporary buffer and copy all at once...
				//
				unsigned long light_copy;

				// top-left sample -> only compute when i==0 and j==0 
				if ((i==x0) && (j==y0)) {
					l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1, getYWithOrigin(j)) - m_map->getDisplayHeight(un0, getYWithOrigin(j))));
					n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i), (getYWithOrigin(j)+1)) - m_map->getDisplayHeight(getXWithOrigin(i), vn0)));
					Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
					doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				} 
				vb++;	vbMirror++;

				//top-right sample -> compute when j==0, then copy to (right,0)
				if (j==y0) {
					l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(up1 , getYWithOrigin(j) ) - m_map->getDisplayHeight(getXWithOrigin(i) , getYWithOrigin(j) )));
					n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1 , (getYWithOrigin(j)+1) ) - m_map->getDisplayHeight(getXWithOrigin(i)+1 , vn0 )));
					Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
					light_copy = doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				
					if (i < x1-1) {
						// copy light to (right,0)
						(vBase + offset + quad_right_offset)->diffuse = (light_copy&0x00FFFFFF) | ((vbaseMirror + offset + quad_right_offset)->diffuse&0xff000000) ;
					}
				} 
				vb++;	vbMirror++;

				//bottom-right sample -> always compute, then copy to (right,3), (down,1), (down+right,0)
				l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(up1 , (getYWithOrigin(j)+1) ) - m_map->getDisplayHeight(getXWithOrigin(i) , (getYWithOrigin(j)+1) )));
				n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1 , vp1 ) - m_map->getDisplayHeight(getXWithOrigin(i)+1 , getYWithOrigin(j) )));
				Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
				light_copy = doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);
				
				if (i < x1-1) {
					// copy light to (right,3)
					//(vBase + offset + quad_right_offset + 3)->diffuse = light_copy;
					(vBase + offset + quad_right_offset + 3)->diffuse = (light_copy&0x00FFFFFF) | ((vbaseMirror + offset + quad_right_offset + 3)->diffuse&0xff000000) ;
				}
				if (j < y1-1) {
					// copy light to (down,1)
					//(vBase + offset + quad_below_offset + 1)->diffuse = light_copy;
					(vBase + offset + quad_right_offset + 1)->diffuse = (light_copy&0x00FFFFFF) | ((vbaseMirror + offset + quad_right_offset + 1)->diffuse&0xff000000) ;
				}
				if ((i < x1-1) && (j < y1-1)) {
					// copy light to (right+down,0)
					//(vBase + offset + quad_below_right_offset)->diffuse = light_copy;
					(vBase + offset + quad_right_offset)->diffuse = (light_copy&0x00FFFFFF) | ((vbaseMirror + offset + quad_right_offset)->diffuse&0xff000000) ;
				}
				vb++;	vbMirror++;

				//bottom-left sample -> compute when i==0, otherwise copy from (left,2)
				if (i==x0) {
					l2r.Set(2*MAP_XY_FACTOR,0,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i)+1 , (getYWithOrigin(j)+1) ) - m_map->getDisplayHeight(un0 , (getYWithOrigin(j)+1) )));
					n2f.Set(0,2*MAP_XY_FACTOR,MAP_HEIGHT_SCALE*(m_map->getDisplayHeight(getXWithOrigin(i) , vp1 ) - m_map->getDisplayHeight(getXWithOrigin(i) , getYWithOrigin(j) )));
					Vector3::Normalized_Cross_Product(l2r, n2f, &normalAtTexel);
					light_copy = doTheDynamicLight(vb, vbMirror, &lightRay, &normalAtTexel, pLights, numLights);

					if (j < y1-1) {
						// copy light to (down,0)
						//(vBase + offset + quad_below_offset)->diffuse = light_copy;
						(vBase + offset + quad_below_offset)->diffuse = (light_copy&0x00FFFFFF) | ((vbaseMirror + offset + quad_below_offset)->diffuse&0xff000000) ;
					}
				} 
				vb++;	vbMirror++;
			}
		}
		return 0; //success.
	}
	return -1;
}



//=============================================================================
// HeightMapRenderObjClass::doPartialUpdate
//=============================================================================
/** Updates a partial block of vertices from [x0,y0 to x1,y1]
The coordinates in partialRange are map cell coordinates, relative to the entire map.
The vertex coordinates and texture coordinates, as well as static lighting are updated.
*/
void HeightMapRenderObjClass::doPartialUpdate(const IRegion2D &partialRange, WorldHeightMap *htMap, RefRenderObjListIterator *pLightsIterator)
{	
	// Adjust range into the current drawn map range.
	Int minX = partialRange.lo.x - htMap->getDrawOrgX();
	Int maxX = partialRange.hi.x - htMap->getDrawOrgX();
	Int minY = partialRange.lo.y - htMap->getDrawOrgY();
	Int maxY = partialRange.hi.y - htMap->getDrawOrgY();
	if (minX<0) minX = 0;
	if (minY<0) minY = 0;
	if (maxX > m_x-1) maxX = m_x-1;
	if (maxY > m_y-1) maxY = m_y-1;
	if (maxX < minX) return;
	if (maxY < minY) return;
	if (m_originX == 0 && m_originY == 0) {
		// simple case.
		updateBlock(minX, minY, maxX, maxY,
								htMap, pLightsIterator);
	}
	else
	{
		minY = minY+m_originY;
		maxY = maxY+m_originY;

		if (minY> m_y-1) {
			minY -= m_y-1;
 			maxY -= m_y-1;
		}
		if (maxY > m_y-1) {
			maxY -= m_y-1;
			updateBlock(0, minY, m_x-1, m_y-1, htMap, pLightsIterator);
			updateBlock(0, 0, m_x-1, maxY, htMap, pLightsIterator);
		} else {
			updateBlock(0, minY, m_x-1, maxY, htMap, pLightsIterator);
		}
	}

	if (!m_extraBlendTilePositions)
	{	//Need to allocate memory
		m_extraBlendTilePositions = NEW Int[DEFAULT_MAX_MAP_EXTRABLEND_TILES];
		m_extraBlendTilePositionsSize = DEFAULT_MAX_MAP_EXTRABLEND_TILES;
	}
	
	//Find list of all extra blend tiles used on map.  These are tiles with 3 materials/textures
	//over the same tile and require an extra render pass.

	Int i, j;
	//First remove any existing extra blend tiles within this partial region
	for (j=0; j<m_numExtraBlendTiles; j++)
	{	Int x = m_extraBlendTilePositions[j] & 0xffff;
		Int y = m_extraBlendTilePositions[j] >> 16;
		if (x >= partialRange.lo.x && x < partialRange.hi.x &&
			y >= partialRange.lo.y && y < partialRange.hi.y)
		{	//this tile is inside region being updated so remove it by shifting tile array
			memcpy(m_extraBlendTilePositions+j,m_extraBlendTilePositions+j+1,(m_numExtraBlendTiles-1-j)*sizeof(Int));
			m_numExtraBlendTiles--;
			j--;	//need to look at index j again because this tile was removed
		}
	}

	for (j=partialRange.lo.y; j<partialRange.hi.y; j++)
		for (i=partialRange.lo.x; i<partialRange.hi.x; i++)
		{
			if (j<0 || i<0) continue;
			Real U[4],V[4];
			UnsignedByte alpha[4];
			Bool flipState,cliffState;
			if (htMap->getExtraAlphaUVData(i,j,U,V,alpha,&flipState, &cliffState))
			{	if (m_numExtraBlendTiles >= m_extraBlendTilePositionsSize)
				{	//no more room to store extra blend tiles so enlarge the buffer.
					Int *tempPositions=NEW Int[m_extraBlendTilePositionsSize+512];
					memcpy(tempPositions, m_extraBlendTilePositions, m_extraBlendTilePositionsSize*sizeof(Int));
					delete [] m_extraBlendTilePositions;
					//enlarge by more tiles to reduce memory trashing
					m_extraBlendTilePositions = tempPositions;
					m_extraBlendTilePositionsSize += 512;
				}
				//Pack x and y position into single integer since maps are limited in size
				m_extraBlendTilePositions[m_numExtraBlendTiles]=i | (j <<16);
				m_numExtraBlendTiles++;
			}
		}
	updateShorelineTiles(partialRange.lo.x,partialRange.lo.y,partialRange.hi.x,partialRange.hi.y,htMap);

	updateViewImpassableAreas(TRUE, minX, maxX, minY, maxY);
}

//=============================================================================
// HeightMapRenderObjClass::updateBlock
//=============================================================================
/** Updates a block of vertices from [x0,y0 to x1,y1]
The vertex coordinates and texture coordinates, as well as static lighting are updated.
*/
Int HeightMapRenderObjClass::updateBlock(Int x0, Int y0, Int x1, Int y1,  WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator)
{	
#ifdef _DEBUG
	DEBUG_ASSERTCRASH(x0>=0,  ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond left edge."));
	DEBUG_ASSERTCRASH(y0>=0,  ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond bottom edge."));
	DEBUG_ASSERTCRASH(x1<m_x, ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond right edge."));
	DEBUG_ASSERTCRASH(y1<m_y, ("HeightMapRenderObjClass::UpdateBlock parameters extend beyond top edge."));
	DEBUG_ASSERTCRASH(x0<=x1, ("HeightMapRenderObjClass::UpdateBlock parameters have inside-out rectangle (on X)."));
	DEBUG_ASSERTCRASH(y0<=y1, ("HeightMapRenderObjClass::UpdateBlock parameters have inside-out rectangle (on Y)."));
#endif
	Invalidate_Cached_Bounding_Volumes();
	// The tile colors are either prelit or white for the shader; switch only when every tile is rebuilt.
	if (x0 == 0 && y0 == 0 && x1 == m_x-1 && y1 == m_y-1) {
		m_gpuStaticLighting = m_gpuStaticLightingWanted;
	}
	if (pMap) {
		REF_PTR_SET(m_stageZeroTexture, pMap->getTerrainTexture());
		REF_PTR_SET(m_stageOneTexture, pMap->getAlphaTerrainTexture());
	}

	// The pieces of the rectangle per tile. With worker threads, enough cells and the lighting in the shader they
	// are filled in parallel (one tile per job); the buffers are locked and unlocked on this thread.
	struct TilePiece { Int tile, xMin, yMin, xMax, yMax, originX, originY; };
	std::vector<TilePiece> pieces;
	Int cells = 0;
	Int i,j;
	Int originX,originY;
	//step through each vertex buffer that needs updating
	for (j=0; j<m_numVBTilesY; j++)
	{
		originY=j*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
		Int yMin, yMax;
		yMin = originY;
		if (y0>yMin) yMin = y0;
		yMax = originY+VERTEX_BUFFER_TILE_LENGTH;
		if (y1<yMax) yMax = y1;
		if (yMin >= yMax) {
			continue;
		}
		for (i=0; i<m_numVBTilesX; i++)
		{	
			originX=i*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
			Int xMin, xMax;
			xMin = originX;
			if (xMin<x0) xMin = x0;
			xMax = originX+VERTEX_BUFFER_TILE_LENGTH;
			if (xMax>x1) xMax = x1;
			if (xMin >= xMax) {
				continue;
			}
			const TilePiece piece = { j*m_numVBTilesX+i, xMin, yMin, xMax, yMax, originX, originY };
			pieces.push_back(piece);
			if (m_tileBoundsDirty) {
				m_tileBoundsDirty[piece.tile] = true;
			}
			cells += (xMax-xMin)*(yMax-yMin);
		}
	}

	enum { PARALLEL_MIN_CELLS = 4096 };
	const Bool parallel = m_gpuStaticLighting && pMap && m_vertexBufferTiles && pieces.size() > 1 &&
		cells >= PARALLEL_MIN_CELLS && ::Engine::JobSystem::get().workerCount() > 0;
	if (!parallel) {
		for (const TilePiece &p : pieces) {
			updateVB(m_vertexBufferTiles[p.tile], m_vertexBufferBackup[p.tile], p.xMin, p.yMin, p.xMax, p.yMax, p.originX, p.originY, pMap, pLightsIterator);
		}
		return 0;
	}

	REF_PTR_SET(m_map, pMap);
	std::vector<std::unique_ptr<DeviceVertexBufferClass::AppendLockClass>> locks(pieces.size());
	std::vector<VERTEX_FORMAT *> targets(pieces.size());
	for (size_t k = 0; k < pieces.size(); ++k) {
		const TilePiece &p = pieces[k];
		locks[k].reset(lockTileRows(m_vertexBufferTiles[p.tile], p.yMin, p.yMax, p.originY, &targets[k]));
	}
	::Engine::parallelFor(pieces.size(), 1,
		[&]( size_t begin, size_t end )
		{
			for (size_t k = begin; k < end; ++k) {
				const TilePiece &p = pieces[k];
				fillVB(targets[k], m_vertexBufferBackup[p.tile], p.xMin, p.yMin, p.xMax, p.yMax, p.originX, p.originY, pMap, nullptr);
			}
		} );
	locks.clear();

	return 0;
}


//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// HeightMapRenderObjClass::~HeightMapRenderObjClass
//=============================================================================
/** Destructor. Releases w3d assets. */
//=============================================================================
HeightMapRenderObjClass::~HeightMapRenderObjClass(void)
{
	freeMapResources();
	if (m_extraBlendTilePositions) {
		delete [] m_extraBlendTilePositions;
		m_extraBlendTilePositions = nullptr;
	}
}

//=============================================================================
// HeightMapRenderObjClass::HeightMapRenderObjClass
//=============================================================================
/** Constructor. Mostly nulls out the member variables. */
//=============================================================================
HeightMapRenderObjClass::HeightMapRenderObjClass(void):
m_extraBlendTilePositions(nullptr),
m_numExtraBlendTiles(0),
m_numVisibleExtraBlendTiles(0),
m_extraBlendTilePositionsSize(0),
m_vertexBufferTiles(nullptr),
m_vertexBufferBackup(nullptr),
m_tileBounds(nullptr),
m_tileBoundsDirty(nullptr),
m_originX(0),
m_originY(0),
m_indexBuffer(nullptr),
m_numVBTilesX(0),
m_numVBTilesY(0),
m_numVertexBufferTiles(0),
m_numBlockColumnsInLastVB(0),
m_numBlockRowsInLastVB(0),
m_gpuDynamicLights(false),
m_gpuStaticLighting(false),
m_gpuStaticLightingWanted(false)
{
	m_staticLighting.global.count = 0;
	m_staticLighting.scene_count = 0;
	::Engine::context().heightMap = this;
}


//=============================================================================
// HeightMapRenderObjClass::adjustTerrainLOD
//=============================================================================
/** Adjust the terrain Level Of Detail.  If adj > 0 , increases LOD 1 step, if 
adj < 0 decreases it one step, if adj==0, then just sets up for the current LOD */
//=============================================================================
void HeightMapRenderObjClass::adjustTerrainLOD(Int adj) 
{
	BaseHeightMapRenderObjClass::adjustTerrainLOD(adj);

	return;

}

//=============================================================================
// HeightMapRenderObjClass::ReleaseResources
//=============================================================================
/** Releases all w3d assets, to prepare for Reset device call. */
//=============================================================================
void HeightMapRenderObjClass::ReleaseResources(void)
{
	BaseHeightMapRenderObjClass::ReleaseResources();
}

//=============================================================================
// HeightMapRenderObjClass::ReAcquireResources
//=============================================================================
/** Reallocates all W3D assets after a reset.. */
//=============================================================================
void HeightMapRenderObjClass::ReAcquireResources(void)
{
	BaseHeightMapRenderObjClass::ReAcquireResources();
}

//=============================================================================
// HeightMapRenderObjClass::reset
//=============================================================================
/** Updates the macro noise/lightmap texture (pass 3) */
//=============================================================================
void HeightMapRenderObjClass::reset(void)
{
	BaseHeightMapRenderObjClass::reset();
}

//=============================================================================
// HeightMapRenderObjClass::oversizeTerrain
//=============================================================================
/** Sets the terrain oversize amount. */
//=============================================================================
void HeightMapRenderObjClass::oversizeTerrain(Int tilesToOversize) 
{
	// The base size is the default draw window, so the widescreen extension survives a scripted
	// camera move that calls oversizeTerrain(0).
	Int width = WorldHeightMap::getDefaultDrawSize();
	Int height = WorldHeightMap::getDefaultDrawSize();
	if (tilesToOversize>0 && tilesToOversize<5) 
	{
		width += 32*tilesToOversize;
		height += 32*tilesToOversize;
		if (width>m_map->getXExtent()) 
			width = m_map->getXExtent();
		if (height>m_map->getYExtent()) 
			height = m_map->getYExtent();
	}
	Int dx = width-m_map->getDrawWidth();
	Int dy = height-m_map->getDrawHeight();
 	m_map->setDrawWidth(width);
	m_map->setDrawHeight(height);
	dx /= 2;
	dy /= 2;
	Int newOrgX = m_map->getDrawOrgX()-dx;
	Int newOrgy = m_map->getDrawOrgY()-dy;
	if (newOrgX<0) newOrgX=0;
	if (newOrgy<0) newOrgy=0;
	m_map->setDrawOrg(newOrgX,newOrgy);
	m_originX = 0;
	m_originY = 0;
	if (m_shroud)
		m_shroud->reset();
	//delete m_shroud;
	//m_shroud = NULL;
	initHeightData(m_map->getDrawWidth(), m_map->getDrawHeight(), m_map, nullptr, FALSE);		 
	m_needFullUpdate = true;
}




//=============================================================================
// HeightMapRenderObjClass::initHeightData
//=============================================================================
/** Allocate a heightmap of x by y vertices and fill with initial height values.
Also allocates all rendering resources such as vertex buffers, index buffers, 
shaders, and materials.*/
//=============================================================================
Int HeightMapRenderObjClass::initHeightData(Int x, Int y, WorldHeightMap *pMap, RefRenderObjListIterator *pLightsIterator, Bool updateExtraPassTiles)
{	
	BaseHeightMapRenderObjClass::initHeightData(x, y, pMap, pLightsIterator, updateExtraPassTiles);
	Int i,j;
//	Int	vertsPerRow=x*2-2;
//	Int	vertsPerColumn=y*2-2;

	HeightSampleType *data = nullptr;
	if (pMap) {
		data = pMap->getDataPtr();
	}

	if (updateExtraPassTiles)
	{
		m_numExtraBlendTiles = 0;
		//Do some preprocessing on map to extract useful data
		if (pMap)
		{
			Int m_mapDX=pMap->getXExtent();
			Int m_mapDY=pMap->getYExtent();
			if (!m_extraBlendTilePositions)
			{	//Need to allocate memory
				m_extraBlendTilePositions = NEW Int[DEFAULT_MAX_MAP_EXTRABLEND_TILES];
				m_extraBlendTilePositionsSize = DEFAULT_MAX_MAP_EXTRABLEND_TILES;
			}
			
			//Find list of all extra blend tiles used on map.  These are tiles with 3 materials/textures
			//over the same tile and require an extra render pass.
			for (j=0; j<(m_mapDY-1); j++)
				for (i=0; i<(m_mapDX-1); i++)
				{
					Real U[4],V[4];
					UnsignedByte alpha[4];
					Bool flipState,cliffState;
					if (pMap->getExtraAlphaUVData(i,j,U,V,alpha,&flipState, &cliffState))
					{	if (m_numExtraBlendTiles >= m_extraBlendTilePositionsSize)
						{	//no more room to store extra blend tiles so enlarge the buffer.
							Int *tempPositions=NEW Int[m_extraBlendTilePositionsSize+512];
							memcpy(tempPositions, m_extraBlendTilePositions, m_extraBlendTilePositionsSize*sizeof(Int));
							delete [] m_extraBlendTilePositions;
							//enlarge by more tiles to reduce memory trashing
							m_extraBlendTilePositions = tempPositions;
							m_extraBlendTilePositionsSize += 512;
						}
						//Pack x and y position into single integer since maps are limited in size
						m_extraBlendTilePositions[m_numExtraBlendTiles]=i | (j <<16);
						m_numExtraBlendTiles++;
					}
				}
		}
	}

	m_originX = 0;
	m_originY = 0;
	m_needFullUpdate = true;

	// If the size changed, we need to allocate.
	Bool needToAllocate = (x != m_x || y != m_y);
	// If the textures aren't allocated (usually because of a hardware reset) need to allocate.
	if (m_stageOneTexture == nullptr) {
		needToAllocate = true;
	}
	if (data && needToAllocate)
	{	//requested heightmap different from old one.
		freeIndexVertexBuffers();
		//Create static index buffers.  These will index the vertex buffers holding the map.
		m_indexBuffer=NEW_REF(DeviceIndexBufferClass,(VERTEX_BUFFER_TILE_LENGTH*VERTEX_BUFFER_TILE_LENGTH*2*3));

		// Fill up the IB
		DeviceIndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
		UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
			
		for (j=0; j<(VERTEX_BUFFER_TILE_LENGTH*VERTEX_BUFFER_TILE_LENGTH*4); j+=VERTEX_BUFFER_TILE_LENGTH*4)
		{
			for (i=j; i<(j+VERTEX_BUFFER_TILE_LENGTH*4); i+=4)	//4 vertices per 2x2 block
			{
				ib[0]=static_cast<UnsignedShort>(i);
				ib[1]=static_cast<UnsignedShort>(i+2);
				ib[2]=static_cast<UnsignedShort>(i+3);

				ib[3]=static_cast<UnsignedShort>(i);
				ib[4]=static_cast<UnsignedShort>(i+1);
				ib[5]=static_cast<UnsignedShort>(i+2);

				ib+=6;	//skip the 6 indices we just filled
			}
		}

		//Get number of vertex buffers needed to hold current map
		//First round dimensions to next multiple of VERTEX_BUFFER_TILE_LENGTH since that's our
		//blocksize
		m_numVBTilesX=1;
		for (i=VERTEX_BUFFER_TILE_LENGTH+1; i<x;)
		{	i+=VERTEX_BUFFER_TILE_LENGTH;
			m_numVBTilesX++;
		}
		m_numVBTilesY=1;
		for (j=VERTEX_BUFFER_TILE_LENGTH+1; j<y;)
		{	j+=VERTEX_BUFFER_TILE_LENGTH;
			m_numVBTilesY++;
		}

		m_numBlockColumnsInLastVB=(x-1)%VERTEX_BUFFER_TILE_LENGTH;	//right border within last VB
		m_numBlockRowsInLastVB=(y-1)%VERTEX_BUFFER_TILE_LENGTH;	//bottom border within last VB

		m_numVertexBufferTiles=m_numVBTilesX*m_numVBTilesY;
		m_x=x;
		m_y=y;
		m_vertexBufferTiles = NEW DeviceVertexBufferClass*[m_numVertexBufferTiles];
		m_vertexBufferBackup = NEW char *[m_numVertexBufferTiles];
		m_tileBounds = NEW AABoxClass[m_numVertexBufferTiles];
		m_tileBoundsDirty = NEW Bool[m_numVertexBufferTiles];
		for (i=0; i<m_numVertexBufferTiles; i++) {
			m_tileBoundsDirty[i] = true;
		}

		Int numVertex = VERTEX_BUFFER_TILE_LENGTH*2*VERTEX_BUFFER_TILE_LENGTH*2;

		for (i=0; i<m_numVertexBufferTiles; i++) {
#ifdef USE_NORMALS	 
			m_vertexBufferTiles[i]=NEW_REF(DeviceVertexBufferClass,(RENDER_FVF_XYZNUV2,numVertex,DeviceVertexBufferClass::USAGE_DEFAULT));
#else
			m_vertexBufferTiles[i]=NEW_REF(DeviceVertexBufferClass,(DEVICE_VERTEX_FORMAT,static_cast<unsigned short>(numVertex),DeviceVertexBufferClass::USAGE_DEFAULT));
#endif
			m_vertexBufferBackup[i] = NEW char[numVertex*sizeof(VERTEX_FORMAT)];
		} 

		//go with a preset material for now.
	}

	// The map lights are counted from the next frame on; a change then rebuilds all tiles.
	m_gpuStaticLightingWanted = canLightOnGpu();
	updateBlock(0,0,x-1,y-1,pMap,pLightsIterator);

	return 0;
}

//=============================================================================
// HeightMapRenderObjClass::canLightOnGpu
//=============================================================================
Bool HeightMapRenderObjClass::canLightOnGpu(void)
{
	// Depth fade, the impassable-area colors and the half resolution mesh stay on the CPU.
	return !TheGlobalData->m_cpuTerrainLights && RenderDevice::Terrain_Dynamic_Lights_On_GPU()
		&& !m_useDepthFade && !m_showImpassableAreas && !HALF_RES_MESH;
}

//=============================================================================
// HeightMapRenderObjClass::gatherStaticLighting
//=============================================================================
/** The values of doTheLight for the vertex shader: ambient, global lights and the static scene lights. */
//=============================================================================
Bool HeightMapRenderObjClass::gatherStaticLighting(void)
{
	RenderTerrainStaticLighting &out = m_staticLighting;
	out.global.ambient[0] = TheGlobalData->m_terrainAmbient[0].red;
	out.global.ambient[1] = TheGlobalData->m_terrainAmbient[0].green;
	out.global.ambient[2] = TheGlobalData->m_terrainAmbient[0].blue;
	out.global.count = 0;
	for (Int lightIndex = 0; lightIndex < TheGlobalData->m_numGlobalLights && lightIndex < 3; ++lightIndex)
	{
		const Coord3D &pos = TheGlobalData->m_terrainLightPos[lightIndex];
		out.global.ray[lightIndex][0] = -pos.x;
		out.global.ray[lightIndex][1] = -pos.y;
		out.global.ray[lightIndex][2] = -pos.z;
		out.global.diffuse[lightIndex][0] = TheGlobalData->m_terrainDiffuse[lightIndex].red;
		out.global.diffuse[lightIndex][1] = TheGlobalData->m_terrainDiffuse[lightIndex].green;
		out.global.diffuse[lightIndex][2] = TheGlobalData->m_terrainDiffuse[lightIndex].blue;
		out.global.count = lightIndex + 1;
	}
	out.scene_count = 0;
	if (!canLightOnGpu() || TheGlobalData->m_numGlobalLights > 3) {
		return false;
	}
	Bool fits = true;
	RTS3DScene *pMyScene = static_cast<RTS3DScene *>(Scene);
	RefRenderObjListIterator *it = pMyScene ? pMyScene->createLightsIterator() : nullptr;
	if (it) {
		for (it->First(); !it->Is_Done(); it->Next())
		{
			LightClass *pLight = static_cast<LightClass*>(it->Peek_Obj());
			RenderTerrainDynamicLight light;
			if (pLight->Get_Type() == LightClass::DIRECTIONAL) {
				Vector3 direction = pLight->Get_Transform().Get_Z_Vector();
				direction.Normalize();
				light.position[0] = -direction.X;
				light.position[1] = -direction.Y;
				light.position[2] = -direction.Z;
				light.directional = 1.0f;
				light.mid_range = 0.0f;
				light.range = 0.0f;
			} else {
				double range, midRange;
				pLight->Get_Far_Attenuation_Range(midRange, range);
				if (midRange < 0.1) {
					continue;
				}
				Vector3 pos = pLight->Get_Position();
				light.position[0] = pos.X;
				light.position[1] = pos.Y;
				light.position[2] = pos.Z;
				light.directional = 0.0f;
				light.mid_range = static_cast<float>(midRange);
				light.range = static_cast<float>(range);
			}
			Vector3 diffuse, ambient;
			pLight->Get_Diffuse(&diffuse);
			pLight->Get_Ambient(&ambient);
			light.diffuse[0] = diffuse.X;
			light.diffuse[1] = diffuse.Y;
			light.diffuse[2] = diffuse.Z;
			light.ambient[0] = ambient.X;
			light.ambient[1] = ambient.Y;
			light.ambient[2] = ambient.Z;
			if (out.scene_count == RenderTerrainStaticLighting::MAX_SCENE_LIGHTS) {
				fits = false;
				break;
			}
			out.scene[out.scene_count++] = light;
		}
		pMyScene->destroyLightsIterator(it);
	}
	return fits;
}


//=============================================================================
// HeightMapRenderObjClass::On_Frame_Update
//=============================================================================
/** Updates the diffuse color values in the vertices as affected by the dynamic lights.*/
//=============================================================================
void HeightMapRenderObjClass::On_Frame_Update(void)
{	
	BaseHeightMapRenderObjClass::On_Frame_Update();
	Int i,j,k;
	DeviceVertexBufferClass	**pVB;
	Int originX,originY;
	if (Scene==nullptr) return;
	RTS3DScene *pMyScene = static_cast<RTS3DScene *>(Scene);


	RefRenderObjListIterator pDynamicLightsIterator(pMyScene->getDynamicLights());
	if (m_map == nullptr) {
		return;
	}


#ifdef EXTENDED_STATS
	if (RenderDevice::stats.m_disableTerrain) {
		return;
	}
#endif

	Int numDynaLights=0;
	W3DDynamicLight *enabledLights[MAX_ENABLED_DYNAMIC_LIGHTS];

	// With Direct3D 11 the vertex shader lights the terrain; the vertex buffers keep the prelit colors.
	m_gpuDynamicLights = !TheGlobalData->m_cpuTerrainLights && RenderDevice::Terrain_Dynamic_Lights_On_GPU();
	// The static prelighting too, unless there are more map lights than the shader takes.
	m_gpuStaticLightingWanted = gatherStaticLighting();
	if (m_gpuStaticLightingWanted != m_gpuStaticLighting) {
		m_needFullUpdate = true;
	}

	Int yCoordMin = m_map->getDrawOrgY();
	Int yCoordMax = m_y+m_map->getDrawOrgY();
	Int xCoordMin = m_map->getDrawOrgX();
	Int xCoordMax = m_x+m_map->getDrawOrgX();

	for (pDynamicLightsIterator.First(); !pDynamicLightsIterator.Is_Done(); pDynamicLightsIterator.Next())
	{		
		W3DDynamicLight *pLight = static_cast<W3DDynamicLight*>(pDynamicLightsIterator.Peek_Obj());
		pLight->m_processMe = false;
		if (pLight->m_enabled || pLight->m_priorEnable) {
			Real range = pLight->Get_Attenuation_Range();
			if (pLight->m_priorEnable) {
				pLight->m_prevMinX = pLight->m_minX;
				pLight->m_prevMinY = pLight->m_minY;
				pLight->m_prevMaxX = pLight->m_maxX;
				pLight->m_prevMaxY = pLight->m_maxY;
			} 
			Vector3	pos = pLight->Get_Position();
			pLight->m_minX = static_cast<Int>((pos.X-range)/MAP_XY_FACTOR);
			pLight->m_maxX = static_cast<Int>((pos.X+range)/MAP_XY_FACTOR+1.0f);
			pLight->m_minY = static_cast<Int>((pos.Y-range)/MAP_XY_FACTOR);
			pLight->m_maxY = static_cast<Int>((pos.Y+range)/MAP_XY_FACTOR+1.0f);
			if (!pLight->m_priorEnable) {
				pLight->m_prevMinX = pLight->m_minX;
				pLight->m_prevMinY = pLight->m_minY;
				pLight->m_prevMaxX = pLight->m_maxX;
				pLight->m_prevMaxY = pLight->m_maxY;
			} 

			if (pLight->m_minX < xCoordMax && 
					pLight->m_minY < yCoordMax &&
					pLight->m_maxX > xCoordMin && 
					pLight->m_maxY > yCoordMin) {
				pLight->m_processMe = TRUE;
			} else if (pLight->m_prevMinX < xCoordMax && 
					pLight->m_prevMinY < yCoordMax &&
					pLight->m_prevMaxX > xCoordMin && 
					pLight->m_prevMaxY > yCoordMin) {
				pLight->m_processMe = TRUE;
			} else {
				pLight->m_processMe = false;
			}
			if (pLight->m_processMe) {
				enabledLights[numDynaLights] = pLight;
				numDynaLights++;
				if (numDynaLights == MAX_ENABLED_DYNAMIC_LIGHTS) {
					break;
				}
			}
		}
		pLight->m_priorEnable = pLight->m_enabled;
	}
	if (m_gpuDynamicLights) {
		// Same selection and values as doTheDynamicLight.
		static_assert(MAX_ENABLED_DYNAMIC_LIGHTS <= RenderTerrainDynamicLight::MAX_LIGHTS, "too many dynamic lights");
		RenderTerrainDynamicLight lights[MAX_ENABLED_DYNAMIC_LIGHTS];
		UnsignedInt count = 0;
		for (k=0; k<numDynaLights; k++) {
			W3DDynamicLight *pLight = enabledLights[k];
			if (!pLight->isEnabled()) {
				continue;
			}
			RenderTerrainDynamicLight &out = lights[count];
			if (pLight->Get_Type() == LightClass::DIRECTIONAL) {
				Vector3 direction;
				pLight->Get_Spot_Direction(direction);
				out.position[0] = -direction.X;
				out.position[1] = -direction.Y;
				out.position[2] = -direction.Z;
				out.directional = 1.0f;
				out.mid_range = 0.0f;
				out.range = 0.0f;
			} else {
				double range, midRange;
				pLight->Get_Far_Attenuation_Range(midRange, range);
				if (midRange < 0.1) {
					continue;
				}
				Vector3 pos = pLight->Get_Position();
				out.position[0] = pos.X;
				out.position[1] = pos.Y;
				out.position[2] = pos.Z;
				out.directional = 0.0f;
				out.mid_range = static_cast<float>(midRange);
				out.range = static_cast<float>(range);
			}
			Vector3 diffuse, ambient;
			pLight->Get_Diffuse(&diffuse);
			pLight->Get_Ambient(&ambient);
			out.diffuse[0] = diffuse.X;
			out.diffuse[1] = diffuse.Y;
			out.diffuse[2] = diffuse.Z;
			out.ambient[0] = ambient.X;
			out.ambient[1] = ambient.Y;
			out.ambient[2] = ambient.Z;
			count++;
		}
		RenderDevice::Set_Terrain_Dynamic_Lights(lights, count);
		return;
	}
	if (numDynaLights > 0) {
		//step through each vertex buffer that needs updating
		for (j=0; j<m_numVBTilesY; j++)
		{
			originY=j*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
			Int yMin, yMax;
			yMin = originY;
			yMax = originY+VERTEX_BUFFER_TILE_LENGTH;
			Bool intersect = false;
			Int yCoordMin2 = getYWithOrigin(yMin)+m_map->getDrawOrgY()-m_map->getBorderSizeInline();
			Int yCoordMax2 = getYWithOrigin(yMax-1)+m_map->getDrawOrgY()+1-m_map->getBorderSizeInline();
			if (yCoordMax2>yCoordMin2) {
				// no wrap occurred.
				for (k=0; k<numDynaLights; k++) {
					if (enabledLights[k]->m_minY < yCoordMax2 && 
						enabledLights[k]->m_maxY > yCoordMin2) {
						intersect = true;
						break;
					}
					if (enabledLights[k]->m_prevMinY < yCoordMax2 && 
						enabledLights[k]->m_prevMaxY > yCoordMin2) {
						intersect = true;
						break;
					}
				}
			} else {
				// wrap occurred, so we are outside of this range.
				int tmp=yCoordMin2;
				yCoordMin2 = yCoordMax2;
				yCoordMax2 = tmp;
				for (k=0; k<numDynaLights; k++) {
					if (enabledLights[k]->m_minY <=  yCoordMin2 || 
						enabledLights[k]->m_maxY >= yCoordMax2) {
						intersect = true;
						break;
					}
					if (enabledLights[k]->m_prevMinY <=  yCoordMin2 || 
						enabledLights[k]->m_prevMaxY >= yCoordMax2) {
						intersect = true;
						break;
					}
				}
			}
			if (!intersect) {
				continue;
			}

			for (i=0; i<m_numVBTilesX; i++)
			{	
				originX=i*VERTEX_BUFFER_TILE_LENGTH;	//location of this VB on the large full-size heightmap
				Int xMin, xMax;
				xMin = originX;
				xMax = originX+VERTEX_BUFFER_TILE_LENGTH;

				Bool intersect2 = false;
				Int xCoordMin2 = getXWithOrigin(xMin)+m_map->getDrawOrgX()-m_map->getBorderSizeInline();
				Int xCoordMax2 = getXWithOrigin(xMax-1)+m_map->getDrawOrgX()+1-m_map->getBorderSizeInline();
				if (xCoordMax2>xCoordMin2) {
					// no wrap occurred.
					for (k=0; k<numDynaLights; k++) {
						if (enabledLights[k]->m_minX < xCoordMax2 && 
							enabledLights[k]->m_maxX > xCoordMin2) {
							intersect2 = true;
							break;
						}
						if (enabledLights[k]->m_prevMinX < xCoordMax2 && 
							enabledLights[k]->m_prevMaxX > xCoordMin2) {
							intersect2 = true;
							break;
						}
					}
				} else {
					// wrap occurred, so we are outside of this range.
					int tmp=xCoordMin2;
					xCoordMin2 = xCoordMax2;
					xCoordMax2 = tmp;
					for (k=0; k<numDynaLights; k++) {
						if (enabledLights[k]->m_minX <=  xCoordMin2 || 
							enabledLights[k]->m_maxX >= xCoordMax2) {
							intersect2 = true;
							break;
						}
						if (enabledLights[k]->m_prevMinX <=  xCoordMin2 ||
							enabledLights[k]->m_prevMaxX >= xCoordMax2) {
							intersect2 = true;
							break;
						}
					}
				}
				if (!intersect2) {
					continue;
				}
				pVB=m_vertexBufferTiles+j*m_numVBTilesX+i;	//point to correct row/column of vertex buffers 
				char **pData = m_vertexBufferBackup+j*m_numVBTilesX+i;
				updateVBForLight(*pVB, *pData, xMin, yMin, xMax, yMax, originX,originY, enabledLights, numDynaLights);
			}
		}
	}
}

//=============================================================================
// HeightMapRenderObjClass::staticLightingChanged
//=============================================================================
/** Notification that all lighting needs to be recalculated. */
//=============================================================================
void HeightMapRenderObjClass::staticLightingChanged( void )
{
	BaseHeightMapRenderObjClass::staticLightingChanged();
}

//=============================================================================
// HeightMapRenderObjClass::heightsChanged
//=============================================================================
/** A building flattened the ground: only the cells around the change are rebuilt (their normals reach
two cells), not the whole window. Scorches and road lighting follow as with a lighting change. */
//=============================================================================
void HeightMapRenderObjClass::heightsChanged( const IRegion2D &range )
{
	IRegion2D grown;
	grown.lo.x = range.lo.x - 2;
	grown.lo.y = range.lo.y - 2;
	grown.hi.x = range.hi.x + 2;
	grown.hi.y = range.hi.y + 2;
	if (m_needPartialUpdate) {
		if (grown.lo.x > m_partialRange.lo.x) grown.lo.x = m_partialRange.lo.x;
		if (grown.lo.y > m_partialRange.lo.y) grown.lo.y = m_partialRange.lo.y;
		if (grown.hi.x < m_partialRange.hi.x) grown.hi.x = m_partialRange.hi.x;
		if (grown.hi.y < m_partialRange.hi.y) grown.hi.y = m_partialRange.hi.y;
	}
	m_partialRange = grown;
	m_needPartialUpdate = true;
	m_scorchesInBuffer = 0;
	m_curNumScorchVertices = 0;
	m_curNumScorchIndices = 0;
	m_roadBuffer->updateLighting();
}

constexpr auto CENTER_LIMIT = 2;
constexpr auto BIG_JUMP = 16;
constexpr auto WIDE_STEP = 32;

static Int visMinX, visMinY, visMaxX, visMaxY;
static Bool check(const FrustumClass & frustum, WorldHeightMap *pMap, Int x, Int y) 
{
	if (x<0 || y<0) return(false);
	if (x>= pMap->getXExtent() || y>= pMap->getYExtent()) return(false);
	if (x >= visMinX && y >= visMinY && x <=visMaxX && y <= visMaxY) {
		return(true);
	}
	Int height = pMap->getHeight(x, y);
	Vector3 loc((x-pMap->getBorderSizeInline())*MAP_XY_FACTOR, (y-pMap->getBorderSizeInline())*MAP_XY_FACTOR, height*MAP_HEIGHT_SCALE);
	if (CollisionMath::Overlap_Test(frustum,loc) == CollisionMath::INSIDE) {
		if (x<visMinX) visMinX=x;
		if (x>visMaxX) visMaxX=x;
		if (y<visMinY) visMinY=y;
		if (y>visMaxY) visMaxY=y;
		return(true);
	}
	return(false);
}

static void calcVis(const FrustumClass & frustum, WorldHeightMap *pMap, Int minX, Int minY, Int maxX, Int maxY, Int limit)
{
	if (maxX-minX<2) return;
	if (maxY-minY<2) return;
	if (minX >=visMinX && minY >= visMinY && maxX <=visMaxX && maxY <= visMaxY) {
		return;
	}
	Int midX = (minX+maxX)/2;
	Int midY = (minY+maxY)/2;
	Bool recurse1 = maxX-minX>=limit;
	Bool recurse2 = recurse1;
	Bool recurse3 = recurse1;
	Bool recurse4 = recurse1;	 
	/* boxes are:

			1     2


			3			4 */

	if (check(frustum, pMap, midX, maxY)) {
		recurse1=true;
		recurse2=true;
	}
	if (check(frustum, pMap, midX, minY)) {
		recurse3=true;
		recurse4=true;
	}
	if (check(frustum, pMap, midX, midY)) {
		recurse1=true;
		recurse2=true;
		recurse3=true;
		recurse4=true;
	}
	if (check(frustum, pMap, minX, midY)) {
		recurse1=true;
		recurse3=true;
	}
	if (check(frustum, pMap, maxX, midY)) {
		recurse2=true;
		recurse4=true;
	}
	if (recurse1) {
		calcVis(frustum, pMap, minX, midY, midX, maxY, limit);
	}
	if (recurse2) {
		calcVis(frustum, pMap, midX, midY, maxX, maxY, limit);
	}
	if (recurse3) {
		calcVis(frustum, pMap, minX, minY, midX, midY, limit);
	}
	if (recurse4) {
		calcVis(frustum, pMap, midX, minY, maxX, midY, limit);
	}
}





//=============================================================================
// Moves the window origin so the cell range [nearMin, nearMax] plus margin lies inside a window
// of the given size. If the range is larger than the window, nearMin wins.
//=============================================================================
static Int clampDrawOrgToNear(Int org, Real nearMin, Real nearMax, Int size, Int margin)
{
	const Int lo = static_cast<Int>(floorf(nearMin)) - margin;
	const Int hi = static_cast<Int>(ceilf(nearMax)) + margin;
	if (org + size - 1 < hi)
		org = hi - (size - 1);
	if (org > lo)
		org = lo;
	return org;
}

//=============================================================================
// HeightMapRenderObjClass::updateCenter
//=============================================================================
/** Updates the positioning of the drawn portion of the height map in the 
heightmap.  As the view slides around, this determines what is the actually
rendered portion of the terrain.  Only a 96x96 section is rendered at any time, 
even though maps can be up to 1024x1024.  This function determines which subset
is rendered. */
//=============================================================================
void HeightMapRenderObjClass::updateCenter(CameraClass *camera , RefRenderObjListIterator *pLightsIterator)
{
	if (m_map==nullptr) {
		return;
	}
	if (m_updating) {
		return;
	}
	if (m_vertexBufferTiles ==nullptr)
		return;		//did not initialize resources yet.

	BaseHeightMapRenderObjClass::updateCenter(camera, pLightsIterator);

	m_updating = true;
	if (m_needFullUpdate) 
  {
		m_needFullUpdate = false;
		m_needPartialUpdate = false;
		updateBlock(0, 0, m_x-1, m_y-1, m_map, pLightsIterator);
		m_updating = false;
		return;
	}
	if (m_needPartialUpdate)
	{
		m_needPartialUpdate = false;
		doPartialUpdate(m_partialRange, m_map, pLightsIterator);
	}

	if (m_x >= m_map->getXExtent() && m_y >= m_map->getYExtent()) 
  {
		m_updating = false;
		return; // no need to center. 
	}

	Int cellOffset = 1;
	if (HALF_RES_MESH) {
		cellOffset = 2;
	}
	// determine the ray corresponding to the camera and distance to projection plane
	Matrix3D camera_matrix = camera->Get_Transform();
	
	Vector3 camera_location  = camera->Get_Position();

	Vector3 rayLocation;
	Vector3 rayDirection;
	Vector3 rayDirectionPt;
	// the projected ray has the same origin as the camera
	rayLocation = camera_location; 
	// determine the location of the screen coordinate in camera-model space
	const ViewportClass &viewport = camera->Get_Viewport();
	Int i, j, minHt;
	Int maxHt = 0;	// highest terrain height in the window, for the near screen corners

	Real intersectionZ;
	minHt = m_map->getMaxHeightValue();
	{
		// Row by row over contiguous bytes (getDisplayHeight(i, j) is data[orgX + i + width * (orgY + j)]); the
		// compiler vectorizes the inner loop.
		const UnsignedByte *data = m_map->getDataPtr();
		const Int width = m_map->getXExtent();
		UnsignedByte lo = 255, hi = 0;
		for (j=0; j<m_y; j++) {
			const UnsignedByte *row = data + m_map->getDrawOrgX() + width * (m_map->getDrawOrgY() + j);
			for (i=0; i<m_x; i++) {
				const UnsignedByte cur = row[i];
				lo = (cur < lo) ? cur : lo;
				hi = (cur > hi) ? cur : hi;
			}
		}
		if (m_x > 0 && m_y > 0) {
			if (lo < minHt) minHt = lo;
			if (hi > maxHt) maxHt = hi;
		}
	}
	intersectionZ = static_cast<float>(minHt);
//	float aspect = camera->Get_Aspect_Ratio();

	Vector2 min,max;
	camera->Get_View_Plane(min,max);
	float xscale = (max.X - min.X);
	float yscale = (max.Y - min.Y);

	float zmod = -1.0; // Scene->vpd; // Note: view plane distance is now always 1.0 from the camera
	float minX = 200000;
	float maxX = -minX;
	float minY = 200000;
	float maxY = -minY;
	// Per screen row j, the ground points of the corners at the highest terrain height; no visible
	// terrain can lie closer to the camera in that corner. The row whose ray points down more
	// steeply is the lower, near screen edge.
	float cornerDirZ[2] = { 0.0f, 0.0f };
	float nearCornerX[2][2];
	float nearCornerY[2][2];
	// Map heights are raw values (0..255); world height is raw * MAP_HEIGHT_SCALE. The near point
	// never lies behind the camera: if the camera is below the highest terrain (e.g. in cutscenes),
	// visible terrain may start right at the camera.
	const float nearPlaneZ = static_cast<float>(maxHt) * MAP_HEIGHT_SCALE;
	for (i=0; i<2; i++) {
		for (j=0; j<2; j++) {
			float xmod = static_cast<float>((-i + 0.5 + viewport.Min.X) * zmod * xscale);// / aspect;
			float ymod = static_cast<float>((j - 0.5 - viewport.Min.Y) * zmod * yscale);// * aspect;

			// transform the screen coordinates by the camera's matrix into world coordinates.
			float x = zmod * camera_matrix[0][2] + xmod * camera_matrix[0][0] + ymod * camera_matrix[0][1];
			float y = zmod * camera_matrix[1][2] + xmod * camera_matrix[1][0] + ymod * camera_matrix[1][1];
			float z = zmod * camera_matrix[2][2] + xmod * camera_matrix[2][0] + ymod * camera_matrix[2][1];

			rayDirection.Set(x,y,z);
			rayDirection.Normalize();
			rayDirectionPt = rayLocation+rayDirection;

			cornerDirZ[j] = rayDirection.Z;
			float nearT = 0.0f;	// ray parameter up to nearPlaneZ, never negative
			if (rayDirection.Z < 0.0f && rayLocation.Z > nearPlaneZ)
				nearT = (nearPlaneZ - rayLocation.Z) / rayDirection.Z;
			nearCornerX[j][i] = rayLocation.X + nearT * rayDirection.X;
			nearCornerY[j][i] = rayLocation.Y + nearT * rayDirection.Y;

			x = Vector3::Find_X_At_Z(intersectionZ, rayLocation, rayDirectionPt);
			y = Vector3::Find_Y_At_Z(intersectionZ, rayLocation, rayDirectionPt);
			if (x<minX) minX = x;
			if (x>maxX) maxX = x;
			if (y<minY) minY = y;
			if (y>maxY) maxY = y;
		}
	}

	// convert back to cell indexes.
	minX /= MAP_XY_FACTOR;
	maxX /= MAP_XY_FACTOR;
	minY /= MAP_XY_FACTOR;
	maxY /= MAP_XY_FACTOR;

	minX += m_map->getBorderSizeInline();
	maxX += m_map->getBorderSizeInline();
	minY += m_map->getBorderSizeInline();
	maxY += m_map->getBorderSizeInline();

	visMinX = m_map->getXExtent();
	visMinY = m_map->getYExtent();
	visMaxX = 0;
	visMaxY = 0;

	///< @todo find out why values go out of range
	if (minX<0) minX=0;
	if (minY<0) minY=0;
	if (maxX > static_cast<float>(visMinX)) maxX = static_cast<float>(visMinX);
	if (maxY > static_cast<float>(visMinY)) maxY = static_cast<float>(visMinY);

	const FrustumClass & frustum = camera->Get_Frustum();
	Int limit = static_cast<Int>((maxX-minX)/2);
	if (limit > WIDE_STEP/2) {
		limit=WIDE_STEP/2;
	}
	calcVis(frustum, m_map, static_cast<Int>(minX-WIDE_STEP/2), static_cast<Int>(minY-WIDE_STEP/2), static_cast<Int>(maxX+WIDE_STEP/2), static_cast<Int>(maxY+WIDE_STEP/2), limit);

	if (m_map) {
		Int newOrgX;
		if (visMaxX-visMinX > m_x) {
			newOrgX = static_cast<Int>((maxX+minX)/2-m_x/2.0);	
		} else {
			newOrgX = static_cast<Int>((visMaxX+visMinX)/2-m_x/2.0);
		}

		Int newOrgY;
		if (visMaxY - visMinY > m_y) {
			newOrgY = visMinY+1;	
		}	else {
			newOrgY = static_cast<Int>((visMaxY+visMinY)/2-m_y/2.0);
		}																															  
		if (TheTacticalView->getFieldOfView() != 0) {
			newOrgX = static_cast<Int>((visMaxX+visMinX)/2-m_x/2.0);
			newOrgY = static_cast<Int>((visMaxY+visMinY)/2-m_y/2.0);
		}
		// If the visible area is larger than the window (tilted or diagonally rotated camera), the near
		// terrain at the bottom screen edge must still lie inside the window, otherwise objects stand
		// in black. If the window is large enough, nothing changes.
		const Int nearRow = (cornerDirZ[0] < cornerDirZ[1]) ? 0 : 1;
		if (cornerDirZ[nearRow] < 0.0f) {
			const Real border = static_cast<Real>(m_map->getBorderSizeInline());
			const Real nearMinX = (nearCornerX[nearRow][0] < nearCornerX[nearRow][1] ? nearCornerX[nearRow][0] : nearCornerX[nearRow][1]) / MAP_XY_FACTOR + border;
			const Real nearMaxX = (nearCornerX[nearRow][0] > nearCornerX[nearRow][1] ? nearCornerX[nearRow][0] : nearCornerX[nearRow][1]) / MAP_XY_FACTOR + border;
			const Real nearMinY = (nearCornerY[nearRow][0] < nearCornerY[nearRow][1] ? nearCornerY[nearRow][0] : nearCornerY[nearRow][1]) / MAP_XY_FACTOR + border;
			const Real nearMaxY = (nearCornerY[nearRow][0] > nearCornerY[nearRow][1] ? nearCornerY[nearRow][0] : nearCornerY[nearRow][1]) / MAP_XY_FACTOR + border;
			const Int NEAR_MARGIN = 2;
			newOrgX = clampDrawOrgToNear(newOrgX, nearMinX, nearMaxX, m_x, NEAR_MARGIN);
			newOrgY = clampDrawOrgToNear(newOrgY, nearMinY, nearMaxY, m_y, NEAR_MARGIN);
		}
		if (HALF_RES_MESH) {
			newOrgX &= 0xFFFFFFFE;
			newOrgY &= 0xFFFFFFFE;
		}
		Int deltaX = newOrgX - m_map->getDrawOrgX();
		Int deltaY = newOrgY - m_map->getDrawOrgY();
		if (IABS(deltaX) > m_x/2 || IABS(deltaY)>m_x/2) {
			m_map->setDrawOrg(newOrgX, newOrgY);
			m_originY = 0;
			m_originX = 0;
			updateBlock(0, 0, m_x-1, m_y-1, m_map, pLightsIterator); 
			m_updating = false;
			return;
		}

		if (abs(deltaX)>CENTER_LIMIT || abs(deltaY)>CENTER_LIMIT) {
			if (abs(deltaY) >= CENTER_LIMIT) {
				if (m_map->setDrawOrg(m_map->getDrawOrgX(), newOrgY)) {
					Int minY2 = 0;
					Int maxY2 = 0;
					deltaY -= newOrgY - m_map->getDrawOrgY(); 
					m_originY += deltaY;
					if (m_originY >= m_y-1) m_originY -= m_y-1;
					if (deltaY<0) {
						minY2 = m_originY;
						maxY2 = m_originY-deltaY;
					} else {
						minY2 = m_originY - deltaY;
						maxY2 = m_originY;
					}
					minY2-=cellOffset;
					if (m_originY < 0) m_originY += m_y-1;
					if (minY2<0) {
						minY2 += m_y-1;
						if (minY2<0) minY2 = 0;
						updateBlock(0, minY2, m_x-1, m_y-1, m_map, pLightsIterator);
						updateBlock(0, 0, m_x-1, maxY2, m_map, pLightsIterator);
					} else {
						updateBlock(0, minY2, m_x-1, maxY2, m_map, pLightsIterator);
					}
				}
				// It is much more efficient to update a cople of columns one frame, and then
				// a couple of rows.  So if we aren't "jumping" to a new view, and have done X
				// recently, return.
				if (abs(deltaX) < BIG_JUMP && !m_doXNextTime) {
					m_updating = false;
					m_doXNextTime = true;
					return;	// Only do the y this frame.  Do x next frame.  jba.
				}
			}
			if (abs(deltaX) > CENTER_LIMIT) {
				m_doXNextTime = false;
				newOrgX = m_map->getDrawOrgX() + deltaX;
				if (m_map->setDrawOrg(newOrgX, m_map->getDrawOrgY())) {
					Int minX2 = 0;
					Int maxX2 = 0;
					deltaX -= newOrgX - m_map->getDrawOrgX(); 
					m_originX += deltaX;
					if (m_originX >= m_x-1) m_originX -= m_x-1;
					if (deltaX<0) {
						minX2 = m_originX;
						maxX2 = m_originX-deltaX;
					} else {
						minX2 = m_originX - deltaX;
						maxX2 = m_originX;
					}
					minX2-=cellOffset;
					maxX2+=cellOffset;
					if (m_originX < 0) m_originX += m_x-1;
					if (minX2<0) {
						minX2 += m_x-1;
						if (minX2<0) minX2 = 0;
						updateBlock(minX2,0,m_x-1, m_y-1, m_map, pLightsIterator);
						updateBlock(0,0,maxX2, m_y-1, m_map, pLightsIterator);
					} else {
						updateBlock(minX2,0,maxX2, m_y-1, m_map, pLightsIterator);
					}
				}
			} 
		}
	}
	m_updating = false;
}

//=============================================================================
// HeightMapRenderObjClass::Render
//=============================================================================
/** Renders (draws) the terrain. */
//=============================================================================
//DECLARE_PERF_TIMER(Terrain_Render)

// The bounds come from the backup copy of the tile's used cells (with the ring buffer origin a tile can hold
// cells from both window edges, so they are not derived from the tile position).
Bool HeightMapRenderObjClass::isTileVisible(Int tile, CameraClass *camera)
{
	if (!camera || !m_tileBounds || !m_vertexBufferBackup || HALF_RES_MESH) {
		return true;
	}
	if (m_tileBoundsDirty[tile]) {
		m_tileBoundsDirty[tile] = false;
		const Int tileX = tile % m_numVBTilesX;
		const Int tileY = tile / m_numVBTilesX;
		Int columns = (m_x-1) - tileX*VERTEX_BUFFER_TILE_LENGTH;
		if (columns > VERTEX_BUFFER_TILE_LENGTH) columns = VERTEX_BUFFER_TILE_LENGTH;
		Int rows = (m_y-1) - tileY*VERTEX_BUFFER_TILE_LENGTH;
		if (rows > VERTEX_BUFFER_TILE_LENGTH) rows = VERTEX_BUFFER_TILE_LENGTH;
		const Int vertsPerRow = VERTEX_BUFFER_TILE_LENGTH*4;
		const VERTEX_FORMAT *base = reinterpret_cast<const VERTEX_FORMAT*>(m_vertexBufferBackup[tile]);
		Vector3 lo(FLT_MAX, FLT_MAX, FLT_MAX);
		Vector3 hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (Int row = 0; row < rows; ++row) {
			const VERTEX_FORMAT *v = base + row*vertsPerRow;
			for (Int k = 0; k < columns*4; ++k) {
				if (v[k].x < lo.X) lo.X = v[k].x;
				if (v[k].y < lo.Y) lo.Y = v[k].y;
				if (v[k].z < lo.Z) lo.Z = v[k].z;
				if (v[k].x > hi.X) hi.X = v[k].x;
				if (v[k].y > hi.Y) hi.Y = v[k].y;
				if (v[k].z > hi.Z) hi.Z = v[k].z;
			}
		}
		if (rows <= 0 || columns <= 0) {
			lo.Set(0.0f, 0.0f, 0.0f);
			hi.Set(0.0f, 0.0f, 0.0f);
		}
		m_tileBounds[tile].Init_Min_Max(lo, hi);
	}
	// The tessellation moves the surface along its normal; a tile just outside the view can reach into it.
	const Real displacement = RenderDevice::Terrain_Tess_Max_Displacement();
	if (displacement > 0.0f) {
		AABoxClass box = m_tileBounds[tile];
		box.Extent += Vector3(displacement, displacement, displacement);
		return !camera->Cull_Box(box);
	}
	return !camera->Cull_Box(m_tileBounds[tile]);
}

void HeightMapRenderObjClass::Render(RenderInfoClass & rinfo)
{
	//USE_PERF_TIMER(Terrain_Render)
	
	Int i,j,devicePasses;
	W3DShaderManager::ShaderTypes st = W3DShaderManager::ST_TERRAIN_BASE;
	Bool doCloud = TheGlobalData->m_useCloudMap;

	Matrix3D tm(Transform);
	// If there are trees, tell them to draw at the transparent time to draw.
	if (m_treeBuffer) {
		m_treeBuffer->setIsTerrain();
	}



#ifdef EXTENDED_STATS
	if (RenderDevice::stats.m_disableTerrain) {
		return;
	}
#endif

	RenderDevice::Set_Light_Environment(rinfo.light_environment);

	// Force shaders to update.
	m_stageTwoTexture->restore();
	RenderDevice::Set_Texture(0,nullptr);
	RenderDevice::Set_Texture(1,nullptr);
	ShaderClass::Invalidate();

	//	tm.Scale(ObjSpaceExtent);
	RenderDevice::Set_Transform(WW3D_TS_WORLD,tm);

	//Apply the shader and material

	RenderDevice::Set_Index_Buffer(m_indexBuffer,0);

	Bool doMultiPassWireFrame=FALSE;

	if ((static_cast<RTS3DScene *>(rinfo.Camera.Get_User_Data()))->getCustomPassMode() == CustomScenePassModes::SCENE_PASS_ALPHA_MASK ||
		(static_cast<SceneClass *>(rinfo.Camera.Get_User_Data()))->Get_Extra_Pass_Polygon_Mode() == SceneClass::EXTRA_PASS_CLEAR_LINE)
	{
			if (WW3D::Is_Texturing_Enabled())
			{	//first pass where we just fill the z-buffer

				devicePasses=1;	//one pass solid, next in wireframe.
				doMultiPassWireFrame=TRUE;

				if (rinfo.Additional_Pass_Count())
				{
					rinfo.Peek_Additional_Pass(0)->Install_Materials();
					renderTerrainPass(&rinfo.Camera, !RenderDevice::Shadow_Maps_Active());
					rinfo.Peek_Additional_Pass(0)->UnInstall_Materials();
					return;
				}
			}
			else
			{	//wireframe pass
				//Set to vertex diffuse lighting
				RenderDevice::Set_Material(m_vertexMaterialClass);
				//Set shader to non-textured solid color from vertex
				RenderDevice::Set_Shader(ShaderClass::_PresetOpaqueSolidShader);
				devicePasses=1;	//one pass solid, next in wireframe.
				RenderDevice::Apply_Render_State_Changes();
				RenderDevice::Set_Texture_Stage(0, TextureStageDesc().Color_Arg2(WW3D_TA_TFACTOR));
				RenderDevice::Set_Render_State(WW3D_RS_TEXTUREFACTOR,0xff808080);
				doMultiPassWireFrame=TRUE;
				renderTerrainPass(&rinfo.Camera, !RenderDevice::Shadow_Maps_Active());
				RenderDevice::Set_Render_State(WW3D_RS_TEXTUREFACTOR,0xff008000);
				return;
			}
	}
	else
	{
		RenderDevice::Set_Material(m_vertexMaterialClass);
		RenderDevice::Set_Shader(m_shaderClass);

		if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT) {
			doCloud = false;
		}

 		st=W3DShaderManager::ST_TERRAIN_BASE; //set default shader
 		
 		//set correct shader based on current settings
 		if (!ShaderClass::Is_Backface_Culling_Inverted())
 		{	//not reflection pass
 			if (TheGlobalData->m_useLightMap && doCloud)
 			{	st=W3DShaderManager::ST_TERRAIN_BASE_NOISE12;
 			}
 			else
 			if (TheGlobalData->m_useLightMap)
 			{	//lightmap only
 				st=W3DShaderManager::ST_TERRAIN_BASE_NOISE2;
 			}
 			else
 			if (doCloud)
 			{	//cloudmap only
 				st=W3DShaderManager::ST_TERRAIN_BASE_NOISE1;
 			}
 		}
 		else
 		{	//reflection pass, just do base texture
 			st=W3DShaderManager::ST_TERRAIN_BASE;
 		}
 
 		//Find number of passes required to render current shader
 		devicePasses=W3DShaderManager::getShaderPasses(st);
 
 		if (m_disableTextures)
 			devicePasses=1;	//force to 1 lighting-only pass
 
 		//Specify all textures that this shader may need.
 		W3DShaderManager::setTexture(0,m_stageZeroTexture);
 		W3DShaderManager::setTexture(1,m_stageZeroTexture);
 		W3DShaderManager::setTexture(2,m_stageTwoTexture);	//cloud
 		W3DShaderManager::setTexture(3,m_stageThreeTexture);//noise
		//Disable writes to destination alpha channel (if there is one)
		if (RenderDevice::getBackBufferFormat() == WW3D_FORMAT_A8R8G8B8)
			RenderDevice::Set_Render_State(WW3D_RS_COLORWRITEENABLE,WW3D_COLORWRITEENABLE_BLUE|WW3D_COLORWRITEENABLE_GREEN|WW3D_COLORWRITEENABLE_RED);
	}

	Int pass;
	// The base pass draws terrain with atlas coordinates; with "terraindetail" the terrain shader adds
	// the detail layer on top (not in the reflection).
	const Bool terrainDetail = !doMultiPassWireFrame && !ShaderClass::Is_Backface_Culling_Inverted();
	// The rock layer and the lighting in the vertex shader need the map height field. The prelighting light
	// (same values as doTheLight) is the scene light of the frame constants and is set every frame. The backend
	// uploads the height field only when it changes.
	// The relief of the terrain textures needs both as well, the tessellation the height field (normals).
	const Bool terrainPixelLight = terrainDetail &&
		(RenderDevice::Get_Effect_Settings().flags & (RENDER_EFFECT_TERRAIN_ROCK | RENDER_EFFECT_TERRAIN_NORMAL));
	const Bool terrainTess = terrainDetail && (RenderDevice::Get_Effect_Settings().flags & RENDER_EFFECT_TERRAIN_TESS);
	const Bool shaderLights = m_gpuDynamicLights || m_gpuStaticLighting;
	RenderDevice::Set_Terrain_Static_Lighting(m_gpuStaticLighting ? &m_staticLighting : nullptr);
	if (terrainTess && m_map)
	{
		if (!m_tessMask)
			m_tessMask = NEW TerrainTessMask;
		Real scorchDiscs[MAX_SCORCH_MARKS * 3];
		for (Int s = 0; s < m_numScorches; ++s)
		{
			scorchDiscs[s * 3] = m_scorches[s].location.X;
			scorchDiscs[s * 3 + 1] = m_scorches[s].location.Y;
			scorchDiscs[s * 3 + 2] = m_scorches[s].radius;
		}
		m_tessMask->update(m_map, scorchDiscs, m_numScorches);
	}
	if (m_map && (terrainPixelLight || terrainTess || shaderLights))
	{
		RenderDevice::Set_Terrain_Height_Field(m_map->getDataPtr(), m_map->getXExtent(), m_map->getYExtent(),
			m_map->getBorderSize(), MAP_XY_FACTOR, MAP_HEIGHT_SCALE);
	}
	if (m_map)
	{
		RenderTerrainLighting lighting;
		lighting.ambient[0] = TheGlobalData->m_terrainAmbient[0].red;
		lighting.ambient[1] = TheGlobalData->m_terrainAmbient[0].green;
		lighting.ambient[2] = TheGlobalData->m_terrainAmbient[0].blue;
		lighting.count = 0;
		for (Int lightIndex = 0; lightIndex < TheGlobalData->m_numGlobalLights && lightIndex < 3; ++lightIndex)
		{
			const Coord3D &pos = TheGlobalData->m_terrainLightPos[lightIndex];
			lighting.ray[lightIndex][0] = -pos.x;
			lighting.ray[lightIndex][1] = -pos.y;
			lighting.ray[lightIndex][2] = -pos.z;
			lighting.diffuse[lightIndex][0] = TheGlobalData->m_terrainDiffuse[lightIndex].red;
			lighting.diffuse[lightIndex][1] = TheGlobalData->m_terrainDiffuse[lightIndex].green;
			lighting.diffuse[lightIndex][2] = TheGlobalData->m_terrainDiffuse[lightIndex].blue;
			lighting.count = lightIndex + 1;
		}
		RenderDevice::Set_Terrain_Lighting(lighting);
	}
	if (terrainDetail)
	{
		RenderDevice::Set_Terrain_Detail(true);
		RenderDevice::Set_Terrain_Tessellation(true);
	}
	if (shaderLights)
		RenderDevice::Enable_Terrain_Dynamic_Lights(true);
	// Tiles outside the view are skipped. While shadow maps record the casters they are only recorded, not drawn:
	// a hill outside the view can still shadow the visible ground. The tile bounds are in world space, so this needs
	// an identity transform; the terrain is never moved and its identity flag is only set by Set_Transform, so the
	// matrix itself is compared.
	Bool cullTiles = TRUE;
	for (Int row = 0; row < 3 && cullTiles; ++row)
		for (Int col = 0; col < 4; ++col)
			if (tm[row][col] != ((row == col) ? 1.0f : 0.0f))
				cullTiles = FALSE;
	const Bool shadowMaps = RenderDevice::Shadow_Maps_Active();
 	for (pass=0; pass<devicePasses; pass++) {
#ifdef TIMING_TESTS
#endif
		if (!doMultiPassWireFrame)	//multi-pass wireframe doesn't use regular shaders.
		{
 			if (m_disableTextures ) {
 				RenderDevice::Set_Shader(ShaderClass::_PresetOpaque2DShader);
 				RenderDevice::Set_Texture(0,nullptr);
   			} else {
 				W3DShaderManager::setShader(st, pass);
			}
		}

		for (j=0; j<m_numVBTilesY; j++)
			for (i=0; i<m_numVBTilesX; i++)
			{
				static int count = 0;
				count++;
				const Bool casterOnly = cullTiles && !isTileVisible(j*m_numVBTilesX+i, &rinfo.Camera);
				if (casterOnly && !shadowMaps) {
					continue;
				}
				Int numPolys = VERTEX_BUFFER_TILE_LENGTH*VERTEX_BUFFER_TILE_LENGTH*2;
				Int numVertex = (VERTEX_BUFFER_TILE_LENGTH*2)*(VERTEX_BUFFER_TILE_LENGTH*2);
				if (HALF_RES_MESH) {
					numPolys /= 4;
					numVertex /= 4;
				}
				RenderDevice::Set_Vertex_Buffer(m_vertexBufferTiles[j*m_numVBTilesX+i]);
				if (Is_Hidden() == 0) {
					if (casterOnly)
						RenderDevice::Set_Shadow_Caster_Only(true);
					RenderDevice::Draw_Triangles(	0,static_cast<unsigned short>(numPolys), 0,	static_cast<unsigned short>(numVertex));
					if (casterOnly)
						RenderDevice::Set_Shadow_Caster_Only(false);
				}

			}
	}		

	if (terrainDetail)
	{
		RenderDevice::Set_Terrain_Tessellation(false);
		RenderDevice::Set_Terrain_Detail(false);
	}
	if (shaderLights)
		RenderDevice::Enable_Terrain_Dynamic_Lights(false);

	if (!doMultiPassWireFrame)
	{
		if (pass)	//shader was applied at least once?
 			W3DShaderManager::resetShader(st);

		//Draw feathered shorelines
		renderShoreLines(&rinfo.Camera);

		//Do additional pass over any tiles that have 3 textures blended together.
		if (TheGlobalData->m_use3WayTerrainBlends)
			renderExtraBlendTiles();

		Int yCoordMin = m_map->getDrawOrgY();
		Int yCoordMax = m_y+m_map->getDrawOrgY()-1;
		Int xCoordMin = m_map->getDrawOrgX();
		Int xCoordMax = m_x+m_map->getDrawOrgX()-1;	
	#ifdef TEST_CUSTOM_EDGING
		// Draw edging just before last pass.
		RenderDevice::Set_Texture(0,nullptr);
		RenderDevice::Set_Texture(1,nullptr);
		m_stageTwoTexture->restore();
		m_customEdging->drawEdging(m_map, xCoordMin, xCoordMax, yCoordMin, yCoordMax, 
			m_stageZeroTexture, doCloud?m_stageTwoTexture:nullptr, TheGlobalData->m_useLightMap?m_stageThreeTexture:nullptr);
	#endif
	#ifdef DO_ROADS
		RenderDevice::Set_Texture(0,nullptr);
		RenderDevice::Set_Texture(1,nullptr);
		m_stageTwoTexture->restore();

		ShaderClass::Invalidate();
		if (!ShaderClass::Is_Backface_Culling_Inverted()) {
			RenderDevice::Set_Material(m_vertexMaterialClass);
			if (Scene) {
				RTS3DScene *pMyScene = static_cast<RTS3DScene *>(Scene);
				RefRenderObjListIterator pDynamicLightsIterator(pMyScene->getDynamicLights());
				m_roadBuffer->drawRoads(&rinfo.Camera, doCloud?m_stageTwoTexture:nullptr, TheGlobalData->m_useLightMap?m_stageThreeTexture:nullptr,
					m_disableTextures,xCoordMin-m_map->getBorderSizeInline(), xCoordMax-m_map->getBorderSizeInline(), yCoordMin-m_map->getBorderSizeInline(), yCoordMax-m_map->getBorderSizeInline(), &pDynamicLightsIterator);
			}
		}
	#endif
	// Terrain and roads are done; props, bridges and objects follow. The render backend records the
	// terrain depth and shadow casters here.
	if (!ShaderClass::Is_Backface_Culling_Inverted())
		RenderDevice::Scene_Terrain_Done();
	if (m_propBuffer) {
		m_propBuffer->drawProps(rinfo);
	}
	#ifdef DO_SCORCH
		RenderDevice::Set_Texture(0,nullptr);
		RenderDevice::Set_Texture(1,nullptr);
		m_stageTwoTexture->restore();

		ShaderClass::Invalidate();
		if (!ShaderClass::Is_Backface_Culling_Inverted()) {
			drawScorches();
		}
	#endif
		RenderDevice::Set_Texture(0,nullptr);
		RenderDevice::Set_Texture(1,nullptr);
		m_stageTwoTexture->restore();
		ShaderClass::Invalidate();
		RenderDevice::Apply_Render_State_Changes();

		m_bridgeBuffer->drawBridges(&rinfo.Camera, m_disableTextures, doCloud?m_stageTwoTexture:nullptr);

		if (TheTerrainTracksRenderObjClassSystem)
			TheTerrainTracksRenderObjClassSystem->flush();

		if (m_shroud && rinfo.Additional_Pass_Count())
		{
			// The shroud pass tests depth EQUAL, so it is tessellated like the base pass.
			const Bool shroudTess = !ShaderClass::Is_Backface_Culling_Inverted();
			rinfo.Peek_Additional_Pass(0)->Install_Materials();
			if (shroudTess)
				RenderDevice::Set_Terrain_Tessellation(true);
			renderTerrainPass(&rinfo.Camera, true);
			if (shroudTess)
				RenderDevice::Set_Terrain_Tessellation(false);
			rinfo.Peek_Additional_Pass(0)->UnInstall_Materials();
		}

		ShaderClass::Invalidate();
		RenderDevice::Apply_Render_State_Changes();
	}
	else
			m_bridgeBuffer->drawBridges(&rinfo.Camera, m_disableTextures, m_stageTwoTexture);

  if ( m_waypointBuffer ) 
	  m_waypointBuffer->drawWaypoints(rinfo);

	m_bibBuffer->renderBibs();

	// We do some custom blending, so tell the shader class to reset everything.
	RenderDevice::Set_Texture(0,nullptr);
	RenderDevice::Set_Texture(1,nullptr);
	m_stageTwoTexture->restore();
	ShaderClass::Invalidate();
	RenderDevice::Set_Material(nullptr);

}



///Performs additional terrain rendering pass, blending in the black shroud texture.
void HeightMapRenderObjClass::renderTerrainPass(CameraClass *pCamera, Bool cullTiles)
{
	RenderDevice::Set_Transform(WW3D_TS_WORLD,Matrix3D(1));

	//Apply the shader and material
	
	RenderDevice::Set_Index_Buffer(m_indexBuffer,0);

	if (m_gpuDynamicLights || m_gpuStaticLighting)
		RenderDevice::Enable_Terrain_Dynamic_Lights(true);
	for (Int j=0; j<m_numVBTilesY; j++)
		for (Int i=0; i<m_numVBTilesX; i++)
		{
			static int count = 0;
			count++;
			if (cullTiles && !isTileVisible(j*m_numVBTilesX+i, pCamera)) {
				continue;
			}
			Int numPolys = VERTEX_BUFFER_TILE_LENGTH*VERTEX_BUFFER_TILE_LENGTH*2;
			Int numVertex = (VERTEX_BUFFER_TILE_LENGTH*2)*(VERTEX_BUFFER_TILE_LENGTH*2);
			if (HALF_RES_MESH) {
				numPolys /= 4;
				numVertex /= 4;
			}
			RenderDevice::Set_Vertex_Buffer(m_vertexBufferTiles[j*m_numVBTilesX+i]);
			if (Is_Hidden() == 0) {
				RenderDevice::Draw_Triangles(	0,static_cast<unsigned short>(numPolys), 0,	static_cast<unsigned short>(numVertex));
			}
		}
	if (m_gpuDynamicLights || m_gpuStaticLighting)
		RenderDevice::Enable_Terrain_Dynamic_Lights(false);
}

//=============================================================================
// HeightMapRenderObjClass::renderExtraBlendTiles
//=============================================================================
/** Renders an additoinal terrain pass including only those tiles which have more than 2 textures
blended together.  Used primarily for corner cases where 3 different textures meet.*/
void HeightMapRenderObjClass::renderExtraBlendTiles(void)
{
	Int vertexCount = 0;
	Int indexCount = 0;
	Int xExtent = m_map->getXExtent();
	Int border = m_map->getBorderSizeInline();
	static Int maxBlendTiles = DEFAULT_MAX_FRAME_EXTRABLEND_TILES;

	m_numVisibleExtraBlendTiles = 0;

	if (!m_numExtraBlendTiles)
		return;	//nothing to draw

	if (maxBlendTiles > 10000)	//we can only fit about 10000 tiles into a single VB.
		maxBlendTiles = 10000;

	DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DEVICE,RENDER_FVF_XYZNDUV2,static_cast<unsigned short>(maxBlendTiles*4));
	DynamicIBAccessClass ib_access(BUFFER_TYPE_DYNAMIC_DEVICE,static_cast<unsigned short>(maxBlendTiles*6));
	{

		DynamicVBAccessClass::WriteLockClass lock(&vb_access);
		VertexFormatXYZNDUV2* vb= lock.Get_Formatted_Vertex_Array();
		DynamicIBAccessClass::WriteLockClass lockib(&ib_access);
		UnsignedShort *ib=lockib.Get_Index_Array();

		if (!vb || !ib) return;

		const UnsignedByte* data = m_map->getDataPtr();

		//Loop over visible terrain and extract all the tiles that need extra blend
		Int drawEdgeY=m_map->getDrawOrgY()+m_map->getDrawHeight()-1;
		Int drawEdgeX=m_map->getDrawOrgX()+m_map->getDrawWidth()-1;
		if (drawEdgeX > (m_map->getXExtent()-1))
			drawEdgeX = m_map->getXExtent()-1;
		if (drawEdgeY > (m_map->getYExtent()-1))
			drawEdgeY = m_map->getYExtent()-1;
		Int drawStartX=m_map->getDrawOrgX();
		Int drawStartY=m_map->getDrawOrgY();

		try {
		for (Int j=0; j<m_numExtraBlendTiles; j++)
		{
			if (vertexCount >= (maxBlendTiles*4))
				break;	//no room in vertex buffer

			Real U[4],V[4];
			UnsignedByte alpha[4];
			Bool flipState,cliffState;
			Int x = m_extraBlendTilePositions[j] & 0xffff;
			Int y = m_extraBlendTilePositions[j] >> 16;

			if (x >= drawStartX && x < drawEdgeX &&
				y >= drawStartY && y < drawEdgeY &&
				m_map->getExtraAlphaUVData(x,y,U,V,alpha,&flipState, &cliffState))
			{	//this tile is inside visible region and has 3rd blend layer.

				Int idx = x+y*xExtent;

				Real p0=data[idx]*MAP_HEIGHT_SCALE;
				Real p1=data[idx+1]*MAP_HEIGHT_SCALE;
				Real p2=data[idx + 1 + xExtent]*MAP_HEIGHT_SCALE;
				Real p3=data[idx + xExtent]*MAP_HEIGHT_SCALE;
				if (cliffState && abs(p0-p2) > abs(p1-p3))	//cliffs sometimes force a flip
					flipState = TRUE;

				vb->x=(x-border)*MAP_XY_FACTOR;	 
				vb->y=(y-border)*MAP_XY_FACTOR;
				vb->z=p0;
				vb->nx=0;
				vb->ny=0;
				vb->nz=0;
				vb->diffuse=(alpha[0]<<24)|(m_gpuStaticLighting ? 0x00ffffff : (getStaticDiffuse(x,y) & 0x00ffffff));
				vb->u1=U[0];
				vb->v1=V[0];
				vb->u2=0;
				vb->v2=0;
				vb++;

				vb->x=(x+1-border)*MAP_XY_FACTOR;	 
				vb->y=(y-border)*MAP_XY_FACTOR;
				vb->z=p1;
				vb->nx=0;
				vb->ny=0;
				vb->nz=0;
				vb->diffuse=(alpha[1]<<24)|(m_gpuStaticLighting ? 0x00ffffff : (getStaticDiffuse(x+1,y) & 0x00ffffff));
				vb->u1=U[1];
				vb->v1=V[1];
				vb->u2=0;
				vb->v2=0;
				vb++;

				vb->x=(x+1-border)*MAP_XY_FACTOR;	 
				vb->y=(y+1-border)*MAP_XY_FACTOR;
				vb->z=p2;
				vb->nx=0;
				vb->ny=0;
				vb->nz=0;
				vb->diffuse=(alpha[2]<<24)|(m_gpuStaticLighting ? 0x00ffffff : (getStaticDiffuse(x+1,y+1) & 0x00ffffff));
				vb->u1=U[2];
				vb->v1=V[2];
				vb->u2=0;
				vb->v2=0;
				vb++;

				vb->x=(x-border)*MAP_XY_FACTOR;	 
				vb->y=(y+1-border)*MAP_XY_FACTOR;
				vb->z=p3;
				vb->nx=0;
				vb->ny=0;
				vb->nz=0;
				vb->diffuse=(alpha[3]<<24)|(m_gpuStaticLighting ? 0x00ffffff : (getStaticDiffuse(x,y+1) & 0x00ffffff));
				vb->u1=U[3];
				vb->v1=V[3];
				vb->u2=0;
				vb->v2=0;
				vb++;
				
				if (flipState)
				{
					ib[0]=static_cast<UnsignedShort>(1+vertexCount);
					ib[1]=static_cast<UnsignedShort>(3+vertexCount);
					ib[2]=static_cast<UnsignedShort>(0+vertexCount);
					ib[3]=static_cast<UnsignedShort>(1+vertexCount);
					ib[4]=static_cast<UnsignedShort>(2+vertexCount);
					ib[5]=static_cast<UnsignedShort>(3+vertexCount);
				}
				else
				{
					ib[0]=static_cast<UnsignedShort>(0+vertexCount);
					ib[1]=static_cast<UnsignedShort>(2+vertexCount);
					ib[2]=static_cast<UnsignedShort>(3+vertexCount);
					ib[3]=static_cast<UnsignedShort>(0+vertexCount);
					ib[4]=static_cast<UnsignedShort>(1+vertexCount);
					ib[5]=static_cast<UnsignedShort>(2+vertexCount);
				}
				ib += 6;
				vertexCount +=4;
				indexCount +=6;
			}//tile has 3rd blend layer and is visible
		}	//for all extre blend tiles
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}
	}//unlock vertex buffer

	if (vertexCount)
	{
		//Check if we couldn't fit all blend tiles into vertex buffer so we can enlarge it for next frame.
		if (vertexCount == (maxBlendTiles*4))
			maxBlendTiles += 16;	//enlarge by 16 to reduce trashing.
		
		ShaderClass::Invalidate();	//invalidate to force shader to reset since we directly changed states
		RenderDevice::Set_Index_Buffer(ib_access,0);
		RenderDevice::Set_Vertex_Buffer(vb_access);
		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		RenderDevice::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);
		ShaderClass shader=ShaderClass::_PresetOpaqueShader;
		shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);	//disable writes to z
		RenderDevice::Set_Shader(shader);

		if (TheGlobalData->m_use3WayTerrainBlends == 2)
		{
			shader.Set_Primary_Gradient(ShaderClass::GRADIENT_DISABLE);	//disable lighting.
			shader.Set_Texturing(ShaderClass::TEXTURING_DISABLE);		//disable texturing.
			RenderDevice::Set_Shader(shader);
			RenderDevice::Set_Texture(0,nullptr);	//debug mode which draws terrain tiles in white.
			if (Is_Hidden() == 0) {
				RenderDevice::Draw_Triangles(	0,static_cast<unsigned short>(indexCount/3), 0,	static_cast<unsigned short>(vertexCount));	//draw a quad, 2 triangles, 4 verts
				m_numVisibleExtraBlendTiles += indexCount/6;
			}
		}
		else
		{
			W3DShaderManager::setTexture(0,m_stageOneTexture);
			W3DShaderManager::setTexture(1,m_stageTwoTexture);	//cloud
			W3DShaderManager::setTexture(2,m_stageThreeTexture);	//noise/lightmap

			W3DShaderManager::ShaderTypes st = W3DShaderManager::ST_ROAD_BASE;

			Bool doCloud = TheGlobalData->m_useCloudMap;
			if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT) {
				doCloud = false;
			}

			if (TheGlobalData->m_useLightMap && doCloud)
 			{	
				st = W3DShaderManager::ST_ROAD_BASE_NOISE12;
 			}
 			else if (TheGlobalData->m_useLightMap)
 			{	//lightmap only
 				st = W3DShaderManager::ST_ROAD_BASE_NOISE2;
 			}
 			else if (doCloud)
 			{	//cloudmap only
 				st = W3DShaderManager::ST_ROAD_BASE_NOISE1;
 			}

			Int devicePasses=W3DShaderManager::getShaderPasses(st);

			// The third layer also lives in the terrain atlas (stage 0); with the road pixel shader
			// (roadnoise2.ps, clouds and light map on) it gets the detail layer.
			const Bool terrainDetail = !ShaderClass::Is_Backface_Culling_Inverted();
			if (terrainDetail)
			{
				RenderDevice::Set_Terrain_Detail(true);
				RenderDevice::Set_Terrain_Tessellation(true);
			}
			// The third layer covers lit base tiles, so it gets the shader lighting too.
			if (m_gpuDynamicLights || m_gpuStaticLighting)
				RenderDevice::Enable_Terrain_Dynamic_Lights(true);
			for (Int pass=0; pass < devicePasses; pass++)
			{
				W3DShaderManager::setShader(st, pass);
				if (Is_Hidden() == 0) {
					RenderDevice::Draw_Triangles(	0,static_cast<unsigned short>(indexCount/3), 0,	static_cast<unsigned short>(vertexCount));	//draw a quad, 2 triangles, 4 verts
					m_numVisibleExtraBlendTiles += indexCount/6;
				}
			}
			if (m_gpuDynamicLights || m_gpuStaticLighting)
				RenderDevice::Enable_Terrain_Dynamic_Lights(false);
			if (terrainDetail)
			{
				RenderDevice::Set_Terrain_Tessellation(false);
				RenderDevice::Set_Terrain_Detail(false);
			}
			W3DShaderManager::resetShader(st);
		}
  }
}
