/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RecentIds.h -- the last IDs seen from one sender.
//
// A sender repeats a command whose acknowledgement got lost; the copy carries the same 16-bit ID.
// Commands that are acted on when they arrive (chat, map transfer, ...) use this to act once.
//
// No engine header: tested by EngineTests.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Engine
{

template <std::size_t N>
class RecentIds
{
public:
	/// True the first time `id` is seen among the last N IDs (it is remembered then); false for a
	/// repeat.
	bool firstTime(std::uint16_t id)
	{
		for (std::size_t i = 0; i < m_count; ++i)
		{
			if (m_ids[i] == id)
				return false;
		}
		m_ids[m_next] = id;
		m_next = (m_next + 1) % N;
		if (m_count < N)
			++m_count;
		return true;
	}

	void clear() { m_next = 0; m_count = 0; }

private:
	std::array<std::uint16_t, N> m_ids{};
	std::size_t m_next = 0;
	std::size_t m_count = 0;
};

} // namespace Engine
