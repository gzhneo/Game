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
#include "GameNetwork/NetCommandWrapperList.h"
#include "GameNetwork/networkutil.h"

// ConnectionManager: packets of the other players -- relaying, acknowledging and handing the
// commands to the frame data.

/**
 * ConnectionManager::doRelay()
 * Queries the transport for commands that need to be relayed to another client.
 * Get those commands and relay them to the appropriate Connection(s). We make the
 * assumption that a command will only be relayed once.
 */
// Commands come from other players. Their slot and index fields are used as array indices further
// on (connections, frame data, acks, disconnect votes, progress), and the run-ahead values divide
// and size the frame queue. Drop a command with a value out of range before it is processed or
// relayed; every machine applies the same test, so synchronized commands stay in step.
static Bool isAcceptableNetCommand(NetCommandMsg *msg)
{
	if (msg->getPlayerID() >= MAX_SLOTS)
		return FALSE;
	switch (msg->getNetCommandType())
	{
		case NETCOMMANDTYPE_ACKBOTH:
			return static_cast<NetAckBothCommandMsg *>(msg)->getOriginalPlayerID() < MAX_SLOTS;
		case NETCOMMANDTYPE_ACKSTAGE1:
			return static_cast<NetAckStage1CommandMsg *>(msg)->getOriginalPlayerID() < MAX_SLOTS;
		case NETCOMMANDTYPE_ACKSTAGE2:
			return static_cast<NetAckStage2CommandMsg *>(msg)->getOriginalPlayerID() < MAX_SLOTS;
		case NETCOMMANDTYPE_PLAYERLEAVE:
			// a player announces only his own leaving; otherwise anyone could remove anyone, us included
			return static_cast<NetPlayerLeaveCommandMsg *>(msg)->getLeavingPlayerID() == msg->getPlayerID();
		case NETCOMMANDTYPE_DISCONNECTPLAYER:
			return static_cast<NetDisconnectPlayerCommandMsg *>(msg)->getDisconnectSlot() < MAX_SLOTS;
		case NETCOMMANDTYPE_DISCONNECTVOTE:
			return static_cast<NetDisconnectVoteCommandMsg *>(msg)->getSlot() < MAX_SLOTS;
		case NETCOMMANDTYPE_REMOVEPLAYERVOTE:
			return static_cast<NetRemovePlayerVoteCommandMsg *>(msg)->getSlot() < MAX_SLOTS;
		case NETCOMMANDTYPE_PLAYERRETURN:
			return static_cast<NetPlayerReturnCommandMsg *>(msg)->getSlot() < MAX_SLOTS;
		case NETCOMMANDTYPE_RUNAHEAD:
		{
			NetRunAheadCommandMsg *runAhead = static_cast<NetRunAheadCommandMsg *>(msg);
			return runAhead->getRunAhead() >= 1 && runAhead->getRunAhead() <= MAX_FRAMES_AHEAD
				&& runAhead->getFrameRate() >= 1;
		}
		default:
			return TRUE;
	}
}

// A command of a player reaches us from that player or from the packet router, which relays the
// commands of all players. A command under another player's number from anyone else is forged.
// Only the address is compared (one game per machine).
static Bool comesFromSenderOrRouter(const std::array<PoolPtr<Connection>, MAX_SLOTS> &connections, UnsignedInt routerSlot,
	UnsignedInt playerID, UnsignedInt addr)
{
	const auto addressOf = [&connections](UnsignedInt slot, UnsignedInt &out) {
		if (slot >= MAX_SLOTS || connections[slot] == nullptr || connections[slot]->getUser() == nullptr)
			return FALSE;
		out = connections[slot]->getUser()->GetIPAddr();
		return TRUE;
	};
	UnsignedInt known = 0;
	if (addressOf(playerID, known) && known == addr)
		return TRUE;
	return addressOf(routerSlot, known) && known == addr;
}

// Commands of a frame go to the frame data, which keeps one of each. A command acted on when it
// arrives would act twice when its sender repeats it after a lost acknowledgement (chat shown
// twice); the copy is acknowledged again but not processed or relayed.
Bool ConnectionManager::isRepeatedArrival(NetCommandMsg *msg) {
	const NetCommandType type = msg->getNetCommandType();
	if (IsCommandSynchronized(type) || !DoesCommandRequireACommandID(type) || type == NETCOMMANDTYPE_WRAPPER) {
		return FALSE;
	}
	if (m_arrivedIds[msg->getPlayerID()].firstTime(msg->getID())) {
		return FALSE;
	}
	DEBUG_LOG(("ConnectionManager - command %d of player %d (type %d) came again, not processed\n",
		(int)msg->getID(), (int)msg->getPlayerID(), (int)type));
	return TRUE;
}

void ConnectionManager::doRelay() {

	NetPacket *packet = nullptr;

	for (Int i = 0; i < MAX_MESSAGES; ++i) {
		if (m_transport->m_inBuffer[i].length != 0) {
			// This transport buffer has yet to be processed.

			// make a NetPacket out of this data so it can be broken up into individual commands.
			packet = newInstance(NetPacket)(&(m_transport->m_inBuffer[i]));

			//DEBUG_LOG(("ConnectionManager::doRelay() - got a packet with %d commands\n", packet->getNumCommands()));
			//LOGBUFFER( packet->getData(), packet->getLength() );

			// Get the command list from the packet.
			NetCommandList *cmdList = packet->getCommandList();
			NetCommandRef *cmd = cmdList->getFirstMessage();

			// Iterate through the commands in this packet and send them to the proper connections.
			while (cmd != nullptr) {
				//DEBUG_LOG(("ConnectionManager::doRelay() - Looking at a command of type %s\n",
					//GetAsciiNetCommandType(cmd->getCommand()->getNetCommandType()).str()));
				if (!isAcceptableNetCommand(cmd->getCommand())) {
					DEBUG_LOG(("ConnectionManager::doRelay() - dropped a command with out-of-range values (type %d, player %d)\n",
						(int)cmd->getCommand()->getNetCommandType(), (int)cmd->getCommand()->getPlayerID()));
				} else if (!comesFromSenderOrRouter(m_connections, m_packetRouterSlot, cmd->getCommand()->getPlayerID(), m_transport->m_inBuffer[i].addr)) {
					DEBUG_LOG(("ConnectionManager::doRelay() - dropped a command of player %d from another address (type %d)\n",
						(int)cmd->getCommand()->getPlayerID(), (int)cmd->getCommand()->getNetCommandType()));
				} else if (isAwaitedReturn(cmd->getCommand()->getPlayerID())) {
					// a returning player the packet router took in before us: it sends this again
					DEBUG_LOG(("ConnectionManager::doRelay() - command of returning player %d not taken yet (type %d)\n",
						(int)cmd->getCommand()->getPlayerID(), (int)cmd->getCommand()->getNetCommandType()));
				} else {
					if (CommandRequiresAck(cmd->getCommand())) {
						ackCommand(cmd, m_localSlot);
					}
					if (!isRepeatedArrival(cmd->getCommand()) && !processNetCommand(cmd)) {
						sendRemoteCommand(cmd);
					}
				}
				cmd = cmd->getNext();

			}

			// Delete this packet since we won't be needing it anymore.
			deleteInstanceOf(packet);
			packet = nullptr;

			deleteInstanceOf(cmdList);
			cmdList = nullptr;

			// signal that this has been processed.
			m_transport->m_inBuffer[i].length = 0;
		}
	}

	NetCommandList *cmdList = m_netCommandWrapperList->getReadyCommands();
	NetCommandRef *cmd = cmdList->getFirstMessage();
	while (cmd != nullptr) {
		if (isAcceptableNetCommand(cmd->getCommand())) {
			if (CommandRequiresAck(cmd->getCommand())) {
				ackCommand(cmd, m_localSlot);
			}
			if (!isRepeatedArrival(cmd->getCommand()) && !processNetCommand(cmd)) {
				sendRemoteCommand(cmd);
			}
		}
		cmd = cmd->getNext();

	}

	// Delete this packet since we won't be needing it anymore.
	deleteInstanceOf(packet);
	packet = nullptr;

	deleteInstanceOf(cmdList);
	cmdList = nullptr;
}

/**
 * This is where the non-synchronized network commands should be processed.
 * Return TRUE if the command should not be relayed. Return FALSE if it should be relayed.
 */
Bool ConnectionManager::processNetCommand(NetCommandRef *ref) {
	NetCommandMsg *msg = ref->getCommand();

	if ((msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE1) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE2) ||
			(msg->getNetCommandType() == NETCOMMANDTYPE_ACKBOTH)) {

		processAck(msg);
		return FALSE;
	}

	if ((m_connections[msg->getPlayerID()] == nullptr) && (msg->getPlayerID() != m_localSlot)) {
		// if this is from a player that is no longer in the game, then ignore them.
		return TRUE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_WRAPPER) {
		processWrapper(ref); // need to send the NetCommandRef since we have to construct the relay for the wrapped command.
		return FALSE;
	}

	if ((msg->getPlayerID() >= 0) && (msg->getPlayerID() < MAX_SLOTS) && (msg->getPlayerID() != m_localSlot)) {
		if (m_connections[msg->getPlayerID()] == nullptr) {
			return TRUE;
		}
	}

	// An observer's leaving is not a command of a frame: nobody waits for an observer's frames.
	if (msg->getNetCommandType() == NETCOMMANDTYPE_PLAYERLEAVE && isObserver(msg->getPlayerID()) && msg->getPlayerID() != m_localSlot) {
		observerLeft(ref);
		return TRUE;
	}

	// Don't allow an out of date command to be sent through.
	// Its unnecessary traffic and it could cause problems.
	//
	// This was a fix for a command count bug where a command would be
	// executed, then a command for that old frame would be added to the
	// FrameData for that frame + 256, and would screw up the command count.

	if (IsCommandSynchronized(msg->getNetCommandType())) {
		if (ref->getCommand()->getExecutionFrame() < TheGameLogic->getFrame()) {
			return TRUE;
		}
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_FRAMEINFO) {
		processFrameInfo(static_cast<NetFrameCommandMsg *>(msg));
		
		// need to set the relay so we don't send it to ourselves.
		UnsignedByte relay = ref->getRelay();
		relay = relay & (0xff ^ (1 << m_localSlot));
		ref->setRelay(relay);
		return FALSE;
	}
	
	if (msg->getNetCommandType() == NETCOMMANDTYPE_PROGRESS)
	{
		//DEBUG_LOG(("ConnectionManager::processNetCommand - got a progress net command from player %d\n", msg->getPlayerID()));
		processProgress(static_cast<NetProgressCommandMsg *>(msg));

		// need to set the relay so we don't send it to ourselves.
		UnsignedByte relay = ref->getRelay();
		relay = relay & (0xff ^ (1 << m_localSlot));
		ref->setRelay(relay);
		return FALSE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_TIMEOUTSTART)
	{
		DEBUG_LOG(("ConnectionManager::processNetCommand - got a TimeOut GameStart net command from player %d\n", msg->getPlayerID()));
		processTimeOutGameStart(msg);
		return FALSE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_RUNAHEADMETRICS) {
		processRunAheadMetrics(static_cast<NetRunAheadMetricsCommandMsg *>(msg));
		return TRUE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_KEEPALIVE) {
		return TRUE;
	}

	if ((msg->getNetCommandType() > NETCOMMANDTYPE_DISCONNECTSTART) && (msg->getNetCommandType() < NETCOMMANDTYPE_DISCONNECTEND)) {
		m_disconnectManager->processDisconnectCommand(ref, this);
		return TRUE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTCHAT) {
		processDisconnectChat(static_cast<NetDisconnectChatCommandMsg *>(msg));
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_LOADCOMPLETE)
	{
		DEBUG_LOG(("ConnectionManager::processNetCommand - got a Load Complete net command from player %d\n", msg->getPlayerID()));
		processLoadComplete(msg);
		return FALSE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_CHAT) {
		processChat(static_cast<NetChatCommandMsg *>(msg));
		return FALSE;
	} 

	if (msg->getNetCommandType() == NETCOMMANDTYPE_FILE) {
		processFile(static_cast<NetFileCommandMsg *>(msg));
		return FALSE;
	} 

	if (msg->getNetCommandType() == NETCOMMANDTYPE_FILEANNOUNCE) {
		processFileAnnounce(static_cast<NetFileAnnounceCommandMsg *>(msg));
		return FALSE;
	} 

	if (msg->getNetCommandType() == NETCOMMANDTYPE_FILEPROGRESS) {
		processFileProgress(static_cast<NetFileProgressCommandMsg *>(msg));
		return FALSE;
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_FRAMERESENDREQUEST) {
		processFrameResendRequest(static_cast<NetFrameResendRequestCommandMsg *>(msg));
		return TRUE;
	}

	return FALSE;
}

void ConnectionManager::processFrameResendRequest(NetFrameResendRequestCommandMsg *msg) {
	// first make sure this is a valid slot
	Int playerID = msg->getPlayerID();
	if ((playerID < 0) || (playerID >= MAX_SLOTS)) {	
		return;
	}

	// make sure this player is still in our game.
	if ((m_connections[playerID] == nullptr) || (m_connections[playerID]->isQuitting() == TRUE)) {
		return;
	}

	sendFrameDataToPlayer(playerID, msg->getFrameToResend());
}

/**
 * We have received a wrapper for a command too big to fit in a packet.
 */
void ConnectionManager::processWrapper(NetCommandRef *ref) 
{
	NetWrapperCommandMsg *wrapperMsg = static_cast<NetWrapperCommandMsg *>(ref->getCommand());
	UnsignedShort commandID = wrapperMsg->getWrappedCommandID();
	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::processWrapper() - wrapped commandID is %d, commandID is %d\n",
		commandID, wrapperMsg->getID()));
	Int origProgress = 0;
	FileCommandMap::iterator fcIt = m_fileCommands.find(commandID);
	if (fcIt != m_fileCommands.end())
	{
		origProgress = m_fileProgress[m_localSlot][commandID];
	}
	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::processWrapper() - origProgress[%d] == %d for command %d\n",
		m_localSlot, origProgress, commandID));

	m_netCommandWrapperList->processWrapper(ref);

	if (fcIt != m_fileCommands.end())
	{
		Int newProgress = m_netCommandWrapperList->getPercentComplete(commandID);
		DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::processWrapper() - newProgress[%d] == %d for command %d\n",
			m_localSlot, newProgress, commandID));
		if (newProgress > origProgress && newProgress < 100)
		{
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::processWrapper() - sending a NetFileProgressCommandMsg\n"));
			m_fileProgress[m_localSlot][commandID] = newProgress;

			Int progressMask = 0xff ^ (1 << m_localSlot);
			NetFileProgressCommandMsg *msg = newInstance(NetFileProgressCommandMsg);
			msg->setPlayerID(m_localSlot);
			msg->setID(0);
			if (DoesCommandRequireACommandID(msg->getNetCommandType()))
			{
				msg->setID(GenerateNextCommandID());
			}
			msg->setFileID(commandID);
			msg->setProgress(newProgress);
			sendLocalCommand(msg, static_cast<UnsignedByte>(progressMask));
			processFileProgress(msg);
			msg->detach();
		}
	}
}

void ConnectionManager::processDisconnectChat(NetDisconnectChatCommandMsg *msg) 
{
	UnicodeString unitext;
	UnicodeString name;
	UnsignedByte playerID = static_cast<UnsignedByte>(msg->getPlayerID());
	if (playerID == m_localSlot) {
		name = m_localUser->GetName();
	} else if (isPlayerConnected(playerID)) {
		name = m_connections[playerID]->getUser()->GetName();
	}
	unitext.format(L"[%ls] %ls", name.str(), msg->getText().str());
//	DEBUG_LOG(("ConnectionManager::processDisconnectChat - got message from player %d, message is %ls\n", playerID, unitext.str()));
	m_events->disconnectChat(unitext);
}

void ConnectionManager::processChat(NetChatCommandMsg *msg) 
{
	UnicodeString unitext;
	UnicodeString name;
	UnsignedByte playerID = static_cast<UnsignedByte>(msg->getPlayerID());
	if (playerID == m_localSlot) {
		name = m_localUser->GetName();
	} else if (((m_connections[playerID] != nullptr) && (m_connections[playerID]->isQuitting() == FALSE))) {
		name = m_connections[playerID]->getUser()->GetName();
	}
	unitext.format(L"[%ls] %ls", name.str(), msg->getText().str());

	m_events->chat(playerID, static_cast<UnsignedByte>(msg->getPlayerMask()), static_cast<Int>(m_localSlot), unitext);
}

void ConnectionManager::processProgress( NetProgressCommandMsg *msg )
{
	TheGameLogic->processProgress(msg->getPlayerID(), msg->getPercentage());
}

void ConnectionManager::processLoadComplete( NetCommandMsg *msg )
{
	TheGameLogic->processProgressComplete(msg->getPlayerID());
}

void ConnectionManager::processTimeOutGameStart( NetCommandMsg *msg )
{
	TheGameLogic->timeOutGameStart();
}

/**
 * Another client has sent us the command count for a new frame.
 */
void ConnectionManager::processFrameInfo(NetFrameCommandMsg *msg) {
	//stupid frame info, why don't you process yourself?

	UnsignedInt playerID = msg->getPlayerID();

	if ((playerID >= 0) && (playerID < MAX_SLOTS)) {
		if (isObserver(playerID) && msg->getExecutionFrame() > m_observerFrameInfo[playerID]) {
			m_observerFrameInfo[playerID] = msg->getExecutionFrame();
		}
		if (m_frameData[playerID] != nullptr) {
//			DEBUG_LOG(("ConnectionManager::processFrameInfo - player %d, frame %d, command count %d, received on frame %d\n", playerID, msg->getExecutionFrame(), msg->getCommandCount(), TheGameLogic->getFrame()));
			m_frameData[playerID]->setFrameCommandCount(msg->getExecutionFrame(), msg->getCommandCount());
		}
	}
}

/**
 * We just got a stage 1 ack from someone.  So we should remove it from the connection that sent it so
 * it doesn't keep resending it.
 */
void ConnectionManager::processAckStage1(NetCommandMsg *msg) {
#if defined(_DEBUG)
	Bool doDebug = (msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTFRAME) ? TRUE : FALSE;
#endif

	UnsignedByte playerID = static_cast<UnsignedByte>(msg->getPlayerID());
	NetCommandRef *ref = nullptr;

#if defined(_DEBUG)
	if (doDebug == TRUE) {
		DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::processAck - processing ack for command %d from player %d\n", ((NetAckStage1CommandMsg *)msg)->getCommandID(), playerID));
	}
#endif
	
	if ((playerID >= 0) && (playerID < NUM_CONNECTIONS)) {
		if (m_connections[playerID] != nullptr) {
			ref = m_connections[playerID]->processAck(msg);
		}
	} else {
		DEBUG_ASSERTCRASH((playerID >= 0) && (playerID < NUM_CONNECTIONS), ("ConnectionManager::processAck - %d is an invalid player number", playerID));
	}

	if (ref != nullptr) {
		if (ref->getCommand()->getNetCommandType() == NETCOMMANDTYPE_FRAMEINFO) {
			m_frameMetrics.processLatencyResponse((static_cast<NetFrameCommandMsg *>(ref->getCommand()))->getExecutionFrame());
		}

		deleteInstanceOf(ref);
		ref = nullptr;
	}
}

/**
 * We just got a stage 2 ack from someone.  So remove it from the pending commands list so it doesn't
 * get sent in the case of a new packet router.
 */
void ConnectionManager::processAckStage2(NetCommandMsg *msg) {
	UnsignedShort commandID = 0;
	UnsignedByte playerID = 0;
	if (msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE2) {
		commandID = (static_cast<NetAckStage2CommandMsg *>(msg))->getCommandID();
		playerID = (static_cast<NetAckStage2CommandMsg *>(msg))->getOriginalPlayerID();
	} else if (msg->getNetCommandType() == NETCOMMANDTYPE_ACKBOTH) {
		commandID = (static_cast<NetAckBothCommandMsg *>(msg))->getCommandID();
		playerID = (static_cast<NetAckBothCommandMsg *>(msg))->getOriginalPlayerID();
	} else {
		return;
	}

	NetCommandRef *ref = m_pendingCommands->findMessage(commandID, playerID);
	if (ref != nullptr) {
		//DEBUG_LOG(("ConnectionManager::processAckStage2 - removing command %d from the pending commands list.\n", commandID));
		DEBUG_ASSERTCRASH((m_localSlot == playerID), ("Found a command in the pending commands list that wasn't originated by the local player"));
		m_pendingCommands->removeMessage(ref);
		deleteInstanceOf(ref);
		ref = nullptr;
	} else {
		//DEBUG_LOG(("ConnectionManager::processAckStage2 - Couldn't find command %d from player %d in the pending commands list.\n", commandID, playerID));
	}

	ref = m_relayedCommands->findMessage(commandID, playerID);
	if (ref != nullptr) {
		//DEBUG_LOG(("ConnectionManager::processAckStage2 - found command ID %d from player %d in the relayed commands list.\n", commandID, playerID));
		UnsignedByte relay = ref->getRelay();
		//DEBUG_LOG(("ConnectionManager::processAckStage2 - relay was %d and is now ", relay));
		relay = relay & ~(1 << msg->getPlayerID());
		//DEBUG_LOG(("%d\n", relay));
		if (relay == 0) {
			//DEBUG_LOG(("ConnectionManager::processAckStage2 - relay is 0, removing command from the relayed commands list.\n"));
			m_relayedCommands->removeMessage(ref);
			NetAckStage2CommandMsg *ackmsg = newInstance(NetAckStage2CommandMsg)(ref->getCommand());
			sendLocalCommand(ackmsg, static_cast<UnsignedByte>(1 << ackmsg->getOriginalPlayerID()));
			deleteInstanceOf(ref);
			ref = nullptr;

			ackmsg->detach();
			ackmsg = nullptr;
		} else {
			ref->setRelay(relay);
		}
	}
}

/**
 * We just got a "both" ack from someone.  So process it as both a stage 1 and stage 2 ack.
 */
void ConnectionManager::processAck(NetCommandMsg *msg) {
	if ((msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE1) || (msg->getNetCommandType() == NETCOMMANDTYPE_ACKBOTH)) {
		processAckStage1(msg);
	}
	if ((msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE2) || (msg->getNetCommandType() == NETCOMMANDTYPE_ACKBOTH)) {
		processAckStage2(msg);
	}
}

/**
 * We just got a message that needs to be ack'd, so ack it!
 */
void ConnectionManager::ackCommand(NetCommandRef *ref, UnsignedInt localSlot) {
	NetCommandMsg *msg = ref->getCommand();
	NetCommandMsg *ackmsg;
	UnsignedShort commandID;
	UnsignedByte originalPlayerID;
	UnsignedByte sendRelay = 0;

	// Make send relay a bitmask for the connections that the relay will actually be sent to. This is
	// necessary to determine whether or not we have to wait to send a stage 2 ack.
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (((m_connections[i] != nullptr) && (m_connections[i]->isQuitting() == FALSE))) {
			sendRelay = static_cast<UnsignedByte>(sendRelay | (1 << i));
		}
	}

#if defined(_DEBUG)
	Bool doDebug = (msg->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTFRAME) ? TRUE : FALSE;
#endif

	sendRelay = sendRelay & ref->getRelay();
	if (sendRelay == 0) {
		NetAckBothCommandMsg *bothmsg = newInstance(NetAckBothCommandMsg)(ref->getCommand());
		ackmsg = bothmsg;
		commandID = bothmsg->getCommandID();
		originalPlayerID = bothmsg->getOriginalPlayerID();
#if defined(_DEBUG)
		if (doDebug) {
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::ackCommand - doing ack both for command %d from player %d\n", bothmsg->getCommandID(), bothmsg->getOriginalPlayerID()));
		}
#endif
	} else {
		NetAckStage1CommandMsg *stage1msg = newInstance(NetAckStage1CommandMsg)(ref->getCommand());
		ackmsg = stage1msg;
		commandID = stage1msg->getCommandID();
		originalPlayerID = stage1msg->getOriginalPlayerID();
#if defined(_DEBUG)
		if (doDebug) {
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::ackCommand - doing ack stage 1 for command %d from player %d\n", stage1msg->getCommandID(), stage1msg->getOriginalPlayerID()));
		}
#endif
	}

	ackmsg->setPlayerID(localSlot); // Tell the player who this ack is coming from.

	if (CommandRequiresDirectSend(msg) && CommandRequiresAck(msg)) {
		// Send this ack directly back to the sending player, don't go through the packet router.
		if ((msg->getPlayerID() >= 0) && (msg->getPlayerID() < MAX_SLOTS)) {
			if (m_connections[msg->getPlayerID()] != nullptr) {
				m_connections[msg->getPlayerID()]->sendNetCommandMsg(ackmsg, static_cast<UnsignedByte>(1 << msg->getPlayerID()));
			}
		}
	} else {
		// The local connection may be the packet router, in that case, the connection would be NULL.  So do something about it!
		if ((m_packetRouterSlot >= 0) && (m_packetRouterSlot < MAX_SLOTS)) {
			if (m_connections[m_packetRouterSlot] != nullptr) {
//				DEBUG_LOG(("ConnectionManager::ackCommand - acking command %d from player %d to packet router.\n", commandID, m_packetRouterSlot));
				m_connections[m_packetRouterSlot]->sendNetCommandMsg(ackmsg, static_cast<UnsignedByte>(1 << m_packetRouterSlot));
			} else if (m_localSlot == m_packetRouterSlot) {
				// we are the packet router, send the ack to the player that sent the command.
				if ((msg->getPlayerID() >= 0) && (msg->getPlayerID() < MAX_SLOTS)) {
					if (m_connections[msg->getPlayerID()] != nullptr) {
//						DEBUG_LOG(("ConnectionManager::ackCommand - acking command %d from player %d directly to player.\n", commandID, msg->getPlayerID()));
						m_connections[msg->getPlayerID()]->sendNetCommandMsg(ackmsg, static_cast<UnsignedByte>(1 << msg->getPlayerID()));
					} else {
	//					DEBUG_ASSERTCRASH(m_connections[msg->getPlayerID()] != NULL, ("Connection to player is NULL"));
					}
				} else {
					DEBUG_ASSERTCRASH((msg->getPlayerID() >= 0) && (msg->getPlayerID() < MAX_SLOTS), ("Command sent by an invalid player ID."));
				}
			} else {
				DEBUG_ASSERTCRASH(m_connections[m_packetRouterSlot] != nullptr, ("Connection to packet router is NULL"));
			}
		} else {
			// No packet router is left only after every slot, the local one included, was disconnected:
			// the game is being torn down and nobody waits for this ack.
			DEBUG_LOG(("ConnectionManager::ackCommand - no packet router left, ack of command %d from player %d not sent\n",
				commandID, originalPlayerID));
		}
	}

	ackmsg->detach();
}

/**
 * This is where we relay a command from one client to others (including ourselves).
 * This should only be done by the current packet router.
 */
void ConnectionManager::sendRemoteCommand(NetCommandRef *msg, Bool keep) {
	UnsignedByte actualRelay = 0;
	if (msg->getCommand() == nullptr) {
		return;
	}

	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendRemoteCommand - sending net command %d of type %s from player %d, relay is 0x%x\n",
		msg->getCommand()->getID(), GetAsciiNetCommandType(msg->getCommand()->getNetCommandType()).str(), msg->getCommand()->getPlayerID(), msg->getRelay()));

	UnsignedByte relay = msg->getRelay();
	if (keep && (relay & (1 << m_localSlot)) && (m_frameData[msg->getCommand()->getPlayerID()] != nullptr)) {
		if (IsCommandSynchronized(msg->getCommand()->getNetCommandType())) {
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendRemoteCommand - adding net command of type %s to player %d for frame %d\n", GetAsciiNetCommandType(msg->getCommand()->getNetCommandType()).str(), msg->getCommand()->getPlayerID(), msg->getCommand()->getExecutionFrame()));
			m_frameData[msg->getCommand()->getPlayerID()]->addNetCommandMsg(msg->getCommand());
		}
	}

	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if ((relay & (1 << i)) && ((m_connections[i] != nullptr) && (m_connections[i]->isQuitting() == FALSE))) {
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendRemoteCommand - relaying command %d to player %d\n", msg->getCommand()->getID(), i));
			m_connections[i]->sendNetCommandMsg(msg->getCommand(), static_cast<UnsignedByte>(1 << i));
			actualRelay = static_cast<UnsignedByte>(actualRelay | (1 << i));
		}
	}

	if ((actualRelay != 0) && (CommandRequiresAck(msg->getCommand()) == TRUE)) {
		NetCommandRef *ref = m_relayedCommands->addMessage(msg->getCommand());
		if (ref != nullptr) {
			ref->setRelay(actualRelay);
			//DEBUG_LOG(("ConnectionManager::sendRemoteCommand - command %d added to relayed commands with relay %d\n", msg->getCommand()->getID(), ref->getRelay()));
		}
	}

	// Do some metrics to find the minimum packet arrival cushion.
	if (IsCommandSynchronized(msg->getCommand()->getNetCommandType())) {
//		DEBUG_LOG(("ConnectionManager::sendRemoteCommand - about to call allCommandsReady\n"));
		if (allCommandsReady(msg->getCommand()->getExecutionFrame(), TRUE)) {
			UnsignedInt cushion = msg->getCommand()->getExecutionFrame() - TheGameLogic->getFrame();
			if ((cushion < m_smallestPacketArrivalCushion) || (m_smallestPacketArrivalCushion == -1)) {
				m_smallestPacketArrivalCushion = cushion;
			}
			m_frameMetrics.addCushion(cushion);
//			DEBUG_LOG(("Adding %d to cushion for frame %d\n", cushion, msg->getCommand()->getExecutionFrame()));
		}
	}
}
