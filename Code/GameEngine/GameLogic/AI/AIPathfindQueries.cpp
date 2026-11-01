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

// Destination checks and adjustments, sight lines and passability

Locomotor* Pathfinder::chooseBestLocomotorForPosition(PathfindLayerEnum layer, LocomotorSet* locomotorSet, const Coord3D* pos )
{
	Int x = REAL_TO_INT_FLOOR(pos->x/PATHFIND_CELL_SIZE);
	Int y = REAL_TO_INT_FLOOR(pos->y/PATHFIND_CELL_SIZE);
	PathfindCell* cell = getCell(layer, x, y );
	// off the map? call it CELL_CLEAR...
	PathfindCell::CellType celltype = cell ? cell->getType() : PathfindCell::CELL_CLEAR;

	LocomotorSurfaceTypeMask acceptableSurfaces = validLocomotorSurfacesForCellType(celltype);
	return locomotorSet->findLocomotor(acceptableSurfaces);
}

/*static*/ LocomotorSurfaceTypeMask Pathfinder::validLocomotorSurfacesForCellType(PathfindCell::CellType t)
{
	if (t == PathfindCell::CELL_OBSTACLE) {
		return LOCOMOTORSURFACE_AIR;
	}
	if (t == PathfindCell::CELL_IMPASSABLE) {
		return LOCOMOTORSURFACE_AIR;
	}
	if (t == PathfindCell::CELL_BRIDGE_IMPASSABLE) {
		return LOCOMOTORSURFACE_AIR;
	}
	if (t==PathfindCell::CELL_CLEAR) {
		return LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_AIR;
	}
	if (t == PathfindCell::CELL_WATER) {
		return LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_AIR;
	}
	if (t == PathfindCell::CELL_RUBBLE) {
		return LOCOMOTORSURFACE_RUBBLE | LOCOMOTORSURFACE_AIR;
	}
	if ( t == PathfindCell::CELL_CLIFF ) {
		return LOCOMOTORSURFACE_CLIFF | LOCOMOTORSURFACE_AIR;
	}
	return NO_SURFACES;
}

//
// Return true if we can move onto this position
//
Bool Pathfinder::validMovementTerrain( PathfindLayerEnum layer, const Locomotor* locomotor, const Coord3D *pos)
{
	Int x = REAL_TO_INT_FLOOR(pos->x/PATHFIND_CELL_SIZE);
	Int y = REAL_TO_INT_FLOOR(pos->y/PATHFIND_CELL_SIZE);

	PathfindCell *toCell = nullptr;
	toCell = getCell( layer, x, y );

	if (toCell == nullptr)
		return false;
	if (toCell->getType()==PathfindCell::CELL_OBSTACLE) return true;
	if (toCell->getType()==PathfindCell::CELL_IMPASSABLE) return true;
	// check validity of destination cell
	LocomotorSurfaceTypeMask acceptableSurfaces = validLocomotorSurfacesForCellType(toCell->getType());
	if ((locomotor->getLegalSurfaces() & acceptableSurfaces) == 0)
		return false;
	return true;
}

//
// Return true if we can move onto this position
//
Bool Pathfinder::validMovementPosition( Bool isCrusher, LocomotorSurfaceTypeMask acceptableSurfaces, 
																			 PathfindCell *toCell, PathfindCell *fromCell )
{
	if (toCell == nullptr)
		return false;

	// check if the destination cell is classified as an obstacle,
	// and we happen to be ignoring it
	if (toCell->isObstaclePresent( m_ignoreObstacleID ))
		return true;

	if (isCrusher && toCell->isObstacleFence()) {
		return true;
	}

	// check validity of destination cell
	LocomotorSurfaceTypeMask cellSurfaces = validLocomotorSurfacesForCellType(toCell->getType());
	if ((cellSurfaces & acceptableSurfaces) == 0)
		return false;


	return true;
}

/**
 * Checks to see if obj can occupy the pathfind cell at x,y.
 * Returns false if there is another unit's goal already there.
 * Assumes your locomotor already said you can go there.
 */
Bool Pathfinder::checkDestination(const Object *obj, Int cellX, Int cellY, PathfindLayerEnum layer, Int iRadius, Bool centerInCell)
{
	// If obj==NULL, means we are checking for any ground units present.  jba.
	Int numCellsAbove = iRadius;
	if (centerInCell) numCellsAbove++;
	Bool checkForAircraft = false;
	Int i, j;
	ObjectID ignoreId = INVALID_ID;
	ObjectID objID = INVALID_ID;
	if (obj && obj->getAIUpdateInterface()) {
		ignoreId =  obj->getAIUpdateInterface()->getIgnoredObstacleID();
		checkForAircraft = obj->getAI()->isAircraftThatAdjustsDestination();
		objID = obj->getID();
	}
	for (i=cellX-iRadius; i<cellX+numCellsAbove; i++) {
		for (j=cellY-iRadius; j<cellY+numCellsAbove; j++) {
			PathfindCell	*cell = getCell(layer, i, j);
			if (cell) {
				if (checkForAircraft) {
					if (!cell->isAircraftGoal()) continue;
					if (cell->getGoalAircraft()==objID) continue;
					return false;
				}
				if (cell->getType()==PathfindCell::CELL_OBSTACLE) {
					if (cell->isObstaclePresent( ignoreId ))
						continue;
					return false;
				}
				if (IS_IMPASSABLE(cell->getType())) {
					return false;
				}
				if (cell->getFlags() == PathfindCell::NO_UNITS) {
					continue;  // Nobody is here, so it's ok.
				} 
				ObjectID goalUnitID = cell->getGoalUnit();
				if (goalUnitID==objID) {
					continue; // we got it.
				} else if (ignoreId==goalUnitID) {
					continue; // we are ignoring it.
				} else if (goalUnitID!=INVALID_ID) {
					if (obj==nullptr) {
						return false;
					}
					Object *unit = TheGameLogic->findObjectByID(goalUnitID);
					if (unit) {
						// order matters: we want to know if I consider it to be an ally, not vice versa
						if (obj->getRelationship(unit) == ALLIES) {
							return false; 	// Don't usurp your allies goals.  jba.
						}	
						if (cell->getFlags()==PathfindCell::UNIT_PRESENT_FIXED) {
							Bool canCrush = obj->canCrushOrSquish(unit, CrushSquishTestType::TEST_CRUSH_OR_SQUISH);
							if (!canCrush) {
								return false; // Don't move to an occupied cell.
							}							
						}
					}
				}
			} else {
				return false; // off the map, so can't place here.
			}
		}
	}	

	return true;
}

/**
 * Checks to see if obj can move through the pathfind cell at x,y.
 * Returns false if there are other units already there.
 * Assumes your locomotor already said you can go there.
 */
Bool Pathfinder::checkForMovement(const Object *obj, TCheckMovementInfo &info)	  
{
	info.allyFixedCount = 0;
	info.allyMoving = false;
	info.allyGoal = false;
	info.enemyFixed = false;

	const Int		maxAlly = 5;
	ObjectID		allies[maxAlly];
	Int					numAlly = 0;

	if (!obj) return true; // not object can move there.
	ObjectID ignoreId = INVALID_ID;
	if (obj->getAIUpdateInterface()) {
		ignoreId =  obj->getAIUpdateInterface()->getIgnoredObstacleID();
	}

	Int numCellsAbove = info.radius;
	if (info.centerInCell) numCellsAbove++;
	Int i, j;
//	Bool isInfantry = obj->isKindOf(KINDOF_INFANTRY);
	for (i=info.cell.x-info.radius; i<info.cell.x+numCellsAbove; i++) {
		for (j=info.cell.y-info.radius; j<info.cell.y+numCellsAbove; j++) {
			PathfindCell	*cell = getCell(info.layer,i, j);
			if (cell) {
				enum PathfindCell::CellFlags flags = cell->getFlags();
				ObjectID posUnit = cell->getPosUnit();
				if ((flags == PathfindCell::UNIT_GOAL) || (flags == PathfindCell::UNIT_GOAL_OTHER_MOVING)) {
					info.allyGoal = true;
				}
				if (flags == PathfindCell::NO_UNITS) {
					continue;  // Nobody is here, so it's ok.
				} else if (posUnit==obj->getID()) {
					continue; // we got it.
				} else if (posUnit==ignoreId) {
					continue; // we are ignoring this one.
				}	else {
					Bool check = false;
					Object *unit = nullptr;
					if (flags==PathfindCell::UNIT_PRESENT_MOVING || flags==PathfindCell::UNIT_GOAL_OTHER_MOVING) {
						unit = TheGameLogic->findObjectByID(posUnit);
						// order matters: we want to know if I consider it to be an ally, not vice versa
						if (unit && obj->getRelationship(unit) == ALLIES) {
							info.allyMoving = true;
						}
						if (info.considerTransient) {
							check = true;
						}
					}
					if (flags == PathfindCell::UNIT_PRESENT_FIXED) {
						check = true;
						unit = TheGameLogic->findObjectByID(posUnit);
					}
					if (check && unit!=nullptr) {
						if (obj->getAIUpdateInterface() && obj->getAIUpdateInterface()->getIgnoredObstacleID()==unit->getID()) {
							// Don't check if it's the ignored obstacle.
							check = false;
						}
					}
					if (check && unit) {
#ifdef INFANTRY_MOVES_THROUGH_INFANTRY
						if (obj->isKindOf(KINDOF_INFANTRY) && unit->isKindOf(KINDOF_INFANTRY)) {
							// Infantry can run through infantry.
							continue; // 
						}
#endif
						// See if it is an ally.
						// order matters: we want to know if I consider it to be an ally, not vice versa
						if (obj->getRelationship(unit) == ALLIES) {
							if (!unit->getAIUpdateInterface()) {
								return false; // can't path through not-idle units.
							}
							Bool found = false;
							Int k;
							for (k=0; k<numAlly; k++) {
								if (allies[k] == unit->getID()) {
									found = true;
								}
							}
							if (!found) {
								info.allyFixedCount++;
								if (numAlly < maxAlly) {
									allies[numAlly] = unit->getID();
									numAlly++;
								}
							}
						}	else {
							Bool canCrush = obj->canCrushOrSquish( unit, CrushSquishTestType::TEST_CRUSH_OR_SQUISH );
							if (!canCrush) {
								info.enemyFixed = true;
							}
						}
					}
				}
			} else {
				return false; // off the map, so can't place here.
			}
		}
	}
	return true;
}

/**
 * Adjusts a coordinate to the center of it's cell.
 */
// Snaps the current position to it's grid location.
void Pathfinder::snapPosition(Object *obj, Coord3D *pos)
{
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *pos;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	worldToCell( &adjustDest, &cell );
	adjustCoordToCell(cell.x, cell.y,  center, *pos, LAYER_GROUND);
}

/**
 * Adjusts a goal position to the center of it's cell.
 */
// Snaps the current position to it's grid location.
void Pathfinder::snapClosestGoalPosition(Object *obj, Coord3D *pos)
{
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *pos;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(pos);
	worldToCell( &adjustDest, &cell );
	adjustCoordToCell(cell.x, cell.y,  center, *pos, LAYER_GROUND);
	if (checkDestination(obj, cell.x, cell.y , layer, iRadius, center)) {
		return;
	}

	// Try adjusting by 1.
	Int i,j;
	for (i=cell.x-1; i<cell.x+2; i++) {
		for (j=cell.y-1; j<cell.y+2; j++) {
			if (checkDestination(obj, i, j, layer, iRadius, center)) {
				adjustCoordToCell(i, j,  center, *pos, layer);
				return;
			}
		}
	}
	if (iRadius==0) {
		// Try to find an unoccupied cell.
		for (i=cell.x-1; i<cell.x+2; i++) {
			for (j=cell.y-1; j<cell.y+2; j++) {
				PathfindCell	*newCell = getCell(layer,i, j);
				if (newCell) {
					if (newCell->getGoalUnit()==INVALID_ID || newCell->getGoalUnit()==obj->getID()) {
						adjustCoordToCell(i, j,  center, *pos, layer);
						return;
					}
				}
			}
		}
		// Try to find an unoccupied cell.
		for (i=cell.x-1; i<cell.x+2; i++) {
			for (j=cell.y-1; j<cell.y+2; j++) {
				PathfindCell	*newCell = getCell(layer,i, j);
				if (newCell) {
					if (newCell->getFlags()!=PathfindCell::UNIT_PRESENT_FIXED) {
						adjustCoordToCell(i, j,  center, *pos, layer);
						return;
					}
				}
			}
		}
	}
	//DEBUG_LOG(("Couldn't find goal.\n"));
}

/** 
 * Returns coordinates of goal.
 *
 */
Bool Pathfinder::goalPosition(Object *obj, Coord3D *pos)
{
	Int iRadius;
	Bool center;
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return false; // only consider ai objects.
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell = *ai->getPathfindGoalCell();
	pos->zero();
	if (cell.x<0 || cell.y<0) return false;
	adjustCoordToCell(cell.x, cell.y,  center, *pos, LAYER_GROUND);
	return true;
}

Bool Pathfinder::checkForAdjust(Object *obj, const LocomotorSet& locomotorSet, Bool isHuman,
																Int cellX, Int cellY, PathfindLayerEnum layer, 
																Int iRadius, Bool center, Coord3D *dest, const Coord3D *groupDest) 
{
	Coord3D adjustDest;
	PathfindCell *cellP = getCell(layer, cellX, cellY);
	if (cellP==nullptr) return false;
	if (cellP && cellP->getType() == PathfindCell::CELL_CLIFF) {
		return false;  // no final destinations on cliffs.
	}
	if (isHuman) {
		// check if new cell is in logical map.	(computer can move off logical map)
		if (cellX < m_logicalExtent.lo.x ||
				cellY < m_logicalExtent.lo.y || 
				cellX > m_logicalExtent.hi.x || 
				cellY > m_logicalExtent.hi.y) return false; 
	}
	if (checkDestination(obj, cellX, cellY, layer, iRadius, center)) {
		adjustCoordToCell(cellX, cellY,  center, adjustDest, cellP->getLayer());
		Bool pathExists;
		Bool adjustedPathExists;
		if (obj->isKindOf(KINDOF_AIRCRAFT)) {
			pathExists = true;
			adjustedPathExists = true;
		}	else {
			pathExists = clientSafeQuickDoesPathExist( locomotorSet, obj->getPosition(), dest);
			adjustedPathExists = clientSafeQuickDoesPathExist( locomotorSet, obj->getPosition(), &adjustDest);
			if (!pathExists) {	
				if (clientSafeQuickDoesPathExist( locomotorSet, dest, &adjustDest))	{
 					adjustedPathExists = true;
				}
			}
		}
		if ( adjustedPathExists	) {
			if (groupDest) {
				tightenPath(obj, locomotorSet, &adjustDest, groupDest);
				// Check to see if it is a long way to get to the adjusted destination.
				Int cost = checkPathCost(obj, locomotorSet, groupDest, &adjustDest);
				Int dx = IABS(static_cast<Int>(groupDest->x-adjustDest.x));
				Int dy = IABS(static_cast<Int>(groupDest->y-adjustDest.y));
				if (1.4f*(dx+dy)<cost) {
					return false; 
				}
			}			
			*dest = adjustDest;
			return true;
		}
	}
	return false;
}

Bool Pathfinder::checkForLanding(Int cellX, Int cellY, PathfindLayerEnum layer, 
																Int iRadius, Bool center, Coord3D *dest) 
{
	Coord3D adjustDest;
	PathfindCell *cellP = getCell(layer, cellX, cellY);
	if (cellP==nullptr) return false;
	switch (cellP->getType())
	{
		case PathfindCell::CELL_CLIFF:
		case PathfindCell::CELL_WATER:
		case PathfindCell::CELL_IMPASSABLE:
			return false;  // no final destinations on cliffs, water, etc.
	}
	if (checkDestination(nullptr, cellX, cellY, layer, iRadius, center)) {
		adjustCoordToCell(cellX, cellY,  center, adjustDest, cellP->getLayer());
		*dest = adjustDest;
		return true;
	}
	return false;
}

/**
 * Find an unoccupied spot for a unit to land at.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::adjustToLandingDestination(Object *obj, Coord3D *dest)
{
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *dest;

	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent(&extent);
	// If the object is off the map & the goal is off the map, it is a scripted setup, so just
	// go to the dest.
	if (!extent.isInRegionNoZ(dest)) {
		if (!extent.isInRegionNoZ(obj->getPosition())) {
			return true;
		}
	}

	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	worldToCell( &adjustDest, &cell );

	enum {MAX_CELLS_TO_TRY=400};
	Int limit = MAX_CELLS_TO_TRY; 
	Int i, j;
	i = cell.x;
	j = cell.y;
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(dest);
	if (checkForLanding(i,j, layer, iRadius, center, dest)) {
		return true;
	}

	Int delta=1;
	Int count;
	while (limit>0) {
		for (count = delta; count>0; count--) {
			i++;
			limit--;
			if (checkForLanding(i,j, layer, iRadius, center, dest)) {
				return true;
			}
		}
		for (count = delta; count>0; count--) {
			j++;
			limit--;
			if (checkForLanding(i,j, layer, iRadius, center, dest)) {
				return true;
			}
		}
		delta++;
		for (count = delta; count>0; count--) {
			i--;
			limit--;
			if (checkForLanding(i,j, layer, iRadius, center, dest)) {
				return true;
			}
		}
		for (count = delta; count>0; count--) {
			j--;
			limit--;
			if (checkForLanding(i,j, layer, iRadius, center, dest)) {
				return true;
			}
		}
		delta++;
	}
	return false;
}	

/**
 * Find an unoccupied spot for a unit to move to.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::adjustDestination(Object *obj, const LocomotorSet& locomotorSet, Coord3D *dest, const Coord3D *groupDest)
{
	if( obj->isKindOf(KINDOF_PROJECTILE) )
	{
		return true; // missiles can go wherever they want to. jba.
	}

	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *dest;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	worldToCell( &adjustDest, &cell );
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(dest);
	if (groupDest) {
		layer = TheTerrainLogic->getLayerForDestination(groupDest);
	}

	enum {MAX_CELLS_TO_TRY=400};
	Int limit = MAX_CELLS_TO_TRY; 
	Int i, j;
	i = cell.x;
	j = cell.y;
	if (checkForAdjust(obj, locomotorSet, isHuman, i,j, layer, iRadius, center, dest, groupDest)) {
		return true;
	}

	Int delta=1;
	Int count;
	while (limit>0) {
		for (count = delta; count>0; count--) {
			i++;
			limit--;
			if (checkForAdjust(obj, locomotorSet, isHuman, i,j, layer, iRadius, center, dest, groupDest)) {
				return true;
			}
		}
		for (count = delta; count>0; count--) {
			j++;
			limit--;
			if (checkForAdjust(obj, locomotorSet, isHuman, i,j, layer, iRadius, center, dest, groupDest)) {
				return true;
			}
		}
		delta++;
		for (count = delta; count>0; count--) {
			i--;
			limit--;
			if (checkForAdjust(obj, locomotorSet, isHuman, i,j, layer, iRadius, center, dest, groupDest)) {
				return true;
			}
		}
		for (count = delta; count>0; count--) {
			j--;
			limit--;
			if (checkForAdjust(obj, locomotorSet, isHuman, i,j, layer, iRadius, center, dest, groupDest)) {
				return true;
			}
		}
		delta++;
	}
	if (groupDest) {
		// Didn't work, so just do simple adjust.
		return(adjustDestination(obj, locomotorSet, dest, nullptr));
	}
	//DEBUG_LOG(("adjustDestination failed, dest (%f, %f), adj dest (%f,%f), %x %s\n", dest->x, dest->y, adjustDest.x, adjustDest.y,
		//obj, obj->getTemplate()->getName().str()));
	return false;
}

Bool Pathfinder::checkForTarget(const Object *obj, 	Int cellX, Int cellY, const Weapon *weapon,
																const Object *victim, const Coord3D *victimPos,
																Int iRadius, Bool center,Coord3D *dest) 
{
	Coord3D adjustDest;
	if (checkDestination(obj, cellX, cellY, LAYER_GROUND, iRadius, center)) {
		adjustCoordToCell(cellX, cellY,  center, adjustDest, LAYER_GROUND);
		if (weapon->isGoalPosWithinAttackRange( obj, &adjustDest, victim, victimPos ))	{
			*dest = adjustDest;
			return true;
		}
	}
	return false;
}

/**
 * Find an unoccupied spot for a unit to move to that can fire at victim.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::adjustTargetDestination(const Object *obj, const Object *target, const Coord3D *targetPos, 
																				 const Weapon *weapon, Coord3D *dest)
{
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *dest;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	if (worldToCell( &adjustDest, &cell )) {
		return false; // outside of bounds.
	}
	enum {MAX_CELLS_TO_TRY=400};
	Int limit = MAX_CELLS_TO_TRY; 
	Int i, j;
	i = cell.x;
	j = cell.y;
	if (checkForTarget(obj, i,j, weapon, target, targetPos, iRadius, center, dest)) {
		return true;
	}

	Int delta=1;
	Int count;
	while (limit>0) {
		for (count = delta; count>0; count--) {
			i++;
			limit--;
			if (checkForTarget(obj, i,j, weapon, target, targetPos, iRadius, center, dest)) {
				return true;
			}
		}
		for (count = delta; count>0; count--) {
			j++;
			limit--;
			if (checkForTarget(obj, i,j, weapon, target, targetPos, iRadius, center, dest)) {
				return true;
			}
		}
		delta++;
		for (count = delta; count>0; count--) {
			i--;
			limit--;
			if (checkForTarget(obj, i,j, weapon, target, targetPos, iRadius, center, dest)) {
				return true;
			}
		}
		for (count = delta; count>0; count--) {
			j--;
			limit--;
			if (checkForTarget(obj, i,j, weapon, target, targetPos, iRadius, center, dest)) {
				return true;
			}
		}
		delta++;
	}
	return false;
}

Bool Pathfinder::checkForPossible(Bool isCrusher, Int fromZone,  Bool center, const LocomotorSet& locomotorSet, 
																	Int cellX, Int cellY, PathfindLayerEnum layer, Coord3D *dest, Bool startingInObstacle) 
{
	PathfindCell *goalCell = getCell(layer, cellX, cellY);
	if (!goalCell) return false;
	if (IS_IMPASSABLE(goalCell->getType())) return false;
	Int zone2 =  m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, goalCell->getZone());
	if (startingInObstacle) {
		zone2 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone2));
	}
	if (fromZone==zone2) {
		adjustCoordToCell(cellX, cellY,  center, *dest, layer);
		return true;
	}
	return false;
}

/**
 * Find a pathable spot near the destination.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::adjustToPossibleDestination(Object *obj, const LocomotorSet& locomotorSet, 
																						 Coord3D *dest)
{
	Int radius;
	Bool center;
	getRadiusAndCenter(obj, radius, center);
	ICoord2D goalCellNdx;
	Coord3D adjustDest = *dest;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	if (worldToCell( &adjustDest, &goalCellNdx )) {
		return false; // outside of bounds.
	}

	// determine goal cell
	PathfindCell *goalCell;
	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(dest);
	
	goalCell = getCell(destinationLayer, goalCellNdx.x, goalCellNdx.y);


	Coord3D from = *obj->getPosition();

	// determine start cell
	ICoord2D startCellNdx;
	worldToCell(&from, &startCellNdx);
	PathfindLayerEnum layer = LAYER_GROUND;
	if (obj) {
		layer = obj->getLayer();
	}
	PathfindCell *parentCell = getClippedCell( layer, &from );
	if (parentCell == nullptr) {
		return false;
	}

	//
	Int zone1, zone2;
	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;
	zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, parentCell->getZone()); 
	Bool isObstacle = false;
	if (parentCell->getType() == PathfindCell::CELL_OBSTACLE)	{
		isObstacle = true;
	}
	if (isObstacle) {
		zone1 = m_zoneManager.getEffectiveTerrainZone(static_cast<zoneStorageType>(zone1));
		zone1 = m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, static_cast<zoneStorageType>(zone1));
	}

	zone2 =  m_zoneManager.getEffectiveZone(locomotorSet.getValidSurfaces(), isCrusher, goalCell->getZone());

	if (zone1 == zone2) {
		if (checkDestination(obj, goalCellNdx.x, goalCellNdx.y, destinationLayer, radius, center)) {
			return true;
		}
	}

	enum {MAX_CELLS_TO_TRY=400};
	Int limit = MAX_CELLS_TO_TRY; 
	Int i, j;
	i = goalCellNdx.x;
	j = goalCellNdx.y;

	Int delta=1;
	Int count;
	while (limit>0) {
		for (count = delta; count>0; count--) {
			i++;
			limit--;
			if (checkForPossible(isCrusher, zone1, center, locomotorSet, i,j, destinationLayer, dest, isObstacle)) {
				if (checkDestination(obj, i, j, destinationLayer, radius, center)) {
					return true;
				}
			}
		}
		for (count = delta; count>0; count--) {
			j++;
			limit--;
			if (checkForPossible(isCrusher, zone1, center, locomotorSet, i,j, destinationLayer, dest, isObstacle)) {
				if (checkDestination(obj, i, j, destinationLayer, radius, center)) {
					return true;
				}
			}
		}
		delta++;
		for (count = delta; count>0; count--) {
			i--;
			limit--;
			if (checkForPossible(isCrusher, zone1, center, locomotorSet, i,j, destinationLayer, dest, isObstacle)) {
				if (checkDestination(obj, i, j, destinationLayer, radius, center)) {
					return true;
				}
			}
		}
		for (count = delta; count>0; count--) {
			j--;
			limit--;
			if (checkForPossible(isCrusher, zone1, center, locomotorSet, i,j, destinationLayer, dest, isObstacle)) {
				if (checkDestination(obj, i, j, destinationLayer, radius, center)) {
					return true;
				}
			}
		}
		delta++;
	}
	return false;
}

/**
 * Given two world-space points, call callback for each cell.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
Int Pathfinder::iterateCellsAlongLine( const Coord3D& startWorld, const Coord3D& endWorld, 
																			PathfindLayerEnum layer, CellAlongLineProc proc, void* userData )
{
	ICoord2D start, end;
	worldToCell( &startWorld, &start );
	worldToCell( &endWorld, &end );
	return iterateCellsAlongLine(start, end, layer, proc, userData);
}

/**
 * Given two world-space points, call callback for each cell.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
Int Pathfinder::iterateCellsAlongLine( const ICoord2D &start, const ICoord2D &end, 
																			PathfindLayerEnum layer, CellAlongLineProc proc, void* userData )
{
	Int delta_x = abs(end.x - start.x);			// The difference between the x's
	Int delta_y = abs(end.y - start.y);			// The difference between the y's
	Int x = start.x;												// Start x off at the first pixel
	Int y = start.y;												// Start y off at the first pixel

	Int xinc1, xinc2;
	if (end.x >= start.x)								// The x-values are increasing
	{
		xinc1 = 1;
		xinc2 = 1;
	}
	else																// The x-values are decreasing
	{
		xinc1 = -1;
		xinc2 = -1;
	}

	Int yinc1, yinc2;
	if (end.y >= start.y)               // The y-values are increasing
	{
		yinc1 = 1;
		yinc2 = 1;
	}
	else																// The y-values are decreasing
	{
		yinc1 = -1;
		yinc2 = -1;
	}

	Bool checkY = true;
	Int den, num, numadd, numpixels;
	if (delta_x >= delta_y)							// There is at least one x-value for every y-value
	{
		xinc1 = 0;												// Don't change the x when numerator >= denominator
		yinc2 = 0;												// Don't change the y for every iteration
		den = delta_x;
		num = delta_x / 2;
		numadd = delta_y;
		numpixels = delta_x;							// There are more x-values than y-values
	}
	else																// There is at least one y-value for every x-value
	{
		checkY = false;
		xinc2 = 0;												// Don't change the x for every iteration
		yinc1 = 0;												// Don't change the y when numerator >= denominator
		den = delta_y;
		num = delta_y / 2;
		numadd = delta_x;
		numpixels = delta_y;							// There are more y-values than x-values
	}

	PathfindCell* from = nullptr;
	for (Int curpixel = 0; curpixel <= numpixels; curpixel++)
	{
		PathfindCell* to = getCell( layer, x, y );
		if (to==nullptr) return 0;
		
		Int ret = (*proc)(this, from, to, x, y, userData);
		if (ret != 0)
			return ret;

		num += numadd;										// Increase the numerator by the top of the fraction
		if (num >= den)										// Check if numerator >= denominator
		{
			num -= den;											// Calculate the new numerator value
			x += xinc1;											// Change the x as appropriate
			y += yinc1;											// Change the y as appropriate
			from = to;
			to = getCell( layer, x, y );
			if (to==nullptr) return 0;
			Int ret2 = (*proc)(this, from, to, x, y, userData);
			if (ret2 != 0)
				return ret2;
		}
		x += xinc2;												// Change the x as appropriate
		y += yinc2;												// Change the y as appropriate

		from = to;
	}

	return 0;
}

static ObjectID getSlaverID(const Object* o)
{
	for (BehaviorModule** update = o->getBehaviorModules(); *update; ++update)
	{
		SlavedUpdateInterface* sdu = (*update)->getSlavedUpdateInterface();
		if (sdu != nullptr)
		{
			return sdu->getSlaverID();
		}
	}
	
	return INVALID_ID;
}

static ObjectID getContainerID(const Object* o)
{
	const Object* container = o ? o->getContainedBy() : nullptr;
	return container ? container->getID() : INVALID_ID;
}

struct segmentIntersectsStruct
{
	Object *theTallBuilding;
	ObjectID ignoreBuilding;
};

/*static*/ Int Pathfinder::segmentIntersectsBuildingCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	segmentIntersectsStruct* d = static_cast<segmentIntersectsStruct*>(userData);

	if (to != nullptr && (to->getType() == PathfindCell::CELL_OBSTACLE))
	{
		Object *obj = TheGameLogic->findObjectByID(to->getObstacleID());
		if (obj && obj->isKindOf(KINDOF_AIRCRAFT_PATH_AROUND)) {
			if (obj->getID() == d->ignoreBuilding) {
				return 0;
			}
			d->theTallBuilding = obj;
			return 1;
		}
	}

	return 0;	// keep going
}

struct ViewBlockedStruct
{
	const Object *obj;
	const Object *objOther;
};

/*static*/ Int Pathfinder::lineBlockedByObstacleCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	const ViewBlockedStruct* d = static_cast<const ViewBlockedStruct*>(userData);

	if (to != nullptr && (to->getType() == PathfindCell::CELL_OBSTACLE))
	{

		// we never block our own view!
		if (to->isObstaclePresent(d->obj->getID()))
			return 0;

		// nor does the object we're trying to see!
		if (to->isObstaclePresent(d->objOther->getID()))
			return 0;

		// if the obstacle is our container, ignore it as an obstacle.
		if (to->isObstaclePresent(getContainerID(d->obj)))
			return 0;

		// @todo: if the obstacle is objOther's container, AND it's a "visible" container, ignore it.

		// if the obstacle is the item to which we are slaved, ignore it as an obstacle.
		if (to->isObstaclePresent(getSlaverID(d->obj)))
			return 0;

		// if the obstacle is the item to which objOther is slaved, ignore it as an obstacle.				
		if (to->isObstaclePresent(getSlaverID(d->objOther)))
			return 0;

		// if the obstacle is transparent, ignore it, since this callback is only used for line-of-sight. (srj)
		if (to->isObstacleTransparent())
			return 0;

		return 1;	// bail early
	}

	return 0;	// keep going
}

struct ViewAttackBlockedStruct
{
	const Object *obj;
	const Object *victim;
	const PathfindCell *victimCell;
	Int		skipCount;
};

/*static*/ Int Pathfinder::attackBlockedByObstacleCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	ViewAttackBlockedStruct* d = static_cast<ViewAttackBlockedStruct*>(userData);

	if (d->skipCount>0) {
		d->skipCount--;
		return 0;
	}
	if (to != nullptr && (to->getType() == PathfindCell::CELL_OBSTACLE))
	{
		// we never block our own view!
		if (to->isObstaclePresent(d->obj->getID()))
			return 0;

		if (d->victim) {
			// nor does the object we're trying to attack!
			if (to->isObstaclePresent(d->victim->getID()))
				return 0;
			// if the obstacle is the item to which objOther is slaved, ignore it as an obstacle.				
			if (to->isObstaclePresent(getSlaverID(d->victim)))
				return 0;
		}

		// if the obstacle is our container, ignore it as an obstacle.
		if (to->isObstaclePresent(getContainerID(d->obj)))
			return 0;

		// @todo: if the obstacle is objOther's container, AND it's a "visible" container, ignore it.

		// if the obstacle is the item to which we are slaved, ignore it as an obstacle.
		if (to->isObstaclePresent(getSlaverID(d->obj)))
			return 0;

		if (to->isObstacleTransparent())
			return 0;
		//Kris: Added the check for victimCell because in China01 -- after the intro, NW of your 
		//base is a cream colored building that lies in a negative coord. When you order units to 
		//force attack it, it crashes.
		if( d->victimCell && to->isObstaclePresent( d->victimCell->getObstacleID() ) ) 
		{
			// Victim is inside the bounds of another object.  We don't let this block us, 
			// as usually it is on the edge and it looks like we should be able to shoot it. jba.
			return 0;
		}
		return 1;	// bail early
	}

	return 0;	// keep going
}

//-----------------------------------------------------------------------------
Bool Pathfinder::isViewBlockedByObstacle(const Object* obj, const Object* objOther)
{
	ViewBlockedStruct info;
	info.obj = obj;
	info.objOther = objOther;
	if (objOther && objOther->isSignificantlyAboveTerrain()) {
		return false; // We don't check los to flying objects.  jba.
	}
	if (!obj || !objOther) {
		return false;
	}
#if 1
	return isAttackViewBlockedByObstacle(obj, *obj->getPosition(), objOther, *objOther->getPosition());
#else 
	PathfindLayerEnum layer = objOther->getLayer();
	if (layer==LAYER_GROUND) {
		layer = obj->getLayer();
	}
	Int ret = iterateCellsAlongLine(*obj->getPosition(), *objOther->getPosition(), 
		layer, lineBlockedByObstacleCallback, &info);
	return ret != 0;
#endif 
}

//-----------------------------------------------------------------------------
Bool Pathfinder::isAttackViewBlockedByObstacle(const Object* attacker, const Coord3D& attackerPos, const Object* victim, const Coord3D& victimPos)
{
	//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() - attackerPos is (%g,%g,%g) (%X,%X,%X)\n",
	//	attackerPos.x, attackerPos.y, attackerPos.z,
	//	AS_INT(attackerPos.x),AS_INT(attackerPos.y),AS_INT(attackerPos.z)));
	//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() - victimPos is (%g,%g,%g) (%X,%X,%X)\n",
	//	victimPos.x, victimPos.y, victimPos.z,
	//	AS_INT(victimPos.x),AS_INT(victimPos.y),AS_INT(victimPos.z)));
	// Global switch to turn this off in case it doesn't work.
	if (!TheAI->getAiData()->m_attackUsesLineOfSight) 
	{
		//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 1\n"));
		return false;
	}

	// If the attacker doesn't need line of sight, isn't blocked.
	if (!attacker->isKindOf(KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT)) 
	{
		//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 2\n"));
		return false;
	}

// srj sez: this is a good start at taking terrain into account for attacks, but findAttackPath needs to be smartened also
#define LOS_TERRAIN
#ifdef LOS_TERRAIN
	const Weapon* w = attacker->getCurrentWeapon();
	if (attacker->isKindOf(KINDOF_IMMOBILE)) {
		// Don't take terrain blockage into account, since we can't move around it. jba.
		w = nullptr;
	}
	if (w)
	{
		Bool viewBlocked;
		if (victim)
			viewBlocked = !w->isClearGoalFiringLineOfSightTerrain(attacker, attackerPos, victim);
		else
			viewBlocked = !w->isClearGoalFiringLineOfSightTerrain(attacker, attackerPos, victimPos);

		if (viewBlocked)
		{
			//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 3\n"));
			return true;
		}
	}
#endif

	ViewAttackBlockedStruct info;
	info.obj = attacker;
	info.victim = victim;
	PathfindLayerEnum layer = LAYER_GROUND;
	if (victim) {
		layer = victim->getLayer();
	}
	info.victimCell = getCell(layer, &victimPos);
	
	info.skipCount = 0;
	if (attacker->getLayer() != LAYER_GROUND) 
	{
		info.skipCount = 3;	/// srj -- someone wanna tell me what this magic number means?
												/// jba - Yes, it means that if someone is on a bridge, or rooftop, they can see 
												///      3 pathfind cells out of whatever they are standing on.  
												/// srj -- awesome! thank you very much :-)
		if (layer==LAYER_GROUND) {
			layer = attacker->getLayer();
		}
	}

	Int ret = iterateCellsAlongLine(attackerPos, victimPos, layer, attackBlockedByObstacleCallback, &info);
	//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 4\n"));
	return ret != 0;
}

static void computeNormalRadialOffset(const Coord3D& from,	Coord3D& insert, const Coord3D& to, 
																			Object *obj, Real radius)
{
	Real crossProduct;
	Real dx = to.x - from.x;
	Real dy = to.y -from.y;
	Coord3D objPos = *obj->getPosition();


	Real objDx = objPos.x - from.x;
	Real objDy = objPos.y - from.y;

	crossProduct = dx*objDy - dy*objDx;

	Coord3D fromToNormal;
	fromToNormal.z = 0;
	if (crossProduct>0) {
		fromToNormal.x = dy;
		fromToNormal.y = -dx;
	}	else {
		fromToNormal.x = -dy;
		fromToNormal.y = dx;
	}
	fromToNormal.normalize();
	Real length = radius;
	insert = *obj->getPosition();
	insert.x += fromToNormal.x*length;
	insert.y += fromToNormal.y*length;

}

//-----------------------------------------------------------------------------
Bool Pathfinder::segmentIntersectsTallBuilding(const PathNode *curNode, 
										PathNode *nextNode,  ObjectID ignoreBuilding, Coord3D *insertPos1,  Coord3D *insertPos2,  Coord3D *insertPos3 )
{
	segmentIntersectsStruct info;
	info.theTallBuilding = nullptr;
	info.ignoreBuilding = ignoreBuilding;

	Coord3D fromPos = *curNode->getPosition();
	Coord3D toPos = *nextNode->getPosition();

	Int i;
	for (i=0; i<2; i++) {
		Int ret = iterateCellsAlongLine(fromPos, toPos, LAYER_GROUND, segmentIntersectsBuildingCallback, &info);
		if (ret!=0 && info.theTallBuilding) {
			// see if toPos is inside the radius of the tall building.
			Coord3D bldgPos = *info.theTallBuilding->getPosition();
			Coord2D delta;
			Real radius = info.theTallBuilding->getGeometryInfo().getBoundingCircleRadius() + 2*PATHFIND_CELL_SIZE_F; 
			delta.x = toPos.x - bldgPos.x;
			delta.y = toPos.y - bldgPos.y;
			if (delta.length() <= radius*0.98) {
				if (delta.length() < 0.1) {
					delta.x = 1;
				}
				delta.normalize();
				delta.x *= radius;
				delta.y *= radius;
				toPos.x = bldgPos.x+delta.x;
				toPos.y = bldgPos.y+delta.y;
				nextNode->setPosition(&toPos);
				continue;
			}
			delta.x = fromPos.x - bldgPos.x;
			delta.y = fromPos.y - bldgPos.y;
			if (delta.length() <= radius*0.98) {
				if (delta.length() < 0.1) {
					delta.x = 1;
				}
				delta.normalize();
				delta.x *= radius;
				delta.y *= radius;
				fromPos.x = bldgPos.x+delta.x;
				fromPos.y = bldgPos.y+delta.y;
			}


			computeNormalRadialOffset(fromPos, *insertPos2, toPos, info.theTallBuilding, radius);
			computeNormalRadialOffset(fromPos, *insertPos1, *insertPos2, info.theTallBuilding, radius);
			computeNormalRadialOffset(*insertPos2, *insertPos3, toPos, info.theTallBuilding, radius);

			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
Bool Pathfinder::circleClipsTallBuilding(	const Coord3D *from, const Coord3D *to, Real circleRadius, ObjectID ignoreBuilding, Coord3D *adjustTo)
{
	PartitionFilterAcceptByKindOf filterKindof(MAKE_KINDOF_MASK(KINDOF_AIRCRAFT_PATH_AROUND), KINDOFMASK_NONE);
	PartitionFilter *filters[] = { &filterKindof, nullptr };
	Object* tallBuilding = ThePartitionManager->getClosestObject(to, circleRadius, DistanceCalculationType::FROM_BOUNDINGSPHERE_2D, filters);
	if (tallBuilding) {
		Real radius = tallBuilding->getGeometryInfo().getBoundingCircleRadius() + 2*PATHFIND_CELL_SIZE_F; 
		computeNormalRadialOffset(*from, *adjustTo, *to, tallBuilding, circleRadius+radius);
		Object* otherTallBuilding = ThePartitionManager->getClosestObject(adjustTo, circleRadius, DistanceCalculationType::FROM_BOUNDINGSPHERE_2D, filters);
		if (otherTallBuilding && otherTallBuilding!=tallBuilding) {
			radius = otherTallBuilding->getGeometryInfo().getBoundingCircleRadius() + 2*PATHFIND_CELL_SIZE_F; 
			Coord3D tmpTo = *adjustTo;
			computeNormalRadialOffset(*from, *adjustTo, tmpTo, otherTallBuilding, circleRadius+radius);
		}
		return true;
	}
	return false;
}

struct LinePassableStruct
{
	const Object *obj;
	LocomotorSurfaceTypeMask acceptableSurfaces;
	Int radius;
	Bool centerInCell;
	Bool blocked;
	Bool allowPinched;
};

/*static*/ Int Pathfinder::linePassableCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	const LinePassableStruct* d = static_cast<const LinePassableStruct*>(userData);

	Bool isCrusher = d->obj ? d->obj->getCrusherLevel() > 0 : false;
	TCheckMovementInfo info;
	info.cell.x = to_x;
	info.cell.y = to_y;
	info.layer = to->getLayer();
	info.centerInCell = d->centerInCell;
	info.radius = d->radius;
	info.considerTransient = d->blocked;
	info.acceptableSurfaces = d->acceptableSurfaces;
	if (!pathfinder->checkForMovement(d->obj, info))
	{
		return 1;	// bail out
	}

	if (info.allyFixedCount || info.enemyFixed) 
	{
		return 1;	// bail out
	}

	if (!d->allowPinched && to->getPinched()) {
		return 1; // bail out.
	}

	if (from && to->getLayer() != LAYER_GROUND && from->getLayer() == to->getLayer()) {
		if (to->getType() == PathfindCell::CELL_CLEAR) {
			return 0;
		}
	}

	if (pathfinder->validMovementPosition( isCrusher, d->acceptableSurfaces, to, from ) == false)
	{
		return 1;	// bail out
	}

	return 0;	// keep going
}

struct GroundPathPassableStruct
{
	Int		diameter;
	Bool	crusher;
};

/*static*/ Int Pathfinder::groundPathPassableCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	const GroundPathPassableStruct* d = static_cast<const GroundPathPassableStruct*>(userData);

	Int curDiameter = pathfinder->clearCellForDiameter(d->crusher, to_x, to_y, to->getLayer(), d->diameter);
	if (curDiameter==d->diameter) return 0;	//  good to go.
	if (from && to->getLayer() != LAYER_GROUND && from->getLayer() == to->getLayer()) {
		return 0;
	}

	return 1;	// failed.
}

/**
 * Given two world-space points, check the line of sight between them for any impassible cells.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
Bool Pathfinder::isLinePassable( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces, 
																PathfindLayerEnum layer, const Coord3D& startWorld, 
																const Coord3D& endWorld, Bool blocked, 
																Bool allowPinched)
{
	LinePassableStruct info;
	//CRCDEBUG_LOG(("Pathfinder::isLinePassable(): %d %d %d \n", m_ignoreObstacleID, m_isMapReady, m_isTunneling));

	info.obj = obj;
	info.acceptableSurfaces = acceptableSurfaces;
	getRadiusAndCenter(obj, info.radius, info.centerInCell);
	info.blocked = blocked;
	info.allowPinched = allowPinched;

	Int ret = iterateCellsAlongLine(startWorld, endWorld, layer, linePassableCallback, reinterpret_cast<void*>(&info));
	return ret == 0;
}

/**
 * Given two world-space points, check the line of sight between them for any impassible cells.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
Bool Pathfinder::isGroundPathPassable( Bool isCrusher, const Coord3D& startWorld, PathfindLayerEnum startLayer, 
		const Coord3D& endWorld, Int pathDiameter)
{
	GroundPathPassableStruct info;

	info.diameter = pathDiameter;
	info.crusher = isCrusher;

	Int ret = iterateCellsAlongLine(startWorld, endWorld, startLayer, groundPathPassableCallback, reinterpret_cast<void*>(&info));
	return ret == 0;
}
