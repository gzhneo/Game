/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SimRandom.h -- randomness for parallel sections.
//
// The global game random (`GameLogicRandomValue`, `theGameLogicSeed`) is state that advances on
// every call. The **order of calls** is therefore part of the game state, which cannot be
// parallelized.
//
// Parallel sections use a counter-based generator instead: the value is **computed** from seed,
// frame, key (e.g. ObjectID) and a salt, not drawn from running state. The same call always
// yields the same result, regardless of which thread runs it, in which order, or how many
// workers run.
//
//     const UnsignedInt v = Engine::SimRandom::value(seed, frame, (unsigned)objectID, SALT_SCAN);
//
// `GameLogicRandomValue` stays in use for serial code; the two streams do not mix.
//
// Method: SplitMix64 over the four inputs. SplitMix64 is the mixing step xoshiro uses to seed
// its state -- cheap (a few multiplications), stateless and well distributed in all bits.

#pragma once

#ifndef ENGINE_SIMRANDOM_H
#define ENGINE_SIMRANDOM_H

#include <cstdint>

namespace Engine
{

/// Salts separate different uses for the same object in the same frame.
/// New uses get a new value; values are never reused.
enum SimRandomSalt
{
	SIM_SALT_GENERIC = 0,
	SIM_SALT_TARGET_SCAN = 1,		///< target scan
	SIM_SALT_PATHFIND = 2,			///< pathfinding
	SIM_SALT_LOCOMOTOR = 3,			///< movement
	SIM_SALT_VISION = 4				///< vision and fog
};

class SimRandom
{
public:
	/// 64-bit mix of a single number (SplitMix64).
	static std::uint64_t mix(std::uint64_t x)
	{
		x += 0x9E3779B97F4A7C15ull;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
		return x ^ (x >> 31);
	}

	/// 64-bit random value from the four inputs. Pure function: no state, thread-safe.
	static std::uint64_t value64(std::uint32_t seed, std::uint32_t frame, std::uint32_t key, std::uint32_t salt)
	{
		std::uint64_t h = mix((static_cast<std::uint64_t>(seed) << 32) ^ static_cast<std::uint64_t>(frame));
		h = mix(h ^ ((static_cast<std::uint64_t>(key) << 32) ^ static_cast<std::uint64_t>(salt)));
		return h;
	}

	/// 32-bit random value.
	static std::uint32_t value(std::uint32_t seed, std::uint32_t frame, std::uint32_t key, std::uint32_t salt)
	{
		return static_cast<std::uint32_t>(value64(seed, frame, key, salt) >> 32);
	}

	/// Integer in [lo, hi] (inclusive). Returns lo if hi <= lo.
	static int valueInRange(int lo, int hi, std::uint32_t seed, std::uint32_t frame, std::uint32_t key, std::uint32_t salt)
	{
		if (hi <= lo)
			return lo;
		// Compute in 64 bits: `hi - lo` overflows int for wide ranges (e.g. lo negative, hi large
		// positive), and signed overflow is undefined behavior.
		const std::uint64_t span = static_cast<std::uint64_t>(static_cast<std::int64_t>(hi) - static_cast<std::int64_t>(lo)) + 1ull;
		// Multiply-shift instead of modulo: no division, and the bias is far below anything the
		// game logic could distinguish.
		const std::uint64_t product = static_cast<std::uint64_t>(value(seed, frame, key, salt)) * span;
		// The offset can exceed INT_MAX; add in 64 bits, the sum lies in [lo, hi].
		return static_cast<int>(static_cast<std::int64_t>(lo) + static_cast<std::int64_t>(product >> 32));
	}

	/// Float in [0, 1). Exactly reproducible (only bit operations and a division by a power of
	/// two -- no rounding differences between runs).
	static float valueUnitFloat(std::uint32_t seed, std::uint32_t frame, std::uint32_t key, std::uint32_t salt)
	{
		return static_cast<float>(value(seed, frame, key, salt) >> 8) * (1.0f / 16777216.0f);	// 24 Bit
	}

	/// A stream of several values for the same site: index 0, 1, 2 ... yields independent
	/// values without depending on call order.
	static std::uint32_t valueAt(std::uint32_t seed, std::uint32_t frame, std::uint32_t key,
		std::uint32_t salt, std::uint32_t index)
	{
		return static_cast<std::uint32_t>(mix(value64(seed, frame, key, salt) ^ (static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ull)) >> 32);
	}
};

} // namespace Engine

#endif // ENGINE_SIMRANDOM_H
