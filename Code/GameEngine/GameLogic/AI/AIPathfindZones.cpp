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

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameLogic/AIPathfind.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/CRCDebug.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/LatchRestore.h"	 
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"							 
#include "GameClient/Line2D.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/ScriptEngine.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/PerfMetrics.h"
#include "Common/PerfTimer.h"
#include "Engine/Clock.h"
#include <algorithm>	// std::sort in PathfindOpenList::releaseAll

#include "AIPathfindInternal.h"

static UnsignedInt ZONE_UPDATE_FREQUENCY = 300;

// Zone blocks, the zone manager and the bridge and wall layers

static void __fastcall resolveBlockZones(Int srcZone, Int targetZone, zoneStorageType *zoneEquivalency, Int sizeOfZE)
{
	Int i;
	// We have two zones being combined now. Keep the lower zone.
	DEBUG_ASSERTCRASH(srcZone!=0 && targetZone!=0,  ("Bad resolve zones	."));
	if (targetZone<srcZone) {
		for (i=0; i<sizeOfZE; i++) {
			if (zoneEquivalency[i] == srcZone) {
				zoneEquivalency[i] = static_cast<zoneStorageType>(targetZone);
			}
		}
	} else {
		for (i=0; i<sizeOfZE; i++) {
			if (zoneEquivalency[i] == targetZone) {
				zoneEquivalency[i] = static_cast<zoneStorageType>(srcZone);
			}
		}
	}
}

static void __fastcall resolveZones(Int srcZone, Int targetZone, zoneStorageType *zoneEquivalency, Int sizeOfZE)
{
	Int i;
	// We have two zones being combined now. Keep the lower zone.
	DEBUG_ASSERTCRASH(srcZone!=0 && targetZone!=0,  ("Bad resolve zones	."));
	DEBUG_ASSERTCRASH(srcZone<sizeOfZE && targetZone<sizeOfZE,  ("Bad resolve zones	."));
	srcZone = zoneEquivalency[srcZone];
	targetZone = zoneEquivalency[targetZone];
	DEBUG_ASSERTCRASH(srcZone<sizeOfZE && targetZone<sizeOfZE,  ("Bad resolve zones	."));
	zoneStorageType finalZone;
	if (targetZone<srcZone) {
		finalZone = zoneEquivalency[targetZone];
	} else {
		finalZone = zoneEquivalency[srcZone];
	}
	DEBUG_ASSERTCRASH(finalZone<sizeOfZE ,  ("Bad resolve zones	."));
	for (i=0; i<sizeOfZE; i++) { 
		zoneStorageType ze = zoneEquivalency[i];
		if (ze == targetZone || ze == srcZone) {
			zoneEquivalency[i] = finalZone;
		}
	}
}

static void flattenZones(zoneStorageType *zoneArray, zoneStorageType *zoneHierarchical, Int sizeOfZones)
{
	Int i;
	for (i=0; i<sizeOfZones; i++) {
		Int zone1 = zoneArray[i];
		Int zone2 = zoneHierarchical[zone1];
		zone1 = zoneArray[zone2];
		zone2 = zoneHierarchical[zone1];
		zoneArray[i] = static_cast<zoneStorageType>(zone2);
	}
#if 1

	for (i=0; i<sizeOfZones; i++) {
		Int zone1 = zoneArray[i];
		Int zone2 = zoneHierarchical[i];
		if (zone1!=zone2) {
			resolveZones(zone1, zone2, zoneArray, sizeOfZones);
		}
	}
#endif
}

inline void applyZone(PathfindCell &targetCell, const PathfindCell &sourceCell, zoneStorageType *zoneEquivalency, Int sizeOfZE)
{
	DEBUG_ASSERTCRASH(sourceCell.getZone()!=0, ("Unset source zone."));
	Int srcZone = zoneEquivalency[sourceCell.getZone()];
	//DEBUG_ASSERTCRASH(srcZone!=0, ("Bad zone equivalency zone."));
	Int targetZone = zoneEquivalency[targetCell.getZone()];

	if (targetZone == 0) {
		targetCell.setZone(static_cast<zoneStorageType>(srcZone));
		return;
	}
	if (targetZone == srcZone) {
		return; // already match.
	}
	resolveZones(srcZone, targetZone, zoneEquivalency, sizeOfZE);

}

inline void applyBlockZone(PathfindCell &targetCell, const PathfindCell &sourceCell,
													 zoneStorageType *zoneEquivalency, Int firstZone, Int sizeOfZE)
{	
	DEBUG_ASSERTCRASH(sourceCell.getZone()>=firstZone && sourceCell.getZone()<firstZone+sizeOfZE, ("Memory overrun - FATAL ERROR."));
	Int srcZone = zoneEquivalency[sourceCell.getZone()-firstZone];
	DEBUG_ASSERTCRASH(targetCell.getZone()>=firstZone && sourceCell.getZone()<firstZone+sizeOfZE, ("Memory overrun - FATAL ERROR."));
	Int targetZone = zoneEquivalency[targetCell.getZone()-firstZone];
	if (targetZone == srcZone) {
		return; // already match.
	}
	resolveBlockZones(srcZone, targetZone, zoneEquivalency, sizeOfZE);

}

//------------------------  ZoneBlock  -------------------------------
ZoneBlock::ZoneBlock() : m_firstZone(0), 
m_numZones(0), 
m_groundCliffZones(nullptr), 
m_groundWaterZones(nullptr), 
m_groundRubbleZones(nullptr), 
m_crusherZones(nullptr), 
m_zonesAllocated(0),
m_interactsWithBridge(FALSE)
{		
	m_cellOrigin.x = 0;
	m_cellOrigin.y = 0;
	//Added By Sadullah Nader
	//Initialization(s) inserted
	m_firstZone = 0;
	m_markedPassable = TRUE;
	//
}

ZoneBlock::~ZoneBlock()  
{
	freeZones();
}

void ZoneBlock::freeZones(void) 
{
	if (m_groundCliffZones) {
		delete [] m_groundCliffZones;
		m_groundCliffZones = nullptr;
	}
	if (m_groundWaterZones) {
		delete [] m_groundWaterZones;
		m_groundWaterZones = nullptr;
	}
	if (m_groundRubbleZones) {
		delete [] m_groundRubbleZones;
		m_groundRubbleZones = nullptr;
	}
	if (m_crusherZones) {
		delete [] m_crusherZones;
		m_crusherZones = nullptr;
	}
}

static void xferZoneArray( Xfer *xfer, zoneStorageType *zones, UnsignedShort count )
{
	for( UnsignedShort i = 0; i < count; ++i )
		xfer->xferUnsignedShort( &zones[ i ] );
}

void ZoneBlock::xferZones( Xfer *xfer )
{
	xfer->xferUnsignedShort( &m_firstZone );
	xfer->xferUnsignedShort( &m_numZones );
	UnsignedShort allocated = m_groundCliffZones ? m_zonesAllocated : 0;
	xfer->xferUnsignedShort( &allocated );
	if( xfer->getXferMode() == XFER_LOAD )
	{
		freeZones();
		m_zonesAllocated = allocated;
		if( allocated )
		{
			m_groundCliffZones = MSGNEW("PathfindZoneInfo") zoneStorageType[ allocated ];
			m_groundWaterZones = MSGNEW("PathfindZoneInfo") zoneStorageType[ allocated ];
			m_groundRubbleZones = MSGNEW("PathfindZoneInfo") zoneStorageType[ allocated ];
			m_crusherZones = MSGNEW("PathfindZoneInfo") zoneStorageType[ allocated ];
		}
	}
	if( allocated )
	{
		xferZoneArray( xfer, m_groundCliffZones, allocated );
		xferZoneArray( xfer, m_groundWaterZones, allocated );
		xferZoneArray( xfer, m_groundRubbleZones, allocated );
		xferZoneArray( xfer, m_crusherZones, allocated );
	}
	xfer->xferBool( &m_interactsWithBridge );
	xfer->xferBool( &m_markedPassable );
}

/* Allocate zone equivalency arrays large enough to hold required entries.  If the arrays are already
large enough, reuse.  Then calculate terrain equivalencies. */
void ZoneBlock::blockCalculateZones(PathfindCell **map, PathfindLayer layers[], const IRegion2D &bounds) 
{
	Int i, j;
	m_cellOrigin = bounds.lo;
	UnsignedInt minZone = map[bounds.lo.x][bounds.lo.y].getZone();
	UnsignedInt maxZone = minZone;

	for( j=bounds.lo.y; j<=bounds.hi.y; j++ )	{
		for( i=bounds.lo.x; i<=bounds.hi.x; i++ )	{
			PathfindCell *cell = &map[i][j];
			zoneStorageType zone = cell->getZone();
			if (minZone>zone) minZone=zone;
			if (maxZone<zone) maxZone=zone;
		}
	}
	m_firstZone = static_cast<zoneStorageType>(minZone);
	m_numZones = static_cast<UnsignedShort>(1 + maxZone - minZone);

	allocateZones();

	if (m_numZones==1) return; // all zones are equivalent.

	// Determine water/ground equivalent zones, and ground/cliff equivalent zones.
	for (i=0; i<m_zonesAllocated; i++) {
		m_groundCliffZones[i] = static_cast<zoneStorageType>(i+m_firstZone);
		m_groundWaterZones[i] = static_cast<zoneStorageType>(i+m_firstZone);
		m_groundRubbleZones[i] = static_cast<zoneStorageType>(i+m_firstZone);
		m_crusherZones[i] = static_cast<zoneStorageType>(i+m_firstZone);
	}

	for( j=bounds.lo.y; j<=bounds.hi.y; j++ )	{
		for( i=bounds.lo.x; i<=bounds.hi.x; i++ )	{
			if (i>bounds.lo.x && map[i][j].getZone()!=map[i-1][j].getZone()) {

				if (waterGround(map[i][j], map[i-1][j])) {
					applyBlockZone(map[i][j], map[i-1][j], m_groundWaterZones, m_firstZone, m_numZones);
				}
				if (groundRubble(map[i][j], map[i-1][j])) {
					applyBlockZone(map[i][j], map[i-1][j], m_groundRubbleZones, m_firstZone, m_numZones);
				}
				if (groundCliff(map[i][j], map[i-1][j])) {
					applyBlockZone(map[i][j], map[i-1][j], m_groundCliffZones, m_firstZone, m_numZones);
				}
				if (crusherGround(map[i][j], map[i-1][j])) {
					applyBlockZone(map[i][j], map[i-1][j], m_crusherZones, m_firstZone, m_numZones);
				}
			}
			if (j>bounds.lo.y && map[i][j].getZone()!=map[i][j-1].getZone()) {
				if (waterGround(map[i][j],map[i][j-1])) {
					applyBlockZone(map[i][j], map[i][j-1], m_groundWaterZones, m_firstZone, m_numZones);
				}
				if (groundRubble(map[i][j], map[i][j-1])) {
					applyBlockZone(map[i][j], map[i][j-1], m_groundRubbleZones, m_firstZone, m_numZones);
				}
				if (groundCliff(map[i][j],map[i][j-1])) {
					applyBlockZone(map[i][j], map[i][j-1], m_groundCliffZones, m_firstZone, m_numZones);
				}
				if (crusherGround(map[i][j], map[i][j-1])) {
					applyBlockZone(map[i][j], map[i][j-1], m_crusherZones, m_firstZone, m_numZones);
				}
			}
			DEBUG_ASSERTCRASH(map[i][j].getZone() != 0, ("Cleared the zone."));
		}
	}
	
}

//
// Return the zone at this location.
//
zoneStorageType ZoneBlock::getEffectiveZone( LocomotorSurfaceTypeMask acceptableSurfaces, 
																					 Bool crusher, zoneStorageType zone) const
{
	if (zone==PathfindZoneManager::UNINITIALIZED_ZONE) {
		return zone;
	}

	if (acceptableSurfaces&LOCOMOTORSURFACE_AIR) return 1; // air is all zone 1.

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_CLIFF)) {
		// Locomotors can go on ground, water & cliff, so all is zone 1.
		return 1; 
	}
	if (m_numZones<2) {
		return m_firstZone; // if we only got 1 zone, it's all the same zone.
	}
	DEBUG_ASSERTCRASH(zone >=m_firstZone && zone < m_firstZone+m_numZones, ("Invalid range."));
	if (zone<m_firstZone || zone >= m_firstZone+m_numZones) {
		return m_firstZone;
	}
	zone -= m_firstZone;
	if (crusher) {
		zone = m_crusherZones[zone];
		DEBUG_ASSERTCRASH(zone >=m_firstZone && zone < m_firstZone+m_numZones, ("Invalid range."));
		zone -= m_firstZone;
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_CLIFF)) {
		// Locomotors can go on ground & cliff, so use the ground cliff combiner.
		zone = m_groundCliffZones[zone];
		DEBUG_ASSERTCRASH(zone >=m_firstZone && zone < m_firstZone+m_numZones, ("Invalid range."));
		return zone; 
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER)) {
		// Locomotors can go on ground & water, so use the ground water combiner.
		zone = m_groundWaterZones[zone];
		DEBUG_ASSERTCRASH(zone >=m_firstZone && zone < m_firstZone+m_numZones, ("Invalid range."));
		return zone; 
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_RUBBLE)) {
		// Locomotors can go on ground & rubble, so use the ground rubble combiner.
		zone = m_groundRubbleZones[zone];
		return zone; 
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_CLIFF) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER)) {
		// Locomotors can go on ground & cliff, so use the ground cliff combiner.
		DEBUG_CRASH(("Cliff water only locomotor sets not supported yet.")); 
	}

	return zone+m_firstZone;
}

/* Allocate zone equivalency arrays large enough to hold m_maxZone entries.  If the arrays are already
large enough, just return. */
void ZoneBlock::allocateZones(void) 
{
	if (m_zonesAllocated>m_numZones && m_groundCliffZones!=nullptr) {
		return;
	}
	freeZones();

	if (m_numZones==1) {
		return; // we don't need any zone equivalency tables.
	}
	
	if (m_zonesAllocated == 0) {
		m_zonesAllocated = 4;
	}
	while (m_zonesAllocated <= m_numZones) {
		m_zonesAllocated *= 2;
	}
	// pool[]ify
	m_groundCliffZones = MSGNEW("PathfindZoneInfo") zoneStorageType [m_zonesAllocated];
	m_groundWaterZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_groundRubbleZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_crusherZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
}

//------------------------  PathfindZoneManager  -------------------------------
PathfindZoneManager::PathfindZoneManager() : m_maxZone(0), 
m_nextFrameToCalculateZones(0), 
m_groundCliffZones(nullptr), 
m_groundWaterZones(nullptr), 
m_groundRubbleZones(nullptr), 
m_terrainZones(nullptr), 
m_crusherZones(nullptr), 
m_hierarchicalZones(nullptr), 
m_blockOfZoneBlocks(nullptr),
m_zoneBlocks(nullptr),
m_zonesAllocated(0)
{		
	m_zoneBlockExtent.x = 0;
	m_zoneBlockExtent.y = 0;
}

PathfindZoneManager::~PathfindZoneManager()  
{
	freeZones();
	freeBlocks();
}

void PathfindZoneManager::freeZones() 
{
	if (m_groundCliffZones) {
		delete [] m_groundCliffZones;
		m_groundCliffZones = nullptr;
	}
	if (m_groundWaterZones) {
		delete [] m_groundWaterZones;
		m_groundWaterZones = nullptr;
	}
	if (m_groundRubbleZones) {
		delete [] m_groundRubbleZones;
		m_groundRubbleZones = nullptr;
	}
	if (m_terrainZones) {
		delete [] m_terrainZones;
		m_terrainZones = nullptr;
	}
	if (m_crusherZones) {
		delete [] m_crusherZones;
		m_crusherZones = nullptr;
	}
	if (m_hierarchicalZones) {
		delete [] m_hierarchicalZones;
		m_hierarchicalZones = nullptr;
	}
	m_zonesAllocated = 0;
}

void PathfindZoneManager::freeBlocks() 
{
	if (m_blockOfZoneBlocks) {
		delete [] m_blockOfZoneBlocks;
		m_blockOfZoneBlocks = nullptr;
	}
	if (m_zoneBlocks) {
		delete [] m_zoneBlocks;
		m_zoneBlocks = nullptr;
	}
	m_zoneBlockExtent.x = 0;
	m_zoneBlockExtent.y = 0;
}

/* Allocate zone equivalency arrays large enough to hold m_maxZone entries.  If the arrays are already
large enough, just return. */
void PathfindZoneManager::allocateZones(void) 
{
	if (m_zonesAllocated>m_maxZone && m_groundCliffZones!=nullptr) {
		return;
	}
	freeZones();
	
	if (m_zonesAllocated == 0) {
		m_zonesAllocated = INITIAL_ZONES;
	}
	while (m_zonesAllocated <= m_maxZone) {
		m_zonesAllocated *= 2;
	}
	DEBUG_LOG(("Allocating zone tables of size %d\n", m_zonesAllocated));
	// pool[]ify
	m_groundCliffZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_groundWaterZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_groundRubbleZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_terrainZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_crusherZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
	m_hierarchicalZones = MSGNEW("PathfindZoneInfo") zoneStorageType[m_zonesAllocated];
}

/* Allocate zone blocks for hierarchical pathfinding.   */
void PathfindZoneManager::allocateBlocks(const IRegion2D &globalBounds) 
{
	freeBlocks();

	m_zoneBlockExtent.x = (globalBounds.hi.x-globalBounds.lo.x+1+ZONE_BLOCK_SIZE-1)/ZONE_BLOCK_SIZE;
	m_zoneBlockExtent.y = (globalBounds.hi.y-globalBounds.lo.y+1+ZONE_BLOCK_SIZE-1)/ZONE_BLOCK_SIZE;

	m_blockOfZoneBlocks = MSGNEW("PathfindZoneBlocks") ZoneBlock[(m_zoneBlockExtent.x)*(m_zoneBlockExtent.y)];
	m_zoneBlocks = MSGNEW("PathfindZoneBlocks") ZoneBlockP[m_zoneBlockExtent.x];
	Int i;
	for (i=0; i<m_zoneBlockExtent.x; i++) {
		m_zoneBlocks[i] = &m_blockOfZoneBlocks[i*(m_zoneBlockExtent.y)];
	}
}

void PathfindZoneManager::reset(void)  ///< Called when the map is reset.
{
	freeZones();
	freeBlocks();
} 

void PathfindZoneManager::xferZones( Xfer *xfer )
{
	xfer->xferUnsignedShort( &m_maxZone );
	xfer->xferUnsignedInt( &m_nextFrameToCalculateZones );
	UnsignedShort allocated = m_groundCliffZones ? m_zonesAllocated : 0;
	xfer->xferUnsignedShort( &allocated );
	if( xfer->getXferMode() == XFER_LOAD )
	{
		freeZones();
		m_zonesAllocated = allocated;
		if( allocated )
		{
			m_groundCliffZones = MSGNEW("PathfindZoneInfo") zoneStorageType[ allocated ];
			m_groundWaterZones = MSGNEW("PathfindZoneInfo") zoneStorageType[ allocated ];
			m_groundRubbleZones = MSGNEW("PathfindZoneInfo") zoneStorageType[ allocated ];
			m_terrainZones = MSGNEW("PathfindZoneInfo") zoneStorageType[ allocated ];
			m_crusherZones = MSGNEW("PathfindZoneInfo") zoneStorageType[ allocated ];
			m_hierarchicalZones = MSGNEW("PathfindZoneInfo") zoneStorageType[ allocated ];
		}
	}
	if( allocated )
	{
		xferZoneArray( xfer, m_groundCliffZones, allocated );
		xferZoneArray( xfer, m_groundWaterZones, allocated );
		xferZoneArray( xfer, m_groundRubbleZones, allocated );
		xferZoneArray( xfer, m_terrainZones, allocated );
		xferZoneArray( xfer, m_crusherZones, allocated );
		xferZoneArray( xfer, m_hierarchicalZones, allocated );
	}

	ICoord2D extent = m_zoneBlockExtent;
	xfer->xferICoord2D( &extent );
	if( extent.x != m_zoneBlockExtent.x || extent.y != m_zoneBlockExtent.y )
	{
		DEBUG_CRASH(( "PathfindZoneManager::xferZones - zone block extent mismatch" ));
		throw SaveCode::SC_INVALID_DATA;
	}
	for( Int i = 0; i < extent.x * extent.y; ++i )
		m_blockOfZoneBlocks[ i ].xferZones( xfer );
}

void PathfindZoneManager::markZonesDirty( Bool insert )  ///< Called when the zones need to be recalculated.
{

	if (TheGameLogic->getFrame()<2) {
		m_nextFrameToCalculateZones = 2;
		return;
	}
//  if ( insert )
//  	m_nextFrameToCalculateZones = TheGameLogic->getFrame();
//  else
    m_nextFrameToCalculateZones = MIN( m_nextFrameToCalculateZones, TheGameLogic->getFrame() + ZONE_UPDATE_FREQUENCY );
} 

void PathfindZoneManager::calculateZones( PathfindCell **map, PathfindLayer layers[], const IRegion2D &globalBounds )
{

#ifdef DEBUG_QPF
#if defined(DEBUG_LOGGING) 
	__int64 startTime64;
	static double timeToUpdate = 0.0f;
  static double averageTimeToUpdate = 0.0f;
  static Int updateSamples = 0;
	__int64 endTime64,freq64;
	QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&freq64));
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&startTime64));
#endif
#endif


	m_maxZone = 1;	// we start using zone 0 as a flag.
	const Int maxZones=24000;
	zoneStorageType zoneEquivalency[maxZones];
	Int i, j;
	for (i=0; i<maxZones; i++) {
		zoneEquivalency[i] = static_cast<zoneStorageType>(i);
	}
	for (i=0; i<=LAYER_LAST; i++) {
		layers[i].setZone(0);
	}

	Int xCount = (globalBounds.hi.x-globalBounds.lo.x+1+ZONE_BLOCK_SIZE-1)/ZONE_BLOCK_SIZE;
	Int yCount = (globalBounds.hi.y-globalBounds.lo.y+1+ZONE_BLOCK_SIZE-1)/ZONE_BLOCK_SIZE;

	Int xBlock = 0, yBlock = 0;
	for (xBlock = 0; xBlock<xCount; xBlock++) {
		for (yBlock=0; yBlock<yCount; yBlock++) {
			IRegion2D bounds;
			bounds.lo.x = globalBounds.lo.x + xBlock*ZONE_BLOCK_SIZE;
			bounds.lo.y = globalBounds.lo.y + yBlock*ZONE_BLOCK_SIZE;
			bounds.hi.x = bounds.lo.x + ZONE_BLOCK_SIZE - 1; // bounds are inclusive.
			bounds.hi.y = bounds.lo.y + ZONE_BLOCK_SIZE - 1; // bounds are inclusive.
			if (bounds.hi.x > globalBounds.hi.x) {
				bounds.hi.x = globalBounds.hi.x;
			}
			if (bounds.hi.y > globalBounds.hi.y) {
				bounds.hi.y = globalBounds.hi.y;
			}
//			if (bounds.lo.x>bounds.hi.x || bounds.lo.y>bounds.hi.y) {
//				DEBUG_CRASH(("Incorrect bounds calculation. Logic error, fix me. jba."));
//				continue;
//			}
			m_zoneBlocks[xBlock][yBlock].setInteractsWithBridge(false);
			for( j=bounds.lo.y; j<=bounds.hi.y; j++ )	{
				for( i=bounds.lo.x; i<=bounds.hi.x; i++ )	{
					PathfindCell *cell = &map[i][j];
					cell->setZone(0);

					if (i>bounds.lo.x) {
						if (map[i][j].getType() == map[i-1][j].getType()) {
							applyZone(map[i][j], map[i-1][j], zoneEquivalency, m_maxZone);
						}
					}
					if (j>bounds.lo.y) {
						if (map[i][j].getType() == map[i][j-1].getType()) {
							applyZone(map[i][j], map[i][j-1], zoneEquivalency, m_maxZone);
						}
					}
					if (cell->getZone()==0) {
						cell->setZone(m_maxZone);
						m_maxZone++;
//						if (m_maxZone>= maxZones) {
//							DEBUG_CRASH(("Ran out of pathfind zones.  SERIOUS ERROR! jba."));
//							break;
//						}
					}
					if (cell->getConnectLayer() > LAYER_GROUND) {
 						m_zoneBlocks[xBlock][yBlock].setInteractsWithBridge(true);
					}
					
				}
			}
			//DEBUG_LOG(("Collapsed zones %d\n", m_maxZone));
 		}
	}

	Int totalZones = m_maxZone;
//	if (totalZones>maxZones/2) {
//		DEBUG_LOG(("Max zones %d\n", m_maxZone));
//	}

	// Collapse the zones into a 1,2,3... sequence, removing collapsed zones.
	m_maxZone = 1;
	Int collapsedZones[maxZones];
	collapsedZones[0] = 0;

  i = 1;
  while ( i < totalZones ) 
  {
		Int zone = zoneEquivalency[ i ];
		if (zone == i) 
    {
			collapsedZones[ i ] = m_maxZone;
			++m_maxZone;
		}	
    else 
			collapsedZones[ i ] = collapsedZones[zone];

    ++i;
  }

//	for (i=1; i<totalZones; i++) {
//		Int zone = zoneEquivalency[i];
//		if (zone == i) {
//			collapsedZones[i] = m_maxZone;
//			++m_maxZone;
//		}	else {
//			collapsedZones[i] = collapsedZones[zone];
//		}
//	}
  





//#ifdef DEBUG_QPF
//#if defined(DEBUG_LOGGING)
//	QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
//	timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));
//	DEBUG_LOG(("Time to calculate first %f\n", timeToUpdate));
//#endif
//#endif






	// Now map the zones in the map back into the collapsed zones.
	j=globalBounds.lo.y;
  while( j<=globalBounds.hi.y )	
  {
    i=globalBounds.lo.x;
		while( i<=globalBounds.hi.x )	
    {
      PathfindCell &cell = map[i][j];
			cell.setZone(static_cast<zoneStorageType>(collapsedZones[cell.getZone()]));
			
      //if (cell.getZone()==0) 
      //{
			//	DEBUG_CRASH(("Zone not set cell %d, %d", i, j));
			//}
      ++i;
		}
    ++j;
	}

//	for( j=globalBounds.lo.y; j<=globalBounds.hi.y; j++ )	{
//		for( i=globalBounds.lo.x; i<=globalBounds.hi.x; i++ )	{
//			map[i][j].setZone(collapsedZones[map[i][j].getZone()]);
////			if (map[i][j].getZone()==0) {
////				DEBUG_CRASH(("Zone not set cell %d, %d", i, j));
////			}
//		}
//	}






  i = 0;
	while ( i <= LAYER_LAST ) 
  {
    PathfindLayer &r_thisLayer = layers[i];

		Int zone = collapsedZones[r_thisLayer.getZone()];
		if (zone == 0) 
    {
			zone = m_maxZone;
			m_maxZone++;
		}

    r_thisLayer.setZone( zone );
    r_thisLayer.applyZone();
		
    if (!r_thisLayer.isUnused() && !r_thisLayer.isDestroyed()) 
    {
			ICoord2D ndx;
			r_thisLayer.getStartCellIndex(&ndx);
			setBridge(ndx.x, ndx.y, true);	
			r_thisLayer.getEndCellIndex(&ndx);
			setBridge(ndx.x, ndx.y, true);	
		}

    ++i;
	}

//	for (i=0; i<=LAYER_LAST; i++) {
//		Int zone = collapsedZones[layers[i].getZone()];
//		if (zone == 0) {
//			zone = m_maxZone;
//			m_maxZone++;
//		}
//		layers[i].setZone( zone );
////		if (!layers[i].isUnused() && !layers[i].isDestroyed() && layers[i].getZone()==0) {
////			DEBUG_CRASH(("Zone not set Layer %d", i));
////		}
//		layers[i].applyZone();
//		if (!layers[i].isUnused() && !layers[i].isDestroyed()) {
//			ICoord2D ndx;
//			layers[i].getStartCellIndex(&ndx);
//			setBridge(ndx.x, ndx.y, true);	
//			layers[i].getEndCellIndex(&ndx);
//			setBridge(ndx.x, ndx.y, true);	
//		}
//	}






	allocateZones();



//	DEBUG_ASSERTCRASH(xBlock==m_zoneBlockExtent.x && yBlock==m_zoneBlockExtent.y, ("Inconsistent allocation - SERIOUS ERROR. jba"));
	for (xBlock=0; xBlock<xCount; xBlock++) 
  {
		for (yBlock=0; yBlock<yCount; yBlock++) 
    {
			IRegion2D bounds;
			bounds.lo.x = globalBounds.lo.x + xBlock*ZONE_BLOCK_SIZE;
			bounds.lo.y = globalBounds.lo.y + yBlock*ZONE_BLOCK_SIZE;
			bounds.hi.x = bounds.lo.x + ZONE_BLOCK_SIZE - 1; // bounds are inclusive.
			bounds.hi.y = bounds.lo.y + ZONE_BLOCK_SIZE - 1; // bounds are inclusive.
			
      if (bounds.hi.x > globalBounds.hi.x) 
				bounds.hi.x = globalBounds.hi.x;
			
      if (bounds.hi.y > globalBounds.hi.y) 
				bounds.hi.y = globalBounds.hi.y;

// Although a good safeguard, the logic is already proven, thus skip the check
//			if (bounds.lo.x>bounds.hi.x || bounds.lo.y>bounds.hi.y) {
//				DEBUG_CRASH(("Incorrect bounds calculation. Logic error, fix me. jba."));
//				continue;
//			}
			m_zoneBlocks[xBlock][yBlock].blockCalculateZones(map, layers, bounds);
		}
	}




//#ifdef DEBUG_QPF
//#if defined(DEBUG_LOGGING) 
//	QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
//	timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));
//	DEBUG_LOG(("Time to calculate second %f\n", timeToUpdate));
//#endif
//#endif





	// Determine water/ground equivalent zones, and ground/cliff equivalent zones.
//	for (i=0; i<m_zonesAllocated; i++) {
//		m_groundCliffZones[i] = i;
//		m_groundWaterZones[i] = i;
//		m_groundRubbleZones[i] = i;
//		m_terrainZones[i] = i;
//		m_crusherZones[i] = i;
//		m_hierarchicalZones[i] = i;
//	}
	i = 0;
  while ( i < m_zonesAllocated ) 
	{
    m_groundCliffZones[i] = m_groundWaterZones[i] = m_groundRubbleZones[i] = m_terrainZones[i] = m_crusherZones[i] = m_hierarchicalZones[i] = static_cast<zoneStorageType>(i);
    i++;
  }






//	for( j=globalBounds.lo.y; j<=globalBounds.hi.y; j++ )	{
//		for( i=globalBounds.lo.x; i<=globalBounds.hi.x; i++ )	{
//			if ( (map[i][j].getConnectLayer() > LAYER_GROUND) && 
//				(map[i][j].getType() == PathfindCell::CELL_CLEAR) ) {
//				PathfindLayer *layer = layers + map[i][j].getConnectLayer();
//				resolveZones(map[i][j].getZone(), layer->getZone(), m_hierarchicalZones, m_maxZone);
//			}
//			if (i>globalBounds.lo.x && map[i][j].getZone()!=map[i-1][j].getZone()) {
//				if (map[i][j].getType() == map[i-1][j].getType()) {
//					applyZone(map[i][j], map[i-1][j], m_hierarchicalZones, m_maxZone);
//				}
//				if (waterGround(map[i][j], map[i-1][j])) {
//					applyZone(map[i][j], map[i-1][j], m_groundWaterZones, m_maxZone);
//				}
//				if (groundRubble(map[i][j], map[i-1][j])) {
////					Int zone1 = map[i][j].getZone();
////					Int zone2 = map[i-1][j].getZone();
////					if (m_terrainZones[zone1] != m_terrainZones[zone2]) {
////						//DEBUG_LOG(("Matching terrain zone %d to %d.\n", zone1, zone2));
////					}
//					applyZone(map[i][j], map[i-1][j], m_groundRubbleZones, m_maxZone);
//				}
//				if (groundCliff(map[i][j], map[i-1][j])) {
//					applyZone(map[i][j], map[i-1][j], m_groundCliffZones, m_maxZone);
//				}
//				if (terrain(map[i][j], map[i-1][j])) {
//					applyZone(map[i][j], map[i-1][j], m_terrainZones, m_maxZone);
//				}
//				if (crusherGround(map[i][j], map[i-1][j])) {
//					applyZone(map[i][j], map[i-1][j], m_crusherZones, m_maxZone);
//				}
//			}
//			if (j>globalBounds.lo.y && map[i][j].getZone()!=map[i][j-1].getZone()) {
//				if (map[i][j].getType() == map[i][j-1].getType()) {
//					applyZone(map[i][j], map[i][j-1], m_hierarchicalZones, m_maxZone);
//				}
//				if (waterGround(map[i][j],map[i][j-1])) {
//					applyZone(map[i][j], map[i][j-1], m_groundWaterZones, m_maxZone);
//				}
//				if (groundRubble(map[i][j], map[i][j-1])) {
////					Int zone1 = map[i][j].getZone();
////					Int zone2 = map[i][j-1].getZone();
////					if (m_terrainZones[zone1] != m_terrainZones[zone2]) {
////						//DEBUG_LOG(("Matching terrain zone %d to %d.\n", zone1, zone2));
////					}
//					applyZone(map[i][j], map[i][j-1], m_groundRubbleZones, m_maxZone);
//				}
//				if (groundCliff(map[i][j],map[i][j-1])) {
//					applyZone(map[i][j], map[i][j-1], m_groundCliffZones, m_maxZone);
//				}
//				if (terrain(map[i][j], map[i][j-1])) {
//					applyZone(map[i][j], map[i][j-1], m_terrainZones, m_maxZone);
//				}
//				if (crusherGround(map[i][j], map[i][j-1])) {
//					applyZone(map[i][j], map[i][j-1], m_crusherZones, m_maxZone);
//				}
//			}
//		//	DEBUG_ASSERTCRASH(map[i][j].getZone() != 0, ("Cleared the zone."));
//		}
//	}
  UnsignedInt maxZone = m_maxZone;
	j=globalBounds.lo.y;
  while( j <= globalBounds.hi.y )	
  {
    i=globalBounds.lo.x;
		while( i <= globalBounds.hi.x )	
    {
      PathfindCell &r_thisCell = map[i][j];

			if ( (r_thisCell.getConnectLayer() > LAYER_GROUND) && 
				(r_thisCell.getType() == PathfindCell::CELL_CLEAR) ) 
      {
				PathfindLayer *layer = layers + r_thisCell.getConnectLayer();
				resolveZones(r_thisCell.getZone(), layer->getZone(), m_hierarchicalZones, maxZone);
			}

			if ( i > globalBounds.lo.x && r_thisCell.getZone() != map[i-1][j].getZone() ) 
      {
        const PathfindCell &r_leftCell = map[i-1][j];

				if (r_thisCell.getType() == r_leftCell.getType()) 
					applyZone(r_thisCell, r_leftCell, m_hierarchicalZones, maxZone);//if this is true, skip all the ones below
        else
        {
          Bool notTerrainOrCrusher = TRUE; // if this is false, skip the if-else-ladder below 

          if (terrain(r_thisCell, r_leftCell)) 
          {
					  applyZone(r_thisCell, r_leftCell, m_terrainZones, maxZone);
            notTerrainOrCrusher = FALSE;
          }

          if (crusherGround(r_thisCell, r_leftCell)) 
          {
					  applyZone(r_thisCell, r_leftCell, m_crusherZones, maxZone); 
            notTerrainOrCrusher = FALSE;
          }

          if ( notTerrainOrCrusher )
          {
            if (waterGround(r_thisCell, r_leftCell)) 
					    applyZone(r_thisCell, r_leftCell, m_groundWaterZones, maxZone);
            else if (groundRubble(r_thisCell, r_leftCell)) 
					    applyZone(r_thisCell, r_leftCell, m_groundRubbleZones, maxZone);
            else if (groundCliff(r_thisCell, r_leftCell)) 
					    applyZone(r_thisCell, r_leftCell, m_groundCliffZones, maxZone);
          }

        }

      }

			if (j>globalBounds.lo.y && r_thisCell.getZone()!=map[i][j-1].getZone()) 
      {
        const PathfindCell &r_topCell = map[i][j-1];

        if (r_thisCell.getType() == r_topCell.getType()) 
					applyZone(r_thisCell, r_topCell, m_hierarchicalZones, maxZone);
        else
        {
          Bool notTerrainOrCrusher = TRUE; // if this is false, skip the if-else-ladder below 

          if (terrain(r_thisCell, r_topCell)) 
          {
            applyZone(r_thisCell, r_topCell, m_terrainZones, maxZone);
            notTerrainOrCrusher = FALSE;
          }

          if (crusherGround(r_thisCell, r_topCell)) 
          {
					  applyZone(r_thisCell, r_topCell, m_crusherZones, maxZone);
            notTerrainOrCrusher = FALSE;
          }

          if (waterGround(r_thisCell,r_topCell)) 
					  applyZone(r_thisCell, r_topCell, m_groundWaterZones, maxZone);
          else if (groundRubble(r_thisCell, r_topCell)) 
					  applyZone(r_thisCell, r_topCell, m_groundRubbleZones, maxZone);
          else if (groundCliff(r_thisCell,r_topCell)) 
					  applyZone(r_thisCell, r_topCell, m_groundCliffZones, maxZone);

        }

      }

//			DEBUG_ASSERTCRASH(r_thisCell.getZone() != 0, ("Cleared the zone."));

      ++i;
		}

    ++j; 
	}









//	if (m_maxZone >= m_zonesAllocated) {
//		RELEASE_CRASH("Pathfind allocation error - fatal. see jba.");
//	}

//	for (i=1; i<m_maxZone; i++) {
//		// Flatten hierarchical zones.
//		Int zone = m_hierarchicalZones[i];
//		m_hierarchicalZones[i] = m_hierarchicalZones[zone];
//	}
  //FLATTEN HIERARCHICAL ZONES
  {
	  i = 1;
    Int zone;  
    while ( static_cast<UnsignedInt>(i) < maxZone ) 
    {		// Flatten hierarchical zones.
		  zone = m_hierarchicalZones[i];
		  m_hierarchicalZones[i] = m_hierarchicalZones[ zone ];
      ++i;
	  }
  }

  
  
  //THIS BLOCK IS 20% 
	flattenZones(m_groundCliffZones, m_hierarchicalZones, m_maxZone);
	flattenZones(m_groundWaterZones, m_hierarchicalZones, m_maxZone);
	flattenZones(m_groundRubbleZones, m_hierarchicalZones, m_maxZone);
	flattenZones(m_terrainZones, m_hierarchicalZones, m_maxZone);
	flattenZones(m_crusherZones, m_hierarchicalZones, m_maxZone);


#ifdef DEBUG_QPF
#if defined(DEBUG_LOGGING) 
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&endTime64));
	timeToUpdate = (static_cast<double>(endTime64-startTime64) / static_cast<double>(freq64));

//	DEBUG_LOG(("Time to calculate zones %f, cells %d\n", timeToUpdate, (globalBounds.hi.x-globalBounds.lo.x)*(globalBounds.hi.y-globalBounds.lo.y)));
  if ( updateSamples < 400 )
  {
    averageTimeToUpdate = ((averageTimeToUpdate * updateSamples) + timeToUpdate) / (updateSamples + 1.0f);
    updateSamples++;
  	DEBUG_LOG(("computing...: %f, \n", averageTimeToUpdate));
  }
  else if ( updateSamples == 400 )
  {
  	DEBUG_LOG((" =============DONE============= Average time to calculate zones: %f, \n", averageTimeToUpdate));
  	DEBUG_LOG(("                                           Percent of baseline : %f, \n", averageTimeToUpdate/0.003335f));
    updateSamples = 777;
#ifdef forceRefreshCalling
    s_stopForceCalling = TRUE;
#endif
  }

#endif
#endif
#if defined _DEBUG
	if (TheGlobalData->m_debugAI == AI_DEBUG_ZONES) 
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
		RGBColor color;
		memset(&color, 0, sizeof(Color));
		addIcon(nullptr, 0, 0, color);
		for( j=0; j<globalBounds.hi.y; j++ )	{
			for( i=0; i<globalBounds.hi.x; i++ )	{
				Int zone = map[i][j].getZone();
				//zone = m_terrainZones[zone];
				//zone = m_groundCliffZones[zone];
				zone = m_hierarchicalZones[zone];

				color.blue = (zone%3) * 0.5f;
				zone = zone/3;
				color.green = (zone%3) * 0.5f;
				zone = zone/3;
				color.red = static_cast<Real>((zone%3) * 0.5);
				Coord3D pos;
				pos.x = (static_cast<Real>(i) + 0.5f) * PATHFIND_CELL_SIZE_F;
				pos.y = (static_cast<Real>(j) + 0.5f) * PATHFIND_CELL_SIZE_F;
				pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, map[i][j].getLayer() ) + 0.5f;
				addIcon(&pos, PATHFIND_CELL_SIZE_F*0.8f, 500, color);
			}
		}
	}
#endif
	m_nextFrameToCalculateZones = 0xffffffff;
}

/**
 * Update zones where a structure has been added or removed.
 * This can be done by just updating the equivalency arrays, without rezoning the map..
 */
void PathfindZoneManager::updateZonesForModify(PathfindCell **map, PathfindLayer layers[], const IRegion2D &structureBounds, const IRegion2D &globalBounds )
{

#ifdef DEBUG_QPF
#if defined(DEBUG_LOGGING) 
	__int64 startTime64;
	double timeToUpdate=0.0f;
	__int64 endTime64,freq64;
	QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER *>(&freq64));
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&startTime64));
#endif
#endif
	IRegion2D bounds = structureBounds;
	bounds.hi.x++;
	bounds.hi.y++;
	if (bounds.hi.x > globalBounds.hi.x) {
		bounds.hi.x = globalBounds.hi.x;
	}
	if (bounds.hi.y > globalBounds.hi.y) {
		bounds.hi.y = globalBounds.hi.y;
	}

	Int xBlock, yBlock;
	for (xBlock = 0; xBlock<m_zoneBlockExtent.x; xBlock++) {
		for (yBlock=0; yBlock<m_zoneBlockExtent.y; yBlock++) {
			IRegion2D blockBounds;
			blockBounds.lo.x = globalBounds.lo.x + xBlock*ZONE_BLOCK_SIZE;
			blockBounds.lo.y = globalBounds.lo.y + yBlock*ZONE_BLOCK_SIZE;
			blockBounds.hi.x = blockBounds.lo.x + ZONE_BLOCK_SIZE - 1; // blockBounds are inclusive.
			blockBounds.hi.y = blockBounds.lo.y + ZONE_BLOCK_SIZE - 1; // blockBounds are inclusive.
			if (blockBounds.hi.x > bounds.hi.x) {
				blockBounds.hi.x = bounds.hi.x;
			}
			if (blockBounds.hi.y > bounds.hi.y) {
				blockBounds.hi.y = bounds.hi.y;
			}
			if (blockBounds.lo.x < bounds.lo.x) {
				blockBounds.lo.x = bounds.lo.x;
			}
			if (blockBounds.lo.y < bounds.lo.y) {
				blockBounds.lo.y = bounds.lo.y;
			}
			if (blockBounds.lo.x>blockBounds.hi.x || blockBounds.lo.y>blockBounds.hi.y) {
				continue;
			}
			m_zoneBlocks[xBlock][yBlock].setInteractsWithBridge(false);
			Int i, j;
			for( j=blockBounds.lo.y; j<=blockBounds.hi.y; j++ )	{
				for( i=blockBounds.lo.x; i<=blockBounds.hi.x; i++ )	{
					PathfindCell *cell = &map[i][j];
					if (cell->getZone()!=UNINITIALIZED_ZONE) continue;

					if (i>blockBounds.lo.x) {
						if (map[i][j].getType() == map[i-1][j].getType()) {
							cell->setZone(map[i-1][j].getZone());
							if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
						}
					}
					if (j>blockBounds.lo.y) {
						if (cell->getType() == map[i][j-1].getType()) {
							cell->setZone(map[i][j-1].getZone());
							if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
						}
						if (i<blockBounds.hi.x) {
							if (typesMatch(*cell, map[i+1][j-1]) &&
									typesMatch(*cell, map[i+1][j])) {
								cell->setZone(map[i+1][j-1].getZone());
								if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
							}
						}
					}
				}
			}
			for( j=blockBounds.hi.y; j>=blockBounds.lo.y; j-- )	{
				for( i=blockBounds.hi.x; i>=blockBounds.lo.x; i-- )	{
					PathfindCell *cell = &map[i][j];
					if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
					if (i<blockBounds.hi.x) {
						if (map[i][j].getType() == map[i+1][j].getType()) {
							cell->setZone(map[i+1][j].getZone());
							if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
						}
					}
					if (j<blockBounds.hi.y) {
						if (cell->getType() == map[i][j+1].getType()) {
							cell->setZone(map[i][j+1].getZone());
							if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
						}
						if (i<blockBounds.hi.x) {
							if (typesMatch(*cell, map[i+1][j+1]) &&
									typesMatch(*cell, map[i+1][j])) {
								cell->setZone(map[i+1][j+1].getZone());
								if (cell->getZone()!=UNINITIALIZED_ZONE) continue;
							}
						}
					}
				}
			}
			//DEBUG_LOG(("Collapsed zones %d\n", m_maxZone));
 		}
	}
#ifdef DEBUG_QPF
#if defined(DEBUG_LOGGING) 
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&endTime64));
	timeToUpdate = (static_cast<double>(endTime64-startTime64) / static_cast<double>(freq64));
	//DEBUG_LOG(("Time to update zones %f, cells %d\n", timeToUpdate, (globalBounds.hi.x-globalBounds.lo.x)*(globalBounds.hi.y-globalBounds.lo.y)));
#endif
#endif
#if defined _DEBUG
	if (TheGlobalData->m_debugAI==AI_DEBUG_ZONES) 
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
		RGBColor color;
		memset(&color, 0, sizeof(Color));
		addIcon(nullptr, 0, 0, color);
		Int i, j;
		for( j=0; j<globalBounds.hi.y; j++ )	{
			for( i=0; i<globalBounds.hi.x; i++ )	{
				Int zone = map[i][j].getZone();
				//zone = m_terrainZones[zone];
				zone = m_hierarchicalZones[zone];

				color.blue = (zone%3) * 0.5f;
				zone = zone/3;
				color.green = (zone%3) * 0.5f;
				zone = zone/3;
				color.red = static_cast<Real>((zone%3) * 0.5);
				Coord3D pos;
				pos.x = (static_cast<Real>(i) + 0.5f) * PATHFIND_CELL_SIZE_F;
				pos.y = (static_cast<Real>(j) + 0.5f) * PATHFIND_CELL_SIZE_F;
				pos.z = TheTerrainLogic->getLayerHeight( pos.x, pos.y, map[i][j].getLayer() ) + 0.5f;
				addIcon(&pos, PATHFIND_CELL_SIZE_F*0.8f, 200, color);
			}
		}
	}
#endif

}

//
// Set the passable flag for the block at this location.
//
void PathfindZoneManager::setPassable(Int cellX, Int cellY, Bool passable) 
{
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		DEBUG_CRASH(("Invalid block."));
		return;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		DEBUG_CRASH(("Invalid block."));
		return;
	}
	m_zoneBlocks[blockX][blockY].setPassable(passable);
}

//
// Get the passable flag for the block at this location.
//
Bool PathfindZoneManager::isPassable(Int cellX, Int cellY) const
{
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		DEBUG_CRASH(("Invalid block."));
		return false;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		DEBUG_CRASH(("Invalid block."));
		return false;
	}
	return m_zoneBlocks[blockX][blockY].isPassable();
}

//
// Get the passable flag for the block at this location.
//
Bool PathfindZoneManager::clipIsPassable(Int cellX, Int cellY) const
{
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		return false;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		return false;
	}
	return m_zoneBlocks[blockX][blockY].isPassable();
}

//
// Set the bridge flag for the block at this location.
//
void PathfindZoneManager::setBridge(Int cellX, Int cellY, Bool bridge) 
{
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		// DEBUG_CRASH(("Invalid block."));  Bridges can be off the playable grid, so don't crash. jba.
		return;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		// DEBUG_CRASH(("Invalid block."));  Bridges can be off the playable grid, so don't crash. jba.
		return;
	}
	m_zoneBlocks[blockX][blockY].setInteractsWithBridge(bridge);
}

//
// Set the bridge flag for the block at this location.
//
Bool PathfindZoneManager::interactsWithBridge(Int cellX, Int cellY) const
{
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		DEBUG_CRASH(("Invalid block."));
		return false;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		DEBUG_CRASH(("Invalid block."));
		return false;
	}
	return m_zoneBlocks[blockX][blockY].getInteractsWithBridge();
}

//
// Return the zone at this location.
//
zoneStorageType PathfindZoneManager::getBlockZone(LocomotorSurfaceTypeMask acceptableSurfaces, Bool crusher,Int cellX, Int cellY, PathfindCell **map) const
{
	PathfindCell *cell = &(map[cellX][cellY]); 
	Int blockX = cellX/ZONE_BLOCK_SIZE;
	Int blockY = cellY/ZONE_BLOCK_SIZE;

	if (blockX<0 || blockX>=m_zoneBlockExtent.x) {
		DEBUG_CRASH(("Invalid block."));
		return 0;
	}
	if (blockY<0 || blockY>=m_zoneBlockExtent.y) {
		DEBUG_CRASH(("Invalid block."));
		return 0;
	}
	zoneStorageType zone =  m_zoneBlocks[blockX][blockY].getEffectiveZone(acceptableSurfaces, crusher, cell->getZone());
	if (zone >= m_maxZone) {
		DEBUG_CRASH(("Invalid zone."));
		return UNINITIALIZED_ZONE;
	}
	return zone;
}

//
// Return the zone at this location.
//
zoneStorageType PathfindZoneManager::getEffectiveTerrainZone(zoneStorageType zone) const
{
	return m_hierarchicalZones[m_terrainZones[zone]];
}

//
// Return the zone at this location.
//
zoneStorageType PathfindZoneManager::getEffectiveZone( LocomotorSurfaceTypeMask acceptableSurfaces, 
																										Bool crusher, zoneStorageType zone) const
{
	//DEBUG_ASSERTCRASH(zone, ("Zone not set"));
	if (zone>m_maxZone) {
		DEBUG_CRASH(("Invalid zone"));
		return (0);
	}
	if (zone>m_maxZone) {
		DEBUG_CRASH(("Invalid zone"));
		return (0);
	}
	if (acceptableSurfaces&LOCOMOTORSURFACE_AIR) return 1; // air is all zone 1.

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_CLIFF)) {
		// Locomotors can go on ground, water & cliff, so all is zone 1.
		return 1; 
	}

	if (crusher) {
		zone = m_crusherZones[zone];
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_CLIFF)) {
		// Locomotors can go on ground & cliff, so use the ground cliff combiner.
		zone = m_groundCliffZones[zone];
		return zone; 
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER)) {
		// Locomotors can go on ground & water, so use the ground water combiner.
		zone = m_groundWaterZones[zone];
		return zone; 
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_GROUND) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_RUBBLE)) {
		// Locomotors can go on ground & rubble, so use the ground rubble combiner.
		zone = m_groundRubbleZones[zone];
		return zone; 
	}

	if ( (acceptableSurfaces&LOCOMOTORSURFACE_CLIFF) &&
			(acceptableSurfaces&LOCOMOTORSURFACE_WATER)) {
		// Locomotors can go on ground & cliff, so use the ground cliff combiner.
		DEBUG_CRASH(("Cliff water only locomotor sets not supported yet.")); 
	}
	zone = m_hierarchicalZones[zone];

	return zone;
}

//-------------------- PathfindLayer ----------------------------------------
PathfindLayer::PathfindLayer() : m_blockOfMapCells(nullptr), m_layerCells(nullptr), m_bridge(nullptr),
// Added By Sadullah Nader
// Initializations inserted
m_destroyed(FALSE),
m_height(0),
m_width(0),
m_xOrigin(0),
m_yOrigin(0),
m_zone(0)
//
{
	m_startCell.x = -1;
	m_startCell.y = -1;
	m_endCell.x = -1;
	m_endCell.y = -1;
}

PathfindLayer::~PathfindLayer()  
{
	reset();
}

/**
 * Returns true if the layer is avaialble for use.
 */
void PathfindLayer::reset(void) 
{
	m_bridge = nullptr;
	if (m_layerCells) {
		Int i, j;
		for (i=0; i<m_width; i++) {
			for (j=0; j<m_height; j++) {
				PathfindCell *cell = &m_layerCells[i][j];
				cell->reset();
			}
		}
		delete [] m_layerCells;
		m_layerCells = nullptr;
	}
	if (m_blockOfMapCells) {
		delete [] m_blockOfMapCells;
		m_blockOfMapCells = nullptr;
	}
	m_width = 0;
	m_height = 0;
	m_xOrigin = 0;
	m_yOrigin = 0;	 
	m_startCell.x = -1;
	m_startCell.y = -1;
	m_endCell.x = -1;
	m_endCell.y = -1;
	m_layer = LAYER_GROUND;
}

/**
 * Returns true if the layer is avaialble for use.
 */
Bool PathfindLayer::isUnused(void) 
{
	// Special case - wall layer is built from not a bridge.  jba.
	if (m_layer == LAYER_WALL && m_width>0) return false;

	if (m_bridge==nullptr) return true;
	return false;
}

/**
 * Draws debug cell info.
 */
#if defined _DEBUG
void PathfindLayer::doDebugIcons(void) {
	if (isUnused()) return;
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
	// render AI debug information
	{
		Coord3D topLeftCorner;
		RGBColor color;
		color.red = color.green = color.blue = 0;	
		Coord3D center;
		center.x = (m_xOrigin+m_width/2)*PATHFIND_CELL_SIZE_F;
		center.y = (m_yOrigin+m_height/2)*PATHFIND_CELL_SIZE_F;
		center.z = 0;
		Real bridgeHeight = TheTerrainLogic->getLayerHeight(center.x , center.y, m_layer);
		if (m_layer == LAYER_WALL) {
			bridgeHeight = TheAI->pathfinder()->getWallHeight();
		}
		static Int flash = 0;
		flash--;
		if (flash<1) flash = 20;
		if (flash < 10) return;
		Bool showCells = TheGlobalData->m_debugAI==AI_DEBUG_CELLS;
		// show the pathfind grid
		for( int j=0; j<m_height; j++ )
		{
			topLeftCorner.y = static_cast<Real>(j+m_yOrigin) * PATHFIND_CELL_SIZE_F;

			for( int i=0; i<m_width; i++ )
			{
				topLeftCorner.x = static_cast<Real>(i+m_xOrigin) * PATHFIND_CELL_SIZE_F;
				
				color.red = color.green = color.blue = 0;	
				Bool empty = false;
				Real size = 0.4f;
				const PathfindCell *cell = &m_layerCells[i][j];
				if (cell)
				{
					if (cell->getConnectLayer()==LAYER_GROUND) {
							color.green = 1;
							color.blue = 1;
							empty = false;
					}	else if (cell->getType() == PathfindCell::CELL_IMPASSABLE) {
							color.red = color.green = color.blue = 1;
							size = 0.2f;
							empty = false;
					}	else if (cell->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE) {
							color.blue = color.red = 1;
							empty = false;
					}	else if (cell->getType() == PathfindCell::CELL_CLIFF) {
							color.red = 1;
							empty = false;
					}	else {
							size = 0.2f;
					}
				}
				if (showCells) {
					empty = true;
					color.red = color.green = color.blue = 0;	
					if (empty && cell) {
						if (cell->getFlags()!=PathfindCell::NO_UNITS) {
							empty = false;
							if (cell->getFlags() == PathfindCell::UNIT_GOAL) {
								color.red = 1;
							}	else if (cell->getFlags() == PathfindCell::UNIT_PRESENT_FIXED) {
								color.green = color.blue = color.red = 1;
							}	else if (cell->getFlags() == PathfindCell::UNIT_PRESENT_MOVING) {
								color.green = 1; 
							}	else {
								color.green = color.red = 1;
							}
						}
					}
				}
				if (!empty) {
					Coord3D loc;
					loc.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F/2.0f;
					loc.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F/2.0f;
					loc.z = bridgeHeight;
					addIcon(&loc, PATHFIND_CELL_SIZE_F*size, 99, color);
				}
			}
		}

	}
}
#endif

/**
 * Sets the bridge & layer number for a layer.
 */
Bool PathfindLayer::init(Bridge *theBridge, PathfindLayerEnum layer)  
{
	if (m_bridge!=nullptr) return false;
	m_bridge = theBridge;
	m_layer = layer;
	m_destroyed = false;
	return true;
}

/**
 * Allocates the pathfind cells for the bridge layer.
 */
void PathfindLayer::allocateCells(const IRegion2D *extent)
{
	if (m_bridge == nullptr) return;
	Region2D bridgeBounds = *m_bridge->getBounds();
	Int maxX, maxY;
	m_xOrigin = REAL_TO_INT_FLOOR((bridgeBounds.lo.x-PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	m_yOrigin = REAL_TO_INT_FLOOR((bridgeBounds.lo.y-PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	m_width = 0;
	m_height = 0;
	maxX = REAL_TO_INT_CEIL((bridgeBounds.hi.x+PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	maxY = REAL_TO_INT_CEIL((bridgeBounds.hi.y+PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	// Pad with 1 extra;
	m_xOrigin--;
	m_yOrigin--;
	maxX++;
	maxY++;

	if (m_xOrigin < extent->lo.x) m_xOrigin = extent->lo.x;
	if (m_yOrigin < extent->lo.y) m_yOrigin = extent->lo.y;
	if (maxX > extent->hi.x) maxX = extent->hi.x;
	if (maxY > extent->hi.y) maxY = extent->hi.y;
	if (maxX <= m_xOrigin) return;
	if (maxY <= m_yOrigin) return;
	m_width = maxX - m_xOrigin;
	m_height = maxY - m_yOrigin;

	// Allocate cells.
	// pool[]ify
	m_blockOfMapCells = MSGNEW("PathfindMapCells") PathfindCell[m_width*m_height];
	m_layerCells = MSGNEW("PathfindMapCells") PathfindCellP[m_width];
	Int i;
	for (i=0; i<m_width; i++) {
		m_layerCells[i] = &m_blockOfMapCells[i*m_height];
	}
}

/**
 * Allocates the pathfind cells for the wall bridge layer.
 */
void PathfindLayer::allocateCellsForWallLayer(const IRegion2D *extent, ObjectID *wallPieces, Int numPieces)
{
	DEBUG_ASSERTCRASH(m_layer==LAYER_WALL, ("Wrong layer for wall."));
	if (m_layer != LAYER_WALL) return;
	// stays unset if numPieces == 0 or all wall pieces are destroyed, but is read below
	Region2D bridgeBounds = {0};

	Int i; 
	Bool first = true;
	for (i=0; i<numPieces; i++) {
		Object *obj = TheGameLogic->findObjectByID(wallPieces[i]);
		Region2D objBounds;
		if (obj==nullptr) continue;
		obj->getGeometryInfo().get2DBounds(*obj->getPosition(), obj->getOrientation(), objBounds);
		if (first) {
			bridgeBounds = objBounds;
			first = false;
		} else {
			if (bridgeBounds.lo.x>objBounds.lo.x) bridgeBounds.lo.x = objBounds.lo.x;
			if (bridgeBounds.lo.y>objBounds.lo.y) bridgeBounds.lo.y = objBounds.lo.y;
			if (bridgeBounds.hi.x<objBounds.hi.x) bridgeBounds.hi.x = objBounds.hi.x;
			if (bridgeBounds.hi.y<objBounds.hi.y) bridgeBounds.hi.y = objBounds.hi.y;
		}
	}

	Int maxX, maxY;
	m_xOrigin = REAL_TO_INT_FLOOR((bridgeBounds.lo.x-PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	m_yOrigin = REAL_TO_INT_FLOOR((bridgeBounds.lo.y-PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	m_width = 0;
	m_height = 0;
	maxX = REAL_TO_INT_CEIL((bridgeBounds.hi.x+PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	maxY = REAL_TO_INT_CEIL((bridgeBounds.hi.y+PATHFIND_CELL_SIZE/100)/PATHFIND_CELL_SIZE);
	// Pad with 1 extra;
	m_xOrigin--;
	m_yOrigin--;
	maxX++;
	maxY++;

	if (m_xOrigin < extent->lo.x) m_xOrigin = extent->lo.x;
	if (m_yOrigin < extent->lo.y) m_yOrigin = extent->lo.y;
	if (maxX > extent->hi.x) maxX = extent->hi.x;
	if (maxY > extent->hi.y) maxY = extent->hi.y;
	if (maxX <= m_xOrigin) return;
	if (maxY <= m_yOrigin) return;
	m_width = maxX - m_xOrigin;
	m_height = maxY - m_yOrigin;

	// Allocate cells.
	m_blockOfMapCells = MSGNEW("PathfindMapCells") PathfindCell[m_width*m_height];
	m_layerCells = MSGNEW("PathfindMapCells") PathfindCellP[m_width];

	for (i=0; i<m_width; i++) {
		m_layerCells[i] = &m_blockOfMapCells[i*m_height];
	}
}

/**
 * Checks to see if a broken bridge connects 2 zones.
 */
Bool PathfindLayer::connectsZones(PathfindZoneManager *zm, const LocomotorSet& locoSet,
																	Int zone1, Int zone2)
{
	if (!m_destroyed) {
		return false;
	}
	Bool found1 = false;
	Bool found2 = false;
	Int i, j;
	for (i=0; i<m_width; i++) {
		for (j=0; j<m_height; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			if (cell->getConnectLayer()==LAYER_GROUND) {
					PathfindCell *groundCell = TheAI->pathfinder()->getCell(LAYER_GROUND, i+m_xOrigin, j+m_yOrigin);
					DEBUG_ASSERTCRASH(groundCell, ("Should have cell."));
					if (groundCell) {
						zoneStorageType zone = zm->getEffectiveZone(locoSet.getValidSurfaces(),
							true, groundCell->getZone());
						zone = zm->getEffectiveTerrainZone(zone);
						if (zone == zone1) found1 = true;
						if (zone == zone2) found2 = true;
					}
			}
		}
	}
	return found1 && found2;
}

/**
 * Classifies the pathfind cells for the bridge layer.
 */
void PathfindLayer::classifyCells()
{
	m_startCell.x = -1;
	m_startCell.y = -1;
	m_endCell.x = -1;
	m_endCell.y = -1;
	Int i, j;
	for (i=0; i<m_width; i++) {
		for (j=0; j<m_height; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			cell->setConnectLayer(LAYER_INVALID);
			cell->setLayer(m_layer);
			classifyLayerMapCell(i+m_xOrigin, j+m_yOrigin, cell, m_bridge);	
		}
		BridgeInfo info;
		m_bridge->getBridgeInfo(&info);
		Coord3D bridgeDir = info.to;
		bridgeDir.x -= info.from.x;
		bridgeDir.y -= info.from.y;
		bridgeDir.z -= info.from.z;
		bridgeDir.normalize();
		bridgeDir.x *= PATHFIND_CELL_SIZE_F*0.7f;
		bridgeDir.y *= PATHFIND_CELL_SIZE_F*0.7f;

		m_startCell.x = REAL_TO_INT_FLOOR((info.from.x-bridgeDir.x) / PATHFIND_CELL_SIZE_F);
		m_startCell.y = REAL_TO_INT_FLOOR((info.from.y-bridgeDir.y) / PATHFIND_CELL_SIZE_F);
		m_endCell.x = REAL_TO_INT_FLOOR((info.to.x+bridgeDir.x) / PATHFIND_CELL_SIZE_F);
		m_endCell.y = REAL_TO_INT_FLOOR((info.to.y+bridgeDir.y) / PATHFIND_CELL_SIZE_F);
	}	
	if (m_destroyed) {
		Int i5, j3;
		for (i5=0; i5<m_width; i5++) {
			for (j3=0; j3<m_height; j3++) {
				PathfindCell *cell = &m_layerCells[i5][j3];
				if (cell->getConnectLayer() == LAYER_GROUND) {
					PathfindCell *groundCell = TheAI->pathfinder()->getCell(LAYER_GROUND, i5+m_xOrigin, j3+m_yOrigin);
					DEBUG_ASSERTCRASH(groundCell, ("Should have cell."));
					if (groundCell) {
						DEBUG_ASSERTCRASH(groundCell->getConnectLayer()==m_layer, ("Should connect to this layer.jba."));
						groundCell->setConnectLayer(LAYER_INVALID); // disconnect it.
					}
				}
				cell->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
			}
		}
	}
}

/**
 * Classifies the pathfind cells for the wall bridge layer.
 */
void PathfindLayer::classifyWallCells(ObjectID *wallPieces, Int numPieces)
{
	DEBUG_ASSERTCRASH(m_layer==LAYER_WALL, ("Wrong layer for wall."));
	if (m_layer != LAYER_WALL) return;
	if (m_layerCells == nullptr) return;

	Int i, j;
	for (i=0; i<m_width; i++) {
		for (j=0; j<m_height; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			cell->setConnectLayer(LAYER_INVALID);
			cell->setLayer(m_layer);
			classifyWallMapCell(i+m_xOrigin, j+m_yOrigin, cell, wallPieces, numPieces);	
			cell->setPinched(false);
		}
	}
	if (m_destroyed) {
		Int i4, j2;
		for (i4=0; i4<m_width; i4++) {
			for (j2=0; j2<m_height; j2++) {
				PathfindCell *cell = &m_layerCells[i4][j2];
				if (cell->getConnectLayer() == LAYER_GROUND) {
					PathfindCell *groundCell = TheAI->pathfinder()->getCell(LAYER_GROUND, i4+m_xOrigin, j2+m_yOrigin);
					DEBUG_ASSERTCRASH(groundCell, ("Should have cell."));
					if (groundCell) {
						DEBUG_ASSERTCRASH(groundCell->getConnectLayer()==m_layer, ("Should connect to this layer.jba."));
						groundCell->setConnectLayer(LAYER_INVALID); // disconnect it.
					}
				}
				cell->setType(PathfindCell::CELL_IMPASSABLE);
			}
		}
	}

	// Tighten up 1 cell.
	for (i=1; i<m_width-1; i++) {
		for (j=1; j<m_height-1; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			Int k, l;
			for (k=i-1; k<i+2; k++) {
				for (l=j-1; l<j+2; l++) {
					PathfindCell *adjacentCell = &m_layerCells[k][l];
					if (adjacentCell->getType() != PathfindCell::CELL_CLEAR) {
						cell->setPinched(true);
					}
				}
			}
		}
	}
	for (i=0; i<m_width; i++) {
		for (j=0; j<m_height; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			if (cell->getPinched() && cell->getType() == PathfindCell::CELL_CLEAR) {
				cell->setType(PathfindCell::CELL_CLIFF);
			}
			cell->setPinched(false);
		}
	}
}

/**
 * Relassifies the pathfind cells for the destroyed bridge layer.
 */
Bool PathfindLayer::setDestroyed(Bool destroyed)
{
	if (destroyed == m_destroyed) return false;
	
	m_destroyed = destroyed;
	classifyCells();

	return true;
}

/**
 * Copies m_zone into the zone for all the member cells.
 */
void PathfindLayer::applyZone( void )
{
	Int i, j;
	for (i=0; i<m_width; i++) {
		for (j=0; j<m_height; j++) {
			PathfindCell *cell = &m_layerCells[i][j];
			cell->setZone(static_cast<zoneStorageType>(m_zone));
		}
	}
}

/**
 * Return the bridge's object id.
 */
ObjectID PathfindLayer::getBridgeID(void)
{
	return m_bridge->peekBridgeInfo()->bridgeObjectID;
}

/**
 * Return the cell at the index location.
 */
PathfindCell *PathfindLayer::getCell(Int x, Int y)
{
	DEBUG_ASSERTCRASH(m_layerCells, ("no data in layer, why get cells?"));
	if (m_layerCells==nullptr) {
		return nullptr;
	}
	x -= m_xOrigin;
	y -= m_yOrigin;
	if (x<0 || x>=m_width) return nullptr;
	if (y<0 || y>=m_height) return nullptr;
	PathfindCell *cell = &m_layerCells[x][y];
	if (cell->getType() == PathfindCell::CELL_IMPASSABLE) {
		return nullptr; // Impassable cells are ignored.
	}
	return cell;
}

/**
 * Classify the given map cell as clear, or not, etc.
 */
void PathfindLayer::classifyLayerMapCell( Int i, Int j , PathfindCell *cell, Bridge *theBridge)
{
	Coord3D topLeftCorner, bottomRightCorner;

	topLeftCorner.y = static_cast<Real>(j) * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F;

	topLeftCorner.x = static_cast<Real>(i) * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F;


	Int bridgeCount = 0;
	Coord3D pt;
	if (theBridge->isPointOnBridge(&topLeftCorner) ) {
		bridgeCount++;
	}
	pt = topLeftCorner;
	pt.y = bottomRightCorner.y;
	if (theBridge->isPointOnBridge(&pt) ) {
		bridgeCount++;
	}
	if (theBridge->isPointOnBridge(&bottomRightCorner) ) {
		bridgeCount++;
	}
	pt = topLeftCorner;
	pt.x = bottomRightCorner.x;
	if (theBridge->isPointOnBridge(&pt) ) {
		bridgeCount++;
	}
	cell->reset( );
	cell->setLayer(m_layer);
	cell->setType(PathfindCell::CELL_IMPASSABLE);
	if (bridgeCount == 4) {
		cell->setType(PathfindCell::CELL_CLEAR);
	} else {
		if (bridgeCount!=0) {
			cell->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE); // it's off the bridge.
		}
		
		// check against the end lines.

		Region2D cellBounds;
		cellBounds.lo.x = topLeftCorner.x;
		cellBounds.lo.y = topLeftCorner.y;
		cellBounds.hi.x = bottomRightCorner.x;
		cellBounds.hi.y = bottomRightCorner.y;

		if (m_bridge->isCellOnSide(&cellBounds)) {
			cell->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
		} else {
			if (m_bridge->isCellOnEnd(&cellBounds)) {
				cell->setType(PathfindCell::CELL_CLEAR);
			}
			if (m_bridge->isCellEntryPoint(&cellBounds)) {
				cell->setType(PathfindCell::CELL_CLEAR);
				cell->setConnectLayer(LAYER_GROUND);
				PathfindCell *groundCell = TheAI->pathfinder()->getCell(LAYER_GROUND, i, j );
				groundCell->setConnectLayer(cell->getLayer());
			}
		}
	}
	Coord3D center = topLeftCorner;
	center.x += PATHFIND_CELL_SIZE/2;
	center.y += PATHFIND_CELL_SIZE/2;
	if (cell->getType()!=PathfindCell::CELL_IMPASSABLE) {
		if (!(cell->getConnectLayer()==LAYER_GROUND) ) {
			// Check for bridge clearance.  If the ground isn't 1 pathfind cells below, mark impassable.
			Real groundHeight = TheTerrainLogic->getLayerHeight( center.x, center.y, LAYER_GROUND );
			Real bridgeHeight = theBridge->getBridgeHeight( &center, nullptr );
			if (groundHeight+LAYER_Z_CLOSE_ENOUGH_F > bridgeHeight) {
				PathfindCell *groundCell = TheAI->pathfinder()->getCell(LAYER_GROUND,i, j);
				if (!(groundCell->getType()==PathfindCell::CELL_OBSTACLE)) {
					groundCell->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
				}
			}
		}
	}
	return;
}

Bool PathfindLayer::isPointOnWall(ObjectID *wallPieces, Int numPieces, const Coord3D *pt)
{
	Int i; 
	for (i=0; i<numPieces; i++) {
		Object *obj = TheGameLogic->findObjectByID(wallPieces[i]);
		if (obj==nullptr) continue;
		Real major = obj->getGeometryInfo().getMajorRadius();
		Real minor = (obj->getGeometryInfo().getGeomType() == GEOMETRY_SPHERE) ? obj->getGeometryInfo().getMajorRadius() : obj->getGeometryInfo().getMinorRadius();

		Real c = static_cast<Real>(Cos(-obj->getOrientation()));
		Real s = static_cast<Real>(Sin(-obj->getOrientation()));

		// convert to a delta relative to rect ctr
		Real ptx = pt->x - obj->getPosition()->x;
		Real pty = pt->y - obj->getPosition()->y;

		// inverse-rotate it to the right coord system
		Real ptx_new = static_cast<Real>(fabs(ptx*c - pty*s));
		Real pty_new = static_cast<Real>(fabs(ptx*s + pty*c));

		if (ptx_new <= major && pty_new <= minor)
		{
			return true;
		}
	}
	return false;
}

/**
 * Classify the given map cell as clear, or not, etc.
 */
void PathfindLayer::classifyWallMapCell( Int i, Int j , PathfindCell *cell, ObjectID *wallPieces, Int numPieces)
{
	Coord3D topLeftCorner, bottomRightCorner;

	topLeftCorner.y = static_cast<Real>(j) * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F;

	topLeftCorner.x = static_cast<Real>(i) * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F;


	Int bridgeCount = 0;
	Coord3D pt;
	if (isPointOnWall(wallPieces, numPieces, &topLeftCorner) ) {
		bridgeCount++;
	}
	pt = topLeftCorner;
	pt.y = bottomRightCorner.y;
	if (isPointOnWall(wallPieces, numPieces, &pt) ) {
		bridgeCount++;
	}
	if (isPointOnWall(wallPieces, numPieces, &bottomRightCorner) ) {
		bridgeCount++;
	}
	pt = topLeftCorner;
	pt.x = bottomRightCorner.x;
	if (isPointOnWall(wallPieces, numPieces, &pt) ) {
		bridgeCount++;
	}
	cell->reset( );
	cell->setLayer(m_layer);
	cell->setType(PathfindCell::CELL_IMPASSABLE);
	if (bridgeCount == 4) {
		cell->setType(PathfindCell::CELL_CLEAR);
	} else {
		if (bridgeCount!=0) {
			cell->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE); // it's off the bridge.
		}
		
	}
}
