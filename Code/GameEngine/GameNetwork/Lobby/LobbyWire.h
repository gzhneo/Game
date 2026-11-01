/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LobbyWire.h -- between the game's types and the session and rendezvous messages: UTF-8 texts,
// key fingerprints, endpoints, names of listed games, encoding and sending.

#pragma once

#ifndef GAMENETWORK_LOBBY_LOBBYWIRE_H
#define GAMENETWORK_LOBBY_LOBBYWIRE_H

#include "Engine/NetAddress.h"
#include "Engine/PacketSeal.h"
#include "Engine/Rendezvous.h"
#include "Engine/SessionMessage.h"

#include <cstddef>
#include <string>

class Transport;

namespace Lobby
{

/// Context of the lobby keys between the host and a member (both sides pass the same bytes).
inline constexpr char LOBBY_CONTEXT[] = "GZH Neo lobby";

std::string toUtf8( const UnicodeString &text );
/// Malformed sequences become '?'; control characters are dropped (names and chat lines only).
UnicodeString fromUtf8( const std::string &text );
/// A player name from a message: trimmed and cut to the length the slots hold.
UnicodeString playerName( const std::string &utf8 );

/// The 40 bits of sealFingerprint() as bytes (announcements carry them).
void fingerprintBytes( const UnsignedByte key[::Engine::SEAL_KEY_SIZE], UnsignedByte out[5] );
/// sealFingerprint() without the dash, as invitation codes carry it.
std::string fingerprintCode( const UnsignedByte key[::Engine::SEAL_KEY_SIZE] );
/// "ROOM-FINGERPRINT@server[:port]" for a room on the server named as in the server list.
std::string inviteCodeAt( const std::string &room, const std::string &fingerprint, const std::string &serverText );

::Engine::WireEndpoint wireEndpoint( const ::Engine::NetAddress &address );
::Engine::NetAddress netAddress( const ::Engine::WireEndpoint &endpoint );
/// Adds an endpoint to a list of three unless it is there; a full list loses its last entry.
Bool addEndpoint( ::Engine::WireEndpoint (&list)[::Engine::SESSION_MAX_ENDPOINTS], const ::Engine::WireEndpoint &endpoint );

/// Name of a listed game: its session id in hex.
UnicodeString sessionGameName( UnsignedInt64 sessionId );

/// A random non-zero number for tags, nonces and ids (the clock if the system has no randomness).
UnsignedInt randomTag( void );

/// An encoded session message.
struct Encoded
{
	UnsignedByte data[::Engine::MAX_SESSION_MESSAGE];
	Int size = 0;
};

/// False (and logged) if the message breaks a field limit.
[[nodiscard]] Bool encode( const ::Engine::SessionMessage &message, Encoded &out );

/// Pads the request to `padTo` bytes (0: as it is) and sends it raw.
void sendTreff( Transport *transport, ::Engine::TreffMessage &message, const ::Engine::NetAddress &server, std::size_t padTo = 0 );

} // namespace Lobby

#endif // GAMENETWORK_LOBBY_LOBBYWIRE_H
