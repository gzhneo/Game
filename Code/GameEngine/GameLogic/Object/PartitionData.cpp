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

#include "Common/LogicToClient.h"
#include "Common/ActionManager.h"
#include "Common/DiscreteCircle.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Radar.h"
#include "Common/ThingFactory.h"	// for bullet type hack
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/Squad.h"
#include "GameLogic/GhostObject.h"
#include "GameClient/Line2D.h"
#include "GameClient/ControlBar.h"
#include "Common/PlayerList.h"
#include "Common/MapObject.h"

#include "PartitionManagerInternal.h"

// Per-object partition data: cell coverage, shroud status per player, collision checks

#define DISABLE_INVALID_PREVENTION	//Steven, I had to turn this off because it was causing problem with map border resizing (USA04). -MW

//-----------------------------------------------------------------------------
PartitionData::PartitionData()
{
	//DEBUG_LOG(("create pd %08lx\n",this));
	m_next = nullptr;
	m_prev = nullptr;
	m_nextDirty = nullptr;
	m_prevDirty = nullptr;
	m_object = nullptr;
	m_ghostObject = nullptr;
	m_coiArrayCount = 0;
	m_coiArray = nullptr;
	m_coiInUseCount = 0;
	m_doneFlag = 0;
	m_dirtyStatus = DirtyStatus::NOT_DIRTY;
	m_lastCell = nullptr;
	for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		m_everSeenByPlayer[i] = false;
		m_shroudedness[i] = OBJECTSHROUD_INVALID;
		m_shroudednessPrevious[i] = OBJECTSHROUD_INVALID;
	}
}

//-----------------------------------------------------------------------------
PartitionData::~PartitionData()
{
	//DEBUG_LOG(("toss pd for pd %08lx obj %08lx\n",this,m_object));
	removeAllTouchedCells();
	freeCoiArray();
	DEBUG_ASSERTCRASH(ThePartitionManager, ("ThePartitionManager is null"));
	if (ThePartitionManager && ThePartitionManager->isInListDirtyModules(this))
	{
		//DEBUG_LOG(("remove pd %08lx from dirty list (%08lx %08lx)\n",this,m_prevDirty,m_nextDirty));
		ThePartitionManager->removeFromDirtyModules(this);
		//DEBUG_ASSERTCRASH(!ThePartitionManager->isInListDirtyModules(this), ("hmm\n"));
	}
} 

//-----------------------------------------------------------------------------
Int PartitionData::getControllingPlayerIndex() const
{
	const Player* p = getObject()->getControllingPlayer();
	if (p)
	{
		Int playerIndex = p->getPlayerIndex();
		DEBUG_ASSERTCRASH(playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT, ("bad playerIndex"));
		return playerIndex;
	}

	DEBUG_CRASH(("this should never happen"));
	throw ERROR_BUG;
}

//-----------------------------------------------------------------------------
// only used to restore state after map border resizing and/or xfer!
void PartitionData::friend_setShroudednessPrevious(Int playerIndex, ObjectShroudStatus status) 
{
	m_shroudednessPrevious[playerIndex] = status;

	// if our status is anything but shrouded, assume we've never been seen by the player.
	m_everSeenByPlayer[playerIndex] = (status == OBJECTSHROUD_SHROUDED) ? false : true;

	// if we're already invalid, mark us as special-invalid.
#ifndef DISABLE_INVALID_PREVENTION
	if (m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID)
		m_shroudedness[playerIndex] = OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID;
#endif
} 

//-----------------------------------------------------------------------------
ObjectShroudStatus PartitionData::getShroudedStatus(Int playerIndex)
{
	// sanity
	DEBUG_ASSERTCRASH( playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT, 
										 ("PartitionData::getShroudedStatus - Invalid player index '%d'\n", playerIndex) );

	if (!ThePartitionManager->getUpdatedSinceLastReset()) 
	{
		// The shroud should be invalid until update has been called.
		// I'll set myself as invalid so the next time you ask I'll recompute.  (If I just return Invalid, then the old state will persist unfairly over a reset)
		invalidateShroudedStatusForPlayer(playerIndex);
		return m_shroudedness[playerIndex];
	}

#ifndef DISABLE_INVALID_PREVENTION
	if (m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID || m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID)
	{
		Bool updateShroudednessPrevious = (m_shroudedness[playerIndex] != OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID);
#else
	if (m_shroudedness[playerIndex] == OBJECTSHROUD_INVALID)
	{
#endif
		Int shroudedCells = 0;
		Int foggedCells = 0;
		CellAndObjectIntersection* coi = m_coiArray;
		for (Int i = m_coiInUseCount; i; --i, ++coi)
		{
			if( coi->getCell()->getShroudStatusForPlayer(playerIndex) == CellShroudStatus::CELLSHROUD_SHROUDED )
				++shroudedCells;
			else if( coi->getCell()->getShroudStatusForPlayer(playerIndex) == CellShroudStatus::CELLSHROUD_FOGGED )
				++foggedCells;
		}

		if( m_coiInUseCount == 0 )
		{	m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;				// Stuff off the map have no COIs since they aren't in the PartitionCell range
			m_everSeenByPlayer[playerIndex] = false;	//force object as never seen by the player
		}
		else if( shroudedCells == m_coiInUseCount )
		{	m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;				// every cell I use is shrouded
			m_everSeenByPlayer[playerIndex] = false; //force object as never seen by the player
			if (m_ghostObject && m_shroudednessPrevious[playerIndex] == OBJECTSHROUD_FOGGED)
			{	//we are shrouding an area that used to be fogged so release our memory of what was there.
				m_ghostObject->freeSnapShot(playerIndex);
			}
		}
		else if( shroudedCells + foggedCells == m_coiInUseCount )
		{	
			m_shroudedness[playerIndex] = OBJECTSHROUD_FOGGED;	//object is visible but fogged.
			if (m_object && m_ghostObject)	//object does not exist for modules holding only GhostObjects
			{
				//fogged but may not be visible if faction unit or faction building that has not been seen before
				Player *player=ThePlayerList->getNthPlayer(playerIndex);
				if (player->getRelationship(m_object->getTeam()) == NEUTRAL)
				{	//anything neutral that moves around will not be rendered inside fog.
					if (!m_object->isKindOf(KINDOF_IMMOBILE))
						m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;
				}
				else	//Not neutral
				{	//enemy unit will always be shrouded unless it's a building that's already been seen by the player.  Fogged Mines are also always
					//shroued no matter what.
					if (!(m_object->isKindOf(KINDOF_IMMOBILE) && m_everSeenByPlayer[playerIndex]) || m_object->isKindOf(KINDOF_MINE))
						m_shroudedness[playerIndex] = OBJECTSHROUD_SHROUDED;
				}
				if (m_shroudedness[playerIndex] == OBJECTSHROUD_FOGGED)
				{	//successfully applied fog to object so check if we need to freeze it's state
					if (m_shroudednessPrevious[playerIndex] < OBJECTSHROUD_FOGGED)
					{	//object was not previously fogged but now is fogged.
						//need to record its current state so that it doesn't change
						//while fogged.
						m_ghostObject->snapShot(playerIndex);
					}
				}
			}
		}
		else if( shroudedCells == 0  &&  foggedCells == 0 )
		{	//Record that this object was seen by the player.  This info will be used to show fogged enemy faction buildings.
			m_everSeenByPlayer[playerIndex] = true;
			m_shroudedness[playerIndex] = OBJECTSHROUD_CLEAR;
			if (m_ghostObject && m_shroudednessPrevious[playerIndex] == OBJECTSHROUD_FOGGED)
			{	//object was previously fogged but now is visible so we no longer
				//need a ghost object.
				m_ghostObject->freeSnapShot(playerIndex);
			}
		}	// no cell I use has anything
		else
		{	//Record that this object was seen by the player.  This info will be used to show fogged enemy faction buildings.
			m_everSeenByPlayer[playerIndex] = true;
			m_shroudedness[playerIndex] = OBJECTSHROUD_PARTIAL_CLEAR;		// I am at least partially clear otherwise
			if (m_ghostObject && m_shroudednessPrevious[playerIndex] == OBJECTSHROUD_FOGGED)
			{	//object was previously fogged but now is visible so we no longer
				//need a ghost object.
				m_ghostObject->freeSnapShot(playerIndex);
			}
		}
#ifndef DISABLE_INVALID_PREVENTION
 		if (m_coiInUseCount && updateShroudednessPrevious) 
		{	
#else
 		if (m_coiInUseCount) 
		{	
#endif
			//only remember the previous state of objects actually on the map.
 			m_shroudednessPrevious[playerIndex] = m_shroudedness[playerIndex];
		}
	}

	return m_shroudedness[playerIndex];
}

//-----------------------------------------------------------------------------
void PartitionData::removeAllTouchedCells()
{
	CellAndObjectIntersection *coi = m_coiArray;
	for (Int i = m_coiArrayCount; i > 0; --i, ++coi)
	{
		if (coi->getModule())
		{
			DEBUG_ASSERTCRASH(coi->getModule() == this, ("coi uses wrong module"));
			coi->removeAllCoverage();
			DEBUG_ASSERTCRASH(!coi->getModule(), ("coi should no longer be in use"));
			--m_coiInUseCount;
		}
	}
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
}

// -----------------------------------------------------------------------------
void PartitionData::addSubPixToCoverage(PartitionCell *cell)
{
	DEBUG_ASSERTCRASH(m_coiInUseCount < m_coiArrayCount, ("not enough cois allocated for this object"));
	if (cell)
	{			
		// see if we already have a coi for this cell.
		CellAndObjectIntersection *coi = m_coiArray;
		CellAndObjectIntersection *coiToUse = nullptr;
		for (Int i = __min(m_coiInUseCount,m_coiArrayCount); i; --i, ++coi)
		{
			if (coi->getCell() == cell)
			{
				coiToUse = coi;
				break;
			}
		}
		DEBUG_ASSERTCRASH(coiToUse != nullptr || m_coiInUseCount < m_coiArrayCount, ("not enough cois allocated for this object"));
		if (coiToUse == nullptr && m_coiInUseCount < m_coiArrayCount)
		{
			// nope, no coi for this cell, allocate a new one
			coiToUse = &m_coiArray[m_coiInUseCount++];
		}
		if (coiToUse)
		{
			coiToUse->addCoverage(cell, this);
		}
	}
}

// -----------------------------------------------------------------------------
void PartitionData::doRectFill(
	Real centerX,
	Real centerY,
	Real halfsizeX,
	Real halfsizeY,
	Real angle
)
{
	Real c = static_cast<Real>(Cos(angle));
	Real s = static_cast<Real>(Sin(angle));

	Real actualCellSize = ThePartitionManager->getCellSize();
	Real stepSize = actualCellSize * 0.5f; // in theory, should be getCellSize() exactly, but needs to be smaller to avoid aliasing problems
	Real ydx = s * stepSize;
	Real ydy = -c * stepSize;
	Real xdx = c * stepSize;
	Real xdy = s * stepSize;

	// GS 12-26-02  Oh my.  The step size has been fudged down to ensure that a cell is not jumped over,
	// but then the original step size was used to determine the number of steps.  This error gets
	// more pronounced on bigger buildings.  ie a 100 X 160 registers cells for a 80 x 100.  If you
	// fudge one, you have to fudge the other.
//	Real stepSizeInvTimes2 = 2.0f * ThePartitionManager->getCellSizeInv();
	Real stepSizeInvTimes2 = 2.0f * (1.0f / stepSize);

	/*
		srj fixes subtle bug: need +1 to get all of misaligned geometries accted for.
	*/
//	Int numStepsX = REAL_TO_INT_CEIL(halfsizeX * stepSizeInvTimes2) + 1;
//	Int numStepsY = REAL_TO_INT_CEIL(halfsizeY * stepSizeInvTimes2) + 1;
	Int numStepsX = REAL_TO_INT_CEIL(halfsizeX * stepSizeInvTimes2);
	Int numStepsY = REAL_TO_INT_CEIL(halfsizeY * stepSizeInvTimes2);
	// GS 12-26-02  Oh my part 2.  Now that we are no longer greatly underestimating our COI imprint,
	// we don't need this fudge.  The fudge is not in the calcMaxCOIForShape, so it just results in 
	// good COIs getting dropped since we didn't allocate enough to handle this fudge.

	Real tl_x = centerX - halfsizeX*c - halfsizeY*s;
	Real tl_y = centerY + halfsizeY*c - halfsizeX*s;

	for (Int iy = 0; iy < numStepsY; ++iy, tl_x += ydx, tl_y += ydy)
	{
		Real x = tl_x;
		Real y = tl_y;
		for (Int ix = 0; ix < numStepsX; ++ix, x += xdx, y += xdy)
		{
			Int cellx, celly;
			ThePartitionManager->worldToCell(x, y, &cellx, &celly);
			PartitionCell *cell = ThePartitionManager->getCellAt(cellx, celly);	// might be null if off the edge
			if (cell)
			{
				addSubPixToCoverage(cell);
			}
		}
	}

}

// -----------------------------------------------------------------------------
void PartitionData::hLineCircle(Int x1, Int x2, Int y)
{
	for (Int x = x1; x <= x2; ++x)
	{
		PartitionCell* cell = ThePartitionManager->getCellAt(x, y);
		if (cell)
		{
      addSubPixToCoverage(cell);
		}
	}
}

// -----------------------------------------------------------------------------
void PartitionData::doCircleFill(
	Real centerX,
	Real centerY,
	Real radius
)
{
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("expected no coi in use here"));

	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	Int y = cellRadius - 1;
	Int dec = 3 - 2*cellRadius;
	for (Int x = 0; x < cellRadius; x++)
	{
		hLineCircle(cellCenterX - x, cellCenterX + x, cellCenterY + y);
		hLineCircle(cellCenterX - x, cellCenterX + x, cellCenterY - y);
		hLineCircle(cellCenterX - y, cellCenterX + y, cellCenterY + x);
		hLineCircle(cellCenterX - y, cellCenterX + y, cellCenterY - x);

		if (dec >= 0)
		{
			dec += (1 - y) << 2;
			--y;
		}
		dec += (x << 2) + 6;
	}
}

// -----------------------------------------------------------------------------
void PartitionData::doSmallFill(
	Real centerX,
	Real centerY,
	Real radius
)
{
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("expected no coi in use here"));

	Real halfCellSize = ThePartitionManager->getCellSize() * 0.5f;
	if (radius > halfCellSize)
	{
		// data fault (an object with a small geometry class but a large radius): logged once, not asserted
		static bool logged = false;
		if (!logged)
		{
			logged = true;
			DEBUG_LOG(("*** ASSET ERROR: object is too large to use a 'small' geometry, truncating size to cellsize\n"));
		}
		radius = halfCellSize;
	}

	Int cx1, cy1, cx2, cy2;
	ThePartitionManager->worldToCell(centerX - radius, centerY - radius, &cx1, &cy1);
	ThePartitionManager->worldToCell(centerX + radius, centerY + radius, &cx2, &cy2);

	DEBUG_ASSERTCRASH(absInt(cx2-cx1)<=1,("bad cx"));
	DEBUG_ASSERTCRASH(absInt(cy2-cy1)<=1,("bad cy"));

	for (Int x = cx1; x <= cx2; x++)
	{
		for (Int y = cy1; y <= cy2; y++)
		{
			PartitionCell *cell = ThePartitionManager->getCellAt(x, y);
			if (cell)
			{
				m_coiArray[m_coiInUseCount++].addCoverage(cell, this);
			}
		}
	}

	#ifdef INTENSE_DEBUG
	for (int i = 0; i < m_coiInUseCount; i++)
	{
		for (int j = 0; j < i; j++)
		{
			DEBUG_ASSERTCRASH(m_coiArray[i].getCell() != m_coiArray[j].getCell(), ("dup cells"));
		}
	}
	#endif
}

//-----------------------------------------------------------------------------
void PartitionData::addPossibleCollisions(PartitionContactList *ctList)
{
// actually, we do occasionally want to detect collisions of dead AIs.
// e.g., dead technicals flying thru the air should check for collisions with
// immobile structures, because otherwise they pass thru them, which looks dopey.
// if preventing dead-ai-collisions is necessary to fix something else, please
// ensure that the above case is cool before signing off. (srj)
#ifdef NOPE_READ_THE_COMMENT
	// dead objects never collide with things...
 	AIUpdateInterface *ai = getObject()->getAIUpdateInterface();
 	if (ai && ai->isAiInDeadState())
	{
		return;
	}
#endif

	//DEBUG_LOG(("adding possible collision for %s\n",getObject()->getTemplate()->getName().str()));

	CellAndObjectIntersection *myCoi = m_coiArray;
	for (Int i = m_coiInUseCount; i > 0; --i, ++myCoi)
	{
		PartitionCell *cell = myCoi->getCell();
		if (cell->getCoiCount() < 2)
			continue;

		for (CellAndObjectIntersection *coi = cell->getFirstCoiInCell(); coi; coi = coi->getNextCoi())
		{
			PartitionData *that = coi->getModule();
			if (this != that)
			{
				ctList->addToContactList(this, that);
			}
		}
	}
}

//-----------------------------------------------------------------------------
Bool PartitionData::collidesWith(const PartitionData *that, CollideLocAndNormal *cinfo) const
{
	const Object *thisObj = this->getObject();
	const Object *thatObj = that->getObject();

	if( thisObj->isKindOf( KINDOF_NO_COLLIDE )  ||  thatObj->isKindOf( KINDOF_NO_COLLIDE ) )
		return FALSE; // A collision extent of zero size is still a point and can collide, but we don't always want to.

	CollideInfo thisInfo(thisObj->getPosition(), thisObj->getGeometryInfo(), thisObj->getOrientation());
	CollideInfo thatInfo(thatObj->getPosition(), thatObj->getGeometryInfo(), thatObj->getOrientation());

	// invariant for all geometries: first do z collision check.
	Real thisTop = thisInfo.position.z + thisInfo.geom.getMaxHeightAbovePosition();
	Real thisBot = thisInfo.position.z - thisInfo.geom.getMaxHeightBelowPosition();
	Real thatTop = thatInfo.position.z + thatInfo.geom.getMaxHeightAbovePosition();
	Real thatBot = thatInfo.position.z - thatInfo.geom.getMaxHeightBelowPosition();
	if (thisTop >= thatBot && thisBot <= thatTop)
	{
		GeometryType thisGeom = thisObj->getGeometryInfo().getGeomType();
		GeometryType thatGeom = thatObj->getGeometryInfo().getGeomType();

		//
		// NOTE: This assumes geometry enumerations that start at GEOMETRY_FIRST AND depends on the
		// order in which they appear in the enum list
		//
		CollideTestProc collideProc = theCollideTestProcs[ (thisGeom - GEOMETRY_FIRST) * GEOMETRY_NUM_TYPES + (thatGeom - GEOMETRY_FIRST) ];
		return (*collideProc)(&thisInfo, &thatInfo, cinfo);
	}
	else
	{
		// no z-intersection -> no collision.
		return false;
	}
}

//-----------------------------------------------------------------------------
void PartitionData::updateCellsTouched()
{
	GeometryType geom;
	Bool isSmall;
	Coord3D pos;
	Real angle,majorRadius,minorRadius;


	Object *obj = getObject();
	DEBUG_ASSERTCRASH(obj != nullptr || m_ghostObject != nullptr, ("must be attached to an Object here 1"));
	// without an Object and a GhostObject, geom/isSmall/pos/angle/majorRadius/minorRadius stay unset
	if (!obj && !m_ghostObject)
		return;

	if (obj)
	{	
		//we have no object using this PartitionData but we still have a GhostObject so copy its data.
		geom = obj->getGeometryInfo().getGeomType();
		isSmall = obj->getGeometryInfo().getIsSmall();
		pos = *(obj->getPosition());
		angle = obj->getOrientation();
		majorRadius = obj->getGeometryInfo().getMajorRadius();
		minorRadius = obj->getGeometryInfo().getMinorRadius();
	}
	else
	{
		geom = m_ghostObject->getGeometryType();
		isSmall = m_ghostObject->getGeometrySmall();
		pos = *m_ghostObject->getParentPosition();
		angle = m_ghostObject->getParentAngle();
		majorRadius = m_ghostObject->getGeometryMajorRadius();
		minorRadius = m_ghostObject->getGeometryMinorRadius();
	}

	removeAllTouchedCells();
	if (isSmall)
	{
		doSmallFill(pos.x, pos.y, majorRadius);
	}
	else
	{
		switch(geom)
		{
			case GEOMETRY_SPHERE:
			case GEOMETRY_CYLINDER:
			{
				doCircleFill(pos.x, pos.y, majorRadius);
				break;
			}

			case GEOMETRY_BOX:
			{
				doRectFill(pos.x, pos.y, majorRadius, minorRadius, angle);
				break;
			}
		};
	}

	Int currentCellIndexX, currentCellIndexY;
	ThePartitionManager->worldToCell( pos.x, pos.y, &currentCellIndexX, &currentCellIndexY );
	const PartitionCell *currentCell = ThePartitionManager->getCellAt( currentCellIndexX, currentCellIndexY );
	if(obj && currentCell != m_lastCell )
	{
		// To not expose PartitionCells, he will think in terms of points.  He will 
		// unlook at a point and look at the new point.  We do the rounding and the 
		// changing into PartitionCells
		obj->onPartitionCellChange(); 
		m_lastCell = currentCell;
	}

	// if we have moved, our shroudedness status might be different for all players,
	// so it must all be invalidated.
	invalidateShroudedStatusForAllPlayers();

#ifdef INTENSE_DEBUG
	for (Int i = 0; i < m_coiInUseCount; i++)
	{
		for (Int j = 0; j < i; j++)
		{
			if (m_coiArray[i].getCell() == m_coiArray[j].getCell())
			{
				DEBUG_CRASH(("dup cells in COI array, this is bad"));
			}
		}
	}
#endif

}

//-----------------------------------------------------------------------------
void PartitionData::invalidateShroudedStatusForPlayer(Int playerIndex) 
{ 
#ifndef DISABLE_INVALID_PREVENTION
	if (m_shroudedness[playerIndex] != OBJECTSHROUD_INVALID && m_shroudedness[playerIndex] != OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID)
#endif
		m_shroudedness[playerIndex] = OBJECTSHROUD_INVALID;
}

//-----------------------------------------------------------------------------
void PartitionData::invalidateShroudedStatusForAllPlayers() 
{ 
	for (Int i = 0; i < MAX_PLAYER_COUNT; i++) 
	{
		invalidateShroudedStatusForPlayer(i);
	}
}

//-----------------------------------------------------------------------------
Int PartitionData::calcMaxCoiForShape(GeometryType geom, Real majorRadius, Real minorRadius, Bool isSmall)
{
	Int result = 0;


  // THis is commented out, since some cases od big extets labeled small seem to be escaping.
  //M Lorenzen 8/26/03
//	if (isSmall)
//	{
//		#if defined(_DEBUG)
//		Int chk = calcMaxCoiForShape(geom, majorRadius, minorRadius, false);
//		DEBUG_ASSERTCRASH(chk <= 4, ("Small objects should be <= 4 cells, but I calced %s as %d\n",theObjName.str(),chk));
//		#endif
//		result = 4;
//	}
//	else
	{
		switch(geom)
		{
			case GEOMETRY_SPHERE:
			case GEOMETRY_CYLINDER:
			{
				// note that majorRadius is a radius, not a diameter.
				// this actually allocates a few too many, but that's ok.
				Int cells = ThePartitionManager->worldToCellDist(majorRadius*2) + 1;
				result = cells * cells;
				// The result only sizes the COI array (not saved, not a loop bound; m_coiInUseCount is used).
				break;
			}
			case GEOMETRY_BOX:
			{
				Real diagonal = static_cast<Real>(sqrtf(majorRadius*majorRadius + minorRadius*minorRadius));
				Int cells = ThePartitionManager->worldToCellDist(diagonal*2) + 1;
				result = cells * cells;
				break;
			}
		};
	}
	if (result < 4)
		result = 4;
	return result;
}

//-----------------------------------------------------------------------------
Int PartitionData::calcMaxCoiForObject()
{
	Object *obj = getObject();
	DEBUG_ASSERTCRASH(obj != nullptr, ("must be attached to an Object here 2"));
	
	GeometryType geom = obj->getGeometryInfo().getGeomType();
	Real majorRadius = obj->getGeometryInfo().getMajorRadius();
	Real minorRadius = obj->getGeometryInfo().getMinorRadius();
	Bool isSmall = obj->getGeometryInfo().getIsSmall();
	return calcMaxCoiForShape(geom, majorRadius, minorRadius, isSmall);
}

//-----------------------------------------------------------------------------
void PartitionData::makeDirty(Bool needToUpdateCells)
{
	//DEBUG_LOG(("makeDirty for pd %08lx obj %08lx\n",this,m_object));
	if (!ThePartitionManager->isInListDirtyModules(this))
	{
		if (needToUpdateCells)
			m_dirtyStatus = DirtyStatus::NEED_CELL_UPDATE_AND_COLLISION_CHECK;
		else if (m_dirtyStatus == DirtyStatus::NOT_DIRTY)
			m_dirtyStatus = DirtyStatus::NEED_COLLISION_CHECK;
		ThePartitionManager->prependToDirtyModules(this);
	}
	else if (needToUpdateCells)
	{
		// If I am in the list already, I may be in there for a half update, so if my
		// needs have increased I'll upgrade what I want.
		m_dirtyStatus = DirtyStatus::NEED_CELL_UPDATE_AND_COLLISION_CHECK;
	}
}

//-----------------------------------------------------------------------------
void PartitionData::allocCoiArray()
{
	DEBUG_ASSERTCRASH(m_coiArrayCount == 0 && m_coiArray == nullptr, ("hmm, coi should probably be null here"));
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
	m_coiArrayCount = calcMaxCoiForObject();
	m_coiArray = MSGNEW("PartitionManager_COI") CellAndObjectIntersection[m_coiArrayCount];	// may throw!
	m_coiInUseCount = 0;
	makeDirty(true);
}

//-----------------------------------------------------------------------------
void PartitionData::freeCoiArray()
{
	delete [] m_coiArray;	// yes, it's OK to call this on null...
	m_coiArray = nullptr;
	m_coiArrayCount = 0;
	m_coiInUseCount = 0;
	makeDirty(true);
}

//-----------------------------------------------------------------------------
void PartitionData::attachToObject(Object* object)
{

	// remember who contains us
	m_object = object;

	// we only snapshot things that are immobile and have something to draw.  Don't need ghostobjects for others.
	if (object->isKindOf(KINDOF_IMMOBILE))
	{	const ThingTemplate *tmplate=object->getTemplate();
		const ModuleInfo &drawMod=tmplate->getDrawModuleInfo();
		//Objects with default draw modules usually don't need ghostobjects so skip them to save memory.
		Bool makeGhostObject=TRUE;
		for (Int i=0; i<drawMod.getCount(); i++)
		{
			if (strcmp(drawMod.getNthName(i).str(),"W3DDefaultDraw") == 0)
			{	makeGhostObject=FALSE;
				break;
			}
		}
		if (makeGhostObject)
			m_ghostObject = TheGhostObjectManager->addGhostObject(object, this);
	}

	//DEBUG_LOG(("attach pd for pd %08lx obj %08lx\n",this,m_object));

	// (re)calc maxCoi and (re)alloc cois
	DEBUG_ASSERTCRASH(m_coiArrayCount == 0 && m_coiArray == nullptr, ("hmm, coi should probably be null here"));
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
	freeCoiArray();
	allocCoiArray();	// may throw!

	if (object)
		object->friend_setPartitionData(this);
	makeDirty(true);
}

//-----------------------------------------------------------------------------
void PartitionData::detachFromObject()
{
	// this is a little hokey... if we are in the midst of processing the contact
	// list, we have to ensure that any potential collisions get removed from that
	// list, since we are about to go bye-bye. ugly, but effective. (srj)
	//
	// note 2: we used to do this in our dtor, but it's too late then, since we require
	// our Object to still be present in order to figure out what to remove.
	// so do it here, since detaching it should nuke it from the obj list anyway. (srj)
	if (TheContactList)
		TheContactList->removeSpecificPartitionData(this);

	if (m_object)
		m_object->friend_setPartitionData(nullptr);
	removeAllTouchedCells();
	freeCoiArray();

	//DEBUG_LOG(("detach pd for pd %08lx obj %08lx\n",this,m_object));

	// no longer attached to object
	m_object = nullptr;
	TheGhostObjectManager->removeGhostObject(m_ghostObject);
	m_ghostObject = nullptr;
}

//-----------------------------------------------------------------------------
void PartitionData::attachToGhostObject(GhostObject* object)
{

	// remember who contains us
	m_object = nullptr;	//it's only attached to a ghost object, no parent object.
	m_ghostObject = object;

	// (re)calc maxCoi and (re)alloc cois
	DEBUG_ASSERTCRASH(m_coiArrayCount == 0 && m_coiArray == nullptr, ("hmm, coi should probably be null here"));
	DEBUG_ASSERTCRASH(m_coiInUseCount == 0, ("hmm, coi count mismatch"));
	freeCoiArray();

	m_coiArrayCount = calcMaxCoiForShape(object->getGeometryType(), object->getGeometryMajorRadius(), object->getGeometryMinorRadius(),object->getGeometrySmall());
	m_coiArray = MSGNEW("PartitionManager_COI") CellAndObjectIntersection[m_coiArrayCount];	// may throw!
	m_coiInUseCount = 0;
	makeDirty(true);

	if (m_ghostObject)
		m_ghostObject->updateParentObject(nullptr,this);
}

//-----------------------------------------------------------------------------
void PartitionData::detachFromGhostObject(void)
{
	// this is a little hokey... if we are in the midst of processing the contact
	// list, we have to ensure that any potential collisions get removed from that
	// list, since we are about to go bye-bye. ugly, but effective. (srj)
	//
	// note 2: we used to do this in our dtor, but it's too late then, since we require
	// our Object to still be present in order to figure out what to remove.
	// so do it here, since detaching it should nuke it from the obj list anyway. (srj)
	if (TheContactList)
		TheContactList->removeSpecificPartitionData(this);

	removeAllTouchedCells();
	freeCoiArray();

	//DEBUG_LOG(("detach pd for pd %08lx obj %08lx\n",this,m_object));

	// no longer attached to object
	m_object = nullptr;
	m_ghostObject = nullptr;
}
