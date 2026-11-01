/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RendezvousServer.cpp -- see RendezvousServer.h.

#include "Engine/RendezvousServer.h"

#include "Engine/InviteCode.h"

#include <cstring>

namespace Engine
{

namespace
{
	bool expired(std::uint32_t last, std::uint32_t now, std::uint32_t ttl)
	{
		return static_cast<std::uint32_t>(now - last) > ttl;
	}

	bool validRoom(const std::string &room)
	{
		return !room.empty() && canonicalCrockford(room, 4) == room;
	}

	bool sameSecret(const std::uint8_t a[16], const std::uint8_t b[16])
	{
		std::uint8_t diff = 0;
		for (int i = 0; i < 16; ++i)
			diff = static_cast<std::uint8_t>(diff | (a[i] ^ b[i]));
		return diff == 0;
	}

	/// First the endpoint the server saw, then what the client reported, without duplicates.
	void combine(const WireEndpoint &observed, const WireEndpoint (&reported)[TREFF_MAX_ENDPOINTS],
		WireEndpoint (&out)[TREFF_MAX_ENDPOINTS])
	{
		int n = 0;
		if (observed.isSet())
			out[n++] = observed;
		for (const WireEndpoint &e : reported)
		{
			if (n >= TREFF_MAX_ENDPOINTS)
				break;
			bool known = !e.isSet();
			for (int k = 0; k < n && !known; ++k)
				known = out[k] == e;
			if (!known)
				out[n++] = e;
		}
		for (; n < TREFF_MAX_ENDPOINTS; ++n)
			out[n] = WireEndpoint();
	}
}

std::uint32_t rendezvousAddressKey(const WireEndpoint &endpoint)
{
	if (endpoint.family == 4)
		return endpoint.ipv4Address();
	// FNV-1a over the /64: one household or host has one budget
	std::uint32_t h = 2166136261u;
	for (int i = 0; i < 8; ++i)
		h = (h ^ endpoint.address[i]) * 16777619u;
	return h | 1u;
}

void RendezvousServer::configure(const Config &config)
{
	m_config = config;
	m_limiter.configure(config.perSender, config.shared);
	m_rooms.clear();
	m_relays.clear();
	m_stats = Stats();
}

int RendezvousServer::roomsOf(std::uint32_t addressKey) const
{
	int n = 0;
	for (const auto &entry : m_rooms)
		n += entry.second.addressKey == addressKey ? 1 : 0;
	return n;
}

void RendezvousServer::send(std::vector<Datagram> &out, const WireEndpoint &to, const TreffMessage &message,
	std::size_t sizeLimit)
{
	Datagram d;
	d.to = to;
	d.data.resize(MAX_TREFF_MESSAGE);
	const std::size_t size = encodeTreff(message, d.data.data(), d.data.size());
	if (size == 0 || size > sizeLimit)
	{
		++m_stats.limited;
		return;
	}
	d.data.resize(size);
	out.push_back(std::move(d));
	++m_stats.answered;
}

void RendezvousServer::refuse(std::vector<Datagram> &out, const WireEndpoint &to, std::uint32_t tag,
	TreffRefusal reason, const std::string &room)
{
	TreffMessage m;
	m.type = TreffType::REFUSED;
	m.tag = tag;
	m.reason = reason;
	m.room = validRoom(room) ? room : std::string();
	send(out, to, m, MAX_TREFF_MESSAGE);
}

void RendezvousServer::handle(const WireEndpoint &from, const std::uint8_t *data, std::size_t size,
	std::uint32_t nowMs, std::vector<Datagram> &out)
{
	++m_stats.received;
	if (!from.isSet() || data == nullptr)
		return;
	if (looksLikeRelayFrame(data, size))
	{
		handleRelayFrame(from, data, size, nowMs, out);
		return;
	}
	if (!m_limiter.allow(rendezvousAddressKey(from), nowMs))
	{
		++m_stats.limited;
		return;
	}
	TreffMessage m;
	if (!decodeTreff(data, size, m))
	{
		++m_stats.malformed;
		return;
	}
	handleMessage(from, m, size, nowMs, out);
}

void RendezvousServer::handleMessage(const WireEndpoint &from, const TreffMessage &m, std::size_t size,
	std::uint32_t nowMs, std::vector<Datagram> &out)
{
	switch (m.type)
	{
		case TreffType::OBSERVE:
		{
			if (size < TREFF_OBSERVE_SIZE)
			{
				++m_stats.limited;
				return;
			}
			TreffMessage a;
			a.type = TreffType::OBSERVED;
			a.tag = m.tag;
			a.socket = m.socket;
			a.observed = from;
			send(out, from, a, size);
			return;
		}
		case TreffType::REGISTER:
		{
			if (!validRoom(m.room))
			{
				++m_stats.malformed;
				return;
			}
			auto it = m_rooms.find(m.room);
			if (m.socket == TreffSocket::GAME)
			{
				if (it == m_rooms.end() || !sameSecret(it->second.owner, m.owner))
				{
					refuse(out, from, m.tag, TreffRefusal::NO_ROOM, m.room);
					return;
				}
				it->second.hostGame = from;
			}
			else
			{
				if (it != m_rooms.end() && !sameSecret(it->second.owner, m.owner))
				{
					refuse(out, from, m.tag, TreffRefusal::ROOM_TAKEN, m.room);
					return;
				}
				const std::uint32_t key = rendezvousAddressKey(from);
				if (it == m_rooms.end())
				{
					if (m_rooms.size() >= m_config.maxRooms || roomsOf(key) >= m_config.roomsPerAddress)
					{
						refuse(out, from, m.tag, TreffRefusal::LIMIT, m.room);
						return;
					}
					it = m_rooms.emplace(m.room, Room()).first;
					std::memcpy(it->second.owner, m.owner, sizeof(m.owner));
				}
				Room &room = it->second;
				room.hostLobby = from;
				room.addressKey = key;
				room.isPublic = m.isPublic;
				room.game = m.game;
				room.game.room = m.room;
				for (int i = 0; i < TREFF_MAX_ENDPOINTS; ++i)
				{
					room.reportedLobby[i] = m.lobby[i];
					room.reportedGame[i] = m.gameSocket[i];
				}
				room.lastRefresh = nowMs;
			}
			TreffMessage a;
			a.type = TreffType::REGISTER_ACK;
			a.tag = m.tag;
			a.room = m.room;
			a.socket = m.socket;
			a.observed = from;
			send(out, from, a, MAX_TREFF_MESSAGE);
			return;
		}
		case TreffType::UNREGISTER:
		{
			auto it = m_rooms.find(m.room);
			if (it != m_rooms.end() && sameSecret(it->second.owner, m.owner) && it->second.hostLobby == from)
				m_rooms.erase(it);
			return;
		}
		case TreffType::LOOKUP:
		{
			if (size < TREFF_LOOKUP_SIZE)
			{
				++m_stats.limited;
				return;
			}
			auto it = m_rooms.find(m.room);
			if (it == m_rooms.end())
			{
				refuse(out, from, m.tag, TreffRefusal::NO_ROOM, m.room);
				return;
			}
			Room &room = it->second;
			TreffMessage found;
			found.type = TreffType::FOUND;
			found.tag = m.tag;
			found.room = m.room;
			combine(room.hostLobby, room.reportedLobby, found.lobby);
			combine(room.hostGame, room.reportedGame, found.gameSocket);
			send(out, from, found, size);

			// the host learns where the joiner is, so both can open their NATs towards each other
			if (static_cast<std::uint32_t>(nowMs - room.knockWindow) >= 1000)
			{
				room.knockWindow = nowMs;
				room.knocks = 0;
			}
			if (room.knocks < m_config.knocksPerRoomPerSecond)
			{
				++room.knocks;
				TreffMessage knock;
				knock.type = TreffType::KNOCK;
				knock.room = m.room;
				combine(from, m.lobby, knock.lobby);
				WireEndpoint none;
				combine(none, m.gameSocket, knock.gameSocket);
				send(out, room.hostLobby, knock, MAX_TREFF_MESSAGE);
			}
			return;
		}
		case TreffType::LIST:
		{
			if (size < TREFF_LIST_SIZE)
			{
				++m_stats.limited;
				return;
			}
			TreffMessage a;
			a.type = TreffType::GAMES;
			a.tag = m.tag;
			std::uint8_t buffer[MAX_TREFF_MESSAGE];
			for (const auto &entry : m_rooms)
			{
				if (!entry.second.isPublic || a.games.size() >= TREFF_MAX_GAMES)
					continue;
				a.games.push_back(entry.second.game);
				const std::size_t encoded = encodeTreff(a, buffer, sizeof(buffer));
				if (encoded == 0 || encoded > size)
				{
					a.games.pop_back();
					break;
				}
			}
			send(out, from, a, size);
			return;
		}
		case TreffType::SERVERS:
		{
			if (size < TREFF_LIST_SIZE)
			{
				++m_stats.limited;
				return;
			}
			TreffMessage a;
			a.type = TreffType::SERVER_LIST;
			a.tag = m.tag;
			for (const std::string &peer : m_config.peers)
			{
				if (a.servers.size() < TREFF_MAX_SERVERS && peer.size() <= TREFF_MAX_SERVER_TEXT)
					a.servers.push_back(peer);
			}
			send(out, from, a, size);
			return;
		}
		case TreffType::RELAY_OPEN:
		{
			if (!m_config.relay)
			{
				refuse(out, from, m.tag, TreffRefusal::RELAY_OFF, m.room);
				return;
			}
			auto it = m_rooms.find(m.room);
			if (it == m_rooms.end() || !sameSecret(it->second.owner, m.owner) || it->second.hostLobby != from)
			{
				refuse(out, from, m.tag, TreffRefusal::NO_ROOM, m.room);
				return;
			}
			auto relay = m_relays.find(m.relayId);
			if (relay == m_relays.end())
			{
				if (m_relays.size() >= m_config.maxRelays || m.relayId == 0)
				{
					refuse(out, from, m.tag, TreffRefusal::LIMIT, m.room);
					return;
				}
				relay = m_relays.emplace(m.relayId, Relay()).first;
				relay->second.room = m.room;
			}
			else if (relay->second.room != m.room)
			{
				refuse(out, from, m.tag, TreffRefusal::NO_ROOM, m.room);
				return;
			}
			// keep the bindings of members that stay
			std::vector<Binding> members;
			for (const TreffRelayMember &member : m.relayMembers)
			{
				Binding b;
				b.identity = member.identity;
				std::memcpy(b.tokenHash, member.tokenHash, sizeof(b.tokenHash));
				for (const Binding &old : relay->second.members)
				{
					if (old.identity == b.identity && sameSecret(old.tokenHash, b.tokenHash))
					{
						b.lobby = old.lobby;
						b.game = old.game;
					}
				}
				members.push_back(b);
			}
			relay->second.members = members;
			relay->second.lastUse = nowMs;
			TreffMessage a;
			a.type = TreffType::RELAY_OPENED;
			a.tag = m.tag;
			a.relayId = m.relayId;
			send(out, from, a, MAX_TREFF_MESSAGE);
			return;
		}
		case TreffType::RELAY_BIND:
		{
			auto relay = m_relays.find(m.relayId);
			std::uint8_t hash[16];
			relayTokenHash(m.token, hash);
			Binding *binding = nullptr;
			if (relay != m_relays.end())
			{
				for (Binding &b : relay->second.members)
				{
					if (b.identity == m.identity && sameSecret(b.tokenHash, hash))
						binding = &b;
				}
			}
			if (binding == nullptr)
			{
				refuse(out, from, m.tag, TreffRefusal::BAD_TOKEN, std::string());
				return;
			}
			if (m.socket == TreffSocket::GAME)
				binding->game = from;
			else
				binding->lobby = from;
			relay->second.lastUse = nowMs;
			TreffMessage a;
			a.type = TreffType::RELAY_BOUND;
			a.tag = m.tag;
			a.relayId = m.relayId;
			a.socket = m.socket;
			a.observed = from;
			send(out, from, a, size);
			return;
		}
		default:
			// answers are for clients; a server ignores them
			++m_stats.malformed;
			return;
	}
}

void RendezvousServer::handleRelayFrame(const WireEndpoint &from, const std::uint8_t *data, std::size_t size,
	std::uint32_t nowMs, std::vector<Datagram> &out)
{
	RelayHeader header;
	auto relay = m_relays.end();
	if (readRelayHeader(data, size, header))
		relay = m_relays.find(header.relayId);
	if (relay == m_relays.end() || size > MAX_TREFF_MESSAGE + RELAY_HEADER_SIZE)
	{
		++m_stats.relayDropped;
		return;
	}
	const Binding *sender = nullptr;
	const Binding *receiver = nullptr;
	bool gameSocket = false;
	for (const Binding &b : relay->second.members)
	{
		if (b.identity == header.from)
		{
			if (b.lobby.isSet() && b.lobby == from)
				sender = &b;
			else if (b.game.isSet() && b.game == from)
			{
				sender = &b;
				gameSocket = true;
			}
		}
	}
	if (sender != nullptr)
	{
		for (const Binding &b : relay->second.members)
		{
			if (b.identity == header.to && b.identity != header.from)
				receiver = &b;
		}
	}
	const WireEndpoint *target = nullptr;
	if (receiver != nullptr)
		target = gameSocket ? &receiver->game : &receiver->lobby;
	if (target == nullptr || !target->isSet())
	{
		++m_stats.relayDropped;
		return;
	}
	Relay &r = relay->second;
	if (static_cast<std::uint32_t>(nowMs - r.window) >= 1000)
	{
		r.window = nowMs;
		r.windowBytes = 0;
	}
	if (static_cast<std::uint32_t>(nowMs - m_totalWindow) >= 1000)
	{
		m_totalWindow = nowMs;
		m_totalWindowBytes = 0;
	}
	if (r.windowBytes + size > m_config.relayBytesPerSecond || m_totalWindowBytes + size > m_config.relayTotalBytesPerSecond)
	{
		++m_stats.relayDropped;
		return;
	}
	r.windowBytes += size;
	m_totalWindowBytes += size;
	r.lastUse = nowMs;
	Datagram d;
	d.to = *target;
	d.data.assign(data, data + size);
	out.push_back(std::move(d));
	++m_stats.relayed;
	m_stats.relayedBytes += size;
}

void RendezvousServer::expire(std::uint32_t nowMs)
{
	for (auto it = m_rooms.begin(); it != m_rooms.end();)
	{
		if (expired(it->second.lastRefresh, nowMs, m_config.roomTtlMs))
			it = m_rooms.erase(it);
		else
			++it;
	}
	for (auto it = m_relays.begin(); it != m_relays.end();)
	{
		if (expired(it->second.lastUse, nowMs, m_config.relayTtlMs))
			it = m_relays.erase(it);
		else
			++it;
	}
}

} // namespace Engine
