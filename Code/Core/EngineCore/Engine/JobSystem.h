/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// JobSystem.h -- worker pool and parallelFor.
//
// A small custom job system instead of a library, because of these requirements:
//
//   * **Fixed block size instead of "N threads".** `parallelFor(count, grain, fn)` splits the
//     work into blocks of `grain` elements. The split does NOT depend on the thread count; a
//     run with 2, 8 or 32 workers computes exactly the same blocks, so logic stays bit-identical.
//   * **`workers = 0` is the reference mode.** Everything runs serially in the caller, in index
//     order. Determinism tests compare against this mode.
//   * **Ordered reduction.** `parallelMapOrdered` collects one result per block and combines
//     them **in index order** in the calling thread, never in completion order.
//   * **Waiting threads help.** A thread waiting for results executes blocks itself, so jobs
//     can nest without deadlocking the pool.
//   * **Three priorities.** Frame jobs (sim/client, due this tick), async jobs (computed now,
//     needed a few ticks later) and background jobs (loading, decoding). Higher priorities are
//     always taken first. A thread waiting in parallelFor() helps only with frame jobs, so a long
//     async job never stalls the tick that is waiting for its blocks.
//   * **Async tasks with a deadline.** `AsyncTask` starts one job and is waited for later; the
//     waiter helps until it is done. The sim uses it with a fixed latency (start at tick N, take
//     over at tick N + k on every machine), so the result does not depend on thread timing.
//
// Workers register as THREAD_ROLE_WORKER so `ASSERT_NOT_WORKER()` in the logic fires.
//
// Depends on no engine header so the pool runs in unit tests without the game.

#pragma once

#ifndef ENGINE_JOBSYSTEM_H
#define ENGINE_JOBSYSTEM_H

#include <atomic>
#include <cstddef>
#include <functional>
#include <vector>

namespace Engine
{

enum JobPriority
{
	JOB_PRIORITY_FRAME = 0,			///< due this tick (sim and client work)
	JOB_PRIORITY_ASYNC,				///< due a few ticks later (AsyncTask)
	JOB_PRIORITY_BACKGROUND,		///< may wait (loading, decoding)
	JOB_PRIORITY_COUNT
};

/// Work function of a block: half-open range [begin, end).
using RangeFunction = std::function<void(std::size_t /*begin*/, std::size_t /*end*/)>;

class JobSystem
{
public:
	/// The process-wide pool.
	static JobSystem &get();

	/// Starts the pool. `workerCount`:
	///   < 0  default (logical processors - 2, at least 1; see CpuTopology)
	///   0    no pool -- everything runs serially in the caller (reference mode for determinism tests)
	///   > 0  exactly this many workers
	/// Starting again without shutdown() is a no-op if the count is unchanged.
	void start(int workerCount = -1);

	/// Stops all workers (waits for running blocks). Afterwards everything runs serially.
	void shutdown();

	/// Number of worker threads (0 = serial mode).
	int workerCount() const { return m_workerCount; }

	/// TRUE if the calling thread is a worker of this pool.
	bool isWorkerThread() const;

	/// Processes `count` elements in blocks of `grain` elements. Returns once all blocks are
	/// done; the caller helps meanwhile.
	void parallelFor(std::size_t count, std::size_t grain, const RangeFunction &fn,
		JobPriority priority = JOB_PRIORITY_FRAME);

	/// Submits a single background job (no waiting). Without a pool it runs immediately in the
	/// caller.
	void submitBackground(const std::function<void()> &fn);

	/// Submits one job whose completion is tracked by `pending` (incremented here, decremented
	/// when the job ends). Without a pool it runs immediately in the caller. Use AsyncTask.
	void submitTracked(const std::function<void()> &fn, std::atomic<int> &pending,
		JobPriority priority = JOB_PRIORITY_ASYNC);

	/// Returns once `pending` is 0; the caller executes queued jobs of any priority meanwhile.
	void waitFor(std::atomic<int> &pending);

	/// Verification mode. When on, `parallelMapOrdered` computes each section **additionally
	/// serially** and compares the block results. A mismatch is logged (category Job, level
	/// Error) and counted; the serial result is returned. Costs about twice the compute time, so
	/// it is meant for test runs only (`-verifyParallel`).
	void setVerifyParallel(bool on);
	bool isVerifyParallel() const { return m_verifyParallel; }
	/// Number of mismatches found so far between parallel and serial results.
	unsigned long long verifyMismatchCount() const;
	/// Reports a mismatch (used by parallelMapOrdered; the section name goes to the log).
	void reportVerifyMismatch(const char *section, std::size_t blockIndex);

	/// Number of blocks that ended with an exception. An exception from a job is caught and
	/// logged (nobody on a worker thread could catch it), and the block counts as done --
	/// otherwise the caller would wait forever.
	unsigned long long jobExceptionCount() const;

	/// Upper bound for the worker count. -workers and Options.ini are user input; without a cap
	/// a typo could flood the machine with threads.
	enum { MAX_WORKER_COUNT = 64 };

	/// Number of blocks executed so far (diagnostics and tests).
	unsigned long long blocksExecuted() const;

	/// Number of blocks the calling (non-pool) thread executed itself -- shows in tests that
	/// waiting threads really help.
	unsigned long long blocksExecutedByCallers() const;

	JobSystem();
	~JobSystem();

private:
	JobSystem(const JobSystem &);				// not copyable
	JobSystem &operator=(const JobSystem &);

	struct Impl;
	Impl *m_impl;
	int m_workerCount;
	bool m_verifyParallel;
	/// Mismatch counter. Lives here and **not** in Impl: the pool is recreated on every start()
	/// with a different worker count and on shutdown, which would otherwise discard exactly the
	/// finding the verification mode is looking for.
	std::atomic<unsigned long long> m_verifyMismatches{0};
};

/// One job started now and waited for later.
///
/// Typical use in the sim: start() at tick N with a function that reads only its own copied
/// input, wait() at tick N + k. The result is the same with 0 or 64 workers, because the job
/// sees only its input and the take-over tick is fixed. Without workers start() runs the job
/// right away. The destructor waits, so the job never outlives the data it writes to.
class AsyncTask
{
public:
	AsyncTask() = default;
	~AsyncTask() { wait(); }

	/// Starts `fn`. A task still running is waited for first.
	void start(const std::function<void()> &fn, JobPriority priority = JOB_PRIORITY_ASYNC)
	{
		wait();
		JobSystem::get().submitTracked(fn, m_pending, priority);
	}

	/// TRUE while the job is queued or running.
	bool isRunning() const { return m_pending.load(std::memory_order_acquire) > 0; }

	/// Returns once the job is done; helps with queued jobs meanwhile.
	void wait()
	{
		if (isRunning())
			JobSystem::get().waitFor(m_pending);
	}

private:
	AsyncTask(const AsyncTask &);				// not copyable
	AsyncTask &operator=(const AsyncTask &);

	std::atomic<int> m_pending{0};
};

/// Convenience accessor.
inline JobSystem &theJobSystem() { return JobSystem::get(); }

/// Processes `count` elements in blocks of `grain` (see JobSystem::parallelFor).
inline void parallelFor(std::size_t count, std::size_t grain, const RangeFunction &fn,
	JobPriority priority = JOB_PRIORITY_FRAME)
{
	JobSystem::get().parallelFor(count, grain, fn, priority);
}

/// Compute in parallel, combine serially.
///
/// Each block yields a result of type T; `map(begin, end)` computes it, `reduce(acc, r)`
/// combines it. Combining happens afterwards **in block order** in the calling thread, so the
/// result does not depend on which worker finished when and is bit-identical with 0, 1, 7 or
/// 31 workers.
template <typename T, typename MapFn, typename ReduceFn>
T parallelMapOrdered(std::size_t count, std::size_t grain, const T &initial,
	const MapFn &map, const ReduceFn &reduce, JobPriority priority = JOB_PRIORITY_FRAME,
	const char *sectionName = "ParallelMapOrdered")
{
	if (count == 0)
		return initial;
	if (grain == 0)
		grain = 1;

	const std::size_t blockCount = (count + grain - 1) / grain;
	std::vector<T> results(blockCount, initial);

	JobSystem::get().parallelFor(count, grain,
		[&](std::size_t begin, std::size_t end) {
			results[begin / grain] = map(begin, end);
		}, priority);

	// Verification mode: repeat the computation serially and compare result by result.
	if (JobSystem::get().isVerifyParallel())
	{
		for (std::size_t block = 0; block < blockCount; ++block)
		{
			const std::size_t begin = block * grain;
			const std::size_t end = (begin + grain < count) ? (begin + grain) : count;
			const T serial = map(begin, end);
			if (!(serial == results[block]))
			{
				JobSystem::get().reportVerifyMismatch(sectionName, block);
				results[block] = serial;	// the serial result wins
			}
		}
	}

	T accumulated = initial;
	for (std::size_t i = 0; i < blockCount; ++i)
		accumulated = reduce(accumulated, results[i]);
	return accumulated;
}

} // namespace Engine

#endif // ENGINE_JOBSYSTEM_H
