/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WatchStream.h -- a running game's replay, streamed from the host to spectators.
//
// The host's recorder appends every command to the replay file. The host sends what is new in
// chunks over the sealed session socket, together with a mark (bytes, frames): the first `bytes` of
// the file hold every command of the frames below `frames`. A spectator plays the replay as far as
// the newest mark whose bytes it has, and waits there for more. Neither side touches the lockstep:
// players never wait for spectators.
//
// WatchSender: a window of unacknowledged bytes. Without progress it sends the first missing chunk
// again, and after HEAD_RETRIES of those everything from the first missing byte. WatchReceiver: keeps the contiguous bytes and, up to a limit,
// chunks that came early (behind a lost one), and the marks it cannot use yet. WatchHistory: the host's marks over the
// game, so the stream can lag a fixed number of frames behind the players.
//
// No engine header: tested by EngineTests.

#pragma once

#ifndef ENGINE_WATCHSTREAM_H
#define ENGINE_WATCHSTREAM_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace Engine
{

constexpr std::size_t WATCH_CHUNK = 896;						///< payload of one message (fits MAX_SESSION_MESSAGE)
constexpr std::uint32_t WATCH_MAX_BYTES = 256u * 1024u * 1024u;	///< larger streams are refused

struct WatchMark
{
	std::uint32_t bytes = 0;
	std::uint32_t frames = 0;
};

class WatchSender
{
public:
	struct Config
	{
		std::uint32_t windowBytes = 64u * 1024u;	///< sent but not acknowledged
		std::uint32_t resendMs = 400;				///< without progress, send again from the first missing byte
	};

	/// Resends of the first missing chunk alone before the whole window goes out again.
	static constexpr int HEAD_RETRIES = 2;

	WatchSender() = default;
	explicit WatchSender(const Config &config) : m_config(config) {}

	void reset();
	/// The receiver holds every byte below `received`; older or impossible values are ignored.
	void acknowledge(std::uint32_t received, std::uint32_t nowMs);
	/// The next chunk to send while `available` bytes exist; false if nothing is due now.
	bool nextChunk(std::uint32_t available, std::uint32_t nowMs, std::uint32_t &offset, std::uint32_t &length);

	std::uint32_t acknowledged() const { return m_acked; }
	std::uint32_t sent() const { return m_next; }

private:
	Config m_config;
	std::uint32_t m_acked = 0;
	std::uint32_t m_next = 0;			///< first byte not sent since the last rewind
	std::uint32_t m_highest = 0;		///< end of everything ever sent
	int m_retries = 0;					///< head resends since the last progress
	std::uint32_t m_progressMs = 0;		///< last acknowledgement that moved forward, or the first send
	bool m_waiting = false;				///< bytes are out and unacknowledged
};

class WatchReceiver
{
public:
	/// Marks kept until their bytes arrive; older ones are merged into newer ones beyond this.
	static constexpr std::size_t MAX_PENDING_MARKS = 64;
	/// Early chunks are kept only this far beyond the contiguous bytes.
	static constexpr std::uint32_t MAX_EARLY_BYTES = 64u * 1024u;

	void reset();
	/// A chunk at `offset`: what extends the contiguous bytes is appended to `out`, together with early
	/// chunks it connects. Returns how many bytes were appended; a chunk after a gap is kept for later
	/// (within MAX_EARLY_BYTES) and adds nothing yet.
	std::size_t accept(std::uint32_t offset, const std::uint8_t *data, std::size_t size, std::vector<std::uint8_t> &out);
	/// A mark of the host; refused (false) if it goes backwards or beyond the limit.
	bool noteMark(const WatchMark &mark);

	std::uint32_t received() const { return m_received; }
	/// Frames below this can be played: the newest mark whose bytes have all arrived.
	std::uint32_t playableFrames() const { return m_playable; }
	/// The newest mark heard, whether or not its bytes are here.
	const WatchMark &latestMark() const { return m_latest; }

private:
	void advance();
	void append(std::uint32_t offset, const std::uint8_t *data, std::size_t size, std::vector<std::uint8_t> &out);

	std::uint32_t m_received = 0;
	std::map<std::uint32_t, std::vector<std::uint8_t>> m_early;	///< by offset, all beyond m_received
	std::uint32_t m_playable = 0;
	WatchMark m_latest;
	std::vector<WatchMark> m_pending;
};

class WatchHistory
{
public:
	void reset() { m_marks.clear(); }
	/// The host's state between two logic frames: `bytes` of the file, every frame below `frames` done.
	void record(std::uint32_t bytes, std::uint32_t frames);
	/// The mark for spectators `delayFrames` behind the newest state; frames 0 before that.
	WatchMark delayed(std::uint32_t delayFrames) const;

private:
	std::vector<WatchMark> m_marks;		///< one entry per change of the byte count, frames ascending
};

} // namespace Engine

#endif // ENGINE_WATCHSTREAM_H
