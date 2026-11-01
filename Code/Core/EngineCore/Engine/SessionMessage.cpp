/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SessionMessage.cpp -- see SessionMessage.h.

#include "Engine/SessionMessage.h"

namespace Engine
{

namespace
{
	const std::uint8_t MAGIC[4] = { 'G', 'Z', 'S', 1 };

	void writeEndpointList(WireWriter &w, const WireEndpoint (&list)[SESSION_MAX_ENDPOINTS])
	{
		std::uint8_t count = 0;
		for (const WireEndpoint &e : list)
			count = static_cast<std::uint8_t>(count + (e.isSet() ? 1 : 0));
		w.u8(count);
		for (const WireEndpoint &e : list)
		{
			if (e.isSet())
				w.endpoint(e);
		}
	}

	void readEndpointList(WireReader &r, WireEndpoint (&list)[SESSION_MAX_ENDPOINTS])
	{
		const std::uint8_t count = r.u8();
		if (count > SESSION_MAX_ENDPOINTS)
		{
			r.fail();
			return;
		}
		for (std::uint8_t i = 0; i < count; ++i)
		{
			list[i] = r.endpoint();
			if (!list[i].isSet())
				r.fail();	// a listed endpoint is never "none"
		}
	}

	void writeEndpoints(WireWriter &w, const SessionEndpoints &e)
	{
		writeEndpointList(w, e.lobby);
		writeEndpointList(w, e.game);
	}

	void readEndpoints(WireReader &r, SessionEndpoints &e)
	{
		readEndpointList(r, e.lobby);
		readEndpointList(r, e.game);
	}

	void writeBody(WireWriter &w, const SessionMessage &m)
	{
		switch (m.type)
		{
			case SessionType::QUERY:
				w.u32(m.protocol);
				break;
			case SessionType::ANNOUNCE:
				w.u32(m.protocol);
				w.u64(m.sessionId);
				w.u32(m.programCRC);
				w.u32(m.dataCRC);
				w.bytes(m.fingerprint, sizeof(m.fingerprint));
				w.text(m.gameName, SESSION_MAX_GAME_NAME);
				w.text(m.hostName, SESSION_MAX_NAME);
				w.text(m.mapName, SESSION_MAX_MAP);
				w.u8(m.players);
				w.u8(m.maxPlayers);
				w.u8(m.inProgress ? 1 : 0);
				w.longText(m.options, SESSION_MAX_OPTIONS);
				break;
			case SessionType::PRESENCE:
				w.text(m.name, SESSION_MAX_NAME);
				break;
			case SessionType::LOBBY_CHAT:
				w.text(m.name, SESSION_MAX_NAME);
				w.u8(m.chatKind);
				w.longText(m.text, SESSION_MAX_CHAT);
				break;
			case SessionType::BYE:
				break;
			case SessionType::HELLO:
				w.u32(m.protocol);
				w.u64(m.sessionId);
				w.bytes(m.key, SESSION_KEY_SIZE);
				w.bytes(m.nonce, sizeof(m.nonce));
				w.zeros(SESSION_HELLO_PADDING);
				break;
			case SessionType::CHALLENGE:
				w.u64(m.sessionId);
				w.bytes(m.key, SESSION_KEY_SIZE);
				w.bytes(m.cookie, sizeof(m.cookie));
				w.bytes(m.nonce, sizeof(m.nonce));
				break;
			case SessionType::JOIN:
				w.u64(m.sessionId);
				w.bytes(m.key, SESSION_KEY_SIZE);
				w.bytes(m.nonce, sizeof(m.nonce));
				w.bytes(m.cookie, sizeof(m.cookie));
				if (m.sealed.size() > SESSION_MAX_SEALED)
				{
					w.u16(0xFFFF);	// makes the writer fail below
					w.bytes(nullptr, 1);
					break;
				}
				w.u16(static_cast<std::uint16_t>(m.sealed.size()));
				w.bytes(m.sealed.data(), m.sealed.size());
				break;
			case SessionType::REFUSE:
				w.u8(m.reason);
				w.u32(m.protocol);
				w.text(m.versionText, SESSION_MAX_VERSION);
				w.u64(m.sessionId);
				break;
			case SessionType::PROBE:
			case SessionType::PROBE_ACK:
				w.u64(m.sessionId);
				w.u32(m.identity);
				w.u32(m.value);
				break;
			case SessionType::WELCOME:
				w.u64(m.sessionId);
				w.u8(m.slot);
				w.u32(m.identity);
				w.bytes(m.token, SESSION_TOKEN_SIZE);
				w.u8(m.flag ? 1 : 0);
				break;
			case SessionType::STATE:
				w.u32(m.revision);
				w.longText(m.options, SESSION_MAX_OPTIONS);
				break;
			case SessionType::MEMBER:
				w.u8(m.member.slot);
				w.u32(m.member.identity);
				w.bytes(m.member.key, SESSION_KEY_SIZE);
				writeEndpoints(w, m.member.endpoints);
				break;
			case SessionType::WISH:
				w.text(m.text, SESSION_MAX_WISH);
				break;
			case SessionType::ACCEPT:
				w.u8(m.flag ? 1 : 0);
				break;
			case SessionType::HAS_MAP:
				w.u8(m.flag ? 1 : 0);
				w.u32(m.mapCRC);
				break;
			case SessionType::CHAT:
				w.u8(m.slot);
				w.u8(m.chatKind);
				w.longText(m.text, SESSION_MAX_CHAT);
				break;
			case SessionType::TIMER:
				w.u32(m.value);
				break;
			case SessionType::START:
			case SessionType::LEAVE:
				break;
			case SessionType::PING:
			case SessionType::PONG:
				w.u32(m.value);
				break;
			case SessionType::RELAY:
				w.text(m.relayServer, SESSION_MAX_SERVER);
				w.u64(m.relayId);
				w.bytes(m.relayToken, sizeof(m.relayToken));
				break;
			case SessionType::WATCH:
				w.u32(m.offset);
				w.u32(m.markBytes);
				w.u32(m.markFrames);
				w.u8(m.flag ? 1 : 0);
				if (m.chunk.size() > SESSION_MAX_WATCH_CHUNK)
				{
					w.bytes(nullptr, 1);	// fail
					break;
				}
				w.u16(static_cast<std::uint16_t>(m.chunk.size()));
				w.bytes(m.chunk.data(), m.chunk.size());
				break;
			case SessionType::WATCH_ACK:
			case SessionType::RETURN_READY:
			case SessionType::RETURN_DONE:
				w.u32(m.value);
				break;
			case SessionType::RETURN_AT:
				w.u32(m.value);
				w.u32(m.offset);
				w.u8(m.slot);
				break;
			case SessionType::RETURN_STATE:
			{
				const SessionReturnState &s = m.returnState;
				w.u32(s.frame);
				w.u8(s.connected);
				w.u8(s.crcPending);
				w.u8(s.router);
				w.bytes(s.fallback, SESSION_SLOTS);
				for (std::uint32_t frame : s.dropFrame)
					w.u32(frame);
				w.u16(s.runAhead);
				w.u16(s.frameRate);
				break;
			}
			default:
				w.bytes(nullptr, 1);	// unknown type: fail
				break;
		}
	}

	void readBody(WireReader &r, SessionMessage &m)
	{
		switch (m.type)
		{
			case SessionType::QUERY:
				m.protocol = r.u32();
				break;
			case SessionType::ANNOUNCE:
				m.protocol = r.u32();
				m.sessionId = r.u64();
				m.programCRC = r.u32();
				m.dataCRC = r.u32();
				r.bytes(m.fingerprint, sizeof(m.fingerprint));
				m.gameName = r.text(SESSION_MAX_GAME_NAME);
				m.hostName = r.text(SESSION_MAX_NAME);
				m.mapName = r.text(SESSION_MAX_MAP);
				m.players = r.u8();
				m.maxPlayers = r.u8();
				m.inProgress = r.flag();
				m.options = r.longText(SESSION_MAX_OPTIONS);
				break;

			case SessionType::PRESENCE:
				m.name = r.text(SESSION_MAX_NAME);
				break;
			case SessionType::LOBBY_CHAT:
				m.name = r.text(SESSION_MAX_NAME);
				m.chatKind = r.u8();
				m.text = r.longText(SESSION_MAX_CHAT);
				break;
			case SessionType::BYE:
				break;
			case SessionType::HELLO:
				m.protocol = r.u32();
				m.sessionId = r.u64();
				r.bytes(m.key, SESSION_KEY_SIZE);
				r.bytes(m.nonce, sizeof(m.nonce));
				r.zeros(SESSION_HELLO_PADDING);
				break;
			case SessionType::CHALLENGE:
				m.sessionId = r.u64();
				r.bytes(m.key, SESSION_KEY_SIZE);
				r.bytes(m.cookie, sizeof(m.cookie));
				r.bytes(m.nonce, sizeof(m.nonce));
				break;
			case SessionType::JOIN:
			{
				m.sessionId = r.u64();
				r.bytes(m.key, SESSION_KEY_SIZE);
				r.bytes(m.nonce, sizeof(m.nonce));
				r.bytes(m.cookie, sizeof(m.cookie));
				const std::uint16_t size = r.u16();
				if (size > SESSION_MAX_SEALED || size > r.remaining())
				{
					r.fail();
					break;
				}
				m.sealed.resize(size);
				r.bytes(m.sealed.data(), size);
				break;
			}
			case SessionType::REFUSE:
				m.reason = r.u8();
				m.protocol = r.u32();
				m.versionText = r.text(SESSION_MAX_VERSION);
				m.sessionId = r.u64();
				break;
			case SessionType::PROBE:
			case SessionType::PROBE_ACK:
				m.sessionId = r.u64();
				m.identity = r.u32();
				m.value = r.u32();
				break;
			case SessionType::WELCOME:
				m.sessionId = r.u64();
				m.slot = r.u8();
				m.identity = r.u32();
				r.bytes(m.token, SESSION_TOKEN_SIZE);
				m.flag = r.flag();
				break;
			case SessionType::STATE:
				m.revision = r.u32();
				m.options = r.longText(SESSION_MAX_OPTIONS);
				break;
			case SessionType::MEMBER:
				m.member.slot = r.u8();
				m.member.identity = r.u32();
				r.bytes(m.member.key, SESSION_KEY_SIZE);
				readEndpoints(r, m.member.endpoints);
				break;
			case SessionType::WISH:
				m.text = r.text(SESSION_MAX_WISH);
				break;
			case SessionType::ACCEPT:
				m.flag = r.flag();
				break;
			case SessionType::HAS_MAP:
				m.flag = r.flag();
				m.mapCRC = r.u32();
				break;
			case SessionType::CHAT:
				m.slot = r.u8();
				m.chatKind = r.u8();
				m.text = r.longText(SESSION_MAX_CHAT);
				break;
			case SessionType::TIMER:
				m.value = r.u32();
				break;
			case SessionType::START:
			case SessionType::LEAVE:
				break;
			case SessionType::PING:
			case SessionType::PONG:
				m.value = r.u32();
				break;
			case SessionType::RELAY:
				m.relayServer = r.text(SESSION_MAX_SERVER);
				m.relayId = r.u64();
				r.bytes(m.relayToken, sizeof(m.relayToken));
				break;
			case SessionType::WATCH:
			{
				m.offset = r.u32();
				m.markBytes = r.u32();
				m.markFrames = r.u32();
				m.flag = r.flag();
				const std::uint16_t size = r.u16();
				if (size > SESSION_MAX_WATCH_CHUNK || size > r.remaining())
				{
					r.fail();
					break;
				}
				m.chunk.resize(size);
				r.bytes(m.chunk.data(), size);
				break;
			}
			case SessionType::WATCH_ACK:
			case SessionType::RETURN_READY:
			case SessionType::RETURN_DONE:
				m.value = r.u32();
				break;
			case SessionType::RETURN_AT:
				m.value = r.u32();
				m.offset = r.u32();
				m.slot = r.u8();
				break;
			case SessionType::RETURN_STATE:
			{
				SessionReturnState &s = m.returnState;
				s.frame = r.u32();
				s.connected = r.u8();
				s.crcPending = r.u8();
				s.router = r.u8();
				r.bytes(s.fallback, SESSION_SLOTS);
				for (std::uint32_t &frame : s.dropFrame)
					frame = r.u32();
				s.runAhead = r.u16();
				s.frameRate = r.u16();
				break;
			}
			default:
				r.fail();
				break;
		}
	}
}

bool isKnownSessionType(std::uint8_t type)
{
	return (type >= static_cast<std::uint8_t>(SessionType::QUERY) && type <= static_cast<std::uint8_t>(SessionType::PROBE_ACK))
		|| (type >= static_cast<std::uint8_t>(SessionType::WELCOME) && type <= static_cast<std::uint8_t>(SessionType::RETURN_DONE));
}

bool looksLikeSession(const std::uint8_t *data, std::size_t size)
{
	return data != nullptr && size >= SESSION_HEADER_SIZE && data[0] == MAGIC[0] && data[1] == MAGIC[1]
		&& data[2] == MAGIC[2] && data[3] == MAGIC[3];
}

std::size_t encodeSession(const SessionMessage &message, std::uint8_t *out, std::size_t capacity)
{
	if (!isKnownSessionType(static_cast<std::uint8_t>(message.type)))
		return 0;
	const std::size_t limit = capacity < MAX_SESSION_MESSAGE ? capacity : MAX_SESSION_MESSAGE;
	WireWriter w(out, limit);
	w.bytes(MAGIC, sizeof(MAGIC));
	w.u8(static_cast<std::uint8_t>(message.type));
	w.u8(0);	// reserved
	w.u16(0);	// body length, patched below
	writeBody(w, message);
	if (!w.ok())
		return 0;
	w.patchU16(6, static_cast<std::uint16_t>(w.size() - SESSION_HEADER_SIZE));
	return w.ok() ? w.size() : 0;
}

bool decodeSession(const std::uint8_t *data, std::size_t size, SessionMessage &out)
{
	if (!looksLikeSession(data, size) || size > MAX_SESSION_MESSAGE)
		return false;
	WireReader header(data, SESSION_HEADER_SIZE);
	header.skip(4);
	const std::uint8_t type = header.u8();
	const std::uint8_t reserved = header.u8();
	const std::uint16_t length = header.u16();
	if (!header.done() || reserved != 0 || !isKnownSessionType(type) || length != size - SESSION_HEADER_SIZE)
		return false;
	SessionMessage m;
	m.type = static_cast<SessionType>(type);
	WireReader r(data + SESSION_HEADER_SIZE, length);
	readBody(r, m);
	if (!r.done())
		return false;
	out = std::move(m);
	return true;
}

std::size_t encodeJoinInfo(const SessionJoinInfo &info, std::uint8_t *out, std::size_t capacity)
{
	WireWriter w(out, capacity);
	w.text(info.name, SESSION_MAX_NAME);
	w.u32(info.protocol);
	w.u32(info.programCRC);
	w.u32(info.dataCRC);
	w.text(info.versionText, SESSION_MAX_VERSION);
	writeEndpoints(w, info.endpoints);
	w.u8(static_cast<std::uint8_t>(info.wants));
	if (info.wants == SessionWants::RETURN)
	{
		w.u8(info.returnSlot);
		w.bytes(info.returnToken, SESSION_TOKEN_SIZE);
	}
	return w.ok() ? w.size() : 0;
}

bool decodeJoinInfo(const std::uint8_t *data, std::size_t size, SessionJoinInfo &out)
{
	WireReader r(data, size);
	SessionJoinInfo info;
	info.name = r.text(SESSION_MAX_NAME);
	info.protocol = r.u32();
	info.programCRC = r.u32();
	info.dataCRC = r.u32();
	info.versionText = r.text(SESSION_MAX_VERSION);
	readEndpoints(r, info.endpoints);
	const std::uint8_t wants = r.u8();
	if (wants > static_cast<std::uint8_t>(SessionWants::RETURN))
		return false;
	info.wants = static_cast<SessionWants>(wants);
	if (info.wants == SessionWants::RETURN)
	{
		info.returnSlot = r.u8();
		r.bytes(info.returnToken, SESSION_TOKEN_SIZE);
	}
	if (!r.done())
		return false;
	out = std::move(info);
	return true;
}

} // namespace Engine
