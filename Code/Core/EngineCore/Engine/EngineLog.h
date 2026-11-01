/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineLog.h -- thread-safe log with categories, levels, a ring buffer for the crash report
// and assert forwarding from worker threads.
//
// DEBUG_LOG (Debug.cpp) writes through a global critical section that is only set up in
// WinMain, has no levels and is compiled out in release builds. This log complements it:
//
//   * **Thread-safe from the first call** -- no init order, no null pointer. Each line is
//     formatted thread-locally and written whole under a lock, so lines from different
//     threads never interleave.
//   * **Levels** Trace/Debug/Info/Warning/Error and **categories** (Engine, Logic, Client, Net,
//     Audio, IO, Job, Memory). Both are filterable at runtime so noisy sources (e.g. the
//     XAudio2 manager) can run at Trace instead of being deleted.
//   * **Ring buffer** of the last 2,000 lines, written into the crash report, also in release.
//   * **Asserts from worker threads** show no MessageBox. They are queued; the main thread
//     fetches them with pollPendingAssert() and reports them as usual.
//
// Depends on no engine header so tests and tools can use it.

#pragma once

#ifndef ENGINE_ENGINELOG_H
#define ENGINE_ENGINELOG_H

#include "Engine/FormatCheck.h"

#include <cstdarg>
#include <cstddef>

namespace Engine
{

enum LogLevel
{
	LOG_TRACE = 0,	///< very frequent details (per frame, per object)
	LOG_DEBUG,		///< developer info
	LOG_INFO,		///< state changes, start/end, configuration
	LOG_WARNING,	///< unexpected but handled
	LOG_ERROR,		///< errors
	LOG_LEVEL_COUNT
};

enum LogCategory
{
	LOG_CAT_ENGINE = 0,
	LOG_CAT_LOGIC,
	LOG_CAT_CLIENT,
	LOG_CAT_NET,
	LOG_CAT_AUDIO,
	LOG_CAT_IO,
	LOG_CAT_JOB,
	LOG_CAT_MEMORY,
	LOG_CATEGORY_COUNT
};

/// One line from the ring buffer (for the crash report).
struct LogRecord
{
	unsigned		timeMs;			///< Engine::Clock::milliseconds() when written
	LogLevel		level;
	LogCategory		category;
	int				threadRole;		///< Engine::ThreadRole
	unsigned		threadId;		///< platform thread id, for identification only
	char			text[192];		///< truncated if longer
};

class Log
{
public:
	/// Opens the file (append). Without it the log writes only to the ring buffer (and, in
	/// debug builds, to the debugger output).
	static bool openFile(const char *path);
	static void closeFile();

	/// Minimum level, global or per category. Default: LOG_INFO in release, LOG_DEBUG otherwise.
	static void setMinLevel(LogLevel level);
	static void setCategoryMinLevel(LogCategory category, LogLevel level);
	static LogLevel minLevel(LogCategory category);
	static bool isEnabled(LogCategory category, LogLevel level) { return level >= minLevel(category); }

	/// Writes one line. printf-style formatting; the line is written atomically.
	static void write(LogCategory category, LogLevel level, const char *format, ...) ENGINE_PRINTF_FORMAT(3, 4);
	static void writeV(LogCategory category, LogLevel level, const char *format, std::va_list args);

	/// Ring buffer: number of stored lines and access in write order (oldest first).
	static const std::size_t RING_CAPACITY = 2000;
	static std::size_t ringCount();
	static bool ringRecord(std::size_t index, LogRecord &out);
	/// Writes the ring buffer as text to a file (crash report).
	static bool writeRingBufferTo(const char *path);

	/// Queues an assert from a non-main thread (no MessageBox in the worker).
	static void postAssertFromThread(const char *file, int line, const char *function, const char *message);
	/// Called by the main thread: returns the next queued message or false.
	static bool pollPendingAssert(char *buffer, std::size_t bufferSize);
	static std::size_t pendingAssertCount();

	static const char *levelName(LogLevel level);
	static const char *categoryName(LogCategory category);
};

} // namespace Engine

// Convenience macros. No formatting cost when the level is filtered out.
#define ENGINE_LOG(cat, level, ...)                                        \
	do {                                                                   \
		if (::Engine::Log::isEnabled(cat, level))                          \
			::Engine::Log::write(cat, level, __VA_ARGS__);                 \
	} while (0)

#define ENGINE_LOG_TRACE(cat, ...)		ENGINE_LOG(cat, ::Engine::LOG_TRACE, __VA_ARGS__)
#define ENGINE_LOG_DEBUG(cat, ...)		ENGINE_LOG(cat, ::Engine::LOG_DEBUG, __VA_ARGS__)
#define ENGINE_LOG_INFO(cat, ...)		ENGINE_LOG(cat, ::Engine::LOG_INFO, __VA_ARGS__)
#define ENGINE_LOG_WARNING(cat, ...)	ENGINE_LOG(cat, ::Engine::LOG_WARNING, __VA_ARGS__)
#define ENGINE_LOG_ERROR(cat, ...)		ENGINE_LOG(cat, ::Engine::LOG_ERROR, __VA_ARGS__)

#endif // ENGINE_ENGINELOG_H
