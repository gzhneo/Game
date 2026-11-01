/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RateLimiter.cpp -- see RateLimiter.h.

#include "Engine/RateLimiter.h"

namespace Engine
{

namespace
{
	constexpr std::uint32_t ONE_PACKET = 1000;		// tokens are counted in thousandths
	constexpr std::uint32_t IDLE_MS = 10000;		// an untrusted bucket unused this long may be reused
	constexpr std::uint32_t MAX_ELAPSED_MS = 60000;	// keeps the refill arithmetic small
}

void RateLimiter::configure(const RateLimit &perSender, const RateLimit &shared)
{
	m_perSender = perSender;
	m_shared = shared;
	m_buckets.assign(MAX_TRACKED, Bucket());
	m_sharedBucket = Bucket();
	m_sharedBucket.used = true;
	m_sharedBucket.tokens = shared.burst * ONE_PACKET;
	m_stats = RateLimiterStats();
}

void RateLimiter::refill(Bucket &b, const RateLimit &limit, std::uint32_t nowMs) const
{
	std::uint32_t elapsed = nowMs - b.last;
	if (static_cast<std::int32_t>(elapsed) < 0)
		elapsed = 0;	// clock went backwards
	if (elapsed > MAX_ELAPSED_MS)
		elapsed = MAX_ELAPSED_MS;
	const std::uint64_t capacity = static_cast<std::uint64_t>(limit.burst) * ONE_PACKET;
	std::uint64_t tokens = b.tokens + static_cast<std::uint64_t>(limit.rate) * elapsed;
	if (tokens > capacity)
		tokens = capacity;
	b.tokens = static_cast<std::uint32_t>(tokens);
	b.last = nowMs;
}

bool RateLimiter::take(Bucket &b, const RateLimit &limit, std::uint32_t nowMs)
{
	refill(b, limit, nowMs);
	if (b.tokens < ONE_PACKET)
		return false;
	b.tokens -= ONE_PACKET;
	return true;
}

RateLimiter::Bucket *RateLimiter::find(std::uint32_t sender)
{
	for (Bucket &b : m_buckets)
		if (b.used && b.sender == sender)
			return &b;
	return nullptr;
}

// A free bucket, or the untrusted one idle longest if that is idle long enough. Under a flood of
// new (forged) senders no bucket is idle, so they end up in the shared bucket instead of each
// getting a full budget.
RateLimiter::Bucket *RateLimiter::claim(std::uint32_t sender, std::uint32_t nowMs)
{
	Bucket *choice = nullptr;
	for (Bucket &b : m_buckets)
	{
		if (!b.used)
		{
			choice = &b;
			break;
		}
		if (!b.trusted && nowMs - b.last >= IDLE_MS && (choice == nullptr || nowMs - b.last > nowMs - choice->last))
			choice = &b;
	}
	if (choice == nullptr)
		return nullptr;
	*choice = Bucket();
	choice->used = true;
	choice->sender = sender;
	choice->last = nowMs;
	choice->tokens = m_perSender.burst * ONE_PACKET;
	return choice;
}

void RateLimiter::setTrusted(std::uint32_t sender, bool trusted, std::uint32_t nowMs)
{
	Bucket *b = find(sender);
	if (b == nullptr && trusted)
	{
		b = claim(sender, nowMs);
		if (b == nullptr)
		{
			// every bucket is busy: a player displaces the untrusted sender idle longest
			for (Bucket &c : m_buckets)
				if (!c.trusted && (b == nullptr || nowMs - c.last > nowMs - b->last))
					b = &c;
			if (b == nullptr)
				return;
			*b = Bucket();
			b->used = true;
			b->sender = sender;
			b->last = nowMs;
			b->tokens = m_perSender.burst * ONE_PACKET;
		}
	}
	if (b != nullptr)
		b->trusted = trusted;
}

void RateLimiter::clearTrusted()
{
	for (Bucket &b : m_buckets)
		b.trusted = false;
}

bool RateLimiter::allow(std::uint32_t sender, std::uint32_t nowMs)
{
	if (m_perSender.rate == 0)
		return true;
	Bucket *b = find(sender);
	if (b == nullptr)
		b = claim(sender, nowMs);
	if (b != nullptr)
	{
		if (take(*b, m_perSender, nowMs))
		{
			++m_stats.accepted;
			return true;
		}
		++m_stats.dropped;
		return false;
	}
	if (m_shared.rate != 0 && take(m_sharedBucket, m_shared, nowMs))
	{
		++m_stats.accepted;
		return true;
	}
	++m_stats.droppedShared;
	return false;
}

} // namespace Engine
