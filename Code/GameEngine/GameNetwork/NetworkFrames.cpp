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
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/Network.h"

#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"

// Network: the frames -- the game's commands out, the commands of a complete frame in, leaving the game.

/**
 * Does this command need to be transfered to the other game clients?
 */
Bool Network::isTransferCommand(GameMessage *msg) {
	if ((msg != nullptr) && ((msg->getType() > GameMessage::MSG_BEGIN_NETWORK_MESSAGES) && (msg->getType() < GameMessage::MSG_END_NETWORK_MESSAGES))) {
		return TRUE;
	}
	return FALSE;
}

/**
 * Take commands from TheCommandList and give them to the connection manager for transport.
 */
void Network::GetCommandsFromCommandList() {
	GameMessage *msg = TheCommandList->getFirstMessage();
	GameMessage *next = nullptr;
	while (msg != nullptr) {
		next = msg->next();
		if (isTransferCommand(msg)) { // Is this something we should be sending to the other players?
			if (m_localStatus == NETLOCALSTATUS_INGAME) {
				m_conMgr->sendLocalGameMessage(msg, getExecutionFrame());
			}
			TheCommandList->removeMessage(msg); // This does not destroy msg's prev and next pointers, so they should still be valid.
			deleteInstanceOf(msg);
		} else {
			if (processCommand(msg)) {
				TheCommandList->removeMessage(msg);
				deleteInstanceOf(msg);
			}
		}
		msg = next;
	}
}

Int Network::getExecutionFrame() {
	Int logicFrame = TheGameLogic->getFrame() + m_runAhead;
	if (logicFrame > m_lastExecutionFrame) {
		m_lastExecutionFrame = logicFrame;
		return (logicFrame);
	}
	return m_lastExecutionFrame;
}

/**
 * This is where any processing that needs to be done for specific game messages.
 * Also check here to see if the logic frame number has changed to see if we need to
 * send our info for the last frame to the other players.
 * Return true if the message should be "eaten" by the network.
 */
Bool Network::processCommand(GameMessage *msg) 
{
	if ((m_lastFrame != TheGameLogic->getFrame()) || (m_localStatus == NETLOCALSTATUS_PREGAME)) {
		// If this is the start of a new game logic frame, then tell the connection manager that the last
		// frame is over and that it should now produce a frame info packet for the other players.

		if (m_localStatus == NETLOCALSTATUS_PREGAME) {
			// a sort-of-hack that prevents extraneous frames from being executed before the game actually starts.
			// Idealy this shouldn't be necessary, but I don't think its hurting anything by being here.
			if (TheGameLogic->getFrame() == 1) {
				m_localStatus = NETLOCALSTATUS_INGAME;
				NetCommandList *netcmdlist = m_conMgr->getFrameCommandList(0); // clear out frame 0 since we skipped it
				deleteInstanceOf(netcmdlist);
			} else {
				return FALSE;
			}
		}

		// Only send frame info packets for frames where we are actually going to be in the game.
		// The reason is so we don't have to wait for frame info packets to be sent that aren't going to
		// matter anyways when we actually try to leave the game ourselves.
		if (m_localStatus == NETLOCALSTATUS_INGAME) {
			Int executionFrame = getExecutionFrame();

			// Send command counts for all the frames we can.
			for (Int i = m_lastFrameCompleted + 1; i < executionFrame; ++i) {
				m_conMgr->processFrameTick(i);
				//DEBUG_LOG(("Network::processCommand - calling processFrameTick for frame %d\n", i));
				m_lastFrameCompleted = i;
			}
		}

		//DEBUG_LOG(("Next Execution Frame - %d, last frame completed - %d\n", getExecutionFrame(), m_lastFrameCompleted));
		m_lastFrame = TheGameLogic->getFrame();
	}

	// Are we leaving the game?
	// This has to happen after the check to see if this is the start of a new logic frame.
	// The reason is that we have to send all the frame info packets necessary to get to the
	// frame where everyone else is going to see that we left.
	if ((msg->getType() == GameMessage::MSG_CLEAR_GAME_DATA) && (m_localStatus == NETLOCALSTATUS_INGAME)) {
		Int executionFrame = getExecutionFrame();
		DEBUG_LOG(("Network::processCommand - local player leaving, executionFrame = %d, player leaving on frame %d\n", executionFrame, executionFrame+1));

		m_conMgr->handleLocalPlayerLeaving(executionFrame+1);
		m_conMgr->processFrameTick(executionFrame); // This is the last command we will execute, so send the command count.
																								// Also, we are guaranteed not to send any more commands for this frame
																								// since the local status will change to "Leaving" so we don't have to
																								// worry about messing up the other players.
		m_conMgr->processFrameTick(executionFrame+1); // since we send it for executionFrame+1, we need to process both ticks
		m_lastFrameCompleted = executionFrame;
		DEBUG_LOG(("Network::processCommand - player leaving on frame %d\n", executionFrame));
		m_localStatus = NETLOCALSTATUS_LEAVING;
		return TRUE;
	}
	return FALSE;
}

/**
 * returns true if all the commands are ready for the given frame.
 */
Bool Network::AllCommandsReady(UnsignedInt frame) {
	if (m_conMgr == nullptr) {
		return TRUE;
	}

	if (m_localStatus == NETLOCALSTATUS_PREGAME) {
		return TRUE;
	}

	if (m_localStatus == NETLOCALSTATUS_POSTGAME) {
		return TRUE;
	}

	if (m_localStatus == NETLOCALSTATUS_RETURNING) {
		return returningFrameReady(frame);
	}

	return m_conMgr->allCommandsReady(frame);// && m_conMgr->allCRCsReady(frame);
}

/**
 * Take commands from the connection manager and put them on TheCommandList.
 * The commands need to be put on in the same order across all clients.
 */
void Network::RelayCommandsToCommandList(UnsignedInt frame) {
	if ((m_conMgr == nullptr) || (m_localStatus == NETLOCALSTATUS_PREGAME)) {
		return;
	}
	if (m_localStatus == NETLOCALSTATUS_RETURNING) {
		// before the return frame: the frames of the host's replay
		if (m_returnFeed != nullptr) {
			m_returnFeed->appendFrame(frame, TheCommandList);
		}
		return;
	}
	m_checkCRCsThisFrame = FALSE;
	// a returned player's CRC skipped in the last frame: its next ones are compared
	m_crcPending &= static_cast<UnsignedByte>(~m_crcSkipped);
	m_crcSkipped = 0;
	NetCommandList *netcmdlist = m_conMgr->getFrameCommandList(frame);
	NetCommandRef *msg = netcmdlist->getFirstMessage();
	while (msg != nullptr) {
		NetCommandType cmdType = msg->getCommand()->getNetCommandType();
		if (cmdType == NETCOMMANDTYPE_GAMECOMMAND) {
			//DEBUG_LOG(("Network::RelayCommandsToCommandList - appending command %d of type %s to command list on frame %d\n", msg->getCommand()->getID(), ((NetGameCommandMsg *)msg->getCommand())->constructGameMessage()->getCommandAsAsciiString().str(), TheGameLogic->getFrame()));
			// Senders only send commands of the network range (isTransferCommand); anything else
			// (MSG_NEW_GAME, MSG_CLEAR_GAME_DATA, ...) would restart or end the game on every client.
			NetGameCommandMsg *command = static_cast<NetGameCommandMsg *>(msg->getCommand());
			const GameMessage::Type type = command->getGameMessageType();
			if (type > GameMessage::MSG_BEGIN_NETWORK_MESSAGES && type < GameMessage::MSG_END_NETWORK_MESSAGES)
				TheCommandList->appendMessage(command->constructGameMessage());
			else
				DEBUG_LOG(("Network::RelayCommandsToCommandList - command of type %d dropped\n", static_cast<Int>(type)));
		} else {
			processFrameSynchronizedNetCommand(msg);
		}
		msg = msg->getNext();
	}

	deleteInstanceOf(netcmdlist);
	captureReturnStates(frame);
}

/**
 * This is where network commands that need to be executed on the same frame should be executed.
 */
void Network::processFrameSynchronizedNetCommand(NetCommandRef *msg) {
	NetCommandMsg *cmdMsg = msg->getCommand();
	if (cmdMsg->getNetCommandType() == NETCOMMANDTYPE_PLAYERLEAVE) {
		// a player removed by vote in this frame may announce its own leaving in the same frame
		const UnsignedInt leaving = static_cast<NetPlayerLeaveCommandMsg *>(cmdMsg)->getLeavingPlayerID();
		const Bool gone = (leaving == m_conMgr->getLocalPlayerID()) ? (m_localStatus == NETLOCALSTATUS_LEFT) : !m_conMgr->isPlayerConnected(leaving);
		if (gone) {
			DEBUG_LOG(("Network::processFrameSynchronizedNetCommand - player %d left already\n", leaving));
			return;
		}
		PlayerLeaveCode retval = m_conMgr->processPlayerLeave(static_cast<NetPlayerLeaveCommandMsg *>(cmdMsg));
		if (retval == PLAYERLEAVECODE_LOCAL) {
			DEBUG_LOG(("Network::processFrameSynchronizedNetCommand - Local player left the game on frame %d.\n", TheGameLogic->getFrame()));
			m_localStatus = NETLOCALSTATUS_LEFT;
		} else if (retval == PLAYERLEAVECODE_PACKETROUTER) {
			DEBUG_LOG(("Network::processFrameSynchronizedNetCommand - Packet router left the game on frame %d\n", TheGameLogic->getFrame()));
		} else {
			DEBUG_LOG(("Network::processFrameSynchronizedNetCommand - Client left the game on frame %d\n", TheGameLogic->getFrame()));
		}
	}
	else if (cmdMsg->getNetCommandType() == NETCOMMANDTYPE_RUNAHEAD) {
		NetRunAheadCommandMsg *netmsg = static_cast<NetRunAheadCommandMsg *>(cmdMsg);
		processRunAheadCommand(netmsg);
		DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("command to set run ahead to %d and frame rate to %d on frame %d actually executed on frame %d\n", netmsg->getRunAhead(), netmsg->getFrameRate(), netmsg->getExecutionFrame(), TheGameLogic->getFrame()));
	}
	else if (cmdMsg->getNetCommandType() == NETCOMMANDTYPE_DESTROYPLAYER) {
		NetDestroyPlayerCommandMsg *netmsg = static_cast<NetDestroyPlayerCommandMsg *>(cmdMsg);
		processDestroyPlayerCommand(netmsg);
		//DEBUG_LOG(("CRC command (%8.8X) on frame %d actually executed on frame %d\n", netmsg->getCRC(), netmsg->getExecutionFrame(), TheGameLogic->getFrame()));
	}
	else if (cmdMsg->getNetCommandType() == NETCOMMANDTYPE_REMOVEPLAYERVOTE) {
		processRemovePlayerVote(static_cast<NetRemovePlayerVoteCommandMsg *>(cmdMsg));
	}
	else if (cmdMsg->getNetCommandType() == NETCOMMANDTYPE_PLAYERRETURN) {
		processPlayerReturn(static_cast<NetPlayerReturnCommandMsg *>(cmdMsg));
	}
}

void Network::processRemovePlayerVote(NetRemovePlayerVoteCommandMsg *msg)
{
	const UnsignedByte inGame = playersInGame();
	const UnsignedByte voters = removalVoters();
	const Int voter = msg->getPlayerID();
	const Int target = msg->getSlot();
	const UnsignedInt frame = TheGameLogic->getFrame();
	const ::Engine::RemovalVote::Outcome outcome = m_removalVote.vote(voter, target, frame, inGame, voters);
	if (outcome == ::Engine::RemovalVote::Outcome::Refused) {
		DEBUG_LOG(("Network::processRemovePlayerVote - vote of %d against %d refused on frame %d\n", voter, target, frame));
		return;
	}
	const ::Engine::RemovalVote::Tally tally = m_removalVote.tally(target, frame, voters);
	DEBUG_LOG(("Network::processRemovePlayerVote - %d votes to remove %d on frame %d: %d of %d\n", voter, target, frame, tally.votes, tally.needed));
	m_conMgr->events().removalVote(voter, target, tally.votes, tally.needed);
	if (outcome == ::Engine::RemovalVote::Outcome::Passed) {
		removePlayerByVote(target);
	}
}

// The players in the game are the same on every machine in a frame: leaving and dropping happen in
// frames too, and the slot list is the same everywhere.
UnsignedByte Network::playersInGame()
{
	UnsignedByte inGame = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_conMgr->isPlayerConnected(i)) {
			inGame |= static_cast<UnsignedByte>(1 << i);
		}
	}
	return inGame;
}

UnsignedByte Network::removalVoters()
{
	UnsignedByte voters = playersInGame();
	for (Int i = 0; i < MAX_SLOTS && TheGameInfo != nullptr; ++i) {
		const GameSlot *slot = TheGameInfo->getConstSlot(i);
		if (slot != nullptr && slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER) {
			voters &= static_cast<UnsignedByte>(~(1 << i));
		}
	}
	return voters;
}

/**
 * Every machine removes the player in the same frame, as if it had left: its commands up to this
 * frame are executed, later ones are not, and its units go to an ally as when a player quits.
 */
void Network::removePlayerByVote(Int slot)
{
	m_removalVote.forget(slot);
	m_conMgr->events().removedByVote(slot, m_conMgr->getPlayerName(slot));
	const PlayerLeaveCode code = m_conMgr->processPlayerLeave(static_cast<UnsignedByte>(slot));
	if (code == PLAYERLEAVECODE_LOCAL) {
		DEBUG_LOG(("Network::removePlayerByVote - the local player was removed on frame %d\n", TheGameLogic->getFrame()));
		m_localStatus = NETLOCALSTATUS_LEFT;
		return;
	}
	destroyPlayerUnits(slot, TRUE);
}

void Network::processRunAheadCommand(NetRunAheadCommandMsg *msg) {
	m_runAhead = msg->getRunAhead();
	m_frameRate = msg->getFrameRate();
	setFrameGrouping();
}

void Network::setFrameGrouping() {
	time_t frameGrouping = (1000 * m_runAhead) / m_frameRate; // number of miliseconds between packet sends
	frameGrouping = frameGrouping / 2; // since we only want the latency for one way to be a factor.
//	DEBUG_LOG(("Network::processRunAheadCommand - trying to set frame grouping to %d.  run ahead = %d, m_frameRate = %d\n", frameGrouping, m_runAhead, m_frameRate));
	if (frameGrouping < 1) {
		frameGrouping = 1; // Having a value less than 1 doesn't make sense.
	}
	if (frameGrouping > 500) {
		frameGrouping = 500; // Max of a half a second.
	}
	m_conMgr->setFrameGrouping(frameGrouping);
}

void Network::processDestroyPlayerCommand(NetDestroyPlayerCommandMsg *msg)
{
	UnsignedInt playerIndex = msg->getPlayerIndex();
	DEBUG_ASSERTCRASH(playerIndex < MAX_SLOTS, ("Bad player index"));
	if (playerIndex >= MAX_SLOTS)
		return;

	// The disconnect procedure sends this after the disconnect itself, which every machine handles
	// on arrival, before this frame. A command for a player still connected here is not part of it
	// and would let a player destroy another player's units.
	if (m_conMgr->isPlayerConnected(playerIndex))
	{
		DEBUG_LOG(("Network::processDestroyPlayerCommand - player %d is still connected, ignored\n", playerIndex));
		return;
	}
	// a dropped player whose return window ran out: it stays out
	if (m_conMgr->isReturnable(playerIndex))
	{
		m_conMgr->closeReturn(playerIndex);
		m_conMgr->events().returnClosed(playerIndex);
	}

	destroyPlayerUnits(playerIndex, FALSE);

	DEBUG_LOG(("Saw DestroyPlayer from %d about %d for frame %d on frame %d\n", msg->getPlayerID(), msg->getPlayerIndex(),
		msg->getExecutionFrame(), TheGameLogic->getFrame()));
}

void Network::destroyPlayerUnits(UnsignedInt playerIndex, Bool toAllies)
{
	AsciiString playerName;
	playerName.format("player%d", playerIndex);
	Player *pPlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
	if (pPlayer)
	{
		GameMessage *destructMsg = newInstance(GameMessage)(GameMessage::MSG_SELF_DESTRUCT);
		destructMsg->appendBooleanArgument(toAllies);
		destructMsg->friend_setPlayerIndex(pPlayer->getPlayerIndex());
		TheCommandList->appendMessage(destructMsg);
	}
}

/**
 * Service queues, process message stream, etc
 */
void Network::update( void )
{
//
// 1. Take Commands off TheCommandList, hand them off to the ConnectionManager.
// 2. Call ConnectionManager->update;
// 3. Check to see if all the commands for the next frame are there.
// 4. If all commands are there, put that frame's commands on TheCommandList.
//
	m_frameDataReady = FALSE;

#if defined(_DEBUG)
	if (m_networkOn == FALSE) {
		return;
	}
#endif

	GetCommandsFromCommandList(); // Remove commands from TheCommandList and send them to the connection manager.
	if (m_conMgr != nullptr) {
		if (m_localStatus == NETLOCALSTATUS_INGAME) {
			m_conMgr->updateRunAhead(m_runAhead, m_frameRate, m_didSelfSlug, getExecutionFrame());
			m_didSelfSlug = FALSE;
			m_conMgr->expireReturns(TheGameLogic->getFrame(), getExecutionFrame() + 1);
		}
		//m_conMgr->update(); // Do the priority thing, packetize thing. This also calls the Transport::update function.
									 // depacketizes the incoming packets and puts them on the Network command list.
	}

	liteupdate();

	if (m_localStatus == NETLOCALSTATUS_LEFT) {// || (m_localStatus == NETLOCALSTATUS_LEAVING)) {
		endOfGameCheck();
	}

	if (AllCommandsReady(TheGameLogic->getFrame())) { // If all the commands are ready for the next frame...
		m_conMgr->handleAllCommandsReady();
//		DEBUG_LOG(("Network::update - frame %d is ready\n", TheGameLogic->getFrame()));
		if (timeForNewFrame()) { // This needs to come after any other pre-frame execution checks as this changes the timing variables.
			RelayCommandsToCommandList(TheGameLogic->getFrame());	// Put the commands for the next frame on TheCommandList.
			m_frameDataReady = TRUE; // Tell the GameEngine to run the commands for the new frame.
		}
	}
}

void Network::liteupdate() {

#if defined(_DEBUG)
	if (m_networkOn == FALSE) {
		return;
	}
#endif

	if (m_conMgr != nullptr) {
		// a returning player does not wait for anybody before its return frame
		if (m_localStatus == NETLOCALSTATUS_PREGAME || m_localStatus == NETLOCALSTATUS_RETURNING) {
			m_conMgr->update(FALSE);
		} else {
			m_conMgr->update(TRUE);
		}
	}
}

void Network::endOfGameCheck() {
	if (m_conMgr != nullptr) {
		if (m_conMgr->canILeave()) {
			m_conMgr->disconnectLocalPlayer();
			TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
			m_localStatus = NETLOCALSTATUS_POSTGAME;

			DEBUG_LOG(("Network::endOfGameCheck - about to show the shell\n"));
		}
#if defined(_DEBUG)
		else {
			m_conMgr->debugPrintConnectionCommands();
		}
#endif
	}
}

Bool Network::timeForNewFrame() {
	if (isCatchingUp()) {
		return TRUE;
	}
	// Nobody waits for an observer, so one that fell behind has to catch up: while the commands of
	// frames well ahead are there already, it runs without the frame rate.
	if (m_conMgr != nullptr && m_conMgr->isObserver(m_conMgr->getLocalPlayerID())
		&& m_conMgr->allCommandsReady(TheGameLogic->getFrame() + m_runAhead + 5, TRUE)) {
		return TRUE;
	}

	__int64 curTime;
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER *>(&curTime));
	__int64 frameDelay = m_perfCountFreq / m_frameRate;

	/*
	 * If we're pushing up against the edge of our run ahead, we should slow the framerate down a bit
	 * to avoid being frozen by spikes in network lag.  This will happen if another user's computer is
	 * running too far behind us, so we need to slow down to let them catch up.
	 */
	if (m_conMgr != nullptr) {
		Real cushion = static_cast<Real>(m_conMgr->getMinimumCushion());
		Real runAheadPercentage = m_runAhead * (TheGlobalData->m_networkRunAheadSlack / static_cast<Real>(100.0)); // If we are at least 50% into our slack, we need to slow down.
		if (cushion < runAheadPercentage) {
//			DEBUG_LOG(("Average cushion = %f, run ahead percentage = %f.  Adjusting frameDelay from %I64d to ", cushion, runAheadPercentage, frameDelay));
			frameDelay += frameDelay / 10; // temporarily decrease the frame rate by 20%.
//			DEBUG_LOG(("%I64d\n", frameDelay));
			m_didSelfSlug = TRUE;
//		} else {
//			DEBUG_LOG(("Average cushion = %f, run ahead percentage = %f\n", cushion, runAheadPercentage));
		}
	}

	// Check to see if we can run another frame.
	if (curTime >= m_nextFrameTime) {
//		DEBUG_LOG(("Allowing a new frame, frameDelay = %I64d, curTime - m_nextFrameTime = %I64d\n", frameDelay, curTime - m_nextFrameTime));

//		if (m_nextFrameTime + frameDelay < curTime) {
		if ((m_nextFrameTime + (2 * frameDelay)) < curTime) {
			// If we get too far behind on our framerate we need to reset the nextFrameTime thing.
			m_nextFrameTime = curTime;
//			DEBUG_LOG(("Initializing m_nextFrameTime to %I64d\n", m_nextFrameTime));
		} else {
			// Set the soonest possible starting time for the next frame.
			m_nextFrameTime += frameDelay;
//			DEBUG_LOG(("m_nextFrameTime = %I64d\n", m_nextFrameTime));
		}

		return TRUE;
	}
//	DEBUG_LOG(("Slowing down frame rate. frame rate = %d, frame delay = %I64d, curTime - m_nextFrameTime = %I64d\n", m_frameRate, frameDelay, curTime - m_nextFrameTime));
	return FALSE;
}

/**
 * Returns true if the game commands for the next frame have been put on the command list.
 */
Bool Network::isFrameDataReady() {
	return (m_frameDataReady || (m_localStatus == NETLOCALSTATUS_LEFT));
}
