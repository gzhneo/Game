/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RendezvousServer.h -- what a rendezvous server does with each datagram.
//
// No sockets and no clock of its own: the program around it (Code/Tools/Treffpunkt) hands in each
// received datagram with its sender and the time and sends what comes out. Rooms and relay sessions
// live in memory only and expire. See Rendezvous.h for the messages.
//
// No engine header: tested by EngineTests.

#pragma once

#ifndef ENGINE_RENDEZVOUSSERVER_H
#define ENGINE_RENDEZVOUSSERVER_H

#include "Engine/RateLimiter.h"
#include "Engine/Rendezvous.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Engine
{

class RendezvousServer
{
public:
	struct Config
	{
		std::size_t maxRooms = 4096;
		int roomsPerAddress = 4;
		std::uint32_t roomTtlMs = 60000;			///< without a refresh
		bool relay = true;
		std::size_t maxRelays = 512;
		std::uint32_t relayTtlMs = 120000;			///< without traffic or refresh
		std::uint32_t relayBytesPerSecond = 128 * 1024;		///< per relay session
		std::uint64_t relayTotalBytesPerSecond = 16ull * 1024 * 1024;
		std::uint32_t knocksPerRoomPerSecond = 4;
		RateLimit perSender{ 60, 120 };				///< control messages per address
		RateLimit shared{ 600, 1200 };
		std::vector<std::string> peers;				///< other servers, "host[:port]"
	};

	struct Datagram
	{
		WireEndpoint to;
		std::vector<std::uint8_t> data;
	};

	struct Stats
	{
		std::uint64_t received = 0;
		std::uint64_t answered = 0;
		std::uint64_t malformed = 0;		///< neither a valid message nor a relay frame
		std::uint64_t limited = 0;			///< over a sender's budget or too small for its answer
		std::uint64_t relayed = 0;
		std::uint64_t relayedBytes = 0;
		std::uint64_t relayDropped = 0;
	};

	RendezvousServer() { configure(Config()); }

	void configure(const Config &config);
	const Config &config() const { return m_config; }

	/// One datagram from `from` at nowMs (wrapping millisecond clock); answers go to `out`.
	void handle(const WireEndpoint &from, const std::uint8_t *data, std::size_t size, std::uint32_t nowMs,
		std::vector<Datagram> &out);

	/// Drops expired rooms and relay sessions; call about once a second.
	void expire(std::uint32_t nowMs);

	std::size_t roomCount() const { return m_rooms.size(); }
	std::size_t relayCount() const { return m_relays.size(); }
	const Stats &stats() const { return m_stats; }

private:
	struct Room
	{
		std::uint8_t owner[16] = {};
		WireEndpoint hostLobby;
		WireEndpoint hostGame;
		WireEndpoint reportedLobby[TREFF_MAX_ENDPOINTS];
		WireEndpoint reportedGame[TREFF_MAX_ENDPOINTS];
		bool isPublic = false;
		TreffGame game;
		std::uint32_t lastRefresh = 0;
		std::uint32_t addressKey = 0;
		std::uint32_t knockWindow = 0;
		std::uint32_t knocks = 0;
	};

	struct Binding
	{
		std::uint32_t identity = 0;
		std::uint8_t tokenHash[16] = {};
		WireEndpoint lobby;
		WireEndpoint game;
	};

	struct Relay
	{
		std::string room;
		std::vector<Binding> members;
		std::uint32_t lastUse = 0;
		std::uint32_t window = 0;
		std::uint64_t windowBytes = 0;
	};

	void send(std::vector<Datagram> &out, const WireEndpoint &to, const TreffMessage &message, std::size_t sizeLimit);
	void refuse(std::vector<Datagram> &out, const WireEndpoint &to, std::uint32_t tag, TreffRefusal reason,
		const std::string &room);
	void handleMessage(const WireEndpoint &from, const TreffMessage &m, std::size_t size, std::uint32_t nowMs,
		std::vector<Datagram> &out);
	void handleRelayFrame(const WireEndpoint &from, const std::uint8_t *data, std::size_t size, std::uint32_t nowMs,
		std::vector<Datagram> &out);
	int roomsOf(std::uint32_t addressKey) const;

	Config m_config;
	std::map<std::string, Room> m_rooms;
	std::map<std::uint64_t, Relay> m_relays;
	RateLimiter m_limiter;
	std::uint32_t m_totalWindow = 0;
	std::uint64_t m_totalWindowBytes = 0;
	Stats m_stats;
};

/// 32-bit key of an address for budgets: the IPv4 address, or a hash of the /64 of an IPv6 one.
std::uint32_t rendezvousAddressKey(const WireEndpoint &endpoint);

} // namespace Engine

#endif // ENGINE_RENDEZVOUSSERVER_H
