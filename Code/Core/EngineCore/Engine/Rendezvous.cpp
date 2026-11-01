/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Rendezvous.cpp -- see Rendezvous.h.

#include "Engine/Rendezvous.h"

#include "Monocypher/monocypher.h"

namespace Engine
{

namespace
{
	const std::uint8_t MAGIC[4] = { 'G', 'Z', 'T', 1 };
	const std::uint8_t RELAY_MAGIC[4] = { 'G', 'Z', 'R', 1 };
	constexpr std::size_t MAX_ROOM = 8;
	constexpr std::size_t MAX_NAME = 64;
	constexpr std::size_t MAX_MAP = 160;
	constexpr std::size_t MAX_FINGERPRINT = 8;

	void writeList(WireWriter &w, const WireEndpoint (&list)[TREFF_MAX_ENDPOINTS])
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

	void readList(WireReader &r, WireEndpoint (&list)[TREFF_MAX_ENDPOINTS])
	{
		const std::uint8_t count = r.u8();
		if (count > TREFF_MAX_ENDPOINTS)
		{
			r.fail();
			return;
		}
		for (std::uint8_t i = 0; i < count; ++i)
		{
			list[i] = r.endpoint();
			if (!list[i].isSet())
				r.fail();
		}
	}

	void writeGame(WireWriter &w, const TreffGame &g)
	{
		w.text(g.room, MAX_ROOM);
		w.text(g.gameName, MAX_NAME);
		w.text(g.hostName, MAX_NAME);
		w.text(g.mapName, MAX_MAP);
		w.text(g.fingerprint, MAX_FINGERPRINT);
		w.u8(g.players);
		w.u8(g.maxPlayers);
		w.u32(g.programCRC);
		w.u32(g.dataCRC);
	}

	void readGame(WireReader &r, TreffGame &g)
	{
		g.room = r.text(MAX_ROOM);
		g.gameName = r.text(MAX_NAME);
		g.hostName = r.text(MAX_NAME);
		g.mapName = r.text(MAX_MAP);
		g.fingerprint = r.text(MAX_FINGERPRINT);
		g.players = r.u8();
		g.maxPlayers = r.u8();
		g.programCRC = r.u32();
		g.dataCRC = r.u32();
	}

	void writePadding(WireWriter &w, std::uint16_t padding)
	{
		if (padding > TREFF_MAX_PADDING)
		{
			w.bytes(nullptr, 1);	// fails the writer
			return;
		}
		w.u16(padding);
		w.zeros(padding);
	}

	std::uint16_t readPadding(WireReader &r)
	{
		const std::uint16_t padding = r.u16();
		if (padding > TREFF_MAX_PADDING)
			r.fail();
		else
			r.zeros(padding);
		return padding;
	}

	void writeSocket(WireWriter &w, TreffSocket s)
	{
		w.u8(static_cast<std::uint8_t>(s));
	}

	TreffSocket readSocket(WireReader &r)
	{
		const std::uint8_t s = r.u8();
		if (s > 1)
			r.fail();
		return s == 1 ? TreffSocket::GAME : TreffSocket::LOBBY;
	}

	void writeBody(WireWriter &w, const TreffMessage &m)
	{
		w.u32(m.tag);
		switch (m.type)
		{
			case TreffType::OBSERVE:
				writeSocket(w, m.socket);
				writePadding(w, m.padding);
				break;
			case TreffType::OBSERVED:
				writeSocket(w, m.socket);
				w.endpoint(m.observed);
				break;
			case TreffType::REGISTER:
				w.u32(m.protocol);
				w.text(m.room, MAX_ROOM);
				w.bytes(m.owner, sizeof(m.owner));
				writeSocket(w, m.socket);
				w.u8(m.isPublic ? 1 : 0);
				writeGame(w, m.game);
				writeList(w, m.lobby);
				writeList(w, m.gameSocket);
				break;
			case TreffType::REGISTER_ACK:
				w.text(m.room, MAX_ROOM);
				writeSocket(w, m.socket);
				w.endpoint(m.observed);
				break;
			case TreffType::UNREGISTER:
				w.text(m.room, MAX_ROOM);
				w.bytes(m.owner, sizeof(m.owner));
				break;
			case TreffType::LOOKUP:
				w.u32(m.protocol);
				w.text(m.room, MAX_ROOM);
				writeList(w, m.lobby);
				writeList(w, m.gameSocket);
				writePadding(w, m.padding);
				break;
			case TreffType::FOUND:
			case TreffType::KNOCK:
				w.text(m.room, MAX_ROOM);
				writeList(w, m.lobby);
				writeList(w, m.gameSocket);
				break;
			case TreffType::LIST:
			case TreffType::SERVERS:
				writePadding(w, m.padding);
				break;
			case TreffType::GAMES:
				if (m.games.size() > TREFF_MAX_GAMES)
				{
					w.bytes(nullptr, 1);
					break;
				}
				w.u8(static_cast<std::uint8_t>(m.games.size()));
				for (const TreffGame &g : m.games)
					writeGame(w, g);
				break;
			case TreffType::SERVER_LIST:
				if (m.servers.size() > TREFF_MAX_SERVERS)
				{
					w.bytes(nullptr, 1);
					break;
				}
				w.u8(static_cast<std::uint8_t>(m.servers.size()));
				for (const std::string &s : m.servers)
					w.text(s, TREFF_MAX_SERVER_TEXT);
				break;
			case TreffType::RELAY_OPEN:
				w.text(m.room, MAX_ROOM);
				w.bytes(m.owner, sizeof(m.owner));
				w.u64(m.relayId);
				if (m.relayMembers.size() > TREFF_MAX_RELAY_MEMBERS)
				{
					w.bytes(nullptr, 1);
					break;
				}
				w.u8(static_cast<std::uint8_t>(m.relayMembers.size()));
				for (const TreffRelayMember &member : m.relayMembers)
				{
					w.u32(member.identity);
					w.bytes(member.tokenHash, sizeof(member.tokenHash));
				}
				break;
			case TreffType::RELAY_OPENED:
				w.u64(m.relayId);
				break;
			case TreffType::RELAY_BIND:
				w.u64(m.relayId);
				w.u32(m.identity);
				w.bytes(m.token, sizeof(m.token));
				writeSocket(w, m.socket);
				break;
			case TreffType::RELAY_BOUND:
				w.u64(m.relayId);
				writeSocket(w, m.socket);
				w.endpoint(m.observed);
				break;
			case TreffType::REFUSED:
				w.u8(static_cast<std::uint8_t>(m.reason));
				w.text(m.room, MAX_ROOM);
				break;
			default:
				w.bytes(nullptr, 1);
				break;
		}
	}

	void readBody(WireReader &r, TreffMessage &m)
	{
		m.tag = r.u32();
		switch (m.type)
		{
			case TreffType::OBSERVE:
				m.socket = readSocket(r);
				m.padding = readPadding(r);
				break;
			case TreffType::OBSERVED:
				m.socket = readSocket(r);
				m.observed = r.endpoint();
				break;
			case TreffType::REGISTER:
				m.protocol = r.u32();
				m.room = r.text(MAX_ROOM);
				r.bytes(m.owner, sizeof(m.owner));
				m.socket = readSocket(r);
				m.isPublic = r.flag();
				readGame(r, m.game);
				readList(r, m.lobby);
				readList(r, m.gameSocket);
				break;
			case TreffType::REGISTER_ACK:
				m.room = r.text(MAX_ROOM);
				m.socket = readSocket(r);
				m.observed = r.endpoint();
				break;
			case TreffType::UNREGISTER:
				m.room = r.text(MAX_ROOM);
				r.bytes(m.owner, sizeof(m.owner));
				break;
			case TreffType::LOOKUP:
				m.protocol = r.u32();
				m.room = r.text(MAX_ROOM);
				readList(r, m.lobby);
				readList(r, m.gameSocket);
				m.padding = readPadding(r);
				break;
			case TreffType::FOUND:
			case TreffType::KNOCK:
				m.room = r.text(MAX_ROOM);
				readList(r, m.lobby);
				readList(r, m.gameSocket);
				break;
			case TreffType::LIST:
			case TreffType::SERVERS:
				m.padding = readPadding(r);
				break;
			case TreffType::GAMES:
			{
				const std::uint8_t count = r.u8();
				if (count > TREFF_MAX_GAMES)
				{
					r.fail();
					break;
				}
				for (std::uint8_t i = 0; i < count && r.ok(); ++i)
				{
					TreffGame g;
					readGame(r, g);
					m.games.push_back(g);
				}
				break;
			}
			case TreffType::SERVER_LIST:
			{
				const std::uint8_t count = r.u8();
				if (count > TREFF_MAX_SERVERS)
				{
					r.fail();
					break;
				}
				for (std::uint8_t i = 0; i < count && r.ok(); ++i)
					m.servers.push_back(r.text(TREFF_MAX_SERVER_TEXT));
				break;
			}
			case TreffType::RELAY_OPEN:
			{
				m.room = r.text(MAX_ROOM);
				r.bytes(m.owner, sizeof(m.owner));
				m.relayId = r.u64();
				const std::uint8_t count = r.u8();
				if (count > TREFF_MAX_RELAY_MEMBERS)
				{
					r.fail();
					break;
				}
				for (std::uint8_t i = 0; i < count && r.ok(); ++i)
				{
					TreffRelayMember member;
					member.identity = r.u32();
					r.bytes(member.tokenHash, sizeof(member.tokenHash));
					m.relayMembers.push_back(member);
				}
				break;
			}
			case TreffType::RELAY_OPENED:
				m.relayId = r.u64();
				break;
			case TreffType::RELAY_BIND:
				m.relayId = r.u64();
				m.identity = r.u32();
				r.bytes(m.token, sizeof(m.token));
				m.socket = readSocket(r);
				break;
			case TreffType::RELAY_BOUND:
				m.relayId = r.u64();
				m.socket = readSocket(r);
				m.observed = r.endpoint();
				break;
			case TreffType::REFUSED:
			{
				const std::uint8_t reason = r.u8();
				if (reason > static_cast<std::uint8_t>(TreffRefusal::BAD_TOKEN))
					r.fail();
				m.reason = static_cast<TreffRefusal>(reason);
				m.room = r.text(MAX_ROOM);
				break;
			}
			default:
				r.fail();
				break;
		}
	}

	bool isKnownType(std::uint8_t type)
	{
		return type >= static_cast<std::uint8_t>(TreffType::OBSERVE) && type <= static_cast<std::uint8_t>(TreffType::REFUSED);
	}
}

bool looksLikeTreff(const std::uint8_t *data, std::size_t size)
{
	return data != nullptr && size >= TREFF_HEADER_SIZE && data[0] == MAGIC[0] && data[1] == MAGIC[1]
		&& data[2] == MAGIC[2] && data[3] == MAGIC[3];
}

std::size_t encodeTreff(const TreffMessage &message, std::uint8_t *out, std::size_t capacity)
{
	if (!isKnownType(static_cast<std::uint8_t>(message.type)))
		return 0;
	const std::size_t limit = capacity < MAX_TREFF_MESSAGE ? capacity : MAX_TREFF_MESSAGE;
	WireWriter w(out, limit);
	w.bytes(MAGIC, sizeof(MAGIC));
	w.u8(static_cast<std::uint8_t>(message.type));
	w.u8(0);
	w.u16(0);
	writeBody(w, message);
	if (!w.ok())
		return 0;
	w.patchU16(6, static_cast<std::uint16_t>(w.size() - TREFF_HEADER_SIZE));
	return w.ok() ? w.size() : 0;
}

bool decodeTreff(const std::uint8_t *data, std::size_t size, TreffMessage &out)
{
	if (!looksLikeTreff(data, size) || size > MAX_TREFF_MESSAGE)
		return false;
	WireReader header(data, TREFF_HEADER_SIZE);
	header.skip(4);
	const std::uint8_t type = header.u8();
	const std::uint8_t reserved = header.u8();
	const std::uint16_t length = header.u16();
	if (!header.done() || reserved != 0 || !isKnownType(type) || length != size - TREFF_HEADER_SIZE)
		return false;
	TreffMessage m;
	m.type = static_cast<TreffType>(type);
	WireReader r(data + TREFF_HEADER_SIZE, length);
	readBody(r, m);
	if (!r.done())
		return false;
	out = std::move(m);
	return true;
}

void padTreffRequest(TreffMessage &message, std::size_t minSize)
{
	message.padding = 0;
	std::uint8_t buffer[MAX_TREFF_MESSAGE];
	const std::size_t size = encodeTreff(message, buffer, sizeof(buffer));
	if (size != 0 && size < minSize)
	{
		const std::size_t missing = minSize - size;
		message.padding = static_cast<std::uint16_t>(missing > TREFF_MAX_PADDING ? TREFF_MAX_PADDING : missing);
	}
}

void relayTokenHash(const std::uint8_t token[16], std::uint8_t out[16])
{
	static const char LABEL[] = "GZH Neo relay token";
	crypto_blake2b_ctx ctx;
	crypto_blake2b_init(&ctx, 16);
	crypto_blake2b_update(&ctx, reinterpret_cast<const std::uint8_t *>(LABEL), sizeof(LABEL));
	crypto_blake2b_update(&ctx, token, 16);
	crypto_blake2b_final(&ctx, out);
}

bool looksLikeRelayFrame(const std::uint8_t *data, std::size_t size)
{
	return data != nullptr && size >= RELAY_HEADER_SIZE && data[0] == RELAY_MAGIC[0] && data[1] == RELAY_MAGIC[1]
		&& data[2] == RELAY_MAGIC[2] && data[3] == RELAY_MAGIC[3];
}

void writeRelayHeader(const RelayHeader &header, std::uint8_t out[RELAY_HEADER_SIZE])
{
	WireWriter w(out, RELAY_HEADER_SIZE);
	w.bytes(RELAY_MAGIC, sizeof(RELAY_MAGIC));
	w.u64(header.relayId);
	w.u32(header.from);
	w.u32(header.to);
}

bool readRelayHeader(const std::uint8_t *data, std::size_t size, RelayHeader &out)
{
	if (!looksLikeRelayFrame(data, size))
		return false;
	WireReader r(data + 4, RELAY_HEADER_SIZE - 4);
	RelayHeader h;
	h.relayId = r.u64();
	h.from = r.u32();
	h.to = r.u32();
	if (!r.done())
		return false;
	out = h;
	return true;
}

} // namespace Engine
