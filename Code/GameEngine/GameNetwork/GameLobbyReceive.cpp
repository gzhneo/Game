/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////
// FILE: GameLobbyReceive.cpp
// Author: Matthew D. Campbell, October 2001
// Description: every received session and rendezvous message: the lobby's own, the rest handed on
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GlobalData.h"
#include "Engine/SessionHandshake.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/Lobby/LobbyTiming.h"

extern Bool LANbuttonPushed;

using namespace Lobby;

namespace
{
	UnsignedShort tagOf( UnsignedInt crc )
	{
		return static_cast<UnsignedShort>(crc ^ (crc >> 16));
	}
}

void GameLobby::receive( Transport *transport, Bool gameSocket )
{
	Transport::RawDatagram d;
	Int budget = 256;
	// a handler may close the game socket or push a menu: stop reading then
	while (budget-- > 0 && !LANbuttonPushed && transport == (gameSocket ? m_gameTransport.get() : m_transport.get()) && transport->takeRaw(d))
		handleDatagram(d, gameSocket);
}

void GameLobby::handleDatagram( const Transport::RawDatagram &d, Bool gameSocket )
{
	if (d.length <= 0 || d.length > Transport::MAX_RAW_DATAGRAM)
		return;
	const std::uint8_t *data = d.data;
	const std::size_t size = static_cast<std::size_t>(d.length);
	if (::Engine::looksLikeTreff(data, size))
	{
		::Engine::TreffMessage t;
		if (!d.authenticated && !d.relayed && isRendezvousServer(d.from) && ::Engine::decodeTreff(data, size, t))
			handleTreff(t, d, gameSocket);
		return;
	}
	::Engine::SessionMessage m;
	if (!::Engine::looksLikeSession(data, size) || !::Engine::decodeSession(data, size, m))
		return;
	if (::Engine::isSealedSessionType(m.type))
	{
		if (d.authenticated)
			handleSealed(m, d, gameSocket);
	}
	else
	{
		handlePlain(m, d, gameSocket);
	}
}

void GameLobby::handlePlain( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d, Bool gameSocket )
{
	if (gameSocket)
	{
		// only probes, sealed with the keys of a player of our game
		if (!d.authenticated)
			return;
		if (m.type == ::Engine::SessionType::PROBE)
			handleProbe(m, d);
		else if (m.type == ::Engine::SessionType::PROBE_ACK)
			m_links.probeAnswered(m, d);
		return;
	}
	// the local network does not take part in the Internet lobby
	const Bool lan = m_mode == Mode::Lan;
	switch (m.type)
	{
		case ::Engine::SessionType::QUERY:
		case ::Engine::SessionType::ANNOUNCE:
		case ::Engine::SessionType::PRESENCE:
		case ::Engine::SessionType::LOBBY_CHAT:
		case ::Engine::SessionType::BYE:
			if (!lan)
				return;
			break;
		default:
			break;
	}
	switch (m.type)
	{
		case ::Engine::SessionType::QUERY:
			if (!isOwnEndpoint(d.from))
				handleQuery(d);
			break;
		case ::Engine::SessionType::ANNOUNCE:
			if (!isOwnEndpoint(d.from))
				handleAnnounce(m, d);
			break;
		case ::Engine::SessionType::PRESENCE:
			if (!isOwnEndpoint(d.from))
				handlePresence(m, d);
			break;
		case ::Engine::SessionType::LOBBY_CHAT:
			if (!isOwnEndpoint(d.from))
				handleLobbyChat(m, d);
			break;
		case ::Engine::SessionType::BYE:
			if (!isOwnEndpoint(d.from))
				handleBye(d);
			break;
		case ::Engine::SessionType::HELLO:
			handleHello(m, d);
			break;
		case ::Engine::SessionType::CHALLENGE:
			handleChallenge(m, d);
			break;
		case ::Engine::SessionType::JOIN:
			handleJoin(m, d);
			break;
		case ::Engine::SessionType::REFUSE:
			handleRefuse(m, d);
			break;
		default:
			// PROBE on the session socket opens a router for a joiner; nothing to do
			break;
	}
}

void GameLobby::handleQuery( const Transport::RawDatagram &d )
{
	// The answer is a broadcast; our periodic announcements reach anybody left out.
	if (!m_replyLimiter.allow(0, timeGetTime()))
		return;
	if (m_inLobby && m_pendingAction == ACT_NONE && !m_join.active)
	{
		sendPresence();
		m_lastResendTime = timeGetTime();
	}
	else if (m_currentGame && !m_inLobby && AmIHost())
	{
		sendAnnounce();
	}
}

void GameLobby::handleAnnounce( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d )
{
	if (m.sessionId == 0 || (m_sessionId != 0 && m.sessionId == m_sessionId))
		return;
	if (m_currentGame && (m_currentGame->isGameInProgress() || !m_inLobby))
		return; // Don't care about games if we're playing
	const UnicodeString name = sessionGameName(m.sessionId);
	LANGameInfo *game = m_directory.findGame(name);
	const UnsignedInt now = timeGetTime();
	// Announcements are plain: a game claimed from another endpoint keeps its first host while it
	// keeps announcing.
	if (game != nullptr && game->getHostEndpoint() != d.from && !silentFor(now, game->getLastHeard(), Timing::GAME_LIST))
		return;
	std::unique_ptr<LANGameInfo> created;
	if (game == nullptr)
	{
		if (!m_directory.canAddGame())
			return;
		created = std::make_unique<LANGameInfo>();
		created->setName(name);
		game = created.get();
	}
	if (!ParseGameOptionsString(game, AsciiString(m.options.c_str())))
	{
		if (!created)
			m_directory.eraseGame(game);
		OnGameList(m_directory.games());
		return;
	}
	if (created)
		m_directory.adoptGame(std::move(created));
	game->setGameInProgress(m.inProgress);
	game->setIsDirectConnect(FALSE);
	game->setHostVersion(m.protocol, tagOf(m.programCRC), tagOf(m.dataCRC));
	game->setSession(m.sessionId, d.from, m.fingerprint);
	game->setLastHeard(now);
	OnGameList(m_directory.games());
}

void GameLobby::handlePresence( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d )
{
	const UnicodeString name = playerName(m.name);
	if (name.isEmpty() || d.sender == m_localIP)
		return;
	LANPlayer *player = m_directory.notePlayer(d.sender, name, timeGetTime());
	if (player != nullptr)
		OnNameChange(player->getIP(), player->getName());
}

void GameLobby::handleLobbyChat( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d )
{
	if (!m_inLobby || d.sender == m_localIP || !m_chatLimiter.allow(d.sender, timeGetTime()))
		return;
	LANPlayer *player = m_directory.findPlayer(d.sender);
	if (player == nullptr)
		return;
	player->setLastHeard(timeGetTime());
	OnChat(player->getName(), player->getIP(), fromUtf8(m.text), chatTypeOf(m.chatKind));
}

void GameLobby::handleBye( const Transport::RawDatagram &d )
{
	if (m_inLobby && d.sender != m_localIP && m_directory.erasePlayer(d.sender))
		OnPlayerList(m_directory.players());
}

// A probe of a player on our game socket. The host also tells the others where the player's game
// socket shows up (its public port, even without a rendezvous server).
void GameLobby::handleProbe( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d )
{
	m_links.answerProbe(m, d);
	if (!AmIHost() || d.relayed || m.sessionId != m_sessionId || m.identity != d.sender)
		return;
	const Int slot = slotOf(d.sender);
	if (slot > 0 && addEndpoint(m_links.member(slot).endpoints.game, wireEndpoint(d.from)))
		m_host.membersChanged = TRUE;
}

// Sealed messages of the game: from the host while joining, the members (host) or the host (member).
void GameLobby::handleSealed( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d, Bool gameSocket )
{
	if (gameSocket)
		return;
	if (m_return.active)
	{
		if (d.sender == m_return.hostIdentity)
			returnReceives(m);
		return;
	}
	if (m_watch.active)
	{
		if (d.sender == m_watch.hostIdentity)
			watchReceives(m);
		return;
	}
	if (m_join.active)
	{
		if (!m_join.challenged || d.sender != ::Engine::SessionHandshake::identityOf(m_join.hostKey))
			return;
		if (m.type == ::Engine::SessionType::WELCOME)
			handleWelcome(m, d);
		else if (m_join.early.size() < Limit::EARLY_MESSAGES)
			m_join.early.push_back(d);
		return;
	}
	if (!m_currentGame || m_inLobby)
		return;
	const Int slot = slotOf(d.sender);
	if (AmIHost())
	{
		if (slot > 0)
			hostReceives(m, d, slot);
	}
	else if (slot == 0 && d.sender == m_links.member(0).identity)
	{
		memberReceives(m, d);
	}
}

// Answers of rendezvous servers: what every socket learns, then the host's room and the joiner.
void GameLobby::handleTreff( const ::Engine::TreffMessage &m, const Transport::RawDatagram &d, Bool gameSocket )
{
	const UnsignedInt now = timeGetTime();
	if (m.type == ::Engine::TreffType::OBSERVED)
	{
		if (m.tag != 0 && m.tag == m_observeTag && m.observed.isSet())
			(gameSocket ? m_publicGame : m_publicLobby) = m.observed;
		return;
	}
	if (m.type == ::Engine::TreffType::RELAY_BOUND)
	{
		const ::Engine::WireEndpoint seen = m_links.relayBoundAnswer(m, d.from, now);
		if (gameSocket && seen.isSet() && !m_publicGame.isSet())
			m_publicGame = seen;
		return;
	}
	if (!gameSocket && m_publicGames.answer(m, d.from, now))
		return;
	roomAnswer(m, d, gameSocket);
	joinAnswer(m, d, gameSocket);
}

GameLobbyInterface::ChatType GameLobby::chatTypeOf( std::uint8_t kind )
{
	return kind < LANCHAT_MAX ? static_cast<ChatType>(kind) : LANCHAT_NORMAL;
}
