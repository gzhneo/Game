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

#include "GameClient/DisconnectMenu.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameLobbyCallbacks.h"
#include "GameNetwork/NetCommandWrapperList.h"
#include "GameNetwork/networkutil.h"

ConnectionManager::~ConnectionManager(void)
{
	// the connections and frame data go with the members; what was shown for the game goes now
	m_events->networkClosed();
}

ConnectionManager::ConnectionManager(void)
	: m_localUser(newInstance(User))
{
	m_localAddr = 0;
	m_localPort = 0;
}

/**
 * Initialize the connection manager and any subsystems.
 */
void ConnectionManager::init() 
{
	UnsignedInt i;
	for (i = 0; i < MAX_SLOTS; ++i) {
		m_connections[i].reset();
		m_frameData[i].reset();
	}

	if (m_pendingCommands == nullptr) {
		m_pendingCommands.reset(newInstance(NetCommandList));
		m_pendingCommands->init();
	}
	m_pendingCommands->reset();

	if (m_relayedCommands == nullptr) {
		m_relayedCommands.reset(newInstance(NetCommandList));
		m_relayedCommands->init();
	}
	m_relayedCommands->reset();

	m_localSlot = static_cast<UnsignedInt>(-1);
#ifdef MEMORYPOOL_DEBUG
	TheMemoryPoolFactory->debugSetInitFillerIndex(m_localSlot);
#endif
	m_packetRouterSlot = 0;	// parseUserList names the packet router
	for (i = 0; i < MAX_SLOTS; ++i) {
		m_packetRouterFallback[i] = static_cast<UnsignedInt>(-1);
	}

	for (i = 0; i < MAX_SLOTS; ++i) {
		m_fpsAverages[i] = -1;
	}
	for (i = 0; i < MAX_SLOTS; ++i) {
		m_latencyAverages[i] = 0.0; // using zero since all floating point standards should be able to specify 0.0 accurately.
	}
	m_smallestPacketArrivalCushion = static_cast<UnsignedInt>(-1);

	m_frameMetrics.init();

	m_disconnectManager = std::make_unique<DisconnectManager>();
	m_disconnectManager->init();

	m_netCommandWrapperList.reset(newInstance(NetCommandWrapperList));
	m_netCommandWrapperList->init();

	m_fileCommands.clear();
	m_fileRecipients.clear();
	for (i = 0; i < MAX_SLOTS; ++i) {
		m_fileProgress[i].clear();
	}
}

UnsignedInt ConnectionManager::getPingFrame()
{
	return (m_disconnectManager)?m_disconnectManager->getPingFrame():0;
}

Int ConnectionManager::getPingsSent()
{
	return (m_disconnectManager)?m_disconnectManager->getPingsSent():0;
}

Int ConnectionManager::getPingsRecieved()
{
	return (m_disconnectManager)?m_disconnectManager->getPingsRecieved():0;
}

void ConnectionManager::attachTransport(Transport *transport) {
	m_transport.reset(transport);
}

/**
 * zero out the command counts for the given frames.  Presently this is used for
 * the start of a game since there won't be any commands for the first few frames due to runahead.
 */
void ConnectionManager::zeroFrames(UnsignedInt startingFrame, UnsignedInt numFrames) {
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_frameData[i] != nullptr) {
//			DEBUG_LOG(("Calling zeroFrames on player %d, starting frame %d, numFrames %d\n", i, startingFrame, numFrames));
			m_frameData[i]->zeroFrames(startingFrame, numFrames);
		}
	}
}

/**
 * Destroy any game messages that are left over due to the run ahead.
 */
void ConnectionManager::destroyGameMessages() {
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		// Need to destroy these game messages because when the game ends, there are
		// still some game messages left over because of the run ahead aspect of
		// network play.
		if (m_frameData[i] != nullptr) {
			m_frameData[i]->destroyGameMessages();
		}
	}
}

/**
 * ConnectionManager::update
 * Update the connections. Tell them to do the receive and send.  Also relay
 * commands to their final destinations as necessary.
 */
void ConnectionManager::update(Bool isInGame) {
//
// 1. do this
// 2. do that
// 3. do something else
// 4. blow something up
// 5. bust some cap
//

	// The local address may be 0: a socket bound to every interface.
	if ((m_transport == nullptr) || (m_localPort == 0)) {
		DEBUG_ASSERTCRASH((m_transport != nullptr) && (m_localPort != 0), ("ConnectionManager doesn't have a transport or local port."));
		return;
	}

	m_transport->doRecv();

	if (isInGame) {
		m_disconnectManager->update(this);
		dropLaggingObservers();
	}

	// take the packets from the transport, break them up into commands, and give them to the appropriate connections.
	doRelay();

	// send any necessary keep-alive packets.
	doKeepAlive();

	for (Int i = 0; i < NUM_CONNECTIONS; ++i) {
		if (m_connections[i] != nullptr) {
			/*
			if (m_connections[i]->isQueueEmpty() == FALSE) {
//				DEBUG_LOG(("ConnectionManager::update - calling doSend on connection %d\n", i));
			}
			*/

			m_connections[i]->doSend();

			if (m_connections[i]->isQuitting() && m_connections[i]->isQueueEmpty())
			{
				DEBUG_LOG(("ConnectionManager::update - deleting connection for slot %d\n", i));
				m_connections[i].reset();
			}
		}

		if ((m_frameData[i] != nullptr) && (m_frameData[i]->getIsQuitting() == TRUE)) {
			if (m_frameData[i]->getQuitFrame() == TheGameLogic->getFrame()) {
				DEBUG_LOG(("ConnectionManager::update - deleting frame data for slot %d on quitting frame %d\n", i, m_frameData[i]->getQuitFrame()));
				m_frameData[i].reset();
			}
		}
	}

	m_transport->doSend();
	m_transport->recoverLostAddress();
}

/**
 * Keep-alive to one player per second, so idle connections stay open.
 */
void ConnectionManager::doKeepAlive() {
	time_t curTime = timeGetTime();

	if (m_keepAliveStart == 0) {
		m_keepAliveStart = curTime;
		return;
	}

	time_t numSeconds = (curTime - m_keepAliveStart) / 1000;

	while ((m_keepAliveNext <= numSeconds) && (m_keepAliveNext < MAX_SLOTS)) {
//		DEBUG_LOG(("ConnectionManager::doKeepAlive - trying to send keep alive message to player %d\n", m_keepAliveNext));
		if (m_connections[m_keepAliveNext] != nullptr) {
			NetKeepAliveCommandMsg *msg = newInstance(NetKeepAliveCommandMsg);
			msg->setPlayerID(m_localSlot);
			if (DoesCommandRequireACommandID(msg->getNetCommandType()) == TRUE) {
				msg->setID(GenerateNextCommandID());
			}
//			DEBUG_LOG(("ConnectionManager::doKeepAlive - sending keep alive message to player %d\n", m_keepAliveNext));
			sendLocalCommandDirect(msg, static_cast<UnsignedByte>(1 << m_keepAliveNext));
			msg->detach();
		}
		++m_keepAliveNext;
	}
	if (m_keepAliveNext == MAX_SLOTS) {
		m_keepAliveNext = 0;
		m_keepAliveStart = curTime;
	}
}

/**
 * Set the local address.
 */
void ConnectionManager::setLocalAddress(UnsignedInt ip, UnsignedInt port) {
	DEBUG_LOG(("ConnectionManager::setLocalAddress() - local address is %X:%d\n", ip, port));
	m_localAddr = ip;
	m_localPort = port;
}

/**
 * Initialize the transport object
 */
void ConnectionManager::initTransport() {
	DEBUG_ASSERTCRASH((m_transport == nullptr), ("m_transport already exists when trying to init it."));
	DEBUG_LOG(("ConnectionManager::initTransport - Initializing Transport\n"));
	m_transport = std::make_unique<Transport>();
	m_transport->reset();
	m_transport->init(m_localAddr, static_cast<UnsignedShort>(m_localPort));
}

/**
 * Returns true if all the commands for the given frame are ready to be executed.
 */
Bool ConnectionManager::allCommandsReady(UnsignedInt frame, Bool justTesting /* = FALSE */) {
	Bool retval = TRUE;
	// Initialized because no slot may meet the condition below (e.g. all m_frameData[i] == NULL), but
	// the value is checked afterwards.
	FrameDataReturnType frameRetVal = FRAMEDATA_READY;
	Int i;
	for (i = 0; (i < MAX_SLOTS) && retval; ++i) {
		// nobody waits for observers (dropLaggingObservers lets one go that falls too far behind)
		if (isObserver(i) && static_cast<UnsignedInt>(i) != m_localSlot) {
			continue;
		}
		if ((m_frameData[i] != nullptr) && (m_frameData[i]->getIsQuitting() == FALSE)) {
			frameRetVal = m_frameData[i]->allCommandsReady(frame, (frame != 0) && (justTesting == FALSE));
			if (frameRetVal == FRAMEDATA_NOTREADY) {
				retval = FALSE;
			} else if (frameRetVal == FRAMEDATA_RESEND) {
				requestFrameDataResend(i, frame);
				retval = FALSE;
			}
		}
	}

	if (frameRetVal == FRAMEDATA_RESEND) {
		// this frame's data is really screwed up, we need to clean it out so it can be resent to us.
		for (i = 0; i < MAX_SLOTS; ++i) {
			if ((m_frameData[i] != nullptr) && (static_cast<UnsignedInt>(i) != m_localSlot)) {
				m_frameData[i]->resetFrame(frame, FALSE);
			}
		}
	}

	if ((retval == TRUE) && (justTesting == FALSE)) {
		m_disconnectManager->allCommandsReady(TheGameLogic->getFrame(), this);
		retval = m_disconnectManager->allowedToContinue(); // allow the disconnect manager to keep us on this frame
																											// in case we are waiting for a new packet router or something.
	}

	return retval;
}

void ConnectionManager::handleAllCommandsReady(void)
{
	m_disconnectManager->allCommandsReady(TheGameLogic->getFrame(), this, FALSE);
}

/**
 * Only call this after making sure that all the commands are there for this frame.
 * After calling this the commands for this frame will be removed from the connection.
 *
 * BGC - To account for the case where the host disconnects without sending the
 *       same commands to all players, we now have to keep around the last 'run ahead'
 *       frames so we can potentially send those commands to the other players in the
 *       game so they can catch up.
 */
NetCommandList *ConnectionManager::getFrameCommandList(UnsignedInt frame) 
{
	NetCommandList *retlist = newInstance(NetCommandList);
	retlist->init();

	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_frameData[i] != nullptr) {
			// A player who left keeps its frame data for a while, but its commands for frames after the
			// leaving are not executed: a player removed by vote may have sent some, and not every
			// machine need have them.
			const Bool afterLeaving = m_frameData[i]->getIsQuitting() && frame + FRAMES_TO_KEEP + 1 > m_frameData[i]->getQuitFrame();
			if (isObserver(i)) {
				// Players do not wait for observers, so not every machine has an observer's commands for
				// this frame: they are not executed, on the observer's machine neither. Only its own
				// leaving takes it out of the game there; elsewhere it leaves on arrival.
				if (static_cast<UnsignedInt>(i) == m_localSlot) {
					NetCommandList *own = m_frameData[i]->getFrameCommandList(frame);
					for (NetCommandRef *ref = own->getFirstMessage(); ref != nullptr; ref = ref->getNext()) {
						if (ref->getCommand()->getNetCommandType() == NETCOMMANDTYPE_PLAYERLEAVE) {
							retlist->addMessage(ref->getCommand());
						}
					}
				}
			} else if (!afterLeaving) {
				retlist->appendList(m_frameData[i]->getFrameCommandList(frame));
			}
			if (frame > static_cast<UnsignedInt>(FRAMES_TO_KEEP)) {
				m_frameData[i]->resetFrame(frame - FRAMES_TO_KEEP);	// After getting the commands for that frame from this
													// FrameDataManager object, we need to tell it that we're
													// done with the messages for that frame.
				DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("getFrameCommandList - called reset frame on player %d for frame %d\n", i, frame - FRAMES_TO_KEEP));
			}
		}
	}

	return retlist; // retlist deallocated by calling function.
}

/**
 * Takes all the commands that are ready to send and sends them right now.
 */
void ConnectionManager::flushConnections() {
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_connections[i] != nullptr) {
//			DEBUG_LOG(("ConnectionManager::flushConnections - flushing connection to player %d\n", i));
			/*
			if (m_connections[i]->isQueueEmpty()) {
//				DEBUG_LOG(("ConnectionManager::flushConnections - connection queue empty\n"));
			}
			*/
			m_connections[i]->doSend();
		}
	}

	if (m_transport != nullptr) {
		m_transport->doSend();
	}
}

/**
 * Take a user list and make connections and frame data manager objects for each of the players.
 * For now, this is also how we'll determine the packet router fallback plan.
 */
void ConnectionManager::parseUserList(const GameInfo *game)
{
	if (!game)
		return;

	Int i;
	Int numUsers = 0;
	m_localSlot = static_cast<UnsignedInt>(-1);
	m_observers = 0;
	for (i = 0; i < MAX_SLOTS; ++i) {
		m_observerFrameInfo[i] = 0;
	}
	DEBUG_LOG(("Local slot is %d\n", game->getLocalSlotNum()));
	for (i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);	// badness, but since we cast right back to const, we should be ok
		if (slot->isHuman())
		{
			if (game->getLocalSlotNum() == i)
			{
				m_localSlot = i;
   			m_localUser->setName(slot->getName());
			}

			if (m_localSlot != static_cast<UnsignedInt>(i))
			{
				m_connections[i].reset(newInstance(Connection)());
				m_connections[i]->init();
				m_connections[i]->attachTransport(m_transport.get());
//				UnsignedShort port = (TheNAT)?TheNAT->getSlotPort(i):8088;
				UnsignedShort port = slot->getPort();
				m_connections[i]->setUser(newInstance(User)(slot->getName(), slot->getIP(), port));
				const ::Engine::SealSessionKeys *keys = game->getSealKeys(i);
				if (keys && m_transport)
					m_transport->setPeerKeys(slot->getIP(), port, *keys);
				m_frameData[i].reset(newInstance(FrameDataManager)(FALSE));
				DEBUG_LOG(("Remote user is at %X:%d\n", slot->getIP(), slot->getPort()));
			}
			else
			{
				DEBUG_LOG(("Local user is %d (%X:%d)\n", m_localSlot, slot->getIP(), slot->getPort()));
				m_frameData[i].reset(newInstance(FrameDataManager)(TRUE));
			}
			m_frameData[i]->init();
			m_frameData[i]->reset();

			if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER) {
				m_observers |= static_cast<UnsignedByte>(1 << i);
			}
			++numUsers;

		}
	}

	// The packet router relays every command: players first, so that an observer, who may fall
	// behind, does not hold the game up.
	Int fallback = 0;
	for (Int observers = 0; observers < 2; ++observers) {
		for (i = 0; i < MAX_SLOTS; ++i) {
			if (m_frameData[i] != nullptr && isObserver(i) == (observers == 1)) {
				m_packetRouterFallback[fallback++] = i;
			}
		}
	}
	if (fallback > 0) {
		m_packetRouterSlot = m_packetRouterFallback[0];
	}
#ifdef MEMORYPOOL_DEBUG
	TheMemoryPoolFactory->debugSetInitFillerIndex(m_localSlot);
#endif
}

/**
 * Return the number of incoming bytes per second averaged over 30 sec.
 */
Real ConnectionManager::getIncomingBytesPerSecond( void )
{
	if (m_transport)
		return m_transport->getIncomingBytesPerSecond();
	else
	  return 0.0;
}

/**
 * Return the number of incoming packets per second averaged over the last 30 sec.
 */
Real ConnectionManager::getIncomingPacketsPerSecond( void )
{
	if (m_transport)
		return m_transport->getIncomingPacketsPerSecond();
	else
	  return 0.0;
}

/**
 * Return the number of outgoing bytes per second averaged over the last 30 sec.
 */
Real ConnectionManager::getOutgoingBytesPerSecond( void )
{
	if (m_transport)
		return m_transport->getOutgoingBytesPerSecond();
	else
	  return 0.0;
}

/**
 * Return the number of outgoing packets per second averaged over the last 30 sec.
 */
Real ConnectionManager::getOutgoingPacketsPerSecond( void )
{
	if (m_transport) {
		return m_transport->getOutgoingPacketsPerSecond();
	} else {
	  return 0.0;
	}
}

/**
 * Return the number of bytes not from generals clients received per second averaged over the last 30 sec.
 */
Real ConnectionManager::getUnknownBytesPerSecond( void )
{
	if (m_transport)
		return m_transport->getUnknownBytesPerSecond();
	else
	  return 0.0;
}

/**
 * Return the number ov packets not from generals clients received per second averaged over the last 30 sec.
 */
Real ConnectionManager::getUnknownPacketsPerSecond( void )
{
	if (m_transport)
		return m_transport->getUnknownPacketsPerSecond();
	else
	  return 0.0;
}

#if defined(_DEBUG)
void ConnectionManager::debugPrintConnectionCommands() {
	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::debugPrintConnectionCommands - begin commands\n"));
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_connections[i] != nullptr) {
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::debugPrintConnectionCommands - commands for connection %d\n", i));
			m_connections[i]->debugPrintCommands();
		}
	}
	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::debugPrintConnectionCommands - end commands\n"));
}
#endif
