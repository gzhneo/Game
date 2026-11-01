/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/Lobby/LobbyDirectory.h"

#include <algorithm>

namespace Lobby
{

namespace
{
	Bool heardWithin( UnsignedInt now, UnsignedInt heard, UnsignedInt timeout )
	{
		return static_cast<Int>(now - heard) < static_cast<Int>(timeout);
	}
}

Directory::~Directory()
{
	clear();
}

void Directory::clear( void )
{
	m_games.clear();
	m_players.clear();
}

LANGameInfo *Directory::findGame( const UnicodeString &name ) const
{
	for (const auto &game : m_games)
		if (game->getName() == name)
			return game.get();
	return nullptr;
}

LANGameInfo *Directory::gameAt( Int offset ) const
{
	if (offset < 0 || static_cast<size_t>(offset) >= m_games.size())
		return nullptr;
	return m_games[static_cast<size_t>(offset)].get();
}

LANPlayer *Directory::findPlayer( UnsignedInt identity ) const
{
	for (const auto &player : m_players)
		if (player->getIP() == identity)
			return player.get();
	return nullptr;
}

LANGameInfo *Directory::adoptGame( std::unique_ptr<LANGameInfo> game )
{
	LANGameInfo *raw = game.get();
	if (raw == nullptr || findGame(raw->getName()) == raw)
		return raw;
	m_games.push_back(std::move(game));
	relinkGames();
	return raw;
}

std::unique_ptr<LANGameInfo> Directory::releaseGame( LANGameInfo *game )
{
	const auto it = std::find_if(m_games.begin(), m_games.end(), [game]( const auto &g ) { return g.get() == game; });
	if (it == m_games.end())
		return nullptr;
	std::unique_ptr<LANGameInfo> owned = std::move(*it);
	m_games.erase(it);
	owned->setNext(nullptr);
	relinkGames();
	return owned;
}

LANPlayer *Directory::notePlayer( UnsignedInt identity, const UnicodeString &name, UnsignedInt now )
{
	LANPlayer *player = findPlayer(identity);
	if (player == nullptr)
	{
		if (m_players.size() >= MAX_PLAYERS)
			return nullptr;
		m_players.push_back(std::make_unique<LANPlayer>());
		player = m_players.back().get();
		player->setIP(identity);
	}
	player->setName(name);
	player->setLastHeard(now);
	relinkPlayers();
	return player;
}

Bool Directory::erasePlayer( UnsignedInt identity )
{
	const auto it = std::find_if(m_players.begin(), m_players.end(), [identity]( const auto &p ) { return p->getIP() == identity; });
	if (it == m_players.end())
		return FALSE;
	m_players.erase(it);
	relinkPlayers();
	return TRUE;
}

Directory::Expired Directory::expire( UnsignedInt now, UnsignedInt gameTimeout, UnsignedInt playerTimeout,
	const LANGameInfo *keepGame, UnsignedInt keepPlayer )
{
	Expired expired;
	const size_t games = m_games.size();
	std::erase_if(m_games, [&]( const auto &g ) {
		return g.get() != keepGame && !heardWithin(now, g->getLastHeard(), gameTimeout);
	});
	const size_t players = m_players.size();
	std::erase_if(m_players, [&]( const auto &p ) {
		return p->getIP() != keepPlayer && !heardWithin(now, p->getLastHeard(), playerTimeout);
	});
	expired.games = m_games.size() != games;
	expired.players = m_players.size() != players;
	if (expired.games)
		relinkGames();
	if (expired.players)
		relinkPlayers();
	return expired;
}

void Directory::relinkGames( void )
{
	std::stable_sort(m_games.begin(), m_games.end(), []( const auto &a, const auto &b ) {
		return a->getName().compareNoCase(b->getName()) < 0;
	});
	for (size_t i = 0; i < m_games.size(); ++i)
		m_games[i]->setNext(i + 1 < m_games.size() ? m_games[i + 1].get() : nullptr);
}

void Directory::relinkPlayers( void )
{
	std::stable_sort(m_players.begin(), m_players.end(), []( const auto &a, const auto &b ) {
		return a->getName().compareNoCase(b->getName()) < 0;
	});
	for (size_t i = 0; i < m_players.size(); ++i)
		m_players[i]->setNext(i + 1 < m_players.size() ? m_players[i + 1].get() : nullptr);
}

} // namespace Lobby
