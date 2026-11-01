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

// WorldHeightMap.cpp
// Class to encapsulate height map.
// Author: John Ahlquist, April 2001

#define INSTANTIATE_WELL_KNOWN_KEYS

#include "windows.h"
#include "formatinto.h"
#include "stdlib.h"
#include <string.h>
#include "Common/STLTypedefs.h"

#include "Common/DataChunk.h"
//#include "Common/GameFileSystem.h"
#include "Common/FileSystem.h" // for LOAD_TEST_ASSETS
#include "Common/GlobalData.h"
#include "Common/GameEngine.h"	// GameEngine_RunnerLog
#include "Common/MapReaderWriterInfo.h"
#include "Common/TerrainTypes.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"

#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/SidesList.h"

#include "GameClient/Display.h"
#include "GameClient/View.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/TileData.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/TerrainTileImage.h"
#include "Engine/TexturePackage.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "WW3D2/Common/renderdevice.h"	// RenderDevice::Set_Terrain_Detail_Table
#include <vector>

#include "Common/file.h"
#include "formatinto.h"

// Limits for values read from map files; the engine's own maps stay far below them.
constexpr Int MAX_MAP_EXTENT = 16384;
constexpr Int MAX_MAP_BOUNDARIES = 1024;

// A tile index packs four cells per source tile (index >> 2).
static Bool isValidTileNdx(Int tileNdx)
{
	return tileNdx >= 0 && (tileNdx >> 2) < NUM_SOURCE_TILES;
}

// Tile counts only bound loops over the source tile arrays.
static Int clampTileCount(Int count)
{
	DEBUG_ASSERTCRASH(count >= 0 && count <= NUM_SOURCE_TILES, ("Tile count %d out of range.", count));
	return count < 0 ? 0 : (count > NUM_SOURCE_TILES ? NUM_SOURCE_TILES : count);
}

// firstTile..firstTile+numTiles and the width*width block placed in the terrain texture
// must stay inside the source tile arrays; a class outside them is dropped (no tiles).
static void validateTileRange(TXTextureClass &tc)
{
	const Bool valid = tc.firstTile >= 0 && tc.numTiles >= 0 && tc.width >= 0 && tc.width <= NUM_SOURCE_TILES
		&& tc.firstTile + tc.numTiles <= NUM_SOURCE_TILES && tc.firstTile + tc.width*tc.width <= NUM_SOURCE_TILES;
	DEBUG_ASSERTCRASH(valid, ("Texture class tile range out of bounds."));
	if (!valid) {
		tc.firstTile = 0;
		tc.numTiles = 0;
		tc.width = 0;
	}
}

#define K_OBSOLETE_HEIGHT_MAP_VERSION 8

constexpr auto PATHFIND_CLIFF_SLOPE_LIMIT_F = 9.8f;

// -----------------------------------------------------------
static AsciiString validateName(AsciiString n, Int flags)
{

	return n;

}

/* ********* GDIFileStream class ****************************/
class GDIFileStream : public InputStream
{
protected:
	File* m_file;
public:
	GDIFileStream(File* pFile):m_file(pFile) {};
	virtual Int read(void *pData, Int numBytes) override {
			return(m_file->read(pData, numBytes));
	};
};


/* ********* MapObject class ****************************/
/*static*/ MapObject *MapObject::TheMapObjectListPtr = nullptr;
/*static*/ Dict MapObject::TheWorldDict;

MapObject::MapObject(Coord3D loc, AsciiString name, Real angle, Int flags, const Dict* props,
										 const ThingTemplate *thingTemplate )
{
	m_objectName = validateName( name, flags );
	m_thingTemplate = thingTemplate;
	m_nextMapObject = nullptr;
	m_location = loc;
	m_angle = normalizeAngle(angle);
	m_color = (0xff)<<8; // Bright green.
	m_flags = flags;
	m_renderObj = nullptr;
	m_shadowObj = nullptr;
	m_runtimeFlags = 0;
	// Note - do NOT set TheKey_objectSelectable on creation - allow it to follow the .ini value unless specified by user action.  jba. [3/20/2003]
	if (props)
	{
		m_properties = *props;
	} 
	else 
	{
		m_properties.setInt(TheKey_objectInitialHealth, 100);
		m_properties.setBool(TheKey_objectEnabled, true);
		m_properties.setBool(TheKey_objectIndestructible, false);
		m_properties.setBool(TheKey_objectUnsellable, false);
		m_properties.setBool(TheKey_objectPowered, true);
		m_properties.setBool(TheKey_objectRecruitableAI, true);
		m_properties.setBool(TheKey_objectTargetable, false );
	}

	for( Int i = 0; i < BRIDGE_MAX_TOWERS; ++i )
		setBridgeRenderObject( static_cast<BridgeTowerType>(i), nullptr );

}	


MapObject::~MapObject(void)
{
	setRenderObj(nullptr);
	setShadowObj(nullptr);
	if (m_nextMapObject) {
		MapObject *cur = m_nextMapObject;
		MapObject *next;
		while (cur) {
			next = cur->getNext();
			cur->setNextMap(nullptr); // prevents recursion. 
			deleteInstanceOf(cur);
			cur = next;
		}
	}
	for( Int i = 0; i < BRIDGE_MAX_TOWERS; ++i )	
		setBridgeRenderObject( static_cast<BridgeTowerType>(i), nullptr );

}

MapObject *MapObject::duplicate(void)
{
	MapObject *pObj = newInstance( MapObject)(m_location, m_objectName, m_angle, m_flags, &m_properties, m_thingTemplate);
	pObj->setColor(getColor());
	pObj->m_runtimeFlags = m_runtimeFlags;
	return pObj;
}

void MapObject::setRenderObj(RenderObjClass *pObj)
{
	REF_PTR_SET(m_renderObj, pObj);
}

void MapObject::setBridgeRenderObject( BridgeTowerType type, RenderObjClass* renderObj )
{

	if( type >= 0 && type < BRIDGE_MAX_TOWERS )
		REF_PTR_SET( m_bridgeTowers[ type ], renderObj );

}

RenderObjClass* MapObject::getBridgeRenderObject( BridgeTowerType type )
{

	if( type >= 0 && type < BRIDGE_MAX_TOWERS )
		return m_bridgeTowers[ type ];
	return nullptr;

}

void MapObject::validate(void)
{
	verifyValidTeam();
	verifyValidUniqueID();
}

void MapObject::verifyValidTeam(void)
{
	// if this map object has a valid team, then do nothing.
	// if it has an invalid team, the place it on the default neutral team, (by clearing the 
	// existing team name.)
	Bool exists;
	AsciiString teamName = getProperties()->getAsciiString(TheKey_originalOwner, &exists);
	if (exists) {
		Bool valid = false;

		int numSides = TheSidesList->getNumTeams();

		for (int i = 0; i < numSides; ++i) {
			TeamsInfo *teamInfo = TheSidesList->getTeamInfo(i);
			if (!teamInfo) {
				continue;
			}
			
			Bool itBetter;
			AsciiString testAgainstTeamName = teamInfo->getDict()->getAsciiString(TheKey_teamName, &itBetter);
			if (itBetter) {
				if (testAgainstTeamName.compare(teamName) == 0) {
					valid = true;
				}
			}
		}

		if (!valid) {
			getProperties()->remove(TheKey_originalOwner);
		}
	}
}

void MapObject::verifyValidUniqueID(void)
{
	Bool exists;
	AsciiString uniqueID = getProperties()->getAsciiString(TheKey_uniqueID, &exists);
	MapObject *obj = MapObject::getFirstMapObject();

	// -1 is the sentinel
	int highestIndex = -1;

	while (obj) {
		if (obj == this) {
			// the first object is THIS OBJECT, cause we've already been added. 
			obj = obj->getNext();
			continue;
		}

		if (obj->isWaypoint()) {
			// waypoints throw this off. Sad but true. :-(
			obj = obj->getNext();
			continue;
		}

		Bool iterateExists;
		AsciiString tempStr = obj->getProperties()->getAsciiString(TheKey_uniqueID, &iterateExists);
		const char* lastSpace = tempStr.reverseFind(' ');

		int testIndex = -1; 
		if (lastSpace) {
			testIndex = atoi(lastSpace);
		}

		if (testIndex > highestIndex) {
			highestIndex = testIndex;
		}
		break;
	}

	int indexOfThisObject = highestIndex + 1;
	
	const char* thingName;
	if (getThingTemplate()) {
		thingName = getThingTemplate()->getName().str();
	} else if (isWaypoint()) {
		thingName = getWaypointName().str();
	} else {
		thingName = getName().str();
	}
	const char* pName = thingName;

	while (*thingName) {
		if ((*thingName) == '/') {
			pName = thingName + 1;
		}
		++thingName;
	}

	AsciiString newID;
	if (isWaypoint()) {
		newID.format("%s", pName);
	} else {
		newID.format("%s %d", pName, indexOfThisObject);
	}
	getProperties()->setAsciiString(TheKey_uniqueID, newID);
}

void MapObject::fastAssignAllUniqueIDs(void)
{
	// here's what we do. Take all of them, push them onto a stack. Then, pop each one, setting its id.
	// should be much faster than what we currently do.

	MapObject *pMapObj = getFirstMapObject();

	std::stack<MapObject*> objStack;
	Int actualNumObjects = 0;
	
	while (pMapObj) {
		++actualNumObjects;
		objStack.push(pMapObj);
		pMapObj = pMapObj->getNext();
	}

	Int indexOfThisObject = 0;
	while (actualNumObjects) {
		MapObject *obj = objStack.top();
		

		const char* thingName;
		if (obj->getThingTemplate()) {
			thingName = obj->getThingTemplate()->getName().str();
		} else if (obj->isWaypoint()) {
			thingName = obj->getWaypointName().str();
		} else {
			thingName = obj->getName().str();
		}
		const char* pName = thingName;

		while (*thingName) {
			if ((*thingName) == '/') {
				pName = thingName + 1;
			}
			++thingName;
		}

		AsciiString newID;
		if (obj->isWaypoint()) {
			newID.format("%s", pName);
		} else {
			newID.format("%s %d", pName, indexOfThisObject);
		}

		obj->getProperties()->setAsciiString(TheKey_uniqueID, newID);
		objStack.pop();
	
		++indexOfThisObject;
		--actualNumObjects;
	}
}



void MapObject::setThingTemplate(const ThingTemplate *thing)
{
	m_thingTemplate = thing;
	m_objectName = thing->getName();
}


void MapObject::setName(AsciiString name)
{
	m_objectName = name;
}

WaypointID MapObject::getWaypointID() { return static_cast<WaypointID>(getProperties()->getInt(TheKey_waypointID)); }
AsciiString MapObject::getWaypointName() { return getProperties()->getAsciiString(TheKey_waypointName); }
void MapObject::setWaypointID(Int i) { getProperties()->setInt(TheKey_waypointID, i); }
void MapObject::setWaypointName(AsciiString n) { getProperties()->setAsciiString(TheKey_waypointName, n); }

/*static */ Int MapObject::countMapObjectsWithOwner(const AsciiString& n)
{
	Int count = 0;
	for (MapObject *pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext()) 
	{
		if (pMapObj->getProperties()->getAsciiString(TheKey_originalOwner) == n)
			++count;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
const ThingTemplate *MapObject::getThingTemplate( void ) const
{
	if (m_thingTemplate)
		return static_cast<const ThingTemplate*>(m_thingTemplate->getFinalOverride()); 
	
	return nullptr;
}


/* ********* WorldHeightMap class ****************************/


//
// WorldHeightMap destructor .
//
WorldHeightMap::~WorldHeightMap(void)
{
	if (m_data) {
		delete[] m_data;
		m_data = nullptr;
	}
	if (m_tileNdxes) {
		delete[] m_tileNdxes;
		m_tileNdxes = nullptr;
	}
	if (m_blendTileNdxes) {
		delete[] m_blendTileNdxes;
		m_blendTileNdxes = nullptr;
	}
	if (m_extraBlendTileNdxes) {
		delete[] m_extraBlendTileNdxes;
		m_extraBlendTileNdxes = nullptr;
	}
	if (m_cliffInfoNdxes) {
		delete[] m_cliffInfoNdxes;
		m_cliffInfoNdxes = nullptr;
	}
	if (m_cellFlipState)
	{	delete[] m_cellFlipState;
		m_cellFlipState = nullptr;
	}
	if (m_seismicUpdateFlag)
	{	delete[] m_seismicUpdateFlag;
		m_seismicUpdateFlag = nullptr;
	}
	if (m_seismicZVelocities)
	{	delete[] m_seismicZVelocities;
		m_seismicZVelocities = nullptr;
	}
	if (m_cellCliffState)
	{	delete[] m_cellCliffState;
		m_cellCliffState = nullptr;
	}
	int i;
	for (i=0; i<NUM_SOURCE_TILES; i++) {
		REF_PTR_RELEASE(m_sourceTiles[i]);
		REF_PTR_RELEASE(m_edgeTiles[i]);
		REF_PTR_RELEASE(m_reliefTiles[i]);
		REF_PTR_RELEASE(m_heightTiles[i]);
	}
	if (m_terrainReliefTex) {
		RenderDevice::Set_Terrain_Relief_Atlas(m_terrainReliefTex->Peek_D3D_Texture(), false);
		REF_PTR_RELEASE(m_terrainReliefTex);
	}
	if (m_terrainHeightTex) {
		RenderTerrainTessData drop = {};
		drop.atlas = m_terrainHeightTex->Peek_D3D_Texture();
		RenderDevice::Set_Terrain_Tess_Data(drop, false);
		REF_PTR_RELEASE(m_terrainHeightTex);
	}
	REF_PTR_RELEASE(m_terrainTex);
	REF_PTR_RELEASE(m_alphaTerrainTex);
	REF_PTR_RELEASE(m_alphaEdgeTex);
}

void WorldHeightMap::freeListOfMapObjects(void)
{
	if (MapObject::TheMapObjectListPtr) 
	{
		deleteInstanceOf(MapObject::TheMapObjectListPtr);
		MapObject::TheMapObjectListPtr = nullptr;
	}
	MapObject::getWorldDict()->clear();
}


/**
 Only a window of NORMAL_DRAW_WIDTH x NORMAL_DRAW_HEIGHT cells around the camera is put into
 vertex buffers (HeightMapRenderObjClass::updateCenter). That window fits a 4:3 view at the normal
 maximum height; with the wider Hor+ view or a raised zoom-out limit (View::getGameZoomOutFactor) the camera
 sees more cells and the rest would stay black. So the width grows by both factors (the visible
 width grows linearly with the camera height), rounded up to whole vertex buffer tiles
 (VERTEX_BUFFER_TILE_LENGTH = 32 cells, plus one border vertex as in 129 = 4*32+1).
*/
Int WorldHeightMap::getDefaultDrawWidth(void)
{
	Int width = NORMAL_DRAW_WIDTH;
	const Real horPlus = TheDisplay ? TheDisplay->getHorPlusFactor() : 1.0f;
	const Real zoomOut = View::getGameZoomOutFactor();
	if (horPlus > 1.0f || zoomOut > 1.0f)
	{
		const Int tileLength = VERTEX_BUFFER_TILE_LENGTH;
		const Real cells = static_cast<Real>(NORMAL_DRAW_WIDTH - 1) * (horPlus > 1.0f ? horPlus : 1.0f) * zoomOut;
		width = (static_cast<Int>(ceil(cells / static_cast<Real>(tileLength)))) * tileLength + 1;
	}
	return width;
}

/**
 Square draw window of at least DEEP_DRAW_SIZE cells. A tilted camera (scripted camera moves,
 far clip plane at 12000) sees much deeper, and when rotated the wide screen axis lies along
 world Y; a smaller window would leave black areas there.
*/
Int WorldHeightMap::getDefaultDrawSize(void)
{
	const Int width = getDefaultDrawWidth();
	return (width > DEEP_DRAW_SIZE) ? width : DEEP_DRAW_SIZE;
}

/**
 WorldHeightMap - create a new height map for class WorldHeightMap.
 Note that there is 1 m_numBlendedTiles, which is the implied
 transparent tile for non-blended tiles.
*/
WorldHeightMap::WorldHeightMap():
	m_width(0), m_height(0),  m_dataSize(0), m_data(nullptr), m_cellFlipState(nullptr), m_seismicUpdateFlag(nullptr), m_seismicZVelocities(nullptr),
	m_drawOriginX(0), m_drawOriginY(0), 
	m_numTextureClasses(0),	
	m_drawWidthX(getDefaultDrawSize()), m_drawHeightY(getDefaultDrawSize()),
	m_tileNdxes(nullptr), m_blendTileNdxes(nullptr), m_extraBlendTileNdxes(nullptr), m_cliffInfoNdxes(nullptr),
	m_terrainTexHeight(1), m_alphaTexHeight(1),	m_cellCliffState(nullptr),
#ifdef EVAL_TILING_MODES
	m_tileMode(TILE_4x4),
#endif
	m_numCliffInfo(1),
	m_terrainTex(nullptr), m_alphaTerrainTex(nullptr), m_numBitmapTiles(0), m_numBlendedTiles(1)
{
	Int i;
	for (i=0; i<NUM_SOURCE_TILES; i++) {
		m_sourceTiles[i] = nullptr;
		m_edgeTiles[i] = nullptr;
	}

	TheSidesList->validateSides();
}

#ifdef EVAL_TILING_MODES
static Bool ParseFunkyTilingDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *pThis = (WorldHeightMap *)userData;
	*((Int *)&pThis->m_tileMode) = file.readInt();
	return true;
}
#endif

/**
* WorldHeightMap - read a height map from a file.
* Format is  Chunky.
*
*	Input: ChunkInputStream, 
*		
*/
WorldHeightMap::WorldHeightMap(ChunkInputStream *pStrm, Bool logicalDataOnly):
	m_width(0), m_height(0),  m_dataSize(0), m_data(nullptr), m_cellFlipState(nullptr), m_seismicUpdateFlag(nullptr), m_seismicZVelocities(nullptr),
	m_drawOriginX(0),	m_cellCliffState(nullptr), m_drawOriginY(0),
	m_numTextureClasses(0),	
	m_drawWidthX(getDefaultDrawSize()), m_drawHeightY(getDefaultDrawSize()),
	m_tileNdxes(nullptr), m_blendTileNdxes(nullptr), m_extraBlendTileNdxes(nullptr), m_cliffInfoNdxes(nullptr),
	m_terrainTexHeight(1), m_alphaTexHeight(1),
#ifdef EVAL_TILING_MODES
	m_tileMode(TILE_4x4),
#endif
	m_numCliffInfo(1),
	m_terrainTex(nullptr), m_alphaTerrainTex(nullptr), m_numBitmapTiles(0), m_numBlendedTiles(1)
{

	int i;
	for (i=0; i<NUM_SOURCE_TILES; i++) {
		m_sourceTiles[i]=nullptr;
		m_edgeTiles[i]=nullptr;
	}
	if (TheGlobalData && TheGlobalData->m_stretchTerrain) {
		m_drawWidthX=STRETCH_DRAW_WIDTH;
		m_drawHeightY=STRETCH_DRAW_HEIGHT;
	}

	try {
		DataChunkInput file( pStrm );

		if (logicalDataOnly) {
			file.registerParser( AsciiString("HeightMapData"), AsciiString::TheEmptyString, ParseSizeOnlyInChunk );
			file.registerParser( AsciiString("WorldInfo"), AsciiString::TheEmptyString, ParseWorldDictDataChunk );
			file.registerParser( AsciiString("ObjectsList"), AsciiString::TheEmptyString, ParseObjectsDataChunk );
			freeListOfMapObjects(); // just in case.
			file.registerParser( AsciiString("PolygonTriggers"), AsciiString::TheEmptyString, PolygonTrigger::ParsePolygonTriggersDataChunk );
			PolygonTrigger::deleteTriggers(); // just in case.
			TheSidesList->emptySides();
			file.registerParser(AsciiString("SidesList"), AsciiString::TheEmptyString,	SidesList::ParseSidesDataChunk );
		}	else {
			file.registerParser( AsciiString("HeightMapData"), AsciiString::TheEmptyString, ParseHeightMapDataChunk );
			file.registerParser( AsciiString("BlendTileData"), AsciiString::TheEmptyString, ParseBlendTileDataChunk );
#ifdef EVAL_TILING_MODES
			file.registerParser( AsciiString("FUNKY_TILING"), AsciiString::TheEmptyString, ParseFunkyTilingDataChunk );
#endif
			file.registerParser( AsciiString("GlobalLighting"), AsciiString::TheEmptyString, ParseLightingDataChunk );
		}
		if (!file.parse(this)) {
			throw(ERROR_CORRUPT_FILE_FORMAT);
		}
		// patch bad maps. 
		if (!logicalDataOnly) {
			if (m_tileNdxes == nullptr) {
				throw ERROR_CORRUPT_FILE_FORMAT;	// no BlendTileData chunk
			}
			for (i=1; i<m_numBlendedTiles; i++) {
				if (!isValidTileNdx(m_blendedTiles[i].blendNdx)) {
					m_blendedTiles[i].blendNdx = 0;
				}
				if (m_blendedTiles[i].customBlendEdgeClass >= m_numEdgeTextureClasses) {
					m_blendedTiles[i].customBlendEdgeClass = -1;
				}
			}
			for (i=1; i<m_numCliffInfo; i++) {
				if (!isValidTileNdx(m_cliffInfo[i].tileIndex)) {
					m_cliffInfo[i].tileIndex = 0;
				}
			}
			for(i=0; i<m_dataSize; i++) {
				if (!isValidTileNdx(m_tileNdxes[i])) {
					m_tileNdxes[i] = 0;
				}
				if (m_cliffInfoNdxes[i]<0 || m_cliffInfoNdxes[i]>= m_numCliffInfo) {
					m_cliffInfoNdxes[i] = 0;		
				}
				if (m_blendTileNdxes[i]<0 || m_blendTileNdxes[i]>= m_numBlendedTiles) {
					m_blendTileNdxes[i] = 0;		
				}
				if (m_extraBlendTileNdxes[i]<0 || m_extraBlendTileNdxes[i]>= m_numBlendedTiles) {
					m_extraBlendTileNdxes[i] = 0;		
				}
			}
		}
	} catch (...) {
		m_loadFailed = TRUE;
		return;
	}
	if (TheGlobalData && TheGlobalData->m_drawEntireTerrain) {
		m_drawWidthX=m_width;
		m_drawHeightY=m_height;
	}
	if (m_drawWidthX > m_width) {
		m_drawWidthX = m_width;
	}
	if (m_drawHeightY > m_height) {
		m_drawHeightY = m_height;
	}

	TheSidesList->validateSides();
}

/** Optimized version of method to get triangle flip state of a terrain cell.  Use this
*	instead of getAlphaUVData() whenever possible.
*/
Bool WorldHeightMap::getFlipState(Int xIndex, Int yIndex) const
{
	if (xIndex<0 || yIndex<0) return false;
	if (yIndex>=m_height) return false;
	if (xIndex>=m_width) return false;
	if (!m_cellFlipState) return false;
	return m_cellFlipState[yIndex*m_flipStateWidth + (xIndex >> 3)] & (1<<(xIndex&0x7));
}

/** Clears all flip state bits.
*/
void WorldHeightMap::clearFlipStates(void) {
	if (m_cellFlipState) {
		memset(m_cellFlipState,0,m_flipStateWidth*m_height);	//clear all flags
	}
}




//////////////////////////////////////////////////////////////////////////////m_SeismicUpdateFlag
Bool WorldHeightMap::getSeismicUpdateFlag(Int xIndex, Int yIndex) const
{
	if (xIndex<0 || yIndex<0) return false;
	if (yIndex>=m_height) return false;
	if (xIndex>=m_width) return false;
	if (!m_seismicUpdateFlag) return false;
	return m_seismicUpdateFlag[yIndex*m_seismicUpdateWidth + (xIndex >> 3)] & (1<<(xIndex&0x7));
}
void WorldHeightMap::setSeismicUpdateFlag(Int xIndex, Int yIndex, Bool value) 
{
	if (xIndex<0 || yIndex<0) return ;
	if (yIndex>=m_height) return ;
	if (xIndex>=m_width) return ;
	if (!m_seismicUpdateFlag) return ;
	UnsignedByte *curVal = &m_seismicUpdateFlag[yIndex*m_seismicUpdateWidth + (xIndex >> 3)];
	if (value) {
		*curVal |= (1<<(xIndex&0x7));
	}	else {
		*curVal &= ~(1<<(xIndex&0x7));
	}
}
void WorldHeightMap::clearSeismicUpdateFlags(void) 
{
	if (m_seismicUpdateFlag) {
		memset(m_seismicUpdateFlag,0,m_seismicUpdateWidth*m_height);	//clear all flags
	}
}

///////////////////////////////////////////////m_SeismicZVelocities
Real WorldHeightMap::getSeismicZVelocity(Int xIndex, Int yIndex) const
{
	if (xIndex<0 || yIndex<0) return false;
	if (yIndex>=m_height) return false;
	if (xIndex>=m_width) return false;
	if (!m_seismicZVelocities) return false;
	return m_seismicZVelocities[yIndex*m_width + xIndex];
}
void WorldHeightMap::setSeismicZVelocity(Int xIndex, Int yIndex, Real value) 
{
	if (xIndex<0 || yIndex<0) return ;
	if (yIndex>=m_height) return ;
	if (xIndex>=m_width) return ;
	if (!m_seismicZVelocities) return ;
	m_seismicZVelocities[yIndex*m_width + xIndex] = value;
}
void WorldHeightMap::fillSeismicZVelocities( Real value ) 
{
	if (!m_seismicZVelocities) return ;
  for (Int idx = 0; idx < m_width*m_height; ++idx)
    m_seismicZVelocities[idx] = value;
}

Real WorldHeightMap::getBilinearSampleSeismicZVelocity( Int x, Int y)
{
	if ( x < 0 || y < 0 ) return 0;
	if ( y >= m_height ) return 0;
	if ( x >= m_width ) return 0;
	if (!m_seismicZVelocities) return 0;

  Real collector = 0.0f;
  Real divisor = 0.0f;

  collector += m_seismicZVelocities[ y * m_width + x ];
  ++divisor;

  if ( y > 0 )
  {
    collector += m_seismicZVelocities[ (y-1) * m_width + x ];//bottom
    ++divisor;

    if( x > 0 )
    {
      collector += m_seismicZVelocities[ (y-1) * m_width + (x-1) ];//lower left
      ++divisor;
    }
    if ( x < m_width-1 )
    {
      collector += m_seismicZVelocities[ (y-1) * m_width + (x+1) ];//lower right
      ++divisor;
    }
  }
  if ( y < m_height-1 )
  {
    collector += m_seismicZVelocities[ (y+1) * m_width + x ];//top
    ++divisor;

    if( x > 0 )
    {
      collector += m_seismicZVelocities[ (y+1) * m_width + (x-1) ];//upper left
      ++divisor;
    }
    if ( x < m_width-1 )
    {
      collector += m_seismicZVelocities[ (y+1) * m_width + (x+1) ];//upper right
      ++divisor;
    }
  }
  if( x > 0 )
  {
    collector += m_seismicZVelocities[ y * m_width + (x-1) ];//left
    ++divisor;
  }
  if ( x < m_width-1 )
  {
    collector += m_seismicZVelocities[ y * m_width + (x+1) ];//right
    ++divisor;
  }

  collector /= divisor;

  return collector;

}
















/** Get whether the cell is a cliff cell (impassable to ground vehicles).
*/
Bool WorldHeightMap::getCliffState(Int xIndex, Int yIndex) const
{
	if (xIndex<0 || yIndex<0) return false;
	if (yIndex>=m_height) return false;
	if (xIndex>=m_width) return false;
	if (!m_cellCliffState) return false;
	return m_cellCliffState[yIndex*m_flipStateWidth + (xIndex >> 3)] & (1<<(xIndex&0x7));
}

//=============================================================================
// setCliffState
//=============================================================================
/** Sets the cliff state for a given cell. */
//=============================================================================
void WorldHeightMap::setCliffState(Int xIndex, Int yIndex, Bool state) 
{
	if (xIndex<0 || yIndex<0) return;
	if (yIndex>=m_height) return;
	if (xIndex>=m_width) return;
	if (!m_cellCliffState) return;
	UnsignedByte	flagByte = m_cellCliffState[yIndex*m_flipStateWidth + (xIndex >> 3)];
	UnsignedByte flagMask = (static_cast<UnsignedByte>(1<<(xIndex&0x7)));
	if (state) {
		flagByte |= flagMask;
	} else {
		flagByte &= (~flagMask);
	}
	m_cellCliffState[yIndex*m_flipStateWidth + (xIndex >> 3)] = flagByte;
}

Bool WorldHeightMap::ParseWorldDictDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	Dict d = file.readDict();
	*MapObject::getWorldDict() = d;
	Bool exists;
	Int theWeather = MapObject::getWorldDict()->getInt(TheKey_weather, &exists);
	if (exists) {
		TheWritableGlobalData->m_weather = static_cast<Weather>(theWeather);
	}
	return true;
}

/**
* WorldHeightMap::ParseLightingDataChunk - read a global lights chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMap::ParseLightingDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
		TheWritableGlobalData->m_timeOfDay = static_cast<TimeOfDay>(file.readInt());
		Int i;
		GlobalData::TerrainLighting	initLightValues	= { { 0,0,0},{0,0,0},{0,0,-1.0f}};

		// initialize the directions of the lights to not be totally invalid, in case old maps are read
		for (i=0; i<4; i++) {
			for (Int j=0;j<MAX_GLOBAL_LIGHTS; j++) {
				TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j]=initLightValues;
				TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j]=initLightValues;
			}
		}

		for (i=0; i<4; i++) {
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].ambient.red = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].ambient.green = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].ambient.blue = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].diffuse.red = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].diffuse.green = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].diffuse.blue = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].lightPos.x = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].lightPos.y = file.readReal();
			TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][0].lightPos.z = file.readReal();

			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].ambient.red = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].ambient.green = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].ambient.blue = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].diffuse.red = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].diffuse.green = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].diffuse.blue = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].lightPos.x = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].lightPos.y = file.readReal();
			TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][0].lightPos.z = file.readReal();

			if (info->version >= K_LIGHTING_VERSION_2) {
				for (Int j=1; j<3; j++)	//added support for 2 extra object lights
				{
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].ambient.red = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].ambient.green = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].ambient.blue = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].diffuse.red = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].diffuse.green = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].diffuse.blue = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].lightPos.x = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].lightPos.y = file.readReal();
					TheWritableGlobalData->m_terrainObjectsLighting[i+TIME_OF_DAY_FIRST][j].lightPos.z = file.readReal();
				}
			}
			if (info->version >= K_LIGHTING_VERSION_3) {
				for (Int j=1; j<3; j++)	//added support for 2 extra terrain lights
				{
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].ambient.red = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].ambient.green = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].ambient.blue = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].diffuse.red = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].diffuse.green = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].diffuse.blue = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].lightPos.x = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].lightPos.y = file.readReal();
					TheWritableGlobalData->m_terrainLighting[i+TIME_OF_DAY_FIRST][j].lightPos.z = file.readReal();
				}
			}
		}
		if (!file.atEndOfChunk()) {
			UnsignedInt shadowColor = file.readInt();
			if (TheW3DShadowManager) {
				TheW3DShadowManager->setShadowColor(shadowColor);
			}
		}
	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Unexpected data left over."));
	return true;
}

/**
* WorldHeightMap::ParseObjectsDataChunk - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMap::ParseObjectsDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	file.m_currentObject = nullptr;
	file.registerParser( AsciiString("Object"), info->label, ParseObjectDataChunk );
	return (file.parse(userData));
}

/**
* WorldHeightMap::ParseHeightMapData - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMap::ParseHeightMapDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *pThis = static_cast<WorldHeightMap *>(userData);
	return pThis->ParseHeightMapData(file, info, userData);
}

/**
* WorldHeightMap::ParseHeightMapData - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMap::ParseHeightMapData(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	if (m_data != nullptr) {
		throw ERROR_CORRUPT_FILE_FORMAT;	// second height map chunk
	}
	m_width = file.readInt();
	m_height = file.readInt();
	if (info->version >= K_HEIGHT_MAP_VERSION_3) {
		m_borderSize = file.readInt();
	} else {
		m_borderSize = 0;
	}
	if (m_width <= 0 || m_height <= 0 || m_width > MAX_MAP_EXTENT || m_height > MAX_MAP_EXTENT || m_borderSize < 0) {
		throw ERROR_CORRUPT_FILE_FORMAT;
	}

	if (info->version >= K_HEIGHT_MAP_VERSION_4) {
		Int numBorders = file.readInt();
		if (numBorders < 0 || numBorders > MAX_MAP_BOUNDARIES) {
			throw ERROR_CORRUPT_FILE_FORMAT;
		}
		m_boundaries.resize(numBorders);
		for (int i = 0; i < numBorders; ++i) {
			m_boundaries[i].x = file.readInt();
			m_boundaries[i].y = file.readInt();
		}
	} else {
		m_boundaries.resize(1);
		m_boundaries[0].x = m_width - 2 * m_borderSize;
		m_boundaries[0].y = m_height - 2 * m_borderSize;
	}

	m_dataSize = file.readInt();
	if (m_dataSize <= 0 || (m_dataSize != (m_width*m_height)) || m_dataSize > static_cast<Int>(file.getChunkDataSizeLeft())) {
		throw ERROR_CORRUPT_FILE_FORMAT	;
	}
	m_data = MSGNEW("WorldHeightMap_ParseHeightMapData") UnsignedByte[m_dataSize];

	Int numBytesX = (m_width+7)/8;	//how many bytes to fit all bitflags
	Int numBytesY = m_height;	
	m_seismicUpdateWidth=numBytesX;
	m_seismicUpdateFlag	= MSGNEW("WorldHeightMap::ParseHeightMapData _ m_seismicUpdateFlag allocated") UnsignedByte[numBytesX*numBytesY];
  clearSeismicUpdateFlags();
  m_seismicZVelocities = MSGNEW("WorldHeightMap_ParseHeightMapData _ zvelocities allocated") Real[m_dataSize];
  fillSeismicZVelocities( 0 );


	file.readArrayOfBytes(reinterpret_cast<char *>(m_data), m_dataSize);
	// Resize me. 
	if (info->version == K_HEIGHT_MAP_VERSION_1) {
		Int newWidth = (m_width+1)/2;
		Int newHeight = (m_height+1)/2;
		Int i, j;
		for (i=0; i<newHeight; i++) {
			for (j=0; j<newWidth; j++) {
				m_data[i*newWidth+j] = m_data[2*i*m_width+2*j];
			}
		}
	}
	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Unexpected data left over."));
	return true;
}

/**
* WorldHeightMap::ParseHeightMapData - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMap::ParseSizeOnlyInChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *pThis = static_cast<WorldHeightMap *>(userData);
	return pThis->ParseSizeOnly(file, info, userData);
}

/**
* WorldHeightMap::ParseHeightMapData - read a height map chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMap::ParseSizeOnly(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	if (m_data != nullptr) {
		throw ERROR_CORRUPT_FILE_FORMAT;	// second height map chunk
	}
	m_width = file.readInt();
	m_height = file.readInt();
	if (info->version >= K_HEIGHT_MAP_VERSION_3) {
		m_borderSize = file.readInt();
	} else {
		m_borderSize = 0;
	}
	if (m_width <= 0 || m_height <= 0 || m_width > MAX_MAP_EXTENT || m_height > MAX_MAP_EXTENT || m_borderSize < 0) {
		throw ERROR_CORRUPT_FILE_FORMAT;
	}

	if (info->version >= K_HEIGHT_MAP_VERSION_4) {
		Int numBorders = file.readInt();
		if (numBorders < 0 || numBorders > MAX_MAP_BOUNDARIES) {
			throw ERROR_CORRUPT_FILE_FORMAT;
		}
		m_boundaries.resize(numBorders);
		for (int i = 0; i < numBorders; ++i) {
			m_boundaries[i].x = file.readInt();
			m_boundaries[i].y = file.readInt();
		}
	} else {
		m_boundaries.resize(1);
		m_boundaries[0].x = m_width - 2 * m_borderSize;
		m_boundaries[0].y = m_height - 2 * m_borderSize;
	}

	m_dataSize = file.readInt();
	if (m_dataSize <= 0 || (m_dataSize != (m_width*m_height)) || m_dataSize > static_cast<Int>(file.getChunkDataSizeLeft())) {
		throw ERROR_CORRUPT_FILE_FORMAT	;
	}
	m_data = MSGNEW("WorldHeightMap_ParseSizeOnly") UnsignedByte[m_dataSize];
	file.readArrayOfBytes(reinterpret_cast<char *>(m_data), m_dataSize);
	// Resize me. 
	if (info->version == K_HEIGHT_MAP_VERSION_1) {
		Int newWidth = (m_width+1)/2;
		Int newHeight = (m_height+1)/2;
		Int i, j;
		for (i=0; i<newHeight; i++) {
			for (j=0; j<newWidth; j++) {
				m_data[i*newWidth+j] = m_data[2*i*m_width+2*j];
			}
		}
		m_width = newWidth;
		m_height = newHeight;
	}
	return true;
}

/**
* WorldHeightMap::ParseBlendTileDataChunk - read a blend tile info chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMap::ParseBlendTileDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *pThis = static_cast<WorldHeightMap *>(userData);
	return pThis->ParseBlendTileData(file, info, userData);
}

// Reason the last countTiles/readTiles call rejected a tile file, for the log.
static const char *s_tileFileError = nullptr;
// Texture package: name of the tile file being read and the mip level countTiles chose for it; readTiles
// decodes that level (WorldHeightMap is filled on the main thread only).
static const char *s_tilePackageName = nullptr;
static Int s_tileMipLevel = 0;

void WorldHeightMap::setTilePackageName(const char *name)
{
	s_tilePackageName = name;
	s_tileMipLevel = 0;
}

// Edge of the largest square of at most numTiles tiles (at most 10), 0 for none.
static Int squareTileWidth(Int numTiles)
{
	Int width;
	for (width = 10; width >= 1; width--) {
		if (numTiles >= width*width) {
			break;
		}
	}
	return width;
}

// Reads the tiles of a texture class from an open tile file. False if the file holds too few tiles or
// cannot be decoded; the tiles stay unchanged then.
static Bool readTexClassTiles(File *file, TXTextureClass *texClass, TileData **tileData, Int tilesPerRow, const char *path)
{
	GDIFileStream theStream(file);
	InputStream *pStr = &theStream;
	s_tileFileError = nullptr;
	s_tilePackageName = path;
	s_tileMipLevel = 0;
	Int numTiles = WorldHeightMap::countTiles(pStr, nullptr, tilesPerRow);
	file->seek(0, File::seekMode::START);
	Bool ok = FALSE;
	if (numTiles < texClass->numTiles) {
		if (s_tileFileError == nullptr) {
			s_tileFileError = "too few tiles";
		}
	} else {
		ok = WorldHeightMap::readTiles(pStr, tileData+texClass->firstTile, squareTileWidth(texClass->numTiles), tilesPerRow);
	}
	s_tilePackageName = nullptr;
	s_tileMipLevel = 0;
	return ok;
}

/** Function to read in the tiles for a texture class. */
void WorldHeightMap::readTexClass(TXTextureClass *texClass, TileData **tileData)
{
	File *theFile = nullptr;
	const Int tilesPerRow = squareTileWidth(texClass->numTiles);

	// get the file from the description in TheTerrainTypes
	TerrainType *terrain = TheTerrainTypes->findTerrain( texClass->name );
	char texturePath[ _MAX_PATH ];
	if (terrain==nullptr)
	{
#ifdef LOAD_TEST_ASSETS
		theFile = TheFileSystem->openFile( texClass->name.str(), File::READ|File::BINARY);
#endif
	}
	else
	{
		WWFormat::Into(texturePath, "{}{}", TERRAIN_TGA_DIR_PATH, terrain->getTexture().str());
		// A DDS of the same name wins over the TGA wherever it is, as for all other textures
		// (DDSFileClass). One that cannot be used leaves the TGA.
		const size_t len = strlen(texturePath);
		if (len > 4 && _stricmp(texturePath + len - 4, ".tga") == 0) {
			char ddsPath[ _MAX_PATH ];
			strcpy(ddsPath, texturePath);
			strcpy(ddsPath + len - 4, ".dds");
			File *ddsFile = TheFileSystem->openFile( ddsPath, File::READ|File::BINARY);
			if (ddsFile != nullptr) {
				const Bool ok = readTexClassTiles(ddsFile, texClass, tileData, tilesPerRow, ddsPath);
				ddsFile->close();
				char line[_MAX_PATH + 96];
				if (ok) {
					const TileData *first = texClass->numTiles > 0 ? tileData[texClass->firstTile] : nullptr;
					WWFormat::Into(line, "TERRAINTILES dds {} tiles {:d} extent {:d}", ddsPath, tilesPerRow*tilesPerRow, first ? first->getExtent() : 0);
				} else {
					WWFormat::Into(line, "TERRAINTILES dds {} rejected: {}", ddsPath, s_tileFileError ? s_tileFileError : "unknown");
				}
				GameEngine_RunnerLog(line);
				DEBUG_LOG(("%s\n", line));
				if (ok) {
					return;
				}
			}
		}
		theFile = TheFileSystem->openFile( texturePath, File::READ|File::BINARY);
	}

	if (theFile != nullptr) {
		readTexClassTiles(theFile, texClass, tileData, tilesPerRow, texturePath);
		theFile->close();
	}
}

/** Relief of a texture class: <texture>_n.dds next to the terrain texture, read into m_reliefTiles. Only with the
	terrainnormal effect (it costs a second atlas); classes without the file stay flat. */
void WorldHeightMap::readTexClassRelief(TXTextureClass *texClass)
{
	if (!TheGlobalData || !(TheGlobalData->m_renderEffects & RENDER_EFFECT_TERRAIN_NORMAL))
		return;
	readTexClassExtra(texClass, "_n.dds", m_reliefTiles, "TERRAINRELIEF");
}

/** Heights of a texture class for the terrain tessellation: <texture>_h.dds (BC4, height of the ground scans, 128 =
	mean), read into m_heightTiles. Only with the terraintess effect; classes without the file are not displaced. */
void WorldHeightMap::readTexClassHeights(TXTextureClass *texClass)
{
	if (!TheGlobalData || !(TheGlobalData->m_renderEffects & RENDER_EFFECT_TERRAIN_TESS))
		return;
	readTexClassExtra(texClass, "_h.dds", m_heightTiles, "TERRAINTESS");
}

void WorldHeightMap::readTexClassExtra(TXTextureClass *texClass, const char *suffix, TileData **tiles, const char *tag)
{
	if (!TheTerrainTypes || !TheFileSystem)
		return;
	TerrainType *terrain = TheTerrainTypes->findTerrain( texClass->name );
	if (terrain == nullptr)
		return;
	char path[ _MAX_PATH ];
	WWFormat::Into(path, "{}{}", TERRAIN_TGA_DIR_PATH, terrain->getTexture().str());
	const size_t len = strlen(path);
	if (len <= 4 || len - 4 + strlen(suffix) >= sizeof(path) || _stricmp(path + len - 4, ".tga") != 0)
		return;
	strcpy(path + len - 4, suffix);
	File *file = TheFileSystem->openFile( path, File::READ|File::BINARY);
	if (file == nullptr)
		return;
	const Bool ok = readTexClassTiles(file, texClass, tiles, squareTileWidth(texClass->numTiles), path);
	file->close();
	if (!ok) {
		for (Int t = 0; t < texClass->numTiles; ++t) {
			const Int ndx = texClass->firstTile + t;
			if (ndx >= 0 && ndx < NUM_SOURCE_TILES)
				REF_PTR_RELEASE(tiles[ndx]);
		}
	}
	char line[_MAX_PATH + 96];
	WWFormat::Into(line, "{} dds {} {}", tag, path, ok ? "ok" : (s_tileFileError ? s_tileFileError : "rejected"));
	DEBUG_LOG(("%s\n", line));
}

/**
* WorldHeightMap::ParseBlendTileData - read a blend tile info chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMap::ParseBlendTileData(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	int i, j;
	if (m_data == nullptr || m_tileNdxes != nullptr) {
		throw ERROR_CORRUPT_FILE_FORMAT;	// needs the height map first, only once
	}
	Int len = file.readInt();
	if (m_dataSize != len) {
		throw ERROR_CORRUPT_FILE_FORMAT	;
	}
	m_tileNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize];
	m_cliffInfoNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize]; 
	m_blendTileNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize];
	m_extraBlendTileNdxes = MSGNEW("WorldHeightMap_ParseBlendTileData") Short[m_dataSize];
	// Note - we have one less cell than the width & height. But for paranoia, allocate
	// extra row. jba.
	// 
	Int numBytesX = (m_width+7)/8;	//how many bytes to fit all bitflags
	Int numBytesY = m_height;	

	m_flipStateWidth=numBytesX;

	m_cellFlipState	= MSGNEW("WorldHeightMap_getTerrainTexture") UnsignedByte[numBytesX*numBytesY];
	m_cellCliffState	= MSGNEW("WorldHeightMap_getTerrainTexture") UnsignedByte[numBytesX*numBytesY];
	memset(m_cellFlipState,0,numBytesX*numBytesY);	//clear all flags
	memset(m_cellCliffState,0,numBytesX*numBytesY);	//clear all flags

	file.readArrayOfBytes(reinterpret_cast<char*>(m_tileNdxes), m_dataSize*sizeof(Short));
	file.readArrayOfBytes(reinterpret_cast<char*>(m_blendTileNdxes), m_dataSize*sizeof(Short));
	if (info->version >= K_BLEND_TILE_VERSION_6) {
		file.readArrayOfBytes(reinterpret_cast<char*>(m_extraBlendTileNdxes), m_dataSize*sizeof(Short));
		//Allow clearing of extra blend tiles via ini and resaving of map.
		//Useful for flushing out initial maps made with buggy 3-way blending.
		if (!TheGlobalData->m_use3WayTerrainBlends)
			memset(m_extraBlendTileNdxes,0,m_dataSize*sizeof(Short));		
	} 
	if (info->version >= K_BLEND_TILE_VERSION_5) {
		file.readArrayOfBytes(reinterpret_cast<char*>(m_cliffInfoNdxes), m_dataSize*sizeof(Short));
	} 
	if (info->version >= K_BLEND_TILE_VERSION_7) {
		if (info->version==K_BLEND_TILE_VERSION_7) {
			Int byteWidth = (m_width+1)/8; // previous incorrect length that got used to save the file.  jba. [4/3/2003]
			UnsignedByte *data = new UnsignedByte[m_height*byteWidth];
			file.readArrayOfBytes(reinterpret_cast<char*>(data), m_height*byteWidth);
			for (j=0; j<m_height; j++) {
				for (i=0; i<byteWidth; i++) {
					m_cellCliffState[j*m_flipStateWidth + i] = data[j*byteWidth + i];
				}
			}
			delete [] data;
		} else {
			file.readArrayOfBytes(reinterpret_cast<char*>(m_cellCliffState), m_height*m_flipStateWidth);
		}
	} else {
		initCliffFlagsFromHeights();
	}
	m_numBitmapTiles = file.readInt();
	DEBUG_ASSERTCRASH(m_numBitmapTiles>0 && m_numBitmapTiles<2048, ("Unlikely numBitmapTiles."));
	m_numBlendedTiles = file.readInt();
	DEBUG_ASSERTCRASH(m_numBlendedTiles>0 && m_numBlendedTiles<NUM_BLEND_TILES+1, ("Unlikely numBlendedTiles."));
	if (info->version >= K_BLEND_TILE_VERSION_5) {
		m_numCliffInfo = file.readInt();
	} else {
		m_numCliffInfo = 1;	// cliffInfo[0] is the default info.
	}
	// The counts index fixed arrays below and in the renderer. EA maps store 0 cliff infos; entry 0
	// is the default and out-of-range cell indices are reset to it after loading.
	if (m_numBlendedTiles < 0 || m_numBlendedTiles > NUM_BLEND_TILES
		|| m_numCliffInfo < 0 || m_numCliffInfo > NUM_CLIFF_INFO) {
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
	m_numBitmapTiles = clampTileCount(m_numBitmapTiles);
// --> file loading here
	m_numTextureClasses	= file.readInt();
	DEBUG_ASSERTCRASH(m_numTextureClasses>0 && m_numTextureClasses<200, ("Unlikely m_numTextureClasses."));
	if (m_numTextureClasses < 0 || m_numTextureClasses > NUM_TEXTURE_CLASSES) {
		throw ERROR_CORRUPT_FILE_FORMAT;
	}
	for (i=0; i<m_numTextureClasses; i++) {
		m_textureClasses[i].globalTextureClass = -1;
		m_textureClasses[i].firstTile = file.readInt();
		m_textureClasses[i].numTiles = file.readInt();
		m_textureClasses[i].width = file.readInt();
		validateTileRange(m_textureClasses[i]);

		// legacy GDF data
		// used to read "m_textureClasses[i].isGDF = file.readInt();"
	/*	Int legacy = */ file.readInt();

		m_textureClasses[i].name = file.readAsciiString();
		readTexClass(&m_textureClasses[i], m_sourceTiles);
		readTexClassRelief(&m_textureClasses[i]);
		readTexClassHeights(&m_textureClasses[i]);
	}
	m_numEdgeTextureClasses = 0;
	m_numEdgeTiles = 0;
	if (info->version >= K_BLEND_TILE_VERSION_4) {
		m_numEdgeTiles	= file.readInt();
		m_numEdgeTextureClasses	= file.readInt();
		if (m_numEdgeTextureClasses < 0 || m_numEdgeTextureClasses > NUM_TEXTURE_CLASSES) {
			throw ERROR_CORRUPT_FILE_FORMAT;
		}
		m_numEdgeTiles = clampTileCount(m_numEdgeTiles);
		for (i=0; i<m_numEdgeTextureClasses; i++) {
			m_edgeTextureClasses[i].globalTextureClass = -1;
			m_edgeTextureClasses[i].firstTile = file.readInt();
			m_edgeTextureClasses[i].numTiles = file.readInt();
			m_edgeTextureClasses[i].width = file.readInt();
			validateTileRange(m_edgeTextureClasses[i]);
			m_edgeTextureClasses[i].name = file.readAsciiString();
			readTexClass(&m_edgeTextureClasses[i], m_edgeTiles);
		}
	}
	for (i=1; i<m_numBlendedTiles; i++) {
		Int flag;
		m_blendedTiles[i].blendNdx = file.readInt();
		m_blendedTiles[i].horiz = file.readByte();
		m_blendedTiles[i].vert = file.readByte();
		m_blendedTiles[i].rightDiagonal = file.readByte();
		m_blendedTiles[i].leftDiagonal = file.readByte();
		m_blendedTiles[i].inverted = file.readByte();
		//Allow clearing of extra blend tiles via ini and resaving of map.
		//Useful for flushing out initial maps made with buggy 3-way blending.
		if (!TheGlobalData->m_use3WayTerrainBlends)
			m_blendedTiles[i].inverted &= ~FLIPPED_MASK;	//filter out extra flips from 3-way

		if (info->version >= K_BLEND_TILE_VERSION_3) {
			m_blendedTiles[i].longDiagonal = file.readByte();
		} else {
			m_blendedTiles[i].longDiagonal = false;
		}
		if (info->version >= K_BLEND_TILE_VERSION_4) {
			m_blendedTiles[i].customBlendEdgeClass = file.readInt();
		} else {
			m_blendedTiles[i].customBlendEdgeClass = -1;
		}
		
		flag = file.readInt();
		DEBUG_ASSERTCRASH(flag==FLAG_VAL, ("Invalid format."));
		if (flag != FLAG_VAL) {
			throw ERROR_CORRUPT_FILE_FORMAT;
		}
	}
	if (info->version >= K_BLEND_TILE_VERSION_5) {
		for (i=1; i<m_numCliffInfo; i++) {
			m_cliffInfo[i].tileIndex = static_cast<Short>(file.readInt());
			m_cliffInfo[i].u0 = file.readReal();
			m_cliffInfo[i].v0 = file.readReal();
			m_cliffInfo[i].u1 = file.readReal();
			m_cliffInfo[i].v1 = file.readReal();
			m_cliffInfo[i].u2 = file.readReal();
			m_cliffInfo[i].v2 = file.readReal();
			m_cliffInfo[i].u3 = file.readReal();
			m_cliffInfo[i].v3 = file.readReal();
			m_cliffInfo[i].flip = file.readByte();
			m_cliffInfo[i].mutant = file.readByte();
		}
	}
	// Resize me. 
	if (info->version == K_BLEND_TILE_VERSION_1) {
		Int newWidth = (m_width+1)/2;
		Int newHeight = (m_height+1)/2;
		Int i2, j2;
		for (i2=0; i2<newHeight; i2++) {
			for (j2=0; j2<newWidth; j2++) {
				m_tileNdxes[i2*newWidth+j2] = m_tileNdxes[2*i2*m_width+2*j2];
				m_blendTileNdxes[i2*newWidth+j2] = 0;
				m_extraBlendTileNdxes[i2*newWidth+j2] = 0;
				m_cliffInfoNdxes[i2*newWidth+j2] = 0;	 
			}
		}
		m_numBlendedTiles = 1;
		m_numCliffInfo = 1;
		m_width= newWidth;
		m_height = newHeight;
		m_dataSize = m_width*m_height;
	}
	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Unexpected data left over."));
	return true;
}


/**
* WorldHeightMap::ParseObjectData - read a object info chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMap::ParseObjectDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	WorldHeightMap *pThis = static_cast<WorldHeightMap *>(file.m_userData);
	return pThis->ParseObjectData(file, info, userData, info->version >= K_OBJECTS_VERSION_2);
}

/**
* WorldHeightMap::ParseObjectData - read a object info chunk.
* Format is the newer CHUNKY format.
*	See WHeightMapEdit.cpp for the writer.
*	Input: DataChunkInput 
*		
*/
Bool WorldHeightMap::ParseObjectData(DataChunkInput &file, DataChunkInfo *info, void *userData, Bool readDict)
{
	MapObject *pPrevious = static_cast<MapObject *>(file.m_currentObject);

	Coord3D loc;
	loc.x = file.readReal();
	loc.y = file.readReal();
	loc.z = file.readReal();

	Real minZ = -100*MAP_XY_FACTOR;
	Real maxZ = (255*10)*MAP_HEIGHT_SCALE;

	if (info->version <= K_OBJECTS_VERSION_2) {
		loc.z = 0;
	}

	Real angle = file.readReal();
	Int flags = file.readInt(); 
	AsciiString name = file.readAsciiString();
	Dict d;
	if (readDict)
	{
		d = file.readDict();
	}		 

	if (loc.z<minZ || loc.z>maxZ) {
		DEBUG_LOG(("Removing object at z height %f\n", loc.z));
		return true;
	}

	MapObject *pThisOne;
	
	// create the map object
	pThisOne = newInstance( MapObject )( loc, name, angle, flags, &d, 
														TheThingFactory->findTemplate( name, FALSE ) );

//DEBUG_LOG(("obj %s owner %s\n",name.str(),d.getAsciiString(TheKey_originalOwner).str()));

	if (pThisOne->getProperties()->getType(TheKey_waypointID) == Dict::DICT_INT)
		pThisOne->setIsWaypoint();

	if (pThisOne->getProperties()->getType(TheKey_lightHeightAboveTerrain) == Dict::DICT_REAL)
		pThisOne->setIsLight();

	if (pThisOne->getProperties()->getType(TheKey_scorchType) == Dict::DICT_INT)
		pThisOne->setIsScorch();
	

	if (pPrevious) {
		DEBUG_ASSERTCRASH(MapObject::TheMapObjectListPtr != nullptr && pPrevious->getNext() == nullptr, ("Bad linkage."));
		pPrevious->setNextMap(pThisOne);
	}	else {
		DEBUG_ASSERTCRASH(MapObject::TheMapObjectListPtr == nullptr, ("Bad linkage."));
		MapObject::TheMapObjectListPtr = pThisOne;
	}
	file.m_currentObject = pThisOne;
	return true;
}



// Targa format:  Header

typedef struct {
	UnsignedByte	idLength;
	UnsignedByte	colorMapType; // 0 = rgb, 1 = indexed.
	UnsignedByte	imageType; //0x1 = indexed, 0x2 = rgb, 0x8 = rle.
	UnsignedByte	colorMapInfo[5]; // we ignore, only do rgb.
	Short			xOrigin;
	Short			yOrigin;
	Short			imageWidth;
	Short			imageHeight;
	UnsignedByte	pixelDepth;
	UnsignedByte	flags; //  &0x0F = alpha channel bits, &0x10 is right to left flag,
						   // 0x20 is top to bottom flag.  (0x0? is left to right, bottom to top)
						   // 0x3? is top to bottom, right to left.
} TTargaHeader;

// followed by idLength bytes of ascii data

// followed by pixel data

// followed by optional data.



// Tile scale of a TGA file. Texture quality tiers store terrain and tree tiles at 64*s pixels and write
// "ZHTS<s>" into the image ID field; without it (original files) the scale is 1. Reads the ID field and
// leaves the stream at the start of the image data. TerrainTileImage::decode applies the same rule.
static Int readTileScaleId(InputStream *pStr, const TTargaHeader &hdr)
{
	Int scale = 1;
	if (hdr.idLength > 0) {
		char id[256];
		Int n = pStr->read(id, hdr.idLength);
		if (n == hdr.idLength && n >= 5 && memcmp(id, "ZHTS", 4) == 0) {
			if (id[4] == '2') scale = 2;
			if (id[4] == '4') scale = 4;
		}
	}
	return scale;
}

/// Count how many tiles come in from a targa or DDS file.
Int WorldHeightMap::countTiles(InputStream *pStr, Bool *halfTile, Int tilesPerRow)
{
	TTargaHeader hdr;
	if (halfTile) {
		*halfTile = false;
	}
	Int len = pStr->read(&hdr,sizeof(hdr));
	if (len!=sizeof(hdr)) return(0);
	Int imageWidth = hdr.imageWidth;
	Int imageHeight = hdr.imageHeight;
	Int fileExtent;
	if (TerrainTileImage::isDds(&hdr, len)) {
		unsigned char header[TerrainTileImage::MAX_HEADER_BYTES];
		memcpy(header, &hdr, len);
		const Int more = pStr->read(header + len, static_cast<Int>(sizeof(header)) - len);
		TerrainTileImage::Info info;
		const std::size_t headerBytes = static_cast<std::size_t>(len + (more > 0 ? more : 0));
		if (!TerrainTileImage::readDdsInfo(header, headerBytes, tilesPerRow, info, &s_tileFileError)) {
			return(0);
		}
		// Texture package: the level of the chosen tier (tiles of the original 64 pixels in SD).
		const Int level = s_tilePackageName != nullptr ? static_cast<Int>(::Engine::TexturePackage::instance().mipOffset(
			s_tilePackageName, static_cast<unsigned>(info.width), static_cast<unsigned>(info.height), static_cast<unsigned>(info.mipLevels))) : 0;
		if (level > 0 && !TerrainTileImage::readDdsInfo(header, headerBytes, tilesPerRow, info, &s_tileFileError, level)) {
			return(0);
		}
		s_tileMipLevel = info.mipLevel;
		imageWidth = info.width;
		imageHeight = info.height;
		fileExtent = info.tileExtent;
	} else {
		fileExtent = TILE_PIXEL_EXTENT*readTileScaleId(pStr, hdr);	// hd-ki2
		// Texture package: halve a scaled TGA down to the tiles of the chosen tier (not below 64 pixels).
		if (s_tilePackageName != nullptr && imageWidth > 0 && imageHeight > 0) {
			unsigned levels = 0;
			for (Int w = imageWidth, h = imageHeight; w > 0 && h > 0; w >>= 1, h >>= 1) {
				++levels;
			}
			Int level = static_cast<Int>(::Engine::TexturePackage::instance().mipOffset(s_tilePackageName,
				static_cast<unsigned>(imageWidth), static_cast<unsigned>(imageHeight), levels));
			while (level > 0 && (fileExtent >> level) < TILE_PIXEL_EXTENT) {
				--level;
			}
			imageWidth >>= level;
			imageHeight >>= level;
			fileExtent >>= level;
			s_tileMipLevel = level;
		}
		if (hdr.colorMapType != 0) {
			return(0); // we don't do indexed at this time. jba.
		}
		if (hdr.imageType != 0x2 && hdr.imageType != 0xA) {
			return(0); // we don't do indexed at this time. jba.
		}

		if (hdr.pixelDepth < 24) return(false);
		if (hdr.pixelDepth > 32) return(false);
	}
	Int tileWidth = imageWidth/fileExtent;
	Int tileHeight = imageHeight/fileExtent;
// 3x3 gives 9, 
	// 2x2 gives 4, 
	// 1x1 gives 1, 
	// else 0;
	if (tileWidth>10 || tileHeight>10) return(0);  // don't do huge images, or bad files.
	if (tileWidth>=10 && tileHeight >=10) return(100);
	if (tileWidth>=9 && tileHeight >=9) return(81);
	if (tileWidth>=8 && tileHeight >=8) return(64);
	if (tileWidth>=7 && tileHeight >=7) return(49);
	if (tileWidth>=6 && tileHeight >=6) return(36);
	if (tileWidth>=5 && tileHeight >=5) return(25);
	if (tileWidth>=4 && tileHeight >=4) return(16);
	if (tileWidth>=3 && tileHeight >=3) return(9);
	if (tileWidth>=2 && tileHeight >=2) return(4);
	if (tileWidth>=1 && tileHeight >=1) return(1);
	if (halfTile && imageHeight==fileExtent/2 && imageWidth==fileExtent/2) {
		*halfTile = true;
		return 1;
	}
	return(0);
}
/*Break down a .tga or .dds file into a collection of tiles.  numRows * numRows total tiles.*/
// Tiles get their size from the file (TGA: ID ZHTS<s>, DDS: width / tilesPerRow, see TerrainTileImage.h),
// 64*s pixels. Whoever combines them into an atlas equalizes them with TileData::unifyExtent afterwards.
// The first file row (TGA as stored, DDS flipped) becomes the bottom tile row.
Bool WorldHeightMap::readTiles(InputStream *pStr, TileData **tiles, Int numRows, Int tilesPerRow)
{
	std::vector<unsigned char> file;
	for (;;) {
		constexpr Int CHUNK = 1 << 16;
		const size_t used = file.size();
		file.resize(used + CHUNK);
		const Int got = pStr->read(file.data() + used, CHUNK);
		file.resize(used + (got > 0 ? got : 0));
		if (got < CHUNK) break;
	}
	TerrainTileImage::Image image;
	if (!TerrainTileImage::decode(file.data(), file.size(), tilesPerRow, image, &s_tileFileError, s_tileMipLevel)) {
		DEBUG_LOG(("readTiles: %s\n", s_tileFileError));
		return(false);
	}
	file.clear();
	file.shrink_to_fit();

	const Int fileExtent = image.info.tileExtent;
	const Int imageWidth = image.info.width;
	const Int imageHeight = image.info.height;
	Int tileWidth = imageWidth/fileExtent;
	Int tileHeight = imageHeight/fileExtent;

	if (imageHeight==fileExtent/2) {
		tileHeight = 1;
	}
	if (imageWidth==fileExtent/2) {
		tileWidth = 1;
	}

	if (tileWidth<numRows && tileHeight<numRows) {
		s_tileFileError = "too few tiles";
		return(false);
	}
	int row = 0;
	int column = 0;
	int i;
	for (i=0; i<numRows*numRows; i++) {
		if (tiles[i] != nullptr && tiles[i]->getExtent() != fileExtent) {
			REF_PTR_RELEASE(tiles[i]);	// hd-ki2: vorhandene Kachel anderer Groesse ersetzen
		}
		if (tiles[i] == nullptr)
			tiles[i] = MSGNEW("WorldHeightMap_readTiles") TileData(fileExtent);
	}
	static const UnsignedByte transparentBlack[4] = { 0, 0, 0, 0 };
	const Int columns = imageWidth < numRows*fileExtent ? imageWidth : numRows*fileExtent;
	for (row = 0; row < numRows*fileExtent; row++) {
		for (column=0; column<columns; column++) {
			// B G R A like the tiles; rows below the picture stay transparent black.
			const UnsignedByte *source = row < imageHeight
				? &image.bgra[(static_cast<size_t>(row)*imageWidth + column)*4] : transparentBlack;
			int tileNdx = (column/fileExtent) + numRows*(row/fileExtent);
			int pixelNdx = (column%fileExtent) + fileExtent*(row%fileExtent);

			UnsignedByte *pixel = tiles[tileNdx]->getDataPtr();

			pixel += pixelNdx*TILE_BYTES_PER_PIXEL;
			memcpy(pixel, source, TILE_BYTES_PER_PIXEL);
		}
	}
for (i=0; i<numRows*numRows; i++) {
		tiles[i]->updateMips();
	}
	return(true);
}



/** updateTileTexturePositions - assigns each tile a location in the texture.
*/
Int WorldHeightMap::updateTileTexturePositions(Int *edgeHeight)
{
	Int i, j;
	Int maxHeight = 0;
	const Int tilesPerRow = TEXTURE_WIDTH/(TILE_PIXEL_EXTENT+TILE_OFFSET);

	Bool availableGrid[tilesPerRow][tilesPerRow];
	Int row, column;
	for (row=0; row<tilesPerRow; row++) {
		for (column=0; column<tilesPerRow; column++) {
			availableGrid[row][column] = true;
		}
	}

	for (i=0; i<m_numBitmapTiles; i++) {
		if (m_sourceTiles[i]) {
			m_sourceTiles[i]->m_tileLocationInTexture.x = 0;
			m_sourceTiles[i]->m_tileLocationInTexture.y = 0;
		}
	}	 

	/* put the normal tiles into the terrain texture */
	Int texClass;
	Int tileWidth;
	for (tileWidth = tilesPerRow; tileWidth>0; tileWidth--) {
		for (texClass=0; texClass<m_numTextureClasses; texClass++) {
			Int width = m_textureClasses[texClass].width;
			if (width != tileWidth) continue;
			// Find an available block of space.
			Bool found = false;
			for (row=0; row<tilesPerRow-width+1 && !found; row++) {
				for (column=0; column<tilesPerRow-width+1 && !found; column++) {
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
				m_textureClasses[texClass].positionInTexture.x = 0;
				m_textureClasses[texClass].positionInTexture.y = 0;
				continue;
			}

			Int xOrigin = TILE_OFFSET/2 + column*(TILE_PIXEL_EXTENT+TILE_OFFSET);
			Int yOrigin = TILE_OFFSET/2 + row*(TILE_PIXEL_EXTENT+TILE_OFFSET);
			m_textureClasses[texClass].positionInTexture.x = xOrigin;
			m_textureClasses[texClass].positionInTexture.y = yOrigin;
			Int classHeight = yOrigin + width*TILE_PIXEL_EXTENT+ TILE_OFFSET/2;
			if (maxHeight < classHeight) maxHeight = classHeight;

			for (i=0; i<width; i++) {
				for (j=0; j<width; j++) {
					availableGrid[row+j][column+i] = false;
					Int baseNdx = m_textureClasses[texClass].firstTile + i + j*width;
					// In case we are just checking for room...
					if (m_sourceTiles[baseNdx] == nullptr) continue;
					Int x = xOrigin + i*TILE_PIXEL_EXTENT;
					Int y = yOrigin + (width-j-1)*TILE_PIXEL_EXTENT;
					m_sourceTiles[baseNdx]->m_tileLocationInTexture.x = x;
					m_sourceTiles[baseNdx]->m_tileLocationInTexture.y = y;
				}
			}
		}
	}

	for (i=0; i<m_numBitmapTiles; i++) {
		if (m_edgeTiles[i]) {
			m_edgeTiles[i]->m_tileLocationInTexture.x = 0;
			m_edgeTiles[i]->m_tileLocationInTexture.y = 0;
		}
	}	 

	/* put the blend edge tiles into the blend edges texture */
	Int maxEdgeHeight = 0;
	// Reset the grid, cause we're using a different texture now.
	for (row=0; row<tilesPerRow; row++) {
		for (column=0; column<tilesPerRow; column++) {
			availableGrid[row][column] = true;
		}
	}
	for (texClass=0; texClass<m_numEdgeTextureClasses; texClass++) {
		Int width = m_edgeTextureClasses[texClass].width;
		// Find an available block of space.
		Bool found = false;
		for (row=0; row<tilesPerRow-width+1 && !found; row++) {
			for (column=0; column<tilesPerRow-width+1 && !found; column++) {
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
			m_edgeTextureClasses[texClass].positionInTexture.x = 0;
			m_edgeTextureClasses[texClass].positionInTexture.y = 0;
			continue;
		}

		Int xOrigin = TILE_OFFSET/2 + column*(TILE_PIXEL_EXTENT+TILE_OFFSET);
		Int yOrigin = TILE_OFFSET/2 + row*(TILE_PIXEL_EXTENT+TILE_OFFSET);
		m_edgeTextureClasses[texClass].positionInTexture.x = xOrigin;
		m_edgeTextureClasses[texClass].positionInTexture.y = yOrigin;
		Int classHeight = yOrigin + width*TILE_PIXEL_EXTENT+ TILE_OFFSET/2;
		if (maxEdgeHeight < classHeight) maxEdgeHeight = classHeight;

		for (i=0; i<width; i++) {
			for (j=0; j<width; j++) {
				availableGrid[row+j][column+i] = false;
				Int baseNdx = m_edgeTextureClasses[texClass].firstTile + i + j*width;
				// In case we are just checking for room...
				if (m_edgeTiles[baseNdx] == nullptr) continue;
				Int x = xOrigin + i*TILE_PIXEL_EXTENT;
				Int y = yOrigin + (width-j-1)*TILE_PIXEL_EXTENT;
				// Use negative offsets to differentiate between tiles & edges.
				m_edgeTiles[baseNdx]->m_tileLocationInTexture.x = x;
				m_edgeTiles[baseNdx]->m_tileLocationInTexture.y = y;
			}
		}
	}
	if (edgeHeight) *edgeHeight = maxEdgeHeight;
	return maxHeight;
}

/** getUVData - Gets the texture coordinates to use.  See getTerrainTexture.
*/
void WorldHeightMap::getUVForNdx(Int tileNdx, float *minU, float *minV, float *maxU, float*maxV, Bool fullTile)
{
	Short baseNdx = static_cast<Short>(tileNdx>>2);
	if (m_sourceTiles[baseNdx] == nullptr) {
		// Missing texture.
		*minU = *minV = *maxU = *maxV = 0.0f;
		return;
	}
	ICoord2D pos = m_sourceTiles[baseNdx]->m_tileLocationInTexture;
	*minU = static_cast<float>(pos.x);
	*minV = static_cast<float>(pos.y);
	*maxU = *minU+TILE_PIXEL_EXTENT; 
	*maxV = *minV+TILE_PIXEL_EXTENT;
#ifdef EVAL_TILING_MODES 
	if (m_tileMode == TILE_8x8) {
		*maxU = *minU+TILE_PIXEL_EXTENT/2.0f; 
		*maxV = *minV+TILE_PIXEL_EXTENT/2.0f;
	} else if (m_tileMode == TILE_6x6) {
		*maxU = *minU+2.0f*TILE_PIXEL_EXTENT/3.0f; 
		*maxV = *minV+2.0f*TILE_PIXEL_EXTENT/3.0f;
	} else {
		*maxU = *minU+TILE_PIXEL_EXTENT; 
		*maxV = *minV+TILE_PIXEL_EXTENT;
	}
#endif
	*minU/=TEXTURE_WIDTH;
	*minV/=m_terrainTexHeight;
	*maxU/=TEXTURE_WIDTH;
	*maxV/=m_terrainTexHeight;
	if (!fullTile) {
		// Tiles are 64x64 pixels, height grids map to 32x32. 
		// So get the proper quadrant of the tile.
		Real midX = (*minU+*maxU)/2;
		Real midY = (*minV+*maxV)/2;
		if (tileNdx&2) {		// y's are flipped.
			*maxV = midY;
		} else {
			*minV = midY;
		}
		if (tileNdx&1) {
			*minU = midX;
		} else {
			*maxU = midX;
		}
	}
}

/** getUVData - Gets the texture coordinates to use.  See getTerrainTexture.
*/
void WorldHeightMap::getUVForBlend(Int edgeClass, Region2D *range)
{
	ICoord2D pos = m_edgeTextureClasses[edgeClass].positionInTexture;
	Int width = m_edgeTextureClasses[edgeClass].width;
	range->lo.x = static_cast<Real>(pos.x)/TEXTURE_WIDTH;
	range->lo.y = static_cast<Real>(pos.y)/m_alphaEdgeHeight;
	range->hi.x = (static_cast<Real>(pos.x) + width*TILE_PIXEL_EXTENT)/TEXTURE_WIDTH;
	range->hi.y = (static_cast<Real>(pos.y) + width*TILE_PIXEL_EXTENT)/m_alphaEdgeHeight;
}

/// Get whether something is cliff indexed with the offset that HeightMapRenderObjClass uses built in.
Bool WorldHeightMap::isCliffMappedTexture(Int x, Int y) { 
	Int ndx = x+m_drawOriginX+m_width*(y+m_drawOriginY);
	if (ndx>=0 && ndx<m_dataSize) {
		return m_cliffInfoNdxes[ndx] != 0;
	}
	return false;
};

/** getUVData - Gets the texture coordinates to use.  See getTerrainTexture.
		xIndex and yIndex are the integer coorddinates into the height map.
		U and V are the texture coordiantes for the 4 corners of a height map cell.
		fullTile is true if we are doing 1/2 resolution height map, and require a full
		tile to texture  a cell.  Otherwise, we use quarter tiles per cell.
*/
Bool WorldHeightMap::getUVData(Int xIndex, Int yIndex, float U[4], float V[4], Bool fullTile)
{
#define dont_SHOW_THE_TEXTURE_FOR_DEBUG 1
#if SHOW_THE_TEXTURE_FOR_DEBUG
		// This is debug code that just shows the generated texture laid on the terrain.
		// For debugging ;) jba.
		xIndex += m_drawOriginX;
		yIndex += m_drawOriginY;
		float nU= xIndex;
		float xU = xIndex+1;
		float nV = 48-yIndex-1;
		float xV = 48-yIndex;
		float k = 48;
		nU /= k;
		xU /= k;
		k = k*m_terrainTexHeight/TEXTURE_WIDTH;
		nV /= k;
		xV /= k;
		U[0] = nU; U[1] = xU; U[2] = xU; U[3] = nU;
		V[0] = xV; V[1] = xV; V[2] = nV; V[3] = nV;
		return(true);
#else
	xIndex += m_drawOriginX;
	yIndex += m_drawOriginY;
	Int ndx = (yIndex*m_width)+xIndex;
	if ((ndx<m_dataSize) && m_tileNdxes) {
		Short tileNdx = m_tileNdxes[ndx];
		return getUVForTileIndex(ndx, tileNdx, U, V, fullTile);
	}
	return false;
#endif
}

/** getUVForTileIndex - Gets the texture coordinates to use.  See getTerrainTexture.
		ndx is the index into the linear height array.
		tileNdx is the index into the texture tiles array.
		U and V are the texture coordiantes for the 4 corners of a height map cell.
		fullTile is true if we are doing 1/2 resolution height map, and require a full
		tile to texture  a cell.  Otherwise, we use quarter tiles per cell.
*/

Bool WorldHeightMap::getUVForTileIndex(Int ndx, Short tileNdx, float U[4], float V[4], Bool fullTile)
{
	Real nU, nV, xU, xV;
	nU=nV=xU=xV = 0.0f;
	Int tilesPerRow = TEXTURE_WIDTH/(2*TILE_PIXEL_EXTENT+TILE_OFFSET);
	tilesPerRow *= 4;

	if ((ndx<m_dataSize) && m_tileNdxes) {
		getUVForNdx(tileNdx, &nU, &nV, &xU, &xV, fullTile);
		U[0] = nU; U[1] = xU; U[2] = xU; U[3] = nU;
		V[0] = xV; V[1] = xV; V[2] = nV; V[3] = nV;
		if (TheGlobalData && !TheGlobalData->m_adjustCliffTextures) {
			return false;
		}
		if (nU==0.0) {
			return false; // missing texture.
		}
		if (fullTile) {
			return false;
		}
		if (m_cliffInfoNdxes[ndx]) {
			TCliffInfo info = m_cliffInfo[m_cliffInfoNdxes[ndx]]; 
			Bool tilesMatch = false;
			Int ndx1 = tileNdx>>2;
			Int ndx2 = info.tileIndex>>2;
			Int i;
			for (i=0; i<this->m_numTextureClasses; i++) {
				if (ndx1 >= m_textureClasses[i].firstTile && ndx1 < m_textureClasses[i].firstTile + m_textureClasses[i].numTiles) {
					tilesMatch = ndx2 >= m_textureClasses[i].firstTile && ndx2 < m_textureClasses[i].firstTile + m_textureClasses[i].numTiles;
					//tilesMatch = true;
					break;
				}
			}
			if (tilesMatch) {
				Real minU = static_cast<Real>(m_textureClasses[i].positionInTexture.x);
				Real maxV = static_cast<Real>(m_textureClasses[i].positionInTexture.y + m_textureClasses[i].width*TILE_PIXEL_EXTENT);
				minU/=TEXTURE_WIDTH;
				maxV/=m_terrainTexHeight;
				Real vFactor = static_cast<Real>(TEXTURE_WIDTH/m_terrainTexHeight);
				U[0] = info.u0+minU;
				U[1] = info.u1+minU;
				U[2] = info.u2+minU;
				U[3] = info.u3+minU;
				V[0] = info.v0*vFactor+maxV;
				V[1] = info.v1*vFactor+maxV;
				V[2] = info.v2*vFactor+maxV;
				V[3] = info.v3*vFactor+maxV;
				return info.flip;
			}
		}
#define DO_OLD_UV
#ifdef DO_OLD_UV
// old uv adjustment for cliffs 
		static Real STRETCH_LIMIT = 1.5f;	 // If it is stretching less than this, don't adjust.
		static Real TILE_LIMIT = 4.0;			// Our tiles are currently 4 cells wide & tall, so dont'
																			// adjust to more than 4.0.

		static Real TALL_STRETCH_LIMIT = 2.0f; 
		static Real DIAMOND_STRETCH_LIMIT = 2.4f;
		static Real HEIGHT_SCALE = MAP_HEIGHT_SCALE / MAP_XY_FACTOR;

		Real nU2, nV2, xU2, xV2;
		nU2=nV2=xU2=xV2 = 0.0f;
		Int tilesPerRow2 = TEXTURE_WIDTH/(2*TILE_PIXEL_EXTENT+TILE_OFFSET);
		tilesPerRow2 *= 4;


		getUVForNdx(tileNdx, &nU2, &nV2, &xU2, &xV2, fullTile);
		U[0] = nU2; U[1] = xU2; U[2] = xU2; U[3] = nU2;
		V[0] = xV2; V[1] = xV2; V[2] = nV2; V[3] = nV2;
		if (TheGlobalData && !TheGlobalData->m_adjustCliffTextures) {
			return false;
		}
		if (nU2==0.0) {
			return false; // missing texture.
		}
		if (fullTile) {
			return false;
		}
		// check for excessive heights. 
		if (ndx < this->m_dataSize - m_width - 1) {
			Int h0 = m_data[ndx];
			Int h1 = m_data[ndx+1];
			Int h2 = m_data[ndx+m_width+1];
			Int h3 = m_data[ndx+m_width];
			Int minH, maxH;
			minH = maxH = h0;
			if (minH>h1) minH = h1;
			if (maxH<h1) maxH = h1;
			if (minH>h2) minH = h2;
			if (maxH<h2) maxH = h2;
			if (minH>h3) minH = h3;
			if (maxH<h3) maxH = h3;
//			Int avgH = (h1+h2+h3+h0+2)/4;
			Int deltaH = maxH-minH;
			Int below = 0;
			Int above = 0;
			Int belowLimit = minH+(2*deltaH+1)/3;
			Int aboveLimit = minH+(deltaH+1)/3;
			if (h0<belowLimit) below++;
			if (h1<belowLimit) below++;
			if (h2<belowLimit) below++;
			if (h3<belowLimit) below++;
			if (h0>aboveLimit) above++;
			if (h1>aboveLimit) above++;
			if (h2>aboveLimit) above++;
			if (h3>aboveLimit) above++;
			if (deltaH*HEIGHT_SCALE < STRETCH_LIMIT) { 
				return false;
			}

			Short baseNdx = tileNdx>>2;
			Short texClass;
			for (texClass=0; texClass<m_numTextureClasses; texClass++) {
				if (m_textureClasses[texClass].firstTile<0) {
					continue;
				}
				// see if the blend tile is in a texture class, and get the right tile for xIndex, yIndex.
				if (baseNdx >= m_textureClasses[texClass].firstTile && 
					baseNdx < m_textureClasses[texClass].firstTile+m_textureClasses[texClass].numTiles) {
					break;
				}
			}
			if (texClass>= m_numTextureClasses) return false;
			Real nUb, nVb, xUb, xVb;
			nUb = static_cast<Real>(m_textureClasses[texClass].positionInTexture.x);
			nVb = static_cast<Real>(m_textureClasses[texClass].positionInTexture.y);
			xUb = nUb+m_textureClasses[texClass].width*TILE_PIXEL_EXTENT; 
			xVb = nVb+m_textureClasses[texClass].width*TILE_PIXEL_EXTENT;
			nUb/=TEXTURE_WIDTH;
			nVb/=m_terrainTexHeight;
			xUb/=TEXTURE_WIDTH;
			xVb/=m_terrainTexHeight;
			// Now covers texture bounds.
			// too much stretch.
			Real divisor = TILE_LIMIT/(deltaH*HEIGHT_SCALE);
			if (divisor > TILE_LIMIT) divisor = TILE_LIMIT;
			if (divisor < 1.0f) divisor = 1.0f;
			Real deltaV = (xVb-nVb); 
//			Real deltaU = (xUb-nUb); 

			if (above != 1 && below != 1 && (above!=2 || below != 2)) {
				// diamond shaped.  Use default if it is not too stretched, as
				// the fix is not that appealing either.
				if (deltaH*HEIGHT_SCALE < DIAMOND_STRETCH_LIMIT) { 
					return false;
				}
			}

			if (below==1 || above>below) { //(avgH > minH + (2*deltaH+2)/3) 
				// we got one low guy.
				if (h0==minH) {
					V[0] = nV2+deltaV/divisor;
				} else if (h1 == minH) {
					V[1] = nV2+deltaV/divisor;
				}	else if (h2 == minH) {
					V[2] = xV2-deltaV/divisor;
				} else if (h3 == minH) {
					V[3] = xV2-deltaV/divisor;
				}
			}	else if (above==1 || below>above) { //(avgH < minH + (deltaH+1)/3) 
				// we got one high guy
				if (h0==maxH) {
					V[0] = nV2+deltaV/divisor;
				} else if (h1 == maxH) {
					V[1] = nV2+deltaV/divisor;
				}	else if (h2 == maxH) {
					V[2] = xV2-deltaV/divisor;
				} else if (h3 == maxH) {
					V[3] = xV2-deltaV/divisor;
				}
			} else {
				// we got two up and two down.
				if (deltaH*HEIGHT_SCALE < TALL_STRETCH_LIMIT) { 
					return false;
				}
				Real dx = (h3-h2)*HEIGHT_SCALE;
				dx = sqrt(1+dx*dx); // lenght of the bottom of the cell
				Real dy =	(h3-h0)*HEIGHT_SCALE;
				dy = sqrt(1+dy*dy); // length of the left side.
				if (dx<STRETCH_LIMIT) dx = 1.0f; // don't make a seam unless there is great stretch. 
				if (dy<STRETCH_LIMIT) dy = 1.0f; // don't make a seam unless there is great stretch. 
				if (dx>TILE_LIMIT) dx = TILE_LIMIT; // don't tile past the texture's edge. 
				if (dy>TILE_LIMIT) dy = TILE_LIMIT; // don't tile past the texture's edge. 
				dx *= xU2-nU2;
				dy *= xV2-nV2;
				U[0] = nU2; U[1] = nU2+dx; U[2] = nU2+dx; U[3] = nU2;
				V[0] = nV2+dy; V[1] = nV2+dy; V[2] = nV2; V[3] = nV2;
				if (below==1) {
					below = 1;
				}
				// recalc for point 1.
				dx = (h1-h0)*HEIGHT_SCALE;
				dx = sqrt(1+dx*dx); // lenght of the bottom of the cell
				dy =	(h2-h1)*HEIGHT_SCALE;
				dy = sqrt(1+dy*dy); // length of the left side.
				if (dx<STRETCH_LIMIT) dx = 1.0f; // don't make a seam unless there is great stretch. 
				if (dy<STRETCH_LIMIT) dy = 1.0f; // don't make a seam unless there is great stretch. 
				if (dx>TILE_LIMIT) dx = TILE_LIMIT; // don't tile past the texture's edge. 
				if (dy>TILE_LIMIT) dy = TILE_LIMIT; // don't tile past the texture's edge. 
				dx *= xU2-nU2;
				dy *= xV2-nV2;
				U[1] = U[0]+dx;
				V[1] = V[3] + dy;
			}
			// Make sure we are within the texture;
			Real adjU = 0;
			Real adjV = 0;
			Int i;
			for (i=0; i<4; i++) {
				if (nVb - V[i] > adjV) adjV = nVb - V[i];
			}
			for (i=0; i<4; i++) {
				V[i] += adjV;
			}
			adjV = 0;
			for (i=0; i<4; i++) {
				if (U[i] - xUb > adjU) adjU = U[i]-xUb;
				if (V[i] - xVb > adjV) adjV = V[i]-xVb;
			}
			for (i=0; i<4; i++) {
				U[i] -= adjU;
				V[i] -= adjV;
			}
		}	
		return true;
// 
#endif

	}	
	return false;
}

///@todo: Are the different "if" cases mutually exclusive?  If so, should add else statements.
Bool WorldHeightMap::getExtraAlphaUVData(Int xIndex, Int yIndex, float U[4], float V[4], UnsignedByte alpha[4], Bool *needFlip, Bool *cliff)
{
	Int ndx = (yIndex*m_width)+xIndex;
	*needFlip = FALSE;
	*cliff = FALSE;

	if ( (ndx>=0) && (ndx<m_dataSize) && m_tileNdxes) {
		Short blendNdx = m_extraBlendTileNdxes[ndx];
		if (blendNdx == 0) {
			return FALSE;
		} else {
			*cliff = getUVForTileIndex(ndx, static_cast<Short>(m_blendedTiles[blendNdx].blendNdx), U, V, FALSE);
			alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
			if (m_blendedTiles[blendNdx].horiz) {
				// Horizontals don't need flipping unless forced because of 3way blend
				// and a diagonal in base blend layer.
				*needFlip = m_blendedTiles[blendNdx].inverted & FLIPPED_MASK;
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[0] = alpha[3] = 255;
				} else {
					alpha[1] = alpha[2] = 255;
				}
			}
			if (m_blendedTiles[blendNdx].vert) {
				// Verticals don't need flipping unless forced because of 3way blend
				// and a diagonal in base blend layer.
				*needFlip = m_blendedTiles[blendNdx].inverted & FLIPPED_MASK;
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[0] = alpha[1] = 255;
				} else {
					alpha[2] = alpha[3]  = 255;
				}
			}
			if (m_blendedTiles[blendNdx].rightDiagonal) {
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[1] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[0] = 255;
						alpha[2] = 255;
					}
				} else {
					// Uninverted right diagonals need flipping.
					*needFlip = TRUE;
					alpha[2] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[1] = 255;
						alpha[3] = 255;
					}
				}
			}
			if (m_blendedTiles[blendNdx].leftDiagonal) {
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					// Inverted left diagonals need flipping.
					*needFlip = TRUE;
					alpha[0] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[1] = 255;
						alpha[3] = 255;
					}
				} else {
					alpha[3] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[0] = 255;
						alpha[2] = 255;
					}
				}
			}
			if (m_blendedTiles[blendNdx].customBlendEdgeClass>=0) {
				alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
				// No alpha blend, so never need to flip.
				*needFlip = FALSE;
			}
		}
	}

	return TRUE;
}

/** getUVData - Gets the texture coordinates to use with the alpha texture.  
		xIndex and yIndex are the integer coorddinates into the height map.
		U and V are the texture coordiantes for the 4 corners of a height map cell.
		fullTile is true if we are doing 1/2 resolution height map, and require a full
		tile to texture  a cell.  Otherwise, we use quarter tiles per cell.
		flip is set if we need to flip the diagonal across the cell to make the 
		alpha coordinates blend properly.  Filling a square with 2 triangles is not symmetrical :)
*/
void WorldHeightMap::getAlphaUVData(Int xIndex, Int yIndex, float U[4], float V[4], 
																		UnsignedByte alpha[4], Bool *flip, Bool fullTile)
{
	xIndex += m_drawOriginX;
	yIndex += m_drawOriginY;
	Int ndx = (yIndex*m_width)+xIndex;
	Bool stretchedForCliff = false;
	Bool needFlip = false;

	if ((ndx<m_dataSize) && m_tileNdxes) {
		Short blendNdx = m_blendTileNdxes[ndx];
		if (fullTile) blendNdx = 0;
		if (blendNdx == 0) {
			stretchedForCliff = getUVForTileIndex(ndx, m_tileNdxes[ndx], U, V, fullTile);		
			alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
			// No alpha blend, so never need to flip.
			needFlip = false;
		} else {
			stretchedForCliff = getUVForTileIndex(ndx, static_cast<Short>(m_blendedTiles[blendNdx].blendNdx), U, V, fullTile);
			alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
			if (m_blendedTiles[blendNdx].horiz) {
				// Horizontals don't need flipping unless forced because of 3way blend.
				needFlip = m_blendedTiles[blendNdx].inverted & FLIPPED_MASK;
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[0] = alpha[3] = 255;
				} else {
					alpha[1] = alpha[2] = 255;
				}
			}
			if (m_blendedTiles[blendNdx].vert) {
				// Verticals don't need flipping unless forced because of 3way blend.
				needFlip = m_blendedTiles[blendNdx].inverted & FLIPPED_MASK;
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[0] = alpha[1] = 255;
				} else {
					alpha[2] = alpha[3]  = 255;
				}
			}
			if (m_blendedTiles[blendNdx].rightDiagonal) {
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					alpha[1] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[0] = 255;
						alpha[2] = 255;
					}
				} else {
					// Uninverted right diagonals need flipping.
					needFlip = true;
					alpha[2] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[1] = 255;
						alpha[3] = 255;
					}
				}
			}
			if (m_blendedTiles[blendNdx].leftDiagonal) {
				if (m_blendedTiles[blendNdx].inverted & INVERTED_MASK) {
					// Inverted left diagonals need flipping.
					needFlip = true;
					alpha[0] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[1] = 255;
						alpha[3] = 255;
					}
				} else {
					alpha[3] = 255;
					if (m_blendedTiles[blendNdx].longDiagonal) {
						alpha[0] = 255;
						alpha[2] = 255;
					}
				}
			}
			if (m_blendedTiles[blendNdx].customBlendEdgeClass>=0) {
				alpha[0] = alpha[1] = alpha[2] = alpha[3] = 0;
				// No alpha blend, so never need to flip.
				needFlip = false;
			}
		}
	}
	if (stretchedForCliff) {
		// If we had to stretch for clif, check heights.
		Int p0=getHeight(xIndex, yIndex);
		Int p1=getHeight(xIndex+1, yIndex);
		Int p2=getHeight(xIndex+1, yIndex+1);
		Int p3=getHeight(xIndex, yIndex+1);
		Int dz1 = abs(p0-p2);
		Int dz2 = abs(p1-p3);
		needFlip = dz1>dz2;
	}
#ifdef FLIP_TRIANGLES
	*flip = needFlip;
#endif
}

void WorldHeightMap::setTextureLOD(Int lod)
{
	if (m_terrainTex)
		m_terrainTex->setLOD(lod);
}

//-------------------------------------------------------------------------------------------------
// Terrain texture -> detail material mapping from ProjectData\INI\BodenDetail.ini. Lines have the form
// "<file>.tga = <material> ; comment"; material numbers match the detail texture channels (terraindetail.hlsli).
//-------------------------------------------------------------------------------------------------
static const char *const TERRAIN_DETAIL_MATERIALS[] =
	{ "keins", "gras", "trockengras", "erde", "kies", "sand", "fels", "schnee", "asphalt" };

struct TerrainDetailEntry
{
	char texture[48];
	Int material;
};

// Fixed array instead of a container so nothing on the heap lives until process exit (leak report).
// Zero Hour and Generals together have about 250 terrain textures.
static const Int MAX_TERRAIN_DETAIL_ENTRIES = 512;
static TerrainDetailEntry s_terrainDetailMap[MAX_TERRAIN_DETAIL_ENTRIES];
static Int s_terrainDetailCount = 0;
static Bool s_terrainDetailMapLoaded = FALSE;

static void trimTerrainDetailField(char *s)
{
	char *start = s;
	while (*start == ' ' || *start == '\t')
		++start;
	if (start != s)
		memmove(s, start, strlen(start) + 1);
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
		s[--n] = 0;
}

static void loadTerrainDetailMap(void)
{
	s_terrainDetailMapLoaded = TRUE;
	s_terrainDetailCount = 0;
	File *file = TheFileSystem ? TheFileSystem->openFile("ProjectData\\INI\\BodenDetail.ini", File::READ | File::BINARY) : nullptr;
	if (file == nullptr)
		return;
	const Int size = file->size();
	std::vector<char> text(static_cast<size_t>(size > 0 ? size : 0) + 1, 0);
	if (size > 0)
		file->read(text.data(), size);
	file->close();
	char *next = text.data();
	while (next && *next)
	{
		char line[256];
		char *end = strchr(next, '\n');
		const size_t n = end ? static_cast<size_t>(end - next) : strlen(next);
		const size_t copy = (n < sizeof(line) - 1) ? n : sizeof(line) - 1;
		memcpy(line, next, copy);
		line[copy] = 0;
		next = end ? end + 1 : nullptr;
		char *comment = strchr(line, ';');
		if (comment)
			*comment = 0;
		char *eq = strchr(line, '=');
		if (eq == nullptr)
			continue;
		*eq = 0;
		char *name = line;
		char *value = eq + 1;
		trimTerrainDetailField(name);
		trimTerrainDetailField(value);
		if (*name == 0 || strlen(name) >= sizeof(TerrainDetailEntry::texture) || s_terrainDetailCount >= MAX_TERRAIN_DETAIL_ENTRIES)
			continue;
		for (Int m = 0; m < static_cast<Int>(sizeof(TERRAIN_DETAIL_MATERIALS) / sizeof(TERRAIN_DETAIL_MATERIALS[0])); ++m)
		{
			if (_stricmp(value, TERRAIN_DETAIL_MATERIALS[m]) == 0)
			{
				TerrainDetailEntry &entry = s_terrainDetailMap[s_terrainDetailCount++];
				WWFormat::Into(entry.texture, "{}", name);
				entry.material = m;
				break;
			}
		}
	}
	DEBUG_LOG(("Terrain detail: %d mappings from BodenDetail.ini\n", s_terrainDetailCount));
}

static Int findTerrainDetailMaterial(const char *texture)
{
	if (!s_terrainDetailMapLoaded)
		loadTerrainDetailMap();
	for (Int i = 0; i < s_terrainDetailCount; ++i)
	{
		if (_stricmp(s_terrainDetailMap[i].texture, texture) == 0)
			return s_terrainDetailMap[i].material;
	}
	return 0;
}

/** Table "atlas grid cell -> detail material and tile origin" for the terrain shader.
	Each class occupies width x width cells of (TILE_PIXEL_EXTENT + TILE_OFFSET) pixels from its origin
	(updateTileTexturePositions); tiles are spaced TILE_PIXEL_EXTENT apart from the origin. */
void WorldHeightMap::updateTerrainDetailTable(void)
{
	static RenderTerrainDetailTable table;
	const Int cell = TILE_PIXEL_EXTENT + TILE_OFFSET;
	const Int columns = TEXTURE_WIDTH / cell;
	Int rows = (m_terrainTexHeight + cell - 1) / cell;
	if (rows > columns)
		rows = columns;
	if (columns * rows > RenderTerrainDetailTable::MAX_CELLS)
		rows = RenderTerrainDetailTable::MAX_CELLS / columns;
	memset(&table, 0, sizeof(table));
	table.atlas_width = static_cast<float>(TEXTURE_WIDTH);
	table.atlas_height = static_cast<float>(m_terrainTexHeight);
	table.cell_pixels = static_cast<float>(cell);
	table.tile_pixels = static_cast<float>(TILE_PIXEL_EXTENT);
	table.columns = static_cast<unsigned>(columns);
	table.rows = static_cast<unsigned>(rows);
	Int withDetail = 0;
	for (Int texClass = 0; texClass < m_numTextureClasses; ++texClass)
	{
		const TXTextureClass &tc = m_textureClasses[texClass];
		if (tc.positionInTexture.x <= 0 || tc.width <= 0)
			continue;
		// average color of the class for the rock layer, also without a detail material
		double sum[3] = { 0.0, 0.0, 0.0 };
		double count = 0.0;
		for (Int t = 0; t < tc.numTiles; ++t)
		{
			TileData *tile = getSourceTile(static_cast<UnsignedInt>(tc.firstTile + t));
			if (tile == nullptr || tile->getDataPtr() == nullptr)
				continue;
			const UnsignedByte *px = tile->getDataPtr();
			const Int n = tile->dataLen() / TILE_BYTES_PER_PIXEL;
			for (Int k = 0; k < n; ++k, px += TILE_BYTES_PER_PIXEL)
			{
				sum[0] += px[2];	// BGRA
				sum[1] += px[1];
				sum[2] += px[0];
			}
			count += n;
		}
		const unsigned color = (count > 0.0)
			? (0xFF000000u | (static_cast<unsigned>(sum[0] / count + 0.5) << 16)
				| (static_cast<unsigned>(sum[1] / count + 0.5) << 8) | static_cast<unsigned>(sum[2] / count + 0.5))
			: 0u;
		TerrainType *terrain = TheTerrainTypes ? TheTerrainTypes->findTerrain(tc.name) : nullptr;
		const Int material = terrain ? findTerrainDetailMaterial(terrain->getTexture().str()) : 0;
		if (material > 0)
			++withDetail;
		const Int column0 = (tc.positionInTexture.x - TILE_OFFSET / 2) / cell;
		const Int row0 = (tc.positionInTexture.y - TILE_OFFSET / 2) / cell;
		const unsigned value = (material > 0) ? (static_cast<unsigned>(material)
			| (static_cast<unsigned>(tc.positionInTexture.x % TILE_PIXEL_EXTENT) << 8)
			| (static_cast<unsigned>(tc.positionInTexture.y % TILE_PIXEL_EXTENT) << 16)) : 0u;
		for (Int j = 0; j < tc.width; ++j)
		{
			for (Int i = 0; i < tc.width; ++i)
			{
				const Int c = column0 + i;
				const Int r = row0 + j;
				if (c >= 0 && c < columns && r >= 0 && r < rows)
				{
					table.cells[r * columns + c] = value;
					table.colors[r * columns + c] = color;
				}
			}
		}
	}
	RenderDevice::Set_Terrain_Detail_Table(table);
	char line[128];
	WWFormat::Into(line, "BODENDETAIL klassen {:d} mit_detail {:d} zuordnungen {:d} raster {:d} x {:d} atlas {:d}", m_numTextureClasses, withDetail, s_terrainDetailCount, columns, rows, m_terrainTexHeight);
	GameEngine_RunnerLog(line);
	DEBUG_LOG(("Terrain detail: %s\n", line));
}

/** Relief atlas: the relief tiles at the positions of their colour tiles, flat where a class has none. Same
	logical layout as m_terrainTex, so the terrain shader reads it with the atlas coordinates. */
void WorldHeightMap::updateTerrainReliefAtlas(Int pow2Height)
{
	if (m_terrainReliefTex) {
		RenderDevice::Set_Terrain_Relief_Atlas(m_terrainReliefTex->Peek_D3D_Texture(), false);
		REF_PTR_RELEASE(m_terrainReliefTex);
	}
	Int tiles = 0;
	for (Int i = 0; i < m_numBitmapTiles && i < NUM_SOURCE_TILES; ++i) {
		if (m_reliefTiles[i] == nullptr)
			continue;
		if (m_sourceTiles[i] == nullptr) {
			REF_PTR_RELEASE(m_reliefTiles[i]);
			continue;
		}
		m_reliefTiles[i]->m_tileLocationInTexture = m_sourceTiles[i]->m_tileLocationInTexture;
		++tiles;
	}
	if (tiles == 0)
		return;
	const Int scale = TileData::unifyExtent(m_reliefTiles, m_numBitmapTiles, false);
	m_terrainReliefTex = MSGNEW("WorldHeightMap_getTerrainTexture") TerrainTextureClass(pow2Height, scale);
	m_terrainReliefTex->updateRelief(this, scale);
	RenderDevice::Set_Terrain_Relief_Atlas(m_terrainReliefTex->Peek_D3D_Texture(), true);
	char line[128];
	WWFormat::Into(line, "TERRAINRELIEF kacheln {:d} von {:d} stufe {:d} atlas {:d} x {:d}", tiles, m_numBitmapTiles, scale,
		TEXTURE_WIDTH * scale, pow2Height * scale);
	GameEngine_RunnerLog(line);
	DEBUG_LOG(("%s\n", line));
}

/** Height atlas of the terrain tessellation: the height tiles at the positions of their colour tiles (128 where a class
	has none), the place of every class in the atlas, and per grid point the two classes that make up most of the
	colour there. The domain shader reads a class where the base pass shows it (getUVForTileIndex: tile and quarter
	from the cell position modulo the class width), so the heights are a function of the world position. */
void WorldHeightMap::updateTerrainHeightAtlas(Int pow2Height)
{
	if (m_terrainHeightTex) {
		RenderTerrainTessData drop = {};
		drop.atlas = m_terrainHeightTex->Peek_D3D_Texture();
		RenderDevice::Set_Terrain_Tess_Data(drop, false);
		REF_PTR_RELEASE(m_terrainHeightTex);
	}
	Int tiles = 0;
	for (Int i = 0; i < m_numBitmapTiles && i < NUM_SOURCE_TILES; ++i) {
		if (m_heightTiles[i] == nullptr)
			continue;
		if (m_sourceTiles[i] == nullptr) {
			REF_PTR_RELEASE(m_heightTiles[i]);
			continue;
		}
		m_heightTiles[i]->m_tileLocationInTexture = m_sourceTiles[i]->m_tileLocationInTexture;
		++tiles;
	}
	// Without height tiles the tessellation still runs for the rock displacement on steep slopes (then with an
	// atlas of flat heights).
	const UnsignedInt rockTess = RENDER_EFFECT_TERRAIN_TESS | RENDER_EFFECT_TERRAIN_ROCK | RENDER_EFFECT_ROCK_TESS;
	const Bool rockOnly = tiles == 0 && TheGlobalData && (TheGlobalData->m_renderEffects & rockTess) == rockTess;
	if ((tiles == 0 && !rockOnly) || m_terrainTexHeight <= 0 || m_width < 2 || m_height < 2)
		return;
	const Int scale = (tiles > 0) ? TileData::unifyExtent(m_heightTiles, m_numBitmapTiles, false) : 1;
	m_terrainHeightTex = MSGNEW("WorldHeightMap_getTerrainTexture") TerrainTextureClass(pow2Height, scale);
	m_terrainHeightTex->updateHeights(this, scale);

	static RenderTerrainTessData data;		// 4 KB class table, filled anew for every map
	::memset(&data, 0, sizeof(data));
	data.atlas = m_terrainHeightTex->Peek_D3D_Texture();
	data.cell_u = static_cast<Real>(TILE_PIXEL_EXTENT / 2) / TEXTURE_WIDTH;
	data.cell_v = static_cast<Real>(TILE_PIXEL_EXTENT / 2) / m_terrainTexHeight;
	Int classes = 0;
	for (Int c = 0; c < m_numTextureClasses && c < RenderTerrainTessData::MAX_CLASSES; ++c) {
		const TXTextureClass &tc = m_textureClasses[c];
		if (tc.positionInTexture.x <= 0 || tc.width <= 0 || tc.firstTile < 0)
			continue;
		Bool heights = FALSE;
		for (Int t = 0; t < tc.numTiles; ++t) {
			const Int ndx = tc.firstTile + t;
			if (ndx >= 0 && ndx < NUM_SOURCE_TILES && m_heightTiles[ndx])
				heights = TRUE;
		}
		data.classes[c][0] = static_cast<Real>(tc.positionInTexture.x) / TEXTURE_WIDTH;
		data.classes[c][1] = static_cast<Real>(tc.positionInTexture.y + tc.width * TILE_PIXEL_EXTENT) / m_terrainTexHeight;
		data.classes[c][2] = static_cast<Real>(2 * tc.width);
		data.classes[c][3] = heights ? 1.0f : 0.0f;
		if (heights)
			++classes;
	}
	data.class_count = (m_numTextureClasses < RenderTerrainTessData::MAX_CLASSES) ? m_numTextureClasses : RenderTerrainTessData::MAX_CLASSES;
	std::vector<UnsignedByte> weights(static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4);
	const Int mismatched = bakeTessWeights(weights.data());
	data.weights = weights.data();
	data.width = m_width;
	data.height = m_height;
	RenderDevice::Set_Terrain_Tess_Data(data, true);
	char line[160];
	WWFormat::Into(line, "TERRAINTESS kacheln {:d} von {:d} stufe {:d} klassen {:d} von {:d} gitter {:d} x {:d} abweichend {:d}", tiles,
		m_numBitmapTiles, scale, classes, m_numTextureClasses, m_width, m_height, mismatched);
	GameEngine_RunnerLog(line);
	DEBUG_LOG(("%s\n", line));
}

Int WorldHeightMap::bakeTessWeights(UnsignedByte *rgba)
{
	// local texture class of each source tile
	Short tileClass[NUM_SOURCE_TILES];
	for (Int t = 0; t < NUM_SOURCE_TILES; ++t)
		tileClass[t] = -1;
	for (Int c = 0; c < m_numTextureClasses && c < RenderTerrainTessData::MAX_CLASSES; ++c) {
		for (Int t = 0; t < m_textureClasses[c].numTiles; ++t) {
			const Int ndx = m_textureClasses[c].firstTile + t;
			if (ndx >= 0 && ndx < NUM_SOURCE_TILES)
				tileClass[ndx] = static_cast<Short>(c);
		}
	}
	auto classOf = [&](Int tileNdx) -> Int {
		const Int t = tileNdx >> 2;
		return (t >= 0 && t < NUM_SOURCE_TILES) ? tileClass[t] : -1;
	};

	// Per cell: base, blend and three-way class with their corner alphas (as the base pass and renderExtraBlendTiles
	// draw them), and whether the cell has cliff coordinates (not placed by world position).
	struct CellMix
	{
		Short base, blend, extra;
		UnsignedByte a1[4], a2[4];
		Bool cliff;
	};
	const Int cellsX = m_width - 1, cellsY = m_height - 1;
	std::vector<CellMix> cells(static_cast<size_t>(cellsX) * static_cast<size_t>(cellsY));
	Int mismatched = 0;
	for (Int y = 0; y < cellsY; ++y) {
		for (Int x = 0; x < cellsX; ++x) {
			CellMix &m = cells[static_cast<size_t>(y) * cellsX + x];
			::memset(&m, 0, sizeof(m));
			m.blend = m.extra = -1;
			const Int ndx = y * m_width + x;
			m.base = static_cast<Short>(classOf(m_tileNdxes[ndx]));
			m.cliff = m_cliffInfoNdxes[ndx] != 0;
			if (m.base >= 0) {
				const TXTextureClass &tc = m_textureClasses[m.base];
				const Int w = (tc.width > 0) ? tc.width : 1;
				const Int expected = ((tc.firstTile + ((x / 2) % w) + w * ((y / 2) % w)) << 2) + 2 * (y & 1) + (x & 1);
				if (expected != m_tileNdxes[ndx])
					++mismatched;
			}
			Real U[4], V[4];
			Bool flip = FALSE, cliff = FALSE;
			const Short blendNdx = m_blendTileNdxes[ndx];
			if (blendNdx > 0 && blendNdx < NUM_BLEND_TILES) {
				getAlphaUVData(x - m_drawOriginX, y - m_drawOriginY, U, V, m.a1, &flip, FALSE);
				m.blend = static_cast<Short>(classOf(m_blendedTiles[blendNdx].blendNdx));
			}
			const Short extraNdx = m_extraBlendTileNdxes[ndx];
			if (extraNdx > 0 && extraNdx < NUM_BLEND_TILES && getExtraAlphaUVData(x, y, U, V, m.a2, &flip, &cliff))
				m.extra = static_cast<Short>(classOf(m_blendedTiles[extraNdx].blendNdx));
		}
	}

	// Per grid point: the mixes of the up to four cells around it, averaged; the two largest classes. Corners of a
	// cell: 0 = (x, y), 1 = (x+1, y), 2 = (x+1, y+1), 3 = (x, y+1).
	for (Int gy = 0; gy < m_height; ++gy) {
		for (Int gx = 0; gx < m_width; ++gx) {
			Int cls[12];
			Real wt[12];
			Int n = 0, count = 0;
			Bool cliff = FALSE;
			auto add = [&](Int c, Real w) {
				if (c < 0 || w <= 0.0f)
					return;
				for (Int i = 0; i < n; ++i) {
					if (cls[i] == c) {
						wt[i] += w;
						return;
					}
				}
				cls[n] = c;
				wt[n] = w;
				++n;
			};
			for (Int cy = gy - 1; cy <= gy; ++cy) {
				for (Int cx = gx - 1; cx <= gx; ++cx) {
					if (cx < 0 || cy < 0 || cx >= cellsX || cy >= cellsY)
						continue;
					const CellMix &m = cells[static_cast<size_t>(cy) * cellsX + cx];
					const Int dx = gx - cx, dy = gy - cy;
					const Int k = (dy == 0) ? dx : (dx == 1 ? 2 : 3);
					const Real a1 = (m.blend >= 0) ? m.a1[k] / 255.0f : 0.0f;
					const Real a2 = (m.extra >= 0) ? m.a2[k] / 255.0f : 0.0f;
					add(m.base, (1.0f - a1) * (1.0f - a2));
					add(m.blend, a1 * (1.0f - a2));
					add(m.extra, a2);
					cliff = cliff || m.cliff;
					++count;
				}
			}
			UnsignedByte *out = rgba + (static_cast<size_t>(gy) * m_width + gx) * 4;
			out[0] = out[1] = out[2] = out[3] = 0;
			if (cliff || count == 0 || n == 0)
				continue;
			Real total = 0.0f;
			Int best = 0, second = -1;
			for (Int i = 0; i < n; ++i) {
				total += wt[i];
				if (wt[i] > wt[best])
					best = i;
			}
			for (Int i = 0; i < n; ++i) {
				if (i != best && (second < 0 || wt[i] > wt[second]))
					second = i;
			}
			const Real kept = wt[best] + ((second >= 0) ? wt[second] : 0.0f);
			const Real scale = (kept > 0.0f) ? (total / count) / kept : 0.0f;
			out[0] = static_cast<UnsignedByte>(cls[best]);
			out[1] = static_cast<UnsignedByte>(wt[best] * scale * 255.0f + 0.5f);
			if (second >= 0) {
				out[2] = static_cast<UnsignedByte>(cls[second]);
				out[3] = static_cast<UnsignedByte>(wt[second] * scale * 255.0f + 0.5f);
			}
		}
	}
	return mismatched;
}

TextureClass *WorldHeightMap::getTerrainTexture(void)
{
	if (m_terrainTex == nullptr) {
		Int edgeHeight;
		Int height = updateTileTexturePositions(&edgeHeight);
		Int pow2Height = 1;
		while (pow2Height<height) {
			pow2Height *=2;
		}
		REF_PTR_RELEASE(m_terrainTex);
		// Tiles may be 64*s pixels depending on the texture quality. Bring all tiles to the largest size (edge tiles
		// unfiltered, their alpha depends on exact black/white), then for s > 1 build a 32-bit atlas s times larger.
		// The heights (m_terrainTexHeight, m_alphaEdgeHeight) stay logical so all texture coordinates remain valid.
		Int tileScale = TileData::unifyExtent(m_sourceTiles, m_numBitmapTiles, false);
		const Int edgeScale = TileData::unifyExtent(m_edgeTiles, m_numEdgeTiles, true, tileScale*TILE_PIXEL_EXTENT);
		if (edgeScale > tileScale) {
			tileScale = TileData::unifyExtent(m_sourceTiles, m_numBitmapTiles, false, edgeScale*TILE_PIXEL_EXTENT);
		}
		if (tileScale > 1) {
			m_terrainTex = MSGNEW("WorldHeightMap_getTerrainTexture") TerrainTextureClass(pow2Height, tileScale);
			m_terrainTexHeight = m_terrainTex->updateScaled(this, tileScale);
		} else {
			m_terrainTex = MSGNEW("WorldHeightMap_getTerrainTexture") TerrainTextureClass(pow2Height);
			m_terrainTexHeight = m_terrainTex->update(this);
		}
		char buf[64];
		WWFormat::Into(buf, "Base tex height {:d}\n", pow2Height);
		DEBUG_LOG(("%s", buf));
		updateTerrainDetailTable();
		updateTerrainReliefAtlas(pow2Height);
		updateTerrainHeightAtlas(pow2Height);
		// atlas size for the runner log; A8R8G8B8, mips estimated as 21/16
		if (TheGlobalData && TheGlobalData->m_logTextureStats > 0)
		{
			char line[128];
			WWFormat::Into(line, "TERRAINATLAS {:d} x {:d} used {:d} bytes {:d}", TEXTURE_WIDTH, pow2Height, m_terrainTexHeight, (TEXTURE_WIDTH * pow2Height * 4 * 21) / 16);
			GameEngine_RunnerLog(line);
			if (tileScale > 1) {
				// physical size of the scaled atlas (32 bit, mips estimated as 21/16)
				WWFormat::Into(line, "TERRAINATLAS_HD scale {:d} {:d} x {:d} bytes {:d}", tileScale, TEXTURE_WIDTH*tileScale, pow2Height*tileScale, (TEXTURE_WIDTH*tileScale * pow2Height*tileScale * 4 * 21) / 16);
				GameEngine_RunnerLog(line);
			}
		}
		REF_PTR_RELEASE(m_alphaTerrainTex);
		m_alphaTerrainTex = MSGNEW("WorldHeightMap_getTerrainTexture") AlphaTerrainTextureClass(m_terrainTex);

		pow2Height = 1;
		while (pow2Height<edgeHeight) {
			pow2Height *=2;
		}
		REF_PTR_RELEASE(m_alphaEdgeTex);
		if (tileScale > 1) {	// scaled atlas as above
			m_alphaEdgeTex = MSGNEW("WorldHeightMap_getTerrainTexture") AlphaEdgeTextureClass(pow2Height, tileScale);
			m_alphaEdgeHeight = m_alphaEdgeTex->updateScaled(this, tileScale);
		} else {
			m_alphaEdgeTex = MSGNEW("WorldHeightMap_getTerrainTexture") AlphaEdgeTextureClass(pow2Height);
			m_alphaEdgeHeight = m_alphaEdgeTex->update(this);
		}

		//Generate lookup table for determining triangle order in each terrain cell.
		//Not the best place to put this but getAlphaUVData() requires a valid terrain
		//texture to return valid values.

		for (Int y=0; y<(m_height-1); y++)
			for (Int x=0; x<(m_width-1); x++)
			{
				UnsignedByte alpha[4];
				float UA[4], VA[4];
				Bool flipForBlend;

				getAlphaUVData(x, y, UA, VA, alpha, &flipForBlend, false);

				m_cellFlipState[y*m_flipStateWidth+(x>>3)] |= flipForBlend << (x & 0x7);
				DEBUG_ASSERTCRASH ((y*m_flipStateWidth+(x>>3)) < (m_flipStateWidth * m_height), ("Bad range"));
			}
	}

	return m_terrainTex;
}

TextureClass *WorldHeightMap::getAlphaTerrainTexture(void)
{
	if (m_alphaTerrainTex == nullptr) {
		getTerrainTexture();
	}
	return m_alphaTerrainTex;
}
	
TextureClass *WorldHeightMap::getEdgeTerrainTexture(void)
{
	if (m_alphaEdgeTex == nullptr) {
		getTerrainTexture();
	}
	return m_alphaEdgeTex;
}

Bool WorldHeightMap::setDrawOrg(Int xOrg, Int yOrg)
{
	Int newX, newY;
	Int newWidth, newHeight;
	newX = xOrg;
	newY = yOrg;
	newWidth = m_drawWidthX;
	newHeight = m_drawHeightY;
	if (TheGlobalData && TheGlobalData->m_stretchTerrain) {
		newWidth=STRETCH_DRAW_WIDTH;
		newHeight=STRETCH_DRAW_HEIGHT;
	}
	if (TheGlobalData && TheGlobalData->m_drawEntireTerrain) {
		newWidth=m_width;
		newHeight=m_height;
	}
	if (newWidth > m_width) newWidth = m_width;
	if (newHeight > m_height) newHeight = m_height;
	if (newX > m_width - newWidth) newX = m_width-newWidth; 
	if (newX<0) newX=0;
	if (newY > m_height - newHeight) newY = m_height - newHeight; 
	if (newY<0) newY=0;
	Bool anythingDifferent = (m_drawOriginX!=newX) ||
										 (m_drawOriginY!=newY) ||
										 (m_drawWidthX!=newWidth) ||
										 (m_drawHeightY!=newHeight) ;

	if (anythingDifferent) {
		m_drawOriginX=newX;
		m_drawOriginY=newY;
		m_drawWidthX=newWidth;
		m_drawHeightY=newHeight;
		return(true);
	}
	return(false);
}

/** Gets global texture class. */
Int WorldHeightMap::getTextureClass(Int xIndex, Int yIndex, Bool baseClass)
{
	Int ndx = (yIndex*m_width)+xIndex;
	DEBUG_ASSERTCRASH((ndx>=0 && ndx<this->m_dataSize),("oops"));
	if (ndx<0 || ndx >= this->m_dataSize) return(-1);
	Int textureNdx = m_tileNdxes[ndx];
	if (!baseClass && (m_blendTileNdxes[ndx] != 0 || m_extraBlendTileNdxes[ndx] != 0)) {
		return(-1);  // blended, so not of the original class.
	}
	return getTextureClassFromNdx(textureNdx);
}


/** Sets all the cliff flags in map based on height. */
void WorldHeightMap::initCliffFlagsFromHeights()
{
	Int xIndex, yIndex;

	for (xIndex=0; xIndex<m_width-1; xIndex++) {
		for (yIndex=0; yIndex<m_height-1; yIndex++) {
			setCellCliffFlagFromHeights(xIndex, yIndex);
		}
	}
}

/** Sets the cliff flag for a cell based on height. */
void WorldHeightMap::setCellCliffFlagFromHeights(Int xIndex, Int yIndex)
{
	Real height1 = getHeight(xIndex, yIndex)*MAP_HEIGHT_SCALE;
	Real height2 = getHeight(xIndex+1, yIndex)*MAP_HEIGHT_SCALE;
	Real height3 = getHeight(xIndex, yIndex+1)*MAP_HEIGHT_SCALE;
	Real height4 = getHeight(xIndex+1, yIndex+1)*MAP_HEIGHT_SCALE;
	Real minZ = height1;
	if (minZ > height2) minZ = height2;
	if (minZ > height3) minZ = height3;
	if (minZ > height4) minZ = height4;
	Real maxZ = height1;
	if (maxZ < height2) maxZ = height2;
	if (maxZ < height3) maxZ = height3;
	if (maxZ < height4) maxZ = height4;
	const Real cliffRange = PATHFIND_CLIFF_SLOPE_LIMIT_F;	
	Bool isCliff = (maxZ-minZ > cliffRange);
	setCliffState(xIndex, yIndex, isCliff);

}

/** Gets global texture class. */
Int WorldHeightMap::getTextureClassFromNdx(Int tileNdx) 
{
	Int i;
	tileNdx = tileNdx>>2;
	for (i=0; i<m_numTextureClasses; i++) {
		if (m_textureClasses[i].firstTile<0) {
			continue;
		}
		// see if the blend tile is in a texture class, and get the right tile for xIndex, yIndex.
		if (tileNdx >= m_textureClasses[i].firstTile && 
			tileNdx < m_textureClasses[i].firstTile+m_textureClasses[i].numTiles) {
			return(m_textureClasses[i].globalTextureClass);
		}
	}
	return(-1);
}

TXTextureClass WorldHeightMap::getTextureFromIndex( Int textureIndex )
{
	return m_textureClasses[textureIndex];
}

void WorldHeightMap::getTerrainColorAt(Real x, Real y, RGBColor *pColor)
{
	Int xIndex = REAL_TO_INT_FLOOR(x/MAP_XY_FACTOR);
	Int yIndex = REAL_TO_INT_FLOOR(y/MAP_XY_FACTOR);
	xIndex += m_borderSize;
	yIndex += m_borderSize;
	pColor->red = pColor->green = pColor->blue = 0;
	if (xIndex<0) xIndex = 0;
	if (yIndex<0) yIndex = 0;
	if (xIndex >= m_width) xIndex = m_width-1;
	if (yIndex >= m_height) yIndex = m_height-1;
	Int ndx = (yIndex*m_width)+xIndex;
	if (ndx<0 || ndx >= this->m_dataSize) return;
	Int tileNdx = m_tileNdxes[ndx];
	tileNdx = tileNdx>>2;	 // We pack 4 grids into a tile.

	TileData *pTile = getSourceTile(tileNdx);
	if (pTile) {
		// pTile contains the bitmap data for 4 squares.
		// Get the data mipped down to one pixel for the tile.
		UnsignedByte *pData = pTile->getRGBDataForWidth(1);
		// Data is in microsoft bgra format.
		pColor->red = static_cast<Real>(pData[2]/255.0);
		pColor->green = static_cast<Real>(pData[1]/255.0);
		pColor->blue = static_cast<Real>(pData[0]/255.0);
	}
}	

AsciiString WorldHeightMap::getTerrainNameAt(Real x, Real y)
{
	Int xIndex = REAL_TO_INT_FLOOR(x/MAP_XY_FACTOR);
	Int yIndex = REAL_TO_INT_FLOOR(y/MAP_XY_FACTOR);
	xIndex += m_borderSize;
	yIndex += m_borderSize;
	if (xIndex<0) xIndex = 0;
	if (yIndex<0) yIndex = 0;
	if (xIndex >= m_width) xIndex = m_width-1;
	if (yIndex >= m_height) yIndex = m_height-1;
	Int ndx = (yIndex*m_width)+xIndex;
	if (ndx<0 || ndx >= this->m_dataSize) return AsciiString::TheEmptyString;
	Int tileNdx = m_tileNdxes[ndx];
	tileNdx = tileNdx>>2;	 // We pack 4 grids into a tile.

	Int i;
	for (i=0; i<this->m_numTextureClasses; i++) {
		if (tileNdx >= m_textureClasses[i].firstTile && tileNdx < m_textureClasses[i].firstTile + m_textureClasses[i].numTiles) {
			return(m_textureClasses[i].name);
		}
	}
	return AsciiString::TheEmptyString;
}	


