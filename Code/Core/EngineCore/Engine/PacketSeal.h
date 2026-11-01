/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PacketSeal.h -- key exchange and authenticated encryption of network packets.
//
// Every player makes a fresh X25519 key pair when creating or joining a game and announces the
// public key in the lobby. At game start each pair of players derives two keys from the shared
// secret, one per direction; A's send key is B's receive key. A packet is sealed with
// XChaCha20-Poly1305 (Monocypher):
//
//     magic (4) | counter (8, little endian) | cipher text | tag (16)
//
// The counter is the nonce and grows by one per packet, the magic and counter are authenticated as
// additional data. The receiver rejects packets whose tag does not verify (forged, altered, from a
// third party, other protocol version) and packets it has already accepted (replay window of 64).
//
// No engine header, no game code: tested by EngineTests.

#pragma once

#ifndef ENGINE_PACKETSEAL_H
#define ENGINE_PACKETSEAL_H

#include <cstddef>
#include <cstdint>

namespace Engine
{

constexpr std::size_t SEAL_KEY_SIZE = 32;
constexpr std::size_t SEAL_HEADER_SIZE = 12;		///< magic and counter
constexpr std::size_t SEAL_TAG_SIZE = 16;
constexpr std::size_t SEAL_OVERHEAD = SEAL_HEADER_SIZE + SEAL_TAG_SIZE;
constexpr std::uint32_t SEAL_MAGIC = 0x314E5A47u;	///< "GZN1" in memory; changes with the protocol

/// Cryptographically secure random bytes from the operating system. False if none are available.
bool secureRandom(void *buffer, std::size_t size);

struct SealKeyPair
{
	std::uint8_t secretKey[SEAL_KEY_SIZE] = {};
	std::uint8_t publicKey[SEAL_KEY_SIZE] = {};

	/// New key pair from secureRandom(). False (and all zero) if no randomness is available.
	bool generate();
	/// Key pair of a given secret (tests and tools that need the same keys every run).
	void fromSecret(const std::uint8_t secret[SEAL_KEY_SIZE]);
	void wipe();
};

struct SealSessionKeys
{
	std::uint8_t send[SEAL_KEY_SIZE] = {};
	std::uint8_t receive[SEAL_KEY_SIZE] = {};

	void wipe();
};

/// Keys between the local player and one peer. `context` binds them to one game (for example the
/// game seed and both slots); both sides must pass the same bytes. False for a peer key that gives
/// no secret (low-order point).
bool deriveSessionKeys(const SealKeyPair &local, const std::uint8_t peerPublicKey[SEAL_KEY_SIZE],
	const void *context, std::size_t contextSize, SealSessionKeys &out);

/// Short code of a public key for players to compare or pass on ("K7Q2-9XPA"): 40 bits of its
/// BLAKE2b hash in Crockford base 32. A key pair for a given code takes about 2^40 tries, far too
/// many for an attacker during a join (keys are new for every game).
void sealFingerprint(const std::uint8_t publicKey[SEAL_KEY_SIZE], char out[10]);

/// True if `typed` is the code of the key; case, '-' and spaces are ignored, O reads as 0 and I, L
/// as 1. False for text that is no complete code.
bool sealFingerprintMatches(const char *typed, const std::uint8_t publicKey[SEAL_KEY_SIZE]);

/// True if `typed` has the form of such a code (eight characters of Crockford base 32, '-' and
/// spaces ignored).
bool isSealFingerprintText(const char *typed);

struct SealStats
{
	std::uint64_t sealed = 0;
	std::uint64_t opened = 0;
	std::uint64_t rejectedFormat = 0;	///< too short or wrong magic
	std::uint64_t rejectedTag = 0;		///< forged, altered or wrong key
	std::uint64_t rejectedReplay = 0;
};

/// One direction pair with one peer: seals outgoing packets, opens incoming ones.
class SealedChannel
{
public:
	SealedChannel() = default;
	~SealedChannel() { clear(); }
	SealedChannel(const SealedChannel &) = delete;
	SealedChannel &operator=(const SealedChannel &) = delete;

	void setKeys(const SealSessionKeys &keys);
	void clear();
	bool hasKeys() const { return m_hasKeys; }

	/// Writes size + SEAL_OVERHEAD bytes to out (must not overlap plain). Returns that size, 0
	/// without keys or when the counter is exhausted.
	std::size_t seal(const void *plain, std::size_t size, void *out);

	/// Whether a rejected packet counts in stats(): not when a receiver tries the keys of several
	/// peers to find who sent a packet from an unknown endpoint.
	enum class Rejects { Count, Ignore };

	/// Writes size - SEAL_OVERHEAD bytes to plainOut (may be the same buffer as wire). Returns
	/// that size, or -1 if the packet is rejected; plainOut is then unspecified.
	long open(const void *wire, std::size_t size, void *plainOut, Rejects rejects = Rejects::Count);

	const SealStats &stats() const { return m_stats; }

private:
	bool acceptCounter(std::uint64_t counter) const;
	void markCounter(std::uint64_t counter);

	std::uint8_t m_sendKey[SEAL_KEY_SIZE] = {};
	std::uint8_t m_receiveKey[SEAL_KEY_SIZE] = {};
	bool m_hasKeys = false;
	std::uint64_t m_sendCounter = 0;
	std::uint64_t m_receiveHighest = 0;
	std::uint64_t m_receiveWindow = 0;		///< bit i: counter m_receiveHighest - i accepted
	SealStats m_stats;
};

} // namespace Engine

#endif // ENGINE_PACKETSEAL_H
