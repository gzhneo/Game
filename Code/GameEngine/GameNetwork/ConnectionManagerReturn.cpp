/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ConnectionManager: a player dropped for not answering comes back.
//
// The disconnect procedure drops a player in the same frame on every machine; from then on it is
// returnable until its units are destroyed. The packet router destroys them once the return window
// has run out. A player who comes back has a new key and socket: the host announces it with a
// PLAYERRETURN command, and in that command's frame every machine opens a connection to it again
// and waits for its commands from the return frame on. The frames before come to the returning
// player from the host's replay; at the return frame it takes over the network state the host had
// there (captureReturnState, applyReturnState).

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/ConnectionManager.h"

#include "Engine/NetAddressBook.h"
#include "Engine/SessionHandshake.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/networkutil.h"

static_assert(MAX_SLOTS == ::Engine::SESSION_SLOTS, "the return state has a field per slot");

void ConnectionManager::markReturnable(Int slot)
{
	if (slot < 0 || slot >= MAX_SLOTS || static_cast<UnsignedInt>(slot) == m_localSlot || isObserver(slot)) {
		return;
	}
	ReturnSlot &r = m_returns[slot];
	r.returnable = TRUE;
	r.dropFrame = TheGameLogic->getFrame();
	r.destroySent = FALSE;
	DEBUG_LOG(("ConnectionManager::markReturnable - slot %d may come back (dropped on frame %d)\n", slot, r.dropFrame));
}

Bool ConnectionManager::isReturnable(Int slot) const
{
	return slot >= 0 && slot < MAX_SLOTS && m_returns[slot].returnable;
}

UnsignedInt ConnectionManager::getDropFrame(Int slot) const
{
	return isReturnable(slot) ? m_returns[slot].dropFrame : 0;
}

Bool ConnectionManager::isAwaitedReturn(Int slot) const
{
	return isReturnable(slot) && m_connections[slot] == nullptr;
}

void ConnectionManager::closeReturn(Int slot)
{
	if (slot >= 0 && slot < MAX_SLOTS) {
		m_returns[slot] = ReturnSlot();
	}
}

Bool ConnectionManager::admitReturningPlayer(NetPlayerReturnCommandMsg *msg, UnsignedInt localTicked)
{
	const Int slot = msg->getSlot();
	const UnsignedInt frame = TheGameLogic->getFrame();
	// The same test on every machine: the drop and the destruction of the units happen in frames.
	if (!isReturnable(slot) || m_connections[slot] != nullptr || m_frameData[slot] != nullptr || msg->getReturnFrame() <= frame
		|| TheGameInfo == nullptr) {
		DEBUG_LOG(("ConnectionManager::admitReturningPlayer - slot %d cannot come back on frame %d\n", slot, frame));
		return FALSE;
	}
	GameSlot *gameSlot = TheGameInfo->getSlot(slot);
	if (gameSlot == nullptr || !gameSlot->isHuman()) {
		return FALSE;
	}
	const UnsignedInt oldIdentity = gameSlot->getIP();
	const UnsignedInt identity = ::Engine::SessionHandshake::identityOf(msg->getKey());
	const ::Engine::SealSessionKeys *keys = TheGameInfo->replaceSlotKey(slot, msg->getKey());
	gameSlot->setIP(identity);
	gameSlot->setPort(msg->getPort());
	gameSlot->markAsReturned();
	if (m_transport != nullptr) {
		if (oldIdentity != identity) {
			m_transport->removePeerKeys(oldIdentity);
		}
		// Without keys (the machine's own key is unknown here) its packets are refused, as at the start.
		if (keys != nullptr) {
			m_transport->setPeerKeys(identity, msg->getPort(), *keys);
		}
		if (msg->getEndpointFamily() == 4 || msg->getEndpointFamily() == 6) {
			::Engine::NetAddress endpoint;
			if (msg->getEndpointFamily() == 4) {
				endpoint.family = ::Engine::NetAddress::Family::V4;
			} else {
				endpoint.family = ::Engine::NetAddress::Family::V6;
			}
			memcpy(endpoint.bytes, msg->getEndpointAddress(), sizeof(endpoint.bytes));
			endpoint.port = msg->getEndpointPort();
			m_transport->bindPeer(identity, endpoint);
		}
	}

	m_connections[slot].reset(newInstance(Connection)());
	m_connections[slot]->init();
	m_connections[slot]->attachTransport(m_transport.get());
	m_connections[slot]->setUser(newInstance(User)(gameSlot->getName(), identity, msg->getPort()));
	m_frameData[slot].reset(newInstance(FrameDataManager)(FALSE));
	m_frameData[slot]->init();
	m_frameData[slot]->reset();
	m_frameData[slot]->setJoinFrame(msg->getReturnFrame());
	m_arrivedIds[slot] = ::Engine::RecentIds<128>();
	m_fpsAverages[slot] = -1;
	m_latencyAverages[slot] = 0.0;

	// last in line to relay
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_packetRouterFallback[i] == static_cast<UnsignedInt>(-1)) {
			m_packetRouterFallback[i] = slot;
			break;
		}
	}
	m_returns[slot] = ReturnSlot();
	m_disconnectManager->playerReturned(slot, this);
	DEBUG_LOG(("ConnectionManager::admitReturningPlayer - slot %d is back as %X on frame %d, commands from frame %d on\n",
		slot, identity, frame, msg->getReturnFrame()));

	if (isPacketRouter()) {
		sendReturnBacklog(slot, msg->getReturnFrame(), localTicked);
	}
	m_events->playerReturned(slot, gameSlot->getName());
	m_events->playersChanged();
	return TRUE;
}

/**
 * The commands of the others for frames from the return frame on that the packet router has already
 * relayed: the returning player was not there yet. What arrives later is relayed to it as usual.
 */
void ConnectionManager::sendReturnBacklog(Int slot, UnsignedInt returnFrame, UnsignedInt localTicked)
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	// beyond this the ring of frame data holds nothing yet
	const UnsignedInt end = frame + FRAME_DATA_LENGTH - FRAMES_TO_KEEP;
	const UnsignedByte relay = static_cast<UnsignedByte>(1 << slot);
	Int sent = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (i == slot || m_frameData[i] == nullptr || m_frameData[i]->getIsQuitting()) {
			continue;
		}
		const Bool local = static_cast<UnsignedInt>(i) == m_localSlot;
		for (UnsignedInt f = returnFrame; f < end; ++f) {
			NetCommandList *list = m_frameData[i]->getFrameCommandList(f);
			for (NetCommandRef *ref = list != nullptr ? list->getFirstMessage() : nullptr; ref != nullptr; ref = ref->getNext()) {
				if (ref->getCommand()->getExecutionFrame() == f) {
					sendLocalCommandDirect(ref->getCommand(), relay);
					++sent;
				}
			}
			// a count only where it is final: ours once sent, the others' once it arrived
			const UnsignedInt count = m_frameData[i]->getFrameCommandCount(f);
			if ((local && f > localTicked) || (!local && count == static_cast<UnsignedInt>(-1))) {
				continue;
			}
			NetFrameCommandMsg *info = newInstance(NetFrameCommandMsg);
			info->setExecutionFrame(f);
			info->setCommandCount(static_cast<UnsignedShort>(count));
			if (DoesCommandRequireACommandID(info->getNetCommandType())) {
				info->setID(GenerateNextCommandID());
			}
			info->setPlayerID(i);
			sendLocalCommandDirect(info, relay);
			info->detach();
			++sent;
		}
	}
	DEBUG_LOG(("ConnectionManager::sendReturnBacklog - %d commands and counts of frames from %d on to slot %d\n", sent, returnFrame, slot));
}

void ConnectionManager::sendPlayerDestruct(Int slot, UnsignedInt executionFrame)
{
	NetDestroyPlayerCommandMsg *msg = newInstance(NetDestroyPlayerCommandMsg);
	msg->setExecutionFrame(executionFrame);
	msg->setPlayerID(m_localSlot);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}
	msg->setPlayerIndex(slot);
	DEBUG_LOG(("ConnectionManager::sendPlayerDestruct - the return window of slot %d is over, its units go on frame %d\n",
		slot, executionFrame));
	sendLocalCommand(msg);
	msg->detach();
}

void ConnectionManager::expireReturns(UnsignedInt frame, UnsignedInt executionFrame)
{
	if (!isPacketRouter()) {
		return;
	}
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		ReturnSlot &r = m_returns[i];
		if (!r.returnable || r.destroySent || m_connections[i] != nullptr || frame < r.dropFrame + m_returnWindowFrames) {
			continue;
		}
		r.destroySent = TRUE;
		sendPlayerDestruct(i, executionFrame);
	}
}

void ConnectionManager::captureReturnState(::Engine::SessionReturnState &out)
{
	out = ::Engine::SessionReturnState();
	out.frame = TheGameLogic->getFrame();
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (isPlayerConnected(i) && (m_frameData[i] != nullptr) && !m_frameData[i]->getIsQuitting()) {
			out.connected |= static_cast<std::uint8_t>(1 << i);
		}
		const UnsignedInt fallback = m_packetRouterFallback[i];
		out.fallback[i] = fallback < MAX_SLOTS ? static_cast<std::uint8_t>(fallback) : 0xFF;
		out.dropFrame[i] = isReturnable(i) ? m_returns[i].dropFrame : 0;
	}
	out.router = m_packetRouterSlot < MAX_SLOTS ? static_cast<std::uint8_t>(m_packetRouterSlot) : 0xFF;
}

void ConnectionManager::restartFrameData()
{
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_frameData[i] != nullptr) {
			m_frameData[i]->destroyGameMessages();
			m_frameData[i]->reset();
		}
	}
}

/**
 * The returning player takes over the state the host had at the start of the return frame: who is
 * still in the game, who relays, who may come back.
 */
void ConnectionManager::applyReturnState(const ::Engine::SessionReturnState &state)
{
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (static_cast<UnsignedInt>(i) == m_localSlot) {
			continue;
		}
		const Bool inGame = ((state.connected >> i) & 1) != 0;
		if (!inGame && (m_connections[i] != nullptr || m_frameData[i] != nullptr)) {
			// gone before we came back: no leaving to report
			DEBUG_LOG(("ConnectionManager::applyReturnState - slot %d is no longer in the game\n", i));
			m_connections[i].reset();
			m_frameData[i].reset();
			if (TheGameInfo != nullptr && TheGameInfo->getSlot(i) != nullptr && TheGameInfo->getSlot(i)->isHuman()) {
				TheGameInfo->getSlot(i)->setLastFrameInGame(state.frame);
			}
		} else if (inGame && m_connections[i] == nullptr) {
			DEBUG_LOG(("ConnectionManager::applyReturnState - slot %d is in the game, but we know nothing of it\n", i));
		}
		m_returns[i] = ReturnSlot();
		if (state.dropFrame[i] != 0 && !inGame) {
			m_returns[i].returnable = TRUE;
			m_returns[i].dropFrame = state.dropFrame[i];
		}
	}
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		m_packetRouterFallback[i] = state.fallback[i] < MAX_SLOTS ? state.fallback[i] : static_cast<UnsignedInt>(-1);
	}
	if (state.router < MAX_SLOTS) {
		m_packetRouterSlot = state.router;
	}
	m_events->playersChanged();
}
