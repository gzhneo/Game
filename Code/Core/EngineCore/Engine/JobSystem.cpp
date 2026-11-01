/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// JobSystem.cpp -- see JobSystem.h.
//
// Layout: one queue (deque) per worker plus a shared queue for external submitters, each split
// by priority. A worker with nothing left steals from the others in turn. The queues use a mutex
// instead of being lock-free: blocks are coarse (grain size), so the overhead is negligible and
// correctness is easy to follow.
//
// Workers are not pinned to cores.

#include "Engine/JobSystem.h"
#include "Engine/CpuTopology.h"
#include "Engine/EngineLog.h"
#include "Engine/ThreadRole.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <thread>

// Tracy zone per block. Tracy exists only in the profiling build (ENABLE_TRACY), so this checks
// TRACY_ENABLE directly instead of going through WWDebug/wwtracy.h -- this file must compile
// without the WWDebug include path (EngineTests).
#ifdef TRACY_ENABLE
	#include <tracy/Tracy.hpp>
	#define JOB_ZONE(name)	ZoneScopedN(name)
#else
	#define JOB_ZONE(name)
#endif

namespace Engine
{

namespace
{
	/// One piece of work: either a range from parallelFor or a background job.
	struct Job
	{
		const RangeFunction *		range = nullptr;	///< points to the caller's function (outlives the job)
		std::size_t					begin = 0;
		std::size_t					end = 0;
		std::atomic<int> *			pending = nullptr;	///< remaining-block counter of the parallelFor group
		std::function<void()>		standalone;			///< for submitBackground
	};

	thread_local int t_workerIndex = -1;		///< index in the pool, -1 = not a worker of this pool
	thread_local const void *t_workerPool = nullptr;	///< the pool the index belongs to
}

struct JobSystem::Impl
{
	struct WorkerQueue
	{
		std::mutex			mutex;
		std::deque<Job>		jobs[JOB_PRIORITY_COUNT];
	};

	std::vector<WorkerQueue *>	queues;			///< one per worker, plus one for external submitters
	std::vector<std::thread>	threads;
	std::mutex					wakeMutex;
	std::condition_variable		wake;
	std::atomic<bool>			stopping{false};
	std::atomic<unsigned long long> blocksExecuted{0};
	std::atomic<unsigned long long> blocksByCallers{0};
	std::atomic<int>			sleepingWorkers{0};
	std::atomic<unsigned long long> jobExceptions{0};	// blocks that ended with an exception

	/// Index of the queue the calling thread submits to.
	std::size_t queueIndexForCaller() const
	{
		if (t_workerIndex >= 0 && t_workerPool == this)
			return static_cast<std::size_t>(t_workerIndex);
		return queues.size() - 1;	// the queue for everything outside the pool
	}

	void push(const Job &job, JobPriority priority, std::size_t queueIndex)
	{
		{
			std::lock_guard<std::mutex> lock(queues[queueIndex]->mutex);
			queues[queueIndex]->jobs[priority].push_back(job);
		}
		if (sleepingWorkers.load(std::memory_order_seq_cst) > 0)
		{
			// Take the lock briefly before notifying. Without it a notify_one() that lands
			// between the worker's check and its wait() is lost, and the worker sleeps although
			// work is queued. Together with the check in workerMain() (under the same lock):
			// either the worker sees the work, or it is already waiting and gets woken.
			std::lock_guard<std::mutex> lock(wakeMutex);
			wake.notify_one();
		}
	}

	/// Is there work anywhere? Only for the check right before going to sleep.
	bool hasAnyWork()
	{
		for (std::size_t i = 0; i < queues.size(); ++i)
		{
			std::lock_guard<std::mutex> lock(queues[i]->mutex);
			for (int priority = 0; priority < JOB_PRIORITY_COUNT; ++priority)
				if (!queues[i]->jobs[priority].empty())
					return true;
		}
		return false;
	}

	/// Fetches the next job: first from the own queue (back -- the most recently queued work,
	/// usually with a warm cache), then steals in turn (front, so thief and owner do not collide).
	/// Higher priorities first; priorities above `lowest` are not taken.
	bool popJob(Job &out, std::size_t preferredQueue, int lowest = JOB_PRIORITY_COUNT - 1)
	{
		const std::size_t n = queues.size();
		for (std::size_t attempt = 0; attempt < n; ++attempt)
		{
			const std::size_t index = (preferredQueue + attempt) % n;
			std::lock_guard<std::mutex> lock(queues[index]->mutex);
			for (int priority = 0; priority <= lowest; ++priority)
			{
				std::deque<Job> &deque = queues[index]->jobs[priority];
				if (deque.empty())
					continue;
				if (attempt == 0)
				{
					out = deque.back();		// own work from the back
					deque.pop_back();
				}
				else
				{
					out = deque.front();	// stolen work from the front
					deque.pop_front();
				}
				return true;
			}
		}
		return false;
	}

	void execute(const Job &job, bool byCaller)
	{
		JOB_ZONE("Job");
		// An exception from a block must not skip the pending counter, or the caller waits
		// forever in workUntilDone() -- the game hangs instead of showing the error. The engine
		// does throw (INIException, the memory pool when exhausted), and nobody on a worker thread
		// could catch it. So catch and report here, and always decrement the counter.
		try
		{
			if (job.standalone)
			{
				job.standalone();
			}
			else if (job.range != nullptr)
			{
				(*job.range)(job.begin, job.end);
			}
		}
		catch (...)
		{
			jobExceptions.fetch_add(1, std::memory_order_relaxed);
			Log::write(LOG_CAT_JOB, LOG_ERROR,
				"exception in job block [%u, %u) -- the block counts as done, its result "
				"is unusable", static_cast<unsigned>(job.begin), static_cast<unsigned>(job.end));
		}
		if (job.pending != nullptr)
			job.pending->fetch_sub(1, std::memory_order_acq_rel);
		blocksExecuted.fetch_add(1, std::memory_order_relaxed);
		if (byCaller)
			blocksByCallers.fetch_add(1, std::memory_order_relaxed);
	}

	/// Works until `pending` reaches 0. Waiting threads help, so jobs can nest without
	/// deadlocking the pool; they take jobs up to priority `lowest`.
	void workUntilDone(std::atomic<int> &pending, bool byCaller, std::size_t preferredQueue,
		int lowest = JOB_PRIORITY_COUNT - 1)
	{
		while (pending.load(std::memory_order_acquire) > 0)
		{
			Job job;
			if (popJob(job, preferredQueue, lowest))
				execute(job, byCaller);
			else
				std::this_thread::yield();	// all handed out, but not finished yet
		}
	}

	void workerMain(int index)
	{
		t_workerIndex = index;
		t_workerPool = this;

		const std::string name = std::format("Job worker {}", index);
		setThreadRole(THREAD_ROLE_WORKER, name.c_str());

		while (!stopping.load(std::memory_order_acquire))
		{
			Job job;
			if (popJob(job, static_cast<std::size_t>(index)))
			{
				execute(job, false);
				continue;
			}

			// Nothing to do: sleep until work is submitted or the pool ends.
			std::unique_lock<std::mutex> lock(wakeMutex);
			// Check again under the lock. Work may have been queued between popJob() above and
			// here, whose notify_one() would reach nobody -- the worker would sleep until the
			// timeout although the block could run right away. This check closes that window.
			if (hasAnyWork() || stopping.load(std::memory_order_acquire))
				continue;
			sleepingWorkers.fetch_add(1, std::memory_order_seq_cst);
			// The timeout is a safety net for the pool ending without a wakeup.
			wake.wait_for(lock, std::chrono::milliseconds(1));
			sleepingWorkers.fetch_sub(1, std::memory_order_seq_cst);
		}

		t_workerIndex = -1;
		t_workerPool = nullptr;
	}
};

JobSystem::JobSystem() : m_impl(nullptr), m_workerCount(0), m_verifyParallel(false)
{
}

JobSystem::~JobSystem()
{
	shutdown();
}

JobSystem &JobSystem::get()
{
	static JobSystem system;
	return system;
}

void JobSystem::start(int workerCount)
{
	if (workerCount < 0)
		workerCount = CpuTopology::get().recommendedWorkerCount();

	// Upper bound: the count comes from the command line (-workers) or Options.ini, which may
	// contain a typo. Without a cap "-workers 100000" would try to start 100000 threads and stall
	// the machine before any error message appears.
	const int maxWorkers = MAX_WORKER_COUNT;
	if (workerCount > maxWorkers)
	{
		ENGINE_LOG_WARNING(LOG_CAT_JOB, "Job system: %d workers requested, limited to %d",
			workerCount, maxWorkers);
		workerCount = maxWorkers;
	}

	if (m_impl != nullptr && m_workerCount == workerCount)
		return;		// already running like this

	shutdown();

	if (workerCount <= 0)
	{
		m_workerCount = 0;
		ENGINE_LOG_INFO(LOG_CAT_JOB, "Job system: serial mode (0 workers)");
		return;
	}

	m_impl = new Impl();
	m_workerCount = workerCount;

	// One queue per worker plus one for external submitters (main/sim thread).
	m_impl->queues.reserve(static_cast<std::size_t>(workerCount) + 1);
	for (int i = 0; i <= workerCount; ++i)
		m_impl->queues.push_back(new Impl::WorkerQueue());

	m_impl->threads.reserve(static_cast<std::size_t>(workerCount));
	for (int i = 0; i < workerCount; ++i)
	{
		Impl *impl = m_impl;
		m_impl->threads.push_back(std::thread([impl, i]() { impl->workerMain(i); }));
	}

	ENGINE_LOG_INFO(LOG_CAT_JOB, "Job system: %d workers started (%d logical processors)",
		workerCount, CpuTopology::get().logicalProcessors);
}

unsigned long long JobSystem::jobExceptionCount() const
{
	return (m_impl != nullptr) ? m_impl->jobExceptions.load(std::memory_order_relaxed) : 0;
}

void JobSystem::shutdown()
{
	if (m_impl == nullptr)
	{
		m_workerCount = 0;
		return;
	}

	m_impl->stopping.store(true, std::memory_order_release);
	m_impl->wake.notify_all();
	for (std::size_t i = 0; i < m_impl->threads.size(); ++i)
	{
		if (m_impl->threads[i].joinable())
			m_impl->threads[i].join();
	}
	// Jobs still queued (async or background) run here, so every tracked counter reaches 0 and
	// nobody waits forever for a job of a pool that no longer exists.
	{
		Job job;
		while (m_impl->popJob(job, m_impl->queues.size() - 1))
			m_impl->execute(job, true);
	}
	for (std::size_t i = 0; i < m_impl->queues.size(); ++i)
		delete m_impl->queues[i];

	delete m_impl;
	m_impl = nullptr;
	m_workerCount = 0;
}

bool JobSystem::isWorkerThread() const
{
	return m_impl != nullptr && t_workerIndex >= 0 && t_workerPool == m_impl;
}

void JobSystem::parallelFor(std::size_t count, std::size_t grain, const RangeFunction &fn,
	JobPriority priority)
{
	if (count == 0)
		return;
	if (grain == 0)
		grain = 1;

	// Reference mode and special cases: serial in the caller, in index order.
	if (m_impl == nullptr || m_workerCount == 0)
	{
		for (std::size_t begin = 0; begin < count; begin += grain)
		{
			const std::size_t end = (begin + grain < count) ? (begin + grain) : count;
			fn(begin, end);
		}
		return;
	}

	const std::size_t blockCount = (count + grain - 1) / grain;
	std::atomic<int> pending(static_cast<int>(blockCount));

	const std::size_t queueIndex = m_impl->queueIndexForCaller();
	for (std::size_t block = 0; block < blockCount; ++block)
	{
		Job job;
		job.range = &fn;
		job.begin = block * grain;
		job.end = (job.begin + grain < count) ? (job.begin + grain) : count;
		job.pending = &pending;
		m_impl->push(job, priority, queueIndex);
	}

	// The caller does not idle but processes blocks too -- frame jobs only, so it never picks up
	// a long async or background job while its own tick waits.
	m_impl->workUntilDone(pending, !isWorkerThread(), queueIndex, JOB_PRIORITY_FRAME);
}

void JobSystem::submitBackground(const std::function<void()> &fn)
{
	if (!fn)
		return;
	if (m_impl == nullptr || m_workerCount == 0)
	{
		fn();
		return;
	}
	Job job;
	job.standalone = fn;
	m_impl->push(job, JOB_PRIORITY_BACKGROUND, m_impl->queueIndexForCaller());
}

void JobSystem::submitTracked(const std::function<void()> &fn, std::atomic<int> &pending,
	JobPriority priority)
{
	if (!fn)
		return;
	if (m_impl == nullptr || m_workerCount == 0)
	{
		fn();
		return;
	}
	pending.fetch_add(1, std::memory_order_acq_rel);
	Job job;
	job.standalone = fn;
	job.pending = &pending;
	m_impl->push(job, priority, m_impl->queueIndexForCaller());
}

void JobSystem::waitFor(std::atomic<int> &pending)
{
	if (pending.load(std::memory_order_acquire) <= 0)
		return;
	if (m_impl == nullptr)
		return;		// without a pool nothing is pending: shutdown() ran every queued job
	m_impl->workUntilDone(pending, !isWorkerThread(), m_impl->queueIndexForCaller());
}

void JobSystem::setVerifyParallel(bool on)
{
	m_verifyParallel = on;
	ENGINE_LOG_INFO(LOG_CAT_JOB, "Verify mode for parallel sections %s", on ? "on" : "off");
}

unsigned long long JobSystem::verifyMismatchCount() const
{
	return m_verifyMismatches.load(std::memory_order_relaxed);
}

void JobSystem::reportVerifyMismatch(const char *section, std::size_t blockIndex)
{
	m_verifyMismatches.fetch_add(1, std::memory_order_relaxed);

	Log::write(LOG_CAT_JOB, LOG_ERROR,
		"parallel and serial results differ -- section '%s', block %u",
		(section != nullptr) ? section : "?", static_cast<unsigned>(blockIndex));
}

unsigned long long JobSystem::blocksExecuted() const
{
	return (m_impl != nullptr) ? m_impl->blocksExecuted.load(std::memory_order_relaxed) : 0;
}

unsigned long long JobSystem::blocksExecutedByCallers() const
{
	return (m_impl != nullptr) ? m_impl->blocksByCallers.load(std::memory_order_relaxed) : 0;
}

} // namespace Engine
