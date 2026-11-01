/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SessionMessage.h -- the messages of the lobby and session layer.
//
//     'G' 'Z' 'S' 1 | type (1) | body length (2, little endian) | body
//
// Every type has a fixed sequence of fields (Wire.h). decodeSession() refuses a message whose body
// is shorter or longer than its fields, whose length field does not match, whose type is unknown or
// whose texts exceed their limits. Types from SESSION_FIRST_SEALED on belong to a game and are only
// accepted when the transport opened them from a sealed envelope.
//
// No engine header: tested by EngineTests.

#pragma once

#ifndef ENGINE_SESSIONMESSAGE_H
#define ENGINE_SESSIONMESSAGE_H

#include "Engine/Wire.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine
{

enum class SessionType : std::uint8_t
{
	// plain
	QUERY = 1,			///< who hosts a game here?
	ANNOUNCE,			///< a game (LAN list)
	PRESENCE,			///< a player in the LAN lobby
	LOBBY_CHAT,			///< chat outside a game
	BYE,				///< leaves the LAN lobby
	HELLO,				///< join, step 1
	CHALLENGE,			///< join, step 2
	JOIN,				///< join, step 3
	REFUSE,				///< join refused
	PROBE,				///< hole punching and reachability
	PROBE_ACK,

	// sealed
	WELCOME = 32,		///< joined: slot and identity
	STATE,				///< host: game options
	MEMBER,				///< host: key and endpoints of one player
	WISH,				///< player: option change request
	ACCEPT,				///< player: ready
	HAS_MAP,			///< player: map status
	CHAT,				///< chat within a game
	TIMER,				///< host: start countdown
	START,				///< host: start
	LEAVE,				///< leaves or is removed
	PING,
	PONG,
	RELAY,				///< host: relay session for this game
	WATCH,				///< host: a chunk of the running game's replay and a mark (WatchStream.h)
	WATCH_ACK,			///< spectator: every byte below `value` arrived
	RETURN_READY,		///< returning player: caught up with the stream, `value` frames played
	RETURN_AT,			///< host: commands from frame `value` on, `offset` frames counted ahead, packet router `slot`
	RETURN_STATE,		///< host: the game's network state at the return frame
	RETURN_DONE,		///< returning player: plays from frame `value` on
};

constexpr std::uint8_t SESSION_FIRST_SEALED = 32;
constexpr std::size_t SESSION_HEADER_SIZE = 8;
constexpr std::size_t MAX_SESSION_MESSAGE = 1000;	///< sealed, it still fits a transport buffer
constexpr std::size_t SESSION_KEY_SIZE = 32;
constexpr int SESSION_MAX_ENDPOINTS = 3;

// field limits in bytes (UTF-8)
constexpr std::size_t SESSION_MAX_NAME = 48;
constexpr std::size_t SESSION_MAX_GAME_NAME = 64;
constexpr std::size_t SESSION_MAX_MAP = 160;
constexpr std::size_t SESSION_MAX_CHAT = 400;
constexpr std::size_t SESSION_MAX_VERSION = 80;
constexpr std::size_t SESSION_MAX_OPTIONS = 900;
constexpr std::size_t SESSION_MAX_WISH = 120;
constexpr std::size_t SESSION_MAX_SERVER = 96;
constexpr std::size_t SESSION_MAX_SEALED = 600;
constexpr std::size_t SESSION_HELLO_PADDING = 32;	///< a HELLO is no smaller than the CHALLENGE it asks for
constexpr std::size_t SESSION_MAX_WATCH_CHUNK = 896;
/// WELCOME to a spectator instead of a slot number
constexpr std::uint8_t SESSION_SPECTATOR_SLOT = 0xFF;
constexpr int SESSION_SLOTS = 8;
constexpr std::size_t SESSION_TOKEN_SIZE = 16;

bool isKnownSessionType(std::uint8_t type);
inline bool isSealedSessionType(SessionType type) { return static_cast<std::uint8_t>(type) >= SESSION_FIRST_SEALED; }

/// Endpoints of one player: its sockets as seen in its LAN and from the internet.
struct SessionEndpoints
{
	WireEndpoint lobby[SESSION_MAX_ENDPOINTS];
	WireEndpoint game[SESSION_MAX_ENDPOINTS];
};

struct SessionMember
{
	std::uint8_t slot = 0;
	std::uint32_t identity = 0;
	std::uint8_t key[SESSION_KEY_SIZE] = {};
	SessionEndpoints endpoints;
};

/// The network state of a running game at the start of the frame a dropped player comes back in.
struct SessionReturnState
{
	std::uint32_t frame = 0;
	std::uint8_t connected = 0;			///< a bit per slot in the game
	std::uint8_t crcPending = 0;		///< a bit per slot whose next CRC is not compared
	std::uint8_t router = 0;			///< packet router
	std::uint8_t fallback[SESSION_SLOTS] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };	///< 0xFF: none
	std::uint32_t dropFrame[SESSION_SLOTS] = {};	///< players that may still come back: the frame they were dropped in
	std::uint16_t runAhead = 0;
	std::uint16_t frameRate = 0;
};

/// One message; the fields a type does not use keep their defaults.
struct SessionMessage
{
	SessionType type = SessionType::QUERY;

	std::uint32_t protocol = 0;			///< QUERY, ANNOUNCE, HELLO, REFUSE
	std::uint64_t sessionId = 0;		///< ANNOUNCE, HELLO, CHALLENGE, JOIN, PROBE, PROBE_ACK, WELCOME

	// ANNOUNCE
	std::uint32_t programCRC = 0;
	std::uint32_t dataCRC = 0;
	std::uint8_t fingerprint[5] = {};	///< of the host's key
	std::string gameName;
	std::string hostName;
	std::string mapName;
	std::uint8_t players = 0;
	std::uint8_t maxPlayers = 0;
	bool inProgress = false;
	// ANNOUNCE, STATE: game options as text (GameInfoToAsciiString)
	std::string options;

	// PRESENCE, LOBBY_CHAT (name); LOBBY_CHAT, CHAT (kind, text); CHAT, WELCOME (slot); WISH (text)
	std::string name;
	std::uint8_t chatKind = 0;
	std::uint8_t slot = 0;
	std::string text;

	// HELLO, CHALLENGE, JOIN
	std::uint8_t key[SESSION_KEY_SIZE] = {};
	std::uint8_t nonce[8] = {};
	std::uint8_t cookie[16] = {};
	std::vector<std::uint8_t> sealed;	///< JOIN: SessionJoinInfo, sealed (SessionHandshake)

	// REFUSE
	std::uint8_t reason = 0;
	std::string versionText;

	// PROBE, PROBE_ACK, PING, PONG (identity, value); WELCOME (identity); TIMER (value = seconds)
	std::uint32_t identity = 0;
	std::uint32_t value = 0;

	// WELCOME: the secret a player comes back with after a drop; `flag`: welcomed back into the
	// running game
	std::uint8_t token[SESSION_TOKEN_SIZE] = {};

	// RETURN_STATE
	SessionReturnState returnState;

	// STATE
	std::uint32_t revision = 0;


	// MEMBER
	SessionMember member;

	// ACCEPT, HAS_MAP
	bool flag = false;
	std::uint32_t mapCRC = 0;

	// RELAY
	std::string relayServer;			///< host[:port] of the rendezvous server
	std::uint64_t relayId = 0;
	std::uint8_t relayToken[16] = {};

	// WATCH: replay bytes from `offset`; the first markBytes hold every frame below markFrames;
	// `flag`: the host's recording has ended at markBytes
	std::uint32_t offset = 0;
	std::uint32_t markBytes = 0;
	std::uint32_t markFrames = 0;
	std::vector<std::uint8_t> chunk;
};

/// What a joiner asks for (SessionJoinInfo::wants).
enum class SessionWants : std::uint8_t
{
	PLAY = 0,			///< a slot; refused once the game runs
	WATCH = 1,			///< watch the running game
	PLAY_OR_WATCH = 2,	///< a slot before the start, else watch
	RETURN = 3,			///< back into the running game after a drop (returnSlot, returnToken)
};

/// The sealed content of JOIN.
struct SessionJoinInfo
{
	std::string name;
	std::uint32_t protocol = 0;
	std::uint32_t programCRC = 0;
	std::uint32_t dataCRC = 0;
	std::string versionText;
	SessionEndpoints endpoints;
	SessionWants wants = SessionWants::PLAY;
	std::uint8_t returnSlot = 0;
	std::uint8_t returnToken[SESSION_TOKEN_SIZE] = {};
};

/// Writes the message to out; returns its size, 0 if a field breaks its limits or out is too small.
std::size_t encodeSession(const SessionMessage &message, std::uint8_t *out, std::size_t capacity);
bool decodeSession(const std::uint8_t *data, std::size_t size, SessionMessage &out);

/// True if data starts like a session message (magic and version), whatever follows.
bool looksLikeSession(const std::uint8_t *data, std::size_t size);

std::size_t encodeJoinInfo(const SessionJoinInfo &info, std::uint8_t *out, std::size_t capacity);
bool decodeJoinInfo(const std::uint8_t *data, std::size_t size, SessionJoinInfo &out);

} // namespace Engine

#endif // ENGINE_SESSIONMESSAGE_H
