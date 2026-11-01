/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SessionHandshake.cpp -- see SessionHandshake.h.

#include "Engine/SessionHandshake.h"

#include "Monocypher/monocypher.h"

#include <cstring>

namespace Engine
{
namespace SessionHandshake
{

namespace
{
	const char JOIN_LABEL[] = "GZH Neo join";
	const char IDENTITY_LABEL[] = "GZH Neo identity";

	void putLE64(std::uint8_t *p, std::uint64_t v)
	{
		for (int i = 0; i < 8; ++i)
			p[i] = static_cast<std::uint8_t>(v >> (8 * i));
	}

	/// Key of the sealed JOIN block: hash of the shared secret and both public keys.
	bool joinKey(const SealKeyPair &own, const std::uint8_t peerKey[32], const std::uint8_t joinerKey[32],
		const std::uint8_t hostKey[32], std::uint8_t out[32])
	{
		std::uint8_t shared[32];
		crypto_x25519(shared, own.secretKey, peerKey);
		static const std::uint8_t zero[32] = {};
		if (crypto_verify32(shared, zero) == 0)
		{
			crypto_wipe(shared, sizeof(shared));
			return false;
		}
		crypto_blake2b_ctx ctx;
		crypto_blake2b_init(&ctx, 32);
		crypto_blake2b_update(&ctx, reinterpret_cast<const std::uint8_t *>(JOIN_LABEL), sizeof(JOIN_LABEL));
		crypto_blake2b_update(&ctx, shared, 32);
		crypto_blake2b_update(&ctx, joinerKey, 32);
		crypto_blake2b_update(&ctx, hostKey, 32);
		crypto_blake2b_final(&ctx, out);
		crypto_wipe(shared, sizeof(shared));
		return true;
	}

	void joinNonce(const std::uint8_t nonce[8], std::uint8_t out[24])
	{
		std::memset(out, 0, 24);
		std::memcpy(out, nonce, 8);
	}
}

void cookie(const std::uint8_t cookieKey[32], std::uint64_t sessionId, const std::uint8_t joinerKey[32],
	const std::uint8_t nonce[8], const WireEndpoint &from, std::uint8_t out[COOKIE_SIZE])
{
	std::uint8_t id[8];
	putLE64(id, sessionId);
	std::uint8_t endpoint[19];
	endpoint[0] = from.family;
	std::memcpy(endpoint + 1, from.address, 16);
	endpoint[17] = static_cast<std::uint8_t>(from.port);
	endpoint[18] = static_cast<std::uint8_t>(from.port >> 8);
	crypto_blake2b_ctx ctx;
	crypto_blake2b_keyed_init(&ctx, COOKIE_SIZE, cookieKey, 32);
	crypto_blake2b_update(&ctx, id, sizeof(id));
	crypto_blake2b_update(&ctx, joinerKey, 32);
	crypto_blake2b_update(&ctx, nonce, 8);
	crypto_blake2b_update(&ctx, endpoint, sizeof(endpoint));
	crypto_blake2b_final(&ctx, out);
}

bool cookieEquals(const std::uint8_t a[COOKIE_SIZE], const std::uint8_t b[COOKIE_SIZE])
{
	return crypto_verify16(a, b) == 0;
}

bool sealJoin(const SealKeyPair &joiner, const std::uint8_t hostKey[32], const std::uint8_t nonce[8],
	const std::uint8_t *plain, std::size_t size, std::vector<std::uint8_t> &sealed)
{
	if (plain == nullptr && size != 0)
		return false;
	std::uint8_t key[32];
	if (!joinKey(joiner, hostKey, joiner.publicKey, hostKey, key))
		return false;
	std::uint8_t fullNonce[24];
	joinNonce(nonce, fullNonce);
	sealed.assign(JOIN_SEAL_OVERHEAD + size, 0);
	crypto_aead_lock(sealed.data() + JOIN_SEAL_OVERHEAD, sealed.data(), key, fullNonce, nullptr, 0, plain, size);
	crypto_wipe(key, sizeof(key));
	return true;
}

bool openJoin(const SealKeyPair &host, const std::uint8_t joinerKey[32], const std::uint8_t nonce[8],
	const std::uint8_t *sealed, std::size_t size, std::vector<std::uint8_t> &plain)
{
	if (sealed == nullptr || size < JOIN_SEAL_OVERHEAD)
		return false;
	std::uint8_t key[32];
	if (!joinKey(host, joinerKey, joinerKey, host.publicKey, key))
		return false;
	std::uint8_t fullNonce[24];
	joinNonce(nonce, fullNonce);
	plain.assign(size - JOIN_SEAL_OVERHEAD, 0);
	const int result = crypto_aead_unlock(plain.data(), sealed, key, fullNonce, nullptr, 0,
		sealed + JOIN_SEAL_OVERHEAD, size - JOIN_SEAL_OVERHEAD);
	crypto_wipe(key, sizeof(key));
	if (result != 0)
	{
		plain.clear();
		return false;
	}
	return true;
}

std::uint32_t identityOf(const std::uint8_t key[32])
{
	std::uint8_t hash[4];
	crypto_blake2b_ctx ctx;
	crypto_blake2b_init(&ctx, sizeof(hash));
	crypto_blake2b_update(&ctx, reinterpret_cast<const std::uint8_t *>(IDENTITY_LABEL), sizeof(IDENTITY_LABEL));
	crypto_blake2b_update(&ctx, key, 32);
	crypto_blake2b_final(&ctx, hash);
	const std::uint32_t h = (static_cast<std::uint32_t>(hash[0]) << 24) | (static_cast<std::uint32_t>(hash[1]) << 16)
		| (static_cast<std::uint32_t>(hash[2]) << 8) | static_cast<std::uint32_t>(hash[3]);
	// 0xF0000000 .. 0xFEFFFFFF: 15 * 2^24 values
	return 0xF0000000u + (h % 0x0F000000u);
}

} // namespace SessionHandshake
} // namespace Engine
