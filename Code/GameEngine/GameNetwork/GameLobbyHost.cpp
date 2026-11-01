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

// GameLobbyHost.cpp: what the host of a game does. It admits joiners (HELLO, CHALLENGE with a
// stateless cookie, JOIN, WELCOME), keeps the state and the keys of all players with everybody,
// removes players that leave or go silent, announces the game in the LAN, keeps a room at a
// rendezvous server and opens the relay session of the game.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/crc.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/UserSettings.h"
#include "Engine/SessionHandshake.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/Lobby/LobbyTiming.h"
#include "GameNetwork/networkutil.h"

#include <cstring>

extern Bool LANbuttonPushed;

using namespace Lobby;

void GameLobby::RequestGameCreate( UnicodeString gameName, Bool isDirectConnect )
{
	if ((!m_inLobby || m_currentGame) && !isDirectConnect)
	{
		DEBUG_ASSERTCRASH(m_inLobby && m_currentGame, ("Can't create a game while in one!"));
		OnGameCreate(GameLobbyInterface::RET_BUSY);
		return;
	}

	if (m_pendingAction != ACT_NONE || m_join.active)
	{
		OnGameCreate(GameLobbyInterface::RET_BUSY);
		return;
	}

	closeGameTransport();
	clearSession();
	if (!openGameTransport())
	{
		OnGameCreate(GameLobbyInterface::RET_UNKNOWN);
		return;
	}
	newKeyPair();
	while (m_sessionId == 0)
	{
		if (!::Engine::secureRandom(&m_sessionId, sizeof(m_sessionId)))
			m_sessionId = (static_cast<UnsignedInt64>(randomTag()) << 32) | randomTag();
	}
	if (!::Engine::secureRandom(m_host.cookieKey, sizeof(m_host.cookieKey)))
		DEBUG_CRASH(("GameLobby::RequestGameCreate - no random numbers from the system"));
	memcpy(m_host.oldCookieKey, m_host.cookieKey, sizeof(m_host.cookieKey));
	m_host.cookieKeyTime = timeGetTime();
	m_host.stateRevision = 1;
	m_listPublicly = FALSE;

	// Create the local game object
	m_inLobby = FALSE;
	m_localIP = ::Engine::SessionHandshake::identityOf(m_keyPair.publicKey);
	linkGame();
	auto created = std::make_unique<LANGameInfo>();
	LANGameInfo *myGame = created.get();
	myGame->setSeed(GetTickCount());
	myGame->setLocalIP(m_localIP);
	myGame->enterGame();
	myGame->setName(sessionGameName(m_sessionId));

	LANGameSlot newSlot;
	newSlot.setState(SLOT_PLAYER, m_name);
	newSlot.setIP(m_localIP);
	newSlot.setPort(m_gamePort);
	newSlot.setLastHeard(0);
	newSlot.setLogin(m_userName);
	newSlot.setHost(m_hostName);

	myGame->setSlot(0,newSlot);
	myGame->setPublicKey(0, m_keyPair.publicKey);
	m_links.setMember(0, m_localIP, m_keyPair.publicKey, timeGetTime());
	UnsignedByte fingerprint[5];
	fingerprintBytes(m_keyPair.publicKey, fingerprint);
	myGame->setSession(m_sessionId, ::Engine::NetAddress(), fingerprint);
	myGame->setHostVersion(LAN_PROTOCOL_VERSION, LANProgramTag(), LANDataTag());
	m_expectedHostCode.clear();
	m_showGameCode = TRUE;
	myGame->setMap(LobbySettings(LobbySettings::Kind::Lan).getPreferredMap());
	myGame->setIsDirectConnect(isDirectConnect);
	myGame->setLastHeard(timeGetTime());
	m_currentGame = m_directory.adoptGame(std::move(created));

	// games over the internet: a room at a rendezvous server gives an invitation code
	m_room.start(!m_servers.empty(), timeGetTime());

	OnGameCreate(GameLobbyInterface::RET_OK);
}

void GameLobby::hostUpdate( UnsignedInt now )
{
	if (!m_currentGame || m_inLobby || !AmIHost() || m_currentGame->isGameInProgress())
		return;

	// the cookie of a CHALLENGE stays valid for one to two rotations
	if (due(now, m_host.cookieKeyTime + Timing::COOKIE_ROTATION))
	{
		memcpy(m_host.oldCookieKey, m_host.cookieKey, sizeof(m_host.cookieKey));
		if (!::Engine::secureRandom(m_host.cookieKey, sizeof(m_host.cookieKey)))
			DEBUG_CRASH(("GameLobby::hostUpdate - no random numbers from the system"));
		m_host.cookieKeyTime = now;
	}

	syncMembers();

	for (Int slot = 1; slot < MAX_SLOTS; ++slot)
	{
		LANGameSlot *s = m_currentGame->getLANSlot(slot);
		if (s->isHuman() && silentFor(now, m_host.memberHeard[slot], Timing::SILENCE))
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:PlayerDropped"), s->getName().str());
			removeMember(slot, TRUE);
			OnChat(UnicodeString::TheEmptyString, m_localIP, text, LANCHAT_SYSTEM);
			if (LANbuttonPushed || !m_currentGame)
				return;
		}
	}

	if (!m_currentGame->getIsDirectConnect() && due(now, m_host.lastAnnounce + Timing::ANNOUNCE))
		sendAnnounce();
	if (due(now, m_host.lastState + Timing::STATE))
	{
		sendState();
		sendMembers();
	}
	if (m_host.membersChanged)
	{
		m_host.membersChanged = FALSE;
		sendMembers();
		openRelay();
	}

	switch (refreshRoom(now))
	{
		case HostRoom::Refresh::Refreshed:
			openRelay();	// keeps the relay session alive as well
			break;
		case HostRoom::Refresh::NoServer:
			systemChat(TheGameText->fetch("GZH:LanInviteNone"));
			break;
		default:
			break;
	}

	m_links.probe(now, [this]( Int slot ) {
		const Int s = slotOf(m_links.member(slot).identity);
		if (s < 0)
			return;
		UnicodeString text;
		text.formatUnchecked(TheGameText->fetch("GZH:LanRelayUsed").str(), m_currentGame->getConstLANSlot(s)->getName().str());
		systemChat(text);
	});
	m_links.bindRelay(now);
}

void GameLobby::sendAnnounce( void )
{
	if (!m_currentGame || !AmIHost() || m_currentGame->getIsDirectConnect())
		return;
	::Engine::SessionMessage m;
	m.type = ::Engine::SessionType::ANNOUNCE;
	m.protocol = LAN_PROTOCOL_VERSION;
	m.sessionId = m_sessionId;
	m.programCRC = TheGlobalData->m_exeCRC;
	m.dataCRC = TheGlobalData->m_iniCRC;
	fingerprintBytes(m_keyPair.publicKey, m.fingerprint);
	m.gameName = toUtf8(m_currentGame->getName());
	m.hostName = toUtf8(m_name);
	m.mapName = m_currentGame->getMap().str();
	if (m.mapName.size() > ::Engine::SESSION_MAX_MAP)
		m.mapName.clear();
	Int players = 0;
	Int places = 0;
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		const LANGameSlot *s = m_currentGame->getConstLANSlot(slot);
		if (s->isOccupied())
			++players;
		if (s->getState() != SLOT_CLOSED)
			++places;
	}
	m.players = static_cast<std::uint8_t>(players);
	m.maxPlayers = static_cast<std::uint8_t>(places);
	m.inProgress = m_currentGame->isGameInProgress();
	m.options = GameInfoToAsciiString(m_currentGame).str();
	broadcast(m);
	m_host.lastAnnounce = timeGetTime();
}

void GameLobby::sendState( UnsignedInt to )
{
	if (!m_currentGame || !AmIHost())
		return;
	if (m_host.lastGameOptions.isEmpty())
		m_host.lastGameOptions = GenerateGameOptionsString();
	::Engine::SessionMessage m;
	m.type = ::Engine::SessionType::STATE;
	m.revision = m_host.stateRevision;
	m.options = m_host.lastGameOptions.str();
	if (to != 0)
		sendToPeer(m, to);
	else
		sendToMembers(m);
	m_host.lastState = timeGetTime();
}

void GameLobby::sendMembers( UnsignedInt to )
{
	if (!m_currentGame || !AmIHost())
		return;
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		const LANGameSlot *s = m_currentGame->getConstLANSlot(slot);
		const Member &r = m_links.member(slot);
		if (!s->isHuman() || r.identity == 0 || r.identity != s->getIP())
			continue;
		::Engine::SessionMessage m;
		m.type = ::Engine::SessionType::MEMBER;
		m.member.slot = static_cast<std::uint8_t>(slot);
		m.member.identity = r.identity;
		memcpy(m.member.key, r.key, sizeof(m.member.key));
		if (slot == 0)
			ownEndpoints(m.member.endpoints);
		else
			m.member.endpoints = m_links.member(slot).endpoints;
		if (to != 0)
			sendToPeer(m, to);
		else
			sendToMembers(m);
	}
}

void GameLobby::RequestGameStart( void )
{
	if (m_inLobby || !m_currentGame || !AmIHost())
		return;

	applyMemberKeys();
	sendMembers();
	::Engine::SessionMessage m;
	m.type = ::Engine::SessionType::START;
	sendToMembers(m);
	sendToMembers(m);	// a lost START would leave a player behind

	OnGameStart();
}

void GameLobby::RequestGameStartTimer( Int seconds )
{
	if (m_inLobby || !m_currentGame || !AmIHost())
		return;

	UnsignedInt now = timeGetTime();
	m_gameStartTime = now + 1000;
	m_gameStartSeconds = (seconds) ? seconds - 1 : 0;

	sendMembers();
	::Engine::SessionMessage m;
	m.type = ::Engine::SessionType::TIMER;
	m.value = static_cast<std::uint32_t>(seconds);
	sendToMembers(m);

	OnGameStartTimer(seconds);
}

// A member is out (left, timed out, removed by the host's slot changes): it hears so, its keys go.
void GameLobby::syncMembers( void )
{
	if (!m_currentGame || !AmIHost())
		return;
	for (Int slot = 1; slot < MAX_SLOTS; ++slot)
	{
		const UnsignedInt old = m_links.member(slot).identity;
		const LANGameSlot *s = m_currentGame->getConstLANSlot(slot);
		if (old == 0 || (s->isHuman() && s->getIP() == old))
			continue;
		if (slotOf(old) < 0)
		{
			::Engine::SessionMessage leave;
			leave.type = ::Engine::SessionType::LEAVE;
			sendToPeer(leave, old);
			m_transport->removePeerKeys(old);
		}
		m_links.clearMember(slot);
		m_host.memberHeard[slot] = 0;
		m_host.membersChanged = TRUE;
	}
}

void GameLobby::removeMember( Int slot, Bool tellThem )
{
	if (!m_currentGame || !AmIHost() || slot <= 0 || slot >= MAX_SLOTS)
		return;
	LANGameSlot *s = m_currentGame->getLANSlot(slot);
	if (!s->isHuman())
		return;
	const UnicodeString name = s->getName();
	const UnsignedInt identity = s->getIP();
	if (tellThem)
	{
		::Engine::SessionMessage leave;
		leave.type = ::Engine::SessionType::LEAVE;
		sendToPeer(leave, identity);
	}
	LANGameSlot open;
	open.setState(SLOT_OPEN);
	m_currentGame->setSlot(slot, open);
	syncMembers();
	OnPlayerLeave(name);
	if (m_currentGame)
	{
		m_currentGame->resetAccepted();
		RequestGameOptions(GenerateGameOptionsString(), false);
	}
}

// Before a cookie came back, an answer is never larger than the request.
void GameLobby::refuse( const ::Engine::NetAddress &to, UnsignedInt64 sessionId, ReturnType reason, std::size_t sizeLimit )
{
	if (!m_replyLimiter.allow(0, timeGetTime()))
		return;
	::Engine::SessionMessage r;
	r.type = ::Engine::SessionType::REFUSE;
	r.reason = static_cast<std::uint8_t>(reason);
	r.protocol = LAN_PROTOCOL_VERSION;
	r.sessionId = sessionId;
	r.versionText = LANVersionText().str();
	if (r.versionText.size() > ::Engine::SESSION_MAX_VERSION)
		r.versionText.resize(::Engine::SESSION_MAX_VERSION);
	// shorten the version text until the answer is no larger than the request
	Encoded wire;
	while (encode(r, wire) && static_cast<std::size_t>(wire.size) > sizeLimit && !r.versionText.empty())
	{
		const std::size_t excess = static_cast<std::size_t>(wire.size) - sizeLimit;
		r.versionText.resize(excess >= r.versionText.size() ? 0 : r.versionText.size() - excess);
	}
	if (encode(r, wire) && static_cast<std::size_t>(wire.size) <= sizeLimit)
		m_transport->sendRaw(to, wire.data, wire.size);
}

// Stateless: the cookie proves in JOIN that the joiner receives what we send to its endpoint.
void GameLobby::handleHello( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d )
{
	if (!m_currentGame || m_inLobby || !AmIHost())
		return;
	const std::size_t limit = static_cast<std::size_t>(d.length);
	if (m.protocol != LAN_PROTOCOL_VERSION)
	{
		refuse(d.from, m.sessionId, RET_VERSION_MISMATCH, limit);
		return;
	}
	if (m.sessionId != 0 && m.sessionId != m_sessionId)
	{
		refuse(d.from, m.sessionId, RET_GAME_GONE, limit);
		return;
	}
	// a running game still takes spectators; the JOIN tells what the joiner wants
	::Engine::SessionMessage c;
	c.type = ::Engine::SessionType::CHALLENGE;
	c.sessionId = m_sessionId;
	memcpy(c.key, m_keyPair.publicKey, sizeof(c.key));
	memcpy(c.nonce, m.nonce, sizeof(c.nonce));
	::Engine::SessionHandshake::cookie(m_host.cookieKey, m_sessionId, m.key, m.nonce, wireEndpoint(d.from), c.cookie);
	sendPlain(c, d.from);
}

void GameLobby::handleJoin( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d )
{
	if (!m_currentGame || m_inLobby || !AmIHost() || m.sessionId != m_sessionId)
		return;
	UnsignedByte expected[::Engine::SessionHandshake::COOKIE_SIZE];
	::Engine::SessionHandshake::cookie(m_host.cookieKey, m_sessionId, m.key, m.nonce, wireEndpoint(d.from), expected);
	if (!::Engine::SessionHandshake::cookieEquals(expected, m.cookie))
	{
		::Engine::SessionHandshake::cookie(m_host.oldCookieKey, m_sessionId, m.key, m.nonce, wireEndpoint(d.from), expected);
		if (!::Engine::SessionHandshake::cookieEquals(expected, m.cookie))
			return;
	}
	std::vector<std::uint8_t> plain;
	::Engine::SessionJoinInfo info;
	if (!::Engine::SessionHandshake::openJoin(m_keyPair, m.key, m.nonce, m.sealed.data(), m.sealed.size(), plain)
		|| !::Engine::decodeJoinInfo(plain.data(), plain.size(), info))
	{
		return;
	}
	const UnsignedInt identity = ::Engine::SessionHandshake::identityOf(m.key);
	const UnsignedInt now = timeGetTime();

	// a repeated JOIN of a spectator or of a player coming back
	if (const Int index = findSpectator(identity); index >= 0)
	{
		const Spectator &s = m_watchHost.spectators[static_cast<size_t>(index)];
		if (s.returnSlot >= 0)
		{
			sendReturnWelcome(s);
			return;
		}
		::Engine::SessionMessage w;
		w.type = ::Engine::SessionType::WELCOME;
		w.sessionId = m_sessionId;
		w.slot = ::Engine::SESSION_SPECTATOR_SLOT;
		w.identity = identity;
		sendToPeer(w, identity);
		return;
	}

	// a repeated JOIN (our WELCOME got lost)
	const Int existing = slotOf(identity);
	if (existing > 0 && m_links.member(existing).identity == identity && memcmp(m_links.member(existing).key, m.key, sizeof(m.key)) == 0)
	{
		::Engine::SessionMessage w;
		w.type = ::Engine::SessionType::WELCOME;
		w.sessionId = m_sessionId;
		w.slot = static_cast<std::uint8_t>(existing);
		w.identity = identity;
		memcpy(w.token, m_host.returnTokens[existing], sizeof(w.token));
		sendToPeer(w, identity);
		sendState(identity);
		sendMembers(identity);
		m_host.memberHeard[existing] = now;
		return;
	}

	const std::size_t unlimited = ::Engine::MAX_SESSION_MESSAGE;
	// Version check. The program (m_exeCRC: logic version, game, SkirmishScripts.scb and
	// MultiplayerScripts.scb) and the game data (m_iniCRC: all game INIs loaded with XferCRC) must
	// match, as must the protocol. The joiner is told which of them differs.
	ReturnType mismatch = RET_OK;
	if (info.protocol != LAN_PROTOCOL_VERSION)
	{
		mismatch = RET_VERSION_MISMATCH;
	}
#if defined(_DEBUG)
	else if (TheGlobalData->m_netMinPlayers > 0) {
#else
	else {
#endif
		if (info.programCRC != TheGlobalData->m_exeCRC)
			mismatch = RET_VERSION_MISMATCH;
		else if (info.dataCRC != TheGlobalData->m_iniCRC)
			mismatch = RET_DATA_MISMATCH;
	}
	if (mismatch != RET_OK)
	{
		DEBUG_LOG(("GameLobby::handleJoin - join denied: protocol them/us %u/%u, CRCs INI:%X/%X exe:%X/%X\n",
			info.protocol, LAN_PROTOCOL_VERSION, info.dataCRC, TheGlobalData->m_iniCRC, info.programCRC, TheGlobalData->m_exeCRC));
		refuse(d.from, m_sessionId, mismatch, unlimited);
		return;
	}
	if (info.wants == ::Engine::SessionWants::RETURN)
	{
		acceptReturner(m, d, info, identity);
		return;
	}
	const UnicodeString name = playerName(info.name);
	if (name.isEmpty() || existing >= 0 || identity == m_localIP)
	{
		refuse(d.from, m_sessionId, RET_UNKNOWN, unlimited);
		return;
	}
	const Bool running = m_currentGame->isGameInProgress();
	if (info.wants == ::Engine::SessionWants::WATCH || (info.wants == ::Engine::SessionWants::PLAY_OR_WATCH && running))
	{
		if (running)
			acceptSpectator(m, d, name, identity);
		else
			refuse(d.from, m_sessionId, RET_GAME_GONE, unlimited);
		return;
	}
	if (running)
	{
		refuse(d.from, m_sessionId, RET_GAME_STARTED, unlimited);
		return;
	}
	for (Int player = 0; player < MAX_SLOTS; ++player)
	{
		LANGameSlot *slot = m_currentGame->getLANSlot(player);
		if (slot->isHuman() && slot->getName().compare(name) == 0)
		{
			refuse(d.from, m_sessionId, RET_DUPLICATE_NAME, unlimited);
			return;
		}
	}

	// room: players (observers aside) against the starting spots of the map, and an open slot
	Int numPlayers = 0;
	for (Int player = 0; player < MAX_SLOTS; ++player)
	{
		if (m_currentGame->getLANSlot(player)->isOccupied()
			&& !(m_currentGame->getLANSlot(player)->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER))
		{
			++numPlayers;
		}
	}
	Int numStartingSpots = MAX_SLOTS;
	const MapMetaData *md = TheMapCache->findMap(m_currentGame->getMap());
	if (md != nullptr)
		numStartingSpots = md->m_numPlayers;
	Int slotNum = -1;
	if (numPlayers < numStartingSpots)
	{
		for (Int player = 1; player < MAX_SLOTS && slotNum < 0; ++player)
		{
			if (m_currentGame->getLANSlot(player)->isOpen())
				slotNum = player;
		}
	}
	if (slotNum < 0)
	{
		refuse(d.from, m_sessionId, RET_GAME_FULL, unlimited);
		return;
	}

	::Engine::SealSessionKeys keys;
	if (!::Engine::deriveSessionKeys(m_keyPair, m.key, LOBBY_CONTEXT, sizeof(LOBBY_CONTEXT), keys))
		return;
	m_transport->setPeerKeys(identity, m_lobbyPort, keys);
	keys.wipe();
	m_transport->bindPeer(identity, d.from);

	LANGameSlot newSlot;
	newSlot.setState(SLOT_PLAYER, name);
	newSlot.setIP(identity);
	newSlot.setPort(m_gamePort);
	newSlot.setLastHeard(now);
	m_currentGame->setSlot(slotNum, newSlot);
	m_currentGame->setPublicKey(slotNum, m.key);
	// where the joiner's packets came from first, then what it reports
	::Engine::SessionEndpoints endpoints;
	addEndpoint(endpoints.lobby, wireEndpoint(d.from));
	for (const ::Engine::WireEndpoint &e : info.endpoints.lobby)
		addEndpoint(endpoints.lobby, e);
	for (const ::Engine::WireEndpoint &e : info.endpoints.game)
		addEndpoint(endpoints.game, e);
	m_links.setMember(slotNum, identity, m.key, now);
	m_links.member(slotNum).endpoints = endpoints;
	m_host.memberHeard[slotNum] = now;
	DEBUG_LOG(("GameLobby::handleJoin - %ls joins in slot %d as %X from %s\n", name.str(), slotNum, identity, d.from.toString().c_str()));

	newReturnToken(slotNum);
	::Engine::SessionMessage w;
	w.type = ::Engine::SessionType::WELCOME;
	w.sessionId = m_sessionId;
	w.slot = static_cast<std::uint8_t>(slotNum);
	w.identity = identity;
	memcpy(w.token, m_host.returnTokens[slotNum], sizeof(w.token));
	sendToPeer(w, identity);
	m_host.membersChanged = TRUE;

	OnPlayerJoin(slotNum, name);	// sends the new state to everybody
	sendMembers();
}

// The relay session of the game at the room's server, with a binding token for every player.
void GameLobby::openRelay( void )
{
	if (!m_currentGame || !AmIHost())
		return;
	std::vector<HostRoom::RelayMember> members;
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		const LANGameSlot *s = m_currentGame->getConstLANSlot(slot);
		const UnsignedInt identity = m_links.member(slot).identity;
		if (s->isHuman() && identity != 0 && identity == s->getIP())
			members.push_back({ slot, identity });
	}
	m_room.openRelay(members, m_transport.get());
}

void GameLobby::showInvite( void )
{
	m_inviteCode = m_room.inviteCode(fingerprintCode(m_keyPair.publicKey)).c_str();
	UnicodeString text;
	text.formatUnchecked(TheGameText->fetch("GZH:LanInvite").str(), m_inviteCode.str());
	systemChat(text);
}

// A sealed message of a member of our game.
void GameLobby::hostReceives( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d, Int slot )
{
	const UnsignedInt now = timeGetTime();
	m_host.memberHeard[slot] = now;
	const Bool open = !m_currentGame->isGameInProgress();
	switch (m.type)
	{
		case ::Engine::SessionType::WISH:
			if (open)
				OnGameOptions(d.sender, slot, AsciiString(m.text.c_str()));
			break;
		case ::Engine::SessionType::ACCEPT:
			// taking back readiness (alt-tab) only counts before the countdown
			if (open && (m.flag || m_gameStartTime == 0))
				OnAccept(d.sender, m.flag);
			break;
		case ::Engine::SessionType::HAS_MAP:
		{
			CRC mapNameCRC;
			const AsciiString portableMapName = TheGameState->realMapPathToPortableMapPath(m_currentGame->getMap());
			mapNameCRC.computeCRC(portableMapName.str(), portableMapName.getLength());
			if (open && mapNameCRC.get() == m.mapCRC)
				OnHasMap(d.sender, m.flag);
			break;
		}
		case ::Engine::SessionType::CHAT:
		{
			if (!m_chatLimiter.allow(d.sender, now))
				break;
			OnChat(m_currentGame->getLANSlot(slot)->getName(), d.sender, fromUtf8(m.text), chatTypeOf(m.chatKind));
			// the others hear it from us, with the slot it came from
			::Engine::SessionMessage forward;
			forward.type = ::Engine::SessionType::CHAT;
			forward.slot = static_cast<std::uint8_t>(slot);
			forward.chatKind = m.chatKind;
			forward.text = m.text;
			if (m_currentGame)
				sendToMembers(forward, d.sender);
			break;
		}
		case ::Engine::SessionType::LEAVE:
			removeMember(slot, FALSE);
			break;
		case ::Engine::SessionType::PING:
		{
			::Engine::SessionMessage pong;
			pong.type = ::Engine::SessionType::PONG;
			pong.value = m.value;
			sendToPeer(pong, d.sender);
			break;
		}
		default:
			break;
	}
}

// Answers of the server our room is at.
void GameLobby::roomAnswer( const ::Engine::TreffMessage &m, const Transport::RawDatagram &d, Bool gameSocket )
{
	if (!m_currentGame || !AmIHost())
		return;
	const UnsignedInt now = timeGetTime();
	switch (m_room.answer(m, d.from, gameSocket, now, m_transport.get()))
	{
		case HostRoom::Answer::Registered:
			m_publicLobby = m_room.observedLobby();
			showInvite();
			m_host.membersChanged = TRUE;
			break;
		case HostRoom::Answer::RelayOpened:
		{
			m_links.joinRelay(m_room.server(), m_room.serverText(), m_room.relayId(), m_room.relayToken(0), now);
			for (Int slot = 1; slot < MAX_SLOTS; ++slot)
			{
				const LANGameSlot *s = m_currentGame->getConstLANSlot(slot);
				if (!s->isHuman() || m_links.member(slot).identity != s->getIP())
					continue;
				::Engine::SessionMessage relay;
				relay.type = ::Engine::SessionType::RELAY;
				relay.relayServer = m_room.serverText();
				relay.relayId = m_room.relayId();
				memcpy(relay.relayToken, m_room.relayToken(slot), sizeof(relay.relayToken));
				sendToPeer(relay, s->getIP());
			}
			break;
		}
		case HostRoom::Answer::ServersNamed:
			m_servers.addFound(m_room.named());
			break;
		default:
			break;
	}
	if (gameSocket && m_room.observedGame().isSet())
		m_publicGame = m_room.observedGame();
}
