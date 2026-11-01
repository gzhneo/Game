/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PacketSeal.cpp -- see PacketSeal.h.

#include "Engine/PacketSeal.h"

#include "Monocypher/monocypher.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
	#include <windows.h>
#endif

namespace Engine
{

namespace
{
	const char KDF_LABEL[] = "GZH Neo packet keys v1";

	void putLE64(std::uint8_t *p, std::uint64_t v)
	{
		for (int i = 0; i < 8; ++i)
			p[i] = static_cast<std::uint8_t>(v >> (8 * i));
	}

	std::uint64_t getLE64(const std::uint8_t *p)
	{
		std::uint64_t v = 0;
		for (int i = 0; i < 8; ++i)
			v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
		return v;
	}

	void putLE32(std::uint8_t *p, std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i)
			p[i] = static_cast<std::uint8_t>(v >> (8 * i));
	}

	std::uint32_t getLE32(const std::uint8_t *p)
	{
		return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
			| (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
	}

	void nonceFor(std::uint64_t counter, std::uint8_t nonce[24])
	{
		std::memset(nonce, 0, 24);
		putLE64(nonce, counter);
	}

	void directionKey(const std::uint8_t shared[32], const std::uint8_t first[32], const std::uint8_t second[32],
		const void *context, std::size_t contextSize, std::uint8_t out[SEAL_KEY_SIZE])
	{
		crypto_blake2b_ctx ctx;
		crypto_blake2b_init(&ctx, SEAL_KEY_SIZE);
		crypto_blake2b_update(&ctx, reinterpret_cast<const std::uint8_t *>(KDF_LABEL), sizeof(KDF_LABEL));
		crypto_blake2b_update(&ctx, shared, 32);
		crypto_blake2b_update(&ctx, first, 32);
		crypto_blake2b_update(&ctx, second, 32);
		std::uint8_t length[8];
		putLE64(length, contextSize);
		crypto_blake2b_update(&ctx, length, sizeof(length));
		if (contextSize != 0)
			crypto_blake2b_update(&ctx, static_cast<const std::uint8_t *>(context), contextSize);
		crypto_blake2b_final(&ctx, out);
	}
}

namespace
{
	const char CROCKFORD[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
}

void sealFingerprint(const std::uint8_t publicKey[SEAL_KEY_SIZE], char out[10])
{
	std::uint8_t hash[5];
	crypto_blake2b(hash, sizeof(hash), publicKey, SEAL_KEY_SIZE);
	std::uint64_t bits = 0;
	for (int i = 0; i < 5; ++i)
		bits = (bits << 8) | hash[i];
	int o = 0;
	for (int i = 0; i < 8; ++i)
	{
		if (i == 4)
			out[o++] = '-';
		out[o++] = CROCKFORD[(bits >> (35 - 5 * i)) & 31];
	}
	out[o] = 0;
}

bool sealFingerprintMatches(const char *typed, const std::uint8_t publicKey[SEAL_KEY_SIZE])
{
	if (typed == nullptr)
		return false;
	char expected[10];
	sealFingerprint(publicKey, expected);
	int e = 0;
	for (const char *p = typed; *p != 0; ++p)
	{
		char c = *p;
		if (c == '-' || c == ' ')
			continue;
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
		if (c == 'O')
			c = '0';
		if (c == 'I' || c == 'L')
			c = '1';
		if (expected[e] == '-')
			++e;
		if (e >= 9 || expected[e] != c)
			return false;
		++e;
	}
	return e == 9;
}

bool isSealFingerprintText(const char *typed)
{
	if (typed == nullptr)
		return false;
	int count = 0;
	for (const char *p = typed; *p != 0; ++p)
	{
		char c = *p;
		if (c == '-' || c == ' ')
			continue;
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
		if (c == 'O' || c == 'I' || c == 'L')
		{
			++count;
			continue;
		}
		bool known = false;
		for (const char *k = CROCKFORD; *k != 0 && !known; ++k)
			known = *k == c;
		if (!known)
			return false;
		++count;
	}
	return count == 8;
}

bool secureRandom(void *buffer, std::size_t size)
{
	if (size == 0)
		return true;
	if (buffer == nullptr)
		return false;
#if defined(_WIN32)
	// Loaded at run time so no target needs an extra import library.
	using GenRandom = LONG (WINAPI *)(void *, unsigned char *, ULONG, ULONG);
	static GenRandom s_genRandom = []() -> GenRandom {
		HMODULE bcrypt = ::LoadLibraryA("bcrypt.dll");
		return bcrypt ? reinterpret_cast<GenRandom>(reinterpret_cast<void *>(::GetProcAddress(bcrypt, "BCryptGenRandom"))) : nullptr;
	}();
	const ULONG BCRYPT_USE_SYSTEM_PREFERRED_RNG_FLAG = 0x00000002;
	if (s_genRandom == nullptr)
		return false;
	unsigned char *p = static_cast<unsigned char *>(buffer);
	while (size > 0)
	{
		const ULONG chunk = size > 0x10000000u ? 0x10000000u : static_cast<ULONG>(size);
		if (s_genRandom(nullptr, p, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG_FLAG) != 0)
			return false;
		p += chunk;
		size -= chunk;
	}
	return true;
#else
	std::FILE *f = std::fopen("/dev/urandom", "rb");
	if (f == nullptr)
		return false;
	const bool ok = std::fread(buffer, 1, size, f) == size;
	std::fclose(f);
	return ok;
#endif
}

bool SealKeyPair::generate()
{
	if (!secureRandom(secretKey, sizeof(secretKey)))
	{
		wipe();
		return false;
	}
	crypto_x25519_public_key(publicKey, secretKey);
	return true;
}

void SealKeyPair::fromSecret(const std::uint8_t secret[SEAL_KEY_SIZE])
{
	std::memcpy(secretKey, secret, SEAL_KEY_SIZE);
	crypto_x25519_public_key(publicKey, secretKey);
}

void SealKeyPair::wipe()
{
	crypto_wipe(secretKey, sizeof(secretKey));
	crypto_wipe(publicKey, sizeof(publicKey));
}

void SealSessionKeys::wipe()
{
	crypto_wipe(send, sizeof(send));
	crypto_wipe(receive, sizeof(receive));
}

bool deriveSessionKeys(const SealKeyPair &local, const std::uint8_t peerPublicKey[SEAL_KEY_SIZE],
	const void *context, std::size_t contextSize, SealSessionKeys &out)
{
	std::uint8_t shared[32];
	crypto_x25519(shared, local.secretKey, peerPublicKey);
	static const std::uint8_t zero[32] = {};
	if (crypto_verify32(shared, zero) == 0)
	{
		crypto_wipe(shared, sizeof(shared));
		out.wipe();
		return false;
	}
	directionKey(shared, local.publicKey, peerPublicKey, context, contextSize, out.send);
	directionKey(shared, peerPublicKey, local.publicKey, context, contextSize, out.receive);
	crypto_wipe(shared, sizeof(shared));
	return true;
}

void SealedChannel::setKeys(const SealSessionKeys &keys)
{
	clear();
	std::memcpy(m_sendKey, keys.send, SEAL_KEY_SIZE);
	std::memcpy(m_receiveKey, keys.receive, SEAL_KEY_SIZE);
	m_hasKeys = true;
}

void SealedChannel::clear()
{
	crypto_wipe(m_sendKey, sizeof(m_sendKey));
	crypto_wipe(m_receiveKey, sizeof(m_receiveKey));
	m_hasKeys = false;
	m_sendCounter = 0;
	m_receiveHighest = 0;
	m_receiveWindow = 0;
	m_stats = SealStats();
}

std::size_t SealedChannel::seal(const void *plain, std::size_t size, void *out)
{
	if (!m_hasKeys || out == nullptr || (plain == nullptr && size != 0) || m_sendCounter == ~0ull)
		return 0;
	const std::uint64_t counter = ++m_sendCounter;
	std::uint8_t *w = static_cast<std::uint8_t *>(out);
	putLE32(w, SEAL_MAGIC);
	putLE64(w + 4, counter);
	std::uint8_t nonce[24];
	nonceFor(counter, nonce);
	crypto_aead_lock(w + SEAL_HEADER_SIZE, w + SEAL_HEADER_SIZE + size, m_sendKey, nonce,
		w, SEAL_HEADER_SIZE, static_cast<const std::uint8_t *>(plain), size);
	++m_stats.sealed;
	return size + SEAL_OVERHEAD;
}

bool SealedChannel::acceptCounter(std::uint64_t counter) const
{
	if (counter == 0)
		return false;
	if (counter > m_receiveHighest)
		return true;
	const std::uint64_t age = m_receiveHighest - counter;
	if (age >= 64)
		return false;
	return (m_receiveWindow & (1ull << age)) == 0;
}

void SealedChannel::markCounter(std::uint64_t counter)
{
	if (counter > m_receiveHighest)
	{
		const std::uint64_t shift = counter - m_receiveHighest;
		m_receiveWindow = shift >= 64 ? 0 : (m_receiveWindow << shift);
		m_receiveWindow |= 1;
		m_receiveHighest = counter;
	}
	else
	{
		m_receiveWindow |= 1ull << (m_receiveHighest - counter);
	}
}

long SealedChannel::open(const void *wire, std::size_t size, void *plainOut, Rejects rejects)
{
	const bool count = rejects == Rejects::Count;
	if (!m_hasKeys || wire == nullptr || plainOut == nullptr || size < SEAL_OVERHEAD || getLE32(static_cast<const std::uint8_t *>(wire)) != SEAL_MAGIC)
	{
		m_stats.rejectedFormat += count;
		return -1;
	}
	const std::uint8_t *w = static_cast<const std::uint8_t *>(wire);
	const std::uint64_t counter = getLE64(w + 4);
	if (!acceptCounter(counter))
	{
		m_stats.rejectedReplay += count;
		return -1;
	}
	// The header is authenticated data; keep a copy in case plainOut overlaps it.
	std::uint8_t header[SEAL_HEADER_SIZE];
	std::memcpy(header, w, SEAL_HEADER_SIZE);
	std::uint8_t tag[SEAL_TAG_SIZE];
	const std::size_t plainSize = size - SEAL_OVERHEAD;
	std::memcpy(tag, w + SEAL_HEADER_SIZE + plainSize, SEAL_TAG_SIZE);
	std::uint8_t nonce[24];
	nonceFor(counter, nonce);
	// Monocypher decrypts in place when cipher and plain text are the same buffer; a shifted overlap
	// (plainOut == wire) is moved first.
	std::uint8_t *p = static_cast<std::uint8_t *>(plainOut);
	std::memmove(p, w + SEAL_HEADER_SIZE, plainSize);
	if (crypto_aead_unlock(p, tag, m_receiveKey, nonce, header, SEAL_HEADER_SIZE, p, plainSize) != 0)
	{
		m_stats.rejectedTag += count;
		return -1;
	}
	markCounter(counter);
	++m_stats.opened;
	return static_cast<long>(plainSize);
}

} // namespace Engine
