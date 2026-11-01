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

// Partition cells, cell/object intersections and the outward cell iterator

#ifdef FASTER_GCO
// nothing
#else
//-----------------------------------------------------------------------------
CellOutwardIterator::CellOutwardIterator(PartitionManager *mgr, Int x, Int y)
{
	m_mgr = mgr;
	m_cellCenterX = x;
	m_cellCenterY = y;
	m_cellRadius = 0;
	m_maxRadius = maxInt(maxInt(x, mgr->getCellCountX() - x), maxInt(y, mgr->getCellCountY() - y));
	m_delta[0] = m_delta[1] = 0;
	m_inc = -1;
	m_cnt = 0;
	m_axis = 1;
	m_iter = 1;
	m_delta[m_axis] -= m_inc;
}

//-----------------------------------------------------------------------------
CellOutwardIterator::~CellOutwardIterator()
{
}

//-----------------------------------------------------------------------------
PartitionCell *CellOutwardIterator::nextCell(Bool skipEmpties)
{
	while (m_cellRadius <= m_maxRadius)
	{

		if (m_iter > 0)
		{
			m_delta[m_axis] += m_inc;
			--m_iter;
			PartitionCell *cell = m_mgr->getCellAt(m_cellCenterX + m_delta[0], m_cellCenterY + m_delta[1]);
			if (!cell)
				continue;
			if (skipEmpties && !cell->getFirstCoiInCell())
				continue;
			return cell;
		}

		m_axis += 1;
		if (m_axis >= 2)
		{
			m_axis = 0;
			m_inc = -m_inc;
			m_cnt += 1;
			m_cellRadius = maxInt(absInt(m_delta[0]), absInt(m_delta[1]));
		}

		m_iter = m_cnt;
	}

	return nullptr;
}
#endif

//-----------------------------------------------------------------------------
CellAndObjectIntersection::CellAndObjectIntersection()
{
	m_cell = nullptr;
	m_module = nullptr;
	m_prevCoi = nullptr;
	m_nextCoi = nullptr;
}

//-----------------------------------------------------------------------------
CellAndObjectIntersection::~CellAndObjectIntersection()
{
	DEBUG_ASSERTCRASH(m_prevCoi == nullptr && m_nextCoi == nullptr, ("destroying a linked COI"));
	DEBUG_ASSERTCRASH(!getModule(), ("destroying an in-use COI"));
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::friend_addToCellList(CellAndObjectIntersection **pListHead)
{
	DEBUG_ASSERTCRASH(m_prevCoi == nullptr && m_nextCoi == nullptr && *pListHead != this, ("trying to add a cell to list, but it appears to already be in a list"));
	
	this->m_nextCoi = *pListHead;
	if (*pListHead)
		(*pListHead)->m_prevCoi = this;
	*pListHead = this;
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::friend_removeFromCellList(CellAndObjectIntersection **pListHead)
{
#define DEBUG_ASSERTINLIST(c) \
	DEBUG_ASSERTCRASH((c)->m_prevCoi != nullptr || (c)->m_nextCoi != nullptr || *pListHead == (c), ("cell is not in list"));

	DEBUG_ASSERTINLIST(this);

	if (this->m_prevCoi)
	{
		DEBUG_ASSERTINLIST(this->m_prevCoi);
		DEBUG_ASSERTCRASH(*pListHead != this, ("bad linkage"));
		this->m_prevCoi->m_nextCoi = this->m_nextCoi;
	}
	else
	{
		DEBUG_ASSERTCRASH(*pListHead == this, ("bad linkage"));
		*pListHead = this->m_nextCoi;
	}

	if (this->m_nextCoi)
	{
		DEBUG_ASSERTINLIST(this->m_nextCoi);
		this->m_nextCoi->m_prevCoi = this->m_prevCoi;
	}

	this->m_prevCoi = nullptr;
	this->m_nextCoi = nullptr;

#undef DEBUG_ASSERTINLIST
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::addCoverage(PartitionCell *cell, PartitionData *module)
{
	DEBUG_ASSERTCRASH(m_cell == nullptr || m_cell == cell, ("mismatch"));
	DEBUG_ASSERTCRASH(m_module == nullptr || m_module == module, ("mismatch"));

	if (m_module != nullptr && m_module != module)
	{
		DEBUG_CRASH(("COI already in use by another module!"));
		return;
	}

	if (m_cell == nullptr)
		cell->friend_addToCellList(this);

	m_cell = cell;
	m_module = module;
}

//-----------------------------------------------------------------------------
void CellAndObjectIntersection::removeAllCoverage()
{
	if (m_module == nullptr)
	{
		DEBUG_CRASH(("COI not in use"));
		return;
	}

	m_cell->friend_removeFromCellList(this);
	m_cell = nullptr;
	m_module = nullptr;
}

//-----------------------------------------------------------------------------
PartitionCell::PartitionCell()
{
	//Added By Sadullah Nader
	//Initializations inserted
	m_cellX = m_cellY = 0;
	//
	m_firstCoiInCell = nullptr;
	m_coiCount = 0;
#ifdef PM_CACHE_TERRAIN_HEIGHT
	m_loTerrainZ = HUGE_DIST;		// huge positive
	m_hiTerrainZ = -HUGE_DIST;	// huge negative
#endif
	/*
		You may be asking yourself: why do we model the shroud for all players,
		rather than just the local player? And the answer is: because this allows
		us to checksum these values for net games, to help prevent "shroud cheaters"
		(who use a trainer to disable the shroud on their system).
	*/
	for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		// Default is "passive shroud".  1,0.
		m_shroudLevel[i].m_currentShroud = 1;
		m_shroudLevel[i].m_activeShroudLevel = 0;

		// default cash value is 0
		m_cashValue[i] = 0;

		// default threat value is 0
		m_threatValue[i] = 0;
	}
}

//-----------------------------------------------------------------------------
PartitionCell::~PartitionCell()
{
	DEBUG_ASSERTCRASH(m_firstCoiInCell == nullptr && m_coiCount == 0, ("destroying a nonempty PartitionCell"));
	// but don't destroy the Cois; they don't belong to us
}

//-----------------------------------------------------------------------------
void PartitionCell::invalidateShroudedStatusForAllCois(Int playerIndex)
{
	for (CellAndObjectIntersection* coi = m_firstCoiInCell; coi; coi = coi->getNextCoi())
	{
		coi->getModule()->invalidateShroudedStatusForPlayer(playerIndex);
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::addLooker(Int playerIndex)
{
	CellShroudStatus oldShroud = getShroudStatusForPlayer( playerIndex );
	// The decreasing Algorithm: A 1 will go straight to -1, otherwise it just gets decremented
	m_shroudLevel[playerIndex].m_currentShroud = static_cast<Short>(min( m_shroudLevel[playerIndex].m_currentShroud - 1, -1 ));

	CellShroudStatus newShroud = getShroudStatusForPlayer( playerIndex );

//	DEBUG_LOG(( "ADD    %d, %d.  CS = %d, AS = %d for player %d.\n", 
//							m_cellX, 
//							m_cellY, 
//							m_shroudLevel[playerIndex].m_currentShroud,
//							m_shroudLevel[playerIndex].m_activeShroudLevel,
//							playerIndex
//							));

	if( oldShroud != newShroud )
	{
		// On an edge trigger, tell all objects to think about their shroudedness
		invalidateShroudedStatusForAllCois( playerIndex );

		if( playerIndex == ThePlayerList->getLocalPlayer()->getPlayerIndex() )
		{
			// and if this is the local player, do the Client update.
			TheLogicToClient->shroudSetLevel(m_cellX, m_cellY, newShroud);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeLooker(Int playerIndex)
{
	CellShroudStatus oldShroud = getShroudStatusForPlayer( playerIndex );
	// the increasing Algorithm: a -1 goes up to min(1,activeLevel), otherwise it just gets incremented
	if( m_shroudLevel[playerIndex].m_currentShroud == -1 )
		m_shroudLevel[playerIndex].m_currentShroud = min( m_shroudLevel[playerIndex].m_activeShroudLevel, static_cast<Short>(1) );
	else
	{
		DEBUG_ASSERTCRASH( m_shroudLevel[playerIndex].m_currentShroud < 0, ("Someone is RemoveLooker-ing on a cell that is not looked at.  This will make a permanent shroud blob.") );
		m_shroudLevel[playerIndex].m_currentShroud++;
	}
	CellShroudStatus newShroud = getShroudStatusForPlayer( playerIndex );

//	DEBUG_LOG(( "REMOVE %d, %d.  CS = %d, AS = %d for player %d.\n", 
//							m_cellX, 
//							m_cellY, 
//							m_shroudLevel[playerIndex].m_currentShroud,
//							m_shroudLevel[playerIndex].m_activeShroudLevel,
//							playerIndex
//							));

	if( oldShroud != newShroud )
	{
		// On an edge trigger, tell all objects to think about their shroudedness
		invalidateShroudedStatusForAllCois( playerIndex );

		if( playerIndex == ThePlayerList->getLocalPlayer()->getPlayerIndex() )
		{
			// and if this is the local player, do the Client update.
			TheLogicToClient->shroudSetLevel(m_cellX, m_cellY, newShroud);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::addShrouder( Int playerIndex )
{
	CellShroudStatus oldShroud = getShroudStatusForPlayer( playerIndex );
	// Increasing active shroud: activeLevel gets incremented, and CS is set to 1 if at zero
	// do the algorithm
	m_shroudLevel[playerIndex].m_activeShroudLevel++;
	if( m_shroudLevel[playerIndex].m_currentShroud == 0 )
	{
		m_shroudLevel[playerIndex].m_currentShroud = 1;
	}
	CellShroudStatus newShroud = getShroudStatusForPlayer( playerIndex );

	if( oldShroud != newShroud )
	{
		// On an edge trigger, tell all objects to think about their shroudedness
		invalidateShroudedStatusForAllCois( playerIndex );

		// and update the client if we are on the local player
		if( playerIndex == ThePlayerList->getLocalPlayer()->getPlayerIndex() )
		{
			TheLogicToClient->shroudSetLevel(m_cellX, m_cellY, newShroud);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeShrouder( Int playerIndex )
{
	// Decreasing active shroud: just decrement activeLevel.  This will never result in a client change.
	// Either it was passive shroud and is now active, or it was being looked at and still is.
	m_shroudLevel[playerIndex].m_activeShroudLevel--;
	DEBUG_ASSERTCRASH( m_shroudLevel[playerIndex].m_activeShroudLevel >= 0, ("Shroud generation has gone negative.  This can't happen.") );
}

//-----------------------------------------------------------------------------
CellShroudStatus PartitionCell::getShroudStatusForPlayer( Int playerIndex ) const
{
	// There are now three answers, but the question still requires "to whom"

	if( m_shroudLevel[playerIndex].m_currentShroud == 1 )
		return CellShroudStatus::CELLSHROUD_SHROUDED;
	else if( m_shroudLevel[playerIndex].m_currentShroud == 0 )
		return CellShroudStatus::CELLSHROUD_FOGGED;// ie Nobody actively looking
	else
		return CellShroudStatus::CELLSHROUD_CLEAR;
}

//-----------------------------------------------------------------------------
UnsignedInt PartitionCell::getThreatValue( Int playerIndex )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
		return m_threatValue[playerIndex];
	}
	return 0;
}

//-----------------------------------------------------------------------------
void PartitionCell::addThreatValue( Int playerIndex, UnsignedInt threatValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef _DEBUG
		UnsignedInt oldThreatVal = m_threatValue[playerIndex];
		DEBUG_ASSERTCRASH(oldThreatVal <= oldThreatVal + threatValue, ("adding new threat value overflowed allotted storage."));
#endif
		m_threatValue[playerIndex] += threatValue;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeThreatValue( Int playerIndex, UnsignedInt threatValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef _DEBUG
		UnsignedInt oldThreatVal = m_threatValue[playerIndex];
		DEBUG_ASSERTCRASH(oldThreatVal >= oldThreatVal - threatValue, ("removing new threat value underflowed allotted storage."));
#endif
		m_threatValue[playerIndex] -= threatValue;
	}
}

//-----------------------------------------------------------------------------
UnsignedInt PartitionCell::getCashValue( Int playerIndex )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
		return m_cashValue[playerIndex];
	}
	return 0;
}

//-----------------------------------------------------------------------------
void PartitionCell::addCashValue( Int playerIndex, UnsignedInt cashValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef _DEBUG
		UnsignedInt oldCashVal = m_cashValue[playerIndex];
		DEBUG_ASSERTCRASH(oldCashVal <= oldCashVal + cashValue, ("adding new cash value overflowed allotted storage."));
#endif
		m_cashValue[playerIndex] += cashValue;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::removeCashValue( Int playerIndex, UnsignedInt cashValue )
{
	if (playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT) {
#ifdef _DEBUG
		UnsignedInt oldCashVal = m_cashValue[playerIndex];
		DEBUG_ASSERTCRASH(oldCashVal >= oldCashVal - cashValue, ("removing new cash value underflowed allotted storage."));
#endif
		m_cashValue[playerIndex] -= cashValue;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::friend_addToCellList(CellAndObjectIntersection *coi)
{
	if (coi)
	{
		coi->friend_addToCellList(&m_firstCoiInCell);
		++m_coiCount;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::friend_removeFromCellList(CellAndObjectIntersection *coi)
{
	if (coi)
	{
		coi->friend_removeFromCellList(&m_firstCoiInCell);
		--m_coiCount;
	}
}

//-----------------------------------------------------------------------------
void PartitionCell::getCellCenterPos(Real& x, Real& y)
{
	ThePartitionManager->getCellCenterPos(m_cellX, m_cellY, x, y);
}

//-----------------------------------------------------------------------------
#ifdef _DEBUG
void PartitionCell::validateCoiList()
{
	CellAndObjectIntersection *nextCoi = 0, *prevCoi = 0;
	for (CellAndObjectIntersection *coi = getFirstCoiInCell(); coi; prevCoi = coi, coi = nextCoi)
	{
		nextCoi = coi->getNextCoi();
		DEBUG_ASSERTCRASH(coi->getPrevCoi() == prevCoi, ("coi link mismatch"));
		DEBUG_ASSERTCRASH(prevCoi == nullptr || prevCoi->getNextCoi() == coi, ("coi link mismatch"));
		DEBUG_ASSERTCRASH((coi == getFirstCoiInCell()) == (prevCoi == nullptr) , ("coi link mismatch"));
		DEBUG_ASSERTCRASH(nextCoi == nullptr || nextCoi->getPrevCoi() == coi, ("coi link mismatch"));
	}
}
#endif

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PartitionCell::crc( Xfer *xfer )
{

	xfer->xferUser(&m_shroudLevel, sizeof(ShroudLevel) * MAX_PLAYER_COUNT);
	xfer->xferUser(&m_cellX, sizeof(m_cellX));
	xfer->xferUser(&m_cellY, sizeof(m_cellY));

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void PartitionCell::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// xfer shroud data
	xfer->xferUser( &m_shroudLevel, sizeof( ShroudLevel ) * MAX_PLAYER_COUNT );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PartitionCell::loadPostProcess( void )
{

}  // end loadPostProcess
