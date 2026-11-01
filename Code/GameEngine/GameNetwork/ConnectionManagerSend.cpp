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
#include "GameNetwork/networkutil.h"

// ConnectionManager: commands of the local player -- game commands, frame info, chat, load
// progress and the frame data the disconnect procedure sends to others.

/**
 * The commands for the given frame are all ready, time to send out our command count for that frame.
 */
void ConnectionManager::processFrameTick(UnsignedInt frame) {
	if ((m_frameData[m_localSlot] == nullptr) || (m_frameData[m_localSlot]->getIsQuitting() == TRUE)) {
		// if the local frame data stuff is NULL, we must be leaving the game.
		return;
	}
	UnsignedShort commandCount = static_cast<UnsignedShort>(m_frameData[m_localSlot]->getCommandCount(frame));
	NetFrameCommandMsg *msg = newInstance(NetFrameCommandMsg);
	msg->setExecutionFrame(frame);
	msg->setCommandCount(commandCount);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}
	msg->setPlayerID(m_localSlot);

	m_frameMetrics.doPerFrameMetrics(frame);

	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::processFrameTick - sending frame info for frame %d, ID %d, command count %d\n", frame, msg->getID(), commandCount));

	sendLocalCommand(msg, 0xff & ~(1 << m_localSlot));

	msg->detach();
}

/**
 * This is where the commands from the local client are sent to the other clients in
 * the game.  This is also where the local commands are put into the frame data for
 * future execution.
 */
void ConnectionManager::sendLocalGameMessage(GameMessage *msg, UnsignedInt frame) {
	UnsignedShort currentID = 0;
	if (DoesCommandRequireACommandID(NETCOMMANDTYPE_GAMECOMMAND)) {
		currentID = GenerateNextCommandID();
	}

	NetCommandMsg *netmsg = newInstance(NetGameCommandMsg)(msg);
	netmsg->setExecutionFrame(frame);
	netmsg->setPlayerID(m_localSlot);
	netmsg->setID(currentID);

	sendLocalCommand(netmsg);

	netmsg->detach();
}

/**
 * This is a NetCommandMsg that originated on the local computer. Send this to everyone specified
 * in the relay field.  Commands sent in this way go through the packet router.
 */
void ConnectionManager::sendLocalCommand(NetCommandMsg *msg, UnsignedByte relay /* = 0xff by default*/) {
	if (CommandRequiresDirectSend(msg) || (m_packetRouterSlot < 0) || (m_packetRouterSlot >= MAX_SLOTS) || (m_connections[m_packetRouterSlot] == nullptr)) {
		sendLocalCommandDirect(msg, relay);
		return;
	}
	msg->attach();

	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendLocalCommand - sending net command %d of type %s\n", msg->getID(),
		GetAsciiNetCommandType(msg->getNetCommandType()).str()));

	if (relay & (1 << m_localSlot)) {
		DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendLocalCommand - adding net command of type %s to player %d for frame %d\n", GetAsciiNetCommandType(msg->getNetCommandType()).str(), msg->getPlayerID(), msg->getExecutionFrame()));
		m_frameData[m_localSlot]->addNetCommandMsg(msg);
	}

	// Send the packet to everyone else
	if (m_localSlot == m_packetRouterSlot) {
		// I am the packet router, I need to send this packet to everyone individually.
		for (Int i = 0; i < MAX_SLOTS; ++i) {
			// Send it to all open connections.
			if (((m_connections[i] != nullptr) && (m_connections[i]->isQuitting() == FALSE)) && (relay & (1 << i))) {
				// Set the relay mask to only go to this player so he knows not to relay it to anyone else.
				UnsignedByte temprelay = static_cast<UnsignedByte>(1 << i);
				m_connections[i]->sendNetCommandMsg(msg, temprelay); // This will create a new copy of netmsg for this connection.
			}
		}
	} else {
		// Send the command to everyone else via the packet router.
		UnsignedByte temprelay = relay & ~(1 << m_localSlot);	// Tell the packet router to relay the message to everyone but myself.
														// Hopefully the packet router is smart enough to not send it
														// to slots that are not in the game.

		m_connections[m_packetRouterSlot]->sendNetCommandMsg(msg, temprelay); // This will create a new copy of netmsg for this connection.

		if (CommandRequiresAck(msg)) {
			NetCommandRef *ref = m_pendingCommands->addMessage(msg);
			//DEBUG_LOG(("ConnectionManager::sendLocalCommand - added command %d to pending commands list.\n", msg->getID()));
			if (ref != nullptr) {
				ref->setRelay(temprelay);
			}
		}
	}

	msg->detach(); // detach from the command msg.
}

/**
 * This is a NetCommandMsg that originated on the local computer.  Send this to everyone specified
 * in the relay field.  Commands sent in this way do not go through the packet router.
 */
void ConnectionManager::sendLocalCommandDirect(NetCommandMsg *msg, UnsignedByte relay) {
	msg->attach();

	if (((relay & (1 << m_localSlot)) != 0) && (m_frameData[m_localSlot] != nullptr)) {
		if (IsCommandSynchronized(msg->getNetCommandType()) == TRUE) {
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendLocalCommandDirect - adding net command of type %s to player %d for frame %d\n", GetAsciiNetCommandType(msg->getNetCommandType()).str(), msg->getPlayerID(), msg->getExecutionFrame()));
			m_frameData[m_localSlot]->addNetCommandMsg(msg);
		}
	}

	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if ((relay & (1 << i)) != 0) {
			if ((m_connections[i] != nullptr) && (m_connections[i]->isQuitting() == FALSE)) {
				UnsignedByte temprelay = static_cast<UnsignedByte>(1 << i);
				m_connections[i]->sendNetCommandMsg(msg, temprelay);
				DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendLocalCommandDirect - Sending direct command %d of type %s to player %d\n", msg->getID(), GetAsciiNetCommandType(msg->getNetCommandType()).str(), i));
			}
		}
	}

	msg->detach();
}

void ConnectionManager::sendChat(UnicodeString text, Int playerMask, UnsignedInt executionFrame)
{
	NetChatCommandMsg *msg = newInstance(NetChatCommandMsg);
	msg->setText(text);
	msg->setPlayerMask(playerMask);
	msg->setPlayerID(m_localSlot);
	msg->setID(0);
	msg->setExecutionFrame(executionFrame);
	if (DoesCommandRequireACommandID(msg->getNetCommandType()))
	{
		msg->setID(GenerateNextCommandID());
	}
	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("Chat message has ID of %d, mask of %8.8X, text of %ls\n", msg->getID(), msg->getPlayerMask(), msg->getText().str()));

	sendLocalCommand(msg, static_cast<UnsignedByte>(0xff ^ (1 << m_localSlot)));
	processChat(msg);

	msg->detach();
}

void ConnectionManager::sendDisconnectChat(UnicodeString text) {
	NetDisconnectChatCommandMsg *msg = newInstance(NetDisconnectChatCommandMsg);
	msg->setPlayerID(m_localSlot);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}
	msg->setText(text);

	sendLocalCommandDirect(msg, static_cast<UnsignedByte>(0xff ^ (1 << m_localSlot)));
	processDisconnectChat(msg);
}

void ConnectionManager::updateLoadProgress( Int progress )
{
	NetProgressCommandMsg *msg = newInstance(NetProgressCommandMsg);
	msg->setPercentage( static_cast<UnsignedByte>(progress) );
	msg->setPlayerID( m_localSlot );
	if (DoesCommandRequireACommandID(msg->getNetCommandType()) == TRUE) {
		msg->setID(GenerateNextCommandID());
	}
	processProgress(msg);
	sendLocalCommand(msg, static_cast<UnsignedByte>(0xff ^ (1 << m_localSlot)));

	msg->detach();
}

void ConnectionManager::loadProgressComplete()
{
	NetCommandMsg *msg = newInstance(NetCommandMsg);
	msg->setPlayerID( m_localSlot );
	if (DoesCommandRequireACommandID(msg->getNetCommandType()) == TRUE) {
		msg->setID(GenerateNextCommandID());
	}
	msg->setNetCommandType(NETCOMMANDTYPE_LOADCOMPLETE);
	processLoadComplete(msg);
	sendLocalCommand(msg, static_cast<UnsignedByte>(0xff ^ (1 << m_localSlot)));

	msg->detach();
}

void ConnectionManager::sendTimeOutGameStart()
{
	NetCommandMsg *msg = newInstance(NetCommandMsg);
	msg->setPlayerID( m_localSlot );
	msg->setNetCommandType(NETCOMMANDTYPE_TIMEOUTSTART);
	if (DoesCommandRequireACommandID(msg->getNetCommandType()) == TRUE) {
		msg->setID(GenerateNextCommandID());
	}
	processTimeOutGameStart(msg);
	sendLocalCommand(msg, static_cast<UnsignedByte>(0xff ^ (1 << m_localSlot)));

	msg->detach();
}

void ConnectionManager::notifyOthersOfCurrentFrame(Int frame) {
	NetDisconnectFrameCommandMsg *msg = newInstance(NetDisconnectFrameCommandMsg);

	msg->setPlayerID(m_localSlot);
	msg->setDisconnectFrame(frame);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}

	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::notifyOthersOfCurrentFrame - sending disconnect frame of %d, command ID = %d\n", frame, msg->getID()));
	sendLocalCommandDirect(msg, static_cast<UnsignedByte>(0xff ^ (1 << m_localSlot)));
	NetCommandRef *ref = NEW_NETCOMMANDREF(msg);
	ref->setRelay(static_cast<UnsignedByte>(1 << m_localSlot));
	m_disconnectManager->processDisconnectCommand(ref, this);
	deleteInstanceOf(ref);

	msg->detach();

	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::notifyOthersOfCurrentFrame - start screen on debug stuff\n"));
#if defined(_DEBUG)
	debugPrintConnectionCommands();
#endif
}

void ConnectionManager::notifyOthersOfNewFrame(UnsignedInt frame) {
	NetDisconnectScreenOffCommandMsg *msg = newInstance(NetDisconnectScreenOffCommandMsg);

	msg->setPlayerID(m_localSlot);
	msg->setNewFrame(frame);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}

	sendLocalCommandDirect(msg, static_cast<UnsignedByte>(0xff ^ (1 << m_localSlot)));
	NetCommandRef *ref = NEW_NETCOMMANDREF(msg);
	ref->setRelay(static_cast<UnsignedByte>(1 << m_localSlot));
	m_disconnectManager->processDisconnectCommand(ref, this);
	deleteInstanceOf(ref);

	msg->detach();
}

void ConnectionManager::sendFrameDataToPlayer(UnsignedInt playerID, UnsignedInt startingFrame) {
	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendFrameDataToPlayer - sending frame data to player %d starting with frame %d\n", playerID, startingFrame));
	for (UnsignedInt frame = startingFrame; frame < TheGameLogic->getFrame(); ++frame) {
		sendSingleFrameToPlayer(playerID, frame);
	}
	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendFrameDataToPlayer - done sending commands to player %d\n", playerID));
}

void ConnectionManager::sendSingleFrameToPlayer(UnsignedInt playerID, UnsignedInt frame) {
	if ((TheGameLogic->getFrame() - FRAMES_TO_KEEP) > frame) {
		DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendSingleFrameToPlayer - player %d requested frame %d when we are on frame %d, this is too far in the past.\n", playerID, frame, TheGameLogic->getFrame()));
		return;
	}

	UnsignedByte relay = static_cast<UnsignedByte>(1 << playerID);

	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendFrameDataToPlayer - sending data for frame %d\n", frame));
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if ((m_frameData[i] != nullptr) && (static_cast<UnsignedInt>(i) != playerID)) { // no need to send his own commands to him.
			NetCommandList *list = m_frameData[i]->getFrameCommandList(frame);
			if (list != nullptr) {
				NetCommandRef *ref = list->getFirstMessage();
				while (ref != nullptr) {
					DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendFrameDataToPlayer - sending command %d from player %d to player %d using relay 0x%x\n", ref->getCommand()->getID(), i, playerID, relay));
					sendLocalCommandDirect(ref->getCommand(), relay);
					ref = ref->getNext();
				}
			}
			UnsignedInt frameCommandCount = m_frameData[i]->getFrameCommandCount(frame);
			NetFrameCommandMsg *msg = newInstance(NetFrameCommandMsg);
			msg->setExecutionFrame(frame);
			msg->setCommandCount(static_cast<UnsignedShort>(frameCommandCount));
			if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
				msg->setID(GenerateNextCommandID());
			}
			msg->setPlayerID(i);
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendFrameDataToPlayer - sending frame info from player %d to player %d for frame %d with command count %d and ID %d and relay %d\n", i, playerID, msg->getExecutionFrame(), msg->getCommandCount(), msg->getID(), relay));
			sendLocalCommandDirect(msg, relay);
			msg->detach();
		}
	}
}

void ConnectionManager::requestFrameDataResend(Int playerID, UnsignedInt frame) {
	NetFrameResendRequestCommandMsg *msg = newInstance(NetFrameResendRequestCommandMsg);
	msg->setPlayerID(m_localSlot);
	msg->setFrameToResend(frame);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}

	if (isPlayerConnected(playerID) == FALSE) {
		playerID = 0;
		while ((playerID < MAX_SLOTS) && (isPlayerConnected(playerID) == FALSE)) {
			++playerID;
		}
	}

	if (playerID < MAX_SLOTS) {
		sendLocalCommandDirect(msg, static_cast<UnsignedByte>(1 << playerID));
	}

	msg->detach();
}
