/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SessionHandshake.h -- the cryptography of joining a game.
//
// HELLO carries the joiner's fresh X25519 key. The host answers with a cookie it can recompute from
// its secret cookie key, so it keeps no state for joiners that have not proved they receive its
// answers (a forged sender never sees the cookie). JOIN repeats key and cookie and carries the
// joiner's details sealed with a key only the two ends can derive.
//
// No engine header: tested by EngineTests.

#pragma once

#ifndef ENGINE_SESSIONHANDSHAKE_H
#define ENGINE_SESSIONHANDSHAKE_H

#include "Engine/PacketSeal.h"
#include "Engine/Wire.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Engine
{
namespace SessionHandshake
{

constexpr std::size_t COOKIE_SIZE = 16;
constexpr std::size_t JOIN_SEAL_OVERHEAD = 16;	///< Poly1305 tag

/// Cookie for a joiner key and nonce, bound to the game and the sender's endpoint.
void cookie(const std::uint8_t cookieKey[32], std::uint64_t sessionId, const std::uint8_t joinerKey[32],
	const std::uint8_t nonce[8], const WireEndpoint &from, std::uint8_t out[COOKIE_SIZE]);

/// Constant-time comparison.
bool cookieEquals(const std::uint8_t a[COOKIE_SIZE], const std::uint8_t b[COOKIE_SIZE]);

/// Seals `plain` for the host (joiner side). False if the host key gives no secret.
bool sealJoin(const SealKeyPair &joiner, const std::uint8_t hostKey[32], const std::uint8_t nonce[8],
	const std::uint8_t *plain, std::size_t size, std::vector<std::uint8_t> &sealed);

/// Opens what sealJoin() made (host side). False if forged, altered or for another key.
bool openJoin(const SealKeyPair &host, const std::uint8_t joinerKey[32], const std::uint8_t nonce[8],
	const std::uint8_t *sealed, std::size_t size, std::vector<std::uint8_t> &plain);

/// The 32-bit identity a player's key stands for in a game: in 240.0.0.0 to 254.255.255.255, the
/// range no machine has as IPv4 address; the same on every machine.
std::uint32_t identityOf(const std::uint8_t key[32]);

} // namespace SessionHandshake
} // namespace Engine

#endif // ENGINE_SESSIONHANDSHAKE_H
