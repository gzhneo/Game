/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ReplayCrcQueue.h -- pairs the logic CRCs a replay recomputes with the ones the recorded game sent.
//
// In a network game the CRC of logic frame g reaches the network in frame g + 1. While the network
// is still in its pregame state (frames 0 and 1) it discards outgoing commands, so the first CRCs of
// a game never reach the replay file; the playback recomputes them all the same. How many are
// missing depends on the order of the messages in that frame, and the file does not record the
// frame a CRC was computed in. The queue therefore keeps the recomputed CRCs with their frame and,
// before the first comparison, tries each possible number of missing pregame CRCs: only an offset
// that matches the recorded values survives. A single-player replay records every CRC (offset 0).

#pragma once

#ifndef ENGINE_REPLAYCRCQUEUE_H
#define ENGINE_REPLAYCRCQUEUE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace Engine
{

class ReplayCrcQueue
{
public:
	/// Recomputed CRCs that arrive up to this frame may be missing from the recording (pregame
	/// frames 0 and 1, each arriving one frame after it was computed).
	static constexpr std::uint32_t PREGAME_ARRIVAL_FRAME = 2;
	/// Recorded CRCs looked at before taking the smallest offset that still fits.
	static constexpr std::size_t MAX_UNDECIDED = 8;

	/// A CRC the playback computed, with the logic frame it arrived in.
	void addComputed( std::uint32_t crc, std::uint32_t frame )
	{
		m_computed.push_back( Entry{ crc, frame } );
	}

	/// A CRC from the recording. Returns false on a mismatch and then sets expected to the
	/// recomputed value it was compared with. While the offset is undecided it returns true.
	bool checkRecorded( std::uint32_t recorded, std::uint32_t &expected )
	{
		if ( m_aligned )
			return compareNext( recorded, expected );

		m_undecided.push_back( recorded );
		std::size_t fitting = 0;
		std::size_t smallest = 0;
		const std::size_t most = skippable();
		for ( std::size_t offset = most + 1; offset-- > 0; )
		{
			if ( fits( offset ) )
			{
				++fitting;
				smallest = offset;
			}
		}
		if ( fitting == 0 )
		{
			// no offset explains the recording: compare as recorded, which reports the first difference
			m_aligned = true;
			std::vector<std::uint32_t> pending;
			pending.swap( m_undecided );
			for ( std::uint32_t value : pending )
			{
				if ( !compareNext( value, expected ) )
					return false;
			}
			return true;
		}
		if ( fitting == 1 || m_undecided.size() >= MAX_UNDECIDED )
		{
			m_offset = smallest;
			m_aligned = true;
			m_computed.erase( m_computed.begin(), m_computed.begin() + static_cast<std::ptrdiff_t>( smallest + m_undecided.size() ) );
			m_undecided.clear();
		}
		return true;
	}

	bool aligned() const { return m_aligned; }
	/// Recomputed CRCs the recording lacks (valid once aligned).
	std::size_t offset() const { return m_offset; }

private:
	struct Entry
	{
		std::uint32_t crc;
		std::uint32_t frame;
	};

	/// Leading recomputed CRCs of the pregame frames.
	std::size_t skippable() const
	{
		std::size_t n = 0;
		while ( n < m_computed.size() && m_computed[n].frame <= PREGAME_ARRIVAL_FRAME )
			++n;
		return n;
	}

	/// Whether skipping offset recomputed CRCs matches every undecided recorded one. A recorded CRC
	/// always follows its recomputed one (it was executed later in the recorded game), so a missing
	/// recomputed value means the offset does not fit.
	bool fits( std::size_t offset ) const
	{
		for ( std::size_t i = 0; i < m_undecided.size(); ++i )
		{
			if ( offset + i >= m_computed.size() || m_computed[offset + i].crc != m_undecided[i] )
				return false;
		}
		return true;
	}

	bool compareNext( std::uint32_t recorded, std::uint32_t &expected )
	{
		if ( m_computed.empty() )
		{
			expected = 0;
			return recorded == 0;
		}
		expected = m_computed.front().crc;
		m_computed.pop_front();
		return expected == recorded;
	}

	std::deque<Entry> m_computed;
	std::vector<std::uint32_t> m_undecided;
	std::size_t m_offset = 0;
	bool m_aligned = false;
};

} // namespace Engine

#endif // ENGINE_REPLAYCRCQUEUE_H
