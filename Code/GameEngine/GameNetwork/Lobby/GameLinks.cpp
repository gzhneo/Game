/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/Lobby/GameLinks.h"

#include "GameNetwork/LANGameInfo.h"
#include "GameNetwork/Lobby/LobbyTiming.h"
#include "GameNetwork/Lobby/LobbyWire.h"

#include <cstring>

namespace Lobby
{

namespace
{
	constexpr char PROBE_CONTEXT[] = "GZH Neo probe";
}

void GameLinks::reset( void )
{
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
		m_members[slot] = Member();
	m_relayServer = ::Engine::NetAddress();
	m_relayServerText.clear();
	m_relayId = 0;
	std::memset(m_relayToken, 0, sizeof(m_relayToken));
	m_relayBound = FALSE;
	m_nextRelayBind = 0;
	m_relayBindTag = 0;
}

Int GameLinks::slotOf( UnsignedInt identity ) const
{
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
		if (identity != 0 && m_members[slot].identity == identity)
			return slot;
	return -1;
}

Bool GameLinks::setMember( Int slot, UnsignedInt identity, const UnsignedByte *key, UnsignedInt now )
{
	if (!valid(slot) || key == nullptr)
		return FALSE;
	Member &m = m_members[slot];
	if (m.identity == identity && std::memcmp(m.key, key, sizeof(m.key)) == 0)
		return FALSE;
	m.identity = identity;
	std::memcpy(m.key, key, sizeof(m.key));
	m.link = ::Engine::PeerLink(identity, randomTag(), now);
	if (identity == m_context.ownIdentity || m_context.gameSocket == nullptr || m_context.ownKeys == nullptr)
		return TRUE;
	// the keys belong to this pair and this game
	UnsignedByte context[sizeof(PROBE_CONTEXT) + 8];
	std::memcpy(context, PROBE_CONTEXT, sizeof(PROBE_CONTEXT));
	for (Int b = 0; b < 8; ++b)
		context[sizeof(PROBE_CONTEXT) + b] = static_cast<UnsignedByte>(m_context.sessionId >> (8 * b));
	::Engine::SealSessionKeys keys;
	if (::Engine::deriveSessionKeys(*m_context.ownKeys, key, context, sizeof(context), keys))
		m_context.gameSocket->setPeerKeys(identity, m_context.gamePort, keys);
	keys.wipe();
	return TRUE;
}

void GameLinks::clearMember( Int slot )
{
	if (!valid(slot))
		return;
	const UnsignedInt identity = m_members[slot].identity;
	if (identity != 0 && m_context.gameSocket != nullptr && slotOf(identity) == slot)
	{
		m_context.gameSocket->removePeerKeys(identity);
		m_context.gameSocket->setRelayed(identity, FALSE);
	}
	m_members[slot] = Member();
}

void GameLinks::applyKeys( LANGameInfo &game ) const
{
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		const LANGameSlot *s = game.getConstLANSlot(slot);
		const Int record = s->isHuman() ? slotOf(s->getIP()) : -1;
		game.setPublicKey(slot, record >= 0 ? m_members[record].key : nullptr);
	}
}

void GameLinks::probe( UnsignedInt now, const std::function<void( Int slot )> &relayUsed )
{
	Transport *socket = m_context.gameSocket;
	if (socket == nullptr)
		return;
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		Member &m = m_members[slot];
		if (m.identity == 0 || m.identity == m_context.ownIdentity)
			continue;
		if (m.link.update(now, m_relayBound, m_context.forceRelay) == ::Engine::PeerLink::Change::UseRelay)
		{
			socket->setRelayed(m.identity, TRUE);
			DEBUG_LOG(("GameLinks - the way to slot %d goes through the relay%s\n", static_cast<int>(slot), m_context.forceRelay ? " (forced)" : ""));
			if (relayUsed && !m_context.forceRelay)
				relayUsed(slot);
		}
		if (!m.link.takeProbe(now))
			continue;
		::Engine::SessionMessage probe;
		probe.type = ::Engine::SessionType::PROBE;
		probe.sessionId = m_context.sessionId;
		probe.identity = m_context.ownIdentity;
		probe.value = m.link.nonce();
		Encoded wire;
		if (!encode(probe, wire))
			continue;
		for (const ::Engine::WireEndpoint &e : m.endpoints.game)
		{
			if (e.isSet())
				socket->sendSealedTo(m.identity, netAddress(e), wire.data, wire.size);
		}
	}
}

void GameLinks::answerProbe( const ::Engine::SessionMessage &probe, const Transport::RawDatagram &d )
{
	Transport *socket = m_context.gameSocket;
	if (socket == nullptr || m_context.sessionId == 0 || probe.sessionId != m_context.sessionId || probe.identity != d.sender)
		return;
	::Engine::SessionMessage ack;
	ack.type = ::Engine::SessionType::PROBE_ACK;
	ack.sessionId = m_context.sessionId;
	ack.identity = m_context.ownIdentity;
	ack.value = probe.value;
	Encoded wire;
	if (!encode(ack, wire))
		return;
	if (d.relayed)
		socket->sendSealed(d.sender, m_context.gamePort, wire.data, wire.size);
	else
		socket->sendSealedTo(d.sender, d.from, wire.data, wire.size);
}

void GameLinks::probeAnswered( const ::Engine::SessionMessage &answer, const Transport::RawDatagram &d )
{
	Transport *socket = m_context.gameSocket;
	if (socket == nullptr || answer.sessionId != m_context.sessionId || answer.identity != d.sender || d.relayed)
		return;
	const Int slot = slotOf(d.sender);
	if (slot < 0)
		return;
	Member &m = m_members[slot];
	const Bool wasDirect = m.link.direct();
	const ::Engine::PeerLink::Change change = m.link.answered(answer.value, m_context.forceRelay);
	if (!wasDirect && m.link.direct())
		socket->bindPeer(d.sender, d.from);
	if (change == ::Engine::PeerLink::Change::UseDirect)
	{
		socket->setRelayed(d.sender, FALSE);
		DEBUG_LOG(("GameLinks - the way to slot %d is direct again\n", static_cast<int>(slot)));
	}
}

void GameLinks::joinRelay( const ::Engine::NetAddress &server, const std::string &serverText, UnsignedInt64 relayId,
	const UnsignedByte token[16], UnsignedInt now )
{
	if (relayId == m_relayId && server == m_relayServer && std::memcmp(token, m_relayToken, sizeof(m_relayToken)) == 0)
		return;
	m_relayServer = server;
	m_relayServerText = serverText;
	m_relayId = relayId;
	std::memcpy(m_relayToken, token, sizeof(m_relayToken));
	m_relayBound = FALSE;
	m_nextRelayBind = now;
}

void GameLinks::bindRelay( UnsignedInt now )
{
	if (m_context.gameSocket == nullptr || m_relayId == 0 || !due(now, m_nextRelayBind))
		return;
	::Engine::TreffMessage bind;
	bind.type = ::Engine::TreffType::RELAY_BIND;
	m_relayBindTag = randomTag();
	bind.tag = m_relayBindTag;
	bind.relayId = m_relayId;
	bind.identity = m_context.ownIdentity;
	std::memcpy(bind.token, m_relayToken, sizeof(bind.token));
	bind.socket = ::Engine::TreffSocket::GAME;
	sendTreff(m_context.gameSocket, bind, m_relayServer);
	m_nextRelayBind = now + (m_relayBound ? Timing::RELAY_BIND_REFRESH : Timing::RELAY_BIND_RETRY);
}

::Engine::WireEndpoint GameLinks::relayBoundAnswer( const ::Engine::TreffMessage &answer, const ::Engine::NetAddress &from, UnsignedInt now )
{
	if (m_context.gameSocket == nullptr || answer.tag != m_relayBindTag || from != m_relayServer || answer.relayId != m_relayId)
		return ::Engine::WireEndpoint();
	if (!m_relayBound)
	{
		m_relayBound = TRUE;
		m_nextRelayBind = now + Timing::RELAY_BIND_REFRESH;
		m_context.gameSocket->setRelay(m_relayServer, m_relayId, m_context.ownIdentity);
	}
	return answer.observed;
}

} // namespace Lobby
