/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LobbyDirectory.h -- the games and players the lobby knows.
//
// Owns every entry. The menus walk games and players as the linked lists they always did
// (getNext()); the directory keeps those links in its sort order (by name, case ignored). Anybody
// can announce games and players under any number of forged addresses, so both lists are bounded.

#pragma once

#ifndef GAMENETWORK_LOBBY_LOBBYDIRECTORY_H
#define GAMENETWORK_LOBBY_LOBBYDIRECTORY_H

#include "GameNetwork/LANGameInfo.h"
#include "GameNetwork/LANPlayer.h"

#include <memory>
#include <vector>

namespace Lobby
{

class Directory
{
public:
	static constexpr size_t MAX_GAMES = 64;
	static constexpr size_t MAX_PLAYERS = 128;

	Directory() = default;
	~Directory();
	Directory( const Directory & ) = delete;
	Directory &operator=( const Directory & ) = delete;

	/// Heads of the lists the menus walk; nullptr when empty.
	LANGameInfo *games( void ) const { return m_games.empty() ? nullptr : m_games.front().get(); }
	LANPlayer *players( void ) const { return m_players.empty() ? nullptr : m_players.front().get(); }

	size_t gameCount( void ) const { return m_games.size(); }
	size_t playerCount( void ) const { return m_players.size(); }

	LANGameInfo *findGame( const UnicodeString &name ) const;
	LANGameInfo *gameAt( Int offset ) const;
	LANPlayer *findPlayer( UnsignedInt identity ) const;

	Bool canAddGame( void ) const { return m_games.size() < MAX_GAMES; }

	/// Takes a game into the list; returns it.
	LANGameInfo *adoptGame( std::unique_ptr<LANGameInfo> game );
	/// Takes a game out of the list; the caller owns it then (nullptr if it was not listed).
	std::unique_ptr<LANGameInfo> releaseGame( LANGameInfo *game );
	void eraseGame( LANGameInfo *game ) { releaseGame(game); }

	/// A player heard from: added (if there is room) or renamed. nullptr if the list is full.
	LANPlayer *notePlayer( UnsignedInt identity, const UnicodeString &name, UnsignedInt now );
	Bool erasePlayer( UnsignedInt identity );

	struct Expired
	{
		Bool games = FALSE;
		Bool players = FALSE;
	};
	/// Drops games and players not heard from within their timeouts, except `keepGame` and `keepPlayer`.
	Expired expire( UnsignedInt now, UnsignedInt gameTimeout, UnsignedInt playerTimeout,
		const LANGameInfo *keepGame, UnsignedInt keepPlayer );

	void clear( void );

private:
	void relinkGames( void );
	void relinkPlayers( void );

	std::vector< std::unique_ptr<LANGameInfo> > m_games;
	std::vector< std::unique_ptr<LANPlayer> > m_players;
};

} // namespace Lobby

#endif // GAMENETWORK_LOBBY_LOBBYDIRECTORY_H
