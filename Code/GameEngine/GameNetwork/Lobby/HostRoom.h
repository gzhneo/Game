/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// HostRoom.h -- a host's room at a rendezvous server.
//
// The host registers the room from both sockets at the first server that takes it and refreshes
// it; the invitation code names room, key fingerprint and server. The server announces joiners
// (KNOCK): the host probes them so its router opens for their HELLO. Once members are there, the
// host opens the relay session of the game with a token for every player. The first registration
// also asks the server for its neighbours.

#pragma once

#ifndef GAMENETWORK_LOBBY_HOSTROOM_H
#define GAMENETWORK_LOBBY_HOSTROOM_H

#include "Engine/NetAddress.h"
#include "Engine/Rendezvous.h"
#include "Engine/SessionMessage.h"
#include "GameNetwork/NetworkDefs.h"

#include <string>
#include <vector>

class Transport;

namespace Lobby
{

class RendezvousServers;

class HostRoom
{
public:
	/// What the room tells the server about the game.
	struct Report
	{
		std::string hostName;
		::Engine::SessionEndpoints endpoints;	///< ours
		// for the public game list; the server lists the room only if isPublic
		Bool isPublic = FALSE;
		std::string mapName;
		std::string fingerprint;
		UnsignedByte players = 0;
		UnsignedByte maxPlayers = 0;
	};

	/// A player of the relay session.
	struct RelayMember
	{
		Int slot = 0;
		UnsignedInt identity = 0;
	};

	enum class Answer : UnsignedByte
	{
		None,
		Registered,			///< the room stands for the first time: show the code
		RelayOpened,		///< tell the members their tokens
		ServersNamed,		///< neighbours of the server (see named())
	};

	void reset( void );
	/// A room for a new game, if servers are known.
	void start( Bool haveServers, UnsignedInt now );

	Bool active( void ) const { return m_active; }
	Bool registered( void ) const { return m_registered; }
	const ::Engine::NetAddress &server( void ) const { return m_server; }
	const std::string &serverText( void ) const { return m_serverText; }
	const std::string &code( void ) const { return m_code; }
	/// Tags of the requests in flight (answers must carry them).
	UnsignedInt tag( void ) const { return m_tag; }
	UnsignedInt relayTag( void ) const { return m_relayTag; }
	UnsignedInt serversTag( void ) const { return m_serversTag; }
	/// "ROOM-FINGERPRINT@server[:port]"; empty until registered.
	std::string inviteCode( const std::string &fingerprint ) const;

	enum class Refresh : UnsignedByte
	{
		Idle,			///< nothing due
		Sent,			///< registration sent, no answer yet
		Refreshed,		///< a standing room refreshed (keep the relay session alive as well)
		NoServer,		///< no server took the room; it stays inactive
	};
	/// Registers or refreshes the room when due.
	Refresh refresh( UnsignedInt now, RendezvousServers &servers, Bool withIPv6, const Report &report,
		Transport *lobbySocket, Transport *gameSocket );
	void unregister( Transport *lobbySocket );
	/// The report changed (listed or not, players): refresh at the next chance.
	void refreshSoon( UnsignedInt now );

	/// The relay session with a token for every member (at least two players).
	void openRelay( const std::vector<RelayMember> &members, Transport *lobbySocket );
	UnsignedInt64 relayId( void ) const { return m_relayId; }
	const UnsignedByte *relayToken( Int slot ) const { return m_relayTokens[slot]; }

	/// Probes joiners the server announced while that is due.
	void probeKnocks( UnsignedInt now, UnsignedInt64 sessionId, Transport *lobbySocket );

	/// An answer of the server (ignored unless it belongs to this room).
	Answer answer( const ::Engine::TreffMessage &message, const ::Engine::NetAddress &from, Bool gameSocket,
		UnsignedInt now, Transport *lobbySocket );
	/// The endpoint the server saw our lobby socket at (after Registered).
	const ::Engine::WireEndpoint &observedLobby( void ) const { return m_observedLobby; }
	const ::Engine::WireEndpoint &observedGame( void ) const { return m_observedGame; }
	const std::vector<std::string> &named( void ) const { return m_named; }

private:
	struct Knock
	{
		::Engine::NetAddress to;
		UnsignedInt until = 0;
		UnsignedInt next = 0;
	};

	Bool m_active = FALSE;
	Bool m_registered = FALSE;
	::Engine::NetAddress m_server;
	std::string m_serverText;
	std::string m_code;
	UnsignedByte m_owner[16] = {};
	UnsignedInt m_tag = 0;
	UnsignedInt m_nextRefresh = 0;
	Int m_serverIndex = 0;
	Int m_attempts = 0;

	std::vector<Knock> m_knocks;

	UnsignedInt64 m_relayId = 0;
	UnsignedByte m_relayTokens[MAX_SLOTS][16] = {};
	UnsignedInt m_relayTag = 0;
	Bool m_relayOpened = FALSE;

	Bool m_serversAsked = FALSE;
	UnsignedInt m_serversTag = 0;
	std::vector<std::string> m_named;

	::Engine::WireEndpoint m_observedLobby;
	::Engine::WireEndpoint m_observedGame;
};

} // namespace Lobby

#endif // GAMENETWORK_LOBBY_HOSTROOM_H
