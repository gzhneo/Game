/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/Lobby/PublicGames.h"

#include "Engine/InviteCode.h"
#include "GameNetwork/Lobby/LobbyTiming.h"
#include "GameNetwork/Lobby/LobbyWire.h"
#include "GameNetwork/Lobby/RendezvousServers.h"

#include <algorithm>

namespace Lobby
{

void PublicGames::reset( void )
{
	m_requests.clear();
	if (!m_entries.empty())
		++m_revision;
	m_entries.clear();
	m_next = 0;
	m_nextAsk = 0;
}

void PublicGames::poll( UnsignedInt now, RendezvousServers &servers, Bool withIPv6, Transport *lobbySocket )
{
	if (lobbySocket == nullptr || !due(now, m_nextAsk))
		return;
	std::erase_if(m_requests, [now]( const Request &r ) { return silentFor(now, r.sent, Timing::LIST_ANSWER); });

	const std::vector<std::string> &list = servers.list();
	if (list.empty())
	{
		m_nextAsk = now + Timing::LIST_ROUND;
		return;
	}
	if (m_next >= list.size())
		m_next = 0;
	const std::string server = list[m_next++];
	// one round through all servers per LIST_ROUND, but never faster than LIST_ASK_GAP
	const UnsignedInt gap = Timing::LIST_ROUND / static_cast<UnsignedInt>(list.size());
	m_nextAsk = now + std::max(gap, Timing::LIST_ASK_GAP);

	Request request;
	if (!servers.resolve(server, request.address, withIPv6))
		return;
	request.server = server;
	request.tag = randomTag();
	request.sent = now;
	std::erase_if(m_requests, [&request]( const Request &r ) { return r.server == request.server; });
	m_requests.push_back(request);

	::Engine::TreffMessage ask;
	ask.type = ::Engine::TreffType::LIST;
	ask.tag = request.tag;
	sendTreff(lobbySocket, ask, request.address, ::Engine::TREFF_LIST_SIZE);
}

Bool PublicGames::answer( const ::Engine::TreffMessage &message, const ::Engine::NetAddress &from, UnsignedInt now )
{
	if (message.type != ::Engine::TreffType::GAMES)
		return FALSE;
	const auto request = std::find_if(m_requests.begin(), m_requests.end(), [&]( const Request &r ) {
		return r.tag != 0 && r.tag == message.tag && r.address == from;
	});
	if (request == m_requests.end())
		return FALSE;
	const std::string server = request->server;
	m_requests.erase(request);

	std::erase_if(m_entries, [&server]( const Entry &e ) { return e.server == server; });
	for (const ::Engine::TreffGame &game : message.games)
	{
		if (m_entries.size() >= Limit::PUBLIC_GAMES)
			break;
		Entry entry;
		entry.server = server;
		entry.game = game;
		entry.heard = now;
		if (!inviteCode(entry).empty())
			m_entries.push_back(entry);
	}
	++m_revision;
	return TRUE;
}

Bool PublicGames::expire( UnsignedInt now )
{
	const size_t before = m_entries.size();
	std::erase_if(m_entries, [now]( const Entry &e ) { return silentFor(now, e.heard, Timing::LIST_KEEP); });
	if (m_entries.size() == before)
		return FALSE;
	++m_revision;
	return TRUE;
}

std::string PublicGames::inviteCode( const Entry &entry )
{
	const std::string room = ::Engine::canonicalCrockford(entry.game.room, 4);
	const std::string fingerprint = ::Engine::canonicalCrockford(entry.game.fingerprint, 8);
	if (room.empty() || fingerprint.empty())
		return std::string();
	return inviteCodeAt(room, fingerprint, entry.server);
}

} // namespace Lobby
