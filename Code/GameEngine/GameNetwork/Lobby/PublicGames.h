/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PublicGames.h -- the games rendezvous servers list publicly.
//
// The known servers are asked in turn (LIST); each answer replaces what that server listed before.
// A listed game is joined by its invitation code, so the joiner checks the host's key against the
// fingerprint the host registered with.

#pragma once

#ifndef GAMENETWORK_LOBBY_PUBLICGAMES_H
#define GAMENETWORK_LOBBY_PUBLICGAMES_H

#include "Engine/NetAddress.h"
#include "Engine/Rendezvous.h"

#include <string>
#include <vector>

class Transport;

namespace Lobby
{

class RendezvousServers;

class PublicGames
{
public:
	struct Entry
	{
		std::string server;			///< as in the server list ("host[:port]")
		::Engine::TreffGame game;
		UnsignedInt heard = 0;
	};

	void reset( void );
	/// Asks the next server for its list when due.
	void poll( UnsignedInt now, RendezvousServers &servers, Bool withIPv6, Transport *lobbySocket );
	/// A list a server sent; FALSE if it answers no request of ours.
	Bool answer( const ::Engine::TreffMessage &message, const ::Engine::NetAddress &from, UnsignedInt now );
	/// Drops games no server listed for a while; TRUE if any went.
	Bool expire( UnsignedInt now );

	const std::vector<Entry> &entries( void ) const { return m_entries; }
	/// Counts every change of the entries.
	UnsignedInt revision( void ) const { return m_revision; }

	/// The invitation code that joins an entry; empty if its room or fingerprint is malformed.
	static std::string inviteCode( const Entry &entry );

private:
	struct Request
	{
		std::string server;
		::Engine::NetAddress address;
		UnsignedInt tag = 0;
		UnsignedInt sent = 0;
	};

	std::vector<Request> m_requests;
	std::vector<Entry> m_entries;
	size_t m_next = 0;
	UnsignedInt m_nextAsk = 0;
	UnsignedInt m_revision = 0;
};

} // namespace Lobby

#endif // GAMENETWORK_LOBBY_PUBLICGAMES_H
