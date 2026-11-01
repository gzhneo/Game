/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// InviteCode.h -- the code a host passes on to invite players over the internet.
//
//     K7Q2-DP941QG2@meet.example.org:8090
//
// Room (4 characters) on a rendezvous server, fingerprint of the host's key (8 characters, as
// Engine::sealFingerprint without the dash) and optionally the server. Crockford base 32; case is
// ignored, O reads as 0, I and L as 1. The joiner checks the host's key against the fingerprint, so
// a server cannot point it to another host.
//
// No engine header: tested by EngineTests.

#pragma once

#ifndef ENGINE_INVITECODE_H
#define ENGINE_INVITECODE_H

#include "Engine/NetAddress.h"

#include <cstdint>
#include <string>

namespace Engine
{

constexpr std::uint16_t RENDEZVOUS_DEFAULT_PORT = 8090;

struct InviteCode
{
	std::string room;			///< 4 characters, canonical (upper case, 0 and 1)
	std::string fingerprint;	///< 8 characters, canonical
	NetTarget server;			///< Kind::Invalid if the code names none
	std::uint16_t serverPort() const { return server.port != 0 ? server.port : RENDEZVOUS_DEFAULT_PORT; }
};

/// Reads a code; surrounding spaces are ignored, the dash may be missing. False for anything else.
bool parseInviteCode(const std::string &text, InviteCode &out);

/// "ROOM-FINGERPRINT" and, if given, "@server[:port]" (port only if not the default).
std::string formatInviteCode(const std::string &room, const std::string &fingerprint, const std::string &server,
	std::uint16_t port);

/// A room name from 20 random bits.
std::string inviteRoomFromBits(std::uint32_t bits);

/// Canonical form of Crockford base-32 text of the given length; empty if it is none.
std::string canonicalCrockford(const std::string &text, std::size_t length);

} // namespace Engine

#endif // ENGINE_INVITECODE_H
