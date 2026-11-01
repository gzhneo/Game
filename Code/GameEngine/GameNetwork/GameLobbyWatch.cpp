/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GameLobbyWatch.cpp: spectators of a running game. The host keeps its session socket open during
// the game, takes spectators through the usual handshake and streams them its replay file as the
// recorder writes it (Engine::WatchStream). A spectator writes what arrives to a replay file of its
// own and plays it while it grows (RecorderClass::playbackLive). Players never wait for spectators;
// the lockstep does not know them.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
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

#include <cstring>

using namespace Lobby;

static_assert(::Engine::WATCH_CHUNK == ::Engine::SESSION_MAX_WATCH_CHUNK, "a replay chunk fits one session message");

namespace
{
	/// The file a spectator writes the watched game to, in the replay folder (the replay menu shows it
	/// afterwards).
	const char WATCH_FILE_NAME[] = "WatchedGame";

	AsciiString watchFileName( void )
	{
		AsciiString name = WATCH_FILE_NAME;
		name.concat(TheRecorder->getReplayExtention());
		return name;
	}
}

// The parts of the lobby that go on outside the lobby menus: the host of a running game (or of one
// that just ended) and a spectator.
void GameLobby::serviceInGame( void )
{
	if (!m_transport)
		return;
	const Bool hosting = m_currentGame != nullptr && !m_inLobby && !m_isInLANMenu && AmIHost();
	if (!hosting && !m_watch.active && !m_return.active)
		return;
	const UnsignedInt now = timeGetTime();
	if (!due(now, m_lastUpdate + Timing::UPDATE))
		return;
	m_lastUpdate = now;

	m_transport->update();
	receiveInGame();
	if (m_watch.active)
		watchUpdate(now);
	if (m_return.active)
		returnUpdate(now);
	if (!hosting || m_currentGame == nullptr)
		return;

	// the cookie of a CHALLENGE stays valid for one to two rotations
	if (due(now, m_host.cookieKeyTime + Timing::COOKIE_ROTATION))
	{
		memcpy(m_host.oldCookieKey, m_host.cookieKey, sizeof(m_host.cookieKey));
		if (!::Engine::secureRandom(m_host.cookieKey, sizeof(m_host.cookieKey)))
			DEBUG_CRASH(("GameLobby::serviceInGame - no random numbers from the system"));
		m_host.cookieKeyTime = now;
	}
	if (!m_currentGame->getIsDirectConnect() && due(now, m_host.lastAnnounce + Timing::ANNOUNCE))
		sendAnnounce();
	refreshRoom(now);
	hostWatchUpdate(now);
}

// In a game only what spectators need: joins at the host, the stream at both ends, the rendezvous
// server of the host's room and LAN searches. Everything else waits for the lobby menus.
void GameLobby::receiveInGame( void )
{
	Transport::RawDatagram d;
	Int budget = 256;
	while (budget-- > 0 && m_transport->takeRaw(d))
	{
		if (d.length <= 0 || d.length > Transport::MAX_RAW_DATAGRAM)
			continue;
		const std::uint8_t *data = d.data;
		const std::size_t size = static_cast<std::size_t>(d.length);
		if (::Engine::looksLikeTreff(data, size))
		{
			::Engine::TreffMessage t;
			if (!d.authenticated && !d.relayed && isRendezvousServer(d.from) && ::Engine::decodeTreff(data, size, t))
				handleTreff(t, d, FALSE);
			continue;
		}
		::Engine::SessionMessage m;
		if (!::Engine::looksLikeSession(data, size) || !::Engine::decodeSession(data, size, m))
			continue;
		if (::Engine::isSealedSessionType(m.type))
		{
			if (!d.authenticated)
				continue;
			if (m_return.active && d.sender == m_return.hostIdentity)
				returnReceives(m);
			else if (m_watch.active && d.sender == m_watch.hostIdentity)
				watchReceives(m);
			else if (const Int index = findSpectator(d.sender); index >= 0)
				spectatorSends(m, index);
			continue;
		}
		switch (m.type)
		{
			case ::Engine::SessionType::QUERY:
				if (m_mode == Mode::Lan && !isOwnEndpoint(d.from))
					handleQuery(d);
				break;
			case ::Engine::SessionType::HELLO:
				handleHello(m, d);
				break;
			case ::Engine::SessionType::JOIN:
				handleJoin(m, d);
				break;
			default:
				break;
		}
	}
}

// The host's room at the rendezvous server, refreshed in the lobby and in the game (spectators come
// by invitation as well).
HostRoom::Refresh GameLobby::refreshRoom( UnsignedInt now )
{
	HostRoom::Report report;
	report.hostName = toUtf8(m_name);
	ownEndpoints(report.endpoints);
	report.isPublic = m_listPublicly;
	report.mapName = m_currentGame->getMap().str();
	report.fingerprint = fingerprintCode(m_keyPair.publicKey);
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		const LANGameSlot *s = m_currentGame->getConstLANSlot(slot);
		if (s->isOccupied())
			++report.players;
		if (s->getState() != SLOT_CLOSED)
			++report.maxPlayers;
	}
	const HostRoom::Refresh result = m_room.refresh(now, m_servers, hasIPv6(), report, m_transport.get(), m_gameTransport.get());
	m_room.probeKnocks(now, m_sessionId, m_transport.get());
	return result;
}

Int GameLobby::findSpectator( UnsignedInt identity ) const
{
	for (size_t i = 0; i < m_watchHost.spectators.size(); ++i)
	{
		if (m_watchHost.spectators[i].identity == identity)
			return static_cast<Int>(i);
	}
	return -1;
}

// The JOIN of a spectator passed the cookie, the versions and the name.
void GameLobby::acceptSpectator( const ::Engine::SessionMessage &m, const Transport::RawDatagram &d, const UnicodeString &name, UnsignedInt identity )
{
	const std::size_t unlimited = ::Engine::MAX_SESSION_MESSAGE;
	if (UserSettings::get("Network.AllowSpectators").compareNoCase("no") == 0)
	{
		refuse(d.from, m_sessionId, RET_GAME_STARTED, unlimited);
		return;
	}
	if (!m_watchHost.delaySet)
	{
		m_watchHost.delayFrames = static_cast<UnsignedInt>(atoi(UserSettings::get("Network.SpectatorDelay").str())) * LOGICFRAMES_PER_SECOND;
		m_watchHost.delaySet = TRUE;
	}
	const AsciiString source = m_watchHost.testPath.isNotEmpty() ? m_watchHost.testPath : TheRecorder->getRecordingPath();
	if (source.isEmpty() && m_watchHost.file == nullptr)
	{
		refuse(d.from, m_sessionId, RET_GAME_GONE, unlimited);
		return;
	}
	if (m_watchHost.spectators.size() >= Limit::SPECTATORS)
	{
		refuse(d.from, m_sessionId, RET_GAME_FULL, unlimited);
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
	s.name = name;
	s.heard = timeGetTime();
	m_watchHost.spectators.push_back(std::move(s));
	DEBUG_LOG(("GameLobby::acceptSpectator - %ls watches as %X from %s\n", name.str(), identity, d.from.toString().c_str()));

	::Engine::SessionMessage w;
	w.type = ::Engine::SessionType::WELCOME;
	w.sessionId = m_sessionId;
	w.slot = ::Engine::SESSION_SPECTATOR_SLOT;
	w.identity = identity;
	sendToPeer(w, identity);

	UnicodeString text;
	text.formatUnchecked(TheGameText->fetch("GZH:WatchStarted").str(), name.str());
	tellPlayers(text);
}

void GameLobby::removeSpectator( Int index, Bool tellThem )
{
	if (index < 0 || index >= static_cast<Int>(m_watchHost.spectators.size()))
		return;
	const Spectator s = m_watchHost.spectators[static_cast<size_t>(index)];
	if (tellThem)
	{
		::Engine::SessionMessage leave;
		leave.type = ::Engine::SessionType::LEAVE;
		sendToPeer(leave, s.identity);
	}
	m_transport->removePeerKeys(s.identity);
	m_watchHost.spectators.erase(m_watchHost.spectators.begin() + index);
	if (s.returnSlot >= 0)
		return;	// a returning player: the lockstep tells whether it is back
	UnicodeString text;
	text.formatUnchecked(TheGameText->fetch("GZH:WatchStopped").str(), s.name.str());
	tellPlayers(text);
}

// A line for every player of the running game, as chat of the host.
void GameLobby::tellPlayers( const UnicodeString &text )
{
	if (TheNetwork == nullptr || m_currentGame == nullptr)
		return;
	Int mask = 0;
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		if (m_currentGame->getConstSlot(slot)->isHuman())
			mask |= 1 << slot;
	}
	TheNetwork->sendChat(text, mask);
}

// What the recorder wrote since the last look. The file is complete once the recorder closed it.
void GameLobby::readReplayLog( void )
{
	if (m_watchHost.finished)
		return;
	const AsciiString source = m_watchHost.testPath.isNotEmpty() ? m_watchHost.testPath : TheRecorder->getRecordingPath();
	const Bool recording = source.isNotEmpty();
	if (m_watchHost.file == nullptr)
	{
		if (!recording)
		{
			m_watchHost.finished = !m_watchHost.log.empty();
			return;
		}
		m_watchHost.file = fopen(source.str(), "rb");
		if (m_watchHost.file == nullptr)
			return;
	}
	// a fresh position drops what the C library buffered and its end of file: the recorder appends more
	if (fseek(m_watchHost.file, static_cast<long>(m_watchHost.log.size()), SEEK_SET) != 0)
		return;
	UnsignedByte buffer[16384];
	for (;;)
	{
		const size_t n = fread(buffer, 1, sizeof(buffer), m_watchHost.file);
		if (n == 0)
			break;
		const size_t room = ::Engine::WATCH_MAX_BYTES - m_watchHost.log.size();
		m_watchHost.log.insert(m_watchHost.log.end(), buffer, buffer + (n < room ? n : room));
		if (n >= room)
		{
			DEBUG_LOG(("GameLobby::readReplayLog - the replay outgrew the stream limit\n"));
			break;
		}
	}
	if (!recording)
	{
		fclose(m_watchHost.file);
		m_watchHost.file = nullptr;
		m_watchHost.finished = TRUE;
	}
}

void GameLobby::hostWatchUpdate( UnsignedInt now )
{
	if (m_watchHost.spectators.empty())
		return;
	readReplayLog();
	const UnsignedInt frames = m_watchHost.testPath.isNotEmpty() ? m_watchHost.testFrames : TheGameLogic->getFrame();
	if (!m_watchHost.finished)
		m_watchHost.history.record(static_cast<UnsignedInt>(m_watchHost.log.size()), frames);
	::Engine::WatchMark mark = m_watchHost.history.delayed(m_watchHost.delayFrames);
	// a returning player catches up with the game as it is
	::Engine::WatchMark live = m_watchHost.history.delayed(0);
	if (m_watchHost.finished)
	{
		mark.bytes = static_cast<UnsignedInt>(m_watchHost.log.size());
		if (mark.frames < frames)
			mark.frames = frames;
		live = mark;
	}

	for (Int i = static_cast<Int>(m_watchHost.spectators.size()) - 1; i >= 0; --i)
	{
		Spectator &s = m_watchHost.spectators[static_cast<size_t>(i)];
		if (silentFor(now, s.heard, Timing::SILENCE))
		{
			removeSpectator(i, TRUE);
			continue;
		}
		const ::Engine::WatchMark &own = s.returnSlot >= 0 ? live : mark;
		Bool sent = FALSE;
		UnsignedInt offset = 0;
		UnsignedInt length = 0;
		for (Int n = 0; n < Limit::WATCH_CHUNKS && s.sender.nextChunk(own.bytes, now, offset, length); ++n)
		{
			sendWatchChunk(s, offset, length, own, now);
			sent = TRUE;
		}
		if (!sent && due(now, s.lastSend + Timing::WATCH_MARK))
			sendWatchChunk(s, own.bytes, 0, own, now);
		hostReturnUpdate(s, now);
	}
}

void GameLobby::sendWatchChunk( Spectator &s, UnsignedInt offset, UnsignedInt length, const ::Engine::WatchMark &mark, UnsignedInt now )
{
	::Engine::SessionMessage w;
	w.type = ::Engine::SessionType::WATCH;
	w.offset = offset;
	w.markBytes = mark.bytes;
	w.markFrames = mark.frames;
	w.flag = m_watchHost.finished && mark.bytes == m_watchHost.log.size();
	if (length > 0 && static_cast<size_t>(offset) + length <= m_watchHost.log.size())
		w.chunk.assign(m_watchHost.log.begin() + offset, m_watchHost.log.begin() + offset + length);
	sendToPeer(w, s.identity);
	s.lastSend = now;
}

void GameLobby::spectatorSends( const ::Engine::SessionMessage &m, Int index )
{
	const UnsignedInt now = timeGetTime();
	Spectator &s = m_watchHost.spectators[static_cast<size_t>(index)];
	s.heard = now;
	switch (m.type)
	{
		case ::Engine::SessionType::WATCH_ACK:
			s.sender.acknowledge(m.value, now);
			break;
		case ::Engine::SessionType::LEAVE:
			removeSpectator(index, FALSE);
			break;
		case ::Engine::SessionType::RETURN_READY:
			// caught up: the lockstep takes it in from a frame a little ahead
			if (s.returnSlot >= 0 && s.returnFrame == 0 && TheNetwork != nullptr)
			{
				s.returnFrame = TheNetwork->announcePlayerReturn(s.returnSlot, s.key, s.gamePort, s.gameEndpoint, s.prefill);
				if (s.returnFrame == 0)
					removeSpectator(index, TRUE);
			}
			break;
		case ::Engine::SessionType::RETURN_DONE:
			if (s.returnSlot >= 0 && s.returnFrame != 0 && m.value == s.returnFrame)
				s.done = TRUE;
			break;
		case ::Engine::SessionType::PING:
		{
			::Engine::SessionMessage pong;
			pong.type = ::Engine::SessionType::PONG;
			pong.value = m.value;
			sendToPeer(pong, s.identity);
			break;
		}
		default:
			break;
	}
}

// The game ended for the host, or it leaves the lobby: spectators get what there is and a LEAVE.
void GameLobby::endWatchHost( void )
{
	for (const Spectator &s : m_watchHost.spectators)
	{
		::Engine::SessionMessage leave;
		leave.type = ::Engine::SessionType::LEAVE;
		sendToPeer(leave, s.identity);
		sendToPeer(leave, s.identity);
		if (m_transport)
			m_transport->removePeerKeys(s.identity);
	}
	if (m_watchHost.file != nullptr)
		fclose(m_watchHost.file);
	const AsciiString testPath = m_watchHost.testPath;
	const UnsignedInt testFrames = m_watchHost.testFrames;
	m_watchHost = WatchHost();
	m_watchHost.testPath = testPath;
	m_watchHost.testFrames = testFrames;
}

// The host welcomed us as a spectator of its running game.
void GameLobby::startWatching( const ::Engine::SessionMessage &m )
{
	const UnsignedInt own = ::Engine::SessionHandshake::identityOf(m_keyPair.publicKey);
	if (m.sessionId != m_join.sessionId || m.identity != own)
		return;
	AsciiString path = TheRecorder->getReplayDir();
	TheFileSystem->createDirectory(path);
	path.concat(watchFileName());
	FILE *file = fopen(path.str(), "wb");
	if (file == nullptr)
	{
		DEBUG_LOG(("GameLobby::startWatching - cannot write %s\n", path.str()));
		finishJoin(RET_UNKNOWN);
		return;
	}
	const Bool startReplay = m_watch.startReplay;
	m_watch = WatchFeed();
	m_watch.startReplay = startReplay;
	m_watch.active = TRUE;
	m_watch.hostIdentity = ::Engine::SessionHandshake::identityOf(m_join.hostKey);
	m_watch.file = file;
	m_watch.hostHeard = timeGetTime();
	m_sessionId = m.sessionId;
	m_pendingAction = ACT_NONE;
	m_expectedHostCode.clear();
	std::vector<Transport::RawDatagram> early;
	early.swap(m_join.early);
	m_join = JoinAttempt();
	closeGameTransport();	// a spectator only listens on the session socket
	DEBUG_LOG(("GameLobby::startWatching - watching the game of %X\n", m_watch.hostIdentity));
	if (m_isInLANMenu)
		systemChat(TheGameText->fetch("GZH:WatchJoined"));
	for (const Transport::RawDatagram &e : early)
		handleDatagram(e, FALSE);
}

void GameLobby::watchReceives( const ::Engine::SessionMessage &m )
{
	m_watch.hostHeard = timeGetTime();
	switch (m.type)
	{
		case ::Engine::SessionType::WATCH:
		{
			if (!m.chunk.empty())
			{
				std::vector<std::uint8_t> fresh;
				if (m_watch.receiver.accept(m.offset, m.chunk.data(), m.chunk.size(), fresh) > 0 && m_watch.file != nullptr)
				{
					fwrite(fresh.data(), 1, fresh.size(), m_watch.file);
					fflush(m_watch.file);
				}
			}
			::Engine::WatchMark mark;
			mark.bytes = m.markBytes;
			mark.frames = m.markFrames;
			m_watch.receiver.noteMark(mark);
			if (m.flag && !m_watch.ended)
			{
				m_watch.ended = TRUE;
				m_watch.endBytes = m.markBytes;
			}
			m_watch.ackDue = TRUE;
			break;
		}
		case ::Engine::SessionType::LEAVE:
			// the host is gone: what arrived is all there is
			if (!m_watch.ended)
			{
				m_watch.ended = TRUE;
				m_watch.endBytes = m_watch.receiver.received();
			}
			m_watch.hostIdentity = 0;
			break;
		default:
			break;
	}
}

void GameLobby::watchUpdate( UnsignedInt now )
{
	if (m_watch.hostIdentity != 0)
	{
		if ((m_watch.ackDue && due(now, m_watch.lastAck + Timing::UPDATE)) || due(now, m_watch.lastAck + Timing::WATCH_ACK))
		{
			::Engine::SessionMessage ack;
			ack.type = ::Engine::SessionType::WATCH_ACK;
			ack.value = m_watch.receiver.received();
			sendToPeer(ack, m_watch.hostIdentity);
			m_watch.lastAck = now;
			m_watch.ackDue = FALSE;
		}
		if (silentFor(now, m_watch.hostHeard, Timing::SILENCE) && !m_watch.ended)
		{
			m_watch.ended = TRUE;
			m_watch.endBytes = m_watch.receiver.received();
			m_watch.hostIdentity = 0;
			if (m_watch.started && TheInGameUI)
				TheInGameUI->message("GZH:WatchLost");
		}
	}
	const Bool complete = m_watch.ended && m_watch.receiver.received() >= m_watch.endBytes;

	if (!m_watch.started)
	{
		if (!m_watch.startReplay)
			return;
		if (m_watch.receiver.playableFrames() == 0 && !(complete && m_watch.receiver.received() > 0))
		{
			if (complete)
			{
				if (m_isInLANMenu)
					systemChat(TheGameText->fetch("GZH:WatchLostBeforeStart"));
				stopWatching(FALSE);
			}
			return;
		}
		if (!watchedMapPresent())
		{
			if (m_isInLANMenu)
				systemChat(TheGameText->fetch("GZH:WatchNoMap"));
			stopWatching(TRUE);
			return;
		}
		if (!TheRecorder->playbackLive(watchFileName()))
		{
			if (m_isInLANMenu)
				systemChat(TheGameText->fetch("GZH:WatchLostBeforeStart"));
			stopWatching(TRUE);
			return;
		}
		m_watch.started = TRUE;
		m_isInLANMenu = FALSE;
	}
	if (!TheRecorder->isLive())
	{
		// the replay ended or the spectator quit it
		stopWatching(m_watch.hostIdentity != 0);
		return;
	}
	TheRecorder->setLiveLimit(m_watch.receiver.playableFrames(), complete);
}

// Spectators get no map transfer: the map must be here already.
Bool GameLobby::watchedMapPresent( void )
{
	RecorderClass::ReplayHeader header;
	header.forPlayback = FALSE;
	header.filename = watchFileName();
	if (!TheRecorder->readReplayHeader(header))
		return FALSE;
	ReplayGameInfo info;
	if (!ParseAsciiStringToGameInfo(&info, header.gameOptions))
		return FALSE;
	TheMapCache->updateCache();
	return TheMapCache->findMap(info.getMap()) != nullptr;
}

void GameLobby::stopWatching( Bool tellHost )
{
	if (!m_watch.active)
		return;
	if (tellHost && m_watch.hostIdentity != 0)
	{
		::Engine::SessionMessage leave;
		leave.type = ::Engine::SessionType::LEAVE;
		sendToPeer(leave, m_watch.hostIdentity);
		sendToPeer(leave, m_watch.hostIdentity);
	}
	if (m_watch.hostIdentity != 0 && m_transport)
		m_transport->removePeerKeys(m_watch.hostIdentity);
	if (m_watch.file != nullptr)
		fclose(m_watch.file);
	const Bool startReplay = m_watch.startReplay;
	m_watch = WatchFeed();
	m_watch.startReplay = startReplay;
	m_sessionId = 0;
}
