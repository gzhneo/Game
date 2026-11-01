/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ThreadRole.h -- thread roles and their checks.
//
// Logic, rendering and job workers may run on different threads. So that wrong accesses fail
// in tests and not for players, each thread gets a role, and code that may only be called from
// a certain role gets an ASSERT macro:
//
//   ASSERT_MAIN_THREAD()   window, input, GameClient, rendering
//   ASSERT_SIM_THREAD()    GameLogic tick, ScriptEngine, AI
//   ASSERT_NOT_WORKER()    everything forbidden inside a job (global random, memory pool
//                          special cases, changing logic state)
//
// In release builds the macros are empty. The role itself is still tracked (a thread_local
// byte) because logging and crash reports show it.
//
// While logic and client share one thread, the main thread carries both roles Main AND Sim --
// hence THREAD_ROLE_MAIN_AND_SIM. The macros are correct for either layout.

#pragma once

#ifndef ENGINE_THREADROLE_H
#define ENGINE_THREADROLE_H
#include <cstddef>

namespace Engine
{

enum ThreadRole
{
	THREAD_ROLE_UNKNOWN = 0,		///< not set (library threads, audio, driver)
	THREAD_ROLE_MAIN,				///< main thread: window, input, client, rendering
	THREAD_ROLE_SIM,				///< simulation thread: GameLogic tick
	THREAD_ROLE_MAIN_AND_SIM,		///< both in one thread
	THREAD_ROLE_WORKER,				///< job system
	THREAD_ROLE_IO,					///< file/archive reading, streaming
	THREAD_ROLE_AUDIO,				///< audio threads
	THREAD_ROLE_COUNT
};

/// Sets the role of the calling thread. Also sets the thread name for debugging if a name is
/// given (nullptr = keep the name).
void setThreadRole(ThreadRole role, const char *threadName = nullptr);

/// Role of the calling thread.
ThreadRole threadRole();

/// Name of the role, e.g. "Sim" -- for log and crash output.
const char *threadRoleName(ThreadRole role);

/// TRUE if the calling thread has the given role. MAIN_AND_SIM satisfies both MAIN and SIM.
bool threadHasRole(ThreadRole role);

/// TRUE if the calling thread is a job worker.
bool threadIsWorker();

/// Reports a role violation (once per site, silent afterwards). Returns TRUE if this violation
/// occurred for the first time.
bool reportThreadRoleViolation(const char *file, int line, const char *expected, const char *function);
/// Number of sites with violations so far.
std::size_t threadRoleViolationSiteCount();
/// Writes the violations (site, function, expected and actual role, count) as a text file.
bool writeThreadRoleViolationsTo(const char *path);
/// Discards the collected violations (their storage would otherwise outlive the memory report).
void clearThreadRoleViolations();

} // namespace Engine

// The macros check only in debug builds. They are do/while statements so they can stand
// anywhere a statement can.
#if defined(_DEBUG)
	#define ENGINE_ASSERT_THREAD(condition, name)                                            \
		do {                                                                                 \
			if (!(condition))                                                                \
				::Engine::reportThreadRoleViolation(__FILE__, __LINE__, name, __FUNCTION__); \
		} while (0)
#else
	#define ENGINE_ASSERT_THREAD(condition, name) do { } while (0)
#endif

#define ASSERT_MAIN_THREAD()	ENGINE_ASSERT_THREAD(::Engine::threadHasRole(::Engine::THREAD_ROLE_MAIN), "Main")
#define ASSERT_SIM_THREAD()		ENGINE_ASSERT_THREAD(::Engine::threadHasRole(::Engine::THREAD_ROLE_SIM), "Sim")
#define ASSERT_NOT_WORKER()		ENGINE_ASSERT_THREAD(!::Engine::threadIsWorker(), "NotWorker")

#endif // ENGINE_THREADROLE_H
