/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GameLinks.h -- the players of a game and the ways between their game sockets.
//
// Every player sends sealed probes (keys of their own for each pair) to every endpoint it knows
// of every other player's game socket, so the routers on both sides open; the first answer fixes
// the way (Engine::PeerLink). A player that stays silent is reached through the relay session of
// the game, to which our game socket binds with its token.

#pragma once

#ifndef GAMENETWORK_LOBBY_GAMELINKS_H
#define GAMENETWORK_LOBBY_GAMELINKS_H

#include "Engine/PacketSeal.h"
#include "Engine/PeerLink.h"
#include "Engine/Rendezvous.h"
#include "Engine/SessionMessage.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/Transport.h"

#include <functional>
#include <string>

class LANGameInfo;

namespace Lobby
{

struct Member
{
	UnsignedInt identity = 0;
	UnsignedByte key[::Engine::SEAL_KEY_SIZE] = {};
	::Engine::SessionEndpoints endpoints;	///< where the player's sockets are reached
	::Engine::PeerLink link;				///< our game socket towards the player's
};

class GameLinks
{
public:
	/// What the links of one game work with.
	struct Context
	{
		Transport *gameSocket = nullptr;				///< not owned
		const ::Engine::SealKeyPair *ownKeys = nullptr;
		UnsignedInt64 sessionId = 0;
		UnsignedInt ownIdentity = 0;
		UnsignedShort gamePort = 0;
		Bool forceRelay = FALSE;						///< tests: every way through the relay
	};

	void reset( void );
	void setContext( const Context &context ) { m_context = context; }
	const Context &context( void ) const { return m_context; }

	Member &member( Int slot ) { return m_members[slot]; }
	const Member &member( Int slot ) const { return m_members[slot]; }
	/// The slot whose record holds this identity; -1 for none.
	Int slotOf( UnsignedInt identity ) const;

	/// A player's key. Derives the probe keys for our game socket; false if nothing changed.
	Bool setMember( Int slot, UnsignedInt identity, const UnsignedByte *key, UnsignedInt now );
	/// Forgets the record of a slot and the keys of its player on the game socket.
	void clearMember( Int slot );
	/// Keys of every player into the slots of the game, by identity.
	void applyKeys( LANGameInfo &game ) const;

	Bool reachable( Int slot ) const { return valid(slot) && m_members[slot].link.direct(); }
	Bool relayed( Int slot ) const { return valid(slot) && m_members[slot].link.relayed(); }

	/// Probes what is due; `relayUsed` hears of each player that goes through the relay from now on.
	void probe( UnsignedInt now, const std::function<void( Int slot )> &relayUsed );
	/// A probe of a player on our game socket: answered where it came from.
	void answerProbe( const ::Engine::SessionMessage &probe, const Transport::RawDatagram &datagram );
	/// The answer to one of our probes: the direct way works.
	void probeAnswered( const ::Engine::SessionMessage &answer, const Transport::RawDatagram &datagram );

	// relay
	/// The relay session of the game and our token for it (from the host).
	void joinRelay( const ::Engine::NetAddress &server, const std::string &serverText, UnsignedInt64 relayId,
		const UnsignedByte token[16], UnsignedInt now );
	Bool relayJoined( void ) const { return m_relayId != 0; }
	Bool relayBound( void ) const { return m_relayBound; }
	UnsignedInt64 relayId( void ) const { return m_relayId; }
	const ::Engine::NetAddress &relayServer( void ) const { return m_relayServer; }
	UnsignedInt relayBindTag( void ) const { return m_relayBindTag; }
	/// Binds (and keeps binding) our game socket to the relay session.
	void bindRelay( UnsignedInt now );
	/// The server's answer to our binding; the game socket sends through the relay from now on.
	/// Returns the endpoint the server saw, unset if the answer is not ours.
	::Engine::WireEndpoint relayBoundAnswer( const ::Engine::TreffMessage &answer, const ::Engine::NetAddress &from, UnsignedInt now );

private:
	static Bool valid( Int slot ) { return slot >= 0 && slot < MAX_SLOTS; }

	Context m_context;
	Member m_members[MAX_SLOTS];

	::Engine::NetAddress m_relayServer;
	std::string m_relayServerText;
	UnsignedInt64 m_relayId = 0;
	UnsignedByte m_relayToken[16] = {};
	Bool m_relayBound = FALSE;
	UnsignedInt m_nextRelayBind = 0;
	UnsignedInt m_relayBindTag = 0;
};

} // namespace Lobby

#endif // GAMENETWORK_LOBBY_GAMELINKS_H
