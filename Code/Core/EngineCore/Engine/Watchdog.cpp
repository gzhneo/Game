/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Watchdog.cpp -- see Watchdog.h.

#include "Engine/Watchdog.h"
#include "Engine/Clock.h"
#include "Engine/EngineLog.h"
#include "Engine/ThreadRole.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace Engine
{

namespace
{
// A subject may report between reading `now` and `last`; treat a newer heartbeat as fresh
// instead of letting the unsigned difference underflow to nearly 2^32.
unsigned elapsedSince(unsigned now, unsigned last)
{
	const int delta = static_cast<int>(now - last);
	return (delta > 0) ? static_cast<unsigned>(delta) : 0u;
}
}

struct Watchdog::Impl
{
	struct Subject
	{
		std::atomic<unsigned>	lastHeartbeatMs{0};
		std::atomic<int>		suspendDepth{0};
		std::atomic<bool>		stallReported{false};
	};

	Subject					subjects[WATCHDOG_SUBJECT_COUNT];
	std::atomic<unsigned>	timeoutMs{10000};
	std::atomic<unsigned>	pollMs{250};
	std::atomic<bool>		running{false};
	std::atomic<unsigned>	stalls{0};
	std::thread				thread;
	std::mutex				callbackMutex;
	WatchdogCallback		callback;
	std::mutex				sleepMutex;
	std::condition_variable	sleep;

	void monitor()
	{
		setThreadRole(THREAD_ROLE_UNKNOWN, "Watchdog");

		while (running.load(std::memory_order_acquire))
		{
			const unsigned now = Clock::milliseconds();
			for (int i = 0; i < WATCHDOG_SUBJECT_COUNT; ++i)
			{
				Subject &subject = subjects[i];
				if (subject.suspendDepth.load(std::memory_order_relaxed) > 0)
					continue;
				const unsigned last = subject.lastHeartbeatMs.load(std::memory_order_relaxed);
				if (last == 0)
					continue;	// never reported: nothing to monitor
				const unsigned stalled = elapsedSince(now, last);
				if (stalled < timeoutMs.load(std::memory_order_relaxed))
				{
					subject.stallReported.store(false, std::memory_order_relaxed);
					continue;
				}
				// Report once per hang, not on every poll.
				if (subject.stallReported.exchange(true, std::memory_order_acq_rel))
					continue;

				stalls.fetch_add(1, std::memory_order_relaxed);
				Log::write(LOG_CAT_ENGINE, LOG_ERROR,
					"%s hung for %u ms (limit %u ms)",
					subjectName(static_cast<WatchdogSubject>(i)), stalled, timeoutMs.load(std::memory_order_relaxed));

				WatchdogCallback callbackCopy;
				{
					std::lock_guard<std::mutex> lock(callbackMutex);
					callbackCopy = callback;
				}
				if (callbackCopy)
					callbackCopy(static_cast<WatchdogSubject>(i), stalled);
			}

			std::unique_lock<std::mutex> lock(sleepMutex);
			sleep.wait_for(lock, std::chrono::milliseconds(pollMs.load(std::memory_order_relaxed)));
		}
	}
};

// Impl lives in static storage, not on the heap. The watchdog itself is a function-local static
// (get()) that dies only after WinMain; a heap Impl would count as a leak in the memory report at
// the end of WinMain.
Watchdog::Watchdog() : m_impl(nullptr)
{
	static Impl impl;
	m_impl = &impl;
}

Watchdog::~Watchdog()
{
	stop();
	m_impl = nullptr;
}

Watchdog &Watchdog::get()
{
	static Watchdog watchdog;
	return watchdog;
}

const char *Watchdog::subjectName(WatchdogSubject subject)
{
	static const char *names[WATCHDOG_SUBJECT_COUNT] = { "Hauptthread", "Sim-Thread" };
	return (subject >= 0 && subject < WATCHDOG_SUBJECT_COUNT) ? names[subject] : "?";
}

void Watchdog::start(unsigned timeoutMs, unsigned pollMs)
{
	stop();
	m_impl->timeoutMs.store(timeoutMs > 0 ? timeoutMs : 1);
	m_impl->pollMs.store(pollMs > 0 ? pollMs : 1);
	for (int i = 0; i < WATCHDOG_SUBJECT_COUNT; ++i)
	{
		m_impl->subjects[i].lastHeartbeatMs.store(0);
		m_impl->subjects[i].stallReported.store(false);
	}
	m_impl->running.store(true, std::memory_order_release);
	Impl *impl = m_impl;
	m_impl->thread = std::thread([impl]() { impl->monitor(); });

	ENGINE_LOG_INFO(LOG_CAT_ENGINE, "Watchdog started (limit %u ms, poll %u ms)", timeoutMs, pollMs);
}

void Watchdog::stop()
{
	if (!m_impl->running.load(std::memory_order_acquire))
		return;
	m_impl->running.store(false, std::memory_order_release);
	m_impl->sleep.notify_all();
	if (m_impl->thread.joinable())
		m_impl->thread.join();
}

bool Watchdog::isRunning() const
{
	return m_impl->running.load(std::memory_order_acquire);
}

void Watchdog::setCallback(const WatchdogCallback &callback)
{
	std::lock_guard<std::mutex> lock(m_impl->callbackMutex);
	m_impl->callback = callback;
}

void Watchdog::heartbeat(WatchdogSubject subject)
{
	if (subject < 0 || subject >= WATCHDOG_SUBJECT_COUNT)
		return;
	// Clock::milliseconds() is monotonic; 0 means "never reported", so store at least 1.
	const unsigned now = Clock::milliseconds();
	m_impl->subjects[subject].lastHeartbeatMs.store(now != 0 ? now : 1, std::memory_order_relaxed);
}

void Watchdog::suspend(WatchdogSubject subject)
{
	if (subject >= 0 && subject < WATCHDOG_SUBJECT_COUNT)
		m_impl->subjects[subject].suspendDepth.fetch_add(1, std::memory_order_relaxed);
}

void Watchdog::resume(WatchdogSubject subject)
{
	if (subject < 0 || subject >= WATCHDOG_SUBJECT_COUNT)
		return;
	// Reset the clock first, then monitor again -- only when the outermost suspension ends.
	std::atomic<int> &depth = m_impl->subjects[subject].suspendDepth;
	int current = depth.load(std::memory_order_relaxed);
	while (current > 1 && !depth.compare_exchange_weak(current, current - 1, std::memory_order_relaxed))
	{
	}
	if (current > 1)
		return;
	heartbeat(subject);
	m_impl->subjects[subject].stallReported.store(false, std::memory_order_relaxed);
	depth.store(0, std::memory_order_relaxed);
}

unsigned Watchdog::stallCount() const
{
	return m_impl->stalls.load(std::memory_order_relaxed);
}

unsigned Watchdog::millisecondsSinceHeartbeat(WatchdogSubject subject) const
{
	if (subject < 0 || subject >= WATCHDOG_SUBJECT_COUNT)
		return 0;
	const unsigned last = m_impl->subjects[subject].lastHeartbeatMs.load(std::memory_order_relaxed);
	return (last == 0) ? 0 : elapsedSince(Clock::milliseconds(), last);
}

} // namespace Engine
