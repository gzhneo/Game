/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ArchivePrefetch.cpp --------------------------------------------------------------------------
// Reads archives in the background so the file cache is warm for the first map load (see
// ArchivePrefetch.h). The plan is taken on the main thread; the thread then only reads with its
// own file handles into its own buffer and uses no file system object. A single flag cancels it.
// ----------------------------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ArchivePrefetch.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/Debug.h"
#include "Common/GameEngine.h"
#include "Engine/PositionalFile.h"
#include "formatinto.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "wwtracy.h"	// thread name
#include "Engine/ThreadRole.h"

namespace
{

std::thread s_thread;
// Written before the thread starts, then only read by it. On the heap and freed in ArchivePrefetch_Stop: a static
// vector would keep its debug-STL container proxy until after the leak report.
std::unique_ptr<std::vector<ArchiveFileSystem::PrefetchJob>> s_jobs;
std::atomic<bool> s_stop{ false };
std::atomic<bool> s_done{ false };
unsigned s_statFiles = 0, s_statMegabytes = 0, s_statMillis = 0;	// written by the thread before s_done
bool s_reported = false;
std::uint64_t s_plannedBytes = 0;

void prefetchThread()
{
	WWDebug_Set_Thread_Name("Archive prefetch");
	::Engine::setThreadRole(::Engine::THREAD_ROLE_IO);
	::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	const std::size_t BUFFER_SIZE = 4 * 1024 * 1024;
	char *buffer = static_cast<char *>(::VirtualAlloc(nullptr, BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	const DWORD startTime = ::GetTickCount();
	std::uint64_t totalBytes = 0;
	unsigned filesDone = 0;
	const std::vector<ArchiveFileSystem::PrefetchJob> &jobs = *s_jobs;
	for (std::size_t n = 0; buffer != nullptr && n < jobs.size() && !s_stop.load(std::memory_order_relaxed); ++n)
	{
		const ArchiveFileSystem::PrefetchJob &job = jobs[n];
		::Engine::PositionalFile file;
		if (!file.open(job.path, ::Engine::PositionalFile::Pattern::Sequential))
			continue;
		const ::Engine::Vfs::PrefetchRange whole = { 0, file.size() };
		const ::Engine::Vfs::PrefetchRange *ranges = job.ranges.empty() ? &whole : job.ranges.data();
		const std::size_t rangeCount = job.ranges.empty() ? 1 : job.ranges.size();
		for (std::size_t r = 0; r < rangeCount; ++r)
		{
			const ::Engine::Vfs::PrefetchRange &range = ranges[r];
			std::uint64_t at = range.offset;
			const std::uint64_t end = range.offset + range.size;
			while (at < end && !s_stop.load(std::memory_order_relaxed))
			{
				const std::size_t want = end - at < BUFFER_SIZE ? static_cast<std::size_t>(end - at) : BUFFER_SIZE;
				const std::size_t got = file.readAt(at, buffer, want);
				if (got == 0)
					break;
				at += got;
				totalBytes += got;
			}
		}
		++filesDone;
	}
	if (buffer != nullptr)
		::VirtualFree(buffer, 0, MEM_RELEASE);

	s_statFiles = filesDone;
	s_statMegabytes = static_cast<unsigned>(totalBytes / (1024 * 1024));
	s_statMillis = ::GetTickCount() - startTime;
	s_done.store(true, std::memory_order_release);
}

}	// namespace

void ArchivePrefetch_Poll( void )
{
	if (s_reported || !s_done.load(std::memory_order_acquire))
		return;
	s_reported = true;
	char line[160];
	WWFormat::Into(line, "VORABLESEN dateien {:d} mb {:d} ms {:d}{}", s_statFiles, s_statMegabytes, s_statMillis,
		s_stop.load() ? " abgebrochen" : "");
	line[sizeof(line) - 1] = 0;
	GameEngine_RunnerLog(line);
	DEBUG_LOG(("%s\n", line));
}

void ArchivePrefetch_Start( void )
{
	if (s_thread.joinable() || TheArchiveFileSystem == nullptr)
		return;
	s_jobs = std::make_unique<std::vector<ArchiveFileSystem::PrefetchJob>>(TheArchiveFileSystem->getPrefetchJobs());
	s_plannedBytes = 0;
	for (const ArchiveFileSystem::PrefetchJob &job : *s_jobs)
		s_plannedBytes += job.bytes;
	s_stop.store(false);
	s_done.store(false);
	s_reported = false;

	char line[160];
	WWFormat::Into(line, "VORABLESEN plan dateien {:d} mb {:d}", static_cast<Int>(s_jobs->size()),
		static_cast<unsigned>(s_plannedBytes / (1024 * 1024)));
	line[sizeof(line) - 1] = 0;
	GameEngine_RunnerLog(line);
	DEBUG_LOG(("%s\n", line));
#if defined(_DEBUG)
	for (const ArchiveFileSystem::PrefetchJob &job : *s_jobs)
		DEBUG_LOG(("Prefetch: %s, %u KB%s\n", job.path.c_str(), static_cast<unsigned>(job.bytes / 1024), job.ranges.empty() ? "" : " (served entries)"));
#endif
	s_thread = std::thread(prefetchThread);
}

void ArchivePrefetch_Stop( void )
{
	if (!s_thread.joinable())
		return;
	s_stop.store(true);
	s_thread.join();
	s_jobs.reset();
}
