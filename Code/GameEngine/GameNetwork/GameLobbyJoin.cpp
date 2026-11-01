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

// GameLobbyJoin.cpp: joining a game (from the list, by address or by invitation code over the
// rendezvous servers) and what a member does in the game until the start.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GlobalData.h"
#include "Common/QuotedPrintable.h"
#include "Common/UserSettings.h"
#include "Engine/InviteCode.h"
#include "Engine/NetAddressBook.h"
#include "Engine/SessionHandshake.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/Lobby/LobbyTiming.h"
#include "GameNetwork/networkutil.h"

#include <cstdio>
#include <cstring>

extern Bool LANbuttonPushed;

using namespace Lobby;

// Common start of every way to join: fresh keys and the game socket, whose endpoints go into JOIN.
void GameLobby::startJoin( void )
{
	closeGameTransport();
	clearSession();
	m_joinedInvite.clear();
	m_localIP = lanAddress();
	m_join.active = TRUE;
	newKeyPair();
	if (!::Engine::secureRandom(m_join.nonce, sizeof(m_join.nonce)))
		DEBUG_CRASH(("GameLobby::startJoin - no random numbers from the system"));
	m_joinDenyDetail.clear();
	openGameTransport();
	m_join.nextSend = timeGetTime();
}

void GameLobby::RequestGameJoin( LANGameInfo *game, UnsignedInt ip /* = 0 */ )
{
	if (m_pendingAction != ACT_NONE || m_join.active || !m_inLobby)
	{
		OnGameJoin( RET_BUSY, nullptr );
		return;
	}
	if (game && game->getListedCode().isNotEmpty())
	{
		RequestGameJoinInvite(game->getListedCode());
		return;
	}
	if (!game || game->getSessionId() == 0 || !(game->getHostEndpoint().isV4() || game->getHostEndpoint().isV6()))
	{
		OnGameJoin( RET_GAME_GONE, nullptr );
		return;
	}
	startJoin();
	if (!m_gameTransport)
	{
		finishJoin(RET_UNKNOWN);
		return;
	}
	m_join.candidates[0] = game->getHostEndpoint();
	m_join.candidateCount = 1;
	// a running game in the list can be watched
	m_join.wants = game->isGameInProgress() ? ::Engine::SessionWants::WATCH : ::Engine::SessionWants::PLAY;
	m_join.sessionId = game->getSessionId();
	m_join.gameSessionId = game->getSessionId();
	m_join.hasFingerprint = TRUE;
	memcpy(m_join.fingerprint, game->getHostFingerprint(), sizeof(m_join.fingerprint));
	m_join.deadline = timeGetTime() + Timing::JOIN_LAN;
	m_pendingAction = ACT_JOIN;
	sendHello(timeGetTime());
}

void GameLobby::RequestGameJoinDirectConnect( UnsignedInt ipaddress )
{
	::Engine::NetAddress host;
	if (::Engine::NetAddressBook::isHandle(ipaddress))
		TheNetAddressBook().addressOf(ipaddress, host);
	else if (ipaddress != 0)
		host = ::Engine::NetAddress::fromIPv4(ipaddress);
	if (!(host.isV4() || host.isV6()))
	{
		OnGameJoin( RET_GAME_GONE, nullptr );
		return;
	}
	host.port = m_lobbyPort;
	RequestGameJoinAt(host);
}

void GameLobby::RequestGameJoinAt( const ::Engine::NetAddress &hostAddress )
{
	if (m_pendingAction != ACT_NONE || m_join.active)
	{
		OnGameJoin( RET_BUSY, nullptr );
		return;
	}
	const AsciiString expected = m_expectedHostCode;
	startJoin();
	if (!m_gameTransport)
	{
		finishJoin(RET_UNKNOWN);
		return;
	}
	m_join.candidates[0] = hostAddress;
	if (m_join.candidates[0].port == 0)
		m_join.candidates[0].port = m_lobbyPort;
	m_join.candidateCount = 1;
	m_join.wants = ::Engine::SessionWants::PLAY_OR_WATCH;
	m_join.expectedCode = expected;
	m_join.deadline = timeGetTime() + Timing::JOIN_LAN;
	m_pendingAction = ACT_JOINDIRECTCONNECT;
	sendHello(timeGetTime());
}

Bool GameLobby::RequestGameJoinInvite( AsciiString code )
{
	::Engine::InviteCode invite;
	if (!::Engine::parseInviteCode(code.str(), invite))
		return FALSE;
	if (m_pendingAction != ACT_NONE || m_join.active)
	{
		OnGameJoin( RET_BUSY, nullptr );
		return TRUE;
	}
	startJoin();
	if (!m_gameTransport)
	{
		finishJoin(RET_UNKNOWN);
		return TRUE;
	}
	m_join.room = invite.room;
	m_join.wants = ::Engine::SessionWants::PLAY_OR_WATCH;
	m_joinedInvite = code;
	m_join.expectedCode = invite.fingerprint.c_str();
	if (invite.server.kind == ::Engine::NetTarget::Kind::Address)
	{
		::Engine::NetAddress a = invite.server.address;
		a.port = invite.serverPort();
		m_join.servers.push_back(a.toString(true));
	}
	else if (invite.server.kind == ::Engine::NetTarget::Kind::HostName)
	{
		char port[8];
		snprintf(port, sizeof(port), "%u", static_cast<unsigned>(invite.serverPort()));
		m_join.servers.push_back(invite.server.hostName + ":" + port);
	}
	else
	{
		m_join.servers = m_servers.list();
	}
	m_join.deadline = timeGetTime() + Timing::JOIN_INVITE;
	m_pendingAction = ACT_JOINDIRECTCONNECT;
	if (m_join.servers.empty())
	{
		finishJoin(RET_NOT_FOUND);
		return TRUE;
	}
	m_join.nextLookup = timeGetTime();
	lookupInvite(timeGetTime());
	return TRUE;
}

// Asks the servers of the invitation in turn for the room; the one that knows it tells the host
// about us (KNOCK) and us where the host is (FOUND).
void GameLobby::lookupInvite( UnsignedInt now )
{
	if (m_join.found || !due(now, m_join.nextLookup))
		return;
	if (m_join.lookups >= Limit::LOOKUP_ATTEMPTS)
	{
		++m_join.serverIndex;
		m_join.lookups = 0;
		m_join.server = ::Engine::NetAddress();
	}
	while (!(m_join.server.isV4() || m_join.server.isV6()))
	{
		if (m_join.serverIndex >= static_cast<Int>(m_join.servers.size()))
		{
			finishJoin(RET_NOT_FOUND);
			return;
		}
		if (!m_servers.resolve(m_join.servers[static_cast<size_t>(m_join.serverIndex)], m_join.server, hasIPv6()))
			++m_join.serverIndex;
	}
	++m_join.lookups;
	m_join.nextLookup = now + Timing::LOOKUP_RESEND;

	::Engine::SessionEndpoints own;
	ownEndpoints(own);

	::Engine::TreffMessage observe;
	observe.type = ::Engine::TreffType::OBSERVE;
	m_observeTag = randomTag();
	observe.tag = m_observeTag;
	observe.socket = ::Engine::TreffSocket::GAME;
	sendTreff(m_gameTransport.get(), observe, m_join.server, ::Engine::TREFF_OBSERVE_SIZE);

	::Engine::TreffMessage lookup;
	lookup.type = ::Engine::TreffType::LOOKUP;
	if (m_join.lookupTag == 0)
		m_join.lookupTag = randomTag();
	lookup.tag = m_join.lookupTag;
	lookup.protocol = LAN_PROTOCOL_VERSION;
	lookup.room = m_join.room;
	for (Int i = 0; i < ::Engine::TREFF_MAX_ENDPOINTS; ++i)
	{
		lookup.lobby[i] = own.lobby[i];
		lookup.gameSocket[i] = own.game[i];
	}
	sendTreff(m_transport.get(), lookup, m_join.server, ::Engine::TREFF_LOOKUP_SIZE);
}

// HELLO to every endpoint the host may answer from until one answers, then JOIN to it.
void GameLobby::sendHello( UnsignedInt now )
{
	if (!m_join.active || !due(now, m_join.nextSend))
		return;
	if (m_join.challenged)
	{
		if (!m_join.joinMessage.empty())
			m_transport->sendRaw(m_join.host, m_join.joinMessage.data(), static_cast<Int>(m_join.joinMessage.size()));
		m_join.nextSend = now + Timing::JOIN_RESEND;
		return;
	}
	::Engine::SessionMessage hello;
	hello.type = ::Engine::SessionType::HELLO;
	hello.protocol = LAN_PROTOCOL_VERSION;
	hello.sessionId = m_join.sessionId;
	memcpy(hello.key, m_keyPair.publicKey, sizeof(hello.key));
	memcpy(hello.nonce, m_join.nonce, sizeof(hello.nonce));
	for (Int i = 0; i < m_join.candidateCount; ++i)
		sendPlain(hello, m_join.candidates[i]);
	m_join.nextSend = now + Timing::HELLO_RESEND;
}

void GameLobby::finishJoin( ReturnType result )
{
	const UnsignedInt hostIdentity = m_join.challenged ? ::Engine::SessionHandshake::identityOf(m_join.hostKey) : 0;
	LANGameInfo *game = m_join.gameSessionId != 0 ? LookupGame(sessionGameName(m_join.gameSessionId)) : nullptr;
	m_join = JoinAttempt();
	if (hostIdentity != 0)
		m_transport->removePeerKeys(hostIdentity);
	closeGameTransport();
	m_pendingAction = ACT_NONE;
	m_localIP = lanAddress();
	m_expectedHostCode.clear();
	OnGameJoin(result, game);
}

void GameLobby::joinUpdate( UnsignedInt now )
{
	if (!m_join.active)
		return;
	if (due(now, m_join.deadline))
	{
		// no answer at all from an address typed in: most often the host's router keeps us out
		if (m_join.room.empty() && !m_join.challenged && m_pendingAction == ACT_JOINDIRECTCONNECT)
			m_joinDenyDetail.formatUnchecked(TheGameText->fetch("GZH:LanNoAnswer").str(), m_lobbyPort, m_gamePort);
		finishJoin(!m_join.room.empty() && !m_join.found ? RET_NOT_FOUND : RET_TIMEOUT);
		return;
	}
	if (!m_join.room.empty())
	{
		lookupInvite(now);
		if (!m_join.active)
			return;
	}
	if (m_join.candidateCount > 0)
		sendHello(now);
}

void GameLobby::memberUpdate( UnsignedInt now )
{
	if (!m_currentGame || m_inLobby || AmIHost() || m_currentGame->isGameInProgress())
		return;
	const UnsignedInt host = m_links.member(0).identity;
	if (due(now, m_nextPing))
	{
		::Engine::SessionMessage ping;
		ping.type = ::Engine::SessionType::PING;
		ping.value = now;
		sendToPeer(ping, host);
		m_nextPing = now + Timing::PING;
	}
	if (silentFor(now, m_hostHeard, Timing::SILENCE))
	{
		leaveGame(TRUE);
		OnChat(UnicodeString::TheEmptyString, m_localIP, TheGameText->fetch("LAN:HostNotResponding"), LANCHAT_SYSTEM);
		return;
	}
	// players the host no longer lists
	for (Int slot = 1; slot < MAX_SLOTS && m_lastStateRevision != 0; ++slot)
	{
		const UnsignedInt identity = m_links.member(slot).identity;
		if (identity == 0 || identity == m_localIP)
			continue;
		const LANGameSlot *s = m_currentGame->getConstLANSlot(slot);
		if (!s->isHuman() || s->getIP() != identity)
			m_links.clearMember(slot);
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

void GameLobby::handleChallenge( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d )
{
	if (!m_join.active)
		return;
	Bool known = FALSE;
	for (Int i = 0; i < m_join.candidateCount && !known; ++i)
		known = m_join.candidates[i] == d.from;
	if (!known || memcmp(m.nonce, m_join.nonce, sizeof(m.nonce)) != 0)
		return;
	if (m_join.sessionId != 0 && m.sessionId != m_join.sessionId)
		return;
	if (m_join.challenged)
	{
		// the host's cookie key rotated while our JOIN was under way
		if (d.from == m_join.host && memcmp(m.key, m_join.hostKey, sizeof(m.key)) == 0
			&& memcmp(m.cookie, m_join.cookie, sizeof(m.cookie)) != 0)
		{
			memcpy(m_join.cookie, m.cookie, sizeof(m.cookie));
			m_join.joinMessage.clear();
		}
		else
		{
			return;
		}
	}
	else
	{
		// the key must be the one the invitation, the typed game key or the announcement named
		if (!m_join.expectedCode.isEmpty() && !::Engine::sealFingerprintMatches(m_join.expectedCode.str(), m.key))
		{
			DEBUG_LOG(("GameLobby::handleChallenge - the host's key does not match the game key %s\n", m_join.expectedCode.str()));
			finishJoin(RET_HOST_KEY_MISMATCH);
			return;
		}
		if (m_join.hasFingerprint)
		{
			UnsignedByte fingerprint[5];
			fingerprintBytes(m.key, fingerprint);
			if (memcmp(fingerprint, m_join.fingerprint, sizeof(fingerprint)) != 0)
			{
				finishJoin(RET_HOST_KEY_MISMATCH);
				return;
			}
		}
		memcpy(m_join.hostKey, m.key, sizeof(m.key));
		memcpy(m_join.cookie, m.cookie, sizeof(m.cookie));
		m_join.host = d.from;
		m_join.sessionId = m.sessionId;
		m_join.challenged = TRUE;

		const UnsignedInt hostIdentity = ::Engine::SessionHandshake::identityOf(m.key);
		::Engine::SealSessionKeys keys;
		if (!::Engine::deriveSessionKeys(m_keyPair, m.key, LOBBY_CONTEXT, sizeof(LOBBY_CONTEXT), keys))
		{
			finishJoin(RET_UNKNOWN);
			return;
		}
		m_transport->setPeerKeys(hostIdentity, m_lobbyPort, keys);
		keys.wipe();
		m_transport->bindPeer(hostIdentity, d.from);
	}

	// JOIN, built once and repeated until WELCOME
	::Engine::SessionJoinInfo info;
	info.name = toUtf8(m_name);
	info.protocol = LAN_PROTOCOL_VERSION;
	info.programCRC = TheGlobalData->m_exeCRC;
	info.dataCRC = TheGlobalData->m_iniCRC;
	info.versionText = LANVersionText().str();
	if (info.versionText.size() > ::Engine::SESSION_MAX_VERSION)
		info.versionText.resize(::Engine::SESSION_MAX_VERSION);
	ownEndpoints(info.endpoints);
	info.wants = m_join.wants;
	if (info.wants == ::Engine::SessionWants::RETURN)
	{
		info.returnSlot = static_cast<std::uint8_t>(m_join.returnSlot);
		memcpy(info.returnToken, m_join.returnToken, sizeof(info.returnToken));
	}
	std::uint8_t plain[::Engine::SESSION_MAX_SEALED];
	const std::size_t plainSize = ::Engine::encodeJoinInfo(info, plain, sizeof(plain) - ::Engine::SessionHandshake::JOIN_SEAL_OVERHEAD);
	::Engine::SessionMessage join;
	join.type = ::Engine::SessionType::JOIN;
	join.sessionId = m_join.sessionId;
	memcpy(join.key, m_keyPair.publicKey, sizeof(join.key));
	memcpy(join.nonce, m_join.nonce, sizeof(join.nonce));
	memcpy(join.cookie, m_join.cookie, sizeof(join.cookie));
	Encoded wire;
	if (plainSize == 0 || !::Engine::SessionHandshake::sealJoin(m_keyPair, m_join.hostKey, m_join.nonce, plain, plainSize, join.sealed)
		|| !encode(join, wire))
	{
		finishJoin(RET_UNKNOWN);
		return;
	}
	m_join.joinMessage.assign(wire.data, wire.data + wire.size);
	m_join.nextSend = timeGetTime();
	sendHello(timeGetTime());
}

void GameLobby::handleRefuse( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d )
{
	if (!m_join.active)
		return;
	Bool known = FALSE;
	for (Int i = 0; i < m_join.candidateCount && !known; ++i)
		known = m_join.candidates[i] == d.from;
	if (!known || (m_join.sessionId != 0 && m.sessionId != m_join.sessionId))
		return;
	const ReturnType reason = m.reason < RET_MAX ? static_cast<ReturnType>(m.reason) : RET_UNKNOWN;
	if (reason == RET_VERSION_MISMATCH)
	{
		UnicodeString host;
		UnicodeString mine;
		host.translate(AsciiString(m.versionText.c_str()));
		mine.translate(LANVersionText());
		m_joinDenyDetail.formatUnchecked(TheGameText->fetch("GZH:LanVersionDetail").str(),
			host.str(), m.protocol, mine.str(), LAN_PROTOCOL_VERSION);
	}
	// no way back into that game any more
	if (m_join.wants == ::Engine::SessionWants::RETURN && reason == RET_GAME_GONE)
		clearReturnTicket();
	finishJoin(reason == RET_OK ? RET_UNKNOWN : reason);
}

void GameLobby::handleWelcome( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d )
{
	if (m.slot == ::Engine::SESSION_SPECTATOR_SLOT)
	{
		startWatching(m);
		return;
	}
	if (m.flag)
	{
		if (m_join.wants == ::Engine::SessionWants::RETURN)
			startReturn(m);
		return;
	}
	const UnsignedInt own = ::Engine::SessionHandshake::identityOf(m_keyPair.publicKey);
	if (m.sessionId != m_join.sessionId || m.slot < 1 || m.slot >= MAX_SLOTS || m.identity != own)
		return;
	const UnsignedInt hostIdentity = ::Engine::SessionHandshake::identityOf(m_join.hostKey);
	const UnsignedInt now = timeGetTime();

	m_sessionId = m.sessionId;
	m_localIP = own;
	linkGame();
	LANGameInfo *game = m_directory.findGame(sessionGameName(m.sessionId));
	const Bool listed = game != nullptr;
	if (!listed)
	{
		auto created = std::make_unique<LANGameInfo>();
		created->setName(sessionGameName(m.sessionId));
		game = m_directory.adoptGame(std::move(created));
	}
	game->setLocalIP(own);
	if (listed)
	{
		// the options as announced stay until the host's first STATE
		AsciiString options = GameInfoToAsciiString(game);
		game->enterGame();
		ParseAsciiStringToGameInfo(game, options);
	}
	else
	{
		game->enterGame();
	}

	LANGameSlot *hostSlot = game->getLANSlot(0);
	if (!hostSlot->isHuman())
		hostSlot->setState(SLOT_PLAYER, UnicodeString(L"?"));
	hostSlot->setIP(hostIdentity);

	LANGameSlot slot;
	slot.setState(SLOT_PLAYER, m_name);
	slot.setIP(own);
	slot.setPort(m_gamePort);
	slot.setLastHeard(0);
	slot.setLogin(m_userName);
	slot.setHost(m_hostName);
	game->setSlot(m.slot, slot);
	game->setPublicKey(0, m_join.hostKey);
	game->setPublicKey(m.slot, m_keyPair.publicKey);
	UnsignedByte fingerprint[5];
	fingerprintBytes(m_join.hostKey, fingerprint);
	game->setSession(m_sessionId, m_join.host, fingerprint);
	game->setIsDirectConnect(m_pendingAction == ACT_JOINDIRECTCONNECT);

	m_links.setMember(0, hostIdentity, m_join.hostKey, now);
	m_links.setMember(m.slot, own, m_keyPair.publicKey, now);
	memcpy(m_returnToken, m.token, sizeof(m_returnToken));
	m_hasReturnToken = TRUE;
	m_currentGame = game;
	m_inLobby = FALSE;
	m_hostHeard = now;
	m_nextPing = now;
	m_lastStateRevision = 0;
	m_pendingAction = ACT_NONE;
	m_expectedHostCode.clear();
	m_showGameCode = TRUE;
	const ::Engine::NetAddress hostEndpoint = m_join.host;
	std::vector<Transport::RawDatagram> early;
	early.swap(m_join.early);
	m_join = JoinAttempt();

	// the address list of the direct connect menu (IPv4 at the lobby port only)
	if (hostEndpoint.isV4() && hostEndpoint.port == LAN_LOBBY_PORT)
	{
		const UnsignedInt ip = hostEndpoint.ipv4();
		// replaces the newest entry, which is the address just entered in the menu
		LobbySettings prefs(LobbySettings::Kind::Lan);
		std::vector<AsciiString> entries = prefs.getRemoteIPs();
		if (!entries.empty())
		{
			entries[0].format("%d.%d.%d.%d:%s", ip >> 24, (ip & 0xff0000) >> 16, (ip & 0xff00) >> 8, ip & 0xff, UnicodeStringToQuotedPrintable(m_currentGame->getSlot(0)->getName()).str());
			prefs.setRemoteIPs(entries);
			prefs.write();
		}
	}

	OnGameJoin(RET_OK, m_currentGame);
	for (const Transport::RawDatagram &e : early)
	{
		if (LANbuttonPushed && m_currentGame == nullptr)
			break;
		handleDatagram(e, FALSE);
	}
}

// Keys and endpoints of the players, from the host.
void GameLobby::handleMember( const ::Engine::SessionMessage &m )
{
	const Int slot = m.member.slot;
	if (slot < 0 || slot >= MAX_SLOTS || ::Engine::SessionHandshake::identityOf(m.member.key) != m.member.identity)
		return;
	if (m.member.identity == m_localIP)
		return;
	if (slot == 0 && m.member.identity != m_links.member(0).identity)
		return;	// the host's key came with the handshake and stays
	for (Int other = 1; other < MAX_SLOTS; ++other)
	{
		if (other != slot && m_links.member(other).identity == m.member.identity)
		{
			m_links.clearMember(other);
		}
	}
	m_links.setMember(slot, m.member.identity, m.member.key, timeGetTime());
	m_links.member(slot).endpoints = m.member.endpoints;
}

// The relay session of the game and our token for it, from the host.
void GameLobby::handleRelay( const ::Engine::SessionMessage &m )
{
	::Engine::NetAddress server;
	if (m.relayId != 0 && m_servers.resolve(m.relayServer, server, hasIPv6()))
		m_links.joinRelay(server, m.relayServer, m.relayId, m.relayToken, timeGetTime());
}

// A sealed message of the host of the game we joined.
void GameLobby::memberReceives( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d )
{
	const UnsignedInt now = timeGetTime();
	m_hostHeard = now;
	const Bool open = !m_currentGame->isGameInProgress();
	switch (m.type)
	{
		case ::Engine::SessionType::STATE:
			// an older revision is a late duplicate (revisions wrap)
			if (!open || (m.revision < m_lastStateRevision && m_lastStateRevision - m.revision < 0x80000000u))
				break;
			m_lastStateRevision = m.revision;
			OnGameOptions(d.sender, 0, AsciiString(m.options.c_str()));
			break;
		case ::Engine::SessionType::MEMBER:
			if (open)
				handleMember(m);
			break;
		case ::Engine::SessionType::ACCEPT:
			if (open)
				OnAccept(d.sender, m.flag);
			break;
		case ::Engine::SessionType::CHAT:
		{
			if (m.slot >= MAX_SLOTS || !m_chatLimiter.allow(d.sender, now))
				break;
			const LANGameSlot *s = m_currentGame->getConstLANSlot(m.slot);
			if (s->isHuman())
				OnChat(s->getName(), s->getIP(), fromUtf8(m.text), chatTypeOf(m.chatKind));
			break;
		}
		case ::Engine::SessionType::TIMER:
			if (open && m.value <= 60)
				OnGameStartTimer(static_cast<Int>(m.value));
			break;
		case ::Engine::SessionType::START:
			if (open)
				OnGameStart();
			break;
		case ::Engine::SessionType::LEAVE:
			if (open)
				leaveGame(TRUE);
			break;
		case ::Engine::SessionType::RELAY:
			if (open)
				handleRelay(m);
			break;
		default:
			break;
	}
}

// Answers of the server asked for an invitation's room.
void GameLobby::joinAnswer( const ::Engine::TreffMessage &m, const Transport::RawDatagram &d, Bool gameSocket )
{
	const UnsignedInt now = timeGetTime();
	switch (m.type)
	{
		case ::Engine::TreffType::FOUND:
			if (!m_join.active || gameSocket || m.tag != m_join.lookupTag || d.from != m_join.server || m.room != m_join.room
				|| m_join.found)
			{
				break;
			}
			m_join.found = TRUE;
			m_join.candidateCount = 0;
			for (const ::Engine::WireEndpoint &e : m.lobby)
			{
				if (e.isSet() && m_join.candidateCount < ::Engine::TREFF_MAX_ENDPOINTS)
					m_join.candidates[m_join.candidateCount++] = netAddress(e);
			}
			for (Int i = 0; i < ::Engine::TREFF_MAX_ENDPOINTS; ++i)
				m_links.member(0).endpoints.game[i] = m.gameSocket[i];
			m_join.nextSend = now;
			sendHello(now);
			break;
		case ::Engine::TreffType::REFUSED:
			if (m_join.active && !gameSocket && m.tag == m_join.lookupTag && d.from == m_join.server && !m_join.found)
			{
				// this server does not know the room: the next one
				m_join.lookups = Limit::LOOKUP_ATTEMPTS;
				m_join.nextLookup = now;
			}
			break;
		default:
			break;
	}
}
