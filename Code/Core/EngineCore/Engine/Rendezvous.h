/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Rendezvous.h -- messages between players and a rendezvous server ("Treffpunkt").
//
//     'G' 'Z' 'T' 1 | type (1) | reserved (1) | body length (2) | body
//
// A server tells clients how the internet sees them (OBSERVE), keeps rooms that hosts register
// (REGISTER) and hands their endpoints to joiners (LOOKUP), telling the host about the joiner at
// the same time (KNOCK), lists public rooms (LIST), names other servers (SERVERS) and relays
// packets between players that cannot reach each other (RELAY_*, and frames 'G' 'Z' 'R' 1).
// Requests a server answers without knowing the sender are at least as large as the answer: the
// client pads them, so a forged sender cannot use a server to multiply traffic.
//
// No engine header: tested by EngineTests.

#pragma once

#ifndef ENGINE_RENDEZVOUS_H
#define ENGINE_RENDEZVOUS_H

#include "Engine/Wire.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine
{

enum class TreffType : std::uint8_t
{
	OBSERVE = 1,
	OBSERVED,
	REGISTER,
	REGISTER_ACK,
	UNREGISTER,
	LOOKUP,
	FOUND,
	KNOCK,
	LIST,
	GAMES,
	SERVERS,
	SERVER_LIST,
	RELAY_OPEN,
	RELAY_OPENED,
	RELAY_BIND,
	RELAY_BOUND,
	REFUSED,
};

enum class TreffRefusal : std::uint8_t
{
	NONE = 0,
	ROOM_TAKEN,
	NO_ROOM,			///< unknown room or wrong owner secret
	LIMIT,				///< server full or too many rooms from this address
	RELAY_OFF,			///< this server does not relay
	BAD_TOKEN,
};

constexpr std::size_t TREFF_HEADER_SIZE = 8;
constexpr std::size_t MAX_TREFF_MESSAGE = 1200;
constexpr int TREFF_MAX_ENDPOINTS = 3;
constexpr int TREFF_MAX_GAMES = 16;
constexpr int TREFF_MAX_SERVERS = 16;
constexpr int TREFF_MAX_RELAY_MEMBERS = 8;
constexpr std::size_t TREFF_MAX_SERVER_TEXT = 96;
constexpr std::size_t TREFF_MAX_PADDING = 1100;

/// Smallest request sizes (the client pads): each is at least as large as the answer.
constexpr std::size_t TREFF_OBSERVE_SIZE = 64;
constexpr std::size_t TREFF_LOOKUP_SIZE = 256;
constexpr std::size_t TREFF_LIST_SIZE = 1100;

enum class TreffSocket : std::uint8_t { LOBBY = 0, GAME = 1 };

struct TreffGame
{
	std::string room;
	std::string gameName;
	std::string hostName;
	std::string mapName;
	std::string fingerprint;			///< of the host's key (8 characters, as in an invite code)
	std::uint8_t players = 0;
	std::uint8_t maxPlayers = 0;
	std::uint32_t programCRC = 0;
	std::uint32_t dataCRC = 0;
};

struct TreffRelayMember
{
	std::uint32_t identity = 0;
	std::uint8_t tokenHash[16] = {};
};

struct TreffMessage
{
	TreffType type = TreffType::OBSERVE;
	std::uint32_t tag = 0;				///< chosen by the client, echoed in the answer
	std::uint32_t protocol = 0;			///< REGISTER, LOOKUP

	std::string room;
	std::uint8_t owner[16] = {};		///< REGISTER, UNREGISTER, RELAY_OPEN: the host's secret for its room
	TreffSocket socket = TreffSocket::LOBBY;
	bool isPublic = false;
	TreffGame game;						///< REGISTER (lobby socket) summary for LIST

	WireEndpoint observed;				///< OBSERVED, REGISTER_ACK, RELAY_BOUND
	WireEndpoint lobby[TREFF_MAX_ENDPOINTS];	///< REGISTER, LOOKUP: reported; FOUND, KNOCK: of the other side
	WireEndpoint gameSocket[TREFF_MAX_ENDPOINTS];

	std::vector<TreffGame> games;		///< GAMES
	std::vector<std::string> servers;	///< SERVER_LIST: "host[:port]"

	std::uint64_t relayId = 0;
	std::vector<TreffRelayMember> relayMembers;	///< RELAY_OPEN
	std::uint32_t identity = 0;			///< RELAY_BIND
	std::uint8_t token[16] = {};		///< RELAY_BIND

	TreffRefusal reason = TreffRefusal::NONE;
	std::uint16_t padding = 0;			///< requests: zero bytes to reach the minimum size
};

std::size_t encodeTreff(const TreffMessage &message, std::uint8_t *out, std::size_t capacity);
bool decodeTreff(const std::uint8_t *data, std::size_t size, TreffMessage &out);
bool looksLikeTreff(const std::uint8_t *data, std::size_t size);

/// Sets `padding` so that the encoded request reaches at least minSize.
void padTreffRequest(TreffMessage &message, std::size_t minSize);

/// Hash of a relay token as the host announces it to the server.
void relayTokenHash(const std::uint8_t token[16], std::uint8_t out[16]);

// --- relay frames -------------------------------------------------------------------------------

constexpr std::size_t RELAY_HEADER_SIZE = 20;	///< magic 4, relay id 8, from 4, to 4

struct RelayHeader
{
	std::uint64_t relayId = 0;
	std::uint32_t from = 0;		///< identity of the sender
	std::uint32_t to = 0;		///< identity of the receiver
};

bool looksLikeRelayFrame(const std::uint8_t *data, std::size_t size);
void writeRelayHeader(const RelayHeader &header, std::uint8_t out[RELAY_HEADER_SIZE]);
bool readRelayHeader(const std::uint8_t *data, std::size_t size, RelayHeader &out);

} // namespace Engine

#endif // ENGINE_RENDEZVOUS_H
