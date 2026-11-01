/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// NetEmulator.cpp -- see NetEmulator.h.

#include "Engine/NetEmulator.h"
#include "Engine/SimRandom.h"

#include <algorithm>
#include <cstring>

namespace Engine
{

namespace
{
	constexpr std::uint32_t MAX_DELAY_MS = 10000;

	// Signed distance handles the wrap of the millisecond clock.
	bool isDue(std::uint32_t due, std::uint32_t now)
	{
		return static_cast<std::int32_t>(now - due) >= 0;
	}

	bool dueBefore(std::uint32_t a, std::uint32_t b)
	{
		return static_cast<std::int32_t>(a - b) < 0;
	}
}

void NetEmulator::configure(const NetEmulatorSettings &settings)
{
	m_settings = settings;
	m_settings.latencyMs = std::min(m_settings.latencyMs, MAX_DELAY_MS);
	m_settings.jitterMs = std::min(m_settings.jitterMs, MAX_DELAY_MS);
	m_settings.lossPercent = std::min(m_settings.lossPercent, 100u);
	m_settings.duplicatePercent = std::min(m_settings.duplicatePercent, 100u);
	m_settings.reorderPercent = std::min(m_settings.reorderPercent, 100u);
	m_counter = 0;
	m_stats = NetEmulatorStats();
	clear();
}

void NetEmulator::clear()
{
	m_queue.clear();
	m_order = 0;
}

std::uint32_t NetEmulator::nextRandom()
{
	return static_cast<std::uint32_t>(SimRandom::mix((static_cast<std::uint64_t>(m_settings.seed) << 32) ^ m_counter++) >> 32);
}

bool NetEmulator::chance(std::uint32_t percent)
{
	if (percent == 0)
		return false;
	return (nextRandom() % 100u) < percent;
}

std::uint32_t NetEmulator::delay()
{
	std::uint32_t d = m_settings.latencyMs;
	if (m_settings.jitterMs != 0)
		d += nextRandom() % (m_settings.jitterMs + 1u);
	return d;
}

bool NetEmulator::enqueue(const void *data, std::size_t length, std::uint32_t addr, std::uint16_t port, std::uint32_t due, std::uint8_t tag)
{
	if (m_queue.size() >= MAX_QUEUED)
	{
		++m_stats.overflowed;
		return false;
	}
	Entry e;
	e.due = due;
	e.order = m_order++;
	e.packet.addr = addr;
	e.packet.port = port;
	e.packet.tag = tag;
	const std::uint8_t *bytes = static_cast<const std::uint8_t *>(data);
	e.packet.data.assign(bytes, bytes + length);
	m_queue.push_back(std::move(e));
	return true;
}

int NetEmulator::push(const void *data, std::size_t length, std::uint32_t addr, std::uint16_t port, std::uint32_t nowMs, std::uint8_t tag)
{
	++m_stats.received;
	if (data == nullptr && length != 0)
		return 0;

	if (chance(m_settings.lossPercent))
	{
		++m_stats.dropped;
		return 0;
	}

	std::uint32_t d = delay();
	if (chance(m_settings.reorderPercent))
	{
		// Held back long enough that packets arriving shortly after overtake it.
		d += std::max(MIN_REORDER_DELAY_MS, 2u * m_settings.jitterMs);
		++m_stats.reordered;
	}

	int copies = 0;
	if (enqueue(data, length, addr, port, nowMs + d, tag))
		++copies;

	if (chance(m_settings.duplicatePercent))
	{
		if (enqueue(data, length, addr, port, nowMs + delay(), tag))
		{
			++copies;
			++m_stats.duplicated;
		}
	}
	return copies;
}

bool NetEmulator::pop(std::uint32_t nowMs, Packet &out)
{
	// Few packets are queued at a time (one frame of traffic), a linear search is enough.
	std::size_t best = m_queue.size();
	for (std::size_t i = 0; i < m_queue.size(); ++i)
	{
		const Entry &e = m_queue[i];
		if (!isDue(e.due, nowMs))
			continue;
		if (best == m_queue.size()
			|| dueBefore(e.due, m_queue[best].due)
			|| (e.due == m_queue[best].due && e.order < m_queue[best].order))
		{
			best = i;
		}
	}
	if (best == m_queue.size())
		return false;

	out = std::move(m_queue[best].packet);
	m_queue.erase(m_queue.begin() + static_cast<std::ptrdiff_t>(best));
	++m_stats.delivered;
	return true;
}

} // namespace Engine
