/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RateLimiter.h -- limits how many packets each sender may deliver.
//
// Every sender has a token bucket: `rate` packets per second on average, up to `burst` at once. A
// sender over its budget loses the excess packets, so one flooding machine cannot crowd out the
// others in the transport's small receive buffer. Senders without a bucket of their own (more
// than the table holds, as with forged source addresses) share one common bucket; trusted
// senders (the players of the game) always keep their own.
//
// No engine header, no sockets: tested by EngineTests.

#pragma once

#ifndef ENGINE_RATELIMITER_H
#define ENGINE_RATELIMITER_H

#include <cstdint>
#include <vector>

namespace Engine
{

struct RateLimit
{
	std::uint32_t rate = 0;		///< packets per second on average; 0 turns the limit off
	std::uint32_t burst = 0;	///< packets at once
};

struct RateLimiterStats
{
	std::uint64_t accepted = 0;
	std::uint64_t dropped = 0;			///< over the sender's own budget
	std::uint64_t droppedShared = 0;	///< over the common budget of untracked senders
};

class RateLimiter
{
public:
	/// Senders with a bucket of their own; beyond that they share the common one.
	static constexpr std::size_t MAX_TRACKED = 64;

	void configure(const RateLimit &perSender, const RateLimit &shared);

	/// Players of the game: an own bucket that is never given to another sender.
	void setTrusted(std::uint32_t sender, bool trusted, std::uint32_t nowMs);
	void clearTrusted();

	/// Takes one packet of `sender` at time nowMs (wrapping millisecond clock). False: drop it.
	bool allow(std::uint32_t sender, std::uint32_t nowMs);

	const RateLimiterStats &stats() const { return m_stats; }

private:
	struct Bucket
	{
		std::uint32_t sender = 0;
		std::uint32_t last = 0;		///< time of the last refill
		std::uint32_t tokens = 0;	///< in thousandths of a packet
		bool trusted = false;
		bool used = false;
	};

	void refill(Bucket &b, const RateLimit &limit, std::uint32_t nowMs) const;
	bool take(Bucket &b, const RateLimit &limit, std::uint32_t nowMs);
	Bucket *find(std::uint32_t sender);
	Bucket *claim(std::uint32_t sender, std::uint32_t nowMs);

	RateLimit m_perSender;
	RateLimit m_shared;
	std::vector<Bucket> m_buckets;
	Bucket m_sharedBucket;
	RateLimiterStats m_stats;
};

} // namespace Engine

#endif // ENGINE_RATELIMITER_H
