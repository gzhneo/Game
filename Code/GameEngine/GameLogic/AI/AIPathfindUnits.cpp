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

// Unit goals and positions in the map, moving allies away, attack and safe paths

/** 
 * Classify the cells under the bridge
 * If 'repaired' is true, bridge is repaired 
 * If 'repaired' is false, bridge has been damaged to be impassable 
 */
void Pathfinder::changeBridgeState( PathfindLayerEnum layer, Bool repaired)
{
	if (m_layers[layer].isUnused()) return;	
	if (m_layers[layer].setDestroyed(!repaired)) {
		m_zoneManager.markZonesDirty( repaired );
	}
}

void Pathfinder::getRadiusAndCenter(const Object *obj, Int &iRadius, Bool &center)
{
	enum {MAX_RADIUS = 2};
	if (!obj) 
	{
		center = true;
		iRadius = 0;
		return;
	}
	Real diameter = 2*obj->getGeometryInfo().getBoundingCircleRadius();
	if (diameter>PATHFIND_CELL_SIZE_F && diameter<2.0f*PATHFIND_CELL_SIZE_F) {
		diameter = 2.0f*PATHFIND_CELL_SIZE_F;
	}
	iRadius = REAL_TO_INT_FLOOR(diameter/PATHFIND_CELL_SIZE_F+0.3f);
	center = false;	
	if (iRadius==0) iRadius++;
	if (iRadius&1) 
	{
		center = true;
	}
	iRadius /= 2;
	if (iRadius > MAX_RADIUS) 
	{
		iRadius = MAX_RADIUS;
		center = true;
	}
}

/** 
 * Updates the goal cell for an ai unit.
 */
void Pathfinder::updateGoal( Object *obj, const Coord3D *newGoalPos, PathfindLayerEnum layer)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.



  if (!ai->isDoingGroundMovement()) // exception:sniped choppers are on ground  
  {

    Bool isUnmannedHelicopter = ( obj->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) && obj->isDisabledByType( DISABLED_UNMANNED  ) ) ;
    if ( ! isUnmannedHelicopter )
    {
		  updateAircraftGoal(obj, newGoalPos);
		  return;
    }
	}

	PathfindLayerEnum originalLayer = obj->getDestinationLayer();

	//DEBUG_LOG(("Object Goal layer is %d\n", layer));
	Bool layerChanged = originalLayer != layer;

	Bool doGround=false;
	Bool doLayer=false;
	if (layer==LAYER_GROUND) {
		doGround = true;
	} else {
		doLayer = true;
		if (TheTerrainLogic->objectInteractsWithBridgeEnd(obj, layer)) {
			doGround = true;
		}
	}

	ICoord2D goalCell = *ai->getPathfindGoalCell();

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	if (centerInCell) {
		newCell.x = REAL_TO_INT_FLOOR(newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(newGoalPos->y/PATHFIND_CELL_SIZE_F);
	} else {
		newCell.x = REAL_TO_INT_FLOOR(0.5f+newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(0.5f+newGoalPos->y/PATHFIND_CELL_SIZE_F);
	}
	if (!layerChanged && newCell.x==goalCell.x && newCell.y == goalCell.y) {
		return;
	}
	removeGoal(obj);

	obj->setDestinationLayer(layer);
	ai->setPathfindGoalCell(newCell);
	Int i,j;
	ICoord2D cellNdx;

	Bool warn = true;
	for (i=newCell.x-radius; i<newCell.x+numCellsAbove; i++) {
		for (j=newCell.y-radius; j<newCell.y+numCellsAbove; j++) {
			PathfindCell	*cell;
			if (doLayer) {
				cell = getCell(layer, i, j);
				if (cell) {
					if (warn && cell->getGoalUnit()!=INVALID_ID && cell->getGoalUnit() != obj->getID()) {
						warn = false;
						// jba intense debug
						//DEBUG_LOG(, ("Units got stuck close to each other.  jba\n"));
					}	
					cellNdx.x = i;
					cellNdx.y = j;
					cell->setGoalUnit(obj->getID(), cellNdx);
				}
			}
			if (doGround) {
				cell = getCell(LAYER_GROUND, i, j);
				if (cell) {
					if (warn && cell->getGoalUnit()!=INVALID_ID && cell->getGoalUnit() != obj->getID()) {
						warn = false;
						// jba intense debug
						//DEBUG_LOG(, ("Units got stuck close to each other.  jba\n"));
					}
					cellNdx.x = i;
					cellNdx.y = j;
					cell->setGoalUnit(obj->getID(), cellNdx);
				}
			}
		}
	}

}

/** 
 * Updates the goal cell for an ai unit.
 */
void Pathfinder::updateAircraftGoal( Object *obj, const Coord3D *newGoalPos)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}
	removeGoal(obj);
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.
	if (ai->isDoingGroundMovement()) {
		return;  // shouldn't really happen, but just in case.
	}

	// For now, we are only doing HOVER, and WINGS.
	if (!ai->isAircraftThatAdjustsDestination()) return;

	ICoord2D goalCell = *ai->getPathfindGoalCell();

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	if (centerInCell) {
		newCell.x = REAL_TO_INT_FLOOR(newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(newGoalPos->y/PATHFIND_CELL_SIZE_F);
	} else {
		newCell.x = REAL_TO_INT_FLOOR(0.5f+newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(0.5f+newGoalPos->y/PATHFIND_CELL_SIZE_F);
	}
	if (newCell.x==goalCell.x && newCell.y == goalCell.y) {
		return;
	}

	ai->setPathfindGoalCell(newCell);
	Int i,j;
	ICoord2D cellNdx;

	for (i=newCell.x-radius; i<newCell.x+numCellsAbove; i++) {
		for (j=newCell.y-radius; j<newCell.y+numCellsAbove; j++) {
			PathfindCell	*cell;
			cell = getCell(LAYER_GROUND, i, j);
			if (cell) {
				cellNdx.x = i;
				cellNdx.y = j;
				cell->setGoalAircraft(obj->getID(), cellNdx);
			}
		}
	}

}

/** 
 * Removes the goal cell for an ai unit.
 * Used for a unit that is going to be moving several times, like following a waypoint path, 
 * or intentionally collides with other units (like a car bomb). jba
 */
void Pathfinder::removeGoal( Object *obj)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.
	ICoord2D goalCell = *ai->getPathfindGoalCell();

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	if (radius==0) {
		radius++;
	}
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	newCell.x = newCell.y = -1;
	if (newCell.x==goalCell.x && newCell.y == goalCell.y) {
		return;
	}
	ICoord2D cellNdx;
	ai->setPathfindGoalCell(newCell);
	Int i,j;
	if (goalCell.x>=0 && goalCell.y>=0) {
		for (i=goalCell.x-radius; i<goalCell.x+numCellsAbove; i++) {
			for (j=goalCell.y-radius; j<goalCell.y+numCellsAbove; j++) {
				PathfindCell	*cell = getCell(LAYER_GROUND, i, j);
				if (cell) {
					if (cell->getGoalUnit()==obj->getID()) {
						cellNdx.x = i;
						cellNdx.y = j;
						cell->setGoalUnit(INVALID_ID, cellNdx);
					}
					if (cell->getGoalAircraft()==obj->getID()) {
						cellNdx.x = i;
						cellNdx.y = j;
						cell->setGoalAircraft(INVALID_ID, cellNdx);
					}
				}
				if (obj->getDestinationLayer()!=LAYER_GROUND) {
					cell = getCell( obj->getDestinationLayer(), i, j);
					if (cell) {
						if (cell->getGoalUnit()==obj->getID()) {
							cellNdx.x = i;
							cellNdx.y = j;
							cell->setGoalUnit(INVALID_ID, cellNdx);
						}
					}
				}
			}
		}
	}
}

/** 
 * Updates the position cell for an ai unit.
 */
void Pathfinder::updatePos( Object *obj, const Coord3D *newPos)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) 
	{
		// Only consider mobile.
		return;
	}
	if (!m_isMapReady) 
		return;

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) 
		return; // only consider ai objects.

	ICoord2D curCell = *ai->getCurPathfindCell();
	if (!ai->isDoingGroundMovement()) 
	{
		if (curCell.x>=0 && curCell.y>=0) 
		{
			removePos(obj);
		}
		return;
	}

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) 
		numCellsAbove++;
	if (centerInCell) 
	{
		newCell.x = REAL_TO_INT_FLOOR(newPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(newPos->y/PATHFIND_CELL_SIZE_F);
	} 
	else 
	{
		newCell.x = REAL_TO_INT_FLOOR(0.5f+newPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(0.5f+newPos->y/PATHFIND_CELL_SIZE_F);
	}
	if (newCell.x==curCell.x && newCell.y == curCell.y) 
	{
		return;
	}

	PathfindLayerEnum layer = obj->getLayer();
	Bool doGround=false;
	Bool doLayer=false;
	if (layer==LAYER_GROUND) {
		doGround = true;	// just have to do ground
	} else {
		doLayer = true; // have to do the layer
		if (TheTerrainLogic->objectInteractsWithBridgeEnd(obj, layer)) {
			doGround = true; // In this case, have to both layer & ground, as they overlap here.
		}
	}

	ai->setCurPathfindCell(newCell);
	Int i,j;
	ICoord2D cellNdx;
	//DEBUG_LOG(("Updating unit pos at cell %d, %d\n", newCell.x, newCell.y));
	if (curCell.x>=0 && curCell.y>=0) {
		for (i=curCell.x-radius; i<curCell.x+numCellsAbove; i++) {
			for (j=curCell.y-radius; j<curCell.y+numCellsAbove; j++) {
				cellNdx.x = i;
				cellNdx.y = j;
				PathfindCell	*cell = getCell(layer, i, j);
				if (cell) {
					if (cell->getPosUnit()==obj->getID()) {
						cell->setPosUnit(INVALID_ID, cellNdx);
					}
				}
				if (layer!=LAYER_GROUND) {
					// Remove from the ground, if present.
					cell = getCell(LAYER_GROUND, i, j);
					if (cell) {
						if (cell->getPosUnit()==obj->getID()) {
							cell->setPosUnit(INVALID_ID, cellNdx);
						}
					}
				}
			}
		}
	}
	for (i=newCell.x-radius; i<newCell.x+numCellsAbove; i++) {
		for (j=newCell.y-radius; j<newCell.y+numCellsAbove; j++) {
			PathfindCell	*cell;
			cellNdx.x = i;
			cellNdx.y = j;
			if (doLayer) {
				cell = getCell(layer, i, j);
				if (cell) {
					cell->setPosUnit(obj->getID(), cellNdx);
				}
			}
			if (doGround) {
				cell = getCell(LAYER_GROUND, i, j);
				if (cell) {
					cell->setPosUnit(obj->getID(), cellNdx);
				}
			}
		}
	}
}

/** 
 * Removes the position cell flags for an ai unit.
 */
void Pathfinder::removePos( Object *obj)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}
	if (!m_isMapReady) return;
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.
	ICoord2D curCell = *ai->getCurPathfindCell();
	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	PathfindLayerEnum layer = obj->getLayer();

	ICoord2D newCell;
	newCell.x = newCell.y = -1;
	ai->setCurPathfindCell(newCell);

	Int i,j;
	ICoord2D cellNdx;
	//DEBUG_LOG(("Updating unit pos at cell %d, %d\n", newCell.x, newCell.y));
	if (curCell.x>=0 && curCell.y>=0) {
		for (i=curCell.x-radius; i<curCell.x+numCellsAbove; i++) {
			for (j=curCell.y-radius; j<curCell.y+numCellsAbove; j++) {
				cellNdx.x = i;
				cellNdx.y = j;
				PathfindCell	*cell = getCell(layer, i, j);
				if (cell) {
					if (cell->getPosUnit()==obj->getID()) {
						cell->setPosUnit(INVALID_ID, cellNdx);
					}
				}
				if (layer!=LAYER_GROUND) {
					// Remove from the ground, if present.
					cell = getCell(LAYER_GROUND, i, j);
					if (cell) {
						if (cell->getPosUnit()==obj->getID()) {
							cell->setPosUnit(INVALID_ID, cellNdx);
						}
					}
				}
			}
		}
	}
}

/** 
 * Removes a mobile unit from the pathfind grid.
 */
void Pathfinder::removeUnitFromPathfindMap(  Object *obj )
{
	removePos(obj);
	removeGoal(obj);
}

Bool Pathfinder::moveAllies(Object *obj, Path *path)
{
								 
	if (!obj->isKindOf(KINDOF_DOZER) && !obj->isKindOf(KINDOF_HARVESTER)) {
		// Harvesters & dozers want a clear path.
		if (!path->getBlockedByAlly()) return FALSE; // Only move units if it is required.
	}
	LatchRestore<Int> recursiveDepth(m_moveAlliesDepth, m_moveAlliesDepth+1);	
	if (m_moveAlliesDepth > 2) return false;

	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	PathNode *node;
	ObjectID ignoreId = INVALID_ID;
	if (obj->getAIUpdateInterface()) {
		ignoreId = obj->getAIUpdateInterface()->getIgnoredObstacleID();
	}
	for( node = path->getLastNode(); node && node != path->getFirstNode(); node = node->getPrevious() )	{
		ICoord2D curCell;
		worldToCell(node->getPosition(), &curCell);
		Int i, j;
		for (i=curCell.x-radius; i<curCell.x+numCellsAbove; i++) {
			for (j=curCell.y-radius; j<curCell.y+numCellsAbove; j++) {
				PathfindCell	*cell = getCell(node->getLayer(), i, j);
				if (cell) {
					if (cell->getPosUnit()==INVALID_ID) {
						continue;
					}
					if (cell->getPosUnit()==obj->getID()) { 
						continue;	// It's us.
					}
					if (cell->getPosUnit()==ignoreId) { 
						continue;	 // It's the one we are ignoring.
					}
					Object *otherObj = TheGameLogic->findObjectByID(cell->getPosUnit());
					if (obj->getRelationship(otherObj)!=ALLIES) {
						continue;  // Only move allies.
					}
					if (otherObj==nullptr) continue;
					if (obj->isKindOf(KINDOF_INFANTRY) && otherObj->isKindOf(KINDOF_INFANTRY)) {
						continue;  // infantry can walk through other infantry, so just let them.
					}
					if (obj->isKindOf(KINDOF_INFANTRY) && !otherObj->isKindOf(KINDOF_INFANTRY)) {
						// If this is a general clear operation, don't let infantry push vehicles.
						if (!path->getBlockedByAlly()) continue;
					}
					if( otherObj && otherObj->getAI() && !otherObj->getAI()->isMoving() ) 
					{
						if( otherObj->getAI()->isAttacking() ) 
						{
							continue; // Don't move units that are attacking. [8/14/2003]
						}

						//Kris: Patch 1.01 November 3, 2003
						//Black Lotus exploit fix -- moving while hacking.
						if( otherObj->testStatus( OBJECT_STATUS_IS_USING_ABILITY ) || otherObj->getAI()->isBusy() )
						{
							continue; // Packing or unpacking objects for example
						}
						
						//DEBUG_LOG(("Moving ally\n"));
						otherObj->getAI()->aiMoveAwayFromUnit(obj, CMD_FROM_AI);
					}
				}
			}
		}
	}
	return true;
}

/**
 * Moves an allied unit out of the path of another unit.
 * Uses A* algorithm.
 */
Path *Pathfinder::getMoveAwayFromPath(Object* obj, Object *otherObj,
											Path *pathToAvoid, Object *otherObj2, Path *pathToAvoid2)
{
	const SearchStatScope statScope( *this, SEARCH_MOVE_AWAY );
	if (m_isMapReady == false) return nullptr;	// Should always be ok.
	if (obj == nullptr) return nullptr;
#if defined _DEBUG
	Int startTimeMS = Engine::Clock::milliseconds();
#endif
	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}
	Bool otherCenter;
	Int otherRadius;
	getRadiusAndCenter(otherObj, otherRadius, otherCenter);

	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;

	m_zoneManager.setAllPassable();

	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);

	DEBUG_ASSERTCRASH(m_openList.isEmpty() && m_closedList == nullptr, ("Dangling lists."));

	// determine start cell
	ICoord2D startCellNdx;
	Coord3D startPos = *obj->getPosition();
	if (!centerInCell) {
		startPos.x += PATHFIND_CELL_SIZE_F*0.5f;
		startPos.x += PATHFIND_CELL_SIZE_F*0.5f;
	}
	worldToCell(&startPos, &startCellNdx);
	PathfindCell *parentCell = getClippedCell( obj->getLayer(), obj->getPosition() ); 
	if (parentCell == nullptr)
		return nullptr;
	if (!obj->getAIUpdateInterface()) {
		return nullptr;	// shouldn't happen, but can't move it without an ai.
	}
	const LocomotorSet& locomotorSet = obj->getAIUpdateInterface()->getLocomotorSet();

	m_isTunneling = false;
	if (validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), parentCell ) == false) {
		m_isTunneling = true; // We can't move from our current location.  So relax the constraints.
	}
	
	TCheckMovementInfo info;
	info.cell = startCellNdx;
	info.layer = obj->getLayer();
	info.centerInCell = centerInCell;
	info.radius = radius;
	info.considerTransient = false;
	info.acceptableSurfaces = locomotorSet.getValidSurfaces();
	if (!checkForMovement(obj, info) || info.enemyFixed) {
		m_isTunneling = true; // We can't move from our current location.  So relax the constraints.
	}

	if (!parentCell->allocateInfo(startCellNdx)) {
		return nullptr;
	}
	parentCell->startPathfind(nullptr);

	// initialize "open" list to contain start cell
	m_openList.initWithStartCell(parentCell);

	// "closed" list is initially empty
	m_closedList = nullptr;

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//

	Real boxHalfWidth = radius*PATHFIND_CELL_SIZE_F - (PATHFIND_CELL_SIZE_F/4.0f);
	if (centerInCell) boxHalfWidth+=PATHFIND_CELL_SIZE_F/2;
	boxHalfWidth += otherRadius*PATHFIND_CELL_SIZE_F;
	if (otherCenter) boxHalfWidth+=PATHFIND_CELL_SIZE_F/2;

	while( !m_openList.isEmpty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		m_openList.remove(parentCell);

		Region2D bounds;
		Coord3D cellCenter;
		adjustCoordToCell(parentCell->getXIndex(), parentCell->getYIndex(), centerInCell, cellCenter, parentCell->getLayer());
		bounds.lo.x = cellCenter.x-boxHalfWidth;
		bounds.lo.y = cellCenter.y-boxHalfWidth;
		bounds.hi.x = cellCenter.x+boxHalfWidth;
		bounds.hi.y = cellCenter.y+boxHalfWidth;
		PathNode *node;
		Bool overlap = false;
		if (obj) {
			if (bounds.lo.x<obj->getPosition()->x && bounds.hi.x>obj->getPosition()->x &&
				bounds.lo.y<obj->getPosition()->y && bounds.hi.y>obj->getPosition()->y)	{
					//overlap = true;
				}
		}
		for( node = pathToAvoid->getFirstNode(); node && node->getNextOptimized(); node = node->getNextOptimized() )	{
			Coord2D start, end;
			start.x = node->getPosition()->x;
			start.y = node->getPosition()->y;
			end.x = node->getNextOptimized()->getPosition()->x;
			end.y = node->getNextOptimized()->getPosition()->y;
			if (LineInRegion(&start, &end, &bounds)) {
				overlap = true;
				break;
			}
		}
		if (otherObj) {
			if (bounds.lo.x<otherObj->getPosition()->x && bounds.hi.x>otherObj->getPosition()->x &&
				bounds.lo.y<otherObj->getPosition()->y && bounds.hi.y>otherObj->getPosition()->y)	{
					//overlap = true;
				}
		}
		if (!overlap && pathToAvoid2) {
			for( node = pathToAvoid2->getFirstNode(); node && node->getNextOptimized(); node = node->getNextOptimized() )	{
				Coord2D start, end;
				start.x = node->getPosition()->x;
				start.y = node->getPosition()->y;
				end.x = node->getNextOptimized()->getPosition()->x;
				end.y = node->getNextOptimized()->getPosition()->y;
				if (LineInRegion(&start, &end, &bounds)) {
					overlap = true;
					break;
				}
			}
		}
		if (!overlap) {
			if (startCellNdx.x == parentCell->getXIndex() && startCellNdx.y == parentCell->getYIndex()) {
				// we didn't move. Always move at least 1 cell. jba.
				overlap = true;
			}
		}
		///@todo - Adjust cost intersecting path - closer to front is more expensive. jba.
		if (!overlap && checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(), 
				parentCell->getLayer(), radius, centerInCell)) {
			// success - found a path to the goal
			if (false && TheGlobalData->m_debugAI)
				debugShowSearch(true);
			m_isTunneling = false;
			// construct and return path
			Path *newPath = buildActualPath( obj, locomotorSet.getValidSurfaces(), obj->getPosition(), parentCell, centerInCell, false);
			parentCell->releaseInfo();
			cleanOpenAndClosedLists();
			return newPath;
		}	
		// put parent cell onto closed list - its evaluation is finished
		m_closedList = parentCell->putOnClosedList( m_closedList );

		// Check to see if we can change layers in this cell.
		checkChangeLayers(parentCell);

		examineNeighboringCells(parentCell, nullptr, locomotorSet, isHuman, centerInCell, radius, startCellNdx, obj, NO_ATTACK);

	}

#if defined _DEBUG
	debugShowSearch(true);
	DEBUG_LOG(("%d ", TheGameLogic->getFrame()));
	DEBUG_LOG(("getMoveAwayFromPath pathfind failed  -- "));
	DEBUG_LOG(("Unit '%s', time %f\n", obj->getTemplate()->getName().str(), (Engine::Clock::milliseconds()-startTimeMS)/1000.0f));
#endif
	m_isTunneling = false;
	cleanOpenAndClosedLists();
	return nullptr;
}

/** Patch to the exiting path from the current position, either because we became blocked, 
  or because we had to move off the path to avoid other units. */
Path *Pathfinder::patchPath( const Object *obj, const LocomotorSet& locomotorSet, 
		Path *originalPath, Bool blocked )
{
	const SearchStatScope statScope( *this, SEARCH_PATCH );
	//CRCDEBUG_LOG(("Pathfinder::patchPath()\n"));
#if defined _DEBUG
	Int startTimeMS = Engine::Clock::milliseconds();
#endif
	if (originalPath==nullptr) return nullptr;
	if (obj==nullptr) return nullptr;
	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);
	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}

	m_zoneManager.setAllPassable();

	DEBUG_ASSERTCRASH(m_openList.isEmpty() && m_closedList == nullptr, ("Dangling lists."));

	enum {CELL_LIMIT = 2000}; // max cells to examine.
	Int cellCount = 0;
		 
	Coord3D currentPosition = *obj->getPosition();

	// determine start cell
	ICoord2D startCellNdx;
	Coord3D startPos = *obj->getPosition();
	if (!centerInCell) {
		startPos.x += PATHFIND_CELL_SIZE_F*0.5f;
		startPos.x += PATHFIND_CELL_SIZE_F*0.5f;
	}
	worldToCell(&startPos, &startCellNdx);
	//worldToCell(obj->getPosition(), &startCellNdx);
	PathfindCell *parentCell = getClippedCell( obj->getLayer(), &currentPosition); 
	if (parentCell == nullptr)
		return nullptr;
	if (!obj->getAIUpdateInterface()) {
		return nullptr;	// shouldn't happen, but can't move it without an ai.
	}

	m_isTunneling = false;
	
	if (!parentCell->allocateInfo(startCellNdx)) {
		return nullptr;
	}
	parentCell->startPathfind( nullptr);

	// initialize "open" list to contain start cell
	m_openList.initWithStartCell(parentCell);

	// "closed" list is initially empty
	m_closedList = nullptr;

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//

#if defined _DEBUG
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
	if (TheGlobalData->m_debugAI) 
	{
		RGBColor color;
		color.setFromInt(0);
		addIcon(nullptr, 0,0,color);
	}
#endif

	PathNode *startNode;
	Coord3D goalPos = *originalPath->getLastNode()->getPosition();
	Real goalDeltaSqr = sqr(goalPos.x-currentPosition.x) + sqr(goalPos.y - currentPosition.y);
	for( startNode = originalPath->getLastNode(); startNode != originalPath->getFirstNode(); startNode = startNode->getPrevious() )	{
		ICoord2D cellCoord;
		worldToCell(startNode->getPosition(), &cellCoord);
		TCheckMovementInfo info;
		info.cell = cellCoord;
		info.layer = startNode->getLayer();
		info.centerInCell = centerInCell;
		info.radius = radius;
		info.considerTransient = blocked;
		info.acceptableSurfaces = locomotorSet.getValidSurfaces();
#if defined _DEBUG
		if (TheGlobalData->m_debugAI) {
			RGBColor color;
			color.setFromInt(0);
			color.green = 1;
			addIcon(startNode->getPosition(), PATHFIND_CELL_SIZE_F*0.5f, 100, color);
		}
#endif
		Int dx = cellCoord.x-startCellNdx.x;
		Int dy = cellCoord.y-startCellNdx.y;
		if (dx<-2 || dx>2) info.considerTransient = false;
		if (dy<-2 || dy>2) info.considerTransient = false;
		if (!checkForMovement(obj, info)) {
			break;
		}
		if (info.allyFixedCount || info.enemyFixed) {
			break;	// Don't patch through cells that are occupied.
		}	
		Real curSqr = sqr(startNode->getPosition()->x-currentPosition.x) + sqr(startNode->getPosition()->y - currentPosition.y);
		if (curSqr < goalDeltaSqr) {
			goalPos = *startNode->getPosition();
			goalDeltaSqr = curSqr;
		}

	}	
	if (startNode == originalPath->getLastNode()) {
		cleanOpenAndClosedLists();
		return nullptr; // no open nodes.
	}
	PathfindCell *candidateGoal;
	candidateGoal = getCell(LAYER_GROUND, &goalPos); // just using for cost estimates.
	ICoord2D goalCellNdx;
	worldToCell(&goalPos, &goalCellNdx);
	if (!candidateGoal->allocateInfo(goalCellNdx)) {
		return nullptr;
	}

	while( !m_openList.isEmpty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		m_openList.remove(parentCell);

		Coord3D cellCenter;
		adjustCoordToCell(parentCell->getXIndex(), parentCell->getYIndex(), centerInCell, cellCenter, parentCell->getLayer());
		PathNode *matchNode;
		Bool found = false;
		for( matchNode = originalPath->getLastNode(); matchNode != startNode; matchNode = matchNode->getPrevious() )	{
			if (cellCenter.x == matchNode->getPosition()->x && cellCenter.y == matchNode->getPosition()->y)	{
				found = true;
				break;
			}
		}
		if (found ) {
			// success - found a path to the goal
			if ( TheGlobalData->m_debugAI)
				debugShowSearch(true);
			m_isTunneling = false;
			// construct and return path
			Path *path = newInstance(Path);
			PathNode *node;
			for( node = originalPath->getLastNode(); node != matchNode; node = node->getPrevious() )	{	
				path->prependNode(node->getPosition(), node->getLayer());
			}
			prependCells(path, obj->getPosition(), parentCell, centerInCell);

			// cleanup the path by checking line of sight
			path->optimize(obj, locomotorSet.getValidSurfaces(), blocked);
			parentCell->releaseInfo();
			cleanOpenAndClosedLists();
			candidateGoal->releaseInfo();

			return path;
		}	
		// put parent cell onto closed list - its evaluation is finished
		m_closedList = parentCell->putOnClosedList( m_closedList );

		if (cellCount < CELL_LIMIT) {
			// Check to see if we can change layers in this cell.
			checkChangeLayers(parentCell); 
			cellCount += examineNeighboringCells(parentCell, nullptr, locomotorSet, isHuman, centerInCell, radius, startCellNdx, obj, NO_ATTACK);
		}
	}

#if defined _DEBUG
	DEBUG_LOG(("%d ", TheGameLogic->getFrame()));
	DEBUG_LOG(("patchPath Pathfind failed  -- "));
	DEBUG_LOG(("Unit '%s', time %f\n", obj->getTemplate()->getName().str(), (Engine::Clock::milliseconds()-startTimeMS)/1000.0f));
	if (TheGlobalData->m_debugAI) {
		debugShowSearch(true);
	}
#endif
	m_isTunneling = false;
	if (!candidateGoal->getOpen() && !candidateGoal->getClosed())
	{
		// Not on one of the lists 
		candidateGoal->releaseInfo();
	}
	cleanOpenAndClosedLists();
	return nullptr;
}

/** Find a short, valid path to a location that obj can attack victim from.  */
Path *Pathfinder::findAttackPath( const Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		const Object *victim, const Coord3D* victimPos, const Weapon *weapon ) 
{
	const SearchStatScope statScope( *this, SEARCH_ATTACK );
	/*
	CRCDEBUG_LOG(("Pathfinder::findAttackPath() for object %d (%s)\n", obj->getID(), obj->getTemplate()->getName().str()));
	XferCRC xferCRC;
	xferCRC.open("lightCRC");
	xferCRC.xferSnapshot((Object *)obj);
	xferCRC.close();
	CRCDEBUG_LOG(("obj CRC is %X\n", xferCRC.getCRC()));
	if (from)
	{
		CRCDEBUG_LOG(("from: (%g,%g,%g) (%X,%X,%X)\n",
			from->x, from->y, from->z,
			AS_INT(from->x), AS_INT(from->y), AS_INT(from->z)));
	}
	if (victim)
	{
		CRCDEBUG_LOG(("victim is %d (%s)\n", victim->getID(), victim->getTemplate()->getName().str()));
		XferCRC xferCRC;
		xferCRC.open("lightCRC");
		xferCRC.xferSnapshot((Object *)victim);
		xferCRC.close();
		CRCDEBUG_LOG(("victim CRC is %X\n", xferCRC.getCRC()));
	}
	if (victimPos)
	{
		CRCDEBUG_LOG(("from: (%g,%g,%g) (%X,%X,%X)\n",
			victimPos->x, victimPos->y, victimPos->z,
			AS_INT(victimPos->x), AS_INT(victimPos->y), AS_INT(victimPos->z)));
	}
	*/
	if (m_isMapReady == false) return nullptr;	// Should always be ok.
	if (obj == nullptr) return nullptr;
#if defined _DEBUG
//	Int startTimeMS = Engine::Clock::milliseconds();
#endif

	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;
	Int radius;
	Bool centerInCell;
	getRadiusAndCenter(obj, radius, centerInCell);

	// Quick check:  See if moving couple of cells towards the victim will work.
	{
		Coord3D curPos = *obj->getPosition();
		Coord3D goalPos = victim?*victim->getPosition():*victimPos;
		Coord3D delta;
		delta.set(goalPos.x-curPos.x, goalPos.y-curPos.y, 0);
		delta.normalize();
		delta.x *= PATHFIND_CELL_SIZE_F;
		delta.y *= PATHFIND_CELL_SIZE_F;
		Int i;
		for (i=1; i<10; i++) {
			Coord3D testPos = curPos;
			testPos.x += delta.x*i*0.5f;
			testPos.y += delta.y*i*0.5f;

			ICoord2D cellNdx;
			worldToCell(&testPos, &cellNdx);
			PathfindCell *aCell = getCell(obj->getLayer(), cellNdx.x, cellNdx.y);
			if (aCell==nullptr) break;
			if (!validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), aCell )) {
				break;
			}
			if (!checkDestination(obj, cellNdx.x, cellNdx.y, obj->getLayer(), radius, centerInCell)) {
				break;
			}
			if (weapon->isGoalPosWithinAttackRange(obj, &testPos, victim, victimPos)) {
				if (!isAttackViewBlockedByObstacle(obj, testPos, victim, *victimPos)) {
					// return path.
					Path *path = newInstance(Path);
					path->prependNode( &testPos, obj->getLayer() );
					path->prependNode( &curPos, obj->getLayer() );
					path->getFirstNode()->setNextOptimized(path->getFirstNode()->getNext());
					if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS) {
						setDebugPath(path);
					}
					return path;
				}
			}
		}
	}



	const Int ATTACK_CELL_LIMIT = 2500; // this is a rather expensive operation, so limit the search.

	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}
	m_zoneManager.clearPassableFlags();
	Path *hPat = findClosestHierarchicalPath(isHuman, locomotorSet, from, victimPos, isCrusher);
	if (hPat) {
		deleteInstanceOf(hPat);
	}	else {
		m_zoneManager.setAllPassable();
	}

	Int cellCount = 0;

	DEBUG_ASSERTCRASH(m_openList.isEmpty() && m_closedList == nullptr, ("Dangling lists."));

	Int attackDistance = static_cast<Int>(weapon->getAttackDistance(obj, victim, victimPos));
	attackDistance += 3*PATHFIND_CELL_SIZE;

		// determine start cell
	ICoord2D startCellNdx;
	Coord3D objPos = *obj->getPosition();
	// since worldtocell truncates, add.
	if (centerInCell) {
		objPos.x += PATHFIND_CELL_SIZE_F/2.0f;
		objPos.y += PATHFIND_CELL_SIZE_F/2.0f;
	}	
	worldToCell(&objPos, &startCellNdx);
	PathfindCell *parentCell = getClippedCell( obj->getLayer(), &objPos ); 
	if (parentCell == nullptr)
		return nullptr;
	if (!obj->getAIUpdateInterface()) {
		return nullptr;	// shouldn't happen, but can't move it without an ai.
	}
	const PathfindCell *startCell = parentCell;

	if (!parentCell->allocateInfo(startCellNdx)) {
		return nullptr;
	}
	parentCell->startPathfind(nullptr);

	// determine start cell
	ICoord2D victimCellNdx;
	worldToCell(victim ? victim->getPosition() : victimPos, &victimCellNdx);

	// determine goal cell
	PathfindCell *goalCell = getCell( LAYER_GROUND, victimCellNdx.x, victimCellNdx.y );
	if (goalCell == nullptr)
		return nullptr;

 	if (!goalCell->allocateInfo(victimCellNdx)) {
		return nullptr;
	}

	// initialize "open" list to contain start cell
	m_openList.initWithStartCell(parentCell);

	// "closed" list is initially empty
	m_closedList = nullptr;

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//

	PathfindCell *closestCell = nullptr;
	Real closestDistanceSqr = FLT_MAX;
	Bool checkLOS = false;
	if (!victim) {
		checkLOS = true;
	}
	if (victim && !victim->isSignificantlyAboveTerrain()) {
		checkLOS = true;
	}
	
	while( !m_openList.isEmpty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		m_openList.remove(parentCell);

		Coord3D cellCenter;
		adjustCoordToCell(parentCell->getXIndex(), parentCell->getYIndex(), centerInCell, cellCenter, parentCell->getLayer());

		///@todo - Adjust cost intersecting path - closer to front is more expensive. jba.
		if (weapon->isGoalPosWithinAttackRange(obj, &cellCenter, victim, victimPos) && 
			checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(), 
				parentCell->getLayer(), radius, centerInCell)) {
			// check line of sight.
			Bool viewBlocked = false;
			if (checkLOS) 
			{
				viewBlocked = isAttackViewBlockedByObstacle(obj, cellCenter, victim, *victimPos);
			}
			if (startCell == parentCell) {
				// We never want to accept our starting cell.
				// If we could attack from there, we wouldn't be calling
				// FindAttackPath.  Usually happens cause the cell is valid for attack, but
				// a point near the cell center isn't, and that happens to be where the 
				// attacker is standing, and it's too close to move to.
				viewBlocked = true;
			} else {
				// If through some unfortunate rounding, we end up moving near ourselves, 
				// don't want it.
				Coord3D cellPos;
				adjustCoordToCell(parentCell->getXIndex(), parentCell->getYIndex(), centerInCell, cellPos, parentCell->getLayer());
				Real dx = (cellPos.x - objPos.x);
				Real dy = (cellPos.y - objPos.y);
				if (sqr(dx) + sqr(dy) < sqr(PATHFIND_CELL_SIZE_F*0.5f)) {
					viewBlocked = true;
				}
			}
			if (!viewBlocked) 
			{ 
				// success - found a path to the goal
				Bool show = TheGlobalData->m_debugAI;
	#ifdef INTENSE_DEBUG
				Int count = 0;
				PathfindCell *cur;
				for (cur = m_closedList; cur; cur=cur->getNextOpen()) {
					count++;
				}
				if (count>1000) {
					show = true;
					DEBUG_LOG(("FAP cells %d obj %s %x\n", count, obj->getTemplate()->getName().str(), obj));
	#ifdef STATE_MACHINE_DEBUG
					if( obj->getAIUpdateInterface() )
					{
						DEBUG_LOG(("State %s\n",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
					}
	#endif
					TheScriptEngine->AppendDebugMessage("Big Attack path", false);
				}
	#endif
				if (show)
					debugShowSearch(true);
	#if defined _DEBUG
				//DEBUG_LOG(("Attack path took %d cells, %f sec\n", cellCount, (Engine::Clock::milliseconds()-startTimeMS)/1000.0f));
	#endif
				// put parent cell onto closed list - its evaluation is finished
				m_closedList = parentCell->putOnClosedList( m_closedList );
				// construct and return path
				if (obj->isKindOf(KINDOF_VEHICLE)) {
					// Strip backwards.
					PathfindCell *lastBlocked = nullptr;
					PathfindCell *cur = parentCell;
					Bool useLargeRadius = false;
					Int cellLimit = 12; // Magic number, yes I know - jba.   It is about 4 * size of an average vehicle width (3 cells) [8/15/2003]
					while (cur) {
						cellLimit--;
						if (cellLimit<0) {
							break;
						}
						TCheckMovementInfo info;
						info.cell.x = cur->getXIndex();
						info.cell.y = cur->getYIndex();
						info.layer = cur->getLayer();
						if (useLargeRadius) {
							info.centerInCell = centerInCell;
							info.radius = radius;
						} else {
							info.centerInCell = true;
							info.radius = 0;
						}
						info.considerTransient = false;
						info.acceptableSurfaces = locomotorSet.getValidSurfaces();
						PathfindCell	*cell = getCell(info.layer,info.cell.x,info.cell.y);
						Bool unitIdle = false;
						if (cell) {
							ObjectID posUnit = cell->getPosUnit();
							Object *unit = TheGameLogic->findObjectByID(posUnit);
							if (unit && unit->getAI() && unit->getAI()->isIdle()) {
								unitIdle = true;
							}
						}
						Bool checkMovement = checkForMovement(obj, info);
						Bool blockedByEnemy = info.enemyFixed;
						Bool blockedByAllies = info.allyFixedCount || info.allyGoal;
						if (unitIdle) {
							// If the unit present is idle, it doesn't block allies. [8/18/2003]
							blockedByAllies = false;
						}
						

						if (!checkMovement || blockedByEnemy || blockedByAllies) {							
							lastBlocked = cur;
							useLargeRadius = true;
						} else {
							useLargeRadius = false;
						}
						cur = cur->getParentCell();
					}
					if (lastBlocked) {
						parentCell = lastBlocked;
						if (lastBlocked->getParentCell()) {
							parentCell = lastBlocked->getParentCell();
						} 
					}
				}
				Path *path = buildActualPath( obj, locomotorSet.getValidSurfaces(), obj->getPosition(), parentCell, centerInCell, false);
				if (goalCell->hasInfo() && !goalCell->getClosed() && !goalCell->getOpen()) {
					goalCell->releaseInfo();
				}
				cleanOpenAndClosedLists();
				return path;
			}
		}	
		if (checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(), 
																parentCell->getLayer(), radius, centerInCell)) {
			if (validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), parentCell )) {
				Real dx = static_cast<Real>(IABS(victimCellNdx.x-parentCell->getXIndex()));
				Real dy = static_cast<Real>(IABS(victimCellNdx.y-parentCell->getYIndex()));
				Real distSqr = dx*dx+dy*dy;
				if (distSqr < closestDistanceSqr) {
					closestCell = parentCell;
					closestDistanceSqr = distSqr;
				}
			}
		}

		// put parent cell onto closed list - its evaluation is finished
		m_closedList = parentCell->putOnClosedList( m_closedList );

		if (cellCount < ATTACK_CELL_LIMIT) {
				// Check to see if we can change layers in this cell.
			checkChangeLayers(parentCell);
			cellCount += examineNeighboringCells(parentCell, goalCell, locomotorSet, isHuman, centerInCell, 
				radius, startCellNdx, obj, attackDistance);
		}

	}

#ifdef INTENSE_DEBUG
	DEBUG_LOG(("obj %s %x\n", obj->getTemplate()->getName().str(), obj));
#ifdef STATE_MACHINE_DEBUG
	if( obj->getAIUpdateInterface() )
	{
		DEBUG_LOG(("State %s\n",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
	}
#endif
	debugShowSearch(true);
	TheScriptEngine->AppendDebugMessage("Overflowed attack path", false);
#endif
#ifdef DUMP_PERF_STATS
	TheGameLogic->incrementOverallFailedPathfinds();
#endif
	m_isTunneling = false;
	if (goalCell->hasInfo() && !goalCell->getClosed() && !goalCell->getOpen()) {
		goalCell->releaseInfo();
	}
	cleanOpenAndClosedLists();
	return nullptr;
}

/** Find a short, valid path to a location that is safe from the repulsors.  */
Path *Pathfinder::findSafePath( const Object *obj, const LocomotorSet& locomotorSet, 
		const Coord3D *from, const Coord3D* repulsorPos1, const Coord3D* repulsorPos2, Real repulsorRadius) 
{
	const SearchStatScope statScope( *this, SEARCH_SAFE );
	//CRCDEBUG_LOG(("Pathfinder::findSafePath()\n"));
	if (m_isMapReady == false) return nullptr;	// Should always be ok.
	if (obj == nullptr) return nullptr;
#if defined _DEBUG
//	Int startTimeMS = Engine::Clock::milliseconds();
#endif

	const Int MAX_CELLS = 2000; // this is a rather expensive operation, so limit the search.

	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);
	Real repulsorDistSqr = repulsorRadius*repulsorRadius;
	Int cellCount = 0;
	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}

	DEBUG_ASSERTCRASH(m_openList.isEmpty() && m_closedList == nullptr, ("Dangling lists."));
	// create unique "mark" values for open and closed cells for this pathfind invocation

	m_zoneManager.setAllPassable();
	// determine start cell
	ICoord2D startCellNdx;
	worldToCell(obj->getPosition(), &startCellNdx);
	PathfindCell *parentCell = getClippedCell( obj->getLayer(), obj->getPosition() ); 
	if (parentCell == nullptr)
		return nullptr;
	if (!obj->getAIUpdateInterface()) {
		return nullptr;	// shouldn't happen, but can't move it without an ai.
	}
	if (!parentCell->allocateInfo(startCellNdx)) {
		return nullptr;
	}
	parentCell->startPathfind( nullptr);

	// initialize "open" list to contain start cell
	m_openList.initWithStartCell(parentCell);

	// "closed" list is initially empty
	m_closedList = nullptr;

	//
	// Continue search until "open" list is empty, or
	// until goal is found.
	//

	Real farthestDistanceSqr = 0;

	while( !m_openList.isEmpty() )
	{
		// take head cell off of open list - it has lowest estimated total path cost
		parentCell = m_openList.getHead();
		m_openList.remove(parentCell);

		Coord3D cellCenter;
		adjustCoordToCell(parentCell->getXIndex(), parentCell->getYIndex(), centerInCell, cellCenter, parentCell->getLayer());

		///@todo - Adjust cost intersecting path - closer to front is more expensive. jba.
		Real dx = cellCenter.x-repulsorPos1->x;
		Real dy = cellCenter.y-repulsorPos1->y;
		Bool ok = false;
		Real distSqr = dx*dx+dy*dy;
		dx = cellCenter.x-repulsorPos2->x;
		dy = cellCenter.y-repulsorPos2->y;
		Real distSqr2 = dx*dx+dy*dy;
		if (distSqr2<distSqr) {
			distSqr = distSqr2;
		}
		if (distSqr>repulsorDistSqr) {
			ok = true;
		}
		if (m_openList.isEmpty() && cellCount>0) {
			ok = true; // exhausted the search space, just take the last cell.
		}
		if (distSqr > farthestDistanceSqr) {
			farthestDistanceSqr = distSqr;
			if (cellCount > MAX_CELLS) {
#ifdef INTENSE_DEBUG
				DEBUG_LOG(("Took intermediate path, dist %f, goal dist %f\n", sqrt(farthestDistanceSqr), repulsorRadius));
#endif
				ok = true; // Already a big search, just take this one.
			}
		}
		if ( ok && 
			checkDestination(obj, parentCell->getXIndex(), parentCell->getYIndex(), 
				parentCell->getLayer(), radius, centerInCell)) {
			// success - found a path to the goal
			Bool show = TheGlobalData->m_debugAI;
#ifdef INTENSE_DEBUG
			Int count = 0;
			PathfindCell *cur;
			for (cur = m_closedList; cur; cur=cur->getNextOpen()) {
				count++;
			}
			if (count>2000) {
				show = true;
				DEBUG_LOG(("cells %d obj %s %x\n", count, obj->getTemplate()->getName().str(), obj));
#ifdef STATE_MACHINE_DEBUG
				if( obj->getAIUpdateInterface() )
				{
					DEBUG_LOG(("State %s\n",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
				}
#endif
				TheScriptEngine->AppendDebugMessage("Big Safe path", false);
			}
#endif
			if (show)
				debugShowSearch(true);
#if defined _DEBUG
			//DEBUG_LOG(("Attack path took %d cells, %f sec\n", cellCount, (Engine::Clock::milliseconds()-startTimeMS)/1000.0f));
#endif
			// construct and return path
			Path *path = buildActualPath( obj, locomotorSet.getValidSurfaces(), obj->getPosition(), parentCell, centerInCell, false);
			parentCell->releaseInfo();
			cleanOpenAndClosedLists();
			return path;
		}	

		// put parent cell onto closed list - its evaluation is finished
		m_closedList = parentCell->putOnClosedList( m_closedList );

		// Check to see if we can change layers in this cell.
		checkChangeLayers(parentCell);

		cellCount += examineNeighboringCells(parentCell, nullptr, locomotorSet, isHuman, centerInCell, radius, startCellNdx, obj, NO_ATTACK);

	}

#ifdef INTENSE_DEBUG
	DEBUG_LOG(("obj %s %x count %d\n", obj->getTemplate()->getName().str(), obj, cellCount));
#ifdef STATE_MACHINE_DEBUG
	if( obj->getAIUpdateInterface() )
	{
		DEBUG_LOG(("State %s\n",  obj->getAIUpdateInterface()->getCurrentStateName().str()));
	}
#endif
	TheScriptEngine->AppendDebugMessage("Overflowed Safe path", false);
#endif
#ifdef DUMP_PERF_STATS
	TheGameLogic->incrementOverallFailedPathfinds();
#endif
	m_isTunneling = false;
	cleanOpenAndClosedLists();
	return nullptr;
}
