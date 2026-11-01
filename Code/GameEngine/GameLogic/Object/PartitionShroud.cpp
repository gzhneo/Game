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

// forward declarations: defined further down, used before
void hLineAddLooker(Int x1, Int x2, Int y, void *parms);
void hLineRemoveLooker(Int x1, Int x2, Int y, void *parms);
void hLineAddShrouder(Int x1, Int x2, Int y, void *parms);
void hLineRemoveShrouder(Int x1, Int x2, Int y, void *parms);
void hLineAddThreat(Int x1, Int x2, Int y, void *parms);
void hLineRemoveThreat(Int x1, Int x2, Int y, void *parms);
void hLineAddValue(Int x1, Int x2, Int y, void *parms);
void hLineRemoveValue(Int x1, Int x2, Int y, void *parms);

// Shroud, reveal, threat and value maps per player

//-----------------------------------------------------------------------------
//         Local Types                                                      
//-----------------------------------------------------------------------------
struct ThreatValueParms
{
	Int playerIndex;
	Real xCenter;
	Real yCenter;
	Real radius;
	UnsignedInt threatOrValue;
};

struct CellValueProcParms
{
	Int valueRequired;
	Bool greaterThan;
	ValueOrThreat valueType;
	PlayerMaskType allPlayersMask[MAX_PLAYER_COUNT];
	PlayerMaskType allowedPlayersMasks;
};

static int cellValueProc(PartitionCell* cell, void* userData);

/** 
	Reveals the map for the given player, but does not override Shroud generation.  (Script)
*/
void PartitionManager::revealMapForPlayer( Int playerIndex )
{
	// By looking and then stopping on every cell, I clear all Passive Shroud
	// By adding a looker directly I don't hit the Ally logic of the normal look/doShroudReveal
	for (int i = 0; i < m_totalCellCount; ++i) 
	{
		m_cells[i].addLooker( playerIndex );
		m_cells[i].removeLooker( playerIndex );
	}
}

/** 
	Reveals the map for the given player, AND permanently disables all Shroud generation (Observer Mode).
	*/
void PartitionManager::revealMapForPlayerPermanently( Int playerIndex )
{
	// By skipping the removeLooker, I consider myself as actively looking at everything, 
	// so Shroud generation will no longer function
	// By adding a looker directly I don't hit the Ally logic of the normal look/doShroudReveal
	for (int i = 0; i < m_totalCellCount; ++i) 
	{
		m_cells[i].addLooker( playerIndex );
	}
}

/** 
		Adds a layer of permanent blindness.  Used solely to undo the permanent reveal for debugging
	*/
void PartitionManager::undoRevealMapForPlayerPermanently( Int playerIndex )
{
	//First make sure no lingering looks will leave holes when they aren't wanted.
	processEntirePendingUndoShroudRevealQueue();

	// This will have amusing consequences if done without a preceding revealMapForPlayerPermanently.
	// Everything you own can become shrouded.
	for (int i = 0; i < m_totalCellCount; ++i) 
	{
		m_cells[i].removeLooker( playerIndex );
	}
}

/** 
	Resets the shroud for the given player with passive shroud (can re-explore).
	*/
void PartitionManager::shroudMapForPlayer( Int playerIndex )
{
	//First make sure no lingering looks will leave holes when they aren't wanted.
	processEntirePendingUndoShroudRevealQueue();

	// By pulsing a blast of shroud like this, we will set everything not actively looked at as Passive Shroud
	for (int i = 0; i < m_totalCellCount; ++i) 
	{
		m_cells[i].addShrouder( playerIndex );
		m_cells[i].removeShrouder( playerIndex );
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::refreshShroudForLocalPlayer()
{
	// This is a drawing refresh only, and so is allowed to use the Local Player.
	TheLogicToClient->shroudClear();

	Int playerIndex = ThePlayerList->getLocalPlayer()->getPlayerIndex();
	for (int i = 0; i < m_totalCellCount; ++i)
	{
		Int x = m_cells[i].getCellX();
		Int y = m_cells[i].getCellY();
		CellShroudStatus status = m_cells[i].getShroudStatusForPlayer(playerIndex);
		TheLogicToClient->shroudSetLevel(x, y, status);
		m_cells[i].invalidateShroudedStatusForAllCois(playerIndex);
	}
}

//-----------------------------------------------------------------------------
CellShroudStatus PartitionManager::getShroudStatusForPlayer(Int playerIndex, Int x, Int y) const
{
	if( playerIndex < 0 )
		return CellShroudStatus::CELLSHROUD_SHROUDED;// Safety.  There are no Negative players, but PlayerIndex is typedef'd to Int, not UnsignedInt

	const PartitionCell* cell = getCellAt(x, y);
	return cell ? cell->getShroudStatusForPlayer(playerIndex) : CellShroudStatus::CELLSHROUD_SHROUDED;
}

//-----------------------------------------------------------------------------
CellShroudStatus PartitionManager::getShroudStatusForPlayer(Int playerIndex, const Coord3D *loc ) const
{
	Int x, y;

	ThePartitionManager->worldToCell( loc->x, loc->y, &x, &y );

	return getShroudStatusForPlayer( playerIndex, x, y );
}

//-----------------------------------------------------------------------------
ObjectShroudStatus PartitionManager::getPropShroudStatusForPlayer(Int playerIndex, const Coord3D *loc ) const
{
	Int x, y;

	ThePartitionManager->worldToCell( loc->x - m_cellSize*0.5f, loc->y - m_cellSize*0.5f, &x, &y );

	CellShroudStatus cellStat = getShroudStatusForPlayer( playerIndex, x, y );
	if (cellStat != getShroudStatusForPlayer( playerIndex, x+1, y )) {
		return OBJECTSHROUD_PARTIAL_CLEAR;
	}
	if (cellStat != getShroudStatusForPlayer( playerIndex, x+1, y+1 )) {
		return OBJECTSHROUD_PARTIAL_CLEAR;
	}
	if (cellStat != getShroudStatusForPlayer( playerIndex, x, y+1 )) {
		return OBJECTSHROUD_PARTIAL_CLEAR;
	}
	if (cellStat == CellShroudStatus::CELLSHROUD_SHROUDED) {
		return OBJECTSHROUD_SHROUDED;
	}
	if (cellStat == CellShroudStatus::CELLSHROUD_CLEAR) {
		return OBJECTSHROUD_CLEAR;
	}
	return OBJECTSHROUD_FOGGED;
}

//-----------------------------------------------------------------------------
// This is the main accessor of the shroud system.  At this level, allies are taken
// into consideration as specified by the caller.  Look/Unlook are the ones sending Ally info, as that
// is in Object where Allies make sense.  AddLooker literally just adds a looker for the player you specify.
// This way, Full map reveals and Observer mode active look will not carry over to all 
// allies.  They'll use the RevealWholeDamnMap series, which call addLooker directly.
void PartitionManager::doShroudReveal(Real centerX, Real centerY, Real radius, PlayerMaskType playerMask) 
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		// Object's Look is the one who knows about allies.  Anyone can pask a player mask to me and all
		// of those players will have an active looker applied to a bunch of cells
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineAddLooker, reinterpret_cast<void*>(static_cast<intptr_t>(currentIndex)));
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::processPendingUndoShroudRevealQueue( Bool considerTimestamp )
{
	//Keep going until the front one is in the future.  UndoShroudReveal on each one you process.
	//Again, you know these are in order, because we control the adding of the Constant in-the-queue time.
	UnsignedInt compareTime;
	if(considerTimestamp)
		compareTime = TheGameLogic->getFrame();
	else
		compareTime = UINT_MAX;

	while( !m_pendingUndoShroudReveals.empty() && (m_pendingUndoShroudReveals.front()->m_data < compareTime) )
	{
		SightingInfo *thisInfo = m_pendingUndoShroudReveals.front();

		undoShroudReveal( thisInfo->m_where.x, thisInfo->m_where.y, thisInfo->m_howFar, thisInfo->m_forWhom );

		deleteInstanceOf(thisInfo);
		m_pendingUndoShroudReveals.pop();
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::processEntirePendingUndoShroudRevealQueue()
{
	// Something major is about to happen, so we need to rush through our queue and get everything in order.
	// Examples are map resize, full map undo-reveal, stuff like that.

	// Don't like duplicating code, so route to original function.
	processPendingUndoShroudRevealQueue(FALSE);
}

//-----------------------------------------------------------------------------
void PartitionManager::resetPendingUndoShroudRevealQueue()
{
	while( !m_pendingUndoShroudReveals.empty() )
	{
		SightingInfo *thisInfo = m_pendingUndoShroudReveals.front();
		deleteInstanceOf(thisInfo);
		m_pendingUndoShroudReveals.pop();
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoShroudReveal(Real centerX, Real centerY, Real radius, PlayerMaskType playerMask) 
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineRemoveLooker, reinterpret_cast<void*>(static_cast<intptr_t>(currentIndex)));
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::queueUndoShroudReveal(Real centerX, Real centerY, Real radius, PlayerMaskType playerMask) 
{
	UnsignedInt now = TheGameLogic->getFrame();
	SightingInfo *newInfo = newInstance(SightingInfo);

	newInfo->m_where.x = centerX;
	newInfo->m_where.y = centerY;
	newInfo->m_howFar = radius;
	newInfo->m_forWhom = playerMask;
	newInfo->m_data = now + TheGlobalData->m_unlookPersistDuration;

	m_pendingUndoShroudReveals.push(newInfo);
}

//-----------------------------------------------------------------------------
void PartitionManager::doShroudCover(Real centerX, Real centerY, Real radius, PlayerMaskType playerMask) 
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		// Object's Shroud is the one who knows about allies.  Anyone can pask a player mask to me and all
		// of those players will have an active shrouder applied to a bunch of cells
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineAddShrouder, reinterpret_cast<void*>(static_cast<intptr_t>(currentIndex)));
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoShroudCover(Real centerX, Real centerY, Real radius, PlayerMaskType playerMask) 
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			circle.drawCircle(hLineRemoveShrouder, reinterpret_cast<void*>(static_cast<intptr_t>(currentIndex)));
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::doThreatAffect( Real centerX, Real centerY, Real radius, UnsignedInt threatVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = threatVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineAddThreat, &parms);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoThreatAffect( Real centerX, Real centerY, Real radius, UnsignedInt threatVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = threatVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineRemoveThreat, &parms);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::doValueAffect( Real centerX, Real centerY, Real radius, UnsignedInt valueVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = valueVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;

	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineAddValue, &parms);
		}
	}
}

//-----------------------------------------------------------------------------
void PartitionManager::undoValueAffect( Real centerX, Real centerY, Real radius, UnsignedInt valueVal, PlayerMaskType playerMask)
{
	Int cellCenterX, cellCenterY;
	ThePartitionManager->worldToCell(centerX, centerY, &cellCenterX, &cellCenterY);
	Real fCellCenterX = INT_TO_REAL(cellCenterX);
	Real fCellCenterY = INT_TO_REAL(cellCenterY);

	Int cellRadius = ThePartitionManager->worldToCellDist(radius);
	if (cellRadius < 1) 
		cellRadius = 1;

	Real fCellRadius = INT_TO_REAL(cellRadius + 1);

	DiscreteCircle circle(cellCenterX, cellCenterY, cellRadius);

	ThreatValueParms parms;
	parms.radius = fCellRadius;
	parms.threatOrValue = valueVal;
	parms.xCenter = fCellCenterX;
	parms.yCenter = fCellCenterY;
	
	for( Int currentIndex = ThePlayerList->getPlayerCount() - 1; currentIndex >=0; currentIndex-- )
	{
		const Player *currentPlayer = ThePlayerList->getNthPlayer( currentIndex );
		if( BitTest( playerMask, currentPlayer->getPlayerMask() ) )
		{
			parms.playerIndex = currentIndex;
			circle.drawCircle(hLineRemoveValue, &parms);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::getMostValuableLocation( Int playerIndex, UnsignedInt whichPlayerTypes, ValueOrThreat valType, Coord3D *outLocation )
{
	if (!outLocation)
		return;

	PlayerMaskType playerMask = ThePlayerList->getPlayersWithRelationship(playerIndex, whichPlayerTypes);
	if (playerMask == 0)
		return;

	Int cellCount = m_cellCountX * m_cellCountY;
	
	PlayerMaskType allPlayerMasks[MAX_PLAYER_COUNT] = { 0 };
	Int totalPlayerCount = ThePlayerList->getPlayerCount();
	
	Int i;
	for (i = 0; i < totalPlayerCount; ++i) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (!player) {
			continue;
		}

		allPlayerMasks[i] = player->getPlayerMask();
	}

	Int greatestValueCell = -1;
	Int maxCellValue = -1;
	for (i = 0; i < cellCount; ++i) {
		Int cellValue = 0;

		for (Int player = 0; player < MAX_PLAYER_COUNT; ++player) {
			if (BitTest(allPlayerMasks[player], playerMask)) {
				if (valType == ValueOrThreat::VOT_CashValue) {
					cellValue += m_cells[i].getCashValue(player);
				} else {
					cellValue += m_cells[i].getThreatValue(player);
				}
			}
		}

		if (cellValue > maxCellValue) {
			maxCellValue = cellValue;
			greatestValueCell = i;
		}
	}

	if (greatestValueCell == -1 || maxCellValue == -1) {
		DEBUG_CRASH(("PartitionManager::getMostValuableLocation: jkmcd"));
		return;
	}

	outLocation->set(m_cells[greatestValueCell].getCellX() * TheGlobalData->m_partitionCellSize,
									 m_cells[greatestValueCell].getCellY() * TheGlobalData->m_partitionCellSize,
									 0
									);
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::getNearestGroupWithValue( Int playerIndex, UnsignedInt whichPlayerTypes, ValueOrThreat valType,
															 const Coord3D *sourceLocation, Int valueRequired, Bool greaterThan, Coord3D *outLocation )
{
	if (!(sourceLocation && outLocation))
		return;

	PlayerMaskType playerMask = ThePlayerList->getPlayersWithRelationship(playerIndex, whichPlayerTypes);
	if (playerMask == 0)
		return;
	
	PlayerMaskType allPlayerMasks[MAX_PLAYER_COUNT] = { 0 };
	Int totalPlayerCount = ThePlayerList->getPlayerCount();
	
	Int i;
	for (i = 0; i < totalPlayerCount; ++i) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (!player) {
			continue;
		}

		allPlayerMasks[i] = player->getPlayerMask();
	}

	CellValueProcParms parms;
	parms.valueRequired = valueRequired;
	parms.greaterThan = valueRequired;
	parms.valueType = valType;
	parms.allowedPlayersMasks = playerMask;
	for (i = 0; i < MAX_PLAYER_COUNT; ++i) 
		parms.allPlayersMask[i] = allPlayerMasks[i];
	
	Int nearestGreat = iterateCellsBreadthFirst(sourceLocation, cellValueProc, &parms);
	if (nearestGreat != -1) {
		(*outLocation).x = m_cells[nearestGreat].getCellX() * TheGlobalData->m_partitionCellSize;
		(*outLocation).y = m_cells[nearestGreat].getCellY() * TheGlobalData->m_partitionCellSize;
		(*outLocation).z = 0;
	}

	// all done
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::storeFoggedCells(ShroudStatusStoreRestore &outPartitionStore, Bool storeToFog) const
{
	Int i, j, p;

	if (storeToFog) {
		// This is the first pass
		outPartitionStore.m_cellsWide = m_cellCountX;
		
		// Resize all of our arrays.
		for (p = 0; p < MAX_PLAYER_COUNT; ++p) {
			outPartitionStore.m_foggedOrRevealed[p].resize(m_totalCellCount);
			for (i = 0; i < m_totalCellCount; ++i) {
				outPartitionStore.m_foggedOrRevealed[p][i] = STORE_DONTTOUCH;
			}
		}
	}

	for (p = 0; p < MAX_PLAYER_COUNT; ++p) {
		if (outPartitionStore.m_foggedOrRevealed[p].size() != m_totalCellCount) {
			DEBUG_CRASH(("PartitionManager::storeFoggedCells: jkmcd - x36872"));
			continue;
		}

		for (j = 0; j < m_cellCountY; ++j) {
			for (i = 0; i < m_cellCountX; ++i) {
				UnsignedByte &byteToWrite = outPartitionStore.m_foggedOrRevealed[p][j * m_cellCountX + i]; 

				if (storeToFog && m_cells[j * m_cellCountX + i].getShroudStatusForPlayer(p) == CellShroudStatus::CELLSHROUD_FOGGED) {
					byteToWrite = STORE_FOG;
				}
					
				if (!storeToFog && m_cells[j * m_cellCountX + i].getShroudStatusForPlayer(p) == CellShroudStatus::CELLSHROUD_CLEAR) {
					byteToWrite = STORE_PERMANENTLY_REVEALED;
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void PartitionManager::restoreFoggedCells(const ShroudStatusStoreRestore &inPartitionStore, Bool restoreToFog)
{
	Int i, j, p;

	Int storeWidth = inPartitionStore.m_cellsWide;
	Int storeHeight = static_cast<Int>(inPartitionStore.m_foggedOrRevealed[0].size()) / storeWidth;

	for (p = 0; p < MAX_PLAYER_COUNT; ++p) {
		for (j = 0; j < storeHeight; ++j) {
			if (j >= m_cellCountY) {
				// All done.
				return;
			}

			for (i = 0; i < storeWidth; ++i) {
				if (i >= m_cellCountX) {
					// Skip to the next line. This info will be thrown away.
					break;
				}

				UnsignedByte byteToRestore = inPartitionStore.m_foggedOrRevealed[p][j * storeWidth + i];
				if (byteToRestore == STORE_DONTTOUCH) {
					continue;
				}

				if (byteToRestore == STORE_FOG && restoreToFog) {
					// restore the fog status of this cell
					m_cells[j * m_cellCountX + i].addLooker(p);
					m_cells[j * m_cellCountX + i].removeLooker(p);
				}

				if (byteToRestore == STORE_PERMANENTLY_REVEALED && !restoreToFog) {
					// Add an extra looker.
					m_cells[j * m_cellCountX + i].addLooker(p);
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
static int cellValueProc(PartitionCell* cell, void* userData)
{
	CellValueProcParms *parms = static_cast<CellValueProcParms*>(userData);

	UnsignedInt val = 0;
	for (Int i = 0; i < MAX_PLAYER_COUNT; ++i) {
		if (BitTest(parms->allowedPlayersMasks, parms->allPlayersMask[i])) {
			if (parms->valueType == ValueOrThreat::VOT_CashValue) {
				val += cell->getCashValue(i);
			} else {
				val += cell->getThreatValue(i);
			}
		}
	}

	if ((val > static_cast<UnsignedInt>(parms->valueRequired) && parms->greaterThan) || 
			(val < static_cast<UnsignedInt>(parms->valueRequired) && !parms->greaterThan)) {
		return 1;
	}

	return 0;
}

// -----------------------------------------------------------------------------
void hLineAddLooker(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	Int playerIndex = static_cast<Int>(reinterpret_cast<intptr_t>(playerIndexVoid));

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->addLooker(playerIndex);
	}
}

// -----------------------------------------------------------------------------
void hLineRemoveLooker(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	Int playerIndex = static_cast<Int>(reinterpret_cast<intptr_t>(playerIndexVoid));

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->removeLooker(playerIndex);
	}
}

// -----------------------------------------------------------------------------
void hLineAddShrouder(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	Int playerIndex = static_cast<Int>(reinterpret_cast<intptr_t>(playerIndexVoid));

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->addShrouder( playerIndex );
	}
}

// -----------------------------------------------------------------------------
void hLineRemoveShrouder(Int x1, Int x2, Int y, void *playerIndexVoid)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	Int playerIndex = static_cast<Int>(reinterpret_cast<intptr_t>(playerIndexVoid));

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;
		cell->removeShrouder( playerIndex );
	}
}

// -----------------------------------------------------------------------------
void hLineAddThreat(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = static_cast<ThreatValueParms*>(threatValueParms);

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = static_cast<Real>(sqrt( pow(x - parms->xCenter, 2) + pow(y - parms->yCenter, 2) ));
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f) 
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;

		cell->addThreatValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}

// -----------------------------------------------------------------------------
void hLineRemoveThreat(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = static_cast<ThreatValueParms*>(threatValueParms);

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = static_cast<Real>(sqrt( pow(x - parms->xCenter, 2) + pow(y - parms->yCenter, 2) ));
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f) 
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;
		
		cell->removeThreatValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}

// -----------------------------------------------------------------------------
void hLineAddValue(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = static_cast<ThreatValueParms*>(threatValueParms);

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = static_cast<Real>(sqrt( pow(x - parms->xCenter, 2) + pow(y - parms->yCenter, 2) ));
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f) 
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;
		
		cell->addCashValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}

// -----------------------------------------------------------------------------
void hLineRemoveValue(Int x1, Int x2, Int y, void *threatValueParms)
{
	if (y < 0 || y >= ThePartitionManager->m_cellCountY || x1 >= ThePartitionManager->m_cellCountX || x2 < 0)
		return;

	ThreatValueParms *parms = static_cast<ThreatValueParms*>(threatValueParms);

	Real distance;
	Real mulVal = 1.0f;

	PartitionCell* cell = &ThePartitionManager->m_cells[y * ThePartitionManager->m_cellCountX + x1];	// yes, this could be invalid. we'll skip the bad ones.
	for (Int x = x1; x <= x2; ++x, ++cell)
	{
		if (x < 0 || x >= ThePartitionManager->m_cellCountX)
			continue;

		distance = static_cast<Real>(sqrt( pow(x - parms->xCenter, 2) + pow(y - parms->yCenter, 2) ));
		mulVal = 1 - distance / parms->radius;
		if (mulVal < 0.0f) 
			mulVal = 0.0f;
		else if (mulVal > 1.0f)
			mulVal = 1.0f;

		cell->removeCashValue( parms->playerIndex, REAL_TO_UNSIGNEDINT(parms->threatOrValue * mulVal) );
	}
}
