/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WatchStream.cpp -- see WatchStream.h.

#include "Engine/WatchStream.h"

namespace Engine
{

void WatchSender::reset()
{
	m_acked = 0;
	m_next = 0;
	m_highest = 0;
	m_retries = 0;
	m_progressMs = 0;
	m_waiting = false;
}

void WatchSender::acknowledge(std::uint32_t received, std::uint32_t nowMs)
{
	// a receiver cannot hold what was never sent
	if (received <= m_acked || received > m_highest)
		return;
	m_acked = received;
	m_retries = 0;
	if (m_next < m_acked)
		m_next = m_acked;	// acknowledged after a rewind
	m_progressMs = nowMs;
	m_waiting = m_acked < m_next;
}

bool WatchSender::nextChunk(std::uint32_t available, std::uint32_t nowMs, std::uint32_t &offset, std::uint32_t &length)
{
	if (available > WATCH_MAX_BYTES)
		available = WATCH_MAX_BYTES;
	if (m_waiting && static_cast<std::uint32_t>(nowMs - m_progressMs) >= m_config.resendMs)
	{
		m_progressMs = nowMs;
		if (m_retries < HEAD_RETRIES)
		{
			// the receiver keeps early chunks: the first missing one may be all it lacks
			++m_retries;
			const std::uint32_t missing = m_highest - m_acked;
			offset = m_acked;
			length = missing < WATCH_CHUNK ? missing : static_cast<std::uint32_t>(WATCH_CHUNK);
			return true;
		}
		// still nothing: everything from the first missing byte again
		m_retries = 0;
		m_next = m_acked;
	}
	const std::uint32_t windowEnd = m_acked + m_config.windowBytes;
	std::uint32_t end = available < windowEnd ? available : windowEnd;
	if (m_next >= end)
		return false;
	if (end - m_next > WATCH_CHUNK)
		end = m_next + static_cast<std::uint32_t>(WATCH_CHUNK);
	offset = m_next;
	length = end - m_next;
	if (!m_waiting)
	{
		m_waiting = true;
		m_progressMs = nowMs;
	}
	m_next = end;
	if (m_highest < end)
		m_highest = end;
	return true;
}

void WatchReceiver::reset()
{
	m_received = 0;
	m_playable = 0;
	m_latest = WatchMark();
	m_pending.clear();
	m_early.clear();
}

std::size_t WatchReceiver::accept(std::uint32_t offset, const std::uint8_t *data, std::size_t size, std::vector<std::uint8_t> &out)
{
	if (data == nullptr || size == 0 || size > WATCH_CHUNK)
		return 0;
	const std::uint64_t end = static_cast<std::uint64_t>(offset) + size;
	if (end <= m_received || end > WATCH_MAX_BYTES)
		return 0;
	if (offset > m_received)
	{
		// behind a lost chunk: keep it if it is near and new
		if (end - m_received <= MAX_EARLY_BYTES && m_early.find(offset) == m_early.end())
			m_early.emplace(offset, std::vector<std::uint8_t>(data, data + size));
		return 0;
	}
	const std::uint32_t before = m_received;
	append(offset, data, size, out);
	// early chunks that now connect
	while (!m_early.empty() && m_early.begin()->first <= m_received)
	{
		const auto first = m_early.begin();
		const std::vector<std::uint8_t> chunk = std::move(first->second);
		const std::uint32_t at = first->first;
		m_early.erase(first);
		if (at + chunk.size() > m_received)
			append(at, chunk.data(), chunk.size(), out);
	}
	advance();
	return m_received - before;
}

void WatchReceiver::append(std::uint32_t offset, const std::uint8_t *data, std::size_t size, std::vector<std::uint8_t> &out)
{
	const std::size_t skip = m_received - offset;
	out.insert(out.end(), data + skip, data + size);
	m_received = offset + static_cast<std::uint32_t>(size);
}

bool WatchReceiver::noteMark(const WatchMark &mark)
{
	if (mark.bytes > WATCH_MAX_BYTES || mark.bytes < m_latest.bytes || mark.frames < m_latest.frames)
		return false;
	if (mark.frames == m_latest.frames)
		return mark.bytes == m_latest.bytes;	// the same mark again
	m_latest = mark;
	if (m_pending.size() >= MAX_PENDING_MARKS)
		m_pending.back() = mark;	// merged: the older one becomes playable with this one
	else
		m_pending.push_back(mark);
	advance();
	return true;
}

void WatchReceiver::advance()
{
	std::size_t used = 0;
	while (used < m_pending.size() && m_pending[used].bytes <= m_received)
	{
		m_playable = m_pending[used].frames;
		++used;
	}
	if (used > 0)
		m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(used));
}

void WatchHistory::record(std::uint32_t bytes, std::uint32_t frames)
{
	if (!m_marks.empty())
	{
		WatchMark &last = m_marks.back();
		if (bytes < last.bytes || frames < last.frames)
			return;	// a new game must reset first
		if (bytes == last.bytes)
		{
			last.frames = frames;
			return;
		}
	}
	m_marks.push_back({ bytes, frames });
}

WatchMark WatchHistory::delayed(std::uint32_t delayFrames) const
{
	if (m_marks.empty())
		return WatchMark();
	const std::uint32_t newest = m_marks.back().frames;
	if (delayFrames == 0)
		return m_marks.back();
	if (newest <= delayFrames)
		return WatchMark();
	const std::uint32_t limit = newest - delayFrames;
	// the newest entry at or below the limit: its bytes hold every frame below its frames
	std::size_t lo = 0;
	std::size_t hi = m_marks.size();
	while (lo < hi)
	{
		const std::size_t mid = lo + (hi - lo) / 2;
		if (m_marks[mid].frames <= limit)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo == 0 ? WatchMark() : m_marks[lo - 1];
}

} // namespace Engine
