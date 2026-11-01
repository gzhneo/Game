/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// LogicAccessGuard.h -- access guard for logic objects.
//
// If the logic tick runs on its own thread, the main thread may touch logic objects only at the
// sync point. The client reads object state directly in many places (health bars, draw modules,
// tooltips, ControlBar). This guard finds those accesses while everything is still serial:
//
//   * The logic is "open" during `GameEngine::update()` (tick) -- client and logic run one
//     after the other on the same thread there.
//   * It is "closed" during a pure render frame (drawn between two ticks).
//   * Every marked access while closed is reported: file, line, function, category --
//     **once per site** so the log does not fill up.
//
// The guard does not change behavior: it counts and reports. It compiles out in release builds.
// The collected list (`writeLogicAccessFindingsTo`) is the work list of sites to fix.

#pragma once

#ifndef ENGINE_LOGICACCESSGUARD_H
#define ENGINE_LOGICACCESSGUARD_H

#include <cstddef>

namespace Engine
{

/// Opens/closes logic access. Nesting is counted so a tick inside a tick (loading screen,
/// camera move) does not close early.
void openLogicAccess();
void closeLogicAccess();

/// TRUE while access is allowed (tick or sync point).
bool isLogicAccessOpen();

/// Reports an access while closed. Returns TRUE the first time for this site.
bool reportLogicAccess(const char *file, int line, const char *category, const char *function);

/// Number of sites found so far and of all reported accesses.
std::size_t logicAccessFindingCount();
unsigned long long logicAccessTotalCount();

/// Writes the findings as a text file. Returns FALSE if the file could not be written.
bool writeLogicAccessFindingsTo(const char *path);

/// Discards findings and counters. The state is a static object that dies only after WinMain;
/// its entries would otherwise count as leaks in the memory report at the end of WinMain.
void clearLogicAccessFindings();

/// Helper for a section with open access (tick, sync point).
class ScopedLogicAccess
{
public:
	ScopedLogicAccess() { openLogicAccess(); }
	~ScopedLogicAccess() { closeLogicAccess(); }
};

} // namespace Engine

// The guard costs nothing in release builds.
#if defined(_DEBUG)
	#define ASSERT_LOGIC_ACCESS(category)                                                    \
		do {                                                                                 \
			if (!::Engine::isLogicAccessOpen())                                              \
				::Engine::reportLogicAccess(__FILE__, __LINE__, category, __FUNCTION__);     \
		} while (0)
	#define SCOPED_LOGIC_ACCESS()	::Engine::ScopedLogicAccess engineScopedLogicAccess_

	// Inline accessors would only report their own header line. They take the caller's position as
	// a defaulted last parameter instead, so the findings name the call site.
	#include <source_location>
	#define LOGIC_ACCESS_SITE_ONLY		std::source_location logicAccessSite = std::source_location::current()
	#define LOGIC_ACCESS_SITE_DEFAULT	, std::source_location logicAccessSite = std::source_location::current()
	#define LOGIC_ACCESS_SITE			, std::source_location logicAccessSite
	#define ASSERT_LOGIC_ACCESS_AT_SITE(category)                                                   \
		do {                                                                                 \
			if (!::Engine::isLogicAccessOpen())                                              \
				::Engine::reportLogicAccess(logicAccessSite.file_name(),                     \
					static_cast<int>(logicAccessSite.line()), category, logicAccessSite.function_name()); \
		} while (0)
#else
	#define ASSERT_LOGIC_ACCESS(category) do { } while (0)
	#define SCOPED_LOGIC_ACCESS()         do { } while (0)
	#define LOGIC_ACCESS_SITE_ONLY
	#define LOGIC_ACCESS_SITE_DEFAULT
	#define LOGIC_ACCESS_SITE
	#define ASSERT_LOGIC_ACCESS_AT_SITE(category) do { } while (0)
#endif

#endif // ENGINE_LOGICACCESSGUARD_H
