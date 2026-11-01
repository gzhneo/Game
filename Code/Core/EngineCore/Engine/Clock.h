/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Clock.h -- the central clock.
//
// The engine has two distinct notions of time:
//
//   1. **Real time** (wall clock). For rendering, input, network timeouts and measurements.
//      It must NOT appear in game logic: two machines measure differently, and even the same
//      machine measures differently during a replay than during recording.
//   2. **Logic time** (frame number). 30 ticks per second, identical on all machines and in
//      every playback. This is the only time that may be game state.
//
// This file provides both and keeps them apart. It depends on no engine header (no PreRTS.h)
// so tools and unit tests can use it without the whole engine.
//
// Client, network and tool code use `Engine::Clock::milliseconds()` for real time. In
// `Source/GameLogic` any real-time query is a determinism bug; use the frame number
// (`Engine::LogicTime`).

#pragma once

#ifndef ENGINE_CLOCK_H
#define ENGINE_CLOCK_H

#include <cstdint>

namespace Engine
{

/// Real time since program start. Monotonic (no jumps on DST or NTP adjustments), microsecond
/// resolution (QueryPerformanceCounter or std::chrono::steady_clock).
class Clock
{
public:
	/// Microseconds since the first call in the process. Thread-safe.
	static std::uint64_t microseconds();

	/// Milliseconds since program start; use instead of timeGetTime()/GetTickCount().
	static std::uint32_t milliseconds() { return static_cast<std::uint32_t>(microseconds() / 1000u); }

	/// Seconds as floating point, for interpolation and measurement output.
	static double seconds() { return static_cast<double>(microseconds()) / 1000000.0; }
};

/// Times a section. Only for measurement and rendering, never for game logic.
class ScopedTimer
{
public:
	ScopedTimer() : m_start(Clock::microseconds()) {}
	void reset() { m_start = Clock::microseconds(); }
	std::uint64_t elapsedMicroseconds() const { return Clock::microseconds() - m_start; }
	double elapsedMilliseconds() const { return static_cast<double>(elapsedMicroseconds()) / 1000.0; }

private:
	std::uint64_t m_start;
};

/// Logic time: everything in the simulation that measures time counts frames.
/// LOGICFRAMES_PER_SECOND is 30 (GameCommon.h); repeated here without engine dependencies so
/// tools can use it. A static_assert in GameLogic.cpp keeps both in sync.
namespace LogicTime
{
	constexpr int FRAMES_PER_SECOND = 30;
	constexpr double SECONDS_PER_FRAME = 1.0 / static_cast<double>(FRAMES_PER_SECOND);

	constexpr unsigned framesFromSeconds(double seconds)
	{
		return static_cast<unsigned>(seconds * FRAMES_PER_SECOND + 0.5);
	}
	constexpr unsigned framesFromMilliseconds(unsigned ms)
	{
		return (ms * FRAMES_PER_SECOND + 500u) / 1000u;
	}
	constexpr double secondsFromFrames(unsigned frames)
	{
		return static_cast<double>(frames) * SECONDS_PER_FRAME;
	}
	constexpr unsigned millisecondsFromFrames(unsigned frames)
	{
		return (frames * 1000u) / FRAMES_PER_SECOND;
	}
}

} // namespace Engine

#endif // ENGINE_CLOCK_H
