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
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/networkutil.h"

// Network: what is passed on to the connection manager (players, chat, files, statistics).

UnsignedInt Network::getPingFrame()
{
	return (m_conMgr)?m_conMgr->getPingFrame():0;
}

Int Network::getPingsSent()
{
	return (m_conMgr)?m_conMgr->getPingsSent():0;
}

Int Network::getPingsRecieved()
{
	return (m_conMgr)?m_conMgr->getPingsRecieved():0;
}

Bool Network::hasLocalPlayerLeft() {
	return m_localStatus == NETLOCALSTATUS_LEFT || m_localStatus == NETLOCALSTATUS_POSTGAME;
}

Bool Network::isPlayerConnected( Int playerID ) {
	if (static_cast<UnsignedInt>(playerID) == getLocalPlayerID()) {
		return m_localStatus == NETLOCALSTATUS_INGAME || m_localStatus == NETLOCALSTATUS_LEAVING;
	}
	return m_conMgr->isPlayerConnected(playerID);
}

/**
 * returns the number of incoming bytes per second averaged over the last 30 sec.
 */
Real Network::getIncomingBytesPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getIncomingBytesPerSecond();
	else
	  return 0.0;
}

/**
 * returns the number of incoming packets per second averaged over the last 30 sec.
 */
Real Network::getIncomingPacketsPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getIncomingPacketsPerSecond();
	else
	  return 0.0;
}

/**
 * returns the number of outgoing bytes per second averaged over the last 30 sec.
 */
Real Network::getOutgoingBytesPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getOutgoingBytesPerSecond();
	else
	  return 0.0;
}

/**
 * returns the number of outgoing packets per second averaged over the last 30 sec.
 */
Real Network::getOutgoingPacketsPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getOutgoingPacketsPerSecond();
	else
	  return 0.0;
}

/**
 * returns the number of bytes received per second that are not from a generals client averaged over 30 sec.
 */
Real Network::getUnknownBytesPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getUnknownBytesPerSecond();
	else
	  return 0.0;
}

/**
 * returns the number of packets received per second that are not from a generals client averaged over 30 sec.
 */
Real Network::getUnknownPacketsPerSecond( void )
{
	if (m_conMgr)
		return m_conMgr->getUnknownPacketsPerSecond();
	else
	  return 0.0;
}

/**
 * returns the smallest packet arrival cushion since this was last called.
 */
UnsignedInt Network::getPacketArrivalCushion( void )
{
	if (m_conMgr)
		return m_conMgr->getPacketArrivalCushion();
	else
		return 0;
}

/**
 * Sends a line of chat to the other players
 */
void Network::sendChat(UnicodeString text, Int playerMask) {
	if (m_localStatus == NETLOCALSTATUS_RETURNING) {
		return;
	}
	Int executionFrame = getExecutionFrame();
	m_conMgr->sendChat(text, playerMask, executionFrame);
}

/**
 * Sends a line of chat to the other players using the disconnect manager.
 */
void Network::sendDisconnectChat(UnicodeString text) {
	m_conMgr->sendDisconnectChat(text);
}

// send a file.  woohoo.
void Network::sendFile(AsciiString path, UnsignedByte playerMask, UnsignedShort commandID)
{
	m_conMgr->sendFile(path, playerMask, commandID);
}

// send a file.  woohoo.
UnsignedShort Network::sendFileAnnounce(AsciiString path, UnsignedByte playerMask)
{
	return m_conMgr->sendFileAnnounce(path, playerMask);
}

Int Network::getFileTransferProgress(Int playerID, AsciiString path)
{
	return m_conMgr->getFileTransferProgress(playerID, path);
}

Bool Network::areAllQueuesEmpty(void)
{
	return m_conMgr->canILeave();
}

/**
 * Quit the game now.  This should only be called from the disconnect screen.
 */
void Network::quitGame() {
	if (m_conMgr != nullptr) {
		m_conMgr->quitGame();
	}

	// Blow up / Transfer your units when you quit.  Like a normal quit menu quit.
	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_SELF_DESTRUCT);
	msg->appendBooleanArgument(TRUE);

	TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
	m_localStatus = NETLOCALSTATUS_POSTGAME;
	DEBUG_LOG(("Network::quitGame - quitting game..."));
}


Bool Network::isPacketRouter( void )
{
	return m_conMgr && m_conMgr->isPacketRouter();
}

/**
 * Register a vote towards a player being disconnected.
 */
void Network::voteForPlayerDisconnect(Int slot) {
	if (m_conMgr != nullptr) {
		m_conMgr->voteForPlayerDisconnect(slot);
	}
}

Bool Network::voteToRemovePlayer(Int slot) {
	if ((m_conMgr == nullptr) || (m_localStatus != NETLOCALSTATUS_INGAME)) {
		return FALSE;
	}
	const UnsignedInt localSlot = m_conMgr->getLocalPlayerID();
	if ((slot < 0) || (slot >= MAX_SLOTS) || (static_cast<UnsignedInt>(slot) == localSlot) || !m_conMgr->isPlayerConnected(slot)) {
		return FALSE;
	}
	// the same test as the count, so a vote that would be refused is not sent
	const ::Engine::RemovalVote::Tally tally = ::Engine::RemovalVote().tally(slot, 0, removalVoters());
	if (((removalVoters() >> localSlot) & 1) == 0 || tally.needed < ::Engine::RemovalVote::MIN_VOTERS) {
		return FALSE;
	}
	NetRemovePlayerVoteCommandMsg *msg = newInstance(NetRemovePlayerVoteCommandMsg);
	msg->setSlot(static_cast<UnsignedByte>(slot));
	msg->setPlayerID(localSlot);
	msg->setExecutionFrame(getExecutionFrame());
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}
	m_conMgr->sendLocalCommand(msg);
	msg->detach();
	return TRUE;
}

void Network::updateLoadProgress( Int percent )
{
	// a returning player loads alone: the others play
	if (m_conMgr != nullptr && m_localStatus != NETLOCALSTATUS_RETURNING) {
		m_conMgr->updateLoadProgress( percent );
	}
}

void Network::loadProgressComplete( void )
{
	if (m_conMgr != nullptr && m_localStatus != NETLOCALSTATUS_RETURNING) {
		m_conMgr->loadProgressComplete();
	}
}

void Network::sendTimeOutGameStart( void )
{
	if (m_conMgr != nullptr && m_localStatus != NETLOCALSTATUS_RETURNING) {
		m_conMgr->sendTimeOutGameStart();
	}
}


UnsignedInt Network::getLocalPlayerID()
{
if (m_conMgr != nullptr) {
		return m_conMgr->getLocalPlayerID();
	}
	return 49;
}
UnicodeString Network::getPlayerName(Int playerNum)
{
	if (static_cast<UnsignedInt>(playerNum) == m_conMgr->getLocalPlayerID()) {
		if (m_localStatus != NETLOCALSTATUS_INGAME) {
			return UnicodeString::TheEmptyString;
		}
	}
	if (m_conMgr != nullptr) {
		return m_conMgr->getPlayerName( playerNum );
	}
	return UnicodeString::TheEmptyString;
}
Int Network::getNumPlayers()
{
	if (m_conMgr != nullptr) {
		return m_conMgr->getNumPlayers();
	}
	return -1;
}

Int Network::getSlotAverageFPS(Int slot) {
	if (m_conMgr != nullptr) {
		return m_conMgr->getSlotAverageFPS(slot);
	}
	return -1;
}

#if defined(_DEBUG)
void Network::toggleNetworkOn() {
	if (m_networkOn == TRUE) {
		m_networkOn = FALSE;
	} else {
		m_networkOn = TRUE;
	}
}
#endif

void Network::notifyOthersOfCurrentFrame() {
	if (m_conMgr != nullptr) {
		m_conMgr->notifyOthersOfCurrentFrame(TheGameLogic->getFrame());
	}
}

void Network::notifyOthersOfNewFrame(UnsignedInt frame) {
	if (m_conMgr != nullptr) {
		m_conMgr->notifyOthersOfNewFrame(frame);
	}
}
