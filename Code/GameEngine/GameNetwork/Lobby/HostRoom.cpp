/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/Lobby/HostRoom.h"

#include "Common/GlobalData.h"
#include "Engine/InviteCode.h"
#include "Engine/PacketSeal.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/Lobby/LobbyTiming.h"
#include "GameNetwork/Lobby/LobbyWire.h"
#include "GameNetwork/Lobby/RendezvousServers.h"
#include "GameNetwork/Transport.h"

#include <algorithm>
#include <cstring>

namespace Lobby
{

void HostRoom::reset( void )
{
	*this = HostRoom();
}

void HostRoom::start( Bool haveServers, UnsignedInt now )
{
	reset();
	m_active = haveServers;
	m_nextRefresh = now;
}

std::string HostRoom::inviteCode( const std::string &fingerprint ) const
{
	if (!m_registered)
		return std::string();
	return inviteCodeAt(m_code, fingerprint, m_serverText);
}

HostRoom::Refresh HostRoom::refresh( UnsignedInt now, RendezvousServers &servers, Bool withIPv6, const Report &report,
	Transport *lobbySocket, Transport *gameSocket )
{
	if (!m_active || !due(now, m_nextRefresh))
		return Refresh::Idle;
	if (!m_registered && m_attempts >= Limit::ROOM_ATTEMPTS)
	{
		++m_serverIndex;
		m_attempts = 0;
		m_serverText.clear();
	}
	const std::vector<std::string> &list = servers.list();
	while (m_serverText.empty())
	{
		if (m_serverIndex >= static_cast<Int>(list.size()))
		{
			m_active = FALSE;
			return Refresh::NoServer;
		}
		const std::string &candidate = list[static_cast<size_t>(m_serverIndex)];
		if (servers.resolve(candidate, m_server, withIPv6))
			m_serverText = candidate;
		else
			++m_serverIndex;
	}
	if (m_code.empty())
	{
		m_code = ::Engine::inviteRoomFromBits(randomTag());
		if (!::Engine::secureRandom(m_owner, sizeof(m_owner)))
			return Refresh::Idle;
	}
	if (m_tag == 0)
		m_tag = randomTag();

	::Engine::TreffMessage reg;
	reg.type = ::Engine::TreffType::REGISTER;
	reg.tag = m_tag;
	reg.protocol = LAN_PROTOCOL_VERSION;
	reg.room = m_code;
	std::memcpy(reg.owner, m_owner, sizeof(reg.owner));
	reg.socket = ::Engine::TreffSocket::LOBBY;
	reg.isPublic = report.isPublic != FALSE;
	reg.game.gameName = report.hostName.substr(0, 48);
	reg.game.hostName = reg.game.gameName;
	reg.game.mapName = report.mapName.substr(0, 160);
	reg.game.fingerprint = report.fingerprint.substr(0, 8);
	reg.game.players = report.players;
	reg.game.maxPlayers = report.maxPlayers;
	reg.game.programCRC = TheGlobalData->m_exeCRC;
	reg.game.dataCRC = TheGlobalData->m_iniCRC;
	for (Int i = 0; i < ::Engine::TREFF_MAX_ENDPOINTS; ++i)
	{
		reg.lobby[i] = report.endpoints.lobby[i];
		reg.gameSocket[i] = report.endpoints.game[i];
	}
	sendTreff(lobbySocket, reg, m_server);
	// the game socket as an extra binding of the same room
	reg.socket = ::Engine::TreffSocket::GAME;
	sendTreff(gameSocket, reg, m_server);

	if (m_registered)
	{
		m_nextRefresh = now + Timing::ROOM_REFRESH;
		return Refresh::Refreshed;
	}
	++m_attempts;
	m_nextRefresh = now + Timing::ROOM_RETRY;
	return Refresh::Sent;
}

void HostRoom::refreshSoon( UnsignedInt now )
{
	if (m_registered)
		m_nextRefresh = now;
}

void HostRoom::unregister( Transport *lobbySocket )
{
	if (m_registered)
	{
		::Engine::TreffMessage m;
		m.type = ::Engine::TreffType::UNREGISTER;
		m.room = m_code;
		std::memcpy(m.owner, m_owner, sizeof(m.owner));
		sendTreff(lobbySocket, m, m_server);
	}
	reset();
}

void HostRoom::openRelay( const std::vector<RelayMember> &members, Transport *lobbySocket )
{
	if (!m_registered || members.size() < 2)
		return;
	::Engine::TreffMessage open;
	open.type = ::Engine::TreffType::RELAY_OPEN;
	static const UnsignedByte none[16] = {};
	for (const RelayMember &member : members)
	{
		UnsignedByte (&token)[16] = m_relayTokens[member.slot];
		if (std::memcmp(token, none, sizeof(none)) == 0 && !::Engine::secureRandom(token, sizeof(token)))
			continue;
		::Engine::TreffRelayMember entry;
		entry.identity = member.identity;
		::Engine::relayTokenHash(token, entry.tokenHash);
		open.relayMembers.push_back(entry);
	}
	while (m_relayId == 0)
	{
		if (!::Engine::secureRandom(&m_relayId, sizeof(m_relayId)))
			return;
	}
	open.room = m_code;
	std::memcpy(open.owner, m_owner, sizeof(open.owner));
	open.relayId = m_relayId;
	m_relayTag = randomTag();
	open.tag = m_relayTag;
	sendTreff(lobbySocket, open, m_server);
}

void HostRoom::probeKnocks( UnsignedInt now, UnsignedInt64 sessionId, Transport *lobbySocket )
{
	std::erase_if(m_knocks, [now]( const Knock &k ) { return due(now, k.until); });
	for (Knock &knock : m_knocks)
	{
		if (!due(now, knock.next))
			continue;
		::Engine::SessionMessage probe;
		probe.type = ::Engine::SessionType::PROBE;
		probe.sessionId = sessionId;
		Encoded wire;
		if (lobbySocket != nullptr && encode(probe, wire))
			lobbySocket->sendRaw(knock.to, wire.data, wire.size);
		knock.next = now + Timing::KNOCK_PROBE_INTERVAL;
	}
}

HostRoom::Answer HostRoom::answer( const ::Engine::TreffMessage &m, const ::Engine::NetAddress &from, Bool gameSocket,
	UnsignedInt now, Transport *lobbySocket )
{
	if (!m_active || from != m_server)
		return Answer::None;
	switch (m.type)
	{
		case ::Engine::TreffType::REGISTER_ACK:
		{
			if (m.tag != m_tag || m.room != m_code)
				return Answer::None;
			if (gameSocket)
			{
				m_observedGame = m.observed;
				return Answer::None;
			}
			m_observedLobby = m.observed;
			if (m_registered)
				return Answer::None;
			m_registered = TRUE;
			m_attempts = 0;
			m_nextRefresh = now + Timing::ROOM_REFRESH;
			if (!m_serversAsked)
			{
				m_serversAsked = TRUE;
				::Engine::TreffMessage ask;
				ask.type = ::Engine::TreffType::SERVERS;
				m_serversTag = randomTag();
				ask.tag = m_serversTag;
				sendTreff(lobbySocket, ask, m_server, ::Engine::TREFF_LIST_SIZE);
			}
			return Answer::Registered;
		}
		case ::Engine::TreffType::KNOCK:
			if (gameSocket || !m_registered || m.room != m_code)
				return Answer::None;
			for (const ::Engine::WireEndpoint &e : m.lobby)
			{
				if (!e.isSet() || m_knocks.size() >= Limit::KNOCKS)
					continue;
				Knock knock;
				knock.to = netAddress(e);
				knock.until = now + Timing::KNOCK_PROBES;
				knock.next = now;
				m_knocks.push_back(knock);
			}
			return Answer::None;
		case ::Engine::TreffType::RELAY_OPENED:
			if (gameSocket || m_relayTag == 0 || m.tag != m_relayTag || m.relayId != m_relayId)
				return Answer::None;
			m_relayOpened = TRUE;
			return Answer::RelayOpened;
		case ::Engine::TreffType::SERVER_LIST:
			if (gameSocket || m_serversTag == 0 || m.tag != m_serversTag)
				return Answer::None;
			m_serversTag = 0;
			m_named = m.servers;
			return Answer::ServersNamed;
		case ::Engine::TreffType::REFUSED:
			if (gameSocket)
				return Answer::None;
			if (!m_registered && m.tag == m_tag)
			{
				// a taken room gets a new name; anything else moves on to the next server
				if (m.reason == ::Engine::TreffRefusal::ROOM_TAKEN)
					m_code.clear();
				else
					m_attempts = Limit::ROOM_ATTEMPTS;
				m_nextRefresh = now;
			}
			else if (m_relayTag != 0 && m.tag == m_relayTag)
			{
				DEBUG_LOG(("HostRoom - the rendezvous server keeps no relay for us (reason %d)\n", static_cast<Int>(m.reason)));
				m_relayTag = 0;
			}
			return Answer::None;
		default:
			return Answer::None;
	}
}

} // namespace Lobby
