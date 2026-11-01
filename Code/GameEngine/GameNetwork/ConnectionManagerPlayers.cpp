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

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/ConnectionManager.h"

#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameLobbyCallbacks.h"
#include "GameNetwork/networkutil.h"

// ConnectionManager: players leaving and being disconnected, and the packet router fallback plan.

Bool ConnectionManager::isPlayerConnected( Int playerID )
{
	return ( static_cast<UnsignedInt>(playerID) == m_localSlot || (m_connections[playerID] && !m_connections[playerID]->isQuitting()) );
}

/**
 * A player has just left our game. Delete their connection and frame data manager.
 * return codes are:
 * PLAYERLEAVECODE_UNKNOWN - player didn't have a valid slot number.
 * PLAYERLEAVECODE_CLIENT - someone in the game that wasn't us or the packet router.
 * PLAYERLEAVECODE_LOCAL - We are leaving the game, we could also be the packet router.
 * PLAYERLEAVECODE_PACKETROUTER - The packet router left the game.
 *
 * If we are leaving and are also the packet router, it will return the PLAYERLEAVECODE_LOCAL return code.
 */
PlayerLeaveCode ConnectionManager::processPlayerLeave(NetPlayerLeaveCommandMsg *msg) {
	return processPlayerLeave(msg->getLeavingPlayerID());
}

PlayerLeaveCode ConnectionManager::processPlayerLeave(UnsignedByte playerID) {
	if (playerID >= MAX_SLOTS) {
		return PLAYERLEAVECODE_UNKNOWN;
	}
	// the name, while the connection still gives it: a quitting one does not
	const UnicodeString leavingName = (playerID != m_localSlot) ? getPlayerName(playerID) : UnicodeString();
	if ((playerID != m_localSlot) && (m_connections[playerID] != nullptr)) {
		DEBUG_LOG(("ConnectionManager::processPlayerLeave() - setQuitting() on player %d on frame %d\n", playerID, TheGameLogic->getFrame()));
		m_connections[playerID]->setQuitting();
	}
	DEBUG_ASSERTCRASH(m_frameData[playerID] == nullptr || m_frameData[playerID]->getIsQuitting() == FALSE, ("Player %d is already quitting", playerID));
	if ((playerID != m_localSlot) && (m_frameData[playerID] != nullptr) && (m_frameData[playerID]->getIsQuitting() == FALSE)) {
		DEBUG_LOG(("ConnectionManager::processPlayerLeave - setQuitFrame on player %d for frame %d\n", playerID, TheGameLogic->getFrame()+1));
		m_frameData[playerID]->setQuitFrame(TheGameLogic->getFrame() + FRAMES_TO_KEEP + 1);
	}

	if (playerID == m_localSlot)
	{
		// we're leaving, so mark our connections and frame datas to go away.
		for (Int i=0; i<MAX_SLOTS; ++i)
		{
			if (m_connections[i])
			{
				m_connections[i]->clearCommandsExceptFrom(m_localSlot);
				m_connections[i]->setQuitting();
			}
		}
	}

	PlayerLeaveCode code = disconnectPlayer(playerID);
	if (leavingName.getLength() > 0) {
		m_events->playerLeft(playerID, leavingName);
	}
	DEBUG_LOG(("ConnectionManager::processPlayerLeave() - just disconnected player %d with ret code %d\n", playerID, code));
	if (code == PLAYERLEAVECODE_PACKETROUTER)
		resendPendingCommands();

	m_events->playersChanged();
	return code;
}

void ConnectionManager::observerLeft(NetCommandRef *ref) {
	const UnsignedByte slot = static_cast<NetPlayerLeaveCommandMsg *>(ref->getCommand())->getLeavingPlayerID();
	sendRemoteCommand(ref, FALSE);	// the packet router passes it on
	if (slot != ref->getCommand()->getPlayerID() || !isPlayerConnected(slot)) {
		return;
	}
	DEBUG_LOG(("ConnectionManager::observerLeft - observer %d left on frame %d\n", slot, TheGameLogic->getFrame()));
	const PlayerLeaveCode code = processPlayerLeave(slot);
	if (code == PLAYERLEAVECODE_PACKETROUTER) {
		resendPendingCommands();
	}
}

/**
 * An observer's machine keeps the commands of the players for frames ahead of its own in a ring of
 * FRAME_DATA_LENGTH frames, of which FRAMES_TO_KEEP lie behind. The players are ahead of it by its lag
 * plus their run ahead, and its command counts lead its frame by its run ahead: past this lag the
 * ring would overflow. An observer that far behind is let go; it does not hold the players up.
 */
void ConnectionManager::dropLaggingObservers() {
	if (m_observers == 0) {
		return;
	}
	const Int limit = FRAME_DATA_LENGTH - FRAMES_TO_KEEP - 2 * max(m_currentRunAhead, MIN_RUNAHEAD) - 8;
	const UnsignedInt frame = TheGameLogic->getFrame();
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (!isObserver(i) || static_cast<UnsignedInt>(i) == m_localSlot || !isPlayerConnected(i)) {
			continue;
		}
		if (frame > m_observerFrameInfo[i] + static_cast<UnsignedInt>(limit)) {
			DEBUG_LOG(("ConnectionManager::dropLaggingObservers - observer %d is %d frames behind on frame %d, let go\n", i,
				frame - m_observerFrameInfo[i], frame));
			if (disconnectPlayer(i) == PLAYERLEAVECODE_PACKETROUTER) {
				resendPendingCommands();
			}
			m_events->playersChanged();
		}
	}
}

UnsignedInt ConnectionManager::getPacketRouterFallbackSlot(Int packetRouterNumber) {
	if ((packetRouterNumber >= 0) && (packetRouterNumber < MAX_SLOTS)) {
		return m_packetRouterFallback[packetRouterNumber];
	}
	return MAX_SLOTS;
}

UnsignedInt ConnectionManager::getPacketRouterSlot() {
	return m_packetRouterSlot;
}

Bool ConnectionManager::areAllQueuesEmpty(void) {
	Bool retval = TRUE;
	for (Int i = 0; (i < MAX_SLOTS) && retval; ++i) {
		if (m_connections[i] != nullptr) {
			if (m_connections[i]->isQueueEmpty() == FALSE) {
				//DEBUG_LOG(("ConnectionManager::areAllQueuesEmpty() - m_connections[%d] is not empty\n", i));
				//m_connections[i]->debugPrintCommands();
				retval = FALSE;
			}
		}
	}

	return retval;
}

Bool ConnectionManager::canILeave() {
	return areAllQueuesEmpty();
}

/**
 * The local player is leaving. Tell the local player as well as the other players
 * to remove this player at the specified frame.
 */
void ConnectionManager::handleLocalPlayerLeaving(UnsignedInt frame) {
	NetPlayerLeaveCommandMsg *msg = newInstance(NetPlayerLeaveCommandMsg);

	msg->setLeavingPlayerID(static_cast<UnsignedByte>(m_localSlot));
	msg->setExecutionFrame(frame);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}
	msg->setPlayerID(m_localSlot);

	DEBUG_LOG(("ConnectionManager::handleLocalPlayerLeaving - Local player leaving on frame %d\n", frame));
	DEBUG_ASSERTCRASH(m_packetRouterSlot >= 0, ("ConnectionManager::handleLocalPlayerLeaving, packet router is %d, illegal value.", m_packetRouterSlot));

	sendLocalCommand(msg);

	msg->detach();
}

PlayerLeaveCode ConnectionManager::disconnectPlayer(Int slot) {
	// Need to do the deletion of the slot's connection and frame data here.
	PlayerLeaveCode retval = PLAYERLEAVECODE_CLIENT;
	DEBUG_LOG(("ConnectionManager::disconnectPlayer - disconnecting slot %d on frame %d\n", slot, TheGameLogic->getFrame()));

	if ((slot < 0) || (slot >= MAX_SLOTS)) {
		return PLAYERLEAVECODE_UNKNOWN;
	}

	if (TheGameInfo)
	{
		GameSlot *gSlot = TheGameInfo->getSlot( slot );
		if (gSlot && !gSlot->lastFrameInGame())
		{
			DEBUG_LOG(("ConnectionManager::disconnectPlayer(%d) - slot is last in the game on frame %d\n",
				slot, TheGameLogic->getFrame()));
			gSlot->setLastFrameInGame(TheGameLogic->getFrame());
		}
	}

	UnicodeString unicodeName;
	unicodeName = getPlayerName(slot);
	if (unicodeName.getLength() > 0 && m_connections[slot]) {
		m_events->playerLeft(slot, unicodeName);
	}

	if ((m_frameData[slot] != nullptr) && (m_frameData[slot]->getIsQuitting() == FALSE)) {
		DEBUG_LOG(("ConnectionManager::disconnectPlayer - deleting player %d frame data\n", slot));
		m_frameData[slot].reset();
	}

	if (m_connections[slot] != nullptr && !m_connections[slot]->isQuitting()) {
		DEBUG_LOG(("ConnectionManager::disconnectPlayer - deleting player %d connection\n", slot));
		m_connections[slot].reset();
	}

//	if (playerID == m_localSlot) {
//		TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
//	}

	if (static_cast<UnsignedInt>(slot) == m_packetRouterSlot) {
		Int index = 0;
		while ((index < (MAX_SLOTS-1)) && (m_packetRouterFallback[index] != m_packetRouterSlot)) {
			++index;
		}
		++index;
		m_packetRouterSlot = (index < MAX_SLOTS) ? m_packetRouterFallback[index] : static_cast<UnsignedInt>(-1);
		DEBUG_LOG(("Packet router left.  New packet router is slot %d\n", m_packetRouterSlot));
		retval = PLAYERLEAVECODE_PACKETROUTER;
	}
	if (m_localSlot == static_cast<UnsignedInt>(slot)) {
		DEBUG_LOG(("Disconnecting self\n"));
		retval = PLAYERLEAVECODE_LOCAL;
	}

	// Take the player out of the fallback plan
	Int fallbackindex = 0;
	while ((fallbackindex < MAX_SLOTS) && (m_packetRouterFallback[fallbackindex] != static_cast<UnsignedInt>(slot))) {
		++fallbackindex;
	}

	for (Int i = fallbackindex; i < MAX_SLOTS-1; ++i) {
		m_packetRouterFallback[i] = m_packetRouterFallback[i+1];
	}
	m_packetRouterFallback[MAX_SLOTS-1] = static_cast<UnsignedInt>(-1);

	return retval;
}

void ConnectionManager::quitGame() {
	// Need to do the NetDisconnectPlayerCommandMsg creation and sending here.
	NetDisconnectPlayerCommandMsg *disconnectMsg = newInstance(NetDisconnectPlayerCommandMsg);
	disconnectMsg->setDisconnectSlot(static_cast<UnsignedByte>(m_localSlot));
	disconnectMsg->setPlayerID(m_localSlot);
	if (DoesCommandRequireACommandID(disconnectMsg->getNetCommandType())) {
		disconnectMsg->setID(GenerateNextCommandID());
	}
	//DEBUG_LOG(("ConnectionManager::disconnectLocalPlayer - about to send disconnect command\n"));
	sendLocalCommandDirect(disconnectMsg, static_cast<UnsignedByte>(0xff ^ (1 << m_localSlot)));

	//DEBUG_LOG(("ConnectionManager::disconnectLocalPlayer - about to flush connections\n"));
	flushConnections(); // need to do this so our packet actually gets sent before the connections are deleted.
	//DEBUG_LOG(("ConnectionManager::disconnectLocalPlayer - done flushing connections\n"));

	disconnectMsg->detach();

	disconnectLocalPlayer();
}

void ConnectionManager::disconnectLocalPlayer() {
	// kill the frame data and the connections for all the other players.
	DEBUG_LOG(("ConnectionManager::disconnectLocalPlayer()\n"));
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (static_cast<UnsignedInt>(i) != m_localSlot) {
			disconnectPlayer(i);
		}
	}
}

void ConnectionManager::resendPendingCommands() {
	//DEBUG_LOG(("ConnectionManager::resendPendingCommands()\n"));
	if (m_pendingCommands == nullptr) {
		return;
	}

	NetCommandRef *ref = m_pendingCommands->getFirstMessage();
	while (ref != nullptr) {
		//DEBUG_LOG(("ConnectionManager::resendPendingCommands - resending command %d\n", ref->getCommand()->getID()));
		sendLocalCommand(ref->getCommand(), ref->getRelay());
		ref = ref->getNext();
	}
}

UnsignedInt ConnectionManager::getLocalPlayerID() {
	return m_localSlot;
}

UnicodeString ConnectionManager::getPlayerName(Int playerNum) {
	UnicodeString retval;
	if( static_cast<UnsignedInt>(playerNum) == m_localSlot ) {
		retval = m_localUser->GetName();
	}	else if (((m_connections[playerNum] != nullptr) && (m_connections[playerNum]->isQuitting() == FALSE))) {
		retval = m_connections[playerNum]->getUser()->GetName();
	}
	return retval;
}

void ConnectionManager::voteForPlayerDisconnect(Int slot) {
	if (m_disconnectManager != nullptr) {
		m_disconnectManager->voteForPlayerDisconnect(slot, this);
	}
}

Int ConnectionManager::getNumPlayers()
{
	Int retval = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (isPlayerConnected(i)) {
			++retval;
		}
	}

	return retval;
}

Bool ConnectionManager::isPacketRouter( void )
{
	return m_localSlot == m_packetRouterSlot;
}

UnsignedInt ConnectionManager::getNextPacketRouterSlot(UnsignedInt playerID) {
	Int index = 0;
	while ((index < (MAX_SLOTS-1)) && (m_packetRouterFallback[index] != playerID)) {
		++index;
	}
	++index;
	return m_packetRouterFallback[index];
}
