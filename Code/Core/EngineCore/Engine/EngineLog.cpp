/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineLog.cpp -- see EngineLog.h.

#include "Engine/EngineLog.h"
#include "Engine/Clock.h"
#include "Engine/FormatInto.h"
#include "Engine/ThreadRole.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string_view>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
	#include <windows.h>
#endif

namespace Engine
{

namespace
{
	/// All state shared between threads. Built on first access (function-local static,
	/// thread-safe since C++11), so there is no init gap like with TheDebugLogCriticalSection.
	struct LogState
	{
		// Timed because `writeRingBufferTo()` runs from the crash handler: waiting for a thread
		// that just died or froze must not cost the whole report (see there).
		std::timed_mutex	mutex;
		std::FILE *			file = nullptr;
		// Levels are atomics, not behind the lock: `isEnabled()` precedes **every** log call,
		// also in loops over objects. A mutex there would cost more than the write itself and
		// serialize threads just to read a number.
		std::atomic<int>	minLevel{
#if defined(_DEBUG)
			LOG_DEBUG
#else
			LOG_INFO
#endif
		};
		std::atomic<int>	categoryMinLevel[LOG_CATEGORY_COUNT];
		std::vector<LogRecord> ring;
		std::size_t			ringNext = 0;		///< next write index
		bool				ringWrapped = false;
		std::vector<std::vector<char> > pendingAsserts;

		LogState()
		{
			for (int i = 0; i < LOG_CATEGORY_COUNT; ++i)
				categoryMinLevel[i].store(LOG_TRACE);	// category adds no filter until set
			ring.resize(Log::RING_CAPACITY);
		}
	};

	LogState &state()
	{
		static LogState s;
		return s;
	}

	unsigned currentThreadId()
	{
#if defined(_WIN32)
		return static_cast<unsigned>(::GetCurrentThreadId());
#else
		return static_cast<unsigned>(std::hash<std::thread::id>()(std::this_thread::get_id()) & 0xFFFFFFFFu);
#endif
	}
}

const char *Log::levelName(LogLevel level)
{
	static const char *names[LOG_LEVEL_COUNT] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR" };
	return (level >= 0 && level < LOG_LEVEL_COUNT) ? names[level] : "?";
}

const char *Log::categoryName(LogCategory category)
{
	static const char *names[LOG_CATEGORY_COUNT] =
		{ "Engine", "Logic", "Client", "Net", "Audio", "IO", "Job", "Memory" };
	return (category >= 0 && category < LOG_CATEGORY_COUNT) ? names[category] : "?";
}

bool Log::openFile(const char *path)
{
	LogState &s = state();
	std::lock_guard<std::timed_mutex> lock(s.mutex);
	if (s.file != nullptr)
	{
		std::fclose(s.file);
		s.file = nullptr;
	}
	s.file = std::fopen(path, "a");
	return s.file != nullptr;
}

void Log::closeFile()
{
	LogState &s = state();
	std::lock_guard<std::timed_mutex> lock(s.mutex);
	if (s.file != nullptr)
	{
		std::fclose(s.file);
		s.file = nullptr;
	}
}

void Log::setMinLevel(LogLevel level)
{
	state().minLevel.store(level, std::memory_order_relaxed);
}

void Log::setCategoryMinLevel(LogCategory category, LogLevel level)
{
	if (category < 0 || category >= LOG_CATEGORY_COUNT)
		return;
	state().categoryMinLevel[category].store(level, std::memory_order_relaxed);
}

LogLevel Log::minLevel(LogCategory category)
{
	LogState &s = state();
	int level = s.minLevel.load(std::memory_order_relaxed);
	if (category >= 0 && category < LOG_CATEGORY_COUNT)
	{
		const int categoryLevel = s.categoryMinLevel[category].load(std::memory_order_relaxed);
		if (categoryLevel > level)
			level = categoryLevel;
	}
	return static_cast<LogLevel>(level);
}

void Log::writeV(LogCategory category, LogLevel level, const char *format, std::va_list args)
{
	if (!isEnabled(category, level))
		return;

	// Format thread-locally: no lock needed up to here, and the finished line is written whole
	// (no interleaving between threads).
	LogRecord record;
	record.timeMs = Clock::milliseconds();
	record.level = level;
	record.category = category;
	record.threadRole = static_cast<int>(threadRole());
	record.threadId = currentThreadId();
	std::vsnprintf(record.text, sizeof(record.text), format, args);
	record.text[sizeof(record.text) - 1] = '\0';
	// Strip trailing line breaks -- DEBUG_LOG callers often append "\n".
	for (std::size_t len = std::strlen(record.text); len > 0 && (record.text[len - 1] == '\n' || record.text[len - 1] == '\r'); --len)
		record.text[len - 1] = '\0';

	char line[320];
	formatInto(line, "[{:>8}] {:<5} {:<6} {:<11} {}\n",
		record.timeMs, levelName(level), categoryName(category),
		threadRoleName(static_cast<ThreadRole>(record.threadRole)), record.text);

	LogState &s = state();
	std::lock_guard<std::timed_mutex> lock(s.mutex);
	s.ring[s.ringNext] = record;
	s.ringNext = (s.ringNext + 1) % RING_CAPACITY;
	if (s.ringNext == 0)
		s.ringWrapped = true;
	if (s.file != nullptr)
	{
		std::fputs(line, s.file);
		if (level >= LOG_WARNING)
			std::fflush(s.file);
	}
#if defined(_WIN32) && defined(_DEBUG)
	::OutputDebugStringA(line);
#endif
}

void Log::write(LogCategory category, LogLevel level, const char *format, ...)
{
	std::va_list args;
	va_start(args, format);
	writeV(category, level, format, args);
	va_end(args);
}

std::size_t Log::ringCount()
{
	LogState &s = state();
	std::lock_guard<std::timed_mutex> lock(s.mutex);
	return s.ringWrapped ? RING_CAPACITY : s.ringNext;
}

bool Log::ringRecord(std::size_t index, LogRecord &out)
{
	LogState &s = state();
	std::lock_guard<std::timed_mutex> lock(s.mutex);
	const std::size_t count = s.ringWrapped ? RING_CAPACITY : s.ringNext;
	if (index >= count)
		return false;
	const std::size_t first = s.ringWrapped ? s.ringNext : 0;
	out = s.ring[(first + index) % RING_CAPACITY];
	return true;
}

bool Log::writeRingBufferTo(const char *path)
{
	std::FILE *out = std::fopen(path, "w");
	if (out == nullptr)
		return false;

	// The whole ring under *one* lock, without allocating anything.
	//
	// Locking per line (ringCount()/ringRecord() per entry) would not give a snapshot: if other
	// threads keep writing -- and during a crash they do -- the ring start moves and lines
	// appear twice or go missing.
	//
	// Copying into a vector first would be the usual approach but is wrong here: this is called
	// from the crash handler, and the most common crash cause is a corrupted heap. An allocation
	// there could be the final blow. So write directly from the ring to the file.
	LogState &s = state();
	// No blocking lock(): if another thread holds the lock and does not continue (or it is the
	// crashed thread itself, hit mid-write), there would be no report at all.
	std::unique_lock<std::timed_mutex> lock(s.mutex, std::chrono::milliseconds(500));
	if (!lock.owns_lock())
		std::fputs("(could not take the log lock -- the last lines may be incomplete)\n", out);

	const std::size_t count = s.ringWrapped ? RING_CAPACITY : s.ringNext;
	const std::size_t first = s.ringWrapped ? s.ringNext : 0;
	for (std::size_t i = 0; i < count; ++i)
	{
		const LogRecord &r = s.ring[(first + i) % RING_CAPACITY];
		// Without the lock a writer may be mid-way through this record and its text may briefly
		// lack the terminator; never read past the array.
		const std::size_t textLength = strnlen(r.text, sizeof(r.text));
		char line[sizeof(r.text) + 64];
		formatInto(line, "[{:>8}] {:<5} {:<6} {:<11} (tid {}) {}\n", r.timeMs, levelName(r.level),
			categoryName(r.category), threadRoleName(static_cast<ThreadRole>(r.threadRole)),
			r.threadId, std::string_view(r.text, textLength));
		std::fputs(line, out);
	}
	std::fclose(out);
	return true;
}

void Log::postAssertFromThread(const char *file, int line, const char *function, const char *message)
{
	char text[512];
	formatInto(text, "{}({}) {}: {}", file ? file : "?", line,
		function ? function : "?", message ? message : "");

	write(LOG_CAT_ENGINE, LOG_ERROR, "ASSERT from thread role %s: %s",
		threadRoleName(threadRole()), text);

	LogState &s = state();
	std::lock_guard<std::timed_mutex> lock(s.mutex);
	if (s.pendingAsserts.size() < 64)	// bounded queue
		s.pendingAsserts.push_back(std::vector<char>(text, text + std::strlen(text) + 1));
}

bool Log::pollPendingAssert(char *buffer, std::size_t bufferSize)
{
	if (buffer == nullptr || bufferSize == 0)
		return false;
	LogState &s = state();
	std::lock_guard<std::timed_mutex> lock(s.mutex);
	if (s.pendingAsserts.empty())
		return false;
	const std::vector<char> &front = s.pendingAsserts.front();
	formatInto(buffer, bufferSize, "{}", front.data());
	s.pendingAsserts.erase(s.pendingAsserts.begin());
	return true;
}

std::size_t Log::pendingAssertCount()
{
	LogState &s = state();
	std::lock_guard<std::timed_mutex> lock(s.mutex);
	return s.pendingAsserts.size();
}

} // namespace Engine
