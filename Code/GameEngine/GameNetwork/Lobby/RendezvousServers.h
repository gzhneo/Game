/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RendezvousServers.h -- the rendezvous servers the lobby asks, from where the player keeps them.
//
// Order: -treffpunkt on the command line, <user data>\Rendezvous.txt, the list shipped with the
// game (ProjectData\Network\Rendezvous.txt), <user data>\RendezvousFound.txt (servers other servers
// named). -netzOhneTreffpunkt leaves the list empty. Host names are resolved once and kept.

#pragma once

#ifndef GAMENETWORK_LOBBY_RENDEZVOUSSERVERS_H
#define GAMENETWORK_LOBBY_RENDEZVOUSSERVERS_H

#include "Engine/NetAddress.h"
#include "Engine/RendezvousServerList.h"

#include <string>
#include <utility>
#include <vector>

namespace Lobby
{

class RendezvousServers
{
public:
	/// Reads the lists (unless fixed by setFixed).
	void load( void );
	/// Tests: exactly these servers; no file is read or written.
	void setFixed( const std::vector<std::string> &servers );

	const std::vector<std::string> &list( void ) const { return m_list.servers(); }
	Bool empty( void ) const { return m_list.empty(); }

	enum class Change { Done, Invalid, Unchanged, NotOwn };
	/// The player's own list (<user data>\Rendezvous.txt): a server added or taken out, then all
	/// lists read again. Only servers of the own list can be taken out (NotOwn for the others).
	Change addOwn( const std::string &server );
	Change removeOwn( const std::string &server );

	/// Address of an entry (port 8090 if it names none); false if it does not resolve.
	Bool resolve( const std::string &text, ::Engine::NetAddress &out, Bool withIPv6 );
	/// An address one of the entries resolved to.
	Bool isKnown( const ::Engine::NetAddress &from ) const;

	/// Servers a server named: new ones are asked from now on and kept for later sessions.
	void addFound( const std::vector<std::string> &named );

private:
	::Engine::RendezvousServerList m_list;
	Bool m_fixed = FALSE;
	std::vector< std::pair<std::string, ::Engine::NetAddress> > m_resolved;
};

} // namespace Lobby

#endif // GAMENETWORK_LOBBY_RENDEZVOUSSERVERS_H
