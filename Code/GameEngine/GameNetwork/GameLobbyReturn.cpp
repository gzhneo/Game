/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GameLobbyReturn.cpp: a player dropped from a running game for not answering (crash, network gone)
// comes back into it while its units are still there.
//
// Every member gets a secret token in the host's WELCOME and keeps it in Rejoin.ini at the start.
// To come back it joins the host with a new key and the token (SessionWants::RETURN). The host
// streams it the running game's replay without delay, as to a spectator; the player starts the game
// from the replay's header and plays the frames as fast as they come (Network::beginReturn). Once it
// has caught up (RETURN_READY) the host announces it to the lockstep (Network::announcePlayerReturn)
// and, when every machine took it in, names the return frame (RETURN_AT). At the start of that frame
// the host notes the network state and sends it (RETURN_STATE); the player takes it over and plays
// in the lockstep again from there (RETURN_DONE).

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "Common/UserSettings.h"
#include "Engine/SessionHandshake.h"
#include "GameClient/GameText.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameLobby.h"
#include "GameNetwork/Lobby/LobbyTiming.h"
#include "GameNetwork/NetworkInterface.h"

#include <cstdio>
#include <cstring>
#include <ctime>

using namespace Lobby;

namespace
{
	/// The stream a returning player catches up with, in the replay folder.
	const char RETURN_FILE_NAME[] = "ReturnGame";
	const char TICKET_FILE_NAME[] = "Rejoin.ini";
	/// The player asks to be taken in once it is this close to the stream.
	constexpr UnsignedInt READY_GAP = 15;

	AsciiString returnFileName( void )
	{
		AsciiString name = RETURN_FILE_NAME;
		name.concat(TheRecorder->getReplayExtention());
		return name;
	}

	std::string hexOf( const UnsignedByte *bytes, size_t size )
	{
		static const char digits[] = "0123456789abcdef";
		std::string text;
		for (size_t i = 0; i < size; ++i)
		{
			text += digits[bytes[i] >> 4];
			text += digits[bytes[i] & 15];
		}
		return text;
	}

	Bool bytesOf( const std::string &text, UnsignedByte *bytes, size_t size )
	{
		if (text.size() != 2 * size)
			return FALSE;
		for (size_t i = 0; i < size; ++i)
		{
			unsigned value = 0;
			if (sscanf(text.c_str() + 2 * i, "%2x", &value) != 1)
				return FALSE;
			bytes[i] = static_cast<UnsignedByte>(value);
		}
		return TRUE;
	}

	/// Of a player's game endpoints the one at the address its lobby socket was seen at (same
	/// machine, same router), else the first.
	::Engine::WireEndpoint likelyGameEndpoint( const ::Engine::SessionEndpoints &e, const ::Engine::WireEndpoint &seen )
	{
		for (const ::Engine::WireEndpoint &g : e.game)
		{
			if (g.isSet() && seen.isSet() && g.family == seen.family && memcmp(g.address, seen.address, sizeof(g.address)) == 0)
				return g;
		}
		for (const ::Engine::WireEndpoint &g : e.game)
		{
			if (g.isSet())
				return g;
		}
		return ::Engine::WireEndpoint();
	}

	/// A key=value file of a few lines.
	typedef std::vector< std::pair<std::string, std::string> > Ticket;

	Bool readTicket( const AsciiString &path, Ticket &out )
	{
		FILE *file = fopen(path.str(), "r");
		if (file == nullptr)
			return FALSE;
		char line[512];
		while (fgets(line, sizeof(line), file) != nullptr)
		{
			std::string text(line);
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
				text.pop_back();
			const size_t eq = text.find('=');
			if (eq != std::string::npos)
				out.push_back({ text.substr(0, eq), text.substr(eq + 1) });
		}
		fclose(file);
		return TRUE;
	}

	std::string valueOf( const Ticket &t, const char *key )
	{
		for (const auto &kv : t)
		{
			if (kv.first == key)
				return kv.second;
		}
		return std::string();
	}
}

// ------------------------------------------------------------------------------------------------
// what a member keeps

void GameLobby::newReturnToken( Int slot )
{
	if (slot < 0 || slot >= MAX_SLOTS)
		return;
	UnsignedByte *token = m_host.returnTokens[slot];
	if (!::Engine::secureRandom(token, ::Engine::SESSION_TOKEN_SIZE))
	{
		for (size_t i = 0; i < ::Engine::SESSION_TOKEN_SIZE; i += 4)
		{
			const UnsignedInt tag = randomTag();
			memcpy(token + i, &tag, 4);
		}
	}
}

AsciiString GameLobby::returnTicketPath( void ) const
{
	if (m_returnTicketPath.isNotEmpty())
		return m_returnTicketPath;
	AsciiString path = TheGlobalData->getPath_UserData();
	path.concat(TICKET_FILE_NAME);
	return path;
}

// At the start of a game we joined: what we need to come back after a drop.
void GameLobby::keepReturnTicket( void )
{
	if (!m_currentGame || AmIHost() || !m_hasReturnToken)
		return;
	const Int slot = m_currentGame->getLocalSlotNum();
	const Member &host = m_links.member(0);
	const ::Engine::NetAddress hostEndpoint = m_currentGame->getHostEndpoint();
	if (slot <= 0 || host.identity == 0 || !(hostEndpoint.isV4() || hostEndpoint.isV6()))
		return;
	FILE *file = fopen(returnTicketPath().str(), "w");
	if (file == nullptr)
	{
		DEBUG_LOG(("GameLobby::keepReturnTicket - cannot write %s\n", returnTicketPath().str()));
		return;
	}
	const UnsignedInt64 session = m_sessionId;
	fprintf(file, "Session=%s\n", hexOf(reinterpret_cast<const UnsignedByte *>(&session), sizeof(session)).c_str());
	fprintf(file, "Slot=%d\n", slot);
	fprintf(file, "Token=%s\n", hexOf(m_returnToken, sizeof(m_returnToken)).c_str());
	fprintf(file, "Host=%s\n", hostEndpoint.toString(true).c_str());
	fprintf(file, "Key=%s\n", fingerprintCode(host.key).c_str());
	fprintf(file, "Invite=%s\n", m_joinedInvite.str());
	fprintf(file, "Leader=%s\n", toUtf8(m_currentGame->getConstSlot(0)->getName()).c_str());
	fprintf(file, "Time=%lld\n", static_cast<long long>(time(nullptr)));
	fclose(file);
}

void GameLobby::clearReturnTicket( void )
{
	remove(returnTicketPath().str());
}

Bool GameLobby::hasReturnOffer( UnicodeString &hostName )
{
	Ticket t;
	if (!readTicket(returnTicketPath(), t))
		return FALSE;
	const long long when = atoll(valueOf(t, "Time").c_str());
	const long long now = static_cast<long long>(time(nullptr));
	if (when <= 0 || now < when || now - when > static_cast<long long>(Timing::RETURN_TICKET_AGE))
		return FALSE;
	hostName = fromUtf8(valueOf(t, "Leader"));
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// the returning player

Bool GameLobby::RequestGameReturn( void )
{
	Ticket t;
	if (!readTicket(returnTicketPath(), t))
		return FALSE;
	UnsignedInt64 session = 0;
	UnsignedByte token[::Engine::SESSION_TOKEN_SIZE];
	const Int slot = atoi(valueOf(t, "Slot").c_str());
	::Engine::NetAddress host;
	const std::string invite = valueOf(t, "Invite");
	if (!bytesOf(valueOf(t, "Session"), reinterpret_cast<UnsignedByte *>(&session), sizeof(session)) || session == 0
		|| !bytesOf(valueOf(t, "Token"), token, sizeof(token)) || slot <= 0 || slot >= MAX_SLOTS)
	{
		return FALSE;
	}
	if (!::Engine::parseNetAddress(valueOf(t, "Host"), host) && invite.empty())
		return FALSE;
	if (m_pendingAction != ACT_NONE || m_join.active || m_return.active || !m_inLobby)
	{
		OnGameJoin(RET_BUSY, nullptr);
		return TRUE;
	}
	const AsciiString key = valueOf(t, "Key").c_str();
	if (!invite.empty())
	{
		m_expectedHostCode.clear();
		if (!RequestGameJoinInvite(AsciiString(invite.c_str())))
			return FALSE;
	}
	else
	{
		m_expectedHostCode = key;
		RequestGameJoinAt(host);
	}
	if (!m_join.active)
		return TRUE;
	m_join.wants = ::Engine::SessionWants::RETURN;
	m_join.sessionId = session;
	m_join.returnSlot = slot;
	memcpy(m_join.returnToken, token, sizeof(token));
	m_join.expectedCode = key;
	DEBUG_LOG(("GameLobby::RequestGameReturn - back into game %llx as slot %d\n", static_cast<unsigned long long>(session), slot));
	return TRUE;
}

// The host welcomed us back into its running game.
void GameLobby::startReturn( const ::Engine::SessionMessage &m )
{
	const UnsignedInt own = ::Engine::SessionHandshake::identityOf(m_keyPair.publicKey);
	if (m.sessionId != m_join.sessionId || m.identity != own || m.slot != m_join.returnSlot || m.slot >= MAX_SLOTS)
		return;
	AsciiString path = TheRecorder->getReplayDir();
	TheFileSystem->createDirectory(path);
	path.concat(returnFileName());
	FILE *file = fopen(path.str(), "wb");
	FILE *read = file != nullptr ? fopen(path.str(), "rb") : nullptr;
	if (read == nullptr)
	{
		DEBUG_LOG(("GameLobby::startReturn - cannot write %s\n", path.str()));
		if (file != nullptr)
			fclose(file);
		finishJoin(RET_UNKNOWN);
		return;
	}
	m_return = ReturnState();
	m_return.active = TRUE;
	m_return.hostIdentity = ::Engine::SessionHandshake::identityOf(m_join.hostKey);
	m_return.slot = m.slot;
	m_return.file = file;
	m_return.read = read;
	m_return.hostHeard = timeGetTime();
	m_sessionId = m.sessionId;
	m_localIP = own;
	linkGame();
	m_links.setMember(0, m_return.hostIdentity, m_join.hostKey, timeGetTime());
	m_links.setMember(m.slot, own, m_keyPair.publicKey, timeGetTime());
	m_return.hostLobby = wireEndpoint(m_join.host);
	m_pendingAction = ACT_NONE;
	m_expectedHostCode.clear();
	std::vector<Transport::RawDatagram> early;
	early.swap(m_join.early);
	m_join = JoinAttempt();
	DEBUG_LOG(("GameLobby::startReturn - back into the game of %X as slot %d\n", m_return.hostIdentity, m_return.slot));
	if (m_isInLANMenu)
		systemChat(TheGameText->fetch("GZH:ReturnJoined"));
	for (const Transport::RawDatagram &e : early)
		handleDatagram(e, FALSE);
}

void GameLobby::returnReceives( const ::Engine::SessionMessage &m )
{
	m_return.hostHeard = timeGetTime();
	switch (m.type)
	{
		case ::Engine::SessionType::WATCH:
		{
			if (!m.chunk.empty())
			{
				std::vector<std::uint8_t> fresh;
				if (m_return.receiver.accept(m.offset, m.chunk.data(), m.chunk.size(), fresh) > 0 && m_return.file != nullptr)
				{
					fwrite(fresh.data(), 1, fresh.size(), m_return.file);
					fflush(m_return.file);
				}
			}
			::Engine::WatchMark mark;
			mark.bytes = m.markBytes;
			mark.frames = m.markFrames;
			m_return.receiver.noteMark(mark);
			m_return.ackDue = TRUE;
			break;
		}
		case ::Engine::SessionType::MEMBER:
			if (!m_return.started)
				handleMember(m);
			break;
		case ::Engine::SessionType::RETURN_AT:
			if (m_return.returnFrame == 0 && m.value != 0)
			{
				m_return.returnFrame = m.value;
				if (TheNetwork != nullptr && TheNetwork->isReturning())
					TheNetwork->setReturnFrame(m.value, m.offset, m.slot);
			}
			break;
		case ::Engine::SessionType::RETURN_STATE:
		{
			if (m_return.returnFrame == 0 || m.returnState.frame != m_return.returnFrame)
				break;
			if (!m_return.stateApplied && TheNetwork != nullptr)
			{
				TheNetwork->applyReturnState(m.returnState);
				m_return.stateApplied = TRUE;
			}
			::Engine::SessionMessage done;
			done.type = ::Engine::SessionType::RETURN_DONE;
			done.value = m_return.returnFrame;
			sendToPeer(done, m_return.hostIdentity);
			break;
		}
		case ::Engine::SessionType::LEAVE:
			// refused, or the host's game is over
			m_return.hostIdentity = 0;
			stopReturn(FALSE, "GZH:ReturnRefused");
			break;
		default:
			break;
	}
}

// The returning player's frames before the return frame come from the stream file: every command
// of a frame below the host's mark is in it.
UnsignedInt GameLobby::StreamFeed::completeFrames()
{
	const ReturnState &r = m_lobby.m_return;
	return r.active && r.headerRead && !r.broken ? r.receiver.playableFrames() : 0;
}

void GameLobby::StreamFeed::appendFrame( UnsignedInt frame, CommandList *list )
{
	ReturnState &r = m_lobby.m_return;
	if (!r.active || !r.headerRead || r.broken || r.read == nullptr)
		return;
	// a fresh position drops what the C library buffered: the file grows
	if (fseek(r.read, r.position, SEEK_SET) != 0)
		return;
	for (;;)
	{
		if (!r.havePending)
		{
			UnsignedInt next = 0;
			if (fread(&next, sizeof(next), 1, r.read) != 1)
				break;
			r.havePending = TRUE;
			r.pendingFrame = next;
			r.position = ftell(r.read);
		}
		if (r.pendingFrame > frame)
			break;
		Bool ok = FALSE;
		GameMessage *msg = RecorderClass::readCommandFrom(r.read, ok);
		if (!ok)
		{
			// the mark promised the whole frame
			DEBUG_LOG(("GameLobby::StreamFeed - command of frame %d incomplete in the stream\n", r.pendingFrame));
			if (msg != nullptr)
				deleteInstanceOf(msg);
			r.broken = TRUE;
			break;
		}
		r.havePending = FALSE;
		r.position = ftell(r.read);
		if (msg != nullptr)
			list->appendMessage(msg);
	}
	clearerr(r.read);
}

// The game we come back to, as it started (replay header), with the players and keys of now.
Bool GameLobby::startReturningGame( void )
{
	const UnsignedInt own = m_localIP;
	auto created = std::make_unique<LANGameInfo>();
	LANGameInfo *game = created.get();
	game->setName(sessionGameName(m_sessionId));
	game->setLocalIP(own);
	game->enterGame();
	if (!ParseAsciiStringToGameInfo(game, m_return.gameOptions))
		return FALSE;
	game->setLocalIP(own);
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		LANGameSlot *s = game->getLANSlot(slot);
		if (!s->isHuman())
			continue;
		if (slot == m_return.slot)
		{
			s->setIP(own);
			s->setPort(m_gamePort);
			game->setPublicKey(slot, m_keyPair.publicKey);
			continue;
		}
		const Member &r = m_links.member(slot);
		if (r.identity == 0)
			continue;	// gone: the host's state at the return frame says so
		s->setIP(r.identity);
		game->setPublicKey(slot, r.key);
		// where its game socket is: the address its lobby socket was seen at first; the host's at the
		// address we reach it at
		::Engine::WireEndpoint endpoint = likelyGameEndpoint(r.endpoints, slot == 0 ? m_return.hostLobby : r.endpoints.lobby[0]);
		if (slot == 0 && (!endpoint.isSet() || endpoint.family != m_return.hostLobby.family
			|| memcmp(endpoint.address, m_return.hostLobby.address, sizeof(endpoint.address)) != 0))
		{
			const UnsignedShort port = endpoint.isSet() ? endpoint.port : s->getPort();
			endpoint = m_return.hostLobby;
			endpoint.port = port;
		}
		if (endpoint.isSet() && m_gameTransport)
		{
			s->setPort(endpoint.port);
			m_gameTransport->bindPeer(r.identity, netAddress(endpoint));
		}
	}
	if (game->getLocalSlotNum() != m_return.slot)
		return FALSE;
	UnsignedByte fingerprint[5];
	fingerprintBytes(m_links.member(0).key, fingerprint);
	game->setSession(m_sessionId, netAddress(m_return.hostLobby), fingerprint);

	TheMapCache->updateCache();
	if (TheMapCache->findMap(game->getMap()) == nullptr)
	{
		systemChat(TheGameText->fetch("GZH:WatchNoMap"));
		return FALSE;
	}

	m_currentGame = m_directory.adoptGame(std::move(created));
	m_inLobby = FALSE;
	m_isInLANMenu = FALSE;

	if (TheNetwork != nullptr)
	{
		delete TheNetwork;
		::Engine::context().network = nullptr;
	}
	::Engine::context().network = NetworkInterface::createNetwork();
	TheNetwork->init();
	Transport *gameTransport = takeGameTransport();
	if (gameTransport != nullptr)
	{
		TheNetwork->setLocalAddress(own, m_gamePort);
		TheNetwork->attachTransport(gameTransport);
	}
	else
	{
		TheNetwork->setLocalAddress(m_bindIP, m_gamePort);
		TheNetwork->initTransport();
	}
	if (!m_currentGame->deriveSealKeys(m_keyPair))
		DEBUG_LOG(("GameLobby::startReturningGame - no keys for some players (gone, or the state tells)\n"));
	TheNetwork->parseUserList(m_currentGame);
	TheNetwork->setReturnWindow(static_cast<UnsignedInt>(atoi(UserSettings::get("Network.ReturnWindow").str())));
	TheNetwork->beginReturn(&m_returnFeed);

	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData();
	m_currentGame->startGame(0);
	TheWritableGlobalData->m_pendingFile = m_currentGame->getMap();
	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_NEW_GAME);
	msg->appendIntegerArgument(GAME_LAN);
	TheWritableGlobalData->m_useFpsLimit = false;
	InitGameLogicRandom(m_currentGame->getSeed());
	DEBUG_LOG(("GameLobby::startReturningGame - the game of %X starts again for slot %d\n", m_return.hostIdentity, m_return.slot));
	return TRUE;
}

void GameLobby::returnUpdate( UnsignedInt now )
{
	if (m_return.hostIdentity != 0)
	{
		if ((m_return.ackDue && due(now, m_return.lastAck + Timing::UPDATE)) || due(now, m_return.lastAck + Timing::WATCH_ACK))
		{
			::Engine::SessionMessage ack;
			ack.type = ::Engine::SessionType::WATCH_ACK;
			ack.value = m_return.receiver.received();
			sendToPeer(ack, m_return.hostIdentity);
			m_return.lastAck = now;
			m_return.ackDue = FALSE;
		}
		if (silentFor(now, m_return.hostHeard, Timing::SILENCE))
		{
			stopReturn(FALSE, "GZH:ReturnLost");
			return;
		}
	}

	if (!m_return.started)
	{
		if (!m_return.headerRead && m_return.read != nullptr && m_return.receiver.received() > 0)
		{
			RecorderClass::ReplayHeader header;
			if (fseek(m_return.read, 0, SEEK_SET) == 0 && RecorderClass::readHeaderFrom(m_return.read, header))
			{
				m_return.headerRead = TRUE;
				m_return.gameOptions = header.gameOptions;
				m_return.position = ftell(m_return.read);
			}
			clearerr(m_return.read);
		}
		if (!m_return.headerRead)
			return;
		if (m_returnStartGame)
		{
			if (m_return.waitSince == 0)
				m_return.waitSince = now;
			// the keys of the others come with MEMBER right after the welcome
			if (!silentFor(now, m_return.waitSince, 2 * Timing::RETURN_RESEND))
				return;
			if (!startReturningGame())
			{
				stopReturn(TRUE, "GZH:ReturnLostBeforeStart");
				return;
			}
			m_return.started = TRUE;
			return;
		}
		// tests: no game, every frame of the stream counts as played
	}
	else if (TheNetwork == nullptr || !TheGameLogic->isInGame())
	{
		stopReturn(TRUE, nullptr);	// the game ended or we quit it
		return;
	}
	if (!m_returnStartGame || TheNetwork->isReturning())
	{
		const UnsignedInt playable = m_return.receiver.playableFrames();
		const UnsignedInt played = m_returnStartGame ? TheGameLogic->getFrame() : playable;
		if (m_return.returnFrame == 0 && playable > 0 && played + READY_GAP >= playable
			&& due(now, m_return.lastReady + Timing::RETURN_RESEND))
		{
			::Engine::SessionMessage ready;
			ready.type = ::Engine::SessionType::RETURN_READY;
			ready.value = TheGameLogic->getFrame();
			sendToPeer(ready, m_return.hostIdentity);
			m_return.lastReady = now;
		}
		return;
	}
	// in the lockstep again: the stream has done its work
	if (TheInGameUI)
		TheInGameUI->message("GZH:ReturnBack");
	stopReturn(TRUE, nullptr);
}

void GameLobby::stopReturn( Bool tellHost, const char *reason )
{
	if (!m_return.active)
		return;
	if (tellHost && m_return.hostIdentity != 0)
	{
		::Engine::SessionMessage leave;
		leave.type = ::Engine::SessionType::LEAVE;
		sendToPeer(leave, m_return.hostIdentity);
		sendToPeer(leave, m_return.hostIdentity);
	}
	if (m_return.hostIdentity != 0 && m_transport)
		m_transport->removePeerKeys(m_return.hostIdentity);
	if (m_return.file != nullptr)
		fclose(m_return.file);
	if (m_return.read != nullptr)
		fclose(m_return.read);
	const Bool started = m_return.started;
	const Bool stillReturning = TheNetwork != nullptr && TheNetwork->isReturning();
	m_return = ReturnState();
	if (reason != nullptr)
	{
		if (started && TheInGameUI)
			TheInGameUI->message(reason);
		else if (m_isInLANMenu)
			systemChat(TheGameText->fetch(reason));
	}
	if (!started && m_isInLANMenu)
		m_sessionId = 0;
	// a game that cannot reach its return frame any more ends
	if (started && stillReturning && TheMessageStream)
		TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);
	else if (!started)
		clearReturnTicket();
}

// ------------------------------------------------------------------------------------------------
// the host

// The JOIN of a player who wants back into the running game passed the cookie and the versions.
void GameLobby::acceptReturner( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d, const ::Engine::SessionJoinInfo &info,
	UnsignedInt identity )
{
	const std::size_t unlimited = ::Engine::MAX_SESSION_MESSAGE;
	const Int slot = info.returnSlot;
	Bool known = slot > 0 && slot < MAX_SLOTS && TheNetwork != nullptr && m_currentGame->isGameInProgress();
	if (known)
	{
		// the token in constant time
		UnsignedByte diff = 0;
		for (size_t i = 0; i < ::Engine::SESSION_TOKEN_SIZE; ++i)
			diff |= static_cast<UnsignedByte>(m_host.returnTokens[slot][i] ^ info.returnToken[i]);
		known = diff == 0;
	}
	for (const Spectator &s : m_watchHost.spectators)
	{
		if (known && s.returnSlot == slot)
			known = FALSE;	// on the way back already
	}
	const AsciiString source = m_watchHost.testPath.isNotEmpty() ? m_watchHost.testPath : TheRecorder->getRecordingPath();
	if (!known || !TheNetwork->isReturnable(slot) || (source.isEmpty() && m_watchHost.file == nullptr)
		|| m_watchHost.spectators.size() >= Limit::SPECTATORS)
	{
		DEBUG_LOG(("GameLobby::acceptReturner - no way back into slot %d for %X\n", slot, identity));
		refuse(d.from, m_sessionId, RET_GAME_GONE, unlimited);
		return;
	}
	::Engine::SealSessionKeys keys;
	if (!::Engine::deriveSessionKeys(m_keyPair, m.key, LOBBY_CONTEXT, sizeof(LOBBY_CONTEXT), keys))
		return;
	m_transport->setPeerKeys(identity, m_lobbyPort, keys);
	keys.wipe();
	if (!m_transport->hasPeerKeys(identity))
	{
		refuse(d.from, m_sessionId, RET_GAME_FULL, unlimited);
		return;
	}
	m_transport->bindPeer(identity, d.from);
	m_transport->setTrustedSender(identity, TRUE);

	Spectator s;
	s.identity = identity;
	s.name = m_currentGame->getConstSlot(slot)->getName();
	s.heard = timeGetTime();
	s.returnSlot = slot;
	memcpy(s.key, m.key, sizeof(s.key));
	s.endpoints = info.endpoints;
	s.gameEndpoint = likelyGameEndpoint(info.endpoints, wireEndpoint(d.from));
	if (!s.gameEndpoint.isSet())
	{
		s.gameEndpoint = wireEndpoint(d.from);
		s.gameEndpoint.port = m_gamePort;
	}
	s.gamePort = s.gameEndpoint.port;
	m_watchHost.spectators.push_back(std::move(s));
	DEBUG_LOG(("GameLobby::acceptReturner - %ls comes back into slot %d as %X from %s\n", m_watchHost.spectators.back().name.str(),
		slot, identity, d.from.toString().c_str()));

	sendReturnWelcome(m_watchHost.spectators.back());
	UnicodeString text;
	text.formatUnchecked(TheGameText->fetch("GZH:ReturnComing").str(), m_watchHost.spectators.back().name.str());
	tellPlayers(text);
}

void GameLobby::sendReturnWelcome( const Spectator &s )
{
	::Engine::SessionMessage w;
	w.type = ::Engine::SessionType::WELCOME;
	w.sessionId = m_sessionId;
	w.slot = static_cast<std::uint8_t>(s.returnSlot);
	w.identity = s.identity;
	w.flag = TRUE;
	sendToPeer(w, s.identity);
	sendMembers(s.identity);
}

// A returning player: announced when it caught up, then the return frame and the state there.
void GameLobby::hostReturnUpdate( Spectator &s, UnsignedInt now )
{
	if (s.returnSlot < 0 || s.done || s.returnFrame == 0 || TheNetwork == nullptr)
		return;
	if (!s.admitted)
	{
		if (TheNetwork->isPlayerConnected(s.returnSlot))
		{
			s.admitted = TRUE;
			// the lockstep holds its new key now; members coming back later learn it from MEMBER
			Member &record = m_links.member(s.returnSlot);
			record.identity = s.identity;
			memcpy(record.key, s.key, sizeof(record.key));
			record.endpoints = s.endpoints;
		}
		else
		{
			return;
		}
	}
	if (!due(now, s.lastReturnSend + Timing::RETURN_RESEND))
		return;
	s.lastReturnSend = now;
	::Engine::SessionMessage at;
	at.type = ::Engine::SessionType::RETURN_AT;
	at.value = s.returnFrame;
	at.offset = s.prefill;
	at.slot = static_cast<std::uint8_t>(TheNetwork->getPacketRouterSlot());
	sendToPeer(at, s.identity);
	::Engine::SessionMessage state;
	state.type = ::Engine::SessionType::RETURN_STATE;
	if (TheNetwork->getReturnState(s.returnSlot, state.returnState))
		sendToPeer(state, s.identity);
}
