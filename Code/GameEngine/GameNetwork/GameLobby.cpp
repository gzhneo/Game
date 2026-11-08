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

// GameLobby.cpp: lifecycle, the update loop, sending and the requests of the menus. Hosting is in
// GameLobbyHost.cpp, joining in GameLobbyJoin.cpp, every received message in GameLobbyReceive.cpp.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // only bare bones windows stuff wanted
#endif

#include "Common/ArchiveFileSystem.h"
#include "Common/crc.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/version.h"
#include "Engine/ModCatalog.h"
#include "Engine/PortMapping.h"
#include "GameClient/Clipboard.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/OptionsMenuMods.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/Lobby/LobbyTiming.h"
#include "GameNetwork/networkutil.h"
#include "GameNetwork/PortMapper.h"
#include "Lib/SafeString.h"
#include "Engine/UserDataFiles.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern Bool LANbuttonPushed;
extern Bool LANSocketErrorDetected;

using namespace Lobby;

// Readable version for join denials: the version number alone does not tell two builds apart.
AsciiString LANVersionText()
{
	AsciiString text;
	if (TheVersion)
		text.format("%s (%s)", TheVersion->getAsciiVersion().str(), TheVersion->getAsciiBuildTime().str());
	return text;
}

static UnsignedShort versionTag( UnsignedInt crc )
{
	return static_cast<UnsignedShort>(crc ^ (crc >> 16));
}

UnsignedShort LANProgramTag()
{
	return versionTag(TheGlobalData ? TheGlobalData->m_exeCRC : 0);
}

UnsignedShort LANDataTag()
{
	return versionTag(TheGlobalData ? TheGlobalData->m_iniCRC : 0);
}

// Mirrors the checks of handleJoin on what a game announcement carries.
GameLobbyInterface::ReturnType GameLobby::compatibilityOf( LANGameInfo *game )
{
	if (game == nullptr || (game == m_currentGame && AmIHost()))
		return RET_OK;
	if (game->getHostProtocol() != LAN_PROTOCOL_VERSION)
		return RET_VERSION_MISMATCH;
#if defined(_DEBUG)
	if (TheGlobalData->m_netMinPlayers <= 0)
		return RET_OK;
#endif
	if (game->getHostProgramTag() != LANProgramTag())
		return RET_VERSION_MISMATCH;
	if (game->getHostDataTag() != LANDataTag())
		return RET_DATA_MISMATCH;
	return RET_OK;
}

UnsignedInt GetLocalNetInstanceIP()
{
	if (!TheGlobalData)
		return 0;
	const Int n = TheGlobalData->m_netLocalInstance;
	if (n < 1 || n > MAX_LOCAL_NET_INSTANCES)
		return 0;
	return LocalNetInstanceIP(n);
}

GameLobby::GameLobby( void )
	: m_localIP(GetLocalNetInstanceIP())	// binds the right address from the first init() on
	, m_bindIP(m_localIP)
	, m_lobbyPort(LAN_LOBBY_PORT)
	, m_gamePort(NETWORK_BASE_PORT_NUMBER)
	, m_transport(std::make_unique<Transport>())
	, m_forceRelay(FALSE)
	, m_offline(FALSE)
	, m_observeTag(0)
	, m_currentGame(nullptr)
	, m_inLobby(TRUE)
	, m_isInLANMenu(TRUE)
	, m_pendingAction(ACT_NONE)
	, m_lastResendTime(0)
	, m_lastUpdate(0)
	, m_isActive(TRUE)
	, m_gameStartTime(0)
	, m_gameStartSeconds(0)
	, m_showGameCode(FALSE)
	, m_portMapperChanges(ThePortMapper().changeCount())
	, m_portMapperReported(ThePortMapper().state())
	, m_pinholesReported(ThePortMapper().pinholeState())
	, m_routerLogNamed(FALSE)
	, m_messagesBuilt(0)
	, m_sessionId(0)
	, m_hostHeard(0)
	, m_nextPing(0)
	, m_lastStateRevision(0)
{
	::Engine::RateLimit replies;	// one common budget (sender 0); a new player asks once
	replies.rate = 5;
	replies.burst = 10;
	m_replyLimiter.configure(replies, replies);
	::Engine::RateLimit chatPerSender;
	chatPerSender.rate = 3;
	chatPerSender.burst = 10;
	::Engine::RateLimit chatShared;
	chatShared.rate = 10;
	chatShared.burst = 20;
	m_chatLimiter.configure(chatPerSender, chatShared);
	clearSession();
}

GameLobby::~GameLobby( void )
{
	ThePortMapper().stop();
	reset();
	closeGameTransport();
}

// What belongs to one game, hosted or joined.
void GameLobby::clearSession( void )
{
	endWatchHost();
	stopWatching(TRUE);
	stopReturn(TRUE, nullptr);
	m_hasReturnToken = FALSE;
	m_sessionId = 0;
	m_host = HostState();
	m_room.reset();
	m_inviteCode.clear();
	m_join = JoinAttempt();
	m_links.reset();
	m_hostHeard = 0;
	m_nextPing = 0;
	m_lastStateRevision = 0;
	m_publicLobby = ::Engine::WireEndpoint();
	m_publicGame = ::Engine::WireEndpoint();
	m_observeTag = 0;
	m_gameStartTime = 0;
	m_gameStartSeconds = 0;
	m_keyPair.wipe();
	if (m_transport)
		m_transport->retainPeers(nullptr, 0);
	linkGame();
}

Bool GameLobby::openLobbyTransport( void )
{
	m_transport->reset();
	const Bool ok = m_transport->init(m_bindIP, m_lobbyPort);
	m_transport->allowBroadcasts(TRUE);
	m_transport->setSealingOptional(TRUE);	// game searches stay plain, messages within a game are sealed
	m_transport->setMaxPacketSize(MAX_MESSAGE_LEN);
	return ok;
}

Bool GameLobby::openGameTransport( void )
{
	if (m_gameTransport)
		return TRUE;
	auto socket = std::make_unique<Transport>();
	if (!m_offline && !socket->init(m_bindIP, m_gamePort))
		return FALSE;
	m_gameTransport = std::move(socket);
	linkGame();
	return TRUE;
}

void GameLobby::closeGameTransport( void )
{
	m_gameTransport.reset();
	linkGame();
}

Transport *GameLobby::takeGameTransport( void )
{
	Transport *socket = m_gameTransport.release();
	linkGame();	// the game owns the socket now; the links must not touch it any more
	return socket;
}

void GameLobby::linkGame( void )
{
	GameLinks::Context context;
	context.gameSocket = m_gameTransport.get();
	context.ownKeys = &m_keyPair;
	context.sessionId = m_sessionId;
	context.ownIdentity = m_localIP;
	context.gamePort = m_gamePort;
	context.forceRelay = m_forceRelay || (TheGlobalData != nullptr && TheGlobalData->m_netForceRelay);
	m_links.setContext(context);
}

void GameLobby::init( void )
{
	closeGameTransport();
	clearSession();
	m_localIP = lanAddress();
	openLobbyTransport();

	m_pendingAction = ACT_NONE;
	m_inLobby = TRUE;
	m_isInLANMenu = TRUE;
	m_currentGame = nullptr;

	unsigned long bufSize = UNLEN + 1;
	char userName[UNLEN + 1];
	if (!GetUserName(userName, &bufSize))
		safe_strcpy(userName, "unknown");
	m_userName = userName;

	bufSize = MAX_COMPUTERNAME_LENGTH + 1;
	char computerName[MAX_COMPUTERNAME_LENGTH + 1];
	if (!GetComputerName(computerName, &bufSize))
		safe_strcpy(computerName, "unknown");
	m_hostName = computerName;

	m_servers.load();
}

void GameLobby::reset( void )
{
	if (m_inLobby && m_transport)
	{
		::Engine::SessionMessage bye;
		bye.type = ::Engine::SessionType::BYE;
		broadcast(bye);
	}
	m_room.unregister(m_transport.get());
	closeGameTransport();
	clearSession();
	m_currentGame = nullptr;
	m_directory.clear();
	m_pendingAction = ACT_NONE;
	m_inLobby = TRUE;
	m_isInLANMenu = TRUE;
	m_localIP = lanAddress();
}

void GameLobby::checkMOTD( void )
{
#if defined(_DEBUG)
	if (TheGlobalData->m_useLocalMOTD)
	{
		// for a playtest, let's log some play statistics, eh?
		if (TheGlobalData->m_playStats <= 0)
			TheWritableGlobalData->m_playStats = 30;

		static UnsignedInt oldMOTDCRC = 0;
		UnsignedInt newMOTDCRC = 0;
		AsciiString asciiMOTD;
		char buf[4096];
		FILE *fp = fopen(TheGlobalData->m_MOTDPath.str(), "r");
		Int len;
		if (fp)
		{
			while( (len = static_cast<Int>(fread(buf, 1, 4095, fp))) > 0 )
			{
				buf[len] = 0;
				asciiMOTD.concat(buf);
			}
			fclose(fp);
			CRC crcObj;
			crcObj.computeCRC(asciiMOTD.str(), asciiMOTD.getLength());
			newMOTDCRC = crcObj.get();
		}

		if (oldMOTDCRC != newMOTDCRC)
		{
			// different MOTD... display it
			oldMOTDCRC = newMOTDCRC;
			AsciiString line;
			while (asciiMOTD.nextToken(&line, "\n"))
			{
				if (line.getCharAt(line.getLength()-1) == '\r')
					line.removeLastChar();	// there is a trailing '\r'

				if (line.isEmpty())
				{
					line = " ";
				}

				UnicodeString uniLine;
				uniLine.translate(line);
				OnChat( UnicodeString(L"MOTD"), 0, uniLine, LANCHAT_SYSTEM );
			}
		}
	}
#endif
}

void GameLobby::systemChat( const UnicodeString &text )
{
	OnChat(UnicodeString(L"SYSTEM"), m_localIP, text, LANCHAT_SYSTEM);
}

// ------------------------------------------------------------------------------------------------
// update

void GameLobby::update( void )
{
	// a spectator or a returning player is served by serviceInGame() from the welcome on
	if (LANbuttonPushed || m_watch.active || m_return.active)
		return;
	const UnsignedInt now = timeGetTime();
	if (!due(now, m_lastUpdate + Timing::UPDATE))
		return;
	m_lastUpdate = now;

	// the players of our game keep their packet budget during a flood
	m_transport->clearTrustedSenders();
	if (m_currentGame && !m_inLobby)
	{
		for (Int slot = 0; slot < MAX_SLOTS; ++slot)
		{
			if (m_currentGame->getConstSlot(slot)->isHuman())
				m_transport->setTrustedSender(m_currentGame->getIP(slot), TRUE);
		}
	}

	reportPortMapping();

	if (!m_transport->update() && !LANSocketErrorDetected && m_isInLANMenu)
		LANSocketErrorDetected = TRUE;
	if (m_gameTransport)
		m_gameTransport->update();

	receive(m_transport.get(), FALSE);
	if (LANbuttonPushed)
		return;
	if (m_gameTransport)
		receive(m_gameTransport.get(), TRUE);
	if (LANbuttonPushed)
		return;

	if (m_inLobby && m_pendingAction == ACT_NONE && !m_join.active && due(now, m_lastResendTime + Timing::PRESENCE))
	{
		m_lastResendTime = now;
		sendPresence();
	}
	if (m_mode == Mode::Internet && m_inLobby && !m_currentGame)
	{
		m_publicGames.poll(now, m_servers, hasIPv6(), m_transport.get());
		m_publicGames.expire(now);
		syncPublicGames(now);
	}

	joinUpdate(now);
	if (LANbuttonPushed)
		return;
	hostUpdate(now);
	if (LANbuttonPushed)
		return;
	memberUpdate(now);
	if (LANbuttonPushed)
		return;
	showGameKey();

	const Directory::Expired expired = m_directory.expire(now, Timing::GAME_LIST, Timing::PLAYER_LIST, m_currentGame, m_localIP);
	if (expired.players)
		OnPlayerList(m_directory.players());
	if (expired.games)
		OnGameList(m_directory.games());

	// the start countdown: m_gameStartTime is when the next message goes out, m_gameStartSeconds how
	// many seconds it names
	if (m_gameStartTime && m_gameStartSeconds && due(now, m_gameStartTime))
	{
		RequestGameStartTimer(m_gameStartSeconds);
	}
	else if (m_gameStartTime && due(now, m_gameStartTime))
	{
		ResetGameStartTimer();
		RequestGameStart();
	}

	static UnsignedInt lastMOTDCheck = 0;
	static const UnsignedInt motdInterval = 30000;
	if (now > lastMOTDCheck + motdInterval)
	{
		checkMOTD();
		lastMOTDCheck = now;
	}
}

// ------------------------------------------------------------------------------------------------
// sending

Bool GameLobby::encode( const ::Engine::SessionMessage &message, Encoded &out )
{
	if (!Lobby::encode(message, out))
		return FALSE;
	++m_messagesBuilt;
	return TRUE;
}

void GameLobby::sendPlain( const ::Engine::SessionMessage &message, const ::Engine::NetAddress &to )
{
	Encoded wire;
	if (encode(message, wire))
		m_transport->sendRaw(to, wire.data, wire.size);
}

void GameLobby::sendToPeer( const ::Engine::SessionMessage &message, UnsignedInt identity )
{
	Encoded wire;
	if (identity != 0 && identity != m_localIP && encode(message, wire))
		m_transport->sendSealed(identity, m_lobbyPort, wire.data, wire.size);
}

void GameLobby::sendToMembers( const ::Engine::SessionMessage &message, UnsignedInt except )
{
	Encoded wire;
	if (!m_currentGame || !encode(message, wire))
		return;
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		const LANGameSlot *s = m_currentGame->getConstLANSlot(slot);
		const UnsignedInt identity = s->getIP();
		if (s->isHuman() && identity != m_localIP && identity != except && m_transport->hasPeerKeys(identity))
			m_transport->sendSealed(identity, m_lobbyPort, wire.data, wire.size);
	}
}

void GameLobby::broadcast( const ::Engine::SessionMessage &message )
{
	if (m_mode == Mode::Internet)
		return;
	Encoded wire;
	if (!encode(message, wire))
		return;
	if (!m_discoveryTargets.empty())
	{
		for (const ::Engine::NetAddress &target : m_discoveryTargets)
			m_transport->sendRaw(target, wire.data, wire.size);
	}
	else if (GetLocalNetInstanceIP() != 0)
	{
		// Loopback addresses receive no broadcasts; reach every possible local instance directly.
		for (Int n = 1; n <= MAX_LOCAL_NET_INSTANCES; ++n)
			m_transport->sendRaw(::Engine::NetAddress::fromIPv4(LocalNetInstanceIP(n), m_lobbyPort), wire.data, wire.size);
	}
	else
	{
		// Bound to every adapter: each local network gets its own broadcast, since the limited one
		// leaves on one adapter only.
		if (m_bindIP == 0)
		{
			refreshLanNetworks();
			for (const ::Engine::LocalNetwork &network : m_lanNetworks)
			{
				const UnsignedInt target = ::Engine::directedBroadcast(network);
				if (target != 0)
					m_transport->sendRaw(::Engine::NetAddress::fromIPv4(target, m_lobbyPort), wire.data, wire.size);
			}
		}
		m_transport->sendRaw(::Engine::NetAddress::fromIPv4(0xFFFFFFFFu, m_lobbyPort), wire.data, wire.size);
	}
}

void GameLobby::refreshLanNetworks( void )
{
	const UnsignedInt now = timeGetTime();
	if (m_lanNetworksTime != 0 && !silentFor(now, m_lanNetworksTime, Timing::LAN_NETWORKS))
		return;
	m_lanNetworksTime = now == 0 ? 1 : now;
	m_lanNetworks = LocalIPv4Networks();
}

UnsignedInt GameLobby::lanAddress( void ) const
{
	if (m_bindIP != 0)
		return m_bindIP;
	return m_lanNetworks.empty() ? 0 : m_lanNetworks.front().address;
}

namespace
{
	GameLobby::Mode s_menuMode = GameLobby::Mode::Lan;
}

void GameLobby::setMenuMode( Mode mode )
{
	s_menuMode = mode;
}

GameLobby::Mode GameLobby::menuMode( void )
{
	return s_menuMode;
}

void GameLobby::setMode( Mode mode )
{
	if (mode == m_mode)
		return;
	m_mode = mode;
	m_publicGames.reset();
	m_publicRevision = m_publicGames.revision();
	// the games of the other mode go
	std::vector<LANGameInfo *> others;
	for (LANGameInfo *game = m_directory.games(); game != nullptr; game = game->getNext())
	{
		if (game != m_currentGame)
			others.push_back(game);
	}
	for (LANGameInfo *game : others)
		m_directory.eraseGame(game);
	OnGameList(m_directory.games());
}

void GameLobby::setListedPublicly( Bool listed )
{
	if (listed == m_listPublicly)
		return;
	m_listPublicly = listed;
	m_room.refreshSoon(timeGetTime());
}

void GameLobby::syncPublicGames( UnsignedInt now )
{
	if (m_publicGames.revision() == m_publicRevision)
		return;
	m_publicRevision = m_publicGames.revision();

	std::vector<std::string> codes;
	for (const PublicGames::Entry &entry : m_publicGames.entries())
		codes.push_back(PublicGames::inviteCode(entry));
	std::vector<LANGameInfo *> gone;
	for (LANGameInfo *game = m_directory.games(); game != nullptr; game = game->getNext())
	{
		if (game != m_currentGame && std::find(codes.begin(), codes.end(), game->getListedCode().str()) == codes.end())
			gone.push_back(game);
	}
	for (LANGameInfo *game : gone)
		m_directory.eraseGame(game);

	for (size_t i = 0; i < codes.size(); ++i)
	{
		const PublicGames::Entry &entry = m_publicGames.entries()[i];
		const UnicodeString key = fromUtf8(codes[i]);
		LANGameInfo *game = m_directory.findGame(key);
		if (game == nullptr)
		{
			if (!m_directory.canAddGame())
				break;
			auto created = std::make_unique<LANGameInfo>();
			created->setName(key);
			created->enterGame();
			game = m_directory.adoptGame(std::move(created));
		}
		game->setListedCode(AsciiString(codes[i].c_str()));
		game->setMap(AsciiString(entry.game.mapName.c_str()));
		game->setHostVersion(LAN_PROTOCOL_VERSION, versionTag(entry.game.programCRC), versionTag(entry.game.dataCRC));
		game->setLastHeard(now);
		// the server knows how many play, not who: the others are shown without names
		for (Int slot = 0; slot < MAX_SLOTS; ++slot)
		{
			LANGameSlot s;
			if (slot == 0)
				s.setState(SLOT_PLAYER, fromUtf8(entry.game.hostName));
			else if (slot < entry.game.players)
				s.setState(SLOT_PLAYER, UnicodeString(L"?"));
			else if (slot < entry.game.maxPlayers)
				s.setState(SLOT_OPEN);
			else
				s.setState(SLOT_CLOSED);
			game->setSlot(slot, s);
		}
	}
	OnGameList(m_directory.games());
}

void GameLobby::sendPresence( void )
{
	::Engine::SessionMessage m;
	m.type = ::Engine::SessionType::PRESENCE;
	m.name = toUtf8(m_name);
	broadcast(m);
}

// ------------------------------------------------------------------------------------------------
// requests of the menus

void GameLobby::RequestLocations( void )
{
	::Engine::SessionMessage m;
	m.type = ::Engine::SessionType::QUERY;
	m.protocol = LAN_PROTOCOL_VERSION;
	broadcast(m);
}

void GameLobby::RequestGameLeave( void )
{
	if (!m_currentGame || m_inLobby)
		return;
	::Engine::SessionMessage leave;
	leave.type = ::Engine::SessionType::LEAVE;
	if (AmIHost())
	{
		sendToMembers(leave);
		sendToMembers(leave);
		m_room.unregister(m_transport.get());
	}
	else
	{
		sendToHost(leave);
		sendToHost(leave);
	}

	OnPlayerLeave(m_name);
	m_directory.eraseGame(m_currentGame);
	m_currentGame = nullptr;
	m_inLobby = TRUE;
	m_localIP = lanAddress();
	closeGameTransport();
	clearSession();
}

// The host is gone or sent us away.
void GameLobby::leaveGame( Bool hostLeft )
{
	if (!m_currentGame || m_inLobby)
		return;
	if (hostLeft)
		OnHostLeave();
	else
		OnPlayerLeave(m_name);
	m_directory.eraseGame(m_currentGame);
	m_currentGame = nullptr;
	m_inLobby = TRUE;
	m_localIP = lanAddress();
	closeGameTransport();
	clearSession();
	addSelfToLobby();
}

void GameLobby::addSelfToLobby( void )
{
	LANPlayer *self = m_directory.notePlayer(m_localIP, m_name, timeGetTime());
	if (self != nullptr)
	{
		self->setHost(m_hostName);
		self->setLogin(m_userName);
	}
}

void GameLobby::RequestGameAnnounce( void )
{
	sendAnnounce();
}

void GameLobby::RequestAccept( void )
{
	if (m_inLobby || !m_currentGame)
		return;
	::Engine::SessionMessage m;
	m.type = ::Engine::SessionType::ACCEPT;
	m.flag = true;
	if (AmIHost())
		sendToMembers(m);
	else
		sendToHost(m);
}

static UnsignedInt portableMapCRC( const AsciiString &map )
{
	CRC mapNameCRC;
	AsciiString portableMapName = TheGameState->realMapPathToPortableMapPath(map);
	mapNameCRC.computeCRC(portableMapName.str(), portableMapName.getLength());
	return mapNameCRC.get();
}

void GameLobby::RequestHasMap( void )
{
	if (m_inLobby || !m_currentGame)
		return;

	const Int localSlot = m_currentGame->getLocalSlotNum();
	if (localSlot < 0)
		return;
	::Engine::SessionMessage m;
	m.type = ::Engine::SessionType::HAS_MAP;
	m.flag = m_currentGame->getSlot(localSlot)->hasMap();
	m.mapCRC = portableMapCRC(m_currentGame->getMap());
	if (!AmIHost())
		sendToHost(m);

	if (!m.flag)
	{
		UnicodeString text;
		UnicodeString mapDisplayName;
		const MapMetaData *mapData = TheMapCache->findMap( m_currentGame->getMap() );
		Bool willTransfer = TRUE;
		if (mapData)
		{
			mapDisplayName.format(L"%ls", mapData->m_displayName.str());
			if (mapData->m_isOfficial)
				willTransfer = FALSE;
		}
		else
		{
			mapDisplayName.format(L"%hs", TheGameState->getMapLeafName(m_currentGame->getMap()).str());
			willTransfer = WouldMapTransfer(m_currentGame->getMap());
		}
		if (willTransfer)
			text.format(TheGameText->fetch("GUI:LocalPlayerNoMapWillTransfer"), mapDisplayName.str());
		else
			text.format(TheGameText->fetch("GUI:LocalPlayerNoMap"), mapDisplayName.str());
		systemChat(text);
	}
}

void GameLobby::showModeHelp( void )
{
	UnicodeString host;
	if (hasReturnOffer(host))
	{
		UnicodeString text;
		text.formatUnchecked(TheGameText->fetch("GZH:ReturnOffer").str(), host.str());
		systemChat(text);
	}
	if (m_mode != Mode::Internet)
		return;
	systemChat(TheGameText->fetch("GZH:InternetWelcome"));
	if (m_servers.empty())
		systemChat(TheGameText->fetch("GZH:InternetNoServers"));
	systemChat(TheGameText->fetch("GZH:InternetCommands"));
}

// Commands typed into the chat line: the rendezvous servers and whether our game is listed publicly.
void GameLobby::chatCommand( const UnicodeString &line )
{
	std::vector<std::string> words;
	{
		std::string text = toUtf8(line);
		size_t at = 0;
		while (at < text.size())
		{
			const size_t start = text.find_first_not_of(' ', at);
			if (start == std::string::npos)
				break;
			const size_t end = text.find(' ', start);
			words.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
			at = end == std::string::npos ? text.size() : end;
		}
	}
	const auto say = [this]( const char *label, const std::string &argument ) {
		UnicodeString text;
		text.formatUnchecked(TheGameText->fetch(label).str(), argument.c_str());
		systemChat(text);
	};
	const std::string verb = words.empty() ? std::string() : words[0];

	if (verb == "/return")
	{
		if (!RequestGameReturn())
			systemChat(TheGameText->fetch("GZH:ReturnNone"));
		return;
	}

	if (verb == "/servers" || (verb == "/server" && words.size() == 1))
	{
		if (m_servers.empty())
			systemChat(TheGameText->fetch("GZH:InternetNoServers"));
		for (const std::string &server : m_servers.list())
			say("GZH:InternetServerEntry", server);
		return;
	}
	if (verb == "/server" && words.size() == 3 && (words[1] == "add" || words[1] == "remove"))
	{
		const Bool add = words[1] == "add";
		const std::string &server = words[2];
		switch (add ? m_servers.addOwn(server) : m_servers.removeOwn(server))
		{
			case RendezvousServers::Change::Done:
				say(add ? "GZH:InternetServerAdded" : "GZH:InternetServerRemoved", server);
				m_publicGames.reset();	// the next round asks the new list
				break;
			case RendezvousServers::Change::Invalid:
				say("GZH:InternetServerInvalid", server);
				break;
			case RendezvousServers::Change::NotOwn:
				say("GZH:InternetServerNotOwn", server);
				break;
			default:
				say("GZH:InternetServerUnchanged", server);
				break;
		}
		return;
	}
	if (verb == "/public" || verb == "/private")
	{
		if (!m_currentGame || m_inLobby || !AmIHost())
		{
			systemChat(TheGameText->fetch("GZH:InternetOnlyHost"));
			return;
		}
		setListedPublicly(verb == "/public");
		systemChat(TheGameText->fetch(m_listPublicly ? "GZH:InternetListed" : "GZH:InternetPrivate"));
		return;
	}
	systemChat(TheGameText->fetch("GZH:InternetCommands"));
}

void GameLobby::RequestChat( UnicodeString message, ChatType format )
{
	if (message.getLength() > 0 && message.getCharAt(0) == L'/')
	{
		chatCommand(message);
		return;
	}
	while (message.getLength() > g_lanMaxChatLength)
		message.removeLastChar();
	::Engine::SessionMessage m;
	m.chatKind = static_cast<std::uint8_t>(format);
	m.text = toUtf8(message);
	if ((m_inLobby || !m_currentGame) && m_mode == Mode::Internet)
	{
		// nobody to talk to outside a game
		systemChat(TheGameText->fetch("GZH:InternetNoLobbyChat"));
		return;
	}
	if (m_inLobby || !m_currentGame)
	{
		m.type = ::Engine::SessionType::LOBBY_CHAT;
		m.name = toUtf8(m_name);
		broadcast(m);
	}
	else
	{
		m.type = ::Engine::SessionType::CHAT;
		const Int localSlot = m_currentGame->getLocalSlotNum();
		m.slot = static_cast<std::uint8_t>(localSlot < 0 ? 0 : localSlot);
		if (AmIHost())
			sendToMembers(m);
		else
			sendToHost(m);
	}
	OnChat(m_name, m_localIP, message, format);
}

void GameLobby::RequestGameOptions( AsciiString gameOptions, Bool isPublic, UnsignedInt ip /* = 0 */ )
{
	if (!m_currentGame || m_inLobby)
		return;

	if (AmIHost())
	{
		if (gameOptions != m_host.lastGameOptions)
		{
			++m_host.stateRevision;
			m_host.lastGameOptions = gameOptions;
		}
		sendState(ip);
	}
	else
	{
		::Engine::SessionMessage m;
		m.type = ::Engine::SessionType::WISH;
		m.text = gameOptions.str();
		if (m.text.size() <= ::Engine::SESSION_MAX_WISH)
			sendToHost(m);
	}

	for (Int player = 0; player < MAX_SLOTS; ++player)
	{
		if (m_currentGame->getIP(player) == m_localIP)
		{
			OnGameOptions(m_localIP, player, gameOptions);
			break;
		}
	}
}

void GameLobby::RequestSetName( UnicodeString newName )
{
	newName.trim();
	if (m_pendingAction != ACT_NONE || m_join.active)
	{
		// Can't change name while joining games
		OnNameChange(m_localIP, newName);
		return;
	}

	m_lastResendTime = timeGetTime();

	if (m_inLobby)
	{
		m_name = newName;
		sendPresence();
		addSelfToLobby();
		OnNameChange(m_localIP, m_name);
	}
}

void GameLobby::RequestLobbyLeave( Bool forced )
{
	::Engine::SessionMessage bye;
	bye.type = ::Engine::SessionType::BYE;
	broadcast(bye);
}

void GameLobby::ResetGameStartTimer( void )
{
	m_gameStartTime = 0;
	m_gameStartSeconds = 0;
}

// Alt-tabbed out: the host takes back our readiness.
void GameLobby::setIsActive( Bool isActive )
{
	if (isActive != m_isActive && isActive == FALSE && !m_inLobby && m_currentGame != nullptr && !AmIHost())
	{
		::Engine::SessionMessage m;
		m.type = ::Engine::SessionType::ACCEPT;
		m.flag = false;
		sendToHost(m);
	}
	m_isActive = isActive;
}

// ------------------------------------------------------------------------------------------------
// who we are

void GameLobby::newKeyPair( void )
{
	if (!m_keyPair.generate())
		DEBUG_CRASH(("GameLobby::newKeyPair - no random numbers from the system; network games cannot start"));
}

Bool GameLobby::AmIHost( void )
{
	return m_currentGame && m_currentGame->getIP(0) == m_localIP;
}

Int GameLobby::slotOf( UnsignedInt identity ) const
{
	if (!m_currentGame || identity == 0)
		return -1;
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		const LANGameSlot *s = m_currentGame->getConstLANSlot(slot);
		if (s->isHuman() && s->getIP() == identity)
			return slot;
	}
	return -1;
}

void GameLobby::ownEndpoints( ::Engine::SessionEndpoints &out ) const
{
	out = ::Engine::SessionEndpoints();
	const UnsignedInt lan = lanAddress();
	if (lan != 0)
	{
		addEndpoint(out.lobby, ::Engine::WireEndpoint::ipv4(lan, m_lobbyPort));
		addEndpoint(out.game, ::Engine::WireEndpoint::ipv4(lan, m_gamePort));
	}
	addEndpoint(out.lobby, m_publicLobby);
	addEndpoint(out.game, m_publicGame);
	const UnsignedInt external = ThePortMapper().externalIP();
	if (ThePortMapper().state() == PortMapper::STATE_MAPPED && external != 0)
	{
		addEndpoint(out.lobby, ::Engine::WireEndpoint::ipv4(external, m_lobbyPort));
		addEndpoint(out.game, ::Engine::WireEndpoint::ipv4(external, m_gamePort));
	}
}

// Broadcasts of our own socket come back to it.
Bool GameLobby::isOwnEndpoint( const ::Engine::NetAddress &from ) const
{
	if (!from.isV4() || from.port != m_lobbyPort)
		return FALSE;
	if (from.ipv4() == m_bindIP)
		return TRUE;
	if (m_bindIP != 0)
		return FALSE;
	return from.isLoopback() || std::any_of(m_lanNetworks.begin(), m_lanNetworks.end(),
		[&from]( const ::Engine::LocalNetwork &n ) { return n.address == from.ipv4(); });
}

Bool GameLobby::isRendezvousServer( const ::Engine::NetAddress &from ) const
{
	if (!(from.isV4() || from.isV6()))
		return FALSE;
	return from == m_room.server() || from == m_join.server || from == m_links.relayServer() || m_servers.isKnown(from);
}

// The game key once the game menu is up: players compare it (or pass it on with the address).
void GameLobby::showGameKey( void )
{
	if (!m_showGameCode || !m_currentGame || m_inLobby || m_currentGame->getPublicKey(0) == nullptr)
		return;
	m_showGameCode = FALSE;
	char code[10];
	::Engine::sealFingerprint(m_currentGame->getPublicKey(0), code);
	UnicodeString text;
	text.formatUnchecked(TheGameText->fetch(AmIHost() ? "GZH:LanGameKeyHost" : "GZH:LanGameKeyJoined").str(), code, code);
	systemChat(text);
	if (AmIHost())
		showInternetAddress(TRUE);
	// mods without game data of their own (same data checksum) may differ; say what the host plays with
	const AsciiString &mine = TheArchiveFileSystem ? TheArchiveFileSystem->getModTag() : AsciiString::TheEmptyString;
	if (!AmIHost() && !::Engine::sameModTags(m_currentGame->getModTag().str(), mine.str()))
	{
		text.formatUnchecked(TheGameText->fetch("GZH:LanHostMods").str(), OptionsMenuMods::listText(m_currentGame->getModTag()).str());
		systemChat(text);
	}
}

Bool GameLobby::SetLocalIP( UnsignedInt localIP )
{
	m_bindIP = localIP;
	if (localIP == 0)
	{
		m_lanNetworksTime = 0;
		refreshLanNetworks();
	}
	// on every adapter, the first LAN address names us in the lobby
	m_localIP = lanAddress();
	const Bool opened = openLobbyTransport();
	startPortMapping();
	return opened;
}

void GameLobby::SetLocalIP( AsciiString localIP )
{
	SetLocalIP(ResolveIP(localIP));
}

// Behind a router, ask it to forward the lobby and game ports so that players on the internet reach
// us; on the loopback (-netzLokal) and with a public address there is nothing to forward.
void GameLobby::startPortMapping( void )
{
	if (TheGlobalData == nullptr || TheGlobalData->m_netNoPortMapping || TheGlobalData->m_netLocalInstance > 0
		|| !::Engine::PortMapping::isPrivateIPv4(lanAddress()))
	{
		return;
	}
	PortMapper::Settings settings;
	settings.localIP = lanAddress();
	if (hasIPv6())
	{
		// the router's IPv6 firewall lets nobody in by default: ask it for pinholes to the address we give out
		const ::Engine::NetAddress v6 = PublishedIPv6Address();
		if (v6.isV6())
			settings.localIPv6 = v6.toString(false);
	}
	settings.ports[0] = m_lobbyPort;
	settings.ports[1] = m_gamePort;
	settings.portCount = 2;
	ThePortMapper().start(settings);
}

void GameLobby::reportPortMapping( void )
{
	const unsigned changes = ThePortMapper().changeCount();
	if (changes == m_portMapperChanges)
		return;
	m_portMapperChanges = changes;
	reportPinholes();
	writeRouterLog();
	const PortMapper::State state = ThePortMapper().state();
	if (state == m_portMapperReported)
		return;
	m_portMapperReported = state;
	if (state != PortMapper::STATE_MAPPED && state != PortMapper::STATE_FAILED)
		return;
	// whether the router or this computer kept the search answers back
	const PortMapper::SearchOutcome search = ThePortMapper().searchOutcome();
	if (search == PortMapper::SEARCH_FILTERED)
		systemChat(TheGameText->fetch("GZH:LanSearchFiltered"));
	else if (search == PortMapper::SEARCH_SILENT && state == PortMapper::STATE_FAILED)
		systemChat(TheGameText->fetch("GZH:LanUpnpOff"));
	UnicodeString text;
	const UnsignedInt external = ThePortMapper().externalIP();
	if (external != 0 && !::Engine::PortMapping::isPublicIPv4(external))
	{
		// a forwarded port behind DS-Lite or carrier-grade NAT leads nowhere
		text.formatUnchecked(TheGameText->fetch("GZH:LanNoPublicIPv4").str(), ::Engine::PortMapping::ipv4Text(external).c_str());
		systemChat(text);
		return;
	}
	if (state == PortMapper::STATE_MAPPED)
	{
		text.formatUnchecked(TheGameText->fetch("GZH:LanPortsOpened").str(), m_lobbyPort, m_gamePort,
			ThePortMapper().method() == PortMapper::METHOD_NATPMP ? "NAT-PMP" : "UPnP");
		systemChat(text);
		showInternetAddress(FALSE);
	}
	else if (ThePortMapper().failure() == PortMapper::FAILURE_REFUSED)
	{
		text.formatUnchecked(TheGameText->fetch("GZH:LanPortsRefused").str(), m_lobbyPort, m_gamePort, ThePortMapper().refusalCode());
		systemChat(text);
	}
	else
	{
		text.formatUnchecked(TheGameText->fetch("GZH:LanPortsNoRouter").str(), m_lobbyPort, m_gamePort);
		systemChat(text);
	}
}

// The steps of the router search and what the routers answered, for the player to pass on: only the
// router knows why it refuses. Written once both results are in; named in the chat if one failed.
void GameLobby::writeRouterLog( void )
{
	const PortMapper::State state = ThePortMapper().state();
	const PortMapper::PinholeState pinholes = ThePortMapper().pinholeState();
	if (state == PortMapper::STATE_WORKING || state == PortMapper::STATE_OFF || pinholes == PortMapper::PINHOLE_WORKING
		|| TheGlobalData == nullptr)
	{
		return;
	}
	AsciiString path;
	path.format("%sRouterLog.txt", TheGlobalData->getPath_UserData().str());
	FILE *fp = ::Engine::openNarrowFile(path.str(), "w");
	if (fp == nullptr)
		return;
	for (const std::string &line : ThePortMapper().log())
		fprintf(fp, "%s\n", line.c_str());
	fclose(fp);
	const Bool failed = state == PortMapper::STATE_FAILED
		|| (pinholes != PortMapper::PINHOLE_OFF && pinholes != PortMapper::PINHOLE_OPEN && pinholes != PortMapper::PINHOLE_NOT_NEEDED);
	if (failed && !m_routerLogNamed)
	{
		m_routerLogNamed = TRUE;
		UnicodeString text;
		text.formatUnchecked(TheGameText->fetch("GZH:LanRouterLog").str(), path.str());
		systemChat(text);
	}
}

void GameLobby::reportPinholes( void )
{
	const PortMapper::PinholeState state = ThePortMapper().pinholeState();
	if (state == m_pinholesReported)
		return;
	m_pinholesReported = state;
	UnicodeString text;
	switch (state)
	{
		case PortMapper::PINHOLE_OPEN:
			text.formatUnchecked(TheGameText->fetch("GZH:LanPinholesOpen").str(), m_lobbyPort, m_gamePort);
			break;
		case PortMapper::PINHOLE_NOT_NEEDED:
			text = TheGameText->fetch("GZH:LanPinholesNotNeeded");
			break;
		case PortMapper::PINHOLE_NO_SERVICE:
			text.formatUnchecked(TheGameText->fetch("GZH:LanPinholesNoService").str(), m_lobbyPort, m_gamePort);
			break;
		case PortMapper::PINHOLE_REFUSED:
			text.formatUnchecked(TheGameText->fetch("GZH:LanPinholesRefused").str(), m_lobbyPort, m_gamePort,
				ThePortMapper().pinholeRefusalCode());
			break;
		default:
			return;
	}
	systemChat(text);
}

void GameLobby::showInternetAddress( Bool withIPv6 )
{
	const Bool hosting = m_currentGame && !m_inLobby && AmIHost() && m_currentGame->getPublicKey(0);
	char code[10] = {};
	if (hosting)
		::Engine::sealFingerprint(m_currentGame->getPublicKey(0), code);
	UnicodeString joinWith;
	const UnsignedInt external = ThePortMapper().externalIP();
	if (ThePortMapper().state() == PortMapper::STATE_MAPPED && ::Engine::PortMapping::isPublicIPv4(external))
	{
		const std::string address = ::Engine::PortMapping::ipv4Text(external);
		UnicodeString text;
		if (hosting)
		{
			text.formatUnchecked(TheGameText->fetch("GZH:LanInternetJoin").str(), address.c_str(), code);
			joinWith.format(L"%hs#%hs", address.c_str(), code);
		}
		else
		{
			text.formatUnchecked(TheGameText->fetch("GZH:LanInternetAddress").str(), address.c_str());
		}
		systemChat(text);
	}
	// an IPv6 address is the same inside and outside: no router has to translate it, only let it in
	if (withIPv6 && hosting && hasIPv6())
	{
		const ::Engine::NetAddress v6 = PublishedIPv6Address();
		if (v6.isV6())
		{
			const std::string address = "[" + v6.toString(false) + "]";
			UnicodeString text;
			text.formatUnchecked(TheGameText->fetch("GZH:LanInternetJoinV6").str(), address.c_str(), code, m_lobbyPort, m_gamePort);
			systemChat(text);
			if (joinWith.isEmpty())
				joinWith.format(L"%hs#%hs", address.c_str(), code);
		}
	}
	// unattended tests leave the user's clipboard alone
	if (!joinWith.isEmpty() && TheGlobalData->m_testScriptFile.isEmpty() && ClipboardSetText(joinWith))
	{
		UnicodeString text;
		text.formatUnchecked(TheGameText->fetch("GZH:LanJoinCopied").str(), joinWith.str());
		systemChat(text);
	}
}
