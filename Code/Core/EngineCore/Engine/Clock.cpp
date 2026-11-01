/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Clock.cpp -- see Clock.h.

#include "Engine/Clock.h"

#include <chrono>

namespace Engine
{

std::uint64_t Clock::microseconds()
{
	// steady_clock is QueryPerformanceCounter under MSVC. Zero is the first call so values stay
	// small and 32-bit milliseconds last over 1000 hours.
	using clock = std::chrono::steady_clock;
	static const clock::time_point start = clock::now();
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start).count());
}

} // namespace Engine
